/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file AuctionHouseBotBuyer.cpp
 * @brief 拍卖行机器人买家代理实现文件
 *
 * 本文件实现了拍卖行机器人的买家代理(AuctionBotBuyer),负责在拍卖行中
 * 自动购买和竞拍物品,模拟真实玩家的购买行为。
 *
 * 核心算法:
 * 1. 价格评估算法:
 *    - 使用物品的出售价格或配置的基础价格作为基准
 *    - 价格越接近基准价格,购买几率越高
 *    - 几率计算使用指数函数: chance = min(100, 100^(1 + (1 - priceRatio) / factor))
 *
 * 2. 市场调节算法:
 *    - 统计同类物品的市场平均价格
 *    - 如果物品数量超过5个,根据市场均价调整几率
 *    - 使用平方根函数平滑价格影响
 *
 * 3. 几率调整因子:
 *    - 品质倍数: 不同品质物品有不同的基础几率
 *    - 玩家竞拍: 已有玩家竞拍的物品,几率降低为1/5
 *    - 检查间隔: 避免频繁检查同一拍卖项
 *
 * 工作流程:
 * 1. Update() -> GetItemInformation() 收集物品信息
 * 2. Update() -> PrepareListOfEntry() 准备待处理列表
 * 3. Update() -> BuyAndBidItems() 执行购买和竞拍
 * 4. BuyAndBidItems() -> RollBuyChance/RollBidChance() 计算几率
 * 5. BuyAndBidItems() -> BuyEntry/PlaceBidToEntry() 执行操作
 *
 * 性能优化:
 * - 使用检查间隔避免频繁扫描
 * - 每次更新限制处理的物品数量
 * - 使用缓存减少数据库查询
 */

#include "AuctionHouseBotBuyer.h"
#include "GameTime.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Random.h"

/**
 * @brief AuctionBotBuyer构造函数
 *
 * 初始化检查间隔为20分钟,并初始化各个拍卖行的配置。
 */
AuctionBotBuyer::AuctionBotBuyer() : _checkInterval(20 * MINUTE)
{
    // 为每个拍卖行类型初始化配置
    for (int i = 0; i < MAX_AUCTION_HOUSE_TYPE; ++i)
        _houseConfig[i].Initialize(AuctionHouseType(i));
}

/**
 * @brief AuctionBotBuyer析构函数
 */
AuctionBotBuyer::~AuctionBotBuyer()
{
}

/**
 * @brief 初始化买家代理
 * @return 如果至少有一个拍卖行的买家启用返回true
 *
 * 加载配置并设置检查间隔。检查间隔控制买家扫描拍卖行的频率。
 */
bool AuctionBotBuyer::Initialize()
{
    LoadConfig();

    // 检查是否至少有一个拍卖行的买家启用
    bool activeHouse = false;
    for (int i = 0; i < MAX_AUCTION_HOUSE_TYPE; ++i)
    {
        if (_houseConfig[i].BuyerEnabled)
        {
            activeHouse = true;
            break;
        }
    }

    if (!activeHouse)
        return false;

    // 加载检查间隔(配置单位为分钟,转换为秒)
    _checkInterval = sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_RECHECK_INTERVAL) * MINUTE;
    TC_LOG_DEBUG("ahbot", "AHBot buyer interval is {} minutes", _checkInterval / MINUTE);
    return true;
}

/**
 * @brief 加载买家配置
 *
 * 从全局配置管理器加载各个拍卖行的买家启用状态。
 */
void AuctionBotBuyer::LoadConfig()
{
    for (int i = 0; i < MAX_AUCTION_HOUSE_TYPE; ++i)
    {
        _houseConfig[i].BuyerEnabled = sAuctionBotConfig->GetConfigBuyerEnabled(AuctionHouseType(i));
        if (_houseConfig[i].BuyerEnabled)
            LoadBuyerValues(_houseConfig[i]);
    }
}

/**
 * @brief 加载买家特定值
 * @param config 买家配置
 *
 * 当前为空实现,预留给未来扩展使用。
 */
