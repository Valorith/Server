#include "zone/client.h"
#include "zone/dialogue_window.h"
#include "zone/expedition_config.h"
#include "zone/expedition_db.h"
#include "zone/npc.h"
#include "zone/zone.h"
#include "zone/zonedb.h"

#include "common/say_link.h"
#include "common/strings.h"
#include "common/zone_store.h"

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
			if (const auto* event_data = ExpeditionDB::FindEvent(state.selected_event_id)) {
				return event_data;
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

	std::string Duration(uint32_t seconds)
	{
		if (!seconds) {
			return "none";
		}

		return Strings::SecondsToTime(seconds);
	}

	void ShowHelp(Client* c)
	{
		c->Message(Chat::White, "#expedition help - Show this help.");
		c->Message(Chat::White, fmt::format(
			"{} - Open the expedition builder menu. {} - List templates. {} - Reload DB templates.",
			Saylink::Silent("#expedition menu", "#expedition menu"),
			Saylink::Silent("#expedition list", "#expedition list"),
			Saylink::Silent("#expedition reload", "#expedition reload")
		).c_str());
		c->Message(Chat::White, "#expedition create \"Name\" - Create and select an expedition using your current zone/location.");
		c->Message(Chat::White, "#expedition select <id|name> - Select a template for short follow-up commands.");
		c->Message(Chat::White, "#expedition set zone - Use your current zone. Add a zone short name/id to override.");
		c->Message(Chat::White, "#expedition set duration <duration> - Set duration, e.g. 6h or 21600.");
		c->Message(Chat::White, "#expedition set players <min> <max> - Set member bounds.");
		c->Message(Chat::White, "#expedition set zonein - Use your current location. You can also provide x y z [h].");
		c->Message(Chat::White, "#expedition set safereturn - Use your current location as safe return.");
		c->Message(Chat::White, "#expedition set compass - Point compass to your current location.");
		c->Message(Chat::White, "#expedition requestnpc [phrase] - Use your targeted NPC as the request NPC. Defaults to 'expedition'.");
		c->Message(Chat::White, "#expedition event add \"Name\" - Add and select an event.");
		c->Message(Chat::White, "#expedition event npc [boss|add|chest|loot] - Add your targeted NPC to the selected event.");
		c->Message(Chat::White, "#expedition event lockout <duration> - Set selected event lockout.");
		c->Message(Chat::White, "#expedition event loot on|off - Toggle loot protection for the targeted NPC.");
		c->Message(Chat::White, "#expedition validate - Check for missing or risky setup.");
		c->Message(Chat::White, "#expedition enable - Publish the selected expedition. #expedition disable - Unpublish it.");
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
			"Expedition [{}] id [{}] dz_template [{}] enabled [{}]",
			template_data.name,
			template_data.id,
			template_data.dz_template_id,
			OnOff(template_data.enabled)
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

	void ShowMenu(Client* c)
	{
		const auto* template_data = SelectedTemplate(c);
		std::string body;
		body += DialogueWindow::CenterMessage(DialogueWindow::ColorMessage("gold", "Expedition Builder"));
		body += DialogueWindow::Break(2);

		if (!template_data) {
			body += "No expedition selected.";
			body += DialogueWindow::Break();
			body += Saylink::Silent("#expedition list", "List Expeditions");
			c->SendPopupToClient("Expedition Builder", body.c_str());
			return;
		}

		const auto validation = ExpeditionDB::ValidateTemplate(*template_data);
		std::string table;
		table += DialogueWindow::TableRow(DialogueWindow::TableCell("Name") + DialogueWindow::TableCell(template_data->name));
		table += DialogueWindow::TableRow(DialogueWindow::TableCell("Status") + DialogueWindow::TableCell(validation.StatusName()));
		table += DialogueWindow::TableRow(DialogueWindow::TableCell("Enabled") + DialogueWindow::TableCell(OnOff(template_data->enabled)));
		table += DialogueWindow::TableRow(DialogueWindow::TableCell("Zone") + DialogueWindow::TableCell(fmt::format("{}:{}", ZoneName(template_data->dz_template.zone_id, true), template_data->dz_template.zone_version)));
		table += DialogueWindow::TableRow(DialogueWindow::TableCell("Duration") + DialogueWindow::TableCell(Duration(template_data->dz_template.duration_seconds)));
		table += DialogueWindow::TableRow(DialogueWindow::TableCell("Players") + DialogueWindow::TableCell(fmt::format("{}-{}", template_data->dz_template.min_players, template_data->dz_template.max_players)));
		body += DialogueWindow::Table(table);
		body += DialogueWindow::Break(2);
		body += DialogueWindow::ColorMessage("gold", "Quick Setup");
		body += DialogueWindow::Break();
		body += fmt::format(
			"{} | {} | {} | {} | {}",
			Saylink::Silent("#expedition set zone", "Set Zone"),
			Saylink::Silent("#expedition set zonein", "Set Zone-In"),
			Saylink::Silent("#expedition set safereturn", "Set Safe Return"),
			Saylink::Silent("#expedition set compass", "Set Compass"),
			Saylink::Silent("#expedition requestnpc", "Use Target Request NPC")
		);
		body += DialogueWindow::Break(2);
		body += DialogueWindow::ColorMessage("gold", "Events");
		body += DialogueWindow::Break();
		body += fmt::format(
			"{} | {} | {} | {}",
			Saylink::Silent("#expedition event add \"Boss Defeated\"", "Add Event"),
			Saylink::Silent("#expedition event npc", "Add Target NPC"),
			Saylink::Silent("#expedition event loot on", "Protect Target Loot"),
			Saylink::Silent("#expedition event lockout 6h", "Set 6h Lockout")
		);
		body += DialogueWindow::Break(2);
		body += DialogueWindow::ColorMessage("gold", "Validation and Testing");
		body += DialogueWindow::Break();
		body += fmt::format(
			"{} | {} | {} | {} | {}",
			Saylink::Silent("#expedition validate", "Validate"),
			Saylink::Silent("#expedition test create", "Test Create"),
			Saylink::Silent("#expedition test move", "Test Move"),
			Saylink::Silent("#expedition enable", "Enable"),
			Saylink::Silent("#expedition disable", "Disable")
		);
		body += DialogueWindow::Break(2);
		body += Saylink::Silent(fmt::format("#expedition delete {} confirm", template_data->id), "Delete Confirm");

		c->SendPopupToClient("Expedition Builder", body.c_str());
	}

	uint32_t ParseZoneArg(const char* arg)
	{
		if (!arg || arg[0] == '\0') {
			return zone ? zone->GetZoneID() : 0;
		}

		return Strings::IsNumber(arg) ? Strings::ToUnsignedInt(arg) : ZoneID(arg);
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
		std::string name = sep->argplus[2];
		Strings::Trim(name);
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

	if (sub == "select") {
		const auto* template_data = ExpeditionDB::FindTemplate(sep->argplus[2]);
		if (!template_data) {
			c->Message(Chat::Red, "Expedition template not found.");
			return;
		}

		ExpeditionDB::SetSelectedTemplate(c->CharacterID(), template_data->id);
		c->Message(Chat::Green, fmt::format("Selected expedition [{}] ({}).", template_data->id, template_data->name).c_str());
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

	if (sub == "validate") {
		if (!template_data) {
			NeedSelection(c);
			return;
		}
		PrintValidation(c, *template_data);
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
				return;
			}
		}
		ExpeditionDB::SetTemplateEnabled(content_db, template_data->id, sub == "enable");
		c->Message(Chat::Green, fmt::format("{} expedition [{}].", sub == "enable" ? "Enabled" : "Disabled", template_data->name).c_str());
		return;
	}

	if (sub == "delete") {
		const auto* delete_template = ResolveTemplate(c, sep->arg[2]);
		if (!delete_template) {
			NeedSelection(c);
			return;
		}
		if (strcasecmp(sep->arg[3], "confirm") != 0 && strcasecmp(sep->arg[4], "confirm") != 0) {
			c->Message(Chat::Yellow, fmt::format("Confirm delete with: #expedition delete {} confirm", delete_template->id).c_str());
			return;
		}
		ExpeditionDB::DeleteTemplate(content_db, delete_template->id);
		c->Message(Chat::Green, "Deleted DB expedition template.");
		return;
	}

	template_data = SelectedTemplate(c);
	if (!template_data) {
		NeedSelection(c);
		return;
	}

	if (sub == "set") {
		const std::string field = Strings::ToLower(sep->arg[2]);
		if (field == "zone") {
			const uint32_t zone_id = ParseZoneArg(sep->arg[3]);
			const uint32_t version = sep->IsNumber(4) ? Strings::ToUnsignedInt(sep->arg[4]) : (zone_id == zone->GetZoneID() ? zone->GetInstanceVersion() : 0);
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
			if (!sep->IsNumber(3) || !sep->IsNumber(4)) {
				c->Message(Chat::Red, "Usage: #expedition set players <min> <max>");
				return;
			}
			ExpeditionDB::SetDzTemplatePlayers(content_db, template_data->dz_template_id, Strings::ToUnsignedInt(sep->arg[3]), Strings::ToUnsignedInt(sep->arg[4]));
			c->Message(Chat::Green, "Updated player bounds.");
		}
		else if (field == "zonein") {
			if (sep->IsNumber(3) && sep->IsNumber(4) && sep->IsNumber(5)) {
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
				const auto& pos = c->GetPosition();
				ExpeditionDB::SetDzTemplateZoneIn(content_db, template_data->dz_template_id, pos.x, pos.y, pos.z, pos.w);
			}
			c->Message(Chat::Green, "Set zone-in location.");
		}
		else if (field == "safereturn") {
			if (sep->IsNumber(4) && sep->IsNumber(5) && sep->IsNumber(6)) {
				const uint32_t return_zone = ParseZoneArg(sep->arg[3]);
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
				const auto& pos = c->GetPosition();
				ExpeditionDB::SetDzTemplateSafeReturn(content_db, template_data->dz_template_id, zone->GetZoneID(), pos.x, pos.y, pos.z, pos.w);
			}
			c->Message(Chat::Green, "Set safe return location.");
		}
		else if (field == "compass") {
			if (sep->IsNumber(4) && sep->IsNumber(5) && sep->IsNumber(6)) {
				const uint32_t compass_zone = ParseZoneArg(sep->arg[3]);
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
				const auto& pos = c->GetPosition();
				ExpeditionDB::SetDzTemplateCompass(content_db, template_data->dz_template_id, zone->GetZoneID(), pos.x, pos.y, pos.z);
			}
			c->Message(Chat::Green, "Set compass location.");
		}
		else if (field == "switchid") {
			uint32_t switch_id = 0;
			if (strcasecmp(sep->arg[3], "target") == 0 && c->GetTarget()) {
				switch_id = c->GetTarget()->GetID();
			}
			else if (sep->IsNumber(3)) {
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
			const uint32_t seconds = ParseExpeditionDuration(sep->arg[3]);
			ExpeditionDB::SetTemplateReplay(content_db, template_data->id, seconds);
			c->Message(Chat::Green, fmt::format("Set replay lockout to [{}].", Duration(seconds)).c_str());
		}
		else if (field == "silent") {
			const bool enabled = Strings::ToBool(sep->arg[3]);
			ExpeditionDB::SetTemplateSilent(content_db, template_data->id, enabled);
			c->Message(Chat::Green, fmt::format("Set silent to [{}].", OnOff(enabled)).c_str());
		}
		else {
			c->Message(Chat::Red, "Unknown #expedition set option. Use #expedition help.");
		}
		return;
	}

	if (sub == "requestnpc") {
		NPC* npc = TargetNpc(c);
		if (!npc) {
			NeedTargetNpc(c);
			return;
		}

		if (strcasecmp(sep->arg[2], "remove") == 0) {
			ExpeditionDB::DeleteRequestNpc(content_db, template_data->id, npc->GetNPCTypeID(), npc->GetSpawnPointID());
			c->Message(Chat::Green, "Removed targeted request NPC.");
			return;
		}

		if (strcasecmp(sep->arg[2], "list") == 0) {
			for (const auto& request_npc : template_data->request_npcs) {
				c->Message(Chat::White, fmt::format(
					"NPC type [{}] spawn [{}] zone [{}] phrase [{}]",
					request_npc.npc_type_id,
					request_npc.spawn2_id,
					request_npc.zone_id,
					request_npc.phrase
				).c_str());
			}
			return;
		}

		std::string phrase = sep->argplus[2];
		Strings::Trim(phrase);
		if (phrase.empty()) {
			phrase = "expedition";
		}
		ExpeditionDB::UpsertRequestNpc(content_db, template_data->id, zone->GetZoneID(), npc->GetNPCTypeID(), npc->GetSpawnPointID(), phrase);
		c->Message(Chat::Green, fmt::format("Set targeted NPC as request NPC with phrase [{}].", phrase).c_str());
		return;
	}

	if (sub == "event") {
		const std::string action = Strings::ToLower(sep->arg[2]);
		if (action == "add") {
			std::string event_name = sep->argplus[3];
			Strings::Trim(event_name);
			if (event_name.empty()) {
				c->Message(Chat::Red, "Usage: #expedition event add \"Event Name\"");
				return;
			}
			const uint32_t event_id = ExpeditionDB::AddEvent(content_db, template_data->id, event_name);
			ExpeditionDB::SetSelectedEvent(c->CharacterID(), event_id);
			c->Message(Chat::Green, fmt::format("Added and selected event [{}].", event_name).c_str());
			return;
		}

		if (action == "select") {
			const auto* event_data = ExpeditionDB::FindEvent(*template_data, sep->argplus[3]);
			if (!event_data) {
				c->Message(Chat::Red, "Event not found.");
				return;
			}
			ExpeditionDB::SetSelectedEvent(c->CharacterID(), event_data->id);
			c->Message(Chat::Green, fmt::format("Selected event [{}].", event_data->event_name).c_str());
			return;
		}

		if (action == "list") {
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
			return;
		}

		const auto* event_data = SelectedEvent(c, *template_data);
		if (!event_data) {
			c->Message(Chat::Red, "Add or select an event first.");
			return;
		}

		if (action == "remove") {
			if (strcasecmp(sep->arg[3], "confirm") != 0) {
				c->Message(Chat::Yellow, "Confirm with: #expedition event remove confirm");
				return;
			}
			ExpeditionDB::DeleteEvent(content_db, event_data->id);
			c->Message(Chat::Green, "Removed selected event.");
			return;
		}

		if (action == "lockout") {
			const uint32_t seconds = ParseExpeditionDuration(sep->arg[3]);
			if (!seconds) {
				c->Message(Chat::Red, "Usage: #expedition event lockout <duration>");
				return;
			}
			ExpeditionDB::SetEventLockout(content_db, event_data->id, seconds);
			c->Message(Chat::Green, fmt::format("Set event lockout to [{}].", Duration(seconds)).c_str());
			return;
		}

		if (action == "replay") {
			const uint32_t seconds = ParseExpeditionDuration(sep->arg[3]);
			ExpeditionDB::SetEventReplay(content_db, event_data->id, seconds);
			c->Message(Chat::Green, fmt::format("Set event replay lockout to [{}].", Duration(seconds)).c_str());
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
				ExpeditionDB::SetEventNpc(content_db, event_data->id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), role);
				c->Message(Chat::Green, fmt::format("Added target NPC to event [{}] as [{}].", event_data->event_name, role).c_str());
			}
			else if (action == "loot") {
				const bool enabled = sep->arg[3][0] == '\0' || Strings::ToBool(sep->arg[3]);
				ExpeditionDB::SetEventNpc(content_db, event_data->id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), "loot");
				ExpeditionDB::SetEventNpcLoot(content_db, event_data->id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), enabled);
				c->Message(Chat::Green, fmt::format("Set target loot protection [{}].", OnOff(enabled)).c_str());
			}
			else {
				const bool enabled = sep->arg[3][0] == '\0' || Strings::ToBool(sep->arg[3]);
				ExpeditionDB::SetEventNpcCompleteOnDeath(content_db, event_data->id, npc->GetNPCTypeID(), npc->GetSpawnPointID(), enabled);
				c->Message(Chat::Green, fmt::format("Set complete-on-death [{}].", OnOff(enabled)).c_str());
			}
			return;
		}

		c->Message(Chat::Red, "Unknown #expedition event option. Use #expedition help.");
		return;
	}

	if (sub == "test") {
		const std::string action = Strings::ToLower(sep->arg[2]);
		if (action == "create") {
			if (DynamicZone* dz = ExpeditionDB::CreateExpeditionFromTemplate(*c, *template_data)) {
				c->Message(Chat::Green, fmt::format("Created test expedition [{}].", dz->GetName()).c_str());
			}
			else {
				c->Message(Chat::Red, "Failed to create test expedition.");
			}
		}
		else if (action == "move") {
			c->MovePCExpedition(true);
		}
		else if (action == "request") {
			NPC* npc = TargetNpc(c);
			if (!npc) {
				NeedTargetNpc(c);
				return;
			}
			ExpeditionDB::HandleRequestSay(*c, *npc, template_data->request_phrase);
		}
		else if (action == "lockout") {
			if (DynamicZone* dz = c->GetExpedition()) {
				const auto* event_data = SelectedEvent(c, *template_data);
				if (event_data) {
					dz->AddLockout(event_data->event_name, event_data->lockout_seconds);
					c->Message(Chat::Green, "Applied selected event lockout.");
				}
			}
		}
		else if (action == "loot") {
			if (DynamicZone* dz = c->GetExpedition()) {
				ExpeditionDB::ApplyLootEvents(*dz);
				c->Message(Chat::Green, "Re-applied DB loot-event protection for your expedition.");
			}
		}
		else {
			c->Message(Chat::Red, "Usage: #expedition test create|move|request|lockout|loot");
		}
		return;
	}

	ShowHelp(c);
}
