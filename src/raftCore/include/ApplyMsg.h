#ifndef APPLYMSG_H
#define APPLYMSG_H
#include <string>

// ApplyMsg：Raft 与上层状态机（KvServer）的消息载体
// Raft 在日志提交或快照安装时，通过 applyChan 将消息推送给上层
// 消息类型分为两类：
//   1) 命令消息（CommandValid = true）：携带已提交的日志命令
//   2) 快照消息（SnapshotValid = true）：携带需要安装的快照数据
class ApplyMsg {
 public:
  // === 命令消息字段 ===
  // 是否为命令消息，true 表示已提交的日志命令
  bool CommandValid;
  // 命令内容（序列化后的字符串，通常为 Op）
  std::string Command;
  // 命令在日志中的逻辑索引
  int CommandIndex;

  // === 快照消息字段 ===
  // 是否为快照消息，true 表示需要安装快照
  bool SnapshotValid;
  // 快照数据（序列化后的状态机数据与去重表）
  std::string Snapshot;
  // 快照覆盖的最后一条日志任期
  int SnapshotTerm;
  // 快照覆盖的最后一条日志索引
  int SnapshotIndex;

 public:
  // 构造函数：valid 字段置 false，数值字段置 -1
  ApplyMsg()
      : CommandValid(false), Command(), CommandIndex(-1), SnapshotValid(false), SnapshotTerm(-1), SnapshotIndex(-1) {

        };
};

#endif  // APPLYMSG_H