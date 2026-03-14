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
 * @file QueryPackets.h
 * @brief 查询数据包定义模块
 *
 * 本模块定义了游戏中各种实体查询相关的网络数据包结构，包括：
 * - 生物(Creature)查询请求和响应
 * - 游戏对象(GameObject)查询请求和响应
 * - 物品(Item)查询请求和响应
 * - 任务POI(兴趣点)查询请求
 *
 * 这些数据包用于客户端向服务器查询实体的详细信息，
 * 是游戏客户端获取游戏世界数据的重要通信机制。
 *
 * 主要功能：
 * 1. 客户端发起查询请求，获取生物、物品、游戏对象的详细信息
 * 2. 服务器响应查询，返回实体的统计数据和属性信息
 * 3. 支持任务POI查询，用于地图上显示任务相关标记
 */

#ifndef QueryPackets_h__
#define QueryPackets_h__

#include "Packet.h"
#include "ObjectGuid.h"

#include "SharedDefines.h"
#include "Creature.h"
#include "GameObject.h"
#include "ItemTemplate.h"
#include "QuestDef.h"

namespace WorldPackets
{
    namespace Query
    {
        /**
         * @class QueryCreature
         * @brief 生物查询请求数据包
         *
         * 继承自 ClientPacket，用于客户端向服务器请求生物的详细信息。
         * 当玩家看到一个新的生物时，客户端会发送此请求获取生物的名称、类型、属性等信息。
         *
         * 调用时机：
         * - 玩家首次看到某个生物类型时
         * - 鼠标悬停在生物上显示提示信息时
         * - 查看生物详细信息时
         */
        class QueryCreature final : public ClientPacket
        {
            public:
                /**
                 * @brief 构造函数
                 * @param packet 世界包数据，从网络接收的原始数据包
                 */
                QueryCreature(WorldPacket&& packet) : ClientPacket(CMSG_CREATURE_QUERY, std::move(packet)) { }

                /**
                 * @brief 读取数据包内容
                 *
                 * 从网络数据包中读取生物ID和GUID信息
                 */
                void Read() override;

                uint32 CreatureID = 0;    ///< 生物模板ID(CreatureTemplate entry)，用于查找生物的静态数据
                ObjectGuid Guid;           ///< 生物实例的GUID，用于标识世界中的具体生物实例
        };

        /**
         * @struct CreatureStats
         * @brief 生物统计数据结构
         *
         * 包含生物的所有静态属性信息，这些数据通常来自数据库的creature_template表。
         * 这些信息会发送给客户端用于显示生物提示、确定交互类型等。
         */
        struct CreatureStats
        {
            std::string Name;              ///< 生物名称，显示在头顶和提示框中
            std::string NameAlt;           ///< 备用名称，用于本地化或其他显示
            std::string CursorName;        ///< 光标名称，用于确定鼠标悬停时显示的图标类型
            uint32 Flags = 0;              ///< 生物标志位，控制生物的特殊行为(如不可攻击、可驯服等)
            uint32 CreatureType = 0;       ///< 生物类型(人形生物、野兽、恶魔等)，对应CreatureType.dbc
            uint32 CreatureFamily = 0;     ///< 生物科属(狼、熊、猫科等)，对应CreatureFamily.dbc，用于宠物系统
            uint32 Classification = 0;     ///< 生物等级分类(普通、精英、稀有精英、首领、稀有首领)
            uint32 ProxyCreatureID[MAX_KILL_CREDIT] = { };      ///< 代理生物ID数组，用于击杀任务计数(3.1版本新增)
            uint32 CreatureDisplayID[MAX_CREATURE_MODELS] = { }; ///< 生物显示ID数组，定义生物的模型外观
            float HpMulti = 0.0f;          ///< 生命值倍数，用于调整生物的生命值
            float EnergyMulti = 0.0f;      ///< 能量值倍数，用于调整生物的法力值/能量值
            bool Leader = false;           ///< 是否为领袖NPC，领袖NPC会有特殊标记
            uint32 QuestItems[MAX_CREATURE_QUEST_ITEMS] = { };  ///< 任务物品ID数组，该生物掉落的任务物品列表
            uint32 CreatureMovementInfoID = 0;                  ///< 生物移动信息ID，对应CreatureMovementInfo.dbc
        };

