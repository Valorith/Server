#include "expedition_db.h"

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

namespace ExpeditionDB {
namespace {
	std::unordered_map<uint32_t, Template> g_templates;
	std::unordered_map<uint32_t, BuilderState> g_builder_states;
	std::unordered_map<uint32_t, uint16_t> g_last_gm_target_menu_entity;

	bool Truthy(const char* value)
	{
		return value && Strings::ToBool(value);
	}

	uint32_t UInt(const char* value)
	{
		return value ? static_cast<uint32_t>(strtoul(value, nullptr, 10)) : 0;
	}

	int32_t Int(const char* value)
	{
		return value ? static_cast<int32_t>(atoi(value)) : 0;
	}

	float Float(const char* value)
	{
		return value ? strtof(value, nullptr) : 0.0f;
	}

	std::string Text(const char* value)
	{
		return value ? value : "";
	}

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

	std::vector<RequestNpc> LoadRequestNpcs(Database& db)
	{
		std::vector<RequestNpc> out;
		auto results = db.QueryDatabase(
			"SELECT id, expedition_template_id, zone_id, zone_version, npc_type_id, spawn2_id, phrase, enabled "
			"FROM expedition_template_request_npcs"
		);

		if (!results.Success()) {
			return out;
		}

		for (auto row = results.begin(); row != results.end(); ++row) {
			RequestNpc e;
			e.id = UInt(row[0]);
			e.expedition_template_id = UInt(row[1]);
			e.zone_id = UInt(row[2]);
			e.zone_version = Int(row[3]);
			e.npc_type_id = UInt(row[4]);
			e.spawn2_id = UInt(row[5]);
			e.phrase = NormalizePhrase(Text(row[6]));
			e.enabled = Truthy(row[7]);
			out.push_back(e);
		}

		return out;
	}

	std::vector<Event> LoadEvents(Database& db)
	{
		std::vector<Event> out;
		auto results = db.QueryDatabase(
			"SELECT id, expedition_template_id, event_name, lockout_seconds, replay_lockout_seconds, "
			"lock_on_success, lock_on_failure, loot_protected, sort_order "
			"FROM expedition_template_events ORDER BY expedition_template_id, sort_order, id"
		);

		if (!results.Success()) {
			return out;
		}

		for (auto row = results.begin(); row != results.end(); ++row) {
			Event e;
			e.id = UInt(row[0]);
			e.expedition_template_id = UInt(row[1]);
			e.event_name = Text(row[2]);
			e.lockout_seconds = UInt(row[3]);
			e.replay_lockout_seconds = UInt(row[4]);
			e.lock_on_success = Truthy(row[5]);
			e.lock_on_failure = Truthy(row[6]);
			e.loot_protected = Truthy(row[7]);
			e.sort_order = Int(row[8]);
			out.push_back(e);
		}

		return out;
	}

	std::vector<EventNpc> LoadEventNpcs(Database& db)
	{
		std::vector<EventNpc> out;
		auto results = db.QueryDatabase(
			"SELECT id, event_id, npc_type_id, spawn2_id, role, complete_on_death, complete_on_spawn, loot_protected "
			"FROM expedition_template_event_npcs"
		);

		if (!results.Success()) {
			return out;
		}

		for (auto row = results.begin(); row != results.end(); ++row) {
			EventNpc e;
			e.id = UInt(row[0]);
			e.event_id = UInt(row[1]);
			e.npc_type_id = UInt(row[2]);
			e.spawn2_id = UInt(row[3]);
			e.role = Text(row[4]);
			e.complete_on_death = Truthy(row[5]);
			e.complete_on_spawn = Truthy(row[6]);
			e.loot_protected = Truthy(row[7]);
			out.push_back(e);
		}

		return out;
	}

