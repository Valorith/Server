#pragma once

#include "../common/eq_constants.h"
#include "../common/item_data.h"
#include "../common/timer.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class Client;
class EQApplicationPacket;

// RoF2 shares Select Reward across providers, with separate claimable and
// read-only preview channels.
enum class RewardSelectionSource : uint8_t {
	Unknown     = 0,
	Achievement = 1,
	Task        = 2,
	General     = 3
};

enum class RewardSelectionChannel : uint8_t {
	Claimable,
	Preview
};

// Provider-specific view and pending actions used by the shared reward manager.
inline constexpr uint32_t RewardSelectionActionTaskView        = 4;
inline constexpr uint32_t RewardSelectionActionAchievementView = 5;
inline constexpr uint32_t RewardSelectionActionPending         = 6;

enum class RewardSelectionRewardType : uint8_t {
	Item                 = 0,
	Experience           = 1,
	AlternateAdvancement = 2,
	Copper               = 3,
	AlternateCurrency    = 4,
	Title                = 5
};

inline constexpr bool IsRewardSelectionRewardIdempotent(
	RewardSelectionRewardType type
)
{
	return type == RewardSelectionRewardType::Title;
}

inline constexpr bool HasRewardSelectionCursorCapacity(
	size_t current_cursor_items,
	size_t required_item_rewards,
	size_t cursor_capacity
)
{
	return
		current_cursor_items <= cursor_capacity &&
		required_item_rewards <= cursor_capacity - current_cursor_items;
}

// Experience rewards use data_id to select normal-only or normal/AA allocation.
enum class RewardSelectionExperienceMode : uint32_t {
	Default    = 0,
	NormalOnly = 1
};

inline bool IsValidRewardSelectionRewardDefinition(
	RewardSelectionRewardType type,
	uint32_t data_id,
	uint64_t amount
)
{
	if (!amount) {
		return false;
	}

	switch (type) {
	case RewardSelectionRewardType::Item:
		return
			data_id &&
			amount <= static_cast<uint64_t>(
				std::numeric_limits<int16_t>::max()
			);
	case RewardSelectionRewardType::Experience:
		return
			data_id <= static_cast<uint32_t>(
				RewardSelectionExperienceMode::NormalOnly
			) &&
			amount <= static_cast<uint64_t>(
				std::numeric_limits<uint32_t>::max()
			);
	case RewardSelectionRewardType::AlternateAdvancement:
		return
			amount <= static_cast<uint64_t>(
				std::numeric_limits<int>::max()
			);
	case RewardSelectionRewardType::Copper:
		return
			amount <=
				static_cast<uint64_t>(
					std::numeric_limits<int32_t>::max()
				) * 1000ULL + 999ULL;
	case RewardSelectionRewardType::AlternateCurrency:
		return
			data_id &&
			amount <= static_cast<uint64_t>(
				std::numeric_limits<int>::max()
			);
	case RewardSelectionRewardType::Title:
		return
			data_id &&
			data_id <= static_cast<uint32_t>(
				std::numeric_limits<int>::max()
			);
	}

	return false;
}

inline bool IsValidRewardSelectionItemAmount(
	const EQ::ItemData *item,
	uint64_t amount
)
{
	if (
		!item ||
		!amount ||
		amount > static_cast<uint64_t>(std::numeric_limits<int16_t>::max())
	) {
		return false;
	}
	if (amount == 1) {
		return true;
	}
	if (item->Stackable) {
		return
			item->StackSize > 0 &&
			amount <= static_cast<uint64_t>(item->StackSize);
	}
	return
		item->MaxCharges > 0 &&
		amount <= static_cast<uint64_t>(item->MaxCharges);
}

struct RewardSelectionReward {
	// RoF2 echoes only the low 32 bits; loaders reject wider entry IDs.
	uint64_t                  entry_id = 0;
	RewardSelectionRewardType type = RewardSelectionRewardType::Item;
	uint32_t                  data_id = 0;
	uint64_t                  amount = 0;
	std::string               description;
};