void AuctionBotBuyer::LoadBuyerValues(BuyerConfiguration& /* config */)
{

}

/**
 * @brief 更新指定拍卖行的买家状态
 * @param houseType 要处理的拍卖行类型
 * @return 如果执行了操作返回true,如果该拍卖行买家未启用返回false
 *
 * 此方法由主管理器定期调用,执行完整的买家扫描和购买流程:
 * 1. 收集拍卖行物品信息
 * 2. 准备待处理拍卖项列表
 * 3. 执行购买和竞拍操作
 *
 * 调用时机: 世界更新循环中,由AuctionHouseBot::Update()轮流调用
 * 性能注意: 每次调用最多处理配置数量的物品,避免长时间阻塞
 */
bool AuctionBotBuyer::Update(AuctionHouseType houseType)
{
    // 检查该拍卖行的买家是否启用
    if (!sAuctionBotConfig->GetConfigBuyerEnabled(houseType))
        return false;

    TC_LOG_DEBUG("ahbot", "AHBot: {} buying ...", AuctionBotConfig::GetHouseTypeName(houseType));

    BuyerConfiguration& config = _houseConfig[houseType];

    // 收集拍卖行物品信息
    uint32 eligibleItems = GetItemInformation(config);
    if (eligibleItems)
    {
        // 准备待处理列表 - 移除过期项
        PrepareListOfEntry(config);
        // 执行购买和竞拍操作
        BuyAndBidItems(config);
    }

    return true;
}

/**
 * @brief 收集拍卖行物品信息
 * @param config 买家配置
 * @return 符合条件的拍卖项数量
 *
 * 扫描拍卖行中的所有拍卖项,执行以下操作:
 * 1. 统计同类物品的价格信息(最小价格、平均价格等)
 * 2. 筛选出符合条件的拍卖项(可购买或竞拍的)
 *
 * 统计信息用途:
 * - 计算市场平均价格,帮助决策是否购买
 * - 判断物品价格是否合理
 *
 * 筛选条件:
 * - 过滤掉机器人自己上架的物品
 * - 只保留未出价或玩家已出价的拍卖项
 * - 更新拍卖项的存在时间戳
 *
 * 性能注意: 此函数遍历所有拍卖项,在物品数量较多时可能耗时
 */
uint32 AuctionBotBuyer::GetItemInformation(BuyerConfiguration& config)
{
    // 清空之前的统计信息
    config.SameItemInfo.clear();
    time_t now = GameTime::GetGameTime();
    uint32 count = 0;

    // 获取拍卖行对象
    AuctionHouseObject* house = sAuctionMgr->GetAuctionsMap(config.GetHouseType());

    // 遍历所有拍卖项
    for (AuctionHouseObject::AuctionEntryMap::const_iterator itr = house->GetAuctionsBegin(); itr != house->GetAuctionsEnd(); ++itr)
    {
        AuctionEntry* entry = itr->second;

        // 跳过机器人上架的拍卖项
        if (!entry->owner || sAuctionBotConfig->IsBotChar(entry->owner))
            continue;

        // 获取拍卖物品
        Item* item = sAuctionMgr->GetAItem(entry->itemGUIDLow);
        if (!item)
            continue;

        // 获取或创建该物品类型的统计信息
        BuyerItemInfo& itemInfo = config.SameItemInfo[item->GetEntry()];

        // 更新竞拍价格统计
        uint32 itemBidPrice = entry->startbid / item->GetCount();
        itemInfo.TotalBidPrice = itemInfo.TotalBidPrice + itemBidPrice;
        itemInfo.BidItemCount++;

        // 更新最小竞拍价格
        if (!itemInfo.MinBidPrice)
            itemInfo.MinBidPrice = itemBidPrice;
        else
            itemBidPrice = std::min(itemInfo.MinBidPrice, itemBidPrice);

        // 如果有一口价,更新一口价统计
        if (entry->buyout)
        {
            uint32 itemBuyPrice = entry->buyout / item->GetCount();
            itemInfo.TotalBuyPrice = itemInfo.TotalBuyPrice + itemBuyPrice;
            itemInfo.BuyItemCount++;

            // 更新最小一口价
            if (!itemInfo.MinBuyPrice)
                itemInfo.MinBuyPrice = itemBuyPrice;
            else
                itemInfo.MinBuyPrice = std::min(itemInfo.MinBuyPrice, itemBuyPrice);
        }

        // 添加到符合条件的拍卖项列表
        // 条件: 无竞拍 或 已有竞拍者(可能是玩家)
        if (!entry->bid || entry->bidder)
        {
            config.EligibleItems[entry->Id].LastExist = now;
            config.EligibleItems[entry->Id].AuctionId = entry->Id;
            ++count;
        }
    }

    TC_LOG_DEBUG("ahbot", "AHBot: {} items added to buyable/biddable vector for ah type: {}", count, config.GetHouseType());
    TC_LOG_DEBUG("ahbot", "AHBot: SameItemInfo size = {}", (uint32)config.SameItemInfo.size());
    return count;
}

