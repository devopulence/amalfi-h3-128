# H3-Extended (128-bit) Playbook — v4.1.0

> **Status:** Strategic roadmap. Companion to the H3 v4.4.1 source code already in this repository.
> **Pairs with:** `CLAUDE.md` v2.0.0 (guardrails) and `.claude/skills/h3-128-audit/` (validator — Phase G deliverable, does not exist yet).
> **Audience:** A Claude Code session at `/Users/johndesposito/amalfi_work/amalfi-h3-128/` that has direct file access to the entire H3 source tree.
> **Scope:** What to change, why, in what order, and what to watch for. Source line numbers, function names, and signatures are NOT pinned here — `grep` and `Read` are the source of truth.

---

## How to Use This Document

This playbook is a **roadmap, not a tutorial**. It tells you the strategic shape of the work — bit layout, new macros, phase order, categorical widening rules, trap catalog, FFI contract, test gates. Concrete line numbers, current function bodies, call chains, and stock macro definitions live in the source. When this playbook says "every function in `h3Index.c` that reads `H3_GET_RESOLUTION` as a loop bound," you `grep` to find them. The audit script at `.claude/skills/h3-128-audit/` is the structural validator; `ctest` is the final arbiter.

| Question | Where to look |
|----------|---------------|
| Is X currently in the source? | `grep` / `Read` the source tree |
| What pattern of change does X need? | This playbook (§5) |
| Why does that pattern apply? | This playbook (§3, §6) |
| Did I miss a site? | `.claude/skills/h3-128-audit/audit.py` |
| Did I break stock behavior? | `ctest --output-on-failure` |

---

## §0 — Mandatory Build Discipline (NON-NEGOTIABLE)

Build discipline is enforced in `CLAUDE.md` Non-Negotiable #4 and is **not optional**. Summary:

- **Sanitizers always on** (`-fsanitize=address,undefined -Werror -Wall -Wextra`) in `build-dev/`. Catch the moment any test trips a UB shift.
- **Pre-commit hook** runs the full `ctest` suite. No commit lands red.
- **CI matrix** on every push: `{ubuntu-latest, macos-latest} × {release, sanitizers}`. PRs block on red.
- **Granular commits**: one widening site = one commit. Bisectability is the point.
- **Tests land with the code change they exercise.** No batched end-of-phase test commits.
- **No `--no-verify`.** Bypassing the hook defeats the system.
- **316/316 stock baseline never goes red.** A red stock test is a regression — revert and rethink.

The full setup commands (CMake invocation, hook script, `ci.yml`) are inline in `CLAUDE.md` as a copy-paste block. Run them as the FIRST commit on `feat/h3-128-mvp` before any widening work.

**Why this is §0:** the spec's role drops from "perfectly specify every line" to "tell the implementer what category of change to make." The build catches the implementation mistakes. ASAN+UBSAN+`-Werror` plus 316 ctest gates eliminate >90% of widening errors at edit time. This shift is what makes a 30 KB strategic playbook viable.

---

## §1 — Mission and Non-Negotiables

**Mission.** Widen `uber/h3` from 64-bit to 128-bit indexes, unlocking resolutions 16–22 (~1 cm edge to ~0.4 mm). This is foundational IP for the Amalfi Intelligence centimeter-scale spatial digital twin. Every defect, measurement, and regression signal will be keyed to a hex cell at this resolution.

**Non-negotiables** (full text in `CLAUDE.md`):

1. **Accuracy > speed.** ~2–5× slowdown vs. native 64-bit H3 is acceptable. Optimize after correctness.
2. **Bit layout is a strict superset of 64-bit.** For res 0–15, every 128-bit cell ID is **byte-identical** to the stock cell ID with the high 64 bits zeroed. See §2.
3. **All existing tests must pass byte-identically** (`ctest`: 316/316 on stock).
4. **Mandatory build discipline** — see §0.

If any of these conflict with an apparent need, **STOP and surface the issue**. Do not silently break compatibility.

---

## §2 — Bit Layout (Bits 64–127)

```
Bits 0–44   : digits 1–15           (3 bits each — UNCHANGED from stock)
Bits 45–51  : base cell             (7 bits — UNCHANGED)
Bits 52–55  : resolution low 4 bits (4 bits — see §2.1 below)
Bits 56–58  : reserved (must be 0)  (UNCHANGED)
Bits 59–62  : mode                  (UNCHANGED)
Bit  63     : high bit (must be 0)  (UNCHANGED)
Bit  64     : EXT FLAG              (NEW — 0 = stock cell, 1 = ext cell)
Bits 65–85  : digits 16–22          (NEW — 7 digits × 3 bits each)
Bits 86–127 : reserved              (NEW — must be 0 in MVP)
```

### §2.1 Resolution Encoding — the load-bearing decision

The 4-bit stock-res field (bits 52–55) cannot represent values 16–22 directly. The encoding is:

| `EXT_FLAG` (bit 64) | Stock-res field (bits 52–55) | Effective resolution |
|---------------------|------------------------------|----------------------|
| 0                   | r                            | r (0–15)             |
| 1                   | r                            | r + 16 (16–22, so stored r ∈ 0–6) |

Decoder formula (Group C, §3):

```
effective_res(h) = H3_GET_RESOLUTION(h) + (H3_GET_EXT_FLAG(h) * 16)
```

**Why this encoding.**

- A stock 64-bit cell zero-extended to 128 bits is automatically valid: bit 64 = 0, stock res field = 0–15, effective_res = stock res. Non-negotiable #2 satisfied.
- The dispatch signal (bit 64) is a single-bit test, register-cheap, ABI-stable, and survives byte ordering.
- Stock macros (`H3_GET_RESOLUTION`, `H3_GET_INDEX_DIGIT`, etc.) read bits 0–63 only and remain valid C99 expressions on a 128-bit type after the typedef widens (the cast is no-op for the low half).
- Stock macros applied to an ext cell return *misleading* values (e.g. `H3_GET_RESOLUTION` on a res 18 cell returns 2). This is a feature: it forces every code path that may receive an ext cell to use Group C dispatch (`H3_GET_EFFECTIVE_RESOLUTION`, `H3_GET_DIGIT_AT_RES`) instead of stock macros. The audit catches sites that don't.

### §2.2 Sentinel digit value

Stock H3 reserves digit value `7` (`INVALID_DIGIT`) as the "this digit is beyond the cell's resolution" sentinel. The same convention extends to ext digits 16–22: any ext digit at position ≥ effective_res must be `7`. `H3_INIT_EXT` (§3) initializes both stock and ext digit positions to 7.

### §2.3 Reserved bits 86–127

In MVP these must be **0**. Future use (e.g. timestamp, owner tag, trajectory ID) is out of scope. The audit (§8 G3) enforces zeroing.

---

## §3 — Group C Macros

Stock H3 has two macro groups in `h3Index.h`:

- **Group A** — bit-position constants (`H3_MAX_OFFSET`, `H3_RES_OFFSET`, `H3_BC_OFFSET`, `H3_DIGIT_MASK`, etc.)
- **Group B** — getters/setters that read/write Group A positions (`H3_GET_RESOLUTION`, `H3_SET_INDEX_DIGIT`, etc.)

