/*****************************************************************************
 * aout_directx.c: Windows DirectX audio output method
 *****************************************************************************
 * Copyright (C) 2001 VideoLAN
 * $Id: aout_directx.c,v 1.25 2002/07/20 18:01:42 sam Exp $
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
#include <dsound.h>

/*****************************************************************************
 * DirectSound GUIDs.
 * Defining them here allows us to get rid of the dxguid library during
 * the linking stage.
 *****************************************************************************/
#include <initguid.h>
DEFINE_GUID(IID_IDirectSoundNotify, 0xb0210783, 0x89cd, 0x11d0, 0xaf, 0x8, 0x0, 0xa0, 0xc9, 0x25, 0xcd, 0x16);

/*****************************************************************************
 * notification_thread_t: DirectX event thread
 *****************************************************************************/
typedef struct notification_thread_t
{
    VLC_COMMON_MEMBERS

    aout_thread_t * p_aout;
    DSBPOSITIONNOTIFY p_events[2];               /* play notification events */

} notification_thread_t;

/*****************************************************************************
 * aout_sys_t: directx audio output method descriptor
 *****************************************************************************
 * This structure is part of the audio output thread descriptor.
 * It describes the direct sound specific properties of an audio device.
 *****************************************************************************/

struct aout_sys_t
{
    LPDIRECTSOUND       p_dsobject;              /* main Direct Sound object */

    LPDIRECTSOUNDBUFFER p_dsbuffer_primary;     /* the actual sound card buffer
                                                   (not used directly) */

    LPDIRECTSOUNDBUFFER p_dsbuffer;   /* the sound buffer we use (direct sound
                                       * takes care of mixing all the
                                       * secondary buffers into the primary) */

    LPDIRECTSOUNDNOTIFY p_dsnotify;         /* the position notify interface */

    HINSTANCE           hdsound_dll;      /* handle of the opened dsound dll */

    long l_buffer_size;                       /* secondary sound buffer size */
    long l_write_position;             /* next write position for the buffer */

    volatile vlc_bool_t b_buffer_underflown;   /* buffer underflow detection */
    volatile long l_data_played_from_beginning;   /* for underflow detection */
    volatile long l_data_written_from_beginning;  /* for underflow detection */

    vlc_mutex_t buffer_lock;                            /* audio buffer lock */

    notification_thread_t * p_notif;                 /* DirectSoundThread id */
};

/*****************************************************************************
 * Local prototypes.
 *****************************************************************************/
static int     aout_Open        ( aout_thread_t * );
static int     aout_SetFormat   ( aout_thread_t * );
static int     aout_GetBufInfo  ( aout_thread_t *, int );
static void    aout_Play        ( aout_thread_t *, byte_t *, int );
static void    aout_Close       ( aout_thread_t * );

/* local functions */
static int  DirectxCreateSecondaryBuffer ( aout_thread_t * );
static void DirectxDestroySecondaryBuffer( aout_thread_t * );
static int  DirectxInitDSound            ( aout_thread_t * );
static void DirectSoundThread            ( notification_thread_t * );

/*****************************************************************************
 * Functions exported as capabilities. They are declared as static so that
 * we don't pollute the namespace too much.
 *****************************************************************************/
void _M( aout_getfunctions )( function_list_t * p_function_list )
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
 * This function opens and setups Direct Sound.
 *****************************************************************************/
