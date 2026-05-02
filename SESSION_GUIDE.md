# Session Guide — Implementation Sessions

> **Version:** 1.0.0 — 2026-05-02
> **Purpose:** Defines the 3-session architecture, build setup, checkpoint format, and kickoff prompts.
> **When to read:** Once at session start, after CLAUDE.md and POC_MANDATE.md.

---

## Build Setup (FIRST commit on `feat/h3-128-mvp`)

```bash
mkdir -p build-dev && cd build-dev
cmake .. -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g -Werror -Wall -Wextra" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cd ..
cmake --build build-dev -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
(cd build-dev && ctest --output-on-failure)
# Must be 316/316. If not, STOP.

cat > .git/hooks/pre-commit <<'EOF'
#!/bin/bash
set -e
cmake --build build-dev -j --quiet
(cd build-dev && ctest --output-on-failure --quiet)
echo "[pre-commit] All green."
EOF
chmod +x .git/hooks/pre-commit

mkdir -p .github/workflows
cat > .github/workflows/ci.yml <<'EOF'
name: CI
on: [push, pull_request]
jobs:
  build-and-test:
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, macos-latest]
        config: [release, sanitizers]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: |
          mkdir build && cd build
          if [[ "${{ matrix.config }}" == "sanitizers" ]]; then
            cmake .. -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_C_FLAGS="-fsanitize=address,undefined -O1 -g -Werror -Wall -Wextra" \
              -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
          else
            cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-Werror"
          fi
      - name: Build
        run: cmake --build build -j
      - name: Test
        run: cd build && ctest --output-on-failure
EOF
git add .github/workflows/ci.yml
git commit -m "Mandatory build discipline: ASAN+UBSAN dev config, pre-commit hook, CI"
```

---

## Session Boundaries

| Session | Phases | Exit condition |
|---------|--------|----------------|
| 1 | A + B + C | Typedef widened, Group C macros compiling, string I/O round-tripping, 316/316 green |
| 2 | D (D1–D7) | Encode/decode/hierarchy/localij/rotation/FFI widened and tested. Context pressure valve at D3 boundary. |
| 3 | E + F + G + H (or D4+ if Session 2 checkpointed early) | Full acceptance criteria (§11), tag `v0.1.0-128bit-mvp` |

---

## Checkpoint Format

Each session writes `SESSION_N_CHECKPOINT.md` to the repo root on completion:

```
# Session N Checkpoint
- Date: <ISO timestamp>
- Commit: <SHA of last green commit>
- Stock ctest: <pass count>/<total>
- Ext tests added: <count>
- ASAN: clean | <failure summary>
- UBSAN: clean | <failure summary>
- Gates passed: <list with exact results, e.g. "A1: PASS — sizeof==16">
- Gates deferred: <list with reasons>
- Incomplete phases: <list, or "none">
```

The next session reads this checkpoint and verifies before proceeding — no manual validation step.

---

## Session 1 Kickoff Prompt

```
Read CLAUDE.md end-to-end. Then read POC_MANDATE.md. Then read POC_TRANSFER_GUIDE.md. Then read SESSION_GUIDE.md. Then read h3-extended-playbook-v4.1.0.md end-to-end.

MANDATORY DISCIPLINE:
- Compile + ctest after EVERY edit. Edit → build → test → green = commit, red = revert. No exceptions.
- ASAN+UBSAN on throughout. One widening site = one commit. Tests in same commit.
- Use POC-proven patterns for ALL edits. POC_MANDATE.md defines the 5 patterns. POC_TRANSFER_GUIDE.md tells you what to copy from which file.

Before writing any code:
1. Verify branch feat/h3-128-mvp, upstream commit 69e01f3c.
2. Verify POC checkpoints exist: POC1 through POC4.
3. Build unmodified source, confirm 316/316.
4. Set up build discipline per SESSION_GUIDE.md. Commit as first commit.
5. Confirm 316/316 under sanitizers.

PHASE A — Type widening (POC-1 patterns):
- Widen typedef to __uint128_t. Add #error guard.
- Add MAX_H3_EXT_RES=22.
- Copy all 14 Group C macros VERBATIM from poc1_bit_layout.c into h3Index.h.
- GATE CHECK: A1 (sizeof==16), A2 (macros compile), A3 (constant), A4 (316/316), A5 (sanitizers). ANY fail → STOP.

PHASE B — Build hygiene:
- CMakeLists, asserts, libm tolerance.
- GATE CHECK: B1, B3, 316/316.

PHASE C — String I/O:
- Widen stringToH3/h3ToString for 32-char hex.
- GATE CHECK: C1 (stock round-trip), C2 (32-char ext), C3 (1,000 random ext round-trips — architecture gate. Fail → STOP).

Write SESSION_1_CHECKPOINT.md with exact gate results. Commit. Stop. Do not proceed to Phase D.
```