inline bool HasRewardSelectionItemBatchCapacity(
	const std::vector<RewardSelectionReward> &common_rewards,
	const std::vector<RewardSelectionReward> &selected_rewards,
	size_t cursor_capacity
)
{
	const auto count_items = [](const auto &rewards) {
		return static_cast<size_t>(std::count_if(
			rewards.begin(),
			rewards.end(),
			[](const RewardSelectionReward &reward) {
				return reward.type == RewardSelectionRewardType::Item;
			}
		));
	};
	const auto common_item_count = count_items(common_rewards);
	const auto selected_item_count = count_items(selected_rewards);
	return
		common_item_count <= cursor_capacity &&
		selected_item_count <= cursor_capacity - common_item_count;
}

// Parser-neutral input shared by the Perl hashref and Lua table bindings.
// Exactly one reward discriminator must be present.
struct ScriptRewardSelectionRewardConfig
{
	std::optional<uint64_t> item_id;
	std::optional<uint64_t> experience;
	std::optional<uint64_t> experience_no_aa;
	std::optional<uint64_t> aa_points;
	std::optional<uint64_t> money;
	std::optional<uint64_t> alternate_currency_id;
	std::optional<uint64_t> title_set_id;
	std::optional<uint64_t> quantity;
	std::optional<uint64_t> amount;
	std::string description;
};

inline std::optional<RewardSelectionReward> MakeScriptRewardSelectionReward(
    const ScriptRewardSelectionRewardConfig &config,
    std::string *error = nullptr)
{
	const auto fail = [error](const std::string &message) {
		if (error) {
			*error = message;
		}
		return std::optional<RewardSelectionReward>{};
	};
	if (error) {
		error->clear();
	}

	const size_t discriminator_count =
	    static_cast<size_t>(config.item_id.has_value()) +
	    static_cast<size_t>(config.experience.has_value()) +
	    static_cast<size_t>(config.experience_no_aa.has_value()) +
	    static_cast<size_t>(config.aa_points.has_value()) +
	    static_cast<size_t>(config.money.has_value()) +
	    static_cast<size_t>(config.alternate_currency_id.has_value()) +
	    static_cast<size_t>(config.title_set_id.has_value());
	if (discriminator_count != 1) {
		return fail("reward must contain exactly one reward-type field");
	}

	RewardSelectionReward reward;
	reward.description = config.description;
	if (config.item_id) {
		if (config.amount) {
			return fail("item rewards use quantity, not amount");
		}
		if (*config.item_id > std::numeric_limits<uint32_t>::max()) {
			return fail("item_id exceeds the supported range");
		}
		reward.type = RewardSelectionRewardType::Item;
		reward.data_id = static_cast<uint32_t>(*config.item_id);
		reward.amount = config.quantity.value_or(1);
	} else if (config.experience) {
		if (config.quantity || config.amount) {
			return fail("experience rewards do not accept quantity or amount");
		}
		reward.type = RewardSelectionRewardType::Experience;
		reward.data_id =
		    static_cast<uint32_t>(RewardSelectionExperienceMode::Default);
		reward.amount = *config.experience;
	} else if (config.experience_no_aa) {
		if (config.quantity || config.amount) {
			return fail(
			    "experience_no_aa rewards do not accept quantity or amount");
		}
		reward.type = RewardSelectionRewardType::Experience;
		reward.data_id =
		    static_cast<uint32_t>(RewardSelectionExperienceMode::NormalOnly);
		reward.amount = *config.experience_no_aa;
	} else if (config.aa_points) {
		if (config.quantity || config.amount) {
			return fail("aa_points rewards do not accept quantity or amount");
		}
		reward.type = RewardSelectionRewardType::AlternateAdvancement;
		reward.amount = *config.aa_points;
	} else if (config.money) {
		if (config.quantity || config.amount) {
			return fail("money rewards do not accept quantity or amount");
		}
		reward.type = RewardSelectionRewardType::Copper;
		reward.amount = *config.money;
	} else if (config.alternate_currency_id) {
		if (config.quantity) {
			return fail("alternate currency rewards use amount, not quantity");
		}
		if (!config.amount) {
			return fail("alternate currency rewards require amount");
		}
		if (*config.alternate_currency_id >
		    std::numeric_limits<uint32_t>::max()) {
			return fail("alternate_currency_id exceeds the supported range");
		}
		reward.type = RewardSelectionRewardType::AlternateCurrency;
		reward.data_id = static_cast<uint32_t>(*config.alternate_currency_id);
		reward.amount = *config.amount;
	} else {
		if (config.quantity || config.amount) {
			return fail("title rewards do not accept quantity or amount");
		}
		if (*config.title_set_id > std::numeric_limits<uint32_t>::max()) {
			return fail("title_set_id exceeds the supported range");
		}
		reward.type = RewardSelectionRewardType::Title;
		reward.data_id = static_cast<uint32_t>(*config.title_set_id);
		reward.amount = 1;
	}

	if (!IsValidRewardSelectionRewardDefinition(
	        reward.type, reward.data_id, reward.amount)) {
		return fail("reward value is zero or exceeds the supported range");
	}
	return reward;
}

