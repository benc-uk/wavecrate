# WaveCrate development plan

Updated: 2026-11-22.

## Goal

Build a fast, native Windows browser for large on-disk WAV collections and ZIP
sample packs. Make listening, finding and curating sounds quick without forcing
users to import or extract whole libraries.

## Status definitions

- **Planned:** agreed direction, not implemented.
- **In progress:** implementation is underway.
- **Blocked:** a named prerequisite prevents completion.
- **Done:** implemented and verified against the acceptance criteria.

## Milestones

| ID | Milestone | Status | Depends on | Acceptance |
| --- | --- | --- | --- | --- |
| M0 | Native application window | Done | None | Debug and Release build; resizable window opens and closes cleanly; no separate runtime installer |
| M1 | Browse and preview local WAVs | Done | M0 | Browse one folder; select and play/stop WAVs; keyboard navigation; malformed files fail safely |
| M1.1 | Navigate subfolders to find samples | Done | M1 | Show subfolders of the opened path; navigate the folder tree; list and preview WAVs in the selected folder |
| M2 | Waveform display | Done | M1 | Render the selected WAV in the existing pane using cached peaks, background generation and bounded memory |
| M2.1 | Waveform navigation | Done | M2 | Zoom, pan and click-to-seek within the selected sample |
| M2.2 | Region selection and audition | Done | M2.1 | Select and adjust a frame-accurate region; play/loop the selection; provide an explicit whole-sample loop toggle |
| M3 | File management | Withdrawn | M1.1 | Removed after implementation; not scheduled |
| M4 | ZIP libraries and preview cache | Planned | M2 | Browse ZIP entries as folders without whole-pack extraction; preview via per-entry temporary files; bounded cache and safe invalidation |
| M5 | Scale, recovery and distribution | Planned | M4 | Representative large-folder checks, device/drive-loss recovery and a clean-machine release check |
| E1 | Sample editing (deferred epic) | Planned | M2.2 | Whole-sample and selected-region manipulation, precision editing tools and safe save/export; implement later |
| E2 | Catalogue and curation (deferred epic) | Planned | M1.1 | Persistent catalogue, indexing and search; tags, favourites, ratings, notes, collections and saved regions; implement later |

Dependencies describe technical
requirements, not a promise to implement everything in one pass.
M2.1 adds the viewport navigation used by M2.2.
E1 and E2 are deliberately deferred. Neither is a prerequisite for M4 or M5.
Until E2 is scheduled, browsing is folder-based: the folder tree and per-folder
sample lists are the only way to find samples.

## M0: first window

| Item | Status |
| --- | --- |
| C++20 GUI application, no plug-in targets | Done: Debug and Release builds passed |
| Pinned JUCE dependency, CMake debug/release presets and static runtime setting | Done: both presets built; Release imports only Windows system DLLs |
| Native title bar, resize support and simple empty-state UI | Done: window observed and exercised |
| README, agent guidance and this feature plan | Done |
| Available compiler, Windows SDK, CMake and Ninja | Done: user installation completed; x64 tools verified |
| Build, launch, resize and clean shutdown | Done: Debug and Release passed |

Do not mark M0 done until an actual executable has built and its window has been
observed. The agent has not installed or modified system tools.

The user completed the Build Tools installation. MSVC, the Windows SDK, CMake
and Ninja are available in the x64 developer environment. Both builds passed,
and each window was checked for visibility, responsiveness, resizing,
minimise/maximise/restore and clean exit. The welcome screen was inspected.
The M0 Release executable was 4,642,816 bytes and used the Windows GUI subsystem.
Its import table contained only Windows system DLLs, with no external MSVC
runtime imports. Clean-machine compatibility and distribution checks remain
part of M5, not a claim made by this local smoke check.

## M1: local WAV browsing and preview

Scope is one non-recursive local folder, explicit playback and safe file errors.
There is no automatic preview on selection, persistent catalogue, waveform,
region editing, ZIP support or file management.

| Item | Status |
| --- | --- |
| Native folder chooser, virtualised rows and background enumeration | Done |
| Case-insensitive WAV filtering, filename ordering and stale-scan rejection | Done |
| Streamed mono/stereo PCM/float WAV preview through shared WASAPI | Done |
| Play/Stop, Up/Down selection, Enter/double-click and Space/Escape | Done |
| Pending-play cancellation, selection replacement and clean shutdown | Done |
| Malformed/truncated/unsupported WAV rejection with visible errors | Done |
| File-list columns and selected-sample details: duration, size, channels, sample rate and bitrate | Done: background metadata, resizable columns and a separate waveform details row; Debug build and focused checks passed |
| Debug build, hardware-independent tests and native UI checks | Done |
| Windows audio-session output, transport position, Stop and EOF smoke check | Done |
| Release build and runtime dependency check | Done: only Windows system DLL imports |

