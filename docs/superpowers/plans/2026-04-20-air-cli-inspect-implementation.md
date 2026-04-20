# Air CLI Inspect Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `./fisco-bcos -cli inspect` and `inspect <domain>` to the air node binary with human-readable output by default, `--json` output for automation, `admin-ipc` as an opt-in in-process source, and `rpc+logs` as the automatic fallback source.

**Architecture:** Extend `libinitializer/CommandHelper` to parse CLI mode into a typed inspect request, branch `fisco-bcos-air/main.cpp` into node mode vs CLI mode, add a small inspect application layer under `fisco-bcos-air/cli/`, and keep data collection separate from rendering. Implement fallback inspection first, then add optional admin IPC server/client wiring so the same CLI request can choose `admin-ipc` when available.

**Tech Stack:** C++17, Boost.Program_options, Boost.PropertyTree, JsonCpp, existing FISCO-BCOS local RPC/node runtime objects, Boost.Test, CMake.

---

### Task 1: Add CLI mode parsing and tests

**Files:**
- Modify: `libinitializer/CommandHelper.h`
- Modify: `libinitializer/CommandHelper.cpp`
- Modify: `fisco-bcos-air/main.cpp`
- Create: `tests/unittest/CommandHelperTest.cpp`

- [ ] **Step 1: Write the failing parsing tests**

```cpp
#include "libinitializer/CommandHelper.h"
#include <boost/test/unit_test.hpp>

using namespace bcos::initializer;

BOOST_AUTO_TEST_CASE(parseInspectOverviewCli)
{
    const char* argv[] = {"fisco-bcos", "-c", "config.ini", "-g", "config.genesis", "-cli",
        "inspect"};
    auto params = initAirNodeCommandLine(static_cast<int>(std::size(argv)), argv, false);

    BOOST_CHECK(params.cliMode);
    BOOST_CHECK_EQUAL(params.cliRequest.command, "inspect");
    BOOST_CHECK_EQUAL(params.cliRequest.domain, "");
    BOOST_CHECK(!params.cliRequest.jsonOutput);
}

BOOST_AUTO_TEST_CASE(parseInspectDomainJsonCli)
{
    const char* argv[] = {"fisco-bcos", "-c", "config.ini", "-cli", "inspect", "network",
        "--json", "--timeout", "2500"};
    auto params = initAirNodeCommandLine(static_cast<int>(std::size(argv)), argv, false);

    BOOST_CHECK(params.cliMode);
    BOOST_CHECK_EQUAL(params.cliRequest.command, "inspect");
    BOOST_CHECK_EQUAL(params.cliRequest.domain, "network");
    BOOST_CHECK(params.cliRequest.jsonOutput);
    BOOST_CHECK_EQUAL(params.cliRequest.timeoutMs, 2500);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target fisco-bcos-test -j4`

Run: `ctest --test-dir build -R fisco-bcos-test --output-on-failure`

Expected: build or test fails because `Params` does not contain CLI request fields yet.

- [ ] **Step 3: Write the minimal parsing types and branch**

```cpp
struct CLIRequest
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

struct Params
{
    std::string configFilePath;
    std::string genesisFilePath;
    std::string snapshotPath;
    float txSpeed = 10;
    bool cliMode = false;
    CLIRequest cliRequest;
    enum class operation : int { ... } op;
};
```

```cpp
main_options.add_options()("cli", "run inspect CLI mode")(
    "json", "render inspect output as json")("verbose", "show more detail")(
    "source", "always print selected source")(
    "timeout", boost::program_options::value<int>()->default_value(3000),
    "inspect timeout in milliseconds")(
    "tail", boost::program_options::value<int>(), "tail N lines for inspect logs")(
    "level", boost::program_options::value<std::string>(), "log level filter");

auto parsed = boost::program_options::command_line_parser(argc, argv)
                  .options(main_options)
                  .allow_unregistered()
                  .run();
```

```cpp
if (params.cliMode)
{
    return runAirInspectCLI(params);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target fisco-bcos-test -j4`

