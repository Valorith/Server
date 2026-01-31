# Visual Guide: PR #40 Rebase Process

## Current State (BEFORE Rebase)

```
fix/buff-suppression-pet-restore (base branch)
│
├─ b19503a ← Merge PR #42 (includes return; fix)
│  ├─ 4faf48a ← Merge PR #43 (includes return; fix)  
│  ├─ ff7dbce ← Fix compilation: return false → return
│  └─ ...more commits with validation fixes
│
└─ dffc461 ← Old base (what your PR was originally based on)
   │
   └─ copilot/sub-pr-39 (your branch) ← CONFLICTS HERE!
      ├─ 6e2a9a4 ← Resolve merge conflict
      ├─ 597e6eb ← Add validation check (return;)
      ├─ 03f7704 ← Add Levitate restoration ✓ (THIS IS WHAT WE WANT)
      └─ 950e644 ← Initial plan
```

**Problem:** Your commits 597e6eb and 6e2a9a4 add the same fix that's already in b19503a!

---

## After Rebase (TARGET State)

```
fix/buff-suppression-pet-restore (base branch)
│
├─ b19503a ← Merge PR #42 (includes return; fix & nimbus)
│  ├─ 4faf48a ← Merge PR #43
│  ├─ ff7dbce ← Fix compilation
│  └─ ...
│
└─ copilot/sub-pr-39 (your branch - rebased) ← CLEAN!
   ├─ f94cb8d ← Add Levitate restoration ✓
   └─ dee427b ← Initial plan
```

**Result:** Clean history with only your Levitate feature!

---

## What the Rebase Does

### Step 1: Replay Your Commits
```
git rebase origin/fix/buff-suppression-pet-restore
```
Git attempts to replay your commits on top of b19503a:
- ✓ 950e644 (Initial plan) → dee427b
- ⚠️ 597e6eb (Add validation) → **CONFLICT** (already in base)
- ⏭️ Skip this commit with: `git rebase --skip`
- ⏭️ 6e2a9a4 automatically dropped (contents already upstream)
- ✓ 03f7704 (Add Levitate) → f94cb8d

### Step 2: Force Push
```
git push --force-with-lease origin copilot/sub-pr-39
```
Updates the remote branch with the clean history.

---

## The Conflict Explained

When rebasing, Git will show:

```cpp
<<<<<<< HEAD (base branch - what's already there)
if (!IsValidSpell(buffs[slot].spellid))
    return;
=======
if (!IsValidSpell(buffs[slot].spellid)) {  (your commit - same fix!)
    return;
}
>>>>>>> 597e6eb (Add validation check)
```

**Both sides have `return;`** - just different formatting!
Since the base already has the fix, skip your commit: `git rebase --skip`

---

## Commit Details

### Your Levitate Commit (03f7704)
This is the ONLY commit that should remain in your PR:

```cpp
case SpellEffect::Levitate:
{
    if (!zone->CanLevitate()) {
        SendAppearancePacket(AppearanceType::FlyMode, 0);
        BuffFadeByEffect(SpellEffect::Levitate);
    } else {
        if (spell.limit_value[i] == 1) {
            SendAppearancePacket(AppearanceType::FlyMode, 
                EQ::constants::GravityBehavior::LevitateWhileRunning);
        } else {
            SendAppearancePacket(AppearanceType::FlyMode, 
                EQ::constants::GravityBehavior::Levitating);
        }
    }
    break;
}
```

Location: `zone/spell_effects.cpp` lines 4722-4735

---

## Quick Reference Commands

```bash
# The complete rebase process
git checkout copilot/sub-pr-39
git fetch origin
git rebase origin/fix/buff-suppression-pet-restore
# When conflict appears:
git rebase --skip
# After rebase completes:
git push --force-with-lease origin copilot/sub-pr-39
```

## Troubleshooting

### If rebase fails with different error:
```bash
git rebase --abort
# Try again or use Option 3 (cherry-pick)
```

### To see what will change:
```bash
git log origin/fix/buff-suppression-pet-restore..HEAD --oneline
```

### To verify base branch:
```bash
git fetch origin
git show origin/fix/buff-suppression-pet-restore:zone/spell_effects.cpp | grep -A3 "IsValidSpell"
# Should show: if (!IsValidSpell(buffs[slot].spellid))
#                  return;
```

---

## Why This is Safe

1. **Your Levitate code is preserved** - The commit 03f7704 will be replayed as-is
2. **No data loss** - Redundant commits are removed because they're already in base
3. **Cleaner history** - Removes duplicate/conflicting commits
4. **PR becomes mergeable** - No conflicts after rebase

The validation fix you added is not lost - it's already in the base branch!
