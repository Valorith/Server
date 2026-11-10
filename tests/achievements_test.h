#pragma once

#include "cppunit/cpptest.h"
#include "../common/achievement_mutations.h"
#include "../common/achievements.h"
#include "../common/rulesys.h"
#include "../common/skills.h"
#include "../common/types.h"
#include "../common/compression.h"
#include "../zone/achievement_manager.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

class AchievementsTest : public Test::Suite
{
public:
	AchievementsTest()
	{
		TEST_ADD(AchievementsTest::DefinitionLayout);
		TEST_ADD(AchievementsTest::CategoryParentLayout);
		TEST_ADD(AchievementsTest::ActiveCategorySelection);
		TEST_ADD(AchievementsTest::CompressedEnvelope);
		TEST_ADD(AchievementsTest::PackedStateLayout);
		TEST_ADD(AchievementsTest::DenseInitialStateLayout);
		TEST_ADD(AchievementsTest::EmptyInitializationLayout);
		TEST_ADD(AchievementsTest::IncrementalAndProgressLayouts);
		TEST_ADD(AchievementsTest::LinkDataLayout);
		TEST_ADD(AchievementsTest::EarnedNotificationLayout);
		TEST_ADD(AchievementsTest::DenseIncrementalValidation);
		TEST_ADD(AchievementsTest::ComparisonCountsLayout);
		TEST_ADD(AchievementsTest::NpcNameIdentityHashLayout);
		TEST_ADD(AchievementsTest::SkillWildcardDoesNotAliasSkillZero);
		TEST_ADD(AchievementsTest::TypeThreeIsPresentationOnly);
		TEST_ADD(AchievementsTest::MutationRequestValidation);
		TEST_ADD(AchievementsTest::RewardSequenceOrdering);
		TEST_ADD(AchievementsTest::GuildMemberNotificationRule);
		TEST_ADD(AchievementsTest::NearbyPlayerNotificationRules);
	}

private:
	void RewardSequenceOrdering()
	{
		std::vector<AchievementReward> rewards(4);
		rewards[0].reward_row_id = 40;
		rewards[0].sequence = 2;
		rewards[1].reward_row_id = 30;
		rewards[1].sequence = 1;
		rewards[2].reward_row_id = 20;
		rewards[2].sequence = 2;
		rewards[3].reward_row_id = 10;
		rewards[3].sequence = 1;

		std::sort(
			rewards.begin(),
			rewards.end(),
			AchievementRewardSequenceLess
		);

		TEST_ASSERT(rewards[0].reward_row_id == 10);
		TEST_ASSERT(rewards[1].reward_row_id == 30);
		TEST_ASSERT(rewards[2].reward_row_id == 20);
		TEST_ASSERT(rewards[3].reward_row_id == 40);
	}

	void MutationRequestValidation()
	{
		using namespace AchievementMutations;

		TEST_ASSERT(static_cast<uint8_t>(Status::Pending) == 0);
		TEST_ASSERT(static_cast<uint8_t>(Status::Blocked) == 1);
		TEST_ASSERT(static_cast<uint8_t>(Status::Processing) == 2);
		TEST_ASSERT(ProcessingLeaseSeconds > 0);

		Request advance{
			.target_id = 42,
			.achievement_id = 100,
			.component_id = 7,
			.value = 3,
			.definition_version = 2,
			.target_type = TargetType::Character,
			.operation = Operation::Advance,
			.component_type = 1
		};
		TEST_ASSERT(IsValidRequest(advance));
		advance.component_id = 0;
		TEST_ASSERT(IsValidRequest(advance));
		advance.component_id = 7;

		advance.reserved32 = 1;
		TEST_ASSERT(!IsValidRequest(advance));
		advance.reserved32 = 0;
		advance.target_type = TargetType::SharedTask;
		advance.target_id = std::numeric_limits<uint64_t>::max();
		TEST_ASSERT(!IsValidRequest(advance));

		Request completion{
			.target_id = 9,
			.achievement_id = 100,
			.definition_version = 2,
			.target_type = TargetType::Raid,
			.operation = Operation::Complete
		};
		TEST_ASSERT(IsValidRequest(completion));
		completion.value = 1;
		TEST_ASSERT(!IsValidRequest(completion));
	}