/**
 * @brief 掷骰决定是否购买(一口价)
 * @param ahInfo 物品统计信息(可为NULL)
 * @param item 物品对象
 * @param auction 拍卖项
 * @param bidPrice 竞拍价格(未使用)
 * @return 如果决定购买返回true
 *
 * 购买几率计算算法:
 * 1. 获取基准价格:
 *    - 优先使用物品的出售价格(SellPrice)
 *    - 如果没有出售价格,使用配置的基础价格
 *    - 基准价格乘以1.4(包含拍卖行抽成)
 *
 * 2. 计算基础几率:
 *    - 使用指数函数: chance = min(100, 100^(1 + (1 - priceRatio) / factor))
 *    - priceRatio = 物品价格 / 基准价格
 *    - priceRatio越小(价格越低),几率越高
 *
 * 3. 市场调节:
 *    - 如果有市场统计信息且物品数量>5,根据市场均价调整几率
 *    - 使用平方根函数平滑价格差异的影响
 *
 * 4. 几率调整:
 *    - 如果已有玩家竞拍,几率降低为1/5
 *    - 乘以品质几率倍数
 *
 * 5. 随机判定:
 *    - 生成0-100的随机数
 *    - 如果随机数<=几率,返回true
 *
 * 性能注意: 使用简单的数学运算,性能开销很小
 */
bool AuctionBotBuyer::RollBuyChance(BuyerItemInfo const* ahInfo, Item const* item, AuctionEntry const* auction, uint32 /*bidPrice*/)
{
    // 如果没有一口价,无法购买
    if (!auction->buyout)
        return false;

    // 计算物品单价
    float itemBuyPrice = float(auction->buyout / item->GetCount());

    // 获取基准价格
    float itemPrice = float(item->GetTemplate()->SellPrice ? item->GetTemplate()->SellPrice : GetVendorPrice(item->GetTemplate()->Quality));
    // 基准价格乘以1.4,包含拍卖行抽成,但避免价格正好等于基准时100%购买
    itemPrice *= 1.4f;

    // 计算购买几率
    // 几率范围0-100,>=100表示100%几率,<0表示0%几率
    float chance = std::min(100.f, std::pow(100.f, 1.f + (1.f - itemBuyPrice / itemPrice) / sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_CHANCE_FACTOR)));

    // 如果已有玩家竞拍,几率降低为1/5
    if (auction->bidder)
        chance = chance / 5.f;

    // 如果有市场统计信息,根据市场均价调整几率
    if (ahInfo)
    {
        float avgBuyPrice = ahInfo->TotalBuyPrice / float(ahInfo->BuyItemCount);

        TC_LOG_DEBUG("ahbot", "AHBot: buyout average: {:.1f} items with buyout: {}", avgBuyPrice, ahInfo->BuyItemCount);

        // 如果有超过5个同类物品,考虑市场均价
        if (ahInfo->BuyItemCount > 5)
            chance *= 1.f / std::sqrt(itemBuyPrice / avgBuyPrice);
    }

    // 应用品质几率倍数
    chance *= GetChanceMultiplier(item->GetTemplate()->Quality) / 100.0f;

    // 随机判定
    float rand = frand(0.f, 100.f);
    bool win = rand <= chance;
    TC_LOG_DEBUG("ahbot", "AHBot: {} BUY! chance = {:.2f}, price = {}, buyprice = {}.", win ? "WIN" : "LOSE", chance, uint32(itemPrice), uint32(itemBuyPrice));
    return win;
}

