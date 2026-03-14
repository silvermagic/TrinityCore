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
 * @file ObjectPosSelector.h
 * @brief 对象位置选择器
 *
 * 本模块实现了在指定距离周围寻找可用位置的功能。
 * 主要用于生物召唤、队列排列等场景，避免多个对象重叠。
 *
 * 工作原理：
 * - 维护一个中心点和已占用位置的列表
 * - 按角度顺序查找可用的空闲位置
 * - 考虑对象的大小和碰撞半径
 *
 * 使用场景：
 * - 召唤宠物或随从时，选择合适的位置
 * - 生物组队时，排列成员位置
 * - 玩家聚集时，寻找站位
 */

#ifndef _OBJECT_POS_SELECTOR_H
#define _OBJECT_POS_SELECTOR_H

#include "Common.h"
#include <map>
#include <cmath>

/**
 * @enum UsedPosType
 * @brief 已占用位置类型枚举
 *
 * 用于区分正角度和负角度的位置，便于双向查找。
 */
enum UsedPosType { USED_POS_PLUS, USED_POS_MINUS };

/**
 * @brief 已占用位置类型取反操作符
 * @param uptype 位置类型
 * @return 相反的位置类型
 */
inline UsedPosType operator~(UsedPosType uptype)
{
    return uptype == USED_POS_PLUS ? USED_POS_MINUS : USED_POS_PLUS;
}

/**
 * @struct ObjectPosSelector
 * @brief 对象位置选择器
 *
 * 用于在指定距离周围寻找可用的空闲位置。
 * 通过维护已占用位置列表，按角度查找空闲位置。
 *
 * 算法思想：
 * 1. 以中心点为原点，在指定距离的圆周上查找位置
 * 2. 维护两个列表：正角度和负角度的已占用位置
 * 3. 从角度0开始，向两侧查找第一个足够大的空闲区域
 * 4. 返回可用位置的角度
 *
 * 使用示例：
 * @code
 * ObjectPosSelector selector(x, y, centralSize, searchDist);
 * selector.AddUsedPos(objSize1, angle1, dist1);
 * selector.AddUsedPos(objSize2, angle2, dist2);
 * selector.InitializeAngle();
 *
 * float angle;
 * if (selector.FirstAngle(angle))
 * {
 *     // 在angle方向找到了可用位置
 * }
 * @endcode
 */
struct TC_GAME_API ObjectPosSelector
{
    /**
     * @struct UsedPos
     * @brief 已占用位置信息
     */
    struct UsedPos
    {
        /**
         * @brief 构造函数
         * @param sign_ 角度符号（正或负）
         * @param size_ 对象大小
         * @param dist_ 到中心点的距离（包括中心对象大小）
         */
        UsedPos(float sign_, float size_, float dist_) : sign(sign_), size(size_), dist(dist_) { }

        float sign;  ///< 角度符号（1.0f为正角，-1.0f为负角）

        float size;  ///< 对象的大小（碰撞半径）
        float dist;  ///< 到中心点的距离（包括中心对象大小）
    };

    typedef std::multimap<float, UsedPos> UsedPosList;  ///< 已占用位置列表类型（按角度绝对值排序）

    /**
     * @brief 构造函数
     * @param x 中心点X坐标
     * @param y 中心点Y坐标
     * @param size 中心对象的大小
     * @param dist 搜索距离（包括中心对象大小）
     */
    ObjectPosSelector(float x, float y, float size, float dist);

    /**
     * @brief 添加一个已占用位置
     * @param size 对象大小
     * @param angle 角度（弧度）
     * @param dist 到中心点的距离
     */
    void AddUsedPos(float size, float angle, float dist);

    /**
     * @brief 初始化角度搜索
     *
     * 重置搜索迭代器，准备开始查找可用位置。
     */
    void InitializeAngle();

    /**
     * @brief 查找第一个可用角度
     * @param angle 输出参数，找到的可用角度
     * @return 如果找到返回true，否则返回false
     */
    bool FirstAngle(float& angle);

    /**
     * @brief 查找下一个可用角度
     * @param angle 输出参数，找到的可用角度
     * @return 如果找到返回true，否则返回false
     */
    bool NextAngle(float& angle);

    /**
     * @brief 查找下一个已占用角度
     * @param angle 输出参数，找到的已占用角度
     * @return 如果找到返回true，否则返回false
     */
    bool NextUsedAngle(float& angle);

    /**
     * @brief 查找下一个可能可用的角度
     * @param angle 输出参数，找到的角度
     * @return 如果找到返回true，否则返回false
     */
    bool NextPosibleAngle(float& angle);

