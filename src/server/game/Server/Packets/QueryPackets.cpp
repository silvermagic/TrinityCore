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
 * @file QueryPackets.cpp
 * @brief 查询数据包实现文件
 *
 * 本文件实现了查询数据包的读取和写入功能，包括：
 * - 生物查询请求的读取
 * - 生物查询响应的写入
 * - 游戏对象查询请求的读取
 * - 游戏对象查询响应的写入
 * - 物品查询请求的读取
 * - 物品查询响应的写入
 * - 任务POI查询请求的读取
 *
 * 数据包序列化和反序列化遵循魔兽世界3.3.5客户端协议。
 */

#include "QueryPackets.h"

/**
 * @brief 读取生物查询请求数据包
 *
 * 从客户端接收的数据包中读取生物ID和GUID。
 * 客户端通过这个包请求特定生物的详细信息。
 *
 * 数据包格式：
 * - uint32 CreatureID: 生物模板ID
 * - ObjectGuid Guid: 生物实例GUID
 */
void WorldPackets::Query::QueryCreature::Read()
{
    _worldPacket >> CreatureID;  // 读取生物模板ID
    _worldPacket >> Guid;        // 读取生物实例GUID
}

/**
 * @brief 写入生物查询响应数据包
 * @return 返回构建完成的世界包指针
 *
 * 将生物统计数据序列化为网络数据包发送给客户端。
 * 如果Allow为false，只发送生物ID和失败标志，不包含详细数据。
 * 如果Allow为true，发送完整的生物信息。
 *
 * 数据包格式：
 * - uint32 CreatureID | 标志位(0x80000000表示失败)
 * - 如果成功(Allow=true):
 *   - string Name: 生物名称
 *   - uint8 x3: 空字符串(name2, name3, name4)
 *   - string NameAlt: 备用名称
 *   - string CursorName: 光标名称
 *   - uint32 Flags: 生物标志
 *   - uint32 CreatureType: 生物类型
 *   - uint32 CreatureFamily: 生物科属
 *   - uint32 Classification: 等级分类
 *   - uint32[2] ProxyCreatureID: 代理生物ID数组
 *   - uint32[4] CreatureDisplayID: 显示ID数组
 *   - float HpMulti: 生命值倍数
 *   - float EnergyMulti: 能量值倍数
 *   - uint8 Leader: 是否为领袖
 *   - uint32[4] QuestItems: 任务物品数组
 *   - uint32 CreatureMovementInfoID: 移动信息ID
 *
 * 性能注意事项：
 * - 使用位运算设置标志位，避免额外的标志字段
 * - 空名称字段使用uint8(0)代替空字符串，节省带宽
 */
WorldPacket const* WorldPackets::Query::QueryCreatureResponse::Write()
{
    // 写入生物ID，使用最高位作为成功/失败标志
    // 0x80000000表示查询失败，0x00000000表示成功
    _worldPacket << uint32(CreatureID | (Allow ? 0x00000000 : 0x80000000)); // creature entry

    if (Allow)
    {
        // 写入生物基本信息
        _worldPacket << Stats.Name;
        _worldPacket << uint8(0) << uint8(0) << uint8(0);                   // name2, name3, name4, always empty
        _worldPacket << Stats.NameAlt;
        _worldPacket << Stats.CursorName;                                   // "Directions" for guard, string for Icons 2.3.0

        // 写入生物类型和属性信息
        _worldPacket << uint32(Stats.Flags);                                // flags
        _worldPacket << uint32(Stats.CreatureType);                         // CreatureType.dbc
        _worldPacket << uint32(Stats.CreatureFamily);                       // CreatureFamily.dbc
        _worldPacket << uint32(Stats.Classification);                       // Creature Rank (elite, boss, etc)

        // 写入代理生物ID数组(击杀任务计数用，3.1版本新增)
        _worldPacket.append(Stats.ProxyCreatureID, MAX_KILL_CREDIT);        // new in 3.1, kill credit

        // 写入生物显示ID数组(模型外观)
        _worldPacket.append(Stats.CreatureDisplayID, MAX_CREATURE_MODELS);  // Modelid

        // 写入属性倍数
        _worldPacket << float(Stats.HpMulti);                               // dmg/hp modifier
        _worldPacket << float(Stats.EnergyMulti);                           // dmg/mana modifier

        // 写入额外标志和任务物品
        _worldPacket << uint8(Stats.Leader);
        _worldPacket.append(Stats.QuestItems, MAX_CREATURE_QUEST_ITEMS);

        // 写入移动信息ID
        _worldPacket << uint32(Stats.CreatureMovementInfoID);               // CreatureMovementInfo.dbc
    }

    return &_worldPacket;
}

