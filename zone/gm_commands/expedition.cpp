#include "zone/client.h"
#include "zone/dialogue_window.h"
#include "zone/entity.h"
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
		c->Message(Chat::Red, "No expedition selected. Open #expedition to pick one, or start a new one with #expedition create \"Name\".");
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

	bool IsStrictInteger(const char* value)
	{
		if (!value || value[0] == '\0') {
			return false;
		}

		if (*value == '-') {
			++value;
		}

		return IsStrictUnsigned(value);
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

	bool IsChestArg(const char* value)
	{
		return value && (
			Strings::EqualFold(value, "chest") ||
			Strings::EqualFold(value, "loot_chest") ||
			Strings::EqualFold(value, "spawn_chest")
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

		if (request_npc.zone_version != -1 && request_npc.zone_version != static_cast<int32_t>(zone->GetInstanceVersion())) {
			return false;
		}

		if (request_npc.spawn2_id != 0 && request_npc.spawn2_id != npc.GetSpawnPointID()) {
			return false;
		}

		return request_npc.npc_type_id == npc.GetNPCTypeID();
	}

	std::string TargetBossName(NPC& npc)
	{
		std::string name = npc.GetCleanName();
		Strings::Trim(name);
		Strings::FindReplace(name, "_", " ");
		return name.empty() ? fmt::format("NPC {}", npc.GetNPCTypeID()) : name;
	}

	std::string BossEventName(NPC& npc)
	{
		return TargetBossName(npc);
	}

	std::string BossEventName(const ExpeditionDB::Event& event_data, const ExpeditionDB::EventNpc& event_npc)
	{
		if (!Strings::EqualFold(event_data.event_name, ExpeditionDB::kSimpleBossEventName)) {
			return event_data.event_name;
		}

		const std::string npc_name = ExpeditionDB::NpcTypeName(event_npc.npc_type_id);
		return npc_name.empty() ? ExpeditionDB::kSimpleBossEventName : npc_name;
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
		std::string line = "  ";
		for (size_t i = 0; i < actions.size(); ++i) {
			if (i != 0) {
				line += "   ";
			}
			line += Saylink::Silent(actions[i].first, fmt::format("[ {} ]", actions[i].second));
		}
		c->Message(Chat::White, line.c_str());
	}

	void SendHelpLink(Client* c, const std::string& command, const std::string& description)
	{
		c->Message(Chat::White, fmt::format("  {} - {}", Saylink::Silent(command, command), description).c_str());
	}

	void SendInfoLine(Client* c, const std::string& label, const std::string& value)
	{
		c->Message(Chat::White, fmt::format("  {:<16} {}", label, value).c_str());
	}

	// --- Card framing: every menu is wrapped in a titled, ruled "card" so blocks stay visually
	// distinct in the chat window instead of melding together. ---
	constexpr const char* CardRule()
	{
		return "===========================================";
	}

	void SendCardTop(Client* c, const std::string& title)
	{
		c->Message(Chat::Yellow, CardRule());
		c->Message(Chat::Yellow, fmt::format("   {}", title).c_str());
		c->Message(Chat::Yellow, CardRule());
	}

	void SendCardBottom(Client* c)
	{
		c->Message(Chat::Yellow, CardRule());
	}

	// Render a row of clickable actions on a single line: "[ A ]   [ B ]   [ C ]".
	void SendActionRow(Client* c, const std::vector<std::pair<std::string, std::string>>& actions)
	{
		if (!c || actions.empty()) {
			return;
		}
		std::string line = "  ";
		for (size_t i = 0; i < actions.size(); ++i) {
			if (i != 0) {
				line += "   ";
			}
			line += Saylink::Silent(actions[i].first, fmt::format("[ {} ]", actions[i].second));
		}
		c->Message(Chat::White, line.c_str());
	}

	// Persistent, unmistakable indicator (bright magenta) shown on every edit-mode surface so it is
	// always obvious that edit mode is active and exactly which expedition is being edited.
	void SendEditBanner(Client* c, const ExpeditionDB::Template& template_data)
	{
		c->Message(Chat::Magenta, fmt::format(
			">>>>>  EDIT MODE  --  editing: {} [{}] ({})  <<<<<",
			template_data.name,
			template_data.id,
			template_data.enabled ? "published" : "draft"
		).c_str());
	}

	// Forward declarations for the new-UX screens, referenced by helpers defined earlier than them.
	void RenderHome(Client* c, bool all);
	void RefreshBuilderView(Client* c);
	void ShowEditScreen(Client* c, const ExpeditionDB::Template& template_data);

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
		SendCardTop(c, "Create An Expedition");
		c->Message(Chat::White, "  #expedition create \"Name\"  -  create a draft from your current zone/location and start editing it.");
		c->Message(Chat::White, "  It uses your current zone/version, your spot as the zone-in and compass, the zone safe point as the return, group size, and a 2h replay timer.");
		c->Message(Chat::Yellow, "  Examples:");
		c->Message(Chat::White, "    #expedition create \"Nagafen's Lair\"");
		c->Message(Chat::White, "    #expedition create \"Cazic Thule Trial\"");
		c->Message(Chat::White, "  After creating, you're in edit mode - target NPCs to add them as bosses or requesters.");
		SendActionRow(c, {{"#expedition", "Back to List"}});
		SendCardBottom(c);
	}


	const ExpeditionDB::Event* FindBossEventForNpc(const ExpeditionDB::Template& template_data, NPC& npc);

	struct WizardGuidance {
		std::string focus;
		std::string why;
		std::string next_command;
		std::string next_label;
		std::vector<std::pair<std::string, std::string>> actions;
	};

	WizardGuidance BuildWizardGuidance(Client* c, const ExpeditionDB::Template& template_data, const ExpeditionDB::ValidationResult& validation);





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

	// One friendly zone format used on every screen: "Nagafen's Lair (soldungb)".
	std::string ZoneLabel(uint32_t zone_id)
	{
		return zone_id ? fmt::format("{} ({})", ZoneLongName(zone_id), ZoneName(zone_id, true)) : "unset";
	}

	// Same, with an explicit version suffix: "Nagafen's Lair (soldungb) v0".
	std::string ZoneVersionLabel(uint32_t zone_id, int32_t zone_version)
	{
		if (!zone_id) {
			return "unset";
		}
		return zone_version == -1 ?
			fmt::format("{} v*", ZoneLabel(zone_id)) :
			fmt::format("{} v{}", ZoneLabel(zone_id), zone_version);
	}

	std::string NpcMappingLabel(uint32_t npc_type_id, uint32_t spawn2_id)
	{
		return spawn2_id == 0 ?
			fmt::format("{} | spawn any", ExpeditionDB::NpcTypeLabel(npc_type_id)) :
			fmt::format("{} | spawn {}", ExpeditionDB::NpcTypeLabel(npc_type_id), spawn2_id);
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
			return event_npc.npc_type_id != 0 && (event_npc.complete_on_death || event_npc.complete_on_spawn);
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
						names.push_back(fmt::format("{} (death)", BossEventName(event_data, event_npc)));
					}
					else if (event_npc.npc_type_id != 0 && event_npc.complete_on_spawn) {
						names.push_back(fmt::format("{} (chest spawn)", event_data.event_name));
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
				(Strings::EqualFold(event_npc->role, "boss") || event_npc->complete_on_death || event_npc->complete_on_spawn || event_data.lockout_seconds > 0)
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
					{"#expedition show", "Show"}
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
				{"#expedition show", "Show"}
			}
		};
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
		SendHelpLink(c, "#expedition set bossonly on|off", "toggle spawning only mapped boss NPCs in expedition instances");
		SendHelpLink(c, "#expedition set switchid target|<id>", "set dynamic-zone switch entity id");

		SendActionGroup(c, "Common Setup Actions", {
			{"#expedition set zone", "Current Zone"},
			{"#expedition set zonein", "Current Zone-In"},
			{"#expedition set safereturn", "Current Safe Return"},
			{"#expedition set compass", "Current Compass"},
			{"#expedition preset group", "Group Preset"},
			{"#expedition", "Home / Overview"}
		});
	}

	void ShowRequestNpcHelp(Client* c)
	{
		SendSectionHeader(c, "Request NPC Catalog");
		c->Message(Chat::White, "Request NPC commands use your selected expedition and current NPC target unless the command is list/help.");

		SendSectionHeader(c, "Targeted Setup");
		SendHelpLink(c, "#expedition request", "use targeted NPC with phrase 'expedition'");
		SendHelpLink(c, "#expedition request <phrase>", "use targeted NPC with a custom request phrase");
		SendHelpLink(c, "#expedition request add <phrase>", "explicit add with a custom request phrase");
		SendHelpLink(c, "#expedition request remove confirm", "remove targeted request NPC mapping");

		SendSectionHeader(c, "Review");
		SendHelpLink(c, "#expedition request list", "list configured request NPCs");

		SendSectionHeader(c, "Request Mode");
		SendHelpLink(c, "#expedition set requestmode db_only", "DB offers request options on hail and handles request links/phrases");
		SendHelpLink(c, "#expedition set requestmode script_can_opt_in", "scripts handle dialogue and may call DB APIs");
		SendHelpLink(c, "#expedition set requestmode script_only", "scripts fully own request handling");

		SendActionGroup(c, "Request NPC Actions", {
			{"#expedition request", "Add Target as Request NPC"},
			{"#expedition request list", "List Request NPCs"},
			{"#expedition set requestmode db_only", "DB Only"},
			{"#expedition", "Home / Overview"}
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
		SendHelpLink(c, "#expedition event npc remove confirm", "remove targeted NPC mapping from selected event");
		SendHelpLink(c, "#expedition event completeondeath on|off", "toggle target death completion");
		SendHelpLink(c, "#expedition event completeonspawn on|off", "toggle target spawn completion");
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
			{"#expedition", "Home / Overview"}
		});
		if (const auto* template_data = SelectedTemplate(c)) {
			RefreshBuilderView(c);
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
		SendCardTop(c, "Advanced Expedition Catalog");
		c->Message(Chat::White, "  Advanced commands are intentionally outside the top-level menu to keep normal setup focused.");

		SendSectionHeader(c, "Identity And Copies");
		SendHelpLink(c, "#expedition create \"Name\"", "create or select a draft from your current zone/location");
		SendHelpLink(c, "#expedition rename", "rename command catalog (alias of set name)");
		SendHelpLink(c, "#expedition clone <id|name|current> \"New Name\"", "copy an existing setup");

		SendSectionHeader(c, "Detailed Editing");
		SendHelpLink(c, "#expedition set", "explicit coordinates, player counts, replay, request modes, and switch IDs");
		SendHelpLink(c, "#expedition request help", "request NPC list/remove and advanced request mapping");
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
			{"#expedition", "Back To Home"}
		});
		SendCardBottom(c);
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

	// Render one numbered roster NPC line, plus a clickable GoTo/Target row when it is currently spawned in-zone.
	void RenderRosterNpc(Client* c, uint32_t number, const std::string& label, uint32_t npc_type_id, const std::string& extra)
	{
		NPC* live = entity_list.GetNPCByNPCTypeID(npc_type_id);
		std::string name = ExpeditionDB::NpcTypeName(npc_type_id);
		if (name.empty()) {
			name = "unknown";
		}
		c->Message(live ? Chat::Green : Chat::Gray, fmt::format(
			"   {:>2}. {}: {} ({}){}  -  {}",
			number, label, name, npc_type_id, extra, live ? "SPAWNED" : "not spawned"
		).c_str());
		if (live) {
			c->Message(Chat::White, fmt::format("       {}    {}",
				Saylink::Silent(fmt::format("#expedition goto {}", npc_type_id), "[ GoTo ]"),
				Saylink::Silent(fmt::format("#expedition target {}", npc_type_id), "[ Target ]")
			).c_str());
		}
	}

	void ShowTemplate(Client* c, const ExpeditionDB::Template& template_data)
	{
		SendCardTop(c, fmt::format("Expedition: {} [{}]   ({})",
			template_data.name, template_data.id, template_data.enabled ? "published" : "draft"));
		SendInfoLine(c, "Zone", ZoneVersionLabel(template_data.dz_template.zone_id, template_data.dz_template.zone_version));
		SendInfoLine(c, "Players", fmt::format("{} - {}", template_data.dz_template.min_players, template_data.dz_template.max_players));
		SendInfoLine(c, "Duration", Duration(template_data.dz_template.duration_seconds));
		SendInfoLine(c, "Replay", Duration(template_data.replay_lockout_seconds));
		SendInfoLine(c, "Requests", template_data.request_mode);
		SendInfoLine(c, "Boss-only spawns", OnOff(template_data.boss_only_spawn));
		c->Message(Chat::White, ChatSeparator());

		c->Message(Chat::Yellow, "Bosses & Chests:");
		if (template_data.events.empty()) {
			c->Message(Chat::Gray, "  (none yet)");
		}
		uint32_t roster_index = 0;
		for (const auto& event_data : template_data.events) {
			c->Message(Chat::LightBlue, fmt::format("  Event: {}   -   lockout {}",
				event_data.event_name, Duration(event_data.lockout_seconds)).c_str());
			bool any_npc = false;
			for (const auto& event_npc : event_data.npcs) {
				if (Strings::EqualFold(event_npc.role, "chest") || event_npc.complete_on_spawn) {
					continue;
				}
				RenderRosterNpc(c, ++roster_index, "Boss", event_npc.npc_type_id,
					fmt::format(" - trigger {}", event_npc.complete_on_death ? "death" : "manual"));
				any_npc = true;
			}
			for (const auto& event_npc : event_data.npcs) {
				if (!(Strings::EqualFold(event_npc.role, "chest") || event_npc.complete_on_spawn)) {
					continue;
				}
				RenderRosterNpc(c, ++roster_index, "Chest", event_npc.npc_type_id,
					fmt::format(" - completes \"{}\" on spawn, loot protected", event_data.event_name));
				any_npc = true;
			}
			if (!any_npc) {
				c->Message(Chat::Gray, "    (no NPCs mapped)");
			}
		}
		c->Message(Chat::White, ChatSeparator());

		c->Message(Chat::Yellow, "Requesters:");
		if (template_data.request_npcs.empty()) {
			c->Message(Chat::Gray, "  (none yet)");
		}
		uint32_t requester_index = 0;
		for (const auto& request_npc : template_data.request_npcs) {
			RenderRosterNpc(c, ++requester_index, "Requester", request_npc.npc_type_id,
				fmt::format(" @ {} - phrase '{}'", ZoneLabel(request_npc.zone_id), request_npc.phrase));
		}

		c->Message(Chat::White, ChatSeparator());
		SendActionRow(c, {
			{fmt::format("#expedition edit on {}", template_data.id), "Edit This"},
			{fmt::format("#expedition delete {}", template_data.id), "Delete"},
			{"#expedition", "Back to List"}
		});
		SendCardBottom(c);
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
		c->Message(Chat::White, fmt::format("Boss-only spawns: {}", OnOff(template_data.boss_only_spawn)).c_str());
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
				ZoneVersionLabel(request_npc.zone_id, request_npc.zone_version),
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




	uint32_t ParseZoneArg(const char* arg)
	{
		if (!arg || arg[0] == '\0') {
			return zone ? zone->GetZoneID() : 0;
		}

		return IsStrictUnsigned(arg) ? Strings::ToUnsignedInt(arg) : ZoneID(arg);
	}

	// Shared guard helpers: return the resolved object, or nullptr (after messaging the
	// player) when the precondition is not met. Callers bail on nullptr.
	const ExpeditionDB::Template* RequireSelectedTemplate(Client* c)
	{
		const auto* template_data = SelectedTemplate(c);
		if (!template_data) {
			NeedSelection(c);
		}

		return template_data;
	}

	NPC* RequireTargetNpc(Client* c, const char* extra_message = nullptr)
	{
		NPC* npc = TargetNpc(c);
		if (!npc) {
			NeedTargetNpc(c);
			if (extra_message) {
				c->Message(Chat::Yellow, extra_message);
			}
		}

		return npc;
	}

	const ExpeditionDB::Event* RequireSelectedEvent(Client* c, const ExpeditionDB::Template& template_data)
	{
		const auto* event_data = SelectedEvent(c, template_data);
		if (!event_data) {
			c->Message(Chat::Red, "Add or select an event first.");
		}

		return event_data;
	}

	// Shared tail for the 'rename' verb and 'set name': persist the new name, keep it selected, and
	// confirm. Returns false (with an error already messaged) if the save failed so the caller bails.
	bool ApplyTemplateRename(Client* c, uint32_t template_id, const std::string& old_name, const std::string& new_name)
	{
		if (!ExpeditionDB::SetTemplateName(content_db, template_id, new_name)) {
			c->Message(Chat::Red, fmt::format("Failed to rename expedition [{}].", old_name).c_str());
			return false;
		}

		ExpeditionDB::SetSelectedTemplate(c->CharacterID(), template_id);
		c->Message(Chat::Green, fmt::format("Saved: renamed expedition [{}] to [{}].", old_name, new_name).c_str());
		return true;
	}

	// Find an existing event NPC mapping for the target, or create one with an inferred role.
	// Returns the npc_type_id/spawn2_id to operate on and whether the lookup succeeded. On a
	// creation failure ok is false and the caller bails.
	struct EventNpcMapping {
		uint32_t npc_type_id = 0;
		uint32_t spawn2_id = 0;
		bool ok = false;
	};

	EventNpcMapping GetOrCreateEventNpcMapping(
		Client* c,
		uint32_t event_id,
		const ExpeditionDB::Event& event_data,
		NPC& target_npc,
		const std::string& event_name
	)
	{
		if (const auto* mapped_npc = FindEventNpc(event_data, target_npc)) {
			return {mapped_npc->npc_type_id, mapped_npc->spawn2_id, true};
		}

		const uint32_t npc_type_id = target_npc.GetNPCTypeID();
		const uint32_t spawn2_id = target_npc.GetSpawnPointID();
		const std::string inferred_role = ExpeditionDB::RoleFromTarget(target_npc);
		if (!ExpeditionDB::SetEventNpc(content_db, event_id, npc_type_id, spawn2_id, inferred_role)) {
			c->Message(Chat::Red, fmt::format("Failed to create target NPC mapping for event [{}].", event_name).c_str());
			return {};
		}

		c->Message(Chat::Green, fmt::format("Saved: created event NPC mapping for [{}] as [{}].", NpcLabel(target_npc), inferred_role).c_str());
		return {npc_type_id, spawn2_id, true};
	}
}

