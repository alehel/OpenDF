#include "Mnu.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace df {

std::string upperCase(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

namespace {

// strtok over `s` from `pos` with the delimiter set `delims`.
std::string nextToken(const std::string& s, size_t& pos, const char* delims) {
    while (pos < s.size() && std::strchr(delims, s[pos])) ++pos;
    const size_t start = pos;
    while (pos < s.size() && !std::strchr(delims, s[pos])) ++pos;
    return s.substr(start, pos - start);
}

int toInt(const std::string& s) { return std::atoi(s.c_str()); }   // atoi: "200x" = 200

} // namespace

MnuButton* Mnu::button(const std::string& name) {
    auto it = buttons.find(upperCase(name));
    return it == buttons.end() ? nullptr : &it->second;
}

const MnuMenu* Mnu::menu(const std::string& name) const {
    auto it = menus.find(upperCase(name));
    return it == menus.end() ? nullptr : &it->second;
}

bool Mnu::parse(const std::string& source, const TextTable& menuText) {
    std::string text = upperCase(source);
    for (auto& c : text)
        if (c == '\t') c = ' ';
    std::istringstream in(text);
    std::string line, section;
    const char* delims = ", \n\r";
    // Records are kept per section and parsed in DF.EXE's order (colours,
    // fonts, images, buttons, menus, menu buttons).
    std::map<std::string, std::vector<std::string>> records;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '[') {
            const size_t e = line.find(']');
            section = line.substr(1, e == std::string::npos ? std::string::npos : e - 1);
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        if (key.empty() || key[0] == ';') continue;
        std::string value = line.substr(eq + 1);
        if (const size_t sc = value.find(';'); sc != std::string::npos) value.resize(sc);
        records[section + "/" + key].push_back(value);
    }
    auto each = [&](const char* sectionKey, auto fn) {
        for (const auto& v : records[sectionKey]) fn(v);
    };
    auto rgb = [&](const std::string& v, bool alpha) {
        size_t p = 0;
        const std::string name = nextToken(v, p, delims);
        const int r = toInt(nextToken(v, p, delims)), g = toInt(nextToken(v, p, delims)), b = toInt(nextToken(v, p, delims));
        const int a = alpha ? toInt(nextToken(v, p, delims)) : 255;
        colors[name] = uint32_t(a & 255) << 24 | uint32_t(r & 255) << 16 | uint32_t(g & 255) << 8 | uint32_t(b & 255);
    };
    each("RGBAS/RGBA", [&](const std::string& v) { rgb(v, true); });
    each("RGBS/RGB", [&](const std::string& v) { rgb(v, false); });
    each("FONTS/FONT", [&](const std::string& v) {
        // Name, File, FG normal, BG normal, FG lit, BG lit, FG disabled, BG
        // disabled: only the three foregrounds are kept (0x40632c).
        size_t p = 0;
        MnuFont f;
        const std::string name = nextToken(v, p, delims);
        f.file = nextToken(v, p, delims);
        std::string c[6];
        for (auto& x : c) x = nextToken(v, p, delims);
        auto col = [&](const std::string& n, uint32_t def) { auto it = colors.find(n); return it == colors.end() ? def : it->second; };
        f.fg[0] = col(c[0], 0xFF00FF00);
        f.fg[1] = col(c[2], f.fg[0]);
        f.fg[2] = col(c[4], f.fg[0]);
        fonts[name] = f;
    });
    each("IMAGES/IMAGE", [&](const std::string& v) {
        size_t p = 0;
        const std::string name = nextToken(v, p, delims);
        images[name] = nextToken(v, p, delims);
    });
    each("BUTTONS/BUTTON", [&](const std::string& v) {
        // name, type, attrib, textid (",\n" delimiters), act_cb, link, ho_cb,
        // x, y, font, image, slidersize, sliderpos, width, height, state,
        // drawstate, hotkey (discarded), userid, userdraw (0x405a54).
        size_t p = 0;
        MnuButton b;
        b.name = nextToken(v, p, delims);
        b.type = toInt(nextToken(v, p, delims));
        b.attrib = toInt(nextToken(v, p, delims));
        b.textId = nextToken(v, p, ",\n");
        while (!b.textId.empty() && b.textId.front() == ' ') b.textId.erase(b.textId.begin());
        while (!b.textId.empty() && b.textId.back() == ' ') b.textId.pop_back();
        b.callback = nextToken(v, p, delims);
        b.link = nextToken(v, p, delims);
        nextToken(v, p, delims);   // ho_cb: never called
        b.x = toInt(nextToken(v, p, delims));
        b.y = toInt(nextToken(v, p, delims));
        b.font = nextToken(v, p, delims);
        b.image = nextToken(v, p, delims);
        b.sliderSize = toInt(nextToken(v, p, delims));
        b.sliderPos = toInt(nextToken(v, p, delims));
        b.width = toInt(nextToken(v, p, delims));
        b.height = toInt(nextToken(v, p, delims));
        b.state = toInt(nextToken(v, p, delims));
        b.drawstate = toInt(nextToken(v, p, delims));
        nextToken(v, p, delims);   // hotkey: discarded
        b.userid = toInt(nextToken(v, p, delims));
        b.userdraw = nextToken(v, p, delims);
        for (std::string* s : {&b.callback, &b.link, &b.font, &b.image, &b.userdraw})
            if (*s == "NULL") s->clear();
        if (!b.textId.empty() && b.textId != "NULL")
            if (const std::string* t = menuText.find("GroupMainMenu", b.textId)) b.text = *t;
        // "{hot}X": the marker removed, X the hot key (0x405b5a).
        const size_t h = upperCase(b.text).find("{HOT}");
        if (h != std::string::npos) {
            b.text.erase(h, 5);
            if (h < b.text.size()) {
                b.hot = char(std::toupper(static_cast<unsigned char>(b.text[h])));
                b.hotPos = int(h);
            }
        }
        if (!buttons.count(b.name)) buttons[b.name] = b;   // a duplicate is dropped
    });
    each("MENUS/MENU", [&](const std::string& v) {
        // name, bg image, font, accept func, accept button, back func, back
        // button, up, down, left, right, group (0x405344).
        size_t p = 0;
        MnuMenu m;
        m.name = nextToken(v, p, delims);
        m.background = nextToken(v, p, delims);
        nextToken(v, p, delims);   // font: not used
        nextToken(v, p, delims);   // accept func: always NULL
        m.accept = nextToken(v, p, delims);
        nextToken(v, p, delims);   // back func: never called
        m.back = nextToken(v, p, delims);
        m.up = nextToken(v, p, delims);
        m.down = nextToken(v, p, delims);
        m.left = nextToken(v, p, delims);
        m.right = nextToken(v, p, delims);
        for (std::string* s : {&m.background, &m.accept, &m.back, &m.up, &m.down, &m.left, &m.right})
            if (*s == "NULL") s->clear();
        menus[m.name] = m;
    });
    each("MENUBUTTONS/MENUBUTTON", [&](const std::string& v) {
        size_t p = 0;
        const std::string menuName = nextToken(v, p, delims);
        auto it = menus.find(menuName);
        for (std::string t = nextToken(v, p, delims); !t.empty() && t != ".STOP"; t = nextToken(v, p, delims))
            if (it != menus.end()) it->second.buttons.push_back(t);
    });
    return !menus.empty();
}

} // namespace df
