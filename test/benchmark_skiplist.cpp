#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../src/skipList/include/skipList.h"

using namespace std;
using namespace chrono;

int main() {
  int n = 1000000;
  int num_threads = 8;
  cout << "Generating " << n << " random keys..." << endl;
  cout << "Testing with " << num_threads << " threads concurrent write" << endl;

  vector<string> keys(n);
  vector<string> vals(n);
  for (int i = 0; i < n; i++) {
    keys[i] = to_string(rand());
    vals[i] = "val" + to_string(i);
  }

  int chunk_size = n / num_threads;

  cout << "--- Benchmarking std::map + std::mutex ---" << endl;
  map<string, string> m;
  mutex m_mtx;
  auto start_map = high_resolution_clock::now();
  vector<thread> threads;
  for (int t = 0; t < num_threads; t++) {
    threads.emplace_back([&, t]() {
      int start = t * chunk_size;
      int end = (t == num_threads - 1) ? n : (t + 1) * chunk_size;
      for (int i = start; i < end; i++) {
        lock_guard<mutex> lock(m_mtx);
        m[keys[i]] = vals[i];
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }
  auto end_map = high_resolution_clock::now();
  double map_time = duration_cast<milliseconds>(end_map - start_map).count();
  cout << "std::map insert 1e6 keys time: " << map_time << " ms" << endl;

  double map_throughput = n / (map_time / 1000.0);
  cout << "std::map Throughput: " << map_throughput << " ops/sec" << endl;

  cout << "--- Benchmarking SkipList (built-in mutex) ---" << endl;
  SkipList<string, string> sl(18);  // Max level 18
  auto start_sl = high_resolution_clock::now();
  threads.clear();
  for (int t = 0; t < num_threads; t++) {
    threads.emplace_back([&, t]() {
      int start = t * chunk_size;
      int end = (t == num_threads - 1) ? n : (t + 1) * chunk_size;
      for (int i = start; i < end; i++) {
        sl.insert_element(keys[i], vals[i]);
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }
  auto end_sl = high_resolution_clock::now();
  double sl_time = duration_cast<milliseconds>(end_sl - start_sl).count();
  cout << "SkipList insert 1e6 keys time: " << sl_time << " ms" << endl;

  double sl_throughput = n / (sl_time / 1000.0);
  cout << "SkipList Throughput: " << sl_throughput << " ops/sec" << endl;

  double perf_increase = ((sl_throughput - map_throughput) / map_throughput) * 100.0;
  cout << ">>> Performance Increase: " << perf_increase << "% <<<" << endl;

  return 0;
}
