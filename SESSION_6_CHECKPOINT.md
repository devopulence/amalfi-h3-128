# Session 6 Checkpoint — v0.2.0 CoordIJK int64 widening complete

> **Date:** 2026-05-05 (continued)
> **Branch:** `feat/h3-128-mvp`
> **Predecessor:** SESSION_5_CHECKPOINT.md (HEAD `39ebff5c`, CI all-green)

---

## v0.2.0 — CoordIJK int32 → int64 widening (4 commits)

POC-5's predicted failure boundary (hexagon at res 21+, pentagon at
res 19+) is now CLEARED. The widening landed across 4 surgical
commits, each with full ctest under ASAN+UBSAN before the next one
started.

### Commit chain

| Hash | Subject | Tests |
|------|---------|-------|
| `57672bc6` | (Session 5) widen CoordIJK fields int → int64_t | 326/326 |
| `f84ab171` | widen maxDim/unitScale tables → int64_t[23] + locals | 326/326 |
| `5fbdcf4e` | widen inner arithmetic + testCoordIjkExtRes (CO-G1..G6) | **327/327** |
| `36a5b36d` | audit: W64 mutations for CoordIJK + table widening | 327/327 |

### testCoordIjkExtRes (new, +1 to ctest count)

6 TEST blocks proving POC-5's failure boundary is now the post-widening
pass region:

| Gate | Subject | Result |
|------|---------|--------|
| CO-G1 | 500 res 20 (Class II) round-trips bit-identical | PASS |
| CO-G2 | 500 res 21 (Class III) round-trips bit-identical | PASS (was 247/500 pre-widening) |
| CO-G3 | 500 res 22 (Class II) round-trips bit-identical | PASS (was UBSAN-trip pre-widening) |
| CO-G4 | pentagon res 19+ getPentagons clean | PASS |
| CO-G5 | stock res 0-15 byte-identity preserved | PASS |
| CO-G6 | 3 POC-5 geos × res 20-22 round-trip | PASS (9/9) |

### Sites widened in this session

**Tables (faceijk.c)**:
- `maxDimByCIIres[]` length 21 → 23, element `int → int64_t`. New entries: `[21]=-1` (Class III), `[22]=2*7^11=3,954,653,486`.
- `unitScaleByCIIres[]` length 21 → 23, element `int → int64_t`. New entries: `[21]=-1`, `[22]=7^11=1,977,326,743`.

**Consumers (faceijk.c)**:
- `_adjustOverageClassII` `int maxDim, unitScale` locals → `int64_t`.
- `_faceIjkPentToVerts` overage Vec2d edge bounds: 2 sites widened.
- Vec2d initializers gain explicit `(double)maxDim` casts.

**CoordIJK helpers (coordijk.h)**:
- All 4 `_upAp7 / _upAp7r / _upAp7Checked / _upAp7rChecked`: `int i, j` locals → `int64_t`; `(int)lround` → `(int64_t)lround`.
- `_ijkNormalize` `int min` → `int64_t`.
- `_ijkNormalizeCouldOverflow` `int max, min` → `int64_t`; `ADD/SUB_INT32S_OVERFLOWS` replaced with `__builtin_*_overflow`.
- `_ijkToHex2d` `int i, j` → `int64_t` with explicit `(double)` casts.
- `_hex2dToCoordIJK` `int m1, m2` → `int64_t`; `axisi/diff` widened.
- `INT32_MAX_3` → `INT64_MAX_3` (now ~3.07e18 — effectively unreachable for H3 magnitudes but kept for defense).
- `_setIJK` parameters `int → int64_t`.
- `_ijkScale` factor parameter `int → int64_t`.
- `ijkDistance` return type `int → int64_t`; `abs()` → `llabs()`.

**Audit (.claude/skills/h3-128-audit/mutations/manifest.json)**:
- Added 2 `W64` mutations covering `CoordIJK` field widening and `maxDimByCIIres` table widening.

### Stock test contract update (1 file)

Per CLAUDE.md non-negotiable #2 ("if compatibility must break, STOP and surface"):

- `src/apps/testapps/testCoordIjkInternal.c:54-94` — `_upAp7Checked` / `_upAp7rChecked` overflow guard tests. `INT32_MAX → INT64_MAX` and `INT32_MAX/N → INT64_MAX/N`. Test intent ("i + i overflows", "i * 3 overflows", etc.) preserved at the new int64 bound. The previous int32 behavior is the OLD contract; the int64 widening necessarily changes what counts as overflow.

