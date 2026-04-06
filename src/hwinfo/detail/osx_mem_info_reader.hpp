// Osx memory info reader
#pragma once

#if defined(__APPLE__) && defined(__arm64__)

#include <system_error>
#include <vector>

#include "hwinfo/metric.hpp"

namespace hwlcd {
namespace hwinfo {
namespace detail {
namespace osx {

struct mem_info_sample;

struct mem_info_sample_diff;

class mem_info_reader {
 public:
  auto sample() -> mem_info_sample;
  auto sample_diff(const mem_info_sample& start, const mem_info_sample& end) -> mem_info_sample_diff;
};

struct memory_stats {
  unsigned int active_count = 0;
  unsigned int wire_count = 0;
  unsigned int inactive_count = 0;
  unsigned int compressor_page_count = 0;
  unsigned int free_count = 0;
};

struct mem_info_sample {
  memory_stats stats;

  std::vector<std::error_code> errors;
};

struct mem_info_sample_diff {
  memory_stats stats;

  std::vector<std::error_code> errors;
};

}  // namespace osx
}  // namespace detail
}  // namespace hwinfo
}  // namespace hwlcd

#endif
