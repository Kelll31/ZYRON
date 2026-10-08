---
description: Record or update an architecture decision in docs/DECISIONS.md (research first if facts are unverified)
argument-hint: "TITLE [or ADR-00NN to update]"
---

Create or update an ADR. Argument: `$ARGUMENTS`

1. Read `docs/DECISIONS.md`. If the argument names an existing ADR, update it in place; otherwise take the next
   free number (`ADR-00NN`) and append.
2. If the decision involves a library, license, algorithm or platform behaviour, **verify the facts now** (use
   `prior-art-scout`): license from the repo/vendor, last release, platform support. Mark facts you couldn't
   verify as *unverified* and put the verification date on verified ones.
3. Format (keep it ≤ 15 lines):
   `## ADR-00NN — Title — *Status*` where Status ∈ Proposed | Open | Accepted | Superseded by ADR-MM, then
   Context (what forces the decision, with SPEC `§` refs), Options (table if ≥ 2 real candidates), Decision or the
   criteria/spike that will decide, Consequences (what it constrains, what it unblocks, ROADMAP task ids).
4. **Only the owner can move an ADR from Open/Proposed to Accepted.** If the decision is owner-level (licensing,
   product scope, paid dependencies), present a recommendation with trade-offs and ask; don't accept it yourself.
5. Update `docs/ROADMAP.md` (`⛔` flags) and `CLAUDE.md` "Known risks" if the decision changes them.
