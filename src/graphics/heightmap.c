#include "heightmap.h"

#include "graphics/context.h"
#include "graphics/drawable3d.h"
#include "graphics/material.h"
#include "asset/mesh.h"
#include "graphics/fragdata.h"
#include "graphics/arrayattrib.h"
#include "graphics/mesh.h"
#include "graphics/tex.h"
#include "graphics/renderer.h"
#include "util/log.h"

typedef struct ilG_heightmap {
    ilG_tex height, normal, color;
    ilG_material *shader;
    ilA_mesh *mesh;
    ilG_drawable3d *drawable;
    ilG_context *context;
    unsigned w,h;
} ilG_heightmap;

static void heightmap_free(void *ptr, ilG_rendid id)
{
    (void) id;
    ilG_heightmap *self = ptr;
    il_unref(self->shader);
    free(self);
}

static void heightmap_draw(void *ptr, ilG_rendid id)
{
    ilG_testError("Unknown");
    ilG_heightmap *self = ptr;
    ilG_tex_bind(&self->height);
    ilG_tex_bind(&self->normal);
    ilG_tex_bind(&self->color);
    ilG_bindable_bind(&ilG_mesh_bindable, self->drawable);
    ilG_bindable_bind(&ilG_material_bindable, self->shader);
    il_positionable *pos;
    unsigned iter;
    while ((pos = ilG_context_iterPositionables(self->context, id, &iter))) {
        self->context->positionable = pos;
        ilG_bindable_action(&ilG_material_bindable, self->shader);
        ilG_bindable_action(&ilG_mesh_bindable, self->drawable);
    }
    ilG_testError("heightmap_draw");
}

static bool heightmap_build(void *ptr, ilG_rendid id, ilG_context *context, ilG_renderer *out)
{
    ilG_heightmap *self = ptr;
    self->context = context;
    ilG_tex_build(&self->height, context);
    ilG_tex_build(&self->normal, context);
    ilG_tex_build(&self->color, context);
    if (ilG_material_link(self->shader, context)) {
        return false;
    }
    self->drawable = ilG_mesh(self->mesh, context);
    ilA_mesh_free(self->mesh);
    *out = (ilG_renderer) {
        .id = id,
        .free = heightmap_free,
        .draw = heightmap_draw,
        .obj = self
    };
    return true;
}

static void width_uniform(ilG_material *mtl, GLint location, void *user)
{
    (void)mtl;
    ilG_heightmap *self = user;
    glUniform2f(location, self->w, self->h);
}

ilG_builder ilG_heightmap_builder(unsigned w, unsigned h, ilG_tex height, ilG_tex normal, ilG_tex color)
{
    ilA_mesh *mesh = ilA_mesh_new(ILA_MESH_POSITION|ILA_MESH_TEXCOORD, w * h * 6);
    unsigned x, y, i = 0, j;
    
    mesh->mode = ILA_MESH_TRIANGLES;
    for (y = 1; y < h; y++) {
        for (x = 1; x < w; x++) {
            float pos[] = {
                x,  y,  0, 1,
                x,  y-1,0, 1,
                x-1,y,  0, 1,

                x,  y-1,0, 1,
                x-1,y-1,0, 1,
                x-1,y,  0, 1,
            };
            for (j = 0; j < 24; j += 4) {
                pos[j] /= w;
            }
            for (j = 1; j < 24; j += 4) {
                pos[j] /= h;
            }
            memcpy(mesh->position + i, pos, sizeof(pos));
            float tex[] = {
                x, y, 0, 0,
                x, y, 0, 0,
                x, y, 0, 0,

                x, y, 0, 0,
                x, y, 0, 0,
                x, y, 0, 0,
            };
            memcpy(mesh->texcoord + i, tex, sizeof(tex));
            i += 6;
        }
    }

    ilG_heightmap *self = calloc(1, sizeof(ilG_heightmap));
    self->w = w;
    self->h = h;
    height.unit = 0;
    normal.unit = 1;
    color.unit = 2;
    self->height = height;
    self->normal = normal;
    self->color = color;
    self->mesh = mesh;

    ilG_material *mat = self->shader = ilG_material_new();
    ilG_material_vertex_file(mat, "heightmap.vert");
    ilG_material_fragment_file(mat, "heightmap.frag");
    ilG_material_name(mat, "Heightmap Shader");
    ilG_material_arrayAttrib(mat, ILG_ARRATTR_POSITION, "in_Position");
    ilG_material_arrayAttrib(mat, ILG_ARRATTR_TEXCOORD, "in_Texcoord");
    ilG_material_textureUnit(mat, 0, "height_tex");
    ilG_material_textureUnit(mat, 1, "normal_tex");
    ilG_material_textureUnit(mat, 2, "ambient_tex");
    ilG_material_fragData(mat, ILG_FRAGDATA_ACCUMULATION, "out_Ambient");
    ilG_material_fragData(mat, ILG_FRAGDATA_NORMAL, "out_Normal");
    ilG_material_fragData(mat, ILG_FRAGDATA_DIFFUSE, "out_Diffuse");
    ilG_material_fragData(mat, ILG_FRAGDATA_SPECULAR, "out_Specular");
    ilG_material_matrix(mat, ILG_MVP, "mvp");
    ilG_material_matrix(mat, ILG_INVERSE | ILG_MODEL | ILG_TRANSPOSE, "imt");
    ilG_material_bindFunc(mat, width_uniform, self, "size");

    return ilG_builder_wrap(self, heightmap_build);
}

