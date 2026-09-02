#include "reward_selection.h"

#include "../common/eq_packet.h"
#include "../common/reward_selection.h"
#include "../common/rulesys.h"
#include "client.h"
#include "questmgr.h"
#include "titles.h"
#include "zone.h"
#include "zonedb.h"

#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <limits>
#include <memory>
#include <unordered_set>

namespace
{

constexpr uint32_t kRewardRequestRateLimitMs = 500;
constexpr uint32_t kRewardItemRequestRateLimitMs = 100;
constexpr uint32_t kRewardClaimRequestRateLimitMs = 500;
constexpr uint32_t kRoF2PlayerFlagsStringId = 3689;

uint32_t RewardDisplayValue(uint64_t value)
{
	return static_cast<uint32_t>(std::min<uint64_t>(
		value,
		std::numeric_limits<uint32_t>::max()
	));
}

std::string AlternateCurrencyDisplayName(uint32_t currency_id)
{
	if (!zone) {
		return {};
	}

	const auto item_id = zone->GetCurrencyItemID(currency_id);
	const auto item = item_id ? database.GetItem(item_id) : nullptr;
	return item ? item->Name : std::string{};
}

std::string TitleDisplayName(uint32_t title_set_id)
{
	std::unordered_set<std::string> names;
	for (const auto &title : title_manager.GetTitles()) {
		if (title.title_set != static_cast<int32_t>(title_set_id)) {
			continue;
		}
		if (!title.prefix.empty()) {
			names.insert(title.prefix);
		}
		if (!title.suffix.empty()) {
			names.insert(title.suffix);
		}
	}

	return names.size() == 1 ? *names.begin() : std::string{};
}

std::string RewardDisplayName(const RewardSelectionReward &reward)
{
	switch (reward.type) {
	case RewardSelectionRewardType::Item: {
		const auto item = database.GetItem(reward.data_id);
		return item ? item->Name : fmt::format("Item {}", reward.data_id);
	}
	case RewardSelectionRewardType::Experience:
		return reward.data_id == static_cast<uint32_t>(
		                             RewardSelectionExperienceMode::NormalOnly)
		           ? "Experience (No AA)"
		           : "Experience";
	case RewardSelectionRewardType::AlternateAdvancement:
		return "Alternate Advancement";
	case RewardSelectionRewardType::Copper:
		return "Coin";
	case RewardSelectionRewardType::AlternateCurrency: {
		const auto name = AlternateCurrencyDisplayName(reward.data_id);
		return name.empty() ? "Alternate Currency" : name;
	}
	case RewardSelectionRewardType::Title: {
		const auto name = TitleDisplayName(reward.data_id);
		return name.empty() ? "Title Unlock" : name;
	}
	}

	return "Reward";
}

std::string FormatCoinDescription(uint64_t copper)
{
	const auto platinum = copper / 1000;
	const auto gold = (copper % 1000) / 100;
	const auto silver = (copper % 100) / 10;
	const auto remaining_copper = copper % 10;
	std::string description;
	const auto append = [&description](uint64_t amount, const char *suffix) {
		if (!amount) {
			return;
		}
		if (!description.empty()) {
			description += " ";
		}
		description += fmt::format("{}{}", amount, suffix);
	};
	append(platinum, "pp");
	append(gold, "gp");
	append(silver, "sp");
	append(remaining_copper, "cp");
	return description.empty() ? "0cp" : description;
}

std::string InferredRewardDescription(const RewardSelectionReward &reward)
{
	if (!reward.description.empty()) {
		return reward.description;
	}

	const auto name = RewardDisplayName(reward);
	switch (reward.type) {
	case RewardSelectionRewardType::Item:
		return reward.amount == 1 ? name
		                          : fmt::format("{} x {}", reward.amount, name);
	case RewardSelectionRewardType::Experience:
		return fmt::format("{} {}", reward.amount, name);
	case RewardSelectionRewardType::AlternateAdvancement:
		return fmt::format("{} Alternate Advancement Point{}",
		    reward.amount,
		    reward.amount == 1 ? "" : "s");
	case RewardSelectionRewardType::Copper:
		return FormatCoinDescription(reward.amount);
	case RewardSelectionRewardType::AlternateCurrency:
		return fmt::format("{} {}", reward.amount, name);
	case RewardSelectionRewardType::Title:
		return name == "Title Unlock"
		           ? "Unlocks a player title"
		           : fmt::format("Unlocks the title '{}'", name);
	}

	return name;
}

std::string InferredOptionLabel(const RewardSelectionOption &option)
{
	if (!option.label.empty()) {
		return option.label;
	}
	if (option.common_to_all) {
		return "Also Includes";
	}
	if (option.rewards.empty()) {
		return "Reward";
	}

	std::string label = RewardDisplayName(option.rewards.front());
	if (option.rewards.size() >= 2) {
		label += " + " + RewardDisplayName(option.rewards[1]);
	}
	if (option.rewards.size() > 2) {
		label += fmt::format(" + {} more", option.rewards.size() - 2);
	}
	return label;
}

EQ::RewardSelection::DisplayEntry BuildDisplayEntry(
	const RewardSelectionReward &reward
)
{
	using namespace EQ::RewardSelection;

	DisplayEntry entry;
	entry.fields[0] = static_cast<uint32_t>(reward.entry_id);
	entry.description = InferredRewardDescription(reward);

	switch (reward.type) {
	case RewardSelectionRewardType::Item: {
		entry.wire_type = WireType::Item;
		const auto item = database.GetItem(reward.data_id);
		entry.items.push_back({
			reward.data_id,
			RewardDisplayValue(reward.amount),
			static_cast<uint8_t>(item && item->LoreFlag),
			item && item->LoreFlag ? static_cast<uint32_t>(item->LoreGroup) : 0,
			item ? item->Name : fmt::format("Item {}", reward.data_id)
		});
		if (entry.description.empty()) {
			entry.description = entry.items.front().text;
		}
		break;
	}
	case RewardSelectionRewardType::Experience:
		entry.wire_type = WireType::Experience;
		entry.fields[1] = 3690;
		if (entry.description.empty()) {
			entry.description = fmt::format("{} Experience", reward.amount);
		}
		break;
	case RewardSelectionRewardType::AlternateAdvancement:
		entry.wire_type = WireType::AlternateAdvancementPoints;
		entry.values[0] = RewardDisplayValue(reward.amount);
		if (entry.description.empty()) {
			entry.description = fmt::format(
				"{} Alternate Advancement Point{}",
				reward.amount,
				reward.amount == 1 ? "" : "s"
			);
		}
		break;
	case RewardSelectionRewardType::Copper:
		entry.wire_type = WireType::Money;
		entry.values[0] = RewardDisplayValue(reward.amount / 1000);
		entry.values[1] = RewardDisplayValue((reward.amount % 1000) / 100);
		entry.values[2] = RewardDisplayValue((reward.amount % 100) / 10);
		entry.values[3] = RewardDisplayValue(reward.amount % 10);
		if (entry.description.empty()) {
			entry.description = "Coin";
		}
		break;
	case RewardSelectionRewardType::AlternateCurrency:
		entry.wire_type = WireType::GenericPoints;
		entry.values[2] = RewardDisplayValue(reward.amount);
		entry.values[3] = reward.data_id;
		if (entry.description.empty()) {
			entry.description = fmt::format(
				"{} Alternate Currency {}",
				reward.amount,
				reward.data_id
			);
		}
		break;
	case RewardSelectionRewardType::Title:
		entry.wire_type = WireType::Text;
		entry.fields[1] = kRoF2PlayerFlagsStringId;
		if (entry.description.empty()) {
			entry.description = fmt::format("Title {}", reward.data_id);
		}
		break;
	}

	return entry;
}

EQ::RewardSelection::DisplaySet BuildDisplaySet(
	const RewardSelectionSession &session
)
{
	EQ::RewardSelection::DisplaySet display;
	display.pending_reward_id = session.pending_reward_id;
	display.reward_set_id = session.reward_set.reward_set_id;
	display.title = session.reward_set.title.empty() ? "Choose a Reward"
	                                                 : session.reward_set.title;
	display.subsets.reserve(session.reward_set.options.size());

	for (const auto &option : session.reward_set.options) {
		EQ::RewardSelection::DisplaySubset subset;
		subset.subset_id = option.wire_option_id;
		subset.common_to_all = option.common_to_all;
		subset.flags = option.flags;
		subset.option_label = InferredOptionLabel(option);
		subset.entries.reserve(option.rewards.size());
		for (const auto &reward : option.rewards) {
			subset.entries.push_back(BuildDisplayEntry(reward));
		}
		display.subsets.emplace_back(std::move(subset));
	}

	return display;
}

bool SupportsRewardSelection(const Client &client)
{
	return client.ClientVersion() == EQ::versions::ClientVersion::RoF2;
}

EmuOpcode RewardSelectionOpcode(RewardSelectionChannel channel)
{
	return channel == RewardSelectionChannel::Claimable
		? OP_AchievementReward
		: OP_RewardSelection;
}

uint64_t ScriptOriginKey()
{
	const auto owner = quest_manager.GetOwner();
	if (!owner || !owner->IsNPC()) {
		return 0;
	}

	return
		(static_cast<uint64_t>(owner->GetNPCTypeID()) << 32) |
		owner->GetID();
}

} // namespace