**Group C is new** — macros that reach into bits 64–127 OR dispatch correctly across stock/ext. Group A and Group B stay verbatim. Group C is added to `h3Index.h` (or, optionally, `h3IndexExt.h`).

| # | Macro | Type | Purpose |
|---|-------|------|---------|
| 1 | `MAX_H3_EXT_RES` | constant `int` = `22` | Upper bound for resolution. Add adjacent to `MAX_H3_RES` in `constants.h`. |
| 2 | `H3_EXT_FLAG_OFFSET` | constant `= 64` | Bit position of ext flag. |
| 3 | `H3_EXT_FLAG_MASK` | constant `= ((__uint128_t)1 << 64)` | Single-bit mask. |
| 4 | `H3_EXT_DIGITS_OFFSET` | constant `= 65` | Bit position of digit 16. |
| 5 | `H3_EXT_DIGITS_MASK` | constant `= ((((__uint128_t)1 << 21) - 1) << 65)` | All 7 ext-digit slots set. Outer parens mandatory for safe macro expansion in arbitrary expression contexts. |
| 6 | `H3_INIT_EXT` | constant `H3Index` | Init pattern with bit 64 set, bits 65–85 all `1` (ext digits = 7), bits 86–127 zero, low 64 bits = `H3_INIT`. Used by ext-aware `setH3Index` analog. |
| 7 | `H3_GET_EXT_FLAG(h)` | macro → `int` | Returns 0 or 1. `(int)(((h) & H3_EXT_FLAG_MASK) >> H3_EXT_FLAG_OFFSET)`. |
| 8 | `H3_SET_EXT_FLAG(h, v)` | macro | Sets bit 64 to `v`. Mirrors `H3_SET_HIGH_BIT` style. |
| 9 | `H3_GET_EFFECTIVE_RESOLUTION(h)` | macro → `int` | `(H3_GET_RESOLUTION(h) + (H3_GET_EXT_FLAG(h) << 4))`. Returns 0–22. |
| 10 | `H3_SET_EFFECTIVE_RESOLUTION(h, res)` | macro | Atomically sets stock-res field to `(res) & 0xF` AND ext flag to `(res) >= 16`. Handles flag-flip in a single expression so callers never write half-state. **Implementation must evaluate `res` exactly once** — use a statement-expression or require callers to pass a side-effect-free integer expression (no function calls or post-increments). |
| 11 | `H3_GET_EXT_INDEX_DIGIT(h, res)` | macro → `Direction` | For `res ∈ [16, 22]`: extract 3 bits at offset `H3_EXT_DIGITS_OFFSET + (res - 16) * 3`. **UB if res < 16 or res > 22.** Caller's responsibility (use Group C #13 if range unknown). |
| 12 | `H3_SET_EXT_INDEX_DIGIT(h, res, digit)` | macro | Mirror of #11 for write. UB if res out of range. |
| 13 | `H3_GET_DIGIT_AT_RES(h, res)` | macro → `Direction` | Dispatching getter. For `res ∈ [1, 15]` → `H3_GET_INDEX_DIGIT(h, res)`. For `res ∈ [16, 22]` → `H3_GET_EXT_INDEX_DIGIT(h, res)`. Implementation: `((res) <= MAX_H3_RES ? H3_GET_INDEX_DIGIT(h, res) : H3_GET_EXT_INDEX_DIGIT(h, res))`. |
| 14 | `H3_SET_DIGIT_AT_RES(h, res, digit)` | macro | Dispatching setter. Same dispatch as #13 over `H3_SET_INDEX_DIGIT` / `H3_SET_EXT_INDEX_DIGIT`. **This is the macro that replaces every `H3_SET_INDEX_DIGIT` site whose `res` argument can exceed 15 at runtime.** |

**Naming convention.** All Group C identifiers carry an explicit `EXT_` infix or `_AT_RES` / `_EFFECTIVE_` suffix to make the dispatch visible at the call site.

**`H3_SET_EFFECTIVE_RESOLUTION` implementation note.** This macro MUST use a GCC/Clang statement-expression `({...})` for single-evaluation of `res`. Do NOT use a comma-expression (evaluates `res` twice — bugs with side-effect arguments). Since `__SIZEOF_INT128__` is already required (§9.1), statement-expressions are available. POC-1 test PF-19 validates this with `++counter`.

**Encoding-formula hazard.** `H3_GET_INDEX_DIGIT` uses `(MAX_H3_RES - res) * H3_PER_DIGIT_OFFSET` as the shift count. This is non-negative for `res ∈ [0, 15]` (stock) but goes **negative** for `res ∈ [16, 22]`. Negative shift counts are undefined behavior in C. Calling stock `H3_GET_INDEX_DIGIT(h, 18)` is UB even if `MAX_H3_RES` is bumped — bumping `MAX_H3_RES` is forbidden by non-negotiable #2 anyway. Hence Group C #11/#12 use a **separate offset formula** anchored at `H3_EXT_DIGITS_OFFSET`, not at `H3_MAX_OFFSET - MAX_H3_RES * 3`.

---

## §4 — Phase Order and Dependencies

Eight phases, lettered A–H. Each phase has explicit upstream deps; later phases assume earlier phases compile cleanly with green ctest.

```
A — Type widening               (typedef + Group C macros + constants)
    ↓
B — Build hygiene               (CMakeLists, format/byte-order asserts, libm, MSVC drop)
    ↓
C — String I/O                  (stringToH3, h3ToString)
    ↓
D — Encode/decode + hierarchy   (the body of work — see §4.1)
    ↓
E — Validation widening         (isValidCell + bit-magic predicates)
    ↓
F — Auxiliary surfaces          (boundary, faces, IcoFaces, getPentagons, isResClassIII)
    ↓
G — Audits                      (line-by-line ext coverage, mutation tests)
    ↓
H — Acceptance                  (full ctest + ext-suite + sanitizer + cross-platform CI)
```

### §4.1 Phase D sub-phases

Phase D is the bulk of the widening work and is itself ordered:

| Sub-phase | Scope |
|-----------|-------|
| D1 | Encoder: `latLngToCell`, `vec3ToCell`, `_faceIjkToH3`, helper `setH3Index` |
| D2 | Decoder: `cellToLatLng`, `cellToVec3`, `_h3ToFaceIjk`, `_h3ToFaceIjkWithInitializedFijk` |
| D3 | Public accessors: `getResolution` upgrade to effective-res, `getIndexDigit`, `constructCell`, `getBaseCellNumber` |
| D4 | Hierarchy: `cellToParent`, `cellToChildren`, `cellToCenterChild`, `cellToChildrenSize`, `cellToChildPos`, `childPosToCell`, `_hasChildAtRes`, `makeDirectChild`, `_zeroIndexDigits`, plus iterators in `iterators.c` |
| D5 | localij surface: `cellToLocalIj`, `localIjToCell`, `gridDistance`, `gridPathCells`, plus internal helpers `cellToLocalIjk`, `localIjkToCell` |
| D6 | Rotation: `_h3Rotate60ccw`, `_h3Rotate60cw`, `_h3RotatePent60ccw`, `_h3RotatePent60cw`, `_h3LeadingNonZeroDigit` |
| D7 | FFI shim layer (see §7) |

