#pragma once

#include <json/json.h>
#include <string>
#include <vector>

namespace bcos::air::cli
{
struct InspectSection
{
    bool available = false;
    std::string reason;
    Json::Value data = Json::objectValue;
};

struct InspectSummary
{
    std::string latestBlock;
    std::string totalTx;
};

struct InspectResponse
{
    std::string source;
    std::string command;
    std::string domain;
    std::string timestamp;
    InspectSection node;
    InspectSection chain;
    InspectSection network;
    InspectSection storage;
    InspectSection executor;
    InspectSection logs;
    InspectSummary summary;
    std::vector<std::string> warnings;
};

Json::Value toJson(const InspectSection& section);
Json::Value toJson(const InspectResponse& response);
}  // namespace bcos::air::cli
