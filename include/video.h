/*****************************************************************************
 * video.h: common video definitions
 * This header is required by all modules which have to handle pictures. It
 * includes all common video types and constants.
 *****************************************************************************
 * Copyright (C) 1999, 2000 VideoLAN
 * $Id: video.h,v 1.55 2002/07/20 18:01:41 sam Exp $
 *
 * Authors: Vincent Seguin <seguin@via.ecp.fr>
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
 * plane_t: description of a planar graphic field
 *****************************************************************************/
typedef struct plane_t
{
    u8 *p_pixels;                               /* Start of the plane's data */

    /* Variables used for fast memcpy operations */
    int i_lines;                                          /* Number of lines */
    int i_pitch;             /* Number of bytes in a line, including margins */

    /* Size of a macropixel, defaults to 1 */
    int i_pixel_bytes;

    /* Is there a margin ? defaults to no */
    vlc_bool_t b_margin;

    /* Variables used for pictures with margins */
    int i_visible_bytes;                 /* How many real pixels are there ? */
    vlc_bool_t b_hidden;          /* Are we allowed to write to the margin ? */

} plane_t;

/*****************************************************************************
 * picture_t: video picture
 *****************************************************************************
 * Any picture destined to be displayed by a video output thread should be
 * stored in this structure from it's creation to it's effective display.
 * Picture type and flags should only be modified by the output thread. Note
 * that an empty picture MUST have its flags set to 0.
 *****************************************************************************/
struct picture_t
{
    /* Picture data - data can always be freely modified, but p_data may
     * NEVER be modified. A direct buffer can be handled as the plugin
     * wishes, it can even swap p_pixels buffers. */
    u8             *p_data;
    void           *p_data_orig;                  /* pointer before memalign */
    plane_t         p[ VOUT_MAX_PLANES ];       /* description of the planes */
    int             i_planes;                  /* number of allocated planes */

    /* Type and flags - should NOT be modified except by the vout thread */
    int             i_status;                               /* picture flags */
    int             i_type;                  /* is picture a direct buffer ? */
    int             i_matrix_coefficients;     /* in YUV type, encoding type */

    /* Picture management properties - these properties can be modified using
     * the video output thread API, but should never be written directly */
    int             i_refcount;                    /* link reference counter */
    mtime_t         date;                                    /* display date */
    vlc_bool_t      b_force;

    /* Picture dynamic properties - those properties can be changed by the
     * decoder */
    vlc_bool_t      b_progressive;            /* is it a progressive frame ? */
    vlc_bool_t      b_repeat_first_field;                         /* RFF bit */
    vlc_bool_t      b_top_field_first;               /* which field is first */

    /* The picture heap we are attached to */
    picture_heap_t* p_heap;

    /* Private data - the video output plugin might want to put stuff here to
     * keep track of the picture */
    picture_sys_t * p_sys;
};

/*****************************************************************************
 * picture_heap_t: video picture heap, either render (to store pictures used
 * by the decoder) or output (to store pictures displayed by the vout plugin)
 *****************************************************************************/
struct picture_heap_t
{
    int i_pictures;                                     /* current heap size */

    /* Picture static properties - those properties are fixed at initialization
     * and should NOT be modified */
    int i_width;                                            /* picture width */
    int i_height;                                          /* picture height */
    u32 i_chroma;                                          /* picture chroma */
    int i_aspect;                                            /* aspect ratio */

    /* Real pictures */
    picture_t*      pp_picture[VOUT_MAX_PICTURES];               /* pictures */

    /* Stuff used for truecolor RGB planes */
    int i_rmask, i_rrshift, i_lrshift;
    int i_gmask, i_rgshift, i_lgshift;
    int i_bmask, i_rbshift, i_lbshift;

    /* Stuff used for palettized RGB planes */
    void (* pf_setpalette) ( vout_thread_t *, u16 *, u16 *, u16 * );
};

/* RGB2PIXEL: assemble RGB components to a pixel value, returns a u32 */
#define RGB2PIXEL( p_vout, i_r, i_g, i_b )                                    \
    (((((u32)i_r) >> p_vout->output.i_rrshift) << p_vout->output.i_lrshift) | \
     ((((u32)i_g) >> p_vout->output.i_rgshift) << p_vout->output.i_lgshift) | \
     ((((u32)i_b) >> p_vout->output.i_rbshift) << p_vout->output.i_lbshift))

