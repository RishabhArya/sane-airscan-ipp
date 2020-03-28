/* sane - Scanner Access Now Easy.

   Copyright (C) 2020 Thierry HUCHARD <thierry@ordissimo.com>

   This file is part of the SANE package.

   SANE is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3 of the License, or (at your
   option) any later version.

   SANE is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with sane; see the file COPYING.  If not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   This file implements a SANE backend for airscan scanners. */

#include "airscan.h"

#include <tiffio.h>
#include <string.h>

/* TIFF image decoder
 */
typedef struct {
    image_decoder                 decoder;   /* Base class */
    TIFF*                         tif;       /* libtiff decoder */
    int                           num_lines; /* Num of lines left to read */
    int                           current_line; /* Current of lines */
    unsigned char                *mem_file; /* Position of the beginning
                                               of the tiff file. */
    toff_t                         offset_file; /* Moving the start position 
                                                  of the tiff file. */
    tsize_t                        size_file; /* Size of the tiff file. */
    
} image_decoder_tiff;

static void
airscan_dummy_unmap_proc (thandle_t fd, tdata_t base, toff_t size)
{
    (void)fd;
    (void)base;
    (void)size;
    printf("airscan_write_proc: This function is not useful. Defined out of necessity..\n");
}

static int
airscan_dummy_map_proc (thandle_t fd, tdata_t* pbase, toff_t* psize) 
{
    (void)fd;
    (void)pbase;
    (void)psize;

    printf("airscan_write_proc: This function is not useful. Defined out of necessity..\n");
    return (0);
}

static tsize_t 
airscan_read_proc(thandle_t handle, tdata_t data, tsize_t n)
{
    tsize_t n_remaining, n_copy;
    image_decoder_tiff *tiff;
    void *src_addr;

    /* Pointer to the memory file */
    tiff = (image_decoder_tiff*)(handle);
    if(!tiff || !tiff->mem_file) {
        fprintf(stderr, "airscan_read_proc: !tiff || !tiff->mem_file!\n");
        return 0;
    }

    /* find the actual number of bytes to read (copy) */
    n_copy = n;
    if((tsize_t)tiff->offset_file >= tiff->size_file)
        n_remaining = 0;
    else
        n_remaining = tiff->size_file - tiff->offset_file;
    
    if(n_copy > n_remaining)
        n_copy = n_remaining;
    
    /* EOF, return immediately */
    if(n_copy <= 0)
        return (0);

    src_addr = (void*)(&(tiff->mem_file[tiff->offset_file]));
    memcpy((void*)data, src_addr, n_copy);
    tiff->offset_file += n_copy;     /* Actualisation de l'offset */
    return n_copy;
}

static tsize_t
airscan_write_proc(thandle_t handle, tdata_t data, tsize_t n)
{
    (void)handle;
    (void)data;
    (void)n;
    
    printf("airscan_write_proc: This function is not useful. Defined out of necessity..\n");
    return (-1);
}

static toff_t
airscan_seek_proc (thandle_t handle, toff_t ofs, int whence)
{
    image_decoder_tiff *tiff;
    toff_t new_offset;

    /* Pointer to the memory file */
    tiff = (image_decoder_tiff*)(handle);
    if(!tiff || !tiff->mem_file) {
        fprintf(stderr, "airscan_seek_proc: !tiff || !tiff->mem_file!\n");
        return (-1);
    }

    /* find the location we plan to seek to */
    switch (whence) {
        case SEEK_SET:
            new_offset = ofs;
            break;
        case SEEK_CUR:
            new_offset = tiff->offset_file + ofs;
            break;
        default:
            /* Not supported */
            fprintf(stderr, 
                "airscan_seek_proc: "
                "Unsupported TIFF SEEK type.\n");
            return (-1);
    }

    /* Updating the offset */
    tiff->offset_file = new_offset;
    return tiff->offset_file;
}

static int
airscan_close_proc (thandle_t handle)
{
    image_decoder_tiff *tiff;

    /* Pointer to the memory file */
    tiff = (image_decoder_tiff*)(handle);
    if(!tiff || !tiff->mem_file) {
        fprintf(stderr,"airscan_close_proc: !tiff || !tiff->mem_file!\n");
        return (0);
    }
    
    /* Close the memory file */
    tiff->mem_file    = NULL;
    tiff->offset_file = 0;
    tiff->size_file   = 0;
    
    return (0);
}

static toff_t
airscan_size_proc (thandle_t handle)
{
    image_decoder_tiff *tiff;

    /* Pointer to the memory file */
    tiff = (image_decoder_tiff*)(handle);
    if(!tiff || !tiff->mem_file) {
        fprintf(stderr,"airscan_size_proc: !tiff || !tiff->mem_file!\n");
        return (0);
    }

    /* return size */
    return (toff_t)(tiff->size_file);
}

/* Free TIFF decoder
 */
static void
image_decoder_tiff_free (image_decoder *decoder)
{
    image_decoder_tiff *tiff = (image_decoder_tiff*) decoder;

    if (tiff->tif)
       TIFFClose(tiff->tif);
    g_free(tiff);
}

