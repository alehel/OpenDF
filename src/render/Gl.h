// Minimal OpenGL 3.3 core loader built on SDL_GL_GetProcAddress, so the engine
// has no extra dependencies on Windows, macOS or Linux.
#pragma once

#define GL_SILENCE_DEPRECATION
#include <SDL.h>
#include <SDL_opengl.h>
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define DFGL_API __stdcall
#else
#define DFGL_API
#endif

#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#define GL_RG8 0x822B
#define GL_RED 0x1903
#define GL_RG 0x8227
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_CONSTANT_COLOR
#define GL_CONSTANT_COLOR 0x8001
#define GL_CONSTANT_ALPHA 0x8003
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif

#define DFGL_FUNCS(X) \
    X(void, BlendColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const char* const*, const GLint*)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, char*)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, char*)) \
    X(void, DeleteShader, (GLuint)) \
    X(void, DeleteProgram, (GLuint)) \
    X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const char*)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*)) \
    X(void, GenVertexArrays, (GLsizei, GLuint*)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*)) \
    X(void, GenBuffers, (GLsizei, GLuint*)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, std::ptrdiff_t, const void*, GLenum)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint*)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, TexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, GenerateMipmap, (GLenum))

#define DFGL_DECLARE(ret, name, args) \
    using PFN_df##name = ret(DFGL_API*) args; \
    extern PFN_df##name dfgl##name;
DFGL_FUNCS(DFGL_DECLARE)
#undef DFGL_DECLARE

#define glBlendColor dfglBlendColor
#define glCreateShader dfglCreateShader
#define glShaderSource dfglShaderSource
#define glCompileShader dfglCompileShader
#define glGetShaderiv dfglGetShaderiv
#define glGetShaderInfoLog dfglGetShaderInfoLog
#define glCreateProgram dfglCreateProgram
#define glAttachShader dfglAttachShader
#define glLinkProgram dfglLinkProgram
#define glGetProgramiv dfglGetProgramiv
#define glGetProgramInfoLog dfglGetProgramInfoLog
#define glDeleteShader dfglDeleteShader
#define glDeleteProgram dfglDeleteProgram
#define glUseProgram dfglUseProgram
#define glGetUniformLocation dfglGetUniformLocation
#define glUniform1i dfglUniform1i
#define glUniform1f dfglUniform1f
#define glUniform2f dfglUniform2f
#define glUniform4f dfglUniform4f
#define glUniform3f dfglUniform3f
#define glUniformMatrix4fv dfglUniformMatrix4fv
#define glGenVertexArrays dfglGenVertexArrays
#define glBindVertexArray dfglBindVertexArray
#define glDeleteVertexArrays dfglDeleteVertexArrays
#define glGenBuffers dfglGenBuffers
#define glBindBuffer dfglBindBuffer
#define glBufferData dfglBufferData
#define glDeleteBuffers dfglDeleteBuffers
#define glEnableVertexAttribArray dfglEnableVertexAttribArray
#define glVertexAttribPointer dfglVertexAttribPointer
#define glActiveTexture dfglActiveTexture
#define glTexImage3D dfglTexImage3D
#define glGenerateMipmap dfglGenerateMipmap

namespace df {
bool loadGl(const char** missing);
GLuint compileProgram(const char* vs, const char* fs);
} // namespace df
