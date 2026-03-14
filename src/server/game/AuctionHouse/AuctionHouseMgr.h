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
 * @file AuctionHouseMgr.h
 * @brief 拍卖行管理器模块
 *
 * 本模块负责管理游戏中的拍卖行系统，包括：
 * - 拍卖行的创建、更新、删除
 * - 拍卖物品的管理
 * - 竞拍和一口价交易
 * - 拍卖邮件通知
 * - 部落、联盟和中立拍卖行的独立管理
 *
 * 拍卖行系统支持跨阵营交易配置，并提供了完整的拍卖生命周期管理。
 */

#ifndef _AUCTION_HOUSE_MGR_H
#define _AUCTION_HOUSE_MGR_H

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include "ObjectGuid.h"
#include <map>
#include <set>
#include <unordered_map>

// 前向声明
class Item;
class Player;
class WorldPacket;
struct AuctionHouseEntry;

// 拍卖行相关常量定义
#define MIN_AUCTION_TIME (12*HOUR)      // 最小拍卖时间：12小时
#define MAX_AUCTION_ITEMS 160           // 单个拍卖行最大拍卖物品数量
#define MAX_GETALL_RETURN 55000         // GetAll查询返回的最大数据量

/**
 * @enum AuctionError
 * @brief 拍卖行操作错误码定义
 *
 * 定义了拍卖行操作过程中可能返回的各种错误状态
 */
enum AuctionError : uint8
{
    ERR_AUCTION_OK                  = 0,   // 操作成功
    ERR_AUCTION_INVENTORY           = 1,   // 背包空间不足
    ERR_AUCTION_DATABASE_ERROR      = 2,   // 数据库错误
    ERR_AUCTION_NOT_ENOUGHT_MONEY   = 3,   // 金钱不足
    ERR_AUCTION_ITEM_NOT_FOUND      = 4,   // 物品未找到
    ERR_AUCTION_HIGHER_BID          = 5,   // 已有更高出价
    ERR_AUCTION_BID_INCREMENT       = 7,   // 出价增量不符合要求
    ERR_AUCTION_BID_OWN             = 10,  // 不能竞拍自己的物品
    ERR_AUCTION_RESTRICTED_ACCOUNT  = 13   // 账号受限（试用账号等）
};

/**
 * @enum AuctionAction
 * @brief 拍卖行操作类型定义
 *
 * 定义了玩家可以执行的拍卖行操作类型
 */
enum AuctionAction : uint8
{
    AUCTION_SELL_ITEM   = 0,  // 出售物品（创建拍卖）
    AUCTION_CANCEL      = 1,  // 取消拍卖
    AUCTION_PLACE_BID   = 2   // 竞拍出价
};

/**
 * @enum MailAuctionAnswers
 * @brief 拍卖邮件响应类型定义
 *
 * 定义了拍卖结果相关的邮件类型，用于邮件主题构建
 */
enum MailAuctionAnswers
{
    AUCTION_OUTBIDDED           = 0,  // 被其他人出价超过
    AUCTION_WON                 = 1,  // 竞拍成功赢得物品
    AUCTION_SUCCESSFUL          = 2,  // 拍卖成功售出
    AUCTION_EXPIRED             = 3,  // 拍卖到期未售出
    AUCTION_CANCELLED_TO_BIDDER = 4,  // 拍卖被取消（通知竞拍者）
    AUCTION_CANCELED            = 5,  // 拍卖被取消（通知卖家）
    AUCTION_SALE_PENDING        = 6   // 销售待处理（延迟到账通知）
};

/**
 * @enum AuctionHouses
 * @brief 拍卖行ID定义
 *
 * 定义了游戏中三个主要拍卖行的ID
 * 对应 AuctionHouse.dbc 中的条目
 */
enum AuctionHouses
{
    AUCTIONHOUSE_ALLIANCE       = 2,  // 联盟拍卖行（暴风城、铁炉堡等）
    AUCTIONHOUSE_HORDE          = 6,  // 部落拍卖行（奥格瑞玛、幽暗城等）
    AUCTIONHOUSE_NEUTRAL        = 7   // 中立拍卖行（藏宝海湾、加基森等）
};