/* Begin TIFF decoding
 */
static error
image_decoder_tiff_begin (image_decoder *decoder, const void *data,
        size_t size)
{
    image_decoder_tiff *tiff = (image_decoder_tiff*) decoder;

    /* Set the TiffClientOpen interface to read a file from memory. */
    tiff->mem_file = (unsigned char*)data;
    tiff->offset_file = 0;
    tiff->size_file = size;

    tiff->tif = TIFFClientOpen("airscan TIFF Interface", 
         "r", (image_decoder_tiff*)(tiff),
        airscan_read_proc, airscan_write_proc,
        airscan_seek_proc, airscan_close_proc,
        airscan_size_proc, airscan_dummy_map_proc, airscan_dummy_unmap_proc);
	if (tiff->tif == NULL) {
		 return ERROR("TIFF: invalid open memory");
	}
    if (TIFFGetField(tiff->tif, TIFFTAG_IMAGELENGTH, &tiff->num_lines))
        return NULL;
    return ERROR("TIFF: invalid header");;
}

/* Reset TIFF decoder
 */
static void
image_decoder_tiff_reset (image_decoder *decoder)
{
    image_decoder_tiff *tiff = (image_decoder_tiff*) decoder;
    TIFFCleanup(tiff->tif);
    tiff->offset_file = 0;
}

/* Get bytes count per pixel
 */
static int
image_decoder_tiff_get_bytes_per_pixel (image_decoder *decoder)
{
	int componment = 0;
    image_decoder_tiff *tiff = (image_decoder_tiff*) decoder;
    TIFFGetField(tiff->tif, TIFFTAG_SAMPLESPERPIXEL, &componment);
    return componment;
}

/* Get image parameters
 */
static void
image_decoder_tiff_get_params (image_decoder *decoder, SANE_Parameters *params)
{
	int w, h, componment;
    image_decoder_tiff *tiff = (image_decoder_tiff*) decoder;
    TIFFGetField(tiff->tif, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(tiff->tif, TIFFTAG_IMAGELENGTH, &h);
    TIFFGetField(tiff->tif, TIFFTAG_SAMPLESPERPIXEL, &componment);
    
    params->last_frame = SANE_TRUE;
    params->pixels_per_line = w;
    params->lines = h;
    params->depth = 8;

    if (componment == 1) {
        params->format = SANE_FRAME_GRAY;
        params->bytes_per_line = params->pixels_per_line;
    } else {
        params->format = SANE_FRAME_RGB;
        params->bytes_per_line = params->pixels_per_line * 3;
    }
}

/* Set clipping window
 */
static error
image_decoder_tiff_set_window (image_decoder *decoder, image_window *win)
{
    image_decoder_tiff *tiff = (image_decoder_tiff*) decoder;
// #if     1
    win->x_off = win->y_off = 0;
    TIFFGetField(tiff->tif, TIFFTAG_IMAGEWIDTH, &win->wid);
    TIFFGetField(tiff->tif, TIFFTAG_IMAGELENGTH, &win->hei);
    return NULL;
/*
#else
    int         x_off = win->x_off;
    int         wid = win->wid;

    if (!setjmp(jpeg->jmpb)) {
        jpeg_crop_scanline(&jpeg->cinfo, &x_off, &wid);
        if (win->y_off > 0) {
            jpeg_skip_scanlines(&jpeg->cinfo, win->y_off);
        }

        tiff->num_lines = win->hei;

        win->x_off = x_off;
        win->wid = wid;

        return NULL;
    }
    return ERROR(jpeg->errbuf);
#endif
*/
}

/* Read next line of image
 */
static error
image_decoder_tiff_read_line (image_decoder *decoder, void *buffer)
{
    image_decoder_tiff *tiff = (image_decoder_tiff*) decoder;
    tdata_t buf = (tdata_t) buffer;

    if (tiff->num_lines <= (tiff->current_line + 1)) {
        return ERROR("TIFF: end of file");
    }

    if (TIFFReadScanline(tiff->tif, buf, tiff->current_line, 0) == -1) {
       return ERROR("TIFF: read scanline error");
    }
    tiff->current_line ++;
    return NULL;
}

/* Create TIFF image decoder
 */
image_decoder*
image_decoder_tiff_new (void)
{
    image_decoder_tiff *tiff = g_new0(image_decoder_tiff, 1);

    tiff->decoder.content_type = "image/tiff";
    tiff->decoder.free = image_decoder_tiff_free;
    tiff->decoder.begin = image_decoder_tiff_begin;
    tiff->decoder.reset = image_decoder_tiff_reset;
    tiff->decoder.get_bytes_per_pixel = image_decoder_tiff_get_bytes_per_pixel;
    tiff->decoder.get_params = image_decoder_tiff_get_params;
    tiff->decoder.set_window = image_decoder_tiff_set_window;
    tiff->decoder.read_line = image_decoder_tiff_read_line;
    tiff->mem_file = NULL;
    tiff->offset_file = 0;
    tiff->size_file = 0;
    tiff->current_line = 0;

    return &tiff->decoder;
}

/* vim:ts=8:sw=4:et
 */