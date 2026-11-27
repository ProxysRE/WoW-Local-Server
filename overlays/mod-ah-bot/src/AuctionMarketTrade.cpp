#include "AuctionMarket.h"
#include "AuctionMarketProtocol.h"
#include "AuctionHouseBotCommon.h"
#include "AuctionHouseBotConfig.h"
#include "AuctionHouseMgr.h"
#include "AuctionHouseSearcher.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Util.h"
#include "WorldSession.h"
#include <chrono>
#include <ctime>
#include <map>
#include <memory>
#include <string>

namespace
{
using Clock = std::chrono::steady_clock;
std::string const Prefix = "ACAM1\t";
struct Quote
{
    uint32 token = 0;
    uint32 item = 0;
    AuctionHouseId house = AuctionHouseId::Neutral;
    uint64 npc = 0;
    AuctionMarket::Purchase purchase;
    Clock::time_point expires;
};
struct Client
{
    Clock::time_point last;
    Quote quote;
};
std::map<uint64, Client> clients;
uint32 nextToken = 0;

void Send(Player* player, uint32 request, std::string const& payload)
{
    WorldPacket packet;
    std::string frame = Prefix + std::to_string(request) + "|" + payload;
    if (frame.size() > 254)
        return;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, player, player, frame);
    player->SendDirectMessage(&packet);
}

bool Commodity(ItemTemplate const* item)
{
    return item && (item->Class == 0 || item->Class == 6 || item->Class == 7) &&
        item->GetMaxStackSize() > 1 && item->Bonding != BIND_WHEN_PICKED_UP &&
        item->Bonding != BIND_QUEST_ITEM && !item->Duration && !item->RandomProperty &&
        !item->RandomSuffix && !item->HasFlag(ITEM_FLAG_HAS_LOOT) && !item->HasFlag(ITEM_FLAG_CONJURED);
}

bool OfferFor(Player* player, AuctionEntry const* auction, AuctionMarket::Offer& offer)
{
    if (!auction || !gBotsId.count(auction->owner.GetCounter()) || auction->bidder || auction->bid ||
        auction->expire_time <= time(nullptr) || !auction->itemCount || !auction->buyout ||
        auction->buyout % auction->itemCount ||
        sCharacterCache->GetCharacterAccountIdByGuid(auction->owner.GetCounter()) ==
            player->GetSession()->GetAccountId())
        return false;
    Item* item = sAuctionMgr->GetAItem(auction->item_guid);
    if (!item || item->GetCount() != auction->itemCount || !Commodity(item->GetTemplate()) ||
        item->GetItemRandomPropertyId() || item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT) ||
        item->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
        return false;
    offer = {auction->Id, auction->itemCount, auction->buyout / auction->itemCount,
        auction->owner.GetRawValue(), auction->item_guid.GetRawValue()};
    return true;
}

std::string Name(uint32 id, LocaleConstant locale)
{
    ItemTemplate const* item = sObjectMgr->GetItemTemplate(id);
    std::string name = item ? item->Name1 : "";
    if (ItemLocale const* loc = sObjectMgr->GetItemLocale(id))
        ObjectMgr::GetLocaleString(loc->Name, locale, name);
    return name;
}

std::wstring Fold(std::string const& text)
{
    std::wstring result;
    if (!Utf8toWStr(text, result))
        return {};
    wstrToLower(result);
    return result;
}

