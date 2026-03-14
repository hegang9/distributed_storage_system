// RaftRpcUtil：Raft 节点间 RPC 通信封装
// 封装 protobuf 生成的 raftRpc_Stub，提供三个核心 RPC：
//   1) AppendEntries：日志复制 / 心跳
//   2) RequestVote：选举拉票
//   3) InstallSnapshot：快照安装
// 每个 Raft 节点维护 N-1 个 RaftRpcUtil 实例（对应集群中的其他节点）
//

#ifndef RAFTRPC_H
#define RAFTRPC_H

#include "raftRPC.pb.h"

// RaftRpcUtil：Raft 节点间 RPC 发送端
// 每个实例维护一条到目标 Raft 节点的 RPC 连接（通过 MprpcChannel）
class RaftRpcUtil {
 private:
  // protobuf 生成的 RPC Stub（客户端代理）
  // 用于远程调用 AppendEntries / RequestVote / InstallSnapshot
  raftRpcProctoc::raftRpc_Stub *stub_;

 public:
  // 发送日志复制/心跳（Leader 侧调用）
  // 返回 true 表示网络通信成功，false 表示网络异常
  bool AppendEntries(raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *response);

  // 发送快照安装（Leader 侧调用，当 Follower 日志严重落后时使用）
  // 返回 true 表示网络通信成功，false 表示网络异常
  bool InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *args, raftRpcProctoc::InstallSnapshotResponse *response);

  // 发送选举拉票（Candidate 侧调用）
  // 返回 true 表示网络通信成功，false 表示网络异常
  bool RequestVote(raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *response);

  // 构造函数：创建到目标 Raft 节点的 RPC 连接
  // @param ip   目标 Raft 节点 IP
  // @param port 目标 Raft 节点端口
  RaftRpcUtil(std::string ip, short port);

  // 析构函数：释放 RPC Stub 资源
  ~RaftRpcUtil();
};

#endif  // RAFTRPC_H
