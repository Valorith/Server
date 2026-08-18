#pragma once

#include "common/item_instance.h"
#include "common/shareddb.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class Bazaar {
public:
	struct PurchaseQuantityValidation {
		bool   is_valid;
		uint32 quantity;
	};

	struct TransactionValueValidation {
		bool   is_valid;
		uint64 total_cost;
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

	static TransactionValueValidation ValidateBuyLinePrice(
		uint32 unit_price,
		uint64 max_transaction_value
	);

	static TransactionValueValidation ValidateTransactionValue(
		uint32 unit_price,
		uint32 quantity,
		uint64 max_transaction_value
	);

	static bool ValidatePurchasePrice(uint32 requested_price, uint32 listed_price);

	// Same character + same zone + same instance reclaim keeps listings.
	// Alt login, dest change, and invalid IDs still wipe.
	static bool ShouldPreserveOfflineListings(
		uint32 offline_character_id,
		uint32 offline_zone_id,
		int32 offline_instance_id,
		uint32 selected_character_id,
		uint32 selected_zone_id,
		int32 selected_instance_id
	);

	// A live buyer row is authoritative, including zero buy lines after the
	// last quantity was purchased while offline. Missing buyer row means use
	// the RoF2 start payload. Barter Off deletes the row and is the reset.
	static bool ShouldUseClientBuyerStartPayload(
		bool buyer_row_exists,
		size_t persisted_buy_line_count
	);

	// Start-mode satchel add/remove is applied when unique IDs differ.
	// Matching IDs keep the persisted price instead of a stale INI price.
	static bool TraderListingSetsMatch(
		const std::vector<std::string> &persisted_unique_ids,
		const std::vector<std::string> &client_unique_ids
	);

	static uint32 ResolveTraderStartPrice(
		bool has_persisted_price,
		uint32 persisted_price,
		uint32 client_price
	);

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
