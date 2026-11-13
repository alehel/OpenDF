#include "Game.h"

#include "../render/Gl.h"
#include "../render/Hud.h"
#include "../render/Models.h"
#include "../render/Terrain.h"
#include "Player.h"
#include "Weapons.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <memory>

namespace df {

namespace {

constexpr float kPi = 3.14159265f;
const Color kGreen{0.45f, 0.95f, 0.45f, 1};
const Color kAmber{1.0f, 0.8f, 0.3f, 1};
const Color kWhite{0.95f, 0.95f, 0.9f, 1};
const Color kPanel{0, 0, 0, 0.45f};

struct Message { std::string text; float age; };

bool project(const Mat4& vp, Vec3 p, int w, int h, float& sx, float& sy, float& depth) {
    float x = vp.at(0, 0) * p.x + vp.at(0, 1) * p.y + vp.at(0, 2) * p.z + vp.at(0, 3);
    float y = vp.at(1, 0) * p.x + vp.at(1, 1) * p.y + vp.at(1, 2) * p.z + vp.at(1, 3);
    float cw = vp.at(3, 0) * p.x + vp.at(3, 1) * p.y + vp.at(3, 2) * p.z + vp.at(3, 3);
    if (cw <= 0.05f) return false;
    sx = (x / cw * 0.5f + 0.5f) * w;
    sy = (1 - (y / cw * 0.5f + 0.5f)) * h;
    depth = cw;
    return true;
}

void writePpm(const std::string& path, int w, int h) {
    std::vector<uint8_t> px(size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    std::ofstream f(path, std::ios::binary);
    f << "P6 " << w << " " << h << " 255\n";
    for (int y = h - 1; y >= 0; --y) f.write(reinterpret_cast<const char*>(&px[size_t(y) * w * 3]), w * 3);
}

std::string gridName(Vec3 feet, float extent) {
    // 8 x 8 grid over the map, letters west→east, numbers north→south.
    int col = std::clamp(int(feet.x / extent * 8), 0, 7);
    int row = std::clamp(int(feet.z / extent * 8), 0, 7);
    return std::string(1, char('A' + col)) + std::to_string(row + 1);
}

class Game {
public:
    Game(Vfs& vfs, const GameOptions& o) : vfs_(vfs), o_(o) {}
    int run();

private:
    bool loadMission(int index);
    void handleKey(const SDL_KeyboardEvent& k);
    void drawWorld(const Mat4& view, const Mat4& proj, int w, int h);
    void drawViewModel(int w, int h);
    void drawHud(const Mat4& viewProj, int w, int h);
    void drawCommandMap(int w, int h, float s);

    Vfs& vfs_;
    GameOptions o_;
    SDL_Window* win_ = nullptr;
    ItemDefs items_;
    std::vector<std::string> missions_;
    int current_ = 0;
    Mission mission_;
    // GL-owning members live behind pointers so they can be released before
    // the context is destroyed.
    std::unique_ptr<Terrain> terrainPtr_ = std::make_unique<Terrain>();
    std::unique_ptr<ModelRenderer> modelsPtr_ = std::make_unique<ModelRenderer>();
    std::unique_ptr<Hud> hudPtr_ = std::make_unique<Hud>();
    Terrain& terrain_ = *terrainPtr_;
    ModelRenderer& models_ = *modelsPtr_;
    Hud& hud_ = *hudPtr_;
    World world_;
    Player player_;
    Arsenal arsenal_;
    std::deque<Message> messages_;
    float missionTime_ = 0;
    float viewDistance_ = 1200;
    bool showMap_ = false, showGoals_ = false, crosshair_ = true, gunVisible_ = true;
    bool captured_ = true;
};

bool Game::loadMission(int index) {
    std::vector<uint8_t> raw;
    std::string err;
    const std::string& name = missions_[index];
    if (!vfs_.read(name + ".BMS", raw) || !parseMission(raw, mission_, &err)) {
        std::fprintf(stderr, "%s: %s\n", name.c_str(), err.empty() ? "unreadable" : err.c_str());
        return false;
    }
    std::string trn = mission_.terrain;
    if (auto dot = trn.rfind('.'); dot != std::string::npos) trn.resize(dot);
    if (!terrain_.load(vfs_, trn, &err)) {
        std::fprintf(stderr, "%s: %s\n", name.c_str(), err.c_str());
        return false;
    }
    current_ = index;
    world_.terrain = &terrain_;
    world_.build(vfs_, models_, items_, mission_);

    player_ = Player{};
    player_.feet = {512, 0, 512};
    for (const auto& obj : mission_.objects) {
        const ItemDef* item = items_.byMissionType(obj.type);
        if (item && item->id == kItemPlayerStart) {
            const float ext = terrain_.mapExtent();
            player_.feet = {std::fmod(std::fmod(obj.x, ext) + ext, ext), 0, std::fmod(std::fmod(-obj.y, ext) + ext, ext)};
            player_.yaw = obj.heading * kPi / 180.f;
            break;
        }
    }
    player_.feet.y = world_.groundHeight(player_.feet, 0.45f);
    arsenal_.init();
    missionTime_ = 0;
    messages_.clear();
    messages_.push_back({mission_.title, 0});
    std::printf("mission %s: \"%s\" on %s, %zu objects\n", name.c_str(), mission_.title.c_str(), trn.c_str(),
                world_.objects.size());
    return true;
}

void Game::handleKey(const SDL_KeyboardEvent& k) {
    const bool shift = (k.keysym.mod & KMOD_SHIFT) != 0;
    switch (k.keysym.sym) {
    case SDLK_ESCAPE:
        if (showMap_ || showGoals_) { showMap_ = showGoals_ = false; break; }
        if (captured_) { captured_ = false; SDL_SetRelativeMouseMode(SDL_FALSE); }
        else { SDL_Event q; q.type = SDL_QUIT; SDL_PushEvent(&q); }
        break;
    // Stances, as on the original keyboard layout (Delete / End / Page Down),
    // plus X and Z as toggles.
    case SDLK_DELETE: player_.stance = Stance::Stand; break;
    case SDLK_END: player_.stance = Stance::Crouch; break;
    case SDLK_PAGEDOWN: player_.stance = Stance::Prone; break;
    case SDLK_x: player_.stance = player_.stance == Stance::Crouch ? Stance::Stand : Stance::Crouch; break;
    case SDLK_z: player_.stance = player_.stance == Stance::Prone ? Stance::Stand : Stance::Prone; break;
    // Weapons: 2 sidearm, 3/4 primary single/auto, 6-8 others; Backspace cycles.
    case SDLK_2: arsenal_.select(0); break;
    case SDLK_3: arsenal_.select(1, 0); break;
    case SDLK_4: arsenal_.select(1, 1); break;
    case SDLK_6: arsenal_.select(2); break;
    case SDLK_7: arsenal_.select(3); break;
    case SDLK_8: arsenal_.select(4); break;
    case SDLK_BACKSPACE: arsenal_.cycle(); break;
    case SDLK_m: case SDLK_r: arsenal_.reload(); break;           // M = "change magazine"
    case SDLK_c: showMap_ = !showMap_; showGoals_ = false; break; // command map
    case SDLK_g: showGoals_ = !showGoals_; showMap_ = false; break;
    case SDLK_F3: crosshair_ = !crosshair_; break;
    case SDLK_F4: gunVisible_ = !gunVisible_; break;
    case SDLK_n: player_.fly = !player_.fly; break;               // debug flight
    case SDLK_TAB: {
        int n = int(missions_.size());
        int next = (current_ + (shift ? n - 1 : 1)) % n;
        for (int tries = 0; tries < n && !loadMission(next); ++tries) next = (next + (shift ? n - 1 : 1)) % n;
        break;
    }
    default: break;
    }
}

void Game::drawWorld(const Mat4& view, const Mat4& proj, int w, int h) {
    const Vec3 eye = player_.eye();
    terrain_.draw(view, proj, eye, viewDistance_);
    SceneLighting light{eye, terrain_.fogColor(), terrain_.sunDirection(), viewDistance_ * 0.35f, viewDistance_ * 0.98f};
    models_.begin(proj * view, light);
    const Vec3 fwd = player_.forward();
    struct Visible { const WorldObject* o; Vec3 at; int lod; };
    std::vector<Visible> visible;
    for (const auto& o : world_.objects) {
        if (o.removed) continue;
        Vec3 at = world_.nearest(o.pos, eye);
        Vec3 d = at - eye;
        float dist = std::sqrt(dot(d, d));
        if (dist > viewDistance_ + o.model->radius || dot(d, fwd) < -o.model->radius) continue;
        // Zoomed views keep the detailed LOD further out.
        visible.push_back({&o, at, ModelRenderer::chooseLod(*o.model, dist / arsenal_.zoom())});
    }
    for (auto& v : visible) models_.draw(*v.o->model, v.lod, v.o->matrix(v.at));
    for (auto& v : visible) models_.drawTranslucent(*v.o->model, v.lod, v.o->matrix(v.at));
    models_.end();
}

void Game::drawViewModel(int w, int h) {
    if (!gunVisible_ || arsenal_.scoped() || showMap_) return;
    const GpuModel* m = models_.get(vfs_, arsenal_.def().viewModel);
    if (!m) return;
    glClear(GL_DEPTH_BUFFER_BIT);
    const auto& l = m->lods[0];
    Vec3 center{(l.min[0] + l.max[0]) * 0.5f, (l.min[1] + l.max[1]) * 0.5f, (l.min[2] + l.max[2]) * 0.5f};
    const float length = l.max[0] - l.min[0];
    // Held low and to the right; pulled back by recoil, lowered while reloading.
    float kick = arsenal_.recoil() * 0.012f;
    float lower = arsenal_.reloading() ? 0.12f : 0.f;
    float bob = player_.moving() && !player_.fly ? std::sin(missionTime_ * 9.f) * 0.006f : 0.f;
    Vec3 place{0.20f, -0.24f - lower + bob, -0.42f - length * 0.5f + kick};
    Mat4 model = translate(place) * rotateY(kPi * 0.5f) * translate(center * -1.f);
    Mat4 proj = perspective(55.f * kPi / 180.f, float(w) / std::max(h, 1), 0.02f, 10.f);
    SceneLighting light{{0, 0, 0}, {0, 0, 0}, normalize(Vec3{0.3f, 0.9f, 0.4f}), 1e6f, 2e6f};
    models_.begin(proj, light);
    models_.draw(*m, 0, model);
    models_.drawTranslucent(*m, 0, model, arsenal_.flash() > 0);
    models_.end();
}

void Game::drawCommandMap(int w, int h, float s) {
    hud_.rect(0, 0, float(w), float(h), {0, 0, 0, 0.85f});
    const float size = std::min(w, h) * 0.86f;
    const float x0 = (w - size) * 0.5f, y0 = (h - size) * 0.5f + 10 * s;
    hud_.image(terrain_.colorTexture(), x0, y0, size, size);
    const float ext = terrain_.mapExtent();
    for (int i = 0; i <= 8; ++i) {
        float t = size * i / 8.f;
        hud_.line(x0 + t, y0, x0 + t, y0 + size, 1, {0, 0, 0, 0.5f});
        hud_.line(x0, y0 + t, x0 + size, y0 + t, 1, {0, 0, 0, 0.5f});
        if (i < 8) {
            hud_.text(x0 + t + size / 16 - 4 * s, y0 - 15 * s, std::string(1, char('A' + i)), kWhite, s);
            hud_.text(x0 - 16 * s, y0 + t + size / 16 - 6 * s, std::to_string(i + 1), kWhite, s);
        }
    }
    // People killed so far, and the player as an arrow.
    for (const auto& o : world_.objects)
        if (o.dead) hud_.rect(x0 + o.pos.x / ext * size - 2 * s, y0 + o.pos.z / ext * size - 2 * s, 4 * s, 4 * s, {0.9f, 0.1f, 0.1f, 1});
    float px = x0 + player_.feet.x / ext * size, py = y0 + player_.feet.z / ext * size;
    float dx = std::sin(player_.yaw), dy = -std::cos(player_.yaw);
    float L = 12 * s;
    hud_.line(px - dx * L * 0.5f, py - dy * L * 0.5f, px + dx * L, py + dy * L, 3 * s, kAmber);
    hud_.line(px + dx * L, py + dy * L, px + (dx * 0.4f - dy * 0.5f) * L, py + (dy * 0.4f + dx * 0.5f) * L, 3 * s, kAmber);
    hud_.line(px + dx * L, py + dy * L, px + (dx * 0.4f + dy * 0.5f) * L, py + (dy * 0.4f - dx * 0.5f) * L, 3 * s, kAmber);
    hud_.text(x0, y0 + size + 6 * s, "COMMAND MAP  -  GRID " + gridName(player_.feet, ext) + "  -  C TO CLOSE", kWhite, s);
}

void Game::drawHud(const Mat4& viewProj, int w, int h) {
    const float s = std::max(1.f, std::round(h / 600.f));
    hud_.begin(w, h);

    // Bullet impacts as small puffs: dust on the ground, dark on objects.
    for (const auto& i : arsenal_.impacts()) {
        float sx, sy, depth;
        if (!project(viewProj, i.pos, w, h, sx, sy, depth)) continue;
        float r = std::clamp(60.f / depth, 1.5f, 18.f) * s * arsenal_.zoom() * (1 + i.age * 1.5f);
        float a = std::max(0.f, 1 - i.age / 1.5f) * 0.8f;
        Color c = i.object ? Color{0.15f, 0.15f, 0.15f, a} : Color{0.72f, 0.65f, 0.52f, a};
        hud_.puff(sx, sy, r, c);
    }

    const float cx = w * 0.5f, cy = h * 0.5f;
    if (arsenal_.scoped()) {
        const float radius = std::min(w, h) * 0.46f;
        hud_.scopeMask(cx, cy, radius);
        hud_.line(cx - radius, cy, cx + radius, cy, 1.5f * s, {0, 0, 0, 1});
        hud_.line(cx, cy - radius, cx, cy + radius, 1.5f * s, {0, 0, 0, 1});
        for (int i = -4; i <= 4; ++i) {
            if (!i) continue;
            hud_.rect(cx + i * radius * 0.1f - 1.5f * s, cy - 1.5f * s, 3 * s, 3 * s, {0, 0, 0, 1});
            hud_.rect(cx - 1.5f * s, cy + i * radius * 0.1f - 1.5f * s, 3 * s, 3 * s, {0, 0, 0, 1});
        }
    } else if (crosshair_ && !showMap_) {
        const float g = 4 * s, len = 7 * s;
        hud_.rect(cx - g - len, cy - s * 0.5f, len, s, kGreen);
        hud_.rect(cx + g, cy - s * 0.5f, len, s, kGreen);
        hud_.rect(cx - s * 0.5f, cy - g - len, s, len, kGreen);
        hud_.rect(cx - s * 0.5f, cy + g, s, len, kGreen);
    }

    // Compass: heading strip across the top centre.
    {
        const float cw = 320 * s, ch = 22 * s, x0 = cx - cw / 2, y0 = 8 * s;
        hud_.rect(x0, y0, cw, ch, kPanel);
        const float deg = player_.yaw * 180.f / kPi;
        for (int d = -180; d <= 540; d += 15) {
            float rel = d - deg;
            rel = std::fmod(std::fmod(rel + 180.f, 360.f) + 360.f, 360.f) - 180.f;
            if (std::fabs(rel) > 60) continue;
            float x = cx + rel / 60.f * (cw / 2 - 10 * s);
            int dd = ((d % 360) + 360) % 360;
            const char* card = dd == 0 ? "N" : dd == 90 ? "E" : dd == 180 ? "S" : dd == 270 ? "W" : nullptr;
            if (card) hud_.text(x - hud_.textWidth(card, s) / 2, y0 + 4 * s, card, kAmber, s);
            else hud_.rect(x - 0.5f * s, y0 + ch - 7 * s, s, 5 * s, kWhite);
        }
        hud_.rect(cx - s, y0 + ch, 2 * s, 5 * s, kGreen);
        char buf[16];
        std::snprintf(buf, sizeof buf, "%03d", (int(std::lround(deg)) % 360 + 360) % 360);
        hud_.text(cx - hud_.textWidth(buf, s) / 2, y0 + ch + 6 * s, buf, kWhite, s);
    }

    // Mission name and timer, top left; stance, top right.
    {
        int secs = int(missionTime_);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%02d:%02d", secs / 60, secs % 60);
        hud_.rect(6 * s, 6 * s, 260 * s, 34 * s, kPanel);
        hud_.text(12 * s, 10 * s, mission_.title, kWhite, s);
        hud_.text(12 * s, 24 * s, buf, kGreen, s);
        const char* st = player_.fly ? "FLY" : stanceName(player_.stance);
        float tw = hud_.textWidth(st, s);
        hud_.rect(w - tw - 22 * s, 6 * s, tw + 16 * s, 20 * s, kPanel);
        hud_.text(w - tw - 14 * s, 10 * s, st, kGreen, s);
    }

    // Weapon panel, bottom left: name, mode, magazines and rounds.
    {
        const auto& d = arsenal_.def();
        const auto& sl = arsenal_.current();
        const float pw = 220 * s, ph = 50 * s, x0 = 6 * s, y0 = h - ph - 6 * s;
        hud_.rect(x0, y0, pw, ph, kPanel);
        hud_.text(x0 + 6 * s, y0 + 4 * s, d.name, kWhite, s);
        const char* mode = d.roundsPerMinute < 100 ? "BOLT" : sl.autoMode ? "AUTO" : "SEMI";
        hud_.text(x0 + pw - hud_.textWidth(mode, s) - 6 * s, y0 + 4 * s, mode, kAmber, s);
        char buf[64];
        std::snprintf(buf, sizeof buf, "MAGS %d   RDS %d", sl.mags, sl.rounds);
        hud_.text(x0 + 6 * s, y0 + 19 * s, buf, sl.rounds ? kGreen : kAmber, s);
        // Chamber indicator box, as on the original HUD.
        hud_.rect(x0 + pw - 16 * s, y0 + 20 * s, 10 * s, 10 * s, {0.6f, 0.6f, 0.6f, 0.8f});
        if (sl.rounds > 0) hud_.rect(x0 + pw - 14 * s, y0 + 22 * s, 6 * s, 6 * s, kGreen);
        std::string info = arsenal_.reloading() ? "RELOADING" : "GRID " + gridName(player_.feet, terrain_.mapExtent());
        if (arsenal_.zoom() > 1) { char z[16]; std::snprintf(z, sizeof z, "   %.0fX", arsenal_.zoom()); info += z; }
        hud_.text(x0 + 6 * s, y0 + 34 * s, info, arsenal_.reloading() ? kAmber : kWhite, s);
    }

    // Radio / information messages, bottom centre.
    {
        float y = h - 30 * s;
        for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
            float a = std::clamp(5.f - it->age, 0.f, 1.f);
            if (a <= 0) continue;
            hud_.text(cx - hud_.textWidth(it->text, s) / 2, y, it->text, {kGreen.r, kGreen.g, kGreen.b, a}, s);
            y -= 15 * s;
        }
    }

    if (showMap_) drawCommandMap(w, h, s);
    if (showGoals_) {
        const float bw = 420 * s, bh = 90 * s, x0 = cx - bw / 2, y0 = cy - bh / 2;
        hud_.rect(x0, y0, bw, bh, {0, 0, 0, 0.8f});
        hud_.text(x0 + 10 * s, y0 + 8 * s, "MISSION GOALS - " + mission_.title, kAmber, s);
        hud_.text(x0 + 10 * s, y0 + 30 * s, "Objectives are not decoded from the mission", kWhite, s);
        hud_.text(x0 + 10 * s, y0 + 45 * s, "files yet (planned for milestone 6).", kWhite, s);
        hud_.text(x0 + 10 * s, y0 + 68 * s, "G TO CLOSE", kGreen, s);
    }
    hud_.end();
}

int Game::run() {
    std::vector<uint8_t> raw;
    if (!vfs_.read("ITEMS.DEF", raw) || !items_.parse(std::string(raw.begin(), raw.end()))) {
        std::fprintf(stderr, "cannot read ITEMS.DEF\n");
        return 1;
    }
    for (auto& n : vfs_.list(".BMS")) missions_.push_back(n.substr(0, n.size() - 4));
    if (missions_.empty()) { std::fprintf(stderr, "no missions found\n"); return 1; }
    auto it = std::find(missions_.begin(), missions_.end(), toUpper(o_.mission));
    if (it == missions_.end()) { std::fprintf(stderr, "unknown mission %s\n", o_.mission.c_str()); return 1; }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const bool test = !o_.screenshot.empty();
    win_ = SDL_CreateWindow("OpenDF", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, o_.width, o_.height,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | (test ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE));
    if (!win_) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win_);
    if (!ctx) { std::fprintf(stderr, "OpenGL 3.3 context: %s\n", SDL_GetError()); SDL_DestroyWindow(win_); return 1; }
    SDL_GL_SetSwapInterval(1);
    int rc = 0;
    const char* missing = nullptr;
    std::string err;
    if (!loadGl(&missing)) { std::fprintf(stderr, "missing OpenGL function %s\n", missing); rc = 1; }
    else if (!models_.init(&err) || !hud_.init(vfs_, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); rc = 1; }
    else if (!loadMission(int(it - missions_.begin()))) rc = 1;

