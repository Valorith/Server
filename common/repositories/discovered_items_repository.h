#pragma once

#include "common/repositories/base/base_discovered_items_repository.h"

#include "common/database.h"
#include "common/strings.h"

#include <unordered_set>

class DiscoveredItemsRepository: public BaseDiscoveredItemsRepository {
public:

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
     * DiscoveredItemsRepository::GetByZoneAndVersion(int zone_id, int zone_version)
     * DiscoveredItemsRepository::GetWhereNeverExpires()
     * DiscoveredItemsRepository::GetWhereXAndY()
     * DiscoveredItemsRepository::DeleteWhereXAndY()
     *
     * Most of the above could be covered by base methods, but if you as a developer
     * find yourself re-using logic for other parts of the code, its best to just make a
     * method that can be re-used easily elsewhere especially if it can use a base repository
     * method and encapsulate filters there
     */

	// Custom extended repository methods here
	static bool InsertIgnore(Database& db, const DiscoveredItems &e)
	{
		std::vector<std::string> v;

		v.push_back(std::to_string(e.item_id));
		v.push_back("'" + Strings::Escape(e.char_name) + "'");
		v.push_back(std::to_string(e.discovered_date));
		v.push_back(std::to_string(e.account_status));

		auto results = db.QueryDatabase(
			fmt::format(
				"INSERT IGNORE INTO {} ({}) VALUES ({})",
				TableName(),
				ColumnsRaw(),
				Strings::Implode(",", v)
			)
		);

		return results.Success() && results.RowsAffected() > 0;
	}

	static std::unordered_set<uint32_t> GetAllItemIDs(Database& db)
	{
		std::unordered_set<uint32_t> item_ids;

		auto results = db.QueryDatabase(
			fmt::format(
				"SELECT `{}` FROM {}",
				PrimaryKey(),
				TableName()
			)
		);

		if (!results.Success()) {
			LogWarning("Failed to load discovered item IDs: {}", results.ErrorMessage());
			return item_ids;
		}

		item_ids.reserve(results.RowCount());

		for (auto row = results.begin(); row != results.end(); ++row) {
			if (row[0]) {
				item_ids.insert(static_cast<uint32_t>(strtoul(row[0], nullptr, 10)));
			}
		}

		return item_ids;
	}

};