### Verification (all green)

- ctest **327/327 PASS** under ASAN+UBSAN at HEAD `36a5b36d` (~575s wall).
- POC-5 still **85/85 PASS** (analytic, type-agnostic — boundary it predicts is now corrected in lib).
- audit.py: 24 sites classified, 0 CRITICAL, 0 FINDING.
- pre-commit hook ran full ctest on every commit.

### Architectural notes

1. **POC-5 was the right preflight.** Its analytic boundary
   prediction matched the actual ctest result EXACTLY: hexagon
   first-broken at res 21, pentagon first-broken at res 19. The
   predicted overflow sites in coordijk.h (`_upAp7` line 487, etc.)
   were exactly where UBSAN tripped first.

2. **Widening cascaded through 3 layers** (struct → table → inner
   arithmetic). Each layer alone was insufficient. Catching the
   first 2 layers in v0.1.0 would have been ineffective because
   the `int i = ijk->i - ijk->k;` patterns in `_upAp7Checked`
   kept truncating regardless. Inner-arithmetic locals had to be
   widened in lock-step.

3. **Stock test contract update is a one-liner change but a
   semantic shift.** `INT32_MAX → INT64_MAX` codifies that the
   library's safe arithmetic range is now ~4.3 billion times
   larger. This is THE point of v0.2.0.

4. **`__builtin_*_overflow` is type-generic.** Replaced
   `ADD_INT32S_OVERFLOWS` / `SUB_INT32S_OVERFLOWS` with the
   builtin equivalents in coordijk.h to avoid having to add
   parallel `INT64S` helpers for one-shot use. The original
   helpers in mathExtensions.h remain unchanged for any stock
   callers.

5. **Pentagon non-monotonic boundary (POC-5 §"CO-30") confirmed**:
   pre-widening, pentagon path tripped at res 19, recovered at 20,
   tripped again at 21+. Post-widening, all res 16-22 work. The
   non-monotonic prediction was correct and motivated CO-G4
   covering the full range, not just res 19.

### Architectural deferrals carried forward

| Item | Status |
|------|--------|
| `cellToChildPos` / `childPosToCell` widening (h3Index.c:1676) | Still `deferred:post-mvp` per user direction |
| Compaction widening | Out of v0.1.0 scope per §11; v0.2.0 did not include |
| Polyfill widening | Out of v0.1.0 scope per §11; v0.2.0 did not include |
| Edges, vertices widening | Out of v0.1.0 scope per §11; v0.2.0 did not include |
| Auxiliary stat helpers | Out of v0.1.0 scope per §11; need ext-resolution constants |
| B2 MSVC `#error` path | Cannot be exercised on macOS arm64; deferred |

### Next session entry points

1. **Tag v0.2.0** — once CI confirms the v0.2.0 commits green on
   Linux, create annotated tag `v0.2.0-coordijk-int64`. The
   widening unblocks res 20-22 round-trips and pentagon res 19+
   overage; foundation for cm-precision spatial workloads.

2. **`cellToChildPos / childPosToCell`** — single Rule LB widening
   commit at h3Index.c:1676 + regression test. Currently
   `deferred:post-mvp` in audit. User direction: separate post-MVP
   commit.

3. **Empirical libm tolerance calibration** — first cross-platform
   Linux CI run was Session 5. Now we have post-widening data;
   compare lat/lng tolerances on glibc vs Apple libm at res 22 to
   recalibrate `latlng_within_tolerance` placeholders.

4. **Move POC source files to tracked history** — `poc1*.{c,md}` ..
   `poc4*.{c,md,h}` and `initial-main-event-prompt.md` are still
   untracked. POC-5 IS now tracked. User decision: bundle older
   POCs into a single retroactive commit (`docs(pocs): track POC
   source artifacts`)?

---

## Session 6 commits

| Hash | Subject |
|------|---------|
| `f84ab171` | v0.2.0(faceijk): widen maxDim/unitScale tables → int64_t[23] + locals |
| `5fbdcf4e` | v0.2.0(coordijk): widen inner arithmetic + testCoordIjkExtRes |
| `36a5b36d` | audit(v0.2.0): add W64 mutations for CoordIJK + maxDimByCIIres |
| `<this>` | docs(session-6): checkpoint + Session 7 entry points |

ctest progression: 326/326 (start) → 326/326 → 327/327 (+testCoordIjkExtRes) → 327/327. POC-5 85/85 throughout.
