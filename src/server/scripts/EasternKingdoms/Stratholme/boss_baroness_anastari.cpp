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
 * @file    boss_baroness_anastari.cpp
 * @brief   斯坦索姆副本 - 巴隆夫人·阿纳斯塔里Boss战斗AI脚本
 *
 * @details 本模块实现了斯坦索姆副本巴隆夫人·阿纳斯塔里的战斗逻辑:
 *          - 女妖类型的亡灵Boss,使用暗影和诅咒技能
 *          - 独特的附身机制:控制随机玩家直到其生命值低于50%
 *          - 附身期间Boss进入隐身状态
 *          - 使用女妖哀嚎、女妖诅咒、沉默等技能
 */

#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "stratholme.h"

/**
 * @brief 法术ID枚举
 * 定义巴隆夫人使用的所有法术技能ID
 */
enum Spells
{
    SPELL_BANSHEEWAIL           = 16565,  // 女妖哀嚎 - 暗影伤害法术
    SPELL_BANSHEECURSE          = 16867,  // 女妖诅咒 - 降低目标造成的伤害
    SPELL_SILENCE               = 18327,  // 沉默 - 阻止施法
    SPELL_POSSESS               = 17244,  // 附身 - 对玩家施放的控制效果
    SPELL_POSSESSED             = 17246,  // 被附身 - 玩家身上的伤害递减debuff
    SPELL_POSSESS_INV           = 17250   // 附身隐身 - Boss附身期间进入隐身状态
};

/**
 * @brief 事件ID枚举
 * 用于Boss AI事件调度系统
 */
enum BaronessAnastariEvents
{
    EVENT_SPELL_BANSHEEWAIL     = 1,  // 女妖哀嚎事件
    EVENT_SPELL_BANSHEECURSE    = 2,  // 女妖诅咒事件
    EVENT_SPELL_SILENCE         = 3,  // 沉默事件
    EVENT_SPELL_POSSESS         = 4,  // 附身事件
    EVENT_CHECK_POSSESSED       = 5   // 检查附身状态事件
};

/**
 * @struct boss_baroness_anastari
 * @brief 巴隆夫人·阿纳斯塔里Boss AI
 *
 * @details 实现巴隆夫人的战斗逻辑:
 *          - 常规技能循环(女妖哀嚎、诅咒、沉默)
 *          - 特殊的附身机制
 *          - 附身期间持续监控被附身玩家的状态
 */
struct boss_baroness_anastari : public BossAI
{
public:
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_baroness_anastari(Creature* creature) : BossAI(creature, TYPE_BARONESS) { }

