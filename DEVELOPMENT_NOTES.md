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

An optional SPF suite runs when `music1_1.spf` (any SPF) is in the fixture folder: open at both rates, nonzero PCM and declared length, reopen determinism, SPF/MMF/SPF mode switching in one hot helper, ignored tempo/transposition, volume zero and monotonic volume, exact and native seeks, one extra repeat, truncated input and (with an MA5 folder) rejection on MA5. Its expectations are structural and have not yet been run against real libraries. Coverage includes nonzero PCM, warm/fresh consistency, concurrent decoders, exact/native seeking, volume and effects, repeat/EOF behavior, malformed inputs, cancellation, missing libraries and helper crashes. MA5 tests also check engine differences and unsupported formats. Sample-rate tests switch 48/44.1/48 kHz in one helper, compare exact seeks and song duration, measure synthetic MIDI pitch and check that silent tail removal preserves audible effects.

## Real foobar hosts

Build `foo_mmf_tests.vcxproj` for the selected architecture. Run `tools/Host-Test.ps1 -Package` with `-Foobar` pointing to a matching foobar installation or extracted distribution and `-Platform x64` or `Win32`.

The script creates a disposable portable copy under `analysis/`, leaving the source installation and its profile untouched. It expects `MCP-MA7/`, `newer_ma5/` and `test files/` under the repository root, plus an older MA5 set in the default MidRadio installation. Do not install the test component in a normal profile. It is never included in release packages.

Add `-Recovery` for controlled loading stalls. The test suspends only its own MA5 helpers, then checks one fresh-process retry, repeated-stall MA7 fallback, missing fallback, worker termination, console messages and prompt cancellation. Run host suites serially without simultaneous bridge stress so UI timer checks remain meaningful. Missing or stale result files must not count as a pass.

Add `-Features` for MMF title/copyright, both sample rates with both engines, exact/native seeks, preference keyboard navigation and consecutive complete decodes of the three loop fixtures. This suite also needs `test files/meta/GuitarMan.mmf`. It tests decoder EOF, not the output-device timing of foobar's Repeat (track) command.

Add `-Corpus` for all MLD files recursively under `test files/n900i`, including `Unlocked`. Eight concurrent decoders run each file in conversion and integrity-test modes at 48 kHz, checking EOF and seeks beyond/back from EOF. This suite has a five-minute host deadline and bounded per-file rendering; its results must be fresh like the other host suites.

Add `-Endpoints` for the four graphics-bearing MMF endpoint regressions: `BoyMeetsGirl-Q-UTF8EN.mmf`, `BoyMeetsGirl-QQ-UTF8EN.mmf`, `ChuteDePluie-Q-UTF8EN.mmf` and `EatMyHeart-QQ-UTF8EN.mmf`, directly under `test files/`. It runs each with newer MA5 and MA7, both rates, and playback/conversion/integrity flags, then tests seeks beyond and back from EOF. It requires the selected engine to succeed without fallback and has a five-minute host deadline. These were supplied as newer-MA5 fixtures; after private graphics filtering the older MA5 release also accepts and fully renders all four at both rates.

Add `-CapturePcm` to the standard suite to repeat the volume-control comparisons 20 times and save the first failing pair under `analysis/pcm-captures/<PID>-<tick>/`. `actual.wav` and `reference.wav` are unnormalized stereo float WAVs; `capture.txt` records the assertion, source fixture, sample rate and sample counts. The assertions still fail normally. For the negative assertion that temporary volume should change the audio, identical actual/reference clips demonstrate the missing change rather than a corrupted waveform.

## Metadata tests

`tests/spf_checks.hpp` (SPF content detection, truncated/oversized chunks, mutation fuzzing and the Phrase volume mapping) has no Windows dependency and also runs from `metadata_tests`. Build `metadata_tests.vcxproj` for x64 and Win32. Run the executable without arguments for synthetic CNTI/OPDA encoding, escaping, malformed/truncated input and mutation tests. An optional folder argument, such as `"test files\meta"`, checks supplied metadata fixtures. These tests need no Yamaha DLLs or foobar installation.