Run: `ctest --test-dir build -R fisco-bcos-test --output-on-failure`

Expected: the new `CommandHelperTest` passes and existing command-line behavior still builds.

- [ ] **Step 5: Commit**

```bash
git add libinitializer/CommandHelper.h libinitializer/CommandHelper.cpp fisco-bcos-air/main.cpp tests/unittest/CommandHelperTest.cpp
git commit -m "feat: parse air inspect cli mode"
```

### Task 2: Add inspect DTOs, renderers, and fallback source selection

**Files:**
- Create: `fisco-bcos-air/cli/InspectTypes.h`
- Create: `fisco-bcos-air/cli/InspectTypes.cpp`
- Create: `fisco-bcos-air/cli/InspectRenderer.h`
- Create: `fisco-bcos-air/cli/InspectRenderer.cpp`
- Create: `fisco-bcos-air/cli/InspectConfig.h`
- Create: `fisco-bcos-air/cli/InspectConfig.cpp`
- Create: `fisco-bcos-air/cli/InspectApplication.h`
- Create: `fisco-bcos-air/cli/InspectApplication.cpp`
- Modify: `fisco-bcos-air/CMakeLists.txt`
- Test: `tests/unittest/CommandHelperTest.cpp`

- [ ] **Step 1: Write the failing source-selection and rendering tests**

```cpp
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
}

BOOST_AUTO_TEST_CASE(selectsRpcAndLogsWhenAdminDisabled)
{
    auto config = bcos::air::cli::loadInspectConfig("tools/BcosAirBuilder/nodes/127.0.0.1/node0/config.ini");
    BOOST_CHECK(!config.adminEnabled);
    BOOST_CHECK_EQUAL(config.preferredSource(), "rpc+logs");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target fisco-bcos-test -j4`

Expected: missing inspect types/config/renderer symbols.

- [ ] **Step 3: Write minimal inspect DTOs and fallback application**

```cpp
namespace bcos::air::cli
{
struct InspectSection
{
    bool available = false;
    std::string reason;
    Json::Value data = Json::objectValue;
};

struct InspectResponse
{
    std::string source;
    std::string command;
    std::string timestamp;
    InspectSection node;
    InspectSection chain;
    InspectSection network;
    InspectSection storage;
    InspectSection executor;
    InspectSection logs;
    struct Summary
    {
        std::string latestBlock;
        std::string totalTx;
    } summary;
    std::vector<std::string> warnings;
};
}
```

```cpp
class InspectConfig
{
public:
    static InspectConfig load(const std::string& configPath);
    std::string preferredSource() const { return adminEnabled ? "admin-ipc" : "rpc+logs"; }

    bool adminEnabled = false;
    std::string adminIPCPath;
    std::string rpcListenIP;
    int rpcListenPort = 20200;
    bool web3Enabled = false;
    std::string web3ListenIP;
    int web3ListenPort = 8545;
    std::string logPath;
};
```

```cpp
int runAirInspectCLI(const bcos::initializer::Params& params)
{
    auto config = bcos::air::cli::InspectConfig::load(params.configFilePath);
    auto response = bcos::air::cli::InspectApplication(config).run(params.cliRequest);
    std::cout << (params.cliRequest.jsonOutput ? renderJson(response) : renderHumanReadable(response))
              << std::endl;
    return 0;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target fisco-bcos-test -j4`

Expected: inspect DTO and renderer tests pass, `fisco-bcos` still links.

- [ ] **Step 5: Commit**

```bash
git add fisco-bcos-air/CMakeLists.txt fisco-bcos-air/cli/InspectTypes.h fisco-bcos-air/cli/InspectTypes.cpp fisco-bcos-air/cli/InspectRenderer.h fisco-bcos-air/cli/InspectRenderer.cpp fisco-bcos-air/cli/InspectConfig.h fisco-bcos-air/cli/InspectConfig.cpp fisco-bcos-air/cli/InspectApplication.h fisco-bcos-air/cli/InspectApplication.cpp tests/unittest/CommandHelperTest.cpp
git commit -m "feat: add inspect response model and fallback runner"
```

