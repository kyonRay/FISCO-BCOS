# FISCO-BCOS Air CLI Inspect 操作手册

本文档描述 `fisco-bcos air` 二进制中新增的本地只读 CLI 能力，重点面向运维和 AI agent 的手工验证。

当前功能目标是让用户直接通过节点二进制执行：

```bash
./fisco-bcos -c config.ini -g config.genesis -cli inspect ...
```

在不新增远程运维接口的前提下，读取当前节点视角的本地运行信息。

## 1. 功能概览

### 1.1 入口

节点原有启动方式不变：

```bash
./fisco-bcos -c config.ini -g config.genesis
```

新增 CLI 模式：

```bash
./fisco-bcos -c config.ini -g config.genesis -cli inspect
./fisco-bcos -c config.ini -g config.genesis -cli inspect <domain>
```

### 1.2 输出模式

- 默认输出为人类可读文本
- 加 `--json` 输出 JSON，便于 AI agent/脚本消费

### 1.3 支持的 domain

- `node`
- `chain`
- `network`
- `storage`
- `executor`
- `logs`

不带 domain 时返回总览视图。

### 1.4 数据来源选择规则

- 默认优先级：
  - 若本地 Admin IPC 已开启且可达，使用 `admin-ipc`
  - 否则自动退化到 `rpc+logs`
- `admin-ipc` 默认关闭
- `rpc+logs` 是 MVP 的兜底只读视图

### 1.5 当前实现边界

- 只支持本地节点视角
- 只读，不修改节点状态
- 不新增远程运维接口
- `verbose` 和 `--source` 参数当前已解析，但暂未带来额外输出差异，可视为预留参数
- fallback 模式下：
  - `logs` 可真实读取本地日志
  - 其他域部分为降级信息或配置视图
- admin IPC 模式下：
  - `node/chain/network/storage/executor/logs` 会尽量返回真实进程内状态
  - `logs` 仍然读取本地日志文件，不直接读 boost log 内存缓冲

## 2. 配置说明

### 2.1 Admin IPC 开关

支持以下两种配置名，任选其一即可：

```ini
[admin]
enable=true
ipc_path=./run/admin.sock
```

或：

```ini
[admin_ipc]
enable=true
path=./run/admin.sock
```

默认建议：

```ini
[admin]
enable=false
ipc_path=./run/admin.sock
```

### 2.2 相对路径解析规则

`inspect` 相关路径会按 `config.ini` 所在目录解析，不按当前 shell 工作目录解析。

当前已覆盖的相对路径包括：

- `log.log_path`
- `storage.data_path`
- `admin.ipc_path`
- `admin_ipc.path`

例如：

```ini
[log]
log_path=./log
```

如果配置文件在 `/data/node0/config.ini`，则 CLI 读取的实际日志目录是：

```text
/data/node0/log
```

## 3. 命令格式

### 3.1 总览

```bash
./fisco-bcos -c config.ini -g config.genesis -cli inspect
```

### 3.2 读取单个 domain

```bash
./fisco-bcos -c config.ini -g config.genesis -cli inspect node
./fisco-bcos -c config.ini -g config.genesis -cli inspect chain
./fisco-bcos -c config.ini -g config.genesis -cli inspect network
./fisco-bcos -c config.ini -g config.genesis -cli inspect storage
./fisco-bcos -c config.ini -g config.genesis -cli inspect executor
./fisco-bcos -c config.ini -g config.genesis -cli inspect logs
```

### 3.3 JSON 输出

```bash
./fisco-bcos -c config.ini -g config.genesis -cli inspect --json
./fisco-bcos -c config.ini -g config.genesis -cli inspect network --json
```

### 3.4 日志读取选项

```bash
./fisco-bcos -c config.ini -g config.genesis -cli inspect logs --tail 100
./fisco-bcos -c config.ini -g config.genesis -cli inspect logs --level INFO
./fisco-bcos -c config.ini -g config.genesis -cli inspect logs --json --tail 50 --level ERROR
```

### 3.5 超时选项

该参数主要用于访问进程内状态时的异步读取等待时间，单位毫秒：

```bash
./fisco-bcos -c config.ini -g config.genesis -cli inspect chain --timeout 5000
```

### 3.6 预留参数

当前可解析但暂未带来额外差异：

```bash
./fisco-bcos -c config.ini -g config.genesis -cli inspect --verbose
./fisco-bcos -c config.ini -g config.genesis -cli inspect --source
```

## 4. 输出字段说明

### 4.1 顶层字段

无论文本还是 JSON，核心语义一致：

- `source`
  - `admin-ipc` 表示来自本地进程内 IPC
  - `rpc+logs` 表示 IPC 不可用，已退化到 fallback 模式
- `command`
  - 当前固定为 `inspect`
- `domain`
  - 指定 domain 时返回
- `timestamp`
  - CLI 生成响应的本地时间
- `summary.latestBlock`
- `summary.totalTx`
- `warnings`

### 4.2 section 结构

每个 section 都是统一结构：

- `available`
- `reason`
- `data`

含义：

- `available=true` 表示该 section 当前可提供有效数据
- `available=false` 表示当前不可用或为降级视图
- `reason` 解释不可用原因
- `data` 为实际载荷

