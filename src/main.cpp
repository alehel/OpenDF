// OpenDF - a cross-platform reimplementation of the Delta Force (1998) engine.
//
//   opendf [--data DIR] [--terrain NAME]       free-roam terrain viewer
//   opendf [--data DIR] list [EXT]              list archive contents
//   opendf [--data DIR] extract OUTDIR [EXT]    extract (and decrypt) files
//
// DIR is the folder holding the game's PFF archives (the CD root or its
// DFSETUP folder). It defaults to the current directory and its parent.

#include "core/Pff.h"
#include "render/Gl.h"
#include "render/Math.h"
#include "render/Terrain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace df;

namespace {

struct Options {
    fs::path dataDir;
    std::string command = "view";
    std::vector<std::string> args;
    std::string terrain;
    std::string screenshot;  // render one frame to a .ppm and exit (for testing)
    int width = 1280, height = 720;
};

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data") o.dataDir = next();
        else if (a == "--terrain") o.terrain = next();
        else if (a == "--screenshot") o.screenshot = next();
        else if (a == "--size") {
            std::string s = next();
            if (std::sscanf(s.c_str(), "%dx%d", &o.width, &o.height) != 2) return false;
        } else if (a == "-h" || a == "--help") return false;
        else if (o.command == "view" && o.args.empty() && (a == "list" || a == "extract" || a == "view")) o.command = a;
        else o.args.push_back(a);
    }
    return true;
}

void usage() {
    std::puts(
        "OpenDF - Delta Force engine reimplementation\n"
        "\n"
        "usage:\n"
        "  opendf [--data DIR] [--terrain NAME] [--size WxH]   terrain viewer\n"
        "  opendf [--data DIR] list [EXT]                      list files in the archives\n"
        "  opendf [--data DIR] extract OUTDIR [EXT]            extract files (PFF2 decrypted)\n"
        "\n"
        "DIR is the folder with the game's .PFF files (CD root or DFSETUP).\n"
        "\n"
        "viewer controls:\n"
        "  mouse look, WASD move, Shift fast, Space/C up/down (fly)\n"
        "  F toggle walk/fly, Tab / Shift+Tab next/previous terrain\n"
        "  [ ] height scale, - = view distance, Esc release mouse / quit");
}

bool mountData(Vfs& vfs, Options& o) {
    std::vector<fs::path> candidates;
    if (!o.dataDir.empty()) candidates.push_back(o.dataDir);
    else {
        candidates = {fs::current_path(), fs::current_path().parent_path()};
        if (char* base = SDL_GetBasePath()) {
            fs::path b = base;
            SDL_free(base);
            candidates.push_back(b);
            candidates.push_back(b.parent_path().parent_path());
            candidates.push_back(b.parent_path().parent_path().parent_path());
        }
    }
    for (auto& c : candidates) {
        std::string err;
        Vfs probe;
        if (probe.mountGameDir(c, &err) && probe.exists("DFG1.TRN")) {
            vfs.mountGameDir(c, nullptr);
            o.dataDir = c;
            return true;
        }
    }
    std::fprintf(stderr, "Could not find the Delta Force data (DF.PFF etc.). Use --data DIR.\n");
    return false;
}

int cmdList(Vfs& vfs, const Options& o) {
    for (auto& a : vfs.archives())
        std::printf("# %s  (%s, %zu files)\n", a->path().string().c_str(), a->encrypted() ? "PFF2, encrypted" : "PFF3",
                    a->entries().size());
    for (auto& n : vfs.list(o.args.empty() ? "" : o.args[0])) std::puts(n.c_str());
    return 0;
}

int cmdExtract(Vfs& vfs, const Options& o) {
    if (o.args.empty()) { usage(); return 1; }
    fs::path out = o.args[0];
    fs::create_directories(out);
    std::vector<uint8_t> buf;
    int n = 0;
    for (auto& name : vfs.list(o.args.size() > 1 ? o.args[1] : "")) {
        if (!vfs.read(name, buf)) { std::fprintf(stderr, "failed: %s\n", name.c_str()); continue; }
        std::ofstream f(out / name, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
        ++n;
    }
    std::printf("extracted %d files to %s\n", n, out.string().c_str());
    return 0;
}

void writePpm(const std::string& path, int w, int h) {
    std::vector<uint8_t> px(size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream f(path, std::ios::binary);
    f << "P6 " << w << " " << h << " 255\n";
    for (int y = h - 1; y >= 0; --y) f.write(reinterpret_cast<const char*>(&px[size_t(y) * w * 3]), w * 3);
}

struct Camera {
    Vec3 pos{4096, 300, 4096};
    float yaw = 0.6f, pitch = -0.05f;
    bool walking = false;
    Vec3 forward() const { return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)}; }
};

