// Osx cpu load reader
#if defined(__APPLE__) && defined(__arm64__)

#include "hwinfo/detail/osx_cpu_load_reader.hpp"

#include <mach/mach.h>
#include <mach/mach_host.h>

#include <expected>
#include <system_error>

#include "hwinfo/detail/osx_util.hpp"

using namespace hwlcd::hwinfo;
using namespace hwlcd::hwinfo::detail;
using namespace hwlcd::hwinfo::detail::osx;

auto get_cpu_load() -> std::expected<std::vector<cpu_core_load>, std::error_code> {
  natural_t cpu_num;
  processor_info_array_t cpu_info;
  mach_msg_type_number_t num_cpu_info;

  if (auto ec = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cpu_num, &cpu_info, &num_cpu_info);
      ec != KERN_SUCCESS) {
    return std::unexpected(make_mach_error_code(ec));
  }

  std::vector<cpu_core_load> loads;

  for (natural_t i = 0; i < cpu_num; ++i) {
    int base = CPU_STATE_MAX * i;
    int user = cpu_info[base + CPU_STATE_USER];
    int system = cpu_info[base + CPU_STATE_SYSTEM];
    int idle = cpu_info[base + CPU_STATE_IDLE];
    int nice = cpu_info[base + CPU_STATE_NICE];
    loads.emplace_back(user, system, idle, nice);
  }

  vm_deallocate(mach_task_self(), (vm_address_t)cpu_info, num_cpu_info * sizeof(integer_t));
  return loads;
}

auto cpu_load_reader::sample() -> cpu_load_sample {
  cpu_load_sample sample;

  get_cpu_load()
      .and_then([&sample](std::vector<cpu_core_load>&& loads) {
        sample.loads = std::move(loads);
        return std::expected<void, std::error_code>();
      })
      .or_else([&sample](std::error_code&& ec) {
        sample.errors.emplace_back(std::move(ec));
        return std::expected<void, std::error_code>();
      });

  return sample;
}

auto cpu_load_reader::sample_diff(const cpu_load_sample& start, const cpu_load_sample& end) -> cpu_load_sample_diff {
  cpu_load_sample_diff diff;

  if (start.loads.size() != end.loads.size()) {
    diff.errors.push_back(std::make_error_code(std::errc::invalid_argument));
    return diff;
  }

  if (start.loads.size() > 0) {
    diff.loads.resize(start.loads.size());
    for (size_t i = 0; i < start.loads.size(); ++i) {
      diff.loads[i].idle = end.loads[i].idle >= start.loads[i].idle ? end.loads[i].idle - start.loads[i].idle : 0;
      diff.loads[i].nice = end.loads[i].nice >= start.loads[i].nice ? end.loads[i].nice - start.loads[i].nice : 0;
      diff.loads[i].user = end.loads[i].user >= start.loads[i].user ? end.loads[i].user - start.loads[i].user : 0;
      diff.loads[i].system =
          end.loads[i].system >= start.loads[i].system ? end.loads[i].system - start.loads[i].system : 0;
    }
  }

  if (start.errors.size() > 0 || end.errors.size() > 0) {
    diff.errors.reserve(start.errors.size() + end.errors.size());
    diff.errors.insert(diff.errors.end(), start.errors.begin(), start.errors.end());
    diff.errors.insert(diff.errors.end(), end.errors.begin(), end.errors.end());
  }

  return diff;
}

#endif
