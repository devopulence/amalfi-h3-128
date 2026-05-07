# Architecture

## Why the fork exists

Stock Uber H3 caps spatial resolution at 15 (~50 cm² hexagons), which
is too coarse for the Amalfi Intelligence digital-twin pipeline.
Resolution 19 gives ~7 m² hexagons; res 20 gives ~1 m²; res 22 gives
~5 mm² (sub-centimeter precision). To reach those resolutions the H3
index width has to grow from 64 to 128 bits. There is no extension
point in stock H3 for this — every digit-access macro, every iterator,
every encoder/decoder/validator is hard-wired around 64-bit shifts.

The fork widens the type and every code path that touches it, while
keeping res 0-15 cells **byte-identical to stock**. A user who only
calls res 0-15 functions sees no change in behavior or output.

## The Four Non-Negotiables

These are absolute. If any is violated, the fork has failed.

### 1. Accuracy > Speed

A 2-5× slowdown is acceptable. No 64-bit fast-paths, no premature
optimization, no skipping ext-aware code at "low" resolutions.

### 2. Bit layout is a strict superset of 64-bit

For any res 0-15 cell:
- Bits 0-63 are byte-identical to stock H3.
- Bits 64-127 are zero.

Layout (little-endian native order):

```
bits   0..52   reserved/mode/baseCell/digits 1..15      (stock)
bit       64   ext flag (0 = stock cell, 1 = ext cell)
bits 65..85    ext digits 16..22 (3 bits each, 7 × 3 = 21 bits)
bits 86..127   reserved (must be zero)
```

Resolution encoding: `effective_res = stock_res_field + (ext_flag * 16)`
where `stock_res_field` is the 4-bit field at bits 52-55 (0-15 in
stock terms) and `ext_flag` is bit 64. So an ext cell at res 19 has
`stock_res_field=3, ext_flag=1, effective_res=19`. POC-1 proved this
with 685 assertions.

If compatibility breaks (i.e. a res 0-15 cell produces a different
64-bit value than stock), STOP and surface — that's a fork bug, not
an enhancement.

### 3. All 316 stock tests pass byte-identically

Baseline: 316/316 on unmodified `69e01f3c` (v4.4.1). Any red stock
test means revert. The fork's own ext tests are additive and currently
push the count to 327/327 (327 = 316 stock + 11 ext suites).

### 4. Compile + test after every edit. Sanitizers ON.

```
edit → cmake --build build-dev -j → ctest --output-on-failure → 
      green = commit, red = revert
```

One widening site = one commit. Tests in same commit. Pre-commit hook
installed at `.git/hooks/pre-commit` runs `cmake --build` and
`ctest --output-on-failure --quiet` on every commit. **NO --no-verify.**
ASAN+UBSAN are on throughout dev builds.

---

## The Five Mandatory Coding Patterns (POC-validated)

When extending or modifying the fork, these patterns are not
suggestions — the POC campaign has 10,067 assertions behind them. An
alternative approach has zero. Use the POC code.

### Pattern 1: Group C Macros (← POC-1, 685 assertions)

The 14 Group C macros in `poc1_bit_layout.c` (lines ~100–170) are the
**exact** implementations that landed in `src/h3lib/include/h3Index.h`.
Do not rewrite them. Do not "simplify." Do not replace
`H3_SET_EFFECTIVE_RESOLUTION` (which is a GCC statement-expression)
with a comma-expression — the validated form survives macro reuse.

### Pattern 2: Validation Predicates (← POC-2, 3,473 assertions)

| Predicate | Validated approach | Wrong approach (UB or incorrect) |
|---|---|---|
| `_hasGoodTopBits` | Split low-half + high-half check | Single 128-bit shift expression |
| `_firstOneIndex` | High-half-first; return -1 for h=0 | `__builtin_clzll` on truncated value |
| `_hasAny7UptoRes` | Stock bit-magic for res 1-15 + loop for 16-22 via `H3_GET_DIGIT_AT_RES` | Extend bit-magic to 128 — mask doesn't scale |
| `_hasAll7AfterRes` | Loop res+1 → effective_res; remove early-return at res ≥ 15 | Keep early-return — skips ext check entirely |
| `_hasDeletedSubsequence` | Walk digits via `H3_GET_DIGIT_AT_RES` | Stock `h <<= 19; h >>= 19` mask (only 45-bit window) |

### Pattern 3: Iterator State Machine (← POC-3, 4,595 assertions)

