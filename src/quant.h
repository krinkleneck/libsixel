/*
 * Copyright (c) 2014 Hayaki Saito
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef LIBSQUANT_H
#define LIBSQUANT_H

/* method for finding the largest dimention for splitting, and sorting by that component */
enum methodForLargest {
    LARGE_NORM,  /* simply comparing the range in RGB space */
    LARGE_LUM    /* transforming into luminosities before the comparison */
};

/* method for choosing a color from the box */
enum methodForRep {
    REP_CENTER_BOX,     /* choose the center of the box */
    REP_AVERAGE_COLORS, /* choose the average all the color in the box (specified in Heckbert's paper) */
    REP_AVERAGE_PIXELS  /* choose the averate all the pixels in the box */
};

/* method for dithering */
enum methodForDiffuse {
    DIFFUSE_NONE        /* don't diffuse */,
    DIFFUSE_FS,         /* diffuse with Floyd-Steinberg method */
    DIFFUSE_JAJUNI      /* diffuse with Jarvis, Judice & Ninke method */
};


#ifdef __cplusplus
extern "C" {
#endif

/* exported functions */

/* the palette manipulation API */

extern unsigned char *
LSQ_MakePalette(unsigned char *data,
                int x,
                int y,
                int depth,
                int reqcolors,
                int *ncolors);


extern unsigned char *
LSQ_ApplyPalette(unsigned char *data,
                 int x,
                 int y,
                 int depth,
                 uint8_t *palette,
                 int ncolors,
                 enum methodForRep const methodForRep,
                 enum methodForDiffuse const methodForDiffuse);


extern void
LSQ_FreePalette(unsigned char * data);

#ifdef __cplusplus
}
#endif

#endif /* LIBSQUANT_H */

/* emacs, -*- Mode: C; tab-width: 4; indent-tabs-mode: nil -*- */
/* vim: set expandtab ts=4 : */
/* EOF */