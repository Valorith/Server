#include "zone/client.h"

#include "common/auto_skill.h"
#include "common/classes.h"
#include "common/item_data.h"
#include "common/item_instance.h"
#include "common/rulesys.h"
#include "common/strings.h"

namespace {

constexpr const char *AutoSkillBucketKey = "autoskill.enabled_mask";

bool IsSlamRace(uint16 race_id)
{
	switch (race_id) {
		case Race::Ogre:
		case Race::Troll:
		case Race::Barbarian:
			return true;
		default:
			return false;
	}
}

bool IsMonkSpecialAttack(EQ::skills::SkillType skill)
{
	switch (skill) {
		case EQ::skills::SkillFlyingKick:
		case EQ::skills::SkillDragonPunch:
		case EQ::skills::SkillEagleStrike:
		case EQ::skills::SkillTigerClaw:
		case EQ::skills::SkillRoundKick:
			return true;
		default:
			return false;
	}
}

bool CanUseKickClass(const Client *client)
{
	const auto class_id = client->GetClass();
	const auto extra_allowed_kick_classes = RuleI(Combat, ExtraAllowedKickClassesBitmask);

	return (
		class_id == Class::Warrior ||
		class_id == Class::Ranger ||
		class_id == Class::Monk ||
		class_id == Class::Beastlord ||
		class_id == Class::Berserker ||
		(extra_allowed_kick_classes & GetPlayerClassBit(class_id))
	);
}

bool HasPrimaryPiercingWeapon(const Client *client)
{
	const auto *item_instance = client->GetInv().GetItem(EQ::invslot::slotPrimary);
	return (
		item_instance &&
		item_instance->GetItem() &&
		item_instance->GetItem()->ItemType == EQ::item::ItemType1HPiercing
	);
}

bool CanAutoSkillBackstab(const Client *client, Mob *target)
{
	if (!HasPrimaryPiercingWeapon(client)) {
		return false;
	}

	if (client->BehindMob(target, client->GetX(), client->GetY())) {
		return true;
	}

	const auto item_bonuses = client->GetItemBonuses();
	const auto spell_bonuses = client->GetSpellBonuses();
	const auto aa_bonuses = client->GetAABonuses();

	return (
		(item_bonuses.FrontalBackstabChance + spell_bonuses.FrontalBackstabChance + aa_bonuses.FrontalBackstabChance) > 0 ||
		(item_bonuses.FrontalBackstabMinDmg + spell_bonuses.FrontalBackstabMinDmg + aa_bonuses.FrontalBackstabMinDmg) > 0
	);
}

pTimerType GetAutoSkillTimer(EQ::skills::SkillType skill)
{
	if (EQ::skills::autoskill::UsesSecondaryReuseTimer(skill)) {
		return pTimerCombatAbility2;
	}

	return pTimerCombatAbility;
}

int GetCrossPathPersistentTimerOffset(EQ::skills::SkillType skill)
{
	switch (skill) {
		case EQ::skills::SkillDragonPunch: // Tail Rake uses the same skill ID
			return 0;
		case EQ::skills::SkillEagleStrike:
			return 1;
		case EQ::skills::SkillTigerClaw:
			return 2;
		case EQ::skills::SkillBash: // Slam uses Bash
			return 3;
		default:
			return -1;
	}
}

pTimerType GetCrossPathPersistentTimer(EQ::skills::SkillType skill, bool auto_skill_activation)
{
	const auto timer_offset = GetCrossPathPersistentTimerOffset(skill);
	if (timer_offset < 0) {
		return 0;
	}

	const auto timer_start = auto_skill_activation ?
		pTimerAutoSkillSchedulerCrossPathStart :
		pTimerAutoSkillManualCrossPathStart;
	return static_cast<pTimerType>(timer_start + timer_offset);
}

bool IsValidAutoSkillTarget(Client *client, Mob *target)
{
	return (
		target &&
		target != client &&
		target->GetHP() > -10 &&
		client->IsAttackAllowed(target) &&
		client->CombatRange(target) &&
		client->CheckLosFN(target) &&
		client->IsFacingMob(target)
	);
}

} // namespace

bool Client::IsAutoSkillReuseTimerReady(pTimerType timer)
{
	auto &reuse_timer = timer == pTimerCombatAbility2 ?
		auto_skill_combat_ability_2_timer :
		auto_skill_combat_ability_timer;

	// Persistent combat ability timers use whole seconds. This timer preserves the actual millisecond reuse deadline.
	return !reuse_timer.Enabled() || reuse_timer.Check(false);
}

