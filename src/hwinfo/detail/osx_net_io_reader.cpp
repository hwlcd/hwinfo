// Osx network io reader
#if defined(__APPLE__) && defined(__arm64__)

#include "hwinfo/detail/osx_net_io_reader.hpp"

#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/vm_map.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <sys/socket.h>

#include <expected>

#include "hwinfo/detail/osx_util.hpp"

using namespace hwlcd::hwinfo;
using namespace hwlcd::hwinfo::detail;
using namespace hwlcd::hwinfo::detail::osx;

auto get_network_io_counters() -> std::expected<std::unordered_map<std::string, net_io_counter>, std::error_code> {
  int mib[6] = {
      CTL_NET,         // networking subsystem
      PF_ROUTE,        // type of information
      0,               // protocol (IPPROTO_xxx)
      0,               // address family
      NET_RT_IFLIST2,  // operation
      0,
  };

  std::unordered_map<std::string, net_io_counter> counters;
  auto ec = sysctl_dynamic_buffer<32 * 1024>(mib, 6, [&counters](const char* buffer, size_t size) -> std::error_code {
    const char* p_end = buffer + size;
    const char* p_read = buffer;

    while (p_read < p_end) {
      // Read header
      if (p_read + sizeof(if_msghdr) > p_end) {
        break;
      }
      const if_msghdr* p_header = (if_msghdr*)p_read;
      if (p_header->ifm_msglen == 0 || p_read + p_header->ifm_msglen > p_end) {
        return std::make_error_code(std::errc::bad_message);
      }
      // Move pointer to next header
      p_read += p_header->ifm_msglen;
      // Read counter data
      if (p_header->ifm_type == RTM_IFINFO2) {
        const if_msghdr2* p_header2 = (if_msghdr2*)p_header;
        if ((const char*)(p_header2 + 1) > p_end) {
          return std::make_error_code(std::errc::bad_message);
        }
        // Read if name
        const sockaddr_dl* p_sock_addr = (sockaddr_dl*)(p_header2 + 1);
        if ((const char*)(p_sock_addr + 1) > p_end) {
          return std::make_error_code(std::errc::bad_message);
        }
        std::string if_name((const char*)p_sock_addr->sdl_data, p_sock_addr->sdl_nlen);
        if (if_name.size() > 0) {
          net_io_counter counter{
              .read_bytes = p_header2->ifm_data.ifi_ibytes,
              .write_bytes = p_header2->ifm_data.ifi_obytes,
              .read_packets = p_header2->ifm_data.ifi_ipackets,
              .write_packets = p_header2->ifm_data.ifi_opackets,
              .read_errors = p_header2->ifm_data.ifi_ierrors,
              .write_errors = p_header2->ifm_data.ifi_oerrors,
          };
          counters.emplace(std::move(if_name), std::move(counter));
        }
      }
    }

    return std::error_code();
  });

  if (ec) {
    return std::unexpected(ec);
  }
  return counters;
}

auto net_io_reader::sample() -> net_io_sample {
  net_io_sample sample;

  get_network_io_counters()
      .and_then([&sample](std::unordered_map<std::string, net_io_counter>&& counters) {
        sample.counters = std::move(counters);
        return std::expected<void, std::error_code>();
      })
      .or_else([&sample](std::error_code&& ec) {
        sample.errors.emplace_back(std::move(ec));
        return std::expected<void, std::error_code>();
      });

  return sample;
}

auto net_io_reader::sample_diff(const net_io_sample& start, const net_io_sample& end) -> net_io_sample_diff {
  net_io_sample_diff diff;

  for (const auto& [name, start_counter] : start.counters) {
    if (const auto it = end.counters.find(name); it != end.counters.end()) {
      const auto& end_counter = it->second;
      diff.counters.emplace(name,
                            net_io_counter{
                                .read_bytes = start_counter.read_bytes > 0 && end_counter.read_bytes > 0 &&
                                                      end_counter.read_bytes >= start_counter.read_bytes
                                                  ? end_counter.read_bytes - start_counter.read_bytes
                                                  : 0,
                                .write_bytes = start_counter.write_bytes > 0 && end_counter.write_bytes > 0 &&
                                                       end_counter.write_bytes >= start_counter.write_bytes
                                                   ? end_counter.write_bytes - start_counter.write_bytes
                                                   : 0,
                                .read_packets = start_counter.read_packets > 0 && end_counter.read_packets > 0 &&
                                                        end_counter.read_packets >= start_counter.read_packets
                                                    ? end_counter.read_packets - start_counter.read_packets
                                                    : 0,
                                .write_packets = start_counter.write_packets > 0 && end_counter.write_packets > 0 &&
                                                         end_counter.write_packets >= start_counter.write_packets
                                                     ? end_counter.write_packets - start_counter.write_packets
                                                     : 0,
                                .read_errors = start_counter.read_errors > 0 && end_counter.read_errors > 0 &&
                                                       end_counter.read_errors >= start_counter.read_errors
                                                   ? end_counter.read_errors - start_counter.read_errors
                                                   : 0,
                                .write_errors = start_counter.write_errors > 0 && end_counter.write_errors > 0 &&
                                                        end_counter.write_errors >= start_counter.write_errors
                                                    ? end_counter.write_errors - start_counter.write_errors
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
