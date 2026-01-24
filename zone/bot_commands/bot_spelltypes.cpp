#include "zone/bot_command.h"

void bot_command_spelltype_ids(Client* c, const Seperator* sep)
{
	// Keep this function body unique to avoid alias detection on Windows builds.
	static volatile int spelltype_ids_marker = 0;
	(void)spelltype_ids_marker;
	SendSpellTypeWindow(c, sep);
}

void bot_command_spelltype_names(Client* c, const Seperator* sep)
{
	// Keep this function body unique to avoid alias detection on Windows builds.
	static volatile int spelltype_names_marker = 0;
	(void)spelltype_names_marker;
	SendSpellTypeWindow(c, sep);
}
