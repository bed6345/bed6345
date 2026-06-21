#include "protectionstones/claim_manager.h"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace ps {

namespace {

ClaimSize sizeFromKey(const std::string &key)
{
    if (key == "medium") {
        return ClaimSize::Medium;
    }
    if (key == "large") {
        return ClaimSize::Large;
    }
    return ClaimSize::Small;
}

MemberLevel levelFromKey(const std::string &key)
{
    return key == "guest" ? MemberLevel::Guest : MemberLevel::Member;
}

const char *levelKey(MemberLevel level)
{
    return level == MemberLevel::Guest ? "guest" : "member";
}

}  // namespace

ClaimManager::ClaimManager(std::filesystem::path data_file) : data_file_(std::move(data_file)) {}

// --- index maintenance -----------------------------------------------------

void ClaimManager::indexClaim(const Claim &claim)
{
    auto &dim = index_[claim.dimension];
    const int min_cx = claim.minX() >> 4;
    const int max_cx = claim.maxX() >> 4;
    const int min_cz = claim.minZ() >> 4;
    const int max_cz = claim.maxZ() >> 4;
    for (int cx = min_cx; cx <= max_cx; ++cx) {
        for (int cz = min_cz; cz <= max_cz; ++cz) {
            dim[packChunk(cx, cz)].push_back(claim.id);
        }
    }
}

void ClaimManager::unindexClaim(const Claim &claim)
{
    auto dim_it = index_.find(claim.dimension);
    if (dim_it == index_.end()) {
        return;
    }
    const int min_cx = claim.minX() >> 4;
    const int max_cx = claim.maxX() >> 4;
    const int min_cz = claim.minZ() >> 4;
    const int max_cz = claim.maxZ() >> 4;
    for (int cx = min_cx; cx <= max_cx; ++cx) {
        for (int cz = min_cz; cz <= max_cz; ++cz) {
            auto chunk_it = dim_it->second.find(packChunk(cx, cz));
            if (chunk_it == dim_it->second.end()) {
                continue;
            }
            auto &ids = chunk_it->second;
            ids.erase(std::remove(ids.begin(), ids.end(), claim.id), ids.end());
            if (ids.empty()) {
                dim_it->second.erase(chunk_it);
            }
        }
    }
}

// --- queries ---------------------------------------------------------------

Claim *ClaimManager::getClaimAt(const std::string &dimension, int x, int z) noexcept
{
    auto dim_it = index_.find(dimension);
    if (dim_it == index_.end()) {
        return nullptr;
    }
    auto chunk_it = dim_it->second.find(packChunk(x >> 4, z >> 4));
    if (chunk_it == dim_it->second.end()) {
        return nullptr;
    }
    for (const std::uint64_t id : chunk_it->second) {
        auto claim_it = claims_.find(id);
        if (claim_it != claims_.end() && claim_it->second.contains(x, z)) {
            return &claim_it->second;
        }
    }
    return nullptr;
}

const Claim *ClaimManager::getClaimAt(const std::string &dimension, int x, int z) const noexcept
{
    // Reuse the non-const logic without duplicating it.
    return const_cast<ClaimManager *>(this)->getClaimAt(dimension, x, z);
}

bool ClaimManager::overlaps(const std::string &dimension, int cx, int cz, int radius) const noexcept
{
    auto dim_it = index_.find(dimension);
    if (dim_it == index_.end()) {
        return false;
    }
    const int min_x = cx - radius;
    const int max_x = cx + radius;
    const int min_z = cz - radius;
    const int max_z = cz + radius;
    for (int chunk_x = min_x >> 4; chunk_x <= (max_x >> 4); ++chunk_x) {
        for (int chunk_z = min_z >> 4; chunk_z <= (max_z >> 4); ++chunk_z) {
            auto chunk_it = dim_it->second.find(packChunk(chunk_x, chunk_z));
            if (chunk_it == dim_it->second.end()) {
                continue;
            }
            for (const std::uint64_t id : chunk_it->second) {
                auto claim_it = claims_.find(id);
                if (claim_it == claims_.end()) {
                    continue;
                }
                const Claim &c = claim_it->second;
                // Standard AABB-vs-AABB intersection on the X/Z plane.
                const bool disjoint =
                    max_x < c.minX() || min_x > c.maxX() || max_z < c.minZ() || min_z > c.maxZ();
                if (!disjoint) {
                    return true;
                }
            }
        }
    }
    return false;
}

// --- creation / removal ----------------------------------------------------

Claim *ClaimManager::createClaim(const std::string &owner_xuid, const std::string &owner_name,
                                 const std::string &dimension, int cx, int cy, int cz, ClaimSize size)
{
    Claim claim;
    claim.id = next_id_++;
    claim.owner_xuid = owner_xuid;
    claim.owner_name = owner_name;
    claim.dimension = dimension;
    claim.center_x = cx;
    claim.center_y = cy;
    claim.center_z = cz;
    claim.size = size;
    claim.radius = radiusForSize(size);

    auto [it, inserted] = claims_.emplace(claim.id, std::move(claim));
    indexClaim(it->second);
    return &it->second;
}

