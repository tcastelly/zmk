# Copyright (c) 2022 The ZMK Contributors
# SPDX-License-Identifier: MIT

zephyr_include_directories(.)

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_CW2015 cw2015.c)
zephyr_library_sources_ifndef(CONFIG_CW2015 ${ZEPHYR_BASE}/misc/empty_file.c)
