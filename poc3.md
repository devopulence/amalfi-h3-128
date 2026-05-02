# POC-3: Iterator State Machine — Test Specification

> **Version:** 1.0.0 — 2026-05-02
> **Prerequisite:** POC-1 passed (685/685)

## Objective

Write a standalone C program (`poc3_iterator.c`) that implements and exhaustively tests the widened iterator state machine for child-cell enumeration across the stock/ext boundary. Zero H3 dependencies. Compiles and runs with sanitizers.

## Construction Instructions

1. **Copy the macro foundation from POC-1.** The first ~150 lines should be the proven Group A/B/C macros from `poc1_bit_layout.c`.

2. **Define the iterator struct:**
```c
typedef struct {
    H3Index h;          // current child cell
    int _parentRes;     // effective resolution of parent (0-22)
    int _skipDigit;     // digit value to skip (-1 if hexagon, 1 if pentagon)
} ChildIterator;
```

3. **Implement the five widened functions** (`_zeroIndexDigits`, `_incrementResDigit`, `_getResDigit`, `_iterInitParent`, `iterStepChild`) as documented in CLAUDE.md.

4. **Implement `iterStepChild` carry logic.** When incrementing the digit at `childRes` produces value 7 (sentinel), reset it to 0 and carry to the next higher digit. When the carry propagates past `_parentRes + 1`, enumeration is complete (set `h = H3_NULL` or a sentinel value).

5. **Implement pentagon skip logic.** When `_skipDigit >= 0`, the digit at position `_parentRes + 1` must skip that value. After `_incrementResDigit` at `_parentRes + 1`, if the digit equals `_skipDigit`, increment again.

6. **Write test functions** for all categories IT-01 through IT-28.

7. **Build cell-fabrication helpers** as in POC-1/POC-2.

## Key Test Patterns

### The critical boundary test (IT-17)
```c
// Hexagon parent at res 15, enumerate children at res 16
H3Index parent = make_stock_cell(15, 5);
// Set digits 1-15 to known values (e.g., all 0 = center child path)
ChildIterator it;
_iterInitParent(&it, parent, 16);
int count = 0;
while (it.h != H3_NULL) {
    // Verify: bit 64 set, effective res = 16, base cell = 5
    // Verify: digits 1-15 match parent, digit 16 is unique per child
    count++;
    iterStepChild(&it);
}
assert(count == 7);  // hexagon → 7 children
```

### The full-depth test (IT-19)
```c
// Enumerate res 15 → res 18 = 7^3 = 343 children
// Collect all children, verify:
// - count == 343
// - no duplicates (hash or sort + dedup)
// - all have effective res 18
// - all have bit 64 set
// - all have correct base cell
// - all have reserved bits clear
```

### The UBSAN trap test (IT-26)
```c
// This is the test that catches §6.6 and §6.10
// Enumerate from res 15 to res 22 (7^7 = 823,543 children)
// Under UBSAN, any negative shift in _incrementResDigit or _zeroIndexDigits
// will abort immediately. Completing without abort proves the UB is eliminated.
// NOTE: This takes ~1-2 seconds. Acceptable for a POC.
```

## Assertion Target

Aim for **500+ assertions** across the 28 test categories. The enumeration tests (IT-17 through IT-26) generate assertions per child cell, so the count accumulates quickly.

## Success Criteria

```
All assertions passed, 0 failed
POC-3 PASSED — iterator state machine validated.
```

Plus: exit 0, no ASAN output, no UBSAN output.

## Checkpoint

On success, write `POC3_CHECKPOINT.md` to the repo root:
```
# POC-3 Checkpoint
- Date: <ISO timestamp>
- Compiler: <gcc/clang version>
- Platform: <uname -s -m>
- Assertions: <count> passed, 0 failed
- ASAN: clean
- UBSAN: clean
- Exit code: 0
- Result: POC-3 PASSED
- Functions validated: _zeroIndexDigits, _incrementResDigit, _getResDigit, _iterInitParent, iterStepChild
- Children enumerated: <total across all tests>
- Max depth tested: res 15 → res 22 (823,543 children)
```
