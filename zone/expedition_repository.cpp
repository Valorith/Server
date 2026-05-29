#include "zone/expedition_repository.h"

#include "common/database.h"
#include "common/strings.h"

#include <fmt/format.h>

namespace ExpeditionRepository {

namespace {
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

	std::string Text(const char* value)
	{
		return value ? value : "";
	}

	// Centralized SQL escaping for the entire expedition data-access layer.
	std::string Escape(const std::string& value)
	{
		return Strings::Escape(value);
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
}

// ============================================================================
// LOAD OPERATIONS
// ============================================================================

std::vector<ExpeditionDB::Template> LoadAllTemplates(Database& db)
{
	std::vector<ExpeditionDB::Template> out;
	auto results = db.QueryDatabase(
		"SELECT id, dz_template_id, name, slug, enabled, replay_lockout_seconds, replay_on_join, silent, request_phrase, request_mode, notes "
		"FROM expedition_templates"
	);

	if (!results.Success()) {
		return out;
	}

	for (auto row = results.begin(); row != results.end(); ++row) {
		ExpeditionDB::Template e;
		e.id = UInt(row[0]);
		e.dz_template_id = UInt(row[1]);
		e.name = Text(row[2]);
		e.slug = Text(row[3]);
		e.enabled = Truthy(row[4]);
		e.replay_lockout_seconds = UInt(row[5]);
		e.replay_on_join = Truthy(row[6]);
		e.silent = Truthy(row[7]);
		e.request_phrase = Text(row[8]);
		e.request_mode = Text(row[9]);
		e.notes = Text(row[10]);
		out.push_back(std::move(e));
	}

	return out;
}

std::vector<ExpeditionDB::RequestNpc> LoadAllRequestNpcs(Database& db)
{
	std::vector<ExpeditionDB::RequestNpc> out;
	auto results = db.QueryDatabase(
		"SELECT id, expedition_template_id, zone_id, zone_version, npc_type_id, spawn2_id, phrase, enabled "
		"FROM expedition_template_request_npcs"
	);

	if (!results.Success()) {
		return out;
	}

	for (auto row = results.begin(); row != results.end(); ++row) {
		ExpeditionDB::RequestNpc e;
		e.id = UInt(row[0]);
		e.expedition_template_id = UInt(row[1]);
		e.zone_id = UInt(row[2]);
		e.zone_version = Int(row[3]);
		e.npc_type_id = UInt(row[4]);
		e.spawn2_id = UInt(row[5]);
		e.phrase = Text(row[6]);
		e.enabled = Truthy(row[7]);
		out.push_back(std::move(e));
	}

	return out;
}

std::vector<ExpeditionDB::Event> LoadAllEvents(Database& db)
{
	std::vector<ExpeditionDB::Event> out;
	auto results = db.QueryDatabase(
		"SELECT id, expedition_template_id, event_name, lockout_seconds, replay_lockout_seconds, "
		"lock_on_success, lock_on_failure, loot_protected, sort_order "
		"FROM expedition_template_events ORDER BY expedition_template_id, sort_order, id"
	);

	if (!results.Success()) {
		return out;
	}

	for (auto row = results.begin(); row != results.end(); ++row) {
		ExpeditionDB::Event e;
		e.id = UInt(row[0]);
		e.expedition_template_id = UInt(row[1]);
		e.event_name = Text(row[2]);
		e.lockout_seconds = UInt(row[3]);
		e.replay_lockout_seconds = UInt(row[4]);
		e.lock_on_success = Truthy(row[5]);
		e.lock_on_failure = Truthy(row[6]);
		e.loot_protected = Truthy(row[7]);
		e.sort_order = Int(row[8]);
		out.push_back(std::move(e));
	}

	return out;
}

std::vector<ExpeditionDB::EventNpc> LoadAllEventNpcs(Database& db)
{
	std::vector<ExpeditionDB::EventNpc> out;
	auto results = db.QueryDatabase(
		"SELECT id, event_id, npc_type_id, spawn2_id, role, complete_on_death, complete_on_spawn, loot_protected "
		"FROM expedition_template_event_npcs"
	);

	if (!results.Success()) {
		return out;
	}

	for (auto row = results.begin(); row != results.end(); ++row) {
		ExpeditionDB::EventNpc e;
		e.id = UInt(row[0]);
		e.event_id = UInt(row[1]);
		e.npc_type_id = UInt(row[2]);
		e.spawn2_id = UInt(row[3]);
		e.role = Text(row[4]);
		e.complete_on_death = Truthy(row[5]);
		e.complete_on_spawn = Truthy(row[6]);
		e.loot_protected = Truthy(row[7]);
		out.push_back(std::move(e));
	}

	return out;
}

std::vector<ExpeditionDB::Action> LoadAllActions(Database& db)
{
	std::vector<ExpeditionDB::Action> out;
	auto results = db.QueryDatabase(
		"SELECT id, event_id, action_type, action_value, sort_order "
		"FROM expedition_template_actions ORDER BY event_id, sort_order, id"
	);

	if (!results.Success()) {
		return out;
	}

	for (auto row = results.begin(); row != results.end(); ++row) {
		ExpeditionDB::Action e;
		e.id = UInt(row[0]);
		e.event_id = UInt(row[1]);
		e.action_type = Text(row[2]);
		e.action_value = Text(row[3]);
		e.sort_order = Int(row[4]);
		out.push_back(std::move(e));
	}

	return out;
}

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
	const std::string& request_phrase,
	const std::string& request_mode,
	const std::string& notes)
{
	return InsertID(db, fmt::format(
		"INSERT INTO expedition_templates "
		"(dz_template_id, name, slug, enabled, replay_lockout_seconds, replay_on_join, silent, request_phrase, request_mode, notes) "
		"VALUES ({}, '{}', '{}', {}, {}, {}, {}, '{}', '{}', '{}')",
		dz_template_id,
		Escape(name),
		Escape(slug),
		enabled ? 1 : 0,
		replay_lockout_seconds,
		replay_on_join ? 1 : 0,
		silent ? 1 : 0,
		Escape(request_phrase),
		Escape(request_mode),
		Escape(notes)
	));
}

uint32_t InsertTemplateFrom(
	Database& db,
	uint32_t source_template_id,
	uint32_t dz_template_id,
	const std::string& name,
	const std::string& slug)
{
	return InsertID(db, fmt::format(
		"INSERT INTO expedition_templates "
		"(dz_template_id, name, slug, enabled, replay_lockout_seconds, replay_on_join, silent, request_phrase, request_mode, notes) "
		"SELECT {}, '{}', '{}', 0, replay_lockout_seconds, replay_on_join, silent, request_phrase, request_mode, notes "
		"FROM expedition_templates WHERE id = {}",
		dz_template_id,
		Escape(name),
		Escape(slug),
		source_template_id
	));
}

bool UpdateTemplateName(Database& db, uint32_t template_id, const std::string& name, const std::string& slug)
{
	return QueryOK(db, fmt::format(
		"UPDATE expedition_templates SET name = '{}', slug = '{}' WHERE id = {}",
		Escape(name),
		Escape(slug),
		template_id
	));
}

bool UpdateTemplateEnabled(Database& db, uint32_t template_id, bool enabled)
{
	return QueryOK(db, fmt::format("UPDATE expedition_templates SET enabled = {} WHERE id = {}", enabled ? 1 : 0, template_id));
}

bool UpdateTemplateReplay(Database& db, uint32_t template_id, uint32_t seconds)
{
	return QueryOK(db, fmt::format("UPDATE expedition_templates SET replay_lockout_seconds = {} WHERE id = {}", seconds, template_id));
}

bool UpdateTemplateSilent(Database& db, uint32_t template_id, bool silent)
{
	return QueryOK(db, fmt::format("UPDATE expedition_templates SET silent = {} WHERE id = {}", silent ? 1 : 0, template_id));
}

bool UpdateTemplateRequestMode(Database& db, uint32_t template_id, const std::string& request_mode)
{
	return QueryOK(db, fmt::format(
		"UPDATE expedition_templates SET request_mode = '{}' WHERE id = {}",
		Escape(request_mode),
		template_id
	));
}

bool DeleteTemplate(Database& db, uint32_t template_id)
{
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
	return QueryOK(db, fmt::format("DELETE FROM expedition_templates WHERE id = {}", template_id));
}

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
	bool enabled)
{
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
			"UPDATE expedition_template_request_npcs SET phrase = '{}', enabled = {} WHERE id = {}",
			Escape(phrase),
			enabled ? 1 : 0,
			id
		));
		return id;
	}

	return InsertID(db, fmt::format(
		"INSERT INTO expedition_template_request_npcs "
		"(expedition_template_id, zone_id, zone_version, npc_type_id, spawn2_id, phrase, enabled) "
		"VALUES ({}, {}, {}, {}, {}, '{}', {})",
		template_id,
		zone_id,
		zone_version,
		npc_type_id,
		spawn2_id,
		Escape(phrase),
		enabled ? 1 : 0
	));
}

