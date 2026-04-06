// Osx memory info reader
#if defined(__APPLE__) && defined(__arm64__)

#include "hwinfo/detail/osx_mem_info_reader.hpp"

#include <mach/mach.h>
#include <mach/mach_host.h>

#include <expected>
#include <system_error>

#include "hwinfo/detail/osx_util.hpp"

using namespace hwlcd::hwinfo;
using namespace hwlcd::hwinfo::detail;
using namespace hwlcd::hwinfo::detail::osx;

auto get_memory_stats() -> std::expected<vm_statistics64, std::error_code> {
  vm_statistics64 stats;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  if (auto ec = host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info_t)&stats, &count); ec != KERN_SUCCESS) {
    return std::unexpected(make_mach_error_code(ec));
  }
  return stats;
}

auto mem_info_reader::sample() -> mem_info_sample {
  mem_info_sample sample;

  get_memory_stats()
      .and_then([&sample](vm_statistics64&& stats) {
        sample.stats.active_count = stats.active_count;
        sample.stats.wire_count = stats.wire_count;
        sample.stats.inactive_count = stats.inactive_count;
        sample.stats.compressor_page_count = stats.compressor_page_count;
        sample.stats.free_count = stats.free_count;
        return std::expected<void, std::error_code>();
      })
      .or_else([&sample](std::error_code&& ec) {
        sample.errors.emplace_back(std::move(ec));
        return std::expected<void, std::error_code>();
      });

  return sample;
}

auto mem_info_reader::sample_diff(const mem_info_sample& start, const mem_info_sample& end) -> mem_info_sample_diff {
  mem_info_sample_diff diff;

  diff.stats = end.stats;  // Copy

  if (start.errors.size() > 0 || end.errors.size() > 0) {
    diff.errors.reserve(start.errors.size() + end.errors.size());
    diff.errors.insert(diff.errors.end(), start.errors.begin(), start.errors.end());
    diff.errors.insert(diff.errors.end(), end.errors.begin(), end.errors.end());
  }

  return diff;
}

#endif