struct RewardSelectionOption {
	uint32_t                           option_id = 0;
	// RoF2 indexes option details across the entire reward manager. This ID is
	// assigned per displayed collection and mapped back to option_id on input.
	uint32_t                           wire_option_id = 0;
	uint32_t                           sequence = 0;
	std::string                        label;
	bool                               common_to_all = false;
	uint8_t                            flags = 0;
	std::vector<RewardSelectionReward> rewards;
};

struct RewardSelectionSet {
	uint32_t                           reward_set_id = 0;
	std::string                        title;
	std::vector<RewardSelectionOption> options;
};

struct ScriptRewardSelectionOffer
{
	std::string title;
	std::vector<RewardSelectionOption> options;
	std::vector<RewardSelectionReward> common_rewards;
};

inline bool ValidateScriptRewardSelectionDisplayText(
	const ScriptRewardSelectionOffer &offer,
	std::string &error
)
{
	error.clear();
	const auto validate = [&error](
		const std::string &value,
		const std::string &path
	) {
		if (value.find('\0') == std::string::npos) {
			return true;
		}
		error = path + " cannot contain embedded NUL bytes";
		return false;
	};

	if (!validate(offer.title, "title")) {
		return false;
	}
	for (size_t option_index = 0; option_index < offer.options.size();
	     ++option_index) {
		const auto &option = offer.options[option_index];
		const auto option_path =
			"options[" + std::to_string(option_index + 1) + "]";
		if (!validate(option.label, option_path + ".label")) {
			return false;
		}
		for (size_t reward_index = 0;
		     reward_index < option.rewards.size();
		     ++reward_index) {
			if (!validate(
				option.rewards[reward_index].description,
				option_path + ".rewards[" +
					std::to_string(reward_index + 1) +
					"].description"
			)) {
				return false;
			}
		}
	}
	for (size_t reward_index = 0;
	     reward_index < offer.common_rewards.size();
	     ++reward_index) {
		if (!validate(
			offer.common_rewards[reward_index].description,
			"common_rewards[" + std::to_string(reward_index + 1) +
				"].description"
		)) {
			return false;
		}
	}
	return true;
}

inline bool ShouldRecoverCompletedSelectableReward(
	bool live_selectable_method,
	bool was_rewarded,
	bool active_task_complete,
	bool has_durable_occurrence,
	bool has_durable_selection
)
{
	return
		(live_selectable_method && active_task_complete) ||
		(
			has_durable_occurrence &&
			(was_rewarded || has_durable_selection)
		);
}

template <typename ItemResolver>
bool FindRewardSelectionLoreConflict(
	const std::vector<RewardSelectionReward> &common_rewards,
	const std::vector<RewardSelectionReward> &selected_rewards,
	ItemResolver resolve_item,
	uint32_t &left_item_id,
	uint32_t &right_item_id
)
{
	left_item_id = 0;
	right_item_id = 0;
	std::vector<std::pair<uint32_t, const EQ::ItemData *>> items;
	items.reserve(common_rewards.size() + selected_rewards.size());

	const auto append_items = [&items, &resolve_item](
		const std::vector<RewardSelectionReward> &rewards
	) {
		for (const auto &reward : rewards) {
			if (reward.type != RewardSelectionRewardType::Item) {
				continue;
			}
			const auto item = resolve_item(reward.data_id);
			if (item) {
				items.emplace_back(reward.data_id, item);
			}
		}
	};
	append_items(common_rewards);
	append_items(selected_rewards);

	for (size_t left = 0; left < items.size(); ++left) {
		for (size_t right = left + 1; right < items.size(); ++right) {
			if (EQ::ItemData::CheckLoreConflict(
				items[left].second,
				items[right].second
			)) {
				left_item_id = items[left].first;
				right_item_id = items[right].first;
				return true;
			}
		}
	}
	return false;
}

