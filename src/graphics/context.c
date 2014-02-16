#include "context.h"

#include <string.h>
#include <errno.h>

#include "util/log.h"
#include "util/logger.h"
#include "util/ilassert.h"
#include "util/timer.h"
#include "graphics/glutil.h"
#include "common/event.h"
#include "graphics/graphics.h"
#include "input/input.h"

struct ilG_context_msg {
    struct ilG_context_msg *next;
    enum ilG_context_msgtype {
        ILG_UPLOAD,
        ILG_RESIZE,
        ILG_RENAME,
        ILG_STOP
    } type;
    union {
        struct {
            void (*cb)(void*);
            void *ptr;
        } upload;
        int resize[2];
        char *rename;
    } value;
};

struct ilG_context_queue {
    struct ilG_context_msg *first;
    volatile struct ilG_context_msg *head, *tail;
};

static void queue_init(struct ilG_context_queue *queue)
{
    queue->head = queue->tail = calloc(1, sizeof(struct ilG_context_msg));
    queue->first = (struct ilG_context_msg*)queue->head;
}

void queue_free(struct ilG_context_queue *queue) // TODO: Fix this cross-translation unit hack
{
    struct ilG_context_msg *cur;
    while (cur) {
        cur = queue->first;
        queue->first = cur->next;
        free(cur);
    }
}

// TODO: Multiple producer threads
static void produce(struct ilG_context_queue *queue, struct ilG_context_msg *msg)
{
    msg->next = NULL;
    queue->tail->next = msg;
    queue->tail = msg;
    struct ilG_context_msg *tmp;
    while (queue->first != queue->head) {
        tmp = queue->first;
        queue->first = queue->first->next;
        free(tmp);
    }
}

static struct ilG_context_msg *consume(struct ilG_context_queue *queue)
{
    if (queue->head != queue->tail) {
        return (struct ilG_context_msg*)queue->head->next;
    }
    return NULL;
}

static void done_consume(struct ilG_context_queue *queue)
{
    if (queue->head != queue->tail)
    {
        queue->head = queue->head->next;
    }
}

void ilG_registerInputBackend(ilG_context *ctx);

ilG_context *ilG_context_new()
{
    ilG_context *self = calloc(1, sizeof(ilG_context));
    self->contextMajor = 3;
#ifdef __APPLE__
    self->contextMinor = 2;
    self->forwardCompat = 1;
#else
    self->contextMinor = 1;
#endif
    self->profile = ILG_CONTEXT_NONE;
    self->experimental = 1;
    self->startWidth = 800;
    self->startHeight = 600;
    self->initialTitle = "IntenseLogic";
    self->tick      = ilE_handler_new_with_name("il.graphics.context.tick");
    self->resize    = ilE_handler_new_with_name("il.graphics.context.resize");
    self->close     = ilE_handler_new_with_name("il.graphics.context.close");
    self->destroy   = ilE_handler_new_with_name("il.graphics.context.destroy");
    ilI_handler_init(&self->handler);
    self->queue = calloc(1, sizeof(struct ilG_context_queue));
    queue_init(self->queue);
    return self;
}

void ilG_context_free(ilG_context *self)
{
    ilG_context_renderer.free(self);
}

void ilG_context_hint(ilG_context *self, enum ilG_context_hint hint, int param)
{
#define HINT(v, f) case v: self->f = param; break;
    switch (hint) {
        HINT(ILG_CONTEXT_MAJOR, contextMajor)
        HINT(ILG_CONTEXT_MINOR, contextMinor)
        HINT(ILG_CONTEXT_FORWARD_COMPAT, forwardCompat)
        HINT(ILG_CONTEXT_PROFILE, profile)
        HINT(ILG_CONTEXT_DEBUG_CONTEXT, debug_context)
        HINT(ILG_CONTEXT_EXPERIMENTAL, experimental)
        HINT(ILG_CONTEXT_WIDTH, startWidth)
        HINT(ILG_CONTEXT_HEIGHT, startHeight)
        HINT(ILG_CONTEXT_HDR, hdr)
        HINT(ILG_CONTEXT_USE_DEFAULT_FB, use_default_fb)
        HINT(ILG_CONTEXT_DEBUG_RENDER, debug_render)
        default:
        il_error("Invalid hint");
    }
}

static int upload(ilG_context *self, void (*fn)(void*), void* ptr)
{
    (void)self;
    fn(ptr);
    return 1;
}

