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
 * @file boss_vectus.cpp
 * @brief 通灵学院BOSS维克图斯战斗脚本
 *
 * 本模块实现了通灵学院BOSS维克图斯的战斗AI：
 * - 维克图斯（亡灵法师）
 * - 火焰打击、冲击波、火焰护盾和狂暴技能
 *
 * 战斗机制：
 * 1. 火焰护盾：对自己施放，持续90秒，反弹火焰伤害
 * 2. 冲击波：对周围敌人造成火焰伤害并击退
 * 3. 狂暴：生命值低于25%时触发，提高攻击速度和伤害
 *
 * 特殊机制：
 * - 狂暴状态会在25%生命值以下每24秒触发一次
 * - 狂暴时会发出表情提示团队
 *
 * 完成度：100%
 * 备注：维克图斯是通灵学院的小BOSS之一
 */

#include "scholomance.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"

/**
 * @brief 表情ID枚举
 *
 * 定义维克图斯使用的表情ID
 */
enum Emotes
{
    EMOTE_FRENZY                 = 0  // 狂暴表情 - 提示团队BOSS进入狂暴状态
};

/**
 * @brief 法术ID枚举
 *
 * 定义维克图斯使用的所有法术ID
 */
enum Spells
{
    SPELL_FLAMESTRIKE            = 18399,  // 火焰打击 - 在目标位置召唤火焰打击（未在当前版本使用）
    SPELL_BLAST_WAVE             = 16046,  // 冲击波 - 对周围敌人造成火焰伤害并击退
    SPELL_FIRE_SHIELD            = 19626,  // 火焰护盾 - 反弹火焰伤害给攻击者
    SPELL_FRENZY                 = 8269    // 狂暴 - 提高攻击速度和伤害（备选ID：28371）
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum Events
{
    EVENT_FIRE_SHIELD = 1,  // 火焰护盾事件
    EVENT_BLAST_WAVE,       // 冲击波事件
    EVENT_FRENZY            // 狂暴事件
};

/**
 * @brief 维克图斯脚本类
 *
 * 实现维克图斯的战斗AI，包括：
 * - 火焰护盾技能（反弹火焰伤害）
 * - 冲击波技能（范围火焰伤害和击退）
 * - 狂暴机制（生命值低于25%时触发）
 */
class boss_vectus : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称
     */
    boss_vectus() : CreatureScript("boss_vectus") { }

    /**
     * @brief 维克图斯AI结构体
     *
     * 实现维克图斯的战斗AI逻辑，继承自ScriptedAI
     * 注意：未使用BossAI基类，因此需要自己管理事件映射
     */
    struct boss_vectusAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_vectusAI(Creature* creature) : ScriptedAI(creature) { }

        /**
         * @brief 重置AI状态
         *
         * 当战斗重置时调用：
         * - 清空所有事件
         *
         * 调用时机：战斗重置或BOSS脱离战斗时
         */
        void Reset() override
        {
            events.Reset();
        }

        /**
         * @brief 进入战斗事件
         * @param who 进入战斗的目标（未使用）
         *
         * 当维克图斯进入战斗时：
         * - 安排火焰护盾技能（2秒后）
         * - 安排冲击波技能（14秒后）
         *
         * 调用时机：BOSS进入战斗时
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            events.ScheduleEvent(EVENT_FIRE_SHIELD, 2s);
            events.ScheduleEvent(EVENT_BLAST_WAVE, 14s);
        }

        /**
         * @brief 受到伤害事件
         * @param attacker 攻击者（未使用）
         * @param damage 伤害值（引用）
         * @param damageType 伤害类型（未使用）
         * @param spellInfo 法术信息（未使用）
         *
         * 当维克图斯受到伤害时检查生命值：
         * - 如果生命值低于25%且首次到达该阈值，触发狂暴
         * - 狂暴会提高攻击速度和伤害
         * - 每24秒重复触发狂暴
         *
         * 调用时机：BOSS受到伤害时
         * 性能注意：每次受到伤害都会调用，需要保持轻量
         */
        void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            // 当生命值低于25%时触发狂暴
            if (me->HealthBelowPctDamaged(25, damage))
            {
                DoCast(me, SPELL_FRENZY);      // 施放狂暴
                Talk(EMOTE_FRENZY);             // 发出狂暴表情提示团队
                events.ScheduleEvent(EVENT_FRENZY, 24s);  // 24秒后再次狂暴
            }
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 每帧调用，处理战斗逻辑：
         * - 检查是否有战斗目标
         * - 更新事件计时器
         * - 检查施法状态
         * - 执行技能事件（火焰护盾、冲击波、狂暴）
         * - 进行近战攻击
         *
         * 调用时机：每帧由核心代码调用
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())  // 没有战斗目标则返回
                return;

            events.Update(diff);  // 更新事件计时器

            if (me->HasUnitState(UNIT_STATE_CASTING))  // 正在施法则等待
                return;

            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_FIRE_SHIELD:
                        // 对自己施放火焰护盾
                        DoCast(me, SPELL_FIRE_SHIELD);
                        events.ScheduleEvent(EVENT_FIRE_SHIELD, 90s);  // 90秒后再次施放
                        break;
                    case EVENT_BLAST_WAVE:
                        // 施放冲击波
                        DoCast(me, SPELL_BLAST_WAVE);
                        events.ScheduleEvent(EVENT_BLAST_WAVE, 12s);  // 12秒后再次施放
                        break;
                    case EVENT_FRENZY:
                        // 施放狂暴
                        DoCast(me, SPELL_FRENZY);
                        Talk(EMOTE_FRENZY);  // 发出狂暴表情
                        events.ScheduleEvent(EVENT_FRENZY, 24s);  // 24秒后再次狂暴
                        break;
                    default:
                        break;
                }

                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;
            }

            DoMeleeAttackIfReady();  // 如果可以，进行近战攻击
        }

        private:
            EventMap events;  // 事件映射表，管理技能冷却和施放计划
    };

    /**
     * @brief 获取AI
     * @param creature 生物对象指针
     * @return AI对象指针
     *
     * 创建并返回维克图斯AI对象
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetScholomanceAI<boss_vectusAI>(creature);
    }
};

/**
 * @brief 注册维克图斯脚本
 *
 * 将维克图斯脚本注册到脚本系统中
 */
void AddSC_boss_vectus()
{
    new boss_vectus();
}
