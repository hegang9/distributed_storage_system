#include "mprpcchannel.h"
#include <arpa/inet.h>        // 包含 inet_addr()：将点分十进制 IP 转换为网络字节序的二进制格式
#include <netinet/in.h>       // 包含 sockaddr_in 结构体、htons()：主机字节序到网络字节序的端口转换
#include <sys/socket.h>       // 包含 socket()、connect()、send()、recv() 等套接字操作函数
#include <unistd.h>           // 包含 close()：关闭文件描述符（套接字）
#include <cerrno>             // 包含 errno：系统调用错误码，用于诊断网络操作失败原因
#include <string>             // 包含 std::string 字符串类
#include "mprpccontroller.h"  // 包含 MprpcController：RPC 控制器，用于设置/获取 RPC 调用的错误状态
#include "rpcheader.pb.h"     // 包含 RPC::RpcHeader：由 protobuf 生成的 RPC 协议头消息类
#include "util.h"             // 包含 DPrintf()：调试打印宏/函数，用于输出调试日志

/**
 * ========================= RPC 请求协议格式说明 =========================
 *
 * 发送的数据流格式如下：
 *   [header_size (varint编码)] + [rpc_header (service_name + method_name + args_size)] + [args (序列化的请求参数)]
 *
 * 其中：
 *   - header_size：RPC 头部的字节长度，使用 protobuf 的 varint 变长编码，节省空间
 *   - rpc_header：protobuf 序列化的 RPC::RpcHeader 消息，包含服务名、方法名、参数长度
 *   - args：protobuf 序列化的请求参数（即 request 消息的序列化结果）
 *
 * 接收端解析流程：
 *   1) 先读取 varint 得到 header_size
 *   2) 再读取 header_size 字节，反序列化得到 RpcHeader（从中获取 service_name、method_name、args_size）
 *   3) 再读取 args_size 字节，反序列化得到请求参数
 */