The native UI check exercised the actual folder chooser, sample selection,
playback shortcuts, recovery after a malformed file and two-Space cancellation.
The audio smoke check observed a non-zero meter value for this process's output
session. It does not claim subjective listening or physical speaker verification.
Device-loss, blocked-drive shutdown and large-folder stress remain M5 work.

## M1.1: subfolder navigation

Implemented in the Debug build. The opened root is opened and selected after
JUCE rebuilds the tree, so its immediate subfolders are visible. Deeper branches
remain lazy and collapsible. Actual tree selection drives the sample list;
folder selection and root replacement clear stale selection. The incremental build and focused
in-process navigation checks passed, without a Release build or full UI suite.

Opening a path should make it a clear starting point for exploring its subfolders
and finding samples. This is folder navigation, not recursive catalogue indexing.

Acceptance criteria:

- Show the opened folder and its immediate subfolders in the left-hand browser.
- Expand and collapse folders to explore deeper levels, loading children on demand.
- Selecting a folder updates the right-hand sample list with WAVs directly inside
  that folder, retaining the existing preview controls.
- Make the selected folder/path clear and support returning to parent folders.
- Clear stale sample selection and stop the previous preview when changing folders.
- Keep navigation responsive and show useful states for empty or unreadable folders.

Do not scan the entire directory tree, combine samples from every descendant or
add catalogue, waveform, editing or ZIP features. Use targeted navigation checks
and the fast-feedback validation policy in AGENTS.md.

## M2: waveform display

Implemented in Debug: the selected WAV renders as a full-recording mono/stereo
overview. One background worker produces at most 4,096 min/max bins per channel
using a 4,096-frame read buffer. An eight-entry in-memory LRU cache reuses summaries
until file size or modification time changes. This is not a persistent disk cache.
Selection changes discard stale results, and the view handles empty/loading/error
states without blocking playback. It shares the existing validated WAV reader.

Stereo files default to a combined view using the extrema of both cached channels.
The waveform view menu also offers separate stereo lanes. The choice persists
across samples for the session, leaves mono waveforms unchanged and does not alter
playback.

The incremental Debug build, focused waveform checks and shared-reader regressions
passed. A rendered stereo fixture was inspected. No Release build, hardware retest
or full UI suite was run.

This milestone is display only. Do not add zoom, seeking, region selection,
editing controls, export or catalogue functionality in this step.

## M2.1: waveform navigation

Implemented in Debug: -/+ and pointer-anchored wheel zoom, Shift/horizontal-wheel
panning, a horizontal scrollbar and Fit. The view uses a clamped 64-bit source-frame
range; new samples reset to fit, while resizing preserves the range. Zoom is
limited to the cached overview's peak-bin resolution, not new sample-level reads.

Left-click seeking is prepared on the existing audio worker. Playing seeks resume
at the new position; stopped seeks remain stopped and set the next Play position.
Stop rewinds, end-of-file Play restarts, and stale seek requests are discarded.
A cursor shows playback inside the visible range without automatic scrolling.
Region selection, looping and export remain out of scope.

The incremental Debug build and focused viewport/transport checks passed. The
existing short audio-device smoke check confirmed continued output after a playing
seek. Release compilation was subsequently verified at the user's request.
No full UI suite was run.

Implemented navigation quality-of-life additions: **Zoom selection** (button or Z) frames the current
region without changing selection or playback. It keeps the overview-resolution
limit, centring shorter regions in the smallest view and clamping at sample
edges. Right-button dragging pans a zoomed waveform horizontally, with the
waveform following the pointer. It preserves selection and playback, clamps at
the sample edges and updates the existing scrollbar.
The incremental Debug app/test build and focused waveform navigation checks
passed, including short and edge selections, 64-bit ranges, pan direction and
clamping, and unchanged selection/seek callbacks.

## M2.2: region selection and audition

Implemented in Debug. Dragging in the waveform creates a [startFrame, endFrame)
selection of 64-bit source frames; dragging within 5 px of an edge adjusts it.
A click without dragging seeks on release and clears the selection. The selection
is drawn with edge handles and is cleared when the sample changes.

