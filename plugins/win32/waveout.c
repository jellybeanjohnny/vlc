/*****************************************************************************
 * waveout.c : Windows waveOut plugin for vlc
 *****************************************************************************
 * Copyright (C) 2001 VideoLAN
 * $Id: waveout.c,v 1.10 2002/07/20 18:01:43 sam Exp $
 *
 * Authors: Gildas Bazin <gbazin@netcourrier.com>
 *      
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <errno.h>                                                 /* ENOMEM */
#include <fcntl.h>                                       /* open(), O_WRONLY */
#include <string.h>                                            /* strerror() */

#include <stdlib.h>                            /* calloc(), malloc(), free() */

#include <vlc/vlc.h>
#include <vlc/aout.h>

#include <mmsystem.h>

/*****************************************************************************
 * Local prototypes.
 *****************************************************************************/
static void    aout_getfunctions( function_list_t * p_function_list );

static int     aout_Open        ( aout_thread_t *p_aout );
static int     aout_SetFormat   ( aout_thread_t *p_aout );
static int     aout_GetBufInfo  ( aout_thread_t *p_aout, int i_buffer_info );
static void    aout_Play        ( aout_thread_t *p_aout,
                                  byte_t *buffer, int i_size );
static void    aout_Close       ( aout_thread_t *p_aout );

/* local functions */
static int     OpenWaveOutDevice( aout_thread_t *p_aout );

/*****************************************************************************
 * Building configuration tree
 *****************************************************************************/
MODULE_CONFIG_START
MODULE_CONFIG_STOP

MODULE_INIT_START
    SET_DESCRIPTION( _("Win32 waveOut extension module") )
    ADD_CAPABILITY( AOUT, 250 )
MODULE_INIT_STOP

MODULE_ACTIVATE_START
    aout_getfunctions( &p_module->p_functions->aout );
MODULE_ACTIVATE_STOP

MODULE_DEACTIVATE_START
MODULE_DEACTIVATE_STOP

/*****************************************************************************
 * aout_sys_t: waveOut audio output method descriptor
 *****************************************************************************
 * This structure is part of the audio output thread descriptor.
 * It describes the waveOut specific properties of an audio device.
 *****************************************************************************/

#define NUMBUF 3           /* We use triple buffering to be on the safe side */

struct aout_sys_t
{
    HWAVEOUT h_waveout;                        /* handle to waveout instance */

    WAVEFORMATEX waveformat;                                 /* Audio format */

    WAVEHDR waveheader[NUMBUF];

    int i_current_buffer;

    DWORD dw_counter;              /* Number of bytes played since beginning */
};

/*****************************************************************************
 * Functions exported as capabilities. They are declared as static so that
 * we don't pollute the namespace too much.
 *****************************************************************************/
static void aout_getfunctions( function_list_t * p_function_list )
{
    p_function_list->functions.aout.pf_open = aout_Open;
    p_function_list->functions.aout.pf_setformat = aout_SetFormat;
    p_function_list->functions.aout.pf_getbufinfo = aout_GetBufInfo;
    p_function_list->functions.aout.pf_play = aout_Play;
    p_function_list->functions.aout.pf_close = aout_Close;
}

/*****************************************************************************
 * aout_Open: open the audio device
 *****************************************************************************
 * This function opens and setups Win32 waveOut
 *****************************************************************************/
static int aout_Open( aout_thread_t *p_aout )
{
    int i;

    /* Allocate structure */
    p_aout->p_sys = malloc( sizeof( aout_sys_t ) );

    if( p_aout->p_sys == NULL )
    {
        msg_Err( p_aout, "out of memory" );
        return( 1 );
    }


    /* Initialize some variables */
    p_aout->p_sys->i_current_buffer = 0;
    for( i=0; i<NUMBUF; i++)
        p_aout->p_sys->waveheader[i].lpData = malloc( 1 );

    return OpenWaveOutDevice( p_aout );

}

/*****************************************************************************
 * aout_SetFormat: reset the audio device and sets its format
 *****************************************************************************
 * This functions set a new audio format.
 * For this we need to close the current device and create another
 * one with the desired format.
 *****************************************************************************/
static int aout_SetFormat( aout_thread_t *p_aout )
{
    msg_Dbg( p_aout, "aout_SetFormat" );

    /* Check if the format has changed */

    if( (p_aout->p_sys->waveformat.nChannels != p_aout->i_channels) ||
        (p_aout->p_sys->waveformat.nSamplesPerSec != p_aout->i_rate) )
    {
        /* Before calling waveOutClose we must reset the device */
        waveOutReset( p_aout->p_sys->h_waveout );

        if( waveOutClose( p_aout->p_sys->h_waveout ) != MMSYSERR_NOERROR )
        {
            msg_Err( p_aout, "waveOutClose failed" );
        }

        return OpenWaveOutDevice( p_aout );
    }

    return 0;
}

/*****************************************************************************
 * aout_GetBufInfo: buffer status query
 *****************************************************************************
 * returns the number of bytes in the audio buffer that have not yet been
 * sent to the sound device.
 *****************************************************************************/
