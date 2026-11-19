# WP-18 Download to Switch: chapters on the SD card, a Switch downloads tab, the reader reads them first

Model: **Opus** (D47; never Sonnet: files on the SD card, the reader, memory) · Size: **L** (split point below) · Needs:
WP-13b, WP-13d, WP-17 (all done) · Owner: – (answers given 2026-11-19; optional: try it on your server)

## Goal
After this WP the Switch can **download chapters onto its own SD card** (D47: "Download" means to the Switch, like Mihon),
one chapter at a time in the background, and **the reader reads a downloaded chapter from the SD card**: its pages turn
without a single page request, in the page reader and the webtoon strip. A new **Switch downloads** tab shows the space used,
the queue (Pause / Resume, Clear, per chapter Move to top / bottom, Remove, Retry) and the downloaded manga. The details
screen gets "Download to Switch" entries, a bulk list, tags and a filter, next to WP-17's server entries and never mixed up
with them. Settings get a space limit and "Delete Switch downloads after reading". Everything is application mode only (D21,
D57), with every limit in `RuntimeProfile`, so WP-21 can give `Lite` its own. Reading progress still goes to the server:
starting without the server and syncing offline progress is WP-18b, which builds on what this WP stores.

## Owner's answers (2026-11-19)
1. **Words that name the Switch** (not the plain "Download"): a chapter's menu "Download to Switch", "Cancel download to
   Switch" while it waits, "Delete from Switch" once it is there; row tag "On Switch" (with WP-17's: "On Switch · On
   server"); MINUS "Download to Switch...". WP-17's words keep saying "server".
2. **A space limit in Settings, default 8 GB**: "Space for downloads": 2 / 4 / 8 / 16 / 32 GB / No limit (games share the
   SD card). At least 1 GB always stays free. When full, the queue pauses with a message; **nothing is deleted by itself**;
   deleting something or raising the limit lets it go on.
3. **"Delete Switch downloads after reading": a switch of its own, off**, apart from WP-17's server one, with the same rules
   (finished in the reader, not bookmarked, deleted once the reader has closed).
