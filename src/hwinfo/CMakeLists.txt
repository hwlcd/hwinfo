# OSX dependencies
find_library(FW_IOKit IOKit REQUIRED)
find_library(FW_CoreFoundation CoreFoundation REQUIRED)
find_library(IOReport IOReport REQUIRED)

# Boost
find_package(Boost 1.90.0 REQUIRED)

set(SourceFiles
    hwinfo.hpp
    hwinfo_osx.hpp
    hwinfo_osx.cpp
    metric.hpp
    detail/osx_cpu_load_reader.hpp
    detail/osx_cpu_load_reader.cpp
    detail/osx_disk_io_reader.hpp
    detail/osx_disk_io_reader.cpp
    detail/osx_io_report.hpp
    detail/osx_io_report.cpp
    detail/osx_mem_info_reader.hpp
    detail/osx_mem_info_reader.cpp
    detail/osx_net_io_reader.hpp
    detail/osx_net_io_reader.cpp
    detail/osx_smc_reader.hpp
    detail/osx_smc_reader.cpp
    detail/osx_sys_info.hpp
    detail/osx_sys_info.cpp
    detail/osx_util.hpp)

# Target: hwinfo
add_library(hwinfo STATIC ${SourceFiles})
target_include_directories(hwinfo PUBLIC ..)
target_link_libraries(hwinfo PRIVATE Boost::boost ${FW_IOKit} ${FW_CoreFoundation} ${IOReport})

if(MSVC)
  target_compile_options(hwinfo PRIVATE /W4 /WX)
else()
  target_compile_options(hwinfo PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