// Per-verb handler functions. The dispatcher in command_expedition routes to these.
namespace {
	void HandleReload(Client* c, const Seperator* sep);
	void HandleGoto(Client* c, const Seperator* sep);
	void HandleTargetNpc(Client* c, const Seperator* sep);
	void HandleHome(Client* c, const Seperator* sep);
	void RenderHome(Client* c, bool all);
	void RefreshBuilderView(Client* c);
	void ShowEditScreen(Client* c, const ExpeditionDB::Template& template_data);
	void ShowConfigScreen(Client* c, const ExpeditionDB::Template& template_data);
	void HandleConfig(Client* c, const Seperator* sep);
	void HandleCreate(Client* c, const Seperator* sep);
	void HandleClone(Client* c, const Seperator* sep);
	void HandleSelect(Client* c, const Seperator* sep);
	void HandleRequest(Client* c, const Seperator* sep);
	void HandleBoss(Client* c, const Seperator* sep);
	void HandleChest(Client* c, const Seperator* sep);
	void HandlePublish(Client* c, const Seperator* sep);
	void HandleShow(Client* c, const Seperator* sep);
	void HandlePreview(Client* c, const Seperator* sep);
	void HandleValidate(Client* c, const Seperator* sep);
	void HandleDisable(Client* c, const Seperator* sep);
	void HandleDelete(Client* c, const Seperator* sep);
	void HandleSet(Client* c, const Seperator* sep);
	void HandleEvent(Client* c, const Seperator* sep);
	void HandleAction(Client* c, const Seperator* sep);
	void HandlePreset(Client* c, const Seperator* sep);
	void HandleTest(Client* c, const Seperator* sep);
	void HandleEdit(Client* c, const Seperator* sep);
}

void command_expedition(Client* c, const Seperator* sep)
{
	if (!c || !zone) {
		return;
	}

	const std::string sub = Strings::ToLower(sep->arg[1]);

	// Core verbs (in-zone happy path).
	if (sub == "create" || sub == "setup") {
		return HandleCreate(c, sep);
	}
	if (sub == "boss") {
		return HandleBoss(c, sep);
	}
	if (sub == "request") {
		return HandleRequest(c, sep);
	}
	if (sub == "chest") {
		return HandleChest(c, sep);
	}
	if (sub == "show") {
		return HandleShow(c, sep);
	}
	if (sub == "publish") {
		return HandlePublish(c, sep);
	}

	// Drill-down namespaced configuration.
	if (sub == "set") {
		return HandleSet(c, sep);
	}
	if (sub == "event") {
		return HandleEvent(c, sep);
	}
	if (sub == "action") {
		return HandleAction(c, sep);
	}
	if (sub == "config") {
		return HandleConfig(c, sep);
	}
	if (sub == "edit") {
		return HandleEdit(c, sep);
	}

	// Home / overview is the single top-level surface.
	if (sub.empty() || sub == "list" || sub == "home") {
		return HandleHome(c, sep);
	}

	// Utility / advanced.
	if (sub == "select") {
		return HandleSelect(c, sep);
	}
	if (sub == "clone") {
		return HandleClone(c, sep);
	}
	if (sub == "rename") {
		// "rename" is now an alias of "set name".
		return HandleSet(c, sep);
	}
	if (sub == "delete") {
		return HandleDelete(c, sep);
	}
	if (sub == "enable" || sub == "disable") {
		return HandleDisable(c, sep);
	}
	if (sub == "validate") {
		return HandleValidate(c, sep);
	}
	if (sub == "fix") {
		// Fix shortcuts live under set's quick-repair flow.
		return HandleSet(c, sep);
	}
	if (sub == "preview") {
		return HandlePreview(c, sep);
	}
	if (sub == "preset") {
		return HandlePreset(c, sep);
	}
	if (sub == "test") {
		return HandleTest(c, sep);
	}
	if (sub == "menu" || sub == "wizard") {
		// Legacy entry points now land on the new Home overview.
		return HandleHome(c, sep);
	}
	if (sub == "reload") {
		return HandleReload(c, sep);
	}
	if (sub == "goto") {
		return HandleGoto(c, sep);
	}
	if (sub == "target") {
		return HandleTargetNpc(c, sep);
	}
	if (sub == "advanced") {
		ShowAdvancedHelp(c);
		return;
	}

	// help / unknown -> the home overview
	HandleHome(c, sep);
}

