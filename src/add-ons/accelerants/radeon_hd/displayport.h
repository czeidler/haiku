/*
 * Copyright 2011-2013, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Alexander von Gluck IV, kallisti5@unixzen.com
 *		Bill Randle, billr@neocat.org
 */
#ifndef RADEON_HD_DISPLAYPORT_H
#define RADEON_HD_DISPLAYPORT_H


#include <create_display_modes.h>
#include <stdint.h>
#include <SupportDefs.h>

#include "accelerant.h"
#include "dp.h"


// Radeon HD specific DisplayPort Configuration Data
#define DP_TRAINING_AUX_RD_INTERVAL 0x000e
#define DP_TPS3_SUPPORTED (1 << 6) // Stored within MAX_LANE_COUNT


uint8 dpcd_reg_read(uint32 hwPin, uint16 address);
void dpcd_reg_write(uint32 hwPin, uint16 address, uint8 value);

int dp_aux_write(uint32 hwPin, uint16 address, uint8* send,
	uint8 sendBytes, uint8 delay);
int dp_aux_read(uint32 hwPin, uint16 address, uint8* recv,
	int recvBytes, uint8 delay);
status_t dp_aux_set_i2c_byte(uint32 hwPin, uint16 address,
	uint8* data, bool start, bool stop);
status_t dp_aux_get_i2c_byte(uint32 hwPin, uint16 address,
	uint8* data, bool start, bool stop);

uint32 dp_get_link_rate(uint32 connectorIndex, display_mode* mode);
uint32 dp_get_lane_count(uint32 connectorIndex, display_mode* mode);

void dp_setup_connectors();

status_t dp_link_train(uint8 crtcID);
status_t dp_link_train_cr(uint32 connectorIndex);
status_t dp_link_train_ce(uint32 connectorIndex);

void debug_dp_info();

status_t dp_get_pixel_size_for(color_space space, size_t *pixelChunk,
	size_t *rowAlignment, size_t *pixelsPerChunk);

bool ddc2_dp_read_edid1(uint32 connectorIndex, edid1_info *edid);


#endif /* RADEON_HD_DISPLAYPORT_H */
