#include "zone/bot_command.h"

void bot_command_spelltype_ids(Client* c, const Seperator* sep)
{
	// Unique marker to prevent MSVC's Identical COMDAT Folding (/OPT:ICF)
	// from merging this function with bot_command_spelltype_names, which
	// would cause the alias detection in bot_command_add() to fail.
	static volatile int spelltype_ids_marker = 0;
	(void)spelltype_ids_marker;
	SendSpellTypeWindow(c, sep);
}

void bot_command_spelltype_names(Client* c, const Seperator* sep)
{
	// Unique marker to prevent MSVC's Identical COMDAT Folding (/OPT:ICF)
	// from merging this function with bot_command_spelltype_ids, which
	// would cause the alias detection in bot_command_add() to fail.
	static volatile int spelltype_names_marker = 0;
	(void)spelltype_names_marker;
	SendSpellTypeWindow(c, sep);
}
