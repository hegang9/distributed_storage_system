#pragma once
#include <google/protobuf/descriptor.h>  // protobuf 描述符相关头文件，用于获取服务和方法的元信息
#include <muduo/net/EventLoop.h>         // muduo 事件循环，Reactor 模式的核心组件
#include <muduo/net/InetAddress.h>       // muduo 网络地址封装
#include <muduo/net/TcpConnection.h>     // muduo TCP 连接抽象
#include <muduo/net/TcpServer.h>         // muduo TCP 服务器封装
#include <functional>
#include <string>
#include <unordered_map>
#include "google/protobuf/service.h"  // protobuf Service 基类，所有 rpc 服务类的基类

/**
 * @brief RPC 服务提供者类（RPC 服务端）
 *
 * 该类是 RPC 框架的核心组件之一，负责：
 * 1. 注册本地 RPC 服务（NotifyService）
 * 2. 启动网络服务监听端口，接受远程 RPC 调用请求（Run）
 * 3. 解析 RPC 请求并分发到对应的本地服务方法执行（OnMessage）
 * 4. 将执行结果序列化后通过网络返回给调用方（SendRpcResponse）
 *
 * 底层网络通信使用 muduo 网络库实现高性能的异步非阻塞 IO。
 */
class RpcProvider {
 public:
  /**
   * @brief 注册 RPC 服务
   * @param service 指向 protobuf Service 派生类对象的指针
   *
   * 外部调用此接口将自定义的 RPC 服务对象注册到框架中。
   * 框架会通过 protobuf 的描述符机制自动解析出该服务包含的所有方法，
   * 并将服务名 -> 服务对象、方法名 -> 方法描述符的映射关系保存起来，
   * 以便后续根据远程请求快速查找并调用对应的方法。
   */
  void NotifyService(google::protobuf::Service *service);

  /**
   * @brief 启动 RPC 服务节点
   * @param nodeIndex 节点编号，用于在配置文件中区分不同的 Raft 节点
   * @param port 监听端口号
   *
   * 该函数会：
   * 1. 获取本机 IP 地址
   * 2. 将 IP 和端口信息写入配置文件 test.conf（供其他节点发现使用）
   * 3. 创建 muduo TcpServer，绑定连接回调和消息回调
   * 4. 启动事件循环，开始监听并处理 RPC 请求
   *
   * 注意：此函数会阻塞在 EventLoop::loop()，直到服务被关闭。
   */
  void Run(int nodeIndex, short port);

 private:
  // muduo 事件循环对象，负责驱动底层 epoll 事件分发
  muduo::net::EventLoop m_eventLoop;

  // muduo TCP 服务器对象，使用 shared_ptr 管理生命周期
  std::shared_ptr<muduo::net::TcpServer> m_muduo_server;

  /**
   * @brief 服务信息结构体
   *
   * 保存一个已注册的 RPC 服务的完整信息，包括：
   * - 服务对象指针（用于后续调用 CallMethod）
   * - 该服务下所有方法名到方法描述符的映射（用于根据方法名快速定位方法）
   */
  struct ServiceInfo {
    google::protobuf::Service *m_service;  // 服务对象指针（如 UserService 实例）
    // 方法名 -> 方法描述符的映射表
    // 例如: "Login" -> LoginMethodDescriptor, "Register" -> RegisterMethodDescriptor
    std::unordered_map<std::string, const google::protobuf::MethodDescriptor *> m_methodMap;
  };

  // 服务名 -> ServiceInfo 的映射表
  // 例如: "UserService" -> { service对象, {方法映射表} }
  // 通过两级映射，可以根据服务名和方法名快速定位到具体的服务对象和方法描述符
  std::unordered_map<std::string, ServiceInfo> m_serviceMap;

  /**
   * @brief TCP 连接建立/断开的回调函数
   * @param conn TCP 连接的智能指针
   *
   * 当有新连接建立时不做处理（默认接收连接）；
   * 当连接断开时，主动关闭写端（shutdown）。
   */
  void OnConnection(const muduo::net::TcpConnectionPtr &);

  /**
   * @brief 接收到 RPC 请求数据时的回调函数（核心处理逻辑）
   * @param conn TCP 连接的智能指针
   * @param buffer muduo 的接收缓冲区，包含 RPC 请求的原始字节流
   * @param timestamp 消息到达的时间戳
   *
   * 该函数负责：
   * 1. 从网络字节流中解析出 header_size（varint32 编码）
   * 2. 根据 header_size 读取并反序列化 RpcHeader，得到 service_name、method_name、args_size
   * 3. 根据 args_size 读取方法参数的序列化数据
   * 4. 在 m_serviceMap 中查找对应的服务和方法
   * 5. 反序列化请求参数，调用本地方法，通过回调返回结果
   *
   * 数据格式: [header_size(varint32)] [header_str(RpcHeader序列化)] [args_str(方法参数序列化)]
   */
  void OnMessage(const muduo::net::TcpConnectionPtr &, muduo::net::Buffer *, muduo::Timestamp);

  /**
   * @brief RPC 方法执行完毕后的回调函数
   * @param conn TCP 连接的智能指针，用于发送响应数据
   * @param response RPC 方法的响应消息对象
   *
   * 将 response 对象序列化为字节流，通过 TCP 连接发送回 RPC 调用方。
   * 采用长连接模式，发送完响应后不主动断开连接。
   */
  void SendRpcResponse(const muduo::net::TcpConnectionPtr &, google::protobuf::Message *);

 public:
  /**
   * @brief 析构函数
   *
   * 退出事件循环，释放网络资源。
   */
  ~RpcProvider();
};