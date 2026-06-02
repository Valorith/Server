#include "expedition_db.h"

#include "expedition_repository.h"
#include "common/repositories/dynamic_zone_templates_repository.h"
#include "common/say_link.h"
#include "common/strings.h"
#include "common/zone_store.h"
#include "zone/client.h"
#include "zone/dynamic_zone.h"
#include "zone/entity.h"
#include "zone/expedition_config.h"
#include "zone/event_codes.h"
#include "zone/npc.h"
#include "zone/quest_parser_collection.h"
#include "zone/zone.h"
#include "zone/zonedb.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <unordered_map>
#include <unordered_set>

namespace ExpeditionDB {

std::string NpcTypeName(uint32_t npc_type_id)
{
	if (npc_type_id == 0) {
		return "";
	}

	std::string name = content_db.GetCleanNPCNameByID(npc_type_id);
	if (name.empty()) {
		name = content_db.GetNPCNameByID(npc_type_id);
		Strings::FindReplace(name, "_", " ");
		Strings::Trim(name);
	}

	return name.empty() ? "unknown NPC" : name;
}

std::string NpcTypeLabel(uint32_t npc_type_id)
{
	if (npc_type_id == 0) {
		return "unset";
	}

	return fmt::format("{} ({})", NpcTypeName(npc_type_id), npc_type_id);
}

namespace {
	constexpr const char* kManageRule = "===========================================";

	std::unordered_map<uint32_t, Template> g_templates;
	std::unordered_map<uint32_t, BuilderState> g_builder_states;
	std::unordered_map<uint32_t, uint16_t> g_last_gm_target_menu_entity;
	std::unordered_map<uint32_t, std::unordered_set<std::string>> g_completed_runtime_events;

	uint32_t UInt(const char* value)
	{
		return value ? static_cast<uint32_t>(strtoul(value, nullptr, 10)) : 0;
	}

	std::string Escape(const std::string& value)
	{
		return Strings::Escape(value);
	}

	std::string NormalizeRequestMode(const std::string& value)
	{
		if (Strings::EqualFold(value, "script_only") || Strings::EqualFold(value, "script")) {
			return "script_only";
		}

		if (Strings::EqualFold(value, "script_can_opt_in") || Strings::EqualFold(value, "script_opt_in") || Strings::EqualFold(value, "opt_in")) {
			return "script_can_opt_in";
		}

		return "db_only";
	}

	bool IsKnownRequestMode(const std::string& value)
	{
		return (
			Strings::EqualFold(value, "db_only") ||
			Strings::EqualFold(value, "db") ||
			Strings::EqualFold(value, "script_only") ||
			Strings::EqualFold(value, "script") ||
			Strings::EqualFold(value, "script_can_opt_in") ||
			Strings::EqualFold(value, "script_opt_in") ||
			Strings::EqualFold(value, "opt_in")
		);
	}

	std::string Slugify(std::string value)
	{
		std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		for (char& c : value) {
			if (!std::isalnum(static_cast<unsigned char>(c))) {
				c = '_';
			}
		}

		while (value.find("__") != std::string::npos) {
			value = Strings::Replace(value, "__", "_");
		}

		if (!value.empty() && value.front() == '_') {
			value.erase(value.begin());
		}

		if (!value.empty() && value.back() == '_') {
			value.pop_back();
		}

		return value.empty() ? "expedition" : value;
	}

	std::string UniqueSlug(const std::string& name, uint32_t ignore_template_id = 0)
	{
		const std::string base_slug = Slugify(name);
		std::string slug = base_slug;
		uint32_t suffix = ignore_template_id ? ignore_template_id : 2;
		bool conflict = true;
		while (conflict) {
			conflict = false;
			for (const auto& [other_id, other_template] : g_templates) {
				if (other_id != ignore_template_id && Strings::EqualFold(other_template.slug, slug)) {
					slug = fmt::format("{}_{}", base_slug, suffix++);
					conflict = true;
					break;
				}
			}
		}

		return slug;
	}

	bool QueryOK(Database& db, const std::string& query)
	{
		auto results = db.QueryDatabase(query);
		return results.Success();
	}

	uint32_t InsertID(Database& db, const std::string& query)
	{
		auto results = db.QueryDatabase(query);
		if (!results.Success()) {
			return 0;
		}

		return results.LastInsertedID();
	}

	DynamicZone BuildDynamicZone(const Template& template_data)
	{
		DynamicZone dz(DynamicZoneType::Expedition);
		dz.LoadTemplate(template_data.dz_template);
		return dz;
	}

	const Template* FindTemplateByDz(DynamicZone& expedition)
	{
		for (const auto& [id, template_data] : g_templates) {
			if (!template_data.enabled) {
				continue;
			}

			if (
				template_data.dz_template.zone_id == expedition.GetZoneID() &&
				template_data.dz_template.zone_version == expedition.GetZoneVersion() &&
				Strings::EqualFold(template_data.dz_template.name, expedition.GetName())
			) {
				return &template_data;
			}
		}

		return nullptr;
	}

	bool MatchNpc(const EventNpc& configured, NPC& npc)
	{
		if (configured.spawn2_id != 0 && configured.spawn2_id != npc.GetSpawnPointID()) {
			return false;
		}

		return configured.npc_type_id == npc.GetNPCTypeID();
	}

	bool MatchRequestNpc(const RequestNpc& configured, NPC& npc)
	{
		if (!zone || configured.zone_id != zone->GetZoneID()) {
			return false;
		}

		if (configured.zone_version != -1 && configured.zone_version != static_cast<int32_t>(zone->GetInstanceVersion())) {
			return false;
		}

		if (configured.spawn2_id != 0 && configured.spawn2_id != npc.GetSpawnPointID()) {
			return false;
		}

		return configured.npc_type_id == npc.GetNPCTypeID();
	}

	int RequestNpcSpecificity(const RequestNpc& configured, NPC& npc)
	{
		if (!MatchRequestNpc(configured, npc)) {
			return -1;
		}

		int score = 0;
		if (configured.zone_version == static_cast<int32_t>(zone->GetInstanceVersion())) {
			score += 2;
		}
		if (configured.spawn2_id != 0) {
			score += 1;
		}

		return score;
	}

	std::string NormalizeSayText(const std::string& message)
	{
		std::string text = message;
		Strings::Trim(text);
		std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

		return text;
	}

	bool HasHailPrefix(const std::string& text)
	{
		if (text == "hail") {
			return true;
		}

		if (!text.starts_with("hail") || text.size() <= 4) {
			return false;
		}

		const auto next = static_cast<unsigned char>(text[4]);
		return std::isspace(next) || std::ispunct(next);
	}

	std::string CommandWord(const std::string& message)
	{
		auto text = NormalizeSayText(message);
		if (HasHailPrefix(text)) {
			text = text.substr(4);
			while (!text.empty()) {
				const auto next = static_cast<unsigned char>(text.front());
				if (!std::isspace(next) && !std::ispunct(next)) {
					break;
				}
				text.erase(text.begin());
			}
		}

		return text;
	}

	bool IsHailMessage(const std::string& message)
	{
		return HasHailPrefix(NormalizeSayText(message));
	}

	std::string RequestPhrase(const Template& template_data, const RequestNpc& request_npc)
	{
		return NormalizePhrase(request_npc.phrase.empty() ? template_data.request_phrase : request_npc.phrase);
	}

	std::string RequestMenuPhrase(uint32_t template_id)
	{
		return fmt::format("dbexpedition request {}", template_id);
	}

	std::string EnterMenuPhrase(uint32_t template_id)
	{
		return fmt::format("dbexpedition enter {}", template_id);
	}

	std::string LeaveMenuPhrase(uint32_t template_id)
	{
		return fmt::format("dbexpedition leave {}", template_id);
	}

	uint32_t RequestMenuTemplateId(const std::string& phrase)
	{
		static const std::string prefix = "dbexpedition request ";
		if (!phrase.starts_with(prefix)) {
			return 0;
		}

		return Strings::ToUnsignedInt(phrase.substr(prefix.size()));
	}

	uint32_t EnterMenuTemplateId(const std::string& phrase)
	{
		static const std::string prefix = "dbexpedition enter ";
		if (!phrase.starts_with(prefix)) {
			return 0;
		}

		return Strings::ToUnsignedInt(phrase.substr(prefix.size()));
	}

