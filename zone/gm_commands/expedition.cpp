#include "zone/client.h"
#include "zone/dialogue_window.h"
#include "zone/expedition_config.h"
#include "zone/expedition_db.h"
#include "zone/event_codes.h"
#include "zone/npc.h"
#include "zone/quest_parser_collection.h"
#include "zone/zone.h"
#include "zone/zonedb.h"

#include "common/say_link.h"
#include "common/strings.h"
#include "common/zone_store.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {
	const ExpeditionDB::Template* SelectedTemplate(Client* c)
	{
		if (!c) {
			return nullptr;
		}

		return ExpeditionDB::FindTemplate(ExpeditionDB::GetBuilderState(c->CharacterID()).selected_template_id);
	}

	const ExpeditionDB::Template* ResolveTemplate(Client* c, const char* value)
	{
		if (value && value[0] != '\0') {
			return ExpeditionDB::FindTemplate(value);
		}

		return SelectedTemplate(c);
	}

	const ExpeditionDB::Event* SelectedEvent(Client* c, const ExpeditionDB::Template& template_data)
	{
		const auto& state = ExpeditionDB::GetBuilderState(c->CharacterID());
		if (state.selected_event_id) {
			auto it = std::ranges::find_if(template_data.events, [&](const ExpeditionDB::Event& event_data) {
				return event_data.id == state.selected_event_id;
			});
			if (it != template_data.events.end()) {
				return &*it;
			}
		}

		return template_data.events.empty() ? nullptr : &template_data.events.front();
	}

	NPC* TargetNpc(Client* c)
	{
		if (!c || !c->GetTarget() || !c->GetTarget()->IsNPC()) {
			return nullptr;
		}

		return c->GetTarget()->CastToNPC();
	}

	void NeedSelection(Client* c)
	{
		c->Message(Chat::Red, "Select an expedition first: #expedition select <id|name>, or create one with #expedition create \"Name\".");
	}

	void NeedTargetNpc(Client* c)
	{
		c->Message(Chat::Red, "Target an NPC first.");
	}

	std::string OnOff(bool value)
	{
		return value ? "on" : "off";
	}

	std::string TailArg(const char* value)
	{
		std::string out = value ? value : "";
		Strings::Trim(out);
		if (out.size() >= 2 && out.front() == '"' && out.back() == '"') {
			out = out.substr(1, out.size() - 2);
		}
		else {
			if (!out.empty() && out.front() == '"') {
				out.erase(out.begin());
			}
			if (!out.empty() && out.back() == '"') {
				out.pop_back();
			}
		}
		Strings::Trim(out);
		return out;
	}

	bool IsStrictUnsigned(const char* value)
	{
		if (!value || value[0] == '\0') {
			return false;
		}

		for (const char* c = value; *c; ++c) {
			if (!std::isdigit(static_cast<unsigned char>(*c))) {
				return false;
			}
		}

		return true;
	}

	bool ParseOnOffArg(const char* value, bool& out)
	{
		if (!value || value[0] == '\0') {
			return false;
		}

		if (
			Strings::EqualFold(value, "on") ||
			Strings::EqualFold(value, "true") ||
			Strings::EqualFold(value, "yes") ||
			Strings::EqualFold(value, "1")
		) {
			out = true;
			return true;
		}

		if (
			Strings::EqualFold(value, "off") ||
			Strings::EqualFold(value, "false") ||
			Strings::EqualFold(value, "no") ||
			Strings::EqualFold(value, "0")
		) {
			out = false;
			return true;
		}

		return false;
	}

	bool IsClearDurationArg(const char* value)
	{
		return value && (
			Strings::EqualFold(value, "none") ||
			Strings::EqualFold(value, "off") ||
			Strings::EqualFold(value, "clear") ||
			Strings::EqualFold(value, "0")
		);
	}

	bool IsRequestModeArg(const char* value)
	{
		return value && (
			Strings::EqualFold(value, "db_only") ||
			Strings::EqualFold(value, "db") ||
			Strings::EqualFold(value, "script_only") ||
			Strings::EqualFold(value, "script") ||
			Strings::EqualFold(value, "script_can_opt_in") ||
			Strings::EqualFold(value, "script_opt_in") ||
			Strings::EqualFold(value, "opt_in")
		);
	}

	std::string CanonicalRequestMode(const char* value)
	{
		if (Strings::EqualFold(value, "script_only") || Strings::EqualFold(value, "script")) {
			return "script_only";
		}

		if (Strings::EqualFold(value, "script_can_opt_in") || Strings::EqualFold(value, "script_opt_in") || Strings::EqualFold(value, "opt_in")) {
			return "script_can_opt_in";
		}

		return "db_only";
	}

	std::string Duration(uint32_t seconds)
	{
		if (!seconds) {
			return "none";
		}

		return Strings::SecondsToTime(seconds);
	}

	std::string Location(uint32_t zone_id, float x, float y, float z, float h = 0.0f)
	{
		return fmt::format("{} ({:.2f}, {:.2f}, {:.2f}, h {:.2f})", ZoneName(zone_id, true), x, y, z, h);
	}

	std::string RequestModeDescription(const std::string& mode)
	{
		if (Strings::EqualFold(mode, "script_only")) {
			return "script_only: scripts fully own request handling; automatic DB request NPCs are ignored.";
		}

		if (Strings::EqualFold(mode, "script_can_opt_in")) {
			return "script_can_opt_in: scripts own dialogue and may explicitly call DB template APIs.";
		}

		return "db_only: configured request NPCs automatically create DB expeditions.";
	}

	std::string RequestModeLabel(const std::string& mode)
	{
		if (Strings::EqualFold(mode, "script_only")) {
			return "Script-only request handling";
		}

		if (Strings::EqualFold(mode, "script_can_opt_in")) {
			return "Script opt-in request handling";
		}

		return "DB automatic request handling";
	}

	std::string StatusLabel(const ExpeditionDB::ValidationResult& validation)
	{
		if (!validation.errors.empty()) {
			return "Invalid";
		}

		if (!validation.warnings.empty()) {
			return "Valid, with warnings";
		}

		return "Valid";
	}

	std::string PopupSafe(std::string value)
	{
		Strings::FindReplace(value, "&", "&amp;");
		Strings::FindReplace(value, "<", "&lt;");
		Strings::FindReplace(value, ">", "&gt;");
		Strings::FindReplace(value, "\"", "&quot;");
		return value;
	}

	std::string NpcLabel(NPC& npc)
	{
		return fmt::format("{} (type {}, spawn {})", npc.GetCleanName(), npc.GetNPCTypeID(), npc.GetSpawnPointID());
	}

	const ExpeditionDB::EventNpc* FindEventNpc(const ExpeditionDB::Event& event_data, NPC& npc)
	{
		auto it = std::ranges::find_if(event_data.npcs, [&](const ExpeditionDB::EventNpc& event_npc) {
			if (event_npc.spawn2_id != 0 && event_npc.spawn2_id != npc.GetSpawnPointID()) {
				return false;
			}

			return event_npc.npc_type_id == npc.GetNPCTypeID();
		});

		return it != event_data.npcs.end() ? &*it : nullptr;
	}

	bool RequestNpcMatches(const ExpeditionDB::RequestNpc& request_npc, NPC& npc)
	{
		if (!request_npc.enabled || !zone || request_npc.zone_id != zone->GetZoneID()) {
			return false;
		}

		if (request_npc.spawn2_id != 0 && request_npc.spawn2_id != npc.GetSpawnPointID()) {
			return false;
		}

		return request_npc.npc_type_id == npc.GetNPCTypeID();
	}

	void SendActionGroup(Client* c, const std::string& title, const std::vector<std::pair<std::string, std::string>>& actions)
	{
		if (!c || actions.empty()) {
			return;
		}

		std::vector<std::string> links;
		links.reserve(actions.size());
		for (const auto& action : actions) {
			links.emplace_back(Saylink::Silent(action.first, action.second));
		}

		c->Message(Chat::White, fmt::format("{}: {}", title, Strings::Join(links, " | ")).c_str());
	}

	void SendSelectedEventChatUi(Client* c, const ExpeditionDB::Template& template_data)
	{
		const auto* event_data = SelectedEvent(c, template_data);
		if (!event_data) {
			SendActionGroup(c, "Event setup", {
				{"#expedition event add \"Boss Defeated\"", "Add Boss Event"},
				{"#expedition event list", "List Events"}
			});
			return;
		}

		c->Message(Chat::Yellow, fmt::format(
			"Active event [{}] lockout [{}] replay [{}] NPCs [{}] actions [{}].",
			event_data->event_name,
			Duration(event_data->lockout_seconds),
			Duration(event_data->replay_lockout_seconds),
			event_data->npcs.size(),
			event_data->actions.size()
		).c_str());
		if (NPC* npc = TargetNpc(c)) {
			c->Message(Chat::Yellow, fmt::format("Event target candidate: {}.", NpcLabel(*npc)).c_str());
			if (const auto* mapped_npc = FindEventNpc(*event_data, *npc)) {
				c->Message(Chat::Yellow, fmt::format(
					"Target event mapping: role [{}] loot [{}] complete-on-death [{}].",
					mapped_npc->role,
					OnOff(mapped_npc->loot_protected),
					OnOff(mapped_npc->complete_on_death)
				).c_str());
			}
			else {
				c->Message(Chat::Yellow, "Target event mapping: not mapped yet.");
			}
		}

		SendActionGroup(c, "Selected event", {
			{"#expedition event npc", "Add Target as Event NPC"},
			{"#expedition event npc boss", "Mark Target Boss"},
			{"#expedition event npc add", "Mark Target Encounter Add"},
			{"#expedition event npc chest", "Mark Target Loot Chest"},
			{"#expedition event loot on", "Target Loot On"},
			{"#expedition event loot off", "Target Loot Off"},
			{"#expedition event completeondeath on", "Target Death On"},
			{"#expedition event completeondeath off", "Target Death Off"}
		});

		SendActionGroup(c, "Event timing", {
			{"#expedition event lockout 6h", "6h Lockout"},
			{"#expedition event replay 2h", "2h Replay"},
			{"#expedition action", "Action Catalog"},
			{"#expedition action list", "List Actions"},
			{"#expedition event remove", "Delete Event..."}
		});

		SendActionGroup(c, "Next", {
			{"#expedition menu", "Snapshot"},
			{"#expedition validate", "Validate"},
			{"#expedition preview", "Preview"},
			{"#expedition enable", "Publish"}
		});
	}

	void SendBuilderChatUi(Client* c, const ExpeditionDB::Template& template_data)
	{
		const auto validation = ExpeditionDB::ValidateTemplate(template_data);
		c->Message(Chat::Yellow, fmt::format(
			"Expedition Builder: [{}] status [{}] enabled [{}].",
			template_data.name,
			validation.StatusName(),
			OnOff(template_data.enabled)
		).c_str());
		if (NPC* npc = TargetNpc(c)) {
			c->Message(Chat::Yellow, fmt::format("Current target NPC: {}.", NpcLabel(*npc)).c_str());
		}
		else {
			c->Message(Chat::Yellow, "Current target NPC: none. Target an NPC before using request/event target actions.");
		}

		SendActionGroup(c, "1 Base setup", {
			{"#expedition set zone", "Use Current Zone"},
			{"#expedition set zonein", "Use Current Zone-In"},
			{"#expedition set safereturn", "Use Current Safe Return"},
			{"#expedition set compass", "Use Current Compass"},
			{"#expedition preset solo", "Solo Preset"},
			{"#expedition preset group", "Group Preset"},
			{"#expedition preset raid", "Raid Preset"}
		});

		SendActionGroup(c, "2 Request NPCs", {
			{"#expedition requestnpc", "Add Target as Request NPC"},
			{"#expedition requestnpc list", "List"},
			{"#expedition requestnpc remove", "Remove Target..."},
			{"#expedition set requestmode db_only", "DB Only"},
			{"#expedition set requestmode script_can_opt_in", "Script Opt-In"},
			{"#expedition set requestmode script_only", "Script Only"}
		});

		SendActionGroup(c, "3 Events", {
			{"#expedition event add \"Boss Defeated\"", "Add Boss Event"},
			{"#expedition event list", "List Events"},
			{"#expedition preset boss", "Boss Preset"},
			{"#expedition preset chest", "Chest Preset"}
		});

		SendActionGroup(c, "4 Options", {
			{"#expedition set silent on", "Silent On"},
			{"#expedition set silent off", "Silent Off"},
			{"#expedition set replay 2h", "2h Replay"},
			{"#expedition set switchid target", "Use Target Switch"},
			{"#expedition set", "Setup Catalog"}
		});

		for (const auto& event_data : template_data.events) {
			SendActionGroup(c, fmt::format("Event [{}]", event_data.event_name), {
				{fmt::format("#expedition event select {}", event_data.id), "Select"}
			});
		}

		SendSelectedEventChatUi(c, template_data);

		SendActionGroup(c, "5 Validate/Test", {
			{"#expedition menu", "Snapshot"},
			{"#expedition validate", "Validate"},
			{"#expedition fix", "Fixes"},
			{"#expedition preview", "Preview"},
			{"#expedition test request", "Simulate Request"}
		});
		c->Message(Chat::White, "Live test commands require typing confirm: #expedition test create confirm, #expedition test move confirm, #expedition test lockout confirm, #expedition test loot confirm.");

		SendActionGroup(c, "6 Publish", {
			{"#expedition enable", "Enable"},
			{"#expedition disable", "Disable"},
			{fmt::format("#expedition delete {}", template_data.id), "Delete..."}
		});

		SendActionGroup(c, "Advanced", {
			{"#expedition wizard", "Wizard"},
			{"#expedition reload", "Reload DB Templates"}
		});
	}

	void SendCurrentBuilderChatUi(Client* c)
	{
		if (const auto* template_data = SelectedTemplate(c)) {
			SendBuilderChatUi(c, *template_data);
		}
	}

	void SendCurrentEventChatUi(Client* c)
	{
		if (const auto* template_data = SelectedTemplate(c)) {
			SendSelectedEventChatUi(c, *template_data);
		}
	}

	void AppendPopupText(std::string& body, const std::string& text)
	{
		static constexpr size_t max_popup_text_size = 3900;
		static constexpr const char* overflow_message = "<br><c \"#FBB117\">Additional snapshot rows omitted because this popup is full. Use #expedition preview for the expanded chat view.</c>";

		if (body.size() >= max_popup_text_size) {
			return;
		}

		if ((body.size() + text.size()) < max_popup_text_size) {
			body += text;
			return;
		}

		if (
			body.find("Additional snapshot rows omitted") == std::string::npos &&
			(body.size() + strlen(overflow_message)) < max_popup_text_size
		) {
			body += overflow_message;
		}
	}

	void AppendPopupSection(std::string& body, const std::string& title)
	{
		AppendPopupText(body, DialogueWindow::Break(2));
		AppendPopupText(body, DialogueWindow::ColorMessage("gold", title));
		AppendPopupText(body, DialogueWindow::Break());
	}

	std::string SnapshotRow(const std::string& label, const std::string& value)
	{
		return DialogueWindow::TableRow(DialogueWindow::TableCell(PopupSafe(label)) + DialogueWindow::TableCell(PopupSafe(value)));
	}

	std::string ZoneLabel(uint32_t zone_id)
	{
		return zone_id ? fmt::format("{} ({})", ZoneName(zone_id, true), zone_id) : "unset";
	}

	std::string NextStep(const ExpeditionDB::Template& template_data, const ExpeditionDB::ValidationResult& validation)
	{
		if (template_data.request_mode == "db_only" && template_data.request_npcs.empty()) {
			return "Target the request NPC, then use Add Target as Request NPC in chat.";
		}

		if (template_data.events.empty()) {
			return "Add an event, then target the boss or chest NPC and map it to that event.";
		}

		if (!validation.errors.empty()) {
			return "Fix validation errors before publishing.";
		}

		if (!template_data.enabled) {
			return "Ready to publish. Use Enable in chat after reviewing warnings.";
		}

		return "Published. Clone or disable before making risky live changes.";
	}

	void ShowHelp(Client* c)
	{
		c->Message(Chat::White, fmt::format("{} - Show this command catalog.", Saylink::Silent("#expedition help", "#expedition help")).c_str());
		c->Message(Chat::White, fmt::format(
			"{} - Open the expedition builder menu. {} - List templates. {} - Reload DB templates.",
			Saylink::Silent("#expedition menu", "#expedition menu"),
			Saylink::Silent("#expedition list", "#expedition list"),
			Saylink::Silent("#expedition reload", "#expedition reload")
		).c_str());
		c->Message(Chat::White, "#expedition create \"Name\" - Create and select an expedition using your current zone/location.");
		c->Message(Chat::White, "#expedition select <id|name> - Select a template for short follow-up commands.");
		c->Message(Chat::White, fmt::format(
			"{} - Show all setup fields. {} - Show request NPC commands. {} - Show event commands. {} - Show test commands.",
			Saylink::Silent("#expedition set", "#expedition set"),
			Saylink::Silent("#expedition requestnpc help", "#expedition requestnpc"),
			Saylink::Silent("#expedition event", "#expedition event"),
			Saylink::Silent("#expedition test", "#expedition test")
		).c_str());
		c->Message(Chat::White, fmt::format("{} - Check for missing or risky setup.", Saylink::Silent("#expedition validate", "#expedition validate")).c_str());
		c->Message(Chat::White, fmt::format(
			"{} - Guided setup popup. {} - Show validation fixes. {} - Preview runtime behavior.",
			Saylink::Silent("#expedition wizard", "#expedition wizard"),
			Saylink::Silent("#expedition fix", "#expedition fix"),
			Saylink::Silent("#expedition preview", "#expedition preview")
		).c_str());
		c->Message(Chat::White, fmt::format(
			"#expedition clone <id|name> \"Name\" - Copy an existing setup. {} - Apply common group defaults.",
			Saylink::Silent("#expedition preset group", "#expedition preset group")
		).c_str());
		c->Message(Chat::White, fmt::format(
			"{} - Publish the selected expedition. {} - Unpublish it.",
			Saylink::Silent("#expedition enable", "#expedition enable"),
			Saylink::Silent("#expedition disable", "#expedition disable")
		).c_str());
		c->Message(Chat::White, "#expedition delete <id|name> confirm - Delete an unpublished/editing template and its linked setup rows.");
	}

	void ShowSetHelp(Client* c)
	{
		c->Message(Chat::White, "#expedition set command catalog:");
		c->Message(Chat::White, "#expedition set zone - Set the selected expedition to your current zone and version.");
		c->Message(Chat::White, "#expedition set zone <zone_short_name|zone_id> [version] - Set an explicit expedition zone; version defaults to 0 unless it is your current zone.");
		c->Message(Chat::White, "#expedition set duration <duration> - Set expedition duration. Examples: 6h, 90m, 21600.");
		c->Message(Chat::White, "#expedition set players <min> <max> - Set player-count request limits.");
		c->Message(Chat::White, "#expedition set zonein - Set zone-in to your current location and heading.");
		c->Message(Chat::White, "#expedition set zonein <x> <y> <z> [h] - Set explicit zone-in coordinates.");
		c->Message(Chat::White, "#expedition set safereturn - Set safe return to your current zone/location/heading.");
		c->Message(Chat::White, "#expedition set safereturn <zone_short_name|zone_id> <x> <y> <z> [h] - Set explicit safe return.");
		c->Message(Chat::White, "#expedition set compass - Set the compass marker to your current zone/location.");
		c->Message(Chat::White, "#expedition set compass <zone_short_name|zone_id> <x> <y> <z> - Set an explicit compass marker.");
		c->Message(Chat::White, "#expedition set switchid target - Use your current target entity id as the dynamic-zone switch id.");
		c->Message(Chat::White, "#expedition set switchid <id> - Set a specific dynamic-zone switch id.");
		c->Message(Chat::White, "#expedition set replay none|<duration> - Set or clear the replay lockout awarded on creation.");
		c->Message(Chat::White, "#expedition set silent on|off - Toggle normal creation failure/success messages.");
		c->Message(Chat::White, "#expedition set requestmode db_only|script_can_opt_in|script_only - Choose how DB request NPCs interact with quest scripts.");
		SendActionGroup(c, "Common setup", {
			{"#expedition set zone", "Current Zone"},
			{"#expedition set zonein", "Current Zone-In"},
			{"#expedition set safereturn", "Current Safe Return"},
			{"#expedition set compass", "Current Compass"},
			{"#expedition preset group", "Group Preset"},
			{"#expedition menu", "Snapshot"}
		});
	}

	void ShowRequestNpcHelp(Client* c)
	{
		c->Message(Chat::White, "#expedition requestnpc command catalog:");
		c->Message(Chat::White, "#expedition requestnpc - Use your targeted NPC as the request NPC with phrase 'expedition'.");
		c->Message(Chat::White, "#expedition requestnpc <phrase> - Use your targeted NPC with a custom request phrase.");
		c->Message(Chat::White, "#expedition requestnpc list - List configured request NPCs for the selected expedition.");
		c->Message(Chat::White, "#expedition requestnpc remove - Prompt before removing your targeted NPC from request NPCs.");
		c->Message(Chat::White, "Request NPC commands use your selected expedition and current NPC target unless the command is list/help.");
		SendActionGroup(c, "Request NPCs", {
			{"#expedition requestnpc", "Add Target as Request NPC"},
			{"#expedition requestnpc list", "List"},
			{"#expedition requestnpc remove", "Remove Target..."},
			{"#expedition set requestmode db_only", "DB Only"},
			{"#expedition set requestmode script_can_opt_in", "Script Opt-In"},
			{"#expedition set requestmode script_only", "Script Only"}
		});
	}

	void ShowEventHelp(Client* c)
	{
		c->Message(Chat::White, "#expedition event command catalog:");
		c->Message(Chat::White, "#expedition event add \"Event Name\" - Add a DB event and select it.");
		c->Message(Chat::White, "#expedition event select <id|name> - Select an event for short follow-up commands.");
		c->Message(Chat::White, "#expedition event list - List events on the selected expedition.");
		c->Message(Chat::White, "#expedition event remove confirm - Delete the selected event and its NPC/action mappings.");
		c->Message(Chat::White, "#expedition event lockout <duration> - Set the selected event lockout duration.");
		c->Message(Chat::White, "#expedition event replay none|<duration> - Set or clear replay lockout awarded when the selected event completes.");
		c->Message(Chat::White, "#expedition event npc - Add your targeted NPC to the selected event and infer a role.");
		c->Message(Chat::White, "#expedition event npc boss|add|chest - Add your targeted NPC with an explicit role.");
		c->Message(Chat::White, "#expedition event loot on|off - Toggle loot-event protection for your targeted NPC.");
		c->Message(Chat::White, "#expedition event completeondeath on|off - Toggle whether your targeted NPC completes the selected event on death.");
		c->Message(Chat::White, "#expedition action add lock|unlock|lockout|replay|depop|message|remaining [value] - Add selected-event runtime actions.");
		c->Message(Chat::White, "#expedition action list - List selected-event runtime actions.");
		c->Message(Chat::White, "#expedition action clear - Prompt for selected-event action removal. #expedition action clear confirm - Remove them.");
		SendActionGroup(c, "Event setup", {
			{"#expedition event add \"Boss Defeated\"", "Add Boss Event"},
			{"#expedition event list", "List Events"},
			{"#expedition event npc", "Add Target as Event NPC"},
			{"#expedition event npc boss", "Mark Target Boss"},
			{"#expedition event npc chest", "Mark Target Chest"},
			{"#expedition event loot on", "Protect Target Loot"},
			{"#expedition event completeondeath on", "Complete On Death"}
		});
		if (const auto* template_data = SelectedTemplate(c)) {
			SendSelectedEventChatUi(c, *template_data);
		}
	}

	void ShowTestHelp(Client* c)
	{
		c->Message(Chat::White, "#expedition test command catalog:");
		c->Message(Chat::White, "#expedition test create confirm - Create the selected DB expedition for your current group/raid/self.");
		c->Message(Chat::White, "#expedition test move confirm - Move you into your current expedition.");
		c->Message(Chat::White, fmt::format("{} - Simulate the targeted request NPC phrase flow.", Saylink::Silent("#expedition test request", "#expedition test request")).c_str());
		c->Message(Chat::White, "#expedition test lockout confirm - Apply the selected event lockout to your current expedition.");
		c->Message(Chat::White, "#expedition test loot confirm - Re-apply DB loot-event protection for your expedition.");
	}

	void ShowPresetHelp(Client* c)
	{
		c->Message(Chat::White, "#expedition preset command catalog:");
		c->Message(Chat::White, "#expedition preset solo - 1 player, 90 minute duration, 30 minute replay.");
		c->Message(Chat::White, "#expedition preset group - 1-6 players, 6 hour duration, 2 hour replay.");
		c->Message(Chat::White, "#expedition preset raid - 6-54 players, 6 hour duration, 2 hour replay.");
		c->Message(Chat::White, "#expedition preset boss - Selected event gets 6 hour lockout, 2 hour replay, and targeted NPC as boss when targeted.");
		c->Message(Chat::White, "#expedition preset chest - Targeted NPC is added as loot-protected chest for selected event.");
		SendActionGroup(c, "Presets", {
			{"#expedition preset solo", "Solo"},
			{"#expedition preset group", "Group"},
			{"#expedition preset raid", "Raid"},
			{"#expedition preset boss", "Boss Event"},
			{"#expedition preset chest", "Loot Chest"}
		});
	}

	void ShowActionHelp(Client* c)
	{
		c->Message(Chat::White, "#expedition action command catalog:");
		c->Message(Chat::White, "#expedition action add lock - Lock the expedition when the selected event completes.");
		c->Message(Chat::White, "#expedition action add unlock - Unlock the expedition when the selected event completes.");
		c->Message(Chat::White, "#expedition action add lockout <duration> - Add a lockout for the selected event.");
		c->Message(Chat::White, "#expedition action add lockout <event_name_or_id> <duration> - Add a lockout for another event.");
		c->Message(Chat::White, "#expedition action add replay <duration> - Add or refresh the Replay Timer.");
		c->Message(Chat::White, "#expedition action add depop <npc_type_id> - Depop all NPCs of that type in the current zone.");
		c->Message(Chat::White, "#expedition action add message <text> - Message expedition members in the zone.");
		c->Message(Chat::White, "#expedition action add remaining <duration> - Set expedition remaining time.");
		c->Message(Chat::White, "#expedition action list - List actions for the selected event.");
		c->Message(Chat::White, "#expedition action clear - Prompt for selected-event action removal. #expedition action clear confirm - Remove selected-event actions.");
		SendActionGroup(c, "Runtime actions", {
			{"#expedition action add lock", "Lock"},
			{"#expedition action add unlock", "Unlock"},
			{"#expedition action add replay 2h", "2h Replay"},
			{"#expedition action add lockout 6h", "6h Lockout"},
			{"#expedition action add remaining 1h", "1h Remaining"},
			{"#expedition action list", "List Actions"},
			{"#expedition action clear", "Clear Actions..."}
		});
	}

	void PrintValidation(Client* c, const ExpeditionDB::Template& template_data)
	{
		const auto result = ExpeditionDB::ValidateTemplate(template_data);
		const uint32 color = result.IsValid() ? Chat::Green : Chat::Red;
		c->Message(color, fmt::format("Expedition [{}] validation: {}", template_data.name, result.StatusName()).c_str());

		for (const auto& error : result.errors) {
			c->Message(Chat::Red, fmt::format("Error: {}", error).c_str());
		}

		for (const auto& warning : result.warnings) {
			c->Message(Chat::Yellow, fmt::format("Warning: {}", warning).c_str());
		}
	}

	void ShowTemplate(Client* c, const ExpeditionDB::Template& template_data)
	{
		c->Message(Chat::White, fmt::format(
			"Expedition [{}] id [{}] dz_template [{}] enabled [{}] request_mode [{}]",
			template_data.name,
			template_data.id,
			template_data.dz_template_id,
			OnOff(template_data.enabled),
			template_data.request_mode
		).c_str());
		c->Message(Chat::White, fmt::format(
			"Zone [{}:{}] duration [{}] players [{}-{}] replay [{}] silent [{}]",
			ZoneName(template_data.dz_template.zone_id, true),
			template_data.dz_template.zone_version,
			Duration(template_data.dz_template.duration_seconds),
			template_data.dz_template.min_players,
			template_data.dz_template.max_players,
			Duration(template_data.replay_lockout_seconds),
			OnOff(template_data.silent)
		).c_str());
		c->Message(Chat::White, fmt::format(
			"Request NPCs [{}] events [{}]",
			template_data.request_npcs.size(),
			template_data.events.size()
		).c_str());
	}

	void ShowPreview(Client* c, const ExpeditionDB::Template& template_data)
	{
		c->Message(Chat::White, fmt::format("Preview: [{}] [{}]", template_data.name, template_data.enabled ? "enabled" : "disabled").c_str());
		c->Message(Chat::White, fmt::format("Request mode: {}", RequestModeDescription(template_data.request_mode)).c_str());
		c->Message(Chat::White, fmt::format(
			"Zone [{}:{}], duration [{}], players [{}-{}], replay [{}], silent [{}]",
			ZoneName(template_data.dz_template.zone_id, true),
			template_data.dz_template.zone_version,
			Duration(template_data.dz_template.duration_seconds),
			template_data.dz_template.min_players,
			template_data.dz_template.max_players,
			Duration(template_data.replay_lockout_seconds),
			OnOff(template_data.silent)
		).c_str());
		c->Message(Chat::White, fmt::format(
			"Zone-in: {}",
			Location(template_data.dz_template.zone_id, template_data.dz_template.zone_in_x, template_data.dz_template.zone_in_y, template_data.dz_template.zone_in_z, template_data.dz_template.zone_in_h)
		).c_str());
		c->Message(Chat::White, fmt::format(
			"Safe return: {}",
			Location(template_data.dz_template.return_zone_id, template_data.dz_template.return_x, template_data.dz_template.return_y, template_data.dz_template.return_z, template_data.dz_template.return_h)
		).c_str());
		c->Message(Chat::White, fmt::format(
			"Compass: {}",
			Location(template_data.dz_template.compass_zone_id, template_data.dz_template.compass_x, template_data.dz_template.compass_y, template_data.dz_template.compass_z)
		).c_str());

		for (const auto& request_npc : template_data.request_npcs) {
			const bool scripted = parse && parse->HasQuestSub(request_npc.npc_type_id, EVENT_SAY);
			c->Message(Chat::White, fmt::format(
				"Request NPC type [{}] spawn [{}] zone [{}] phrase [{}] {}",
				request_npc.npc_type_id,
				request_npc.spawn2_id,
				ZoneName(request_npc.zone_id, true),
				request_npc.phrase,
				scripted ? "(has EVENT_SAY script)" : ""
			).c_str());
		}

		for (const auto& event_data : template_data.events) {
			c->Message(Chat::White, fmt::format(
				"Event [{}] lockout [{}] replay [{}] npcs [{}] actions [{}]",
				event_data.event_name,
				Duration(event_data.lockout_seconds),
				Duration(event_data.replay_lockout_seconds),
				event_data.npcs.size(),
				event_data.actions.size()
			).c_str());
		}
	}

	void ShowFixes(Client* c, const ExpeditionDB::Template& template_data)
	{
		const auto validation = ExpeditionDB::ValidateTemplate(template_data);
		c->Message(Chat::White, fmt::format("Fix catalog for [{}]: {}", template_data.name, validation.StatusName()).c_str());
		for (const auto& error : validation.errors) {
			c->Message(Chat::Red, fmt::format("Error: {}", error).c_str());
		}
		for (const auto& warning : validation.warnings) {
			c->Message(Chat::Yellow, fmt::format("Warning: {}", warning).c_str());
		}
		c->Message(Chat::White, fmt::format("{} - Use your current location as zone-in.", Saylink::Silent("#expedition fix zonein", "#expedition fix zonein")).c_str());
		c->Message(Chat::White, fmt::format("{} - Use your current location as safe return.", Saylink::Silent("#expedition fix safereturn", "#expedition fix safereturn")).c_str());
		c->Message(Chat::White, fmt::format("{} - Add your targeted NPC as request NPC.", Saylink::Silent("#expedition fix requestnpc", "#expedition fix requestnpc")).c_str());
		c->Message(Chat::White, fmt::format("{} - Set a 2 hour replay lockout.", Saylink::Silent("#expedition fix replay 2h", "#expedition fix replay 2h")).c_str());
		c->Message(Chat::White, fmt::format("{} - Apply group defaults.", Saylink::Silent("#expedition preset group", "#expedition preset group")).c_str());
	}

	void ShowWizard(Client* c, const ExpeditionDB::Template* template_data)
	{
		std::string body;
		body += DialogueWindow::CenterMessage(DialogueWindow::ColorMessage("gold", "Expedition Wizard"));
		body += DialogueWindow::Break(2);
		if (!template_data) {
			body += "Start by creating an expedition from your current zone and location.";
			body += DialogueWindow::Break(2);
			body += "Use the action link in chat to create a new expedition.";
			c->SendPopupToClient("Expedition Wizard", body.c_str());
			SendActionGroup(c, "Expedition Wizard", {
				{"#expedition create \"New Expedition\"", "Create New Expedition"}
			});
			return;
		}

		const auto validation = ExpeditionDB::ValidateTemplate(*template_data);
		body += fmt::format("{} [{}]", DialogueWindow::ColorMessage("gold", template_data->name), validation.StatusName());
		body += DialogueWindow::Break(2);
		body += DialogueWindow::ColorMessage("gold", "1. Base Setup");
		body += DialogueWindow::Break();
		body += "Use Current Zone | Use Current Zone-In | Use Current Safe Return | Group Preset";
		body += DialogueWindow::Break(2);
		body += DialogueWindow::ColorMessage("gold", "2. Request Handling");
		body += DialogueWindow::Break();
		body += "Use Target Request NPC | DB Auto | Script Opt-In";
		body += DialogueWindow::Break(2);
		body += DialogueWindow::ColorMessage("gold", "3. Encounter");
		body += DialogueWindow::Break();
		body += "Add Event | Boss Preset | Chest Preset | Actions";
		body += DialogueWindow::Break(2);
		body += DialogueWindow::ColorMessage("gold", "4. Review");
		body += DialogueWindow::Break();
		body += "Preview | Fixes | Test Request | Enable";
		body += DialogueWindow::Break(2);
		body += "Clickable command links are sent to your chat window.";
		c->SendPopupToClient("Expedition Wizard", body.c_str());
		SendActionGroup(c, "Wizard base setup", {
			{"#expedition set zone", "Use Current Zone"},
			{"#expedition set zonein", "Use Current Zone-In"},
			{"#expedition set safereturn", "Use Current Safe Return"},
			{"#expedition preset group", "Group Preset"}
		});
		SendActionGroup(c, "Wizard request", {
			{"#expedition requestnpc", "Use Target Request NPC"},
			{"#expedition set requestmode db_only", "DB Auto"},
			{"#expedition set requestmode script_can_opt_in", "Script Opt-In"}
		});
		SendActionGroup(c, "Wizard encounter", {
			{"#expedition event add \"Boss Defeated\"", "Add Event"},
			{"#expedition preset boss", "Boss Preset"},
			{"#expedition preset chest", "Chest Preset"},
			{"#expedition action", "Actions"}
		});
		SendActionGroup(c, "Wizard review", {
			{"#expedition preview", "Preview"},
			{"#expedition fix", "Fixes"},
			{"#expedition test request", "Test Request"},
			{"#expedition enable", "Enable"}
		});
	}

	void ShowMenu(Client* c)
	{
		const auto* template_data = SelectedTemplate(c);
		std::string body;
		body += DialogueWindow::CenterMessage(DialogueWindow::ColorMessage("gold", "Expedition Builder"));
		body += DialogueWindow::Break(2);

		if (!template_data) {
			body += "No expedition selected.";
			body += DialogueWindow::Break(2);
			body += "Use the action link in chat to list expeditions.";
			c->SendPopupToClient("Expedition Builder", body.c_str());
			SendActionGroup(c, "Expedition Builder", {
				{"#expedition create \"New Expedition\"", "Create New Expedition"},
				{"#expedition list", "List Expeditions"}
			});
			return;
		}

		const auto validation = ExpeditionDB::ValidateTemplate(*template_data);
		std::string table;
		table += SnapshotRow("ID", std::to_string(template_data->id));
		table += SnapshotRow("Name", template_data->name);
		table += SnapshotRow("Status", StatusLabel(validation));
		table += SnapshotRow("Enabled", OnOff(template_data->enabled));
		table += SnapshotRow("Request Mode", RequestModeLabel(template_data->request_mode));
		table += SnapshotRow("Dynamic Zone Template", std::to_string(template_data->dz_template_id));
		table += SnapshotRow("Zone", fmt::format("{}:{}", ZoneLabel(template_data->dz_template.zone_id), template_data->dz_template.zone_version));
		table += SnapshotRow("Duration", Duration(template_data->dz_template.duration_seconds));
		table += SnapshotRow("Players", fmt::format("{}-{}", template_data->dz_template.min_players, template_data->dz_template.max_players));
		table += SnapshotRow("Replay", Duration(template_data->replay_lockout_seconds));
		table += SnapshotRow("Replay On Join", OnOff(template_data->replay_on_join));
		table += SnapshotRow("Silent", OnOff(template_data->silent));
		table += SnapshotRow("Switch Entity ID", std::to_string(template_data->dz_template.dz_switch_id));
		table += SnapshotRow("Current Target", TargetNpc(c) ? NpcLabel(*TargetNpc(c)) : "none");
		table += SnapshotRow("Next Step", NextStep(*template_data, validation));
		AppendPopupText(body, DialogueWindow::Table(table));

		AppendPopupSection(body, "Validation");
		table.clear();
		table += SnapshotRow("Status", StatusLabel(validation));
		table += SnapshotRow("Errors", std::to_string(validation.errors.size()));
		table += SnapshotRow("Warnings", std::to_string(validation.warnings.size()));
		AppendPopupText(body, DialogueWindow::Table(table));
		for (const auto& error : validation.errors) {
			AppendPopupText(body, DialogueWindow::Break() + DialogueWindow::ColorMessage("red", PopupSafe(fmt::format("Error: {}", error))));
		}
		for (const auto& warning : validation.warnings) {
			AppendPopupText(body, DialogueWindow::Break() + DialogueWindow::ColorMessage("gold", PopupSafe(fmt::format("Warning: {}", warning))));
		}

		AppendPopupSection(body, "Locations");
		table.clear();
		table += SnapshotRow("Zone-In", template_data->dz_template.override_zone_in ?
			Location(template_data->dz_template.zone_id, template_data->dz_template.zone_in_x, template_data->dz_template.zone_in_y, template_data->dz_template.zone_in_z, template_data->dz_template.zone_in_h) :
			"zone default");
		table += SnapshotRow("Safe Return", template_data->dz_template.return_zone_id ?
			Location(template_data->dz_template.return_zone_id, template_data->dz_template.return_x, template_data->dz_template.return_y, template_data->dz_template.return_z, template_data->dz_template.return_h) :
			"zone safe point/default");
		table += SnapshotRow("Compass", template_data->dz_template.compass_zone_id ?
			Location(template_data->dz_template.compass_zone_id, template_data->dz_template.compass_x, template_data->dz_template.compass_y, template_data->dz_template.compass_z) :
			"unset");
		AppendPopupText(body, DialogueWindow::Table(table));

		AppendPopupSection(body, "Request NPCs");
		if (template_data->request_npcs.empty()) {
			AppendPopupText(body, "None configured.");
		}
		else {
			table.clear();
			for (const auto& request_npc : template_data->request_npcs) {
				const bool scripted = parse && parse->HasQuestSub(request_npc.npc_type_id, EVENT_SAY);
				const std::string behavior = template_data->request_mode == "db_only" ?
					(scripted ? "scripted; DB auto defers" : "DB auto") :
					(template_data->request_mode == "script_can_opt_in" ? "script opt-in only" : "script-only");
				table += SnapshotRow(
					fmt::format("NPC {}", request_npc.id),
					fmt::format(
						"zone {}, type {}, spawn {}, phrase '{}', {}, {}",
						ZoneLabel(request_npc.zone_id),
						request_npc.npc_type_id,
						request_npc.spawn2_id,
						ExpeditionDB::NormalizePhrase(request_npc.phrase.empty() ? template_data->request_phrase : request_npc.phrase),
						request_npc.enabled ? "enabled" : "disabled",
						behavior
					)
				);
			}
			AppendPopupText(body, DialogueWindow::Table(table));
		}

		AppendPopupSection(body, fmt::format("Events ({})", template_data->events.size()));
		if (template_data->events.empty()) {
			AppendPopupText(body, "No events configured.");
		}
		else {
			const auto* active_event = SelectedEvent(c, *template_data);
			const uint32_t selected_event_id = active_event ? active_event->id : 0;
			const bool explicit_selection = ExpeditionDB::GetBuilderState(c->CharacterID()).selected_event_id != 0;
			for (const auto& event_data : template_data->events) {
				table.clear();
				table += SnapshotRow(
					"Event",
					fmt::format(
						"{}{} [{}]",
						event_data.event_name,
						event_data.id == selected_event_id ? (explicit_selection ? " (selected)" : " (active default)") : "",
						event_data.id
					)
				);
				table += SnapshotRow("Lockout", Duration(event_data.lockout_seconds));
				table += SnapshotRow("Replay", Duration(event_data.replay_lockout_seconds));
				table += SnapshotRow("Lock Success", OnOff(event_data.lock_on_success));
				table += SnapshotRow("Lock Failure", OnOff(event_data.lock_on_failure));
				table += SnapshotRow("Loot Protected", OnOff(event_data.loot_protected));
				table += SnapshotRow("NPCs", std::to_string(event_data.npcs.size()));
				table += SnapshotRow("Actions", std::to_string(event_data.actions.size()));
				AppendPopupText(body, DialogueWindow::Table(table));

				if (!event_data.npcs.empty()) {
					table.clear();
					for (const auto& event_npc : event_data.npcs) {
						table += SnapshotRow(
							fmt::format("NPC {}", event_npc.id),
							fmt::format(
								"role {}, type {}, spawn {}, death {}, loot {}",
								event_npc.role,
								event_npc.npc_type_id,
								event_npc.spawn2_id,
								OnOff(event_npc.complete_on_death),
								OnOff(event_npc.loot_protected)
							)
						);
					}
					AppendPopupText(body, DialogueWindow::Table(table));
				}

				if (!event_data.actions.empty()) {
					table.clear();
					for (const auto& event_action : event_data.actions) {
						table += SnapshotRow(
							fmt::format("Action {}", event_action.id),
							fmt::format("{} [{}] order {}", event_action.action_type, event_action.action_value, event_action.sort_order)
						);
					}
					AppendPopupText(body, DialogueWindow::Table(table));
				}
				AppendPopupText(body, DialogueWindow::Break());
			}
		}

		AppendPopupSection(body, "Chat Actions");
		AppendPopupText(body, "Clickable command links are sent to your chat window.");

		c->SendPopupToClient("Expedition Builder", body.c_str());
		SendBuilderChatUi(c, *template_data);
	}

	uint32_t ParseZoneArg(const char* arg)
	{
		if (!arg || arg[0] == '\0') {
			return zone ? zone->GetZoneID() : 0;
		}

		return IsStrictUnsigned(arg) ? Strings::ToUnsignedInt(arg) : ZoneID(arg);
	}
}

