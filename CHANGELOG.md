# Changelog

All notable changes to Suwayomi NX. Versions follow [Semantic Versioning](https://semver.org/); while the version starts
with 0, any release may change things.

## [Unreleased]

### Added
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
