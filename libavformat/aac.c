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

