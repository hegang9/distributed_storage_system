#include "raftRpcUtil.h"

#include <mprpcchannel.h>
#include <mprpccontroller.h>

// 发送 AppendEntries RPC（心跳/日志复制）
bool RaftRpcUtil::AppendEntries(raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *response) {
  MprpcController controller;
  stub_->AppendEntries(&controller, args, response, nullptr);
  return !controller.Failed();
}

// 发送 InstallSnapshot RPC（快照安装）
bool RaftRpcUtil::InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *args,
                                  raftRpcProctoc::InstallSnapshotResponse *response) {
  MprpcController controller;
  stub_->InstallSnapshot(&controller, args, response, nullptr);
  return !controller.Failed();
}

// 发送 RequestVote RPC（选举投票）
bool RaftRpcUtil::RequestVote(raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *response) {
  MprpcController controller;
  stub_->RequestVote(&controller, args, response, nullptr);
  return !controller.Failed();
}

// 先开启服务器，再尝试连接其他的节点，中间给一个间隔时间，等待其他的rpc服务器节点启动

// 构造函数：创建到目标 Raft 节点的 RPC 通道
RaftRpcUtil::RaftRpcUtil(std::string ip, short port) {
  //*********************************************  */
  // 发送rpc设置
  stub_ = new raftRpcProctoc::raftRpc_Stub(new MprpcChannel(ip, port, true));
}

// 析构函数：释放 stub 资源
RaftRpcUtil::~RaftRpcUtil() { delete stub_; }