ClientRewardSelection::ClientRewardSelection(Client &client)
	: m_client(client)
{
}

bool ClientRewardSelection::ValidateScriptRewardContent(
    const RewardSelectionReward &reward, std::string &error) const
{
	if (!IsValidRewardSelectionRewardDefinition(
	        reward.type, reward.data_id, reward.amount)) {
		error = "reward definition is invalid";
		return false;
	}

	switch (reward.type) {
	case RewardSelectionRewardType::Item: {
		const auto item = database.GetItem(reward.data_id);
		if (!item) {
			error = fmt::format("item_id {} does not exist", reward.data_id);
			return false;
		}
		if (!IsValidRewardSelectionItemAmount(item, reward.amount)) {
			error = fmt::format(
				"item_id {} does not support quantity {}",
				reward.data_id,
				reward.amount
			);
			return false;
		}
		break;
	}
	case RewardSelectionRewardType::AlternateCurrency:
		if (!zone || !zone->DoesAlternateCurrencyExist(reward.data_id)) {
			error = fmt::format(
			    "alternate_currency_id {} does not exist", reward.data_id);
			return false;
		}
		break;
	case RewardSelectionRewardType::Title:
		if (std::none_of(title_manager.GetTitles().begin(),
		        title_manager.GetTitles().end(),
		        [&reward](const auto &title) {
			        return title.title_set ==
			               static_cast<int32_t>(reward.data_id);
		        })) {
			error =
			    fmt::format("title_set_id {} does not exist", reward.data_id);
			return false;
		}
		break;
	default:
		break;
	}
	return true;
}

