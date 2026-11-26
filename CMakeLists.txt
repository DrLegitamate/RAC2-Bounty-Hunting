# Bounty Hunter: an OpenRAC native mod for Ratchet & Clank: Going Commando (README.md), from the
# SDK's template (sdk/template/CMakeLists.txt).
#
#   python3 tools/fetch_sdk.py                     # the SDK sdk.lock pins, into .sdk/
#   cmake -S . -B build -G Ninja
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#   python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/bounty_hunter -o dist
cmake_minimum_required(VERSION 3.20)
project(bounty_hunter LANGUAGES C CXX)

if(NOT OpenRACMod_DIR)
  if(DEFINED OPENRAC_SDK)
    set(OpenRACMod_DIR "${OPENRAC_SDK}/cmake")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock" lock)
    string(JSON sdk_version GET "${lock}" version)
    set(OpenRACMod_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.sdk/openrac-mod-sdk-${sdk_version}/cmake")
    if(NOT EXISTS "${OpenRACMod_DIR}/OpenRACModConfig.cmake")
      message(FATAL_ERROR "No OpenRAC mod SDK ${sdk_version}: run python3 tools/fetch_sdk.py first "
                          "(or pass -DOPENRAC_SDK=<an unpacked SDK>)")
    endif()
  endif()
endif()
find_package(OpenRACMod 1.29 REQUIRED)

# The rules: no SDK, no game. The mod and the tests both link them.
add_library(bounty_hunter_rules STATIC src/rules.cpp)
target_include_directories(bounty_hunter_rules PUBLIC src)
set_target_properties(bounty_hunter_rules PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
if(MSVC)
  target_compile_options(bounty_hunter_rules PRIVATE /W4)
else()
  target_compile_options(bounty_hunter_rules PRIVATE -Wall -Wextra)
endif()

openrac_add_mod(bounty_hunter MANIFEST openrac-mod.json SOURCES src/mod.cpp)
target_link_libraries(bounty_hunter PRIVATE bounty_hunter_rules)
set_target_properties(bounty_hunter PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

include(CTest)
if(BUILD_TESTING)
  foreach(name bounty attribution contract highlight)
    add_executable(${name}_test tests/${name}_test.cpp)
    target_link_libraries(${name}_test PRIVATE bounty_hunter_rules)
    target_include_directories(${name}_test PRIVATE tests)
    set_target_properties(${name}_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME ${name} COMMAND ${name}_test)
  endforeach()

  # The built library loaded as the port loads it, against a fake port (tests/load_test.cpp).
  add_executable(load_test tests/load_test.cpp)
  target_link_libraries(load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  set_target_properties(load_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_dependencies(load_test bounty_hunter)
  add_test(NAME load COMMAND load_test $<TARGET_FILE:bounty_hunter>)
endif()
