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
 * @file boss_amnennar_the_coldbringer.cpp
 * @brief 剃刀高地BOSS寒冰使者亚门纳尔的AI脚本
 *
 * 本模块实现了剃刀高地最终BOSS寒冰使者亚门纳尔的战斗逻辑：
 * - 霜冻箭：对目标造成冰霜伤害
 * - 冰霜新星：对周围敌人造成冰霜伤害并减速
 * - 亚门纳尔之怒：对敌人造成暗影伤害
 * - 冰霜幽灵召唤：在血量60%和30%时召唤冰霜幽灵
 * - 特殊血量提示：在50%血量时发出警告
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "razorfen_downs.h"

/**
 * @brief 对话ID枚举
 */
enum Say
{
    SAY_AGGRO               = 0,  ///< 开战喊话
    SAY_SUMMON60            = 1,  ///< 血量60%召唤冰霜幽灵喊话
    SAY_SUMMON30            = 2,  ///< 血量30%召唤冰霜幽灵喊话
    SAY_HP                  = 3,  ///< 血量50%警告喊话
    SAY_KILL                = 4   ///< 击杀玩家喊话
};

/**
 * @brief 法术ID枚举
 */
enum Spells
{
    SPELL_AMNENNARSWRATH    = 13009,  ///< 亚门纳尔之怒：对目标造成暗影伤害
    SPELL_FROSTBOLT         = 15530,  ///< 霜冻箭：对目标造成冰霜伤害
    SPELL_FROST_NOVA        = 15531,  ///< 冰霜新星：对周围敌人造成冰霜伤害并减速
    SPELL_FROST_SPECTRES    = 12642   ///< 冰霜幽灵：召唤冰霜幽灵辅助战斗
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_AMNENNARSWRATH    = 1,  ///< 亚门纳尔之怒事件
    EVENT_FROSTBOLT         = 2,  ///< 霜冻箭事件
    EVENT_FROST_NOVA        = 3   ///< 冰霜新星事件
};

/**
 * @class boss_amnennar_the_coldbringer
 * @brief 寒冰使者亚门纳尔BOSS脚本类
 *
 * 负责注册和管理寒冰使者亚门纳尔BOSS的AI行为
 */
