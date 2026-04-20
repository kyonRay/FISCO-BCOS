#include "fisco-bcos-air/cli/FallbackInspectors.h"
#include "fisco-bcos-air/cli/InspectConfig.h"
#include "fisco-bcos-air/cli/InspectRenderer.h"
#include "fisco-bcos-air/cli/InspectTypes.h"
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <fstream>
#include <optional>

namespace
{
struct TempInspectConfigFile
{
    TempInspectConfigFile()
    {
        directory = boost::filesystem::temp_directory_path() /
                    boost::filesystem::unique_path("air-inspect-config-%%%%-%%%%-%%%%");
        boost::filesystem::create_directories(directory);
        path = (directory / "config.ini").string();
    }

    ~TempInspectConfigFile() { boost::filesystem::remove_all(directory); }

    void write(const std::string& content)
    {
        std::ofstream out(path);
        out << content;
        out.close();
    }

    boost::filesystem::path directory;
    std::string path;
};
}  // namespace

BOOST_AUTO_TEST_CASE(renderHumanReadableOverview)
{
    bcos::air::cli::InspectResponse response;
    response.source = "rpc+logs";
    response.command = "inspect";
    response.summary.latestBlock = "123";
    response.summary.totalTx = "456";

    auto rendered = bcos::air::cli::renderHumanReadable(response);
    BOOST_CHECK(rendered.find("Source: rpc+logs") != std::string::npos);
    BOOST_CHECK(rendered.find("latestBlock=123") != std::string::npos);
    BOOST_CHECK(rendered.find("totalTx=456") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(selectsRpcAndLogsWhenAdminDisabled)
{
    TempInspectConfigFile file;
    file.write(R"([rpc]
listen_ip=127.0.0.1
listen_port=20200

[web3_rpc]
enable=false
listen_ip=127.0.0.1
listen_port=8545

[log]
log_path=./log

[admin]
enable=false
ipc_path=./run/admin.sock
)");

    auto config = bcos::air::cli::loadInspectConfig(file.path);
    BOOST_CHECK(!config.adminEnabled);
    BOOST_CHECK_EQUAL(config.preferredSource(), "rpc+logs");
    BOOST_CHECK_EQUAL(config.rpcListenIP, "127.0.0.1");
    BOOST_CHECK_EQUAL(config.rpcListenPort, 20200);
    BOOST_CHECK_EQUAL(config.logPath, "./log");
}

BOOST_AUTO_TEST_CASE(logInspectorReadsConfiguredPath)
{
    TempInspectConfigFile file;
    auto logDir = file.directory / "log";
    boost::filesystem::create_directories(logDir);
    std::ofstream((logDir / "node.log").string()) << "[INFO] node started\n"
                                                  << "[DEBUG] trace detail\n"
                                                  << "[INFO] block sealed\n";

    bcos::air::cli::InspectConfig config;
    config.logPath = logDir.string();

    auto section = bcos::air::cli::buildFallbackLogSection(config, 2, "INFO");
    BOOST_CHECK(section.available);
    BOOST_CHECK(section.data.isObject());
    BOOST_CHECK_EQUAL(section.data["path"].asString(), logDir.string());
    BOOST_CHECK(section.data["entries"].isArray());
    BOOST_CHECK_EQUAL(section.data["entries"].size(), 2);
}

BOOST_AUTO_TEST_CASE(executorSectionIsMarkedUnavailableInFallbackMode)
{
    auto section = bcos::air::cli::buildUnavailableExecutorFallbackSection();
    BOOST_CHECK(!section.available);
    BOOST_CHECK(section.reason.find("rpc+logs") != std::string::npos);
    BOOST_CHECK_EQUAL(section.data["source"].asString(), "rpc+logs");
}
