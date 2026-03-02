#include "fd_manager.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "hook.hpp"

namespace monsoon {

// 构造时初始化状态并尝试探测 fd 类型
FdCtx::FdCtx(int fd)
    : m_isInit(false),
      m_isSocket(false),
      m_sysNonblock(false),
      m_userNonblock(false),
      m_isClosed(false),
      m_fd(fd),
      m_recvTimeout(-1),
      m_sendTimeout(-1) {
  init();
}

// 析构：资源由系统 fd 管理，FdCtx 本身不关闭 fd
FdCtx::~FdCtx() {}

// 初始化 fd 元信息：类型识别 + socket 非阻塞设置
bool FdCtx::init() {
  if (m_isInit) {
    return true;
  }
  m_recvTimeout = -1;
  m_sendTimeout = -1;

  // 获取文件状态信息
  struct stat fd_stat;
  if (-1 == fstat(m_fd, &fd_stat)) {
    m_isInit = false;
    m_isSocket = false;
  } else {
    m_isInit = true;
    // 判断是否是 socket
    m_isSocket = S_ISSOCK(fd_stat.st_mode);
  }

  // 对 socket 设置系统层非阻塞（仅当其为 socket）
  if (m_isSocket) {
    int flags = fcntl_f(m_fd, F_GETFL, 0);
    if (!(flags & O_NONBLOCK)) {
      fcntl_f(m_fd, F_SETFL, flags | O_NONBLOCK);
    }
    m_sysNonblock = true;
  } else {
    m_sysNonblock = false;
  }

  m_userNonblock = false;
  m_isClosed = false;
  return m_isInit;
}

// 设置读/写超时时间（单位毫秒）
void FdCtx::setTimeout(int type, uint64_t v) {
  if (type == SO_RCVTIMEO) {
    m_recvTimeout = v;
  } else {
    m_sendTimeout = v;
  }
}

// 获取读/写超时时间（单位毫秒）
uint64_t FdCtx::getTimeout(int type) {
  if (type == SO_RCVTIMEO) {
    return m_recvTimeout;
  } else {
    return m_sendTimeout;
  }
}

// 初始化默认容量，避免频繁扩容
FdManager::FdManager() { m_datas.resize(64); }

// 获取或创建 fd 上下文
// - 读锁尝试快速命中
// - 必要时升级到写锁进行创建或扩容
FdCtx::ptr FdManager::get(int fd, bool auto_create) {
  if (fd == -1) {
    return nullptr;
  }
  RWMutexType::ReadLock lock(m_mutex);
  if ((int)m_datas.size() <= fd) {
    if (auto_create == false) {
      return nullptr;
    }
  } else {
    if (m_datas[fd] || !auto_create) {
      return m_datas[fd];
    }
  }
  lock.unlock();

  // 需要创建：加写锁并确保容量足够
  RWMutexType::WriteLock lock2(m_mutex);
  FdCtx::ptr ctx(new FdCtx(fd));
  if (fd >= (int)m_datas.size()) {
    m_datas.resize(fd * 1.5);
  }
  m_datas[fd] = ctx;
  return ctx;
}

// 删除 fd 上下文（不关闭底层 fd，因为底层fd由系统管理）
void FdManager::del(int fd) {
  RWMutexType::WriteLock lock(m_mutex);
  if ((int)m_datas.size() <= fd) {
    return;
  }
  m_datas[fd].reset();
}
}  // namespace monsoon