bool ClientRewardSelection::OfferScriptSelection(
    ScriptRewardSelectionOffer offer, std::string &error)
{
	error.clear();
	if (!SupportsRewardSelection(m_client)) {
		error = "reward selection requires the RoF2 client";
		return false;
	}
	uint32_t common_option_id = 0;
	if (!AssignScriptRewardSelectionOptionIds(
		offer,
		common_option_id,
		&error
	)) {
		return false;
	}
	if (m_claimable_channel.claim_in_flight) {
		error = "a reward selection claim is already in progress";
		return false;
	}

	uint64_t next_entry_id = 1;
	for (size_t index = 0; index < offer.options.size(); ++index) {
		auto &option = offer.options[index];
		if (option.rewards.empty()) {
			error = fmt::format("options[{}] has no rewards", index + 1);
			return false;
		}
		option.sequence = static_cast<uint32_t>(index);
		option.common_to_all = false;
		for (size_t reward_index = 0; reward_index < option.rewards.size();
		     ++reward_index) {
			auto &reward = option.rewards[reward_index];
			std::string content_error;
			if (!ValidateScriptRewardContent(reward, content_error)) {
				error = fmt::format("options[{}].rewards[{}]: {}",
				    index + 1,
				    reward_index + 1,
				    content_error);
				return false;
			}
			if (next_entry_id > std::numeric_limits<uint32_t>::max()) {
				error = "options contains too many reward entries";
				return false;
			}
			reward.entry_id = next_entry_id++;
		}
	}

	if (!offer.common_rewards.empty()) {
		for (size_t index = 0; index < offer.common_rewards.size(); ++index) {
			auto &reward = offer.common_rewards[index];
			std::string content_error;
			if (!ValidateScriptRewardContent(reward, content_error)) {
				error = fmt::format(
				    "common_rewards[{}]: {}", index + 1, content_error);
				return false;
			}
			if (next_entry_id > std::numeric_limits<uint32_t>::max()) {
				error = "offer contains too many reward entries";
				return false;
			}
			reward.entry_id = next_entry_id++;
		}
	}

	for (size_t index = 0; index < offer.options.size(); ++index) {
		uint32_t left_item_id = 0;
		uint32_t right_item_id = 0;
		if (FindRewardSelectionLoreConflict(
			offer.common_rewards,
			offer.options[index].rewards,
			[](uint32_t item_id) {
				return database.GetItem(item_id);
			},
			left_item_id,
			right_item_id
		)) {
			error = fmt::format(
				"options[{}] would grant lore-conflicting items {} and {}",
				index + 1,
				left_item_id,
				right_item_id
			);
			return false;
		}
	}

	if (!offer.common_rewards.empty()) {
		RewardSelectionOption common_option;
		common_option.option_id = common_option_id;
		common_option.sequence = static_cast<uint32_t>(offer.options.size());
		common_option.common_to_all = true;
		common_option.rewards = std::move(offer.common_rewards);
		offer.options.emplace_back(std::move(common_option));
	}

	const auto selection_id = AllocateScriptSelectionId();
	if (!selection_id) {
		error = "no internal selection ID is available";
		return false;
	}
	RewardSelectionSession session;
	session.source.source = RewardSelectionSource::General;
	session.source.source_id = selection_id;
	session.source.source_instance_id = ScriptOriginKey();
	session.channel = RewardSelectionChannel::Claimable;
	session.pending_reward_id = selection_id;
	session.reward_set.reward_set_id = selection_id;
	session.reward_set.title = std::move(offer.title);
	session.reward_set.options = std::move(offer.options);
	if (!OpenInternal({session}, RewardSelectionSource::General)) {
		error = "the reward selection could not be opened";
		return false;
	}
	return true;
}

uint32_t ClientRewardSelection::AllocateScriptSelectionId()
{
	const auto &sessions = m_claimable_channel.sessions;
	for (size_t attempt = 0; attempt <= sessions.size() * 2; ++attempt) {
		const auto candidate = m_next_script_selection_id;
		m_next_script_selection_id = candidate == 1
			? std::numeric_limits<uint32_t>::max()
			: candidate - 1;
		const auto used = std::any_of(
			sessions.begin(),
			sessions.end(),
			[candidate](const RewardSelectionSession &session) {
				return
					session.pending_reward_id == candidate ||
					session.reward_set.reward_set_id == candidate;
			}
		);
		if (!used) {
			return candidate;
		}
	}
	return 0;
}

void ClientRewardSelection::ClearScriptOffer(bool notify_client)
{
	if (m_claimable_channel.claim_in_flight) {
		if (
			m_claimable_channel.in_flight_source ==
			RewardSelectionSource::General
		) {
			m_script_clear_requested_during_claim = true;
		}
		else {
			m_script_clear_deferred_during_claim = true;
		}
		return;
	}
	m_script_clear_requested_during_claim = false;

	ClearSource(
		RewardSelectionChannel::Claimable,
		RewardSelectionSource::General,
		0,
		notify_client
	);
}

void ClientRewardSelection::ClearScriptOfferForOrigin(
	uint32_t npc_type_id,
	uint16_t entity_id
)
{
	if (!npc_type_id || !entity_id) {
		return;
	}

	const auto origin_key =
		(static_cast<uint64_t>(npc_type_id) << 32) | entity_id;
	const auto matches_origin = [origin_key](
		const RewardSelectionSession &session
	) {
		return
			session.source.source == RewardSelectionSource::General &&
			session.source.source_instance_id == origin_key;
	};
	const bool open_matches = std::any_of(
		m_claimable_channel.sessions.begin(),
		m_claimable_channel.sessions.end(),
		matches_origin
	);
	if (open_matches) {
		ClearScriptOffer();
	}
}

bool ClientRewardSelection::HasScriptOffer() const
{
	return std::any_of(
		m_claimable_channel.sessions.begin(),
		m_claimable_channel.sessions.end(),
		[](const RewardSelectionSession &session) {
			return session.source.source == RewardSelectionSource::General;
		}
	);
}

