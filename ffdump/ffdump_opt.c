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
//  ffdump_opt.c
//  ffdump
//
//  Created by Vadim Kalinsky on 2014-06-26.
//
//

#include <stdio.h>

#include "ffdump_opt.h"

const char program_name[] = "ffdump";
const int program_birth_year = 2010;

int s_read_frames        = 0;
int s_dump_frames        = 0;
int s_need_decode        = 0;
int s_show_deltas        = 0;
int s_show_rawtimestamps = 0;
int s_real_time          = 0;
int s_compact            = 0;
const char* s_input_file_name = NULL;
const char* s_forced_input_format = NULL;

int s_measure_lipsync_track_a = -1;
int s_measure_lipsync_track_b = -1;

rect_t crop;

int nb_compare_pict = 0;
char** compare_pict_filenames = NULL;
int s_comparator_learning_mode = 0;

static int parse_ls_tracks( void *optctx, const char *opt, const char* arg )
{
    if( arg != NULL )
    {
        const char* track_a = strtok((char*)arg, ",");
        const char* track_b = NULL;
		
        if( track_a != NULL )
            track_b = strtok(NULL, ",");
		
        if( track_a && track_b )
        {
            s_measure_lipsync_track_a = atoi(track_a);
            s_measure_lipsync_track_b = atoi(track_b);
        }
		
        if( s_measure_lipsync_track_a == 0 || s_measure_lipsync_track_b == 0 )
        {
            fprintf(stderr, "Bad value for --ls-tracks parameter.\n");
            exit(1);
        }
		
        // Convert from 1-based to zero-based.
        s_measure_lipsync_track_a--;
        s_measure_lipsync_track_b--;
    }
    
    return 0;
}

static int parse_cropping( void *optctx, const char *opt, const char* arg )
{
    if( arg == NULL || 4 != sscanf((const char*)arg, "%dx%d/%dx%d", &crop.origin.x, &crop.origin.y, &crop.size.width, &crop.size.height) )
    {
        crop = MAKE_RECT(-1,-1,-1,-1);
        fprintf(stderr, "Bad value for --crop.\n");
        exit(1);
    }
    
    return 0;
}

static void opt_input_file(void *optctx, const char *arg)
{
    s_input_file_name = arg;
}

static int add_compare_pict( void* optctx, const char* opt, const char* arg )
{
    nb_compare_pict++;
    compare_pict_filenames = realloc(compare_pict_filenames, nb_compare_pict*sizeof(*compare_pict_filenames));
    
    compare_pict_filenames[nb_compare_pict-1] = av_strdup(arg);
    
    return 0;
}

const OptionDef options[] = {
    { "h"             , OPT_EXIT, { .func_arg = show_help }, "show help" },
    { "c"             , OPT_BOOL, {  &s_dump_frames       }, "dump file content" },
    { "d"             , OPT_BOOL, {  &s_need_decode       }, "decode incoming packets" },
    { "r"             , OPT_BOOL, {  &s_read_frames       }, "read whole frames instead of packets" },
    { "-deltas"       , OPT_BOOL, {  &s_show_deltas       }, "show deltas between timestamps" },
    { "-rawts"        , OPT_BOOL, {  &s_show_rawtimestamps}, "show raw timestamps" },
    { "-real-time"    , OPT_BOOL, {  &s_real_time         }, "show absolute timestamps" },
    { "-compact"      , OPT_BOOL, {  &s_compact           }, "don't show info about all packets" },
    { "-force-format" , OPT_STRING|HAS_ARG, { &s_forced_input_format }, "force input format" },
    { "-ls-tracks"    , HAS_ARG , { .func_arg = parse_ls_tracks }, "1-based indexes of tracks used for lipsync detection, separated by comma: \"--ls-tracks 1,2\"" },
    { "-crop"         , HAS_ARG , { .func_arg = parse_cropping }, "cropping for picture analyzer, in percents: \"--crop 10x10/25x25\"" },
    { "-compare-with" , HAS_ARG , { .func_arg = add_compare_pict }, "compare each frame with picture from file (implies -c -r -d)" },
    { "-comparator-learning-mode", OPT_BOOL , { &s_comparator_learning_mode }, "dump unique frames from stream (up to 30)" }, // MAX_COMPARE_PICTURES=30
    { NULL }
};

void show_help_default(const char *opt, const char *arg)
{
    av_log_set_callback(log_callback_help);
    show_help_options(options, "options:", 0, 0, 0);
    printf("\n");
}

void ffdump_parse_options(int argc, char* argv[])
{
    parse_options(NULL, argc, argv, options, opt_input_file);
}
