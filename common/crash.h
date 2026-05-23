#pragma once

#include <string>
#include <vector>

namespace CrashReport {
	std::vector<std::string> GetCrashReportEndpoints(const std::string &additional_endpoints);
	std::string RedactCrashReportEndpoint(const std::string &endpoint);
	std::string GetCrashReportRequestTarget(const std::string &endpoint);
	bool IsCrashReportEndpoint(const std::string &endpoint);
	bool IsCrashReportSuccessStatus(int status);
}

void SendCrashReport(const std::string &crash_report);
void set_exception_handler();
