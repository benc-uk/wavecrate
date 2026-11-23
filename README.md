# WaveCrate

A native Windows sample browser for large WAV collections and ZIP sample packs.
Browse, preview and curate samples without importing the whole collection.

**Current scope:** navigate local folders, preview WAV files, zoom, pan or seek
within their waveform overviews, and select or loop a region. Editing and export are deferred. There is no persistent catalogue
or ZIP handling yet.

**Current status:** M1, M1.1, M2, M2.1 and M2.2 are implemented. M3 (file management) was withdrawn. Functionality
has been checked in Debug. The Release build was last verified at M2.1.
Clean-machine compatibility remains a release task.

## Stack

| Component | Choice | Status |
| --- | --- | --- |
| Language | C++20 | Built |
| Desktop UI | JUCE 9.0.5, pinned to a source commit | Folder controls, sample transport and resizable panes |
| Build | CMake presets, Ninja and MSVC | Debug built; Release last built at M2.1 |
| Catalogue/search | SQLite with FTS5 | Planned (deferred epic E2) |
| ZIP access | libzip and a per-entry temporary cache | Planned |
| Audio | JUCE audio formats, devices and a preview transport | Streamed WAV preview implemented |
| Waveform | Background min/max peak generation and a bounded overview cache | Mono/stereo overview, zoom, pan, seeking and region selection implemented |

This is a standalone application. It will not host or build VSTs or other plug-ins.
It does not use .NET, Electron or WebView2.

## Using the browser

Use **Open...** beside the Folders heading (Ctrl+O) to choose a folder, or navigate
using the tree and its location selector. Select a parent folder in the tree to
return to it. Selecting a folder updates the sample list on the right.
The opened folder shows its immediate subfolders automatically; expand a branch
to discover deeper folders on demand, or collapse it to keep the tree compact.
The sample list includes .wav and .WAV files directly inside that folder, sorted
by filename; it does not recursively collect samples from subfolders.

The file view shows duration, file size, channel count, sample rate and audio
bitrate. WAV details load in the background without decoding the recording.
Columns can be resized; narrow panes scroll horizontally.
Unavailable details are marked with a dash, with the reason in the row tooltip.
Size is the file length, not the allocated disk space. Bitrate describes the
uncompressed audio payload, not container metadata or the playback device rate.

The full-width upper pane displays the selected sample's waveform. Play, Stop and
playback position are in the transport bar directly below it, with a separate
sample-details row showing duration, size, channels, sample rate, bit depth and
audio bitrate. Editing controls
have been removed until their layout is decided. Drag the horizontal and
vertical dividers to resize the panes. Fonts and neutral colours follow a
Windows-style desktop presentation, with the system light/dark setting.

Folder discovery and audio file preparation run in the background.

Waveforms initially show the entire recording. Stereo files default to a combined
view, showing the lowest and highest peaks from either channel in one lane. Use
the **View: Combined** menu to choose **View: Stereo** for separate channels.
Mono files always use one lane. The choice lasts for the current session and
changes the display only, not playback. Peaks are generated in the background
using bounded read buffers and at most 4,096 bins per channel. The in-memory
cache retains up to eight summaries and invalidates them when the source size or
modification time changes. It is not a persistent disk cache, and the first
overview of a long recording may take time to generate.
Changing selection clears stale results; empty/loading/error states are explicit.
Use the mouse wheel or the -/+ buttons to zoom. **Zoom selection** frames the
selected region, keeping the selection and playback unchanged. Selections shorter
than one peak bin are shown centred in the smallest available view, clamped at
the sample edges. Hold the right mouse button and drag left/right to pan a zoomed
waveform, or use Shift+wheel or the horizontal scrollbar. Fit restores the full
recording. Zoom uses the existing overview, down to one peak bin; higher-resolution
and individual-sample detail are not loaded yet. Changing samples resets the view.

Click the waveform to seek. Playback continues if already playing; otherwise the
new position is held for the next Play. The cursor tracks playback within the
visible range without automatically moving the viewport. Stop rewinds to zero,
and Play after the end of a file starts again from zero.

