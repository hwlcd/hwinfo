// Osx disk io reader
#if defined(__APPLE__) && defined(__arm64__)

#include "hwinfo/detail/osx_disk_io_reader.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOBlockStorageDriver.h>
#include <IOKit/storage/IOMedia.h>

#include <expected>
#include <system_error>

#include "hwinfo/detail/osx_util.hpp"
#include "hwinfo/detail/util.hpp"

using namespace hwlcd::hwinfo;
using namespace hwlcd::hwinfo::detail;
using namespace hwlcd::hwinfo::detail::osx;

auto get_disk_io_counter() -> std::expected<std::unordered_map<std::string, disk_io_counter>, std::error_code> {
  auto ref = IOServiceMatching(kIOMediaClass);
  if (!ref) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }
  // Iterator disk
  io_iterator_t iterator;
  if (auto ec = IOServiceGetMatchingServices(kIOMainPortDefault, ref, &iterator); ec != KERN_SUCCESS) {
    return std::unexpected(make_mach_error_code(ec));
  }
  if (!iterator) {
    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  std::unordered_map<std::string, disk_io_counter> counters;

  while (true) {
    auto disk = IOIteratorNext(iterator);
    if (!disk) {
      break;
    }
    // Get disk service
    auto _0 = make_scope_guard([&disk]() { IOObjectRelease(disk); });
    io_registry_entry_t parent = IO_OBJECT_NULL;
    if (IORegistryEntryGetParentEntry(disk, kIOServicePlane, &parent) != KERN_SUCCESS || !parent) {
      continue;
    }
    auto _1 = make_scope_guard([&parent]() { IOObjectRelease(parent); });
    if (!IOObjectConformsTo(parent, "IOBlockStorageDriver")) {
      continue;
    }
    // Get disk name
    std::string disk_name;
    {
      CFMutableDictionaryRef disk_props;
      if (IORegistryEntryCreateCFProperties(disk, &disk_props, kCFAllocatorDefault, 0) != KERN_SUCCESS || !disk_props) {
        continue;
      }
      auto _ = make_scope_guard([&disk_props]() { CFRelease(disk_props); });
      auto disk_name_ref = (CFStringRef)CFDictionaryGetValue(disk_props, CFSTR(kIOBSDNameKey));
      if (!disk_name_ref) {
        continue;
      }
      disk_name = from_cf_string(disk_name_ref);
    }
    // Get io counters
    {
      CFMutableDictionaryRef props;
      if (IORegistryEntryCreateCFProperties(parent, &props, kCFAllocatorDefault, 0) != KERN_SUCCESS) {
        continue;
      }
      auto _ = make_scope_guard([&props]() { CFRelease(props); });
      // Get stats value from props dict
      auto stats_dict = (CFDictionaryRef)CFDictionaryGetValue(props, CFSTR(kIOBlockStorageDriverStatisticsKey));
      if (!stats_dict) {
        continue;
      }
      CFNumberRef number_ref;
      // All following values are calculated since the block storage driver was instantiated.
      int64_t read_opts = 0, write_opts = 0, read_bytes = 0, write_bytes = 0;
      int64_t read_time = 0, write_time = 0;  // in nanoseconds
      if (number_ref = (CFNumberRef)CFDictionaryGetValue(stats_dict, CFSTR(kIOBlockStorageDriverStatisticsReadsKey));
          number_ref) {
        CFNumberGetValue(number_ref, kCFNumberSInt64Type, &read_opts);
      }
      if (number_ref = (CFNumberRef)CFDictionaryGetValue(stats_dict, CFSTR(kIOBlockStorageDriverStatisticsWritesKey));
          number_ref) {
        CFNumberGetValue(number_ref, kCFNumberSInt64Type, &write_opts);
      }
      if (number_ref =
              (CFNumberRef)CFDictionaryGetValue(stats_dict, CFSTR(kIOBlockStorageDriverStatisticsBytesReadKey));
          number_ref) {
        CFNumberGetValue(number_ref, kCFNumberSInt64Type, &read_bytes);
      }
      if (number_ref =
              (CFNumberRef)CFDictionaryGetValue(stats_dict, CFSTR(kIOBlockStorageDriverStatisticsBytesWrittenKey));
          number_ref) {
        CFNumberGetValue(number_ref, kCFNumberSInt64Type, &write_bytes);
      }
      if (number_ref =
              (CFNumberRef)CFDictionaryGetValue(stats_dict, CFSTR(kIOBlockStorageDriverStatisticsTotalReadTimeKey));
          number_ref) {
        CFNumberGetValue(number_ref, kCFNumberSInt64Type, &read_time);
      }
      if (number_ref =
              (CFNumberRef)CFDictionaryGetValue(stats_dict, CFSTR(kIOBlockStorageDriverStatisticsTotalWriteTimeKey));
          number_ref) {
        CFNumberGetValue(number_ref, kCFNumberSInt64Type, &write_time);
      }
      // Add io counter
      disk_io_counter counter{
          .read_opts = read_opts > 0 ? read_opts : 0,
          .write_opts = write_opts > 0 ? write_opts : 0,
          .read_bytes = read_bytes > 0 ? read_bytes : 0,
          .write_bytes = write_bytes > 0 ? write_bytes : 0,
          .read_time = read_time > 0 ? read_time : 0,
          .write_time = write_time > 0 ? write_time : 0,
      };
      counters.emplace(std::move(disk_name), std::move(counter));
    }
  }

  IOObjectRelease(iterator);
  return counters;
}

