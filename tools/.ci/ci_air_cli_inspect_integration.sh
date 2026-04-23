#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

DEFAULT_BINARY="${REPO_ROOT}/build-make/fisco-bcos-air/fisco-bcos"
FISCO_BCOS_BINARY="${FISCO_BCOS_BINARY:-${1:-${DEFAULT_BINARY}}}"
BUILD_CHAIN="${REPO_ROOT}/tools/BcosAirBuilder/build_chain.sh"

TMP_ROOT=""
NODE_ROOT=""
NODE_DIR=""

LOG_INFO() {
    local content="${1}"
    echo -e "\033[32m[INFO] ${content}\033[0m"
}

LOG_ERROR() {
    local content="${1}"
    echo -e "\033[31m[ERROR] ${content}\033[0m"
}

cleanup() {
    local exit_code=$?

    if [[ -n "${NODE_DIR}" && -f "${NODE_DIR}/stop.sh" ]]; then
        bash "${NODE_DIR}/stop.sh" >/dev/null 2>&1 || true
    fi

    if [[ -n "${TMP_ROOT}" && -d "${TMP_ROOT}" ]]; then
        rm -rf "${TMP_ROOT}"
    fi

    exit "${exit_code}"
}

trap cleanup EXIT

assert_file_exists() {
    local path="${1}"
    if [[ ! -f "${path}" ]]; then
        LOG_ERROR "File not found: ${path}"
        exit 1
    fi
}

assert_binary() {
    if [[ ! -x "${FISCO_BCOS_BINARY}" ]]; then
        LOG_ERROR "fisco-bcos binary is not executable: ${FISCO_BCOS_BINARY}"
        exit 1
    fi
    assert_file_exists "${BUILD_CHAIN}"
}

prepare_chain() {
    TMP_ROOT="$(mktemp -d /tmp/fisco-air-cli-it.XXXXXX)"
    NODE_ROOT="${TMP_ROOT}/nodes/127.0.0.1"
    NODE_DIR="${NODE_ROOT}/node0"

    LOG_INFO "Generate temporary air chain under ${TMP_ROOT}"
    bash "${BUILD_CHAIN}" -p 43300,33200 -l 127.0.0.1:1 -o "${TMP_ROOT}/nodes" \
        -e "${FISCO_BCOS_BINARY}" >/tmp/ci_air_cli_inspect_build.log 2>&1

    assert_file_exists "${NODE_DIR}/config.ini"
    assert_file_exists "${NODE_DIR}/config.genesis"

    cat <<'EOF' >>"${NODE_DIR}/config.ini"

[admin]
enable=true
ipc_path=./run/admin.sock
EOF
}

start_node() {
    LOG_INFO "Start node for admin-ipc integration test"
    bash "${NODE_DIR}/start.sh"

    local socket_path="${NODE_DIR}/run/admin.sock"
    local ready="false"
    for _ in $(seq 1 30); do
        if [[ -S "${socket_path}" ]]; then
            ready="true"
            break
        fi
        sleep 1
    done

    if [[ "${ready}" != "true" ]]; then
        LOG_ERROR "Admin IPC socket was not created: ${socket_path}"
        tail -n 100 "${NODE_DIR}/nohup.out" || true
        exit 1
    fi
}

collect_fixtures() {
    LOG_INFO "Collect inspect and RPC fixtures"

    (
        cd "${NODE_DIR}"
        ../fisco-bcos -c config.ini -g config.genesis -cli inspect --json \
            > inspect-overview.json
    )

    curl -sk \
        --cert "${NODE_ROOT}/sdk/sdk.crt" \
        --key "${NODE_ROOT}/sdk/sdk.key" \
        --cacert "${NODE_ROOT}/sdk/ca.crt" \
        https://127.0.0.1:33200 \
        -X POST \
        --data '{"jsonrpc":"2.0","method":"getBlockNumber","params":["group0", ""],"id":67}' \
        >"${NODE_DIR}/rpc-getBlockNumber.json"

    curl -sk \
        --cert "${NODE_ROOT}/sdk/sdk.crt" \
        --key "${NODE_ROOT}/sdk/sdk.key" \
        --cacert "${NODE_ROOT}/sdk/ca.crt" \
        https://127.0.0.1:33200 \
        -X POST \
        --data '{"jsonrpc":"2.0","method":"getTotalTransactionCount","params":["group0", ""],"id":68}' \
        >"${NODE_DIR}/rpc-getTotalTransactionCount.json"
}