static int resize(ilG_context *self, int w, int h)
{
    /*if (!self->complete) {
        il_error("Resizing incomplete context");
        return 0;
    }*/
    if (self->use_default_fb) {
        self->valid = 1;
        return 1;
    }

    // GL setup
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    IL_GRAPHICS_TESTERROR("Error setting up screen");

    self->width = w;
    self->height = h;
    glBindFramebuffer(GL_FRAMEBUFFER, self->framebuffer);
    glBindTexture(GL_TEXTURE_RECTANGLE, self->fbtextures[ILG_CONTEXT_DEPTH]);
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_DEPTH_COMPONENT, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_RECTANGLE, self->fbtextures[ILG_CONTEXT_DEPTH], 0);
    ilG_testError("Unable to create depth buffer");
    glBindTexture(GL_TEXTURE_RECTANGLE, self->fbtextures[ILG_CONTEXT_ACCUM]);
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGBA, w, h, 0, GL_RGBA, self->hdr? GL_FLOAT : GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, self->fbtextures[ILG_CONTEXT_ACCUM], 0);
    ilG_testError("Unable to create accumulation buffer");
    glBindTexture(GL_TEXTURE_RECTANGLE, self->fbtextures[ILG_CONTEXT_NORMAL]);
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGB, w, h, 0, GL_RGB, GL_FLOAT, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_RECTANGLE, self->fbtextures[ILG_CONTEXT_NORMAL], 0);
    ilG_testError("Unable to create normal buffer");
    glBindTexture(GL_TEXTURE_RECTANGLE, self->fbtextures[ILG_CONTEXT_DIFFUSE]);
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_RECTANGLE, self->fbtextures[ILG_CONTEXT_DIFFUSE], 0);
    ilG_testError("Unable to create diffuse buffer");
    glBindTexture(GL_TEXTURE_RECTANGLE, self->fbtextures[ILG_CONTEXT_SPECULAR]);
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_RECTANGLE, self->fbtextures[ILG_CONTEXT_SPECULAR], 0);
    ilG_testError("Unable to create specular buffer");
    // completeness testing
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        const char *status_str;
        switch(status) {
            case GL_FRAMEBUFFER_UNDEFINED:                      status_str = "GL_FRAMEBUFFER_UNDEFINED";                        break;
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:          status_str = "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";            break;
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:  status_str = "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";    break;
            case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:         status_str = "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";           break;
            case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:         status_str = "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";           break;
            case GL_FRAMEBUFFER_UNSUPPORTED:                    status_str = "GL_FRAMEBUFFER_UNSUPPORTED";                      break;
            case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:         status_str = "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";           break;
            case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:       status_str = "GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS";         break;
            default:                                            status_str = "???";                                             break;
        }
        il_error("Unable to create framebuffer for context: %s", status_str);
        return 0;
    }
    il_value val = il_value_vectorl(2, il_value_int(self->width), il_value_int(self->height));
    ilE_handler_fire(self->resize, &val);
    il_value_free(val);
    self->valid = 1;
    return 1;
}

static int context_rename(ilG_context *self, GLFWwindow *window, char *title)
{
    if (title != self->title && title) {
        if (self->title) {
            free(self->title);
        }
        self->title = title;
        glfwSetWindowTitle(window, self->title);
    }
    return 1;
}

int ilG_context_upload(ilG_context *self, void (*fn)(void*), void* ptr)
{
    struct ilG_context_msg *msg = calloc(1, sizeof(struct ilG_context_msg));
    msg->type = ILG_UPLOAD;
    msg->value.upload.cb = fn;
    msg->value.upload.ptr = ptr;
    produce(self->queue, msg);
    return 1;
}

int ilG_context_resize(ilG_context *self, int w, int h)
{
    struct ilG_context_msg *msg = calloc(1, sizeof(struct ilG_context_msg));
    msg->type = ILG_RESIZE;
    msg->value.resize[0] = w;
    msg->value.resize[1] = h;
    produce(self->queue, msg);
    return 1;
}

char *strdup(const char*);
int ilG_context_rename(ilG_context *self, const char *title)
{
    struct ilG_context_msg *msg = calloc(1, sizeof(struct ilG_context_msg));
    msg->type = ILG_RENAME;
    msg->value.rename = strdup(title);
    produce(self->queue, msg);
    return 1;
}

void ilG_context_bindFB(ilG_context *self)
{
    if (self->use_default_fb) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        static const GLenum drawbufs[] = {
            GL_COLOR_ATTACHMENT0,   // accumulation
            GL_COLOR_ATTACHMENT1,   // normal
            GL_COLOR_ATTACHMENT2,   // diffuse
            GL_COLOR_ATTACHMENT3    // specular
        };
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, self->framebuffer);
        glDrawBuffers(4, &drawbufs[0]);
    }
}

void ilG_context_bind_for_outpass(ilG_context *self)
{
    /*static const GLenum drawbufs[] = {
        GL_COLOR_ATTACHMENT0    // accumulation
    };*/
    glBindFramebuffer(GL_READ_FRAMEBUFFER, self->framebuffer);
    //glDrawBuffers(1, &drawbufs[0]);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
}

