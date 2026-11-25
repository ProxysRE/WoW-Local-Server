// SPDX-License-Identifier: GPL-2.0-or-later
#include "BestiaryProtocol.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "LootMgr.h"
#include "MapMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <tuple>

namespace
{
using Rows = std::vector<std::string>;
struct LootRow
{
    uint32 item;
    uint32 reference;
    float chance;
    uint32 quest;
    uint32 mode;
    uint32 group;
    uint32 min;
    uint32 max;
};
using LootTable = std::map<uint32, std::vector<LootRow>>;
struct Client
{
    std::chrono::steady_clock::time_point last;
    uint32 detail = 0;
    Rows rows;
};

class Catalogue
{
public:
    std::map<uint32, uint32> spawns;
    std::map<uint64, Client> clients;
    bool ready = false;

    void Load()
    {
        // Fixed read-only startup queries: module SQL cannot register core prepared-statement enums.
        // Never interpolate client input or query the DB from chat handlers.
        char const* queries[] = {
            "SELECT Entry, Item, Reference, Chance, QuestRequired, LootMode, GroupId, MinCount, MaxCount "
                "FROM creature_loot_template",
            "SELECT Entry, Item, Reference, Chance, QuestRequired, LootMode, GroupId, MinCount, MaxCount "
                "FROM skinning_loot_template",
            "SELECT Entry, Item, Reference, Chance, QuestRequired, LootMode, GroupId, MinCount, MaxCount "
                "FROM pickpocketing_loot_template",
            "SELECT Entry, Item, Reference, Chance, QuestRequired, LootMode, GroupId, MinCount, MaxCount "
                "FROM reference_loot_template"
        };
        for (std::size_t i = 0; i < 4; ++i)
            if (QueryResult result = WorldDatabase.Query(queries[i]))
                do
                {
                    Field* f = result->Fetch();
                    int32 const reference = f[2].Get<int32>();
                    uint32 const ref = static_cast<uint32>(reference < 0 ? -int64(reference) : reference);
                    uint32 const mode = std::max<uint32>(1, f[5].Get<uint16>());
                    // Use the core's own validation, including rejected zero-chance ungrouped entries.
                    LootStoreItem candidate(f[1].Get<uint32>(), reference, f[3].Get<float>(),
                        f[4].Get<bool>(), mode, f[6].Get<uint8>(), f[7].Get<uint8>(), f[8].Get<uint8>());
                    LootStore const* stores[] = {&LootTemplates_Creature, &LootTemplates_Skinning,
                        &LootTemplates_Pickpocketing, &LootTemplates_Reference};
                    if (candidate.IsValid(*stores[i], f[0].Get<uint32>()))
                        tables[i][f[0].Get<uint32>()].push_back({f[1].Get<uint32>(), ref,
                            f[3].Get<float>(), f[4].Get<bool>() ? 1u : 0u, mode, f[6].Get<uint8>(),
                            f[7].Get<uint8>(), f[8].Get<uint8>()});
                } while (result->NextRow());

        if (QueryResult result = WorldDatabase.Query(
            "SELECT DISTINCT SourceTypeOrReferenceId, SourceGroup, SourceEntry FROM conditions "
            "WHERE SourceTypeOrReferenceId IN (1,8,10,11)"))
            do
            {
                Field* f = result->Fetch();
                conditions.emplace(f[0].Get<uint32>(), f[1].Get<uint32>(), f[2].Get<uint32>());
            } while (result->NextRow());

        for (auto const& [guid, data] : sObjectMgr->GetAllCreatureData())
        {
            if (!data.dbData || data.mapid != 0)
                continue;
            if (sMapMgr->GetZoneId(data.phaseMask, data.mapid, data.posX, data.posY, data.posZ) != 10)
                continue;
            // Multispawn alternatives are distinct species sharing one DB spawn.
            std::set<uint32> const entries = {data.id, data.id2, data.id3};
            for (uint32 entry : entries)
                if (entry && sObjectMgr->GetCreatureTemplate(entry))
                    ++spawns[entry];
        }
        ready = !spawns.empty() && !tables[0].empty() && !tables[1].empty() &&
            !tables[2].empty() && !tables[3].empty();
        LOG_INFO("server.loading", "Bestiary: loaded {} Duskwood creature types; read-only catalogue", spawns.size());
        if (!ready)
            LOG_ERROR("server.loading", "Bestiary: catalogue unavailable; check map data and loot table SELECT errors");
    }

    Rows Zone(LocaleConstant locale)
    {
        Rows rows;
        for (auto const& [entry, count] : spawns)
        {
            CreatureTemplate const* mob = sObjectMgr->GetCreatureTemplate(entry);
            std::string name = mob->Name;
            if (CreatureLocale const* loc = sObjectMgr->GetCreatureLocale(entry))
                ObjectMgr::GetLocaleString(loc->Name, locale, name);
            rows.push_back("M|" + std::to_string(entry) + "|" + Bestiary::Escape(name) + "|" +
                std::to_string(mob->minlevel) + "|" + std::to_string(mob->maxlevel) + "|" +
                std::to_string(mob->rank) + "|" + std::to_string(count) + "|" + std::to_string(mob->type));
        }
        return rows;
    }

    Rows Detail(uint32 entry, LocaleConstant locale)
    {
        Rows rows;
        CreatureTemplate const* mob = sObjectMgr->GetCreatureTemplate(entry);
        if (!mob)
            return rows;
        uint32 const ids[] = {mob->lootid, mob->SkinLootId, mob->pickpocketLootId};
        char const* tabs[] = {"loot", "skin", "pick"};
        for (uint32 i = 0; i < 3; ++i)
        {
            std::set<uint32> path;
            Flatten(i, ids[i], tabs[i], locale, rows, path, "", false);
        }
        return rows;
    }

private:
    LootTable tables[4];
    std::set<std::tuple<uint32, uint32, uint32>> conditions;

