# foo_mmf 1.1

Yamaha SMAF playback for foobar2000 2.x on Windows 10 or later. The component uses original, user-supplied Yamaha MA7 or MA5 synthesizer libraries in a separate 32-bit process.

## Install

1. Install **`foo_mmf.fb2k-component`** through File > Preferences > Components > Install, then restart foobar. This single package includes both the x86 and x64 components; foobar selects the correct version automatically. ARM64EC foobar uses the x64 version.
2. Open **File > Preferences > Playback > Input > Yamaha SMAF**. Choose an engine and locate its library folder if necessary. **Automatic (prefer MA7)** is the default. Keep each Yamaha library set together, including its runtime DLLs. Yamaha binaries are not included in this component.
For **MA7**, an empty folder setting searches `C:\Program Files (x86)\Yamaha\MCP-MA7` on x64 or ARM64 Windows, and `C:\Program Files\Yamaha\MCP-MA7` on 32-bit Windows (using the system drive if different). The folder needs `M7_EmuSmw7.dll`, `SMAFMMS7EMU.dll`, `M7_EmuHw.dll`, `EMOJIMGR.dll` and their original runtime dependencies.
For **MA5**, select **MA5 (fall back to MA7)** and use the separate MA5 folder field. An empty field uses `C:\Program Files (x86)\Yamaha\MidRadio Player` on 64-bit Windows, or `C:\Program Files\Yamaha\MidRadio Player` on 32-bit Windows. The folder needs `M5_EmuSmw5.dll`, `SMAFMMS5EMU.dll` and `M5_EmuHw.dll`; the Smaf.dll add-in is not required. Both the older MidRadio set and the supplied newer set have been tested. Select the folder containing the version you want; avoid mixing DLLs from different releases.

3. You can start playing! The component supports `.mmf`, `.mld`, `.mid` or `.midi` files, with each format registering a separate decoder. If you also have **foo_midi** or another decoder for these extensions, open **File > Preferences > Playback > Decoding** and order **Yamaha MMF decoder**, **Yamaha MLD decoder** and **Yamaha MIDI decoder** independently against competing decoders. This affects MMF as well as MIDI: foo_midi also handles MMF. Put your other decoder first when you prefer its rendering, then restart playback. For screen reader/keyboard users, open the context menu (Shift+F10/Applications key) on a decoder in the list to find the move up/down commands.


MA5 gives some songs a different sound because some patches differ from MA7. If you listen to a ringtone created for a device using a MA5 chip and it sounds different to how you remember, change the Playback Engine setting to "MA5." If it cannot load a file, the component tries MA7 when its libraries are available. Both tested MA5 versions reject the MA7 and MLD regression fixtures and standard MIDI, so those use MA7 fallback. **Automatic** also uses MA5 when MA7 is missing or cannot open the file. Selecting **MA7** uses only MA7. File properties report the engine that actually loaded the song. Engine/path changes apply to newly opened sessions.

The older MA5 libraries reject modern Windows version numbers. The component applies Windows' version-reporting compatibility fix only to MA5 helper processes; it does not alter your system compatibility settings.

## Playback and settings

- **Sample rate**, under **File > Preferences > Playback > Input > Yamaha SMAF**, selects **44,100 Hz** or **48,000 Hz**, defaulting to **48,000 Hz**. Both MA7 and both tested MA5 releases support these rates. The selected rate applies to every file when playback or conversion starts a new session. Foobar may still display a previously cached rate; use **Properties > Tools > Reload info from file(s)** on affected tracks to refresh it.
- MMF text metadata, including title, artist, composer and copyright, appears in foobar's tags when present. Files already in the library may need **Properties > Tools > Reload info from file(s)** to refresh cached metadata. Tags are read only; source files are unchanged.

- Supports supplied MA2, MA3, MA5 and MA7 samples, MLD, and standard MIDI.
- **Default Yamaha volume**, in preferences, ranges from **0 to 127** and defaults to **64**. It controls the synthesizer before its PCM output clips. Lower it for loud songs: reducing foobar's own volume cannot repair clipping that has already occurred inside Yamaha. Some files may need a value below the default.
- **Enable Yamaha reverb** and **Enable Yamaha chorus for MID and MIDI files** are independent preferences, both **on** by default, preserving the file's effect settings. Uncheck either to disable that effect. Changes apply when a file is next opened and also apply to conversion. MMF/MLD effects are unchanged. Disabled effect sends are suppressed in memory; source files are never edited.
- **View > SMAF control** has labelled spin boxes for tempo (**70-130%**), transposition (**-12 to +12 semitones**) and Yamaha volume (**0-127**). Type a value or use Up/Down; valid changes apply after **350 ms** without further edits. There is no Apply button. Incomplete or out-of-range values leave the previous settings active.
- Tab and Shift+Tab navigate the controls. Alt+T, Alt+P and Alt+V select the fields; Alt+R resets; Enter and Escape close the window. Reset restores normal tempo/pitch and the volume currently saved in preferences. Closing also applies any pending valid edit.
- Temporary controls apply across tracks until foobar exits. Saved preferences initialize new decoders; a temporary volume override takes precedence during playback. Conversion, integrity testing and canonical decoding use normal tempo/pitch and the saved volume. Changes become audible after foobar's existing output buffer plays out.
- Preferences provide **0-254** extra whole-song repeats and a **0-30 second** fade over the end of the last repeated pass. Defaults are no extra repeats and a five-second repeat fade. Foobar's no-loop flag disables extra repeats.
- Ordinary playback preserves the natural release/reverb tail until it settles, with a ten-second tail limit. The final silence used to confirm the end is discarded, removing the previous quarter-second gap when repeating dry tracks in foobar. End positions retain 10 ms rendering resolution. Metadata reports nominal song duration; audible release tails can extend playback.