## 5. 手工测试准备

### 5.1 编译

建议使用当前分支验证时已采用的构建参数：

```bash
cmake -S . -B build-make -G "Unix Makefiles" \
  -DTESTS=ON \
  -DWITH_TIKV=OFF \
  -DWITH_TARS_SERVICES=OFF \
  -DWITH_LIGHTNODE=OFF \
  -DWITH_CPPSDK=OFF \
  -DBUILD_STATIC=OFF \
  -DWITH_WASM=OFF \
  -DTOOLS=OFF

cmake --build build-make --target fisco-bcos -j4
```

二进制路径：

```text
build-make/fisco-bcos-air/fisco-bcos
```

### 5.2 准备可启动节点目录

你需要准备一套真实可运行的 air 节点目录，至少包含：

- `config.ini`
- `config.genesis`
- 证书/私钥相关文件
- 节点运行目录，如 `data/`、`log/`

本文以下示例假设节点目录为：

```text
/path/to/node0/
```

对应：

- `/path/to/node0/config.ini`
- `/path/to/node0/config.genesis`

## 6. 场景一：不开 Admin IPC，只验证 fallback

这是最先应该验证的主流程。

### 6.1 配置

确保 `config.ini` 中关闭 admin IPC：

```ini
[admin]
enable=false
ipc_path=./run/admin.sock
```

或：

```ini
[admin_ipc]
enable=false
path=./run/admin.sock
```

### 6.2 启动节点

```bash
cd /path/to/node0
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis
```

### 6.3 验证总览命令

另开一个终端：

```bash
cd /path/to/node0
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect
```

预期：

- 命令成功返回
- `Source` 或 JSON 中的 `source` 为 `rpc+logs`
- 输出包含 warning，说明 admin IPC 不可用，已退化

### 6.4 验证 domain 读取

分别执行：

```bash
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect node
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect chain
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect network
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect storage
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect executor
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect logs
```

预期：

- `node/network/storage/logs` 通常能返回内容
- `chain/executor` 在 fallback 下可能是降级或 unavailable
- 结果中只显示所请求的单个 domain

### 6.5 验证 JSON 输出

```bash
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect --json
```

预期：

- 输出为合法 JSON
- `source` 为 `rpc+logs`
- 有 `summary`、`warnings`、各 section 字段

### 6.6 验证日志读取

先确保 `log.log_path` 对应目录下已有节点日志。

执行：

```bash
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect logs --tail 20
```

再执行：

```bash
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect logs --json --tail 20 --level INFO
```

预期：

- 能读到最近日志
- `--tail` 生效
- `--level INFO` 仅保留包含 `INFO` 的日志行
- JSON 输出里 `logs.data.entries` 为数组

### 6.7 验证相对路径解析

这一步重点验证不是按当前工作目录读日志。

例如：

```bash
cd /tmp
/path/to/build-make/fisco-bcos-air/fisco-bcos \
  -c /path/to/node0/config.ini \
  -g /path/to/node0/config.genesis \
  -cli inspect logs --json --tail 10
```

预期：

- 仍能正确读取 `/path/to/node0/log`
- `logs.data.path` 是解析后的绝对路径或规范化路径
- 不会因为当前 shell 在 `/tmp` 而找错日志目录

## 7. 场景二：开启 Admin IPC，验证进程内视图

### 7.1 修改配置

在 `config.ini` 中开启：

```ini
[admin]
enable=true
ipc_path=./run/admin.sock
```

建议保留相对路径，方便验证配置目录锚定。

### 7.2 启动节点

```bash
cd /path/to/node0
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis
```

### 7.3 验证 socket 文件创建

```bash
ls -l /path/to/node0/run/admin.sock
```

预期：

- 文件存在
- 权限为本地 Unix socket，代码中会尝试设置为 `0600`

### 7.4 验证总览

另开终端执行：

```bash
cd /path/to/node0
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect
```

预期：

- `source=admin-ipc`
- 无 fallback warning
- `node/chain/network/storage/executor/logs` 中大部分应可用

### 7.5 验证各 domain

执行：

```bash
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect node --json
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect chain --json
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect network --json
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect storage --json
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect executor --json
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect logs --json --tail 20
```

重点观察：

- `node`
  - `chainID`
  - `groupID`
  - `nodeName`
  - `rpcService/gatewayService/executorService/txpoolService`
  - `withoutTarsFramework`
- `chain`
  - `consensusType`
  - `latestBlock`
  - `totalTx`
  - `pendingTxs`
- `network`
  - `rpc/web3/p2p` 监听地址
  - `connectedNodeCount`
  - `connectedNodes`
- `storage`
  - `type`
  - `dataPath`
  - `stateDBPath`
  - `blockDBPath`
- `executor`
  - `schedulerAvailable`
  - `storageAvailable`
  - `ledgerAvailable`
  - `baselineScheduler*`
- `logs`
  - 日志文件实际读取结果

## 8. 场景三：开启配置但让 Admin IPC 不可达

该场景用于验证自动退化逻辑。

