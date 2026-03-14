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
 * @file boss_celebras_the_cursed.cpp
 * @brief 玛拉顿副本BOSS - 被诅咒的塞雷布拉斯
 *
 * 该模块实现了被诅咒的塞雷布拉斯的AI行为,包括:
 * - 使用愤怒法术攻击随机目标
 * - 使用纠缠根须控制当前目标
 * - 使用腐化力量增强自身
 * - 死亡后召唤塞雷布拉斯的灵魂事件
 *
 * 被诅咒的塞雷布拉斯是塞雷布拉斯的堕落形态,
 * 玩家击败他后会净化他的灵魂,从而获得塞雷布拉斯的祝福。
 */

/* ScriptData
SDName: Boss_Celebras_the_Cursed
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
 * 定义被诅咒的塞雷布拉斯使用的所有法术
 */
enum Spells
{
    SPELL_WRATH                 = 21807,   ///< 愤怒 - 自然伤害法术
    SPELL_ENTANGLINGROOTS       = 12747,   ///< 纠缠根须 - 定身目标
    SPELL_CORRUPT_FORCES        = 21968    ///< 腐化力量 - 自身增益效果
};

/**
 * @brief 被诅咒的塞雷布拉斯脚本类
 *
 * 继承自 CreatureScript,负责注册和管理BOSS的AI
 */
class celebras_the_cursed : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称为 "celebras_the_cursed"
     */
    celebras_the_cursed() : CreatureScript("celebras_the_cursed") { }

    /**
     * @brief 获取AI实例
     * @param creature 需要AI的生物对象
     * @return 返回被诅咒的塞雷布拉斯AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetMaraudonAI<celebras_the_cursedAI>(creature);
    }

    /**
     * @brief 被诅咒的塞雷布拉斯AI结构体
     *
     * 实现BOSS的战斗逻辑,包括法术施放和时间管理
     */
    struct celebras_the_cursedAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature AI控制的生物对象
         */
        celebras_the_cursedAI(Creature* creature) : ScriptedAI(creature)
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
            WrathTimer = 8000;              // 愤怒法术初始冷却8秒
            EntanglingRootsTimer = 2000;    // 纠缠根须初始冷却2秒
            CorruptForcesTimer = 30000;     // 腐化力量初始冷却30秒
        }

        uint32 WrathTimer;              ///< 愤怒法术冷却计时器(毫秒)
        uint32 EntanglingRootsTimer;    ///< 纠缠根须冷却计时器(毫秒)
        uint32 CorruptForcesTimer;      ///< 腐化力量冷却计时器(毫秒)

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
         * BOSS死亡时召唤塞雷布拉斯的灵魂(NPC ID: 13716)
         * 灵魂会在10分钟后自动消失,用于触发后续剧情
         */
        void JustDied(Unit* /*killer*/) override
        {
            me->SummonCreature(13716, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN, 10min);
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间间隔(毫秒)
         *
         * 每帧调用,处理BOSS的战斗行为:
         * - 检查是否有有效目标
         * - 更新并施放愤怒法术(随机目标)
         * - 更新并施放纠缠根须(当前目标)
         * - 更新并施放腐化力量(自身增益)
         * - 执行近战攻击
         *
         * @note 腐化力量施放时会打断其他非近战法术
         */
        void UpdateAI(uint32 diff) override
        {
            // 检查是否有有效战斗目标
            if (!UpdateVictim())
                return;

            // 愤怒法术逻辑 - 每8秒施放一次
            if (WrathTimer <= diff)
            {
                // 选择随机目标施放愤怒
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    DoCast(target, SPELL_WRATH);
                WrathTimer = 8000;
            }
            else WrathTimer -= diff;

            // 纠缠根须逻辑 - 每20秒施放一次
            if (EntanglingRootsTimer <= diff)
            {
                // 对当前目标施放纠缠根须
                DoCastVictim(SPELL_ENTANGLINGROOTS);
                EntanglingRootsTimer = 20000;
            }
            else EntanglingRootsTimer -= diff;

            // 腐化力量逻辑 - 每20秒施放一次
            if (CorruptForcesTimer <= diff)
            {
                // 打断当前施法后施放腐化力量
                me->InterruptNonMeleeSpells(false);
                DoCast(me, SPELL_CORRUPT_FORCES);
                CorruptForcesTimer = 20000;
            }
            else CorruptForcesTimer -= diff;

            // 如果近战攻击准备就绪,执行近战攻击
            DoMeleeAttackIfReady();
        }
    };
};

/**
 * @brief 注册被诅咒的塞雷布拉斯脚本
 *
 * 该函数由脚本加载器调用,用于将BOSS脚本注册到系统中
 */
void AddSC_boss_celebras_the_cursed()
{
    new celebras_the_cursed();
}
