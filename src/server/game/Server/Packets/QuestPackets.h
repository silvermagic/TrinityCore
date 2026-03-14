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
 * @file QuestPackets.h
 * @brief 任务系统网络数据包定义
 *
 * 本文件定义了任务系统相关的所有网络数据包结构,包括:
 * - 客户端查询任务信息的请求包
 * - 服务器响应任务查询的响应包
 * - 任务给予者显示任务详情的包
 * - 任务奖励信息包
 *
 * 这些数据包用于客户端与服务器之间的任务数据通信,
 * 实现任务的查询、显示、接受和完成等功能。
 */

#ifndef QuestPackets_h__
#define QuestPackets_h__

#include "ObjectGuid.h"
#include "Packet.h"
#include "QuestDef.h"

namespace WorldPackets
{
    namespace Quest
    {
        /**
         * @class QueryQuestInfo
         * @brief 客户端查询任务信息请求包
         *
         * 当客户端需要获取某个任务的详细信息时发送此包。
         * 继承自 ClientPacket,表示这是一个客户端主动发起的请求。
         *
         * @调用时机:
         * - 玩家打开任务日志查看任务详情时
         * - 接到新任务需要显示任务信息时
         * - 与NPC对话查看可接任务时
         */
        class QueryQuestInfo final : public ClientPacket
        {
            public:
                /**
                 * @brief 构造函数
                 * @param packet 世界包数据,使用右值引用转移所有权
                 */
                QueryQuestInfo(WorldPacket&& packet) : ClientPacket(CMSG_QUEST_QUERY, std::move(packet)) { }

                /**
                 * @brief 读取客户端发送的任务ID
                 *
                 * 从网络包中读取玩家查询的任务ID。
                 */
                void Read() override;

                uint32 QuestID = 0;  ///< 要查询的任务ID
        };

        /**
         * @struct QuestInfoChoiceItem
         * @brief 任务奖励选择物品信息
         *
         * 用于表示玩家可以从多个奖励物品中选择其一的物品项。
         * 通常用于任务完成后的奖励选择界面。
         */
        struct QuestInfoChoiceItem
        {
            uint32 ItemID = 0;   ///< 物品模板ID
            uint32 Quantity = 0; ///< 物品数量
        };

        /**
         * @struct QuestInfo
         * @brief 任务详细信息结构
         *
         * 包含任务的所有静态数据和动态属性,用于向客户端发送完整的任务信息。
         * 这个结构体包含了任务的目标、奖励、要求、描述等所有相关信息。
         */
        struct QuestInfo
        {
            uint32 QuestID                  = 0;    ///< 任务ID
            uint32 QuestMethod              = 0;    ///< 任务方法: 0=自动完成(跳过目标/详情), 1或2=正常任务
            int32  QuestLevel               = 0;    ///< 任务等级(可能为-1表示动态等级,应使用Player::GetQuestLevel获取)
            uint32 QuestMinLevel            = 0;    ///< 接受任务所需的最低等级
            int32  QuestSortID              = 0;    ///< 任务分类ID,用于任务日志中显示所属区域或类别
            uint32 QuestType                = 0;    ///< 任务类型
            uint32 SuggestedGroupNum        = 0;    ///< 建议组队人数
            int32  AllowableRaces           = -1;   ///< 允许接受的种族掩码(-1表示所有种族)

            uint32 RequiredFactionId[PVP_TEAMS_COUNT]  = { };      ///< 所需阵营ID(联盟/部落),在任务目标中显示
            int32  RequiredFactionValue[PVP_TEAMS_COUNT]  = { };   ///< 所需阵营声望值,在任务目标中显示

