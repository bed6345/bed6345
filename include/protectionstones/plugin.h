// ProtectionStones for Bedrock — main Plugin class
//
// Wires everything together: owns the ClaimManager, the language catalogue, the
// event listener and the UI manager, registers the `/ps` command, and exposes
// the shared permission-decision helpers used by both the listener and the UI.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <endstone/endstone.hpp>

#include "protectionstones/claim_manager.h"
#include "protectionstones/lang.h"

namespace ps {

class ProtectionListener;
class UiManager;

/// Server-wide configuration (loaded from config.json, with sane defaults).
struct Config {
    int max_claims_per_player{3};
    std::string border_particle{"minecraft:basic_flame_particle"};
    int border_seconds{10};
    /// Permission -> claim-limit overrides; the highest matching tier wins.
    std::vector<std::pair<std::string, int>> limit_tiers;
    /// Entity types that may still spawn inside Admin claims (e.g. NPC plugins).
    std::vector<std::string> admin_spawn_whitelist{"minecraft:npc", "npcp:*"};
};

class ProtectionStonesPlugin : public endstone::Plugin {
public:
    ProtectionStonesPlugin();
    ~ProtectionStonesPlugin() override;

    void onLoad() override;
    void onEnable() override;
    void onDisable() override;

    bool onCommand(endstone::CommandSender &sender, const endstone::Command &command,
                   const std::vector<std::string> &args) override;

    // --- accessors used by the listener / UI ---
    [[nodiscard]] ClaimManager &claims() noexcept { return *claims_; }
    [[nodiscard]] Lang &lang() noexcept { return lang_; }
    [[nodiscard]] const Config &config() const noexcept { return config_; }

    // --- permission decisions (single source of truth) ---
    /// Owner, server operator, or in admin-bypass mode.
    [[nodiscard]] bool canManage(const Claim &claim, const endstone::Player &player) const;
    /// May modify blocks: owner / member / op / bypass.
    [[nodiscard]] bool canBuild(const Claim &claim, const endstone::Player &player) const;
    /// May use doors/chests/buttons: owner / member / op / bypass, or guest when
    /// the interact flag is enabled.
    [[nodiscard]] bool canInteract(const Claim &claim, const endstone::Player &player) const;

    [[nodiscard]] bool isBypassing(const std::string &xuid) const;
    void setBypassing(const std::string &xuid, bool on);

    /// The claim limit that applies to this player: the base limit, raised by any
    /// `limit_tiers` permission they hold.
    [[nodiscard]] int claimLimitFor(const endstone::Player &player) const;

    /// Briefly renders the claim's border with particles for the given player.
    void showBorder(endstone::Player &player, const Claim &claim);

    /// Opens the confirmation form before a claim is deleted.
    void confirmDelete(endstone::Player &player, std::uint64_t claim_id);

    /// Actually removes a claim: clears the centre block, refunds the item, and
    /// persists. Shared by the `/ps delete` flow and the confirmation form.
    void performDelete(endstone::Player &player, std::uint64_t claim_id);

private:
    // /ps sub-command handlers (player-only ones receive the resolved Player&).
    bool cmdGet(endstone::Player &player, const std::vector<std::string> &args);
    bool cmdDelete(endstone::Player &player);
    bool cmdAdd(endstone::Player &player, const std::vector<std::string> &args);
    bool cmdRemove(endstone::Player &player, const std::vector<std::string> &args);
    bool cmdTransfer(endstone::Player &player, const std::vector<std::string> &args);
    bool cmdList(endstone::Player &player);
    bool cmdBypass(endstone::Player &player);
    bool cmdMenu(endstone::Player &player);

    /// Returns the claim the player is currently standing in, or nullptr.
    Claim *claimAtPlayer(endstone::Player &player);

    std::unique_ptr<ClaimManager> claims_;
    Lang lang_;
    Config config_;
    std::unique_ptr<ProtectionListener> listener_;
    std::unique_ptr<UiManager> ui_;
    std::unordered_set<std::string> bypassing_;  // xuids currently in bypass mode
};

}  // namespace ps
