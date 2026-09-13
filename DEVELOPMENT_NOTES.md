# Development notes

## Test prerequisites

Building requires the foobar SDK, C++ compiler and Windows SDK described in DEVELOPMENT.md. Running audio tests also requires your own Yamaha libraries and fixtures; neither is redistributed. The named-pipe bridge requires a normal local Windows process token. Keep production pipe permissions intact when testing.

## Bridge tests

Build `bridge_tests.vcxproj` for x64 and Win32 with `build.ps1`. Run the resulting executable with absolute helper, MA7-library and fixture-folder paths:

```powershell
.\build.ps1 -Project bridge_tests.vcxproj -Platform x64
$repo = (Get-Location).Path
& .\build\x64\Release\bridge_tests.exe "$repo\build\Win32\Release\smaf_helper.exe" "$repo\MCP-MA7" "$repo\test files"
```

Append older-MA5 and newer-MA5 library folders to run both MA5 suites. The named fixture set expected by integration tests is listed in `tests/bridge_tests.cpp` and `tests/host_tests.cpp`. These tests are optional when those files are unavailable. Synthetic MIDI is created by the tests.

Coverage includes nonzero PCM, warm/fresh consistency, concurrent decoders, exact/native seeking, volume and effects, repeat/EOF behavior, malformed inputs, cancellation, missing libraries and helper crashes. MA5 tests also check engine differences and unsupported formats.

## Real foobar hosts

Build `foo_mmf_tests.vcxproj` for the selected architecture. Run `tools/Host-Test.ps1 -Package` with `-Foobar` pointing to a matching foobar installation or extracted distribution and `-Platform x64` or `Win32`.

The script creates a disposable portable copy under `analysis/`, leaving the source installation and its profile untouched. It expects `MCP-MA7/`, `newer_ma5/` and `test files/` under the repository root, plus an older MA5 set in the default MidRadio installation. Do not install the test component in a normal profile. It is never included in release packages.

Add `-Recovery` for controlled loading stalls. The test suspends only its own MA5 helpers, then checks one fresh-process retry, repeated-stall MA7 fallback, missing fallback, worker termination, console messages and prompt cancellation. Run host suites serially without simultaneous bridge stress so UI timer checks remain meaningful. Missing or stale result files must not count as a pass.

## General loading stress and rendering

The standalone tools can use your own songs without the named regression fixture set:

```powershell
.\build.ps1 -Project load_stress.vcxproj -Platform Win32
# Supply absolute helper, MA5-library and song-folder paths.
.\build\Win32\Release\load_stress.exe helper-path ma5-folder song-folder 10
.\build.ps1
.\build\Win32\Release\smaf_probe.exe library-folder song.mmf output.wav
```

The stress tool repeatedly opens shuffled MMF files, records timing and discards failed workers before continuing. A nonzero exit indicates a failure or timeout. The probe renders WAV or measures initialization and warm opens with `library-folder --benchmark song.mmf 5` (7 for MA7). Older MA5 libraries require the process-local `__COMPAT_LAYER=WinXPSP2VersionLie` environment when using the direct probe; the bridge applies this itself.

## Maintenance notes

- IPC version 5 uses fixed-width messages and transfers audio, file bytes and folder strings. Never pass pointers between processes. Update both component architectures and the helper together after protocol changes.
- Keep the MA7 and MA5 adapters separate: their initialization, control argument conventions and PCM layouts differ. Retain the existing hardware reset and complete engine reinitialization between files. Validate adapter changes with rendered audio, not just DLL loading.
- Older MA5 libraries require the child-only Windows version compatibility setting. Do not change foobar's environment or persistent system compatibility settings.
- MA5 position may stop one millisecond before nominal duration. Position polling is amortized over 100 ms of audio and confirmed near EOF. Preserve finite repeats and release tails.
- Native playback seeks preserve musical position, not accumulated effect history. Conversion seeks must remain sample-exact and use saved volume with normal tempo/pitch.
- MIDI effects tests cover all four reverb/chorus combinations, running status, controller/system resets, source preservation and restoration across warm opens.
- Playback controls use native spin buttons, labelled edit buddies and a 350 ms debounce. Enter, Escape and Close flush valid pending edits. Conversion must not inherit temporary playback controls.
- Normal loading is quiet. Timeout diagnostics include the current filename, phase, helper PID and elapsed time. Recovery remains bounded, with cancellation bypassing retries.

## Release validation

The pre-release component passed packaged x86/x64 host suites, bridge suites with MA7 and both MA5 releases, controlled recovery tests and package architecture checks. Manual testing confirmed NVDA accessibility, ARM64EC playback and corrected ARM64 MA7 automatic discovery.

Version 1.0 preserves those playback features and prepares the source and tooling for distribution. Yamaha libraries, SDK sources, fixtures, generated audio and local test output remain excluded from Git and packages. A broader compatibility corpus and controlled comparisons with Yamaha's players remain future work.

The 1.0 package builds for both component architectures and passes architecture/content and portable-symbol-path validation. The validator also rejects a deliberately nonportable symbol record. The direct x64 bridge suite passes with MA7 and both MA5 releases, and the simplified loading stress tool completed a full fixture pass without errors. Both foobar host architectures produced passing runs.

Repeated host runs also exposed intermittent strict PCM equality failures around saved/temporary volume and live-control isolation, similar to failures seen before release preparation. A captured mismatch was substantial, not just floating-point rounding. The cause remains unresolved; a passing rerun is not proof of consistent behavior. Keep the strict assertions and their mismatch diagnostics. The release preparation changes do not modify synthesis or playback-control logic. Investigating this intermittent behavior remains follow-up work.
