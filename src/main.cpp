// OpenDF - a cross-platform reimplementation of the Delta Force (1998) engine.
//
//   opendf [--data DIR] [--terrain NAME]       free-roam terrain viewer
//   opendf [--data DIR] models [NAME]           3DI model viewer
//   opendf [--data DIR] list [EXT]              list archive contents
//   opendf [--data DIR] extract OUTDIR [EXT]    extract (and decrypt) files
//
// DIR is the folder holding the game's PFF archives (the CD root or its
// DFSETUP folder). It defaults to the current directory and its parent.

#include "core/Pff.h"
#include "game/Game.h"
#include "formats/Mission.h"
#include "formats/Model3di.h"
#include "formats/Pcx.h"
#include "formats/Trn.h"
#include "render/Gl.h"
#include "render/Math.h"
#include "render/Models.h"
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

constexpr float kPi = 3.14159265f;

struct Options {
    fs::path dataDir;
    std::string command = "view";
    std::vector<std::string> args;
    std::string terrain;
    std::string screenshot;  // render one frame to a .ppm and exit (for testing)
    bool spawn = false;      // place the sample objects at start (for testing)
    bool hasCamera = false;  // --camera x,y,height,yaw,pitch (map metres, degrees)
    float camera[5] = {0, 0, 0, 0, 0};
    // Game testing aids (mission mode).
    int frames = 1, weapon = -1, stance = -1, zoom = 0;
    bool holdFire = false, showMap = false, walkForward = false, viewModelOnly = false, killEnemies = false;
    int width = 1280, height = 720;
};

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--data") o.dataDir = next();
        else if (a == "--terrain") o.terrain = next();
        else if (a == "--screenshot") o.screenshot = next();
        else if (a == "--spawn") o.spawn = true;
        else if (a == "--frames") o.frames = std::atoi(next().c_str());
        else if (a == "--weapon") o.weapon = std::atoi(next().c_str());
        else if (a == "--stance") o.stance = std::atoi(next().c_str());
        else if (a == "--zoom") o.zoom = std::atoi(next().c_str());
        else if (a == "--fire") o.holdFire = true;
        else if (a == "--walk") o.walkForward = true;
        else if (a == "--kill-enemies") o.killEnemies = true;
        else if (a == "--viewmodel-only") o.viewModelOnly = true;
        else if (a == "--map") o.showMap = true;
        else if (a == "--camera") {
            std::string s = next();
            float* c = o.camera;
            if (std::sscanf(s.c_str(), "%f,%f,%f,%f,%f", &c[0], &c[1], &c[2], &c[3], &c[4]) != 5) return false;
            o.hasCamera = true;
        }
        else if (a == "--size") {
            std::string s = next();
            if (std::sscanf(s.c_str(), "%dx%d", &o.width, &o.height) != 2) return false;
        } else if (a == "-h" || a == "--help") return false;
        else if (o.command == "view" && o.args.empty() &&
                 (a == "list" || a == "extract" || a == "view" || a == "models" || a == "check" || a == "mission")) o.command = a;
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
        "  opendf [--data DIR] mission [C02M01]                play a mission in first person\n"
        "  opendf [--data DIR] models [NAME]                   3DI model viewer\n"
        "  opendf [--data DIR] list [EXT]                      list files in the archives\n"
        "  opendf [--data DIR] extract OUTDIR [EXT]            extract files (PFF2 decrypted)\n"
        "  opendf [--data DIR] check                           parse every model/terrain/image\n"
        "  --camera X,Y,HEIGHT,YAW,PITCH  start at map metres X (east), Y (north), HEIGHT above\n"
        "                                 ground, compass YAW and PITCH in degrees\n"
        "\n"
        "DIR is the folder with the game's .PFF files (CD root or DFSETUP).\n"
        "\n"
        "terrain viewer: mouse look, WASD move, Shift fast, Space/C up/down (fly)\n"
        "  F walk/fly, Tab / Shift+Tab next/previous terrain or mission,\n"
        "  M place sample objects, N clear them\n"
        "  [ ] height scale, - = view distance, Esc release mouse / quit\n"
        "mission: mouse look, WASD/arrows move (running; Shift walks), Space jump,\n"
        "  left mouse/Ctrl fire, right mouse zoom/scope, M or R reload,\n"
        "  Delete/End/PageDown stand/crouch/prone (X, Z toggle crouch/prone),\n"
        "  2 pistol, 3/4 M4 single/auto, 6 M40 sniper, 7 MP5SD, 8 M249, Backspace cycle,\n"
        "  C command map, G goals, F3 crosshair, F4 gun, N fly, Tab next mission\n"
        "model viewer: drag to orbit, wheel to zoom, Tab / Shift+Tab model,\n"
        "  PageUp/PageDown jump 10, L cycle level of detail, Esc quit");
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

