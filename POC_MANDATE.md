# POC Mandate — Proven Architecture and Mandatory Patterns

> **Version:** 1.0.0 — 2026-05-02
> **Purpose:** Records the POC campaign results and defines the mandatory coding patterns for all implementation work. Read this before writing any code.
> **When to read:** Once at the start of each implementation session, after CLAUDE.md.

---

## Campaign Results

| POC | Risk area | Assertions | Result | Checkpoint |
|-----|-----------|-----------|--------|------------|
| POC-1 | Bit layout + Group C macros | 685 | PASSED | `POC1_CHECKPOINT.md` |
| POC-2 | Validation predicates | 3,473 | PASSED | `POC2_CHECKPOINT.md` |
| POC-3 | Iterator state machine | 4,595 | PASSED | `POC3_CHECKPOINT.md` |
| POC-4 | FFI ABI (cross-TU) | 1,314 | PASSED | `POC4_CHECKPOINT.md` |
| **Total** | **4 dimensions** | **10,067** | **All clean under ASAN+UBSAN** | |

All POCs ran on Apple clang 21.0.0 / macOS arm64 with `-fsanitize=address,undefined -Werror`.

---

## POC Files in This Repository

```
poc1_bit_layout.c / poc1.md / POC1_CHECKPOINT.md
poc2_validation.c / poc2.md / POC2_CHECKPOINT.md
poc3_iterator.c   / poc3.md / POC3_CHECKPOINT.md
poc4_shim.h, poc4_shim.c, poc4_caller.c / poc4.md / POC4_CHECKPOINT.md
POC_TRANSFER_GUIDE.md
```

---

## The Five Mandatory Patterns

**This is a requirement, not a suggestion.** Every widening edit must follow the patterns validated in the POCs. The POCs have 10,067 assertions behind them. An alternative approach has zero. If you think there's a better way, use the POC way anyway.

### Pattern 1: Group C Macros ← POC-1

The 14 Group C macros in `poc1_bit_layout.c` (lines ~100–170) are the **exact implementations** to place in `h3Index.h`. Copy verbatim. Do not:
- Rewrite them in a "simpler" form
- Change mask expressions
- Replace the `H3_SET_EFFECTIVE_RESOLUTION` GCC statement-expression with a comma expression
- Add wrappers that bypass the dispatch logic

### Pattern 2: Validation Predicates ← POC-2

| Predicate | Proven pattern | Do NOT do instead |
|-----------|---------------|-------------------|
| `_hasGoodTopBits` | Split low-half/high-half check | Single-expression 128-bit shift (what breaks) |
| `_firstOneIndex` | High-half-first: check high, then low, return -1 for h=0 | `__builtin_clzll` on truncated value (UB when set bit > 63) |
| `_hasAny7UptoRes` | Stock bit-magic for 1-15, loop for 16-22 via `H3_GET_DIGIT_AT_RES` | Extend stock bit-magic to 128 bits (mask doesn't scale) |
| `_hasAll7AfterRes` | Loop from res+1 to effective_res, remove early-return at res>=15 | Keep early return (skips ext check entirely) |
| `_hasDeletedSubsequence` | Walk digits via `H3_GET_DIGIT_AT_RES` loop | Stock `h <<= 19; h >>= 19` mask (only covers 45 bits) |

### Pattern 3: Iterator State Machine ← POC-3

| Function | Proven pattern | Do NOT do instead |
|----------|---------------|-------------------|
| `_zeroIndexDigits` | `for` loop with `H3_SET_DIGIT_AT_RES` | Stock bit-magic (negative shift when end>15 — UB) |
| `_incrementResDigit` | Read/write via `H3_GET/SET_DIGIT_AT_RES` | Stock shift formula (same UB class) |
| `_getResDigit` | `H3_GET_DIGIT_AT_RES` | `H3_GET_INDEX_DIGIT` (UB for res>15) |
| `_iterInitParent` | `H3_GET/SET_EFFECTIVE_RESOLUTION`, `H3_SET_DIGIT_AT_RES` | `H3_GET/SET_RESOLUTION` (half-state cells) |
| `iterStepChild` | `H3_GET_EFFECTIVE_RESOLUTION` for childRes | Stock resolution macros (wrong for ext) |

### Pattern 4: FFI Shim ← POC-4

The 10 function signatures in `poc4_shim.h` are validated. All return `H3Error`, all pass `H3Index` by pointer (never by value), all use `h3_ext_` prefix. `sizeof` and `_Alignof` = 16 in both TUs. Replace stub bodies with real H3 calls. Do not change signatures or pointer convention.

### Pattern 5: Widening Rules ← Playbook §5

| Rule | Edit pattern | POC that validates |
|------|-------------|-------------------|
| LB (Loop-Bound) | `H3_GET_RESOLUTION` → `H3_GET_EFFECTIVE_RESOLUTION` | POC-3 |
| GR (Guard Relaxation) | `MAX_H3_RES` → `MAX_H3_EXT_RES` in guards | POC-1 |
| DW/DR (Digit Write/Read) | `H3_SET/GET_INDEX_DIGIT` → `H3_SET/GET_DIGIT_AT_RES` | POC-1, POC-3 |
| RW (Resolution Write) | `H3_SET_RESOLUTION` → `H3_SET_EFFECTIVE_RESOLUTION` | POC-1, POC-3 |
| INIT (Ext-Init) | `H3_INIT` → `H3_INIT_EXT` when res may be ≥16 | POC-1 |

---

## Re-Run POCs as Diagnostics

If you suspect a change broke something architectural, re-run the relevant POC (< 2 seconds each):

```bash
# Macros
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -o poc1 poc1_bit_layout.c -lm && ./poc1

# Validation predicates
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -o poc2 poc2_validation.c -lm && ./poc2

# Iterator
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -o poc3 poc3_iterator.c -lm && ./poc3

# FFI (separate TUs — critical)
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -c poc4_shim.c -o poc4_shim.o
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -c poc4_caller.c -o poc4_caller.o
gcc -fsanitize=address,undefined -o poc4 poc4_shim.o poc4_caller.o -lm && ./poc4
```

---

## What Was NOT Proven

- Integration with the actual H3 source tree (typedef cascade, CMake, stock test fixtures)
- Geometric correctness (lat/lng encoding, face/ijk projection at ext resolutions)
- Performance
- Cross-platform behavior (Linux x86_64 — expected to match but unverified)
