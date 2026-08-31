#pragma once

#include "common/reward_selection.h"
#include "cppunit/cpptest.h"

#include <cstring>
#include <stdexcept>
#include <string>

class RewardSelectionTest : public Test::Suite
{
public:
	RewardSelectionTest()
	{
		TEST_ADD(RewardSelectionTest::DisplayLayout);
		TEST_ADD(RewardSelectionTest::ClaimReplyLayout);
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
};
