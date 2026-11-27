// Minimal stateful core contract for exercising the actual market implementation.
#pragma once
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
using uint32=uint32_t; using uint64=uint64_t; using int32=int32_t;
constexpr uint32 BIND_WHEN_PICKED_UP=1, BIND_QUEST_ITEM=4, ITEM_FLAG_HAS_LOOT=4, ITEM_FLAG_CONJURED=2;
constexpr uint32 PERM_ENCHANTMENT_SLOT=0, TEMP_ENCHANTMENT_SLOT=1, ITEM_CHANGED=1;
constexpr uint32 CHAT_MSG_WHISPER=1, LANG_ADDON=0xffffffff, UNIT_NPC_FLAG_AUCTIONEER=1;
constexpr uint32 PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT=1, PLAYERHOOK_ON_LOGOUT=2;
constexpr uint32 MAX_MAIL_ITEMS=12, MAIL_NORMAL=0, MAIL_STATIONERY_AUCTION=62;
using LocaleConstant=int;
enum class AuctionHouseId : uint32 {Alliance=2,Horde=6,Neutral=7};
struct ObjectGuid
{
    uint64 value=0; ObjectGuid()=default; explicit ObjectGuid(uint64 v):value(v){}
    uint64 GetRawValue() const{return value;} uint32 GetCounter() const{return uint32(value);}
    std::string ToString() const{return std::to_string(value);} explicit operator bool() const{return value!=0;}
};
struct ItemTemplate
{
    uint32 ItemId=1, Class=7, SubClass=0, InventoryType=0, RequiredLevel=0, Quality=1;
    uint32 Bonding=0, Duration=0, RandomProperty=0, RandomSuffix=0, flags=0, stack=200;
    std::string Name1="Copper";
    uint32 GetMaxStackSize() const{return stack;} bool HasFlag(uint32 f) const{return flags&f;}
};
struct ItemLocale {std::string Name;};
struct WorldSession {uint32 GetAccountId() const{return 1;} int GetSessionDbLocaleIndex() const{return 0;}};
struct Creature {uint32 GetFaction() const{return 2;}};
struct Tx {std::vector<std::function<void()>> ops;};
using CharacterDatabaseTransaction=std::shared_ptr<Tx>;
inline std::map<uint32,uint32> dbItems, searchItems;
inline std::vector<uint32> mailed, dbMailed;
inline uint64 dbGold=0; inline uint32 nextItem=10000;
struct Player;
struct Item
{
    uint32 id=0,count=0,guid=0,state=0;
    ItemTemplate const* GetTemplate() const;
    uint32 GetCount() const{return count;} uint32 GetItemRandomPropertyId() const{return 0;}
    uint32 GetEnchantmentId(uint32) const{return 0;}
    Item* CloneItem(uint32 qty,Player*) const {return new Item{id,qty,++nextItem,0};}
    void SetCount(uint32 qty){count=qty;} void SetState(uint32 s){state=s;}
    void SaveToDB(CharacterDatabaseTransaction tx){auto g=guid,c=count;tx->ops.push_back([g,c]{dbItems[g]=c;});}
};
struct AuctionEntry
{
    uint32 Id=0,itemCount=0,item_template=0; uint64 buyout=0; uint32 startbid=0,bid=0;
    ObjectGuid owner,item_guid,bidder; time_t expire_time=time(nullptr)+1000;
    void DeleteFromDB(CharacterDatabaseTransaction) const{}
    void SaveToDB(CharacterDatabaseTransaction) const{}
};
struct AuctionHouseObject
{
    std::map<uint32,AuctionEntry*> auctions;
    auto GetAuctionsBegin(){return auctions.begin();} auto GetAuctionsEnd(){return auctions.end();}
    AuctionEntry* GetAuction(uint32 id){auto i=auctions.find(id);return i==auctions.end()?nullptr:i->second;}
    bool RemoveAuction(AuctionEntry* a){searchItems.erase(a->Id);auctions.erase(a->Id);delete a;return true;}
};
struct AuctionHouseSearcher
{
    void RemoveAuction(AuctionEntry* a){searchItems.erase(a->Id);}
    void AddAuction(AuctionEntry* a){searchItems[a->Id]=a->itemCount;}
};
struct AuctionHouseEntry {uint32 houseId=2;};
struct FakeAuctionMgr
{
    AuctionHouseObject house;AuctionHouseSearcher search;AuctionHouseEntry entry;std::map<uint32,Item*> items;
    Item* GetAItem(ObjectGuid g){auto i=items.find(g.GetCounter());return i==items.end()?nullptr:i->second;}
    bool RemoveAItem(ObjectGuid g,bool remove,CharacterDatabaseTransaction* tx)
    {
        if(remove){auto id=g.GetCounter();(*tx)->ops.push_back([id]{dbItems.erase(id);});delete items[id];}
        items.erase(g.GetCounter());return true;
    }
    AuctionHouseSearcher* GetAuctionHouseSearcher(){return &search;}
    AuctionHouseEntry const* GetAuctionHouseEntryFromFactionTemplate(uint32){return &entry;}
    AuctionHouseObject* GetAuctionsMap(uint32){return &house;}
};
inline FakeAuctionMgr auctionMgr;inline auto sAuctionMgr=&auctionMgr;
struct FakeObjectMgr
{
    std::map<uint32,ItemTemplate> templates;
    ItemTemplate const* GetItemTemplate(uint32 id){auto i=templates.find(id);return i==templates.end()?nullptr:&i->second;}
    ItemLocale const* GetItemLocale(uint32){return nullptr;}
    static void GetLocaleString(std::string const&,int,std::string&){}
};
using ObjectMgr=FakeObjectMgr;inline FakeObjectMgr objectMgr;inline auto sObjectMgr=&objectMgr;
inline ItemTemplate const* Item::GetTemplate() const{return sObjectMgr->GetItemTemplate(id);}
struct FakeCache {uint32 GetCharacterAccountIdByGuid(uint32 id){return id==99?102:1;}};
inline FakeCache cache;inline auto sCharacterCache=&cache;
struct WorldPacket {std::string text;};
inline std::vector<std::string> replies;
struct Player
{
    uint64 gold=1000;bool near=true;WorldSession session;Creature npc;
    uint64 GetMoney() const{return gold;}void ModifyMoney(int32 n){gold+=n;}
    ObjectGuid GetGUID() const{return ObjectGuid(uint64(1));}
    WorldSession* GetSession(){return &session;}
    Creature* GetNPCIfCanInteractWith(ObjectGuid g,uint32){return near&&g.value==1234?&npc:nullptr;}
    void SaveInventoryAndGoldToDB(CharacterDatabaseTransaction tx){auto value=gold;tx->ops.push_back([value]{dbGold=value;});}
    void SendDirectMessage(WorldPacket* packet){replies.push_back(packet->text);}
};
struct ChatHandler
{
    static void BuildChatPacket(WorldPacket& p,uint32,uint32,Player*,Player*,std::string const& s){p.text=s;}
};
struct MailReceiver {explicit MailReceiver(Player*){}};
struct MailSender {MailSender(uint32,uint32,uint32){}};
struct MailDraft
{
    std::vector<Item*> items;
    MailDraft(std::string,std::string){}
    MailDraft& AddItem(Item* i){items.push_back(i);return *this;}
    void SendMailTo(CharacterDatabaseTransaction tx,MailReceiver,MailSender)
    {
        for(Item* i:items){auto c=i->count;mailed.push_back(c);tx->ops.push_back([c]{dbMailed.push_back(c);});delete i;}
    }
};
struct FakeCharacterDB
{
    CharacterDatabaseTransaction BeginTransaction(){return std::make_shared<Tx>();}
    void CommitTransaction(CharacterDatabaseTransaction tx){for(auto& op:tx->ops)op();}
};
inline FakeCharacterDB CharacterDatabase;
struct Field {template<class T>T Get(){return T{};}};
struct Result {explicit operator bool() const{return false;}Result* operator->(){return this;}Field* Fetch(){static Field f[6];return f;}bool NextRow(){return false;}};
using QueryResult=Result;
struct FakeWorldDB {Result Query(std::string const&){return {};}};
inline FakeWorldDB WorldDatabase;
struct FakeConfig {template<class T>T GetOption(char const*,T v){return v;}};
inline FakeConfig config;inline auto sConfigMgr=&config;
inline std::set<uint32> gBotsId={99};
struct AHBConfig
{
    std::set<uint32> GreyTradeGoodsBin,WhiteTradeGoodsBin,GreenTradeGoodsBin,BlueTradeGoodsBin,PurpleTradeGoodsBin,OrangeTradeGoodsBin,YellowTradeGoodsBin;
    std::set<uint32> GreyItemsBin,WhiteItemsBin,GreenItemsBin,BlueItemsBin,PurpleItemsBin,OrangeItemsBin,YellowItemsBin;
    uint32 GetAHID(){return 2;}void UpdateItemStats(uint32,uint32,uint64){}
};
inline AHBConfig cfg;inline auto gAllianceConfig=&cfg;inline auto gHordeConfig=&cfg;inline auto gNeutralConfig=&cfg;
struct PlayerScript
{
    PlayerScript(char const*,std::initializer_list<uint32>){}virtual ~PlayerScript()=default;
    virtual void OnPlayerLogout(Player*){}
    virtual bool OnPlayerCanUseChat(Player*,uint32,uint32,std::string&,Player*){return true;}
};
inline bool Utf8toWStr(std::string const& in,std::wstring& out){out.assign(in.begin(),in.end());return true;}
inline void wstrToLower(std::wstring& s){for(auto& c:s)if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';}
inline uint32 urand(uint32 a,uint32){return a;}
#define LOG_INFO(...) (void)0

struct SkillLineAbilityEntry {uint32 SkillLine=0,Spell=0;};
struct SkillStore
{
    std::vector<SkillLineAbilityEntry> rows;
    uint32 GetNumRows() const{return uint32(rows.size());}
    SkillLineAbilityEntry const* LookupEntry(uint32 i) const{return &rows[i];}
};
inline SkillStore sSkillLineAbilityStore;
constexpr uint32 SPELL_EFFECT_CREATE_ITEM=24;
struct EffectData {uint32 Effect=0,ItemType=0;};
struct SpellInfo {std::vector<EffectData> Effects;};
struct FakeSpellMgr
{
    std::map<uint32,SpellInfo> spells;
    SpellInfo const* GetSpellInfo(uint32 id){auto it=spells.find(id);return it==spells.end()?nullptr:&it->second;}
};
inline FakeSpellMgr spellMgr;inline auto sSpellMgr=&spellMgr;
