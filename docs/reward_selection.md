# RoF2 Select Reward support

Select Reward is a shared RoF2 client facility, not an achievement-owned
system. The zone keeps its packet/session and delivery machinery in
`ClientRewardSelection`; providers authorize requests, persist their own state,
and supply reward definitions. Personal tasks are the first provider in this
change; the boundary remains generic so later systems can share the same client
manager safely.

RoF2 exposes two independent manager lanes. They describe what the window may
do, not which gameplay system supplied the reward:

| Lane | Emulated opcode | RoF2 opcode | Client behavior |
| --- | --- | --- | --- |
| Claimable | `OP_AchievementReward` | `0x6411` | Shows **Choose** and permits action `3` |
| Preview | `OP_RewardSelection` | `0x6471` | Shows rewards, details, Inspect, and Preview, but hides **Choose** |

Sessions and request limits are independent per lane. The claimable lane holds
all outstanding provider sessions and presents them as native client tabs;
each tab is claimed and persisted independently. Tasks and future providers may
use either lane according to authorization. Provider reloads clear only
matching source sessions, so one provider cannot discard another provider's
pending claim.

## Compatibility boundary

Selectable task rewards are strictly opt-in and RoF2-only:

- `tasks.reward_method = 3` (`METHODSELECT`) opts a task into the new path.
- The task must also have one valid, enabled reward set. Invalid or incomplete
  content, including a reward that references a missing item, is rejected and
  is not advertised to the client.
- Shared tasks remain on their existing reward methods. World may assign them
  to offline or older-client members whose Select Reward capability is
  unknown, so `METHODSELECT` shared-task sets fail closed until member
  capability negotiation exists.
- Existing task reward methods `0`, `1`, and `2`, including
  `reward_id_list`, retain their existing behavior.
- Titanium, SoF, SoD, UF, and RoF retain their existing task selector and task
  description bytes, including a zero reward-selection indicator.
  `METHODSELECT` tasks are not offered to or accepted from those clients, and
  they are never sent the RoF2 Select Reward opcode.

`METHODSELECT` replaces the automatic `reward_id_list` item grant with the
selectable set. Existing completion emote, faction, cash, experience, and
reward-point fields still run through the established task reward path. A
selectable set may also contain coin, experience, AA, currency, or title
entries; leave the corresponding legacy field empty when the same grant is in
the set so content does not award it twice.

## Scripted offers

Perl and Lua quests can offer a transient reward selector with one atomic call.
The script supplies the complete selection as a Perl hash reference or Lua
table; the server parses and validates the entire definition before replacing
any currently open scripted offer. Invalid input returns `false`, logs the
precise field path, and leaves the current offer unchanged.

The scripting surface intentionally contains only three methods:

| Client method | Return | Purpose |
| --- | --- | --- |
| **OfferRewardSelection(config)** | Boolean | Validates and opens one complete scripted offer. |
| **ClearRewardSelection()** | None | Removes the client's open scripted offer. During the selection callback it also cancels automatic delivery and reopening. |
| **HasRewardSelection()** | Boolean | Reports whether the client currently has an open scripted offer. |

The feature is RoF2-only. `OfferRewardSelection` returns `false` for older
clients, so a quest can provide a fallback interaction. Calling it while a
claim is being processed also returns `false` rather than replacing the
in-flight selection.

### Offer config

| Field | Required | Type | Meaning |
| --- | --- | --- | --- |
| `selection_id` | Yes | Unsigned 32-bit integer, greater than zero | Script-defined identity returned to the selection event. |
| `title` | No | String | Window title. Defaults to **Choose a Reward**. |
| `options` | Yes | Ordered, dense array | One or more player-selectable choices. |
| `common_rewards` | No | Ordered, dense array of reward tables | Rewards displayed as **Also Includes** and granted with every choice. |

Unknown fields are rejected. Arrays must start at index 1 and contain no
holes. Perl array references naturally preserve their declared order; Lua
tables used as arrays may not contain string keys or sparse numeric indices.

Each option supports either a concise flat reward or an explicit reward
bundle:

| Field | Required | Type | Meaning |
| --- | --- | --- | --- |
| `option_id` | No | Unsigned 32-bit integer, greater than zero | Event identity for this choice. Defaults to its 1-based array position. IDs must be unique within the offer. |
| `label` | No | String | Choice label. When omitted, the server infers it from the reward definitions. |
| One reward definition | Yes for a flat option | Reward fields from the table below | The shortest form for a one-reward choice, such as `{ item_id => 1001 }`. |
| `rewards` | Yes for a bundle | Non-empty ordered array of reward tables | Grants every listed reward when this choice is authorized. Do not mix `rewards` with flat reward fields. |

