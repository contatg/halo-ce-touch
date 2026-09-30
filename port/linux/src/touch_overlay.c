#include "platform.h"
#include "sdl_platform.h"
#include "touch_overlay.h"
#include "gl.h"

#ifdef HALO_ANDROID

#include <math.h>
#include <string.h>

/*
 * Virtual controller overlay.
 *
 * Coordinates are normalized to the current framebuffer:
 *   x = 0..1 left -> right
 *   y = 0..1 top  -> bottom
 *
 * The input side uses the same regions in sdl_platform.c.
 */

static GLuint overlay_program;
static GLuint overlay_vao;
static GLuint overlay_vbo;
static GLint overlay_color;
static GLint overlay_pos;

static const char *overlay_vertex_shader =
        "#version 320 es\n"
        "layout(location = 0) in vec2 a_pos;\n"
        "uniform vec2 u_pos;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_pos + u_pos, 0.0, 1.0);\n"
        "}\n";

static const char *overlay_fragment_shader =
        "#version 320 es\n"
        "precision mediump float;\n"
        "uniform vec4 u_color;\n"
        "out vec4 frag_color;\n"
        "void main() {\n"
        "    frag_color = u_color;\n"
        "}\n";

static GLuint compile_shader(GLenum type, const char *source)
{
        GLuint shader = glCreateShader(type);
        GLint ok = GL_FALSE;

        glShaderSource(shader, 1, &source, NULL);
        glCompileShader(shader);
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

        if (!ok)
        {
                glDeleteShader(shader);
                return 0;
        }

        return shader;
}

static void overlay_add_circle(float cx, float cy, float radius,
        float alpha, int segments)
{
        float vertices[128 * 2];
        int count = segments + 2;

        if (count > 128)
                count = 128;

        vertices[0] = 0.0f;
        vertices[1] = 0.0f;

        for (int i = 0; i <= segments && i + 1 < 128; i++)
        {
                float angle = (float)i * 6.28318530718f / (float)segments;
                vertices[(i + 1) * 2 + 0] = cosf(angle) * radius;
                vertices[(i + 1) * 2 + 1] = sinf(angle) * radius;
        }

        glUniform2f(overlay_pos, cx * 2.0f - 1.0f,
                1.0f - cy * 2.0f);

        glUniform4fv(overlay_color, 1,
                (const GLfloat[]){0.15f, 0.15f, 0.15f, alpha});

        glBindBuffer(GL_ARRAY_BUFFER, overlay_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                sizeof(float) * count * 2,
                vertices,
                GL_STREAM_DRAW);

        glDrawArrays(GL_TRIANGLE_FAN, 0, count);
}

static void overlay_add_button(float x, float y, float radius,
        float alpha, const char *label)
{
        (void)label;
        overlay_add_circle(x, y, radius, alpha, 32);
}

static void overlay_add_stick(float x, float y, float radius)
{
        overlay_add_circle(x, y, radius, 0.20f, 40);
        overlay_add_circle(x, y, radius * 0.48f, 0.38f, 32);
}

void touch_overlay_init(void)
{
        GLuint vertex;
        GLuint fragment;

        if (overlay_program)
                return;

        vertex = compile_shader(GL_VERTEX_SHADER, overlay_vertex_shader);
        fragment = compile_shader(GL_FRAGMENT_SHADER, overlay_fragment_shader);

        if (!vertex || !fragment)
                return;

        overlay_program = glCreateProgram();
        glAttachShader(overlay_program, vertex);
        glAttachShader(overlay_program, fragment);
        glLinkProgram(overlay_program);

        glDeleteShader(vertex);
        glDeleteShader(fragment);

        glGenVertexArrays(1, &overlay_vao);
        glGenBuffers(1, &overlay_vbo);

        overlay_pos = glGetUniformLocation(overlay_program, "u_pos");
        overlay_color = glGetUniformLocation(overlay_program, "u_color");

        glBindVertexArray(overlay_vao);
        glBindBuffer(GL_ARRAY_BUFFER, overlay_vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
                0, 2, GL_FLOAT, GL_FALSE,
                sizeof(float) * 2, (const void *)0);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void touch_overlay_draw(void)
{
        GLint viewport[4];

        touch_overlay_init();

        if (!overlay_program)
                return;

        glGetIntegerv(GL_VIEWPORT, viewport);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(overlay_program);
        glBindVertexArray(overlay_vao);

        /*
         * Left/right virtual sticks.
         */
        overlay_add_stick(0.16f, 0.76f, 0.105f);
        overlay_add_stick(0.84f, 0.76f, 0.105f);

        /*
         * ABXY cluster.
         */
        overlay_add_button(0.87f, 0.64f, 0.050f, 0.32f, "A");
        overlay_add_button(0.94f, 0.56f, 0.050f, 0.32f, "B");
        overlay_add_button(0.80f, 0.56f, 0.050f, 0.32f, "X");
        overlay_add_button(0.87f, 0.48f, 0.050f, 0.32f, "Y");

        /*
         * Bumpers.
         */
        overlay_add_button(0.77f, 0.14f, 0.055f, 0.30f, "LB");
        overlay_add_button(0.92f, 0.14f, 0.055f, 0.30f, "RB");

        /*
         * Triggers.
         */
        overlay_add_button(0.77f, 0.88f, 0.055f, 0.28f, "LT");
        overlay_add_button(0.92f, 0.88f, 0.055f, 0.28f, "RT");

        /*
         * Back / Start.
         */
        overlay_add_button(0.49f, 0.14f, 0.045f, 0.28f, "BACK");
        overlay_add_button(0.625f, 0.14f, 0.045f, 0.28f, "START");

        glBindVertexArray(0);
        glUseProgram(0);

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);

        (void)viewport;
}

#endif
