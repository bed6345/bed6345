#include "protectionstones/protection_listener.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <endstone/endstone.hpp>

#include "protectionstones/claim.h"
#include "protectionstones/claim_manager.h"
#include "protectionstones/plugin.h"

namespace ps {

namespace {

/// Convenience: send a plain (already-formatted) message to any sender.
void tell(const endstone::CommandSender &sender, std::string message)
{
    sender.sendMessage(endstone::Message(std::move(message)));
}

/// Block offset for a piston face. Bedrock axis convention:
/// North = -Z, South = +Z, West = -X, East = +X, Up = +Y, Down = -Y.
void faceOffset(endstone::BlockFace face, int &dx, int &dy, int &dz)
{
    dx = dy = dz = 0;
    switch (face) {
    case endstone::BlockFace::Down:
        dy = -1;
        break;
    case endstone::BlockFace::Up:
        dy = 1;
        break;
    case endstone::BlockFace::North:
        dz = -1;
        break;
    case endstone::BlockFace::South:
        dz = 1;
        break;
    case endstone::BlockFace::West:
        dx = -1;
        break;
    case endstone::BlockFace::East:
        dx = 1;
        break;
    }
}

/// A piston can move at most 12 blocks; scan that far for a protected centre.
constexpr int kMaxPistonPush = 12;

}  // namespace

// ---------------------------------------------------------------------------
// Block place: either create a claim (protection block) or enforce build rights.
// ---------------------------------------------------------------------------
void ProtectionListener::onBlockPlace(endstone::BlockPlaceEvent &event)
{
    endstone::Player &player = event.getPlayer();
    endstone::Block &block = event.getBlock();
    const std::string dim = block.getDimension().getName();
    const int x = block.getX();
    const int y = block.getY();
    const int z = block.getZ();

    ClaimSize size;
    if (sizeForBlockType(event.getBlockPlacedState().getType(), &size)) {
        // --- creating a new claim ---
        const int radius = radiusForSize(size);
        const std::string xuid = player.getXuid();

        // Limit (operators / bypass are exempt; permission tiers raise the cap).
        const bool exempt = player.isOp() || plugin_.isBypassing(xuid);
        if (!exempt) {
            const int count = plugin_.claims().countClaimsByOwner(xuid);
            const int limit = plugin_.claimLimitFor(player);
            if (count >= limit) {
                event.setCancelled(true);
                tell(player, plugin_.lang().get("claim_limit", count, limit));
                return;
            }
        }

        // Overlap with any existing claim (this also blocks placing inside
        // someone else's region, since that always overlaps).
        if (plugin_.claims().overlaps(dim, x, z, radius)) {
            event.setCancelled(true);
            tell(player, plugin_.lang().get("claim_overlap"));
            return;
        }

        Claim *claim = plugin_.claims().createClaim(xuid, player.getName(), dim, x, y, z, size);
        plugin_.claims().save();
        tell(player, plugin_.lang().get("claim_created", plugin_.lang().raw(std::string("size_") + sizeKey(size)),
                                        radius));
        if (claim != nullptr) {
            plugin_.showBorder(player, *claim);
        }
        return;
    }

    // --- normal block inside an existing claim ---
    Claim *claim = plugin_.claims().getClaimAt(dim, x, z);
    if (claim != nullptr && !plugin_.canBuild(*claim, player)) {
        event.setCancelled(true);
        tell(player, plugin_.lang().get("build_denied"));
    }
}

// ---------------------------------------------------------------------------
// Block break: centre block needs confirmation; other blocks need build rights.
// ---------------------------------------------------------------------------
void ProtectionListener::onBlockBreak(endstone::BlockBreakEvent &event)
{
    endstone::Player &player = event.getPlayer();
    endstone::Block &block = event.getBlock();
    const std::string dim = block.getDimension().getName();
    const int x = block.getX();
    const int y = block.getY();
    const int z = block.getZ();

    Claim *claim = plugin_.claims().getClaimAt(dim, x, z);
    if (claim == nullptr) {
        return;
    }

    if (claim->isCenter(x, y, z)) {
        // The centre block is only ever destroyed via the confirmation form.
        event.setCancelled(true);
        if (plugin_.canManage(*claim, player)) {
            plugin_.confirmDelete(player, claim->id);
        }
        else {
            tell(player, plugin_.lang().get("center_protected"));
        }
        return;
    }

    if (!plugin_.canBuild(*claim, player)) {
        event.setCancelled(true);
        tell(player, plugin_.lang().get("build_denied"));
    }
}

// ---------------------------------------------------------------------------
// Explosions: strip protected blocks (and always the centre) from the blast.
// ---------------------------------------------------------------------------
void ProtectionListener::onBlockExplode(endstone::BlockExplodeEvent &event)
{
    auto &blocks = event.getBlockList();
    auto &mgr = plugin_.claims();
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                                [&mgr](const std::unique_ptr<endstone::Block> &b) {
                                    const Claim *c = mgr.getClaimAt(b->getDimension().getName(), b->getX(), b->getZ());
                                    if (c == nullptr) {
                                        return false;
                                    }
                                    // Always shield the centre; otherwise honour the explosion flag.
                                    return c->isCenter(b->getX(), b->getY(), b->getZ()) || !c->flags.explosion;
                                }),
                 blocks.end());
}