// Owns the SDL window and its OpenGL 3.3 core context.
struct GlWindow {
    SDL_Window* win = nullptr;
    SDL_GLContext ctx = nullptr;

    bool open(const Options& o, const char* title) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI |
                       (o.screenshot.empty() ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_HIDDEN);
        win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, o.width, o.height, flags);
        if (!win) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
        ctx = SDL_GL_CreateContext(win);
        if (!ctx) { std::fprintf(stderr, "OpenGL 3.3 context: %s\n", SDL_GetError()); return false; }
        SDL_GL_SetSwapInterval(1);
        const char* missing = nullptr;
        if (!loadGl(&missing)) { std::fprintf(stderr, "missing OpenGL function %s\n", missing); return false; }
        return true;
    }
    ~GlWindow() {
        if (ctx) SDL_GL_DeleteContext(ctx);
        if (win) SDL_DestroyWindow(win);
    }
};

struct Camera {
    Vec3 pos{512, 60, 512};
    float yaw = 0.6f, pitch = -0.05f;
    bool walking = false;
    Vec3 forward() const { return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)}; }
};

struct Placed {
    const GpuModel* model;
    Vec3 pos;        // x/z inside the map; y = ground + lift
    float heading;   // radians clockwise from north; the model's +X faces it
    float lift;      // height above the ground
};

// A row of familiar objects for judging the world scale against the terrain.
const char* kSampleObjects[] = {"PLAYER01", "RUSS01", "HUMVEE", "PICKUP", "BARREL", "CRATE1", "PALM1",
                                "OAK", "HUT1", "BARRACKS", "GRDTOWER", "BLACKHWK"};

std::vector<Placed> placeSamples(Vfs& vfs, ModelRenderer& models, const Terrain& terrain, const Camera& cam) {
    std::vector<Placed> out;
    Vec3 fwd = normalize(Vec3{std::sin(cam.yaw), 0, -std::cos(cam.yaw)});
    Vec3 right{-fwd.z, 0, fwd.x};
    Vec3 base = cam.pos + fwd * 12.f;
    float offset = 0;
    for (const char* name : kSampleObjects) {
        std::string err;
        const GpuModel* m = models.get(vfs, name, &err);
        if (!m) { std::fprintf(stderr, "%s: %s\n", name, err.c_str()); continue; }
        const auto& l = m->lods[0];
        float halfWidth = std::max(l.max[0] - l.min[0], l.max[2] - l.min[2]) * 0.5f;
        offset += halfWidth + 1.f;
        Vec3 p = base + right * offset + fwd * halfWidth;
        p.y = terrain.heightAt(p.x, p.z) - l.min[1];
        out.push_back({m, p, cam.yaw + kPi * 0.5f, -l.min[1]});
        offset += halfWidth + 1.f;
    }
    return out;
}

// The terrain repeats every `extent` metres; draw each object at the copy
// nearest the camera.
Vec3 nearestCopy(Vec3 p, Vec3 eye, float extent) {
    auto wrap = [&](float v, float ref) {
        float d = std::fmod(v - ref, extent);
        if (d > extent * 0.5f) d -= extent;
        if (d < -extent * 0.5f) d += extent;
        return ref + d;
    };
    return {wrap(p.x, eye.x), p.y, wrap(p.z, eye.z)};
}

Mat4 objectMatrix(const Placed& p, Vec3 at) { return translate(at) * rotateY(kPi * 0.5f - p.heading); }

void reseat(std::vector<Placed>& placed, const Terrain& terrain) {
    for (auto& p : placed) p.pos.y = terrain.heightAt(p.pos.x, p.pos.z) + p.lift;
}