### Task 3: Implement `rpc+logs` inspectors for overview, domains, and degraded fields

**Files:**
- Create: `fisco-bcos-air/cli/FallbackInspectors.h`
- Create: `fisco-bcos-air/cli/FallbackInspectors.cpp`
- Modify: `fisco-bcos-air/cli/InspectApplication.cpp`
- Test: `tests/unittest/CommandHelperTest.cpp`

- [ ] **Step 1: Write the failing fallback inspection tests**

```cpp
BOOST_AUTO_TEST_CASE(logInspectorReadsConfiguredPath)
{
    bcos::air::cli::InspectConfig config;
    config.logPath = "tools/BcosAirBuilder/nodes/127.0.0.1/node0/log";

    auto response = bcos::air::cli::buildFallbackLogSection(config, std::nullopt, "");
    BOOST_CHECK(response.available);
    BOOST_CHECK(response.data.isObject());
}

BOOST_AUTO_TEST_CASE(executorSectionIsMarkedUnavailableInFallbackMode)
{
    auto section = bcos::air::cli::buildUnavailableExecutorFallbackSection();
    BOOST_CHECK(!section.available);
    BOOST_CHECK(section.reason.find("rpc+logs") != std::string::npos);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target fisco-bcos-test -j4`

Expected: missing fallback inspector helpers.

- [ ] **Step 3: Write the minimal fallback inspectors**

```cpp
InspectSection buildUnavailableExecutorFallbackSection()
{
    InspectSection section;
    section.available = false;
    section.reason = "internal executor state unavailable in source=rpc+logs";
    section.data["source"] = "rpc+logs";
    return section;
}
```

```cpp
InspectSection buildFallbackLogSection(
    const InspectConfig& config, std::optional<int> tail, const std::string& levelFilter)
{
    InspectSection section;
    section.available = true;
    section.data["path"] = config.logPath;
    section.data["tail"] = tail ? *tail : 0;
    section.data["levelFilter"] = levelFilter;
    return section;
}
```

```cpp
InspectResponse InspectApplication::runFallback(const CLIRequest& request)
{
    InspectResponse response;
    response.source = "rpc+logs";
    response.command = request.domain.empty() ? "inspect" : "inspect " + request.domain;
    response.logs = buildFallbackLogSection(m_config, request.tail, request.logLevel);
    response.executor = buildUnavailableExecutorFallbackSection();
    response.storage = buildFallbackStorageSection(m_config);
    response.network = buildFallbackNetworkSection(m_config);
    response.chain = buildFallbackChainSection(m_config, request.timeoutMs);
    return response;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target fisco-bcos-test -j4`

Run: `ctest --test-dir build -R fisco-bcos-test --output-on-failure`

Expected: fallback-domain tests pass and overview renders degraded fields explicitly.

- [ ] **Step 5: Commit**

```bash
git add fisco-bcos-air/cli/FallbackInspectors.h fisco-bcos-air/cli/FallbackInspectors.cpp fisco-bcos-air/cli/InspectApplication.cpp tests/unittest/CommandHelperTest.cpp
git commit -m "feat: add rpc and log fallback inspectors"
```

### Task 4: Add opt-in admin IPC config, protocol, server, and client selection

**Files:**
- Create: `fisco-bcos-air/cli/AdminIPCProtocol.h`
- Create: `fisco-bcos-air/cli/AdminIPCProtocol.cpp`
- Create: `fisco-bcos-air/cli/AdminIPCServer.h`
- Create: `fisco-bcos-air/cli/AdminIPCServer.cpp`
- Create: `fisco-bcos-air/cli/AdminIPCClient.h`
- Create: `fisco-bcos-air/cli/AdminIPCClient.cpp`
- Modify: `fisco-bcos-air/AirNodeInitializer.h`
- Modify: `fisco-bcos-air/AirNodeInitializer.cpp`
- Modify: `fisco-bcos-air/cli/InspectConfig.cpp`
- Modify: `fisco-bcos-air/cli/InspectApplication.cpp`
- Test: `tests/unittest/CommandHelperTest.cpp`

