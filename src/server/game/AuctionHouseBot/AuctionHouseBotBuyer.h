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
 * @file AuctionHouseBotBuyer.h
 * @brief 拍卖行机器人买家代理头文件
 *
 * 本文件定义了拍卖行机器人买家代理(AuctionBotBuyer)及其相关数据结构。
 * 买家代理负责模拟玩家在拍卖行中的购买和竞拍行为。
 *
 * 主要功能:
 * - 自动购买和竞拍玩家上架的物品
 * - 根据物品价格和市场情况决定是否购买/竞拍
 * - 模拟真实的经济环境,活跃拍卖行
 *
 * 工作流程:
 * 1. 收集拍卖行物品信息(价格、数量等)
 * 2. 根据算法计算购买/竞拍几率
 * 3. 随机决定是否购买或竞拍
 * 4. 执行购买/竞拍操作并发送邮件通知
 *
 * 算法说明:
 * - 价格越低,购买几率越高
 * - 根据市场平均价格调整几率
 * - 已有玩家竞拍的物品,几率降低为1/5
 * - 不同品质物品有不同的基础几率倍数
 */

#ifndef AUCTION_HOUSE_BOT_BUYER_H
#define AUCTION_HOUSE_BOT_BUYER_H

#include "Define.h"
#include "AuctionHouseMgr.h"
#include "AuctionHouseBot.h"

/**
 * @struct BuyerAuctionEval
 * @brief 买家拍卖评估信息
 *
 * 用于跟踪拍卖项的检查时间和存在时间。
 * 帮助买家代理避免频繁检查同一拍卖项。
 */
struct BuyerAuctionEval
{
    BuyerAuctionEval() : AuctionId(0), LastChecked(0), LastExist(0) { }

    uint32 AuctionId;       // 拍卖ID
    time_t LastChecked;     // 上次检查时间戳
    time_t LastExist;       // 上次确认存在的时间戳
};

/**
 * @struct BuyerItemInfo
 * @brief 买家物品统计信息
 *
 * 用于统计同一种物品(相同Entry)在拍卖行中的价格分布。
 * 帮助买家代理判断当前价格是否合理。
 */
struct BuyerItemInfo
{
    BuyerItemInfo() : BidItemCount(0), BuyItemCount(0), MinBuyPrice(0), MinBidPrice(0), TotalBuyPrice(0), TotalBidPrice(0) { }

    uint32 BidItemCount;    // 有竞拍价的物品数量
    uint32 BuyItemCount;    // 有一口价的物品数量
    uint32 MinBuyPrice;     // 最低一口价(单价)
    uint32 MinBidPrice;     // 最低竞拍价(单价)
    double TotalBuyPrice;   // 一口价总价(单价累计,用于计算平均价)
    double TotalBidPrice;   // 竞拍价总价(单价累计,用于计算平均价)
};

// 物品ID到物品信息的映射
typedef std::map<uint32, BuyerItemInfo> BuyerItemInfoMap;

// 拍卖ID到评估信息的映射
typedef std::map<uint32, BuyerAuctionEval> CheckEntryMap;

/**
 * @struct BuyerConfiguration
 * @brief 单个拍卖行的买家配置
 *
 * 存储特定拍卖行的买家配置和运行时数据。
 * 每个拍卖行(联盟/部落/中立)都有独立的配置实例。
 */
struct BuyerConfiguration
{
    BuyerConfiguration() : BuyerEnabled(false), _houseType(AUCTION_HOUSE_NEUTRAL) { }

    /**
     * @brief 初始化配置
     * @param houseType 拍卖行类型
     */
    void Initialize(AuctionHouseType houseType)
    {
        _houseType = houseType;
    }

    /**
     * @brief 获取拍卖行类型
     * @return 拍卖行类型
     */
    AuctionHouseType GetHouseType() const { return _houseType; }

    BuyerItemInfoMap SameItemInfo;      // 同类物品的价格统计信息
    CheckEntryMap EligibleItems;        // 符合条件的拍卖项(可购买/竞拍的)
    bool BuyerEnabled;                  // 此拍卖行的买家是否启用

private:
    AuctionHouseType _houseType;        // 拍卖行类型
};

/**
 * @class AuctionBotBuyer
 * @brief 拍卖行机器人买家代理
 *
 * 继承自AuctionBotAgent,负责在拍卖行中自动购买和竞拍物品。
 * 模拟真实玩家的购买行为,活跃拍卖行经济环境。
 *
 * 主要职责:
 * - 定期扫描拍卖行,收集物品价格信息
 * - 根据价格和市场情况计算购买/竞拍几率
 * - 执行购买和竞拍操作
 * - 发送邮件通知买卖双方
 *
 * 工作流程:
 * 1. Update()被定期调用
 * 2. GetItemInformation()收集拍卖行物品信息
 * 3. PrepareListOfEntry()清理过期项,准备待处理列表
 * 4. BuyAndBidItems()执行购买和竞拍操作
 *
 * 算法细节:
 * - 价格越接近出售价格(或基础价格),购买几率越高
 * - 市场平均价格影响购买决策
 * - 不同品质物品有不同的基础几率倍数
 * - 已有玩家竞拍的物品,几率降低为1/5
 *
 * 调用时机:
 * - Initialize()在服务器启动时调用
 * - Update()在世界更新循环中定期调用
 *
 * 性能注意事项:
 * - 使用检查间隔避免频繁扫描同一拍卖项
 * - 每次更新最多处理配置的物品数量,避免性能峰值
 */
