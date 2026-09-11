#include "moonpi/sensors.hpp"
#include <algorithm>
#include <cmath>
namespace moonpi {
Bme280Sensor::Bme280Sensor(II2cController &bus, std::string node, std::function<bool()> cancelled)
    : bus_(bus), node_(std::move(node)), cancelled_(std::move(cancelled)) {
  dev_.intf = BME280_I2C_INTF;
  dev_.intf_ptr = this;
  dev_.read = read;
  dev_.write = write;
  dev_.delay_us = delay;
}
int8_t Bme280Sensor::read(uint8_t reg, uint8_t *data, uint32_t size, void *context) {
  auto &self = *static_cast<Bme280Sensor *>(context);
  try {
    if (self.cancelled_())
      throw Error("sensor.cancelled", "Sensor request cancelled");
    const auto result = self.bus_.transfer(self.node_, std::span(&reg, 1), size);
    if (result.size() != size)
      throw Error("sensor.short", "Incomplete sensor read");
    std::copy(result.begin(), result.end(), data);
    return 0;
  } catch (const std::exception &e) {
    self.error_ = e.what();
    return -1;
  }
}
int8_t Bme280Sensor::write(uint8_t reg, const uint8_t *data, uint32_t size, void *context) {
  auto &self = *static_cast<Bme280Sensor *>(context);
  try {
    if (self.cancelled_())
      throw Error("sensor.cancelled", "Sensor request cancelled");
    if (size > 255)
      throw Error("sensor.size", "Sensor write exceeds bound");
    std::vector<uint8_t> tx{reg};
    tx.insert(tx.end(), data, data + size);
    self.bus_.transfer(self.node_, tx, 0);
    return 0;
  } catch (const std::exception &e) {
    self.error_ = e.what();
    return -1;
  }
}
void Bme280Sensor::delay(uint32_t period, void *context) {
  auto &self = *static_cast<Bme280Sensor *>(context);
  const auto until = Clock::now() + std::chrono::microseconds(std::min(period, 100000u));
  while (!self.cancelled_() && Clock::now() < until)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
void Bme280Sensor::check(int8_t status) {
  if (status != BME280_OK || cancelled_()) {
    initialized_ = false;
    throw Error("sensor.bme280",
                error_.empty() ? "BME280 operation failed (check address, wiring and chip identity)"
                               : error_);
  }
}
Json Bme280Sensor::sample() {
  error_.clear();
  if (!initialized_) {
    check(bme280_init(&dev_));
    initialized_ = true;
  }
  bme280_settings settings{};
  settings.osr_h = BME280_OVERSAMPLING_1X;
  settings.osr_p = BME280_OVERSAMPLING_1X;
  settings.osr_t = BME280_OVERSAMPLING_1X;
  check(bme280_set_sensor_settings(BME280_SEL_OSR_HUM | BME280_SEL_OSR_PRESS | BME280_SEL_OSR_TEMP,
                                   &settings, &dev_));
  check(bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &dev_));
  uint32_t wait = 0;
  check(bme280_cal_meas_delay(&wait, &settings));
  delay(wait, this);
  uint8_t status = 0;
  check(read(0xf3, &status, 1, this));
  if (status & 0x09)
    throw Error("sensor.busy", "BME280 conversion not ready");
  bme280_data sample{};
  check(bme280_get_sensor_data(BME280_ALL, &sample, &dev_));
  if (!std::isfinite(sample.temperature) || !std::isfinite(sample.pressure) ||
      !std::isfinite(sample.humidity))
    throw Error("sensor.invalid", "Non-finite sensor reading");
  return {{"temperature", sample.temperature},
          {"pressure", sample.pressure / 100.0},
          {"humidity", sample.humidity},
          {"timestamp_ms", timestamp_ms()},
          {"state", "ACTIVE"}};
}
SensorWorker::SensorWorker(std::unique_ptr<II2cController> bus)
    : bus_(std::move(bus)), thread_([this](std::stop_token s) { loop(s); }) {}
SensorWorker::~SensorWorker() {
  cancel();
  {
    std::lock_guard lock(queue_mutex_);
    thread_.request_stop();
  }
  wake_.notify_all();
  thread_.join();
  bus_->release_all();
}
void SensorWorker::cancel() {
  std::lock_guard lock(queue_mutex_);
  ++generation_;
  jobs_.clear();
  results_.clear();
  release_ = true;
  wake_.notify_all();
}
void SensorWorker::configure(const std::vector<I2cLease> &leases) {
  cancel();
  std::lock_guard io(io_mutex_);
  std::lock_guard queue(queue_mutex_);
  release_ = false;
  sensors_.clear();
  bus_->configure(leases);
  const auto generation = generation_.load();
  for (const auto &l : leases)
    sensors_[l.node] = std::make_unique<Bme280Sensor>(
        *bus_, l.node, [this, generation] { return generation_.load() != generation; });
}
bool SensorWorker::request(const std::string &node) {
  std::lock_guard lock(queue_mutex_);
  if (jobs_.size() >= 32)
    return false;
  for (const auto &j : jobs_)
    if (j.node == node)
      return false;
  jobs_.push_back({generation_.load(), node});
  wake_.notify_all();
  return true;
}
std::vector<std::pair<std::string, Json>> SensorWorker::drain() {
  std::lock_guard lock(queue_mutex_);
  std::vector<std::pair<std::string, Json>> result(results_.begin(), results_.end());
  results_.clear();
  return result;
}
void SensorWorker::loop(std::stop_token stop) {
  while (!stop.stop_requested()) {
    Job job;
    {
      std::unique_lock lock(queue_mutex_);
      wake_.wait(lock, [&] { return stop.stop_requested() || release_ || !jobs_.empty(); });
      if (stop.stop_requested())
        break;
      if (!jobs_.empty()) {
        job = jobs_.front();
        jobs_.pop_front();
      } else
        job = {generation_.load(), ""};
    }
    Json result;
    {
      std::lock_guard io(io_mutex_);
      {
        std::lock_guard queue(queue_mutex_);
        if (release_) {
          bus_->release_all();
          sensors_.clear();
          release_ = false;
        }
      }
      if (job.node.empty() || generation_.load() != job.generation)
        continue;
      try {
        if (!sensors_.contains(job.node))
          throw Error("sensor.no_lease", "Sensor is not configured");
        result = sensors_.at(job.node)->sample();
      } catch (const std::exception &e) {
        result = {{"state", "DEGRADED"}, {"error", e.what()}, {"timestamp_ms", timestamp_ms()}};
      }
    }
    std::lock_guard lock(queue_mutex_);
    if (generation_.load() == job.generation && results_.size() < 32)
      results_.emplace_back(job.node, std::move(result));
  }
}
} // namespace moonpi