namespace {

void HandleReload(Client* c, const Seperator* /*sep*/)
{
	zone->LoadDynamicZoneTemplates();
	c->Message(Chat::Green, fmt::format("Reloaded [{}] DB expedition template(s).", ExpeditionDB::Templates().size()).c_str());
	RefreshBuilderView(c);
}


// Top-level home/overview: the single entry point. Lists expeditions in the current zone with a
// compact, organized action row per expedition, plus a Create action. Each rendered as a framed card.
void HandleHome(Client* c, const Seperator* sep)
{
		RenderHome(c, strcasecmp(sep->arg[2], "all") == 0);
}

// After any mutation, land the user on the screen they're on: the edit card while editing, else home.
void RefreshBuilderView(Client* c)
{
		const auto& builder_state = ExpeditionDB::GetBuilderState(c->CharacterID());
		if (builder_state.edit_mode) {
			if (const auto* template_data = ExpeditionDB::FindTemplate(builder_state.selected_template_id)) {
				ShowEditScreen(c, *template_data);
				return;
			}
		}
		RenderHome(c, false);
}

void RenderHome(Client* c, bool all)
{
		const auto& builder_state = ExpeditionDB::GetBuilderState(c->CharacterID());
		const uint32_t editing_id = builder_state.edit_mode ? builder_state.selected_template_id : 0;
		const std::string zone_name = ZoneName(zone->GetZoneID(), true);

		if (editing_id != 0) {
			if (const auto* editing_template = ExpeditionDB::FindTemplate(editing_id)) {
				SendEditBanner(c, *editing_template);
			}
		}

		SendCardTop(c, all ? "Expeditions - All Zones" : fmt::format("Expeditions - {}", zone_name));

		SendActionRow(c, {{"#expedition create", "+ Create New Expedition"}});
		c->Message(Chat::White, "  Creating drops you into edit mode: target NPCs to add bosses / requesters.");
		c->Message(Chat::White, ChatSeparator());

		uint32_t shown = 0;
		for (const auto& [id, template_data] : ExpeditionDB::Templates()) {
			if (!all && template_data.dz_template.zone_id != zone->GetZoneID()) {
				continue;
			}
			++shown;
			const bool editing = (editing_id == id);
			c->Message(editing ? Chat::Green : Chat::White, fmt::format(
				"  {} [{}]   -   {}{}",
				template_data.name,
				id,
				template_data.enabled ? "published" : "draft",
				editing ? "   *** EDITING ***" : ""
			).c_str());

			// Compact content summary so the zone's expeditions are scannable without opening each.
			uint32_t boss_count = 0;
			for (const auto& event_data : template_data.events) {
				for (const auto& event_npc : event_data.npcs) {
					if (Strings::EqualFold(event_npc.role, "boss")) {
						++boss_count;
					}
				}
			}
			const bool ready = ExpeditionDB::ValidateTemplate(template_data).IsValid();
			c->Message(Chat::Gray, fmt::format("     Bosses: {}  |  Requesters: {}  |  {}",
				boss_count, template_data.request_npcs.size(),
				ready ? "Ready to publish" : "Needs setup").c_str());

			std::vector<std::pair<std::string, std::string>> actions;
			actions.push_back(editing
				? std::pair<std::string, std::string>{"#expedition edit off", "Stop Editing"}
				: std::pair<std::string, std::string>{fmt::format("#expedition edit on {}", id), "Edit"});
			actions.push_back({fmt::format("#expedition show {}", id), "Show"});
			actions.push_back(template_data.enabled
				? std::pair<std::string, std::string>{fmt::format("#expedition disable {}", id), "Unpublish"}
				: std::pair<std::string, std::string>{fmt::format("#expedition publish {}", id), "Publish"});
			SendActionRow(c, actions);
			c->Message(Chat::White, ChatSeparator());
		}

		if (shown == 0) {
			const std::string empty = all
				? std::string("  No expeditions exist yet.")
				: fmt::format("  No expeditions in {} yet - create one above.", zone_name);
			c->Message(Chat::White, empty.c_str());
			c->Message(Chat::White, ChatSeparator());
		}

		std::vector<std::pair<std::string, std::string>> footer;
		footer.push_back(all
			? std::pair<std::string, std::string>{"#expedition", "This Zone Only"}
			: std::pair<std::string, std::string>{"#expedition list all", "Other Zones"});
		footer.push_back({"#expedition advanced", "Advanced Commands"});
		SendActionRow(c, footer);
		SendCardBottom(c);
}

// The per-expedition working screen shown while in edit mode.
void ShowEditScreen(Client* c, const ExpeditionDB::Template& template_data)
{
		// Re-arm the target menu so re-clicking the NPC you just added re-opens its manage card.
		ExpeditionDB::ResetTargetMenu(c->CharacterID());
		SendEditBanner(c, template_data);
		SendCardTop(c, fmt::format("Editing: {} [{}]   ({})",
			template_data.name, template_data.id, template_data.enabled ? "published" : "draft"));
		c->Message(Chat::White, "  Target any NPC to add it (Boss / Requester), or manage it if it's already part of this expedition.");

		// Roster summary so you always know what's in the expedition while editing.
		uint32_t boss_count = 0;
		for (const auto& event_data : template_data.events) {
			for (const auto& event_npc : event_data.npcs) {
				if (Strings::EqualFold(event_npc.role, "boss")) {
					++boss_count;
				}
			}
		}
		c->Message(Chat::White, fmt::format("  Contents:   Bosses: {}  |  Requesters: {}  |  Events: {}",
			boss_count, template_data.request_npcs.size(), template_data.events.size()).c_str());

		// Publish readiness + the single next step needed.
		const auto validation = ExpeditionDB::ValidateTemplate(template_data);
		if (validation.IsValid()) {
			c->Message(Chat::Green, "  Ready to publish.");
		}
		else {
			const auto guidance = BuildWizardGuidance(c, template_data, validation);
			c->Message(Chat::Yellow, fmt::format("  Not ready yet - next: {}", guidance.focus).c_str());
		}
		c->Message(Chat::White, ChatSeparator());

		// Build/publish actions on one row, navigation on the next, so the row never wraps awkwardly.
		c->Message(Chat::White, "  Build:");
		SendActionRow(c, {
			{"#expedition config", "Configure"},
			{fmt::format("#expedition show {}", template_data.id), "Show"},
			template_data.enabled
				? std::pair<std::string, std::string>{"#expedition disable", "Unpublish"}
				: std::pair<std::string, std::string>{"#expedition publish", "Publish"}
		});
		c->Message(Chat::White, "  Navigate:");
		SendActionRow(c, {
			{"#expedition edit off", "Stop Editing"},
			{"#expedition", "Back to List"}
		});
		// Rename uses a non-silent saylink: clicking pre-fills the command in your input box to edit the name.
		c->Message(Chat::White, fmt::format("  Rename:  {}",
			Saylink::Create(fmt::format("#expedition rename {} \"New Name\"", template_data.id), false, "[ Rename ]")).c_str());
		SendCardBottom(c);
}

// Click-driven configuration card: shows current values and applies changes via buttons (no typing).
// Drill-down time picker: a minute/hour/day matrix of clickable presets. Each option re-runs
// command_prefix with the chosen value (e.g. "#expedition event lockout 6h").
void ShowTimeMatrix(Client* c, const std::string& title, const std::string& command_prefix, const std::string& current, bool allow_none)
{
		SendCardTop(c, title);
		SendInfoLine(c, "Current", current);
		c->Message(Chat::White, "  Minutes:");
		SendActionRow(c, {
			{command_prefix + " 5m", "5m"}, {command_prefix + " 10m", "10m"},
			{command_prefix + " 15m", "15m"}, {command_prefix + " 30m", "30m"},
			{command_prefix + " 45m", "45m"}
		});
		c->Message(Chat::White, "  Hours:");
		SendActionRow(c, {
			{command_prefix + " 1h", "1h"}, {command_prefix + " 2h", "2h"},
			{command_prefix + " 3h", "3h"}, {command_prefix + " 4h", "4h"},
			{command_prefix + " 6h", "6h"}, {command_prefix + " 8h", "8h"},
			{command_prefix + " 12h", "12h"}, {command_prefix + " 18h", "18h"}
		});
		c->Message(Chat::White, "  Days:");
		SendActionRow(c, {
			{command_prefix + " 1d", "1d"}, {command_prefix + " 2d", "2d"},
			{command_prefix + " 3d", "3d"}, {command_prefix + " 5d", "5d"},
			{command_prefix + " 7d", "7d"}, {command_prefix + " 14d", "14d"}
		});
		c->Message(Chat::White, ChatSeparator());
		if (allow_none) {
			SendActionRow(c, {{command_prefix + " none", "No Lockout (clear)"}});
		}
		SendCardBottom(c);
}

// Drill-down player-count picker: a group/raid matrix of clickable counts. Each option re-runs
// command_prefix with the chosen value (e.g. "#expedition config minplayers 3").
void ShowCountMatrix(Client* c, const std::string& title, const std::string& command_prefix, uint32_t current)
{
		SendCardTop(c, title);
		SendInfoLine(c, "Current", fmt::format("{}", current));
		c->Message(Chat::White, "  Group:");
		SendActionRow(c, {
			{command_prefix + " 1", "1"}, {command_prefix + " 2", "2"},
			{command_prefix + " 3", "3"}, {command_prefix + " 4", "4"},
			{command_prefix + " 5", "5"}, {command_prefix + " 6", "6"}
		});
		c->Message(Chat::White, "  Raid:");
		SendActionRow(c, {
			{command_prefix + " 12", "12"}, {command_prefix + " 18", "18"},
			{command_prefix + " 24", "24"}, {command_prefix + " 30", "30"},
			{command_prefix + " 36", "36"}, {command_prefix + " 42", "42"},
			{command_prefix + " 48", "48"}, {command_prefix + " 54", "54"}
		});
		SendCardBottom(c);
}

std::vector<uint32_t> ExistingZoneVersions(uint32_t zone_id)
{
	std::vector<uint32_t> versions;
	for (const auto& zone_entry : ZoneStore::Instance()->GetZones()) {
		if (zone_entry.zoneidnumber == zone_id) {
			versions.push_back(zone_entry.version);
		}
	}

	std::sort(versions.begin(), versions.end());
	versions.erase(std::unique(versions.begin(), versions.end()), versions.end());

	return versions;
}

bool ZoneVersionExists(uint32_t zone_id, uint32_t zone_version)
{
	const auto versions = ExistingZoneVersions(zone_id);
	return std::ranges::find(versions, zone_version) != versions.end();
}

void ShowVersionScreen(Client* c, const ExpeditionDB::Template& template_data)
{
	if (ExpeditionDB::GetBuilderState(c->CharacterID()).edit_mode) {
		SendEditBanner(c, template_data);
	}

	SendCardTop(c, fmt::format("Zone Version: {} [{}]", template_data.name, template_data.id));

	const auto& dz = template_data.dz_template;
	if (dz.zone_id == 0 || !ZoneName(dz.zone_id)) {
		c->Message(Chat::Red, "The selected expedition has no valid destination zone. Use #expedition set zone first.");
		SendActionRow(c, {
			{"#expedition set zone", "Use Current Zone"},
			{"#expedition config", "Back to Config"}
		});
		SendCardBottom(c);
		return;
	}

	c->Message(Chat::White, fmt::format("  Current Version: {}", dz.zone_version).c_str());

	const auto versions = ExistingZoneVersions(dz.zone_id);
	if (versions.empty()) {
		c->Message(Chat::Yellow, fmt::format(
			"  No loaded zone versions were found for {}. Use #expedition set zone <zone|id> [version].",
			ZoneLabel(dz.zone_id)
		).c_str());
	}
	else {
		std::string line = "  Zone Versions: ";
		for (size_t i = 0; i < versions.size(); ++i) {
			if (i != 0) {
				line += " ";
			}

			const auto version = versions[i];
			const bool is_current = static_cast<int32_t>(version) == dz.zone_version;
			const std::string label = is_current ?
				fmt::format("[{}]", version) :
				fmt::format("{}", version);

			line += Saylink::Silent(fmt::format("#expedition config version {}", version), label);
		}

		c->Message(Chat::White, line.c_str());
	}

	SendActionRow(c, {{"#expedition config", "Back to Config"}});
	SendCardBottom(c);
}

void ShowConfigScreen(Client* c, const ExpeditionDB::Template& template_data)
{
		if (ExpeditionDB::GetBuilderState(c->CharacterID()).edit_mode) {
			SendEditBanner(c, template_data);
		}
		SendCardTop(c, fmt::format("Configure: {} [{}]", template_data.name, template_data.id));

		const auto& dz = template_data.dz_template;
		SendInfoLine(c, "Zone", ZoneVersionLabel(dz.zone_id, dz.zone_version));
		SendInfoLine(c, "Duration", Duration(dz.duration_seconds));
		SendInfoLine(c, "Replay", Duration(template_data.replay_lockout_seconds));
		SendInfoLine(c, "Players", fmt::format("{} - {}", dz.min_players, dz.max_players));
		SendInfoLine(c, "Zone-in", dz.override_zone_in ? "set" : "NOT SET");
		SendInfoLine(c, "Safe return", dz.return_zone_id ? "set" : "default");
		SendInfoLine(c, "Boss-only spawns", OnOff(template_data.boss_only_spawn));
		c->Message(Chat::White, ChatSeparator());

		c->Message(Chat::White, "  Destination:");
		SendActionRow(c, {
			{"#expedition config version", "Version"}
		});
		c->Message(Chat::White, "  Duration (how long the instance stays open):");
		SendActionRow(c, {
			{"#expedition config duration 1h", "1h"},
			{"#expedition config duration 3h", "3h"},
			{"#expedition config duration 6h", "6h"},
			{"#expedition config duration 12h", "12h"},
			{"#expedition config duration", "More..."}
		});
		c->Message(Chat::White, "  Replay timer (how long before players can re-run it):");
		SendActionRow(c, {
			{"#expedition config replay 6h", "6h"},
			{"#expedition config replay 1d", "1d"},
			{"#expedition config replay 3d", "3d"},
			{"#expedition config replay 7d", "7d"},
			{"#expedition config replay", "More..."}
		});
		c->Message(Chat::White, "  Min players (fewest needed to form):");
		SendActionRow(c, {
			{"#expedition config minplayers 1", "1"},
			{"#expedition config minplayers 2", "2"},
			{"#expedition config minplayers 3", "3"},
			{"#expedition config minplayers 6", "6"},
			{"#expedition config minplayers", "More..."}
		});
		c->Message(Chat::White, "  Max players (largest allowed):");
		SendActionRow(c, {
			{"#expedition config maxplayers 1", "1"},
			{"#expedition config maxplayers 6", "6"},
			{"#expedition config maxplayers 12", "12"},
			{"#expedition config maxplayers 24", "24"},
			{"#expedition config maxplayers 54", "54"},
			{"#expedition config maxplayers", "More..."}
		});
		c->Message(Chat::White, "  Locations (captures your current spot):");
		SendActionRow(c, {
			{"#expedition config zonein", "Set Zone-in Here"},
			{"#expedition config safereturn", "Set Safe Return Here"},
			{"#expedition config compass", "Set Compass Here"}
		});
		c->Message(Chat::White, "  Spawn behavior:");
		SendActionRow(c, {
			{"#expedition config bossonly on", "Bosses Only"},
			{"#expedition config bossonly off", "Normal Spawns"}
		});
		c->Message(Chat::White, ChatSeparator());
		SendActionRow(c, {
			{fmt::format("#expedition edit on {}", template_data.id), "Back to Edit"},
			{fmt::format("#expedition show {}", template_data.id), "Show"}
		});
		SendCardBottom(c);
}

void HandleConfig(Client* c, const Seperator* sep)
{
		const auto* template_data = RequireSelectedTemplate(c);
		if (!template_data) {
			return;
		}
		const std::string action = Strings::ToLower(sep->arg[2]);
		if (action.empty() || action == "help") {
			ShowConfigScreen(c, *template_data);
			return;
		}

		const uint32_t template_id = template_data->id;
		const uint32_t dz_template_id = template_data->dz_template_id;
		const auto& pos = c->GetPosition();

		if (action == "duration") {
			if (sep->arg[3][0] == '\0') {
				ShowTimeMatrix(c, fmt::format("Duration: {}", template_data->name),
					"#expedition config duration", Duration(template_data->dz_template.duration_seconds), false);
				return;
			}
			const uint32_t seconds = ParseExpeditionDuration(sep->arg[3]);
			if (!seconds) {
				c->Message(Chat::Red, "Usage: #expedition config duration <duration>");
				return;
			}
			ExpeditionDB::SetDzTemplateDuration(content_db, dz_template_id, seconds);
			c->Message(Chat::Green, fmt::format("Set duration to {}.", Duration(seconds)).c_str());
		}
		else if (action == "replay") {
			if (sep->arg[3][0] == '\0') {
				ShowTimeMatrix(c, fmt::format("Replay Timer: {}", template_data->name),
					"#expedition config replay", Duration(template_data->replay_lockout_seconds), true);
				return;
			}
			const bool clear = IsClearDurationArg(sep->arg[3]);
			const uint32_t seconds = clear ? 0 : ParseExpeditionDuration(sep->arg[3]);
			if (!seconds && !clear) {
				c->Message(Chat::Red, "Usage: #expedition config replay none|<duration>");
				return;
			}
			ExpeditionDB::SetTemplateReplay(content_db, template_id, seconds);
			c->Message(Chat::Green, fmt::format("Set replay timer to {}.", Duration(seconds)).c_str());
		}
		else if (action == "version") {
			const auto& dz = template_data->dz_template;
			if (dz.zone_id == 0 || !ZoneName(dz.zone_id)) {
				ShowVersionScreen(c, *template_data);
				return;
			}

			if (sep->arg[3][0] == '\0') {
				ShowVersionScreen(c, *template_data);
				return;
			}

			if (!IsStrictUnsigned(sep->arg[3])) {
				c->Message(Chat::Red, "Usage: #expedition config version <version>");
				return;
			}

			const uint32_t version = Strings::ToUnsignedInt(sep->arg[3]);
			if (!ZoneVersionExists(dz.zone_id, version)) {
				c->Message(Chat::Red, fmt::format(
					"Zone version [{}] does not exist for {}.",
					version,
					ZoneLabel(dz.zone_id)
				).c_str());
				ShowVersionScreen(c, *template_data);
				return;
			}

			ExpeditionDB::SetDzTemplateZone(content_db, dz_template_id, dz.zone_id, version);
			c->Message(Chat::Green, fmt::format("Set zone version to [{}].", version).c_str());
			if (const auto* refreshed = ExpeditionDB::FindTemplate(template_id)) {
				ShowVersionScreen(c, *refreshed);
			}
			return;
		}
		else if (action == "minplayers") {
			if (sep->arg[3][0] == '\0') {
				ShowCountMatrix(c, fmt::format("Min Players: {}", template_data->name),
					"#expedition config minplayers", template_data->dz_template.min_players);
				return;
			}
			if (!IsStrictUnsigned(sep->arg[3])) {
				c->Message(Chat::Red, "Usage: #expedition config minplayers <count>");
				return;
			}
			uint32_t new_min = Strings::ToUnsignedInt(sep->arg[3]);
			if (new_min < 1) {
				new_min = 1;
			}
			const uint32_t cur_max = template_data->dz_template.max_players;
			const uint32_t new_max = (new_min > cur_max) ? new_min : cur_max;
			ExpeditionDB::SetDzTemplatePlayers(content_db, dz_template_id, new_min, new_max);
			if (new_max != cur_max) {
				c->Message(Chat::Green, fmt::format("Set min players to {} (raised max to {} to stay valid).", new_min, new_max).c_str());
			}
			else {
				c->Message(Chat::Green, fmt::format("Set min players to {}.", new_min).c_str());
			}
		}
		else if (action == "maxplayers") {
			if (sep->arg[3][0] == '\0') {
				ShowCountMatrix(c, fmt::format("Max Players: {}", template_data->name),
					"#expedition config maxplayers", template_data->dz_template.max_players);
				return;
			}
			if (!IsStrictUnsigned(sep->arg[3])) {
				c->Message(Chat::Red, "Usage: #expedition config maxplayers <count>");
				return;
			}
			uint32_t new_max = Strings::ToUnsignedInt(sep->arg[3]);
			if (new_max < 1) {
				new_max = 1;
			}
			const uint32_t cur_min = template_data->dz_template.min_players ? template_data->dz_template.min_players : 1;
			const uint32_t new_min = (cur_min > new_max) ? new_max : cur_min;
			ExpeditionDB::SetDzTemplatePlayers(content_db, dz_template_id, new_min, new_max);
			if (new_min != template_data->dz_template.min_players) {
				c->Message(Chat::Green, fmt::format("Set max players to {} (lowered min to {} to stay valid).", new_max, new_min).c_str());
			}
			else {
				c->Message(Chat::Green, fmt::format("Set max players to {}.", new_max).c_str());
			}
		}
		else if (action == "size") {
			const std::string size = Strings::ToLower(sep->arg[3]);
			uint32_t min_players = 0;
			uint32_t max_players = 0;
			if (size == "solo") { min_players = 1; max_players = 1; }
			else if (size == "group") { min_players = 1; max_players = 6; }
			else if (size == "raid") { min_players = 6; max_players = 54; }
			else {
				c->Message(Chat::Red, "Usage: #expedition config size <solo|group|raid>");
				return;
			}
			ExpeditionDB::SetDzTemplatePlayers(content_db, dz_template_id, min_players, max_players);
			c->Message(Chat::Green, fmt::format("Set group size to {} ({}-{} players).", size, min_players, max_players).c_str());
		}
		else if (action == "zonein") {
			ExpeditionDB::SetDzTemplateZoneIn(content_db, dz_template_id, pos.x, pos.y, pos.z, pos.w);
			c->Message(Chat::Green, "Set the zone-in point to your current location.");
		}
		else if (action == "safereturn") {
			ExpeditionDB::SetDzTemplateSafeReturn(content_db, dz_template_id, zone->GetZoneID(), pos.x, pos.y, pos.z, pos.w);
			c->Message(Chat::Green, "Set the safe return to your current location.");
		}
		else if (action == "compass") {
			ExpeditionDB::SetDzTemplateCompass(content_db, dz_template_id, zone->GetZoneID(), pos.x, pos.y, pos.z);
			c->Message(Chat::Green, "Set the compass marker to your current location.");
		}
		else if (action == "bossonly" || action == "boss_only") {
			bool enabled = false;
			if (!ParseOnOffArg(sep->arg[3], enabled)) {
				c->Message(Chat::Red, "Usage: #expedition config bossonly on|off");
				return;
			}
			ExpeditionDB::SetTemplateBossOnlySpawn(content_db, template_id, enabled);
			c->Message(Chat::Green, fmt::format("Set boss-only spawns to [{}].", OnOff(enabled)).c_str());
		}
		else {
			ShowConfigScreen(c, *template_data);
			return;
		}

		if (const auto* refreshed = ExpeditionDB::FindTemplate(template_id)) {
			ShowConfigScreen(c, *refreshed);
		}
}

// Merge of the old 'setup' and 'create' verbs: create or select a draft from the
// current zone/location with sane defaults.
void HandleCreate(Client* c, const Seperator* sep)
{
	std::string name = CommandTail(sep, 2);
	if (Strings::EqualFold(name, "help")) {
		ShowCreateHelp(c);
		return;
	}
	if (name.empty()) {
		// No name given (e.g. the "+ Create New Expedition" button) -> default to the zone name,
		// appending an incrementing number if that name is already taken. The user can rename after.
		std::string base = ZoneLongName(zone->GetZoneID());
		if (base.empty()) {
			base = ZoneName(zone->GetZoneID(), true);
		}
		if (base.empty()) {
			base = "Expedition";
		}
		const auto name_taken = [](const std::string& candidate) {
			for (const auto& [id, candidate_template] : ExpeditionDB::Templates()) {
				if (Strings::EqualFold(candidate_template.name, candidate)) {
					return true;
				}
			}
			return false;
		};
		name = base;
		uint32_t suffix = 1;
		while (name_taken(name)) {
			++suffix;
			name = fmt::format("{} {}", base, suffix);
		}
	}

	const ExpeditionDB::Template* existing_template = nullptr;
	for (const auto& [id, candidate] : ExpeditionDB::Templates()) {
		if (
			candidate.dz_template.zone_id == zone->GetZoneID() &&
			candidate.dz_template.zone_version == zone->GetInstanceVersion() &&
			(
				Strings::EqualFold(candidate.name, name) ||
				Strings::EqualFold(candidate.slug, name) ||
				Strings::EqualFold(candidate.dz_template.name, name)
			)
		) {
			if (!existing_template || (!existing_template->enabled && candidate.enabled) || (existing_template->enabled == candidate.enabled && candidate.id < existing_template->id)) {
				existing_template = &candidate;
			}
		}
	}

	if (existing_template) {
		ExpeditionDB::SetSelectedTemplate(c->CharacterID(), existing_template->id);
		ExpeditionDB::GetBuilderState(c->CharacterID()).edit_mode = true;
		c->Message(Chat::Green, fmt::format("Selected existing expedition [{}] ({}) - now editing.", existing_template->id, existing_template->name).c_str());
		ShowEditScreen(c, *existing_template);
		return;
	}

	const uint32_t id = ExpeditionDB::CreateTemplateFromClient(content_db, *c, name);
	if (!id) {
		c->Message(Chat::Red, "Failed to create DB expedition draft.");
		return;
	}

	ExpeditionDB::SetSelectedTemplate(c->CharacterID(), id);
	ExpeditionDB::GetBuilderState(c->CharacterID()).edit_mode = true;
	c->Message(Chat::Green, fmt::format(
		"Created expedition [{}] \"{}\" with group defaults - now in edit mode. Use [ Rename ] below to change the name, or target NPCs to add bosses / requesters.",
		id, name
	).c_str());
	if (const auto* created = ExpeditionDB::FindTemplate(id)) {
		ShowEditScreen(c, *created);
	}
}

void HandleClone(Client* c, const Seperator* sep)
{
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
	RefreshBuilderView(c);
}

void HandleSelect(Client* c, const Seperator* sep)
{
	const auto* template_data = ExpeditionDB::FindTemplate(CommandTail(sep, 2));
	if (!template_data) {
		c->Message(Chat::Red, "Expedition template not found.");
		return;
	}

	ExpeditionDB::SetSelectedTemplate(c->CharacterID(), template_data->id);
	c->Message(Chat::Green, fmt::format("Selected expedition [{}] ({}).", template_data->id, template_data->name).c_str());
	RefreshBuilderView(c);
}

// Merge of the old 'request' (simple) and 'requestnpc' (advanced) verbs.
//   #expedition request [phrase]           - add targeted NPC as request NPC
//   #expedition request list               - list configured request NPCs
//   #expedition request remove [confirm]    - remove targeted request NPC mapping
//   #expedition request add [phrase]        - explicit add (alias of bare form)
//   #expedition request help                - request NPC catalog
// Polished requester roster card: each requester shows spawn status, phrase, and GoTo/Target/Remove.
void ShowRequesterList(Client* c, const ExpeditionDB::Template& template_data)
{
		SendCardTop(c, fmt::format("Requesters: {} [{}]", template_data.name, template_data.id));
		if (template_data.request_npcs.empty()) {
			c->Message(Chat::Gray, "  No requesters configured yet.");
			c->Message(Chat::White, "  In edit mode, target the NPC players should hail and answer the Requester prompt.");
			SendCardBottom(c);
			return;
		}
		uint32_t requester_index = 0;
		for (const auto& request_npc : template_data.request_npcs) {
			NPC* live = entity_list.GetNPCByNPCTypeID(request_npc.npc_type_id);
			std::string name = ExpeditionDB::NpcTypeName(request_npc.npc_type_id);
			if (name.empty()) {
				name = "unknown";
			}
			c->Message(Chat::White, ChatSeparator());
			c->Message(live ? Chat::Green : Chat::Gray, fmt::format(
				"  {:>2}. {} ({})   @ {}   -   {}",
				++requester_index, name, request_npc.npc_type_id, ZoneVersionLabel(request_npc.zone_id, request_npc.zone_version),
				live ? "SPAWNED" : "not spawned"
			).c_str());
			SendInfoLine(c, "Phrase", fmt::format("\"{}\"   ({})", request_npc.phrase, request_npc.enabled ? "enabled" : "disabled"));
			std::vector<std::pair<std::string, std::string>> actions;
			if (live) {
				actions.push_back({fmt::format("#expedition goto {}", request_npc.npc_type_id), "GoTo"});
				actions.push_back({fmt::format("#expedition target {}", request_npc.npc_type_id), "Target"});
			}
			actions.push_back({fmt::format(
				"#expedition request removeid {} {} {} {}",
				request_npc.npc_type_id,
				request_npc.spawn2_id,
				request_npc.zone_id,
				request_npc.zone_version
			), "Remove"});
			SendActionRow(c, actions);
		}
		c->Message(Chat::White, ChatSeparator());
		SendActionRow(c, {{"#expedition", "Back to List"}});
		SendCardBottom(c);
}

void HandleRequest(Client* c, const Seperator* sep)
{
	const std::string action = Strings::ToLower(sep->arg[2]);
	if (action == "help") {
		ShowRequestNpcHelp(c);
		return;
	}

	const auto* template_data = SelectedTemplate(c);
	if (!template_data) {
		if (action.empty()) {
			NeedSelection(c);
			c->Message(Chat::Yellow, "Target the NPC players should talk to, then run #expedition request.");
		}
		else {
			NeedSelection(c);
		}
		return;
	}

	if (action == "list") {
		ShowRequesterList(c, *template_data);
		return;
	}

	// Remove a requester by NPC type id (from the requester list's Remove button - no target needed).
	if (action == "removeid") {
		if (!IsStrictUnsigned(sep->arg[3])) {
			c->Message(Chat::Red, "Pick Remove from the requester list.");
			return;
		}
		const uint32_t type_id = Strings::ToUnsignedInt(sep->arg[3]);
		const uint32_t spawn2_id = IsStrictUnsigned(sep->arg[4]) ? Strings::ToUnsignedInt(sep->arg[4]) : 0;
		if (!IsStrictUnsigned(sep->arg[5]) || !IsStrictInteger(sep->arg[6])) {
			c->Message(Chat::Red, "Pick Remove from the requester list.");
			return;
		}
		const uint32_t zone_id = Strings::ToUnsignedInt(sep->arg[5]);
		const int32_t zone_version = Strings::ToInt(sep->arg[6]);
		if (strcasecmp(sep->arg[7], "confirm") != 0) {
			c->Message(Chat::Yellow, fmt::format(
				"Remove requester (NPC type {}) from [{}] in {}?",
				type_id,
				template_data->name,
				ZoneVersionLabel(zone_id, zone_version)
			).c_str());
			SendActionRow(c, {
				{fmt::format("#expedition request removeid {} {} {} {} confirm", type_id, spawn2_id, zone_id, zone_version), "Confirm Remove"},
				{"#expedition request list", "Cancel"}
			});
			return;
		}
		if (!ExpeditionDB::DeleteRequestNpc(content_db, template_data->id, zone_id, zone_version, type_id, spawn2_id)) {
			c->Message(Chat::Red, "Failed to remove requester.");
			return;
		}
		c->Message(Chat::Green, "Removed requester.");
		if (NPC* live = entity_list.GetNPCByNPCTypeID(type_id)) {
			ExpeditionDB::ApplyRequesterLastName(*live);
		}
		if (const auto* refreshed = ExpeditionDB::FindTemplate(template_data->id)) {
			ShowRequesterList(c, *refreshed);
		}
		return;
	}

	NPC* npc = RequireTargetNpc(c, "Target the NPC players should talk to, then run #expedition request.");
	if (!npc) {
		return;
	}

	if (action == "remove") {
		if (strcasecmp(sep->arg[3], "confirm") != 0) {
			c->Message(Chat::Yellow, "Type #expedition request remove confirm to remove the targeted request NPC mapping.");
			return;
		}
		if (!ExpeditionDB::DeleteRequestNpc(
			content_db,
			template_data->id,
			zone->GetZoneID(),
			zone->GetInstanceVersion(),
			npc->GetNPCTypeID(),
			npc->GetSpawnPointID()
		)) {
			c->Message(Chat::Red, "Failed to remove targeted request NPC mapping.");
			return;
		}
		c->Message(Chat::Green, fmt::format("Saved: removed request NPC [{}] from expedition [{}].", NpcLabel(*npc), template_data->name).c_str());
		ExpeditionDB::ApplyRequesterLastName(*npc);
		RefreshBuilderView(c);
		return;
	}

	// Bare phrase or explicit 'add' sub-action: upsert the targeted request NPC.
	const int phrase_index = action == "add" ? 3 : 2;
	std::string phrase = CommandTail(sep, phrase_index);
	if (phrase.empty()) {
		phrase = ExpeditionDB::kSimpleRequestPhrase;
	}

	const uint32_t request_npc_id = ExpeditionDB::UpsertRequestNpc(content_db, template_data->id, zone->GetZoneID(), npc->GetNPCTypeID(), npc->GetSpawnPointID(), phrase, zone->GetInstanceVersion());
	if (!request_npc_id) {
		c->Message(Chat::Red, "Failed to save targeted request NPC mapping.");
		return;
	}

	c->Message(Chat::Green, fmt::format("Saved: request NPC [{}] uses phrase [{}].", NpcLabel(*npc), phrase).c_str());
	ExpeditionDB::ApplyRequesterLastName(*npc);
	SendScriptedRequestNpcWarning(c, *template_data, *npc);
	RefreshBuilderView(c);
}

void HandleBoss(Client* c, const Seperator* sep)
{
	if (strcasecmp(sep->arg[2], "help") == 0) {
		c->Message(Chat::White, "Usage: #expedition boss [lockout] [loot|noloot|chest]");
		c->Message(Chat::White, "Example: #expedition boss 6h loot");
		c->Message(Chat::White, "Example: #expedition boss 6h chest");
		return;
	}

	const auto* template_data = RequireSelectedTemplate(c);
	if (!template_data) {
		return;
	}

	NPC* npc = RequireTargetNpc(c, "Target the boss NPC, then run #expedition boss 6h or #expedition boss 6h loot.");
	if (!npc) {
		return;
	}

	uint32_t lockout_seconds = ExpeditionDB::kSimpleBossLockoutSeconds;
		int loot_mode = -1;
		bool chest_completion = false;
		for (int i = 2; i < 10 && sep->arg[i][0] != '\0'; ++i) {
			if (IsLootArg(sep->arg[i])) {
				loot_mode = 1;
				continue;
			}

			if (IsNoLootArg(sep->arg[i])) {
				loot_mode = 0;
				continue;
			}

			if (IsChestArg(sep->arg[i])) {
				chest_completion = true;
				continue;
			}

			const uint32_t parsed_duration = ParseExpeditionDuration(sep->arg[i]);
			if (parsed_duration) {
				lockout_seconds = parsed_duration;
				continue;
			}

			c->Message(Chat::Red, "Usage: #expedition boss [lockout] [loot|noloot|chest]");
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

		if (chest_completion && !ExpeditionDB::SetEventNpcCompleteOnDeath(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), false)) {
			c->Message(Chat::Red, fmt::format("Failed to set chest completion flow for event [{}].", event_name).c_str());
			return;
		}

		if (loot_mode != -1 && !ExpeditionDB::SetEventNpcLoot(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), loot_mode == 1)) {
			c->Message(Chat::Red, fmt::format("Failed to set boss loot protection for event [{}].", event_name).c_str());
			return;
		}

