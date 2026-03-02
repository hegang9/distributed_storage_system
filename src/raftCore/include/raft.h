#ifndef RAFT_H
#define RAFT_H

#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "ApplyMsg.h"
#include "Persister.h"
#include "boost/any.hpp"
#include "boost/serialization/serialization.hpp"
#include "config.h"
#include "monsoon.h"
#include "raftRpcUtil.h"
#include "util.h"
// 网络状态标识：用于区分 RPC 连接是否正常（仅用于调试）
// TODO：生产环境可移除该字段
constexpr int Disconnected = 0;  // 调试时标识网络异常，避免 matchIndex[] 异常回退
constexpr int AppNormal = 1;

// 投票状态标识
constexpr int Killed = 0;  // 调试用：旧投票信息已失效
constexpr int Voted = 1;   // 当前任期已投票
constexpr int Expire = 2;  // 请求过期（任期落后或被更高任期覆盖）
constexpr int Normal = 3;  // 正常状态（未投票、未过期）

// Raft：共识协议核心实现
class Raft : public raftRpcProctoc::raftRpc {
 private:
  // 互斥锁：保护 Raft 内部状态
  std::mutex m_mtx;
  // 需要与其他raft节点进行通信，这里保存与其他节点通信的rpc入口
  std::vector<std::shared_ptr<RaftRpcUtil>> m_peers;
  // 持久化组件，负责raft数据的持久化
  std::shared_ptr<Persister> m_persister;
  // 当前节点编号
  int m_me;
  // 当前任期
  int m_currentTerm;
  // 当前任期投票给的候选人，记录当前term当前节点给谁投了票
  int m_votedFor;
  // 日志条目数组：包含命令与收到领导时的任期号
  std::vector<raftRpcProctoc::LogEntry> m_logs;

  // 这两个状态所有节点都在维护，易丢失
  // 已提交日志索引
  int m_commitIndex;
  // 已提交且已投递给上层的日志索引
  int m_lastApplied;

  // 仅 Leader 维护的易失状态
  // nextIndex：下一条发送给 follower 的日志索引
  // 下标从 1 开始（0 作为无效索引）
  std::vector<int> m_nextIndex;
  // follower 返回给 leader 已经收到了多少日志条目的索引
  std::vector<int> m_matchIndex;
  // 节点角色
  enum Status { Follower, Candidate, Leader };
  // 保存当前角色
  Status m_status;

  // client与raft交互的通道，Raft通过该通道向上层服务投递已提交的日志命令
  std::shared_ptr<LockQueue<ApplyMsg>> applyChan;  // Raft 与 KvServer 的交互通道

  // 选举超时定时器
  std::chrono::_V2::system_clock::time_point m_lastResetElectionTime;
  // 心跳超时定时器
  std::chrono::_V2::system_clock::time_point m_lastResetHearBeatTime;

  // 快照元信息（2D）
  // 快照包含的最后日志索引
  int m_lastSnapshotIncludeIndex;
  // 快照包含的最后日志任期
  int m_lastSnapshotIncludeTerm;

  // 协程调度器
  std::unique_ptr<monsoon::IOManager> m_ioManager = nullptr;

