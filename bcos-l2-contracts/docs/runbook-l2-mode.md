# Runbook: running a node in L2 mode

How to stand up a single FISCO-BCOS node in OP-Stack L2 mode (`chain_mode =
l2`), what changes versus the default `pbft` mode, the error strings you will
hit if the genesis is wrong, and the upgrade paths.

This is node-bring-up only. The op-node / sequencer wiring and the L1 bridge
are A8-workstream concerns and are not covered here.

## Smallest failing scenario this prevents

You write `chain_mode = l2` into `config.genesis` but leave the `[alloc.*]`
sections out. The node refuses to start:

```
invalid config: L2 chain_mode requires non-empty [alloc.*] section in config.genesis
```

L2 mode has no contracts unless genesis allocs materialize them — predeploy
constructors never run on-chain. The quick start below produces those allocs.

## Quick start (5 steps)

All paths are relative to the repo root. `tools/opstack-genesis/` holds the
allocs generator; `bcos-l2-contracts/` holds the Solidity suite.

### 1. Build the contracts and edit a chain config

```bash
cd tools/opstack-genesis
make contracts CONTRACTS=../../bcos-l2-contracts   # forge build + forge build --contracts vendor/op-fork/src
cp chain-config.template.yaml chain-config.yaml
$EDITOR chain-config.yaml      # set chain_id, l2_block_gas_limit, compatibility_version, system_config_owner
```

`chain_id` is patched into `SystemConfig`'s `immutable chainId` (runtime
bytecode, not a storage slot). `system_config_owner` is the SystemConfig
governance owner. See `tools/opstack-genesis/README.md` for every field.

### 2. Generate the genesis allocs

```bash
make allocs CONFIG=chain-config.yaml OUT=allocs.ini
# equivalently:
python3 build-allocs.py --config chain-config.yaml \
    --contracts ../../bcos-l2-contracts --out allocs.ini
```

`allocs.ini` contains the `[alloc.N]` + `[alloc.N.storage]` fragments for all
13 predeploys (runtime bytecode + seeded storage).

### 3. Assemble `config.genesis`

Set `chain_mode = l2` under `[chain]` and append the generated `allocs.ini`:

```ini
[chain]
    sm_crypto = false
    group_id  = group0
    chain_id  = chain0
    chain_mode = l2

; ... rest of the normal genesis ...

; --- appended from allocs.ini ---
[alloc.0]
    address = 42000000000000000000000000000000000000c0
    balance = 0
    nonce   = 0
    code    = 0x60806040...
[alloc.0.storage]
    0x0000...0000 = 0x...
; ... 12 more predeploys ...
```

`chain_mode` defaults to `pbft` when the key is absent. `NodeConfig` parses it
via `_genesisConfig.get<std::string>("chain.chain_mode", "pbft")`
(`bcos-tool/bcos-tool/NodeConfig.cpp:211`) and rejects anything that is neither
`pbft` nor `l2`.

### 4. Start the node

```bash
./fisco-bcos -c config.ini -g config.genesis
```

On first init with L2 mode, the Ledger writes a `genesis_immutable_hash` row
into `SYS_CURRENT_STATE` over `chain_mode / allocs / chainID / groupID /
smCrypto / isWasm` (`bcos-ledger/bcos-ledger/Ledger.cpp:2215-2229`). Every later
startup recomputes and verifies that hash; it is never rewritten.

### 5. Verify

```bash
# all 13 predeploys carry code
cast code 0x42000000000000000000000000000000000000C0 --rpc-url http://127.0.0.1:8545

# getChainConfig() returns 4 ABI words (>= 128 bytes)
cast call 0x42000000000000000000000000000000000000C0 0x606c0c94 --rpc-url http://127.0.0.1:8545

# eth_chainId agrees with SystemConfig chainId (PR-4/PR-6 path consistency)
cast chain-id --rpc-url http://127.0.0.1:8545
```

`Testing/l2-integration/run-all.sh` automates the equivalent checks against a
running devnet.

## L2 mode vs pbft mode

