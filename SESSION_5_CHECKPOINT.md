# Session 5 Checkpoint — H4 close-out + v0.2.0 POC-5

> **Date:** 2026-05-05
> **Branch:** `feat/h3-128-mvp`
> **Predecessor:** SESSION_4_CHECKPOINT.md (HEAD `6bc988e9`, tag `v0.1.0-128bit-mvp`)

---

## H4 — CI matrix close-out

Session 4 left H4 (CI matrix on push) deferred. First push of the v0.1.0 tag commit (`6bc988e9`) and the subsequent Session-4 docs commit (`2be94029`) both ran on the matrix with 2/4 jobs failing.

### Failure mode

Linux gcc with `-Werror=shift-count-negative` rejected the stock-branch
expansion of `H3_SET_DIGIT_AT_RES` / `H3_GET_DIGIT_AT_RES` whenever a
caller passed a literal ext-res argument:

```
src/h3lib/include/h3Index.h:170:24: error: left shift count is negative
note: in expansion of macro 'H3_SET_INDEX_DIGIT'
note: in expansion of macro 'H3_SET_DIGIT_AT_RES'
src/apps/testapps/testValidationExt.c:223:9: H3_SET_DIGIT_AT_RES(h, 18, INVALID_DIGIT);
```

gcc constant-folds the dead branch of the dispatch and computes
`(MAX_H3_RES - 18) * 3 = -9` — a negative shift count, rejected as
warning-as-error. Apple clang does **not** run this static check, so
the issue is invisible on macOS local builds (Sessions 1-4 all green
on macOS, all red on Linux).

Triggering call sites: `testValidationExt.c:223,237,258`,
`testRotationExt.c:127,131`, `testValidationExt.c:281` — all
literal-constant ext-res arguments to the dispatch macros.

### Fix (commit `1fa4db7c`)

`src/h3lib/include/h3Index.h:227-243` — mask the stock-branch `res`
argument with `MAX_H3_RES` (`= 0xF`):

```c
#define H3_GET_DIGIT_AT_RES(h, res)                                     \
    ((res) <= MAX_H3_RES ? H3_GET_INDEX_DIGIT((h), ((res) & MAX_H3_RES)) \
                         : H3_GET_EXT_INDEX_DIGIT((h), (res)))

#define H3_SET_DIGIT_AT_RES(h, res, digit)                              \
    do {                                                                \
        if ((res) <= MAX_H3_RES)                                        \
            H3_SET_INDEX_DIGIT((h), ((res) & MAX_H3_RES), (digit));     \
        else                                                            \
            H3_SET_EXT_INDEX_DIGIT((h), (res), (digit));                \
    } while (0)
```

The mask is identity for `res ∈ [0, 15]` and unreachable at runtime for
`res > 15`, so semantics are unchanged. Only effect: gcc's static
constant-fold no longer sees a negative shift count.

### CI results

Run `25370995715` on commit `1fa4db7c` — all 4 jobs **green**:

| Config | Job ID | Duration |
|--------|--------|----------|
| release on macos-latest | 74394171722 | 1m23s |
| release on ubuntu-latest | 74394171759 | 1m39s |
| sanitizers on macos-latest | 74394171723 | 9m15s |
| sanitizers on ubuntu-latest | 74394171729 | 7m43s |

Ubuntu sanitizers is the **first cross-platform validation** of the
H3-Extended fork. POCs ran on macOS arm64 only; ctest 326/326 PASS on
glibc + gcc + Linux x86_64 confirms the architecture transports.

### §11 acceptance — final state

| Criterion | Status |
|-----------|--------|
| H1 326 ctest PASS | ✓ macOS local (586.79s wall) |
| H2 ASAN clean | ✓ |
| H3 UBSAN clean | ✓ |
| H4 CI matrix green | ✓ **closed at `1fa4db7c`** |
| H5 audit clean (`audit.py --quiet` exits 0) | ✓ |
| H6 pre-commit hook installed | ✓ |
| §11 #6 mutation manifest covers all 6 rules | ✓ |
| §11 #10 FFI shim 10 entry points smoke | ✓ |
| §11 #11 stock 316 byte-identical to upstream `69e01f3c` | ✓ |
| §11 #12 v0.1.0 tag pushed | ✓ |

**10/10 §11 acceptance criteria PASS.** v0.1.0 conclusively shipped.

### Lesson (audit-rule candidate)

Apple clang and Linux gcc disagree on which dead-branch shifts are
warned about. Pre-commit ctest only covers Apple clang locally — CI is
the only gate for gcc-specific warnings. Future work that adds
dispatch-macro patterns must validate on both compilers before the
green/red signal is meaningful.

