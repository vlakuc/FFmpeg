#ifndef CONTENT_SYNC_DETECTOR_H
#define CONTENT_SYNC_DETECTOR_H

typedef enum { CONTENT_UNDEFINED=0, CONTENT_BLACK=1, CONTENT_WHITE=2 } content_state_t;

typedef struct {
    content_state_t state;
    int64_t         state_switch_time;
    unsigned int    threshold;
    unsigned int    last_value;
} content_track_t;

typedef struct {
    int nb_tracks;
    content_track_t* tracks;
} content_sync_detector_ctx_t;

void content_sync_detector_init(content_sync_detector_ctx_t* ctx, int threshold);
content_sync_detector_ctx_t* content_sync_detector_create(unsigned int nb_tracks, int threshold);
void content_sync_detector_destroy( content_sync_detector_ctx_t* ctx );

void content_sync_write( content_sync_detector_ctx_t* ctx, unsigned int track_idx, int64_t time, unsigned int value );
float content_sync_get_diff( content_sync_detector_ctx_t* ctx, unsigned int track_idx_a, unsigned int track_idx_b );

#endif /* CONTENT_SYNC_DETECTOR_H */
