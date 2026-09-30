# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: Apache-2.0

if("${SB_CONFIG_REMOTE_BOARD}" STREQUAL "")
  message(FATAL_ERROR "${BOARD}/${BOARD_QUALIFIERS} is not supported, "
    "no FLPR board target set in Kconfig.sysbuild")
endif()

ExternalZephyrProject_Add(
  APPLICATION remote
  SOURCE_DIR  ${APP_DIR}/remote
  BOARD       ${SB_CONFIG_REMOTE_BOARD}
  BOARD_REVISION ${BOARD_REVISION}
)
