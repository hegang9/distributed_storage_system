#include "hook.hpp"
#include <dlfcn.h>
#include <cstdarg>
#include <string>
#include "fd_manager.hpp"
#include "fiber.hpp"
#include "iomanager.hpp"
namespace monsoon {
// 当前线程是否启用hook开关
static thread_local bool t_hook_enable = false;
// TCP连接的默认超时时间，初始化为5000毫秒
static int g_tcp_connect_timeout = 5000;

// 定义需要进行hook拦截的系统和C库函数宏列表
#define HOOK_FUN(XX) \
  XX(sleep)          \
  XX(usleep)         \
  XX(nanosleep)      \
  XX(socket)         \
  XX(connect)        \
  XX(accept)         \
  XX(read)           \
  XX(readv)          \
  XX(recv)           \
  XX(recvfrom)       \
  XX(recvmsg)        \
  XX(write)          \
  XX(writev)         \
  XX(send)           \
  XX(sendto)         \
  XX(sendmsg)        \
  XX(close)          \
  XX(fcntl)          \
  XX(ioctl)          \
  XX(getsockopt)     \
  XX(setsockopt)

// 初始化hook，从系统库中提取原生的函数指针
void hook_init() {
  static bool is_inited = false;
  if (is_inited) {
    return;
  }
  // 使用dlsym和RTLD_NEXT在动态链接库中寻找下一个同名函数的实际地址（即原生系统调用）
#define XX(name) name##_f = (name##_fun)dlsym(RTLD_NEXT, #name);
  HOOK_FUN(XX);
#undef XX
}

// 默认超时时间配置变量
static uint64_t s_connect_timeout = -1;

// 静态初始化结构体，利用其构造函数在main函数执行前自动完成hook_init
struct _HOOKIniter {
  _HOOKIniter() {
    hook_init();
    s_connect_timeout = g_tcp_connect_timeout;
  }
};
// 声明静态全局变量，触发它的构造从而执行初始化
static _HOOKIniter s_hook_initer;

// 返回当前线程是否开启了hook
bool is_hook_enable() { return t_hook_enable; }

// 设置当前线程的hook开关
void set_hook_enable(const bool flag) { t_hook_enable = flag; }

// 辅助结构，用于在定时器回调中传递超时信息或取消状态
struct timer_info {
  int cnacelled = 0;
};

// 核心模板函数：所有的IO操作读/写等，都会通过此函数进行统一封装处理以实现异步调度
template <typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char *hook_fun_name, uint32_t event, int timeout_so, Args &&...args) {
  // 如果当前线程未开启hook，直接以同步方式调用原生API
  if (!t_hook_enable) {
    return fun(fd, std::forward<Args>(args)...);
  }
  // 从文件描述符管理器中获取该fd的上下文对象
  FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
  if (!ctx) {
    return fun(fd, std::forward<Args>(args)...);
  }
  // 检查文件已经关闭
  if (ctx->isClose()) {
    errno = EBADF;
    return -1;
  }

  // 不是套接字，或者用户强行要求为非阻塞模式（用户已自己处理），也走原生调用
  if (!ctx->isSocket() || ctx->getUserNonblock()) {
    return fun(fd, std::forward<Args>(args)...);
  }
  // 提取对应事件类型的超时时间
  uint64_t to = ctx->getTimeout(timeout_so);
  std::shared_ptr<timer_info> tinfo(new timer_info);

retry:
  // 首先尝试用原生API读写一遍
  ssize_t n = fun(fd, std::forward<Args>(args)...);
  // 如果被内核中断信号打断（EINTR），则重复操作
  while (n == -1 && errno == EINTR) {
    n = fun(fd, std::forward<Args>(args)...);
  }
  // 重点：返回EAGAIN表示当前套接字缓冲区无数据可读/空间可写（底层已被设置成非阻塞）
  if (n == -1 && errno == EAGAIN) {
    IOManager *iom = IOManager::GetThis();
    Timer::ptr timer;
    std::weak_ptr<timer_info> winfo(tinfo);

    // 如果设置了超时，为其添加一个条件定时器去应对长久无数据的情况
    if (to != (uint64_t)-1) {
      timer = iom->addConditionTimer(
          to,
          [winfo, fd, iom, event]() {
            auto t = winfo.lock();
            // 定时器触发时，检查标志位；如果已经被主流程置位说明请求完成，这里退出
            if (!t || t->cnacelled) {
              return;
            }
            // 发生真正的超时，修改标志位并主动触发取消这个Epoll事件
            t->cnacelled = ETIMEDOUT;
            iom->cancelEvent(fd, (Event)(event));
          },
          winfo);
    }

    // 核心步骤：向IOManager注册当前协程去等待该文件描述符上的读或写事件
    int rt = iom->addEvent(fd, (Event)(event));
    if (rt) {
      // 注册失败，通常是因为内核级错误
      std::cout << hook_fun_name << " addEvent(" << fd << ", " << event << ")";
      if (timer) {
        timer->cancel();
      }
      return -1;
    } else {
      // 注册成功，当前协程主动挂起，让出CPU以执行其他就绪任务！
      Fiber::GetThis()->yield();

      // --挂起在这里-- 当网卡收到数据，通过Epoll唤醒后，协程从这里醒来继续向下执行

      // 醒来后，先取消原本设定的监控超时定时器
      if (timer) {
        timer->cancel();
      }
      // 检查唤醒的原因是否是超时（由上面定时器设置的）
      if (tinfo->cnacelled) {
        errno = tinfo->cnacelled;
        return -1;
      }
      // 如果不是由于超时而醒来，证明是真的IO就绪了，goto重新去用原生API获取数据
      goto retry;
    }
  }

  return n;
}

