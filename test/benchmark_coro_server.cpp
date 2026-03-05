#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>

#include "../src/fiber/include/hook.hpp"
#include "../src/fiber/include/monsoon.h"

void handle_client(int fd) {
  char buffer[1024];
  while (true) {
    memset(buffer, 0, sizeof(buffer));
    // 这里的 recv 会被协程系统的 hook 机制拦截。
    // 在底层，如果 socket 数据没准备好，它会自动把当前 fiber 挂起(yield)，
    // 等到 epoll 检测到有数据可读时，再自动唤醒(resume)回来，不阻塞底层的内核 Worker 线程。
    int ret = recv(fd, buffer, sizeof(buffer), 0);
    if (ret > 0) {
      // 模拟极简 HTTP 响应
      const char* resp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
      // 同样，这里的 send 也会被 hook 拦截，变为异步非阻塞发送
      send(fd, resp, strlen(resp), 0);
    } else {
      // ret == 0 表示客户端断开连接，ret < 0 发生了错误（这两种情况都直接跳出循环断开）
      break;
    }
  }
  close(fd);  // 自动清理释放 fd
}

void accept_fiber() {
  int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  // 开启端口复用，防止重启时报 Address already in use
  setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(8081);  // 协程服务端监听 8081 端口
  addr.sin_addr.s_addr = INADDR_ANY;

  bind(listen_sock, (sockaddr*)&addr, sizeof(addr));
  listen(listen_sock, 1024);

  std::cout << "Coroutine server listening on 8081" << std::endl;

  while (true) {
    // accept 也被 hook 劫持了。无新连接时当前 fiber 挂起，直到有客户端连入。
    int fd = accept(listen_sock, nullptr, nullptr);
    if (fd > 0) {
      // 当有新连接进入时，将这个新连接的处理任务封装成一个 lambda 函数，
      // 交给 IOManager，让底层的 4 个线程调度执行。
      monsoon::IOManager::GetThis()->scheduler([fd]() { handle_client(fd); });
    }
  }
}

int main() {
  // 必须开启协程 hook 开关，劫持底层的 recv, send, accept 等 syscall 变为非阻塞异步
  monsoon::set_hook_enable(true);

  // 初始化协程 IO 管理器，这里我们限定底层只初始化 4 个内核线程当做 worker pool 处理所有逻辑
  monsoon::IOManager iom(4, true);  // 4 threads pool

  // 将监听连接等待的函数丢进协程调度器，系统会把它当成一个 Fiber 执行
  iom.scheduler(accept_fiber);
  return 0;
}
