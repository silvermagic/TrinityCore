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
 * @file LFGPlayerData.cpp
 * @brief LFG玩家数据类的实现文件
 *
 * 本文件实现了 LfgPlayerData 类，该类用于存储和管理玩家在随机副本查找系统(LFG)中的相关数据。
 * 包括玩家的 LFG 状态、队伍信息、角色选择、选择的副本列表等核心数据。
 * 这些数据在玩家参与随机副本查找、组队和副本完成过程中被维护和使用。
 */

#include "LFGPlayerData.h"

namespace lfg
{

/**
 * @brief LfgPlayerData 构造函数
 *
 * 初始化玩家的 LFG 数据，将所有状态和属性设置为默认值：
 * - LFG 状态设置为 LFG_STATE_NONE（无状态）
 * - 旧 LFG 状态设置为 LFG_STATE_NONE
 * - 阵营标识设置为 0
 * - 队伍 GUID 设置为空
 * - 角色标识设置为 0
 * - 备注信息设置为空字符串
 * - 加入时的队伍成员数量设置为 0
 */
LfgPlayerData::LfgPlayerData(): m_State(LFG_STATE_NONE), m_OldState(LFG_STATE_NONE),
    m_Team(0), m_Group(), m_Roles(0), m_Comment(""), m_NumberOfPartyMembersAtJoin(0)
{ }

/**
 * @brief LfgPlayerData 析构函数
 *
 * 清理玩家 LFG 数据，释放相关资源。
 */
LfgPlayerData::~LfgPlayerData() { }

/**
 * @brief 设置玩家的 LFG 状态
 *
 * 更新玩家的当前 LFG 状态，并根据不同状态执行相应的数据清理或保存操作：
 *
 * - LFG_STATE_NONE（无状态）或 LFG_STATE_FINISHED_DUNGEON（完成副本）：
 *   清理角色选择、已选副本列表和备注信息，因为这些数据不再需要
 *   然后保存到旧状态
 *
 * - LFG_STATE_DUNGEON（在副本中）：
 *   直接保存到旧状态，用于后续恢复
 *
 * - 其他状态：
 *   仅更新当前状态
 *
 * 这种设计允许玩家在完成副本后恢复到之前的状态（如继续排队），
 * 同时确保敏感数据（如角色选择）在适当的时机被清理。
 *
 * @param state 要设置的新 LFG 状态
 * @see LfgState
 * @see RestoreState()
 */
void LfgPlayerData::SetState(LfgState state)
{
    switch (state)
    {
        case LFG_STATE_NONE:
        case LFG_STATE_FINISHED_DUNGEON:
            m_Roles = 0;
            m_SelectedDungeons.clear();
            m_Comment.clear();
            [[fallthrough]];
        case LFG_STATE_DUNGEON:
            m_OldState = state;
            [[fallthrough]];
        default:
            m_State = state;
    }
}

/**
 * @brief 恢复玩家到之前的 LFG 状态
 *
 * 将玩家的当前 LFG 状态恢复为之前保存的旧状态。
 * 这个功能主要用于玩家离开副本后的状态恢复。
 *
 * 如果旧状态为 LFG_STATE_NONE，说明玩家之前没有进行 LFG 活动，
 * 此时需要清理已选副本列表和角色选择信息，因为玩家已经完全退出了 LFG 系统。
 *
 * 典型使用场景：
 * - 玩家完成副本后，恢复到 LFG_STATE_DUNGEON 状态以继续排队
 * - 玩家离开副本时，恢复到之前的状态
 *
 * @see SetState()
 */
void LfgPlayerData::RestoreState()
{
    if (m_OldState == LFG_STATE_NONE)
    {
        m_SelectedDungeons.clear();
        m_Roles = 0;
    }
    m_State = m_OldState;
}

/**
 * @brief 设置玩家的阵营标识
 *
 * 设置玩家所属的阵营（联盟或部落）。这个信息用于 LFG 匹配系统，
 * 确保只有相同阵营的玩家才能组队。
 *
 * @param team 阵营标识值（通常为 TEAM_ALLIANCE 或 TEAM_HORDE）
 */
void LfgPlayerData::SetTeam(uint8 team)
{
    m_Team = team;
}

/**
 * @brief 设置玩家所在的队伍 GUID
 *
 * 记录玩家当前所在队伍的全局唯一标识符。
 * 如果玩家不在任何队伍中，此值应为空 GUID。
 *
 * @param group 队伍的 ObjectGuid
 */
void LfgPlayerData::SetGroup(ObjectGuid group)
{
    m_Group = group;
}

/**
 * @brief 设置玩家选择的角色（坦克/治疗/输出）
 *
 * 设置玩家在 LFG 系统中愿意扮演的角色组合。
 * 角色标识可以是以下值的组合：
 * - PLAYER_ROLE_TANK（坦克）
 * - PLAYER_ROLE_HEALER（治疗）
 * - PLAYER_ROLE_DAMAGE（输出）
 *
 * 玩家可以选择多个角色（例如既当坦克又当输出），
 * LFG 系统会根据队伍需求分配角色。
 *
 * @param roles 角色标识的位掩码组合
 */
void LfgPlayerData::SetRoles(uint8 roles)
{
    m_Roles = roles;
}

/**
 * @brief 设置玩家的 LFG 备注信息
 *
 * 设置玩家在 LFG 系统中显示的备注文本。
 * 其他玩家在查看队伍申请时可以看到此备注。
 * 备注通常用于说明玩家的特殊需求或信息。
 *
 * @param comment 备注文本内容
 */
void LfgPlayerData::SetComment(std::string const& comment)
{
    m_Comment = comment;
}

/**
 * @brief 设置玩家选择的副本列表
 *
 * 设置玩家想要排队的副本集合。玩家可以选择一个或多个副本，
 * LFG 系统会尝试将玩家匹配到其中一个副本。
 *
 * 如果玩家选择的是随机副本，此列表可能包含特定的随机副本类型。
 *
 * @param dungeons 包含副本 ID 的集合
 * @see LfgDungeonSet
 */
void LfgPlayerData::SetSelectedDungeons(LfgDungeonSet const& dungeons)
{
    m_SelectedDungeons = dungeons;
}

/**
 * @brief 获取玩家当前的 LFG 状态
 *
 * 返回玩家在 LFG 系统中的当前状态。
 * 状态值可以是 LFG_STATE_NONE、LFG_STATE_DUNGEON 等，
 * 表示玩家当前是否在排队、是否在副本中等信息。
 *
 * @return 当前 LFG 状态
 * @see LfgState
 */
LfgState LfgPlayerData::GetState() const
{
    return m_State;
}

/**
 * @brief 获取玩家的旧 LFG 状态
 *
 * 返回玩家在进入当前状态之前保存的 LFG 状态。
 * 这主要用于玩家离开副本后恢复到之前的状态，
 * 例如继续排队或保持原有的排队设置。
 *
 * @return 旧的 LFG 状态
 * @see RestoreState()
 */
LfgState LfgPlayerData::GetOldState() const
{
    return m_OldState;
}

/**
 * @brief 获取玩家的阵营标识
 *
 * 返回玩家所属的阵营（联盟或部落）。
 * LFG 系统使用此信息确保只有相同阵营的玩家才能组队。
 *
 * @return 阵营标识值
 */
uint8 LfgPlayerData::GetTeam() const
{
    return m_Team;
}

/**
 * @brief 获取玩家所在队伍的 GUID
 *
 * 返回玩家当前所在队伍的全局唯一标识符。
 * 如果玩家不在任何队伍中，返回空 GUID。
 *
 * @return 队伍的 ObjectGuid
 */
ObjectGuid LfgPlayerData::GetGroup() const
{
    return m_Group;
}

/**
 * @brief 获取玩家选择的角色（坦克/治疗/输出）
 *
 * 返回玩家在 LFG 系统中选择的角色组合的位掩码。
 * 可以通过位运算检查是否包含特定角色：
 * - roles & PLAYER_ROLE_TANK：是否选择坦克
 * - roles & PLAYER_ROLE_HEALER：是否选择治疗
 * - roles & PLAYER_ROLE_DAMAGE：是否选择输出
 *
 * @return 角色标识的位掩码
 */
uint8 LfgPlayerData::GetRoles() const
{
    return m_Roles;
}

/**
 * @brief 获取玩家的 LFG 备注信息
 *
 * 返回玩家在 LFG 系统中设置的备注文本。
 * 此备注会显示给其他玩家查看。
 *
 * @return 备注文本的常量引用
 */
std::string const& LfgPlayerData::GetComment() const
{
    return m_Comment;
}

/**
 * @brief 获取玩家选择的副本列表
 *
 * 返回玩家想要排队的副本集合。
 * 玩家可以选择一个或多个副本进行排队。
 *
 * @return 包含副本 ID 的集合的常量引用
 * @see LfgDungeonSet
 */
LfgDungeonSet const& LfgPlayerData::GetSelectedDungeons() const
{
    return m_SelectedDungeons;
}

/**
 * @brief 设置玩家加入队伍时的成员数量
 *
 * 记录玩家加入当前队伍时的成员总数。
 * 此信息用于 LFG 奖励计算，特别是针对随机副本的奖励。
 *
 * 在 WoW 的 LFG 系统中，如果玩家是以小队形式加入随机副本，
 * 队伍成员数量可能会影响获得的奖励（如正义点数、凯旋纹章等）。
 * 此数据确保正确计算和发放奖励。
 *
 * @param count 加入时的队伍成员数量
 */
void LfgPlayerData::SetNumberOfPartyMembersAtJoin(uint8 count)
{
    m_NumberOfPartyMembersAtJoin = count;
}

/**
 * @brief 获取玩家加入队伍时的成员数量
 *
 * 返回玩家加入当前队伍时记录的成员总数。
 * 此信息用于 LFG 奖励计算和验证。
 *
 * @return 加入时的队伍成员数量
 */
uint8 LfgPlayerData::GetNumberOfPartyMembersAtJoin()
{
    return m_NumberOfPartyMembersAtJoin;
}

} // namespace lfg
