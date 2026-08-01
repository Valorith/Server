#pragma once

#include "common/auto_skill.h"
#include "cppunit/cpptest.h"

#include <algorithm>
#include <array>
#include <limits>

class AutoSkillUtilTest : public Test::Suite {
	typedef void(AutoSkillUtilTest::*TestFunction)(void);

public:
	AutoSkillUtilTest() {
		TEST_ADD(AutoSkillUtilTest::FindsSkillsAndAliases);
		TEST_ADD(AutoSkillUtilTest::HandlesEnabledMask);
		TEST_ADD(AutoSkillUtilTest::FiltersUnavailableEnabledSkills);
		TEST_ADD(AutoSkillUtilTest::EnforcesOneSkillPerReuseTimerGroup);
		TEST_ADD(AutoSkillUtilTest::NormalizesPersistedReuseTimerGroups);
		TEST_ADD(AutoSkillUtilTest::KeepsPriorityOrder);
		TEST_ADD(AutoSkillUtilTest::CalculatesReuseTimes);
		TEST_ADD(AutoSkillUtilTest::ValidatesObservedHasteMatrix);
		TEST_ADD(AutoSkillUtilTest::RoundsReuseTimesUpAcrossRange);
		TEST_ADD(AutoSkillUtilTest::ClampsPersistentReuseTimes);
		TEST_ADD(AutoSkillUtilTest::ArbitratesReuseTimerReadiness);
		TEST_ADD(AutoSkillUtilTest::SelectsAutoSkillProcReuseTimes);
	}

	~AutoSkillUtilTest() {
	}

private:
	void FindsSkillsAndAliases() {
		auto *flying_kick = EQ::skills::autoskill::FindSkillDefinition("Flying-Kick");
		TEST_ASSERT(flying_kick);
		TEST_ASSERT(flying_kick->skill == EQ::skills::SkillFlyingKick);

		auto *tail_rake = EQ::skills::autoskill::FindSkillDefinition("tail rake");
		TEST_ASSERT(tail_rake);
		TEST_ASSERT(tail_rake->skill == EQ::skills::SkillDragonPunch);

		auto *slam = EQ::skills::autoskill::FindSkillDefinition("slam");
		TEST_ASSERT(slam);
		TEST_ASSERT(slam->skill == EQ::skills::SkillBash);

		TEST_ASSERT(!EQ::skills::autoskill::FindSkillDefinition("taunt"));
	}

	void HandlesEnabledMask() {
		uint32 enabled_mask = 0;

		enabled_mask = EQ::skills::autoskill::SetEnabled(enabled_mask, EQ::skills::SkillKick, true);
		TEST_ASSERT(EQ::skills::autoskill::IsEnabled(enabled_mask, EQ::skills::SkillKick));

		enabled_mask = EQ::skills::autoskill::SetEnabled(enabled_mask, EQ::skills::SkillKick, false);
		TEST_ASSERT(!EQ::skills::autoskill::IsEnabled(enabled_mask, EQ::skills::SkillKick));

		enabled_mask = EQ::skills::autoskill::SetEnabled(enabled_mask, EQ::skills::SkillTaunt, true);
		TEST_ASSERT(enabled_mask == 0);

		TEST_ASSERT(EQ::skills::autoskill::SanitizeMask(0xFFFFFFFF) == 0x1FF);
	}

	void FiltersUnavailableEnabledSkills() {
		using namespace EQ::skills;
		using namespace EQ::skills::autoskill;

		const auto flying_kick_enabled = SetEnabled(0, SkillFlyingKick, true);
		const auto kick_usable = SetEnabled(0, SkillKick, true);
		const auto no_active_skills = GetActiveMask(flying_kick_enabled, kick_usable);

		TEST_ASSERT(no_active_skills == 0);
		TEST_ASSERT(!ShouldEnforceReuseTimer(no_active_skills, SkillKick, true));

		const auto flying_kick_usable = SetEnabled(kick_usable, SkillFlyingKick, true);
		const auto flying_kick_active = GetActiveMask(flying_kick_enabled, flying_kick_usable);

		TEST_ASSERT(IsEnabled(flying_kick_active, SkillFlyingKick));
		TEST_ASSERT(ShouldEnforceReuseTimer(flying_kick_active, SkillKick, true));
	}

