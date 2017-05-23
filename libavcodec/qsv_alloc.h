#include "avcodec.h"
#include "qsv_internal.h"

#include "mfx/mfxvideo.h"

// =================================================================
// Intel Media SDK memory allocator entrypoints....
// Implementation of this functions is OS/Memory type specific.
mfxStatus set_display(QSVSession *qs);
mfxStatus simple_alloc(mfxHDL pthis, mfxFrameAllocRequest* request, mfxFrameAllocResponse* response);
mfxStatus simple_lock(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr);
mfxStatus simple_unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData* ptr);
mfxStatus simple_gethdl(mfxHDL pthis, mfxMemId mid, mfxHDL* handle);
mfxStatus simple_free(mfxHDL pthis, mfxFrameAllocResponse* response);


