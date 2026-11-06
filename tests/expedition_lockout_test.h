#pragma once

#include "common/dynamic_zone_base.h"
#include "cppunit/cpptest.h"
#include "zone/expedition_db.h"

#include <chrono>
#include <stdexcept>

class ExpeditionLockoutTest : public Test::Suite {
public:
	ExpeditionLockoutTest()
	{
		TEST_ADD(ExpeditionLockoutTest::InheritedEventLockoutSurvivesReplayExpiry);
		TEST_ADD(ExpeditionLockoutTest::CurrentExpeditionLockoutBlocksEvent);
		TEST_ADD(ExpeditionLockoutTest::ExpiredEventLockoutsDoNotBlockEvent);
		TEST_ADD(ExpeditionLockoutTest::OtherEventsAndReplayDoNotBlockEvent);
		TEST_ADD(ExpeditionLockoutTest::AvailabilityCheckPreservesInheritedLockout);
	}

private:
	class TestExpedition final : public DynamicZoneBase {
	public:
		TestExpedition() : DynamicZoneBase(DynamicZoneType::Expedition)
		{
			SetName("Temple of Veeshan");
			SetUUID("new-expedition");
		}

		void LoadLockout(const DzLockout& lockout)
		{
			HandleLockoutUpdate(lockout, false, false);
		}

		void SetSecondsRemaining(uint32_t) override
		{
			throw std::logic_error("Lockout availability must not change instance duration");
		}

	protected:
		Database& GetDatabase() override
		{
			throw std::logic_error("Lockout availability must not access the database");
		}

		bool SendServerPacket(ServerPacket*) override
		{
			throw std::logic_error("Lockout availability must not send packets");
		}
	};

	static uint64_t Now()
	{
		return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	}

	void InheritedEventLockoutSurvivesReplayExpiry()
	{
		TestExpedition expedition;
		const auto now = Now();
		expedition.LoadLockout({"old-expedition", expedition.GetName(), DzLockout::ReplayTimer, now - 60, 36000});
		expedition.LoadLockout({"old-expedition", expedition.GetName(), "Lord Vyemm", now + 86400, 561600});

		TEST_ASSERT(!ExpeditionDB::HasActiveExpeditionLockout(expedition, DzLockout::ReplayTimer));
		TEST_ASSERT(ExpeditionDB::HasActiveExpeditionLockout(expedition, "Lord Vyemm"));
	}

	void CurrentExpeditionLockoutBlocksEvent()
	{
		TestExpedition expedition;
		expedition.LoadLockout({expedition.GetUUID(), expedition.GetName(), "Lord Vyemm", Now() + 3600, 3600});

		TEST_ASSERT(ExpeditionDB::HasActiveExpeditionLockout(expedition, "Lord Vyemm"));
	}

	void ExpiredEventLockoutsDoNotBlockEvent()
	{
		TestExpedition expedition;
		const auto now = Now();
		expedition.LoadLockout({"old-expedition", expedition.GetName(), "Lord Vyemm", now - 60, 3600});
		expedition.LoadLockout({expedition.GetUUID(), expedition.GetName(), "Lord Kreizenn", now, 3600});

		TEST_ASSERT(!ExpeditionDB::HasActiveExpeditionLockout(expedition, "Lord Vyemm"));
		TEST_ASSERT(!ExpeditionDB::HasActiveExpeditionLockout(expedition, "Lord Kreizenn"));
	}

	void OtherEventsAndReplayDoNotBlockEvent()
	{
		TestExpedition expedition;
		TEST_ASSERT(!ExpeditionDB::HasActiveExpeditionLockout(expedition, "Lord Vyemm"));

		const auto now = Now();
		expedition.LoadLockout({expedition.GetUUID(), expedition.GetName(), DzLockout::ReplayTimer, now + 3600, 3600});
		expedition.LoadLockout({"old-expedition", expedition.GetName(), "Lord Kreizenn", now + 86400, 561600});

		TEST_ASSERT(!ExpeditionDB::HasActiveExpeditionLockout(expedition, "Lord Vyemm"));
		TEST_ASSERT(ExpeditionDB::HasActiveExpeditionLockout(expedition, DzLockout::ReplayTimer));
	}

	void AvailabilityCheckPreservesInheritedLockout()
	{
		TestExpedition expedition;
		const auto expires = Now() + 86400;
		expedition.LoadLockout({"old-expedition", expedition.GetName(), "Lord Vyemm", expires, 561600});

		TEST_ASSERT(ExpeditionDB::HasActiveExpeditionLockout(expedition, "Lord Vyemm"));
		TEST_ASSERT(expedition.GetLockouts().size() == 1);
		const auto& lockout = expedition.GetLockouts().front();
		TEST_ASSERT(lockout.UUID() == "old-expedition");
		TEST_ASSERT(lockout.DzName() == expedition.GetName());
		TEST_ASSERT(lockout.Event() == "Lord Vyemm");
		TEST_ASSERT(lockout.GetExpireTime() == expires);
		TEST_ASSERT(lockout.GetDuration() == 561600);
	}
};