/**
 * @enum AuctionEntryFlag
 * @brief 拍卖条目标志位定义
 *
 * 用于标记拍卖条目的特殊属性和优化标记
 */
enum AuctionEntryFlag : uint8
{
    AUCTION_ENTRY_FLAG_NONE         = 0x0,  // 无特殊标志
    AUCTION_ENTRY_FLAG_GM_LOG_BUYER = 0x1   // 标记竞拍者为GM账号，需要记录到GM日志
                                            // 优化标志：避免查询离线玩家权限
};

/**
 * @struct AuctionEntry
 * @brief 拍卖条目数据结构
 *
 * 存储单个拍卖的所有相关信息，包括拍卖物品、价格、时间、竞拍者等
 * 这是拍卖行系统的核心数据结构
 */
struct TC_GAME_API AuctionEntry
{
    uint32 Id;                                          // 拍卖ID（数据库主键）
    uint8 houseId;                                      // 拍卖行ID（联盟/部落/中立）
    ObjectGuid::LowType itemGUIDLow;                    // 拍卖物品的低端GUID
    uint32 itemEntry;                                   // 物品模板ID
    uint32 itemCount;                                   // 物品数量（堆叠数量）
    ObjectGuid::LowType owner;                          // 卖家角色GUID低端部分
    uint32 startbid;                                    // 起拍价（可能已废弃）
    uint32 bid;                                         // 当前最高出价
    uint32 buyout;                                      // 一口价
    time_t expire_time;                                 // 过期时间（Unix时间戳）
    ObjectGuid::LowType bidder;                         // 当前最高出价者GUID低端部分
    uint32 deposit;                                     // 保证金（仅在创建拍卖时计算）
    uint32 etime;                                       // 拍卖持续时间
    std::unordered_set<ObjectGuid> bidders;            // 所有参与竞拍的玩家集合
    AuctionHouseEntry const* auctionHouseEntry;        // 拍卖行配置条目（来自AuctionHouse.dbc）
    AuctionEntryFlag Flags;                            // 拍卖标志位

    /**
     * @brief 获取拍卖行ID
     * @return 拍卖行ID
     */
    uint8 GetHouseId() const { return houseId; }

    /**
     * @brief 计算拍卖行手续费
     * @return 手续费金额（金币）
     * @note 手续费基于成交价的一定比例，由拍卖行配置决定
     */
    uint32 GetAuctionCut() const;

    /**
     * @brief 计算最小出价增量
     * @return 最小出价增量金额
     * @note 用于确保新的出价必须比当前价格高出一定金额
     *       计算公式：当前出价的5%，最少1铜币
     */
    uint32 GetAuctionOutBid() const;

    /**
     * @brief 构建拍卖信息数据包
     * @param data 输出的网络数据包
     * @param sourceItem 可选的物品指针，如果为空则从管理器获取
     * @return 构建成功返回true，物品不存在返回false
     * @note 将拍卖的完整信息打包到网络数据包中，用于发送给客户端
     */
    bool BuildAuctionInfo(WorldPacket & data, Item* sourceItem = nullptr) const;

    /**
     * @brief 从数据库删除拍卖记录
     * @param trans 数据库事务
     * @note 同时删除拍卖表和竞拍者表中的记录
     */
    void DeleteFromDB(CharacterDatabaseTransaction trans) const;

    /**
     * @brief 将拍卖保存到数据库
     * @param trans 数据库事务
     * @note 执行INSERT操作，新增拍卖记录
     */
    void SaveToDB(CharacterDatabaseTransaction trans) const;

    /**
     * @brief 从数据库字段加载拍卖数据
     * @param fields 数据库查询结果字段数组
     * @return 加载成功返回true，数据无效返回false
     * @note 会验证拍卖行ID和物品是否存在
     */
    bool LoadFromDB(Field* fields);