With a region, Play starts at the region start (or the cursor, if inside it) and
stops at the region end; the next Play restarts at the region start. Changing the
region while playing restarts at its start. A Loop toggle (button or L) loops the
region, or the whole sample without one. It defaults off, persists across samples
in the session and toggles without restarting playback. Loops wrap gaplessly; the
loop start is held in a small in-memory buffer so wrapping does not depend on
read-ahead. A waveform badge and the position text show the loop mode.

The incremental Debug build, the hardware-independent test suite with new
region/loop transport and selection cases, and an app launch/close smoke check
passed. Looped playback through an audio device has not been listened to or
measured; no Release build or full UI suite was run.

The original scope follows. Add a selection overlay with adjustable start/end boundaries, stored as 64-bit
audio-frame positions with an exclusive end. Support playing and looping only
that selection. Also provide an explicit whole-sample loop toggle in the playback
controls, available without a region selection. It defaults off to preserve the
existing stop-at-end behaviour. Whole-sample and selection looping must be clearly
distinguished, with visible loop state.

This milestone changes playback and selection only, not audio content.
Named/persistent regions belong to E2; manipulation and export belong to E1.

Each step is a separate, usable increment. Apply the fast-feedback policy in
AGENTS.md rather than running the whole chain or a full UI validation suite.

## M3: file management (withdrawn)

Copy, move and rename with an operation history was implemented and then removed
because the controls did not fit the browser. WaveCrate does not change files.
Revisit file organisation only when explicitly requested, with a new design.

## E1: sample editing (deferred epic)

Implement later, only when explicitly requested. This epic gathers audio-content
editing and its supporting tools rather than adding them to the waveform browsing
milestones. Break it into small delivery steps when it is scheduled.

Editing must support both the whole sample and a selected region. Define how
operations affect audio outside the selection, duration, channel layout and saved
region positions before implementing each operation.

| Feature | Intended scope |
| --- | --- |
| Crop/trim | Keep or remove audio using the selected boundaries |
| Normalisation | Normalise the whole sample or selected audio to a chosen target |
| Amplification/gain | Increase or reduce the level of the target audio |
| Reverse | Reverse the whole sample or selected region |
| Zero-crossing snap | Snap selection/edit boundaries to suitable zero crossings |
| Precision zoom | Zoom-to-selection and sample-level detail for accurate editing |
| Stereo to mono | Convert or mix channels, with explicit whole-sample and partial-region behaviour |
| Save/export | Write edited audio or a selected region to a new WAV |
| Export sample rate | Optionally resample on export to a chosen rate, such as 44.1, 22.05 or 11.025 kHz or lower; default keeps the source rate |
| Further manipulation | Gather and scope additional operations when the epic is scheduled |

Preserve original files by default. Define preview, undo/redo and safe save/export
behaviour before implementation, including explicit handling of filename collisions.
Keep lengthy processing off the UI and audio callback threads.

Existing M2.1 zoom/pan remains a browsing feature; E1 adds editing-focused precision.
Normalisation mode, gain/clipping policy, mono mixing rules, partial-region channel
conversion, export resampling quality, anti-alias filtering, the lowest supported
rate, how saved region frame positions map to the new rate, and detailed editing
controls are decisions for this future epic.
Catalogue curation belongs to E2.

## E2: catalogue and curation (deferred epic)

Implement later, only when explicitly requested. How and when to deliver it is
undecided. Break it into small delivery steps when it is scheduled.

| Feature | Intended scope |
| --- | --- |
| Persistent catalogue | SQLite schema with stable sample IDs that do not depend on file paths |
| Indexing | Incremental discovery, header metadata, optional analysis and reconciliation scans in bounded, cancellable jobs |
| Search | FTS5 search over filename, path, tags and notes; duration/channel/rate filters; sorting; saved queries; virtualised result pages |
| Curation | Bulk tags, favourites, ratings, notes and collections without copying files |
| Saved regions | Named regions using M2.2's 64-bit [startFrame, endFrame) selections |
| ZIP entries | Catalogue archive entries from M4 with an archive badge |
| Offline and relinking | Treat disconnected drives as offline; relink moved libraries safely |
| Backups | Consistent catalogue backups and portable curation export |
| Duplicate review | Exact-file hashes on demand, then optional deeper comparison; no automatic deletion |

Keep user curation separate from rebuildable waveform and analysis caches.
SQLite is not a build dependency until this epic is scheduled.

## Desktop workspace