RewardSelectionDeliveryResult ClientRewardSelection::CompleteScriptClaim(
	const ResolvedRewardSelectionClaim &claim,
	bool authorized
)
{
	if (claim.session.source.source != RewardSelectionSource::General) {
		return RewardSelectionDeliveryResult::RetryableFailure;
	}

	const bool cancel_before_delivery =
		m_script_clear_requested_during_claim;
	const auto result = authorized && !cancel_before_delivery
		? GrantBatch(m_client, claim.rewards)
		: RewardSelectionDeliveryResult::RetryableFailure;
	CompleteClaim(claim, result);

	if (m_script_clear_requested_during_claim) {
		// CompleteClaim has already removed the retryable session, so clearing
		// by source can no longer detect a state change to notify the client.
		// Reset the script state without a duplicate notification, then send
		// the remaining claimable collection (or an explicit display clear).
		ClearScriptOffer(false);
		SendSessions(RewardSelectionChannel::Claimable);
	}
	else if (
		result == RewardSelectionDeliveryResult::RetryableFailure ||
		result == RewardSelectionDeliveryResult::RetryableFailureSameOption
	) {
		auto retry_session = claim.session;
		if (
			result == RewardSelectionDeliveryResult::RetryableFailureSameOption
		) {
			std::erase_if(
				retry_session.reward_set.options,
				[&claim](const RewardSelectionOption &option) {
					return
						!option.common_to_all &&
						option.option_id != claim.selected_option_id;
				}
			);
		}
		Open(retry_session);
	}

	return result;
}

bool ClientRewardSelection::ValidateSession(
	const RewardSelectionSession &session
)
{
	if (
		session.source.source == RewardSelectionSource::Unknown ||
		!session.source.source_id ||
		!session.pending_reward_id ||
		!session.reward_set.reward_set_id ||
		session.reward_set.options.empty()
	) {
		return false;
	}
	if (!HasSupportedRewardSelectionCommonGrouping(
		session.reward_set.options
	)) {
		return false;
	}

	std::unordered_set<uint32_t> option_ids;
	std::unordered_set<uint64_t> entry_ids;
	for (const auto &option : session.reward_set.options) {
		if (
			!option.option_id ||
			!option_ids.insert(option.option_id).second ||
			option.rewards.empty()
		) {
			return false;
		}

		for (const auto &reward : option.rewards) {
			if (
				!reward.entry_id ||
				reward.entry_id > std::numeric_limits<uint32_t>::max() ||
				!entry_ids.insert(reward.entry_id).second ||
				!IsValidRewardSelectionRewardDefinition(
					reward.type,
					reward.data_id,
					reward.amount
				)
			) {
				return false;
			}
		}
	}

	return true;
}

bool ClientRewardSelection::SameSession(
	const RewardSelectionSession &left,
	const RewardSelectionSession &right
)
{
	return SameRewardSelectionSession(left, right);
}

bool ClientRewardSelection::AssignWireOptionIds(
	std::vector<RewardSelectionSession> &sessions,
	const std::vector<RewardSelectionSession> &previous_sessions
)
{
	return AssignStableRewardSelectionWireOptionIds(
		sessions,
		previous_sessions
	);
}

ClientRewardSelection::ChannelState &ClientRewardSelection::State(
	RewardSelectionChannel channel
)
{
	return channel == RewardSelectionChannel::Claimable
		? m_claimable_channel
		: m_preview_channel;
}

const ClientRewardSelection::ChannelState &ClientRewardSelection::State(
	RewardSelectionChannel channel
) const
{
	return channel == RewardSelectionChannel::Claimable
		? m_claimable_channel
		: m_preview_channel;
}

bool ClientRewardSelection::Open(const RewardSelectionSession &session)
{
	return Open(std::vector<RewardSelectionSession>{session});
}

bool ClientRewardSelection::Open(
    const std::vector<RewardSelectionSession> &sessions)
{
	return OpenInternal(sessions, std::nullopt);
}

bool ClientRewardSelection::OpenInternal(
    const std::vector<RewardSelectionSession> &sessions,
    std::optional<RewardSelectionSource> replace_source)
{
	if (!SupportsRewardSelection(m_client) || sessions.empty()) {
		return false;
	}

	const auto channel = sessions.front().channel;
	for (const auto &session : sessions) {
		if (session.channel != channel || !ValidateSession(session)) {
			return false;
		}
		if (channel == RewardSelectionChannel::Claimable &&
		    std::none_of(session.reward_set.options.begin(),
		        session.reward_set.options.end(),
		        [](const RewardSelectionOption &option) {
			        return !option.common_to_all;
		        })) {
			return false;
		}
	}

	auto &state = State(channel);
	if (state.claim_in_flight) {
		return false;
	}

	auto next_sessions = state.sessions;
	if (replace_source) {
		if (channel != RewardSelectionChannel::Claimable) {
			return false;
		}
		std::erase_if(next_sessions, [replace_source](const auto &session) {
			return session.source.source == *replace_source;
		});
	}

	if (channel == RewardSelectionChannel::Preview) {
		if (sessions.size() != 1) {
			return false;
		}
		next_sessions = sessions;
	} else {
		for (const auto &session : sessions) {
			const auto found = std::find_if(next_sessions.begin(),
			    next_sessions.end(),
			    [&session](const RewardSelectionSession &existing) {
				    return SameSession(existing, session);
			    });
			if (found == next_sessions.end()) {
				next_sessions.push_back(session);
			} else {
				*found = session;
			}
		}
	}
	if (!AssignWireOptionIds(next_sessions, state.sessions)) {
		return false;
	}
	state.sessions = std::move(next_sessions);

	++state.session_generation;
	if (!state.session_generation) {
		++state.session_generation;
	}
	state.claim_in_flight = false;
	return SendSessions(channel);
}

bool ClientRewardSelection::SendSessions(RewardSelectionChannel channel)
{
	const auto &state = State(channel);
	SerializeBuffer data;
	if (state.sessions.empty()) {
		data = EQ::RewardSelection::SerializeDisplayClear();
	}
	else if (channel == RewardSelectionChannel::Preview) {
		const auto display = BuildDisplaySet(state.sessions.front());
		data = EQ::RewardSelection::SerializeDisplay(&display);
	}
	else {
		std::vector<EQ::RewardSelection::DisplaySet> displays;
		displays.reserve(state.sessions.size());
		for (const auto &session : state.sessions) {
			displays.push_back(BuildDisplaySet(session));
		}
		data = EQ::RewardSelection::SerializeDisplays(displays);
	}

	auto packet = new EQApplicationPacket(
		RewardSelectionOpcode(channel),
		data
	);
	m_client.FastQueuePacket(&packet);
	return true;
}

