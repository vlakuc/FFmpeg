//
//  compare_pictures.h
//  ffdump
//
//  Created by Vadim Kalinsky on 2014-10-08.
//
//

#ifndef __ffdump__compare_pictures__
#define __ffdump__compare_pictures__

#include "ffdump.h"

typedef struct compare_pict_ctx_t compare_pict_ctx_t;

compare_pict_ctx_t* cpc_alloc(void);
void cpc_free( compare_pict_ctx_t* ctx );

void cpc_set_learn_mode( compare_pict_ctx_t* ctx, int enabled );

int cpc_add_file( compare_pict_ctx_t* ctx, const char* filename );

int cpc_find( compare_pict_ctx_t* ctx, AVFrame* f, rect_t crop );

#endif /* defined(__ffdump__compare_pictures__) */
