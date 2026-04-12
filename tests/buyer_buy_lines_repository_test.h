/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2014 EQEMu Development Team (http://eqemulator.net)

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

#include "common/repositories/buyer_buy_lines_repository.h"
#include "cppunit/cpptest.h"

class BuyerBuyLinesRepositoryTest: public Test::Suite {
public:
	BuyerBuyLinesRepositoryTest()
	{
		TEST_ADD(BuyerBuyLinesRepositoryTest::BuildsCharacterOnlyDeleteFilter);
		TEST_ADD(BuyerBuyLinesRepositoryTest::BuildsCharacterAndSlotDeleteFilter);
		TEST_ADD(BuyerBuyLinesRepositoryTest::BuildsTradeItemCleanupFilterFromBuyLineIds);
		TEST_ADD(BuyerBuyLinesRepositoryTest::ReturnsEmptyTradeItemCleanupFilterWithoutBuyLines);
	}

private:
	void BuildsCharacterOnlyDeleteFilter()
	{
		TEST_ASSERT(
			BuyerBuyLinesRepository::GetDeleteBuyLineWhereFilter(123) == "`char_id` = '123'"
		);
	}

	void BuildsCharacterAndSlotDeleteFilter()
	{
		TEST_ASSERT(
			BuyerBuyLinesRepository::GetDeleteBuyLineWhereFilter(123, 7) ==
			"`char_id` = '123' AND `buy_slot_id` = '7'"
		);
	}

	void BuildsTradeItemCleanupFilterFromBuyLineIds()
	{
		auto first = BuyerBuyLinesRepository::NewEntity();
		first.id = 42;

		auto second = BuyerBuyLinesRepository::NewEntity();
		second.id = 77;

		const std::vector<BuyerBuyLinesRepository::BuyerBuyLines> buy_lines = { first, second };

		TEST_ASSERT(
			BuyerBuyLinesRepository::GetTradeItemCleanupWhereFilter(buy_lines) ==
			"`buyer_buy_lines_id` IN(42, 77)"
		);
	}

	void ReturnsEmptyTradeItemCleanupFilterWithoutBuyLines()
	{
		const std::vector<BuyerBuyLinesRepository::BuyerBuyLines> buy_lines = {};

		TEST_ASSERT(
			BuyerBuyLinesRepository::GetTradeItemCleanupWhereFilter(buy_lines).empty()
		);
	}
};
