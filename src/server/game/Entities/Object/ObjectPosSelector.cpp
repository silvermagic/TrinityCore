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
 * @file ObjectPosSelector.cpp
 * @brief 对象位置选择器实现
 *
 * 本文件实现了在指定距离周围寻找可用位置的功能。
 * 详细说明请参考 ObjectPosSelector.h
 */

#include "ObjectPosSelector.h"

/**
 * @brief 构造函数
 * @param x 中心点X坐标
 * @param y 中心点Y坐标
 * @param size 中心对象的大小
 * @param dist 搜索距离（包括中心对象大小）
 *
 * 初始化位置选择器，计算角度步长，并初始化所有迭代器和状态变量。
 */
ObjectPosSelector::ObjectPosSelector(float x, float y, float size, float dist)
: m_center_x(x), m_center_y(y), m_size(size), m_dist(dist)
{
    // 计算角度步长，基于几何关系：cos(angle) = dist / (dist + 2*size)
    // 这是两个相邻对象之间所需的最小角度间隔
    m_anglestep = std::acos(m_dist/(m_dist+2*m_size));

    // 初始化搜索迭代器为end状态
    m_nextUsedPos[USED_POS_PLUS]  = m_UsedPosLists[USED_POS_PLUS].end();
    m_nextUsedPos[USED_POS_MINUS] = m_UsedPosLists[USED_POS_MINUS].end();

    // 初始化小步长搜索状态
    m_smallStepAngle[USED_POS_PLUS]  = 0;
    m_smallStepAngle[USED_POS_MINUS] = 0;

    m_smallStepOk[USED_POS_PLUS]  = false;
    m_smallStepOk[USED_POS_MINUS] = false;

    m_smallStepNextUsedPos[USED_POS_PLUS]  = nullptr;
    m_smallStepNextUsedPos[USED_POS_MINUS] = nullptr;
}

/**
 * @brief 获取下一个已占用位置
 * @param uptype 位置类型（正角或负角）
 * @return 下一个已占用位置的指针，如果没有则返回nullptr
 *
 * 查找顺序：
 * 1. 如果当前列表还有下一个位置，返回它
 * 2. 如果当前列表已遍历完，检查另一个列表的最后一个位置
 * 3. 如果两个列表都遍历完，返回nullptr
 */
ObjectPosSelector::UsedPosList::value_type const* ObjectPosSelector::nextUsedPos(UsedPosType uptype)
{
    UsedPosList::const_iterator itr = m_nextUsedPos[uptype];
    if (itr!=m_UsedPosLists[uptype].end())
        ++itr;

    if (itr == m_UsedPosLists[uptype].end())
    {
        // 当前列表已遍历完，检查另一个列表（~uptype）的最后一个位置
        if (!m_UsedPosLists[~uptype].empty())
            return &*m_UsedPosLists[~uptype].rbegin();
        else
            return nullptr;
    }
    else
        return &*itr;
}

/**
 * @brief 添加一个已占用位置
 * @param size 对象大小
 * @param angle 角度（弧度）
 * @param dist 到中心点的距离
 *
 * 根据角度的正负，将位置添加到对应的列表中。
 * 正角度添加到USED_POS_PLUS列表，负角度添加到USED_POS_MINUS列表。
 */
void ObjectPosSelector::AddUsedPos(float size, float angle, float dist)
{
    if (angle >= 0)
        m_UsedPosLists[USED_POS_PLUS].insert(UsedPosList::value_type(angle, UsedPos(1.0f, size, dist)));
    else
        m_UsedPosLists[USED_POS_MINUS].insert(UsedPosList::value_type(-angle, UsedPos(-1.0f, size, dist)));
}

/**
 * @brief 初始化角度搜索
 *
 * 重置所有搜索迭代器和状态变量，准备开始新的搜索。
 */
void ObjectPosSelector::InitializeAngle()
{
    // 将迭代器指向列表开头
    m_nextUsedPos[USED_POS_PLUS]  = m_UsedPosLists[USED_POS_PLUS].begin();
    m_nextUsedPos[USED_POS_MINUS] = m_UsedPosLists[USED_POS_MINUS].begin();

    // 重置小步长搜索状态
    m_smallStepAngle[USED_POS_PLUS]  = 0;
    m_smallStepAngle[USED_POS_MINUS] = 0;

    m_smallStepOk[USED_POS_PLUS]  = true;
    m_smallStepOk[USED_POS_MINUS] = true;
}

/**
 * @brief 查找第一个可用角度
 * @param angle 输出参数，找到的可用角度
 * @return 如果找到返回true，否则返回false
 *
 * 处理不平衡的情况：当一个列表为空，另一个不为空时，
 * 从非空列表的第一个位置向空列表的方向查找。
 */
