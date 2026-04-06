// Osx io report
//
//  The osx private api codes are mainly from:
//  https://github.com/exelban/stats/blob/9f201533b69e4ebb7a44a1893630dad622c951e1/Modules/CPU/bridge.h#L19
//
#if defined(__APPLE__) && defined(__arm64__)

#include "hwinfo/detail/osx_io_report.hpp"

#include <CoreFoundation/CoreFoundation.h>

#include <array>
#include <charconv>
#include <iostream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "hwinfo/detail/osx_util.hpp"

//
// The following c apis are undocumented private osx apis which I copied from the above source file
//

extern "C" {
typedef void* IOReportSubscriptionRef;
CFDictionaryRef IOReportCopyChannelsInGroup(CFStringRef a, CFStringRef b, uint64_t c, uint64_t d, uint64_t e);
void IOReportMergeChannels(CFDictionaryRef a, CFDictionaryRef b, CFTypeRef null);
IOReportSubscriptionRef IOReportCreateSubscription(void* a, CFMutableDictionaryRef b, CFMutableDictionaryRef* c,
                                                   uint64_t d, CFTypeRef e);
CFDictionaryRef IOReportCreateSamples(IOReportSubscriptionRef a, CFMutableDictionaryRef b, CFTypeRef c);
CFDictionaryRef IOReportCreateSamplesDelta(CFDictionaryRef a, CFDictionaryRef b, CFTypeRef c);
CFStringRef IOReportChannelGetGroup(CFDictionaryRef a);
CFStringRef IOReportChannelGetSubGroup(CFDictionaryRef a);
CFStringRef IOReportChannelGetChannelName(CFDictionaryRef a);
CFStringRef IOReportChannelGetUnitLabel(CFDictionaryRef a);
int32_t IOReportStateGetCount(CFDictionaryRef a);
CFStringRef IOReportStateGetNameForIndex(CFDictionaryRef a, int32_t b);
int64_t IOReportStateGetResidency(CFDictionaryRef a, int32_t b);
int64_t IOReportSimpleGetIntegerValue(CFDictionaryRef a, int32_t b);
}

using namespace hwlcd::hwinfo;
using namespace hwlcd::hwinfo::detail;
using namespace hwlcd::hwinfo::detail::osx;

static constexpr std::array<std::tuple<std::string_view, std::string_view>, 3> report_channel_names{{
    {"Energy Model", ""},
    //{"CPU Stats", "CPU Complex Performance States"},
    {"CPU Stats", "CPU Core Performance States"},
    {"GPU Stats", "GPU Performance States"},
}};

auto calculate_freq(CFDictionaryRef item, const std::vector<xpu_dynamic_freq>& freq_list) -> double {
  auto count = IOReportStateGetCount(item);
  auto freq_list_it = freq_list.begin();
  double tick_sum = 0, freq_sum = 0;
  for (int32_t i = 0; i < count; ++i) {
    const auto name = from_cf_string(IOReportStateGetNameForIndex(item, i));
    if (name == "IDLE" || name == "DOWN" || name == "OFF") {
      continue;
    }
    // Use this value
    if (freq_list_it == freq_list.end()) {
      // NOTE: ?? This may be an error
      break;
    }
    auto ticks = static_cast<double>(IOReportStateGetResidency(item, i)) / 1e6;  // To avoid overflow: /1e6
    tick_sum += ticks;
    freq_sum += ticks * static_cast<double>(freq_list_it->freq);
    // Move to next freq list
    ++freq_list_it;
  }
  return freq_sum / tick_sum;
}

auto calculate_watts(CFDictionaryRef item, const std::string& unit, io_report_sample_diff::duration duration)
    -> double {
  // x per second
  auto value = static_cast<double>(IOReportSimpleGetIntegerValue(item, 0)) /
               std::chrono::duration_cast<std::chrono::seconds>(duration).count();
  if (unit == "mJ") {
    return value / 1e3;
  } else if (unit == "uJ") {
    return value / 1e6;
  } else if (unit == "nJ") {
    return value / 1e9;
  }
  return 0;
}

// ------ io_report implementation

io_report::~io_report() {
  if (channel_) {
    CFRelease(channel_);
    channel_ = nullptr;
  }
  if (subscription_) {
    CFRelease(subscription_);
    subscription_ = nullptr;
  }
}

