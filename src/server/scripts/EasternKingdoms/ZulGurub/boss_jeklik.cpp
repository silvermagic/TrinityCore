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
 * @file boss_jeklik.cpp
 * @brief 祖尔格拉布副本 - 杰利克(Boss Jeklik)战斗脚本
 *
 * 本模块实现了祖尔格拉布副本中蝙蝠祭司杰利克的AI逻辑：
 * - 阶段1(蝙蝠形态): 使用护甲穿刺、血蛭、冲锋、音爆、俯冲等技能
 * - 阶段2(巨魔形态): 使用鲜血诅咒、心灵尖啸、暗言术：痛、精神鞭笞、强力治疗等技能
 * - 特殊机制: 召唤狂热嗜血蝙蝠、召唤蝙蝠骑士
 */

#include "zulgurub.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "TemporarySummon.h"

/**
 * @brief Boss 文本ID
 */
enum Texts
{
    SAY_AGGRO               = 0,    /**< 开战对话 */
    SAY_CALL_RIDERS         = 1,    /**< 召唤蝙蝠骑士对话 */
    SAY_DEATH               = 2,    /**< 死亡对话 */
    EMOTE_SUMMON_BATS       = 3,    /**< 召唤蝙蝠表情 */
    EMOTE_GREAT_HEAL        = 4     /**< 强力治疗表情 */
};

/**
 * @brief Boss 使用的法术ID
 */
enum Spells
{
    // 开场技能
    SPELL_GREEN_CHANNELING       = 13540, /**< 绿色引导 - 开场时的引导动画 */
    SPELL_BAT_FORM               = 23966, /**< 蝙蝠形态 - 变身为蝙蝠 */

    // 第一阶段(蝙蝠形态)
    SPELL_PIERCE_ARMOR           = 12097, /**< 护甲穿刺 - 降低目标护甲 */
    SPELL_BLOOD_LEECH            = 22644, /**< 血蛭 - 吸取生命 */
    SPELL_CHARGE                 = 22911, /**< 冲锋 - 冲向目标 */
    SPELL_SONIC_BURST            = 23918, /**< 音爆 - 范围伤害 */
    SPELL_SWOOP                  = 23919, /**< 俯冲 - 攻击技能 */
    SPELL_SUMMON_BATS            = 23974, /**< 召唤蝙蝠 - 召唤狂热嗜血蝙蝠 */

    // 第二阶段(巨魔形态)
    SPELL_CURSE_OF_BLOOD         = 16098, /**< 鲜血诅咒 - 诅咒效果 */
    SPELL_PSYCHIC_SCREAM         = 22884, /**< 心灵尖啸 - 恐惧效果 */
    SPELL_SHADOW_WORD_PAIN       = 23952, /**< 暗言术：痛 - 持续伤害 */
    SPELL_MIND_FLAY              = 23953, /**< 精神鞭笞 - 引导伤害 */
    SPELL_GREAT_HEAL             = 23954, /**< 强力治疗 - 治疗自己 */

    // 狂热嗜血蝙蝠
    SPELL_ROOT_SELF              = 23973, /**< 自我定身 - 蝙蝠生成时带有此光环 */

    // 古拉巴什蝙蝠骑士
    SPELL_LIQUID_FIRE_PERIODIC   = 23968, /**< 液态火周期触发 - 周期性触发23969 */
    SPELL_LIQUID_FIRE_DAMAGE     = 23970, /**< 液态火伤害 - 推测在23969脚本中使用 */
    SPELL_SUMMON_LIQUID_FIRE     = 23971  /**< 召唤液态火 - 推测在23970脚本中使用 */
};

/**
 * @brief Boss 事件ID
 */
enum Events
{
    EVENT_PIERCE_ARMOR      = 1,    /**< 护甲穿刺事件 */
    EVENT_BLOOD_LEECH,              /**< 血蛭事件 */
    EVENT_CHARGE_JEKLIK,            /**< 冲锋事件 */
    EVENT_SONIC_BURST,              /**< 音爆事件 */
    EVENT_SWOOP,                    /**< 俯冲事件 */
    EVENT_SUMMON_BATS,              /**< 召唤蝙蝠事件 */

    EVENT_CURSE_OF_BLOOD,           /**< 鲜血诅咒事件 */
    EVENT_PSYCHIC_SCREAM,           /**< 心灵尖啸事件 */
    EVENT_SHADOW_WORD_PAIN,         /**< 暗言术：痛事件 */
    EVENT_MIND_FLAY,                /**< 精神鞭笞事件 */
    EVENT_GREAT_HEAL,               /**< 强力治疗事件 */
    EVENT_SPAWN_BAT_RIDER           /**< 召唤蝙蝠骑士事件 */
};