	void GuildMemberNotificationRule()
	{
		auto *rules = RuleManager::Instance();
		std::string original_value;
		TEST_ASSERT(rules->GetRule("Achievements:GuildMemberNotifications", original_value));

		struct RuleRestorer {
			RuleManager *rules;
			std::string value;

			~RuleRestorer()
			{
				rules->SetRule("Achievements:GuildMemberNotifications", value);
			}
		} rule_restorer{rules, original_value};

		TEST_ASSERT(rules->SetRule("Achievements:GuildMemberNotifications", "false"));
		TEST_ASSERT(!RuleB(Achievements, GuildMemberNotifications));
		TEST_ASSERT(rules->SetRule("Achievements:GuildMemberNotifications", "true"));
		TEST_ASSERT(RuleB(Achievements, GuildMemberNotifications));
	}

	void NearbyPlayerNotificationRules()
	{
		auto *rules = RuleManager::Instance();
		std::string original_enabled;
		std::string original_distance;
		TEST_ASSERT(rules->GetRule("Achievements:NearbyPlayerNotifications", original_enabled));
		TEST_ASSERT(rules->GetRule("Achievements:NearbyPlayerNotificationDistance", original_distance));

		struct RuleRestorer {
			RuleManager *rules;
			std::string enabled;
			std::string distance;

			~RuleRestorer()
			{
				rules->SetRule("Achievements:NearbyPlayerNotifications", enabled);
				rules->SetRule("Achievements:NearbyPlayerNotificationDistance", distance);
			}
		} rule_restorer{rules, original_enabled, original_distance};

		TEST_ASSERT(rules->SetRule("Achievements:NearbyPlayerNotifications", "false"));
		TEST_ASSERT(!RuleB(Achievements, NearbyPlayerNotifications));
		TEST_ASSERT(rules->SetRule("Achievements:NearbyPlayerNotifications", "true"));
		TEST_ASSERT(RuleB(Achievements, NearbyPlayerNotifications));
		TEST_ASSERT(rules->SetRule("Achievements:NearbyPlayerNotificationDistance", "375"));
		TEST_ASSERT(RuleI(Achievements, NearbyPlayerNotificationDistance) == 375);
	}

	struct Reader {
		const unsigned char *data;
		size_t size;
		size_t position = 0;

		template <typename T>
		T Read()
		{
			if (position + sizeof(T) > size) {
				throw std::out_of_range("achievement test packet read exceeds buffer");
			}
			T value{};
			std::memcpy(&value, data + position, sizeof(T));
			position += sizeof(T);
			return value;
		}

		std::string ReadString()
		{
			const auto start = position;
			while (position < size && data[position] != 0) {
				++position;
			}
			if (position >= size) {
				throw std::out_of_range("achievement test string is not NUL terminated");
			}
			std::string value(reinterpret_cast<const char *>(data + start), position - start);
			++position;
			return value;
		}
	};

	static EQ::Achievements::Definition TestDefinition()
	{
		using namespace EQ::Achievements;
		Definition definition;
		definition.achievement_id = 42;
		definition.name = "A";
		definition.description = "B";
		definition.icon_id = 77;
		definition.definition_version = 2;
		definition.components[1].push_back({101, 1, 2, 5, "Reach five", "", 2});
		definition.components[3].push_back({303, 3, 9, 1, "Presentation only", "", 9});
		definition.points = 10;
		definition.reward_display = 1;
		return definition;
	}

