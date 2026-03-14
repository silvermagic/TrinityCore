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
 * @file boss_princess_theradras.cpp
 * @brief 玛拉顿副本最终BOSS - 公主瑟莱德拉斯
 *
 * 该模块实现了公主瑟莱德拉斯的AI行为,包括:
 * - 使用尘埃领域降低周围敌人的命中率
 * - 使用巨石技能对随机目标造成伤害
 * - 使用痛击增加近战攻击次数
 * - 使用排斥凝视恐惧当前目标
 * - 死亡后召唤扎里塔娜(任务NPC)
 *
 * 公主瑟莱德拉斯是玛拉顿的最终BOSS,是石母瑟莱德拉斯的女儿。
 */

/* ScriptData
SDName: Boss_Princess_Theradras
SD%Complete: 100
SDComment:
SDCategory: Maraudon
EndScriptData */

#include "ScriptMgr.h"
#include "maraudon.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 *
 * 定义公主瑟莱德拉斯使用的所有法术
 */
enum Spells
{
    SPELL_DUSTFIELD             = 21909,   ///< 尘埃领域 - 降低周围敌人的命中率
    SPELL_BOULDER               = 21832,   ///< 巨石 - 对目标造成物理伤害
    SPELL_THRASH                = 3391,    ///< 痛击 - 增加近战攻击次数
    SPELL_REPULSIVEGAZE         = 21869    ///< 排斥凝视 - 恐惧目标
};

/**
 * @brief 公主瑟莱德拉斯脚本类
 *
 * 继承自 CreatureScript,负责注册和管理BOSS的AI
 */
class boss_princess_theradras : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称为 "boss_princess_theradras"
     */
    boss_princess_theradras() : CreatureScript("boss_princess_theradras") { }

    /**
     * @brief 获取AI实例
     * @param creature 需要AI的生物对象
     * @return 返回公主瑟莱德拉斯AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetMaraudonAI<boss_ptheradrasAI>(creature);
    }

    /**
     * @brief 公主瑟莱德拉斯AI结构体
     *
     * 实现BOSS的战斗逻辑,包括多种法术和技能的组合使用
     */
    struct boss_ptheradrasAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature AI控制的生物对象
         */
        boss_ptheradrasAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化计时器
         *
         * 重置所有技能冷却计时器到初始值
         */
        void Initialize()
        {
            DustfieldTimer = 8000;       // 尘埃领域初始冷却8秒
            BoulderTimer = 2000;         // 巨石初始冷却2秒
            ThrashTimer = 5000;          // 痛击初始冷却5秒
            RepulsiveGazeTimer = 23000;  // 排斥凝视初始冷却23秒
        }

        uint32 DustfieldTimer;       ///< 尘埃领域冷却计时器(毫秒)
        uint32 BoulderTimer;         ///< 巨石冷却计时器(毫秒)
        uint32 ThrashTimer;          ///< 痛击冷却计时器(毫秒)
        uint32 RepulsiveGazeTimer;   ///< 排斥凝视冷却计时器(毫秒)

        /**
         * @brief 重置事件
         *
         * 当BOSS脱离战斗时调用,重置所有计时器和状态
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 进入战斗事件
         * @param who 进入战斗的目标(未使用)
         *
         * 当BOSS被玩家攻击时触发
         */
        void JustEngagedWith(Unit* /*who*/) override { }

        /**
         * @brief 死亡事件
         * @param killer 击杀者(未使用)
         *
         * BOSS死亡时召唤扎里塔娜(NPC ID: 12238)
         * 扎里塔娜是任务NPC,提供任务完成奖励
         * 她会在10分钟后自动消失
         *
         * @note 召唤位置固定在 (28.1887, 62.3964, -123.161)
         */
        void JustDied(Unit* /*killer*/) override
        {
            me->SummonCreature(12238, 28.1887f, 62.3964f, -123.161f, 4.31096f, TEMPSUMMON_TIMED_DESPAWN, 10min);
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间间隔(毫秒)
         *
         * 每帧调用,处理BOSS的战斗行为:
         * - 检查是否有有效目标
         * - 更新并施放尘埃领域(自身范围效果)
         * - 更新并施放巨石(随机目标)
         * - 更新并施放排斥凝视(当前目标)
         * - 更新并施放痛击(自身增益)
         * - 执行近战攻击
         *
         * @note 这是一个拥有多种技能的BOSS,需要合理管理技能冷却
         */
        void UpdateAI(uint32 diff) override
        {
            // 检查是否有有效战斗目标
            if (!UpdateVictim())
                return;

            // 尘埃领域逻辑 - 每14秒施放一次
            if (DustfieldTimer <= diff)
            {
                // 对自身施放尘埃领域,影响周围敌人
                DoCast(me, SPELL_DUSTFIELD);
                DustfieldTimer = 14000;
            }
            else DustfieldTimer -= diff;

            // 巨石逻辑 - 每10秒施放一次
            if (BoulderTimer <= diff)
            {
                // 选择随机目标施放巨石
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    DoCast(target, SPELL_BOULDER);
                BoulderTimer = 10000;
            }
            else BoulderTimer -= diff;

            // 排斥凝视逻辑 - 每20秒施放一次
            if (RepulsiveGazeTimer <= diff)
            {
                // 对当前目标施放排斥凝视,使其恐惧
                DoCastVictim(SPELL_REPULSIVEGAZE);
                RepulsiveGazeTimer = 20000;
            }
            else RepulsiveGazeTimer -= diff;

            // 痛击逻辑 - 每18秒施放一次
            if (ThrashTimer <= diff)
            {
                // 对自身施放痛击,增加近战攻击次数
                DoCast(me, SPELL_THRASH);
                ThrashTimer = 18000;
            }
            else ThrashTimer -= diff;

            // 如果近战攻击准备就绪,执行近战攻击
            DoMeleeAttackIfReady();
        }
    };
};

/**
 * @brief 注册公主瑟莱德拉斯脚本
 *
 * 该函数由脚本加载器调用,用于将BOSS脚本注册到系统中
 */
void AddSC_boss_ptheradras()
{
    new boss_princess_theradras();
}
