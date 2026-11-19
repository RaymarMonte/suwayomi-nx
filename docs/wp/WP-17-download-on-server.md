# WP-17 Download on server: a Server downloads tab, chapter actions, delete after reading

Model: **Opus** (owner, 2026-11-19; the roadmap allowed Sonnet) · Size: M (split point below) · Needs: WP-15, WP-23 (both
done) · Owner: – (answers given 2026-11-19; optional: try it on your server)

## Goal
After this WP the Switch can ask the server to **download chapters onto the server's own disk** (they then open fast and
stay readable when the source goes away), and shows the **server's download queue** on a new **Server downloads** tab:
progress, Pause / Resume, Remove, Retry, Move to top / bottom, Clear. The details screen gets per-chapter actions, the web
UI's bulk list ("Next 5", "Unread", ...), a tag on each chapter row and a "Downloaded on server" filter. A setting deletes a
chapter's server download once it is read in the reader. **Nothing is stored on the Switch**: that is WP-18, which gets its
own "Downloads" tab, so the two can never be mixed up. Every word this WP shows says "server".

## Owner's answers (2026-11-19)
1. **A tab of its own, "Server downloads"**, after Browse (Settings moves down one). WP-18's Switch downloads get a
   separate "Downloads" tab later: two tabs, never mixed.
2. **The details screen like Suwayomi's web UI** (checked in its code, `ChaptersDownloadActionMenuItems.tsx`): MINUS →
   "Download on server" → Next chapter, Next 5, Next 10, Next 25, Unread, All (its "Download ahead" needs a reader setting
   we don't have: left out). Plus a **"Downloaded on server" filter** in Show, next to All / Unread / Bookmarked (the web UI
   and Mihon both have a Downloaded filter). Each chapter's menu: Download on server / Cancel server download / Delete
   server download.
3. **The queue:** Pause / Resume, Clear, and per chapter Remove, Retry (failed ones), **Move to top / Move to bottom** (no
   step up / down).
