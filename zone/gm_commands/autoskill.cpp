#include "zone/client.h"

#include "common/auto_skill.h"
#include "common/rulesys.h"
#include "common/say_link.h"
#include "common/strings.h"

#include "fmt/format.h"

#include <sstream>
#include <vector>

namespace {

bool TryParseAutoSkillState(const std::string &state_token, bool &enabled)
{
	const auto normalized_state = EQ::skills::autoskill::NormalizeSkillName(state_token);

	if (
		normalized_state == "on" ||
		normalized_state == "enable" ||
		normalized_state == "enabled" ||
		normalized_state == "true" ||
		normalized_state == "1"
	) {
		enabled = true;
		return true;
	}

	if (
		normalized_state == "off" ||
		normalized_state == "disable" ||
		normalized_state == "disabled" ||
		normalized_state == "false" ||
		normalized_state == "0"
	) {
		enabled = false;
		return true;
	}

	return false;
}

std::vector<std::string> TokenizeCommandRemainder(const std::string &remainder)
{
	std::vector<std::string> tokens;
	std::istringstream stream(remainder);
	std::string token;

	while (stream >> token) {
		tokens.push_back(token);
	}

	return tokens;
}

std::string JoinTokens(const std::vector<std::string> &tokens)
{
	std::string joined_tokens;

	for (const auto &token : tokens) {
		if (!joined_tokens.empty()) {
			joined_tokens.append(" ");
		}

		joined_tokens.append(token);
	}

	return joined_tokens;
}

std::string GetApplicableSkillsList(Client *client)
{
	std::vector<std::string> skill_names;

	for (const auto skill : client->GetApplicableAutoSkills()) {
		const auto *definition = EQ::skills::autoskill::GetSkillDefinition(skill);
		if (definition) {
			skill_names.emplace_back(definition->name);
		}
	}

	return Strings::Join(skill_names, ", ");
}

void ShowAutoSkillMenuEntry(Client *client, EQ::skills::SkillType skill)
{
	const auto *definition = EQ::skills::autoskill::GetSkillDefinition(skill);
	if (!definition) {
		return;
	}

	const bool enabled = client->IsAutoSkillEnabled(skill);
	const auto toggle_link = Saylink::Silent(
		fmt::format(
			"#autoskill {} {}",
			definition->command_name,
			enabled ? "off" : "on"
		),
		enabled ? "[Turn Off]" : "[Turn On]"
	);

	client->Message(
		enabled ? Chat::Green : Chat::White,
		fmt::format(
			"  {}: {} {}",
			definition->name,
			enabled ? "On" : "Off",
			toggle_link
		).c_str()
	);
}

void ShowAutoSkillMenuGroup(
	Client *client,
	const std::string &title,
	const std::vector<EQ::skills::SkillType> &skills
)
{
	if (skills.empty()) {
		return;
	}

	client->Message(Chat::White, title.c_str());

	for (const auto skill : skills) {
		ShowAutoSkillMenuEntry(client, skill);
	}
}

void ShowAutoSkillMenu(Client *client)
{
	const auto applicable_skills = client->GetApplicableAutoSkills();
	if (applicable_skills.empty()) {
		client->Message(Chat::White, "No supported autoskills are available to your character at this level.");
		return;
	}

	std::vector<EQ::skills::SkillType> shared_cooldown_skills;
	std::vector<EQ::skills::SkillType> secondary_cooldown_skills;

	for (const auto skill : applicable_skills) {
		if (EQ::skills::autoskill::UsesSecondaryReuseTimer(
			skill,
			client->ClientVersion() >= EQ::versions::ClientVersion::RoF2
		)) {
			secondary_cooldown_skills.push_back(skill);
			continue;
		}

		shared_cooldown_skills.push_back(skill);
	}

	client->Message(Chat::White, "Autoskill settings:");
	client->Message(
		Chat::White,
		"Choose one skill per cooldown group. Turning one on automatically turns the others in that group off."
	);

	ShowAutoSkillMenuGroup(client, "Combat Ability Cooldown Group (choose one):", shared_cooldown_skills);
	ShowAutoSkillMenuGroup(client, "Tiger Claw Cooldown Group (choose one):", secondary_cooldown_skills);
}

} // namespace

void command_autoskill(Client *c, const Seperator *sep)
{
	if (!RuleB(Combat, EnableAutoSkill)) {
		c->Message(Chat::Red, "Autoskill is disabled on this server.");
		return;
	}

	const std::string command_remainder = sep->argplus[1] ? sep->argplus[1] : "";
	auto tokens = TokenizeCommandRemainder(command_remainder);

	if (tokens.empty()) {
		ShowAutoSkillMenu(c);
		return;
	}

	bool has_requested_state = false;
	bool requested_state = false;

	if (TryParseAutoSkillState(tokens.back(), requested_state)) {
		has_requested_state = true;
		tokens.pop_back();
	}

	const auto skill_name = JoinTokens(tokens);
	if (skill_name.empty()) {
		c->Message(Chat::White, "Usage: #autoskill [skill] [on|off]");
		return;
	}

	const auto *definition = EQ::skills::autoskill::FindSkillDefinition(skill_name);
	if (!definition) {
		const auto skill_list = GetApplicableSkillsList(c);
		c->Message(
			Chat::White,
			fmt::format(
				"Unsupported autoskill '{}'. Available autoskills: {}.",
				skill_name,
				skill_list.empty() ? "None" : skill_list
			).c_str()
		);
		return;
	}

	const bool enabled = has_requested_state ? requested_state : !c->IsAutoSkillEnabled(definition->skill);
	if (enabled && !c->IsAutoSkillUsable(definition->skill)) {
		c->Message(
			Chat::White,
			fmt::format(
				"{} is not available to your character at this level.",
				definition->name
			).c_str()
		);
		return;
	}

	const auto previously_enabled_mask = c->GetAutoSkillEnabledMask();
	c->SetAutoSkillEnabled(definition->skill, enabled);

	std::vector<std::string> automatically_disabled_skills;
	if (enabled) {
		for (const auto &other_definition : EQ::skills::autoskill::GetSkillDefinitions()) {
			if (
				other_definition.skill != definition->skill &&
				EQ::skills::autoskill::IsEnabled(previously_enabled_mask, other_definition.skill) &&
				!c->IsAutoSkillEnabled(other_definition.skill)
			) {
				automatically_disabled_skills.emplace_back(other_definition.name);
			}
		}
	}

	c->Message(
		Chat::White,
		fmt::format(
			"Autoskill for {} is now {}.",
			definition->name,
			enabled ? "on" : "off"
		).c_str()
	);

	if (!automatically_disabled_skills.empty()) {
		c->Message(
			Chat::White,
			fmt::format(
				"Automatically disabled {} because {} the same cooldown.",
				Strings::Join(automatically_disabled_skills, ", "),
				automatically_disabled_skills.size() == 1 ? "it shares" : "they share"
			).c_str()
		);
	}

	ShowAutoSkillMenu(c);
}