/*****************************************************************************
 * Flags used to describe the status of a picture
 *****************************************************************************/

/* Picture type */
#define EMPTY_PICTURE           0                            /* empty buffer */
#define MEMORY_PICTURE          100                 /* heap-allocated buffer */
#define DIRECT_PICTURE          200                         /* direct buffer */

/* Picture status */
#define FREE_PICTURE            0                  /* free and not allocated */
#define RESERVED_PICTURE        1                  /* allocated and reserved */
#define RESERVED_DATED_PICTURE  2              /* waiting for DisplayPicture */
#define RESERVED_DISP_PICTURE   3               /* waiting for a DatePicture */
#define READY_PICTURE           4                       /* ready for display */
#define DISPLAYED_PICTURE       5            /* been displayed but is linked */
#define DESTROYED_PICTURE       6              /* allocated but no more used */

/*****************************************************************************
 * Codes used to describe picture format - see http://www.webartz.com/fourcc/
 *****************************************************************************/
#define VLC_FOURCC( a, b, c, d ) \
    ( ((u32)a) | ( ((u32)b) << 8 ) | ( ((u32)c) << 16 ) | ( ((u32)d) << 24 ) )

#define VLC_TWOCC( a, b ) \
    ( (u16)(a) | ( (u16)(b) << 8 ) )

/* AVI stuff */
#define FOURCC_RIFF         VLC_FOURCC('R','I','F','F')
#define FOURCC_LIST         VLC_FOURCC('L','I','S','T')
#define FOURCC_JUNK         VLC_FOURCC('J','U','N','K')
#define FOURCC_AVI          VLC_FOURCC('A','V','I',' ')
#define FOURCC_WAVE         VLC_FOURCC('W','A','V','E')

#define FOURCC_avih         VLC_FOURCC('a','v','i','h')
#define FOURCC_hdrl         VLC_FOURCC('h','d','r','l')
#define FOURCC_movi         VLC_FOURCC('m','o','v','i')
#define FOURCC_idx1         VLC_FOURCC('i','d','x','1')

#define FOURCC_strl         VLC_FOURCC('s','t','r','l')
#define FOURCC_strh         VLC_FOURCC('s','t','r','h')
#define FOURCC_strf         VLC_FOURCC('s','t','r','f')
#define FOURCC_strd         VLC_FOURCC('s','t','r','d')

#define FOURCC_rec          VLC_FOURCC('r','e','c',' ')
#define FOURCC_auds         VLC_FOURCC('a','u','d','s')
#define FOURCC_vids         VLC_FOURCC('v','i','d','s')

#define TWOCC_wb            VLC_TWOCC('w','b')
#define TWOCC_db            VLC_TWOCC('d','b')
#define TWOCC_dc            VLC_TWOCC('d','c')
#define TWOCC_pc            VLC_TWOCC('p','c')

/* MPEG4 codec */
#define FOURCC_DIVX         VLC_FOURCC('D','I','V','X')
#define FOURCC_divx         VLC_FOURCC('d','i','v','x')
#define FOURCC_DIV1         VLC_FOURCC('D','I','V','1')
#define FOURCC_div1         VLC_FOURCC('d','i','v','1')
#define FOURCC_MP4S         VLC_FOURCC('M','P','4','S')
#define FOURCC_mp4s         VLC_FOURCC('m','p','4','s')
#define FOURCC_M4S2         VLC_FOURCC('M','4','S','2')
#define FOURCC_m4s2         VLC_FOURCC('m','4','s','2')
#define FOURCC_xvid         VLC_FOURCC('x','v','i','d')
#define FOURCC_XVID         VLC_FOURCC('X','V','I','D')
#define FOURCC_XviD         VLC_FOURCC('X','v','i','D')
#define FOURCC_DX50         VLC_FOURCC('D','X','5','0')
#define FOURCC_mp4v         VLC_FOURCC('m','p','4','v')
#define FOURCC_4            VLC_FOURCC( 4,  0,  0,  0 )
 
