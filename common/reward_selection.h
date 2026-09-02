#pragma once

#include "serialize_buffer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace EQ::RewardSelection
{

// RoF2 Select Reward entry types.
enum class WireType : uint32_t {
	Text                         = 0,
	Money                        = 1,
	Item                         = 2,
	Experience                   = 3,
	AlternateAdvancementAbility = 4,
	AlternateAdvancementPoints  = 5,
	GenericPoints                = 11
};

inline constexpr uint32_t ActionList        = 0;
inline constexpr uint32_t ActionInspectItem = 1;
inline constexpr uint32_t ActionClaim       = 3;
inline constexpr uint32_t ActionView        = 5;
inline constexpr uint32_t ActionBulk        = 7;

struct DisplayItem {
	uint32_t    field0 = 0;
	uint32_t    field1 = 0;
	uint8_t     field2 = 0;
	uint32_t    field3 = 0;
	std::string text;
};

struct DisplayEntry {
	WireType                 wire_type = WireType::Text;
	std::array<uint32_t, 2>  fields{};
	std::string              description;
	std::array<uint32_t, 4>  values{};
	uint8_t                  flag = 0;
	std::vector<DisplayItem> items;
};

struct DisplaySubset {
	uint32_t                  subset_id = 0;
	bool                      common_to_all = false;
	uint8_t                   flags = 0;
	std::string               option_label;
	std::vector<DisplayEntry> entries;
};

struct DisplaySet {
	uint32_t                   pending_reward_id = 0;
	uint32_t                   reward_set_id = 0;
	std::string                title;
	// IEEE-754 1.0f. The client multiplies numeric reward displays by this.
	uint32_t                   amount_multiplier_bits = 0x3f800000;
	std::vector<DisplaySubset> subsets;
};

// RoF2 action 0. A null display set emits the client-supported "no reward"
// response (action followed by has_reward=0).
SerializeBuffer SerializeDisplay(const DisplaySet *reward_set);

// RoF2 action 7 replaces the reward manager with a tab for each display set.
// An empty collection clears an already-populated manager.
SerializeBuffer SerializeDisplays(const std::vector<DisplaySet> &reward_sets);

SerializeBuffer SerializeDisplayClear();

// RoF2 action 3. The three request identity fields are echoed and the final
// byte tells the client whether to remove the pending reward.
SerializeBuffer SerializeClaimReply(
	uint32_t pending_reward_id,
	uint32_t reward_set_id,
	uint32_t selected_subset_id,
	bool success
);

} // namespace EQ::RewardSelection
