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
 * @file boss_void_reaver.cpp
 * @brief 风暴要塞-风暴之眼副本Boss虚空掠夺者AI实现
 *
 * 本模块实现了虚空掠夺者Boss的战斗逻辑。
 * 虚空掠夺者是一个机械Boss，具有以下特点:
 * - 周期性施放重击(范围伤害)
 * - 对远程目标施放奥术宝珠
 * - 击退并降低威胁值
 * - 10分钟狂暴计时
 *
 * @see https://wowpedia.fandom.com/wiki/Void_Reaver
 */

#include "the_eye.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"

/**
 * @enum ReaverTexts
 * @brief 虚空掠夺者台词枚举
 */
enum ReaverTexts
{
    SAY_AGGRO                   = 0,  ///< 激活台词
    SAY_SLAY                    = 1,  ///< 击杀玩家台词
    SAY_DEATH                   = 2,  ///< 死亡台词
    SAY_POUNDING                = 3   ///< 重击台词
};

/**
 * @enum ReaverSpells
 * @brief 虚空掠夺者法术ID枚举
 */
enum ReaverSpells
{
    SPELL_POUNDING              = 34162, ///< 重击 - 范围伤害技能
    SPELL_ARCANE_ORB            = 34172, ///< 奥术宝珠 - 对远程目标施放
    SPELL_KNOCK_AWAY            = 25778, ///< 击退 - 击退当前目标并降低威胁值
    SPELL_BERSERK               = 27680  ///< 狂暴 - 10分钟后进入狂暴状态
};

/**
 * @enum ReaverEvents
 * @brief 虚空掠夺者事件ID枚举
 */
enum ReaverEvents
{
    EVENT_POUNDING              = 1,  ///< 重击事件
    EVENT_ARCANE_ORB,                  ///< 奥术宝珠事件
    EVENT_KNOCK_AWAY,                  ///< 击退事件
    EVENT_BERSERK                      ///< 狂暴事件
};

/**
 * @struct boss_void_reaver
 * @brief 虚空掠夺者Boss AI实现
 *
 * 继承自BossAI，实现虚空掠夺者的战斗逻辑。
 * 虚空掠夺者是一个简单的装备检测型Boss，主要机制是威胁值管理和躲避技能。
 *
 * 战斗要点:
 * - 重击需要全团躲避，范围伤害很高
 * - 奥术宝珠优先攻击18码外的玩家
 * - 击退会降低威胁值，需要坦克轮换
 * - 10分钟狂暴限制
 */
struct boss_void_reaver : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_void_reaver(Creature* creature) : BossAI(creature, DATA_VOID_REAVER), _enraged(false) { }

    /**
     * @brief 重置Boss状态
     *
     * @调用时机 Boss脱离战斗时
     */
    void Reset() override
    {
        _Reset();
        _enraged = false;  // 重置狂暴状态
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     *
     * 喊话、呼叫帮助并调度所有技能事件
     * @调用时机 Boss进入战斗时
     */
    void JustEngagedWith(Unit* who) override
    {
        Talk(SAY_AGGRO);  // 说激活台词
        BossAI::JustEngagedWith(who);
        me->CallForHelp(120.0f);  // 呼叫周围120码内的友方单位

        // 调度技能事件
        events.ScheduleEvent(EVENT_POUNDING, 15s);   // 15秒后施放重击
        events.ScheduleEvent(EVENT_ARCANE_ORB, 3s);  // 3秒后施放奥术宝珠
        events.ScheduleEvent(EVENT_KNOCK_AWAY, 30s); // 30秒后施放击退
        events.ScheduleEvent(EVENT_BERSERK, 10min);  // 10分钟后狂暴
    }

    /**
     * @brief 击杀单位时调用
     * @param victim 被击杀的单位
     */
    void KilledUnit(Unit* /*victim*/) override
    {
        Talk(SAY_SLAY);  // 说击杀台词
    }

    /**
     * @brief Boss死亡时调用
     * @param killer 击杀者
     */
    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_DEATH);  // 说死亡台词
        _JustDied();
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 主AI更新循环，处理所有技能事件
     * @调用时机 每个游戏循环tick
     * @性能注意事项 包含威胁列表遍历
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        // 如果正在施法，则等待
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_POUNDING:
                    // 施放重击技能
                    DoCastVictim(SPELL_POUNDING);
                    Talk(SAY_POUNDING);  // 说重击台词
                    events.ScheduleEvent(EVENT_POUNDING, 15s);
                    break;
                case EVENT_ARCANE_ORB:
                {
                    // 收集18码外的玩家目标
                    std::vector<Unit*> target_list;
                    for (auto* ref : me->GetThreatManager().GetUnsortedThreatList())
                    {
                        Unit* target = ref->GetVictim();
                        // 只选择18码外的活着的玩家
                        if (target->GetTypeId() == TYPEID_PLAYER && target->IsAlive() && !target->IsWithinDist(me, 18, false))
                            target_list.push_back(target);
                    }

                    Unit* target;
                    if (!target_list.empty())
                        // 随机选择一个远程目标
                        target = *(target_list.begin() + rand32() % target_list.size());
                    else
                        // 如果没有远程目标，则选择当前目标
                        target = me->GetVictim();

                    if (target)
                        me->CastSpell(target, SPELL_ARCANE_ORB);  // 施放奥术宝珠

                    events.ScheduleEvent(EVENT_ARCANE_ORB, 3s);
                    break;
                }
                case EVENT_KNOCK_AWAY:
                    // 施放击退技能
                    DoCastVictim(SPELL_KNOCK_AWAY);
                    // 降低当前目标25%的威胁值
                    if (GetThreat(me->GetVictim()))
                        ModifyThreatByPercent(me->GetVictim(), -25);

                    events.ScheduleEvent(EVENT_KNOCK_AWAY, 30s);
                    break;
                case EVENT_BERSERK:
                    // 进入狂暴状态
                    if (!_enraged)
                    {
                        DoCastSelf(SPELL_BERSERK);
                        _enraged = true;
                    }
                    break;
                default:
                    break;
            }

            // 如果开始施法，则退出事件循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    bool _enraged;  ///< 是否已进入狂暴状态
};

/**
 * @brief 注册虚空掠夺者Boss脚本
 *
 * 此函数由脚本系统在服务器启动时调用，注册虚空掠夺者AI
 */
void AddSC_boss_void_reaver()
{
    RegisterTheEyeCreatureAI(boss_void_reaver);
}