| Function | Validated approach | Wrong approach |
|---|---|---|
| `_zeroIndexDigits` | `for` loop with `H3_SET_DIGIT_AT_RES` | Stock bit-magic — negative shift on `end > 15` |
| `_incrementResDigit` | Read/write via `H3_GET/SET_DIGIT_AT_RES` | Stock shift formula — same UB class |
| `_getResDigit` | `H3_GET_DIGIT_AT_RES` | `H3_GET_INDEX_DIGIT` (UB for res > 15) |
| `_iterInitParent` | `H3_GET/SET_EFFECTIVE_RESOLUTION` + `H3_SET_DIGIT_AT_RES` | `H3_GET/SET_RESOLUTION` (creates half-state cells) |
| `iterStepChild` | `H3_GET_EFFECTIVE_RESOLUTION` for childRes | Stock resolution macros (wrong for ext) |

### Pattern 4: FFI Shim (← POC-4, 1,314 assertions)

The shim signatures in `src/h3lib/include/h3ExtShim.h` are validated
ABI. Every shim function:

- Returns `H3Error` (the underlying H3 error, or `E_FAILED` on null).
- Passes `H3Index` **by pointer to a 16-byte buffer** — never by value.
- Uses the `h3_ext_` prefix (no symbol collision with stock libh3).
- Treats NULL pointer as `E_FAILED` (not a segfault).

`__uint128_t` lacks a stable register-passing ABI on Windows MSVC and
is fragile on other platforms. The by-pointer convention sidesteps
this entirely and makes Python cffi binding trivial. **Never expose
`__uint128_t` across the FFI boundary by value.**

### Pattern 5: Widening Rules (← Playbook §5)

These are the five mechanical edits that turn a 64-bit-only function
into an ext-aware function:

| Rule | Edit | Validated by |
|---|---|---|
| **LB** (Loop-Bound) | `H3_GET_RESOLUTION` → `H3_GET_EFFECTIVE_RESOLUTION` | POC-3 |
| **GR** (Guard Relaxation) | `MAX_H3_RES` → `MAX_H3_EXT_RES` (in input guards) | POC-1 |
| **DW** / **DR** (Digit Write/Read) | `H3_SET/GET_INDEX_DIGIT` → `H3_SET/GET_DIGIT_AT_RES` | POC-1, POC-3 |
| **RW** (Resolution Write) | `H3_SET_RESOLUTION` → `H3_SET_EFFECTIVE_RESOLUTION` | POC-1, POC-3 |
| **INIT** (Ext-Init) | `H3_INIT` → `H3_INIT_EXT` (when res may be ≥ 16) | POC-1 |

The audit infrastructure (`.claude/skills/h3-128-audit/audit.py` in the
fork) enumerates every site that should have one of these mutations
applied and tracks status (widened, deferred, intentionally skipped).
As of HEAD: **23 sites classified, 0 CRITICAL, 0 FINDING**.

---

## Don'ts

- **Don't reverse the dependency.** The fork imports nothing from
  `amalfi_intelligence_platform`. Other repos depend on the fork; the
  fork depends on nothing internal.
- **Don't open-source the fork.** Private repository.
- **Don't change bit positions in bits 0-63.** That's the stock layout
  and breaking it breaks Non-Negotiable #2.
- **Don't bump `MAX_H3_RES`.** It stays at 15. The new constant is
  `MAX_H3_EXT_RES = 22` and lives next to it.
- **Don't optimize before correctness.** ~2-5× slowdown is acceptable.
- **Don't deviate from POC-proven patterns.** They're in
  `POC_MANDATE.md` for a reason.
- **Don't modify the POC files.** They are frozen proof artifacts.
  Copy from them.
- **Don't pin source-side facts in comments.** Use `grep` — the fork
  evolves, comments rot.
- **Don't pass `__uint128_t` by value across the FFI boundary.**
  POC-4 was specifically about this.

---

## Toolchain Constraints

- C99, K&R braces, 4-space indent.
- Comment widening edits with `// H3-EXTENDED: <reason>`.
- `#error` if `__SIZEOF_INT128__` is missing. **No struct fallback.
  MSVC unsupported.**
- Apple clang 21.0.0 / Darwin arm64 (local development).
- Ubuntu gcc / x86_64 (CI matrix — used for cross-platform parity).
- Build flags (dev): `-fsanitize=address,undefined -fno-omit-frame-pointer
  -O1 -g -Werror -Wall -Wextra`.
- Build flags (release): `-DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON`.