static int aout_GetBufInfo( aout_thread_t *p_aout, int i_buffer_limit )
{
    MMTIME mmtime;

    mmtime.wType = TIME_BYTES;
    if( (waveOutGetPosition(p_aout->p_sys->h_waveout, &mmtime, sizeof(MMTIME)))
        != MMSYSERR_NOERROR || (mmtime.wType != TIME_BYTES) )
    {
        msg_Warn( p_aout, "waveOutGetPosition failed" );
        return i_buffer_limit;
    }


#if 0
    msg_Dbg( p_aout, "aout_GetBufInfo: %i",
                      p_aout->p_sys->dw_counter - mmtime.u.cb );
#endif

    return (p_aout->p_sys->dw_counter - mmtime.u.cb);
}

/*****************************************************************************
 * aout_Play: play a sound buffer
 *****************************************************************************
 * This function writes a buffer of i_length bytes
 *****************************************************************************/
static void aout_Play( aout_thread_t *p_aout, byte_t *p_buffer, int i_size )
{
    MMRESULT result;
    int current_buffer = p_aout->p_sys->i_current_buffer;

    p_aout->p_sys->i_current_buffer = (current_buffer + 1) % NUMBUF;

    /* Unprepare the old buffer */
    waveOutUnprepareHeader( p_aout->p_sys->h_waveout,
                            &p_aout->p_sys->waveheader[current_buffer],
                            sizeof(WAVEHDR) );

    /* Prepare the buffer */
    p_aout->p_sys->waveheader[current_buffer].lpData =
        realloc( p_aout->p_sys->waveheader[current_buffer].lpData, i_size );
    if( !p_aout->p_sys->waveheader[current_buffer].lpData )
    {
        msg_Err( p_aout, "could not allocate buffer" );
        return;
    }
    p_aout->p_sys->waveheader[current_buffer].dwBufferLength = i_size;
    p_aout->p_sys->waveheader[current_buffer].dwFlags = 0;

    result = waveOutPrepareHeader( p_aout->p_sys->h_waveout,
                                   &p_aout->p_sys->waveheader[current_buffer],
                                   sizeof(WAVEHDR) );
    if( result != MMSYSERR_NOERROR )
    {
        msg_Err( p_aout, "waveOutPrepareHeader failed" );
        return;
    }

    /* Send the buffer the waveOut queue */
    p_aout->p_vlc->pf_memcpy( p_aout->p_sys->waveheader[current_buffer].lpData,
                              p_buffer, i_size );
    result = waveOutWrite( p_aout->p_sys->h_waveout,
                           &p_aout->p_sys->waveheader[current_buffer],
                           sizeof(WAVEHDR) );
    if( result != MMSYSERR_NOERROR )
    {
        msg_Err( p_aout, "waveOutWrite failed" );
        return;
    }

    /* keep track of number of bytes played */
    p_aout->p_sys->dw_counter += i_size;

}

/*****************************************************************************
 * aout_Close: close the audio device
 *****************************************************************************/
static void aout_Close( aout_thread_t *p_aout )
{
    int i;

    /* Before calling waveOutClose we must reset the device */
    waveOutReset( p_aout->p_sys->h_waveout );

    /* Close the device */
    if( waveOutClose( p_aout->p_sys->h_waveout ) != MMSYSERR_NOERROR )
    {
        msg_Err( p_aout, "waveOutClose failed" );
    }

    /* Deallocate memory */
    for( i=0; i<NUMBUF; i++ )
        free( p_aout->p_sys->waveheader[i].lpData );

    if( p_aout->p_sys != NULL )
    { 
        free( p_aout->p_sys );
        p_aout->p_sys = NULL;
    }
}

/*****************************************************************************
 * OpenWaveOutDevice: open the sound device
 ****************************************************************************/
static int OpenWaveOutDevice( aout_thread_t *p_aout )
{
    MMRESULT result;

    /* initialize played bytes counter */
    p_aout->p_sys->dw_counter = 0;

    /* Set sound format */
    p_aout->p_sys->waveformat.wFormatTag       = WAVE_FORMAT_PCM;
    p_aout->p_sys->waveformat.nChannels        = p_aout->i_channels;
    p_aout->p_sys->waveformat.nSamplesPerSec   = p_aout->i_rate;
    p_aout->p_sys->waveformat.wBitsPerSample   = 16;
    p_aout->p_sys->waveformat.nBlockAlign      =
        p_aout->p_sys->waveformat.wBitsPerSample / 8 * p_aout->i_channels;
    p_aout->p_sys->waveformat.nAvgBytesPerSec  =
        p_aout->p_sys->waveformat.nSamplesPerSec *
            p_aout->p_sys->waveformat.nBlockAlign;


    /* Open the device */
    result = waveOutOpen( &p_aout->p_sys->h_waveout, WAVE_MAPPER,
                          &p_aout->p_sys->waveformat,
                          0 /*callback*/, 0 /*callback data*/, CALLBACK_NULL );
    if( result != MMSYSERR_NOERROR )
    {
        msg_Err( p_aout, "waveOutOpen failed" );
        return( 1 );
    }

    return( 0 );
}