    if (rc == 0) {
        if (o_.hasCamera) {
            player_.feet = {o_.camera[0], 0, -o_.camera[1]};
            player_.feet.y = world_.groundHeight(player_.feet, 0.45f) + o_.camera[2];
            player_.yaw = o_.camera[3] * kPi / 180.f;
            player_.pitch = o_.camera[4] * kPi / 180.f;
            player_.fly = o_.camera[2] > 2;
        }
        if (o_.weapon >= 0) arsenal_.select(o_.weapon, 1);
        if (o_.stance >= 0) player_.stance = Stance(std::clamp(o_.stance, 0, 2));
        for (int i = 0; i < o_.zoom; ++i) arsenal_.toggleZoom();
        showMap_ = o_.showMap;

        captured_ = !test;
        SDL_SetRelativeMouseMode(captured_ ? SDL_TRUE : SDL_FALSE);
        Uint64 last = SDL_GetPerformanceCounter();
        bool running = true, prevTrigger = false;
        int frame = 0;
        float fpsTimer = 0;
        int fpsFrames = 0;
        while (running) {
            Uint64 now = SDL_GetPerformanceCounter();
            float dt = test ? 1.f / 60.f : std::min(float(now - last) / SDL_GetPerformanceFrequency(), 0.1f);
            last = now;

            SDL_Event e;
            bool zoomClick = false;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = false;
                else if (e.type == SDL_MOUSEBUTTONDOWN && !captured_) { captured_ = true; SDL_SetRelativeMouseMode(SDL_TRUE); }
                else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) zoomClick = true;
                else if (e.type == SDL_MOUSEMOTION && captured_ && !showMap_) {
                    float sens = 0.0022f / arsenal_.zoom();
                    player_.yaw += e.motion.xrel * sens;
                    player_.pitch = std::clamp(player_.pitch - e.motion.yrel * sens, -1.5f, 1.5f);
                } else if (e.type == SDL_KEYDOWN && !e.key.repeat) handleKey(e.key);
            }
            if (zoomClick) arsenal_.toggleZoom();

