cmake_minimum_required(VERSION 3.16)
project(cms_portal VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Password hashing runs 600,000 rounds, which is slow without optimizations.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(cms_core
  src/crypto.cpp
  src/storage.cpp
  src/portal.cpp
)
target_include_directories(cms_core PUBLIC include)

if(MSVC)
  target_compile_options(cms_core PUBLIC /W4 /utf-8 /permissive-)
  target_compile_definitions(cms_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(cms_core PUBLIC -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
  target_link_libraries(cms_core PUBLIC bcrypt)
endif()

add_executable(cms src/main.cpp src/app.cpp src/console.cpp)
target_link_libraries(cms PRIVATE cms_core)

option(CMS_BUILD_TESTS "Build the tests" ON)
if(CMS_BUILD_TESTS)
  enable_testing()
  add_executable(cms_tests
    tests/main.cpp
    tests/test_crypto.cpp
    tests/test_storage.cpp
    tests/test_portal.cpp
  )
  target_link_libraries(cms_tests PRIVATE cms_core)
  add_test(NAME cms_tests COMMAND cms_tests)
endif()
