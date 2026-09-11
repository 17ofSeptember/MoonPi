#pragma once
#include "i2c.hpp"
#include <atomic>
#include <bme280.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <thread>
namespace moonpi {
class Bme280Sensor {
  II2cController &bus_;
  std::string node_, error_;
  bme280_dev dev_{};
  bool initialized_ = false;
  std::function<bool()> cancelled_;
  static int8_t read(uint8_t, uint8_t *, uint32_t, void *);
  static int8_t write(uint8_t, const uint8_t *, uint32_t, void *);
  static void delay(uint32_t, void *);
  void check(int8_t);

public:
  Bme280Sensor(II2cController &, std::string, std::function<bool()>);
  Json sample();
};
class SensorWorker {
  std::unique_ptr<II2cController> bus_;
  std::mutex queue_mutex_, io_mutex_;
  std::condition_variable wake_;
  struct Job {
    uint64_t generation;
    std::string node;
  };
  std::deque<Job> jobs_;
  std::deque<std::pair<std::string, Json>> results_;
  std::map<std::string, std::unique_ptr<Bme280Sensor>> sensors_;
  std::atomic<uint64_t> generation_{0};
  bool release_ = false;
  std::jthread thread_;
  void loop(std::stop_token);

public:
  explicit SensorWorker(std::unique_ptr<II2cController>);
  ~SensorWorker();
  void configure(const std::vector<I2cLease> &);
  void cancel();
  bool request(const std::string &);
  std::vector<std::pair<std::string, Json>> drain();
};
} // namespace moonpi
