// Osx smc reader
//
//  The smc sensor keys depends on the following projects:
//
//    - https://github.com/exelban/stats
//      https://github.com/exelban/stats/blob/ba4a3811edb0c62f59f6c464e729e4fa1fae1903/Modules/Sensors/values.swift
//    - https://github.com/dkorunic/iSMC
//      https://github.com/dkorunic/iSMC/blob/bb16b461d01b345c267e58c532ddbfd074070e3f/src/temp.txt
//
//
#if defined(__APPLE__) && defined(__arm64__)

#include "hwinfo/detail/osx_smc_reader.hpp"

#include <IOKit/IOKitLib.h>

#include <array>
#include <unordered_map>

using namespace hwlcd::hwinfo;
using namespace hwlcd::hwinfo::detail;
using namespace hwlcd::hwinfo::detail::osx;

constexpr static uint32_t smc_kernel_index = 2;
constexpr static uint8_t smc_cmd_read_bytes = 5;
constexpr static uint8_t smc_cmd_read_key_info = 9;

using smc_bytes = char[32];

struct smc_version {
  char major;
  char minor;
  char build;
  char reserved[1];
  uint16_t release;
};

struct smc_prochot_limit {
  uint16_t version;
  uint16_t length;
  uint32_t cpu_p_limit;
  uint32_t gpu_p_limit;
  uint32_t mem_p_limit;
};

struct smc_key_info {
  uint32_t data_size;
  uint32_t data_type;
  char data_attributes;
};

struct smc_data {
  uint32_t key;
  smc_version version;
  smc_prochot_limit prochot_limit;
  smc_key_info key_info;
  uint8_t unknown1;
  uint8_t unknown2;
  uint8_t cmd;
  uint32_t unknown3;
  smc_bytes data;
};

constexpr auto smc_str_to_uint32(const char str[5]) -> uint32_t {
  return (static_cast<uint32_t>(str[0]) << 24) + (static_cast<uint32_t>(str[1]) << 16) +
         (static_cast<uint32_t>(str[2]) << 8) + static_cast<uint32_t>(str[3]);
}

constexpr void smc_uint32_to_str(uint32_t value, char out_str[5]) {
  out_str[0] = static_cast<char>(value >> 24);
  out_str[1] = static_cast<char>(value >> 16);
  out_str[2] = static_cast<char>(value >> 8);
  out_str[3] = static_cast<char>(value);
  out_str[4] = '\0';
}

constexpr uint32_t smc_data_type_flt = smc_str_to_uint32("flt ");

template <uint32_t T>
struct smc_data_type_traits {};

template <>
struct smc_data_type_traits<smc_data_type_flt> {
  using decoded_type = float;

  union smc_float_union {
    float f;
    char b[4];
  };

  static auto decode(smc_bytes data, size_t size, float* p_out) -> std::error_code {
    if (size != 4) {
      return std::make_error_code(std::errc::invalid_argument);
    }
    if (p_out) {
      smc_float_union flt;
      flt.b[0] = data[0];
      flt.b[1] = data[1];
      flt.b[2] = data[2];
      flt.b[3] = data[3];
      *p_out = flt.f;
    }
    return std::error_code();
  }
};

template <uint32_t smc_data_type>
auto decode_smc_data(smc_bytes data, size_t size, typename smc_data_type_traits<smc_data_type>::decoded_type* p_out)
    -> std::error_code {
  return smc_data_type_traits<smc_data_type>::decode(data, size, p_out);
}

auto read_smc_key_info(io_connect_t conn, uint32_t key, uint32_t* p_out_data_size, uint32_t* p_out_data_type)
    -> std::error_code {
  smc_data input_data = {.key = key, .cmd = smc_cmd_read_key_info};
  smc_data output_data = {};
  size_t input_data_size = sizeof(smc_data);
  size_t output_data_size = sizeof(smc_data);

  if (IOConnectCallStructMethod(conn, smc_kernel_index, &input_data, input_data_size, &output_data,
                                &output_data_size)) {
    return std::make_error_code(std::errc::invalid_argument);
  }

  if (p_out_data_size) {
    *p_out_data_size = output_data.key_info.data_size;
  }
  if (p_out_data_type) {
    *p_out_data_type = output_data.key_info.data_type;
  }
  return std::error_code();
}

