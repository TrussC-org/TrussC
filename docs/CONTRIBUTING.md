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

## Security

Don't open a public issue for a security problem. Report it privately, as described in [SECURITY.md](SECURITY.md#reporting).
