#pragma once

#include "common/repositories/dynamic_zone_templates_repository.h"
#include "common/strings.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Client;
class Database;
class DynamicZone;
class NPC;
struct ExpeditionCheckResult;

namespace ExpeditionDB {

inline constexpr uint32_t kSimpleSetupMinPlayers = 1;
inline constexpr uint32_t kSimpleSetupMaxPlayers = 6;
inline constexpr uint32_t kSimpleSetupDurationSeconds = 21600;
inline constexpr uint32_t kSimpleSetupReplaySeconds = 7200;
inline constexpr uint32_t kSimpleBossLockoutSeconds = 21600;
inline constexpr uint32_t kSimpleBossReplaySeconds = 7200;
inline constexpr const char* kSimpleRequestPhrase = "expedition";
inline constexpr const char* kRequesterLastName = "Expeditions";
inline constexpr const char* kSimpleRequestMode = "db_only";
inline constexpr const char* kSimpleBossEventName = "Boss Defeated";
inline constexpr const char* kBaseZoneBossSpawnedReason = "base_zone_boss_spawned";
inline constexpr const char* kBaseZoneBossUnavailableStatus = "Unavailable";
inline constexpr const char* kBaseZoneBossUnavailableNote = "while associated bosses are spawned in the base zone.";
inline constexpr const char* kBaseZoneBossUnavailableMessage = "Unavailable while associated bosses are spawned in the base zone.";
inline constexpr const char* kBaseZoneBossUnavailableFailure = "unavailable while associated bosses are spawned in the base zone";
inline constexpr uint32_t kBossLockoutSpawnRetryMilliseconds = 60000;

struct RequestNpc {
	uint32_t id = 0;
	uint32_t expedition_template_id = 0;
	uint32_t zone_id = 0;
	int32_t zone_version = -1;
	uint32_t npc_type_id = 0;
	uint32_t spawn2_id = 0;
	std::string phrase = "expedition";
	bool enabled = true;
};

struct EventNpc {
	uint32_t id = 0;
	uint32_t event_id = 0;
	uint32_t npc_type_id = 0;
	uint32_t spawn2_id = 0;
	std::string role;
	bool complete_on_death = false;
	bool complete_on_spawn = false;
	bool loot_protected = false;
};

struct Action {
	uint32_t id = 0;
	uint32_t event_id = 0;
	std::string action_type;
	std::string action_value;
	int32_t sort_order = 0;
};

struct Event {
	uint32_t id = 0;
	uint32_t expedition_template_id = 0;
	std::string event_name;
	uint32_t lockout_seconds = 0;
	uint32_t replay_lockout_seconds = 0;
	bool lock_on_success = true;
	bool lock_on_failure = false;
	bool loot_protected = false;
	int32_t sort_order = 0;
	std::vector<EventNpc> npcs;
	std::vector<Action> actions;
};

inline bool IsBossEventNpc(const EventNpc& event_npc)
{
	return Strings::EqualFold(event_npc.role, "boss");
}

inline bool IsBaseZoneAvailabilityBoss(const EventNpc& event_npc)
{
	return IsBossEventNpc(event_npc) && event_npc.npc_type_id != 0;
}

inline bool BossEventNpcMatchesSpawn(const EventNpc& event_npc, uint32_t npc_type_id, uint32_t spawn2_id)
{
	if (!IsBaseZoneAvailabilityBoss(event_npc) || event_npc.npc_type_id != npc_type_id) {
		return false;
	}

	return event_npc.spawn2_id == 0 || event_npc.spawn2_id == spawn2_id;
}

inline bool BaseZoneAvailabilityBossMatchesSpawn(const EventNpc& event_npc, uint32_t npc_type_id, uint32_t spawn2_id)
{
	return BossEventNpcMatchesSpawn(event_npc, npc_type_id, spawn2_id);
}

inline bool EventNpcRemovalDeletesEvent(const EventNpc& event_npc)
{
	return IsBossEventNpc(event_npc);
}

struct Template {
	uint32_t id = 0;
	uint32_t dz_template_id = 0;
	std::string name;
	std::string slug;
	bool enabled = false;
	uint32_t replay_lockout_seconds = 0;
	bool replay_on_join = true;
	bool silent = false;
	bool boss_only_spawn = false;
	std::string request_phrase = "expedition";
	std::string request_mode = "db_only";
	std::string notes;
	DynamicZoneTemplatesRepository::DynamicZoneTemplates dz_template;
	std::vector<RequestNpc> request_npcs;
	std::vector<Event> events;
};

struct RequesterMatch {
	const Template* template_data = nullptr;
	const RequestNpc* request_npc = nullptr;
	int specificity = 0;
};

using RequesterMatches = std::vector<RequesterMatch>;

inline std::string RequesterMenuLabel(const Template& template_data)
{
	if (!template_data.name.empty()) {
		return template_data.name;
	}

	if (!template_data.dz_template.name.empty()) {
		return template_data.dz_template.name;
	}

	return "Template " + std::to_string(template_data.id);
}

inline void SortRequesterMatches(RequesterMatches& matches)
{
	std::ranges::sort(matches, [](const RequesterMatch& lhs, const RequesterMatch& rhs) {
		if (!lhs.template_data || !lhs.request_npc) {
			return false;
		}

		if (!rhs.template_data || !rhs.request_npc) {
			return true;
		}

		const std::string lhs_label = Strings::ToLower(RequesterMenuLabel(*lhs.template_data));
		const std::string rhs_label = Strings::ToLower(RequesterMenuLabel(*rhs.template_data));
		if (lhs_label != rhs_label) {
			return lhs_label < rhs_label;
		}

		const std::string lhs_dz_name = Strings::ToLower(lhs.template_data->dz_template.name);
		const std::string rhs_dz_name = Strings::ToLower(rhs.template_data->dz_template.name);
		if (lhs_dz_name != rhs_dz_name) {
			return lhs_dz_name < rhs_dz_name;
		}

		if (lhs.template_data->id != rhs.template_data->id) {
			return lhs.template_data->id < rhs.template_data->id;
		}

		return lhs.request_npc->id < rhs.request_npc->id;
	});
}

inline bool IsRequesterLockoutReason(const std::string& reason)
{
	return reason == "replay_lockout" || reason == "event_lockout_conflict";
}

inline bool IsBaseZoneBossSpawnedReason(const std::string& reason)
{
	return reason == kBaseZoneBossSpawnedReason;
}

inline bool IsRequesterStatusBlockReason(const std::string& reason)
{
	return IsBaseZoneBossSpawnedReason(reason);
}

inline bool SharesExpeditionLockoutNamespace(const Template& lhs, const Template& rhs)
{
	return Strings::EqualFold(lhs.dz_template.name, rhs.dz_template.name);
}

inline bool TemplateHasBaseZoneAvailabilityBossForSpawn(const Template& template_data, uint32_t npc_type_id, uint32_t spawn2_id)
{
	for (const auto& event_data : template_data.events) {
		for (const auto& event_npc : event_data.npcs) {
			if (BaseZoneAvailabilityBossMatchesSpawn(event_npc, npc_type_id, spawn2_id)) {
				return true;
			}
		}
	}

	return false;
}

struct ValidationResult {
	enum class Status {
		Valid,
		ValidWithWarnings,
		Invalid
	};