 public:
  // 日志同步＋心跳
  void AppendEntries1(const raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *reply);
  // 定期向状态机写入日志
  void applierTicker();
  // 记录某个时刻的状态
  bool CondInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex, std::string snapshot);
  // 发起选举
  void doElection();
  // Leader发起心跳
  void doHeartBeat();
  // 选举超时定时器循环
  // 周期检查是否选举计时器是否被重置，未重置则触发超时
  void electionTimeOutTicker();
  // 获取需要 apply 的日志集合
  std::vector<ApplyMsg> getApplyLogs();
  // 获取新命令的日志索引
  int getNewCommandIndex();
  // leader 获取当前日志信息
  void getPrevLogInfo(int server, int *preIndex, int *preTerm);
  // 检查当前任期当前节点是否为 leader
  void GetState(int *term, bool *isLeader);
  // 安装快照
  void InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest *args,
                       raftRpcProctoc::InstallSnapshotResponse *reply);
  // leader 心跳定时器，负责查看是否该发送心跳了，如果应该发送了，就调用 doHeartBeat() 发送心跳
  void leaderHearBeatTicker();
  // leader 发送快照给 follower
  void leaderSendSnapShot(int server);
  // leader 更新 commitIndex（已提交日志索引）
  void leaderUpdateCommitIndex();
  // 判断Leader节点日志与追随者日志是否匹配
  bool matchLog(int logIndex, int logTerm);
  // 持久化 Raft 状态
  void persist();
  // 请求投票
  void RequestVote(const raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *reply);
  // 判断节点是否有最新日志（用于投票时判断候选人日志是否落后）
  bool UpToDate(int index, int term);
  // 获取最后一条日志索引
  int getLastLogIndex();
  // 获取最后一条日志任期
  int getLastLogTerm();
  // 获取最后日志索引与任期
  void getLastLogIndexAndTerm(int *lastLogIndex, int *lastLogTerm);
  // 通过索引获取指定日志的任期
  int getLogTermFromLogIndex(int logIndex);
  // 获取持久化 Raft 状态大小
  int GetRaftStateSize();
  // 逻辑索引 -> m_logs 物理下标转换
  int getSlicesIndexFromLogIndex(int logIndex);

  // 发送 RequestVote RPC，请求其他节点给自己投票
  bool sendRequestVote(int server, std::shared_ptr<raftRpcProctoc::RequestVoteArgs> args,
                       std::shared_ptr<raftRpcProctoc::RequestVoteReply> reply, std::shared_ptr<int> votedNum);
  // 发送 AppendEntries RPC
  bool sendAppendEntries(int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,
                         std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply, std::shared_ptr<int> appendNums);

  // 推送 ApplyMsg 到 KV 层（不持锁执行）
  void pushMsgToKvServer(ApplyMsg msg);
  // 读取持久化数据
  void readPersist(std::string data);
  // 序列化 Raft 状态
  std::string persistData();

  // 提交新命令到 Raft
  void Start(Op command, int *newLogIndex, int *newLogTerm, bool *isLeader);

  // 上层服务通知 Raft 创建快照并截断日志：
  // index 表示快照已覆盖到的日志索引，snapshot 为上层传入的快照字节流
  // Raft 需丢弃 index 之前的日志并更新快照元信息
  void Snapshot(int index, std::string snapshot);

 public:
  // RPC 接口：AppendEntries
  // rpc 框架已完成序列化/反序列化，这里直接转发到本地处理逻辑
  void AppendEntries(google::protobuf::RpcController *controller, const ::raftRpcProctoc::AppendEntriesArgs *request,
                     ::raftRpcProctoc::AppendEntriesReply *response, ::google::protobuf::Closure *done) override;
  // RPC 接口：InstallSnapshot
  void InstallSnapshot(google::protobuf::RpcController *controller,
                       const ::raftRpcProctoc::InstallSnapshotRequest *request,
                       ::raftRpcProctoc::InstallSnapshotResponse *response, ::google::protobuf::Closure *done) override;
  // RPC 接口：RequestVote
  void RequestVote(google::protobuf::RpcController *controller, const ::raftRpcProctoc::RequestVoteArgs *request,
                   ::raftRpcProctoc::RequestVoteReply *response, ::google::protobuf::Closure *done) override;

 public:
  // 初始化 Raft 节点与定时任务
  void init(std::vector<std::shared_ptr<RaftRpcUtil>> peers, int me, std::shared_ptr<Persister> persister,
            std::shared_ptr<LockQueue<ApplyMsg>> applyCh);

 private:
  // 持久化序列化辅助结构
  class BoostPersistRaftNode {
   public:
    friend class boost::serialization::access;
    // 当 Archive 为输出档案时 & 等同于 <<，输入档案时 & 等同于 >>
    template <class Archive>
    void serialize(Archive &ar, const unsigned int version) {
      ar & m_currentTerm;
      ar & m_votedFor;
      ar & m_lastSnapshotIncludeIndex;
      ar & m_lastSnapshotIncludeTerm;
      ar & m_logs;
    }
    // 持久化的当前任期
    int m_currentTerm;
    // 持久化的投票对象
    int m_votedFor;
    // 快照覆盖的最后日志索引
    int m_lastSnapshotIncludeIndex;
    // 快照覆盖的最后日志任期
    int m_lastSnapshotIncludeTerm;
    // 持久化的日志条目序列（序列化后的字符串）
    std::vector<std::string> m_logs;
    // 预留字段（目前未使用）
    std::unordered_map<std::string, int> umap;

   public:
  };
};

#endif  // RAFT_H