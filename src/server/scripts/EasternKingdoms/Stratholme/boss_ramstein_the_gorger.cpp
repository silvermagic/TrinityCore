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
 * @file    boss_ramstein_the_gorger.cpp
 * @brief   拉姆斯登·沃戈尔(Ramstein the Gorger)BOSS脚本
 *
 * @details 本模块实现了斯坦索姆副本中的BOSS拉姆斯登的AI逻辑:
 *          - 拉姆斯登是亡灵侧屠宰场的守关BOSS
 *          - 在玩家击杀所有憎恶后刷新
 *          - 死亡后会触发黑卫士刷新事件
 *          - 死亡时召唤30只无脑亡灵围攻玩家
 *          - 使用践踏和击倒技能
 *
 * @note BOSS完成度: 70%
 *       拉姆斯登是通往巴隆·瑞文戴尔前的最后一道防线
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "stratholme.h"
#include "TemporarySummon.h"

/**
 * @brief 拉姆斯登使用的法术枚举
 */
enum Spells
{
    SPELL_TRAMPLE           = 5568,  // 践踏 - 对周围敌人造成伤害
    SPELL_KNOCKOUT          = 17307  // 击倒 - 击晕目标
};

/**
 * @brief 生物ID枚举
 */
enum CreatureId
{
    NPC_MINDLESS_UNDEAD     = 11030  // 无脑亡灵 - 拉姆斯登死后召唤的小怪
};

/**
 * @class boss_ramstein_the_gorger
 * @brief 拉姆斯登BOSS脚本类
 *
 * @details 实现拉姆斯登BOSS的AI脚本注册和AI逻辑
 */
class boss_ramstein_the_gorger : public CreatureScript
{
public:
    boss_ramstein_the_gorger() : CreatureScript("boss_ramstein_the_gorger") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物指针
     * @return AI实例指针
     *
     * @details 创建并返回拉姆斯登AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetStratholmeAI<boss_ramstein_the_gorgerAI>(creature);
    }

    /**
     * @struct boss_ramstein_the_gorgerAI
     * @brief 拉姆斯登AI实现
     *
     * @details 实现拉姆斯登的战斗AI:
     *          - 定期施放践踏技能伤害周围玩家
     *          - 击倒目标眩晕玩家
     *          - 死亡时召唤大量无脑亡灵
     *          - 死亡时更新副本进度,触发黑卫士事件
     */
    struct boss_ramstein_the_gorgerAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物指针
         *
         * @details 初始化AI并获取副本实例脚本
         */
        boss_ramstein_the_gorgerAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = me->GetInstanceScript();
        }

        /**
         * @brief 初始化成员变量
         *
         * @details 设置技能计时器的初始值:
         *          - 践踏: 3秒后首次施放
         *          - 击倒: 12秒后首次施放
         */
        void Initialize()
        {
            Trample_Timer = 3000;   // 践踏计时器(毫秒)
            Knockout_Timer = 12000; // 击倒计时器(毫秒)
        }

        InstanceScript* instance;  // 副本实例脚本指针

        uint32 Trample_Timer;      // 践踏冷却计时器
        uint32 Knockout_Timer;     // 击倒冷却计时器

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
         *          1. 召唤30只无脑亡灵包围玩家:
         *             - 召唤位置: (3969.35, -3391.87, 119.11)附近10码范围内
         *             - 朝向: 5.91弧度
         *             - 存在时间: 30分钟或死亡后消失
         *             - 立即攻击最近的玩家
         *          2. 设置副本的拉姆斯登状态为完成
         *          3. 触发黑卫士刷新事件(1分钟后刷新)
         *
         * @note 这是斯坦索姆副本的经典场面,大量亡灵围攻玩家
         */
        void JustDied(Unit* /*killer*/) override
        {
            // 召唤30只无脑亡灵
            for (uint8 i = 0; i < 30; ++i)
            {
                if (Creature* mob = me->SummonCreature(NPC_MINDLESS_UNDEAD, 3969.35f+irand(-10, 10), -3391.87f+irand(-10, 10), 119.11f, 5.91f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 30min))
                    mob->AI()->AttackStart(me->SelectNearestTarget(100.0f));
            }

            instance->SetData(TYPE_RAMSTEIN, DONE);
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * @details 主循环逻辑,每帧调用:
         *          1. 检查是否有战斗目标,无则返回
         *          2. 践踏: 7秒冷却,对周围所有敌人造成伤害
         *          3. 击倒: 10秒冷却,眩晕当前目标
         *          4. 执行近战攻击
         *
         * @note 拉姆斯登的技能相对简单,主要依靠高血量和召唤小怪
         */
        void UpdateAI(uint32 diff) override
        {
            //Return since we have no target
            // 没有目标则返回
            if (!UpdateVictim())
                return;

            //Trample - 践踏
            if (Trample_Timer <= diff)
            {
                DoCast(me, SPELL_TRAMPLE);
                Trample_Timer = 7000;  // 7秒冷却
            } else Trample_Timer -= diff;

            //Knockout - 击倒
            if (Knockout_Timer <= diff)
            {
                DoCastVictim(SPELL_KNOCKOUT);
                Knockout_Timer = 10000;  // 10秒冷却
            } else Knockout_Timer -= diff;

            DoMeleeAttackIfReady();
        }
    };

};

/**
 * @brief 注册脚本
 *
 * @details 将拉姆斯登BOSS脚本注册到脚本系统
 */
void AddSC_boss_ramstein_the_gorger()
{
    new boss_ramstein_the_gorger();
}
