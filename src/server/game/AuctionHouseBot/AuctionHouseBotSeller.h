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
 * @file AuctionHouseBotSeller.h
 * @brief 拍卖行机器人卖家模块头文件
 *
 * 本文件定义了拍卖行机器人卖家系统的核心类和数据结构。
 * 该系统负责自动在拍卖行中上架物品，模拟真实玩家经济行为，
 * 维持拍卖行中的物品供应和经济活力。
 *
 * 主要功能包括：
 * - 根据配置自动上架物品
 * - 按物品品质和类型管理物品池
 * - 动态调整价格和上架时间
 * - 支持联盟、部落和中立拍卖行
 */

#ifndef AUCTION_HOUSE_BOT_SELLER_H
#define AUCTION_HOUSE_BOT_SELLER_H

#include "Define.h"
#include "ItemTemplate.h"
#include "AuctionHouseBot.h"

/**
 * @struct ItemToSell
 * @brief 待出售物品信息结构
 *
 * 用于存储需要上架出售的物品的基本分类信息，
 * 包括物品颜色和物品类别。
 */
struct ItemToSell
{
    uint32 Color;       ///< 物品颜色标识（品质相关）
    uint32 Itemclass;   ///< 物品类别（武器、护甲、消耗品等）
};

/// 待出售物品数组类型
typedef std::vector<ItemToSell> ItemsToSellArray;

/// 所有物品二维数组类型，按类别和品质组织
typedef std::vector<std::vector<uint32>> AllItemsArray;

/**
 * @struct SellerItemInfo
 * @brief 卖家物品信息结构
 *
 * 存储特定品质和类别的物品的数量信息，
 * 用于跟踪当前上架数量和缺失数量。
 */
struct SellerItemInfo
{
    uint32 AmountOfItems = 0;   ///< 目标上架物品数量
    uint32 MissItems = 0;       ///< 缺失的物品数量（目标数量 - 实际数量）
};

/**
 * @struct SellerItemClassSharedInfo
 * @brief 卖家物品类别共享信息结构
 *
 * 存储特定物品类别的共享配置信息，
 * 这些配置会影响该类别所有品质的物品。
 */
struct SellerItemClassSharedInfo
{
    uint32 PriceRatio = 0;          ///< 价格比率（百分比），影响最终售价
    uint32 RandomStackRatio = 100;  ///< 随机堆叠比率（百分比），决定是否随机堆叠数量
};

/**
 * @struct SellerItemQualitySharedInfo
 * @brief 卖家物品品质共享信息结构
 *
 * 存储特定物品品质的共享配置信息，
 * 这些配置会影响该品质所有类别的物品。
 */
struct SellerItemQualitySharedInfo
{
    uint32 AmountOfItems = 0;   ///< 该品质的物品总数量
    uint32 PriceRatio = 0;      ///< 该品质的价格比率（百分比）
};

/**
 * @class SellerConfiguration
 * @brief 卖家配置类
 *
 * 管理单个拍卖行的卖家配置信息，包括：
 * - 拍卖行类型（联盟/部落/中立）
 * - 物品上架时间范围
 * - 各品质和类别的物品数量配置
 * - 价格比率和堆叠设置
 *
 * 每个拍卖行类型都有独立的 SellerConfiguration 实例，
 * 允许对不同拍卖行进行差异化配置。
 */