4. **The tab: queue + downloaded manga**: a space line at the top ("2.3 GB of 8 GB used · 40 GB free on the SD card"), the
   queue while anything waits (WP-17's controls), then "On this Switch": one row per manga (cover, title, chapters, size).
   A row opens the manga's details screen showing only its downloaded chapters; its menu has "Delete all downloads...".
5. **In the background: pause at HOME and sleep, stay awake on the tab.** Downloads pause while the app isn't on screen
   (HOME menu, sleep; S5's "no writes in the background") and go on when it is back. While the Switch downloads tab is on
   screen and a chapter is downloading, the console doesn't dim or sleep by itself: **the owner OK'd widening S9** (today
   keep-awake is for the reader only).
6. **Tab name "Switch downloads"** (amends D58's "Downloads"), **before Server downloads**: Library, Updates, History,
   Browse, Switch downloads, Server downloads, Settings.
7. **Updates rows: not now** (a follow-up; downloads start from the details screen only).

## Checked on the test server (v2.4.2366, 2026-11-19; goes into SUWAYOMI-API.md "Pages for downloads")
Probed with Local fixtures (Alpha), SNX Test (Comet Courier en-01 = manga 158, Missing Page Mansion en-08 = 177, Skyline
Strip en-02 = 195) and the switch file. Probe scripts: curl + jq from WSL, no app code.
- **Page URLs** come from `fetchChapterPages`: `/api/v1/manga/<mangaId>/chapter/<sourceOrder>/page/<index>` (the chapter's
  `sourceOrder`, not its id, SUWAYOMI-API.md), `?format=image%2Fjpeg` added when a format is asked. The same bytes every time
  (sha256 equal). A page index past the end: 404, empty body. Answers are **chunked, without Content-Length**
  (`Cache-Control: max-age=86400`), so the size is known only once the page is in; progress counts pages, not bytes.
- **Content-Type can't be trusted:** an SNX Test page says `image/jpeg` the first time and `image/jpg` once the server has it
  in its own cache. The format is sniffed from the bytes (the loader already does, D39).
- **Sizes:** Alpha's JPEG pages ~60 KB (10 per chapter, ~600 KB), Comet Courier 6 × ~52 KB, Skyline Strip WebP strips
  ~100 KB, covers ~16 KB; Delta Big has 300 chapters. (Real manga pages are bigger, ~0.2–1 MB: a 40-page chapter ~10–30 MB,
  so 8 GB holds a few hundred chapters. An estimate, not measured.)
- **Speed and parallel calls:** from the PC a Local page takes 2–20 ms, an SNX Test page 6–19 ms from the source and ~3 ms
  from the server's cache. With `delayMs` 1000 every page takes 1 s; **6 pages asked at once took 2 s: five answered after
  ~1 s, the sixth after 2 s**, so the server makes at most 5 source calls at once. The downloader uses 2 at a time, which
  leaves room for the reader (`prefetchAhead` 3 + the page on screen).
- `fetchChapterPages`: 18 ms, ~1 s with `delayMs` 1000 (it asks the source for the page list unless the server has it).
  A failing source answers a GraphQL error whose `message` starts "Exception while fetching data (/fetchChapterPages) : SNX
  test failure: pages" and goes on with a long Java stack: show the first line only.
- **Failures:** an `image` failure is HTTP 500 `text/plain` "SNX test failure: image". Missing Page Mansion's chapter 1
  **page index 1** answers HTTP 500 (12 bytes) every time, its other pages work: a chapter that can never fully download. A
  cover that answers 404 at the source (en-06, manga 163) is HTTP 500 `text/plain` from the server.
- **Chapter ids:** `fetchChapters` keeps the ids (578, 579). The held-back chapter 3 got a new id (643, sourceOrder 3);
  withdrawn, the server forgot it (gone from the list). So a chapter on the Switch can vanish from the server's list, and a
  chapter that comes back has a new id. Downloads are kept by chapter id and never move to another chapter.
- `chapterState` (ChapterState.graphql, WP-15b) answers the progress (`lastPageRead isRead lastReadAt meta`) without
  calling the source: what the reader asks for a downloaded chapter instead of `fetchChapterPages`.
- Page requests need the login like every request (basic or Bearer, D38's 401 retry); `aboutServer` has no server id
  (`name version buildType` only), so downloads are filed by the server's address.
- On the console (from the code, nothing run): borealis sets `AppletFocusHandlingMode_NoSuspend`, so the app keeps running
  slowly at HOME (pausing there is our choice); sleep suspends everything and the resume hook resets the connections;
  `statvfs` answers on the Switch (the disk cache's free-space guard, D39, writes only when it does). SD card: FAT32 (STATUS):
  files ≤ 4 GiB, ~65 000 entries per folder: folders by id keep every folder small.
- The probes added chapter rows 641–645 (manga 177, 195, the held-back one); `reset` wipes them.

## Planner's defaults (the owner can change them after trying; record them all in D59)
**Words** (every one names the Switch; WP-17's name the server): menu entries "Download to Switch", "Cancel download to
Switch", "Delete from Switch", for a failed or damaged chapter "Retry download to Switch"; MINUS "Download to Switch..." with
WP-17's six choices ("Next chapter", "Next 5 chapters", "Next 10 chapters", "Next 25 chapters", "Unread chapters", "All
chapters"); Show filter "Downloaded to Switch"; row tags (first on the second line, before WP-17's) "On Switch", "Switch:
queued", "Switch: 40 %", "Switch: paused", "Switch: failed", "Switch: damaged". Messages: "12 chapters queued to download to
this Switch.", "Every chapter is already on this Switch or queued.", "Only the first 1000 chapters fit in the queue."
Sizes: "340 MB", "2.3 GB" (1 GB = 1024³ bytes, as Settings' choices).

**Where the files go** (all through SafeFs, D14; names are ids, never titles):
```
downloads/<server key>/server.json            {"urls": ["http://<server-lan-ip>:4567"]}: the addresses of this server
downloads/<server key>/queue.json (+ .bak)    the queue: paused flag + entries, written like config.json
downloads/<server key>/m<mangaId>/manga.json (+ .bak)   the manga's fields + its finished chapters: the index
downloads/<server key>/m<mangaId>/cover.img   the cover's bytes as downloaded
downloads/<server key>/m<mangaId>/c<chapterId>/0000.jpg ...   the pages, index + the sniffed format
```
- **Server key** = 16 hex of FNV-1a of the address the folder was made for (the address as config.json has it, trailing "/"
  removed, like WaitingSaves' `serverUrl`). The configured server's folder is the one whose `server.json` lists its address.
  Another server's downloads stay on the SD card, hidden, and show again when that address comes back.
- **Same server, new address:** when the user saves a new address in Settings or Setup while the old one has downloads, the
  app asks once: "Is <new> the same server as <old>? Its 12 downloaded chapters stay with <old> otherwise." Yes: the new
  address joins that `server.json` (so going back and forth between a LAN address and a Tailscale name, D18, asks once).
  Only an address the user saves asks, never the test config's server.
- **A chapter counts as downloaded only once it is in `manga.json`**, which is rewritten (SafeFs::writeAtomic + .bak) after
  its last page is written: the commit. Each page is one `writeAtomic` (.tmp → fsync → rename, S5), written once, never
  edited. A chapter entry: id, name, chapterNumber, sourceOrder, scanlator, uploadDate, page count, one format letter per page
  (`j` JPEG, `p` PNG, `w` WebP, `g` GIF; "-" for a page the server listed in a form we can't use), bytes, when. The manga's
  fields: what the details screen shows (id, title, author, artist, description, genres, status, source id and name, the
  thumbnail URL), each cut to a byte limit (S6). WP-18b reads these to show downloaded manga without the server.
- **Read back with S6's limits** (`switchMangaFileBytes`, `switchQueueFileBytes`, the JSON depth): a broken entry is skipped
  and counted, a broken file falls back to its .bak; a manga whose `manga.json` and .bak are both unreadable is shown as
  "damaged" and its folder is never deleted by itself (only by "Delete all downloads..."). Fuzzed.
- **At start** (on the store's thread, not the main one): list the server folder, read each `manga.json`; delete the `.tmp`
  files in chapter folders of queued chapters, and the chapter folders that are neither in a readable `manga.json` nor in the
  queue (a remove or delete that a power cut stopped halfway); a queued chapter's pages already written stay (the download
  goes on from there). Space used = the chapters' bytes + covers + the queued chapters' pages so far.
- **Deleting a chapter**: `manga.json` without it first, then its folder (`removeTree`); the last chapter of a manga with
  nothing queued for it takes the manga's folder too. A crash in between leaves an orphan the next start deletes.

**The store: `ui/downloads/DownloadStore`** (files only, no network): every file call on its own worker thread
(`workerThreadStackBytes`, FIFO), answers on the main thread through `deliver`. Scan, write a page, commit a chapter, write
the cover, delete chapters / a manga, write the queue (debounced `configSaveDelayMs`, at once at HOME / sleep / exit:
`flush()`), used bytes, free bytes (`SafeFs::freeBytes`). Its pure parts (paths, file formats, rules) live in
`core/SwitchDownloads.hpp`. **The build measures `freeBytes()` on the console**: if one call takes over 20 ms, ask it once per
chapter and count what was written since.

**The queue model: `ui/downloads/SwitchDownloads`** (no borealis, like `ServerDownloads`: the client, the store, `Limits`
from the profile, a clock and a schedule hook, several listeners). One for the app, `AppContext::switchDownloads`, made in
`main()` after the client and the loader; MainActivity calls `setServer()` when the server or login changes (the running
download cancels; the new server's folder and queue load).
- **One chapter at a time**, in the queue's order. For each: `fetchChapterPages(chapterId, the reader's format setting)` at
  Background priority (the page list and the count; it stays `fetchChapterPages`'s job to tell the server the count); the
  manga's cover and `manga.json` fields once per manga (a cover that fails: no cover, never a failed chapter); then the pages
  not on the SD card yet, **`switchDownloadPagesAtOnce` (2) at a time**, plain HTTP GETs at `net::Priority::Background`
  (the reader's pages at Reader priority go first) with `maxImageBytes` and `httpImageTimeoutMs`, the 401 retry like the
  loader (`imageAuthRetryMs`). Each page is checked before it is written (S6): HTTP 200, a known format by its bytes
  (`image::sniffFormat`), sides from the header (`readHeader`) within `maxImageSide` and `maxDecodeBytes`. No decoding: the
  CPU stays free for the reader. Then the commit, then the next chapter.
- **A page that fails** (HTTP error, a check fails) is tried `switchDownloadPageTries` (3) times, `switchDownloadRetryMs`
  (1000) apart, doubling. Still failing: the chapter becomes **Failed** ("Failed at page 2 of 10"), keeps its pages, and the
  queue goes on with the next chapter. Retry queues it again: the page list again; a list of another length starts the
  chapter over. **Can't reach the server** (Offline, Timeout) costs no try: the queue waits ("Waiting for the server"),
  `progressSyncMs` doubling up to `progressRetryMaxMs` (D45), and goes on when the server answers. **Auth** (a login that no
  longer works): the queue waits ("Can't log in to the server") until a login works (like WaitingSaves, D53).
- **Space** (before every page): it waits ("Paused: the space for downloads is full (8 GB)") when used + this page would pass
  the limit, or ("Paused: the SD card is nearly full (1 GB left)") when the SD card would drop below `minSdFreeBytes`
  (1 GiB, the disk cache's floor too). Free space unknown: no write (like the disk cache). A delete or a higher limit lets it
  go on by itself. Nothing is ever deleted to make room.
- **Memory:** while the image loader's memory guard pauses prefetch (`prefetchPaused()`), no new page request starts.
- **Background:** the app loses focus (HOME, sleep) → the running requests are cancelled (those pages come again), no more
  writes, `queue.json` flushed; focus back → it goes on. Exit: cancelled and flushed within S5's 1.5 s, before
  `images.shutdown()`.
- **After a restart** the queue comes back from `queue.json` and goes on by itself (unless the user had paused it) once the
  main screen's server check worked.
- **What the tab shows** (a pure rule, `core::switchQueueView`): no entries → Idle; downloading → Running; paused by the user
  → Paused; waiting for space / the server / a login / memory → Waiting with the reason; only failed entries → Stopped
  ("Stopped: 2 failed").
- **Actions** (main thread, at once, no server answer to wait for): `enqueue(manga, chapters)` (at most `switchQueueMax`
  entries in all; chapters already on the Switch or queued are skipped), `removeFromQueue(chapterId)` (a downloading one
  cancels; its pages go), `retry(chapterId)`, `retryFailed()`, `moveToTop` / `moveToBottom` (the downloading chapter goes on
  downloading where it ends up, like the server's queue), `pause()`, `resume()`, `clear()` (queued and failed entries; pages
  so far go; downloaded chapters stay; the rows say "Failed at page N of M" for the first page that failed),
  `deleteChapters(ids, action)` (Delete from Switch, delete after reading),
  `deleteManga(mangaId)`. Errors (a file that can't be written: "Can't write to the SD card") reach the view by `takeError()`.
- **What screens read:** `chapterState(chapterId)` → None / Queued / Downloading (pages done, total) / Paused / Failed /
  OnSwitch / Damaged; `downloadedManga()` (the tab's rows: id, title, chapters, bytes, damaged); `pageFiles(chapterId)` (the
  reader: the page files, or nothing); `space()` (used, limit, free); listeners hear every change, at most one progress
  change per page.
- **Bulk choices** reuse WP-17's rule: `core::chaptersToDownload` becomes one rule over a "skip these ids" set (WP-17: on the
  server or queued there; WP-18: on the Switch or queued here), the same order and choices. Local manga (source "0") may
  download to the Switch: the server reads its own disk and serves the same page URLs.

**The reader reads downloaded chapters from the SD card** (`ReaderModel`, `StripModel`, `ReaderActivity`):
- Both get the chapter files lookup in their options (`SwitchDownloads::pageFiles`). A chapter that is on the Switch opens
  **without `fetchChapterPages`**: the page list is its files; the progress comes from `chapterState` (fast, no source call),
  waited for at most `switchOpenWaitMs` (3000); without an answer by then, or with an error, it opens with what the chapter
  list said (`lastPageRead`, `isRead`; no strip spot) and a late answer is ignored. KOReader's `syncConflict` (only in
  `fetchChapterPages`) isn't asked for downloaded chapters. The next chapter's prefetch (paged) and the chapters the strip
  joins come from the files too when they are on the Switch. The waiting-save take-over (WP-15b) works as before.
- Pages load through the loader with **`LoadRequest::file`** (new: a path inside the app's folder): read on the loader's disk
  thread (`SafeFs::readFile`, `maxImageBytes`), then sniffed, header-checked and decoded as usual; never put in the disk
  cache; the memory cache keyed by the file; covers too (`ImageKind::Cover`). The `image` event says `"source":"download"`.
- **A damaged page** (missing, cut short, won't decode): the reader shows its broken-page state with "This page's download is
  damaged."; the chapter becomes Damaged ("Switch: damaged"); "Retry download to Switch" fetches its missing / broken pages.
  No silent fall-back to the network: what is read from the SD card is predictable, and WP-18b reads with no server anyway.
- Progress saves go to the server as now. A chapter still in the queue (not committed) reads from the server as now.
- `ChapterState.graphql` gains `isBookmarked isDownloaded`, so both "delete after reading" rules (WP-17's needs
  `isDownloaded`, which came from `fetchChapterPages`) still work for chapters read from the Switch.

**The tab: `ui/downloads/SwitchDownloadsTab`** (copy `ServerDownloadsTab`: a one-column `CoverGrid`, a `StatusView`).
- At the top the space line ("2.3 GB of 8 GB used · 40 GB free on the SD card"; No limit: "2.3 GB used · 40 GB free on the SD
  card"); then the queue's state line ("Downloading · 3 in the queue", "Paused: 3 waiting", the Waiting reasons above,
  "Stopped: 2 failed") with **Pause / Resume** (or **Retry failed**) and **Clear...** while it has entries.
- One list, two kinds of rows (one cell class): the queue's entries first (small cover, manga title, chapter name, "Queued" /
  "Downloading 40 %" / "Paused at 40 %" / "Failed at page 2 of 10", a thin progress bar), then a header row "On this Switch"
  and one row per downloaded manga (cover, title, "3 chapters · 1.8 MB", "1 damaged"). Empty: "Nothing is downloaded to this
  Switch yet. A chapter's menu on a manga's screen has Download to Switch."
- Controls (D16): a queue row like WP-17's (**A / tap** → Move to top, Move to bottom, Remove, Retry for a failed one, Open
  the manga; **Y** opens the manga); a manga row: **A / tap** opens its details screen with Show set to "Downloaded to Switch"
  (for that visit only), **Y** its menu (Open, Delete all downloads... → "Delete all 3 chapters of Alpha from this Switch?");
  **X** Pause / Resume (Retry failed) anywhere; **MINUS** Pause / Resume, Retry failed, Clear queue... ("Remove all 14
  chapters from the queue? Chapters already downloaded stay."). A row that leaves keeps the focus at the same index.
- **Keep awake:** `platform::keepScreenOn(true)` while this tab is the shown tab, nothing covers the main screen, the app has
  focus and the queue is Running; off otherwise (the reader keeps its own; the two never overlap: the reader covers the tab).
- It listens while shown; the model downloads whether or not anything watches (no polling here: everything is the app's own).

**The details screen** (`MangaActivity` + `MangaModel`):
- A chapter's menu gets one entry **above** WP-17's server entry: Download to Switch / Cancel download to Switch (queued or
  downloading) / Retry download to Switch (failed, damaged) / Delete from Switch (on the Switch). Local manga too.
- MINUS → "Download to Switch..." (above "Download on server...") → the six choices. Show gets "Downloaded to Switch"
  (`ChapterFilter::OnSwitch`, from the store; offered for Local too). The row tag (above), live from the listener.
- A manga not in the library (Browse's preview) may download too; the tab lists it like any other.

**Settings** (config.json `switchDownloads`, a new section, no migration): "Space for downloads" (2 / 4 / 8 / 16 / 32 GB /
No limit; missing = the profile's `switchDownloadLimitBytes`, 8 GiB) and "Delete Switch downloads after reading" (off; under
it: "A chapter you finish in the reader is deleted from this Switch when the reader closes. Bookmarked chapters stay.").
Delete after reading copies WP-17's rule exactly (the reader's read save answered ok for a chapter unread when it opened, not
bookmarked, on the Switch; one `deleteChapters` once the reader has closed and its saves ended; a failure only logged).

**Tab order:** Library, Updates, History, Browse, **Switch downloads**, Server downloads, Settings (`kTabLabels`,
`MainModel::tabs()`, `tests/e2e/lib/screens.ts` `TABS`: Server downloads and Settings move down 72 px, Settings to y 588;
check every scenario that reaches them by D-pad presses or taps).

**Profile** (`RuntimeProfile`, Full; safe mode and the applet profile: 1 page at a time, WP-21 settles `Lite`):
`switchDownloadPagesAtOnce` 2, `switchDownloadPageTries` 3, `switchDownloadRetryMs` 1000, `switchDownloadLimitBytes` 8 GiB
(the Settings default), `switchQueueMax` 1000, `switchQueueFileBytes` 512 KiB, `switchMangaFileBytes` 1 MiB,
`switchOpenWaitMs` 3000; reused: `minSdFreeBytes`, `maxImageBytes`, `maxImageSide`, `maxDecodeBytes`, `httpImageTimeoutMs`,
`imageAuthRetryMs`, `configSaveDelayMs`, `progressSyncMs`, `progressRetryMaxMs`, `workerThreadStackBytes`.

## Read first
STATUS.md; this doc; `src/ui/README.md`; `src/ui/downloads/ServerDownloads.hpp` **and `.cpp`**, `ServerDownloadsTab.hpp`
**and `.cpp`** (the model and tab to copy); `src/ui/reader/WaitingSaves.hpp` and `.cpp` (a file written like config.json,
flushed at HOME); `src/image/README.md`, `ImageLoader.hpp` + its disk-read path in `ImageLoader.cpp`, `Decode.hpp`,
`DiskCache.hpp`; `src/platform/README.md`, `SafeFs.hpp`; `src/ui/reader/ReaderModel.hpp`, `StripModel.hpp` and their open /
next-chapter / join code, `ReaderActivity.cpp` (closing, keep-awake, WP-17's delete after reading); `src/ui/manga/*.hpp` +
`MangaActivity.cpp`; `src/ui/main/MainActivity.*`, `MainModel.hpp`, `SettingsTab.*`, the setup screen's save;
`src/api/README.md` + `SuwayomiClient.hpp`; `src/core/README.md`, `core/ServerDownloads.hpp`, `RuntimeProfile.hpp`,
`AppConfig.hpp`, `TestConfig.hpp`, `core/graphql/ChapterState.graphql`; `app/AppContext.hpp`, `app/main.cpp` (the focus
handler, the exit steps); SAFETY.md S4–S6, S9, S10; `tests/e2e/README.md`, `lib/server-downloads.ts`, `lib/test-source.ts`,
`screens.ts`; TESTING.md "Test source" and the `server-downloads` scenario.

## Deliverables
- [ ] core: `core/SwitchDownloads.hpp`: the paths (`switchServerKey`, the manga / chapter / page file names), the file
      formats with tolerant readers and writers (`manga.json`, `queue.json`, `server.json`; S6 limits), the space rule
      (fits / limit full / SD full / unknown), `switchQueueView`, `bytesText`; `chaptersToDownload` over a skip set (WP-17's
      callers unchanged in effect). Profile values above; `AppConfig::switchDownloads` (`limitGb`, 0 = no limit;
      `deleteAfterReading`); test config `switchDownloadLimitBytes` and `sdFreeBytes` (a fake free space). Tests
      `test_core_switch_downloads.cpp`; fuzz target `fuzz_switch_downloads` (the three readers) with seeds.
- [ ] core GraphQL: `ChapterState` gains `isBookmarked isDownloaded`; the contract test green.
- [ ] image: `LoadRequest::file` (read through SafeFs on the disk thread, never cached on the disk, memory key by file,
      `"source":"download"`); tests in `test_image_loader.cpp` (a file loads, a missing / cut / too big file fails Corrupt /
      TooLarge, no disk-cache write, no request).
- [ ] ui store: `DownloadStore` (scan, orphans, `.tmp`, pages, commit, cover, deletes, queue file debounced + flush, space,
      a damaged `manga.json` kept); tests in temp folders with a fake free space.
- [ ] ui model: `SwitchDownloads` in `AppContext`; tests `test_ui_switch_downloads.cpp` (FakeHttpClient, a played clock): one
      chapter at a time, ≤ N pages at once at Background priority, the S6 checks, page tries then Failed and the next goes
      on, Offline waits without a try, Auth waits for a login, space waits and goes on after a delete, the memory pause,
      focus lost cancels and writes nothing, a restart goes on from the kept pages, a page list of another length starts
      over, remove / clear delete pages, top / bottom, `setServer()`, destroyed while requests are out.
- [ ] reader: a downloaded chapter opens from its files with `chapterState` (no `fetchChapterPages`, no page request), the
      `switchOpenWaitMs` fall-back, the next chapter and the strip's joins from files, the damaged page; tests in
      `test_ui_reader.cpp` / `test_ui_strip.cpp`.
- [ ] ui views: `SwitchDownloadsTab` (+ `resources/xml/`, `resources/i18n/en-US/switch_downloads.json`), MainActivity (the
      tab, `setServer()`), MangaActivity / MangaModel (menu entry, MINUS submenu, Show filter, tag, live updates),
      SettingsTab (the limit, the switch), the setup / Settings address save ("Same server?"), ReaderActivity (delete after
      reading), `main.cpp` (focus → background, exit order). Touch ≥ 72 px.
- [ ] Test events: `switch_downloads` (view, reason, entries, failed, used / limit / free bytes, `keepAwake`, `queueWrites`
      so far, the first 10 entries: chapter, manga, state, pages done / total; written when what the tab shows changes),
      `switch_download_action` (enqueue / remove / top / bottom / pause / resume / retry / retry_failed / clear / delete /
      delete_manga / delete_after_reading, chapters, ok, error, `ms` from the tap), `switch_download` (a chapter committed or
      failed: chapter, pages, bytes, ms, error); `image` `source` `download`; `manga` gains `switch` (on Switch, queued,
      downloading, failed counts) and filter `downloaded_to_switch`.
- [ ] Integration (`tests/integration/`, all 3 auth modes): `SwitchDownloads` with the real client into a temp SafeFs:
      Alpha chapter 1 (Local) and Comet Courier chapter 1 (SNX Test) → the files' sha256 equal the server's pages; in
      ui_login a download that spans the 20 s token expiry (`delayMs`); Missing Page Mansion chapter 1 → Failed at page 2;
      cleans up after itself.
- [ ] e2e `switch-downloads` (desktop + Switch), helpers in `tests/e2e/lib/switch-downloads.ts`. Known state first and last:
      the queue empty and only **the test server's** downloads folder deleted (found by its `server.json`; never another
      `downloads/<key>/`: on the console the owner's own downloads live next to it), the switch file cleared, Settings back.
      Steps: Alpha → Chapter 1's menu → Download to Switch → "Switch: queued" → "On Switch"; two page files' hashes equal the
      server's (desktop: the file; Switch: sys-autopilot `GET /files/hash`, S10); MINUS → Download to Switch... → Unread →
      "2 chapters queued..."; Comet Courier with `delayMs` 1500 → Next 5 → the tab: rows in order, Running, X → Paused (golden
      `switch-queue`, the space line masked: free space differs per console), X → Resume, Move to bottom / top, Remove → its
      folder gone; `image` fails (the server's page cache cleared first) → "Failed at page 1 of 6" → cleared → Retry → On
      Switch; quit while a chapter downloads → relaunch → it finishes without fetching its kept pages again (`net` events);
      open a downloaded Alpha chapter → pages with `"source":"download"`, no page request; Skyline Strip downloaded → opens as
      the strip from its files; the tab's "On this Switch" (golden `switch-downloads-tab`), A on Alpha → Show = Downloaded
      to Switch (golden `chapters-on-switch`); Delete all downloads... → gone; delete after reading on → read a downloaded
      chapter to its end → close → its folder gone, a bookmarked one stays → off; relaunch with `switchDownloadLimitBytes`
      (~1.5 MB) → "Paused: the space for downloads is full" → Delete from Switch → it goes on; relaunch with `sdFreeBytes`
      just over 1 GiB → "Paused: the SD card is nearly full"; the server stopped → a downloaded chapter opens and turns 3
      pages, then the server starts again. Switch only: HOME → `switch_downloads` Waiting (background) and no request,
      HOME again → Running; `keepAwake` true on the tab while Running, false on Library; 100 entries queued (`delayMs`
      10000) → 10 rounds tab ↔ Library (memory), then Clear.
- [ ] Docs: SUWAYOMI-API.md "Pages for downloads" (the facts above), ARCHITECTURE.md (the downloads folder), SAFETY.md S5
      (pages written once, the `manga.json` commit, the background pause) and **S9** (keep-awake on the Switch downloads
      tab, owner OK 2026-11-19), ui / core / image / platform READMEs, TESTING.md (scenario, events, test config keys),
      `screens.ts`, CHANGELOG.md, D59 "as built", ROADMAP (row done), STATUS.

## Acceptance
- `tools/build.sh tests` (unit + contract + the write gate), `tools/build.sh fuzz` (at least the seeds), `npm test` +
  `npm run typecheck`, `tools/build.sh integration` (3 modes), all desktop e2e green (the tabs moved: `smoke`,
  `setup-first-run`, `auth-matrix`, `touch-pass`, `browse`, `server-downloads` still pass), `tools/build.sh switch` + gate PASS.
- Switch (application mode, handheld, stock clocks): `switch-downloads` green, and `smoke`, `manga-details`, `touch-pass`,
  `browse`, `server-downloads`, `reader-paged`, `reader-webtoon`, `resume-progress`, `offline-errors` green (the reader's
  open path changed). Numbers (D59): a downloaded chapter's first page on screen ≤ 300 ms from the played tap (D54); its
  page turns make no page request; a Local 10-page chapter downloads in ≤ 20 s (record pages per second); `queue.json`
  written at most once per `configSaveDelayMs` while downloading; HOME pauses within 1 s and it goes on after; memory flat
  (+< 16 MiB) over the 10 rounds; `freeBytes()`'s time on the console recorded.

## Split point
If it runs long: **Move to top / bottom**, **delete after reading** (the setting, the reader's part, its steps), **the "Same
server?" question** and **the keep-awake** go to **WP-18c** (run before WP-18b). The store, the queue (Pause / Resume,
Remove, Retry, Clear), the space limit, the background pause and the restart, the reader from the SD card, the details
screen's entries / bulk list / filter / tags and the tab stay.

## Sonnet-safe chunks (Opus WPs)
- The pure core rules and file readers / writers (`core/SwitchDownloads.hpp`) with their tests and fuzz seeds, from the
  header Opus writes first.
- `SwitchDownloadsTab`'s view (XML, texts, the row cell) once `SwitchDownloads` and its tests exist, with
  `ServerDownloadsTab` as the example. Opus reviews both.

## Not in this WP
Starting without the server, the offline details screen and offline progress (WP-18b); download actions on Updates rows
(owner: later); auto-download of new chapters; "download ahead" while reading; a download badge on library covers; deleting
downloads when a manga leaves the library; downloaded chapters the server no longer lists (only the tab's count shows them
until WP-18b's offline list); re-encoding or shrinking pages to save space; many chapters by selection (WP-27); local
CBZ / folders (WP-28); `Lite` limits and applet mode (WP-21).
