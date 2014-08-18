#include "transparencypass.h"

#include "common/world.h"
#include "graphics/arrayattrib.h"
#include "graphics/context.h"
#include "graphics/material.h"
#include "graphics/renderer.h"
#include "tgl/tgl.h"
#include "util/array.h"
#include "util/log.h"

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

static bool trans_build(void *ptr, ilG_rendid id, ilG_context *context, ilG_buildresult *out)
{
    (void)context, (void)id;
    *out = (ilG_buildresult) {
        .free = trans_free,
        .update = trans_update,
        .draw = NULL,
        .view = NULL,
        .types = NULL,
        .num_types = 0,
        .obj = ptr
    };
    return true;
}

ilG_builder ilG_transparency_builder()
{
    return ilG_builder_wrap(NULL, trans_build);
}
