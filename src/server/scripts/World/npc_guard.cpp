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
 * @file    npc_guard.cpp
 * @brief   城市守卫NPC脚本
 *
 * 本文件实现了游戏中各类城市守卫的AI行为。
 * 城市守卫是保护主城的重要NPC，会自动攻击敌对阵营玩家和敌对生物。
 *
 * 主要功能：
 * - 通用城市守卫AI（暴风城卫兵、奥格瑞玛步兵等）
 * - 沙塔斯城阵营守卫（阿尔多或占星者）
 * - 守卫对玩家表情的回应
 * - 守卫的战斗行为（近战、施法、自我治疗）
 *
 * 性能注意事项：
 * - 守卫在城市中数量众多，需要优化AI逻辑
 * - 使用任务调度器管理定时任务
 * - 避免频繁的目标搜索
 */

#include "GuardAI.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"

/**
 * @brief 守卫相关ID定义
 */
enum GuardMisc
{
    SAY_GUARD_SIL_AGGRO = 0,             ///< 对话：塞纳里奥要塞步兵进入战斗

    NPC_CENARION_HOLD_INFANTRY = 15184,  ///< NPC：塞纳里奥要塞步兵
    NPC_STORMWIND_CITY_GUARD = 68,       ///< NPC：暴风城卫兵
    NPC_STORMWIND_CITY_PATROLLER = 1976, ///< NPC：暴风城巡逻兵
    NPC_ORGRIMMAR_GRUNT = 3296,          ///< NPC：奥格瑞玛步兵
    NPC_ALDOR_VINDICATOR = 18549,        ///< NPC：阿尔多捍卫者（沙塔斯）

    SPELL_BANISHED_SHATTRATH_A = 36642,  ///< 法术：放逐沙塔斯（联盟）
    SPELL_BANISHED_SHATTRATH_S = 36671,  ///< 法术：放逐沙塔斯（部落）
    SPELL_BANISH_TELEPORT = 36643,       ///< 法术：放逐传送
    SPELL_EXILE = 39533,                 ///< 法术：流放
};

/**
 * @struct npc_guard_generic
 * @brief 通用城市守卫AI
 *
 * 实现大部分城市守卫的AI行为：
 * - 自动施放增益法术（如自身Buff）
 * - 对玩家表情的回应
 * - 战斗中的近战和法术攻击
 * - 低血量时自我治疗
 *
 * 支持的守卫类型：
 * - 暴风城卫兵/巡逻兵
 * - 奥格瑞玛步兵
 * - 塞纳里奥要塞步兵
 * - 其他通用守卫
 */
struct npc_guard_generic : public GuardAI
{
    /**
     * @brief 构造函数
     * @param creature 生物指针
     *
     * 初始化任务调度器，设置验证条件。
     */
    npc_guard_generic(Creature* creature) : GuardAI(creature)
    {
        // 设置非战斗调度器验证条件：不在施法、不在躲避模式、存活
        _scheduler.SetValidator([this]
        {
            return !me->HasUnitState(UNIT_STATE_CASTING) && !me->IsInEvadeMode() && me->IsAlive();
        });
        // 设置战斗调度器验证条件：不在施法
        _combatScheduler.SetValidator([this]
        {
            return !me->HasUnitState(UNIT_STATE_CASTING);
        });
    }

    /**
     * @brief 重置守卫状态
     *
     * 初始化增益法术施放调度：
     * - 每10分钟尝试施放一次增益法术
     */
    void Reset() override
    {
        _scheduler.CancelAll();
        _combatScheduler.CancelAll();
        _scheduler.Schedule(Seconds(1), [this](TaskContext context)
        {
            // 查找一个以友方为目标并施加光环的法术（通常是增益法术）
            if (SpellInfo const* spellInfo = SelectSpell(me, 0, 0, SELECT_TARGET_ANY_FRIEND, 0, 0, 0, 0, SELECT_EFFECT_AURA))
                DoCast(me, spellInfo->Id);

            context.Repeat(Minutes(10));  // 10分钟后重复
        });
    }