/**
 * @brief Boss 战斗阶段
 */
enum Phase
{
    PHASE_ONE               = 1,    /**< 第一阶段 - 蝙蝠形态 */
    PHASE_TWO               = 2     /**< 第二阶段 - 巨魔形态 */
};

/**
 * @brief 杰利克Boss AI
 *
 * 实现杰利克的战斗逻辑,包括:
 * - 第一阶段(蝙蝠形态): 空中战斗,使用物理技能
 * - 第二阶段(巨魔形态): 地面战斗,使用暗影魔法和治疗技能
 * - 召唤机制: 召唤蝙蝠和蝙蝠骑士
 */
struct boss_jeklik : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_jeklik(Creature* creature) : BossAI(creature, DATA_JEKLIK), _calledRiders(false) { }

    /**
     * @brief 重置Boss状态
     *
     * 在战斗重置时调用:
     * - 施放绿色引导效果(开场动画)
     * - 重置蝙蝠骑士召唤标记
     * - 调用父类的重置方法
     */
    void Reset() override
    {
        DoCastSelf(SPELL_GREEN_CHANNELING);
        _calledRiders = false;
        _Reset();
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     *
     * 初始化战斗状态:
     * - 说开战对话
     * - 切换到第一阶段
     * - 安排第一阶段技能事件
     * - 变身为蝙蝠形态
     * - 启用飞行模式
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);
        events.SetPhase(PHASE_ONE);

        /// @todo: 需要添加开场移动序列
        // 安排第一阶段技能
        events.ScheduleEvent(EVENT_PIERCE_ARMOR, 10s, 20s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_BLOOD_LEECH, 10s, 20s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_CHARGE_JEKLIK, 10s, 25s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_SONIC_BURST, 10s, 25s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_SWOOP, 10s, 15s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_SUMMON_BATS, 40s, 0, PHASE_ONE);

        // 启用飞行模式并变身为蝙蝠
        me->SetDisableGravity(true);
        me->RemoveAurasDueToSpell(SPELL_GREEN_CHANNELING);
        DoCastSelf(SPELL_BAT_FORM, true);
    }

    /**
     * @brief 受到伤害时调用
     * @param attacker 攻击者(未使用)
     * @param damage 伤害值(未使用)
     * @param damageType 伤害类型(未使用)
     * @param spellInfo 法术信息(未使用)
     *
     * 监控血量并触发阶段转换:
     * - 血量低于50%时,切换到第二阶段
     * - 血量低于35%时,召唤蝙蝠骑士
     */
    void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 第一阶段且血量低于50%时,切换到第二阶段
        if (events.IsInPhase(PHASE_ONE) && !HealthAbovePct(50))
        {
            me->RemoveAurasDueToSpell(SPELL_BAT_FORM);
            me->SetDisableGravity(false);
            ResetThreatList();
            events.SetPhase(PHASE_TWO);

            // 安排第二阶段技能
            events.ScheduleEvent(EVENT_CURSE_OF_BLOOD, 10s, 20s, 0, PHASE_TWO);
            events.ScheduleEvent(EVENT_PSYCHIC_SCREAM, 25s, 35s, 0, PHASE_TWO);
            events.ScheduleEvent(EVENT_SHADOW_WORD_PAIN, 10s, 15s, 0, PHASE_TWO);
            events.ScheduleEvent(EVENT_MIND_FLAY, 10s, 30s, 0, PHASE_TWO);
            events.ScheduleEvent(EVENT_GREAT_HEAL, 25s, 0, PHASE_TWO);
        }

        // 血量低于35%时,召唤蝙蝠骑士(只触发一次)
        if (!_calledRiders && !HealthAbovePct(35))
        {
            _calledRiders = true;
            Talk(SAY_CALL_RIDERS);
            //events.ScheduleEvent(EVENT_SPAWN_BAT_RIDER, 0s, 0, PHASE_TWO);
        }
    }

    /**
     * @brief 进入躲避模式时调用
     * @param why 躲避原因(未使用)
     *
     * 清除所有召唤物并消失
     */
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        summons.DespawnAll();
        _DespawnAtEvade();
    }

    /**
     * @brief Boss死亡时调用
     * @param killer 击杀者(未使用)
     *
     * 调用父类的死亡方法并说死亡对话
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 主要处理事件调度和技能施放:
     * - 第一阶段: 护甲穿刺、血蛭、冲锋、音爆、俯冲、召唤蝙蝠
     * - 第二阶段: 鲜血诅咒、心灵尖啸、暗言术：痛、精神鞭笞、强力治疗
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        // 如果正在施法,不执行其他操作
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                // 第一阶段技能
                case EVENT_PIERCE_ARMOR:
                    // 对当前目标施放护甲穿刺
                    DoCastVictim(SPELL_PIERCE_ARMOR);
                    events.Repeat(20s, 30s);
                    break;

                case EVENT_BLOOD_LEECH:
                    // 对当前目标施放血蛭
                    DoCastVictim(SPELL_BLOOD_LEECH);
                    events.Repeat(10s, 20s);
                    break;

                case EVENT_CHARGE_JEKLIK:
                    // 对随机目标施放冲锋
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.f, true))
                        DoCast(target, SPELL_CHARGE);
                    events.Repeat(15s, 30s);
                    break;

                case EVENT_SONIC_BURST:
                    // 对自己施放音爆(范围伤害)
                    DoCastSelf(SPELL_SONIC_BURST);
                    events.Repeat(20s, 30s);
                    break;

                case EVENT_SWOOP:
                    // 对当前目标施放俯冲
                    DoCastVictim(SPELL_SWOOP);
                    events.Repeat(15s, 20s);
                    break;

                case EVENT_SUMMON_BATS:
                    // 召唤狂热嗜血蝙蝠
                    Talk(EMOTE_SUMMON_BATS);
                    DoCastSelf(SPELL_SUMMON_BATS);
                    events.Repeat(1min);
                    break;

                // 第二阶段技能
                case EVENT_CURSE_OF_BLOOD:
                    // 对自己施放鲜血诅咒(范围效果)
                    DoCastSelf(SPELL_CURSE_OF_BLOOD);
                    events.Repeat(25s, 30s);
                    break;

                case EVENT_PSYCHIC_SCREAM:
                    // 对自己施放心灵尖啸(范围恐惧)
                    DoCastSelf(SPELL_PSYCHIC_SCREAM);
                    events.Repeat(35s, 45s);
                    break;

                case EVENT_SHADOW_WORD_PAIN:
                    // 对随机目标施放暗言术：痛
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.f, true))
                        DoCast(target, SPELL_SHADOW_WORD_PAIN);
                    events.Repeat(10s, 20s);
                    break;

                case EVENT_MIND_FLAY:
                    // 对当前目标施放精神鞭笞
                    DoCastVictim(SPELL_MIND_FLAY);
                    events.Repeat(25s, 40s);
                    break;

                case EVENT_GREAT_HEAL:
                    // 治疗自己
                    Talk(EMOTE_GREAT_HEAL);
                    DoCastSelf(SPELL_GREAT_HEAL);
                    events.Repeat(25s);
                    break;

                /// @todo: 应该在以下位置之一生成一个古拉巴什蝙蝠骑士:
                /// -12301.7 -1371.29 145.092 4.74729 或 -12298 -1368.51 145.398 4.79965
                /// 它们是不可交互的、被动的,只是沿路径飞行并投掷炸弹,在路径结束时消失
//              case EVENT_SPAWN_BAT_RIDER:
//                  events.Repeat(10s);
//                  break;

                default:
                    break;
            }

            // 如果正在施法,退出循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    bool _calledRiders;  /**< 是否已经召唤过蝙蝠骑士 */
};

/**
 * @brief 狂热嗜血蝙蝠AI
 *
 * 被杰利克召唤的蝙蝠,简单的近战攻击AI
 * 生成后立即进入战斗
 */
struct npc_frenzied_bloodseeker_bat : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_frenzied_bloodseeker_bat(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 重置AI状态
     *
     * 生成后立即进入战斗
     */
    void Reset() override
    {
        DoZoneInCombat();
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 简单的近战攻击
     */
    void UpdateAI(uint32 /*diff*/) override
    {
        if (!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 注册Boss和NPC脚本
 *
 * 注册以下脚本:
 * - boss_jeklik: 杰利克Boss
 * - npc_frenzied_bloodseeker_bat: 狂热嗜血蝙蝠
 */
void AddSC_boss_jeklik()
{
    RegisterZulGurubCreatureAI(boss_jeklik);
    RegisterZulGurubCreatureAI(npc_frenzied_bloodseeker_bat);
}