- [ ] **Step 1: Write the failing admin-config and source-selection tests**

```cpp
BOOST_AUTO_TEST_CASE(selectsAdminIpcWhenEnabledAndReachable)
{
    bcos::air::cli::InspectConfig config;
    config.adminEnabled = true;
    config.adminIPCPath = "/tmp/fisco-bcos-admin.sock";

    bcos::air::cli::InspectApplication app(config);
    app.setAdminReachabilityForTest(true);
    BOOST_CHECK_EQUAL(app.selectSource(), "admin-ipc");
}

BOOST_AUTO_TEST_CASE(fallsBackWhenAdminIpcUnavailable)
{
    bcos::air::cli::InspectConfig config;
    config.adminEnabled = true;
    config.adminIPCPath = "/tmp/missing.sock";

    bcos::air::cli::InspectApplication app(config);
    app.setAdminReachabilityForTest(false);
    BOOST_CHECK_EQUAL(app.selectSource(), "rpc+logs");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target fisco-bcos-test -j4`

Expected: missing admin protocol/client/server support.

- [ ] **Step 3: Write the minimal admin IPC protocol and wiring**

```cpp
struct AdminInspectRequest
{
    std::string command;
    std::string domain;
    bool jsonOutput = false;
    bool verbose = false;
    int timeoutMs = 3000;
};

struct AdminInspectReply
{
    bool ok = false;
    Json::Value payload = Json::objectValue;
    std::string error;
};
```

```cpp
class AdminIPCServer
{
public:
    void start(const InspectConfig& config, std::function<Json::Value(const AdminInspectRequest&)> handler);
    void stop();
};
```

```cpp
class AdminIPCClient
{
public:
    bool reachable(const std::string& socketPath) const;
    AdminInspectReply request(const std::string& socketPath, const AdminInspectRequest& request) const;
};
```

```cpp
if (config.adminEnabled && m_adminClient.reachable(config.adminIPCPath))
{
    return runAdminIPC(request);
}
return runFallback(request);
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target fisco-bcos-test -j4`

Expected: source selection prefers admin IPC when reachable and falls back cleanly otherwise.

- [ ] **Step 5: Commit**

```bash
git add fisco-bcos-air/cli/AdminIPCProtocol.h fisco-bcos-air/cli/AdminIPCProtocol.cpp fisco-bcos-air/cli/AdminIPCServer.h fisco-bcos-air/cli/AdminIPCServer.cpp fisco-bcos-air/cli/AdminIPCClient.h fisco-bcos-air/cli/AdminIPCClient.cpp fisco-bcos-air/AirNodeInitializer.h fisco-bcos-air/AirNodeInitializer.cpp fisco-bcos-air/cli/InspectConfig.cpp fisco-bcos-air/cli/InspectApplication.cpp tests/unittest/CommandHelperTest.cpp
git commit -m "feat: add opt-in admin ipc inspect path"
```

### Task 5: Expose in-process admin inspectors and verify the binary build

**Files:**
- Create: `fisco-bcos-air/cli/AdminInspectors.h`
- Create: `fisco-bcos-air/cli/AdminInspectors.cpp`
- Modify: `fisco-bcos-air/cli/AdminIPCServer.cpp`
- Modify: `fisco-bcos-air/cli/InspectTypes.cpp`
- Modify: `fisco-bcos-air/main.cpp`
- Test: `tests/unittest/CommandHelperTest.cpp`

- [ ] **Step 1: Write the failing in-process inspection tests**

```cpp
BOOST_AUTO_TEST_CASE(adminExecutorSectionReportsAvailableWhenInitializerPresent)
{
    auto section = bcos::air::cli::buildAdminExecutorSectionForTest(true);
    BOOST_CHECK(section.available);
    BOOST_CHECK(section.data.isObject());
}

BOOST_AUTO_TEST_CASE(adminStorageSectionIncludesStorageType)
{
    auto section = bcos::air::cli::buildAdminStorageSectionForTest("RocksDB");
    BOOST_CHECK_EQUAL(section.data["storageType"].asString(), "RocksDB");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target fisco-bcos-test -j4`