/**
 * @brief 读取游戏对象查询请求数据包
 *
 * 从客户端接收的数据包中读取游戏对象ID和GUID。
 * 客户端通过这个包请求特定游戏对象的详细信息。
 *
 * 数据包格式：
 * - uint32 GameObjectID: 游戏对象模板ID
 * - ObjectGuid Guid: 游戏对象实例GUID
 */
void WorldPackets::Query::QueryGameObject::Read()
{
    _worldPacket >> GameObjectID;  // 读取游戏对象模板ID
    _worldPacket >> Guid;          // 读取游戏对象实例GUID
}

/**
 * @brief 写入游戏对象查询响应数据包
 * @return 返回构建完成的世界包指针
 *
 * 将游戏对象统计数据序列化为网络数据包发送给客户端。
 * 如果Allow为false，只发送游戏对象ID和失败标志。
 * 如果Allow为true，发送完整的游戏对象信息。
 *
 * 数据包格式：
 * - uint32 GameObjectID | 标志位(0x80000000表示失败)
 * - 如果成功(Allow=true):
 *   - uint32 Type: 游戏对象类型
 *   - uint32 DisplayID: 显示ID
 *   - string Name: 名称
 *   - uint8 x3: 空字符串(name2, name3, name4)
 *   - string IconName: 图标名称(2.0.3版本新增)
 *   - string CastBarCaption: 施法条标题(2.0.3版本新增)
 *   - string UnkString: 未知字符串(2.0.3版本新增)
 *   - uint32[24] Data: 游戏对象数据数组
 *   - float Size: 大小
 *   - uint32[4] QuestItems: 任务物品数组
 *
 * 性能注意事项：
 * - Data数组根据游戏对象类型不同存储不同参数
 * - 空名称字段使用uint8(0)节省带宽
 */
WorldPacket const* WorldPackets::Query::QueryGameObjectResponse::Write()
{
    // 写入游戏对象ID，使用最高位作为成功/失败标志
    _worldPacket << uint32(GameObjectID | (Allow ? 0x00000000 : 0x80000000));

    if (Allow)
    {
        // 写入游戏对象基本信息
        _worldPacket << uint32(Stats.Type);
        _worldPacket << uint32(Stats.DisplayID);
        _worldPacket << Stats.Name;
        _worldPacket << uint8(0) << uint8(0) << uint8(0);           // name2, name3, name4

        // 写入交互相关信息(2.0.3版本新增)
        _worldPacket << Stats.IconName;                             // 2.0.3, string. Icon name to use instead of default icon for go's (ex: "Attack" makes sword)
        _worldPacket << Stats.CastBarCaption;                       // 2.0.3, string. Text will appear in Cast Bar when using GO (ex: "Collecting")
        _worldPacket << Stats.UnkString;                            // 2.0.3, string

        // 写入游戏对象数据和大小
        _worldPacket.append(Stats.Data, MAX_GAMEOBJECT_DATA);
        _worldPacket << float(Stats.Size);                          // go size

        // 写入任务物品数组
        _worldPacket.append(Stats.QuestItems, MAX_GAMEOBJECT_QUEST_ITEMS);
    }

    return &_worldPacket;
}

/**
 * @brief 读取物品查询请求数据包
 *
 * 从客户端接收的数据包中读取物品ID。
 * 客户端通过这个包请求特定物品的详细信息。
 *
 * 数据包格式：
 * - uint32 ItemID: 物品模板ID
 */
void WorldPackets::Query::QueryItemSingle::Read()
{
    _worldPacket >> ItemID;  // 读取物品模板ID
}