/**
 * @brief RPC 通道的核心方法——执行一次完整的 RPC 远程过程调用
 *
 * 所有通过 protobuf Stub 代理对象调用的 RPC 方法，最终都会路由到此方法。
 * 该方法统一负责：请求数据的序列化、协议打包、TCP 网络发送、响应接收、响应反序列化。
 *
 * @param method     方法描述符，由 protobuf 框架传入，包含当前调用的方法名和所属服务信息
 * @param controller RPC 控制器，用于在调用过程中发生错误时通过 SetFailed() 设置错误信息
 * @param request    请求消息指针，包含本次 RPC 调用的输入参数
 * @param response   响应消息指针，本方法会将服务端返回的数据反序列化填充到此对象
 * @param done       完成回调闭包，当前实现为同步调用，未使用此参数
 */
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                              google::protobuf::RpcController* controller, const google::protobuf::Message* request,
                              google::protobuf::Message* response, google::protobuf::Closure* done) {
  // ==================== 第一步：检查并建立 TCP 连接 ====================
  // 如果当前没有有效的 TCP 连接（m_clientFd == -1），则尝试建立新连接
  if (m_clientFd == -1) {
    std::string errMsg;
    bool rt = newConnect(m_ip.c_str(), m_port, &errMsg);
    if (!rt) {
      // 连接失败，通过 controller 通知调用方，并直接返回
      DPrintf("[func-MprpcChannel::CallMethod]重连接ip：{%s} port{%d}失败", m_ip.c_str(), m_port);
      controller->SetFailed(errMsg);
      return;
    } else {
      DPrintf("[func-MprpcChannel::CallMethod]连接ip：{%s} port{%d}成功", m_ip.c_str(), m_port);
    }
  }

  // ==================== 第二步：提取 RPC 服务名和方法名 ====================
  // 通过 protobuf 的 MethodDescriptor 获取该方法所属的 ServiceDescriptor
  const google::protobuf::ServiceDescriptor* sd = method->service();
  std::string service_name = sd->name();     // 获取服务名，如 "UserServiceRpc"
  std::string method_name = method->name();  // 获取方法名，如 "Login"

  // ==================== 第三步：序列化请求参数 ====================
  // 将 request 消息对象序列化为字符串，获取序列化后的字节长度
  uint32_t args_size{};  // 请求参数序列化后的字节长度
  std::string args_str;  // 请求参数序列化后的字符串
  if (request->SerializeToString(&args_str)) {
    args_size = args_str.size();
  } else {
    // 序列化失败，设置错误信息并返回
    controller->SetFailed("serialize request error!");
    return;
  }

  // ==================== 第四步：构建 RPC 协议头 ====================
  // 创建 RpcHeader protobuf 消息，填充服务名、方法名、参数长度
  RPC::RpcHeader rpcHeader;
  rpcHeader.set_service_name(service_name);  // 设置服务名
  rpcHeader.set_method_name(method_name);    // 设置方法名
  rpcHeader.set_args_size(args_size);        // 设置参数长度，接收端据此读取正确长度的参数数据

  // 将 RpcHeader 序列化为字符串
  std::string rpc_header_str;
  if (!rpcHeader.SerializeToString(&rpc_header_str)) {
    // 协议头序列化失败，设置错误信息并返回
    controller->SetFailed("serialize rpc header error!");
    return;
  }

  // ==================== 第五步：按协议格式打包完整的发送数据 ====================
  // 最终数据格式：[header_size(varint)] + [rpc_header] + [args]
  std::string send_rpc_str;  // 用来存储最终要通过 TCP 发送的完整数据
  {
    // 使用 protobuf 提供的流式写入工具，确保 varint 编码的正确性
    // StringOutputStream 将数据写入 std::string 对象
    google::protobuf::io::StringOutputStream string_output(&send_rpc_str);
    // CodedOutputStream 提供 varint 编码等高级写入功能
    google::protobuf::io::CodedOutputStream coded_output(&string_output);

    // 写入 rpc_header 的长度，使用 varint 变长编码（1~5字节），节省空间
    // 接收端先读取这个长度值，才知道后续多少字节是 rpc_header
    coded_output.WriteVarint32(static_cast<uint32_t>(rpc_header_str.size()));

    // 写入 rpc_header 本身（包含 service_name、method_name、args_size）
    coded_output.WriteString(rpc_header_str);
  }
  // CodedOutputStream 析构时会 flush 缓冲区到 send_rpc_str

  // 将序列化后的请求参数追加到发送数据的末尾
  send_rpc_str += args_str;

  // ==================== 第六步：通过 TCP 发送 RPC 请求 ====================
  // 使用 send() 系统调用发送数据，如果发送失败（返回 -1），则尝试重连后再发送
  // 注意：这里使用 while 循环实现发送失败时的重连重试机制
  while (-1 == send(m_clientFd, send_rpc_str.c_str(), send_rpc_str.size(), 0)) {
    // 发送失败，打印错误信息
    char errtxt[512] = {0};
    sprintf(errtxt, "send error! errno:%d", errno);
    std::cout << "尝试重新连接，对方ip：" << m_ip << " 对方端口" << m_port << std::endl;

    // 关闭当前失效的连接，重置文件描述符
    close(m_clientFd);
    m_clientFd = -1;

    // 尝试重新建立 TCP 连接
    std::string errMsg;
    bool rt = newConnect(m_ip.c_str(), m_port, &errMsg);
    if (!rt) {
      // 重连失败，设置错误信息并返回，不再继续重试
      controller->SetFailed(errMsg);
      return;
    }
    // 重连成功后，while 循环会再次尝试 send()
  }

  // ==================== 第七步：接收 RPC 响应数据 ====================
  // 请求发送成功后，阻塞等待服务端处理并返回响应
  // 注意：当前缓冲区大小为 1024 字节，如果响应数据超过此大小可能会截断
  char recv_buf[1024] = {0};  // 接收缓冲区
  int recv_size = 0;          // 实际接收到的字节数
  if (-1 == (recv_size = recv(m_clientFd, recv_buf, 1024, 0))) {
    // 接收失败，关闭连接并设置错误信息
    close(m_clientFd);
    m_clientFd = -1;
    char errtxt[512] = {0};
    sprintf(errtxt, "recv error! errno:%d", errno);
    controller->SetFailed(errtxt);
    return;
  }

  // ==================== 第八步：反序列化响应数据 ====================
  // 将接收到的原始字节数据反序列化为 protobuf Message 对象
  // 注意：这里使用 ParseFromArray 而非 ParseFromString
  // 原因：如果用 string(recv_buf)，遇到 \0 字符会截断后续数据，导致反序列化失败
  // ParseFromArray 直接按字节长度读取，不受 \0 影响
  if (!response->ParseFromArray(recv_buf, recv_size)) {
    // 反序列化失败，设置错误信息
    char errtxt[1050] = {0};
    sprintf(errtxt, "parse error! response_str:%s", recv_buf);
    controller->SetFailed(errtxt);
    return;
  }
  // RPC 调用完成，response 对象已填充好服务端返回的数据
}