template <typename ItemResolver, typename LoreConflictChecker>
bool HasRewardSelectionInventoryLoreConflict(
	const std::vector<RewardSelectionReward> &rewards,
	ItemResolver resolve_item,
	LoreConflictChecker has_lore_conflict
)
{
	for (const auto &reward : rewards) {
		if (reward.type != RewardSelectionRewardType::Item) {
			continue;
		}
		const auto item = resolve_item(reward.data_id);
		if (item && has_lore_conflict(item)) {
			return true;
		}
	}
	return false;
}

inline std::optional<RewardSelectionOption>
MakeScriptItemRewardSelectionOption(
	uint64_t item_id,
	std::string *error = nullptr
)
{
	if (error) {
		error->clear();
	}

	ScriptRewardSelectionRewardConfig config;
	config.item_id = item_id;

	std::string reward_error;
	auto reward = MakeScriptRewardSelectionReward(config, &reward_error);
	if (!reward) {
		if (error) {
			*error = reward_error;
		}
		return std::nullopt;
	}

	RewardSelectionOption option;
	option.rewards.emplace_back(std::move(*reward));
	return option;
}

inline std::optional<ScriptRewardSelectionOffer>
MakeScriptItemRewardSelectionOffer(
	const std::vector<uint64_t> &item_ids,
	std::string *error = nullptr
)
{
	const auto fail = [error](const std::string &message) {
		if (error) {
			*error = message;
		}
		return std::optional<ScriptRewardSelectionOffer>{};
	};
	if (error) {
		error->clear();
	}
	if (item_ids.empty()) {
		return fail("item_ids must contain at least one item ID");
	}

	ScriptRewardSelectionOffer offer;
	offer.options.reserve(item_ids.size());
	for (size_t index = 0; index < item_ids.size(); ++index) {
		std::string option_error;
		auto option = MakeScriptItemRewardSelectionOption(
			item_ids[index],
			&option_error
		);
		if (!option) {
			return fail(
				"item_ids[" + std::to_string(index + 1) + "]: " +
				option_error
			);
		}
		offer.options.emplace_back(std::move(*option));
	}
	return offer;
}

