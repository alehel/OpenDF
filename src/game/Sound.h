// Game sounds as DF.EXE plays them: the sound table (0x4EB194: id -> name),
// definitions from SNIPER.PWF (layers of variants with range, gain and
// pitch), positioned playback (0x473220: distance, terrain occlusion, pan
// from the listener's heading) and 2D playback (0x47319c), dialog speech
// from DFDLGnn.PWF, and looping sounds attached to things (LOOP.DEF).
#pragma once

#include "../audio/Audio.h"
#include "../audio/Pwf.h"
#include "../core/ExeImage.h"
#include "../render/Math.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace df {

class Vfs;
class Terrain;

class SoundSystem {
public:
    // Sound ids used by the game code (DF.EXE's table).
    enum : int {
        ExploM240HE = 0x32, ExploWater = 0x35, BulletWood = 0x36, BulletMetal = 0x37, BulletBush = 0x38,
        BulletFlesh = 0x39, BulletSand = 0x3A, BulletWater = 0x3B, BulletDirt = 0x3C, BulletStone = 0x3D,
        BulletArmor = 0x3E, BulletCanvas = 0x3F, BulletGrass = 0x40, BulletBy = 0x41, ShrapnelArmor = 0x4D,
        ShrapnelStone = 0x4E, FsWaterR = 0x88, FsWaterL = 0x89, AmmoPickup = 0x164,
    };

    bool init(Vfs& vfs, const ExeImage* exe, Audio* audio, std::string* error = nullptr);
    bool ready() const { return audio_ != nullptr; }

    // The listener (0x4735b8): position (OpenDF world, metres) and compass
    // heading (radians clockwise from north); `terrain` for occlusion.
    void setListener(Vec3 pos, float yaw, const Terrain* terrain);
    Vec3 listener() const { return listener_; }

    // 0x473220: a sound at a position. `pitch` 0 = 1.0.
    void play(int id, Vec3 at, float pitch = 0);
    // 0x47319c: a sound with no position (full volume, centred).
    void play2D(int id);
    // Range of a sound in metres (0x473ebc), for AI hearing.
    float range(int id) const;
    // A dialog line from the mission's dialog bank (centred, volume
    // dialog_volume 200/255); false if the clip is missing.
    bool loadDialogBank(Vfs& vfs, const std::string& name);
    bool playDialogLine(const std::string& clip);
    // Length of a dialog line in 60 Hz ticks (0 if missing).
    int dialogLineTicks(const std::string& clip);
    void stopAll();
    // Voice line (0x473f70): sound of `slot` (1-11) in voice set `set`
    // (table 0x4F019C, 12 per set), at 1 m above `at`.
    void voice(int set, int slot, Vec3 at);

    // Looping sounds (LOOP.DEF, 0x473960 / 0x473a8c): an emitter per thing
    // (`key`), refreshed every 8 frames: volume = min(volume x curve(d /
    // maxdist) >> 8, clip), no occlusion; the 6 loudest play.
    void setLoop(int key, int id, Vec3 at);
    void clearLoop(int key);
    void updateLoops();
    // Ends the loops not set since the previous sweep.
    void sweepLoops();
    // Table id of a sound name (case-insensitive); 0 if none.
    int idOf(const std::string& name) const;

private:
    struct LoopDef { std::string file; int volume = 255, clip = 255; float maxDistance = 100; };
    struct Emitter { int key, id; Vec3 at; bool seen; int voice; };
    std::map<std::string, LoopDef> loopDefs_;
    std::vector<Emitter> emitters_;
    Vfs* vfs_ = nullptr;
    int loopFrame_ = 0;
    int direction(Vec3 at) const;

    void start(SoundDef& def, float distance, int dir, bool positional, float callerPitch);
    float occlusion(Vec3 a, Vec3 b) const;
    uint32_t random();

    Audio* audio_ = nullptr;
    Pwf bank_, dialogs_;
    std::array<SoundDef*, 0x200> byId_{};
    std::array<std::string, 0x200> names_{};
    Vec3 listener_{0, 0, 0};
    float listenerYaw_ = 0;
    const Terrain* terrain_ = nullptr;
    uint32_t seed_ = 0x2B0749C1;
    std::vector<int> phrases_;   // 0x4F019C
};

} // namespace df