template <uint32_t T>
auto read_smc_data(io_connect_t conn, uint32_t key, uint32_t data_size,
                   typename smc_data_type_traits<T>::decoded_type* p_out_data) -> std::error_code {
  if (!p_out_data) {
    return std::make_error_code(std::errc::no_buffer_space);
  }

  smc_data input_data = {.key = key, .key_info = {.data_size = data_size}, .cmd = smc_cmd_read_bytes};
  smc_data output_data = {};
  size_t input_data_size = sizeof(smc_data);
  size_t output_data_size = sizeof(smc_data);

  if (IOConnectCallStructMethod(conn, smc_kernel_index, &input_data, input_data_size, &output_data,
                                &output_data_size)) {
    return std::make_error_code(std::errc::invalid_argument);
  }

  return decode_smc_data<T>(output_data.data, data_size, p_out_data);
}

/**
 * @brief A known key
 *
 */
template <MetricType T>
struct known_key {
  /**
   * @brief Key
   *
   */
  uint32_t key = 0;
  /**
   * @brief Expected data size
   *
   */
  uint32_t data_size = 0;
  /**
   * @brief Expected data type
   *
   */
  uint32_t data_type = 0;
  /**
   * @brief Empty metric object of this key
   *
   */
  T empty_metric = {};
};

// ------ Cpu core temperature

static constexpr std::array<known_key<device_sequential_metric<float>>, 10> known_m1_cpu_core_temperature_keys{{
    {smc_str_to_uint32("Tp09"), 4, smc_data_type_flt, {device_type::e_cpu_core, 1, 0}},
    {smc_str_to_uint32("Tp0T"), 4, smc_data_type_flt, {device_type::e_cpu_core, 2, 0}},
    {smc_str_to_uint32("Tp01"), 4, smc_data_type_flt, {device_type::p_cpu_core, 1, 0}},
    {smc_str_to_uint32("Tp05"), 4, smc_data_type_flt, {device_type::p_cpu_core, 2, 0}},
    {smc_str_to_uint32("Tp0D"), 4, smc_data_type_flt, {device_type::p_cpu_core, 3, 0}},
    {smc_str_to_uint32("Tp0H"), 4, smc_data_type_flt, {device_type::p_cpu_core, 4, 0}},
    {smc_str_to_uint32("Tp0L"), 4, smc_data_type_flt, {device_type::p_cpu_core, 5, 0}},
    {smc_str_to_uint32("Tp0P"), 4, smc_data_type_flt, {device_type::p_cpu_core, 6, 0}},
    {smc_str_to_uint32("Tp0X"), 4, smc_data_type_flt, {device_type::p_cpu_core, 7, 0}},
    {smc_str_to_uint32("Tp0b"), 4, smc_data_type_flt, {device_type::p_cpu_core, 8, 0}},
}};

static constexpr std::array<known_key<device_sequential_metric<float>>, 14> known_m2_cpu_core_temperature_keys{{
    {smc_str_to_uint32("Tp1h"), 4, smc_data_type_flt, {device_type::e_cpu_core, 1, 0}},
    {smc_str_to_uint32("Tp1t"), 4, smc_data_type_flt, {device_type::e_cpu_core, 2, 0}},
    {smc_str_to_uint32("Tp1p"), 4, smc_data_type_flt, {device_type::e_cpu_core, 3, 0}},
    {smc_str_to_uint32("Tp1l"), 4, smc_data_type_flt, {device_type::e_cpu_core, 4, 0}},
    {smc_str_to_uint32("Tp01"), 4, smc_data_type_flt, {device_type::p_cpu_core, 1, 0}},
    {smc_str_to_uint32("Tp05"), 4, smc_data_type_flt, {device_type::p_cpu_core, 2, 0}},
    {smc_str_to_uint32("Tp09"), 4, smc_data_type_flt, {device_type::p_cpu_core, 3, 0}},
    {smc_str_to_uint32("Tp0D"), 4, smc_data_type_flt, {device_type::p_cpu_core, 4, 0}},
    {smc_str_to_uint32("Tp0X"), 4, smc_data_type_flt, {device_type::p_cpu_core, 5, 0}},
    {smc_str_to_uint32("Tp0b"), 4, smc_data_type_flt, {device_type::p_cpu_core, 6, 0}},
    {smc_str_to_uint32("Tp0f"), 4, smc_data_type_flt, {device_type::p_cpu_core, 7, 0}},
    {smc_str_to_uint32("Tp0j"), 4, smc_data_type_flt, {device_type::p_cpu_core, 8, 0}},
}};

