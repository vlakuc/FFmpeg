//
//  ffdump_opt.h
//  ffdump
//
//  Created by Vadim Kalinsky on 2014-06-26.
//
//

#ifndef ffdump_ffdump_opt_h
#define ffdump_ffdump_opt_h

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
