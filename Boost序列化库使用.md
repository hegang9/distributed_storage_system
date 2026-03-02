# Boost 序列化库使用指南

## 目录

- [1. Boost 序列化库概述](#1-boost-序列化库概述)
- [2. 核心概念](#2-核心概念)
- [3. 基础使用](#3-基础使用)
- [4. 序列化归档类型](#4-序列化归档类型)
- [5. 实战示例：跳表序列化](#5-实战示例跳表序列化)
- [6. 高级特性](#6-高级特性)
- [7. 常见问题与最佳实践](#7-常见问题与最佳实践)

---

## 1. Boost 序列化库概述

### 1.1 什么是序列化？

**序列化（Serialization）** 是将数据结构或对象的状态转换为可存储或可传输格式的过程。反序列化（Deserialization）则是将序列化的数据恢复为原始数据结构的逆过程。

**主要用途：**
- 数据持久化（保存到磁盘）
- 网络传输（RPC、分布式系统）
- 进程间通信（IPC）
- 对象深拷贝

### 1.2 Boost.Serialization 简介

Boost.Serialization 是 C++ Boost 库中用于对象序列化的强大工具，提供了：

- **类型安全**：编译时类型检查
- **非侵入式**：可以序列化不修改源码的第三方类
- **多种格式支持**：文本、XML、二进制等
- **版本控制**：支持数据结构的版本演进
- **自动化**：自动处理指针、继承、多态等复杂情况

---

## 2. 核心概念

### 2.1 归档（Archive）

归档是序列化的核心概念，类似于"数据容器"：

- **输出归档（Output Archive）**：将对象序列化为特定格式
- **输入归档（Input Archive）**：从特定格式反序列化为对象

```cpp
// 输出归档示例
std::ostringstream oss;
boost::archive::text_oarchive oa(oss);  // 创建文本输出归档
oa << myObject;  // 序列化对象

// 输入归档示例
std::istringstream iss(serializedData);
boost::archive::text_iarchive ia(iss);  // 创建文本输入归档
ia >> myObject;  // 反序列化对象
```

### 2.2 序列化函数

每个可序列化的类需要提供序列化接口，有两种方式：

#### 方式一：侵入式（成员函数）

```cpp
class MyClass {
private:
    friend class boost::serialization::access;
    
    template<class Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar & member1;
        ar & member2;
    }
    
    int member1;
    std::string member2;
};
```

#### 方式二：非侵入式（自由函数）

```cpp
class MyClass {
public:
    int member1;
    std::string member2;
};

namespace boost {
namespace serialization {
    template<class Archive>
    void serialize(Archive& ar, MyClass& obj, const unsigned int version) {
        ar & obj.member1;
        ar & obj.member2;
    }
}
}
```

### 2.3 序列化操作符

Boost 提供了三种序列化操作符：

| 操作符 | 用途 | 示例 |
|--------|------|------|
| `&` | 自动选择保存/加载 | `ar & data;` |
| `<<` | 仅保存（序列化） | `ar << data;` |
| `>>` | 仅加载（反序列化） | `ar >> data;` |

推荐使用 `&` 操作符，它能根据归档类型自动选择正确的操作。

---

## 3. 基础使用

### 3.1 安装与引入

**安装 Boost（Ubuntu/Debian）：**
```bash
sudo apt-get install libboost-all-dev
```

**CMake 配置：**
```cmake
find_package(Boost REQUIRED COMPONENTS serialization)
target_link_libraries(your_target Boost::serialization)
```

**头文件引入：**
```cpp
#include <boost/archive/text_oarchive.hpp>    // 文本输出归档
#include <boost/archive/text_iarchive.hpp>    // 文本输入归档
#include <boost/archive/binary_oarchive.hpp>  // 二进制输出归档
#include <boost/archive/binary_iarchive.hpp>  // 二进制输入归档
#include <boost/serialization/vector.hpp>     // 序列化 std::vector
#include <boost/serialization/string.hpp>     // 序列化 std::string
#include <boost/serialization/map.hpp>        // 序列化 std::map
```

### 3.2 基础示例：序列化简单对象

```cpp
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <sstream>
#include <iostream>

class Person {
private:
    friend class boost::serialization::access;
    
    template<class Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar & name;
        ar & age;
        ar & salary;
    }
    
public:
    std::string name;
    int age;
    double salary;
    
    Person() : age(0), salary(0.0) {}
    Person(std::string n, int a, double s) : name(n), age(a), salary(s) {}
    
    void print() const {
        std::cout << "Name: " << name 
                  << ", Age: " << age 
                  << ", Salary: " << salary << std::endl;
    }
};

int main() {
    // ========== 序列化 ==========
    Person p1("Alice", 30, 50000.0);
    std::ostringstream oss;
    {
        boost::archive::text_oarchive oa(oss);
        oa << p1;
    }
    std::string serializedData = oss.str();
    std::cout << "序列化数据：\n" << serializedData << std::endl;
    
    // ========== 反序列化 ==========
    Person p2;
    std::istringstream iss(serializedData);
    {
        boost::archive::text_iarchive ia(iss);
        ia >> p2;
    }
    std::cout << "反序列化对象：\n";
    p2.print();
    
    return 0;
}
```

**输出示例：**
```
序列化数据：
22 serialization::archive 17 0 0 5 Alice 30 50000
反序列化对象：
Name: Alice, Age: 30, Salary: 50000
```

---

## 4. 序列化归档类型

### 4.1 文本归档（Text Archive）

**特点：**
- 人类可读
- 跨平台兼容性好
- 体积较大
- 速度较慢

```cpp
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

// 序列化到文件
std::ofstream ofs("data.txt");
boost::archive::text_oarchive oa(ofs);
oa << myObject;

// 从文件反序列化
std::ifstream ifs("data.txt");
boost::archive::text_iarchive ia(ifs);
ia >> myObject;
```

### 4.2 二进制归档（Binary Archive）

**特点：**
- 紧凑高效
- 速度快
- 不可读
- 平台相关性需注意

```cpp
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>

std::ofstream ofs("data.bin", std::ios::binary);
boost::archive::binary_oarchive oa(ofs);
oa << myObject;

std::ifstream ifs("data.bin", std::ios::binary);
boost::archive::binary_iarchive ia(ifs);
ia >> myObject;
```

### 4.3 XML 归档（XML Archive）

**特点：**
- 高度可读
- 自描述性强
- 体积最大
- 便于调试

```cpp
#include <boost/archive/xml_oarchive.hpp>
#include <boost/archive/xml_iarchive.hpp>

std::ofstream ofs("data.xml");
boost::archive::xml_oarchive oa(ofs);
oa << BOOST_SERIALIZATION_NVP(myObject);  // 需要命名

std::ifstream ifs("data.xml");
boost::archive::xml_iarchive ia(ifs);
ia >> BOOST_SERIALIZATION_NVP(myObject);
```

---

## 5. 实战示例：跳表序列化

### 5.1 问题背景

在分布式 KV 存储系统中，跳表（Skip List）需要支持：
- 将内存中的所有键值对序列化为字符串
- 通过网络传输或持久化到磁盘
- 从序列化数据恢复跳表结构

### 5.2 设计思路

**核心挑战：**
1. **跳表结构复杂**：包含多层指针、随机层级等，直接序列化困难
2. **指针无法序列化**：`forward` 指针数组无法直接传输
3. **需要轻量化**：只序列化必要数据（键值对），忽略结构信息

**解决方案：**
- 创建辅助类 `SkipListDump`，仅存储键值对的两个向量
- 序列化时遍历第 0 层链表，提取所有键值对
- 反序列化时重新插入，让跳表自动重建多层结构

### 5.3 代码实现

#### 步骤 1：定义辅助序列化类

```cpp
template <typename K, typename V>
class SkipListDump {
private:
    friend class boost::serialization::access;
    
    // Boost 序列化函数
    template <class Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar & keyDumpVt_;  // 序列化键向量
        ar & valDumpVt_;  // 序列化值向量
    }

public:
    std::vector<K> keyDumpVt_;  // 存储所有键
    std::vector<V> valDumpVt_;  // 存储所有值
    
    // 插入节点的键值对
    void insert(const Node<K, V>& node) {
        keyDumpVt_.emplace_back(node.get_key());
        valDumpVt_.emplace_back(node.get_value());
    }
};
```

**设计要点：**
- 使用 `std::vector` 存储键值对，Boost 自动支持其序列化
- `serialize` 函数使用 `&` 操作符，同时支持保存和加载
- `friend class boost::serialization::access` 允许 Boost 访问私有成员

#### 步骤 2：跳表序列化函数

```cpp
template <typename K, typename V>
std::string SkipList<K, V>::dump_file() {
    // 从第 0 层第一个节点开始遍历
    Node<K, V>* node = this->_header->forward[0];
    SkipListDump<K, V> dumper;
    
    // 遍历所有节点，提取键值对
    while (node != nullptr) {
        dumper.insert(*node);
        node = node->forward[0];  // 移动到下一个节点
    }
    
    // 使用 Boost 序列化为字符串
    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);
    oa << dumper;
    
    return ss.str();  // 返回序列化字符串
}
```

**执行流程：**
1. **遍历第 0 层**：`forward[0]` 包含所有节点，按键顺序排列
2. **提取数据**：将每个节点的键值对添加到 `dumper`
3. **创建归档**：使用 `stringstream` 作为缓冲区
4. **序列化**：`oa << dumper` 触发 `serialize` 函数
5. **返回结果**：字符串可用于网络传输或文件存储

#### 步骤 3：跳表反序列化函数

```cpp
template <typename K, typename V>
void SkipList<K, V>::load_file(const std::string& dumpStr) {
    if (dumpStr.empty()) {
        return;
    }
    
    // 创建 dumper 对象
    SkipListDump<K, V> dumper;
    
    // 从字符串反序列化
    std::stringstream iss(dumpStr);
    boost::archive::text_iarchive ia(iss);
    ia >> dumper;
    
    // 将所有键值对重新插入跳表
    for (size_t i = 0; i < dumper.keyDumpVt_.size(); ++i) {
        insert_element(dumper.keyDumpVt_[i], dumper.valDumpVt_[i]);
    }
}
```

**执行流程：**
1. **创建归档**：用序列化字符串初始化输入流
2. **反序列化**：`ia >> dumper` 恢复键值向量
3. **重建跳表**：逐个调用 `insert_element`，自动生成随机层级和多层索引

### 5.4 完整使用示例

```cpp
#include "skipList.h"
#include <iostream>

int main() {
    // ========== 创建并填充跳表 ==========
    SkipList<int, std::string> skipList(6);
    skipList.insert_element(1, "Apple");
    skipList.insert_element(10, "Banana");
    skipList.insert_element(30, "Cherry");
    skipList.insert_element(50, "Date");
    
    std::cout << "原始跳表：" << std::endl;
    skipList.display_list();
    
    // ========== 序列化 ==========
    std::string serializedData = skipList.dump_file();
    std::cout << "\n序列化数据：\n" << serializedData << std::endl;
    
    // ========== 模拟网络传输或持久化 ==========
    // ... 数据可以保存到文件、发送到网络等 ...
    
    // ========== 反序列化到新跳表 ==========
    SkipList<int, std::string> newSkipList(6);
    newSkipList.load_file(serializedData);
    
    std::cout << "\n反序列化后的跳表：" << std::endl;
    newSkipList.display_list();
    
    // ========== 验证数据完整性 ==========
    std::string value;
    if (newSkipList.search_element(30, value)) {
        std::cout << "\n查找键 30: " << value << std::endl;  // 输出: Cherry
    }
    
    return 0;
}
```

**输出示例：**
```
原始跳表：
*****Skip List*****
Level 0: 1:Apple;10:Banana;30:Cherry;50:Date;
Level 1: 1:Apple;30:Cherry;50:Date;
Level 2: 30:Cherry;

序列化数据：
22 serialization::archive 17 0 0 4 1 10 30 50 4 5 Apple 6 Banana 6 Cherry 4 Date

反序列化后的跳表：
*****Skip List*****
Level 0: 1:Apple;10:Banana;30:Cherry;50:Date;
Level 1: 10:Banana;50:Date;
Level 2: 50:Date;

查找键 30: Cherry
```

**注意事项：**
- 反序列化后的跳表层级结构可能与原跳表不同（因为层级是随机生成的）
- 但键值对的逻辑顺序和数据完整性完全一致
- 性能特性（$O(\log n)$ 查找）保持不变

---

## 6. 高级特性

### 6.1 序列化 STL 容器

Boost 内置支持常用 STL 容器，需引入对应头文件：

```cpp
#include <boost/serialization/vector.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/string.hpp>

class DataStore {
private:
    friend class boost::serialization::access;
    
    template<class Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar & data_map;
        ar & id_list;
        ar & tags;
    }
    
public:
    std::map<int, std::string> data_map;
    std::vector<int> id_list;
    std::set<std::string> tags;
};
```

### 6.2 指针序列化

Boost 能自动处理指针，包括：
- 原始指针
- 智能指针（`shared_ptr`、`unique_ptr`）
- 循环引用检测

```cpp
#include <boost/serialization/shared_ptr.hpp>

class Node {
private:
    friend class boost::serialization::access;
    
    template<class Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar & value;
        ar & next;  // 自动处理指针
    }
    
public:
    int value;
    std::shared_ptr<Node> next;
};
```

### 6.3 版本控制

支持数据结构的演进，向后兼容旧版本数据：

```cpp
class MyClass {
private:
    friend class boost::serialization::access;
    
    template<class Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar & member1;
        ar & member2;
        
        // 版本 1 新增字段
        if (version >= 1) {
            ar & member3;
        }
    }
    
public:
    int member1;
    std::string member2;
    double member3 = 0.0;  // 新增字段，旧版本默认为 0
};

// 声明当前版本号
BOOST_CLASS_VERSION(MyClass, 1)
```

### 6.4 分离保存/加载

当序列化和反序列化逻辑不同时：

```cpp
class MyClass {
private:
    friend class boost::serialization::access;
    
    template<class Archive>
    void save(Archive& ar, const unsigned int version) const {
        ar << computed_value();  // 保存计算结果
    }
    
    template<class Archive>
    void load(Archive& ar, const unsigned int version) {
        int temp;
        ar >> temp;
        set_value(temp);  // 加载后执行额外处理
    }
    
    BOOST_SERIALIZATION_SPLIT_MEMBER()
    
    int data;
};
```

---

## 7. 常见问题与最佳实践

### 7.1 常见错误

#### 错误 1：忘记包含容器序列化头文件

```cpp
// ❌ 错误：缺少头文件
std::vector<int> vec;
ar & vec;  // 编译错误

// ✅ 正确
#include <boost/serialization/vector.hpp>
std::vector<int> vec;
ar & vec;  // 正常工作
```

#### 错误 2：归档对象作用域问题

```cpp
// ❌ 错误：数据可能未完全写入
std::string result;
{
    std::ostringstream oss;
    boost::archive::text_oarchive oa(oss);
    oa << myObject;
    // 归档析构前可能未 flush
}

// ✅ 正确：确保归档先析构
std::string result;
{
    std::ostringstream oss;
    {
        boost::archive::text_oarchive oa(oss);
        oa << myObject;
    }  // 归档析构，确保数据写入
    result = oss.str();
}
```

#### 错误 3：跨平台二进制兼容性

```cpp
// ⚠️ 警告：二进制归档在不同平台/编译器间可能不兼容
// 原因：字节序、对齐方式、类型大小可能不同

// ✅ 建议：跨平台传输使用文本或 XML 归档
```

### 7.2 性能优化建议

#### 1. 使用二进制归档提升性能

```cpp
// 文本归档：慢但可读
boost::archive::text_oarchive oa_text(oss);

// 二进制归档：快但不可读（推荐生产环境）
boost::archive::binary_oarchive oa_binary(oss);
```

#### 2. 预分配内存

```cpp
std::ostringstream oss;
oss.str().reserve(EXPECTED_SIZE);  // 减少内存重分配
boost::archive::binary_oarchive oa(oss);
```

#### 3. 使用 `emplace_back` 而非 `push_back`

```cpp
// ❌ 低效
dumper.keyDumpVt_.push_back(node.get_key());

// ✅ 高效
dumper.keyDumpVt_.emplace_back(node.get_key());
```

### 7.3 线程安全

Boost.Serialization **不是线程安全的**：

```cpp
class ThreadSafeSkipList {
private:
    SkipList<K, V> skipList;
    mutable std::mutex mtx;
    
public:
    std::string dump_file() {
        std::lock_guard<std::mutex> lock(mtx);
        return skipList.dump_file();
    }
    
    void load_file(const std::string& data) {
        std::lock_guard<std::mutex> lock(mtx);
        skipList.load_file(data);
    }
};
```

### 7.4 调试技巧

#### 使用文本归档调试

```cpp
// 开发阶段使用文本归档，方便查看序列化数据
std::cout << "序列化数据：\n" << serializedData << std::endl;

// 生产环境切换为二进制归档
```

#### 验证反序列化结果

```cpp
// 双向验证
std::string data1 = obj1.serialize();
Object obj2;
obj2.deserialize(data1);
std::string data2 = obj2.serialize();

assert(data1 == data2);  // 确保序列化幂等性
```

### 7.5 最佳实践总结

| 场景 | 推荐方案 |
|------|----------|
| **持久化到磁盘** | 二进制归档（性能优先）或文本归档（兼容性优先） |
| **网络传输** | 二进制归档 + 压缩算法 |
| **跨语言通信** | 使用 Protobuf/JSON，不用 Boost |
| **调试开发** | 文本归档或 XML 归档 |
| **版本迭代** | 使用版本控制机制 |
| **大数据量** | 二进制归档 + 流式处理 |

---

## 参考资料

- [Boost.Serialization 官方文档](https://www.boost.org/doc/libs/release/libs/serialization/)
- [Boost 中文文档](https://github.com/boostorg/serialization)
- 本项目跳表实现：`src/skipList/include/skipList.h`

---

**最后更新**：2026-02-05
