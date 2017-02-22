//
//  content_sync_detector.h
//  ffdump
//
//  Created by Vadim Kalinsky on 2013-11-29.
//
//

#ifndef ffdump_content_sync_detector_h
#define ffdump_content_sync_detector_h

#include <sys/types.h>

typedef enum { CONTENT_UNDEFINED=0, CONTENT_BLACK=1, CONTENT_WHITE=2 } content_state_t;

typedef struct {
    content_state_t state;
    int64_t         state_switch_time;
    unsigned int    threshold;
    unsigned int    last_value;
} content_track_t;

typedef struct {
    content_track_t tracks[64];
} content_sync_detector_ctx_t;

void content_sync_detector_init( content_sync_detector_ctx_t* ctx );
void content_sync_detector_destroy( content_sync_detector_ctx_t* ctx );

void content_sync_write( content_sync_detector_ctx_t* ctx, unsigned int track_idx, int64_t time, unsigned int value );
float content_sync_get_diff( content_sync_detector_ctx_t* ctx, unsigned int track_idx_a, unsigned int track_idx_b );

#endif
