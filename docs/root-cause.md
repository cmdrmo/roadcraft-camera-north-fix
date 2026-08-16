# Root cause and validation notes

Investigation date: 2026-08-17

## Symptom

Near North, the horizontal camera yaw sometimes jumps for approximately one
frame and then returns through the game's normal smoothing. Pitch does not
change. Both leftward and rightward jumps occur, commonly near 90 degrees.

Reducing the camera slerp rate transformed the brief twitch into a visible
rotational impulse and full-circle recovery. Vehicle rotation alone could
trigger it in free-camera mode, and time spent near camera/vehicle alignment
changed the probability.

## Localization

The final target-yaw writer computes:

```text
target_yaw(+0x54) = local_yaw(+0x68) + vehicle_yaw(+0x114A8)
```

During real triggers, the vehicle value remained continuous while local yaw
changed by roughly 160 degrees between an earlier continuous writer and this
final calculation.

The missing writer was an overlapping qword copy at RVA `0x9E6123`:

```text
0x1409E611B  mov rax, [rsp+0x120]
0x1409E6123  mov [r14+0x04], rax
0x1409E6127  mov byte ptr [r15], 1
```

Here `r14 == camera_controller + 0x60`, so the high half of the qword overwrites
the float at `camera_controller + 0x68`.

A direct observer recorded 6,628 normal-path writes and eight bad candidates in
one trigger run. The maximum circular discontinuity was 163.555 degrees. An
alternate overlapping writer at RVA `0x9E6138` executed zero times during a
separate 60-second high-probability test.

## Rejected intervention

Simply retaining the previous high dword caused a feedback latch: 5,164 of
6,419 writes were rejected, and horizontal camera movement intermittently
locked while vertical movement remained available.

## Canonical rebase

For candidates whose resulting circular world-yaw discontinuity exceeds 20
degrees, the working fix:

1. performs the original low-dword write;
2. replaces the high dword with the previous local yaw normalized into
   `[-180, +180]`;
3. performs the original state-byte write.

This keeps the same circular direction while giving subsequent updates a
canonical representation, avoiding the feedback latch.

In a bounded test, the hook observed 6,587 writes and corrected 20 independent
events. The maximum bad candidate was 161.299 degrees; the circular error of the
replacement was approximately zero for every event.

## Standalone UI A/B test

Two complete enable/restore cycles produced:

| Cycle | Path writes | Bad candidates | Rebases | Operator result |
| --- | ---: | ---: | ---: | --- |
| 1 | 6,735 | 15 | 15 | Twitch absent while enabled; returned after restore |
| 2 | 2,608 | 7 | 7 | Twitch absent while enabled; returned after restore |

The standalone fixer therefore corrected 22 additional independent anomalies.
It was also successfully applied to a fresh game process after restarting
RoadCraft.

These results establish a strong causal link for the tested build; they do not
prove compatibility with later builds or every hardware/configuration variant.