static constexpr std::array<known_key<device_sequential_metric<float>>, 16> known_m3_cpu_core_temperature_keys{{
    {smc_str_to_uint32("Te05"), 4, smc_data_type_flt, {device_type::e_cpu_core, 1, 0}},
    {smc_str_to_uint32("Te0L"), 4, smc_data_type_flt, {device_type::e_cpu_core, 2, 0}},
    {smc_str_to_uint32("Te0P"), 4, smc_data_type_flt, {device_type::e_cpu_core, 3, 0}},
    {smc_str_to_uint32("Te0S"), 4, smc_data_type_flt, {device_type::e_cpu_core, 4, 0}},
    {smc_str_to_uint32("Tf04"), 4, smc_data_type_flt, {device_type::p_cpu_core, 1, 0}},
    {smc_str_to_uint32("Tf09"), 4, smc_data_type_flt, {device_type::p_cpu_core, 2, 0}},
    {smc_str_to_uint32("Tf0A"), 4, smc_data_type_flt, {device_type::p_cpu_core, 3, 0}},
    {smc_str_to_uint32("Tf0B"), 4, smc_data_type_flt, {device_type::p_cpu_core, 4, 0}},
    {smc_str_to_uint32("Tf0D"), 4, smc_data_type_flt, {device_type::p_cpu_core, 5, 0}},
    {smc_str_to_uint32("Tf0E"), 4, smc_data_type_flt, {device_type::p_cpu_core, 6, 0}},
    {smc_str_to_uint32("Tf44"), 4, smc_data_type_flt, {device_type::p_cpu_core, 7, 0}},
    {smc_str_to_uint32("Tf49"), 4, smc_data_type_flt, {device_type::p_cpu_core, 8, 0}},
    {smc_str_to_uint32("Tf4A"), 4, smc_data_type_flt, {device_type::p_cpu_core, 9, 0}},
    {smc_str_to_uint32("Tf4B"), 4, smc_data_type_flt, {device_type::p_cpu_core, 10, 0}},
    {smc_str_to_uint32("Tf4D"), 4, smc_data_type_flt, {device_type::p_cpu_core, 11, 0}},
    {smc_str_to_uint32("Tf4E"), 4, smc_data_type_flt, {device_type::p_cpu_core, 12, 0}},
}};

static constexpr std::array<known_key<device_sequential_metric<float>>, 16> known_m4_cpu_core_temperature_keys{{
    {smc_str_to_uint32("Te05"), 4, smc_data_type_flt, {device_type::e_cpu_core, 1, 0}},
    {smc_str_to_uint32("Te0S"), 4, smc_data_type_flt, {device_type::e_cpu_core, 2, 0}},
    {smc_str_to_uint32("Te09"), 4, smc_data_type_flt, {device_type::e_cpu_core, 3, 0}},
    {smc_str_to_uint32("Te0H"), 4, smc_data_type_flt, {device_type::e_cpu_core, 4, 0}},
    {smc_str_to_uint32("Tp01"), 4, smc_data_type_flt, {device_type::p_cpu_core, 1, 0}},
    {smc_str_to_uint32("Tp05"), 4, smc_data_type_flt, {device_type::p_cpu_core, 2, 0}},
    {smc_str_to_uint32("Tp09"), 4, smc_data_type_flt, {device_type::p_cpu_core, 3, 0}},
    {smc_str_to_uint32("Tp0D"), 4, smc_data_type_flt, {device_type::p_cpu_core, 4, 0}},
    {smc_str_to_uint32("Tp0V"), 4, smc_data_type_flt, {device_type::p_cpu_core, 5, 0}},
    {smc_str_to_uint32("Tp0Y"), 4, smc_data_type_flt, {device_type::p_cpu_core, 6, 0}},
    {smc_str_to_uint32("Tp0b"), 4, smc_data_type_flt, {device_type::p_cpu_core, 7, 0}},
    {smc_str_to_uint32("Tp0e"), 4, smc_data_type_flt, {device_type::p_cpu_core, 8, 0}},
}};

