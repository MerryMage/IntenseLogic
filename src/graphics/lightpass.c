#include "lightpass.h"

#include "graphics/light.h"
#include "graphics/context.h"
#include "graphics/bindable.h"
#include "graphics/material.h"
#include "graphics/shape.h"
#include "graphics/fragdata.h"
#include "graphics/arrayattrib.h"
#include "graphics/renderer.h"
#include "graphics/drawable3d.h"
#include "math/vector.h"
#include "math/matrix.h"
#include "util/log.h"

typedef struct ilG_lights {
    ilG_context *context;
    GLuint vao, vbo, ibo, lights_ubo, lights_index, mvp_ubo, mvp_index;
    GLint lights_size, mvp_size, lights_offset[3], mvp_offset[1], position_loc, color_loc, radius_loc;
    ilG_material* material;
    int invalidated;
} ilG_lights;

static void lights_free(void *ptr, ilG_rendid id)
{
    (void)id;
    ilG_lights *self = ptr;
    glDeleteVertexArrays(1, &self->vao);
    glDeleteBuffers(1, &self->vbo);
    glDeleteBuffers(1, &self->ibo);
    il_unref(self->material);
    free(self);
}

static void size_uniform(ilG_material *self, GLint location, void *user)
{
    (void)self;
    ilG_lights *lights = user;
    glUniform2f(location, lights->context->width, lights->context->height);
}

static void lights_draw(void *ptr, ilG_rendid id)
{ 
    ilG_lights *self = ptr;
    ilG_context *context = self->context;
    ilG_testError("Unknown");
    /*if (context->lightdata.invalidated) {
        if (!context->lightdata.created) {
            context->lightdata.lights_index = glGetUniformBlockIndex(context->material->program, "LightBlock");
            context->lightdata.mvp_index = glGetUniformBlockIndex(context->material->program, "MVPBlock");
            glGetActiveUniformBlockiv(context->material->program, context->lightdata.lights_index, GL_UNIFORM_BLOCK_DATA_SIZE, &context->lightdata.lights_size);
            glGetActiveUniformBlockiv(context->material->program, context->lightdata.mvp_index, GL_UNIFORM_BLOCK_DATA_SIZE, &context->lightdata.mvp_size);
            //GLubyte *lights_buf = malloc(lights_size), *mvp_buf = malloc(mvp_size);
            const GLchar *lights_names[] = {
                "position",
                "color",
                "radius"
            }, *mvp_names[] = {
                "mvp"
            };
            GLuint lights_indices[3], mvp_indices[1];
            glGetUniformIndices(context->material->program, 3, lights_names, lights_indices);
            glGetUniformIndices(context->material->program, 1, mvp_names, mvp_indices);
            glGetActiveUniformsiv(context->material->program, 3, lights_indices, GL_UNIFORM_OFFSET, context->lightdata.lights_offset);
            glGetActiveUniformsiv(context->material->program, 1, mvp_indices, GL_UNIFORM_OFFSET, context->lightdata.mvp_offset);
            context->lightdata.created = 1;
        }
        if (context->lightdata.lights_ubo) {
            glDeleteBuffers(1, &context->lightdata.lights_ubo);
            glDeleteBuffers(1, &context->lightdata.mvp_ubo);
        }
        glGenBuffers(1, &context->lightdata.lights_ubo);
        glGenBuffers(1, &context->lightdata.mvp_ubo);
        glBindBuffer(GL_UNIFORM_BUFFER, context->lightdata.lights_ubo);
        GLubyte *lights_buf = calloc(1, context->lightdata.lights_size);        
        unsigned int i;
        for (i = 0; i < context->lights.length; i++) {
            ((il_Vector3*)lights_buf + context->lightdata.lights_offset[0])[i] = context->lights.data[i]->positionable->position;
            ((il_Vector3*)lights_buf + context->lightdata.lights_offset[1])[i] = context->lights.data[i]->color;
            ((float*)lights_buf + context->lightdata.lights_offset[2])[i] = context->lights.data[i]->radius;
        }
        glBufferData(GL_UNIFORM_BUFFER, context->lightdata.lights_size, lights_buf, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, context->lightdata.mvp_ubo);
        free(lights_buf);
        GLubyte *mvp_buf = calloc(1, context->lightdata.mvp_size);
        for (i = 0; i < context->lights.length; i++) {
            ((il_Matrix*)mvp_buf + context->lightdata.mvp_offset[0])[i] = 
                il_Matrix_transpose(ilG_computeMVP(context->camera, context->lights.data[i]->positionable));
        }
        glBufferData(GL_UNIFORM_BUFFER, context->lightdata.mvp_size, mvp_buf, GL_DYNAMIC_DRAW);
        free(mvp_buf);
        context->lightdata.invalidated = 0;
    }*/
    //glBindBufferBase(GL_UNIFORM_BUFFER, context->lightdata.lights_index, context->lightdata.lights_ubo);
    //glBindBufferBase(GL_UNIFORM_BUFFER, context->lightdata.mvp_index, context->lightdata.mvp_ubo);
    ilG_drawable3d *drawable = ilG_icosahedron(context);
    ilG_bindable_bind(&ilG_shape_bindable, drawable);
    ilG_bindable_bind(&ilG_material_bindable, self->material);
    il_vec3 pos = il_positionable_getPosition(&context->camera->positionable);
    glUniform3f(glGetUniformLocation(self->material->program, "camera"), 
            pos.x, 
            pos.y,
            pos.z);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_RECTANGLE, context->fbtextures[0]);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_RECTANGLE, context->fbtextures[2]);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_RECTANGLE, context->fbtextures[3]);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_RECTANGLE, context->fbtextures[4]);
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBlendFunc(GL_ONE, GL_ONE);
    //glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glCullFace(GL_FRONT);
    
    //il_mat vp = ilG_computeMVP(ILG_VP, context->camera, NULL);
    //il_vec4 vec = il_vec4_new();
    unsigned iter;
    ilG_light *l;
    while ((l = ilG_context_iterLights(self->context, id, &iter))) {
        context->positionable = &l->positionable;
        ilG_bindable_action(&ilG_material_bindable, self->material);
        il_vec3 pos = il_positionable_getPosition(&l->positionable);
        //pos = il_mat_mulv(vp, pos, pos);
        glUniform3f(self->position_loc, pos.x, pos.y, pos.z);
        il_vec3 col = l->color;
        glUniform3f(self->color_loc, col.x, col.y, col.z);
        glUniform1f(self->radius_loc, l->radius);
        ilG_bindable_action(&ilG_shape_bindable, drawable);
        //glDrawElements(GL_TRIANGLES, sizeof(indices)/sizeof(GLuint), GL_UNSIGNED_INT, NULL);
    }
    //il_vec4_free(vec);
    //glDrawElementsInstanced(GL_TRIANGLES, sizeof(indices)/sizeof(GLuint), GL_UNSIGNED_INT, NULL, context->lights.length);
    glDisable(GL_BLEND);
    context->drawable = NULL;
    context->material = NULL;
    context->drawableb = NULL;
    context->materialb = NULL;
    ilG_testError("Error drawing lights");
}

