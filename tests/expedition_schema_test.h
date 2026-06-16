#pragma once

#include "common/database/database_update.h"
#include "common/database_schema.h"
#include "common/version.h"
#include "cppunit/cpptest.h"
#include "zone/expedition_db.h"

#include <algorithm>
#include <unordered_set>

extern std::vector<ManifestEntry> manifest_entries;

class ExpeditionSchemaTest : public Test::Suite {
public:
	ExpeditionSchemaTest()
	{
		TEST_ADD(ExpeditionSchemaTest::ContentSchemaIncludesExpeditionAuthoringTables);
		TEST_ADD(ExpeditionSchemaTest::CreationMigrationHasFinalSchema);
		TEST_ADD(ExpeditionSchemaTest::BossOnlyMigrationUpdatesExistingTemplates);
		TEST_ADD(ExpeditionSchemaTest::EventCompletionModeMigrationUpdatesExistingEvents);
		TEST_ADD(ExpeditionSchemaTest::RequireBossesDeadMigrationUpdatesExistingTemplates);
		TEST_ADD(ExpeditionSchemaTest::BinaryDatabaseVersionIncludesExpeditionMigrations);
		TEST_ADD(ExpeditionSchemaTest::BossOnlyDefaultsAreOff);
		TEST_ADD(ExpeditionSchemaTest::RequireBossesDeadDefaultsOn);
		TEST_ADD(ExpeditionSchemaTest::EventCompletionModeDefaultsToFirstCompletion);
		TEST_ADD(ExpeditionSchemaTest::BossEventNpcRemovalDeletesEvent);
		TEST_ADD(ExpeditionSchemaTest::LockoutNamespaceUsesDynamicZoneName);
		TEST_ADD(ExpeditionSchemaTest::BossOnlyFilterKeepsRequestersUnrestricted);
		TEST_ADD(ExpeditionSchemaTest::BossOnlyFilterEnforcesWithoutConcreteBossSpawn);
		TEST_ADD(ExpeditionSchemaTest::BossOnlyFilterAllowsTriggerSpawnOverrides);
		TEST_ADD(ExpeditionSchemaTest::SimpleBuilderDefaultsAreRaidReady);
		TEST_ADD(ExpeditionSchemaTest::SharedRequesterMenuSortsExpeditions);
		TEST_ADD(ExpeditionSchemaTest::RequesterLockoutReasonsAreStatusAware);
		TEST_ADD(ExpeditionSchemaTest::BaseZoneBossSpawnedReasonUsesFormattedUnavailableMessage);
		TEST_ADD(ExpeditionSchemaTest::BaseZoneBossAvailabilityIsTemplateSpecific);
		TEST_ADD(ExpeditionSchemaTest::BossLockoutSpawnGateUsesConfiguredBossMappings);
		TEST_ADD(ExpeditionSchemaTest::GroupedBossRequirementMatchingSupportsDynamicBosses);
		TEST_ADD(ExpeditionSchemaTest::GroupedBossChestTriggerDoesNotCountAsRequirement);
		TEST_ADD(ExpeditionSchemaTest::GroupedBossAllCompletionRequiresEveryRequirement);
		TEST_ADD(ExpeditionSchemaTest::GroupedBossValidationHelpersRequireUnambiguousBossGroup);
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
		TEST_ASSERT(entry->sql.find("require_bosses_dead") != std::string::npos);
		TEST_ASSERT(entry->sql.find("db_only") != std::string::npos);
		TEST_ASSERT(entry->sql.find("zone_version") != std::string::npos);
		TEST_ASSERT(entry->sql.find("complete_on_spawn") != std::string::npos);
		TEST_ASSERT(entry->sql.find("completion_mode") != std::string::npos);
		TEST_ASSERT(entry->sql.find("first_completion") != std::string::npos);
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

	void EventCompletionModeMigrationUpdatesExistingEvents()
	{
		auto entry = std::find_if(manifest_entries.begin(), manifest_entries.end(), [](const ManifestEntry& e) {
			return e.version == 9346 && e.description == "2026_06_08_expedition_event_completion_mode.sql";
		});

		TEST_ASSERT(entry != manifest_entries.end());
		TEST_ASSERT(entry->content_schema_update);
		TEST_ASSERT(entry->check == "SHOW COLUMNS FROM `expedition_template_events` LIKE 'completion_mode'");
		TEST_ASSERT(entry->condition == "empty");
		TEST_ASSERT(entry->sql.find("ALTER TABLE `expedition_template_events`") != std::string::npos);
		TEST_ASSERT(entry->sql.find("completion_mode") != std::string::npos);
		TEST_ASSERT(entry->sql.find("DEFAULT 'first_completion'") != std::string::npos);
	}

	void RequireBossesDeadMigrationUpdatesExistingTemplates()
	{
		auto entry = std::find_if(manifest_entries.begin(), manifest_entries.end(), [](const ManifestEntry& e) {
			return e.version == 9347 && e.description == "2026_06_12_expedition_require_bosses_dead.sql";
		});

		TEST_ASSERT(entry != manifest_entries.end());
		TEST_ASSERT(entry->content_schema_update);
		TEST_ASSERT(entry->check == "SHOW COLUMNS FROM `expedition_templates` LIKE 'require_bosses_dead'");
		TEST_ASSERT(entry->condition == "empty");
		TEST_ASSERT(entry->sql.find("ALTER TABLE `expedition_templates`") != std::string::npos);
		TEST_ASSERT(entry->sql.find("require_bosses_dead") != std::string::npos);
		TEST_ASSERT(entry->sql.find("DEFAULT '1'") != std::string::npos);
	}

	void BinaryDatabaseVersionIncludesExpeditionMigrations()
	{
		TEST_ASSERT(CURRENT_BINARY_DATABASE_VERSION >= 9347);
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

	void RequireBossesDeadDefaultsOn()
	{
		ExpeditionDB::Template template_data;
		TEST_ASSERT(template_data.require_bosses_dead);
	}

	void EventCompletionModeDefaultsToFirstCompletion()
	{
		ExpeditionDB::Event event_data;
		TEST_ASSERT(event_data.completion_mode == ExpeditionDB::kCompletionModeFirst);
		TEST_ASSERT(!ExpeditionDB::IsGroupedBossCompletionMode(event_data));
		TEST_ASSERT(ExpeditionDB::NormalizeCompletionMode("all") == ExpeditionDB::kCompletionModeAllBosses);
		TEST_ASSERT(ExpeditionDB::NormalizeCompletionMode("any") == ExpeditionDB::kCompletionModeAnyBoss);
		TEST_ASSERT(ExpeditionDB::NormalizeCompletionMode("unexpected") == ExpeditionDB::kCompletionModeFirst);
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

		ExpeditionDB::Event single_boss_event;
		TEST_ASSERT(ExpeditionDB::EventNpcRemovalDeletesEvent(single_boss_event, boss_npc));

		ExpeditionDB::Event grouped_boss_event;
		grouped_boss_event.completion_mode = ExpeditionDB::kCompletionModeAllBosses;
		TEST_ASSERT(!ExpeditionDB::EventNpcRemovalDeletesEvent(grouped_boss_event, boss_npc));
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

	void BossOnlyFilterEnforcesWithoutConcreteBossSpawn()
	{
		ExpeditionDB::Template template_data;
		template_data.boss_only_spawn = true;

		ExpeditionDB::RequestNpc request_npc;
		request_npc.enabled = true;
		request_npc.npc_type_id = 300;
		request_npc.spawn2_id = 30;
		template_data.request_npcs.push_back(request_npc);

		ExpeditionDB::Event event_data;
		ExpeditionDB::EventNpc dynamic_boss;
		dynamic_boss.role = "boss";
		dynamic_boss.npc_type_id = 200;
		dynamic_boss.spawn2_id = 0;
		event_data.npcs.push_back(dynamic_boss);
		template_data.events.push_back(event_data);

		const auto filter = ExpeditionDB::BuildBossOnlySpawnFilter(template_data);

		TEST_ASSERT(filter.enabled);
		TEST_ASSERT(filter.AllowsSpawn2(30));
		TEST_ASSERT(!filter.AllowsSpawn2(10));
		TEST_ASSERT(filter.unrestricted_spawn2_ids.contains(30));
		TEST_ASSERT(filter.npc_type_ids_by_spawn2_id.empty());
	}

	void BossOnlyFilterAllowsTriggerSpawnOverrides()
	{
		ExpeditionDB::Template template_data;
		template_data.boss_only_spawn = true;

		ExpeditionDB::Event event_data;
		ExpeditionDB::EventNpc boss_npc;
		boss_npc.role = "boss";
		boss_npc.npc_type_id = 200;
		boss_npc.spawn2_id = 20;
		event_data.npcs.push_back(boss_npc);
		template_data.events.push_back(event_data);

		const auto filter = ExpeditionDB::BuildBossOnlySpawnFilter(template_data);

		ExpeditionDB::NPCTypeIDsBySpawn2ID always_allowed_npc_type_ids_by_spawn2_id;
		always_allowed_npc_type_ids_by_spawn2_id[10].insert(67000);
		always_allowed_npc_type_ids_by_spawn2_id[20].insert(67001);

		bool blocked_spawn_enabled = true;
		std::unordered_set<uint32_t> blocked_combined_allowed;
		const auto* blocked_allowed = ExpeditionDB::ResolveBossOnlyAllowedNPCTypeIDs(
			filter,
			always_allowed_npc_type_ids_by_spawn2_id,
			10,
			blocked_spawn_enabled,
			blocked_combined_allowed
		);

		TEST_ASSERT(blocked_spawn_enabled);
		TEST_ASSERT(blocked_allowed != nullptr);
		TEST_ASSERT(blocked_allowed->contains(67000));

		bool boss_spawn_enabled = true;
		std::unordered_set<uint32_t> boss_combined_allowed;
		const auto* boss_allowed = ExpeditionDB::ResolveBossOnlyAllowedNPCTypeIDs(
			filter,
			always_allowed_npc_type_ids_by_spawn2_id,
			20,
			boss_spawn_enabled,
			boss_combined_allowed
		);

		TEST_ASSERT(boss_spawn_enabled);
		TEST_ASSERT(boss_allowed != nullptr);
		TEST_ASSERT(boss_allowed->contains(200));
		TEST_ASSERT(boss_allowed->contains(67001));

		bool trash_spawn_enabled = true;
		std::unordered_set<uint32_t> trash_combined_allowed;
		const auto* trash_allowed = ExpeditionDB::ResolveBossOnlyAllowedNPCTypeIDs(
			filter,
			always_allowed_npc_type_ids_by_spawn2_id,
			50,
			trash_spawn_enabled,
			trash_combined_allowed
		);

		TEST_ASSERT(!trash_spawn_enabled);
		TEST_ASSERT(trash_allowed == nullptr);
	}

	void SimpleBuilderDefaultsAreRaidReady()
	{
		TEST_ASSERT(ExpeditionDB::kSimpleSetupMinPlayers == 6);
		TEST_ASSERT(ExpeditionDB::kSimpleSetupMaxPlayers == 54);
		TEST_ASSERT(ExpeditionDB::kSimpleSetupDurationSeconds == 21600);
		TEST_ASSERT(ExpeditionDB::kSimpleSetupReplaySeconds == 86400);
		TEST_ASSERT(ExpeditionDB::kSimpleBossLockoutSeconds == 561600);
		TEST_ASSERT(ExpeditionDB::kSimpleBossReplaySeconds == 86400);
		TEST_ASSERT(std::string(ExpeditionDB::kSimpleRequestPhrase) == "expedition");
		TEST_ASSERT(std::string(ExpeditionDB::kSimpleRequestMode) == "db_only");
		TEST_ASSERT(std::string(ExpeditionDB::kSimpleBossEventName) == "Boss Defeated");
	}

	void SharedRequesterMenuSortsExpeditions()
	{
		ExpeditionDB::Template first;
		first.id = 20;
		first.name = "Zlandicar's Labyrinth";

		ExpeditionDB::RequestNpc first_requester;
		first_requester.id = 2;
		first_requester.npc_type_id = 100;
		first_requester.spawn2_id = 10;

		ExpeditionDB::Template second;
		second.id = 10;
		second.name = "Aaryonar's Lair";

		ExpeditionDB::RequestNpc second_requester;
		second_requester.id = 1;
		second_requester.npc_type_id = first_requester.npc_type_id;
		second_requester.spawn2_id = first_requester.spawn2_id;

		ExpeditionDB::Template fallback;
		fallback.id = 30;
		fallback.dz_template.name = "Cekenar's Hall";

		ExpeditionDB::RequestNpc fallback_requester;
		fallback_requester.id = 3;
		fallback_requester.npc_type_id = first_requester.npc_type_id;
		fallback_requester.spawn2_id = first_requester.spawn2_id;

		ExpeditionDB::RequesterMatches matches = {
			{ &first, &first_requester, 0 },
			{ &fallback, &fallback_requester, 0 },
			{ &second, &second_requester, 0 }
		};

		ExpeditionDB::SortRequesterMatches(matches);

		TEST_ASSERT(matches.size() == 3);
		TEST_ASSERT(matches[0].template_data == &second);
		TEST_ASSERT(matches[1].template_data == &fallback);
		TEST_ASSERT(matches[2].template_data == &first);
		TEST_ASSERT(ExpeditionDB::RequesterMenuLabel(fallback) == "Cekenar's Hall");
	}

	void RequesterLockoutReasonsAreStatusAware()
	{
		TEST_ASSERT(ExpeditionDB::IsRequesterLockoutReason("replay_lockout"));
		TEST_ASSERT(ExpeditionDB::IsRequesterLockoutReason("event_lockout_conflict"));
		TEST_ASSERT(!ExpeditionDB::IsRequesterLockoutReason("player_count"));
		TEST_ASSERT(!ExpeditionDB::IsRequesterLockoutReason("member_already_in_expedition"));
	}

	void BaseZoneBossSpawnedReasonUsesFormattedUnavailableMessage()
	{
		TEST_ASSERT(ExpeditionDB::IsBaseZoneBossSpawnedReason(ExpeditionDB::kBaseZoneBossSpawnedReason));
		TEST_ASSERT(ExpeditionDB::IsRequesterStatusBlockReason(ExpeditionDB::kBaseZoneBossSpawnedReason));
		TEST_ASSERT(!ExpeditionDB::IsRequesterStatusBlockReason("replay_lockout"));
		TEST_ASSERT(std::string(ExpeditionDB::kBaseZoneBossUnavailableStatus) == "Unavailable");
		TEST_ASSERT(std::string(ExpeditionDB::kBaseZoneBossUnavailableNote) == "while associated bosses are spawned in the base zone.");
		TEST_ASSERT(std::string(ExpeditionDB::kBaseZoneBossUnavailableMessage) == "Unavailable while associated bosses are spawned in the base zone.");
		TEST_ASSERT(std::string(ExpeditionDB::kBaseZoneBossUnavailableFailure) == "unavailable while associated bosses are spawned in the base zone");
	}

	void BaseZoneBossAvailabilityIsTemplateSpecific()
	{
		ExpeditionDB::Template first_template;
		ExpeditionDB::Event first_event;
		ExpeditionDB::EventNpc first_boss;
		first_boss.role = "boss";
		first_boss.npc_type_id = 100;
		first_boss.spawn2_id = 10;
		first_event.npcs.push_back(first_boss);
		first_template.events.push_back(first_event);

		ExpeditionDB::Template second_template;
		ExpeditionDB::Event second_event;
		ExpeditionDB::EventNpc second_boss;
		second_boss.role = "boss";
		second_boss.npc_type_id = 200;
		second_boss.spawn2_id = 20;
		second_event.npcs.push_back(second_boss);
		second_template.events.push_back(second_event);

		ExpeditionDB::Template non_boss_template;
		ExpeditionDB::Event non_boss_event;
		ExpeditionDB::EventNpc add_npc;
		add_npc.role = "add";
		add_npc.npc_type_id = first_boss.npc_type_id;
		add_npc.spawn2_id = first_boss.spawn2_id;
		non_boss_event.npcs.push_back(add_npc);
		non_boss_template.events.push_back(non_boss_event);

		ExpeditionDB::Template wildcard_template;
		ExpeditionDB::Event wildcard_event;
		ExpeditionDB::EventNpc wildcard_boss;
		wildcard_boss.role = "boss";
		wildcard_boss.npc_type_id = 300;
		wildcard_boss.spawn2_id = 0;
		wildcard_event.npcs.push_back(wildcard_boss);
		wildcard_template.events.push_back(wildcard_event);

		TEST_ASSERT(ExpeditionDB::TemplateHasBaseZoneAvailabilityBossForSpawn(first_template, 100, 10));
		TEST_ASSERT(!ExpeditionDB::TemplateHasBaseZoneAvailabilityBossForSpawn(first_template, 100, 11));
		TEST_ASSERT(!ExpeditionDB::TemplateHasBaseZoneAvailabilityBossForSpawn(second_template, 100, 10));
		TEST_ASSERT(!ExpeditionDB::TemplateHasBaseZoneAvailabilityBossForSpawn(non_boss_template, 100, 10));
		TEST_ASSERT(ExpeditionDB::TemplateHasBaseZoneAvailabilityBossForSpawn(wildcard_template, 300, 99));
		TEST_ASSERT(!ExpeditionDB::TemplateHasBaseZoneAvailabilityBossForSpawn(wildcard_template, 301, 99));
	}

	void BossLockoutSpawnGateUsesConfiguredBossMappings()
	{
		TEST_ASSERT(ExpeditionDB::kBossLockoutSpawnRetryMilliseconds == 60000);

		ExpeditionDB::EventNpc boss_npc;
		boss_npc.role = "boss";
		boss_npc.npc_type_id = 100;
		boss_npc.spawn2_id = 10;

		ExpeditionDB::EventNpc wildcard_boss_npc;
		wildcard_boss_npc.role = "boss";
		wildcard_boss_npc.npc_type_id = 200;
		wildcard_boss_npc.spawn2_id = 0;

		ExpeditionDB::EventNpc non_boss_npc;
		non_boss_npc.role = "add";
		non_boss_npc.npc_type_id = boss_npc.npc_type_id;
		non_boss_npc.spawn2_id = boss_npc.spawn2_id;

		ExpeditionDB::EventNpc unset_boss_npc;
		unset_boss_npc.role = "boss";
		unset_boss_npc.npc_type_id = 0;
		unset_boss_npc.spawn2_id = boss_npc.spawn2_id;

		TEST_ASSERT(ExpeditionDB::BossEventNpcMatchesSpawn(boss_npc, 100, 10));
		TEST_ASSERT(!ExpeditionDB::BossEventNpcMatchesSpawn(boss_npc, 100, 11));
		TEST_ASSERT(!ExpeditionDB::BossEventNpcMatchesSpawn(boss_npc, 101, 10));
		TEST_ASSERT(ExpeditionDB::BossEventNpcMatchesSpawn(wildcard_boss_npc, 200, 99));
		TEST_ASSERT(!ExpeditionDB::BossEventNpcMatchesSpawn(non_boss_npc, 100, 10));
		TEST_ASSERT(!ExpeditionDB::BossEventNpcMatchesSpawn(unset_boss_npc, 0, 10));
	}

	void GroupedBossRequirementMatchingSupportsDynamicBosses()
	{
		ExpeditionDB::Event grouped_event;
		grouped_event.completion_mode = ExpeditionDB::kCompletionModeAllBosses;

		ExpeditionDB::EventNpc spawn_boss;
		spawn_boss.id = 1;
		spawn_boss.role = "boss";
		spawn_boss.npc_type_id = 100;
		spawn_boss.spawn2_id = 10;
		spawn_boss.complete_on_death = true;

		ExpeditionDB::EventNpc dynamic_boss;
		dynamic_boss.id = 2;
		dynamic_boss.role = "boss";
		dynamic_boss.npc_type_id = 200;
		dynamic_boss.spawn2_id = 0;
		dynamic_boss.complete_on_death = true;

		ExpeditionDB::EventNpc add_npc;
		add_npc.id = 3;
		add_npc.role = "add";
		add_npc.npc_type_id = 300;
		add_npc.complete_on_death = true;

		grouped_event.npcs = { spawn_boss, dynamic_boss, add_npc };

		TEST_ASSERT(ExpeditionDB::IsGroupedBossCompletionMode(grouped_event));
		TEST_ASSERT(ExpeditionDB::GroupedBossRequirementCount(grouped_event) == 2);
		TEST_ASSERT(ExpeditionDB::GroupedBossRequirementMatchesSpawn(spawn_boss, 100, 10));
		TEST_ASSERT(!ExpeditionDB::GroupedBossRequirementMatchesSpawn(spawn_boss, 100, 11));
		TEST_ASSERT(ExpeditionDB::GroupedBossRequirementMatchesSpawn(dynamic_boss, 200, 99));
		TEST_ASSERT(!ExpeditionDB::GroupedBossRequirementMatchesSpawn(add_npc, 300, 0));
	}

	void GroupedBossChestTriggerDoesNotCountAsRequirement()
	{
		ExpeditionDB::Event grouped_event;
		grouped_event.completion_mode = ExpeditionDB::kCompletionModeAllBosses;

		ExpeditionDB::EventNpc first_boss;
		first_boss.id = 1;
		first_boss.role = "boss";
		first_boss.npc_type_id = 100;
		first_boss.complete_on_death = true;

		ExpeditionDB::EventNpc second_boss;
		second_boss.id = 2;
		second_boss.role = "boss";
		second_boss.npc_type_id = 200;
		second_boss.complete_on_death = true;

		ExpeditionDB::EventNpc chest_trigger;
		chest_trigger.id = 3;
		chest_trigger.role = "chest";
		chest_trigger.npc_type_id = 300;
		chest_trigger.complete_on_spawn = true;

		grouped_event.npcs = { first_boss, second_boss, chest_trigger };

		TEST_ASSERT(ExpeditionDB::IsGroupedBossChestTrigger(chest_trigger));
		TEST_ASSERT(ExpeditionDB::GroupedBossRequirementCount(grouped_event) == 2);

		std::unordered_set<uint32_t> completed;
		completed.insert(chest_trigger.id);
		TEST_ASSERT(!ExpeditionDB::GroupedBossRequirementsComplete(grouped_event, completed));
	}

	void GroupedBossAllCompletionRequiresEveryRequirement()
	{
		ExpeditionDB::Event grouped_event;
		grouped_event.completion_mode = ExpeditionDB::kCompletionModeAllBosses;

		ExpeditionDB::EventNpc first_boss;
		first_boss.id = 10;
		first_boss.role = "boss";
		first_boss.npc_type_id = 100;
		first_boss.complete_on_death = true;

		ExpeditionDB::EventNpc second_boss;
		second_boss.id = 20;
		second_boss.role = "boss";
		second_boss.npc_type_id = 200;
		second_boss.complete_on_death = true;

		grouped_event.npcs = { first_boss, second_boss };

		std::unordered_set<uint32_t> completed;
		TEST_ASSERT(!ExpeditionDB::GroupedBossRequirementsComplete(grouped_event, completed));
		completed.insert(first_boss.id);
		TEST_ASSERT(!ExpeditionDB::GroupedBossRequirementsComplete(grouped_event, completed));
		completed.insert(second_boss.id);
		TEST_ASSERT(ExpeditionDB::GroupedBossRequirementsComplete(grouped_event, completed));
	}

	void GroupedBossValidationHelpersRequireUnambiguousBossGroup()
	{
		ExpeditionDB::Event grouped_event;
		grouped_event.event_name = ExpeditionDB::kSimpleBossEventName;
		grouped_event.completion_mode = ExpeditionDB::kCompletionModeAllBosses;

		ExpeditionDB::EventNpc dynamic_boss;
		dynamic_boss.id = 1;
		dynamic_boss.role = "boss";
		dynamic_boss.npc_type_id = 100;
		dynamic_boss.spawn2_id = 0;
		dynamic_boss.complete_on_death = true;

		ExpeditionDB::EventNpc spawn_boss;
		spawn_boss.id = 2;
		spawn_boss.role = "boss";
		spawn_boss.npc_type_id = 100;
		spawn_boss.spawn2_id = 10;
		spawn_boss.complete_on_death = true;

		grouped_event.npcs = { dynamic_boss, spawn_boss };

		TEST_ASSERT(ExpeditionDB::IsGroupedBossCompletionMode(grouped_event));
		TEST_ASSERT(Strings::EqualFold(grouped_event.event_name, ExpeditionDB::kSimpleBossEventName));
		TEST_ASSERT(ExpeditionDB::GroupedBossHasAmbiguousDynamicRequirement(grouped_event));

		spawn_boss.npc_type_id = 200;
		grouped_event.npcs = { dynamic_boss, spawn_boss };
		TEST_ASSERT(!ExpeditionDB::GroupedBossHasAmbiguousDynamicRequirement(grouped_event));
	}
};
