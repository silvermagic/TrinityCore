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
 * @file enuminfo_CreatureAI.cpp
 * @brief CreatureAI枚举类型工具实现文件
 *
 * 本文件提供了CreatureAI中定义的枚举类型的序列化和反序列化工具。
 * 主要用于命令行工具、调试输出和配置文件解析，将枚举值与字符串进行转换。
 * 该文件实现了CreatureAI::EvadeReason枚举的相关工具函数。
 */

#include "CreatureAI.h"
#include "Define.h"
#include "SmartEnum.h"
#include <stdexcept>

namespace Trinity::Impl::EnumUtilsImpl
{

/****************************************************************************\
|* data for enum 'CreatureAI::EvadeReason' in 'CreatureAI.h' auto-generated *|
\****************************************************************************/

/**
 * @brief 将EvadeReason枚举值转换为文本描述
 *
 * 该模板特化函数实现了将生物脱离战斗原因（EvadeReason）枚举值转换为
 * 可读的文本格式。返回的EnumText包含三个字段：
 * - 第一个字段：枚举常量名称（用于代码和配置）
 * - 第二个字段：显示名称（与常量名相同）
 * - 第三个字段：详细描述（用于文档和调试）
 *
 * @param value 需要转换的EvadeReason枚举值
 * @return 返回包含枚举名称、显示名称和描述的EnumText结构
 *
 * @调用时机 命令行工具输出、日志记录、配置文件序列化时调用
 * @性能注意事项 使用switch-case实现，效率较高
     * @throw std::out_of_range 当传入无效的枚举值时抛出异常
 *
 * @note 该函数通常由Trinity的枚举工具系统自动调用
 */
template <>
TC_API_EXPORT EnumText EnumUtils<CreatureAI::EvadeReason>::ToString(CreatureAI::EvadeReason value)
{
    switch (value)
    {
        // 无敌对目标：威胁列表为空
        case CreatureAI::EVADE_REASON_NO_HOSTILES:
            return { "EVADE_REASON_NO_HOSTILES", "EVADE_REASON_NO_HOSTILES", "the creature's threat list is empty" };

        // 脱离边界：生物移动到了脱离战斗边界之外
        case CreatureAI::EVADE_REASON_BOUNDARY:
            return { "EVADE_REASON_BOUNDARY", "EVADE_REASON_BOUNDARY", "the creature has moved outside its evade boundary" };

        // 无法到达目标：生物超过5秒无法到达其目标
        case CreatureAI::EVADE_REASON_NO_PATH:
            return { "EVADE_REASON_NO_PATH", "EVADE_REASON_NO_PATH", "the creature was unable to reach its target for over 5 seconds" };

        // 序列中断：首领的前置战斗尚未完成
        case CreatureAI::EVADE_REASON_SEQUENCE_BREAK:
            return { "EVADE_REASON_SEQUENCE_BREAK", "EVADE_REASON_SEQUENCE_BREAK", "this is a boss and the pre-requisite encounters for engaging it are not defeated yet" };

        // 其他原因：其他未分类的脱离战斗原因
        case CreatureAI::EVADE_REASON_OTHER:
            return { "EVADE_REASON_OTHER", "EVADE_REASON_OTHER", "anything else" };

        // 无效枚举值：抛出异常
        default:
            throw std::out_of_range("value");
    }
}

/**
 * @brief 获取EvadeReason枚举的元素总数
 *
 * 返回EvadeReason枚举定义的所有取值数量。
 * 该函数用于枚举迭代和边界检查。
 *
 * @return 返回枚举值的总数量（当前为5）
 *
 * @调用时机 枚举迭代、数组分配、边界验证时调用
 * @性能注意事项 内联函数，直接返回常量，性能开销极小
     */
template <>
TC_API_EXPORT size_t EnumUtils<CreatureAI::EvadeReason>::Count() { return 5; }

/**
 * @brief 将索引转换为EvadeReason枚举值
 *
 * 根据索引（0到Count()-1）返回对应的枚举值。
 * 该函数支持枚举的顺序迭代，通常与ToIndex配合使用。
 *
 * @param index 枚举索引（0到Count()-1范围内）
 * @return 返回对应索引的EvadeReason枚举值
 *
 * @调用时机 枚举迭代、配置文件解析时调用
 * @性能注意事项 使用switch-case实现，效率较高
     * @throw std::out_of_range 当索引超出有效范围时抛出异常
 *
 * @note 索引顺序与ToString中的case顺序一致
 */
template <>
TC_API_EXPORT CreatureAI::EvadeReason EnumUtils<CreatureAI::EvadeReason>::FromIndex(size_t index)
{
    switch (index)
    {
        case 0: return CreatureAI::EVADE_REASON_NO_HOSTILES;   // 索引0：无仇恨目标
        case 1: return CreatureAI::EVADE_REASON_BOUNDARY;      // 索引1：脱离边界
        case 2: return CreatureAI::EVADE_REASON_NO_PATH;       // 索引2：无法寻路
        case 3: return CreatureAI::EVADE_REASON_SEQUENCE_BREAK; // 索引3：序列中断
        case 4: return CreatureAI::EVADE_REASON_OTHER;         // 索引4：其他原因
        default: throw std::out_of_range("index");             // 无效索引
    }
}

/**
 * @brief 将EvadeReason枚举值转换为索引
 *
 * 将枚举值转换为对应的索引值（0到Count()-1）。
 * 该函数是FromIndex的逆操作，用于枚举值的序列化和快速查找。
 *
 * @param value 需要转换的EvadeReason枚举值
 * @return 返回对应的索引值（0到Count()-1范围内）
 *
 * @调用时机 枚举序列化、数组索引计算时调用
 * @性能注意事项 使用switch-case实现，效率较高
     * @throw std::out_of_range 当传入无效的枚举值时抛出异常
 *
 * @note 返回的索引值与FromIndex中的case顺序一致
 */
template <>
TC_API_EXPORT size_t EnumUtils<CreatureAI::EvadeReason>::ToIndex(CreatureAI::EvadeReason value)
{
    switch (value)
    {
        case CreatureAI::EVADE_REASON_NO_HOSTILES: return 0;    // 无仇恨目标 -> 索引0
        case CreatureAI::EVADE_REASON_BOUNDARY: return 1;       // 脱离边界 -> 索引1
        case CreatureAI::EVADE_REASON_NO_PATH: return 2;        // 无法寻路 -> 索引2
        case CreatureAI::EVADE_REASON_SEQUENCE_BREAK: return 3; // 序列中断 -> 索引3
        case CreatureAI::EVADE_REASON_OTHER: return 4;          // 其他原因 -> 索引4
        default: throw std::out_of_range("value");              // 无效枚举值
    }
}
}
