# WP-xx Title

Model: **Opus** | **Sonnet** (only if clear-path, see ROADMAP) · Size: S/M/L · Needs: WP-.. · Owner: – / what's needed

## Goal
One paragraph: what exists after this session that didn't before, and why it matters.

## Read first
Only the files this WP needs (STATUS.md is always read). Prefer module READMEs + headers over .cpp files.

## Deliverables
- [ ] Concrete, checkable items. Each new piece of logic has tests. Each new screen has a scenario.

## Acceptance
How to *prove* it's done: which test commands, which scenarios on which driver, which numbers.

## Sonnet-safe chunks (Opus WPs)
Pieces Opus may hand to a Sonnet subagent with a tight spec, then review. "None" is a fine answer.

## Escalate to Opus if (Sonnet WPs only)
- Any test fails for a reason not explained by this WP's change
- You need to change a header in `core/`, `net/`, `api/`, `image/` or `platform/`
- Threading, memory, auth tokens or on-device behaviour is involved
- You're about to guess at API/field names not in the schema
