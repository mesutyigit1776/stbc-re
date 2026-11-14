/* Texture file decoders: TGA, BMP and DDS to 8-bit RGBA (rows top to bottom). No Python and no platform code. */
#ifndef IMAGE_H
#define IMAGE_H
/* decodes by content (DDS and BMP by their magic, else TGA), not by the file extension, because mods rename files.
   Returns a malloc'd w*h*4 RGBA buffer, or NULL (err says why). */
unsigned char *img_decode(const unsigned char *buf, long len, int *w, int *h, char *err, int errlen);
unsigned char *img_load(const char *path, int *w, int *h, char *err, int errlen);
#endif
