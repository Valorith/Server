/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2013 EQEmu Development Team (http://eqemulator.net)

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

#include "common/bazaar.h"
#include "common/database/database_update.h"
#include "common/version.h"
#include "cppunit/cpptest.h"

#include <algorithm>

extern std::vector<ManifestEntry> manifest_entries;

class BazaarTest : public Test::Suite {
public:
	BazaarTest()
	{
		TEST_ADD(BazaarTest::RejectsZeroQuantity);
		TEST_ADD(BazaarTest::RejectsNegativeEncodedQuantity);
		TEST_ADD(BazaarTest::RejectsStackableWithoutListedCharges);
		TEST_ADD(BazaarTest::ClampsStackableQuantityToListedCharges);
		TEST_ADD(BazaarTest::AcceptsPositiveNonStackableQuantity);
		TEST_ADD(BazaarTest::RejectsNonStackableQuantityAboveOne);
		TEST_ADD(BazaarTest::UsesPurchaseQuantityForChargedStackable);
		TEST_ADD(BazaarTest::UsesPurchaseQuantityForUnchargedStackable);
		TEST_ADD(BazaarTest::PreservesChargesForChargedNonStackable);
		TEST_ADD(BazaarTest::UsesPurchaseQuantityForUnchargedNonStackable);
		TEST_ADD(BazaarTest::SupportsFullChargedStackPurchase);
		TEST_ADD(BazaarTest::ChargedStackPartialPurchaseDoesNotDuplicate);
		TEST_ADD(BazaarTest::CreatesDistinctNonStackableBarterItems);
		TEST_ADD(BazaarTest::RejectsInvalidBarterItemCreation);
		TEST_ADD(BazaarTest::RejectsZeroBarterSellQuantity);
		TEST_ADD(BazaarTest::RejectsBarterSellQuantityAboveBuyLineQuantity);
		TEST_ADD(BazaarTest::AcceptsBarterSellQuantityWithinBuyLineQuantity);
		TEST_ADD(BazaarTest::RejectsZeroListedPrice);
		TEST_ADD(BazaarTest::RejectsMismatchedPrice);
		TEST_ADD(BazaarTest::AcceptsMatchingListedPrice);
		TEST_ADD(BazaarTest::FailureSubActionRewritesSuccess);
		TEST_ADD(BazaarTest::FailureSubActionPreservesSpecificFailure);
		TEST_ADD(BazaarTest::AuditTotalCostMigrationUsesUnsignedBigint);
		TEST_ADD(BazaarTest::BinaryDatabaseVersionIncludesAuditMigration);
	}

	~BazaarTest()
	{
	}

private:
	void RejectsZeroQuantity()
	{
		auto result = Bazaar::ValidatePurchaseQuantity(0, true, 20);

		TEST_ASSERT(!result.is_valid);
		TEST_ASSERT(result.quantity == 0);
	}

	void RejectsNegativeEncodedQuantity()
	{
		auto result = Bazaar::ValidatePurchaseQuantity(0xFFFFFFFF, true, 20);

		TEST_ASSERT(!result.is_valid);
		TEST_ASSERT(result.quantity == 0);
	}

	void RejectsStackableWithoutListedCharges()
	{
		auto result = Bazaar::ValidatePurchaseQuantity(1, true, 0);

		TEST_ASSERT(!result.is_valid);
		TEST_ASSERT(result.quantity == 0);
	}

	void ClampsStackableQuantityToListedCharges()
	{
		auto result = Bazaar::ValidatePurchaseQuantity(50, true, 20);

		TEST_ASSERT(result.is_valid);
		TEST_ASSERT(result.quantity == 20);
	}

	void AcceptsPositiveNonStackableQuantity()
	{
		auto result = Bazaar::ValidatePurchaseQuantity(1, false, 0);

		TEST_ASSERT(result.is_valid);
		TEST_ASSERT(result.quantity == 1);
	}

	void RejectsNonStackableQuantityAboveOne()
	{
		auto result = Bazaar::ValidatePurchaseQuantity(2, false, 0);

		TEST_ASSERT(!result.is_valid);
		TEST_ASSERT(result.quantity == 0);
	}

	void UsesPurchaseQuantityForChargedStackable()
	{
		auto charges = Bazaar::ResolvePurchaseItemCharges(1, true, 1, 95);

		TEST_ASSERT(charges == 1);
	}

	void UsesPurchaseQuantityForUnchargedStackable()
	{
		auto charges = Bazaar::ResolvePurchaseItemCharges(7, true, -1, 95);

		TEST_ASSERT(charges == 7);
	}

	void PreservesChargesForChargedNonStackable()
	{
		auto charges = Bazaar::ResolvePurchaseItemCharges(1, false, 5, 3);

		TEST_ASSERT(charges == 3);
	}

	void UsesPurchaseQuantityForUnchargedNonStackable()
	{
		auto charges = Bazaar::ResolvePurchaseItemCharges(1, false, -1, 0);

		TEST_ASSERT(charges == 1);
	}

	void SupportsFullChargedStackPurchase()
	{
		auto charges = Bazaar::ResolvePurchaseItemCharges(95, true, 1, 95);

		TEST_ASSERT(charges == 95);
	}

	void ChargedStackPartialPurchaseDoesNotDuplicate()
	{
		const int16 listed_quantity = 95;
		auto quantity_validation = Bazaar::ValidatePurchaseQuantity(1, true, listed_quantity);
		auto delivered_quantity = Bazaar::ResolvePurchaseItemCharges(
			quantity_validation.quantity,
			true,
			1,
			listed_quantity
		);
		auto seller_quantity = listed_quantity - static_cast<int16>(quantity_validation.quantity);

		TEST_ASSERT(quantity_validation.is_valid);
		TEST_ASSERT(delivered_quantity == 1);
		TEST_ASSERT(seller_quantity == 94);
		TEST_ASSERT(delivered_quantity + seller_quantity == listed_quantity);
	}

	void CreatesDistinctNonStackableBarterItems()
	{
		SharedDatabase db;
		EQ::ItemData item{};
		item.ID        = 60539;
		item.Stackable = false;

		auto items = Bazaar::CreateBarterPurchaseItems(db, &item, 3);

		TEST_ASSERT(items.size() == 3);
		TEST_ASSERT(items[0].get() != items[1].get());
		TEST_ASSERT(items[1].get() != items[2].get());
		TEST_ASSERT(items[0]->GetSerialNumber() != items[1]->GetSerialNumber());
		TEST_ASSERT(items[1]->GetSerialNumber() != items[2]->GetSerialNumber());
		TEST_ASSERT(items[0]->GetUniqueID().empty());
		TEST_ASSERT(items[1]->GetUniqueID().empty());
		TEST_ASSERT(items[2]->GetUniqueID().empty());
	}

	void RejectsInvalidBarterItemCreation()
	{
		SharedDatabase db;

		TEST_ASSERT(Bazaar::CreateBarterPurchaseItems(db, nullptr, 3).empty());
		TEST_ASSERT(Bazaar::CreateBarterPurchaseItems(db, nullptr, 0).empty());
	}

	void RejectsZeroBarterSellQuantity()
	{
		TEST_ASSERT(!Bazaar::ValidateBarterSellQuantity(0, 20));
	}

	void RejectsBarterSellQuantityAboveBuyLineQuantity()
	{
		TEST_ASSERT(!Bazaar::ValidateBarterSellQuantity(21, 20));
	}

	void AcceptsBarterSellQuantityWithinBuyLineQuantity()
	{
		TEST_ASSERT(Bazaar::ValidateBarterSellQuantity(20, 20));
	}

	void RejectsZeroListedPrice()
	{
		TEST_ASSERT(!Bazaar::ValidatePurchasePrice(0, 0));
	}

	void RejectsMismatchedPrice()
	{
		TEST_ASSERT(!Bazaar::ValidatePurchasePrice(1, 100));
	}

	void AcceptsMatchingListedPrice()
	{
		TEST_ASSERT(Bazaar::ValidatePurchasePrice(100, 100));
	}

	void FailureSubActionRewritesSuccess()
	{
		TEST_ASSERT(Bazaar::ResolvePurchaseFailureSubAction(Success) == Failed);
	}

	void FailureSubActionPreservesSpecificFailure()
	{
		TEST_ASSERT(Bazaar::ResolvePurchaseFailureSubAction(TooManyParcels) == TooManyParcels);
	}

	void AuditTotalCostMigrationUsesUnsignedBigint()
	{
		auto entry = std::find_if(manifest_entries.begin(), manifest_entries.end(), [](const ManifestEntry &e) {
			return e.version == 9352 && e.description == "2026_07_28_trader_audit_totalcost_bigint.sql";
		});

		TEST_ASSERT(entry != manifest_entries.end());
		TEST_ASSERT(entry->condition == "empty");
		TEST_ASSERT(entry->check.find("information_schema") != std::string::npos);
		TEST_ASSERT(entry->check.find("DATA_TYPE` = 'bigint'") != std::string::npos);
		TEST_ASSERT(entry->check.find("COLUMN_TYPE` LIKE '%unsigned%'") != std::string::npos);
		TEST_ASSERT(entry->sql.find("MODIFY COLUMN `totalcost` BIGINT UNSIGNED") != std::string::npos);
		TEST_ASSERT(DatabaseUpdate::ShouldRunMigration(*entry, ""));
		TEST_ASSERT(!DatabaseUpdate::ShouldRunMigration(*entry, "totalcost"));
	}

	void BinaryDatabaseVersionIncludesAuditMigration()
	{
		TEST_ASSERT(CURRENT_BINARY_DATABASE_VERSION >= 9352);
	}
};