/**
 * @brief 掷骰决定是否竞拍
 * @param ahInfo 物品统计信息(可为NULL)
 * @param item 物品对象
 * @param auction 拍卖项
 * @param bidPrice 竞拍价格
 * @return 如果决定竞拍返回true
 *
 * 竞拍几率计算算法与购买类似,但有以下区别:
 * 1. 使用竞拍价格而非一口价
 * 2. 如果玩家(非机器人)已竞拍,几率降低为1/5
 * 3. 根据竞拍市场的平均价格调整几率
 *
 * 算法细节:
 * - 基准价格同样使用出售价格或配置的基础价格
 * - 使用相同的指数函数计算基础几率
 * - 应用市场调节和品质倍数
 *
 * 性能注意: 使用简单的数学运算,性能开销很小
 */
bool AuctionBotBuyer::RollBidChance(BuyerItemInfo const* ahInfo, Item const* item, AuctionEntry const* auction, uint32 bidPrice)
{
    // 计算竞拍单价
    float itemBidPrice = float(bidPrice / item->GetCount());

    // 获取基准价格
    float itemPrice = float(item->GetTemplate()->SellPrice ? item->GetTemplate()->SellPrice : GetVendorPrice(item->GetTemplate()->Quality));
    // 基准价格乘以1.4,包含拍卖行抽成
    itemPrice *= 1.4f;

    // 计算竞拍几率
    float chance = std::min(100.f, std::pow(100.f, 1.f + (1.f - itemBidPrice / itemPrice) / sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_CHANCE_FACTOR)));

    // 如果有市场统计信息,根据市场均价调整几率
    if (ahInfo)
    {
        float avgBidPrice = ahInfo->TotalBidPrice / float(ahInfo->BidItemCount);

        TC_LOG_DEBUG("ahbot", "AHBot: Bid average: {:.1f} biddable item count: {}", avgBidPrice, ahInfo->BidItemCount);

        // 如果有超过5个同类物品,考虑市场均价
        if (ahInfo->BidItemCount >= 5)
            chance *= 1.f / std::sqrt(itemBidPrice / avgBidPrice);
    }

    // 如果玩家(非机器人)已竞拍,几率降低为1/5
    if (auction->bidder && !sAuctionBotConfig->IsBotChar(auction->bidder))
        chance = chance / 5.f;

    // 应用品质几率倍数
    chance *= GetChanceMultiplier(item->GetTemplate()->Quality) / 100.0f;

    // 随机判定
    float rand = frand(0.f, 100.f);
    bool win = rand <= chance;
    TC_LOG_DEBUG("ahbot", "AHBot: {} BID! chance = {:.2f}, price = {}, bidprice = {}.", win ? "WIN" : "LOSE", chance, uint32(itemPrice), uint32(itemBidPrice));
    return win;
}

/**
 * @brief 准备待处理拍卖项列表
 * @param config 买家配置
 *
 * 清理过期的拍卖项,只保留有效的待处理项。
 * 过期判定标准: LastExist时间早于当前时间-5秒
 *
 * 为什么要减5秒?
 * - GetItemInformation()刚刚更新的项,LastExist接近当前时间
 * - 减5秒可以保留刚刚更新的项,避免误删
 * - 同时清理掉上次更新已不存在的项
 *
 * 调用时机: 在GetItemInformation()之后,BuyAndBidItems()之前
 */
void AuctionBotBuyer::PrepareListOfEntry(BuyerConfiguration& config)
{
    // 当前时间减5秒,保留刚刚更新的项
    time_t now = GameTime::GetGameTime() - 5;

    // 遍历并移除过期项
    for (CheckEntryMap::iterator itr = config.EligibleItems.begin(); itr != config.EligibleItems.end();)
    {
        if (itr->second.LastExist < now)
            config.EligibleItems.erase(itr++);
        else
            ++itr;
    }

    TC_LOG_DEBUG("ahbot", "AHBot: EligibleItems size = {}", (uint32)config.EligibleItems.size());
}

