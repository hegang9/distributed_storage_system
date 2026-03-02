// Monsoon 框架同步原语封装
// 提供线程同步机制：信号量、互斥锁、读写锁
// 以及 RAII 风格的局部锁模板
//
// 核心组件：
// 1. Semaphore：POSIX 信号量封装，用于线程间同步与资源计数
// 2. Mutex：互斥锁（pthread_mutex），保护临界区互斥访问
// 3. RWMutex：读写锁（pthread_rwlock），允许多读单写
// 4. ScopedLockImpl：RAII 局部锁模板，自动加锁/解锁
// 5. ReadScopedLockImpl / WriteScopedLockImpl：读写锁的 RAII 封装

#ifndef __MONSOON_MUTEX_H_
#define __MONSOON_MUTEX_H_

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <atomic>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

#include "noncopyable.hpp"
#include "utils.hpp"

namespace monsoon {

// ============================================================================
// 信号量封装：基于 POSIX sem_t 实现
// ============================================================================
// @brief 信号量（Semaphore）- 用于线程间同步与资源计数
//
// @note 使用场景：
//       - 限制同时访问资源的线程数（如连接池）
//       - 生产者-消费者模型中的通知机制
//       - 线程间的等待/通知
//
// @note 与 Mutex 的区别：
//       - Mutex：只能由持有者解锁（所有权语义）
//       - Semaphore：任何线程都可以 post（计数语义）
//
// TODO: 实现 wait/notify 方法（当前仅声明）
class Semaphore : Nonecopyable {
 public:
  // 构造时初始化信号量计数值
  // @param count 初始计数（默认 0，表示需要 wait 时会阻塞）
  Semaphore(uint32_t count = 0);
  ~Semaphore();

  // 等待（P 操作）：计数 -1，若为 0 则阻塞
  void wait();

  // 通知（V 操作）：计数 +1，唤醒等待线程
  void notify();

 private:
  sem_t semaphore_;  // POSIX 信号量
};

// ============================================================================
// RAII 局部锁模板：自动管理锁的生命周期
// ============================================================================
// @brief 通用 RAII 锁封装（适用于 Mutex 等支持 lock()/unlock() 的类型）
//
// @tparam T 锁类型（需要提供 lock() 和 unlock() 方法）
//
// @note 核心特性：
//       - 构造时自动加锁
//       - 析构时自动解锁
//       - 防止忘记释放锁导致的死锁
//
// @note 使用示例：
//       Mutex mtx;
//       {
//         ScopedLockImpl<Mutex> lock(mtx);  // 自动加锁
//         // 临界区代码
//       }  // 自动解锁
template <class T>
struct ScopedLockImpl {
 public:
  // 构造时立即加锁
  ScopedLockImpl(T &mutex) : m_(mutex) {
    m_.lock();
    isLocked_ = true;
  }

  // 手动加锁（若已锁定则忽略）
  void lock() {
    if (!isLocked_) {
      m_.lock();
      isLocked_ = true;
    }
  }

  // 手动解锁（若已解锁则忽略）
  void unlock() {
    if (isLocked_) {
      m_.unlock();
      isLocked_ = false;
    }
  }

  // 析构时自动解锁
  ~ScopedLockImpl() { unlock(); }

 private:
  T &m_;           // 被管理的锁对象引用
  bool isLocked_;  // 当前锁状态标记
};

// ============================================================================
// 读锁 RAII 封装：用于读写锁的读者模式
// ============================================================================
// @brief 读锁的 RAII 封装（适用于 RWMutex 等读写锁）
//
// @tparam T 读写锁类型（需要提供 rdlock() 和 unlock() 方法）
//
// @note 核心特性：
//       - 构造时获取读锁（允许多个读者并发）
//       - 析构时释放锁
//       - 用于读多写少的场景，提高并发性能
template <class T>
struct ReadScopedLockImpl {
 public:
  // 构造时获取读锁
  ReadScopedLockImpl(T &mutex) : mutex_(mutex) {
    mutex_.rdlock();
    isLocked_ = true;
  }

  // 析构时释放锁
  ~ReadScopedLockImpl() { unlock(); }

  // 手动加读锁
  void lock() {
    if (!isLocked_) {
      mutex_.rdlock();
      isLocked_ = true;
    }
  }

