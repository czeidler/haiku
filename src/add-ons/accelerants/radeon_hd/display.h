/*
 * Copyright 2006-2011, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *      Alexander von Gluck, kallisti5@unixzen.com
 */
#ifndef RADEON_HD_DISPLAY_H
#define RADEON_HD_DISPLAY_H


#include <video_configuration.h>

#include "accelerant.h"
#include "pll.h"


status_t init_registers(register_info* reg, uint8 crtid);
status_t detect_crt_ranges(uint32 crtid);
status_t detect_displays();
void debug_displays();

uint32 display_get_encoder_mode(uint32 connectorIndex);
void display_crtc_ss(pll_info* pll, int command);
void display_crtc_lock(uint8 crtcID, int command);
void display_crtc_blank(uint8 crtcID, int command);
void display_crtc_dpms(uint8 crtcID, int mode);
void display_crtc_scale(uint8 crtcID, display_mode* mode);
void display_crtc_fb_set(uint8 crtcID, display_mode* mode);
void display_crtc_set(uint8 crtcID, display_mode* mode);
void display_crtc_set_dtd(uint8 crtcID, display_mode* mode);
void display_crtc_power(uint8 crtcID, int command);
void display_crtc_memreq(uint8 crtcID, int command);
void display_avivo_crtc_load_lut(uint8 crtcID);
void display_dce45_crtc_load_lut(uint8 crtcID);


#endif /* RADEON_HD_DISPLAY_H */
