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
 * @file boss_landslide.cpp
 * @brief 玛拉顿副本BOSS - 兰斯利德
 *
 * 该模块实现了兰斯利德的AI行为,包括:
 * - 使用击退技能击飞当前目标
 * - 使用践踏技能造成范围伤害
 * - 生命值低于50%时使用山崩技能增强攻击
 *
 * 兰斯利德是一个巨大的元素生物,守护着玛拉顿的深处。
 */

/* ScriptData
SDName: Boss_Landslide
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
 * 定义兰斯利德使用的所有法术
 */
enum Spells
{
    SPELL_KNOCKAWAY         = 18670,   ///< 击退 - 击飞目标并造成伤害
    SPELL_TRAMPLE           = 5568,    ///< 践踏 - 对周围敌人造成伤害
    SPELL_LANDSLIDE         = 21808    ///< 山崩 - 强力增益效果
};

/**
 * @brief 兰斯利德脚本类
 *
 * 继承自 CreatureScript,负责注册和管理BOSS的AI
 */
class boss_landslide : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称为 "boss_landslide"
     */
    boss_landslide() : CreatureScript("boss_landslide") { }

    /**
     * @brief 获取AI实例
     * @param creature 需要AI的生物对象
     * @return 返回兰斯利德AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetMaraudonAI<boss_landslideAI>(creature);
    }

    /**
     * @brief 兰斯利德AI结构体
     *
     * 实现BOSS的战斗逻辑,包括技能施放和生命值触发机制
     */
    struct boss_landslideAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature AI控制的生物对象
         */
        boss_landslideAI(Creature* creature) : ScriptedAI(creature)
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
            KnockAwayTimer = 8000;   // 击退初始冷却8秒
            TrampleTimer = 2000;     // 践踏初始冷却2秒
            LandslideTimer = 0;      // 山崩初始冷却0秒(立即施放)
        }

        uint32 KnockAwayTimer;   ///< 击退技能冷却计时器(毫秒)
        uint32 TrampleTimer;     ///< 践踏技能冷却计时器(毫秒)
        uint32 LandslideTimer;   ///< 山崩技能冷却计时器(毫秒)

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
        void JustEngagedWith(Unit* /*who*/) override
        {
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间间隔(毫秒)
         *
         * 每帧调用,处理BOSS的战斗行为:
         * - 检查是否有有效目标
         * - 更新并施放击退技能(当前目标)
         * - 更新并施放践踏技能(自身范围)
         * - 当生命值低于50%时施放山崩技能
         * - 执行近战攻击
         *
         * @note 山崩技能施放时会打断其他非近战法术
         * @note 山崩技能冷却时间较长(60秒),是一次性增强效果
         */
        void UpdateAI(uint32 diff) override
        {
            // 检查是否有有效战斗目标
            if (!UpdateVictim())
                return;

            // 击退技能逻辑 - 每15秒施放一次
            if (KnockAwayTimer <= diff)
            {
                DoCastVictim(SPELL_KNOCKAWAY);
                KnockAwayTimer = 15000;
            }
            else KnockAwayTimer -= diff;

            // 践踏技能逻辑 - 每8秒施放一次
            if (TrampleTimer <= diff)
            {
                // 对自身施放践踏,影响周围敌人
                DoCast(me, SPELL_TRAMPLE);
                TrampleTimer = 8000;
            }
            else TrampleTimer -= diff;

            // 山崩技能逻辑 - 生命值低于50%时触发
            if (HealthBelowPct(50))
            {
                if (LandslideTimer <= diff)
                {
                    // 打断当前施法后施放山崩
                    me->InterruptNonMeleeSpells(false);
                    DoCast(me, SPELL_LANDSLIDE);
                    LandslideTimer = 60000;  // 60秒冷却
                }
                else LandslideTimer -= diff;
            }

            // 如果近战攻击准备就绪,执行近战攻击
            DoMeleeAttackIfReady();
        }
    };
};

/**
 * @brief 注册兰斯利德脚本
 *
 * 该函数由脚本加载器调用,用于将BOSS脚本注册到系统中
 */
void AddSC_boss_landslide()
{
    new boss_landslide();
}
