#include "reward_selection.h"

#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace EQ::RewardSelection
{
namespace
{

uint32_t Count32(size_t count)
{
	if (count > std::numeric_limits<uint32_t>::max()) {
		throw std::length_error("reward collection exceeds uint32");
	}

	return static_cast<uint32_t>(count);
}

void SerializeDisplaySet(SerializeBuffer &out, const DisplaySet &reward_set)
{
	if (
		!reward_set.pending_reward_id ||
		!reward_set.reward_set_id ||
		reward_set.subsets.size() >
			static_cast<size_t>(std::numeric_limits<int32_t>::max())
	) {
		throw std::invalid_argument("invalid RoF2 reward set");
	}

	out.WriteUInt32(reward_set.pending_reward_id);
	out.WriteUInt32(reward_set.reward_set_id);
	out.WriteString(reward_set.title);
	out.WriteUInt32(reward_set.amount_multiplier_bits);

	out.WriteUInt32(reward_set.reward_set_id);
	out.WriteInt32(static_cast<int32_t>(reward_set.subsets.size()));

	std::unordered_set<uint32_t> subset_ids;
	for (const auto &subset : reward_set.subsets) {
		if (
			!subset.subset_id ||
			!subset_ids.insert(subset.subset_id).second ||
			subset.entries.size() > std::numeric_limits<uint32_t>::max()
		) {
			throw std::invalid_argument("invalid RoF2 reward subset");
		}

		out.WriteUInt32(subset.subset_id);
		out.WriteUInt32(Count32(subset.entries.size()));
		out.WriteUInt8(subset.common_to_all ? 1 : 0);
		out.WriteUInt8(subset.flags);
		out.WriteString(subset.option_label);

		for (const auto &entry : subset.entries) {
			out.WriteUInt32(static_cast<uint32_t>(entry.wire_type));
			for (const auto field : entry.fields) {
				out.WriteUInt32(field);
			}
			out.WriteString(entry.description);

			switch (entry.wire_type) {
			case WireType::Money:
				for (const auto value : entry.values) {
					out.WriteUInt32(value);
				}
				break;
			case WireType::Item:
				out.WriteUInt32(Count32(entry.items.size()));
				for (const auto &item : entry.items) {
					out.WriteUInt32(item.field0);
					out.WriteUInt32(item.field1);
					out.WriteUInt8(item.field2);
					out.WriteUInt32(item.field3);
					out.WriteString(item.text);
				}
				break;
			case WireType::AlternateAdvancementAbility:
				out.WriteUInt32(entry.values[0]);
				out.WriteUInt32(entry.values[1]);
				break;
			case WireType::AlternateAdvancementPoints:
				out.WriteUInt32(entry.values[0]);
				out.WriteUInt8(entry.flag);
				break;
			case WireType::GenericPoints:
				for (const auto value : entry.values) {
					out.WriteUInt32(value);
				}
				break;
			default:
				break;
			}
		}
	}
}

} // namespace

SerializeBuffer SerializeDisplay(const DisplaySet *reward_set)
{
	SerializeBuffer out;
	out.WriteUInt32(ActionList);
	out.WriteUInt8(reward_set ? 1 : 0);
	if (reward_set) {
		SerializeDisplaySet(out, *reward_set);
	}

	return out;
}

SerializeBuffer SerializeDisplays(const std::vector<DisplaySet> &reward_sets)
{
	if (reward_sets.size() >
		static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
		throw std::invalid_argument("too many RoF2 reward display sets");
	}

	SerializeBuffer out;
	out.WriteUInt32(ActionBulk);
	out.WriteInt32(static_cast<int32_t>(reward_sets.size()));
	out.WriteUInt8(0);
	for (const auto &reward_set : reward_sets) {
		SerializeDisplaySet(out, reward_set);
	}
	return out;
}

SerializeBuffer SerializeDisplayClear()
{
	return SerializeDisplays({});
}

SerializeBuffer SerializeClaimReply(
	uint32_t pending_reward_id,
	uint32_t reward_set_id,
	uint32_t selected_subset_id,
	bool success
)
{
	SerializeBuffer out;
	out.WriteUInt32(ActionClaim);
	out.WriteUInt32(pending_reward_id);
	out.WriteUInt32(reward_set_id);
	out.WriteUInt32(selected_subset_id);
	out.WriteUInt8(success ? 1 : 0);
	out.WriteUInt8(0);
	out.WriteUInt8(0);
	out.WriteUInt8(0);
	return out;
}

} // namespace EQ::RewardSelection
