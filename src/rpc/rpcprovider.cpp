#include "rpcprovider.h"
#include <arpa/inet.h>  // inet_ntoa: 将网络字节序的 IP 地址转换为点分十进制字符串
#include <netdb.h>      // gethostname/gethostbyname: 获取本机主机名和 IP 地址
#include <unistd.h>     // POSIX 标准接口
#include <cstring>
#include <fstream>  // 文件读写操作，用于将节点信息写入配置文件
#include <string>
#include "rpcheader.pb.h"  // protoc 生成的 RPC 消息头定义（包含 service_name, method_name, args_size）
#include "util.h"

/**
 * RPC 通信协议格式说明：
 *
 * 客户端发送的 RPC 请求数据格式如下：
 * +------------------+-----------------------------+-------------------+
 * | header_size      | header_str                  | args_str          |
 * | (varint32编码)   | (RpcHeader protobuf序列化)   | (方法参数序列化)    |
 * +------------------+-----------------------------+-------------------+
 *
 * 其中 RpcHeader 包含：
 *   - service_name: 要调用的服务名（如 "UserService"）
 *   - method_name:  要调用的方法名（如 "Login"）
 *   - args_size:    参数数据 args_str 的长度
 */
/**
 * @brief 注册 RPC 服务到框架中
 * @param service 指向用户自定义的 protobuf Service 派生类对象
 *
 * 通过 protobuf 的反射机制（Descriptor），自动解析出服务名称和该服务下的所有方法，
 * 并将 服务名->服务对象、方法名->方法描述符 的映射关系存储到 m_serviceMap 中。
 * 这样当远程 RPC 请求到来时，可以根据请求中的服务名和方法名快速定位到本地方法。
 *
 * todo: 待修改，需要把本机开启的 IP 和端口写入文件以供服务发现使用
 */
void RpcProvider::NotifyService(google::protobuf::Service *service) {
  ServiceInfo service_info;

  // 通过 Service 对象获取其服务描述符（ServiceDescriptor）
  // 描述符包含了该服务的名称、方法数量、每个方法的详细信息等元数据
  const google::protobuf::ServiceDescriptor *pserviceDesc = service->GetDescriptor();

  // 从描述符中获取服务的名称（如 "UserService"）
  std::string service_name = pserviceDesc->name();

  // 获取该服务中定义的 RPC 方法总数
  int methodCnt = pserviceDesc->method_count();

  std::cout << "service_name:" << service_name << std::endl;

  // 遍历该服务的所有方法，将每个方法的名称和描述符存入 m_methodMap
  for (int i = 0; i < methodCnt; ++i) {
    // 获取第 i 个方法的描述符（MethodDescriptor）
    // 方法描述符包含方法名、输入类型、输出类型等信息
    const google::protobuf::MethodDescriptor *pmethodDesc = pserviceDesc->method(i);
    std::string method_name = pmethodDesc->name();

    // 将方法名和方法描述符的映射存入 service_info
    service_info.m_methodMap.insert({method_name, pmethodDesc});
  }

  // 保存服务对象指针，后续通过它来调用具体的 RPC 方法
  service_info.m_service = service;

  // 将服务名 -> ServiceInfo 的映射存入全局服务表
  m_serviceMap.insert({service_name, service_info});
}

/**
 * @brief 启动 RPC 服务节点，开始监听并处理远程调用请求
 * @param nodeIndex Raft 集群中的节点编号（用于配置文件中区分不同节点）
 * @param port 本节点监听的端口号
 *
 * 启动流程：
 * 1. 通过系统调用获取本机 IP 地址
 * 2. 将节点 IP 和端口信息写入 test.conf 配置文件（其他节点可读取此文件进行服务发现）
 * 3. 创建 muduo TcpServer，设置连接回调和消息读写回调
 * 4. 设置 IO 线程数为 4（muduo 采用 one loop per thread 模型）
 * 5. 启动 TcpServer 并进入事件循环（阻塞）
 */
