# Developing foo_mmf

## Release scope

Version 1.0 provides independent MMF, MLD and MIDI decoders for foobar2000 2.x. It supports Yamaha MA2, MA3, MA5 and MA7 songs through user-supplied MA7 or MA5 libraries. MA4 and MA6 are not target generations. Decoder priority is set under Preferences > Playback > Decoding.

The component runs in x86 or x64 foobar; ARM64EC foobar uses the x64 component. Yamaha libraries always run in a separate x86 helper. Both installers use Program Files (x86) on x64/ARM64 Windows and Program Files on 32-bit Windows. Explicit library folders override discovery.

## Source layout

| Path | Purpose |
| --- | --- |
| `src/component.cpp` | Decoders, metadata, helper reuse and engine fallback |
| `src/ui.cpp`, `src/component.rc` | Preferences and accessible playback controls |
| `src/bridge.*`, `src/protocol.hpp`, `src/loading.hpp` | Process isolation, IPC and deadlines |
| `src/helper.cpp`, `src/yamaha.*` | x86 synthesis host and engine adapters |
| `src/midi.hpp` | In-memory MIDI effects filtering |
| `src/probe.cpp` | Standalone WAV renderer and loading benchmark |
| `tests/` | Bridge, real-host and loading stress tests |
| `tools/` | Package validation and disposable host test launcher |

## Build

Install Visual Studio 2026 with the C++ desktop workload, v145 tools and a Windows SDK. Place the foobar2000 SDK under `sdk/`; SDK 2025-03-07 is the validated version. Do not modify SDK sources. Yamaha libraries and sample songs are not required to compile or package the component.

From the repository root:

```powershell
.\package.ps1
.\tools\Test-Package.ps1
```

The output is `out/foo_mmf.fb2k-component`. The package contains the x86 component at its root, the x64 component under `x64/`, a shared x86 helper, README and license notices. Test binaries and Yamaha libraries are excluded.

`build.ps1` locates Visual Studio through vswhere. Use `-Project`, `-Platform` and `-Configuration` to build an individual target. Compiler concurrency is capped. PFC references must use `Release FB2K` to avoid conflicting utility hooks. Generated files stay under `build/`. The linker embeds only the symbol filename, not its local build path.

## Architecture and behavior

Each active decoder exclusively owns a helper. Up to two idle helpers retain their process and DLLs for later files. Engine state is fully reinitialized between songs to prevent effect leakage. Helpers are pooled by library folder and engine; failed or cancelled workers are discarded. Closing the helper's job terminates the child process.

Automatic selection prefers MA7. Explicit MA5 falls back to MA7 for unsupported files or loading failures; explicit MA7 stays on MA7. Tested MA5 releases reject standard MIDI and the MLD/MA7 regression fixtures, which use MA7 fallback. Metadata reports the engine that succeeded.

MA5 loading has a five-second no-progress timeout and one retry in a fresh helper. A second failure uses the configured fallback policy. Every IPC request has a separate 30-second total limit. Cancellation takes precedence over retry and fallback. The foobar console reports loading failures and recovery.

Playback uses native seeks. Conversion and integrity testing reconstruct exact sample positions at normal tempo and pitch. Saved Yamaha volume applies to conversion; temporary playback controls do not. Tempo is limited to 70..130 percent, transpose to -12..12 semitones, and volume to 0..127 with a default of 64.

MIDI decoding is always registered. Reverb and chorus default on and can be disabled independently. Disabled effects are suppressed in an in-memory MIDI copy, including after resets. MMF/MLD effects and source files are preserved.

## Validation and remaining work

Playback and default discovery have been confirmed in x86, x64 and ARM64EC foobar. NVDA accessibility has been confirmed. Automated tests cover audio, seeking, effects, helper reuse, cancellation, MA5 recovery and native UI controls. See DEVELOPMENT_NOTES.md for test prerequisites and commands.

Broader song/library compatibility and controlled listening comparisons remain useful future work. The intermittent MA5 stall is handled by process recovery; its internal cause is unresolved. Keep both development documents current when changing behavior or validation coverage.

Repeated real-host runs can intermittently fail strict PCM comparisons around volume and live-control transitions. Both architectures also produced passing runs; the direct bridge suite passed. The cause is unresolved and the assertions remain strict. See DEVELOPMENT_NOTES.md before interpreting a single passing run as complete validation.
