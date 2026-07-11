// Standalone tests for the EndStone-independent core (ClaimManager + Claim).
// These cover the spatial index (incl. negative coords / chunk boundaries),
// overlap rejection, owner counting, removal/index cleanup, and JSON round-trip.
//
// Build via CMake: configure with -DPS_BUILD_TESTS=ON, then `ctest`.
// (No EndStone needed — this links only ClaimManager + nlohmann/json.)

#include "protectionstones/claim_manager.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                             \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(cond)) {                                                          \
            ++g_failures;                                                       \
            std::printf("  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                       \
    } while (0)

using ps::Claim;
using ps::ClaimManager;
using ps::ClaimSize;
using ps::MemberInfo;
using ps::MemberLevel;

static std::filesystem::path tmpFile(const char *name)
{
    return std::filesystem::temp_directory_path() / name;
}

static void test_basic_lookup()
{
    std::printf("test_basic_lookup\n");
    ClaimManager mgr(tmpFile("ps_t1.json"));
    Claim *c = mgr.createClaim("xuid_a", "Alice", "Overworld", 0, 64, 0, ClaimSize::Small);  // r10
    CHECK(c != nullptr);

    CHECK(mgr.getClaimAt("Overworld", 0, 0) == c);     // centre
    CHECK(mgr.getClaimAt("Overworld", 10, 10) == c);   // corner (inclusive)
    CHECK(mgr.getClaimAt("Overworld", -10, -10) == c); // negative corner
    CHECK(mgr.getClaimAt("Overworld", 11, 0) == nullptr);   // just outside +x
    CHECK(mgr.getClaimAt("Overworld", 0, -11) == nullptr);  // just outside -z
    CHECK(mgr.getClaimAt("Nether", 0, 0) == nullptr);       // wrong dimension
}

static void test_negative_and_chunk_spanning()
{
    std::printf("test_negative_and_chunk_spanning\n");
    ClaimManager mgr(tmpFile("ps_t2.json"));
    // Large claim centred deep in negative space, spanning many chunks.
    Claim *c = mgr.createClaim("xuid_b", "Bob", "Overworld", -1000, 64, -1000, ClaimSize::Large);  // r50
    CHECK(c != nullptr);
    CHECK(mgr.getClaimAt("Overworld", -1000, -1000) == c);
    CHECK(mgr.getClaimAt("Overworld", -1050, -1050) == c);  // corner
    CHECK(mgr.getClaimAt("Overworld", -950, -950) == c);    // other corner
    CHECK(mgr.getClaimAt("Overworld", -1051, -1000) == nullptr);
    CHECK(mgr.getClaimAt("Overworld", -1000, -949) == nullptr);
    // A point near a chunk boundary inside the claim.
    CHECK(mgr.getClaimAt("Overworld", -1024, -1024) == c);
}

static void test_overlap()
{
    std::printf("test_overlap\n");
    ClaimManager mgr(tmpFile("ps_t3.json"));
    mgr.createClaim("xuid_a", "Alice", "Overworld", 0, 64, 0, ClaimSize::Small);  // [-10,10]

    // Sharing even a single column counts as overlap.
    CHECK(mgr.overlaps("Overworld", 20, 0, 10) == true);   // [10,30] shares x=10
    // One block further apart -> no overlap.
    CHECK(mgr.overlaps("Overworld", 21, 0, 10) == false);  // [11,31]
    // Different size, far away -> no overlap.
    CHECK(mgr.overlaps("Overworld", 200, 0, 25) == false);
    // Different dimension never overlaps.
    CHECK(mgr.overlaps("Nether", 0, 0, 50) == false);
    // Fully contained smaller box.
    CHECK(mgr.overlaps("Overworld", 0, 0, 1) == true);
}

static void test_owner_count_and_remove()
{
    std::printf("test_owner_count_and_remove\n");
    ClaimManager mgr(tmpFile("ps_t4.json"));
    Claim *c1 = mgr.createClaim("xuid_a", "Alice", "Overworld", 0, 64, 0, ClaimSize::Small);
    mgr.createClaim("xuid_a", "Alice", "Overworld", 100, 64, 0, ClaimSize::Small);
    mgr.createClaim("xuid_b", "Bob", "Overworld", 300, 64, 0, ClaimSize::Small);

    CHECK(mgr.countClaimsByOwner("xuid_a") == 2);
    CHECK(mgr.countClaimsByOwner("xuid_b") == 1);
    CHECK(mgr.countClaimsByOwner("nobody") == 0);

    const std::uint64_t id = c1->id;
    CHECK(mgr.removeClaim(id) == true);
    CHECK(mgr.removeClaim(id) == false);                    // already gone
    CHECK(mgr.getClaimAt("Overworld", 0, 0) == nullptr);    // index cleaned
    CHECK(mgr.countClaimsByOwner("xuid_a") == 1);
    // Space is free again after removal.
    CHECK(mgr.overlaps("Overworld", 0, 0, 10) == false);
}

static void test_persistence_roundtrip()
{
    std::printf("test_persistence_roundtrip\n");
    const auto path = tmpFile("ps_t5.json");
    std::filesystem::remove(path);
    std::uint64_t saved_id = 0;
    {
        ClaimManager mgr(path);
        Claim *c = mgr.createClaim("xuid_a", "Alice", "Overworld", 32, 70, -48, ClaimSize::Medium);  // r25
        c->flags.pvp = true;
        c->flags.explosion = true;
        c->members.emplace("xuid_c", MemberInfo{MemberLevel::Guest, "Carol"});
        saved_id = c->id;
        mgr.save();
    }
    {
        ClaimManager mgr(path);
        mgr.load();
        Claim *c = mgr.getClaim(saved_id);
        CHECK(c != nullptr);
        if (c != nullptr) {
            CHECK(c->owner_xuid == "xuid_a");
            CHECK(c->size == ClaimSize::Medium);
            CHECK(c->radius == 25);
            CHECK(c->flags.pvp == true);
            CHECK(c->flags.explosion == true);
            CHECK(c->flags.entity_protect == true);  // default preserved
            CHECK(c->members.size() == 1);
            auto it = c->members.find("xuid_c");
            CHECK(it != c->members.end());
            if (it != c->members.end()) {
                CHECK(it->second.level == MemberLevel::Guest);
                CHECK(it->second.name == "Carol");
            }
        }
        // Spatial index must be rebuilt on load.
        CHECK(mgr.getClaimAt("Overworld", 32, -48) == c);
        CHECK(mgr.getClaimAt("Overworld", 57, -23) == c);  // corner of r25 box
        // next_id must continue past the loaded ids (no collision).
        Claim *c2 = mgr.createClaim("xuid_z", "Zed", "Overworld", 1000, 64, 1000, ClaimSize::Small);
        CHECK(c2 != nullptr);
        CHECK(c2->id != saved_id);
    }
}

static void test_refresh_display_name()
{
    std::printf("test_refresh_display_name\n");
    ClaimManager mgr(tmpFile("ps_t6.json"));
    Claim *c = mgr.createClaim("xuid_a", "OldName", "Overworld", 0, 64, 0, ClaimSize::Small);
    c->members.emplace("xuid_c", MemberInfo{MemberLevel::Member, "OldCarol"});

    CHECK(mgr.refreshDisplayName("xuid_a", "NewName") == true);
    CHECK(mgr.getClaim(c->id)->owner_name == "NewName");
    CHECK(mgr.refreshDisplayName("xuid_a", "NewName") == false);  // no change
    CHECK(mgr.refreshDisplayName("xuid_c", "NewCarol") == true);
    CHECK(mgr.getClaim(c->id)->members.at("xuid_c").name == "NewCarol");
}

static void test_admin_claim()
{
    std::printf("test_admin_claim\n");
    ClaimManager mgr(tmpFile("ps_t7.json"));
    Claim *c = mgr.createClaim("xuid_op", "Admin", "Overworld", 0, 64, 0, ClaimSize::Admin);  // r250
    CHECK(c != nullptr);
    CHECK(c->size == ClaimSize::Admin);
    CHECK(c->radius == 250);

    CHECK(mgr.getClaimAt("Overworld", 0, 0) == c);
    CHECK(mgr.getClaimAt("Overworld", 250, 250) == c);
    CHECK(mgr.getClaimAt("Overworld", -250, -250) == c);
    CHECK(mgr.getClaimAt("Overworld", 251, 0) == nullptr);
    CHECK(mgr.getClaimAt("Overworld", 0, -251) == nullptr);

    CHECK(c->contains(250, 250) == true);
    CHECK(c->contains(251, 0) == false);
}

static void test_admin_persistence()
{
    std::printf("test_admin_persistence\n");
    const auto path = tmpFile("ps_t8.json");
    std::filesystem::remove(path);
    std::uint64_t saved_id = 0;
    {
        ClaimManager mgr(path);
        Claim *c = mgr.createClaim("xuid_op", "Admin", "Overworld", 500, 64, 500, ClaimSize::Admin);
        saved_id = c->id;
        mgr.save();
    }
    {
        ClaimManager mgr(path);
        mgr.load();
        Claim *c = mgr.getClaim(saved_id);
        CHECK(c != nullptr);
        if (c != nullptr) {
            CHECK(c->size == ClaimSize::Admin);
            CHECK(c->radius == 250);
            CHECK(c->owner_xuid == "xuid_op");
        }
        CHECK(mgr.getClaimAt("Overworld", 500, 500) == c);
        CHECK(mgr.getClaimAt("Overworld", 750, 750) == c);
    }
}

int main()
{
    test_basic_lookup();
    test_negative_and_chunk_spanning();
    test_overlap();
    test_owner_count_and_remove();
    test_persistence_roundtrip();
    test_refresh_display_name();
    test_admin_claim();
    test_admin_persistence();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
