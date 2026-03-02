
#ifndef UTIL_H
#define UTIL_H

// 网络与端口检查相关的系统头文件
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/access.hpp>
// 并发与同步相关头文件
#include <condition_variable>  // pthread_condition_t
#include <functional>
#include <iostream>
#include <mutex>  // pthread_mutex_t
#include <queue>
#include <random>
#include <sstream>
#include <thread>
// 运行时配置与全局开关（如 Debug、选举超时时间范围）
#include "config.h"

// 轻量级“延迟执行”工具：对象析构时执行传入的回调
// 使用方式：DEFER { ... }; 在作用域结束时自动执行
template <class F>
class DeferClass {
 public:
  DeferClass(F&& f) : m_func(std::forward<F>(f)) {}
  DeferClass(const F& f) : m_func(f) {}
  ~DeferClass() { m_func(); }

  DeferClass(const DeferClass& e) = delete;
  DeferClass& operator=(const DeferClass& e) = delete;

 private:
  F m_func;
};

// 通过宏拼接生成唯一变量名，避免多处 DEFER 冲突
#define _CONCAT(a, b) a##b  // ##是宏拼接操作符，用于把两个记号拼接成一个
#define _MAKE_DEFER_(line) DeferClass _CONCAT(defer_placeholder, line) = [&]()  // 函数式宏，line是参数，

#undef DEFER  // 取消之前可能定义过的 DEFER，确保使用新的定义
// 在当前作用域声明一个延迟执行的匿名对象
#define DEFER _MAKE_DEFER_(__LINE__)  //__LINE__ 是预定义宏，在编译时被替换为当前源代码行号

// 条件调试日志：当 Debug 为 true 时输出时间戳与格式化内容
void DPrintf(const char* format, ...);

// 断言辅助：条件不成立时输出错误并终止进程
void myAssert(bool condition, std::string message = "Assertion failed!");

// 轻量级 printf 风格格式化，返回 std::string
// 先计算所需缓冲区大小，再格式化写入
template <typename... Args>
std::string format(const char* format_str, Args... args) {
  // 计算格式化后需要多少字节
  int size_s = std::snprintf(nullptr, 0, format_str, args...) + 1;  // "\0"
  if (size_s <= 0) {
    throw std::runtime_error("Error during formatting.");
  }
  auto size = static_cast<size_t>(size_s);
  std::vector<char> buf(size);
  std::snprintf(buf.data(), size, format_str, args...);
  return std::string(buf.data(), buf.data() + size - 1);  // remove '\0'
}

// 获取当前时间点（高精度时钟）
std::chrono::_V2::system_clock::time_point now();

// 生成随机选举超时时间（毫秒），用于 Raft 选举抖动
std::chrono::milliseconds getRandomizedElectionTimeout();
// 睡眠 N 毫秒
void sleepNMilliseconds(int N);

// 异步写日志的日志队列
// 读操作是阻塞的，语义类似 Go 的无缓冲 chan
// TODO：批处理优化
template <typename T>
class LockQueue {
 public:
  // 多生产者：多个 worker 线程写入队列
  void Push(const T& data) {
    std::lock_guard<std::mutex> lock(m_mutex);  // 使用lock_gurad，即RAII的思想保证锁正确释放
    m_queue.push(data);
    m_condvariable.notify_one();  // 唤醒等待的消费者线程
  }

  // 单消费者：一个线程读取队列并写日志文件
  T Pop() {
    std::unique_lock<std::mutex> lock(m_mutex);
    while (m_queue.empty()) {
      // 日志队列为空，线程进入wait状态
      m_condvariable.wait(lock);  // 这里用unique_lock是因为lock_guard不支持解锁，而unique_lock支持
    }
    T data = m_queue.front();
    m_queue.pop();
    return data;
  }

  // 带超时的读取：timeout 为毫秒，超时返回 false
  bool timeOutPop(int timeout, T* ResData) {
    std::unique_lock<std::mutex> lock(m_mutex);

    // 获取当前时间点，并计算出超时时刻
    auto now = std::chrono::system_clock::now();
    auto timeout_time = now + std::chrono::milliseconds(timeout);

    // 在超时之前，不断检查队列是否为空
    while (m_queue.empty()) {
      // 如果已经超时了，就返回失败
      if (m_condvariable.wait_until(lock, timeout_time) == std::cv_status::timeout) {
        return false;
      } else {
        continue;
      }
    }

    T data = m_queue.front();
    m_queue.pop();
    *ResData = data;
    return true;
  }

 private:
  std::queue<T> m_queue;
  std::mutex m_mutex;
  std::condition_variable m_condvariable;  // 条件变量，用于线程同步和唤醒
};

// 这个 Op 是 KV 层传递给 Raft 的 command
class Op {
 public:
  // Your definitions here.
  // Field names must start with capital letters,
  // otherwise RPC will break.
  // 操作类型："Get" / "Put" / "Append"
  std::string Operation;
  std::string Key;
  std::string Value;
  // 客户端唯一标识（用于去重与线性一致性）
  std::string ClientId;
  // 客户端请求序列号（防止重复执行）
  int RequestId;
  // IfDuplicate bool // Duplicate command can't be applied twice , but only for PUT and APPEND

 public:
  // 序列化为字符串：当前通过 Boost 序列化输出文本
  // 注意：Raft RPC 中 command 目前用 string 表示，后续可替换为 protobuf
  std::string asString() const {
    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);  // 构造函数参数 ss 表示把序列化后的数据写入这个字符串流

    oa << *this;  // 这一行触发Boost的序列化机制，调用 Op 类中的 serialize 方法，把对象的状态写入 oa 中，最终写入 ss 中

    return ss.str();
  }

  // 从字符串反序列化恢复对象
  bool parseFromString(std::string str) {
    std::stringstream iss(str);
    boost::archive::text_iarchive ia(iss);
    // read class state from archive
    ia >> *this;  // 直接修改当前对象的成员变量
    return true;  // TODO : 解析失败如何处理，要看一下boost库了
  }

 public:
  // 便于调试输出的格式化打印
  friend std::ostream& operator<<(std::ostream& os, const Op& obj) {
    os << "[MyClass:Operation{" + obj.Operation + "},Key{" + obj.Key + "},Value{" + obj.Value + "},ClientId{" +
              obj.ClientId + "},RequestId{" + std::to_string(obj.RequestId) + "}";  // 在这里实现自定义的输出格式
    return os;
  }

 private:
  // 允许 Boost 序列化库的内部类 access 访问 Op 的私有成员。这是Boost序列化的必要声明，否则 Boost
  // 无法访问私有的serialize() 方法。
  friend class boost::serialization::access;
  // Boost 序列化入口：保持字段顺序一致；既处理序列化也处理反序列化
  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar & Operation;
    ar & Key;
    ar & Value;
    ar & ClientId;
    ar & RequestId;
  }
};

// KVServer 统一错误码

const std::string OK = "OK";
const std::string ErrNoKey = "ErrNoKey";
const std::string ErrWrongLeader = "ErrWrongLeader";

// 获取可用端口

// 判断端口是否可用（尝试绑定回环地址）
bool isReleasePort(unsigned short usPort);

// 从指定端口开始向后搜索可用端口（最多尝试 30 次）
bool getReleasePort(short& port);

// int main(int argc, char** argv)
//{
//     short port = 9060;
//     if(getReleasePort(port)) //在port的基础上获取一个可用的port
//     {
//         std::cout << "可用的端口号为：" << port << std::endl;
//     }
//     else
//     {
//         std::cout << "获取可用端口号失败！" << std::endl;
//     }
//     return 0;
// }

#endif  //  UTIL_H