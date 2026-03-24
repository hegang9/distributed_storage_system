#include "Persister.h"
#include "util.h"

// todo:会涉及反复打开文件的操作，没有考虑如果文件出现问题会怎么办？？
// 保存 raftState 与 snapshot（原子覆盖写）
void Persister::Save(const std::string raftstate, const std::string snapshot) {
  std::lock_guard<std::mutex> lg(m_mtx);
  clearRaftStateAndSnapshot();
  // 将raftstate和snapshot写入本地文件
  m_raftStateOutStream << raftstate;
  m_snapshotOutStream << snapshot;

  // 必须显式flush，否则数据只停留在C++运行时缓冲区，进程崩溃会导致Raft状态丢失
  m_raftStateOutStream.flush();
  m_snapshotOutStream.flush();
}

// 读取 snapshot 数据
std::string Persister::ReadSnapshot() {
  std::lock_guard<std::mutex> lg(m_mtx);
  if (m_snapshotOutStream.is_open()) {
    m_snapshotOutStream.close();
  }

  DEFER {
    m_snapshotOutStream.open(m_snapshotFileName);  // 默认是追加
  };
  std::fstream ifs(m_snapshotFileName, std::ios_base::in);
  if (!ifs.good()) {
    return "";
  }
  std::string snapshot;
  ifs >> snapshot;
  ifs.close();
  return snapshot;
}

// 仅保存 raftState
void Persister::SaveRaftState(const std::string &data) {
  std::lock_guard<std::mutex> lg(m_mtx);
  // 将raftstate和snapshot写入本地文件
  clearRaftState();
  m_raftStateOutStream << data;
  m_raftStateOutStream.flush();  // 强制刷盘，保证严格的预写式日志(WAL)语义
  m_raftStateSize += data.size();
}

// 获取当前 raftState 大小
long long Persister::RaftStateSize() {
  std::lock_guard<std::mutex> lg(m_mtx);

  return m_raftStateSize;
}

// 读取 raftState 数据
std::string Persister::ReadRaftState() {
  std::lock_guard<std::mutex> lg(m_mtx);

  std::fstream ifs(m_raftStateFileName, std::ios_base::in);
  if (!ifs.good()) {
    return "";
  }
  std::string snapshot;
  ifs >> snapshot;
  ifs.close();
  return snapshot;
}

// 构造函数：初始化持久化文件并绑定输出流
Persister::Persister(const int me)
    : m_raftStateFileName("raftstatePersist" + std::to_string(me) + ".txt"),
      m_snapshotFileName("snapshotPersist" + std::to_string(me) + ".txt"),
      m_raftStateSize(0) {
  /**
   * 检查文件状态并清空文件
   */
  bool fileOpenFlag = true;
  std::fstream file(m_raftStateFileName, std::ios::out | std::ios::trunc);
  if (file.is_open()) {
    file.close();
  } else {
    fileOpenFlag = false;
  }
  file = std::fstream(m_snapshotFileName, std::ios::out | std::ios::trunc);
  if (file.is_open()) {
    file.close();
  } else {
    fileOpenFlag = false;
  }
  if (!fileOpenFlag) {
    DPrintf("[func-Persister::Persister] file open error");
  }
  /**
   * 绑定流
   */
  m_raftStateOutStream.open(m_raftStateFileName);
  m_snapshotOutStream.open(m_snapshotFileName);
}

// 析构函数：关闭打开的文件流
Persister::~Persister() {
  if (m_raftStateOutStream.is_open()) {
    m_raftStateOutStream.close();
  }
  if (m_snapshotOutStream.is_open()) {
    m_snapshotOutStream.close();
  }
}

// 清空 raftState 文件并重置大小
void Persister::clearRaftState() {
  m_raftStateSize = 0;
  // 关闭文件流
  if (m_raftStateOutStream.is_open()) {
    m_raftStateOutStream.close();
  }
  // 重新打开文件流并清空文件内容
  m_raftStateOutStream.open(m_raftStateFileName, std::ios::out | std::ios::trunc);
}

// 清空 snapshot 文件
void Persister::clearSnapshot() {
  if (m_snapshotOutStream.is_open()) {
    m_snapshotOutStream.close();
  }
  m_snapshotOutStream.open(m_snapshotFileName, std::ios::out | std::ios::trunc);
}

// 同时清空 raftState 与 snapshot
void Persister::clearRaftStateAndSnapshot() {
  clearRaftState();
  clearSnapshot();
}
