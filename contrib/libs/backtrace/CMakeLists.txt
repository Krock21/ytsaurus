# This file was generated by the build system used internally in Yandex and called "ya"
# (https://github.com/yandex/yatool).
#
# Configuration files of ya build system are usually named ya.make. If ya.make file is present
# at the root of the repository, then this repository supports ya build.
#
# If the repository supports both CMake and ya build configurations, please modify both of them.
#
# If only CMake build configuration is supported then modify only CMake files and note that only
# simple modifications are allowed like adding source-files to targets or adding simple properties
# like target_include_directories. These modifications will be ported to original ya.make files
# by maintainers. Any complex modifications which can't be easily ported back to the ya build
# system may be rejected.

find_package(linux-headers-generic REQUIRED)

add_library(contrib-libs-backtrace)

target_compile_options(contrib-libs-backtrace PRIVATE
  -DHAVE_CONFIG_H
  $<IF:$<CXX_COMPILER_ID:MSVC>,,-Wno-everything>
)

target_include_directories(contrib-libs-backtrace PRIVATE
  ${PROJECT_SOURCE_DIR}/contrib/libs/backtrace
)

target_link_libraries(contrib-libs-backtrace PUBLIC
  linux-headers-generic::linux-headers-generic
)

target_sources(contrib-libs-backtrace PRIVATE
  ${PROJECT_SOURCE_DIR}/contrib/libs/backtrace/atomic.c
  ${PROJECT_SOURCE_DIR}/contrib/libs/backtrace/backtrace.c
  ${PROJECT_SOURCE_DIR}/contrib/libs/backtrace/dwarf.c
  ${PROJECT_SOURCE_DIR}/contrib/libs/backtrace/fileline.c
  ${PROJECT_SOURCE_DIR}/contrib/libs/backtrace/mmap.c
  ${PROJECT_SOURCE_DIR}/contrib/libs/backtrace/mmapio.c
  ${PROJECT_SOURCE_DIR}/contrib/libs/backtrace/posix.c
  ${PROJECT_SOURCE_DIR}/contrib/libs/backtrace/print.c
  ${PROJECT_SOURCE_DIR}/contrib/libs/backtrace/simple.c
  ${PROJECT_SOURCE_DIR}/contrib/libs/backtrace/sort.c
  ${PROJECT_SOURCE_DIR}/contrib/libs/backtrace/state.c
  ${PROJECT_SOURCE_DIR}/contrib/libs/backtrace/elf.c
)

