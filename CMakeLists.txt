cmake_minimum_required(VERSION 3.16)
project(state_channel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_library(state_channel INTERFACE)
target_include_directories(state_channel INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Fast maths licences the compiler to assume the values it is reasoning about
# are ordinary numbers, which is the one thing this code does not promise: the
# guards exist because the inputs include denormals, infinities and NaN.
#
# The default is strict. The option is here so that the claim "the guards do not
# depend on strict semantics, because they classify the integer bits and never
# the float" can be checked rather than asserted:
#
#   cmake -S . -B build-fast -DSTATE_CHANNEL_FAST_MATH=ON
option(STATE_CHANNEL_FAST_MATH
       "Build with fast maths, to check the guards do not rely on strict IEEE semantics" OFF)

if(MSVC)
  if(STATE_CHANNEL_FAST_MATH)
    target_compile_options(state_channel INTERFACE /W4 /fp:fast)
  else()
    target_compile_options(state_channel INTERFACE /W4 /fp:strict)
  endif()
else()
  if(STATE_CHANNEL_FAST_MATH)
    target_compile_options(state_channel INTERFACE
      -Wall -Wextra -Wpedantic -ffast-math)
  else()
    target_compile_options(state_channel INTERFACE
      -Wall -Wextra -Wpedantic -ffp-contract=off)
  endif()
endif()

enable_testing()

function(add_channel_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE state_channel)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# The twelve, in the order they appear in the README.
add_channel_test(test_round_trip tests/test_round_trip.cpp)                     # 1
add_channel_test(test_flag_discrimination tests/test_flag_discrimination.cpp)   # 2
add_channel_test(test_carrier_fidelity tests/test_carrier_fidelity.cpp)         # 3
add_channel_test(test_guards_non_finite tests/test_guards_non_finite.cpp)       # 4
add_channel_test(test_guards_denormal_zero tests/test_guards_denormal_zero.cpp) # 5
add_channel_test(test_guards_sign tests/test_guards_sign.cpp)                   # 6
add_channel_test(test_graceful_degradation tests/test_graceful_degradation.cpp) # 7
add_channel_test(test_idempotence tests/test_idempotence.cpp)                   # 8
add_channel_test(test_payload_boundaries tests/test_payload_boundaries.cpp)     # 9
add_channel_test(test_type_punning tests/test_type_punning.cpp)                 # 10
add_channel_test(test_fuzz tests/test_fuzz.cpp)                                 # 11
add_channel_test(bench_channel tests/bench_channel.cpp)                         # 12
add_channel_test(test_guarded_channel tests/test_guarded_channel.cpp)           # 13

# Generates the data behind the figures in the README. Not a test: it writes
# into docs/data and is run by hand when a figure needs regenerating.
add_executable(figures tools/figures.cpp)
target_link_libraries(figures PRIVATE state_channel)
if(MSVC)
  target_compile_definitions(figures PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
