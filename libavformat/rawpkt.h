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

#ifndef AVFORMAT_RAWPKT_H
#define AVFORMAT_RAWPKT_H
#define MKTAG64(a,b,c,d,e,f,g,h) ((int64_t)a | ((int64_t)b << 8) | ((int64_t)c << 16) | ((int64_t)d << 24) | ((int64_t)e << 32) | ((int64_t)f << 40) | ((int64_t)g << 48) | ((int64_t)h << 56))

#define RAWHEADER_MAGIC MKTAG64('R','A','W',' ','H','E','A','D')

#define RAWPACKET_MAGIC MKTAG64('R','A','W',' ','P','A','C','K')

#define RAWHEADER_METADATA MKTAG64('M','E','T','A','D','A','T','A')
#define RAWHEADER_STREAM MKTAG64('S','T','R','E','A','M',' ',' ')
#define RAWHEADER_TRACK MKTAG64('T','R','A','C','K',' ',' ',' ')
#define RAWHEADER_CODECCTX MKTAG64('C','O','D','E','C','C','T','X')


#define RAWHEADER_MIN_SIZE 16
#define RAWTAG_PADDING_SIZE 4 
#define RAWTAG_SEEK(pb) { while ((avio_tell(pb) % RAWTAG_PADDING_SIZE) != 0) avio_w8(pb,0); }
#define RAWTAG_SKIP(pb,to) { while (avio_tell(pb) < to) avio_r8(pb); }

#define RAWPACKET_VERSION 2

// Direct access to rawpacket stream header and rawpacket packet header
int ff_rawpacket_read_header(AVIOContext *pb, AVFormatContext** s);			
int ff_rawpacket_read_packet_header(AVIOContext *pb, AVPacket* pkt, int alloc);

#endif
