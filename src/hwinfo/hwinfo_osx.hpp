// hwinfo osx header
#pragma once

#if defined(__APPLE__) && defined(__arm64__)

#include <iostream>
#include <ranges>

#include "hwinfo/detail/util.hpp"
#include "hwinfo/hwinfo.hpp"

namespace hwlcd {
namespace hwinfo {

template <typename BeginIt, typename EndIt>
auto hwinfo::sample_diff(BeginIt&& begin, EndIt&& end) -> hwlcd::hwinfo::sample_diff {
  if (begin == end || !begin->major()) {
    return hwlcd::hwinfo::sample_diff();
  }

  // Get the first and the last sample
  auto first_it = begin;
  auto last_it = begin;
  hwlcd::hwinfo::sample_diff::time_point start_time = begin->time();
  hwlcd::hwinfo::sample_diff::time_point end_time = begin->time();
  size_t sample_num = 1;
  auto it = begin;
  for (++it; it != end; ++it) {
    if (it->time() > end_time) {
      ++sample_num;
      last_it = it;
      end_time = it->time();
    }
  }

  if (first_it == last_it || !last_it->major() || sample_num <= 1) {
    return hwlcd::hwinfo::sample_diff();
  }

  hwlcd::hwinfo::sample_diff diff(start_time, end_time, sample_num);

  auto duration_in_second = std::chrono::duration_cast<std::chrono::seconds>(diff.sample_duration).count();

  diff.memory_size = sys_info_.memory_total_size();

  // Smc samples
  {
    auto smc_samples_view =
        std::ranges::subrange(begin, end) |
        std::views::transform([](const hwlcd::hwinfo::sample& s) -> const hwlcd::hwinfo::detail::osx::smc_sample& {
          return s.smc_sample_;
        });
    auto smc_sample_diff =
        smc_reader_.sample_diff(std::ranges::begin(smc_samples_view), std::ranges::end(smc_samples_view));
    diff.temperatures = std::move(smc_sample_diff.temperatures);
    diff.fan_speeds = std::move(smc_sample_diff.fan_speeds);
    // Cpu temperature
    auto cpu_temperature_view =
        diff.temperatures | std::views::filter([](const device_sequential_metric<float>& metric) {
          return metric.device == device_type::e_cpu_core || metric.device == device_type::p_cpu_core ||
                 metric.device == device_type::s_cpu_core;
        });
    diff.cpu_temperature =
        detail::stat_values<float>(std::ranges::begin(cpu_temperature_view), std::ranges::end(cpu_temperature_view));
    // Gpu temperature
    auto gpu_temperature_view =
        diff.temperatures | std::views::filter([](const device_sequential_metric<float>& metric) {
          return metric.device == device_type::gpu_core;
        });
    diff.gpu_temperature =
        detail::stat_values<float>(std::ranges::begin(gpu_temperature_view), std::ranges::end(gpu_temperature_view));
    // Memory temperature
    auto memory_temperature_view =
        diff.temperatures | std::views::filter([](const device_sequential_metric<float>& metric) {
          return metric.device == device_type::dram;
        });
    diff.memory_temperature = detail::stat_values<float>(std::ranges::begin(memory_temperature_view),
                                                         std::ranges::end(memory_temperature_view));
    // Disk temperature
    auto disk_temperature_view =
        diff.temperatures | std::views::filter([](const device_sequential_metric<float>& metric) {
          return metric.device == device_type::nand;
        });
    diff.disk_temperature =
        detail::stat_values<float>(std::ranges::begin(disk_temperature_view), std::ranges::end(disk_temperature_view));
    // Battery temperature
    auto battery_temperature_view =
        diff.temperatures | std::views::filter([](const device_sequential_metric<float>& metric) {
          return metric.device == device_type::battery;
        });
    diff.battery_temperature = detail::stat_values<float>(std::ranges::begin(battery_temperature_view),
                                                          std::ranges::end(battery_temperature_view));
    // Fan speed
    diff.fan_speed = detail::stat_values<float>(diff.fan_speeds.begin(), diff.fan_speeds.end());

    // Errors
    if (smc_sample_diff.errors.size() > 0) {
      diff.errors.insert(diff.errors.end(), smc_sample_diff.errors.begin(), smc_sample_diff.errors.end());
    }
  }

  // IO report samples
  {
    auto io_report_diff = io_report_.sample_diff(*(first_it->io_report_sample_), *(last_it->io_report_sample_));
    diff.gpu_freq = io_report_diff.gpu_freq;
    diff.cpu_core_freqs = std::move(io_report_diff.cpu_core_freqs);
    diff.power_consumptions = std::move(io_report_diff.power_consumptions);
    diff.cpu_core_power_consumptions = std::move(io_report_diff.cpu_core_power_consumptions);
    // Cpu freq
    diff.cpu_freq = detail::stat_values<float>(diff.cpu_core_freqs.begin(), diff.cpu_core_freqs.end());
    // Cpu core type freq
    auto e_cpu_core_freqs_view =
        diff.cpu_core_freqs | std::views::filter([](const device_sequential_metric<float>& metric) {
          return metric.device == device_type::e_cpu_core;
        });
    auto p_cpu_core_freqs_view =
        diff.cpu_core_freqs | std::views::filter([](const device_sequential_metric<float>& metric) {
          return metric.device == device_type::p_cpu_core;
        });
    auto s_cpu_core_freqs_view =
        diff.cpu_core_freqs | std::views::filter([](const device_sequential_metric<float>& metric) {
          return metric.device == device_type::s_cpu_core;
        });
    if (std::ranges::begin(e_cpu_core_freqs_view) != std::ranges::end(e_cpu_core_freqs_view)) {
      diff.cpu_core_type_freq.emplace(device_type::e_cpu,
                                      detail::stat_values<float>(std::ranges::begin(e_cpu_core_freqs_view),
                                                                 std::ranges::end(e_cpu_core_freqs_view)));
    }
    if (std::ranges::begin(p_cpu_core_freqs_view) != std::ranges::end(p_cpu_core_freqs_view)) {
      diff.cpu_core_type_freq.emplace(device_type::p_cpu,
                                      detail::stat_values<float>(std::ranges::begin(p_cpu_core_freqs_view),
                                                                 std::ranges::end(p_cpu_core_freqs_view)));
    }
    if (std::ranges::begin(s_cpu_core_freqs_view) != std::ranges::end(s_cpu_core_freqs_view)) {
      diff.cpu_core_type_freq.emplace(device_type::s_cpu,
                                      detail::stat_values<float>(std::ranges::begin(s_cpu_core_freqs_view),
                                                                 std::ranges::end(s_cpu_core_freqs_view)));
    }

    // Errors
    if (io_report_diff.errors.size() > 0) {
      diff.errors.insert(diff.errors.end(), io_report_diff.errors.begin(), io_report_diff.errors.end());
    }
  }

  // Cpu load
  {
    auto cpu_load_diff = cpu_load_reader_.sample_diff(*(first_it->cpu_load_sample_), *(last_it->cpu_load_sample_));
    if (cpu_load_diff.loads.size() > 0) {
      diff.cpu_core_usage.resize(cpu_load_diff.loads.size());
      float usage_sum = 0;
      for (size_t i = 0; i < cpu_load_diff.loads.size(); ++i) {
        const auto& load = cpu_load_diff.loads[i];
        auto user = static_cast<float>(load.user);
        auto system = static_cast<float>(load.system);
        auto idle = static_cast<float>(load.idle);
        auto nice = static_cast<float>(load.nice);
        auto usage = (user + system) / (user + system + idle + nice);
        // NOTE: Lack of cpu sub type info, use common cpu_core type
        diff.cpu_core_usage[i] = device_sequential_metric<float>(device_type::cpu_core, i + 1, usage);
        usage_sum += usage;
      }
      diff.cpu_usage = usage_sum / cpu_load_diff.loads.size();
    }

    // Errors
    if (cpu_load_diff.errors.size() > 0) {
      diff.errors.insert(diff.errors.end(), cpu_load_diff.errors.begin(), cpu_load_diff.errors.end());
    }
  }

  // Memory info
  {
    auto memory_info_diff = mem_info_reader_.sample_diff(*(first_it->mem_info_sample_), *(last_it->mem_info_sample_));
    auto total_count = static_cast<float>(memory_info_diff.stats.wire_count + memory_info_diff.stats.active_count +
                                          memory_info_diff.stats.inactive_count + memory_info_diff.stats.free_count +
                                          memory_info_diff.stats.compressor_page_count);
    diff.memory_usage =
        static_cast<float>(memory_info_diff.stats.wire_count + memory_info_diff.stats.active_count) / total_count;
    diff.memory_available_percentage =
        static_cast<float>(memory_info_diff.stats.free_count + memory_info_diff.stats.inactive_count +
                           memory_info_diff.stats.compressor_page_count) /
        total_count;
    diff.memory_free_percentage = static_cast<float>(memory_info_diff.stats.free_count) / total_count;

    // Errors
    if (memory_info_diff.errors.size() > 0) {
      diff.errors.insert(diff.errors.end(), memory_info_diff.errors.begin(), memory_info_diff.errors.end());
    }
  }

  // Disk io counter
  {
    auto disk_io_diff = disk_io_reader_.sample_diff(*(first_it->disk_io_sample_), *(last_it->disk_io_sample_));
    diff.disk_io_counter_per_disk = std::move(disk_io_diff.counters);
    for (const auto& [name, counter] : diff.disk_io_counter_per_disk) {
      diff.disk_io_rate_per_disk.emplace(
          name, disk_io_rate{
                    .read_opts = static_cast<float>(counter.read_opts) / duration_in_second,
                    .write_opts = static_cast<float>(counter.write_opts) / duration_in_second,
                    .read_bytes = static_cast<float>(counter.read_bytes) / duration_in_second,
                    .write_bytes = static_cast<float>(counter.write_bytes) / duration_in_second,
                    .read_time_pct = static_cast<float>(counter.read_time) / 1e9f / duration_in_second,
                    .write_time_pct = static_cast<float>(counter.write_time) / 1e9f / duration_in_second,
                });
      diff.total_disk_io_counter.read_opts += counter.read_opts;
      diff.total_disk_io_counter.write_opts += counter.write_opts;
      diff.total_disk_io_counter.read_bytes += counter.read_bytes;
      diff.total_disk_io_counter.write_bytes += counter.write_bytes;
      diff.total_disk_io_counter.read_time += counter.read_time;
      diff.total_disk_io_counter.write_time += counter.write_time;
    }
    diff.total_disk_io_rate.read_opts = static_cast<float>(diff.total_disk_io_counter.read_opts) / duration_in_second;
    diff.total_disk_io_rate.write_opts = static_cast<float>(diff.total_disk_io_counter.write_opts) / duration_in_second;
    diff.total_disk_io_rate.read_bytes = static_cast<float>(diff.total_disk_io_counter.read_bytes) / duration_in_second;
    diff.total_disk_io_rate.write_bytes =
        static_cast<float>(diff.total_disk_io_counter.write_bytes) / duration_in_second;
    diff.total_disk_io_rate.read_time_pct =
        static_cast<float>(diff.total_disk_io_counter.read_time) / 1e9f / duration_in_second;
    diff.total_disk_io_rate.write_time_pct =
        static_cast<float>(diff.total_disk_io_counter.write_time) / 1e9f / duration_in_second;

    // Errors
    if (disk_io_diff.errors.size() > 0) {
      diff.errors.insert(diff.errors.end(), disk_io_diff.errors.begin(), disk_io_diff.errors.end());
    }
  }

  // Network io counter
  {
    auto net_io_diff = net_io_reader_.sample_diff(*(first_it->net_io_sample_), *(last_it->net_io_sample_));
    diff.net_io_counter_per_if = std::move(net_io_diff.counters);
    for (const auto& [name, counter] : diff.net_io_counter_per_if) {
      diff.net_io_rate_per_if.emplace(
          name, net_io_rate{
                    .read_bytes = static_cast<float>(counter.read_bytes) / duration_in_second,
                    .write_bytes = static_cast<float>(counter.write_bytes) / duration_in_second,
                    .read_packets = static_cast<float>(counter.read_packets) / duration_in_second,
                    .write_packets = static_cast<float>(counter.write_packets) / duration_in_second,
                    .read_errors = static_cast<float>(counter.read_errors) / duration_in_second,
                    .write_errors = static_cast<float>(counter.write_errors) / duration_in_second,
                });
      diff.total_net_io_counter.read_bytes += counter.read_bytes;
      diff.total_net_io_counter.write_bytes += counter.write_bytes;
      diff.total_net_io_counter.read_packets += counter.read_packets;
      diff.total_net_io_counter.write_packets += counter.write_packets;
      diff.total_net_io_counter.read_errors += counter.read_errors;
      diff.total_net_io_counter.write_errors += counter.write_errors;
    }
    diff.total_net_io_rate.read_bytes = static_cast<float>(diff.total_net_io_counter.read_bytes) / duration_in_second;
    diff.total_net_io_rate.write_bytes = static_cast<float>(diff.total_net_io_counter.write_bytes) / duration_in_second;
    diff.total_net_io_rate.read_packets =
        static_cast<float>(diff.total_net_io_counter.read_packets) / duration_in_second;
    diff.total_net_io_rate.write_packets =
        static_cast<float>(diff.total_net_io_counter.write_packets) / duration_in_second;
    diff.total_net_io_rate.read_errors = static_cast<float>(diff.total_net_io_counter.read_errors) / duration_in_second;
    diff.total_net_io_rate.write_errors =
        static_cast<float>(diff.total_net_io_counter.write_errors) / duration_in_second;

    // Errors
    if (net_io_diff.errors.size() > 0) {
      diff.errors.insert(diff.errors.end(), net_io_diff.errors.begin(), net_io_diff.errors.end());
    }
  }

  return diff;
}

}  // namespace hwinfo
}  // namespace hwlcd

#endif
