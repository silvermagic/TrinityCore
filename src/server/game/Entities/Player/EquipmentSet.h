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
 * @file EquipmentSet.h
 * @brief 装备套装管理系统数据结构定义
 *
 * 本文件定义了玩家装备套装系统的核心数据结构，用于管理玩家保存的装备配置。
 * 装备套装系统允许玩家保存和快速切换多套装备配置，便于在不同场景
 * (如PVP、PVE、不同天赋)下快速更换装备。
 *
 * 主要功能包括：
 * - 存储装备套装的元数据(名称、图标)
 * - 记录套装中各个装备槽位的物品GUID
 * - 追踪套装数据的更新状态(新增、修改、删除)
 * - 支持客户端最多10套装备配置的限制
 */

#ifndef EquipmentSet_h__
#define EquipmentSet_h__

#include "Define.h"
#include "ObjectGuid.h"
#include <array>
#include <map>

/**
 * @brief 装备套装更新状态枚举
 *
 * 标识装备套装数据的当前状态，用于数据库同步和客户端更新判断。
 * 服务器通过状态标记追踪哪些套装需要保存到数据库或发送给客户端。
 */
enum EquipmentSetUpdateState
{
    EQUIPMENT_SET_UNCHANGED = 0,  ///< 未变更：套装数据自加载以来未修改，无需数据库保存
    EQUIPMENT_SET_CHANGED   = 1,  ///< 已变更：现有套装的属性被修改，需要更新数据库
    EQUIPMENT_SET_NEW       = 2,  ///< 新增：新创建的套装，需要插入到数据库
    EQUIPMENT_SET_DELETED   = 3   ///< 已删除：套装被删除，需要从数据库移除
};

/**
 * @brief 装备套装槽位数量
 *
 * 定义一个装备套装包含的装备槽位总数。
 * 这对应于玩家可装备的物品槽位数量（头部、颈部、肩膀、背部、胸部、手腕、
 * 手套、腰带、腿部、脚部、戒指x2、饰品x2、主手、副手、远程武器等）。
 */
#define EQUIPMENT_SET_SLOTS 19

/**
 * @brief 装备套装信息结构体
 *
 * 存储单个装备套装的完整信息，包括需要与客户端同步的数据和服务器端的状态管理数据。
 * 每个玩家最多可拥有 MAX_EQUIPMENT_SET_INDEX (10) 个装备套装配置。
 *
 * 数据分为两部分：
 * 1. Data：需要与客户端同步的数据，会通过网络包发送
 * 2. State：服务器端专用的状态标记，用于数据库同步控制
 */
struct EquipmentSetInfo
{
    /**
     * @brief 装备套装数据结构
     *
     * 包含所有需要与客户端同步的套装信息，这些数据会通过 SMSG_EQUIPMENT_SET_LIST
     * 等数据包发送给客户端，也会在套装保存时写入数据库的 equipment_sets 表。
     */
    struct EquipmentSetData
    {
        uint64 Guid = 0;                                        ///< 套装唯一标识符：用于客户端-服务器套装实例匹配
        uint32 SetID = 0;                                       ///< 套装索引：客户端显示位置(0-9)，同一玩家内唯一
        std::string SetName;                                    ///< 套装名称：用户自定义的套装显示名
        std::string SetIcon;                                    ///< 套装图标：UI显示用的图标资源路径或标识
        uint32 IgnoreMask = 0;                                  ///< 忽略槽位掩码：标记哪些槽位不参与套装切换
        std::array<ObjectGuid, EQUIPMENT_SET_SLOTS> Pieces = {}; ///< 各槽位物品GUID：按装备槽位索引存储物品引用
    } Data;

    /**
     * @brief 套装更新状态
     *
     * 服务器端专用字段，不发送给客户端。
     * 用于追踪套装数据是否需要数据库操作（INSERT/UPDATE/DELETE）。
     * 默认状态为 EQUIPMENT_SET_NEW，表示新建套装需要插入数据库。
     */
    EquipmentSetUpdateState State = EQUIPMENT_SET_NEW;
};

/**
 * @brief 装备套装最大数量限制
 *
 * 定义玩家可拥有的最大装备套装数量。
 * 此限制由客户端UI决定，客户端仅提供10个套装保存槽位。
 * 服务器必须强制此限制，避免创建超出客户端显示能力的数据。
 */
#define MAX_EQUIPMENT_SET_INDEX 10

/**
 * @brief 装备套装容器类型
 *
 * 使用GUID作为键的映射容器，存储玩家的所有装备套装信息。
 * 选择map而非vector的原因：
 * - 通过GUID快速查找特定套装，O(log n)复杂度
 * - 套装可能被删除，GUID作为键保持引用稳定性
 * - 数据库操作时需要GUID作为主键
 *
 * 性能考虑：
 * - 插入/删除：O(log n)
 * - 查找：O(log n)
 * - 遍历：所有套装用于保存或发送给客户端时使用迭代器
 */
typedef std::map<uint64, EquipmentSetInfo> EquipmentSetContainer;

#endif // EquipmentSet_h__
