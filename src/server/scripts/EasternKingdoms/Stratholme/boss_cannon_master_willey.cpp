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
 * @file    boss_cannon_master_willey.cpp
 * @brief   斯坦索姆副本 - 炮手威利Boss战斗AI脚本
 *
 * @details 本模块实现了斯坦索姆副本炮手威利的战斗逻辑:
 *          - 使用火枪和近战技能
 *          - 定期召唤赤色步枪手援军
 *          - 死亡时召唤大量赤色步枪手
 *          - 斯坦索姆十字军广场区域的Boss之一
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "stratholme.h"

/**
 * @brief 召唤点位置定义
 * 定义赤色步枪手的召唤位置坐标(前左位置)
 */
//front, left - 前左位置
#define ADD_1X 3553.851807f
#define ADD_1Y -2945.885986f
#define ADD_1Z 125.001015f
#define ADD_1O 0.592007f

/**
 * @brief 召唤点位置定义
 * 定义赤色步枪手的召唤位置坐标(前右位置)
 */
//front, right - 前右位置
#define ADD_2X 3559.206299f
#define ADD_2Y -2952.929932f
#define ADD_2Z 125.001015f
#define ADD_2O 0.592007f

/**
 * @brief 召唤点位置定义
 * 定义赤色步枪手的召唤位置坐标(中左位置)
 */
//mid, left - 中左位置
#define ADD_3X 3552.417480f
#define ADD_3Y -2948.667236f
#define ADD_3Z 125.001015f
#define ADD_3O 0.592007f

/**
 * @brief 召唤点位置定义
 * 定义赤色步枪手的召唤位置坐标(中右位置)
 */
//mid, right - 中右位置
#define ADD_4X 3555.651855f
#define ADD_4Y -2953.519043f
#define ADD_4Z 125.001015f
#define ADD_4O 0.592007f

/**
 * @brief 召唤点位置定义
 * 定义赤色步枪手的召唤位置坐标(后左位置)
 */
//back, left - 后左位置
#define ADD_5X 3547.927246f
#define ADD_5Y -2950.977295f
#define ADD_5Z 125.001015f
#define ADD_5O 0.592007f

/**
 * @brief 召唤点位置定义
 * 定义赤色步枪手的召唤位置坐标(后中位置)
 */
//back, mid - 后中位置
#define ADD_6X 3553.094697f
#define ADD_6Y -2952.123291f
#define ADD_6Z 125.001015f
#define ADD_6O 0.592007f

/**
 * @brief 召唤点位置定义
 * 定义赤色步枪手的召唤位置坐标(后右位置)
 */
//back, right - 后右位置
#define ADD_7X 3552.727539f
#define ADD_7Y -2957.776123f
#define ADD_7Z 125.001015f
#define ADD_7O 0.592007f

/**
 * @brief 召唤点位置定义
 * 定义赤色步枪手的召唤位置坐标(最后左位置)
 */
//behind, left - 最后左位置
#define ADD_8X 3547.156250f
#define ADD_8Y -2953.162354f
#define ADD_8Z 125.001015f
#define ADD_8O 0.592007f

/**
 * @brief 召唤点位置定义
 * 定义赤色步枪手的召唤位置坐标(最后右位置)
 */
//behind, right - 最后右位置
#define ADD_9X 3550.202148f
#define ADD_9Y -2957.437744f
#define ADD_9Z 125.001015f
#define ADD_9O 0.592007f

/**
 * @brief 法术ID枚举
 * 定义炮手威利使用的所有法术技能ID
 */
enum Spells
{
    SPELL_KNOCKAWAY                 = 10101,  // 击退 - 击退近战目标
    SPELL_PUMMEL                    = 15615,  // 殴打 - 打断施法并造成伤害
    SPELL_SHOOT                     = 16496   // 射击 - 远程物理攻击
    //SPELL_SUMMONCRIMSONRIFLEMAN     = 17279  // 召唤赤色步枪手 - 未使用
};

/**
 * @class boss_cannon_master_willey
 * @brief 炮手威利Boss脚本类
 *
 * @details 实现炮手威利的脚本注册和AI创建
 */