        /**
         * @class QueryCreatureResponse
         * @brief 生物查询响应数据包
         *
         * 继承自 ServerPacket，用于服务器向客户端发送生物的详细信息。
         * 响应客户端的QueryCreature请求，包含生物的所有静态属性数据。
         *
         * 调用时机：
         * - 收到QueryCreature请求后立即响应
         * - 初始包大小设为100字节，根据实际数据动态调整
         */
        class QueryCreatureResponse final : public ServerPacket
        {
            public:
                /**
                 * @brief 构造函数
                 *
                 * 初始化服务器响应包，设置操作码为SMSG_CREATURE_QUERY_RESPONSE
                 * 预留100字节的缓冲区空间
                 */
                QueryCreatureResponse() : ServerPacket(SMSG_CREATURE_QUERY_RESPONSE, 100) { }

                /**
                 * @brief 写入数据包内容
                 * @return 返回构建完成的世界包指针
                 *
                 * 将生物统计数据序列化为网络数据包格式发送给客户端
                 * 如果Allow为false，只发送生物ID和标志位，不包含详细数据
                 */
                WorldPacket const* Write() override;

                bool Allow = false;        ///< 是否允许查询，false表示查无此生物或查询失败
                CreatureStats Stats;       ///< 生物统计数据，包含生物的所有详细信息
                uint32 CreatureID = 0;     ///< 生物模板ID，用于客户端缓存和索引
        };

        /**
         * @class QueryGameObject
         * @brief 游戏对象查询请求数据包
         *
         * 继承自 ClientPacket，用于客户端向服务器请求游戏对象的详细信息。
         * 游戏对象包括：箱子、任务物品、传送门、矿物、草药等各种可交互对象。
         *
         * 调用时机：
         * - 玩家首次看到某个游戏对象类型时
         * - 鼠标悬停在游戏对象上显示提示信息时
         * - 准备与游戏对象交互时
         */
        class QueryGameObject final : public ClientPacket
        {
            public:
                /**
                 * @brief 构造函数
                 * @param packet 世界包数据，从网络接收的原始数据包
                 */
                QueryGameObject(WorldPacket&& packet) : ClientPacket(CMSG_GAMEOBJECT_QUERY, std::move(packet)) { }

                /**
                 * @brief 读取数据包内容
                 *
                 * 从网络数据包中读取游戏对象ID和GUID信息
                 */
                void Read() override;

                uint32 GameObjectID = 0;   ///< 游戏对象模板ID(GameObjectTemplate entry)
                ObjectGuid Guid;           ///< 游戏对象实例的GUID
        };

        /**
         * @struct GameObjectStats
         * @brief 游戏对象统计数据结构
         *
         * 包含游戏对象的所有静态属性信息，这些数据来自数据库的gameobject_template表。
         * 不同类型的游戏对象使用Data数组存储不同的参数。
         */
        struct GameObjectStats
        {
            std::string Name;              ///< 游戏对象名称，显示在提示框中
            std::string IconName;          ///< 图标名称，用于替代默认光标图标(如"Attack"显示为剑)
            std::string CastBarCaption;    ///< 施法条标题，使用游戏对象时显示在施法条上的文本
            std::string UnkString;         ///< 未知字符串，2.0.3版本新增，用途不明
            uint32 Type = 0;               ///< 游戏对象类型(门、按钮、箱子、任务 giver等)
            uint32 DisplayID = 0;          ///< 显示ID，定义游戏对象的模型外观
            uint32 Data[MAX_GAMEOBJECT_DATA] = { };      ///< 游戏对象数据数组，根据Type不同存储不同参数
            float Size = 0.0f;             ///< 游戏对象大小倍数
            uint32 QuestItems[MAX_GAMEOBJECT_QUEST_ITEMS] = { };  ///< 任务物品ID数组
        };

