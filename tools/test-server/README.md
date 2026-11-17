# Test server (WP-02, D29)

A throwaway Suwayomi-Server for tests: Windows Java, port **4568**, filled with synthetic fixture manga from Local Source.
It never touches a real Suwayomi library: all its files live in `<repo>/.test-server/` (git-ignored). Not in `%LOCALAPPDATA%`:
inside Claude Code's sandbox, files that Windows programs write outside the project folder are invisible to WSL (D29).

```bash
tools/test-server/test-server.sh reset            # fresh data + fixture manga (stops the server first)
tools/test-server/test-server.sh start            # start, wait, set up the library. --auth none|basic|ui_login
tools/test-server/test-server.sh status           # JSON: running, auth, LAN addresses, URL from WSL
tools/test-server/test-server.sh stop
eval "$(tools/test-server/test-server.sh env)"    # SNX_SERVER_URL/SNX_AUTH/SNX_USER/SNX_PASS for the other tools
```
Run them in WSL (`wsl.exe -d Ubuntu --exec bash -lc '<repo>/tools/test-server/test-server.sh start'`).

## What's where
| Path | What |
|---|---|
| `server.json` | pinned server version, jar URL + SHA-256, port, test login (`snx` / `snx-test-only`), JWT expiry 20 s |
| `test-server.ps1` | Windows side: download the jar, start/stop Java, wipe and refill the data folder. Works on its own in PowerShell too |
| `test-server.sh` | WSL side: everything above plus fixture generation (`tools/gen-fixtures`) and library setup (`seed.ts`) |
| `seed.ts` | adds every Local Source manga to the library, loads chapters, creates the categories and assigns manga, then Epsilon Weekly. Safe to run again |
| `.test-server/jar/` | the downloaded jar (kept between resets) |
| `.test-server/fixtures/` | pristine fixture manga, re-generated only when the generator changes (`fixtures.stamp`) |
| `.test-server/fixtures-extra/` | `Epsilon Weekly` incl. the held-back Chapter 3. Kept out of `fixtures/` because `reset` copies that folder whole and the server numbers manga in folder order |
| `.test-server/root/` | the server's data folder (`rootDir`). `reset` deletes it, but only if it holds the `.snx-test-root` marker |
| `.test-server/server.out.log`, `server.err.log` | Java's output. The server's own log is in `root/logs/` |
| `.test-server/web/` | the test source's store (`store/`) and test site (`site/`), served by jwebserver on 127.0.0.1:4569 (WP-23, below) |
| `.test-server/web-site.stamp`, `web-source.stamp` | when the site generator / the Java extension last changed; `reset` rebuilds only what changed |
| `.test-server/source-build/` | javac's output and the jar staging folders (rebuilt every time) |
| `.test-server/web.out.log`, `web.err.log` | jwebserver's request log |

Java is started through WMI (`Win32_Process.Create`), not `Start-Process`: a child of the PowerShell that WSL called would inherit the
pipe WSL reads from, and WSL would then wait for Java to end. `stop` finds Java by its command line (it names our `root` folder).

## Epsilon Weekly and the new-chapter helpers (WP-15)
Epsilon Weekly is the manga for "a library update finds a new chapter". The seed adds it last (manga 157, chapters 460-461, in
Reading, both read) so no older id moves; only its chapters 1 and 2 are copied into the server. In `tests/e2e/lib/suwayomi.ts`:
- `addNewChapter()` copies the held-back Chapter 3 into `.test-server/root/local/Epsilon Weekly/` (fixed folder time = upload date).
  The server sees it at the next `updateLibrary(input: {})`, which gives it the next free chapter id (462 on a fresh server;
  more once a test browsed the test source, so tests look it up, D55).
- `removeNewChapter()` deletes that folder and runs `fetchMangaAndChapters` for manga 157, so the server forgets that chapter.
Both are safe to call when already in that state. A live check: `eval "$(tools/test-server/test-server.sh env)";
SNX_LIVE_SERVER=1 npx vitest run tools/test-server/epsilon.test.ts`.

## The test source: our extension, a store and a test site (WP-23, D55)
Besides Local Source the server has **our own extension**, so browse, extension and server-download tests have a real online
source (contents: `tools/test-source/README.md`, docs/TESTING.md "Test source"):
- `reset` writes `.test-server/web/` (Node) and builds the extension jars (Windows JDK, `test-server.ps1 build-source`), each
  only when its stamp changed, puts the store back on SNX Test v1 and deletes the switch file. `start` starts **jwebserver**
  (`jwebserver -b 127.0.0.1 -p 4569 -d .test-server/web`, from the JDK) before Java; `stop` stops both; `status` reports it
  under `web`. **Loopback only**: no firewall rule, and the Switch never talks to it (covers, pages and icons come through the
  server). From WSL it can't be reached either; tests change its files directly.
