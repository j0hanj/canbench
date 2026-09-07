include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.4
)
FetchContent_MakeAvailable(Catch2)

add_executable(canbench_tests
  unit/smoke_test.cpp
  unit/frame_test.cpp
  unit/log_test.cpp
  unit/dbc_test.cpp
  unit/wave_test.cpp
)
target_link_libraries(canbench_tests PRIVATE canbench_core Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(canbench_tests)