extern "C" {
// 宏：定义一个函数指针变量用来保存原生的系统调用地址（如 sleep_f, read_f）
#define XX(name) name##_fun name##_f = nullptr;
HOOK_FUN(XX);
#undef XX

/**
 * \brief 重写 sleep 函数
 * \param seconds 睡眠的秒数
 * \return 0
 */
unsigned int sleep(unsigned int seconds) {
  // std::cout << "HOOK SLEEP" << std::endl;
  if (!t_hook_enable) {
    // 不允许hook,则直接使用同步的系统调用阻塞物理线程
    return sleep_f(seconds);
  }
  // 允许hook,则直接让当前协程退出运行态，seconds秒后再由定时器在IOManager中调度重启
  Fiber::ptr fiber = Fiber::GetThis();
  IOManager *iom = IOManager::GetThis();
  // 添加一个定时任务：时间到后，把当前协程重新放入调度队列去被别的线程消费唤醒
  iom->addTimer(seconds * 1000,
                std::bind((void (Scheduler::*)(Fiber::ptr, int thread))&IOManager::scheduler, iom, fiber, -1));
  // 当前协程让出CPU
  Fiber::GetThis()->yield();
  return 0;
}
// usleep 在指定的微妙数内暂停线程运行
int usleep(useconds_t usec) {
  // std::cout << "HOOK USLEEP START" << std::endl;
  if (!t_hook_enable) {
    // 不允许hook,则直接使用系统调用
    // std::cout << "THIS THREAD NOT ALLOW HOOK" << std::endl;
    auto ret = usleep_f(usec);
    // std::cout << "THIS THREAD WAKE UP" << std::endl;
    return 0;
  }
  // std::cout << "HOOK USLEEP REAL START" << std::endl;
  // 允许hook,则不阻塞线程，只挂起当前协程
  Fiber::ptr fiber = Fiber::GetThis();
  IOManager *iom = IOManager::GetThis();
  iom->addTimer(usec / 1000,
                std::bind((void (Scheduler::*)(Fiber::ptr, int thread))&IOManager::scheduler, iom, fiber, -1));
  Fiber::GetThis()->yield();
  return 0;
}
// nanosleep 在指定的纳秒数内暂停当前线程的执行
int nanosleep(const struct timespec *req, struct timespec *rem) {
  if (!t_hook_enable) {
    // 不允许hook,则直接使用系统调用
    return nanosleep_f(req, rem);
  }
  // 允许hook,则将系统阻塞转换成超时协程调度
  Fiber::ptr fiber = Fiber::GetThis();
  IOManager *iom = IOManager::GetThis();
  int timeout_s = req->tv_sec * 1000 + req->tv_nsec / 1000 / 1000;
  iom->addTimer(timeout_s,
                std::bind((void (Scheduler::*)(Fiber::ptr, int thread))&IOManager::scheduler, iom, fiber, -1));
  Fiber::GetThis()->yield();
  return 0;
}

// 重写原生的 socket 系统调用
int socket(int domain, int type, int protocol) {
  // std::cout << "HOOK SOCKET" << std::endl;
  if (!t_hook_enable) {
    return socket_f(domain, type, protocol);
  }
  int fd = socket_f(domain, type, protocol);
  if (fd == -1) {
    return fd;
  }
  // 如果开启了hook，要把刚创建的fd加入FdManager统一管理上下文，并将其底层设置为非阻塞（O_NONBLOCK）
  FdMgr::GetInstance()->get(fd, true);
  return fd;
}

// 自定义的携带超时参数的 connect 封装
int connect_with_timeout(int fd, const struct sockaddr *addr, socklen_t addrlen, uint64_t timeout_ms) {
  // std::cout << "HOOK CONNECT_WITH_TIMEOUT" << std::endl;
  if (!t_hook_enable) {
    return connect_f(fd, addr, addrlen);
  }
  FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
  if (!ctx || ctx->isClose()) {
    errno = EBADF;
    return -1;
  }

  // 只有非用户明确指定的套接字才进行拦截重写
  if (!ctx->isSocket()) {
    return connect_f(fd, addr, addrlen);
  }

  // fd是否被显示设置为非阻塞模式(代表用户想自己去处理重试而不是让框架协程接管)
  if (ctx->getUserNonblock()) {
    return connect_f(fd, addr, addrlen);
  }

  // 开始框架自己接管：调用底层的connect（此时fd因为前面的socket封装，底层必然是非阻塞的）
  int n = connect_f(fd, addr, addrlen);
  // 连接直接成功了！
  if (n == 0) {
    return 0;
  } else if (n != -1 || errno != EINPROGRESS) {
    // 发生了除 EINPROGRESS 外的其他严重连接错误
    return n;
  }

  // 重点：返回 EINPROGRESS，代表TCP三次握手正在进行中，但尚未完成
  // 此时不该阻塞当前物理线程干等，而是要去Epoll挂监听然后让出协程！
  IOManager *iom = IOManager::GetThis();
  Timer::ptr timer;
  std::shared_ptr<timer_info> tinfo(new timer_info);
  std::weak_ptr<timer_info> winfo(tinfo);

  // 如果需要等待，先保证配置一个防卡死的超时定时器
  if (timeout_ms != (uint64_t)-1) {
    // 添加条件定时器
    timer = iom->addConditionTimer(
        timeout_ms,
        [winfo, fd, iom]() {
          auto t = winfo.lock();
          if (!t || t->cnacelled) {
            return;
          }
          // 定时时间到达，握手一直没成功，设置超时标志位
          t->cnacelled = ETIMEDOUT;
          // 主动取消WRITE事件，不再等待握手完成了
          iom->cancelEvent(fd, WRITE);
        },
        winfo);
  }

  // 连接建立成功在Epoll里会被当成当前 fd 的可写事件（WRITE）触发！
  int rt = iom->addEvent(fd, WRITE);
  if (rt == 0) {
    // 监听注册成功，当前发出connect动作的协程立刻陷入挂起！
    Fiber::GetThis()->yield();
    // ---------------------- 挂起分割线 ----------------------
    // 等待握手完成触发写入事件或者超时触发取消...本协程被唤醒回到这里
    if (timer) {
      timer->cancel();
    }
    // 检查如果是因为超时醒来，报错抛出
    if (tinfo->cnacelled) {
      errno = tinfo->cnacelled;
      return -1;
    }
  } else {
    // addevennt error 注册epoll事件失败
    if (timer) {
      timer->cancel();
    }
    std::cout << "connect addEvent(" << fd << ", WRITE) error" << std::endl;
  }

  // 如果走到了这里说明被Epoll正常以WRITE事件唤醒了（可能是连上了，也可能是握手被拒了）
  int error = 0;
  socklen_t len = sizeof(int);
  // 必须通过 getsockopt 获取套接字最新的内部真实挂起状态
  if (-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) {
    return -1;
  }
  if (!error) {
    return 0;  // errno为0代表真是连接成功了
  } else {
    errno = error;
    return -1;
  }
}

// 拦截系统的 connect 调用并委托给我们的自定义 connect_with_timeout 函数
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  return monsoon::connect_with_timeout(sockfd, addr, addrlen, s_connect_timeout);
}