Drag across the waveform to select a region; drag near an edge to adjust it. A
click without dragging seeks and clears the selection. With a selection, Play
starts at the selection start (or the cursor, if it is inside the selection) and
stops at the selection end. The Loop toggle (L) loops the selection, or the whole
sample when nothing is selected. It defaults off and stays set when you change
samples. The waveform badge and position text show the current loop mode.
Selections are frame-accurate, not saved, and do not change the audio. Export
and editing are not implemented.

Select a file to prepare it, then use Play or the keyboard. Selection alone does
not start playback; changing the selection or folder stops the previous sample.

| Action | Control |
| --- | --- |
| Select a sample | Click a row, or Up/Down in the list |
| Play from the current position | Play, Enter or double-click |
| Play/stop | Space, including cancellation of a pending Play |
| Stop and rewind | Stop or Escape |
| Show combined or separate stereo waveforms | View: Combined / View: Stereo menu |
| Zoom in/out | +/- buttons, or mouse wheel over the waveform |
| Zoom to the selected region | Zoom selection or Z |
| Pan the waveform | Right-drag, Shift+wheel, horizontal wheel or scrollbar |
| Show the whole waveform | Fit |
| Seek | Click within the waveform |
| Select or adjust a region | Drag in the waveform, or drag a selection edge |
| Loop selection or whole sample | Loop or L |

The preview uses the default Windows output device in WASAPI shared mode, at a
fixed half-scale gain. No recording input is opened. Device selection and adjustable
volume are later work.

Supported files are mono/stereo little-endian RIFF/RF64 WAVs with 8/16/24/32-bit
integer PCM or 32-bit floating-point samples, at 8,000 to 384,000 Hz. Unsupported,
missing or malformed files produce an error in the preview panel. Unused WAV
metadata is skipped. Audio is streamed through bounded read-ahead rather than
loading entire recordings.

The current folder list is not a persistent catalogue. It does not watch for
external changes; reopen the folder to refresh it. Preview and region selection
do not modify audio or files.

## Minimum Windows toolchain, without the Visual Studio IDE

Use **Build Tools for Visual Studio**, not the Visual Studio IDE. In the installer,
choose Desktop development with C++ and retain these components:

| Required component | Purpose |
| --- | --- |
| MSVC x64/x86 C++ build tools, VS 2022 or newer | Compiler, linker and C++ standard library |
| One current Windows 10/11 SDK | Windows headers, import libraries, resource compiler and manifest tools |
| C++ CMake tools for Windows | CMake and Ninja |

The project requires CMake 3.24 or newer. Prefer the current CMake supplied with
your build tools. A separate portable CMake and Ninja installation also works.

Do not add the Visual Studio IDE, .NET workloads, MFC, ATL, C++/CLI, UWP or game
development workloads for this project. Git is already available and useful for
source control, but the JUCE source download uses HTTPS and does not need a Git
checkout.

The Windows SDK is a **build-time requirement**, not something shipped to users.
Changing from MSVC to clang-cl would not remove its Windows header/library
requirements. The configured path is MSVC, to keep the first build straightforward.

The application and JUCE modules use the static MSVC runtime: /MT in Release and
/MTd in Debug. The intended release is a native executable with no separate
runtime installer. Windows system DLLs are still required. Confirm the final
release dependencies on a clean machine before distributing it.

## Build

From an ordinary PowerShell window:

```powershell
Set-Location D:\personal\wavecrate
.\scripts\build.ps1
& .\out\build\windows-debug\WaveCrate_artefacts\Debug\WaveCrate.exe
```

The helper discovers installed C++ Build Tools and loads their x64 developer
environment in a child process when needed. It then checks prerequisites,
configures and builds. It does not install anything, change system settings or
modify the calling terminal's PATH. An existing x64 developer shell also works.

For a release build:

```powershell
.\scripts\build.ps1 -Configuration Release
& .\out\build\windows-release\WaveCrate_artefacts\Release\WaveCrate.exe
```

To run CMake directly, use the **x64 Native Tools Command Prompt** so the compiler
and SDK are in that shell's environment:

```bat
cd /d D:\personal\wavecrate
cmake --preset windows-debug
cmake --build --preset windows-debug
```

The first configure downloads the pinned JUCE source from GitHub into the ignored
build tree. It also compiles JUCE's build helper, so configure itself needs the
compiler. Allow more time for the initial build.

To use an existing matching JUCE source checkout without downloading it:

