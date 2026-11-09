#pragma once

#include "common/net/console_server.h"

class ClientListEntry;
class ServerPacket;

void RegisterConsoleFunctions(std::unique_ptr<EQ::Net::ConsoleServer> &console);
void QueuePlayerSupportTell(EQ::Net::ConsoleServerConnection *connection, ClientListEntry &sender, ServerPacket &packet);
void ProcessPlayerSupportTells();
