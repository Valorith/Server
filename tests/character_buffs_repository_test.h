#pragma once

#include "common/database/database_update.h"
#include "common/repositories/base/base_character_buffs_repository.h"
#include "common/version.h"
#include "cppunit/cpptest.h"

#include <algorithm>
#include <iterator>

extern std::vector<ManifestEntry> manifest_entries;

class CharacterBuffsRepositoryTest : public Test::Suite {
public:
	CharacterBuffsRepositoryTest()
	{
		TEST_ADD(CharacterBuffsRepositoryTest::MigrationPersistsClientCasterProvenance);
		TEST_ADD(CharacterBuffsRepositoryTest::RepositoryColumnOrderIncludesClientCasterProvenance);
		TEST_ADD(CharacterBuffsRepositoryTest::NewEntityDefaultsClientCasterProvenanceOff);
	}

private:
	void MigrationPersistsClientCasterProvenance()
	{
		auto entry = std::find_if(manifest_entries.begin(), manifest_entries.end(), [](const ManifestEntry &e) {
			return e.version == 9352 &&
				e.description == "2026_07_09_add_caster_is_client_to_character_buffs.sql";
		});

		TEST_ASSERT(entry != manifest_entries.end());
		TEST_ASSERT(!entry->content_schema_update);
		TEST_ASSERT(entry->check == "SHOW COLUMNS FROM `character_buffs` LIKE 'caster_is_client'");
		TEST_ASSERT(entry->condition == "empty");
		TEST_ASSERT(entry->sql.find("ALTER TABLE `character_buffs`") != std::string::npos);
		TEST_ASSERT(entry->sql.find("`caster_is_client` TINYINT(1) UNSIGNED NOT NULL DEFAULT '0'") != std::string::npos);
		TEST_ASSERT(CURRENT_BINARY_DATABASE_VERSION >= 9352);
	}

	void RepositoryColumnOrderIncludesClientCasterProvenance()
	{
		const auto columns = BaseCharacterBuffsRepository::Columns();
		const auto select_columns = BaseCharacterBuffsRepository::SelectColumns();
		const auto caster_name = std::find(columns.begin(), columns.end(), "caster_name");

		TEST_ASSERT(columns == select_columns);
		TEST_ASSERT(caster_name != columns.end());
		TEST_ASSERT(std::next(caster_name) != columns.end());
		TEST_ASSERT(*std::next(caster_name) == "caster_is_client");
		TEST_ASSERT(std::next(caster_name, 2) != columns.end());
		TEST_ASSERT(*std::next(caster_name, 2) == "ticsremaining");
	}

	void NewEntityDefaultsClientCasterProvenanceOff()
	{
		const auto entity = BaseCharacterBuffsRepository::NewEntity();
		TEST_ASSERT(entity.caster_is_client == 0);
	}
};
