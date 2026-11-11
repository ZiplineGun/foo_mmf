# Working on foo_mmf

Read DEVELOPMENT.md and DEVELOPMENT_NOTES.md before changes. Keep both current with verified behavior, validation and remaining work.

## Build and validate

- Build with `powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1`; build both component architectures and the shared helper with `package.ps1`.
- Use `tools/Test-Package.ps1` to validate the universal archive in `out/`. Preserve the common x86 helper after the x64 overlay.
- The validated toolchain is Visual Studio 2026 v145, Windows SDK and foobar SDK 2025-03-07. Keep SDK modifications out of the implementation. PFC references must use `Release FB2K`. Cap compiler concurrency.
- Audio tests require separately supplied Yamaha libraries and fixtures; see DEVELOPMENT_NOTES.md. Keep pipe permissions unchanged and run tests with a normal Windows token.
- Use `tools/Host-Test.ps1` for disposable portable profiles, never an installed user profile. `-Recovery` tests stalled MA5 workers owned by that test host. Run host suites serially. Reject stale or missing results.
- Keep build paths out of distributed binaries. Do not commit Yamaha binaries, SDK sources, fixtures, local output or personal machine configuration.

## Invariants

- Yamaha is x86-only and stays in the helper. Never pass pointers over IPC. Keep MA7 and MA5 adapter conventions separate and verify changes with rendered audio.
- Default library folders use Program Files (x86) on x64/ARM64 Windows and Program Files on 32-bit Windows. Explicit saved paths override discovery.
- Supported generations are MA2, MA3, MA5 and MA7. SPF (SMAF Phrase) is MA7-only: detect it from the file contents, use `Mapi_Phrase_*` in `Mapi_SetMode(2)`, reinitialize when switching between Melody and Phrase mode, keep the song buffer alive until `Phrase_RemoveData`, never route SPF to MA5 or fall back to it, and ignore tempo/transposition.
- Preserve the existing MA7 device reset and engine configuration. Fully reinitialize between songs to prevent effect-state leakage.
- Apply `WinXPSP2VersionLie` only in the MA5 child's environment, never in foobar or persistent system settings.
- Each active decoder exclusively owns its helper. Pool by folder and engine, retain at most two idle workers, and clear them at shutdown. Discard failed/cancelled workers.
- MA5 opens have a five-second no-progress timeout and one fresh-process retry before normal fallback. All requests have a 30-second absolute limit that progress cannot extend. Cancellation takes precedence and never retries. Log recovery with filename, phase and PID.
- MIDI decoding stays registered; reverb and chorus are independent preferences enabled by default. Suppress disabled sends in an in-memory SMF copy, including after resets. Preserve source files and MMF/MLD effects.
- Saved Yamaha volume spans 0..127, default 64. Temporary playback controls never affect conversion or integrity testing.
- Preserve native playback seeks, exact reconstruction for conversion, cancellation, repeat state and EOF. Confirm MA5 position near EOF while amortizing position calls.
- Track generated musical time independently of engine position and preserve release tails. MA5's scheduler clock must advance with PCM, remain monotonic across lifecycle changes, and leave IPC deadlines untouched. Test paced final notes as well as offline EOF; preserve source files when filtering graphics from the private SMAF copy.
- MA7 MLD position can stop short of length. Confirm stalled position within the final 10 ms before starting tail handling, and reset position history on open, seek and repeat. Cover this with full renders, since request timeouts do not catch endless valid PCM.
- Support 44,100 and 48,000 Hz with a 48,000 Hz component default. Keep PCM blocks at 10 ms, report the actual rate over IPC and use it for exact seeks. Validate both rates with both engines.
- Parse metadata within declared SMAF chunk bounds and encodings; omit unsupported text and preserve source files. Hold candidate end silence until confirmed, preserving delayed effects and clearing queued audio on seeks/opens.
- Tempo stays within 70..130 percent and transpose within -12..12 semitones. Keep controls keyboard-navigable and labelled for screen readers.
- Commit completed, checked milestones. Do not claim runtime or audio compatibility based only on compilation.
