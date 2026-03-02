#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>

// 定义持久化存储文件的路径
#define STORE_FILE "store/dumpFile"

// 定义键值对在文件中的分隔符
static std::string delimiter = ":";

/**
 * @brief 跳表节点类模板
 * @tparam K 键的类型
 * @tparam V 值的类型
 * 
 * 跳表的每个节点包含键值对,以及指向不同层级下一个节点的指针数组
 */
template <typename K, typename V>
class Node {
 public:
  // 默认构造函数
  Node() {}

  // 构造函数:创建指定键值和层级的节点
  Node(K k, V v, int);

  // 析构函数:释放forward指针数组
  ~Node();

  // 获取节点的键
  K get_key() const;

  // 获取节点的值
  V get_value() const;

  // 设置节点的值
  void set_value(V);

  // 指针数组,存储该节点在不同层级指向的下一个节点
  // forward[i]表示第i层指向的下一个节点
  Node<K, V> **forward;

  // 节点的层级高度
  int node_level;

 private:
  K key;    // 节点存储的键
  V value;  // 节点存储的值
};

/**
 * @brief Node构造函数实现
 * @param k 节点的键
 * @param v 节点的值
 * @param level 节点的层级(0到level)
 */
template <typename K, typename V>
Node<K, V>::Node(const K k, const V v, int level) {
  this->key = k;
  this->value = v;
  this->node_level = level;

  // 分配forward数组,大小为level+1,因为数组索引从0到level
  this->forward = new Node<K, V> *[level + 1];

  // 将forward数组初始化为NULL
  memset(this->forward, 0, sizeof(Node<K, V> *) * (level + 1));
};

/**
 * @brief Node析构函数,释放forward指针数组
 */
template <typename K, typename V>
Node<K, V>::~Node() {
  delete[] forward;
};

/**
 * @brief 获取节点的键
 * @return 返回节点的键
 */
template <typename K, typename V>
K Node<K, V>::get_key() const {
  return key;
};

/**
 * @brief 获取节点的值
 * @return 返回节点的值
 */
template <typename K, typename V>
V Node<K, V>::get_value() const {
  return value;
};

/**
 * @brief 设置节点的值
 * @param value 新的值
 */
template <typename K, typename V>
void Node<K, V>::set_value(V value) {
  this->value = value;
};

/**
 * @brief 跳表序列化辅助类
 * @tparam K 键的类型
 * @tparam V 值的类型
 * 
 * 用于将跳表数据序列化到字符串或从字符串反序列化
 * 使用boost序列化库实现持久化
 */
template <typename K, typename V>
class SkipListDump {
 public:
  // 声明boost序列化访问权限
  friend class boost::serialization::access;

  /**
   * @brief 序列化函数,由boost序列化库调用
   * @param ar 归档对象
   * @param version 版本号
   */
  template <class Archive>
  void serialize(Archive &ar, const unsigned int version) {
    ar &keyDumpVt_;  // 序列化键向量
    ar &valDumpVt_;  // 序列化值向量
  }
  
  std::vector<K> keyDumpVt_;  // 存储所有节点的键
  std::vector<V> valDumpVt_;  // 存储所有节点的值

 public:
  // 将节点插入到dump向量中
  void insert(const Node<K, V> &node);
};

/**
 * @brief 将节点的键值对插入到dump向量中
 * @param node 要插入的节点
 */
template <typename K, typename V>
void SkipListDump<K, V>::insert(const Node<K, V> &node) {
  keyDumpVt_.emplace_back(node.get_key());    // 将键添加到键向量
  valDumpVt_.emplace_back(node.get_value());  // 将值添加到值向量
}

/**
 * @brief 跳表(Skip List)类模板
 * @tparam K 键的类型
 * @tparam V 值的类型
 * 
 * 跳表是一种概率型数据结构,通过多层索引实现快速查找
 * 支持插入、删除、查找操作,平均时间复杂度O(log n)
 * 线程安全:关键操作使用互斥锁保护
 */
template <typename K, typename V>
class SkipList {
 public:
  // 构造函数:创建指定最大层级的跳表
  SkipList(int);
  
  // 析构函数:释放所有节点和资源
  ~SkipList();
  
  // 随机生成节点层级(1到_max_level之间)
  int get_random_level();
  
  // 创建新节点
  Node<K, V> *create_node(K, V, int);
  
  // 插入元素,如果键已存在则返回1,插入成功返回0
  int insert_element(K, V);
  