// Everything a mission places that has a graphic (markers, triggers and
// sounds are invisible). Returns the player start, if any.
bool placeMission(Vfs& vfs, ModelRenderer& models, const ItemDefs& items, const Mission& mission,
                  const Terrain& terrain, std::vector<Placed>& out, Placed* start) {
    out.clear();
    bool haveStart = false;
    const float ext = terrain.mapExtent();
    int missing = 0;
    for (const auto& o : mission.objects) {
        const ItemDef* item = items.byMissionType(o.type);
        Vec3 pos{std::fmod(std::fmod(o.x, ext) + ext, ext), 0, std::fmod(std::fmod(-o.y, ext) + ext, ext)};
        const float heading = o.heading * kPi / 180.f;
        if (item && item->id == kItemPlayerStart && !haveStart) {
            haveStart = true;
            if (start) *start = {nullptr, pos, heading, o.z};
        }
        if (!item || item->graphic.empty() || item->type == "effect") continue;
        const GpuModel* m = models.get(vfs, item->graphic);
        if (!m) { ++missing; continue; }
        out.push_back({m, pos, heading, o.z});
    }
    reseat(out, terrain);
    if (missing) std::fprintf(stderr, "%d objects have no loadable graphic\n", missing);
    return haveStart;
}

// Parses every file type OpenDF understands and reports anything that fails.
int cmdCheck(Vfs& vfs) {
    std::vector<uint8_t> buf;
    int total = 0, failed = 0;
    auto report = [&](const std::string& name, bool ok, const std::string& why) {
        ++total;
        if (!ok) { ++failed; std::printf("FAIL %-14s %s\n", name.c_str(), why.c_str()); }
    };
    for (auto& n : vfs.list(".3DI")) {
        Model3di m;
        std::string err;
        bool ok = vfs.read(n, buf) && loadModel3di(buf, m, &err);
        if (ok && m.lods.empty()) { ok = false; err = "no geometry"; }
        report(n, ok, err);
    }
    for (auto& n : vfs.list(".PCX")) {
        IndexedImage img;
        report(n, vfs.read(n, buf) && decodePcx(buf, img), "bad PCX");
    }
    for (auto& n : vfs.list(".TRN")) {
        TerrainDesc d;
        report(n, vfs.read(n, buf) && parseTrn(std::string(buf.begin(), buf.end()), d), "bad TRN");
    }
    ItemDefs items;
    bool haveItems = vfs.read("ITEMS.DEF", buf) && items.parse(std::string(buf.begin(), buf.end()));
    report("ITEMS.DEF", haveItems, "cannot parse");
    int objects = 0, unknown = 0;
    for (auto& n : vfs.list(".BMS")) {
        Mission m;
        std::string err;
        bool ok = vfs.read(n, buf) && parseMission(buf, m, &err);
        if (ok) {
            std::string trn = m.terrain;
            if (!vfs.exists(trn)) { ok = false; err = "terrain " + trn + " missing"; }
            for (auto& obj : m.objects) {
                ++objects;
                if (haveItems && !items.byMissionType(obj.type)) ++unknown;
            }
        }
        report(n, ok, err);
    }
    std::printf("%d mission objects, %d with unknown item types\n", objects, unknown);
    std::printf("%d files checked, %d failed\n", total, failed);
    return 0;
}

