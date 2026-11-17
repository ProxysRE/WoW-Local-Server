// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellMgr.h"
#include "World.h"
#include "WorldSession.h"

#include <array>

namespace
{
struct Profession
{
    uint32 skill;
    uint32 apprenticeSpell;
};

// Learn the apprentice spell normally: the core initializes the skill at 1/75
// and teaches the recipes rewarded at skill 1. Do not force skill values.
constexpr std::array<Profession, 14> Professions = {{
    {171, 2259},  // Alchemy
    {164, 2018},  // Blacksmithing
    {333, 7411},  // Enchanting
    {202, 4036},  // Engineering
    {182, 2366},  // Herbalism
    {773, 45357}, // Inscription
    {755, 25229}, // Jewelcrafting
    {165, 2108},  // Leatherworking
    {186, 2575},  // Mining
    {393, 8613},  // Skinning
    {197, 3908},  // Tailoring
    {185, 2550},  // Cooking
    {129, 3273},  // First Aid
    {356, 7620},  // Fishing
}};

constexpr std::array<uint32, 4> Tools = {{
    2901, // Mining Pick
    7005, // Skinning Knife
    5956, // Blacksmith Hammer
    6256, // Fishing Pole
}};

class StarterProfessionsPlayer final : public PlayerScript
{
public:
    StarterProfessionsPlayer() : PlayerScript("StarterProfessionsPlayer", {
        PLAYERHOOK_ON_FIRST_LOGIN
    }) { }

    void OnPlayerFirstLogin(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("StarterProfessions.Enable", true))
            return;

        // IsHeadless is available in the pinned Playerbot core. Unlike AI
        // lookups, this also works before Playerbots has attached a bot AI.
        if (!player->GetSession() || player->GetSession()->IsHeadless())
            return;

        if (sWorld->getIntConfig(CONFIG_MAX_PRIMARY_TRADE_SKILL) < 11)
        {
            LOG_ERROR("module", "StarterProfessions: set MaxPrimaryTradeSkill = 11 or higher; no starter grant for {}.", player->GetName());
            ChatHandler(player->GetSession()).SendSysMessage(
                "Starter professions were not granted: the server requires MaxPrimaryTradeSkill >= 11.");
            return;
        }

        // Check the complete payload before modifying the character.
        for (Profession const& profession : Professions)
            if (!sSpellMgr->GetSpellInfo(profession.apprenticeSpell))
            {
                LOG_ERROR("module", "StarterProfessions: missing apprentice spell {}; no grant for {}.", profession.apprenticeSpell, player->GetName());
                return;
            }
        for (uint32 entry : Tools)
            if (!sObjectMgr->GetItemTemplate(entry))
            {
                LOG_ERROR("module", "StarterProfessions: missing tool {}; no grant for {}.", entry, player->GetName());
                return;
            }

        uint32 learned = 0;
        for (Profession const& profession : Professions)
        {
            // Preserve racial bonuses, existing ranks, recipes and progress.
            if (player->HasSkill(profession.skill))
                continue;

            player->learnSpell(profession.apprenticeSpell);
            if (player->HasSkill(profession.skill))
                ++learned;
            else
                LOG_ERROR("module", "StarterProfessions: spell {} did not teach skill {} to {}.", profession.apprenticeSpell, profession.skill, player->GetName());
        }

        CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();
        MailDraft overflow("Starter profession tools", "Your bags were full. Here are the remaining starter tools.");
        uint32 mailed = 0;
        uint32 stored = 0;

        for (uint32 entry : Tools)
        {
            if (player->HasItemCount(entry, 1, true))
                continue;

            ItemPosCountVec destination;
            if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, entry, 1) == EQUIP_ERR_OK)
            {
                if (Item* item = player->StoreNewItem(destination, entry, true))
                {
                    player->SendNewItem(item, 1, true, false);
                    ++stored;
                    continue;
                }
            }

            if (Item* item = Item::CreateItem(entry, 1, player))
            {
                item->SaveToDB(transaction);
                overflow.AddItem(item);
                ++mailed;
            }
            else
                LOG_ERROR("module", "StarterProfessions: could not create tool {} for {}.", entry, player->GetName());
        }

        if (mailed)
            overflow.SendMailTo(transaction, MailReceiver(player), MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM));

        // Save professions, inventory and the consumed AT_LOGIN_FIRST flag
        // together with overflow mail; a normal relog must not repeat the grant.
        player->SaveToDB(transaction, false, false);
        CharacterDatabase.CommitTransaction(transaction);

        LOG_INFO("module", "StarterProfessions: {} learned {} professions; {} tools stored, {} mailed.", player->GetName(), learned, stored, mailed);
        ChatHandler(player->GetSession()).SendSysMessage(
            "Starter professions learned. Continue training skills and recipes normally.");
        if (mailed)
            ChatHandler(player->GetSession()).SendSysMessage("Some starter tools were sent to your mailbox because your bags were full.");
    }
};
}

void AddStarterProfessionsScripts()
{
    new StarterProfessionsPlayer();
}
