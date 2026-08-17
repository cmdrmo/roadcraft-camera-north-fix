# Root cause and validation notes

Investigation date: 2026-08-17

## Symptom

Near North, horizontal camera yaw can jump for approximately one frame and then
return through normal smoothing. Pitch does not change. Both directions occur,
commonly near 90 degrees. Vehicle rotation alone can trigger it in free-camera
mode, and camera/vehicle alignment changes its probability.

Reducing the camera slerp rate transformed the twitch into a rotational impulse
or full-circle recovery. This was the first strong indication that the problem
was an angle interpolation path rather than input or collision response.

## Interpolation data flow

The relevant path computes two adjacent camera components and later stores them
as a qword. The yaw component is formed as follows:

```text
0x1409E60DB  movss xmm1, [rbx+0x5C]       ; desired yaw
0x1409E60E0  subss xmm3, xmm0             ; smoothing factor
0x1409E60E4  subss xmm1, [rdi+0x04]       ; desired - current
0x1409E60F2  mulss xmm1, xmm3
0x1409E60FA  addss xmm1, [rdi+0x04]
0x1409E6103  movss [rsp+0x124], xmm1
0x1409E611B  mov rax, [rsp+0x120]
0x1409E6123  mov [r14+0x04], rax           ; paired-float store
```

At this point `[rdi+0x04]` is controller local yaw at `+0x68`. The subtraction
does not normalize its cyclic difference before scalar interpolation.

The final target writer later uses:

```text
target_yaw(+0x54) = local_yaw(+0x68) + vehicle_yaw(+0x114A8)
```

Direct searches for float writes originally missed the qword store and led to
an early interpretation that the overlapping high dword was accidental. The
paired store is better understood as a compiler-generated two-float copy. The
actual defect is the unwrapped subtraction that produces its yaw candidate.

## Transition-frame proof

A transition probe captured two independent entries into a persistent winding
mismatch. In both, desired yaw remained continuous while current local yaw
changed by approximately one negative turn.

```text
previous desired:    1.470
previous current:    1.021
current desired:     1.166
current current:  -359.271
raw delta:          360.437
wrapped delta:        0.437
```

```text
previous desired:   12.832
previous current:   11.557
current desired:    13.098
current current:  -348.355
raw delta:          361.454
wrapped delta:        1.454
```

Afterward, the raw delta remained near `360 degrees` for thousands of frames
while the equivalent wrapped delta remained near zero.

A separate persistent state reached a maximum raw delta of `1085.820 degrees`,
equivalent to only `5.820 degrees` after subtracting three complete turns.

## Failed v0.1 intervention

The first public experiment detected a large final target discontinuity at RVA
`0x9E6123` and retained an equivalent previous local yaw. It prevented ordinary
transient twitches, but in a persistent winding mismatch it rejected every
final write and produced horizontal camera lock while pitch remained available.

One reproduced lock session recorded 2,679 consecutive interventions at roughly
95-107 camera updates per second. Restoring the original bytes unlocked the
camera. This established that destination-side holding was not a complete fix.

## v0.2 wrapped-delta intervention

v0.2 hooks the five-byte subtraction at RVA `0x9E60E4`, executes it, and wraps
the resulting `xmm1` delta into `[-180,+180]`. The original smoothing, paired
write, state byte, and downstream target calculation then execute normally.

The hook preserves CPU flags that remain live across this region. It does not
modify pitch, reject final writes, retain a destination value, or use a shared
replacement-angle scratch field.

## Validation

Four complete enable/restore sessions observed:

| Session | Path writes | Wrapped frames |
| --- | ---: | ---: |
| 1 | 57,481 | 42,522 |
| 2 | 15,472 | 11,444 |
| 3 | 6,672 | 3,949 |
| 4 | 2,492 | 240 |
| **Total** | **82,117** | **58,155** |

No twitch or horizontal lock was observed while v0.2 was enabled. Both wrap
directions were exercised in one session (`+340 / -3407`) without visible
failure. Persistent per-frame wrapping also remained stable.

When the original subtraction was restored during a persistent mismatch, the
camera twitched immediately. Restoring it after a transient run had ended did
not twitch. This provides a direct A/B link between the unwrapped subtraction
and the visible failure.

All tested stop operations restored the original hook and relay bytes, released
the temporary allocation, and left the game responsive.

## Remaining limits

The workaround is validated only for Steam build `23930923` on the original
investigator's system. It is intentionally guarded by exact instruction and
relay-byte checks. Wider hardware and long-duration community testing remain
useful, and every future game build must be revalidated before offsets are
updated.
