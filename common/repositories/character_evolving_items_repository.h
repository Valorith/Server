#pragma once

#include "common/repositories/base/base_character_evolving_items_repository.h"

#include "common/database.h"
#include "common/strings.h"

class CharacterEvolvingItemsRepository: public BaseCharacterEvolvingItemsRepository {
public:
	// Custom extended repository methods here

	static CharacterEvolvingItems SetCurrentAmountAndProgression(Database& db, const uint64 id, const uint64 amount, const double progression)
	{
		auto e = FindOne(db, id);
		if (e.id == 0) {
			return NewEntity();
		}

		e.current_amount = amount;
		e.progression    = progression;
		e.deleted_at     = 0;
		UpdateOne(db, e);
		return e;
	}

	static CharacterEvolvingItems SetEquipped(Database& db, const uint64 id, const bool equipped)
	{
		auto e = FindOne(db, id);
		if (e.id == 0) {
			return NewEntity();
		}

		e.equipped   = equipped;
		e.deleted_at = 0;
		UpdateOne(db, e);
		return e;
	}

	static CharacterEvolvingItems SoftDelete(Database& db, const uint64 id)
	{
		auto e = FindOne(db, id);
		if (e.id == 0) {
			return NewEntity();
		}

		e.deleted_at = time(nullptr);
		UpdateOne(db, e);
		return e;
	}

	static bool UpdateCharID(Database &db, const uint64 id, const uint32 to_char_id)
	{
		auto e = FindOne(db, id);
		if (e.id == 0) {
			return false;
		}

		e.character_id = to_char_id;
		e.deleted_at   = 0;
		return UpdateOne(db, e);
	}

	static CharacterEvolvingItems FindOneByItemUniqueID(Database &db, const std::string &item_unique_id, const uint32 char_id = 0)
	{
		if (item_unique_id.empty()) {
			return NewEntity();
		}

		auto filter = fmt::format(
			"`item_unique_id` = '{}' AND `deleted_at` IS NULL",
			Strings::Escape(item_unique_id)
		);

		if (char_id) {
			filter += fmt::format(" AND `character_id` = '{}'", char_id);
		}

		filter += " LIMIT 1";

		auto results = GetWhere(db, filter);
		return results.empty() ? NewEntity() : results.front();
	}

	static CharacterEvolvingItems SyncItemIdentity(
		Database &db,
		const uint64 id,
		const uint32 item_id,
		const std::string &item_unique_id,
		const uint32 final_item_id,
		const bool equipped
	)
	{
		auto e = FindOne(db, id);
		if (e.id == 0) {
			return NewEntity();
		}

		e.item_id        = item_id;
		e.item_unique_id = item_unique_id;
		e.final_item_id  = final_item_id;
		e.equipped       = equipped;
		e.deleted_at     = 0;
		UpdateOne(db, e);
		return e;
	}

	static CharacterEvolvingItems UpdateTransferState(
		Database &db,
		const uint64 id,
		const uint32 item_id,
		const std::string &item_unique_id,
		const uint64 amount,
		const double progression,
		const uint32 final_item_id
	)
	{
		auto e = FindOne(db, id);
		if (e.id == 0) {
			return NewEntity();
		}

		e.item_id        = item_id;
		e.item_unique_id = item_unique_id;
		e.current_amount = amount;
		e.progression    = progression;
		e.final_item_id  = final_item_id;
		e.deleted_at     = 0;
		UpdateOne(db, e);
		return e;
	}
};