	uint32_t LeaveMenuTemplateId(const std::string& phrase)
	{
		static const std::string prefix = "dbexpedition leave ";
		if (!phrase.starts_with(prefix)) {
			return 0;
		}

		return Strings::ToUnsignedInt(phrase.substr(prefix.size()));
	}

	bool IsMatchingExpedition(DynamicZone& expedition, const Template& template_data)
	{
		DynamicZone dz = BuildDynamicZone(template_data);
		return Strings::EqualFold(expedition.GetName(), dz.GetName()) &&
			expedition.GetZoneID() == dz.GetZoneID() &&
			expedition.GetZoneVersion() == dz.GetZoneVersion();
	}

	std::string RequestFailureLabel(const ExpeditionCheckResult& check)
	{
		if (check.reason == "member_already_in_expedition") {
			return "already in an expedition";
		}

		if (check.reason == "replay_lockout") {
			return "replay timer active";
		}

		if (check.reason == "event_lockout_conflict") {
			return "event lockout conflict";
		}

		if (check.reason == "player_count") {
			return fmt::format("need {}-{} players", check.min_players, check.max_players);
		}

		if (check.reason == "no_members") {
			return "no eligible members";
		}

		if (check.reason == "member_lookup_failed") {
			return "member lookup failed";
		}

		if (check.reason == "template_disabled") {
			return "expedition disabled";
		}

		if (check.reason == "template_not_found") {
			return "expedition not found";
		}

		return "request unavailable";
	}

	std::string RequestStatusLabel(const ExpeditionCheckResult& check)
	{
		if (check.success) {
			return "Available";
		}

		return IsRequesterLockoutReason(check.reason) ? "Locked" : "Unavailable";
	}

	bool TryCreateTemplateRequest(Client& client, const Template& template_data)
	{
		DynamicZone dz = BuildDynamicZone(template_data);
		const auto check = CheckExpeditionRequest(client, dz, true);
		if (!check.success) {
			if (!template_data.silent) {
				client.Message(Chat::Red, fmt::format("Cannot create expedition: {}.", RequestFailureLabel(check)).c_str());
			}
			return true;
		}

		if (DynamicZone* expedition = CreateExpeditionFromTemplate(client, template_data)) {
			expedition->MovePCInto(&client, true);
			return true;
		}

		if (!template_data.silent) {
			client.Message(Chat::Red, "Unable to create that expedition right now.");
		}
		return true;
	}

	bool TryEnterTemplateRequest(Client& client, const Template& template_data)
	{
		DynamicZone* expedition = client.GetExpedition();
		if (!expedition) {
			client.Message(Chat::Red, "No active expedition.");
			return true;
		}

		if (!IsMatchingExpedition(*expedition, template_data)) {
			client.Message(Chat::Red, "Already in another expedition.");
			return true;
		}

		if (expedition->IsCurrentZoneDz()) {
			client.Message(Chat::Yellow, "Already inside expedition.");
			return true;
		}

		if (!client.MovePCExpedition(false)) {
			client.Message(Chat::Red, "Cannot enter expedition.");
		}

		return true;
	}

	bool TryLeaveTemplateRequest(Client& client, const Template& template_data)
	{
		DynamicZone* expedition = client.GetExpedition();
		if (!expedition) {
			client.Message(Chat::Red, "No active expedition.");
			return true;
		}

		if (!IsMatchingExpedition(*expedition, template_data)) {
			client.Message(Chat::Red, "Already in another expedition.");
			return true;
		}

		if (!expedition->IsCurrentZoneDz()) {
			client.Message(Chat::Yellow, "Already outside instance.");
			return true;
		}

		const auto& safe_return = expedition->GetSafeReturnLocation();
		if (safe_return.zone_id != 0) {
			client.MovePC(safe_return.zone_id, 0, safe_return.x, safe_return.y, safe_return.z, safe_return.heading);
			return true;
		}

		const glm::vec4 safe = ZoneStore::Instance()->GetZoneSafeCoordinates(expedition->GetZoneID(), expedition->GetZoneVersion());
		client.MovePC(expedition->GetZoneID(), 0, safe.x, safe.y, safe.z, safe.w, 0, ZoneMode::ZoneToSafeCoords);
		return true;
	}

	void OfferRequestMenu(Client& client, const RequesterMatches& matches)
	{
		if (matches.empty()) {
			return;
		}

		const bool multi = matches.size() > 1;
		DynamicZone* active_expedition = client.GetExpedition();

		// Each menu entry is either a clickable action (phrase + button label) or an info note.
		struct MenuEntry {
			std::string name;
			std::string phrase;
			std::string button;
			std::string status;
			std::string note;
		};
		std::vector<MenuEntry> entries;
		entries.reserve(matches.size());
		for (const auto& match : matches) {
			const Template* template_data = match.template_data;
			const RequestNpc* request_npc = match.request_npc;
			if (!template_data || !request_npc) {
				continue;
			}

			MenuEntry entry;
			entry.name = RequesterMenuLabel(*template_data);
			if (active_expedition) {
				if (IsMatchingExpedition(*active_expedition, *template_data)) {
					entry.status = "Current";
					if (active_expedition->IsCurrentZoneDz()) {
						entry.phrase = LeaveMenuPhrase(template_data->id);
						entry.button = "Leave Instance";
						entry.note = "inside now";
					}
					else {
						entry.phrase = EnterMenuPhrase(template_data->id);
						entry.button = "Enter Expedition";
						entry.note = "ready to enter";
					}
				}
				else {
					entry.status = "Unavailable";
					entry.note = "already in another expedition";
				}
			}
			else {
				DynamicZone dz = BuildDynamicZone(*template_data);
				const auto check = CheckExpeditionRequest(client, dz, true);
				entry.status = RequestStatusLabel(check);
				if (check.success) {
					entry.phrase = RequestMenuPhrase(template_data->id);
					entry.button = "Form Expedition";
				}
				else {
					entry.note = RequestFailureLabel(check);
				}
			}
			entries.push_back(std::move(entry));
		}

		if (entries.empty()) {
			return;
		}

		const std::string title = multi ? "   Expeditions Offered Here" : fmt::format("   Expedition: {}", entries.front().name);
		client.Message(Chat::NPCQuestSay, "%s", kManageRule);
		client.Message(Chat::NPCQuestSay, "%s", title.c_str());
		client.Message(Chat::NPCQuestSay, "%s", kManageRule);
		for (const auto& entry : entries) {
			const std::string status = entry.note.empty() ? entry.status : fmt::format("{} ({})", entry.status, entry.note);
			if (!entry.phrase.empty()) {
				const std::string action = Saylink::Silent(entry.phrase, fmt::format("[ {} ]", entry.button));
				const std::string line = multi ?
					fmt::format("   {} - {} {}", entry.name, status, action) :
					fmt::format("   {} {}", status, action);
				client.Message(Chat::NPCQuestSay, "%s", line.c_str());
				continue;
			}

			const std::string line = multi ?
				fmt::format("   {} - {}", entry.name, status) :
				fmt::format("   {}", status);
			const uint16_t chat_type = entry.status == "Locked" ? Chat::Red : Chat::Yellow;
			client.Message(chat_type, "%s", line.c_str());
		}
		client.Message(Chat::NPCQuestSay, "%s", kManageRule);
	}

	std::string BossEventName(const EventNpc& event_npc)
	{
		std::string boss_name = NpcTypeName(event_npc.npc_type_id);
		return boss_name.empty() ? kSimpleBossEventName : boss_name;
	}

	std::string RuntimeEventName(const Event& event_data, const EventNpc& event_npc)
	{
		if (Strings::EqualFold(event_data.event_name, kSimpleBossEventName)) {
			return BossEventName(event_npc);
		}

		return event_data.event_name;
	}

	bool IsLootProtected(const Event& event_data, const EventNpc& event_npc)
	{
		return event_data.loot_protected || event_npc.loot_protected;
	}

	void ApplyLootEventForNpc(DynamicZone& expedition, const Event& event_data, const EventNpc& event_npc, NPC& npc)
	{
		if (!IsLootProtected(event_data, event_npc) || !MatchNpc(event_npc, npc)) {
			return;
		}

		const std::string runtime_event_name = RuntimeEventName(event_data, event_npc);
		if (event_npc.spawn2_id != 0) {
			expedition.SetLootEvent(npc.GetID(), runtime_event_name, DzLootEvent::Type::Entity);
			return;
		}

		if (event_npc.npc_type_id != 0) {
			expedition.SetLootEvent(event_npc.npc_type_id, runtime_event_name, DzLootEvent::Type::NpcType);
		}
	}

