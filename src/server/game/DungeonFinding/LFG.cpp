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
 * @file LFG.cpp
 * @brief LFG系统辅助函数实现
 *
 * 本文件实现了LFG系统的辅助工具函数，主要包括：
 * - 副本ID集合的字符串格式化
 * - 角色类型的本地化字符串转换
 * - LFG状态的本地化字符串转换
 *
 * 这些函数主要用于日志输出、调试信息和客户端显示。
 */

#include "LFG.h"
#include "Language.h"
#include "ObjectMgr.h"
#include <sstream>

namespace lfg
{

/**
 * @brief 将副本ID集合连接成字符串
 *
 * @param dungeons 副本ID集合
 * @return 格式化的字符串，多个副本ID用逗号分隔
 *
 * @details 该函数将副本ID集合转换为可读的字符串格式。
 *          如果集合为空，返回空字符串。
 *          第一个副本ID前不加逗号，后续副本ID前添加逗号和空格。
 *
 * @performance 时间复杂度O(n)，其中n为副本数量
 *
 * @example
 *   LfgDungeonSet dungeons = {123, 456, 789};
 *   std::string str = ConcatenateDungeons(dungeons);  // "123, 456, 789"
 */
std::string ConcatenateDungeons(LfgDungeonSet const& dungeons)
{
    std::string dungeonstr = "";
    if (!dungeons.empty())
    {
        std::ostringstream o;
        LfgDungeonSet::const_iterator it = dungeons.begin();
        o << (*it);
        // 遍历剩余的副本ID，每个前面添加逗号和空格
        for (++it; it != dungeons.end(); ++it)
            o << ", " << uint32(*it);
        dungeonstr = o.str();
    }
    return dungeonstr;
}

/**
 * @brief 获取角色的字符串表示
 *
 * @param roles 角色位掩码
 * @return 角色名称字符串，多个角色用逗号分隔
 *
 * @details 该函数将角色位掩码转换为本地化的字符串表示。
 *          支持的角色包括：坦克、治疗、输出、队长。
 *          角色按固定顺序显示：坦克 -> 治疗 -> 输出 -> 队长
 *          如果角色掩码为0，返回"无角色"的本地化字符串。
 *
 * @note 返回的字符串根据服务器的DBC语言设置进行本地化
 *
 * @example
 *   uint8 roles = PLAYER_ROLE_TANK | PLAYER_ROLE_LEADER;
 *   std::string str = GetRolesString(roles);  // "坦克, 队长"（中文）
 */
std::string GetRolesString(uint8 roles)
{
    std::string rolesstr = "";

    // 检查坦克角色
    if (roles & PLAYER_ROLE_TANK)
        rolesstr.append(sObjectMgr->GetTrinityStringForDBCLocale(LANG_LFG_ROLE_TANK));

    // 检查治疗角色
    if (roles & PLAYER_ROLE_HEALER)
    {
        if (!rolesstr.empty())
            rolesstr.append(", ");
        rolesstr.append(sObjectMgr->GetTrinityStringForDBCLocale(LANG_LFG_ROLE_HEALER));
    }

    // 检查输出角色
    if (roles & PLAYER_ROLE_DAMAGE)
    {
        if (!rolesstr.empty())
            rolesstr.append(", ");
        rolesstr.append(sObjectMgr->GetTrinityStringForDBCLocale(LANG_LFG_ROLE_DAMAGE));
    }

    // 检查队长角色
    if (roles & PLAYER_ROLE_LEADER)
    {
        if (!rolesstr.empty())
            rolesstr.append(", ");
        rolesstr.append(sObjectMgr->GetTrinityStringForDBCLocale(LANG_LFG_ROLE_LEADER));
    }

    // 如果没有任何角色，返回"无角色"
    if (rolesstr.empty())
        rolesstr.append(sObjectMgr->GetTrinityStringForDBCLocale(LANG_LFG_ROLE_NONE));

    return rolesstr;
}

/**
 * @brief 获取LFG状态的字符串表示
 *
 * @param state LFG状态枚举
 * @return 状态名称字符串
 *
 * @details 该函数将LFG状态枚举转换为本地化的字符串表示。
 *          如果状态未识别，返回错误信息。
 *
 * @note 返回的字符串根据服务器的DBC语言设置进行本地化
 *
 * @example
 *   std::string str = GetStateString(LFG_STATE_QUEUED);  // "排队中"（中文）
 */
std::string GetStateString(LfgState state)
{
    int32 entry = LANG_LFG_ERROR;  // 默认为错误状态
    switch (state)
    {
        case LFG_STATE_NONE:
            entry = LANG_LFG_STATE_NONE;
            break;
        case LFG_STATE_ROLECHECK:
            entry = LANG_LFG_STATE_ROLECHECK;
            break;
        case LFG_STATE_QUEUED:
            entry = LANG_LFG_STATE_QUEUED;
            break;
        case LFG_STATE_PROPOSAL:
            entry = LANG_LFG_STATE_PROPOSAL;
            break;
        case LFG_STATE_DUNGEON:
            entry = LANG_LFG_STATE_DUNGEON;
            break;
        case LFG_STATE_FINISHED_DUNGEON:
            entry = LANG_LFG_STATE_FINISHED_DUNGEON;
            break;
        case LFG_STATE_RAIDBROWSER:
            entry = LANG_LFG_STATE_RAIDBROWSER;
            break;
    }

    return std::string(sObjectMgr->GetTrinityStringForDBCLocale(entry));
}

} // namespace lfg