/* MSMPEG4 v2 */
#define FOURCC_MPG4         VLC_FOURCC('M','P','G','4')
#define FOURCC_mpg4         VLC_FOURCC('m','p','g','4')
#define FOURCC_DIV2         VLC_FOURCC('D','I','V','2')
#define FOURCC_div2         VLC_FOURCC('d','i','v','2')
#define FOURCC_MP42         VLC_FOURCC('M','P','4','2')
#define FOURCC_mp42         VLC_FOURCC('m','p','4','2')

/* MSMPEG4 v3 / M$ mpeg4 v3 */
#define FOURCC_MPG3         VLC_FOURCC('M','P','G','3')
#define FOURCC_mpg3         VLC_FOURCC('m','p','g','3')
#define FOURCC_div3         VLC_FOURCC('d','i','v','3')
#define FOURCC_MP43         VLC_FOURCC('M','P','4','3')
#define FOURCC_mp43         VLC_FOURCC('m','p','4','3')

/* DivX 3.20 */
#define FOURCC_DIV3         VLC_FOURCC('D','I','V','3')
#define FOURCC_DIV4         VLC_FOURCC('D','I','V','4')
#define FOURCC_div4         VLC_FOURCC('d','i','v','4')
#define FOURCC_DIV5         VLC_FOURCC('D','I','V','5')
#define FOURCC_div5         VLC_FOURCC('d','i','v','5')
#define FOURCC_DIV6         VLC_FOURCC('D','I','V','6')
#define FOURCC_div6         VLC_FOURCC('d','i','v','6')

/* AngelPotion stuff */
#define FOURCC_AP41         VLC_FOURCC('A','P','4','1')

/* ?? */
#define FOURCC_3IV1         VLC_FOURCC('3','I','V','1')
/* H263 and H263i */
#define FOURCC_H263         VLC_FOURCC('H','2','6','3')
#define FOURCC_h263         VLC_FOURCC('h','2','6','3')
#define FOURCC_U263         VLC_FOURCC('U','2','6','3')
#define FOURCC_I263         VLC_FOURCC('I','2','6','3')
#define FOURCC_i263         VLC_FOURCC('i','2','6','3')


/* Packed RGB for 8bpp */
#define FOURCC_BI_RGB       VLC_FOURCC( 0 , 0 , 0 , 0 )
#define FOURCC_RGB2         VLC_FOURCC('R','G','B','2')

/* Packed RGB for 16, 24, 32bpp */
#define FOURCC_BI_BITFIELDS VLC_FOURCC( 0 , 0 , 0 , 3 )

/* Packed RGB 15bpp, 0x1f, 0x7e0, 0xf800 */
#define FOURCC_RV15         VLC_FOURCC('R','V','1','5')

/* Packed RGB 16bpp, 0x1f, 0x3e0, 0x7c00 */
#define FOURCC_RV16         VLC_FOURCC('R','V','1','6')

/* Packed RGB 24bpp, 0xff, 0xff00, 0xff0000 */
#define FOURCC_RV24         VLC_FOURCC('R','V','2','4')

/* Packed RGB 32bpp, 0xff, 0xff00, 0xff0000 */
#define FOURCC_RV32         VLC_FOURCC('R','V','3','2')

/* Planar YUV 4:2:0, Y:U:V */
#define FOURCC_I420         VLC_FOURCC('I','4','2','0')
#define FOURCC_IYUV         VLC_FOURCC('I','Y','U','V')

/* Planar YUV 4:2:0, Y:V:U */
#define FOURCC_YV12         VLC_FOURCC('Y','V','1','2')

/* Packed YUV 4:2:2, U:Y:V:Y, interlaced */
#define FOURCC_IUYV         VLC_FOURCC('I','U','Y','V')

/* Packed YUV 4:2:2, U:Y:V:Y */
#define FOURCC_UYVY         VLC_FOURCC('U','Y','V','Y')
#define FOURCC_UYNV         VLC_FOURCC('U','Y','N','V')
#define FOURCC_Y422         VLC_FOURCC('Y','4','2','2')

