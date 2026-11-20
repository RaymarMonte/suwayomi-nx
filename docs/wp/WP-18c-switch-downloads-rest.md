# WP-18c Offline 1b: the rest of WP-18 ("Same server?", the e2e steps it left, the token expiry)

Model: **Opus** (files on the SD card, the downloader, the console) · Size: **S/M** · Needs: WP-18 (done 2026-11-20) ·
Owner: answers given 2026-11-20 (below); the Switch runs need the console

## Goal
After this WP a second address of the same server (a LAN address and a Tailscale name, D18) **reuses its downloads**: saving
a new address on the setup screen asks once "the same server?", and Yes files the new address in that server's
`server.json`. Every e2e step D59 planned and WP-18 left runs: the queue slowed by the test source (Move to top / bottom,
Remove), a failed page then Retry, quitting in the middle of a download, the webtoon strip from its files, reading while the
server is stopped, the SD card nearly full (`sdFreeBytes`), 100 queued entries with the memory rounds, and the keep-awake
event. The integration test downloads a chapter across the ui_login token's expiry. And the D-pad press the console
sometimes loses toward a row below the visible part of a list gets a look.

## Owner's answers (2026-11-20)
1. **The question's words** (the planner's): "Is <new> the same server as <old>? Its 12 downloaded chapters stay with <old>
   otherwise." Buttons **Same server** / **Another server**.
2. **Remember "Another server"**: the new address gets its own downloads folder (only its `server.json`) at once, so the
   question comes once per address, whichever way it is answered.
3. **Only on a change**: the first setup (no saved address) never asks, even when the SD card holds downloads of an earlier
   setup; those folders stay and show again when their address comes back.

## Planner's defaults (record them in D59's "as built")
**When it asks** (`SetupActivity::finish()`, the one save path of the first run and of Settings -> Server address): the saved
address (config.json's, before this save) is not empty, the new one differs from it (`downloadServerUrl` form), the saved
server's folder has at least one downloaded chapter (`SwitchDownloads`, which still holds the saved server: `setServer()`
runs only after the save), the new address isn't in that folder's `server.json`, and no other folder's `server.json` lists
it. A queue alone (nothing downloaded) doesn't ask. Never for the test config's server (it isn't saved through this screen).
- **Same server**: the new address is added to the saved server's `server.json` (`SwitchDownloads::joinAddress`, the store's
  thread, before the scan `setServer()` starts, so the scan finds the folder). At most `kSwitchUrlsMax` (16) addresses: a
  17th drops the oldest one that isn't the saved address.
- **Another server**: `downloads/<key of new>/server.json` with the new address only (`SwitchDownloads::keepApart`), unless a
  folder of that name is already there.
- The dialog can't be closed with B (two plain choices). The address is saved after the answer, as before. A write that fails
  is logged, shown as "Can't write to the SD card." and the save goes on (the downloads then stay with the old address).
- Test event `same_server` (`old`, `new`, `chapters`, `same`); the dialog writes its usual `dialog` event.
- The pure part: `core::addServerUrl(urls, url)` (no repeats, the cap). Unit tests for it and for the model (ask / no ask
  cases, join, keep apart, the cap, `setServer(new)` after a join loads the same folder).

