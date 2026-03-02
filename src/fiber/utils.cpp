#include "utils.hpp"

namespace monsoon {

// 获取当前线程的线程 ID（TID，内核级线程标识）
// 与 pthread_self() 的区别：
//   - pthread_self()：返回 POSIX 线程 ID（进程内唯一）
//   - syscall(SYS_gettid)：返回内核线程 ID（系统全局唯一）
// Linux 特有：在其他 UNIX 系统上不可用
// 用途：日志标记、性能分析、线程追踪等
pid_t GetThreadId() { return syscall(SYS_gettid); }

// 获取当前协程的协程 ID
// 当前状态：未实现（TODO），仅返回 0
// 实现需要：与协程调度器关联，从线程本地存储（TLS）或协程管理器获取当前协程
// 实现方式参考：
//   1. 使用 thread_local 存储当前协程指针
//   2. 从协程调度器的 m_fiber 成员获取
//   3. 从协程对象本身的 m_id 字段返回
u_int32_t GetFiberId() {
  // TODO: 实现与协程调度器的关联
  return 0;
}

}  // namespace monsoon