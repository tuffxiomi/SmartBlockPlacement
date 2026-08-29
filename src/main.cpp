#include <pl/Mod.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

#include "mod_metadata.hpp"

#include <android/log.h>
#include <dlfcn.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace smartblockplacement {
namespace {

constexpr char kLogTag[] = "SmartBlockPlacement";

void log(const char* message) {
    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "%s",
        message ? message : ""
    );
}

struct BlockPos {
    int x;
    int y;
    int z;
};

struct Vec3 {
    float x;
    float y;
    float z;
};

namespace offsets {

constexpr std::size_t ActorLevel = 464;
constexpr std::size_t ActorDimension = 448;
constexpr std::size_t DimensionBlockSource = 208;

constexpr std::size_t HitType = 24;
constexpr std::size_t HitPos = 44;

} // namespace offsets

enum class Sig : std::size_t {
    NormalTick,
    GameModeUseItemOn,
    SurvivalModeUseItemOn,
    LevelGetHitResult,
    BlockSourceGetBlock,
    Count
};

constexpr const char* kPatterns[
    static_cast<std::size_t>(Sig::Count)
] = {
    // NormalTick
    "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 "
    "? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 54 D0 3B D5 "
    "F3 03 00 AA ? ? ? F9 ? ? ? F8 ? ? ? 39",

    // GameModeUseItemOn
    "? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 "
    "? ? ? A9 FD 03 00 91 ? ? ? D1 ? ? ? F9 "
    "5C D0 3B D5 F8 03 00 AA",

    // SurvivalModeUseItemOn
    "? ? ? 39 ? ? ? 34 ? ? ? 90 ? ? ? 39 ? ? ? 34 "
    "? ? ? A9 FD 03 00 91 E1 03 1F 2A ? ? ? 97 "
    "E0 03 1F 2A ? ? ? A8 C0 03 5F D6 ? ? ? 12",

    // LevelGetHitResult
    "? ? ? F9 C0 03 5F D6 ? ? ? F9 ? ? ? 14 "
    "? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 "
    "? ? ? F9 ? ? ? F9 ? ? ? B4 ? ? ? A9 "
    "F3 03 00 AA F4 03 08 AA ? ? ? B4 ? ? ? 91 "
    "? ? ? 52 ? ? ? 96 ? ? ? F9 ? ? ? F9 ? ? ? B4 "
    "? ? ? F9 ? ? ? 39 ? ? ? F9 ? ? ? 36 "
    "? ? ? A9 ? ? ? A9 ? ? ? A8 C0 03 5F D6 "
    "? ? ? 96 ? ? ? 91",

    // BlockSourceGetBlock
    "? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 "
    "56 D0 3B D5 ? ? ? F9 ? ? ? F9 ? ? ? B9 "
    "? ? ? 79 1F 01 09 6B ? ? ? 54 ? ? ? 79 "
    "F3 03 00 AA 1F 01 09 6B ? ? ? 54 E0 03 00 91"
};

using AddressMap = std::unordered_map<std::string, std::uintptr_t>;

AddressMap gResolved;

std::uintptr_t resolve(Sig id) {
    const auto index = static_cast<std::size_t>(id);

    if (index >= static_cast<std::size_t>(Sig::Count)) {
        return 0;
    }

    const auto it = gResolved.find(kPatterns[index]);

    if (it == gResolved.end()) {
        return 0;
    }

    return it->second;
}

bool resolveSignatures() {
    std::vector<std::string> patterns;

    patterns.reserve(
        static_cast<std::size_t>(Sig::Count)
    );

    for (const char* pattern : kPatterns) {
        patterns.emplace_back(pattern);
    }

    const auto result =
        pl::memory::resolveSignatures(
            patterns,
            "libminecraftpe.so"
        );

    gResolved.clear();

    for (const auto& [pattern, address] : result) {
        if (address != 0) {
            gResolved.emplace(pattern, address);
        }
    }

    return
        gResolved.size() ==
        static_cast<std::size_t>(Sig::Count);
}

