# Status from Camilla's Claude (camilla-claude)

Updated: 2026-11-07

## 2026-11-07 (session 2): synced to main, brief found, area still open

### Done
- Synced claude-camilla with main (git pull --rebase origin main). Main is at 48225ef. CLAUDE.md with the roster, ownership and merge rules is on main (PR #3 merged 12:09 UTC). The older "needs a merge to main" note is resolved.
- Read every status file in notes/ (rafale-mac.md, cloud-test.md, this one) and the latest commits on origin/claude-rafale (28028d2) and origin/claude/project-thread-psph52 (no new game work).
- Read the project brief on main, the file named "the breif sort of" (commit 48225ef by Jokerman200). It sets the game: "Upgrade", theme Time, 2-player local co-op, C++ with OpenGL (raylib recommended), graphics eras 1-bit to 2.5D, split abilities, Faded and revive, chunk files in /assets/chunks, and milestones. I have not changed it.

### Flags for the team
1. The brief went straight to main. CLAUDE.md says main takes only tested modules, by pull request. Rafale, please confirm this was intended.
2. The brief opens with "# CLAUDE.md". This repo already has a CLAUDE.md with the working rules. Do not copy the brief over it. Suggest renaming the brief (for example GAME_BRIEF.md) in a pull request the team agrees.
3. The brief says the theme, engine, scope and work split were agreed. The older status notes (this file and rafale-mac.md) say they are still open. The team needs to confirm in chat that the brief is the agreed design.
4. CLAUDE.md lists Rafale's branch as "to be agreed", but origin has claude-rafale, and rafale-mac.md is current. Suggest the table names claude-rafale.
5. Teammates 3 and 4 have no names or branches yet.

### Still to do
- Area and owned files: not assigned. Brief section 13 lists the split (core player, systems, art, level and design, audio, UI). I am waiting for Camilla to choose the area. When she does, I will list the owned files here and avoid everyone else's.
- No game code until the area is chosen. Brief milestone: CMake project builds and a window opens by about 14:00 on Sat 7 Nov (UK time).

### Questions for the other Claudes
- Rafale's Claude: which area are you taking? I want to avoid overlap in the Core and Systems areas.

## 2026-11-07 update (session 1)
- Two more people joined, so there are now four Claudes. CLAUDE.md has a roster table, file ownership rules and a merge rule.
- Area and owned files: not assigned yet.

## Done (session 1)
- Attached the repo to Camilla's cloud environment. Setup sets pull.rebase, autostash and auto upstream on push.
- Added CLAUDE.md with shared working rules. It follows the notes/ convention from rafale-mac.md.

## For the other Claudes
- I work on branch claude-camilla and edit only notes/camilla-claude.md plus files I add.
- Before touching any file you created, I will read your status file and ask first.
