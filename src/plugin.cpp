#include "protectionstones/plugin.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "protectionstones/protection_listener.h"
#include "protectionstones/ui_manager.h"

namespace ps {

namespace {

void tell(const endstone::CommandSender &sender, std::string message)
{
    sender.sendMessage(endstone::Message(std::move(message)));
}

/// Parses a membership level argument; defaults to Member.
MemberLevel parseLevel(const std::string &arg)
{
    std::string lower;
    lower.reserve(arg.size());
    std::transform(arg.begin(), arg.end(), std::back_inserter(lower),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == "guest" ? MemberLevel::Guest : MemberLevel::Member;
}

/// Lowercases an ASCII string (commands/args are case-insensitive).
std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Parses a size argument; returns false if not one of small/medium/large.
bool parseSize(const std::string &raw_arg, ClaimSize *out)
{
    const std::string arg = toLower(raw_arg);
    if (arg == "small") {
        *out = ClaimSize::Small;
        return true;
    }
    if (arg == "medium") {
        *out = ClaimSize::Medium;
        return true;
    }
    if (arg == "large") {
        *out = ClaimSize::Large;
        return true;
    }
    return false;
}

}  // namespace

ProtectionStonesPlugin::ProtectionStonesPlugin() = default;
ProtectionStonesPlugin::~ProtectionStonesPlugin() = default;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void ProtectionStonesPlugin::onLoad()
{
    const std::filesystem::path data_folder = getDataFolder();
    std::error_code ec;
    std::filesystem::create_directories(data_folder, ec);

    // Optional config.json.
    const auto config_path = data_folder / "config.json";
    std::ifstream cfg(config_path);
    if (cfg.is_open()) {
        try {
            nlohmann::json j;
            cfg >> j;
            config_.max_claims_per_player = j.value("max_claims_per_player", config_.max_claims_per_player);
            config_.border_particle = j.value("border_particle", config_.border_particle);
            config_.border_seconds = j.value("border_seconds", config_.border_seconds);
            if (j.contains("limit_permissions") && j["limit_permissions"].is_object()) {
                for (auto it = j["limit_permissions"].begin(); it != j["limit_permissions"].end(); ++it) {
                    if (it.value().is_number_integer()) {
                        config_.limit_tiers.emplace_back(it.key(), it.value().get<int>());
                    }
                }
            }
        }
        catch (const std::exception &e) {
            getLogger().error("Failed to parse config.json ({}), using defaults", e.what());
        }
    }
    else {
        nlohmann::json j;
        j["max_claims_per_player"] = config_.max_claims_per_player;
        j["border_particle"] = config_.border_particle;
        j["border_seconds"] = config_.border_seconds;
        // Example permission tiers: a player holding "protectionstones.limit.vip"
        // may own up to 10 claims. Add your own permission -> limit entries here.
        j["limit_permissions"] = {{"protectionstones.limit.vip", 10}};
        std::ofstream out(config_path, std::ios::trunc);
        if (out.is_open()) {
            out << j.dump(2);
        }
    }

    lang_.load(data_folder / "lang_th_TH.json", getLogger());

    claims_ = std::make_unique<ClaimManager>(data_folder / "claims.json");
    claims_->load();
    getLogger().info("Loaded {} claim(s)", claims_->size());

    ui_ = std::make_unique<UiManager>(*this);
    listener_ = std::make_unique<ProtectionListener>(*this);
}

void ProtectionStonesPlugin::onEnable()
{
    using endstone::EventPriority;
    // High priority for the cancel-decisions so we act before cosmetic plugins.
    registerEvent(&ProtectionListener::onBlockPlace, *listener_, EventPriority::High);
    registerEvent(&ProtectionListener::onBlockBreak, *listener_, EventPriority::High);
    registerEvent(&ProtectionListener::onBlockExplode, *listener_, EventPriority::High);
    registerEvent(&ProtectionListener::onActorExplode, *listener_, EventPriority::High);
    registerEvent(&ProtectionListener::onBlockFromTo, *listener_, EventPriority::High);
    registerEvent(&ProtectionListener::onPistonExtend, *listener_, EventPriority::High);
    registerEvent(&ProtectionListener::onPistonRetract, *listener_, EventPriority::High);
    registerEvent(&ProtectionListener::onActorSpawn, *listener_, EventPriority::High);
    registerEvent(&ProtectionListener::onActorDamage, *listener_, EventPriority::High);
    registerEvent(&ProtectionListener::onPlayerInteract, *listener_, EventPriority::High);
    registerEvent(&ProtectionListener::onPlayerJoin, *listener_);

    getLogger().info("ProtectionStones enabled");
}

void ProtectionStonesPlugin::onDisable()
{
    if (claims_) {
        claims_->save();
    }
    getLogger().info("ProtectionStones disabled");
}

// ---------------------------------------------------------------------------
// Command dispatch
// ---------------------------------------------------------------------------
bool ProtectionStonesPlugin::onCommand(endstone::CommandSender &sender, const endstone::Command & /*command*/,
                                       const std::vector<std::string> &args)
{
    endstone::Player *player = sender.asPlayer();
    if (player == nullptr) {
        tell(sender, lang_.get("player_only"));
        return true;
    }

    // The `/ps` usage declares a single `message` parameter, which EndStone
    // delivers as one arg holding the whole remainder. Normalise to whitespace-
    // separated tokens so the sub-command parsing below works regardless.
    std::vector<std::string> tokens;
    for (const std::string &chunk : args) {
        std::istringstream iss(chunk);
        std::string tok;
        while (iss >> tok) {
            tokens.push_back(tok);
        }
    }

    if (tokens.empty()) {
        return cmdMenu(*player);
    }

    const std::string sub = toLower(tokens[0]);
    const std::vector<std::string> rest(tokens.begin() + 1, tokens.end());

    if (sub == "get") {
        return cmdGet(*player, rest);
    }
    if (sub == "delete") {
        return cmdDelete(*player);
    }
    if (sub == "add") {
        return cmdAdd(*player, rest);
    }
    if (sub == "remove") {
        return cmdRemove(*player, rest);
    }
    if (sub == "transfer") {
        return cmdTransfer(*player, rest);
    }
    if (sub == "list") {
        return cmdList(*player);
    }
    if (sub == "bypass") {
        return cmdBypass(*player);
    }
    if (sub == "menu") {
        return cmdMenu(*player);
    }

    tell(*player, lang_.get("usage_get"));
    return true;
}

// ---------------------------------------------------------------------------
// Permission decisions
// ---------------------------------------------------------------------------
bool ProtectionStonesPlugin::canManage(const Claim &claim, const endstone::Player &player) const
{
    const std::string xuid = player.getXuid();
    return player.isOp() || isBypassing(xuid) || claim.owner_xuid == xuid;
}

bool ProtectionStonesPlugin::canBuild(const Claim &claim, const endstone::Player &player) const
{
    if (canManage(claim, player)) {
        return true;
    }
    auto it = claim.members.find(player.getXuid());
    return it != claim.members.end() && it->second.level == MemberLevel::Member && claim.flags.build;
}

bool ProtectionStonesPlugin::canInteract(const Claim &claim, const endstone::Player &player) const
{
    if (canManage(claim, player)) {
        return true;
    }
    auto it = claim.members.find(player.getXuid());
    if (it == claim.members.end()) {
        return false;
    }
    if (it->second.level == MemberLevel::Member) {
        return true;
    }
    return claim.flags.interact;  // guest: only when interaction is allowed
}

bool ProtectionStonesPlugin::isBypassing(const std::string &xuid) const
{
    return bypassing_.find(xuid) != bypassing_.end();
}

void ProtectionStonesPlugin::setBypassing(const std::string &xuid, bool on)
{
    if (on) {
        bypassing_.insert(xuid);
    }
    else {
        bypassing_.erase(xuid);
    }
}

int ProtectionStonesPlugin::claimLimitFor(const endstone::Player &player) const
{
    int limit = config_.max_claims_per_player;
    for (const auto &[perm, value] : config_.limit_tiers) {
        if (player.hasPermission(perm)) {
            limit = std::max(limit, value);
        }
    }
    return limit;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
Claim *ProtectionStonesPlugin::claimAtPlayer(endstone::Player &player)
{
    const endstone::Location loc = player.getLocation();
    return claims_->getClaimAt(player.getDimension().getName(), loc.getBlockX(), loc.getBlockZ());
}

void ProtectionStonesPlugin::confirmDelete(endstone::Player &player, std::uint64_t claim_id)
{
    ui_->openDeleteConfirm(player, claim_id);
}

void ProtectionStonesPlugin::performDelete(endstone::Player &player, std::uint64_t claim_id)
{
    Claim *claim = claims_->getClaim(claim_id);
    if (claim == nullptr) {
        return;
    }
    // Capture what we need before the claim is erased.
    const std::string dim_name = claim->dimension;
    const int cx = claim->center_x;
    const int cy = claim->center_y;
    const int cz = claim->center_z;
    const ClaimSize size = claim->size;

    claims_->removeClaim(claim_id);
    claims_->save();

    // Clear the centre block and refund the protection block.
    if (endstone::Level *level = getServer().getLevel()) {
        if (endstone::Dimension *dim = level->getDimension(dim_name)) {
            if (auto block = dim->getBlockAt(cx, cy, cz)) {
                block->setType("minecraft:air");
            }
        }
    }
    endstone::ItemStack refund(blockTypeForSize(size), 1);
    player.getInventory().addItem({refund});

    tell(player, lang_.get("claim_deleted"));
}

void ProtectionStonesPlugin::showBorder(endstone::Player &player, const Claim &claim)
{
    const endstone::UUID uuid = player.getUniqueId();
    const std::string dim = claim.dimension;
    const std::string particle = config_.border_particle;
    const int min_x = claim.minX();
    const int max_x = claim.maxX();
    const int min_z = claim.minZ();
    const int max_z = claim.maxZ();

    // Draw every 10 ticks (0.5s) and stop after border_seconds. Capturing the
    // player's UUID (not a reference) keeps this safe if they disconnect.
    const int max_draws = std::max(1, config_.border_seconds * 2);
    auto holder = std::make_shared<std::shared_ptr<endstone::Task>>();
    endstone::Server *server = &getServer();

    *holder = getServer().getScheduler().runTaskTimer(
        *this,
        [server, uuid, dim, particle, min_x, max_x, min_z, max_z, holder]() {
            endstone::Player *pl = server->getPlayer(uuid);
            if (pl == nullptr) {
                if (*holder) {
                    (*holder)->cancel();
                }
                return;
            }
            if (pl->getDimension().getName() == dim) {
                const float y = static_cast<float>(pl->getLocation().getBlockY()) + 1.0F;
                for (int x = min_x; x <= max_x; x += 2) {
                    pl->spawnParticle(particle, static_cast<float>(x) + 0.5F, y, static_cast<float>(min_z) + 0.5F);
                    pl->spawnParticle(particle, static_cast<float>(x) + 0.5F, y, static_cast<float>(max_z) + 0.5F);
                }
                for (int z = min_z; z <= max_z; z += 2) {
                    pl->spawnParticle(particle, static_cast<float>(min_x) + 0.5F, y, static_cast<float>(z) + 0.5F);
                    pl->spawnParticle(particle, static_cast<float>(max_x) + 0.5F, y, static_cast<float>(z) + 0.5F);
                }
            }
        },
        0, 10);

    // Stop the timer after the configured number of draws.
    getServer().getScheduler().runTaskLater(
        *this,
        [holder]() {
            if (*holder) {
                (*holder)->cancel();
            }
        },
        static_cast<std::uint64_t>(max_draws) * 10);
}

// ---------------------------------------------------------------------------
// Sub-commands
// ---------------------------------------------------------------------------
bool ProtectionStonesPlugin::cmdGet(endstone::Player &player, const std::vector<std::string> &args)
{
    if (args.empty()) {
        tell(player, lang_.get("usage_get"));
        return true;
    }
    ClaimSize size;
    if (!parseSize(args[0], &size)) {
        tell(player, lang_.get("unknown_size"));
        return true;
    }
    const std::string perm = std::string("protectionstones.get.") + sizeKey(size);
    if (!player.hasPermission(perm)) {
        tell(player, lang_.get("no_permission"));
        return true;
    }

    endstone::ItemStack stack(blockTypeForSize(size), 1);
    const auto leftover = player.getInventory().addItem({stack});
    if (!leftover.empty()) {
        tell(player, lang_.get("inventory_full"));
        return true;
    }
    tell(player, lang_.get("got_block", lang_.raw(std::string("size_") + sizeKey(size))));
    return true;
}

bool ProtectionStonesPlugin::cmdDelete(endstone::Player &player)
{
    Claim *claim = claimAtPlayer(player);
    if (claim == nullptr) {
        tell(player, lang_.get("not_in_claim"));
        return true;
    }
    if (!canManage(*claim, player)) {
        tell(player, lang_.get("not_owner"));
        return true;
    }
    confirmDelete(player, claim->id);
    return true;
}

bool ProtectionStonesPlugin::cmdAdd(endstone::Player &player, const std::vector<std::string> &args)
{
    if (args.empty()) {
        tell(player, lang_.get("usage_add"));
        return true;
    }
    Claim *claim = claimAtPlayer(player);
    if (claim == nullptr) {
        tell(player, lang_.get("not_in_claim"));
        return true;
    }
    if (!canManage(*claim, player)) {
        tell(player, lang_.get("not_owner"));
        return true;
    }

    endstone::Player *target = getServer().getPlayer(args[0]);
    if (target == nullptr) {
        tell(player, lang_.get("player_not_online", args[0]));
        return true;
    }
    const MemberLevel level = args.size() >= 2 ? parseLevel(args[1]) : MemberLevel::Member;
    const std::string level_name = lang_.raw(level == MemberLevel::Guest ? "level_guest" : "level_member");
    const std::string xuid = target->getXuid();

    auto it = claim->members.find(xuid);
    if (it != claim->members.end()) {
        it->second.level = level;
        it->second.name = target->getName();
        claims_->save();
        tell(player, lang_.get("member_already", target->getName(), level_name));
        return true;
    }
    claim->members.emplace(xuid, MemberInfo{level, target->getName()});
    claims_->save();
    tell(player, lang_.get("member_added", target->getName(), level_name));
    return true;
}

bool ProtectionStonesPlugin::cmdRemove(endstone::Player &player, const std::vector<std::string> &args)
{
    if (args.empty()) {
        tell(player, lang_.get("usage_remove"));
        return true;
    }
    Claim *claim = claimAtPlayer(player);
    if (claim == nullptr) {
        tell(player, lang_.get("not_in_claim"));
        return true;
    }
    if (!canManage(*claim, player)) {
        tell(player, lang_.get("not_owner"));
        return true;
    }

    // Match by stored display name (works even if the member is offline).
    const std::string &name = args[0];
    for (auto it = claim->members.begin(); it != claim->members.end(); ++it) {
        if (it->second.name == name) {
            claim->members.erase(it);
            claims_->save();
            tell(player, lang_.get("member_removed", name));
            return true;
        }
    }
    tell(player, lang_.get("member_not_found", name));
    return true;
}

bool ProtectionStonesPlugin::cmdTransfer(endstone::Player &player, const std::vector<std::string> &args)
{
    if (args.empty()) {
        tell(player, lang_.get("usage_transfer"));
        return true;
    }
    Claim *claim = claimAtPlayer(player);
    if (claim == nullptr) {
        tell(player, lang_.get("not_in_claim"));
        return true;
    }
    if (!canManage(*claim, player)) {
        tell(player, lang_.get("not_owner"));
        return true;
    }
    endstone::Player *target = getServer().getPlayer(args[0]);
    if (target == nullptr) {
        tell(player, lang_.get("player_not_online", args[0]));
        return true;
    }
    claim->owner_xuid = target->getXuid();
    claim->owner_name = target->getName();
    claim->members.erase(target->getXuid());  // owner needn't also be a member
    claims_->save();
    tell(player, lang_.get("transferred", target->getName()));
    tell(*target, lang_.get("transferred_to_you", player.getName()));
    return true;
}

bool ProtectionStonesPlugin::cmdList(endstone::Player &player)
{
    const std::vector<Claim *> owned = claims_->getClaimsByOwner(player.getXuid());
    if (owned.empty()) {
        tell(player, lang_.get("list_empty"));
        return true;
    }

    endstone::ActionForm form;
    form.setTitle(endstone::Message(lang_.get("list_title")));
    for (const Claim *c : owned) {
        const std::uint64_t id = c->id;
        const std::string size_name = lang_.raw(std::string("size_") + sizeKey(c->size));
        const std::string label = lang_.get("list_entry", id, size_name, c->radius, c->dimension);
        form.addButton(endstone::Message(label), std::nullopt, [this, id](endstone::Player *p) {
            if (p == nullptr) {
                return;
            }
            Claim *claim = claims_->getClaim(id);
            if (claim == nullptr) {
                return;
            }
            endstone::Level *level = getServer().getLevel();
            if (level == nullptr) {
                return;
            }
            endstone::Dimension *dim = level->getDimension(claim->dimension);
            if (dim == nullptr) {
                return;
            }
            const endstone::Location target(*dim, static_cast<float>(claim->center_x) + 0.5F,
                                            static_cast<float>(claim->center_y) + 1.0F,
                                            static_cast<float>(claim->center_z) + 0.5F);
            p->teleport(target);
            tell(*p, lang_.get("warped", id));
        });
    }
    player.sendForm(std::move(form));
    return true;
}

bool ProtectionStonesPlugin::cmdBypass(endstone::Player &player)
{
    if (!player.isOp() && !player.hasPermission("protectionstones.bypass")) {
        tell(player, lang_.get("no_permission"));
        return true;
    }
    const std::string xuid = player.getXuid();
    const bool now_on = !isBypassing(xuid);
    setBypassing(xuid, now_on);
    tell(player, lang_.get(now_on ? "bypass_on" : "bypass_off"));
    return true;
}

bool ProtectionStonesPlugin::cmdMenu(endstone::Player &player)
{
    Claim *claim = claimAtPlayer(player);
    if (claim == nullptr) {
        tell(player, lang_.get("not_in_claim"));
        return true;
    }
    ui_->openMainMenu(player, claim->id);
    return true;
}

}  // namespace ps

// ---------------------------------------------------------------------------
// Plugin entry point and metadata
// ---------------------------------------------------------------------------
ENDSTONE_PLUGIN("protectionstones", "1.0.0", ps::ProtectionStonesPlugin)
{
    description = "ProtectionStones-style land protection for Minecraft Bedrock (EndStone)";
    website = "https://github.com/EndstoneMC/endstone";
    authors = {"bed6345"};
    prefix = "ProtectionStones";
    load = endstone::PluginLoadOrder::PostWorld;

    // Permissions
    permission("protectionstones.command")
        .description("Use the /ps command")
        .default_(endstone::PermissionDefault::True);
    permission("protectionstones.get.small")
        .description("Obtain a small (iron) protection block")
        .default_(endstone::PermissionDefault::True);
    permission("protectionstones.get.medium")
        .description("Obtain a medium (gold) protection block")
        .default_(endstone::PermissionDefault::True);
    permission("protectionstones.get.large")
        .description("Obtain a large (diamond) protection block")
        .default_(endstone::PermissionDefault::True);
    permission("protectionstones.bypass")
        .description("Bypass all claim protection (admin)")
        .default_(endstone::PermissionDefault::Operator);

    // Command
    // Sub-commands are dispatched manually in onCommand(), so a single greedy
    // `message` argument captures everything after `/ps`. ('message' must be the
    // last parameter — see EndStone's command usage rules.)
    command("ps")
        .description("Land protection commands")
        .usages("/ps [args: message]")
        .aliases("protectionstones", "land")
        .permissions("protectionstones.command");
}
