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
#include <ctime>
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

	const ExpeditionDB::Event* ExplicitSelectedEvent(Client* c, const ExpeditionDB::Template& template_data)
	{
		const auto& state = ExpeditionDB::GetBuilderState(c->CharacterID());
		if (!state.selected_event_id) {
			return nullptr;
		}

		auto it = std::ranges::find_if(template_data.events, [&](const ExpeditionDB::Event& event_data) {
			return event_data.id == state.selected_event_id;
		});
		return it != template_data.events.end() ? &*it : nullptr;
	}

	const ExpeditionDB::Event* RenameDefaultEvent(Client* c, const ExpeditionDB::Template& template_data)
	{
		if (const auto* event_data = ExplicitSelectedEvent(c, template_data)) {
			return event_data;
		}

		return template_data.events.size() == 1 ? &template_data.events.front() : nullptr;
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
		c->Message(Chat::Red, "Select an expedition first: #expedition select <id|name>, or start one with #expedition setup \"Name\".");
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

	bool HasQuoteArtifact(const std::string& value)
	{
		std::string out = value;
		Strings::Trim(out);
		return !out.empty() && (out.front() == '"' || out.front() == '\'' || out.back() == '"' || out.back() == '\'');
	}

	std::string CommandTail(const Seperator* sep, int index)
	{
		const std::string raw = TailArg(sep->argplus[index]);
		const std::string parsed = TailArg(sep->arg[index]);

		if (!parsed.empty() && HasQuoteArtifact(sep->argplus[index])) {
			return parsed;
		}

		return raw;
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

	bool IsLootArg(const char* value)
	{
		return value && (
			Strings::EqualFold(value, "loot") ||
			Strings::EqualFold(value, "protect") ||
			Strings::EqualFold(value, "protected") ||
			Strings::EqualFold(value, "loot_protect") ||
			Strings::EqualFold(value, "lootprotect")
		);
	}

	bool IsNoLootArg(const char* value)
	{
		return value && (
			Strings::EqualFold(value, "noloot") ||
			Strings::EqualFold(value, "no_loot") ||
			Strings::EqualFold(value, "unprotected") ||
			Strings::EqualFold(value, "no_protect")
		);
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

		return "db_only: configured request NPCs offer DB expedition options on hail and create from request links or phrases.";
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

	std::string NpcTypeName(uint32_t npc_type_id);

	std::string TargetBossName(NPC& npc)
	{
		std::string name = npc.GetCleanName();
		Strings::Trim(name);
		Strings::FindReplace(name, "_", " ");
		return name.empty() ? fmt::format("NPC {}", npc.GetNPCTypeID()) : name;
	}

	std::string BossEventName(NPC& npc)
	{
		return fmt::format("{} Defeated", TargetBossName(npc));
	}

	std::string BossEventName(const ExpeditionDB::Event& event_data, const ExpeditionDB::EventNpc& event_npc)
	{
		if (!Strings::EqualFold(event_data.event_name, ExpeditionDB::kSimpleBossEventName)) {
			return event_data.event_name;
		}

		return fmt::format("{} Defeated", NpcTypeName(event_npc.npc_type_id));
	}

	constexpr const char* ChatSeparator()
	{
		return "--------------------------------------";
	}

	void SendSectionHeader(Client* c, const std::string& title)
	{
		c->Message(Chat::White, ChatSeparator());
		c->Message(Chat::Yellow, title.c_str());
	}

	void SendActionGroup(Client* c, const std::string& title, const std::vector<std::pair<std::string, std::string>>& actions)
	{
		if (!c || actions.empty()) {
			return;
		}

		SendSectionHeader(c, title);
		for (const auto& action : actions) {
			c->Message(Chat::White, fmt::format("  {}", Saylink::Silent(action.first, action.second)).c_str());
		}
	}

	void SendHelpLink(Client* c, const std::string& command, const std::string& description)
	{
		c->Message(Chat::White, fmt::format("  {} - {}", Saylink::Silent(command, command), description).c_str());
	}

	void SendInfoLine(Client* c, const std::string& label, const std::string& value)
	{
		c->Message(Chat::White, fmt::format("  {:<16} {}", label, value).c_str());
	}

	void SendScriptedRequestNpcWarning(Client* c, const ExpeditionDB::Template& template_data, NPC& npc)
	{
		if (!Strings::EqualFold(template_data.request_mode, "db_only") || !parse || !parse->HasQuestSub(npc.GetNPCTypeID(), EVENT_SAY)) {
			return;
		}

		c->Message(Chat::Yellow, fmt::format(
			"Target [{}] has EVENT_SAY. DB expedition options will be offered after the script runs; script behavior is preserved.",
			NpcLabel(npc)
		).c_str());
		SendActionGroup(c, "Request Behavior", {
			{"#expedition set requestmode db_only", "Auto Offer DB Menu"},
			{"#expedition set requestmode script_only", "Script Owns Requests"},
			{"#expedition request", "Keep Target Request NPC"}
		});
	}

	void ShowRenameHelp(Client* c)
	{
		SendSectionHeader(c, "Rename Commands");
		c->Message(Chat::White, "#expedition rename \"New Name\" - Rename the selected expedition.");
		c->Message(Chat::White, "#expedition rename <id|name> \"New Name\" - Rename a specific expedition.");
		c->Message(Chat::White, "#expedition set name \"New Name\" - Alias for renaming the selected expedition.");

		if (const auto* template_data = SelectedTemplate(c)) {
			c->Message(Chat::Yellow, fmt::format("Selected expedition: [{}] id [{}].", template_data->name, template_data->id).c_str());
		}

		SendActionGroup(c, "Rename Examples", {
			{"#expedition rename \"New Expedition Name\"", "Rename Selected Expedition"},
			{"#expedition set name \"New Expedition Name\"", "Rename Selected With Set"}
		});
	}

	void ShowEventRenameHelp(Client* c)
	{
		SendSectionHeader(c, "Event Rename Commands");
		c->Message(Chat::White, "#expedition event rename \"New Event Name\" - Rename the explicitly selected event, or the only event if there is just one.");
		c->Message(Chat::White, "#expedition event rename selected \"New Event Name\" - Rename the explicitly selected event.");
		c->Message(Chat::White, "#expedition event rename <id> \"New Event Name\" - Rename a specific event by id.");

		if (const auto* template_data = SelectedTemplate(c)) {
			if (const auto* event_data = SelectedEvent(c, *template_data)) {
				c->Message(Chat::Yellow, fmt::format("Active event: [{}] id [{}].", event_data->event_name, event_data->id).c_str());
				SendActionGroup(c, "Rename Examples", {
					{"#expedition event rename \"New Event Name\"", "Rename Active Event"},
					{fmt::format("#expedition event rename {} \"New Event Name\"", event_data->id), "Rename Active Event By ID"}
				});
				return;
			}
		}

		SendActionGroup(c, "Event Setup", {
			{"#expedition event add \"Boss Defeated\"", "Add Boss Event"},
			{"#expedition event list", "List Events"}
		});
	}

	void ShowCreateHelp(Client* c)
	{
		SendSectionHeader(c, "Start Expedition Setup");
		c->Message(Chat::White, "#expedition setup \"Name\" - Create or select a draft using your current zone and location.");
		c->Message(Chat::White, "The builder uses your current zone/version, current location as zone-in and compass, zone safe point as return, group defaults, DB-only requests, and a 2 hour replay timer.");
		c->Message(Chat::Yellow, "Examples:");
		c->Message(Chat::White, "  #expedition setup \"Nagafen's Lair\"");
		c->Message(Chat::White, "  #expedition setup \"Cazic Thule Trial\"");
		c->Message(Chat::White, "  #expedition setup \"Plane of Hate Raid\"");

		SendActionGroup(c, "After Create", {
			{"#expedition request", "Use Target Request NPC"},
			{"#expedition boss 6h loot", "Use Target Boss"},
			{"#expedition menu", "Setup Checklist"}
		});
	}

	struct ChecklistItem {
		std::string label;
		std::string state;
		std::string detail;
	};

	std::vector<ChecklistItem> BuildChecklist(Client* c, const ExpeditionDB::Template& template_data, const ExpeditionDB::ValidationResult& validation);
	void SendChecklistChat(Client* c, const std::vector<ChecklistItem>& items);
	void SendBossLockoutChat(Client* c, const ExpeditionDB::Template& template_data);
	const ExpeditionDB::Event* FindBossEventForNpc(const ExpeditionDB::Template& template_data, NPC& npc);

	struct WizardGuidance {
		std::string focus;
		std::string why;
		std::string next_command;
		std::string next_label;
		std::vector<std::pair<std::string, std::string>> actions;
	};

	WizardGuidance BuildWizardGuidance(Client* c, const ExpeditionDB::Template& template_data, const ExpeditionDB::ValidationResult& validation);

	void SendSelectedEventChatUi(Client* c, const ExpeditionDB::Template& template_data)
	{
		const auto* event_data = SelectedEvent(c, template_data);
		if (!event_data) {
			SendActionGroup(c, "Event Setup", {
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

		SendActionGroup(c, "Event Core Actions", {
			{"#expedition event npc", "Add Target as Event NPC"},
			{"#expedition event lockout 6h", "6h Lockout"},
			{"#expedition event completeondeath on", "Target Completes Event"},
			{"#expedition event loot on", "Protect Target Loot"}
		});

		SendActionGroup(c, "Event Drill-Down", {
			{"#expedition event", "Event Command Catalog"},
			{"#expedition action", "Runtime Action Catalog"},
			{fmt::format("#expedition event remove {}", event_data->id), "Delete This Event..."}
		});
	}

	void SendBuilderChatUi(Client* c, const ExpeditionDB::Template& template_data)
	{
		const auto validation = ExpeditionDB::ValidateTemplate(template_data);
		const auto guidance = BuildWizardGuidance(c, template_data, validation);
		const auto checklist = BuildChecklist(c, template_data, validation);
		SendSectionHeader(c, "Expedition Setup");
		SendInfoLine(c, "Selected", fmt::format("{} [{}]", template_data.name, template_data.id));
		SendInfoLine(c, "State", template_data.enabled ? "published" : "draft");
		SendInfoLine(c, "Next", guidance.focus);

		if (NPC* npc = TargetNpc(c)) {
			SendInfoLine(c, "Target", NpcLabel(*npc));
		}
		else {
			SendInfoLine(c, "Target", "none");
		}

		SendChecklistChat(c, checklist);
		SendBossLockoutChat(c, template_data);

		if (NPC* npc = TargetNpc(c)) {
			const auto* boss_event = FindBossEventForNpc(template_data, *npc);
			SendInfoLine(c, "Target Boss", boss_event ?
				fmt::format("updates [{}]", boss_event->event_name) :
				"not mapped; boss command will add a separate lockout event"
			);
		}

		SendActionGroup(c, "Primary Next Action", {
			{guidance.next_command, guidance.next_label}
		});

		if (!validation.errors.empty()) {
			SendSectionHeader(c, "Blocking Issues");
			for (const auto& error : validation.errors) {
				c->Message(Chat::Red, fmt::format("  {}", error).c_str());
			}
		}
		else if (!validation.warnings.empty()) {
			SendSectionHeader(c, "Warnings");
			for (const auto& warning : validation.warnings) {
				c->Message(Chat::Yellow, fmt::format("  {}", warning).c_str());
			}
		}

		SendActionGroup(c, "Core Actions", {
			{"#expedition request", "Use Target Request NPC"},
			{"#expedition boss 6h loot", "Add/Update Target Boss With Loot"},
			{"#expedition boss 6h", "Add/Update Target Boss"},
			{"#expedition publish", "Publish"},
			{"#expedition menu", "Refresh Checklist"}
		});

		SendActionGroup(c, "Manage Expedition", {
			{"#expedition rename", "Rename Expedition"},
			{fmt::format("#expedition delete {}", template_data.id), "Delete Expedition..."}
		});

		SendActionGroup(c, "Secondary Actions", {
			{"#expedition preview", "Runtime Preview"},
			{"#expedition validate", "Validate"},
			{"#expedition test", "Testing Catalog"},
			{"#expedition advanced", "Advanced Catalog"},
			{"#expedition disable", "Disable"}
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

	std::string NpcTypeName(uint32_t npc_type_id)
	{
		if (npc_type_id == 0) {
			return "unset";
		}

		std::string name = content_db.GetCleanNPCNameByID(npc_type_id);
		if (name.empty()) {
			name = content_db.GetNPCNameByID(npc_type_id);
			Strings::FindReplace(name, "_", " ");
			Strings::Trim(name);
		}

		return name.empty() ? "unknown NPC" : name;
	}

	std::string NpcTypeLabel(uint32_t npc_type_id)
	{
		return npc_type_id == 0 ? "unset" : fmt::format("{} ({})", NpcTypeName(npc_type_id), npc_type_id);
	}

	std::string NpcMappingLabel(uint32_t npc_type_id, uint32_t spawn2_id)
	{
		return spawn2_id == 0 ?
			fmt::format("{} | spawn any", NpcTypeLabel(npc_type_id)) :
			fmt::format("{} | spawn {}", NpcTypeLabel(npc_type_id), spawn2_id);
	}

	std::string NextStep(const ExpeditionDB::Template& template_data, const ExpeditionDB::ValidationResult& validation)
	{
		if (template_data.request_mode == "db_only" && template_data.request_npcs.empty()) {
			return "Target the request NPC, then use #expedition request.";
		}

		if (template_data.events.empty()) {
			return "Target the boss, then use #expedition boss 6h or #expedition boss 6h loot.";
		}

		if (!validation.errors.empty()) {
			return "Fix validation errors before publishing.";
		}

		if (!template_data.enabled) {
			return "Ready to publish. Use #expedition publish.";
		}

		return "Published. Clone or disable before making risky live changes.";
	}

	bool HasMappedNpc(const ExpeditionDB::Event& event_data)
	{
		return std::ranges::any_of(event_data.npcs, [](const ExpeditionDB::EventNpc& event_npc) {
			return event_npc.npc_type_id != 0;
		});
	}

	bool HasCompletionNpc(const ExpeditionDB::Event& event_data)
	{
		return std::ranges::any_of(event_data.npcs, [](const ExpeditionDB::EventNpc& event_npc) {
			return event_npc.npc_type_id != 0 && event_npc.complete_on_death;
		});
	}

	bool HasLootProtectedNpc(const ExpeditionDB::Event& event_data)
	{
		return std::ranges::any_of(event_data.npcs, [](const ExpeditionDB::EventNpc& event_npc) {
			return event_npc.npc_type_id != 0 && event_npc.loot_protected;
		});
	}

	bool IsBossCompletionEvent(const ExpeditionDB::Event& event_data)
	{
		return event_data.lockout_seconds > 0 && HasCompletionNpc(event_data);
	}

	uint32_t BossCompletionEventCount(const ExpeditionDB::Template& template_data)
	{
		return static_cast<uint32_t>(std::ranges::count_if(template_data.events, IsBossCompletionEvent));
	}

	std::string BossCompletionSummary(const ExpeditionDB::Template& template_data)
	{
		std::vector<std::string> names;
		for (const auto& event_data : template_data.events) {
			if (IsBossCompletionEvent(event_data)) {
				for (const auto& event_npc : event_data.npcs) {
					if (event_npc.npc_type_id != 0 && event_npc.complete_on_death) {
						names.push_back(BossEventName(event_data, event_npc));
					}
				}
			}
		}

		if (names.empty()) {
			return "Target the boss, then use #expedition boss 6h or #expedition boss 6h loot.";
		}

		if (names.size() == 1) {
			return fmt::format("{} boss lockout: {}. Target another boss and use #expedition boss 6h loot to add it.", names.size(), names.front());
		}

		if (names.size() == 2) {
			return fmt::format("{} boss lockouts: {} and {}. Target another boss and use #expedition boss 6h loot to add it.", names.size(), names[0], names[1]);
		}

		return fmt::format("{} boss lockouts: {}, {}, and {} more. Target another boss and use #expedition boss 6h loot to add it.", names.size(), names[0], names[1], names.size() - 2);
	}

	const ExpeditionDB::Event* FindBossEventForNpc(const ExpeditionDB::Template& template_data, NPC& npc)
	{
		for (const auto& event_data : template_data.events) {
			const auto* event_npc = FindEventNpc(event_data, npc);
			if (
				event_npc &&
				event_npc->complete_on_death &&
				(Strings::EqualFold(event_npc->role, "boss") || event_data.lockout_seconds > 0)
			) {
				return &event_data;
			}
		}

		return nullptr;
	}

	std::string UniqueEventName(const ExpeditionDB::Template& template_data, const std::string& desired_name, uint32_t ignore_event_id = 0)
	{
		auto is_available = [&](const std::string& candidate) {
			return std::ranges::none_of(template_data.events, [&](const ExpeditionDB::Event& event_data) {
				return event_data.id != ignore_event_id && Strings::EqualFold(event_data.event_name, candidate);
			});
		};

		if (is_available(desired_name)) {
			return desired_name;
		}

		for (uint32_t suffix = 2; suffix < 100; ++suffix) {
			const std::string candidate = fmt::format("{} {}", desired_name, suffix);
			if (is_available(candidate)) {
				return candidate;
			}
		}

		return fmt::format("{} {}", desired_name, static_cast<uint32_t>(std::time(nullptr)));
	}

	bool HasEnabledRequestNpc(const ExpeditionDB::Template& template_data)
	{
		return std::ranges::any_of(template_data.request_npcs, [](const ExpeditionDB::RequestNpc& request_npc) {
			return request_npc.enabled && request_npc.npc_type_id != 0;
		});
	}

	bool HasScriptedDbOnlyRequestNpc(const ExpeditionDB::Template& template_data)
	{
		return Strings::EqualFold(template_data.request_mode, "db_only") && parse && std::ranges::any_of(
			template_data.request_npcs,
			[](const ExpeditionDB::RequestNpc& request_npc) {
				return request_npc.enabled && request_npc.npc_type_id != 0 && parse->HasQuestSub(request_npc.npc_type_id, EVENT_SAY);
			}
		);
	}

	std::vector<ChecklistItem> BuildChecklist(Client* c, const ExpeditionDB::Template& template_data, const ExpeditionDB::ValidationResult& validation)
	{
		const bool entrance_ready =
			template_data.dz_template.zone_id != 0 &&
			ZoneName(template_data.dz_template.zone_id) &&
			template_data.dz_template.duration_seconds > 0 &&
			template_data.dz_template.min_players > 0 &&
			template_data.dz_template.max_players >= template_data.dz_template.min_players;
		const bool request_ready = Strings::EqualFold(template_data.request_mode, "db_only") ? HasEnabledRequestNpc(template_data) : true;
		const uint32_t boss_count = BossCompletionEventCount(template_data);
		const bool completion_ready = boss_count > 0;
		const bool has_target = TargetNpc(c) != nullptr;

		std::vector<ChecklistItem> items;
		items.push_back({
			"Draft",
			template_data.enabled ? "done" : "ready",
			fmt::format("{} [{}] is selected.", template_data.name, template_data.id)
		});
		items.push_back({
			"Entrance",
			entrance_ready ? "done" : "needs fix",
			entrance_ready ?
				fmt::format("{}:{} | {} | {}-{} players",
					ZoneName(template_data.dz_template.zone_id, true),
					template_data.dz_template.zone_version,
					Duration(template_data.dz_template.duration_seconds),
					template_data.dz_template.min_players,
					template_data.dz_template.max_players
				) :
				"Use #expedition setup again or the advanced set commands to repair base zone, duration, or player bounds."
		});
		items.push_back({
			"Request NPC",
			request_ready ? "done" : (has_target ? "ready" : "needs target"),
			request_ready ?
				fmt::format(
					"{} request NPC(s), mode {}{}",
					template_data.request_npcs.size(),
					template_data.request_mode,
					HasScriptedDbOnlyRequestNpc(template_data) ? ", scripts preserved" : ""
				) :
				"Target the NPC players should talk to, then use #expedition request."
		});
		items.push_back({
			"Completion",
			completion_ready ? "done" : (has_target ? "ready" : "needs target"),
			BossCompletionSummary(template_data)
		});
		items.push_back({
			"Publish",
			template_data.enabled ? "done" : (validation.errors.empty() ? "ready" : "needs fix"),
			template_data.enabled ?
				"Published and available to runtime request handling." :
				(validation.errors.empty() ?
					"Use #expedition publish when ready." :
					fmt::format("{} blocking issue(s) must be fixed.", validation.errors.size()))
		});

		return items;
	}

	void SendChecklistChat(Client* c, const std::vector<ChecklistItem>& items)
	{
		SendSectionHeader(c, "Setup Checklist");
		for (const auto& item : items) {
			SendInfoLine(c, item.label, fmt::format("{} - {}", item.state, item.detail));
		}
	}

	void SendBossLockoutChat(Client* c, const ExpeditionDB::Template& template_data)
	{
		if (BossCompletionEventCount(template_data) == 0) {
			return;
		}

		SendSectionHeader(c, "Boss Lockouts");
		for (const auto& event_data : template_data.events) {
			if (!IsBossCompletionEvent(event_data)) {
				continue;
			}

			for (const auto& event_npc : event_data.npcs) {
				if (event_npc.npc_type_id == 0 || !event_npc.complete_on_death) {
					continue;
				}

				SendInfoLine(c, BossEventName(event_data, event_npc), fmt::format("{} | lockout {} | replay {}",
					NpcMappingLabel(event_npc.npc_type_id, event_npc.spawn2_id),
					Duration(event_data.lockout_seconds),
					Duration(event_data.replay_lockout_seconds)
				));
			}
		}
	}

	void AppendChecklistPopup(std::string& body, const std::vector<ChecklistItem>& items)
	{
		std::string table;
		for (const auto& item : items) {
			table += SnapshotRow(item.label, fmt::format("{} - {}", item.state, item.detail));
		}
		AppendPopupText(body, DialogueWindow::Table(table));
	}

	void AppendRequestNpcPopup(std::string& body, const ExpeditionDB::Template& template_data)
	{
		if (template_data.request_npcs.empty()) {
			return;
		}

		AppendPopupSection(body, "Request NPCs");
		std::string table;
		for (const auto& request_npc : template_data.request_npcs) {
			table += SnapshotRow(
				request_npc.enabled ? "Request NPC" : "Request NPC off",
				fmt::format(
					"{} | zone {} | phrase {}",
					NpcMappingLabel(request_npc.npc_type_id, request_npc.spawn2_id),
					ZoneLabel(request_npc.zone_id),
					request_npc.phrase.empty() ? "(none)" : request_npc.phrase
				)
			);
		}
		AppendPopupText(body, DialogueWindow::Table(table));
	}

	void AppendBossesPopup(std::string& body, const ExpeditionDB::Template& template_data)
	{
		AppendPopupSection(body, "Bosses");

		std::string table;
		uint32_t boss_index = 1;
		for (const auto& event_data : template_data.events) {
			if (!IsBossCompletionEvent(event_data)) {
				continue;
			}

			for (const auto& event_npc : event_data.npcs) {
				if (event_npc.npc_type_id == 0 || !event_npc.complete_on_death) {
					continue;
				}

				const bool loot_protected = event_data.loot_protected || event_npc.loot_protected;
				table += SnapshotRow(
					fmt::format("Boss {}", boss_index++),
					fmt::format(
						"{} | event {} | lockout {} | replay {} | loot {}",
						NpcMappingLabel(event_npc.npc_type_id, event_npc.spawn2_id),
						BossEventName(event_data, event_npc),
						Duration(event_data.lockout_seconds),
						Duration(event_data.replay_lockout_seconds),
						loot_protected ? "protected" : "not protected"
					)
				);
			}
		}

		if (table.empty()) {
			table += SnapshotRow("Bosses", "none configured; target a boss and use #expedition boss 6h loot");
		}

		AppendPopupText(body, DialogueWindow::Table(table));
	}

	WizardGuidance BuildWizardGuidance(Client* c, const ExpeditionDB::Template& template_data, const ExpeditionDB::ValidationResult& validation)
	{
		NPC* npc = TargetNpc(c);
		const auto* event_data = SelectedEvent(c, template_data);

		if (template_data.dz_template.zone_id == 0 || !ZoneName(template_data.dz_template.zone_id)) {
			return {
				"Set expedition zone",
				"The selected expedition has no valid destination zone.",
				"#expedition set zone",
				"Use Current Zone",
				{
					{"#expedition set zone", "Use Current Zone"},
					{"#expedition set zonein", "Use Current Zone-In"},
					{"#expedition set safereturn", "Use Current Safe Return"}
				}
			};
		}

		if (template_data.dz_template.duration_seconds == 0) {
			return {
				"Set duration",
				"The expedition needs a non-zero duration before it can be published.",
				"#expedition set duration 6h",
				"Set 6 Hour Duration",
				{
					{"#expedition set duration 90m", "90 Minute Duration"},
					{"#expedition set duration 6h", "6 Hour Duration"},
					{"#expedition set duration 12h", "12 Hour Duration"}
				}
			};
		}

		if (template_data.dz_template.min_players == 0 || template_data.dz_template.max_players < template_data.dz_template.min_players) {
			return {
				"Set player limits",
				"The player minimum and maximum are not valid.",
				"#expedition preset group",
				"Apply Group Defaults",
				{
					{"#expedition preset solo", "Solo Preset"},
					{"#expedition preset group", "Group Preset"},
					{"#expedition preset raid", "Raid Preset"}
				}
			};
		}

		if (template_data.request_mode == "db_only" && template_data.request_npcs.empty()) {
			return {
				"Add request NPC",
				npc ? fmt::format("Your current target [{}] can be saved as the request NPC.", NpcLabel(*npc)) : "Target the NPC players should talk to when requesting this expedition.",
				"#expedition request",
				npc ? "Use Target Request NPC" : "Show Request Step",
				{
					{"#expedition request", "Use Target Request NPC"},
					{"#expedition set requestmode script_can_opt_in", "Use Script Opt-In"},
					{"#expedition set requestmode script_only", "Use Script-Only Requests"}
				}
			};
		}

		if (template_data.events.empty()) {
			return {
				"Add encounter event",
				npc ? fmt::format("Your current target [{}] can seed the first boss event.", NpcLabel(*npc)) : "Add at least one event for boss completion, lockouts, and optional loot protection.",
				npc ? "#expedition boss 6h loot" : "#expedition boss 6h",
				npc ? "Create Boss Event From Target" : "Show Boss Step",
				{
					{"#expedition boss 6h", "Use Target Boss"},
					{"#expedition boss 6h loot", "Use Target Boss With Loot"},
					{"#expedition event list", "List Events"}
				}
			};
		}

		if (event_data && !HasMappedNpc(*event_data)) {
			return {
				"Map event NPC",
				npc ? fmt::format("Map target [{}] to event [{}].", NpcLabel(*npc), event_data->event_name) : fmt::format("Event [{}] has no NPC mapping yet. Target the boss, add, chest, or loot NPC for this event.", event_data->event_name),
				"#expedition boss 6h",
				npc ? "Use Target Boss" : "Show Boss Step",
				{
					{"#expedition boss 6h", "Use Target Boss"},
					{"#expedition boss 6h loot", "Use Target Boss With Loot"},
					{"#expedition event list", "List Events"}
				}
			};
		}

		if (event_data && event_data->lock_on_success && event_data->lockout_seconds == 0) {
			return {
				"Set event lockout",
				fmt::format("Event [{}] completes successfully but has no lockout duration.", event_data->event_name),
				"#expedition boss 6h",
				"Set 6 Hour Event Lockout",
				{
					{"#expedition boss 2h", "2 Hour Lockout"},
					{"#expedition boss 6h", "6 Hour Lockout"},
					{"#expedition event replay 2h", "2 Hour Replay"}
				}
			};
		}

		if (event_data && !HasCompletionNpc(*event_data)) {
			return {
				"Choose completion trigger",
				fmt::format("Event [{}] has NPC mappings, but none are marked to complete the event on death.", event_data->event_name),
				"#expedition boss 6h",
				npc ? "Use Target Boss" : "Show Boss Step",
				{
					{"#expedition boss 6h", "Use Target Boss"},
					{"#expedition action", "Runtime Actions"},
					{"#expedition event list", "List Events"}
				}
			};
		}

		if (template_data.replay_lockout_seconds == 0) {
			return {
				"Set replay timer",
				"No replay lockout is configured. A replay timer prevents immediate repeated requests.",
				"#expedition set replay 2h",
				"Set 2 Hour Replay",
				{
					{"#expedition set replay 30m", "30 Minute Replay"},
					{"#expedition set replay 2h", "2 Hour Replay"},
					{"#expedition set replay none", "No Replay Timer"}
				}
			};
		}

		if (event_data && npc && FindEventNpc(*event_data, *npc) && !HasLootProtectedNpc(*event_data)) {
			return {
				"Consider loot protection",
				fmt::format("Target [{}] is mapped to event [{}]. Enable loot protection if this NPC gates boss or chest rewards.", NpcLabel(*npc), event_data->event_name),
				"#expedition boss 6h loot",
				"Protect Target Loot",
				{
					{"#expedition boss 6h loot", "Protect Target Loot"},
					{"#expedition boss 6h noloot", "Leave Loot Unprotected"},
					{"#expedition validate", "Validate"}
				}
			};
		}

		if (!validation.errors.empty()) {
			return {
				"Fix validation errors",
				validation.errors.front(),
				"#expedition fix",
				"Show Fixes",
				{
					{"#expedition validate", "Show Validation"},
					{"#expedition fix", "Show Fixes"},
					{"#expedition menu", "Snapshot"}
				}
			};
		}

		if (!validation.warnings.empty() && !template_data.enabled) {
			return {
				"Review warnings",
				validation.warnings.front(),
				"#expedition validate",
				"Review Validation",
				{
					{"#expedition validate", "Review Warnings"},
					{"#expedition preview", "Preview Runtime"},
					{"#expedition publish", "Publish"}
				}
			};
		}

		if (!template_data.enabled) {
			return {
				"Publish expedition",
				"The setup is valid. Enable it when you are ready for request NPCs and runtime hooks to use it.",
				"#expedition publish",
				"Publish Expedition",
				{
					{"#expedition preview", "Preview Runtime"},
					{"#expedition test request", "Simulate Request"},
					{"#expedition publish", "Publish Expedition"}
				}
			};
		}

		return {
			"Test live flow",
			"The expedition is enabled. Run a controlled request/create/move test before handing it to players.",
			"#expedition test request",
			"Simulate Request",
			{
				{"#expedition test request", "Simulate Request"},
				{"#expedition test create confirm", "Create Live Test"},
				{"#expedition test move confirm", "Move Into Expedition"},
				{"#expedition menu", "Snapshot"}
			}
		};
	}

	void ShowHelp(Client* c)
	{
		SendSectionHeader(c, "Expedition Builder");
		c->Message(Chat::White, "Normal DB expeditions use a short setup flow. Advanced catalogs are still available for custom layouts.");

		SendSectionHeader(c, "Happy Path");
		SendHelpLink(c, "#expedition setup \"Name\"", "create or select a disabled draft from your current zone/location");
		SendHelpLink(c, "#expedition request [phrase]", "use targeted NPC as the request NPC");
		SendHelpLink(c, "#expedition boss [lockout] [loot]", "add/update targeted NPC as a boss-specific completion and lockout");
		SendHelpLink(c, "#expedition publish", "validate and enable when setup is ready");

		SendSectionHeader(c, "Review");
		SendHelpLink(c, "#expedition list", "list templates in this zone");
		SendHelpLink(c, "#expedition select <id|name>", "select a template");
		SendHelpLink(c, "#expedition menu", "show the compact setup checklist");
		SendHelpLink(c, "#expedition wizard", "same compact checklist plus next action");
		SendHelpLink(c, "#expedition validate", "check errors and warnings");
		SendHelpLink(c, "#expedition preview", "preview runtime behavior");

		SendSectionHeader(c, "Manage");
		SendHelpLink(c, "#expedition rename", "rename the selected expedition");
		SendHelpLink(c, "#expedition rename <id|name> \"New Name\"", "rename a specific expedition");
		SendHelpLink(c, "#expedition delete <id|name>", "review delete for a disabled expedition");
		SendHelpLink(c, "#expedition disable", "unpublish the selected expedition before deleting it");

		SendSectionHeader(c, "Advanced Catalogs");
		SendHelpLink(c, "#expedition set", "base setup, locations, player counts, replay, request mode");
		SendHelpLink(c, "#expedition requestnpc help", "request NPC setup");
		SendHelpLink(c, "#expedition event", "event, NPC, lockout, loot, and completion setup");
		SendHelpLink(c, "#expedition action", "runtime actions fired by events");
		SendHelpLink(c, "#expedition test", "testing commands");
		SendHelpLink(c, "#expedition advanced", "rename, clone, reload, delete, and other advanced actions");
		SendHelpLink(c, "#expedition disable", "unpublish the selected expedition");
	}

	void ShowSetHelp(Client* c)
	{
		SendSectionHeader(c, "Setup Catalog");
		c->Message(Chat::White, "Use bare commands to capture your current context; use explicit arguments when you need precision.");

		SendSectionHeader(c, "Identity And Zone");
		SendHelpLink(c, "#expedition set name \"New Name\"", "rename selected expedition");
		SendHelpLink(c, "#expedition set zone", "use current zone and instance version");
		SendHelpLink(c, "#expedition set zone <zone|id> [version]", "use an explicit expedition zone");

		SendSectionHeader(c, "Player And Time Limits");
		SendHelpLink(c, "#expedition set duration <duration>", "set expedition duration, like 90m or 6h");
		SendHelpLink(c, "#expedition set players <min> <max>", "set request min/max player counts");
		SendHelpLink(c, "#expedition set replay none|<duration>", "set or clear replay lockout awarded on creation");

		SendSectionHeader(c, "Locations");
		SendHelpLink(c, "#expedition set zonein", "use current location and heading");
		SendHelpLink(c, "#expedition set zonein <x> <y> <z> [h]", "set explicit zone-in coordinates");
		SendHelpLink(c, "#expedition set safereturn", "use current zone/location/heading");
		SendHelpLink(c, "#expedition set safereturn <zone|id> <x> <y> <z> [h]", "set explicit safe return");
		SendHelpLink(c, "#expedition set compass", "use current zone/location as compass marker");
		SendHelpLink(c, "#expedition set compass <zone|id> <x> <y> <z>", "set explicit compass marker");

		SendSectionHeader(c, "Behavior");
		SendHelpLink(c, "#expedition set requestmode db_only|script_can_opt_in|script_only", "choose DB/script request handling");
		SendHelpLink(c, "#expedition set silent on|off", "toggle normal create/failure messages");
		SendHelpLink(c, "#expedition set switchid target|<id>", "set dynamic-zone switch entity id");

		SendActionGroup(c, "Common Setup Actions", {
			{"#expedition set zone", "Current Zone"},
			{"#expedition set zonein", "Current Zone-In"},
			{"#expedition set safereturn", "Current Safe Return"},
			{"#expedition set compass", "Current Compass"},
			{"#expedition preset group", "Group Preset"},
			{"#expedition wizard", "Guided Next Step"}
		});
	}

	void ShowRequestNpcHelp(Client* c)
	{
		SendSectionHeader(c, "Request NPC Catalog");
		c->Message(Chat::White, "Request NPC commands use your selected expedition and current NPC target unless the command is list/help.");

		SendSectionHeader(c, "Targeted Setup");
		SendHelpLink(c, "#expedition request", "preferred simple flow: use targeted NPC with phrase 'expedition'");
		SendHelpLink(c, "#expedition request <phrase>", "preferred simple flow with a custom phrase");
		SendHelpLink(c, "#expedition requestnpc", "use targeted NPC with phrase 'expedition'");
		SendHelpLink(c, "#expedition requestnpc <phrase>", "use targeted NPC with a custom request phrase");
		SendHelpLink(c, "#expedition requestnpc remove", "prompt before removing targeted request NPC mapping");

		SendSectionHeader(c, "Review");
		SendHelpLink(c, "#expedition requestnpc list", "list configured request NPCs");

		SendSectionHeader(c, "Request Mode");
		SendHelpLink(c, "#expedition set requestmode db_only", "DB offers request options on hail and handles request links/phrases");
		SendHelpLink(c, "#expedition set requestmode script_can_opt_in", "scripts handle dialogue and may call DB APIs");
		SendHelpLink(c, "#expedition set requestmode script_only", "scripts fully own request handling");

		SendActionGroup(c, "Request NPC Actions", {
			{"#expedition request", "Add Target as Request NPC"},
			{"#expedition requestnpc list", "List Request NPCs"},
			{"#expedition set requestmode db_only", "DB Only"},
			{"#expedition wizard", "Guided Next Step"}
		});
	}

	void ShowEventHelp(Client* c)
	{
		SendSectionHeader(c, "Event Catalog");
		c->Message(Chat::White, "Events model encounter milestones such as boss deaths, chest unlocks, and lockout awards.");

		SendSectionHeader(c, "Event Basics");
		SendHelpLink(c, "#expedition event add \"Event Name\"", "add and select a DB event");
		SendHelpLink(c, "#expedition event select <id|name>", "select event for short follow-up commands");
		SendHelpLink(c, "#expedition event list", "list events on selected expedition");
		SendHelpLink(c, "#expedition event rename", "show event rename commands");

		SendSectionHeader(c, "Target NPC Mapping");
		SendHelpLink(c, "#expedition event npc", "add targeted NPC and infer role");
		SendHelpLink(c, "#expedition event npc boss|add|chest", "add targeted NPC with explicit role");
		SendHelpLink(c, "#expedition event completeondeath on|off", "toggle target death completion");
		SendHelpLink(c, "#expedition event loot on|off", "toggle target loot protection");

		SendSectionHeader(c, "Timing");
		SendHelpLink(c, "#expedition event lockout <duration>", "set selected event lockout");
		SendHelpLink(c, "#expedition event replay none|<duration>", "set or clear replay lockout on completion");

		SendSectionHeader(c, "Advanced Event Operations");
		SendHelpLink(c, "#expedition action", "runtime action catalog");
		SendHelpLink(c, "#expedition event remove <id|name>", "review event deletion target");
		SendHelpLink(c, "#expedition event remove <id|name> confirm", "delete event and its mappings");

		SendActionGroup(c, "Event Actions", {
			{"#expedition event add \"Boss Defeated\"", "Add Boss Event"},
			{"#expedition event list", "List Events"},
			{"#expedition event npc", "Add Target as Event NPC"},
			{"#expedition event lockout 6h", "6h Lockout"},
			{"#expedition wizard", "Guided Next Step"}
		});
		if (const auto* template_data = SelectedTemplate(c)) {
			SendSelectedEventChatUi(c, *template_data);
		}
	}

	void ShowTestHelp(Client* c)
	{
		SendSectionHeader(c, "Testing Catalog");
		c->Message(Chat::White, "Simulation commands are safe. Commands that create/move/apply live state require confirm.");
		SendHelpLink(c, "#expedition test request", "simulate targeted request NPC phrase flow");
		SendHelpLink(c, "#expedition test create confirm", "create selected DB expedition for your group/raid/self");
		SendHelpLink(c, "#expedition test move confirm", "move you into your current expedition");
		SendHelpLink(c, "#expedition test lockout confirm", "apply selected event lockout to current expedition");
		SendHelpLink(c, "#expedition test loot confirm", "re-apply DB loot-event protection for your expedition");
		SendActionGroup(c, "Testing Actions", {
			{"#expedition test request", "Simulate Request"},
			{"#expedition validate", "Validate"},
			{"#expedition preview", "Preview Runtime"}
		});
	}

	void ShowPresetHelp(Client* c)
	{
		SendSectionHeader(c, "Preset Catalog");
		SendHelpLink(c, "#expedition preset solo", "1 player, 90 minute duration, 30 minute replay");
		SendHelpLink(c, "#expedition preset group", "1-6 players, 6 hour duration, 2 hour replay");
		SendHelpLink(c, "#expedition preset raid", "6-54 players, 6 hour duration, 2 hour replay");
		SendHelpLink(c, "#expedition preset boss", "selected event gets timing and targeted NPC as boss");
		SendHelpLink(c, "#expedition preset chest", "targeted NPC becomes loot-protected chest for selected event");
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
		SendSectionHeader(c, "Runtime Action Catalog");
		c->Message(Chat::White, "Runtime actions fire when the selected event is resolved by DB expedition handling.");

		SendSectionHeader(c, "Lock And Time");
		SendHelpLink(c, "#expedition action add lock", "lock the expedition when selected event completes");
		SendHelpLink(c, "#expedition action add unlock", "unlock the expedition when selected event completes");
		SendHelpLink(c, "#expedition action add lockout <duration>", "add lockout for selected event");
		SendHelpLink(c, "#expedition action add lockout <event> <duration>", "add lockout for another event");
		SendHelpLink(c, "#expedition action add replay <duration>", "add or refresh Replay Timer");
		SendHelpLink(c, "#expedition action add remaining <duration>", "set expedition remaining time");

		SendSectionHeader(c, "Zone Effects");
		SendHelpLink(c, "#expedition action add depop <npc_type_id>", "depop all NPCs of that type in current zone");
		SendHelpLink(c, "#expedition action add message <text>", "message expedition members in zone");

		SendSectionHeader(c, "Review And Clear");
		SendHelpLink(c, "#expedition action list", "list actions for selected event");
		SendHelpLink(c, "#expedition action clear", "prompt before removing selected-event actions");
		SendHelpLink(c, "#expedition action clear confirm", "remove selected-event actions");

		SendActionGroup(c, "Runtime Actions", {
			{"#expedition action add lock", "Lock"},
			{"#expedition action add unlock", "Unlock"},
			{"#expedition action add replay 2h", "2h Replay"},
			{"#expedition action add lockout 6h", "6h Lockout"},
			{"#expedition action add remaining 1h", "1h Remaining"},
			{"#expedition action list", "List Actions"},
			{"#expedition action clear", "Clear Actions..."}
		});
	}

	void ShowAdvancedHelp(Client* c)
	{
		SendSectionHeader(c, "Advanced Expedition Catalog");
		c->Message(Chat::White, "Advanced commands are intentionally outside the top-level menu to keep normal setup focused.");

		SendSectionHeader(c, "Identity And Copies");
		SendHelpLink(c, "#expedition create \"Name\"", "legacy alias for starting a draft; prefer setup");
		SendHelpLink(c, "#expedition rename", "rename command catalog");
		SendHelpLink(c, "#expedition clone <id|name|current> \"New Name\"", "copy an existing setup");

		SendSectionHeader(c, "Detailed Editing");
		SendHelpLink(c, "#expedition set", "explicit coordinates, player counts, replay, request modes, and switch IDs");
		SendHelpLink(c, "#expedition requestnpc help", "request NPC list/remove and advanced request mapping");
		SendHelpLink(c, "#expedition event", "multi-event editing, event NPC roles, lockouts, and completion controls");
		SendHelpLink(c, "#expedition action", "runtime actions fired by DB events");
		SendHelpLink(c, "#expedition preset", "solo/group/raid/boss/chest presets");

		SendSectionHeader(c, "Database And Runtime Cache");
		SendHelpLink(c, "#expedition enable", "legacy publish command; prefer publish");
		SendHelpLink(c, "#expedition reload", "reload DB templates into zone memory");
		SendHelpLink(c, "#expedition list all", "list templates across all zones");
		SendHelpLink(c, "#expedition show", "show selected template details in chat");

		SendSectionHeader(c, "Danger Zone");
		SendHelpLink(c, "#expedition delete <id|name> confirm", "delete unpublished template and linked setup rows");

		SendActionGroup(c, "Advanced Actions", {
			{"#expedition rename", "Rename Catalog"},
			{"#expedition list all", "List All Templates"},
			{"#expedition reload", "Reload DB Templates"},
			{"#expedition menu", "Back To Core Menu"}
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
				scripted ? "(has EVENT_SAY script; DB menu is additive)" : ""
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

	void SendWizardChatUi(Client* c, const ExpeditionDB::Template& template_data, const ExpeditionDB::ValidationResult& validation, const WizardGuidance& guidance)
	{
		SendSectionHeader(c, "Wizard Next Step");
		c->Message(Chat::Yellow, fmt::format("Focus: {}", guidance.focus).c_str());
		c->Message(Chat::White, fmt::format("Why: {}", guidance.why).c_str());
		c->Message(Chat::White, fmt::format("Next: {}", Saylink::Silent(guidance.next_command, guidance.next_label)).c_str());

		SendActionGroup(c, "Recommended Actions", guidance.actions);

		SendSectionHeader(c, "Wizard State");
		c->Message(Chat::White, fmt::format("Expedition: [{}] id [{}] status [{}] enabled [{}].",
			template_data.name,
			template_data.id,
			validation.StatusName(),
			OnOff(template_data.enabled)
		).c_str());
		c->Message(Chat::White, fmt::format("Request mode [{}], request NPCs [{}], events [{}].",
			template_data.request_mode,
			template_data.request_npcs.size(),
			template_data.events.size()
		).c_str());
		if (const auto* event_data = SelectedEvent(c, template_data)) {
			c->Message(Chat::White, fmt::format("Active event [{}] lockout [{}] replay [{}] NPCs [{}] actions [{}].",
				event_data->event_name,
				Duration(event_data->lockout_seconds),
				Duration(event_data->replay_lockout_seconds),
				event_data->npcs.size(),
				event_data->actions.size()
			).c_str());
		}
		if (NPC* npc = TargetNpc(c)) {
			c->Message(Chat::White, fmt::format("Current target: {}.", NpcLabel(*npc)).c_str());
		}
		else {
			c->Message(Chat::White, "Current target: none.");
		}

		SendActionGroup(c, "Review", {
			{"#expedition wizard", "Refresh Wizard"},
			{"#expedition menu", "Full Snapshot"},
			{"#expedition validate", "Validate"},
			{"#expedition preview", "Preview"}
		});
	}

	void ShowWizard(Client* c, const ExpeditionDB::Template* template_data)
	{
		std::string body;
		body += DialogueWindow::CenterMessage(DialogueWindow::ColorMessage("gold", "Expedition Wizard"));
		body += DialogueWindow::Break(2);
		if (!template_data) {
			body += "Start by creating an expedition draft from your current zone and location.";
			body += DialogueWindow::Break(2);
			body += "Use #expedition setup \"Name\".";
			c->SendPopupToClient("Expedition Wizard", body.c_str());
			SendActionGroup(c, "Expedition Wizard", {
				{"#expedition setup", "Start Setup..."},
				{"#expedition list", "Select Existing Expedition"}
			});
			return;
		}

		const auto validation = ExpeditionDB::ValidateTemplate(*template_data);
		const auto guidance = BuildWizardGuidance(c, *template_data, validation);
		const auto checklist = BuildChecklist(c, *template_data, validation);

		std::string table;
		table += SnapshotRow("Expedition", fmt::format("{} [{}]", template_data->name, template_data->id));
		table += SnapshotRow("State", template_data->enabled ? "published" : "draft");
		table += SnapshotRow("Target", TargetNpc(c) ? NpcLabel(*TargetNpc(c)) : "none");
		table += SnapshotRow("Next", guidance.focus);
		table += SnapshotRow("Why", guidance.why);
		table += SnapshotRow("Next Command", guidance.next_command);
		AppendPopupText(body, DialogueWindow::Table(table));

		AppendPopupSection(body, "Checklist");
		AppendChecklistPopup(body, checklist);

		AppendRequestNpcPopup(body, *template_data);
		AppendBossesPopup(body, *template_data);

		if (!validation.errors.empty()) {
			AppendPopupSection(body, "Blocking Issues");
			for (const auto& error : validation.errors) {
				AppendPopupText(body, DialogueWindow::Break() + DialogueWindow::ColorMessage("red", PopupSafe(error)));
			}
		}
		else if (!validation.warnings.empty()) {
			AppendPopupSection(body, "Warnings");
			for (const auto& warning : validation.warnings) {
				AppendPopupText(body, DialogueWindow::Break() + DialogueWindow::ColorMessage("gold", PopupSafe(warning)));
			}
		}

		AppendPopupSection(body, "Chat Actions");
		AppendPopupText(body, "Recommended command links were sent to your chat window.");
		c->SendPopupToClient("Expedition Wizard", body.c_str());
		SendBuilderChatUi(c, *template_data);
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
			body += "Use #expedition setup \"Name\" to start a draft or select an existing expedition.";
			c->SendPopupToClient("Expedition Builder", body.c_str());
			SendActionGroup(c, "Expedition Builder", {
				{"#expedition setup", "Start Setup..."},
				{"#expedition list", "List Expeditions"},
				{"#expedition help", "Command Menu"}
			});
			return;
		}

		const auto validation = ExpeditionDB::ValidateTemplate(*template_data);
		const auto guidance = BuildWizardGuidance(c, *template_data, validation);
		const auto checklist = BuildChecklist(c, *template_data, validation);
		std::string table;
		table += SnapshotRow("Expedition", fmt::format("{} [{}]", template_data->name, template_data->id));
		table += SnapshotRow("State", template_data->enabled ? "published" : "draft");
		table += SnapshotRow("Target", TargetNpc(c) ? NpcLabel(*TargetNpc(c)) : "none");
		table += SnapshotRow("Next", guidance.focus);
		table += SnapshotRow("Next Command", guidance.next_command);
		AppendPopupText(body, DialogueWindow::Table(table));

		AppendPopupSection(body, "Checklist");
		AppendChecklistPopup(body, checklist);

		AppendRequestNpcPopup(body, *template_data);
		AppendBossesPopup(body, *template_data);

		if (!validation.errors.empty()) {
			AppendPopupSection(body, "Blocking Issues");
			for (const auto& error : validation.errors) {
				AppendPopupText(body, DialogueWindow::Break() + DialogueWindow::ColorMessage("red", PopupSafe(error)));
			}
		}
		else if (!validation.warnings.empty()) {
			AppendPopupSection(body, "Warnings");
			for (const auto& warning : validation.warnings) {
				AppendPopupText(body, DialogueWindow::Break() + DialogueWindow::ColorMessage("gold", PopupSafe(warning)));
			}
		}

		AppendPopupSection(body, "Chat Actions");
		AppendPopupText(body, "Clickable command links for the next action were sent to your chat window. Use #expedition preview for detailed runtime data.");

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
		ShowMenu(c);
		return;
	}

	if (sub == "menu") {
		if (sep->arg[2][0] != '\0') {
			const auto* template_data = ExpeditionDB::FindTemplate(CommandTail(sep, 2));
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
		SendSectionHeader(c, strcasecmp(sep->arg[2], "all") == 0 ? "All Expedition Templates" : "Zone Expedition Templates");
		c->Message(Chat::White, fmt::format("DB expedition templates: [{}]", ExpeditionDB::Templates().size()).c_str());
		for (const auto& [id, template_data] : ExpeditionDB::Templates()) {
			if (strcasecmp(sep->arg[2], "all") != 0 && template_data.dz_template.zone_id != zone->GetZoneID()) {
				continue;
			}
			c->Message(Chat::White, ChatSeparator());
			SendInfoLine(c, "Template", fmt::format("{} [{}]", template_data.name, id));
			SendInfoLine(c, "Zone", fmt::format("{}:{}", ZoneName(template_data.dz_template.zone_id, true), template_data.dz_template.zone_version));
			SendInfoLine(c, "Status", template_data.enabled ? "enabled" : "disabled");
			SendHelpLink(c, fmt::format("#expedition select {}", id), "select this template");
			SendHelpLink(c, fmt::format("#expedition menu {}", id), "open menu for this template");
			SendHelpLink(c, fmt::format("#expedition rename {} \"New Name\"", id), "rename this template");
			SendHelpLink(c, fmt::format("#expedition delete {}", id), template_data.enabled ? "review delete; disable first if live" : "review delete this draft");
		}
		return;
	}

	if (sub == "advanced") {
		ShowAdvancedHelp(c);
		return;
	}

	if (sub == "setup") {
		std::string name = CommandTail(sep, 2);
		if (name.empty()) {
			ShowCreateHelp(c);
			return;
		}

		if (const auto* existing_template = ExpeditionDB::FindTemplate(name)) {
			ExpeditionDB::SetSelectedTemplate(c->CharacterID(), existing_template->id);
			c->Message(Chat::Green, fmt::format("Selected existing expedition [{}] ({}).", existing_template->id, existing_template->name).c_str());
			ShowMenu(c);
			return;
		}

		const uint32_t id = ExpeditionDB::CreateTemplateFromClient(content_db, *c, name);
		if (!id) {
			c->Message(Chat::Red, "Failed to create DB expedition draft.");
			return;
		}

		ExpeditionDB::SetSelectedTemplate(c->CharacterID(), id);
		c->Message(Chat::Green, fmt::format(
			"Created draft expedition [{}] with group defaults, DB-only requests, and a [{}] replay timer.",
			id,
			Duration(ExpeditionDB::kSimpleSetupReplaySeconds)
		).c_str());
		ShowMenu(c);
		return;
	}

	if (sub == "create") {
		std::string name = CommandTail(sep, 2);
		if (name.empty()) {
			ShowCreateHelp(c);
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
		std::string name = CommandTail(sep, 3);
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

	if (sub == "rename") {
		if (sep->arg[2][0] == '\0' || strcasecmp(sep->arg[2], "help") == 0) {
			ShowRenameHelp(c);
			return;
		}

		const bool has_target = sep->arg[3][0] != '\0';
		const auto* rename_template = has_target ? ResolveTemplate(c, sep->arg[2]) : SelectedTemplate(c);
		const std::string new_name = has_target ? CommandTail(sep, 3) : CommandTail(sep, 2);
		if (!rename_template || new_name.empty()) {
			c->Message(Chat::Red, "Usage: #expedition rename \"New Name\" OR #expedition rename <id|name> \"New Name\"");
			if (!rename_template) {
				NeedSelection(c);
			}
			return;
		}

		const uint32_t template_id = rename_template->id;
		const std::string old_name = rename_template->name;
		if (!ExpeditionDB::SetTemplateName(content_db, template_id, new_name)) {
			c->Message(Chat::Red, fmt::format("Failed to rename expedition [{}].", old_name).c_str());
			return;
		}

		ExpeditionDB::SetSelectedTemplate(c->CharacterID(), template_id);
		c->Message(Chat::Green, fmt::format("Saved: renamed expedition [{}] to [{}].", old_name, new_name).c_str());
		ShowMenu(c);
		return;
	}

	if (sub == "select") {
		const auto* template_data = ExpeditionDB::FindTemplate(CommandTail(sep, 2));
		if (!template_data) {
			c->Message(Chat::Red, "Expedition template not found.");
			return;
		}

		ExpeditionDB::SetSelectedTemplate(c->CharacterID(), template_data->id);
		c->Message(Chat::Green, fmt::format("Selected expedition [{}] ({}).", template_data->id, template_data->name).c_str());
		ShowMenu(c);
		return;
	}

	if (sub == "request") {
		if (strcasecmp(sep->arg[2], "help") == 0) {
			ShowRequestNpcHelp(c);
			return;
		}

		const auto* template_data = SelectedTemplate(c);
		if (!template_data) {
			NeedSelection(c);
			return;
		}

		NPC* npc = TargetNpc(c);
		if (!npc) {
			NeedTargetNpc(c);
			c->Message(Chat::Yellow, "Target the NPC players should talk to, then run #expedition request.");
			return;
		}

		std::string phrase = CommandTail(sep, 2);
		if (phrase.empty()) {
			phrase = ExpeditionDB::kSimpleRequestPhrase;
		}

		const uint32_t request_npc_id = ExpeditionDB::UpsertRequestNpc(content_db, template_data->id, zone->GetZoneID(), npc->GetNPCTypeID(), npc->GetSpawnPointID(), phrase);
		if (!request_npc_id) {
			c->Message(Chat::Red, "Failed to save targeted request NPC mapping.");
			return;
		}

		c->Message(Chat::Green, fmt::format("Saved: request NPC [{}] uses phrase [{}].", NpcLabel(*npc), phrase).c_str());
		SendScriptedRequestNpcWarning(c, *template_data, *npc);
		ShowMenu(c);
		return;
	}

	if (sub == "boss") {
		if (strcasecmp(sep->arg[2], "help") == 0) {
			c->Message(Chat::White, "Usage: #expedition boss [lockout] [loot|noloot]");
			c->Message(Chat::White, "Example: #expedition boss 6h loot");
			return;
		}

		const auto* template_data = SelectedTemplate(c);
		if (!template_data) {
			NeedSelection(c);
			return;
		}

		NPC* npc = TargetNpc(c);
		if (!npc) {
			NeedTargetNpc(c);
			c->Message(Chat::Yellow, "Target the boss NPC, then run #expedition boss 6h or #expedition boss 6h loot.");
			return;
		}

		uint32_t lockout_seconds = ExpeditionDB::kSimpleBossLockoutSeconds;
		int loot_mode = -1;
		for (int i = 2; i < 10 && sep->arg[i][0] != '\0'; ++i) {
			if (IsLootArg(sep->arg[i])) {
				loot_mode = 1;
				continue;
			}

			if (IsNoLootArg(sep->arg[i])) {
				loot_mode = 0;
				continue;
			}

			const uint32_t parsed_duration = ParseExpeditionDuration(sep->arg[i]);
			if (parsed_duration) {
				lockout_seconds = parsed_duration;
				continue;
			}

			c->Message(Chat::Red, "Usage: #expedition boss [lockout] [loot|noloot]");
			return;
		}

		const auto* event_data = FindBossEventForNpc(*template_data, *npc);
		std::string event_name = event_data ? event_data->event_name : UniqueEventName(*template_data, BossEventName(*npc));
		const uint32_t event_id = event_data ? event_data->id : ExpeditionDB::AddEvent(content_db, template_data->id, event_name);
		if (!event_id) {
			c->Message(Chat::Red, "Failed to create boss completion event.");
			return;
		}

		if (event_data && Strings::EqualFold(event_name, ExpeditionDB::kSimpleBossEventName)) {
			const std::string renamed_event = UniqueEventName(*template_data, BossEventName(*npc), event_id);
			if (ExpeditionDB::SetEventName(content_db, event_id, renamed_event)) {
				event_name = renamed_event;
			}
		}

		ExpeditionDB::SetSelectedEvent(c->CharacterID(), event_id);
		if (
			!ExpeditionDB::SetEventNpc(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), "boss") ||
			!ExpeditionDB::SetEventLockout(content_db, event_id, lockout_seconds) ||
			!ExpeditionDB::SetEventReplay(content_db, event_id, ExpeditionDB::kSimpleBossReplaySeconds)
		) {
			c->Message(Chat::Red, fmt::format("Failed to save boss setup for event [{}].", event_name).c_str());
			return;
		}

		if (loot_mode != -1 && !ExpeditionDB::SetEventNpcLoot(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), loot_mode == 1)) {
			c->Message(Chat::Red, fmt::format("Failed to set boss loot protection for event [{}].", event_name).c_str());
			return;
		}

		c->Message(Chat::Green, fmt::format(
			"Saved: boss [{}] completes [{}] with lockout [{}] and replay [{}].",
			NpcLabel(*npc),
			event_name,
			Duration(lockout_seconds),
			Duration(ExpeditionDB::kSimpleBossReplaySeconds)
		).c_str());
		if (loot_mode == 1) {
			c->Message(Chat::Yellow, "Loot protection is enabled for this NPC type in the DB builder flow.");
		}
		else if (loot_mode == 0) {
			c->Message(Chat::Yellow, "Loot protection is disabled for this boss mapping.");
		}
		ShowMenu(c);
		return;
	}

	if (sub == "publish") {
		const auto* template_data = SelectedTemplate(c);
		if (!template_data) {
			NeedSelection(c);
			return;
		}

		const auto validation = ExpeditionDB::ValidateTemplate(*template_data);
		if (!validation.errors.empty()) {
			c->Message(Chat::Red, fmt::format("Publish blocked: [{}] has [{}] blocking issue(s).", template_data->name, validation.errors.size()).c_str());
			for (const auto& error : validation.errors) {
				c->Message(Chat::Red, fmt::format("Error: {}", error).c_str());
			}
			const auto guidance = BuildWizardGuidance(c, *template_data, validation);
			SendActionGroup(c, "Next Action", {
				{guidance.next_command, guidance.next_label}
			});
			return;
		}

		for (const auto& warning : validation.warnings) {
			c->Message(Chat::Yellow, fmt::format("Warning: {}", warning).c_str());
		}

		if (!ExpeditionDB::SetTemplateEnabled(content_db, template_data->id, true)) {
			c->Message(Chat::Red, fmt::format("Failed to publish expedition [{}].", template_data->name).c_str());
			return;
		}

		c->Message(Chat::Green, fmt::format("Published expedition [{}].", template_data->name).c_str());
		ShowMenu(c);
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
			ExpeditionDB::UpsertRequestNpc(content_db, template_data->id, zone->GetZoneID(), npc->GetNPCTypeID(), npc->GetSpawnPointID(), ExpeditionDB::kSimpleRequestPhrase);
			c->Message(Chat::Green, "Added targeted NPC as request NPC.");
			SendScriptedRequestNpcWarning(c, *template_data, *npc);
		}
		else if (action == "replay") {
			const uint32_t seconds = ParseExpeditionDuration(sep->arg[3][0] ? sep->arg[3] : "2h");
			ExpeditionDB::SetTemplateReplay(content_db, template_data->id, seconds);
			c->Message(Chat::Green, fmt::format("Set replay lockout to [{}].", Duration(seconds)).c_str());
		}
		else {
			ShowFixes(c, *template_data);
		}
		ShowMenu(c);
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
		ShowMenu(c);
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
		ShowMenu(c);
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
		if (field == "name") {
			const std::string new_name = CommandTail(sep, 3);
			if (new_name.empty()) {
				c->Message(Chat::Red, "Usage: #expedition set name \"New Name\"");
				return;
			}
			const uint32_t template_id = template_data->id;
			const std::string old_name = template_data->name;
			if (!ExpeditionDB::SetTemplateName(content_db, template_id, new_name)) {
				c->Message(Chat::Red, fmt::format("Failed to rename expedition [{}].", old_name).c_str());
				return;
			}
			ExpeditionDB::SetSelectedTemplate(c->CharacterID(), template_id);
			c->Message(Chat::Green, fmt::format("Saved: renamed expedition [{}] to [{}].", old_name, new_name).c_str());
		}
		else if (field == "zone") {
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
		ShowMenu(c);
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
			SendSectionHeader(c, "Configured Request NPCs");
			for (const auto& request_npc : template_data->request_npcs) {
				c->Message(Chat::White, ChatSeparator());
				SendInfoLine(c, "NPC", fmt::format("type {} | spawn {} | zone {}", request_npc.npc_type_id, request_npc.spawn2_id, ZoneLabel(request_npc.zone_id)));
				SendInfoLine(c, "Phrase", request_npc.phrase);
				SendInfoLine(c, "Enabled", OnOff(request_npc.enabled));
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
			if (!ExpeditionDB::DeleteRequestNpc(content_db, template_data->id, npc->GetNPCTypeID(), npc->GetSpawnPointID())) {
				c->Message(Chat::Red, "Failed to remove targeted request NPC mapping.");
				return;
			}
			c->Message(Chat::Green, fmt::format("Saved: removed request NPC [{}] from expedition [{}].", NpcLabel(*npc), template_data->name).c_str());
			ShowMenu(c);
			return;
		}

		std::string phrase = CommandTail(sep, 2);
		if (phrase.empty()) {
			phrase = ExpeditionDB::kSimpleRequestPhrase;
		}
		const uint32_t request_npc_id = ExpeditionDB::UpsertRequestNpc(content_db, template_data->id, zone->GetZoneID(), npc->GetNPCTypeID(), npc->GetSpawnPointID(), phrase);
		if (!request_npc_id) {
			c->Message(Chat::Red, "Failed to save targeted request NPC mapping.");
			return;
		}
		c->Message(Chat::Green, fmt::format("Saved: request NPC [{}] uses phrase [{}] for expedition [{}].", NpcLabel(*npc), phrase, template_data->name).c_str());
		SendScriptedRequestNpcWarning(c, *template_data, *npc);
		ShowMenu(c);
		return;
	}

	if (sub == "event") {
		const std::string action = Strings::ToLower(sep->arg[2]);
		if (action.empty() || action == "help") {
			ShowEventHelp(c);
			return;
		}
		if (action == "rename" && (sep->arg[3][0] == '\0' || strcasecmp(sep->arg[3], "help") == 0)) {
			ShowEventRenameHelp(c);
			return;
		}
		template_data = SelectedTemplate(c);
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		if (action == "add") {
			std::string event_name = CommandTail(sep, 3);
			if (event_name.empty()) {
				c->Message(Chat::Red, "Usage: #expedition event add \"Event Name\"");
				return;
			}
			const uint32_t event_id = ExpeditionDB::AddEvent(content_db, template_data->id, event_name);
			if (!event_id) {
				c->Message(Chat::Red, "Failed to add expedition event.");
				return;
			}
			ExpeditionDB::SetSelectedEvent(c->CharacterID(), event_id);
			c->Message(Chat::Green, fmt::format("Saved: added and selected event [{}] id [{}].", event_name, event_id).c_str());
			ShowMenu(c);
			return;
		}

		if (action == "select") {
			const auto* event_data = ExpeditionDB::FindEvent(*template_data, CommandTail(sep, 3));
			if (!event_data) {
				c->Message(Chat::Red, "Event not found.");
				return;
			}
			ExpeditionDB::SetSelectedEvent(c->CharacterID(), event_data->id);
			c->Message(Chat::Green, fmt::format("Selected event [{}].", event_data->event_name).c_str());
			ShowMenu(c);
			return;
		}

		if (action == "list") {
			if (template_data->events.empty()) {
				c->Message(Chat::White, "No events are configured for the selected expedition.");
				SendSelectedEventChatUi(c, *template_data);
				return;
			}
			SendSectionHeader(c, "Configured Events");
			for (const auto& event_data : template_data->events) {
				c->Message(Chat::White, ChatSeparator());
				SendInfoLine(c, "Event", fmt::format("{} [{}]", event_data.event_name, event_data.id));
				SendInfoLine(c, "Timing", fmt::format("lockout {} | replay {}", Duration(event_data.lockout_seconds), Duration(event_data.replay_lockout_seconds)));
				SendInfoLine(c, "Mappings", fmt::format("NPCs {} | actions {}", event_data.npcs.size(), event_data.actions.size()));
				SendHelpLink(c, fmt::format("#expedition event select {}", event_data.id), "select event");
				SendHelpLink(c, "#expedition event rename", "show rename commands");
				SendHelpLink(c, fmt::format("#expedition event remove {}", event_data.id), "review delete");
			}
			SendCurrentEventChatUi(c);
			return;
		}

		if (action == "rename") {
			if (sep->arg[4][0] == '\0' && ExpeditionDB::FindEvent(*template_data, sep->arg[3])) {
				c->Message(Chat::Yellow, "Add the new event name after the event id.");
				ShowEventRenameHelp(c);
				return;
			}
			const bool has_new_name_for_target = sep->arg[4][0] != '\0';
			const bool use_selected_target =
				!has_new_name_for_target ||
				Strings::EqualFold(sep->arg[3], "selected") ||
				Strings::EqualFold(sep->arg[3], "current");
			const auto* rename_event = use_selected_target ?
				RenameDefaultEvent(c, *template_data) :
				ExpeditionDB::FindEvent(*template_data, sep->arg[3]);
			const std::string new_name = use_selected_target ? CommandTail(sep, 3) : CommandTail(sep, 4);

			if (sep->arg[3][0] == '\0' || new_name.empty()) {
				c->Message(Chat::Red, "Usage: #expedition event rename \"New Name\" OR #expedition event rename <id> \"New Name\"");
				SendSelectedEventChatUi(c, *template_data);
				return;
			}

			if (!rename_event) {
				c->Message(Chat::Red, "Select exactly one event first, or use #expedition event rename <id> \"New Name\".");
				SendSelectedEventChatUi(c, *template_data);
				return;
			}

			const uint32_t event_id = rename_event->id;
			const std::string old_name = rename_event->event_name;
			if (!ExpeditionDB::SetEventName(content_db, event_id, new_name)) {
				c->Message(Chat::Red, fmt::format("Failed to rename event [{}].", old_name).c_str());
				return;
			}
			ExpeditionDB::SetSelectedEvent(c->CharacterID(), event_id);
			c->Message(Chat::Green, fmt::format("Saved: renamed event [{}] to [{}].", old_name, new_name).c_str());
			ShowMenu(c);
			return;
		}

		const auto* event_data = SelectedEvent(c, *template_data);
		if (!event_data) {
			c->Message(Chat::Red, "Add or select an event first.");
			return;
		}
		const uint32_t selected_event_id = event_data->id;
		const std::string selected_event_name = event_data->event_name;

		if (action == "remove" || action == "delete") {
			const bool bare_confirm = strcasecmp(sep->arg[3], "confirm") == 0;
			const bool target_confirm = strcasecmp(sep->arg[4], "confirm") == 0;
			const bool has_target = sep->arg[3][0] != '\0' && !bare_confirm;
			const auto* delete_event = has_target ? ExpeditionDB::FindEvent(*template_data, CommandTail(sep, 3)) : ExplicitSelectedEvent(c, *template_data);

			if (!delete_event) {
				if (!has_target) {
					c->Message(Chat::Red, "No explicitly selected event is available to delete. Use #expedition event list, then #expedition event remove <id>.");
				}
				else {
					c->Message(Chat::Red, "Event not found. Use #expedition event list, then #expedition event remove <id>.");
				}
				SendSelectedEventChatUi(c, *template_data);
				return;
			}

			if (!bare_confirm && !target_confirm) {
				c->Message(Chat::Yellow, ChatSeparator());
				c->Message(Chat::Yellow, "Confirm Event Deletion");
				c->Message(Chat::Yellow, fmt::format(
					"Event [{}] id [{}] has [{}] NPC mapping(s) and [{}] action(s).",
					delete_event->event_name,
					delete_event->id,
					delete_event->npcs.size(),
					delete_event->actions.size()
				).c_str());
				c->Message(Chat::Yellow, "This will remove the event, mapped NPCs, and runtime actions from the DB template.");
				c->Message(Chat::Yellow, fmt::format("Type #expedition event remove {} confirm to delete it.", delete_event->id).c_str());
				return;
			}

			const uint32_t deleted_event_id = delete_event->id;
			const std::string deleted_event_name = delete_event->event_name;
			if (!ExpeditionDB::DeleteEvent(content_db, deleted_event_id)) {
				c->Message(Chat::Red, fmt::format("Failed to remove event [{}].", deleted_event_name).c_str());
				return;
			}
			if (const auto* refreshed_template = SelectedTemplate(c)) {
				const uint32_t next_event_id = refreshed_template->events.empty() ? 0 : refreshed_template->events.front().id;
				ExpeditionDB::SetSelectedEvent(c->CharacterID(), next_event_id);
			}
			c->Message(Chat::Green, fmt::format("Saved: removed event [{}] id [{}].", deleted_event_name, deleted_event_id).c_str());
			ShowMenu(c);
			return;
		}

		if (action == "lockout") {
			const uint32_t seconds = ParseExpeditionDuration(sep->arg[3]);
			if (!seconds) {
				c->Message(Chat::Red, "Usage: #expedition event lockout <duration>");
				return;
			}
			if (!ExpeditionDB::SetEventLockout(content_db, selected_event_id, seconds)) {
				c->Message(Chat::Red, fmt::format("Failed to set event [{}] lockout.", selected_event_name).c_str());
				return;
			}
			c->Message(Chat::Green, fmt::format("Saved: event [{}] lockout is now [{}].", selected_event_name, Duration(seconds)).c_str());
			ShowMenu(c);
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
			if (!ExpeditionDB::SetEventReplay(content_db, selected_event_id, seconds)) {
				c->Message(Chat::Red, fmt::format("Failed to set event [{}] replay lockout.", selected_event_name).c_str());
				return;
			}
			c->Message(Chat::Green, fmt::format("Saved: event [{}] replay lockout is now [{}].", selected_event_name, Duration(seconds)).c_str());
			ShowMenu(c);
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
				if (!ExpeditionDB::SetEventNpc(content_db, selected_event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), role)) {
					c->Message(Chat::Red, fmt::format("Failed to save target NPC mapping for event [{}].", selected_event_name).c_str());
					return;
				}
				c->Message(Chat::Green, fmt::format(
					"Saved: event [{}] target NPC [{}] role is [{}].",
					selected_event_name,
					NpcLabel(*npc),
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
					const std::string inferred_role = ExpeditionDB::RoleFromTarget(*npc);
					if (!ExpeditionDB::SetEventNpc(content_db, selected_event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), inferred_role)) {
						c->Message(Chat::Red, fmt::format("Failed to create target NPC mapping for event [{}].", selected_event_name).c_str());
						return;
					}
					c->Message(Chat::Green, fmt::format("Saved: created event NPC mapping for [{}] as [{}].", NpcLabel(*npc), inferred_role).c_str());
				}
				if (!ExpeditionDB::SetEventNpcLoot(content_db, selected_event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), enabled)) {
					c->Message(Chat::Red, fmt::format("Failed to set loot protection for event [{}].", selected_event_name).c_str());
					return;
				}
				c->Message(Chat::Green, fmt::format(
					"Saved: event [{}] target NPC [{}] loot protection is [{}].",
					selected_event_name,
					NpcLabel(*npc),
					OnOff(enabled)
				).c_str());
			}
			else {
				bool enabled = true;
				if (sep->arg[3][0] != '\0' && !ParseOnOffArg(sep->arg[3], enabled)) {
					c->Message(Chat::Red, "Usage: #expedition event completeondeath on|off");
					return;
				}
				if (!FindEventNpc(*event_data, *npc)) {
					const std::string inferred_role = ExpeditionDB::RoleFromTarget(*npc);
					if (!ExpeditionDB::SetEventNpc(content_db, selected_event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), inferred_role)) {
						c->Message(Chat::Red, fmt::format("Failed to create target NPC mapping for event [{}].", selected_event_name).c_str());
						return;
					}
					c->Message(Chat::Green, fmt::format("Saved: created event NPC mapping for [{}] as [{}].", NpcLabel(*npc), inferred_role).c_str());
				}
				if (!ExpeditionDB::SetEventNpcCompleteOnDeath(content_db, selected_event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), enabled)) {
					c->Message(Chat::Red, fmt::format("Failed to set complete-on-death for event [{}].", selected_event_name).c_str());
					return;
				}
				c->Message(Chat::Green, fmt::format(
					"Saved: event [{}] target NPC [{}] complete-on-death is [{}].",
					selected_event_name,
					NpcLabel(*npc),
					OnOff(enabled)
				).c_str());
			}
			ShowMenu(c);
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
			if (!ExpeditionDB::ClearActions(content_db, selected_event_id)) {
				c->Message(Chat::Red, fmt::format("Failed to clear runtime actions for event [{}].", selected_event_name).c_str());
				return;
			}
			c->Message(Chat::Green, fmt::format("Saved: cleared runtime actions for event [{}].", selected_event_name).c_str());
			ShowMenu(c);
			return;
		}

		if (action != "add") {
			ShowActionHelp(c);
			return;
		}

		const std::string type = Strings::ToLower(sep->arg[3]);
		if (type == "lock" || type == "unlock") {
			const uint32_t action_id = ExpeditionDB::AddAction(content_db, selected_event_id, type, "");
			if (!action_id) {
				c->Message(Chat::Red, fmt::format("Failed to add [{}] action to event [{}].", type, selected_event_name).c_str());
				return;
			}
			c->Message(Chat::Green, fmt::format("Saved: added [{}] action id [{}] to event [{}].", type, action_id, selected_event_name).c_str());
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
			const uint32_t action_id = ExpeditionDB::AddAction(content_db, selected_event_id, "add_lockout", fmt::format("{}|{}", event_name, seconds));
			if (!action_id) {
				c->Message(Chat::Red, fmt::format("Failed to add lockout action to event [{}].", selected_event_name).c_str());
				return;
			}
			c->Message(Chat::Green, fmt::format("Saved: added lockout action id [{}] [{}] for [{}] to event [{}].", action_id, Duration(seconds), event_name, selected_event_name).c_str());
		}
		else if (type == "replay") {
			const uint32_t seconds = ParseExpeditionDuration(sep->arg[4]);
			if (!seconds) {
				c->Message(Chat::Red, "Usage: #expedition action add replay <duration>");
				return;
			}
			const uint32_t action_id = ExpeditionDB::AddAction(content_db, selected_event_id, "add_replay_lockout", std::to_string(seconds));
			if (!action_id) {
				c->Message(Chat::Red, fmt::format("Failed to add replay lockout action to event [{}].", selected_event_name).c_str());
				return;
			}
			c->Message(Chat::Green, fmt::format("Saved: added replay lockout action id [{}] [{}] to event [{}].", action_id, Duration(seconds), selected_event_name).c_str());
		}
		else if (type == "depop") {
			if (!IsStrictUnsigned(sep->arg[4])) {
				c->Message(Chat::Red, "Usage: #expedition action add depop <npc_type_id>");
				return;
			}
			const uint32_t action_id = ExpeditionDB::AddAction(content_db, selected_event_id, "depop_npc_type", sep->arg[4]);
			if (!action_id) {
				c->Message(Chat::Red, fmt::format("Failed to add depop action to event [{}].", selected_event_name).c_str());
				return;
			}
			c->Message(Chat::Green, fmt::format("Saved: added depop action id [{}] for NPC type [{}] to event [{}].", action_id, sep->arg[4], selected_event_name).c_str());
		}
		else if (type == "message") {
			std::string message = CommandTail(sep, 4);
			if (message.empty()) {
				c->Message(Chat::Red, "Usage: #expedition action add message <text>");
				return;
			}
			const uint32_t action_id = ExpeditionDB::AddAction(content_db, selected_event_id, "message_members", message);
			if (!action_id) {
				c->Message(Chat::Red, fmt::format("Failed to add member message action to event [{}].", selected_event_name).c_str());
				return;
			}
			c->Message(Chat::Green, fmt::format("Saved: added member message action id [{}] to event [{}].", action_id, selected_event_name).c_str());
		}
		else if (type == "remaining") {
			const uint32_t seconds = ParseExpeditionDuration(sep->arg[4]);
			if (!seconds) {
				c->Message(Chat::Red, "Usage: #expedition action add remaining <duration>");
				return;
			}
			const uint32_t action_id = ExpeditionDB::AddAction(content_db, selected_event_id, "set_remaining", std::to_string(seconds));
			if (!action_id) {
				c->Message(Chat::Red, fmt::format("Failed to add remaining-time action to event [{}].", selected_event_name).c_str());
				return;
			}
			c->Message(Chat::Green, fmt::format("Saved: added remaining-time action id [{}] [{}] to event [{}].", action_id, Duration(seconds), selected_event_name).c_str());
		}
		else {
			ShowActionHelp(c);
		}
		ShowMenu(c);
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
			if (!event_id) {
				c->Message(Chat::Red, "Failed to create boss event for preset.");
				return;
			}
			const std::string event_name = event_data ? event_data->event_name : "Boss Defeated";
			ExpeditionDB::SetSelectedEvent(c->CharacterID(), event_id);
			if (
				!ExpeditionDB::SetEventLockout(content_db, event_id, ParseExpeditionDuration("6h")) ||
				!ExpeditionDB::SetEventReplay(content_db, event_id, ParseExpeditionDuration("2h"))
			) {
				c->Message(Chat::Red, fmt::format("Failed to apply timing for boss event [{}].", event_name).c_str());
				return;
			}
			if (NPC* npc = TargetNpc(c)) {
				if (!ExpeditionDB::SetEventNpc(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), "boss")) {
					c->Message(Chat::Red, fmt::format("Failed to map target NPC [{}] to boss event [{}].", NpcLabel(*npc), event_name).c_str());
					return;
				}
				c->Message(Chat::Green, fmt::format("Saved: boss preset applied to event [{}] id [{}] with target NPC [{}].", event_name, event_id, NpcLabel(*npc)).c_str());
			}
			else {
				c->Message(Chat::Green, fmt::format("Saved: boss preset applied to event [{}] id [{}]. Target a boss and use #expedition event npc boss to map it.", event_name, event_id).c_str());
			}
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
			if (
				!ExpeditionDB::SetEventNpc(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), "chest") ||
				!ExpeditionDB::SetEventNpcLoot(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), true)
			) {
				c->Message(Chat::Red, fmt::format("Failed to apply loot chest preset to event [{}].", event_data->event_name).c_str());
				return;
			}
			c->Message(Chat::Green, fmt::format("Saved: loot chest preset applied to event [{}] with target NPC [{}].", event_data->event_name, NpcLabel(*npc)).c_str());
		}
		else {
			ShowPresetHelp(c);
		}
		ShowMenu(c);
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
			c->Message(Chat::Green, "Move request sent for your current expedition.");
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
				c->Message(Chat::Yellow, "Target has EVENT_SAY; DB expedition options are offered after the script runs, and script behavior is preserved.");
			}
			else if (template_data->request_mode != "db_only") {
				c->Message(Chat::Yellow, "Automatic DB request handling is disabled for this request mode; scripts must opt in explicitly.");
			}
			if (check.success && matched_request_npc && template_data->request_mode == "db_only") {
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
