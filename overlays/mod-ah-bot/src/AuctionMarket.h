#ifndef AUCTION_MARKET_H
#define AUCTION_MARKET_H
#include "AuctionMarketPolicy.h"
#include "Common.h"
#include <map>
#include <set>
#include <vector>
class AHBConfig;
class AuctionHouseObject;
struct ItemTemplate;
namespace AuctionMarket
{
void Load();
bool Enabled();
uint32 ListingLimit();
bool AllowOverride(uint32 item);
void AddCraftedItems(std::set<uint32>& items);
struct StockCycle
{
    struct Entry
    {
        uint32 item;
        uint32 bin;
        uint32 group;
        uint32 itemClass;
        uint32 maxStack;
        Limits limits;
    };
    uint32 house = 0;
    std::vector<Entry> entries;
    std::map<uint32, uint64> units;
    std::map<uint32, uint32> lots;
    std::map<uint32, uint32> groupLots;
    uint32 Pick(uint32& bin, uint32& count);
    void Posted(uint32 item, uint32 count);
};
StockCycle Snapshot(AHBConfig* config, AuctionHouseObject* house);
}
void AddAuctionMarketScripts();
#endif