    /**
     * @brief 构建拍卖邮件主题
     * @param response 邮件响应类型
     * @return 格式化的邮件主题字符串
     * @note 格式：物品ID:随机属性ID:响应类型:拍卖ID:物品数量
     */
    std::string BuildAuctionMailSubject(MailAuctionAnswers response) const;

    /**
     * @brief 构建竞拍成功邮件正文
     * @param guid 卖家GUID
     * @param bid 成交价格
     * @param buyout 一口价（可能为0）
     * @return 格式化的邮件正文
     */
    static std::string BuildAuctionWonMailBody(ObjectGuid guid, uint32 bid, uint32 buyout);

    /**
     * @brief 构建拍卖成功邮件正文
     * @param guid 买家GUID
     * @param bid 成交价格
     * @param buyout 一口价
     * @param deposit 保证金
     * @param consignment 手续费
     * @return 格式化的邮件正文
     */
    static std::string BuildAuctionSoldMailBody(ObjectGuid guid, uint32 bid, uint32 buyout, uint32 deposit, uint32 consignment);

    /**
     * @brief 构建拍卖发票邮件正文
     * @param guid 买家GUID
     * @param bid 成交价格
     * @param buyout 一口价
     * @param deposit 保证金
     * @param consignment 手续费
     * @param moneyDelay 金币到账延迟时间
     * @param eta 预计到账时间
     * @return 格式化的邮件正文
     */
    static std::string BuildAuctionInvoiceMailBody(ObjectGuid guid, uint32 bid, uint32 buyout, uint32 deposit, uint32 consignment, uint32 moneyDelay, uint32 eta);
};

/**
 * @class AuctionHouseObject
 * @brief 单个拍卖行实例管理类
 *
 * 管理单个拍卖行（联盟/部落/中立）的所有拍卖条目
 * 负责拍卖的生命周期管理、查询、更新和过期处理
 */
class TC_GAME_API AuctionHouseObject
{
public:
    /**
     * @brief 析构函数
     * @note 清理所有拍卖条目，释放内存
     */
    ~AuctionHouseObject()
    {
        for (AuctionEntryMap::iterator itr = AuctionsMap.begin(); itr != AuctionsMap.end(); ++itr)
            delete itr->second;
    }

    // 类型定义
    typedef std::map<uint32, AuctionEntry*> AuctionEntryMap;              // 拍卖ID到拍卖条目的映射
    typedef std::unordered_map<ObjectGuid, time_t> PlayerGetAllThrottleMap; // 玩家GetAll操作的限流映射

    /**
     * @brief 获取拍卖数量
     * @return 当前拍卖行中的拍卖总数
     */
    uint32 Getcount() const { return AuctionsMap.size(); }

    /**
     * @brief 获取拍卖映射表起始迭代器
     * @return 拍卖映射表起始迭代器
     */
    AuctionEntryMap::iterator GetAuctionsBegin() {return AuctionsMap.begin();}

    /**
     * @brief 获取拍卖映射表结束迭代器
     * @return 拍卖映射表结束迭代器
     */
    AuctionEntryMap::iterator GetAuctionsEnd() {return AuctionsMap.end();}

    /**
     * @brief 根据ID获取拍卖条目
     * @param id 拍卖ID
     * @return 拍卖条目指针，未找到返回nullptr
     */
    AuctionEntry* GetAuction(uint32 id) const
    {
        AuctionEntryMap::const_iterator itr = AuctionsMap.find(id);
        return itr != AuctionsMap.end() ? itr->second : nullptr;
    }

    /**
     * @brief 添加拍卖到拍卖行
     * @param auction 拍卖条目指针
     * @note 会触发脚本事件OnAuctionAdd
     */
    void AddAuction(AuctionEntry* auction);

    /**
     * @brief 从拍卖行移除拍卖
     * @param auction 拍卖条目指针
     * @return 如果拍卖在映射中返回true，否则false
     * @note 会触发脚本事件OnAuctionRemove，并删除拍卖对象
     */
    bool RemoveAuction(AuctionEntry* auction);

