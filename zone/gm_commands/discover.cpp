#include "zone/client.h"

#include "fmt/format.h"

#include <unordered_set>

namespace {
	struct OperationCounts {
		uint32 changed = 0;
		uint32 unchanged = 0;
		uint32 failed = 0;
	};

	struct ItemReference {
		uint32 item_id = 0;
		EQ::ItemInstance *instance = nullptr;
		std::string item_link;
		bool from_argument = false;
	};

	std::string GetItemLink(EQ::ItemInstance *inst)
	{
		EQ::SayLinkEngine linker;
		linker.SetLinkType(EQ::saylink::SayLinkItemInst);
		linker.SetItemInst(inst);

		return linker.GenerateLink();
	}

	bool ResolveItemReference(Client *c, const Seperator *sep, const char *command_name, ItemReference &item_ref)
	{
		if (sep->argnum >= 1 && sep->arg[1][0]) {
			if (!sep->IsNumber(1)) {
				c->Message(
					Chat::Red,
					fmt::format(
						"#{} failed: item ID argument [{}] is not numeric.",
						command_name,
						sep->arg[1]
					).c_str()
				);
				return false;
			}

			item_ref.item_id = Strings::ToUnsignedInt(sep->arg[1]);
			item_ref.from_argument = true;

			const auto *item = database.GetItem(item_ref.item_id);
			if (!item) {
				c->Message(
					Chat::Red,
					fmt::format(
						"#{} failed: item ID {} does not exist.",
						command_name,
						item_ref.item_id
					).c_str()
				);
				return false;
			}

			item_ref.item_link = database.CreateItemLink(item_ref.item_id);
			return true;
		}

		item_ref.instance = c->GetInv().GetItem(EQ::invslot::slotCursor);
		if (!item_ref.instance) {
			c->Message(
				Chat::Red,
				fmt::format(
					"#{} failed: no item found on your cursor.",
					command_name
				).c_str()
			);
			return false;
		}

		if (!item_ref.instance->GetItem()) {
			c->Message(
				Chat::Red,
				fmt::format(
					"#{} failed: cursor item ID {} has no item data.",
					command_name,
					item_ref.instance->GetID()
				).c_str()
			);
			return false;
		}

		item_ref.item_id = item_ref.instance->GetID();
		item_ref.item_link = GetItemLink(item_ref.instance);
		return true;
	}

	void DiscoverSingleItem(
		Client *c,
		uint32 item_id,
		std::unordered_set<uint32> &seen_items,
		OperationCounts &counts
	)
	{
		if (!item_id || !seen_items.emplace(item_id).second) {
			return;
		}

		if (c->IsDiscovered(item_id)) {
			counts.unchanged++;
		} else if (c->DiscoverItem(item_id)) {
			counts.changed++;
		} else if (c->IsDiscovered(item_id)) {
			counts.unchanged++;
		} else {
			counts.failed++;
		}
	}

	void UndiscoverSingleItem(
		Client *c,
		uint32 item_id,
		std::unordered_set<uint32> &seen_items,
		OperationCounts &counts
	)
	{
		if (!item_id || !seen_items.emplace(item_id).second) {
			return;
		}

		if (!c->IsDiscovered(item_id)) {
			counts.unchanged++;
		} else if (c->UndiscoverItem(item_id)) {
			counts.changed++;
		} else if (!c->IsDiscovered(item_id)) {
			counts.changed++;
		} else {
			counts.failed++;
		}
	}

	template <typename Operation>
	void ProcessItemTree(
		Client *c,
		EQ::ItemInstance *inst,
		std::unordered_set<uint32> &seen_items,
		OperationCounts &counts,
		Operation operation
	)
	{
		if (!inst || !inst->GetItem()) {
			return;
		}

		operation(c, inst->GetID(), seen_items, counts);

		for (const auto item_id : inst->GetAugmentIDs()) {
			operation(c, item_id, seen_items, counts);
		}

		for (const auto &[_, content_inst] : *inst->GetContents()) {
			ProcessItemTree(c, content_inst, seen_items, counts, operation);
		}
	}

	template <typename Operation>
	OperationCounts ProcessItemReference(Client *c, const ItemReference &item_ref, Operation operation)
	{
		std::unordered_set<uint32> seen_items;
		OperationCounts counts;

		if (item_ref.from_argument) {
			operation(c, item_ref.item_id, seen_items, counts);
			return counts;
		}

		ProcessItemTree(c, item_ref.instance, seen_items, counts, operation);
		return counts;
	}
}

void command_discover(Client *c, const Seperator *sep)
{
	ItemReference item_ref;
	if (!ResolveItemReference(c, sep, "discover", item_ref)) {
		return;
	}

	const OperationCounts counts = ProcessItemReference(c, item_ref, DiscoverSingleItem);

	if (counts.failed) {
		c->Message(
			Chat::Red,
			fmt::format(
				"#discover partially failed for {}: {} new, {} already discovered, {} failed.",
				item_ref.item_link,
				counts.changed,
				counts.unchanged,
				counts.failed
			).c_str()
		);
		return;
	}

	if (!counts.changed) {
		c->Message(
			Chat::Red,
			fmt::format(
				"#discover found no new discoveries for {}: {} item{} already discovered.",
				item_ref.item_link,
				counts.unchanged,
				counts.unchanged == 1 ? " was" : "s were"
			).c_str()
		);
		return;
	}

	c->Message(
		Chat::Green,
		fmt::format(
			"#discover succeeded for {}: {} new item{} discovered, {} already discovered.",
			item_ref.item_link,
			counts.changed,
			counts.changed == 1 ? "" : "s",
			counts.unchanged
		).c_str()
	);
}

void command_undiscover(Client *c, const Seperator *sep)
{
	ItemReference item_ref;
	if (!ResolveItemReference(c, sep, "undiscover", item_ref)) {
		return;
	}

	const OperationCounts counts = ProcessItemReference(c, item_ref, UndiscoverSingleItem);

	if (counts.failed) {
		c->Message(
			Chat::Red,
			fmt::format(
				"#undiscover partially failed for {}: {} removed, {} not discovered, {} failed.",
				item_ref.item_link,
				counts.changed,
				counts.unchanged,
				counts.failed
			).c_str()
		);
		return;
	}

	if (!counts.changed) {
		c->Message(
			Chat::Red,
			fmt::format(
				"#undiscover found no discoveries to remove for {}: {} item{} not discovered.",
				item_ref.item_link,
				counts.unchanged,
				counts.unchanged == 1 ? " was" : "s were"
			).c_str()
		);
		return;
	}

	c->Message(
		Chat::Green,
		fmt::format(
			"#undiscover succeeded for {}: {} item{} removed from discovered items, {} not discovered.",
			item_ref.item_link,
			counts.changed,
			counts.changed == 1 ? "" : "s",
			counts.unchanged
		).c_str()
	);
}
