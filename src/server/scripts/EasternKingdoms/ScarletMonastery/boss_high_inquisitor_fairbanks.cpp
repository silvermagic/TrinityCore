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
 * @file boss_high_inquisitor_fairbanks.cpp
 * @brief 血色修道院BOSS大检察官费尔班克斯战斗脚本
 *
 * 本模块实现了血色修道院（教堂）BOSS大检察官费尔班克斯的战斗AI：
 * - 大检察官费尔班克斯（亡灵牧师）
 * - 血色修道院教堂区域的隐藏BOSS
 *
 * 战斗机制：
 * 1. 使用诅咒、恐惧、睡眠等控制技能
 * 2. 可以驱散玩家的增益效果
 * 3. 核心机制：血量低于25%时施放真言术：盾并开始治疗自己
 *
 * 特殊事件：
 * - 初始状态为死亡状态，需要被唤醒才能战斗
 * - 血量低于25%时进入防御模式，施放护盾并治疗自己
 * - 驱散魔法技能会针对有魔法增益效果的玩家
 */

#include "scarlet_monastery.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "Timer.h"

/**
 * @brief 大检察官费尔班克斯法术ID枚举
 *
 * 定义费尔班克斯使用的所有法术ID
 */
enum HighInquisitorFairbanksSpells
{
    SPELL_CURSEOFBLOOD = 8282,      // 鲜血诅咒 - 降低目标的护甲值
    SPELL_DISPEL_MAGIC = 15090,     // 驱散魔法 - 移除目标的魔法效果
    SPELL_FEAR = 12096,             // 恐惧术 - 使目标恐惧逃跑
    SPELL_HEAL = 12039,             // 治疗 - 恢复生命值
    SPELL_POWERWORDSHIELD = 11647,  // 真言术：盾 - 吸收伤害的护盾
    SPELL_SLEEP = 8399              // 睡眠术 - 使目标睡眠
};

/**
 * @brief 大检察官费尔班克斯事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum HighInquisitorFairbanksEvents
{
    EVENT_CURSE_BLOOD = 1, // 鲜血诅咒事件
    EVENT_DIPEL_MAGIC,     // 驱散魔法事件
    EVENT_FEAR,            // 恐惧术事件
    EVENT_HEAL,            // 治疗事件
    EVENT_SLEEP            // 睡眠术事件
};

/**
 * @brief 驱散魔法目标选择器
 *
 * 用于选择合适的目标施放驱散魔法
 * - 检查目标是否为玩家
 * - 检查目标是否在施法距离内
 * - 检查目标是否有可驱散的魔法效果
 */
class HighInquisitorFairbanksDispelMagicTargetSelector
{
public:
    /**
     * @brief 构造函数
     * @param owner 施法者单位
     */
    HighInquisitorFairbanksDispelMagicTargetSelector(Unit* owner) : _me(owner) { }

    /**
     * @brief 目标选择判断运算符
     * @param unit 待检查的单位
     * @return 如果目标符合条件返回true，否则返回false
     *
     * 判断条件：
     * 1. 目标必须是玩家
     * 2. 目标必须在30码范围内
     * 3. 目标必须有可驱散的魔法效果
     */
    bool operator()(Unit* unit) const
    {
        if (unit->GetTypeId() != TYPEID_PLAYER || _me->GetDistance(unit) > 30.f)
            return false;

        DispelChargesList dispelList;
        unit->GetDispellableAuraList(_me, DISPEL_MAGIC, dispelList);
        if (dispelList.empty())
            return false;

        return true;
    }

private:
    Unit const* _me;  // 施法者单位
};

/**
 * @brief 大检察官费尔班克斯BOSS AI结构体
 *
 * 实现费尔班克斯的战斗AI逻辑，包括：
 * - 鲜血诅咒、恐惧、睡眠、驱散魔法等技能
 * - 血量25%以下时的防御和治疗机制
 *
 * 战斗流程：
 * 1. 初始状态为死亡状态，进入战斗时站起来
 * 2. 定期施放鲜血诅咒（25秒冷却）、驱散魔法（30秒冷却）、恐惧术（40秒冷却）、睡眠术（30秒冷却）
 * 3. 血量低于25%时施放真言术：盾并开始治疗自己
 *
 * 特殊机制：
 * - 治疗技能不在事件系统中，而是在DamageTaken中检查触发
 * - 治疗30秒冷却，护盾只施放一次
 */
