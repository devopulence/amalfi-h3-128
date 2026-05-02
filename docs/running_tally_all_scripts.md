# Running Tally — All Scripts and Source Files

> **Last Updated:** 2026-05-02
> **Scope:** H3-Extended (128-bit) preflight validation campaign — standalone C programs that compile with sanitizers, prove one architectural risk area, and run in complete isolation from the H3 source tree.

---

## Preflight Validation Programs

Standalone C test programs that validate the 128-bit architecture before any H3 source is modified. Each compiles with `-fsanitize=address,undefined -Werror -Wall -Wextra`, runs to completion with zero ASAN/UBSAN output, and exits 0 on success.

| Script | Created | Description |
|--------|---------|-------------|
| `poc1_bit_layout.c` | 2026-05-02 | POC-1 bit layout + Group C macro validation. Defines stock Group A/B constants and 14 Group C macros for 128-bit cells; runs 19 test categories (PF-01..PF-19) covering sizeof, resolution encoding, digit round-trip across all 22 positions, reserved-bit zeroing, H3_INIT/H3_INIT_EXT sentinel patterns, cross-boundary digit isolation, and single-evaluation safety. Result: 685/685 passed. |
| `poc2_validation.c` | 2026-05-02 | POC-2 validation predicates. Implements widened `_hasGoodTopBits`, `_firstOneIndex`, `_hasAny7UptoRes`, `_hasAll7AfterRes`, `_hasDeletedSubsequence` for 128-bit cells. 23 categories (VP-01..VP-23) including 128-position single-bit `_firstOneIndex` sweep, every-bit-individually corruption checks for reserved 86–127, and pentagon K-axis detection through ext digits. Result: 3473/3473 passed. |
| `poc3_iterator.c` | 2026-05-02 | POC-3 iterator state machine. Implements `_zeroIndexDigits`, `_incrementResDigit`, `_getResDigit`, `_iterInitParent`, `iterStepChild` plus the `ChildIterator` struct. 28 categories (IT-01..IT-28) culminating in the 823,543-child full-depth enumeration (parent res 15 → child res 22) under UBSAN — proves the §6.6 / §6.10 negative-shift traps are eliminated. Pentagon skip semantics validated at the stock/ext boundary. Result: 4595/4595 passed; 828,210 children enumerated total. |
| `poc4_shim.h` | 2026-05-02 | POC-4 shared FFI header. Declares the `H3Index` typedef (`__uint128_t`), the `H3Error` enum, and 10 cross-TU shim function signatures (all by-pointer, never by-value because `__uint128_t` lacks a stable register-passing ABI). Plus `h3_ext_sizeof_h3index()` / `h3_ext_alignof_h3index()` probes for cross-TU consistency. Included by both shim and caller TUs. |
| `poc4_shim.c` | 2026-05-02 | POC-4 shim translation unit. Implements all 10 functions with deterministic stub logic — `h3_ext_lat_lng_to_cell` packs a hash-derived bit pattern; `h3_ext_h3_to_string`/`h3_ext_string_to_h3` perform real 32-char hex serialization; `h3_ext_cell_to_children` uses a base-7 `decode_child` helper rather than carrying the full iterator. NULL-pointer / NaN / out-of-range res returns map to `E_FAILED` / `E_DOMAIN` / `E_RES_DOMAIN` respectively. |
| `poc4_caller.c` | 2026-05-02 | POC-4 caller translation unit (contains `main`). Fabricates 128-bit cells using Group A/B/C macros copied from POC-1, then invokes every shim function across 22 ABI categories (ABI-01..ABI-22). Includes 100-cell deterministic-LCG string round-trip with `memcmp` bit-identical assertion, 500-cell stress test, full 21-case NULL-pointer error contract, and adversarial all-zero / all-ones / single-bit-127 round-trips proving the high half transits intact. Result: 1314/1314 passed. |

---

## Build Commands

Each POC is built and run independently from the repo root:

```bash
# POC-1
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc1_bit_layout poc1_bit_layout.c -lm && ./poc1_bit_layout

# POC-2
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc2_validation poc2_validation.c -lm && ./poc2_validation

# POC-3
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc3_iterator poc3_iterator.c -lm && ./poc3_iterator

# POC-4 (separate translation units — must NOT be combined)
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -c poc4_shim.c   -o poc4_shim.o
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra -c poc4_caller.c -o poc4_caller.o
gcc -fsanitize=address,undefined -o poc4_ffi poc4_shim.o poc4_caller.o -lm && ./poc4_ffi
```

---

## Summary

| Category | Count |
|----------|-------|
| Preflight Validation Programs | 6 |
| **Total** | **6** |

| Metric | Value |
|--------|-------|
| Total assertions across all POCs | 10,067 |
| Total children enumerated (POC-3) | 828,210 |
| Toolchain | Apple clang 21.0.0 / Darwin arm64 |
| Sanitizers | AddressSanitizer + UndefinedBehaviorSanitizer (both clean) |
| POCs passing | 4 / 4 |