static int aout_Open( aout_thread_t *p_aout )
{
    HRESULT dsresult;
    DSBUFFERDESC dsbuffer_desc;

    msg_Dbg( p_aout, "aout_Open" );

   /* Allocate structure */
    p_aout->p_sys = malloc( sizeof( aout_sys_t ) );

    if( p_aout->p_sys == NULL )
    {
        msg_Err( p_aout, "out of memory" );
        return( 1 );
    }

    /* Initialize some variables */
    p_aout->p_sys->p_dsobject = NULL;
    p_aout->p_sys->p_dsbuffer_primary = NULL;
    p_aout->p_sys->p_dsbuffer = NULL;
    p_aout->p_sys->p_dsnotify = NULL;
    p_aout->p_sys->l_data_written_from_beginning = 0;
    p_aout->p_sys->l_data_played_from_beginning = 0;
    vlc_mutex_init( p_aout, &p_aout->p_sys->buffer_lock );


    /* Initialise DirectSound */
    if( DirectxInitDSound( p_aout ) )
    {
        msg_Warn( p_aout, "cannot initialize DirectSound" );
        return( 1 );
    }

    /* Obtain (not create) Direct Sound primary buffer */
    memset( &dsbuffer_desc, 0, sizeof(DSBUFFERDESC) );
    dsbuffer_desc.dwSize = sizeof(DSBUFFERDESC);
    dsbuffer_desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
    msg_Warn( p_aout, "create direct sound primary buffer" );
    dsresult = IDirectSound_CreateSoundBuffer(p_aout->p_sys->p_dsobject,
                                            &dsbuffer_desc,
                                            &p_aout->p_sys->p_dsbuffer_primary,
                                            NULL);
    if( dsresult != DS_OK )
    {
        msg_Warn( p_aout, "cannot create direct sound primary buffer" );
        IDirectSound_Release( p_aout->p_sys->p_dsobject );
        p_aout->p_sys->p_dsobject = NULL;
        p_aout->p_sys->p_dsbuffer_primary = NULL;
        return( 1 );
    }


    /* Now we need to setup DirectSound play notification */

    /* first we need to create the notification events */
    p_aout->p_sys->p_notif->p_events[0].hEventNotify =
        CreateEvent( NULL, FALSE, FALSE, NULL );
    p_aout->p_sys->p_notif->p_events[1].hEventNotify =
        CreateEvent( NULL, FALSE, FALSE, NULL );

    /* then launch the notification thread */
    msg_Dbg( p_aout, "creating DirectSoundThread" );
    p_aout->p_sys->p_notif =
                vlc_object_create( p_aout, sizeof(notification_thread_t) );
    p_aout->p_sys->p_notif->p_aout = p_aout;
    if( vlc_thread_create( p_aout->p_sys->p_notif,
                    "DirectSound Notification Thread", DirectSoundThread, 1 ) )
    {
        msg_Err( p_aout, "cannot create DirectSoundThread" );
        /* Let's go on anyway */
    }

    vlc_object_attach( p_aout->p_sys->p_notif, p_aout );

    return( 0 );
}

/*****************************************************************************
 * aout_SetFormat: reset the audio device and sets its format
 *****************************************************************************
 * This functions set a new audio format.
 * For this we need to close the current secondary buffer and create another
 * one with the desired format.
 *****************************************************************************/
static int aout_SetFormat( aout_thread_t *p_aout )
{
    HRESULT       dsresult;
    WAVEFORMATEX  *p_waveformat;
    unsigned long i_size_struct;

    msg_Dbg( p_aout, "aout_SetFormat" );

    /* Set the format of Direct Sound primary buffer */

    /* first we need to know the current format */
    dsresult = IDirectSoundBuffer_GetFormat( p_aout->p_sys->p_dsbuffer_primary,
                                             NULL, 0, &i_size_struct );
    if( dsresult == DS_OK )
    {
        p_waveformat = malloc( i_size_struct );
        dsresult = IDirectSoundBuffer_GetFormat(
                                             p_aout->p_sys->p_dsbuffer_primary,
                                             p_waveformat, i_size_struct,
                                             NULL );
    }

    if( dsresult == DS_OK )
    {
        /* Here we'll change the format */
        p_waveformat->nChannels        = 2; 
        p_waveformat->nSamplesPerSec   = (p_aout->i_rate < 44100) ? 44100
                                             : p_aout->i_rate; 
        p_waveformat->wBitsPerSample   = 16; 
        p_waveformat->nBlockAlign      = p_waveformat->wBitsPerSample / 8 *
                                             p_waveformat->nChannels;
        p_waveformat->nAvgBytesPerSec  = p_waveformat->nSamplesPerSec *
                                             p_waveformat->nBlockAlign;

        dsresult = IDirectSoundBuffer_SetFormat(
                                             p_aout->p_sys->p_dsbuffer_primary,
                                             p_waveformat );
    }
    else msg_Warn( p_aout, "cannot get primary buffer format" );

    if( dsresult != DS_OK )
        msg_Warn( p_aout, "cannot set primary buffer format" );


    /* Now we need to take care of Direct Sound secondary buffer */

    vlc_mutex_lock( &p_aout->p_sys->buffer_lock );

    /* first release the current secondary buffer */
    DirectxDestroySecondaryBuffer( p_aout );

    /* then create a new secondary buffer */
    if( DirectxCreateSecondaryBuffer( p_aout ) )
    {
        msg_Warn( p_aout, "cannot create buffer" );
        vlc_mutex_unlock( &p_aout->p_sys->buffer_lock );
        return( 1 );
    }

    vlc_mutex_unlock( &p_aout->p_sys->buffer_lock );

    return( 0 );
}