// 拦截 accept 调用：服务端接受新连接
int accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
  // 把真正的accept通过多路分发大管家do_io保护起来！没链接来时协程就让出CPU休息
  int fd = do_io(s, accept_f, "accept", READ, SO_RCVTIMEO, addr, addrlen);
  if (fd >= 0) {
    // 连上了后将新的连接由框架进行套接字属性接管
    FdMgr::GetInstance()->get(fd, true);
  }
  return fd;
}

// ------------------------
// 下方全都是极其统一且干净的数据网络收发 Hook 重写：
// 用宏定义的真名与宏事件标识（READ/WRITE）通过 do_io 接管
// ------------------------

ssize_t read(int fd, void *buf, size_t count) { return do_io(fd, read_f, "read", READ, SO_RCVTIMEO, buf, count); }

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
  return do_io(fd, readv_f, "readv", READ, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
  return do_io(sockfd, recv_f, "recv", READ, SO_RCVTIMEO, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
  return do_io(sockfd, recvfrom_f, "recvfrom", READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags) {
  return do_io(sockfd, recvmsg_f, "recvmsg", READ, SO_RCVTIMEO, msg, flags);
}

ssize_t write(int fd, const void *buf, size_t count) {
  return do_io(fd, write_f, "write", WRITE, SO_SNDTIMEO, buf, count);
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
  return do_io(fd, writev_f, "writev", WRITE, SO_SNDTIMEO, iov, iovcnt);
}

ssize_t send(int s, const void *msg, size_t len, int flags) {
  return do_io(s, send_f, "send", WRITE, SO_SNDTIMEO, msg, len, flags);
}

ssize_t sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
  return do_io(s, sendto_f, "sendto", WRITE, SO_SNDTIMEO, msg, len, flags, to, tolen);
}

ssize_t sendmsg(int s, const struct msghdr *msg, int flags) {
  return do_io(s, sendmsg_f, "sendmsg", WRITE, SO_SNDTIMEO, msg, flags);
}

// 拦截文件关闭操作，这是为了释放我们封装的FdCtx上下文与清空还挂在epoll里的残留事件
int close(int fd) {
  if (!t_hook_enable) {
    return close_f(fd);
  }

  FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
  if (ctx) {
    auto iom = IOManager::GetThis();
    if (iom) {
      // 在fd被销毁前，将其身上挂着的事件（读/写等）先从epoll里强制抹除，防止内存泄漏和野指针唤醒
      iom->cancelAll(fd);
    }
    // 把该fd的自定义信息从管理池中删掉
    FdMgr::GetInstance()->del(fd);
  }
  return close_f(fd);
}
int fcntl(int fd, int cmd, ... /* arg */) {
  va_list va;
  va_start(va, cmd);
  switch (cmd) {
    case F_SETFL: {
      int arg = va_arg(va, int);
      va_end(va);
      FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
      if (!ctx || ctx->isClose() || !ctx->isSocket()) {
        return fcntl_f(fd, cmd, arg);
      }
      ctx->setUserNonblock(arg & O_NONBLOCK);
      if (ctx->getSysNonblock()) {
        arg |= O_NONBLOCK;
      } else {
        arg &= ~O_NONBLOCK;
      }
      return fcntl_f(fd, cmd, arg);
    } break;
    case F_GETFL: {
      va_end(va);
      int arg = fcntl_f(fd, cmd);
      FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
      if (!ctx || ctx->isClose() || !ctx->isSocket()) {
        return arg;
      }
      if (ctx->getUserNonblock()) {
        return arg | O_NONBLOCK;
      } else {
        return arg & ~O_NONBLOCK;
      }
    } break;
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
    case F_SETFD:
    case F_SETOWN:
    case F_SETSIG:
    case F_SETLEASE:
    case F_NOTIFY:
#ifdef F_SETPIPE_SZ
    case F_SETPIPE_SZ:
#endif
    {
      int arg = va_arg(va, int);
      va_end(va);
      return fcntl_f(fd, cmd, arg);
    } break;
    case F_GETFD:
    case F_GETOWN:
    case F_GETSIG:
    case F_GETLEASE:
#ifdef F_GETPIPE_SZ
    case F_GETPIPE_SZ:
#endif
    {
      va_end(va);
      return fcntl_f(fd, cmd);
    } break;
    case F_SETLK:
    case F_SETLKW:
    case F_GETLK: {
      struct flock *arg = va_arg(va, struct flock *);
      va_end(va);
      return fcntl_f(fd, cmd, arg);
    } break;
    case F_GETOWN_EX:
    case F_SETOWN_EX: {
      struct f_owner_exlock *arg = va_arg(va, struct f_owner_exlock *);
      va_end(va);
      return fcntl_f(fd, cmd, arg);
    } break;
    default:
      va_end(va);
      return fcntl_f(fd, cmd);
  }
}

int ioctl(int d, unsigned long int request, ...) {
  va_list va;
  va_start(va, request);
  void *arg = va_arg(va, void *);
  va_end(va);

  if (FIONBIO == request) {
    bool user_nonblock = !!*(int *)arg;
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(d);
    if (!ctx || ctx->isClose() || !ctx->isSocket()) {
      return ioctl_f(d, request, arg);
    }
    ctx->setUserNonblock(user_nonblock);
  }
  return ioctl_f(d, request, arg);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
  return getsockopt_f(sockfd, level, optname, optval, optlen);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
  if (!t_hook_enable) {
    return setsockopt_f(sockfd, level, optname, optval, optlen);
  }
  if (level == SOL_SOCKET) {
    if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) {
      FdCtx::ptr ctx = FdMgr::GetInstance()->get(sockfd);
      if (ctx) {
        const timeval *v = (const timeval *)optval;
        ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
      }
    }
  }
  return setsockopt_f(sockfd, level, optname, optval, optlen);
}
}
}  // namespace monsoon