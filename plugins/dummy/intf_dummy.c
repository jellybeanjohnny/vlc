/*****************************************************************************
 * intf_dummy.c: dummy interface plugin
 *****************************************************************************
 * Copyright (C) 2000, 2001 VideoLAN
 * $Id: intf_dummy.c,v 1.21 2002/07/20 18:01:42 sam Exp $
 *
 * Authors: Samuel Hocevar <sam@zoy.org>
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
#include <stdlib.h>                                      /* malloc(), free() */
#include <string.h>

#include <vlc/vlc.h>
#include <vlc/intf.h>

/*****************************************************************************
 * intf_sys_t: description and status of FB interface
 *****************************************************************************/
struct intf_sys_t
{
    /* Prevent malloc(0) */
    int i_dummy;
};

/*****************************************************************************
 * Local prototypes.
 *****************************************************************************/
static int  intf_Open      ( intf_thread_t *p_intf );
static void intf_Close     ( intf_thread_t *p_intf );
static void intf_Run       ( intf_thread_t *p_intf );

/*****************************************************************************
 * Functions exported as capabilities. They are declared as static so that
 * we don't pollute the namespace too much.
 *****************************************************************************/
void _M( intf_getfunctions )( function_list_t * p_function_list )
{
    p_function_list->functions.intf.pf_open  = intf_Open;
    p_function_list->functions.intf.pf_close = intf_Close;
    p_function_list->functions.intf.pf_run   = intf_Run;
}

/*****************************************************************************
 * intf_Open: initialize dummy interface
 *****************************************************************************/
static int intf_Open( intf_thread_t *p_intf )
{
    /* Allocate instance and initialize some members */
    p_intf->p_sys = malloc( sizeof( intf_sys_t ) );
    if( p_intf->p_sys == NULL )
    {
        return( 1 );
    };

#ifdef WIN32
    AllocConsole();
    freopen( "CONOUT$", "w", stdout );
    freopen( "CONOUT$", "w", stderr );
    freopen( "CONIN$", "r", stdin );
    msg_Info( p_intf, VERSION_MESSAGE );
    msg_Info( p_intf, _("\nUsing the dummy interface plugin...") );
#endif

    return( 0 );
}

/*****************************************************************************
 * intf_Close: destroy dummy interface
 *****************************************************************************/
static void intf_Close( intf_thread_t *p_intf )
{
    /* Destroy structure */
    free( p_intf->p_sys );
}


/*****************************************************************************
 * intf_Run: main loop
 *****************************************************************************/
static void intf_Run( intf_thread_t *p_intf )
{
    while( !p_intf->b_die )
    {
        /* Wait a bit */
        msleep( INTF_IDLE_SLEEP );
    }
}

