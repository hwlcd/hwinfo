// Osx disk io reader
#pragma once

#if defined(__APPLE__) && defined(__arm64__)

#include <system_error>
#include <unordered_map>
#include <vector>

#include "hwinfo/metric.hpp"

namespace hwlcd {
namespace hwinfo {
namespace detail {
namespace osx {

struct disk_io_sample;

struct disk_io_sample_diff;

class disk_io_reader {
 public:
  auto sample() -> disk_io_sample;
  auto sample_diff(const disk_io_sample& start, const disk_io_sample& end) -> disk_io_sample_diff;
};

struct disk_io_sample {
  // Counters which contains [total] counter values
  std::unordered_map<std::string, disk_io_counter> counters;

  std::vector<std::error_code> errors;
};

struct disk_io_sample_diff {
  // Counters which contains [diff] counter values
  std::unordered_map<std::string, disk_io_counter> counters;

  std::vector<std::error_code> errors;
};

}  // namespace osx
}  // namespace detail
}  // namespace hwinfo
}  // namespace hwlcd

#endif