int cmdView(Vfs& vfs, Options& o) {
    std::vector<std::string> terrains;
    for (auto& t : vfs.list(".TRN")) terrains.push_back(t.substr(0, t.size() - 4));
    if (terrains.empty()) { std::fprintf(stderr, "no terrains found\n"); return 1; }
    int current = 0;
    if (!o.terrain.empty()) {
        auto it = std::find(terrains.begin(), terrains.end(), toUpper(o.terrain));
        if (it == terrains.end()) { std::fprintf(stderr, "unknown terrain %s\n", o.terrain.c_str()); return 1; }
        current = int(it - terrains.begin());
    } else if (auto it = std::find(terrains.begin(), terrains.end(), "DFG1"); it != terrains.end()) {
        current = int(it - terrains.begin());
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | (o.screenshot.empty() ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_HIDDEN);
    SDL_Window* win = SDL_CreateWindow("OpenDF", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, o.width, o.height, flags);
    if (!win) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { std::fprintf(stderr, "OpenGL 3.3 context: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(1);
    const char* missing = nullptr;
    if (!loadGl(&missing)) { std::fprintf(stderr, "missing OpenGL function %s\n", missing); return 1; }

    int rc = 0;
    {
        Terrain terrain;
        Camera cam;
        float viewDistance = 4000.f;
        auto load = [&](int idx) {
            std::string err;
            WorldScale keep = terrain.scale;
            if (!terrain.load(vfs, terrains[idx], &err)) {
                std::fprintf(stderr, "%s: %s\n", terrains[idx].c_str(), err.c_str());
                return false;
            }
            terrain.scale = keep;
            current = idx;
            std::printf("terrain %s: \"%s\" by %s\n", terrains[idx].c_str(), terrain.desc().name.c_str(),
                        terrain.desc().creator.c_str());
            cam.pos.y = std::max(cam.pos.y, terrain.heightAt(cam.pos.x, cam.pos.z) + 40.f);
            return true;
        };
        if (!load(current)) rc = 1;
        else {
            cam.pos.y = terrain.heightAt(cam.pos.x, cam.pos.z) + 120.f;

            bool captured = o.screenshot.empty();
            SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE);
            Uint64 last = SDL_GetPerformanceCounter();
            float fpsTimer = 0, vy = 0;
            int frames = 0;
            bool running = true;
            while (running) {
                Uint64 now = SDL_GetPerformanceCounter();
                float dt = std::min(float(now - last) / SDL_GetPerformanceFrequency(), 0.1f);
                last = now;

                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_QUIT) running = false;
                    else if (e.type == SDL_MOUSEBUTTONDOWN && !captured) {
                        captured = true;
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                    } else if (e.type == SDL_MOUSEMOTION && captured) {
                        cam.yaw += e.motion.xrel * 0.0025f;
                        cam.pitch = std::clamp(cam.pitch - e.motion.yrel * 0.0025f, -1.5f, 1.5f);
                    } else if (e.type == SDL_KEYDOWN) {
                        bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                        switch (e.key.keysym.sym) {
                        case SDLK_ESCAPE:
                            if (captured) { captured = false; SDL_SetRelativeMouseMode(SDL_FALSE); }
                            else running = false;
                            break;
                        case SDLK_TAB: {
                            int n = int(terrains.size());
                            int next = (current + (shift ? n - 1 : 1)) % n;
                            for (int tries = 0; tries < n && !load(next); ++tries) next = (next + (shift ? n - 1 : 1)) % n;
                            break;
                        }
                        case SDLK_f: cam.walking = !cam.walking; vy = 0; break;
                        case SDLK_LEFTBRACKET: terrain.scale.heightScale = std::max(0.25f, terrain.scale.heightScale - 0.25f); break;
                        case SDLK_RIGHTBRACKET: terrain.scale.heightScale += 0.25f; break;
                        case SDLK_MINUS: viewDistance = std::max(1000.f, viewDistance - 500.f); break;
                        case SDLK_EQUALS: viewDistance = std::min(12000.f, viewDistance + 500.f); break;
                        default: break;
                        }
                    }
                }

                const Uint8* k = SDL_GetKeyboardState(nullptr);
                bool fast = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
                Vec3 fwd = cam.forward();
                Vec3 flatFwd = normalize(Vec3{fwd.x, 0, fwd.z});
                Vec3 right = normalize(cross(flatFwd, Vec3{0, 1, 0}));
                Vec3 move{};
                Vec3 dirF = cam.walking ? flatFwd : fwd;
                if (k[SDL_SCANCODE_W]) move = move + dirF;
                if (k[SDL_SCANCODE_S]) move = move - dirF;
                if (k[SDL_SCANCODE_D]) move = move + right;
                if (k[SDL_SCANCODE_A]) move = move - right;
                float speed = cam.walking ? (fast ? 9.f : 4.5f) : (fast ? 400.f : 80.f);
                if (!cam.walking) {
                    if (k[SDL_SCANCODE_SPACE]) move.y += 1;
                    if (k[SDL_SCANCODE_C]) move.y -= 1;
                }
                cam.pos = cam.pos + normalize(move) * (speed * dt);

                const float ground = terrain.heightAt(cam.pos.x, cam.pos.z);
                const float eyeHeight = 1.7f;
                if (cam.walking) {
                    if (k[SDL_SCANCODE_SPACE] && cam.pos.y <= ground + eyeHeight + 0.01f) vy = 5.f;
                    vy -= 9.81f * dt;
                    cam.pos.y += vy * dt;
                    if (cam.pos.y < ground + eyeHeight) { cam.pos.y = ground + eyeHeight; vy = 0; }
                } else {
                    cam.pos.y = std::max(cam.pos.y, ground + 2.f);
                }
                // Keep coordinates small; the world repeats every map extent.
                const float ext = terrain.mapExtent();
                cam.pos.x = std::fmod(std::fmod(cam.pos.x, ext) + ext, ext);
                cam.pos.z = std::fmod(std::fmod(cam.pos.z, ext) + ext, ext);

                int dw, dh;
                SDL_GL_GetDrawableSize(win, &dw, &dh);
                glViewport(0, 0, dw, dh);
                glClearColor(0, 0, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                Mat4 proj = perspective(60.f * 3.14159265f / 180.f, float(dw) / std::max(dh, 1), 0.5f, viewDistance * 1.1f);
                Mat4 view = lookDir(cam.pos, cam.forward(), {0, 1, 0});
                terrain.draw(view, proj, cam.pos, viewDistance);

                if (!o.screenshot.empty()) {
                    writePpm(o.screenshot, dw, dh);
                    std::printf("wrote %s (%dx%d)\n", o.screenshot.c_str(), dw, dh);
                    running = false;
                }
                SDL_GL_SwapWindow(win);

                ++frames;
                fpsTimer += dt;
                if (fpsTimer >= 0.5f) {
                    char title[256];
                    std::snprintf(title, sizeof title, "OpenDF - %s \"%s\" - %s - %.0f fps - pos %.0f %.0f alt %.0f m",
                                  terrains[current].c_str(), terrain.desc().name.c_str(), cam.walking ? "walk" : "fly",
                                  frames / fpsTimer, cam.pos.x, cam.pos.z, cam.pos.y - ground);
                    SDL_SetWindowTitle(win, title);
                    frames = 0;
                    fpsTimer = 0;
                }
            }
        }
    }
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    return rc;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseArgs(argc, argv, o)) { usage(); return 1; }
    if (SDL_Init(o.command == "view" ? SDL_INIT_VIDEO : 0) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    Vfs vfs;
    int rc = 1;
    if (mountData(vfs, o)) {
        if (o.command == "list") rc = cmdList(vfs, o);
        else if (o.command == "extract") rc = cmdExtract(vfs, o);
        else rc = cmdView(vfs, o);
    }
    SDL_Quit();
    return rc;
}
