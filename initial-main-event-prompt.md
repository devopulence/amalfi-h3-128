Read CLAUDE.md end-to-end. Then read POC_MANDATE.md. Then read POC_TRANSFER_GUIDE.md. Then read SESSION_GUIDE.md. Then read h3-extended-playbook-v4.1.0.md end-to-end.

MANDATORY DISCIPLINE:
- Compile + ctest after EVERY edit. Edit → build → test → green = commit, red = revert. No exceptions.
- ASAN+UBSAN on throughout. One widening site = one commit. Tests in same commit.
- Use POC-proven patterns for ALL edits. POC_MANDATE.md defines the 5 patterns. POC_TRANSFER_GUIDE.md tells you what to copy from which file.

Before writing any code:
1. Verify we are on branch feat/h3-128-mvp.
2. Verify the H3 source code is v4.4.1 by checking: grep 'H3_VERSION_MAJOR\|H3_VERSION_MINOR\|H3_VERSION_PATCH' src/h3lib/include/h3api.h.in — expect 4.4.1.
3. Verify POC checkpoints exist: POC1_CHECKPOINT.md through POC4_CHECKPOINT.md.
4. Build the current source and run ctest. Confirm 316/316 stock tests pass.
5. Set up the mandatory build discipline per SESSION_GUIDE.md (build-dev with ASAN+UBSAN, pre-commit hook, CI workflow). Commit as the first implementation commit.
6. Confirm 316/316 under sanitizers in build-dev.

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