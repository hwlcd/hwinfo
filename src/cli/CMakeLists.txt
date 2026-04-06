# Boost
find_package(Boost 1.90.0 REQUIRED COMPONENTS program_options json)
# Curses
find_package(Curses REQUIRED)

# Target: hwinfo-cli
add_executable(hwinfo-cli main.cpp)
target_link_libraries(hwinfo-cli PRIVATE hwinfo Boost::program_options Boost::json Curses)

set_property(TARGET hwinfo-cli PROPERTY OUTPUT_NAME hwinfo)

if(MSVC)
  target_compile_options(hwinfo-cli PRIVATE /W4 /WX)
else()
  target_compile_options(hwinfo-cli PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