		c->Message(Chat::Green, fmt::format(
			"Saved: boss [{}] uses event [{}] with lockout [{}] and replay [{}].",
			NpcLabel(*npc),
			event_name,
			Duration(lockout_seconds),
			Duration(ExpeditionDB::kSimpleBossReplaySeconds)
		).c_str());
		if (chest_completion) {
			c->Message(Chat::Yellow, "Chest completion flow is enabled. Target the dynamic chest when it exists and use #expedition chest loot, or preconfigure it with #expedition chest type <npc_type_id>.");
		}
		if (loot_mode == 1) {
			c->Message(Chat::Yellow, "Loot protection is enabled for this NPC type in the DB builder flow.");
		}
		else if (loot_mode == 0) {
			c->Message(Chat::Yellow, "Loot protection is disabled for this boss mapping.");
		}
		RefreshBuilderView(c);
}

void HandleChest(Client* c, const Seperator* sep)
{
		if (strcasecmp(sep->arg[2], "help") == 0) {
			c->Message(Chat::White, "Usage: #expedition chest [loot|noloot]");
			c->Message(Chat::White, "Usage: #expedition chest type <npc_type_id> [loot|noloot]");
			return;
		}

		const auto* template_data = RequireSelectedTemplate(c);
		if (!template_data) {
			return;
		}

		const auto* event_data = SelectedEvent(c, *template_data);
		if (!event_data) {
			c->Message(Chat::Red, "Add or select the boss event first. Use #expedition boss 6h chest after targeting the boss.");
			return;
		}

		uint32_t npc_type_id = 0;
		std::string npc_label;
		int arg_index = 2;
		if (Strings::EqualFold(sep->arg[2], "type")) {
			if (!IsStrictUnsigned(sep->arg[3])) {
				c->Message(Chat::Red, "Usage: #expedition chest type <npc_type_id> [loot|noloot]");
				return;
			}
			npc_type_id = Strings::ToUnsignedInt(sep->arg[3]);
			npc_label = ExpeditionDB::NpcTypeLabel(npc_type_id);
			arg_index = 4;
		}
		else {
			NPC* npc = RequireTargetNpc(c, "Target the dynamic loot chest, or use #expedition chest type <npc_type_id> before it spawns.");
			if (!npc) {
				return;
			}
			npc_type_id = npc->GetNPCTypeID();
			npc_label = NpcLabel(*npc);
		}

		int loot_mode = 1;
		for (int i = arg_index; i < 10 && sep->arg[i][0] != '\0'; ++i) {
			if (IsLootArg(sep->arg[i])) {
				loot_mode = 1;
				continue;
			}
			if (IsNoLootArg(sep->arg[i])) {
				loot_mode = 0;
				continue;
			}
			c->Message(Chat::Red, "Usage: #expedition chest [loot|noloot] OR #expedition chest type <npc_type_id> [loot|noloot]");
			return;
		}

		const uint32_t event_id = event_data->id;
		if (
			!ExpeditionDB::SetEventNpc(content_db, event_id, npc_type_id, 0, "chest") ||
			!ExpeditionDB::SetEventNpcCompleteOnDeath(content_db, event_id, npc_type_id, 0, false) ||
			!ExpeditionDB::SetEventNpcCompleteOnSpawn(content_db, event_id, npc_type_id, 0, true) ||
			!ExpeditionDB::SetEventNpcLoot(content_db, event_id, npc_type_id, 0, loot_mode == 1)
		) {
			c->Message(Chat::Red, fmt::format("Failed to save loot chest setup for event [{}].", event_data->event_name).c_str());
			return;
		}

		c->Message(Chat::Green, fmt::format(
			"Saved: dynamic chest [{}] completes [{}] on spawn; loot protection [{}].",
			npc_label,
			event_data->event_name,
			loot_mode == 1 ? "on" : "off"
		).c_str());
		RefreshBuilderView(c);
}

