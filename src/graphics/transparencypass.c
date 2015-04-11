#include "graphics/renderer.h"

#include "graphics/context.h"
#include "tgl/tgl.h"

static void trans_free(void *ptr)
{
    (void)ptr;
}

static void trans_update(void *ptr, ilG_rendid id)
{
    (void)ptr, (void)id;
    tgl_check("Unknown");
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    tgl_check("glEnable");
}

static bool trans_build(void *ptr, ilG_rendid id, ilG_renderman *rm, ilG_buildresult *out)
{
    (void)rm, (void)id;
    *out = (ilG_buildresult) {
        .free = trans_free,
        .update = trans_update,
        .draw = NULL,
        .view = NULL,
        .types = NULL,
        .num_types = 0,
        .obj = ptr,
        .name = strdup("Transparency")
    };
    return true;
}

ilG_builder ilG_transparency_builder()
{
    return ilG_builder_wrap(NULL, trans_build);
}