/* Packed YUV 4:2:2, U:Y:V:Y, reverted */
#define FOURCC_cyuv         VLC_FOURCC('c','y','u','v')

/* Packed YUV 4:2:2, Y:U:Y:V */
#define FOURCC_YUY2         VLC_FOURCC('Y','U','Y','2')
#define FOURCC_YUNV         VLC_FOURCC('Y','U','N','V')

/* Packed YUV 4:2:2, Y:V:Y:U */
#define FOURCC_YVYU         VLC_FOURCC('Y','V','Y','U')

/* Packed YUV 2:1:1, Y:U:Y:V */
#define FOURCC_Y211         VLC_FOURCC('Y','2','1','1')

/* Custom formats which we use but which don't exist in the fourcc database */

/* Planar Y, packed UV, from Matrox */
#define FOURCC_YMGA         VLC_FOURCC('Y','M','G','A')

/* Planar 4:2:2, Y:U:V */
#define FOURCC_I422         VLC_FOURCC('I','4','2','2')

/* Planar 4:4:4, Y:U:V */
#define FOURCC_I444         VLC_FOURCC('I','4','4','4')

/*****************************************************************************
 * Shortcuts to access image components
 *****************************************************************************/

/* Plane indices */
#define Y_PLANE      0
#define U_PLANE      1
#define V_PLANE      2

/* Shortcuts */
#define Y_PIXELS     p[Y_PLANE].p_pixels
#define Y_PITCH      p[Y_PLANE].i_pitch
#define U_PIXELS     p[U_PLANE].p_pixels
#define U_PITCH      p[U_PLANE].i_pitch
#define V_PIXELS     p[V_PLANE].p_pixels
#define V_PITCH      p[V_PLANE].i_pitch

/*****************************************************************************
 * subpicture_t: video subtitle
 *****************************************************************************
 * Any subtitle destined to be displayed by a video output thread should
 * be stored in this structure from it's creation to it's effective display.
 * Subtitle type and flags should only be modified by the output thread. Note
 * that an empty subtitle MUST have its flags set to 0.
 *****************************************************************************/
struct subpicture_t
{
    /* Type and flags - should NOT be modified except by the vout thread */
    int             i_type;                                          /* type */
    int             i_status;                                       /* flags */
    int             i_size;                                     /* data size */
    subpicture_t *  p_next;                 /* next subtitle to be displayed */

    /* Date properties */
    mtime_t         i_start;                    /* beginning of display date */
    mtime_t         i_stop;                           /* end of display date */
    vlc_bool_t      b_ephemer;             /* does the subtitle have a TTL ? */

    /* Display properties - these properties are only indicative and may be
     * changed by the video output thread, or simply ignored depending of the
     * subtitle type. */
    int             i_x;                   /* offset from alignment position */
    int             i_y;                   /* offset from alignment position */
    int             i_width;                                /* picture width */
    int             i_height;                              /* picture height */

#if 0
    /* Additionnal properties depending of the subpicture type */
    union
    {
        /* Text subpictures properties - text is stored in data area, in ASCIIZ
         * format */
        struct
        {
            vout_font_t *       p_font;            /* font, NULL for default */
            int                 i_style;                       /* text style */
            u32                 i_char_color;             /* character color */
            u32                 i_border_color;              /* border color */
            u32                 i_bg_color;              /* background color */
        } text;
    } type;
#endif

    /* The subpicture rendering routine */
    void ( *pf_render ) ( vout_thread_t *, picture_t *, const subpicture_t * );

    /* Private data - the subtitle plugin might want to put stuff here to
     * keep track of the subpicture */
    subpicture_sys_t *p_sys;                              /* subpicture data */
    void             *p_sys_orig;                 /* pointer before memalign */
};

/* Subpicture type */
#define EMPTY_SUBPICTURE       0     /* subtitle slot is empty and available */
#define MEMORY_SUBPICTURE      100            /* subpicture stored in memory */

/* Subpicture status */
#define FREE_SUBPICTURE        0                   /* free and not allocated */
#define RESERVED_SUBPICTURE    1                   /* allocated and reserved */
#define READY_SUBPICTURE       2                        /* ready for display */
#define DESTROYED_SUBPICTURE   3           /* allocated but not used anymore */

