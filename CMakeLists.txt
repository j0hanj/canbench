cmake_minimum_required(VERSION 3.20)
project(canbench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

# core lib, grows as i add stuff
add_library(canbench_core STATIC
  src/frame/frame.cpp
)
target_include_directories(canbench_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(canbench src/cli/main.cpp)
target_link_libraries(canbench PRIVATE canbench_core)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()