	bool TryMarkRuntimeEventComplete(DynamicZone& expedition, const std::string& runtime_event_name)
	{
		return g_completed_runtime_events[expedition.GetID()].insert(runtime_event_name).second;
	}

	std::pair<std::string, uint32_t> ParseLockoutActionValue(const std::string& value, const std::string& default_event_name)
	{
		auto parts = Strings::Split(value, "|");
		if (parts.size() >= 2) {
			return { parts[0], Strings::ToUnsignedInt(parts[1]) };
		}

		return { default_event_name, Strings::ToUnsignedInt(value) };
	}

	void MessageMembers(DynamicZone& expedition, const std::string& message)
	{
		for (const auto& member : expedition.GetMembers()) {
			if (Client* client = entity_list.GetClientByCharID(member.id)) {
				client->Message(Chat::Yellow, "%s", message.c_str());
			}
		}
	}

	void AddReplayLockoutIfLonger(DynamicZone& expedition, uint32_t seconds)
	{
		if (seconds == 0) {
			return;
		}

		// The client has one replay-timer slot per expedition; event completions can extend it
		// but should not shorten it.
		const DzLockout proposed = DzLockout::Create(expedition.GetName(), DzLockout::ReplayTimer, seconds, expedition.GetUUID());
		const auto& lockouts = expedition.GetLockouts();
		const auto replay_timer = std::ranges::find_if(lockouts, [&](const DzLockout& lockout) {
			return lockout.IsReplay() && lockout.IsUUID(expedition.GetUUID());
		});

		if (replay_timer != lockouts.end() && replay_timer->GetExpireTime() >= proposed.GetExpireTime()) {
			return;
		}

		expedition.AddLockout(DzLockout::ReplayTimer, seconds);
	}

	bool HasCurrentExpeditionLockout(DynamicZone& expedition, const std::string& event_name)
	{
		const auto& lockouts = expedition.GetLockouts();
		return std::ranges::any_of(lockouts, [&](const DzLockout& lockout) {
			return lockout.IsEvent(event_name) && lockout.IsUUID(expedition.GetUUID()) && !lockout.IsExpired();
		});
	}

	void ExecuteActions(DynamicZone& expedition, const Event& event_data, const std::string& runtime_event_name)
	{
		for (const auto& action : event_data.actions) {
			if (Strings::EqualFold(action.action_type, "lock")) {
				expedition.SetLocked(true, true);
			}
			else if (Strings::EqualFold(action.action_type, "unlock")) {
				expedition.SetLocked(false, true);
			}
			else if (Strings::EqualFold(action.action_type, "add_lockout")) {
				const auto [event_name, seconds] = ParseLockoutActionValue(action.action_value, runtime_event_name);
				if (!event_name.empty() && seconds > 0) {
					expedition.AddLockout(event_name, seconds);
				}
			}
			else if (Strings::EqualFold(action.action_type, "add_replay_lockout")) {
				const uint32_t seconds = Strings::ToUnsignedInt(action.action_value);
				AddReplayLockoutIfLonger(expedition, seconds);
			}
			else if (Strings::EqualFold(action.action_type, "depop_npc_type")) {
				const uint32_t npc_type_id = Strings::ToUnsignedInt(action.action_value);
				if (npc_type_id > 0) {
					entity_list.DepopAll(npc_type_id, false);
				}
			}
			else if (Strings::EqualFold(action.action_type, "message_members")) {
				MessageMembers(expedition, action.action_value);
			}
			else if (Strings::EqualFold(action.action_type, "set_remaining")) {
				const uint32_t seconds = Strings::ToUnsignedInt(action.action_value);
				if (seconds > 0) {
					expedition.SetSecondsRemaining(seconds);
				}
			}
		}
	}