    /**
     * @brief 检查指定角度是否可用
     * @param nextUsedPos 下一个已占用位置
     * @param sign 角度符号
     * @param angle 要检查的角度
     * @return 如果角度可用返回true
     */
    bool CheckAngle(UsedPosList::value_type const& nextUsedPos, float sign, float angle ) const
    {
        float angle_step2  = GetAngle(nextUsedPos.second);

        float next_angle = nextUsedPos.first;
        if (nextUsedPos.second.sign * sign < 0)                       // last node from diff. list (-pi+alpha)
            next_angle = 2 * float(M_PI) - next_angle;   // move to positive

        return std::fabs(angle) + angle_step2 <= next_angle;
    }

    /**
     * @brief 检查原始位置（角度0）是否可用
     * @return 如果原始位置可用返回true
     */
    bool CheckOriginal() const
    {
        return (m_UsedPosLists[USED_POS_PLUS].empty() || CheckAngle(*m_UsedPosLists[USED_POS_PLUS].begin(), 1.0f, 0)) &&
            (m_UsedPosLists[USED_POS_MINUS].empty() || CheckAngle(*m_UsedPosLists[USED_POS_MINUS].begin(), -1.0f, 0));
    }

    /**
     * @brief 检查位置列表是否不平衡
     * @return 如果两个列表一个为空一个不为空返回true
     */
    bool IsNonBalanced() const { return m_UsedPosLists[USED_POS_PLUS].empty() != m_UsedPosLists[USED_POS_MINUS].empty(); }

    /**
     * @brief 为指定已占用位置查找下一个可用角度
     * @param usedPos 已占用位置
     * @param sign 角度符号
     * @param uptype 位置类型
     * @param angle 输出参数，找到的角度
     * @return 如果找到返回true
     */
    bool NextAngleFor(UsedPosList::value_type const& usedPos, float sign, UsedPosType uptype, float &angle)
    {
        float angle_step  = GetAngle(usedPos.second);

        // next possible angle
        angle  = usedPos.first * usedPos.second.sign + angle_step * sign;

        UsedPosList::value_type const* nextNode = nextUsedPos(uptype);
        if (nextNode)
        {
            // if next node permit use selected angle, then do it
            if (!CheckAngle(*nextNode, sign, angle))
            {
                m_smallStepOk[uptype] = false;
                return false;
            }
        }

        // possible more points
        m_smallStepOk[uptype] = true;
        m_smallStepAngle[uptype] = angle;
        m_smallStepNextUsedPos[uptype] = nextNode;

        return true;
    }

    /**
     * @brief 以小步长查找下一个角度
     * @param sign 角度符号
     * @param uptype 位置类型
     * @param angle 输出参数，找到的角度
     * @return 如果找到返回true
     */
    bool NextSmallStepAngle(float sign, UsedPosType uptype, float &angle)
    {
        // next possible angle
        angle  = m_smallStepAngle[uptype] + m_anglestep * sign;

        if (std::fabs(angle) > float(M_PI))
        {
            m_smallStepOk[uptype] = false;
            return false;
        }

        if (m_smallStepNextUsedPos[uptype])
        {
            if (std::fabs(angle) >= m_smallStepNextUsedPos[uptype]->first)
            {
                m_smallStepOk[uptype] = false;
                return false;
            }

            // if next node permit use selected angle, then do it
            if (!CheckAngle(*m_smallStepNextUsedPos[uptype], sign, angle))
            {
                m_smallStepOk[uptype] = false;
                return false;
            }
        }

        // possible more points
        m_smallStepAngle[uptype] = angle;
        return true;
    }

    /**
     * @brief 获取下一个已占用位置
     * @param uptype 位置类型
     * @return 下一个已占用位置，如果没有则返回nullptr
     */
    UsedPosList::value_type const* nextUsedPos(UsedPosType uptype);

    /**
     * @brief 计算从已占用位置到下一个可能空闲位置的角度
     * @param usedPos 已占用位置
     * @return 角度步长（弧度）
     */
    float GetAngle(UsedPos const& usedPos) const { return std::acos(m_dist/(usedPos.dist+usedPos.size+m_size)); }

    float m_center_x;  ///< 中心点X坐标
    float m_center_y;  ///< 中心点Y坐标
    float m_size;      ///< 中心对象的大小
    float m_dist;      ///< 搜索距离（包括中心对象大小）
    float m_anglestep; ///< 角度步长，用于小步长搜索

    UsedPosList m_UsedPosLists[2];  ///< 已占用位置列表 [USED_POS_PLUS, USED_POS_MINUS]
    UsedPosList::const_iterator m_nextUsedPos[2];  ///< 当前搜索迭代器

    // 用于小步长搜索的字段
    float m_smallStepAngle[2];  ///< 小步长搜索的当前角度
    bool  m_smallStepOk[2];     ///< 小步长搜索是否可行
    UsedPosList::value_type const* m_smallStepNextUsedPos[2];  ///< 小步长搜索的下一个已占用位置
};
#endif
