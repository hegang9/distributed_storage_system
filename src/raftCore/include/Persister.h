// Persister：Raft 状态持久化组件
// 负责将 Raft 的核心状态（currentTerm、votedFor、logs、快照元信息）和快照数据落盘
// 保障节点崩溃或重启后可恢复一致性状态
// 每个 Raft 节点对应一个独立 Persister 实例
//

#ifndef SKIP_LIST_ON_RAFT_PERSISTER_H
#define SKIP_LIST_ON_RAFT_PERSISTER_H
#include <fstream>
#include <mutex>

// Persister：Raft 持久化实现
// 通过文件系统保存两类数据：
//   1. raftState：Raft 的核心状态（currentTerm、votedFor、logs、快照元信息）
//   2. snapshot：KvServer 生成的快照数据（KVDB 状态 + 去重表）
class Persister {
 private:
  std::mutex m_mtx;         // 保护内部成员的互斥锁
  std::string m_raftState;  // 内存缓存的 raftState 数据
  std::string m_snapshot;   // 内存缓存的 snapshot 数据

  // raftState 持久化文件名，格式为 "raftstatePersist{nodeId}.txt"
  const std::string m_raftStateFileName;
  // snapshot 持久化文件名，格式为 "snapshotPersist{nodeId}.txt"
  const std::string m_snapshotFileName;
  // raftState 文件输出流
  std::ofstream m_raftStateOutStream;
  // snapshot 文件输出流
  std::ofstream m_snapshotOutStream;
  // 缓存的 raftState 大小，避免频繁读取文件
  // 用于判断是否触发快照压缩
  long long m_raftStateSize;

 public:
  // 同时保存 raftState 与 snapshot（写入前清空旧内容）
  void Save(std::string raftstate, std::string snapshot);

  // 读取 snapshot 数据
  std::string ReadSnapshot();

  // 仅保存 raftState（不涉及 snapshot）
  void SaveRaftState(const std::string& data);

  // 获取当前 raftState 大小（字节数）
  long long RaftStateSize();

  // 读取 raftState 数据
  std::string ReadRaftState();

  // 构造函数：根据节点 ID 初始化文件名并创建/清空持久化文件
  explicit Persister(int me);

  // 析构函数：关闭文件流
  ~Persister();

 private:
  // 清空 raftState 文件内容并重置大小计数
  void clearRaftState();
  // 清空 snapshot 文件内容
  void clearSnapshot();
  // 同时清空 raftState 与 snapshot 文件
  void clearRaftStateAndSnapshot();
};

#endif  // SKIP_LIST_ON_RAFT_PERSISTER_H
