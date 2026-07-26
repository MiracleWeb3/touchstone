# Changelog

All notable changes. Versions follow semver; the manifest (`.claude-plugin/plugin.json`) is the source of truth.

## 4.1.0 — 2026-07-26

Renamed: **pantheon → touchstone**.

- **Why.** 4.0.0 deleted everything that was not the verification gate, and "pantheon" — a collection of many gods — became a name for a tool that does exactly one thing. A touchstone is the black stone you rub gold against: the streak it leaves tells you whether the metal is genuine, and says nothing about what it is worth. That is precisely this tool's claim and its documented ceiling — it checks that verification *happened*, not that your tests are any good.
- **Breaking, in the way a package rename always is:** the plugin is now `touchstone@touchstone`, the binary caches to `~/.cache/touchstone/gate`, and state lives in `~/.claude/touchstone/gate/`. Project config moves from `.pantheon/config.json` to `.touchstone/config.json`, global from `~/.claude/pantheon/config.json` to `~/.claude/touchstone/config.json`. No migration path is provided and none is needed: 4.0.0 shipped hours earlier with zero installs. Versioned 4.1.0 rather than 5.0.0 for that reason — a major bump for a rename nobody had installed would be theatre.
- **Internals renamed too**, so nothing carries the old name: C++ namespace `pan` → `stone`, selftest macro `PANTHEON_SELFTEST` → `TOUCHSTONE_SELFTEST`.
- **New identity.** The Greek temple and meander borders were pantheon's iconography and meant nothing here; the mark is now a basanite slab with gold streaks rubbed across it. Wordmark resized to fit ten letters where eight used to sit.
- **No behaviour change.** Same 82 selftest assertions, same replay benchmark at 11/11 traps caught and 0/10 false positives, same gate logic. The GitHub repository redirects from the old path.
- Entries below deliberately keep the name **pantheon** — that is what the project was called when they happened.

## 4.0.0 — 2026-07-26

Rewritten in C++. Everything that was not the verification gate is gone.

- **The gate, ported to C++20.** No Python, no interpreter on the Stop path, no dependencies. It compiles itself once on SessionStart into `~/.cache/pantheon/gate` and rebuilds only when a source file is newer than the binary.
- **Proven faithful before anything changed.** Both implementations were run over **1,146 real transcript turns** (408 carrying edits or test runs, 74 raising a gate problem) and their decisions compared as parsed JSON: **1146/1146 identical, 0 mismatches**. The replay benchmark is unchanged at 11/11 traps caught, 0/10 false positives.
- **One bug the differential caught.** Python slices the per-command key by codepoints; the first C++ draft sliced by bytes. An em dash in a commit message diverged the two. Because that string is the key that collapses repeated runs of a command into one verdict, a byte cut could have merged two distinct checks and flipped `verified`.
- **Removed: 184 bundled skills (46 MB).** 67 were byte-identical to a copy shipped by an already-installed plugin; ~47 more were renames of the same tools. They spent a session's skill-listing budget to provide a worse copy of something already present. `LICENSES/` and `CREDITS.md` go with them — the vendored material they covered is no longer distributed.
- **Removed: the MCP server.** Its logs held connection handshakes and zero tool calls.
- **Removed: receipts, adaptive routing, the store, the TUI dashboard, the statusline HUD, budget caps, team packs, forge, export, doctor, the bundled agents, and the `UserPromptSubmit` + `SessionStart` behaviour hooks.** The state directory all of it wrote to had never been created on any machine — a complete record of how often it ran.
- **Config reduced to one key**, `gate` (`block` / `warn` / `off`), still layered project-over-global. The legacy `preset` values still resolve.
- **Breaking.** The `pantheon` CLI, its MCP tools, and every bundled skill are gone. Pre-4.0 config files keep their unread keys; nothing is migrated because nothing was ever stored.

4,177 lines of Python became 1,349 lines of C++ plus 343 of tests. The repo went from 46 MB to under 1 MB, and a 43 MB transcript now scans in 0.04s rather than 0.14s.

## 3.0.3 — 2026-07-20

Load fix: the manifest declared the hooks file Claude Code already loads.

- **The bug.** `.claude-plugin/plugin.json` carried `"hooks": "./hooks/hooks.json"`. Claude Code loads `hooks/hooks.json` automatically, so the manifest registered it a second time and the loader refused the whole file: *"Duplicate hooks file detected ... manifest.hooks should only reference additional hook files."* Every hook pantheon ships was dead on arrival, the verification gate among them — and because the plugin failed to load, `/plugin update` fetched 3.0.2 into the cache but could not activate it.
- **The fix.** Drop the key. `manifest.hooks` is for *additional* hook files only; the standard path needs no declaration.

## 3.0.2 — 2026-07-20

Gate fix: a bare `make` and `./test.sh` never counted as verification.

