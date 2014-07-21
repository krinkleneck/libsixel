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

#include "config.h"
#include "malloc_stub.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#if defined(HAVE_UNISTD_H)
# include <unistd.h>
#elif defined(HAVE_SYS_UNISTD_H)
# include <sys/unistd.h>
#endif

#if defined(HAVE_GETOPT_H)
# include <getopt.h>
#endif

#if defined(HAVE_INTTYPES_H)
# include <inttypes.h>
#endif

#if defined(HAVE_ERRNO_H)
# include <errno.h>
#endif

#if defined(HAVE_SIGNAL_H)
# include <signal.h>
#elif defined(HAVE_SYS_SIGNAL_H)
# include <sys/signal.h>
#endif


#include <sixel.h>
#include "scale.h"
#include "quant.h"
#include "loader.h"


/* loop modes */
enum loopMode {
    LOOP_AUTO,       /* honer the setting of GIF header */
    LOOP_FORCE,      /* always enable loop */
    LOOP_DISABLE,    /* always disable loop */
};


static unsigned char *
prepare_monochrome_palette(finvert)
{
    unsigned char *palette;

    palette = malloc(6);
    if (finvert) {
        palette[0] = 0xff;
        palette[1] = 0xff;
        palette[2] = 0xff;
        palette[3] = 0x00;
        palette[4] = 0x00;
        palette[5] = 0x00;
    } else {
        palette[0] = 0x00;
        palette[1] = 0x00;
        palette[2] = 0x00;
        palette[3] = 0xff;
        palette[4] = 0xff;
        palette[5] = 0xff;
    }

    return palette;
}


static unsigned char *
prepare_specified_palette(char const *mapfile, int reqcolors, int *pncolors)
{
    FILE *f;
    unsigned char *mappixels;
    unsigned char *palette;
    int origcolors;
    int map_sx;
    int map_sy;
    int frame_count;
    int loop_count;
    int delay;

    mappixels = load_image_file(mapfile, &map_sx, &map_sy,
                                &frame_count, &loop_count, &delay);
    if (!mappixels) {
        return NULL;
    }
    palette = LSQ_MakePalette(mappixels, map_sx, map_sy, 3,
                              reqcolors, pncolors, &origcolors,
                              LARGE_NORM, REP_CENTER_BOX, QUALITY_HIGH);
    return palette;
}


#if HAVE_SIGNAL

static volatile int signaled = 0;

static void
signal_handler(int sig)
{
    signaled = sig;
}

#endif

typedef struct Settings {
    int reqcolors;
    char *mapfile;
    int monochrome;
    enum methodForDiffuse method_for_diffuse;
    enum methodForLargest method_for_largest;
    enum methodForRep method_for_rep;
    enum qualityMode quality_mode;
    enum methodForResampling method_for_resampling;
    enum loopMode loop_mode;
    int f8bit;
    int finvert;
    int fuse_macro;
    int fignore_delay;
    int pixelwidth;
    int pixelheight;
    int percentwidth;
    int percentheight;
} settings_t;

typedef struct Frame {
    int sx;
    int sy;
    unsigned char *buffer;
} frame_t;

typedef struct FrameSet {
    int frame_count;
    unsigned char *palette;
    frame_t pframe[1];
} frame_set_t;


static unsigned char *
prepare_palette(unsigned char *frame, int sx, int sy,
                settings_t *psettings,
                int *pncolors, int *porigcolors)
{
    unsigned char *palette;

    if (psettings->monochrome) {
        palette = prepare_monochrome_palette(psettings->finvert);
        *pncolors = 2;
    } else if (psettings->mapfile) {
        palette = prepare_specified_palette(psettings->mapfile,
                                            psettings->reqcolors,
                                            pncolors);
    } else {
        if (psettings->method_for_largest == LARGE_AUTO) {
            psettings->method_for_largest = LARGE_NORM;
        }
        if (psettings->method_for_rep == REP_AUTO) {
            psettings->method_for_rep = REP_CENTER_BOX;
        }
        if (psettings->quality_mode == QUALITY_AUTO) {
            if (psettings->reqcolors <= 8) {
                psettings->quality_mode = QUALITY_HIGH;
            } else {
                psettings->quality_mode = QUALITY_LOW;
            }
        }
        palette = LSQ_MakePalette(frame, sx, sy, 3,
                                  psettings->reqcolors,
                                  pncolors,
                                  porigcolors,
                                  psettings->method_for_largest,
                                  psettings->method_for_rep,
                                  psettings->quality_mode);
        if (*porigcolors <= *pncolors) {
            psettings->method_for_diffuse = DIFFUSE_NONE;
        }
    }
    return palette;
}

