#include "libinitializer/CommandHelper.h"
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <fstream>

using namespace bcos::initializer;

namespace
{
struct TempConfigFiles
{
    TempConfigFiles()
    {
        directory = boost::filesystem::temp_directory_path() /
                    boost::filesystem::unique_path("air-cli-inspect-%%%%-%%%%-%%%%");
        boost::filesystem::create_directories(directory);
        configPath = (directory / "config.ini").string();
        genesisPath = (directory / "config.genesis").string();
        std::ofstream(configPath).close();
        std::ofstream(genesisPath).close();
    }

    ~TempConfigFiles() { boost::filesystem::remove_all(directory); }

    boost::filesystem::path directory;
    std::string configPath;
    std::string genesisPath;
};
}  // namespace

BOOST_AUTO_TEST_CASE(parseInspectOverviewCli)
{
    TempConfigFiles files;
    const char* argv[] = {"fisco-bcos", "-c", files.configPath.c_str(), "-g",
        files.genesisPath.c_str(), "-cli", "inspect"};
    auto params = parseAirNodeCommandLine(static_cast<int>(std::size(argv)), argv, false);

    BOOST_CHECK(params.cliMode);
    BOOST_CHECK_EQUAL(params.cliRequest.command, "inspect");
    BOOST_CHECK_EQUAL(params.cliRequest.domain, "");
    BOOST_CHECK(!params.cliRequest.jsonOutput);
    BOOST_CHECK_EQUAL(params.configFilePath, files.configPath);
}

BOOST_AUTO_TEST_CASE(parseInspectDomainJsonCli)
{
    TempConfigFiles files;
    const char* argv[] = {"fisco-bcos", "-c", files.configPath.c_str(), "-g",
        files.genesisPath.c_str(), "-cli", "inspect", "network", "--json", "--timeout", "2500"};
    auto params = parseAirNodeCommandLine(static_cast<int>(std::size(argv)), argv, false);

    BOOST_CHECK(params.cliMode);
    BOOST_CHECK_EQUAL(params.cliRequest.command, "inspect");
    BOOST_CHECK_EQUAL(params.cliRequest.domain, "network");
    BOOST_CHECK(params.cliRequest.jsonOutput);
    BOOST_CHECK_EQUAL(params.cliRequest.timeoutMs, 2500);
}
