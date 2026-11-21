# IS-001 Weeb Central: "The server took too long to answer" on the Switch

Kind: bug fix · Status: done · Opened: 2026-11-19 · Closed: 2026-11-19
Outcome: fixed: Settings → Server → "Wait for sources" (203cfea, docs b64f87a, D60)
Model: Opus · Owner: – (picked 2 minutes afterwards)
Written up afterwards (2026-11-21, D63) from D60, the two commits and SUWAYOMI-API.md "Slow source answers".

## Report
As D60 records it (the owner's exact words weren't kept): Weeb Central loads in the Suwayomi web UI, but on the
Switch the app says "The server took too long to answer".

## What we checked
- The owner's server has FlareSolverr switched on (their own addition, time limit 90 s; D18, update 2026-11-19).
  With it on, when a site shows a Cloudflare check, the server waits for FlareSolverr before it answers.
- The web UI sets no time limit and simply waits. The app gave up after 30 s (`httpTimeoutMs`, D37), the same
  limit as every other call.
- Three calls make the server ask the source's website before it answers, so they take the site's time:
  `fetchSourceManga` (browse a source), `FetchMangaAndChapters` (refresh a manga), `fetchChapterPages` (open a chapter).
- Timed on the owner's server that day: while all was well, Weeb Central pages took 1–3 s from the web UI and
  1–16 s from the Switch (covers came from the Switch's cache, so the wait was the server's). Once, the server sent
  nothing for over 2.5 minutes to every client, the web UI included.

## Cause
A source call can take the server longer than 30 s when it waits for FlareSolverr or for the site. The app gave up
first; the web UI didn't.

## What changed
- 203cfea: Settings → Server → "Wait for sources": 30 s (default, as before), 1, 2 or 5 minutes, for those three calls
  only (`GqlRequest::asksSource`). Saved as `browse.sourceWaitSeconds` only once chosen. Every other call keeps 30 s,
  images 60 s. Leaving the screen still cancels the wait.
- b64f87a: SUWAYOMI-API.md "Slow source answers", the module READMEs, D18's new server address.
- D60 (the rule) and a CHANGELOG line. Unit tests for the config and the per-call limit; checked on desktop and the Switch.
- Rejected (D60): a longer default, no limit, reading FlareSolverr's time limit from the server's settings.

## For the owner
If a source with Cloudflare checks is slow, pick a longer "Wait for sources" (it has been 2 minutes since).

## If it comes back
- "Took too long" with a long "Wait for sources" already set means the server itself isn't answering. Open the same
  source in the web UI: if it fails or hangs too, the cause is on the server's side. IS-002 found that the server runs
  one FlareSolverr job at a time, so one stuck site makes the others wait. That likely explains the 2.5-minute hang
  seen here too (not checked).
- Not fixed by this: a server that hangs for longer than the choice. The app then still says "took too long" (D60).
