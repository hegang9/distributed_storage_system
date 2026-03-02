#ifndef __MONSOON_TIMER_H__
#  define __MONSSON_TIMER_H__

#  include <memory>
#  include <set>
#  include <vector>
#  include "mutex.hpp"

namespace monsoon {
class TimerManager;

/**
 * @class Timer
 * @brief 定时器类，用于管理单个定时任务
 *
 * 继承自enable_shared_from_this<Timer>，支持获取自身的shared_ptr。
 * 定时器可以是一次性的或循环的，支持取消、刷新和重置操作。
 */
class Timer : public std::enable_shared_from_this<Timer> {
  friend class TimerManager;

 public:
  typedef std::shared_ptr<Timer> ptr;  ///< Timer智能指针类型

  /// 取消定时器，清除回调函数并从管理器中删除
  bool cancel();

  /// 刷新定时器，重新计算执行时间为当前时刻 + 执行周期
  bool refresh();

  /// 重置定时器的执行周期
  /// @param ms 新的执行周期(毫秒)
  /// @param from_now true表示下次触发时间从当前时刻开始计算，false表示从上一次触发时间开始计算
  bool reset(uint64_t ms, bool from_now);

 private:
  /// 完整构造函数，用于创建新定时器
  /// @param ms 执行周期(毫秒)
  /// @param cb 回调函数
  /// @param recuring 是否是循环定时器
  /// @param manager 定时器管理器指针
  Timer(uint64_t ms, std::function<void()> cb, bool recuring, TimerManager *manager);

  /// 简化构造函数，仅用于比较时使用
  /// @param next 执行时间戳
  Timer(uint64_t next);

  bool recurring_ = false;           ///< 是否是循环定时器标志
  uint64_t ms_ = 0;                  ///< 定时器执行周期(毫秒)
  uint64_t next_ = 0;                ///< 下一次执行的精确时间戳(毫秒)
  std::function<void()> cb_;         ///< 定时器触发时的回调函数
  TimerManager *manager_ = nullptr;  ///< 所属的定时器管理器

 private:
  /// 定时器比较器，用于std::set排序
  /// 根据执行时间(next_)升序排列，执行时间相同时按地址比较
  struct Comparator {
    bool operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const;
  };
};

/**
 * @class TimerManager
 * @brief 定时器管理器，负责管理所有定时器
 *
 * 使用std::set维护定时器集合，按执行时间排序。
 * 提供添加、删除、查询定时器等功能。
 * 需要子类实现OnTimerInsertedAtFront虚函数，在有新定时器插入到首部时进行通知。
 */
class TimerManager {
  friend class Timer;

 public:
  TimerManager();
  virtual ~TimerManager();

  /// 添加定时器
  /// @param ms 执行周期(毫秒)
  /// @param cb 回调函数
  /// @param recuring 是否循环执行
  /// @return 返回新创建的定时器智能指针
  Timer::ptr addTimer(uint64_t ms, std::function<void()> cb, bool recuring = false);

  /// 添加条件定时器，当weak_ptr指向的对象被释放时，定时器将被禁用
  /// @param ms 执行周期(毫秒)
  /// @param cb 回调函数
  /// @param weak_cond 条件对象的weak_ptr，用于检查对象是否仍存活
  /// @param recurring 是否循环执行
  /// @return 返回新创建的定时器智能指针
  Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond,
                               bool recurring = false);

  /// 获取到最近一个待执行定时器的时间间隔
  /// @return 返回毫秒数，如果没有定时器则返回~0ull，如果有定时器已过期则返回0
  uint64_t getNextTimer();

  /// 获取所有已过期的定时器的回调函数列表
  /// @param cbs 输出参数，存放已过期定时器的回调函数
  void listExpiredCb(std::vector<std::function<void()>> &cbs);

  /// 检查是否还有未执行的定时器
  /// @return true表示有定时器，false表示没有定时器
  bool hasTimer();

 protected:
  /// 虚函数：当有新的定时器插入到定时器集合首部时调用
  /// 子类应该在此函数中进行相应的通知操作(如唤醒epoll等)
  virtual void OnTimerInsertedAtFront() = 0;

  /// 将定时器添加到管理器的定时器集合中
  /// @param val 要添加的定时器智能指针
  /// @param lock 获得的写锁，函数内会解锁并可能触发OnTimerInsertedAtFront
  void addTimer(Timer::ptr val, RWMutex::WriteLock &lock);

 private:
  /// 检测系统时间是否被往回调整
  /// 如果当前时间比上次记录的时间小1小时以上，则判定为时间被调后
  /// @param now_ms 当前时间戳(毫秒)
  /// @return true表示检测到时间回调，false表示正常
  bool detectClockRolllover(uint64_t now_ms);

  RWMutex mutex_;                                   ///< 读写互斥锁，保护定时器集合的线程安全
  std::set<Timer::ptr, Timer::Comparator> timers_;  ///< 定时器集合，按执行时间升序排列
  bool tickled_ = false;                            ///< 标志位，表示是否已触发OnTimerInsertedAtFront
  uint64_t previouseTime_ = 0;                      ///< 上一次检查的时间戳(毫秒)，用于检测时间回调
};
}  // namespace monsoon

#endif