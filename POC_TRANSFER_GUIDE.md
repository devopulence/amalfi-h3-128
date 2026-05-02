# H3-Extended (128-bit) — POC Results and Implementation Transfer Guide

> **Version:** 1.0.0 — 2026-05-02
> **Context:** Four proof-of-concept validations were executed in isolation before any H3 source code was modified. All four passed. This document records the results and tells implementation sessions exactly how to use the validated code.

## POC Campaign Results

| POC | Risk area | Assertions | Result | Checkpoint |
|-----|-----------|-----------|--------|------------|
| POC-1 | Bit layout + Group C macros | 685 | PASSED | `POC1_CHECKPOINT.md` |
| POC-2 | Validation predicates | 3,473 | PASSED | `POC2_CHECKPOINT.md` |
| POC-3 | Iterator state machine | 4,595 | PASSED | `POC3_CHECKPOINT.md` |
| POC-4 | FFI ABI (cross-TU) | 1,314 | PASSED | `POC4_CHECKPOINT.md` |
| **Total** | **4 dimensions** | **10,067** | **All clean under ASAN+UBSAN** | |

All POCs compiled with `-fsanitize=address,undefined -Werror -Wall -Wextra` on Apple clang 21.0.0 / macOS arm64. Zero sanitizer findings. Zero failures.

---

## What Was Proven (and What Was NOT)

### Proven — do not re-derive, re-debate, or redesign:

1. **The bit layout is correct.** Bits 0–63 are stock H3. Bit 64 is the ext flag. Bits 65–85 are ext digits 16–22. Bits 86–127 are reserved (must be zero). Resolution encoding: `effective_res = stock_res_field + (ext_flag * 16)`. This is settled. POC-1 proved it with 685 assertions.

2. **All 14 Group C macros are correct.** The exact implementations in `poc1_bit_layout.c` (lines ~100–170) are validated. They can be copied verbatim into `h3Index.h`. Do not rewrite them. Do not "improve" them. They are proven.

3. **The five validation predicates work on 128-bit cells.** The widened implementations of `_hasGoodTopBits`, `_firstOneIndex`, `_hasAny7UptoRes`, `_hasAll7AfterRes`, and `_hasDeletedSubsequence` in `poc2_validation.c` are validated. POC-2 proved them with 3,473 assertions including adversarial inputs.

4. **The iterator state machine crosses the stock/ext boundary correctly.** The widened `_zeroIndexDigits`, `_incrementResDigit`, `_getResDigit`, `_iterInitParent`, and `iterStepChild` in `poc3_iterator.c` are validated. POC-3 enumerated 828,210 children including the full res 15 → 22 depth test under UBSAN. No negative-shift UB. No duplicates. Correct counts for hexagons and pentagons.

5. **The FFI shim ABI is safe.** `__uint128_t` values survive cross-TU pointer passing. `sizeof` and `_Alignof` are consistent (16/16) across translation units. All 10 shim function signatures from §7.1 are correct. String round-trips work across the TU boundary. POC-4 proved this with 1,314 assertions across separate .o files.

### NOT proven — still needs implementation-time validation:

- **Integration with the actual H3 source tree.** The POCs used self-contained macro copies, not the real `h3Index.h`. The typedef cascade through CMake, the interaction with stock test fixtures, and compiler warnings from unchanged code paths are untested.
- **Geometric correctness.** The POCs validated bit manipulation and state machines, not lat/lng encoding, face/ijk projection, or boundary computation at ext resolutions.
- **Performance.** No benchmarks were run. The ~2–5× slowdown expectation is still theoretical.
- **Cross-platform behavior.** POCs ran on macOS arm64 only. Linux x86_64 behavior is expected to match (same ABI for `__uint128_t` on System V) but is unverified.

---

## Implementation Transfer Map

This section tells each implementation phase exactly which POC code to use and how.

### Phase A (Type Widening) ← POC-1

**What to copy from `poc1_bit_layout.c`:**

