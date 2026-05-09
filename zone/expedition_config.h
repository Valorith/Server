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
