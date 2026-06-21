// ProtectionStones for Bedrock — ClaimManager
//
// Owns every claim plus the chunk-based spatial index that makes per-event
// permission checks O(1)-ish instead of looping over all claims. Also handles
// loading/saving claims as JSON and rebuilding the index on load.
//
// Index design:
//   dimension name -> ( packed chunk key -> list of claim ids )
// A claim's AABB usually spans several chunks, so its id is registered in every
// chunk it overlaps. A point lookup hashes straight to the (small) candidate
// list for that one chunk; we then AABB-test only those few candidates.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "protectionstones/claim.h"

namespace ps {

class ClaimManager {
public:
    explicit ClaimManager(std::filesystem::path data_file);

    // --- persistence ---
    /// Loads claims from disk (if the file exists) and rebuilds the index.
    void load();
    /// Atomically writes all claims to disk.
    void save() const;

    // --- hot-path queries ---
    /// Returns the claim covering the given X/Z column, or nullptr. O(1)-ish.
    [[nodiscard]] Claim *getClaimAt(const std::string &dimension, int x, int z) noexcept;
    [[nodiscard]] const Claim *getClaimAt(const std::string &dimension, int x, int z) const noexcept;

    // --- creation / removal ---
    /// True if a radius-r box centred at (cx,cz) in dimension overlaps any claim.
    [[nodiscard]] bool overlaps(const std::string &dimension, int cx, int cz, int radius) const noexcept;

    /// Creates and indexes a claim. Caller must have checked overlap/limits.
    /// Returns a pointer to the stored claim (stable for the claim's lifetime).
    Claim *createClaim(const std::string &owner_xuid, const std::string &owner_name,
                       const std::string &dimension, int cx, int cy, int cz, ClaimSize size);

    /// Removes a claim by id. Returns true if it existed.
    bool removeClaim(std::uint64_t id);

    // --- lookups ---
    [[nodiscard]] Claim *getClaim(std::uint64_t id) noexcept;
    [[nodiscard]] int countClaimsByOwner(const std::string &xuid) const noexcept;
    [[nodiscard]] std::vector<Claim *> getClaimsByOwner(const std::string &xuid);

    [[nodiscard]] std::size_t size() const noexcept { return claims_.size(); }
    [[nodiscard]] const std::unordered_map<std::uint64_t, Claim> &all() const noexcept { return claims_; }

    /// Refreshes the cached display name for owner/member entries matching xuid.
    /// Returns true if anything changed (so the caller can decide to persist).
    bool refreshDisplayName(const std::string &xuid, const std::string &name);

private:
    void indexClaim(const Claim &claim);
    void unindexClaim(const Claim &claim);

    /// Packs a chunk coordinate pair into a single 64-bit key.
    static std::uint64_t packChunk(int cx, int cz) noexcept
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32) |
               static_cast<std::uint32_t>(cz);
    }

    std::unordered_map<std::uint64_t, Claim> claims_;
    std::unordered_map<std::string, std::unordered_map<std::uint64_t, std::vector<std::uint64_t>>> index_;
    std::uint64_t next_id_{1};
    std::filesystem::path data_file_;
};

}  // namespace ps
