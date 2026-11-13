#include "Gl.h"

#include <cstdio>
#include <vector>

#define DFGL_DEFINE(ret, name, args) PFN_df##name dfgl##name = nullptr;
DFGL_FUNCS(DFGL_DEFINE)
#undef DFGL_DEFINE

namespace df {

bool loadGl(const char** missing) {
#define DFGL_LOAD(ret, name, args)                                                     \
    dfgl##name = reinterpret_cast<PFN_df##name>(SDL_GL_GetProcAddress("gl" #name));   \
    if (!dfgl##name) { if (missing) *missing = "gl" #name; return false; }
    DFGL_FUNCS(DFGL_LOAD)
#undef DFGL_LOAD
    return true;
}

static GLuint compileStage(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(size_t(len) + 1);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::fprintf(stderr, "shader compile error:\n%s\n", log.data());
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint compileProgram(const char* vs, const char* fs) {
    GLuint v = compileStage(GL_VERTEX_SHADER, vs);
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(size_t(len) + 1);
        glGetProgramInfoLog(p, len, nullptr, log.data());
        std::fprintf(stderr, "program link error:\n%s\n", log.data());
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

} // namespace df