	bool CompleteEventForNpc(DynamicZone& expedition, const Event& event_data, const EventNpc& event_npc, Client* notifier)
	{
		const std::string runtime_event_name = RuntimeEventName(event_data, event_npc);
		if (HasCurrentExpeditionLockout(expedition, runtime_event_name) || !TryMarkRuntimeEventComplete(expedition, runtime_event_name)) {
			return false;
		}

		if (event_data.lock_on_success && event_data.lockout_seconds > 0) {
			expedition.AddLockout(runtime_event_name, event_data.lockout_seconds);
		}

		if (event_data.replay_lockout_seconds > 0) {
			AddReplayLockoutIfLonger(expedition, event_data.replay_lockout_seconds);
		}

		ExecuteActions(expedition, event_data, runtime_event_name);

		if (notifier) {
			notifier->Message(Chat::Yellow, fmt::format("Expedition event complete: {}", runtime_event_name).c_str());
		}

		return true;
	}
}

std::string ValidationResult::StatusName() const
{
	if (!errors.empty()) {
		return "invalid";
	}

	if (!warnings.empty()) {
		return "valid_with_warnings";
	}

	return "valid";
}

std::string NormalizePhrase(const std::string& phrase)
{
	std::string out = phrase;
	Strings::Trim(out);
	std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out.empty() ? "expedition" : out;
}

uint32_t CreateTemplateFromClient(Database& db, Client& client, const std::string& name)
{
	if (!zone || name.empty()) {
		return 0;
	}

	const glm::vec4 safe = ZoneStore::Instance()->GetZoneSafeCoordinates(zone->GetZoneID(), zone->GetInstanceVersion());
	const std::string escaped_name = Escape(name);
	const auto& pos = client.GetPosition();

	const uint32_t dz_template_id = InsertID(db, fmt::format(
		"INSERT INTO dynamic_zone_templates "
		"(zone_id, zone_version, name, min_players, max_players, duration_seconds, dz_switch_id, "
		"compass_zone_id, compass_x, compass_y, compass_z, return_zone_id, return_x, return_y, return_z, return_h, "
		"override_zone_in, zone_in_x, zone_in_y, zone_in_z, zone_in_h) "
		"VALUES ({}, {}, '{}', {}, {}, {}, 0, {}, {}, {}, {}, {}, {}, {}, {}, {}, 1, {}, {}, {}, {})",
		zone->GetZoneID(),
		zone->GetInstanceVersion(),
		escaped_name,
		kSimpleSetupMinPlayers,
		kSimpleSetupMaxPlayers,
		kSimpleSetupDurationSeconds,
		zone->GetZoneID(),
		pos.x,
		pos.y,
		pos.z,
		zone->GetZoneID(),
		safe.x,
		safe.y,
		safe.z,
		safe.w,
		pos.x,
		pos.y,
		pos.z,
		pos.w
	));

	if (!dz_template_id) {
		return 0;
	}

	const uint32_t template_id = ExpeditionRepository::InsertTemplate(
		db,
		dz_template_id,
		name,
		UniqueSlug(name),
		false,
		kSimpleSetupReplaySeconds,
		true,
		false,
		false,
		kSimpleRequestPhrase,
		kSimpleRequestMode,
		""
	);

	if (!template_id) {
		DynamicZoneTemplatesRepository::DeleteOne(db, dz_template_id);
		return 0;
	}

	Reload(db);
	return template_id;
}

uint32_t CloneTemplate(Database& db, uint32_t source_template_id, const std::string& name)
{
	const Template* source = FindTemplate(source_template_id);
	if (!source || name.empty()) {
		return 0;
	}

	const std::string escaped_name = Escape(name);
	const std::string slug = fmt::format("{}_{}_{}", Slugify(name), source_template_id, static_cast<uint32_t>(std::time(nullptr)));
	const uint32_t dz_template_id = InsertID(db, fmt::format(
		"INSERT INTO dynamic_zone_templates "
		"(zone_id, zone_version, name, min_players, max_players, duration_seconds, dz_switch_id, "
		"compass_zone_id, compass_x, compass_y, compass_z, return_zone_id, return_x, return_y, return_z, return_h, "
		"override_zone_in, zone_in_x, zone_in_y, zone_in_z, zone_in_h) "
		"SELECT zone_id, zone_version, '{}', min_players, max_players, duration_seconds, dz_switch_id, "
		"compass_zone_id, compass_x, compass_y, compass_z, return_zone_id, return_x, return_y, return_z, return_h, "
		"override_zone_in, zone_in_x, zone_in_y, zone_in_z, zone_in_h "
		"FROM dynamic_zone_templates WHERE id = {}",
		escaped_name,
		source->dz_template_id
	));

	if (!dz_template_id) {
		return 0;
	}

	const uint32_t template_id = ExpeditionRepository::InsertTemplateFrom(db, source_template_id, dz_template_id, name, slug);

	if (!template_id) {
		DynamicZoneTemplatesRepository::DeleteOne(db, dz_template_id);
		return 0;
	}

	ExpeditionRepository::CopyRequestNpcs(db, source_template_id, template_id);

	for (const auto& event_data : source->events) {
		const uint32_t event_id = ExpeditionRepository::InsertEvent(
			db,
			template_id,
			event_data.event_name,
			event_data.lockout_seconds,
			event_data.replay_lockout_seconds,
			event_data.lock_on_success,
			event_data.lock_on_failure,
			event_data.loot_protected,
			event_data.sort_order
		);

		if (!event_id) {
			continue;
		}

		ExpeditionRepository::CopyEventNpcs(db, event_data.id, event_id);
		ExpeditionRepository::CopyActions(db, event_data.id, event_id);
	}

	Reload(db);
	return template_id;
}

bool DeleteTemplate(Database& db, uint32_t template_id)
{
	const Template* template_data = FindTemplate(template_id);
	if (!template_data) {
		return false;
	}

	ExpeditionRepository::DeleteTemplate(db, template_id);
	DynamicZoneTemplatesRepository::DeleteOne(db, template_data->dz_template_id);
	Reload(db);
	return true;
}

bool SetTemplateName(Database& db, uint32_t template_id, const std::string& name)
{
	const Template* template_data = FindTemplate(template_id);
	if (!template_data || name.empty()) {
		return false;
	}

	const std::string slug = UniqueSlug(name, template_id);

	const bool template_ok = ExpeditionRepository::UpdateTemplateName(db, template_id, name, slug);
	const bool dz_ok = QueryOK(db, fmt::format(
		"UPDATE dynamic_zone_templates SET name = '{}' WHERE id = {}",
		Escape(name),
		template_data->dz_template_id
	));
	Reload(db);
	return template_ok && dz_ok;
}

bool SetTemplateEnabled(Database& db, uint32_t template_id, bool enabled)
{
	const bool ok = ExpeditionRepository::UpdateTemplateEnabled(db, template_id, enabled);
	Reload(db);
	return ok;
}

bool SetTemplateReplay(Database& db, uint32_t template_id, uint32_t seconds)
{
	const bool ok = ExpeditionRepository::UpdateTemplateReplay(db, template_id, seconds);
	Reload(db);
	return ok;
}

bool SetTemplateSilent(Database& db, uint32_t template_id, bool silent)
{
	const bool ok = ExpeditionRepository::UpdateTemplateSilent(db, template_id, silent);
	Reload(db);
	return ok;
}

bool SetTemplateBossOnlySpawn(Database& db, uint32_t template_id, bool enabled)
{
	const bool ok = ExpeditionRepository::UpdateTemplateBossOnlySpawn(db, template_id, enabled);
	Reload(db);
	return ok;
}

bool SetTemplateRequestMode(Database& db, uint32_t template_id, const std::string& request_mode)
{
	if (!IsKnownRequestMode(request_mode)) {
		return false;
	}

	const bool ok = ExpeditionRepository::UpdateTemplateRequestMode(db, template_id, NormalizeRequestMode(request_mode));
	Reload(db);
	return ok;
}

bool SetDzTemplateZone(Database& db, uint32_t dz_template_id, uint32_t zone_id, uint32_t version)
{
	const bool ok = QueryOK(db, fmt::format(
		"UPDATE dynamic_zone_templates SET zone_id = {}, zone_version = {} WHERE id = {}",
		zone_id,
		version,
		dz_template_id
	));
	Reload(db);
	return ok;
}

bool SetDzTemplateDuration(Database& db, uint32_t dz_template_id, uint32_t seconds)
{
	const bool ok = QueryOK(db, fmt::format("UPDATE dynamic_zone_templates SET duration_seconds = {} WHERE id = {}", seconds, dz_template_id));
	Reload(db);
	return ok;
}

bool SetDzTemplatePlayers(Database& db, uint32_t dz_template_id, uint32_t min_players, uint32_t max_players)
{
	const bool ok = QueryOK(db, fmt::format(
		"UPDATE dynamic_zone_templates SET min_players = {}, max_players = {} WHERE id = {}",
		min_players,
		max_players,
		dz_template_id
	));
	Reload(db);
	return ok;
}

bool SetDzTemplateZoneIn(Database& db, uint32_t dz_template_id, float x, float y, float z, float h)
{
	const bool ok = QueryOK(db, fmt::format(
		"UPDATE dynamic_zone_templates SET override_zone_in = 1, zone_in_x = {}, zone_in_y = {}, zone_in_z = {}, zone_in_h = {} WHERE id = {}",
		x,
		y,
		z,
		h,
		dz_template_id
	));
	Reload(db);
	return ok;
}

bool SetDzTemplateSafeReturn(Database& db, uint32_t dz_template_id, uint32_t zone_id, float x, float y, float z, float h)
{
	const bool ok = QueryOK(db, fmt::format(
		"UPDATE dynamic_zone_templates SET return_zone_id = {}, return_x = {}, return_y = {}, return_z = {}, return_h = {} WHERE id = {}",
		zone_id,
		x,
		y,
		z,
		h,
		dz_template_id
	));
	Reload(db);
	return ok;
}

bool SetDzTemplateCompass(Database& db, uint32_t dz_template_id, uint32_t zone_id, float x, float y, float z)
{
	const bool ok = QueryOK(db, fmt::format(
		"UPDATE dynamic_zone_templates SET compass_zone_id = {}, compass_x = {}, compass_y = {}, compass_z = {} WHERE id = {}",
		zone_id,
		x,
		y,
		z,
		dz_template_id
	));
	Reload(db);
	return ok;
}

bool SetDzTemplateSwitchID(Database& db, uint32_t dz_template_id, uint32_t switch_id)
{
	const bool ok = QueryOK(db, fmt::format("UPDATE dynamic_zone_templates SET dz_switch_id = {} WHERE id = {}", switch_id, dz_template_id));
	Reload(db);
	return ok;
}

uint32_t UpsertRequestNpc(Database& db, uint32_t template_id, uint32_t zone_id, uint32_t npc_type_id, uint32_t spawn2_id, const std::string& phrase, int32_t zone_version)
{
	const std::string normalized = NormalizePhrase(phrase);
	const uint32_t id = ExpeditionRepository::UpsertRequestNpc(
		db,
		template_id,
		zone_id,
		npc_type_id,
		spawn2_id,
		normalized,
		zone_version,
		true
	);
	Reload(db);
	return id;
}

bool DeleteRequestNpc(Database& db, uint32_t template_id, uint32_t zone_id, int32_t zone_version, uint32_t npc_type_id, uint32_t spawn2_id)
{
	const bool ok = ExpeditionRepository::DeleteRequestNpc(db, template_id, zone_id, zone_version, npc_type_id, spawn2_id);
	Reload(db);
	return ok;
}

uint32_t AddEvent(Database& db, uint32_t template_id, const std::string& event_name)
{
	const uint32_t id = ExpeditionRepository::InsertEvent(db, template_id, event_name, 0, 0, true, false, false, 0);
	Reload(db);
	return id;
}

bool DeleteEvent(Database& db, uint32_t event_id)
{
	const bool ok = ExpeditionRepository::DeleteEvent(db, event_id);
	Reload(db);
	return ok;
}

bool SetEventName(Database& db, uint32_t event_id, const std::string& event_name)
{
	if (event_name.empty()) {
		return false;
	}

	const bool ok = ExpeditionRepository::UpdateEventName(db, event_id, event_name);
	Reload(db);
	return ok;
}

bool SetEventLockout(Database& db, uint32_t event_id, uint32_t seconds)
{
	const bool ok = ExpeditionRepository::UpdateEventLockout(db, event_id, seconds);
	Reload(db);
	return ok;
}

bool SetEventReplay(Database& db, uint32_t event_id, uint32_t seconds)
{
	const bool ok = ExpeditionRepository::UpdateEventReplay(db, event_id, seconds);
	Reload(db);
	return ok;
}

bool SetEventNpc(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, const std::string& role)
{
	auto results = db.QueryDatabase(fmt::format(
		"SELECT id FROM expedition_template_event_npcs WHERE event_id = {} AND npc_type_id = {} AND spawn2_id = {} LIMIT 1",
		event_id,
		npc_type_id,
		spawn2_id
	));

	if (results.Success() && results.RowCount() == 1) {
		auto row = results.begin();
		const bool completes_on_death = Strings::EqualFold(role, "boss");
		const bool ok = QueryOK(db, fmt::format(
			"UPDATE expedition_template_event_npcs SET role = '{}', complete_on_death = {}, complete_on_spawn = 0 WHERE id = {}",
			Escape(role),
			completes_on_death ? 1 : 0,
			UInt(row[0])
		));
		Reload(db);
		return ok;
	}

	const bool completes_on_death = Strings::EqualFold(role, "boss");
	const bool ok = InsertID(db, fmt::format(
		"INSERT INTO expedition_template_event_npcs "
		"(event_id, npc_type_id, spawn2_id, role, complete_on_death, complete_on_spawn, loot_protected) "
		"VALUES ({}, {}, {}, '{}', {}, 0, 0)",
		event_id,
		npc_type_id,
		spawn2_id,
		Escape(role),
		completes_on_death ? 1 : 0
	)) != 0;
	Reload(db);
	return ok;
}

bool SetEventNpcLoot(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool enabled)
{
	const bool ok = ExpeditionRepository::UpdateEventNpcLoot(db, event_id, npc_type_id, spawn2_id, enabled);
	Reload(db);
	return ok;
}

bool SetEventNpcCompleteOnDeath(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool enabled)
{
	const bool ok = ExpeditionRepository::UpdateEventNpcCompleteOnDeath(db, event_id, npc_type_id, spawn2_id, enabled);
	Reload(db);
	return ok;
}

bool SetEventNpcCompleteOnSpawn(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool enabled)
{
	const bool ok = ExpeditionRepository::UpdateEventNpcCompleteOnSpawn(db, event_id, npc_type_id, spawn2_id, enabled);
	Reload(db);
	return ok;
}

bool DeleteEventNpc(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id)
{
	bool delete_event = false;
	auto results = db.QueryDatabase(fmt::format(
		"SELECT role FROM expedition_template_event_npcs WHERE event_id = {} AND npc_type_id = {} AND spawn2_id = {}",
		event_id,
		npc_type_id,
		spawn2_id
	));
	if (!results.Success()) {
		return false;
	}

	for (auto row = results.begin(); row != results.end(); ++row) {
		EventNpc event_npc;
		event_npc.role = row[0] ? row[0] : "";
		if (EventNpcRemovalDeletesEvent(event_npc)) {
			delete_event = true;
			break;
		}
	}

	const bool ok = delete_event ?
		ExpeditionRepository::DeleteEvent(db, event_id) :
		ExpeditionRepository::DeleteEventNpc(db, event_id, npc_type_id, spawn2_id);
	Reload(db);
	return ok;
}

uint32_t AddAction(Database& db, uint32_t event_id, const std::string& action_type, const std::string& action_value)
{
	const uint32_t id = ExpeditionRepository::InsertAction(db, event_id, action_type, action_value);
	Reload(db);
	return id;
}

bool ClearActions(Database& db, uint32_t event_id)
{
	const bool ok = ExpeditionRepository::DeleteActionsByEvent(db, event_id);
	Reload(db);
	return ok;
}

void Reload(Database& db)
{
	auto template_rows = ExpeditionRepository::LoadAllTemplates(db);
	if (!template_rows) {
		return;
	}

	auto request_npc_rows = ExpeditionRepository::LoadAllRequestNpcs(db);
	if (!request_npc_rows) {
		return;
	}

	auto event_rows = ExpeditionRepository::LoadAllEvents(db);
	if (!event_rows) {
		return;
	}

	auto event_npc_rows = ExpeditionRepository::LoadAllEventNpcs(db);
	if (!event_npc_rows) {
		return;
	}

	auto action_rows = ExpeditionRepository::LoadAllActions(db);
	if (!action_rows) {
		return;
	}

	std::unordered_map<uint32_t, Template> templates;
	for (auto& template_data : *template_rows) {
		template_data.request_phrase = NormalizePhrase(template_data.request_phrase);
		template_data.request_mode = IsKnownRequestMode(template_data.request_mode) ?
			NormalizeRequestMode(template_data.request_mode) : template_data.request_mode;
		template_data.dz_template = DynamicZoneTemplatesRepository::FindOne(db, template_data.dz_template_id);
		const uint32_t id = template_data.id;
		templates[id] = std::move(template_data);
	}

	for (auto& request_npc : *request_npc_rows) {
		request_npc.phrase = NormalizePhrase(request_npc.phrase);
		auto it = templates.find(request_npc.expedition_template_id);
		if (it != templates.end()) {
			it->second.request_npcs.push_back(request_npc);
		}
	}

	for (auto event_data : *event_rows) {
		auto it = templates.find(event_data.expedition_template_id);
		if (it != templates.end()) {
			it->second.events.push_back(event_data);
		}
	}

	for (const auto& event_npc : *event_npc_rows) {
		for (auto& [id, template_data] : templates) {
			auto event_it = std::ranges::find_if(template_data.events, [&](const Event& event_data) {
				return event_data.id == event_npc.event_id;
			});

			if (event_it != template_data.events.end()) {
				event_it->npcs.push_back(event_npc);
				break;
			}
		}
	}

	for (const auto& action : *action_rows) {
		for (auto& [id, template_data] : templates) {
			auto event_it = std::ranges::find_if(template_data.events, [&](const Event& event_data) {
				return event_data.id == action.event_id;
			});

			if (event_it != template_data.events.end()) {
				event_it->actions.push_back(action);
				break;
			}
		}
	}

	g_templates = std::move(templates);
}

const std::unordered_map<uint32_t, Template>& Templates()
{
	return g_templates;
}

const Template* FindTemplate(uint32_t template_id)
{
	auto it = g_templates.find(template_id);
	return it != g_templates.end() ? &it->second : nullptr;
}

const Template* FindTemplate(const std::string& id_or_name)
{
	if (id_or_name.empty()) {
		return nullptr;
	}

	if (Strings::IsNumber(id_or_name)) {
		return FindTemplate(Strings::ToUnsignedInt(id_or_name));
	}

	const Template* best_match = nullptr;
	for (const auto& [id, template_data] : g_templates) {
		if (
			Strings::EqualFold(template_data.name, id_or_name) ||
			Strings::EqualFold(template_data.slug, id_or_name) ||
			Strings::EqualFold(template_data.dz_template.name, id_or_name)
		) {
			if (
				!best_match ||
				(!best_match->enabled && template_data.enabled) ||
				(best_match->enabled == template_data.enabled && template_data.id < best_match->id)
			) {
				best_match = &template_data;
			}
		}
	}

	return best_match;
}

const Event* FindEvent(uint32_t event_id)
{
	for (const auto& [template_id, template_data] : g_templates) {
		auto it = std::ranges::find_if(template_data.events, [&](const Event& event_data) {
			return event_data.id == event_id;
		});

		if (it != template_data.events.end()) {
			return &*it;
		}
	}

	return nullptr;
}

const Event* FindEvent(const Template& template_data, const std::string& id_or_name)
{
	if (id_or_name.empty()) {
		return nullptr;
	}

	if (Strings::IsNumber(id_or_name)) {
		const uint32_t event_id = Strings::ToUnsignedInt(id_or_name);
		auto it = std::ranges::find_if(template_data.events, [&](const Event& event_data) { return event_data.id == event_id; });
		return it != template_data.events.end() ? &*it : nullptr;
	}

	auto it = std::ranges::find_if(template_data.events, [&](const Event& event_data) {
		return Strings::EqualFold(event_data.event_name, id_or_name);
	});
	return it != template_data.events.end() ? &*it : nullptr;
}

ValidationResult ValidateTemplate(const Template& template_data)
{
	ValidationResult result;
	if (template_data.dz_template_id == 0 || template_data.dz_template.id == 0) {
		result.errors.push_back("Missing linked dynamic_zone_templates row.");
	}

	if (template_data.dz_template.zone_id == 0 || !ZoneName(template_data.dz_template.zone_id)) {
		result.errors.push_back("Invalid expedition zone.");
	}

	if (template_data.dz_template.duration_seconds <= 0) {
		result.errors.push_back("Duration must be greater than zero.");
	}

	if (template_data.dz_template.min_players == 0 || template_data.dz_template.max_players < template_data.dz_template.min_players) {
		result.errors.push_back("Player minimum and maximum are invalid.");
	}

	if (
		template_data.request_mode != "db_only" &&
		template_data.request_mode != "script_can_opt_in" &&
		template_data.request_mode != "script_only"
	) {
		result.errors.push_back("Request mode must be db_only, script_can_opt_in, or script_only.");
	}

	if (template_data.request_mode == "db_only" && template_data.request_npcs.empty()) {
		result.errors.push_back("DB-only templates require at least one request NPC before publishing.");
	}

	for (const auto& request_npc : template_data.request_npcs) {
		if (request_npc.npc_type_id == 0) {
			result.errors.push_back("Request NPC entry is missing an NPC type.");
		}

		// DB request menus are additive after EVENT_SAY scripts run, so scripted
		// request NPCs are valid and keep their existing quest behavior.
	}

	if (template_data.request_mode == "script_only" && !template_data.request_npcs.empty()) {
		result.warnings.push_back("Request mode is script_only, so automatic DB request NPC mappings are ignored.");
	}

	if (template_data.request_mode == "script_can_opt_in" && !template_data.request_npcs.empty()) {
		result.warnings.push_back("Request mode is script_can_opt_in, so request NPC mappings are documentation only unless scripts call DB template APIs.");
	}

	if (template_data.boss_only_spawn) {
		bool has_concrete_boss = false;
		for (const auto& event_data : template_data.events) {
			for (const auto& event_npc : event_data.npcs) {
				if (Strings::EqualFold(event_npc.role, "boss") && event_npc.npc_type_id != 0 && event_npc.spawn2_id != 0) {
					has_concrete_boss = true;
				}
			}
		}

		if (!has_concrete_boss) {
			result.errors.push_back("Boss-only spawn mode requires at least one boss NPC mapping with a concrete spawn2_id.");
		}
	}

	for (const auto& [other_id, other_template] : g_templates) {
		if (
			other_id != template_data.id &&
			other_template.enabled &&
			SharesExpeditionLockoutNamespace(template_data, other_template)
		) {
			if (
				other_template.dz_template.zone_id == template_data.dz_template.zone_id &&
				other_template.dz_template.zone_version == template_data.dz_template.zone_version
			) {
				result.errors.push_back(fmt::format(
					"Another enabled DB expedition template [{}] shares this dynamic-zone name and zone/version; runtime hooks may attach to the wrong template and lockout timers can overwrite each other.",
					other_template.name
				));
			}
			else {
				result.errors.push_back(fmt::format(
					"Another enabled DB expedition template [{}] shares dynamic-zone name [{}]; character expedition lockouts are keyed by expedition name and event name, so timers can overwrite each other.",
					other_template.name,
					template_data.dz_template.name
				));
			}
		}
	}

	std::unordered_map<std::string, size_t> event_lockout_names;
	std::unordered_set<std::string> duplicate_event_lockout_names;
	auto remember_event_lockout_name = [&](size_t event_index, const std::string& event_name) {
		if (event_name.empty()) {
			return;
		}

		const std::string key = Strings::ToLower(event_name);
		auto [it, inserted] = event_lockout_names.try_emplace(key, event_index);
		if (!inserted && it->second != event_index && duplicate_event_lockout_names.insert(key).second) {
			result.errors.push_back(fmt::format(
				"Event lockout name [{}] is produced by multiple events; expedition timers are keyed by event name and would overwrite each other.",
				event_name
			));
		}
	};

	size_t event_index = 0;
	for (const auto& event_data : template_data.events) {
		if (event_data.event_name.empty()) {
			result.errors.push_back("Event is missing a name.");
		}

		if (event_data.lockout_seconds == 0 && event_data.lock_on_success) {
			result.errors.push_back(fmt::format("Event [{}] has no lockout duration.", event_data.event_name));
		}

		const bool has_loot_protection = event_data.loot_protected || std::ranges::any_of(event_data.npcs, [](const EventNpc& npc) { return npc.loot_protected; });
		if (has_loot_protection && event_data.npcs.empty()) {
			result.errors.push_back(fmt::format("Loot-protected event [{}] has no mapped NPCs.", event_data.event_name));
		}

		for (const auto& event_npc : event_data.npcs) {
			if (event_npc.npc_type_id == 0) {
				result.errors.push_back(fmt::format("Event [{}] has an NPC mapping without an NPC type.", event_data.event_name));
			}

			if (event_npc.spawn2_id == 0 && !event_npc.complete_on_spawn) {
				result.warnings.push_back(fmt::format("Event [{}] maps NPC [{}] without a spawn-specific id.", event_data.event_name, NpcTypeLabel(event_npc.npc_type_id)));
			}

			if (
				parse &&
				(parse->HasQuestSub(event_npc.npc_type_id, EVENT_DEATH) || parse->HasQuestSub(event_npc.npc_type_id, EVENT_DEATH_COMPLETE))
			) {
				result.warnings.push_back(fmt::format("Event NPC [{}] already has a death quest script; verify DB event actions do not duplicate scripted behavior.", NpcTypeLabel(event_npc.npc_type_id)));
			}
		}

		std::unordered_set<std::string> runtime_names;
		for (const auto& event_npc : event_data.npcs) {
			if (event_npc.complete_on_death || event_npc.complete_on_spawn) {
				runtime_names.insert(RuntimeEventName(event_data, event_npc));
			}
		}

		if (runtime_names.empty()) {
			runtime_names.insert(event_data.event_name);
		}

		for (const auto& runtime_name : runtime_names) {
			remember_event_lockout_name(event_index, runtime_name);
		}
		++event_index;
	}

	for (const auto& event_data : template_data.events) {
		for (const auto& event_npc : event_data.npcs) {
			if (!event_npc.complete_on_spawn || event_npc.npc_type_id == 0) {
				continue;
			}

			uint32_t duplicate_count = 0;
			for (const auto& other_event : template_data.events) {
				for (const auto& other_npc : other_event.npcs) {
					if (other_npc.complete_on_spawn && other_npc.npc_type_id == event_npc.npc_type_id) {
						duplicate_count++;
					}
				}
			}

			if (duplicate_count > 1) {
				result.warnings.push_back(fmt::format(
					"Dynamic spawn-completion NPC [{}] appears in multiple events; use a unique chest NPC type per event when spawn IDs are unavailable.",
					NpcTypeLabel(event_npc.npc_type_id)
				));
			}
		}
	}

	if (parse && (parse->HasQuestSub(ZONE_CONTROLLER_NPC_ID, EVENT_DEATH_ZONE) || parse->ZoneHasQuestSub(EVENT_DEATH_ZONE))) {
		result.warnings.push_back("This zone has death event scripts; verify DB event actions do not duplicate scripted behavior.");
	}

	if (template_data.replay_lockout_seconds == 0) {
		result.warnings.push_back("No replay lockout is configured.");
	}

	result.status = result.errors.empty() ?
		(result.warnings.empty() ? ValidationResult::Status::Valid : ValidationResult::Status::ValidWithWarnings) :
		ValidationResult::Status::Invalid;

	return result;
}

DynamicZone* CreateExpeditionFromTemplate(Client& client, const Template& template_data, bool allow_disabled)
{
	if (!allow_disabled && !template_data.enabled) {
		return nullptr;
	}

	DynamicZone dz = BuildDynamicZone(template_data);
	ExpeditionCreationOptions options;
	options.silent = template_data.silent;
	options.has_replay_on_join = true;
	options.replay_on_join = template_data.replay_on_join;
	options.has_replay_lockout = template_data.replay_lockout_seconds > 0;
	options.replay_lockout_seconds = template_data.replay_lockout_seconds;

	DynamicZone* expedition = CreateExpeditionWithOptions(client, dz, options);
	if (expedition) {
		ApplyLootEvents(*expedition);
	}

	return expedition;
}

DynamicZone* CreateExpeditionFromTemplate(Client& client, const Template& template_data)
{
	return CreateExpeditionFromTemplate(client, template_data, false);
}

DynamicZone* CreateExpeditionFromTemplate(Client& client, const std::string& id_or_name)
{
	const Template* template_data = FindTemplate(id_or_name);
	if (!template_data) {
		return nullptr;
	}

	return CreateExpeditionFromTemplate(client, *template_data);
}

bool CanCreateExpeditionFromTemplate(Client& client, const Template& template_data, bool allow_disabled)
{
	return CheckExpeditionFromTemplate(client, template_data, allow_disabled).success;
}

bool CanCreateExpeditionFromTemplate(Client& client, const Template& template_data)
{
	return CanCreateExpeditionFromTemplate(client, template_data, false);
}

ExpeditionCheckResult CheckExpeditionFromTemplate(Client& client, const Template& template_data, bool allow_disabled)
{
	ExpeditionCheckResult result;
	if (!allow_disabled && !template_data.enabled) {
		result.reason = "template_disabled";
		return result;
	}

	DynamicZone dz = BuildDynamicZone(template_data);
	return CheckExpeditionRequest(client, dz, true);
}

ExpeditionCheckResult CheckExpeditionFromTemplate(Client& client, const Template& template_data)
{
	return CheckExpeditionFromTemplate(client, template_data, false);
}

ExpeditionCheckResult CheckExpeditionFromTemplate(Client& client, const std::string& id_or_name)
{
	const Template* template_data = FindTemplate(id_or_name);
	if (template_data) {
		return CheckExpeditionFromTemplate(client, *template_data);
	}

	ExpeditionCheckResult result;
	result.reason = "template_not_found";
	return result;
}

bool HandleRequestSay(Client& client, NPC& npc, const std::string& message)
{
	if (!zone) {
		return false;
	}

	const std::string phrase = CommandWord(message);
	const bool is_hail = IsHailMessage(message);
	const uint32_t menu_template_id = RequestMenuTemplateId(phrase);
	const uint32_t enter_template_id = EnterMenuTemplateId(phrase);
	const uint32_t leave_template_id = LeaveMenuTemplateId(phrase);
	RequesterMatches matches;

	for (const auto& [template_id, template_data] : g_templates) {
		if (!template_data.enabled) {
			continue;
		}

		if (template_data.request_mode != "db_only") {
			continue;
		}

		const uint32_t current_template_id = template_id;
		for (const auto& request_npc : template_data.request_npcs) {
			const int specificity = request_npc.enabled ? RequestNpcSpecificity(request_npc, npc) : -1;
			if (specificity < 0) {
				continue;
			}

			auto existing = std::ranges::find_if(matches, [&](const RequesterMatch& match) {
				return match.template_data && match.template_data->id == current_template_id;
			});
			if (existing == matches.end()) {
				matches.push_back({ &template_data, &request_npc, specificity });
			}
			else {
				if (specificity > existing->specificity) {
					*existing = { &template_data, &request_npc, specificity };
				}
			}
		}
	}

	SortRequesterMatches(matches);

	if (!is_hail) {
		for (const auto& match : matches) {
			const Template* template_data = match.template_data;
			const RequestNpc* request_npc = match.request_npc;
			if (!template_data || !request_npc) {
				continue;
			}

			if (menu_template_id != 0) {
				if (menu_template_id == template_data->id) {
					TryCreateTemplateRequest(client, *template_data);
					return true;
				}
				continue;
			}

			if (enter_template_id != 0) {
				if (enter_template_id == template_data->id) {
					TryEnterTemplateRequest(client, *template_data);
					return true;
				}
				continue;
			}

			if (leave_template_id != 0) {
				if (leave_template_id == template_data->id) {
					TryLeaveTemplateRequest(client, *template_data);
					return true;
				}

				continue;
			}
		}

		RequesterMatches phrase_matches;
		for (const auto& match : matches) {
			const Template* template_data = match.template_data;
			const RequestNpc* request_npc = match.request_npc;
			if (!template_data || !request_npc) {
				continue;
			}

			const std::string request_phrase = RequestPhrase(*template_data, *request_npc);
			if (phrase == request_phrase || phrase == fmt::format("start {}", request_phrase)) {
				phrase_matches.push_back(match);
			}
		}

		if (phrase_matches.size() > 1) {
			OfferRequestMenu(client, phrase_matches);
			return false;
		}

		if (phrase_matches.size() == 1 && phrase_matches.front().template_data) {
			TryCreateTemplateRequest(client, *phrase_matches.front().template_data);
			return false;
		}
	}

	if (is_hail) {
		OfferRequestMenu(client, matches);
		return false;
	}

	return false;
}

bool HandleNpcDeath(NPC& npc, Client* killer)
{
	if (!zone) {
		return false;
	}

	DynamicZone* expedition = zone->GetDynamicZone();
	if (!expedition || !expedition->IsExpedition()) {
		return false;
	}

	const Template* template_data = FindTemplateByDz(*expedition);
	if (!template_data) {
		return false;
	}

	bool handled = false;
	for (const auto& event_data : template_data->events) {
		for (const auto& event_npc : event_data.npcs) {
			if (!event_npc.complete_on_death || !MatchNpc(event_npc, npc)) {
				continue;
			}

			handled = CompleteEventForNpc(*expedition, event_data, event_npc, killer) || handled;
		}
	}

	return handled;
}

bool HandleNpcSpawn(NPC& npc)
{
	if (!zone) {
		return false;
	}

	DynamicZone* expedition = zone->GetDynamicZone();
	if (!expedition || !expedition->IsExpedition()) {
		return false;
	}

	const Template* template_data = FindTemplateByDz(*expedition);
	if (!template_data) {
		return false;
	}

	bool handled = false;
	for (const auto& event_data : template_data->events) {
		for (const auto& event_npc : event_data.npcs) {
			if (!MatchNpc(event_npc, npc)) {
				continue;
			}

			ApplyLootEventForNpc(*expedition, event_data, event_npc, npc);
			if (event_npc.complete_on_spawn) {
				handled = CompleteEventForNpc(*expedition, event_data, event_npc, nullptr) || handled;
			}
		}
	}

	return handled;
}

BossOnlySpawnFilter GetBossOnlySpawnFilter(DynamicZone* expedition)
{
	if (!expedition || !expedition->IsExpedition()) {
		return {};
	}

	const Template* template_data = FindTemplateByDz(*expedition);
	return template_data ? BuildBossOnlySpawnFilter(*template_data) : BossOnlySpawnFilter{};
}

void ApplyRequesterLastName(NPC& npc)
{
	if (!zone) {
		return;
	}

	// Any NPC configured as a requester (in this zone/version) gets the "Expeditions" surname so
	// players can spot who to hail. Checked against every template, regardless of publish state.
	for (const auto& [template_id, template_data] : g_templates) {
		for (const auto& request_npc : template_data.request_npcs) {
			if (MatchRequestNpc(request_npc, npc)) {
				if (!Strings::EqualFold(npc.GetLastName(), kRequesterLastName)) {
					npc.ChangeLastName(kRequesterLastName);
				}
				return;
			}
		}
	}

	// No longer a requester -> clear only our own surname, never an unrelated one.
	if (Strings::EqualFold(npc.GetLastName(), kRequesterLastName)) {
		npc.ClearLastName();
	}
}

namespace {
	void SendManageCard(Client& client, const std::string& title)
	{
		client.Message(Chat::Yellow, kManageRule);
		client.Message(Chat::Yellow, fmt::format("   {}", title).c_str());
		client.Message(Chat::Yellow, kManageRule);
	}