bool DeleteRequestNpc(Database& db, uint32_t template_id, uint32_t zone_id, int32_t zone_version, uint32_t npc_type_id, uint32_t spawn2_id)
{
	return QueryOK(db, fmt::format(
		"DELETE FROM expedition_template_request_npcs WHERE expedition_template_id = {} AND zone_id = {} AND zone_version = {} AND npc_type_id = {} AND spawn2_id = {}",
		template_id,
		zone_id,
		zone_version,
		npc_type_id,
		spawn2_id
	));
}

bool CopyRequestNpcs(Database& db, uint32_t src_template_id, uint32_t dst_template_id)
{
	return QueryOK(db, fmt::format(
		"INSERT INTO expedition_template_request_npcs "
		"(expedition_template_id, zone_id, zone_version, npc_type_id, spawn2_id, phrase, enabled) "
		"SELECT {}, zone_id, zone_version, npc_type_id, spawn2_id, phrase, enabled FROM expedition_template_request_npcs WHERE expedition_template_id = {}",
		dst_template_id,
		src_template_id
	));
}

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
	int32_t sort_order)
{
	return InsertID(db, fmt::format(
		"INSERT INTO expedition_template_events "
		"(expedition_template_id, event_name, lockout_seconds, replay_lockout_seconds, lock_on_success, lock_on_failure, loot_protected, sort_order) "
		"VALUES ({}, '{}', {}, {}, {}, {}, {}, {})",
		template_id,
		Escape(event_name),
		lockout_seconds,
		replay_lockout_seconds,
		lock_on_success ? 1 : 0,
		lock_on_failure ? 1 : 0,
		loot_protected ? 1 : 0,
		sort_order
	));
}