static constexpr std::array<known_key<device_sequential_metric<float>>, 18> known_m5_cpu_core_temperature_keys{{
    {smc_str_to_uint32("Tp00"), 4, smc_data_type_flt, {device_type::s_cpu_core, 1, 0}},
    {smc_str_to_uint32("Tp04"), 4, smc_data_type_flt, {device_type::s_cpu_core, 2, 0}},
    {smc_str_to_uint32("Tp08"), 4, smc_data_type_flt, {device_type::s_cpu_core, 3, 0}},
    {smc_str_to_uint32("Tp0C"), 4, smc_data_type_flt, {device_type::s_cpu_core, 4, 0}},
    {smc_str_to_uint32("Tp0G"), 4, smc_data_type_flt, {device_type::s_cpu_core, 5, 0}},
    {smc_str_to_uint32("Tp0K"), 4, smc_data_type_flt, {device_type::s_cpu_core, 6, 0}},
    {smc_str_to_uint32("Tp0O"), 4, smc_data_type_flt, {device_type::p_cpu_core, 1, 0}},
    {smc_str_to_uint32("Tp0R"), 4, smc_data_type_flt, {device_type::p_cpu_core, 2, 0}},
    {smc_str_to_uint32("Tp0U"), 4, smc_data_type_flt, {device_type::p_cpu_core, 3, 0}},
    {smc_str_to_uint32("Tp0X"), 4, smc_data_type_flt, {device_type::p_cpu_core, 4, 0}},
    {smc_str_to_uint32("Tp0a"), 4, smc_data_type_flt, {device_type::p_cpu_core, 5, 0}},
    {smc_str_to_uint32("Tp0d"), 4, smc_data_type_flt, {device_type::p_cpu_core, 6, 0}},
    {smc_str_to_uint32("Tp0g"), 4, smc_data_type_flt, {device_type::p_cpu_core, 7, 0}},
    {smc_str_to_uint32("Tp0j"), 4, smc_data_type_flt, {device_type::p_cpu_core, 8, 0}},
    {smc_str_to_uint32("Tp0m"), 4, smc_data_type_flt, {device_type::p_cpu_core, 9, 0}},
    {smc_str_to_uint32("Tp0p"), 4, smc_data_type_flt, {device_type::p_cpu_core, 10, 0}},
    {smc_str_to_uint32("Tp0u"), 4, smc_data_type_flt, {device_type::p_cpu_core, 11, 0}},
    {smc_str_to_uint32("Tp0y"), 4, smc_data_type_flt, {device_type::p_cpu_core, 12, 0}},
}};

// ------ Gpu core temperature

static constexpr std::array<known_key<device_sequential_metric<float>>, 4> known_m1_gpu_core_temperature_keys{{
    {smc_str_to_uint32("Tg05"), 4, smc_data_type_flt, {device_type::gpu_core, 1, 0}},
    {smc_str_to_uint32("Tg0D"), 4, smc_data_type_flt, {device_type::gpu_core, 2, 0}},
    {smc_str_to_uint32("Tg0L"), 4, smc_data_type_flt, {device_type::gpu_core, 3, 0}},
    {smc_str_to_uint32("Tg0T"), 4, smc_data_type_flt, {device_type::gpu_core, 4, 0}},
}};

static constexpr std::array<known_key<device_sequential_metric<float>>, 2> known_m2_gpu_core_temperature_keys{{
    {smc_str_to_uint32("Tg0f"), 4, smc_data_type_flt, {device_type::gpu_core, 1, 0}},
    {smc_str_to_uint32("Tg0j"), 4, smc_data_type_flt, {device_type::gpu_core, 2, 0}},
}};

