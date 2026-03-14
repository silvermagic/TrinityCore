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
 * @file boss_arcanist_doan.cpp
 * @brief 血色修道院BOSS大秘术师多安战斗脚本
 *
 * 本模块实现了血色修道院（图书馆）最终BOSS大秘术师多安的战斗AI：
 * - 多安（血色修道院图书馆管理者）
 * - 拥有强大的奥术魔法能力
 *
 * 战斗机制：
 * 1. 使用沉默术打断施法者
 * 2. 定期施放奥术爆炸造成AOE伤害
 * 3. 随机变羊控制目标
 * 4. 核心机制：血量低于50%时施放奥术护盾和引爆技能
 *
 * 特殊事件：
 * - 血量低于50%时触发特殊AOE技能组合（奥术护盾+引爆）
 * - 这是该BOSS的标志性技能，需要玩家注意躲避
 */

#include "scarlet_monastery.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"

/**
 * @brief 大秘术师多安对话文本ID枚举
 *
 * 定义多安在战斗中的各种对话文本ID
 */
enum ArcanistDoanYells
{
    SAY_AGGRO = 0,      // 进入战斗时的喊话
    SAY_SPECIALAE = 1   // 施放特殊AOE技能时的喊话
};

/**
 * @brief 大秘术师多安法术ID枚举
 *
 * 定义多安使用的所有法术ID
 */
enum ArcanistDoanSpells
{
    SPELL_SILENCE = 8988,          // 沉默术 - 打断施法者的法术施放
    SPELL_ARCANE_EXPLOSION = 9433, // 奥术爆炸 - AOE伤害技能
    SPELL_DETONATION = 9435,       // 引爆 - 血量50%以下时的强力AOE技能
    SPELL_ARCANE_BUBBLE = 9438,    // 奥术护盾 - 保护自己免疫伤害
    SPELL_POLYMORPH = 13323        // 变羊术 - 控制技能
};

/**
 * @brief 大秘术师多安事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum ArcanistDoanEvents
{
    EVENT_SILENCE = 1,          // 沉默术事件
    EVENT_ARCANE_EXPLOSION,     // 奥术爆炸事件
    EVENT_ARCANE_BUBBLE,        // 奥术护盾事件
    EVENT_POLYMORPH             // 变羊术事件
};

/**
 * @brief 大秘术师多安BOSS AI结构体
 *
 * 实现多安的战斗AI逻辑，包括：
 * - 沉默、奥术爆炸、变羊等常规技能
 * - 血量50%时的特殊AOE技能组合
 *
 * 战斗流程：
 * 1. 进入战斗时喊话
 * 2. 定期施放沉默术（15-20秒冷却）、奥术爆炸（8秒冷却）、变羊术（20秒冷却）
 * 3. 血量低于50%时施放奥术护盾和引爆技能
 * 4. 近战攻击
 */
struct boss_arcanist_doan : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化BOSS AI，设置血量标志为true（表示血量还在50%以上）
     */
    boss_arcanist_doan(Creature* creature) : BossAI(creature, DATA_ARCANIST_DOAN)
    {
        _healthAbove50Pct = true;
    }

    /**
     * @brief 重置AI状态
     *
     * 当战斗重置时调用，重置所有状态变量和事件
     * 将血量标志重置为true（表示血量还在50%以上）
     */
    void Reset() override
    {
        _Reset();
        _healthAbove50Pct = true;
    }

    /**
     * @brief 进入战斗事件
     * @param who 进入战斗的目标
     *
     * 当多安进入战斗时：
     * - 喊出战斗台词
     * - 安排技能施放事件
     *
     * 技能安排：
     * - 沉默术：15秒后施放
     * - 奥术爆炸：3秒后施放
     * - 变羊术：30秒后施放
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);

        events.ScheduleEvent(EVENT_SILENCE, 15s);
        events.ScheduleEvent(EVENT_ARCANE_EXPLOSION, 3s);
        events.ScheduleEvent(EVENT_POLYMORPH, 30s);
    }

    /**
     * @brief 更新AI
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 每帧调用，处理技能施放和战斗逻辑
     *
     * 核心机制：
     * 1. 更新事件队列
     * 2. 执行触发的技能事件
     * 3. 检查血量是否低于50%，触发特殊AOE技能组合
     * 4. 执行近战攻击
     *
     * 性能注意事项：
     * - 如果正在施法则跳过事件处理，避免打断施法
     * - 使用while循环处理所有待执行事件
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        // 如果正在施法，则跳过事件处理
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有待执行的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SILENCE:
                    DoCastVictim(SPELL_SILENCE);
                    events.Repeat(15s, 20s);
                    break;
                case EVENT_ARCANE_EXPLOSION:
                    DoCastVictim(SPELL_ARCANE_EXPLOSION);
                    events.Repeat(8s);
                    break;
                case EVENT_POLYMORPH:
                    // 随机选择一个非当前目标施放变羊术
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 30.0f, true))
                        DoCast(target, SPELL_POLYMORPH);
                    events.Repeat(20s);
                    break;
                default:
                    break;
            }

            // 施法后立即返回，避免在同一帧内施放多个技能
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 核心机制：血量低于50%时触发特殊AOE技能组合
        if (_healthAbove50Pct && HealthBelowPct(50))
        {
            _healthAbove50Pct = false;
            Talk(SAY_SPECIALAE);              // 喊出特殊技能台词
            DoCastSelf(SPELL_ARCANE_BUBBLE);  // 施放奥术护盾保护自己
            DoCastAOE(SPELL_DETONATION);      // 施放引爆造成大量AOE伤害
        }

        DoMeleeAttackIfReady();
    }

private:
    bool _healthAbove50Pct;  // 血量是否在50%以上，用于触发特殊AOE技能组合
};

/**
 * @brief 注册BOSS脚本
 *
 * 将大秘术师多安的AI注册到脚本系统中
 */
void AddSC_boss_arcanist_doan()
{
    RegisterScarletMonasteryCreatureAI(boss_arcanist_doan);
}
