# WP-15 Updates + history (and a failed save sent again)

Model: **Opus** (the History list: Sonnet-safe chunk) · Size: M/L (split point below: the waiting-save queue can move to
WP-15b) · Needs: WP-12 (and the readers, all done) · Owner: optional, try it on your own server (Update library with your
real sources, open an update, continue from History, a save made while the server is away)

Planned 2026-11-13 (planning session, owner's answers below). Runs **next** (D51). It is the first screen that polls a job
on the server, a pattern WP-17 copies. WP-18b later grows the waiting-save queue into its offline queue.

## Goal
Today the Updates and History tabs only say "coming soon". After this WP:
- **Updates** lists the new chapters across the library, newest first, under day headers ("Today", "Yesterday", a date).
  A tap opens the chapter in the reader; the cover opens the manga. **Update library** asks the server to look for new
  chapters, shows how far it is ("12 / 40 manga") and what it found ("3 new chapters · 140 manga skipped by the server's
  update settings").
- **History** lists the manga you read, most recent first, one row each with its chapter and page; a tap continues reading
  (the details screen's Continue, D43).
- **A progress save that fails as the reader closes** (the server can't be reached at that moment) is no longer lost
  (D45's leftover, D51): it waits in a small file and goes to the server once the server answers again, even after a
  restart, unless the progress on the server changed meanwhile.

## The owner's answers (2026-11-13)
- **Updates lists new chapters only, like Mihon:** the chapters the server found after the manga was already in the
  library, from the last 3 months. The chapters a manga came with when it was added don't show.
- **A tap opens the reader, like Mihon:** a row in Updates opens that chapter; a row in History continues the manga (the
  same rule as the details screen's Continue). B goes back to the tab. The cover (or the row menu) opens the details.
- **A failed save is kept across a restart:** a small file in the app's own folder, written safely like config.json (S5).
- **If the server's progress for that chapter changed meanwhile** (read on a phone or in the web UI), **the newer server
  change wins:** the waiting save is dropped (and logged). If the server already has the same progress, nothing is sent.

## What the test server showed (v2.4.2366, 2026-11-13; written up in SUWAYOMI-API.md "Updates, history and the library update")
- Units: `fetchedAt`, `lastReadAt` and the manga's `inLibraryAt` are **seconds**; `uploadDate` and `lastUpdateTimestamp`
  are **milliseconds**.
- **Updates:** `chapters(filter: {inLibrary: {equalTo: true}}, order: [{by: FETCHED_AT, byType: DESC}, {by: SOURCE_ORDER,
  byType: DESC}], first, offset)` with `manga { id title thumbnailUrl inLibraryAt }` works; 50 rows are ~17 KB in ~40 ms.
  The seeded library has 459 chapters, all fetched a moment before (or the same second as) their manga's `inLibraryAt`,
  so "new only" hides them all. A library update keeps the old chapters' `fetchedAt`; a new chapter gets the update's
  time. A Local chapter's `uploadDate` is its folder's file time.
- **History:** `chapters(filter: {and: [{lastReadAt: {greaterThan: "0"}}, {or: [{isRead: {equalTo: true}},
  {lastPageRead: {greaterThan: 0}}]}]}, order: [{by: LAST_READ_AT, byType: DESC}, {by: SOURCE_ORDER, byType: DESC}])`
  works. **Trap:** fields written next to `or` (without `and`) are OR'ed with it: that answer listed chapters with
  `lastReadAt` 0. Plain fields side by side are AND'ed. A bulk "mark unread" (`lastPageRead: 0`) stamps `lastReadAt` on
  every chapter in it, hence the filter's "read or with progress" part; `isRead` alone stamps nothing (D43).
- **Library update:** `updateLibrary(input: {})` answers with the **previous** job's status (it looks finished: never use
  it). `lastUpdateTimestamp` changes when an update starts (also when every manga is skipped) and stays during it;
  `libraryUpdateStatus { jobsInfo { isRunning totalJobs finishedJobs skippedMangasCount skippedCategoriesCount } }`
  counts up. So: read the timestamp, start, poll until the timestamp differs and `isRunning` is false. 156 Local manga
  took ~0.6 s (real sources take seconds per manga). `mangaUpdates { status manga { id title } }` lists only the manga
  that ran (`FAILED` among them); skipped ones are only counted.
- **The server's default update settings skip** manga with unread chapters, manga not started and completed manga
  (`excludeEntryWithUnreadChapters`, `excludeNotStarted`, `excludeCompleted`, all true, like Mihon's defaults): on a
  fresh seed all 156 are skipped. A manga whose only chapter was marked read was updated and its new chapter folder found.
- A chapter folder removed + `fetchMangaAndChapters(input: {id, fetchManga: true, fetchChapters: true})` (both fields are
  required, though deprecated) drops the chapter: a test can undo its "new chapter".

## Planner's defaults (the owner can change them after trying; record them all in a new decision D52 at the end)
**Updates tab**
- **New** = the chapter's `fetchedAt` is later than its manga's `inLibraryAt`. **Window:** 90 days back from the newest
  `fetchedAt` in the list (the server's clock, so a wrong console clock doesn't matter). Only manga in the library.
- **Rows:** a small cover, the manga's title, the chapter's name, "Page 5" once started; read chapters dimmed. **Day
  headers:** the local day of `fetchedAt` by the console's time zone: "Today", "Yesterday" (by the console's clock), else
  "Fri 13 Nov" (with the year when it isn't this year). A console whose clock is wrong shows wrong "Today" labels; the
  order and the dates stay the server's. No times in rows (and so stable goldens).
- **Loading:** `updatesPageSize` (50) chapters per request, newest first. Rows the rule hides are skipped; when a page
  leaves less than a screen of rows, the next page loads, at most `updatesFillPages` (5) pages in a row before it waits for
  the user to scroll near the end (a big library of old chapters is never read in one go). The list ends at the window,
  the server's end or `maxUpdateRows` (2000, S6). A chapter seen twice across pages shows once (D42).
- **Header:** an **Update library** button (while an update runs: **Stop**) and a status line: "Last updated: 13 Nov,
  14:32" (`lastUpdateTimestamp`, "never" when 0) → "Updating… 12 / 40 manga" with a thin bar → "3 new chapters · 140 manga
  skipped by the server's update settings · 2 failed" (the failed titles in the log). Empty list: "No new chapters in the
  last 3 months" and "Update library looks for new ones".
- **Update library** = the whole library (`updateLibrary(input: {})`; the server applies its categories' include /
  exclude). Polling every `libraryUpdatePollMs` (1000) only while an update runs, whichever tab or screen is shown (an
  update can outlive the tab); also once when the Updates tab first opens, so an update started elsewhere (the web UI, the
  server's schedule) shows its progress. **Done** = the timestamp differs from the one read before starting and nothing
  runs; if the timestamp hasn't changed after `libraryUpdateStartWaitMs` (10 s) and nothing runs, it is done too ("Nothing
  to update"). **Stop** = `updateStop`, then the same done rule. A poll that fails (server away) says "Can't reach the
  server" in the status line and waits `progressSyncMs` doubling to `progressRetryMaxMs` (D45); the update runs on the
  server anyway. **When done:** the list reloads; "N new chapters" = rows newer than the newest one before the update;
  failures from one more status read with `mangaUpdates`; if anything is new, the library reloads (its unread badges,
  `LibraryModel::refresh` keeps the category).
- **No automatic update from the Switch:** the server's own schedule (`globalUpdateInterval`, set in the web UI) does
  that; the status line shows when the last one ran.
- **Controls (D16):** A / a tap on the row opens the chapter in the reader (at `lastPageRead`, a read chapter at its start,
  D44); B in the reader comes back here, and the list reloads once the progress is saved (`onClosed`). A tap on the cover
  opens the details screen. X toggles read / unread (one `updateChapters`, as on the details screen, D43); Y or the row's
  "..." opens the row menu (Open the manga, Mark read / Mark unread); MINUS the screen menu (Update library / Stop,
  Refresh the list). D-pad up from the first row reaches the header's button. L / R nothing (no categories here).

**History tab**
- **One row per manga:** its chapter read most recently (the first one the server sends; later rows of the same manga are
  skipped, across pages too). Chapters read or with progress, of any manga (in the library or not: WP-14's browse reads
  count too). The same day headers (the local day of `lastReadAt`), cover, title, "Chapter 5 · page 12" or "Chapter 5 ·
  read". `historyPageSize` (50) per request, at most `maxHistoryRows` (500 manga, S6). Empty: "Nothing read yet".
- **A / a tap continues** the manga: its whole chapter list loads, then D43's Continue rule picks the chapter and page;
  every chapter read: the details screen opens instead. The cover / Y: the details screen. X: refresh. MINUS: the menu
  (Refresh). After the reader closes the list reloads.
- **A chapter with a waiting save** (below) keeps its place and the server's text, plus a note "Page 12 not saved yet";
  once the save is sent, the list reloads.

**Opening a chapter from a row** (both tabs): the reader needs the manga's chapter list (next and previous chapter, D44),
so the manga and all its chapters load first (`MangaModel`'s loading, `maxChapters`), then the reader opens. Meanwhile the
row says "Opening…"; B cancels; an error shows as on the details screen.

**The waiting-save queue** (D45's leftover, the owner's answers above)
- **What goes in:** a progress save that fails after the reader began closing: `ProgressSaver::close()`'s last try, and
  the chapters left behind whose save fails after that. A save that fails while the reader is open is retried by the
  reader itself (D45), until it closes. One entry per chapter: a newer one replaces an older one and keeps the older base.
- **What an entry holds:** the server's URL and user (multi-user servers), chapter and manga id, page, read, the strip's
  spot (WP-13b), the **base** (the server's page, read flag and `lastReadAt` as the reader last knew them: from
  `fetchChapterPages`' chapter or the last save that worked; both answers gain `lastReadAt`), the manga's title and the
  chapter's name (for the log and History's note), when it was made (the app clock, for the log).
- **The file:** `progress-queue.json` in the app's folder (D10), written like config.json (`writeAtomic` + `.bak`,
  debounced by `core::SaveScheduler`, waits while the reader moves, D49, flushed at HOME/sleep/exit). At most
  `pendingSavesMax` (100) entries (the oldest goes, logged) and `progressQueueFileBytes` (64 KiB) read (S6); a broken
  entry is skipped, a broken file falls back to the `.bak`, then to empty (logged). No migration: version 1.
- **When it sends:** a round takes the entries of the configured server and user, oldest first, one at a time. A round
  starts after the main screen's connection check works (start-up, Retry, a changed server), whenever the server
  answered anything over HTTP (net's response observer: GraphQL or an image) and no round ran within `progressSyncMs`, and
  after a round that failed for want of the server, `progressSyncMs` later, doubling to `progressRetryMaxMs` (D45).
- **Each entry:** ask the server for the chapter (`chapter(id) { lastPageRead isRead lastReadAt meta }`). Already the same
  as ours (page, read, spot note) → done ("already"). Still at the base → send ours (`setProgress`, with a spot
  `UpdateStripProgress`; never cancelled, D44) → done ("sent"). Else the server moved on → drop ours ("dropped", logged
  with both states). Offline / Timeout → keep it, end the round (wait as above). Auth → keep it, end the round until a
  login works again. Other errors (Server, Parse, Permission, a chapter that is gone) → keep it, count; three in a row for
  one entry → drop it (logged).
- **The reader takes over:** opening a chapter that has a waiting entry, the reader compares the entry's base with what
  `fetchChapterPages` answered. The same: it opens at the entry's page and spot (not the caller's page), treats them as its
  own unsaved progress (saved within `progressSyncMs`), and the entry leaves the queue. Different: the entry is dropped
  (the server moved on).
- **After an entry is sent:** History, the details screen of that manga (if open) and the library's unread badge follow
  (the screens reload what they show, like after the reader closes).

**Unchanged:** the library, the details screen (apart from reloading after a sent entry), both readers and their saves
while open, config.json.

## Read first
`docs/STATUS.md`; D16, D36, D38, D40, D42, D43, D44, D45, D49 ("the spot"), D51 in `docs/DECISIONS.md`;
`docs/SUWAYOMI-API.md` (all of it, short); `src/core/README.md`, `RuntimeProfile.hpp`, `ProgressSync.hpp`,
`SaveScheduler.hpp`, `models/Chapter.hpp`, `Manga.hpp`, `ReadProgress.hpp`, `NodeList.hpp`, `graphql/Operations.hpp` and
`Chapters.graphql`, `FetchChapterPages.graphql`, `UpdateChapterProgress.graphql`, `UpdateStripProgress.graphql`,
`UpdateChapters.graphql`; `src/api/README.md`, `SuwayomiClient.hpp`; `src/net/HttpClient.hpp` (`addResponseObserver`);
`src/platform/README.md`, `Platform.hpp`, `SafeFs.hpp`; `src/app/README.md`, `AppContext.hpp`, `ConfigStore.hpp` and
`main.cpp` (start-up and exit order, the focus flush); `src/ui/README.md` and: `main/MainActivity.{hpp,cpp}` (the tabs),
`library/LibraryModel.hpp` + `LibraryTab.cpp` (the list pattern), `manga/MangaModel.hpp` + `MangaActivity.cpp` (rows,
actions, `openChapter`), `reader/ProgressSaver.{hpp,cpp}`, `ReaderActivity.hpp`, the open path of `ReaderModel` and
`StripModel` (for the take-over), `components/CoverGrid.hpp`, `GridLayout.hpp`, `CoverImage.hpp`, `StatusView.hpp`;
`tests/unit/test_ui_library.cpp`, `test_ui_manga.cpp`, `MainQueue.hpp`; `tests/e2e/README.md`, `lib/suwayomi.ts`,
`lib/library.ts`, `lib/screens.ts`, `scenarios/library-browse.test.ts`, `offline-errors.test.ts` (stopping the server);
`tools/test-server/README.md`, `seed.ts`, `test-server.sh`, `tools/gen-fixtures/fixtures.ts`; TESTING.md "Test hooks" and
"Fixture manga".

## How (the planner's suggestion; the session may find a better way)
1. **core (pure, unit-tested).**
   - Models: `Chapter::fetchedAt` (seconds), `Manga::inLibraryAt` (seconds), a row type `ChapterEntry {Chapter, Manga}` for
     both lists, `ReadProgress::lastReadAt` (answers only), `LibraryUpdateStatus {running, total, finished, skippedManga,
     skippedCategories, lastUpdateMs, failed titles}`.
   - `UpdatesHistory.{hpp,cpp}` (name it better if you like): `isNewChapter`, `updatesWindowStart(newestFetchedAt)`,
     `dayKey(seconds, utcOffset)` and `dayLabel(day, today)`, the header rows of a list, `historyRows` (one per manga across
     pages), and the library update's done rule as a small state machine (`before`, a status, elapsed → Starting /
     Running(finished, total) / Done / NothingToUpdate), so the stale first answer and the 10 s rule are tested here.
   - `ProgressQueue.{hpp,cpp}`: the entry, `addOrReplace` (one per chapter, keeps the older base, the cap), the file to
     and from JSON (tolerant, S6 limits; add it to `fuzz_app_config` or its own fuzz target), `queueDecision(entry,
     serverChapter)` → Already / Send / ServerMoved, and the take-over check. `ProgressSync` keeps the base (it gets the
     server's `lastReadAt` at open and from each saved answer) and can hand out "what isn't saved, with its base".
   - Profile: `updatesPageSize` 50, `historyPageSize` 50, `updatesFillPages` 5, `maxUpdateRows` 2000, `maxHistoryRows`
     500, `libraryUpdatePollMs` 1000, `libraryUpdateStartWaitMs` 10000, `pendingSavesMax` 100, `progressQueueFileBytes`
     64 KiB (applet profile: the row caps halved). Unit test of the profile table.
2. **GraphQL and api.** New documents (file name = operation name, D36): `LibraryUpdates`, `ReadingHistory`,
   `UpdateLibrary`, `LibraryUpdateStatus` (with `lastUpdateTimestamp`; the `mangaUpdates` part only when asked, for the
   summary), `StopLibraryUpdate`, `ChapterState`; `lastReadAt` added to the chapter in `FetchChapterPages`,
   `UpdateChapterProgress` and `UpdateStripProgress`. `SuwayomiClient`: `updates(page)`, `history(page)`,
   `startLibraryUpdate`, `libraryUpdateStatus(withManga)`, `stopLibraryUpdate`, `chapterState(id)` (mutations never
   retried, D38). Contract test; recorded answers (D36: a recording must not change what later recordings show, so record
   `UpdateLibrary` / `StopLibraryUpdate` last or build their answers in the test); the integration tests gain the new
   calls in all three auth modes.
3. **Test server.** A new fixture manga **Epsilon Weekly** [code 8]: 2 chapters × 3 pages, JPEG 1000 × 1500, ongoing
   (`details.json`), plus a held-back **Chapter 3**. All of Epsilon is generated next to the pristine fixtures, not inside
   the folder `reset` copies into `local/` (in there, the server's first browse would number it before Gamma and shift
   every later id). Keep every existing id: the seed's first steps run as today (ids 1–156, chapters 1–459),
   then it copies Epsilon's folder in, browses again, fetches and adds it (manga 157, chapters 460–461), puts it in
   **Reading** (after Alpha and Beta RTL, whose cells stay put) and marks its two chapters read with `isRead` only (no
   `lastReadAt`: History stays empty; with the server's default settings only Epsilon is then updated). Safe to run again.
   Helpers in `tests/e2e/lib/suwayomi.ts` (or `test-server.sh new-chapter add|remove`): `addNewChapter()` copies Chapter 3
   into `root/local/Epsilon Weekly/` with a fixed folder time; `removeNewChapter()` deletes it and runs
   `fetchMangaAndChapters`, so the server forgets it. Then re-record the answers and fix the counts in
   `test_core_graphql.cpp` / `test_core_models.cpp` (156 → 157). Check that `library-browse`, `smoke` and the rest still
   pass; a golden that changes only because of Epsilon gets refreshed with a note.
4. **The queue** (`snx_ui_models`, no borealis; say `ui/reader/ProgressQueue` or its own folder): made in `main()` after
   the client, kept in `AppContext`, its file read at start and flushed at HOME/sleep/exit with config.json. Rounds as
   above: `MainActivity` nudges it after a good connection check; an `HttpClient` response observer nudges it through
   `brls::sync` (it runs on the network thread); the back-off through an injected `schedule(ms, fn)` (the app passes
   `brls::delay`; tests play it with `MainQueue`). `ProgressSaver` gets the queue and hands it a save that fails while
   closing; `ReaderModel` / `StripModel` ask it at open (the take-over). Listeners for "an entry was sent" (History, the
   details screen, the library). `ReaderModel`'s and `StripModel`'s existing tests stay unchanged.
5. **Models.** `UpdatesModel` (rows, paging with hidden rows, the fill limit, the window, dedupe, header rows, and the
   library update: start, poll, stop, done, summary; tests with `FakeHttpClient` and a played clock), `HistoryModel` (rows,
   one per manga, the waiting note), and a small `ChapterOpener` (loads a manga's chapters for the reader, cancellable;
   or reuse `MangaModel`). `MainActivity` owns the Updates and History models (like `library_`, D42: they survive tab
   changes) and drops them when the server or login changes.
6. **Views.** `UpdatesTab` and `HistoryTab` in `src/ui/updates/` and `src/ui/history/`: a `StatusView` and a one-column
   `CoverGrid` (rows ~96 px with a `CoverImage`), the header (button + status line + bar) on Updates. Day headers:
   preferred as their own rows that never take the focus (CoverGrid / `GridLayout` gain a second, smaller row kind, with
   unit tests; the library and details screens keep theirs, their scenarios guard it); if that turns out big, the day goes
   into the first row of each day instead. i18n in `resources/i18n/en-US/updates.json`, `history.json`.
7. **Platform.** `platform::utcOffsetSeconds(utcSeconds)`: the console's time zone (Switch: libnx's time service,
   `timeToCalendarTimeWithMyRule`, read only; desktop: `localtime_r`), logged in `app_start`. Check the safety gate.
8. **Test hooks and the scenario** (below).

## Deliverables
- [x] core: the models' new fields and `ChapterEntry`, `UpdatesHistory` (new rule, window, day keys and labels, header
      rows, history rows, the update's done rule), `ProgressQueue` (entry, add / replace / cap, file round trip and broken
      files, the decision, the take-over), `ProgressSync`'s base, the profile fields. Unit tests: new vs. old by
      `inLibraryAt` (equal = old); the window's end; day labels around midnight and with an offset, "Yesterday" across a
      month end, a date of another year; one history row per manga across pages; the done rule with the stale first
      answer, a skipped-only update, the 10 s rule and a stop; queue: one entry per chapter, the cap drops the oldest, the
      file round trip, a broken entry, a broken file, the 64 KiB limit; already / send / server moved; the take-over both
      ways; `ProgressSync` keeps the base across saves and failures
- [x] GraphQL/api: the six documents, `lastReadAt` in the three answers, the client calls; contract test green, recorded
      answers refreshed, unit tests of every call, integration tests in all three auth modes
- [x] Test server: Epsilon Weekly (TESTING.md's fixture list), the seed's second step, `addNewChapter` /
      `removeNewChapter`, counts fixed, `SNX_FULL_FIXTURES=1 npm test` green
- [ ] Queue in the app: made in `main()`, the file (read at start, flushed with config.json), the three triggers, the
      hand-off from `ProgressSaver`, the take-over in both readers, the listeners. Unit tests (suite `ui`): a close-save
      that fails goes in, a later one for the same chapter replaces it; a round after a nudge sends it (with a spot:
      `UpdateStripProgress`); already; server moved; offline keeps it and backs off; three server errors drop it; the
      take-over in `ReaderModel` and `StripModel` (opens at the entry's page and spot, saves it); a restart (a new queue
      on the same folder) still has it. `ReaderModel` / `StripModel` / `ProgressSaver` tests unchanged otherwise
- [x] `UpdatesModel`, `HistoryModel`, `ChapterOpener` (suite `ui`): pages with hidden rows fill up to a screen and stop
      after 5; the window and the cap end the list; a chapter on two pages shows once; header rows; the update's start,
      progress, done (new count, reload, library reload), nothing to update, stop, a failed poll; History's one row per
      manga, the waiting note; the opener loads all pages and can be cancelled
- [x] Views: `UpdatesTab` (header, status line, bar, rows, day headers, row menu, screen menu), `HistoryTab`, both by touch
      and controller (D16), opening from a row ("Opening…", B cancels), the reload after the reader closes
- [x] Platform: `utcOffsetSeconds` (both), in `app_start`; the Switch build's safety gate PASS
- [x] Test events: `updates` (state, rows, groups, first rows, newest, hidden, pages, end), `library_update` (phase
      started / progress / done / stopped / poll_failed, total, finished, skipped, failed, newChapters, ms), `history`
      (state, rows, first rows with page and read, waiting), `chapter_open` (from, chapterId, page, chapters, ms),
      `progress_queue` (added / sent / already / dropped / kept / taken, chapterId, page, read, reason, waiting)
- [ ] Harness: `progress-queue.json` (+ `.bak`) is removed with config.json before each launch (kept with `keepConfig`),
      so no scenario inherits another's waiting saves; Switch runs back up and put back the owner's copy like their
      config.json (`tests/e2e/out/owner-config/`, `--restore-config`), so a test run never loses or sends the owner's saves
- [x] Scenario `updates-history` (desktop and Switch, TESTING.md 17). First a known state through GraphQL: every chapter
      except Epsilon's marked unread at page 0 (one bulk `updateChapters`; that keeps them out of History, and the
      reads earlier scenarios left behind no longer show or make other manga due for an update), and
      `removeNewChapter()`. The Updates tab by touch: empty, "Last updated" (golden `updates-empty`, the time masked);
      `addNewChapter()`, tap Update library → `library_update` done with total 1, skipped 156, newChapters 1; "Today" +
      Epsilon Weekly Chapter 3 (golden `updates-new`, the status line masked); tap the row → the reader at its page 1
      (page code 8 / 3 / 1), on to page 2, B → the row shows
      "Page 2", History has Epsilon first; Alpha ch1 to page 3 and ch2 to page 2 → History: Alpha first with Chapter 2,
      one row (golden `history`); tap Alpha → Continue opens ch2 at page 2 (code); X in Updates marks Epsilon ch3 read
      (server) and back; the cover → Epsilon's details, B. **The queue:** Alpha ch3 to page 5, `stopServer()`, B
      (`progress` ok false, `progress_queue` added), quit; `startServer()`, start again with the same config → after
      `server_check`, `progress_queue` sent, the server has `lastPageRead` 4, History shows Alpha ch3 page 5. **Server
      moved:** ch3 to page 7, `stopServer()`, B, quit; `startServer()`, set ch3 to page 2 through GraphQL (another device),
      start the app → `progress_queue` dropped (server moved), the server keeps page 2. Delta Big stamped through GraphQL
      (ch 150's `fetchChapterPages` first, else the server saves page 0; then `lastPageRead` 1) → its History row
      continues it at page 2 (`chapter_open` ms). The Switch adds the X / Y / MINUS paths, and
      20 tab switches Library ↔ Updates ↔ History with memory flat
- [x] The existing scenarios stay green: `tools/e2e.sh desktop` (all), and on the Switch `library-browse`,
      `manga-details`, `resume-progress`, `reader-paged`, `reader-webtoon`, `offline-errors`
- [ ] Console (application mode, handheld, stock clocks, D17): the numbers in Acceptance
- [ ] (Optional, the owner's call.) Owner tries it on their server: Update library with real sources (progress and the
      skipped count make sense), an update opened, History's continue, a save made while the server is away
- [x] Docs: D52 (the answers, the defaults as they ended up, the console numbers, the owner's verdict if they tried it),
      ARCHITECTURE.md (Updates / History screens, the queue file in the SD layout D10, the new profile fields),
      TESTING.md (scenario 17, Epsilon, the events and helpers), SUWAYOMI-API.md (anything new found while building),
      SAFETY.md (the queue file in the write-jail list if it names files), `src/core/README.md`, `src/api/README.md`,
      `src/ui/README.md`, `src/platform/README.md`, `tests/e2e/README.md`, `tools/test-server/README.md` (Epsilon, the
      helpers), the user part of README.md ("Not there yet" loses Updates and History), CHANGELOG.md (a version is the
      owner's call)

**Ticked 2026-11-13 (WP-15 without the queue, D52):** the queue's parts of the ticked items (`ProgressQueue`,
`ProgressSync`'s base, `lastReadAt` in the answers, the `progress_queue` event and History's waiting note, the harness, the
scenario's two queue steps), B cancelling "Opening...", and the Switch runs of `updates-history` and the listed scenarios
with the console numbers moved to **WP-15b**. Built as one view and one model for both tabs (`ChapterRowsTab`,
`ChapterRowsModel`, `LibraryUpdater`; opening uses `MangaModel`), the day on the first row of each day (D52).

## Acceptance
`tools/build.sh tests` green with the new unit tests; `npm test`, `npm run typecheck` and `npm run contract` green;
`tools/build.sh integration` green in all three modes. `tools/e2e.sh desktop` green for every scenario.
`tools/e2e.sh switch updates-history` green in handheld mode, plus the Switch scenarios listed above. On the console
(application mode, handheld, stock clocks), against the test server:
- **Update library:** from the tap to `library_update` done ≤ 3 s, with the progress shown while it runs.
- **Opening:** a row to the reader's first page ≤ 1 s (Epsilon, Alpha); History → Continue on Delta Big (300 chapters,
  3 requests) ≤ 2 s.
- **Smooth and flat:** scrolling both lists, frame intervals p90 ≤ 17 ms and none over 34 ms; 20 rounds of tab switches
  grow memory by < 16 MiB after the first; no `mem_pressure`, no `error` event the scenario didn't cause, no crash report.
- **The queue:** both restarts above behave on the console as on desktop; `check-writes.sh` and the safety gate PASS.

## Split point
If the session runs long: Updates (with Update library and its polling) and History land first, green (the scenario
without its queue steps). The waiting-save queue (the file, the rounds, the decision, the take-over, History's note and
the scenario's two queue steps) then moves to a new **WP-15b** in ROADMAP.md (Needs 15, runs right after it, before
WP-24), and STATUS says so.

## Sonnet-safe chunks
- `HistoryModel` and `HistoryTab`, once Opus has built `UpdatesModel` / `UpdatesTab` (the same paging, header rows and
  cells; one row per manga), from a tight spec. Not the waiting note (the queue is Opus).
- Epsilon Weekly in `tools/gen-fixtures`, the seed's second step, `addNewChapter` / `removeNewChapter`, re-recording the
  answers and the count fixes; the contract test of the new documents.
- The docs updates at the end (from a list of facts Opus writes), the i18n strings; refreshing goldens after an intended
  look change.
- Not: the library update's polling and done rule (the pattern WP-17 copies), the queue, `ProgressSaver`, the take-over,
  the response-observer trigger (threads), progress sync (ROADMAP: never Sonnet).

## Not in this WP
Updating one category from the Library tab (a menu entry later, if the owner wants it); selecting several rows at once
(Mihon's batch mark read); downloads from Updates (WP-17, WP-18); removing a manga from History (Suwayomi has no history of
its own: it is the chapters' `lastReadAt`); automatic updates started by the Switch; changing the server's update settings
from the Switch (WP-19's settings); times in the rows; History only for the library; "Today" by the server's clock (the
HTTP `Date` header) for consoles with a wrong clock; KOReader sync's `syncConflict`; progress kept when the app is closed
from HOME while the open reader's save keeps failing, and reading without the server (WP-18b's offline queue).