void List(Player* player, AuctionHouseObject* house, AuctionMarket::Request const& request)
{
    struct Row { uint64 stock = 0; uint64 price = UINT64_MAX; };
    std::map<uint32, Row> rows;
    for (auto it = house->GetAuctionsBegin(); it != house->GetAuctionsEnd(); ++it)
    {
        AuctionMarket::Offer offer;
        if (!OfferFor(player, it->second, offer))
            continue;
        auto& row = rows[it->second->item_template];
        row.stock += offer.count;
        row.price = std::min(row.price, offer.unitPrice);
    }
    auto locale = player->GetSession()->GetSessionDbLocaleIndex();
    auto query = Fold(request.query);
    if (!request.query.empty() && query.empty())
    {
        Send(player, request.id, "X|BAD_SEARCH");
        return;
    }
    std::vector<uint32> matches;
    for (auto const& [item, row] : rows)
        if (query.empty() || Fold(Name(item, locale)).find(query) != std::wstring::npos ||
            request.query == std::to_string(item))
            matches.push_back(item);
    size_t begin = size_t(request.target) * 12;
    Send(player, request.id, "P|" + std::to_string(request.target) + "|" +
        std::to_string(matches.size() > begin + 12));
    for (size_t i = begin; i < std::min(begin + 12, matches.size()); ++i)
    {
        uint32 item = matches[i];
        auto name = Name(item, locale);
        size_t size = std::min(name.size(), size_t(60));
        while (size && (static_cast<unsigned char>(name[size]) & 0xC0) == 0x80)
            --size;
        name.resize(size);
        Send(player, request.id, "I|" + std::to_string(item) + "|" + AuctionMarket::Hex(name) + "|" +
            std::to_string(rows[item].stock) + "|" + std::to_string(rows[item].price));
    }
    Send(player, request.id, "D");
}

void Calculate(Player* player, AuctionHouseObject* house, AuctionHouseId houseId,
    AuctionMarket::Request const& request, Client& client)
{
    client.quote = {};
    std::vector<AuctionMarket::Offer> offers;
    for (auto it = house->GetAuctionsBegin(); it != house->GetAuctionsEnd(); ++it)
    {
        AuctionMarket::Offer offer;
        if (it->second->item_template == request.target && OfferFor(player, it->second, offer))
            offers.push_back(offer);
    }
    AuctionMarket::Purchase purchase;
    if (!AuctionMarket::Plan(std::move(offers), request.quantity, purchase))
    {
        Send(player, request.id, "X|NOT_ENOUGH_STOCK");
        return;
    }
    if (purchase.price > 2147483647u || player->GetMoney() < purchase.price)
    {
        Send(player, request.id, "X|NOT_ENOUGH_MONEY");
        return;
    }
    if (!++nextToken)
        ++nextToken;
    client.quote = {nextToken, request.target, houseId, request.npc,
        std::move(purchase), Clock::now() + std::chrono::seconds(30)};
    Send(player, request.id, "Q|" + std::to_string(nextToken) + "|" + std::to_string(request.target) + "|" +
        std::to_string(client.quote.purchase.quantity) + "|" + std::to_string(client.quote.purchase.price));
}