	void SendManageRow(Client& client, const std::vector<std::pair<std::string, std::string>>& actions)
	{
		if (actions.empty()) {
			return;
		}
		std::string line = "  ";
		for (size_t i = 0; i < actions.size(); ++i) {
			if (i != 0) {
				line += "   ";
			}
			line += Saylink::Silent(actions[i].first, fmt::format("[ {} ]", actions[i].second));
		}
		client.Message(Chat::White, line.c_str());
	}
}

void MaybeShowGmTargetMenu(Client& client, NPC& npc)
{
	if (!client.GetGM()) {
		return;
	}

	const uint16_t entity_id = npc.GetID();
	auto last_it = g_last_gm_target_menu_entity.find(client.CharacterID());
	if (last_it != g_last_gm_target_menu_entity.end() && last_it->second == entity_id) {
		return;
	}
	g_last_gm_target_menu_entity[client.CharacterID()] = entity_id;

	const auto& builder_state = GetBuilderState(client.CharacterID());
	const auto* selected_template = FindTemplate(builder_state.selected_template_id);

	// Edit mode: targeting an NPC that is NOT yet part of the selected expedition opens the guided
	// "add" dialog (step 1 of 3). If the NPC is already part of the expedition, fall through to its
	// management options instead of prompting to re-add it.
	if (builder_state.edit_mode && selected_template) {
		client.Message(Chat::Magenta, fmt::format(
			">>>>>  EDIT MODE  --  editing: {} [{}] ({})  <<<<<",
			selected_template->name, selected_template->id, selected_template->enabled ? "published" : "draft"
		).c_str());

		bool mapped_event_npc = false;
		for (const auto& event_data : selected_template->events) {
			for (const auto& event_npc : event_data.npcs) {
				if (MatchNpc(event_npc, npc)) {
					mapped_event_npc = true;
					break;
				}
			}
			if (mapped_event_npc) {
				break;
			}
		}

		const RequestNpc* mapped_request_npc = nullptr;
		for (const auto& request_npc : selected_template->request_npcs) {
			if (MatchRequestNpc(request_npc, npc)) {
				mapped_request_npc = &request_npc;
				break;
			}
		}

		if (!mapped_event_npc && !mapped_request_npc) {
			// Guard: bosses/requesters only trigger inside the expedition's own zone. Warn on mismatch.
			const uint32_t exp_zone = selected_template->dz_template.zone_id;
			if (exp_zone != 0 && exp_zone != zone->GetZoneID()) {
				client.Message(Chat::Red, fmt::format(
					"WARNING: this NPC is in {} but [{}] runs in {}. NPCs only trigger inside the expedition's own zone - adding it here likely won't work.",
					ZoneLongName(zone->GetZoneID()), selected_template->name, ZoneLongName(exp_zone)
				).c_str());
			}

			// Not part of the expedition yet -> start the sequential per-role add flow with the first
			// question (Boss?). Yes adds as boss; No advances to the Chest question, then Requester.
			const std::string body = fmt::format(
				"Edit mode is ON for {}.<br><br>"
				"Add {} (NPC type {}) as a Boss?<br><br>"
				"A Boss completes its event when defeated. Choose No to consider other roles (Chest, Requester).",
				selected_template->name, npc.GetCleanName(), npc.GetNPCTypeID()
			);
			client.SendFullPopup(
				"Add NPC - Boss?", body.c_str(),
				ExpeditionEditPopup::Make(ExpeditionEditPopup::AddBoss, entity_id),
				ExpeditionEditPopup::Make(ExpeditionEditPopup::AskChest, entity_id),
				1, 0, "Yes", "No"
			);
			return;
		}

		if (mapped_request_npc && !mapped_event_npc) {
			// Already a request NPC -> show requester management as a card.
			SendManageCard(client, fmt::format("Requester: {} ({})", npc.GetCleanName(), npc.GetNPCTypeID()));
			client.Message(Chat::White, fmt::format("  Part of [{}] - players hail/say \"{}\" here to start the expedition.",
				selected_template->name, mapped_request_npc->phrase).c_str());
			SendManageRow(client, {
				{"#expedition request remove confirm", "Remove from Expedition"},
				{"#expedition request list", "List Requesters"},
				{"#expedition", "Back to List"}
			});
			// Pre-fill the current phrase so it can be edited in the input box, then re-sent.
			client.Message(Chat::White, fmt::format("  Change phrase:  {}",
				Saylink::Create(fmt::format("#expedition request add \"{}\"", mapped_request_npc->phrase), false, "[ Change Phrase ]")).c_str());
			client.Message(Chat::Yellow, kManageRule);
			return;
		}

		// Otherwise the NPC is mapped as an event NPC -> fall through to the event options menu below.
	}

	std::vector<const Template*> candidates;
	if (selected_template) {
		candidates.push_back(selected_template);
	}

	if (DynamicZone* expedition = zone ? zone->GetDynamicZone() : nullptr) {
		if (expedition->IsExpedition()) {
			if (const auto* runtime_template = FindTemplateByDz(*expedition)) {
				if (std::ranges::find(candidates, runtime_template) == candidates.end()) {
					candidates.push_back(runtime_template);
				}
			}
		}
	}

	for (const auto* template_data : candidates) {
		if (!template_data) {
			continue;
		}

		for (const auto& event_data : template_data->events) {
			for (const auto& event_npc : event_data.npcs) {
				if (!MatchNpc(event_npc, npc)) {
					continue;
				}

				const bool is_chest = Strings::EqualFold(event_npc.role, "chest") || event_npc.complete_on_spawn;
				const std::string role_label = is_chest ? "Chest" : "Boss";
				const std::string trigger =
					event_npc.complete_on_spawn ? "spawn" :
					(event_npc.complete_on_death ? "death" : "manual");
				const bool loot_protected = event_data.loot_protected || event_npc.loot_protected;

				const bool can_update_selection = !selected_template || selected_template->id == template_data->id;
				if (!can_update_selection) {
					SendManageCard(client, fmt::format("{}: {} ({})", role_label, npc.GetCleanName(), npc.GetNPCTypeID()));
					client.Message(Chat::White, fmt::format("  Belongs to [{}], but you're currently working on [{}].",
						template_data->name, selected_template->name).c_str());
					SendManageRow(client, {
						{fmt::format("#expedition edit on {}", template_data->id), "Edit This Expedition"},
						{"#expedition", "Back to List"}
					});
					client.Message(Chat::Yellow, kManageRule);
					return;
				}

				SetSelectedTemplate(client.CharacterID(), template_data->id);
				SetSelectedEvent(client.CharacterID(), event_data.id);

				SendManageCard(client, fmt::format("{}: {} ({})", role_label, npc.GetCleanName(), npc.GetNPCTypeID()));
				client.Message(Chat::White, fmt::format("  Event [{}]   -   trigger: {}   -   lockout: {}   -   loot protection: {}",
					event_data.event_name, trigger, Strings::SecondsToTime(event_data.lockout_seconds), loot_protected ? "on" : "off").c_str());
				SendManageRow(client, {
					{fmt::format("#expedition event loot {}", loot_protected ? "off" : "on"), loot_protected ? "Loot Protection: Off" : "Loot Protection: On"},
					{"#expedition event lockout", "Set Lockout..."},
					{"#expedition event npc remove confirm", EventNpcRemovalDeletesEvent(event_npc) ? "Remove Boss/Event" : "Remove from Expedition"}
				});
				SendManageRow(client, {
					{fmt::format("#expedition event completeondeath {}", event_npc.complete_on_death ? "off" : "on"), event_npc.complete_on_death ? "Death Trigger Off" : "Death Trigger On"},
					{fmt::format("#expedition event completeonspawn {}", event_npc.complete_on_spawn ? "off" : "on"), event_npc.complete_on_spawn ? "Spawn Trigger Off" : "Spawn Trigger On"}
				});
				// Pre-fill the event name so it can be edited in the input box, then re-sent.
				client.Message(Chat::White, fmt::format("  Rename event:  {}",
					Saylink::Create(fmt::format("#expedition event rename {} \"{}\"", event_data.id, event_data.event_name), false, "[ Rename Event ]")).c_str());
				client.Message(Chat::Yellow, kManageRule);
				return;
			}
		}
	}
}

void ApplyLootEvents(DynamicZone& expedition)
{
	if (!expedition.IsCurrentZoneDz()) {
		return;
	}

	const Template* template_data = FindTemplateByDz(expedition);
	if (!template_data) {
		return;
	}

	for (const auto& event_data : template_data->events) {
		for (const auto& event_npc : event_data.npcs) {
			if (!IsLootProtected(event_data, event_npc)) {
				continue;
			}

			if (event_npc.spawn2_id != 0) {
				if (NPC* npc = entity_list.GetNPCBySpawnID(event_npc.spawn2_id)) {
					ApplyLootEventForNpc(expedition, event_data, event_npc, *npc);
				}
				continue;
			}

			if (event_npc.npc_type_id != 0) {
				expedition.SetLootEvent(event_npc.npc_type_id, RuntimeEventName(event_data, event_npc), DzLootEvent::Type::NpcType);
			}
		}
	}
}

void ResetTargetMenu(uint32_t character_id)
{
	// Clears the "last NPC a target-menu was shown for" dedupe so re-targeting the same NPC
	// (e.g. right after adding it in edit mode) will render its menu again.
	g_last_gm_target_menu_entity.erase(character_id);
}

void ClearRuntimeEventState(uint32_t dz_id)
{
	if (dz_id != 0) {
		g_completed_runtime_events.erase(dz_id);
	}
}

BuilderState& GetBuilderState(uint32_t character_id)
{
	return g_builder_states[character_id];
}

void SetSelectedTemplate(uint32_t character_id, uint32_t template_id)
{
	auto& state = GetBuilderState(character_id);
	state.selected_template_id = template_id;
	state.selected_event_id = 0;
}

void SetSelectedEvent(uint32_t character_id, uint32_t event_id)
{
	GetBuilderState(character_id).selected_event_id = event_id;
}

std::string RoleFromTarget(NPC& npc)
{
	if (npc.GetLevel() > 1) {
		return "boss";
	}

	return "chest";
}

} // namespace ExpeditionDB
