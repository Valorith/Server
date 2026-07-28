#pragma once

#include "common/item_instance.h"
#include "common/shareddb.h"

#include <memory>
#include <vector>

class Bazaar {
public:
	struct PurchaseQuantityValidation {
		bool   is_valid;
		uint32 quantity;
	};

	static PurchaseQuantityValidation ValidatePurchaseQuantity(
		uint32 requested_quantity,
		bool is_stackable,
		int16 listed_charges
	);

	static int16 ResolvePurchaseItemCharges(
		uint32 purchase_quantity,
		bool is_stackable,
		int16 max_charges,
		int16 listed_charges
	);

	static std::vector<std::unique_ptr<EQ::ItemInstance>> CreateBarterPurchaseItems(
		SharedDatabase &db,
		const EQ::ItemData *item,
		uint32 quantity
	);

	static bool ValidateBarterSellQuantity(uint32 requested_quantity, uint32 listed_quantity);

	static bool ValidatePurchasePrice(uint32 requested_price, uint32 listed_price);

	static void RecordAuditTrail(
		Database &db,
		const std::string &seller,
		const std::string &buyer,
		uint32 item_id,
		const std::string &item_name,
		uint32 quantity,
		uint64 total_cost,
		int transaction_type
	);

	static uint32 ResolvePurchaseFailureSubAction(uint32 sub_action);

	static std::vector<BazaarSearchResultsFromDB_Struct>
	GetSearchResults(Database &content_db, Database &db, BazaarSearchCriteria_Struct search, unsigned int char_zone_id, int char_zone_instance_id);

};