	void DefinitionLayout()
	{
		using namespace EQ::Achievements;
		Category category;
		category.category_id = 10;
		category.parent_category_id = 0;
		category.name = "General";
		category.description = "General achievements";
		category.icon = "Achievement";
		category.display_order = 4;
		category.associations.push_back({42, "", 7});
		category.child_category_ids.push_back(11);

		auto packet = SerializeDefinitions({category}, {TestDefinition()});
		Reader reader{packet.buffer(), packet.size()};

		TEST_ASSERT(reader.Read<uint32_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 10);
		TEST_ASSERT(reader.Read<int32_t>() == -1);
		TEST_ASSERT(reader.ReadString() == "General");
		TEST_ASSERT(reader.ReadString() == "General achievements");
		TEST_ASSERT(reader.ReadString() == "Achievement");
		TEST_ASSERT(reader.Read<uint32_t>() == 4);
		TEST_ASSERT(reader.Read<uint32_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 42);
		TEST_ASSERT(reader.ReadString().empty());
		TEST_ASSERT(reader.Read<uint32_t>() == 7);
		TEST_ASSERT(reader.Read<uint32_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 11);

		TEST_ASSERT(reader.Read<uint32_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 42);
		TEST_ASSERT(reader.ReadString() == "A");
		TEST_ASSERT(reader.ReadString() == "B");
		TEST_ASSERT(reader.Read<uint32_t>() == 77);
		TEST_ASSERT(reader.Read<uint8_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 2);
		TEST_ASSERT(reader.Read<uint32_t>() == 0);
		TEST_ASSERT(reader.Read<uint32_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 101);
		TEST_ASSERT(reader.Read<uint8_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 5);
		TEST_ASSERT(reader.ReadString() == "Reach five");
		TEST_ASSERT(reader.ReadString().empty());
		TEST_ASSERT(reader.Read<uint8_t>() == 2);
		TEST_ASSERT(reader.Read<uint32_t>() == 0);
		TEST_ASSERT(reader.Read<uint32_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 303);
		TEST_ASSERT(reader.Read<uint8_t>() == 3);
		TEST_ASSERT(reader.Read<uint32_t>() == 1);
		TEST_ASSERT(reader.ReadString() == "Presentation only");
		TEST_ASSERT(reader.ReadString().empty());
		TEST_ASSERT(reader.Read<uint8_t>() == 9);
		TEST_ASSERT(reader.Read<uint32_t>() == 10);
		TEST_ASSERT(reader.Read<uint32_t>() == 1);
		TEST_ASSERT(reader.position == reader.size);
	}

	void CategoryParentLayout()
	{
		using namespace EQ::Achievements;
		Category root;
		root.category_id = 10;
		root.child_category_ids.push_back(11);

		Category child;
		child.category_id = 11;
		child.parent_category_id = 10;

		auto packet = SerializeDefinitions({root, child}, {});
		Reader reader{packet.buffer(), packet.size()};

		TEST_ASSERT(reader.Read<uint32_t>() == 2);

		TEST_ASSERT(reader.Read<uint32_t>() == 10);
		TEST_ASSERT(reader.Read<int32_t>() == -1);
		TEST_ASSERT(reader.ReadString().empty());
		TEST_ASSERT(reader.ReadString().empty());
		TEST_ASSERT(reader.ReadString().empty());
		TEST_ASSERT(reader.Read<uint32_t>() == 0);
		TEST_ASSERT(reader.Read<uint32_t>() == 0);
		TEST_ASSERT(reader.Read<uint32_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 11);

		TEST_ASSERT(reader.Read<uint32_t>() == 11);
		TEST_ASSERT(reader.Read<int32_t>() == 10);
		TEST_ASSERT(reader.ReadString().empty());
		TEST_ASSERT(reader.ReadString().empty());
		TEST_ASSERT(reader.ReadString().empty());
		TEST_ASSERT(reader.Read<uint32_t>() == 0);
		TEST_ASSERT(reader.Read<uint32_t>() == 0);
		TEST_ASSERT(reader.Read<uint32_t>() == 0);

		TEST_ASSERT(reader.Read<uint32_t>() == 0);
		TEST_ASSERT(reader.position == reader.size);
	}