static constexpr std::array<known_key<device_sequential_metric<float>>, 8> known_m3_gpu_core_temperature_keys{{
    {smc_str_to_uint32("Tf14"), 4, smc_data_type_flt, {device_type::gpu_core, 1, 0}},
    {smc_str_to_uint32("Tf18"), 4, smc_data_type_flt, {device_type::gpu_core, 2, 0}},
    {smc_str_to_uint32("Tf19"), 4, smc_data_type_flt, {device_type::gpu_core, 3, 0}},
    {smc_str_to_uint32("Tf1A"), 4, smc_data_type_flt, {device_type::gpu_core, 4, 0}},
    {smc_str_to_uint32("Tf24"), 4, smc_data_type_flt, {device_type::gpu_core, 5, 0}},
    {smc_str_to_uint32("Tf28"), 4, smc_data_type_flt, {device_type::gpu_core, 6, 0}},
    {smc_str_to_uint32("Tf29"), 4, smc_data_type_flt, {device_type::gpu_core, 7, 0}},
    {smc_str_to_uint32("Tf2A"), 4, smc_data_type_flt, {device_type::gpu_core, 8, 0}},
}};

static constexpr std::array<known_key<device_sequential_metric<float>>, 10> known_m4_gpu_core_temperature_keys{{
    {smc_str_to_uint32("Tg0G"), 4, smc_data_type_flt, {device_type::gpu_core, 1, 0}},
    {smc_str_to_uint32("Tg0H"), 4, smc_data_type_flt, {device_type::gpu_core, 2, 0}},
    {smc_str_to_uint32("Tg1U"), 4, smc_data_type_flt, {device_type::gpu_core, 1, 0}},
    {smc_str_to_uint32("Tg1k"), 4, smc_data_type_flt, {device_type::gpu_core, 2, 0}},
    {smc_str_to_uint32("Tg0K"), 4, smc_data_type_flt, {device_type::gpu_core, 3, 0}},
    {smc_str_to_uint32("Tg0L"), 4, smc_data_type_flt, {device_type::gpu_core, 4, 0}},
    {smc_str_to_uint32("Tg0d"), 4, smc_data_type_flt, {device_type::gpu_core, 5, 0}},
    {smc_str_to_uint32("Tg0e"), 4, smc_data_type_flt, {device_type::gpu_core, 6, 0}},
    {smc_str_to_uint32("Tg0j"), 4, smc_data_type_flt, {device_type::gpu_core, 7, 0}},
    {smc_str_to_uint32("Tg0k"), 4, smc_data_type_flt, {device_type::gpu_core, 8, 0}},
}};

static constexpr std::array<known_key<device_sequential_metric<float>>, 8> known_m5_gpu_core_temperature_keys{{
    {smc_str_to_uint32("Tg0U"), 4, smc_data_type_flt, {device_type::gpu_core, 1, 0}},
    {smc_str_to_uint32("Tg0X"), 4, smc_data_type_flt, {device_type::gpu_core, 2, 0}},
    {smc_str_to_uint32("Tg0d"), 4, smc_data_type_flt, {device_type::gpu_core, 3, 0}},
    {smc_str_to_uint32("Tg0g"), 4, smc_data_type_flt, {device_type::gpu_core, 4, 0}},
    {smc_str_to_uint32("Tg0j"), 4, smc_data_type_flt, {device_type::gpu_core, 5, 0}},
    {smc_str_to_uint32("Tg1Y"), 4, smc_data_type_flt, {device_type::gpu_core, 6, 0}},
    {smc_str_to_uint32("Tg1c"), 4, smc_data_type_flt, {device_type::gpu_core, 7, 0}},
    {smc_str_to_uint32("Tg1g"), 4, smc_data_type_flt, {device_type::gpu_core, 8, 0}},
}};

// ------ DRAM temperature

