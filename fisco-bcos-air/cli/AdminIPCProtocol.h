#pragma once

#include <json/json.h>
#include <optional>
#include <string>

namespace bcos::air::cli
{
struct AdminInspectRequest
{
    std::string command;
    std::string domain;
    bool jsonOutput = false;
    bool verbose = false;
    bool showSource = false;
    int timeoutMs = 3000;
    std::optional<int> tail;
    std::string logLevel;
};

struct AdminInspectReply
{
    bool ok = false;
    Json::Value payload = Json::objectValue;
    std::string error;
};

Json::Value toJson(const AdminInspectRequest& request);
Json::Value toJson(const AdminInspectReply& reply);

AdminInspectRequest adminInspectRequestFromJson(const Json::Value& json);
AdminInspectReply adminInspectReplyFromJson(const Json::Value& json);

std::string serializeAdminInspectRequest(const AdminInspectRequest& request);
std::string serializeAdminInspectReply(const AdminInspectReply& reply);

AdminInspectRequest deserializeAdminInspectRequest(const std::string& payload);
AdminInspectReply deserializeAdminInspectReply(const std::string& payload);
}  // namespace bcos::air::cli