bool UpdateEventName(Database& db, uint32_t event_id, const std::string& event_name)
{
	return QueryOK(db, fmt::format(
		"UPDATE expedition_template_events SET event_name = '{}' WHERE id = {}",
		Escape(event_name),
		event_id
	));
}

bool UpdateEventLockout(Database& db, uint32_t event_id, uint32_t seconds)
{
	return QueryOK(db, fmt::format("UPDATE expedition_template_events SET lockout_seconds = {} WHERE id = {}", seconds, event_id));
}

bool UpdateEventReplay(Database& db, uint32_t event_id, uint32_t seconds)
{
	return QueryOK(db, fmt::format("UPDATE expedition_template_events SET replay_lockout_seconds = {} WHERE id = {}", seconds, event_id));
}

bool DeleteEvent(Database& db, uint32_t event_id)
{
	QueryOK(db, fmt::format("DELETE FROM expedition_template_actions WHERE event_id = {}", event_id));
	QueryOK(db, fmt::format("DELETE FROM expedition_template_event_npcs WHERE event_id = {}", event_id));
	return QueryOK(db, fmt::format("DELETE FROM expedition_template_events WHERE id = {}", event_id));
}

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
	bool loot_protected)
{
	auto results = db.QueryDatabase(fmt::format(
		"SELECT id FROM expedition_template_event_npcs WHERE event_id = {} AND npc_type_id = {} AND spawn2_id = {} LIMIT 1",
		event_id,
		npc_type_id,
		spawn2_id
	));

	if (results.Success() && results.RowCount() == 1) {
		auto row = results.begin();
		const uint32_t id = UInt(row[0]);
		QueryOK(db, fmt::format(
			"UPDATE expedition_template_event_npcs SET role = '{}', complete_on_death = {}, complete_on_spawn = {}, loot_protected = {} WHERE id = {}",
			Escape(role),
			complete_on_death ? 1 : 0,
			complete_on_spawn ? 1 : 0,
			loot_protected ? 1 : 0,
			id
		));
		return id;
	}

	return InsertID(db, fmt::format(
		"INSERT INTO expedition_template_event_npcs "
		"(event_id, npc_type_id, spawn2_id, role, complete_on_death, complete_on_spawn, loot_protected) "
		"VALUES ({}, {}, {}, '{}', {}, {}, {})",
		event_id,
		npc_type_id,
		spawn2_id,
		Escape(role),
		complete_on_death ? 1 : 0,
		complete_on_spawn ? 1 : 0,
		loot_protected ? 1 : 0
	));
}

