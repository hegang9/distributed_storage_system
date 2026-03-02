#include "util.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iomanip>

// 断言辅助：条件失败时打印错误信息并直接退出
void myAssert(bool condition, std::string message) {
  if (!condition) {
    std::cerr << "Error: " << message << std::endl;
    std::exit(EXIT_FAILURE);
  }
}

// 获取高精度当前时间点（用于超时与统计）
std::chrono::_V2::system_clock::time_point now() { return std::chrono::high_resolution_clock::now(); }

// 生成随机选举超时，减少多个节点同时发起选举的概率
std::chrono::milliseconds getRandomizedElectionTimeout() {
  std::random_device rd;
  std::mt19937 rng(rd());
  std::uniform_int_distribution<int> dist(minRandomizedElectionTime, maxRandomizedElectionTime);

  return std::chrono::milliseconds(dist(rng));
}

// 线程休眠 N 毫秒
void sleepNMilliseconds(int N) { std::this_thread::sleep_for(std::chrono::milliseconds(N)); };

// 从给定端口开始向后递增，寻找可用端口
// 尝试次数上限为 30 次，失败时将 port 置为 -1
bool getReleasePort(short &port) {
  short num = 0;
  while (!isReleasePort(port) && num < 30) {
    ++port;
    ++num;
  }
  if (num >= 30) {
    port = -1;
    return false;
  }
  return true;
}

// 通过尝试绑定回环地址来判断端口是否可用
bool isReleasePort(unsigned short usPort) {
  int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(usPort);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 绑定到回环地址127.0.0.1，只在本机测试端口可用性，不涉及网络
  int ret = ::bind(s, (sockaddr *)&addr, sizeof(addr));
  if (ret != 0) {
    close(s);
    return false;
  }
  close(s);
  return true;
}

// 调试打印：仅在 Debug 为 true 时输出
// 输出格式包含时间戳，内容采用 printf 风格可变参数
void DPrintf(const char *format, ...) {
  if (Debug) {
    // 获取当前的日期，然后取日志信息，写入相应的日志文件当中 a+
    time_t now = time(nullptr);   // 获取当前时间的秒数（从 1970-01-01 起），即时间戳
    tm *nowtm = localtime(&now);  // 将时间戳转换为本地时间的 tm 结构，包含年月日时分秒等信息
    va_list args;                 // 参数列表指针，用于遍历...中的所有参数
    va_start(args, format);
    // 先打印时间戳前缀，格式为 [年-月-日-时-分-秒]
    std::printf("[%d-%d-%d-%d-%d-%d] ", nowtm->tm_year + 1900, nowtm->tm_mon + 1, nowtm->tm_mday, nowtm->tm_hour,
                nowtm->tm_min, nowtm->tm_sec);
    std::vprintf(format, args);  // 类似 printf，但参数来自 va_list 而非直接列出。
                                 // 根据 format 中的格式符（%d、%s等）逐个从 args 中取参数并格式化输出
    std::printf("\n");
    va_end(args);  // 清理可变参数列表，释放 args 占用的资源
  }
}