int cmdView(Vfs& vfs, Options& o) {
    const bool missionMode = o.command == "mission";
    // In mission mode Tab cycles missions, otherwise terrains.
    std::vector<std::string> entries;
    for (auto& t : vfs.list(missionMode ? ".BMS" : ".TRN")) entries.push_back(t.substr(0, t.size() - 4));
    if (entries.empty()) { std::fprintf(stderr, "nothing to show\n"); return 1; }
    std::string want = missionMode ? (o.args.empty() ? "C02M01" : o.args[0]) : (o.terrain.empty() ? "DFG1" : o.terrain);
    int current = 0;
    if (auto it = std::find(entries.begin(), entries.end(), toUpper(want)); it != entries.end())
        current = int(it - entries.begin());
    else if (missionMode ? !o.args.empty() : !o.terrain.empty()) {
        std::fprintf(stderr, "unknown %s %s\n", missionMode ? "mission" : "terrain", want.c_str());
        return 1;
    }

    ItemDefs items;
    if (missionMode) {
        std::vector<uint8_t> raw;
        if (!vfs.read("ITEMS.DEF", raw) || !items.parse(std::string(raw.begin(), raw.end()))) {
            std::fprintf(stderr, "cannot read ITEMS.DEF\n");
            return 1;
        }
    }
    std::string missionTitle;

    GlWindow window;
    if (!window.open(o, "OpenDF")) return 1;
    SDL_Window* win = window.win;

    Terrain terrain;
    ModelRenderer models;
    std::string err;
    if (!models.init(&err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    Camera cam;
    std::vector<Placed> placed;
    float viewDistance = 1200.f;

    auto load = [&](int idx) {
        std::string e, trn = entries[idx];
        Mission mission;
        if (missionMode) {
            std::vector<uint8_t> raw;
            if (!vfs.read(entries[idx] + ".BMS", raw) || !parseMission(raw, mission, &e)) {
                std::fprintf(stderr, "%s: %s\n", entries[idx].c_str(), e.empty() ? "unreadable" : e.c_str());
                return false;
            }
            trn = mission.terrain;
            if (auto dot = trn.rfind('.'); dot != std::string::npos) trn.resize(dot);
        }
        WorldScale keep = terrain.scale;
        if (!terrain.load(vfs, trn, &e)) {
            std::fprintf(stderr, "%s: %s\n", entries[idx].c_str(), e.c_str());
            return false;
        }
        terrain.scale = keep;
        current = idx;
        if (missionMode) {
            Placed start{};
            missionTitle = mission.title;
            if (placeMission(vfs, models, items, mission, terrain, placed, &start)) {
                cam.pos = start.pos;
                cam.pos.y = terrain.heightAt(cam.pos.x, cam.pos.z) + 1.7f;
                cam.yaw = start.heading;
                cam.pitch = -0.05f;
                cam.walking = true;
            }
            std::printf("mission %s: \"%s\" by %s on %s (%s), %zu objects shown\n", entries[idx].c_str(),
                        mission.title.c_str(), mission.author.c_str(), trn.c_str(), terrain.desc().name.c_str(),
                        placed.size());
        } else {
            std::printf("terrain %s: \"%s\" by %s\n", entries[idx].c_str(), terrain.desc().name.c_str(),
                        terrain.desc().creator.c_str());
            reseat(placed, terrain);
        }
        cam.pos.y = std::max(cam.pos.y, terrain.heightAt(cam.pos.x, cam.pos.z) + (cam.walking ? 1.7f : 10.f));
        return true;
    };
    if (!missionMode) cam.pos.y = 60.f;
    if (!load(current)) return 1;
    if (o.hasCamera) {
        cam.pos = {o.camera[0], 0, -o.camera[1]};
        cam.pos.y = terrain.heightAt(cam.pos.x, cam.pos.z) + o.camera[2];
        cam.yaw = o.camera[3] * kPi / 180.f;
        cam.pitch = o.camera[4] * kPi / 180.f;
        cam.walking = false;
    }
    if (o.spawn) {
        cam.pos.y = terrain.heightAt(cam.pos.x, cam.pos.z) + 1.7f;
        cam.walking = true;
        placed = placeSamples(vfs, models, terrain, cam);
        cam.yaw += 0.35f;
        cam.pitch = -0.08f;
    }

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
                    int n = int(entries.size());
                    int next = (current + (shift ? n - 1 : 1)) % n;
                    for (int tries = 0; tries < n && !load(next); ++tries) next = (next + (shift ? n - 1 : 1)) % n;
                    break;
                }
                case SDLK_f: cam.walking = !cam.walking; vy = 0; break;
                case SDLK_m: {
                    auto more = placeSamples(vfs, models, terrain, cam);
                    placed.insert(placed.end(), more.begin(), more.end());
                    break;
                }
                case SDLK_n: placed.clear(); break;
                case SDLK_LEFTBRACKET:
                    terrain.scale.heightScale = std::max(0.05f, terrain.scale.heightScale - 0.05f);
                    reseat(placed, terrain);
                    break;
                case SDLK_RIGHTBRACKET:
                    terrain.scale.heightScale += 0.05f;
                    reseat(placed, terrain);
                    break;
                case SDLK_MINUS: viewDistance = std::max(300.f, viewDistance - 100.f); break;
                case SDLK_EQUALS: viewDistance = std::min(4000.f, viewDistance + 100.f); break;
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
        float speed = cam.walking ? (fast ? 9.f : 4.5f) : (fast ? 120.f : 25.f);
        if (!cam.walking) {
            if (k[SDL_SCANCODE_SPACE]) move.y += 1;
            if (k[SDL_SCANCODE_C]) move.y -= 1;
        }
        Vec3 delta = normalize(move) * (speed * dt);
        cam.pos = cam.pos + delta;

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
        Mat4 proj = perspective(60.f * kPi / 180.f, float(dw) / std::max(dh, 1), 0.25f, viewDistance * 1.1f);
        Mat4 view = lookDir(cam.pos, cam.forward(), {0, 1, 0});
        terrain.draw(view, proj, cam.pos, viewDistance);

        if (!placed.empty()) {
            SceneLighting light{cam.pos, terrain.fogColor(), terrain.sunDirection(), viewDistance * 0.35f, viewDistance * 0.98f};
            models.begin(proj * view, light);
            const Vec3 fwdView = cam.forward();
            struct Visible { const Placed* p; Vec3 at; int lod; };
            std::vector<Visible> visible;
            visible.reserve(placed.size());
            for (auto& p : placed) {
                Vec3 at = nearestCopy(p.pos, cam.pos, ext);
                Vec3 d = at - cam.pos;
                float dist = std::sqrt(dot(d, d));
                if (dist > viewDistance + p.model->radius) continue;
                if (dot(d, fwdView) < -p.model->radius) continue;   // behind the camera
                visible.push_back({&p, at, ModelRenderer::chooseLod(*p.model, dist)});
            }
            for (auto& v : visible) models.draw(*v.p->model, v.lod, objectMatrix(*v.p, v.at));
            for (auto& v : visible) models.drawTranslucent(*v.p->model, v.lod, objectMatrix(*v.p, v.at));
            models.end();
        }

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
            std::snprintf(title, sizeof title, "OpenDF - %s \"%s\" - %s - %.0f fps - map %.0f, %.0f - %.0f m above ground",
                          entries[current].c_str(), missionMode ? missionTitle.c_str() : terrain.desc().name.c_str(),
                          cam.walking ? "walk" : "fly", frames / fpsTimer, cam.pos.x, -cam.pos.z, cam.pos.y - ground);
            SDL_SetWindowTitle(win, title);
            frames = 0;
            fpsTimer = 0;
        }
    }
    return 0;
}

