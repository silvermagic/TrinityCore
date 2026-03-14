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
 * @file GuardAI.h
 *
 * @brief 守卫AI模块
 *
 * 本文件定义了守卫AI类，用于控制游戏中的守卫类型生物。
 *
 * 守卫AI的特点：
 * - 主动攻击敌对目标
 * - 始终能看到正在与之交战的玩家控制单位（防止通过隐身脱战）
 * - 死亡时向玩家发送区域被攻击的消息
 * - 脱离战斗后返回出生点
 *
 * 典型应用场景：
 * - 主城守卫
 * - 阵营哨兵
 * - 特殊区域保护者
 */

#ifndef TRINITY_GUARDAI_H
#define TRINITY_GUARDAI_H

#include "ScriptedCreature.h"

class Creature;

/**
 * @brief 守卫AI类
 *
 * 守卫AI是专门为守卫类型生物设计的AI系统。
 * 守卫通常会主动攻击敌对目标，在死亡后会向玩家发送区域被攻击的消息，
 * 在脱离战斗后会返回初始位置。
 *
 * 主要特点：
 * - 主动攻击敌对目标
 * - 总是能看到正在与自己交战的玩家控制单位
 * - 脱离战斗后返回出生点
 * - 死亡时发送区域被攻击消息
 */
class TC_GAME_API GuardAI : public ScriptedAI
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化守卫AI实例
         *
         * @param creature 守卫生物对象指针
         */
        explicit GuardAI(Creature* creature);

        /**
         * @brief 检查生物是否可以使用此AI
         *
         * 判断给定生物是否适合使用守卫AI。
         * 只有被标记为守卫类型(IsGuard)的生物才会被允许使用此AI。
         *
         * @param creature 要检查的生物对象
         * @return 如果生物是守卫类型返回PERMIT_BASE_PROACTIVE，否则返回PERMIT_BASE_NO
         */
        static int32 Permissible(Creature const* creature);

        /**
         * @brief 更新AI逻辑
         *
         * 每帧调用，执行守卫的战斗逻辑。
         * 如果有攻击目标，则执行近战攻击。
         *
         * @param diff 距离上次更新的时间间隔（毫秒）
         */
        void UpdateAI(uint32 diff) override;

        /**
         * @brief 检查是否总是能看到目标
         *
         * 判断守卫是否应该始终能看到某个世界对象。
         * 对于正在与守卫交战的玩家控制单位，守卫总是能看到他们，
         * 这样可以防止玩家通过隐身等方式脱离战斗。
         *
         * @param obj 要检查的世界对象
         * @return 如果守卫应该始终能看到该对象返回true，否则返回false
         */
        bool CanSeeAlways(WorldObject const* obj) override;

        /**
         * @brief 进入撤退模式
         *
         * 当守卫脱离战斗时调用。
         * 执行以下操作：
         * - 移除所有光环效果
         * - 停止战斗
         * - 结束交战状态
         * - 返回初始位置（出生点）
         *
         * @param why 撤退原因
         */
        void EnterEvadeMode(EvadeReason /*why*/) override;

        /**
         * @brief 守卫死亡时调用
         *
         * 当守卫被杀死时触发。
         * 如果击杀者是玩家或玩家控制的单位，则向该玩家发送区域被攻击的消息。
         *
         * @param killer 击杀者单位
         */
        void JustDied(Unit* killer) override;
};
#endif