**Compaction is DEFERRED.** `compactCells`, `uncompactCells`, `uncompactCellsSize` are NOT in MVP. They make heavy use of `H3_GET_RESERVED_BITS` for hash-marker book-keeping and need a careful redesign for ext cells; deferring them keeps Phase D scope finite. Mark them with an `H3_NOT_IMPLEMENTED_FOR_EXT` guard (return `E_FAILED` or a new error code) when called with any ext input.

### §4.2 Why this order

- **A before B** because the typedef change cascades through every header; build hygiene shakes out the noise.
- **C before D** because string round-trip is a cheap end-to-end test that proves bit packing at the boundary.
- **D1–D2 before D3–D7** because encode/decode is the foundation; without it nothing else can be tested.
- **E before F** because validation gates everything downstream — a wrong `isValidCell` masks real bugs in F.
- **G after F** because the audit scans the *implementation*, not the *spec*; runs only after code is in place.
- **H last** because acceptance is full-suite plus cross-platform plus sanitizer, the most expensive run.

### §4.3 Function inventory by file

You `grep` to enumerate. Approximate scale (subject to the actual source):

```bash
grep -c '^H3_EXPORT\|^H3Index\|^H3Error\|^void\|^int\|^Direction\|^bool\|^static' \
    src/h3lib/lib/h3Index.c src/h3lib/lib/localij.c src/h3lib/lib/iterators.c
```

The MVP touches roughly:
- `h3Index.c` — encoder + decoder + hierarchy + accessors + validation + rotation (most functions, minus compaction)
- `localij.c` — 4 public + 2 internal helpers
- `iterators.c` — 6 functions (all of them participate in `cellToChildren` widening)
- Auxiliary `.c` files (algos, directedEdge, latLng, polyfill, vertex) — touched mostly via the type-widening cascade plus a few explicit guard-relaxation sites

Run the audit (§8 G1) to confirm no widening site is missed.

---

## §5 — Categorized Widening Rules

The five rules below are the **mechanical edit patterns**. Apply by category, not by function. The audit (§8 G2) verifies coverage.

### §5.1 Rule LB — Loop-Bound Widening

**Pattern in source.** A loop reads `H3_GET_RESOLUTION(h)` as its iteration limit, or compares a per-iteration `r` against `MAX_H3_RES`.

**Symptom of breakage in 128-bit.** Loop terminates at 15 even when the cell has digits 16–22.

**Edit.** Replace `H3_GET_RESOLUTION(h)` with `H3_GET_EFFECTIVE_RESOLUTION(h)` at the loop bound. Replace `MAX_H3_RES` with `MAX_H3_EXT_RES` in the bound comparison.

**Detection.** `grep -n 'for.*H3_GET_RESOLUTION\|<= *H3_GET_RESOLUTION\|< *MAX_H3_RES' src/h3lib/lib/*.c` — scan each hit and classify.

**Apply unless:** the loop is *intentionally* limited to stock digits (e.g. computing a stock-only sub-result for an explicitly-stock helper). When in doubt, widen and let tests catch incorrect widening.

### §5.2 Rule GR — Guard Relaxation

**Pattern in source.** `if (res < 0 || res > MAX_H3_RES) return E_RES_DOMAIN;` or `if (childRes > MAX_H3_RES) ...` or `if (parentRes > MAX_H3_RES) ...`.

**Symptom of breakage.** Public ext-aware function rejects valid res 16–22 input.

**Edit.** Change `MAX_H3_RES` to `MAX_H3_EXT_RES` in the guard.

**Apply unless:** the guard intentionally rejects ext input because the function isn't ready (deferred path: compaction, polyfill — leave at `MAX_H3_RES` and rely on the caller's effective-res check, OR introduce an explicit `E_RES_NOT_SUPPORTED_EXT` error).

**Sites to scan.** `grep -n 'MAX_H3_RES' src/h3lib/lib/*.c` — many hits in `latLng.c`, `polyfill.c`, `vertex.c`. Each is either a guard (relax) or a static-array dimension (keep — see §5.6).

### §5.3 Rule DW — Digit-Write Widening (and Rule DR — Digit-Read Widening)

**Pattern in source.** `H3_SET_INDEX_DIGIT(h, r, d)` (write) OR `H3_GET_INDEX_DIGIT(h, r)` (read) where `r` is a loop variable that can exceed 15, or where `r = childRes` and `childRes` came from the public API's res arg (now ranges 0–22).

**Symptom of breakage.** Negative shift count → UB. ASAN+UBSAN trips immediately on the first ext test.

