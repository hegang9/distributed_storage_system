#ifndef __MONSOON_UTIL_H__
#define __MONSOON_UTIL_H__

#include <assert.h>
#include <cxxabi.h>
#include <execinfo.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <vector>

namespace monsoon {
// 获取当前线程的线程 ID（TID，内核级线程标识）- Linux 特有
pid_t GetThreadId();

// TODO:获取当前协程的协程 ID（未实现）
// 需要与协程调度器关联来获取当前正在运行的 协程 ID
u_int32_t GetFiberId();

// 获取系统启动以来经过的毫秒数（单调时间）
// 使用 CLOCK_MONOTONIC_RAW 确保不受系统时间调整影响
// 用途：超时计算、性能统计等需要精确时间间隔的场景
static uint64_t GetElapsedMS() {
  struct timespec ts = {0};
  // CLOCK_MONOTONIC_RAW：不受 NTP 调整影响的单调递增时间
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  // ts.tv_sec：秒数；ts.tv_nsec：纳秒数
  // 转换为毫秒：秒*1000 + 纳秒/1000000
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 将编译器生成的 mangled 函数名解析为人类可读的函数名
// 输入示例：\"./a.out(_Z3addii+0x15) [0x56559189]\"
// 输出示例：\"add(int, int)\"
// 作用：用于 backtrace 输出时美化显示
static std::string demangle(const char *str) {
  size_t size = 0;  // 用于存储 demangle 结果的大小
  int status = 0;   // demangle 操作的状态码
  std::string rt;
  rt.resize(256);  // 预分配 256 字节的缓冲区

  // 使用正则表达式从 backtrace 字符串中提取 mangled 名字
  // %*[^(]：跳过第一个 '(' 之前的所有字符
  // %*[^_]：跳过第一个 '_' 之前的所有字符
  // %255[^)+]：提取直到 ')' 或 '+' 之前的字符（最多 255 个）
  if (1 == sscanf(str, "%*[^(]%*[^_]%255[^)+]", &rt[0])) {
    // 调用 C++ ABI 的 demangle 函数解析 mangled 名字
    // abi::__cxa_demangle：若 output_buffer 为 nullptr，由该函数分配内存
    char *v = abi::__cxa_demangle(&rt[0], nullptr, &size, &status);
    if (v) {
      std::string result(v);
      free(v);  // 释放 demangle 分配的内存
      return result;
    }
  }
  // demangle 失败，尝试提取原始函数名作为备选
  if (1 == sscanf(str, "%255s", &rt[0])) {
    return rt;
  }
  // 最后的尝试都失败，返回原始字符串
  return str;
}
// 获取当前线程的调用栈（stack backtrace）
// 参数：
//   bt：输出参数，存储解析后的调用栈字符串
//   size：最多捕获多少层调用栈
//   skip：跳过最上面的多少层（通常跳过 Backtrace 函数本身及框架代码）
// 作用：用于调试和错误报告，显示当前执行位置的完整调用路径
static void Backtrace(std::vector<std::string> &bt, int size, int skip) {
  // 分配指针数组，每个指针存储一个调用栈帧的返回地址
  void **array = (void **)malloc((sizeof(void *) * size));

  // backtrace()：从当前位置向上遍历调用栈，记录每一层的返回地址
  // 返回值：实际捕获的调用栈层数（≤ size）
  size_t s = ::backtrace(array, size);

  // backtrace_symbols()：将调用栈地址转换为符号化的字符串
  // 返回一个字符串数组，每个字符串对应一层调用栈
  // 格式示例：\"./a.out(_Z3addii+0x15) [0x56559189]\"
  char **strings = backtrace_symbols(array, s);
  if (strings == NULL) {
    std::cout << "backtrace_synbols error" << std::endl;
    free(array);
    return;
  }

  // 遍历调用栈，解析每一层的函数名，跳过前 skip 层
  // skip 通常为 3-4，用来跳过 backtrace、Backtrace、CondPanic 等框架代码
  for (size_t i = skip; i < s; ++i) {
    // 调用 demangle() 把 mangled 函数名转换为可读形式
    // 例：\"_Z3addii\" → \"add(int, int)\"
    bt.push_back(demangle(strings[i]));
  }

  // 释放动态分配的内存
  free(strings);
  free(array);
}

// 获取调用栈并格式化为字符串
// 参数：
//   size：最多捕获多少层调用栈
//   skip：跳过最上面的多少层
//   prefix：每行前缀（通常用于缩进或日志标记）
// 返回：格式化后的调用栈字符串（每层一行）
// 用途：便于日志输出或错误报告
static std::string BacktraceToString(int size, int skip, const std::string &prefix) {
  std::vector<std::string> bt;
  Backtrace(bt, size, skip);  // 获取原始调用栈

  std::stringstream ss;
  // 把每一层调用栈加上 prefix，并用换行符分隔
  for (size_t i = 0; i < bt.size(); ++i) {
    ss << prefix << bt[i] << std::endl;
  }
  return ss.str();
}

// 条件断言，失败时输出详细的错误信息和调用栈
// 参数：
//   condition：检查的条件
//   err：出错时打印的错误信息
// 行为：
//   - 若 condition 为 true，函数直接返回（无任何操作）
//   - 若 condition 为 false，打印错误信息和调用栈，然后触发断言失败
// 用途：比 assert() 提供更多信息，便于调试问题根源
static void CondPanic(bool condition, std::string err) {
  if (!condition) {
    // 打印触发断言的源代码位置
    std::cout << "[assert by] (" << __FILE__ << ":" << __LINE__ << "),err: " << err << std::endl;

    // 打印完整的调用栈，帮助定位问题
    // 参数：最多 6 层，跳过前 3 层（Backtrace、CondPanic、调用者），无前缀
    std::cout << "[backtrace]\n" << BacktraceToString(6, 3, "") << std::endl;

    // 触发标准库的 assert 失败，导致程序中止
    assert(condition);
  }
}
}  // namespace monsoon

#endif