| Source location in POC | Destination in H3 source | Notes |
|----------------------|--------------------------|-------|
| Group C constants (macros #1–6: `MAX_H3_EXT_RES`, `H3_EXT_FLAG_OFFSET`, `H3_EXT_FLAG_MASK`, `H3_EXT_DIGITS_OFFSET`, `H3_EXT_DIGITS_MASK`, `H3_INIT_EXT`) | `src/h3lib/include/constants.h` (for `MAX_H3_EXT_RES`) and `src/h3lib/include/h3Index.h` (for the rest) | Add adjacent to existing Group A/B definitions. Do not modify Group A or Group B. |
| Group C getters/setters (macros #7–14) | `src/h3lib/include/h3Index.h` (or new `h3IndexExt.h`) | Copy verbatim. The `H3_SET_EFFECTIVE_RESOLUTION` GCC statement-expression form is validated — do not simplify to a comma expression. |
| `typedef __uint128_t H3Index;` | `src/h3lib/include/h3api.h.in` (replaces `typedef uint64_t H3Index;`) | Add the `#error` guard for missing `__SIZEOF_INT128__` above the typedef. |

**What NOT to copy:** The Group A/B macros in the POC are copies of stock H3 — they already exist in the source. Don't duplicate them.

**Verification after copy:** Run `ctest --output-on-failure`. Stock 316/316 must still pass. Add a `static_assert(sizeof(H3Index) == 16, "")` to confirm.

### Phase D4 (Hierarchy + Iterators) ← POC-3

**What to copy from `poc3_iterator.c`:**

| Source location in POC | Destination in H3 source | Notes |
|----------------------|--------------------------|-------|
| `_zeroIndexDigits` loop implementation | `src/h3lib/lib/h3Index.c` (replaces the stock bit-magic body) | The POC uses a simple `for` loop with `H3_SET_DIGIT_AT_RES`. This replaces the stock `m <<= ...` bit-magic that UBs on `end > 15`. |
| `_incrementResDigit` dispatch implementation | `src/h3lib/lib/iterators.c` | Replaces the stock `val <<= H3_PER_DIGIT_OFFSET * (MAX_H3_RES - res)` that UBs on `res > 15`. |
| `_getResDigit` dispatch wrapper | `src/h3lib/lib/iterators.c` | Wraps `H3_GET_DIGIT_AT_RES` instead of `H3_GET_INDEX_DIGIT`. |
| `_iterInitParent` widened sites | `src/h3lib/lib/iterators.c` | Three specific lines change: `H3_GET_RESOLUTION` → `H3_GET_EFFECTIVE_RESOLUTION`, `H3_SET_RESOLUTION` → `H3_SET_EFFECTIVE_RESOLUTION`, guard `MAX_H3_RES` → `MAX_H3_EXT_RES`. |
| `iterStepChild` widened sites | `src/h3lib/lib/iterators.c` | `H3_GET_RESOLUTION` → `H3_GET_EFFECTIVE_RESOLUTION` for the childRes read. |

**What NOT to copy:** The `ChildIterator` struct in the POC is a simplified version. The real struct in `iterators.c` has additional fields. Widen the real struct in place — don't replace it.

**Verification after copy:** Run the D4-G0 canary (stock-stock byte identity) BEFORE and AFTER touching iterators. Run D4-G3a (hexagon res 15 → 7 children at res 16) and D4-G6 (full recursion to res 22).

### Phase E (Validation Widening) ← POC-2

**What to copy from `poc2_validation.c`:**

| Source location in POC | Destination in H3 source | Notes |
|----------------------|--------------------------|-------|
| `_hasGoodTopBits` rewrite | `src/h3lib/lib/h3Index.c` | Full function replacement. The POC's split low-half/high-half implementation is the validated version. |
| `_firstOneIndex` rewrite | `src/h3lib/lib/h3Index.c` | Full function replacement. High-half-first check eliminates the `__builtin_clzll(0)` UB. |
| `_hasAny7UptoRes` ext extension | `src/h3lib/lib/h3Index.c` | Add the ext-digit loop after the existing stock bit-magic. Keep the stock path unchanged for digits 1–15. |
| `_hasAll7AfterRes` ext extension | `src/h3lib/lib/h3Index.c` | Replace the early-return-true at `res >= 15` with the loop that checks ext digits. |
| `_hasDeletedSubsequence` ext extension | `src/h3lib/lib/h3Index.c` | Extend to walk ext digits via `H3_GET_DIGIT_AT_RES`. |

**What NOT to copy:** The POC's simplified `_hasDeletedSubsequence` may not capture every nuance of the stock pentagon detection logic. The stock digit-pattern rules should be preserved verbatim — only the loop range and digit-access macros change.

**Verification after copy:** Run E1 (`_hasGoodTopBits` accepts valid ext cells), E2 (`_firstOneIndex` for high-half bits), E3 (`isValidCell` accepts 1,000 valid ext, rejects 1,000 corrupted).

### Phase D7 (FFI Shim) ← POC-4

**What to copy from `poc4_shim.h` / `poc4_shim.c`:**

| Source location in POC | Destination in H3 source | Notes |
|----------------------|--------------------------|-------|
| `poc4_shim.h` function prototypes | New header `src/h3lib/include/h3ExtShim.h` (or added to `h3api.h.in`) | The 10 function signatures are validated. The `h3_ext_` prefix, the by-pointer convention, and the `H3Error` return type are all proven. |
| `poc4_shim.c` string serialization (`h3_ext_h3_to_string`, `h3_ext_string_to_h3`) | `src/h3lib/lib/h3Index.c` or new `h3ExtShim.c` | The hex serialization/parsing logic is validated. Replace the stub bodies of the other 8 functions with real H3 calls — the ABI contract (pointer-passing, error returns) stays exactly as the POC defined it. |

**What NOT to copy:** The stub implementations of encode/decode/hierarchy/distance in `poc4_shim.c` are not real H3 geometry — they were ABI test stubs. Replace them with calls to the real (widened) H3 functions.

**Verification after copy:** Run D7-G1 (all 10 functions linkable), D7-G2 (no ABI corruption), D7-G3 (string round-trip at FFI boundary).

---

## Rules for Implementation Sessions

1. **The POC code is the reference implementation.** When the playbook says "implement X," check the POC first. If the POC has a working version, use it. Do not reinvent.

2. **Do not modify the POC files.** They are frozen proof artifacts. Copy from them into the H3 source. If you need to adapt (e.g., the real iterator struct has more fields), adapt in the destination — not the source.

3. **The macro block from POC-1 is the single source of truth for Group C.** Every POC copied this block. The implementation must use the same definitions. If a macro needs to change (it shouldn't — it's validated), all four POCs must be re-run.

4. **When in doubt, re-run the relevant POC.** If you suspect a Phase A change broke the macros, run `poc1_bit_layout.c`. If the validation predicates behave unexpectedly, run `poc2_validation.c`. The POC programs are fast (< 2 seconds each) and definitive.

5. **The POC checkpoint files are evidence.** They record the exact compiler, platform, assertion count, and result. They should be committed and never deleted. They're the audit trail that says "we validated the architecture before we touched the source."

---

## File Inventory

```
poc1_bit_layout.c          # POC-1 test program (pre-written, 700 lines)
poc1.md                    # POC-1 specification (19 test categories)
POC1_CHECKPOINT.md         # POC-1 result (685/685)

poc2_validation.c          # POC-2 test program (written by session)
poc2.md                    # POC-2 specification (23 test categories)
POC2_CHECKPOINT.md         # POC-2 result (3,473/3,473)

poc3_iterator.c            # POC-3 test program (written by session)
poc3.md                    # POC-3 specification (28 test categories)
POC3_CHECKPOINT.md         # POC-3 result (4,595/4,595)

poc4_shim.h                # POC-4 shared header
poc4_shim.c                # POC-4 shim implementation
poc4_caller.c              # POC-4 test caller
poc4.md                    # POC-4 specification (22 test categories)
POC4_CHECKPOINT.md         # POC-4 result (1,314/1,314)

POC_TRANSFER_GUIDE.md      # This file
```
