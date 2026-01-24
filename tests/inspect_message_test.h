/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2013 EQEMu Development Team (http://eqemulator.net)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY except by those people who sell it, which
	are required to give you total support for your newly bought product;
	without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __EQEMU_TESTS_INSPECT_MESSAGE_TEST_H
#define __EQEMU_TESTS_INSPECT_MESSAGE_TEST_H

#include <cstring>
#include <string>
#include "cppunit/cpptest.h"
#include "../common/eq_packet_structs.h"
#include "../common/strings.h"

class InspectMessageTest : public Test::Suite {
	typedef void(InspectMessageTest::*TestFunction)(void);
public:
	InspectMessageTest() {
		TEST_ADD(InspectMessageTest::CopyEnsuresNullTermination);
	}

	~InspectMessageTest() {
	}

private:
	void CopyEnsuresNullTermination() {
		InspectMessage_Struct incoming{};
		InspectMessage_Struct stored{};

		std::string long_text(sizeof(incoming.text) + 44, 'A');
		strn0cpy(incoming.text, long_text.c_str(), sizeof(incoming.text));
		strn0cpy(stored.text, incoming.text, sizeof(stored.text));

		TEST_ASSERT_EQUALS(stored.text[sizeof(stored.text) - 1], '\0');
		TEST_ASSERT_EQUALS(std::strlen(stored.text), sizeof(stored.text) - 1);
	}
};

#endif