struct HookState {
    void* target = nullptr;
    void* detour = nullptr;
};

bool installHook(
    std::uintptr_t target,
    void* detour,
    void** original,
    HookState& state
) {
    if (target == 0 || detour == nullptr || original == nullptr) {
        return false;
    }

    const int result =
        pl::memory::hook(
            reinterpret_cast<void*>(target),
            detour,
            original
        );

    if (result != 0) {
        return false;
    }

    state.target =
        reinterpret_cast<void*>(target);

    state.detour = detour;

    return true;
}

void removeHook(HookState& state) {
    if (
        state.target == nullptr ||
        state.detour == nullptr
    ) {
        return;
    }

    pl::memory::unhook(
        state.target,
        state.detour
    );

    state = {};
}

using NormalTickFn =
    void (*)(void*);

using UseItemOnFn =
    std::uint8_t (*)(
        void*,
        void*,
        const void*,
        std::uint8_t,
        const void*,
        const void*,
        bool
    );

using GetHitResultFn =
    void* (*)(void*);

using GetBlockFn =
    const void* (*)(
        void*,
        const BlockPos*,
        std::int32_t
    );

using DlopenFn =
    void* (*)(const char*, int);

NormalTickFn
    gNormalTickOriginal = nullptr;

UseItemOnFn
    gGameModeUseItemOnOriginal = nullptr;

UseItemOnFn
    gSurvivalModeUseItemOnOriginal = nullptr;

GetHitResultFn
    gGetHitResult = nullptr;

GetBlockFn
    gGetBlock = nullptr;

DlopenFn
    gDlopenOriginal = nullptr;

HookState gTickHook;
HookState gGameModeUseHook;
HookState gSurvivalUseHook;
HookState gDlopenHook;

std::mutex gStateMutex;

std::atomic<bool>
    gEnabled{false};

std::atomic<bool>
    gInstalled{false};

void* gLocalPlayer = nullptr;
void* gLastGameMode = nullptr;
void* gLastItem = nullptr;

UseItemOnFn
    gLastUseFn = nullptr;

std::uint8_t
    gLastFace = 1;

BlockPos
    gLastTarget{};

bool
    gHasLastTarget = false;

std::uint64_t
    gLastPlacementFrame = 0;

std::uint64_t
    gFrameCounter = 0;

thread_local bool
    gAutomaticCall = false;

bool samePos(
    const BlockPos& a,
    const BlockPos& b
) {
    return
        a.x == b.x &&
        a.y == b.y &&
        a.z == b.z;
}

BlockPos hitBlockPosition(
    const Vec3& hit
) {
    return {
        static_cast<int>(std::floor(hit.x)),
        static_cast<int>(std::floor(hit.y)),
        static_cast<int>(std::floor(hit.z))
    };
}

std::uint8_t inferFace(
    const Vec3& hit,
    const BlockPos& block,
    std::uint8_t fallback
) {
    const float fx =
        hit.x -
        static_cast<float>(block.x);

    const float fy =
        hit.y -
        static_cast<float>(block.y);

    const float fz =
        hit.z -
        static_cast<float>(block.z);

    float best = 1000.0f;

    std::uint8_t face = fallback;

    auto choose =
        [&](float distance, std::uint8_t candidate) {
            if (distance < best) {
                best = distance;
                face = candidate;
            }
        };

    // Down
    choose(
        std::fabs(fy),
        0
    );

    // Up
    choose(
        std::fabs(1.0f - fy),
        1
    );

    // North
    choose(
        std::fabs(fz),
        2
    );

    // South
    choose(
        std::fabs(1.0f - fz),
        3
    );

    // West
    choose(
        std::fabs(fx),
        4
    );

    // East
    choose(
        std::fabs(1.0f - fx),
        5
    );

    return face;
}

void* getBlockSource(
    void* player
) {
    if (player == nullptr) {
        return nullptr;
    }

    auto dimension =
        *reinterpret_cast<void**>(
            reinterpret_cast<std::uintptr_t>(player) +
            offsets::ActorDimension
        );

    if (dimension == nullptr) {
        return nullptr;
    }

    return
        *reinterpret_cast<void**>(
            reinterpret_cast<std::uintptr_t>(dimension) +
            offsets::DimensionBlockSource
        );
}