        /**
         * @class QueryGameObjectResponse
         * @brief 游戏对象查询响应数据包
         *
         * 继承自 ServerPacket，用于服务器向客户端发送游戏对象的详细信息。
         * 响应客户端的QueryGameObject请求。
         *
         * 调用时机：
         * - 收到QueryGameObject请求后立即响应
         * - 初始包大小设为150字节，根据实际数据动态调整
         */
        class QueryGameObjectResponse final : public ServerPacket
        {
            public:
                /**
                 * @brief 构造函数
                 *
                 * 初始化服务器响应包，设置操作码为SMSG_GAMEOBJECT_QUERY_RESPONSE
                 * 预留150字节的缓冲区空间
                 */
                QueryGameObjectResponse() : ServerPacket(SMSG_GAMEOBJECT_QUERY_RESPONSE, 150) { }

                /**
                 * @brief 写入数据包内容
                 * @return 返回构建完成的世界包指针
                 *
                 * 将游戏对象统计数据序列化为网络数据包格式发送给客户端
                 */
                WorldPacket const* Write() override;

                uint32 GameObjectID = 0;   ///< 游戏对象模板ID
                bool Allow = false;        ///< 是否允许查询，false表示查无此游戏对象
                GameObjectStats Stats;     ///< 游戏对象统计数据
        };

        /**
         * @class QueryItemSingle
         * @brief 单个物品查询请求数据包
         *
         * 继承自 ClientPacket，用于客户端向服务器请求单个物品的详细信息。
         * 当玩家查看物品属性、悬停物品提示时发送此请求。
         *
         * 调用时机：
         * - 玩家悬停查看物品提示时
         * - 查看物品详细信息时
         * - 获得新物品时
         */
        class QueryItemSingle final : public ClientPacket
        {
            public:
                /**
                 * @brief 构造函数
                 * @param packet 世界包数据，从网络接收的原始数据包
                 */
                QueryItemSingle(WorldPacket&& packet) : ClientPacket(CMSG_ITEM_QUERY_SINGLE, std::move(packet)) { }

                /**
                 * @brief 读取数据包内容
                 *
                 * 从网络数据包中读取物品ID
                 */
                void Read() override;

                uint32 ItemID = 0;         ///< 物品模板ID(ItemTemplate entry)
        };

        /**
         * @struct ItemDamageData
         * @brief 物品伤害数据结构
         *
         * 定义武器的伤害范围和伤害类型。
         * 武器可能有多种伤害类型(物理、火焰、冰霜等)。
         */
        struct ItemDamageData
        {
            float DamageMin = 0.0f;        ///< 最小伤害值
            float DamageMax = 0.0f;        ///< 最大伤害值
            uint32 DamageType = 0;         ///< 伤害类型(物理、神圣、火焰、自然、冰霜、暗影、奥术)
        };

        /**
         * @struct ItemStatData
         * @brief 物品属性数据结构
         *
         * 定义物品提供的属性加成(如力量、耐力、智力等)。
         * 物品可能有多个属性加成。
         */
        struct ItemStatData
        {
            uint32 ItemStatType = 0;       ///< 属性类型(力量、敏捷、耐力、智力、精神等)
            int32 ItemStatValue = 0;       ///< 属性值，可正可负(通常为正值)
        };

        /**
         * @struct ItemSpellData
         * @brief 物品法术数据结构
         *
         * 定义物品携带的法术效果。
         * 物品可能带有使用效果、装备效果或触发效果。
         */
        struct ItemSpellData
        {
            int32 SpellId = -1;            ///< 法术ID，-1表示无法术
            uint32 SpellTrigger = 0;       ///< 法术触发类型(使用、装备、击中时触发等)
            int32 SpellCharges = 0;        ///< 法术充能次数，负数表示消耗后消失
            int32 SpellCooldown = -1;      ///< 法术冷却时间(毫秒)，-1表示使用默认冷却
            uint32 SpellCategory = 0;      ///< 法术分类ID，用于共享冷却
            int32 SpellCategoryCooldown = -1;  ///< 分类冷却时间(毫秒)
        };

        /**
         * @struct ItemSocketData
         * @brief 物品插槽数据结构
         *
         * 定义物品的宝石插槽信息。
         * 镶嵌宝石后，Content字段记录宝石的物品ID。
         */
        struct ItemSocketData
        {
            uint32 Color = 0;              ///< 插槽颜色(红色、黄色、蓝色、多彩等)
            uint32 Content = 0;            ///< 插槽内容，镶嵌宝石的物品ID
        };

