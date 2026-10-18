cmake_minimum_required(VERSION 3.21)
project(pixwall LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive-)
  # 常駐側は C ランタイムを静的に入れて、配るときの DLL を減らす
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# OS に触らない計算。常駐側・設定画面・テストから使う
add_library(pixwall-core STATIC core/core.cpp)
target_include_directories(pixwall-core PUBLIC core)

# 常駐側。WinUI 3・.NET・WebView は入れない
add_executable(pixwall WIN32
  src/main.cpp
  src/desktop.cpp
  src/image_loader.cpp
  src/wall_window.cpp
  src/pixwall.manifest)
target_compile_definitions(pixwall PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00)
target_link_libraries(pixwall PRIVATE pixwall-core d3d11 dcomp windowscodecs wtsapi32 ole32 user32 shell32 uuid)

enable_testing()
add_executable(pixwall-core-tests tests/core_tests.cpp)
target_link_libraries(pixwall-core-tests PRIVATE pixwall-core)
add_test(NAME core COMMAND pixwall-core-tests)
