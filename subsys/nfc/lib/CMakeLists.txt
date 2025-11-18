#
# Copyright (c) 2019 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
zephyr_library()
if(CONFIG_BM_NFC_PLATFORM)
  zephyr_library_sources(nfc_bm_platform.c)
  zephyr_library_sources(nfc_bm_platform_internal.c)
endif()