void HandlePublish(Client* c, const Seperator* sep)
{
		const auto* template_data = ResolveTemplate(c, sep->arg[2]);
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		ExpeditionDB::SetSelectedTemplate(c->CharacterID(), template_data->id);

		const uint32_t template_id = template_data->id;
		const std::string template_name = template_data->name;

		const auto validation = ExpeditionDB::ValidateTemplate(*template_data);
		if (!validation.errors.empty()) {
			c->Message(Chat::Red, fmt::format("Publish blocked: [{}] has [{}] blocking issue(s).", template_name, validation.errors.size()).c_str());
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

		if (!ExpeditionDB::SetTemplateEnabled(content_db, template_id, true)) {
			c->Message(Chat::Red, fmt::format("Failed to publish expedition [{}].", template_name).c_str());
			return;
		}

		auto& builder_state = ExpeditionDB::GetBuilderState(c->CharacterID());
		if (builder_state.selected_template_id == template_id) {
			builder_state.edit_mode = false;
		}
		c->Message(Chat::Green, fmt::format("Published expedition [{}] - edit mode closed.", template_name).c_str());
		HandleHome(c, sep);
}

// --- Expedition edit mode: guided 3-step add flow (target NPC -> Yes/No -> role -> confirm) ---
// Each step is a framing popup window plus clickable Saylink "buttons" in chat (the codebase's
// convention: popups are read-only, chat saylinks carry the clickable actions).
// One Yes/No popup per NPC role, asked in sequence. "Yes" adds the NPC as that role; "No" advances
// to the next role (Boss -> Chest -> Requester -> skip). Extend by adding roles to the chain.
void ShowRoleQuestion(Client* c, const ExpeditionDB::Template& template_data, NPC& npc, uint32_t entity_id, const std::string& role)
{
		SendEditBanner(c, template_data);
		const uint16 eid = static_cast<uint16>(entity_id);
		std::string title;
		std::string body;
		uint32_t yes_id = 0;
		uint32_t no_id = 0;

		if (role == "boss") {
			title = "Add NPC - Boss?";
			body = fmt::format(
				"Add {} (NPC type {}) to {} as a Boss?<br><br>"
				"Defeating it completes its event (default {} lockout). Choose No to consider other roles.",
				npc.GetCleanName(), npc.GetNPCTypeID(), template_data.name,
				Duration(ExpeditionDB::kSimpleBossLockoutSeconds));
			yes_id = ExpeditionEditPopup::Make(ExpeditionEditPopup::AddBoss, eid);
			no_id  = ExpeditionEditPopup::Make(ExpeditionEditPopup::AskChest, eid);
		}
		else if (role == "chest") {
			title = "Add NPC - Loot Chest?";
			body = fmt::format(
				"Not a boss. Add {} (NPC type {}) as a loot Chest?<br><br>"
				"Its spawn completes the selected event and its loot is protected. Choose No for other roles.",
				npc.GetCleanName(), npc.GetNPCTypeID());
			yes_id = ExpeditionEditPopup::Make(ExpeditionEditPopup::AddChest, eid);
			no_id  = ExpeditionEditPopup::Make(ExpeditionEditPopup::AskRequester, eid);
		}
		else { // requester
			title = "Add NPC - Requester?";
			body = fmt::format(
				"Add {} (NPC type {}) as the Requester?<br><br>"
				"Players hail or say to it to request and enter the expedition. Choose No to skip this NPC.",
				npc.GetCleanName(), npc.GetNPCTypeID());
			yes_id = ExpeditionEditPopup::Make(ExpeditionEditPopup::AddRequester, eid);
			no_id  = ExpeditionEditPopup::Make(ExpeditionEditPopup::Skip, eid);
		}

		c->SendFullPopup(title.c_str(), body.c_str(), yes_id, no_id, 1, 0, "Yes", "No");
}

// Links a loot chest to a specific boss event: its spawn completes that event (and fires that
// boss's lockout), with loot protection. Used for both the single-boss auto-link and the chooser.
void ApplyChestToEvent(Client* c, uint32_t entity_id, uint32_t event_id)
{
		const auto* template_data = RequireSelectedTemplate(c);
		if (!template_data) {
			return;
		}
		NPC* npc = entity_list.GetNPCByID(static_cast<uint16>(entity_id));
		if (!npc) {
			c->Message(Chat::Red, "That NPC is no longer present. Target a current NPC and try again.");
			return;
		}

		const ExpeditionDB::Event* event_data = nullptr;
		for (const auto& candidate : template_data->events) {
			if (candidate.id == event_id) {
				event_data = &candidate;
				break;
			}
		}
		if (!event_data) {
			c->Message(Chat::Red, "That boss event no longer exists - re-target the chest to link it.");
			return;
		}

		const uint32_t template_id = template_data->id;
		const std::string event_name = event_data->event_name;
		if (
			!ExpeditionDB::SetEventNpc(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), "chest") ||
			!ExpeditionDB::SetEventNpcCompleteOnDeath(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), false) ||
			!ExpeditionDB::SetEventNpcCompleteOnSpawn(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), true) ||
			!ExpeditionDB::SetEventNpcLoot(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), true)
		) {
			c->Message(Chat::Red, fmt::format("Failed to save loot chest setup for event [{}].", event_name).c_str());
			return;
		}
		ExpeditionDB::SetSelectedEvent(c->CharacterID(), event_id);
		c->Message(Chat::Green, fmt::format(
			"Saved: loot chest [{}] is linked to [{}] - its spawn now completes that boss's event and fires its lockout (loot protected).",
			NpcLabel(*npc), event_name).c_str());
		if (const auto* refreshed = ExpeditionDB::FindTemplate(template_id)) {
			ShowEditScreen(c, *refreshed);
		}
}

