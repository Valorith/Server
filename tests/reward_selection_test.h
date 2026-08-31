#pragma once

#include "common/reward_selection.h"
#include "cppunit/cpptest.h"
#include "zone/reward_selection.h"

#include <cstring>
#include <stdexcept>
#include <string>

class RewardSelectionTest : public Test::Suite
{
public:
	RewardSelectionTest()
	{
		TEST_ADD(RewardSelectionTest::DisplayLayout);
		TEST_ADD(RewardSelectionTest::AdditionalRewardTypesLayout);
		TEST_ADD(RewardSelectionTest::ValidationAndFailedClaimLayout);
		TEST_ADD(RewardSelectionTest::ClaimReplyLayout);
		TEST_ADD(RewardSelectionTest::RewardDefinitionValidation);
		TEST_ADD(RewardSelectionTest::ScriptRewardTypeParsing);
	}

private:
	struct Reader {
		const unsigned char *data;
		size_t size;
		size_t position = 0;

		template <typename T>
		T Read()
		{
			if (position + sizeof(T) > size) {
				throw std::out_of_range("reward test packet read exceeds buffer");
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
				throw std::out_of_range("reward test string is not NUL terminated");
			}
			std::string value(
				reinterpret_cast<const char *>(data + start),
				position - start
			);
			++position;
			return value;
		}
	};

