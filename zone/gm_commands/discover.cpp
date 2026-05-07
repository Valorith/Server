#include "zone/client.h"

#include "fmt/format.h"

#include <unordered_set>

namespace {
	struct DiscoveryCounts {
		uint32 discovered = 0;
		uint32 already_discovered = 0;
		uint32 failed = 0;
	};

	std::string GetItemLink(EQ::ItemInstance *inst)
	{
		EQ::SayLinkEngine linker;
		linker.SetLinkType(EQ::saylink::SayLinkItemInst);
		linker.SetItemInst(inst);

		return linker.GenerateLink();
	}

	void DiscoverSingleItem(
		Client *c,
		uint32 item_id,
		std::unordered_set<uint32> &seen_items,
		DiscoveryCounts &counts
	)
	{
		if (!item_id || !seen_items.emplace(item_id).second) {
			return;
		}

		if (c->IsDiscovered(item_id)) {
			counts.already_discovered++;
		} else if (c->DiscoverItem(item_id)) {
			counts.discovered++;
		} else if (c->IsDiscovered(item_id)) {
			counts.already_discovered++;
		} else {
			counts.failed++;
		}
	}

	void DiscoverItemTree(
		Client *c,
		EQ::ItemInstance *inst,
		std::unordered_set<uint32> &seen_items,
		DiscoveryCounts &counts
	)
	{
		if (!inst || !inst->GetItem()) {
			return;
		}

		DiscoverSingleItem(c, inst->GetID(), seen_items, counts);

		for (const auto item_id : inst->GetAugmentIDs()) {
			DiscoverSingleItem(c, item_id, seen_items, counts);
		}

		for (const auto &[_, content_inst] : *inst->GetContents()) {
			DiscoverItemTree(c, content_inst, seen_items, counts);
		}
	}
}

void command_discover(Client *c, const Seperator *sep)
{
	auto *cursor_item = c->GetInv().GetItem(EQ::invslot::slotCursor);
	if (!cursor_item) {
		c->Message(Chat::Red, "#discover failed: no item found on your cursor.");
		return;
	}

	if (!cursor_item->GetItem()) {
		c->Message(
			Chat::Red,
			fmt::format(
				"#discover failed: cursor item ID {} has no item data.",
				cursor_item->GetID()
			).c_str()
		);
		return;
	}

	const std::string item_link = GetItemLink(cursor_item);

	std::unordered_set<uint32> seen_items;
	DiscoveryCounts counts;
	DiscoverItemTree(c, cursor_item, seen_items, counts);

	if (counts.failed) {
		c->Message(
			Chat::Red,
			fmt::format(
				"#discover partially failed for {}: {} new, {} already discovered, {} failed.",
				item_link,
				counts.discovered,
				counts.already_discovered,
				counts.failed
			).c_str()
		);
		return;
	}

	if (!counts.discovered) {
		c->Message(
			Chat::Red,
			fmt::format(
				"#discover found no new discoveries for {}: {} item{} already discovered.",
				item_link,
				counts.already_discovered,
				counts.already_discovered == 1 ? " was" : "s were"
			).c_str()
		);
		return;
	}

	c->Message(
		Chat::Green,
		fmt::format(
			"#discover succeeded for {}: {} new item{} discovered, {} already discovered.",
			item_link,
			counts.discovered,
			counts.discovered == 1 ? "" : "s",
			counts.already_discovered
		).c_str()
	);
}