	Status status = Status::Invalid;
	std::vector<std::string> errors;
	std::vector<std::string> warnings;

	bool IsValid() const { return errors.empty(); }
	std::string StatusName() const;
};

struct BuilderState {
	uint32_t selected_template_id = 0;
	uint32_t selected_event_id = 0;
	bool edit_mode = false;
};

struct BossOnlySpawnFilter {
	bool enabled = false;
	std::unordered_map<uint32_t, std::unordered_set<uint32_t>> npc_type_ids_by_spawn2_id;
	std::unordered_set<uint32_t> unrestricted_spawn2_ids;

	bool AllowsSpawn2(uint32_t spawn2_id) const
	{
		return !enabled ||
			unrestricted_spawn2_ids.contains(spawn2_id) ||
			npc_type_ids_by_spawn2_id.contains(spawn2_id);
	}

	const std::unordered_set<uint32_t>* AllowedNPCTypeIDs(uint32_t spawn2_id) const
	{
		if (!enabled || unrestricted_spawn2_ids.contains(spawn2_id)) {
			return nullptr;
		}

		const auto allowed_it = npc_type_ids_by_spawn2_id.find(spawn2_id);
		return allowed_it != npc_type_ids_by_spawn2_id.end() ? &allowed_it->second : nullptr;
	}
};

inline BossOnlySpawnFilter BuildBossOnlySpawnFilter(const Template& template_data)
{
	BossOnlySpawnFilter filter;
	if (!template_data.boss_only_spawn) {
		return filter;
	}

	for (const auto& event_data : template_data.events) {
		for (const auto& event_npc : event_data.npcs) {
			if (
				IsBossEventNpc(event_npc) &&
				event_npc.npc_type_id != 0 &&
				event_npc.spawn2_id != 0
			) {
				filter.npc_type_ids_by_spawn2_id[event_npc.spawn2_id].insert(event_npc.npc_type_id);
			}
		}
	}

	for (const auto& request_npc : template_data.request_npcs) {
		if (!request_npc.enabled || request_npc.npc_type_id == 0 || request_npc.spawn2_id == 0) {
			continue;
		}

		auto allowed_it = filter.npc_type_ids_by_spawn2_id.find(request_npc.spawn2_id);
		if (allowed_it != filter.npc_type_ids_by_spawn2_id.end()) {
			allowed_it->second.insert(request_npc.npc_type_id);
		}
		else {
			filter.unrestricted_spawn2_ids.insert(request_npc.spawn2_id);
		}
	}

	filter.enabled = !filter.npc_type_ids_by_spawn2_id.empty();
	return filter;
}

uint32_t CreateTemplateFromClient(Database& db, Client& client, const std::string& name);
uint32_t CloneTemplate(Database& db, uint32_t source_template_id, const std::string& name);
bool DeleteTemplate(Database& db, uint32_t template_id);
bool SetTemplateName(Database& db, uint32_t template_id, const std::string& name);
bool SetTemplateEnabled(Database& db, uint32_t template_id, bool enabled);
bool SetTemplateReplay(Database& db, uint32_t template_id, uint32_t seconds);
bool SetTemplateSilent(Database& db, uint32_t template_id, bool silent);
bool SetTemplateBossOnlySpawn(Database& db, uint32_t template_id, bool enabled);
bool SetTemplateRequestMode(Database& db, uint32_t template_id, const std::string& request_mode);
bool SetDzTemplateZone(Database& db, uint32_t dz_template_id, uint32_t zone_id, uint32_t version);
bool SetDzTemplateDuration(Database& db, uint32_t dz_template_id, uint32_t seconds);
bool SetDzTemplatePlayers(Database& db, uint32_t dz_template_id, uint32_t min_players, uint32_t max_players);
bool SetDzTemplateZoneIn(Database& db, uint32_t dz_template_id, float x, float y, float z, float h);
bool SetDzTemplateSafeReturn(Database& db, uint32_t dz_template_id, uint32_t zone_id, float x, float y, float z, float h);
bool SetDzTemplateCompass(Database& db, uint32_t dz_template_id, uint32_t zone_id, float x, float y, float z);
bool SetDzTemplateSwitchID(Database& db, uint32_t dz_template_id, uint32_t switch_id);
uint32_t UpsertRequestNpc(Database& db, uint32_t template_id, uint32_t zone_id, uint32_t npc_type_id, uint32_t spawn2_id, const std::string& phrase, int32_t zone_version = -1);
bool DeleteRequestNpc(Database& db, uint32_t template_id, uint32_t zone_id, int32_t zone_version, uint32_t npc_type_id, uint32_t spawn2_id);
uint32_t AddEvent(Database& db, uint32_t template_id, const std::string& event_name);
bool DeleteEvent(Database& db, uint32_t event_id);
bool SetEventName(Database& db, uint32_t event_id, const std::string& event_name);
bool SetEventLockout(Database& db, uint32_t event_id, uint32_t seconds);
bool SetEventReplay(Database& db, uint32_t event_id, uint32_t seconds);
bool SetEventNpc(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, const std::string& role);
bool SetEventNpcLoot(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool enabled);
bool SetEventNpcCompleteOnDeath(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool enabled);
bool SetEventNpcCompleteOnSpawn(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool enabled);
bool DeleteEventNpc(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id);
uint32_t AddAction(Database& db, uint32_t event_id, const std::string& action_type, const std::string& action_value);
bool ClearActions(Database& db, uint32_t event_id);

void Reload(Database& db);
const std::unordered_map<uint32_t, Template>& Templates();
const Template* FindTemplate(uint32_t template_id);
const Template* FindTemplate(const std::string& id_or_name);
const Event* FindEvent(uint32_t event_id);
const Event* FindEvent(const Template& template_data, const std::string& id_or_name);
ValidationResult ValidateTemplate(const Template& template_data);
std::string NpcTypeName(uint32_t npc_type_id);
std::string NpcTypeLabel(uint32_t npc_type_id);
DynamicZone* CreateExpeditionFromTemplate(Client& client, const Template& template_data);
DynamicZone* CreateExpeditionFromTemplate(Client& client, const Template& template_data, bool allow_disabled);
DynamicZone* CreateExpeditionFromTemplate(Client& client, const std::string& id_or_name);
bool CanCreateExpeditionFromTemplate(Client& client, const Template& template_data);
bool CanCreateExpeditionFromTemplate(Client& client, const Template& template_data, bool allow_disabled);
ExpeditionCheckResult CheckExpeditionFromTemplate(Client& client, const Template& template_data);
ExpeditionCheckResult CheckExpeditionFromTemplate(Client& client, const Template& template_data, bool allow_disabled);
ExpeditionCheckResult CheckExpeditionFromTemplate(Client& client, const std::string& id_or_name);
bool HandleRequestSay(Client& client, NPC& npc, const std::string& message);
bool HandleNpcDeath(NPC& npc, Client* killer);
bool HandleNpcSpawn(NPC& npc);
bool IsBossSpawnBlockedByActiveLockout(uint32_t npc_type_id, uint32_t spawn2_id);
BossOnlySpawnFilter GetBossOnlySpawnFilter(DynamicZone* expedition);
// Stamps the "Expeditions" surname on configured requester NPCs (and clears a stale one when an
// NPC is no longer a requester). Safe to call for any NPC spawn in any zone.
void ApplyRequesterLastName(NPC& npc);
void MaybeShowGmTargetMenu(Client& client, NPC& npc);
void ResetTargetMenu(uint32_t character_id);
void ApplyLootEvents(DynamicZone& expedition);
void ClearRuntimeEventState(uint32_t dz_id);

BuilderState& GetBuilderState(uint32_t character_id);
void SetSelectedTemplate(uint32_t character_id, uint32_t template_id);
void SetSelectedEvent(uint32_t character_id, uint32_t event_id);

std::string RoleFromTarget(NPC& npc);
std::string NormalizePhrase(const std::string& phrase);

} // namespace ExpeditionDB
