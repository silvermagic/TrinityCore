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
 * @file TotemAI.h
 * @brief 图腾AI模块头文件
 *
 * 本文件定义了图腾AI类，用于控制萨满祭司图腾的行为。
 * 图腾是一种特殊的召唤单位，通常固定在原地执行特定功能。
 *
 * 主要特点：
 * - 继承自NullCreatureAI，提供基础的被动行为
 * - 自动选择攻击目标（主动攻击型图腾）
 * - 支持岗哨图腾的特殊行为
 * - 定期更新图腾的攻击逻辑
 */

#ifndef TRINITY_TOTEMAI_H
#define TRINITY_TOTEMAI_H

#include "CreatureAI.h"
#include "PassiveAI.h"
#include "Timer.h"

// 前向声明
class Creature;
class Totem;

/**
 * @brief 图腾AI类
 *
 * 专门用于控制图腾（Totem）行为的AI类。
 * 图腾是由萨满祭司召唤的固定位置单位，通常具有自动攻击或施法功能。
 * 此AI继承自NullCreatureAI，提供了图腾特有的行为逻辑，
 * 包括目标选择、自动攻击等功能。
 *
 * 主要功能：
 * - 自动选择攻击目标
 * - 定期更新图腾行为
 * - 管理图腾的攻击逻辑
 *
 * 图腾类型：
 * - 主动攻击型图腾（TOTEM_ACTIVE）：如火元素图腾、熔岩图腾等
 * - 被动型图腾（TOTEM_PASSIVE）：如治疗图腾、法力图腾等
 * - 岗哨图腾：特殊类型，可以侦察并提供视野
 *
 * 使用场景：
 * - 萨满祭司召唤的各种图腾
 * - 需要自动攻击行为的固定单位
 */
class TC_GAME_API TotemAI : public NullCreatureAI
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化图腾AI实例，设置初始受害者GUID为空
         *
         * @param creature 拥有此AI的生物对象（图腾）
         *
         * @note 使用断言确保传入的生物确实是图腾类型
         *       如果不是图腾类型，会在运行时触发断言失败
         */
        explicit TotemAI(Creature* creature);

        /**
         * @brief 开始攻击处理函数
         *
         * 当图腾被触发攻击时调用。当前仅处理岗哨图腾的特殊行为。
         *
         * @param victim 攻击目标（当前未使用）
         *
         * 主要流程：
         * 1. 检查是否为岗哨图腾（SENTRY_TOTEM_ENTRY）
         * 2. 如果是岗哨图腾，获取图腾的主人
         * 3. 如果主人是玩家，发送小地图雷达ping消息
         *    - 包含图腾的GUID和位置坐标
         *    - 玩家客户端会在小地图上显示ping标记
         *
         * @note 岗哨图腾在被攻击时会向主人发送位置提醒，
         *       让玩家知道有敌人在图腾附近
         */
        void AttackStart(Unit* victim) override;

        /**
         * @brief 更新AI逻辑
         *
         * 每个游戏周期调用的主更新函数。
         * 负责执行图腾的自动行为，如自动攻击、技能触发等。
         *
         * @param diff 自上次更新以来经过的时间（毫秒）
         *
         * 主要流程：
         * 1. 检查图腾类型：只有主动攻击型图腾才执行攻击逻辑
         * 2. 检查图腾状态：死亡或正在施法则跳过本次更新
         * 3. 获取图腾法术信息：从图腾对象获取其施放的法术
         * 4. 获取法术射程：确定攻击范围
         * 5. 查找目标：
         *    - 首先尝试使用缓存的受害者GUID
         *    - 如果缓存目标无效，则搜索新目标
         * 6. 施放法术：找到有效目标后，对目标施放图腾法术
         *
         * 性能优化：
         * - 使用GUID缓存目标，避免每次都重新搜索
         * - 只在目标无效时才执行完整的搜索
         *
         * @note 被动型图腾（如治疗图腾）不会执行攻击逻辑
         */
        void UpdateAI(uint32 diff) override;

        /**
         * @brief 检查AI是否适用于指定生物
         *
         * 静态函数，用于判断是否应该为此生物使用图腾AI。
         *
         * @param creature 要检查的生物对象
         * @return 返回优先级值：
         *         - PERMIT_BASE_PROACTIVE: 如果是图腾，允许使用此AI
         *         - PERMIT_BASE_NO: 如果不是图腾，不允许使用此AI
         *
         * 判断逻辑：
         * - IsTotem(): 检查生物是否为图腾类型
         * - 图腾是萨满祭司召唤的特殊单位，具有独特的行为模式
         */
        static int32 Permissible(Creature const* creature);

    private:
        ObjectGuid _victimGUID;  ///< 当前攻击目标的GUID（全局唯一标识符）
                                 ///< 用于缓存目标，避免每次更新都重新搜索
                                 ///< 当目标失效时会自动清除并重新搜索
};

#endif
