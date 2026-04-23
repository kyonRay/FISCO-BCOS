/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file CommandHelper.cpp
 * @author: yujiechen
 * @date 2021-06-10
 */
#include "CommandHelper.h"
#include "Common.h"
#include <include/BuildInfo.h>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <stdexcept>
#include <vector>

void bcos::initializer::printVersion()
{
    std::cout << "FISCO BCOS Version : " << FISCO_BCOS_PROJECT_VERSION << std::endl;
    std::cout << "Build Time         : " << FISCO_BCOS_BUILD_TIME << std::endl;
    std::cout << "Build Type         : " << FISCO_BCOS_BUILD_PLATFORM << "/"
              << FISCO_BCOS_BUILD_TYPE << std::endl;
    std::cout << "Git Branch         : " << FISCO_BCOS_BUILD_BRANCH << std::endl;
    std::cout << "Git Commit         : " << FISCO_BCOS_COMMIT_HASH << std::endl;
}

void bcos::initializer::showNodeVersionMetric()
{
    INITIALIZER_LOG(INFO) << METRIC << LOG_KV("binaryVersion", FISCO_BCOS_PROJECT_VERSION)
                          << LOG_KV("buildTime", FISCO_BCOS_BUILD_TIME)
                          << LOG_KV("buildType", FISCO_BCOS_BUILD_TYPE)
                          << LOG_KV("platform", FISCO_BCOS_BUILD_PLATFORM)
                          << LOG_KV("gitBranch", FISCO_BCOS_BUILD_BRANCH)
                          << LOG_KV("commitHash", FISCO_BCOS_COMMIT_HASH);
}

void bcos::initializer::initCommandLine(int argc, char* argv[])
{
    boost::program_options::options_description main_options("Usage of FISCO BCOS");
    main_options.add_options()("help,h", "print help information")(
        "version,v", "version of FISCO BCOS");
    boost::program_options::variables_map vm;
    try
    {
        boost::program_options::store(
            boost::program_options::parse_command_line(argc, argv, main_options), vm);
    }
    catch (...)
    {
        printVersion();
    }
    /// help information
    if (vm.count("help") || vm.count("h"))
    {
        std::cout << main_options << std::endl;
        exit(0);
    }
    /// version information
    if (vm.count("version") || vm.count("v"))
    {
        printVersion();
        exit(0);
    }
}

namespace
{
std::vector<std::string> normalizeAirNodeArgs(int argc, const char* argv[])
{
    std::vector<std::string> normalizedArgs;
    normalizedArgs.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0U);
    for (int i = 1; i < argc; ++i)
    {
        auto arg = std::string(argv[i]);
        if (arg == "-cli")
        {
            normalizedArgs.emplace_back("--cli");
            continue;
        }
        normalizedArgs.emplace_back(std::move(arg));
    }
    return normalizedArgs;
}

boost::program_options::options_description buildAirNodeOptions(bool autoSendTx)
{
    boost::program_options::options_description main_options("Usage of FISCO-BCOS");
    main_options.add_options()("help,h", "print help information")(
        "version,v", "version of FISCO-BCOS")("config,c",
        boost::program_options::value<std::string>()->default_value("./config.ini"),
        "config file path, eg. config.ini")("genesis,g",
        boost::program_options::value<std::string>()->default_value("./config.genesis"),
        "genesis config file path, eg. genesis.ini")("prune,p", "prune the node data")("snapshot,s",
        boost::program_options::value<bool>(),
        "generate snapshot with or without txs and receipts, if true generate snapshot with txs "
        "and receipts")(
        "output,o", boost::program_options::value<std::string>(), "snapshot output directory")(
        "import,i", boost::program_options::value<std::string>(), "import snapshot from directory")(
        "cli", "run inspect CLI mode")("json", "render inspect output as JSON")("verbose",
        "show more inspect details")("source", "show selected inspect data source")("timeout",
        boost::program_options::value<int>()->default_value(3000),
        "inspect timeout in milliseconds")(
        "tail", boost::program_options::value<int>(), "tail line count for inspect logs")(
        "level", boost::program_options::value<std::string>(), "log level filter");

    if (autoSendTx)
    {
        main_options.add_options()(
            "txSpeed,t", boost::program_options::value<float>(), "transaction generate speed");
    }

    return main_options;
}