/**
 * @brief 写入物品查询响应数据包
 * @return 返回构建完成的世界包指针
 *
 * 将物品统计数据序列化为网络数据包发送给客户端。
 * 如果Allow为false，只发送物品ID和失败标志。
 * 如果Allow为true，发送完整的物品信息，包括：
 * - 基本信息(类别、品质、名称等)
 * - 需求条件(等级、职业、种族等)
 * - 属性加成(力量、耐力等)
 * - 伤害数据(武器专用)
 * - 抗性数据
 * - 法术效果
 * - 宝石插槽
 * - 其他特殊属性
 *
 * 数据包格式非常复杂，包含大量字段，详见代码注释。
 *
 * 性能注意事项：
 * - 物品数据结构庞大，序列化开销较大
 * - 只序列化有效的属性数量(StatsCount)
 * - 法术效果需要特殊处理，无效法术使用默认值
 * - 抗性数组固定为7个元素
 */
WorldPacket const* WorldPackets::Query::QueryItemSingleResponse::Write()
{
    // 写入物品ID，使用最高位作为成功/失败标志
    _worldPacket << uint32(ItemID | (Allow ? 0x00000000 : 0x80000000));

    if (Allow)
    {
        // 写入基本物品信息
        _worldPacket << Stats.Class;
        _worldPacket << Stats.SubClass;
        _worldPacket << Stats.SoundOverrideSubclass;
        _worldPacket << Stats.Name;
        _worldPacket << uint8(0x00);                              //Name2; // blizz not send name there, just uint8(0x00); <-- \0 = empty string = empty name...
        _worldPacket << uint8(0x00);                              //Name3; // blizz not send name there, just uint8(0x00);
        _worldPacket << uint8(0x00);                              //Name4; // blizz not send name there, just uint8(0x00);

        // 写入物品外观和品质信息
        _worldPacket << Stats.DisplayInfoID;
        _worldPacket << Stats.Quality;
        _worldPacket << Stats.Flags;
        _worldPacket << Stats.Flags2;

        // 写入价格信息
        _worldPacket << Stats.BuyPrice;
        _worldPacket << Stats.SellPrice;

        // 写入装备位置和限制条件
        _worldPacket << Stats.InventoryType;
        _worldPacket << Stats.AllowableClass;
        _worldPacket << Stats.AllowableRace;

        // 写入物品等级和需求条件
        _worldPacket << Stats.ItemLevel;
        _worldPacket << Stats.RequiredLevel;
        _worldPacket << Stats.RequiredSkill;
        _worldPacket << Stats.RequiredSkillRank;
        _worldPacket << Stats.RequiredSpell;
        _worldPacket << Stats.RequiredHonorRank;
        _worldPacket << Stats.RequiredCityRank;
        _worldPacket << Stats.RequiredReputationFaction;
        _worldPacket << Stats.RequiredReputationRank;

        // 写入物品数量限制
        _worldPacket << int32(Stats.MaxCount);
        _worldPacket << int32(Stats.Stackable);
        _worldPacket << Stats.ContainerSlots;

        // 写入属性加成数据
        _worldPacket << Stats.StatsCount;                         // item stats count
        for (uint32 i = 0; i < Stats.StatsCount; ++i)
        {
            _worldPacket << Stats.ItemStat[i].ItemStatType;
            _worldPacket << Stats.ItemStat[i].ItemStatValue;
        }

        // 写入缩放属性数据
        _worldPacket << Stats.ScalingStatDistribution;            // scaling stats distribution
        _worldPacket << Stats.ScalingStatValue;                   // some kind of flags used to determine stat values column

        // 写入武器伤害数据(可能有多种伤害类型)
        for (uint8 i = 0; i < MAX_ITEM_PROTO_DAMAGES; ++i)
        {
            _worldPacket << Stats.Damage[i].DamageMin;
            _worldPacket << Stats.Damage[i].DamageMax;
            _worldPacket << Stats.Damage[i].DamageType;
        }

        // 写入抗性数据(7种法术学校: 物理、神圣、火焰、自然、冰霜、暗影、奥术)
        // resistances (7)
        for (uint8 i = SPELL_SCHOOL_NORMAL; i < MAX_SPELL_SCHOOL; ++i)
            _worldPacket << Stats.Resistance[i];

        // 写入武器属性
        _worldPacket << Stats.Delay;
        _worldPacket << Stats.AmmoType;
        _worldPacket << Stats.RangedModRange;

        // 写入物品法术效果(最多5个)
        for (uint8 s = 0; s < MAX_ITEM_PROTO_SPELLS; ++s)
        {
            // spells are validated on template loading
            // 只有有效的法术才写入完整数据，否则写入默认值
            if (Stats.Spells[s].SpellId > 0)
            {
                _worldPacket << Stats.Spells[s].SpellId;
                _worldPacket << Stats.Spells[s].SpellTrigger;
                _worldPacket << uint32(-abs(Stats.Spells[s].SpellCharges));  // 充能次数使用绝对值
                _worldPacket << uint32(Stats.Spells[s].SpellCooldown);
                _worldPacket << uint32(Stats.Spells[s].SpellCategory);
                _worldPacket << uint32(Stats.Spells[s].SpellCategoryCooldown);
            }
            else
            {
                // 无效法术使用默认值
                _worldPacket << uint32(0);
                _worldPacket << uint32(0);
                _worldPacket << uint32(0);
                _worldPacket << uint32(-1);  // 冷却时间-1表示无效
                _worldPacket << uint32(0);
                _worldPacket << uint32(-1);  // 分类冷却-1表示无效
            }
        }

        // 写入绑定类型和描述
        _worldPacket << Stats.Bonding;
        _worldPacket << Stats.Description;

        // 写入书本相关数据
        _worldPacket << Stats.PageText;
        _worldPacket << Stats.LanguageID;
        _worldPacket << Stats.PageMaterial;

        // 写入任务相关数据
        _worldPacket << Stats.StartQuest;
        _worldPacket << Stats.LockID;

        // 写入物品材质和外观
        _worldPacket << int32(Stats.Material);
        _worldPacket << Stats.Sheath;

        // 写入随机属性
        _worldPacket << Stats.RandomProperty;
        _worldPacket << Stats.RandomSuffix;

        // 写入盾牌格挡值
        _worldPacket << Stats.Block;

        // 写入套装信息
        _worldPacket << Stats.ItemSet;
        _worldPacket << Stats.MaxDurability;

        // 写入使用限制
        _worldPacket << Stats.Area;
        _worldPacket << Stats.Map;                                // Added in 1.12.x & 2.0.1 client branch
        _worldPacket << Stats.BagFamily;
        _worldPacket << Stats.TotemCategory;

        // 写入宝石插槽数据(最多3个插槽)
        for (uint8 s = 0; s < MAX_ITEM_PROTO_SOCKETS; ++s)
        {
            _worldPacket << Stats.Socket[s].Color;
            _worldPacket << Stats.Socket[s].Content;
        }

        // 写入宝石相关属性
        _worldPacket << Stats.SocketBonus;
        _worldPacket << Stats.GemProperties;
        _worldPacket << Stats.RequiredDisenchantSkill;
        _worldPacket << Stats.ArmorDamageModifier;

        // 写入临时物品和其他属性
        _worldPacket << Stats.Duration;                           // added in 2.4.2.8209, duration (seconds)
        _worldPacket << Stats.ItemLimitCategory;                  // WotLK, ItemLimitCategory
        _worldPacket << Stats.HolidayId;                          // Holiday.dbc?
    }

    return &_worldPacket;
}

/**
 * @brief 读取任务POI查询请求数据包
 *
 * 从客户端接收的数据包中读取缺失POI信息的任务列表。
 * 客户端通过这个包请求特定任务的兴趣点信息，用于在地图上显示任务标记。
 *
 * 数据包格式：
 * - uint32 MissingQuestCount: 缺失POI的任务数量(最大25)
 * - uint32[MissingQuestCount] MissingQuestPOIs: 任务ID数组
 *
 * 性能注意事项：
 * - 任务数量限制为MAX_QUEST_LOG_SIZE(25)，避免恶意大数据包
 * - 如果超过限制，只读取前25个任务ID
 * - 使用rfinish()确保数据包读取完成，避免数据残留
 */
void WorldPackets::Query::QuestPOIQuery::Read()
{
    _worldPacket >> MissingQuestCount; // quest count, max=25

    // 安全检查：只读取最多25个任务ID
    if (MissingQuestCount <= MAX_QUEST_LOG_SIZE)
    {
        for (uint8 i = 0; i < MissingQuestCount; ++i)
            _worldPacket >> MissingQuestPOIs[i];
    }

    // 确保数据包读取完成，重置读取位置
    _worldPacket.rfinish();
}
