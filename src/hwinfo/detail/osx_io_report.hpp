// Osx io report
#pragma once

#if defined(__APPLE__) && defined(__arm64__)

#include <chrono>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "hwinfo/detail/osx_sys_info.hpp"
#include "hwinfo/metric.hpp"

namespace hwlcd {
namespace hwinfo {
namespace detail {
namespace osx {

/**
 * @brief The report sample of a time point
 *
 */
class io_report_sample;

/**
 * @brief The report sample diff of two samples
 *
 */
struct io_report_sample_diff;

/**
 * @brief The io report class
 *
 */
class io_report {
 public:
  io_report(sys_info& si) : sys_info_(si), channel_(nullptr), subscription_(nullptr) {}

  ~io_report();

  io_report(const io_report&) = delete;
  io_report(io_report&&) = delete;

  io_report& operator=(const io_report&) = delete;
  io_report& operator=(io_report&&) = delete;

  auto init() -> std::error_code;
  auto sample() -> io_report_sample;
  auto sample_diff(const io_report_sample& start, const io_report_sample& end) -> io_report_sample_diff;

 private:
  sys_info& sys_info_;
  void* channel_;
  void* subscription_;
};

class io_report_sample {
 public:
  using time_point = std::chrono::system_clock::time_point;

  io_report_sample() : time_(std::chrono::system_clock::now()), ref_(nullptr) {}

  io_report_sample(void* ref) : time_(std::chrono::system_clock::now()), ref_(ref) {}

  ~io_report_sample();

  io_report_sample(const io_report_sample&) = delete;
  io_report_sample(io_report_sample&& other) : time_(other.time_), ref_(other.ref_) {
    other.time_ = time_point();
    other.ref_ = nullptr;
  }

  io_report_sample& operator=(const io_report_sample&) = delete;
  io_report_sample& operator=(io_report_sample&& other);

  /**
   * @brief Get sample time
   *
   * @return time_point
   */
  time_point time() const { return time_; }

  /**
   * @brief Whether current object holds a CFDictionaryRef or not
   *
   * @return true
   * @return false
   */
  bool has_ref() const { return ref_ != nullptr; }

  /**
   * @brief Get the CFDictionaryRef object
   *
   * @return void*
   */
  void* ref() const { return ref_; }

 private:
  time_point time_;
  void* ref_;
};

struct io_report_sample_diff {
  using duration = std::chrono::system_clock::duration;

  duration sample_duration;

  float gpu_freq = 0;

  std::vector<device_sequential_metric<float>> cpu_core_freqs;

  std::unordered_map<device_type, float> power_consumptions;

  std::vector<device_sequential_metric<float>> cpu_core_power_consumptions;

  std::vector<std::error_code> errors;
};

}  // namespace osx
}  // namespace detail
}  // namespace hwinfo
}  // namespace hwlcd

#endif