  // 打印显示跳表的所有层级
  void display_list();
  
  // 查找元素,找到返回true并设置value,否则返回false
  bool search_element(K, V &value);
  
  // 删除指定键的元素
  void delete_element(K);
  
  // 插入或更新元素(如果键存在则更新值,否则插入)
  void insert_set_element(K &, V &);
  
  // 将跳表数据序列化为字符串
  std::string dump_file();
  
  // 从字符串加载数据到跳表
  void load_file(const std::string &dumpStr);
  
  // 递归删除节点(从指定节点开始)
  void clear(Node<K, V> *);
  
  // 获取跳表中元素的数量
  int size();

 private:
  // 从字符串中解析出键和值(格式: "key:value")
  void get_key_value_from_string(const std::string &str, std::string *key, std::string *value);
  
  // 验证字符串格式是否有效
  bool is_valid_string(const std::string &str);

 private:
  // 跳表的最大层级数
  int _max_level;

  // 跳表当前的实际层级数(动态变化)
  int _skip_list_level;

  // 指向头节点的指针(哨兵节点,不存储实际数据)
  Node<K, V> *_header;

  // 文件写入流(用于持久化)
  std::ofstream _file_writer;
  
  // 文件读取流(用于加载数据)
  std::ifstream _file_reader;

  // 跳表中当前存储的元素数量
  int _element_count;

  // 互斥锁,保护跳表操作的线程安全
  std::mutex _mtx;
};

/**
 * @brief 创建新节点
 * @param k 节点的键
 * @param v 节点的值
 * @param level 节点的层级
 * @return 返回新创建的节点指针
 */
template <typename K, typename V>
Node<K, V> *SkipList<K, V>::create_node(const K k, const V v, int level) {
  Node<K, V> *n = new Node<K, V>(k, v, level);
  return n;
}

/**
 * @brief 在跳表中插入键值对
 * @param key 要插入的键
 * @param value 要插入的值
 * @return 如果键已存在返回1,插入成功返回0
 * 
 * 插入过程:
 * 1. 从最高层开始查找插入位置
 * 2. 记录每层需要修改的前驱节点
 * 3. 如果键已存在,返回1
 * 4. 随机生成新节点的层级
 * 5. 创建新节点并更新各层的指针
 * 
 * 示例(插入50):
 *                            +------------+
 *                            |  insert 50 |
 *                            +------------+
 * level 4     +-->1+                                                      100
 *                  |
 *                  |                      insert +----+
 * level 3         1+-------->10+---------------> | 50 |          70       100
 *                                                |    |
 *                                                |    |
 * level 2         1          10         30       | 50 |          70       100
 *                                                |    |
 *                                                |    |
 * level 1         1    4     10         30       | 50 |          70       100
 *                                                |    |
 *                                                |    |
 * level 0         1    4   9 10         30   40  | 50 |  60      70       100
 *                                                +----+
 */
template <typename K, typename V>
int SkipList<K, V>::insert_element(const K key, const V value) {
  _mtx.lock();  // 加锁,保证线程安全
  Node<K, V> *current = this->_header;

  // 创建update数组并初始化
  // update[i]存储第i层中,位于插入位置之前的节点
  // 这些节点的forward指针需要被更新
  Node<K, V> *update[_max_level + 1];
  memset(update, 0, sizeof(Node<K, V> *) * (_max_level + 1));

  // 从跳表的最高层开始向下查找
  for (int i = _skip_list_level; i >= 0; i--) {
    // 在当前层向右移动,直到下一个节点的键 >= 要插入的键
    while (current->forward[i] != NULL && current->forward[i]->get_key() < key) {
      current = current->forward[i];
    }
    // 记录第i层需要修改的节点
    update[i] = current;
  }

  // 到达第0层,current->forward[0]指向插入位置的下一个节点
  current = current->forward[0];

  // 如果找到了相同的键,说明元素已存在
  if (current != NULL && current->get_key() == key) {
    std::cout << "key: " << key << ", exists" << std::endl;
    _mtx.unlock();  // 解锁
    return 1;       // 返回1表示键已存在
  }

  // current为NULL表示到达了链表末尾
  // 或者current的键不等于key,说明需要在update[0]和current之间插入新节点
  if (current == NULL || current->get_key() != key) {
    // 为新节点随机生成一个层级
    int random_level = get_random_level();

    // 如果随机层级大于跳表当前层级,需要更新跳表层级
    // 新增的层级的前驱节点都是头节点
    if (random_level > _skip_list_level) {
      for (int i = _skip_list_level + 1; i < random_level + 1; i++) {
        update[i] = this->_header;  // 新层级的前驱节点设为头节点
      }
      _skip_list_level = random_level;  // 更新跳表的当前层级
    }

    // 创建新节点
    Node<K, V> *inserted_node = create_node(key, value, random_level);

    // 在各层插入新节点,更新指针
    for (int i = 0; i <= random_level; i++) {
      // 新节点的forward[i]指向原来update[i]的下一个节点
      inserted_node->forward[i] = update[i]->forward[i];
      // update[i]的forward[i]指向新节点
      update[i]->forward[i] = inserted_node;
    }
    std::cout << "Successfully inserted key:" << key << ", value:" << value << std::endl;
    _element_count++;  // 元素计数加1
  }
  _mtx.unlock();  // 解锁
  return 0;       // 返回0表示插入成功
}

