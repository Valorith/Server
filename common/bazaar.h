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

	struct BuyerLinePrice {
		uint32 slot;
		uint32 item_id;
		uint32 price;
	};

	// Full replace from the RoF2 start payload only on a fresh Barter On
	// (no persisted lines). ToggleBuyerMode(true) upserts the buyer row
	// before Barter_BuyerItemStart, so row existence is not a restore
	// signal. reject_stale_empty_restore is set only by
	// RestorePersistedBuyerMode for a fulfilled (empty) order.
	static bool ShouldUseClientBuyerStartPayload(
		size_t persisted_buy_line_count,
		bool reject_stale_empty_restore
	);

	// Mid-session Start Barter overlays window prices onto existing lines.
	// After persist restore, the next Start is the stale RoF2 INI and must
	// not overwrite. After an explicit Update write, a later Start that
	// still carries the first-session INI must not overwrite those writes.
	static bool ShouldOverlayBuyerStartPrices(
		size_t persisted_buy_line_count,
		bool has_explicit_price_update,
		bool restored_persisted_buyer_mode
	);

	static int FindBuyerLineIndex(
		const std::vector<BuyerLinePrice> &lines,
		uint32 slot,
		uint32 item_id
	);

	// Update / Modify always persists the window offering.
	static uint32 ResolveBuyerUpdatePrice(uint32 persisted_price, uint32 client_price);

	// Start Barter uses the window price unless Update already wrote.
	static uint32 ResolveBuyerStartPrice(
		uint32 persisted_price,
		uint32 client_price,
		bool has_explicit_price_update
	);

	// Disable/delete must use the persisted slot when the client slot differs.
	static uint32 ResolveBuyerPersistedSlot(
		bool matched,
		uint32 persisted_slot,
		uint32 client_slot
	);

	static bool ShouldRestorePersistedBuyerMode(bool client_supports_buyer_items);

	// Player-visible prices after Update and/or Start, and therefore after
	// the next offline reconnect restore. Does not add INI-only lines.
	static std::vector<BuyerLinePrice> ApplyBuyerClientLinePrices(
		const std::vector<BuyerLinePrice> &persisted,
		const std::vector<BuyerLinePrice> &client,
		bool client_is_update,
		bool has_explicit_price_update,
		bool restored_persisted_buyer_mode = false
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

	// After persist restore, zone-in spawn PPUs are not a real walk-away.
	// Any later movement past the settle distance still ends trader/buyer.
	static constexpr float PersistRestoreSettleDistance = 5.0f;

	static bool ShouldTeardownListingsOnMovement(
		bool defer_after_persist_restore,
		float restore_x,
		float restore_y,
		float current_x,
		float current_y
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
