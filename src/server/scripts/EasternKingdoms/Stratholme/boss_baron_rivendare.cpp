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
 * @file    boss_baron_rivendare.cpp
 * @brief   斯坦索姆副本 - 巴隆·瑞文戴尔Boss战斗AI脚本
 *
 * @details 本模块实现了斯坦索姆副本最终Boss巴隆·瑞文戴尔的战斗逻辑:
 *          - 暗影箭、顺劈斩、致死打击等基础技能
 *          - 召唤骷髅随从并通过死亡契约进行治疗
 *          - 与副本事件系统集成(45分钟救援任务)
 *          - 瑞文戴尔是亡灵天灾的重要死亡骑士
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "stratholme.h"

/**
 * @brief Boss文本消息枚举
 * 用于战斗中向玩家显示的提示信息
 */
enum Texts
{
    EMOTE_RAISE_DEAD    = 0,    // %s raises an undead servant back to life! (复活亡灵随从)
    EMOTE_DEATH_PACT    = 1     // %s attempts to cast Death Pact on his servants! (施放死亡契约)
};

/**
 * @brief 法术ID枚举
 * 定义巴隆·瑞文戴尔使用的所有法术技能ID
 */
enum Spells
{
    SPELL_SHADOWBOLT    = 17393,  // 暗影箭 - 远程暗影伤害
    SPELL_CLEAVE        = 15284,  // 顺劈斩 - 对前方多个目标造成物理伤害
    SPELL_MORTALSTRIKE  = 15708,  // 致死打击 - 造成伤害并降低治疗效果
    SPELL_DEATH_PACT_1  = 17698,  // 死亡契约1 - 治疗瑞文戴尔并伤害骷髅
    SPELL_DEATH_PACT_2  = 17471,  // 死亡契约2 - 视觉效果法术
    SPELL_DEATH_PACT_3  = 17472,  // 死亡契约3 - 立即杀死自身(骷髅使用)
    SPELL_RAISE_DEAD    = 17473,  // 复活死者 - 初始化17698和17471
    SPELL_RAISE_DEAD_1  = 17475,  // 复活死者法术1 - 召唤第一个骷髅
    SPELL_RAISE_DEAD_2  = 17476,  // 复活死者法术2 - 召唤第二个骷髅
    SPELL_RAISE_DEAD_3  = 17477,  // 复活死者法术3 - 召唤第三个骷髅
    SPELL_RAISE_DEAD_4  = 17478,  // 复活死者法术4 - 召唤第四个骷髅
    SPELL_RAISE_DEAD_5  = 17479,  // 复活死者法术5 - 召唤第五个骷髅
    SPELL_RAISE_DEAD_6  = 17480,  // 复活死者法术6 - 召唤第六个骷髅
    SPELL_UNHOLY_AURA   = 17467   // 邪恶光环 - 提供暗影伤害加成
};

/**
 * @brief 事件ID枚举
 * 用于Boss AI事件调度系统
 */
enum BaronRivendareEvents
{
    EVENT_SHADOWBOLT    = 1,  // 暗影箭事件
    EVENT_CLEAVE        = 2,  // 顺劈斩事件
    EVENT_MORTALSTRIKE  = 3,  // 致死打击事件
    EVENT_RAISE_DEAD    = 4   // 复活死者事件
};

/**
 * @brief 复活死者法术数组
 * 包含所有召唤骷髅的法术ID,用于批量施放
 */
uint32 const RaiseDeadSpells[6] =
{
    SPELL_RAISE_DEAD_1, SPELL_RAISE_DEAD_2, SPELL_RAISE_DEAD_3,
    SPELL_RAISE_DEAD_4, SPELL_RAISE_DEAD_5, SPELL_RAISE_DEAD_6
};

/**
 * @struct boss_baron_rivendare
 * @brief 巴隆·瑞文戴尔Boss AI
 *
 * @details 实现巴隆·瑞文戴尔的战斗逻辑:
 *          - 战斗技能轮换(暗影箭、顺劈斩、致死打击)
 *          - 召唤骷髅随从机制
 *          - 死亡契约治疗机制
 *          - 与副本状态同步
 */
struct boss_baron_rivendare : public BossAI
{
public:
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_baron_rivendare(Creature* creature) : BossAI(creature, TYPE_BARON), RaiseDead(false) { }