// When an expedition has more than one boss, ask which boss's event this chest should complete.
void ShowChestLinkChooser(Client* c, const ExpeditionDB::Template& template_data, NPC& npc, uint32_t entity_id)
{
		SendEditBanner(c, template_data);
		SendCardTop(c, "Link Loot Chest To Boss");
		c->Message(Chat::White, fmt::format("  Which boss should [{}] be linked to? Its spawn will complete that boss's event and trigger its lockout.",
			NpcLabel(npc)).c_str());
		c->Message(Chat::White, ChatSeparator());
		for (const auto& event_data : template_data.events) {
			c->Message(Chat::White, ("  " + Saylink::Silent(
				fmt::format("#expedition edit chestlink {} {}", entity_id, event_data.id),
				fmt::format("[ {} ]", event_data.event_name))).c_str());
		}
		c->Message(Chat::White, ChatSeparator());
		SendActionRow(c, {{"#expedition edit on", "Cancel"}});
		SendCardBottom(c);
}

void ApplyEditAdd(Client* c, uint32_t entity_id, const std::string& role)
{
		const auto* template_data = RequireSelectedTemplate(c);
		if (!template_data) {
			return;
		}
		NPC* npc = entity_list.GetNPCByID(static_cast<uint16>(entity_id));
		if (!npc) {
			c->Message(Chat::Red, "That NPC is no longer present. Target a current NPC and try again.");
			return;
		}

		const uint32_t template_id = template_data->id;
		const std::string template_name = template_data->name;

		if (role == "requester") {
			const uint32_t request_npc_id = ExpeditionDB::UpsertRequestNpc(
				content_db, template_id, zone->GetZoneID(),
				npc->GetNPCTypeID(), npc->GetSpawnPointID(),
				ExpeditionDB::kSimpleRequestPhrase, zone->GetInstanceVersion());
			if (!request_npc_id) {
				c->Message(Chat::Red, "Failed to save requester NPC mapping.");
				return;
			}
			c->Message(Chat::Green, fmt::format(
				"Saved: requester [{}] added to [{}] with phrase [{}].",
				NpcLabel(*npc), template_name, ExpeditionDB::kSimpleRequestPhrase).c_str());
			ExpeditionDB::ApplyRequesterLastName(*npc);
			if (const auto* refreshed = ExpeditionDB::FindTemplate(template_id)) {
				SendScriptedRequestNpcWarning(c, *refreshed, *npc);
				ShowEditScreen(c, *refreshed);
			}
			return;
		}

		if (role == "chest") {
			// A chest must be linked to a boss event (its spawn completes that event). Auto-link when
			// there is exactly one boss; otherwise ask which boss it belongs to.
			if (template_data->events.empty()) {
				c->Message(Chat::Red, "Add a boss first - a loot chest completes an existing boss event when it spawns.");
				RefreshBuilderView(c);
				return;
			}
			if (template_data->events.size() == 1) {
				ApplyChestToEvent(c, entity_id, template_data->events.front().id);
				return;
			}
			ShowChestLinkChooser(c, *template_data, *npc, entity_id);
			return;
		}

		const auto* event_data = FindBossEventForNpc(*template_data, *npc);
		const std::string event_name = event_data ? event_data->event_name : UniqueEventName(*template_data, BossEventName(*npc));
		const uint32_t event_id = event_data ? event_data->id : ExpeditionDB::AddEvent(content_db, template_id, event_name);
		if (!event_id) {
			c->Message(Chat::Red, "Failed to create boss completion event.");
			return;
		}
		ExpeditionDB::SetSelectedEvent(c->CharacterID(), event_id);
		if (
			!ExpeditionDB::SetEventNpc(content_db, event_id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), "boss") ||
			!ExpeditionDB::SetEventLockout(content_db, event_id, ExpeditionDB::kSimpleBossLockoutSeconds) ||
			!ExpeditionDB::SetEventReplay(content_db, event_id, ExpeditionDB::kSimpleBossReplaySeconds)
		) {
			c->Message(Chat::Red, fmt::format("Failed to save boss setup for event [{}].", event_name).c_str());
			return;
		}
		c->Message(Chat::Green, fmt::format(
			"Saved: boss [{}] added to [{}] via event [{}] (lockout {}, replay {}).",
			NpcLabel(*npc), template_name, event_name,
			Duration(ExpeditionDB::kSimpleBossLockoutSeconds), Duration(ExpeditionDB::kSimpleBossReplaySeconds)).c_str());
		if (const auto* refreshed = ExpeditionDB::FindTemplate(template_id)) {
			ShowEditScreen(c, *refreshed);
		}
}

