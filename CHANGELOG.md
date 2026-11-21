# Changelog

All notable changes to Suwayomi NX. Versions follow [Semantic Versioning](https://semver.org/); while the version starts
with 0, any release may change things.

## [Unreleased]

### Added
- **Read away from home:** started without your server (or when it can't be reached), the Library shows the manga
  downloaded to this Switch ("Offline: showing what's on this Switch"; X tries the server again), their details and
  chapters come from the SD card, and the reader reads them at once. The progress you read offline is kept on the SD card
  and goes to the server by itself when it is back ("Connected to the server"); the app checks every 30 seconds, when it
  comes back to the front and after sleep. Marks, bookmarks and the other server actions say "This needs the server.".
  A chapter you finished offline stays read; otherwise the later reading wins (by the server's clock).
- **Download to Switch:** chapters can be saved on this Switch's SD card and read from there, with no page coming over the
  network (the reader still asks the server for your progress). On a manga's page, a chapter's menu has Download to Switch,
  Cancel download to Switch, Retry download to Switch or Delete from Switch, and MINUS → Download to Switch... queues the
  next 1, 5, 10 or 25 unread chapters, every unread one or all of them (Local manga too). Rows show "On Switch",
  "Switch: queued" or the download's progress, and Show → Downloaded to Switch lists only those. One chapter downloads at a
  time in the background; HOME and sleep pause it, and it goes on where it stopped after a restart.
- **Switch downloads tab** (before Server downloads; Server downloads and Settings move one place down): the space used,
  the queue (Pause / Resume, Retry failed, Clear...; A on a chapter moves it to the top or bottom, removes it, retries it
  or opens its manga) and the manga on this Switch (A opens its downloaded chapters, Y → Delete all downloads...). While
  this tab shows a running download the screen doesn't dim.
- **Space for downloads** (Settings, 8 GB by default, 2–32 GB or No limit; 1 GB of the SD card always stays free): when it's
  full the queue waits until you delete something; nothing is deleted by itself. **Delete Switch downloads after reading**
  (off by default): a chapter you finish in the reader is deleted from the Switch when you close the reader. Bookmarked
  chapters stay.
- **Same server?** When you change the server address while chapters are downloaded to the Switch, the app asks once
  whether the new address is the same server. Same server: the downloads show at the new address too (handy when the
  server's IP address changed). Another server: they stay with the old address and show again when you go back to it.
- **Settings:** the D-pad moves one row per press all the way down the list (before, a press toward a row below the
  screen's edge sometimes only scrolled a little).
- **Download on server:** your server can download chapters onto its own disk (they open faster and stay readable if the
  source goes away; nothing is stored on the Switch). On a manga's page, a chapter's menu has Download on server, Cancel
  server download or Delete server download, and MINUS → Download on server... queues the next 1, 5, 10 or 25 unread
  chapters, every unread one or all of them. Each chapter row shows "On server", "Server: queued", "Server: 40 %" or
  "Server: failed", and Show → Downloaded on server lists only the downloaded ones. Not offered for Local manga.
- **Server downloads tab:** the server's download queue with each chapter's progress. X (or the button) pauses and resumes
  it, or retries the failed ones; A on a chapter moves it to the top or bottom, removes it, retries it or opens its manga;
  Clear... empties the queue (downloaded chapters stay). The Settings tab is one place further down now.
- **Delete server downloads after reading** (Settings, off by default): a chapter you finish in the reader loses its
  download on the server once you close the reader. Bookmarked chapters keep theirs.
- **Browse:** the Browse tab lists the sources installed on your server by language (the one you used last on top, adult
  sources with an "18+" tag). Open one to see its Popular or Latest manga, search it, or narrow it down with its filters
  (Filters: text, choices, ticks, include / exclude, sort order and groups; Apply searches, Reset puts the defaults back).
  Covers already in your library have an "In library" tag. A manga you open is loaded from its source first.
- **Search all:** Browse → Search all (or MINUS → Search all...) searches every source at once, a row per source with
  how many it found and its first 5 covers (3 sources at a time; the rest wait their turn). A on a source's card opens
  its full results; A on a cover opens the manga, Y adds it to your library.
- **Languages:** Browse → Languages hides the languages you don't read: their sources leave the list and Search all skips
  them (at least one language stays shown).
- **Tick for new manga:** on the Categories screen, a category's menu can mark it "Ticked for new manga": adding a manga
  then starts with it ticked (and goes straight into it after "Don't ask again").
- **Add to and remove from the library:** a manga that isn't in your library has Add to library on its page (and in the
  MINUS menu, and Y on a cover in a source). If you already have a manga with the same name you're asked first (Show
  entry / Add anyway / Cancel); then you pick its categories, with the ones your server marks for new manga already ticked
  ("Don't ask again" skips this next time; Settings → "Ask for categories when adding" turns it back on). Remove from
  library... (MINUS on a manga's page, Y on a library cover) asks first; reading progress stays on the server.
- **Search the library:** the new Search button (or MINUS → Search...) finds manga by title in your whole library, any
  case. The results show in place of the category tabs; B or Clear ends the search.
- **Manage categories:** Edit (or MINUS → Edit categories...) opens the Categories screen: add, rename, move up or down,
  and delete categories (a delete asks first; its manga stay in your library). Default can't be changed. A name that is
  empty, too long or already taken (any case) is refused with a note.
- **Move a manga to other categories:** Y on a cover (it used to open Sort, which is now on its button and in the MINUS
  menu) → Categories..., or the Categories button on a manga's page: tick the categories it belongs in and Save. You can
  make a new category right there.
- **Updates:** the chapters your server found for manga in your library, from the last 3 months, newest first and by day
  ("Today", "Yesterday", "Fri 13 Nov"). A tap reads the chapter, its cover opens the manga, X marks it read or unread.
  **Update library** asks the server to look for new chapters now, shows how far it got and then how many it found (the
  server's update settings decide which manga it checks).
- **History:** each manga you read, the latest first, with the chapter and page you stopped at; a tap goes on reading
  there (like Continue). While a row's manga opens, B cancels it.
- **Progress that couldn't be saved waits:** if the server can't be reached as you close the reader, your page is kept
  (also across a restart) and sent as soon as the server answers again; History shows "page N not saved yet" meanwhile.
  If the chapter changed on the server in between (read on another device), that newer progress wins and yours is
  dropped. Opening the chapter again before that picks up where you were.
- **Zoom:** pinch a page, double-tap its middle, or press ZR / ZL (up to 4 ×). Zoomed in, drag, push either stick or press
  the D-pad to look around; at the edge, a new push or a swipe turns the page. A moment after you stop zooming the page
  turns sharp (it is loaded again at the bigger size, up to 4 × the screen, so large scans stay sharp all the way in). A
  page turn goes back to the whole page; the new reader setting "Zoom on page turn" can keep the zoom instead.
- **Split spreads:** held sideways (portrait), a double-page spread shows as two halves that each fill the screen, one page
  turn each (the right half first when reading right to left). The new reader setting "Split spreads" offers In portrait
  (the default), Always or Never. Both halves count as the same page for your reading progress.
- **Webtoon reader:** a long-strip manga opens as one vertical strip by itself (tagged "Long Strip" or "Webtoon" on the
  server, or a very tall first page); any manga can be switched to it in Reader settings → Reading direction → "Vertical
  strip". Drag, flick, the sticks, the D-pad, L / R and taps (bottom: on, top: back, middle: the menu) scroll it. Chapters
  follow one another below a short band. The exact spot in the page is saved on the server with your progress, so a chapter
  opens where you left it. In landscape the strip is half the screen wide; the new setting "Strip width" offers three
  quarters or full. The strip keeps its own orientation, apart from the page reader's. Zoom in the strip with a pinch, a double tap in
  the middle or ZR / ZL; zoomed, drag, the D-pad's left / right or the stick move it sideways, and the pages on screen
  get sharper once you stop.

### Changed
- A single tap in the middle of the page opens the menu a quarter second later (it waits to see if it is a double tap).

### Fixed
- Holding the D-pad (or a stick) turns only one page, also in portrait.
- A two-page spread split in portrait no longer shows blurry when you open the chapter again at it (Continue).
- Settings -> Server -> **Wait for sources** (30 seconds, 1, 2 or 5 minutes): how long browsing a source, refreshing a manga
  and opening a chapter wait for the server. A server that passes Cloudflare checks with FlareSolverr can need more than the
  default 30 seconds (seen with Weeb Central: "The server took too long to answer").

## [0.2.0] - 2026-10-30

Read with the console held sideways.

### Added
- **Portrait reader:** read with the console held sideways, like a book. Y or the menu's Rotate switches between landscape
  and portrait; the reader settings' Orientation chooses which way you hold it (turned left or right). Pages fill the tall
  screen (a typical page about twice as big), and taps, swipes and the D-pad follow the turned screen. Docked, the reader
  stays landscape. The choice is remembered.

### Fixed
- Quick taps on the page sometimes did nothing when the finger slid a little; they now turn the page like any tap.

## [0.1.0] - 2026-10-30

The first release: read your Suwayomi library on the Switch.

### Added
- **Setup and login:** the server address, login none / basic / the web UI's login, "Test connection". The app checks the
  server at every start and shows a clear message with Retry when it can't be reached.
- **Library:** your categories as tabs, the covers in a grid that scrolls by touch or buttons, sorting by title, unread or
  last read, refresh. It remembers the category and sort.
- **Manga details:** cover, author, status, genres and description; the whole chapter list with sort (newest / oldest first)
  and filter (all / unread / bookmarked); Continue (or Start) where you left off; mark read / unread, bookmark, and "mark
  all before as read / unread"; refresh from the source.
- **Paged reader:** left to right or right to left (the default), fit to screen, width or height, background colour, page
  numbers; double-page spreads are shown whole; the next pages and the next chapter load ahead so turning is instant; the
  previous / next chapter screens; Retry for a page that couldn't be loaded. Direction and fit are remembered per manga.
- **Reading progress** goes to the server (the page every few seconds, "read" at the last page, everything when you leave),
  so Suwayomi's web UI and other clients see it.
- **Touch first:** every screen works fully by touch in handheld mode, and with a controller.
- **Works through a lost connection:** when the server goes away, every screen says so and offers Retry; pages already
  loaded stay readable, failed pages load again once the server is back, and a failed progress save is tried again later
  (waiting longer each time).
- **Safety:** the app writes only inside `sdmc:/switch/suwayomi-nx/`, files are written so that a crash or power loss can't
  leave half a file, and a crash closes the app with a crash report (Settings → Crash logs) and safe mode on the next start.
  The page cache on the SD card keeps at least 1 GB of the card free.
- Application mode only: started from the Album, the app explains how to open it and closes.