    /**
     * @brief 重置Boss状态
     *
     * @details 在战斗结束或重置时调用:
     *          - 清空被附身目标的GUID
     *          - 移除所有玩家身上的附身效果
     *          - 移除Boss身上的隐身效果
     *          - 重置所有事件计时器
     */
    void Reset() override
    {
        _possessedTargetGuid.Clear();

        // 移除所有玩家身上的附身相关效果
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_POSSESS);
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_POSSESSED);
        me->RemoveAurasDueToSpell(SPELL_POSSESS_INV);

        events.Reset();
    }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标(未使用)
     *
     * @details 在Boss进入战斗时调用:
     *          - 初始化技能事件调度:
     *            * 女妖哀嚎: 1秒后首次施放
     *            * 女妖诅咒: 11秒后首次施放
     *            * 沉默: 13秒后首次施放
     *            * 附身: 20-30秒后首次施放
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        events.ScheduleEvent(EVENT_SPELL_BANSHEEWAIL, 1s);
        events.ScheduleEvent(EVENT_SPELL_BANSHEECURSE, 11s);
        events.ScheduleEvent(EVENT_SPELL_SILENCE, 13s);
        events.ScheduleEvent(EVENT_SPELL_POSSESS, 20s, 30s);
    }

    /**
     * @brief Boss死亡处理
     * @param killer 击杀者(未使用)
     *
     * @details Boss死亡时调用:
     *          - 设置副本状态为进行中(需要等待水晶实现)
     *
     * @note 此逻辑在水晶实现前需要保留,参考 instance_stratholme.cpp 第305行
     */
    void JustDied(Unit* /*killer*/) override
    {
        // needed until crystals implemented,
        // see line 305 instance_stratholme.cpp
        // 在水晶实现前需要此逻辑,参考 instance_stratholme.cpp 第305行
        instance->SetData(TYPE_BARONESS, IN_PROGRESS);
    }

    /**
     * @brief 更新AI
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * @details 主循环逻辑,每帧调用:
     *          1. 检查是否有有效目标
     *          2. 更新事件计时器
     *          3. 处理事件队列,施放对应技能
     *          4. 执行近战攻击
     *
     * @par 技能循环:
     *          - 女妖哀嚎: 每4秒对当前目标施放
     *          - 女妖诅咒: 每18秒对当前目标施放
     *          - 沉默: 每13秒对当前目标施放
     *          - 附身: 20-30秒间隔,随机选择非坦克玩家附身
     *
     * @par 附身机制:
     *          - 选择随机非坦克玩家作为目标
     *          - 施放附身(控制效果)和被附身(伤害debuff)
     *          - Boss进入隐身状态
     *          - 持续监控被附身玩家状态
     *          - 当玩家血量低于50%或附身效果消失时解除附身
     *
     * @note 当Boss正在施法时跳过事件处理,避免打断施法
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效目标
        if (!UpdateVictim())
            return;

        // 更新事件计时器
        events.Update(diff);

        // 如果正在施法,等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理事件队列
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SPELL_BANSHEEWAIL:
                    // 对当前目标施放女妖哀嚎
                    DoCastVictim(SPELL_BANSHEEWAIL);
                    events.Repeat(4s);
                    break;
                case EVENT_SPELL_BANSHEECURSE:
                    // 对当前目标施放女妖诅咒
                    DoCastVictim(SPELL_BANSHEECURSE);
                    events.Repeat(18s);
                    break;
                case EVENT_SPELL_SILENCE:
                    // 对当前目标施放沉默
                    DoCastVictim(SPELL_SILENCE);
                    events.Repeat(13s);
                    break;
                case EVENT_SPELL_POSSESS:
                    // 选择随机非坦克玩家进行附身
                    // 参数: 随机选择, 跳过第0个目标(坦克), 距离0, 仅玩家, 不包括当前目标
                    if (Unit* possessTarget = SelectTarget(SelectTargetMethod::Random, 1, 0, true, false))
                    {
                        // 施放附身效果和控制debuff
                        DoCast(possessTarget, SPELL_POSSESS, true);
                        DoCast(possessTarget, SPELL_POSSESSED, true);
                        // Boss进入隐身状态
                        DoCastSelf(SPELL_POSSESS_INV, true);
                        _possessedTargetGuid = possessTarget->GetGUID();
                        // 立即开始检查附身状态
                        events.ScheduleEvent(EVENT_CHECK_POSSESSED, 0s);
                    }
                    else
                    {
                        // 如果没有可选目标,延迟重试
                        events.Repeat(20s, 30s);
                    }
                    break;
                case EVENT_CHECK_POSSESSED:
                    // 获取被附身的玩家
                    if (Player* possessedTarget = ObjectAccessor::GetPlayer(*me, _possessedTargetGuid))
                    {
                        // 检查是否需要解除附身:
                        // 1. 被附身debuff已消失
                        // 2. 玩家血量低于50%
                        if (!possessedTarget->HasAura(SPELL_POSSESSED) || possessedTarget->HealthBelowPct(50))
                        {
                            // 移除所有附身相关效果
                            possessedTarget->RemoveAurasDueToSpell(SPELL_POSSESS);
                            possessedTarget->RemoveAurasDueToSpell(SPELL_POSSESSED);
                            me->RemoveAurasDueToSpell(SPELL_POSSESS_INV);
                            _possessedTargetGuid.Clear();
                            // 重新调度附身技能
                            events.ScheduleEvent(EVENT_SPELL_POSSESS, 20s, 30s);
                            events.CancelEvent(EVENT_CHECK_POSSESSED);
                        }
                        else
                        {
                            // 继续监控被附身玩家
                            events.Repeat(1s);
                        }
                    }
                    break;
            }

            // 如果开始施法,退出事件处理循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    ObjectGuid _possessedTargetGuid;  // 当前被附身玩家的GUID
};

/**
 * @brief 注册脚本
 *
 * @details 将巴隆夫人Boss AI注册到脚本系统
 */
void AddSC_boss_baroness_anastari()
{
    RegisterStratholmeCreatureAI(boss_baroness_anastari);
}
