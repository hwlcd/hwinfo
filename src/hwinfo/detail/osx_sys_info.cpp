// osx system info
#if defined(__APPLE__) && defined(__arm64__)

#include "hwinfo/detail/osx_sys_info.hpp"

#include <IOKit/IOBSD.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <IOKit/storage/IOMedia.h>

#include <expected>
#include <memory>
#include <string_view>
#include <vector>

#include "hwinfo/detail/osx_util.hpp"
#include "hwinfo/detail/util.hpp"

using namespace hwlcd::hwinfo::detail;
using namespace hwlcd::hwinfo::detail::osx;

auto get_cpu_brand() -> std::expected<cpu_brand, std::error_code> {
  char buf[100];
  if (auto ec = sysctlbyname_string<100>("machdep.cpu.brand_string", buf); ec) {
    return std::unexpected(ec);
  }
  std::string_view sv(buf, strnlen(buf, 100));
  if (sv.contains("M1")) {
    return cpu_brand::m1;
  } else if (sv.contains("M2")) {
    return cpu_brand::m2;
  } else if (sv.contains("M3")) {
    return cpu_brand::m3;
  } else if (sv.contains("M4")) {
    return cpu_brand::m4;
  } else if (sv.contains("M5")) {
    return cpu_brand::m5;
  }
  return cpu_brand::unknown;
}

auto get_xpu_dynamic_freq_list(cpu_brand cpub,  //
                               std::vector<xpu_dynamic_freq>* p_out_e_cpu_freq_list,
                               std::vector<xpu_dynamic_freq>* p_out_p_cpu_freq_list,
                               std::vector<xpu_dynamic_freq>* p_out_s_cpu_freq_list,
                               std::vector<xpu_dynamic_freq>* p_out_gpu_freq_list) -> std::error_code {
  if (cpub == cpu_brand::unknown) {
    return std::error_code();
  }
  auto ref = IOServiceMatching("AppleARMIODevice");
  if (!ref) {
    return std::make_error_code(std::errc::invalid_argument);
  }
  kern_return_t k_rtn;
  io_iterator_t iterator;
  k_rtn = IOServiceGetMatchingServices(kIOMainPortDefault, ref, &iterator);
  if (k_rtn != KERN_SUCCESS) {
    return make_mach_error_code(k_rtn);
  }

  while (true) {
    auto device = IOIteratorNext(iterator);
    if (!device) {
      break;
    }
    // Defer release device
    auto _0 = make_scope_guard([&device]() { IOObjectRelease(device); });

    io_name_t name_buffer;
    if (IORegistryEntryGetName(device, name_buffer) != KERN_SUCCESS) {
      // Failed to get name
      continue;
    }
    std::string_view name(name_buffer);
    if (name == "pmgr") {
      // Read properties
      CFMutableDictionaryRef dict_ref;
      if (IORegistryEntryCreateCFProperties(device, &dict_ref, kCFAllocatorDefault, 0) != KERN_SUCCESS) {
        continue;
      }
      // Defer release dict_ref
      auto _1 = make_scope_guard([&dict_ref]() { CFRelease(dict_ref); });

      auto get_freq = [&dict_ref, cpub](const std::string_view& key, std::vector<xpu_dynamic_freq>* p_out,
                                        size_t skip = 0) {
        if (!p_out) {
          return;
        }
        // Read bytes
        auto value_ref = (CFDataRef)cf_get_dict_value(dict_ref, key);
        if (!value_ref) {
          return;
        }
        auto value_size = CFDataGetLength(value_ref);
        if (value_size % 8 != 0) {  // Value is a list of pair (freq, voltage) which is 8 bytes of each pair
          return;
        }
        std::vector<uint8_t> buffer(value_size);
        CFDataGetBytes(value_ref, CFRange{0, value_size}, buffer.data());
        // Get freq list
        size_t count = value_size / 8;
        for (size_t i = skip; i < count; ++i) {
          uint32_t freq = static_cast<uint32_t>(buffer[i * 8]) |  //
                          static_cast<uint32_t>(buffer[i * 8 + 1]) << 8 |
                          static_cast<uint32_t>(buffer[i * 8 + 2]) << 16 |
                          static_cast<uint32_t>(buffer[i * 8 + 3]) << 24;
          if (cpub >= cpu_brand::m4) {
            p_out->push_back({freq / 1000});
          } else {
            p_out->push_back({freq / 1000000});
          }
        }
      };

      if (cpub >= cpu_brand::m5) {
        get_freq("voltage-states22-sram", p_out_p_cpu_freq_list);
        get_freq("voltage-states5-sram", p_out_s_cpu_freq_list);
      } else {
        get_freq("voltage-states1-sram", p_out_e_cpu_freq_list);
        get_freq("voltage-states5-sram", p_out_p_cpu_freq_list);
      }

      // NOTE: Skip the first 0 value
      get_freq("voltage-states9", p_out_gpu_freq_list, 1);

      // Exit loop
      break;
    }
  }

  IOObjectRelease(iterator);
  return std::error_code();
}

// ------ sys_info implementation

auto sys_info::init() -> std::error_code {
  // Get cpu brand
  if (auto rtn = get_cpu_brand().and_then([this](auto&& cb) {
        cpu_brand_ = cb;
        return std::expected<void, std::error_code>();
      });
      !rtn) {
    return rtn.error();
  };
  std::error_code ec;
  // Get cpu core number
  if (ec = sysctlbyname_scalar<size_t>("hw.perflevel1.physicalcpu", &e_cpu_core_num_); ec) {
    return ec;
  }
  if (ec = sysctlbyname_scalar<size_t>("hw.perflevel0.physicalcpu", &p_cpu_core_num_); ec) {
    return ec;
  }
  // Get cpu / gpu dynamic frequency list
  if (ec = get_xpu_dynamic_freq_list(cpu_brand_, &e_cpu_dynamic_freq_list_, &p_cpu_dynamic_freq_list_,
                                     &s_cpu_dynamic_freq_list_, &gpu_dynamic_freq_list_);
      ec) {
    return ec;
  }
  // Get memory info
  if (ec = sysctlbyname_scalar<uint64_t>("hw.memsize", &memory_total_size_); ec) {
    return ec;
  }
  if (ec = sysctlbyname_scalar<uint64_t>("vm.pagesize", &memory_page_size_); ec) {
    return ec;
  }

  return std::error_code();
}

#endif
