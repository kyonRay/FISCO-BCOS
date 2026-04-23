#include "InspectTypes.h"

namespace bcos::air::cli
{
Json::Value toJson(const InspectSection& section)
{
    Json::Value value(Json::objectValue);
    value["available"] = section.available;
    if (!section.reason.empty())
    {
        value["reason"] = section.reason;
    }
    value["data"] = section.data;
    return value;
}

Json::Value toJson(const InspectResponse& response)
{
    Json::Value value(Json::objectValue);
    value["source"] = response.source;
    value["command"] = response.command;
    if (!response.domain.empty())
    {
        value["domain"] = response.domain;
    }
    if (!response.timestamp.empty())
    {
        value["timestamp"] = response.timestamp;
    }

    Json::Value summary(Json::objectValue);
    summary["latestBlock"] = response.summary.latestBlock;
    summary["totalTx"] = response.summary.totalTx;
    value["summary"] = std::move(summary);

    value["node"] = toJson(response.node);
    value["chain"] = toJson(response.chain);
    value["network"] = toJson(response.network);
    value["storage"] = toJson(response.storage);
    value["executor"] = toJson(response.executor);
    value["logs"] = toJson(response.logs);

    Json::Value warnings(Json::arrayValue);
    for (const auto& warning : response.warnings)
    {
        warnings.append(warning);
    }
    value["warnings"] = std::move(warnings);
    return value;
}
}  // namespace bcos::air::cli
