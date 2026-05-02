# POC-4: FFI ABI Validation — Test Specification

> **Version:** 1.0.0 — 2026-05-02
> **Prerequisite:** POC-1 passed (685/685)

## Objective

Write three files (`poc4_shim.h`, `poc4_shim.c`, `poc4_caller.c`) that prove `__uint128_t` values survive cross-translation-unit function calls when passed by pointer. Compile as separate TUs, link, and run with sanitizers.

## Construction Instructions

### `poc4_shim.h` — shared header
```c
#ifndef POC4_SHIM_H
#define POC4_SHIM_H

#ifndef __SIZEOF_INT128__
#error "Requires __uint128_t"
#endif

#include <stdint.h>
#include <stddef.h>

typedef __uint128_t H3Index;

typedef enum {
    H3_SUCCESS = 0,
    E_FAILED = 1,
    E_DOMAIN = 2,
    E_RES_DOMAIN = 3
} H3Error;

// All 10 shim functions — by pointer, never by value
H3Error h3_ext_lat_lng_to_cell(double lat, double lng, int res, H3Index *out);
H3Error h3_ext_cell_to_lat_lng(const H3Index *cell, double *lat, double *lng);
H3Error h3_ext_cell_to_parent(const H3Index *cell, int parentRes, H3Index *out);
H3Error h3_ext_cell_to_children(const H3Index *cell, int childRes, H3Index *children, int64_t *count);
H3Error h3_ext_cell_to_children_size(const H3Index *cell, int childRes, int64_t *out);
H3Error h3_ext_is_valid_cell(const H3Index *cell, int *out);
H3Error h3_ext_get_resolution(const H3Index *cell, int *out);
H3Error h3_ext_grid_distance(const H3Index *a, const H3Index *b, int64_t *out);
H3Error h3_ext_h3_to_string(const H3Index *cell, char *out, size_t sz);
H3Error h3_ext_string_to_h3(const char *str, H3Index *out);

// Cross-TU consistency checks
size_t h3_ext_sizeof_h3index(void);
size_t h3_ext_alignof_h3index(void);

#endif
```

### `poc4_shim.c` — shim implementation (separate TU)
- Include `poc4_shim.h`
- Implement each function with **deterministic stub logic** that exercises the pointer contract
- For `h3_ext_lat_lng_to_cell`: encode lat/lng/res into a known bit pattern (e.g., pack doubles into the 128-bit value deterministically)
- For `h3_ext_h3_to_string` / `h3_ext_string_to_h3`: real hex serialization (this is simple enough to implement and tests the full string round-trip)
- `h3_ext_sizeof_h3index()` returns `sizeof(H3Index)` from the shim's TU
- `h3_ext_alignof_h3index()` returns `_Alignof(H3Index)` from the shim's TU

### `poc4_caller.c` — test caller (separate TU, contains main)
- Include `poc4_shim.h`
- Include Group A/B/C macros (from POC-1) for cell fabrication
- Call every shim function, verify return values and output parameters
- Compare sizeof/alignof between caller TU and shim TU

## Build Steps (MUST be separate compilation)

```bash
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -c poc4_shim.c -o poc4_shim.o
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -c poc4_caller.c -o poc4_caller.o
gcc -fsanitize=address,undefined -o poc4_ffi poc4_shim.o poc4_caller.o -lm
./poc4_ffi
```

## Key Test Patterns

### The fundamental ABI test
```c
// In caller: fabricate a known 128-bit cell
H3Index cell = make_ext_cell(19, 42);  // known bit pattern
// Write to stack, pass pointer to shim
int valid = 0;
H3Error err = h3_ext_is_valid_cell(&cell, &valid);
// Shim reads *cell in a DIFFERENT translation unit
// If the ABI corrupts the pointer dereference, valid will be wrong
assert(err == H3_SUCCESS);
assert(valid == 1);
```

### The string round-trip ABI test
```c
// Fabricate cell in caller TU
H3Index cell = make_ext_cell(20, 77);
char buf[33];
// Serialize in shim TU
h3_ext_h3_to_string(&cell, buf, sizeof(buf));
// Parse back in shim TU
H3Index parsed;
h3_ext_string_to_h3(buf, &parsed);
// Compare in caller TU — must be bit-identical
assert(memcmp(&cell, &parsed, sizeof(H3Index)) == 0);
```

### The array-passing ABI test
```c
// Caller allocates buffer
int64_t count = 0;
h3_ext_cell_to_children_size(&parent, 16, &count);
H3Index *children = calloc(count, sizeof(H3Index));
int64_t actual = 0;
h3_ext_cell_to_children(&parent, 16, children, &actual);
assert(actual == count);
// Verify each child is intact (non-zero, correct resolution, etc.)
for (int64_t i = 0; i < actual; i++) {
    assert(children[i] != 0);
    // ... validate each child
}
free(children);
```

## Assertion Target

Aim for **200+ assertions** across the 22 test categories (ABI-01 through ABI-22). The string round-trip test with 100 random cells adds significantly to the count.

## Success Criteria

```
All assertions passed, 0 failed
POC-4 PASSED — FFI ABI validated.
```

Plus: exit 0, no ASAN output, no UBSAN output.

## Checkpoint

On success, write `POC4_CHECKPOINT.md` to the repo root:
```
# POC-4 Checkpoint
- Date: <ISO timestamp>
- Compiler: <gcc/clang version>
- Platform: <uname -s -m>
- Assertions: <count> passed, 0 failed
- ASAN: clean
- UBSAN: clean
- Exit code: 0
- Result: POC-4 PASSED
- Shim functions validated: all 10
- Compilation model: separate TUs (poc4_shim.o + poc4_caller.o)
- Cross-TU sizeof consistency: confirmed (16 bytes both sides)
- Cross-TU alignof consistency: confirmed (16 bytes both sides)
- String round-trips: 100 random ext cells, bit-identical
```
