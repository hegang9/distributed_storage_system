/**
 * @file config.h
 * @brief Raft 分布式系统全局配置文件
 *
 * 本文件定义了 Raft 共识算法和协程库运行所需的各种超时参数和配置常量。
 * 这些参数直接影响系统的性能和正确性：
 *   - 选举超时(Election Timeout): 决定 Follower 多久没收到心跳后发起选举
 *   - 心跳超时(HeartBeat Timeout): Leader 发送心跳的频率
 *   - 共识超时(Consensus Timeout): 客户端等待命令达成共识的最长时间
 *
 * 调参建议：
 *   - 心跳超时 << 选举超时（通常差一个数量级）
 *   - 网络延迟高时，需要增大 debugMul 系数
 */

#ifndef CONFIG_H
#define CONFIG_H

/**
 * @brief 调试模式开关
 *
 * 当设置为 true 时，DPrintf() 函数会输出调试日志到控制台；
 * 生产环境建议设置为 false 以提高性能。
 */
const bool Debug = true;

/**
 * @brief 时间调试系数
 *
 * 用于统一调整所有超时参数。在网络延迟较高的环境下，
 * 可以增大此值来避免频繁的超时和选举。
 * 单位：毫秒(ms)
 */
const int debugMul = 1;

/**
 * @brief Leader 心跳发送间隔（毫秒）
 *
 * Leader 每隔此时间向所有 Follower 发送心跳（空的 AppendEntries RPC）。
 * 心跳用于：
 *   1. 维持 Leader 的权威，防止 Follower 发起选举
 *   2. 作为日志复制的载体
 *
 * 注意：此值必须远小于选举超时，通常为选举超时的 1/10 ~ 1/5
 */
const int HeartBeatTimeout = 25 * debugMul;

/**
 * @brief 状态机应用日志的检查间隔（毫秒）
 *
 * applier 线程每隔此时间检查是否有新的已提交日志需要应用到状态机。
 * 值越小响应越快，但 CPU 开销越大。
 */
const int ApplyInterval = 10 * debugMul;

/**
 * @brief 选举超时下限（毫秒）
 *
 * Follower 在 [minRandomizedElectionTime, maxRandomizedElectionTime] 范围内
 * 随机选择一个超时时间。如果在此时间内未收到 Leader 心跳，则发起选举。
 *
 * 随机化的目的是避免多个节点同时发起选举导致的"分裂投票"问题。
 */
const int minRandomizedElectionTime = 300 * debugMul;

/**
 * @brief 选举超时上限（毫秒）
 */
const int maxRandomizedElectionTime = 500 * debugMul;

/**
 * @brief 共识超时时间（毫秒）
 *
 * 客户端发送请求后，等待 Raft 集群达成共识的最长时间。
 * 超过此时间后，客户端应重试或切换到其他节点。
 */
const int CONSENSUS_TIMEOUT = 500 * debugMul;

/* ==================== 协程库相关配置 ==================== */

/**
 * @brief 协程调度器线程池大小
 *
 * 协程库内部用于执行协程任务的工作线程数量。
 * 设置为 1 表示单线程调度，适合 I/O 密集型场景。
 */
const int FIBER_THREAD_NUM = 1;

/**
 * @brief 是否使用调用者线程参与调度
 *
 * - true:  创建调度器的线程也会参与任务调度（节省一个线程）
 * - false: 创建调度器的线程不参与调度，只负责提交任务
 */
const bool FIBER_USE_CALLER_THREAD = false;

#endif  // CONFIG_H