static void render_frame(ilG_context *context)
{
    glfwPollEvents();

    glViewport(0, 0, context->width, context->height);
    context->material       = NULL;
    context->materialb      = NULL;
    context->drawable       = NULL;
    context->drawableb      = NULL;
    context->texture        = NULL;
    context->textureb       = NULL;
    context->positionable   = NULL;

    il_debug("Begin render");
    ilG_context_bindFB(context);
    if (context->debug_render) {
        glClearColor(0.39, 0.58, 0.93, 1.0); // cornflower blue
    } else {
        glClearColor(0, 0, 0, 1.0);
    }
    ilG_testError("glClearColor");
    glClearDepth(1.0);
    ilG_testError("glClearDepth");
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ilG_context_renderer.draw(context);
}

static void measure_frametime(ilG_context *context)
{
    struct timeval time, tv;
    struct ilG_frame *iter, *temp, *frame, *last;

    gettimeofday(&time, NULL);
    IL_LIST_ITER(context->frames_head, ll, iter, temp) {
        timersub(&time, &iter->start, &tv);
        if (tv.tv_sec > 0) {
            IL_LIST_POPHEAD(context->frames_head, ll, frame);
            timersub(&context->frames_sum, &frame->elapsed, &context->frames_sum);
            context->num_frames--;
            free(frame);
        }
    }
    last = IL_LIST_TAIL(context->frames_head, ll);
    frame = calloc(1, sizeof(struct ilG_frame));
    frame->start = time;
    if (last) {
        timersub(&time, &last->start, &frame->elapsed);
    } else {
        frame->elapsed = (struct timeval){0,0};
    }
    IL_LIST_APPEND(context->frames_head, ll, frame);
    context->num_frames++;
    timeradd(&frame->elapsed, &context->frames_sum, &context->frames_sum);
    context->frames_average.tv_sec = context->frames_sum.tv_sec / context->num_frames;
    context->frames_average.tv_usec = context->frames_sum.tv_usec / context->num_frames;
    context->frames_average.tv_usec += (context->frames_sum.tv_sec % context->num_frames) * 1000000 / context->num_frames;
}

static APIENTRY GLvoid error_cb(GLenum source, GLenum type, GLuint id, GLenum severity,
                       GLsizei length, const GLchar* message, GLvoid* user)
{
    (void)user;
    const char *ssource;
    switch(source) {
        case GL_DEBUG_SOURCE_API_ARB:               ssource=" API";              break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB:     ssource=" Window System";    break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB:   ssource=" Shader Compiler";  break;
        case GL_DEBUG_SOURCE_THIRD_PARTY_ARB:       ssource=" Third Party";      break;
        case GL_DEBUG_SOURCE_APPLICATION_ARB:       ssource=" Application";      break;
        case GL_DEBUG_SOURCE_OTHER_ARB:             ssource="";            break;
        default: ssource="???";
    }
    const char *stype;
    switch(type) {
        case GL_DEBUG_TYPE_ERROR_ARB:               stype=" error";                 break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB: stype=" deprecated behaviour";  break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:  stype=" undefined behaviour";   break;
        case GL_DEBUG_TYPE_PORTABILITY_ARB:         stype=" portability issue";     break;
        case GL_DEBUG_TYPE_PERFORMANCE_ARB:         stype=" performance issue";     break;
        case GL_DEBUG_TYPE_OTHER_ARB:               stype="";                       break;
        default: stype="???";
    }
    const char *sseverity;
    switch(severity) {
        case GL_DEBUG_SEVERITY_HIGH_ARB:    sseverity="high";   break;
        case GL_DEBUG_SEVERITY_MEDIUM_ARB:  sseverity="medium"; break;
        case GL_DEBUG_SEVERITY_LOW_ARB:     sseverity="low";    break;
        default: sseverity="???";
    }
    char msg[length+1];
    strncpy(msg, message, length+1);
    if (msg[length-1] == '\n') {
        msg[length-1] = 0; // cut off newline
    }

    char source_buf[64];
    snprintf(source_buf, 64, "OpenGL%s", ssource);

    const char *msg_fmt = "%s%s #%u: %s";
    size_t len = snprintf(NULL, 0, msg_fmt, sseverity, stype, id, msg);
    char msg_buf[len+1];
    snprintf(msg_buf, len+1, msg_fmt, sseverity, stype, id, msg);

    il_logmsg *lmsg = il_logmsg_new(1);
    il_logmsg_setLevel(lmsg, IL_NOTIFY);
    il_logmsg_copyMessage(lmsg, msg_buf);
    il_logmsg_copyBtString(lmsg, 0, source_buf);

    il_logger *logger = il_logger_stderr; // TODO
    il_logger_log(logger, lmsg);
}