static int
printf_hex(char const *fmt, ...)
{
    char buffer[65536];
    int i;

    va_list ap;
    va_start(ap, fmt);
    vsprintf(buffer, fmt, ap);
    va_end(ap);
    for (i = 0; i < strlen(buffer); ++i) {
        printf("%02x", buffer[i]);
    }
    return 1;
}

static int
putchar_hex(int c)
{
    return printf("%02x", c);
}

static int
convert_to_sixel(char const *filename, settings_t *psettings)
{
    unsigned char *pixels = NULL;
    unsigned char *frame = NULL;
    unsigned char **frames = NULL;
    unsigned char *scaled_frame = NULL;
    unsigned char *mappixels = NULL;
    unsigned char *palette = NULL;
    unsigned char *data = NULL;
    unsigned char *p = NULL;
    int ncolors;
    int origcolors;
    LSImagePtr im = NULL;
    LSImagePtr *image_array = NULL;
    LSOutputContextPtr context = NULL;
    int sx, sy;
    int frame_count;
    int loop_count;
    int delay;
    int i;
    int c;
    int n;
    int nret = -1;
    FILE *f;
    int size;

    frame_count = 1;
    loop_count = 1;
    delay = 0;

    if (psettings->reqcolors < 2) {
        psettings->reqcolors = 2;
    } else if (psettings->reqcolors > PALETTE_MAX) {
        psettings->reqcolors = PALETTE_MAX;
    }

    pixels = load_image_file(filename, &sx, &sy,
                             &frame_count, &loop_count, &delay);

    if (pixels == NULL) {
        nret = -1;
        goto end;
    }

    frames = malloc(sizeof(unsigned char *) * frame_count);

    frame = pixels;
    for (n = 0; n < frame_count; ++n) {
        frames[n] = frame;
        frame += sx * sy * 3;
    }

    /* scaling */
    if (psettings->percentwidth > 0) {
        psettings->pixelwidth = sx * psettings->percentwidth / 100;
    }
    if (psettings->percentheight > 0) {
        psettings->pixelheight = sy * psettings->percentheight / 100;
    }
    if (psettings->pixelwidth > 0 && psettings->pixelheight <= 0) {
        psettings->pixelheight = sy * psettings->pixelwidth / sx;
    }
    if (psettings->pixelheight > 0 && psettings->pixelwidth <= 0) {
        psettings->pixelwidth = sx * psettings->pixelheight / sy;
    }

    if (psettings->pixelwidth > 0 && psettings->pixelheight > 0) {
        size = psettings->pixelwidth * psettings->pixelheight * 3;
        p = malloc(size * frame_count);
        for (n = 0; n < frame_count; ++n) {
            scaled_frame = LSS_scale(frames[n], sx, sy, 3,
                                     psettings->pixelwidth,
                                     psettings->pixelheight,
                                     psettings->method_for_resampling);
            memcpy(p + size * n, scaled_frame, size); 
        }
        for (n = 0; n < frame_count; ++n) {
            frames[n] = p + size * n;
        }
        free(pixels);
        pixels = p;
        sx = psettings->pixelwidth;
        sy = psettings->pixelheight;
    }

    /* prepare palette */
    palette = prepare_palette(pixels, sx, sy * frame_count,
                              psettings, 
                              &ncolors, &origcolors);
    if (!palette) {
        nret = -1;
        goto end;
    }

    image_array = malloc(sizeof(LSImagePtr) * frame_count);

    for (n = 0; n < frame_count; ++n) {

        /* apply palette */
        if (psettings->method_for_diffuse == DIFFUSE_AUTO) {
            psettings->method_for_diffuse = DIFFUSE_FS;
        }
        data = LSQ_ApplyPalette(frames[n], sx, sy, 3,
                                palette, ncolors,
                                psettings->method_for_diffuse,
                                /* foptimize */ 1);

        if (!data) {
            nret = -1;
            goto end;
        }

        /* create intermidiate bitmap image */
        im = LSImage_create(sx, sy, 3, ncolors);
        if (!im) {
            nret = -1;
            goto end;
        }
        for (i = 0; i < ncolors; i++) {
            LSImage_setpalette(im, i,
                               palette[i * 3],
                               palette[i * 3 + 1],
                               palette[i * 3 + 2]);
        }
        if (psettings->monochrome) {
            im->keycolor = 0;
        } else {
            im->keycolor = -1;
        }
        LSImage_setpixels(im, data);

        data = NULL;

        image_array[n] = im;
    }

#if HAVE_SIGNAL
# if HAVE_DECL_SIGINT
    signal(SIGINT, signal_handler);
# endif
# if HAVE_DECL_SIGTERM
    signal(SIGTERM, signal_handler);
# endif
# if HAVE_DECL_SIGHUP
    signal(SIGHUP, signal_handler);
# endif
#endif

    switch (psettings->loop_mode) {
    case LOOP_FORCE:
        loop_count = -1;
        break;
    case LOOP_DISABLE:
        loop_count = 1;
        break;
    default:
        if (frame_count == 1) {
            loop_count = 1;
        } else if (loop_count == 0) {
            loop_count = -1;
        }
        break;
    }

    if (psettings->fuse_macro && frame_count > 1) {
        context = LSOutputContext_create(putchar_hex, printf_hex);
        context->has_8bit_control = psettings->f8bit;
        for (n = 0; n < frame_count && n < 64; ++n) {
            printf("\033P%d;0;1!z", n);
            LibSixel_LSImageToSixel(image_array[n], context);
            printf("\033\\");
#if HAVE_SIGNAL
            if (signaled) {
                break;
            }
#endif
        }
        if (signaled) {
            if (psettings->f8bit) {
                printf("\x9c");
            } else {
                printf("\x1b\\");
            }
        }
        for (c = 0; c != loop_count; ++c) {
            for (n = 0; n < frame_count && n < 64; ++n) {
                printf("\033[H");
                printf("\033[%d*z", n);
                fflush(0);
#if HAVE_USLEEP
                if (!psettings->fignore_delay) {
                    usleep(10000 * delay);
                }
#endif
#if HAVE_SIGNAL
                if (signaled) {
                    break;
                }
#endif
            }
#if HAVE_SIGNAL
            if (signaled) {
                break;
            }
#endif
        }
    } else {
        /* create output context */
        context = LSOutputContext_create(putchar, printf);
        context->has_8bit_control = psettings->f8bit;
        for (c = 0; c != loop_count; ++c) {
            for (n = 0; n < frame_count; ++n) {

                if (frame_count > 1) {
                    context->fn_printf("\033[H");
                }

                /* convert image object into sixel */
                LibSixel_LSImageToSixel(image_array[n], context);

#if HAVE_SIGNAL
                if (signaled) {
                    break;
                }
#endif
#if HAVE_USLEEP
                if (!psettings->fignore_delay) {
                    usleep(10000 * delay);
                }
#endif
            }
#if HAVE_SIGNAL
            if (signaled) {
                break;
            }
#endif
        }
        if (signaled) {
            if (context->has_8bit_control) {
                context->fn_printf("\x9c");
            } else {
                context->fn_printf("\x1b\\");
            }
        }
    }

    nret = 0;

end:
    if (frames) {
        free(frames);
    }
    if (data) {
        free(data);
    }
    if (pixels) {
        free(pixels);
    }
    if (scaled_frame) {
        free(scaled_frame);
    }
    if (mappixels) {
        free(mappixels);
    }
    if (palette) {
        LSQ_FreePalette(palette);
    }
    if (image_array) {
        for (n = 0; n < frame_count; ++n) {
            LSImage_destroy(image_array[n]);
        }
        free(image_array);
    }
    if (context) {
        LSOutputContext_destroy(context);
    }
    return nret;
}


