// Local auction market policy. No core dependencies: shared by the module and policy tests.
#ifndef AUCTION_MARKET_POLICY_H
#define AUCTION_MARKET_POLICY_H

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace AuctionMarket
{
struct Limits
{
    uint32_t units = 1;
    uint32_t lots = 1;
    uint32_t stack = 1;
    uint32_t interval = 300;
};

inline Limits DefaultLimits(uint32_t itemClass, uint32_t quality, uint32_t maxStack)
{
    if ((itemClass == 7 || itemClass == 0 || itemClass == 6) && maxStack > 1)
    {
        if (quality >= 4)
            return {2, 2, 1, 3600};
        if (quality == 3)
            return {10, 2, 5, 1800};
        if (quality == 2)
            return {40, 2, 20, 900};
        return {200, 4, std::min(50u, maxStack), 300};
    }
    return {1, 1, 1, quality >= 4 ? 3600u : 900u};
}

inline uint32_t AvailableToList(Limits const& limits, uint64_t units, uint32_t lots, uint32_t maxStack)
{
    if (units >= limits.units || lots >= limits.lots)
        return 0;
    return uint32_t(std::min<uint64_t>({limits.units - units, limits.stack, maxStack}));
}

struct Offer
{
    uint32_t id = 0;
    uint32_t count = 0;
    uint64_t unitPrice = 0;
    uint64_t owner = 0;
    uint64_t itemGuid = 0;
};

struct Part
{
    Offer offer;
    uint32_t take = 0;
};

struct Purchase
{
    uint32_t quantity = 0;
    uint64_t price = 0;
    std::vector<Part> parts;
};

inline bool Plan(std::vector<Offer> offers, uint32_t quantity, Purchase& result)
{
    result = {};
    if (!quantity || quantity > 1000)
        return false;
    std::sort(offers.begin(), offers.end(), [](Offer const& a, Offer const& b)
    {
        return a.unitPrice == b.unitPrice ? a.id < b.id : a.unitPrice < b.unitPrice;
    });
    uint32_t remaining = quantity;
    for (Offer const& offer : offers)
    {
        if (!offer.count || !offer.unitPrice)
            continue;
        uint32_t take = std::min(remaining, offer.count);
        if (offer.unitPrice > (std::numeric_limits<uint64_t>::max() - result.price) / take)
            return false;
        result.price += offer.unitPrice * take;
        result.parts.push_back({offer, take});
        remaining -= take;
        if (!remaining)
        {
            result.quantity = quantity;
            return result.parts.size() <= 128;
        }
    }
    result = {};
    return false;
}

inline bool SameOffer(Offer const& a, Offer const& b)
{
    return a.id == b.id && a.count == b.count && a.unitPrice == b.unitPrice &&
        a.owner == b.owner && a.itemGuid == b.itemGuid;
}
}
#endif