The text layout and tag mapping follow Yamaha's [SMAF Specification 3.05, sections 4.2 and 4.3](https://img.atwiki.jp/mmfuta/attach/19/78/SMAF3.05.pdf). CNTI carries escaped comma-delimited options; OPDA Dch fields have 16-bit byte lengths. Dch text overrides earlier CNTI values. Unicode defaults to big endian unless a BOM selects otherwise. Windows converts the supported legacy encodings; unknown encodings, TCVN-6 and binary Dch data are omitted. Category remains CATEGORY rather than being guessed as a musical genre.

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

Build `render_stress.vcxproj` to stress complete renders through the bridge. Arguments are helper path, DLL folder, song folder, engine (5/7), sample rate (44100/48000), passes and worker count (1..16). It recursively shuffles MMF, MLD and MID files, reuses a helper per worker, checks finite PCM and fails if EOF is missing beyond nominal duration plus 11 seconds. Each file also has a 120-second test deadline. Choose files supported by the selected engine; this tool does not implement component fallback.

Build `end_tests.vcxproj` for targeted endpoint regressions. Arguments are helper path, DLL folder, fixture folder and engine (5/7). The four `-Endpoints` fixtures above are required. It checks full duration at both rates, tempo 70/100/130, exact reconstruction, native seeks into and back from EOF, and finite repeats with and without a fade. For newer MA5 it also plays BoyMeetsGirl-Q from the beginning with paced requests and a deliberate 750 ms pause near the end, checking the final two note windows against fast output. Use the newer MA5 DLL folder for engine 5. This suite takes several minutes because paced playback is intentional.

Build `smaf_audio_tests.vcxproj` for synthetic graphics-filter tests; an optional fixture-folder argument checks the four supplied files. These tests need no Yamaha DLLs. Cover chunk boundaries, checksum rejection, truncation, retention of audio/metadata and source preservation on both architectures.

## Maintenance notes

- IPC version 6 adds the requested sample rate to Open; OpenReply reports the actual rate. It uses fixed-width messages and transfers audio, file bytes and folder strings. Never pass pointers between processes. Update both component architectures and the helper together after protocol changes.
- Keep the MA7 and MA5 adapters separate: their initialization, control argument conventions and PCM layouts differ. Retain the existing hardware reset and complete engine reinitialization between files. Validate adapter changes with rendered audio, not just DLL loading.
- Older MA5 libraries require the child-only Windows version compatibility setting. Do not change foobar's environment or persistent system compatibility settings.
- MA5 position may stop one millisecond before nominal duration. Position polling is amortized over 100 ms of audio and confirmed near EOF. Preserve finite repeats and release tails.
- MA7 can also stop short: n900i MLD fixtures have terminal position deficits of 1, 6 or 9 ms. Confirm unchanged position across consecutive generated blocks within the final 10 ms before starting release-tail handling. Keep position history reset across open/seek/repeat; do not use silence earlier in a song as an EOF signal.
- Generated musical time provides an independent end trigger for both engines when their position counter is unreliable. Integrate the current tempo per block, preserve the ordinary quiet/release-tail confirmation, and reset or re-anchor the clock on open/repeat/native seek. Do not use elapsed wall time as the conversion clock.
- MA5 receives a private graphics-free SMAF copy only when the complete top-level container and checksum validate. Do not remove embedded bytes matching a chunk tag or change the source file. Its scheduler clock advances with generated audio, with synchronized monotonic transitions to elapsed time for asynchronous lifecycle operations. Restrict the import redirect to the loaded MA5 MMS module; never change disk DLLs, foobar clocks or bridge deadlines.
- Both rates use 10 ms blocks (441 or 480 stereo frames). Allocate for the maximum and use the actual rate for WAV headers, IPC bounds, seek reconstruction, duration and tail thresholds. The standalone probe retains 44,100 Hz as its default; append 44100 or 48000 after its chorus argument to select a rate, or after the engine in benchmark mode.
- Tail confirmation holds only quiet blocks after nominal EOF (low peak and nearly constant samples, including Yamaha's residual DC offset). Discard the final held run only after confirmation; drain it before later sound. Clear held/ready audio on open and seek. Keep finite-repeat fade behavior unchanged.
- Native playback seeks preserve musical position, not accumulated effect history. Conversion seeks must remain sample-exact and use saved volume with normal tempo/pitch.
- MIDI effects tests cover all four reverb/chorus combinations, running status, controller/system resets, source preservation and restoration across warm opens.
- Playback controls use native spin buttons, labelled edit buddies and a 350 ms debounce. Enter, Escape and Close flush valid pending edits. Conversion must not inherit temporary playback controls.
- Normal loading is quiet. Timeout diagnostics include the current filename, phase, helper PID and elapsed time. Recovery remains bounded, with cancellation bypassing retries.

- SPF: the `Mapi_Phrase_*` sequencing was validated by driving the real `smaf::Yamaha` class against a simulated MA7 DLL (under Wine) that enforces the documented rules: exclusive modes, state machine, `SetData` borrowing the buffer, `Seek` only while stopped, 10 ms block multiples, `GetPosition` failing or returning the length after the end, and `SetData` accepting only flag 0. It covered both rates, repeats with and without fade, native seeks including into a repeat, SPF/MMF switching, a rejected truncated SPF and closing mid-playback. This proves call order and bookkeeping only, not audio. Listen to real SPF output, check the length against `smaf2wav --info`, and confirm the volume mapping before release. Ask whether `Phrase_Play`'s repeat argument is a count if a future need arises (open question in smaf2wav's API notes).

