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
		TEST_ADD(AutoSkillUtilTest::KeepsPriorityOrder);
		TEST_ADD(AutoSkillUtilTest::CalculatesReuseTimes);
		TEST_ADD(AutoSkillUtilTest::RoundsReuseTimesUpAcrossRange);
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
};