- **The bug.** The runner list knew pytest, jest, cargo and npm, but nothing about how a C or shell project is actually checked. A clean `make` build and a `bash test.sh` suite both read as "nothing ran", so the gate blocked turns whose work *had* been verified and passed. Same failure as 3.0.1 one layer up: the check runs, the reader cannot see it.
- **What counts now.** `make` as the build — with `clean`/`install`/`uninstall`/`distclean` excluded, since teardown proves nothing — plus `cc`, `clang`, `cmake`, `ninja`, `bash -n`, `cargo clippy`, `bun test`, `deno test`, and test scripts in both spellings: `bash tests/run-checks.sh` and `./test.sh`.
- **Where the path form had to go.** `./test.sh` starts with a `.`, so inside the `\b(...)\b` group it would have been dead on arrival for precisely the reason `--selftest` was in 3.0.1. It lives in the no-leading-`\b` branch instead.
- **Scratchpad probes are not churn.** `is_code_file()` now excludes `/scratchpad/` and `/tmp/claude-`. Throwaway diagnostics written while investigating were counted as changed code, so a turn that only *looked* at something could cross the 15-line threshold and trip the gate as if it had shipped.
- Selftest covers both directions, including the negatives that keep it honest: `make clean`, `make install`, `cat test.sh` and `ls scripts/test.sh` must still not register. Benchmark unchanged: **11/11 caught, 0/10 false positives**.

## 3.0.1 — 2026-07-19

Gate fix: the `--selftest` pattern had never worked.

- **The bug.** `TEST_RE` listed `--selftest` inside a `\b(...)\b` group. A word boundary can never hold between a space and a leading `-`, so `python3 x.py --selftest` never matched. Measured against a real session transcript: 54 Bash commands in the turn, **0** recognised as checks.
- **Impact, both directions.** Projects tested via `--selftest` never registered verification, so the gate nagged for work that HAD been verified — and, worse, a *failing* selftest was invisible too, so the gate could not catch its headline case on any stdlib-only project. Pantheon's own suite is `--selftest` throughout: the tool was blind to exactly the project it ships in.
- **Why CI never caught it.** The `c8_selftest_passing` fixture used `python3 tokenizer.py --selftest`, which also didn't match — it passed as "clean" only because its edit was under the 15-line churn threshold, not because the selftest was recognised. It has been rewritten with a 30-line edit so recognition is the only thing keeping it clean, and a new `t11_failing_selftest` trap covers the other direction. Both fail against the old pattern; both pass against the fix.
- **Heredoc bodies are no longer judged.** With flag matching working, `python3 - <<'PY' … --selftest … PY` started counting as a check, and a traceback in such a script's output as a *failing* check. A heredoc body is data, not commands — segmentation now cuts at the heredoc marker.
- Benchmark: 21 fixtures, **11/11 caught, 0/10 false positives**.

## 3.0.0 — 2026-07-19

The subtraction release: pantheon stops doing memory.