void ProtectionListener::onActorExplode(endstone::ActorExplodeEvent &event)
{
    auto &blocks = event.getBlockList();
    auto &mgr = plugin_.claims();
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                                [&mgr](const std::unique_ptr<endstone::Block> &b) {
                                    const Claim *c = mgr.getClaimAt(b->getDimension().getName(), b->getX(), b->getZ());
                                    if (c == nullptr) {
                                        return false;
                                    }
                                    return c->isCenter(b->getX(), b->getY(), b->getZ()) || !c->flags.explosion;
                                }),
                 blocks.end());
}

// ---------------------------------------------------------------------------
// Liquid / fire flow: never let anything flow onto the centre block. Full
// flow-into-claim protection is gated behind the (reserved) lava flag.
// ---------------------------------------------------------------------------
void ProtectionListener::onBlockFromTo(endstone::BlockFromToEvent &event)
{
    endstone::Block &to = event.getToBlock();
    const std::string dim = to.getDimension().getName();
    const Claim *claim = plugin_.claims().getClaimAt(dim, to.getX(), to.getZ());
    if (claim == nullptr) {
        return;
    }
    if (claim->isCenter(to.getX(), to.getY(), to.getZ()) || claim->flags.lava || claim->flags.fire) {
        event.setCancelled(true);
    }
}

// ---------------------------------------------------------------------------
// Pistons: cancel if the push/pull would move a centre block.
// ---------------------------------------------------------------------------
void ProtectionListener::onPistonExtend(endstone::BlockPistonExtendEvent &event)
{
    endstone::Block &piston = event.getBlock();
    int dx;
    int dy;
    int dz;
    faceOffset(event.getDirection(), dx, dy, dz);
    const std::string dim = piston.getDimension().getName();
    int x = piston.getX();
    int y = piston.getY();
    int z = piston.getZ();
    for (int i = 1; i <= kMaxPistonPush; ++i) {
        x += dx;
        y += dy;
        z += dz;
        const Claim *c = plugin_.claims().getClaimAt(dim, x, z);
        if (c != nullptr && c->isCenter(x, y, z)) {
            event.setCancelled(true);
            return;
        }
    }
}

void ProtectionListener::onPistonRetract(endstone::BlockPistonRetractEvent &event)
{
    // A sticky piston pulls the block two spaces in front of it.
    endstone::Block &piston = event.getBlock();
    int dx;
    int dy;
    int dz;
    faceOffset(event.getDirection(), dx, dy, dz);
    const std::string dim = piston.getDimension().getName();
    const int x = piston.getX() + (dx * 2);
    const int y = piston.getY() + (dy * 2);
    const int z = piston.getZ() + (dz * 2);
    const Claim *c = plugin_.claims().getClaimAt(dim, x, z);
    if (c != nullptr && c->isCenter(x, y, z)) {
        event.setCancelled(true);
    }
}

