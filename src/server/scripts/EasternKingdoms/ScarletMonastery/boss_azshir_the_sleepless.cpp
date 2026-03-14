/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the option) any later version.
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
 * @file boss_azshir_the_sleepless.cpp
 * @brief 血色修道院BOSS阿兹希·不眠者战斗脚本
 *
 * 本模块实现了血色修道院（墓地）稀有BOSS阿兹希·不眠者的战斗AI：
 * - 阿兹希·不眠者（亡灵战士）
 * - 血色修道院墓地区域的稀有刷新BOSS
 *
 * 战斗机制：
 * 1. 使用坟墓呼唤造成暗影伤害
 * 2. 使用恐惧术控制玩家
 * 3. 核心机制：血量低于50%时开始使用灵魂虹吸技能
 *
 * 特殊事件：
 * - 血量低于50%后开始定期施放灵魂虹吸，对目标造成持续伤害
 */

#include "scarlet_monastery.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"

/**
 * @brief 阿兹希·不眠者法术ID枚举
 *
 * 定义阿兹希使用的所有法术ID
 */
enum AzshirTheSleeplessSpells
{
    SPELL_CALL_OF_THE_GRAVE = 17831, // 坟墓呼唤 - 暗影伤害技能
    SPELL_TERRIFY = 7399,            // 恐惧术 - 使目标恐惧逃跑
    SPELL_SOUL_SIPHON = 7290         // 灵魂虹吸 - 持续吸取生命
};

/**
 * @brief 阿兹希·不眠者事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum AzshirTheSleeplessEvents
{
    EVENT_CALL_OF_GRAVE = 1, // 坟墓呼唤事件
    EVENT_TERRIFY,           // 恐惧术事件
    EVENT_SOUL_SIPHON        // 灵魂虹吸事件
};

/**
 * @brief 阿兹希·不眠者BOSS AI结构体
 *
 * 实现阿兹希·不眠者的战斗AI逻辑，包括：
 * - 坟墓呼唤、恐惧术等常规技能
 * - 血量50%以下的灵魂虹吸机制
 *
 * 战斗流程：
 * 1. 进入战斗时安排技能事件
 * 2. 定期施放坟墓呼唤（30秒冷却）和恐惧术（20秒冷却）
 * 3. 血量低于50%时开始施放灵魂虹吸（20秒冷却）
 * 4. 近战攻击
 */
struct boss_azshir_the_sleepless : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化BOSS AI，设置灵魂虹吸标志为false（表示还未触发）
     */
    boss_azshir_the_sleepless(Creature* creature) : BossAI(creature, DATA_AZSHIR)
    {
        _siphon = false;
    }

    /**
     * @brief 重置AI状态
     *
     * 当战斗重置时调用，重置所有状态变量和事件
     * 将灵魂虹吸标志重置为false
     */
    void Reset() override
    {
        _Reset();
        _siphon = false;
    }

    /**
     * @brief 进入战斗事件
     * @param who 进入战斗的目标
     *
     * 当阿兹希进入战斗时：
     * - 安排技能施放事件
     *
     * 技能安排：
     * - 坟墓呼唤：30秒后施放
     * - 恐惧术：20秒后施放
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_CALL_OF_GRAVE, 30s);
        events.ScheduleEvent(EVENT_TERRIFY, 20s);
    }

    /**
     * @brief 受到伤害事件
     * @param attacker 攻击者
     * @param damage 伤害值（引用，可修改）
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 核心机制：处理阿兹希的灵魂虹吸逻辑
     * - 当血量低于50%且未施放过灵魂虹吸时，立即施放一次
     * - 并安排后续定期施放（20秒冷却）
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (!_siphon && me->HealthBelowPctDamaged(50, damage))
        {
            DoCastVictim(SPELL_SOUL_SIPHON);
            events.ScheduleEvent(EVENT_SOUL_SIPHON, 20s);
            _siphon = true;
        }
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
     * 3. 执行近战攻击
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
                case EVENT_CALL_OF_GRAVE:
                    DoCastVictim(SPELL_CALL_OF_THE_GRAVE);
                    events.Repeat(30s);
                    break;
                case EVENT_TERRIFY:
                    DoCastVictim(SPELL_TERRIFY);
                    events.Repeat(20s);
                    break;
                case EVENT_SOUL_SIPHON:
                    DoCastVictim(SPELL_SOUL_SIPHON);
                    events.Repeat(20s);
                    break;
                default:
                    break;
            }

            // 施法后立即返回，避免在同一帧内施放多个技能
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    bool _siphon;  // 是否已触发灵魂虹吸，用于血量50%判定
};

/**
 * @brief 注册BOSS脚本
 *
 * 将阿兹希·不眠者的AI注册到脚本系统中
 */
void AddSC_boss_azshir_the_sleepless()
{
    RegisterScarletMonasteryCreatureAI(boss_azshir_the_sleepless);
}
