#include "zoneserver.h"

#include "common/discord/discord_manager.h"
#include "common/eqemu_logsys.h"
#include "common/events/player_event_logs.h"
#include "common/repositories/player_event_logs_repository.h"

ZoneServer::ZoneServer(
	std::shared_ptr<EQ::Net::ServertalkServerConnection> in_connection,
	EQ::Net::ConsoleServer *in_console
)
	: m_connection(in_connection)
{

	m_connection->OnMessage(std::bind(&ZoneServer::HandleMessage, this, std::placeholders::_1, std::placeholders::_2));
	m_console = in_console;
}

ZoneServer::~ZoneServer()
{
}

void ZoneServer::HandleMessage(uint16 opcode, const EQ::Net::Packet &p)
{
	ServerPacket tpack(opcode, p);
	auto         pack = &tpack;

	switch (opcode) {
		case ServerOP_PlayerEvent: {
			if (pack->size < sizeof(ServerSendPlayerEvent_Struct)) {
				LogError("ServerOP_PlayerEvent: packet too small");
				break;
			}

			auto *s = reinterpret_cast<ServerSendPlayerEvent_Struct *>(pack->pBuffer);
			const uint64_t expected_size = static_cast<uint64_t>(sizeof(ServerSendPlayerEvent_Struct)) + s->cereal_size;
			if (static_cast<uint64_t>(pack->size) < expected_size) {
				LogError(
					"ServerOP_PlayerEvent: packet size mismatch, expected {}, got {}",
					expected_size,
					pack->size
				);
				break;
			}

			auto n = PlayerEvent::PlayerEventContainer{};
			try {
				EQ::Util::MemoryStreamReader ss(s->cereal_data, s->cereal_size);
				cereal::BinaryInputArchive archive(ss);
				archive(n);
			}
			catch (const std::exception &ex) {
				LogError("ServerOP_PlayerEvent: failed to deserialize payload: {}", ex.what());
				break;
			}

			PlayerEventLogs::Instance()->AddToQueue(n.player_event_log);

			DiscordManager::Instance()->QueuePlayerEventMessage(n);
			break;
		}
		default: {
			LogInfo("Unknown ServerOP Received [{}]", opcode);
			break;
		}
	}
}

void ZoneServer::SendPlayerEventLogSettings()
{
	EQ::Net::DynamicPacket                                                dyn_pack;
	std::vector<PlayerEventLogSettingsRepository::PlayerEventLogSettings> settings(
		PlayerEventLogs::Instance()->GetSettings(),
		PlayerEventLogs::Instance()->GetSettings() + PlayerEvent::EventType::MAX
	);

	dyn_pack.PutSerialize(0, settings);

	auto packet_size = sizeof(ServerSendPlayerEvent_Struct) + dyn_pack.Length();

	auto pack = std::make_unique<ServerPacket>(
		ServerOP_SendPlayerEventSettings,
		static_cast<uint32_t>(packet_size)
	);

	auto* buf        = reinterpret_cast<ServerSendPlayerEvent_Struct*>(pack->pBuffer);
	buf->cereal_size = static_cast<uint32_t>(dyn_pack.Length());
	memcpy(buf->cereal_data, dyn_pack.Data(), dyn_pack.Length());

	SendPacket(pack.release());
}
