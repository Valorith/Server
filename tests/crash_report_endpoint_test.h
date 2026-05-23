/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2026 EQEmu Development Team (http://eqemulator.net)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.

	This program is distributed in the hope that it will be useful,
	without even the implied warranty of MERCHANTABILITY or FITNESS FOR
	A PARTICULAR PURPOSE. See the GNU General Public License for more details.
*/

#pragma once

#include "common/crash.h"
#include "common/http/httplib.h"
#include "common/rulesys.h"
#include "cppunit/cpptest.h"

#include <atomic>
#include <thread>

class CrashReportEndpointTest : public Test::Suite {
	typedef void(CrashReportEndpointTest::*TestFunction)(void);
public:
	CrashReportEndpointTest() {
		TEST_ADD(CrashReportEndpointTest::DefaultEndpointTest);
		TEST_ADD(CrashReportEndpointTest::AdditionalEndpointParsingTest);
		TEST_ADD(CrashReportEndpointTest::RuleValueEndpointTest);
		TEST_ADD(CrashReportEndpointTest::SuccessStatusTest);
		TEST_ADD(CrashReportEndpointTest::RedactedEndpointTest);
		TEST_ADD(CrashReportEndpointTest::RequestTargetTest);
		TEST_ADD(CrashReportEndpointTest::LocalHttpNoContentTest);
	}

	~CrashReportEndpointTest() {
	}

private:
	void DefaultEndpointTest() {
		const auto endpoints = CrashReport::GetCrashReportEndpoints("");
		TEST_ASSERT(endpoints.size() == 1);
		TEST_ASSERT(endpoints[0] == "https://spire.eqemu.dev/api/v1/analytics/server-crash-report");
	}

	void AdditionalEndpointParsingTest() {
		const auto endpoints = CrashReport::GetCrashReportEndpoints(
			" https://one.example/path ; ; ftp://bad.example/token ; not-a-url ; http://two.example:8080/path "
		);

		TEST_ASSERT(endpoints.size() == 3);
		TEST_ASSERT(endpoints[0] == "https://spire.eqemu.dev/api/v1/analytics/server-crash-report");
		TEST_ASSERT(endpoints[1] == "https://one.example/path");
		TEST_ASSERT(endpoints[2] == "http://two.example:8080/path");
	}

	void RuleValueEndpointTest() {
		const auto was_set = RuleManager::Instance()->SetRule(
			"Analytics:CrashReportingAdditionalEndpoints",
			"https://example.com/api/webhook-inbox/webhookId/token"
		);
		TEST_ASSERT(was_set);

		const auto endpoints = CrashReport::GetCrashReportEndpoints(RuleS(Analytics, CrashReportingAdditionalEndpoints));
		RuleManager::Instance()->ResetRules();

		TEST_ASSERT(endpoints.size() == 2);
		TEST_ASSERT(endpoints[0] == "https://spire.eqemu.dev/api/v1/analytics/server-crash-report");
		TEST_ASSERT(endpoints[1] == "https://example.com/api/webhook-inbox/webhookId/token");
	}

	void SuccessStatusTest() {
		TEST_ASSERT(!CrashReport::IsCrashReportSuccessStatus(199));
		TEST_ASSERT(CrashReport::IsCrashReportSuccessStatus(200));
		TEST_ASSERT(CrashReport::IsCrashReportSuccessStatus(204));
		TEST_ASSERT(CrashReport::IsCrashReportSuccessStatus(299));
		TEST_ASSERT(!CrashReport::IsCrashReportSuccessStatus(300));
	}

	void RedactedEndpointTest() {
		const auto redacted = CrashReport::RedactCrashReportEndpoint(
			"https://example.com/api/webhook-inbox/webhookId/token?secret=1"
		);

		TEST_ASSERT(redacted == "https://example.com/api/webhook-inbox/...");
		TEST_ASSERT(redacted.find("webhookId") == std::string::npos);
		TEST_ASSERT(redacted.find("token") == std::string::npos);
		TEST_ASSERT(redacted.find("secret") == std::string::npos);

		const auto short_path_redacted = CrashReport::RedactCrashReportEndpoint("https://example.com/token");
		TEST_ASSERT(short_path_redacted == "https://example.com/...");
		TEST_ASSERT(short_path_redacted.find("token") == std::string::npos);
	}

	void RequestTargetTest() {
		const auto request_target = CrashReport::GetCrashReportRequestTarget(
			"https://example.com/api/webhook-inbox/webhookId/token?source=server"
		);

		TEST_ASSERT(request_target == "/api/webhook-inbox/webhookId/token?source=server");
	}

	void LocalHttpNoContentTest() {
		httplib::Server server;
		std::atomic_bool received(false);
		std::string received_body;

		server.Post("/api/webhook-inbox/webhookId/token", [&](const httplib::Request &req, httplib::Response &res) {
			received = true;
			received_body = req.body;
			res.status = 204;
		});

		const auto port = server.bind_to_any_port("127.0.0.1");
		TEST_ASSERT(port > 0);

		std::thread server_thread([&server]() {
			server.listen_after_bind();
		});

		httplib::Client client("http://127.0.0.1:" + std::to_string(port));
		client.set_connection_timeout(1, 0);
		client.set_read_timeout(1, 0);
		client.set_write_timeout(1, 0);

		const auto endpoint = "http://127.0.0.1:" + std::to_string(port) + "/api/webhook-inbox/webhookId/token";
		const auto res = client.Post(
			CrashReport::GetCrashReportRequestTarget(endpoint),
			"{\"crash_report\":\"test\"}",
			"application/json"
		);

		server.stop();
		server_thread.join();

		TEST_ASSERT(res);
		TEST_ASSERT(CrashReport::IsCrashReportSuccessStatus(res->status));
		TEST_ASSERT(res->status == 204);
		TEST_ASSERT(received);
		TEST_ASSERT(received_body == "{\"crash_report\":\"test\"}");
	}
};
