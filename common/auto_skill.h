#pragma once

#include "common/skills.h"
#include "common/types.h"

#include <string>
#include <vector>

namespace EQ {
namespace skills {
namespace autoskill {

struct AutoSkillDefinition {
	EQ::skills::SkillType skill;
	uint32 mask;
	uint8 base_reuse_time;
	const char *name;
	const char *command_name;
	std::vector<const char *> aliases;
};

const std::vector<AutoSkillDefinition> &GetSkillDefinitions();
const AutoSkillDefinition *GetSkillDefinition(EQ::skills::SkillType skill);
const AutoSkillDefinition *FindSkillDefinition(const std::string &skill_name);

bool IsSupported(EQ::skills::SkillType skill);
bool IsEnabled(uint32 enabled_mask, EQ::skills::SkillType skill);
uint32 SetEnabled(uint32 enabled_mask, EQ::skills::SkillType skill, bool enabled);
uint32 SanitizeMask(uint32 enabled_mask);
uint32 GetActiveMask(uint32 enabled_mask, uint32 usable_mask);
bool UsesSecondaryReuseTimer(EQ::skills::SkillType skill, bool tiger_claw_uses_secondary_timer);
uint32 SetEnabledForReuseTimerGroup(
	uint32 enabled_mask,
	EQ::skills::SkillType skill,
	bool tiger_claw_uses_secondary_timer,
	bool enabled
);
uint32 GetReuseTimeMilliseconds(EQ::skills::SkillType skill, int skill_reuse_reduction, int total_haste);
int ClampPersistentReuseTime(int reuse_time);
bool ShouldEnforceReuseTimer(
	uint32 enabled_mask,
	EQ::skills::SkillType requested_skill,
	bool tiger_claw_uses_secondary_timer
);
bool CanUseReuseTimer(
	uint32 enabled_mask,
	EQ::skills::SkillType requested_skill,
	bool tiger_claw_uses_secondary_timer,
	bool reuse_timer_ready
);
bool ShouldUseAutoSkillProcReuseTime(bool auto_skill_attack_in_progress, bool skill_enabled);
std::string NormalizeSkillName(const std::string &skill_name);

} // namespace autoskill
} // namespace skills
} // namespace EQ