static constexpr std::array<known_key<device_sequential_metric<float>>, 4> known_m1_dram_temperature_keys{{
    {smc_str_to_uint32("Tm02"), 4, smc_data_type_flt, {device_type::dram, 1, 0}},
    {smc_str_to_uint32("Tm06"), 4, smc_data_type_flt, {device_type::dram, 2, 0}},
    {smc_str_to_uint32("Tm08"), 4, smc_data_type_flt, {device_type::dram, 3, 0}},
    {smc_str_to_uint32("Tm09"), 4, smc_data_type_flt, {device_type::dram, 4, 0}},
}};

static constexpr std::array<known_key<device_sequential_metric<float>>, 0> known_m2_dram_temperature_keys{{}};

static constexpr std::array<known_key<device_sequential_metric<float>>, 0> known_m3_dram_temperature_keys{{}};

static constexpr std::array<known_key<device_sequential_metric<float>>, 3> known_m4_dram_temperature_keys{{
    {smc_str_to_uint32("Tm0p"), 4, smc_data_type_flt, {device_type::dram, 1, 0}},
    {smc_str_to_uint32("Tm1p"), 4, smc_data_type_flt, {device_type::dram, 2, 0}},
    {smc_str_to_uint32("Tm2p"), 4, smc_data_type_flt, {device_type::dram, 3, 0}},
}};

static constexpr std::array<known_key<device_sequential_metric<float>>, 0> known_m5_dram_temperature_keys{{}};

// ------ Storage temperature

static constexpr std::array<known_key<device_sequential_metric<float>>, 1> known_storage_temperature_keys{{
    {smc_str_to_uint32("TH0x"), 4, smc_data_type_flt, {device_type::nand, 1, 0}},
}};

// ------ Battery temperature

static constexpr std::array<known_key<device_sequential_metric<float>>, 10> known_battery_temperature_keys{{
    {smc_str_to_uint32("TB0T"), 4, smc_data_type_flt, {device_type::battery, 1, 0}},
    {smc_str_to_uint32("TB1T"), 4, smc_data_type_flt, {device_type::battery, 2, 0}},
    {smc_str_to_uint32("TB2T"), 4, smc_data_type_flt, {device_type::battery, 3, 0}},
    {smc_str_to_uint32("TB3T"), 4, smc_data_type_flt, {device_type::battery, 4, 0}},
    {smc_str_to_uint32("TB4T"), 4, smc_data_type_flt, {device_type::battery, 5, 0}},
    {smc_str_to_uint32("TB5T"), 4, smc_data_type_flt, {device_type::battery, 6, 0}},
    {smc_str_to_uint32("TB6T"), 4, smc_data_type_flt, {device_type::battery, 7, 0}},
    {smc_str_to_uint32("TB7T"), 4, smc_data_type_flt, {device_type::battery, 8, 0}},
    {smc_str_to_uint32("TB8T"), 4, smc_data_type_flt, {device_type::battery, 9, 0}},
    {smc_str_to_uint32("TB9T"), 4, smc_data_type_flt, {device_type::battery, 10, 0}},
}};

// ------ Fan speed

static constexpr std::array<known_key<sequential_metric<float>>, 10> known_fan_speed_keys{{
    {smc_str_to_uint32("F0Ac"), 4, smc_data_type_flt, {1, 0}},
    {smc_str_to_uint32("F1Ac"), 4, smc_data_type_flt, {2, 0}},
    {smc_str_to_uint32("F2Ac"), 4, smc_data_type_flt, {3, 0}},
    {smc_str_to_uint32("F3Ac"), 4, smc_data_type_flt, {4, 0}},
    {smc_str_to_uint32("F4Ac"), 4, smc_data_type_flt, {5, 0}},
    {smc_str_to_uint32("F5Ac"), 4, smc_data_type_flt, {6, 0}},
    {smc_str_to_uint32("F6Ac"), 4, smc_data_type_flt, {7, 0}},
    {smc_str_to_uint32("F7Ac"), 4, smc_data_type_flt, {8, 0}},
    {smc_str_to_uint32("F8Ac"), 4, smc_data_type_flt, {9, 0}},
    {smc_str_to_uint32("F9Ac"), 4, smc_data_type_flt, {10, 0}},
}};

