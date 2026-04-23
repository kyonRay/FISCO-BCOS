#include "AdminIPCProtocol.h"

namespace
{
std::string writeJson(const Json::Value& value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

Json::Value readJson(const std::string& payload)
{
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(payload.data(), payload.data() + payload.size(), &root, &errors))
    {
        throw std::runtime_error("invalid admin ipc payload: " + errors);
    }
    return root;
}
}  // namespace

namespace bcos::air::cli
{
Json::Value toJson(const AdminInspectRequest& request)
{
    Json::Value value(Json::objectValue);
    value["command"] = request.command;
    value["domain"] = request.domain;
    value["jsonOutput"] = request.jsonOutput;
    value["verbose"] = request.verbose;
    value["showSource"] = request.showSource;
    value["timeoutMs"] = request.timeoutMs;
    if (request.tail)
    {
        value["tail"] = *request.tail;
    }
    if (!request.logLevel.empty())
    {
        value["logLevel"] = request.logLevel;
    }
    return value;
}

Json::Value toJson(const AdminInspectReply& reply)
{
    Json::Value value(Json::objectValue);
    value["ok"] = reply.ok;
    value["payload"] = reply.payload;
    if (!reply.error.empty())
    {
        value["error"] = reply.error;
    }
    return value;
}

AdminInspectRequest adminInspectRequestFromJson(const Json::Value& json)
{
    AdminInspectRequest request;
    request.command = json["command"].asString();
    request.domain = json["domain"].asString();
    request.jsonOutput = json.get("jsonOutput", false).asBool();
    request.verbose = json.get("verbose", false).asBool();
    request.showSource = json.get("showSource", false).asBool();
    request.timeoutMs = json.get("timeoutMs", 3000).asInt();
    if (json.isMember("tail"))
    {
        request.tail = json["tail"].asInt();
    }
    request.logLevel = json.get("logLevel", "").asString();
    return request;
}

AdminInspectReply adminInspectReplyFromJson(const Json::Value& json)
{
    AdminInspectReply reply;
    reply.ok = json.get("ok", false).asBool();
    reply.payload = json["payload"];
    reply.error = json.get("error", "").asString();
    return reply;
}

std::string serializeAdminInspectRequest(const AdminInspectRequest& request)
{
    return writeJson(toJson(request));
}

std::string serializeAdminInspectReply(const AdminInspectReply& reply)
{
    return writeJson(toJson(reply));
}

AdminInspectRequest deserializeAdminInspectRequest(const std::string& payload)
{
    return adminInspectRequestFromJson(readJson(payload));
}

AdminInspectReply deserializeAdminInspectReply(const std::string& payload)
{
    return adminInspectReplyFromJson(readJson(payload));
}
}  // namespace bcos::air::cli
