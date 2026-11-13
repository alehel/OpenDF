#include "Game.h"

#include "../render/Gl.h"
#include "../render/Hud.h"
#include "../render/Models.h"
#include "../render/Terrain.h"
#include "../formats/Rtxt.h"
#include "Player.h"
#include "Weapons.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <memory>

namespace df {

namespace {

constexpr float kPi = 3.14159265f;

// HUD colours, matched to the original screen.
const Color kHudGreen{0.30f, 0.92f, 0.30f, 1};
const Color kHudLine{0.12f, 0.62f, 0.12f, 1};
const Color kHudBoxFill{0.0f, 0.12f, 0.0f, 1};
const Color kHudScan{0.0f, 0.19f, 0.0f, 1};
const Color kHudMessage{0.85f, 0.85f, 0.85f, 1};
const Color kHudBlue{0.25f, 0.4f, 1.0f, 1};
const Color kHudRed{0.95f, 0.15f, 0.1f, 1};

// The original HUD is laid out for 640x480; these are its positions in those
// units (measured from the original game). The screen is scaled by height and,
// on wide screens, the right-hand group stays anchored to the right edge
// instead of being stretched.
constexpr float kTopBar = 59, kBottomBar = 60, kTopDivider = 350, kBottomDivider = 431;

// The original's camera: 90° horizontal field of view across its 640x362 3D
// view, i.e. about 58.9° vertical (matches the first-person weapon in footage
// of the original). Wider screens keep this vertical angle and see more at
// the sides.
const float kOriginalFovY = 2.f * std::atan(1.f / (640.f / 362.f));

struct Message { std::string text; float age; };

bool project(const Mat4& vp, Vec3 p, float w, float h, float& sx, float& sy, float& depth) {
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

// The original's grid: 64 m squares, letters west→east, numbers north→south
// (e.g. "H12").
constexpr float kGridSquare = 64.f;
std::string gridName(Vec3 feet) {
    int col = std::clamp(int(feet.x / kGridSquare), 0, 25);
    int row = std::max(0, int(feet.z / kGridSquare));
    return std::string(1, char('A' + col)) + std::to_string(row + 1);
}

// "C03M03 - Blow Up C3" -> "Blow Up C3"
std::string missionName(const std::string& title) {
    auto dash = title.find(" - ");
    return dash == std::string::npos ? title : title.substr(dash + 3);
}

class Game {
public:
    Game(Vfs& vfs, const GameOptions& o) : vfs_(vfs), o_(o) {}
    int run();

private:
    struct Screen {
        float w, h, u;          // pixels; u = pixels per original 640x480 unit
        float top, bottom;      // bar heights in pixels
        float playY, playH;     // 3D view area (top-left origin)
        float lx(float x) const { return x * u; }                 // left-anchored
        float rx(float x) const { return w - (640.f - x) * u; }   // right-anchored
        float cx(float dx) const { return w * 0.5f + dx * u; }     // relative to the centre
        float y(float v) const { return v * u; }
        float by(float v) const { return h - (480.f - v) * u; }   // bottom-bar y
        // HUD text scale: the reference footage uses the 800x600 font size
        // relative to the screen, i.e. 0.8 of a 640x480 pixel.
        float text() const { return u * 0.8f; }
    };

    bool loadMission(int index);
    void handleKey(const SDL_KeyboardEvent& k);
    void drawWorld(const Mat4& view, const Mat4& proj, bool zoomView);
    void drawViewModel(const Screen& s);
    void drawZoomInset(const Screen& s, const Mat4& view, float fovY);
    void drawHud(const Screen& s, const Mat4& viewProj);
    void drawTopBar(const Screen& s);
    void drawBottomBar(const Screen& s);
    void drawCommandMap(const Screen& s);
    void hudBox(const Screen& s, float x0, float y0, float x1, float y1, bool right);
    float rangeToAim() const;
    Vec3 aimDirection() const;

    Vfs& vfs_;
    GameOptions o_;
    SDL_Window* win_ = nullptr;
    ItemDefs items_;
    std::vector<std::string> gameText_;   // DFGAME.BIN
    ExeImage exe_;                        // the original DF.EXE, for its tables
    AnimDefs playerMoves_;                // the player soldier's .ANM move table
    std::string missionName_;             // from the campaign text (DFCAMPxx.BIN)
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
    HudImage stanceIcons_, weaponIcons_;
    World world_;
    Player player_;
    Arsenal arsenal_;
    std::deque<Message> messages_;
    float missionTime_ = 0;
    float viewDistance_ = 1200;
    float mapScale_ = 1.f;   // command map zoom (- / =)
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
    // Testing: OPENDF_HEIGHTSCALE overrides metres per elevation step.
    if (const char* hs = std::getenv("OPENDF_HEIGHTSCALE")) terrain_.scale.heightScale = float(std::atof(hs));
    world_.terrain = &terrain_;
    world_.build(vfs_, models_, items_, mission_);

    player_ = Player{};
    player_.moves = &playerMoves_;
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
    arsenal_.init(gameText_, &exe_, &items_);
    // The mission's display name ("Insurrection") is in the campaign text file:
    // DFCAMPcc.BIN, entry m-1 for CccMmm.
    missionName_ = missionName(mission_.title);
    {
        int c = 0, m = 0;
        std::vector<uint8_t> camp;
        std::vector<std::string> text;
        char file[32];
        if (std::sscanf(name.c_str(), "C%dM%d", &c, &m) == 2 && m > 0) {
            std::snprintf(file, sizeof file, "DFCAMP%02d.BIN", c);
            if (vfs_.read(file, camp) && parseRtxt(camp, text) && m - 1 < int(text.size()) && !text[m - 1].empty())
                missionName_ = text[m - 1];
        }
    }
    missionTime_ = 0;
    messages_.clear();
    if (exe_.loaded() && !arsenal_.fromExe())
        std::fprintf(stderr, "DF.EXE is not the expected version; using built-in weapon values\n");
    else if (arsenal_.fromExe()) std::printf("weapon tables read from DF.EXE\n");
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
    // Stances: Delete / End / Page Down as on the original keyboard, plus X / Z toggles.
    case SDLK_DELETE: player_.stance = Stance::Stand; break;
    case SDLK_END: player_.stance = Stance::Crouch; break;
    case SDLK_PAGEDOWN: player_.stance = Stance::Prone; break;
    case SDLK_x: player_.stance = player_.stance == Stance::Crouch ? Stance::Stand : Stance::Crouch; break;
    case SDLK_z: player_.stance = player_.stance == Stance::Prone ? Stance::Stand : Stance::Prone; break;
    // Weapons as on the original keyboard: 2 sidearm, 3/4 primary modes,
    // 6 secondary. 7 and 8 pick the other primaries (pressing again cycles
    // their fire modes); Backspace cycles weapons.
    case SDLK_2: arsenal_.select(0); break;
    case SDLK_3: arsenal_.select(1, 0); break;
    case SDLK_4: arsenal_.select(1, 1); break;
    case SDLK_6: arsenal_.select(2); break;
    case SDLK_7: if (arsenal_.slotIndex() == 3) arsenal_.cycleMode(); else arsenal_.select(3); break;
    case SDLK_8: arsenal_.select(4); break;
    case SDLK_BACKSPACE: arsenal_.cycle(); break;
    case SDLK_m: case SDLK_r: arsenal_.reload(); break;           // M = "change magazine"
    case SDLK_c: showMap_ = !showMap_; showGoals_ = false; break; // command map
    case SDLK_g: showGoals_ = !showGoals_; showMap_ = false; break;
    case SDLK_MINUS: mapScale_ = std::min(4.f, mapScale_ * 1.5f); break;    // map zoom in
    case SDLK_EQUALS: mapScale_ = std::max(0.25f, mapScale_ / 1.5f); break; // map zoom out
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

Vec3 Game::aimDirection() const {
    Player kicked = player_;
    kicked.pitch = std::clamp(player_.pitch + arsenal_.recoilPitch(), -1.5f, 1.5f);
    return kicked.forward();
}

float Game::rangeToAim() const {
    Vec3 eye = player_.eye();
    RayHit hit;
    const float maxRange = 2000.f;
    if (!world_.raycast(eye, eye + aimDirection() * maxRange, hit)) return -1;
    return hit.t * maxRange;
}

void Game::drawWorld(const Mat4& view, const Mat4& proj, bool zoomView) {
    const Vec3 eye = player_.eye();
    terrain_.draw(view, proj, eye, viewDistance_);
    SceneLighting light{eye, terrain_.fogColor(), terrain_.sunDirection(), viewDistance_ * 0.35f, viewDistance_ * 0.98f};
    models_.begin(proj * view, light);
    const Vec3 fwd = aimDirection();
    const float zoom = zoomView ? arsenal_.zoom() : 1.f;
    struct Visible { const WorldObject* o; Vec3 at; int lod; };
    std::vector<Visible> visible;
    for (const auto& o : world_.objects) {
        if (o.removed) continue;
        Vec3 at = world_.nearest(o.pos, eye);
        Vec3 d = at - eye;
        float dist = std::sqrt(dot(d, d));
        if (dist > viewDistance_ + o.model->radius || dot(d, fwd) < -o.model->radius) continue;
        visible.push_back({&o, at, ModelRenderer::chooseLod(*o.model, dist / zoom)});
    }
    for (auto& v : visible) models_.draw(*v.o->model, v.lod, v.o->matrix(v.at));
    for (auto& v : visible) models_.drawTranslucent(*v.o->model, v.lod, v.o->matrix(v.at));
    models_.end();
}

void Game::drawViewModel(const Screen& s) {
    // The original hides the weapon while the zoom inset is up.
    if (!gunVisible_ || arsenal_.zoomed() || showMap_) return;
    const GpuModel* m = models_.get(vfs_, arsenal_.def().viewModel);
    if (!m) return;
    glClear(GL_DEPTH_BUFFER_BIT);

    // As in the original engine: the P* model is drawn unrotated (aligned with
    // the view) at the weapon's offset from DF.EXE's player weapon table, with
    // the same camera as the world. Offset axes are the model's own: forward,
    // left, up in 1/256 m.
    const int* g = arsenal_.def().gunOffset;
    Vec3 offset{-g[1] / 256.f, g[2] / 256.f, -g[0] / 256.f};
    // Recoil pushes the weapon back; reloading lowers it; a slight bob while moving.
    float bob = player_.moving() && !player_.fly ? std::sin(missionTime_ * 9.f) * 0.006f : 0.f;
    offset = offset + Vec3{0, bob - (arsenal_.reloading() ? 0.12f : 0.f), arsenal_.recoil() * 0.012f};
    // Model axes (+X barrel, +Y up, +Z right) to view axes (-Z ahead, +Y up, +X right).
    Mat4 base;
    base.at(0, 0) = 0; base.at(2, 0) = -1;
    base.at(0, 2) = 1; base.at(2, 2) = 0;
    Mat4 model = translate(offset) * base;

    // Same camera as the world: on wider screens the gun stays where it is
    // relative to the centre of the view (in screen heights); the extra width
    // only shows more of the world, so ultrawide screens look natural.
    Mat4 proj = perspective(kOriginalFovY, s.w / s.playH, 0.02f, 10.f);
    SceneLighting light{{0, 0, 0}, {0, 0, 0}, normalize(Vec3{0.3f, 0.9f, 0.4f}), 1e6f, 2e6f};
    models_.begin(proj, light);
    models_.draw(*m, 0, model);
    models_.drawTranslucent(*m, 0, model, arsenal_.flash() > 0);
    models_.end();
}

// The zoom view is a magnified circle on the right of the 3D view (the main
// view stays as it is), like the original.
void Game::drawZoomInset(const Screen& s, const Mat4& view, float fovY) {
    if (!arsenal_.zoomed() || showMap_) return;
    const float cx = s.cx(160), cy = s.playY + s.playH * 0.5f, r = s.y(120);

    glEnable(GL_STENCIL_TEST);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glViewport(0, 0, int(s.w), int(s.h));
    hud_.begin(int(s.w), int(s.h));
    hud_.disc(cx, cy, r);
    hud_.end();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    // Same pixel density as the main view, magnified by the weapon's zoom.
    const float size = 2 * r;
    const float insetFov = 2 * std::atan(std::tan(fovY * 0.5f) * (size / s.playH) / arsenal_.zoom());
    glViewport(int(cx - r), int(s.h - cy - r), int(size), int(size));
    glClear(GL_DEPTH_BUFFER_BIT);
    drawWorld(view, perspective(insetFov, 1.f, 0.1f, viewDistance_ * 1.1f), true);
    glDisable(GL_STENCIL_TEST);
    glViewport(0, 0, int(s.w), int(s.h));
}

void Game::hudBox(const Screen& s, float x0, float y0, float x1, float y1, bool right) {
    // Dark green box with a scanline texture and a green outline.
    const float X0 = right ? s.rx(x0) : s.lx(x0), X1 = right ? s.rx(x1) : s.lx(x1);
    const float Y0 = s.y(y0), Y1 = s.y(y1);
    hud_.rect(X0, Y0, X1 - X0, Y1 - Y0, kHudBoxFill);
    for (float yy = Y0 + s.u; yy < Y1; yy += 2 * s.u) hud_.rect(X0, yy, X1 - X0, s.u * 0.5f, kHudScan);
    const float t = std::max(1.f, s.u * 0.6f);
    hud_.rect(X0, Y0, X1 - X0, t, kHudLine);
    hud_.rect(X0, Y1 - t, X1 - X0, t, kHudLine);
    hud_.rect(X0, Y0, t, Y1 - Y0, kHudLine);
    hud_.rect(X1 - t, Y0, t, Y1 - Y0, kHudLine);
}

void Game::drawTopBar(const Screen& s) {
    const float u = s.u, t = std::max(1.f, u * 0.8f), ts = s.text();
    hud_.rect(0, 0, s.w, s.top, {0, 0, 0, 1});
    hud_.rect(0, s.top - t, s.w, t, kHudLine);
    hud_.rect(0, 0, s.w, t, kHudLine);
    hud_.rect(s.rx(kTopDivider), 0, t, s.top, kHudLine);

    // Stance icon (POS640.PCX: prone, crouch, stand from the top, 50x50 each).
    if (stanceIcons_.texture) {
        int frame = player_.stance == Stance::Prone ? 0 : player_.stance == Stance::Crouch ? 1 : 2;
        float v0 = frame / 3.f, v1 = (frame + 1) / 3.f;
        hud_.imageUV(stanceIcons_.texture, s.lx(24), s.y(6), s.y(46), s.y(46), 0, v0, 1, v1);
    }

    // Weapon line, magazines, rounds and the chamber indicator.
    const auto& sl = arsenal_.current();
    // The weapon name turns red while reloading, as in the original.
    hud_.text(s.lx(74), s.y(5), arsenal_.modeName(), arsenal_.reloading() ? kHudRed : kHudGreen, ts);
    hudBox(s, 74, 27, 136, 47, false);
    hudBox(s, 141, 27, 201, 47, false);
    // Magazine icon: a small slanted clip, then the spare magazine count.
    hud_.line(s.lx(80), s.y(42), s.lx(84), s.y(31), 3 * u, kHudGreen);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d", sl.mags);
    hud_.text(s.lx(105) - hud_.textWidth(buf, ts) / 2, s.y(31), buf, kHudGreen, ts);
    std::snprintf(buf, sizeof buf, "%d", sl.rounds);
    hud_.text(s.lx(171) - hud_.textWidth(buf, ts) / 2, s.y(31), buf, kHudGreen, ts);
    if (sl.rounds > 0 && !arsenal_.reloading()) hud_.rect(s.lx(206), s.y(27), s.y(6), s.y(20), kHudGreen);
    else hudBox(s, 206, 27, 212, 47, false);

    // Weapon silhouette (WEAP640.PCX: 13 frames of 43 px).
    if (weaponIcons_.texture) {
        const int frame = arsenal_.def().icon;
        const float frames = weaponIcons_.height / 43.f;
        float v0 = frame / frames, v1 = (frame + 1) / frames;
        float hgt = s.y(32), wid = hgt * weaponIcons_.width / 43.f;
        hud_.imageUV(weaponIcons_.texture, s.lx(232), s.y(19), wid, hgt, 0, v0, 1, v1);
    }

    // Compass: north-up ring, blue needle for your heading.
    {
        const float cx = s.rx(382), cy = s.y(29), r = s.y(20);
        hud_.ring(cx, cy, r, std::max(1.f, u * 0.8f), kHudLine);
        hud_.text(cx - hud_.textWidth("N", u * 0.6f) / 2, cy - r - s.y(8), "N", kHudGreen, u * 0.6f);
        hud_.text(cx - hud_.textWidth("S", u * 0.6f) / 2, cy + r - s.y(1), "S", kHudGreen, u * 0.6f);
        hud_.text(cx + r + s.y(2), cy - s.y(4), "E", kHudGreen, u * 0.6f);
        hud_.text(cx - r - s.y(7), cy - s.y(4), "W", kHudGreen, u * 0.6f);
        hud_.ring(cx, cy, s.y(5), std::max(1.f, u * 0.8f), kHudBlue);
        float dx = std::sin(player_.yaw), dy = -std::cos(player_.yaw);
        hud_.line(cx + dx * s.y(5), cy + dy * s.y(5), cx + dx * s.y(13), cy + dy * s.y(13), std::max(1.f, u), kHudBlue);
    }

    // Grid square, carried item, waypoint.
    hudBox(s, 419, 7, 481, 27, true);
    hudBox(s, 487, 7, 632, 27, true);
    hudBox(s, 419, 33, 632, 53, true);
    std::string grid = gridName(player_.feet);
    hud_.text(s.rx(450) - hud_.textWidth(grid, ts) / 2, s.y(11), grid, kHudGreen, ts);
    hud_.text(s.rx(491), s.y(11), "Carrying: Nothing", kHudGreen, ts);
    // Waypoint names and positions are in the .JIM files, not decoded yet.
    hud_.text(s.rx(423), s.y(37), "WP: --", kHudGreen, ts);
}

void Game::drawBottomBar(const Screen& s) {
    const float u = s.u, t = std::max(1.f, u * 0.8f), ts = s.text();
    const float y0 = s.h - s.bottom;
    hud_.rect(0, y0, s.w, s.bottom, {0, 0, 0, 1});
    hud_.rect(0, y0, s.w, t, kHudLine);
    hud_.rect(0, s.h - t, s.w, t, kHudLine);
    hud_.rect(s.rx(kBottomDivider), y0, t, s.bottom, kHudLine);

    // Radio / information messages, newest at the bottom.
    float y = y0 + s.y(6);
    size_t first = messages_.size() > 3 ? messages_.size() - 3 : 0;
    for (size_t i = first; i < messages_.size(); ++i) {
        hud_.text(s.lx(5), y, messages_[i].text, kHudMessage, ts);
        y += s.y(14);
    }

    // Operative and mission.
    hud_.text(s.rx(436), y0 + s.y(5), "OPERATIVE", kHudGreen, ts);
    hud_.text(s.rx(436), y0 + s.y(20), missionName_, kHudGreen, ts);
}

void Game::drawCommandMap(const Screen& s) {
    // Satellite map over the 3D view, centred on you; 64 m grid.
    const float x0 = 0, y0 = s.playY, w = s.w, h = s.playH;
    const float pxPerM = s.u * mapScale_;
    const float ext = terrain_.mapExtent();
    const float halfW = w / pxPerM * 0.5f, halfH = h / pxPerM * 0.5f;
    const Vec3 c = player_.feet;
    const float u0 = (c.x - halfW) / ext, u1 = (c.x + halfW) / ext;
    const float v0 = (c.z - halfH) / ext, v1 = (c.z + halfH) / ext;
    hud_.imageUV(terrain_.colorTexture(), x0, y0, w, h, u0, v0, u1, v1);

    const Color grid{0.2f, 0.95f, 0.2f, 0.85f};
    const float lineW = std::max(1.f, s.u * 0.6f);
    for (float gx = std::ceil((c.x - halfW) / kGridSquare) * kGridSquare; gx <= c.x + halfW; gx += kGridSquare) {
        float sx = x0 + (gx - (c.x - halfW)) * pxPerM;
        hud_.rect(sx, y0, lineW, h, grid);
        int col = ((int(std::floor(gx / kGridSquare)) % 16) + 16) % 16;
        std::string label(1, char('A' + col));
        hud_.text(sx + kGridSquare * pxPerM * 0.5f - hud_.textWidth(label, s.text()) / 2, y0 + s.y(2), label, kHudGreen, s.text());
    }
    for (float gz = std::ceil((c.z - halfH) / kGridSquare) * kGridSquare; gz <= c.z + halfH; gz += kGridSquare) {
        float sy = y0 + (gz - (c.z - halfH)) * pxPerM;
        hud_.rect(x0, sy, w, lineW, grid);
        int row = ((int(std::floor(gz / kGridSquare)) % 16) + 16) % 16;
        hud_.text(x0 + s.y(3), sy + kGridSquare * pxPerM * 0.5f - s.y(5), std::to_string(row + 1), kHudGreen, s.text());
    }

    // Known casualties in red; you as a green circle with a heading line.
    for (const auto& o : world_.objects) {
        if (!o.dead) continue;
        Vec3 at = world_.nearest(o.pos, c);
        float sx = x0 + w / 2 + (at.x - c.x) * pxPerM, sy = y0 + h / 2 + (at.z - c.z) * pxPerM;
        float e = s.y(3);
        hud_.line(sx - e, sy - e, sx + e, sy + e, std::max(1.f, s.u), kHudRed);
        hud_.line(sx - e, sy + e, sx + e, sy - e, std::max(1.f, s.u), kHudRed);
    }
    const float px = x0 + w / 2, py = y0 + h / 2;
    hud_.ring(px, py, s.y(4), std::max(1.f, s.u), kHudGreen);
    hud_.line(px, py, px + std::sin(player_.yaw) * s.y(12), py - std::cos(player_.yaw) * s.y(12), std::max(1.f, s.u), kHudGreen);

    // Compass rose, lower right.
    const float rx = x0 + w - s.y(50), ry = y0 + h - s.y(50), rr = s.y(30);
    hud_.line(rx, ry - rr, rx, ry + rr, std::max(1.f, s.u), kHudGreen);
    hud_.line(rx - rr, ry, rx + rr, ry, std::max(1.f, s.u), kHudGreen);
    hud_.line(rx, ry - rr, rx - s.y(5), ry - s.y(8), std::max(1.f, s.u), kHudGreen);
    hud_.line(rx, ry - rr, rx + s.y(5), ry - s.y(8), std::max(1.f, s.u), kHudGreen);
    hud_.text(rx - hud_.textWidth("N", s.text()) / 2, ry - rr - s.y(13), "N", kHudGreen, s.text());
    hud_.text(rx + rr + s.y(2), ry - s.y(6), "E", kHudGreen, s.text());
    hud_.text(rx - rr - s.y(10), ry - s.y(6), "W", kHudGreen, s.text());
}

void Game::drawHud(const Screen& s, const Mat4& viewProj) {
    hud_.begin(int(s.w), int(s.h));

    // Bullet impacts as small puffs: dust on the ground, dark on objects.
    for (const auto& i : arsenal_.impacts()) {
        float sx, sy, depth;
        if (!project(viewProj, i.pos, s.w, s.playH, sx, sy, depth)) continue;
        sy += s.playY;
        float r = std::clamp(60.f / depth, 1.5f, 18.f) * s.u * (1 + i.age * 1.5f);
        float a = std::max(0.f, 1 - i.age / 1.5f) * 0.8f;
        Color c = i.object ? Color{0.15f, 0.15f, 0.15f, a} : Color{0.72f, 0.65f, 0.52f, a};
        hud_.puff(sx, sy, r, c);
    }

    const float cx = s.w * 0.5f, cy = s.playY + s.playH * 0.5f;
    if (showMap_) drawCommandMap(s);
    else if (crosshair_) {
        // Small green cross with a centre gap.
        const float g = s.y(3), len = s.y(6), t = std::max(1.f, s.u);
        hud_.rect(cx - g - len, cy - t * 0.5f, len, t, kHudGreen);
        hud_.rect(cx + g, cy - t * 0.5f, len, t, kHudGreen);
        hud_.rect(cx - t * 0.5f, cy - g - len, t, len, kHudGreen);
        hud_.rect(cx - t * 0.5f, cy + g, t, len, kHudGreen);
    }

    // Zoom inset furniture: ring, crosshair ticks, range.
    if (arsenal_.zoomed() && !showMap_) {
        const float zx = s.cx(160), zy = cy, r = s.y(120), t = std::max(1.f, s.u);
        hud_.ring(zx, zy, r, std::max(1.5f, s.u * 1.2f), kHudGreen);
        const float in = r * 0.12f, out = r * 0.42f;
        hud_.rect(zx - out, zy - t * 0.5f, out - in, t, kHudGreen);
        hud_.rect(zx + in, zy - t * 0.5f, out - in, t, kHudGreen);
        hud_.rect(zx - t * 0.5f, zy - out, t, out - in, kHudGreen);
        hud_.rect(zx - t * 0.5f, zy + in, t, out - in, kHudGreen);
        float range = rangeToAim();
        char buf[32];
        if (range >= 0) std::snprintf(buf, sizeof buf, "%.0fm", range);
        else std::snprintf(buf, sizeof buf, "---m");
        hud_.text(zx + r * 0.22f, zy + r * 0.68f, buf, kHudGreen, s.text());
    }

    if (showGoals_) {
        const float bw = s.y(400), bh = s.y(70), x0 = cx - bw / 2, y0 = s.playY + s.y(20);
        hud_.rect(x0, y0, bw, bh, {0, 0.08f, 0, 0.8f});
        hudBox(s, x0 / s.u, y0 / s.u, (x0 + bw) / s.u, (y0 + bh) / s.u, false);
        hud_.text(x0 + s.y(10), y0 + s.y(8), "Mission Goals: " + missionName_, kHudGreen, s.text());
        hud_.text(x0 + s.y(10), y0 + s.y(28), "The goal list is not decoded from the mission files yet.", kHudMessage, s.text());
        hud_.text(x0 + s.y(10), y0 + s.y(46), "Press G to close.", kHudGreen, s.text());
    }

    drawTopBar(s);
    drawBottomBar(s);
    hud_.end();
}

int Game::run() {
    std::vector<uint8_t> raw;
    if (!vfs_.read("ITEMS.DEF", raw) || !items_.parse(std::string(raw.begin(), raw.end()))) {
        std::fprintf(stderr, "cannot read ITEMS.DEF\n");
        return 1;
    }
    for (auto& n : vfs_.list(".BMS")) missions_.push_back(n.substr(0, n.size() - 4));
    if (vfs_.read("DFGAME.BIN", raw)) parseRtxt(raw, gameText_);
    {
        // The player soldier (ITEMS.DEF "player01") names its move table.
        const ItemDef* soldier = items_.byId(kItemPlayerSoldier);
        const std::string anm = toUpper(soldier && !soldier->animDef.empty() ? soldier->animDef : "playanim") + ".ANM";
        if (!vfs_.read(anm, raw) || !playerMoves_.parse(std::string(raw.begin(), raw.end())))
            std::fprintf(stderr, "cannot read %s; using built-in movement speeds\n", anm.c_str());
    }
    {
        const auto exePath = findGameExe(o_.gameDir);
        std::string err;
        if (exePath.empty()) std::fprintf(stderr, "DF.EXE not found; using built-in weapon values\n");
        else if (!exe_.load(exePath, &err)) std::fprintf(stderr, "%s; using built-in weapon values\n", err.c_str());
    }
    if (missions_.empty()) { std::fprintf(stderr, "no missions found\n"); return 1; }
    auto it = std::find(missions_.begin(), missions_.end(), toUpper(o_.mission));
    if (it == missions_.end()) { std::fprintf(stderr, "unknown mission %s\n", o_.mission.c_str()); return 1; }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);   // zoom inset mask
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
        stanceIcons_ = hud_.loadImage(vfs_, "POS640.PCX");
        weaponIcons_ = hud_.loadImage(vfs_, "WEAP640.PCX");
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
                    const float sens = 0.0022f;
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

            player_.oneHanded = arsenal_.def().oneHanded;
            player_.update(dt, in, world_);
            std::vector<std::string> msgs;
            arsenal_.update(dt, trigger, trigger && !prevTrigger, player_, world_, msgs);
            prevTrigger = trigger;
            for (auto& m : msgs) messages_.push_back({m, 0});
            for (auto& m : messages_) m.age += dt;
            while (messages_.size() > 6 || (!messages_.empty() && messages_.front().age > 12)) messages_.pop_front();
            world_.update(dt);
            missionTime_ += dt;

            int dw, dh;
            SDL_GL_GetDrawableSize(win_, &dw, &dh);
            Screen s;
            s.w = float(dw);
            s.h = float(dh);
            s.u = s.h / 480.f;
            s.top = kTopBar * s.u;
            s.bottom = kBottomBar * s.u;
            s.playY = s.top;
            s.playH = std::max(1.f, s.h - s.top - s.bottom);

            glViewport(0, 0, dw, dh);
            glClearColor(0, 0, 0, 1);
            glClearStencil(0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            // 3D view between the two HUD bars.
            glViewport(0, int(s.bottom), dw, int(s.playH));
            const float fovY = kOriginalFovY;
            Mat4 proj = perspective(fovY, s.w / s.playH, 0.1f, viewDistance_ * 1.1f);
            Mat4 view = lookDir(player_.eye(), aimDirection(), {0, 1, 0});
            if (o_.viewModelOnly) {
                glClearColor(1, 0, 1, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                drawViewModel(s);
                glViewport(0, 0, dw, dh);
            } else {
                drawWorld(view, proj, false);
                drawViewModel(s);
                drawZoomInset(s, view, fovY);
                glViewport(0, 0, dw, dh);
                drawHud(s, proj * view);
            }

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
