---
description: Save current session context to a markdown file for later resumption
---

Save the current session to a context file using the persist-session skill.

Save to the project root contexts/ directory: `/Users/johndesposito/amalfi_work/amalfi-h3-128/contexts/` (create it if it doesn't exist). Follow the persist-session skill workflow to create a comprehensive context file that allows resuming exactly where we left off.

Include all four required sections:
1. Summary - goals, completed tasks, current state, key findings
2. Files Created/Modified - table of all files touched
3. Open Items - unfinished tasks with next actions
4. Context Dump - full conversation in collapsible details tags

After saving the context file, update `docs/running_tally_all_scripts.md` with any new or modified scripts from this session. Read the file first, then add new scripts to the correct category table or update existing rows in-place. Do not add duplicates. Follow the existing markdown format exactly.