        /**
         * @struct ItemStats
         * @brief 物品统计数据结构
         *
         * 包含物品的所有属性信息，这些数据来自数据库的item_template表。
         * 包括基础属性、需求、伤害、抗性、法术效果、宝石插槽等完整信息。
         *
         * 这个结构体非常庞大，包含了物品系统的所有细节属性。
         */
        struct ItemStats
        {
            uint32 Class = 0;              ///< 物品类别(消耗品、容器、武器、护甲等)
            uint32 SubClass = 0;           ///< 物品子类别(武器类型、护甲类型等)
            int32 SoundOverrideSubclass = 0;  ///< 音效覆盖子类别
            std::string Name;              ///< 物品名称
            uint32 DisplayInfoID = 0;      ///< 显示信息ID，定义物品的视觉外观
            uint32 Quality = 0;            ///< 物品品质(垃圾、普通、优秀、精良、史诗、传说、神器)
            uint32 Flags = 0;              ///< 物品标志位，控制物品的特殊属性
            uint32 Flags2 = 0;             ///< 物品标志位2，额外的控制标志
            int32 BuyPrice = 0;            ///< 购买价格(铜币)
            uint32 SellPrice = 0;          ///< 出售价格(铜币)
            uint32 InventoryType = 0;      ///< 装备部位(头部、肩部、胸部、手、腿、脚等)
            uint32 AllowableClass = 0;     ///< 允许的职业掩码
            uint32 AllowableRace = 0;      ///< 允许的种族掩码
            uint32 ItemLevel = 0;          ///< 物品等级，影响属性和装等
            uint32 RequiredLevel = 0;      ///< 需要的等级
            uint32 RequiredSkill = 0;      ///< 需要的技能ID
            uint32 RequiredSkillRank = 0;  ///< 需要的技能等级
            uint32 RequiredSpell = 0;      ///< 需要的法术ID(如专业技能)
            uint32 RequiredHonorRank = 0;  ///< 需要的荣誉等级
            uint32 RequiredCityRank = 0;   ///< 需要的城市等级
            uint32 RequiredReputationFaction = 0;  ///< 需要的声望阵营ID
            uint32 RequiredReputationRank = 0;     ///< 需要的声望等级
            int32 MaxCount = 0;            ///< 最大拥有数量
            int32 Stackable = 0;           ///< 可堆叠数量
            uint32 ContainerSlots = 0;     ///< 容器槽位数(背包类物品)
            uint32 StatsCount = 0;         ///< 属性数量
            ItemStatData ItemStat[MAX_ITEM_PROTO_STATS];  ///< 属性数组
            uint32 ScalingStatDistribution = 0;  ///< 缩放属性分配ID
            uint32 ScalingStatValue = 0;         ///< 缩放属性值标志
            ItemDamageData Damage[MAX_ITEM_PROTO_DAMAGES];  ///< 伤害数组
            uint32 Resistance[MAX_SPELL_SCHOOL] = { };      ///< 抗性数组(物理、神圣、火焰、自然、冰霜、暗影、奥术)
            uint32 Delay = 0;              ///< 攻击延迟(毫秒)，武器的攻击速度
            uint32 AmmoType = 0;           ///< 弹药类型
            float RangedModRange = 0.0f;   ///< 远程武器射程修正
            ItemSpellData Spells[MAX_ITEM_PROTO_SPELLS];   ///< 法术效果数组
            uint32 Bonding = 0;            ///< 绑定类型(拾取绑定、装备绑定、使用绑定等)
            std::string Description;       ///< 物品描述文本
            uint32 PageText = 0;           ///< 页面文本ID，书本类物品的内容
            uint32 LanguageID = 0;         ///< 语言ID
            uint32 PageMaterial = 0;       ///< 页面材质ID
            uint32 StartQuest = 0;         ///< 起始任务ID，使用物品开始的任务
            uint32 LockID = 0;             ///< 锁ID，用于钥匙开锁
            int32 Material = 0;            ///< 材质类型(金属、皮革、布料等)
            uint32 Sheath = 0;             ///< 武器鞘套类型，决定武器在腰间的放置方式
            int32 RandomProperty = 0;      ///< 随机属性ID
            int32 RandomSuffix = 0;        ///< 随机后缀ID
            uint32 Block = 0;              ///< 格挡值，盾牌专用
            uint32 ItemSet = 0;            ///< 套装ID
            uint32 MaxDurability = 0;      ///< 最大耐久度
            uint32 Area = 0;               ///< 区域限制ID，只能在特定区域使用
            uint32 Map = 0;                ///< 地图限制ID，只能在特定地图使用
            uint32 BagFamily = 0;          ///< 背包家族ID，用于专业背包
            uint32 TotemCategory = 0;      ///< 图腾分类ID
            ItemSocketData Socket[MAX_ITEM_PROTO_SOCKETS];  ///< 宝石插槽数组
            uint32 SocketBonus = 0;        ///< 插槽奖励ID
            uint32 GemProperties = 0;      ///< 宝石属性ID
            uint32 RequiredDisenchantSkill = 0;  ///< 需要的附魔技能等级
            float ArmorDamageModifier = 0.0f;    ///< 护甲伤害修正
            uint32 Duration = 0;           ///< 持续时间(秒)，临时物品
            uint32 ItemLimitCategory = 0;  ///< 物品限制分类ID，WotLK新增
            uint32 HolidayId = 0;          ///< 节日ID，节日相关物品
        };

