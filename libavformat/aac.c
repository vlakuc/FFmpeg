/*
 * Copyright (c) 2017 Epiphan Systems Inc. All rights reserved.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "aac.h"

#ifndef AAC_ADTS_HEADER_SIZE
#define AAC_ADTS_HEADER_SIZE 7
#endif

/* Get size of AAC ADTS header. */

int ff_aac_get_adts_header_size(const uint8_t* buf, int size)
{
    if ( size < AAC_ADTS_HEADER_SIZE )
        return 0;
    /* Check for syncword */
    if ( (buf[0] != 0xFF) || (buf[1] & 0x0F != 0x0F) )
        return 0;
    /* Check for protection */
    if ( (buf[1] & 0x80) == 0 )
        return AAC_ADTS_HEADER_SIZE + 2; // + CRC
    return AAC_ADTS_HEADER_SIZE;
}