class SellerConfiguration
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化卖家配置的默认值：
     * - 拍卖行类型：中立
     * - 最小上架时间：1小时
     * - 最大上架时间：72小时
     */
    SellerConfiguration() : LastMissedItem(0), _houseType(AUCTION_HOUSE_NEUTRAL), _minTime(1), _maxTime(72), _itemInfo(), _itemSharedQualityInfo(), _itemSharedClassInfo() { }

    /**
     * @brief 初始化卖家配置
     *
     * 设置拍卖行类型，准备该拍卖行的配置数据。
     *
     * @param houseType 拍卖行类型
     */
    void Initialize(AuctionHouseType houseType)
    {
        _houseType = houseType;
    }

    /**
     * @brief 获取拍卖行类型
     * @return 当前配置的拍卖行类型
     */
    AuctionHouseType GetHouseType() const { return _houseType; }

    uint32 LastMissedItem;  ///< 最后一次检查缺失物品的ID（用于调试和日志）

    /**
     * @brief 设置最小上架时间
     * @param value 最小上架时间（小时）
     */
    void SetMinTime(uint32 value)
    {
        _minTime = value;
    }

    /**
     * @brief 获取最小上架时间
     * @return 最小上架时间（小时），至少为1小时，且不超过最大上架时间
     */
    uint32 GetMinTime() const
    {
        return std::min(1u, std::min(_minTime, _maxTime));
    }

    /**
     * @brief 设置最大上架时间
     * @param value 最大上架时间（小时）
     */
    void SetMaxTime(uint32 value) { _maxTime = value; }

    /**
     * @brief 获取最大上架时间
     * @return 最大上架时间（小时）
     */
    uint32 GetMaxTime() const { return _maxTime; }

    /**
     * @name 物品类别与品质相关配置
     * @{
     */

    /**
     * @brief 设置指定品质和类别的物品目标数量
     *
     * @param quality 物品品质（灰色、白色、绿色、蓝色、紫色等）
     * @param itemClass 物品类别（武器、护甲、消耗品等）
     * @param amount 目标上架数量
     */
    void SetItemsAmountPerClass(AuctionQuality quality, ItemClass itemClass, uint32 amount) { _itemInfo[quality][itemClass].AmountOfItems = amount; }

    /**
     * @brief 获取指定品质和类别的物品目标数量
     * @param quality 物品品质
     * @param itemClass 物品类别
     * @return 目标上架数量
     */
    uint32 GetItemsAmountPerClass(AuctionQuality quality, ItemClass itemClass) const { return _itemInfo[quality][itemClass].AmountOfItems; }

    /**
     * @brief 设置指定品质和类别的缺失物品数量
     *
     * 根据目标数量和实际找到的数量计算缺失数量。
     * 如果实际数量已达标或超标，缺失数量为0。
     *
     * @param quality 物品品质
     * @param itemClass 物品类别
     * @param found 实际找到的物品数量
     */
    void SetMissedItemsPerClass(AuctionQuality quality, ItemClass itemClass, uint32 found)
    {
        if (_itemInfo[quality][itemClass].AmountOfItems > found)
            _itemInfo[quality][itemClass].MissItems = _itemInfo[quality][itemClass].AmountOfItems - found;
        else
            _itemInfo[quality][itemClass].MissItems = 0;
    }

    /**
     * @brief 获取指定品质和类别的缺失物品数量
     * @param quality 物品品质
     * @param itemClass 物品类别
     * @return 缺失的物品数量
     */
    uint32 GetMissedItemsPerClass(AuctionQuality quality, ItemClass itemClass) const { return _itemInfo[quality][itemClass].MissItems; }

    /** @} */

    /**
     * @name 物品品质相关配置
     * @{
     */

    /**
     * @brief 设置指定品质的物品总数量
     * @param quality 物品品质
     * @param cnt 物品总数量
     */
    void SetItemsAmountPerQuality(AuctionQuality quality, uint32 cnt) { _itemSharedQualityInfo[quality].AmountOfItems = cnt; }

    /**
     * @brief 获取指定品质的物品总数量
     * @param quality 物品品质
     * @return 物品总数量
     */
    uint32 GetItemsAmountPerQuality(AuctionQuality quality) const { return _itemSharedQualityInfo[quality].AmountOfItems; }

    /**
     * @brief 设置指定品质的价格比率
     *
     * 价格比率用于调整该品质物品的最终售价。
     * 例如：100表示原价，50表示半价，200表示双倍价格。
     *
     * @param quality 物品品质
     * @param value 价格比率（百分比）
     */
    void SetPriceRatioPerQuality(AuctionQuality quality, uint32 value) { _itemSharedQualityInfo[quality].PriceRatio = value; }

    /**
     * @brief 获取指定品质的价格比率
     * @param quality 物品品质
     * @return 价格比率（百分比）
     */
    uint32 GetPriceRatioPerQuality(AuctionQuality quality) const { return _itemSharedQualityInfo[quality].PriceRatio; }

    /** @} */

    /**
     * @name 物品类别相关配置
     * @{
     */

    /**
     * @brief 设置指定类别的价格比率
     *
     * 价格比率用于调整该类别物品的最终售价。
     *
     * @param itemClass 物品类别
     * @param value 价格比率（百分比）
     */
    void SetPriceRatioPerClass(ItemClass itemClass, uint32 value) { _itemSharedClassInfo[itemClass].PriceRatio = value; }

    /**
     * @brief 获取指定类别的价格比率
     * @param itemClass 物品类别
     * @return 价格比率（百分比）
     */
    uint32 GetPriceRatioPerClass(ItemClass itemClass) const { return _itemSharedClassInfo[itemClass].PriceRatio; }

    /**
     * @brief 设置指定类别的随机堆叠比率
     *
     * 控制该类别物品是否使用随机堆叠数量。
     * 例如：100表示总是随机，0表示总是使用最大堆叠。
     *
     * @param itemClass 物品类别
     * @param value 随机堆叠比率（百分比）
     */
    void SetRandomStackRatioPerClass(ItemClass itemClass, uint32 value) { _itemSharedClassInfo[itemClass].RandomStackRatio = value; }

    /**
     * @brief 获取指定类别的随机堆叠比率
     * @param itemClass 物品类别
     * @return 随机堆叠比率（百分比）
     */
    uint32 GetRandomStackRatioPerClass(ItemClass itemClass) const { return _itemSharedClassInfo[itemClass].RandomStackRatio; }

    /** @} */