int cmdModels(Vfs& vfs, Options& o) {
    std::vector<std::string> names;
    for (auto& n : vfs.list(".3DI")) names.push_back(n.substr(0, n.size() - 4));
    if (names.empty()) { std::fprintf(stderr, "no models found\n"); return 1; }
    int current = 0;
    if (!o.args.empty()) {
        auto it = std::find(names.begin(), names.end(), toUpper(o.args[0]));
        if (it == names.end()) { std::fprintf(stderr, "unknown model %s\n", o.args[0].c_str()); return 1; }
        current = int(it - names.begin());
    }

    GlWindow window;
    if (!window.open(o, "OpenDF models")) return 1;
    ModelRenderer models;
    std::string err;
    if (!models.init(&err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }

    float yaw = 0.7f, pitch = 0.35f, zoom = 1.0f;
    int lod = 0;
    bool dragging = false, running = true;
    const GpuModel* model = nullptr;
    std::string status;
    auto select = [&](int idx) {
        int n = int(names.size());
        current = ((idx % n) + n) % n;
        std::string e;
        model = models.get(vfs, names[current], &e);
        lod = 0;
        zoom = 1.0f;
        status = model ? "" : e;
    };
    select(current);

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_MOUSEBUTTONDOWN) dragging = true;
            else if (e.type == SDL_MOUSEBUTTONUP) dragging = false;
            else if (e.type == SDL_MOUSEMOTION && dragging) {
                yaw += e.motion.xrel * 0.01f;
                pitch = std::clamp(pitch + e.motion.yrel * 0.01f, -1.5f, 1.5f);
            } else if (e.type == SDL_MOUSEWHEEL) {
                zoom = std::clamp(zoom * (e.wheel.y > 0 ? 0.9f : 1.1f), 0.1f, 20.f);
            } else if (e.type == SDL_KEYDOWN) {
                bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_TAB: select(current + (shift ? -1 : 1)); break;
                case SDLK_PAGEDOWN: select(current + 10); break;
                case SDLK_PAGEUP: select(current - 10); break;
                case SDLK_l: if (model) lod = (lod + 1) % int(model->lods.size()); break;
                default: break;
                }
            }
        }

        int dw, dh;
        SDL_GL_GetDrawableSize(window.win, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.42f, 0.5f, 0.58f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);

        char title[256];
        if (model) {
            const auto& l = model->lods[lod];
            Vec3 center{(l.min[0] + l.max[0]) * 0.5f, (l.min[1] + l.max[1]) * 0.5f, (l.min[2] + l.max[2]) * 0.5f};
            float dist = model->radius * 2.6f * zoom;
            Vec3 dir{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
            Vec3 eye = center + dir * dist;
            Mat4 proj = perspective(45.f * kPi / 180.f, float(dw) / std::max(dh, 1), dist * 0.01f, dist * 10.f);
            Mat4 view = lookDir(eye, center - eye, {0, 1, 0});
            SceneLighting light{eye, {0.42f, 0.5f, 0.58f}, normalize(Vec3{0.5f, 0.8f, 0.35f}), 1e6f, 2e6f};
            models.begin(proj * view, light);
            models.draw(*model, lod, Mat4{});
            models.drawTranslucent(*model, lod, Mat4{});
            models.end();
            std::snprintf(title, sizeof title, "OpenDF models - %s (%d/%zu) - LOD %d/%zu - %d tris - %.2f x %.2f x %.2f m",
                          names[current].c_str(), current + 1, names.size(), lod, model->lods.size(), l.triangles,
                          l.max[0] - l.min[0], l.max[2] - l.min[2], l.max[1] - l.min[1]);
        } else {
            std::snprintf(title, sizeof title, "OpenDF models - %s (%d/%zu) - cannot load: %s", names[current].c_str(),
                          current + 1, names.size(), status.c_str());
        }
        SDL_SetWindowTitle(window.win, title);

        if (!o.screenshot.empty()) {
            writePpm(o.screenshot, dw, dh);
            std::printf("%s\nwrote %s\n", title, o.screenshot.c_str());
            running = false;
        }
        SDL_GL_SwapWindow(window.win);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parseArgs(argc, argv, o)) { usage(); return 1; }
    const bool graphics = o.command == "view" || o.command == "models" || o.command == "mission";
    if (SDL_Init(graphics ? SDL_INIT_VIDEO : 0) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    Vfs vfs;
    int rc = 1;
    if (mountData(vfs, o)) {
        if (o.command == "list") rc = cmdList(vfs, o);
        else if (o.command == "extract") rc = cmdExtract(vfs, o);
        else if (o.command == "models") rc = cmdModels(vfs, o);
        else if (o.command == "mission") {
            GameOptions g;
            if (!o.args.empty()) g.mission = o.args[0];
            g.width = o.width;
            g.height = o.height;
            g.screenshot = o.screenshot;
            g.frames = std::max(1, o.frames);
            g.hasCamera = o.hasCamera;
            std::copy(o.camera, o.camera + 5, g.camera);
            g.weapon = o.weapon;
            g.stance = o.stance;
            g.zoom = o.zoom;
            g.holdFire = o.holdFire;
            g.walkForward = o.walkForward;
            g.viewModelOnly = o.viewModelOnly;
            g.showMap = o.showMap;
            g.gameDir = o.dataDir;
            g.killEnemies = o.killEnemies;
            rc = runGame(vfs, g);
        }
        else if (o.command == "check") rc = cmdCheck(vfs);
        else rc = cmdView(vfs, o);
    }
    SDL_Quit();
    return rc;
}
