#ifndef SHANKPIT_PNG_DECODE_H
#define SHANKPIT_PNG_DECODE_H

// png_decode.h -- the real, native entry point into png_decode_gen.c (PARENA-compiled, see that
// file's own "do not edit by hand" header), real RFC 1951 DEFLATE inflate underneath
// (compress/inflate.prn) plus real PNG chunk parsing + scanline defiltering (image/png.prn). See
// EMILY/BACKLOG.md S459-33 for the full story, including a real, found-and-fixed VS0 compiler
// codegen bug this needed first (a Vec box-type confusion that silently corrupted dynamic-Huffman
// code-length tables on some real inputs).
//
// Real, honest v0 scope, matching image/png.prn's own header comment: 8-bit depth, non-interlaced,
// color type 2 (RGB) or 6 (RGBA) only -- `ok` is 0 for anything else (paletted/grayscale/16-bit/
// interlaced), a real, graceful "couldn't decode this one" signal, never garbage pixels.

#include "parena_runtime.h"

typedef struct {
    int width;
    int height;
    Bytes pixels; /* always RGBA, width*height*4 bytes, regardless of the source PNG's own color type */
    int ok;
} PngImage;

PngImage png_decode(Bytes data, Arena *dest);

#endif