	std::vector<Action> LoadActions(Database& db)
	{
		std::vector<Action> out;
		auto results = db.QueryDatabase(
			"SELECT id, event_id, action_type, action_value, sort_order "
			"FROM expedition_template_actions ORDER BY event_id, sort_order, id"
		);

		if (!results.Success()) {
			return out;
		}

		for (auto row = results.begin(); row != results.end(); ++row) {
			Action e;
			e.id = UInt(row[0]);
			e.event_id = UInt(row[1]);
			e.action_type = Text(row[2]);
			e.action_value = Text(row[3]);
			e.sort_order = Int(row[4]);
			out.push_back(e);
		}

		return out;
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

	std::string CommandWord(const std::string& message)
	{
		auto text = NormalizePhrase(message);
		if (text.starts_with("hail ")) {
			text = text.substr(5);
		}

		return text;
	}

	bool IsHailMessage(const std::string& message)
	{
		const auto text = NormalizePhrase(message);
		return text.starts_with("hail");
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

		return "request unavailable";
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
		if (safe_return.zone_id == expedition->GetZoneID()) {
			client.MovePC(expedition->GetZoneID(), 0, safe_return.x, safe_return.y, safe_return.z, safe_return.heading);
			return true;
		}

		const glm::vec4 safe = ZoneStore::Instance()->GetZoneSafeCoordinates(expedition->GetZoneID(), expedition->GetZoneVersion());
		client.MovePC(expedition->GetZoneID(), 0, safe.x, safe.y, safe.z, safe.w, 0, ZoneMode::ZoneToSafeCoords);
		return true;
	}

	void OfferRequestMenu(Client& client, NPC& npc, const std::vector<std::pair<const Template*, const RequestNpc*>>& matches)
	{
		if (matches.empty()) {
			return;
		}

		const bool include_template_name = matches.size() > 1;
		DynamicZone* active_expedition = client.GetExpedition();
		std::vector<std::string> options;
		options.reserve(matches.size());
		for (const auto& [template_data, request_npc] : matches) {
			if (!template_data || !request_npc) {
				continue;
			}

			const std::string name_suffix = include_template_name ? fmt::format(" {}", template_data->name) : "";
			if (active_expedition) {
				if (IsMatchingExpedition(*active_expedition, *template_data)) {
					options.push_back(active_expedition->IsCurrentZoneDz() ?
						fmt::format("-> {}", Saylink::Silent(LeaveMenuPhrase(template_data->id), fmt::format("Leave Instance{}", name_suffix))) :
						fmt::format("-> {}", Saylink::Silent(EnterMenuPhrase(template_data->id), fmt::format("Enter Expedition{}", name_suffix))));
				}
				else {
					options.push_back(fmt::format("-> Already in another expedition"));
				}
				continue;
			}

			options.push_back(fmt::format(
				"-> {}",
				Saylink::Silent(RequestMenuPhrase(template_data->id), fmt::format("Create Expedition{}", name_suffix))
			));
		}

		if (options.empty()) {
			return;
		}

		client.Message(Chat::NPCQuestSay, "[Expedition Menu]");
		for (const auto& option : options) {
			client.Message(Chat::NPCQuestSay, "%s", option.c_str());
		}
	}

	std::string BossEventName(const EventNpc& event_npc)
	{
		std::string boss_name = NpcTypeName(event_npc.npc_type_id);
		return boss_name.empty() ? kSimpleBossEventName : fmt::format("{} Defeated", boss_name);
	}

	std::string RuntimeEventName(const Event& event_data, const EventNpc& event_npc)
	{
		if (Strings::EqualFold(event_data.event_name, kSimpleBossEventName)) {
			return BossEventName(event_npc);
		}

		return event_data.event_name;
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
				if (seconds > 0) {
					expedition.AddLockout(DzLockout::ReplayTimer, seconds);
				}
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
		if (expedition.HasLockout(runtime_event_name)) {
			return false;
		}

		bool handled = false;
		if (event_data.lock_on_success && event_data.lockout_seconds > 0) {
			expedition.AddLockout(runtime_event_name, event_data.lockout_seconds);
			handled = true;
		}

		if (event_data.replay_lockout_seconds > 0) {
			expedition.AddLockout(DzLockout::ReplayTimer, event_data.replay_lockout_seconds);
			handled = true;
		}

		ExecuteActions(expedition, event_data, runtime_event_name);

		if (notifier) {
			notifier->Message(Chat::Yellow, fmt::format("Expedition event complete: {}", runtime_event_name).c_str());
		}

		return handled;
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

	const uint32_t template_id = InsertID(db, fmt::format(
		"INSERT INTO expedition_templates "
		"(dz_template_id, name, slug, enabled, replay_lockout_seconds, replay_on_join, silent, request_phrase, request_mode, notes) "
		"VALUES ({}, '{}', '{}', 0, {}, 1, 0, '{}', '{}', '')",
		dz_template_id,
		escaped_name,
		Escape(UniqueSlug(name)),
		kSimpleSetupReplaySeconds,
		kSimpleRequestPhrase,
		kSimpleRequestMode
	));

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
	const std::string slug = Escape(fmt::format("{}_{}_{}", Slugify(name), source_template_id, static_cast<uint32_t>(std::time(nullptr))));
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

	const uint32_t template_id = InsertID(db, fmt::format(
		"INSERT INTO expedition_templates "
		"(dz_template_id, name, slug, enabled, replay_lockout_seconds, replay_on_join, silent, request_phrase, request_mode, notes) "
		"SELECT {}, '{}', '{}', 0, replay_lockout_seconds, replay_on_join, silent, request_phrase, request_mode, notes "
		"FROM expedition_templates WHERE id = {}",
		dz_template_id,
		escaped_name,
		slug,
		source_template_id
	));

	if (!template_id) {
		DynamicZoneTemplatesRepository::DeleteOne(db, dz_template_id);
		return 0;
	}

	QueryOK(db, fmt::format(
		"INSERT INTO expedition_template_request_npcs "
		"(expedition_template_id, zone_id, zone_version, npc_type_id, spawn2_id, phrase, enabled) "
		"SELECT {}, zone_id, zone_version, npc_type_id, spawn2_id, phrase, enabled FROM expedition_template_request_npcs WHERE expedition_template_id = {}",
		template_id,
		source_template_id
	));

	for (const auto& event_data : source->events) {
		const uint32_t event_id = InsertID(db, fmt::format(
			"INSERT INTO expedition_template_events "
			"(expedition_template_id, event_name, lockout_seconds, replay_lockout_seconds, lock_on_success, lock_on_failure, loot_protected, sort_order) "
			"VALUES ({}, '{}', {}, {}, {}, {}, {}, {})",
			template_id,
			Escape(event_data.event_name),
			event_data.lockout_seconds,
			event_data.replay_lockout_seconds,
			event_data.lock_on_success ? 1 : 0,
			event_data.lock_on_failure ? 1 : 0,
			event_data.loot_protected ? 1 : 0,
			event_data.sort_order
		));

		if (!event_id) {
			continue;
		}

		for (const auto& event_npc : event_data.npcs) {
			QueryOK(db, fmt::format(
				"INSERT INTO expedition_template_event_npcs "
				"(event_id, npc_type_id, spawn2_id, role, complete_on_death, complete_on_spawn, loot_protected) "
				"VALUES ({}, {}, {}, '{}', {}, {}, {})",
				event_id,
				event_npc.npc_type_id,
				event_npc.spawn2_id,
				Escape(event_npc.role),
				event_npc.complete_on_death ? 1 : 0,
				event_npc.complete_on_spawn ? 1 : 0,
				event_npc.loot_protected ? 1 : 0
			));
		}

		for (const auto& action : event_data.actions) {
			QueryOK(db, fmt::format(
				"INSERT INTO expedition_template_actions (event_id, action_type, action_value, sort_order) "
				"VALUES ({}, '{}', '{}', {})",
				event_id,
				Escape(action.action_type),
				Escape(action.action_value),
				action.sort_order
			));
		}
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

	QueryOK(db, fmt::format(
		"DELETE a FROM expedition_template_actions a "
		"INNER JOIN expedition_template_events e ON e.id = a.event_id "
		"WHERE e.expedition_template_id = {}",
		template_id
	));
	QueryOK(db, fmt::format(
		"DELETE n FROM expedition_template_event_npcs n "
		"INNER JOIN expedition_template_events e ON e.id = n.event_id "
		"WHERE e.expedition_template_id = {}",
		template_id
	));
	QueryOK(db, fmt::format("DELETE FROM expedition_template_events WHERE expedition_template_id = {}", template_id));
	QueryOK(db, fmt::format("DELETE FROM expedition_template_request_npcs WHERE expedition_template_id = {}", template_id));
	QueryOK(db, fmt::format("DELETE FROM expedition_templates WHERE id = {}", template_id));
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

	const bool template_ok = QueryOK(db, fmt::format(
		"UPDATE expedition_templates SET name = '{}', slug = '{}' WHERE id = {}",
		Escape(name),
		Escape(slug),
		template_id
	));
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
	const bool ok = QueryOK(db, fmt::format("UPDATE expedition_templates SET enabled = {} WHERE id = {}", enabled ? 1 : 0, template_id));
	Reload(db);
	return ok;
}

bool SetTemplateReplay(Database& db, uint32_t template_id, uint32_t seconds)
{
	const bool ok = QueryOK(db, fmt::format("UPDATE expedition_templates SET replay_lockout_seconds = {} WHERE id = {}", seconds, template_id));
	Reload(db);
	return ok;
}

bool SetTemplateSilent(Database& db, uint32_t template_id, bool silent)
{
	const bool ok = QueryOK(db, fmt::format("UPDATE expedition_templates SET silent = {} WHERE id = {}", silent ? 1 : 0, template_id));
	Reload(db);
	return ok;
}

bool SetTemplateRequestMode(Database& db, uint32_t template_id, const std::string& request_mode)
{
	if (!IsKnownRequestMode(request_mode)) {
		return false;
	}

	const bool ok = QueryOK(db, fmt::format(
		"UPDATE expedition_templates SET request_mode = '{}' WHERE id = {}",
		Escape(NormalizeRequestMode(request_mode)),
		template_id
	));
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
	auto results = db.QueryDatabase(fmt::format(
		"SELECT id FROM expedition_template_request_npcs "
		"WHERE expedition_template_id = {} AND zone_id = {} AND zone_version = {} AND npc_type_id = {} AND spawn2_id = {} LIMIT 1",
		template_id,
		zone_id,
		zone_version,
		npc_type_id,
		spawn2_id
	));

	if (results.Success() && results.RowCount() == 1) {
		auto row = results.begin();
		const uint32_t id = UInt(row[0]);
		QueryOK(db, fmt::format(
			"UPDATE expedition_template_request_npcs SET phrase = '{}', enabled = 1 WHERE id = {}",
			Escape(normalized),
			id
		));
		Reload(db);
		return id;
	}

	const uint32_t id = InsertID(db, fmt::format(
		"INSERT INTO expedition_template_request_npcs "
		"(expedition_template_id, zone_id, zone_version, npc_type_id, spawn2_id, phrase, enabled) "
		"VALUES ({}, {}, {}, {}, {}, '{}', 1)",
		template_id,
		zone_id,
		zone_version,
		npc_type_id,
		spawn2_id,
		Escape(normalized)
	));
	Reload(db);
	return id;
}

bool DeleteRequestNpc(Database& db, uint32_t template_id, uint32_t npc_type_id, uint32_t spawn2_id)
{
	const bool ok = QueryOK(db, fmt::format(
		"DELETE FROM expedition_template_request_npcs WHERE expedition_template_id = {} AND npc_type_id = {} AND spawn2_id = {}",
		template_id,
		npc_type_id,
		spawn2_id
	));
	Reload(db);
	return ok;
}

uint32_t AddEvent(Database& db, uint32_t template_id, const std::string& event_name)
{
	const uint32_t id = InsertID(db, fmt::format(
		"INSERT INTO expedition_template_events "
		"(expedition_template_id, event_name, lockout_seconds, replay_lockout_seconds, lock_on_success, lock_on_failure, loot_protected, sort_order) "
		"VALUES ({}, '{}', 0, 0, 1, 0, 0, 0)",
		template_id,
		Escape(event_name)
	));
	Reload(db);
	return id;
}

bool DeleteEvent(Database& db, uint32_t event_id)
{
	QueryOK(db, fmt::format("DELETE FROM expedition_template_actions WHERE event_id = {}", event_id));
	QueryOK(db, fmt::format("DELETE FROM expedition_template_event_npcs WHERE event_id = {}", event_id));
	const bool ok = QueryOK(db, fmt::format("DELETE FROM expedition_template_events WHERE id = {}", event_id));
	Reload(db);
	return ok;
}

bool SetEventName(Database& db, uint32_t event_id, const std::string& event_name)
{
	if (event_name.empty()) {
		return false;
	}

	const bool ok = QueryOK(db, fmt::format(
		"UPDATE expedition_template_events SET event_name = '{}' WHERE id = {}",
		Escape(event_name),
		event_id
	));
	Reload(db);
	return ok;
}

bool SetEventLockout(Database& db, uint32_t event_id, uint32_t seconds)
{
	const bool ok = QueryOK(db, fmt::format("UPDATE expedition_template_events SET lockout_seconds = {} WHERE id = {}", seconds, event_id));
	Reload(db);
	return ok;
}

bool SetEventReplay(Database& db, uint32_t event_id, uint32_t seconds)
{
	const bool ok = QueryOK(db, fmt::format("UPDATE expedition_template_events SET replay_lockout_seconds = {} WHERE id = {}", seconds, event_id));
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
	const bool ok = QueryOK(db, fmt::format(
		"UPDATE expedition_template_event_npcs SET loot_protected = {} WHERE event_id = {} AND npc_type_id = {} AND spawn2_id = {}",
		enabled ? 1 : 0,
		event_id,
		npc_type_id,
		spawn2_id
	));
	Reload(db);
	return ok;
}

bool SetEventNpcCompleteOnDeath(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool enabled)
{
	const bool ok = QueryOK(db, fmt::format(
		"UPDATE expedition_template_event_npcs SET complete_on_death = {} WHERE event_id = {} AND npc_type_id = {} AND spawn2_id = {}",
		enabled ? 1 : 0,
		event_id,
		npc_type_id,
		spawn2_id
	));
	Reload(db);
	return ok;
}

bool SetEventNpcCompleteOnSpawn(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool enabled)
{
	const bool ok = QueryOK(db, fmt::format(
		"UPDATE expedition_template_event_npcs SET complete_on_spawn = {} WHERE event_id = {} AND npc_type_id = {} AND spawn2_id = {}",
		enabled ? 1 : 0,
		event_id,
		npc_type_id,
		spawn2_id
	));
	Reload(db);
	return ok;
}

bool DeleteEventNpc(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id)
{
	const bool ok = QueryOK(db, fmt::format(
		"DELETE FROM expedition_template_event_npcs WHERE event_id = {} AND npc_type_id = {} AND spawn2_id = {}",
		event_id,
		npc_type_id,
		spawn2_id
	));
	Reload(db);
	return ok;
}

uint32_t AddAction(Database& db, uint32_t event_id, const std::string& action_type, const std::string& action_value)
{
	auto results = db.QueryDatabase(fmt::format(
		"SELECT COALESCE(MAX(sort_order), 0) + 1 FROM expedition_template_actions WHERE event_id = {}",
		event_id
	));
	int32_t sort_order = 1;
	if (results.Success() && results.RowCount() == 1) {
		auto row = results.begin();
		sort_order = Int(row[0]);
	}

	const uint32_t id = InsertID(db, fmt::format(
		"INSERT INTO expedition_template_actions (event_id, action_type, action_value, sort_order) "
		"VALUES ({}, '{}', '{}', {})",
		event_id,
		Escape(action_type),
		Escape(action_value),
		sort_order
	));
	Reload(db);
	return id;
}

bool ClearActions(Database& db, uint32_t event_id)
{
	const bool ok = QueryOK(db, fmt::format("DELETE FROM expedition_template_actions WHERE event_id = {}", event_id));
	Reload(db);
	return ok;
}

void Reload(Database& db)
{
	std::unordered_map<uint32_t, Template> templates;
	auto results = db.QueryDatabase(
		"SELECT id, dz_template_id, name, slug, enabled, replay_lockout_seconds, replay_on_join, silent, request_phrase, request_mode, notes "
		"FROM expedition_templates"
	);

	if (!results.Success()) {
		return;
	}

	for (auto row = results.begin(); row != results.end(); ++row) {
		Template e;
		e.id = UInt(row[0]);
		e.dz_template_id = UInt(row[1]);
		e.name = Text(row[2]);
		e.slug = Text(row[3]);
		e.enabled = Truthy(row[4]);
		e.replay_lockout_seconds = UInt(row[5]);
		e.replay_on_join = Truthy(row[6]);
		e.silent = Truthy(row[7]);
		e.request_phrase = NormalizePhrase(Text(row[8]));
		const std::string request_mode = Text(row[9]);
		e.request_mode = IsKnownRequestMode(request_mode) ? NormalizeRequestMode(request_mode) : request_mode;
		e.notes = Text(row[10]);
		e.dz_template = DynamicZoneTemplatesRepository::FindOne(db, e.dz_template_id);
		templates[e.id] = e;
	}

	for (const auto& request_npc : LoadRequestNpcs(db)) {
		auto it = templates.find(request_npc.expedition_template_id);
		if (it != templates.end()) {
			it->second.request_npcs.push_back(request_npc);
		}
	}

	for (auto event_data : LoadEvents(db)) {
		auto it = templates.find(event_data.expedition_template_id);
		if (it != templates.end()) {
			it->second.events.push_back(event_data);
		}
	}

	for (const auto& event_npc : LoadEventNpcs(db)) {
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

	for (const auto& action : LoadActions(db)) {
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

	for (const auto& [other_id, other_template] : g_templates) {
		if (
			other_id != template_data.id &&
			other_template.enabled &&
			other_template.dz_template.zone_id == template_data.dz_template.zone_id &&
			other_template.dz_template.zone_version == template_data.dz_template.zone_version &&
			Strings::EqualFold(other_template.dz_template.name, template_data.dz_template.name)
		) {
			const auto message = fmt::format(
				"Another enabled DB expedition template [{}] shares this dynamic-zone name and zone/version; runtime hooks may attach to the wrong template.",
				other_template.name
			);
			if (template_data.enabled) {
				result.errors.push_back(message);
			}
			else {
				result.warnings.push_back(message);
			}
		}
	}

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
	if (!allow_disabled && !template_data.enabled) {
		return false;
	}

	DynamicZone dz = BuildDynamicZone(template_data);
	return CheckExpeditionRequest(client, dz, true).success;
}

bool CanCreateExpeditionFromTemplate(Client& client, const Template& template_data)
{
	return CanCreateExpeditionFromTemplate(client, template_data, false);
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
	std::vector<std::pair<const Template*, const RequestNpc*>> matches;
	std::vector<int> match_scores;

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

			auto existing = std::ranges::find_if(matches, [&](const auto& match) {
				return match.first && match.first->id == current_template_id;
			});
			if (existing == matches.end()) {
				matches.push_back({ &template_data, &request_npc });
				match_scores.push_back(specificity);
			}
			else {
				const auto index = static_cast<size_t>(std::distance(matches.begin(), existing));
				if (specificity > match_scores[index]) {
					matches[index] = { &template_data, &request_npc };
					match_scores[index] = specificity;
				}
			}
		}
	}

	if (!is_hail) {
		for (const auto& [template_data, request_npc] : matches) {
			if (!template_data || !request_npc) {
				continue;
			}

			if (menu_template_id != 0) {
				if (menu_template_id == template_data->id) {
					return TryCreateTemplateRequest(client, *template_data);
				}
				continue;
			}

			if (enter_template_id != 0) {
				if (enter_template_id == template_data->id) {
					return TryEnterTemplateRequest(client, *template_data);
				}
				continue;
			}

			if (leave_template_id != 0) {
				if (leave_template_id == template_data->id) {
					return TryLeaveTemplateRequest(client, *template_data);
				}

				continue;
			}

			const std::string request_phrase = RequestPhrase(*template_data, *request_npc);
			if (phrase == request_phrase || phrase == fmt::format("start {}", request_phrase)) {
				return TryCreateTemplateRequest(client, *template_data);
			}
		}
	}

	if (is_hail) {
		OfferRequestMenu(client, npc, matches);
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
			if (!event_npc.complete_on_spawn || !MatchNpc(event_npc, npc)) {
				continue;
			}

			handled = CompleteEventForNpc(*expedition, event_data, event_npc, nullptr) || handled;
		}
	}

	return handled;
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

	std::vector<const Template*> candidates;
	if (const auto* selected_template = FindTemplate(GetBuilderState(client.CharacterID()).selected_template_id)) {
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
				const std::string header = is_chest ? "[Expedition Chest]" : "[Expedition Boss]";
				const std::string trigger =
					event_npc.complete_on_spawn ? "spawn" :
					(event_npc.complete_on_death ? "death" : "manual");
				const bool loot_protected = event_data.loot_protected || event_npc.loot_protected;
				SetSelectedTemplate(client.CharacterID(), template_data->id);
				SetSelectedEvent(client.CharacterID(), event_data.id);

				client.Message(Chat::NPCQuestSay, header.c_str());
				client.Message(Chat::NPCQuestSay, fmt::format(
					"{} ({}) | event {} [{}] | trigger {} | lockout {} | replay {} | loot {}",
					npc.GetCleanName(),
					npc.GetNPCTypeID(),
					event_data.event_name,
					event_data.id,
					trigger,
					Strings::SecondsToTime(event_data.lockout_seconds),
					Strings::SecondsToTime(event_data.replay_lockout_seconds),
					loot_protected ? "on" : "off"
				).c_str());
				client.Message(Chat::NPCQuestSay, fmt::format(
					"-> {} | {} | {}",
					Saylink::Silent(fmt::format("#expedition event select {}", event_data.id), "Select Event"),
					Saylink::Silent("#expedition menu", "Builder Menu"),
					Saylink::Silent("#expedition event lockout 6h", "Set 6h")
				).c_str());
				client.Message(Chat::NPCQuestSay, fmt::format(
					"-> {} | {} | {}",
					Saylink::Silent(fmt::format("#expedition event loot {}", loot_protected ? "off" : "on"), loot_protected ? "Loot Off" : "Loot On"),
					Saylink::Silent(fmt::format("#expedition event completeondeath {}", event_npc.complete_on_death ? "off" : "on"), event_npc.complete_on_death ? "Death Off" : "Death On"),
					Saylink::Silent(fmt::format("#expedition event completeonspawn {}", event_npc.complete_on_spawn ? "off" : "on"), event_npc.complete_on_spawn ? "Spawn Off" : "Spawn On")
				).c_str());
				client.Message(Chat::NPCQuestSay, fmt::format(
					"-> {} | {}",
					Saylink::Silent("#expedition event rename", "Rename Event"),
					Saylink::Silent("#expedition event npc remove confirm", "Remove Mapping")
				).c_str());
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
			if (!event_data.loot_protected && !event_npc.loot_protected) {
				continue;
			}

			if (event_npc.npc_type_id != 0) {
				expedition.SetLootEvent(event_npc.npc_type_id, RuntimeEventName(event_data, event_npc), DzLootEvent::Type::NpcType);
			}
		}
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