    /**
     * @brief 更新拍卖行状态
     *
     * 处理所有即将过期的拍卖：
     * - 无竞拍者的拍卖：退回物品给卖家
     * - 有竞拍者的拍卖：完成交易，发送邮件
     * - 清理过期的GetAll限流记录
     *
     * @调用时机 服务器主循环定期调用（每次世界更新）
     * @性能注意 可能处理大量拍卖，需要批量数据库操作
     */
    void Update();

    /**
     * @brief 构建玩家竞拍列表数据包
     * @param data 输出的网络数据包
     * @param player 玩家对象
     * @param count 返回实际发送的拍卖数量
     * @param totalcount 返回匹配的拍卖总数
     * @note 遍历所有拍卖，查找玩家参与竞拍的物品
     */
    void BuildListBidderItems(WorldPacket& data, Player* player, uint32& count, uint32& totalcount);

    /**
     * @brief 构建玩家出售列表数据包
     * @param data 输出的网络数据包
     * @param player 玩家对象
     * @param count 返回实际发送的拍卖数量
     * @param totalcount 返回匹配的拍卖总数
     * @note 遍历所有拍卖，查找玩家出售的物品
     */
    void BuildListOwnerItems(WorldPacket& data, Player* player, uint32& count, uint32& totalcount);

    /**
     * @brief 构建拍卖物品搜索结果数据包
     * @param data 输出的网络数据包
     * @param player 玩家对象
     * @param searchedname 搜索的物品名称（宽字符）
     * @param listfrom 起始位置（分页偏移）
     * @param levelmin 最小等级要求
     * @param levelmax 最大等级要求
     * @param usable 是否只显示可使用的物品
     * @param inventoryType 装备类型（背包位置）
     * @param itemClass 物品大类
     * @param itemSubClass 物品子类
     * @param quality 物品品质
     * @param count 返回实际发送的拍卖数量
     * @param totalcount 返回匹配的拍卖总数
     * @param getall 是否使用GetAll模式（获取所有拍卖）
     *
     * @note GetAll模式会返回所有拍卖，但有限流机制（防止频繁查询）
     *       普通搜索模式最多返回50条记录
     *
     * @性能注意 GetAll模式可能返回大量数据，需要限制返回数量（MAX_GETALL_RETURN）
     */
    void BuildListAuctionItems(WorldPacket& data, Player* player,
        std::wstring const& searchedname, uint32 listfrom, uint8 levelmin, uint8 levelmax, uint8 usable,
        uint32 inventoryType, uint32 itemClass, uint32 itemSubClass, uint32 quality,
        uint32& count, uint32& totalcount, bool getall = false);

private:
    AuctionEntryMap AuctionsMap;                     // 拍卖条目映射表（拍卖ID -> 拍卖对象）

    // GetAll操作的玩家限流映射：记录玩家上次GetAll时间和过期时间
    // 存储在此处而非玩家对象中，以保持玩家登出后的持久性
    PlayerGetAllThrottleMap GetAllThrottleMap;

};

/**
 * @class AuctionHouseMgr
 * @brief 拍卖行全局管理器（单例）
 *
 * 负责管理整个服务器的拍卖行系统，包括：
 * - 管理三个拍卖行实例（联盟、部落、中立）
 * - 管理所有拍卖物品
 * - 处理拍卖邮件通知
 * - 加载和保存拍卖数据
 * - 管理待确认的拍卖（保证金扣除前）
 */
class TC_GAME_API AuctionHouseMgr
{
    private:
        /**
         * @brief 私有构造函数（单例模式）
         */
        AuctionHouseMgr();

        /**
         * @brief 私有析构函数
         * @note 清理所有拍卖物品，释放内存
         */
        ~AuctionHouseMgr();

    public:
        /**
         * @brief 获取单例实例
         * @return 拍卖行管理器单例指针
         */
        static AuctionHouseMgr* instance();

