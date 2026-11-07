# GameJam: rules for both Claudes

Four Claudes work in this repo, one for each person on the team. Work with each other. Never against.

## Identity and files
- Each person has one Claude. Each Claude has its own branch and its own status file.
- Branch name: claude-<person>. Status file: notes/<person>-claude.md.

| Person | GitHub | Branch | Status file |
| --- | --- | --- | --- |
| Camilla | RafeM99 | claude-camilla | notes/camilla-claude.md |
| Rafale | Jokerman200 | to be agreed (notes/rafale-mac.md is current) | notes/rafale-mac.md |
| Rafale (cloud thread) | Jokerman200 | claude/project-thread-y8u0w2 | notes/rafale-cloud-claude.md |
| Akshat | AkkiCode06 | claude-akshat (from claude-sohan-upgrade) | notes/akshat-claude.md |
| Teammate 3 | add here | claude-<name> | notes/<name>-claude.md |
| Teammate 4 | add here | claude-<name> | notes/<name>-claude.md |

- A new Claude adds its own row to this table on its own branch, creates its branch from main, and creates its own status file.
- Each Claude edits only its own status file. Read all the others.

## Ownership
- In your status file, list the files and folders you own and the area of the game you are building.
- Do not edit files another Claude owns. Ask in your status file and wait for a reply there.
- Put new work in new files or folders. Name shared assets with your area as a prefix.

## Before every task
0. Read CLAUDE.md in full at the start of every session and task. Rules may have changed.
1. git fetch origin and git pull --rebase.
2. Read every other status file in notes/ and the latest commits on the other branches.
3. Note which files the others own or are touching. Avoid them.

## While working
- Stay on your own branch (claude-camilla for Camilla's Claude) at all times. Do all work, commits and pushes there.
- Touch main only when a big module of the game is complete and fully tested. Then merge by pull request. The team agrees in chat first, and any one of the four people may merge. Never push to main for any other reason.
- Commit small and often. Push after every commit.
- Never force push. Never rewrite history. Never delete or overwrite another Claude's files or branch.
- Prefer adding new files over rewriting shared ones. If you must edit a shared file, keep the change small and say so in your status file.
- If a push is rejected, git pull --rebase, resolve conflicts keeping both sides' work, then push again.
- If a conflict cannot be resolved safely, stop and write it in your status file for the humans.

## Commit messages
Use this shape: Done, To do, Notes for the others.

## Status files
- Keep yours current: what you built, what you are doing now, which files you plan to touch, any question for another Claude.
- Newest entry first, with date.

## Merging
- A big module reaches main only when it is complete and fully tested, by pull request. The team agrees in chat first, and any one of the four people may merge.
