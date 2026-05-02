# POC-2: Validation Predicates — Test Specification

> **Version:** 1.0.0 — 2026-05-02
> **Prerequisite:** POC-1 passed (685/685)

## Objective

Write a standalone C program (`poc2_validation.c`) that implements and exhaustively tests the five widened validation predicates for 128-bit H3 cells. Zero H3 dependencies. Compiles and runs with sanitizers.

## Construction Instructions

1. **Copy the macro foundation from POC-1.** The first ~150 lines of `poc2_validation.c` should be identical to `poc1_bit_layout.c`: the `__uint128_t` typedef, all Group A constants, all Group B macros, and all 14 Group C macros. These are the proven-correct macros from POC-1.

2. **Implement the five predicates** as `static inline` functions, using only the macros above. Each predicate has a specific widened implementation documented in CLAUDE.md.

3. **Write test functions** for each predicate covering the test categories listed in CLAUDE.md (VP-01 through VP-23). Use the same `ASSERT_MSG` harness pattern from POC-1.

4. **Build helper functions** to fabricate test cells:
   - `make_valid_stock_cell(int res, int base_cell)` — mode=1, correct sentinel pattern
   - `make_valid_ext_cell(int res, int base_cell)` — mode=1, ext flag set, correct sentinel pattern
   - `make_corrupted_cell(...)` — various corruption types: dirty reserved bits, wrong mode, high bit set, non-7 past res, etc.

## Assertion Target

Aim for **300+ assertions** across the 23 test categories. More is better — the goal is exhaustive coverage of the predicate behavior on well-formed, malformed, boundary, and adversarial inputs.

## Success Criteria

```
All assertions passed, 0 failed
POC-2 PASSED — validation predicates validated.
```

Plus: exit 0, no ASAN output, no UBSAN output.

## Checkpoint

On success, write `POC2_CHECKPOINT.md` to the repo root:
```
# POC-2 Checkpoint
- Date: <ISO timestamp>
- Compiler: <gcc/clang version>
- Platform: <uname -s -m>
- Assertions: <count> passed, 0 failed
- ASAN: clean
- UBSAN: clean
- Exit code: 0
- Result: POC-2 PASSED
- Predicates validated: _hasGoodTopBits, _firstOneIndex, _hasAny7UptoRes, _hasAll7AfterRes, _hasDeletedSubsequence
```