            const Uint8* k = SDL_GetKeyboardState(nullptr);
            PlayerInput in;
            in.forward = float((k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) - (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]));
            in.strafe = float((k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) - (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]));
            in.walk = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
            in.jump = k[SDL_SCANCODE_SPACE];
            in.up = k[SDL_SCANCODE_SPACE];
            in.down = k[SDL_SCANCODE_C] && player_.fly;
            if (o_.walkForward) in.forward = 1;
            if (showMap_) in = PlayerInput{};
            const bool trigger = !showMap_ && ((captured_ && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) ||
                                               k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL] || o_.holdFire);

            player_.update(dt, in, world_);
            std::vector<std::string> msgs;
            arsenal_.update(dt, trigger, trigger && !prevTrigger, player_, world_, msgs);
            prevTrigger = trigger;
            for (auto& m : msgs) messages_.push_back({m, 0});
            for (auto& m : messages_) m.age += dt;
            while (messages_.size() > 6 || (!messages_.empty() && messages_.front().age > 6)) messages_.pop_front();
            world_.update(dt);
            missionTime_ += dt;

            int dw, dh;
            SDL_GL_GetDrawableSize(win_, &dw, &dh);
            glViewport(0, 0, dw, dh);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            const float fov = 2 * std::atan(std::tan(30.f * kPi / 180.f) / arsenal_.zoom());
            Mat4 proj = perspective(fov, float(dw) / std::max(dh, 1), 0.1f, viewDistance_ * 1.1f);
            Player kicked = player_;
            kicked.pitch = std::clamp(player_.pitch + arsenal_.recoilPitch(), -1.5f, 1.5f);
            Mat4 view = lookDir(player_.eye(), kicked.forward(), {0, 1, 0});
            drawWorld(view, proj, dw, dh);
            drawViewModel(dw, dh);
            drawHud(proj * view, dw, dh);

            if (test && ++frame >= o_.frames) {
                writePpm(o_.screenshot, dw, dh);
                std::printf("wrote %s (%dx%d)\n", o_.screenshot.c_str(), dw, dh);
                for (auto& m : messages_) std::printf("  message: %s\n", m.text.c_str());
                std::printf("  player at map %.2f, %.2f, %.2f m above ground\n", player_.feet.x, -player_.feet.z,
                            player_.feet.y - terrain_.heightAt(player_.feet.x, player_.feet.z));
                running = false;
            }
            SDL_GL_SwapWindow(win_);

            ++fpsFrames;
            fpsTimer += dt;
            if (fpsTimer >= 0.5f) {
                char title[200];
                std::snprintf(title, sizeof title, "OpenDF - %s - %.0f fps", mission_.title.c_str(), fpsFrames / fpsTimer);
                SDL_SetWindowTitle(win_, title);
                fpsFrames = 0;
                fpsTimer = 0;
            }
        }
    }
    // GL objects must go before the context.
    world_.objects.clear();
    terrainPtr_.reset();
    modelsPtr_.reset();
    hudPtr_.reset();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win_);
    return rc;
}

} // namespace

int runGame(Vfs& vfs, const GameOptions& options) {
    Game game(vfs, options);
    return game.run();
}

} // namespace df