class boss_cannon_master_willey : public CreatureScript
{
public:
    boss_cannon_master_willey() : CreatureScript("boss_cannon_master_willey") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 创建的AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetStratholmeAI<boss_cannon_master_willeyAI>(creature);
    }

    /**
     * @struct boss_cannon_master_willeyAI
     * @brief 炮手威利Boss AI
     *
     * @details 实现炮手威利的战斗逻辑:
     *          - 使用射击、殴打、击退等技能
     *          - 定期召唤赤色步枪手援军
     *          - 死亡时召唤7个赤色步枪手
     */
    struct boss_cannon_master_willeyAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_cannon_master_willeyAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         *
         * @details 设置所有技能计时器的初始值:
         *          - 射击: 1秒后首次施放
         *          - 殴打: 7秒后首次施放
         *          - 击退: 11秒后首次施放
         *          - 召唤步枪手: 15秒后首次施放
         */
        void Initialize()
        {
            Shoot_Timer = 1000;
            Pummel_Timer = 7000;
            KnockAway_Timer = 11000;
            SummonRifleman_Timer = 15000;
        }

        // 技能计时器
        uint32 KnockAway_Timer;        // 击退冷却计时器
        uint32 Pummel_Timer;           // 殴打冷却计时器
        uint32 Shoot_Timer;            // 射击冷却计时器
        uint32 SummonRifleman_Timer;   // 召唤步枪手冷却计时器

        /**
         * @brief 重置Boss状态
         *
         * @details 在战斗结束或重置时调用:
         *          - 重新初始化所有计时器
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief Boss死亡处理
         * @param killer 击杀者(未使用)
         *
         * @details Boss死亡时调用:
         *          - 在预设的7个召唤点召唤赤色步枪手
         *          - 僵尸会在4分钟后消失
         *
         * @note 召唤点选择: ADD_1, ADD_2, ADD_3, ADD_4, ADD_5, ADD_7, ADD_9
         */
        void JustDied(Unit* /*killer*/) override
        {
            // 在7个位置召唤赤色步枪手
            me->SummonCreature(11054, ADD_1X, ADD_1Y, ADD_1Z, ADD_1O, TEMPSUMMON_TIMED_DESPAWN, 4min);
            me->SummonCreature(11054, ADD_2X, ADD_2Y, ADD_2Z, ADD_2O, TEMPSUMMON_TIMED_DESPAWN, 4min);
            me->SummonCreature(11054, ADD_3X, ADD_3Y, ADD_3Z, ADD_3O, TEMPSUMMON_TIMED_DESPAWN, 4min);
            me->SummonCreature(11054, ADD_4X, ADD_4Y, ADD_4Z, ADD_4O, TEMPSUMMON_TIMED_DESPAWN, 4min);
            me->SummonCreature(11054, ADD_5X, ADD_5Y, ADD_5Z, ADD_5O, TEMPSUMMON_TIMED_DESPAWN, 4min);
            me->SummonCreature(11054, ADD_7X, ADD_7Y, ADD_7Z, ADD_7O, TEMPSUMMON_TIMED_DESPAWN, 4min);
            me->SummonCreature(11054, ADD_9X, ADD_9Y, ADD_9Z, ADD_9O, TEMPSUMMON_TIMED_DESPAWN, 4min);
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
         *          - 殴打: 12秒间隔, 90%概率施放
         *          - 击退: 14秒间隔, 80%概率施放
         *          - 射击: 1秒间隔, 100%施放
         *          - 召唤步枪手: 30秒间隔, 召唤3个步枪手
         *
         * @par 召唤步枪手机制:
         *          - 从9个预设位置中随机选择一组3个位置
         *          - 共有9种不同的位置组合
         *          - 步枪手会在4分钟后消失
         *
         * @note 使用传统的计时器递减方式,而非事件调度系统
         */
        void UpdateAI(uint32 diff) override
        {
            // 检查是否有有效目标
            //Return since we have no target
            // 没有目标则返回
            if (!UpdateVictim())
                return;

            // 殴打技能处理
            //Pummel - 殴打
            if (Pummel_Timer <= diff)
            {
                //Cast - 施放法术
                // 90%概率施放
                if (rand32() % 100 < 90) //90% chance to cast
                {
                    DoCastVictim(SPELL_PUMMEL);
                }
                //12 seconds until we should cast this again
                // 12秒后再次施放
                Pummel_Timer = 12000;
            } else Pummel_Timer -= diff;

            // 击退技能处理
            //KnockAway - 击退
            if (KnockAway_Timer <= diff)
            {
                //Cast - 施放法术
                // 80%概率施放
                if (rand32() % 100 < 80) //80% chance to cast
                {
                    DoCastVictim(SPELL_KNOCKAWAY);
                }
                //14 seconds until we should cast this again
                // 14秒后再次施放
                KnockAway_Timer = 14000;
            } else KnockAway_Timer -= diff;

            // 射击技能处理
            //Shoot - 射击
            if (Shoot_Timer <= diff)
            {
                //Cast - 施放法术
                DoCastVictim(SPELL_SHOOT);
                //1 seconds until we should cast this again
                // 1秒后再次施放
                Shoot_Timer = 1000;
            } else Shoot_Timer -= diff;

            // 召唤步枪手处理
            //SummonRifleman - 召唤步枪手
            if (SummonRifleman_Timer <= diff)
            {
                //Cast - 施放法术
                // 随机选择一组3个召唤位置
                switch (rand32() % 9)
                {
                case 0:
                    // 组合1: 前左、前右、中右
                    me->SummonCreature(11054, ADD_1X, ADD_1Y, ADD_1Z, ADD_1O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_2X, ADD_2Y, ADD_2Z, ADD_2O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_4X, ADD_4Y, ADD_4Z, ADD_4O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    break;
                case 1:
                    // 组合2: 前右、中左、后左
                    me->SummonCreature(11054, ADD_2X, ADD_2Y, ADD_2Z, ADD_2O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_3X, ADD_3Y, ADD_3Z, ADD_3O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_5X, ADD_5Y, ADD_5Z, ADD_5O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    break;
                case 2:
                    // 组合3: 中左、中右、后中
                    me->SummonCreature(11054, ADD_3X, ADD_3Y, ADD_3Z, ADD_3O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_4X, ADD_4Y, ADD_4Z, ADD_4O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_6X, ADD_6Y, ADD_6Z, ADD_6O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    break;
                case 3:
                    // 组合4: 中右、后左、后右
                    me->SummonCreature(11054, ADD_4X, ADD_4Y, ADD_4Z, ADD_4O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_5X, ADD_5Y, ADD_5Z, ADD_5O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_7X, ADD_7Y, ADD_7Z, ADD_7O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    break;
                case 4:
                    // 组合5: 后左、后中、最后左
                    me->SummonCreature(11054, ADD_5X, ADD_5Y, ADD_5Z, ADD_5O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_6X, ADD_6Y, ADD_6Z, ADD_6O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_8X, ADD_8Y, ADD_8Z, ADD_8O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    break;
                case 5:
                    // 组合6: 后中、后右、最后右
                    me->SummonCreature(11054, ADD_6X, ADD_6Y, ADD_6Z, ADD_6O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_7X, ADD_7Y, ADD_7Z, ADD_7O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_9X, ADD_9Y, ADD_9Z, ADD_9O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    break;
                case 6:
                    // 组合7: 后右、最后左、前左
                    me->SummonCreature(11054, ADD_7X, ADD_7Y, ADD_7Z, ADD_7O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_8X, ADD_8Y, ADD_8Z, ADD_8O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_1X, ADD_1Y, ADD_1Z, ADD_1O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    break;
                case 7:
                    // 组合8: 最后左、最后右、前右
                    me->SummonCreature(11054, ADD_8X, ADD_8Y, ADD_8Z, ADD_8O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_9X, ADD_9Y, ADD_9Z, ADD_9O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_2X, ADD_2Y, ADD_2Z, ADD_2O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    break;
                case 8:
                    // 组合9: 最后右、前左、中左
                    me->SummonCreature(11054, ADD_9X, ADD_9Y, ADD_9Z, ADD_9O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_1X, ADD_1Y, ADD_1Z, ADD_1O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    me->SummonCreature(11054, ADD_3X, ADD_3Y, ADD_3Z, ADD_3O, TEMPSUMMON_TIMED_DESPAWN, 4min);
                    break;
                }
                //30 seconds until we should cast this again
                // 30秒后再次施放
                SummonRifleman_Timer = 30000;
            } else SummonRifleman_Timer -= diff;

            // 执行近战攻击
            DoMeleeAttackIfReady();
        }
    };

};

/**
 * @brief 注册脚本
 *
 * @details 将炮手威利Boss脚本注册到脚本系统
 */
void AddSC_boss_cannon_master_willey()
{
    new boss_cannon_master_willey();
}
