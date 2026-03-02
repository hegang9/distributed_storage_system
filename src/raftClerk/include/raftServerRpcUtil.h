//
// Created by swx on 24-1-4.
//
// raftServerRpcUtil: Clerk 端与 KvServer 之间的 RPC 通信工具类
// 封装了 protobuf 生成的 kvServerRpc_Stub，提供 Get 和 PutAppend 两个 RPC 调用接口
// 注意：这是 Clerk（客户端）端使用的，只需要 caller（调用方）功能，不需要 callee（被调用方）功能
//

#ifndef RAFTSERVERRPC_H
#define RAFTSERVERRPC_H

#include <iostream>
#include "kvServerRPC.pb.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "rpcprovider.h"

// raftServerRpcUtil: Clerk 到 KvServer 的 RPC 通信工具
// 每个实例维护一个到某个 KvServer 节点的 RPC 连接（通过 MprpcChannel）
// Clerk 端持有多个 raftServerRpcUtil 实例，分别对应集群中的每个 KvServer 节点
class raftServerRpcUtil {
 private:
  // protobuf 生成的 RPC Stub（客户端代理）
  // 通过它可以像调用本地函数一样调用远端 KvServer 的 Get/PutAppend 方法
  raftKVRpcProctoc::kvServerRpc_Stub* stub;

 public:
  // 向远端 KvServer 发起 Get RPC 调用
  // 返回 true 表示 RPC 通信成功（不代表业务成功），false 表示网络异常
  bool Get(raftKVRpcProctoc::GetArgs* GetArgs, raftKVRpcProctoc::GetReply* reply);

  // 向远端 KvServer 发起 PutAppend RPC 调用
  // 返回 true 表示 RPC 通信成功（不代表业务成功），false 表示网络异常
  bool PutAppend(raftKVRpcProctoc::PutAppendArgs* args, raftKVRpcProctoc::PutAppendReply* reply);

  // 构造函数：创建到指定 ip:port 的 RPC 连接
  raftServerRpcUtil(std::string ip, short port);

  // 析构函数：释放 stub 资源
  ~raftServerRpcUtil();
};

#endif  // RAFTSERVERRPC_H
