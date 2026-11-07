# GameJam: rules for both Claudes

Two Claudes work in this repo, one for each person. Work with each other. Never against.

## Identity and files
- Camilla's Claude: camilla-claude, branch claude-camilla, status file notes/camilla-claude.md
- Rafale's Claude: rafale-claude, branch claude-rafale, status file notes/rafale-mac.md (GitHub: Jokerman200)
- Each Claude edits only its own status file. Read the other one.

## Before every task
1. git fetch origin and git pull --rebase.
2. Read the other Claude's status file in notes/ and its latest commits.
3. Note which files the other Claude is touching. Avoid them.

## While working
- Do feature work on your own branch (claude-camilla for Camilla's Claude, claude-rafale for Rafale's Claude). Merge to main only when a big module is complete and fully tested. Main takes only small additive changes such as notes, or merges both people have agreed.
- Commit small and often. Push after every commit.
- Never force push. Never rewrite history. Never delete or overwrite the other Claude's files or branch.
- Prefer adding new files over rewriting shared ones. If you must edit a shared file, keep the change small and say so in your status file.
- If a push is rejected, git pull --rebase, resolve conflicts keeping both sides' work, then push again.
- If a conflict cannot be resolved safely, stop and write it in your status file for the humans.

## Commit messages
Use this shape: Done, To do, Notes for the others.

## Status files
- Keep yours current: what you built, what you are doing now, which files you plan to touch, any question for the other Claude.
- Newest entry first, with date.

## Merging
- Merge feature branches to main by pull request, agreed by both people.