/**
 * @brief 执行购买和竞拍操作
 * @param config 买家配置
 *
 * 遍历符合条件的拍卖项,根据几率决定是否购买或竞拍。
 *
 * 处理流程:
 * 1. 确定本次处理的物品数量(正常或加速模式)
 * 2. 遍历符合条件的拍卖项
 * 3. 检查拍卖项是否还存在、是否在检查间隔内
 * 4. 计算竞拍价格(如果有竞拍者,需要加价)
 * 5. 掷骰决定是否购买或竞拍
 * 6. 根据结果执行购买或竞拍操作
 *
 * 购买/竞拍决策逻辑:
 * - 如果竞拍成功且竞拍价>=一口价,直接购买
 * - 如果购买成功但竞拍失败,直接购买
 * - 如果两者都成功,有20%几率购买,80%几率竞拍
 * - 如果只有竞拍成功,执行竞拍
 *
 * 性能优化:
 * - 限制每次处理的物品数量
 * - 使用检查间隔避免频繁检查
 * - 物品数量过多时自动切换到加速模式
 */
void AuctionBotBuyer::BuyAndBidItems(BuyerConfiguration& config)
{
    time_t now = GameTime::GetGameTime();
    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(config.GetHouseType());
    CheckEntryMap& items = config.EligibleItems;

    // 确定本次处理的物品数量
    uint32 cycles = sAuctionBotConfig->GetItemPerCycleNormal();
    if (items.size() > sAuctionBotConfig->GetItemPerCycleBoost())
    {
        // 如果待处理物品过多,切换到加速模式
        cycles = sAuctionBotConfig->GetItemPerCycleBoost();
        TC_LOG_DEBUG("ahbot", "AHBot: Boost value used for Buyer! (if this happens often adjust both ItemsPerCycle in worldserver.conf)");
    }

    // 处理符合条件的拍卖项
    CheckEntryMap::iterator itr = items.begin();
    while (cycles && itr != items.end())
    {
        // 获取拍卖项
        AuctionEntry* auction = auctionHouse->GetAuction(itr->second.AuctionId);
        if (!auction)
        {
            TC_LOG_DEBUG("ahbot", "AHBot: Entry {} doesn't exists, perhaps bought already?", itr->second.AuctionId);
            items.erase(itr++);
            continue;
        }

        // 检查是否在检查间隔内
        if (itr->second.LastChecked && (now - itr->second.LastChecked) <= _checkInterval)
        {
            TC_LOG_DEBUG("ahbot", "AHBot: In time interval wait for entry {}!", auction->Id);
            ++itr;
            continue;
        }

        // 获取拍卖物品
        Item* item = sAuctionMgr->GetAItem(auction->itemGUIDLow);
        if (!item)
        {
            // 物品不可访问,可能正在支付中
            items.erase(itr++);
            continue;
        }

        // 计算竞拍价格
        uint32 bidPrice;
        if (auction->bid >= auction->startbid)
        {
            // 已有竞拍者,需要加价(最低加价幅度)
            bidPrice = auction->bid + auction->GetAuctionOutBid();
        }
        else
        {
            // 无竞拍者,使用起拍价
            bidPrice = auction->startbid;
        }

        // 获取物品的市场统计信息
        BuyerItemInfo const* ahInfo = nullptr;
        BuyerItemInfoMap::const_iterator sameItemItr = config.SameItemInfo.find(item->GetEntry());
        if (sameItemItr != config.SameItemInfo.end())
            ahInfo = &sameItemItr->second;

        TC_LOG_DEBUG("ahbot", "AHBot: Rolling for AHentry {}:", auction->Id);

        // 掷骰决定是否购买或竞拍
        bool successBuy = RollBuyChance(ahInfo, item, auction, bidPrice);
        bool successBid = RollBidChance(ahInfo, item, auction, bidPrice);

        // 根据掷骰结果执行操作
        if ((auction->buyout && successBid && bidPrice >= auction->buyout) ||
            (successBuy && (!successBid || urand(1, 5) == 1)))
        {
            // 直接购买(一口价)
            BuyEntry(auction, auctionHouse);
        }
        else if (successBid)
        {
            // 竞拍
            PlaceBidToEntry(auction, bidPrice);
        }

        // 更新检查时间
        itr->second.LastChecked = now;
        --cycles;
        ++itr;
    }

    // 清理不再需要的统计信息
    config.SameItemInfo.clear();
}