void HandleEdit(Client* c, const Seperator* sep)
{
		const std::string action = Strings::ToLower(sep->arg[2]);

		if (action.empty() || action == "on") {
			if (sep->arg[3][0] != '\0') {
				const auto* picked = ExpeditionDB::FindTemplate(sep->arg[3]);
				if (!picked) {
					c->Message(Chat::Red, "Expedition not found.");
					return;
				}
				ExpeditionDB::SetSelectedTemplate(c->CharacterID(), picked->id);
			}
			const auto* template_data = RequireSelectedTemplate(c);
			if (!template_data) {
				return;
			}
			ExpeditionDB::GetBuilderState(c->CharacterID()).edit_mode = true;
			ShowEditScreen(c, *template_data);
			return;
		}

		if (action == "off") {
			ExpeditionDB::GetBuilderState(c->CharacterID()).edit_mode = false;
			c->Message(Chat::Yellow, "Stopped editing.");
			HandleHome(c, sep);
			return;
		}

		// Internal: chest-link chooser callback (#expedition edit chestlink <chest_entity_id> <event_id>).
		if (action == "chestlink") {
			if (!IsStrictUnsigned(sep->arg[3]) || !IsStrictUnsigned(sep->arg[4])) {
				c->Message(Chat::Red, "Pick a boss from the chest link menu.");
				return;
			}
			ApplyChestToEvent(c, Strings::ToUnsignedInt(sep->arg[3]), Strings::ToUnsignedInt(sep->arg[4]));
			return;
		}

		if (action == "help") {
			SendSectionHeader(c, "Expedition Edit Mode");
			c->Message(Chat::White, "Toggle a guided add flow for the selected expedition.");
			SendHelpLink(c, "#expedition edit on", "enable edit mode for the selected expedition");
			SendHelpLink(c, "#expedition edit off", "disable edit mode");
			c->Message(Chat::White, "While on, target any NPC to get role popups (Boss / Chest / Requester). A chest links to the boss whose event it completes.");
			return;
		}

		c->Message(Chat::Red, "Usage: #expedition edit on|off   (with edit mode on, target NPCs to add them)");
}

void HandleShow(Client* c, const Seperator* sep)
{
		const auto* template_data = ResolveTemplate(c, sep->arg[2]);
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		ShowTemplate(c, *template_data);
}

// Teleport the GM to a spawned NPC (used by the Show roster's GoTo links).
void HandleGoto(Client* c, const Seperator* sep)
{
		if (!IsStrictUnsigned(sep->arg[2])) {
			c->Message(Chat::Red, "Usage: #expedition goto <npc_type_id>");
			return;
		}
		NPC* npc = entity_list.GetNPCByNPCTypeID(Strings::ToUnsignedInt(sep->arg[2]));
		if (!npc) {
			c->Message(Chat::Yellow, "That NPC is not spawned in this zone right now.");
			return;
		}
		c->MovePC(zone->GetZoneID(), zone->GetInstanceID(), npc->GetX(), npc->GetY(), npc->GetZ(), npc->GetHeading());
		c->Message(Chat::Green, fmt::format("Teleported to {}.", npc->GetCleanName()).c_str());
}

// Target a spawned NPC (used by the Show roster's Target links).
void HandleTargetNpc(Client* c, const Seperator* sep)
{
		if (!IsStrictUnsigned(sep->arg[2])) {
			c->Message(Chat::Red, "Usage: #expedition target <npc_type_id>");
			return;
		}
		NPC* npc = entity_list.GetNPCByNPCTypeID(Strings::ToUnsignedInt(sep->arg[2]));
		if (!npc) {
			c->Message(Chat::Yellow, "That NPC is not spawned in this zone right now.");
			return;
		}
		c->SetTarget(npc);
		c->SendTargetCommand(npc->GetID());
		c->Message(Chat::Green, fmt::format("Targeted {}.", npc->GetCleanName()).c_str());
}

void HandlePreview(Client* c, const Seperator* sep)
{
		const auto* template_data = ResolveTemplate(c, sep->arg[2]);
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		ShowPreview(c, *template_data);
}

void HandleValidate(Client* c, const Seperator* sep)
{
		const auto* template_data = ResolveTemplate(c, sep->arg[2]);
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		PrintValidation(c, *template_data);
}

// 'fix' quick-repair flow, reachable via #expedition fix (routed through the set verb).
void HandleFix(Client* c, const Seperator* sep)
{
		const auto* template_data = SelectedTemplate(c);
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
			ExpeditionDB::UpsertRequestNpc(content_db, template_data->id, zone->GetZoneID(), npc->GetNPCTypeID(), npc->GetSpawnPointID(), ExpeditionDB::kSimpleRequestPhrase, zone->GetInstanceVersion());
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
		RefreshBuilderView(c);
}

void HandleDisable(Client* c, const Seperator* sep)
{
		const std::string sub = Strings::ToLower(sep->arg[1]);
		const auto* template_data = ResolveTemplate(c, sep->arg[2]);
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		if (sub == "enable") {
			const auto validation = ExpeditionDB::ValidateTemplate(*template_data);
			if (!validation.IsValid()) {
				PrintValidation(c, *template_data);
				c->Message(Chat::Red, "Fix validation errors before enabling this expedition.");
				return;
			}
		}
		// Unpublishing a live expedition can affect players currently inside it - confirm first.
		if (sub == "disable" && template_data->enabled && strcasecmp(sep->arg[3], "confirm") != 0) {
			c->Message(Chat::Yellow, fmt::format("Unpublish live expedition [{}]? Players may be inside it.", template_data->name).c_str());
			SendActionRow(c, {
				{fmt::format("#expedition disable {} confirm", template_data->id), "Confirm Unpublish"},
				{"#expedition", "Cancel"}
			});
			return;
		}
		const std::string template_name = template_data->name;
		ExpeditionDB::SetTemplateEnabled(content_db, template_data->id, sub == "enable");
		c->Message(Chat::Green, fmt::format("{} expedition [{}].", sub == "enable" ? "Enabled" : "Disabled", template_name).c_str());
		HandleHome(c, sep);
}

void HandleDelete(Client* c, const Seperator* sep)
{
		const auto* delete_template = ResolveTemplate(c, sep->arg[2]);
		if (!delete_template) {
			NeedSelection(c);
			return;
		}
		if (delete_template->enabled) {
			c->Message(Chat::Red, fmt::format("[{}] is published. Unpublish it before deleting.", delete_template->name).c_str());
			SendActionRow(c, {
				{fmt::format("#expedition disable {}", delete_template->id), "Unpublish"},
				{"#expedition", "Cancel"}
			});
			return;
		}
		if (strcasecmp(sep->arg[3], "confirm") != 0 && strcasecmp(sep->arg[4], "confirm") != 0) {
			c->Message(Chat::Yellow, fmt::format("Permanently delete draft expedition [{}]?", delete_template->name).c_str());
			SendActionRow(c, {
				{fmt::format("#expedition delete {} confirm", delete_template->id), "Confirm Delete"},
				{"#expedition", "Cancel"}
			});
			return;
		}
		const uint32_t deleted_id = delete_template->id;
		ExpeditionDB::DeleteTemplate(content_db, deleted_id);
		auto& builder_state = ExpeditionDB::GetBuilderState(c->CharacterID());
		if (builder_state.selected_template_id == deleted_id) {
			builder_state.selected_template_id = 0;
			builder_state.selected_event_id = 0;
			builder_state.edit_mode = false;
		}
		c->Message(Chat::Green, "Deleted expedition template.");
		HandleHome(c, sep);
}

