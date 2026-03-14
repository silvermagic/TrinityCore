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
 * @file    boss_timmy_the_cruel.cpp
 * @brief   残忍的提米(Timmy the Cruel)BOSS脚本
 *
 * @details 本模块实现了斯坦索姆副本中的隐藏BOSS残忍的提米的AI逻辑:
 *          - 提米是血色侧入口区域的隐藏BOSS
 *          - 在血色侧入口前击杀一定数量的血色十字军后刷新
 *          - 进入战斗时会大喊"提米!"
 *          - 使用饥饿之爪技能攻击玩家
 *          - 属于血色侧的隐藏挑战BOSS
 *
 * @note BOSS完成度: 100%
 *       提米的名字来源于魔兽争霸3中的经典角色
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "stratholme.h"

/**
 * @brief 提米的台词枚举
 */
enum Says
{
    SAY_SPAWN                   = 0   // 刷新时的台词 - "提米!"
};

/**
 * @brief 提米使用的法术枚举
 */
enum Spells
{
    SPELL_RAVENOUSCLAW          = 17470  // 饥饿之爪 - 造成物理伤害
};

/**
 * @class boss_timmy_the_cruel
 * @brief 残忍的提米BOSS脚本类
 *
 * @details 实现提米BOSS的AI脚本注册和AI逻辑
 */
class boss_timmy_the_cruel : public CreatureScript
{
public:
    boss_timmy_the_cruel() : CreatureScript("boss_timmy_the_cruel") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物指针
     * @return AI实例指针
     *
     * @details 创建并返回提米AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetStratholmeAI<boss_timmy_the_cruelAI>(creature);
    }

    /**
     * @struct boss_timmy_the_cruelAI
     * @brief 残忍的提米AI实现
     *
     * @details 实现提米的战斗AI:
     *          - 首次进入战斗时大喊台词
     *          - 定期施放饥饿之爪技能
     *          - 属于简单型BOSS,主要依靠基础攻击
     */
    struct boss_timmy_the_cruelAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物指针
         *
         * @details 初始化AI
         */
        boss_timmy_the_cruelAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         *
         * @details 设置技能计时器和喊话状态:
         *          - 饥饿之爪: 10秒后首次施放
         *          - 是否已喊话: 未喊话
         */
        void Initialize()
        {
            RavenousClaw_Timer = 10000;  // 饥饿之爪计时器(毫秒)
            HasYelled = false;           // 是否已喊话标志
        }

        uint32 RavenousClaw_Timer;  // 饥饿之爪冷却计时器
        bool HasYelled;             // 是否已喊话(避免重复喊话)

        /**
         * @brief 重置AI状态
         *
         * @details 在战斗重置时调用,重置计时器和喊话状态
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 进入战斗
         * @param who 进入战斗的目标(未使用)
         *
         * @details 当BOSS进入战斗时调用:
         *          - 首次进入战斗时大喊"提米!"
         *          - 设置已喊话标志避免重复喊话
         *
         * @note 提米的喊话是经典场景,致敬魔兽争霸3
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            if (!HasYelled)
            {
                Talk(SAY_SPAWN);
                HasYelled = true;
            }
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * @details 主循环逻辑,每帧调用:
         *          1. 检查是否有战斗目标,无则返回
         *          2. 饥饿之爪: 15秒冷却,对目标施放
         *          3. 执行近战攻击
         *
         * @note 提米的AI非常简单,主要依靠基础攻击
         */
        void UpdateAI(uint32 diff) override
        {
            //Return since we have no target
            // 没有目标则返回
            if (!UpdateVictim())
                return;

            //RavenousClaw - 饥饿之爪
            if (RavenousClaw_Timer <= diff)
            {
                //Cast - 施放饥饿之爪
                DoCastVictim(SPELL_RAVENOUSCLAW);
                //15 seconds until we should cast this again
                // 15秒后再次施放
                RavenousClaw_Timer = 15000;
            } else RavenousClaw_Timer -= diff;

            DoMeleeAttackIfReady();
        }
    };

};

/**
 * @brief 注册脚本
 *
 * @details 将残忍的提米BOSS脚本注册到脚本系统
 */
void AddSC_boss_timmy_the_cruel()
{
    new boss_timmy_the_cruel();
}