            uint32 RewardNextQuest          = 0;    ///< 后续任务ID,非0时客户端会自动请求此任务
            uint32 RewardXPDifficulty       = 0;    ///< 经验奖励难度系数,用于计算奖励经验
            int32  RewardMoney              = 0;    ///< 奖励金钱(低于最高等级时)
            uint32 RewardBonusMoney         = 0;    ///< 奖励额外金钱,用于客户端经验计算
            uint32 RewardDisplaySpell       = 0;    ///< 奖励法术显示ID,用于显示图标(RewSpellCast为0时施放此法术)
            int32  RewardSpell              = 0;    ///< 奖励法术ID
            uint32 RewardHonor              = 0;    ///< 奖励荣誉值
            float RewardKillHonor           = 0.0f; ///< 奖励击杀荣誉
            uint32 StartItem                = 0;    ///< 任务起始物品ID
            uint32 Flags                    = 0;    ///< 任务标志位
            uint32 RewardTitleId            = 0;    ///< 奖励头衔ID(CharTitles.dbc中的ID),2.4.0版本新增
            uint32 RequiredPlayerKills      = 0;    ///< 需要的玩家击杀数
            uint32 RewardTalents            = 0;    ///< 奖励天赋点数
            int32  RewardArenaPoints        = 0;    ///< 奖励竞技场点数
            uint32 RewardFactionFlags       = 0;    ///< 奖励阵营标志

            uint32 RewardItems[QUEST_REWARDS_COUNT] = { };             ///< 固定奖励物品ID数组
            uint32 RewardAmount[QUEST_REWARDS_COUNT] = { };            ///< 固定奖励物品数量数组
            QuestInfoChoiceItem UnfilteredChoiceItems[QUEST_REWARD_CHOICES_COUNT]; ///< 可选择奖励物品数组(未过滤)
            uint32 RewardFactionID[QUEST_REPUTATIONS_COUNT] = { };     ///< 奖励声望阵营ID数组
            int32  RewardFactionValue[QUEST_REPUTATIONS_COUNT] = { };  ///< 奖励声望值数组
            int32  RewardFactionValueOverride[QUEST_REPUTATIONS_COUNT] = { }; ///< 奖励声望值覆盖数组

            uint32 POIContinent             = 0;    ///< 任务POI(兴趣点)所在大陆ID
            float  POIx                     = 0.0f; ///< 任务POI的X坐标
            float  POIy                     = 0.0f; ///< 任务POI的Y坐标
            uint32 POIPriority              = 0;    ///< 任务POI优先级
            std::string Title;                       ///< 任务标题
            std::string Objectives;                   ///< 任务目标描述
            std::string Details;                      ///< 任务详情描述
            std::string AreaDescription;              ///< 区域描述
            std::string CompletedText;                ///< 任务完成提示文本,在所有目标完成后显示

            int32  RequiredNpcOrGo[QUEST_OBJECTIVES_COUNT] = { };   ///< 需要击杀的NPC或交互的GameObject ID(>0为生物,<0为游戏对象)
            uint32 RequiredNpcOrGoCount[QUEST_OBJECTIVES_COUNT] = { }; ///< 需要击杀/交互的数量

            uint32 ItemDrop[QUEST_SOURCE_ITEM_IDS_COUNT] = { };      ///< 任务物品掉落ID数组
            // uint32 ItemDropQuantity[QUEST_SOURCE_ITEM_IDS_COUNT] = { }; ///< 任务物品掉落数量(已注释,未使用)

            uint32 RequiredItemId[QUEST_ITEM_OBJECTIVES_COUNT] = { };    ///< 需要收集的物品ID数组
            uint32 RequiredItemCount[QUEST_ITEM_OBJECTIVES_COUNT] = { }; ///< 需要收集的物品数量数组

            std::string ObjectiveText[QUEST_OBJECTIVES_COUNT]; ///< 自定义目标文本数组
        };

        /**
         * @class QueryQuestInfoResponse
         * @brief 服务器响应任务查询的数据包
         *
         * 当服务器收到客户端的任务查询请求后,通过此包返回完整的任务信息。
         * 继承自 ServerPacket,表示这是一个服务器发送给客户端的响应包。
         *
         * @调用时机:
         * - 响应客户端的 CMSG_QUEST_QUERY 请求
         * - 返回 QuestInfo 结构中包含的所有任务数据
         *
         * @性能注意:
         * - 初始缓冲区大小设为2000字节,可容纳大多数任务数据
         * - 包含大量字符串和数组数据,序列化开销较大
         */
        class QueryQuestInfoResponse final : public ServerPacket
        {
            public:
                /**
                 * @brief 构造函数
                 *
                 * 初始化服务器包,设置操作码为 SMSG_QUEST_QUERY_RESPONSE,
                 * 预分配2000字节缓冲区以减少重新分配次数。
                 */
                QueryQuestInfoResponse() : ServerPacket(SMSG_QUEST_QUERY_RESPONSE, 2000) { }