void ClientRewardSelection::Clear(
	RewardSelectionChannel channel,
	bool notify_client
)
{
	auto &state = State(channel);
	state.sessions.clear();
	state.claim_in_flight = false;
	state.in_flight_source = RewardSelectionSource::Unknown;
	++state.session_generation;
	if (!state.session_generation) {
		++state.session_generation;
	}

	if (!notify_client || !SupportsRewardSelection(m_client)) {
		return;
	}

	SendSessions(channel);
}

void ClientRewardSelection::ClearSource(
	RewardSelectionChannel channel,
	RewardSelectionSource source,
	uint64_t source_id,
	bool notify_client
)
{
	auto &state = State(channel);
	const auto old_size = state.sessions.size();
	std::erase_if(
		state.sessions,
		[source, source_id](const RewardSelectionSession &session) {
			return
				session.source.source == source &&
				(!source_id || session.source.source_id == source_id);
		}
	);
	if (state.sessions.size() == old_size) {
		return;
	}
	state.claim_in_flight = false;
	state.in_flight_source = RewardSelectionSource::Unknown;
	++state.session_generation;
	if (!state.session_generation) {
		++state.session_generation;
	}
	if (notify_client && SupportsRewardSelection(m_client)) {
		SendSessions(channel);
	}
}

void ClientRewardSelection::ClearSource(
	RewardSelectionSource source,
	uint64_t source_id,
	bool notify_client
)
{
	for (const auto channel : {
		RewardSelectionChannel::Claimable,
		RewardSelectionChannel::Preview
	}) {
		ClearSource(channel, source, source_id, notify_client);
	}
}

void ClientRewardSelection::ClearAll(bool notify_client)
{
	Clear(RewardSelectionChannel::Claimable, notify_client);
	Clear(RewardSelectionChannel::Preview, notify_client);
}

bool ClientRewardSelection::HasActiveSession(
	RewardSelectionChannel channel
) const
{
	return !State(channel).sessions.empty();
}

const RewardSelectionSession *ClientRewardSelection::ActiveSession(
	RewardSelectionChannel channel
) const
{
	const auto &sessions = State(channel).sessions;
	return sessions.empty() ? nullptr : &sessions.front();
}

const RewardSelectionSession *ClientRewardSelection::FindSession(
	RewardSelectionChannel channel,
	RewardSelectionSource source,
	uint64_t source_id,
	uint64_t source_instance_id
) const
{
	const auto &sessions = State(channel).sessions;
	const auto found = std::find_if(
		sessions.begin(),
		sessions.end(),
		[source, source_id, source_instance_id](
			const RewardSelectionSession &session
		) {
			return
				session.source.source == source &&
				session.source.source_id == source_id &&
				(!source_instance_id ||
					session.source.source_instance_id == source_instance_id);
		}
	);
	return found == sessions.end() ? nullptr : &*found;
}

RewardSelectionPacketResult ClientRewardSelection::HandlePacket(
	const EQApplicationPacket &app,
	RewardSelectionChannel channel
)
{
	using namespace EQ::RewardSelection;

	RewardSelectionPacketResult result;
	if (!SupportsRewardSelection(m_client) || app.size < sizeof(uint32_t)) {
		return result;
	}

	uint32_t action = 0;
	std::memcpy(&action, app.pBuffer, sizeof(action));
	if (
		(channel == RewardSelectionChannel::Claimable &&
			action == RewardSelectionActionTaskView) ||
		(channel == RewardSelectionChannel::Preview &&
			(action == RewardSelectionActionAchievementView ||
				action == ActionClaim))
	) {
		return result;
	}
	if (
		((action == RewardSelectionActionTaskView ||
			action == RewardSelectionActionAchievementView) &&
			app.size != sizeof(uint32_t) * 2) ||
		(action == RewardSelectionActionPending &&
			app.size != sizeof(uint32_t)) ||
		(action == ActionInspectItem && app.size != sizeof(uint32_t) * 5) ||
		(action == ActionClaim && app.size != sizeof(uint32_t) * 5)
	) {
		return result;
	}

	if (
		action == RewardSelectionActionTaskView ||
		action == RewardSelectionActionAchievementView ||
		action == RewardSelectionActionPending
	) {
		auto &state = State(channel);
		if (
			state.request_rate_limit.Enabled() &&
			!state.request_rate_limit.Check(false)
		) {
			result.type = RewardSelectionPacketResultType::Handled;
			return result;
		}
		state.request_rate_limit.Start(kRewardRequestRateLimitMs);

		if (action == RewardSelectionActionPending) {
			result.requested_source = RewardSelectionSource::General;
			result.type = RewardSelectionPacketResultType::PendingRequested;
			return result;
		}

		std::memcpy(
			&result.requested_id,
			app.pBuffer + sizeof(action),
			sizeof(result.requested_id)
		);
		result.requested_source =
			action == RewardSelectionActionTaskView
				? RewardSelectionSource::Task
				: RewardSelectionSource::Achievement;
		result.type = RewardSelectionPacketResultType::ViewRequested;
		return result;
	}

	if (action == ActionInspectItem) {
		auto &state = State(channel);
		if (
			state.item_request_rate_limit.Enabled() &&
			!state.item_request_rate_limit.Check(false)
		) {
			result.type = RewardSelectionPacketResultType::Handled;
			return result;
		}
		state.item_request_rate_limit.Start(kRewardItemRequestRateLimitMs);

		uint32_t reward_set_id = 0;
		uint32_t option_id = 0;
		uint32_t reward_entry_id = 0;
		uint32_t item_id = 0;
		std::memcpy(
			&reward_set_id,
			app.pBuffer + sizeof(uint32_t),
			sizeof(reward_set_id)
		);
		std::memcpy(
			&option_id,
			app.pBuffer + sizeof(uint32_t) * 2,
			sizeof(option_id)
		);
		std::memcpy(
			&reward_entry_id,
			app.pBuffer + sizeof(uint32_t) * 3,
			sizeof(reward_entry_id)
		);
		std::memcpy(
			&item_id,
			app.pBuffer + sizeof(uint32_t) * 4,
			sizeof(item_id)
		);
		SendItemInspect(
			channel,
			reward_set_id,
			option_id,
			reward_entry_id,
			item_id
		);
		result.type = RewardSelectionPacketResultType::Handled;
		return result;
	}

	if (action != ActionClaim) {
		return result;
	}

	auto &state = State(channel);
	if (
		state.claim_request_rate_limit.Enabled() &&
		!state.claim_request_rate_limit.Check(false)
	) {
		result.type = RewardSelectionPacketResultType::Handled;
		return result;
	}
	state.claim_request_rate_limit.Start(kRewardClaimRequestRateLimitMs);

	uint32_t pending_reward_id = 0;
	uint32_t reward_set_id = 0;
	uint32_t selected_wire_option_id = 0;
	std::memcpy(
		&pending_reward_id,
		app.pBuffer + sizeof(uint32_t),
		sizeof(pending_reward_id)
	);
	std::memcpy(
		&reward_set_id,
		app.pBuffer + sizeof(uint32_t) * 2,
		sizeof(reward_set_id)
	);
	std::memcpy(
		&selected_wire_option_id,
		app.pBuffer + sizeof(uint32_t) * 3,
		sizeof(selected_wire_option_id)
	);

	result.claim = ResolveClaim(
		channel,
		pending_reward_id,
		reward_set_id,
		selected_wire_option_id
	);
	if (!result.claim) {
		SendClaimReply(
			channel,
			pending_reward_id,
			reward_set_id,
			selected_wire_option_id,
			false
		);
		result.type = RewardSelectionPacketResultType::Handled;
		return result;
	}

	result.type = RewardSelectionPacketResultType::ClaimRequested;
	return result;
}

