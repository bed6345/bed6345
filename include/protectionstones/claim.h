// ProtectionStones for Bedrock — Claim data model
//
// A "claim" is a protected region centred on a special block. The region is an
// axis-aligned box on the X/Z plane (radius blocks in each direction from the
// centre) and spans the full Y column of the world. Everything here is plain
// data with no EndStone dependency so it can be unit-tested in isolation and
// kept cheap to copy/look up on the event hot path.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace ps {

/// Membership tiers stored on a claim.
enum class MemberLevel {
    Guest,   ///< may interact (doors/chests/buttons) but cannot build
    Member,  ///< full build rights inside the claim
};

/// The claim sizes, represented in-game by vanilla blocks.
enum class ClaimSize {
    Small,   ///< Iron Block      — radius 10  (~20×20)
    Medium,  ///< Gold Block      — radius 25  (~50×50)
    Large,   ///< Diamond Block   — radius 50  (~100×100)
    Admin,   ///< Netherite Block  — radius 250 (~500×500), OP-only
};

/// A single member entry. Keyed by XUID elsewhere; the name is only for display
/// and is refreshed every time the player is seen online.
struct MemberInfo {
    MemberLevel level{MemberLevel::Member};
    std::string name;
};

/// Per-claim protection flags. Defaults deny as much as possible for safety.
struct ClaimFlags {
    bool build{true};            ///< members may build (others/guests may not)
    bool interact{true};         ///< guests may use doors/chests/buttons
    bool pvp{false};             ///< player-vs-player damage allowed
    bool explosion{false};       ///< explosions may break blocks inside
    bool fire{false};            ///< fire may spread / liquids may flow in
    bool entity_protect{true};   ///< protect pets / armor stands / item frames
    bool lava{false};            ///< RESERVED: full lava-flow protection (TODO)
};

/// A protected region.
struct Claim {
    std::uint64_t id{0};
    std::string owner_xuid;
    std::string owner_name;  ///< display only
    std::unordered_map<std::string, MemberInfo> members;  ///< xuid -> info

    std::string dimension;  ///< dimension name, used as part of the index key
    int center_x{0};
    int center_y{0};
    int center_z{0};

    ClaimSize size{ClaimSize::Small};
    int radius{10};
    ClaimFlags flags;

    // --- AABB helpers (X/Z only; Y is the full world column) ---
    [[nodiscard]] int minX() const { return center_x - radius; }
    [[nodiscard]] int maxX() const { return center_x + radius; }
    [[nodiscard]] int minZ() const { return center_z - radius; }
    [[nodiscard]] int maxZ() const { return center_z + radius; }

    /// True if the X/Z column falls inside this claim. Hot-path, branch-light.
    [[nodiscard]] bool contains(int x, int z) const noexcept
    {
        return x >= center_x - radius && x <= center_x + radius && z >= center_z - radius &&
               z <= center_z + radius;
    }

    /// True if the given block coordinates are exactly the centre block.
    [[nodiscard]] bool isCenter(int x, int y, int z) const noexcept
    {
        return x == center_x && y == center_y && z == center_z;
    }
};

/// Block radius for a given size.
inline int radiusForSize(ClaimSize size) noexcept
{
    switch (size) {
    case ClaimSize::Small:
        return 10;
    case ClaimSize::Medium:
        return 25;
    case ClaimSize::Large:
        return 50;
    case ClaimSize::Admin:
        return 250;
    }
    return 10;
}

/// Stable lowercase identifier for a size (used in JSON and lang keys).
inline const char *sizeKey(ClaimSize size) noexcept
{
    switch (size) {
    case ClaimSize::Small:
        return "small";
    case ClaimSize::Medium:
        return "medium";
    case ClaimSize::Large:
        return "large";
    case ClaimSize::Admin:
        return "admin";
    }
    return "small";
}

/// The vanilla block type that represents a given size.
inline const char *blockTypeForSize(ClaimSize size) noexcept
{
    switch (size) {
    case ClaimSize::Small:
        return "minecraft:iron_block";
    case ClaimSize::Medium:
        return "minecraft:gold_block";
    case ClaimSize::Large:
        return "minecraft:diamond_block";
    case ClaimSize::Admin:
        return "minecraft:netherite_block";
    }
    return "minecraft:iron_block";
}

/// Maps a placed block type back to a claim size, if it is a protection block.
/// Returns true and writes *out on success.
inline bool sizeForBlockType(const std::string &type, ClaimSize *out) noexcept
{
    if (type == "minecraft:iron_block") {
        *out = ClaimSize::Small;
        return true;
    }
    if (type == "minecraft:gold_block") {
        *out = ClaimSize::Medium;
        return true;
    }
    if (type == "minecraft:diamond_block") {
        *out = ClaimSize::Large;
        return true;
    }
    if (type == "minecraft:netherite_block") {
        *out = ClaimSize::Admin;
        return true;
    }
    return false;
}

}  // namespace ps