### Reward definitions

Every reward table must contain exactly one type field. Modifiers are accepted
only where shown; `description` is always optional and overrides the inferred
detail text.

| Reward | Required fields | Optional fields | Grant and limits |
| --- | --- | --- | --- |
| Item | `item_id` | `quantity` (default `1`), `description` | Item stack/charge count; quantity `1..32767`. The item ID must exist. |
| Experience | `experience` | `description` | Uses normal AA allocation; amount `1..4294967295`. |
| Experience without AA allocation | `experience_no_aa` | `description` | Adds fixed normal experience while preserving AA experience; amount `1..4294967295`. |
| AA points | `aa_points` | `description` | Amount `1..2147483647`. |
| Coin | `money` | `description` | Total copper `1..2147483647999`. |
| Alternate currency | `alternate_currency_id`, `amount` | `description` | Amount `1..2147483647`. The currency ID must exist. |
| Title | `title_set_id` | `description` | Unlocks the existing title set; ID `1..2147483647`. |

Item, alternate-currency, and title-set IDs are resolved when the offer is
created. A missing content definition rejects the whole offer before anything
is shown. Numeric strings, negative values, fractions, zero reward amounts,
out-of-range values, and combinations such as `item_id` plus `experience` are
also rejected.

### Automatic display text

Scripts normally need no title, option label, or reward description:

- Item options use the item name loaded from the database.
- Experience options use **Experience** or **Experience (No AA)**.
- AA, coin, alternate-currency, and title rewards use their natural names.
  Currency labels use the currency's item name. A title uses its unique loaded
  prefix or suffix when the title set resolves to one unambiguous display name.
- A bundle joins its first two reward names and appends `+ N more` when needed.
- Common rewards use **Also Includes**.
- Detail text includes quantities, experience amounts, AA points, a compact
  platinum/gold/silver/copper breakdown, currency amount and name, or the title
  being unlocked.

An explicit `title`, `label`, or `description` is always preserved. The same
inference applies to task reward rows whose title, label, or description is
blank, so scripts and database-backed content present consistent text.

### Selection event

The entries in `OfferRewardSelection` are both the display definition and the
exact grant definition. Selecting a non-common option dispatches the existing
reward-selection event:

| Language and context | Event | Selection ID | Chosen option ID | Client |
| --- | --- | --- | --- | --- |
| Perl NPC or player | `EVENT_REWARD_SELECT` | `$reward_selection_id` | `$reward_option_id` | `$client` |
| Lua NPC | `event_reward_select(e)` | `e.selection_id` | `e.option_id` | `e.other` |
| Lua player | `event_reward_select(e)` | `e.selection_id` | `e.option_id` | `e.self` |

The handler validates quest-specific entitlement and returns a nonzero value
to authorize delivery. It must not manually grant the declared rewards. The
server grants every common reward plus the chosen option only after the event
authorizes the claim, then acknowledges the client.

Returning `0`, omitting the handler, or raising a script error rejects the
claim and reopens the offer. Calling `ClearRewardSelection` during the callback
cancels the offer and suppresses that reopen. The event reports only the chosen
non-common `option_id`; common rewards are automatic. Normal local, global, and
encounter event routing still applies, so only one handler should authorize a
given offer.

An NPC-created offer remains associated with that exact NPC lifetime. Removing
the NPC clears its outstanding offers before the entity ID can be reused. If
the live originating NPC has no handler, the player quest is the fallback.

Scripted offers intentionally have no database ledger. Zoning, disconnecting,
or restarting the zone discards them. The grant engine stops at the first
delivery failure. A retryable failure before any reward commits reopens the
offer. A persistence-ambiguous result, or a failure after an earlier reward
commits, closes it without automatic retry so an already delivered prefix
cannot be duplicated. Keep authorizing handlers short and idempotent because
their own side effects are outside this no-ledger boundary. Claim retries are
limited to one attempt every 500 milliseconds per client.

### Minimal Perl example

~~~perl
sub EVENT_SAY {
	return unless $text =~ /reward/i;

	$client->OfferRewardSelection({
		selection_id => 9001,
		options => [
			{ item_id => 1001 },
			{ experience => 5000 },
		],
	});
}

sub EVENT_REWARD_SELECT {
	return 0 unless $reward_selection_id == 9001;
	return $reward_option_id == 1 || $reward_option_id == 2;
}
~~~

### Complete Perl example