/**
 * @brief 打印显示跳表的所有层级
 * 
 * 从第0层到最高层,逐层打印所有节点的键值对
 */
template <typename K, typename V>
void SkipList<K, V>::display_list() {
  std::cout << "\n*****Skip List*****"
            << "\n";
  // 遍历每一层
  for (int i = 0; i <= _skip_list_level; i++) {
    Node<K, V> *node = this->_header->forward[i];  // 获取第i层的第一个节点
    std::cout << "Level " << i << ": ";
    // 遍历第i层的所有节点
    while (node != NULL) {
      std::cout << node->get_key() << ":" << node->get_value() << ";";
      node = node->forward[i];  // 移动到下一个节点
    }
    std::cout << std::endl;
  }
}

/**
 * @brief 将跳表数据序列化为字符串
 * @return 返回序列化后的字符串
 * 
 * 使用boost序列化库将跳表中的所有键值对序列化
 * TODO: 后续可能需要考虑加锁的问题
 */
template <typename K, typename V>
std::string SkipList<K, V>::dump_file() {
  // std::cout << "dump_file-----------------" << std::endl;
  //
  //
  // _file_writer.open(STORE_FILE);
  
  // 从第0层的第一个节点开始遍历
  Node<K, V> *node = this->_header->forward[0];
  SkipListDump<K, V> dumper;  // 创建序列化辅助对象
  
  // 遍历第0层的所有节点,将键值对添加到dumper中
  while (node != nullptr) {
    dumper.insert(*node);  // 将节点插入到dumper
    // _file_writer << node->get_key() << ":" << node->get_value() << "\n";
    // std::cout << node->get_key() << ":" << node->get_value() << ";\n";
    node = node->forward[0];  // 移动到下一个节点
  }
  
  // 使用boost序列化库将dumper序列化为字符串
  std::stringstream ss;
  boost::archive::text_oarchive oa(ss);  // 创建文本输出归档
  oa << dumper;  // 序列化dumper对象
  return ss.str();  // 返回序列化后的字符串
  // _file_writer.flush();
  // _file_writer.close();
}

/**
 * @brief 从字符串加载数据到跳表
 * @param dumpStr 序列化的字符串数据
 * 
 * 使用boost序列化库将字符串反序列化,然后插入到跳表中
 */
template <typename K, typename V>
void SkipList<K, V>::load_file(const std::string &dumpStr) {
  // _file_reader.open(STORE_FILE);
  // std::cout << "load_file-----------------" << std::endl;
  // std::string line;
  // std::string* key = new std::string();
  // std::string* value = new std::string();
  // while (getline(_file_reader, line)) {
  //     get_key_value_from_string(line, key, value);
  //     if (key->empty() || value->empty()) {
  //         continue;
  //     }
  //     // Define key as int type
  //     insert_element(stoi(*key), *value);
  //     std::cout << "key:" << *key << "value:" << *value << std::endl;
  // }
  // delete key;
  // delete value;
  // _file_reader.close();

  // 如果输入字符串为空,直接返回
  if (dumpStr.empty()) {
    return;
  }
  
  // 创建dumper对象并反序列化
  SkipListDump<K, V> dumper;
  std::stringstream iss(dumpStr);  // 创建字符串流
  boost::archive::text_iarchive ia(iss);  // 创建文本输入归档
  ia >> dumper;  // 反序列化到dumper对象
  
  // 将dumper中的所有键值对插入到跳表中
  for (int i = 0; i < dumper.keyDumpVt_.size(); ++i) {
    insert_element(dumper.keyDumpVt_[i], dumper.keyDumpVt_[i]);
  }
}

