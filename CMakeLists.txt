#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)

# The FLPR image in remote/ only builds through sysbuild.cmake. An app-only build still links,
# then boots whatever is in the FLPR partition.
if(NOT SYSBUILD)
  message(FATAL_ERROR "flpr_spwm needs sysbuild: west build --sysbuild ...")
endif()

# Reserves FLPR code/data SRAM and makes the app core boot the FLPR image.
list(APPEND SNIPPET nordic-flpr)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(flpr_spwm)

target_sources(app PRIVATE src/main.c src/pattern.c src/spwm.c)
target_include_directories(app PRIVATE include)
