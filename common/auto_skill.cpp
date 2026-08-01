#include "common/auto_skill.h"

#include <algorithm>
#include <cctype>
#include <limits>

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
		// Client-facing, unmodified reuse in seconds. These intentionally do not use the legacy pTimer
		// constants in features.h, which contain historical server-side allowances and source-era values.
		{ EQ::skills::SkillBackstab,    1u << 0, 10, "Backstab",     "backstab",     { "back stab" } },
		{ EQ::skills::SkillFrenzy,      1u << 1, 15, "Frenzy",       "frenzy",       {} },
		{ EQ::skills::SkillFlyingKick,  1u << 2,  8, "Flying Kick",  "flying kick",  { "flyingkick" } },
		{ EQ::skills::SkillDragonPunch, 1u << 3,  6, "Dragon Punch", "dragon punch", { "dragonpunch", "tail rake", "tailrake" } },
		{ EQ::skills::SkillEagleStrike, 1u << 4,  6, "Eagle Strike", "eagle strike", { "eaglestrike" } },
		{ EQ::skills::SkillTigerClaw,   1u << 5,  6, "Tiger Claw",   "tiger claw",   { "tigerclaw" } },
		{ EQ::skills::SkillRoundKick,   1u << 6,  8, "Round Kick",   "round kick",   { "roundkick" } },
		{ EQ::skills::SkillKick,        1u << 7,  8, "Kick",         "kick",         {} },
		{ EQ::skills::SkillBash,        1u << 8,  8, "Bash",         "bash",         { "slam" } }
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

uint32 EQ::skills::autoskill::GetActiveMask(uint32 enabled_mask, uint32 usable_mask)
{
	return SanitizeMask(enabled_mask) & SanitizeMask(usable_mask);
}

bool EQ::skills::autoskill::UsesSecondaryReuseTimer(
	EQ::skills::SkillType skill,
	bool tiger_claw_uses_secondary_timer
)
{
	// RoF2+ clients place Tiger Claw on the secondary combat-ability lane.
	return tiger_claw_uses_secondary_timer && skill == EQ::skills::SkillTigerClaw;
}

uint32 EQ::skills::autoskill::NormalizeReuseTimerGroups(
	uint32 enabled_mask,
	bool tiger_claw_uses_secondary_timer
)
{
	return NormalizeReuseTimerGroups(enabled_mask, enabled_mask, tiger_claw_uses_secondary_timer);
}

uint32 EQ::skills::autoskill::NormalizeReuseTimerGroups(
	uint32 enabled_mask,
	uint32 preferred_mask,
	bool tiger_claw_uses_secondary_timer
)
{
	enabled_mask = SanitizeMask(enabled_mask);
	preferred_mask = SanitizeMask(preferred_mask) & enabled_mask;
	uint32 normalized_mask = 0;
	bool primary_timer_claimed = false;
	bool secondary_timer_claimed = false;

	const auto claim_timer_groups = [&](uint32 candidate_mask) {
		// Definition order is scheduler priority within each preference tier.
		for (const auto &definition : GetSkillDefinitions()) {
			if (!IsEnabled(candidate_mask, definition.skill)) {
				continue;
			}

			const bool uses_secondary_timer = UsesSecondaryReuseTimer(
				definition.skill,
				tiger_claw_uses_secondary_timer
			);

			if (uses_secondary_timer) {
				if (secondary_timer_claimed) {
					continue;
				}

				secondary_timer_claimed = true;
			} else {
				if (primary_timer_claimed) {
					continue;
				}

				primary_timer_claimed = true;
			}

			normalized_mask |= definition.mask;
		}
	};

	claim_timer_groups(preferred_mask);
	claim_timer_groups(enabled_mask & ~preferred_mask);

	return normalized_mask;
}

uint32 EQ::skills::autoskill::SetEnabledForReuseTimerGroup(
	uint32 enabled_mask,
	EQ::skills::SkillType skill,
	bool tiger_claw_uses_secondary_timer,
	bool enabled
)
{
	const auto *requested_definition = GetSkillDefinition(skill);
	if (!requested_definition || !enabled) {
		return SetEnabled(enabled_mask, skill, enabled);
	}

	const bool uses_secondary_timer = UsesSecondaryReuseTimer(skill, tiger_claw_uses_secondary_timer);
	uint32 reuse_timer_group_mask = 0;

	for (const auto &definition : GetSkillDefinitions()) {
		if (UsesSecondaryReuseTimer(definition.skill, tiger_claw_uses_secondary_timer) == uses_secondary_timer) {
			reuse_timer_group_mask |= definition.mask;
		}
	}

	return SanitizeMask((enabled_mask & ~reuse_timer_group_mask) | requested_definition->mask);
}

uint32 EQ::skills::autoskill::GetReuseTimeMilliseconds(
	EQ::skills::SkillType skill,
	int skill_reuse_reduction,
	int total_haste
)
{
	const auto *definition = GetSkillDefinition(skill);
	if (!definition) {
		return 0;
	}

	// The one-second allowance used by the client packet pTimer is not part of the automated scheduler deadline.
	const auto adjusted_reuse_time = std::max<int64>(
		static_cast<int64>(definition->base_reuse_time) - static_cast<int64>(skill_reuse_reduction),
		1LL
	);
	// GetHaste() is 100-based after all client haste caps: 100 is unmodified speed.
	const auto effective_haste = total_haste > 0 ? total_haste : 100;
	const auto unscaled_reuse_time = static_cast<uint64>(adjusted_reuse_time) * 1000 * 100;
	const auto reuse_time = (unscaled_reuse_time + effective_haste - 1) / effective_haste;

	// Keep the calculation in milliseconds and round up so automated use never fires before the intended deadline.
	return static_cast<uint32>(
		std::min(reuse_time, static_cast<uint64>(std::numeric_limits<uint32>::max()))
	);
}

int EQ::skills::autoskill::ClampPersistentReuseTime(int reuse_time)
{
	return std::max(reuse_time, 0);
}

bool EQ::skills::autoskill::ShouldEnforceReuseTimer(
	uint32 enabled_mask,
	EQ::skills::SkillType requested_skill,
	bool tiger_claw_uses_secondary_timer
)
{
	if (!IsSupported(requested_skill)) {
		return false;
	}

	const bool requested_skill_uses_secondary_timer = UsesSecondaryReuseTimer(
		requested_skill,
		tiger_claw_uses_secondary_timer
	);

	for (const auto &definition : GetSkillDefinitions()) {
		if (
			(enabled_mask & definition.mask) != 0 &&
			UsesSecondaryReuseTimer(definition.skill, tiger_claw_uses_secondary_timer) ==
				requested_skill_uses_secondary_timer
		) {
			return true;
		}
	}

	return false;
}

bool EQ::skills::autoskill::CanUseReuseTimer(
	uint32 enabled_mask,
	EQ::skills::SkillType requested_skill,
	bool tiger_claw_uses_secondary_timer,
	bool reuse_timer_ready
)
{
	return (
		!ShouldEnforceReuseTimer(enabled_mask, requested_skill, tiger_claw_uses_secondary_timer) ||
		reuse_timer_ready
	);
}

bool EQ::skills::autoskill::ShouldUseAutoSkillProcReuseTime(
	bool auto_skill_attack_in_progress,
	bool skill_enabled
)
{
	return auto_skill_attack_in_progress && skill_enabled;
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
