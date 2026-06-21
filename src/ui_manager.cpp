#include "protectionstones/ui_manager.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <endstone/endstone.hpp>
#include <nlohmann/json.hpp>

#include "protectionstones/claim.h"
#include "protectionstones/claim_manager.h"
#include "protectionstones/plugin.h"

namespace ps {

namespace {

void tell(const endstone::CommandSender &sender, std::string message)
{
    sender.sendMessage(endstone::Message(std::move(message)));
}

std::string sizeDisplay(Lang &lang, ClaimSize size)
{
    return lang.raw(std::string("size_") + sizeKey(size));
}

}  // namespace

// ---------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------
void UiManager::openMainMenu(endstone::Player &player, std::uint64_t claim_id)
{
    Lang &lang = plugin_.lang();
    UiManager *self = this;
    ProtectionStonesPlugin *plugin = &plugin_;

    endstone::ActionForm form;
    form.setTitle(endstone::Message(lang.get("menu_title")));

    form.addButton(endstone::Message(lang.get("menu_members")), std::nullopt,
                   [self, claim_id](endstone::Player *p) {
                       if (p != nullptr) {
                           self->openMemberMenu(*p, claim_id);
                       }
                   });
    form.addButton(endstone::Message(lang.get("menu_flags")), std::nullopt,
                   [self, claim_id](endstone::Player *p) {
                       if (p != nullptr) {
                           self->openFlagMenu(*p, claim_id);
                       }
                   });
    form.addButton(endstone::Message(lang.get("menu_info")), std::nullopt,
                   [self, claim_id](endstone::Player *p) {
                       if (p != nullptr) {
                           self->openInfoMenu(*p, claim_id);
                       }
                   });
    form.addButton(endstone::Message(lang.get("menu_show_border")), std::nullopt,
                   [plugin, claim_id](endstone::Player *p) {
                       if (p == nullptr) {
                           return;
                       }
                       Claim *c = plugin->claims().getClaim(claim_id);
                       if (c != nullptr) {
                           plugin->showBorder(*p, *c);
                           tell(*p, plugin->lang().get("border_shown", plugin->config().border_seconds));
                       }
                   });
    form.addButton(endstone::Message(lang.get("menu_delete")), std::nullopt,
                   [plugin, claim_id](endstone::Player *p) {
                       if (p != nullptr) {
                           plugin->confirmDelete(*p, claim_id);
                       }
                   });

    player.sendForm(std::move(form));
}

// ---------------------------------------------------------------------------
// Member management (list + per-member edit dialog)
// ---------------------------------------------------------------------------
void UiManager::openMemberMenu(endstone::Player &player, std::uint64_t claim_id)
{
    Lang &lang = plugin_.lang();
    ProtectionStonesPlugin *plugin = &plugin_;
    UiManager *self = this;

    Claim *claim = plugin_.claims().getClaim(claim_id);
    if (claim == nullptr) {
        tell(player, lang.get("not_in_claim"));
        return;
    }

    endstone::ActionForm form;
    form.setTitle(endstone::Message(lang.get("members_title")));

    for (const auto &[xuid, info] : claim->members) {
        const std::string level = lang.raw(info.level == MemberLevel::Guest ? "level_guest" : "level_member");
        const std::string text = lang.get("member_line", info.name.empty() ? xuid : info.name, level);
        const std::string target_xuid = xuid;
        form.addButton(endstone::Message(text), std::nullopt,
                       [plugin, claim_id, target_xuid](endstone::Player *p) {
                           if (p == nullptr) {
                               return;
                           }
                           Claim *c = plugin->claims().getClaim(claim_id);
                           if (c == nullptr) {
                               return;
                           }
                           auto it = c->members.find(target_xuid);
                           if (it == c->members.end()) {
                               return;
                           }

                           // Edit dialog: choose level or remove the member.
                           endstone::ModalForm edit;
                           edit.setTitle(endstone::Message(plugin->lang().get("members_title")));
                           edit.addControl(endstone::Dropdown(endstone::Message("ระดับสมาชิก"),
                                                              {"member", "guest"},
                                                              it->second.level == MemberLevel::Guest ? 1 : 0));
                           edit.addControl(endstone::Toggle(endstone::Message("ลบสมาชิกคนนี้"), false));
                           edit.setOnSubmit([plugin, claim_id, target_xuid](endstone::Player *pp,
                                                                            std::string data) {
                               if (pp == nullptr) {
                                   return;
                               }
                               Claim *cc = plugin->claims().getClaim(claim_id);
                               if (cc == nullptr) {
                                   return;
                               }
                               auto mit = cc->members.find(target_xuid);
                               if (mit == cc->members.end()) {
                                   return;
                               }
                               try {
                                   const auto arr = nlohmann::json::parse(data);
                                   const int level_idx = arr.at(0).get<int>();
                                   const bool remove = arr.at(1).get<bool>();
                                   if (remove) {
                                       const std::string name = mit->second.name;
                                       cc->members.erase(mit);
                                       tell(*pp, plugin->lang().get("member_removed",
                                                                    name.empty() ? target_xuid : name));
                                   }
                                   else {
                                       mit->second.level =
                                           level_idx == 1 ? MemberLevel::Guest : MemberLevel::Member;
                                   }
                                   plugin->claims().save();
                               }
                               catch (const std::exception &) {
                               }
                           });
                           p->sendForm(std::move(edit));
                       });
    }

    // Add-member: open a picker of online players (falls back to /ps add).
    form.addButton(endstone::Message(lang.get("members_add_button")), std::nullopt,
                   [self, claim_id](endstone::Player *p) {
                       if (p != nullptr) {
                           self->openAddMember(*p, claim_id);
                       }
                   });

    player.sendForm(std::move(form));
}

// ---------------------------------------------------------------------------
// Add-member dialog (pick an online player + level)
// ---------------------------------------------------------------------------
void UiManager::openAddMember(endstone::Player &player, std::uint64_t claim_id)
{
    Lang &lang = plugin_.lang();
    ProtectionStonesPlugin *plugin = &plugin_;

    Claim *claim = plugin_.claims().getClaim(claim_id);
    if (claim == nullptr) {
        tell(player, lang.get("not_in_claim"));
        return;
    }

    // Candidates = online players who are not the owner and not already members.
    std::vector<std::string> names;
    for (endstone::Player *online : plugin_.getServer().getOnlinePlayers()) {
        if (online == nullptr) {
            continue;
        }
        const std::string xuid = online->getXuid();
        if (xuid == claim->owner_xuid || claim->members.count(xuid) != 0) {
            continue;
        }
        names.push_back(online->getName());
    }
    if (names.empty()) {
        tell(player, lang.get("no_players_online"));
        return;
    }

    endstone::ModalForm form;
    form.setTitle(endstone::Message(lang.get("add_member_title")));
    form.addControl(endstone::Dropdown(endstone::Message(lang.get("add_member_player")), names, 0));
    form.addControl(endstone::Dropdown(endstone::Message(lang.get("add_member_level")),
                                       {"member", "guest"}, 0));
    form.setOnSubmit([plugin, claim_id, names](endstone::Player *p, std::string data) {
        if (p == nullptr) {
            return;
        }
        Claim *c = plugin->claims().getClaim(claim_id);
        if (c == nullptr) {
            return;
        }
        try {
            const auto arr = nlohmann::json::parse(data);
            const int player_idx = arr.at(0).get<int>();
            const int level_idx = arr.at(1).get<int>();
            if (player_idx < 0 || player_idx >= static_cast<int>(names.size())) {
                return;
            }
            const std::string &name = names[player_idx];
            endstone::Player *target = plugin->getServer().getPlayer(name);
            if (target == nullptr) {
                tell(*p, plugin->lang().get("player_not_online", name));
                return;
            }
            const MemberLevel level = level_idx == 1 ? MemberLevel::Guest : MemberLevel::Member;
            const std::string level_name =
                plugin->lang().raw(level == MemberLevel::Guest ? "level_guest" : "level_member");
            c->members[target->getXuid()] = MemberInfo{level, target->getName()};
            plugin->claims().save();
            tell(*p, plugin->lang().get("member_added", target->getName(), level_name));
        }
        catch (const std::exception &) {
        }
    });

    player.sendForm(std::move(form));
}

// ---------------------------------------------------------------------------
// Flag toggles
// ---------------------------------------------------------------------------
void UiManager::openFlagMenu(endstone::Player &player, std::uint64_t claim_id)
{
    Lang &lang = plugin_.lang();
    ProtectionStonesPlugin *plugin = &plugin_;

    Claim *claim = plugin_.claims().getClaim(claim_id);
    if (claim == nullptr) {
        tell(player, lang.get("not_in_claim"));
        return;
    }

    endstone::ModalForm form;
    form.setTitle(endstone::Message(lang.get("flags_title")));
    // Control order defines how we read the submit payload below.
    form.addControl(endstone::Toggle(endstone::Message(lang.get("flag_pvp")), claim->flags.pvp));
    form.addControl(endstone::Toggle(endstone::Message(lang.get("flag_explosion")), claim->flags.explosion));
    form.addControl(endstone::Toggle(endstone::Message(lang.get("flag_fire")), claim->flags.fire));
    form.addControl(endstone::Toggle(endstone::Message(lang.get("flag_interact")), claim->flags.interact));

    form.setOnSubmit([plugin, claim_id](endstone::Player *p, std::string data) {
        if (p == nullptr) {
            return;
        }
        Claim *c = plugin->claims().getClaim(claim_id);
        if (c == nullptr) {
            return;
        }
        try {
            const auto arr = nlohmann::json::parse(data);
            c->flags.pvp = arr.at(0).get<bool>();
            c->flags.explosion = arr.at(1).get<bool>();
            c->flags.fire = arr.at(2).get<bool>();
            c->flags.interact = arr.at(3).get<bool>();
            plugin->claims().save();
            tell(*p, plugin->lang().get("flags_saved"));
        }
        catch (const std::exception &) {
        }
    });

    player.sendForm(std::move(form));
}

// ---------------------------------------------------------------------------
// Read-only info
// ---------------------------------------------------------------------------
void UiManager::openInfoMenu(endstone::Player &player, std::uint64_t claim_id)
{
    Lang &lang = plugin_.lang();
    Claim *claim = plugin_.claims().getClaim(claim_id);
    if (claim == nullptr) {
        tell(player, lang.get("not_in_claim"));
        return;
    }

    endstone::ActionForm form;
    form.setTitle(endstone::Message(lang.get("info_title")));
    form.setContent(endstone::Message(lang.get(
        "info_body", claim->owner_name.empty() ? claim->owner_xuid : claim->owner_name,
        sizeDisplay(lang, claim->size), claim->radius, claim->dimension, claim->center_x, claim->center_y,
        claim->center_z, static_cast<int>(claim->members.size()))));
    player.sendForm(std::move(form));
}

// ---------------------------------------------------------------------------
// Delete confirmation
// ---------------------------------------------------------------------------
void UiManager::openDeleteConfirm(endstone::Player &player, std::uint64_t claim_id)
{
    Lang &lang = plugin_.lang();
    ProtectionStonesPlugin *plugin = &plugin_;

    endstone::MessageForm form;
    form.setTitle(endstone::Message(lang.get("confirm_delete_title")));
    form.setContent(endstone::Message(lang.get("confirm_delete_body")));
    form.setButton1(endstone::Message(lang.get("confirm_yes")));
    form.setButton2(endstone::Message(lang.get("confirm_no")));
    form.setOnSubmit([plugin, claim_id](endstone::Player *p, int button) {
        // Button index 0 == button1 ("delete").
        if (p != nullptr && button == 0) {
            plugin->performDelete(*p, claim_id);
        }
    });
    player.sendForm(std::move(form));
}

}  // namespace ps
