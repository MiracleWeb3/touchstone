# Security

touchstone runs on every attempt to end a turn, so it deserves scrutiny. The whole surface is ~1.3k lines of dependency-free C++20 you can audit in one sitting: [`src/`](src/).

**Trust model, in short:**

- **No network, ever.** There is no update check, no telemetry, no MCP server, no outbound call of any kind.
- **It never executes anything.** The gate reads the transcript and matches patterns. It does not run your tests, your build, or any command it finds — it only observes what already ran.
- **It only ever reads two things:** the transcript path handed to it by Claude Code, and its own config. It writes exactly one kind of file: a small block counter under `~/.claude/touchstone/gate/`.
- **No third-party code.** The JSON reader is 146 lines in this repo. A library you have not read is a runtime you cannot predict, and this one sits on every turn you finish.
- **Fail-open by construction.** Malformed input, an unreadable transcript, an unwritable state directory, a missing compiler — every uncertain path exits 0 and allows the stop. A gate that wedges a session is worse than no gate.

**Reporting:** open a [GitHub security advisory](https://github.com/MiracleWeb3/touchstone/security/advisories/new) or a plain issue if it's not sensitive. Include the output of `touchstone --scan <transcript>` when relevant. Gate bypasses and any path that could make the hook hang or crash a session get priority over everything else.