void HandleSet(Client* c, const Seperator* sep)
{
		const std::string sub = Strings::ToLower(sep->arg[1]);

		// 'rename' is an alias of 'set name'; rewrite its arguments before dispatching.
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

			if (!ApplyTemplateRename(c, rename_template->id, rename_template->name, new_name)) {
				return;
			}

			RefreshBuilderView(c);
			return;
		}

		// 'fix' quick-repair shortcuts are reachable via the set verb.
		if (sub == "fix") {
			HandleFix(c, sep);
			return;
		}

		const std::string field = Strings::ToLower(sep->arg[2]);
		if (field.empty() || field == "help") {
			ShowSetHelp(c);
			return;
		}
		const auto* template_data = SelectedTemplate(c);
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
			if (!ApplyTemplateRename(c, template_data->id, template_data->name, new_name)) {
				return;
			}
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
		else if (field == "bossonly" || field == "boss_only") {
			bool enabled = false;
			if (!ParseOnOffArg(sep->arg[3], enabled)) {
				c->Message(Chat::Red, "Usage: #expedition set bossonly on|off");
				return;
			}
			ExpeditionDB::SetTemplateBossOnlySpawn(content_db, template_data->id, enabled);
			c->Message(Chat::Green, fmt::format("Set boss-only spawns to [{}].", OnOff(enabled)).c_str());
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
		RefreshBuilderView(c);
}

void HandleEvent(Client* c, const Seperator* sep)
{
		const std::string action = Strings::ToLower(sep->arg[2]);
		if (action.empty() || action == "help") {
			ShowEventHelp(c);
			return;
		}
		if (action == "rename" && (sep->arg[3][0] == '\0' || strcasecmp(sep->arg[3], "help") == 0)) {
			ShowEventRenameHelp(c);
			return;
		}
		const auto* template_data = SelectedTemplate(c);
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
			RefreshBuilderView(c);
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
			RefreshBuilderView(c);
			return;
		}

		if (action == "list") {
			if (template_data->events.empty()) {
				c->Message(Chat::White, "No events are configured for the selected expedition.");
				RefreshBuilderView(c);
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
			RefreshBuilderView(c);
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
				RefreshBuilderView(c);
				return;
			}

			if (!rename_event) {
				c->Message(Chat::Red, "Select exactly one event first, or use #expedition event rename <id> \"New Name\".");
				RefreshBuilderView(c);
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
			RefreshBuilderView(c);
			return;
		}

		const auto* event_data = RequireSelectedEvent(c, *template_data);
		if (!event_data) {
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
				RefreshBuilderView(c);
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
			RefreshBuilderView(c);
			return;
		}

		if (action == "lockout") {
			if (sep->arg[3][0] == '\0') {
				const auto* ev = ExpeditionDB::FindEvent(selected_event_id);
				ShowTimeMatrix(c, fmt::format("Boss Lockout: {}", selected_event_name),
					"#expedition event lockout", Duration(ev ? ev->lockout_seconds : 0), true);
				return;
			}
			const bool clear = IsClearDurationArg(sep->arg[3]);
			const uint32_t seconds = clear ? 0 : ParseExpeditionDuration(sep->arg[3]);
			if (!seconds && !clear) {
				c->Message(Chat::Red, "Usage: #expedition event lockout none|<duration>");
				return;
			}
			if (!ExpeditionDB::SetEventLockout(content_db, selected_event_id, seconds)) {
				c->Message(Chat::Red, fmt::format("Failed to set event [{}] lockout.", selected_event_name).c_str());
				return;
			}
			c->Message(Chat::Green, fmt::format("Saved: event [{}] lockout is now [{}].", selected_event_name, Duration(seconds)).c_str());
			RefreshBuilderView(c);
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
			RefreshBuilderView(c);
			return;
		}

		if (action == "npc" || action == "loot" || action == "completeondeath" || action == "completeonspawn") {
			NPC* npc = RequireTargetNpc(c);
			if (!npc) {
				return;
			}

			if (action == "npc") {
				if (Strings::EqualFold(sep->arg[3], "remove")) {
					if (strcasecmp(sep->arg[4], "confirm") != 0) {
						c->Message(Chat::Yellow, "Type #expedition event npc remove confirm to remove the targeted NPC mapping from the selected event.");
						return;
					}
					const auto* mapped_npc = FindEventNpc(*event_data, *npc);
					if (!mapped_npc) {
						c->Message(Chat::Yellow, "Target is not mapped to the selected event.");
						return;
					}
					const bool removes_event = ExpeditionDB::EventNpcRemovalDeletesEvent(*mapped_npc);
					if (!ExpeditionDB::DeleteEventNpc(content_db, selected_event_id, mapped_npc->npc_type_id, mapped_npc->spawn2_id)) {
						c->Message(Chat::Red, fmt::format("Failed to remove target NPC mapping for event [{}].", selected_event_name).c_str());
						return;
					}
					if (removes_event) {
						if (const auto* refreshed_template = SelectedTemplate(c)) {
							const uint32_t next_event_id = refreshed_template->events.empty() ? 0 : refreshed_template->events.front().id;
							ExpeditionDB::SetSelectedEvent(c->CharacterID(), next_event_id);
						}
						c->Message(Chat::Green, fmt::format("Saved: removed boss [{}] and event [{}].", NpcLabel(*npc), selected_event_name).c_str());
					}
					else {
						c->Message(Chat::Green, fmt::format("Saved: removed target NPC mapping from event [{}].", selected_event_name).c_str());
					}
					RefreshBuilderView(c);
					return;
				}

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
				const EventNpcMapping mapping = GetOrCreateEventNpcMapping(c, selected_event_id, *event_data, *npc, selected_event_name);
				if (!mapping.ok) {
					return;
				}
				const uint32_t mapping_npc_type_id = mapping.npc_type_id;
				const uint32_t mapping_spawn2_id = mapping.spawn2_id;
				if (!ExpeditionDB::SetEventNpcLoot(content_db, selected_event_id, mapping_npc_type_id, mapping_spawn2_id, enabled)) {
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
					c->Message(Chat::Red, action == "completeonspawn" ? "Usage: #expedition event completeonspawn on|off" : "Usage: #expedition event completeondeath on|off");
					return;
				}
				const EventNpcMapping mapping = GetOrCreateEventNpcMapping(c, selected_event_id, *event_data, *npc, selected_event_name);
				if (!mapping.ok) {
					return;
				}
				const uint32_t mapping_npc_type_id = mapping.npc_type_id;
				const uint32_t mapping_spawn2_id = mapping.spawn2_id;
				const bool ok = action == "completeonspawn" ?
					ExpeditionDB::SetEventNpcCompleteOnSpawn(content_db, selected_event_id, mapping_npc_type_id, mapping_spawn2_id, enabled) :
					ExpeditionDB::SetEventNpcCompleteOnDeath(content_db, selected_event_id, mapping_npc_type_id, mapping_spawn2_id, enabled);
				if (!ok) {
					c->Message(Chat::Red, fmt::format("Failed to set completion trigger for event [{}].", selected_event_name).c_str());
					return;
				}
				c->Message(Chat::Green, fmt::format(
					"Saved: event [{}] target NPC [{}] {} is [{}].",
					selected_event_name,
					NpcLabel(*npc),
					action == "completeonspawn" ? "complete-on-spawn" : "complete-on-death",
					OnOff(enabled)
				).c_str());
			}
			RefreshBuilderView(c);
			return;
		}

		c->Message(Chat::Red, "Unknown #expedition event option.");
		ShowEventHelp(c);
}

void HandleAction(Client* c, const Seperator* sep)
{
		const std::string action = Strings::ToLower(sep->arg[2]);
		if (action.empty() || action == "help") {
			ShowActionHelp(c);
			return;
		}
		const auto* template_data = SelectedTemplate(c);
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		const auto* event_data = RequireSelectedEvent(c, *template_data);
		if (!event_data) {
			return;
		}
		const uint32_t selected_event_id = event_data->id;
		const std::string selected_event_name = event_data->event_name;

		if (action == "list") {
			if (event_data->actions.empty()) {
				c->Message(Chat::White, "Selected event has no runtime actions.");
				RefreshBuilderView(c);
				return;
			}
			for (const auto& event_action : event_data->actions) {
				c->Message(Chat::White, fmt::format("[{}] {} [{}]", event_action.id, event_action.action_type, event_action.action_value).c_str());
			}
			RefreshBuilderView(c);
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
			RefreshBuilderView(c);
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
		RefreshBuilderView(c);
}

void HandlePreset(Client* c, const Seperator* sep)
{
		const std::string preset = Strings::ToLower(sep->arg[2]);
		if (preset.empty() || preset == "help") {
			ShowPresetHelp(c);
			return;
		}
		const auto* template_data = SelectedTemplate(c);
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
				RefreshBuilderView(c);
				return;
			}
			const uint32_t event_id = event_data->id;
			NPC* npc = TargetNpc(c);
			if (!npc) {
				NeedTargetNpc(c);
				RefreshBuilderView(c);
				return;
			}
			if (
				!ExpeditionDB::SetEventNpc(content_db, event_id, npc->GetNPCTypeID(), 0, "chest") ||
				!ExpeditionDB::SetEventNpcCompleteOnSpawn(content_db, event_id, npc->GetNPCTypeID(), 0, true) ||
				!ExpeditionDB::SetEventNpcLoot(content_db, event_id, npc->GetNPCTypeID(), 0, true)
			) {
				c->Message(Chat::Red, fmt::format("Failed to apply loot chest preset to event [{}].", event_data->event_name).c_str());
				return;
			}
			c->Message(Chat::Green, fmt::format("Saved: loot chest preset applied to event [{}] with target NPC [{}].", event_data->event_name, NpcLabel(*npc)).c_str());
		}
		else {
			ShowPresetHelp(c);
		}
		RefreshBuilderView(c);
}

void HandleTest(Client* c, const Seperator* sep)
{
		const std::string action = Strings::ToLower(sep->arg[2]);
		if (action.empty() || action == "help") {
			ShowTestHelp(c);
			return;
		}
		const auto* template_data = SelectedTemplate(c);
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
			NPC* npc = RequireTargetNpc(c);
			if (!npc) {
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
					RefreshBuilderView(c);
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
}

} // namespace


// Routes a two-button edit-mode popup response (from Handle_OP_PopupResponse) to the next dialog
// step or the apply. The popup id encodes the step and the targeted NPC's entity id (stateless).
void ExpeditionEditPopupResponse(Client* c, uint32_t popup_id)
{
	if (!c || !c->GetGM()) {
		return;
	}

	const uint32_t step = ExpeditionEditPopup::StepOf(popup_id);
	const uint16_t entity_id = ExpeditionEditPopup::EntityOf(popup_id);

	if (step == ExpeditionEditPopup::Skip) {
		c->Message(Chat::Yellow, "NPC not added. Target another NPC to add it, or use the buttons below.");
		RefreshBuilderView(c);
		return;
	}

	const auto* template_data = RequireSelectedTemplate(c);
	if (!template_data) {
		return;
	}

	NPC* npc = entity_list.GetNPCByID(entity_id);
	if (!npc) {
		c->Message(Chat::Red, "That NPC is no longer present. Target a current NPC and try again.");
		return;
	}

	switch (step) {
		case ExpeditionEditPopup::AddBoss:      ApplyEditAdd(c, entity_id, "boss"); break;
		case ExpeditionEditPopup::AskChest:
			// A chest can only complete an existing boss event - skip the chest question entirely
			// when no boss exists yet and go straight to the Requester question.
			if (template_data->events.empty()) {
				ShowRoleQuestion(c, *template_data, *npc, entity_id, "requester");
			}
			else {
				ShowRoleQuestion(c, *template_data, *npc, entity_id, "chest");
			}
			break;
		case ExpeditionEditPopup::AddChest:     ApplyEditAdd(c, entity_id, "chest"); break;
		case ExpeditionEditPopup::AskRequester: ShowRoleQuestion(c, *template_data, *npc, entity_id, "requester"); break;
		case ExpeditionEditPopup::AddRequester: ApplyEditAdd(c, entity_id, "requester"); break;
		default: break;
	}
}
