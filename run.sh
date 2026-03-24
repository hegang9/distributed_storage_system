#!/bin/bash

# 当命令执行出错时自动退出
set -e

PROJECT_DIR=$(pwd)
BUILD_DIR="${PROJECT_DIR}/build"
BIN_DIR="${PROJECT_DIR}/bin"

echo "==================================================="
echo "🚀 开始构建 分布式KV存储系统 (DistributedStorage)"
echo "==================================================="

# 1. 创建并进入 build 目录
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# 2. 生成 Makefile
echo "=== 📦 运行 CMake 配置 ==="
cmake ..

# 3. 编译项目
echo "=== 🔨 开始编译项目 (使用 $(nproc) 线程) ==="
make -j$(nproc)

echo "=== ✅ 编译完成！所有可执行文件保存在 ${BIN_DIR} 目录 ==="
echo ""

# 4. 回到顶级目录或 bin 目录并运行程序
cd "$BIN_DIR"

echo "==================================================="
echo "▶️  正在运行测试程序..."
echo "==================================================="

# 您可以根据需要修改这里运行的测试文件，以下是可选的主要程序：
# ./benchmark_coro_server
# ./benchmark_skiplist
# ./benchmark_thread_server

echo ">> 正在启动 raftCoreRun (作为示例运行)..."
echo ">> 如果需要其他测试，可修改本脚本的运行部分。"
./raftCoreRun -n 3 -f test.conf
