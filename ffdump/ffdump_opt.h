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
//  ffdump_opt.h
//  ffdump
//
//  Created by Vadim Kalinsky on 2014-06-26.
//
//

#ifndef FFDUMP_FFDUMP_OPT_H
#define FFDUMP_FFDUMP_OPT_H

#include "../cmdutils.h"
#include "ffdump.h"

extern int s_need_decode;
extern int s_read_frames;
extern int s_dump_frames;
extern int s_show_deltas;
extern int s_show_rawtimestamps;
extern int s_real_time;
extern int s_compact;
extern const char* s_input_file_name;
extern const char* s_forced_input_format;

extern int s_measure_lipsync_track_a;
extern int s_measure_lipsync_track_b;

extern rect_t crop;

void ffdump_parse_options(int argc, char* argv[]);
void show_help_default(const char *opt, const char *arg);

extern int nb_compare_pict;
extern char** compare_pict_filenames;
extern int s_comparator_learning_mode;

#endif
