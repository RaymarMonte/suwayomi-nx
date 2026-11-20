# WP-18b Offline 2: read away from home (start without the server, offline progress, sync it back)

Model: **Opus** (D47; never Sonnet: progress sync, files on the SD card, the reader, the console) · Size: **L** (split point
below) · Needs: WP-18, WP-18c, WP-15b (all done) · Owner: answers given 2026-11-20 (below); at the end, **try it away from
home** (or with airplane mode on); the Switch runs need the console

## Goal
After this WP the app is useful **without the server**: started away from home (no network, or a network that can't reach
the server), it opens straight into **the manga downloaded to this Switch** (WP-18) instead of an error. Their details and
chapters come from the SD card, the reader reads them as before, and **the progress read offline is kept on the SD card** and
**sent to the server by itself** once the server answers again, with one conflict rule the owner chose (a finished chapter
stays read; otherwise the later reading wins). The app goes offline and back online **by itself**. Two leftovers of WP-18 are
settled: a downloaded chapter the server no longer lists shows tagged, and "delete after reading" works offline. Application
mode only (D21, D57; WP-21 brings it to applet mode), every limit in `RuntimeProfile`.

## Owner's answers (2026-11-20)
1. **Offline look: the Library of downloads.** The Library tab lists the manga on this Switch as one shelf, with a line at
   the top "Offline: showing what's on this Switch". Updates, History, Browse and Server downloads say they need the
   server; Switch downloads and Settings work as usual.
2. **Switching: by itself, both ways.** Offline as soon as the server can't be reached (at start, after waking from sleep,
   or when a screen can't reach it). It checks again every 30 s and when the Switch wakes; once the server answers it goes
   back online by itself with a short note "Connected to the server", and the progress read offline is sent.