---

## Session 2 Kickoff Prompt

```
Read CLAUDE.md end-to-end. Then POC_MANDATE.md. Then POC_TRANSFER_GUIDE.md. Then SESSION_GUIDE.md. Then h3-extended-playbook-v4.1.0.md — focus on §4, §5, §6, §7, §8.D.

MANDATORY DISCIPLINE: Compile after EVERY edit. ASAN+UBSAN. POC patterns. One commit per site.

Before code:
1. Read SESSION_1_CHECKPOINT.md. Verify all A/B/C gates PASS.
2. Confirm ctest green. If checkpoint missing or red: STOP.

D1 — Encoder: Widen latLngToCell, vec3ToCell, _faceIjkToH3, setH3Index. Use INIT rule (Pattern 5).
GATE: D1-G1. Fail → STOP.

D2 — Decoder: Widen cellToLatLng, cellToVec3, _h3ToFaceIjk.
GATE: D2-G1 (1,000 res-19 round-trips within tolerance).

D3 — Accessors: Widen getResolution, getIndexDigit, constructCell, getBaseCellNumber.
GATE: D3-G1.

CONTEXT PRESSURE VALVE: After D3 green, assess context. If heavy, write SESSION_2_CHECKPOINT.md with "Incomplete: D4-D7" and STOP. Session 3 picks up automatically.

D4 — Hierarchy + iterators (Pattern 3 MANDATORY):
- Run D4-G0 canary BEFORE any changes: 1,000 stock cells through cellToParent, byte identity.
- Copy _zeroIndexDigits, _incrementResDigit, _getResDigit, _iterInitParent, iterStepChild patterns from poc3_iterator.c.
- GATES: D4-G0 through D4-G7. RE-RUN D4-G0 after all D4 changes.

D5 — localij. GATE: D5-G1.
D6 — Rotation. GATE: D6-G1 (six rotations restore original).

D7 — FFI shim (Pattern 4 MANDATORY):
- Copy signatures from poc4_shim.h. Replace stubs with real H3 calls.
- GATES: D7-G1 through D7-G3.

Write SESSION_2_CHECKPOINT.md. Commit. Stop.
```

---

## Session 3 Kickoff Prompt

```
Read CLAUDE.md end-to-end. Then POC_MANDATE.md. Then POC_TRANSFER_GUIDE.md. Then SESSION_GUIDE.md. Then h3-extended-playbook-v4.1.0.md — focus on §6, §8, §11, §13.

MANDATORY DISCIPLINE: Same as Sessions 1 and 2.

Before code:
1. Read SESSION_2_CHECKPOINT.md. Note incomplete phases.
2. Confirm ctest green. If missing or red: STOP.

Start from checkpoint. If D4-D7 incomplete, do those first with Patterns 3 and 4.

E — Validation (Pattern 2 MANDATORY):
- Copy _hasGoodTopBits split low/high from poc2_validation.c.
- Copy _firstOneIndex high-half-first from poc2_validation.c.
- Copy _hasAny7UptoRes ext loop from poc2_validation.c.
- Copy _hasAll7AfterRes (remove early-return) from poc2_validation.c.
- Copy _hasDeletedSubsequence ext loop from poc2_validation.c.
- GATES: E1-E7. E1 or E2 fail → STOP.

F — Auxiliary surfaces. GATES: F1-F4.
G — Audit infrastructure. Write audit.py + mutation manifest. GATES: G1-G3.

H — Acceptance. Full §11 battery. Tag v0.1.0-128bit-mvp. GATES: H1-H6.

Write SESSION_3_CHECKPOINT.md (final). Commit. Stop.
```