                /**
                 * @brief 序列化任务信息到网络包
                 * @return 返回序列化后的世界包指针
                 *
                 * 将 QuestInfo 结构体中的所有数据按照协议格式写入网络包。
                 */
                WorldPacket const* Write() override;

                QuestInfo Info; ///< 要发送的任务详细信息
        };

        /**
         * @struct QuestChoiceItem
         * @brief 任务选择物品完整信息
         *
         * 包含物品ID、数量和显示ID的完整物品信息结构。
         * 用于任务奖励界面显示物品外观和属性。
         */
        struct QuestChoiceItem
        {
            /**
             * @brief 构造函数
             * @param itemID 物品模板ID
             * @param quantity 物品数量
             * @param displayID 物品显示ID(用于客户端显示物品外观)
             */
            QuestChoiceItem(uint32 itemID, uint32 quantity, uint32 displayID) : ItemID(itemID), Quantity(quantity),
                                                                                DisplayID(displayID) { }
            uint32 ItemID = 0;     ///< 物品模板ID
            uint32 Quantity = 0;   ///< 物品数量
            uint32 DisplayID = 0;  ///< 物品显示ID,用于客户端显示物品图标和外观
        };

        /**
         * @struct QuestRewards
         * @brief 任务奖励信息结构
         *
         * 包含任务完成后的所有奖励信息,包括物品、金钱、经验、荣誉、声望等。
         * 使用动态数组存储物品奖励,支持不同数量的奖励项。
         */
        struct QuestRewards
        {
            std::vector<QuestChoiceItem> UnfilteredChoiceItems;         ///< 可选择奖励物品列表(未过滤)
            std::vector<QuestChoiceItem> RewardItems;                   ///< 固定奖励物品列表
            uint32 RewardMoney = 0;                                     ///< 奖励金钱
            uint32 RewardXPDifficulty = 0;                              ///< 奖励经验难度系数
            uint32 RewardHonor = 0;                                     ///< 奖励荣誉值
            float RewardKillHonor = 0.f;                                ///< 奖励击杀荣誉
            uint32 RewardDisplaySpell = 0;                              ///< 奖励法术显示ID
            int32 RewardSpell = 0;                                      ///< 奖励法术ID
            uint32 RewardTitleId = 0;                                   ///< 奖励头衔ID
            uint32 RewardTalents = 0;                                   ///< 奖励天赋点数
            uint32 RewardArenaPoints = 0;                               ///< 奖励竞技场点数
            uint32 RewardFactionFlags = 0;                              ///< 奖励阵营标志
            std::array<uint32, QUEST_REPUTATIONS_COUNT> RewardFactionID = { };           ///< 奖励声望阵营ID数组
            std::array<int32, QUEST_REPUTATIONS_COUNT> RewardFactionValue = { };         ///< 奖励声望值数组
            std::array<int32, QUEST_REPUTATIONS_COUNT> RewardFactionValueOverride = { }; ///< 奖励声望值覆盖数组
        };

        /**
         * @struct QuestDescEmote
         * @brief 任务描述表情信息
         *
         * 定义任务对话过程中NPC播放的表情动作,用于增强任务叙事效果。
         */
        struct QuestDescEmote
        {
            /**
             * @brief 构造函数
             * @param type 表情类型ID
             * @param delay 表情延迟时间(毫秒)
             */
            QuestDescEmote(int32 type, uint32 delay) : Type(type), Delay(delay) { }
            uint32 Type;   ///< 表情类型ID
            uint32 Delay;  ///< 表情延迟时间(毫秒)
        };

