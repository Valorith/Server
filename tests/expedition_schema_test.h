#pragma once

#include "common/database/database_update.h"
#include "common/database_schema.h"
#include "common/version.h"
#include "cppunit/cpptest.h"
#include "zone/expedition_db.h"

#include <algorithm>

extern std::vector<ManifestEntry> manifest_entries;

class ExpeditionSchemaTest : public Test::Suite {
public:
	ExpeditionSchemaTest()
	{
		TEST_ADD(ExpeditionSchemaTest::ContentSchemaIncludesExpeditionAuthoringTables);
		TEST_ADD(ExpeditionSchemaTest::CreationMigrationHasFinalSchema);
		TEST_ADD(ExpeditionSchemaTest::BossOnlyMigrationUpdatesExistingTemplates);
		TEST_ADD(ExpeditionSchemaTest::BinaryDatabaseVersionIncludesExpeditionMigrations);
		TEST_ADD(ExpeditionSchemaTest::BossOnlyDefaultsAreOff);
		TEST_ADD(ExpeditionSchemaTest::BossEventNpcRemovalDeletesEvent);
		TEST_ADD(ExpeditionSchemaTest::LockoutNamespaceUsesDynamicZoneName);
		TEST_ADD(ExpeditionSchemaTest::BossOnlyFilterKeepsRequestersUnrestricted);
		TEST_ADD(ExpeditionSchemaTest::SimpleBuilderDefaultsAreGroupReady);
	}

private:
	void ContentSchemaIncludesExpeditionAuthoringTables()
	{
		const auto tables = DatabaseSchema::GetContentTables();
		auto has_table = [&](const std::string& table_name) {
			return std::find(tables.begin(), tables.end(), table_name) != tables.end();
		};

		TEST_ASSERT(has_table("expedition_templates"));
		TEST_ASSERT(has_table("expedition_template_request_npcs"));
		TEST_ASSERT(has_table("expedition_template_events"));
		TEST_ASSERT(has_table("expedition_template_event_npcs"));
		TEST_ASSERT(has_table("expedition_template_actions"));
	}

	void CreationMigrationHasFinalSchema()
	{
		// The DB-driven expedition feature creation migration contains the full
		// schema for a fresh database. Later ALTER migrations only update existing
		// installs.
		auto entry = std::find_if(manifest_entries.begin(), manifest_entries.end(), [](const ManifestEntry& e) {
			return e.version == 9344 && e.description == "2026_05_21_db_driven_expeditions.sql";
		});

		TEST_ASSERT(entry != manifest_entries.end());
		TEST_ASSERT(entry->content_schema_update);
		TEST_ASSERT(entry->check == "SHOW TABLES LIKE 'expedition_templates'");
		TEST_ASSERT(entry->condition == "empty");
		TEST_ASSERT(entry->sql.find("request_mode") != std::string::npos);
		TEST_ASSERT(entry->sql.find("boss_only_spawn") != std::string::npos);
		TEST_ASSERT(entry->sql.find("db_only") != std::string::npos);
		TEST_ASSERT(entry->sql.find("zone_version") != std::string::npos);
		TEST_ASSERT(entry->sql.find("complete_on_spawn") != std::string::npos);
		TEST_ASSERT(entry->sql.find("idx_expedition_request_lookup") != std::string::npos);

		// The redundant follow-up migrations were consolidated away.
		auto followup = std::find_if(manifest_entries.begin(), manifest_entries.end(), [](const ManifestEntry& e) {
			return e.version == 9342 || e.version == 9343;
		});
		TEST_ASSERT(followup == manifest_entries.end());
	}

	void BossOnlyMigrationUpdatesExistingTemplates()
	{
		auto entry = std::find_if(manifest_entries.begin(), manifest_entries.end(), [](const ManifestEntry& e) {
			return e.version == 9345 && e.description == "2026_05_31_expedition_boss_only_spawn.sql";
		});

		TEST_ASSERT(entry != manifest_entries.end());
		TEST_ASSERT(entry->content_schema_update);
		TEST_ASSERT(entry->check == "SHOW COLUMNS FROM `expedition_templates` LIKE 'boss_only_spawn'");
		TEST_ASSERT(entry->condition == "empty");
		TEST_ASSERT(entry->sql.find("ALTER TABLE `expedition_templates`") != std::string::npos);
		TEST_ASSERT(entry->sql.find("boss_only_spawn") != std::string::npos);
		TEST_ASSERT(entry->sql.find("DEFAULT '0'") != std::string::npos);
	}

	void BinaryDatabaseVersionIncludesExpeditionMigrations()
	{
		TEST_ASSERT(CURRENT_BINARY_DATABASE_VERSION >= 9345);
	}

	void BossOnlyDefaultsAreOff()
	{
		ExpeditionDB::Template template_data;
		TEST_ASSERT(!template_data.boss_only_spawn);

		ExpeditionDB::BossOnlySpawnFilter filter;
		TEST_ASSERT(!filter.enabled);
		TEST_ASSERT(filter.npc_type_ids_by_spawn2_id.empty());
		TEST_ASSERT(filter.unrestricted_spawn2_ids.empty());
		TEST_ASSERT(filter.AllowsSpawn2(1));
		TEST_ASSERT(filter.AllowedNPCTypeIDs(1) == nullptr);
	}