        /**
         * @class QueryItemSingleResponse
         * @brief 单个物品查询响应数据包
         *
         * 继承自 ServerPacket，用于服务器向客户端发送物品的详细信息。
         * 响应客户端的QueryItemSingle请求，包含物品的完整属性数据。
         *
         * 调用时机：
         * - 收到QueryItemSingle请求后立即响应
         * - 初始包大小设为500字节，因为物品数据通常较大
         *
         * 性能注意事项：
         * - 物品数据结构庞大，序列化开销较大
         * - 客户端会缓存物品信息，避免重复查询
         */
        class QueryItemSingleResponse final : public ServerPacket
        {
            public:
                /**
                 * @brief 构造函数
                 *
                 * 初始化服务器响应包，设置操作码为SMSG_ITEM_QUERY_SINGLE_RESPONSE
                 * 预留500字节的缓冲区空间
                 */
                QueryItemSingleResponse() : ServerPacket(SMSG_ITEM_QUERY_SINGLE_RESPONSE, 500) { }

                /**
                 * @brief 写入数据包内容
                 * @return 返回构建完成的世界包指针
                 *
                 * 将物品统计数据序列化为网络数据包格式发送给客户端
                 * 包含物品的所有属性、需求、伤害、法术效果等完整信息
                 */
                WorldPacket const* Write() override;

                uint32 ItemID = 0;         ///< 物品模板ID
                bool Allow = false;        ///< 是否允许查询，false表示查无此物品
                ItemStats Stats;           ///< 物品统计数据
        };

        /**
         * @class QuestPOIQuery
         * @brief 任务POI查询请求数据包
         *
         * 继承自 ClientPacket，用于客户端向服务器请求任务兴趣点(POI)信息。
         * POI用于在地图上显示任务目标位置、任务交接点等标记。
         *
         * 调用时机：
         * - 玩家打开地图时
         * - 接受新任务时
         * - 任务日志更新时
         */
        class QuestPOIQuery final : public ClientPacket
        {
            public:
                /**
                 * @brief 构造函数
                 * @param packet 世界包数据，从网络接收的原始数据包
                 */
                QuestPOIQuery(WorldPacket&& packet) : ClientPacket(CMSG_QUEST_POI_QUERY, std::move(packet)) { }

                /**
                 * @brief 读取数据包内容
                 *
                 * 从网络数据包中读取缺失的任务POI数量和任务ID列表
                 *
                 * 性能注意事项：
                 * - 任务POI查询数量限制为MAX_QUEST_LOG_SIZE(25)
                 * - 如果超过限制，会丢弃多余的数据
                 */
                void Read() override;

                uint32 MissingQuestCount = 0;  ///< 缺失POI信息的任务数量
                uint32 MissingQuestPOIs[MAX_QUEST_LOG_SIZE] = { };  ///< 缺失POI信息的任务ID数组
        };
    }
}

#endif // QueryPackets_h__
