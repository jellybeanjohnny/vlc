/*****************************************************************************
 * coreaudio_common.h: Common AudioUnit code for iOS and macOS
 *****************************************************************************
 * Copyright (C) 2005 - 2017 VLC authors and VideoLAN
 *
 * Authors: Derk-Jan Hartman <hartman at videolan dot org>
 *          Felix Paul Kühne <fkuehne at videolan dot org>
 *          David Fuhrmann <david dot fuhrmann at googlemail dot com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# import "config.h"
#endif

#import <vlc_common.h>
#import <vlc_atomic.h>
#import <vlc_aout.h>

#import "TPCircularBuffer.h"

#define STREAM_FORMAT_MSG(pre, sfm) \
    pre "[%f][%4.4s][%u][%u][%u][%u][%u][%u]", \
    sfm.mSampleRate, (char *)&sfm.mFormatID, \
    (unsigned int)sfm.mFormatFlags, (unsigned int)sfm.mBytesPerPacket, \
    (unsigned int)sfm.mFramesPerPacket, (unsigned int)sfm.mBytesPerFrame, \
    (unsigned int)sfm.mChannelsPerFrame, (unsigned int)sfm.mBitsPerChannel

struct aout_sys_common
{
    /* The following is owned by common.c (initialized from ca_Init, cleaned
     * from ca_Clean) */

    /* circular buffer to swap the audio data */
    TPCircularBuffer    circular_buffer;
    atomic_uint         i_underrun_size;
    int                 i_rate;
    unsigned int        i_bytes_per_frame;
    unsigned int        i_frame_length;

    /* The following need to set by the caller */

    uint8_t             chans_to_reorder;
    uint8_t             chan_table[AOUT_CHAN_MAX];
    /* The time the device needs to process the data. In samples. */
    uint32_t            i_device_latency;
};

void ca_Render(audio_output_t *p_aout, uint8_t *p_output, size_t i_requested);

int  ca_TimeGet(audio_output_t *p_aout, mtime_t *delay);

void ca_Flush(audio_output_t *p_aout, bool wait);

void ca_Play(audio_output_t * p_aout, block_t * p_block);

int  ca_Init(audio_output_t *p_aout, const audio_sample_format_t *fmt,
             size_t i_audio_buffer_size);

void ca_Clean(audio_output_t *p_aout);