bool ClientRewardSelection::SendItemInspect(
	RewardSelectionChannel channel,
	uint32_t reward_set_id,
	uint32_t option_id,
	uint32_t reward_entry_id,
	uint32_t item_id
)
{
	auto &state = State(channel);
	if (
		state.sessions.empty() ||
		state.claim_in_flight ||
		!item_id
	) {
		return false;
	}

	const RewardSelectionReward *matched_reward = nullptr;
	for (const auto &session : state.sessions) {
		if (session.reward_set.reward_set_id != reward_set_id) {
			continue;
		}
		for (const auto &option : session.reward_set.options) {
			if (option.wire_option_id != option_id) {
				continue;
			}
			const auto reward = std::find_if(
				option.rewards.begin(),
				option.rewards.end(),
				[reward_entry_id, item_id](
					const RewardSelectionReward &candidate
				) {
					return
						candidate.entry_id == reward_entry_id &&
						candidate.type == RewardSelectionRewardType::Item &&
						candidate.data_id == item_id;
				}
			);
			if (reward != option.rewards.end()) {
				matched_reward = &*reward;
			}
			break;
		}
		if (matched_reward) {
			break;
		}
	}
	if (!matched_reward) {
		return false;
	}

	const auto charges = matched_reward->amount <=
		static_cast<uint64_t>(std::numeric_limits<int16_t>::max())
		? static_cast<int16_t>(matched_reward->amount)
		: 0;
	std::unique_ptr<EQ::ItemInstance> instance(
		database.CreateItem(item_id, charges)
	);
	if (!instance) {
		return false;
	}

	const auto internal_item = instance->Serialize(0);
	auto packet = new EQApplicationPacket(
		RewardSelectionOpcode(channel),
		sizeof(uint32_t) * 2 + internal_item.size()
	);
	uint32_t action = EQ::RewardSelection::ActionInspectItem;
	std::memcpy(packet->pBuffer, &action, sizeof(action));
	std::memcpy(
		packet->pBuffer + sizeof(action),
		&item_id,
		sizeof(item_id)
	);
	std::memcpy(
		packet->pBuffer + sizeof(action) + sizeof(item_id),
		internal_item.data(),
		internal_item.size()
	);
	m_client.FastQueuePacket(&packet);
	return true;
}

std::optional<ResolvedRewardSelectionClaim>
ClientRewardSelection::ResolveClaim(
	RewardSelectionChannel channel,
	uint32_t pending_reward_id,
	uint32_t reward_set_id,
	uint32_t selected_wire_option_id
)
{
	auto &state = State(channel);
	if (
		channel != RewardSelectionChannel::Claimable ||
		state.sessions.empty() ||
		state.claim_in_flight ||
		!pending_reward_id ||
		!reward_set_id
	) {
		return std::nullopt;
	}
	const auto session = std::find_if(
		state.sessions.begin(),
		state.sessions.end(),
		[pending_reward_id, reward_set_id, selected_wire_option_id](
			const RewardSelectionSession &candidate
		) {
			return
				candidate.channel == RewardSelectionChannel::Claimable &&
				candidate.pending_reward_id == pending_reward_id &&
				candidate.reward_set.reward_set_id == reward_set_id &&
				std::any_of(
					candidate.reward_set.options.begin(),
					candidate.reward_set.options.end(),
					[selected_wire_option_id](
						const RewardSelectionOption &option
					) {
						return
							!option.common_to_all &&
							option.wire_option_id == selected_wire_option_id;
					}
				);
		}
	);
	if (session == state.sessions.end()) {
		return std::nullopt;
	}

	const RewardSelectionOption *selected_option = nullptr;
	ResolvedRewardSelectionClaim resolution;
	resolution.session = *session;
	resolution.selected_wire_option_id = selected_wire_option_id;
	resolution.session_generation = state.session_generation;

	for (const auto &option : session->reward_set.options) {
		if (option.common_to_all) {
			resolution.rewards.insert(
				resolution.rewards.end(),
				option.rewards.begin(),
				option.rewards.end()
			);
		}
		else if (option.wire_option_id == selected_wire_option_id) {
			selected_option = &option;
			resolution.selected_option_id = option.option_id;
		}
	}
	if (!selected_option) {
		return std::nullopt;
	}

	resolution.rewards.insert(
		resolution.rewards.end(),
		selected_option->rewards.begin(),
		selected_option->rewards.end()
	);
	state.in_flight_source = session->source.source;
	state.claim_in_flight = true;
	return resolution;
}