/**
 * @brief 建立到指定 IP 和端口的 TCP 连接
 *
 * 该方法执行以下步骤：
 *   1) 调用 socket() 创建一个 TCP 套接字（AF_INET + SOCK_STREAM）
 *   2) 配置 sockaddr_in 结构体，设置目标 IP 和端口
 *   3) 调用 connect() 建立 TCP 连接
 *   4) 成功则将套接字 fd 保存到 m_clientFd，失败则清理资源
 *
 * @param ip     目标 IP 地址（C 风格字符串，点分十进制格式，如 "192.168.1.1"）
 * @param port   目标端口号（主机字节序，函数内部通过 htons() 转为网络字节序）
 * @param errMsg 输出参数，失败时写入错误描述信息
 * @return true  连接成功，m_clientFd 已更新为新套接字
 * @return false 连接失败，m_clientFd 被置为 -1
 */
bool MprpcChannel::newConnect(const char* ip, uint16_t port, string* errMsg) {
  // 创建 TCP 套接字
  // AF_INET: IPv4 协议族
  // SOCK_STREAM: 面向连接的流式套接字（TCP）
  // 第三个参数 0: 自动选择协议（对于 SOCK_STREAM 默认是 TCP）
  int clientfd = socket(AF_INET, SOCK_STREAM, 0);
  if (-1 == clientfd) {
    // 套接字创建失败（可能是文件描述符耗尽等原因）
    char errtxt[512] = {0};
    sprintf(errtxt, "create socket error! errno:%d", errno);
    m_clientFd = -1;
    *errMsg = errtxt;
    return false;
  }

  // 配置服务端地址结构体
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;             // IPv4 地址族
  server_addr.sin_port = htons(port);           // 端口号：主机字节序 -> 网络字节序（大端）
  server_addr.sin_addr.s_addr = inet_addr(ip);  // IP 地址：点分十进制字符串 -> 网络字节序二进制

  // 发起 TCP 三次握手，建立连接
  if (-1 == connect(clientfd, (struct sockaddr*)&server_addr, sizeof(server_addr))) {
    // 连接失败（可能是目标不可达、连接被拒绝、超时等原因）
    close(clientfd);  // 关闭已创建但未成功连接的套接字，释放资源
    char errtxt[512] = {0};
    sprintf(errtxt, "connect fail! errno:%d", errno);
    m_clientFd = -1;
    *errMsg = errtxt;
    return false;
  }

  // 连接成功，保存套接字文件描述符，后续通过此 fd 进行数据收发
  m_clientFd = clientfd;
  return true;
}

/**
 * @brief 构造函数，初始化 RPC 通道并可选择立即建立连接
 *
 * @param ip         RPC 服务端 IP 地址（点分十进制格式）
 * @param port       RPC 服务端端口号（主机字节序）
 * @param connectNow 是否立即建立连接
 *                   - true: 立即尝试连接，失败最多重试 3 次
 *                   - false: 延迟连接，等到第一次 CallMethod 调用时再连接
 *
 * @note 当前使用短连接模式（每次 RPC 调用使用独立的 TCP 连接），
 *       未来可考虑改为长连接以提高性能，减少 TCP 三次握手的开销
 */
MprpcChannel::MprpcChannel(string ip, short port, bool connectNow) : m_ip(ip), m_port(port), m_clientFd(-1) {
  // 如果不需要立即连接（延迟连接模式），直接返回
  // 在首次调用 CallMethod 时，会检测 m_clientFd == -1 并自动触发连接
  if (!connectNow) {
    return;
  }

  // 立即连接模式：尝试建立 TCP 连接
  std::string errMsg;
  auto rt = newConnect(ip.c_str(), port, &errMsg);

  // 如果首次连接失败，最多重试 3 次
  // 这在服务端刚启动、网络短暂抖动等场景下能有效提高连接成功率
  int tryCount = 3;
  while (!rt && tryCount--) {
    std::cout << errMsg << std::endl;  // 输出失败原因，便于调试排查
    rt = newConnect(ip.c_str(), port, &errMsg);
  }
  // 注意：如果重试 3 次仍然失败，m_clientFd 保持 -1，
  // 后续 CallMethod 调用时会再次尝试连接
}