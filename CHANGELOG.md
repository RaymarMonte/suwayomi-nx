# Changelog

All notable changes to Suwayomi NX. Versions follow [Semantic Versioning](https://semver.org/); while the version starts
with 0, any release may change things.

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
