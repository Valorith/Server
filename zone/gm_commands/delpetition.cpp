#include "zone/client.h"

void command_delpetition(Client *c, const Seperator *sep)
{
	if (sep->arg[1][0] == 0 || strcasecmp(sep->arg[1], "*") == 0) {
		c->Message(Chat::White, "Usage: #delpetition (petition number) Type #listpetition for a list");
		return;
	}

	c->Message(Chat::Red, "Attempting to delete petition number: %i", Strings::ToInt(sep->argplus[1]));
	std::string query   = StringFormat("DELETE FROM petitions WHERE petid = %i", Strings::ToInt(sep->argplus[1]));
	auto        results = database.QueryDatabase(query);
	if (!results.Success()) {
		return;
	}

	LogInfo("Delete petition request from [{}], petition number:", c->GetName(), Strings::ToInt(sep->argplus[1]));

}

#include "zone/petitions.h"
#include "zone/worldserver.h"

extern WorldServer worldserver;

// Players can file a petition without the /guidehelp window. While one is
// open, the text goes to Game Master as a tell so Player Support can add it
// to the case or tell the player why it cannot.
void command_petition(Client *c, const Seperator *sep)
{
	std::string text = sep->argplus[1] ? sep->argplus[1] : "";
	Strings::Trim(text);
	const bool has_petition = PetitionList::Instance()->FindPetitionByAccountName(c->AccountName());

	// Petitions are one per account, but Player Support only accepts
	// follow-ups from the character that filed it.
	std::string petitioner;
	if (has_petition) {
		auto results = database.QueryDatabase(fmt::format(
			"SELECT `charname` FROM `petitions` WHERE `accountname` = '{}' ORDER BY `dib` DESC LIMIT 1",
			Strings::Escape(c->AccountName())
		));
		if (results.Success() && results.RowCount() > 0) {
			auto row = results.begin();
			petitioner = row[0] ? row[0] : "";
		}
	}
	const bool own_petition = has_petition && strcasecmp(petitioner.c_str(), c->GetName()) == 0;
	if (has_petition && !own_petition) {
		c->Message(
			Chat::White,
			petitioner.empty()
				? "Your account already has a petition in the queue. Log in as the character who filed it to add to it or withdraw it."
				: fmt::format("Your account's petition was filed by {}. Log in as {} to add to it or withdraw it.", petitioner, petitioner).c_str()
		);
		return;
	}
	if (text.empty()) {
		c->Message(
			Chat::White,
			own_petition ? "Your petition is in the queue. Use #petition [message] to add to it, or /deletepetition to withdraw it."
			             : "Usage: #petition [message] - Describe your problem and a Game Master will respond."
		);
		return;
	}
	if (!own_petition) {
		if (c->FilePetition(text)) {
			c->Message(Chat::Yellow, "Your petition has been submitted. A Game Master will respond as soon as possible.");
		}
		return;
	}
	// Common Tongue at full skill, matching an ordinary tell.
	if (!worldserver.SendChannelMessageRaw(c, "GameMaster", ChatChannel_Tell, 0, 0, 100, text.c_str())) {
		c->Message(Chat::Red, "Error: World server disconnected");
	}
}
