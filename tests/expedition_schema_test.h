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
