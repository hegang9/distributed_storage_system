#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <thread>

void handle_client(int fd) {
  char buffer[1024];
  while (true) {
    memset(buffer, 0, sizeof(buffer));
    // 这是原生阻塞的 recv，如果套接字数据没准备好，当前分配的内核线程会被直接挂起 (Sleep)
    int ret = recv(fd, buffer, sizeof(buffer), 0);
    if (ret > 0) {
      // 模拟极简 HTTP 响应
      const char* resp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
      // 原生阻塞的 send
      send(fd, resp, strlen(resp), 0);
    } else {
      // 读到 0 (对端关闭) 或发生异常 (< 0)，退出循环并关闭连接
      break;
    }
  }
  close(fd);  // 退出此函数后，对应的 std::thread 生命周期也结束了，自动回收堆栈
}

int main() {
  int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  // 开启端口复用，防止重启时报 Address already in use
  setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(8082);  // 传统多线程服务端监听 8082 端口
  addr.sin_addr.s_addr = INADDR_ANY;

  bind(listen_sock, (sockaddr*)&addr, sizeof(addr));
  listen(listen_sock, 1024);

  std::cout << "Thread-per-connection server listening on 8082" << std::endl;

  while (true) {
    // 阻塞等待新的客户端连接
    int fd = accept(listen_sock, nullptr, nullptr);
    if (fd > 0) {
      // 每来一个连接，直接给 OS 下指令 new 一个内核级线程专门伺候它，然后将线程 detach 跑到后台
      // 如果同时存在 2000 个连接，系统里面就会堆积 2000 个内核线程相互抢夺 CPU
      std::thread(handle_client, fd).detach();
    }
  }
  return 0;
}
