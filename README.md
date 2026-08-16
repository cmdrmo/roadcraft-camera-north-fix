# RoadCraft Camera North Fix

An experimental, reversible runtime fix for the horizontal camera twitch that
can occur near North in RoadCraft.

The visible failure is usually a one-frame yaw jump followed by the normal
camera smoothing pulling the view back. Lowering the game's camera slerp rate
made the same failure look like a rotational impulse, which made the faulty
state transition easier to trace.

## Compatibility

- RoadCraft on Steam
- Steam build ID `23930923`
- File version `7.1.PATCH.702788 Art: 702855`
- Windows x64

The fixer checks the exact bytes at both patch locations before doing anything.
If a game update changes them, it refuses to install instead of guessing.

## Usage

1. Start RoadCraft and enter the game.
2. Run `RoadCraftCameraFixUI.exe`.
3. Confirm that the UI says the game bytes match.
4. Click **Enable fix**.
5. Before closing the fixer, click **Stop and restore**, or close the window
   normally and let it restore the original bytes.

The fixer does not modify game files or save data. The patch exists only in the
current game process. Restarting RoadCraft requires enabling it again.

Do not force-terminate the fixer while the patch is active. If restoration ever
fails, leave the fixer open and exit RoadCraft; process exit discards all
temporary memory.

## What it fixes

The bad local-yaw value is produced by an overlapping 64-bit copy on the normal
camera update path:

```text
Roadcraft - Retail.exe+0x9E611B  mov rax, [rsp+0x120]
Roadcraft - Retail.exe+0x9E6123  mov [r14+0x04], rax
Roadcraft - Retail.exe+0x9E6127  mov byte ptr [r15], 1
```

At this site, `r14 == camera_controller + 0x60`. The low dword goes to `+0x64`,
while the high dword unintentionally becomes the horizontal local-yaw state at
`+0x68`. This is why searches limited to direct float stores missed the writer.

When that candidate would create a circular world-yaw discontinuity above 20
degrees, the fix still performs the low-dword write but replaces the high dword
with the previous yaw normalized into `[-180, +180]`. For example, `359.7` is
rebased to the equivalent `-0.3`. This preserves the camera direction without
freezing horizontal movement.

See [docs/root-cause.md](docs/root-cause.md) for the observations and A/B test
results.

## Safety and limitations

- This is an unofficial community workaround, not a Saber Interactive product.
- It opens the RoadCraft process and temporarily writes executable memory.
  Antivirus products may flag that behavior even though the source is public.
- It is intentionally build-specific. A future game update will normally show
  **version mismatch** until the offsets and surrounding code are revalidated.
- It has only been tested by the original investigator so far.
- No network access or telemetry is implemented. A UTF-8 log is written beside
  the executable.

## Building

The current build uses MinGW-w64, CMake, Ninja, GNU assembler, and `objcopy`:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_CXX_COMPILER=g++ `
  -DCMAKE_ASM_COMPILER=gcc `
  -DCMAKE_RC_COMPILER=windres `
  -DCMAKE_OBJCOPY=objcopy
cmake --build build
```

The output is a statically linked single-file Win32 executable:
`build/RoadCraftCameraFixUI.exe`.

## Reporting results

Please include:

- Steam build ID and game file version;
- whether the UI accepted or rejected the version bytes;
- the `writes`, `anomalies`, and `rebased` counters;
- whether the twitch was absent while enabled and returned after restoration.

Please do not upload game binaries, save files, or crash dumps.

## License

MIT. See [LICENSE](LICENSE).
