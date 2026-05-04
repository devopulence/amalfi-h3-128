---
name: h3-128-audit
description: H3-Extended (128-bit) structural audit — Phase G deliverable. Validates the implementation against the source-of-truth contracts in h3-extended-playbook-v4.1.0.md.
---

# H3-128 Audit

Run before every push to `feat/h3-128-mvp`:

```bash
python3 .claude/skills/h3-128-audit/audit.py --quiet
```

Exit 0 = clean (zero CRITICAL, zero FINDING).
Exit 1 = audit failed (see findings).
Exit 2 = audit infrastructure error (missing source files etc.).

## Gates checked (per playbook §8.G)

- **G1**: zero CRITICAL, zero FINDING.
- **G2**: every widening rule (LB / GR / DW / DR / RW / INIT) has at least one
  mutation in `mutations/manifest.json`.
- **G3**: every `MAX_H3_RES` site in `src/h3lib/lib/*.c` is classified as either
  widened (used as a stock/ext threshold in dispatch logic) or
  intentionally-not-widened (with a `deferred:<phase>` rationale tracked in
  `MAX_H3_RES_CLASSIFICATIONS` inside `audit.py`).

## Adding a new MAX_H3_RES site

1. Add the widening to source.
2. Update `MAX_H3_RES_CLASSIFICATIONS` in `audit.py` with `(file, line) → "widened"` or `"deferred:<phase>"`.
3. Re-run audit until clean.

## Mutation manifest

`mutations/manifest.json` — one entry per widening rule. Each entry records:
- `rule`: the widening rule (LB / GR / DW / DR / RW / INIT).
- `find` / `replace`: a single-line reverse-mutation.
- `expected_failures`: which ctest tests should go red when the mutation is applied.

The manifest documents which test guards which widening — running the mutation
manually (apply, build, ctest, revert) is the integration check.
