#include "Controls.h"

#include <cstring>

namespace df {

namespace {

struct ActionName { Action action; const char* name; };
// DF.EXE's names for the actions OpenDF implements.
const ActionName kNames[] = {
    {Action::MoveForward, "move_forward"}, {Action::MoveBack, "move_back"}, {Action::SlideLeft, "slide_left"},
    {Action::SlideRight, "slide_right"}, {Action::TurnLeft, "turn_left"}, {Action::TurnRight, "turn_right"},
    {Action::LookUp, "look_up"}, {Action::LookDown, "look_down"}, {Action::CenterView, "center_view"},
    {Action::Jump, "move_jump"}, {Action::NoRun, "NoRun"}, {Action::Crouch, "Crouch"}, {Action::Prone, "Prone"},
    {Action::Stand, "Stand"}, {Action::Fire, "attack_1"}, {Action::Scope, "scope"}, {Action::Magazine, "magazine"},
    {Action::Cycle, "cycle"}, {Action::Weapon0, "weapon0"}, {Action::Weapon1, "weapon1"},
    {Action::Weapon2, "weapon2"}, {Action::Weapon3, "weapon3"}, {Action::Weapon4, "weapon4"},
    {Action::Weapon5, "weapon5"}, {Action::Weapon6, "weapon6"}, {Action::Weapon7, "weapon7"},
    {Action::Weapon8, "weapon8"}, {Action::Weapon9, "weapon9"}, {Action::NextWaypoint, "NextWaypoint"},
    {Action::BigMap, "BigMap"}, {Action::MissionGoals, "MissionGoals"}, {Action::MapZoomIn, "zoomin"},
    {Action::MapZoomOut, "zoomout"}, {Action::Crosshairs, "crosshairs"}, {Action::ShowGun, "showgun"},
    {Action::Binoculars, "Binoculars"}, {Action::Pause, "Pause"}, {Action::OldMessages, "OldMessages"},
    {Action::ShowFriendly, "ShowFriendly"}, {Action::LetterBox, "lbsize"}, {Action::Drop, "Drop"},
    {Action::Briefing, "briefing"}, {Action::Help, "help"},
    {Action::ScopeInc, "scopeinc"}, {Action::ScopeDec, "scopedec"}, {Action::NightVision, "NVG"},
    {Action::MapTerrain, "mapterrain"}, {Action::MapElevation, "mapelevation"},
    {Action::Escape, "escape"}, {Action::VCam, "VCam"}, {Action::PrevWaypoint, "PrevWaypoint"},
    {Action::FirstPerson, "1stPerson"}, {Action::ThirdPersonP, "3rdPersonp"}, {Action::ThirdPersonW, "3rdPersonw"},
    {Action::ThirdPersonL, "3rdPersonl"}, {Action::CameraLeft, "CameraLeft"}, {Action::CameraRight, "CameraRight"},
    {Action::CameraUp, "CameraUp"}, {Action::CameraDown, "CameraDown"}, {Action::CameraZoomIn, "CameraZoomIn"},
    {Action::CameraZoomOut, "CameraZoomOut"}, {Action::FoCamera, "fo"}, {Action::FoNext, "tab"},
    {Action::ScopeMag, "scopemag"}, {Action::AudioMacros, "AudioMacros"},
    {Action::PicInPic, "pinp"},
};

// Built-in copy of DF.EXE's defaults (virtual-key code | mouse << 16).
struct Default { const char* name; uint32_t key; };
const Default kDefaults[] = {
    {"move_forward", 0x26}, {"move_back", 0x28}, {"slide_left", 0x25}, {"slide_right", 0x27},
    {"turn_left", 0x64}, {"turn_right", 0x66}, {"look_up", 0x68}, {"look_down", 0x62}, {"center_view", 0x65},
    {"move_jump", 0x20}, {"NoRun", 0x10}, {"Crouch", 0x23}, {"Prone", 0x100022}, {"Stand", 0x2e},
    {"attack_1", 0x10011}, {"scope", 0x20053}, {"magazine", 0x4d}, {"cycle", 0x08},
    {"weapon0", 0x30}, {"weapon1", 0x31}, {"weapon2", 0x32}, {"weapon3", 0x33}, {"weapon4", 0x34},
    {"weapon5", 0x35}, {"weapon6", 0x36}, {"weapon7", 0x37}, {"weapon8", 0x38}, {"weapon9", 0x39},
    {"NextWaypoint", 0x57}, {"BigMap", 0x43}, {"MissionGoals", 0x47}, {"zoomin", 0xbb}, {"zoomout", 0xbd},
    {"crosshairs", 0x72}, {"showgun", 0x73}, {"escape", 0x1b},
    {"Binoculars", 0x42}, {"Pause", 0x13}, {"OldMessages", 0x52}, {"ShowFriendly", 0x46}, {"lbsize", 0x7b},
    {"Drop", 0x44}, {"briefing", 0x4f}, {"help", 0x70},
    {"1stPerson", 0x71}, {"3rdPersonw", 0x75}, {"3rdPersonp", 0x74}, {"3rdPersonl", 0x76},
    {"CameraLeft", 0x4c}, {"CameraRight", 0xba}, {"CameraUp", 0x50}, {"CameraDown", 0xbe},
    {"CameraZoomIn", 0xdb}, {"CameraZoomOut", 0xdd}, {"fo", 0x78}, {"tab", 0x09},
    {"mapterrain", 0x79}, {"mapelevation", 0x7a},   // F10 / F11 (GOG Df.exe 0x4fc3a8..)
};

// Left/right variants of the modifier keys count as the same key.
SDL_Scancode partner(SDL_Scancode k) {
    switch (k) {
    case SDL_SCANCODE_LSHIFT: return SDL_SCANCODE_RSHIFT;
    case SDL_SCANCODE_LCTRL: return SDL_SCANCODE_RCTRL;
    case SDL_SCANCODE_LALT: return SDL_SCANCODE_RALT;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

} // namespace

SDL_Scancode Controls::fromVirtualKey(uint32_t vk) {
    if (vk >= 'A' && vk <= 'Z') return SDL_Scancode(SDL_SCANCODE_A + int(vk - 'A'));
    if (vk >= '1' && vk <= '9') return SDL_Scancode(SDL_SCANCODE_1 + int(vk - '1'));
    if (vk == '0') return SDL_SCANCODE_0;
    if (vk >= 0x70 && vk <= 0x7b) return SDL_Scancode(SDL_SCANCODE_F1 + int(vk - 0x70));
    if (vk >= 0x61 && vk <= 0x69) return SDL_Scancode(SDL_SCANCODE_KP_1 + int(vk - 0x61));
    switch (vk) {
    case 0x60: return SDL_SCANCODE_KP_0;
    case 0x08: return SDL_SCANCODE_BACKSPACE;
    case 0x09: return SDL_SCANCODE_TAB;
    case 0x0d: return SDL_SCANCODE_RETURN;
    case 0x10: return SDL_SCANCODE_LSHIFT;
    case 0x11: return SDL_SCANCODE_LCTRL;
    case 0x12: return SDL_SCANCODE_LALT;
    case 0x13: return SDL_SCANCODE_PAUSE;
    case 0x1b: return SDL_SCANCODE_ESCAPE;
    case 0x20: return SDL_SCANCODE_SPACE;
    case 0x21: return SDL_SCANCODE_PAGEUP;
    case 0x22: return SDL_SCANCODE_PAGEDOWN;
    case 0x23: return SDL_SCANCODE_END;
    case 0x24: return SDL_SCANCODE_HOME;
    case 0x25: return SDL_SCANCODE_LEFT;
    case 0x26: return SDL_SCANCODE_UP;
    case 0x27: return SDL_SCANCODE_RIGHT;
    case 0x28: return SDL_SCANCODE_DOWN;
    case 0x2d: return SDL_SCANCODE_INSERT;
    case 0x2e: return SDL_SCANCODE_DELETE;
    case 0xbb: return SDL_SCANCODE_EQUALS;
    case 0xbd: return SDL_SCANCODE_MINUS;
    case 0x6a: return SDL_SCANCODE_KP_MULTIPLY;
    case 0x6b: return SDL_SCANCODE_KP_PLUS;
    case 0x6d: return SDL_SCANCODE_KP_MINUS;
    case 0x6e: return SDL_SCANCODE_KP_PERIOD;
    case 0x6f: return SDL_SCANCODE_KP_DIVIDE;
    case 0xba: return SDL_SCANCODE_SEMICOLON;
    case 0xbc: return SDL_SCANCODE_COMMA;
    case 0xbe: return SDL_SCANCODE_PERIOD;
    case 0xdb: return SDL_SCANCODE_LEFTBRACKET;
    case 0xdd: return SDL_SCANCODE_RIGHTBRACKET;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

uint32_t Controls::toVirtualKey(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_RSHIFT: return 0x10;
    case SDL_SCANCODE_RCTRL: return 0x11;
    case SDL_SCANCODE_RALT: return 0x12;
    case SDL_SCANCODE_KP_ENTER: return 0x0d;
    default: break;
    }
    for (uint32_t vk = 1; vk < 256; ++vk)
        if (fromVirtualKey(vk) == sc) return vk;
    return 0;
}

bool Controls::init(const ExeImage* exe) {
    auto apply = [&](const char* name, uint32_t key) {
        for (const auto& n : kNames)
            if (std::strcmp(n.name, name) == 0)
                bind_[int(n.action)] = {fromVirtualKey(key & 0xFFFF), int(key >> 16)};
    };
    for (const auto& d : kDefaults) apply(d.name, d.key);
    fromExe_ = false;
    const Df1ExeTables* t = exe ? exe->df1() : nullptr;
    if (!t) return false;
    // The table lists "move_forward" with the Up arrow; check that first.
    // CD 0x4df4f8, GOG Df.exe 0x4fb300 (ExeImage::df1()).
    const uint32_t kTable = t->keys;
    constexpr uint32_t kEntry = 44;
    std::vector<std::pair<std::string, uint32_t>> found;
    for (uint32_t i = 0; i < 160; ++i) {
        char name[33] = {};
        bool ok = true;
        for (uint32_t k = 0; k < 32 && ok; k += 4) {
            uint32_t v;
            ok = exe->u32(kTable + i * kEntry + k, v);
            std::memcpy(name + k, &v, 4);
        }
        uint32_t key = 0;
        if (!ok || !exe->u32(kTable + i * kEntry + 32, key)) break;
        if (name[0]) found.push_back({name, key});
    }
    bool sane = false;
    for (const auto& [n, k] : found)
        if (n == "move_forward" && (k & 0xFFFF) == 0x26) sane = true;
    if (!sane) return false;
    for (const auto& [n, k] : found) apply(n.c_str(), k);
    fromExe_ = true;
    return true;
}

bool Controls::initDf2(const ExeImage& exe) {
    const Df2ExeTables* t = exe.df2();
    if (!t) return false;
    // CD 0x4a5cd4 (44-byte records), GOG Df2.exe 0x4b1780 (0x70 bytes, 162
    // bindings); both 0x200 records from 0x4a5ca8 / 0x4b1710 (0x41c700 /
    // GOG 0x41e370), read here from record 1.
    const uint32_t kTable = t->keys, kEntry = t->keyRecord;
    std::vector<std::pair<std::string, uint32_t>> found;
    std::vector<uint16_t> flags;
    for (uint32_t i = 0; i < 0x1FF; ++i) {
        const std::string name = exe.str(kTable + i * kEntry + 4, 32);
        uint32_t key = 0, f = 0;
        if (!exe.u32(kTable + i * kEntry + 36, key)) break;
        if (name.empty()) {
            if (i > 0 && found.empty()) break;
            continue;
        }
        found.push_back({name, key});
        // GOG: the flags word +0x2a (0x421be0, 0x420e10, 0x41f150).
        exe.u32(kTable + i * kEntry + 40, f);
        flags.push_back(uint16_t(f >> 16));
    }
    bool sane = false;
    for (const auto& [n, k] : found)
        if (n == "move_forward" && (k & 0xFFFF) == 0x26) sane = true;
    if (!sane) return false;
    // Start from nothing: DF2's table is the whole set.
    for (auto& b : bind_) b = {};
    for (const auto& [n, k] : found)
        for (const auto& nm : kNames)
            if (n == nm.name) bind_[int(nm.action)] = {fromVirtualKey(k & 0xFFFF), int(k >> 16)};
    df2Recs_.clear();
    if (kEntry == 0x70)   // the GOG build's dispatch (the CD's 44-byte records are not checked)
        for (size_t i = 0; i < found.size(); ++i) {
            Action a = Action::Count;
            for (const auto& nm : kNames)
                if (found[i].first == nm.name) a = nm.action;
            df2Recs_.push_back({found[i].first, a, {fromVirtualKey(found[i].second & 0xFFFF), int(found[i].second >> 16)}, flags[i]});
        }
    fromExe_ = true;
    return true;
}

Action Controls::df2KeyAction(SDL_Scancode key, bool shift, bool ctrl) const {
    auto is = [&](const Binding& b) { return b.key != SDL_SCANCODE_UNKNOWN && (b.key == key || partner(b.key) == key); };
    const bool modKey = key == SDL_SCANCODE_LSHIFT || key == SDL_SCANCODE_RSHIFT || key == SDL_SCANCODE_LCTRL || key == SDL_SCANCODE_RCTRL;
    for (const Df2Rec& r : df2Recs_) {
        if (!is(r.bind) || (r.flags & 4)) continue;
        bool ok = true;
        if (r.flags & 0x40) ok = shift;
        else if (r.flags & 0x100) ok = ctrl;
        else if ((shift || ctrl) && !modKey) ok = (r.flags & 0x200) != 0;
        if (ok) return r.action;
    }
    for (const Df2Rec& r : df2Recs_)
        if (is(r.bind) && (r.flags & 4)) return r.action;
    return Action::Count;
}

Action Controls::df2MouseAction(int mask) const {
    for (const Df2Rec& r : df2Recs_)
        if (r.bind.mouse == mask && !(r.flags & 4)) return r.action;
    return Action::Count;
}

Action Controls::actionFor(SDL_Scancode key) const {
    for (int a = 0; a < int(Action::Count); ++a) {
        const SDL_Scancode k = bind_[a].key;
        if (k != SDL_SCANCODE_UNKNOWN && (k == key || partner(k) == key)) return Action(a);
    }
    return Action::Count;
}

bool Controls::down(Action a, const Uint8* keys, Uint32 mouseButtons) const {
    const Binding& b = bind_[int(a)];
    if (b.key != SDL_SCANCODE_UNKNOWN && (keys[b.key] || (partner(b.key) && keys[partner(b.key)]))) return true;
    if ((b.mouse & 1) && (mouseButtons & SDL_BUTTON_LMASK)) return true;
    if ((b.mouse & 2) && (mouseButtons & SDL_BUTTON_RMASK)) return true;
    if ((b.mouse & 0x10) && (mouseButtons & SDL_BUTTON_MMASK)) return true;   // "Mouse 3"
    return false;
}

std::string Controls::describe(Action a) const {
    const SDL_Scancode k = bind_[int(a)].key;
    return k == SDL_SCANCODE_UNKNOWN ? "-" : SDL_GetScancodeName(k);
}

void Controls::set(const std::string& name, uint32_t key) {
    for (const auto& n : kNames)
        if (name == n.name) bind_[int(n.action)] = {fromVirtualKey(key & 0xFFFF), int(key >> 16)};
    for (Df2Rec& r : df2Recs_)
        if (r.name == name) r.bind = {fromVirtualKey(key & 0xFFFF), int(key >> 16)};
}

} // namespace df