**Edit (writes).** Replace `H3_SET_INDEX_DIGIT` with `H3_SET_DIGIT_AT_RES` (Group C #14).

**Edit (reads).** Equally mandatory: replace `H3_GET_INDEX_DIGIT` with `H3_GET_DIGIT_AT_RES` (Group C #13) when the res arg is unknown-range. Read-side UB is the same severity as write-side. `_h3LeadingNonZeroDigit`, `_h3Rotate60ccw/cw`, `_h3RotatePent60ccw/cw` all read in loops — see §6.12 / §6.13.

**Special trap — `cellToParent` loop body.** The current source has:
```c
for (int i = parentRes + 1; i <= childRes; i++) {
    H3_SET_INDEX_DIGIT(parentH, i, H3_DIGIT_MASK);
}
```
When `childRes ≥ 16`, `i` reaches 16+ and `H3_SET_INDEX_DIGIT` does a negative shift. Use `H3_SET_DIGIT_AT_RES` in the loop body. This is the loop body, not the function entry — easy to miss.

**Special trap — iterator `_incrementResDigit`.** In `iterators.c` the increment uses `H3_PER_DIGIT_OFFSET * (MAX_H3_RES - res)` as the shift. Same UB class. Either (a) replace with a Group C `H3_INCREMENT_DIGIT_AT_RES` helper, or (b) inline a branch on `res > 15` that uses `H3_EXT_DIGITS_OFFSET` instead. Option (a) keeps the iterator clean.

### §5.4 Rule RW — Resolution-Write Widening

**Pattern in source.** `H3_SET_RESOLUTION(h, res)` where `res` can be 16–22.

**Symptom of breakage.** Stock `H3_SET_RESOLUTION` masks res to its low 4 bits AND leaves bit 64 (ext flag) untouched. So `H3_SET_RESOLUTION(h, 18)` writes `2` into bits 52–55 and leaves the ext flag whatever it was — produces a half-state cell.

**Edit.** Replace with `H3_SET_EFFECTIVE_RESOLUTION` (Group C #10). This atomically writes both the low-4-bit field AND the ext flag.

**Special trap — `cellToCenterChild`.** Currently calls `H3_SET_RESOLUTION(h, childRes)` *after* `_zeroIndexDigits`. For ext childRes the 4-bit truncation is **accidentally numerically correct** (16 → 0, 17 → 1, …, 22 → 6 — these match the stock-res-field encoding) but the ext flag is **never set**. Result: a malformed cell that looks like a low-res stock cell on inspection but has zeros in its (unused) ext digits. Use `H3_SET_EFFECTIVE_RESOLUTION` instead.

**Special trap — `_iterInitParent`.** Same issue. The iterator's child cell has its res written via `H3_SET_RESOLUTION(iter->h, childRes)`. Switch to `H3_SET_EFFECTIVE_RESOLUTION`.

**Special trap — `localIjkToCell`.** The output cell's res is written via `H3_SET_RESOLUTION(*out, res)`. Same fix.

### §5.5 Rule INIT — `H3_INIT` Ext-Init Pattern

**Pattern in source.** `H3Index h = H3_INIT;` followed by `H3_SET_RESOLUTION(h, res); H3_SET_BASE_CELL(h, baseCell);` and a loop `for (int r = 1; r <= res; r++) H3_SET_INDEX_DIGIT(h, r, initDigit);`.

**Symptom of breakage.** `H3_INIT` initializes only digits 1–15 to sentinel 7. Ext digits 16–22 are zero — the encoded cell is malformed (digits past the resolution must be 7, not 0).

**Edit.** When `res` may be ≥ 16, init from `H3_INIT_EXT` (Group C #6) and use `H3_SET_DIGIT_AT_RES` in the digit loop. When `res` is provably ≤ 15 (stock-only path), keep `H3_INIT`.

**Site.** `setH3Index` (h3Index.c) — public, called by `iterInitBaseCellNum` etc. Also `_faceIjkToH3` (entry point: `H3Index h = H3_INIT;`).

### §5.6 What NOT to widen

- **Static arrays dimensioned with `MAX_H3_RES + 1`** in `polyfill.c` (`MAX_EDGE_LENGTH_RADS`, `NORTH_POLE_CELLS`, `SOUTH_POLE_CELLS`). These are precomputed lookup tables for stock resolutions only. Do NOT extend the array literals to length 23 unless the table values for res 16–22 are known and computed. Polyfill is auxiliary; defer or guard.
- **`MAX_H3_RES` in encoder bit-position formulas inside Group A/B macros.** Bumping `MAX_H3_RES` to 22 silently relocates every digit position for stock cells and breaks non-negotiable #2.
- **Stock-only test fixtures.** Existing tests validate res 0–15 byte-identically; they should not be touched.
- **`H3_GET_RESERVED_BITS` / `H3_SET_RESERVED_BITS`** in `compactCells`. Compaction is deferred.

---

## §6 — Trap Catalog

Sixteen non-obvious traps. Each entry: trap → symptom → fix.

### §6.1 — `_hasGoodTopBits` is a structural break

**Trap.** Source reads `h >>= (64 - 8); return h == 0b00001000;`. This is the validation predicate for "high bits look like a valid stock cell." For a 128-bit `H3Index` type, the right-shift is on 128 bits and lands the *bit-64 ext flag* at position 8 of the residual — colliding with the `0b00001000` check (which is exactly bit 3, but the predicate compares against bit 3 of the byte at offset 56–63 in the 64-bit world).

**Symptom.** All ext cells fail `isValidCell` because the mode/high-bit pattern is not what the predicate expects.

**Fix.** Restructure as two explicit checks, low half and high half:

```c
static inline bool _hasGoodTopBits(H3Index h) {
    uint64_t low = (uint64_t)h;
    uint64_t high = (uint64_t)(h >> 64);
    // Low-half check: stock pattern (mode=cell, reserved=0, high-bit=0)
    low >>= (64 - 8);
    if (low != 0b00001000) return false;
    // High-half check.
    // Permit ext flag (bit 64 → high-half bit 0) + 7 ext digits × 3 bits
    // (bits 65–85 → high-half bits 1–21) = 22 valid bits total.
    // Bits 86–127 (high-half bits 22+) must be zero.
    if (H3_GET_EXT_FLAG(h)) {
        if (high & ~(((uint64_t)1 << 22) - 1)) return false;  // bits 86–127 zero
        // Stock-res field for ext cells must be in [0, 6] (i.e. effective res 16–22)
        // Encoding: (effective_res - 16) in 4-bit field, valid range 0–6.
        if (H3_GET_RESOLUTION(h) > 6) return false;
    } else {
        if (high != 0) return false;  // ext flag clear ⇒ all bit 64+ zero
    }
    return true;
}
```

This is one of the **two CRITICAL audits** in Phase E — see §8 E1.

### §6.2 — `_firstOneIndex` builtin is 64-bit-only

**Trap.** `__builtin_clzll(h)` operates on `unsigned long long` (typically 64 bits). On a 128-bit `H3Index`, the cast-down loses the high half; if the only set bit is in bits 64–127, `__builtin_clzll` is called on `0` — undefined behavior.

**Fix.** Order the check: high half first.

```c
static inline int _firstOneIndex(H3Index h) {
    uint64_t high = (uint64_t)(h >> 64);
    if (high) return 64 + (63 - __builtin_clzll(high));
    uint64_t low = (uint64_t)h;
    if (low) return 63 - __builtin_clzll(low);
    return -1;  // or whatever the contract says for h == 0
}
```

This is the **second CRITICAL audit** in Phase E (§8 E2).

### §6.3 — `_hasAny7UptoRes` covers only digits 1–15

**Trap.** The bit-magic mask `MHI = 0b100100100100100100100100100100100100100100100` has 15 triples — it operates exclusively on bits 0–44. Ext digits in bits 65–85 are never inspected.

**Fix.** Extend the predicate to also check ext digits when `res > 15`:

```c
if (res > MAX_H3_RES) {
    // Reuse the same MHI/MLO trick over bits 65–85, masked to (res - 15) digits
    // ... or loop digits 16..res checking == 7 (loop is fine — only 7 iterations max)
}
```

A loop is acceptable here — at most 7 ext digits — and is much easier to verify than re-deriving the bit-magic for ext bits.

### §6.4 — `_hasAll7AfterRes` early-returns at `res < 15`

**Trap.** `if (res < 15) { ... } return true;` — function returns `true` unconditionally when `res >= 15`. For ext cells with `res ≥ 16`, the digits **past** the effective res must still be 7, but the check is never run.

**Fix.** Widen the gate:

```c
static inline bool _hasAll7AfterRes(H3Index h, int res) {
    int eff_res = H3_GET_EFFECTIVE_RESOLUTION(h);
    // Stock branch (effectively the original code path) when eff_res < 15
    if (eff_res < MAX_H3_RES) { /* unchanged */ }
    // Ext branch: check ext digits past res
    if (eff_res > MAX_H3_RES) {
        for (int r = eff_res + 1; r <= MAX_H3_EXT_RES; r++) {
            if (H3_GET_DIGIT_AT_RES(h, r) != INVALID_DIGIT) return false;
        }
    }
    return true;
}
```

### §6.5 — `_hasDeletedSubsequence` operates on stock window only

**Trap.** `h <<= 19; h >>= 19;` masks to the lower 45 bits (digits 1–15). For ext cells the leading non-zero digit may live in the ext range (bits 65–85). The current predicate misses pentagon-deleted-subsequence violations in ext digits.

**Fix.** Either (a) add a parallel check on the ext-digit window, or (b) walk the digits explicitly via `H3_GET_DIGIT_AT_RES`. Option (b) is clearer for the small digit count (≤ 22).

### §6.6 — `_zeroIndexDigits` UB shift

**Trap.** Inside the function: `m <<= H3_PER_DIGIT_OFFSET * (MAX_H3_RES - end);`. When `end > 15`, the shift count is negative → UB. The common caller (`cellToCenterChild`, see §6.8) passes `start = parent_eff_res + 1` and `end = childRes`, so for a stock parent at res 10 with childRes 22 the range straddles the stock/ext boundary — this is the **typical** case for ext children, not an edge.

**Fix.** Replace the bit-magic body with a small explicit loop using Group C dispatch:

```c
H3Index _zeroIndexDigits(H3Index h, int start, int end) {
    if (start > end) return h;
    for (int r = start; r <= end; r++) {
        H3_SET_DIGIT_AT_RES(h, r, 0);  // dispatches stock vs ext
    }
    return h;
}
```

Bit-magic optimization is deferred. The function is rarely hot-pathed; correctness > microseconds here.

### §6.7 — `cellToParent` loop body UB

**Trap.** See §5.3. `H3_SET_INDEX_DIGIT(parentH, i, H3_DIGIT_MASK)` for `i ∈ [parentRes+1, childRes]` does UB when `i > 15`.

**Fix.** Use `H3_SET_DIGIT_AT_RES` in the loop body. Also relax the `parentRes > MAX_H3_RES` guard at function entry to `MAX_H3_EXT_RES`.

### §6.8 — `cellToCenterChild` ext-flag silently unset

**Trap.** Two collaborating bugs in the function body:
1. `_zeroIndexDigits(h, H3_GET_RESOLUTION(h) + 1, childRes)` — the start arg uses **stock** `H3_GET_RESOLUTION`. For an ext parent at effective res 18, this returns the stock-res field (= 2), and the digit-zero window is wrong by 16.
2. The very next line, `H3_SET_RESOLUTION(h, childRes)` for `childRes ≥ 16`, truncates to 4 bits but leaves the ext flag clear. The result looks like a low-res stock cell.

**Fix.** Both lines:
- Replace the start arg with `H3_GET_EFFECTIVE_RESOLUTION(h) + 1`. Note that `_zeroIndexDigits` itself must straddle the stock/ext boundary per §6.6.
- Replace `H3_SET_RESOLUTION` with `H3_SET_EFFECTIVE_RESOLUTION` (§5.4).

### §6.9 — `cellToChildren` delegates entirely to iterators

**Trap.** The function body is *just* a `for` loop calling `iterInitParent` / `iterStepChild`. There is no per-digit logic to widen in `cellToChildren` itself. The widening lives entirely in `iterators.c`.

**Fix.** Don't try to widen `cellToChildren` directly. Audit `iterators.c` and apply Group C dispatch at every site that captures or reads resolution:

- `_iterInitParent` — the line `iter->_parentRes = H3_GET_RESOLUTION(h);` MUST become `iter->_parentRes = H3_GET_EFFECTIVE_RESOLUTION(h);`. The iterator's `_parentRes` invariant is now an effective-res value (0–22), not a stock-res field.
- `_iterInitParent` — the resolution write `H3_SET_RESOLUTION(iter->h, childRes);` MUST become `H3_SET_EFFECTIVE_RESOLUTION(iter->h, childRes);` per Rule RW.
- `iterStepChild` — the line `int childRes = H3_GET_RESOLUTION(it->h);` MUST become `int childRes = H3_GET_EFFECTIVE_RESOLUTION(it->h);`. Every downstream `_incrementResDigit(it, ...)` then targets the correct digit position.
- `_incrementResDigit` — see §6.10.
- `_getResDigit` — wraps `H3_GET_INDEX_DIGIT(it->h, res)`; must dispatch via `H3_GET_DIGIT_AT_RES` when res can exceed 15.
- `iterInitBaseCellNum` — `childRes > MAX_H3_RES` guard relaxes to `MAX_H3_EXT_RES`.

### §6.10 — `_incrementResDigit` shift formula UB

**Trap.** `val <<= H3_PER_DIGIT_OFFSET * (MAX_H3_RES - res);` — same UB shift class as `_zeroIndexDigits`.

**Fix.** Either dispatch to a Group C `H3_INCREMENT_DIGIT_AT_RES` helper or branch on `res > 15` and use `H3_EXT_DIGITS_OFFSET`.

### §6.11 — `setH3Index` digit-loop misses ext digits

**Trap.** The for-loop runs `r = 1; r <= res; r++` calling `H3_SET_INDEX_DIGIT`. When `res > 15` the loop visits invalid `r` values for stock macro AND leaves ext-digit positions in their `H3_INIT` state (zero — invalid). Must init from `H3_INIT_EXT` and use Group C #14.

**Fix.** See §5.5 (Rule INIT).

### §6.12 — `H3_SET_INDEX_DIGIT` on rotated digit

**Trap.** `_h3Rotate60ccw`, `_h3Rotate60cw`, `_h3RotatePent60ccw`, `_h3RotatePent60cw` rotate every digit. Loops to `H3_GET_RESOLUTION(h)` (stock res!), so they miss ext digits entirely.

**Fix.** Use `H3_GET_EFFECTIVE_RESOLUTION` for the loop bound and `H3_GET_DIGIT_AT_RES` / `H3_SET_DIGIT_AT_RES` in the body. Pentagon rotation logic depends on identifying the leading non-zero digit, which `_h3LeadingNonZeroDigit` also needs widening (loop bound).

### §6.13 — `_h3LeadingNonZeroDigit` loop bound

**Trap.** Loops `r = 1; r <= H3_GET_RESOLUTION(h); r++`. Misses ext digits.

**Fix.** `H3_GET_EFFECTIVE_RESOLUTION` + `H3_GET_DIGIT_AT_RES`.

### §6.14 — `cellToBoundary` and `getIcosahedronFaces` resolution argument

**Trap.** Both functions call `_faceIjk*ToCellBoundary(&fijk, H3_GET_RESOLUTION(h3), ...)` — passing stock resolution. For ext cells this passes the stock-res field (0–6) instead of effective res (16–22), and downstream the `_faceIjk*` functions do per-resolution geometric calculations that need the real res.

**Fix.** Pass `H3_GET_EFFECTIVE_RESOLUTION(h3)`. Then the downstream `_faceIjk*` functions (in `faceijk.c`) need to handle res 16–22 — their math is naturally extensible (just more digits to walk down) but their iteration limits need widening too.

### §6.15 — `localij.c` digit-loop helpers

**Trap.** `cellToLocalIjk` and `localIjkToCell` (with internal helpers) read/write digits via `H3_GET_RESOLUTION` and `H3_SET_INDEX_DIGIT`. Multiple sites per function — see `grep -n 'H3_GET_RESOLUTION\|H3_SET_RESOLUTION\|H3_SET_INDEX_DIGIT' src/h3lib/lib/localij.c`.

**Fix.** Apply Rules LB, RW, DW. Also: the rotation scaffolding (`_downAp7`, `_downAp7r`) is called in a loop that walks res from `res - 1` down to `0`; that loop is correct as-is provided `res` is the *effective* res.

### §6.16 — `H3_INIT` ext-zero traps in encoder

**Trap.** `_faceIjkToH3` opens with `H3Index h = H3_INIT;` then immediately calls `H3_SET_RESOLUTION(h, res)`. Two collaborating bugs:
1. `H3_INIT` sets only digits 1–15 to sentinel 7. For ext output, bits 64+ are zero — ext-digit positions hold valid `CENTER_DIGIT` (0) even for digits past the effective res.
2. `H3_SET_RESOLUTION(h, res)` for `res ≥ 16` truncates to 4 bits and leaves the ext flag clear (Rule RW).

**Fix.** Both lines:
- Branch at function entry on `res > MAX_H3_RES`: init from `H3_INIT_EXT` (ext path) or `H3_INIT` (stock path).
- Replace `H3_SET_RESOLUTION` with `H3_SET_EFFECTIVE_RESOLUTION` so the ext flag is set atomically with the low-4-bit res field.

### §6.17 — `cellToChildrenSize` and `makeDirectChild` arithmetic on stock res

**Trap.** Both functions read `H3_GET_RESOLUTION(h)` and feed the result into arithmetic, not a loop bound:
- `cellToChildrenSize`: `n = childRes - H3_GET_RESOLUTION(h)`, then `_ipow(7, n)`. For an ext parent at effective res 18 with childRes 22, this computes `22 - 2 = 20` instead of `4`. Returns ~`7^20` instead of `7^4`. Allocation explodes.
- `makeDirectChild`: `int childRes = H3_GET_RESOLUTION(h) + 1;`. For an ext parent at effective res 18, this computes `3` instead of `19`. Returns a malformed cell.

**Fix.** Both sites use `H3_GET_EFFECTIVE_RESOLUTION` instead of `H3_GET_RESOLUTION`. **Generalize Rule LB**: any arithmetic expression on `H3_GET_RESOLUTION(h)` — not just loop bounds — needs the effective-res variant when `h` may be ext.

`makeDirectChild` is a one-line function; verify the result also writes via `H3_SET_EFFECTIVE_RESOLUTION` (Rule RW) and uses `H3_SET_DIGIT_AT_RES` for the digit write (Rule DW).

---

## §7 — FFI Shim Contract

The Python/PySpark/Scala bindings need **C linkage** entry points that pass `__uint128_t` safely across ABI boundaries. Most platform ABIs (System V AMD64, AArch64, Windows x64) do **not** pass `__uint128_t` in registers consistently. The shim avoids ABI surprises by passing 128-bit cells **by pointer**.

### §7.1 Ten shim functions

| Function | Semantics |
|----------|-----------|
| `h3_ext_lat_lng_to_cell(double lat, double lng, int res, H3Index *out)` | Encoder. `out` is the 128-bit cell. |
| `h3_ext_cell_to_lat_lng(const H3Index *cell, double *lat, double *lng)` | Decoder. `cell` is read-only. |
| `h3_ext_cell_to_parent(const H3Index *cell, int parentRes, H3Index *out)` | Hierarchy up. |
| `h3_ext_cell_to_children(const H3Index *cell, int childRes, H3Index *children, int64_t *count)` | Hierarchy down. Caller pre-allocates `children` to the size returned by `h3_ext_cell_to_children_size`. |
| `h3_ext_cell_to_children_size(const H3Index *cell, int childRes, int64_t *out)` | Sizing query for `cell_to_children` allocation. Mandatory because the worst-case (`7^22`) is infeasible to over-allocate. |
| `h3_ext_is_valid_cell(const H3Index *cell, int *out)` | `*out = 1` if valid, `0` otherwise. |
| `h3_ext_get_resolution(const H3Index *cell, int *out)` | Effective resolution (0–22). |
| `h3_ext_grid_distance(const H3Index *a, const H3Index *b, int64_t *out)` | localij distance. |
| `h3_ext_h3_to_string(const H3Index *cell, char *out, size_t sz)` | Serialize to lowercase hex. `out` must be at least 33 bytes (32 hex + NUL). |
| `h3_ext_string_to_h3(const char *str, H3Index *out)` | Parse lowercase hex string into a 128-bit cell. Round-trip mate of the previous. |

All ten return `H3Error` (an `int` enum). All `H3Index *` parameters are size `sizeof(H3Index) == 16`. The `_to_string` / `_from_string` pair exists because every consumer that persists cells (Delta tables, Parquet keys, JSON payloads) needs string round-trip; without these shims consumers hex-encode in their host language and risk drifting from the canonical format.

### §7.2 ABI rationale

- **By pointer, not by value.** `__uint128_t` lacks a stable register-passing ABI on Windows MSVC (no support at all) and is risky-to-fragile on other platforms. By-pointer is universally safe — `H3Index *` is always passed as a 64-bit pointer.
- **Caller-allocated output buffers** for arrays. The shim never `malloc`s on behalf of the caller; the caller pre-allocates via `cellToChildrenSize` (or its FFI shim equivalent). This avoids cross-runtime free issues.
- **`H3Error` return for everything.** No `H3Index` return-by-value (which would smuggle a 128-bit value across ABI). Output via pointer.
- **Names prefixed `h3_ext_`** to avoid collision with stock H3 if the consumer links both libraries.

### §7.3 What NOT to expose

Don't expose internal helpers (`_faceIjkToH3`, `_h3Rotate60cw`, etc.) across FFI. They're unstable internal contracts and exposing them invites versioning headaches. Anything FFI-callable must be a public, contract-stable surface.

### §7.4 Build artifact

The shim compiles into the same `libh3` (or a separate `libh3ext`) — keep the upstream library output filename `libh3.{so,dylib,a}` per `CLAUDE.md` to avoid breaking consumers.

---

## §8 — Test Gates by Phase

Test gates are **categorical** — verify a property holds, not that a specific byte sequence is present. The audit (`.claude/skills/h3-128-audit/audit.py`) is the structural validator; ctest is the runtime validator.

### §8.A Phase A gates
- A1: `sizeof(H3Index) == 16` after typedef widening.
- A2: All Group C macros compile (a smoke header that uses each one).
- A3: `MAX_H3_EXT_RES` constant accessible from `constants.h`.
- A4: 316/316 stock ctest still passes.
- A5: ASAN+UBSAN clean on stock suite.

### §8.B Phase B gates
- B1: CMake builds clean on Linux + macOS.
- B2: MSVC build emits `#error` on `__SIZEOF_INT128__` undefined (per §9).
- B3: libm-tolerance helper compiles and is documented.

### §8.C Phase C gates
- C1: `stringToH3` round-trips a known res-15 stock cell to identical bytes.
- C2: `h3ToString` produces a 32-character lowercase hex string (128 bits / 4 bits per hex char) for an ext cell — note that the buffer must be ≥ 33 bytes to hold the trailing NUL.
- C3: Round-trip 1,000 random ext cells through `h3ToString → stringToH3` byte-identically.

### §8.D Phase D gates (the bulk of the test plan)
- D4-G0: **Stock-stock byte identity** (non-negotiable #2 runtime check). For 1,000 stock cells across res 0–15, run every `cellToParent(child, parentRes)` pair and assert `(low_64_bits == upstream_h3_value) AND (high_64_bits == 0)`. Catches Group A/B regressions early.
- D1-G1: `latLngToCell` at res 19 for 100 known coordinates produces ext cells with bit 64 set.
- D2-G1: `cellToLatLng` round-trips 1,000 res-19 cells within libm tolerance (§9).
- D3-G1: `getResolution` returns 16–22 for ext cells (verifies effective-res).
- D4-G1: `cellToParent(cell, parentRes)` for ext cells where parentRes ≤ 15 produces a stock cell (bit 64 clear).
- D4-G2: `cellToParent(cell, parentRes)` for ext cells where parentRes ∈ [16, 22] produces an ext cell with correct effective res.
- D4-G3a: **Hexagon parent**. `cellToChildren(stock_hex_res_15, child_res_16)` produces **7** ext cells, each with bit 64 set, base cell preserved, stock-res field = 0, ext-digit-16 ∈ {0..6}.
- D4-G3b: **Pentagon parent**. Pick one of the 12 res-0 pentagons; descend to res-15 pentagon; `cellToChildren(pent_res_15, child_res_16)` produces **6** ext cells (PENTAGON_SKIPPED_DIGIT removes one). Verify against `cellToChildrenSize` formula `1 + 5*(_ipow(7,n)-1)/6`.
- D4-G4: `cellToCenterChild(cell, childRes)` for `childRes ≥ 16` returns a cell with bit 64 set (regression for §6.8).
- D4-G5: `cellToChildrenSize` returns `7^(childRes - parentRes)` analytically for hexagon parents and the pentagon formula for pentagons. **Verify the returned int64 — do not enumerate** (worst case `7^22 ≈ 5.6e17` is infeasible to allocate).
- D4-G6: Full `cellToChildren` ext-recursion: 1 cell at res 15 → 7 at res 16 → 49 at res 17 → ... → 7^7 = 823,543 at res 22.
- D4-G7: Iterator ext path (`iterInitParent` + `iterStepChild`) emits exactly the same set as `cellToChildren`.
- D5-G1: `gridDistance(a, b)` for two ext cells in the same base cell returns expected ijk distance.
- D6-G1: `_h3Rotate60ccw` six times restores the original ext cell (rotation is a 6-cycle).
- D7-G1: All ten FFI shim functions linkable from a C test program.
- D7-G2: FFI shim functions accept and return `H3Index *` (16 bytes) without ABI corruption.
- D7-G3: `h3_ext_h3_to_string` / `h3_ext_string_to_h3` round-trip 1,000 random ext cells byte-identically (mirror of C2/C3 at FFI boundary).

### §8.E Phase E gates
- E1: `_hasGoodTopBits` accepts every valid ext cell (regression for §6.1).
- E2: `_firstOneIndex` returns correct bit position for cells with set bits in [64, 127] (regression for §6.2).
- E3: `isValidCell` returns true for 1,000 random valid ext cells, false for 1,000 corrupted ones (set a reserved bit to 1, etc.).
- E4: `_hasAny7UptoRes` correctly flags an ext cell with a digit-7 violation in the ext range (§6.3).
- E5: `_hasAll7AfterRes` correctly flags an ext cell with a non-7 digit past effective res (§6.4).
- E6: **UBSAN gate for `_zeroIndexDigits`** — call directly with `start=11, end=22` under UBSAN; must complete without `shift exponent ... is negative` (regression for §6.6).
- E7: **UBSAN gate for `_incrementResDigit`** — call iterator down to res 22 under UBSAN; must complete without negative-shift UB (regression for §6.10).

### §8.F Phase F gates
- F1: `cellToBoundary` for an ext cell produces 6 vertices (or 5 for pentagons) with finite lat/lng values within libm tolerance of expected.
- F2: `getIcosahedronFaces` for an ext cell returns 1–2 faces (or 5 for pentagons).
- F3: `getPentagons(20, out)` returns 12 pentagons at res 20.
- F4: `isResClassIII` matches `(res % 2)` for res 16–22.

### §8.G Phase G gates (audit)
- G1: `audit.py --quiet` returns exit 0 (zero CRITICAL, zero FINDING).
- G2: Mutation manifest catches every documented widening rule (Rules LB, GR, DW, RW, INIT).
- G3: Every `MAX_H3_RES` site in `src/h3lib/lib/*.c` is classified as either widened (Rule GR/LB) or intentionally-not-widened (with rationale).

### §8.H Phase H gates (acceptance)
- H1: `ctest --output-on-failure` 316/316 stock pass.
- H2: ext suite (~80 new tests) all pass.
- H3: ASAN+UBSAN clean across stock + ext suite.
- H4: CI green on `{ubuntu-latest, macos-latest} × {release, sanitizers}`.
- H5: Audit clean (zero findings).
- H6: Pre-commit hook installed and tested on a deliberately-red change (must reject).

---

## §9 — Platform Decisions

### §9.1 MSVC dropped

The typedef in `h3api.h.in` becomes:

```c
#ifndef __SIZEOF_INT128__
#error "H3-Extended requires __uint128_t; compile with GCC, Clang, or use MinGW/WSL on Windows."
#endif
typedef __uint128_t H3Index;
```

No struct fallback. The struct fallback was in earlier drafts; the hidden cost (every macro doubles in complexity, ABI-fragile, register pressure) outweighs the benefit (Windows-native build). MinGW + WSL are documented escape hatches.

### §9.2 libm tolerance

Round-trip tests (`latLngToCell → cellToLatLng`) cannot be byte-exact at res 16–22 because intermediate `sin`/`cos`/`asin` results vary across libm implementations (glibc vs musl vs Apple libm). The tolerance helper:

```c
static inline bool latlng_within_tolerance(LatLng a, LatLng b, int res) {
    double tolerance_rads = res >= 16 ? 1e-12 : 1e-9;  // tighter for ext
    return fabs(a.lat - b.lat) < tolerance_rads
        && fabs(a.lng - b.lng) < tolerance_rads;
}
```

Used by every Phase D-G1 / D-G2 round-trip test. The tolerance value is empirical — calibrate by running glibc-vs-musl-vs-darwin and taking 10× the worst observed delta.

### §9.3 Endianness and string format

H3Index is treated as an opaque integer, not a byte array, in source. Bit shifts and masks are byte-order-agnostic in C.

**Canonical string format.** `h3ToString` outputs **32 lowercase hex characters** (128 bits / 4 bits per hex digit). Buffer size is 33 bytes (32 hex + trailing NUL). For stock cells, the high 16 hex chars are all `0`, so the printable representation extends the 16-char stock string with 16 leading zeros — not a different format. For ext cells, the high 16 chars carry the ext flag (most-significant bit of the second-highest hex digit) and ext digits.

**Stock compatibility note.** Stock H3 produces 16-char hex strings. Consumers that need to interop with stock-only tooling should detect a 16-char input as stock (no high-half) and zero-extend to 32 chars before parsing. The shim layer's `h3_ext_string_to_h3` MUST accept both 16-char and 32-char inputs and produce a valid 128-bit cell either way.

---

## §10 — Failure-Mode Catalog

Symptom → likely cause → diagnostic.

| Symptom | Likely cause | Diagnostic |
|---------|--------------|------------|
| ASAN / UBSAN: "shift exponent N is negative" | Rule DW missed: `H3_SET_INDEX_DIGIT(h, r, ...)` with `r > 15` | `grep -n 'H3_SET_INDEX_DIGIT\b' src/h3lib/lib/*.c`; check each loop bound |
| Ext cell fails `isValidCell` but looks well-formed | `_hasGoodTopBits` not widened (§6.1) | Read `_hasGoodTopBits`; verify the high-half check exists |
| `cellToChildren` returns junk for stock parent / ext child | `iterators.c` not widened | `grep -n 'H3_GET_RESOLUTION\|H3_SET_INDEX_DIGIT\|MAX_H3_RES' src/h3lib/lib/iterators.c` |
| `cellToParent` returns a half-state cell | Rule RW or DW missed in loop body (§6.7) | Read function body lines |
| `cellToCenterChild` returns res 0–6 for childRes 16–22 | §6.8 — `H3_SET_RESOLUTION` instead of `H3_SET_EFFECTIVE_RESOLUTION` | Check the line just after `_zeroIndexDigits` |
| Ext cell from `setH3Index` has zero ext digits | §6.16 — `H3_INIT` instead of `H3_INIT_EXT` | Check the function entry |
| Stock res-15 round-trip fails after widening | Group A / Group B macro accidentally modified | `git diff` `h3Index.h`; only Group C should be added |
| ctest hangs or stack-overflows on `cellToChildren` | Iterator infinite loop because `iterStepChild` doesn't advance ext digit | Read `_incrementResDigit` and `iterStepChild` |
| `__builtin_clzll` UB in `_firstOneIndex` (UBSAN) | §6.2 — high-half not handled | Read `_firstOneIndex` body |
| `gridDistance` returns wrong result for ext cells | localij not widened (§6.15) | `grep -n 'H3_GET_RESOLUTION\|H3_SET_INDEX_DIGIT' src/h3lib/lib/localij.c` |
| Polyfill segfaults at res 18 | `MAX_EDGE_LENGTH_RADS` array (§5.6) — index out of bounds | Polyfill is deferred or guarded; do not extend the lookup table |
| FFI shim segfault on Linux | Caller passed `H3Index` by value where shim expects pointer | Audit FFI signatures (§7.1) |
| Cross-platform CI red on Linux but green on macOS (or vice versa) | libm tolerance too tight (§9.2) | Inspect the failing test; widen tolerance or pin to one libm |

---

## §11 — Acceptance Criteria

The MVP is "done" when ALL of:

1. ✅ 316/316 stock ctest pass on `feat/h3-128-mvp` HEAD.
2. ✅ Ext test suite (≥ 80 new tests covering D1–D7-G* + E* + F*) passes.
3. ✅ ASAN clean across full suite.
4. ✅ UBSAN clean across full suite.
5. ✅ `audit.py --quiet` exits 0 (zero CRITICAL, zero FINDING).
6. ✅ Mutation tests caught + 0 new gaps.
7. ✅ CI green on `ubuntu-latest × {release, sanitizers}` AND `macos-latest × {release, sanitizers}` for the merge-target SHA.
8. ✅ Pre-commit hook installed (verify with `cat .git/hooks/pre-commit`).
9. ✅ Phase H signed off in commit message: tagged `v0.1.0-128bit-mvp`.
10. ✅ FFI shim linkable: a C smoke test compiles against `libh3.{so,dylib,a}` and exercises all 10 entry points without segfault.

Compaction (`compactCells`, `uncompactCells`, `uncompactCellsSize`), polyfill widening, edges, and vertices are explicitly OUT of MVP scope and tracked as post-MVP work items.

---

## §12 — Three Target Locations

The Amalfi Intelligence platform tests its centimeter-resolution claim on three real-world properties. Each target validates a different geographic regime:

| Tag | Location | Why |
|-----|----------|-----|
| LOC1 | Monmouth Beach, NJ (founder's property — `monmouth_30_9.02`) | Mid-latitude, Class III base cells, salt-air corrosion test bed, GeoJSON parcel boundary in `docs/amalfi-intelligence/property_51_navesink.geojson` |
| LOC2 | Palm Beach, FL (commercial pilot site, TBD) | Sub-tropical, hurricane-zone, commercial complex with curtain wall + flat roof — exercises Roof Inspector, Facade Inspector, Moisture & Thermal agents |
| LOC3 | Sorrento, IT (heritage property, TBD) | High-latitude variant + irregular parcel topology — pentagon-adjacent base cell coverage, architectural heritage detection |

Each location's res 13/14/15 grid is precomputed; res 16–22 grids are generated via `cellToChildren` once Phase D4 is green. The acceptance test for the platform integration (separate from the H3-128 MVP) is: encode 1,000 random points within each parcel boundary at res 19, store in Delta, query by H3 cell, verify round-trip fidelity within libm tolerance (§9.2).

---

## §13 — Audit and Mutation Tests

Live at `.claude/skills/h3-128-audit/{audit.py, mutations/}`. Run before every `feat/h3-128-mvp` push:

```bash
python3 .claude/skills/h3-128-audit/audit.py --quiet
python3 .claude/skills/h3-128-audit/mutations/run.py
```

Both must exit 0 for a green build. Audit categories may evolve as the implementation lands; the mutation manifest is the source of truth for "what failure modes the audit is required to catch."

The audit is a **structural validator of the implementation against the source-of-truth contracts in this playbook** — it does not validate the playbook against itself. The playbook's correctness is reviewed by humans plus one adversarial Claude review pass before each tagged release.

---

## §14 — Glossary

- **Stock cell** — H3 cell with effective resolution ∈ [0, 15]; bit 64 = 0; bytes 8–15 = 0.
- **Ext cell** — H3 cell with effective resolution ∈ [16, 22]; bit 64 = 1; bytes 8–15 hold ext digits + reserved.
- **Group A** — bit-position constants in `h3Index.h` (unchanged).
- **Group B** — getter/setter macros in `h3Index.h` over Group A positions (unchanged).
- **Group C** — new macros (this playbook §3) for ext bits and dispatch.
- **Effective resolution** — true resolution of the cell, 0–22, via `H3_GET_EFFECTIVE_RESOLUTION`.
- **Stock res field** — bits 52–55, holds 0–15. For ext cells holds `effective_res - 16`, range 0–6.
- **Ext flag** — bit 64. 0 = stock, 1 = ext.
- **Sentinel digit** — value 7 (`INVALID_DIGIT`); marks digit positions past the cell's resolution.

---

*Amalfi H3-Extended Playbook v4.1.0 — companion to H3 v4.4.1 source — confidential — 2026-05-02*