void updatePlacementState(
    void* gameMode,
    void* item,
    const void* position,
    std::uint8_t face,
    const void* hit
) {
    if (
        gameMode == nullptr ||
        item == nullptr ||
        hit == nullptr
    ) {
        return;
    }

    const auto* hitBytes =
        reinterpret_cast<const std::uint8_t*>(hit);

    const int type =
        *reinterpret_cast<const int*>(
            hitBytes + offsets::HitType
        );

    // Block/tile hit.
    if (type != 0) {
        return;
    }

    const Vec3 hitPos =
        *reinterpret_cast<const Vec3*>(
            hitBytes + offsets::HitPos
        );

    const BlockPos target =
        hitBlockPosition(hitPos);

    std::lock_guard lock(gStateMutex);

    gLastGameMode = gameMode;
    gLastItem = item;

    gLastUseFn = nullptr;
    gLastFace = face;

    if (position != nullptr) {
        const auto supplied =
            *reinterpret_cast<const BlockPos*>(position);

        if (
            std::abs(supplied.x - target.x) <= 1 &&
            std::abs(supplied.y - target.y) <= 1 &&
            std::abs(supplied.z - target.z) <= 1
        ) {
            gLastTarget = supplied;
        } else {
            gLastTarget = target;
        }
    } else {
        gLastTarget = target;
    }

    gHasLastTarget = true;
    gLastPlacementFrame = gFrameCounter;
}

std::uint8_t gameModeUseItemOnHook(
    void* gameMode,
    void* item,
    const void* position,
    std::uint8_t face,
    const void* hit,
    const void* block,
    bool firstEvent
) {
    const auto original =
        gGameModeUseItemOnOriginal;

    const auto result =
        original
            ? original(
                  gameMode,
                  item,
                  position,
                  face,
                  hit,
                  block,
                  firstEvent
              )
            : 0;

    if (
        gEnabled.load(
            std::memory_order_acquire
        ) &&
        !gAutomaticCall &&
        firstEvent
    ) {
        if (
            (result & 1u) != 0 ||
            (result & 2u) != 0
        ) {
            updatePlacementState(
                gameMode,
                item,
                position,
                face,
                hit
            );

            std::lock_guard lock(gStateMutex);

            gLastUseFn =
                gGameModeUseItemOnOriginal;
        }
    }

    return result;
}

std::uint8_t survivalModeUseItemOnHook(
    void* gameMode,
    void* item,
    const void* position,
    std::uint8_t face,
    const void* hit,
    const void* block,
    bool firstEvent
) {
    const auto original =
        gSurvivalModeUseItemOnOriginal;

    const auto result =
        original
            ? original(
                  gameMode,
                  item,
                  position,
                  face,
                  hit,
                  block,
                  firstEvent
              )
            : 0;

    if (
        gEnabled.load(
            std::memory_order_acquire
        ) &&
        !gAutomaticCall &&
        firstEvent
    ) {
        if (
            (result & 1u) != 0 ||
            (result & 2u) != 0
        ) {
            updatePlacementState(
                gameMode,
                item,
                position,
                face,
                hit
            );

            std::lock_guard lock(gStateMutex);

            gLastUseFn =
                gSurvivalModeUseItemOnOriginal;
        }
    }

    return result;
}

