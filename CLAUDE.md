# Amalfi H3-Extended (128-bit) Fork

> **Version:** 2.0.0 — 2026-05-02
> **Companion files (read in order before writing code):**
> 1. `POC_MANDATE.md` — what the 4 POCs proved, the 5 mandatory coding patterns.
> 2. `POC_TRANSFER_GUIDE.md` — maps POC code → implementation phases, tells you what to copy where.
> 3. `SESSION_GUIDE.md` — 3-session architecture, checkpoints, kickoff prompts.
> 4. `h3-extended-playbook-v4.1.0.md` — strategic roadmap (§0–§14).

## Mission

Fork of `uber/h3` widened from 64-bit to 128-bit indexes, unlocking resolutions 16-22 (~1cm). Foundational IP for the Amalfi Intelligence spatial digital twin platform.

**The architecture is validated.** Four POCs (10,067 assertions, ASAN+UBSAN clean) proved the bit layout, validation predicates, iterator state machine, and FFI ABI in isolation before any H3 source was modified. See `POC_MANDATE.md` for details and mandatory coding patterns.

---

## Four Non-Negotiables

### 1. Accuracy > Speed
~2-5x slowdown acceptable. No 64-bit workarounds. No premature optimization.

### 2. Bit Layout Is a Strict Superset of 64-bit
Res 0-15 cells byte-identical to stock with high 64 bits zeroed. **Proven by POC-1 (685 assertions).** Layout: bits 0-63 unchanged, bit 64 = ext flag, bits 65-85 = ext digits, bits 86-127 = reserved (zero). If compatibility must break, STOP and surface.

### 3. All 316 Stock Tests Pass Byte-Identically
Baseline: 316/316 on unmodified `69e01f3c` (v4.4.1). A red stock test = revert immediately.

### 4. Compile + Test After EVERY Edit. Sanitizers ON.
```
edit → cmake --build build-dev -j → ctest --output-on-failure → green = commit, red = revert
```
One widening site = one commit. Tests in same commit. Pre-commit hook installed. NO `--no-verify`. ASAN+UBSAN on throughout.

---

## Repository State

| Item | Value |
|------|-------|
| Local path | `/Users/johndesposito/amalfi_work/amalfi-h3-128/` |
| Origin | `git@github.com:devopulence/amalfi-h3-128.git` |
| Upstream | `https://github.com/uber/h3.git` |
| Working branch | `feat/h3-128-mvp` |
| H3 version | v4.4.1 (commit `69e01f3c`) |
| ctest baseline | 316/316 |

---

## Verified Source Facts

| Fact | Value |
|------|-------|
| `typedef uint64_t H3Index;` | `src/h3lib/include/h3api.h.in:69` |
| `MAX_H3_RES 15` | `src/h3lib/include/constants.h:76` |
| `H3_INIT` value | `UINT64_C(35184372088831)` = `2^45 − 1` |
| Source commit pin | `69e01f3c` (v4.4.1) |

---

## Coding Standards

- K&R braces, 4-space indent, C99. Comment changes: `// H3-EXTENDED: <reason>`.
- `#error` if `__SIZEOF_INT128__` missing. No struct fallback. MSVC unsupported.
- Do not remove original functionality — only extend.

---

## Don't

- Don't reverse the dependency (fork imports nothing from `amalfi_intelligence_platform`).
- Don't open-source the fork.
- Don't change bit positions in bits 0-63.
- Don't bump `MAX_H3_RES`. Add `MAX_H3_EXT_RES = 22` instead.
- Don't optimize before correctness.
- Don't deviate from POC-proven patterns (see `POC_MANDATE.md`).
- Don't modify POC files. They are frozen proof artifacts. Copy from them.
- Don't pin source-side facts in comments. Use `grep`.

---

## Audit Infrastructure

`.claude/skills/h3-128-audit/audit.py` does not exist yet — Phase G deliverable. Until then, `ctest` is the sole green/red gate.

---

*Companion files: `POC_MANDATE.md`, `POC_TRANSFER_GUIDE.md`, `SESSION_GUIDE.md`, `h3-extended-playbook-v4.1.0.md` — all at repo root.*