int main(int argc, char *argv[])
{
    int n;
    int filecount = 1;
    int long_opt;
#if HAVE_GETOPT_LONG
    int option_index;
#endif  /* HAVE_GETOPT_LONG */
    int ret;
    int exit_code;
    int number;
    char unit[32];
    int parsed;
    char const *optstring = "78p:m:ed:f:s:w:h:r:q:il:ug";
    settings_t settings;

    settings.mapfile = NULL;
    settings.method_for_resampling = RES_BILINEAR;
    settings.method_for_diffuse = DIFFUSE_AUTO;
    settings.method_for_largest = LARGE_AUTO;
    settings.method_for_rep = REP_AUTO;
    settings.quality_mode = QUALITY_AUTO;
    settings.loop_mode = LOOP_AUTO;
    settings.reqcolors = -1;
    settings.f8bit = 0;
    settings.finvert = 0;
    settings.fuse_macro = 0;
    settings.fignore_delay = 0;
    settings.monochrome = 0;
    settings.pixelwidth = -1;
    settings.pixelheight = -1;
    settings.percentwidth = -1;
    settings.percentheight = -1;

#if HAVE_GETOPT_LONG
    struct option long_options[] = {
        {"7bit-mode",    no_argument,        &long_opt, '7'},
        {"8bit-mode",    no_argument,        &long_opt, '8'},
        {"colors",       required_argument,  &long_opt, 'p'},
        {"mapfile",      required_argument,  &long_opt, 'm'},
        {"monochrome",   no_argument,        &long_opt, 'e'},
        {"diffusion",    required_argument,  &long_opt, 'd'},
        {"find-largest", required_argument,  &long_opt, 'f'},
        {"select-color", required_argument,  &long_opt, 's'},
        {"width",        required_argument,  &long_opt, 'w'},
        {"height",       required_argument,  &long_opt, 'h'},
        {"resampling",   required_argument,  &long_opt, 'r'},
        {"quality",      required_argument,  &long_opt, 'q'},
        {"invert",       no_argument,        &long_opt, 'i'},
        {"loop-control", required_argument,  &long_opt, 'l'},
        {"use-macro",    no_argument,        &long_opt, 'u'},
        {"ignore-delay", no_argument,        &long_opt, 'g'},
        {0, 0, 0, 0}
    };
#endif  /* HAVE_GETOPT_LONG */

    for (;;) {

#if HAVE_GETOPT_LONG
        n = getopt_long(argc, argv, optstring,
                        long_options, &option_index);
#else
        n = getopt(argc, argv, optstring);
#endif  /* HAVE_GETOPT_LONG */
        if (n == -1) {
            break;
        }
        if (n == 0) {
            n = long_opt;
        }
        switch(n) {
        case '7':
            settings.f8bit = 0;
            break;
        case '8':
            settings.f8bit = 1;
            break;
        case 'p':
            settings.reqcolors = atoi(optarg);
            break;
        case 'm':
            settings.mapfile = strdup(optarg);
            break;
        case 'e':
            settings.monochrome = 1;
            break;
        case 'd':
            /* parse --diffusion option */
            if (optarg) {
                if (strcmp(optarg, "auto") == 0) {
                    settings.method_for_diffuse = DIFFUSE_AUTO;
                } else if (strcmp(optarg, "none") == 0) {
                    settings.method_for_diffuse = DIFFUSE_NONE;
                } else if (strcmp(optarg, "fs") == 0) {
                    settings.method_for_diffuse = DIFFUSE_FS;
                } else if (strcmp(optarg, "atkinson") == 0) {
                    settings.method_for_diffuse = DIFFUSE_ATKINSON;
                } else if (strcmp(optarg, "jajuni") == 0) {
                    settings.method_for_diffuse = DIFFUSE_JAJUNI;
                } else if (strcmp(optarg, "stucki") == 0) {
                    settings.method_for_diffuse = DIFFUSE_STUCKI;
                } else if (strcmp(optarg, "burkes") == 0) {
                    settings.method_for_diffuse = DIFFUSE_BURKES;
                } else {
                    fprintf(stderr,
                            "Diffusion method '%s' is not supported.\n",
                            optarg);
                    goto argerr;
                }
            }
            break;
        case 'f':
            /* parse --find-largest option */
            if (optarg) {
                if (strcmp(optarg, "auto") == 0) {
                    settings.method_for_largest = LARGE_AUTO;
                } else if (strcmp(optarg, "norm") == 0) {
                    settings.method_for_largest = LARGE_NORM;
                } else if (strcmp(optarg, "lum") == 0) {
                    settings.method_for_largest = LARGE_LUM;
                } else {
                    fprintf(stderr,
                            "Finding method '%s' is not supported.\n",
                            optarg);
                    goto argerr;
                }
            }
            break;
        case 's':
            /* parse --select-color option */
            if (optarg) {
                if (strcmp(optarg, "auto") == 0) {
                    settings.method_for_rep = REP_AUTO;
                } else if (strcmp(optarg, "center") == 0) {
                    settings.method_for_rep = REP_CENTER_BOX;
                } else if (strcmp(optarg, "average") == 0) {
                    settings.method_for_rep = REP_AVERAGE_COLORS;
                } else if (strcmp(optarg, "histgram") == 0) {
                    settings.method_for_rep = REP_AVERAGE_PIXELS;
                } else {
                    fprintf(stderr,
                            "Finding method '%s' is not supported.\n",
                            optarg);
                    goto argerr;
                }
            }
            break;
        case 'w':
            parsed = sscanf(optarg, "%d%s", &number, unit);
            if (parsed == 2 && strcmp(unit, "%") == 0) {
                settings.pixelwidth = -1;
                settings.percentwidth = number;
            } else if (parsed == 1 || (parsed == 2 && strcmp(unit, "px") == 0)) {
                settings.pixelwidth = number;
                settings.percentwidth = -1;
            } else if (strcmp(optarg, "auto") == 0) {
                settings.pixelwidth = -1;
                settings.percentwidth = -1;
            } else {
                fprintf(stderr,
                        "Cannot parse -w/--width option.\n");
                goto argerr;
            }
            break;
        case 'h':
            parsed = sscanf(optarg, "%d%s", &number, unit);
            if (parsed == 2 && strcmp(unit, "%") == 0) {
                settings.pixelheight = -1;
                settings.percentheight = number;
            } else if (parsed == 1 || (parsed == 2 && strcmp(unit, "px") == 0)) {
                settings.pixelheight = number;
                settings.percentheight = -1;
            } else if (strcmp(optarg, "auto") == 0) {
                settings.pixelheight = -1;
                settings.percentheight = -1;
            } else {
                fprintf(stderr,
                        "Cannot parse -h/--height option.\n");
                goto argerr;
            }
            break;
        case 'r':
            /* parse --resampling option */
            if (!optarg) {  /* default */
                settings.method_for_resampling = RES_BILINEAR;
            } else if (strcmp(optarg, "nearest") == 0) {
                settings.method_for_resampling = RES_NEAREST;
            } else if (strcmp(optarg, "gaussian") == 0) {
                settings.method_for_resampling = RES_GAUSSIAN;
            } else if (strcmp(optarg, "hanning") == 0) {
                settings.method_for_resampling = RES_HANNING;
            } else if (strcmp(optarg, "hamming") == 0) {
                settings.method_for_resampling = RES_HAMMING;
            } else if (strcmp(optarg, "bilinear") == 0) {
                settings.method_for_resampling = RES_BILINEAR;
            } else if (strcmp(optarg, "welsh") == 0) {
                settings.method_for_resampling = RES_WELSH;
            } else if (strcmp(optarg, "bicubic") == 0) {
                settings.method_for_resampling = RES_BICUBIC;
            } else if (strcmp(optarg, "lanczos2") == 0) {
                settings.method_for_resampling = RES_LANCZOS2;
            } else if (strcmp(optarg, "lanczos3") == 0) {
                settings.method_for_resampling = RES_LANCZOS3;
            } else if (strcmp(optarg, "lanczos4") == 0) {
                settings.method_for_resampling = RES_LANCZOS4;
            } else {
                fprintf(stderr,
                        "Resampling method '%s' is not supported.\n",
                        optarg);
                goto argerr;
            }
            break;
        case 'q':
            /* parse --quality option */
            if (optarg) {
                if (strcmp(optarg, "auto") == 0) {
                    settings.quality_mode = QUALITY_AUTO;
                } else if (strcmp(optarg, "high") == 0) {
                    settings.quality_mode = QUALITY_HIGH;
                } else if (strcmp(optarg, "hanning") == 0) {
                    settings.quality_mode = QUALITY_LOW;
                } else {
                    fprintf(stderr,
                            "Cannot parse quality option.\n");
                    goto argerr;
                }
            }
            break;
        case 'l':
            /* parse --loop-control option */
            if (optarg) {
                if (strcmp(optarg, "auto") == 0) {
                    settings.loop_mode = LOOP_AUTO;
                } else if (strcmp(optarg, "force") == 0) {
                    settings.loop_mode = LOOP_FORCE;
                } else if (strcmp(optarg, "disable") == 0) {
                    settings.loop_mode = LOOP_DISABLE;
                } else {
                    fprintf(stderr,
                            "Cannot parse loop-control option.\n");
                    goto argerr;
                }
            }
            break;
        case 'i':
            settings.finvert = 1;
            break;
        case 'u':
            settings.fuse_macro = 1;
            break;
        case 'g':
            settings.fignore_delay = 1;
            break;
        case '?':
            goto argerr;
        default:
            goto argerr;
        }
    }
    if (settings.reqcolors != -1 && settings.mapfile) {
        fprintf(stderr, "option -p, --colors conflicts "
                        "with -m, --mapfile.\n");
        goto argerr;
    }
    if (settings.mapfile && settings.monochrome) {
        fprintf(stderr, "option -m, --mapfile conflicts "
                        "with -e, --monochrome.\n");
        goto argerr;
    }
    if (settings.monochrome && settings.reqcolors != -1) {
        fprintf(stderr, "option -e, --monochrome conflicts"
                        " with -p, --colors.\n");
        goto argerr;
    }

    if (settings.reqcolors == -1) {
        settings.reqcolors = PALETTE_MAX;
    }

    if (optind == argc) {
        ret = convert_to_sixel(NULL, &settings);
        if (ret != 0) {
            exit_code = EXIT_FAILURE;
            goto end;
        }
    } else {
        for (n = optind; n < argc; n++) {
            ret = convert_to_sixel(argv[n], &settings);
            if (ret != 0) {
                exit_code = EXIT_FAILURE;
                goto end;
            }
        }
    }
    exit_code = EXIT_SUCCESS;
    goto end;

argerr:
    exit_code = EXIT_FAILURE;
    fprintf(stderr,
            "Usage: img2sixel [Options] imagefiles\n"
            "       img2sixel [Options] < imagefile\n"
            "\n"
            "Options:\n"
            "-7, --7bit-mode            generate a sixel image for 7bit\n"
            "                           terminals or printers (default)\n"
            "-8, --8bit-mode            generate a sixel image for 8bit\n"
            "                           terminals or printers\n"
            "-p COLORS, --colors=COLORS specify number of colors to reduce\n"
            "                           the image to (default=256)\n"
            "-m FILE, --mapfile=FILE    transform image colors to match this\n"
            "                           set of colorsspecify map\n"
            "-e, --monochrome           output monochrome sixel image\n"
            "                           this option assumes the terminal \n"
            "                           background color is black\n"
            "-i, --invert               assume the terminal background color\n"
            "                           is white, make sense only when -e\n"
            "                           option is given\n"
            "-u, --use-macro            use DECDMAC and DEVINVM sequences to\n"
            "                           optimize GIF animation rendering\n"
            "-g, --ignore-delay         render GIF animation without delay\n"
            "-d DIFFUSIONTYPE, --diffusion=DIFFUSIONTYPE\n"
            "                           choose diffusion method which used\n"
            "                           with -p option (color reduction)\n"
            "                           DIFFUSIONTYPE is one of them:\n"
            "                             auto     -> choose diffusion type\n"
            "                                         automatically (default)\n"
            "                             none     -> do not diffuse\n"
            "                             fs       -> Floyd-Steinberg method\n"
            "                             atkinson -> Bill Atkinson's method\n"
            "                             jajuni   -> Jarvis, Judice & Ninke\n"
            "                             stucki   -> Stucki's method\n"
            "                             burkes   -> Burkes' method\n"
            "-f FINDTYPE, --find-largest=FINDTYPE\n"
            "                           choose method for finding the largest\n"
            "                           dimention of median cut boxes for\n"
            "                           splitting, make sense only when -p\n"
            "                           option (color reduction) is\n"
            "                           specified\n"
            "                           FINDTYPE is one of them:\n"
            "                             auto -> choose finding method\n"
            "                                     automatically (default)\n"
            "                             norm -> simply comparing the\n"
            "                                     range in RGB space\n"
            "                             lum  -> transforming into\n"
            "                                     luminosities before the\n"
            "                                     comparison\n"
            "-s SELECTTYPE, --select-color=SELECTTYPE\n"
            "                           choose the method for selecting\n"
            "                           representative color from each\n"
            "                           median-cut box, make sense only\n"
            "                           when -p option (color reduction) is\n"
            "                           specified\n"
            "                           SELECTTYPE is one of them:\n"
            "                             auto     -> choose selecting\n"
            "                                         method automatically\n"
            "                                         (default)\n"
            "                             center   -> choose the center of\n"
            "                                         the box\n"
            "                             average  -> caclulate the color\n"
            "                                         average into the box\n"
            "                             histgram -> similar with average\n"
            "                                         but considers color\n"
            "                                         histgram\n"
            "-w WIDTH, --width=WIDTH    resize image to specific width\n"
            "                           WIDTH is represented by the\n"
            "                           following syntax\n"
            "                             auto       -> preserving aspect\n"
            "                                           ratio (default)\n"
            "                             <number>%%  -> scale width with\n"
            "                                           given percentage\n"
            "                             <number>   -> scale width with\n"
            "                                           pixel counts\n"
            "                             <number>px -> scale width with\n"
            "                                           pixel counts\n"
            "-h HEIGHT, --height=HEIGHT resize image to specific height\n"
            "                           HEIGHT is represented by the\n"
            "                           following syntax\n"
            "                             auto       -> preserving aspect\n"
            "                                           ratio (default)\n"
            "                             <number>%%  -> scale height with\n"
            "                                           given percentage\n"
            "                             <number>   -> scale height with\n"
            "                                           pixel counts\n"
            "                             <number>px -> scale height with\n"
            "                                           pixel counts\n"
            "-r RESAMPLINGTYPE, --resampling=RESAMPLINGTYPE\n"
            "                           choose resampling filter used\n"
            "                           with -w or -h option (scaling)\n"
            "                           RESAMPLINGTYPE is one of them:\n"
            "                             nearest  -> Nearest-Neighbor\n"
            "                                         method\n"
            "                             gaussian -> Gaussian filter\n"
            "                             hanning  -> Hanning filter\n"
            "                             hamming  -> Hamming filter\n"
            "                             bilinear -> Bilinear filter\n"
            "                                         (default)\n"
            "                             welsh    -> Welsh filter\n"
            "                             bicubic  -> Bicubic filter\n"
            "                             lanczos2 -> Lanczos-2 filter\n"
            "                             lanczos3 -> Lanczos-3 filter\n"
            "                             lanczos4 -> Lanczos-4 filter\n"
            "-q QUALITYMODE, --quality=QUALITYMODE\n"
            "                           select quality of color\n"
            "                           quanlization.\n"
            "                             auto -> decide quality mode\n"
            "                                     automatically (default)\n"
            "                             high -> high quality and low\n"
            "                                     speed mode\n"
            "                             low  -> low quality and high\n"
            "                                     speed mode\n"
            "-l LOOPMODE, --loop-control=LOOPMODE\n"
            "                           select loop control mode for GIF\n"
            "                           animation.\n"
            "                             auto   -> honor the setting of\n"
            "                                       GIF header (default)\n"
            "                             force   -> always enable loop\n"
            "                             disable -> always disable loop\n"
            );

end:
    if (settings.mapfile) {
        free(settings.mapfile);
    }
    return exit_code;
}

/* emacs, -*- Mode: C; tab-width: 4; indent-tabs-mode: nil -*- */
/* vim: set expandtab ts=4 : */
/* EOF */