static bool lights_build(void *ptr, ilG_rendid id, ilG_context *context, ilG_renderer *out)
{
    ilG_lights *self = ptr;
    self->context = context;
    if (ilG_material_link(self->material, context)) {
        return false;
    }
    self->position_loc  = glGetUniformLocation(self->material->program, "position");
    self->color_loc     = glGetUniformLocation(self->material->program, "color");
    self->radius_loc    = glGetUniformLocation(self->material->program, "radius");

    *out = (ilG_renderer) {
        .id = id,
        .free = lights_free,
        .draw = lights_draw,
        .obj = self
    };
    return true;
}

ilG_builder ilG_lights_builder()
{
    ilG_lights *self = calloc(1, sizeof(ilG_lights));

    // shader creation
    struct ilG_material* mtl = ilG_material_new();
    ilG_material_vertex_file(mtl, "light.vert");
    ilG_material_fragment_file(mtl, "light.frag");
    ilG_material_name(mtl, "Deferred Shader");
    ilG_material_arrayAttrib(mtl, ILG_ARRATTR_POSITION, "in_Position");
    ilG_material_textureUnit(mtl, 0, "depth");
    ilG_material_textureUnit(mtl, 1, "normal");
    ilG_material_textureUnit(mtl, 2, "diffuse");
    ilG_material_textureUnit(mtl, 3, "specular");
    ilG_material_matrix(mtl, ILG_INVERSE | ILG_VP, "ivp");
    ilG_material_fragData(mtl, ILG_FRAGDATA_ACCUMULATION, "out_Color");
    ilG_material_matrix(mtl, ILG_MODEL_T | ILG_VP, "mvp");
    ilG_material_bindFunc(mtl, size_uniform, self, "size");
    self->material = mtl;
    self->invalidated = 1;

    return ilG_builder_wrap(self, lights_build);
}

