#include "image.h"

#include <png.h>

#include "util/log.h"

static int check_png(const void *data, size_t size)
{
    (void)size;
    return !png_sig_cmp(data, 0, 8);
}

struct read_context {
    const unsigned char *data;
    size_t size;
    size_t head;
};

static int compute_bpp(int channels) 
{
    return (!!(channels & ILA_IMG_R) +
            !!(channels & ILA_IMG_G) +
            !!(channels & ILA_IMG_B) +
            !!(channels & ILA_IMG_A) ) * 8;
}

static void png_read_fn(png_structp png_ptr, png_bytep data, png_size_t length)
{
    struct read_context *self = (struct read_context*)png_get_io_ptr(png_ptr);
    //printf("read %zu bytes starting at %zu\n", length, self->head);
    memcpy(data, self->data + self->head, length);
    self->head += length;
}

static int read_png(ilA_img *self, const void *data, size_t size)
{
    int ctype = 0, bpp, rowbytes;
    unsigned i;
    png_structp png_ptr;
    png_infop info_ptr;
    struct read_context read_context;
    png_bytepp rows;
        
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL,
        NULL, NULL);
    if (!png_ptr) {
        il_error("Failed to allocate png structure");
        return 0;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
        il_error("Failed to allocate png info structure");
        return 0;
    }

    //png_init_io(png_ptr, NULL);
    read_context = (struct read_context){data, size, 8};
    png_set_read_fn(png_ptr, &read_context, &png_read_fn);
    png_set_sig_bytes(png_ptr, 8);
    png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_SCALE_16 | 
        PNG_TRANSFORM_PACKING, NULL);
    png_get_IHDR(png_ptr, info_ptr, &self->width, &self->height, &bpp, &ctype, 
        NULL, NULL, NULL);
    rowbytes = png_get_rowbytes(png_ptr, info_ptr);

    self->channels = ILA_IMG_R;
    if (ctype & PNG_COLOR_MASK_COLOR) {
        self->channels |= ILA_IMG_RGB;
    }
    if (ctype & PNG_COLOR_MASK_ALPHA) {
        self->channels |= ILA_IMG_A;
    }
    if (ctype & PNG_COLOR_MASK_PALETTE) {
        il_error("Palettes are not handled");
        return 0;
    }
    self->bpp = compute_bpp(self->channels);
    
    self->data = calloc(rowbytes, self->width);
    rows = png_get_rows(png_ptr, info_ptr);
    for (i = 0; i < self->height; i++) {
        memcpy(self->data + rowbytes*i, rows[i], rowbytes);
    }

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    
    return 1;
}

ilA_img *ilA_img_load(const void *data, size_t size)
{
    ilA_img *img = calloc(1, sizeof(ilA_img));
    int res;

    if (check_png(data, size)) { // it is, in fact, a png file
        res = read_png(img, data, size);
        if (res) {
            return img;
        }
    }

    il_error("Failed to load image");
    free(img);
    return NULL;
}

ilA_img *ilA_img_loadasset(const ilA_file *iface, il_base *file)
{
    size_t size;
    void *data = ilA_contents(iface, file, &size);
    return ilA_img_load(data, size);
}

ilA_img *ilA_img_loadfile(const char *name)
{
    const ilA_file *iface;
    ilA_path *path = ilA_path_chars(name);
    il_base *file = ilA_stdiofile(path, ILA_FILE_READ, &iface);
    ilA_path_free(path);
    return ilA_img_loadasset(iface, file);
}

void ilA_img_free(ilA_img *self)
{
    free(self->data);
    free(self);
}

// TODO: stop asserting there are 8 bits per channel
static void sample_pixel(void *dest, const void *src, int bpp, int channels, int desired_bpp, int desired_channels)
{
    unsigned char *ptr = dest;
    const unsigned char *src_ptr = src;
    (void)bpp, (void)desired_bpp;
    if (desired_channels&ILA_IMG_R) {
        if (channels&ILA_IMG_R) {
            *ptr++ = *src_ptr++;
        } else {
            *ptr++ = 0;
        }
    }
    if (desired_channels&ILA_IMG_G) {
        if (channels&ILA_IMG_G) {
            *ptr++ = *src_ptr++;
        } else {
            *ptr++ = 0;
        }
    }
    if (desired_channels&ILA_IMG_B) {
        if (channels&ILA_IMG_B) {
            *ptr++ = *src_ptr++;
        } else {
            *ptr++ = 0;
        }
    }
    if (desired_channels&ILA_IMG_A) {
        if (channels&ILA_IMG_A) {
            *ptr++ = *src_ptr++;
        } else {
            *ptr++ = 0;
        }
    }
}

static void nearest_sample(ilA_img *self, const ilA_img *sample, int x, int y)
{
    float xp = (float)x/self->width, yp = (float)y/self->height;
    int xs = xp * sample->width, ys = yp * sample->height;
    int pix = self->bpp * (y*self->width + x) / 8, spix = sample->bpp * (ys*sample->width + xs) / 8;
    sample_pixel(self->data + pix, sample->data + spix, sample->bpp, sample->channels, self->bpp, self->channels);
}

ilA_img *ilA_img_resize(const ilA_img *self, enum ilA_img_interpolation up, enum ilA_img_interpolation down, unsigned w, unsigned h, int channels)
{
    enum ilA_img_interpolation interp;
    unsigned x, y;
    ilA_img *img = calloc(1, sizeof(ilA_img));

    img->width = w;
    img->height = h;
    img->channels = channels;
    img->bpp = compute_bpp(channels);
    img->data = calloc(img->bpp/8, w*h);
    if (w > self->width && h > self->height) {
        interp = up;
    } else {
        interp = down;
    }
    switch (interp) {
        case ILA_IMG_NEAREST:
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                nearest_sample(img, self, x, y);
            }
        }
        break;
        default:
        il_error("Unknown interpolation algorithm");
    }
    return img;
}
