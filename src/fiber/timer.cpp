#include "timer.hpp"
#include "utils.hpp"

namespace monsoon {

/// Timer比较器的operator()实现
/// 用于std::set排序定时器
/// 返回true表示lhs应在rhs之前
bool Timer::Comparator::operator()(const Timer::ptr &lhs, const Timer::ptr &rhs) const {
  // 两个都是null指针，不排序
  if (!lhs && !rhs) {
    return false;
  }
  // lhs为null，null排在前面
  if (!lhs) {
    return true;
  }
  // rhs为null，lhs排在后面
  if (!rhs) {
    return false;
  }
  // 按执行时间升序排列
  if (lhs->next_ < rhs->next_) {
    return true;
  }
  // 执行时间较晚
  if (rhs->next_ < lhs->next_) {
    return false;
  }
  // 执行时间相同，按地址比较保证唯一性
  return lhs.get() < rhs.get();
}

/// Timer完整构造函数
/// 初始化定时器的各个属性，计算第一次执行时间
Timer::Timer(uint64_t ms, std::function<void()> cb, bool recuring, TimerManager *manager)
    : recurring_(recuring), ms_(ms), cb_(cb), manager_(manager) {
  // 计算下一次执行时间 = 当前时间 + 执行周期
  next_ = GetElapsedMS() + ms_;
}

/// Timer简化构造函数，仅用于创建临时定时器用于比较
Timer::Timer(uint64_t next) : next_(next) {}

/// 取消定时器
/// 将回调函数清空并从管理器的定时器集合中删除
bool Timer::cancel() {
  // 获取写锁保护临界区
  RWMutex::WriteLock lock(manager_->mutex_);
  // 检查回调是否存在
  if (cb_) {
    // 清空回调函数
    cb_ = nullptr;
    // 从定时器集合中查找该定时器
    auto it = manager_->timers_.find(shared_from_this());
    // 删除该定时器
    manager_->timers_.erase(it);
    return true;
  }
  return false;
}

/// 刷新定时器
/// 重新计算执行时间为当前时刻 + 执行周期，相当于重启定时器
bool Timer::refresh() {
  // 获取写锁保护临界区
  RWMutex::WriteLock lock(manager_->mutex_);
  // 检查回调是否存在
  if (!cb_) {
    return false;
  }
  // 从集合中查找该定时器
  auto it = manager_->timers_.find(shared_from_this());
  // 如果定时器不在集合中，返回失败
  if (it == manager_->timers_.end()) {
    return false;
  }
  // 从集合中删除该定时器(因为要改变执行时间，需要重新插入以保持排序)
  manager_->timers_.erase(it);
  // 重新计算执行时间
  next_ = GetElapsedMS() + ms_;
  // 重新插入到集合中
  manager_->timers_.insert(shared_from_this());
  return true;
}

/// 重置定时器的执行周期
/// from_now = true: 下次触发时间从当前时刻开始计算
/// from_now = false: 下次触发时间从上一次开始计算
bool Timer::reset(uint64_t ms, bool from_now) {
  // 如果新周期等于旧周期且不是从当前时刻开始，则无需重置
  if (ms == ms_ && !from_now) {
    return true;
  }
  // 获取写锁保护临界区
  RWMutex::WriteLock lock(manager_->mutex_);
  // 检查回调是否存在
  if (!cb_) {
    return true;
  }
  // 从集合中查找该定时器
  auto it = manager_->timers_.find(shared_from_this());
  // 如果定时器不在集合中，返回失败
  if (it == manager_->timers_.end()) {
    return false;
  }
  // 从集合中删除该定时器(需要重新插入)
  manager_->timers_.erase(it);

  uint64_t start = 0;
  // 根据from_now标志决定计算起点
  if (from_now) {
    // 从当前时刻开始计算
    start = GetElapsedMS();
  } else {
    // 从上一次执行时间开始计算(next_ - ms_得到上一次执行时间)
    start = next_ - ms_;
  }
  // 更新执行周期
  ms_ = ms;
  // 计算新的执行时间
  next_ = start + ms_;
  // 重新添加到集合中
  manager_->addTimer(shared_from_this(), lock);
  return true;
}

/// TimerManager构造函数
/// 初始化上次执行时间为当前时间，用于检测时间回调
TimerManager::TimerManager() { previouseTime_ = GetElapsedMS(); }

/// TimerManager析构函数
TimerManager::~TimerManager() {}

/// 添加定时器到管理器
/// 创建新的定时器并添加到集合中
Timer::ptr TimerManager::addTimer(uint64_t ms, std::function<void()> cb, bool recurring) {
  // 创建新的定时器对象
  Timer::ptr timer(new Timer(ms, cb, recurring, this));
  // 获取写锁保护定时器集合
  RWMutex::WriteLock lock(mutex_);
  // 调用内部addTimer函数将定时器添加到集合中
  addTimer(timer, lock);
  return timer;
}

/// 条件定时器的回调包装函数
/// 只有当weak_ptr指向的对象仍存活时才执行回调
static void OnTimer(std::weak_ptr<void> weak_cond, std::function<void()> cb) {
  // 尝试锁定weak_ptr，获取对象的shared_ptr
  std::shared_ptr<void> tmp = weak_cond.lock();
  // 如果对象仍存活，则执行回调
  if (tmp) {
    cb();
  }
  // 否则什么都不做，定时器会被自动清理
}

/// 添加条件定时器
/// 条件定时器会在weak_ptr指向的对象被释放时自动停止执行
Timer::ptr TimerManager::addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond,
                                           bool recurring) {
  // 用OnTimer包装回调函数，添加条件检查
  return addTimer(ms, std::bind(&OnTimer, weak_cond, cb), recurring);
}