	void DisplayLayout()
	{
		using namespace EQ::RewardSelection;

		auto empty = SerializeDisplay(nullptr);
		Reader empty_reader{empty.buffer(), empty.size()};
		TEST_ASSERT(empty_reader.Read<uint32_t>() == ActionList);
		TEST_ASSERT(empty_reader.Read<uint8_t>() == 0);
		TEST_ASSERT(empty_reader.position == empty_reader.size);
		auto clear = SerializeDisplayClear();
		Reader clear_reader{clear.buffer(), clear.size()};
		TEST_ASSERT(clear_reader.Read<uint32_t>() == ActionBulk);
		TEST_ASSERT(clear_reader.Read<uint32_t>() == 0);
		TEST_ASSERT(clear_reader.Read<uint8_t>() == 0);
		TEST_ASSERT(clear_reader.position == clear_reader.size);

		DisplaySet reward_set;
		reward_set.pending_reward_id = 42;
		reward_set.reward_set_id = 700;
		reward_set.title = "A Reward";
		reward_set.amount_multiplier_bits = 0x3f800000;

		DisplaySubset common;
		common.subset_id = 701;
		common.common_to_all = true;
		common.option_label = "Always";
		DisplayEntry money;
		money.wire_type = WireType::Money;
		money.description = "Pocket money";
		money.values = {4, 3, 2, 1};
		common.entries.push_back(money);
		reward_set.subsets.push_back(common);

		DisplaySubset choice;
		choice.subset_id = 702;
		choice.option_label = "Choose this";
		DisplayEntry item;
		item.wire_type = WireType::Item;
		item.fields = {10, 11};
		item.description = "An item";
		item.items.push_back({1001, 2, 1, 99, "Item link"});
		choice.entries.push_back(item);
		DisplayEntry aa_points;
		aa_points.wire_type = WireType::AlternateAdvancementPoints;
		aa_points.description = "AA";
		aa_points.values[0] = 5;
		aa_points.flag = 1;
		choice.entries.push_back(aa_points);
		reward_set.subsets.push_back(choice);

		auto packet = SerializeDisplay(&reward_set);
		Reader reader{packet.buffer(), packet.size()};
		TEST_ASSERT(reader.Read<uint32_t>() == ActionList);
		TEST_ASSERT(reader.Read<uint8_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 42);
		TEST_ASSERT(reader.Read<uint32_t>() == 700);
		TEST_ASSERT(reader.ReadString() == "A Reward");
		TEST_ASSERT(reader.Read<uint32_t>() == 0x3f800000);
		TEST_ASSERT(reader.Read<uint32_t>() == 700);
		TEST_ASSERT(reader.Read<int32_t>() == 2);

		TEST_ASSERT(reader.Read<uint32_t>() == 701);
		TEST_ASSERT(reader.Read<uint32_t>() == 1);
		TEST_ASSERT(reader.Read<uint8_t>() == 1);
		TEST_ASSERT(reader.Read<uint8_t>() == 0);
		TEST_ASSERT(reader.ReadString() == "Always");
		TEST_ASSERT(reader.Read<uint32_t>() == static_cast<uint32_t>(WireType::Money));
		TEST_ASSERT(reader.Read<uint32_t>() == 0);
		TEST_ASSERT(reader.Read<uint32_t>() == 0);
		TEST_ASSERT(reader.ReadString() == "Pocket money");
		TEST_ASSERT(reader.Read<uint32_t>() == 4);
		TEST_ASSERT(reader.Read<uint32_t>() == 3);
		TEST_ASSERT(reader.Read<uint32_t>() == 2);
		TEST_ASSERT(reader.Read<uint32_t>() == 1);

		TEST_ASSERT(reader.Read<uint32_t>() == 702);
		TEST_ASSERT(reader.Read<uint32_t>() == 2);
		TEST_ASSERT(reader.Read<uint8_t>() == 0);
		TEST_ASSERT(reader.Read<uint8_t>() == 0);
		TEST_ASSERT(reader.ReadString() == "Choose this");
		TEST_ASSERT(reader.Read<uint32_t>() == static_cast<uint32_t>(WireType::Item));
		TEST_ASSERT(reader.Read<uint32_t>() == 10);
		TEST_ASSERT(reader.Read<uint32_t>() == 11);
		TEST_ASSERT(reader.ReadString() == "An item");
		TEST_ASSERT(reader.Read<uint32_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 1001);
		TEST_ASSERT(reader.Read<uint32_t>() == 2);
		TEST_ASSERT(reader.Read<uint8_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 99);
		TEST_ASSERT(reader.ReadString() == "Item link");
		TEST_ASSERT(
			reader.Read<uint32_t>() ==
			static_cast<uint32_t>(WireType::AlternateAdvancementPoints)
		);
		TEST_ASSERT(reader.Read<uint32_t>() == 0);
		TEST_ASSERT(reader.Read<uint32_t>() == 0);
		TEST_ASSERT(reader.ReadString() == "AA");
		TEST_ASSERT(reader.Read<uint32_t>() == 5);
		TEST_ASSERT(reader.Read<uint8_t>() == 1);
		TEST_ASSERT(reader.position == reader.size);

		auto second_reward_set = reward_set;
		second_reward_set.pending_reward_id = 43;
		second_reward_set.reward_set_id = 800;
		second_reward_set.title = "Another Reward";
		auto second_packet = SerializeDisplay(&second_reward_set);
		auto bulk = SerializeDisplays({reward_set, second_reward_set});
		Reader bulk_reader{bulk.buffer(), bulk.size()};
		TEST_ASSERT(bulk_reader.Read<uint32_t>() == ActionBulk);
		TEST_ASSERT(bulk_reader.Read<int32_t>() == 2);
		TEST_ASSERT(bulk_reader.Read<uint8_t>() == 0);
		constexpr size_t single_header_size = sizeof(uint32_t) + sizeof(uint8_t);
		TEST_ASSERT(
			bulk.size() ==
			sizeof(uint32_t) + sizeof(int32_t) + sizeof(uint8_t) +
			packet.size() - single_header_size +
			second_packet.size() - single_header_size
		);
		TEST_ASSERT(
			std::memcmp(
				bulk.buffer() + bulk_reader.position,
				packet.buffer() + single_header_size,
				packet.size() - single_header_size
			) == 0
		);
		TEST_ASSERT(
			std::memcmp(
				bulk.buffer() + bulk_reader.position +
					packet.size() - single_header_size,
				second_packet.buffer() + single_header_size,
				second_packet.size() - single_header_size
			) == 0
		);

		reward_set.subsets.push_back(choice);
		TEST_THROWS(SerializeDisplay(&reward_set), std::invalid_argument);
	}

