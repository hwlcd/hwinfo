// hwinfo osx implementation

#ifdef __APPLE__

#include "hwinfo/hwinfo.hpp"

using namespace hwlcd::hwinfo;
using namespace hwlcd::hwinfo::detail;
using namespace hwlcd::hwinfo::detail::osx;

hwinfo::hwinfo() : sys_info_(), smc_reader_(sys_info_), io_report_(sys_info_) {}

auto hwinfo::init() -> std::error_code {
  std::error_code ec;
  if (ec = sys_info_.init(); ec) {
    return ec;
  }
  if (ec = smc_reader_.init(); ec) {
    return ec;
  }
  if (ec = io_report_.init(); ec) {
    return ec;
  }
  return std::error_code();
}

auto hwinfo::sample(bool major) -> hwlcd::hwinfo::sample {
  hwlcd::hwinfo::sample sample(major);

  sample.smc_sample_ = smc_reader_.sample();

  if (major) {
    sample.io_report_sample_ = io_report_.sample();
    sample.cpu_load_sample_ = cpu_load_reader_.sample();
    sample.mem_info_sample_ = mem_info_reader_.sample();
    sample.disk_io_sample_ = disk_io_reader_.sample();
    sample.net_io_sample_ = net_io_reader_.sample();
  }

  return sample;
}

#endif