/*****************************************************************************
 * aout_GetBufInfo: buffer status query
 *****************************************************************************
 * returns the number of bytes in the audio buffer that have not yet been
 * sent to the sound device.
 *****************************************************************************/
static int aout_GetBufInfo( aout_thread_t *p_aout, int i_buffer_limit )
{
    long l_play_position, l_notused, l_result;
    HRESULT dsresult;

    if( p_aout->p_sys->b_buffer_underflown )
    {
        msg_Warn( p_aout, "aout_GetBufInfo underflow" );
        return( i_buffer_limit );
    }

    dsresult = IDirectSoundBuffer_GetCurrentPosition(p_aout->p_sys->p_dsbuffer,
                                                 &l_play_position, &l_notused);
    if( dsresult != DS_OK )
    {
        msg_Warn( p_aout, "aout_GetBufInfo cannot get current pos" );
        return( i_buffer_limit );
    }

    l_result = (p_aout->p_sys->l_write_position >= l_play_position) ?
      (p_aout->p_sys->l_write_position - l_play_position)
               : (p_aout->p_sys->l_buffer_size - l_play_position
                  + p_aout->p_sys->l_write_position);

#if 0
    msg_Dbg( p_aout, "aout_GetBufInfo: %i", i_result);
#endif
    return l_result;
}

/*****************************************************************************
 * aout_Play: play a sound buffer
 *****************************************************************************
 * This function writes a buffer of i_length bytes
 * Don't forget that DirectSound buffers are circular buffers.
 *****************************************************************************/
