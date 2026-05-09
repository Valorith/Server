#include "expedition_config.h"

#include "common/strings.h"
#include "expedition_request.h"
#include "zone/client.h"
#include "zone/zone.h"

uint32_t ParseExpeditionDuration(const std::string& duration)
{
	if (duration.empty()) {
		return 0;
	}

	if (Strings::IsNumber(duration)) {
		return Strings::ToUnsignedInt(duration);
	}

	return Strings::TimeToSeconds(duration);
}

DynamicZone* CreateExpeditionWithOptions(Client& client, DynamicZone& dz, const ExpeditionCreationOptions& options)
{
	DynamicZone* expedition = client.CreateExpedition(dz, options.silent);
	if (!expedition) {
		return nullptr;
	}

	if (options.has_replay_on_join) {
		expedition->SetReplayOnJoin(options.replay_on_join, true);
	}

	if (options.has_replay_lockout && options.replay_lockout_seconds > 0) {
		expedition->AddLockout(DzLockout::ReplayTimer, options.replay_lockout_seconds);
	}

	return expedition;
}

DynamicZone* CreateExpeditionFromTemplateName(Client& client, const std::string& template_name)
{
	if (!zone || template_name.empty()) {
		return nullptr;
	}

	for (const auto& [template_id, dz_template] : zone->dz_template_cache) {
		if (Strings::EqualFold(dz_template.name, template_name)) {
			return client.CreateExpeditionFromTemplate(template_id);
		}
	}

	return nullptr;
}

ExpeditionCheckResult CheckExpeditionRequest(Client& client, const DynamicZone& dz, bool silent)
{
	ExpeditionCheckResult result;
	result.min_players = dz.GetMinPlayers();
	result.max_players = dz.GetMaxPlayers();

	if (!dz.IsExpedition()) {
		result.reason = "invalid_dynamic_zone_type";
		return result;
	}

	ExpeditionRequest request(dz, client, silent);
	result.success = request.Validate();
	result.member_count = static_cast<uint32_t>(request.GetMembers().size());
	result.is_raid = request.IsRaid();
	result.reason = result.success ? "ok" : request.GetFailureReason();
	if (result.reason.empty()) {
		result.reason = "requirements_not_met";
	}

	return result;
}
