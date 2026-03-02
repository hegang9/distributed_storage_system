#ifndef MPRPCCHANNEL_H
#define MPRPCCHANNEL_H

// ========================= protobuf 相关头文件 =========================
#include <google/protobuf/descriptor.h>  // 包含 ServiceDescriptor、MethodDescriptor 等描述符类，用于获取服务和方法的元信息
#include <google/protobuf/message.h>     // 包含 Message 基类，所有 protobuf 消息的基类，提供序列化/反序列化接口
#include <google/protobuf/service.h>     // 包含 RpcChannel、RpcController、Closure 等 RPC 框架基础类

// ========================= C++ 标准库头文件 =========================
#include <algorithm>      // 包含 std::generate_n() 和 std::generate() 等算法函数
#include <functional>     // 包含   std::function 等函数对象相关工具
#include <iostream>       // 包含标准输入输出流 std::cout、std::endl
#include <map>            // 包含 std::map 有序关联容器
#include <random>         // 包含 std::uniform_int_distribution 等随机数生成工具
#include <string>         // 包含 std::string 字符串类
#include <unordered_map>  // 包含 std::unordered_map 哈希关联容器
#include <vector>         // 包含 std::vector 动态数组容器
using namespace std;

/**
 * @class MprpcChannel
 * @brief RPC 客户端通信通道类，继承自 google::protobuf::RpcChannel
 *
 * 这个模块是 RPC 框架中客户端侧的核心通信组件，负责以下功能：
 *
 * 1. **请求序列化**：将 RPC 方法调用信息（服务名、方法名、参数）按自定义协议格式序列化
 *    - 协议格式：header_size(varint编码) + rpc_header(service_name + method_name + args_size) + args
 *
 * 2. **网络通信**：通过 TCP 套接字将序列化后的请求数据发送给 RPC 服务端，
 *    并阻塞等待接收服务端返回的响应数据
 *
 * 3. **响应反序列化**：将接收到的原始字节数据反序列化为 protobuf Message 对象
 *
 * 4. **连接管理**：支持延迟连接（构造时不连接）、断线重连机制，
 *    在发送失败时会自动尝试重新建立 TCP 连接
 *
 * @note 当前实现使用短连接模式，每次 RPC 调用使用独立的 TCP 连接
 * @note 接收缓冲区大小为 1024 字节，对于大响应数据可能需要扩展
 *
 * 使用方式：
 * @code
 *   // 创建通道并立即连接
 *   MprpcChannel channel("127.0.0.1", 8080, true);
 *   // 通过 protobuf 生成的 Stub 类使用该通道进行 RPC 调用
 *   MyService_Stub stub(&channel);
 *   stub.MyMethod(&controller, &request, &response, nullptr);
 * @endcode
 */
class MprpcChannel : public google::protobuf::RpcChannel {
 public:
  /**
   * @brief 重写 protobuf RpcChannel 的核心方法，执行一次完整的 RPC 远程调用
   *
   * 当客户端通过 Stub 代理对象调用任意 RPC 方法时，protobuf 框架会统一回调此方法。
   * 该方法完成以下步骤：
   *   1) 检查 TCP 连接状态，如未连接则尝试建立连接
   *   2) 从 MethodDescriptor 中提取服务名和方法名
   *   3) 将 request 参数序列化为字符串
   *   4) 构建 RPC 协议头（包含服务名、方法名、参数长度）
   *   5) 使用 protobuf CodedOutputStream 将 header 长度 + header + args 打包
   *   6) 通过 TCP 发送打包后的数据，发送失败时自动重连重试
   *   7) 阻塞接收服务端响应数据
   *   8) 将响应数据反序列化到 response 对象中
   *
   * @param method     RPC 方法描述符，包含方法名和所属服务的元信息
   * @param controller RPC 控制器，用于在调用失败时设置错误信息（SetFailed）
   * @param request    RPC 请求消息（输入参数），由调用方填充
   * @param response   RPC 响应消息（输出参数），由本方法填充反序列化后的结果
   * @param done       回调闭包，当前实现中未使用（同步调用模式）
   */
  void CallMethod(const google::protobuf::MethodDescriptor *method, google::protobuf::RpcController *controller,
                  const google::protobuf::Message *request, google::protobuf::Message *response,
                  google::protobuf::Closure *done) override;

  /**
   * @brief 构造函数，初始化 RPC 通道
   * @param ip         RPC 服务端的 IP 地址（点分十进制格式，如 "127.0.0.1"）
   * @param port       RPC 服务端的端口号（主机字节序）
   * @param connectNow 是否在构造时立即建立 TCP 连接
   *                   - true：构造时立即连接，失败最多重试 3 次
   *                   - false：延迟连接，在首次 CallMethod 调用时才建立连接
   */
  MprpcChannel(string ip, short port, bool connectNow);

 private:
  int m_clientFd;          ///< TCP 客户端套接字文件描述符，-1 表示未连接
  const std::string m_ip;  ///< RPC 服务端 IP 地址，保存用于断线重连
  const uint16_t m_port;   ///< RPC 服务端端口号，保存用于断线重连

  /**
   * @brief 建立到指定 IP 和端口的 TCP 连接
   *
   * 创建一个新的 TCP 套接字，并通过 connect() 系统调用连接到目标地址。
   * 连接成功后将新的文件描述符赋值给 m_clientFd。
   * 连接失败时会关闭套接字并将 m_clientFd 置为 -1。
   *
   * @param ip     RPC 服务端 IP 地址（C 风格字符串，点分十进制格式）
   * @param port   RPC 服务端端口号（主机字节序，函数内部会转换为网络字节序）
   * @param errMsg 输出参数，连接失败时通过该指针返回错误描述信息
   * @return true  连接成功
   * @return false 连接失败，错误信息通过 errMsg 返回
   */
  bool newConnect(const char *ip, uint16_t port, string *errMsg);
};

#endif  // MPRPCCHANNEL_H