static void aout_Play( aout_thread_t *p_aout, byte_t *buffer, int i_size )
{
    VOID            *p_write_position, *p_start_buffer;
    long            l_bytes1, l_bytes2, l_play_position;
    HRESULT         dsresult;

    /* protect buffer access (because of DirectSoundThread) */
    vlc_mutex_lock( &p_aout->p_sys->buffer_lock );

    if( p_aout->p_sys->b_buffer_underflown )
    {
        /*  there has been an underflow so we need to play the new sample
         *  as soon as possible. This is why we query the play position */
        dsresult = IDirectSoundBuffer_GetCurrentPosition(
                                            p_aout->p_sys->p_dsbuffer,
                                            &l_play_position,
                                            &p_aout->p_sys->l_write_position );
        if( dsresult != DS_OK )
        {
            msg_Warn( p_aout, "cannot get buffer position" );
            p_aout->p_sys->l_write_position = 0; 
        }

        msg_Warn( p_aout, "aout_Play underflow" );
        /* reinitialise the underflow detection counters */
        p_aout->p_sys->b_buffer_underflown = 0;
        p_aout->p_sys->l_data_written_from_beginning = 0;

#define WRITE_P  p_aout->p_sys->l_write_position
#define PLAY_P   l_play_position
#define BUF_SIZE p_aout->p_sys->l_buffer_size
        p_aout->p_sys->l_data_played_from_beginning = -(WRITE_P %(BUF_SIZE/2));
        if( PLAY_P < BUF_SIZE/2 && WRITE_P > BUF_SIZE/2 )
        {
            p_aout->p_sys->l_data_played_from_beginning -= (BUF_SIZE/2);
        }
        if( PLAY_P > BUF_SIZE/2 && WRITE_P < BUF_SIZE/2 )
        {
            p_aout->p_sys->l_data_played_from_beginning -= (BUF_SIZE/2);
        }        
#undef WRITE_P
#undef PLAY_P
#undef BUF_SIZE
    }

    /* Before copying anything, we have to lock the buffer */
    dsresult = IDirectSoundBuffer_Lock( p_aout->p_sys->p_dsbuffer,
                   p_aout->p_sys->l_write_position,  /* Offset of lock start */
                   i_size,                        /* Number of bytes to lock */
                   &p_write_position,               /* Address of lock start */
                   &l_bytes1,    /* Count of bytes locked before wrap around */
                   &p_start_buffer,        /* Buffer adress (if wrap around) */
                   &l_bytes2,            /* Count of bytes after wrap around */
                   0);                                              /* Flags */
    if( dsresult == DSERR_BUFFERLOST )
    {
        IDirectSoundBuffer_Restore( p_aout->p_sys->p_dsbuffer );
        dsresult = IDirectSoundBuffer_Lock( p_aout->p_sys->p_dsbuffer,
                                            p_aout->p_sys->l_write_position,
                                            i_size,
                                            &p_write_position,
                                            &l_bytes1,
                                            &p_start_buffer,
                                            &l_bytes2,
                                            0);

    }
    if( dsresult != DS_OK )
    {
        msg_Warn( p_aout, "aout_Play cannot lock buffer" );
        vlc_mutex_unlock( &p_aout->p_sys->buffer_lock );
        return;
    }

    /* Now do the actual memcpy (two memcpy because the buffer is circular) */
    memcpy( p_write_position, buffer, l_bytes1 );
    if( p_start_buffer != NULL )
    {
        memcpy( p_start_buffer, buffer + l_bytes1, l_bytes2 );
    }

    /* Now the data has been copied, unlock the buffer */
    IDirectSoundBuffer_Unlock( p_aout->p_sys->p_dsbuffer, 
            p_write_position, l_bytes1, p_start_buffer, l_bytes2 );

    /* Update the write position index of the buffer*/
    p_aout->p_sys->l_write_position += i_size;
    p_aout->p_sys->l_write_position %= p_aout->p_sys->l_buffer_size;
    p_aout->p_sys->l_data_written_from_beginning += i_size;

    vlc_mutex_unlock( &p_aout->p_sys->buffer_lock );

    /* The play function has no effect if the buffer is already playing */
    dsresult = IDirectSoundBuffer_Play( p_aout->p_sys->p_dsbuffer,
                                        0,                         /* Unused */
                                        0,                         /* Unused */
                                        DSBPLAY_LOOPING );          /* Flags */
    if( dsresult == DSERR_BUFFERLOST )
    {
        IDirectSoundBuffer_Restore( p_aout->p_sys->p_dsbuffer );
        dsresult = IDirectSoundBuffer_Play( p_aout->p_sys->p_dsbuffer,
                                            0,                     /* Unused */
                                            0,                     /* Unused */
                                            DSBPLAY_LOOPING );      /* Flags */
    }
    if( dsresult != DS_OK )
    {
        msg_Warn( p_aout, "aout_Play cannot play buffer" );
        return;
    }

}

/*****************************************************************************
 * aout_Close: close the audio device
 *****************************************************************************/
static void aout_Close( aout_thread_t *p_aout )
{

    msg_Dbg( p_aout, "aout_Close" );

    /* kill the position notification thread, if any */
    vlc_object_detach_all( p_aout->p_sys->p_notif );
    if( p_aout->p_sys->p_notif->b_thread )
    {
        p_aout->p_sys->p_notif->b_die = 1;
        vlc_thread_join( p_aout->p_sys->p_notif );
    }
    vlc_object_destroy( p_aout->p_sys->p_notif );

    /* release the secondary buffer */
    DirectxDestroySecondaryBuffer( p_aout );

    /* then release the primary buffer */
    if( p_aout->p_sys->p_dsbuffer_primary != NULL )
    {
        IDirectSoundBuffer_Release( p_aout->p_sys->p_dsbuffer_primary );
        p_aout->p_sys->p_dsbuffer_primary = NULL;
    }  

    /* finally release the DirectSound object */
    if( p_aout->p_sys->p_dsobject != NULL )
    {
        IDirectSound_Release( p_aout->p_sys->p_dsobject );
        p_aout->p_sys->p_dsobject = NULL;
    }  
    
    /* free DSOUND.DLL */
    if( p_aout->p_sys->hdsound_dll != NULL )
    {
       FreeLibrary( p_aout->p_sys->hdsound_dll );
       p_aout->p_sys->hdsound_dll = NULL;
    }

    /* Close the Output. */
    if ( p_aout->p_sys != NULL )
    { 
        free( p_aout->p_sys );
        p_aout->p_sys = NULL;
    }
}

