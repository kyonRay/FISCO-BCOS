#include "InspectRenderer.h"
#include <json/json.h>
#include <sstream>

namespace
{
std::string writeJson(const Json::Value& value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    return Json::writeString(builder, value);
}

void appendSection(
    std::ostringstream& out, const char* name, const bcos::air::cli::InspectSection& section)
{
    if (!section.available && section.reason.empty() &&
        (section.data.isNull() || section.data.empty()))
    {
        return;
    }

    out << name << ": " << (section.available ? "available" : "unavailable");
    if (!section.reason.empty())
    {
        out << " (" << section.reason << ")";
    }
    if (!section.data.isNull() && !section.data.empty())
    {
        out << " data=" << writeJson(section.data);
    }
    out << "\n";
}
}  // namespace

namespace bcos::air::cli
{
std::string renderHumanReadable(const InspectResponse& response)
{
    std::ostringstream out;
    out << "Source: " << response.source << "\n";
    out << "Command: " << response.command;
    if (!response.domain.empty())
    {
        out << " " << response.domain;
    }
    out << "\n";
    if (!response.timestamp.empty())
    {
        out << "Timestamp: " << response.timestamp << "\n";
    }
    out << "Summary: latestBlock=" << response.summary.latestBlock
        << " totalTx=" << response.summary.totalTx << "\n";

    for (const auto& warning : response.warnings)
    {
        out << "Warning: " << warning << "\n";
    }

    appendSection(out, "Node", response.node);
    appendSection(out, "Chain", response.chain);
    appendSection(out, "Network", response.network);
    appendSection(out, "Storage", response.storage);
    appendSection(out, "Executor", response.executor);
    appendSection(out, "Logs", response.logs);
    return out.str();
}

std::string renderJson(const InspectResponse& response)
{
    return writeJson(toJson(response));
}

std::string renderInspectResponse(const InspectResponse& response, bool jsonOutput)
{
    return jsonOutput ? renderJson(response) : renderHumanReadable(response);
}
}  // namespace bcos::air::cli
