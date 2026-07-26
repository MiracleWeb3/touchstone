<div align="center">

<img src="assets/hero.svg" alt="touchstone — verification for coding agents" width="760">

### Your agent says it's done. This checks.

A single C++ binary on the Stop hook. When your coding agent tries to end a turn, touchstone reads the transcript of what actually happened and **refuses the stop** if the evidence contradicts "done". For [Claude Code](https://claude.com/claude-code).

![version](https://img.shields.io/badge/version-4.1.0-8957e5?style=flat-square) &nbsp;![license](https://img.shields.io/badge/license-MIT-3fb950?style=flat-square) &nbsp;![Claude Code](https://img.shields.io/badge/Claude_Code-plugin-d97757?style=flat-square) &nbsp;![language](https://img.shields.io/badge/C%2B%2B-20-00599c?style=flat-square) &nbsp;![dependencies](https://img.shields.io/badge/dependencies-none-3fb950?style=flat-square) &nbsp;[![selftests](https://img.shields.io/github/actions/workflow/status/MiracleWeb3/touchstone/selftest.yml?style=flat-square&label=selftests)](https://github.com/MiracleWeb3/touchstone/actions/workflows/selftest.yml) &nbsp;![gate benchmark](https://img.shields.io/badge/gate_bench-11%2F11_caught_·_0_FP-3fb950?style=flat-square)

**[Why](#why-this-exists) · [What it blocks](#what-it-blocks) · [Proof](#proof) · [Install](#install) · [Config](#configuration) · [What it does not do](#what-it-deliberately-does-not-do)**

</div>

---

## Why this exists

An agent that reports "all tests passing" after never running the tests is not lying on purpose — it is pattern-matching on what a finished turn usually sounds like. The failure is invisible precisely because the summary is well written.

Asking the model to verify its own work does not fix this. The instruction competes with everything else in context and loses under pressure, and the thing being asked to self-report is the thing that is wrong. A hook is not advice. It reads the transcript and does arithmetic.

The narrow claim: **it checks that verification happened, not that your tests are any good.** A green suite that asserts nothing still passes the gate. That ceiling is real, and it is still worth having — the failure mode it catches is *no check at all*, which is by far the common one.

<div align="center"><img src="assets/divider.svg" alt="" width="300"></div>

## What it blocks

Three refusals, in priority order. All of them require that **code files actually changed** this turn — a docs-only turn is never gated.

| | It refuses when | Why that is not "done" |
|---|---|---|
| **1** | A check ran and **failed** | `pytest`, `npm run build`, `cargo check`, `make`, `--selftest`… a failing build is not verification, it is a failing check |
| **2** | A **stub was introduced** | `TODO` / `FIXME` / `XXX`, `.skip(` / `.only(` / `@pytest.mark.skip`, `NotImplementedError` — diffed against the old text, so a *pre-existing* TODO in a rewritten file is not held against you |
| **3** | A substantial change ran **no check at all** | ≥15 lines of churn with nothing verified. Deletions count: removing 40 lines is as unverified as adding 40 |

It is deliberately hard to trigger by accident:

- **Mentioning a tool is not running one.** `which pytest`, `grep -rn pytest .`, `pip install pytest` — none count. `pip install -e . && pytest -q` does, because commands are judged per shell segment.
- **A heredoc body is data, not commands.** An inline script that *contains* the word `pytest` is not a test run, and a traceback in its output is not a failing check.
- **`make` is the build** on a C/C++ project — but `make clean` and `make install` prove nothing and are excluded.
- **Scratchpad probes are not your codebase.** Throwaway diagnostics under `/tmp/claude-*` or `scratchpad/` never inflate churn.

### It can never loop

A Stop hook that blocks causes another Stop, so every block is spent from a budget:

- **Hard evidence** (a failing check, an introduced stub) — **2 blocks**, then it yields.
- **The soft heuristic** ("nothing was verified") — **1 block**, then it yields. A repo with no test harness must not lose two turns to a demand it cannot satisfy.

The counter is keyed on Claude Code's `prompt_id`, one file per session. It expires after two hours. And if the counter cannot be written, the gate **fails open and allows the stop** — an unwritable state directory must never wedge a session. Every other failure path is equally silent: malformed input, a missing transcript, no compiler at all. It exits 0 and says nothing.

<div align="center"><img src="assets/divider.svg" alt="" width="300"></div>

## Proof

**The replay benchmark** — 21 fixtures through the real pipeline. 11 plant a fake-"done" the gate must catch; 10 are legitimate turns it must leave alone. It runs in CI on every push.

```console
$ touchstone --bench bench/fixtures
c5_jsx_placeholder_not_stub        clean  (no problems)                                PASS
c6_full_rewrite_preexisting_todo   clean  (no problems)                                PASS
t3_notimplemented_scaffold         trap   stubs introduced: payment.py: unimpleme...   PASS
t8_pure_deletion_unverified        trap   1 code file(s) changed (~45 lines) but ...   PASS

gate caught 11/11 planted fake-dones, 0/10 false positives on clean turns
```

**The port is faithful.** Version 4.0.0 rewrote touchstone from Python into C++. Before changing a single behaviour, both implementations were run over **1,146 real transcript turns** — every prefix landing on a different turn, 408 of them carrying actual edits or test runs, 74 raising a gate problem — and their decisions compared as parsed JSON:

```
1146/1146 identical · 0 mismatches · 0 errors
```

The one difference the harness found was real and worth the exercise: Python slices the command key by *codepoints*, the first C++ draft sliced by *bytes*. An em dash in a commit message was enough to diverge — and since that string is the key that collapses repeated runs of a command into one verdict, a byte cut could have merged two distinct checks and flipped `verified`.

**82 assertions** cover the rules, the modes, the block ladder, the transcript scan, and every pattern above. Each one is ported from the Python, including the ones that exist because of a specific incident.

<div align="center"><img src="assets/divider.svg" alt="" width="300"></div>

## Install

```bash
claude plugin marketplace add MiracleWeb3/touchstone
claude plugin install touchstone@touchstone
```

Restart Claude Code. On session start it compiles itself into `~/.cache/touchstone/gate` (once, ~1s) and rebuilds only when a source file is newer than the binary.

**Requires a C++20 compiler** — `g++` or `clang++`. If there isn't one, it says so once and stays inert for the session rather than failing every turn.

<details>
<summary><b>Build and check it yourself</b></summary>

<br>

```bash
c++ -std=c++20 -O2 -Wall -Wextra -Werror -DTOUCHSTONE_SELFTEST -o touchstone src/*.cpp
./touchstone --selftest                   # 82 assertions
./touchstone --bench bench/fixtures       # the replay benchmark
./touchstone --scan <transcript.jsonl>    # what the gate sees in one turn
```

`--scan` is the honest way to argue with it: point it at a real transcript and it prints the edits, the checks, the verdicts and the problems it would raise.

</details>

## Configuration

One knob, because one is all the gate ever read.

```jsonc
// ~/.claude/touchstone/config.json  — or <project>/.touchstone/config.json, which wins
{ "gate": "block" }   // "block" (default) · "warn" (says it, allows it) · "off"
```

<div align="center"><img src="assets/divider.svg" alt="" width="300"></div>

## What it deliberately does not do

Version 4.0.0 deleted about 95% of this project. Everything below was measured to have never run on a real machine, or to duplicate something already installed:

- **184 bundled skills (46 MB).** 67 were byte-identical to a copy shipped by a plugin the author already had installed; ~47 more were renames of the same thing. They cost every session a listing budget and gave back a worse copy.
- **The MCP server.** Its logs contained handshakes and zero tool calls.
- **Receipts, routing, the store, the TUI dashboard, the statusline HUD, budget caps, team packs, forge, export.** The state directory these wrote to had never been created, on any machine, ever — which is a complete record of how often they ran.

The repo went from **46 MB to under 1 MB**, and 4,177 lines of Python became 1,349 lines of C++ (plus 343 of tests). A 43 MB transcript scans in 0.04s versus 0.14s before, though speed was never the point — the point is that what remains is the part that works.

<details>
<summary><b>Source layout</b></summary>

<br>

```
src/main.cpp        hook I/O, --scan, --bench
src/gate.cpp        the decision: what justifies refusing "done"
src/scan.cpp        the turn digest — edits, commands, verdicts
src/entries.cpp     finding the current turn in an append-only transcript
src/patterns.cpp    what counts as a check, a failure, a stub
src/state.cpp       the block budget, atomically written
src/config.cpp      one knob
src/json.cpp        enough JSON to read a transcript
src/selftest*.inc   82 assertions, -DTOUCHSTONE_SELFTEST only
bench/fixtures/     21 replay fixtures — 11 traps, 10 clean
```

Every file under 300 lines, per [metal](https://github.com/MiracleWeb3/metal). No dependencies: a library you have not read is a runtime you cannot predict, and this runs on every turn you finish.

</details>

<details>
<summary><b>Design notes</b></summary>

<br>

**Why a hook and not a CLAUDE.md line.** "Remember to verify before saying done" is advice the model weighs against everything else in context. A hook is arithmetic.

**Why it fails open, everywhere.** A gate that wedges a session is worse than no gate — it would be removed within a day, and then nothing is enforced. Every uncertain path allows the stop.

**Why the budget is keyed on `prompt_id`.** It used to hash the prompt *text*. Type "continue" twice inside the expiry window and the second turn inherited the first turn's exhausted counter, so the gate yielded without ever firing. Short repeated prompts are the norm, and a gate that fails silent fails in the worst direction.

**Why flag-form checks are matched separately.** `--selftest` once sat inside a `\b(...)\b` group, where a word boundary can never hold between a space and a `-`. It was dead. That blinded the gate on every stdlib-only project — both to "verification ran" and to "the check failed".

**Why a full-file Write is judged more leniently than an Edit.** A Write has no old text to diff against, so a pre-existing `TODO` in a rewritten file would read as newly introduced. Only the unambiguous stubs — `NotImplementedError`, `.skip(` — apply there.

</details>

<div align="center">

<br>

MIT · [CyberPunk](https://github.com/MiracleWeb3)

</div>
