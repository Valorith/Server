/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2013 EQEMu Development Team (http://eqemulator.net)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY except by those people which sell it, which
	are required to give you total support for your newly bought product;
	without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#pragma once

#include "common/repositories/trader_repository.h"
#include "cppunit/cpptest.h"

class TraderRepositoryTest : public Test::Suite {
public:
	TraderRepositoryTest()
	{
		TEST_ADD(TraderRepositoryTest::BuildBazaarTraderDetailsQueryIncludesItemFiltersAndLimit);
		TEST_ADD(TraderRepositoryTest::BuildBazaarTraderDetailsQueryOmitsLimitWhenUnlimited);
		TEST_ADD(TraderRepositoryTest::BuildsActiveTransactionFilter);
	}

	~TraderRepositoryTest()
	{
	}

private:
	void BuildBazaarTraderDetailsQueryIncludesItemFiltersAndLimit()
	{
		auto query = TraderRepository::BuildBazaarTraderDetailsQuery(
			"trader.item_cost >= 1000",
			"Shiny Dagger's Edge",
			"items.ac",
			"items.classes & 1 = 1",
			25
		);

		TEST_ASSERT(query.find("INNER JOIN items ON trader.item_id = items.id") != std::string::npos);
		TEST_ASSERT(query.find("items.`name` LIKE '%Shiny Dagger\\'s Edge%'") != std::string::npos);
		TEST_ASSERT(query.find("SELECT trader.id, trader.character_id") != std::string::npos);
		TEST_ASSERT(query.find("items.icon, items.ac") != std::string::npos);
		TEST_ASSERT(query.find("WHERE trader.item_cost >= 1000 AND items.`name` LIKE '%Shiny Dagger\\'s Edge%' AND items.classes & 1 = 1") != std::string::npos);
		TEST_ASSERT(query.find("ORDER BY trader.character_id ASC LIMIT 25") != std::string::npos);
	}

	void BuildBazaarTraderDetailsQueryOmitsLimitWhenUnlimited()
	{
		auto query = TraderRepository::BuildBazaarTraderDetailsQuery(
			"TRUE",
			"",
			"FALSE",
			"TRUE",
			0
		);

		TEST_ASSERT(query.find("INNER JOIN items ON trader.item_id = items.id") != std::string::npos);
		TEST_ASSERT(query.find("items.`name` LIKE '%%'") != std::string::npos);
		TEST_ASSERT(query.find(" LIMIT ") == std::string::npos);
	}

	void BuildsActiveTransactionFilter()
	{
		auto filter = TraderRepository::GetActiveTransactionWhereFilter(123, "Unique'Item");

		TEST_ASSERT(filter == "`id` = 123 AND `item_unique_id` = 'Unique\\'Item' AND `active_transaction` <> 0");
	}
};
