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
#include "cppunit/cpptest.h"

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
