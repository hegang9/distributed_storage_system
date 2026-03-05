# 分布式KV存储系统 (Distributed KV Storage System based on Raft)

基于 C++11 实现的高性能分布式 KV 存储系统。融合了底层的非对称协程库（Fiber）、基于 dlsym 的 Hook 技术、跳表（SkipList）内存存储引擎、自研轻量级 RPC 框架以及 Raft 一致性共识算法，保障了系统在高并发场景下的高性能响应与强一致性。

## 🌟 核心特性 (Features)

* **底层协程库与 Hook 机制：** 从零构建 N-M 非对称协程模型（ucontext），利用 `Epoll` 机制管理 I/O 与定时器，并通过 `dlsym` Hook 劫持标准系统的阻塞调用（如 read/write/sleep），实现以极低心智负担的同步代码写出极高并发的反向控制层（类似 Go 的 goroutine机制）。
* **自研轻量级 RPC 框架：** 基于 Protobuf 开发的高效通信框架，解耦底层序列化与网络层模型，实现各分布式节点间的快速调用与高吞吐网络传输。
* **Raft 共识算法引擎：** 完整实现了 Raft 协议（领导者选举、日志复制、持久化机制），在部分节点宕机或网络分区下依然能保证集群数据的强一致性与高可用。
* **SkipList 内存存储引擎：** 底层使用跳表作为 KV 存储引擎的核心数据结构，实现了高效的 O(logN) 的并发增删改查。

## 🏗️ 核心模块 (Architecture)

* `src/fiber/`：用户态非对称协程库与底层调度器（Fiber/Scheduler/IOManager/Hook）。
* `src/raftCore/`：Raft 核心状态机与共识算法实现。
* `src/rpc/` & `src/raftRpcPro/`：自封装的轻量级底层网络通信与 RPC 框架层。
* `src/skipList/`：作为 KV 存储支撑的高效跳表引擎。

## 🛠️ 编译与运行 (Build & Run)

### 环境依赖
* **OS:** Linux (CentOS/Ubuntu/Debian 等)
* **Compiler:** 至少支持 C++11 (推荐 GCC 4.8 或以上)
* **Build Tool:** CMake 3.0+

### 编译指令

```bash
# 获取源码后进入项目主目录
cd /home/hegang/DistributedStorage

# 清理并创建构建目录
rm -rf build/*
mkdir -p build
cd build

# 编译项目 (-j 根据机器核心数调整)
cmake ..
make -j4
```

编译完成后，相关可执行文件及测试程序将生成在 `bin/` 目录下。

## 📂 测试说明

在 `bin/` 目录下包含了多个测试程序，包括协程基准测试、IOManager 测试、SkipList 性能测试等，可以通过以下方式运行：
```bash
# 例如运行协程框架的基本性能测试
./bin/benchmark_coro_server
```
（更多关于组件启动及测试的说明，请参考详细的 `docs/` 和相关 MD 文件）。