Expected: missing admin inspector builders.

- [ ] **Step 3: Write the minimal admin inspectors**

```cpp
InspectSection buildAdminExecutorSection(const bcos::initializer::Initializer& initializer)
{
    InspectSection section;
    section.available = initializer.scheduler() != nullptr;
    section.data["schedulerAvailable"] = initializer.scheduler() != nullptr;
    section.data["txpoolAvailable"] = initializer.txPoolInitializer() != nullptr;
    return section;
}
```

```cpp
InspectSection buildAdminStorageSection(const bcos::initializer::Initializer& initializer)
{
    InspectSection section;
    section.available = initializer.storage() != nullptr;
    section.data["storageType"] = initializer.nodeConfig()->storageType();
    section.data["statePath"] = initializer.nodeConfig()->storagePath();
    return section;
}
```

```cpp
Json::Value AdminIPCServer::handleInspect(const AdminInspectRequest& request)
{
    InspectResponse response;
    response.source = "admin-ipc";
    response.executor = buildAdminExecutorSection(*m_initializer);
    response.storage = buildAdminStorageSection(*m_initializer);
    return toJson(response);
}
```

- [ ] **Step 4: Run test and binary build to verify it passes**

Run: `cmake --build build --target fisco-bcos-test fisco-bcos -j4`

Expected: inspect tests pass and the `fisco-bcos` binary links with the new CLI sources.

- [ ] **Step 5: Commit**

```bash
git add fisco-bcos-air/cli/AdminInspectors.h fisco-bcos-air/cli/AdminInspectors.cpp fisco-bcos-air/cli/AdminIPCServer.cpp fisco-bcos-air/cli/InspectTypes.cpp fisco-bcos-air/main.cpp tests/unittest/CommandHelperTest.cpp
git commit -m "feat: expose in-process admin inspect summaries"
```

### Task 6: Final verification and PR preparation

**Files:**
- Modify: none unless fixes are required

- [ ] **Step 1: Run targeted verification**

Run: `cmake --build build --target fisco-bcos -j4`

Run: `cmake --build build --target fisco-bcos-test -j4`

Run: `ctest --test-dir build -R fisco-bcos-test --output-on-failure`

Expected: binary and test target build successfully and the new command parsing / inspect tests pass.

- [ ] **Step 2: Run manual smoke commands**

Run: `./build/fisco-bcos/fisco-bcos -c tools/BcosAirBuilder/nodes/127.0.0.1/node0/config.ini -g tools/BcosAirBuilder/nodes/127.0.0.1/node0/config.genesis -cli inspect --json`

Run: `./build/fisco-bcos/fisco-bcos -c tools/BcosAirBuilder/nodes/127.0.0.1/node0/config.ini -cli inspect logs --tail 5`

Expected: first command prints structured JSON with `source`, second command prints human-readable log diagnostics.

- [ ] **Step 3: Inspect git diff before PR**

Run: `git status --short`

Run: `git diff --stat`

Expected: only planned worktree files are modified.

- [ ] **Step 4: Push branch and prepare PR**

Run: `git push -u origin feat/air-cli-inspect`

Expected: branch is published to `origin`.

- [ ] **Step 5: Create PR summary**

```text
Title: feat: add air node inspect cli with admin ipc fallback

Summary:
- add -cli inspect and inspect <domain> commands to fisco-bcos air binary
- default to human-readable output with --json support
- add opt-in local admin ipc for in-process inspection
- fall back to rpc plus logs when admin ipc is disabled or unreachable
```

## Self-Review

- Spec coverage: the plan covers CLI parsing, human-readable and JSON output, overview and domain commands, opt-in admin IPC, fallback to `rpc+logs`, and read-only behavior.
- Placeholder scan: no `TODO`, `TBD`, or “implement later” markers remain.
- Type consistency: `CLIRequest`, `InspectConfig`, `InspectResponse`, and `AdminInspectRequest` names are consistent across tasks.