static void setup_context(ilG_context *self)
{
    if (self->complete) {
        il_error("Context already complete");
        return;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, self->contextMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, self->contextMinor);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, self->forwardCompat? GL_TRUE : GL_FALSE);
    switch (self->profile) {
        case ILG_CONTEXT_NONE:
        break;
        case ILG_CONTEXT_CORE:
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        break;
        case ILG_CONTEXT_COMPAT:
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
        break;
        default:
        il_error("Invalid profile");
        return;
    }
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, self->debug_context? GL_TRUE : GL_FALSE);
    if (!(self->window = glfwCreateWindow(self->startWidth, self->startHeight, self->initialTitle, NULL, NULL))) { // TODO: allow context sharing + monitor specification
        il_error("glfwOpenWindow() failed - are you sure you have OpenGL 3.1?");
        return;
    }
    self->width = self->startWidth;
    self->height = self->startHeight;
    glfwMakeContextCurrent(self->window);
    ilG_registerInputBackend(self);
    glfwSetWindowUserPointer(self->window, self);
    glfwSwapInterval(0);
    glewExperimental = self->experimental? GL_TRUE : GL_FALSE; // TODO: find out why IL crashes without this
    GLenum err = glewInit();
    if (GLEW_OK != err) {
        il_error("glewInit() failed: %s", glewGetErrorString(err));
        return;
    }
    il_log("Using GLEW %s", glewGetString(GLEW_VERSION));

#ifndef __APPLE__
    if (!GLEW_VERSION_3_1) {
        il_error("GL version 3.1 is required, you have %s: crashes are on you", glGetString(GL_VERSION));
    } else {
        il_log("OpenGL Version %s", glGetString(GL_VERSION));
    }
#endif

    IL_GRAPHICS_TESTERROR("Unknown");
    if (GLEW_KHR_debug) {
        glDebugMessageCallback((GLDEBUGPROC)&error_cb, NULL);
        glEnable(GL_DEBUG_OUTPUT);
        il_log("KHR_debug present, enabling advanced errors");
        IL_GRAPHICS_TESTERROR("glDebugMessageCallback()");
    } else {
        il_log("KHR_debug missing");
    }
    GLint num_texunits;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &num_texunits);
    ilG_testError("glGetIntegerv");
    self->texunits = calloc(sizeof(unsigned), num_texunits);
    self->num_texunits = num_texunits;
    if (!self->use_default_fb) {
        glGenFramebuffers(1, &self->framebuffer);
        glGenTextures(5, &self->fbtextures[0]);
        ilG_testError("Unable to generate framebuffer");
    }
    self->complete = 1;
}

static void *render_thread(void *ptr)
{
    ilG_context *self = ptr;

    setup_context(self);
    ilG_context_renderer.build(self, NULL);
    resize(self, self->startWidth, self->startHeight);

    while (self->valid && !glfwWindowShouldClose(self->window)) {
        // Process messages
        struct ilG_context_msg *msg;
        while ((msg = consume(self->queue))) {
            switch (msg->type) {
                case ILG_UPLOAD: upload(self, msg->value.upload.cb, msg->value.upload.ptr); break;
                case ILG_RESIZE: resize(self, msg->value.resize[0], msg->value.resize[1]); break;
                case ILG_RENAME: context_rename(self, self->window, msg->value.rename); break;
                case ILG_STOP: goto stop;
            }
            done_consume(self->queue);
        }
        // Run per-frame stuff
        il_value nil = il_value_nil();
        ilE_handler_fire(self->tick, &nil);
        il_value_free(nil);
        int width, height;
        glfwGetFramebufferSize(self->window, &width, &height);
        if (width != self->width || height != self->height) {
            resize(self, width, height);
        }
        // Render
        render_frame(self);
        if (self->use_default_fb) {
            glfwSwapBuffers(self->window);
        }
        // Perform fps measuring calculations
        measure_frametime(self);
    }

    if (!self->valid || !self->complete) {
        il_error("Tried to render invalid context");
    }

stop:
    glDeleteFramebuffers(1, &self->framebuffer);
    glDeleteTextures(5, &self->fbtextures[0]);
    return NULL;
}

int ilG_context_start(ilG_context *self)
{
    if (!self->camera) {
        il_error("No camera");
        return 0;
    }
    if (!self->world) {
        il_error("No world");
        return 0;
    }
    
    // Start thread
    int res = pthread_create(&self->thread, NULL, render_thread, self);
    if (res) {
        il_error("pthread_create: %s", strerror(errno));
    }
    return res == 0;
}


