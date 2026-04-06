// Osx cpu load reader
#pragma once

#if defined(__APPLE__) && defined(__arm64__)

#include <system_error>
#include <vector>

#include "hwinfo/metric.hpp"

namespace hwlcd {
namespace hwinfo {
namespace detail {
namespace osx {

struct cpu_load_sample;

struct cpu_load_sample_diff;

class cpu_load_reader {
 public:
  auto sample() -> cpu_load_sample;
  auto sample_diff(const cpu_load_sample& start, const cpu_load_sample& end) -> cpu_load_sample_diff;
};

// Save per core times of each type
struct cpu_core_load {
  int user = 0;
  int system = 0;
  int idle = 0;
  int nice = 0;
};

struct cpu_load_sample {
  // Cpu [Total] loads per core
  std::vector<cpu_core_load> loads;

  std::vector<std::error_code> errors;
};

struct cpu_load_sample_diff {
  // Cpu [diff] loads per core
  std::vector<cpu_core_load> loads;

  std::vector<std::error_code> errors;
};

}  // namespace osx
}  // namespace detail
}  // namespace hwinfo
}  // namespace hwlcd

#endif