void ClientRewardSelection::CompleteClaim(
	const ResolvedRewardSelectionClaim &claim,
	RewardSelectionDeliveryResult result
)
{
	const auto channel = claim.session.channel;
	if (channel != RewardSelectionChannel::Claimable) {
		return;
	}
	auto &state = State(channel);
	const auto session = std::find_if(
		state.sessions.begin(),
		state.sessions.end(),
		[&claim](const RewardSelectionSession &candidate) {
			return SameSession(candidate, claim.session);
		}
	);
	const bool matches_active =
		session != state.sessions.end() &&
		state.claim_in_flight &&
		claim.session_generation == state.session_generation;
	const bool delivered =
		matches_active &&
		result == RewardSelectionDeliveryResult::Delivered;

	SendClaimReply(
		channel,
		claim.session.pending_reward_id,
		claim.session.reward_set.reward_set_id,
		claim.selected_wire_option_id,
		delivered
	);

	if (!matches_active) {
		return;
	}
	if (
		result == RewardSelectionDeliveryResult::Ambiguous ||
		result == RewardSelectionDeliveryResult::RetryableFailure ||
		result == RewardSelectionDeliveryResult::RetryableFailureSameOption ||
		delivered
	) {
		state.sessions.erase(session);
		++state.session_generation;
		if (!state.session_generation) {
			++state.session_generation;
		}
	}
	state.claim_in_flight = false;
	state.in_flight_source = RewardSelectionSource::Unknown;
	if (m_script_clear_deferred_during_claim) {
		m_script_clear_deferred_during_claim = false;
		ClearScriptOffer(false);
		SendSessions(channel);
	}
	else if (
		result != RewardSelectionDeliveryResult::RetryableFailure &&
		result != RewardSelectionDeliveryResult::RetryableFailureSameOption
	) {
		SendSessions(channel);
	}
}

void ClientRewardSelection::SendClaimReply(
	RewardSelectionChannel channel,
	uint32_t pending_reward_id,
	uint32_t reward_set_id,
	uint32_t selected_option_id,
	bool success
)
{
	auto data = EQ::RewardSelection::SerializeClaimReply(
		pending_reward_id,
		reward_set_id,
		selected_option_id,
		success
	);
	auto packet = new EQApplicationPacket(RewardSelectionOpcode(channel), data);
	m_client.FastQueuePacket(&packet);
}

RewardSelectionDeliveryResult ClientRewardSelection::GrantBatch(
	Client &client,
	const std::vector<RewardSelectionReward> &rewards,
	const RewardSelectionDeliveryPolicy &policy
)
{
	if (HasRewardSelectionInventoryLoreConflict(
		rewards,
		[](uint32_t item_id) { return database.GetItem(item_id); },
		[&client](const EQ::ItemData *item) {
			return client.CheckLoreConflict(item);
		}
	)) {
		return RewardSelectionDeliveryResult::RetryableFailure;
	}

	const auto item_reward_count = static_cast<size_t>(std::count_if(
		rewards.begin(),
		rewards.end(),
		[](const RewardSelectionReward &reward) {
			return reward.type == RewardSelectionRewardType::Item;
		}
	));
	if (
		item_reward_count &&
		!HasRewardSelectionCursorCapacity(
			client.GetInv().CursorSize(),
			item_reward_count,
			EQ::invbag::CURSOR_BAG_COUNT
		)
	) {
		return RewardSelectionDeliveryResult::RetryableFailure;
	}

	bool delivered_non_idempotent = false;
	bool delivered_idempotent = false;
	for (const auto &reward : rewards) {
		const auto result = GrantReward(client, reward, policy);
		if (result == RewardSelectionDeliveryResult::Delivered) {
			delivered_non_idempotent =
				delivered_non_idempotent ||
				!IsRewardSelectionRewardIdempotent(reward.type);
			delivered_idempotent =
				delivered_idempotent ||
				IsRewardSelectionRewardIdempotent(reward.type);
			continue;
		}

		// Scripted batches have no per-entry ledger. Stop at the first failure;
		// if an earlier non-idempotent entry committed, quarantine the result as
		// ambiguous so a retry cannot duplicate the successful prefix. Titles may
		// be replayed safely because EnableTitle ignores an already-owned set.
		return ResolveTransientRewardBatchFailure(
			delivered_non_idempotent,
			delivered_idempotent,
			result
		);
	}
	return RewardSelectionDeliveryResult::Delivered;
}

