// hwinfo
#pragma once

#include <chrono>
#include <optional>
#include <system_error>

#ifndef HWLCD_HWINFO_NO_BOOST
#include <boost/describe.hpp>
#endif

#include "hwinfo/metric.hpp"

#if defined(__APPLE__) && defined(__arm64__)
#include "hwinfo/detail/osx_cpu_load_reader.hpp"
#include "hwinfo/detail/osx_disk_io_reader.hpp"
#include "hwinfo/detail/osx_io_report.hpp"
#include "hwinfo/detail/osx_mem_info_reader.hpp"
#include "hwinfo/detail/osx_net_io_reader.hpp"
#include "hwinfo/detail/osx_smc_reader.hpp"
#include "hwinfo/detail/osx_sys_info.hpp"
#endif

namespace hwlcd {
namespace hwinfo {

/**
 * @brief The sample of a single time point
 *
 */
class sample;

/**
 * @brief The sample diff which contains all metrics that is calculated by several samples
 *
 */
struct sample_diff;

/**
 * @brief Hardware info main class
 *
 *  The implementation of this class is platform - specific
 *
 */
class hwinfo {
 public:
  explicit hwinfo();
  ~hwinfo() = default;

  hwinfo(const hwinfo&) = delete;
  hwinfo(hwinfo&&) = delete;

  hwinfo& operator=(const hwinfo&) = delete;
  hwinfo& operator=(hwinfo&&) = delete;

  /**
   * @brief Get platform type
   *
   * @return auto
   */
  auto platform() const -> platform_type {
#if defined(__APPLE__) && defined(__arm64__)
    return platform_type::osx_arm64;
#else
    return platform_type::unsupported;
#endif
  }
  /**
   * @brief Initialize hwinfo
   *
   * @return std::error_code
   */
  auto init() -> std::error_code;
  /**
   * @brief Create a sample
   *
   * @param major Is major sample
   * @return sample
   */
  auto sample(bool major) -> hwlcd::hwinfo::sample;
  /**
   * @brief Create a sample diff
   *
   *  The first and the last must be major samples
   *
   * @tparam BeginIt Begin iterator type
   * @tparam EndIt End iterator type
   * @param begin Begin iterator
   * @param end End iterator
   * @return sample_diff
   */
  template <typename BeginIt, typename EndIt>
  auto sample_diff(BeginIt&& begin, EndIt&& end) -> hwlcd::hwinfo::sample_diff;

 private:
#if defined(__APPLE__) && defined(__arm64__)
  detail::osx::sys_info sys_info_;
  detail::osx::smc_reader smc_reader_;
  detail::osx::io_report io_report_;
  detail::osx::cpu_load_reader cpu_load_reader_;
  detail::osx::mem_info_reader mem_info_reader_;
  detail::osx::disk_io_reader disk_io_reader_;
  detail::osx::net_io_reader net_io_reader_;
#endif
};

class sample {
 public:
  using time_point = std::chrono::system_clock::time_point;

  explicit sample(bool major) : time_(std::chrono::system_clock::now()), major_(major) {}
  ~sample() = default;

  // Copy sample is not allowed, but move is ok

  sample(const sample&) = delete;
  sample(sample&&) = default;

  sample& operator=(const sample&) = delete;
  sample& operator=(sample&&) = default;

  /**
   * @brief Sample time
   *
   * @return auto
   */
  auto time() const { return time_; }
  /**
   * @brief Is major sample
   *
   * @return auto
   */
  auto major() const { return major_; }

 private:
  friend hwinfo;

  time_point time_;
  bool major_;

#if defined(__APPLE__) && defined(__arm64__)
  detail::osx::smc_sample smc_sample_;
  // Major samples
  std::optional<detail::osx::io_report_sample> io_report_sample_;
  std::optional<detail::osx::cpu_load_sample> cpu_load_sample_;
  std::optional<detail::osx::mem_info_sample> mem_info_sample_;
  std::optional<detail::osx::disk_io_sample> disk_io_sample_;
  std::optional<detail::osx::net_io_sample> net_io_sample_;
#endif
};

struct sample_diff {
  using time_point = std::chrono::system_clock::time_point;
  using duration = std::chrono::system_clock::duration;

  explicit sample_diff() = default;
  explicit sample_diff(time_point start_time, time_point end_time, size_t sample_num)
      : start_time(start_time), end_time(end_time), sample_duration(end_time - start_time), sample_num(sample_num) {}
  ~sample_diff() = default;

  sample_diff(const sample_diff&) = default;
  sample_diff(sample_diff&&) = default;

  sample_diff& operator=(const sample_diff&) = default;
  sample_diff& operator=(sample_diff&&) = default;