The UI has three main areas: a full-width navigable waveform above a resizable
folder-tree/sample-list split. Open sits beside the Folders heading. Play, Stop
and playback position sit in a transport bar below the waveform, with sample
format details on the right. Redundant waveform/filename and sample-list/path
headings are removed; the list starts at its Name column header. There is no
global toolbar or Up button.
The folder-navigation UI and sample preview are present; M1.1 defines the
opened-path and subfolder-navigation acceptance criteria. Base waveform rendering
and navigation are implemented; editing controls are deferred.
M2.2 region selection, selection audition and whole-sample/selection looping are implemented.
Audio editing and export are deferred to E1.
The new layout is built in Debug; no Release rebuild or full UI validation suite
is required for this iteration.

## Intended features

| Area | Planned behaviour | Status |
| --- | --- | --- |
| Libraries | Folder-tree browsing with lazy expansion now; multiple roots later; offline drives and relinking in E2 | In progress |
| Indexing | Discovery first, header metadata next, optional analysis later; bounded cancellable jobs; reconciliation scans | Planned (deferred E2) |
| Search | Filename, path, tags and notes; duration/channel/rate filters; useful sorting; saved queries | Planned (deferred E2) |
| Preview | WASAPI play/stop/seek, keyboard audition, safe source switching and whole-sample/selection looping now; device/volume controls later | In progress |
| Waveforms | Session-cached overview, zoom/pan and playback cursor implemented; persistent summaries and sample-level detail later | In progress |
| Regions | 64-bit frame selection, audition and loop implemented (M2.2); named regions in E2; export in E1 | In progress |
| Audio editing | Whole-sample/region crop, normalise, gain, reverse, zero-crossing snap, precision zoom, mono conversion and save/export with optional sample-rate conversion | Planned (deferred E1) |
| Curation | Bulk tags, ratings, favourites, notes and collections without copying files | Planned (deferred E2) |
| File operations | Copy/move/rename (M3) removed; not scheduled | Withdrawn |
| Duplicate review | Exact-file hashes on demand, then optional deeper comparison; no automatic deletion | Planned (deferred E2) |
| ZIP browsing | Show ZIPs as browsable folders in the tree (M4); catalogue entries with an archive badge in E2 | Planned |
| ZIP preview | Extract selected WAV to an app-owned temporary directory, then use ordinary playback and waveform code | Planned |
| Cache management | Generated filenames, size budget, eviction, in-use protection, cancellation and archive-change invalidation | Planned |
| Recovery | Device and drive-loss handling in M5; offline state, relinking, catalogue backups and curation export in E2 | Planned |
| Distribution | Static native dependencies where practical, dependency audit, licence notices and clean-machine checks | Planned |

## Architecture direction

Start small. Add these boundaries when the corresponding milestone needs them;
do not create empty frameworks in M0.

| Component | Responsibility |
| --- | --- |
| UI | Virtualised browser results, waveform interactions and user commands |
| Catalogue (E2) | SQLite schema, FTS5 search, metadata, stable IDs and user curation |
| Indexer (E2) | Filesystem discovery, reconciliation and background analysis |
| Preview engine | Playback, resampling, output selection and bounded region transport |
| Sample sources/cache | Resolve local files and ZIP entries to playable files; own temporary cache lifetimes |

Keep the database local even when samples are on network storage. Use short
transactions and a single writer queue. Keep analysis output separate from
user-confirmed metadata.

## Important invariants

- Never modify original audio just to preview, tag or select a region.
- Paths and temporary cache filenames are locations, not permanent sample IDs.
- An unavailable drive is offline, not a deleted library.
- WAVs may be malformed, very long, multichannel or not ordinary 16-bit PCM.
- ZIPs may contain duplicate entry names, encrypted data and hostile size/path
  information. Entry identity cannot be just its display name.
- Apply limits to actual decompression output, not only sizes claimed by an archive.
- Never evict a cached file while playback, waveform analysis or export uses it.
- Waveform region endpoints use [startFrame, endFrame), not pixels or milliseconds.
- Keep filesystem work and database access off the audio callback.
- Preserve UI responsiveness while indexing and rapidly changing selections.

## Deferred decisions

- Set numerical targets for index throughput, search latency, preview latency and
  memory use after measuring a representative collection.
- Choose the initial cache budget and how offline-library pinning should behave.
- Review JUCE licensing and choose WaveCrate's distribution licence before shipping.
- Decide whether tempo/key/loudness analysis is worthwhile after core browsing works.

## Out of scope

- VSTs, plug-in hosting, plug-in builds and other DAW integration.
- A full audio workstation or effects rack. Audio manipulation is limited to the
  deferred E1 epic, with original files preserved by default.
- Cloud accounts, synchronisation, AI classification and similarity search in the
  initial version.
- Cross-platform support in the first release.
