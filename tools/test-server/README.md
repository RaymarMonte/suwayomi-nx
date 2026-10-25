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
| `seed.ts` | adds every Local Source manga to the library, loads chapters, creates 2 categories and assigns manga. Safe to run again |
| `.test-server/jar/` | the downloaded jar (kept between resets) |
| `.test-server/fixtures/` | pristine fixture manga, re-generated only when the generator changes (`fixtures.stamp`) |
| `.test-server/root/` | the server's data folder (`rootDir`). `reset` deletes it, but only if it holds the `.snx-test-root` marker |
| `.test-server/server.out.log`, `server.err.log` | Java's output. The server's own log is in `root/logs/` |

Java is started through WMI (`Win32_Process.Create`), not `Start-Process`: a child of the PowerShell that WSL called would inherit the
pipe WSL reads from, and WSL would then wait for Java to end. `stop` finds Java by its command line (it names our `root` folder).

## Settings
Java gets every setting as `-Dsuwayomi.tachidesk.config.server.<key>=<value>` (keys from the jar's `server-reference.conf`):
listen on `0.0.0.0:4568`, no tray, no browser, no WebUI, no WebView (KCEF), no automatic library updates or backups.
Auth modes: `none` → `NONE`, `basic` → `BASIC_AUTH`, `ui_login` → `UI_LOGIN` (JWT, access token lives 20 s so refresh is tested).
Stopping is a hard stop (there's no clean way to send Ctrl+C to a hidden Java process). `reset` repairs anything.

## Library after seeding
| Category | Manga |
|---|---|
| Reading | Alpha, Beta RTL |
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
and review the diff in `tests/fixtures/`.