auto io_report::init() -> std::error_code {
  //
  // Get and merge channel
  //
  std::vector<CFDictionaryRef> channels;
  channels.reserve(report_channel_names.size());
  for (const auto& name_tuple : report_channel_names) {
    cf_string gname(std::get<0>(name_tuple));
    cf_string sname(std::get<1>(name_tuple));
    auto channel = IOReportCopyChannelsInGroup(gname.ref(), sname.ref(), 0, 0, 0);
    if (!channel) {
      continue;
    }
    channels.push_back(channel);
  }
  if (channels.size() == 0) {
    // No channel found
    return std::error_code();
  }
  // Merge channels
  for (size_t i = 1; i < channels.size(); ++i) {
    IOReportMergeChannels(channels[0], channels[i], NULL);
  }
  auto merged_channel =
      CFDictionaryCreateMutableCopy(kCFAllocatorDefault, CFDictionaryGetCount(channels[0]), channels[0]);
  for (const auto& channel : channels) {
    CFRelease(channel);
  }
  if (!merged_channel) {
    return std::make_error_code(std::errc::invalid_argument);
  }
  if (cf_get_dict_value(merged_channel, "IOReportChannels") == NULL) {
    return std::make_error_code(std::errc::invalid_argument);
  }

  //
  // Create subscription
  //
  CFMutableDictionaryRef s;
  auto s_ref = IOReportCreateSubscription(NULL, merged_channel, &s, 0, NULL);
  if (!s_ref) {
    CFRelease(merged_channel);
    return std::make_error_code(std::errc::invalid_argument);
  }

  channel_ = merged_channel;
  subscription_ = s_ref;
  return std::error_code();
}

auto io_report::sample() -> io_report_sample {
  if (channel_ && subscription_) {
    auto ref = IOReportCreateSamples((IOReportSubscriptionRef)subscription_, (CFMutableDictionaryRef)channel_, NULL);
    return io_report_sample((void*)ref);
  }
  return io_report_sample();
}