/*****************************************************************************
 * DirectxInitDSound
 *****************************************************************************
 *****************************************************************************/
static int DirectxInitDSound( aout_thread_t *p_aout )
{
    HRESULT (WINAPI *OurDirectSoundCreate)(LPGUID, LPDIRECTSOUND *, LPUNKNOWN);

    p_aout->p_sys->hdsound_dll = LoadLibrary("DSOUND.DLL");
    if( p_aout->p_sys->hdsound_dll == NULL )
    {
      msg_Warn( p_aout, "cannot open DSOUND.DLL" );
      return( 1 );
    }

    OurDirectSoundCreate = (void *)GetProcAddress( p_aout->p_sys->hdsound_dll,
                                                   "DirectSoundCreate" );

    if( OurDirectSoundCreate == NULL )
    {
      msg_Warn( p_aout, "GetProcAddress FAILED" );
      FreeLibrary( p_aout->p_sys->hdsound_dll );
      p_aout->p_sys->hdsound_dll = NULL;
      return( 1 );
    }

    /* Create the direct sound object */
    if( OurDirectSoundCreate(NULL, &p_aout->p_sys->p_dsobject, NULL) != DS_OK )
    {
        msg_Warn( p_aout, "cannot create a direct sound device" );
        p_aout->p_sys->p_dsobject = NULL;
        FreeLibrary( p_aout->p_sys->hdsound_dll );
        p_aout->p_sys->hdsound_dll = NULL;
        return( 1 );
    }

    /* Set DirectSound Cooperative level, ie what control we want over Windows
     * sound device. In our case, DSSCL_EXCLUSIVE means that we can modify the
     * settings of the primary buffer, but also that only the sound of our
     * application will be hearable when it will have the focus.
     * !!! (this is not really working as intended yet because to set the
     * cooperative level you need the window handle of your application, and
     * I don't know of any easy way to get it. Especially since we might play
     * sound without any video, and so what window handle should we use ???
     * The hack for now is to use the Desktop window handle - it seems to be
     * working */
    if( IDirectSound_SetCooperativeLevel(p_aout->p_sys->p_dsobject,
                                         GetDesktopWindow(),
                                         DSSCL_EXCLUSIVE) )
    {
        msg_Warn( p_aout, "cannot set direct sound cooperative level" );
    }

    return( 0 );
}

/*****************************************************************************
 * DirectxCreateSecondaryBuffer
 *****************************************************************************
 * This function creates the buffer we'll use to play audio.
 * In DirectSound there are two kinds of buffers:
 * - the primary buffer: which is the actual buffer that the soundcard plays
 * - the secondary buffer(s): these buffers are the one actually used by
 *    applications and DirectSound takes care of mixing them into the primary.
 *
 * Once you create a secondary buffer, you cannot change its format anymore so
 * you have to release the current and create another one.
 *****************************************************************************/