// Ignore any errors since we can't handle them easily
template <MetricType M, size_t KS, size_t BS>
void read_known_keys_smc_data(io_connect_t conn, const std::array<known_key<M>, KS>& keys,
                              const std::bitset<BS>& valid_keys, std::vector<M>* p_out_metrics) {
  for (size_t i = 0; i < keys.size() && i < BS; ++i) {
    const auto& key = keys[i];
    if (valid_keys.test(i)) {
      M metric(key.empty_metric);  // New metric
      switch (key.data_type) {
      case smc_data_type_flt:
        read_smc_data<smc_data_type_flt>(conn, key.key, key.data_size, &metric.value);
        break;

      default:
        // Do nothing
      }
      p_out_metrics->emplace_back(std::move(metric));
    }
  }
}

// ------ smc_reader implementation

smc_reader::~smc_reader() {
  if (conn_) {
    IOServiceClose(conn_);
    conn_ = 0;
  }
}

auto smc_reader::init() -> std::error_code {
  if (conn_) {
    return std::error_code();
  }
  // Get AppleSMC service
  auto ref = IOServiceMatching("AppleSMC");
  if (!ref) {
    return std::make_error_code(std::errc::no_such_device);
  }
  kern_return_t k_rtn;  // NOTE: use mach_error_string to get error message from k_rtn
  io_iterator_t iterator;
  k_rtn = IOServiceGetMatchingServices(kIOMainPortDefault, ref, &iterator);
  if (k_rtn != KERN_SUCCESS) {
    return std::make_error_code(std::errc::no_such_device);
  }
  auto service = IOIteratorNext(iterator);
  IOObjectRelease(iterator);
  if (!service) {
    return std::make_error_code(std::errc::no_such_device);
  }
  // Connect to AppleSMC service
  k_rtn = IOServiceOpen(service, mach_task_self(), 0, &conn_);
  IOObjectRelease(service);
  if (k_rtn != KERN_SUCCESS) {
    return std::make_error_code(std::errc::connection_refused);
  }

  // Cache key result
  std::unordered_map<uint32_t, std::tuple<uint32_t, uint32_t>> cache_keys;

  auto check = [this, &cache_keys](auto&& keys, auto&& bitset) {
    // Check keys
    for (size_t i = 0; i < keys.size(); ++i) {
      const auto& key = keys[i];
      uint32_t data_size = 0, data_type = 0;
      // Get key info from smc or cache
      const auto it = cache_keys.find(key.key);
      if (it != cache_keys.end()) {
        data_size = std::get<0>(it->second);
        data_type = std::get<1>(it->second);
      } else {
        if (read_smc_key_info(conn_, key.key, &data_size, &data_type)) {
          continue;
        }
        // Write cache
        cache_keys[key.key] = std::make_tuple(data_size, data_type);
      }
      if (data_size == key.data_size && data_type == key.data_type) {
        bitset.set(i);
      }
    }
  };

  // Cpu core & gpu core & memory temperature keys
  if (sys_info_.cpu_brand() == cpu_brand::m1) {
    check(known_m1_cpu_core_temperature_keys, valid_cpu_core_temperature_keys_);
    check(known_m1_gpu_core_temperature_keys, valid_gpu_core_temperature_keys_);
    check(known_m1_dram_temperature_keys, valid_dram_temperature_keys_);
  } else if (sys_info_.cpu_brand() == cpu_brand::m2) {
    check(known_m2_cpu_core_temperature_keys, valid_cpu_core_temperature_keys_);
    check(known_m2_gpu_core_temperature_keys, valid_gpu_core_temperature_keys_);
    check(known_m2_dram_temperature_keys, valid_dram_temperature_keys_);
  } else if (sys_info_.cpu_brand() == cpu_brand::m3) {
    check(known_m3_cpu_core_temperature_keys, valid_cpu_core_temperature_keys_);
    check(known_m3_gpu_core_temperature_keys, valid_gpu_core_temperature_keys_);
    check(known_m3_dram_temperature_keys, valid_dram_temperature_keys_);
  } else if (sys_info_.cpu_brand() == cpu_brand::m4) {
    check(known_m4_cpu_core_temperature_keys, valid_cpu_core_temperature_keys_);
    check(known_m4_gpu_core_temperature_keys, valid_gpu_core_temperature_keys_);
    check(known_m4_dram_temperature_keys, valid_dram_temperature_keys_);
  } else if (sys_info_.cpu_brand() == cpu_brand::m5) {
    check(known_m5_cpu_core_temperature_keys, valid_cpu_core_temperature_keys_);
    check(known_m5_gpu_core_temperature_keys, valid_gpu_core_temperature_keys_);
    check(known_m5_dram_temperature_keys, valid_dram_temperature_keys_);
  }

  // Storage temperature keys
  check(known_storage_temperature_keys, valid_storage_temperature_keys_);
  // Battery temperature keys
  check(known_battery_temperature_keys, valid_battery_temperature_keys_);
  // Fan speed keys
  check(known_fan_speed_keys, valid_fan_speed_keys_);

  return std::error_code();
}