**The keep-awake event**: `keep_awake` (`on`, `by`: `switch_downloads`), written by the tab when it turns the keep-awake on
or off (a field on `switch_downloads` wouldn't do: that event follows the model, and the keep-awake follows the tab too).

**The e2e steps** (desktop + Switch unless marked). The server's page cache (`clearCachedImages(input: {cachedPages: true})`,
SUWAYOMI-API.md) is emptied before every step that needs slow or failing pages, so no step depends on a freshly reset server
(also in `server-downloads`' known state: it no longer needs a reset after the integration test). Steps that need the queue to
stand still use the space limit (it waits `limit_full` or `sd_full`), not timing.
- A new scenario **`switch-download-queue`** (the queue's life; `switch-downloads` stays about reading downloads):
  1. Paper Garden (SNX Test, Browse -> Latest, oldest first), `delayMs` 4000: MINUS -> Download to Switch... -> Next 5 -> the
     tab: rows 1-5 in order, Running, Chapter 1 downloading, `keep_awake` on. Once Chapter 1 has a page on the SD card:
     Remove it -> its folder gone, Chapter 2 downloads. Chapter 2 (downloading) to the bottom -> still downloading there;
     Chapter 4 to the top. Pause -> `keep_awake` off; Resume -> on; the Library tab -> off; the tab -> on. Clear... -> empty.
  2. A failed page: `image` fails -> Chapter 6 -> "Failed at page 1 of 3" (golden `switch-failed`); the failure off -> Retry
     -> On Switch.
  3. Quit in the middle: Chapter 7, `delayMs` 3000, quit once a page is on the SD card; the delay off; relaunch -> the queue
     comes back and Chapter 7 finishes; the `net` events after the relaunch ask none of the kept pages again.
  4. "Same server?" (desktop only: the console reaches the test server by one address): Settings -> Server address -> the
     other spelling of the test server's address (`127.0.0.1` / `localhost`) -> Continue -> the question -> Same server ->
     the tab still shows Paper Garden; `server.json` lists both addresses.
  5. Relaunch with `sdFreeBytes` just over 1 GiB: Alpha's Chapter 1 -> "Paused: the SD card is nearly full" (`sd_full`);
     then Delta Big's All (300 chapters): the tab shows them; Switch only: 10 rounds tab <-> Library, the memory flat
     (+< 16 MiB); Clear.
- **`switch-downloads`** gains: Gamma Webtoon's Chapter 1 (Long series, WebP strips) downloaded -> opens as the strip from its
  files (no page request); the server stopped -> Alpha's downloaded Chapter 1 opens and turns 3 pages, then the server starts
  again.

**The integration test** (ui_login only): a fresh login, a source manga no other test reads (Glass Harbor, SNX Test en-09;
the page cache emptied first), `delayMs` so the download runs past the 20 s token: the chapter is On Switch, its files equal
the server's pages, the login was renewed (a refresh), never a new password login.

**The lost D-pad press**: find why a press toward a row below the visible part of a list sometimes does nothing on the
console (borealis's focus and scroll code). Fix it if the fix is small and safe, else write down what was found (STATUS, a
ROADMAP note) and keep the e2e's press-again.

## Read first
STATUS.md; this doc; D59 in DECISIONS.md; `docs/wp/WP-18-download-to-switch.md` ("as built" notes only);
`src/ui/downloads/SwitchDownloads.hpp`, `DownloadStore.hpp`, `SwitchDownloadsTab.cpp` (keep-awake);
`src/core/SwitchDownloads.hpp`; `src/ui/setup/SetupActivity.*`; `tests/e2e/scenarios/switch-downloads.test.ts`,
`server-downloads.test.ts` (its helpers), `tests/e2e/lib/switch-downloads.ts`, `test-source.ts`, `screens.ts`;
`tests/integration/test_suwayomi_client.cpp` (`SwitchRig`, the token-expiry test).

## Deliverables
- [x] core: `addServerUrl`; tests in `test_core_switch_downloads.cpp`.
- [x] ui model + store: `SwitchDownloads::addressQuestion` / `joinAddress` / `keepApart` (+ the store's lookup of the folder
      that lists an address); tests in `test_ui_switch_downloads.cpp`.
- [x] ui view: the question in `SetupActivity::finish()` (+ `resources/i18n/en-US/setup.json`), the `same_server` event; the
      tab's `keep_awake` event.
- [x] Integration: the ui_login token-expiry download.
- [x] e2e: `switch-download-queue` (new), `switch-downloads` (strip, server stopped), the page-cache helper (also in
      `server-downloads`' known state); goldens made on desktop.
- [x] The lost D-pad press: looked at, fixed or written down.
- [x] Docs: D59 "as built" (WP-18c), TESTING.md (scenario, events), CHANGELOG.md, ROADMAP row, STATUS.

## Acceptance
- `tools/build.sh tests` (unit + contract + the write gate), `tools/build.sh fuzz` (seeds), `npm test` + `npm run typecheck`,
  `tools/build.sh integration` (3 modes), all desktop e2e green, `tools/build.sh switch` + gate PASS.
- Switch (application mode, handheld): `switch-download-queue` and `switch-downloads` green (goldens made on the console and
  checked by a pixel diff), and `server-downloads` still green (its known state changed). The memory rounds: + < 16 MiB.

## As built (2026-11-20)
Built as planned above, with these differences (also in D59's "as built", WP-18c):
- `core::addServerUrl(urls, url, keep)`: the address the cap never drops is passed in (the saved one).
- The e2e steps use other chapters and numbers than the plan: `delayMs` 6000; the failed page and Retry on Chapter 1, the
  quit on Chapter 2 (the queue's own steps leave 1 and 2 free); "Same server?" uses the test server's LAN address (WSL
  reaches it too) because `127.0.0.1` / `localhost` aren't the desktop app's address of it; the 100-entry step queues Delta
  Big's All (300) behind Alpha's waiting chapter (301 rows). The setup screen opened from Settings has a Cancel button, so
  its Continue sits elsewhere (`SETUP.fromSettings` in `screens.ts`).
- "Failed at page N" names the first page that failed: two pages download at once, and the e2e saw page 2 instead of
  page 1 when page 2 ran out of tries 1 ms earlier.
- Test builds: a text field (`TextFieldCell`) takes the test `text` command like the other text dialogs; the idle rule no
  longer waits for background requests (`HttpClient::inFlightForeground()`), or a slowed download would hold every step.
- The lost D-pad press: borealis's default list scrolling (NATURAL) only scrolls while the button is held, ~1000 px/s, and
  moves the focus once the next row is fully on screen, so a short press can end first. Settings' list now scrolls
  centered (`scrollingBehavior="centered"` in `settings.xml`): the focus moves at once. The e2e press-again stays.

## Sonnet-safe chunks (Opus WPs)
None: the pieces are small and touch the downloader, the console runs or each other.

## Not in this WP
Starting without the server and offline progress (WP-18b); merging two server folders that both have downloads; asking about
leftover folders on the first setup (owner: no); download actions on Updates rows; applet mode (WP-21).