bool ClaimManager::removeClaim(std::uint64_t id)
{
    auto it = claims_.find(id);
    if (it == claims_.end()) {
        return false;
    }
    unindexClaim(it->second);
    claims_.erase(it);
    return true;
}

// --- lookups ---------------------------------------------------------------

Claim *ClaimManager::getClaim(std::uint64_t id) noexcept
{
    auto it = claims_.find(id);
    return it == claims_.end() ? nullptr : &it->second;
}

int ClaimManager::countClaimsByOwner(const std::string &xuid) const noexcept
{
    int count = 0;
    for (const auto &[id, claim] : claims_) {
        if (claim.owner_xuid == xuid) {
            ++count;
        }
    }
    return count;
}

std::vector<Claim *> ClaimManager::getClaimsByOwner(const std::string &xuid)
{
    std::vector<Claim *> result;
    for (auto &[id, claim] : claims_) {
        if (claim.owner_xuid == xuid) {
            result.push_back(&claim);
        }
    }
    return result;
}

bool ClaimManager::refreshDisplayName(const std::string &xuid, const std::string &name)
{
    bool changed = false;
    for (auto &[id, claim] : claims_) {
        if (claim.owner_xuid == xuid && claim.owner_name != name) {
            claim.owner_name = name;
            changed = true;
        }
        auto it = claim.members.find(xuid);
        if (it != claim.members.end() && it->second.name != name) {
            it->second.name = name;
            changed = true;
        }
    }
    return changed;
}

// --- persistence -----------------------------------------------------------

void ClaimManager::load()
{
    claims_.clear();
    index_.clear();
    next_id_ = 1;

    std::ifstream in(data_file_);
    if (!in.is_open()) {
        return;  // first run — nothing to load
    }

    nlohmann::json root;
    try {
        in >> root;
    }
    catch (const std::exception &) {
        return;  // corrupt file: start empty rather than crash
    }

    next_id_ = root.value("next_id", static_cast<std::uint64_t>(1));

    for (const auto &j : root.value("claims", nlohmann::json::array())) {
        Claim claim;
        claim.id = j.value("id", static_cast<std::uint64_t>(0));
        claim.owner_xuid = j.value("owner_xuid", "");
        claim.owner_name = j.value("owner_name", "");
        claim.dimension = j.value("dimension", "Overworld");
        if (j.contains("center") && j["center"].is_array() && j["center"].size() == 3) {
            claim.center_x = j["center"][0].get<int>();
            claim.center_y = j["center"][1].get<int>();
            claim.center_z = j["center"][2].get<int>();
        }
        claim.size = sizeFromKey(j.value("size", "small"));
        claim.radius = j.value("radius", radiusForSize(claim.size));

        const auto &f = j.value("flags", nlohmann::json::object());
        claim.flags.build = f.value("build", true);
        claim.flags.interact = f.value("interact", true);
        claim.flags.pvp = f.value("pvp", false);
        claim.flags.explosion = f.value("explosion", false);
        claim.flags.fire = f.value("fire", false);
        claim.flags.entity_protect = f.value("entity_protect", true);
        claim.flags.lava = f.value("lava", false);

        for (const auto &m : j.value("members", nlohmann::json::array())) {
            MemberInfo info;
            info.name = m.value("name", "");
            info.level = levelFromKey(m.value("level", "member"));
            claim.members.emplace(m.value("xuid", ""), std::move(info));
        }

        if (claim.id >= next_id_) {
            next_id_ = claim.id + 1;
        }
        auto [it, inserted] = claims_.emplace(claim.id, std::move(claim));
        if (inserted) {
            indexClaim(it->second);  // rebuild spatial index
        }
    }
}

void ClaimManager::save() const
{
    nlohmann::json root;
    root["next_id"] = next_id_;
    root["claims"] = nlohmann::json::array();

    for (const auto &[id, claim] : claims_) {
        nlohmann::json j;
        j["id"] = claim.id;
        j["owner_xuid"] = claim.owner_xuid;
        j["owner_name"] = claim.owner_name;
        j["dimension"] = claim.dimension;
        j["center"] = {claim.center_x, claim.center_y, claim.center_z};
        j["size"] = sizeKey(claim.size);
        j["radius"] = claim.radius;
        j["flags"] = {
            {"build", claim.flags.build},     {"interact", claim.flags.interact},
            {"pvp", claim.flags.pvp},         {"explosion", claim.flags.explosion},
            {"fire", claim.flags.fire},       {"entity_protect", claim.flags.entity_protect},
            {"lava", claim.flags.lava},
        };
        j["members"] = nlohmann::json::array();
        for (const auto &[xuid, info] : claim.members) {
            j["members"].push_back({{"xuid", xuid}, {"name", info.name}, {"level", levelKey(info.level)}});
        }
        root["claims"].push_back(std::move(j));
    }

    // Write to a temp file then rename so a crash mid-write can't truncate data.
    std::error_code ec;
    std::filesystem::create_directories(data_file_.parent_path(), ec);
    const auto tmp = data_file_.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) {
            return;
        }
        out << root.dump(2);
    }
    std::filesystem::rename(tmp, data_file_, ec);
}

}  // namespace ps