    /**
     * @brief 回应文本表情
     * @param emote 表情类型ID
     *
     * 根据玩家做的表情，守卫做出相应的回应动作。
     */
    void DoReplyToTextEmote(uint32 emote)
    {
        switch (emote)
        {
            case TEXT_EMOTE_KISS:    // 亲吻 -> 鞠躬
                me->HandleEmoteCommand(EMOTE_ONESHOT_BOW);
                break;
            case TEXT_EMOTE_WAVE:    // 挥手 -> 挥手回应
                me->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
                break;
            case TEXT_EMOTE_SALUTE:  // 敬礼 -> 敬礼回应
                me->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
                break;
            case TEXT_EMOTE_SHY:     // 害羞 -> 展示肌肉
                me->HandleEmoteCommand(EMOTE_ONESHOT_FLEX);
                break;
            case TEXT_EMOTE_RUDE:    // 粗鲁 -> 指点
            case TEXT_EMOTE_CHICKEN: // 小鸡 -> 指点
                me->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
                break;
            default:
                break;
        }
    }

    /**
     * @brief 接收表情回调
     * @param player 做表情的玩家
     * @param textEmote 表情类型ID
     *
     * 只有暴风城卫兵、暴风城巡逻兵和奥格瑞玛步兵会回应玩家表情。
     * 必须是友方玩家才会回应。
     */
    void ReceiveEmote(Player* player, uint32 textEmote) override
    {
        switch (me->GetEntry())
        {
            case NPC_STORMWIND_CITY_GUARD:
            case NPC_STORMWIND_CITY_PATROLLER:
            case NPC_ORGRIMMAR_GRUNT:
                break;
            default:
                return;  // 其他守卫不回应表情
        }

        if (!me->IsFriendlyTo(player))
            return;  // 非友方玩家不回应

        DoReplyToTextEmote(textEmote);
    }

    /**
     * @brief 进入战斗回调
     * @param who 进入战斗的目标
     *
     * 初始化战斗行为：
     * - 塞纳里奥要塞步兵会说战斗台词
     * - 调度近战攻击任务（每秒检测）
     * - 调度法术施放任务（每5秒检测）
     */
    void JustEngagedWith(Unit* who) override
    {
        // 塞纳里奥要塞步兵说战斗台词
        if (me->GetEntry() == NPC_CENARION_HOLD_INFANTRY)
            Talk(SAY_GUARD_SIL_AGGRO, who);

        // 近战攻击调度
        _combatScheduler.Schedule(Seconds(1), [this](TaskContext meleeContext)
        {
            Unit* victim = me->GetVictim();
            // 检查攻击准备状态和近战距离
            if (!me->isAttackReady() || !me->IsWithinMeleeRange(victim))
            {
                meleeContext.Repeat();
                return;
            }
            // 20%几率施放技能而不是普通攻击
            if (roll_chance_i(20))
            {
                if (SpellInfo const* spellInfo = SelectSpell(me->GetVictim(), 0, 0, SELECT_TARGET_ANY_ENEMY, 0, 0, 0, NOMINAL_MELEE_RANGE, SELECT_EFFECT_DONTCARE))
                {
                    me->resetAttackTimer();
                    DoCastVictim(spellInfo->Id);
                    meleeContext.Repeat();
                    return;
                }
            }
            // 执行普通攻击
            me->AttackerStateUpdate(victim);
            me->resetAttackTimer();
            meleeContext.Repeat();
        }).Schedule(Seconds(5), [this](TaskContext spellContext)
        {
            bool healing = false;
            SpellInfo const* spellInfo = nullptr;

            // 如果血量低于30%且有33%几率，选择治疗法术
            if (me->HealthBelowPct(30) && roll_chance_i(33))
                spellInfo = SelectSpell(me, 0, 0, SELECT_TARGET_ANY_FRIEND, 0, 0, 0, 0, SELECT_EFFECT_HEALING);

            // 没有治疗法术，检查是否可以施放远程法术
            if (spellInfo)
                healing = true;
            else
                spellInfo = SelectSpell(me->GetVictim(), 0, 0, SELECT_TARGET_ANY_ENEMY, 0, 0, NOMINAL_MELEE_RANGE, 0, SELECT_EFFECT_DONTCARE);

            // 找到法术后施放
            if (spellInfo)
            {
                if (healing)
                    DoCast(me, spellInfo->Id);  // 自我治疗
                else
                    DoCastVictim(spellInfo->Id);  // 攻击敌人
                spellContext.Repeat(Seconds(5));
            }
            else
                spellContext.Repeat(Seconds(1));  // 没有法术，1秒后再试
        });
    }