	void ClaimReplyLayout()
	{
		using namespace EQ::RewardSelection;
		auto packet = SerializeClaimReply(42, 700, 702, true);
		Reader reader{packet.buffer(), packet.size()};
		TEST_ASSERT(reader.Read<uint32_t>() == ActionClaim);
		TEST_ASSERT(reader.Read<uint32_t>() == 42);
		TEST_ASSERT(reader.Read<uint32_t>() == 700);
		TEST_ASSERT(reader.Read<uint32_t>() == 702);
		TEST_ASSERT(reader.Read<uint8_t>() == 1);
		TEST_ASSERT(reader.Read<uint8_t>() == 0);
		TEST_ASSERT(reader.Read<uint8_t>() == 0);
		TEST_ASSERT(reader.Read<uint8_t>() == 0);
		TEST_ASSERT(reader.position == reader.size);
	}

	void AdditionalRewardTypesLayout()
	{
		using namespace EQ::RewardSelection;

		DisplaySet reward_set;
		reward_set.pending_reward_id = 91;
		reward_set.reward_set_id = 92;
		reward_set.title = "Scripted rewards";

		DisplaySubset choice;
		choice.subset_id = 93;
		choice.option_label = "Mixed option";

		DisplayEntry text;
		text.wire_type = WireType::Text;
		text.fields = {1, 2};
		text.description = "Title";
		choice.entries.push_back(text);

		DisplayEntry experience;
		experience.wire_type = WireType::Experience;
		experience.fields = {3, 4};
		experience.description = "Experience";
		choice.entries.push_back(experience);

		DisplayEntry ability;
		ability.wire_type = WireType::AlternateAdvancementAbility;
		ability.fields = {5, 6};
		ability.description = "Ability";
		ability.values = {7, 8, 0, 0};
		choice.entries.push_back(ability);

		DisplayEntry points;
		points.wire_type = WireType::GenericPoints;
		points.fields = {9, 10};
		points.description = "Currency";
		points.values = {11, 12, 13, 14};
		choice.entries.push_back(points);

		reward_set.subsets.push_back(choice);

		auto packet = SerializeDisplay(&reward_set);
		Reader reader{packet.buffer(), packet.size()};
		TEST_ASSERT(reader.Read<uint32_t>() == ActionList);
		TEST_ASSERT(reader.Read<uint8_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 91);
		TEST_ASSERT(reader.Read<uint32_t>() == 92);
		TEST_ASSERT(reader.ReadString() == "Scripted rewards");
		TEST_ASSERT(reader.Read<uint32_t>() == 0x3f800000);
		TEST_ASSERT(reader.Read<uint32_t>() == 92);
		TEST_ASSERT(reader.Read<int32_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 93);
		TEST_ASSERT(reader.Read<uint32_t>() == 4);
		TEST_ASSERT(reader.Read<uint8_t>() == 0);
		TEST_ASSERT(reader.Read<uint8_t>() == 0);
		TEST_ASSERT(reader.ReadString() == "Mixed option");

		TEST_ASSERT(
			reader.Read<uint32_t>() == static_cast<uint32_t>(WireType::Text)
		);
		TEST_ASSERT(reader.Read<uint32_t>() == 1);
		TEST_ASSERT(reader.Read<uint32_t>() == 2);
		TEST_ASSERT(reader.ReadString() == "Title");

		TEST_ASSERT(
			reader.Read<uint32_t>() ==
			static_cast<uint32_t>(WireType::Experience)
		);
		TEST_ASSERT(reader.Read<uint32_t>() == 3);
		TEST_ASSERT(reader.Read<uint32_t>() == 4);
		TEST_ASSERT(reader.ReadString() == "Experience");

		TEST_ASSERT(
			reader.Read<uint32_t>() ==
			static_cast<uint32_t>(WireType::AlternateAdvancementAbility)
		);
		TEST_ASSERT(reader.Read<uint32_t>() == 5);
		TEST_ASSERT(reader.Read<uint32_t>() == 6);
		TEST_ASSERT(reader.ReadString() == "Ability");
		TEST_ASSERT(reader.Read<uint32_t>() == 7);
		TEST_ASSERT(reader.Read<uint32_t>() == 8);

		TEST_ASSERT(
			reader.Read<uint32_t>() ==
			static_cast<uint32_t>(WireType::GenericPoints)
		);
		TEST_ASSERT(reader.Read<uint32_t>() == 9);
		TEST_ASSERT(reader.Read<uint32_t>() == 10);
		TEST_ASSERT(reader.ReadString() == "Currency");
		TEST_ASSERT(reader.Read<uint32_t>() == 11);
		TEST_ASSERT(reader.Read<uint32_t>() == 12);
		TEST_ASSERT(reader.Read<uint32_t>() == 13);
		TEST_ASSERT(reader.Read<uint32_t>() == 14);
		TEST_ASSERT(reader.position == reader.size);
	}