## Release validation

Public release preparation (2026-09-19): committed the revised README without changing its wording, rebuilt the universal 1.1.2 package and passed `tools/Test-Package.ps1` for archive contents, x86/x64 architectures and portable symbol paths. No synthesis implementation changed after the validated 1.1.2 milestone. The user has confirmed the ending/EOF fix; the separate historical PCM/control observations below remain documented.

Version 1.1.2 endpoint investigation (2026-09-17): all eight baseline newer-MA5 bridge renders of the four supplied files (two passes, 48 kHz) exceeded nominal duration plus 11 seconds without EOF. BoyMeetsGirl Q/QQ and ChuteDePluie capped position exactly 100 ms short. EatMyHeart sometimes advanced only a few position milliseconds per second of generated PCM before reaching that cap. The playback-state query remained in its playing state after silence. MA7 also failed three of eight fast bridge renders, despite the user's successful MA7 playback.

All four files contain a graphics track. A temporary BoyMeetsGirl-Q copy with only that track removed retained its 42,532 ms declared length but let MA5 position reach 42,531 ms instead of 42,432 ms. Its final four seconds were sample-identical to the original. This connects the position discrepancy to graphics processing. MA5 now receives a validated private copy without graphics; audio/metadata chunks and source files are preserved. The generated-time safeguard retains the existing release-tail behavior. With that safeguard alone, direct newer-MA5 renders ended at 42.530 s (both BoyMeetsGirl variants), 31.500 s (ChuteDePluie) and 14.790 s (EatMyHeart), with the same last nonzero sample times as the pre-fix captures.

The initial generated-time change passed targeted bridge endpoint suites on MA5 and MA7 at both rates, covering tempo, exact/native seeking and finite repeat. The existing x64 bridge suite passed with MA7 and both MA5 releases, including audible tails, effects, n900i EOF/repeat/seek and the original loop examples. The initial universal package passed content, architecture and portable symbol-path validation. Packaged x64 and Win32 endpoint host tests each passed all 48 file/engine/rate/mode combinations, and the x64 n900i suite passed 360 decodes. These initial results preceded the MA5 scheduler correction below.

The user confirms MidRadio Player plays BoyMeetsGirl-Q's final notes correctly with newer MA5, and foobar plays them correctly at 44.1 kHz. Both original standalone MA5 ending clips (44.1 and 48 kHz) also played them correctly. Native 48 kHz synthesis is therefore retained. Pacing requests reproduced about 237 ms of lost audio and a library stop, including at 44.1 kHz locally. Graphics removal alone did not fix a paced bridge regression. MA5's multimedia scheduler can switch to elapsed time near EOF and stop audio while the decoder is waiting for its next request. The helper-local scheduler clock addresses this separately from the endless-PCM safeguard.

The combined graphics/scheduler correction passed endpoint suites at both rates, including paced native-seek endings. With the final synchronized clock, complete paced BoyMeetsGirl-Q playback and a deliberate 750 ms pause near EOF matched the fast reference's energy exactly in both final-note windows at both rates. The Win32 MA5 and x64 MA7 endpoint clients passed all four songs, exact/native seeks, tempo 70/100/130 and finite repeats with/without fades. MA5 renders now end at 42.530 s for both BoyMeetsGirl variants, 31.500 s for ChuteDePluie and 14.770 s for EatMyHeart (audio-only declared length 14.764 s). The original x64 bridge suite passed with MA7 and both MA5 releases, including warm reuse, live controls, hot rate changes, effects and retained tails. Graphics-filter tests passed on both architectures, and the universal package passed content, architecture and portable symbol-path checks.

