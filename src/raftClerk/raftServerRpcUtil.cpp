//
// Created by swx on 24-1-4.
//
#include "raftServerRpcUtil.h"

// 构造函数：创建到指定 KvServer 节点的 RPC 连接
// kvserver 不同于 raft 节点之间的通信：
//   - raft 节点之间是双向的（既是 caller 也是 callee）
//   - 这里是 Clerk 向 KvServer 的单向调用，只需要 caller 功能
// 注意：需要先开启服务器，再尝试连接，中间给一个间隔时间等待 RPC 服务器启动
raftServerRpcUtil::raftServerRpcUtil(std::string ip, short port) {
  // 创建 protobuf 生成的 Stub（客户端代理）
  // MprpcChannel 封装了底层的网络通信（序列化、发送、接收、反序列化）
  // 第三个参数 false 表示这是 Clerk 到 KvServer 的连接（不是 raft 节点间连接）
  stub = new raftKVRpcProctoc::kvServerRpc_Stub(new MprpcChannel(ip, port, false));
}

// 析构函数：释放 RPC Stub 资源
raftServerRpcUtil::~raftServerRpcUtil() { delete stub; }

// Get: 调用远端 KvServer 的 Get 方法
// 返回 true 表示 RPC 通信成功，false 表示网络异常
// 具体的业务错误（如 ErrWrongLeader）通过 reply 返回
bool raftServerRpcUtil::Get(raftKVRpcProctoc::GetArgs *GetArgs, raftKVRpcProctoc::GetReply *reply) {
  // 创建 RPC 控制器，用于检测 RPC 调用是否成功
  MprpcController controller;
  // 通过 Stub 发起远程调用，底层会自动序列化参数、发送网络请求、接收并反序列化响应
  stub->Get(&controller, GetArgs, reply, nullptr);
  // 检查 RPC 调用是否失败（网络层面）
  return !controller.Failed();
}

// PutAppend: 调用远端 KvServer 的 PutAppend 方法
// 返回 true 表示 RPC 通信成功，false 表示网络异常
bool raftServerRpcUtil::PutAppend(raftKVRpcProctoc::PutAppendArgs *args, raftKVRpcProctoc::PutAppendReply *reply) {
  MprpcController controller;
  stub->PutAppend(&controller, args, reply, nullptr);
  // 如果 RPC 调用失败，打印错误信息
  if (controller.Failed()) {
    std::cout << controller.ErrorText() << endl;
  }
  return !controller.Failed();
}
