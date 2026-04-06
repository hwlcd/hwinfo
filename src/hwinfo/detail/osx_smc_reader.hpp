// Osx smc reader
#pragma once

#if defined(__APPLE__) && defined(__arm64__)

#include <bitset>
#include <cassert>
#include <chrono>
#include <expected>
#include <system_error>
#include <vector>

#include "hwinfo/detail/osx_sys_info.hpp"
#include "hwinfo/metric.hpp"

namespace hwlcd {
namespace hwinfo {
namespace detail {
namespace osx {

struct smc_sample;

struct smc_sample_diff;

class smc_reader {
 public:
  smc_reader(sys_info& si) : sys_info_(si), conn_(0) {}
  ~smc_reader();

  smc_reader(const smc_reader&) = delete;
  smc_reader(smc_reader&&) = delete;

  smc_reader& operator=(const smc_reader&) = delete;
  smc_reader& operator=(smc_reader&&) = delete;

  auto init() -> std::error_code;
  auto sample() -> smc_sample;
  template <typename BeginIt, typename EndIt>
  auto sample_diff(BeginIt&& begin, EndIt&& end) -> smc_sample_diff;

 private:
  sys_info& sys_info_;
  // Apple SMC service connection
  unsigned int conn_;
  // Key bitsets
  std::bitset<64> valid_cpu_core_temperature_keys_;
  std::bitset<64> valid_gpu_core_temperature_keys_;
  std::bitset<64> valid_dram_temperature_keys_;
  std::bitset<64> valid_storage_temperature_keys_;
  std::bitset<64> valid_battery_temperature_keys_;
  std::bitset<64> valid_fan_speed_keys_;
};

struct smc_sample {
  std::vector<device_sequential_metric<float>> temperatures;
  std::vector<sequential_metric<float>> fan_speeds;
};

/**
 * @brief The smc sample diff of samples
 *
 */
struct smc_sample_diff {
  size_t sample_num = 0;
  std::vector<device_sequential_metric<float>> temperatures;
  std::vector<sequential_metric<float>> fan_speeds;
  std::vector<std::error_code> errors;
};

template <typename BeginIt, typename EndIt>
auto smc_reader::sample_diff(BeginIt&& begin, EndIt&& end) -> smc_sample_diff {
  smc_sample_diff diff;

  if (begin == end) {
    return diff;
  }

  // Apply the first sample
  diff.sample_num = 1;
  diff.temperatures = (*begin).temperatures;  // Copy
  diff.fan_speeds = (*begin).fan_speeds;      // Copy

  // Merge other samples
  auto it = begin;
  for (++it; it != end; ++it) {
    const smc_sample& sample = *it;
    if (sample.temperatures.size() == diff.temperatures.size() && sample.fan_speeds.size() == diff.fan_speeds.size()) {
      ++diff.sample_num;
      for (size_t i = 0; i < diff.temperatures.size(); ++i) {
        diff.temperatures[i].value += sample.temperatures[i].value;
      }
      for (size_t i = 0; i < diff.fan_speeds.size(); ++i) {
        diff.fan_speeds[i].value += sample.fan_speeds[i].value;
      }
    }
  }

  // Average values
  for (auto& m : diff.temperatures) {
    m.value /= diff.sample_num;
  }
  for (auto& m : diff.fan_speeds) {
    m.value /= diff.sample_num;
  }

  return diff;
}

}  // namespace osx
}  // namespace detail
}  // namespace hwinfo
}  // namespace hwlcd

#endif