void RpcProvider::Run(int nodeIndex, short port) {
  // ========== 第一步：获取本机可用 IP 地址 ==========
  // 获取流程：主机名 → DNS/hosts解析 → IP地址列表
  char hname[128];
  struct hostent *hent;
  gethostname(hname, sizeof(hname));  // 1. 获取本机主机名（如 "server01"）
  hent = gethostbyname(
      hname);  // 2. 通过主机名解析出 IP
               // 地址列表（可能有多个网卡），这样可以借助系统配置过滤掉回环地址，返回可对外通信的IP地址列表

  // 3. 遍历 IP 地址列表，取第一个（主网卡 IP）
  // 注意：inet_ntop 是线程安全的现代 API，替代已废弃的 inet_ntoa（使用静态缓冲区，多线程不安全）
  std::string ip;
  char ip_buffer[INET_ADDRSTRLEN];  // 16 字节足够存储 "xxx.xxx.xxx.xxx\0"
  for (int i = 0; hent->h_addr_list[i]; i++) {
    // 将网络字节序的 IP 地址转换为点分十进制字符串
    if (inet_ntop(AF_INET, hent->h_addr_list[i], ip_buffer, INET_ADDRSTRLEN)) {
      ip = ip_buffer;
      break;  // 取第一个有效 IP（主网卡）
    }
  }
  
  if (ip.empty()) {
    std::cout << "获取本机 IP 地址失败！" << std::endl;
    exit(EXIT_FAILURE);
  }

  // ========== 第二步：将节点信息写入配置文件 ==========
  // 配置文件格式示例:
  //   node0ip=192.168.1.100
  //   node0port=8000
  //   node1ip=192.168.1.101
  //   node1port=8001
  std::string node = "node" + std::to_string(nodeIndex);
  std::ofstream outfile;
  outfile.open("test.conf", std::ios::app);  // 以追加模式打开配置文件
  if (!outfile.is_open()) {
    std::cout << "打开文件失败！" << std::endl;
    exit(EXIT_FAILURE);
  }
  outfile << node + "ip=" + ip << std::endl;
  outfile << node + "port=" + std::to_string(port) << std::endl;
  outfile.close();

  // ========== 第三步：创建 muduo 网络服务器 ==========
  muduo::net::InetAddress address(ip, port);

  // 创建 TcpServer 对象，绑定到指定 IP 和端口
  m_muduo_server = std::make_shared<muduo::net::TcpServer>(&m_eventLoop, address, "RpcProvider");

  // 设置连接建立/断开的回调函数
  // 使用 std::bind 将成员函数与 this 指针绑定，使得回调函数中可以访问当前对象的成员
  m_muduo_server->setConnectionCallback(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));

  // 设置消息到达时的回调函数（核心：RPC 请求解析和处理在此回调中完成）
  m_muduo_server->setMessageCallback(
      std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

  // 设置 muduo 的 IO 线程数量（4个线程处理网络IO，提高并发性能）
  m_muduo_server->setThreadNum(4);

  std::cout << "RpcProvider start service at ip:" << ip << " port:" << port << std::endl;

  // ========== 第四步：启动网络服务并进入事件循环 ==========
  // start() 启动 TcpServer，开始监听端口并接受连接
  m_muduo_server->start();

  // loop() 启动事件循环（Reactor 模式），阻塞在此处
  // 持续监听 epoll 事件，处理连接建立、数据到达、定时器等事件
  // 直到调用 EventLoop::quit() 才会退出
  m_eventLoop.loop();
}

/**
 * @brief TCP 连接状态变化的回调函数
 * @param conn 发生状态变化的 TCP 连接
 *
 * 当新连接建立时，不做额外处理（muduo 已自动完成连接的接收）。
 * 当连接断开时（connected() 返回 false），调用 shutdown() 关闭连接的写端，
 * 确保资源被正确释放。
 */
void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &conn) {
  if (!conn->connected()) {
    // RPC 客户端断开连接，关闭写端以释放资源
    conn->shutdown();
  }
}