The final packaged x64 and Win32 endpoint host suites each passed all 48 file/engine/rate/mode combinations, including seeks beyond and back from EOF. The final packaged x64 n900i corpus also passed all 360 conversion/integrity decodes with eight concurrent workers and EOF seeks. The general host suites reproduced the existing failures: x64 reported `Saved volume not applied to a new decoder` (71,355 differing samples of 88,200, maximum delta 0.170806885, energy ratio 0.977259998), and Win32 reported `Temporary volume alone has no effect`. Preferences, MIDI effects, format decoding and exact seeks passed before those failures. These general suites did not complete successfully, and their assertions were not relaxed. The endpoint results do not resolve the older volume/control investigation.

One earlier extended run failed strict exact-seek PCM equality for EatMyHeart at 44.1 kHz; subsequent runs including the final suite passed and the assertion remains unchanged. This is recorded separately from the reproduced endless EOF and paced cutoff. The user subsequently confirmed the 1.1.2 ending and EOF fixes in foobar.

The n900i conversion stall was reproduced at 48 kHz before the 1.1.1 fix: 84 of 360 concurrent renders failed to reach EOF (21 affected filenames, original/unlocked copies, two passes). Direct position checks confirmed that Yamaha continued returning PCM after its millisecond position stopped below the reported length. Most deficits were 1 ms; `n900i_91.mld` stopped 6 ms short and `n900i_98.mld` 9 ms short. The apparent random file depended on batch ordering, rather than a blocked library call.

After the fix, all 720 bridge renders passed: 180 files including both folders, two passes at each of 48 and 44.1 kHz, eight concurrent workers. Packaged x64 and Win32 foobar hosts each passed 360 conversion/integrity decodes at 48 kHz with eight concurrent decoders, including seeks past EOF and back to the start. These tests exercise the actual input decoder services used by conversion and integrity testing; they do not automate the separate Converter or Input Validator user interfaces.

The x64 MA7 bridge regression suite also passed, including a targeted n900i MLD case for exact/native seeks and finite repeats at both rates. Earlier runs of that expanded suite hit intermittent MA7 timeouts at the engine-reset stage during rapid reopen sequences; the existing 30-second request limit correctly discarded the helper. The exact trigger is unresolved. A passing rerun does not establish consistent reset behavior, and the strict tests remain unchanged. This bounded reset failure is distinct from the reproduced endless-PCM EOF bug.

The packaged feature suites passed on x64 and Win32, covering both engines/rates, exact/native seeks, metadata and the original loop examples. The universal archive passed architecture/content and symbol-path validation. The standard host suites did not fully pass this time: x64 reported `Temporary volume alone has no effect`, and Win32 reported `Saved volume not applied to a new decoder` with a substantial PCM mismatch. These are in the previously documented volume/live-control test area; no assertions were relaxed. The successful n900i corpus results do not imply those outstanding issues are resolved.

Version 1.1 validation (2026-09-15):

- Metadata tests passed on x64 and Win32, including 16 supplied `meta` fixtures and synthetic encoding/bounds cases.
- Both bridge clients passed with MA7 and both MA5 library releases at 44,100 and 48,000 Hz. Tests cover hot rate changes, exact/native seeks, EOF, retained effects and the existing regression suite. The synthetic A4 measured 440.5 Hz at both rates; dry MIDI ended at 5.000 seconds, while wet output retained its longer tail.
- Packaged x64 and Win32 foobar hosts passed both the feature suite and the existing full regression suite. Sample-rate defaults, keyboard selection, tab order, nonoverlapping controls, metadata, session reinitialization and consecutive loop-fixture decodes passed.
- At 48 kHz on MA7, GuitarMan rendered 32.000 seconds, Cat Dance 40.560 seconds and MarsMine 28.240 seconds (nominal 28.236). The old 250 ms confirmation gap is removed. Ten-millisecond block resolution and genuine release/effect tails remain; actual output-device Repeat (track) listening is still a manual check.
- The universal package built and passed content, architecture, common x86 helper and portable symbol-path validation. Automated accessibility checks passed; the previously confirmed manual NVDA/ARM64EC results apply to 1.0, with new manual validation still welcome.