	void ActiveCategorySelection()
	{
		using namespace EQ::Achievements;
		Category root;
		root.category_id = 10;

		Category active_child;
		active_child.category_id = 11;
		active_child.parent_category_id = 10;
		active_child.associations.push_back({42, "", 0});

		Category empty_child;
		empty_child.category_id = 12;
		empty_child.parent_category_id = 10;

		Category empty_root;
		empty_root.category_id = 20;

		const auto selected = SelectActiveCategories({
			root,
			active_child,
			empty_child,
			empty_root
		});
		TEST_ASSERT(selected.size() == 2);
		TEST_ASSERT(selected[0].category_id == 10);
		TEST_ASSERT(selected[0].child_category_ids.size() == 1);
		TEST_ASSERT(selected[0].child_category_ids[0] == 11);
		TEST_ASSERT(selected[1].category_id == 11);
		TEST_ASSERT(selected[1].child_category_ids.empty());

		Category missing_parent;
		missing_parent.category_id = 30;
		missing_parent.parent_category_id = 999;
		missing_parent.associations.push_back({43, "", 0});
		const std::vector<Category> missing_parent_categories = {missing_parent};
		TEST_THROWS(
			SelectActiveCategories(missing_parent_categories),
			std::invalid_argument
		);

		Category cycle_a;
		cycle_a.category_id = 40;
		cycle_a.parent_category_id = 41;
		cycle_a.associations.push_back({44, "", 0});
		Category cycle_b;
		cycle_b.category_id = 41;
		cycle_b.parent_category_id = 40;
		const std::vector<Category> cycle_categories = {cycle_a, cycle_b};
		TEST_THROWS(
			SelectActiveCategories(cycle_categories),
			std::invalid_argument
		);

		Category reserved_id;
		reserved_id.associations.push_back({45, "", 0});
		const std::vector<Category> reserved_id_categories = {reserved_id};
		TEST_THROWS(
			SelectActiveCategories(reserved_id_categories),
			std::invalid_argument
		);
	}

	void CompressedEnvelope()
	{
		using namespace EQ::Achievements;
		auto definitions = SerializeDefinitions({}, {TestDefinition()});
		auto compressed = CompressDefinitions(definitions);
		Reader reader{compressed.buffer(), compressed.size()};
		const auto uncompressed_size = reader.Read<uint32_t>();
		TEST_ASSERT(uncompressed_size == definitions.size());

		std::vector<char> inflated(uncompressed_size);
		const auto inflated_size = EQ::InflateData(
			reinterpret_cast<const char *>(compressed.buffer() + reader.position),
			static_cast<uint32_t>(compressed.size() - reader.position),
			inflated.data(),
			uncompressed_size
		);
		TEST_ASSERT(inflated_size == definitions.size());
		TEST_ASSERT(std::memcmp(inflated.data(), definitions.buffer(), definitions.size()) == 0);
	}

	void PackedStateLayout()
	{
		using namespace EQ::Achievements;
		Definition definition;
		definition.components[1].resize(17);
		definition.components[2].resize(1);
		definition.components[0].resize(2);

		State state;
		state.status = Status::Completed;
		state.satisfied[1].resize(17);
		state.satisfied[1][0] = 1;
		state.satisfied[1][15] = 1;
		state.satisfied[1][16] = 1;
		state.satisfied[2] = {1};
		state.satisfied[0] = {0, 1};
		state.completion_timestamp = 0x12345678;

		SerializeBuffer packet;
		SerializeState(packet, definition, state);
		Reader reader{packet.buffer(), packet.size()};
		TEST_ASSERT(reader.Read<int16_t>() == 0);
		TEST_ASSERT(reader.Read<uint16_t>() == 0x8001);
		TEST_ASSERT(reader.Read<uint16_t>() == 0x0001);
		TEST_ASSERT(reader.Read<uint16_t>() == 0x0001);
		TEST_ASSERT(reader.Read<uint16_t>() == 0x0002);
		TEST_ASSERT(reader.Read<uint32_t>() == 0x12345678);
		TEST_ASSERT(reader.position == reader.size);

		State mismatched;
		mismatched.satisfied[1] = {1};
		mismatched.satisfied[2] = {1, 1};
		mismatched.counts[1] = {7};
		mismatched.counts[2] = {8, 9};

		SerializeBuffer mismatched_packet;
		SerializeState(mismatched_packet, definition, mismatched, true);
		Reader mismatched_reader{
			mismatched_packet.buffer(),
			mismatched_packet.size()
		};
		TEST_ASSERT(mismatched_reader.Read<int16_t>() == 1);
		TEST_ASSERT(mismatched_reader.Read<uint16_t>() == 0x0001);
		TEST_ASSERT(mismatched_reader.Read<uint16_t>() == 0x0000);
		TEST_ASSERT(mismatched_reader.Read<uint16_t>() == 0x0001);
		TEST_ASSERT(mismatched_reader.Read<uint16_t>() == 0x0000);
		TEST_ASSERT(mismatched_reader.Read<uint32_t>() == 7);
		for (size_t index = 1; index < 17; ++index) {
			TEST_ASSERT(mismatched_reader.Read<uint32_t>() == 0);
		}
		TEST_ASSERT(mismatched_reader.Read<uint32_t>() == 8);
		TEST_ASSERT(mismatched_reader.Read<uint32_t>() == 0);
		TEST_ASSERT(mismatched_reader.Read<uint32_t>() == 0);
		TEST_ASSERT(mismatched_reader.position == mismatched_reader.size);
	}