/**
 * @brief 获取指定品质物品的基础价格
 * @param quality 物品品质
 * @return 基础价格(铜币)
 *
 * 从配置获取各品质物品的基础价格,用于价格评估算法。
 * 如果物品没有出售价格(SellPrice),则使用此基础价格作为参考。
 *
 * 品质与默认价格(从配置中获取):
 * - 灰色(垃圾): 配置的Gray基础价格
 * - 白色(普通): 配置的White基础价格
 * - 绿色(优秀): 配置的Green基础价格
 * - 蓝色(精良): 配置的Blue基础价格
 * - 紫色(史诗): 配置的Purple基础价格
 * - 橙色(传说): 配置的Orange基础价格
 * - 黄色(神器): 配置的Yellow基础价格
 * - 其他: 1银币(100铜币)
 */
uint32 AuctionBotBuyer::GetVendorPrice(uint32 quality)
{
    switch (quality)
    {
        case ITEM_QUALITY_POOR:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_GRAY);
        case ITEM_QUALITY_NORMAL:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_WHITE);
        case ITEM_QUALITY_UNCOMMON:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_GREEN);
        case ITEM_QUALITY_RARE:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_BLUE);
        case ITEM_QUALITY_EPIC:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_PURPLE);
        case ITEM_QUALITY_LEGENDARY:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_ORANGE);
        case ITEM_QUALITY_ARTIFACT:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_BASEPRICE_YELLOW);
        default:
            return 1 * SILVER;  // 默认1银币
    }
}

/**
 * @brief 获取指定品质物品的几率倍数
 * @param quality 物品品质
 * @return 几率倍数(百分比,100表示100%倍数)
 *
 * 从配置获取各品质物品的几率倍数,用于调整购买/竞拍几率。
 * 倍数越大,该品质物品被购买/竞拍的概率越高。
 *
 * 用途:
 * - 可以通过调整倍数,让买家更倾向于购买某些品质的物品
 * - 例如:降低灰色物品的倍数,减少垃圾物品的购买
 *
 * 品质与默认倍数(从配置中获取):
 * - 灰色到黄色: 各自配置的倍数(通常默认为100)
 * - 其他: 100(不改变几率)
 */
uint32 AuctionBotBuyer::GetChanceMultiplier(uint32 quality)
{
    switch (quality)
    {
        case ITEM_QUALITY_POOR:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_GRAY);
        case ITEM_QUALITY_NORMAL:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_WHITE);
        case ITEM_QUALITY_UNCOMMON:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_GREEN);
        case ITEM_QUALITY_RARE:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_BLUE);
        case ITEM_QUALITY_EPIC:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_PURPLE);
        case ITEM_QUALITY_LEGENDARY:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_ORANGE);
        case ITEM_QUALITY_ARTIFACT:
            return sAuctionBotConfig->GetConfig(CONFIG_AHBOT_BUYER_CHANCEMULTIPLIER_YELLOW);
        default:
            return 100;  // 默认不改变几率
    }
}

/**
 * @brief 直接购买拍卖项
 * @param auction 拍卖项
 * @param auctionHouse 拍卖行对象
 *
 * 执行一口价购买操作,完成以下任务:
 * 1. 发送邮件通知之前的竞拍者(如果有)
 * 2. 设置机器人角色为竞拍者,出价为一口价
 * 3. 发送邮件通知卖家和买家
 * 4. 从数据库删除拍卖项
 * 5. 从内存移除物品和拍卖项
 *
 * 邮件通知:
 * - 如果有其他玩家竞拍,发送"被超过竞拍"邮件
 * - 发送"拍卖成功待处理"邮件给卖家
 * - 发送"拍卖成功"邮件给卖家
 * - 发送"拍卖获胜"邮件给买家(机器人)
 *
 * 事务处理:
 * - 所有数据库操作在一个事务中完成
 * - 保证数据一致性,防止部分失败导致数据丢失
 *
 * 调用时机: BuyAndBidItems()决定购买时调用
 */
