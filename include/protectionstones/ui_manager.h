// ProtectionStones for Bedrock — UiManager
//
// Builds the Bedrock ActionForm/ModalForm menus. Every typed `/ps ...` command
// is still available; these forms are just a friendlier front-end over the same
// operations. Forms capture claim *ids* (never raw pointers) so a callback that
// fires after a claim has been removed degrades gracefully.

#pragma once

#include <cstdint>

namespace endstone {
class Player;
}

namespace ps {

class ProtectionStonesPlugin;

class UiManager {
public:
    explicit UiManager(ProtectionStonesPlugin &plugin) : plugin_(plugin) {}

    /// Top-level menu for the claim the player is standing in.
    void openMainMenu(endstone::Player &player, std::uint64_t claim_id);

    /// Member management: list / add / remove / set level.
    void openMemberMenu(endstone::Player &player, std::uint64_t claim_id);

    /// Toggle protection flags (pvp / explosion / fire / interact).
    void openFlagMenu(endstone::Player &player, std::uint64_t claim_id);

    /// Read-only claim info (owner / size / member count / flags).
    void openInfoMenu(endstone::Player &player, std::uint64_t claim_id);

    /// Confirm-before-delete dialog.
    void openDeleteConfirm(endstone::Player &player, std::uint64_t claim_id);

private:
    ProtectionStonesPlugin &plugin_;
};

}  // namespace ps
