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
 * @file    boss_nerubenkan.cpp
 * @brief   奈幽布(Nerubenkan)BOSS脚本
 *
 * @details 本模块实现了斯坦索姆副本中的BOSS奈幽布的AI逻辑:
 *          - 奈幽布是一只亡灵蜘蛛类BOSS,位于亡灵侧的灵通2
 *          - 使用各种蜘蛛类技能:蜘蛛网缠绕、护甲穿刺、召唤穴居甲虫
 *          - 死亡后激活对应的水晶,允许玩家进入屠宰场
 *          - 属于斯坦索姆45分钟救援路线的关键BOSS之一
 *
 * @note BOSS完成度: 70%
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "stratholme.h"

/**
 * @brief 奈幽布使用的法术枚举
 */
enum Spells
{
    SPELL_ENCASINGWEBS          = 4962,  // 包围之网 - 定身目标30秒
    SPELL_PIERCEARMOR           = 6016,  // 护甲穿刺 - 降低目标护甲
    SPELL_CRYPT_SCARABS         = 31602, // 穴居甲虫 - 召唤甲虫攻击目标
    SPELL_RAISEUNDEADSCARAB     = 17235  // 复活亡灵甲虫 - 召唤亡灵甲虫助手
};

/**
 * @class boss_nerubenkan
 * @brief 奈幽布BOSS脚本类
 *
 * @details 实现奈幽布BOSS的AI脚本注册和AI逻辑
 */
class boss_nerubenkan : public CreatureScript
{
public:
    boss_nerubenkan() : CreatureScript("boss_nerubenkan") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物指针
     * @return AI实例指针
     *
     * @details 创建并返回奈幽布AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetStratholmeAI<boss_nerubenkanAI>(creature);
    }

    /**
     * @struct boss_nerubenkanAI
     * @brief 奈幽布AI实现
     *
     * @details 实现奈幽布的战斗AI:
     *          - 定期施放包围之网定身玩家
     *          - 护甲穿刺降低坦克护甲
     *          - 召唤穴居甲虫和亡灵甲虫助手
     *          - 死亡时更新副本进度
     */
    struct boss_nerubenkanAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物指针
         *
         * @details 初始化AI并获取副本实例脚本
         */
        boss_nerubenkanAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = me->GetInstanceScript();
        }

        /**
         * @brief 初始化成员变量
         *
         * @details 设置所有技能计时器的初始值:
         *          - 穴居甲虫: 3秒后首次施放
         *          - 包围之网: 7秒后首次施放
         *          - 护甲穿刺: 19秒后首次施放
         *          - 复活亡灵甲虫: 3秒后首次召唤
         */
        void Initialize()
        {
            CryptScarabs_Timer = 3000;         // 穴居甲虫计时器(毫秒)
            EncasingWebs_Timer = 7000;         // 包围之网计时器(毫秒)
            PierceArmor_Timer = 19000;         // 护甲穿刺计时器(毫秒)
            RaiseUndeadScarab_Timer = 3000;    // 复活亡灵甲虫计时器(毫秒)
        }

        InstanceScript* instance;              // 副本实例脚本指针

        uint32 EncasingWebs_Timer;             // 包围之网冷却计时器
        uint32 PierceArmor_Timer;              // 护甲穿刺冷却计时器
        uint32 CryptScarabs_Timer;             // 穴居甲虫冷却计时器
        uint32 RaiseUndeadScarab_Timer;        // 复活亡灵甲虫冷却计时器

        /**
         * @brief 重置AI状态
         *
         * @details 在战斗重置时调用,重置所有计时器
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 进入战斗
         * @param who 进入战斗的目标(未使用)
         *
         * @details 当BOSS进入战斗时调用,目前无特殊逻辑
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
        }

        /**
         * @brief 死亡处理
         * @param killer 击杀者(未使用)
         *
         * @details 当BOSS死亡时调用:
         *          - 设置副本的奈幽布状态为进行中(实际应为完成)
         *          - 允许副本更新屠宰场门的状态
         *
         * @note 水晶实现后应将状态改为DONE
         */
        void JustDied(Unit* /*killer*/) override
        {
            instance->SetData(TYPE_NERUB, IN_PROGRESS);
        }

        /**
         * @brief 召唤亡灵甲虫
         * @param victim 目标单位
         *
         * @details 在BOSS附近随机位置召唤一只亡灵甲虫:
         *          - 召唤位置: BOSS周围9码范围内随机位置
         *          - 存在时间: 180秒或直到尸体消失
         *          - 召唤后立即攻击目标
         *
         * @param victim 甲虫的攻击目标
         */
        void RaiseUndeadScarab(Unit* victim)
        {
            if (Creature* pUndeadScarab = DoSpawnCreature(10876, float(irand(-9, 9)), float(irand(-9, 9)), 0, 0, TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 180s))
                if (pUndeadScarab->AI())
                    pUndeadScarab->AI()->AttackStart(victim);
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * @details 主循环逻辑,每帧调用:
         *          1. 检查是否有战斗目标,无则返回
         *          2. 包围之网: 30秒冷却,定身目标
         *          3. 护甲穿刺: 35秒冷却,66%概率施放
         *          4. 穴居甲虫: 20秒冷却,对目标施放
         *          5. 复活亡灵甲虫: 16秒冷却,召唤助手
         *          6. 执行近战攻击
         *
         * @note 技能施放顺序和冷却时间是平衡性的关键
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            // EncasingWebs - 包围之网
            if (EncasingWebs_Timer <= diff)
            {
                DoCastVictim(SPELL_ENCASINGWEBS);
                EncasingWebs_Timer = 30000;  // 30秒冷却
            } else EncasingWebs_Timer -= diff;

            // PierceArmor - 护甲穿刺
            if (PierceArmor_Timer <= diff)
            {
                // 66%概率施放护甲穿刺
                if (urand(0, 3) < 2)
                    DoCastVictim(SPELL_PIERCEARMOR);
                PierceArmor_Timer = 35000;  // 35秒冷却
            } else PierceArmor_Timer -= diff;

            // CryptScarabs_Timer - 穴居甲虫
            if (CryptScarabs_Timer <= diff)
            {
                DoCastVictim(SPELL_CRYPT_SCARABS);
                CryptScarabs_Timer = 20000;  // 20秒冷却
            } else CryptScarabs_Timer -= diff;

            // RaiseUndeadScarab - 复活亡灵甲虫
            if (RaiseUndeadScarab_Timer <= diff)
            {
                RaiseUndeadScarab(me->GetVictim());
                RaiseUndeadScarab_Timer = 16000;  // 16秒冷却
            } else RaiseUndeadScarab_Timer -= diff;

            DoMeleeAttackIfReady();
        }
    };

};

/**
 * @brief 注册脚本
 *
 * @details 将奈幽布BOSS脚本注册到脚本系统
 */
void AddSC_boss_nerubenkan()
{
    new boss_nerubenkan();
}