private:
    AuctionHouseType _houseType;    ///< 拍卖行类型
    uint32 _minTime;                ///< 最小上架时间（小时）
    uint32 _maxTime;                ///< 最大上架时间（小时）

    /**
     * @brief 物品信息数组
     *
     * 二维数组，按品质和类别索引，存储每个组合的物品信息。
     * 第一维：物品品质（MAX_AUCTION_QUALITY）
     * 第二维：物品类别（MAX_ITEM_CLASS）
     */
    SellerItemInfo _itemInfo[MAX_AUCTION_QUALITY][MAX_ITEM_CLASS];

    /**
     * @brief 物品品质共享信息数组
     *
     * 存储每个品质的共享配置（数量、价格比率等）。
     * 数组大小为 MAX_ITEM_QUALITY。
     */
    SellerItemQualitySharedInfo _itemSharedQualityInfo[MAX_ITEM_QUALITY];

    /**
     * @brief 物品类别共享信息数组
     *
     * 存储每个类别的共享配置（价格比率、堆叠比率等）。
     * 数组大小为 MAX_ITEM_CLASS。
     */
    SellerItemClassSharedInfo _itemSharedClassInfo[MAX_ITEM_CLASS];
};

/**
 * @class AuctionBotSeller
 * @brief 拍卖行机器人卖家类
 *
 * 负责管理拍卖行的自动上架功能，继承自 AuctionBotAgent。
 * 该类是拍卖行机器人系统的核心卖家组件，实现了：
 *
 * - 管理三种拍卖行（联盟、部落、中立）的卖家配置
 * - 维护物品池，按品质和类别组织
 * - 自动补充拍卖行中的物品
 * - 根据配置动态调整价格和数量
 * - 统计和报告拍卖行状态
 *
 * 工作流程：
 * 1. 从配置文件加载卖家设置
 * 2. 初始化物品池（所有可上架的物品）
 * 3. 定期检查拍卖行状态
 * 4. 根据缺失数量补充物品
 * 5. 设置合理的价格和上架时间
 */
class TC_GAME_API AuctionBotSeller : public AuctionBotAgent
{
public:
    /// 物品池类型，存储物品ID列表
    typedef std::vector<uint32> ItemPool;

    /**
     * @brief 构造函数
     *
     * 初始化卖家对象，准备配置和物品池。
     */
    AuctionBotSeller();

    /**
     * @brief 析构函数
     */
    ~AuctionBotSeller();

    /**
     * @brief 初始化卖家代理
     *
     * 实现自 AuctionBotAgent 接口。
     * 加载配置、初始化物品池、设置各拍卖行的卖家配置。
     *
     * @return 初始化成功返回 true，失败返回 false
     */
    bool Initialize() override;

    /**
     * @brief 更新指定拍卖行
     *
     * 实现自 AuctionBotAgent 接口。
     * 检查拍卖行状态，补充缺失的物品。
     *
     * @param houseType 拍卖行类型
     * @return 更新成功返回 true，失败返回 false
     */
    bool Update(AuctionHouseType houseType) override;

    /**
     * @brief 为指定配置添加新拍卖
     *
     * 根据缺失物品数量，从物品池中选择物品并上架。
     * 设置物品的买断价、竞拍价、堆叠数量和上架时间。
     *
     * @param config 卖家配置对象
     */
    void AddNewAuctions(SellerConfiguration& config);

    /**
     * @brief 设置物品比率
     *
     * 设置三种拍卖行的物品价格比率。
     *
     * @param al 联盟拍卖行比率
     * @param ho 部落拍卖行比率
     * @param ne 中立拍卖行比率
     */
    void SetItemsRatio(uint32 al, uint32 ho, uint32 ne);

    /**
     * @brief 设置指定拍卖行的物品比率
     *
     * @param house 拍卖行类型
     * @param val 价格比率
     */
    void SetItemsRatioForHouse(AuctionHouseType house, uint32 val);