	void EnforcesOneSkillPerReuseTimerGroup() {
		using namespace EQ::skills;
		using namespace EQ::skills::autoskill;

		auto enabled_mask = SetEnabled(0, SkillFlyingKick, true);
		enabled_mask = SetEnabled(enabled_mask, SkillKick, true);
		enabled_mask = SetEnabledForReuseTimerGroup(enabled_mask, SkillBash, true, true);

		TEST_ASSERT(IsEnabled(enabled_mask, SkillBash));
		TEST_ASSERT(!IsEnabled(enabled_mask, SkillFlyingKick));
		TEST_ASSERT(!IsEnabled(enabled_mask, SkillKick));

		enabled_mask = SetEnabledForReuseTimerGroup(enabled_mask, SkillTigerClaw, true, true);
		TEST_ASSERT(IsEnabled(enabled_mask, SkillBash));
		TEST_ASSERT(IsEnabled(enabled_mask, SkillTigerClaw));

		enabled_mask = SetEnabledForReuseTimerGroup(enabled_mask, SkillRoundKick, true, true);
		TEST_ASSERT(IsEnabled(enabled_mask, SkillRoundKick));
		TEST_ASSERT(IsEnabled(enabled_mask, SkillTigerClaw));
		TEST_ASSERT(!IsEnabled(enabled_mask, SkillBash));

		enabled_mask = SetEnabledForReuseTimerGroup(enabled_mask, SkillTigerClaw, true, false);
		TEST_ASSERT(IsEnabled(enabled_mask, SkillRoundKick));
		TEST_ASSERT(!IsEnabled(enabled_mask, SkillTigerClaw));

		enabled_mask = SetEnabledForReuseTimerGroup(enabled_mask, SkillTigerClaw, true, true);
		enabled_mask = SetEnabledForReuseTimerGroup(enabled_mask, SkillKick, false, true);
		TEST_ASSERT(IsEnabled(enabled_mask, SkillKick));
		TEST_ASSERT(!IsEnabled(enabled_mask, SkillRoundKick));
		TEST_ASSERT(!IsEnabled(enabled_mask, SkillTigerClaw));

		enabled_mask = SetEnabledForReuseTimerGroup(enabled_mask, SkillKick, false, false);
		TEST_ASSERT(enabled_mask == 0);
	}