void tryAutomaticPlacement() {
    if (
        !gEnabled.load(
            std::memory_order_acquire
        )
    ) {
        return;
    }

    if (
        gGetHitResult == nullptr ||
        gLocalPlayer == nullptr
    ) {
        return;
    }

    void* level =
        *reinterpret_cast<void**>(
            reinterpret_cast<std::uintptr_t>(
                gLocalPlayer
            ) +
            offsets::ActorLevel
        );

    if (level == nullptr) {
        return;
    }

    void* hit =
        gGetHitResult(level);

    if (hit == nullptr) {
        return;
    }

    const auto* hitBytes =
        reinterpret_cast<const std::uint8_t*>(hit);

    const int type =
        *reinterpret_cast<const int*>(
            hitBytes + offsets::HitType
        );

    if (type != 0) {
        return;
    }

    const Vec3 hitPos =
        *reinterpret_cast<const Vec3*>(
            hitBytes + offsets::HitPos
        );

    const BlockPos target =
        hitBlockPosition(hitPos);

    void* gameMode = nullptr;
    void* item = nullptr;
    UseItemOnFn useFn = nullptr;

    BlockPos previous{};
    bool havePrevious = false;

    std::uint64_t lastFrame = 0;
    std::uint8_t fallbackFace = 1;

    {
        std::lock_guard lock(gStateMutex);

        gameMode = gLastGameMode;
        item = gLastItem;
        useFn = gLastUseFn;

        previous = gLastTarget;
        havePrevious = gHasLastTarget;

        lastFrame = gLastPlacementFrame;
        fallbackFace = gLastFace;
    }

    if (
        gameMode == nullptr ||
        item == nullptr ||
        useFn == nullptr ||
        !havePrevious
    ) {
        return;
    }

    if (
        gFrameCounter <= lastFrame ||
        gFrameCounter - lastFrame > 18
    ) {
        return;
    }

    if (
        samePos(target, previous)
    ) {
        return;
    }

    void* region =
        getBlockSource(gLocalPlayer);

    if (
        region == nullptr ||
        gGetBlock == nullptr
    ) {
        return;
    }

    const std::uint8_t face =
        inferFace(
            hitPos,
            target,
            fallbackFace
        );

    const void* targetBlock =
        gGetBlock(
            region,
            &target,
            0
        );

    if (targetBlock == nullptr) {
        return;
    }

    gAutomaticCall = true;

    const auto result =
        useFn(
            gameMode,
            item,
            &target,
            face,
            hit,
            targetBlock,
            true
        );

    gAutomaticCall = false;

    if (
        (result & 1u) == 0 &&
        (result & 2u) == 0
    ) {
        return;
    }

    std::lock_guard lock(gStateMutex);

    gLastTarget = target;
    gLastFace = face;
    gLastPlacementFrame = gFrameCounter;
}

void normalTickHook(
    void* actor
) {
    gLocalPlayer = actor;

    if (gNormalTickOriginal) {
        gNormalTickOriginal(actor);
    }

    ++gFrameCounter;

    tryAutomaticPlacement();
}

void resetPlacementState() {
    std::lock_guard lock(gStateMutex);

    gLocalPlayer = nullptr;
    gLastGameMode = nullptr;
    gLastItem = nullptr;
    gLastUseFn = nullptr;

    gHasLastTarget = false;
    gLastPlacementFrame = 0;
}

bool installGameHooks() {
    if (
        gInstalled.load(
            std::memory_order_acquire
        )
    ) {
        return true;
    }

    if (!resolveSignatures()) {
        log(
            "required signatures were not all resolved"
        );
        return false;
    }

    gGetHitResult =
        reinterpret_cast<GetHitResultFn>(
            resolve(Sig::LevelGetHitResult)
        );

    gGetBlock =
        reinterpret_cast<GetBlockFn>(
            resolve(Sig::BlockSourceGetBlock)
        );

    bool ok = true;

    ok =
        installHook(
            resolve(Sig::NormalTick),
            reinterpret_cast<void*>(
                normalTickHook
            ),
            reinterpret_cast<void**>(
                &gNormalTickOriginal
            ),
            gTickHook
        ) &&
        ok;

    ok =
        installHook(
            resolve(Sig::GameModeUseItemOn),
            reinterpret_cast<void*>(
                gameModeUseItemOnHook
            ),
            reinterpret_cast<void**>(
                &gGameModeUseItemOnOriginal
            ),
            gGameModeUseHook
        ) &&
        ok;

    ok =
        installHook(
            resolve(
                Sig::SurvivalModeUseItemOn
            ),
            reinterpret_cast<void*>(
                survivalModeUseItemOnHook
            ),
            reinterpret_cast<void**>(
                &gSurvivalModeUseItemOnOriginal
            ),
            gSurvivalUseHook
        ) &&
        ok;

    if (!ok) {
        removeHook(gTickHook);
        removeHook(gGameModeUseHook);
        removeHook(gSurvivalUseHook);

        gNormalTickOriginal = nullptr;
        gGameModeUseItemOnOriginal = nullptr;
        gSurvivalModeUseItemOnOriginal = nullptr;

        return false;
    }

    gInstalled.store(
        true,
        std::memory_order_release
    );

    log("hooks installed");

    return true;
}