        // 类型定义
        typedef std::unordered_map<ObjectGuid::LowType, Item*> ItemMap;   // 物品GUID -> 物品对象映射
        typedef std::vector<AuctionEntry*> PlayerAuctions;                 // 玩家拍卖列表
        typedef std::pair<PlayerAuctions*, uint32> AuctionPair;            // 拍卖列表和计数的配对

        /**
         * @brief 根据阵营模板ID获取对应的拍卖行
         * @param factionTemplateId 阵营模板ID
         * @return 对应的拍卖行对象指针
         *
         * @note 根据配置决定是否允许跨阵营交易：
         *       - 如果启用跨阵营交易，所有拍卖都使用中立拍卖行
         *       - 否则根据阵营分配到联盟/部落/中立拍卖行
         */
        AuctionHouseObject* GetAuctionsMap(uint32 factionTemplateId);

        /**
         * @brief 根据拍卖行ID获取拍卖行对象
         * @param auctionHouseId 拍卖行ID（2=联盟，6=部落，7=中立）
         * @return 对应的拍卖行对象指针
         *
         * @note 如果启用跨阵营交易，所有ID都返回中立拍卖行
         */
        AuctionHouseObject* GetAuctionsMapByHouseId(uint8 auctionHouseId);

        /**
         * @brief 获取拍卖物品
         * @param id 物品GUID低端部分
         * @return 物品对象指针，未找到返回nullptr
         */
        Item* GetAItem(ObjectGuid::LowType id)
        {
            ItemMap::const_iterator itr = mAitems.find(id);
            if (itr != mAitems.end())
                return itr->second;

            return nullptr;
        }

        // ========== 拍卖邮件相关方法 ==========

        /**
         * @brief 发送竞拍成功邮件给竞拍者
         * @param auction 拍卖条目
         * @param trans 数据库事务
         *
         * @note 将物品发送给竞拍成功的玩家
         *       不释放内存，由调用者负责清理
         */
        void SendAuctionWonMail(AuctionEntry* auction, CharacterDatabaseTransaction trans);

        /**
         * @brief 发送销售待处理邮件给卖家
         * @param auction 拍卖条目
         * @param trans 数据库事务
         *
         * @note 通知卖家物品已售出，金币将延迟到账
         */
        void SendAuctionSalePendingMail(AuctionEntry* auction, CharacterDatabaseTransaction trans);

        /**
         * @brief 发送拍卖成功邮件给卖家
         * @param auction 拍卖条目
         * @param trans 数据库事务
         *
         * @note 发送金币给卖家（扣除手续费），延迟发送
         *       不释放内存，由调用者负责清理
         */
        void SendAuctionSuccessfulMail(AuctionEntry* auction, CharacterDatabaseTransaction trans);

        /**
         * @brief 发送拍卖过期邮件给卖家
         * @param auction 拍卖条目
         * @param trans 数据库事务
         *
         * @note 将物品退回给卖家（无人竞拍）
         *       不释放内存，由调用者负责清理
         */
        void SendAuctionExpiredMail(AuctionEntry* auction, CharacterDatabaseTransaction trans);

        /**
         * @brief 发送被超价邮件给原竞拍者
         * @param auction 拍卖条目
         * @param newPrice 新的出价金额
         * @param newBidder 新的竞拍者
         * @param trans 数据库事务
         *
         * @note 将被超价的金币退还给原竞拍者
         */
        void SendAuctionOutbiddedMail(AuctionEntry* auction, uint32 newPrice, Player* newBidder, CharacterDatabaseTransaction trans);

        /**
         * @brief 发送拍卖取消邮件给竞拍者
         * @param auction 拍卖条目
         * @param trans 数据库事务
         *
         * @note 卖家取消拍卖时，退还金币给竞拍者
         */
        void SendAuctionCancelledToBidderMail(AuctionEntry* auction, CharacterDatabaseTransaction trans);

        /**
         * @brief 计算拍卖保证金
         * @param entry 拍卖行配置条目
         * @param time 拍卖持续时间（秒）
         * @param pItem 物品对象
         * @param count 物品数量
         * @return 保证金金额
         *
         * @note 计算公式：卖出价格 × 存款比例 × 时间系数 × 数量
         *       有最小保证金限制（AH_MINIMUM_DEPOSIT）
         */
        static uint32 GetAuctionDeposit(AuctionHouseEntry const* entry, uint32 time, Item* pItem, uint32 count);