    /**
     * @brief 更新AI
     * @param diff 距离上次更新的时间间隔（毫秒）
     */
    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);

        if (!UpdateVictim())
            return;

        _combatScheduler.Update(diff);
    }

private:
    TaskScheduler _scheduler;       ///< 非战斗任务调度器（增益法术等）
    TaskScheduler _combatScheduler; ///< 战斗任务调度器（攻击、法术等）
};

/**
 * @struct npc_guard_shattrath_faction
 * @brief 沙塔斯城阵营守卫AI
 *
 * 实现沙塔斯城阿尔多和占星者守卫的特殊行为。
 * 这些守卫会放逐攻击他们的敌对阵营玩家。
 *
 * 机制：
 * - 进入战斗5秒后对玩家施放放逐效果
 * - 9秒后传送玩家到沙塔斯城外
 * - 循环处理直到战斗结束
 *
 * 注意：
 * - 阿尔多守卫对联盟玩家施放联盟版放逐
 * - 占星者守卫对部落玩家施放部落版放逐
 */
struct npc_guard_shattrath_faction : public GuardAI
{
    /**
     * @brief 构造函数
     * @param creature 生物指针
     */
    npc_guard_shattrath_faction(Creature* creature) : GuardAI(creature)
    {
        // 设置调度器验证条件：不在施法
        _scheduler.SetValidator([this]
        {
            return !me->HasUnitState(UNIT_STATE_CASTING);
        });
    }

    /**
     * @brief 重置守卫状态
     */
    void Reset() override
    {
        _scheduler.CancelAll();
    }

    /**
     * @brief 进入战斗回调
     * @param who 进入战斗的目标（未使用）
     *
     * 开始调度放逐流程。
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        ScheduleVanish();
    }

    /**
     * @brief 更新AI
     * @param diff 距离上次更新的时间间隔（毫秒）
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        // 更新调度器，如果没有事件执行则进行近战攻击
        _scheduler.Update(diff, std::bind(&GuardAI::DoMeleeAttackIfReady, this));
    }

    /**
     * @brief 调度放逐流程
     *
     * 实现放逐玩家的循环逻辑：
     * 1. 5秒后对目标施放放逐效果
     * 2. 9秒后施放流放和传送法术
     * 3. 重新开始放逐流程
     *
     * 放逐逻辑：
     * - 阿尔多捍卫者对联盟玩家施放SPELL_BANISHED_SHATTRATH_S（部落版）
     * - 占星者守卫对部落玩家施放SPELL_BANISHED_SHATTRATH_A（联盟版）
     *   这样设计是为了让敌对阵营玩家被放逐
     */
    void ScheduleVanish()
    {
        _scheduler.Schedule(Seconds(5), [this](TaskContext banishContext)
        {
            Unit* temp = me->GetVictim();
            // 检查目标是否为玩家
            if (temp && temp->GetTypeId() == TYPEID_PLAYER)
            {
                // 施放放逐效果
                // 阿尔多捍卫者对联盟玩家施放部落版放逐，占星者对部落玩家施放联盟版放逐
                DoCast(temp, me->GetEntry() == NPC_ALDOR_VINDICATOR ? SPELL_BANISHED_SHATTRATH_S : SPELL_BANISHED_SHATTRATH_A);
                ObjectGuid playerGUID = temp->GetGUID();
                // 9秒后施放流放和传送
                banishContext.Schedule(Seconds(9), [this, playerGUID](TaskContext /*exileContext*/)
                {
                    if (Unit* temp = ObjectAccessor::GetUnit(*me, playerGUID))
                    {
                        temp->CastSpell(temp, SPELL_EXILE, true);           // 流放效果
                        temp->CastSpell(temp, SPELL_BANISH_TELEPORT, true); // 传送出城
                    }
                    ScheduleVanish();  // 重新调度放逐流程
                });
            }
            else
                banishContext.Repeat();  // 目标不是玩家，重新尝试
        });
    }

private:
    TaskScheduler _scheduler;  ///< 任务调度器
};

/**
 * @brief 注册守卫脚本
 *
 * 该函数由脚本系统在启动时调用，用于注册本文件中定义的所有守卫AI。
 */
void AddSC_npc_guard()
{
    RegisterCreatureAI(npc_guard_generic);          // 通用城市守卫
    RegisterCreatureAI(npc_guard_shattrath_faction); // 沙塔斯阵营守卫
}