```powershell
cmake --preset windows-debug "-DFETCHCONTENT_SOURCE_DIR_JUCE=C:\path\to\JUCE"
cmake --build --preset windows-debug
```

Machine-specific overrides belong in CMakeUserPresets.json, which is ignored.

## First-window acceptance

After a successful build:

1. Launch WaveCrate.exe. A window titled WaveCrate should show the empty-state UI
   without opening a console.
2. Resize, minimise, maximise and restore the window. Check that text remains visible.
3. Close the window and check that the process exits.
4. Repeat with the Release build. Before release, inspect DLL dependencies with
   `dumpbin /dependents` and test on a clean Windows machine.

## Tests

From the x64 developer environment:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug-tests
ctest --preset windows-debug
```

The suite covers folder filtering/replacement, keyboard callbacks, WAV validation,
latest-request loading, waveform extrema/caching, cancellation, pending-play commands and cleanup. It does not open an audio output
device. The app and test runner share one static library, so JUCE is not compiled
again just to build the tests.

For a focused browser check during UI iteration:

```powershell
& .\out\build\windows-debug\WaveCrateTests.exe --test "Sample folder browser"
& .\out\build\windows-debug\WaveCrateTests.exe --test "Folder navigation"
```

For the waveform data/cache checks only:

```powershell
& .\out\build\windows-debug\WaveCrateTests.exe --test "Waveform data"
```

For waveform navigation only:

```powershell
& .\out\build\windows-debug\WaveCrateTests.exe --test "Waveform navigation"
```

An optional hardware check opens the default output and plays a quiet generated
test tone:

```powershell
& .\out\build\windows-debug\WaveCrateTests.exe --audio-device
```

It checks this process's Windows audio-session meter, transport progress, playing seeks, Stop and
end-of-file behaviour. It does not record microphones or other applications.
An unavailable device or session meter is reported as blocked, not a pass.
This checks the digital output path, not subjective listening quality or physical
speaker output. Device disconnection and slow/unavailable-drive stress checks
remain future robustness work.

## Layout

```text
wavecrate\
  AGENTS.md
  CMakeLists.txt
  CMakePresets.json
  PLAN.md
  README.md
  THIRD_PARTY_NOTICES.md
  Source\
    Main.cpp
    MainComponent.h
    MainComponent.cpp
    DesktopLayout.h
    DesktopLayout.cpp
    WaveformData.h
    WaveformData.cpp
    WaveformView.h
    WaveformView.cpp
    ValidatedWavReader.h
    AudioPreview.h
    AudioPreview.cpp
    PreviewCommands.h
    SampleBrowser.h
    SampleBrowser.cpp
  Tests\
    Main.cpp
    AudioPreviewTests.cpp
    SampleBrowserTests.cpp
    FolderNavigationTests.cpp
    WaveformDataTests.cpp
    WaveformNavigationTests.cpp
    PreviewCommandsTests.cpp
    AudioDeviceSmoke.cpp
  scripts\
    build.ps1
  out\                         generated and ignored
```

PLAN.md tracks intended features, dependencies, acceptance criteria and status.
AGENTS.md contains the project rules for coding agents.

## Agent-assisted development

The repository skill at .github\skills\wavecrate-develop\SKILL.md provides a
reusable development process. Supply the feature, fix or scoped plan to work on;
the skill does not select or hard-code a milestone.

```text
/wavecrate-develop Add local WAV browsing and preview using the scope in PLAN.md.
```

To request fleet-mode orchestration explicitly:

```text
/fleet Use the wavecrate-develop skill to add waveform region selection.
```

The lead uses ordinary agents with task-specific roles, not custom agent profiles.
It allows at most two active workers, prevents nested delegation, assigns each
file one owner and permits only one build runner. Small or sequential tasks stay
with the lead. These are coordination instructions, not filesystem access controls
or a replacement for CLI permissions and limits.

The skill is explicit-use only. If this session has not discovered it, use /skills
to reload the skill. No application work starts merely because the file exists.
No custom profiles or global CLI settings are required by this process.

## Licensing

JUCE has its own licensing requirements; a commercial JUCE licence may be required.
Review THIRD_PARTY_NOTICES.md and choose
the appropriate JUCE licence before distributing the application. WaveCrate's own
distribution licence has not been selected; this scaffold does not make that
decision.
