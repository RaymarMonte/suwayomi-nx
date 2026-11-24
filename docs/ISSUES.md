# Issues: small investigations and changes

The small sibling of the work packages (D63). A WP builds a feature; an issue answers "this doesn't work", "why does
X happen" or "change this one thing", in one session. One row below and one file each in `docs/issues/`, written from
[`issues/_TEMPLATE.md`](issues/_TEMPLATE.md).

## How it works

- **What gets one:** a bug the owner reports, an investigation (also one that ends with no code change), or a small
  change that fits in one session. Not: a question, planning, a docs chore. Claude opens one by itself when a request
  fits; the owner doesn't have to say "issue".
- **Start:** take the next number (bottom of this file), copy the template to `docs/issues/IS-xxx-<short-name>.md`,
  put the owner's words under Report, add the row here (status `open`).
- **While working:** write findings into the file as they come, so a session that stops early leaves them behind.
  Read only what the issue needs (STATUS.md, this file, the modules it touches), like a WP.
- **Too big for an issue:** more than one session, a new screen, product questions beyond one setting, or it touches
  threading, the reader, progress sync or the safety layer. Stop at a clean point, write up what was found, propose a
  WP in ROADMAP.md (owner's OK), and close the issue as `moved to WP-xx`.
- **End:** fill in Outcome and the sections; if code changed, run the test commands for the layer (CLAUDE.md); a
  CHANGELOG line if the owner would notice it; a D-entry only if the fix sets a rule; update the row; commit
  `IS-xxx: <summary>`.
- **STATUS.md:** an issue never overwrites "Last session" (that's the running WP's handoff). It keeps the
  "Open issues" line current: the ones not `done`, and what each waits on.
- **Usage limit:** issues go ahead above 92 %, like the owner's quick requests (owner, 2026-11-21).
- **While a WP session runs:** do the issue on its own branch (a worktree), merged after that WP.
- **Public repo (D23):** placeholders (`<server-lan-ip>`, `<switch-ip>`, `JohnDoe`), no tokens or passwords, no
  screenshots of the owner's library or config in the repo. Point to the git-ignored folder that has them.
- **Device and server work:** the usual rules: the owner's `config.json` back afterwards, the console asleep at the end.

Status: `open` · `waiting on owner` · `done`. Outcome: `fixed` · `not ours` · `won't fix` · `moved to WP-xx`.

## List

| ID | Title | Kind | Opened | Status | Outcome |
|---|---|---|---|---|---|
| [IS-001](issues/IS-001-weeb-central-took-too-long.md) | Weeb Central: "The server took too long to answer" on the Switch | bug fix | 2026-11-19 | done | fixed: Settings → Server → "Wait for sources" (203cfea, D60) |
| [IS-002](issues/IS-002-webtoonxyz-wont-load.md) | WebtoonXYZ won't load on the Switch | investigation | 2026-11-20 | done | not ours: the server's FlareSolverr got no answer from the site |
| [IS-003](issues/IS-003-ci-red-tsan-race-in-tests.md) | CI red: ThreadSanitizer finds a race in the reader's offline unit tests | bug fix | 2026-11-22 | done | fixed: the tests destroy the SD-card helper after the harness; the tests' wait has 60 s for the coverage build (test-only) |
| [IS-004](issues/IS-004-screen-changes-overlap.md) | Opening a manga shows two screens on top of each other for a moment | small change | 2026-11-22 | done | fixed: every screen paints its background, a clean cross-fade (e273d01, D64); the owner's console try passed |
| [IS-005](issues/IS-005-desktop-e2e-flakes.md) | Two desktop e2e checks fail now and then: Resume's 2 s, and a lost press or tap | bug fix | 2026-11-23 | done | fixed (test-only): Resume's 2 s timed on screen (D58/D62, owner); the desktop driver waits for the app to read each input; the scenarios wait for an idle after the input and its answer |
| [IS-006](issues/IS-006-manga-bar-squeezed-while-loading.md) | While a manga's chapters load, Sort / Show / Refresh are squeezed | bug fix | 2026-11-23 | done | fixed: a view that fills a row and shows text starts at width 0, on all 12 such places (37f6211, D65, with a check); the test server's cover cache now resets too; the owner's console try passed |
| [IS-008](issues/IS-008-reader-page-over-details.md) | Leaving the reader shows its page pasted over the manga's details for a moment | bug fix | 2026-11-24 | done | fixed: the reader's own drawing (page, strip, spinner) takes the screen's fade (34a95d4, D66, with a check); the owner's console try passed |

Next number: **IS-009** (IS-007 is taken on the `claude/wizardly-engelbart-c19dea` branch).
