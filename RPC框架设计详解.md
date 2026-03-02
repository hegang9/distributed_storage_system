## ProtoBuf 协议
`Protocol Buffers`（简称 Protobuf）是一种高效的、跨平台的数据序列化协议，由 Google 开发并广泛应用于各种场景。通过使用 Protobuf 编译器（protoc），可以将定义好的 .proto 文件编译成多种编程语言的代码，从而方便地在项目中使用。


## RPC 协议定义
使用ProtoBuf进行网络通信协议RPC的定义。ProtoBuf 负责将复杂的、带有指针引用的、不连续的内存对象，“压扁”成一串标准的、连续的二进制字节流。接收方再把它“还原”成自己内存里的对象。这个过程叫**序列化 (Serialization) 和 反序列化 (Deserialization)**。

在代码中，定义传输消息只需要编写`.proto`文件，这是RPC通信的“合同”，定义通信双方的数据格式和服务接口。使用 ProtoBuf 的编译器 (protoc) 将.proto文件编译为C++代码，从而在项目中使用。

**编写.proto文件的语法：**

| 语法原型 | 语法示例 | 语法作用 |
| :--- | :--- | :--- |
| `syntax = "proto3";` | `syntax = "proto3";` | 指定使用 ProtoBuf 版本 3 的语法。必须位于文件的第一行。 |
| `package <包名>;` | `package fixbug;` | 定义命名空间，防止不同项目之间的命名冲突。生成的 C++ 代码会位于该命名空间下。 |
| `option <选项名> = <值>;` | `option cc_generic_services = true;` | 设置生成代码的选项。`cc_generic_services = true` 告诉编译器生成 C++ 的 Service 基类和 Stub 类。 |
| `message <消息名> { ... }` | `message ResultCode { ... }` | 定义传输的数据结构（消息）。类似于 C++ 中的 struct 或 class。 |
| `<类型> <字段名> = <唯一编号>;` | `int32 errcode = 1;` | 定义消息中的字段。每个字段必须有一个唯一的整数编号（Tag），用于二进制编码中标识该字段，一旦分配不可更改。 |
| `repeated <类型> <字段名> = <唯一编号>;` | `repeated bytes friends = 2;` | 定义重复字段（动态数组）。在 C++ 中生成 `std::vector` 或类似的重复容器结构。 |
| `enum <枚举名> { ... }` | `enum Color { RED = 0; BLUE = 1; }` | 定义枚举类型。第一个枚举值必须为 0。 |
| `service <服务名> { ... }` | `service FiendServiceRpc { ... }` | 定义一个 RPC 服务接口。 |
| `rpc <方法名>(<请求消息>) returns(<响应消息>);` | `rpc GetFriendsList(GetFriendsListRequest) returns(GetFriendsListResponse);` | 在服务中定义具体的 RPC 方法，指定接收的请求消息类型和返回的响应消息类型。 |


## 项目中RPC框架设计
#### 设计层面：
RPC 框架分为三部分：公用的通信协议、服务端处理、客户端调用。
- 通信协议：采用 Header Length + Header Data + Args Data 的变长协议，`rpcheader.proto`: 定义了头部元数据（服务名、方法名、参数体长度）。
- 服务端 (`RpcProvider`)：
  - 注册管理：维护一个 ServiceMap，记录所有可调用的服务对象和方法；
  - 网络监听: 使用 muduo 库监听端口；（后续使用自开发的网络库替换）
  - 请求分发: 收到数据 -> 解析 Header -> 找到对应的 Service 和 Method -> 调用 Service::CallMethod。
- 客户端 (`MprpcChannel`)：
  - 代理发送: 继承自 `google::protobuf::RpcChannel`，拦截所有 Stub 的方法调用；
  - 数据打包: 将请求对象序列化，组装 Header，按协议打包；
  - 网络发送: 建立连接并发送数据，然后阻塞等待响应。

#### 定义层面（rpc目录下）：
- 服务端核心类 `RpcProvider` (rpcprovider.h/cpp)：
  - `NotifyService(Service *service)`：这是服务注册入口。它利用 ProtoBuf 的反射机制 (GetDescriptor())，自动提取该服务下所有的 Method，并存入 `m_serviceMap`，而不需要人工一个个硬编码注册；
  - `Run(port)`: 启动 网络库 `TcpServer`；
  - `OnMessage(...)`：这是核心的“路由器”，它读取字节流，先读出 `header_size`，根据 `header_size` 解析出 `RpcHeader`，得知对方想调 `UserService` 的 Login 方法，从 `m_serviceMap` 中找到注册过的 `UserService` 对象，利用反射创建出具体的请求对象 `Request`，最终调用 `service->CallMethod(...)` 执行真正的业务逻辑。
- 客户端核心类 `MprpcChannel` (mprpcchannel.cpp)：
  - `CallMethod(...)`：这是 ProtoBuf 提供给我们的扩展点。所有 Stub 的调用最终都会流向这里；
  - 这里实现了协议打包逻辑：先序列化业务参数`request`，得到`args_str`；再构造 `RpcHeader`，填入服务名、方法名、args_str.size()；序列化 `RpcHeader`；计算 header 长度，先发长度，再发 header，最后发参数；
  - 使用 `Socket` 发送数据并等待接收结果；

#### 使用层面：
1. 定义协议(.proto)：定义服务接口
2. 写服务端(Provider)：
   1. 继承生成的 `Service` 基类，实现虚函数 `GetFriendsList`
   2. 在 main 函数中创建一个 `RpcProvider`
   3. 调用 `wrapper.NotifyService(new FriendService())` 注册你的服务
   4. 调用 `wrapper.Run()` 启动
3. 写客户端 (Consumer)：
   1. 创建一个 `MprpcChannel` 对象（连到服务端的 IP 端口）
   2. 创建一个 Stub 对象，把 `channel` 传进去：`FiendServiceRpc_Stub stub(channel)`
   3. 直接调用 `stub.GetFriendsList(...)`