auto smc_reader::sample() -> smc_sample {
  smc_sample sample;

  if (sys_info_.cpu_brand() == cpu_brand::m1) {
    read_known_keys_smc_data(conn_, known_m1_cpu_core_temperature_keys, valid_cpu_core_temperature_keys_,
                             &sample.temperatures);
    read_known_keys_smc_data(conn_, known_m1_gpu_core_temperature_keys, valid_gpu_core_temperature_keys_,
                             &sample.temperatures);
    read_known_keys_smc_data(conn_, known_m1_dram_temperature_keys, valid_dram_temperature_keys_, &sample.temperatures);
  } else if (sys_info_.cpu_brand() == cpu_brand::m2) {
    read_known_keys_smc_data(conn_, known_m2_cpu_core_temperature_keys, valid_cpu_core_temperature_keys_,
                             &sample.temperatures);
    read_known_keys_smc_data(conn_, known_m2_gpu_core_temperature_keys, valid_gpu_core_temperature_keys_,
                             &sample.temperatures);
    read_known_keys_smc_data(conn_, known_m2_dram_temperature_keys, valid_dram_temperature_keys_, &sample.temperatures);
  } else if (sys_info_.cpu_brand() == cpu_brand::m3) {
    read_known_keys_smc_data(conn_, known_m3_cpu_core_temperature_keys, valid_cpu_core_temperature_keys_,
                             &sample.temperatures);
    read_known_keys_smc_data(conn_, known_m3_gpu_core_temperature_keys, valid_gpu_core_temperature_keys_,
                             &sample.temperatures);
    read_known_keys_smc_data(conn_, known_m3_dram_temperature_keys, valid_dram_temperature_keys_, &sample.temperatures);
  } else if (sys_info_.cpu_brand() == cpu_brand::m4) {
    read_known_keys_smc_data(conn_, known_m4_cpu_core_temperature_keys, valid_cpu_core_temperature_keys_,
                             &sample.temperatures);
    read_known_keys_smc_data(conn_, known_m4_gpu_core_temperature_keys, valid_gpu_core_temperature_keys_,
                             &sample.temperatures);
    read_known_keys_smc_data(conn_, known_m4_dram_temperature_keys, valid_dram_temperature_keys_, &sample.temperatures);
  } else if (sys_info_.cpu_brand() == cpu_brand::m5) {
    read_known_keys_smc_data(conn_, known_m5_cpu_core_temperature_keys, valid_cpu_core_temperature_keys_,
                             &sample.temperatures);
    read_known_keys_smc_data(conn_, known_m5_gpu_core_temperature_keys, valid_gpu_core_temperature_keys_,
                             &sample.temperatures);
    read_known_keys_smc_data(conn_, known_m5_dram_temperature_keys, valid_dram_temperature_keys_, &sample.temperatures);
  }

  read_known_keys_smc_data(conn_, known_storage_temperature_keys, valid_storage_temperature_keys_,
                           &sample.temperatures);
  read_known_keys_smc_data(conn_, known_battery_temperature_keys, valid_battery_temperature_keys_,
                           &sample.temperatures);
  read_known_keys_smc_data(conn_, known_fan_speed_keys, valid_fan_speed_keys_, &sample.fan_speeds);

  return sample;
}

#endif
