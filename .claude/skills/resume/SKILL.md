---
name: resume
description: This skill resumes a previous session by finding and reading the latest context file from the contexts directory. Use this skill when starting a new session, after a context compact, or when the user asks to "resume", "continue where we left off", "pick up from last session", or "load the last context". The skill reads the saved context and continues work on open items.
---

# Resume

## Overview

This skill loads the most recent session context file and resumes work from where it left off. It complements the persist-session skill - persist-session saves context, resume loads it.

## When to Use

Trigger this skill when the user:
- Asks to "resume" or "continue"
- Says "pick up where we left off"
- Asks to "load the last context" or "restore session"
- Starts a new session and wants to continue previous work
- After a context compact when prior work needs to be restored

## Workflow

### Step 1: Find the Latest Context File

Context files are stored in:
```
/Users/johndesposito/amalfi_work/amalfi-h3-128/contexts/
```

Files follow the naming convention: `contexts-<month>-<day>-<YYYYMMDD>-<HHMMSS>.md` and `session-<month>-<day>-*.md`

To find the most recent file (sorted by OS modification timestamp, newest first):
```bash
ls -t /Users/johndesposito/amalfi_work/amalfi-h3-128/contexts/*.md | head -1
```

### Step 2: Read Authoritative Architecture References

Before reading the context file, read ALL files in the architecture reference directory to reload the full system understanding:

```
/Users/johndesposito/amalfi_work/amalfi-h3-128/docs/amalfi-intelligence/
```

This includes:
- `Amalfi_Intelligence_Agentic_Architecture_Reference.pdf` — Supervisor-Specialist pattern, 22-step pipeline, medallion tables
- `Amalfi_Intelligence_Agent_Roster_v2.md` — 14-specialist agent roster, agent_type values, dispatch logic
- `Amalfi_Intelligence_Analysis_Reference.pdf` — 112 analysis types, models per agent, severity triggers
- `Amalfi_Intelligence_Drone_Hardware_Reference_v2.pdf` — Mavic 3 Enterprise specs, fleet deployment
- `Amalfi_Intelligence_GIS_GPS_Reference.pdf` — H3 spatial foundation, property data
- `Amalfi_Intelligence_Drone_Data_Reference.pdf` — 4 raw data types, pipeline flow
- `property_51_navesink.geojson` — Case Study #1 parcel boundary
- `property_grid.html` — H3 grid visualization

Read ALL of these files every time. These are the authoritative source of truth for the entire platform. If any detail in conversation context conflicts with these documents, the documents are authoritative.

### Step 2b: Read Supporting Docs

Also read the following supporting documents to stay current on all scripts and decisions:

```
/Users/johndesposito/amalfi_work/amalfi-h3-128/docs/running_tally_all_scripts.md
```

This is the running tally of every script created or modified across all sessions. Use it to:
- Know what files exist and their purpose before suggesting new ones
- Avoid creating duplicates
- Understand the full scope of work done across both the legacy crypto platform and the Amalfi Intelligence Platform

### Step 3: Read and Parse the Context File

Read the latest context file. The file contains four sections:

1. **Summary** - Session goals, completed tasks, current state, key findings
2. **Files Created/Modified** - Table of files touched in that session
3. **Open Items** - Unfinished tasks with status, remaining steps, and next actions
4. **Context Dump** - Full conversation history (in collapsible details)

### Step 4: Present Resume Summary

After reading, present a concise summary to the user:

```markdown
## Resuming from: [filename]

**Previous Session Goal**: [goal from summary]

**Completed**:
- [completed items]

**Open Items**:
1. [Open item 1] - [status]
2. [Open item 2] - [status]

**Recommended Next Action**: [first next action from open items]

Ready to continue. What would you like to work on?
```

### Step 5: Continue Work

Based on user direction:
- If user confirms, proceed with the recommended next action
- If user specifies a different task, work on that instead
- Reference the context file for any needed details (commands, file paths, etc.)

## Key Information to Extract

When reading the context file, focus on:

1. **Open Items section** - This is the primary source for what needs to be done
   - Task names and their status (In Progress, Blocked, Not Started)
   - Specific remaining steps
   - Blockers that need resolution
   - Next actions with exact commands

2. **Files Created/Modified** - Know what files exist and their purpose

3. **Commands Reference** - If present, contains ready-to-run commands

4. **Current State** - Understanding of where things stand

## Example Resume Flow

```
User: resume

Claude: Let me find the latest context file...

[Reads /Users/johndesposito/amalfi_work/amalfi-h3-128/contexts/contexts-jan-23-20260123-101224.md]

## Resuming from: contexts-jan-23-20260123-101224.md

**Previous Session Goal**: Webhook prompt guard testing + persist-session skill creation

**Completed**:
- Created persist-session skill
- Created /save-session and /ss commands
- Diagnosed webhook endpoint issues

**Open Items**:
1. Fix Webhook Action Enum Format - **Blocked** (need correct enum values)
2. Verify Slash Commands Work - **Not Started**
3. Add Insomnia Tests - **Not Started**

**Recommended Next Action**: Rebuild Docker image with capitalized "Allow"/"Deny" values and test

Ready to continue. What would you like to work on?
```