void AuctionBotBuyer::BuyEntry(AuctionEntry* auction, AuctionHouseObject* auctionHouse)
{
    TC_LOG_DEBUG("ahbot", "AHBot: Entry {} bought at {:.2f}g", auction->Id, float(auction->buyout) / float(GOLD));

    // 开始数据库事务
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 发送邮件给之前的竞拍者(如果有,且不是机器人)
    if (auction->bidder && !sAuctionBotConfig->IsBotChar(auction->bidder))
        sAuctionMgr->SendAuctionOutbiddedMail(auction, auction->buyout, nullptr, trans);

    // 设置机器人角色为竞拍者,出价为一口价
    auction->bidder = sAuctionBotConfig->GetRandCharExclude(auction->owner);
    auction->bid = auction->buyout;

    // 发送相关邮件(必须在事务控制下)
    sAuctionMgr->SendAuctionSalePendingMail(auction, trans);
    sAuctionMgr->SendAuctionSuccessfulMail(auction, trans);
    sAuctionMgr->SendAuctionWonMail(auction, trans);

    // 从数据库删除拍卖项
    auction->DeleteFromDB(trans);

    // 从内存移除物品和拍卖项
    sAuctionMgr->RemoveAItem(auction->itemGUIDLow);
    auctionHouse->RemoveAuction(auction);

    // 提交事务,执行所有SQL语句
    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 对拍卖项出价
 * @param auction 拍卖项
 * @param bidPrice 出价金额
 *
 * 执行竞拍操作,完成以下任务:
 * 1. 发送邮件通知之前的竞拍者(如果有)
 * 2. 设置机器人角色为新竞拍者
 * 3. 更新拍卖项到数据库
 *
 * 邮件通知:
 * - 如果有其他玩家竞拍,发送"被超过竞拍"邮件
 *
 * 数据库更新:
 * - 更新竞拍者、出价和标志位
 * - 所有操作在一个事务中完成
 *
 * 注意事项:
 * - 竞拍不会立即移除拍卖项,只是更新竞拍者
 * - 如果没有人超过这个价格,拍卖项会在到期时由拍卖行系统处理
 *
 * 调用时机: BuyAndBidItems()决定竞拍时调用
 */
void AuctionBotBuyer::PlaceBidToEntry(AuctionEntry* auction, uint32 bidPrice)
{
    TC_LOG_DEBUG("ahbot", "AHBot: Bid placed to entry {}, {:.2f}g", auction->Id, float(bidPrice) / float(GOLD));

    // 开始数据库事务
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 发送邮件给之前的竞拍者(如果有,且不是机器人)
    if (auction->bidder && !sAuctionBotConfig->IsBotChar(auction->bidder))
        sAuctionMgr->SendAuctionOutbiddedMail(auction, bidPrice, nullptr, trans);

    // 设置机器人角色为新竞拍者
    auction->bidder = sAuctionBotConfig->GetRandCharExclude(auction->owner);
    auction->bid = bidPrice;
    // 清除GM日志买家标志(机器人不需要记录GM日志)
    auction->Flags = AuctionEntryFlag(auction->Flags & ~AUCTION_ENTRY_FLAG_GM_LOG_BUYER);

    // 更新拍卖项到数据库
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_AUCTION_BID);
    stmt->setUInt32(0, auction->bidder);
    stmt->setUInt32(1, auction->bid);
    stmt->setUInt8(2, auction->Flags);
    stmt->setUInt32(3, auction->Id);
    trans->Append(stmt);

    // 提交事务,执行所有SQL语句
    CharacterDatabase.CommitTransaction(trans);
}