### 8.1 做法 A：节点未启动

配置里保持：

```ini
[admin]
enable=true
ipc_path=./run/admin.sock
```

但不要启动节点，直接执行：

```bash
cd /path/to/node0
../../build-make/fisco-bcos-air/fisco-bcos -c config.ini -g config.genesis -cli inspect --json
```

预期：

- `source=rpc+logs`
- 不会报成 `admin-ipc`
- 有 fallback warning

### 8.2 做法 B：删除 socket

在节点退出后执行：

```bash
rm -f /path/to/node0/run/admin.sock
```

然后再次执行 CLI inspect。

预期与上面一致：

- 自动回退到 `rpc+logs`

## 9. 场景四：异常输入验证

### 9.1 缺少 CLI 子命令

```bash
./fisco-bcos -c config.ini -g config.genesis -cli
```

预期：

- 失败
- 提示 `cli command is required`

### 9.2 不支持的 CLI 子命令

```bash
./fisco-bcos -c config.ini -g config.genesis -cli status
```

预期：

- 失败
- 提示 `unsupported cli command`

### 9.3 位置参数过多

```bash
./fisco-bcos -c config.ini -g config.genesis -cli inspect logs extra
```

预期：

- 失败
- 提示 `too many cli positional arguments`

### 9.4 与其他 operation 冲突

```bash
./fisco-bcos -c config.ini -g config.genesis -cli inspect --prune
```

预期：

- 直接拒绝
- 提示 `cli mode can not be used with other operations`

## 10. 人类可读输出和 JSON 输出示例

### 10.1 文本人类可读示例

```text
Source: rpc+logs
Command: inspect logs
Timestamp: 2026-04-20 15:41:45
Summary: latestBlock=degraded totalTx=degraded
Warning: In-process admin IPC is unavailable; using source=rpc+logs degraded view.
Logs: available data={
  ...
}
```

### 10.2 JSON 示例关键字段

```json
{
  "source": "admin-ipc",
  "command": "inspect",
  "domain": "chain",
  "timestamp": "2026-04-20 15:41:45",
  "summary": {
    "latestBlock": "123",
    "totalTx": "456"
  },
  "chain": {
    "available": true,
    "data": {
      "latestBlock": 123,
      "totalTx": 456
    }
  },
  "warnings": []
}
```

## 11. 排障建议

### 11.1 `source` 一直是 `rpc+logs`

检查：

- `config.ini` 中是否真的开启了 `admin.enable=true`
- 节点是否已经启动
- `ipc_path` 对应 socket 是否存在
- CLI 使用的 `config.ini` 是否就是节点实际运行的那份

### 11.2 `logs` 读不到内容

检查：

- `log.log_path` 是否正确
- 日志目录中是否已有日志文件
- `--level` 过滤是否过严
- 是否在错误的配置目录上运行

### 11.3 `chain`/`executor` 不可用

如果 `source=rpc+logs`，这是预期内行为，属于降级视图。

如果 `source=admin-ipc` 但仍然 unavailable，重点检查：

- 节点是否完成初始化
- 相关模块对象是否已建立
- 是否存在 timeout，特别是 `--timeout` 设置过小

## 12. 建议的验收清单

建议按下面顺序验收：

1. 编译成功
2. `admin.enable=false` 时 `inspect --json` 返回 `source=rpc+logs`
3. `inspect logs --json --tail N --level INFO` 能读到本地日志
4. 从非节点工作目录执行 CLI，仍能正确解析相对路径
5. `admin.enable=true` 并启动节点后，返回 `source=admin-ipc`
6. `node/chain/network/storage/executor/logs` 六个 domain 都能单独调用
7. 节点停掉后，再次调用自动退化到 `rpc+logs`

## 13. 当前代码位置

如果你后续手工调试代码，核心文件如下：

- `libinitializer/CommandHelper.h`
- `libinitializer/CommandHelper.cpp`
- `fisco-bcos-air/main.cpp`
- `fisco-bcos-air/AirNodeInitializer.h`
- `fisco-bcos-air/AirNodeInitializer.cpp`
- `fisco-bcos-air/cli/InspectConfig.h`
- `fisco-bcos-air/cli/InspectConfig.cpp`
- `fisco-bcos-air/cli/InspectApplication.h`
- `fisco-bcos-air/cli/InspectApplication.cpp`
- `fisco-bcos-air/cli/FallbackInspectors.h`
- `fisco-bcos-air/cli/FallbackInspectors.cpp`
- `fisco-bcos-air/cli/AdminIPCProtocol.h`
- `fisco-bcos-air/cli/AdminIPCProtocol.cpp`
- `fisco-bcos-air/cli/AdminIPCClient.h`
- `fisco-bcos-air/cli/AdminIPCClient.cpp`
- `fisco-bcos-air/cli/AdminIPCServer.h`
- `fisco-bcos-air/cli/AdminIPCServer.cpp`
- `fisco-bcos-air/cli/AdminInspectors.h`
- `fisco-bcos-air/cli/AdminInspectors.cpp`
- `tests/unittest/AirInspectCLITest.cpp`
- `tests/unittest/CommandHelperTest.cpp`
