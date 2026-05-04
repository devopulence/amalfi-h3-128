#!/usr/bin/env python3
"""H3-Extended (128-bit) structural audit — Phase G deliverable.

Validates the implementation against the source-of-truth contracts in
h3-extended-playbook-v4.1.0.md. Run before every push.

Exit codes:
    0  audit clean (zero CRITICAL, zero FINDING)
    1  audit failed
    2  audit infrastructure error (missing files, etc.)

Gates:
    G1  zero CRITICAL, zero FINDING        (per playbook §8.G1)
    G2  every widening rule has a mutation in mutations/manifest.json
                                            (per playbook §8.G2)
    G3  every MAX_H3_RES site in src/h3lib/lib/*.c classified as
        widened or intentionally-not-widened (per playbook §8.G3)

Usage:
    python3 .claude/skills/h3-128-audit/audit.py            # human output
    python3 .claude/skills/h3-128-audit/audit.py --quiet    # ci mode
    python3 .claude/skills/h3-128-audit/audit.py --json     # machine output
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Iterable

REPO_ROOT = Path(__file__).resolve().parents[3]
H3_LIB_DIR = REPO_ROOT / "src" / "h3lib" / "lib"
H3_INCLUDE = REPO_ROOT / "src" / "h3lib" / "include" / "h3Index.h"
H3_API_HEADER = REPO_ROOT / "src" / "h3lib" / "include" / "h3api.h.in"
H3_CONSTANTS = REPO_ROOT / "src" / "h3lib" / "include" / "constants.h"
H3_SHIM = REPO_ROOT / "src" / "h3lib" / "include" / "h3ExtShim.h"
PRE_COMMIT_HOOK = REPO_ROOT / ".git" / "hooks" / "pre-commit"
MUTATION_MANIFEST = (
    REPO_ROOT / ".claude" / "skills" / "h3-128-audit" / "mutations" /
    "manifest.json"
)

# Required Group C macros (playbook §3). All 14 must appear in h3Index.h.
REQUIRED_GROUP_C_MACROS = [
    "MAX_H3_EXT_RES",          # 1 — in constants.h
    "H3_EXT_FLAG_OFFSET",      # 2
    "H3_EXT_FLAG_MASK",        # 3
    "H3_EXT_DIGITS_OFFSET",    # 4
    "H3_EXT_DIGITS_MASK",      # 5
    "H3_INIT_EXT",             # 6
    "H3_GET_EXT_FLAG",         # 7
    "H3_SET_EXT_FLAG",         # 8
    "H3_GET_EFFECTIVE_RESOLUTION",  # 9
    "H3_SET_EFFECTIVE_RESOLUTION",  # 10
    "H3_GET_EXT_INDEX_DIGIT",       # 11
    "H3_SET_EXT_INDEX_DIGIT",       # 12
    "H3_GET_DIGIT_AT_RES",          # 13
    "H3_SET_DIGIT_AT_RES",          # 14
]

# Required widening rules (playbook §5). Mutation manifest must cover each.
REQUIRED_WIDENING_RULES = ["LB", "GR", "DW", "DR", "RW", "INIT"]

# MAX_H3_RES site classification.
#
# Each entry maps `(filename:line)` to a classification:
#   "widened"          — the site uses MAX_H3_RES correctly as a threshold
#                        in widening dispatch logic (e.g. ext-vs-stock
#                        branch). Not a missed widening.
#   "deferred:<phase>" — intentionally not widened in MVP. Tracked as
#                        post-MVP work per playbook §5.6 / §11.
#
# Adding a new MAX_H3_RES site without classifying it here will trip the
# audit at G3.
MAX_H3_RES_CLASSIFICATIONS: dict[str, dict[int, str]] = {
    "h3Index.c": {
        # setH3Index INIT dispatch (Rule INIT, §5.5)
        155: "widened",
        # _hasAny7UptoRes E3 stock-window cap
        350: "widened",
        352: "widened",
        357: "widened",
        358: "widened",
        # _hasAll7AfterRes E4 ext-flag dispatch
        376: "widened",
        # constructCell INIT dispatch (Rule INIT, §5.5)
        527: "widened",
        # cellToParent high-half scrub guard
        564: "widened",
        570: "widened",
        # cellToCenterChild trailing-sentinel branch
        708: "widened",
        # _faceIjkToH3 INIT dispatch
        1127: "widened",
        # cellToChildPos / childPosToCell guard
        1676: "deferred:post-mvp",
    },
    "iterators.c": {
        255: "widened",  # iterInitBaseCellNum guard relaxed (D4)
    },
    "localij.c": {
        325: "widened",  # localIjkToCell INIT dispatch (Rule INIT, §5.5)
    },
    "algos.c": {
        # getRes0Cells calls getNumCells(MAX_H3_RES, ...) for an upper-bound
        # count used in stock res-0 enumeration. Auxiliary; out of MVP scope
        # per playbook §11.
        181: "deferred:post-mvp",
    },
    "latLng.c": {
        # getHexagonAreaAvgKm2 / M2 / EdgeLengthAvg / getNumCells —
        # auxiliary stat helpers. Intentionally stock-only per playbook
        # §11 (out of MVP scope; no ext-area constants computed yet).
        220: "deferred:post-mvp",
        235: "deferred:post-mvp",
        248: "deferred:post-mvp",
        261: "deferred:post-mvp",
        269: "deferred:post-mvp",
    },
    "polyfill.c": {
        # MAX_EDGE_LENGTH_RADS / NORTH_POLE_CELLS / SOUTH_POLE_CELLS —
        # static lookup tables sized for stock res only. Per playbook §5.6:
        # "Do NOT extend the array literals to length 23 unless the table
        # values for res 16-22 are known and computed."
        47: "deferred:post-mvp",
        54: "deferred:post-mvp",
        61: "deferred:post-mvp",
        # polygonToCells / polyfill guard. Per playbook §11: polyfill
        # widening is post-MVP.
        337: "deferred:post-mvp",
    },
}


@dataclass
class Finding:
    severity: str           # "CRITICAL" | "FINDING" | "INFO"
    gate: str               # "A1", "G1", "G3", etc.
    message: str
    location: str = ""

    def render(self) -> str:
        loc = f" at {self.location}" if self.location else ""
        return f"[{self.severity}] {self.gate}{loc}: {self.message}"


@dataclass
class AuditReport:
    findings: list[Finding] = field(default_factory=list)
    info: list[Finding] = field(default_factory=list)
    sites_classified: int = 0
    sites_widened: int = 0
    sites_deferred: int = 0

    @property
    def critical_count(self) -> int:
        return sum(1 for f in self.findings if f.severity == "CRITICAL")

    @property
    def finding_count(self) -> int:
        return sum(1 for f in self.findings if f.severity == "FINDING")

    def add(self, finding: Finding) -> None:
        if finding.severity == "INFO":
            self.info.append(finding)
        else:
            self.findings.append(finding)


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def _grep_pattern(pattern: str, paths: Iterable[Path]) -> list[tuple[str, int, str]]:
    """Return [(file_basename, line_no, line_content)] for matches."""
    rx = re.compile(pattern)
    results = []
    for path in paths:
        try:
            text = _read_text(path)
        except Exception:
            continue
        for n, line in enumerate(text.splitlines(), start=1):
            if rx.search(line):
                results.append((path.name, n, line))
    return results


def check_phase_a_macros(report: AuditReport) -> None:
    """G1 / Phase A: 14 Group C macros + MAX_H3_EXT_RES present."""
    if not H3_INCLUDE.exists():
        report.add(Finding("CRITICAL", "A2",
                           "h3Index.h missing", str(H3_INCLUDE)))
        return
    if not H3_CONSTANTS.exists():
        report.add(Finding("CRITICAL", "A3",
                           "constants.h missing", str(H3_CONSTANTS)))
        return

    h3_index_text = _read_text(H3_INCLUDE)
    constants_text = _read_text(H3_CONSTANTS)

    for macro in REQUIRED_GROUP_C_MACROS:
        if macro == "MAX_H3_EXT_RES":
            if "MAX_H3_EXT_RES" not in constants_text:
                report.add(Finding(
                    "CRITICAL", "A3",
                    "MAX_H3_EXT_RES not defined in constants.h"))
        else:
            if macro not in h3_index_text:
                report.add(Finding(
                    "CRITICAL", "A2",
                    f"Group C macro `{macro}` missing from h3Index.h"))


def check_typedef_widened(report: AuditReport) -> None:
    """A1: typedef must be __uint128_t."""
    if not H3_API_HEADER.exists():
        report.add(Finding("CRITICAL", "A1",
                           "h3api.h.in missing", str(H3_API_HEADER)))
        return
    text = _read_text(H3_API_HEADER)
    if "__uint128_t" not in text:
        report.add(Finding(
            "CRITICAL", "A1",
            "H3Index typedef not widened to __uint128_t",
            str(H3_API_HEADER)))
    if "__SIZEOF_INT128__" not in text:
        report.add(Finding(
            "CRITICAL", "A1",
            "Missing #error guard for __SIZEOF_INT128__"))


def check_ffi_shim(report: AuditReport) -> None:
    """D7: FFI shim header present + 10 by-pointer prototypes."""
    if not H3_SHIM.exists():
        report.add(Finding("CRITICAL", "D7",
                           "h3ExtShim.h missing", str(H3_SHIM)))
        return
    text = _read_text(H3_SHIM)
    expected = [
        "h3_ext_lat_lng_to_cell",
        "h3_ext_cell_to_lat_lng",
        "h3_ext_cell_to_parent",
        "h3_ext_cell_to_children",
        "h3_ext_cell_to_children_size",
        "h3_ext_is_valid_cell",
        "h3_ext_get_resolution",
        "h3_ext_grid_distance",
        "h3_ext_h3_to_string",
        "h3_ext_string_to_h3",
        "h3_ext_sizeof_h3index",
        "h3_ext_alignof_h3index",
    ]
    for fn in expected:
        if fn not in text:
            report.add(Finding(
                "FINDING", "D7",
                f"FFI shim function `{fn}` missing from h3ExtShim.h"))


def check_pre_commit_hook(report: AuditReport) -> None:
    """H6: pre-commit hook installed."""
    if not PRE_COMMIT_HOOK.exists():
        report.add(Finding(
            "FINDING", "H6",
            "pre-commit hook not installed at .git/hooks/pre-commit"))
        return
    if not os.access(PRE_COMMIT_HOOK, os.X_OK):
        report.add(Finding(
            "FINDING", "H6",
            "pre-commit hook is not executable"))
    text = _read_text(PRE_COMMIT_HOOK)
    if "ctest" not in text:
        report.add(Finding(
            "FINDING", "H6",
            "pre-commit hook does not invoke ctest"))


def _strip_comments(text: str) -> list[str]:
    """Return a list-of-lines with /* ... */ and // ... content blanked out.

    Preserves line numbers so caller can index by 1-based line number.
    Naive but adequate for h3 sources (no string literals containing /*).
    """
    lines = text.splitlines()
    out = []
    in_block = False
    for line in lines:
        result = []
        i = 0
        while i < len(line):
            two = line[i:i + 2]
            if not in_block and two == "//":
                break
            if not in_block and two == "/*":
                in_block = True
                i += 2
                continue
            if in_block and two == "*/":
                in_block = False
                i += 2
                continue
            if in_block:
                i += 1
                continue
            result.append(line[i])
            i += 1
        out.append("".join(result))
    return out


def check_max_h3_res_classification(report: AuditReport) -> None:
    """G3: every MAX_H3_RES site in src/h3lib/lib/*.c classified."""
    c_files = sorted(H3_LIB_DIR.glob("*.c"))
    pattern = re.compile(r"\bMAX_H3_RES\b")

    for c_file in c_files:
        try:
            text = _read_text(c_file)
        except Exception:
            continue
        stripped_lines = _strip_comments(text)
        original_lines = text.splitlines()
        for n, code in enumerate(stripped_lines, start=1):
            if not pattern.search(code):
                continue

            classified = (
                MAX_H3_RES_CLASSIFICATIONS
                .get(c_file.name, {})
                .get(n)
            )
            if classified is None:
                report.add(Finding(
                    "FINDING", "G3",
                    f"unclassified MAX_H3_RES site",
                    f"{c_file.name}:{n} :: {original_lines[n-1].strip()[:80]}"))
            else:
                report.sites_classified += 1
                if classified == "widened":
                    report.sites_widened += 1
                elif classified.startswith("deferred"):
                    report.sites_deferred += 1


def check_mutation_manifest(report: AuditReport) -> None:
    """G2: mutations/manifest.json covers every widening rule."""
    if not MUTATION_MANIFEST.exists():
        report.add(Finding("CRITICAL", "G2",
                           "mutations/manifest.json missing",
                           str(MUTATION_MANIFEST)))
        return
    try:
        manifest = json.loads(_read_text(MUTATION_MANIFEST))
    except json.JSONDecodeError as e:
        report.add(Finding("CRITICAL", "G2",
                           f"mutations/manifest.json invalid: {e}"))
        return
    if not isinstance(manifest, dict) or "mutations" not in manifest:
        report.add(Finding("CRITICAL", "G2",
                           "mutations/manifest.json missing 'mutations' key"))
        return

    rules_seen = {m.get("rule") for m in manifest["mutations"]}
    for rule in REQUIRED_WIDENING_RULES:
        if rule not in rules_seen:
            report.add(Finding(
                "FINDING", "G2",
                f"mutation manifest missing rule `{rule}`"))


def render_human(report: AuditReport, quiet: bool) -> None:
    if not quiet:
        print("=== H3-Extended (128-bit) Audit ===")
        print(f"  Site classifications: {report.sites_classified} "
              f"({report.sites_widened} widened, "
              f"{report.sites_deferred} deferred)")
        print(f"  CRITICAL: {report.critical_count}   "
              f"FINDING: {report.finding_count}")
        for finding in report.findings:
            print(f"  {finding.render()}")
        if report.findings:
            print()
    elif report.findings:
        for finding in report.findings:
            print(finding.render(), file=sys.stderr)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true",
                        help="ci mode — only print on findings")
    parser.add_argument("--json", action="store_true",
                        help="machine-readable JSON output")
    args = parser.parse_args(argv)

    if not REPO_ROOT.exists():
        print(f"ERROR: REPO_ROOT not found: {REPO_ROOT}", file=sys.stderr)
        return 2

    report = AuditReport()
    check_typedef_widened(report)
    check_phase_a_macros(report)
    check_ffi_shim(report)
    check_pre_commit_hook(report)
    check_max_h3_res_classification(report)
    check_mutation_manifest(report)

    if args.json:
        json.dump({
            "critical": report.critical_count,
            "finding": report.finding_count,
            "sites_classified": report.sites_classified,
            "sites_widened": report.sites_widened,
            "sites_deferred": report.sites_deferred,
            "findings": [asdict(f) for f in report.findings],
        }, sys.stdout, indent=2)
        sys.stdout.write("\n")
    else:
        render_human(report, args.quiet)

    if report.critical_count > 0 or report.finding_count > 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
