# H3-Extended Lessons Learned

> Living document. Captures non-obvious surprises encountered during the
> H3-Extended (128-bit) implementation. Use as a reference before starting
> a new widening session: each lesson here cost real ctest cycles, CI
> debugging, or a near-miss in version control.

---

## Sessions 5 + 6 (v0.2.0 — CoordIJK int → int64)

### 1. gcc constant-folds dead branches; Apple clang doesn't.

**What happened.** Sessions 1-4 were green on macOS for the entire MVP
campaign. The first push to CI after Phase E showed `H3_SET_DIGIT_AT_RES`
failing the build on Linux gcc with `-Werror=shift-count-negative`.

**Mechanism.** Callers passing a literal ext-res argument (e.g.
`testValidationExt.c:223 H3_SET_DIGIT_AT_RES(h, 18, INVALID_DIGIT)`)
trigger gcc to *statically* expand both branches of the dispatch macro,
even the unreachable stock branch. The dead `(MAX_H3_RES - 18) * 3 = -9`
becomes a compile-time `-Werror=shift-count-negative`. Apple clang does
not run this static check at literal-arg sites.

**Fix.** Mask the stock-branch `res` argument with `& MAX_H3_RES`.
Identity for `res ∈ [0, 15]`; unreachable at runtime for `res > 15`.

**Lesson.** When a dispatch macro takes a runtime-or-literal argument,
make sure the dead branch is *also* well-formed for every literal a
caller might pass. Cross-platform CI is non-negotiable; "macOS green"
is not "ALL green."

**Artifacts.** Commit `1fa4db7c`, `SESSION_5_CHECKPOINT.md`.

---

### 2. Widening cascades through three layers — each insufficient alone.

**Layers** (CoordIJK int32 → int64):

1. **Struct.** `CoordIJK { int → int64_t }`.
2. **Tables.** `maxDimByCIIres` / `unitScaleByCIIres` length 21 → 23,
   element type `int → int64_t`.
3. **Inner arithmetic.** `_upAp7 / _upAp7r / _upAp7Checked / _upAp7rChecked`
   `int i, j` locals; `_ijkNormalize`; `_ijkToHex2d`; `_hex2dToCoordIJK`;
   `INT32_MAX_3 → INT64_MAX_3`.

**Why each alone fails.** Layer 1 alone: `int i = ijk->i - ijk->k;` in
`_upAp7` silently narrows. Layer 2 alone: struct fields still int32.
Layer 1+2 alone: inner-arithmetic locals still int32; UBSAN trips at
res 22.

**Lesson.** When widening a primitive type through a function tree,
identify *every* layer that mediates the value: storage, lookup
tables, intermediate locals. Walking a callgraph one site at a time
misses silent narrowing patterns. Search for `int [a-z]+ = .*->[ijk]`
to surface the layer-3 traps.

**Artifacts.** Commits `f84ab171`, `5fbdcf4e`.

---

### 3. POC-5 predicted the failure boundary exactly.

**Prediction.** Hexagon first-broken at res 21; pentagon non-monotonic
(res 19 trips, 20 recovers, 21+ trips); failure sites at coordijk.h
`_upAp7` line 487 and `_upAp7r` lines 488, 504, 505.

**Reality.** `testCoordIjkExtRes` UBSAN tripped at coordijk.h:487 with
the exact non-monotonic pentagon pattern. CO-G2 closed 247/500
pre-widening — within sampling noise of the predicted ~50% failure
rate.

**Lesson.** Analytic POCs are cheap (single-file C99,
`__builtin_*_overflow` for static prediction) and worth the time before
modifying core source. They scope the widening, predict UBSAN sites,
and validate post-widening behavior matches.

**Artifacts.** `poc5_coordijk_overflow.c`, `poc5.md`.

---

### 4. Pentagon overage path is non-monotonic across resolutions.

**Observation.** Pre-widening: pentagon path tripped at res 19,
recovered at res 20, tripped again at res 21+.

**Mechanism.** `_adjustPentVertOverage` cross-term `3 * maxDim *
sqrt(7)` is the bottleneck. Class III resolutions (odd res — 19, 21)
amplify by `sqrt(7) ≈ 2.65`; Class II (even res — 20, 22) do not. So
the int32 boundary depends on the table value at the specific res, not
just the depth of recursion.

**Lesson.** When widening, never assume monotonic failure. Test every
res in the affected range, not just the boundary res.

**Artifacts.** Test `CO-G4` covers 19+ explicitly, not just 21+.

---

### 5. Hidden parameter-cascade dependencies (`_setIJK`, `_ijkScale`).

**What happened.** After widening tables to int64 and adding the test,
build was clean and ctest green. But code review showed `_setIJK` was
still declared `void _setIJK(CoordIJK *, int, int, int)` while called
with int64 `maxDim` arguments — silent narrowing at the call site.

**Lesson.** When you widen the *output* type of a producer (a table,
a getter), audit every consumer's parameter signature. Compilers
silently narrow at call boundaries; only static review catches it.

**Artifacts.** `_setIJK` / `_ijkScale` parameter widening in commit
`f84ab171`.

---

### 6. `gh` keychain auth expires silently — use `.env` PAT.

**Workflow.** macOS `gh auth status` reported authenticated against
`uber/h3` (the upstream). Calls against `devopulence/amalfi-h3-128`
returned HTTP 401.