The historical intermittent PCM comparison issue described below remains open despite these passing runs.

Follow-up listening-example investigation: the original failures were numerical test results, not verified audible glitches. Twelve fresh standard host runs (six per architecture) passed without a capture. Forty direct eight-second MA7 comparisons of saved volume 32 versus volume 64 changed to 32 before the first audio request were bit-identical, including 0/1/10/100 ms request delays. A 20-round x64 control check also passed. The subsequent Win32 capture run reproduced `Saved volume not applied to a new decoder` and saved the real failing/reference pair.

That capture contains the first second of MarsMine at 44.1 kHz: saved-volume-32 conversion versus temporary-volume-32 playback. Of 88,200 interleaved samples, 79,603 differ, with maximum absolute delta 0.144775391. Their RMS levels differ by only 0.0026 dB and neither clips, so this capture does not substantiate a missed volume setting despite the assertion's wording. The original captures and a reference/actual listening comparison are saved locally under `out/audio-examples/pcm-mismatch/`, excluded from Git. The preview adds the same 12.04 dB gain to both excerpts without clipping; individual files preserve the original levels and samples.

The user listened to this pair and reported no corruption: the actual excerpt appears to contain a few milliseconds more musical audio, with the difference unlikely to be noticeable outside direct comparison. Both files contain exactly 44,100 stereo frames, so their exported durations are equal. The first nonzero frame is 139 in actual versus 184 in reference (about 1.02 ms earlier); this measures sample onset, not necessarily audible onset. A coarse stereo energy-envelope comparison using 1 ms bins over the central 900 ms finds its best offset within +/-50 ms at actual 4 ms earlier (similarity 0.928 versus 0.881 without alignment). These observations support a small timing difference, but a constant shift has not been shown to explain the entire PCM mismatch. Extra generated audio relative to engine-reported position remains a hypothesis, not a verified cause. Leave playback and strict test assertions unchanged; classify this capture as a timing/render consistency observation rather than verified audio corruption or a volume-setting failure. This listening result does not resolve the separate temporary-volume-no-effect assertion or engine-reset timeouts.

A separate local listening illustration uses normal eight-second MarsMine renders at Yamaha volumes 32 and 64, with no post-render normalization. The volume-64 excerpt measures 11.29 dB louder by RMS. It illustrates a volume change having no effect; it is explicitly not the captured intermittent failure. Generated audio and its explanation stay outside Git under `out/audio-examples/`.

Follow-up manual testing confirmed that the track-length fix plays the loop examples correctly. Differing displayed sample rates were confirmed to be cached metadata; reloading file information corrected them. Direct MA7 renders of Dragon Quest 8 Overture, 930ca_11, Cat Dance and GuitarMan all used 48,000 Hz with normal song timing. The README settings section documents the sample-rate preference and cache refresh.

The user subsequently confirmed that the n900i conversion batch completes successfully with 1.1.1.

The pre-release component passed packaged x86/x64 host suites, bridge suites with MA7 and both MA5 releases, controlled recovery tests and package architecture checks. Manual testing confirmed NVDA accessibility, ARM64EC playback and corrected ARM64 MA7 automatic discovery.

Version 1.0 preserves those playback features and prepares the source and tooling for distribution. Yamaha libraries, SDK sources, fixtures, generated audio and local test output remain excluded from Git and packages. A broader compatibility corpus and controlled comparisons with Yamaha's players remain future work.

The 1.0 package builds for both component architectures and passes architecture/content and portable-symbol-path validation. The validator also rejects a deliberately nonportable symbol record. The direct x64 bridge suite passes with MA7 and both MA5 releases, and the simplified loading stress tool completed a full fixture pass without errors. Both foobar host architectures produced passing runs.

Repeated host runs also exposed intermittent strict PCM equality failures around saved/temporary volume and live-control isolation, similar to failures seen before release preparation. A captured mismatch was substantial, not just floating-point rounding. The cause remains unresolved; a passing rerun is not proof of consistent behavior. Keep the strict assertions and their mismatch diagnostics. The release preparation changes do not modify synthesis or playback-control logic. Investigating this intermittent behavior remains follow-up work.