/**
 * @brief 获取跳表中元素的数量
 * @return 返回元素数量
 */
template <typename K, typename V>
int SkipList<K, V>::size() {
  return _element_count;
}

/**
 * @brief 从字符串中解析出键和值
 * @param str 输入字符串,格式为"key:value"
 * @param key 输出参数,存储解析出的键
 * @param value 输出参数,存储解析出的值
 */
template <typename K, typename V>
void SkipList<K, V>::get_key_value_from_string(const std::string &str, std::string *key, std::string *value) {
  // 验证字符串格式
  if (!is_valid_string(str)) {
    return;
  }
  // 根据分隔符":"分割字符串
  *key = str.substr(0, str.find(delimiter));  // 提取键
  *value = str.substr(str.find(delimiter) + 1, str.length());  // 提取值
}

/**
 * @brief 验证字符串格式是否有效
 * @param str 要验证的字符串
 * @return 如果字符串非空且包含分隔符返回true,否则返回false
 */
template <typename K, typename V>
bool SkipList<K, V>::is_valid_string(const std::string &str) {
  // 字符串为空,无效
  if (str.empty()) {
    return false;
  }
  // 字符串中没有分隔符,无效
  if (str.find(delimiter) == std::string::npos) {
    return false;
  }
  return true;  // 有效
}

/**
 * @brief 从跳表中删除指定键的元素
 * @param key 要删除的键
 * 
 * 删除过程:
 * 1. 从最高层开始查找要删除的节点
 * 2. 记录每层需要修改的前驱节点
 * 3. 如果找到节点,更新各层的指针
 * 4. 删除节点并更新跳表层级
 */
template <typename K, typename V>
void SkipList<K, V>::delete_element(K key) {
  _mtx.lock();  // 加锁
  Node<K, V> *current = this->_header;
  // update数组存储每层需要修改的前驱节点
  Node<K, V> *update[_max_level + 1];
  memset(update, 0, sizeof(Node<K, V> *) * (_max_level + 1));

  // 从跳表的最高层开始向下查找
  for (int i = _skip_list_level; i >= 0; i--) {
    // 在当前层向右移动,直到下一个节点的键 >= 要删除的键
    while (current->forward[i] != NULL && current->forward[i]->get_key() < key) {
      current = current->forward[i];
    }
    // 记录第i层需要修改的节点
    update[i] = current;
  }

  // 移动到第0层的目标节点
  current = current->forward[0];
  
  // 如果找到了要删除的节点
  if (current != NULL && current->get_key() == key) {
    // 从第0层开始,删除各层中的当前节点
    for (int i = 0; i <= _skip_list_level; i++) {
      // 如果第i层的下一个节点不是目标节点,说明更高层没有该节点,退出循环
      if (update[i]->forward[i] != current) break;

      // 更新指针,跳过当前节点
      update[i]->forward[i] = current->forward[i];
    }

    // 删除没有元素的层级,降低跳表高度
    while (_skip_list_level > 0 && _header->forward[_skip_list_level] == 0) {
      _skip_list_level--;
    }

    std::cout << "Successfully deleted key " << key << std::endl;
    delete current;  // 释放节点内存
    _element_count--;  // 元素计数减1
  }
  _mtx.unlock();  // 解锁
  return;
}

/**
 * @brief 插入或更新元素
 * @param key 键的引用
 * @param value 值的引用
 * 
 * 功能:
 * - 如果键存在,先删除旧元素再插入新元素(实现更新)
 * - 如果键不存在,直接插入新元素
 * 
 * 与insert_element的区别:
 * - insert_element: 仅插入新元素,键存在时返回失败
 * - insert_set_element: 插入或更新,键存在时更新其值
 */
template <typename K, typename V>
void SkipList<K, V>::insert_set_element(K &key, V &value) {
  V oldValue;
  // 先查找键是否存在
  if (search_element(key, oldValue)) {
    delete_element(key);  // 如果存在,先删除旧元素
  }
  insert_element(key, value);  // 插入新元素
}

