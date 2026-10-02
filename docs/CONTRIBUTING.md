# Contributing to TrussC

How changes to TrussC itself are proposed, decided and merged. The rules behind the decisions are in [ARCHITECTURE.md, "How design decisions are made"](ARCHITECTURE.md#how-design-decisions-are-made).

## Every change starts as an issue

Every change goes through a GitHub issue. The one exception is a docs-only fix, which can go straight to a pull request.

1. **File an issue.** Search for a duplicate first. Say what happens, where in the code, and how to reproduce it.
2. **Decide.** The approach is discussed on the issue, and the outcome is recorded as a comment that starts with `Decision (owner, YYYY-MM-DD)`. **The last Decision comment on an issue is the spec.**
3. **Mark it ready.** A decided issue gets the `ready` label. Implementation starts only on `ready` issues.
4. **Implement and open a pull request.** Reference the issue (`Closes #123`) and implement what the Decision says. If something comes up that the Decision doesn't cover, stop and ask on the issue. The issue goes back to `needs-decision` until the question is answered, and the answer is recorded there as a comment.
5. **Review.** Every pull request is reviewed against the Decision before it is merged. A pull request that needs a check on real hardware carries `needs-manual-check` until that check is done.

### Review findings

- A problem the pull request introduced, or a mistake in code or text it wrote, is fixed in the same pull request.
- A problem found nearby that was already on `main` becomes a new issue. It doesn't block the pull request.

## Labels

| Label | Meaning |
|---|---|
| `ready` | The spec is decided; implementation may start. |
| `needs-decision` | The approach isn't decided yet. Don't implement it or open a pull request for it. |
| `in-progress` | Someone is implementing it (so two people don't). |
| `needs-manual-check` | On a pull request: it needs a check on real hardware before it is merged. |

- `ready` and `needs-decision` are never on the same issue.
- An issue without a label hasn't been prioritized yet. Don't start on it.

## CI checks

### What becomes a CI check

A rule goes into CI only when all three hold:

1. It is already a documented convention.
2. Breaking it harms users silently: nothing else would catch it before it ships.
3. A failure can be fixed mechanically.

Rules about style alone are not CI checks. They are swept by hand from time to time.

### The checks today

On every pull request and in the merge queue (`.github/workflows/build.yml`). The required status is `ci-ok`, which passes only when every job below passed. A docs-only change skips the build jobs.

| Job | What it checks | When it fails |
|---|---|---|
| `build` (macOS, Windows, Linux) | Builds trusscli and `AllFeaturesExample`, which calls every documented public core API (its `coverage_generated.cpp`). Then it builds and runs the addon tests (`addons/*/tests` and `addons/*/tests-*`, except the `daily-only` ones) and the core tests (`core/tests`). | Fix the build error or the failing test on that platform. |
| `build-android`, `build-web`, `build-ios` | Builds `AllFeaturesExample` for that platform (iOS: device SDK, unsigned). | Fix the compile or link error for that platform. |
| `reference-check` | `node docs/reference/check.js --strict`: every public symbol has an entry in `docs/reference/api-reference.toml`, with no orphans and no duplicates. | Add the missing entry (en / ja / ko), or remove the orphaned one. |
| `header-state-check` | `python3 tools/check_header_state.py`: no new mutable state (static locals, `inline` variables, ...) in core headers outside `tools/header_state_allowlist.txt`. | Move the state to a `.cpp` behind an accessor ([ARCHITECTURE.md §5.G](ARCHITECTURE.md#g-one-instance-per-process-header-inline-state)), or add an allowlist line with its category and reason. The failure message prints the line. |
| `header-state-check` (second step) | `python3 tools/check_core_logging.py`: no `printf` / `cerr` / `cout` / `NSLog` in `core/include/tc` and `core/platform`, so core's messages go through the Logger (log file, `onLog`, platform log). | Log through `logNotice()` / `logWarning()` / `logError()` instead. Only the Logger's own sinks carry a `log-check: allow` marker. |

Once a day on `main` (`.github/workflows/daily.yml`), the daily run builds every example (including the addon examples) on every desktop platform and on the web. It also runs the addon tests, including the `daily-only` ones, and the core tests. A failure there opens or updates a tracking issue instead of blocking a pull request.

## Security

Don't open a public issue for a security problem. Report it privately, as described in [SECURITY.md](SECURITY.md#reporting).