/**
 * @brief RPC 请求到达时的消息处理回调（RPC 框架的核心解析逻辑）
 * @param conn 发送请求的 TCP 连接
 * @param buffer muduo 接收缓冲区，包含完整的 RPC 请求字节流
 * @param timestamp 消息到达时间（此处未使用）
 *
 * 处理流程：
 * 1. 从缓冲区读取全部数据
 * 2. 解析 varint32 编码的 header_size
 * 3. 读取 header_size 字节的 RpcHeader 数据并反序列化，得到 service_name、method_name、args_size
 * 4. 读取 args_size 字节的方法参数数据
 * 5. 在服务表中查找对应的 service 和 method
 * 6. 反序列化请求参数，创建响应对象
 * 7. 通过 protobuf 的 CallMethod 机制调用本地方法
 *
 * 请求数据格式：
 *   [header_size (varint32)] [header_str (RpcHeader)] [args_str (方法参数)]
 */
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buffer, muduo::Timestamp) {
  // 从 muduo 缓冲区中取出全部接收到的数据
  std::string recv_buf = buffer->retrieveAllAsString();

  // ========== 解析步骤1：使用 protobuf 的 CodedInputStream 解析数据流 ==========
  // ArrayInputStream 将原始字节数组包装为 protobuf 的输入流接口
  google::protobuf::io::ArrayInputStream array_input(recv_buf.data(), recv_buf.size());
  // CodedInputStream 提供了 varint 编码/解码等高级读取功能
  google::protobuf::io::CodedInputStream coded_input(&array_input);

  // 读取消息头的长度（使用 varint32 编码，节省空间）
  uint32_t header_size{};
  coded_input.ReadVarint32(&header_size);

  // ========== 解析步骤2：读取并反序列化 RPC 消息头（RpcHeader） ==========
  std::string rpc_header_str;
  RPC::RpcHeader rpcHeader;
  std::string service_name;
  std::string method_name;

  // 设置读取限制为 header_size 字节，防止读取越界
  // PushLimit 会限制后续读取操作不超过指定字节数
  google::protobuf::io::CodedInputStream::Limit msg_limit = coded_input.PushLimit(header_size);
  // 在限制范围内读取 header_size 字节的数据
  coded_input.ReadString(&rpc_header_str, header_size);
  // 恢复之前的读取限制，以便继续读取后面的参数数据
  coded_input.PopLimit(msg_limit);

  // 反序列化 RpcHeader，提取出服务名、方法名和参数长度
  uint32_t args_size{};
  if (rpcHeader.ParseFromString(rpc_header_str)) {
    // 反序列化成功，提取各字段
    service_name = rpcHeader.service_name();  // 服务名，如 "UserService"
    method_name = rpcHeader.method_name();    // 方法名，如 "Login"
    args_size = rpcHeader.args_size();        // 参数数据的字节长度
  } else {
    // 反序列化失败，打印错误信息并丢弃该请求
    std::cout << "rpc_header_str:" << rpc_header_str << " parse error!" << std::endl;
    return;
  }

  // ========== 解析步骤3：读取方法参数的序列化数据 ==========
  std::string args_str;
  bool read_args_success = coded_input.ReadString(&args_str, args_size);

  if (!read_args_success) {
    // 参数数据读取失败（可能数据不完整），丢弃该请求
    return;
  }

  // ========== 查找步骤：在服务表中查找对应的服务和方法 ==========

  // 根据 service_name 查找已注册的服务,it是 ServiceInfo
  auto it = m_serviceMap.find(service_name);
  if (it == m_serviceMap.end()) {
    // 请求的服务未注册，打印错误信息和当前已注册的服务列表
    std::cout << "服务：" << service_name << " is not exist!" << std::endl;
    std::cout << "当前已经有的服务列表为:";
    for (auto item : m_serviceMap) {
      std::cout << item.first << " ";
    }
    std::cout << std::endl;
    return;
  }

  // 根据 method_name 查找该服务下的具体方法
  auto mit = it->second.m_methodMap.find(method_name);
  if (mit == it->second.m_methodMap.end()) {
    // 请求的方法不存在于该服务中
    std::cout << service_name << ":" << method_name << " is not exist!" << std::endl;
    return;
  }

  // 获取服务对象和方法描述符
  google::protobuf::Service *service = it->second.m_service;       // 服务对象（如 UserService 实例）
  const google::protobuf::MethodDescriptor *method = mit->second;  // 方法描述符（如 Login 方法描述）

  // ========== 调用步骤：构造请求/响应对象并执行本地方法 ==========

  // 通过 protobuf 的反射机制创建该方法对应的 request 对象
  // 1. GetRequestPrototype(method): 获取该方法输入参数类型的"原型对象"（空模板实例）
  //    例如：对于 Login 方法，返回一个空的 LoginRequest 对象（但类型是 Message*）
  // 2. New(): 使用原型对象克隆出一个新实例（原型模式）
  //    等价于 new LoginRequest()，但无需知道具体类型名
  google::protobuf::Message *request = service->GetRequestPrototype(method).New();
  if (!request->ParseFromString(args_str)) {
    // 请求参数反序列化失败
    std::cout << "request parse error, content:" << args_str << std::endl;
    return;
  }

  // 同样通过反射创建该方法对应的 response 对象
  // GetResponsePrototype(method): 获取该方法返回值类型的原型对象
  google::protobuf::Message *response = service->GetResponsePrototype(method).New();

  // 创建 Closure 回调对象（异步完成通知机制）
  // Closure 是 protobuf 提供的回调封装，类似 std::function，用于在业务方法完成后自动触发
  // 
  // NewCallback 的工作原理：
  // 1. 将成员函数 &RpcProvider::SendRpcResponse 和参数 (conn, response) 绑定
  // 2. 创建一个 Closure 对象，内部存储了这些信息
  // 3. 当业务代码调用 done->Run() 时，自动执行：this->SendRpcResponse(conn, response)
  //
  // 为什么需要 Closure？
  // - 业务方法可能异步执行（如查询数据库），框架不知道何时完成
  // - 完成后需要自动发送响应，但需要知道"发给谁"(conn)和"发什么"(response)
  // - Closure 将这些信息打包，业务代码只需调用 done->Run() 即可触发发送逻辑
  google::protobuf::Closure *done =
      google::protobuf::NewCallback<RpcProvider, const muduo::net::TcpConnectionPtr &, google::protobuf::Message *>(
          this,                          // 回调函数所属的对象
          &RpcProvider::SendRpcResponse, // 回调函数指针
          conn,                          // 第1个绑定参数：TCP 连接
          response                       // 第2个绑定参数：响应对象
      );

  // 调用本地 RPC 方法
  // 调用链路（多态机制）：
  //   service->CallMethod()
  //     -> protoc 生成的 ServiceRpc 类中重写的 CallMethod()（根据 method 分发到具体方法）
  //       -> 用户自定义的 Service 类中重写的具体方法（如 Login()）
  //
  // 这就是 protobuf RPC 框架的精妙之处：通过三层继承 + 虚函数机制实现了方法的动态分发
  //   用户 Service 类  继承  protoc 生成的 ServiceRpc 类  继承  google::protobuf::Service
  service->CallMethod(method, nullptr, request, response, done);
}