	void NormalizesPersistedReuseTimerGroups() {
		using namespace EQ::skills;
		using namespace EQ::skills::autoskill;

		auto enabled_mask = SetEnabled(0, SkillFlyingKick, true);
		enabled_mask = SetEnabled(enabled_mask, SkillTigerClaw, true);
		enabled_mask = SetEnabled(enabled_mask, SkillKick, true);

		const auto rof2_mask = NormalizeReuseTimerGroups(enabled_mask, true);
		TEST_ASSERT(IsEnabled(rof2_mask, SkillFlyingKick));
		TEST_ASSERT(IsEnabled(rof2_mask, SkillTigerClaw));
		TEST_ASSERT(!IsEnabled(rof2_mask, SkillKick));

		const auto legacy_mask = NormalizeReuseTimerGroups(enabled_mask, false);
		TEST_ASSERT(IsEnabled(legacy_mask, SkillFlyingKick));
		TEST_ASSERT(!IsEnabled(legacy_mask, SkillTigerClaw));
		TEST_ASSERT(!IsEnabled(legacy_mask, SkillKick));
		TEST_ASSERT(NormalizeReuseTimerGroups(enabled_mask | (1u << 31), false) == legacy_mask);

		const auto kick_usable_mask = SetEnabled(0, SkillKick, true);
		const auto usable_first_mask = NormalizeReuseTimerGroups(enabled_mask, kick_usable_mask, true);
		TEST_ASSERT(IsEnabled(usable_first_mask, SkillKick));
		TEST_ASSERT(IsEnabled(usable_first_mask, SkillTigerClaw));
		TEST_ASSERT(!IsEnabled(usable_first_mask, SkillFlyingKick));

		const auto legacy_usable_first_mask = NormalizeReuseTimerGroups(enabled_mask, kick_usable_mask, false);
		TEST_ASSERT(IsEnabled(legacy_usable_first_mask, SkillKick));
		TEST_ASSERT(!IsEnabled(legacy_usable_first_mask, SkillTigerClaw));
		TEST_ASSERT(!IsEnabled(legacy_usable_first_mask, SkillFlyingKick));

		const auto kick_and_tiger_claw_usable_mask = SetEnabled(kick_usable_mask, SkillTigerClaw, true);
		const auto current_session_mask = NormalizeReuseTimerGroups(kick_and_tiger_claw_usable_mask, true);
		TEST_ASSERT(IsEnabled(current_session_mask, SkillKick));
		TEST_ASSERT(IsEnabled(current_session_mask, SkillTigerClaw));
		TEST_ASSERT(!IsEnabled(current_session_mask, SkillFlyingKick));

		const auto after_level_up_usable_mask = SetEnabled(kick_and_tiger_claw_usable_mask, SkillFlyingKick, true);
		const auto after_level_up_mask = NormalizeReuseTimerGroups(after_level_up_usable_mask, true);
		TEST_ASSERT(IsEnabled(after_level_up_mask, SkillFlyingKick));
		TEST_ASSERT(IsEnabled(after_level_up_mask, SkillTigerClaw));
		TEST_ASSERT(!IsEnabled(after_level_up_mask, SkillKick));
		TEST_ASSERT(IsEnabled(enabled_mask, SkillFlyingKick));
		TEST_ASSERT(IsEnabled(enabled_mask, SkillKick));
	}

	void KeepsPriorityOrder() {
		const auto &definitions = EQ::skills::autoskill::GetSkillDefinitions();
		const std::array<uint8, 9> expected_base_reuse_times = {
			10, 15, 8, 6, 6, 6, 8, 8, 8
		};

		TEST_ASSERT(definitions.size() == 9);
		TEST_ASSERT(definitions.front().skill == EQ::skills::SkillBackstab);
		TEST_ASSERT(definitions[1].skill == EQ::skills::SkillFrenzy);
		TEST_ASSERT(definitions.back().skill == EQ::skills::SkillBash);
		for (size_t i = 0; i < definitions.size(); ++i) {
			TEST_ASSERT(definitions[i].base_reuse_time == expected_base_reuse_times[i]);
		}
	}

