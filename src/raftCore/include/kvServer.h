//
// Created by swx on 23-6-1.
//

#ifndef SKIP_LIST_ON_RAFT_KVSERVER_H
#define SKIP_LIST_ON_RAFT_KVSERVER_H

#include <boost/any.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/foreach.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include "kvServerRPC.pb.h"
#include "raft.h"
#include "skipList.h"

// KvServer：KV 存储服务端，负责处理客户端请求并与 Raft 协作完成一致性提交
class KvServer : raftKVRpcProctoc::kvServerRpc {
 private:
  // 互斥锁：保护 KV 状态与等待队列
  std::mutex m_mtx;
  // 当前节点编号
  int m_me;
  // Raft 节点实例
  std::shared_ptr<Raft> m_raftNode;
  // Raft -> KV 的消息通道（ApplyMsg）
  std::shared_ptr<LockQueue<ApplyMsg> > applyChan;  // kvServer和raft节点的通信管道
  // Raft 状态大小阈值，超过则触发快照
  int m_maxRaftState;  // snapshot if log grows this big

  // Your definitions here.
  // 临时序列化缓存（用于快照序列化/反序列化）
  std::string m_serializedKVData;  // todo ： 序列化后的kv数据，理论上可以不用，但是目前没有找到特别好的替代方法
  // SkipList 作为 KV 底层存储结构
  SkipList<std::string, std::string> m_skipList;
  // 备用的 KV Map（当前实现主要使用 SkipList）
  std::unordered_map<std::string, std::string> m_kvDB;

  // 等待 Raft 提交结果的临时通道：raftIndex -> Op 队列
  std::unordered_map<int, LockQueue<Op> *> waitApplyCh;
  // index(raft) -> chan  //？？？字段含义   waitApplyCh是一个map，键是int，值是Op类型的管道

  // 记录每个客户端已处理的最大请求号，用于去重
  std::unordered_map<std::string, int> m_lastRequestId;  // clientid -> requestID  //一个kV服务器可能连接多个client

  // 已安装快照对应的 Raft 日志索引
  int m_lastSnapShotRaftLogIndex;

 public:
  KvServer() = delete;

  // 构造函数：初始化 KVServer、启动 RPC 与 Raft 节点
  KvServer(int me, int maxraftstate, std::string nodeInforFileName, short port);

  // 启动 KVServer（预留接口）
  void StartKVServer();

  // 打印当前 KV 数据（调试用）
  void DprintfKVDB();

  // 执行 Append 操作并更新去重信息
  void ExecuteAppendOpOnKVDB(Op op);

  // 执行 Get 操作，返回 value 与存在标记
  void ExecuteGetOpOnKVDB(Op op, std::string *value, bool *exist);

  // 执行 Put 操作并更新去重信息
  void ExecutePutOpOnKVDB(Op op);

  // 处理客户端 Get 请求（本地逻辑）
  void Get(const raftKVRpcProctoc::GetArgs *args,
           raftKVRpcProctoc::GetReply
               *reply);  // 将 GetArgs 改为rpc调用的，因为是远程客户端，即服务器宕机对客户端来说是无感的
  /**
   * 从 raft 节点接收已提交消息（不是 GET 命令执行）
   * @param message 已提交的 ApplyMsg
   */
  // 从 Raft applyChan 接收已提交命令并执行
  void GetCommandFromRaft(ApplyMsg message);

  // 判断请求是否重复（基于 clientId + requestId）
  bool ifRequestDuplicate(std::string ClientId, int RequestId);

  // 处理客户端 Put/Append 请求（本地逻辑）
  void PutAppend(const raftKVRpcProctoc::PutAppendArgs *args, raftKVRpcProctoc::PutAppendReply *reply);

  // 循环读取 Raft 的 ApplyMsg 并分发处理
  void ReadRaftApplyCommandLoop();

  // 安装快照到本地状态机（KV 数据 + 去重表）
  void ReadSnapShotToInstall(std::string snapshot);

  // 将已提交 Op 通知到等待队列
  bool SendMessageToWaitChan(const Op &op, int raftIndex);

  // 检查 Raft 状态大小，必要时触发快照
  void IfNeedToSendSnapShotCommand(int raftIndex, int proportion);

  // 处理来自 Raft 的快照消息
  void GetSnapShotFromRaft(ApplyMsg message);

  // 生成当前 KV 状态快照字符串
  std::string MakeSnapShot();

 public:  // for rpc
  // RPC 接口：Put/Append
  void PutAppend(google::protobuf::RpcController *controller, const ::raftKVRpcProctoc::PutAppendArgs *request,
                 ::raftKVRpcProctoc::PutAppendReply *response, ::google::protobuf::Closure *done) override;

  // RPC 接口：Get
  void Get(google::protobuf::RpcController *controller, const ::raftKVRpcProctoc::GetArgs *request,
           ::raftKVRpcProctoc::GetReply *response, ::google::protobuf::Closure *done) override;

  ///////////////// serialization start ///////////////////////////////
  // Boost 序列化入口
 private:
  friend class boost::serialization::access;

  // 当 Archive 为输出档案时 & 等同于 <<，输入档案时 & 等同于 >>
  template <class Archive>
  void serialize(Archive &ar, const unsigned int version)  // 这里面写需要序列话和反序列化的字段
  {
    ar & m_serializedKVData;

    // ar & m_kvDB;
    ar & m_lastRequestId;
  }

  // 序列化本地状态为快照字符串
  std::string getSnapshotData() {
    m_serializedKVData = m_skipList.dump_file();
    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);
    oa << *this;
    m_serializedKVData.clear();
    return ss.str();
  }

  // 从快照字符串反序列化恢复本地状态
  void parseFromString(const std::string &str) {
    std::stringstream ss(str);
    boost::archive::text_iarchive ia(ss);
    ia >> *this;
    m_skipList.load_file(m_serializedKVData);
    m_serializedKVData.clear();
  }

  ///////////////// serialization end ///////////////////////////////
};

#endif  // SKIP_LIST_ON_RAFT_KVSERVER_H