bool UpdateEventNpcLoot(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool loot_protected)
{
	return QueryOK(db, fmt::format(
		"UPDATE expedition_template_event_npcs SET loot_protected = {} WHERE event_id = {} AND npc_type_id = {} AND spawn2_id = {}",
		loot_protected ? 1 : 0,
		event_id,
		npc_type_id,
		spawn2_id
	));
}

bool UpdateEventNpcCompleteOnDeath(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool enabled)
{
	return QueryOK(db, fmt::format(
		"UPDATE expedition_template_event_npcs SET complete_on_death = {} WHERE event_id = {} AND npc_type_id = {} AND spawn2_id = {}",
		enabled ? 1 : 0,
		event_id,
		npc_type_id,
		spawn2_id
	));
}

bool UpdateEventNpcCompleteOnSpawn(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id, bool enabled)
{
	return QueryOK(db, fmt::format(
		"UPDATE expedition_template_event_npcs SET complete_on_spawn = {} WHERE event_id = {} AND npc_type_id = {} AND spawn2_id = {}",
		enabled ? 1 : 0,
		event_id,
		npc_type_id,
		spawn2_id
	));
}

bool DeleteEventNpc(Database& db, uint32_t event_id, uint32_t npc_type_id, uint32_t spawn2_id)
{
	return QueryOK(db, fmt::format(
		"DELETE FROM expedition_template_event_npcs WHERE event_id = {} AND npc_type_id = {} AND spawn2_id = {}",
		event_id,
		npc_type_id,
		spawn2_id
	));
}

bool CopyEventNpcs(Database& db, uint32_t src_event_id, uint32_t dst_event_id)
{
	return QueryOK(db, fmt::format(
		"INSERT INTO expedition_template_event_npcs "
		"(event_id, npc_type_id, spawn2_id, role, complete_on_death, complete_on_spawn, loot_protected) "
		"SELECT {}, npc_type_id, spawn2_id, role, complete_on_death, complete_on_spawn, loot_protected FROM expedition_template_event_npcs WHERE event_id = {}",
		dst_event_id,
		src_event_id
	));
}

// ============================================================================
// expedition_template_actions
// ============================================================================

uint32_t InsertAction(Database& db, uint32_t event_id, const std::string& action_type, const std::string& action_value)
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

	return InsertID(db, fmt::format(
		"INSERT INTO expedition_template_actions (event_id, action_type, action_value, sort_order) "
		"VALUES ({}, '{}', '{}', {})",
		event_id,
		Escape(action_type),
		Escape(action_value),
		sort_order
	));
}

bool DeleteActionsByEvent(Database& db, uint32_t event_id)
{
	return QueryOK(db, fmt::format("DELETE FROM expedition_template_actions WHERE event_id = {}", event_id));
}

bool CopyActions(Database& db, uint32_t src_event_id, uint32_t dst_event_id)
{
	return QueryOK(db, fmt::format(
		"INSERT INTO expedition_template_actions (event_id, action_type, action_value, sort_order) "
		"SELECT {}, action_type, action_value, sort_order FROM expedition_template_actions WHERE event_id = {}",
		dst_event_id,
		src_event_id
	));
}

} // namespace ExpeditionRepository
