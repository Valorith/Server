#include "common/auto_skill.h"

#include <cctype>

namespace {

bool AutoSkillNameMatches(const EQ::skills::autoskill::AutoSkillDefinition &definition, const std::string &normalized_name)
{
	if (EQ::skills::autoskill::NormalizeSkillName(definition.name) == normalized_name) {
		return true;
	}

	if (EQ::skills::autoskill::NormalizeSkillName(definition.command_name) == normalized_name) {
		return true;
	}

	for (const auto &alias : definition.aliases) {
		if (EQ::skills::autoskill::NormalizeSkillName(alias) == normalized_name) {
			return true;
		}
	}

	return false;
}

} // namespace

const std::vector<EQ::skills::autoskill::AutoSkillDefinition> &EQ::skills::autoskill::GetSkillDefinitions()
{
	static const std::vector<AutoSkillDefinition> auto_skill_definitions = {
		{ EQ::skills::SkillBackstab,    1u << 0, "Backstab",     "backstab",     { "back stab" } },
		{ EQ::skills::SkillFrenzy,      1u << 1, "Frenzy",       "frenzy",       {} },
		{ EQ::skills::SkillFlyingKick,  1u << 2, "Flying Kick",  "flying kick",  { "flyingkick" } },
		{ EQ::skills::SkillDragonPunch, 1u << 3, "Dragon Punch", "dragon punch", { "dragonpunch", "tail rake", "tailrake" } },
		{ EQ::skills::SkillEagleStrike, 1u << 4, "Eagle Strike", "eagle strike", { "eaglestrike" } },
		{ EQ::skills::SkillTigerClaw,   1u << 5, "Tiger Claw",   "tiger claw",   { "tigerclaw" } },
		{ EQ::skills::SkillRoundKick,   1u << 6, "Round Kick",   "round kick",   { "roundkick" } },
		{ EQ::skills::SkillKick,        1u << 7, "Kick",         "kick",         {} },
		{ EQ::skills::SkillBash,        1u << 8, "Bash",         "bash",         { "slam" } }
	};

	return auto_skill_definitions;
}

const EQ::skills::autoskill::AutoSkillDefinition *EQ::skills::autoskill::GetSkillDefinition(EQ::skills::SkillType skill)
{
	for (const auto &definition : GetSkillDefinitions()) {
		if (definition.skill == skill) {
			return &definition;
		}
	}

	return nullptr;
}

const EQ::skills::autoskill::AutoSkillDefinition *EQ::skills::autoskill::FindSkillDefinition(const std::string &skill_name)
{
	const auto normalized_name = NormalizeSkillName(skill_name);
	if (normalized_name.empty()) {
		return nullptr;
	}

	for (const auto &definition : GetSkillDefinitions()) {
		if (AutoSkillNameMatches(definition, normalized_name)) {
			return &definition;
		}
	}

	return nullptr;
}

bool EQ::skills::autoskill::IsSupported(EQ::skills::SkillType skill)
{
	return GetSkillDefinition(skill) != nullptr;
}

bool EQ::skills::autoskill::IsEnabled(uint32 enabled_mask, EQ::skills::SkillType skill)
{
	const auto *definition = GetSkillDefinition(skill);
	return definition && ((enabled_mask & definition->mask) != 0);
}

uint32 EQ::skills::autoskill::SetEnabled(uint32 enabled_mask, EQ::skills::SkillType skill, bool enabled)
{
	const auto *definition = GetSkillDefinition(skill);
	if (!definition) {
		return SanitizeMask(enabled_mask);
	}

	if (enabled) {
		return SanitizeMask(enabled_mask | definition->mask);
	}

	return SanitizeMask(enabled_mask & ~definition->mask);
}

uint32 EQ::skills::autoskill::SanitizeMask(uint32 enabled_mask)
{
	static const uint32 supported_mask = []() {
		uint32 mask = 0;
		for (const auto &definition : GetSkillDefinitions()) {
			mask |= definition.mask;
		}

		return mask;
	}();

	return enabled_mask & supported_mask;
}

std::string EQ::skills::autoskill::NormalizeSkillName(const std::string &skill_name)
{
	std::string normalized_name;
	for (const unsigned char character : skill_name) {
		if (std::isalnum(character)) {
			normalized_name.push_back(static_cast<char>(std::tolower(character)));
		}
	}

	return normalized_name;
}