// ---------------------------------------------------------------------------
// Falling block (sand/gravel): stop it from stacking on the centre pillar.
// We can't catch fire spread (no such event in v0.11.4), but falling blocks
// spawn as actors, so ActorSpawnEvent lets us guard the centre column cheaply.
// ---------------------------------------------------------------------------
void ProtectionListener::onActorSpawn(endstone::ActorSpawnEvent &event)
{
    endstone::Actor &actor = event.getActor();
    const endstone::Location loc = actor.getLocation();
    const int x = loc.getBlockX();
    const int z = loc.getBlockZ();
    const std::string dim = actor.getDimension().getName();
    const Claim *claim = plugin_.claims().getClaimAt(dim, x, z);
    if (claim == nullptr) {
        return;
    }

    // Admin claims block ALL entity spawns except players and whitelisted types.
    if (claim->size == ClaimSize::Admin && actor.asPlayer() == nullptr) {
        const std::string &type = actor.getType();
        const auto &whitelist = plugin_.config().admin_spawn_whitelist;
        if (std::find(whitelist.begin(), whitelist.end(), type) == whitelist.end()) {
            event.setCancelled(true);
            return;
        }
    }

    // Normal claims: only block falling blocks on the centre column.
    if (actor.getType() == "minecraft:falling_block" && x == claim->center_x && z == claim->center_z) {
        event.setCancelled(true);
    }
}

// ---------------------------------------------------------------------------
// Damage: PvP toggle for players, entity protection for everything else.
// ---------------------------------------------------------------------------
void ProtectionListener::onActorDamage(endstone::ActorDamageEvent &event)
{
    endstone::Mob &victim = event.getActor();
    const endstone::Location loc = victim.getLocation();
    const std::string dim = victim.getDimension().getName();

    const Claim *claim = plugin_.claims().getClaimAt(dim, loc.getBlockX(), loc.getBlockZ());
    if (claim == nullptr) {
        return;
    }

    endstone::Actor *damager = event.getDamageSource().getDamagingActor();
    endstone::Player *attacker = damager != nullptr ? damager->asPlayer() : nullptr;

    if (victim.asPlayer() != nullptr) {
        // Player victim: this is PvP only if a player dealt the damage.
        if (attacker != nullptr && !claim->flags.pvp) {
            event.setCancelled(true);
        }
        return;
    }

    // Non-player victim (pet / armor stand / item frame / mob): protect it from
    // players who don't have build rights here.
    if (claim->flags.entity_protect && attacker != nullptr && !plugin_.canBuild(*claim, *attacker)) {
        event.setCancelled(true);
    }
}

// ---------------------------------------------------------------------------
// Interaction: doors / chests / buttons inside a claim.
// ---------------------------------------------------------------------------
void ProtectionListener::onPlayerInteract(endstone::PlayerInteractEvent &event)
{
    if (event.getAction() != endstone::PlayerInteractEvent::Action::RightClickBlock) {
        return;
    }
    endstone::Block *block = event.getBlock();
    if (block == nullptr) {
        return;
    }
    const std::string dim = block->getDimension().getName();
    const Claim *claim = plugin_.claims().getClaimAt(dim, block->getX(), block->getZ());
    if (claim == nullptr) {
        return;
    }
    if (!plugin_.canInteract(*claim, event.getPlayer())) {
        event.setCancelled(true);
        tell(event.getPlayer(), plugin_.lang().get("interact_denied"));
    }
}

// ---------------------------------------------------------------------------
// Movement: prevent non-admin players from leaving an Admin claim.
// ---------------------------------------------------------------------------
void ProtectionListener::onPlayerMove(endstone::PlayerMoveEvent &event)
{
    endstone::Player &player = event.getPlayer();
    const endstone::Location &from = event.getFrom();
    const endstone::Location &to = event.getTo();
    const std::string dim = player.getDimension().getName();

    const Claim *claim = plugin_.claims().getClaimAt(dim, from.getBlockX(), from.getBlockZ());
    if (claim == nullptr || claim->size != ClaimSize::Admin) {
        return;
    }

    if (claim->contains(to.getBlockX(), to.getBlockZ())) {
        return;
    }

    if (player.isOp() || plugin_.isBypassing(player.getXuid())) {
        return;
    }

    event.setCancelled(true);
}

// ---------------------------------------------------------------------------
// Join: refresh the cached display name for this player's XUID everywhere.
// ---------------------------------------------------------------------------
void ProtectionListener::onPlayerJoin(endstone::PlayerJoinEvent &event)
{
    endstone::Player &player = event.getPlayer();
    if (plugin_.claims().refreshDisplayName(player.getXuid(), player.getName())) {
        plugin_.claims().save();
    }
}

}  // namespace ps
