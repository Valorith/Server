#pragma once

#include "common/reward_selection.h"
#include "cppunit/cpptest.h"
#include "zone/reward_selection.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_set>

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
		TEST_ADD(RewardSelectionTest::ItemQuantityValidation);
		TEST_ADD(RewardSelectionTest::StructuredScriptRewardParsing);
		TEST_ADD(RewardSelectionTest::ScriptItemShorthand);
		TEST_ADD(RewardSelectionTest::ScriptMixedOptionShorthand);
		TEST_ADD(RewardSelectionTest::RewardLoreConflictValidation);
		TEST_ADD(RewardSelectionTest::StructuredScriptRewardGrouping);
		TEST_ADD(RewardSelectionTest::StructuredScriptRewardValidation);
		TEST_ADD(RewardSelectionTest::StableWireOptionIdentity);
		TEST_ADD(RewardSelectionTest::CommonOptionGroupingValidation);
		TEST_ADD(RewardSelectionTest::TransientBatchFailureClassification);
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

	void ItemQuantityValidation()
	{
		EQ::ItemData ordinary{};
		ordinary.ID = 5001;
		ordinary.Stackable = false;
		ordinary.MaxCharges = 0;
		TEST_ASSERT(IsValidRewardSelectionItemAmount(&ordinary, 1));
		TEST_ASSERT(!IsValidRewardSelectionItemAmount(&ordinary, 2));

		EQ::ItemData stackable{};
		stackable.ID = 5002;
		stackable.Stackable = true;
		stackable.StackSize = 20;
		TEST_ASSERT(IsValidRewardSelectionItemAmount(&stackable, 20));
		TEST_ASSERT(!IsValidRewardSelectionItemAmount(&stackable, 21));

		EQ::ItemData charged{};
		charged.ID = 5003;
		charged.MaxCharges = 5;
		TEST_ASSERT(IsValidRewardSelectionItemAmount(&charged, 5));
		TEST_ASSERT(!IsValidRewardSelectionItemAmount(&charged, 6));

		EQ::ItemData unlimited{};
		unlimited.ID = 5004;
		unlimited.MaxCharges = -1;
		TEST_ASSERT(IsValidRewardSelectionItemAmount(&unlimited, 1));
		TEST_ASSERT(!IsValidRewardSelectionItemAmount(&unlimited, 2));
		TEST_ASSERT(!IsValidRewardSelectionItemAmount(nullptr, 1));
	}

	void StructuredScriptRewardParsing()
	{
		ScriptRewardSelectionRewardConfig config;
		config.item_id = 1001;
		auto item = MakeScriptRewardSelectionReward(config);
		TEST_ASSERT(item);
		TEST_ASSERT(item->type == RewardSelectionRewardType::Item);
		TEST_ASSERT(item->data_id == 1001);
		TEST_ASSERT(item->amount == 1);

		config = {};
		config.item_id = 1001;
		config.quantity = 7;
		config.description = "Sword bundle";
		item = MakeScriptRewardSelectionReward(config);
		TEST_ASSERT(item);
		TEST_ASSERT(item->amount == 7);
		TEST_ASSERT(item->description == "Sword bundle");

		config = {};
		config.experience = 5000;
		auto experience = MakeScriptRewardSelectionReward(config);
		TEST_ASSERT(experience);
		TEST_ASSERT(experience->type == RewardSelectionRewardType::Experience);
		TEST_ASSERT(
		    experience->data_id ==
		    static_cast<uint32_t>(RewardSelectionExperienceMode::Default));
		TEST_ASSERT(experience->amount == 5000);

		config = {};
		config.experience_no_aa = 6000;
		experience = MakeScriptRewardSelectionReward(config);
		TEST_ASSERT(experience);
		TEST_ASSERT(
		    experience->data_id ==
		    static_cast<uint32_t>(RewardSelectionExperienceMode::NormalOnly));
		TEST_ASSERT(experience->amount == 6000);

		config = {};
		config.aa_points = 3;
		auto aa = MakeScriptRewardSelectionReward(config);
		TEST_ASSERT(aa);
		TEST_ASSERT(
		    aa->type == RewardSelectionRewardType::AlternateAdvancement);
		TEST_ASSERT(aa->amount == 3);

		config = {};
		config.money = 1234;
		auto money = MakeScriptRewardSelectionReward(config);
		TEST_ASSERT(money);
		TEST_ASSERT(money->type == RewardSelectionRewardType::Copper);
		TEST_ASSERT(money->amount == 1234);

		config = {};
		config.alternate_currency_id = 19;
		config.amount = 25;
		auto currency = MakeScriptRewardSelectionReward(config);
		TEST_ASSERT(currency);
		TEST_ASSERT(
		    currency->type == RewardSelectionRewardType::AlternateCurrency);
		TEST_ASSERT(currency->data_id == 19);
		TEST_ASSERT(currency->amount == 25);

		config = {};
		config.title_set_id = 71;
		auto title = MakeScriptRewardSelectionReward(config);
		TEST_ASSERT(title);
		TEST_ASSERT(title->type == RewardSelectionRewardType::Title);
		TEST_ASSERT(title->data_id == 71);
		TEST_ASSERT(title->amount == 1);
	}

	void StructuredScriptRewardGrouping()
	{
		ScriptRewardSelectionOffer offer;
		offer.options.resize(2);
		offer.options[0].option_id = 100;
		offer.options[1].option_id = 200;
		offer.options[0].rewards.push_back({
			.entry_id = 1,
			.type = RewardSelectionRewardType::Item,
			.data_id = 1001,
			.amount = 1
		});
		offer.options[1].rewards.push_back({
			.entry_id = 2,
			.type = RewardSelectionRewardType::Experience,
			.data_id = static_cast<uint32_t>(
				RewardSelectionExperienceMode::Default
			),
			.amount = 5000
		});
		offer.common_rewards.push_back({
			.entry_id = 3,
			.type = RewardSelectionRewardType::Copper,
			.amount = 1000
		});

		uint32_t common_option_id = 0;
		std::string error;
		TEST_ASSERT(AssignScriptRewardSelectionOptionIds(
			offer,
			common_option_id,
			&error
		));
		TEST_ASSERT(error.empty());
		TEST_ASSERT(offer.options[0].option_id == 1);
		TEST_ASSERT(offer.options[1].option_id == 2);
		TEST_ASSERT(common_option_id == 3);

		ScriptRewardSelectionOffer empty;
		TEST_ASSERT(!AssignScriptRewardSelectionOptionIds(
			empty,
			common_option_id,
			&error
		));
		TEST_ASSERT(error == "options must contain at least one choice");
	}

	void ScriptItemShorthand()
	{
		std::string error;
		auto item_option =
		    MakeScriptItemRewardSelectionOption(2001, &error);
		TEST_ASSERT(item_option);
		TEST_ASSERT(error.empty());
		TEST_ASSERT(item_option->rewards.size() == 1);
		TEST_ASSERT(
		    item_option->rewards[0].type == RewardSelectionRewardType::Item);
		TEST_ASSERT(item_option->rewards[0].data_id == 2001);
		TEST_ASSERT(item_option->rewards[0].amount == 1);

		auto offer = MakeScriptItemRewardSelectionOffer(
			{1001, 1002, 1003},
			&error
		);
		TEST_ASSERT(offer);
		TEST_ASSERT(error.empty());
		TEST_ASSERT(offer->title.empty());
		TEST_ASSERT(offer->common_rewards.empty());
		TEST_ASSERT(offer->options.size() == 3);
		for (size_t index = 0; index < offer->options.size(); ++index) {
			const auto &option = offer->options[index];
			TEST_ASSERT(option.label.empty());
			TEST_ASSERT(option.rewards.size() == 1);
			TEST_ASSERT(
				option.rewards[0].type == RewardSelectionRewardType::Item
			);
			TEST_ASSERT(option.rewards[0].data_id == 1001 + index);
			TEST_ASSERT(option.rewards[0].amount == 1);
		}

		uint32_t common_option_id = 0;
		TEST_ASSERT(AssignScriptRewardSelectionOptionIds(
			*offer,
			common_option_id,
			&error
		));
		TEST_ASSERT(offer->options[0].option_id == 1);
		TEST_ASSERT(offer->options[1].option_id == 2);
		TEST_ASSERT(offer->options[2].option_id == 3);
		TEST_ASSERT(common_option_id == 0);

		TEST_ASSERT(!MakeScriptItemRewardSelectionOffer({}, &error));
		TEST_ASSERT(error == "item_ids must contain at least one item ID");

		TEST_ASSERT(!MakeScriptItemRewardSelectionOffer({1001, 0}, &error));
		TEST_ASSERT(
			error == "item_ids[2]: reward value is zero or exceeds the supported range"
		);

		TEST_ASSERT(!MakeScriptItemRewardSelectionOffer(
			{static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1},
			&error
		));
		TEST_ASSERT(error == "item_ids[1]: item_id exceeds the supported range");
	}

	void ScriptMixedOptionShorthand()
	{
		std::string error;
		ScriptRewardSelectionOffer offer;

		auto item = MakeScriptItemRewardSelectionOption(1001, &error);
		TEST_ASSERT(item);
		offer.options.emplace_back(std::move(*item));

		const auto append_reward =
		    [&offer, &error](const ScriptRewardSelectionRewardConfig &config) {
			auto reward = MakeScriptRewardSelectionReward(config, &error);
			if (!reward) {
				return false;
			}
			RewardSelectionOption option;
			option.rewards.emplace_back(std::move(*reward));
			offer.options.emplace_back(std::move(option));
			return true;
		};

		ScriptRewardSelectionRewardConfig config;
		config.experience = 50000;
		TEST_ASSERT(append_reward(config));

		config = {};
		config.experience_no_aa = 50000;
		TEST_ASSERT(append_reward(config));

		config = {};
		config.aa_points = 2;
		TEST_ASSERT(append_reward(config));

		config = {};
		config.money = 10000;
		TEST_ASSERT(append_reward(config));

		config = {};
		config.alternate_currency_id = 19;
		config.amount = 5;
		TEST_ASSERT(append_reward(config));

		config = {};
		config.title_set_id = 7;
		TEST_ASSERT(append_reward(config));

		RewardSelectionOption bundle;
		auto bundle_item = MakeScriptItemRewardSelectionOption(1002, &error);
		TEST_ASSERT(bundle_item);
		bundle.rewards.emplace_back(
		    std::move(bundle_item->rewards.front()));
		config = {};
		config.aa_points = 1;
		auto bundle_aa = MakeScriptRewardSelectionReward(config, &error);
		TEST_ASSERT(bundle_aa);
		bundle.rewards.emplace_back(std::move(*bundle_aa));
		offer.options.emplace_back(std::move(bundle));

		TEST_ASSERT(offer.options.size() == 8);
		TEST_ASSERT(
		    offer.options[0].rewards[0].type ==
		    RewardSelectionRewardType::Item);
		TEST_ASSERT(
		    offer.options[1].rewards[0].type ==
		    RewardSelectionRewardType::Experience);
		TEST_ASSERT(
		    offer.options[2].rewards[0].type ==
		    RewardSelectionRewardType::Experience);
		TEST_ASSERT(
		    offer.options[2].rewards[0].data_id ==
		    static_cast<uint32_t>(RewardSelectionExperienceMode::NormalOnly));
		TEST_ASSERT(
		    offer.options[3].rewards[0].type ==
		    RewardSelectionRewardType::AlternateAdvancement);
		TEST_ASSERT(
		    offer.options[4].rewards[0].type ==
		    RewardSelectionRewardType::Copper);
		TEST_ASSERT(
		    offer.options[5].rewards[0].type ==
		    RewardSelectionRewardType::AlternateCurrency);
		TEST_ASSERT(
		    offer.options[6].rewards[0].type ==
		    RewardSelectionRewardType::Title);
		TEST_ASSERT(offer.options[7].rewards.size() == 2);

		uint32_t common_option_id = 0;
		TEST_ASSERT(AssignScriptRewardSelectionOptionIds(
		    offer, common_option_id, &error));
		for (size_t index = 0; index < offer.options.size(); ++index) {
			TEST_ASSERT(offer.options[index].option_id == index + 1);
		}
		TEST_ASSERT(common_option_id == 0);
	}

	void RewardLoreConflictValidation()
	{
		EQ::ItemData unique_a{};
		unique_a.ID = 5001;
		unique_a.LoreGroup = -1;
		EQ::ItemData unique_b{};
		unique_b.ID = 5002;
		unique_b.LoreGroup = -1;
		EQ::ItemData group_a{};
		group_a.ID = 5003;
		group_a.LoreGroup = 77;
		EQ::ItemData group_b{};
		group_b.ID = 5004;
		group_b.LoreGroup = 77;
		EQ::ItemData stackable{};
		stackable.ID = 5005;
		stackable.LoreGroup = 0;

		const auto resolve_item = [&](uint32_t item_id)
		    -> const EQ::ItemData * {
			switch (item_id) {
			case 5001:
				return &unique_a;
			case 5002:
				return &unique_b;
			case 5003:
				return &group_a;
			case 5004:
				return &group_b;
			case 5005:
				return &stackable;
			default:
				return nullptr;
			}
		};
		const auto item_reward = [](uint32_t item_id) {
			return RewardSelectionReward{
				.type = RewardSelectionRewardType::Item,
				.data_id = item_id,
				.amount = 1
			};
		};

		uint32_t left_item_id = 0;
		uint32_t right_item_id = 0;
		std::vector<RewardSelectionReward> common_rewards;
		std::vector<RewardSelectionReward> selected_rewards = {
			item_reward(5001),
			item_reward(5002)
		};
		TEST_ASSERT(!FindRewardSelectionLoreConflict(
			common_rewards,
			selected_rewards,
			resolve_item,
			left_item_id,
			right_item_id
		));

		selected_rewards = {item_reward(5001), item_reward(5001)};
		TEST_ASSERT(FindRewardSelectionLoreConflict(
			common_rewards,
			selected_rewards,
			resolve_item,
			left_item_id,
			right_item_id
		));
		TEST_ASSERT(left_item_id == 5001);
		TEST_ASSERT(right_item_id == 5001);

		selected_rewards = {item_reward(5003), item_reward(5004)};
		TEST_ASSERT(FindRewardSelectionLoreConflict(
			common_rewards,
			selected_rewards,
			resolve_item,
			left_item_id,
			right_item_id
		));
		TEST_ASSERT(left_item_id == 5003);
		TEST_ASSERT(right_item_id == 5004);

		common_rewards = {item_reward(5003)};
		selected_rewards = {item_reward(5004)};
		TEST_ASSERT(FindRewardSelectionLoreConflict(
			common_rewards,
			selected_rewards,
			resolve_item,
			left_item_id,
			right_item_id
		));

		common_rewards = {item_reward(5005)};
		selected_rewards = {item_reward(5005)};
		TEST_ASSERT(!FindRewardSelectionLoreConflict(
			common_rewards,
			selected_rewards,
			resolve_item,
			left_item_id,
			right_item_id
		));

		selected_rewards = {
			RewardSelectionReward{
				.type = RewardSelectionRewardType::Copper,
				.amount = 1000
			},
			item_reward(5004)
		};
		TEST_ASSERT(HasRewardSelectionInventoryLoreConflict(
			selected_rewards,
			resolve_item,
			[](const EQ::ItemData *item) { return item->ID == 5004; }
		));
		TEST_ASSERT(!HasRewardSelectionInventoryLoreConflict(
			selected_rewards,
			resolve_item,
			[](const EQ::ItemData *) { return false; }
		));
	}

	void StructuredScriptRewardValidation()
	{
		std::string error;
		ScriptRewardSelectionOffer text_offer;
		TEST_ASSERT(ValidateScriptRewardSelectionDisplayText(
			text_offer,
			error
		));

		const std::string embedded_nul("bad\0text", 8);
		text_offer.title = embedded_nul;
		TEST_ASSERT(!ValidateScriptRewardSelectionDisplayText(
			text_offer,
			error
		));
		TEST_ASSERT(error == "title cannot contain embedded NUL bytes");

		text_offer.title.clear();
		text_offer.options.resize(1);
		text_offer.options[0].label = embedded_nul;
		TEST_ASSERT(!ValidateScriptRewardSelectionDisplayText(
			text_offer,
			error
		));
		TEST_ASSERT(
			error == "options[1].label cannot contain embedded NUL bytes"
		);

		text_offer.options[0].label.clear();
		text_offer.options[0].rewards.resize(1);
		text_offer.options[0].rewards[0].description = embedded_nul;
		TEST_ASSERT(!ValidateScriptRewardSelectionDisplayText(
			text_offer,
			error
		));
		TEST_ASSERT(
			error ==
			"options[1].rewards[1].description cannot contain embedded NUL bytes"
		);

		text_offer.options[0].rewards[0].description.clear();
		text_offer.common_rewards.resize(1);
		text_offer.common_rewards[0].description = embedded_nul;
		TEST_ASSERT(!ValidateScriptRewardSelectionDisplayText(
			text_offer,
			error
		));
		TEST_ASSERT(
			error ==
			"common_rewards[1].description cannot contain embedded NUL bytes"
		);

		TEST_ASSERT(ShouldRecoverCompletedSelectableReward(true, false, true, false, false));
		TEST_ASSERT(ShouldRecoverCompletedSelectableReward(false, true, false, true, false));
		TEST_ASSERT(ShouldRecoverCompletedSelectableReward(true, true, false, true, false));
		TEST_ASSERT(ShouldRecoverCompletedSelectableReward(false, true, true, true, false));
		TEST_ASSERT(ShouldRecoverCompletedSelectableReward(false, false, true, true, true));
		TEST_ASSERT(ShouldRecoverCompletedSelectableReward(false, false, false, true, true));
		TEST_ASSERT(!ShouldRecoverCompletedSelectableReward(false, true, true, false, false));
		TEST_ASSERT(!ShouldRecoverCompletedSelectableReward(true, false, false, true, false));
		TEST_ASSERT(!ShouldRecoverCompletedSelectableReward(false, false, true, true, false));
		TEST_ASSERT(!ShouldRecoverCompletedSelectableReward(false, false, true, false, true));

		ScriptRewardSelectionRewardConfig config;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));
		TEST_ASSERT(
		    error == "reward must contain exactly one reward-type field");

		config.item_id = 1001;
		config.experience = 1;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));
		TEST_ASSERT(
		    error == "reward must contain exactly one reward-type field");

		config = {};
		config.item_id = 0;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));
		TEST_ASSERT(
		    error == "reward value is zero or exceeds the supported range");

		config = {};
		config.item_id = 1001;
		config.quantity =
		    static_cast<uint64_t>(std::numeric_limits<int16_t>::max()) + 1;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));

		config = {};
		config.item_id = 1001;
		config.amount = 2;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));
		TEST_ASSERT(error == "item rewards use quantity, not amount");

		config = {};
		config.experience = 1;
		config.quantity = 1;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));
		TEST_ASSERT(
		    error == "experience rewards do not accept quantity or amount");

		config = {};
		config.experience_no_aa =
		    static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));

		config = {};
		config.aa_points =
		    static_cast<uint64_t>(std::numeric_limits<int>::max()) + 1;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));

		config = {};
		config.money =
		    static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) *
		        1000ULL +
		    1000ULL;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));

		config = {};
		config.alternate_currency_id = 19;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));
		TEST_ASSERT(error == "alternate currency rewards require amount");

		config.amount = 25;
		config.quantity = 1;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));
		TEST_ASSERT(
		    error == "alternate currency rewards use amount, not quantity");

		config = {};
		config.alternate_currency_id =
		    static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1;
		config.amount = 1;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));
		TEST_ASSERT(
		    error == "alternate_currency_id exceeds the supported range");

		config = {};
		config.title_set_id = 71;
		config.amount = 1;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));
		TEST_ASSERT(error == "title rewards do not accept quantity or amount");

		config = {};
		config.title_set_id =
		    static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1;
		TEST_ASSERT(!MakeScriptRewardSelectionReward(config, &error));
		TEST_ASSERT(error == "title_set_id exceeds the supported range");
	}

	void StableWireOptionIdentity()
	{
		std::vector<RewardSelectionSession> previous(2);
		previous[0].channel = RewardSelectionChannel::Claimable;
		previous[0].source = {RewardSelectionSource::Task, 100, 1000};
		previous[0].pending_reward_id = 10;
		previous[0].reward_set.reward_set_id = 100;
		previous[0].reward_set.options.resize(2);
		previous[0].reward_set.options[0].option_id = 10;
		previous[0].reward_set.options[0].wire_option_id = 1;
		previous[0].reward_set.options[1].option_id = 11;
		previous[0].reward_set.options[1].wire_option_id = 2;

		previous[1].channel = RewardSelectionChannel::Claimable;
		previous[1].source = {RewardSelectionSource::Task, 200, 2000};
		previous[1].pending_reward_id = 20;
		previous[1].reward_set.reward_set_id = 200;
		previous[1].reward_set.options.resize(1);
		previous[1].reward_set.options[0].option_id = 20;
		previous[1].reward_set.options[0].wire_option_id = 3;

		std::vector<RewardSelectionSession> refreshed{
			previous[1],
			previous[0]
		};
		std::reverse(
			refreshed[1].reward_set.options.begin(),
			refreshed[1].reward_set.options.end()
		);
		refreshed[1].reward_set.options.push_back({});
		refreshed[1].reward_set.options.back().option_id = 12;

		RewardSelectionSession added;
		added.channel = RewardSelectionChannel::Claimable;
		added.source = {RewardSelectionSource::Task, 300, 3000};
		added.pending_reward_id = 30;
		added.reward_set.reward_set_id = 300;
		added.reward_set.options.push_back({});
		added.reward_set.options.back().option_id = 30;
		refreshed.push_back(std::move(added));

		TEST_ASSERT(AssignStableRewardSelectionWireOptionIds(
			refreshed,
			previous
		));

		const auto wire_id = [](
			const std::vector<RewardSelectionSession> &sessions,
			uint32_t reward_set_id,
			uint32_t option_id
		) {
			for (const auto &session : sessions) {
				if (session.reward_set.reward_set_id != reward_set_id) {
					continue;
				}
				for (const auto &option : session.reward_set.options) {
					if (option.option_id == option_id) {
						return option.wire_option_id;
					}
				}
			}
			return uint32_t{0};
		};

		TEST_ASSERT(wire_id(refreshed, 100, 10) == 1);
		TEST_ASSERT(wire_id(refreshed, 100, 11) == 2);
		TEST_ASSERT(wire_id(refreshed, 200, 20) == 3);
		TEST_ASSERT(wire_id(refreshed, 100, 12) == 4);
		TEST_ASSERT(wire_id(refreshed, 300, 30) == 5);

		std::unordered_set<uint32_t> assigned_ids;
		for (const auto &session : refreshed) {
			for (const auto &option : session.reward_set.options) {
				TEST_ASSERT(option.wire_option_id);
				TEST_ASSERT(
					assigned_ids.insert(option.wire_option_id).second
				);
			}
		}

		std::vector<RewardSelectionSession> after_removal{
			refreshed[0],
			refreshed[2]
		};
		RewardSelectionSession later_added;
		later_added.channel = RewardSelectionChannel::Claimable;
		later_added.source = {RewardSelectionSource::Task, 400, 4000};
		later_added.pending_reward_id = 40;
		later_added.reward_set.reward_set_id = 400;
		later_added.reward_set.options.push_back({});
		later_added.reward_set.options.back().option_id = 40;
		after_removal.push_back(std::move(later_added));

		TEST_ASSERT(AssignStableRewardSelectionWireOptionIds(
			after_removal,
			refreshed
		));
		TEST_ASSERT(wire_id(after_removal, 200, 20) == 3);
		TEST_ASSERT(wire_id(after_removal, 300, 30) == 5);
		TEST_ASSERT(wire_id(after_removal, 400, 40) == 1);
	}

	void CommonOptionGroupingValidation()
	{
		std::vector<RewardSelectionOption> options(3);
		options[1].common_to_all = true;
		TEST_ASSERT(HasSupportedRewardSelectionCommonGrouping(options));

		options[2].common_to_all = true;
		TEST_ASSERT(!HasSupportedRewardSelectionCommonGrouping(options));
	}

	void TransientBatchFailureClassification()
	{
		TEST_ASSERT(
			IsRewardSelectionRewardIdempotent(
				RewardSelectionRewardType::Title
			)
		);
		TEST_ASSERT(
			!IsRewardSelectionRewardIdempotent(
				RewardSelectionRewardType::Item
			)
		);

		TEST_ASSERT(HasRewardSelectionCursorCapacity(9, 1, 10));
		TEST_ASSERT(!HasRewardSelectionCursorCapacity(9, 2, 10));
		TEST_ASSERT(!HasRewardSelectionCursorCapacity(11, 1, 10));

		const RewardSelectionReward item_reward{
			.type = RewardSelectionRewardType::Item,
			.data_id = 1001,
			.amount = 1
		};
		std::vector<RewardSelectionReward> common_items(4, item_reward);
		std::vector<RewardSelectionReward> selected_items(6, item_reward);
		TEST_ASSERT(HasRewardSelectionItemBatchCapacity(
			common_items,
			selected_items,
			10
		));
		selected_items.emplace_back(item_reward);
		TEST_ASSERT(!HasRewardSelectionItemBatchCapacity(
			common_items,
			selected_items,
			10
		));

		TEST_ASSERT(
			ResolveTransientRewardBatchFailure(
				false,
				false,
				RewardSelectionDeliveryResult::RetryableFailure
			) == RewardSelectionDeliveryResult::RetryableFailure
		);
		TEST_ASSERT(
			ResolveTransientRewardBatchFailure(
				true,
				false,
				RewardSelectionDeliveryResult::RetryableFailure
			) == RewardSelectionDeliveryResult::Ambiguous
		);
		TEST_ASSERT(
			ResolveTransientRewardBatchFailure(
				false,
				true,
				RewardSelectionDeliveryResult::RetryableFailure
			) ==
				RewardSelectionDeliveryResult::RetryableFailureSameOption
		);
		TEST_ASSERT(
			ResolveTransientRewardBatchFailure(
				false,
				false,
				RewardSelectionDeliveryResult::Ambiguous
			) == RewardSelectionDeliveryResult::Ambiguous
		);
		TEST_ASSERT(
			ResolveTransientRewardBatchFailure(
				true,
				true,
				RewardSelectionDeliveryResult::Ambiguous
			) == RewardSelectionDeliveryResult::Ambiguous
		);

		TEST_ASSERT(!ShouldRemoveRewardSelectionSessionAfterClaim(
			RewardSelectionDeliveryResult::RetryableFailure,
			false
		));
		TEST_ASSERT(!ShouldRemoveRewardSelectionSessionAfterClaim(
			RewardSelectionDeliveryResult::RetryableFailureSameOption,
			false
		));
		TEST_ASSERT(ShouldRemoveRewardSelectionSessionAfterClaim(
			RewardSelectionDeliveryResult::Ambiguous,
			false
		));
		TEST_ASSERT(ShouldRemoveRewardSelectionSessionAfterClaim(
			RewardSelectionDeliveryResult::Delivered,
			true
		));

		EQ::ItemData stackable{};
		stackable.ID = 1001;
		stackable.Stackable = 1;
		stackable.StackSize = 20;
		const auto resolve_item = [&stackable](uint32_t item_id)
			-> const EQ::ItemData * {
			return item_id == stackable.ID ? &stackable : nullptr;
		};
		const auto resolve_alternate_currency = [](uint32_t currency_id)
			-> std::optional<uint64_t> {
			return currency_id == 7
				? std::optional<uint64_t>(
					static_cast<uint64_t>(std::numeric_limits<int>::max()) - 10
				)
				: std::nullopt;
		};
		const auto resolve_title = [](uint32_t title_set) {
			return title_set == 42;
		};
		const auto reward = [](
			RewardSelectionRewardType type,
			uint32_t data_id,
			uint64_t amount
		) {
			return RewardSelectionReward{
				.type = type,
				.data_id = data_id,
				.amount = amount
			};
		};
		const auto can_grant = [&resolve_item, &resolve_alternate_currency, &resolve_title](
			const std::vector<RewardSelectionReward> &rewards,
			const RewardSelectionBatchState &state,
			const RewardSelectionDeliveryPolicy &policy
		) {
			return CanGrantRewardSelectionBatch(
				rewards,
				policy,
				state,
				resolve_item,
				resolve_alternate_currency,
				resolve_title
			);
		};
		const RewardSelectionBatchState empty_state;
		const RewardSelectionDeliveryPolicy default_policy;

		TEST_ASSERT(can_grant(
			{
				reward(RewardSelectionRewardType::Item, 1001, 20),
				reward(RewardSelectionRewardType::Title, 42, 1)
			},
			empty_state,
			default_policy
		));

		auto no_experience_state = empty_state;
		no_experience_state.experience_enabled = false;
		TEST_ASSERT(!can_grant(
			{
				reward(RewardSelectionRewardType::Copper, 0, 10),
				reward(RewardSelectionRewardType::Experience, 0, 100)
			},
			no_experience_state,
			default_policy
		));
		const RewardSelectionDeliveryPolicy task_policy{
			.experience_source = ExpSource::Task,
			.require_experience_enabled = false,
			.require_quest_experience_rule = false
		};
		TEST_ASSERT(can_grant(
			{reward(
				RewardSelectionRewardType::Experience,
				static_cast<uint32_t>(RewardSelectionExperienceMode::NormalOnly),
				100
			)},
			no_experience_state,
			task_policy
		));

		auto aa_state = empty_state;
		aa_state.aa_points = std::numeric_limits<int>::max() - 10;
		TEST_ASSERT(can_grant(
			{
				reward(RewardSelectionRewardType::AlternateAdvancement, 0, 5),
				reward(RewardSelectionRewardType::AlternateAdvancement, 0, 5)
			},
			aa_state,
			default_policy
		));
		TEST_ASSERT(!can_grant(
			{
				reward(RewardSelectionRewardType::AlternateAdvancement, 0, 5),
				reward(RewardSelectionRewardType::AlternateAdvancement, 0, 6)
			},
			aa_state,
			default_policy
		));
		TEST_ASSERT(!can_grant(
			{
				reward(
					RewardSelectionRewardType::AlternateAdvancement,
					0,
					std::numeric_limits<int>::max()
				),
				reward(RewardSelectionRewardType::AlternateAdvancement, 0, 1)
			},
			empty_state,
			default_policy
		));

		auto coin_state = empty_state;
		coin_state.platinum = std::numeric_limits<int32_t>::max() - 1;
		TEST_ASSERT(!can_grant(
			{
				reward(RewardSelectionRewardType::Copper, 0, 1000),
				reward(RewardSelectionRewardType::Copper, 0, 1000)
			},
			coin_state,
			default_policy
		));

		TEST_ASSERT(can_grant(
			{
				reward(RewardSelectionRewardType::AlternateCurrency, 7, 5),
				reward(RewardSelectionRewardType::AlternateCurrency, 7, 5)
			},
			empty_state,
			default_policy
		));
		TEST_ASSERT(!can_grant(
			{
				reward(RewardSelectionRewardType::AlternateCurrency, 7, 5),
				reward(RewardSelectionRewardType::AlternateCurrency, 7, 6)
			},
			empty_state,
			default_policy
		));
		TEST_ASSERT(!can_grant(
			{reward(RewardSelectionRewardType::AlternateCurrency, 8, 1)},
			empty_state,
			default_policy
		));
		TEST_ASSERT(!can_grant(
			{reward(RewardSelectionRewardType::Item, 9999, 1)},
			empty_state,
			default_policy
		));
		TEST_ASSERT(!can_grant(
			{reward(RewardSelectionRewardType::Item, 1001, 21)},
			empty_state,
			default_policy
		));
		TEST_ASSERT(!can_grant(
			{reward(RewardSelectionRewardType::Title, 99, 1)},
			empty_state,
			default_policy
		));
	}
};