        /**
         * @brief 根据阵营模板ID获取拍卖行配置
         * @param factionTemplateId 阵营模板ID
         * @return 拍卖行配置条目指针
         */
        static AuctionHouseEntry const* GetAuctionHouseEntry(uint32 factionTemplateId);

        /**
         * @brief 根据拍卖行ID获取拍卖行配置
         * @param houseId 拍卖行ID
         * @return 拍卖行配置条目指针
         */
        static AuctionHouseEntry const* GetAuctionHouseEntryFromHouse(uint8 houseId);

    public:
        // ========== 数据加载方法 ==========

        /**
         * @brief 加载拍卖物品
         *
         * 从数据库加载所有拍卖中的物品实例
         * 必须在LoadAuctions之前调用，因为加载拍卖时需要检查物品是否存在
         *
         * @调用时机 服务器启动时
         */
        void LoadAuctionItems();

        /**
         * @brief 加载拍卖数据
         *
         * 从数据库加载所有拍卖条目和竞拍者信息
         *
         * @调用时机 服务器启动时，在LoadAuctionItems之后
         */
        void LoadAuctions();

        /**
         * @brief 添加拍卖物品到管理器
         * @param it 物品对象指针
         * @note 物品必须存在且GUID唯一
         */
        void AddAItem(Item* it);

        /**
         * @brief 从管理器移除拍卖物品
         * @param id 物品GUID低端部分
         * @param deleteItem 是否从数据库删除物品
         * @param trans 数据库事务指针（deleteItem为true时必须提供）
         * @return 移除成功返回true，物品不存在返回false
         */
        bool RemoveAItem(ObjectGuid::LowType id, bool deleteItem = false, CharacterDatabaseTransaction* trans = nullptr);

        /**
         * @brief 添加待确认拍卖
         * @param player 玩家对象
         * @param aEntry 拍卖条目
         * @return 成功添加返回true，玩家金币不足返回false
         *
         * @note 拍卖在扣除保证金前先放入待确认队列
         *       系统会在下一个更新周期检查玩家是否有足够金币
         */
        bool PendingAuctionAdd(Player* player, AuctionEntry* aEntry);

        /**
         * @brief 获取玩家待确认拍卖数量
         * @param player 玩家对象
         * @return 待确认拍卖数量
         */
        uint32 PendingAuctionCount(Player const* player) const;

        /**
         * @brief 处理玩家待确认拍卖
         * @param player 玩家对象
         *
         * @note 扣除玩家金币，将能够支付的拍卖设置为正常拍卖
         *       金币不足的拍卖会被立即过期
         */
        void PendingAuctionProcess(Player* player);

        /**
         * @brief 更新所有待确认拍卖
         *
         * @note 检查所有待确认拍卖，处理金币扣除或过期
         *       如果玩家离线，拍卖会被过期处理
         *
         * @调用时机 服务器主循环定期调用
         */
        void UpdatePendingAuctions();

        /**
         * @brief 更新所有拍卖行
         *
         * @note 更新联盟、部落、中立三个拍卖行
         *
         * @调用时机 服务器主循环每次世界更新时
         */
        void Update();

    private:
        // 三个独立的拍卖行实例
        AuctionHouseObject mHordeAuctions;       // 部落拍卖行
        AuctionHouseObject mAllianceAuctions;    // 联盟拍卖行
        AuctionHouseObject mNeutralAuctions;     // 中立拍卖行

        // 待确认拍卖映射：玩家GUID -> (拍卖列表, 上次处理的数量)
        std::map<ObjectGuid, AuctionPair> pendingAuctionMap;

        // 所有拍卖物品的映射表：物品GUID -> 物品对象
        ItemMap mAitems;
};

#define sAuctionMgr AuctionHouseMgr::instance()

#endif