class TC_GAME_API AuctionBotBuyer : public AuctionBotAgent
{
public:
    AuctionBotBuyer();
    ~AuctionBotBuyer();

    /**
     * @brief 初始化买家代理
     * @return 如果至少有一个拍卖行的买家启用返回true
     *
     * 加载配置并初始化各个拍卖行的买家配置。
     */
    bool Initialize() override;

    /**
     * @brief 更新指定拍卖行的买家状态
     * @param houseType 要处理的拍卖行类型
     * @return 如果执行了操作返回true
     *
     * 此方法由主管理器定期调用,执行拍卖行的扫描和购买操作。
     */
    bool Update(AuctionHouseType houseType) override;

    /**
     * @brief 加载买家配置
     *
     * 从配置管理器加载各个拍卖行的买家启用状态。
     */
    void LoadConfig();

    /**
     * @brief 执行购买和竞拍操作
     * @param config 拍卖行买家配置
     *
     * 遍历符合条件的拍卖项,根据几率决定是否购买或竞拍。
     */
    void BuyAndBidItems(BuyerConfiguration& config);

private:
    uint32 _checkInterval;                                  // 检查间隔(秒)
    BuyerConfiguration _houseConfig[MAX_AUCTION_HOUSE_TYPE]; // 各拍卖行的买家配置

    /**
     * @brief 加载买家特定值
     * @param config 买家配置
     *
     * 当前为空实现,预留给未来扩展。
     */
    void LoadBuyerValues(BuyerConfiguration& config);

    /**
     * @brief 掷骰决定是否购买(一口价)
     * @param ahInfo 物品统计信息(可为NULL)
     * @param item 物品对象
     * @param auction 拍卖项
     * @param bidPrice 竞拍价格(未使用)
     * @return 如果决定购买返回true
     *
     * 根据物品价格和市场情况计算购买几率。
     */
    bool RollBuyChance(BuyerItemInfo const* ahInfo, Item const* item, AuctionEntry const* auction, uint32 bidPrice);

    /**
     * @brief 掷骰决定是否竞拍
     * @param ahInfo 物品统计信息(可为NULL)
     * @param item 物品对象
     * @param auction 拍卖项
     * @param bidPrice 竞拍价格
     * @return 如果决定竞拍返回true
     *
     * 根据竞拍价格和市场情况计算竞拍几率。
     */
    bool RollBidChance(BuyerItemInfo const* ahInfo, Item const* item, AuctionEntry const* auction, uint32 bidPrice);

    /**
     * @brief 对拍卖项出价
     * @param auction 拍卖项
     * @param bidPrice 出价金额
     *
     * 执行竞拍操作,更新数据库并发送邮件通知。
     */
    void PlaceBidToEntry(AuctionEntry* auction, uint32 bidPrice);

    /**
     * @brief 直接购买拍卖项
     * @param auction 拍卖项
     * @param auctionHouse 拍卖行对象
     *
     * 执行一口价购买操作,移除拍卖项并发送邮件通知。
     */
    void BuyEntry(AuctionEntry* auction, AuctionHouseObject* auctionHouse);

    /**
     * @brief 准备待处理拍卖项列表
     * @param config 买家配置
     *
     * 移除过期的拍卖项,只保留有效的待处理项。
     */
    void PrepareListOfEntry(BuyerConfiguration& config);

    /**
     * @brief 收集拍卖行物品信息
     * @param config 买家配置
     * @return 符合条件的拍卖项数量
     *
     * 扫描拍卖行,统计物品价格信息,更新符合条件的拍卖项列表。
     */
    uint32 GetItemInformation(BuyerConfiguration& config);

    /**
     * @brief 获取指定品质物品的基础价格
     * @param quality 物品品质
     * @return 基础价格(铜币)
     *
     * 从配置获取各品质物品的基础价格,用于价格计算。
     */
    uint32 GetVendorPrice(uint32 quality);

    /**
     * @brief 获取指定品质物品的几率倍数
     * @param quality 物品品质
     * @return 几率倍数(百分比)
     *
     * 从配置获取各品质物品的几率倍数,影响购买/竞拍几率。
     */
    uint32 GetChanceMultiplier(uint32 quality);
};

#endif
