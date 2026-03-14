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
 * @file    boss_magistrate_barthilas.cpp
 * @brief   斯坦索姆副本 - 巴希拉斯法官Boss战斗AI脚本
 *
 * @details 本模块实现了斯坦索姆副本巴希拉斯法官的战斗逻辑:
 *          - 使用狂怒、吸取打击、群体殴打、重击等技能
 *          - 死亡时从亡灵形态变回人类形态
 *          - 战斗中会不断叠加愤怒增益
 *          - 斯坦索姆重要的亡灵Boss之一
 *
 * @note 脚本完成度: 70%
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "stratholme.h"

/**
 * @brief 法术ID枚举
 * 定义巴希拉斯法官使用的所有法术技能ID
 */
enum Spells
{
    SPELL_DRAININGBLOW      = 16793,  // 吸取打击 - 造成伤害并治疗自己
    SPELL_CROWDPUMMEL       = 10887,  // 群体殴打 - 范围攻击,造成眩晕
    SPELL_MIGHTYBLOW        = 14099,  // 重击 - 强力物理攻击
    SPELL_FURIOUS_ANGER     = 16791   // 狂怒 - 增加攻击强度
};

/**
 * @brief 模型ID枚举
 * 定义巴希拉斯法官的不同形态模型ID
 */
enum Models
{
    MODEL_NORMAL            = 10433,  // 亡灵形态模型ID
    MODEL_HUMAN             = 3637    // 人类形态模型ID(死亡后显示)
};

/**
 * @class boss_magistrate_barthilas
 * @brief 巴希拉斯法官Boss脚本类
 *
 * @details 实现巴希拉斯法官的脚本注册和AI创建
 */
class boss_magistrate_barthilas : public CreatureScript
{
public:
    boss_magistrate_barthilas() : CreatureScript("boss_magistrate_barthilas") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 创建的AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetStratholmeAI<boss_magistrate_barthilasAI>(creature);
    }

    /**
     * @struct boss_magistrate_barthilasAI
     * @brief 巴希拉斯法官Boss AI
     *
     * @details 实现巴希拉斯法官的战斗逻辑:
     *          - 使用多种近战技能
     *          - 战斗中持续叠加狂怒增益(最多25层)
     *          - 死亡时变回人类形态
     */
    struct boss_magistrate_barthilasAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_magistrate_barthilasAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         *
         * @details 设置所有技能计时器和状态变量的初始值:
         *          - 吸取打击: 20秒后首次施放
         *          - 群体殴打: 15秒后首次施放
         *          - 重击: 10秒后首次施放
         *          - 狂怒: 5秒后首次施放
         *          - 狂怒叠加次数: 0
         */
        void Initialize()
        {
            DrainingBlow_Timer = 20000;
            CrowdPummel_Timer = 15000;
            MightyBlow_Timer = 10000;
            FuriousAnger_Timer = 5000;
            AngerCount = 0;
        }

        // 技能计时器
        uint32 DrainingBlow_Timer;    // 吸取打击冷却计时器
        uint32 CrowdPummel_Timer;     // 群体殴打冷却计时器
        uint32 MightyBlow_Timer;      // 重击冷却计时器
        uint32 FuriousAnger_Timer;    // 狂怒冷却计时器
        uint32 AngerCount;            // 狂怒叠加次数计数器

        /**
         * @brief 重置Boss状态
         *
         * @details 在战斗结束或重置时调用:
         *          - 重新初始化所有计时器和状态
         *          - 根据Boss存活状态设置模型:
         *            * 存活: 显示亡灵形态
         *            * 死亡: 显示人类形态
         */
        void Reset() override
        {
            Initialize();

            // 设置正确的显示模型
            if (me->IsAlive())
                me->SetDisplayId(MODEL_NORMAL);   // 存活时显示亡灵形态
            else
                me->SetDisplayId(MODEL_HUMAN);    // 死亡时显示人类形态
        }

        /**
         * @brief 视线检测
         * @param who 进入视线范围的对象
         *
         * @details 当单位进入视线范围时调用(目前无特殊逻辑)
         *
         * @note 可能需要在此添加主动攻击逻辑
         */
        void MoveInLineOfSight(Unit* who) override

        {
            //nothing to see here yet
            // 目前无特殊处理

            ScriptedAI::MoveInLineOfSight(who);
        }

        /**
         * @brief Boss死亡处理
         * @param killer 击杀者(未使用)
         *
         * @details Boss死亡时调用:
         *          - 将模型变回人类形态,显示其真实身份
         */
        void JustDied(Unit* /*killer*/) override
        {
            me->SetDisplayId(MODEL_HUMAN);
        }

        /**
         * @brief 进入战斗
         * @param who 进入战斗的目标(未使用)
         *
         * @details 进入战斗时调用(目前无特殊逻辑)
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * @details 主循环逻辑,每帧调用:
         *          1. 检查是否有有效目标
         *          2. 更新技能计时器
         *          3. 根据计时器施放对应技能
         *          4. 执行近战攻击
         *
         * @par 技能循环:
         *          - 狂怒: 4秒间隔,最多叠加25层
         *          - 吸取打击: 15秒间隔
         *          - 群体殴打: 15秒间隔
         *          - 重击: 20秒间隔
         *
         * @note 使用传统的计时器递减方式,而非事件调度系统
         */
        void UpdateAI(uint32 diff) override
        {
            //Return since we have no target
            // 没有目标则返回
            if (!UpdateVictim())
                return;

            // 狂怒技能处理 - 持续叠加增益
            if (FuriousAnger_Timer <= diff)
            {
                FuriousAnger_Timer = 4000;
                // 最多叠加25层
                if (AngerCount > 25)
                    return;

                ++AngerCount;
                DoCast(me, SPELL_FURIOUS_ANGER, false);
            } else FuriousAnger_Timer -= diff;

            // 吸取打击技能处理
            //DrainingBlow - 吸取打击
            if (DrainingBlow_Timer <= diff)
            {
                DoCastVictim(SPELL_DRAININGBLOW);
                DrainingBlow_Timer = 15000;
            } else DrainingBlow_Timer -= diff;

            // 群体殴打技能处理
            //CrowdPummel - 群体殴打
            if (CrowdPummel_Timer <= diff)
            {
                DoCastVictim(SPELL_CROWDPUMMEL);
                CrowdPummel_Timer = 15000;
            } else CrowdPummel_Timer -= diff;

            // 重击技能处理
            //MightyBlow - 重击
            if (MightyBlow_Timer <= diff)
            {
                DoCastVictim(SPELL_MIGHTYBLOW);
                MightyBlow_Timer = 20000;
            } else MightyBlow_Timer -= diff;

            // 执行近战攻击
            DoMeleeAttackIfReady();
        }
    };

};

/**
 * @brief 注册脚本
 *
 * @details 将巴希拉斯法官Boss脚本注册到脚本系统
 */
void AddSC_boss_magistrate_barthilas()
{
    new boss_magistrate_barthilas();
}