~~~perl
sub EVENT_SAY {
	return unless $text =~ /rewards/i;

	my $opened = $client->OfferRewardSelection({
		selection_id => 9100,
		title => "Veteran's Reward",
		options => [
			{ option_id => 101, item_id => 1001, quantity => 2 },
			{ option_id => 102, experience => 50000 },
			{ option_id => 103, experience_no_aa => 50000 },
			{ option_id => 104, aa_points => 3 },
			{ option_id => 105, money => 1234 },
			{
				option_id => 106,
				alternate_currency_id => 19,
				amount => 25,
			},
			{ option_id => 107, title_set_id => 71 },
			{
				option_id => 108,
				label => "Adventurer bundle",
				rewards => [
					{ item_id => 1001 },
					{ aa_points => 1 },
				],
			},
		],
		common_rewards => [
			{ money => 1000, description => "Completion bonus" },
		],
	});

	$client->Message(13, "The reward selector is unavailable.") unless $opened;
}

sub EVENT_REWARD_SELECT {
	return 0 unless $reward_selection_id == 9100;
	return $reward_option_id >= 101 && $reward_option_id <= 108;
}
~~~

### Minimal Lua example

~~~lua
function event_say(e)
	if not e.message:lower():find("reward", 1, true) then
		return
	end

	e.other:OfferRewardSelection({
		selection_id = 9001,
		options = {
			{ item_id = 1001 },
			{ experience = 5000 },
		},
	})
end

function event_reward_select(e)
	if e.selection_id ~= 9001 then
		return 0
	end
	return (e.option_id == 1 or e.option_id == 2) and 1 or 0
end
~~~

### Complete Lua example

~~~lua
function event_say(e)
	if not e.message:lower():find("rewards", 1, true) then
		return
	end

	local opened = e.other:OfferRewardSelection({
		selection_id = 9100,
		title = "Veteran's Reward",
		options = {
			{ option_id = 101, item_id = 1001, quantity = 2 },
			{ option_id = 102, experience = 50000 },
			{ option_id = 103, experience_no_aa = 50000 },
			{ option_id = 104, aa_points = 3 },
			{ option_id = 105, money = 1234 },
			{
				option_id = 106,
				alternate_currency_id = 19,
				amount = 25,
			},
			{ option_id = 107, title_set_id = 71 },
			{
				option_id = 108,
				label = "Adventurer bundle",
				rewards = {
					{ item_id = 1001 },
					{ aa_points = 1 },
				},
			},
		},
		common_rewards = {
			{ money = 1000, description = "Completion bonus" },
		},
	})

	if not opened then
		e.other:Message(13, "The reward selector is unavailable.")
	end
end

function event_reward_select(e)
	if e.selection_id ~= 9100 then
		return 0
	end
	return (e.option_id >= 101 and e.option_id <= 108) and 1 or 0
end
~~~

## Task content

The content database owns four tables:

- `task_reward_sets`: one enabled set per task.
- `task_reward_options`: ordered choices within the set. An option marked
  `common_to_all = 1` is granted with every selectable option.
- `task_rewards`: typed grant entries owned by the task.
- `task_reward_option_entries`: maps each reward to exactly one option.

Reward, set, and option IDs sent over the wire must fit in an unsigned 32-bit
integer. Every enabled reward must be mapped exactly once, every enabled option
must contain at least one enabled reward, and a set must contain at least one
non-common option. Item IDs, alternate-currency IDs, and title-set IDs must
resolve against the loaded server definitions or the set fails closed.

Supported reward types are:

| `reward_type` | Grant | `reward_data_id` | `amount` |
| ---: | --- | --- | --- |
| 0 | Item | Item ID | Stack/charge count |
| 1 | Experience | `0` = normal AA allocation; `1` = fixed normal XP only | Experience amount |
| 2 | Alternate advancement | 0 | AA points |
| 3 | Coin | 0 | Copper |
| 4 | Alternate currency | Currency ID | Currency amount |
| 5 | Title | Title-set ID | 1 |

Experience mode `0` uses the established `AddEXP` path, including the
character's AA allocation and configured experience modifiers. Mode `1`
preserves AA experience and adds the raw amount directly to normal experience;
it is the mode for rewards described as **No AA Exp**.
Mode 0 claims remain retryable while character experience is disabled so a
suppressed AddEXP call can never be recorded as delivered.

Blank display fields use the same inference as scripted offers. An empty set
title falls back to the task title; empty option labels and reward descriptions
are generated from the loaded item, currency, or title data and the configured
amount. RoF2 still supplies the visible type label **Player flags** for title
entries. Set `title`, `label`, or `description` only when content needs wording
that differs from the generated text.

