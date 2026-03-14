#ifndef SKIP_LIST_ON_RAFT_CLERK_H
#define SKIP_LIST_ON_RAFT_CLERK_H
#include <arpa/inet.h>
#include <netinet/in.h>
#include <raftServerRpcUtil.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <string>
#include <vector>
#include "kvServerRPC.pb.h"
#include "mprpcconfig.h"

// Clerk: KV 存储系统的客户端
// 通过 RPC 与 KvServer 集群交互，实现 Get/Put/Append 三种操作
// 内部维护了与所有 KvServer 节点的 RPC 连接，自动寻找 leader 并在失败时重试
class Clerk {
 private:
  // 保存与所有 KvServer 节点的 RPC 连接
  // 每个元素对应一个 KvServer 节点的 RPC 通信工具
  std::vector<std::shared_ptr<raftServerRpcUtil>> m_servers;

  // 客户端唯一标识，由 Uuid() 生成
  // 用于 KvServer 端进行请求去重（保证线性一致性）
  std::string m_clientId;

  // 当前请求的自增 ID
  // 配合 m_clientId 实现请求的唯一标识，用于 KvServer 端去重
  int m_requestId;

  // 最近一次成功通信的 leader 节点 ID
  // 下次请求优先发给这个节点，避免每次都轮询
  int m_recentLeaderId;

  // 生成随机的客户端唯一标识
  // 通过拼接多个 rand() 的字符串来生成
  std::string Uuid() {
    return std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand());
  }

  // Put 和 Append 操作的统一内部实现
  // 通过 op 参数区分是 "Put" 还是 "Append" 操作
  // 会不断重试直到找到 leader 并成功提交
  void PutAppend(std::string key, std::string value, std::string op);

 public:
  // 从配置文件读取所有 KvServer 节点信息，并建立 RPC 连接
  void Init(std::string configFileName);

  // 根据 key 获取对应的 value，找不到则返回空字符串
  std::string Get(std::string key);

  // 设置 key 对应的 value（覆盖写）
  void Put(std::string key, std::string value);

  // 将 value 追加到 key 对应的原始值后面
  void Append(std::string key, std::string value);

 public:
  // 构造函数：生成唯一的 clientId，初始化 requestId 为 0，recentLeaderId 为 0
  Clerk();
};

#endif  // SKIP_LIST_ON_RAFT_CLERK_H
