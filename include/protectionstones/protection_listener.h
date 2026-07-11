// ProtectionStones for Bedrock — ProtectionListener (the event hot path)
//
// Every handler here runs on the server thread for potentially thousands of
// events per second, so they are deliberately lean: one O(1)-ish index lookup,
// a couple of integer comparisons, and an early return when nothing is claimed.
// No allocations are made on the common "not in a claim" path.

#pragma once

// EndStone event types are only forward-declared here; the full headers are
// pulled in by the .cpp so this header stays cheap to include.
namespace endstone {
class BlockPlaceEvent;
class BlockBreakEvent;
class BlockExplodeEvent;
class ActorExplodeEvent;
class ActorSpawnEvent;
class BlockFromToEvent;
class BlockPistonExtendEvent;
class BlockPistonRetractEvent;
class ActorDamageEvent;
class PlayerInteractEvent;
class PlayerMoveEvent;
class PlayerJoinEvent;
}  // namespace endstone

namespace ps {

class ProtectionStonesPlugin;

class ProtectionListener {
public:
    explicit ProtectionListener(ProtectionStonesPlugin &plugin) : plugin_(plugin) {}

    // Block lifecycle
    void onBlockPlace(endstone::BlockPlaceEvent &event);
    void onBlockBreak(endstone::BlockBreakEvent &event);

    // Center-block protection against environmental destruction
    void onBlockExplode(endstone::BlockExplodeEvent &event);
    void onActorExplode(endstone::ActorExplodeEvent &event);
    void onBlockFromTo(endstone::BlockFromToEvent &event);
    void onPistonExtend(endstone::BlockPistonExtendEvent &event);
    void onPistonRetract(endstone::BlockPistonRetractEvent &event);
    void onActorSpawn(endstone::ActorSpawnEvent &event);  // falling sand/gravel

    // Gameplay flags
    void onActorDamage(endstone::ActorDamageEvent &event);
    void onPlayerInteract(endstone::PlayerInteractEvent &event);
    void onPlayerMove(endstone::PlayerMoveEvent &event);

    // Housekeeping
    void onPlayerJoin(endstone::PlayerJoinEvent &event);

private:
    ProtectionStonesPlugin &plugin_;
};

}  // namespace ps