Playback uses Yamaha's fast native seek without restarting the helper. Native seeks restore musical position with millisecond precision; they do not reproduce the exact waveform or accumulated effect tail of an uninterrupted render. Conversion and analysis keep sample-accurate seeking by rendering skipped audio inside the helper in batches.

The component keeps up to two idle helpers and their Yamaha DLLs loaded for reuse across tracks and metadata reads. Concurrent decoders get separate helpers. Idle helpers do not render audio and close when foobar exits; failed or cancelled helpers are discarded. Chip and effect state is reinitialized between files to prevent one song affecting the next.

MA5 can occasionally freeze inside Yamaha's libraries. If loading makes no progress for **five seconds**, the component terminates that helper and retries once in a fresh process. If the retry also stalls, it uses MA7 when available; otherwise it reports an error so the file scan can continue. **View > Console** records the filename, stalled stage, elapsed time and recovery action. Cancellation remains available during the wait. This also covers metadata reads and conversion opens. A complete helper request has a 30-second limit even if it reports intermediate progress.

## Build and test

Place the foobar SDK at `sdk/`. Current builds use SDK 2025-03-07 and Visual Studio 2026 with the C++ desktop workload, toolset v145 and a Windows SDK. SDK files are not modified; build output stays under `build/`. No WTL dependency is required. Yamaha libraries and songs are needed only for runtime tests, not compilation or packaging.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File package.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\Test-Package.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Project bridge_tests.vcxproj -Platform x64
$repo = (Get-Location).Path
& .\build\x64\Release\bridge_tests.exe "$repo\build\Win32\Release\smaf_helper.exe" "$repo\MCP-MA7" "$repo\test files"
```

The package layout follows the [foobar component packaging specification](https://wiki.hydrogenaudio.org/index.php?title=Foobar2000:Development:Overview): root `foo_mmf.dll` is x86, `x64/foo_mmf.dll` is x64, and the x86 helper is shared at the root.

`build.ps1` without arguments builds the x86 WAV probe:

```powershell
build\Win32\Release\smaf_probe.exe library-folder song.mmf output.wav
```

Optional probe arguments, in order, are tempo, semitones, extra repeats, volume, seek position in milliseconds, MIDI reverb (0/1), engine (5/7), MIDI chorus (0/1), and sample rate (44100/48000). The probe defaults to volume 64, both effects on, engine 7 and 44100 Hz. For the older MA5 set, launch the probe with the process-local environment variable `__COMPAT_LAYER=WinXPSP2VersionLie`; the component's bridge handles this automatically.

Pass two additional absolute folder arguments (older MA5, newer MA5) to `bridge_tests.exe` to run both MA5 suites. The real-host suite expects the newer set under `newer_ma5/` and the older set at the default MidRadio installation path.

To measure library loading without foobar or IPC, run `smaf_probe.exe DLL-folder --benchmark input.mmf 5` (use 7 for MA7). It reports initialization, the first open and five warm opens. Each warm open resets song/chip state while retaining the process and DLLs. Older MA5 libraries can load more slowly than newer releases. Foobar buffering and helper launch time are additional.

Bridge tests cover both client architectures, hot file switching, exact/native seeks, clipping, MIDI effects, concurrent sessions, controls, cancellation, malformed input, missing libraries and helper crashes. Build `metadata_tests.vcxproj` to test the metadata reader without Yamaha DLLs; pass an optional MMF folder to check your fixtures. Use `tools/Host-Test.ps1 -Package -Features` for metadata, both sample rates and the loop regression cases in foobar. The named fixtures expected by these optional tests are listed in the test sources and are not redistributed. Build `foo_mmf_tests.vcxproj` and use `tools/Host-Test.ps1 -Package -Foobar path-to-foobar` to test packaged binaries in a disposable portable foobar copy, including native keyboard controls and MSAA labels. Never install the test component into your normal profile. See [DEVELOPMENT.md](DEVELOPMENT.md) and [DEVELOPMENT_NOTES.md](DEVELOPMENT_NOTES.md) for contributor instructions.

Use `tools/Host-Test.ps1 -Package -Recovery` for controlled MA5 stalls: the test suspends only its own helpers and checks fresh retry, repeated-stall fallback, missing MA7, console messages and cancellation. Build `load_stress.vcxproj` for Win32, then run `load_stress.exe helper DLL-folder song-folder passes` with absolute paths. It accepts your own MMF files, repeatedly opens them in shuffled order, logs timing and discards stalled workers before continuing.

The component's own code is MIT licensed. Package builds include the foobar SDK and PFC license notices. Yamaha DLLs remain separate and are not redistributed.