class boss_amnennar_the_coldbringer : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    boss_amnennar_the_coldbringer() : CreatureScript("boss_amnennar_the_coldbringer") { }

    /**
     * @class boss_amnennar_the_coldbringerAI
     * @brief 寒冰使者亚门纳尔BOSS的AI实现类
     *
     * 实现了寒冰使者亚门纳尔的战斗逻辑：
     * - 周期性施放霜冻箭造成冰霜伤害
     * - 周期性施放冰霜新星控制周围敌人
     * - 周期性施放亚门纳尔之怒造成暗影伤害
     * - 血量低于60%和30%时召唤冰霜幽灵
     * - 血量低于50%时发出警告
     */
    struct boss_amnennar_the_coldbringerAI : public BossAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物指针
         */
        boss_amnennar_the_coldbringerAI(Creature* creature) : BossAI(creature, DATA_AMNENNAR_THE_COLD_BRINGER)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         *
         * 在构造函数和重置时调用，用于初始化战斗状态变量
         */
        void Initialize()
        {
            hp60Spectrals = false;  ///< 是否已在60%血量召唤过冰霜幽灵
            hp30Spectrals = false;  ///< 是否已在30%血量召唤过冰霜幽灵
            hp50 = false;           ///< 是否已在50%血量发出警告
        }

        /**
         * @brief 重置BOSS状态
         *
         * 当BOSS脱离战斗或重置时调用，重置战斗状态变量
         * @调用时机 战斗重置、BOSS脱战
         */
        void Reset() override
        {
            _Reset();
            Initialize();
        }

        /**
         * @brief 进入战斗回调
         * @param who 仇恨目标
         *
         * 当BOSS进入战斗时调用，安排技能事件并喊话
         * @调用时机 BOSS进入战斗
         */
        void JustEngagedWith(Unit* who) override
        {
            BossAI::JustEngagedWith(who);
            // 安排技能事件
            events.ScheduleEvent(EVENT_AMNENNARSWRATH, 8s);    // 亚门纳尔之怒
            events.ScheduleEvent(EVENT_FROSTBOLT, 1s);         // 霜冻箭
            events.ScheduleEvent(EVENT_FROST_NOVA, 10s, 15s);  // 冰霜新星
            Talk(SAY_AGGRO);
        }

        /**
         * @brief 击杀单位回调
         * @param who 被击杀的单位
         *
         * 当BOSS击杀玩家时喊话
         * @调用时机 BOSS击杀单位时
         */
        void KilledUnit(Unit* who) override
        {
            if (who->GetTypeId() == TYPEID_PLAYER)
                Talk(SAY_KILL);
        }

        /**
         * @brief 死亡回调
         * @param killer 击杀者（未使用）
         *
         * 当BOSS死亡时调用，通知实例脚本
         * @调用时机 BOSS死亡时
         */
        void JustDied(Unit* /*killer*/) override
        {
            _JustDied();
        }

        /**
         * @brief 更新AI
         * @param diff 时间差（毫秒）
         *
         * 每帧调用，处理BOSS的战斗逻辑
         * @调用时机 每帧更新
         * @性能注意事项 该函数每帧调用，需保持高效
         */
        void UpdateAI(uint32 diff) override
        {
            // 如果没有目标则返回
            if (!UpdateVictim())
                return;

            events.Update(diff);

            // 如果正在施法，暂停其他操作
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            // 处理事件队列
            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_AMNENNARSWRATH:
                        // 对当前目标施放亚门纳尔之怒
                        DoCastVictim(SPELL_AMNENNARSWRATH);
                        events.ScheduleEvent(EVENT_AMNENNARSWRATH, 12s);
                        break;
                    case EVENT_FROSTBOLT:
                        // 对当前目标施放霜冻箭
                        DoCastVictim(SPELL_FROSTBOLT);
                        events.ScheduleEvent(EVENT_FROSTBOLT, 8s);
                        break;
                    case EVENT_FROST_NOVA:
                        // 对自己施放冰霜新星（影响周围敌人）
                        DoCast(me, SPELL_FROST_NOVA);
                        events.ScheduleEvent(EVENT_FROST_NOVA, 15s);
                        break;
                }

                // 如果正在施法，暂停其他操作
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;
            }

            // 血量低于60%时召唤冰霜幽灵（只触发一次）
            if (!hp60Spectrals && HealthBelowPct(60))
            {
                Talk(SAY_SUMMON60);
                DoCastVictim(SPELL_FROST_SPECTRES);
                hp60Spectrals = true;
            }

            // 血量低于50%时发出警告（只触发一次）
            if (!hp50 && HealthBelowPct(50))
            {
                Talk(SAY_HP);
                hp50 = true;
            }

            // 血量低于30%时再次召唤冰霜幽灵（只触发一次）
            if (!hp30Spectrals && HealthBelowPct(30))
            {
                Talk(SAY_SUMMON30);
                DoCastVictim(SPELL_FROST_SPECTRES);
                hp30Spectrals = true;
            }

            DoMeleeAttackIfReady();
        }

    private:
        bool hp60Spectrals;  ///< 是否已在60%血量召唤过冰霜幽灵
        bool hp30Spectrals;  ///< 是否已在30%血量召唤过冰霜幽灵
        bool hp50;           ///< 是否已在50%血量发出警告
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物指针
     * @return AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetRazorfenDownsAI<boss_amnennar_the_coldbringerAI>(creature);
    }
};

/**
 * @brief 注册脚本
 *
 * 将寒冰使者亚门纳尔BOSS脚本注册到脚本系统
 */
void AddSC_boss_amnennar_the_coldbringer()
{
    new boss_amnennar_the_coldbringer();
}