struct boss_high_inquisitor_fairbanks : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化BOSS AI，设置治疗计时器和护盾标志
     */
    boss_high_inquisitor_fairbanks(Creature* creature) : BossAI(creature, DATA_HIGH_INQUISITOR_FAIRBANKS), _healTimer(0s), _powerWordShield(false) { }

    /**
     * @brief 重置AI状态
     *
     * 当战斗重置时调用：
     * - 重置所有状态变量和事件
     * - 重置治疗计时器
     * - 重置护盾标志
     * - 设置死亡状态（隐藏BOSS特性）
     */
    void Reset() override
    {
        _Reset();
        _healTimer.Reset(0s);
        _powerWordShield = false;
        me->SetStandState(UNIT_STAND_STATE_DEAD);  // 初始为死亡状态
    }

    /**
     * @brief 进入战斗事件
     * @param who 进入战斗的目标
     *
     * 当费尔班克斯进入战斗时：
     * - 安排技能施放事件
     * - 从死亡状态站起来
     *
     * 技能安排：
     * - 鲜血诅咒：10秒后施放
     * - 驱散魔法：30秒后施放
     * - 恐惧术：40秒后施放
     * - 睡眠术：25秒后施放
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_CURSE_BLOOD, 10s);
        events.ScheduleEvent(EVENT_DIPEL_MAGIC, 30s);
        events.ScheduleEvent(EVENT_FEAR, 40s);
        events.ScheduleEvent(EVENT_SLEEP, 25s);
        me->SetStandState(UNIT_STAND_STATE_STAND);  // 站起来进入战斗
    }

    /**
     * @brief 受到伤害事件
     * @param attacker 攻击者
     * @param damage 伤害值（引用，可修改）
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 核心机制：处理费尔班克斯的防御和治疗逻辑
     * - 当血量低于25%时：
     *   1. 施放真言术：盾（只施放一次）
     *   2. 如果不在施法且治疗冷却结束，则治疗自己（30秒冷却）
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (me->HealthBelowPctDamaged(25, damage))
        {
            // 施放真言术：盾（只施放一次）
            if (!_powerWordShield)
            {
                DoCastSelf(SPELL_POWERWORDSHIELD);
                _powerWordShield = true;
            }

            // 如果不在施法且治疗冷却结束，则治疗自己
            if (!me->IsNonMeleeSpellCast(false) && _healTimer.Passed())
            {
                _healTimer.Reset(30s);
                DoCastSelf(SPELL_HEAL);
            }
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
     * 3. 更新治疗计时器
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
            ExecuteEvent(eventId);
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 更新治疗计时器
        if (!_healTimer.Passed())
            _healTimer.Update(diff);

        DoMeleeAttackIfReady();
    }

    /**
     * @brief 执行事件处理器
     * @param eventId 事件ID
     *
     * 处理定时触发的技能施放事件
     */
    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_CURSE_BLOOD:
                DoCastVictim(SPELL_CURSEOFBLOOD);
                events.Repeat(25s);
                break;
            case EVENT_DIPEL_MAGIC:
                // 使用目标选择器选择有魔法增益的玩家
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, HighInquisitorFairbanksDispelMagicTargetSelector(me)))
                    DoCast(target, SPELL_DISPEL_MAGIC);
                events.Repeat(30s);
                break;
            case EVENT_FEAR:
                // 随机选择非当前目标施放恐惧术
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 20.f, true))
                    DoCast(target, SPELL_FEAR);
                events.Repeat(40s);
                break;
            case EVENT_SLEEP:
                // 对最高仇恨目标施放睡眠术
                if (Unit* target = SelectTarget(SelectTargetMethod::MaxThreat, 0, 30.f, true, false))
                    DoCast(target, SPELL_SLEEP);
                events.Repeat(30s);
                break;
            default:
                break;
        }
    }

private:
    TimeTracker _healTimer;    // 治疗计时器，用于30秒冷却
    bool _powerWordShield;     // 是否已施放真言术：盾
};

/**
 * @brief 注册BOSS脚本
 *
 * 将大检察官费尔班克斯的AI注册到脚本系统中
 */
void AddSC_boss_high_inquisitor_fairbanks()
{
    RegisterScarletMonasteryCreatureAI(boss_high_inquisitor_fairbanks);
}
