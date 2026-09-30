#include "platform.h"
#include "sdl_platform.h"
#include "touch_overlay.h"
#include "gl.h"

#ifdef HALO_ANDROID

#include <math.h>
#include <string.h>
#include <SDL3/SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb/stb_image.h"

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

typedef struct
{
        GLuint texture;
        int width;
        int height;
        int loaded;
} TouchTexture;

static GLint overlay_size;
static GLint overlay_tex;

static TouchTexture tex_A;
static TouchTexture tex_B;
static TouchTexture tex_X;
static TouchTexture tex_Y;
static TouchTexture tex_LT;
static TouchTexture tex_RT;
static TouchTexture tex_START;
static TouchTexture tex_BACK;
static TouchTexture tex_LEFT_STICK;
static TouchTexture tex_RIGHT_STICK;


static const char *overlay_vertex_shader =
        "#version 300 es\n"
        "layout(location = 0) in vec2 a_pos;\n"
        "uniform vec2 u_pos;\n"
        "uniform vec2 u_size;\n"
        "out vec2 v_uv;\n"
        "void main() {\n"
        "    gl_Position = vec4(a_pos * u_size + u_pos, 0.0, 1.0);\n"
        "    v_uv = a_pos * 0.5 + 0.5;\n"
        "    v_uv.y = 1.0 - v_uv.y;\n"
        "}\n";

static const char *overlay_fragment_shader =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform sampler2D u_tex;\n"
        "in vec2 v_uv;\n"
        "out vec4 frag_color;\n"
        "void main() {\n"
        "    frag_color = texture(u_tex, v_uv);\n"
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


static int load_touch_texture(TouchTexture *texture, const char *path)
{
        FILE *file;
        long size;
        void *data;
        int width, height, channels;
        unsigned char *pixels;

        memset(texture, 0, sizeof(*texture));

        file = fopen(path, "rb");
        if (!file)
                return 0;

        if (fseek(file, 0, SEEK_END) != 0)
        {
                fclose(file);
                return 0;
        }

        size = ftell(file);
        if (size <= 0 || size > 64 * 1024 * 1024)
        {
                fclose(file);
                return 0;
        }

        rewind(file);

        data = malloc((size_t)size);
        if (!data)
        {
                fclose(file);
                return 0;
        }

        if (fread(data, 1, (size_t)size, file) != (size_t)size)
        {
                free(data);
                fclose(file);
                return 0;
        }

        fclose(file);

        pixels = stbi_load_from_memory(
                (const unsigned char *)data,
                (int)size,
                &width,
                &height,
                &channels,
                4);

        free(data);

        if (!pixels)
                return 0;

        glGenTextures(1, &texture->texture);
        glBindTexture(GL_TEXTURE_2D, texture->texture);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA,
                width,
                height,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels);

        stbi_image_free(pixels);

        texture->width = width;
        texture->height = height;
        texture->loaded = 1;

        return 1;
}

static void draw_touch_texture(
        TouchTexture *texture,
        float x,
        float y,
        float width,
        float height)
{
        if (!texture->loaded)
                return;

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture->texture);

        glUniform1i(overlay_tex, 0);

        glUniform2f(
                overlay_pos,
                x * 2.0f - 1.0f,
                1.0f - y * 2.0f);

        glUniform2f(
                overlay_size,
                width,
                height);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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
        overlay_size = glGetUniformLocation(overlay_program, "u_size");
        overlay_tex = glGetUniformLocation(overlay_program, "u_tex");
        overlay_color = glGetUniformLocation(overlay_program, "u_color");

        glBindVertexArray(overlay_vao);
        glBindBuffer(GL_ARRAY_BUFFER, overlay_vbo);

        {
                const float quad[] = {
                        -1.0f, -1.0f,
                         1.0f, -1.0f,
                        -1.0f,  1.0f,
                         1.0f,  1.0f
                };

                glBufferData(
                        GL_ARRAY_BUFFER,
                        sizeof(quad),
                        quad,
                        GL_STATIC_DRAW);
        }

        glEnableVertexAttribArray(0);

        glVertexAttribPointer(
                0, 2, GL_FLOAT, GL_FALSE,
                sizeof(float) * 2,
                (const void *)0);

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        load_touch_texture(&tex_A, "touch_controls/A.png");
        load_touch_texture(&tex_B, "touch_controls/B.png");
        load_touch_texture(&tex_X, "touch_controls/X.png");
        load_touch_texture(&tex_Y, "touch_controls/Y.png");
        load_touch_texture(&tex_LT, "touch_controls/LT.png");
        load_touch_texture(&tex_RT, "touch_controls/RT.png");
        load_touch_texture(&tex_START, "touch_controls/START.png");
        load_touch_texture(&tex_BACK, "touch_controls/BACK.png");
        load_touch_texture(&tex_LEFT_STICK,
                "touch_controls/analogico_esquerdo.png");
        load_touch_texture(&tex_RIGHT_STICK,
                "touch_controls/analogico_direito.png");
}

void touch_overlay_draw(void)
{
        touch_overlay_init();

        if (!overlay_program)
                return;

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(overlay_program);
        glBindVertexArray(overlay_vao);

        /*
         * Analógico esquerdo
         */
        draw_touch_texture(
                &tex_LEFT_STICK,
                0.17f, 0.76f,
                0.25f, 0.25f);

        /*
         * Analógico direito
         */
        draw_touch_texture(
                &tex_RIGHT_STICK,
                0.83f, 0.76f,
                0.25f, 0.25f);

        /*
         * LT / RT em cima
         */
        draw_touch_texture(
                &tex_LT,
                0.25f, 0.12f,
                0.16f, 0.10f);

        draw_touch_texture(
                &tex_RT,
                0.75f, 0.12f,
                0.16f, 0.10f);

        /*
         * BACK / START
         */
        draw_touch_texture(
                &tex_BACK,
                0.43f, 0.17f,
                0.10f, 0.07f);

        draw_touch_texture(
                &tex_START,
                0.57f, 0.17f,
                0.10f, 0.07f);

        /*
         * ABXY
         *
         *       Y
         *    X     B
         *       A
         */
        draw_touch_texture(
                &tex_Y,
                0.83f, 0.45f,
                0.12f, 0.12f);

        draw_touch_texture(
                &tex_X,
                0.75f, 0.55f,
                0.12f, 0.12f);

        draw_touch_texture(
                &tex_B,
                0.91f, 0.55f,
                0.12f, 0.12f);

        draw_touch_texture(
                &tex_A,
                0.83f, 0.65f,
                0.12f, 0.12f);

        glBindTexture(GL_TEXTURE_2D, 0);
        glBindVertexArray(0);
        glUseProgram(0);

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
}

#endif