  /**
   * @brief Sample start time
   *
   * @return auto
   */
  time_point start_time;
  /**
   * @brief Sample end time
   *
   * @return auto
   */
  time_point end_time;
  /**
   * @brief Sample duration
   *
   * @return auto
   */
  duration sample_duration;
  /**
   * @brief Sample number
   *
   * @return auto
   */
  size_t sample_num;
  /**
   * @brief All devices' temperatures in celsius
   *
   */
  std::vector<device_sequential_metric<float>> temperatures;
  /**
   * @brief Cpu temperature in celsius
   *
   */
  stats_value<float> cpu_temperature;
  /**
   * @brief Gpu temperature in celsius
   *
   */
  stats_value<float> gpu_temperature;
  /**
   * @brief Memory temperature in celsius
   *
   */
  stats_value<float> memory_temperature;
  /**
   * @brief Disk temperature in celsius
   *
   */
  stats_value<float> disk_temperature;
  /**
   * @brief Battery temperature in celsius
   *
   */
  stats_value<float> battery_temperature;
  /**
   * @brief All fans' speed in rpm
   *
   */
  std::vector<sequential_metric<float>> fan_speeds;
  /**
   * @brief Fan speed in rpm
   *
   */
  stats_value<float> fan_speed;
  /**
   * @brief All devices' power consumption in watt
   *
   */
  std::unordered_map<device_type, float> power_consumptions;
  /**
   * @brief Cpu power consumption per core in watt
   *
   */
  std::vector<device_sequential_metric<float>> cpu_core_power_consumptions;
  /**
   * @brief Cpu usage
   *
   */
  float cpu_usage = 0;
  /**
   * @brief Cpu usage per core
   *
   */
  std::vector<device_sequential_metric<float>> cpu_core_usage;
  /**
   * @brief Cpu frequency
   *
   */
  stats_value<float> cpu_freq;
  /**
   * @brief Cpu frequency per core type
   *
   *  e_cpu / p_cpu / s_cpu / ....
   *
   */
  std::unordered_map<device_type, stats_value<float>> cpu_core_type_freq;
  /**
   * @brief Cpu frequency per core in mhz
   *
   */
  std::vector<device_sequential_metric<float>> cpu_core_freqs;
  /**
   * @brief Device frequency in mhz
   *
   */
  float gpu_freq = 0;
  /**
   * @brief Total memory size in bytes
   *
   */
  float memory_size = 0;
  /**
   * @brief Memory usage percentage
   *
   */
  float memory_usage = 0;
  /**
   * @brief Memory available percentage (including cached memory etc..)
   *
   */
  float memory_available_percentage = 0;
  /**
   * @brief Memory free percentage
   *
   */
  float memory_free_percentage = 0;
  /**
   * @brief Disk io counter
   *
   */
  disk_io_counter total_disk_io_counter;
  /**
   * @brief Disk io rate
   *
   */
  disk_io_rate total_disk_io_rate;
  /**
   * @brief Disk io counter per disk
   *
   */
  std::unordered_map<std::string, disk_io_counter> disk_io_counter_per_disk;
  /**
   * @brief Disk io rate per disk
   *
   */
  std::unordered_map<std::string, disk_io_rate> disk_io_rate_per_disk;
  /**
   * @brief Network io counter
   *
   */
  net_io_counter total_net_io_counter;
  /**
   * @brief Network io rate
   *
   */
  net_io_rate total_net_io_rate;
  /**
   * @brief Network io counter per interface
   *
   */
  std::unordered_map<std::string, net_io_counter> net_io_counter_per_if;
  /**
   * @brief Network io rate per interface
   *
   */
  std::unordered_map<std::string, net_io_rate> net_io_rate_per_if;
  /**
   * @brief Sample errors
   *
   */
  std::vector<std::error_code> errors;
};

#ifndef HWLCD_HWINFO_NO_BOOST
BOOST_DESCRIBE_STRUCT(sample_diff, (),
                      (start_time, end_time, sample_duration, sample_num, temperatures, cpu_temperature,
                       gpu_temperature, memory_temperature, disk_temperature, battery_temperature, fan_speeds,
                       fan_speed, power_consumptions, cpu_core_power_consumptions, cpu_usage, cpu_core_usage, cpu_freq,
                       cpu_core_type_freq, cpu_core_freqs, gpu_freq, memory_size, memory_usage,
                       memory_available_percentage, memory_free_percentage, total_disk_io_counter, total_disk_io_rate,
                       disk_io_counter_per_disk, disk_io_rate_per_disk, total_net_io_counter, total_net_io_rate,
                       net_io_counter_per_if, net_io_rate_per_if, errors))
#endif

}  // namespace hwinfo
}  // namespace hwlcd

#include "hwinfo/hwinfo_osx.hpp"
