/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2014 EQEMu Development Team (http://eqemulator.net)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY except by those people which sell it, which
	are required to give you total support for your newly bought product;
	without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#pragma once

#include "common/repositories/base/base_tasks_repository.h"
#include "common/shared_tasks.h"
#include "common/tasks.h"
#include "cppunit/cpptest.h"

#include <cereal/archives/binary.hpp>
#include <cereal/types/string.hpp>

#include <sstream>

class TaskClassRestrictionTest: public Test::Suite {
public:
	TaskClassRestrictionTest()
	{
		TEST_ADD(TaskClassRestrictionTest::AllowsAllClassesWhenMaskIsZero);
		TEST_ADD(TaskClassRestrictionTest::MatchesIncludedClasses);
		TEST_ADD(TaskClassRestrictionTest::RejectsExcludedClasses);
		TEST_ADD(TaskClassRestrictionTest::DefaultsAllowedClassesToZero);
		TEST_ADD(TaskClassRestrictionTest::PreservesMemberClassAcrossSerialization);
	}

private:
	void AllowsAllClassesWhenMaskIsZero()
	{
		TEST_ASSERT(TaskClassMaskAllowsPlayerClass(0, Class::Warrior));
		TEST_ASSERT(TaskClassMaskAllowsPlayerClass(0, Class::Wizard));
		TEST_ASSERT(TaskClassMaskAllowsPlayerClass(0, Class::Berserker));
	}

	void MatchesIncludedClasses()
	{
		const auto allowed_classes =
			GetPlayerClassBit(Class::Warrior) |
			GetPlayerClassBit(Class::Cleric) |
			GetPlayerClassBit(Class::Bard);

		TEST_ASSERT(TaskClassMaskAllowsPlayerClass(allowed_classes, Class::Warrior));
		TEST_ASSERT(TaskClassMaskAllowsPlayerClass(allowed_classes, Class::Cleric));
		TEST_ASSERT(TaskClassMaskAllowsPlayerClass(allowed_classes, Class::Bard));
	}

	void RejectsExcludedClasses()
	{
		const auto allowed_classes =
			GetPlayerClassBit(Class::Warrior) |
			GetPlayerClassBit(Class::Cleric);

		TEST_ASSERT(!TaskClassMaskAllowsPlayerClass(allowed_classes, Class::Wizard));
		TEST_ASSERT(!TaskClassMaskAllowsPlayerClass(allowed_classes, Class::Berserker));
	}

	void DefaultsAllowedClassesToZero()
	{
		const auto repository_task = BaseTasksRepository::NewEntity();
		const TaskInformation task = {};

		TEST_ASSERT(repository_task.allowed_classes == 0);
		TEST_ASSERT(task.allowed_classes == 0);
	}

	void PreservesMemberClassAcrossSerialization()
	{
		SharedTaskMember source = {};
		source.character_id = 42;
		source.character_name = "TestBard";
		source.class_id = Class::Bard;
		source.is_leader = true;

		std::stringstream stream;
		{
			cereal::BinaryOutputArchive archive(stream);
			archive(source);
		}

		SharedTaskMember restored = {};
		{
			cereal::BinaryInputArchive archive(stream);
			archive(restored);
		}

		TEST_ASSERT(restored.character_id == source.character_id);
		TEST_ASSERT(restored.character_name == source.character_name);
		TEST_ASSERT(restored.class_id == source.class_id);
		TEST_ASSERT(restored.is_leader == source.is_leader);
	}
};
