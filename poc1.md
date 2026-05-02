# POC-1: Bit Layout Validation — Test Specification

> **Status:** COMPLETED — see PREFLIGHT_CHECKPOINT.md

This POC is already validated. The test program (`poc1_bit_layout.c`), skill documentation (`PREFLIGHT_SKILL.md`), and checkpoint (`PREFLIGHT_CHECKPOINT.md`) are committed to the repo.

To re-run:
```bash
gcc -std=c99 -fsanitize=address,undefined -Werror -Wall -Wextra \
    -o poc1_bit_layout poc1_bit_layout.c -lm && ./poc1_bit_layout
```

Expected: 685 passed, 0 failed, exit 0, no sanitizer output.