	void ValidationAndFailedClaimLayout()
	{
		using namespace EQ::RewardSelection;

		DisplaySet reward_set;
		reward_set.pending_reward_id = 1;
		reward_set.reward_set_id = 2;
		reward_set.title = "Validation";

		DisplaySubset choice;
		choice.subset_id = 3;
		choice.option_label = "Choice";
		reward_set.subsets.push_back(choice);

		auto invalid = reward_set;
		invalid.pending_reward_id = 0;
		TEST_THROWS(SerializeDisplay(&invalid), std::invalid_argument);

		invalid = reward_set;
		invalid.reward_set_id = 0;
		TEST_THROWS(SerializeDisplay(&invalid), std::invalid_argument);

		invalid = reward_set;
		invalid.subsets.front().subset_id = 0;
		TEST_THROWS(SerializeDisplay(&invalid), std::invalid_argument);

		invalid = reward_set;
		invalid.subsets.push_back(choice);
		TEST_THROWS(SerializeDisplay(&invalid), std::invalid_argument);
		TEST_THROWS(
			SerializeDisplays({reward_set, invalid}),
			std::invalid_argument
		);

		auto failed = SerializeClaimReply(41, 42, 43, false);
		Reader reader{failed.buffer(), failed.size()};
		TEST_ASSERT(reader.Read<uint32_t>() == ActionClaim);
		TEST_ASSERT(reader.Read<uint32_t>() == 41);
		TEST_ASSERT(reader.Read<uint32_t>() == 42);
		TEST_ASSERT(reader.Read<uint32_t>() == 43);
		TEST_ASSERT(reader.Read<uint8_t>() == 0);
		TEST_ASSERT(reader.Read<uint8_t>() == 0);
		TEST_ASSERT(reader.Read<uint8_t>() == 0);
		TEST_ASSERT(reader.Read<uint8_t>() == 0);
		TEST_ASSERT(reader.position == reader.size);
	}

	void RewardDefinitionValidation()
	{
		TEST_ASSERT(
			IsValidRewardSelectionRewardDefinition(
				RewardSelectionRewardType::Item,
				1,
				std::numeric_limits<int16_t>::max()
			)
		);
		TEST_ASSERT(
			!IsValidRewardSelectionRewardDefinition(
				RewardSelectionRewardType::Item,
				1,
				static_cast<uint64_t>(
					std::numeric_limits<int16_t>::max()
				) + 1
			)
		);
		TEST_ASSERT(
			IsValidRewardSelectionRewardDefinition(
				RewardSelectionRewardType::Experience,
				static_cast<uint32_t>(
					RewardSelectionExperienceMode::NormalOnly
				),
				std::numeric_limits<uint32_t>::max()
			)
		);
		TEST_ASSERT(
			!IsValidRewardSelectionRewardDefinition(
				RewardSelectionRewardType::Experience,
				static_cast<uint32_t>(
					RewardSelectionExperienceMode::NormalOnly
				) + 1,
				1
			)
		);
		TEST_ASSERT(
			IsValidRewardSelectionRewardDefinition(
				RewardSelectionRewardType::AlternateAdvancement,
				0,
				std::numeric_limits<int>::max()
			)
		);
		TEST_ASSERT(
			!IsValidRewardSelectionRewardDefinition(
				RewardSelectionRewardType::AlternateAdvancement,
				0,
				static_cast<uint64_t>(std::numeric_limits<int>::max()) + 1
			)
		);
		TEST_ASSERT(
			IsValidRewardSelectionRewardDefinition(
				RewardSelectionRewardType::Copper,
				0,
				static_cast<uint64_t>(
					std::numeric_limits<int32_t>::max()
				) * 1000ULL + 999ULL
			)
		);
		TEST_ASSERT(
			!IsValidRewardSelectionRewardDefinition(
				RewardSelectionRewardType::Copper,
				0,
				static_cast<uint64_t>(
					std::numeric_limits<int32_t>::max()
				) * 1000ULL + 1000ULL
			)
		);
		TEST_ASSERT(
			IsValidRewardSelectionRewardDefinition(
				RewardSelectionRewardType::AlternateCurrency,
				1,
				std::numeric_limits<int>::max()
			)
		);
		TEST_ASSERT(
			!IsValidRewardSelectionRewardDefinition(
				RewardSelectionRewardType::AlternateCurrency,
				1,
				static_cast<uint64_t>(std::numeric_limits<int>::max()) + 1
			)
		);
		TEST_ASSERT(
			IsValidRewardSelectionRewardDefinition(
				RewardSelectionRewardType::Title,
				std::numeric_limits<int>::max(),
				1
			)
		);
		TEST_ASSERT(
			!IsValidRewardSelectionRewardDefinition(
				RewardSelectionRewardType::Title,
				static_cast<uint32_t>(std::numeric_limits<int>::max()) + 1,
				1
			)
		);
		TEST_ASSERT(
			!IsValidRewardSelectionRewardDefinition(
				static_cast<RewardSelectionRewardType>(6),
				1,
				1
			)
		);
	}