RewardSelectionDeliveryResult ClientRewardSelection::GrantReward(
	Client &client,
	const RewardSelectionReward &reward,
	const RewardSelectionDeliveryPolicy &policy
)
{
	const auto amount = reward.amount;
	if (
		!IsValidRewardSelectionRewardDefinition(
			reward.type,
			reward.data_id,
			amount
		)
	) {
		return RewardSelectionDeliveryResult::RetryableFailure;
	}

	switch (reward.type) {
	case RewardSelectionRewardType::Item: {
		const auto item = database.GetItem(reward.data_id);
		if (
			!IsValidRewardSelectionItemAmount(item, amount) ||
			client.GetInv().CursorSize() >= EQ::invbag::CURSOR_BAG_COUNT
		) {
			return RewardSelectionDeliveryResult::RetryableFailure;
		}
		bool persistence_succeeded = false;
		const auto summoned = client.SummonItem(
			reward.data_id,
			static_cast<int16_t>(amount),
			0,
			0,
			0,
			0,
			0,
			0,
			false,
			EQ::invslot::slotCursor,
			0,
			0,
			0,
			&persistence_succeeded
		);
		if (!summoned) {
			return RewardSelectionDeliveryResult::RetryableFailure;
		}
		return persistence_succeeded
			? RewardSelectionDeliveryResult::Delivered
			: RewardSelectionDeliveryResult::Ambiguous;
	}
	case RewardSelectionRewardType::Experience:
		if (
			!client.IsEXPEnabled() &&
			(
				reward.data_id ==
					static_cast<uint32_t>(
						RewardSelectionExperienceMode::Default
					) ||
				policy.require_experience_enabled
			)
		) {
			return RewardSelectionDeliveryResult::RetryableFailure;
		}
		if (
			reward.data_id ==
			static_cast<uint32_t>(RewardSelectionExperienceMode::NormalOnly)
		) {
			client.SetEXP(
				policy.experience_source,
				static_cast<uint64_t>(client.GetEXP()) + amount,
				client.GetAAXP()
			);
		}
		else {
			client.AddEXP(policy.experience_source, amount);
		}
		return database.SaveCharacterData(
			&client,
			&client.GetPP(),
			&client.GetEPP()
		)
			? RewardSelectionDeliveryResult::Delivered
			: RewardSelectionDeliveryResult::Ambiguous;
	case RewardSelectionRewardType::AlternateAdvancement: {
		const auto current_points = client.GetAAPoints();
		const auto spent_points = client.GetSpentAA();
		if (current_points < 0 || spent_points < 0) {
			return RewardSelectionDeliveryResult::RetryableFailure;
		}
		const auto current_total =
			static_cast<uint64_t>(current_points) +
			static_cast<uint64_t>(spent_points);
		if (
			!amount ||
			current_total > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
			amount >
				static_cast<uint64_t>(std::numeric_limits<int>::max()) -
					current_total
		) {
			return RewardSelectionDeliveryResult::RetryableFailure;
		}
		client.AddAAPoints(static_cast<uint32_t>(amount));
		return database.SaveCharacterData(
			&client,
			&client.GetPP(),
			&client.GetEPP()
		)
			? RewardSelectionDeliveryResult::Delivered
			: RewardSelectionDeliveryResult::Ambiguous;
	}
	case RewardSelectionRewardType::Copper: {
		if (!amount) {
			return RewardSelectionDeliveryResult::RetryableFailure;
		}
		{
			const auto max_currency =
				static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
			const auto platinum = amount / 1000;
			const auto gold = (amount % 1000) / 100;
			const auto silver = (amount % 100) / 10;
			const auto copper = amount % 10;
			if (
				client.GetPlatinum() > max_currency ||
				client.GetGold() > max_currency ||
				client.GetSilver() > max_currency ||
				client.GetCopper() > max_currency ||
				platinum > max_currency - client.GetPlatinum() ||
				gold > max_currency - client.GetGold() ||
				silver > max_currency - client.GetSilver() ||
				copper > max_currency - client.GetCopper()
			) {
				return RewardSelectionDeliveryResult::RetryableFailure;
			}
		}
		bool persistence_succeeded = false;
		client.AddMoneyToPP(amount, true, &persistence_succeeded);
		return persistence_succeeded
			? RewardSelectionDeliveryResult::Delivered
			: RewardSelectionDeliveryResult::Ambiguous;
	}
	case RewardSelectionRewardType::AlternateCurrency:
		if (
			!reward.data_id ||
			!amount ||
			amount > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
			!zone ||
			!zone->DoesAlternateCurrencyExist(reward.data_id)
		) {
			return RewardSelectionDeliveryResult::RetryableFailure;
		}
		{
			const auto current = client.GetAlternateCurrencyValue(reward.data_id);
			if (
				current > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
				amount >
					static_cast<uint64_t>(std::numeric_limits<int>::max()) -
						static_cast<int>(current)
			) {
				return RewardSelectionDeliveryResult::RetryableFailure;
			}
			const auto expected =
				static_cast<int>(current) + static_cast<int>(amount);
			bool persistence_succeeded = false;
			const auto result = client.AddAlternateCurrencyValue(
				reward.data_id,
				static_cast<int>(amount),
				true,
				&persistence_succeeded
			);
			if (result != expected) {
				return client.GetAlternateCurrencyValue(reward.data_id) == current
					? RewardSelectionDeliveryResult::RetryableFailure
					: RewardSelectionDeliveryResult::Ambiguous;
			}
			return persistence_succeeded
				? RewardSelectionDeliveryResult::Delivered
				: RewardSelectionDeliveryResult::Ambiguous;
		}
	case RewardSelectionRewardType::Title:
		if (
			!reward.data_id ||
			reward.data_id >
				static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
			!std::any_of(
				title_manager.GetTitles().begin(),
				title_manager.GetTitles().end(),
				[&reward](const auto &title) {
					return title.title_set == static_cast<int>(reward.data_id);
				}
			)
		) {
			return RewardSelectionDeliveryResult::RetryableFailure;
		}
		client.EnableTitle(static_cast<int>(reward.data_id));
		return client.CheckTitle(static_cast<int>(reward.data_id))
			? RewardSelectionDeliveryResult::Delivered
			: RewardSelectionDeliveryResult::RetryableFailure;
	}

	return RewardSelectionDeliveryResult::RetryableFailure;
}
