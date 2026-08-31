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

Perl and Lua quests can build a transient, manual offer on the claimable lane.
Every builder method returns false when its input or the client is invalid.
The feature is RoF2-only, so scripts should check the return values and provide
another interaction for older clients when needed.

| Client method | Purpose |
| --- | --- |
| **CreateRewardSelection(selection_id, title)** | Starts a new draft and clears any previous scripted offer. |
| **AddRewardSelectionOption(option_id, label[, common_to_all])** | Adds a unique option. At least one option must not be common. |
| **AddRewardSelectionReward(option_id, reward_type, value[, secondary_amount][, description])** | Adds one display reward using the named types below. A description may be supplied directly as the fourth argument when no secondary amount is needed. |
| **OpenRewardSelection()** | Validates and sends the completed draft. |
| **ClearRewardSelection()** | Removes the draft and any open scripted offer. |
| **HasRewardSelection()** | Reports whether a draft or open scripted offer exists. |

`AddRewardSelectionReward` has one name and four overloads in both languages:

- `(option_id, reward_type, value)`
- `(option_id, reward_type, value, description)`
- `(option_id, reward_type, value, secondary_amount)`
- `(option_id, reward_type, value, secondary_amount, description)`

The canonical reward types are:

| `reward_type` | `value` | `secondary_amount` |
| --- | --- | --- |
| **item** | Item ID | Optional quantity; defaults to 1 |
| **experience** | Experience amount using normal AA allocation | Omit |
| **experience_no_aa** | Fixed normal experience amount that preserves AA experience | Omit |
| **aa** | AA points | Omit |
| **money** | Total copper | Omit |
| **alternate_currency** | Currency ID | Required currency amount |
| **title** | Title-set ID | Omit |

Reward type names are case-insensitive. Spaces, hyphens, and underscores are
treated equivalently, so `alternate_currency`, `Alternate Currency`, and
`alternate-currency` all select the same type. Unknown names, unexpected
secondary amounts, zero values, and out-of-range values return false without
changing the draft.

Selection IDs, option IDs, item IDs, currency IDs, and title-set IDs must be
nonzero unsigned 32-bit values. Option IDs need only be unique within the
offer. Every option must contain at least one display reward.
OpenRewardSelection leaves an invalid draft intact so a script can correct or
clear it.

The builder rejects values outside the selector and task-delivery envelopes:
item quantities are limited to 32,767; experience to 4,294,967,295; AA and
alternate currency to 2,147,483,647; total coin to 2,147,483,647,999 copper;
and title-set IDs to 2,147,483,647. A scripted offer does not reserve inventory,
check balances, or verify content IDs. The event handler owns those
character-specific checks and must return success only after its grant
succeeds.

These methods describe what the client displays; they do not grant anything.
When the player chooses a non-common option, the server calls
EVENT_REWARD_SELECT in Perl or event_reward_select in Lua. An NPC quest that
created the offer receives the event while its originating entity and NPC type
still match. If there is no originating NPC handler, the player quest is the
fallback. The event receives:

| Language/context | Selection ID | Chosen option ID | Client |
| --- | --- | --- | --- |
| Perl NPC or player | **$reward_selection_id** | **$reward_option_id** | **$client** |
| Lua NPC | **e.selection_id** | **e.option_id** | **e.other** |
| Lua player | **e.selection_id** | **e.option_id** | **e.self** |

The handler must grant the reward and return a nonzero value only after it
succeeds. Returning 0, omitting the handler, or raising a script error rejects
the claim and reopens the offer. Claim retries are limited to one attempt every
500 milliseconds per client. A common option is displayed with every choice,
but the event reports only the chosen non-common option ID; the handler is
responsible for granting both portions. Normal local, global, and encounter
event routing still applies, so define only one granting handler for an offer.

Scripted offers intentionally have no database ledger. Zoning, disconnecting,
or restarting the zone discards them. Keep handlers short and idempotent:
multi-step grants can be partially applied if a script fails between steps,
and returning 0 permits the player to try again.

### Perl example

~~~perl
sub EVENT_SAY {
	return unless $text =~ /reward/i;

	unless (
		$client->CreateRewardSelection(9001, "Choose a Reward") &&
		$client->AddRewardSelectionOption(1, "A polished sword") &&
		$client->AddRewardSelectionReward(1, "item", 1001, 1) &&
		$client->AddRewardSelectionOption(2, "Experience") &&
		$client->AddRewardSelectionReward(2, "experience", 5000)
	) {
		$client->ClearRewardSelection();
		return;
	}

	$client->OpenRewardSelection();
}

sub EVENT_REWARD_SELECT {
	return 0 unless $reward_selection_id == 9001;

	if ($reward_option_id == 1) {
		$client->SummonItem(1001, 1);
	}
	elsif ($reward_option_id == 2) {
		$client->AddEXP(5000);
	}
	else {
		return 0;
	}

	return 1;
}
~~~

### Lua example

~~~lua
function event_say(e)
	if not e.message:lower():find("reward", 1, true) then
		return
	end

	local client = e.other
	local valid =
		client:CreateRewardSelection(9001, "Choose a Reward") and
		client:AddRewardSelectionOption(1, "A polished sword") and
		client:AddRewardSelectionReward(1, "item", 1001, 1) and
		client:AddRewardSelectionOption(2, "Experience") and
		client:AddRewardSelectionReward(2, "experience", 5000)

	if not valid then
		client:ClearRewardSelection()
		return
	end

	client:OpenRewardSelection()
end

function event_reward_select(e)
	if e.selection_id ~= 9001 then
		return 0
	end

	if e.option_id == 1 then
		e.other:SummonItem(1001, 1)
	elseif e.option_id == 2 then
		e.other:AddEXP(5000)
	else
		return 0
	end

	return 1
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
non-common option.

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

RoF2 supplies the visible list label **Player flags** for title/text rewards.
The row's `description` is shown in the lower detail pane, so title content
should use text such as `Unlocks the prefix and suffix titles Gatebreaker and
the Gatebreaker`.

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