auto disk_io_reader::sample() -> disk_io_sample {
  disk_io_sample sample;

  get_disk_io_counter()
      .and_then([&sample](std::unordered_map<std::string, disk_io_counter>&& counters) {
        sample.counters = std::move(counters);
        return std::expected<void, std::error_code>();
      })
      .or_else([&sample](std::error_code&& ec) {
        sample.errors.emplace_back(std::move(ec));
        return std::expected<void, std::error_code>();
      });

  return sample;
}

auto disk_io_reader::sample_diff(const disk_io_sample& start, const disk_io_sample& end) -> disk_io_sample_diff {
  disk_io_sample_diff diff;

  for (const auto& [name, start_counter] : start.counters) {
    if (const auto it = end.counters.find(name); it != end.counters.end()) {
      const auto& end_counter = it->second;
      diff.counters.emplace(name, disk_io_counter{
                                      .read_opts = start_counter.read_opts > 0 && end_counter.read_opts > 0 &&
                                                           end_counter.read_opts >= start_counter.read_opts
                                                       ? end_counter.read_opts - start_counter.read_opts
                                                       : 0,
                                      .write_opts = start_counter.write_opts > 0 && end_counter.write_opts > 0 &&
                                                            end_counter.write_opts >= start_counter.write_opts
                                                        ? end_counter.write_opts - start_counter.write_opts
                                                        : 0,
                                      .read_bytes = start_counter.read_bytes > 0 && end_counter.read_bytes > 0 &&
                                                            end_counter.read_bytes >= start_counter.read_bytes
                                                        ? end_counter.read_bytes - start_counter.read_bytes
                                                        : 0,
                                      .write_bytes = start_counter.write_bytes > 0 && end_counter.write_bytes > 0 &&
                                                             end_counter.write_bytes >= start_counter.write_bytes
                                                         ? end_counter.write_bytes - start_counter.write_bytes
                                                         : 0,
                                      .read_time = start_counter.read_time > 0 && end_counter.read_time > 0 &&
                                                           end_counter.read_time >= start_counter.read_time
                                                       ? end_counter.read_time - start_counter.read_time
                                                       : 0,
                                      .write_time = start_counter.write_time > 0 && end_counter.write_time > 0 &&
                                                            end_counter.write_time >= start_counter.write_time
                                                        ? end_counter.write_time - start_counter.write_time
                                                        : 0,
                                  });
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