        /**
         * @class QuestGiverQuestDetails
         * @brief 任务给予者显示任务详情包
         *
         * 当玩家与NPC对话查看任务详情时,服务器发送此包显示任务的完整信息。
         * 继承自 ServerPacket,包含任务的标题、描述、目标、奖励等信息。
         *
         * @调用时机:
         * - 玩家与任务NPC对话,选择查看某个任务时
         * - 玩家接受任务后显示任务详情时
         * - GM使用作弊命令查看任务时
         *
         * @性能注意:
         * - 初始缓冲区大小设为1000字节
         * - 包含多个字符串和动态数组,序列化开销中等
         */
        class QuestGiverQuestDetails final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器包,设置操作码为 SMSG_QUEST_GIVER_QUEST_DETAILS,
             * 预分配1000字节缓冲区。
             */
            QuestGiverQuestDetails() : ServerPacket(SMSG_QUEST_GIVER_QUEST_DETAILS, 1000) { }

            /**
             * @brief 序列化任务详情到网络包
             * @return 返回序列化后的世界包指针
             *
             * 将任务给予者的GUID、任务信息、奖励数据、表情等写入网络包。
             */
            WorldPacket const* Write() override;

            ObjectGuid QuestGiverGUID;          ///< 任务给予者的GUID(NPC或物品)
            ObjectGuid InformUnit;              ///< 通知单位GUID(通常用于任务共享)
            uint32 QuestID = 0;                 ///< 任务ID
            std::string Title;                  ///< 任务标题
            std::string Details;                ///< 任务详情描述
            std::string Objectives;             ///< 任务目标描述
            bool AutoLaunched = false;          ///< 是否自动启动(自动接受任务)
            uint32 Flags = 0;                   ///< 任务标志位
            uint32 SuggestedGroupNum = 0;       ///< 建议组队人数
            bool StartCheat = false;            ///< 是否使用作弊方式启动(GM命令)
            QuestRewards Rewards;               ///< 任务奖励信息
            std::vector<QuestDescEmote> DescEmotes; ///< 任务描述表情列表
        };

        /**
         * @class QuestGiverOfferRewardMessage
         * @brief 任务给予者提供奖励消息包
         *
         * 当玩家完成任务并与任务NPC对话时,服务器发送此包显示任务完成界面和奖励信息。
         * 继承自 ServerPacket,包含任务的完成文本、奖励物品、声望等信息。
         *
         * @调用时机:
         * - 玩家完成任务目标后与任务NPC对话时
         * - 玩家选择领取任务奖励时
         * - 显示任务完成确认界面时
         *
         * @性能注意:
         * - 初始缓冲区大小设为600字节
         * - 包含奖励文本、表情列表和奖励数据
         */
        class QuestGiverOfferRewardMessage final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器包,设置操作码为 SMSG_QUEST_GIVER_OFFER_REWARD_MESSAGE,
             * 预分配600字节缓冲区。
             */
            QuestGiverOfferRewardMessage() : ServerPacket(SMSG_QUEST_GIVER_OFFER_REWARD_MESSAGE, 600) { }

            /**
             * @brief 序列化任务奖励消息到网络包
             * @return 返回序列化后的世界包指针
             *
             * 将任务给予者GUID、任务ID、奖励文本、表情、奖励物品等信息写入网络包。
             */
            WorldPacket const* Write() override;

            ObjectGuid QuestGiverGUID;          ///< 任务给予者的GUID
            uint32 QuestID = 0;                 ///< 任务ID
            std::string Title;                  ///< 任务标题
            std::string RewardText;             ///< 奖励文本(任务完成后的感谢词)
            bool AutoLaunched = false;          ///< 是否自动启动
            uint32 Flags = 0;                   ///< 任务标志位
            uint32 SuggestedGroupNum = 0;       ///< 建议组队人数
            std::vector<QuestDescEmote> Emotes; ///< 表情列表(NPC在给予奖励时播放)
            QuestRewards Rewards;               ///< 任务奖励信息
        };
    }
}

#endif // QuestPackets_h__
