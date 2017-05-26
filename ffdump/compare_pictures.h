/*
 * Copyright (c) 2014 - 2017 Epiphan Systems Inc. All rights reserved.
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

//
//  compare_pictures.h
//  ffdump
//
//  Created by Vadim Kalinsky on 2014-10-08.
//
//

#ifndef FFDUMP_COMPARE_PICTURES_H
#define FFDUMP_COMPARE_PICTURES_H

#include "ffdump.h"

typedef struct compare_pict_ctx_t compare_pict_ctx_t;

compare_pict_ctx_t* cpc_alloc(void);
void cpc_free( compare_pict_ctx_t* ctx );

void cpc_set_learn_mode( compare_pict_ctx_t* ctx, int enabled );

int cpc_add_file( compare_pict_ctx_t* ctx, const char* filename );

int cpc_find( compare_pict_ctx_t* ctx, AVFrame* f, rect_t crop );

#endif