    void Flatten(uint32 table, uint32 id, std::string const& tab, LocaleConstant locale,
        Rows& rows, std::set<uint32>& path, std::string const& inherited, bool uncertain)
    {
        auto const found = tables[table].find(id);
        if (!id || found == tables[table].end())
            return;
        uint32 const sources[] = {1, 11, 8, 10};
        for (LootRow const& row : found->second)
        {
            if (rows.size() >= 8192)
                return;
            std::string note = inherited;
            bool const conditional = conditions.count({sources[table], id, row.item}) != 0;
            bool const unknown = uncertain || row.group || conditional || row.mode != 1;
            note += "Количество " + std::to_string(row.min) + "-" + std::to_string(row.max) +
                "; режим " + std::to_string(row.mode) + "; шанс БД " + std::to_string(row.chance) + "%";
            if (row.group)
                note += "; группа " + std::to_string(row.group);
            if (conditional)
                note += "; есть условия";
            if (row.quest)
                note += "; нужен квест";
            if (row.reference)
            {
                // References are candidates, not independent rolls. Do not invent a combined probability.
                if (path.size() >= 16 || !path.insert(row.reference).second)
                    continue;
                Flatten(3, row.reference, tab, locale, rows, path,
                    note + "; ссылка " + std::to_string(row.reference) + " -> ", true);
                path.erase(row.reference);
                continue;
            }
            ItemTemplate const* item = sObjectMgr->GetItemTemplate(row.item);
            if (!item)
                continue;
            std::string name = item->Name1;
            if (ItemLocale const* loc = sObjectMgr->GetItemLocale(row.item))
                ObjectMgr::GetLocaleString(loc->Name, locale, name);
            rows.push_back("I|" + tab + "|" + std::to_string(row.item) + "|" + Bestiary::Escape(name) +
                "|" + std::to_string(item->Quality) + "|" +
                (unknown ? "-1" : std::to_string(std::clamp(row.chance, 0.0f, 100.0f))) +
                "|" + std::to_string(row.quest) + "|" + Bestiary::Escape(note));
        }
    }
};

Catalogue catalogue;

void Send(Player* player, uint32 id, std::string const& payload)
{
    for (std::string const& frame : Bestiary::Frames(id, payload))
    {
        WorldPacket packet;
        ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player, frame);
        player->SendDirectMessage(&packet);
    }
}

class BestiaryWorld : public WorldScript
{
public:
    BestiaryWorld() : WorldScript("BestiaryWorld", {WORLDHOOK_ON_STARTUP}) { }
    void OnStartup() override { catalogue.Load(); }
};

class BestiaryPlayer : public PlayerScript
{
public:
    BestiaryPlayer() : PlayerScript("BestiaryPlayer",
        {PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_LOGOUT}) { }

    void OnPlayerLogout(Player* player) override
    {
        catalogue.clients.erase(player->GetGUID().GetRawValue());
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& message,
        Player* receiver) override
    {
        if (language != LANG_ADDON || type != CHAT_MSG_WHISPER ||
            message.compare(0, Bestiary::Prefix.size(), Bestiary::Prefix) != 0)
            return true;
        // Consume this prefix even on malformed requests or whispers addressed to another player.
        if (receiver != player)
            return false;
        Bestiary::Request request;
        if (!Bestiary::Parse(std::string_view(message).substr(Bestiary::Prefix.size()), request))
            return false;
        Client& client = catalogue.clients[player->GetGUID().GetRawValue()];
        auto const now = std::chrono::steady_clock::now();
        if (now - client.last < std::chrono::milliseconds(100))
            return false;
        client.last = now;
        if (!catalogue.ready)
        {
            Send(player, request.id, "X|NOT_READY");
            return false;
        }
        Rows zone;
        Rows const* rows = nullptr;
        if (request.kind == 'Z' && request.target == 10)
        {
            zone = catalogue.Zone(player->GetSession()->GetSessionDbLocaleIndex());
            rows = &zone;
        }
        else if (request.kind == 'D' && catalogue.spawns.count(request.target))
        {
            if (client.detail != request.target)
            {
                client.rows = catalogue.Detail(request.target, player->GetSession()->GetSessionDbLocaleIndex());
                client.detail = request.target;
            }
            if (client.rows.size() >= 8192)
            {
                Send(player, request.id, "X|CATALOGUE_LIMIT");
                return false;
            }
            rows = &client.rows;
        }
        if (!rows)
        {
            Send(player, request.id, "X|UNKNOWN_TARGET");
            return false;
        }
        std::size_t const pages = std::max<std::size_t>(1,
            (rows->size() + Bestiary::PageSize - 1) / Bestiary::PageSize);
        if (request.page >= pages)
        {
            Send(player, request.id, "X|BAD_PAGE");
            return false;
        }
        std::string payload = "H|" + std::string(1, request.kind) + "|" + std::to_string(request.target) +
            "|" + std::to_string(request.page) + "|" + std::to_string(pages);
        for (std::size_t i = request.page * Bestiary::PageSize;
            i < std::min(rows->size(), (request.page + 1) * Bestiary::PageSize); ++i)
            payload += "\n" + (*rows)[i];
        Send(player, request.id, payload);
        return false;
    }
};
}

void Addmod_bestiaryScripts()
{
    new BestiaryWorld();
    new BestiaryPlayer();
}
