// SPDX-License-Identifier: GPL-2.0-or-later
#include "BestiaryProtocol.h"
#include "BestiaryRegions.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
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
    char kind = 0;
    uint32 target = 0;
    Rows rows;
};

struct Region
{
    uint32 map = 0;
    uint32 zone = 0;
    uint32 category = 0;
    uint32 mode = 0;
    // effective template -> (base entry, number of DB spawns in this mode)
    std::map<uint32, std::pair<uint32, uint32>> members;
};

std::string LocalName(char const* const* names, LocaleConstant locale)
{
    return names[locale] && *names[locale] ? names[locale] : (names[0] ? names[0] : "");
}

uint32 Category(MapEntry const* map, uint32 zone)
{
    if (map->IsNonRaidDungeon())
        return 4;
    if (map->IsRaid())
        return 5;
    if (map->IsBattlegroundOrArena())
        return 6;
    switch (GetVirtualMapForMapAndZone(map->MapID, zone))
    {
        case 0: return 0;
        case 1: return 1;
        case 530: return 2;
        case 571: return 3;
        default: return 7;
    }
}

class Catalogue
{
public:
    std::map<uint32, Region> regions;
    std::set<uint32> entries;
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
            if (!data.dbData)
                continue;
            MapEntry const* map = sMapStore.LookupEntry(data.mapid);
            if (!map)
                continue;
            uint32 zone = 0;
            if (!map->IsDungeon() && !map->IsBattlegroundOrArena())
                zone = sMapMgr->GetZoneId(data.phaseMask, data.mapid, data.posX, data.posY, data.posZ);
            uint32 const modes = map->IsDungeon() ? 4 : 1;
            for (uint32 mode = 0; mode < modes; ++mode)
            {
                if (map->IsDungeon() && (!(data.spawnMask & (1u << mode)) ||
                    !GetMapDifficultyData(data.mapid, Difficulty(mode))))
                    continue;
                uint32 const key = zone ? zone : Bestiary::MapRegion(data.mapid, mode);
                Region& region = regions[key];
                region.map = data.mapid;
                region.zone = zone;
                region.category = Category(map, zone);
                region.mode = mode;
                std::set<uint32> const alternatives = {data.id, data.id2, data.id3};
                for (uint32 entry : alternatives)
                {
                    CreatureTemplate const* base = sObjectMgr->GetCreatureTemplate(entry);
                    if (!entry || !base)
                        continue;
                    uint32 const effective = Bestiary::DifficultyEntry(entry, mode, map->IsRaid(),
                        [base](uint32 index) -> uint32
                        {
                            uint32 const id = base->DifficultyEntry[index];
                            return id && sObjectMgr->GetCreatureTemplate(id) ? id : 0;
                        });
                    auto& member = region.members[effective];
                    member.first = entry;
                    ++member.second;
                    entries.insert(effective);
                }
            }
        }
        ready = !entries.empty() && !tables[0].empty() && !tables[1].empty() &&
            !tables[2].empty() && !tables[3].empty();
        LOG_INFO("server.loading", "Bestiary: loaded {} creature templates across {} regions/modes",
            entries.size(), regions.size());
        if (!ready)
            LOG_ERROR("server.loading", "Bestiary: catalogue unavailable; check map data and loot table SELECT errors");
    }

    Rows Locations(LocaleConstant locale)
    {
        Rows rows;
        for (auto const& [key, region] : regions)
        {
            MapEntry const* map = sMapStore.LookupEntry(region.map);
            MapDifficulty const* difficulty = GetMapDifficultyData(region.map, Difficulty(region.mode));
            uint32 const capacity = difficulty && difficulty->maxPlayers ? difficulty->maxPlayers : map->maxPlayers;
            std::string name = LocalName(map->name, locale);
            if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(region.zone))
                name = LocalName(area->area_name, locale);
            rows.push_back("L|" + std::to_string(key) + "|" + std::to_string(region.map) + "|" +
                std::to_string(region.zone) + "|" + std::to_string(region.category) + "|" +
                std::to_string(region.mode) + "|" + Bestiary::Escape(name) + "|" +
                std::to_string(region.members.size()) + "|" + std::to_string(map->expansionID) + "|" +
                std::to_string(capacity));
        }
        return rows;
    }

    Rows Zone(uint32 key, LocaleConstant locale)
    {
        Rows rows;
        for (auto const& [entry, member] : regions.at(key).members)
        {
            CreatureTemplate const* mob = sObjectMgr->GetCreatureTemplate(entry);
            if (!mob)
                continue;
            CreatureTemplate const* base = sObjectMgr->GetCreatureTemplate(member.first);
            std::string name = base ? base->Name : mob->Name;
            if (CreatureLocale const* loc = sObjectMgr->GetCreatureLocale(member.first))
                ObjectMgr::GetLocaleString(loc->Name, locale, name);
            rows.push_back("M|" + std::to_string(entry) + "|" + Bestiary::Escape(name) + "|" +
                std::to_string(mob->minlevel) + "|" + std::to_string(mob->maxlevel) + "|" +
                std::to_string(mob->rank) + "|" + std::to_string(member.second) + "|" +
                std::to_string(mob->type) + "|" + std::to_string(member.first));
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
        bool const valid = (request.kind == 'L' && request.target == 0) ||
            (request.kind == 'Z' && catalogue.regions.count(request.target)) ||
            (request.kind == 'D' && catalogue.entries.count(request.target));
        if (!valid)
        {
            Send(player, request.id, "X|UNKNOWN_TARGET");
            return false;
        }
        if (client.kind != request.kind || client.target != request.target)
        {
            LocaleConstant const locale = player->GetSession()->GetSessionDbLocaleIndex();
            if (request.kind == 'L')
                client.rows = catalogue.Locations(locale);
            else if (request.kind == 'Z')
                client.rows = catalogue.Zone(request.target, locale);
            else
                client.rows = catalogue.Detail(request.target, locale);
            client.kind = request.kind;
            client.target = request.target;
        }
        if (client.rows.size() >= 8192)
        {
            Send(player, request.id, "X|CATALOGUE_LIMIT");
            return false;
        }
        Rows const* rows = &client.rows;
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
