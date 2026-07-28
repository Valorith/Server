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
#include "common/eq_limits.h"
#include "cppunit/cpptest.h"

#include <limits>

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
		TEST_ADD(BazaarTest::RejectsMultiCopyLoreBarterItemCreation);
		TEST_ADD(BazaarTest::RejectsZeroBarterSellQuantity);
		TEST_ADD(BazaarTest::RejectsBarterSellQuantityAboveBuyLineQuantity);
		TEST_ADD(BazaarTest::AcceptsBarterSellQuantityWithinBuyLineQuantity);
		TEST_ADD(BazaarTest::AcceptsBuyLinePriceAtLimit);
		TEST_ADD(BazaarTest::RejectsBuyLinePriceOneCopperOverLimitWithoutCommitValue);
		TEST_ADD(BazaarTest::AcceptsBuyerTransactionAtLimit);
		TEST_ADD(BazaarTest::AcceptsMultiItemBuyerTransactionAtLimit);
		TEST_ADD(BazaarTest::AllowsBuyLineWhoseFullQuantityExceedsTransactionLimit);
		TEST_ADD(BazaarTest::RejectsBuyerTransactionOneCopperOverLimitWithoutCommitValue);
		TEST_ADD(BazaarTest::RejectsBuyerTransactionOverLimitWithoutCommitValue);
		TEST_ADD(BazaarTest::RejectsOverflowSizedBuyerTransactionWithoutCommitValue);
		TEST_ADD(BazaarTest::RejectsZeroListedPrice);
		TEST_ADD(BazaarTest::RejectsMismatchedPrice);
		TEST_ADD(BazaarTest::AcceptsMatchingListedPrice);
		TEST_ADD(BazaarTest::FailureSubActionRewritesSuccess);
		TEST_ADD(BazaarTest::FailureSubActionPreservesSpecificFailure);
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
		EQ::ItemData charged_item{};
		charged_item.ID         = 60539;
		charged_item.Stackable  = false;
		charged_item.MaxCharges = 5;

		TEST_ASSERT(Bazaar::CreateBarterPurchaseItems(db, nullptr, 3).empty());
		TEST_ASSERT(Bazaar::CreateBarterPurchaseItems(db, nullptr, 0).empty());
		TEST_ASSERT(Bazaar::CreateBarterPurchaseItems(db, &charged_item, 1).empty());
	}

	void RejectsMultiCopyLoreBarterItemCreation()
	{
		SharedDatabase db;
		EQ::ItemData lore_item{};
		lore_item.ID        = 60539;
		lore_item.Stackable = false;
		lore_item.LoreFlag  = true;
		lore_item.LoreGroup = -1;

		TEST_ASSERT(Bazaar::CreateBarterPurchaseItems(db, &lore_item, 2).empty());
		TEST_ASSERT(Bazaar::CreateBarterPurchaseItems(db, &lore_item, 1).size() == 1);
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

	void AcceptsBuyLinePriceAtLimit()
	{
		const auto result = Bazaar::ValidateBuyLinePrice(
			static_cast<uint32>(EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT),
			EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT
		);

		TEST_ASSERT(result.is_valid);
		TEST_ASSERT(result.total_cost == EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT);
	}

	void RejectsBuyLinePriceOneCopperOverLimitWithoutCommitValue()
	{
		const auto result = Bazaar::ValidateBuyLinePrice(
			static_cast<uint32>(EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT + 1),
			EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT
		);

		TEST_ASSERT(!result.is_valid);
		TEST_ASSERT(result.total_cost == 0);
	}

	void AcceptsBuyerTransactionAtLimit()
	{
		const auto result = Bazaar::ValidateTransactionValue(
			static_cast<uint32>(EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT),
			1,
			EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT
		);

		TEST_ASSERT(result.is_valid);
		TEST_ASSERT(result.total_cost == EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT);
	}

	void AcceptsMultiItemBuyerTransactionAtLimit()
	{
		const auto result = Bazaar::ValidateTransactionValue(
			1000000000,
			2,
			EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT
		);

		TEST_ASSERT(result.is_valid);
		TEST_ASSERT(result.total_cost == EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT);
	}

	void AllowsBuyLineWhoseFullQuantityExceedsTransactionLimit()
	{
		const auto buy_line = Bazaar::ValidateBuyLinePrice(
			1000000000,
			EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT
		);
		const auto two_item_sale = Bazaar::ValidateTransactionValue(
			1000000000,
			2,
			EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT
		);
		const auto three_item_sale = Bazaar::ValidateTransactionValue(
			1000000000,
			3,
			EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT
		);

		TEST_ASSERT(buy_line.is_valid);
		TEST_ASSERT(two_item_sale.is_valid);
		TEST_ASSERT(!three_item_sale.is_valid);
		TEST_ASSERT(three_item_sale.total_cost == 0);
	}

	void RejectsBuyerTransactionOneCopperOverLimitWithoutCommitValue()
	{
		const auto result = Bazaar::ValidateTransactionValue(
			static_cast<uint32>(EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT + 1),
			1,
			EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT
		);

		TEST_ASSERT(!result.is_valid);
		TEST_ASSERT(result.total_cost == 0);
	}

	void RejectsBuyerTransactionOverLimitWithoutCommitValue()
	{
		const auto result = Bazaar::ValidateTransactionValue(
			1000000000,
			3,
			EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT
		);

		TEST_ASSERT(!result.is_valid);
		TEST_ASSERT(result.total_cost == 0);
	}

	void RejectsOverflowSizedBuyerTransactionWithoutCommitValue()
	{
		const auto result = Bazaar::ValidateTransactionValue(
			std::numeric_limits<uint32>::max(),
			std::numeric_limits<uint32>::max(),
			EQ::constants::BAZAAR_MAX_TRANSACTION_DEFAULT
		);

		TEST_ASSERT(!result.is_valid);
		TEST_ASSERT(result.total_cost == 0);
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
};
