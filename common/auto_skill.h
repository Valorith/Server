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
std::string NormalizeSkillName(const std::string &skill_name);

} // namespace autoskill
} // namespace skills
} // namespace EQ
