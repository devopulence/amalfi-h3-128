---
name: persist-session
description: This skill persists the current conversation session to a markdown file for context continuity. Use this skill when the user asks to save the session, persist context, capture the conversation, or wants to ensure work can be resumed after a context compact or new session. The skill creates a structured file with all details needed to pick up exactly where the session left off.
---

# Persist Session

## Overview

This skill saves the current conversation to a structured markdown file that enables seamless session continuity. The output file contains enough detail for Claude to pick up exactly where the conversation left off after a context compact, session end, or when starting a new conversation.

## When to Use

Trigger this skill when the user:
- Asks to "save the session" or "persist the context"
- Wants to "capture the conversation" or "save progress"
- Mentions context limits, compacting, or resuming later
- Says "save this so we can continue later"

## Workflow

### Step 1: Determine Output Location

Ask the user for the target directory if not already known. Common locations:
- Project-specific: `<project-root>/contexts/`
- General: `~/.claude/contexts/`

If the user has established a convention (like `contexts/` in the current project), use that.

### Step 2: Generate Filename

Use this naming convention:
```
contexts-<month>-<day>-<YYYYMMDD>-<HHMMSS>.md
```

Get the timestamp in the user's local timezone (default: America/New_York):
```bash
TZ='America/New_York' date '+%Y%m%d-%H%M%S'
```

Example: `contexts-jan-23-20260123-094025.md`

### Step 3: Create the Context File

Generate a markdown file with four required sections:

#### Section 1: Summary

Provide a comprehensive summary including:
- **Session goal**: What the user was trying to accomplish
- **Tasks attempted**: All tasks worked on during the session
- **Tasks completed**: What was successfully finished
- **Current state**: Where things stand at session end
- **Key decisions made**: Important choices or findings
- **Blockers encountered**: Any issues that stopped progress

#### Section 2: Files Created/Modified

List every file created or modified during the session:

```markdown
## Files Created/Modified

| File | Action | Purpose |
|------|--------|---------|
| `path/to/file.py` | Created | Description of what this file does |
| `path/to/config.yaml` | Modified | What was changed and why |
```

Include:
- Full file paths (relative to project root)
- Action type: Created, Modified, Deleted
- Brief purpose/description

#### Section 3: Open Items

List all unfinished work:

```markdown
## Open Items

### 1. [Task Name]
- **Status**: In Progress / Blocked / Not Started
- **What remains**: Specific steps still needed
- **Blocker (if any)**: What's preventing completion
- **Next action**: The immediate next step to take

### 2. [Another Task]
...
```

Be specific enough that work can resume without re-reading the entire conversation.

#### Section 4: Context Dump

Include the full conversation in a format that can be reviewed if needed:

```markdown
## Context Dump

<details>
<summary>Full Conversation (click to expand)</summary>

[Paste complete conversation here, preserving formatting]

</details>
```

Use `<details>` tags to keep the file scannable while preserving full context.

### Step 4: Update Running Script Tally

After creating the context file, update `docs/running_tally_all_scripts.md` with any new or modified scripts from this session.

**Rules:**
1. Read the current `docs/running_tally_all_scripts.md` file first
2. For each `.py`, `.sh`, or `Dockerfile.*` created or modified in this session:
   - **New script**: Add a row to the correct category table, sorted by date DESCENDING within that table (newest first)
   - **Modified script**: Update the existing row's Description column in-place (do NOT add a duplicate row)
   - **New category needed**: Add a new section following the existing format (H2 heading, table with Script/Created/Description columns)
3. Update the `Last Updated` date in the header
4. Update the Summary count table at the bottom
5. Preserve the exact markdown format: `| \`script_name.py\` | YYYY-MM-DD | Description |`
6. Use TODAY's date (the session date) as the Created date — do NOT use git commit dates (commits are batched and lag behind actual creation)

**Category Reference:**
- `Backtesting & Signal Detection` — backtest_*, detect_*, find_*, analyze_*, verify_*
- `Live Trading / WebSocket Systems` — websocket_* (split into Main Orchestrators, Modular Systems, Shared Modules subsections)
- `Data Fetching` — fetch_phemex_*
- `Amplitude Analysis & Coin Screening` — check_*, find_amplitude_*
- `AWS Infrastructure & Utilities` — assign_*, telegram_*, trade_guard*, cancel_*
- `Deployment (Dockerfiles & Shell)` — Dockerfile.*, scripts/*.sh

**Important:** Do NOT pollute the file with session metadata, timestamps of updates, or changelog entries. Each script appears exactly once in its category table. Updates replace the existing row silently.

### Step 5: Write and Confirm

1. Check if target directory exists (don't create if it already exists)
   ```bash
   [ -d "/path/to/contexts" ] || mkdir -p "/path/to/contexts"
   ```
2. Write the context file
3. Write the updated `docs/running_tally_all_scripts.md` (if changes were made)
4. Confirm to the user with both file paths

## Template

Reference the template at `references/context-template.md` for the exact structure to follow.

## Example Output Structure

```markdown
# Session Context - January 23, 2026

## Summary

**Goal**: [What the user wanted to accomplish]

**Completed**:
- Task 1 description
- Task 2 description

**In Progress**:
- Task 3 - stopped at [specific point]

**Key Findings**:
- Finding 1
- Finding 2

---

## Files Created/Modified

| File | Action | Purpose |
|------|--------|---------|
| `src/app.py` | Created | Main application entry point |
| `config.yaml` | Modified | Added new environment variable |

---

## Open Items

### 1. Fix API Response Format
- **Status**: In Progress
- **What remains**: Need to test with correct enum values
- **Next action**: Rebuild Docker image and test endpoint

---

## Context Dump

<details>
<summary>Full Conversation</summary>

[Full conversation content...]

</details>
```