    /**
     * @brief 重置Boss状态
     *
     * @details 在战斗结束或重置时调用:
     *          - 检查拉姆斯登是否已击杀,若是则允许重新开始巴隆战斗
     *          - 重置所有战斗相关状态
     *          - 调用父类Reset完成基础重置
     *
     * @note 此逻辑在副本脚本重写前需要保留
     */
    void Reset() override
    {
        // needed until re-write of instance scripts is done
        // 在副本脚本重写完成前需要此逻辑
        if (instance->GetData(TYPE_RAMSTEIN) == DONE)
            instance->SetData(TYPE_BARON, NOT_STARTED);

        BossAI::Reset();
    }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标(通常为玩家)
     *
     * @details 在Boss进入战斗时调用:
     *          - 更新副本状态为进行中
     *          - 初始化技能事件调度:
     *            * 暗影箭: 5秒后首次施放
     *            * 顺劈斩: 8秒后首次施放
     *            * 致死打击: 12秒后首次施放
     *            * 复活死者: 15秒后首次施放
     *
     * @note 此逻辑在副本脚本重写前需要保留
     */
    void JustEngagedWith(Unit* who) override
    {
        // needed until re-write of instance scripts is done
        // 在副本脚本重写完成前需要此逻辑
        if (instance->GetData(TYPE_BARON) == NOT_STARTED)
            instance->SetData(TYPE_BARON, IN_PROGRESS);

        // 调度战斗技能事件
        events.ScheduleEvent(EVENT_SHADOWBOLT, 5s);
        events.ScheduleEvent(EVENT_CLEAVE, 8s);
        events.ScheduleEvent(EVENT_MORTALSTRIKE, 12s);
        events.ScheduleEvent(EVENT_RAISE_DEAD, 15s);

        BossAI::JustEngagedWith(who);
    }

    /**
     * @brief Boss死亡处理
     * @param killer 击杀者(可为nullptr)
     *
     * @details Boss死亡时调用:
     *          - 设置副本状态为完成
     *          - 触发相关任务完成(如45分钟救援任务)
     *          - 调用父类JustDied处理战利品等
     */
    void JustDied(Unit* killer) override
    {
        // needed until re-write of instance scripts is done
        // 在副本脚本重写完成前需要此逻辑
        instance->SetData(TYPE_BARON, DONE);

        BossAI::JustDied(killer);
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
     *          - 暗影箭: 每10秒对随机目标施放
     *          - 顺劈斩: 7-17秒对当前目标施放
     *          - 致死打击: 10-25秒对当前目标施放
     *          - 复活死者: 每12秒交替召唤骷髅或施放死亡契约
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
                case EVENT_SHADOWBOLT:
                    // 对随机目标施放暗影箭
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_SHADOWBOLT);
                    events.Repeat(10s);
                    break;
                case EVENT_CLEAVE:
                    // 对当前目标施放顺劈斩
                    DoCastVictim(SPELL_CLEAVE);
                    events.Repeat(7s, 17s);
                    break;
                case EVENT_MORTALSTRIKE:
                    // 对当前目标施放致死打击
                    DoCastVictim(SPELL_MORTALSTRIKE);
                    events.Repeat(10s, 25s);
                    break;
                case EVENT_RAISE_DEAD:
                    // 交替执行召唤骷髅或死亡契约
                    if (!RaiseDead)
                    {
                        // 第一次: 召唤6个骷髅随从
                        DoCastSelf(SPELL_RAISE_DEAD);
                        for (uint32 const& summonSkeletons : RaiseDeadSpells)
                            DoCastSelf(summonSkeletons, true);
                        RaiseDead = true;
                        Talk(EMOTE_RAISE_DEAD);
                    }
                    else
                    {
                        // 第二次: 通过死亡契约牺牲骷髅治疗自己
                        RaiseDead = false;
                        Talk(EMOTE_DEATH_PACT);
                    }
                    events.Repeat(12s);
                    break;
                default:
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
    bool RaiseDead;  // 标记当前处于召唤骷髅还是死亡契约阶段
};

/**
 * @struct npc_summoned_skeleton
 * @brief 巴隆召唤的骷髅随从AI
 *
 * @details 骷髅随从的AI逻辑:
 *          - 响应死亡契约法术,被牺牲以治疗巴隆
 *          - 当被巴隆施放死亡契约视觉效果时,自杀
 *
 * @note 死亡契约3(17472)需要由骷髅自己施放
 */
struct npc_summoned_skeleton : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_summoned_skeleton(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 法术命中处理
     * @param caster 施法者对象
     * @param spellInfo 命中的法术信息
     *
     * @details 当骷髅被法术命中时调用:
     *          - 检测是否为死亡契约视觉效果(17471)
     *          - 如果是,则对自己施放死亡契约自杀法术(17472)
     *          - 这会立即杀死骷髅并治疗巴隆
     */
    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        // 检测死亡契约视觉效果
        if (spellInfo->Id == SPELL_DEATH_PACT_2)
            DoCastSelf(SPELL_DEATH_PACT_3, true);  // 自杀以完成死亡契约
    }
};

/**
 * @brief 注册脚本
 *
 * @details 将Boss和骷髅AI注册到脚本系统:
 *          - boss_baron_rivendare: 巴隆·瑞文戴尔Boss AI
 *          - npc_summoned_skeleton: 被召唤的骷髅随从AI
 */
void AddSC_boss_baron_rivendare()
{
    RegisterStratholmeCreatureAI(boss_baron_rivendare);
    RegisterStratholmeCreatureAI(npc_summoned_skeleton);
}