inline bool AssignScriptRewardSelectionOptionIds(
	ScriptRewardSelectionOffer &offer,
	uint32_t &common_option_id,
	std::string *error = nullptr
)
{
	const auto fail = [error](const std::string &message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (error) {
		error->clear();
	}
	if (offer.options.empty()) {
		return fail("options must contain at least one choice");
	}
	if (offer.options.size() > std::numeric_limits<uint32_t>::max()) {
		return fail("options contains too many choices");
	}

	for (size_t index = 0; index < offer.options.size(); ++index) {
		offer.options[index].option_id = static_cast<uint32_t>(index + 1);
	}
	common_option_id = 0;
	if (!offer.common_rewards.empty()) {
		if (
			offer.options.size() >=
			std::numeric_limits<uint32_t>::max()
		) {
			return fail("no internal option ID is available for common_rewards");
		}
		common_option_id = static_cast<uint32_t>(offer.options.size() + 1);
	}
	return true;
}

struct RewardSelectionSourceKey {
	RewardSelectionSource source = RewardSelectionSource::Unknown;
	uint64_t              source_id = 0;
	// Distinguishes repeatable occurrences; scripted General offers encode
	// their originating NPC identity here.
	uint64_t              source_instance_id = 0;
};

struct RewardSelectionSession {
	RewardSelectionSourceKey source;
	RewardSelectionChannel   channel = RewardSelectionChannel::Preview;
	uint32_t                 pending_reward_id = 0;
	RewardSelectionSet       reward_set;
	bool                     requires_script_authorization = false;
};

inline bool SameRewardSelectionSession(
	const RewardSelectionSession &left,
	const RewardSelectionSession &right
)
{
	return
		left.channel == right.channel &&
		left.source.source == right.source.source &&
		left.source.source_id == right.source.source_id &&
		left.source.source_instance_id == right.source.source_instance_id &&
		left.pending_reward_id == right.pending_reward_id &&
		left.reward_set.reward_set_id == right.reward_set.reward_set_id;
}

inline bool HasSupportedRewardSelectionCommonGrouping(
	const std::vector<RewardSelectionOption> &options
)
{
	return
		std::count_if(
			options.begin(),
			options.end(),
			[](const RewardSelectionOption &option) {
				return option.common_to_all;
			}
		) <= 1;
}

inline bool AssignStableRewardSelectionWireOptionIds(
	std::vector<RewardSelectionSession> &sessions,
	const std::vector<RewardSelectionSession> &previous_sessions
)
{
	std::unordered_set<uint32_t> used_ids;
	for (auto &session : sessions) {
		for (auto &option : session.reward_set.options) {
			option.wire_option_id = 0;
		}

		const auto previous = std::find_if(
			previous_sessions.begin(),
			previous_sessions.end(),
			[&session](const RewardSelectionSession &candidate) {
				return SameRewardSelectionSession(candidate, session);
			}
		);
		if (previous == previous_sessions.end()) {
			continue;
		}

		for (auto &option : session.reward_set.options) {
			const auto previous_option = std::find_if(
				previous->reward_set.options.begin(),
				previous->reward_set.options.end(),
				[&option](const RewardSelectionOption &candidate) {
					return candidate.option_id == option.option_id;
				}
			);
			if (
				previous_option != previous->reward_set.options.end() &&
				previous_option->wire_option_id &&
				used_ids.insert(previous_option->wire_option_id).second
			) {
				option.wire_option_id = previous_option->wire_option_id;
			}
		}
	}

	uint64_t next_id = 1;
	for (auto &session : sessions) {
		for (auto &option : session.reward_set.options) {
			if (option.wire_option_id) {
				continue;
			}
			while (
				next_id <= std::numeric_limits<uint32_t>::max() &&
				used_ids.contains(static_cast<uint32_t>(next_id))
			) {
				++next_id;
			}
			if (next_id > std::numeric_limits<uint32_t>::max()) {
				return false;
			}
			option.wire_option_id = static_cast<uint32_t>(next_id);
			used_ids.insert(option.wire_option_id);
			++next_id;
		}
	}
	return true;
}

enum class RewardSelectionDeliveryResult : uint8_t {
	Delivered,
	RetryableFailure,
	RetryableFailureSameOption,
	Ambiguous
};

inline bool ShouldAuthorizeScriptRewardSelection(
	bool authorization_required,
	bool handler_found,
	int handler_result
)
{
	return handler_found ? handler_result != 0 : !authorization_required;
}

inline bool ShouldRemoveRewardSelectionSessionAfterClaim(
	RewardSelectionDeliveryResult result,
	bool delivered
)
{
	return
		delivered || result == RewardSelectionDeliveryResult::Ambiguous;
}

inline RewardSelectionDeliveryResult ResolveTransientRewardBatchFailure(
	bool delivered_non_idempotent,
	bool delivered_idempotent,
	RewardSelectionDeliveryResult failure
)
{
	if (failure == RewardSelectionDeliveryResult::Delivered) {
		return RewardSelectionDeliveryResult::Delivered;
	}

	if (
		delivered_non_idempotent ||
		failure == RewardSelectionDeliveryResult::Ambiguous
	) {
		return RewardSelectionDeliveryResult::Ambiguous;
	}
	return delivered_idempotent
		? RewardSelectionDeliveryResult::RetryableFailureSameOption
		: RewardSelectionDeliveryResult::RetryableFailure;
}

struct RewardSelectionDeliveryPolicy {
	ExpSource experience_source = ExpSource::Quest;
	bool      require_experience_enabled = true;
	bool      require_quest_experience_rule = true;
};

struct RewardSelectionBatchState {
	bool     experience_enabled = true;
	int64_t  aa_points = 0;
	int64_t  spent_aa_points = 0;
	uint64_t platinum = 0;
	uint64_t gold = 0;
	uint64_t silver = 0;
	uint64_t copper = 0;
};

template <typename ItemResolver, typename AlternateCurrencyResolver, typename TitleResolver>
inline bool CanGrantRewardSelectionBatch(
	const std::vector<RewardSelectionReward> &rewards,
	const RewardSelectionDeliveryPolicy &policy,
	const RewardSelectionBatchState &state,
	ItemResolver resolve_item,
	AlternateCurrencyResolver resolve_alternate_currency,
	TitleResolver resolve_title
)
{
	const auto max_int =
		static_cast<uint64_t>(std::numeric_limits<int>::max());
	const auto max_currency =
		static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
	uint64_t aa_total = 0;
	uint64_t platinum_total = 0;
	uint64_t gold_total = 0;
	uint64_t silver_total = 0;
	uint64_t copper_total = 0;
	bool has_coin_rewards = false;
	std::unordered_map<uint32_t, uint64_t> alternate_currency_totals;

	for (const auto &reward : rewards) {
		if (!IsValidRewardSelectionRewardDefinition(
			reward.type,
			reward.data_id,
			reward.amount
		)) {
			return false;
		}

		switch (reward.type) {
		case RewardSelectionRewardType::Item:
			if (!IsValidRewardSelectionItemAmount(
				resolve_item(reward.data_id),
				reward.amount
			)) {
				return false;
			}
			break;
		case RewardSelectionRewardType::Experience:
			if (
				!state.experience_enabled &&
				(
					reward.data_id == static_cast<uint32_t>(
						RewardSelectionExperienceMode::Default
					) ||
					policy.require_experience_enabled
				)
			) {
				return false;
			}
			break;
		case RewardSelectionRewardType::AlternateAdvancement:
			if (reward.amount > max_int - aa_total) {
				return false;
			}
			aa_total += reward.amount;
			break;
		case RewardSelectionRewardType::Copper: {
			has_coin_rewards = true;
			const auto platinum = reward.amount / 1000;
			const auto gold = (reward.amount % 1000) / 100;
			const auto silver = (reward.amount % 100) / 10;
			const auto copper = reward.amount % 10;
			if (
				platinum > max_currency - platinum_total ||
				gold > max_currency - gold_total ||
				silver > max_currency - silver_total ||
				copper > max_currency - copper_total
			) {
				return false;
			}
			platinum_total += platinum;
			gold_total += gold;
			silver_total += silver;
			copper_total += copper;
			break;
		}
		case RewardSelectionRewardType::AlternateCurrency: {
			auto &total = alternate_currency_totals[reward.data_id];
			if (reward.amount > max_int - total) {
				return false;
			}
			total += reward.amount;
			break;
		}
		case RewardSelectionRewardType::Title:
			if (!resolve_title(reward.data_id)) {
				return false;
			}
			break;
		}
	}

	if (aa_total) {
		if (state.aa_points < 0 || state.spent_aa_points < 0) {
			return false;
		}
		const auto current =
			static_cast<uint64_t>(state.aa_points) +
			static_cast<uint64_t>(state.spent_aa_points);
		if (current > max_int || aa_total > max_int - current) {
			return false;
		}
	}
	if (
		has_coin_rewards &&
		(
			state.platinum > max_currency ||
			state.gold > max_currency ||
			state.silver > max_currency ||
			state.copper > max_currency ||
			platinum_total > max_currency - state.platinum ||
			gold_total > max_currency - state.gold ||
			silver_total > max_currency - state.silver ||
			copper_total > max_currency - state.copper
		)
	) {
		return false;
	}
	for (const auto &[currency_id, total] : alternate_currency_totals) {
		const auto current = resolve_alternate_currency(currency_id);
		if (!current || *current > max_int || total > max_int - *current) {
			return false;
		}
	}
	return true;
}

struct ResolvedRewardSelectionClaim {
	RewardSelectionSession              session;
	// Provider/database identity used for authorization and persistence.
	uint32_t                            selected_option_id = 0;
	// Client identity echoed in the action-3 response.
	uint32_t                            selected_wire_option_id = 0;
	std::vector<RewardSelectionReward>  rewards;

	// Prevents a delayed result from completing a newer session.
	uint64_t session_generation = 0;
};

enum class RewardSelectionPacketResultType : uint8_t {
	Ignored,
	Handled,
	ViewRequested,
	PendingRequested,
	ClaimRequested
};

struct RewardSelectionPacketResult {
	RewardSelectionPacketResultType             type =
		RewardSelectionPacketResultType::Ignored;
	RewardSelectionSource                       requested_source =
		RewardSelectionSource::Unknown;
	// Achievement action 5 carries a zero-based definition index. Task action
	// 4 carries the task ID directly.
	uint32_t                                    requested_id = 0;
	std::optional<ResolvedRewardSelectionClaim> claim;
};

class ClientRewardSelection
{
public:
	explicit ClientRewardSelection(Client &client);

	// Copy the provider snapshot so content reloads cannot leave dangling state.
	bool Open(const RewardSelectionSession &session);
	bool Open(const std::vector<RewardSelectionSession> &sessions);
	bool ReplaceSourceSessions(
		RewardSelectionChannel channel,
		RewardSelectionSource source,
		const std::vector<RewardSelectionSession> &sessions
	);
	void Clear(RewardSelectionChannel channel, bool notify_client = true);
	void ClearSource(
		RewardSelectionChannel channel,
		RewardSelectionSource source,
		uint64_t source_id = 0,
		bool notify_client = true
	);
	void ClearSource(
		RewardSelectionSource source,
		uint64_t source_id = 0,
		bool notify_client = true
	);
	void ClearAll(bool notify_client = true);

	// Fully parsed offers replace an existing General session atomically.
	bool OfferScriptSelection(
	    ScriptRewardSelectionOffer offer, std::string &error);
	void ClearScriptOffer(bool notify_client = true);
	void ClearScriptOfferForOrigin(uint32_t npc_type_id, uint16_t entity_id);
	bool HasScriptOffer() const;
	RewardSelectionDeliveryResult CompleteScriptClaim(
		const ResolvedRewardSelectionClaim &claim,
		bool authorized
	);

	bool HasActiveSession(RewardSelectionChannel channel) const;
	const RewardSelectionSession *ActiveSession(
		RewardSelectionChannel channel
	) const;
	const RewardSelectionSession *FindSession(
		RewardSelectionChannel channel,
		RewardSelectionSource source,
		uint64_t source_id,
		uint64_t source_instance_id = 0
	) const;

	// Handles item inspection; provider-owned view, pending, and claim requests
	// are returned for authorization and durable ledger work.
	RewardSelectionPacketResult HandlePacket(
		const EQApplicationPacket &app,
		RewardSelectionChannel channel
	);

	std::optional<ResolvedRewardSelectionClaim> ResolveClaim(
		RewardSelectionChannel channel,
		uint32_t pending_reward_id,
		uint32_t reward_set_id,
		uint32_t selected_wire_option_id
	);

	// Retryable claims release the snapshot for reopening; ambiguous claims close
	// without automatic retry.
	void CompleteClaim(
		const ResolvedRewardSelectionClaim &claim,
		RewardSelectionDeliveryResult result
	);

	static RewardSelectionDeliveryResult GrantReward(
		Client &client,
		const RewardSelectionReward &reward,
		const RewardSelectionDeliveryPolicy &policy = {}
	);
	static RewardSelectionDeliveryResult GrantBatch(
		Client &client,
		const std::vector<RewardSelectionReward> &rewards,
		const RewardSelectionDeliveryPolicy &policy = {}
	);

private:
	struct ChannelState {
		std::vector<RewardSelectionSession>    sessions;
		uint64_t                              session_generation = 0;
		bool                                  claim_in_flight = false;
		RewardSelectionSource                 in_flight_source =
			RewardSelectionSource::Unknown;
		Timer                                 request_rate_limit;
		Timer                                 item_request_rate_limit;
		Timer                                 claim_request_rate_limit;
	};

	static bool ValidateSession(const RewardSelectionSession &session);
	static bool SameSession(
		const RewardSelectionSession &left,
		const RewardSelectionSession &right
	);
	static bool AssignWireOptionIds(
		std::vector<RewardSelectionSession> &sessions,
		const std::vector<RewardSelectionSession> &previous_sessions
	);
	bool OpenInternal(const std::vector<RewardSelectionSession> &sessions,
	    std::optional<RewardSelectionSource> replace_source);
	uint32_t AllocateScriptSelectionId();
	bool ValidateScriptRewardContent(
	    const RewardSelectionReward &reward, std::string &error) const;
	ChannelState &State(RewardSelectionChannel channel);
	const ChannelState &State(RewardSelectionChannel channel) const;
	bool SendSessions(RewardSelectionChannel channel);
	bool SendItemInspect(
		RewardSelectionChannel channel,
		uint32_t reward_set_id,
		uint32_t option_id,
		uint32_t reward_entry_id,
		uint32_t item_id
	);
	void SendClaimReply(
		RewardSelectionChannel channel,
		uint32_t pending_reward_id,
		uint32_t reward_set_id,
		uint32_t selected_option_id,
		bool success
	);

	Client       &m_client;
	ChannelState  m_claimable_channel;
	ChannelState  m_preview_channel;
	bool m_script_clear_requested_during_claim = false;
	bool m_script_clear_deferred_during_claim = false;
	uint32_t m_next_script_selection_id =
		std::numeric_limits<uint32_t>::max();
};
