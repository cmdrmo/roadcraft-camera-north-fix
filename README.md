# RoadCraft Camera North Fix

## Official camera fix shipped in RoadCraft 7.2

Saber released Update 7.2 on September 10, 2026. The [official patch notes](https://steamcommunity.com/games/2104890/announcements/detail/686389990174753840) list **“Fixed camera jerking at certain settings.”** Update the game and use the official fix first.

**This workaround is for the old 7.1 build only. It has not been ported to 7.2 and should not be used with 7.2 or later.** The source, release and original investigation remain available as a historical reference.

Brief local checks on 7.2 (Steam build `25119957`) did not reproduce the reported twitch. Static inspection also found changes to upstream camera reference-frame/settings handling, while the original unwrapped interpolation remains. We have not conclusively established that the official fix covers every North-twitch trigger documented here, or identified its exact causal implementation.

An experimental, reversible runtime fix for the horizontal camera twitch that
can occur near North in RoadCraft.

The visible failure is usually a one-frame yaw jump followed by the normal
camera smoothing pulling the view back. Pitch is unaffected. Both jump
directions occur, and lowering the camera slerp rate turns the brief twitch into
a rotational impulse or full-circle recovery.

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
5. Click **Stop and restore**, or close the window normally, to remove the
   runtime patch.

The fixer does not modify game files or save data. Restarting RoadCraft requires
enabling it again.

Do not force-terminate the fixer while its patch is active. If restoration ever
fails, leave the fixer open and exit RoadCraft; process exit discards all
temporary memory.

## What v0.2 fixes

RoadCraft stores yaw as an accumulating angle, so equivalent headings can be
represented on different 360-degree winding layers. The affected camera path
interpolates those values using ordinary scalar subtraction:

```text
delta  = desired_yaw - current_yaw
result = current_yaw + delta * smoothing_factor
```

When one state is `1 degree` and the other is `-359 degrees`, ordinary
subtraction produces a full-circle `360-degree` delta even though both headings
are equivalent. v0.2 replaces only that subtraction at RVA `0x9E60E4`. It
executes the original instruction and normalizes the delta into `[-180,+180]`
before the game's original smoothing and writeback continue.

```text
raw delta 360.437 degrees  ->  wrapped delta 0.437 degrees
raw delta 1085.820 degrees ->  wrapped delta 5.820 degrees
```

See [docs/root-cause.md](docs/root-cause.md) for the disassembly, transition
captures, failed v0.1 intervention, and validation results.

## Understanding the counters

`Wrapped frames` is not a count of visible camera twitches. It counts frames in
which two equivalent yaw states occupy different winding layers. The counter
may increase continuously while the camera remains completely stable; this is
expected.

`Current run` returns to zero after a transient boundary crossing. It continues
to grow when the game retains a persistent winding mismatch. Both positive and
negative wrap directions have been observed and corrected without visible
twitching or horizontal lock.

## Known limitations

- This is an unofficial community workaround, not a Saber Interactive or Focus
  Entertainment product.
- It opens the RoadCraft process and temporarily writes executable memory.
  Antivirus products may flag that behavior even though the source is public.
- It is intentionally build-specific. A future game update will normally show
  **version mismatch** until the code is revalidated.
- If the fix is stopped during a persistent winding mismatch, the first frame
  restored to RoadCraft's original subtraction may visibly twitch once. This
  does not damage the game; enabling the fix again resumes wrapped interpolation.
- Testing so far has been performed by the original investigator on one system.

The v0.1 destination guard could enter a feedback loop and lock horizontal
camera movement. It has been replaced by the source-level wrapped-delta fix in
v0.2 and should no longer be used.

## Building

The build uses MinGW-w64, CMake, Ninja, GNU assembler, and `objcopy`:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_CXX_COMPILER=g++ `
  -DCMAKE_ASM_COMPILER=gcc `
  -DCMAKE_RC_COMPILER=windres `
  -DCMAKE_OBJCOPY=objcopy
cmake --build build
```

The result is a statically linked single-file Win32 executable:
`build/RoadCraftCameraFixUI.exe`.

## Reporting results

Please include the Steam build ID, whether version validation passed, the UI
counters, and whether the camera remained stable across vehicles, camera modes,
and scene transitions. Do not upload game binaries, save files, or crash dumps.

## License

MIT. See [LICENSE](LICENSE).
