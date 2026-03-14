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
 * @file DisableMgr.h
 * @brief 禁用管理器模块 - 负责游戏内容的禁用控制
 *
 * 本模块提供了游戏内容禁用功能，用于控制特定游戏元素的可用性：
 * - 法术禁用（可针对玩家、生物、宠物等不同目标）
 * - 任务禁用
 * - 地图禁用（副本、战场等）
 * - 战场禁用
 * - 成就条件禁用
 * - 户外PvP禁用
 * - VMAP（可视地图）禁用
 * - MMAP（移动地图）禁用
 *
 * 禁用数据从数据库的disables表加载，支持运行时热重载。
 */

#ifndef TRINITY_DISABLEMGR_H
#define TRINITY_DISABLEMGR_H

#include "Define.h"

class WorldObject;

/**
 * @brief 禁用类型枚举 - 定义可禁用的游戏内容类型
 */
enum DisableType
{
    DISABLE_TYPE_SPELL                  = 0,  // 法术禁用
    DISABLE_TYPE_QUEST                  = 1,  // 任务禁用
    DISABLE_TYPE_MAP                    = 2,  // 地图禁用
    DISABLE_TYPE_BATTLEGROUND           = 3,  // 战场禁用
    DISABLE_TYPE_ACHIEVEMENT_CRITERIA   = 4,  // 成就条件禁用
    DISABLE_TYPE_OUTDOORPVP             = 5,  // 户外PvP禁用
    DISABLE_TYPE_VMAP                   = 6,  // VMAP（可视地图）禁用
    DISABLE_TYPE_MMAP                   = 7,  // MMAP（移动地图）禁用
    DISABLE_TYPE_LFG_MAP                = 8   // 随机副本地图禁用
};

/**
 * @brief 法术禁用类型枚举 - 定义法术禁用的具体目标
 *
 * 使用位掩码，可以组合多种禁用类型。
 */
enum SpellDisableTypes
{
    SPELL_DISABLE_PLAYER            = 0x01,   // 禁用于玩家
    SPELL_DISABLE_CREATURE          = 0x02,   // 禁用于生物
    SPELL_DISABLE_PET               = 0x04,   // 禁用于宠物
    SPELL_DISABLE_DEPRECATED_SPELL  = 0x08,   // 已废弃的法术
    SPELL_DISABLE_MAP               = 0x10,   // 在特定地图禁用
    SPELL_DISABLE_AREA              = 0x20,   // 在特定区域禁用
    SPELL_DISABLE_LOS               = 0x40,   // 禁用视线检查
    SPELL_DISABLE_GAMEOBJECT        = 0x80,   // 禁用于游戏对象
    SPELL_DISABLE_ARENAS            = 0x100,  // 在竞技场禁用
    SPELL_DISABLE_BATTLEGROUNDS     = 0x200,  // 在战场禁用
    MAX_SPELL_DISABLE_TYPE = (  SPELL_DISABLE_PLAYER | SPELL_DISABLE_CREATURE | SPELL_DISABLE_PET |
                                SPELL_DISABLE_DEPRECATED_SPELL | SPELL_DISABLE_MAP | SPELL_DISABLE_AREA |
                                SPELL_DISABLE_LOS | SPELL_DISABLE_GAMEOBJECT | SPELL_DISABLE_ARENAS |
                                SPELL_DISABLE_BATTLEGROUNDS),
};

/**
 * @brief MMAP禁用类型枚举 - 定义移动地图的禁用选项
 */
enum MMapDisableTypes
{
    MMAP_DISABLE_PATHFINDING    = 0x0   // 禁用寻路
};

/**
 * @brief DisableMgr命名空间 - 提供禁用管理的公共接口
 */
namespace DisableMgr
{
    /**
     * @brief 从数据库加载所有禁用设置
     *
     * 调用时机：服务器启动时或执行重载命令时
     * 性能注意：加载过程会读取整个disables表，耗时较短
     */
    TC_GAME_API void LoadDisables();

    /**
     * @brief 检查指定条目是否被禁用
     * @param type 禁用类型
     * @param entry 条目ID（法术ID、任务ID、地图ID等）
     * @param ref 参考对象（用于上下文相关的判断）
     * @param flags 额外标志位
     * @return 是否被禁用
     *
     * 这是最常用的禁用检查接口，被多个游戏系统调用。
     */
    TC_GAME_API bool IsDisabledFor(DisableType type, uint32 entry, WorldObject const* ref, uint8 flags = 0);

    /**
     * @brief 检查任务禁用的有效性
     *
     * 调用时机：任务加载后
     * 用于验证任务禁用设置的有效性，移除无效的任务禁用。
     */
    TC_GAME_API void CheckQuestDisables();

    /**
     * @brief 检查VMAP是否被禁用
     * @param entry 地图ID
     * @param flags VMAP禁用标志
     * @return VMAP是否被禁用
     *
     * 用于地形和视线检查。
     */
    TC_GAME_API bool IsVMAPDisabledFor(uint32 entry, uint8 flags);

    /**
     * @brief 检查指定地图是否启用了寻路
     * @param mapId 地图ID
     * @return 是否启用寻路
     *
     * 需要同时检查全局配置和该地图的MMAP禁用状态。
     */
    TC_GAME_API bool IsPathfindingEnabled(uint32 mapId);
}

#endif //TRINITY_DISABLEMGR_H