verify_results() {
    LOG_INFO "Verify admin-ipc payload against config and RPC"

    python3 - <<'PY' "${NODE_DIR}"
import configparser
import json
import sys
from pathlib import Path

node_dir = Path(sys.argv[1])
inspect = json.loads((node_dir / "inspect-overview.json").read_text())
rpc_block = json.loads((node_dir / "rpc-getBlockNumber.json").read_text())
rpc_total = json.loads((node_dir / "rpc-getTotalTransactionCount.json").read_text())

ini = configparser.ConfigParser()
ini.read(node_dir / "config.ini")
genesis = configparser.ConfigParser()
genesis.read(node_dir / "config.genesis")

checks = []
checks.append(("source", inspect["source"], "admin-ipc"))
checks.append(("warnings", inspect["warnings"], []))
checks.append(("summary.latestBlock", inspect["summary"]["latestBlock"], str(rpc_block["result"])))
checks.append((
    "summary.totalTx",
    inspect["summary"]["totalTx"],
    str(rpc_total["result"]["transactionCount"]),
))
checks.append(("chain.available", inspect["chain"]["available"], True))
checks.append(("chain.chainID", inspect["chain"]["data"]["chainID"], genesis["chain"]["chain_id"]))
checks.append(("chain.groupID", inspect["chain"]["data"]["groupID"], genesis["chain"]["group_id"]))
checks.append((
    "chain.consensusType",
    inspect["chain"]["data"]["consensusType"],
    genesis["consensus"]["consensus_type"],
))
checks.append((
    "chain.latestBlock",
    inspect["chain"]["data"]["latestBlock"],
    rpc_total["result"]["blockNumber"],
))
checks.append((
    "chain.totalTx",
    inspect["chain"]["data"]["totalTx"],
    rpc_total["result"]["transactionCount"],
))
checks.append((
    "chain.failedTx",
    inspect["chain"]["data"]["failedTx"],
    rpc_total["result"]["failedTransactionCount"],
))
checks.append(("node.available", inspect["node"]["available"], True))
checks.append(("node.chainID", inspect["node"]["data"]["chainID"], genesis["chain"]["chain_id"]))
checks.append(("node.groupID", inspect["node"]["data"]["groupID"], genesis["chain"]["group_id"]))
checks.append((
    "node.nodeID",
    inspect["node"]["data"]["nodeID"],
    genesis["consensus"]["node.0"].split(":")[0].strip(),
))
checks.append((
    "node.compatibilityVersion",
    inspect["node"]["data"]["compatibilityVersion"],
    genesis["version"]["compatibility_version"],
))
checks.append(("network.available", inspect["network"]["available"], True))
checks.append((
    "network.rpcListenIP",
    inspect["network"]["data"]["rpcListenIP"],
    ini["rpc"]["listen_ip"],
))
checks.append((
    "network.rpcListenPort",
    inspect["network"]["data"]["rpcListenPort"],
    int(ini["rpc"]["listen_port"]),
))
checks.append((
    "network.p2pListenIP",
    inspect["network"]["data"]["p2pListenIP"],
    ini["p2p"]["listen_ip"],
))
checks.append((
    "network.p2pListenPort",
    inspect["network"]["data"]["p2pListenPort"],
    int(ini["p2p"]["listen_port"]),
))
checks.append((
    "network.web3Enabled",
    inspect["network"]["data"]["web3Enabled"],
    ini["web3_rpc"].getboolean("enable"),
))
checks.append(("network.connectedNodeCount", inspect["network"]["data"]["connectedNodeCount"], 1))
checks.append(("storage.available", inspect["storage"]["available"], True))
checks.append(("storage.type", inspect["storage"]["data"]["type"], "RocksDB"))
checks.append(("storage.dataPath", inspect["storage"]["data"]["dataPath"], ini["storage"]["data_path"]))
checks.append((
    "storage.stateDBPath",
    inspect["storage"]["data"]["stateDBPath"],
    ini["storage"]["data_path"] + "/state",
))
checks.append((
    "storage.blockDBPath",
    inspect["storage"]["data"]["blockDBPath"],
    ini["storage"]["data_path"] + "/block",
))
checks.append(("executor.available", inspect["executor"]["available"], True))
checks.append((
    "executor.serialExecute",
    inspect["executor"]["data"]["serialExecute"],
    genesis["executor"].getboolean("is_serial_execute"),
))
checks.append(("logs.available", inspect["logs"]["available"], True))
checks.append((
    "logs.path",
    str(Path(inspect["logs"]["data"]["path"]).as_posix()),
    str(Path(ini["log"]["log_path"]).as_posix()),
))
checks.append(("logs.entryCountPositive", len(inspect["logs"]["data"]["entries"]) > 0, True))

failed = [(name, actual, expected) for name, actual, expected in checks if actual != expected]

for name, actual, expected in checks:
    print(f"{name}: actual={actual!r} expected={expected!r}")

if failed:
    print("FAILED_ITEMS:")
    for name, actual, expected in failed:
        print(f"  {name}: actual={actual!r} expected={expected!r}")
    raise SystemExit(1)

print("ALL_CHECKS_PASSED")
PY
}

main() {
    assert_binary
    prepare_chain
    start_node
    collect_fixtures
    verify_results
    LOG_INFO "air cli inspect admin-ipc integration test passed"
}

main "$@"