/**
 * @brief RPC 方法执行完毕后的回调，负责将响应序列化并发送回调用方
 * @param conn 与 RPC 调用方之间的 TCP 连接
 * @param response RPC 方法填充好的响应消息对象
 *
 * 当本地 RPC 方法（如 Login）执行完毕并填充好 response 后，
 * 此回调被自动触发，将 response 序列化为字节流并通过 TCP 连接发送回去。
 * 采用长连接模式，发送完毕后不主动断开连接，支持连接复用。
 */
void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response) {
  std::string response_str;
  if (response->SerializeToString(&response_str)) {
    // 序列化成功，将响应数据通过网络发送给 RPC 调用方
    conn->send(response_str);
  } else {
    std::cout << "serialize response_str error!" << std::endl;
  }
  // 注意：此处采用长连接，不主动断开连接
  // 如需短连接模式可取消注释: conn->shutdown();
}

/**
 * @brief 析构函数，负责清理网络资源
 *
 * 打印当前服务器的 IP 和端口信息，然后退出事件循环。
 * EventLoop::quit() 会使 loop() 函数返回，从而结束 Run() 中的阻塞。
 */
RpcProvider::~RpcProvider() {
  std::cout << "[func - RpcProvider::~RpcProvider()]: ip和port信息：" << m_muduo_server->ipPort() << std::endl;
  m_eventLoop.quit();
}
