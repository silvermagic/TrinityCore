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
 * @file boss_noxxion.cpp
 * @brief 玛拉顿副本BOSS - 诺克赛恩
 *
 * 该模块实现了诺克赛恩的AI行为,包括:
 * - 使用毒素齐射对多个目标造成自然伤害
 * - 使用上勾拳对当前目标造成物理伤害
 * - 定期进入隐形状态并召唤5个小怪
 * - 隐形持续15秒后重新出现
 *
 * 诺克赛恩是一个毒素元素生物,守护着玛拉顿的毒素区域。
 */

/* ScriptData
SDName: Boss_Noxxion
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
 * 定义诺克赛恩使用的所有法术
 */
enum Spells
{
    SPELL_TOXICVOLLEY           = 21687,   ///< 毒素齐射 - 对范围内敌人造成自然伤害
    SPELL_UPPERCUT              = 22916    ///< 上勾拳 - 强力近战攻击
};

/**
 * @brief 诺克赛恩脚本类
 *
 * 继承自 CreatureScript,负责注册和管理BOSS的AI
 */
class boss_noxxion : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称为 "boss_noxxion"
     */
    boss_noxxion() : CreatureScript("boss_noxxion") { }

    /**
     * @brief 获取AI实例
     * @param creature 需要AI的生物对象
     * @return 返回诺克赛恩AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetMaraudonAI<boss_noxxionAI>(creature);
    }

    /**
     * @brief 诺克赛恩AI结构体
     *
     * 实现BOSS的战斗逻辑,包括法术施放和隐形召唤机制
     */
    struct boss_noxxionAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature AI控制的生物对象
         */
        boss_noxxionAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化计时器和状态
         *
         * 重置所有技能冷却计时器和隐形状态
         */
        void Initialize()
        {
            ToxicVolleyTimer = 7000;      // 毒素齐射初始冷却7秒
            UppercutTimer = 16000;        // 上勾拳初始冷却16秒
            AddsTimer = 19000;            // 召唤小怪初始冷却19秒
            InvisibleTimer = 15000;       // 隐形持续时间15秒
            Invisible = false;            // 隐形状态标志
        }

        uint32 ToxicVolleyTimer;   ///< 毒素齐射冷却计时器(毫秒)
        uint32 UppercutTimer;      ///< 上勾拳冷却计时器(毫秒)
        uint32 AddsTimer;          ///< 召唤小怪冷却计时器(毫秒)
        uint32 InvisibleTimer;     ///< 隐形持续时间计时器(毫秒)
        bool Invisible;            ///< 当前是否处于隐形状态

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
         * @brief 召唤小怪
         * @param victim 攻击目标
         *
         * 在BOSS周围随机位置召唤一个小怪(NPC ID: 13456)
         * 小怪会在90秒后或死亡时消失
         *
         * @note 小怪召唤位置在BOSS周围7码范围内随机生成
         */
        void SummonAdds(Unit* victim)
        {
            if (Creature* Add = DoSpawnCreature(13456, float(irand(-7, 7)), float(irand(-7, 7)), 0, 0, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 90s))
                Add->AI()->AttackStart(victim);
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间间隔(毫秒)
         *
         * 每帧调用,处理BOSS的战斗行为:
         * - 处理隐形状态下的逻辑(等待15秒后重新出现)
         * - 隐形状态下不执行任何其他操作
         * - 检查是否有有效目标
         * - 更新并施放毒素齐射(当前目标)
         * - 更新并施放上勾拳(当前目标)
         * - 定期进入隐形状态并召唤5个小怪
         * - 执行近战攻击
         *
         * @note 隐形机制:
         *       1. BOSS设置为友好阵营,玩家无法攻击
         *       2. 设置为不可交互状态
         *       3. 更换为隐形模型
         *       4. 召唤5个小怪攻击玩家
         *       5. 15秒后恢复原状
         */
        void UpdateAI(uint32 diff) override
        {
            // 处理隐形状态下的恢复逻辑
            if (Invisible && InvisibleTimer <= diff)
            {
                // 重新变为可见状态
                me->SetFaction(FACTION_MONSTER);                    // 恢复怪物阵营
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);       // 移除不可交互标志
                me->SetDisplayId(11172);                            // 恢复诺克赛恩模型
                Invisible = false;
            }
            else if (Invisible)
            {
                // 隐形状态下,只更新计时器,不执行其他操作
                InvisibleTimer -= diff;
                return;
            }

            // 检查是否有有效战斗目标
            if (!UpdateVictim())
                return;

            // 毒素齐射逻辑 - 每9秒施放一次
            if (ToxicVolleyTimer <= diff)
            {
                DoCastVictim(SPELL_TOXICVOLLEY);
                ToxicVolleyTimer = 9000;
            }
            else ToxicVolleyTimer -= diff;

            // 上勾拳逻辑 - 每12秒施放一次
            if (UppercutTimer <= diff)
            {
                DoCastVictim(SPELL_UPPERCUT);
                UppercutTimer = 12000;
            }
            else UppercutTimer -= diff;

            // 召唤小怪逻辑 - 每40秒进入隐形状态并召唤小怪
            if (!Invisible && AddsTimer <= diff)
            {
                // 打断当前施法
                me->InterruptNonMeleeSpells(false);
                // 设置为友好阵营,使玩家无法攻击
                me->SetFaction(FACTION_FRIENDLY);
                // 设置为不可交互
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                // 更换为隐形模型
                me->SetDisplayId(11686);

                // 召唤5个小怪攻击当前目标
                SummonAdds(me->GetVictim());
                SummonAdds(me->GetVictim());
                SummonAdds(me->GetVictim());
                SummonAdds(me->GetVictim());
                SummonAdds(me->GetVictim());

                // 进入隐形状态
                Invisible = true;
                InvisibleTimer = 15000;

                // 设置下次召唤时间
                AddsTimer = 40000;
            }
            else AddsTimer -= diff;

            // 如果近战攻击准备就绪,执行近战攻击
            DoMeleeAttackIfReady();
        }
    };
};

/**
 * @brief 注册诺克赛恩脚本
 *
 * 该函数由脚本加载器调用,用于将BOSS脚本注册到系统中
 */
void AddSC_boss_noxxion()
{
    new boss_noxxion();
}