    /**
     * @brief 设置各品质的物品数量
     *
     * @param amounts 包含各品质物品数量的数组
     */
    void SetItemsAmount(std::array<uint32, MAX_AUCTION_QUALITY> const& amounts);

    /**
     * @brief 设置指定品质的物品数量
     *
     * @param quality 物品品质
     * @param val 物品数量
     */
    void SetItemsAmountForQuality(AuctionQuality quality, uint32 val);

    /**
     * @brief 从配置文件加载卖家配置
     *
     * 读取 worldserver.conf 中的拍卖行卖家配置，
     * 包括物品数量、价格比率、上架时间等。
     */
    void LoadConfig();

private:
    /**
     * @brief 各拍卖行的卖家配置数组
     *
     * 索引对应 AuctionHouseType 枚举值：
     * - 0: 联盟拍卖行
     * - 1: 部落拍卖行
     * - 2: 中立拍卖行
     */
    SellerConfiguration _houseConfig[MAX_AUCTION_HOUSE_TYPE];

    /**
     * @brief 物品池数组
     *
     * 二维数组，按品质和类别组织所有可上架的物品ID。
     * 第一维：物品品质（MAX_AUCTION_QUALITY）
     * 第二维：物品类别（MAX_ITEM_CLASS）
     */
    ItemPool _itemPool[MAX_AUCTION_QUALITY][MAX_ITEM_CLASS];

    /**
     * @brief 加载卖家配置值
     *
     * 从配置文件读取并应用卖家配置，
     * 包括物品数量、价格比率等。
     *
     * @param config 要加载配置的卖家配置对象
     */
    void LoadSellerValues(SellerConfiguration& config);

    /**
     * @brief 设置统计信息
     *
     * 统计拍卖行中各品质和类别的物品数量，
     * 计算缺失数量并更新配置。
     *
     * @param config 卖家配置对象
     * @return 缺失物品的总数量
     */
    uint32 SetStat(SellerConfiguration& config);

    /**
     * @brief 获取待出售物品列表
     *
     * 根据配置和当前拍卖行状态，生成需要上架的物品列表。
     * 随机选择物品池中的物品填补缺失。
     *
     * @param config 卖家配置对象
     * @param itemsToSellArray 输出：待出售物品列表
     * @param addedItem 当前已添加物品的数组
     * @return 是否成功生成待出售列表
     */
    bool GetItemsToSell(SellerConfiguration& config, ItemsToSellArray& itemsToSellArray, AllItemsArray const& addedItem);

    /**
     * @brief 设置物品价格
     *
     * 根据物品模板和配置，计算物品的买断价和竞拍价。
     * 考虑因素包括：
     * - 物品基础价格（从物品模板获取）
     * - 品质价格比率
     * - 类别价格比率
     * - 堆叠数量
     *
     * @param itemProto 物品模板指针
     * @param config 卖家配置对象
     * @param buyp 输出：买断价格
     * @param bidp 输出：竞拍价格
     * @param stackcnt 堆叠数量
     */
    void SetPricesOfItem(ItemTemplate const* itemProto, SellerConfiguration& config, uint32& buyp, uint32& bidp, uint32 stackcnt);

    /**
     * @brief 获取物品的堆叠数量
     *
     * 根据物品模板和配置，决定物品的堆叠数量。
     * 考虑物品的最大堆叠数和随机堆叠比率配置。
     *
     * @param itemProto 物品模板指针
     * @param config 卖家配置对象
     * @return 堆叠数量
     */
    uint32 GetStackSizeForItem(ItemTemplate const* itemProto, SellerConfiguration& config) const;

    /**
     * @brief 加载物品数量配置
     *
     * 从数据库或配置文件加载各品质和类别的物品数量设置。
     *
     * @param config 卖家配置对象
     */
    void LoadItemsQuantity(SellerConfiguration& config);

    /**
     * @brief 获取购买价格修正值
     *
     * 根据物品类型和属性，计算购买价格的修正系数。
     * 用于调整物品的基础价格到合理的市场价。
     *
     * @param prototype 物品模板指针
     * @return 价格修正值
     */
    static uint32 GetBuyModifier(ItemTemplate const* prototype);

    /**
     * @brief 获取出售价格修正值
     *
     * 根据物品类型和属性，计算出售价格的修正系数。
     *
     * @param itemProto 物品模板指针
     * @return 价格修正值
     */
    static uint32 GetSellModifier(ItemTemplate const* itemProto);
};

#endif
