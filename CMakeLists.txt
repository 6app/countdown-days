cmake_minimum_required(VERSION 3.16)
project(CountDay LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

add_executable(CountDay WIN32
  src/main.cpp
  src/tray.cpp
  src/desktop_host.cpp
  src/card.cpp
  src/renderer.cpp
  src/config.cpp
  src/edit_dialog.cpp
  src/daycalc.cpp
  res/app.rc
)

target_compile_definitions(CountDay PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000003)

target_link_libraries(CountDay PRIVATE
  d2d1 dwrite comctl32 shcore shell32 user32 gdi32 advapi32 ole32 wtsapi32 uxtheme)

if(MSVC)
  # 源码是 UTF-8；静态链接 CRT，单个 exe 即可运行
  target_compile_options(CountDay PRIVATE /utf-8 /W4 /permissive- /GR- /EHsc)
  set_property(TARGET CountDay PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  # 清单由 app.rc 嵌入，避免链接器再生成一份
  target_link_options(CountDay PRIVATE /MANIFEST:NO)
else()
  target_compile_options(CountDay PRIVATE -Wall -Wextra -fno-rtti)
  target_link_options(CountDay PRIVATE -municode -static -s)
endif()