	void IncrementalAndProgressLayouts()
	{
		using namespace EQ::Achievements;
		auto definition = TestDefinition();
		State state;
		state.satisfied[1] = {1};
		auto update = SerializeIncremental(17, {definition}, {{0, state}});
		Reader state_reader{update.buffer(), update.size()};
		TEST_ASSERT(state_reader.Read<uint32_t>() == 17);
		TEST_ASSERT(state_reader.Read<uint8_t>() == 0);
		TEST_ASSERT(state_reader.Read<uint32_t>() == 1);
		TEST_ASSERT(state_reader.Read<uint32_t>() == 0);
		TEST_ASSERT(state_reader.Read<int16_t>() == 1);
		TEST_ASSERT(state_reader.Read<uint16_t>() == 1);
		TEST_ASSERT(state_reader.position == state_reader.size);

		auto progress = SerializeProgress({{42, 101, 3, 1, 4}});
		Reader progress_reader{progress.buffer(), progress.size()};
		TEST_ASSERT(progress_reader.Read<uint32_t>() == 1);
		TEST_ASSERT(progress_reader.Read<uint32_t>() == 42);
		TEST_ASSERT(progress_reader.Read<uint32_t>() == 101);
		TEST_ASSERT(progress_reader.Read<uint32_t>() == 3);
		TEST_ASSERT(progress_reader.Read<uint32_t>() == 1);
		TEST_ASSERT(progress_reader.Read<uint32_t>() == 4);
		TEST_ASSERT(progress_reader.position == progress_reader.size);
	}

	void DenseInitialStateLayout()
	{
		using namespace EQ::Achievements;
		auto first = TestDefinition();
		Definition second;
		second.achievement_id = 43;

		State first_state;
		first_state.status = Status::Completed;
		first_state.satisfied[1] = {1};
		first_state.completion_timestamp = 99;
		State second_state;
		second_state.status = Status::Hidden;

		auto update = SerializeDenseUpdate(
			17,
			{first, second},
			{first_state, second_state}
		);
		Reader reader{update.buffer(), update.size()};
		TEST_ASSERT(reader.Read<uint32_t>() == 17);
		TEST_ASSERT(reader.Read<uint8_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 2);
		TEST_ASSERT(reader.Read<int16_t>() == 0);
		TEST_ASSERT(reader.Read<uint16_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 99);
		TEST_ASSERT(reader.Read<int16_t>() == 3);
		TEST_ASSERT(reader.position == reader.size);

		bool rejected = false;
		try {
			SerializeDenseUpdate(18, {first}, {});
		}
		catch (const std::invalid_argument &) {
			rejected = true;
		}
		TEST_ASSERT(rejected);
	}

	void DenseIncrementalValidation()
	{
		using namespace EQ::Achievements;
		auto definition = TestDefinition();
		State state;
		state.satisfied[1] = {1};

		auto dense = SerializeIncremental(18, {definition}, {{0, state}}, true);
		Reader reader{dense.buffer(), dense.size()};
		TEST_ASSERT(reader.Read<uint32_t>() == 18);
		TEST_ASSERT(reader.Read<uint8_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 1);
		TEST_ASSERT(reader.Read<int16_t>() == 1);
		TEST_ASSERT(reader.Read<uint16_t>() == 1);
		TEST_ASSERT(reader.position == reader.size);

		bool rejected = false;
		try {
			SerializeIncremental(19, {definition}, {{1, state}}, true);
		}
		catch (const std::invalid_argument &) {
			rejected = true;
		}
		TEST_ASSERT(rejected);
	}

