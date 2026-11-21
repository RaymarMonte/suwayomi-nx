# IS-002 WebtoonXYZ won't load on the Switch

Kind: investigation · Status: done · Opened: 2026-11-20 · Closed: 2026-11-20
Outcome: not ours: the server's FlareSolverr got no answer through webtoon.xyz's Cloudflare check, and the web UI
failed the same way. No code changed.
Model: Opus · Owner: had the console on and the web UI open in Brave; the server-side steps are theirs
Written up afterwards (2026-11-21, D63) from that session's notes.

## Report
"I'm unable to browse webtoon xyz at all in the Switch but I'm able to access it in the Suwayomi WebUI."
The owner also asked to try the Switch first and the web UI afterwards.

## What we checked
All on 2026-11-20, the owner's server, a Switch test build of that day's main, with the owner's own config.
- **16:02, Switch, WebtoonXYZ → Popular** ("Wait for sources" set to 2 min): the screen stayed at "Loading WebtoonXYZ
  (EN)...". One request answered at once (likely the filters); the page request (`fetchSourceManga`, POPULAR, page 1)
  got no answer. After 2 min: "The server took too long to answer."
- **16:04, Switch, Latest:** the same, no answer in 120 s.
- **Server settings** (web UI, read only): FlareSolverr on, time limit 90 s, "Response fallback" off.
- **16:07, web UI, WebtoonXYZ → Popular** (the same request the app sends): after 83 s, "Unable to load data …
  java.io.IOException: timeout". Its "Show more" has the server's stack: `CloudflareInterceptor` → timeout.
- **16:09, web UI, Retry:** no answer after 2.5 minutes.
- **Suwayomi-Server's `CloudflareInterceptor.kt`** (read on GitHub): every FlareSolverr job runs behind one lock for
  the whole server, each for up to its time limit + 10 s (here 100 s). When the client gives up, the job keeps going,
  and a retry adds another. So a stuck site builds a queue of minutes, and other Cloudflare sites wait in it too.
- FlareSolverr's own port (8191) isn't reachable from the PC, so its log wasn't read.

## Cause
webtoon.xyz answers the server with a Cloudflare check. The server hands it to FlareSolverr, which sent nothing back
within its time limit, so the server gave up ("timeout"). The app and the web UI get the same failure; the app shows
it as "took too long" when its own limit runs out first. When FlareSolverr does get a check through, the site works
for a while for every client, which fits the web UI working at other times.

## What changed
Nothing in the app. This write-up came later (D63).

## For the owner
On the server machine: read FlareSolverr's log for webtoon.xyz errors, restart it, and update it if it's old
(Cloudflare changes its checks often). If it still fails, try "Response fallback" (web UI → Settings → Server →
Cloudflare bypass; not tested here).
Done on the owner's side the same evening (outside this repo): FlareSolverr now runs all the time, with a 300 s time
limit. Not checked from the Switch since.

## If it comes back
- Try the Switch first, then the web UI. If the web UI fails the same way, the cause is on the server's side. Its
  error's "Show more" names the part (`CloudflareInterceptor` = FlareSolverr).
- How the Switch was driven: a scratch Node script on `tests/e2e/drivers/SwitchDriver.ts` with the owner's config
  (`launch({ keepConfig: true })`), screenshots, and the app's `test/events.jsonl` (each request's end and error).
  Afterwards: `tools/e2e.sh switch --restore-config`, then the console to sleep. The script (`step1-launch.ts`) and the
  screenshots are in the git-ignored `tests/e2e/out/xyz-debug/`.
- Timing the web UI: wrap `window.fetch` in the page and log only each request's operation name, variables and time,
  never headers or bodies (a wider log caught the web UI's login refresh token; it wasn't used or kept).
- To check (a guess, not tested): with a 300 s FlareSolverr limit the server can take 310 s for one job, and longer
  behind a queue. The app's longest "Wait for sources" is 5 minutes (300 s). If the Switch says "took too long" while
  the web UI loads the site in the end, a longer choice would be a new issue.