	void ScriptRewardTypeParsing()
	{
		auto item = MakeScriptRewardSelectionReward("ITEM", 1001);
		TEST_ASSERT(item);
		TEST_ASSERT(item->type == RewardSelectionRewardType::Item);
		TEST_ASSERT(item->data_id == 1001);
		TEST_ASSERT(item->amount == 1);

		item = MakeScriptRewardSelectionReward("item", 1001, 7, "Sword");
		TEST_ASSERT(item);
		TEST_ASSERT(item->amount == 7);
		TEST_ASSERT(item->description == "Sword");

		auto experience = MakeScriptRewardSelectionReward(
			"experience_no-aa",
			5000
		);
		TEST_ASSERT(experience);
		TEST_ASSERT(experience->type == RewardSelectionRewardType::Experience);
		TEST_ASSERT(
			experience->data_id == static_cast<uint32_t>(
				RewardSelectionExperienceMode::NormalOnly
			)
		);
		TEST_ASSERT(experience->amount == 5000);

		auto aa = MakeScriptRewardSelectionReward("AA", 3);
		TEST_ASSERT(aa);
		TEST_ASSERT(
			aa->type == RewardSelectionRewardType::AlternateAdvancement
		);
		TEST_ASSERT(aa->amount == 3);

		auto money = MakeScriptRewardSelectionReward("money", 1234);
		TEST_ASSERT(money);
		TEST_ASSERT(money->type == RewardSelectionRewardType::Copper);
		TEST_ASSERT(money->amount == 1234);

		auto currency = MakeScriptRewardSelectionReward(
			"Alternate Currency",
			19,
			25
		);
		TEST_ASSERT(currency);
		TEST_ASSERT(
			currency->type == RewardSelectionRewardType::AlternateCurrency
		);
		TEST_ASSERT(currency->data_id == 19);
		TEST_ASSERT(currency->amount == 25);

		auto title = MakeScriptRewardSelectionReward("title", 71);
		TEST_ASSERT(title);
		TEST_ASSERT(title->type == RewardSelectionRewardType::Title);
		TEST_ASSERT(title->data_id == 71);
		TEST_ASSERT(title->amount == 1);

		TEST_ASSERT(!MakeScriptRewardSelectionReward("unknown", 1));
		TEST_ASSERT(!MakeScriptRewardSelectionReward("item!", 1));
		TEST_ASSERT(!MakeScriptRewardSelectionReward("item", 0));
		TEST_ASSERT(!MakeScriptRewardSelectionReward("experience", 1, 1));
		TEST_ASSERT(!MakeScriptRewardSelectionReward("alternate_currency", 1));
		TEST_ASSERT(!MakeScriptRewardSelectionReward("title", 1, 1));
	}
};
