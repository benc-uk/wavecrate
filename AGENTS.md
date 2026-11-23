# WaveCrate agent guidance

## Product

WaveCrate is a Windows desktop sample browser for large collections of WAV files
and ZIP sample packs. It is a standalone application, not a DAW or plug-in.

## Scope and decisions

- Use C++20, JUCE and CMake. Use Ninja with the MSVC command-line toolchain.
- Do not add .NET, Electron, a webview, VST support, plug-in hosting or plug-in targets.
- Link the MSVC runtime statically. Development tools must not become application
  installation requirements.
- Do not install SDKs or system-wide tools. Report missing prerequisites instead.
- M1/M1.1 provide folder-tree navigation and previewing a selected folder's WAVs. Implement only
  the user's requested scope, not later milestones automatically.
- M2/M2.1 provide cached waveform overviews, zoom, pan and seeking. Region selection,
  looping and export remain separate milestones and must not be added unless requested.
- E1 (sample editing) and E2 (catalogue, search, tags, collections and saved regions)
  are deferred epics. Do not implement them, or add catalogue groundwork, unless requested.
- SQLite with FTS5 (E2) and libzip (M4) are planned, not current build dependencies.
- Read README.md and PLAN.md before making changes. Keep their status accurate.

## Code and build

- Put application source in Source. Keep build products and downloaded dependencies
  in out, which is ignored.
- Code map: `MainComponent.cpp` wires the application and preview commands;
  `SampleBrowser.*` owns folder and sample-list navigation; `AudioPreview.*`
  streams playback; `ValidatedWavReader.h` validates and reads WAV data;
  `WaveformData.*` generates and caches overview peaks; `WaveformView.*` draws
  and handles waveform navigation and regions; `DesktopLayout.*` owns pane layout.
- Focused test map: `WaveformNavigationTests.cpp` covers viewport behaviour,
  `WaveformDataTests.cpp` covers peak generation and caching,
  `AudioPreviewTests.cpp` covers playback, `PreviewCommandsTests.cpp` covers
  command routing, and `FolderNavigationTests.cpp` / `SampleBrowserTests.cpp`
  cover browsing. All are built into `WaveCrateTests` and run by
  `ctest --preset windows-debug`.
- Use CMake-style forward slashes inside CMake source paths. Use Windows paths for
  shell commands; JUCE's CMake wrappers can re-parse backslash escapes.
- Use four spaces, braces on separate lines, ASCII and British English.
- Prefer RAII and explicit ownership. Use std::unique_ptr for owned objects.
- Include JUCE module headers directly. Do not introduce Projucer project files.
- Keep dependencies pinned. Do not edit downloaded JUCE source.
- Use the debug or release CMake preset. scripts\build.ps1 discovers installed
  Build Tools and loads an x64 child environment when needed; it never installs
  tools or changes the caller's PATH.
- Build hardware-independent tests with the windows-debug-tests build preset, then
  run ctest --preset windows-debug. The optional WaveCrateTests --audio-device check
  plays a quiet test tone and is separate from the default suite.
- Make focused changes. Run the relevant build and checks before marking work done.
  If compilation or launch cannot run, report that and leave verification blocked.
- Prioritise fast feedback: one incremental Debug build and the smallest relevant
  tests by default. No automatic Release build, new GUI automation or extra review
  agent. Broaden validation only for an explicit requirement or unresolved risk;
  stop when the necessary checks pass.

## Future implementation boundaries

- Separate the UI, catalogue, audio transport and archive/cache work.
- Never scan libraries, decompress ZIP entries or query SQLite on the audio callback.
- Keep lengthy filesystem work off the UI thread. Use bounded, cancellable jobs.
- Virtualise both result rows and catalogue queries. Do not load the whole library
  into view models or the whole recording into memory.
- Catalogue IDs must not depend on current file paths or temporary cache paths.
- Store waveform region boundaries as 64-bit audio-frame positions, with an exclusive
  end position.
- Preserve original files by default. Keep tags, collections and regions in the
  catalogue. Treat disconnected drives as offline.
- ZIPs are read-only libraries. Extract only requested entries to an app-owned,
  size-limited cache. Use generated cache names and enforce decompression limits.
- Keep user curation separate from rebuildable waveform and analysis caches.
- Do not invent working controls or mark planned functionality as implemented.