/// 获取到最近一个待执行定时器的时间间隔
/// 返回的时间可用于epoll_wait等函数的超时参数
uint64_t TimerManager::getNextTimer() {
  // 获取读锁
  RWMutex::ReadLock lock(mutex_);
  // 清除tickled标志
  tickled_ = false;
  // 如果没有定时器，返回最大值
  if (timers_.empty()) {
    return ~0ull;
  }
  // 获取第一个定时器(执行时间最早的)
  const Timer::ptr &next = *timers_.begin();
  // 获取当前时间
  uint64_t now_ms = GetElapsedMS();
  // 如果第一个定时器已经过期，返回0
  if (now_ms >= next->next_) {
    return 0;
  } else {
    // 否则返回到该定时器的时间间隔
    return next->next_ - now_ms;
  }
}

/// 获取所有已过期的定时器的回调函数
/// 处理循环定时器，重新计算执行时间并重新插入到集合中
void TimerManager::listExpiredCb(std::vector<std::function<void()>> &cbs) {
  // 获取当前时间
  uint64_t now_ms = GetElapsedMS();
  // 用于存储已过期的定时器
  std::vector<Timer::ptr> expired;

  // 第一次检查是否有定时器(使用读锁)
  {
    RWMutex::ReadLock lock(mutex_);
    if (timers_.empty()) {
      return;
    }
  }

  // 获取写锁以修改定时器集合
  RWMutex::WriteLock lock(mutex_);
  // 再次检查是否有定时器
  if (timers_.empty()) {
    return;
  }

  // 检测是否发生时间回调
  bool rollover = false;
  if (detectClockRolllover(now_ms)) {
    rollover = true;
  }

  // 如果没有时间回调且最早的定时器还没到期，直接返回
  if (!rollover && ((*timers_.begin())->next_ > now_ms)) {
    return;
  }

  // 创建临时定时器用于查找
  Timer::ptr now_timer(new Timer(now_ms));

  // 如果发生时间回调，从end开始；否则用lower_bound查找第一个执行时间不早于now_ms的定时器
  auto it = rollover ? timers_.end() : timers_.lower_bound(now_timer);

  // 跳过执行时间等于now_ms的定时器(由于浮点精度问题)
  while (it != timers_.end() && (*it)->next_ == now_ms) {
    ++it;
  }

  // 提取所有早于it的定时器到expired向量中
  expired.insert(expired.begin(), timers_.begin(), it);
  // 从集合中删除已过期的定时器
  timers_.erase(timers_.begin(), it);

  // 预留空间以提高性能
  cbs.reserve(expired.size());

  // 处理每个已过期的定时器
  for (auto &timer : expired) {
    // 添加回调函数到输出列表
    cbs.push_back(timer->cb_);
    // 如果是循环定时器
    if (timer->recurring_) {
      // 重新计算下一次执行时间
      timer->next_ = now_ms + timer->ms_;
      // 重新插入到集合中
      timers_.insert(timer);
    } else {
      // 非循环定时器，清空回调
      timer->cb_ = nullptr;
    }
  }
}

/// 向定时器集合中添加定时器的内部实现
/// 该函数会检查新定时器是否插入到集合首部，如果是则触发OnTimerInsertedAtFront回调
void TimerManager::addTimer(Timer::ptr val, RWMutex::WriteLock &lock) {
  // 将定时器插入到集合中，返回迭代器指向插入位置
  auto it = timers_.insert(val).first;

  // 检查该定时器是否插入到了集合首部，且还未触发过OnTimerInsertedAtFront
  bool at_front = (it == timers_.begin()) && !tickled_;

  // 如果插入到了首部，标记tickled_为true
  if (at_front) {
    tickled_ = true;
  }

  // 解锁，使得OnTimerInsertedAtFront可以获取锁进行必要的操作
  lock.unlock();

  // 如果插入到了首部，触发OnTimerInsertedAtFront回调
  // 子类应在此函数中进行必要的通知操作，如唤醒epoll
  if (at_front) {
    OnTimerInsertedAtFront();
  }
}

/// 检测系统时间是否被往回调整
/// 用于处理系统时间被人为调整的情况
bool TimerManager::detectClockRolllover(uint64_t now_ms) {
  // 初始化rollover标志为false
  bool rollover = false;

  // 检查当前时间是否比上次记录的时间小，且差值大于1小时
  // 这表明系统时间被往回调整了
  if (now_ms < previouseTime_ && now_ms < (previouseTime_ - 60 * 60 * 1000)) {
    rollover = true;
  }

  // 更新上次检查的时间戳
  previouseTime_ = now_ms;

  return rollover;
}

/// 检查定时器集合是否还有未执行的定时器
bool TimerManager::hasTimer() {
  // 获取读锁
  RWMutex::ReadLock lock(mutex_);
  // 如果集合不为空，则返回true
  return !timers_.empty();
}

}  // namespace monsoon