| Aspect | `pbft` (default) | `l2` |
|--------|------------------|------|
| `[alloc.*]` genesis allocs | rejected | required (non-empty) |
| Predeploys at block 0 | none | 13 (2 self-written + 11 vendored OP) |
| FISCO-private precompiles (`0x1000`..) | live | `disabledInL2()` removes 13 of them (`bcos-executor/src/precompiled/L2DisabledSet.h`) |
| KZG point-evaluation `0x0a` | not registered | registered (A6.14) |
| Per-block config source | static node config | `SystemConfig.getChainConfig()` staticcall each block (PR-4 `L2ConfigLoader`) |
| `eth_chainId` source | node config | `LedgerConfig.chainId` projected from `getChainConfig()` word 0 (A6.9) |
| WASM executor | allowed | rejected (`is_wasm=true` unsupported in L2) |

## Common errors

All are thrown as `bcos::tool::InvalidConfig` (or `std::runtime_error` for the
runtime staticcall path) with these exact `errinfo_comment` strings:

| Error string (verbatim) | Cause | Source |
|-------------------------|-------|--------|
| `invalid [chain].chain_mode: '<v>'; must be 'pbft' or 'l2'` | `chain_mode` set to something other than `pbft`/`l2` | `NodeConfig.cpp:214-216` |
| `L2 chain_mode requires non-empty [alloc.*] section in config.genesis` | `chain_mode = l2` but no allocs | `NodeConfig.cpp:312-316` |
| `pbft chain_mode does not support [alloc.*] section` | allocs present under `pbft` mode | `NodeConfig.cpp:318-321` |
| `L2 chain_mode requires the EVM executor; is_wasm=true is not supported` | `chain_mode = l2` with `is_wasm = true` | `NodeConfig.cpp:326-330` |
| `[alloc.N].address duplicate: <addr>` | two alloc entries share an address | `NodeConfig.cpp:249-253` |
| `[alloc.N].nonce must fit in uint64: <v>` | alloc `nonce` exceeds `uint64` (it is frozen into the immutable hash as a uint64 BE field) | `NodeConfig.cpp:264-272` |
| `[alloc.N].address must be 40 hex chars: <v>` / `... must be 0x-prefixed` / `... is not valid hex` / `... must be even-length hex` | malformed alloc hex field | `NodeConfig.cpp:83-104` |
| `genesis immutable fields (chain_mode / allocs / chainID / groupID / smCrypto / isWasm) changed since first init; refuse to start. stored=<h> computed=<h>` | a frozen genesis field changed after first init | `Ledger.cpp:1883-1889` |
| `L2ConfigLoader: getChainConfig() returned <n> bytes, expected at least 128` | per-block `getChainConfig()` staticcall reverted / OOG / truncated | `L2ConfigLoader.h:109-111` |
| `L2ConfigLoader: getChainConfig() returned chainId == 0 (genesis immutable not patched; breaks EIP-155)` | `SystemConfig.chainId` immutable was never bytecode-patched (build-allocs.py step skipped) | `L2ConfigLoader.h:131-133` |

The immutable-hash error is the most common operational footgun: once a node
has initialized in L2 mode you cannot edit `chain_mode`, the allocs, `chainID`,
`groupID`, `smCrypto`, or `isWasm` in place — the node will refuse to start.
Changing any of them means a new chain (see below).

## Upgrade paths

| Change | Path |
|--------|------|
| `chain_mode` (pbft <-> l2) | immutable after first init; start a **new chain** |
| Add / change a predeploy (different allocs) | allocs are in the immutable hash; start a **new chain** |
| Bump the vendored OP fork to a new tag | re-vendor the source tree — see `runbook-op-fork-upgrade.md` |
| Phase B governance handover (DAO switch) | transfer `ProxyAdmin` ownership to the DAO (a runtime `ProxyAdmin.transferOwnership` tx, not a genesis change); the L2 hardfork enum (`FeaturesL2HardforkTest`) gates the Phase B feature activation |

There is no in-place migration for any of the immutable genesis fields by
design: the immutable-hash guard exists precisely to stop a node from silently
running on a genesis that differs from the one it was initialized with.