- **Breaking — all memory features removed.** Gone: the `lessons` table and BM25 recall, the Stop-hook learning capture and its inbox, per-prompt recall injection, the `recall` config knob, the `pantheon lesson` and `pantheon recall` CLI verbs, the `pantheon_recall` / `pantheon_lesson_add` MCP tools, and the `mnemosyne` / `alexandria` / `anamnesis` / `stele` disciplines. 188 skills → 184.
- **Why** — [claude-memory-light](https://github.com/MiracleWeb3/claude-memory-light) indexes every Claude Code transcript verbatim into SQLite FTS5 for zero tokens, and answers "what did we decide about X" better than a curated lesson table ever did. Running both meant two half-memories competing, one of them a lossy summary of what the other already had complete. A plugin should not ship a worse copy of a job something else does properly.
- **Kept and sharpened** — the verification gate, the adaptive router, receipts, the dashboard, doctor, budget caps, forge, team packs (config + standards, no lessons now).
- **Upgrade is lossless.** Databases created before 3.0 keep their `lessons` table as an orphan: never read, never pruned, never dropped. Nothing captured is destroyed; `sqlite3 ~/.claude/pantheon/pantheon.db 'SELECT text FROM lessons'` still reads it.
- **Gate fix (also in 2.0.1)** — the block budget was keyed on a hash of the prompt text, so typing "continue" twice inside 2h let the second turn inherit the first's exhausted counter and the gate silently never fired. Now keyed on the payload's `prompt_id`, with `stop_hook_active` as a floor.

## 2.0.0 — 2026-07-18

The naming release: every discipline carries its own name now, so nothing routes on ordinary English.

- **Breaking — 21 disciplines renamed.** `dashboard`→`clio`, `doctor`→`asclepius`, `forge`→`hephaestus`, `forge-session`→`hephaestus-session`, `oracle`→`sibyl`, `ask`→`socrates`, `brag`→`kleos`, `budge`→`metron`, `cancel`→`atropos`, `debug`→`theseus`, `learner`→`mathesis`, `plan`→`boule`, `prototype`→`pygmalion`, `release`→`hermes`, `remember`→`anamnesis`, `skill`→`techne`, `team`→`argonauts`, `trace`→`ichnos`, `triage`→`krisis`, `verify`→`basanos`, `wiki`→`stele`.
- **Why** — a discipline named after a common word gets matched on that word. "my postgres oracle migration is failing" summoned the read-the-docs discipline; "the forge is hot" summoned the skill author; a bug report that happened to say "dashboard chat" put the telemetry ledger on top of the debugging disciplines. Once junk lands on top you stop reading the suggestions at all, which is the exact failure the routing exists to prevent.
- **Technology names kept** — `vitest`, `shadcn`, `threejs-*`, `vite`, `pnpm`. The name is the search term; nothing is improved by being called something in Greek.
- **No discoverability cost** — routing reads descriptions as well as names, so every renamed discipline still comes back from a plain sentence about what it does. Checked one by one: "cancel the running mode" finds `atropos`, "guide me through cutting a release" finds `hermes`, "verify this really works before i claim its done" finds `basanos`.

**Migration:** replace `/pantheon:<old>` with `/pantheon:<new>` from the list above. Nothing else changed.

## 1.4.0 — 2026-07-10

The hardening release: a four-agent adversarial audit (core code, competitive gaps, skills integration, claim-vs-reality), every critical/high finding closed.

- **Security** — team-pack lessons are clamped (weight 0.5–2.0, text ≤300 chars), scoped to the repo that shipped them, and never recalled outside it; pack "standards" inject as repo conventions that never override the user; `pack init` excludes auto-captured lessons (secret-safety) unless `--include-captured`.
- **Gate** — fails OPEN if its block counter can't persist (an unwritable state dir can no longer wedge a session); missing-verification nudges block once, hard failures (failing checks, stubs) still twice; large deletions now count as unverified churn.
- **Memory** — auto-lessons require a STRONG correction signal (a stray "no"/"not"/"again" stays in the inbox); auto-lessons need ≥2 shared keywords to resurface; project inboxes are bounded (256KB → 64KB tail) and auto-gitignored.
- **Durability** — every JSON state write is atomic (tmp+rename); the spend ledger survives concurrent sessions (merge-on-write baselines, atomic prune, timestamped session entries that age out); SQLite `busy_timeout` 3s and a schema fast-path (no write transaction per prompt).
- **Adaptive routing actually resolves** — outcomes are read from the store per session; a second routed prompt no longer clobbers the first's pending resolution (superseded fires count as `ignored`), so route demotion can finally trigger.
- **Router** — `the design` / `too much` overfires removed; custom-route regexes length-capped and matched against a bounded haystack (catastrophic backtracking can't eat the hook budget).
- **Store** — monthly retention sweep (routes/metrics 90d, receipts 180d, never-recalled auto-lessons 90d) wired into SessionStart and `doctor --fix`.
- **Doctor** — transcript-format drift tripwire (a Claude Code format change can't silently blind the gate/receipts/meters); reports store size, prune age, and the measured per-session token cost of skill listings.
- **HUD** — transcript discovery walk memoized (120s TTL); all cache writes atomic.
- **CI** — ubuntu/macos/windows × Python 3.9/3.13 matrix.
- **Docs** — version coherence (badge == manifest == changelog), license claims now match CREDITS reality, renamed-skills claim scoped to what is actually renamed.

## 1.3.0 — 2026-07-10

- HUD subscription meters: exact 5-hour-window and weekly used-% from Claude Code ≥2.1 `rate_limits`; transcript-derived `≈` fallback (self-calibrating) on older versions; `⌁api` tag for API-key sessions.

## 1.2.1 — 2026-07-10

- Gate hardening: per-session block counters (no cross-session clobber), stale-counter expiry, machine-generated "user" entries excluded from turn scans, a failing build counts as a failing check.
- Receipts: token counts deduped by message id (no more 2–5× inflation); one bad ledger line can't blind the budget.

## 1.2.0 — 2026-07-10

- Pantheon-native names for the flagship merged sets — `spartan` (lazy-dev), `sisyphus`/`automedon`/`hekaton`/`pythia` (engine power modes) — with the full old→new map in CREDITS.md.

## 1.1.0 — 2026-07-10

- Team packs (committed config + lessons), `forge` (author custom disciplines), cross-agent export (cursor / codex / generic).

## 1.0.0 — 2026-07-09

- Budget guardrails (session/daily/weekly USD caps; warn/ask/block) and `pantheon doctor`.

## 0.9.0 — 2026-07-09

- Adaptive routing (ignored routes demote themselves), intent clarifier, context-wall guard.

## 0.8.0 — 2026-07-09

- The moat four: SQLite store, self-recalling memory, receipts + TUI dashboard, and a verification gate that actually blocks.

## 0.7.0 and earlier — 2026-07-09

- 0.7: the HUD (effort, session time, live context %, hourly/weekly spend). 0.6: the merge — superpowers, oh-my-claudecode, ponytail, ui-skills, attributed. 0.5: presets + three disciplines. 0.4: auto-routing + self-announce. 0.2–0.3: the mythic lifecycle. 0.1: born as "modus".
