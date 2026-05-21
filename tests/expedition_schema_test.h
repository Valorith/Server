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
		TEST_ADD(ExpeditionSchemaTest::InitialMigrationIncludesRequestMode);
		TEST_ADD(ExpeditionSchemaTest::FollowupMigrationAddsRequestMode);
		TEST_ADD(ExpeditionSchemaTest::BinaryDatabaseVersionIncludesExpeditionMigrations);
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

	void InitialMigrationIncludesRequestMode()
	{
		auto entry = std::find_if(manifest_entries.begin(), manifest_entries.end(), [](const ManifestEntry& e) {
			return e.version == 9342 && e.description == "2026_05_09_db_driven_expeditions.sql";
		});

		TEST_ASSERT(entry != manifest_entries.end());
		TEST_ASSERT(entry->content_schema_update);
		TEST_ASSERT(entry->sql.find("request_mode") != std::string::npos);
		TEST_ASSERT(entry->sql.find("db_only") != std::string::npos);
	}

	void FollowupMigrationAddsRequestMode()
	{
		auto entry = std::find_if(manifest_entries.begin(), manifest_entries.end(), [](const ManifestEntry& e) {
			return e.version == 9343;
		});

		TEST_ASSERT(entry != manifest_entries.end());
		TEST_ASSERT(entry->content_schema_update);
		TEST_ASSERT(entry->check == "SHOW COLUMNS FROM `expedition_templates` LIKE 'request_mode'");
		TEST_ASSERT(entry->condition == "empty");
		TEST_ASSERT(entry->sql.find("ADD COLUMN IF NOT EXISTS `request_mode` VARCHAR(32) NOT NULL DEFAULT 'db_only'") != std::string::npos);
	}

	void BinaryDatabaseVersionIncludesExpeditionMigrations()
	{
		TEST_ASSERT(CURRENT_BINARY_DATABASE_VERSION >= 9343);
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
