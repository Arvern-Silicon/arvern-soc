/*----------------------------------------------------------------------------
 *          _    _           Family:    aRVern System
 *         / \__/ \          File:      tang_blns.h
 *        /   /\   \         --------------------------------------------
 *    ===/   /=========      Copyright: (c) 2026, aRVern-dev
 *      /   / RV \   \       Contact:   arvernsilicon@gmail.com
 *     /___/______\___\      GitHub:    https://github.com/Arvern-Silicon
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Full license text is available in the LICENSE file at the repository root.
 *----------------------------------------------------------------------------
 * Belenos platform header of the Tang Console 138K (arvern-belenos
 * sw/include/belenos_hw.h): the features of the Belenos instance in
 * rtl/verilog/arvern_fpga.v, for the driver to build only what is there.
 * Named by each app Makefile (BLNS_PLATFORM: -DBLNS_PLATFORM_HEADER). blns_init()
 * returns BLNS_ERR_CONFIG if the bitstream differs; arvern-belenos
 * sim/rtl_sim/bin/check_deployed.py compares this file with the board top.
 *
 * One firmware runs on the RGB-panel and the HDMI bitstreams: the DPI driver
 * is in both (DIF_TYPE 4 or 5), the HDMI options (DDC, data islands, audio)
 * only in the HDMI one, so they stay detected at run time.
 *----------------------------------------------------------------------------*/
#ifndef TANG_BLNS_H
#define TANG_BLNS_H

#define BLNS_HW_GPU         1
#define BLNS_HW_GPU_XY      1
#define BLNS_HW_GPU_GRAD    1
#define BLNS_HW_GPU_BLEND   0
#define BLNS_HW_GPU_TRI     0
#define BLNS_HW_GPU_REMAP   1
#define BLNS_HW_GPU_MPLANE  1
#define BLNS_HW_GPU_IMM     1
#define BLNS_HW_GPU_LINE    1
#define BLNS_HW_LUT         1
#define BLNS_HW_LUT2        1
#define BLNS_HW_CURSOR      1
#define BLNS_HW_DPI         1
#define BLNS_HW_DBI         0
/* BLNS_HW_DPI_DDC, BLNS_HW_DPI_ISLAND, BLNS_HW_DPI_AUDIO: run time (panel or HDMI) */

#endif