- The seed adds the store `http://127.0.0.1:4569/store/index.json` (`env` prints it as `SNX_STORE_URL`) and installs SNX Test
  v1 (sources "SNX Test (EN)" and "(JA)"). It doesn't browse, so manga 1–157 and chapters 1–461 keep their ids; anything a test
  browses gets the next free ids, which tests look up (`findSourceManga`), never assume.
- **The switch file** `.test-server/web/site/control.json` (no file = all normal) is read on every call:
  `{"fail": [{"lang": "ja", "call": "search"}], "delayMs": 0, "extraChapters": ["en-01"]}`. A failing call (lang `en`, `ja`,
  `fr` or `*`; call `popular`, `latest`, `search`, `details`, `chapters`, `pages`, `image`, `cover` or `*`) makes the server
  answer "SNX test failure: <call>"; `delayMs` (≤ 10 000) waits before every call; `extraChapters` shows a manga's held-back
  chapter (en-01 "Comet Courier" has chapter 3). The server keeps images it served once: empty its page cache
  (`clearCachedImages(input: {cachedPages: true})`) before testing an image failure.
- **Helpers** in `tests/e2e/lib/test-source.ts`: `TEST_SOURCE_IDS`, `setTestSourceControl(c)`, `clearTestSourceControl()`,
  `offerTestSourceUpdate()` / `withdrawTestSourceUpdate()` (store on v2 / back on v1; going back reinstalls v1),
  `installExtension(pkg)` / `uninstallExtension(pkg)`, `resetTestSource()` (as seeded), `findSourceManga(sourceId, key)`.
- **Live check**, on a fresh reset, in at least `none` and `ui_login`: `eval "$(tools/test-server/test-server.sh env)";
  SNX_LIVE_SERVER=1 npx vitest run tools/test-server/test-source.test.ts` (~10 s; it leaves the test source as seeded).
- The store's files get a Last-Modified date 100 years ahead on purpose: the server's HTTP client (OkHttp) would otherwise reuse
  an old copy of the index or a jar for a while without asking, and miss an update.

## Settings
Java gets every setting as `-Dsuwayomi.tachidesk.config.server.<key>=<value>` (keys from the jar's `server-reference.conf`):
listen on `0.0.0.0:4568`, no tray, no browser, no WebUI, no WebView (KCEF), no automatic library updates or backups.
Auth modes: `none` → `NONE`, `basic` → `BASIC_AUTH`, `ui_login` → `UI_LOGIN` (JWT, access token lives 20 s so refresh is tested).
Stopping is a hard stop (there's no clean way to send Ctrl+C to a hidden Java process). `reset` repairs anything.

## Library after seeding
| Category | Manga |
|---|---|
| Reading | Alpha, Beta RTL, Epsilon Weekly (manga 157, chapters 460-461 read) |
| Long series | Gamma Webtoon, Delta Big |
| (none, i.e. Default) | 日本語タイトル テスト, Broken |

## Network
- From WSL: `http://<windows-host>:4568`, where `<windows-host>` is WSL's default gateway (`test-server.sh url` prints it).
  `localhost` inside WSL is not Windows (WSL 2 on Windows 10 is behind NAT).
- From the LAN (Switch, phone): `http://<pc-ip>:4568` (`status` lists the PC's LAN addresses). Quick check in a phone browser:
  `http://<pc-ip>:4568/api/v1/settings/about` shows a line of JSON with the server version.
- Windows Firewall (SETUP.md A.4, done by the owner): an allow rule for TCP 4568 from LocalSubnet on every profile (the PC's network
  is classed "Public", and so is WSL's virtual network), and the old "OpenJDK Platform binary" Block rules for this Java switched off.
  If WSL gets timeouts (not "connection refused") after a Java update, look for a new Block rule.

## Bumping the server version
Change `version`, `jarUrl` and `jarSha256` in `server.json` (the SHA-256 is on the GitHub release page), then
`reset`, `start`, `eval "$(tools/test-server/test-server.sh env)"`, `npm run dump-schema`, `tools/record-fixtures/record-all.sh`,
and review the diff in `tests/fixtures/`. `reset` also recompiles the test extension against the new jar (the version is part
of its stamp): run the test source's live check above in two auth modes (`tools/test-source/README.md` if it doesn't compile).