	void CalculatesReuseTimes() {
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillBackstab, 0, 100) == 10000);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillFrenzy, 0, 100) == 15000);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillFlyingKick, 0, 100) == 8000);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillDragonPunch, 0, 100) == 6000);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillEagleStrike, 0, 100) == 6000);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillTigerClaw, 0, 100) == 6000);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillRoundKick, 0, 100) == 8000);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillKick, 0, 100) == 8000);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillBash, 0, 100) == 8000);

		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillBash, 0, 141) == 5674);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillFrenzy, 0, 141) == 10639);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillFrenzy, 0, 200) == 7500);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillBash, 0, 50) == 16000);

		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillFrenzy, 3, 100) == 12000);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillBash, 2, 141) == 4256);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillBash, 8, 141) == 710);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillBash, 50, 141) == 710);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillBash, -2, 100) == 10000);

		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillBash, 0, 0) == 8000);
		TEST_ASSERT(
			EQ::skills::autoskill::GetReuseTimeMilliseconds(
				EQ::skills::SkillBash,
				std::numeric_limits<int>::max(),
				100
			) == 1000
		);
		TEST_ASSERT(
			EQ::skills::autoskill::GetReuseTimeMilliseconds(
				EQ::skills::SkillBash,
				std::numeric_limits<int>::min(),
				1
			) == std::numeric_limits<uint32>::max()
		);
		TEST_ASSERT(EQ::skills::autoskill::GetReuseTimeMilliseconds(EQ::skills::SkillTaunt, 0, 100) == 0);
	}

	void ValidatesObservedHasteMatrix() {
		struct ReuseMatrixTestCase {
			EQ::skills::SkillType skill;
			std::array<uint32, 6> expected_reuse_ms;
		};

		const std::array<int, 6> total_haste_values = {
			100, 125, 141, 150, 200, 50
		};
		const std::array<ReuseMatrixTestCase, 9> reuse_matrix = {{
			{ EQ::skills::SkillBackstab,    { 10000,  8000,  7093,  6667, 5000, 20000 } },
			{ EQ::skills::SkillFrenzy,      { 15000, 12000, 10639, 10000, 7500, 30000 } },
			{ EQ::skills::SkillFlyingKick,  {  8000,  6400,  5674,  5334, 4000, 16000 } },
			{ EQ::skills::SkillDragonPunch, {  6000,  4800,  4256,  4000, 3000, 12000 } },
			{ EQ::skills::SkillEagleStrike, {  6000,  4800,  4256,  4000, 3000, 12000 } },
			{ EQ::skills::SkillTigerClaw,   {  6000,  4800,  4256,  4000, 3000, 12000 } },
			{ EQ::skills::SkillRoundKick,   {  8000,  6400,  5674,  5334, 4000, 16000 } },
			{ EQ::skills::SkillKick,        {  8000,  6400,  5674,  5334, 4000, 16000 } },
			{ EQ::skills::SkillBash,        {  8000,  6400,  5674,  5334, 4000, 16000 } }
		}};

		for (const auto &test_case : reuse_matrix) {
			for (size_t i = 0; i < total_haste_values.size(); ++i) {
				TEST_ASSERT(
					EQ::skills::autoskill::GetReuseTimeMilliseconds(
						test_case.skill,
						0,
						total_haste_values[i]
					) == test_case.expected_reuse_ms[i]
				);
			}
		}
	}

	void RoundsReuseTimesUpAcrossRange() {
		struct ReuseTestCase {
			EQ::skills::SkillType skill;
			int base_reuse_time;
		};

		const std::array<ReuseTestCase, 9> reuse_test_cases = {{
			{ EQ::skills::SkillBackstab,   10 },
			{ EQ::skills::SkillFrenzy,     15 },
			{ EQ::skills::SkillFlyingKick,  8 },
			{ EQ::skills::SkillDragonPunch, 6 },
			{ EQ::skills::SkillEagleStrike, 6 },
			{ EQ::skills::SkillTigerClaw,   6 },
			{ EQ::skills::SkillRoundKick,   8 },
			{ EQ::skills::SkillKick,        8 },
			{ EQ::skills::SkillBash,        8 }
		}};

		for (const auto &test_case : reuse_test_cases) {
			for (int reduction = -2; reduction <= test_case.base_reuse_time + 2; ++reduction) {
				const auto adjusted_reuse_time = std::max(test_case.base_reuse_time - reduction, 1);
				const auto unscaled_reuse_time = static_cast<uint64>(adjusted_reuse_time) * 1000 * 100;
				const auto unmodified_reuse_time = static_cast<uint32>(adjusted_reuse_time * 1000);

				TEST_ASSERT(
					EQ::skills::autoskill::GetReuseTimeMilliseconds(test_case.skill, reduction, 0) ==
					unmodified_reuse_time
				);
				TEST_ASSERT(
					EQ::skills::autoskill::GetReuseTimeMilliseconds(test_case.skill, reduction, -100) ==
					unmodified_reuse_time
				);

				for (int total_haste = 1; total_haste <= 400; ++total_haste) {
					const auto reuse_time = EQ::skills::autoskill::GetReuseTimeMilliseconds(
						test_case.skill,
						reduction,
						total_haste
					);

					TEST_ASSERT(static_cast<uint64>(reuse_time) * total_haste >= unscaled_reuse_time);
					TEST_ASSERT(
						reuse_time == 1 ||
						static_cast<uint64>(reuse_time - 1) * total_haste < unscaled_reuse_time
					);
				}
			}
		}
	}

	void ClampsPersistentReuseTimes() {
		TEST_ASSERT(EQ::skills::autoskill::ClampPersistentReuseTime(std::numeric_limits<int>::min()) == 0);
		TEST_ASSERT(EQ::skills::autoskill::ClampPersistentReuseTime(-1) == 0);
		TEST_ASSERT(EQ::skills::autoskill::ClampPersistentReuseTime(0) == 0);
		TEST_ASSERT(EQ::skills::autoskill::ClampPersistentReuseTime(1) == 1);
		TEST_ASSERT(
			EQ::skills::autoskill::ClampPersistentReuseTime(std::numeric_limits<int>::max()) ==
			std::numeric_limits<int>::max()
		);
	}

	void ArbitratesReuseTimerReadiness() {
		using namespace EQ::skills;
		using namespace EQ::skills::autoskill;

		const auto kick_enabled = SetEnabled(0, SkillKick, true);
		TEST_ASSERT(ShouldEnforceReuseTimer(kick_enabled, SkillKick, true));
		TEST_ASSERT(ShouldEnforceReuseTimer(kick_enabled, SkillBash, true));
		TEST_ASSERT(!ShouldEnforceReuseTimer(kick_enabled, SkillTigerClaw, true));
		TEST_ASSERT(!CanUseReuseTimer(kick_enabled, SkillKick, true, false));
		TEST_ASSERT(!CanUseReuseTimer(kick_enabled, SkillBash, true, false));
		TEST_ASSERT(CanUseReuseTimer(kick_enabled, SkillKick, true, true));
		TEST_ASSERT(CanUseReuseTimer(kick_enabled, SkillTigerClaw, true, false));

		const auto tiger_claw_enabled = SetEnabled(0, SkillTigerClaw, true);
		TEST_ASSERT(ShouldEnforceReuseTimer(tiger_claw_enabled, SkillTigerClaw, true));
		TEST_ASSERT(!ShouldEnforceReuseTimer(tiger_claw_enabled, SkillKick, true));
		TEST_ASSERT(ShouldEnforceReuseTimer(tiger_claw_enabled, SkillKick, false));
		TEST_ASSERT(!CanUseReuseTimer(tiger_claw_enabled, SkillTigerClaw, true, false));
		TEST_ASSERT(CanUseReuseTimer(tiger_claw_enabled, SkillKick, true, false));
		TEST_ASSERT(!CanUseReuseTimer(tiger_claw_enabled, SkillKick, false, false));

		auto both_timers_enabled = SetEnabled(kick_enabled, SkillTigerClaw, true);
		TEST_ASSERT(ShouldEnforceReuseTimer(both_timers_enabled, SkillKick, true));
		TEST_ASSERT(ShouldEnforceReuseTimer(both_timers_enabled, SkillTigerClaw, true));

		TEST_ASSERT(!ShouldEnforceReuseTimer(0, SkillKick, true));
		TEST_ASSERT(!ShouldEnforceReuseTimer(kick_enabled, SkillTaunt, true));
		TEST_ASSERT(CanUseReuseTimer(0, SkillKick, true, false));
		TEST_ASSERT(CanUseReuseTimer(kick_enabled, SkillTaunt, true, false));
		TEST_ASSERT(!CanUseReuseTimer(0, SkillKick, true, false, true));
		TEST_ASSERT(CanUseReuseTimer(0, SkillKick, true, true, true));
		TEST_ASSERT(CanUseReuseTimer(0, SkillTaunt, true, false, true));
	}

	void SelectsAutoSkillProcReuseTimes() {
		TEST_ASSERT(EQ::skills::autoskill::ShouldUseAutoSkillProcReuseTime(true, true));
		TEST_ASSERT(!EQ::skills::autoskill::ShouldUseAutoSkillProcReuseTime(true, false));
		TEST_ASSERT(!EQ::skills::autoskill::ShouldUseAutoSkillProcReuseTime(false, true));
		TEST_ASSERT(!EQ::skills::autoskill::ShouldUseAutoSkillProcReuseTime(false, false));
	}
};