	void LinkDataLayout()
	{
		using namespace EQ::Achievements;
		Definition definition;
		definition.achievement_id = 42;
		definition.components[1].resize(17);
		definition.components[2].resize(1);
		definition.components[0].resize(2);

		State state;
		state.status = Status::Completed;
		state.satisfied[1].resize(17);
		state.satisfied[1][0] = 1;
		state.satisfied[1][15] = 1;
		state.satisfied[1][16] = 1;
		state.satisfied[2] = {1};
		state.satisfied[0] = {0, 1};
		state.counts[1].resize(17);
		state.counts[1][0] = 5;
		state.counts[1][16] = 7;
		state.counts[2] = {8};
		state.counts[0] = {9, 10};
		state.completion_timestamp = 1700000000;

		TEST_ASSERT(
			SerializeLinkData("Alice", definition, state) ==
			"Alice^42^0^-32767^1^1^2^1700000000^"
			"5^0^0^0^0^0^0^0^0^0^0^0^0^0^0^0^7^8^9^10^"
		);

		Definition empty_definition;
		empty_definition.achievement_id = 7;
		State empty_state;
		empty_state.status = Status::Completed;
		empty_state.completion_timestamp = 99;
		TEST_ASSERT(SerializeLinkData("Bob", empty_definition, empty_state) == "Bob^7^0^99^");

		empty_state.status = Status::Open;
		TEST_ASSERT(SerializeLinkData("Bob", empty_definition, empty_state) == "Bob^7^1^");
		empty_state.status = Status::Locked;
		TEST_ASSERT(SerializeLinkData("Bob", empty_definition, empty_state) == "Bob^7^2^");
		empty_state.status = Status::Hidden;
		TEST_ASSERT(SerializeLinkData("Bob", empty_definition, empty_state) == "Bob^7^3^");
	}

	void EarnedNotificationLayout()
	{
		using namespace EQ::Achievements;
		auto earned = SerializeEarnedNotification(
			1234,
			5678,
			RoF2AchievementSoundId,
			"Bob^5678^0^99^"
		);
		Reader reader{earned.buffer(), earned.size()};
		TEST_ASSERT(reader.Read<uint32_t>() == 1234);
		TEST_ASSERT(reader.Read<uint32_t>() == 5678);
		TEST_ASSERT(reader.Read<uint32_t>() == RoF2AchievementSoundId);
		TEST_ASSERT(reader.ReadString() == "Bob^5678^0^99^");
		TEST_ASSERT(reader.position == reader.size);

		auto named = SerializeEarnedNotification(7, 42, 99, "Alice^42^0^99^");
		Reader named_reader{named.buffer(), named.size()};
		TEST_ASSERT(named_reader.Read<uint32_t>() == 7);
		TEST_ASSERT(named_reader.Read<uint32_t>() == 42);
		TEST_ASSERT(named_reader.Read<uint32_t>() == 99);
		TEST_ASSERT(named_reader.ReadString() == "Alice^42^0^99^");
		TEST_ASSERT(named_reader.position == named_reader.size);

		bool rejected = false;
		try {
			SerializeEarnedNotification(7, 42, 99, "");
		}
		catch (const std::invalid_argument &) {
			rejected = true;
		}
		TEST_ASSERT(rejected);
	}

	void ComparisonCountsLayout()
	{
		using namespace EQ::Achievements;
		auto definition = TestDefinition();
		State state;
		state.status = Status::Completed;
		state.satisfied[1] = {1};
		state.counts[1] = {5};
		state.completion_timestamp = 99;

		auto comparison = SerializeComparison("Alice", 42, definition, state, 7);
		TEST_ASSERT(
			comparison.size() ==
			ComparisonPayloadSize(
				std::string("Alice").size(),
				definition,
				Status::Completed
			)
		);
		Reader reader{comparison.buffer(), comparison.size()};
		TEST_ASSERT(reader.ReadString() == "Alice");
		TEST_ASSERT(reader.Read<uint32_t>() == 42);
		TEST_ASSERT(reader.Read<int16_t>() == 0);
		TEST_ASSERT(reader.Read<uint16_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 99);
		TEST_ASSERT(reader.Read<uint32_t>() == 5);
		TEST_ASSERT(reader.Read<uint8_t>() == 7);
		TEST_ASSERT(reader.position == reader.size);

		state.status = Status::Open;
		auto open_comparison =
			SerializeComparison("Alice", 42, definition, state, 0);
		TEST_ASSERT(
			open_comparison.size() ==
			ComparisonPayloadSize(
				std::string("Alice").size(),
				definition,
				Status::Open
			)
		);
	}

