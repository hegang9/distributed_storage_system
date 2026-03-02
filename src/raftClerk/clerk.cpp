//
// Created by swx on 23-6-4.
//
#include "clerk.h"

#include "raftServerRpcUtil.h"

#include "util.h"

#include <string>
#include <vector>

// Get: 根据 key 从 KV 存储集群获取对应的 value
// 流程：
// 1. 生成唯一的 requestId
// 2. 向最近的 leader 发送 Get RPC
// 3. 如果失败或对方不是 leader，则轮询下一个节点重试
// 4. requestId 不变，保证重试幂等性（KvServer 层负责去重）
std::string Clerk::Get(std::string key) {
  // 每次新请求自增 requestId，保证唯一性
  m_requestId++;
  auto requestId = m_requestId;
  // 从上次成功通信的 leader 开始尝试
  int server = m_recentLeaderId;
  // 构建 Get RPC 请求参数
  raftKVRpcProctoc::GetArgs args;
  args.set_key(key);
  args.set_clientid(m_clientId);
  args.set_requestid(requestId);

  // 无限循环重试，直到成功找到 leader 并获取结果
  while (true) {
    raftKVRpcProctoc::GetReply reply;
    // 调用远端 KvServer 的 Get RPC
    bool ok = m_servers[server]->Get(&args, &reply);
    // RPC 通信失败或对方不是 leader，切换到下一个节点重试
    // requestId 不变，保证重试时 KvServer 可以通过 clientId + requestId 去重
    if (!ok || reply.err() == ErrWrongLeader) {
      server = (server + 1) % m_servers.size();
      continue;
    }
    // key 不存在，返回空字符串
    if (reply.err() == ErrNoKey) {
      return "";
    }
    // 成功获取，记住当前 leader 并返回 value
    if (reply.err() == OK) {
      m_recentLeaderId = server;
      return reply.value();
    }
  }
  return "";
}

// PutAppend: Put 和 Append 操作的统一内部实现
// 参数 op 为 "Put" 或 "Append"，用于区分操作类型
// 流程与 Get 类似：构建请求 -> 发送 RPC -> 失败则轮询重试
void Clerk::PutAppend(std::string key, std::string value, std::string op) {
  // 每次新请求自增 requestId
  m_requestId++;
  auto requestId = m_requestId;
  // 从上次成功通信的 leader 开始尝试
  auto server = m_recentLeaderId;
  // 无限循环重试，直到找到 leader 并成功提交
  while (true) {
    // 构建 PutAppend RPC 请求参数
    raftKVRpcProctoc::PutAppendArgs args;
    args.set_key(key);
    args.set_value(value);
    args.set_op(op);  // "Put" 或 "Append"
    args.set_clientid(m_clientId);
    args.set_requestid(requestId);
    raftKVRpcProctoc::PutAppendReply reply;
    // 调用远端 KvServer 的 PutAppend RPC
    bool ok = m_servers[server]->PutAppend(&args, &reply);
    // RPC 通信失败或对方不是 leader，切换到下一个节点重试
    if (!ok || reply.err() == ErrWrongLeader) {
      DPrintf("【Clerk::PutAppend】原以为的leader：{%d}请求失败，向新leader{%d}重试  ，操作：{%s}", server, server + 1,
              op.c_str());
      if (!ok) {
        DPrintf("重试原因 ，rpc失敗 ，");
      }
      if (reply.err() == ErrWrongLeader) {
        DPrintf("重試原因：非leader");
      }
      // 轮询下一个节点
      server = (server + 1) % m_servers.size();
      continue;
    }
    // 成功提交，记住当前 leader 并返回
    if (reply.err() == OK) {
      m_recentLeaderId = server;
      return;
    }
  }
}

// Put: 对外暴露的接口，将 key 对应的 value 设置为指定值（覆盖写）
void Clerk::Put(std::string key, std::string value) { PutAppend(key, value, "Put"); }

// Append: 对外暴露的接口，将 value 追加到 key 对应的原始值后面
void Clerk::Append(std::string key, std::string value) { PutAppend(key, value, "Append"); }

// Init: 初始化客户端，从配置文件读取所有 KvServer 节点的 ip:port 并建立 RPC 连接
// 配置文件格式：node0ip=xxx, node0port=xxx, node1ip=xxx, node1port=xxx, ...
void Clerk::Init(std::string configFileName) {
  // 读取配置文件
  MprpcConfig config;
  config.LoadConfigFile(configFileName.c_str());
  // 解析所有节点的 ip 和 port
  std::vector<std::pair<std::string, short>> ipPortVt;
  for (int i = 0; i < INT_MAX - 1; ++i) {
    std::string node = "node" + std::to_string(i);

    std::string nodeIp = config.Load(node + "ip");
    std::string nodePortStr = config.Load(node + "port");
    // ip 为空说明没有更多节点了，退出循环
    if (nodeIp.empty()) {
      break;
    }
    ipPortVt.emplace_back(nodeIp, atoi(nodePortStr.c_str()));
  }
  // 为每个节点创建 RPC 连接
  for (const auto& item : ipPortVt) {
    std::string ip = item.first;
    short port = item.second;
    // 创建 RPC 通信工具并加入列表
    auto* rpc = new raftServerRpcUtil(ip, port);
    m_servers.push_back(std::shared_ptr<raftServerRpcUtil>(rpc));
  }
}

// 构造函数：初始化唯一的 clientId、requestId 为 0、默认的 leader 为节点 0
Clerk::Clerk() : m_clientId(Uuid()), m_requestId(0), m_recentLeaderId(0) {}