static int DirectxCreateSecondaryBuffer( aout_thread_t *p_aout )
{
    WAVEFORMATEX         waveformat;
    DSBUFFERDESC         dsbdesc;
    DSBCAPS              dsbcaps;

    /* First set the buffer format */
    memset(&waveformat, 0, sizeof(WAVEFORMATEX)); 
    waveformat.wFormatTag      = WAVE_FORMAT_PCM; 
    waveformat.nChannels       = p_aout->i_channels; 
    waveformat.nSamplesPerSec  = p_aout->i_rate; 
    waveformat.wBitsPerSample  = 16; 
    waveformat.nBlockAlign     = waveformat.wBitsPerSample / 8 *
                                 waveformat.nChannels;
    waveformat.nAvgBytesPerSec = waveformat.nSamplesPerSec *
                                     waveformat.nBlockAlign;

    /* Then fill in the descriptor */
    memset(&dsbdesc, 0, sizeof(DSBUFFERDESC)); 
    dsbdesc.dwSize = sizeof(DSBUFFERDESC); 
    dsbdesc.dwFlags = DSBCAPS_GETCURRENTPOSITION2/* Better position accuracy */
                    | DSBCAPS_CTRLPOSITIONNOTIFY     /* We need notification */
                    | DSBCAPS_GLOBALFOCUS;      /* Allows background playing */
    dsbdesc.dwBufferBytes = waveformat.nAvgBytesPerSec * 2;  /* 2 sec buffer */
    dsbdesc.lpwfxFormat = &waveformat; 
 
    if( IDirectSound_CreateSoundBuffer( p_aout->p_sys->p_dsobject,
                                        &dsbdesc,
                                        &p_aout->p_sys->p_dsbuffer,
                                        NULL) != DS_OK )
    {
        msg_Warn( p_aout, "cannot create direct sound secondary buffer" );
        p_aout->p_sys->p_dsbuffer = NULL;
        return( 1 );
    }

    /* backup the size of the secondary sound buffer */
    memset(&dsbcaps, 0, sizeof(DSBCAPS)); 
    dsbcaps.dwSize = sizeof(DSBCAPS);
    IDirectSoundBuffer_GetCaps( p_aout->p_sys->p_dsbuffer, &dsbcaps  );
    p_aout->p_sys->l_buffer_size = dsbcaps.dwBufferBytes;
    p_aout->p_sys->l_write_position = 0;

    msg_Dbg( p_aout, "DirectxCreateSecondaryBuffer: %li",
                     p_aout->p_sys->l_buffer_size );

    /* Now the secondary buffer is created, we need to setup its position
     * notification */
    p_aout->p_sys->p_notif->p_events[0].dwOffset = 0;    /* notif position */
    p_aout->p_sys->p_notif->p_events[1].dwOffset = dsbcaps.dwBufferBytes / 2;

    /* Get the IDirectSoundNotify interface */
    if FAILED( IDirectSoundBuffer_QueryInterface( p_aout->p_sys->p_dsbuffer,
                                                  &IID_IDirectSoundNotify,
                                       (LPVOID *)&p_aout->p_sys->p_dsnotify ) )
    {
        msg_Warn( p_aout, "cannot get Notify interface" );
        /* Go on anyway */
        p_aout->p_sys->p_dsnotify = NULL;
        return( 0 );
    }
        
    if FAILED( IDirectSoundNotify_SetNotificationPositions(
                                        p_aout->p_sys->p_dsnotify,
                                        2,
                                        p_aout->p_sys->p_notif->p_events ) )
    {
        msg_Warn( p_aout, "cannot set position Notification" );
        /* Go on anyway */
    }

    return( 0 );
}

/*****************************************************************************
 * DirectxCreateSecondaryBuffer
 *****************************************************************************
 * This function destroy the secondary buffer.
 *****************************************************************************/
static void DirectxDestroySecondaryBuffer( aout_thread_t *p_aout )
{
    /* make sure the buffer isn't playing */
    if( p_aout->p_sys->p_dsbuffer != NULL )
    {
        IDirectSoundBuffer_Stop( p_aout->p_sys->p_dsbuffer );
    }

    if( p_aout->p_sys->p_dsnotify != NULL )
    {
        IDirectSoundNotify_Release( p_aout->p_sys->p_dsnotify );
        p_aout->p_sys->p_dsnotify = NULL;
    }

    if( p_aout->p_sys->p_dsbuffer != NULL )
    {
        IDirectSoundBuffer_Release( p_aout->p_sys->p_dsbuffer );
        p_aout->p_sys->p_dsbuffer = NULL;
    }
}

/*****************************************************************************
 * DirectSoundThread: this thread will capture play notification events. 
 *****************************************************************************
 * As Direct Sound uses circular buffers, we need to use event notification to
 * manage them.
 * Using event notification implies blocking the thread until the event is
 * signaled so we really need to run this in a separate thread.
 *****************************************************************************/
