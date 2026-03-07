#pragma once

#include "common/event/event_loop.h"
#include "common/ip_util.h"

#include <functional>
#include <string>

namespace EQ
{
	namespace Net
	{
		static void DNSLookup(const std::string &addr, int port, bool ipv6, std::function<void(const std::string&)> cb) {
			if (IpUtil::IsIPAddress(addr)) {
				cb(addr);
				return;
			}

			struct DNSBaton
			{
				std::function<void(const std::string&)> cb;
				bool ipv6;
				std::string host;
				std::string service;
			};

			addrinfo hints;
			memset(&hints, 0, sizeof(addrinfo));
			hints.ai_family = ipv6 ? PF_INET6 : PF_INET;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;

			auto loop = EQ::EventLoop::Get().Handle();
			uv_getaddrinfo_t *resolver = new uv_getaddrinfo_t();
			memset(resolver, 0, sizeof(uv_getaddrinfo_t));
			DNSBaton *baton = new DNSBaton();
			baton->cb = cb;
			baton->ipv6 = ipv6;
			baton->host = addr;
			baton->service = std::to_string(port);
			resolver->data = baton;

			const auto submit_result = uv_getaddrinfo(loop, resolver, [](uv_getaddrinfo_t* req, int status, addrinfo* res) {
				DNSBaton *baton = (DNSBaton*)req->data;
				if (status < 0) {
					auto cb = baton->cb;
					delete baton;
					delete req;
					cb("");
					return;
				}

				char addr[40] = { 0 };

				if (baton->ipv6) {
					uv_ip6_name((struct sockaddr_in6*)res->ai_addr, addr, 40);
				}
				else {
					uv_ip4_name((struct sockaddr_in*)res->ai_addr, addr, 40);
				}

				auto cb = baton->cb;
				delete baton;
				delete req;
				uv_freeaddrinfo(res);

				cb(addr);
			}, baton->host.c_str(), baton->service.c_str(), &hints);

			if (submit_result < 0) {
				auto callback = baton->cb;
				delete baton;
				delete resolver;
				callback("");
			}
		}
	}
}