void uninstallGameHooks() {
    if (
        !gInstalled.exchange(
            false,
            std::memory_order_acq_rel
        )
    ) {
        return;
    }

    removeHook(gTickHook);
    removeHook(gGameModeUseHook);
    removeHook(gSurvivalUseHook);

    gNormalTickOriginal = nullptr;
    gGameModeUseItemOnOriginal = nullptr;
    gSurvivalModeUseItemOnOriginal = nullptr;

    gGetHitResult = nullptr;
    gGetBlock = nullptr;

    resetPlacementState();
}

void* dlopenHook(
    const char* filename,
    int flags
) {
    void* handle =
        gDlopenOriginal
            ? gDlopenOriginal(filename, flags)
            : nullptr;

    if (
        handle != nullptr &&
        filename != nullptr &&
        std::strstr(
            filename,
            "libminecraftpe.so"
        )
    ) {
        if (
            gEnabled.load(
                std::memory_order_acquire
            )
        ) {
            installGameHooks();
        }
    }

    return handle;
}

bool installDlopenHook() {
    if (gDlopenHook.target != nullptr) {
        return true;
    }

    void* libdl =
        dlopen(
            "libdl.so",
            RTLD_NOW | RTLD_NOLOAD
        );

    if (!libdl) {
        libdl =
            dlopen(
                "libdl.so",
                RTLD_NOW
            );
    }

    if (!libdl) {
        return false;
    }

    void* symbol =
        dlsym(
            libdl,
            "dlopen"
        );

    if (!symbol) {
        dlclose(libdl);
        return false;
    }

    const bool ok =
        installHook(
            reinterpret_cast<std::uintptr_t>(
                symbol
            ),
            reinterpret_cast<void*>(
                dlopenHook
            ),
            reinterpret_cast<void**>(
                &gDlopenOriginal
            ),
            gDlopenHook
        );

    dlclose(libdl);

    return ok;
}

void uninstallDlopenHook() {
    removeHook(gDlopenHook);
    gDlopenOriginal = nullptr;
}

void tryInstallNow() {
    void* minecraft =
        dlopen(
            "libminecraftpe.so",
            RTLD_NOW | RTLD_NOLOAD
        );

    if (minecraft) {
        installGameHooks();
        dlclose(minecraft);
    }
}

class SmartBlockPlacementMod {
public:
    static SmartBlockPlacementMod& instance() {
        static SmartBlockPlacementMod mod;
        return mod;
    }

    bool load(
        pl::mod::ModContext&
    ) {
        log(
            "loading SmartBlockPlacement"
        );

        return true;
    }

    bool enable(
        pl::mod::ModContext&
    ) {
        gEnabled.store(
            true,
            std::memory_order_release
        );

        installDlopenHook();
        tryInstallNow();

        return true;
    }

    bool disable(
        pl::mod::ModContext&
    ) {
        gEnabled.store(
            false,
            std::memory_order_release
        );

        resetPlacementState();

        return true;
    }

    bool unload(
        pl::mod::ModContext&
    ) {
        gEnabled.store(
            false,
            std::memory_order_release
        );

        uninstallGameHooks();
        uninstallDlopenHook();

        return true;
    }
};

} // anonymous namespace

PL_REGISTER_MOD(
    SmartBlockPlacementMod,
    SmartBlockPlacementMod::instance()
)

} // namespace smartblockplacement
