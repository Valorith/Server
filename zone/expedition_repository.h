#pragma once

#include "zone/expedition_db.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Database;

namespace ExpeditionRepository {

// ============================================================================
// LOAD OPERATIONS (fetch from DB, parse rows, return typed vectors)
//
// Loaders return rows verbatim (no business-rule normalization), or std::nullopt
// on query failure so callers can preserve existing cache state. Callers are
// responsible for any normalization (e.g. phrase / request_mode) after load.
// ============================================================================

std::optional<std::vector<ExpeditionDB::Template>>   LoadAllTemplates(Database& db);
std::optional<std::vector<ExpeditionDB::RequestNpc>> LoadAllRequestNpcs(Database& db);
std::optional<std::vector<ExpeditionDB::Event>>      LoadAllEvents(Database& db);
std::optional<std::vector<ExpeditionDB::EventNpc>>   LoadAllEventNpcs(Database& db);
std::optional<std::vector<ExpeditionDB::Action>>     LoadAllActions(Database& db);

// ============================================================================
// expedition_templates
// ============================================================================

uint32_t InsertTemplate(
	Database& db,
	uint32_t dz_template_id,
	const std::string& name,
	const std::string& slug,
	bool enabled,
	uint32_t replay_lockout_seconds,
	bool replay_on_join,
	bool silent,
	bool boss_only_spawn,
	const std::string& request_phrase,
	const std::string& request_mode,
	const std::string& notes
);

uint32_t InsertTemplateFrom(
	Database& db,
	uint32_t source_template_id,
	uint32_t dz_template_id,
	const std::string& name,
	const std::string& slug
);

bool UpdateTemplateName(Database& db, uint32_t template_id, const std::string& name, const std::string& slug);
bool UpdateTemplateEnabled(Database& db, uint32_t template_id, bool enabled);
bool UpdateTemplateReplay(Database& db, uint32_t template_id, uint32_t seconds);
bool UpdateTemplateSilent(Database& db, uint32_t template_id, bool silent);
bool UpdateTemplateBossOnlySpawn(Database& db, uint32_t template_id, bool enabled);
bool UpdateTemplateRequestMode(Database& db, uint32_t template_id, const std::string& request_mode);
bool DeleteTemplate(Database& db, uint32_t template_id);

// ============================================================================
// expedition_template_request_npcs
// ============================================================================

uint32_t UpsertRequestNpc(
	Database& db,
	uint32_t template_id,
	uint32_t zone_id,
	uint32_t npc_type_id,
	uint32_t spawn2_id,
	const std::string& phrase,
	int32_t zone_version,
	bool enabled
);

bool DeleteRequestNpc(Database& db, uint32_t template_id, uint32_t zone_id, int32_t zone_version, uint32_t npc_type_id, uint32_t spawn2_id);
bool CopyRequestNpcs(Database& db, uint32_t src_template_id, uint32_t dst_template_id);

// ============================================================================
// expedition_template_events
// ============================================================================

uint32_t InsertEvent(
	Database& db,
	uint32_t template_id,
	const std::string& event_name,
	uint32_t lockout_seconds,
	uint32_t replay_lockout_seconds,
	bool lock_on_success,
	bool lock_on_failure,
	bool loot_protected,
	int32_t sort_order
);

bool UpdateEventName(Database& db, uint32_t event_id, const std::string& event_name);
bool UpdateEventLockout(Database& db, uint32_t event_id, uint32_t seconds);
bool UpdateEventReplay(Database& db, uint32_t event_id, uint32_t seconds);
bool DeleteEvent(Database& db, uint32_t event_id);

// ============================================================================
// expedition_template_event_npcs
// ============================================================================

uint32_t UpsertEventNpc(
	Database& db,
	uint32_t event_id,
	uint32_t npc_type_id,
	uint32_t spawn2_id,
	const std::string& role,
	bool complete_on_death,
	bool complete_on_spawn,
	bool loot_protected
);

bool UpdateEventNpcLoot(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool loot_protected);
bool UpdateEventNpcCompleteOnDeath(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool enabled);
bool UpdateEventNpcCompleteOnSpawn(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool enabled);
bool DeleteEventNpc(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id);
bool CopyEventNpcs(Database& db, uint32_t src_event_id, uint32_t dst_event_id);

// ============================================================================
// expedition_template_actions
// ============================================================================

uint32_t InsertAction(Database& db, uint32_t event_id, const std::string& action_type, const std::string& action_value);
bool DeleteActionsByEvent(Database& db, uint32_t event_id);
bool CopyActions(Database& db, uint32_t src_event_id, uint32_t dst_event_id);

} // namespace ExpeditionRepository
