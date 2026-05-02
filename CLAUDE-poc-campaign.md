# Amalfi H3-Extended (128-bit) — Preflight Validation Campaign

> **Version:** 1.0.0 — 2026-05-02
> **Scope:** Four proof-of-concept validations that prove the 128-bit architecture is sound before any H3 source code is modified.

## Mission

We are validating four architectural risk areas of the H3-Extended (128-bit) fork in complete isolation — zero H3 source dependencies. Each POC is a standalone C program that compiles with sanitizers, runs, and either passes (exit 0) or fails with a specific diagnostic. All four must pass before the implementation sessions begin.

---

## The Four POCs

| POC | Risk area | Files to run | Output file(s) | Target assertions |
|-----|-----------|-------------|-----------------|-------------------|
| POC-1 | Bit layout + Group C macros | `poc1_bit_layout.c` (pre-written) | — | 685 |
| POC-2 | Validation predicates | `poc2_validation.c` (session writes) | `poc2_validation.c` | 300+ |
| POC-3 | Iterator state machine | `poc3_iterator.c` (session writes) | `poc3_iterator.c` | 500+ |
| POC-4 | FFI ABI (cross-TU) | 3 files (session writes) | `poc4_shim.h`, `poc4_shim.c`, `poc4_caller.c` | 200+ |

Specification files: `poc1.md`, `poc2.md`, `poc3.md`, `poc4.md` — one per POC, each documents the test categories, construction instructions, and success criteria.

---

## POC-1: Bit Layout + Group C Macros

**What it proves:** The 128-bit bit layout is correct and all 14 Group C macros work as specified.

**Why it's first:** Every resolution encoding, digit position, macro, and dispatch signal depends on this layout. If it's wrong, nothing else matters.

**19 test categories (PF-01 through PF-19):** sizeof, stock zero-extension, resolution round-trip, ext flag range, stock-res field encoding, digit read/write round-trip for all 22 positions, bit position verification, reserved bits zeroing, H3_INIT/H3_INIT_EXT patterns, UBSAN cleanliness, stock-macros-on-ext behavior, cross-boundary isolation, mask coverage, field survival (base cell, mode), single-evaluation safety.

**Run:** `gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -o poc1_bit_layout poc1_bit_layout.c -lm && ./poc1_bit_layout`

**Full specification:** `poc1.md`

---

## POC-2: Validation Predicates

**What it proves:** Five internal predicates (`_hasGoodTopBits`, `_firstOneIndex`, `_hasAny7UptoRes`, `_hasAll7AfterRes`, `_hasDeletedSubsequence`) work correctly on 128-bit cells.

**Why it matters:** `_hasGoodTopBits` is a structural break — the stock shift-and-compare produces a different residual on 128 bits. `_firstOneIndex` calls `__builtin_clzll` on truncated input — UB when the set bit is in the high half. These are the two CRITICAL items in the implementation Phase E.

**23 test categories (VP-01 through VP-23):** Valid/invalid stock cells, valid/invalid ext cells, corrupted cells (dirty reserved bits, wrong mode, high bit set), sentinel digit detection in ext range, pentagon deleted-subsequence detection through ext digits, boundary cases at res 15/16.

**Run:** `gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -o poc2_validation poc2_validation.c -lm && ./poc2_validation`

**Full specification:** `poc2.md`

---

## POC-3: Iterator State Machine

**What it proves:** The widened child-cell iterator produces the correct sequence of children when crossing the stock/ext boundary and when operating entirely in ext range.

**Why it matters:** `cellToChildren` delegates entirely to an iterator. Five functions participate, and all five have UB shifts when res > 15. The `_incrementResDigit` and `_zeroIndexDigits` traps (§6.6, §6.10) are the most common UBSAN failures in widening work.

**28 test categories (IT-01 through IT-28):** `_zeroIndexDigits` across boundary, `_incrementResDigit` at ext positions, hexagon parent res 15 → 7 children at res 16, full enumeration res 15 → 22 (823,543 children under UBSAN), pentagon child counts, no duplicates, correct effective resolution on every child.

**Run:** `gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -o poc3_iterator poc3_iterator.c -lm && ./poc3_iterator`

**Full specification:** `poc3.md`

---

## POC-4: FFI ABI (Cross-Translation-Unit)

**What it proves:** `__uint128_t` values survive cross-TU function calls when passed by pointer.

**Why it matters:** The shim passes cells by pointer because `__uint128_t` lacks a stable register-passing ABI. This POC compiles a shim and a caller as **separate .o files**, links them, and proves round-trip integrity.

**22 test categories (ABI-01 through ABI-22):** sizeof/alignof consistency across TUs, all 10 shim functions callable, single-value and array round-trips, string serialization across TU boundary, adversarial inputs, H3Error contract.

**Run (MUST be separate compilation):**
```bash
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -c poc4_shim.c -o poc4_shim.o
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -c poc4_caller.c -o poc4_caller.o
gcc -fsanitize=address,undefined -o poc4_ffi poc4_shim.o poc4_caller.o -lm
./poc4_ffi
```

**Full specification:** `poc4.md`

---

## Execution Order

```
POC-1 (bit layout)     → proves the macros
  ↓
POC-2 (validation)     → uses those macros to test predicates
  ↓
POC-3 (iterator)       → uses those macros to test the state machine
  ↓
POC-4 (FFI ABI)        → uses those macros to test cross-TU passing
```

