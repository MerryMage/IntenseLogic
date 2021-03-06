#include "graphics/renderer.h"

#include <assert.h>

#include "graphics/material.h"
#include "graphics/transform.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "tgl/tgl.h"
#include "util/array.h"
#include "util/log.h"

enum {
    TEX_DEPTH,
    TEX_NORMAL,
    TEX_ALBEDO,
    TEX_REFRACTION,
    TEX_GLOSS
};

void ilG_lighting_free(ilG_lighting *lighting)
{
    ilG_renderman_delMaterial(lighting->rm, lighting->mat);
    tgl_vao_free(&lighting->vao);
    tgl_quad_free(&lighting->quad);
}

void ilG_lighting_draw(ilG_lighting *lighting, const il_mat *ivp, const il_mat *mv,
                       const il_mat *vp, const ilG_light *lights, size_t count)
{
    ilG_material *mat = ilG_renderman_findMaterial(lighting->rm, lighting->mat);
    const bool point = lighting->type == ILG_POINT;
    ilG_material_bind(mat);
    glActiveTexture(GL_TEXTURE0 + TEX_DEPTH);
    tgl_fbo_bindTex(lighting->gbuffer, ILG_GBUFFER_DEPTH);
    glActiveTexture(GL_TEXTURE0 + TEX_NORMAL);
    tgl_fbo_bindTex(lighting->gbuffer, ILG_GBUFFER_NORMAL);
    glActiveTexture(GL_TEXTURE0 + TEX_ALBEDO);
    tgl_fbo_bindTex(lighting->gbuffer, ILG_GBUFFER_ALBEDO);
    glActiveTexture(GL_TEXTURE0 + TEX_REFRACTION);
    tgl_fbo_bindTex(lighting->gbuffer, ILG_GBUFFER_REFRACTION);
    glActiveTexture(GL_TEXTURE0 + TEX_GLOSS);
    tgl_fbo_bindTex(lighting->gbuffer, ILG_GBUFFER_GLOSS);
    tgl_fbo_bind(lighting->accum, TGL_FBO_RW);
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    if (point) {
        //glEnable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        glCullFace(GL_FRONT);
        ilG_shape_bind(lighting->ico);
    } else {
        glDisable(GL_CULL_FACE);
        tgl_vao_bind(&lighting->vao);
    }
    tgl_check("Unknown");

    for (unsigned i = 0; i < count; i++) {
        ilG_material_bindMatrix(mat, lighting->ivp_loc, ivp[i]);
        ilG_material_bindMatrix(mat, lighting->mv_loc,  mv[i]);
        ilG_material_bindMatrix(mat, lighting->mvp_loc, vp[i]);
        glUniform2f(lighting->size_loc, (GLfloat)lighting->width, (GLfloat)lighting->height);
        const ilG_light *l = &lights[i];
        il_vec3 col = l->color;
        glUniform3f(lighting->color_loc, col.x, col.y, col.z);
        glUniform1f(lighting->radius_loc, l->radius);
        glUniform1f(lighting->fovsquared_loc, lighting->fovsquared);
        if (point) {
            ilG_shape_draw(lighting->ico);
        } else {
            tgl_quad_draw_once(&lighting->quad);
        }
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

bool ilG_lighting_build(ilG_lighting *lighting, ilG_renderman *rm, ilG_shape *ico,
                        ilG_light_type type, bool msaa, char **error)
{
    memset(lighting, 0, sizeof(*lighting));
    lighting->rm = rm;
    lighting->ico = ico;

    const char *file = type == ILG_POINT? "light.vert" : "id2d.vert";

    ilG_material m;
    ilG_material_init(&m);
    ilG_material_name(&m, "Deferred Lighting Shader");
    ilG_material_arrayAttrib(&m, 0, "in_Position");
    ilG_material_textureUnit(&m, TEX_DEPTH, "depth");
    ilG_material_textureUnit(&m, TEX_NORMAL, "normal");
    ilG_material_textureUnit(&m, TEX_ALBEDO, "albedo");
    ilG_material_textureUnit(&m, TEX_REFRACTION, "refraction");
    ilG_material_textureUnit(&m, TEX_GLOSS, "gloss");
    ilG_material_fragData(&m, 0, "out_Color");
    if (!ilG_renderman_addMaterialFromFile(rm, m, file,
                                           msaa? "light_msaa.frag" : "light.frag",
                                           &lighting->mat, error)) {
        return false;
    }
    GLuint prog = ilG_renderman_findMaterial(rm, lighting->mat)->program;
    lighting->color_loc     = glGetUniformLocation(prog, "color");
    lighting->radius_loc    = glGetUniformLocation(prog, "radius");
    lighting->mvp_loc       = glGetUniformLocation(prog, "mvp");
    lighting->mv_loc        = glGetUniformLocation(prog, "mv");
    lighting->ivp_loc       = glGetUniformLocation(prog, "ivp");
    lighting->size_loc      = glGetUniformLocation(prog, "size");
    lighting->fovsquared_loc= glGetUniformLocation(prog, "fovsquared");

    tgl_vao_init(&lighting->vao);
    tgl_vao_bind(&lighting->vao);
    tgl_quad_init(&lighting->quad, 0);

    return true;
}
