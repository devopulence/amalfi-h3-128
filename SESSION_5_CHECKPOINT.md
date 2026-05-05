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

## v0.2.0 work — see below

(populated during the session)