POC-2, 3, and 4 all copy the Group A/B/C macro block from `poc1_bit_layout.c` as their foundation. **If POC-1 fails, stop. Nothing else can proceed.**

---

## How to Run Each POC

Each POC is a **fresh Claude Code session**. For each:

1. This `CLAUDE.md` stays in the repo root for all four sessions.
2. The relevant `pocN.md` must be in the repo root.
3. `poc1_bit_layout.c` must be in the repo root (source file for POC-1; macro reference for POC-2/3/4).
4. Paste the kickoff prompt from the relevant section below.

---

## Checkpoint Documentation

Each POC writes a checkpoint file on success:

| POC | Checkpoint file |
|-----|----------------|
| POC-1 | `POC1_CHECKPOINT.md` |
| POC-2 | `POC2_CHECKPOINT.md` |
| POC-3 | `POC3_CHECKPOINT.md` |
| POC-4 | `POC4_CHECKPOINT.md` |

Each checkpoint contains:
```
# POC-N Checkpoint
- Date: <ISO timestamp>
- Compiler: <gcc/clang version>
- Platform: <uname -s -m>
- Assertions: <count> passed, 0 failed
- ASAN: clean
- UBSAN: clean
- Exit code: 0
- Result: POC-N PASSED
- <POC-specific fields as documented in pocN.md>
```

**All four checkpoints must exist before the implementation sessions begin.**

---

## Kickoff Prompts

### POC-1 Kickoff
```
Read CLAUDE.md end-to-end. Then read poc1.md.

This is POC-1 of 4 — bit layout validation. The test program poc1_bit_layout.c is already written.

Step 1 — Compile with sanitizers:
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc1_bit_layout poc1_bit_layout.c -lm

If compilation fails, report the exact errors. Do not attempt to fix the source — report only.

Step 2 — Run:
./poc1_bit_layout

Step 3 — Validate. Expected: 685 passed, 0 failed, exit 0, no ASAN/UBSAN on stderr.

If all conditions met, write POC1_CHECKPOINT.md per the format in CLAUDE.md. Commit with message "poc-1: bit layout validated — 685/685 clean under ASAN+UBSAN". Then stop.

If anything fails, report the exact failure. Do not proceed.
```

### POC-2 Kickoff
```
Read CLAUDE.md end-to-end. Then read poc2.md.

This is POC-2 of 4 — validation predicates. POC-1 must have already passed (check for POC1_CHECKPOINT.md).

Step 1 — Write poc2_validation.c per the construction instructions in poc2.md. Copy the Group A/B/C macro block from poc1_bit_layout.c as the foundation.

Step 2 — Compile:
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc2_validation poc2_validation.c -lm

Step 3 — Run and validate. Expected: all pass, exit 0, no sanitizer output.

If all conditions met, write POC2_CHECKPOINT.md and commit. If anything fails, diagnose, fix, and iterate until all pass.
```

### POC-3 Kickoff
```
Read CLAUDE.md end-to-end. Then read poc3.md.

This is POC-3 of 4 — iterator state machine. POC-1 must have already passed (check for POC1_CHECKPOINT.md).

Step 1 — Write poc3_iterator.c per the construction instructions in poc3.md. Copy the Group A/B/C macro block from poc1_bit_layout.c as the foundation.

Step 2 — Compile:
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc3_iterator poc3_iterator.c -lm

Step 3 — Run and validate. Expected: all pass, exit 0, no sanitizer output. The full-depth test (res 15 → 22, 823,543 children) may take 1-2 seconds — that's expected.

If all conditions met, write POC3_CHECKPOINT.md and commit. If anything fails, diagnose, fix, and iterate until all pass.
```

### POC-4 Kickoff
```
Read CLAUDE.md end-to-end. Then read poc4.md.

This is POC-4 of 4 — FFI ABI validation. POC-1 must have already passed (check for POC1_CHECKPOINT.md).

Step 1 — Write poc4_shim.h, poc4_shim.c, and poc4_caller.c per the construction instructions in poc4.md. Copy the Group A/B/C macro block from poc1_bit_layout.c into both poc4_shim.c and poc4_caller.c.

Step 2 — Compile as SEPARATE translation units (critical — do not combine):
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -c poc4_shim.c -o poc4_shim.o
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -c poc4_caller.c -o poc4_caller.o
gcc -fsanitize=address,undefined -o poc4_ffi poc4_shim.o poc4_caller.o -lm

Step 3 — Run and validate. Expected: all pass, exit 0, no sanitizer output.

If all conditions met, write POC4_CHECKPOINT.md and commit. If anything fails, diagnose, fix, and iterate until all pass.
```

---

## Don't

- **Don't import any H3 headers in any POC.** Every POC is isolation-tested.
- **Don't skip ASAN/UBSAN flags.** The sanitizers catch UB shifts, overflows, and alignment issues.
- **Don't treat a partial pass as acceptable.** Every assertion must pass.
- **Don't run POC-2/3/4 before POC-1 passes.**
- **Don't combine POC-4 into a single translation unit.**
- **Don't modify `poc1_bit_layout.c` during POC-2/3/4.** Copy macros from it; don't edit it.

---

## After All Four Pass

When all four checkpoints exist, the architecture is validated:

1. **Bit layout** — the encoding is correct
2. **Validation** — the predicates work on 128-bit cells
3. **Iterator** — the state machine crosses the stock/ext boundary
4. **FFI ABI** — 128-bit values survive cross-TU pointer passing

The validated POC code transfers directly into the H3 source during implementation Phases A, E, D4, and D7.