**Fix.** Use the `GITHUB_PAT` environment variable from `.env`:

```bash
set -a && source .env && set +a
GH_TOKEN="$GITHUB_PAT" gh run list -R devopulence/amalfi-h3-128 ...
```

**Lesson.** Run `gh auth status` early in any session that needs
GitHub API access. Don't trust the keychain default.

---

### 7. `git add -A` is dangerous in this repo.

**Near-miss.** Session 6 staging used `git add -A` once; this would
have committed `.env` (containing `GITHUB_PAT`). Caught by reading
`git status` before commit; reset, re-staged with named files only.

**Lesson.** Every commit in this repo must use named-file `git add`.
Never `-A`, never `.`. Untracked files are deliberate (`.env`,
`contexts/*.md`, `initial-main-event-prompt.md`).

---

## Session 7 (post-MVP cleanup)

### 8. `_ipow` had a latent dead-store overflow exposed by ext res.

**Mechanism.** The classic exponentiation-by-squaring loop does
`base *= base` unconditionally at every iteration end, including
the final iteration whose squared value is never used. For stock
H3 (`MAX_H3_RES = 15`), the worst case `_ipow(7, 15) = 4.7e12`
fits int64 with room to spare. For ext res, callers can request
`_ipow(7, 22)`, where the trailing dead store computes
`7^32 ≈ 1.1e27` — UBSAN signed integer overflow.

**Fix.** One-line guard: `if (exp) base *= base;` skips the dead
square on the loop's final iteration.

**Lesson.** Latent UB in stock paths can stay invisible for years.
Ext-res adds 7 more bits to the magnitude domain; that's enough
to surface bugs that have always been technically wrong but
benign. Treat every "extends the domain" widening as an
opportunity to surface latent overflow.

**Artifact.** Commit `ee57e69a` (mathExtensions.c).

---

### 9. `H3_GET_RESOLUTION` is silently wrong for ext children.

**Mechanism.** `cellToChildPos` originally read
`H3_GET_RESOLUTION(child)` to walk digit positions. For ext children
(res 16-22), this returns the *stock-res field* (`res & 0xF` after
the ext flag adjustment) — silently wrong, no compile error, no
runtime fault. The function would walk the wrong number of digits
and return a bogus position.

**Lesson.** Any post-MVP widening of a function that takes an
H3Index argument must audit *every* `H3_GET_RESOLUTION` call. The
correct dispatching getter is `H3_GET_EFFECTIVE_RESOLUTION`. There
is no compile-time signal — only behavioral test coverage at ext
res surfaces the bug.

**Artifact.** Commit `ee57e69a` (h3Index.c:1594).

---

## Proposed audit rules (post-v0.2.0)

These are candidates for `audit.py` additions. Each is a `grep` rule
that would have caught one or more of the lessons above.

### Rule R-NoIntFromIjkField (catches lesson 2 layer 3)

**Detect.** `int [a-z_]+ = [^;]*->[ijk]` patterns in
`src/h3lib/include/coordijk.h` and `src/h3lib/lib/coordijk.c`.

**Why.** Captures the silent-narrowing trap where an `int` local
captures an `int64_t` struct field. Any new code in this file that
introduces such a pattern would break ext-res arithmetic.

**Implementation.**
```python
def check_rule_no_int_from_ijk_field(report):
    pattern = re.compile(r"\bint\s+\w+\s*=\s*[^;]*->[ijk]\b")
    for path in [REPO_ROOT / "src/h3lib/include/coordijk.h",
                 REPO_ROOT / "src/h3lib/lib/coordijk.c"]:
        ...
```

---

### Rule R-NoStockResInWidened (catches lesson 9)

**Detect.** `H3_GET_RESOLUTION(` calls in functions known to be widened
for ext (a curated list — `cellToParent`, `cellToChildren`,
`cellToCenterChild`, `cellToChildPos`, `childPosToCell`, etc.).

**Why.** Easy to forget when adding new ext-aware code; no compile
error; ctest may pass if test coverage doesn't probe ext res.

**Implementation.** Static list of "widened functions" + grep for
`H3_GET_RESOLUTION` inside their function bodies.

---

### Rule R-NoUnboundedIpowAtExtRes (catches lesson 8)

**Detect.** Calls to `_ipow(7, n)` where `n` could exceed 15 at
runtime in functions reachable from ext-res code paths.

**Why.** _ipow's dead-store fix is now in tree, but the pattern
"trailing dead store after the last useful loop iteration" exists in
other math helpers (e.g. iterated multiplication). Generic catch
would help.

**Implementation.** Less mechanical than R-NoIntFromIjkField; may
need manual annotation rather than pure grep.

---

## Notes on what *not* to make a rule

- **The H4 macro shift bug** (lesson 1) was already structural — once
  fixed it's locked in. New code following the existing macro pattern
  inherits the fix. No rule needed.
- **The pentagon non-monotonic observation** (lesson 4) is captured
  in test CO-G4. New widenings that affect overage will be caught by
  that test if they regress.
- **`git add -A` discipline** (lesson 7) is workflow, not source. A
  pre-commit hook *could* refuse to commit `.env` directly, but the
  current pre-commit hook is reserved for the build+ctest gate
  (CLAUDE.md non-negotiable #4). Don't dilute it.

---

*Maintained as part of Session 7. Append new lessons here as they
emerge from future widening work.*
