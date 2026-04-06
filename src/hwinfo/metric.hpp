// Metric
#pragma once

#include <string>

#ifndef HWLCD_HWINFO_NO_BOOST
#include <boost/describe.hpp>
#endif

namespace hwlcd {
namespace hwinfo {

/**
 * @brief Platform type
 *
 */
#ifndef HWLCD_HWINFO_NO_BOOST
BOOST_DEFINE_ENUM_CLASS(platform_type, unsupported, linux, windows, osx_arm64)
#else
enum class platform_type {
  unsupported,
  linux,
  windows,
  osx_arm64,
};
#endif

/**
 * @brief A common device type enumerate type which contains all possible device types (among different arch & os)
 *
 */
#ifndef HWLCD_HWINFO_NO_BOOST
BOOST_DEFINE_ENUM_CLASS(device_type, unknown, cpu, cpu_core, cpu_core_sram, e_cpu, e_cpu_core, e_cpu_core_sram, p_cpu,
                        p_cpu_core, p_cpu_core_sram, s_cpu, s_cpu_core, s_cpu_core_sram, gpu, gpu_core, gpu_sram, dram,
                        npu, nand, battery, fan)
#else
enum class device_type {
  unknown,
  cpu,
  cpu_core,
  cpu_core_sram,  // Also including l1/l2/.. cache
  e_cpu,
  e_cpu_core,
  e_cpu_core_sram,  // Also including l1/l2/.. cache
  p_cpu,
  p_cpu_core,
  p_cpu_core_sram,  // Also including l1/l2/.. cache
  s_cpu,
  s_cpu_core,
  s_cpu_core_sram,  // Also including l1/l2/.. cache
  gpu,
  gpu_core,
  gpu_sram,
  dram,
  npu,  // ANE in osx
  nand,
  battery,
  fan,
};
#endif

template <typename T>
concept MetricType = std::is_copy_constructible_v<T> && requires(T t) { t.value; };

template <typename T>
struct sequential_metric {
  /**
   * @brief value type
   *
   */
  using value_type = T;
  /**
   * @brief Sequential number
   *
   */
  size_t num = 0;
  /**
   * @brief Metric value
   *
   */
  T value = {};
};

template <typename T>
struct named_metric {
  /**
   * @brief value type
   *
   */
  using value_type = T;
  /**
   * @brief Sequential number
   *
   */
  std::string name = "";
  /**
   * @brief Metric value
   *
   */
  T value = {};
};

template <typename ValueType>
struct device_metric {
  /**
   * @brief value type
   *
   */
  using value_type = ValueType;
  /**
   * @brief Device
   *
   */
  device_type device = device_type::unknown;
  /**
   * @brief Metric value
   *
   */
  ValueType value = {};
};

template <typename ValueType>
struct device_sequential_metric {
  /**
   * @brief value type
   *
   */
  using value_type = ValueType;
  /**
   * @brief Device
   *
   */
  device_type device = device_type::unknown;
  /**
   * @brief Sequential number
   *
   */
  size_t num = 0;
  /**
   * @brief Metric value
   *
   */
  ValueType value = {};
};

template <typename T>
struct stats_value {
  T avg = {};
  T max = {};
  T min = {};
};

struct disk_io_counter {
  int64_t read_opts = 0;
  int64_t write_opts = 0;
  int64_t read_bytes = 0;
  int64_t write_bytes = 0;
  int64_t read_time = 0;   // in nanoseconds
  int64_t write_time = 0;  // in nanoseconds
};

/**
 * @brief Disk io rate (per second)
 *
 */
struct disk_io_rate {
  float read_opts = 0;
  float write_opts = 0;
  float read_bytes = 0;
  float write_bytes = 0;
  float read_time_pct = 0;
  float write_time_pct = 0;
};

/**
 * @brief Network io counter
 *
 */
struct net_io_counter {
  uint64_t read_bytes = 0;
  uint64_t write_bytes = 0;
  uint64_t read_packets = 0;
  uint64_t write_packets = 0;
  uint64_t read_errors = 0;
  uint64_t write_errors = 0;
};

/**
 * @brief Network io rate (per second)
 *
 */
struct net_io_rate {
  float read_bytes = 0;
  float write_bytes = 0;
  float read_packets = 0;
  float write_packets = 0;
  float read_errors = 0;
  float write_errors = 0;
};

#ifndef HWLCD_HWINFO_NO_BOOST
BOOST_DESCRIBE_STRUCT(sequential_metric<float>, (), (num, value))
BOOST_DESCRIBE_STRUCT(named_metric<float>, (), (name, value))
BOOST_DESCRIBE_STRUCT(device_metric<float>, (), (device, value))
BOOST_DESCRIBE_STRUCT(device_sequential_metric<float>, (), (device, num, value))
BOOST_DESCRIBE_STRUCT(stats_value<float>, (), (avg, max, min))
BOOST_DESCRIBE_STRUCT(disk_io_counter, (), (read_opts, write_opts, read_bytes, write_bytes, read_time, write_time))
BOOST_DESCRIBE_STRUCT(disk_io_rate, (), (read_opts, write_opts, read_bytes, write_bytes, read_time_pct, write_time_pct))
BOOST_DESCRIBE_STRUCT(net_io_counter, (),
                      (read_bytes, write_bytes, read_packets, write_packets, read_errors, write_errors))
BOOST_DESCRIBE_STRUCT(net_io_rate, (),
                      (read_bytes, write_bytes, read_packets, write_packets, read_errors, write_errors))
#endif

}  // namespace hwinfo
}  // namespace hwlcd