bool Client::CanUseAutoSkillReuseTimer(pTimerType timer, EQ::skills::SkillType requested_skill)
{
	if (!EQ::skills::autoskill::IsSupported(requested_skill)) {
		return true;
	}

	const bool reuse_timer_ready = IsAutoSkillReuseTimerReady(timer);
	auto &started_by_auto_skill = timer == pTimerCombatAbility2 ?
		auto_skill_combat_ability_2_timer_started_by_auto_skill :
		auto_skill_combat_ability_timer_started_by_auto_skill;

	if (reuse_timer_ready) {
		started_by_auto_skill = false;
	}

	// The selected timer is authoritative because manual client grouping can differ from autoskill grouping.
	const auto timer_group_skill = timer == pTimerCombatAbility2 ?
		EQ::skills::SkillTigerClaw :
		EQ::skills::SkillKick;

	return EQ::skills::autoskill::CanUseReuseTimer(
		GetActiveAutoSkillEnabledMask(),
		timer_group_skill,
		reuse_timer_ready,
		started_by_auto_skill
	);
}

bool Client::CanUseCrossPathAutoSkillReuseTimer(
	EQ::skills::SkillType skill,
	bool auto_skill_activation
)
{
	if (!EQ::skills::autoskill::IsSupported(skill)) {
		return true;
	}

	const bool manual_uses_secondary_timer = EQ::skills::autoskill::UsesSecondaryCombatAbilityTimer(
		skill,
		false,
		ClientVersion() >= EQ::versions::ClientVersion::RoF2
	);
	if (manual_uses_secondary_timer == EQ::skills::autoskill::UsesSecondaryReuseTimer(skill)) {
		return true;
	}

	const auto timer_entry = auto_skill_cross_path_reuse_timers.find(skill);
	if (timer_entry != auto_skill_cross_path_reuse_timers.end()) {
		auto &reuse_timer = timer_entry->second.timer;
		const bool reuse_timer_ready = !reuse_timer.Enabled() || reuse_timer.Check(false);
		if (reuse_timer_ready) {
			auto_skill_cross_path_reuse_timers.erase(timer_entry);
			return true;
		}

		return EQ::skills::autoskill::CanUseCrossPathReuseTimer(
			false,
			timer_entry->second.started_by_auto_skill,
			auto_skill_activation
		);
	}

	// The conservative persistent timer is only a fallback after zoning discards the precise in-memory timer.
	const auto opposite_path_timer = GetCrossPathPersistentTimer(skill, !auto_skill_activation);
	return !opposite_path_timer || p_timers.Expired(&database, opposite_path_timer, false);
}

void Client::StartAutoSkillReuseTimer(
	pTimerType timer,
	EQ::skills::SkillType skill,
	int skill_reuse_reduction,
	int total_haste
)
{
	if (!RuleB(Combat, EnableAutoSkill)) {
		return;
	}

	const auto reuse_time = EQ::skills::autoskill::GetReuseTimeMilliseconds(
		skill,
		skill_reuse_reduction,
		total_haste
	);
	if (reuse_time == 0) {
		return;
	}

	auto &reuse_timer = timer == pTimerCombatAbility2 ?
		auto_skill_combat_ability_2_timer :
		auto_skill_combat_ability_timer;
	auto &started_by_auto_skill = timer == pTimerCombatAbility2 ?
		auto_skill_combat_ability_2_timer_started_by_auto_skill :
		auto_skill_combat_ability_timer_started_by_auto_skill;

	started_by_auto_skill = auto_skill_attack_in_progress;
	reuse_timer.Start(reuse_time);

	const bool manual_uses_secondary_timer = EQ::skills::autoskill::UsesSecondaryCombatAbilityTimer(
		skill,
		false,
		ClientVersion() >= EQ::versions::ClientVersion::RoF2
	);
	if (manual_uses_secondary_timer != EQ::skills::autoskill::UsesSecondaryReuseTimer(skill)) {
		// Manual and scheduler activations keep their own lane layouts, but the same skill may not bypass
		// its precise reuse deadline by switching activation paths. The pTimer preserves this bridge on zone.
		auto &cross_path_reuse_timer = auto_skill_cross_path_reuse_timers[skill];
		cross_path_reuse_timer.started_by_auto_skill = auto_skill_attack_in_progress;
		cross_path_reuse_timer.timer.Start(reuse_time);

		const auto persistent_timer = GetCrossPathPersistentTimer(skill, auto_skill_attack_in_progress);
		if (persistent_timer) {
			const auto persistent_reuse_time =
				EQ::skills::autoskill::GetConservativePersistentReuseTimeSeconds(reuse_time);
			p_timers.Start(persistent_timer, persistent_reuse_time);
		}
	}
}

bool Client::IsAutoSkillEnabled(EQ::skills::SkillType skill_id) const
{
	return EQ::skills::autoskill::IsEnabled(GetActiveAutoSkillEnabledMask(), skill_id);
}

uint32 Client::GetActiveAutoSkillEnabledMask() const
{
	if (auto_skill_enabled_mask == 0) {
		return 0;
	}

	uint32 usable_mask = 0;

	for (const auto &definition : EQ::skills::autoskill::GetSkillDefinitions()) {
		if (
			EQ::skills::autoskill::IsEnabled(auto_skill_enabled_mask, definition.skill) &&
			IsAutoSkillUsable(definition.skill)
		) {
			usable_mask |= definition.mask;
		}
	}

	// Select the active lane winners from normalized preferences and current usability.
	return EQ::skills::autoskill::NormalizeReuseTimerGroups(usable_mask);
}