	void EmptyInitializationLayout()
	{
		using namespace EQ::Achievements;

		auto definitions = SerializeDefinitions({}, {});
		Reader definitions_reader{definitions.buffer(), definitions.size()};
		TEST_ASSERT(definitions_reader.Read<uint32_t>() == 0);
		TEST_ASSERT(definitions_reader.Read<uint32_t>() == 0);
		TEST_ASSERT(definitions_reader.position == definitions_reader.size);

		auto compressed = CompressDefinitions(definitions);
		Reader compressed_reader{compressed.buffer(), compressed.size()};
		TEST_ASSERT(compressed_reader.Read<uint32_t>() == definitions.size());
		std::vector<char> inflated(definitions.size());
		const auto inflated_size = EQ::InflateData(
			reinterpret_cast<const char *>(
				compressed.buffer() + compressed_reader.position
			),
			static_cast<uint32_t>(
				compressed.size() - compressed_reader.position
			),
			inflated.data(),
			static_cast<uint32_t>(inflated.size())
		);
		TEST_ASSERT(inflated_size == definitions.size());
		TEST_ASSERT(
			std::memcmp(inflated.data(), definitions.buffer(), definitions.size()) == 0
		);

		auto snapshot = SerializeSnapshot({}, {});
		TEST_ASSERT(snapshot.size() == 0);

		auto dense = SerializeDenseUpdate(1, {}, {});
		Reader dense_reader{dense.buffer(), dense.size()};
		TEST_ASSERT(dense_reader.Read<uint32_t>() == 1);
		TEST_ASSERT(dense_reader.Read<uint8_t>() == 1);
		TEST_ASSERT(dense_reader.Read<uint32_t>() == 0);
		TEST_ASSERT(dense_reader.position == dense_reader.size);

		auto progress = SerializeProgress({});
		Reader progress_reader{progress.buffer(), progress.size()};
		TEST_ASSERT(progress_reader.Read<uint32_t>() == 0);
		TEST_ASSERT(progress_reader.position == progress_reader.size);
	}

	void SkillWildcardDoesNotAliasSkillZero()
	{
		using namespace EQ::Achievements;
		TEST_ASSERT(static_cast<uint32_t>(EQ::skills::Skill1HBlunt) == 0);
		TEST_ASSERT(SkillWildcardTargetId != static_cast<uint32_t>(EQ::skills::Skill1HBlunt));
		TEST_ASSERT(SkillWildcardTargetId > static_cast<uint32_t>(EQ::skills::HIGHEST_SKILL));
	}

	void NpcNameIdentityHashLayout()
	{
		using namespace EQ::Achievements;

		static_assert(NpcNameIdentityHash("Vishimtar_the_Fallen00") == 0x708BEE77u);
		static_assert(NpcNameIdentityHash("Tunare's Guardian") == 0xCF16724Eu);
		TEST_ASSERT(static_cast<uint8_t>(EventType::NpcNameKill) == 12);
		TEST_ASSERT(static_cast<uint8_t>(EventType::SkillCap) == 13);
		TEST_ASSERT(NpcNameIdentityHash("Vishimtar_the_Fallen00") == 0x708BEE77u);
		TEST_ASSERT(
			NpcNameIdentityHash("  VISHIMTAR__the   Fallen 99 ") ==
			0x708BEE77u
		);
		TEST_ASSERT(NpcNameIdentityHash("Tunare`s_Guardian00") == 0xCF16724Eu);
		TEST_ASSERT(NpcNameIdentityHash("Tunare's Guardian") == 0xCF16724Eu);
		TEST_ASSERT(NpcNameIdentityHash("#A_Rat_01") == 0x40FF2A77u);
		TEST_ASSERT(NpcNameIdentityHash(" 123_#- ") == 0);
		TEST_ASSERT(NpcNameIdentityHash("\xC3\x89") == 0);
	}

	void TypeThreeIsPresentationOnly()
	{
		using namespace EQ::Achievements;
		bool rejected = false;
		try {
			SerializeProgress({{42, 303, 9, 3, 1}});
		}
		catch (const std::invalid_argument &) {
			rejected = true;
		}
		TEST_ASSERT(rejected);
	}
};