/**
 * @brief 在跳表中查找指定键的元素
 * @param key 要查找的键
 * @param value 输出参数,如果找到则存储对应的值
 * @return 找到返回true,未找到返回false
 * 
 * 查找过程:
 * 1. 从最高层开始向右查找
 * 2. 如果下一个节点的键大于等于目标键,则下降一层
 * 3. 重复直到第0层
 * 4. 检查第0层的节点是否匹配
 * 
 * 示例(查找60):
 *                            +------------+
 *                            |  select 60 |
 *                            +------------+
 * level 4     +-->1+                                                      100
 *                  |
 *                  |
 * level 3         1+-------->10+------------------>50+           70       100
 *                                                    |
 *                                                    |
 * level 2         1          10         30         50|           70       100
 *                                                    |
 *                                                    |
 * level 1         1    4     10         30         50|           70       100
 *                                                    |
 *                                                    |
 * level 0         1    4   9 10         30   40    50+-->60      70       100
 */
template <typename K, typename V>
bool SkipList<K, V>::search_element(K key, V &value) {
  std::cout << "search_element-----------------" << std::endl;
  Node<K, V> *current = _header;  // 从头节点开始  // 从头节点开始

  // 从跳表的最高层开始向下查找
  for (int i = _skip_list_level; i >= 0; i--) {
    // 在当前层向右移动,直到下一个节点为空或其键 >= 目标键
    while (current->forward[i] && current->forward[i]->get_key() < key) {
      current = current->forward[i];
    }
  }

  // 到达第0层,移动到可能匹配的节点
  current = current->forward[0];

  // 如果当前节点的键等于目标键,查找成功
  if (current and current->get_key() == key) {
    value = current->get_value();  // 将值存储到输出参数
    std::cout << "Found key: " << key << ", value: " << current->get_value() << std::endl;
    return true;  // 返回true表示找到
  }

  // 未找到
  std::cout << "Not Found Key:" << key << std::endl;
  return false;  // 返回false表示未找到
}


/**
 * @brief 跳表构造函数
 * @param max_level 跳表的最大层级数
 * 
 * 初始化跳表:
 * - 设置最大层级
 * - 当前层级设为0
 * - 元素计数设为0
 * - 创建头节点(哨兵节点)
 */
template <typename K, typename V>
SkipList<K, V>::SkipList(int max_level) {
  this->_max_level = max_level;      // 设置最大层级
  this->_skip_list_level = 0;        // 初始化当前层级为0
  this->_element_count = 0;          // 初始化元素计数为0

  // 创建头节点,键和值使用默认值
  // 头节点是哨兵节点,不存储实际数据
  K k;
  V v;
  this->_header = new Node<K, V>(k, v, _max_level);
};

/**
 * @brief 跳表析构函数
 * 
 * 清理资源:
 * - 关闭文件流
 * - 递归删除所有节点
 * - 删除头节点
 */
template <typename K, typename V>
SkipList<K, V>::~SkipList() {
  // 关闭文件写入流
  if (_file_writer.is_open()) {
    _file_writer.close();
  }
  // 关闭文件读取流
  if (_file_reader.is_open()) {
    _file_reader.close();
  }

  // 递归删除跳表中的所有节点(从第0层的第一个节点开始)
  if (_header->forward[0] != nullptr) {
    clear(_header->forward[0]);
  }
  // 删除头节点
  delete (_header);
}

/**
 * @brief 递归删除节点
 * @param cur 当前要删除的节点
 * 
 * 采用后序遍历的方式递归删除:
 * 1. 先递归删除后续节点
 * 2. 再删除当前节点
 */
template <typename K, typename V>
void SkipList<K, V>::clear(Node<K, V> *cur) {
  // 如果还有下一个节点,先递归删除下一个节点
  if (cur->forward[0] != nullptr) {
    clear(cur->forward[0]);
  }
  // 删除当前节点
  delete (cur);
}

/**
 * @brief 随机生成节点层级
 * @return 返回1到_max_level之间的随机层级
 * 
 * 使用概率算法:
 * - 每次有50%的概率增加一层
 * - 最高不超过_max_level
 * - 这种概率分布使得跳表的平均性能达到O(log n)
 */
template <typename K, typename V>
int SkipList<K, V>::get_random_level() {
  int k = 1;  // 最少一层
  // 每次有50%的概率继续增加层级
  while (rand() % 2) {
    k++;
  }
  // 确保不超过最大层级
  k = (k < _max_level) ? k : _max_level;
  return k;
};
// vim: et tw=100 ts=4 sw=4 cc=120
#endif  // SKIPLIST_H