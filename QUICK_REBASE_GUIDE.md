# Quick Reference: Resolve PR #40 in 5 Steps

## TL;DR - Just Run These Commands

```bash
# 1. Switch to PR branch
git checkout copilot/sub-pr-39

# 2. Fetch latest
git fetch origin

# 3. Start rebase
git rebase origin/fix/buff-suppression-pet-restore

# 4. Skip the conflicting commit (when prompted)
git rebase --skip

# 5. Force push
git push --force-with-lease origin copilot/sub-pr-39
```

**Done!** PR #40 will now be mergeable.

---

## What You'll See

### Step 3 Output:
```
Rebasing (1/4)
Rebasing (2/4)
Rebasing (3/4)
Auto-merging zone/spell_effects.cpp
CONFLICT (content): Merge conflict in zone/spell_effects.cpp
error: could not apply 597e6eb... Add validation check
```

**This is expected!** → Go to Step 4

### Step 4 Output:
```
Rebasing (4/4)
dropping 6e2a9a4... Resolve merge conflict -- already upstream
Successfully rebased and updated refs/heads/copilot/sub-pr-39.
```

**Success!** → Go to Step 5

### Step 5 Output:
```
To https://github.com/Valorith/Server
   6e2a9a4..f94cb8d  copilot/sub-pr-39 -> copilot/sub-pr-39
```

**Done!** Check PR #40 on GitHub - it should show no conflicts.

---

## Why Each Step?

1. **git checkout** - Make sure you're on the right branch
2. **git fetch** - Get the latest base branch changes
3. **git rebase** - Replay your commits on the new base
4. **git rebase --skip** - Skip redundant commit (already in base)
5. **git push --force-with-lease** - Update GitHub with clean history

---

## After Rebase: Verify

Visit: https://github.com/Valorith/Server/pull/40

You should see:
- ✓ "This branch has no conflicts with the base branch"
- ✓ Only 2 commits
- ✓ Changes show only Levitate restoration code

---

## If Something Goes Wrong

**Abort and start over:**
```bash
git rebase --abort
```

**Need more details?** Read:
- `REBASE_INSTRUCTIONS.md` - Full instructions
- `REBASE_VISUAL_GUIDE.md` - Visual diagrams

---

## One-Liner Alternative

If you trust automation:
```bash
git checkout copilot/sub-pr-39 && \
git fetch origin && \
git rebase origin/fix/buff-suppression-pet-restore && \
git rebase --skip && \
git push --force-with-lease origin copilot/sub-pr-39
```

⚠️ Only use if you understand what each command does!

---

## What Changes

**Before:** 4 commits (validation + conflict resolution + levitate + plan)  
**After:** 2 commits (levitate + plan)

**Result:** Clean PR with only your Levitate feature!

---

**Questions?** Check the detailed guides in this directory.
