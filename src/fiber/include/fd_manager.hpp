#ifndef __FD_MANAGER_H__
#define __FD_MANAGER_H__

#include <memory>
#include <vector>
#include "mutex.hpp"
#include "singleton.hpp"
#include "thread.hpp"

namespace monsoon {
// 文件句柄上下文：保存某个 fd 的状态与超时配置
// - 负责判断 fd 是否为 socket
// - 记录用户/系统层的非阻塞标记
// - 保存读写超时时间（毫秒）
// - 不直接负责 I/O，仅提供元信息管理
class FdCtx : public std::enable_shared_from_this<FdCtx> {
 public:
  typedef std::shared_ptr<FdCtx> ptr;

  // 构造时绑定 fd，并尝试初始化状态
  FdCtx(int fd);
  ~FdCtx();
  // 是否完成初始化（fstat 成功且元信息可用）
  bool isInit() const { return m_isInit; }
  // 是否为 socket 类型 fd
  bool isSocket() const { return m_isSocket; }
  // 是否已经关闭（由上层逻辑设置）
  bool isClose() const { return m_isClosed; }
  // 用户主动设置非阻塞（与系统层非阻塞区分）
  void setUserNonblock(bool v) { m_userNonblock = v; }
  // 用户是否主动设置了非阻塞
  bool getUserNonblock() const { return m_userNonblock; }
  // 设置系统层非阻塞标记（通常由 hook 层维护）
  void setSysNonblock(bool v) { m_sysNonblock = v; }
  // 获取系统层是否非阻塞
  bool getSysNonblock() const { return m_sysNonblock; }
  // 设置超时时间（type: SO_RCVTIMEO / SO_SNDTIMEO，单位毫秒）
  void setTimeout(int type, uint64_t v);
  // 获取超时时间（type: SO_RCVTIMEO / SO_SNDTIMEO，单位毫秒）
  uint64_t getTimeout(int type);

 private:
  // 初始化 fd 元信息并设置默认值
  bool init();

 private:
  /// 是否初始化成功
  bool m_isInit : 1;
  /// 是否为 socket
  bool m_isSocket : 1;
  /// 是否系统层设置为非阻塞（hook 层管理）
  bool m_sysNonblock : 1;
  /// 是否用户主动设置为非阻塞
  bool m_userNonblock : 1;
  /// 是否已经关闭（上层设置）
  bool m_isClosed : 1;
  /// 文件句柄
  int m_fd;
  /// 读超时时间（毫秒）
  uint64_t m_recvTimeout;
  /// 写超时时间（毫秒）
  uint64_t m_sendTimeout;
};
// 文件句柄管理器：维护 fd 到 FdCtx 的映射
// - 线程安全（读写锁）
// - 支持按需创建 FdCtx
class FdManager {
 public:
  typedef RWMutex RWMutexType;

  // 构造时初始化默认容量
  FdManager();
  // 获取/创建文件句柄上下文
  // - auto_create=true 时，若不存在则创建
  // - auto_create=false 时，若不存在返回 nullptr
  FdCtx::ptr get(int fd, bool auto_create = false);
  // 删除文件句柄上下文
  void del(int fd);

 private:
  /// 读写锁
  RWMutexType m_mutex;
  /// 文件句柄集合（索引即 fd）
  std::vector<FdCtx::ptr> m_datas;
};

/// 文件句柄管理器单例
typedef Singleton<FdManager> FdMgr;

}  // namespace monsoon

#endif