void Buy(Player* player, AuctionHouseObject* house, AuctionHouseId houseId,
    AuctionMarket::Request const& request, Client& client)
{
    // Consume before validation: a rejected, expired or successful confirmation cannot be replayed.
    Quote quote = std::move(client.quote);
    client.quote = {};
    if (quote.token != request.target || quote.npc != request.npc || quote.house != houseId ||
        Clock::now() >= quote.expires || quote.purchase.parts.empty())
    {
        Send(player, request.id, "X|QUOTE_EXPIRED");
        return;
    }
    if (player->GetMoney() < quote.purchase.price)
    {
        Send(player, request.id, "X|NOT_ENOUGH_MONEY");
        return;
    }
    // Validate every leg before mutating any gold, item, auction or mail.
    std::vector<std::unique_ptr<Item>> items;
    for (auto const& part : quote.purchase.parts)
    {
        AuctionEntry* auction = house->GetAuction(part.offer.id);
        AuctionMarket::Offer current;
        if (!OfferFor(player, auction, current) || auction->item_template != quote.item ||
            !AuctionMarket::SameOffer(part.offer, current))
        {
            Send(player, request.id, "X|OFFERS_CHANGED");
            return;
        }
        items.emplace_back(sAuctionMgr->GetAItem(auction->item_guid)->CloneItem(part.take, player));
        if (!items.back())
        {
            Send(player, request.id, "X|ITEM_ERROR");
            return;
        }
    }
    auto trans = CharacterDatabase.BeginTransaction();
    player->ModifyMoney(-int32(quote.purchase.price));
    for (auto const& part : quote.purchase.parts)
    {
        AuctionEntry* auction = house->GetAuction(part.offer.id);
        Item* original = sAuctionMgr->GetAItem(auction->item_guid);
        if (part.take == auction->itemCount)
        {
            auction->DeleteFromDB(trans);
            sAuctionMgr->RemoveAItem(auction->item_guid, true, &trans);
            house->RemoveAuction(auction);
        }
        else
        {
            // Searcher stores copies: refresh it as well as the live lot and database row.
            sAuctionMgr->GetAuctionHouseSearcher()->RemoveAuction(auction);
            auction->DeleteFromDB(trans);
            uint64 unit = part.offer.unitPrice;
            auction->startbid = uint32(uint64(auction->startbid) *
                (auction->itemCount - part.take) / auction->itemCount);
            auction->itemCount -= part.take;
            auction->buyout = unit * auction->itemCount;
            original->SetCount(auction->itemCount);
            original->SetState(ITEM_CHANGED);
            original->SaveToDB(trans);
            auction->SaveToDB(trans);
            sAuctionMgr->GetAuctionHouseSearcher()->AddAuction(auction);
        }
    }
    for (size_t i = 0; i < items.size();)
    {
        MailDraft mail("Auction Market", "Purchased goods");
        for (uint32 slot = 0; slot < MAX_MAIL_ITEMS && i < items.size(); ++slot, ++i)
        {
            items[i]->SaveToDB(trans);
            mail.AddItem(items[i].release());
        }
        auto seller = ObjectGuid(quote.purchase.parts.front().offer.owner).GetCounter();
        mail.SendMailTo(trans, MailReceiver(player), MailSender(MAIL_NORMAL, seller, MAIL_STATIONERY_AUCTION));
    }
    player->SaveInventoryAndGoldToDB(trans);
    // Gold, remaining stock, removed lots and mailed items share the native character DB transaction.
    CharacterDatabase.CommitTransaction(trans);
    AHBConfig* config = houseId == AuctionHouseId::Alliance ? gAllianceConfig :
        houseId == AuctionHouseId::Horde ? gHordeConfig : gNeutralConfig;
    config->UpdateItemStats(quote.item, quote.purchase.quantity, quote.purchase.price);
    LOG_INFO("module", "AuctionMarket: buyer={}, item={}, quantity={}, price={}",
        player->GetGUID().ToString(), quote.item, quote.purchase.quantity, quote.purchase.price);
    Send(player, request.id, "OK|" + std::to_string(quote.purchase.quantity) + "|" + std::to_string(quote.purchase.price));
}

class MarketPlayer : public PlayerScript
{
public:
    MarketPlayer() : PlayerScript("AuctionMarketPlayer",
        {PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_LOGOUT}) { }
    void OnPlayerLogout(Player* player) override
    {
        clients.erase(player->GetGUID().GetRawValue());
    }
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language,
        std::string& message, Player* receiver) override
    {
        if (language != LANG_ADDON || type != CHAT_MSG_WHISPER ||
            message.compare(0, Prefix.size(), Prefix) != 0)
            return true;
        if (receiver != player || !AuctionMarket::Enabled())
            return false;
        AuctionMarket::Request request;
        if (!AuctionMarket::Parse(std::string_view(message).substr(Prefix.size()), request))
            return false;
        Client& client = clients[player->GetGUID().GetRawValue()];
        if (Clock::now() - client.last < std::chrono::milliseconds(200))
        {
            Send(player, request.id, "X|WAIT");
            return false;
        }
        client.last = Clock::now();
        Creature* npc = player->GetNPCIfCanInteractWith(ObjectGuid(request.npc), UNIT_NPC_FLAG_AUCTIONEER);
        if (!npc)
        {
            client.quote = {};
            Send(player, request.id, "X|AT_AUCTIONEER_ONLY");
            return false;
        }
        auto entry = sAuctionMgr->GetAuctionHouseEntryFromFactionTemplate(npc->GetFaction());
        auto house = sAuctionMgr->GetAuctionsMap(npc->GetFaction());
        if (!entry || !house)
            return false;
        AuctionHouseId houseId = AuctionHouseId(entry->houseId);
        if (request.action == 'L')
            List(player, house, request);
        else if (request.action == 'Q')
            Calculate(player, house, houseId, request, client);
        else
            Buy(player, house, houseId, request, client);
        return false;
    }
};
}
void AddAuctionMarketScripts()
{
    new MarketPlayer();
}