	void BossEventNpcRemovalDeletesEvent()
	{
		ExpeditionDB::EventNpc boss_npc;
		boss_npc.role = "boss";
		TEST_ASSERT(ExpeditionDB::EventNpcRemovalDeletesEvent(boss_npc));

		ExpeditionDB::EventNpc mixed_case_boss_npc;
		mixed_case_boss_npc.role = "Boss";
		TEST_ASSERT(ExpeditionDB::EventNpcRemovalDeletesEvent(mixed_case_boss_npc));

		ExpeditionDB::EventNpc add_npc;
		add_npc.role = "add";
		TEST_ASSERT(!ExpeditionDB::EventNpcRemovalDeletesEvent(add_npc));

		ExpeditionDB::EventNpc chest_npc;
		chest_npc.role = "chest";
		TEST_ASSERT(!ExpeditionDB::EventNpcRemovalDeletesEvent(chest_npc));
	}

	void LockoutNamespaceUsesDynamicZoneName()
	{
		ExpeditionDB::Template first_template;
		first_template.name = "Catalog A";
		first_template.dz_template.name = "Shared Expedition";
		first_template.dz_template.zone_id = 1;
		first_template.dz_template.zone_version = 0;

		ExpeditionDB::Template second_template;
		second_template.name = "Catalog B";
		second_template.dz_template.name = "shared expedition";
		second_template.dz_template.zone_id = 2;
		second_template.dz_template.zone_version = 0;

		ExpeditionDB::Template unique_template;
		unique_template.name = "Catalog C";
		unique_template.dz_template.name = "Different Expedition";
		unique_template.dz_template.zone_id = first_template.dz_template.zone_id;
		unique_template.dz_template.zone_version = first_template.dz_template.zone_version;

		TEST_ASSERT(ExpeditionDB::SharesExpeditionLockoutNamespace(first_template, second_template));
		TEST_ASSERT(!ExpeditionDB::SharesExpeditionLockoutNamespace(first_template, unique_template));
	}

	void BossOnlyFilterKeepsRequestersUnrestricted()
	{
		ExpeditionDB::Template template_data;
		template_data.boss_only_spawn = true;

		ExpeditionDB::RequestNpc request_npc;
		request_npc.enabled = true;
		request_npc.npc_type_id = 300;
		request_npc.spawn2_id = 30;
		template_data.request_npcs.push_back(request_npc);

		ExpeditionDB::RequestNpc disabled_request_npc;
		disabled_request_npc.enabled = false;
		disabled_request_npc.npc_type_id = 400;
		disabled_request_npc.spawn2_id = 40;
		template_data.request_npcs.push_back(disabled_request_npc);

		ExpeditionDB::RequestNpc boss_spawn_request_npc;
		boss_spawn_request_npc.enabled = true;
		boss_spawn_request_npc.npc_type_id = 500;
		boss_spawn_request_npc.spawn2_id = 20;
		template_data.request_npcs.push_back(boss_spawn_request_npc);

		ExpeditionDB::Event event_data;
		ExpeditionDB::EventNpc trash_npc;
		trash_npc.role = "trash";
		trash_npc.npc_type_id = 100;
		trash_npc.spawn2_id = 10;
		event_data.npcs.push_back(trash_npc);

		ExpeditionDB::EventNpc boss_npc;
		boss_npc.role = "boss";
		boss_npc.npc_type_id = 200;
		boss_npc.spawn2_id = 20;
		event_data.npcs.push_back(boss_npc);
		template_data.events.push_back(event_data);

		const auto filter = ExpeditionDB::BuildBossOnlySpawnFilter(template_data);

		TEST_ASSERT(filter.enabled);
		TEST_ASSERT(filter.AllowsSpawn2(20));
		TEST_ASSERT(filter.AllowsSpawn2(30));
		TEST_ASSERT(!filter.AllowsSpawn2(10));
		TEST_ASSERT(!filter.AllowsSpawn2(40));
		TEST_ASSERT(!filter.AllowsSpawn2(50));
		TEST_ASSERT(filter.unrestricted_spawn2_ids.contains(30));
		TEST_ASSERT(!filter.unrestricted_spawn2_ids.contains(20));

		const auto* boss_allowed_types = filter.AllowedNPCTypeIDs(20);
		TEST_ASSERT(boss_allowed_types != nullptr);
		TEST_ASSERT(boss_allowed_types->contains(200));
		TEST_ASSERT(boss_allowed_types->contains(500));
		TEST_ASSERT(!boss_allowed_types->contains(100));

		TEST_ASSERT(filter.AllowedNPCTypeIDs(30) == nullptr);
	}

	void SimpleBuilderDefaultsAreGroupReady()
	{
		TEST_ASSERT(ExpeditionDB::kSimpleSetupMinPlayers == 1);
		TEST_ASSERT(ExpeditionDB::kSimpleSetupMaxPlayers == 6);
		TEST_ASSERT(ExpeditionDB::kSimpleSetupDurationSeconds == 21600);
		TEST_ASSERT(ExpeditionDB::kSimpleSetupReplaySeconds == 7200);
		TEST_ASSERT(ExpeditionDB::kSimpleBossLockoutSeconds == 21600);
		TEST_ASSERT(ExpeditionDB::kSimpleBossReplaySeconds == 7200);
		TEST_ASSERT(std::string(ExpeditionDB::kSimpleRequestPhrase) == "expedition");
		TEST_ASSERT(std::string(ExpeditionDB::kSimpleRequestMode) == "db_only");
		TEST_ASSERT(std::string(ExpeditionDB::kSimpleBossEventName) == "Boss Defeated");
	}
};