4. **Deleting many: a Settings switch "Delete server downloads after reading"** (neither app has a menu entry; both have
   this setting: the web UI keeps its own in the server's metadata for the web UI, Mihon per phone). One chapter at a time
   from its menu otherwise; WP-27's selection deletes many later.
5. **Model: Opus** (the WP grew from S to M; WP-18 copies its tab and polling). Amends D57's "WP-17 stays S".

## Checked on the test server (v2.4.2366, 2026-11-19; goes into SUWAYOMI-API.md "Server downloads")
Probed with SNX Test chapters (WP-23's source) and the switch file's `delayMs` to slow pages down, polling every 250 ms.
- **`downloadStatus { state queue { position progress state tries chapter { … } manga { … } } }`**: `state` STARTED /
  STOPPED (at rest: STOPPED, `[]`); a row's `state` QUEUED / DOWNLOADING / ERROR (**FINISHED was never seen**);
  `progress` = pages done / pages (0.333 steps for 3 pages; 0 while the page list loads); `position` 0..n-1, renumbered.
  147 queued chapters with the fields the tab needs: 39 KB, ~10 ms.
- **Enqueue** (`enqueueChapterDownloads(input: {ids})`) answers the queue with the new rows QUEUED **and the state still
  STOPPED**; the downloader starts by itself ~0.75–1.25 s later, **also after the user stopped it** (any enqueue resumes the
  whole queue). A chapter already queued isn't added twice. An already downloaded one is queued and leaves at once. **A
  Local chapter is queued and silently dropped** when the downloader reaches it (`isDownloaded` stays false; its files are
  on the server's disk anyway), so the app never offers server downloads for Local (source "0"). An unknown id waits 30 s
  and fails (WP-23).
- **A finished chapter leaves the queue at once**; the queue going empty sets STOPPED by itself (after removing the last
  downloading row it showed STARTED with an empty queue for ≥ 0.5 s first). So the app learns "downloaded" only from the
  chapter's `isDownloaded`, never from the queue.
- **Stop** (`stopDownloader`): STOPPED at once (8 ms); the downloading row goes back to QUEUED **with its progress kept**
  (0.666), and a start goes on with the pages left.
- **Start** (`startDownloader`) from STOPPED with QUEUED rows answers after ~1.2 s (it waits until STARTED); while STARTED,
  10 ms. **With nothing QUEUED (empty, or only ERROR rows) it waits 30 s and fails** ("Timed out waiting for 30000 ms").
- **Errors:** a chapter whose pages fail gets 3 tries within ~1 s, then stays as ERROR (`tries` 3, progress 0). The
  downloader goes on with the next chapter (a ja chapter after an en failure downloaded) and stops (STOPPED) when only ERROR
  rows are left. **Retry = enqueue it again**: QUEUED (tries stays 3), the downloader starts by itself, downloaded on
  success (Sorayomi's Retry is remove + enqueue). `startDownloader` doesn't retry ERROR rows (it hangs, above).
- **Remove** (`dequeueChapterDownloads(input: {ids})`) of a QUEUED or the DOWNLOADING row: at once; a half-done chapter is
  not downloaded afterwards. **Of a chapter that isn't in the queue (it just finished, say): waits 30 s and fails** (the
  app's own `httpTimeoutMs` is 30 s too). The single-id `dequeueChapterDownload(input: {id})` exists as well.
- **Reorder** (`reorderChapterDownload(input: {chapterId, to})`): `to` is the 0-based place in the whole queue, the
  downloading row included; the downloading row goes on downloading wherever it ends up. Top = 0, bottom = rows − 1.
- **Clear** (`clearDownloader(input: {})`): empties the queue (ERROR rows too), STOPPED, 10 ms.
- **Delete** (`deleteDownloadedChapters(input: {ids})`) → `chapters { id isDownloaded }` (in id order). Harmless for a
  chapter that isn't downloaded, for a Local chapter (its files stay, it still opens) and for an unknown id (answers `[]`).
  A queued chapter stays queued: deleting is not removing. Files on the server: `downloads/mangas/<source display
  name>/<title>/<scanlator_>Chapter N/001.jpg …` + `ComicInfo.xml`.
- Per manga: `manga { downloadCount }`; per chapter: `isDownloaded` (already in `Chapters.graphql`). The server's own
  settings (`downloadAsCbz`, `autoDownloadNewChapters`, …) stay the web UI's business. There is **no server setting for
  deleting after reading**: it is the client's job.
- Not testable here: the multi-user `DOWNLOAD_CHAPTERS` permission (preview servers; D38's `Permission` error kind): a unit
  test with a hand-built answer.

## Planner's defaults (the owner can change them after trying; record them all in D58)
**Words** (every one says "server", so WP-18's "Download" / "on this Switch" can't be mixed up): tab "Server downloads";
menu entries "Download on server", "Cancel server download", "Delete server download"; MINUS "Download on server..." with
"Next chapter", "Next 5 chapters", "Next 10 chapters", "Next 25 chapters", "Unread chapters", "All chapters"; filter
"Downloaded on server"; row tags (first on the row's second line, so they are never cut off) "On server", "Server: queued",
"Server: 40 %", "Server: paused 66 %", "Server: failed".

**The model: `ui/downloads/ServerDownloads`** (no borealis, like `LibraryUpdater`: the client, `Limits` from the profile, a
`Clock` and a `Schedule` hook, several listeners). One for the app, in `AppContext::downloads`, made in `main()` after the
client like `WaitingSaves`; MainActivity calls `reset()` when the server or login changes (forget the queue, cancel).
- **The queue it knows** = the last `downloadStatus` the server sent (any call's answer: every download mutation answers
  the queue too), at most `serverQueueMax` rows kept (S6; the tab says "and 240 more").
- **Newest answer wins:** each request (a poll or an action) gets a number when it is sent; an answer older than the last
  one applied is dropped. (A slow remove that waited 30 s must not bring back an old queue.)
- **What the tab shows** (`View`, a pure rule `core::queueView`): no rows → **Idle** ("Nothing in the server's queue."); only
  ERROR rows → **Idle** with "Stopped: 2 failed"; STARTED → **Running**; STOPPED with a QUEUED row within
  `serverDownloadStartWaitMs` (5 s) after our enqueue / retry / resume → **Starting** (the enqueue's own answer says STOPPED,
  above); STOPPED with a QUEUED row otherwise → **Paused**.
- **Polling:** `downloadStatus` every `serverDownloadPollMs` (1000) **only while someone watches and the queue is busy**
  (Running or Starting). Watching: the tab while it is the shown tab and the main screen is on top; a details screen while
  it is on top (not while its reader is open: the reader's frames come first). A new watcher gets one read at once (so a
  queue started in the web UI shows), MINUS → Refresh another. Paused, Idle or nobody watching: no requests (the server
  downloads on its own). A failed poll: "Can't reach the server", then `progressSyncMs` doubling up to
  `progressRetryMaxMs` (D45, as `LibraryUpdater`).
- **Chapters that leave the queue** without our Remove / Clear (most likely downloaded; maybe removed in the web UI) are
  reported to the listeners as "left"; deleted ones (our delete's answer) as "deleted". The details screen reloads its
  chapter list for "left" (the truth is `isDownloaded`), at most every `serverDownloadReloadMs` (3000) while more keep
  leaving and once after the last; "deleted" it applies at once (`MangaModel::applyDownloaded`).
- **Actions** (mutations, never repeated; each answer is applied by the rule above, then the poll goes on):
  `enqueue(ids)` (≤ `serverDownloadBatchMax` = 1000 ids per request, more in several), `remove(chapterId)` (**only a
  chapter in the last queue**, one id per request; a failure polls once and stays quiet if the chapter is gone by then),
  `retry(chapterId)` and `retryFailed()` (= enqueue), `moveToTop` / `moveToBottom`, `pause()`, `resume()` (**only when a row
  is QUEUED**: with only failed rows the tab offers Retry failed instead), `clear()`, `deleteDownloads(ids)`. Errors reach the
  view through `takeError()` (Permission: "Your account may not download chapters on this server.").
- **Bulk choices** are a pure rule, `core::chaptersToDownload(chapters, choice, queuedIds)`: reading order (oldest first,
  `sourceOrder`), not downloaded, not queued; Next N and Unread take unread ones only (the web UI's `onlyUnread`), All
  takes read ones too; Next N takes the first N. Nothing left: "Every chapter is already on the server or queued." Otherwise
  "12 chapters queued on the server." Local manga: no MINUS entry and no row entry.

**The tab: `ui/downloads/ServerDownloadsTab`** (copy `ChapterRowsTab`: a one-column `CoverGrid` of rows, a `StatusView`).
- A header line with the state ("Downloading 2 of 14", "Starting...", "Paused: 14 waiting", "Stopped: 2 failed",
  "Can't reach the server") and buttons **Pause / Resume** (or **Retry failed**) and **Clear...**.
- A row: the small cover (Updates' row size), manga title, chapter name, the state ("Queued", "Downloading 40 %", "Paused at
  66 %", "Failed after 3 tries") and a thin progress bar (a box whose width is the share; no font signs needed).
- Controls (D16): **A / tap a row → its menu** (Move to top, Move to bottom, Remove, Retry for a failed row, Open the manga);
  **X** Pause / Resume (Retry failed when only failed rows wait); **Y** opens the manga; **MINUS** the tab's menu (Pause /
  Resume, Retry failed, Clear queue..., Refresh). Clear asks first: "Remove all 14 chapters from the server's queue? Chapters
  already downloaded stay." A row that leaves the queue keeps the focus at the same index.
- The rows follow the server's order; an action shows the server's answer (no guessing ahead).
- Paused adds: "Any new Download on server resumes the queue." (the server's own rule, above).

**The details screen** (`MangaActivity` + `MangaModel`):
- A chapter's menu gets a 5th entry from the queue it knows: Download on server, Cancel server download (queued or
  downloading), or Delete server download (`isDownloaded`). None for Local. The four reading actions stay as they are (D43).
- MINUS → "Download on server..." → the six choices above (a second dropdown, the `brls::sync` way in ui/README.md).
- Show gets "Downloaded on server" (`ChapterFilter::DownloadedOnServer`: `isDownloaded`).
- The row tag (above). It watches `ServerDownloads` while on top; "left" chapters reload the list, "deleted" ones change in
  place.

**Delete after reading** (Settings: "Delete server downloads after reading", off; config.json `serverDownloads.
deleteAfterReading`, a new section, no migration):
- A chapter counts when **the reader** saved it read (`ProgressSaver`'s save with `isRead` answered ok, for a chapter that
  was unread when it opened), in the page reader or the strip. Chapters marked read by hand (X, menus, Mark previous) and
  saves the waiting-save queue sends later don't count (Mihon's "when manually marked as read" is a separate option: not
  now).
- Only chapters downloaded on the server and **not bookmarked** (Mihon's and the web UI's default). `FetchChapterPages`'
  chapter asks `isDownloaded isBookmarked` too.
- **When the reader has closed** and its saves have ended (`ProgressSaver::close`'s done), one `deleteDownloads(ids)` for
  all of them, at Background priority, never repeated, a failure only logged (the download stays). Like Mihon: a page
  turned back in a finished chapter still comes from the server's disk. The details screen gets them as "deleted".
- Settings says under the switch: "A chapter you finish in the reader loses its download on the server. Bookmarked
  chapters keep theirs. Other apps using this server see it gone too."

**Tab order:** Library, Updates, History, Browse, **Server downloads**, Settings (`kTabLabels`, `MainModel::tabs()`,
`tests/e2e/lib/screens.ts` `TABS`: Settings' touch point moves down 72 px; check every scenario that reaches Settings by
D-pad presses).

## Read first
STATUS.md; this doc; `src/ui/README.md`; `src/ui/updates/LibraryUpdater.hpp` **and `.cpp`** (the pattern to copy: hooks,
listeners, back-off) and `ChapterRowsTab.hpp` / `.cpp` (the row tab to copy); `src/ui/manga/*.hpp` + `MangaActivity.cpp`
(row cell, row menu, MINUS menu, Show); `src/ui/main/MainActivity.*`, `MainModel.hpp`, `SettingsTab.*`;
`src/ui/reader/ProgressSaver.hpp`, `ReaderActivity.cpp` (closing); `src/api/README.md` + `SuwayomiClient.hpp`;
`src/core/README.md`, `core/graphql/Operations.hpp`, `core/models/Chapter.hpp`, `LibraryUpdate.hpp`, `RuntimeProfile.hpp`,
`AppConfig.hpp`; `app/AppContext.hpp`, `app/main.cpp` (where `WaitingSaves` is made); `tests/e2e/README.md`,
`tests/e2e/lib/test-source.ts`, `screens.ts`; TESTING.md "Test source" and scenario 17 (`updates-history`, the polling test).

## Deliverables
- [x] core: `core/ServerDownloads.hpp`: `DownloadRow` (chapter id, name, sourceOrder; manga id, title, thumbnail; state
      Queued / Downloading / Error; progress 0..1; tries; position), `DownloadQueue` (started, rows, rows left out),
      `readDownloadQueue` (tolerant: a broken row is skipped and counted, at most `serverQueueMax`), `queueView` (the View
      rule above), `percentText` (rounded down: 99.9 % is "99 %"), `DownloadChoice` + `chaptersToDownload`. Profile:
      `serverDownloadPollMs` 1000, `serverDownloadStartWaitMs` 5000, `serverDownloadReloadMs` 3000, `serverQueueMax` 1000,
      `serverDownloadBatchMax` 1000. `AppConfig::serverDownloads.deleteAfterReading`. Tests `test_core_downloads.cpp`.
- [x] core GraphQL: documents `DownloadStatus`, `EnqueueChapterDownloads`, `DequeueChapterDownload`,
      `ReorderChapterDownload`, `StartDownloader`, `StopDownloader`, `ClearDownloader` (each answers the same queue
      fields), `DeleteDownloadedChapters`; `FetchChapterPages` gains `isDownloaded isBookmarked`. Builders and readers in
      `Operations`; the contract test green; a recorded `app-download-status.json` (empty queue) plus hand-built answers
      (rows, ERROR, a 30 s timeout error text) in the tests.
- [x] api: `downloadStatus()`, `enqueueDownloads(ids)`, `dequeueDownload(chapterId)`, `reorderDownload(chapterId, to)`,
      `startDownloads()`, `stopDownloads()`, `clearDownloads()`, `deleteServerDownloads(ids)` (answers the chapters' id +
      `isDownloaded`). Mutations never repeated. Tests in `test_api_client.cpp` (also a Permission answer).
- [x] ui model: `ServerDownloads` in `AppContext`; tests `test_ui_downloads.cpp` (FakeHttpClient, a played clock): polls
      only while watched and busy, one read per new watcher, the STOPPED answer after an enqueue shows Starting, an older
      answer never replaces a newer one, remove only what is listed (and a failed remove of a gone chapter stays quiet),
      resume only with a QUEUED row, retry = enqueue, back-off, "left" / "deleted" reported, `reset()`, destroyed while a
      request is out.
- [x] ui views: `ServerDownloadsTab` (+ `resources/xml/`, `resources/i18n/en-US/server_downloads.json`), MainActivity's
      new tab and `reset()` on server change, MangaActivity / MangaModel (menu entry, MINUS submenu, Show filter, row tag,
      `applyDownloaded`, reload on "left"), SettingsTab (the switch), ReaderActivity (delete after reading). Touch ≥ 72 px.
- [x] Test events: `server_downloads` (view, rows, failed, left out, pollFailed, `polls` so far, the first 10 rows: chapter,
      manga, state, percent, tries; `ms` to apply the last status; written when what the tab shows changes),
      `server_download_action` (action enqueue / remove / top / bottom / pause / resume / retry / retry_failed / clear /
      delete / delete_after_reading, chapters, ok, error, `ms` from the tap); `manga` gains `server` (downloaded, queued,
      downloading, failed counts) and `filter` `downloaded_on_server`.
- [x] Integration (`tests/integration/`, all 3 auth modes, SNX Test chapters with `delayMs`): enqueue → status lists them →
      reorder → remove → stop → start → clear; enqueue → finished → `isDownloaded` → delete; cleans up after itself.
- [x] e2e `server-downloads` (desktop + Switch ✅ 2026-11-19), helpers in `tests/e2e/lib/server-downloads.ts`; known state first and
      last (clear the queue, delete the test manga's server downloads, the switch file cleared, the test manga out of the
      library): Paper Garden (SNX Test EN, added to the library by GraphQL) with `delayMs` 1500 → Chapter 1's menu →
      Download on server → its tag "Server: queued", then a %; MINUS → Next 5 → "5 chapters queued" (2–6); the tab: the
      rows in the server's order, Running; X → Paused (golden `server-queue`, paused so the % stands still), the server
      STOPPED; X → Resume; Move to bottom / Move to top → the server's order; Remove → gone on the server; the switch file
      fails `image` → Chapter 8 → "Failed after 3 tries" → failure cleared → Retry → downloaded; `delayMs` 0, the queue
      empties (golden `server-empty`); back on the details screen the downloaded rows say "On server", Show → Downloaded on
      server lists exactly them (golden `chapters-on-server`); Delete server download → the tag goes, the server agrees;
      Unread slowed down → Clear queue... → confirm → empty; Alpha (Local): no server entries; Settings → the switch on →
      read a downloaded chapter to its last page → close → the server's `isDownloaded` false, a bookmarked one keeps its
      download → switch off; while the Library tab shows, no poll (`polls` unchanged over 5 s). Switch only: 100 rows
      queued by GraphQL (`delayMs` 10000) → the tab, 10 rounds tab ↔ Library (memory), then Clear.
- [x] Docs: SUWAYOMI-API.md "Server downloads" (the facts above), ui / core / api READMEs, TESTING.md (scenario, events),
      `screens.ts` comment, CHANGELOG.md, D58 "as built", ROADMAP (row done), STATUS.

## Acceptance
- `tools/build.sh tests` (unit + contract), `npm test` + `npm run typecheck`, `tools/build.sh integration` (3 modes), all
  desktop e2e green (the Settings tab moved: `smoke`, `setup-first-run`, `auth-matrix`, `touch-pass`, `browse` still
  pass), `tools/build.sh switch` + gate PASS.
- Switch (application mode, handheld, stock clocks): `server-downloads` green, and `smoke`, `manga-details`, `touch-pass`,
  `browse`, `updates-history`, `reader-paged`, `resume-progress` green (the reader's closing changed). Numbers (D58): an
  action → its answer on screen ≤ 1 s (the event's `ms`, from the played tap as D54); a status of 100 rows applied ≤ 50 ms;
  memory flat (+< 16 MiB) over the 10 rounds; no requests while nothing watches.

## Split point
If it runs long: **delete after reading** (the setting, the reader's part, its scenario step) and **Move to top / bottom**
go to WP-17b. The tab, Pause / Resume, Remove, Retry, Clear, the details screen's entries, the bulk list and the filter stay.

## Sonnet-safe chunks (Opus WPs)
- The pure core rules (`readDownloadQueue`, `queueView`, `percentText`, `chaptersToDownload`) and their tests, from the
  header Opus writes first.
- `ServerDownloadsTab`'s view (XML, texts, the row cell) once `ServerDownloads` and its tests exist, with `ChapterRowsTab`
  as the example. Opus reviews both.

## Not in this WP
Download count badges on library covers; the server's own download settings (CBZ, path, auto-download of new chapters:
the web UI sets them); "download ahead" while reading; deleting when marked read by hand; download actions on Updates rows;
many chapters at once by selection (WP-27); the `downloadStatusChanged` subscription (needs a WebSocket, D47); anything
stored on the Switch (WP-18).
