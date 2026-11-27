#include "auction_market_fixture.h"
#include "../overlays/mod-ah-bot/src/AuctionMarketStock.cpp"
#include "../overlays/mod-ah-bot/src/AuctionMarketTrade.cpp"
#include <iostream>

void Reset()
{
    for(auto const& [id,a]:auctionMgr.house.auctions)delete a;
    for(auto const& [id,item]:auctionMgr.items)delete item;
    auctionMgr.house.auctions.clear();auctionMgr.items.clear();objectMgr.templates.clear();
    dbItems.clear();searchItems.clear();mailed.clear();dbMailed.clear();replies.clear();clients.clear();
    AuctionMarket::nextSupply.clear();dbGold=1000;
    ItemTemplate copper;copper.ItemId=2770;objectMgr.templates[2770]=copper;
    AuctionMarket::Load();
}
void Lot(uint32 id,uint32 count,uint32 price,uint32 owner=99)
{
    auto a=new AuctionEntry;a->Id=id;a->itemCount=count;a->item_template=2770;a->buyout=uint64(count)*price;
    a->startbid=uint32(a->buyout*7/10);a->owner=ObjectGuid(uint64(owner));a->item_guid=ObjectGuid(uint64(id+500));
    auctionMgr.house.auctions[id]=a;auctionMgr.items[id+500]=new Item{2770,count,id+500,0};
    dbItems[id+500]=count;searchItems[id]=count;
}
void Quote20(Player& p,Client& c)
{
    Calculate(&p,&auctionMgr.house,AuctionHouseId::Alliance,{1,'Q',1234,2770,20,""},c);
}
void Confirm(Player& p,Client& c)
{
    Buy(&p,&auctionMgr.house,AuctionHouseId::Alliance,{2,'B',1234,c.quote.token,0,""},c);
}
int main()
{
    using namespace AuctionMarket;
    Request parsed;
    assert(!Parse("1|Q|4d2|2770|0",parsed));assert(!Parse("1|Q|4d2|2770|1001",parsed));
    assert(!Parse("1|B|4d2|5|extra",parsed));assert(!Parse("4294967296|B|4d2|5",parsed));
    assert(!Parse("1|L|4d2|0|00",parsed));assert(Parse("1|L|4d2|0|",parsed));
    assert(Parse("1|Q|4d2|2770|20",parsed)&&parsed.quantity==20);
    assert(AvailableToList({2,2,1,3600},2,1,20)==0);
    assert(AvailableToList({200,4,50,300},190,3,200)==10);
    Purchase overflow;
    assert(!Plan({{1,2,UINT64_MAX,99,501}},2,overflow));

    sSkillLineAbilityStore.rows={{171,1},{6,2}};
    spellMgr.spells[1].Effects={{SPELL_EFFECT_CREATE_ITEM,123}};
    spellMgr.spells[2].Effects={{SPELL_EFFECT_CREATE_ITEM,999}};
    std::set<uint32> crafted;AddCraftedItems(crafted);assert(crafted==std::set<uint32>({123}));
    Reset();Player p;Client c;Lot(1,12,10);Lot(2,25,20);Lot(3,200,1,123); // player lot must not be used
    Quote20(p,c);assert(c.quote.purchase.price==280);uint32 token=c.quote.token;
    Confirm(p,c);assert(p.gold==720&&dbGold==720);assert(!auctionMgr.house.GetAuction(1));
    assert(auctionMgr.house.GetAuction(2)->itemCount==17&&auctionMgr.house.GetAuction(2)->buyout==340);
    assert(auctionMgr.items.at(502)->count==17&&dbItems.at(502)==17&&searchItems.at(2)==17);
    assert(mailed==std::vector<uint32>({12,8})&&dbMailed==mailed);
    assert(auctionMgr.house.GetAuction(3)->itemCount==200); // no mutation of player stock
    Buy(&p,&auctionMgr.house,AuctionHouseId::Alliance,{3,'B',1234,token,0,""},c);
    assert(p.gold==720&&mailed.size()==2); // consumed token cannot replay

    Reset();p=Player{};c=Client{};Lot(1,12,10);Lot(2,25,20);Quote20(p,c);
    auctionMgr.house.GetAuction(2)->buyout+=25;Confirm(p,c);
    assert(p.gold==1000&&dbGold==1000&&mailed.empty()&&auctionMgr.house.GetAuction(1)->itemCount==12);
    Reset();p=Player{};c=Client{};Lot(1,100,10);Quote20(p,c);p.gold=1;Confirm(p,c);
    assert(p.gold==1&&mailed.empty()&&auctionMgr.house.GetAuction(1)->itemCount==100);
    Reset();p=Player{};c=Client{};Lot(1,100,10);Quote20(p,c);c.quote.expires=Clock::now();Confirm(p,c);
    assert(p.gold==1000&&mailed.empty());
    Quote20(p,c);auto q=c.quote.token;Buy(&p,&auctionMgr.house,AuctionHouseId::Horde,{2,'B',1234,q,0,""},c);
    assert(p.gold==1000&&mailed.empty());
    MarketPlayer script;p.near=false;std::string request="ACAM1\t4|Q|4d2|2770|20";
    assert(!script.OnPlayerCanUseChat(&p,CHAT_MSG_WHISPER,LANG_ADDON,request,&p));
    assert(!clients[p.GetGUID().GetRawValue()].quote.token);

    Reset();cfg.WhiteTradeGoodsBin={2770};auto cycle=Snapshot(&cfg,&auctionMgr.house);
    uint32 bin=0,amount=0;assert(cycle.Pick(bin,amount)==2770&&amount==50);
    cycle.Posted(2770,amount);assert(cycle.Pick(bin,amount)==0); // supply cooldown prevents immediate refill
    nextSupply[{2,2770}]=time(nullptr)-1;cycle.units[2770]=200;assert(cycle.Pick(bin,amount)==0);
    cfg.WhiteTradeGoodsBin.clear();Reset();
    std::cout<<"Auction market: partial/multi-lot purchase, price order, transaction contents, player isolation, stale quote, replay, funds, house and proximity guards, supply caps/cooldowns passed\n";
}