void command_expedition(Client* c, const Seperator* sep)
{
	if (!c || !zone) {
		return;
	}

	const std::string sub = Strings::ToLower(sep->arg[1]);
	if (sub.empty() || sub == "help") {
		ShowHelp(c);
		return;
	}

	if (sub == "reload") {
		zone->LoadDynamicZoneTemplates();
		c->Message(Chat::Green, fmt::format("Reloaded [{}] DB expedition template(s).", ExpeditionDB::Templates().size()).c_str());
		return;
	}

	if (sub == "menu") {
		if (sep->arg[2][0] != '\0') {
			const auto* template_data = ExpeditionDB::FindTemplate(TailArg(sep->argplus[2]));
			if (!template_data) {
				c->Message(Chat::Red, "Expedition template not found.");
				return;
			}
			ExpeditionDB::SetSelectedTemplate(c->CharacterID(), template_data->id);
		}
		ShowMenu(c);
		return;
	}

	if (sub == "list") {
		c->Message(Chat::White, fmt::format("DB expedition templates: [{}]", ExpeditionDB::Templates().size()).c_str());
		for (const auto& [id, template_data] : ExpeditionDB::Templates()) {
			if (strcasecmp(sep->arg[2], "all") != 0 && template_data.dz_template.zone_id != zone->GetZoneID()) {
				continue;
			}
			c->Message(Chat::White, fmt::format(
				"[{}] {} [{}:{}] {} {}",
				id,
				Saylink::Silent(fmt::format("#expedition select {}", id), template_data.name),
				ZoneName(template_data.dz_template.zone_id, true),
				template_data.dz_template.zone_version,
				template_data.enabled ? "enabled" : "disabled",
				Saylink::Silent(fmt::format("#expedition menu {}", id), "menu")
			).c_str());
		}
		return;
	}

	if (sub == "create") {
		std::string name = TailArg(sep->argplus[2]);
		if (name.empty()) {
			c->Message(Chat::Red, "Usage: #expedition create \"Name\"");
			return;
		}

		const uint32_t id = ExpeditionDB::CreateTemplateFromClient(content_db, *c, name);
		if (!id) {
			c->Message(Chat::Red, "Failed to create DB expedition template.");
			return;
		}

		ExpeditionDB::SetSelectedTemplate(c->CharacterID(), id);
		c->Message(Chat::Green, fmt::format("Created and selected DB expedition [{}].", id).c_str());
		ShowMenu(c);
		return;
	}

	if (sub == "clone") {
		const auto* source = strcasecmp(sep->arg[2], "current") == 0 ? SelectedTemplate(c) : ExpeditionDB::FindTemplate(sep->arg[2]);
		std::string name = TailArg(sep->argplus[3]);
		if (!source || name.empty()) {
			c->Message(Chat::Red, "Usage: #expedition clone <id|name|current> \"New Name\"");
			return;
		}

		const std::string source_name = source->name;
		const uint32_t id = ExpeditionDB::CloneTemplate(content_db, source->id, name);
		if (!id) {
			c->Message(Chat::Red, "Failed to clone DB expedition template.");
			return;
		}

		ExpeditionDB::SetSelectedTemplate(c->CharacterID(), id);
		c->Message(Chat::Green, fmt::format("Cloned expedition [{}] into [{}].", source_name, name).c_str());
		ShowMenu(c);
		return;
	}

	if (sub == "select") {
		const auto* template_data = ExpeditionDB::FindTemplate(TailArg(sep->argplus[2]));
		if (!template_data) {
			c->Message(Chat::Red, "Expedition template not found.");
			return;
		}

		ExpeditionDB::SetSelectedTemplate(c->CharacterID(), template_data->id);
		c->Message(Chat::Green, fmt::format("Selected expedition [{}] ({}).", template_data->id, template_data->name).c_str());
		SendBuilderChatUi(c, *template_data);
		return;
	}

	const auto* template_data = ResolveTemplate(c, sep->arg[2]);
	if (sub == "show") {
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		ShowTemplate(c, *template_data);
		return;
	}

	if (sub == "preview") {
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		ShowPreview(c, *template_data);
		return;
	}

	if (sub == "wizard") {
		ShowWizard(c, SelectedTemplate(c));
		return;
	}

	if (sub == "validate") {
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		PrintValidation(c, *template_data);
		return;
	}

	if (sub == "fix") {
		template_data = SelectedTemplate(c);
		if (!template_data) {
			NeedSelection(c);
			return;
		}

		const std::string action = Strings::ToLower(sep->arg[2]);
		if (action.empty() || action == "help") {
			ShowFixes(c, *template_data);
			return;
		}

		if (action == "zonein") {
			const auto& pos = c->GetPosition();
			ExpeditionDB::SetDzTemplateZoneIn(content_db, template_data->dz_template_id, pos.x, pos.y, pos.z, pos.w);
			c->Message(Chat::Green, "Set zone-in to your current location.");
		}
		else if (action == "safereturn") {
			const auto& pos = c->GetPosition();
			ExpeditionDB::SetDzTemplateSafeReturn(content_db, template_data->dz_template_id, zone->GetZoneID(), pos.x, pos.y, pos.z, pos.w);
			c->Message(Chat::Green, "Set safe return to your current location.");
		}
		else if (action == "requestnpc") {
			NPC* npc = TargetNpc(c);
			if (!npc) {
				NeedTargetNpc(c);
				return;
			}
			ExpeditionDB::UpsertRequestNpc(content_db, template_data->id, zone->GetZoneID(), npc->GetNPCTypeID(), npc->GetSpawnPointID(), "expedition");
			c->Message(Chat::Green, "Added targeted NPC as request NPC.");
		}
		else if (action == "replay") {
			const uint32_t seconds = ParseExpeditionDuration(sep->arg[3][0] ? sep->arg[3] : "2h");
			ExpeditionDB::SetTemplateReplay(content_db, template_data->id, seconds);
			c->Message(Chat::Green, fmt::format("Set replay lockout to [{}].", Duration(seconds)).c_str());
		}
		else {
			ShowFixes(c, *template_data);
		}
		SendCurrentBuilderChatUi(c);
		return;
	}

	if (sub == "enable" || sub == "disable") {
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		if (sub == "enable") {
			const auto validation = ExpeditionDB::ValidateTemplate(*template_data);
			if (!validation.IsValid()) {
				PrintValidation(c, *template_data);
				c->Message(Chat::Red, "Fix validation errors before enabling this expedition.");
				SendBuilderChatUi(c, *template_data);
				return;
			}
		}
		const std::string template_name = template_data->name;
		ExpeditionDB::SetTemplateEnabled(content_db, template_data->id, sub == "enable");
		c->Message(Chat::Green, fmt::format("{} expedition [{}].", sub == "enable" ? "Enabled" : "Disabled", template_name).c_str());
		SendCurrentBuilderChatUi(c);
		return;
	}

	if (sub == "delete") {
		const auto* delete_template = ResolveTemplate(c, sep->arg[2]);
		if (!delete_template) {
			NeedSelection(c);
			return;
		}
		if (delete_template->enabled) {
			c->Message(Chat::Red, "Disable this expedition before deleting it. This prevents accidental deletion of live DB templates.");
			return;
		}
		if (strcasecmp(sep->arg[3], "confirm") != 0 && strcasecmp(sep->arg[4], "confirm") != 0) {
			c->Message(Chat::Yellow, fmt::format("Type #expedition delete {} confirm to permanently delete this unpublished template.", delete_template->id).c_str());
			return;
		}
		ExpeditionDB::DeleteTemplate(content_db, delete_template->id);
		c->Message(Chat::Green, "Deleted DB expedition template.");
		return;
	}

	if (sub == "set") {
		const std::string field = Strings::ToLower(sep->arg[2]);
		if (field.empty() || field == "help") {
			ShowSetHelp(c);
			return;
		}
		template_data = SelectedTemplate(c);
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		if (field == "zone") {
			const uint32_t zone_id = ParseZoneArg(sep->arg[3]);
			if (sep->arg[4][0] != '\0' && !IsStrictUnsigned(sep->arg[4])) {
				c->Message(Chat::Red, "Usage: #expedition set zone <zone_short_name|zone_id> [version]");
				return;
			}
			const uint32_t version = IsStrictUnsigned(sep->arg[4]) ? Strings::ToUnsignedInt(sep->arg[4]) : (zone_id == zone->GetZoneID() ? zone->GetInstanceVersion() : 0);
			if (!zone_id) {
				c->Message(Chat::Red, "Invalid zone.");
				return;
			}
			ExpeditionDB::SetDzTemplateZone(content_db, template_data->dz_template_id, zone_id, version);
			c->Message(Chat::Green, fmt::format("Set expedition zone to [{}:{}].", ZoneName(zone_id, true), version).c_str());
		}
		else if (field == "duration") {
			const uint32_t seconds = ParseExpeditionDuration(sep->arg[3]);
			if (!seconds) {
				c->Message(Chat::Red, "Usage: #expedition set duration <duration>");
				return;
			}
			ExpeditionDB::SetDzTemplateDuration(content_db, template_data->dz_template_id, seconds);
			c->Message(Chat::Green, fmt::format("Set duration to [{}].", Duration(seconds)).c_str());
		}
		else if (field == "players") {
			if (!IsStrictUnsigned(sep->arg[3]) || !IsStrictUnsigned(sep->arg[4])) {
				c->Message(Chat::Red, "Usage: #expedition set players <min> <max>");
				return;
			}
			const uint32_t min_players = Strings::ToUnsignedInt(sep->arg[3]);
			const uint32_t max_players = Strings::ToUnsignedInt(sep->arg[4]);
			if (min_players == 0 || max_players < min_players) {
				c->Message(Chat::Red, "Player minimum must be at least 1 and maximum must be greater than or equal to minimum.");
				return;
			}
			ExpeditionDB::SetDzTemplatePlayers(content_db, template_data->dz_template_id, min_players, max_players);
			c->Message(Chat::Green, "Updated player bounds.");
		}
		else if (field == "zonein") {
			if (sep->arg[3][0] == '\0') {
				const auto& pos = c->GetPosition();
				ExpeditionDB::SetDzTemplateZoneIn(content_db, template_data->dz_template_id, pos.x, pos.y, pos.z, pos.w);
			}
			else if (sep->IsNumber(3) && sep->IsNumber(4) && sep->IsNumber(5)) {
				ExpeditionDB::SetDzTemplateZoneIn(
					content_db,
					template_data->dz_template_id,
					Strings::ToFloat(sep->arg[3]),
					Strings::ToFloat(sep->arg[4]),
					Strings::ToFloat(sep->arg[5]),
					sep->IsNumber(6) ? Strings::ToFloat(sep->arg[6]) : c->GetHeading()
				);
			}
			else {
				c->Message(Chat::Red, "Usage: #expedition set zonein OR #expedition set zonein <x> <y> <z> [h]");
				return;
			}
			c->Message(Chat::Green, "Set zone-in location.");
		}
		else if (field == "safereturn") {
			if (sep->arg[3][0] == '\0') {
				const auto& pos = c->GetPosition();
				ExpeditionDB::SetDzTemplateSafeReturn(content_db, template_data->dz_template_id, zone->GetZoneID(), pos.x, pos.y, pos.z, pos.w);
			}
			else if (sep->IsNumber(4) && sep->IsNumber(5) && sep->IsNumber(6)) {
				const uint32_t return_zone = ParseZoneArg(sep->arg[3]);
				if (!return_zone) {
					c->Message(Chat::Red, "Invalid safe return zone.");
					return;
				}
				ExpeditionDB::SetDzTemplateSafeReturn(
					content_db,
					template_data->dz_template_id,
					return_zone,
					Strings::ToFloat(sep->arg[4]),
					Strings::ToFloat(sep->arg[5]),
					Strings::ToFloat(sep->arg[6]),
					sep->IsNumber(7) ? Strings::ToFloat(sep->arg[7]) : c->GetHeading()
				);
			}
			else {
				c->Message(Chat::Red, "Usage: #expedition set safereturn OR #expedition set safereturn <zone_short_name|zone_id> <x> <y> <z> [h]");
				return;
			}
			c->Message(Chat::Green, "Set safe return location.");
		}
		else if (field == "compass") {
			if (sep->arg[3][0] == '\0') {
				const auto& pos = c->GetPosition();
				ExpeditionDB::SetDzTemplateCompass(content_db, template_data->dz_template_id, zone->GetZoneID(), pos.x, pos.y, pos.z);
			}
			else if (sep->IsNumber(4) && sep->IsNumber(5) && sep->IsNumber(6)) {
				const uint32_t compass_zone = ParseZoneArg(sep->arg[3]);
				if (!compass_zone) {
					c->Message(Chat::Red, "Invalid compass zone.");
					return;
				}
				ExpeditionDB::SetDzTemplateCompass(
					content_db,
					template_data->dz_template_id,
					compass_zone,
					Strings::ToFloat(sep->arg[4]),
					Strings::ToFloat(sep->arg[5]),
					Strings::ToFloat(sep->arg[6])
				);
			}
			else {
				c->Message(Chat::Red, "Usage: #expedition set compass OR #expedition set compass <zone_short_name|zone_id> <x> <y> <z>");
				return;
			}
			c->Message(Chat::Green, "Set compass location.");
		}
		else if (field == "switchid") {
			uint32_t switch_id = 0;
			if (strcasecmp(sep->arg[3], "target") == 0 && c->GetTarget()) {
				switch_id = c->GetTarget()->GetID();
			}
			else if (IsStrictUnsigned(sep->arg[3])) {
				switch_id = Strings::ToUnsignedInt(sep->arg[3]);
			}
			else {
				c->Message(Chat::Red, "Usage: #expedition set switchid <id|target>");
				return;
			}
			ExpeditionDB::SetDzTemplateSwitchID(content_db, template_data->dz_template_id, switch_id);
			c->Message(Chat::Green, fmt::format("Set switch id to [{}].", switch_id).c_str());
		}
		else if (field == "replay") {
			if (sep->arg[3][0] == '\0') {
				c->Message(Chat::Red, "Usage: #expedition set replay none|<duration>");
				return;
			}
			const uint32_t seconds = IsClearDurationArg(sep->arg[3]) ? 0 : ParseExpeditionDuration(sep->arg[3]);
			if (!seconds && !IsClearDurationArg(sep->arg[3])) {
				c->Message(Chat::Red, "Usage: #expedition set replay none|<duration>");
				return;
			}
			ExpeditionDB::SetTemplateReplay(content_db, template_data->id, seconds);
			c->Message(Chat::Green, fmt::format("Set replay lockout to [{}].", Duration(seconds)).c_str());
		}
		else if (field == "silent") {
			bool enabled = false;
			if (!ParseOnOffArg(sep->arg[3], enabled)) {
				c->Message(Chat::Red, "Usage: #expedition set silent on|off");
				return;
			}
			ExpeditionDB::SetTemplateSilent(content_db, template_data->id, enabled);
			c->Message(Chat::Green, fmt::format("Set silent to [{}].", OnOff(enabled)).c_str());
		}
		else if (field == "requestmode") {
			if (!IsRequestModeArg(sep->arg[3])) {
				c->Message(Chat::Red, "Usage: #expedition set requestmode db_only|script_can_opt_in|script_only");
				return;
			}
			const std::string canonical_mode = CanonicalRequestMode(sep->arg[3]);
			ExpeditionDB::SetTemplateRequestMode(content_db, template_data->id, canonical_mode);
			c->Message(Chat::Green, fmt::format("Set request mode to [{}].", canonical_mode).c_str());
		}
		else {
			c->Message(Chat::Red, "Unknown #expedition set option.");
			ShowSetHelp(c);
		}
		SendCurrentBuilderChatUi(c);
		return;
	}

	if (sub == "requestnpc") {
		const std::string action = Strings::ToLower(sep->arg[2]);
		if (action == "help") {
			ShowRequestNpcHelp(c);
			return;
		}

		template_data = SelectedTemplate(c);
		if (!template_data) {
			if (action.empty()) {
				ShowRequestNpcHelp(c);
			}
			NeedSelection(c);
			return;
		}

		if (action == "list") {
			if (template_data->request_npcs.empty()) {
				c->Message(Chat::White, "No request NPCs are configured for the selected expedition.");
				ShowRequestNpcHelp(c);
				return;
			}
			for (const auto& request_npc : template_data->request_npcs) {
				c->Message(Chat::White, fmt::format(
					"NPC type [{}] spawn [{}] zone [{}] phrase [{}]",
					request_npc.npc_type_id,
					request_npc.spawn2_id,
					request_npc.zone_id,
					request_npc.phrase
				).c_str());
			}
			ShowRequestNpcHelp(c);
			return;
		}

		NPC* npc = TargetNpc(c);
		if (!npc) {
			NeedTargetNpc(c);
			ShowRequestNpcHelp(c);
			return;
		}

		if (action == "remove") {
			if (strcasecmp(sep->arg[3], "confirm") != 0) {
				c->Message(Chat::Yellow, "Type #expedition requestnpc remove confirm to remove the targeted request NPC mapping.");
				return;
			}
			ExpeditionDB::DeleteRequestNpc(content_db, template_data->id, npc->GetNPCTypeID(), npc->GetSpawnPointID());
			c->Message(Chat::Green, "Removed targeted request NPC.");
			SendCurrentBuilderChatUi(c);
			return;
		}

		std::string phrase = TailArg(sep->argplus[2]);
		if (phrase.empty()) {
			phrase = "expedition";
		}
		ExpeditionDB::UpsertRequestNpc(content_db, template_data->id, zone->GetZoneID(), npc->GetNPCTypeID(), npc->GetSpawnPointID(), phrase);
		c->Message(Chat::Green, fmt::format("Set targeted NPC as request NPC with phrase [{}].", phrase).c_str());
		SendCurrentBuilderChatUi(c);
		return;
	}

	if (sub == "event") {
		const std::string action = Strings::ToLower(sep->arg[2]);
		if (action.empty() || action == "help") {
			ShowEventHelp(c);
			return;
		}
		template_data = SelectedTemplate(c);
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		if (action == "add") {
			std::string event_name = TailArg(sep->argplus[3]);
			if (event_name.empty()) {
				c->Message(Chat::Red, "Usage: #expedition event add \"Event Name\"");
				return;
			}
			const uint32_t event_id = ExpeditionDB::AddEvent(content_db, template_data->id, event_name);
			ExpeditionDB::SetSelectedEvent(c->CharacterID(), event_id);
			c->Message(Chat::Green, fmt::format("Added and selected event [{}].", event_name).c_str());
			SendCurrentEventChatUi(c);
			return;
		}

		if (action == "select") {
			const auto* event_data = ExpeditionDB::FindEvent(*template_data, TailArg(sep->argplus[3]));
			if (!event_data) {
				c->Message(Chat::Red, "Event not found.");
				return;
			}
			ExpeditionDB::SetSelectedEvent(c->CharacterID(), event_data->id);
			c->Message(Chat::Green, fmt::format("Selected event [{}].", event_data->event_name).c_str());
			SendCurrentEventChatUi(c);
			return;
		}

		if (action == "list") {
			if (template_data->events.empty()) {
				c->Message(Chat::White, "No events are configured for the selected expedition.");
				SendSelectedEventChatUi(c, *template_data);
				return;
			}
			for (const auto& event_data : template_data->events) {
				c->Message(Chat::White, fmt::format(
					"[{}] {} lockout [{}] replay [{}] npcs [{}] {}",
					event_data.id,
					event_data.event_name,
					Duration(event_data.lockout_seconds),
					Duration(event_data.replay_lockout_seconds),
					event_data.npcs.size(),
					Saylink::Silent(fmt::format("#expedition event select {}", event_data.id), "select")
				).c_str());
			}
			SendCurrentEventChatUi(c);
			return;
		}

		const auto* event_data = SelectedEvent(c, *template_data);
		if (!event_data) {
			c->Message(Chat::Red, "Add or select an event first.");
			return;
		}
		const uint32_t selected_event_id = event_data->id;
		const std::string selected_event_name = event_data->event_name;

		if (action == "remove") {
			if (strcasecmp(sep->arg[3], "confirm") != 0) {
				c->Message(Chat::Yellow, "Type #expedition event remove confirm to delete the selected event and its NPC/action mappings.");
				return;
			}
			ExpeditionDB::DeleteEvent(content_db, selected_event_id);
			c->Message(Chat::Green, fmt::format("Removed selected event [{}].", selected_event_name).c_str());
			SendCurrentBuilderChatUi(c);
			return;
		}

		if (action == "lockout") {
			const uint32_t seconds = ParseExpeditionDuration(sep->arg[3]);
			if (!seconds) {
				c->Message(Chat::Red, "Usage: #expedition event lockout <duration>");
				return;
			}
			ExpeditionDB::SetEventLockout(content_db, selected_event_id, seconds);
			c->Message(Chat::Green, fmt::format("Set event [{}] lockout to [{}].", selected_event_name, Duration(seconds)).c_str());
			SendCurrentEventChatUi(c);
			return;
		}

		if (action == "replay") {
			if (sep->arg[3][0] == '\0') {
				c->Message(Chat::Red, "Usage: #expedition event replay none|<duration>");
				return;
			}
			const uint32_t seconds = IsClearDurationArg(sep->arg[3]) ? 0 : ParseExpeditionDuration(sep->arg[3]);
			if (!seconds && !IsClearDurationArg(sep->arg[3])) {
				c->Message(Chat::Red, "Usage: #expedition event replay none|<duration>");
				return;
			}
			ExpeditionDB::SetEventReplay(content_db, selected_event_id, seconds);
			c->Message(Chat::Green, fmt::format("Set event [{}] replay lockout to [{}].", selected_event_name, Duration(seconds)).c_str());
			SendCurrentEventChatUi(c);
			return;
		}

		if (action == "npc" || action == "loot" || action == "completeondeath") {
			NPC* npc = TargetNpc(c);
			if (!npc) {
				NeedTargetNpc(c);
				return;
			}

			if (action == "npc") {
				std::string role = sep->arg[3][0] ? sep->arg[3] : ExpeditionDB::RoleFromTarget(*npc);
				if (!Strings::EqualFold(role, "boss") && !Strings::EqualFold(role, "add") && !Strings::EqualFold(role, "chest")) {
					c->Message(Chat::Red, "Usage: #expedition event npc boss|add|chest");
					return;
				}
				ExpeditionDB::SetEventNpc(content_db, selected_event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), role);
				c->Message(Chat::Green, fmt::format(
					"Added target NPC [{}] to event [{}] as [{}].",
					NpcLabel(*npc),
					selected_event_name,
					role
				).c_str());
			}
			else if (action == "loot") {
				bool enabled = true;
				if (sep->arg[3][0] != '\0' && !ParseOnOffArg(sep->arg[3], enabled)) {
					c->Message(Chat::Red, "Usage: #expedition event loot on|off");
					return;
				}
				if (!FindEventNpc(*event_data, *npc)) {
					ExpeditionDB::SetEventNpc(content_db, selected_event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), ExpeditionDB::RoleFromTarget(*npc));
				}
				ExpeditionDB::SetEventNpcLoot(content_db, selected_event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), enabled);
				c->Message(Chat::Green, fmt::format(
					"Set target NPC [{}] loot protection [{}] for event [{}].",
					NpcLabel(*npc),
					OnOff(enabled),
					selected_event_name
				).c_str());
			}
			else {
				bool enabled = true;
				if (sep->arg[3][0] != '\0' && !ParseOnOffArg(sep->arg[3], enabled)) {
					c->Message(Chat::Red, "Usage: #expedition event completeondeath on|off");
					return;
				}
				if (!FindEventNpc(*event_data, *npc)) {
					ExpeditionDB::SetEventNpc(content_db, selected_event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), ExpeditionDB::RoleFromTarget(*npc));
				}
				ExpeditionDB::SetEventNpcCompleteOnDeath(content_db, selected_event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), enabled);
				c->Message(Chat::Green, fmt::format(
					"Set target NPC [{}] complete-on-death [{}] for event [{}].",
					NpcLabel(*npc),
					OnOff(enabled),
					selected_event_name
				).c_str());
			}
			SendCurrentEventChatUi(c);
			return;
		}

		c->Message(Chat::Red, "Unknown #expedition event option.");
		ShowEventHelp(c);
		return;
	}

	if (sub == "action") {
		const std::string action = Strings::ToLower(sep->arg[2]);
		if (action.empty() || action == "help") {
			ShowActionHelp(c);
			return;
		}
		template_data = SelectedTemplate(c);
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		const auto* event_data = SelectedEvent(c, *template_data);
		if (!event_data) {
			c->Message(Chat::Red, "Add or select an event first.");
			return;
		}
		const uint32_t selected_event_id = event_data->id;
		const std::string selected_event_name = event_data->event_name;

		if (action == "list") {
			if (event_data->actions.empty()) {
				c->Message(Chat::White, "Selected event has no runtime actions.");
				SendSelectedEventChatUi(c, *template_data);
				return;
			}
			for (const auto& event_action : event_data->actions) {
				c->Message(Chat::White, fmt::format("[{}] {} [{}]", event_action.id, event_action.action_type, event_action.action_value).c_str());
			}
			SendSelectedEventChatUi(c, *template_data);
			return;
		}

		if (action == "clear") {
			if (strcasecmp(sep->arg[3], "confirm") != 0) {
				c->Message(Chat::Yellow, "Type #expedition action clear confirm to remove every runtime action on the selected event.");
				return;
			}
			ExpeditionDB::ClearActions(content_db, selected_event_id);
			c->Message(Chat::Green, fmt::format("Cleared runtime actions for event [{}].", selected_event_name).c_str());
			SendCurrentEventChatUi(c);
			return;
		}

		if (action != "add") {
			ShowActionHelp(c);
			return;
		}

		const std::string type = Strings::ToLower(sep->arg[3]);
		if (type == "lock" || type == "unlock") {
			ExpeditionDB::AddAction(content_db, selected_event_id, type, "");
			c->Message(Chat::Green, fmt::format("Added [{}] action to event [{}].", type, selected_event_name).c_str());
		}
		else if (type == "lockout") {
			std::string event_name = selected_event_name;
			std::string duration = sep->arg[4];
			if (sep->arg[5][0] != '\0') {
				event_name = TailArg(sep->arg[4]);
				duration = sep->arg[5];
			}
			const uint32_t seconds = ParseExpeditionDuration(duration);
			if (!seconds) {
				c->Message(Chat::Red, "Usage: #expedition action add lockout [event_name] <duration>");
				return;
			}
			ExpeditionDB::AddAction(content_db, selected_event_id, "add_lockout", fmt::format("{}|{}", event_name, seconds));
			c->Message(Chat::Green, fmt::format("Added lockout action [{}] for [{}] to event [{}].", Duration(seconds), event_name, selected_event_name).c_str());
		}
		else if (type == "replay") {
			const uint32_t seconds = ParseExpeditionDuration(sep->arg[4]);
			if (!seconds) {
				c->Message(Chat::Red, "Usage: #expedition action add replay <duration>");
				return;
			}
			ExpeditionDB::AddAction(content_db, selected_event_id, "add_replay_lockout", std::to_string(seconds));
			c->Message(Chat::Green, fmt::format("Added replay lockout action [{}] to event [{}].", Duration(seconds), selected_event_name).c_str());
		}
		else if (type == "depop") {
			if (!IsStrictUnsigned(sep->arg[4])) {
				c->Message(Chat::Red, "Usage: #expedition action add depop <npc_type_id>");
				return;
			}
			ExpeditionDB::AddAction(content_db, selected_event_id, "depop_npc_type", sep->arg[4]);
			c->Message(Chat::Green, fmt::format("Added depop action for NPC type [{}] to event [{}].", sep->arg[4], selected_event_name).c_str());
		}
		else if (type == "message") {
			std::string message = TailArg(sep->argplus[4]);
			if (message.empty()) {
				c->Message(Chat::Red, "Usage: #expedition action add message <text>");
				return;
			}
			ExpeditionDB::AddAction(content_db, selected_event_id, "message_members", message);
			c->Message(Chat::Green, fmt::format("Added member message action to event [{}].", selected_event_name).c_str());
		}
		else if (type == "remaining") {
			const uint32_t seconds = ParseExpeditionDuration(sep->arg[4]);
			if (!seconds) {
				c->Message(Chat::Red, "Usage: #expedition action add remaining <duration>");
				return;
			}
			ExpeditionDB::AddAction(content_db, selected_event_id, "set_remaining", std::to_string(seconds));
			c->Message(Chat::Green, fmt::format("Added remaining-time action [{}] to event [{}].", Duration(seconds), selected_event_name).c_str());
		}
		else {
			ShowActionHelp(c);
		}
		SendCurrentEventChatUi(c);
		return;
	}

	if (sub == "preset") {
		const std::string preset = Strings::ToLower(sep->arg[2]);
		if (preset.empty() || preset == "help") {
			ShowPresetHelp(c);
			return;
		}
		template_data = SelectedTemplate(c);
		if (!template_data) {
			NeedSelection(c);
			return;
		}

		const uint32_t template_id = template_data->id;
		const uint32_t dz_template_id = template_data->dz_template_id;
		if (preset == "solo") {
			ExpeditionDB::SetDzTemplatePlayers(content_db, dz_template_id, 1, 1);
			ExpeditionDB::SetDzTemplateDuration(content_db, dz_template_id, ParseExpeditionDuration("90m"));
			ExpeditionDB::SetTemplateReplay(content_db, template_id, ParseExpeditionDuration("30m"));
			c->Message(Chat::Green, "Applied solo expedition preset.");
		}
		else if (preset == "group") {
			ExpeditionDB::SetDzTemplatePlayers(content_db, dz_template_id, 1, 6);
			ExpeditionDB::SetDzTemplateDuration(content_db, dz_template_id, ParseExpeditionDuration("6h"));
			ExpeditionDB::SetTemplateReplay(content_db, template_id, ParseExpeditionDuration("2h"));
			c->Message(Chat::Green, "Applied group expedition preset.");
		}
		else if (preset == "raid") {
			ExpeditionDB::SetDzTemplatePlayers(content_db, dz_template_id, 6, 54);
			ExpeditionDB::SetDzTemplateDuration(content_db, dz_template_id, ParseExpeditionDuration("6h"));
			ExpeditionDB::SetTemplateReplay(content_db, template_id, ParseExpeditionDuration("2h"));
			c->Message(Chat::Green, "Applied raid expedition preset.");
		}
		else if (preset == "boss") {
			const auto* event_data = SelectedEvent(c, *template_data);
			uint32_t event_id = event_data ? event_data->id : ExpeditionDB::AddEvent(content_db, template_id, "Boss Defeated");
			ExpeditionDB::SetSelectedEvent(c->CharacterID(), event_id);
			ExpeditionDB::SetEventLockout(content_db, event_id, ParseExpeditionDuration("6h"));
			ExpeditionDB::SetEventReplay(content_db, event_id, ParseExpeditionDuration("2h"));
			if (NPC* npc = TargetNpc(c)) {
				ExpeditionDB::SetEventNpc(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), "boss");
			}
			c->Message(Chat::Green, "Applied boss event preset.");
		}
		else if (preset == "chest") {
			const auto* event_data = SelectedEvent(c, *template_data);
			if (!event_data) {
				c->Message(Chat::Red, "Add or select an event first.");
				SendCurrentEventChatUi(c);
				return;
			}
			const uint32_t event_id = event_data->id;
			NPC* npc = TargetNpc(c);
			if (!npc) {
				NeedTargetNpc(c);
				SendCurrentEventChatUi(c);
				return;
			}
			ExpeditionDB::SetEventNpc(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), "chest");
			ExpeditionDB::SetEventNpcLoot(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), true);
			c->Message(Chat::Green, "Applied loot chest preset.");
		}
		else {
			ShowPresetHelp(c);
		}
		SendCurrentBuilderChatUi(c);
		return;
	}

	if (sub == "test") {
		const std::string action = Strings::ToLower(sep->arg[2]);
		if (action.empty() || action == "help") {
			ShowTestHelp(c);
			return;
		}
		template_data = SelectedTemplate(c);
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		if (action == "create") {
			if (strcasecmp(sep->arg[3], "confirm") != 0) {
				c->Message(Chat::Yellow, "Type #expedition test create confirm to create a live test expedition.");
				return;
			}
			if (DynamicZone* dz = ExpeditionDB::CreateExpeditionFromTemplate(*c, *template_data, true)) {
				c->Message(Chat::Green, fmt::format("Created test expedition [{}].", dz->GetName()).c_str());
			}
			else {
				c->Message(Chat::Red, "Failed to create test expedition.");
			}
		}
		else if (action == "move") {
			if (strcasecmp(sep->arg[3], "confirm") != 0) {
				c->Message(Chat::Yellow, "Type #expedition test move confirm to move into your current expedition.");
				return;
			}
			c->MovePCExpedition(true);
		}
		else if (action == "request") {
			NPC* npc = TargetNpc(c);
			if (!npc) {
				NeedTargetNpc(c);
				return;
			}

			const ExpeditionDB::RequestNpc* matched_request_npc = nullptr;
			for (const auto& request_npc : template_data->request_npcs) {
				if (RequestNpcMatches(request_npc, *npc)) {
					matched_request_npc = &request_npc;
					break;
				}
			}

			DynamicZone dz(DynamicZoneType::Expedition);
			dz.LoadTemplate(template_data->dz_template);
			const auto check = CheckExpeditionRequest(*c, dz, true);
			const bool scripted = parse && parse->HasQuestSub(npc->GetNPCTypeID(), EVENT_SAY);
			c->Message(Chat::White, fmt::format(
				"Request test: mode [{}] target NPC type [{}] spawn [{}] scripted [{}] configured [{}]",
				template_data->request_mode,
				npc->GetNPCTypeID(),
				npc->GetSpawnPointID(),
				OnOff(scripted),
				OnOff(matched_request_npc != nullptr)
			).c_str());
			if (matched_request_npc) {
				c->Message(Chat::White, fmt::format(
					"Matched request phrase [{}].",
					ExpeditionDB::NormalizePhrase(matched_request_npc->phrase.empty() ? template_data->request_phrase : matched_request_npc->phrase)
				).c_str());
			}
			else {
				c->Message(Chat::Yellow, "Target is not configured as a request NPC for this expedition.");
			}
			c->Message(Chat::White, fmt::format(
				"Request validation: success [{}] reason [{}] members [{}] players [{}-{}] raid [{}]",
				OnOff(check.success),
				check.reason,
				check.member_count,
				check.min_players,
				check.max_players,
				OnOff(check.is_raid)
			).c_str());
			if (template_data->request_mode == "db_only" && scripted) {
				c->Message(Chat::Yellow, "Target has EVENT_SAY; automatic DB request handling will defer to the script. Use script_can_opt_in and call DB APIs from the script if desired.");
			}
			else if (template_data->request_mode != "db_only") {
				c->Message(Chat::Yellow, "Automatic DB request handling is disabled for this request mode; scripts must opt in explicitly.");
			}
			if (check.success && matched_request_npc && template_data->request_mode == "db_only" && !scripted) {
				c->Message(Chat::Green, "Simulation result: this request would create the expedition and move the requester.");
			}
			else {
				c->Message(Chat::Yellow, "Simulation result: this request would not auto-create from DB-only request handling.");
			}
		}
		else if (action == "lockout") {
			if (strcasecmp(sep->arg[3], "confirm") != 0) {
				c->Message(Chat::Yellow, "Type #expedition test lockout confirm to apply the selected event lockout to your current expedition.");
				return;
			}
			if (DynamicZone* dz = c->GetExpedition()) {
				const auto* event_data = SelectedEvent(c, *template_data);
				if (event_data) {
					if (event_data->lockout_seconds == 0) {
						c->Message(Chat::Red, "Selected event has no lockout duration.");
						return;
					}
					dz->AddLockout(event_data->event_name, event_data->lockout_seconds);
					c->Message(Chat::Green, "Applied selected event lockout.");
				}
				else {
					c->Message(Chat::Red, "Add or select an event first.");
					SendCurrentEventChatUi(c);
				}
			}
			else {
				c->Message(Chat::Red, "You are not currently in an expedition.");
			}
		}
		else if (action == "loot") {
			if (strcasecmp(sep->arg[3], "confirm") != 0) {
				c->Message(Chat::Yellow, "Type #expedition test loot confirm to re-apply DB loot protection to your current expedition.");
				return;
			}
			if (DynamicZone* dz = c->GetExpedition()) {
				ExpeditionDB::ApplyLootEvents(*dz);
				c->Message(Chat::Green, "Re-applied DB loot-event protection for your expedition.");
			}
			else {
				c->Message(Chat::Red, "You are not currently in an expedition.");
			}
		}
		else {
			c->Message(Chat::Red, "Unknown #expedition test option.");
			ShowTestHelp(c);
		}
		return;
	}

	ShowHelp(c);
}