3. **Conflicts: a finished chapter stays read, else the later reading wins** (the app compares its clock with the server's).
4. **Rule scope: one rule for all** waiting saves (offline reads and saves that waited at home, D53). **Amends D53's
   "server moved → ours dropped"** (owner OK in this answer).
5. **Gaps: go on and say so.** Reading offline, when the next chapter isn't on the Switch (1–3 and 5 downloaded), the
   "next chapter" screen (and the strip's band between chapters) says so and goes on with chapter 5.
6. **Delete after reading offline: as online.** A chapter finished offline is deleted from the Switch when the reader
   closes (its read waits on the SD card and is sent later).
7. **Offline marks: only reading works offline.** The chapter menu's Mark read / unread and Bookmark say "Needs the server".
   Reading a chapter to its end still marks it read later.
8. **Gone chapters: shown, tagged.** A chapter on this Switch that the server no longer lists stays in the details list at
   its place, tagged "On Switch · Removed from server"; it reads from the SD card and can be deleted; its progress is kept on
   the Switch only.

## Checked on the test server (v2.4.2366, 2026-11-20; goes into SUWAYOMI-API.md "Offline sync")
Probed with curl + jq from WSL (auth none), no app code; the one progress write was put back.
- **Every answer carries a `Date` header** (Jetty: `Date: Fri, 20 Nov 2026 05:03:18 GMT`, page images too), to the second.
  The app learns the server's clock from it: offset = server time − the console's time, taken from the last answers. On the
  PC both clocks are the same machine; on the console the offset is measured (Acceptance).
- **`lastReadAt` is seconds since 1970** (a Long sent as a string): `updateChapter(lastPageRead: 1)` answered
  `"lastReadAt":"1795150998"`, the PC's `date +%s` in the same second. Any save with `lastPageRead` stamps it (D53).
- **Many chapters in one request:** `chapters(filter: {id: {in: [ids]}}) { nodes { id lastPageRead isRead lastReadAt
  isBookmarked meta { key value } } }` answers only the ids that exist (99999 just isn't there: a gone chapter), any order;
  **400 ids in 26 ms**, all in one answer (no paging without `first`). The app asks in batches of `downloadedProgressBatch`.
- Unchanged facts it builds on: `chapter(id)` of a gone chapter answers only an error; a withdrawn chapter that comes back
  gets a new id (D59); the server makes at most 5 source calls at once (D59).
- **Not probed (no server needed):** what the app does without any network. On the console `networkConnected()` (nifm) says
  so at once and curl fails at once; a network that can't reach the server fails in ≤ `httpConnectTimeoutMs` (5 s; a
  refused connection is retried twice, 250 + 500 ms; a connect timeout isn't, D45). Code facts, no request: the TabFrame
  makes a tab's view each time it is shown (so a tab picks up the offline state when it is next opened), and
  `WaitingSaves::save()` writes `progress-queue.json` on the main thread (in a still frame, D49).

## Planner's defaults (the owner can change them after trying; record them all in D61)
**Words:** Library line "Offline: showing what's on this Switch" (no network: "Offline (no network): showing what's on
this Switch") with a **Try again** button (X); the four server tabs "Offline. This tab needs the server; the Library shows
what's on this Switch." (`main/offline_tab`; `connect_first` stays for Checking / Failed); the note "Connected to the
server"; the offline details screen's line "Offline: only the chapters on this Switch"; the mark entries offline answer
"This needs the server."; a gone chapter's mark entries "This chapter was removed from the server."; the gap line "The
chapter before it isn't on this Switch." / "The 3 chapters before it aren't on this Switch." (paged "next chapter" screen
under "Next: Chapter 5", and the strip's band). Nothing downloaded and offline: today's error + Retry (`errorText`).

**The connection (`ui/main/Connection`, new, in `AppContext::connection`; no borealis, hooks like WaitingSaves):**
- **States:** NotSetUp, Checking, Online, Offline (reason **NoNetwork** or **CantReach**). Every screen reads it; listeners
  hear each change (main thread). MainActivity still runs the check (`checkServer`) and reports the result; Connection holds
  the state and decides when the next check runs (a `check` hook MainActivity sets).
- **What counts as offline:** a check that fails with Network, a Timeout before anything was sent (already Network, D45), or
  HTTP 502 / 503 / 504 (a proxy in front of a stopped server). Auth stays the login flow (ShowLogin / ShowSetup, as now);
  any other failure stays Failed (the error + Retry, as now: the server is there but broken).
- **At start:** no network (`platform::networkConnected()` false) → Offline NoNetwork at once, **no request**; else the
  check: ok → Online, failed for want of the server → Offline CantReach (≤ 5 s, the connect timeout).
- **While offline:** a check every `offlineCheckMs` (30 s, the owner's), at once when the app gets the focus back and after
  waking from sleep (the resume hook, D37); with no network, `networkConnected()` every `offlineNetworkPollMs` (2 s) and a
  check as soon as it turns true. The first check that works → Online.
- **While online:** a request to the configured server that ends with Network (net's response observer, network thread:
  it only sets an atomic and posts once, like WaitingSaves' nudge) is a **suspicion**: a check runs (at most one per
  `offlineSuspectGapMs`, 5 s); it fails too → Offline. Waking from sleep → a check (the network may differ now). A failed
  request alone never flips the state; the check confirms it.
- **Going offline:** the Library tab swaps to the downloads shelf (MainActivity keeps the server's `LibraryModel`, not
  shown); the four server tabs show `main/offline_tab` when next opened (a tab on screen keeps what it shows: its own
  errors + Retry, as now); the reader, the details screen and the Switch downloads queue read `offline()` themselves.
- **Back online:** the note "Connected to the server", the Library swaps back (the server's library refreshes, like X),
  `saves.connected()` (the waiting saves go, below), `switchDownloads.connected()` (the queue goes on; the downloaded
  chapters' progress refreshes, below), a details screen on top that shows the offline list reloads from the server.
- **Clock offset:** the same observer reads each answer's `Date` header (`core::parseHttpDate`, untrusted: at most 64
  bytes, RFC 1123 only) and keeps `server seconds − console seconds` (atomic, the newest answer wins); unknown = 0.

**What the Switch knows of its downloaded chapters' progress (`downloads/<server key>/progress.json`, new):**
- One entry per downloaded chapter of that server: **known** = the server's page, read flag, `lastReadAt`, bookmark and strip
  spot note as last seen, and **gone** (the server no longer has it). For a gone chapter also **local** progress (page, read,
  spot, when): its progress lives only here (answer 8).
- **Updated from:** (1) the download: `fetchChapterPages`' chapter (the downloader asks for it anyway, with `lastReadAt`
  and the spot note), written with the commit; (2) after every check that worked:
  `DownloadedProgress.graphql` (the query above) for all downloaded chapters of this server, `downloadedProgressBatch` (500)
  ids per request at Background priority; an id missing from a complete answer is **gone**; (3) a details screen's complete
  chapter list; (4) the reader's `chapterState` answers and saved answers for downloaded chapters; (5) a waiting save sent
  (its answer). Deleting a chapter drops its entry.
- **The file:** version 1, one entry per line, written by `DownloadStore` on its thread like `queue.json` (`writeAtomic` +
  `.bak`, debounced `configSaveDelayMs`, at once at HOME / sleep / exit), only when something changed; read at the folder's
  scan with S6's limits (`switchProgressFileBytes`, JSON depth), a broken entry skipped, a broken file → `.bak` → empty
  (then the offline list shows the chapters unread at page 1, nothing is lost on the server). Fuzzed.

**Offline progress = the waiting-save queue (D53), grown:**
- **Offline, the reader saves to the queue, not the server:** its saves go to `WaitingSaves::add` (no request), so they are
  on the SD card within `configSaveDelayMs` of a still frame; the existing base rule (an entry keeps the older base) makes
  the base the chapter's **known** progress, or the entry it continued. The reader opens a chapter at the **merged**
  position (an entry waiting for it, else known), so it never "takes over" offline.
- **Held entries:** while a reader has a chapter open, that chapter's entry is **held** (in memory only): rounds skip it,
  so the reader is the only one that writes it. Online, a save that fails for want of the server also puts a held copy in
  the queue (power loss / a crash no longer loses it); the reader's next save that works removes it; closing hands over as
  today (D53) and lets go of the hold. After a restart every entry is a normal one. A reader opened offline stays offline
  for its life (its saves keep going to the queue); they go out after it closes.
- **The decision (amends D53, answers 3 and 4),** for every waiting save in a round and for the online take-over:
  **Already** (as now) → nothing; the server still at the base → **Send** (as now); the server moved: ours **read** and the
  server's not → **Send** ("finished"); else ours made later than the server's stamp (`madeAt` + the clock offset >
  `lastReadAt`) → **Send** ("later"; read false leaves the server's read flag alone, D44); else **dropped** ("server later").
  `madeAt` is the newest save of that chapter (a replaced entry takes the newer time).
- **Caps for long trips:** `pendingSavesMax` 100 → **500**, `progressQueueFileBytes` 64 → **256 KiB** (the oldest go first,
  logged, as now). The write stays on the main thread in a still frame unless the console measures a 500-entry write over
  16 ms; then it moves to a worker thread (the text is made on the main thread, written off it).
- History's "page N not saved yet" works as now (online only).

**Offline screens:**
- **Library** (`LibraryModel` offline mode, the same `LibraryTab`): one shelf "On this Switch" from
  `SwitchDownloads::downloadedManga()` (damaged folders left out), each item's cover from its `cover.img`
  (`LoadRequest::file`; none: the URL through the disk cache, else the placeholder), its badge = downloaded chapters not read
  (merged), sorted by the library's sort (Title; Unread; LastRead from the merged stamps). Search filters titles on the
  console (case-insensitive, a pure `core` function); Edit, Categories... and Y → Categories... aren't offered; X = Try
  again. Empty (nothing downloaded): today's error + Retry. Test event `library` gains `offline`.
- **Details** (`MangaModel` offline load): a manga on the Switch opens from its `manga.json` (details) and its downloaded
  chapters (`core::chapterFrom`, plus the merged progress), the offline line above, Continue from them (D43's rule). Sort
  and Show work (Show without "Downloaded on server"); Refresh from source isn't offered; the chapter menu keeps Delete from
  Switch and its mark entries answer "This needs the server." (X too). A manga that isn't on the Switch (a screen opened
  before going offline): its error + Retry, as now. Test event `manga` gains `offline`.
- **Online: gone chapters** (answer 8): once the server's list is complete (and under `maxChapters`), the manga's chapters
  on this Switch that the server didn't list join it at their `sourceOrder` place, tagged "On Switch · Removed from server"
  (`Chapter::goneFromServer`), from `manga.json` with their local progress. They open from their files; their saves go to
  `progress.json`'s local progress (no request, no waiting entry); Delete from Switch works; the mark entries say it was
  removed. The `progress.json` refresh marks them gone too, so offline they show the same tag.
- **The reader offline:** a downloaded chapter opens **without `chapterState`** (no 3 s wait), at the merged position (strip
  spot included); the next chapter and the strip's joins come from the downloaded chapters only, and when the next one
  skips `sourceOrder`s the gap line shows (answer 5; the count is the `sourceOrder` difference − 1). A page turn makes no
  request. Saves go to the queue (above). Delete after reading (answer 6): a chapter whose read went to the queue counts as
  finished, so it is deleted from the Switch once the reader closes, as online; WP-17's "delete server downloads after
  reading" needs the server and is skipped offline (logged, not kept for later).
- **Switch downloads tab:** as now ("Waiting for the server"); its manga rows open the offline details. **Settings:** the
  server line says "Offline" while offline.

**Profile** (`RuntimeProfile`, Full; safe mode the same; WP-21 settles `Lite`): `offlineCheckMs` 30000,
`offlineNetworkPollMs` 2000, `offlineSuspectGapMs` 5000, `downloadedProgressBatch` 500, `switchProgressFileBytes` 512 KiB;
changed: `pendingSavesMax` 500, `progressQueueFileBytes` 256 KiB; reused: `httpConnectTimeoutMs`, `configSaveDelayMs`,
`progressSyncMs`, `progressRetryMaxMs`, `switchOpenWaitMs` (online only now), `maxChapters`.

**Test hooks** (test builds only): test config `"networkConnected": false` (the console "has no network" at start) and a
command `{"cmd":"network","on":true}` (it comes back), `"offlineCheckMs"` (a scenario sets it long, to change the server
while the app stays offline, or short, to see the timer reconnect). Test events: `connection` (`state`, `reason`, `by`:
start / timer / focus / wake / suspect / retry / network, `ms` of the check), `downloaded_progress` (`chapters`, `gone`,
`changed`, `ms`), `progress_queue` gains `held` and the decision's reason (`finished`, `later`, `server_later`), `library`
and `manga` gain `offline`, `reader` / strip events gain `gap` on the next-chapter screen or band.

## Read first
STATUS.md; this doc; D53, D59 and D61 in DECISIONS.md; `src/ui/README.md`; `src/ui/main/MainActivity.*`, `MainModel.*`,
`SettingsTab.hpp`; `src/ui/components/ServerCheck.*`, `StatusView.hpp`; `src/ui/reader/WaitingSaves.hpp` **and `.cpp`**,
`ProgressSaver.hpp` **and `.cpp`**, `ChapterPages.hpp` and `.cpp`, `ReaderModel.hpp`, `StripModel.hpp` and their open /
next-chapter / join / progress code, `ReaderActivity.cpp` (closing, delete after reading); `src/core/ProgressQueue.hpp` and
`.cpp`, `ProgressSync.hpp`, `models/ReadProgress.hpp`, `models/Chapter.hpp`, `SwitchDownloads.hpp`, `RuntimeProfile.hpp`,
`TestConfig.hpp`, `core/README.md`; `src/ui/downloads/SwitchDownloads.hpp`, `DownloadStore.hpp` and the queue-file write in
`DownloadStore.cpp`; `src/ui/library/LibraryModel.hpp` + `LibraryTab.hpp`; `src/ui/manga/MangaModel.hpp` + the menu in
`MangaActivity.cpp`; `src/net/HttpClient.hpp` (the observer, `Response::header`); `src/platform/Platform.hpp`
(`networkConnected`, `setResumeHandler`); `app/AppContext.hpp`, `app/main.cpp` (the observer, focus, resume, exit order),
`app/TestHooks.hpp` (commands); SAFETY.md S5, S6; `tests/e2e/README.md`, `lib/switch-downloads.ts`, `lib/suwayomi.ts`
(`stopServer` / `startServer`), `lib/test-source.ts` (the held-back chapter), `scenarios/offline-errors.test.ts`.

## Deliverables
- [x] core: `ProgressQueue`'s decision with the new rule and its reason (the clock offset passed in), its old tests updated
      (D53's "server moved → dropped" cases become "finished" / "later" / "server later"); `parseHttpDate`; the
      `progress.json` format (known, gone, local; tolerant reader + writer, S6) and the merge rules (`offlineChapter`: a
      downloaded chapter + known + a waiting entry → the `Chapter` the screens show; `offlineManga`: badge and stamp;
      `gapBefore`); local title search; `Chapter::goneFromServer`. Profile values; test config keys. Tests in
      `test_core_progress_queue.cpp` / `test_core_switch_downloads.cpp` (or a new `test_core_offline.cpp`); fuzz:
      `progress.json`'s reader and `parseHttpDate` (a new target or `fuzz_switch_downloads`) with seeds.
- [x] core GraphQL: `DownloadedProgress.graphql`; the contract test green.
- [x] ui `Connection` (+ `AppContext::connection`): the states, the rules above, timers through hooks, listeners; tests
      `test_ui_connection.cpp` (no network → offline without a request; a failed check → offline; 502 → offline, 500 →
      Failed; the 30 s timer, focus and wake checks; the network poll; a suspicion checks once per gap; back online tells
      the listeners once; destroyed with a check out).
- [x] ui downloads: `progress.json` in `DownloadStore` (debounced, flushed) and `SwitchDownloads` (`knownProgress`,
      `noteProgress`, `refreshProgress` after a check, gone marking, local progress for gone chapters); tests in
      `test_ui_switch_downloads.cpp` (batches, gone, a broken file, deletes drop entries, writes only on change).
- [x] ui queue: `WaitingSaves` held entries (`hold` / `release`, rounds skip them, the held copy of a failed online save),
      the new decision with the clock offset, the caps; tests in `test_ui_queue.cpp`.
- [x] ui reader: the offline open (no `chapterState`, the merged position and spot), offline saves to the queue, the gap
      line (paged screen and strip band), delete after reading offline, gone chapters' local saves; tests in
      `test_ui_reader.cpp` / `test_ui_strip.cpp`.
- [x] ui screens: `LibraryModel` offline mode + `LibraryTab` (line, Try again, no Edit / Categories), `MangaModel` offline
      load + gone chapters + `MangaActivity` (line, menu answers, tag), MainActivity (the connection, the swap, the note,
      the tabs' offline text, the refreshes on return), SettingsTab's server line, `main.cpp` (observer, focus, resume).
      Tests in `test_ui_library.cpp`, `test_ui_manga.cpp`. Texts in `resources/i18n/en-US/`. Touch ≥ 72 px.
- [x] Integration (`tests/integration/`, 3 auth modes): `refreshProgress` against the real server (ids, one gone id, a
      batch boundary), the clock offset from `Date` (≈ 0 on the PC), a waiting save decided against real stamps ("later"
      and "server later").
- [x] e2e **`offline-reading`** (desktop + Switch), helpers in `tests/e2e/lib/offline.ts`. Known state first and last (the
      test server's downloads folder and the queue files only, D59; the server running, Settings back). Steps: download
      Alpha chapters 1 and 3 and Gamma Webtoon chapter 1 → quit → server stopped → launch → `connection` offline
      cant_reach → the shelf (golden `offline-library`), Updates says it needs the server → Alpha → the offline details
      (golden `offline-details`), Mark read → "This needs the server." → read chapter 1 to its end → the gap line → on into
      chapter 3 → page 2 → close: `progress_queue` added (ch1 read, ch3 page 2), no page request, no `chapterState` → Gamma
      in the strip to page 2's middle → close → quit **while the reader is open after a save** (the held copy) → relaunch
      offline → Continue = the right chapter and page → delete after reading on: finish a chapter offline → close → its
      folder gone, its entry waits → `networkConnected` false at start → offline no_network with **no request** → the
      `network` command → online by itself.
- [ ] (→ WP-18d) e2e **`offline-sync`** (desktop + Switch): offline with `offlineCheckMs` long; the server starts; through GraphQL the
      server changes Alpha ch1 (page 4, unread: "finished" must win) and ch3 (page 5 later than ours: "server later") →
      Try again → "Connected to the server" → `progress_queue` sent / dropped with those reasons → the server has ch1 read,
      ch3 page 5, Gamma's spot note = ours, and the Library is the server's again; relaunch offline with `offlineCheckMs`
      3000 → the server starts → online by the timer; Comet Courier's held-back chapter 3: shown, downloaded, withdrawn,
      refreshed from the source → "On Switch · Removed from server" (golden `chapters-gone`), it opens from its files, then
      Delete from Switch. Switch only: HOME and back while offline → a `connection` check `by` focus.
- [x] e2e `offline-errors` adjusted: it has no downloads, so its screens stay today's errors, but the app now checks by
      itself (every 30 s) and after the library's failed Refresh: the steps tap Retry only if the error still shows, and
      accept `connection` events. As built: the scenario sets `offlineCheckMs` long (no timer check between startServer
      and the Retry tap); it found that going offline with nothing on the Switch must keep the server's Library on screen,
      and a details screen for a manga not on the Switch must open the normal way (its Retry then works).
- [x] Docs: SUWAYOMI-API.md "Offline sync" (the facts above), ARCHITECTURE.md (offline mode, `progress.json`), SAFETY.md S5
      (`progress.json` written like `queue.json`) and S6 (its reader, the `Date` header), ui / core / net / platform
      READMEs, TESTING.md (the scenarios, events, test config keys, the `network` command), `screens.ts`, CHANGELOG.md,
      D61 "as built" (+ D53's amended note), ROADMAP (row done), STATUS.

## Acceptance
- `tools/build.sh tests` (unit + contract + the write gate), `tools/build.sh fuzz` (at least the seeds), `npm test` +
  `npm run typecheck`, `tools/build.sh integration` (3 modes), all desktop e2e green (the changed open path: `switch-downloads`,
  `switch-download-queue`, `server-downloads`, `resume-progress`, `updates-history`, `reader-webtoon`, `offline-errors`),
  `tools/build.sh switch` + gate PASS, 0 warnings (the Switch build too).
- Switch (application mode, handheld, stock clocks): `offline-reading` and `offline-sync` green, and `smoke`,
  `switch-downloads`, `switch-download-queue`, `resume-progress`, `updates-history`, `offline-errors`, `reader-webtoon`
  green. Numbers (D61): offline start to the shelf on screen ≤ connect timeout + 1 s with the server stopped, ≤ 1 s with no
  network; an offline downloaded chapter's first page ≤ 300 ms from the tap (no 3 s wait); back online within
  `offlineCheckMs` + 1 s by the timer; `progress-queue.json` write with 500 entries on the console (≤ 16 ms, or moved off
  the main thread); the clock offset console ↔ server recorded; memory flat over 10 rounds offline Library ↔ details.
- **The owner tries it away from home** (or airplane mode at home): download a few chapters, start the app offline, read,
  come back, and see the progress on the server (the web UI). Their words go into D61.

## Split point
If it runs long: **gone chapters** (answer 8, the online merge, the tag, local progress, its e2e steps), **the gap line**
(answer 5; offline the reader still goes on to the next downloaded chapter, just without the line) and **the no-network poll
and `network` command** (offline NoNetwork is still detected at start; it then re-checks on the 30 s timer) go to **WP-18d**
(run before WP-21). The connection, the offline Library / details / reader, `progress.json`, the queue's held entries, the
new decision, delete after reading offline and `offline-reading` / `offline-sync` without those steps stay.

## Sonnet-safe chunks (Opus WPs)
- The pure `core` parts (`parseHttpDate`, the `progress.json` reader / writer, `offlineChapter` / `offlineManga` /
  `gapBefore`, the local title search) with their tests and fuzz seeds, from the header Opus writes first.
- The texts and the offline line's XML once the models exist. Opus reviews both. The decision rule, the connection, the
  held entries and the reader stay Opus.

## Not in this WP
Marking read / unread or bookmarking offline (answer 7); History or Updates offline; categories offline (one shelf, answer
1); reading chapters that aren't downloaded; sending WP-17's server deletes later; downloading while offline (the queue waits,
as now); a Tailscale travel router or Funnel + HTTPS (D18, WP-19); local CBZ / folders (WP-28); `Lite` limits and applet mode
(WP-21).

**Build notes (2026-11-20, desktop done; Switch runs next):** the gap line, gone chapters' online merge / tag / local saves
and the server tabs' offline text went to WP-18d (D61 "As built"); `offline-reading` covers the e2e steps of the offline
start, the offline Library / details / reader, the queue going out after the restart and back online by the timer. The
offline-reading helpers stayed in the scenario (no `lib/offline.ts`).
