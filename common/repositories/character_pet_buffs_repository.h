#pragma once

#include "common/repositories/base/base_character_pet_buffs_repository.h"

#include "common/database.h"
#include "common/strings.h"

class CharacterPetBuffsRepository: public BaseCharacterPetBuffsRepository {
public:
	struct CharacterPetBuffsWithSuppressed {
		int32_t     char_id;
		int32_t     pet;
		int32_t     slot;
		int32_t     spell_id;
		int8_t      caster_level;
		std::string castername;
		int32_t     ticsremaining;
		int32_t     counters;
		int32_t     numhits;
		int32_t     rune;
		uint8_t     instrument_mod;
		uint8_t     suppressed;
	};

    /**
     * This file was auto generated and can be modified and extended upon
     *
     * Base repository methods are automatically
     * generated in the "base" version of this repository. The base repository
     * is immutable and to be left untouched, while methods in this class
     * are used as extension methods for more specific persistence-layer
     * accessors or mutators.
     *
     * Base Methods (Subject to be expanded upon in time)
     *
     * Note: Not all tables are designed appropriately to fit functionality with all base methods
     *
     * InsertOne
     * UpdateOne
     * DeleteOne
     * FindOne
     * GetWhere(std::string where_filter)
     * DeleteWhere(std::string where_filter)
     * InsertMany
     * All
     *
     * Example custom methods in a repository
     *
     * CharacterPetBuffsRepository::GetByZoneAndVersion(int zone_id, int zone_version)
     * CharacterPetBuffsRepository::GetWhereNeverExpires()
     * CharacterPetBuffsRepository::GetWhereXAndY()
     * CharacterPetBuffsRepository::DeleteWhereXAndY()
     *
     * Most of the above could be covered by base methods, but if you as a developer
     * find yourself re-using logic for other parts of the code, its best to just make a
     * method that can be re-used easily elsewhere especially if it can use a base repository
     * method and encapsulate filters there
	 */

	// Custom extended repository methods here

	static CharacterPetBuffsWithSuppressed NewEntityWithSuppressed()
	{
		return CharacterPetBuffsWithSuppressed{
			.char_id = 0,
			.pet = 0,
			.slot = 0,
			.spell_id = 0,
			.caster_level = 0,
			.castername = "",
			.ticsremaining = 0,
			.counters = 0,
			.numhits = 0,
			.rune = 0,
			.instrument_mod = 10,
			.suppressed = 0,
		};
	}

	static std::vector<CharacterPetBuffsWithSuppressed> GetWhereWithSuppressed(
		Database& db,
		const std::string& where_filter
	)
	{
		std::vector<CharacterPetBuffsWithSuppressed> entries;

		auto results = db.QueryDatabase(
			fmt::format(
				"SELECT {}, `suppressed` FROM {} WHERE {}",
				SelectColumnsRaw(),
				TableName(),
				where_filter
			)
		);

		entries.reserve(results.RowCount());

		for (auto row = results.begin(); row != results.end(); ++row) {
			auto e = NewEntityWithSuppressed();

			e.char_id        = row[0] ? static_cast<int32_t>(atoi(row[0])) : 0;
			e.pet            = row[1] ? static_cast<int32_t>(atoi(row[1])) : 0;
			e.slot           = row[2] ? static_cast<int32_t>(atoi(row[2])) : 0;
			e.spell_id       = row[3] ? static_cast<int32_t>(atoi(row[3])) : 0;
			e.caster_level   = row[4] ? static_cast<int8_t>(atoi(row[4])) : 0;
			e.castername     = row[5] ? row[5] : "";
			e.ticsremaining  = row[6] ? static_cast<int32_t>(atoi(row[6])) : 0;
			e.counters       = row[7] ? static_cast<int32_t>(atoi(row[7])) : 0;
			e.numhits        = row[8] ? static_cast<int32_t>(atoi(row[8])) : 0;
			e.rune           = row[9] ? static_cast<int32_t>(atoi(row[9])) : 0;
			e.instrument_mod = row[10] ? static_cast<uint8_t>(strtoul(row[10], nullptr, 10)) : 10;
			e.suppressed     = row[11] ? static_cast<uint8_t>(strtoul(row[11], nullptr, 10)) : 0;

			entries.emplace_back(e);
		}

		return entries;
	}

	static int InsertManyWithSuppressed(
		Database& db,
		const std::vector<CharacterPetBuffsWithSuppressed>& entries
	)
	{
		if (entries.empty()) {
			return 0;
		}

		std::vector<std::string> insert_chunks;
		insert_chunks.reserve(entries.size());

		for (const auto& e : entries) {
			std::vector<std::string> values;
			values.reserve(12);

			values.push_back(std::to_string(e.char_id));
			values.push_back(std::to_string(e.pet));
			values.push_back(std::to_string(e.slot));
			values.push_back(std::to_string(e.spell_id));
			values.push_back(std::to_string(e.caster_level));
			values.push_back("'" + Strings::Escape(e.castername) + "'");
			values.push_back(std::to_string(e.ticsremaining));
			values.push_back(std::to_string(e.counters));
			values.push_back(std::to_string(e.numhits));
			values.push_back(std::to_string(e.rune));
			values.push_back(std::to_string(e.instrument_mod));
			values.push_back(std::to_string(e.suppressed));

			insert_chunks.push_back("(" + Strings::Implode(",", values) + ")");
		}

		auto results = db.QueryDatabase(
			fmt::format(
				"INSERT INTO {} ({}, `suppressed`) VALUES {}",
				TableName(),
				ColumnsRaw(),
				Strings::Implode(",", insert_chunks)
			)
		);

		return results.Success() ? results.RowsAffected() : 0;
	}

};