auto io_report::sample_diff(const io_report_sample& start, const io_report_sample& end) -> io_report_sample_diff {
  io_report_sample_diff diff;

  if (end.time() <= start.time() || !start.has_ref() || !end.has_ref()) {
    // Returns an empty diff object
    return diff;
  }

  diff.sample_duration = end.time() - start.time();

  // Calculate diff
  auto ref = IOReportCreateSamplesDelta((CFDictionaryRef)start.ref(), (CFDictionaryRef)end.ref(), NULL);
  if (!ref) {
    // No diff object
    return diff;
  }

  // Decode diff

  // Get items & size
  auto items = (CFArrayRef)cf_get_dict_value(ref, "IOReportChannels");
  if (!items) {
    return diff;
  }
  auto items_size = (size_t)CFArrayGetCount(items);
  if (!items_size) {
    return diff;
  }

  for (size_t i = 0; i < items_size; ++i) {
    auto item = (CFDictionaryRef)CFArrayGetValueAtIndex(items, i);
    if (!item) {
      continue;
    }
    auto group = from_cf_string(IOReportChannelGetGroup(item));
    auto sub_group = from_cf_string(IOReportChannelGetSubGroup(item));
    auto channel_name = from_cf_string(IOReportChannelGetChannelName(item));
    auto unit = from_cf_string(IOReportChannelGetUnitLabel(item));

    if (group == "Energy Model") {
      //
      // Power consumption
      //
      if (channel_name.ends_with("GPU Energy")) {
        // GPU
        diff.power_consumptions[device_type::gpu] += calculate_watts(item, unit, diff.sample_duration);
      } else if (channel_name.ends_with("CPU Energy")) {
        // CPU
        diff.power_consumptions[device_type::cpu] += calculate_watts(item, unit, diff.sample_duration);
      } else if (channel_name.starts_with("ANE")) {
        // Apple NPU
        diff.power_consumptions[device_type::npu] += calculate_watts(item, unit, diff.sample_duration);
      } else if (channel_name.starts_with("DRAM")) {
        // DRAM
        diff.power_consumptions[device_type::dram] += calculate_watts(item, unit, diff.sample_duration);
      } else if (channel_name.starts_with("GPU SRAM")) {
        // GPU Ram
        diff.power_consumptions[device_type::gpu_sram] += calculate_watts(item, unit, diff.sample_duration);
      } else if (channel_name.starts_with("ECPU") && !channel_name.ends_with("_SRAM")) {
        // ECPU
        size_t core = 0;
        // Parse core
        auto [ptr, ec] = std::from_chars(channel_name.c_str() + 4, channel_name.c_str() + channel_name.size(), core);
        if (ec == std::errc() && ptr == channel_name.c_str() + channel_name.size()) {
          auto pc = calculate_watts(item, unit, diff.sample_duration);
          diff.cpu_core_power_consumptions.emplace_back(device_type::e_cpu_core, core + 1, pc);
        }
      } else if (channel_name.starts_with("PCPU") && !channel_name.ends_with("_SRAM")) {
        // PCPU
        size_t core = 0;
        // Parse core
        auto [ptr, ec] = std::from_chars(channel_name.c_str() + 4, channel_name.c_str() + channel_name.size(), core);
        if (ec == std::errc() && ptr == channel_name.c_str() + channel_name.size()) {
          auto pc = calculate_watts(item, unit, diff.sample_duration);
          diff.cpu_core_power_consumptions.emplace_back(device_type::p_cpu_core, core + 1, pc);
        }
      } else if (channel_name.starts_with("ECPU") && channel_name.ends_with("_SRAM")) {
        // ECPU SRAM
        size_t core = 0;
        // Parse core
        auto [ptr, ec] = std::from_chars(channel_name.c_str() + 4, channel_name.c_str() + channel_name.size(), core);
        if (ec == std::errc()) {
          auto pc = calculate_watts(item, unit, diff.sample_duration);
          diff.cpu_core_power_consumptions.emplace_back(device_type::e_cpu_core_sram, core + 1, pc);
        }
      } else if (channel_name.starts_with("PCPU") && channel_name.ends_with("_SRAM")) {
        // PCPU SRAM
        size_t core = 0;
        // Parse core
        auto [ptr, ec] = std::from_chars(channel_name.c_str() + 4, channel_name.c_str() + channel_name.size(), core);
        if (ec == std::errc()) {
          auto pc = calculate_watts(item, unit, diff.sample_duration);
          diff.cpu_core_power_consumptions.emplace_back(device_type::p_cpu_core_sram, core + 1, pc);
        }
      }
    } else if (group == "CPU Stats" && sub_group == "CPU Core Performance States") {
      //
      // Cpu freq
      //
      if (channel_name.starts_with("ECPU") && sys_info_.e_cpu_dynamic_freq_list().size() > 0) {
        size_t core = 0;
        // Parse core
        auto [ptr, ec] = std::from_chars(channel_name.c_str() + 4, channel_name.c_str() + channel_name.size(), core);
        if (ec == std::errc() && ptr == channel_name.c_str() + channel_name.size()) {
          auto freq = calculate_freq(item, sys_info_.e_cpu_dynamic_freq_list());
          diff.cpu_core_freqs.emplace_back(device_type::e_cpu_core, core + 1, freq);
        }
      } else if (channel_name.starts_with("PCPU") && sys_info_.p_cpu_dynamic_freq_list().size() > 0) {
        size_t core = 0;
        // Parse core
        auto [ptr, ec] = std::from_chars(channel_name.c_str() + 4, channel_name.c_str() + channel_name.size(), core);
        if (ec == std::errc() && ptr == channel_name.c_str() + channel_name.size()) {
          auto freq = calculate_freq(item, sys_info_.p_cpu_dynamic_freq_list());
          diff.cpu_core_freqs.emplace_back(device_type::p_cpu_core, core + 1, freq);
        }
      }
    } else if (group == "GPU Stats" && sub_group == "GPU Performance States" &&
               sys_info_.gpu_dynamic_freq_list().size() > 0) {
      //
      // Gpu freq
      //
      diff.gpu_freq = calculate_freq(item, sys_info_.gpu_dynamic_freq_list());
    }
  }

  return diff;
}

// ------ io_report_sample implementation

io_report_sample::~io_report_sample() {
  if (ref_) {
    CFRelease(ref_);
    ref_ = nullptr;
  }
}

io_report_sample& io_report_sample::operator=(io_report_sample&& other) {
  if (ref_) {
    CFRelease(ref_);
  }
  time_ = other.time_;
  ref_ = other.ref_;
  other.time_ = time_point();
  other.ref_ = nullptr;
  return *this;
}

#endif