Suggested audit rule: `audit.py` could grep for any `H3_*_AT_RES(...)`
call where the `res` argument is a literal `>= 16` and confirm the
dispatching macros wrap the stock-branch arg with `& MAX_H3_RES` (or
equivalent clamp). Open follow-up.

---

## POC-5 — CoordIJK int32 overflow stress (commit `bef722e3`)

Standalone analytic POC documenting the int32 overflow boundary.
Inherits POC-1..4 discipline (single-file C99, no libh3 linkage,
ASAN+UBSAN clean, exit 0). Uses `__builtin_*_overflow` to detect
overflow without invoking the broken arithmetic at runtime.

**85 assertions across CO-01..CO-32**, exit 0, ASAN+UBSAN clean.

### Failure boundary (POC-5 result)

| Path | First-broken res | Last-safe res | Site |
|------|-------------------|---------------|------|
| Hexagon | 21 | 20 | `maxDimByCIIres[22] = 2*7^11 > INT32_MAX` |
| Pentagon | 19 (non-monotonic*) | 18 | `_adjustPentVertOverage` 3 × maxDim × √7 |

*Pentagon is non-monotonic: trips at res 19, recovers at res 20,
trips again at res 21+. POC-5 §"CO-30" documents the pattern.

### v0.2.0 widening targets (per POC-5 §"v0.2.0 Widening Targets")

1. **CoordIJK struct** — `int → int64_t` ✓ landed at `57672bc6`
2. **CoordIJK arithmetic in coordijk.h** — partially landed (typedef +
   `ijkDistance` return + `llabs`); rest implicit via int64_t
   propagation
3. **Static tables `maxDimByCIIres` + `unitScaleByCIIres`** — extend to
   length 23 with `int64_t` elements (PENDING)
4. **`_adjustOverageClassII` / `_adjustPentVertOverage` int locals → int64_t**
   (PENDING)
5. **Add regression suite `testCoordIjkExtRes`** with res 19-22
   round-trips that POC-5 predicts will pass post-widening (PENDING)

---

## v0.2.0 widening — first commit (`57672bc6`)

`v0.2.0(coordijk): widen CoordIJK struct fields int → int64_t`

### Cascade

The struct typedef change required only 2 mechanical fixes for the
build to be green:

1. `coordijk.h:710-716` — `ijkDistance` return type `int → int64_t`,
   `abs()` → `llabs()`. The public `gridDistance` already returns
   `int64_t *`; this just removes the internal truncation. Single
   in-tree caller (`localij.c:615`) is API-compatible.
2. `src/apps/applib/lib/utility.c:55` — `coordIjkPrint` format
   specifier `%d` → `%" PRId64 "` for the widened field type.

### Verification

- `cmake --build build-dev -j` clean under ASAN+UBSAN flags.
- ctest: **326/326 PASS** under ASAN+UBSAN (581.77s wall, pre-commit).
- POC-5 still **85/85 PASS** (POC-5 is analytic, type-agnostic).

### Notes

Subsequent v0.2.0 commits should land:

1. **Table widening (next)** — `faceijk.c:315, 345`. Currently
   `static const int maxDimByCIIres[]` with 21 entries (Session-2
   stopped at index 20 because index 22 = 3,954,653,486 overflows
   int32). Widen to `static const int64_t maxDimByCIIres[]` with
   length 23 (entries through res 22). Same for `unitScaleByCIIres`.

2. **Internal int locals** — `_adjustOverageClassII` (`faceijk.c:602,
   610, 776, 918`) and `_adjustPentVertOverage` use `int maxDim,
   unitScale, transScale` locals. Widen to `int64_t`. Bundled with
   table widening per "one widening site = one commit" applied at
   the LOGICAL site (table + its consumers).

3. **Regression test** — `testCoordIjkExtRes.c`: round-trip
   `latLngToCell → cellToLatLng → latLngToCell` at res 20-22 across
   the 3 POC-5 geos + pentagon-touching coords at res 19+. After
   widening, all should round-trip bit-identically.

4. **Audit update** — `audit.py` `MAX_H3_RES_CLASSIFICATIONS` adds
   `coordijk.h` widened sites and changes `faceijk.c` table sites
   from `deferred:post-mvp` to `widened`.

---

## Session 5 commits

| Hash | Subject |
|------|---------|
| `1fa4db7c` | fix(h4): clamp res in dispatch macros for gcc shift-count-negative |
| `ea8503e6` | docs(h4): close-out — CI matrix green at 1fa4db7c (10/10 §11 PASS) |
| `bef722e3` | test: POC-5 CoordIJK int32 overflow at ext resolutions |
| `57672bc6` | v0.2.0(coordijk): widen CoordIJK struct fields int → int64_t |

ctest progression: 326/326 → 326/326 → 326/326 → 326/326 (no new
test suites this session; Session 6 will add `testCoordIjkExtRes`).

