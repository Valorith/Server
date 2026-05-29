#pragma once

#include "zone/dynamic_zone.h"

#include <cstdint>
#include <string>

class Client;

struct ExpeditionCreationOptions
{
	bool silent = false;
	bool has_replay_lockout = false;
	bool has_replay_on_join = false;
	bool replay_on_join = true;
	uint32_t replay_lockout_seconds = 0;
};

struct ExpeditionCheckResult
{
	bool success = false;
	uint32_t member_count = 0;
	uint32_t min_players = 0;
	uint32_t max_players = 0;
	bool is_raid = false;
	std::string reason;
};

uint32_t ParseExpeditionDuration(const std::string& duration);
DynamicZone* CreateExpeditionWithOptions(Client& client, DynamicZone& dz, const ExpeditionCreationOptions& options);
DynamicZone* CreateExpeditionFromTemplateName(Client& client, const std::string& template_name);
ExpeditionCheckResult CheckExpeditionRequest(Client& client, const DynamicZone& dz, bool silent = true);

// Two-button popup wiring for the in-zone expedition edit-mode flow. The popup id encodes the step
// and the targeted NPC's entity id, so the flow is stateless: id = kBase | (step << 16) | entity_id.
namespace ExpeditionEditPopup
{
	inline constexpr uint32_t kBase = 0x40000000u; // high but positive (avoids signed-popupid quirks); no collision with engine POPUPID_*
	// Sequential per-role add flow: each role is offered as a Yes/No popup. "Yes" -> Add<role>;
	// "No" -> Ask<next role>; "No" on the last -> Skip (NPC not added).
	enum Step : uint32_t
	{
		Skip         = 0,
		AddBoss      = 1,
		AskChest     = 2,
		AddChest     = 3,
		AskRequester = 4,
		AddRequester = 5
	};

	inline uint32_t Make(uint32_t step, uint16_t entity_id) { return kBase | (step << 16) | entity_id; }
	inline bool Owns(uint32_t popup_id) { return (popup_id & 0xFFF00000u) == kBase; }
	inline uint32_t StepOf(uint32_t popup_id) { return (popup_id >> 16) & 0xFu; }
	inline uint16_t EntityOf(uint32_t popup_id) { return static_cast<uint16_t>(popup_id & 0xFFFFu); }
}

// Dispatches a two-button popup response for the expedition edit-mode add flow (defined in gm_commands/expedition.cpp).
void ExpeditionEditPopupResponse(Client* client, uint32_t popup_id);