void fillCLIRequest(const boost::program_options::variables_map& vm,
    const std::vector<std::string>& positional, bcos::initializer::CLIRequest& request)
{
    if (positional.empty())
    {
        throw std::runtime_error("cli command is required");
    }

    request.command = positional.at(0);
    if (request.command != "inspect")
    {
        throw std::runtime_error("unsupported cli command: " + request.command);
    }

    if (positional.size() > 1)
    {
        request.domain = positional.at(1);
    }
    if (positional.size() > 2)
    {
        throw std::runtime_error("too many cli positional arguments");
    }

    request.jsonOutput = vm.count("json");
    request.verbose = vm.count("verbose");
    request.showSource = vm.count("source");
    request.timeoutMs = vm["timeout"].as<int>();
    if (vm.count("tail"))
    {
        request.tail = vm["tail"].as<int>();
    }
    if (vm.count("level"))
    {
        request.logLevel = vm["level"].as<std::string>();
    }
}
}  // namespace

bcos::initializer::Params bcos::initializer::parseAirNodeCommandLine(
    int argc, const char* argv[], bool _autoSendTx)
{
    auto normalizedArgs = normalizeAirNodeArgs(argc, argv);
    auto main_options = buildAirNodeOptions(_autoSendTx);
    boost::program_options::variables_map vm;
    auto parsed = boost::program_options::command_line_parser(normalizedArgs)
                      .options(main_options)
                      .allow_unregistered()
                      .run();
    boost::program_options::store(parsed, vm);
    boost::program_options::notify(vm);

    auto configPath = vm["config"].as<std::string>();
    auto genesisFilePath = vm["genesis"].as<std::string>();
    if (!boost::filesystem::exists(configPath))
    {
        throw std::runtime_error("config '" + configPath + "' not found!");
    }
    if (!boost::filesystem::exists(genesisFilePath))
    {
        throw std::runtime_error("genesis config '" + genesisFilePath + "' not found!");
    }
    float txSpeed = 10.0f;
    if (_autoSendTx)
    {
        if (vm.count("txSpeed"))
        {
            txSpeed = vm["txSpeed"].as<float>();
        }
    }
    auto op = Params::operation::None;
    if (vm.count("prune"))
    {
        op = (op | Params::operation::Prune);
    }
    std::string snapshotPath("./");
    if (vm.count("snapshot"))
    {
        auto snapshot = vm["snapshot"].as<bool>();
        if (snapshot)
        {
            op = (op | Params::operation::Snapshot);
        }
        else
        {
            op = (op | Params::operation::SnapshotWithoutTxAndReceipt);
        }
        if (vm.count("output"))
        {
            snapshotPath = vm["output"].as<std::string>();
        }
        else
        {
            std::cout << "snapshot output directory is not set, use current directory as default";
        }
    }
    if (vm.count("import"))
    {
        if (op != Params::operation::None)
        {
            std::cout << "import snapshot can not be used with other operations";
            exit(0);
        }
        op = Params::operation::ImportSnapshot;
        snapshotPath = vm["import"].as<std::string>();
    }

    Params params;
    params.configFilePath = configPath;
    params.genesisFilePath = genesisFilePath;
    params.snapshotPath = snapshotPath;
    params.txSpeed = txSpeed;
    params.op = op;
    params.cliMode = vm.count("cli");
    if (params.cliMode)
    {
        auto positional = boost::program_options::collect_unrecognized(
            parsed.options, boost::program_options::include_positional);
        fillCLIRequest(vm, positional, params.cliRequest);
    }
    return params;
}

bcos::initializer::Params bcos::initializer::initAirNodeCommandLine(
    int argc, const char* argv[], bool _autoSendTx)
{
    auto normalizedArgs = normalizeAirNodeArgs(argc, argv);
    auto main_options = buildAirNodeOptions(_autoSendTx);
    try
    {
        boost::program_options::variables_map vm;
        auto parsed = boost::program_options::command_line_parser(normalizedArgs)
                          .options(main_options)
                          .allow_unregistered()
                          .run();
        boost::program_options::store(parsed, vm);
        if (vm.count("help") || vm.count("h"))
        {
            std::cout << main_options << std::endl;
            exit(0);
        }
        if (vm.count("version") || vm.count("v"))
        {
            bcos::initializer::printVersion();
            exit(0);
        }
        auto params = parseAirNodeCommandLine(argc, argv, _autoSendTx);
        if (params.cliMode && params.op != Params::operation::None)
        {
            std::cout << "cli mode can not be used with other operations" << std::endl;
            std::cout << main_options << std::endl;
            exit(0);
        }
        return params;
    }
    catch (std::exception const& e)
    {
        std::cout << "invalid parameters: " << e.what() << std::endl;
        std::cout << main_options << std::endl;
        exit(0);
    }
}
