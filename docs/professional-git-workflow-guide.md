# Professional Git Workflow Guide

**Author:** Chun  
**Date:** December 29, 2025  
**Purpose:** Professional Git practices for clean history, proper versioning, and team collaboration

---

## Table of Contents

1. [Git Fundamentals](#git-fundamentals)
2. [Professional Daily Workflow](#professional-daily-workflow)
3. [Understanding Fetch vs Pull vs Rebase](#understanding-fetch-vs-pull-vs-rebase)
4. [Commit Best Practices](#commit-best-practices)
5. [Branch Management](#branch-management)
6. [Tags and Versioning](#tags-and-versioning)
7. [Clean History Management](#clean-history-management)
8. [Team Collaboration](#team-collaboration)
9. [Common Scenarios and Solutions](#common-scenarios-and-solutions)
10. [Git Commands Quick Reference](#git-commands-quick-reference)

---

## Git Fundamentals

### The Three States of Git

```bash
┌─────────────────────────────────────────────────────────────┐
│                    Your Computer                            │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Working Directory          Staging Area        Repository  │
│  (your files)              (git add)            (git commit)│
│                                                             │
│  file.txt (modified) ──→  file.txt (staged) ──→ Commit ✅   │
│       ↑                        ↑                    ↓       │
│  You edit here          git add here         git commit     │
│                                                             │
└─────────────────────────────────────────────────────────────┘
                                ↓ git push
┌─────────────────────────────────────────────────────────────┐
│                         GitHub/GitLab                       │
│                    (Remote Repository)                      │
└─────────────────────────────────────────────────────────────┘
```

### Commit vs Stash

| Aspect | `git commit` | `git stash` |
|--------|-------------|-------------|
| **Purpose** | Save **finished** work | Save **incomplete** work temporarily |
| **Storage** | Branch history (permanent) | Stash stack (temporary) |
| **Visibility** | `git log` shows it | `git stash list` shows it |
| **Shareable** | ✅ Yes (via push) | ❌ No (local only) |
| **Can be lost** | ❌ Nearly impossible | ✅ Easy to lose |
| **Use for** | Complete features, checkpoints | Quick context switches |
| **Part of history** | ✅ Yes | ❌ No |

**Rule of thumb:**
- **Commit:** "This is done (or at a good checkpoint)"
- **Stash:** "I'll come back to this later today"

---

## Professional Daily Workflow

### The Gold Standard: Fetch → Rebase → Push

This is what **professional kernel developers** and **major open source projects** use.

```bash
# ========== MORNING: Start of day ==========

# 1. Check status
git status

# 2. Pull latest changes (if no local work)
git pull --rebase origin main

# ========== DURING DAY: Working ==========

# 3. Make changes
vim my_driver.c

# 4. Stage and commit frequently (logical checkpoints)
git add my_driver.c
git commit -m "feat: Add GPIO interrupt handler"

# Continue working...
git add my_driver.c
git commit -m "feat: Add error handling for GPIO"

# ========== END OF DAY: Share work ==========

# 5. Fetch latest from team
git fetch origin

# 6. Check what's new
git log HEAD..origin/main --oneline

# 7. Rebase your commits on top
git rebase origin/main

# 8. Push to share with team
git push origin main

# ========== DONE! ==========
```

**Why this workflow?**
- ✅ **Clean linear history** (no merge commits)
- ✅ **Easy to review** (git log is readable)
- ✅ **Professional standard** (used by Linux kernel, Git itself, etc.)
- ✅ **Easy to bisect** (find bugs with binary search)
- ✅ **Clear commit messages** (each commit is meaningful)

---

## Understanding Fetch vs Pull vs Rebase

### Visual Comparison

#### **Scenario: You and teammate both made commits**

```bash
Initial state (both started from C):
  Origin (GitHub):  A ← B ← C
  Your local:       A ← B ← C
  Teammate's local: A ← B ← C

After you both work:
  Origin (GitHub):  A ← B ← C ← D ← E (teammate pushed)
  Your local:       A ← B ← C ← X ← Y (your commits)
```

---

### Option 1: `git fetch` (Safe, No Changes)

**What it does:** Downloads commits but **doesn't touch your files**.

```bash
git fetch origin
```

**Result:**
```bash
Origin (GitHub):     A ← B ← C ← D ← E
                                    ↑
                              (origin/main)

Your local commits:  A ← B ← C ← X ← Y
                                    ↑
                                  (main)

Your files: UNCHANGED
Your commits: UNCHANGED
Knowledge: UPDATED (you now know about D and E)
```

**When to use:**

- ✅ Always as first step (safest)
- ✅ To check what's new before merging
- ✅ Multiple times per day (can't hurt)

---

### Option 2: `git pull` (Fetch + Merge)

**What it does:** Downloads commits AND merges them.

```bash
git pull origin main
# Equivalent to:
# git fetch origin
# git merge origin/main
```

**Result:**
```
A ← B ← C ← D ← E
         ↘     ↗
           X ← Y ← [MERGE] ← Creates merge commit
                      ↑
                  Messy history!
```

**Pros:**
- ✅ Simple (one command)
- ✅ Works even with conflicts

**Cons:**
- ❌ Creates merge commits (messy history)
- ❌ Not professional standard
- ❌ Hard to read history

**When to use:**
- ⚠️ Personal projects only
- ⚠️ If you don't care about clean history

---

### Option 3: `git pull --rebase` (Fetch + Rebase)

**What it does:** Downloads commits AND replays yours on top.

```bash
git pull --rebase origin main
# Equivalent to:
# git fetch origin
# git rebase origin/main
```

**Result:**
```
A ← B ← C ← D ← E ← X' ← Y'
            ↑           ↑
      (origin/main)  (main)
      
Clean linear history! ✅
```

**Pros:**
- ✅ Clean linear history
- ✅ Professional standard
- ✅ Easy to review
- ✅ One command

**Cons:**
- ⚠️ Slightly more complex if conflicts
- ⚠️ Rewrites commit IDs (X → X', Y → Y')

**When to use:**
- ✅ **Always** (make it your default!) ⭐
- ✅ Professional projects
- ✅ Team development
- ✅ Open source contributions

---

### Recommended: Fetch THEN Rebase (Safest)

**The professional way:**

```bash
# 1. Fetch (safe, no changes)
git fetch origin

# 2. Review what's new
git log HEAD..origin/main --oneline
git diff HEAD..origin/main --stat

# 3. If looks good, rebase
git rebase origin/main

# 4. Push your work
git push origin main
```

**Why separate steps?**
- ✅ Review before applying changes
- ✅ See exactly what teammates did
- ✅ Decide if rebase is appropriate
- ✅ More control over the process

---

## Commit Best Practices

### The Art of Good Commits

#### **1. Commit Often (Logical Checkpoints)**

**Bad: One giant commit**
```bash
# After 3 days of work...
git commit -m "Update driver"  # ❌ What changed?
```

**Good: Small, logical commits**
```bash
git commit -m "feat: Add GPIO initialization"
git commit -m "feat: Add IRQ handler"
git commit -m "feat: Add sysfs interface"
git commit -m "docs: Update GPIO driver documentation"
```

**Rule:** Each commit should be **one logical change**.

---

#### **2. Write Good Commit Messages**

**Format (Conventional Commits):**

```
<type>(<scope>): <subject>

<body>

<footer>
```

**Types:**
- `feat:` New feature
- `fix:` Bug fix
- `docs:` Documentation only
- `style:` Code style (formatting, no logic change)
- `refactor:` Code restructuring (no feature/bug change)
- `test:` Adding tests
- `chore:` Maintenance (build, dependencies)
- `perf:` Performance improvement

**Examples:**

```bash
# Good: Clear and specific
git commit -m "feat(button): Add GPIO interrupt support"

# Good: Multi-line for complex changes
git commit -m "fix(i2c): Resolve race condition in TMP117 driver

The temperature read was not properly locked, causing occasional
incorrect readings under high load.

- Add mutex around I2C transactions
- Add timeout for I2C operations
- Update documentation

Fixes #123"

# Bad: Vague
git commit -m "Fix stuff"  # ❌

# Bad: Too generic
git commit -m "Update code"  # ❌

# Bad: No type prefix
git commit -m "added new feature"  # ❌
```

---

#### **3. Keep Commits Atomic**

**Atomic commit** = One complete, self-contained change

**Bad: Mixed concerns**
```bash
# Changes 3 unrelated things
git add button_driver.c i2c_driver.c README.md
git commit -m "Various updates"  # ❌
```

**Good: Separate commits**
```bash
# Commit 1: Button driver
git add button_driver.c
git commit -m "feat(button): Add debouncing logic"

# Commit 2: I2C driver
git add i2c_driver.c
git commit -m "fix(i2c): Fix address validation"

# Commit 3: Documentation
git add README.md
git commit -m "docs: Add installation instructions"
```

**Benefits:**
- ✅ Easy to review
- ✅ Easy to revert specific change
- ✅ Easy to cherry-pick
- ✅ Clear history

---

#### **4. Test Before Committing**

```bash
# Build and test
make
./test.sh

# If tests pass, commit
git add my_driver.c
git commit -m "feat: Add new sensor support"

# If tests fail, fix first!
```

**Rule:** Never commit broken code to main branch.

---

### Commit Message Template

Create `~/.gitmessage`:

```bash
# <type>(<scope>): <subject> (max 50 chars)
# |<----  Using a Maximum Of 50 Characters  ---->|

# Explain why this change is being made
# |<----   Try To Limit Each Line to a Maximum Of 72 Characters   ---->|

# Provide links or keys to any relevant tickets, articles or other resources
# Example: Fixes #23

# --- COMMIT END ---
# Type can be:
#    feat     (new feature)
#    fix      (bug fix)
#    refactor (refactoring code)
#    style    (formatting, missing semi colons, etc; no code change)
#    docs     (changes to documentation)
#    test     (adding or refactoring tests; no production code change)
#    chore    (updating build tasks, package manager configs, etc)
# --------------------
# Remember to:
#   - Capitalize the subject line
#   - Use the imperative mood in the subject line
#   - Do not end the subject line with a period
#   - Separate subject from body with a blank line
#   - Use the body to explain what and why vs. how
#   - Can use multiple lines with "-" for bullet points in body
```

**Enable it:**
```bash
git config --global commit.template ~/.gitmessage
```

---

## Branch Management

### Professional Branching Strategy

#### **Main/Master Branch Rules**

```bash
# main branch should ALWAYS:
- ✅ Build successfully
- ✅ Pass all tests
- ✅ Be deployable
- ✅ Have clean history
```

#### **Feature Branch Workflow**

```bash
# 1. Start from latest main
git checkout main
git pull --rebase origin main

# 2. Create feature branch
git checkout -b feature/add-mcp3008-driver

# 3. Work on feature (commit often!)
git commit -m "feat: Add MCP3008 basic structure"
git commit -m "feat: Add SPI communication"
git commit -m "feat: Add IIO interface"

# 4. Keep feature branch updated with main
git fetch origin
git rebase origin/main  # Replay feature commits on latest main

# 5. When feature is complete, merge to main
git checkout main
git merge --ff-only feature/add-mcp3008-driver  # Fast-forward merge
# or
git rebase feature/add-mcp3008-driver

# 6. Push to origin
git push origin main

# 7. Delete feature branch (cleanup)
git branch -d feature/add-mcp3008-driver
```

---

### Branch Naming Conventions

```bash
# Feature branches
feature/add-gpio-driver
feature/implement-iio-subsystem

# Bug fix branches
fix/button-debounce-issue
fix/i2c-timeout-error

# Documentation branches
docs/update-installation-guide
docs/add-driver-architecture-diagram

# Refactoring branches
refactor/split-hardware-layer
refactor/cleanup-error-handling

# Experimental branches
experiment/test-new-algorithm
experiment/alternative-implementation
```

---

### Working with Branches

```bash
# List all branches
git branch -a

# List with last commit
git branch -v

# Create and switch to new branch
git checkout -b feature/new-feature

# Switch to existing branch
git checkout main

# Delete local branch
git branch -d feature/old-feature

# Delete remote branch
git push origin --delete feature/old-feature

# Rename current branch
git branch -m new-name

# See which branches are merged
git branch --merged
git branch --no-merged
```

---

## Tags and Versioning

### Semantic Versioning (SemVer)

**Format:** `MAJOR.MINOR.PATCH` (e.g., `v1.2.3`)

```
v1.2.3
│ │ │
│ │ └─── PATCH: Bug fixes (backward compatible)
│ └───── MINOR: New features (backward compatible)
└─────── MAJOR: Breaking changes (NOT backward compatible)
```

**Examples:**
- `v1.0.0` → `v1.0.1` - Fixed a bug
- `v1.0.1` → `v1.1.0` - Added new feature
- `v1.1.0` → `v2.0.0` - Changed API (breaking change)

---

### Creating Tags

#### **Lightweight Tag** (Simple pointer)

```bash
# Create tag
git tag v1.0.0

# Push tag
git push origin v1.0.0
```

#### **Annotated Tag** (Recommended for releases)

```bash
# Create annotated tag
git tag -a v1.0.0 -m "Release version 1.0.0

Features:
- GPIO button driver
- I2C TMP117 driver
- SPI MCP3008 driver
- Complete documentation

Tested on BeagleBone Black with Yocto 4.0"

# Push tag
git push origin v1.0.0

# Push all tags
git push origin --tags
```

---

### Versioning Workflow

```bash
# 1. Prepare release
git checkout main
git pull --rebase origin main

# 2. Update version in files
vim VERSION
vim README.md  # Update version number
git commit -m "chore: Bump version to 1.0.0"

# 3. Create tag
git tag -a v1.0.0 -m "Release v1.0.0"

# 4. Push everything
git push origin main
git push origin v1.0.0

# 5. Create release on GitHub
# Go to GitHub → Releases → Create new release
# - Tag: v1.0.0
# - Title: "Version 1.0.0"
# - Description: Release notes
# - Attach binaries if needed
```

---

### Tag Commands

```bash
# List all tags
git tag

# List tags matching pattern
git tag -l "v1.*"

# Show tag details
git show v1.0.0

# Checkout specific tag
git checkout v1.0.0

# Delete local tag
git tag -d v1.0.0

# Delete remote tag
git push origin --delete v1.0.0

# Tag older commit
git tag -a v1.0.0 abc1234 -m "Release 1.0.0"
```

---

### Version Numbering Examples

```bash
# Initial development
v0.1.0 - First working prototype
v0.2.0 - Add basic features
v0.9.0 - Feature complete, testing

# First stable release
v1.0.0 - First stable release

# Bug fixes
v1.0.1 - Fix GPIO initialization bug
v1.0.2 - Fix I2C timeout issue

# New features (backward compatible)
v1.1.0 - Add SPI support
v1.2.0 - Add IIO interface
v1.3.0 - Add input subsystem support

# Breaking changes
v2.0.0 - New API (not compatible with v1.x)
v3.0.0 - Major rewrite
```

---

## Clean History Management

### Interactive Rebase (Powerful!)

**Use case:** Clean up commits before pushing.

```bash
# View last 3 commits
git log --oneline -3
abc1234 Fix typo
def5678 Add feature part 2
ghi9012 Add feature part 1

# Interactive rebase last 3 commits
git rebase -i HEAD~3
```

**Editor opens:**
```bash
pick ghi9012 Add feature part 1
pick def5678 Add feature part 2
pick abc1234 Fix typo

# Commands:
# p, pick = use commit
# r, reword = use commit, but edit message
# e, edit = use commit, but stop for amending
# s, squash = use commit, but meld into previous commit
# f, fixup = like "squash", but discard this commit's message
# d, drop = remove commit
```

**Common operations:**

#### **1. Squash commits (combine)**

```bash
pick ghi9012 Add feature part 1
squash def5678 Add feature part 2  # Combine with previous
squash abc1234 Fix typo            # Combine with previous

# Result: One commit "Add feature part 1" with all changes
```

#### **2. Reword commit message**

```bash
pick ghi9012 Add feature part 1
reword def5678 Add feature part 2  # Will prompt to change message
pick abc1234 Fix typo
```

#### **3. Reorder commits**

```bash
pick abc1234 Fix typo              # Move to first
pick ghi9012 Add feature part 1
pick def5678 Add feature part 2
```

#### **4. Drop commits**

```bash
pick ghi9012 Add feature part 1
drop def5678 Add feature part 2    # Remove this commit
pick abc1234 Fix typo
```

---

### Amending Last Commit

```bash
# Forgot to add a file to last commit
git add forgotten_file.c
git commit --amend --no-edit  # Add to last commit, keep message

# Fix commit message
git commit --amend -m "Better commit message"

# ⚠️ WARNING: Only amend if NOT pushed yet!
```

---

### Fixing Mistakes

#### **Undo last commit (keep changes)**

```bash
git reset --soft HEAD~1
# Commit is undone, changes still staged
```

#### **Undo last commit (discard changes)**

```bash
git reset --hard HEAD~1
# ⚠️ Dangerous! Changes are LOST!
```

#### **Revert a commit (safe way)**

```bash
# Creates new commit that undoes old commit
git revert abc1234
# Safe: History is preserved, old commit still there
```

---

## Team Collaboration

### Before Pushing: The Checklist

```bash
# ✅ 1. All files committed?
git status  # Should show "nothing to commit"

# ✅ 2. Code compiles?
make clean && make

# ✅ 3. Tests pass?
./run_tests.sh

# ✅ 4. Commit messages good?
git log --oneline -5

# ✅ 5. Up to date with origin?
git fetch origin
git log HEAD..origin/main  # Should be empty

# ✅ 6. Rebase if needed
git rebase origin/main

# ✅ 7. Push
git push origin main
```

---

### Handling Conflicts During Rebase

```bash
# Start rebase
git rebase origin/main

# Git says:
CONFLICT (content): Merge conflict in driver.c
error: could not apply abc1234... feat: Add new feature

# 1. Check which files have conflicts
git status
# Unmerged paths:
#   both modified:   driver.c

# 2. Open conflicting file
vim driver.c

# You'll see:
<<<<<<< HEAD (origin/main - their version)
code from origin/main
=======
code from your commit
>>>>>>> abc1234 (feat: Add new feature - your version)

# 3. Resolve conflict (choose or merge both)
# Edit file to final desired state
# Remove markers (<<<<<<<, =======, >>>>>>>)

# 4. Stage resolved file
git add driver.c

# 5. Continue rebase
git rebase --continue

# 6. If more conflicts, repeat steps 2-5

# 7. After all resolved, push
git push origin main
```

**Abort rebase if stuck:**
```bash
git rebase --abort  # Go back to state before rebase
```

---

### Pull Requests / Merge Requests

**Professional workflow:**

```bash
# 1. Create feature branch
git checkout -b feature/add-new-driver

# 2. Work and commit
git commit -m "feat: Add driver skeleton"
git commit -m "feat: Implement probe function"
git commit -m "docs: Add driver documentation"

# 3. Keep updated with main
git fetch origin
git rebase origin/main

# 4. Push feature branch
git push origin feature/add-new-driver

# 5. Create Pull Request on GitHub
# - Go to GitHub repository
# - Click "Compare & pull request"
# - Fill in description
# - Request reviewers
# - Link related issues

# 6. Address review comments
git commit -m "fix: Address review comments"
git push origin feature/add-new-driver

# 7. After approval and merge, delete branch
git branch -d feature/add-new-driver
git push origin --delete feature/add-new-driver
```

---

### Code Review Best Practices

**As author:**
- ✅ Small PRs (< 400 lines preferred)
- ✅ Clear description
- ✅ Self-review before requesting
- ✅ Add tests
- ✅ Update documentation

**As reviewer:**
- ✅ Be constructive
- ✅ Ask questions
- ✅ Suggest improvements
- ✅ Test the code
- ✅ Approve only when ready

---

## Common Scenarios and Solutions

### Scenario 1: Need to update from origin with local changes

```bash
# You have uncommitted changes
git status
# modified: driver.c

# Option A: Commit first (if work is done)
git add driver.c
git commit -m "feat: Add feature"
git pull --rebase origin main

# Option B: Stash (if work is incomplete)
git stash
git pull --rebase origin main
git stash pop
```

---

### Scenario 2: Pushed wrong code to main

```bash
# ⚠️ If you're the only one who pulled it:
git reset --hard HEAD~1  # Undo locally
git push --force origin main  # ⚠️ Dangerous!

# ✅ Better: Revert (safe for team)
git revert HEAD  # Creates new commit that undoes
git push origin main
```

---

### Scenario 3: Want to keep main clean while experimenting

```bash
# Create experiment branch
git checkout -b experiment/new-idea

# Experiment freely
git commit -m "Try approach 1"
git commit -m "Try approach 2"

# If experiment works:
git checkout main
git merge experiment/new-idea

# If experiment fails:
git checkout main
git branch -D experiment/new-idea  # Delete branch
```

---

### Scenario 4: Need specific commits from another branch

```bash
# Cherry-pick specific commits
git checkout main
git cherry-pick abc1234  # Apply commit abc1234 to main
git cherry-pick def5678  # Apply another commit
```

---

### Scenario 5: Working on multiple machines (WSL and DELL)

```bash
# On WSL (Windows):
git commit -m "feat: Add feature on WSL"
git fetch origin
git rebase origin/main
git push origin main

# On DELL (Linux):
git fetch origin
git rebase origin/main  # Get WSL's changes
# Continue working...
git commit -m "feat: Add feature on DELL"
git push origin main

# Back on WSL:
git pull --rebase origin main  # Get DELL's changes
```

---

### Scenario 6: Accidentally committed to wrong branch

```bash
# You committed to main instead of feature branch
git log --oneline -1
abc1234 feat: New feature

# Create feature branch from current position
git branch feature/new-feature

# Reset main to before your commit
git reset --hard origin/main

# Switch to feature branch (has your commit)
git checkout feature/new-feature
```

---

## Git Commands Quick Reference

### Daily Commands

```bash
# Check status
git status
git status -s  # Short format

# View changes
git diff               # Unstaged changes
git diff --staged      # Staged changes
git diff origin/main   # Compare with origin

# Stage files
git add file.txt       # Stage specific file
git add .              # Stage all changes
git add -p             # Interactive staging

# Commit
git commit -m "message"
git commit --amend     # Fix last commit

# View history
git log
git log --oneline -10  # Last 10 commits, compact
git log --graph        # Visual branch graph

# Update from origin
git fetch origin
git pull --rebase origin main

# Push to origin
git push origin main
git push origin --tags  # Push tags

# Branch operations
git branch                    # List branches
git branch feature/new        # Create branch
git checkout feature/new      # Switch branch
git checkout -b feature/new   # Create and switch
git branch -d feature/old     # Delete branch
```

---

### Stash Commands

```bash
# Save changes temporarily
git stash
git stash push -m "Work in progress"

# List stashes
git stash list

# Apply stash
git stash pop     # Apply and remove
git stash apply   # Apply but keep

# View stash contents
git stash show
git stash show -p  # Show diff

# Remove stash
git stash drop
git stash clear    # Remove all
```

---

### Advanced Commands

```bash
# Interactive rebase
git rebase -i HEAD~3

# Cherry-pick
git cherry-pick abc1234

# Revert commit
git revert abc1234

# Reset
git reset --soft HEAD~1   # Undo commit, keep changes staged
git reset --mixed HEAD~1  # Undo commit, unstage changes
git reset --hard HEAD~1   # ⚠️ Undo commit, discard changes

# Reflog (recover "lost" commits)
git reflog
git checkout abc1234

# Bisect (find bug)
git bisect start
git bisect bad           # Current commit is bad
git bisect good v1.0.0   # v1.0.0 was good
# Git will check out commits to test
git bisect good/bad      # Mark each test
git bisect reset         # Done

# Clean working directory
git clean -n   # Dry run
git clean -f   # Remove untracked files
git clean -fd  # Remove untracked files and directories
```

---

### Configuration Commands

```bash
# User info
git config --global user.name "Your Name"
git config --global user.email "your@email.com"

# Default branch
git config --global init.defaultBranch main

# Editor
git config --global core.editor "vim"

# Aliases
git config --global alias.co checkout
git config --global alias.br branch
git config --global alias.ci commit
git config --global alias.st status
git config --global alias.lg "log --oneline --graph --all"

# Pull behavior
git config --global pull.rebase true  # Always rebase on pull

# View config
git config --list
git config user.name
```

---

### Git Aliases (Add to ~/.gitconfig)

```ini
[alias]
    # Short commands
    co = checkout
    br = branch
    ci = commit
    st = status
    
    # Log variants
    lg = log --oneline --graph --all --decorate
    lp = log --pretty=format:'%C(yellow)%h%Creset %C(blue)%an%Creset %C(green)%ar%Creset %s'
    
    # Diff variants
    df = diff
    dc = diff --cached
    
    # Stash variants
    sl = stash list
    sp = stash pop
    ss = stash show -p
    
    # Branch management
    brd = branch -d
    brdel = push origin --delete
    
    # Sync commands
    sync = !git fetch origin && git rebase origin/main
    syncf = !git fetch origin && git rebase origin/$(git branch --show-current)
    
    # Undo commands
    undo = reset --soft HEAD~1
    unstage = reset HEAD --
    
    # Clean commands
    wipe = !git reset --hard && git clean -fd
    
    # Info commands
    who = shortlog -sn --no-merges
    recent = for-each-ref --sort=-committerdate --format='%(refname:short)' refs/heads/
```

---

## Workflow Cheat Sheet

### Morning Routine

```bash
cd /path/to/project
git status
git pull --rebase origin main
# Start working...
```

### During Work

```bash
# After each logical change:
git add changed_files
git commit -m "type: clear message"

# Every hour or so:
git fetch origin
```

### End of Day

```bash
# Share your work:
git fetch origin
git rebase origin/main
git push origin main
```

### Before Creating PR

```bash
# Clean up commits
git rebase -i origin/main

# Verify
git log --oneline origin/main..HEAD
git diff origin/main

# Push
git push origin feature/branch-name
```

---

## Professional Git Mindset

### The Golden Rules

1. **Commit early, commit often** (but keep commits logical)
2. **Never commit broken code to main**
3. **Write clear commit messages** (your future self will thank you)
4. **Rebase before pushing** (keep history clean)
5. **Review before pushing** (git log, git diff)
6. **Never force push to main** (unless you're absolutely sure)
7. **Use branches for features** (protect main)
8. **Delete merged branches** (keep repository clean)
9. **Tag releases** (easy to find versions)
10. **Communicate with team** (before force operations)

---

### Clean History Example

**Bad history:**
```bash
* Fix typo
* Fix bug
* WIP
* More changes
* Fix review comments
* Actually fix the bug
* Update documentation
```

**Good history (after squashing):**
```
* feat: Add GPIO interrupt support
* test: Add unit tests for GPIO driver
* docs: Document GPIO interrupt configuration
```

**How to achieve:**
```bash
# Before pushing:
git rebase -i origin/main
# Squash related commits
# Reword messages to be clear
git push origin main
```

---

### Learning Resources

**Official Git:**
- [Git Documentation](https://git-scm.com/doc)
- [Pro Git Book](https://git-scm.com/book/en/v2) (Free online)

**Interactive Learning:**
- [Learn Git Branching](https://learngitbranching.js.org/)
- [Git Immersion](http://gitimmersion.com/)

**Commit Message Guidelines:**
- [Conventional Commits](https://www.conventionalcommits.org/)
- [How to Write a Git Commit Message](https://chris.beams.io/posts/git-commit/)

**Linux Kernel Style:**
- [Linux Kernel Commit Messages](https://www.kernel.org/doc/html/latest/process/submitting-patches.html)

---

## Summary

### What You've Learned

✅ **Daily workflow:** fetch → rebase → push  
✅ **Commit vs stash:** Permanent vs temporary  
✅ **Clean history:** Rebase over merge  
✅ **Good commits:** Small, logical, clear messages  
✅ **Branching:** Feature branches for new work  
✅ **Versioning:** Semantic versioning with tags  
✅ **Team work:** Pull requests and code review  
✅ **Recovery:** Reflog, revert, reset  

### Your Professional Git Workflow

```bash
# Morning
git pull --rebase origin main

# During work
git add <files>
git commit -m "type: clear message"

# Before end of day
git fetch origin
git rebase origin/main
git push origin main
```

**Keep practicing, and these commands will become second nature!** 🚀

---

**Document Status:** Complete professional Git reference  
**Last Updated:** December 29, 2025  
**Author:** Chun

---