  // 手动解锁
  void unlock() {
    if (isLocked_) {
      mutex_.unlock();
      isLocked_ = false;
    }
  }

 private:
  T &mutex_;       // 读写锁引用
  bool isLocked_;  // 锁状态
};

// ============================================================================
// 写锁 RAII 封装：用于读写锁的写者模式
// ============================================================================
// @brief 写锁的 RAII 封装（适用于 RWMutex 等读写锁）
//
// @tparam T 读写锁类型（需要提供 wrlock() 和 unlock() 方法）
//
// @note 核心特性：
//       - 构造时获取写锁（独占访问）
//       - 析构时释放锁
//       - 写锁期间阻塞所有读者和写者
template <class T>
struct WriteScopedLockImpl {
 public:
  // 构造时获取写锁
  WriteScopedLockImpl(T &mutex) : mutex_(mutex) {
    mutex_.wrlock();
    isLocked_ = true;
  }

  // 析构时释放锁
  ~WriteScopedLockImpl() { unlock(); }

  // 手动加写锁
  void lock() {
    if (!isLocked_) {
      mutex_.wrlock();
      isLocked_ = true;
    }
  }

  // 手动解锁
  void unlock() {
    if (isLocked_) {
      mutex_.unlock();
      isLocked_ = false;
    }
  }

 private:
  T &mutex_;       // 读写锁引用
  bool isLocked_;  // 锁状态
};

// ============================================================================
// 互斥锁封装：基于 pthread_mutex_t 实现
// ============================================================================
// @brief 互斥锁（Mutex）- 保护临界区，确保互斥访问
//
// @note 使用场景：
//       - 保护共享数据的读写
//       - 确保临界区代码的原子性
//
// @note 使用示例：
//       Mutex mtx;
//       Mutex::Lock lock(mtx);  // RAII 自动加锁/解锁
//       // 临界区代码
class Mutex : Nonecopyable {
 public:
  typedef ScopedLockImpl<Mutex> Lock;  // RAII 局部锁类型别名

  // 构造时初始化 pthread_mutex
  Mutex() { CondPanic(0 == pthread_mutex_init(&m_, nullptr), "lock init success"); }

  // 加锁（阻塞直到获取锁）
  void lock() { CondPanic(0 == pthread_mutex_lock(&m_), "lock error"); }

  // 解锁
  void unlock() { CondPanic(0 == pthread_mutex_unlock(&m_), "unlock error"); }

  // 析构时销毁 pthread_mutex
  ~Mutex() { CondPanic(0 == pthread_mutex_destroy(&m_), "destroy lock error"); }

 private:
  pthread_mutex_t m_;  // POSIX 互斥锁
};

// ============================================================================
// 读写锁封装：基于 pthread_rwlock_t 实现
// ============================================================================
// @brief 读写锁（RWMutex）- 允许多读单写的并发控制
//
// @note 使用场景：
//       - 读多写少的共享数据保护
//       - 提高读操作的并发性能
//
// @note 语义：
//       - 读锁：多个线程可同时持有读锁
//       - 写锁：独占访问，阻塞所有读者和写者
//
// @note 使用示例：
//       RWMutex rwmtx;
//       // 读操作
//       {
//         RWMutex::ReadLock lock(rwmtx);  // 获取读锁
//         // 读取共享数据
//       }
//       // 写操作
//       {
//         RWMutex::WriteLock lock(rwmtx);  // 获取写锁
//         // 修改共享数据
//       }
class RWMutex : Nonecopyable {
 public:
  typedef ReadScopedLockImpl<RWMutex> ReadLock;    // 读锁 RAII 封装
  typedef WriteScopedLockImpl<RWMutex> WriteLock;  // 写锁 RAII 封装

  // 构造时初始化 pthread_rwlock
  RWMutex() { pthread_rwlock_init(&m_, nullptr); }

  // 析构时销毁 pthread_rwlock
  ~RWMutex() { pthread_rwlock_destroy(&m_); }

  // 获取读锁（可多个线程同时持有）
  void rdlock() { pthread_rwlock_rdlock(&m_); }

  // 获取写锁（独占访问）
  void wrlock() { pthread_rwlock_wrlock(&m_); }

  // 释放锁（读锁或写锁）
  void unlock() { pthread_rwlock_unlock(&m_); }

 private:
  pthread_rwlock_t m_;  // POSIX 读写锁
};
}  // namespace monsoon

#endif