Example with one common coin entry and two item choices:

```sql
UPDATE tasks SET reward_method = 3 WHERE id = 1001;

INSERT INTO task_reward_sets (reward_set_id, task_id, title)
VALUES (1001, 1001, 'Choose a Reward');

INSERT INTO task_reward_options
	(reward_set_id, option_id, sequence, label, common_to_all)
VALUES
	(1001, 1, 0, 'Completion coin', 1),
	(1001, 2, 1, 'First item', 0),
	(1001, 3, 2, 'Second item', 0);

INSERT INTO task_rewards
	(reward_id, task_id, sequence, reward_type, reward_data_id, amount)
VALUES
	(10001, 1001, 0, 3, 0, 1000),
	(10002, 1001, 1, 0, 5001, 1),
	(10003, 1001, 2, 0, 5002, 1);

INSERT INTO task_reward_option_entries
	(reward_set_id, option_id, reward_id)
VALUES
	(1001, 1, 10001),
	(1001, 2, 10002),
	(1001, 3, 10003);
```

## Completion and claim lifecycle

Accepting a selectable task reserves a fresh, server-owned occurrence token in
`character_task_reward_instances`. This does not modify
`character_tasks.accepted_time`; the gameplay timestamp keeps its established
timer and task-history meaning. A task that was already active when the
migration was installed gets its occurrence token lazily at completion.

Completion persists that token as `source_instance_id` in
`character_task_reward_selections` before the completed task can be removed
from `character_tasks`. The token distinguishes separate completions of a
repeatable task even when they were accepted during the same second.
`accepted_time` is copied into both rows only as diagnostic metadata and is not
used as the reward identity.

Creating that pending selection is also the at-most-once gate for the task's
legacy cash, experience, faction, and reward-point fields. If task removal
fails after completion and the server recovers the same pending selection,
those ancillary fields are not replayed.

Those legacy ancillary paths do not have a per-entry delivery ledger. A crash
after the pending-selection gate commits but before every ancillary side effect
finishes can therefore require operator correction. Automatically replaying
the fields would exchange that loss window for a duplicate-grant window, so
the selector keeps the established at-most-once boundary. Fully crash-safe
recovery for those fields requires a separate durable grant ledger and is not
part of this feature.

Completion then opens a copied snapshot on the claimable lane. Client requests
are handled as follows:

- Action 1 inspects an item in the matching tab snapshot.
- Action 3 claims one option. Provider validation checks the character,
  pending row, occurrence token, set, and option before delivery.
- Action 4 requests a read-only task preview on `0x6471`.
- Action 6 rebuilds all eligible pending and retryable tabs on `0x6411`.

The selected option becomes immutable on the first claim attempt. A claim
grants every common option plus the selected non-common option.
`character_task_rewards` is the per-entry delivery ledger, so a confirmed
entry is not granted again during a retry.

RoF2 action `7` replaces the claimable manager with a count followed by one
reward record per tab. A successful claim identifies its tab by pending reward
ID and reward-set ID; the other tabs remain claimable.

Although database option IDs are scoped by reward set, the RoF2 manager's
detail cache treats them as global within the displayed collection. The zone
therefore assigns collection-unique wire option IDs and translates inspection
and claim requests back to the provider's canonical option ID. Reusing option
IDs between independent providers is safe.

Selection status values are:

| Status | Meaning | Automatically restored |
| ---: | --- | --- |
| 0 | Pending or claim in progress | Only while `selected_option_id = 0` |
| 1 | Fully delivered | No |
| 2 | Explicit, retryable delivery failure | Yes |
| 3 | Ambiguous delivery result | No |

An ambiguous result is deliberately not retried automatically because the
underlying grant may already have committed. It requires operator review.
On login or an action-6 restore request, an interrupted status-zero claim with
a selected option is reconciled from its entry ledger. Any in-flight entry
quarantines the selection as ambiguous; otherwise it becomes retryable and
already-delivered entries are skipped.

## Operational notes

Install content migration `9352` and character migration `9353` before starting
a zone that includes this support. The character migration normalizes existing
task-reward rows to occurrence tokens and removes the obsolete same-second
identity. Task reload validates and replaces the in-memory reward definitions;
active client sessions hold copied definitions, so a reload cannot leave
dangling pointers. Pending rows remain the durable authority across zoning,
disconnects, and server restarts.

When changing selectable reward content, avoid reusing a reward, option, or set
ID for a different semantic grant. Stable IDs are part of claim validation and
idempotency.