static void DirectSoundThread( notification_thread_t *p_notif )
{
    HANDLE  notification_events[2];
    VOID    *p_write_position, *p_start_buffer;
    long    l_bytes1, l_bytes2;
    HRESULT dsresult;
    long    l_buffer_size, l_play_position, l_data_in_buffer;

    aout_thread_t *p_aout = p_notif->p_aout;

#define P_EVENTS p_aout->p_sys->p_notif->p_events
    notification_events[0] = P_EVENTS[0].hEventNotify;
    notification_events[1] = P_EVENTS[1].hEventNotify;

    /* Tell the main thread that we are ready */
    vlc_thread_ready( p_notif );

    /* this thread must be high-priority */
    if( !SetThreadPriority( GetCurrentThread(),
                            THREAD_PRIORITY_ABOVE_NORMAL ) )
    {
        msg_Warn( p_notif, "DirectSoundThread could not renice itself" );
    }

    msg_Dbg( p_notif, "DirectSoundThread ready" );

    while( !p_notif->b_die )
    {
        /* wait for the position notification */
        l_play_position = WaitForMultipleObjects( 2, notification_events,
                                                  0, INFINITE ); 
        vlc_mutex_lock( &p_aout->p_sys->buffer_lock );

        if( p_notif->b_die )
        {
            break;
        }

        /* check for buffer underflow (bodge for wrap around) */
        l_buffer_size = p_aout->p_sys->l_buffer_size;
        l_play_position = (l_play_position - WAIT_OBJECT_0) * l_buffer_size/2;
        p_aout->p_sys->l_data_played_from_beginning += (l_buffer_size/2);
        l_data_in_buffer = p_aout->p_sys->l_data_written_from_beginning -
                               p_aout->p_sys->l_data_played_from_beginning; 

        /* detect wrap-around */
        if( l_data_in_buffer < (-l_buffer_size/2) )
        {
            msg_Dbg( p_notif, "DirectSoundThread wrap around: %li",
                              l_data_in_buffer );
            l_data_in_buffer += l_buffer_size;
        }

        /* detect underflow */
        if( l_data_in_buffer <= 0 )
        {
            msg_Warn( p_notif,
                      "DirectSoundThread underflow: %li", l_data_in_buffer );
            p_aout->p_sys->b_buffer_underflown = 1;
            p_aout->p_sys->l_write_position =
                  (l_play_position + l_buffer_size/2) % l_buffer_size;
            l_data_in_buffer = l_buffer_size / 2;
            p_aout->p_sys->l_data_played_from_beginning -= (l_buffer_size/2);
        }


        /* Clear the data which has already been played */

        /* Before copying anything, we have to lock the buffer */
        dsresult = IDirectSoundBuffer_Lock( p_aout->p_sys->p_dsbuffer,
                   p_aout->p_sys->l_write_position,  /* Offset of lock start */
                   l_buffer_size - l_data_in_buffer,      /* Number of bytes */
                   &p_write_position,               /* Address of lock start */
                   &l_bytes1,    /* Count of bytes locked before wrap around */
                   &p_start_buffer,        /* Buffer adress (if wrap around) */
                   &l_bytes2,            /* Count of bytes after wrap around */
                   0);                                              /* Flags */
        if( dsresult == DSERR_BUFFERLOST )
        {
            IDirectSoundBuffer_Restore( p_aout->p_sys->p_dsbuffer );
            dsresult = IDirectSoundBuffer_Lock( p_aout->p_sys->p_dsbuffer,
                                          p_aout->p_sys->l_write_position,
                                          l_buffer_size - l_data_in_buffer,
                                          &p_write_position,
                                          &l_bytes1,
                                          &p_start_buffer,
                                          &l_bytes2,
                                          0);
        }
        if( dsresult != DS_OK )
        {
            msg_Warn( p_notif, "aout_Play cannot lock buffer" );
            vlc_mutex_unlock( &p_aout->p_sys->buffer_lock );
            return;
        }

        /* Now do the actual memcpy (two because the buffer is circular) */
        memset( p_write_position, 0, l_bytes1 );
        if( p_start_buffer != NULL )
        {
            memset( p_start_buffer, 0, l_bytes2 );
        }

        /* Now the data has been copied, unlock the buffer */
        IDirectSoundBuffer_Unlock( p_aout->p_sys->p_dsbuffer, 
                        p_write_position, l_bytes1, p_start_buffer, l_bytes2 );

        vlc_mutex_unlock( &p_aout->p_sys->buffer_lock );

    }

    /* free the events */
    CloseHandle( notification_events[0] );
    CloseHandle( notification_events[1] );

    msg_Dbg( p_notif, "DirectSoundThread exiting" );

}