bool ObjectPosSelector::FirstAngle(float& angle)
{
    if (m_UsedPosLists[USED_POS_PLUS].empty() && !m_UsedPosLists[USED_POS_MINUS].empty())
        return NextAngleFor(*m_UsedPosLists[USED_POS_MINUS].begin(), 1.0f, USED_POS_PLUS, angle);
    else if (m_UsedPosLists[USED_POS_MINUS].empty() && !m_UsedPosLists[USED_POS_PLUS].empty())
        return NextAngleFor(*m_UsedPosLists[USED_POS_PLUS].begin(), -1.0f, USED_POS_MINUS, angle);

    return false;
}

/**
 * @brief 查找下一个可用角度
 * @param angle 输出参数，找到的可用角度
 * @return 如果找到返回true，否则返回false
 *
 * 循环遍历所有可能的位置，直到找到可用的角度或遍历完所有位置。
 */
bool ObjectPosSelector::NextAngle(float& angle)
{
    while (m_nextUsedPos[USED_POS_PLUS]!=m_UsedPosLists[USED_POS_PLUS].end() ||
        m_nextUsedPos[USED_POS_MINUS]!=m_UsedPosLists[USED_POS_MINUS].end() ||
        m_smallStepOk[USED_POS_PLUS] || m_smallStepOk[USED_POS_MINUS] )
    {
        // calculate next possible angle
        if (NextPosibleAngle(angle))
            return true;
    }

    return false;
}

/**
 * @brief 查找下一个已占用角度
 * @param angle 输出参数，找到的已占用角度
 * @return 如果找到返回true，否则返回false
 *
 * 与NextAngle相反，返回已占用位置之间的角度。
 */
bool ObjectPosSelector::NextUsedAngle(float& angle)
{
    while (m_nextUsedPos[USED_POS_PLUS]!=m_UsedPosLists[USED_POS_PLUS].end() ||
        m_nextUsedPos[USED_POS_MINUS]!=m_UsedPosLists[USED_POS_MINUS].end())
    {
        // calculate next possible angle
        if (!NextPosibleAngle(angle))
            return true;
    }

    return false;
}

/**
 * @brief 查找下一个可能可用的角度
 * @param angle 输出参数，找到的角度
 * @return 如果找到可用角度返回true，否则返回false
 *
 * 核心搜索算法：
 * 1. 选择角度较小的方向进行搜索
 * 2. 尝试大步长搜索（跳到下一个已占用位置）
 * 3. 如果大步长不可行，尝试小步长搜索
 * 4. 如果小步长也不可行，移动到下一个已占用位置
 */
bool ObjectPosSelector::NextPosibleAngle(float& angle)
{
    // ++ direction less updated
    if (m_nextUsedPos[USED_POS_PLUS]!=m_UsedPosLists[USED_POS_PLUS].end() &&
        (m_nextUsedPos[USED_POS_MINUS]==m_UsedPosLists[USED_POS_MINUS].end() || m_nextUsedPos[USED_POS_PLUS]->first <= m_nextUsedPos[USED_POS_MINUS]->first))
    {
        bool ok;
        if (m_smallStepOk[USED_POS_PLUS])
            ok = NextSmallStepAngle(1.0f, USED_POS_PLUS, angle);
        else
            ok = NextAngleFor(*m_nextUsedPos[USED_POS_PLUS], 1.0f, USED_POS_PLUS, angle);

        if (!ok)
            ++m_nextUsedPos[USED_POS_PLUS];                 // increase. only at fail (original or checked)
        return ok;
    }
    // -- direction less updated
    else if (m_nextUsedPos[USED_POS_MINUS]!=m_UsedPosLists[USED_POS_MINUS].end())
    {
        bool ok;
        if (m_smallStepOk[USED_POS_MINUS])
            ok = NextSmallStepAngle(-1.0f, USED_POS_MINUS, angle);
        else
            ok = NextAngleFor(*m_nextUsedPos[USED_POS_MINUS], -1.0f, USED_POS_MINUS, angle);

        if (!ok)
            ++m_nextUsedPos[USED_POS_MINUS];
        return ok;
    }
    else                                                    // both list empty
    {
        if (m_smallStepOk[USED_POS_PLUS] && (!m_smallStepOk[USED_POS_MINUS] || m_smallStepAngle[USED_POS_PLUS] <= m_smallStepAngle[USED_POS_MINUS]))
            return NextSmallStepAngle(1.0f, USED_POS_PLUS, angle);
        // -- direction less updated
        else if (m_smallStepOk[USED_POS_MINUS])
            return NextSmallStepAngle(-1.0f, USED_POS_MINUS, angle);
    }

    // no angles
    return false;
}