bool Client::IsAutoSkillUsable(EQ::skills::SkillType skill_id) const
{
	if (!EQ::skills::autoskill::IsSupported(skill_id)) {
		return false;
	}

	if (skill_id == EQ::skills::SkillBash) {
		return MaxSkill(skill_id) > 0 || IsSlamRace(GetRace());
	}

	if (MaxSkill(skill_id) == 0) {
		return false;
	}

	if (skill_id == EQ::skills::SkillKick) {
		return CanUseKickClass(this);
	}

	if (skill_id == EQ::skills::SkillBackstab) {
		return GetClass() == Class::Rogue;
	}

	if (skill_id == EQ::skills::SkillFrenzy) {
		return GetClass() == Class::Berserker;
	}

	if (IsMonkSpecialAttack(skill_id)) {
		return GetClass() == Class::Monk;
	}

	return true;
}

void Client::SetAutoSkillEnabled(EQ::skills::SkillType skill_id, bool enabled)
{
	auto_skill_enabled_mask = EQ::skills::autoskill::SetEnabledForReuseTimerGroup(
		auto_skill_enabled_mask,
		skill_id,
		enabled
	);
	SaveAutoSkillSettings();
}

void Client::LoadAutoSkillSettings()
{
	auto_skill_enabled_mask = 0;

	const auto auto_skill_bucket = GetBucket(AutoSkillBucketKey);
	if (auto_skill_bucket.empty()) {
		return;
	}

	const auto saved_mask = Strings::ToUnsignedInt(auto_skill_bucket);
	auto_skill_enabled_mask = EQ::skills::autoskill::NormalizeReuseTimerGroups(saved_mask);
	if (auto_skill_enabled_mask != saved_mask) {
		SaveAutoSkillSettings();
	}
}

void Client::SaveAutoSkillSettings()
{
	auto_skill_enabled_mask = EQ::skills::autoskill::SanitizeMask(auto_skill_enabled_mask);

	if (auto_skill_enabled_mask == 0) {
		DeleteBucket(AutoSkillBucketKey);
		return;
	}

	SetBucket(AutoSkillBucketKey, std::to_string(auto_skill_enabled_mask));
}

std::vector<EQ::skills::SkillType> Client::GetApplicableAutoSkills() const
{
	std::vector<EQ::skills::SkillType> applicable_skills;

	for (const auto &definition : EQ::skills::autoskill::GetSkillDefinitions()) {
		if (IsAutoSkillUsable(definition.skill)) {
			applicable_skills.push_back(definition.skill);
		}
	}

	return applicable_skills;
}

void Client::ProcessAutoSkills()
{
	if (
		!RuleB(Combat, EnableAutoSkill) ||
		!auto_attack ||
		auto_skill_enabled_mask == 0 ||
		!auto_skill_process_timer.Check() ||
		IsAIControlled() ||
		dead ||
		IsStunned() ||
		IsFeared() ||
		IsMezzed() ||
		GetAppearance() == eaDead ||
		IsMeleeDisabled()
	) {
		return;
	}

	auto_skill_enabled_mask = EQ::skills::autoskill::SanitizeMask(auto_skill_enabled_mask);
	const auto active_auto_skill_enabled_mask = GetActiveAutoSkillEnabledMask();
	if (active_auto_skill_enabled_mask == 0) {
		return;
	}

	Mob *target = GetTarget();
	if (!IsValidAutoSkillTarget(this, target)) {
		return;
	}

	for (const auto &definition : EQ::skills::autoskill::GetSkillDefinitions()) {
		const auto skill = definition.skill;

		if (!EQ::skills::autoskill::IsEnabled(active_auto_skill_enabled_mask, skill)) {
			continue;
		}

		if (!IsAutoSkillUsable(skill)) {
			continue;
		}

		if (skill == EQ::skills::SkillBackstab && !CanAutoSkillBackstab(this, target)) {
			continue;
		}

		if (GetTarget() != target) {
			return;
		}

		const auto timer = GetAutoSkillTimer(skill);
		if (
			!EQ::skills::autoskill::CanUseReuseTimer(
				active_auto_skill_enabled_mask,
				skill,
				IsAutoSkillReuseTimerReady(timer)
			) ||
			!CanUseCrossPathAutoSkillReuseTimer(skill, true) ||
			!p_timers.Expired(&database, timer, false)
		) {
			continue;
		}

		CombatAbility_Struct combat_ability = {};
		combat_ability.m_target = target->GetID();
		combat_ability.m_atk = 100;
		combat_ability.m_skill = skill;

		auto_skill_attack_in_progress = true;
		OPCombatAbility(&combat_ability);
		auto_skill_attack_in_progress = false;

		if (GetTarget() != target || target->GetHP() <= -10) {
			return;
		}
	}
}
