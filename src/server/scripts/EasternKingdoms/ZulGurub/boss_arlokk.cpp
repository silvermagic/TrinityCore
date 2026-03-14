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
 * @file boss_arlokk.cpp
 * @brief 祖尔格拉布副本 - 阿洛卡(Boss Arlokk)战斗脚本
 *
 * 本模块实现了祖尔格拉布副本中豹子女祭司阿洛卡的AI逻辑：
 * - 阶段1：巨魔形态，使用暗言术：痛、凿击等技能，召唤豹子
 * - 阶段2：猎豹形态，使用撕裂、消失等技能，提高伤害输出
 * - 特殊机制：标记玩家，被标记的玩家会吸引豹子攻击
 * - 交互机制：敲击铜锣召唤Boss
 */

#include "zulgurub.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"

/**
 * @brief Boss 对话文本ID
 */
enum Says
{
    SAY_AGGRO                   = 0,    /**< 开战对话 */
    SAY_FEAST_PROWLER           = 1,    /**< 标记玩家时的对话 */
    SAY_DEATH                   = 2     /**< 死亡对话 */
};

/**
 * @brief Boss 使用的法术ID
 */
enum Spells
{
    SPELL_SHADOW_WORD_PAIN      = 24212, /**< 暗言术：痛 - 持续伤害 */
    SPELL_GOUGE                 = 12540, /**< 凿击 - 眩晕目标 */
    SPELL_MARK_OF_ARLOKK        = 24210, /**< 阿洛卡的印记 - 标记玩家,触发24211 */
    SPELL_RAVAGE                = 24213, /**< 撕裂 - 猎豹形态技能 */
    SPELL_CLEAVE                = 25174, /**< 顺劈斩 - 正在寻找正确的法术 */
    SPELL_PANTHER_TRANSFORM     = 24190, /**< 猎豹变形 - 变身为猎豹形态 */
    SPELL_SUMMON_PROWLER        = 24246, /**< 召唤潜行者 - 召唤豹子 */
    SPELL_VANISH_VISUAL         = 24222, /**< 消失视觉效果 */
    SPELL_VANISH                = 24223, /**< 消失 - 进入潜行 */
    SPELL_SUPER_INVIS           = 24235  /**< 超级隐形 - 已添加到Spell_dbc */
};

/**
 * @brief Boss 事件ID
 */
enum Events
{
    EVENT_SHADOW_WORD_PAIN      = 1,    /**< 暗言术：痛事件 */
    EVENT_GOUGE                 = 2,    /**< 凿击事件 */
    EVENT_MARK_OF_ARLOKK        = 3,    /**< 标记事件 */
    EVENT_RAVAGE                = 4,    /**< 撕裂事件 */
    EVENT_TRANSFORM             = 5,    /**< 变形事件 */
    EVENT_VANISH                = 6,    /**< 第一次消失事件 */
    EVENT_VANISH_2              = 7,    /**< 第二次消失事件 */
    EVENT_TRANSFORM_BACK        = 8,    /**< 变回巨魔形态事件 */
    EVENT_VISIBLE               = 9,    /**< 显形事件 */
    EVENT_SUMMON_PROWLERS       = 10    /**< 召唤豹子事件 */
};

/**
 * @brief Boss 战斗阶段
 */
enum Phases
{
    PHASE_ALL                   = 0,    /**< 所有阶段共用 */
    PHASE_ONE                   = 1,    /**< 第一阶段 - 巨魔形态 */
    PHASE_TWO                   = 2     /**< 第二阶段 - 猎豹形态 */
};

/**
 * @brief 武器模型ID
 */
enum Weapon
{
    WEAPON_DAGGER               = 10616 /**< 匕首模型 */
};

/**
 * @brief 其他常量
 */
enum Misc
{
    MAX_PROWLERS_PER_SIDE       = 15    /**< 每侧最多豹子数量 */
};

/**
 * @brief Boss 重置时的移动位置
 */
Position const PosMoveOnSpawn[1] =
{
    { -11561.9f, -1627.868f, 41.29941f, 0.0f }
};

/**
 * @brief 伤害修正常量 (临时方案)
 *
 * 注意：这是一个临时解决方案,用于增加/减少阿洛卡35%的伤害
 * 可能缺少某个光环效果,需要在后续版本中修复
 */
float const DamageIncrease = 35.0f;
float const DamageDecrease = 100.f / (1.f + DamageIncrease / 100.f) - 100.f;

/**
 * @brief 阿洛卡Boss AI
 *
 * 实现阿洛卡的战斗逻辑,包括:
 * - 巨魔形态和猎豹形态之间的切换
 * - 召唤豹子攻击玩家
 * - 标记玩家吸引豹子攻击
 * - 消失和重新出现机制
 */
struct boss_arlokk : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_arlokk(Creature* creature) : BossAI(creature, DATA_ARLOKK)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     */
    void Initialize()
    {
        _summonCountA = 0;  // A侧豹子计数
        _summonCountB = 0;  // B侧豹子计数
    }

    /**
     * @brief 重置Boss状态
     *
     * 在战斗重置时调用,恢复Boss到初始状态:
     * - 如果在猎豹形态,切换回巨魔形态并减少伤害
     * - 重新装备匕首
     * - 移动到初始位置
     */
    void Reset() override
    {
        // 如果在猎豹形态,需要移除伤害加成
        if (events.IsInPhase(PHASE_TWO))
            me->ApplyStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, DamageDecrease); // 临时方案

        _Reset();
        Initialize();

        // 装备双匕首
        me->SetVirtualItem(0, uint32(WEAPON_DAGGER));
        me->SetVirtualItem(1, uint32(WEAPON_DAGGER));
        me->SetWalk(false);
        me->GetMotionMaster()->MovePoint(0, PosMoveOnSpawn[0]);
    }

    /**
     * @brief Boss死亡时调用
     * @param killer 击杀者(未使用)
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     *
     * 安排各技能事件:
     * - 暗言术：痛(第一阶段)
     * - 凿击(第一阶段)
     * - 召唤豹子(所有阶段)
     * - 标记玩家(所有阶段)
     * - 变形(第一阶段)
     *
     * 同时初始化豹子刷新点触发器列表
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);

        // 安排第一阶段技能
        events.ScheduleEvent(EVENT_SHADOW_WORD_PAIN, 7s, 9s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_GOUGE, 12s, 15s, 0, PHASE_ONE);

        // 安排所有阶段的技能
        events.ScheduleEvent(EVENT_SUMMON_PROWLERS, 6s, 0, PHASE_ALL);
        events.ScheduleEvent(EVENT_MARK_OF_ARLOKK, 9s, 11s, 0, PHASE_ALL);

        // 安排变形事件
        events.ScheduleEvent(EVENT_TRANSFORM, 15s, 20s, 0, PHASE_ONE);

        Talk(SAY_AGGRO);

        // 设置豹子刷新触发器列表,分为两侧
        std::list<Creature*> triggerList;
        GetCreatureListWithEntryInGrid(triggerList, me, NPC_PANTHER_TRIGGER, 100.0f);
        if (!triggerList.empty())
        {
            uint8 sideA = 0;
            uint8 sideB = 0;
            for (std::list<Creature*>::const_iterator itr = triggerList.begin(); itr != triggerList.end(); ++itr)
            {
                if (Creature* trigger = *itr)
                {
                    // 根据Y坐标区分两侧
                    if (trigger->GetPositionY() < -1625.0f)
                    {
                        _triggersSideAGUID[sideA] = trigger->GetGUID();
                        ++sideA;
                    }
                    else
                    {
                        _triggersSideBGUID[sideB] = trigger->GetGUID();
                        ++sideB;
                    }
                }
            }
        }
    }

    /**
     * @brief 进入躲避模式时调用
     * @param why 躲避原因
     *
     * 重置铜锣的可交互状态,并消失Boss
     */
    void EnterEvadeMode(EvadeReason why) override
    {
        BossAI::EnterEvadeMode(why);

        // 恢复铜锣的可选择状态
        if (GameObject* object = instance->GetGameObject(DATA_GONG_BETHEKK))
            object->RemoveFlag(GO_FLAG_NOT_SELECTABLE);

        // Boss消失
        me->DespawnOrUnsummon(4s);
    }

    /**
     * @brief 设置数据
     * @param id 数据ID(1=A侧, 2=B侧)
     * @param value 数据值(未使用)
     *
     * 用于豹子死亡时减少对应侧的计数
     */
    void SetData(uint32 id, uint32 /*value*/) override
    {
        if (id == 1)
            --_summonCountA;
        else if (id == 2)
            --_summonCountB;
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 主要处理事件调度和技能施放:
     * - 暗言术：痛: 对当前目标施放
     * - 凿击: 对当前目标施放
     * - 召唤豹子: 从两侧触发器召唤豹子
     * - 标记玩家: 标记一个随机玩家
     * - 变形: 变身为猎豹形态并消失
     * - 消失: 在场地中随机移动
     * - 显形: 从猎豹形态变回巨魔形态
     * - 撕裂: 猎豹形态下的攻击技能
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
                case EVENT_SHADOW_WORD_PAIN:
                    // 对当前目标施放暗言术：痛
                    DoCastVictim(SPELL_SHADOW_WORD_PAIN, true);
                    events.ScheduleEvent(EVENT_SHADOW_WORD_PAIN, 5s, 7s, 0, PHASE_ONE);
                    break;

                case EVENT_GOUGE:
                    // 对当前目标施放凿击
                    DoCastVictim(SPELL_GOUGE, true);
                    break;

                case EVENT_SUMMON_PROWLERS:
                    // 从两侧召唤豹子
                    if (_summonCountA < MAX_PROWLERS_PER_SIDE)
                    {
                        // 随机选择A侧触发器召唤豹子
                        if (Unit* trigger = ObjectAccessor::GetUnit(*me, _triggersSideAGUID[urand(0, 4)]))
                        {
                            trigger->CastSpell(trigger, SPELL_SUMMON_PROWLER);
                            ++_summonCountA;
                        }
                    }
                    if (_summonCountB < MAX_PROWLERS_PER_SIDE)
                    {
                        // 随机选择B侧触发器召唤豹子
                        if (Unit* trigger = ObjectAccessor::GetUnit(*me, _triggersSideBGUID[urand(0, 4)]))
                        {
                            trigger->CastSpell(trigger, SPELL_SUMMON_PROWLER);
                            ++_summonCountB;
                        }
                    }
                    events.ScheduleEvent(EVENT_SUMMON_PROWLERS, 6s, 0, PHASE_ALL);
                    break;

                case EVENT_MARK_OF_ARLOKK:
                {
                    // 标记一个随机玩家(优先选择没有印记的目标)
                    Unit* target = SelectTarget(SelectTargetMethod::MaxThreat, urand(1, 3), 0.0f, false, true, -SPELL_MARK_OF_ARLOKK);
                    if (!target)
                        target = me->GetVictim();
                    if (target)
                    {
                        DoCast(target, SPELL_MARK_OF_ARLOKK, true);
                        Talk(SAY_FEAST_PROWLER, target);
                    }
                    events.ScheduleEvent(EVENT_MARK_OF_ARLOKK, 120s, 130s);
                    break;
                }

                case EVENT_TRANSFORM:
                {
                    // 变身为猎豹形态
                    DoCast(me, SPELL_PANTHER_TRANSFORM); // 施放变形光环
                    me->SetVirtualItem(0, uint32(EQUIP_UNEQUIP)); // 卸下武器
                    me->SetVirtualItem(1, uint32(EQUIP_UNEQUIP));

                    // 停止攻击,重置仇恨,进入被动状态
                    me->AttackStop();
                    ResetThreatList();
                    me->SetReactState(REACT_PASSIVE);
                    me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE|UNIT_FLAG_UNINTERACTIBLE);

                    // 施放消失视觉效果
                    DoCast(me, SPELL_VANISH_VISUAL);
                    DoCast(me, SPELL_VANISH);
                    events.ScheduleEvent(EVENT_VANISH, 1s, 0, PHASE_ONE);
                    break;
                }

                case EVENT_VANISH:
                    // 进入隐形状态并随机移动
                    DoCast(me, SPELL_SUPER_INVIS);
                    me->SetWalk(false);
                    // 在场地中随机移动
                    me->GetMotionMaster()->MovePoint(0, frand(-11551.0f, -11508.0f), frand(-1638.0f, -1617.0f), me->GetPositionZ());
                    events.ScheduleEvent(EVENT_VANISH_2, 9s, 0, PHASE_ONE);
                    break;

                case EVENT_VANISH_2:
                    // 保持隐形状态
                    DoCast(me, SPELL_VANISH);
                    DoCast(me, SPELL_SUPER_INVIS);
                    events.ScheduleEvent(EVENT_VISIBLE, 7s, 10s, 0, PHASE_ONE);
                    break;

                case EVENT_VISIBLE:
                    // 显形并切换到猎豹形态的战斗模式
                    me->SetReactState(REACT_AGGRESSIVE);
                    me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE|UNIT_FLAG_UNINTERACTIBLE);

                    // 随机攻击一个目标
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        AttackStart(target);

                    // 移除隐形效果
                    me->RemoveAura(SPELL_SUPER_INVIS);
                    me->RemoveAura(SPELL_VANISH);

                    // 安排第二阶段技能
                    events.ScheduleEvent(EVENT_RAVAGE, 10s, 14s, 0, PHASE_TWO);
                    events.ScheduleEvent(EVENT_TRANSFORM_BACK, 15s, 18s, 0, PHASE_TWO);
                    events.SetPhase(PHASE_TWO);

                    // 增加伤害(临时方案)
                    me->ApplyStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, DamageIncrease);
                    break;

                case EVENT_RAVAGE:
                    // 猎豹形态下的撕裂攻击
                    DoCastVictim(SPELL_RAVAGE, true);
                    events.ScheduleEvent(EVENT_RAVAGE, 10s, 14s, 0, PHASE_TWO);
                    break;

                case EVENT_TRANSFORM_BACK:
                {
                    // 变回巨魔形态
                    me->RemoveAura(SPELL_PANTHER_TRANSFORM); // 移除变形光环
                    DoCast(me, SPELL_VANISH_VISUAL);

                    // 重新装备匕首
                    me->SetVirtualItem(0, uint32(WEAPON_DAGGER));
                    me->SetVirtualItem(1, uint32(WEAPON_DAGGER));

                    // 减少伤害(临时方案)
                    me->ApplyStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, DamageDecrease);

                    // 重新安排第一阶段技能
                    events.ScheduleEvent(EVENT_SHADOW_WORD_PAIN, 4s, 7s, 0, PHASE_ONE);
                    events.ScheduleEvent(EVENT_GOUGE, 12s, 15s, 0, PHASE_ONE);
                    events.ScheduleEvent(EVENT_TRANSFORM, 16s, 20s, 0, PHASE_ONE);
                    events.SetPhase(PHASE_ONE);
                    break;
                }

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
    uint8 _summonCountA;                 /**< A侧当前豹子数量 */
    uint8 _summonCountB;                 /**< B侧当前豹子数量 */
    ObjectGuid _triggersSideAGUID[5];    /**< A侧豹子触发器GUID列表 */
    ObjectGuid _triggersSideBGUID[5];    /**< B侧豹子触发器GUID列表 */
};

/*######
## npc_zulian_prowler - 祖利安潜行者(豹子)
######*/

/**
 * @brief 祖利安潜行者使用的法术ID
 */
enum ZulianProwlerSpells
{
    SPELL_SNEAK_RANK_1_1         = 22766, /**< 潜行等级1(第一个法术) */
    SPELL_SNEAK_RANK_1_2         = 7939,  /**< 潜行等级1(第二个法术) - 已添加到Spell_dbc */
    SPELL_MARK_OF_ARLOKK_TRIGGER = 24211  /**< 阿洛卡的印记触发法术 - 已添加到Spell_dbc */
};

/**
 * @brief 祖利安潜行者事件ID
 */
enum ZulianProwlerEvents
{
    EVENT_ATTACK                 = 1     /**< 攻击事件 */
};

/**
 * @brief 潜行者中心位置
 */
Position const PosProwlerCenter[1] =
{
    { -11556.7f, -1631.344f, 41.2994f, 0.0f }
};

/**
 * @brief 祖利安潜行者AI
 *
 * 被阿洛卡召唤的豹子,具有以下特点:
 * - 生成后处于潜行状态
 * - 会受到阿洛卡印记的影响,优先攻击被标记的玩家
 * - 死亡后会通知阿洛卡减少计数
 */
struct npc_zulian_prowler : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_zulian_prowler(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript())
    {
        _sideData = 0;
    }

    /**
     * @brief 重置AI状态
     *
     * 初始化豹子:
     * - 根据Y坐标确定所在侧(1=A侧, 2=B侧)
     * - 施放潜行技能
     * - 向阿洛卡移动
     */
    void Reset() override
    {
        // 根据Y坐标判断所在侧
        if (me->GetPositionY() < -1625.0f)
            _sideData = 1;
        else
            _sideData = 2;

        // 施放潜行
        DoCast(me, SPELL_SNEAK_RANK_1_1);
        DoCast(me, SPELL_SNEAK_RANK_1_2);

        // 如果阿洛卡还活着,向它移动
        if (Creature* arlokk = _instance->GetCreature(DATA_ARLOKK))
            if (arlokk->IsAlive())
                me->GetMotionMaster()->MovePoint(0, arlokk->GetPosition());

        // 6秒后开始攻击
        _events.ScheduleEvent(EVENT_ATTACK, 6s);
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标(未使用)
     *
     * 清除移动并移除潜行效果
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        me->GetMotionMaster()->Clear();
        me->RemoveAura(SPELL_SNEAK_RANK_1_1);
        me->RemoveAura(SPELL_SNEAK_RANK_1_2);
    }

    /**
     * @brief 被法术击中时调用
     * @param caster 施法者
     * @param spellInfo 法术信息
     *
     * 如果被阿洛卡的印记触发法术击中,立即攻击施法者
     */
    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        Unit* unitCaster = caster->ToUnit();
        if (!unitCaster)
            return;

        // 如果被印记触发法术击中,攻击该玩家(需要视线)
        if (spellInfo->Id == SPELL_MARK_OF_ARLOKK_TRIGGER)
            me->Attack(unitCaster, true);
    }

    /**
     * @brief 死亡时调用
     * @param killer 击杀者(未使用)
     *
     * 通知阿洛卡减少对应侧的豹子计数,然后消失
     */
    void JustDied(Unit* /*killer*/) override
    {
        if (Creature* arlokk = _instance->GetCreature(DATA_ARLOKK))
        {
            if (arlokk->IsAlive())
                arlokk->GetAI()->SetData(_sideData, 0);
        }
        me->DespawnOrUnsummon(4s);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 如果有目标,进行近战攻击;否则等待攻击事件触发
     */
    void UpdateAI(uint32 diff) override
    {
        if (UpdateVictim())
        {
            DoMeleeAttackIfReady();
            return;
        }

        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_ATTACK:
                    // 随机选择一个目标攻击
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0.0f, 100, false))
                        me->Attack(target, true);
                    break;
                default:
                    break;
            }
        }
    }

private:
    int32 _sideData;                    /**< 所在侧数据(1=A侧, 2=B侧) */
    EventMap _events;                   /**< 事件映射表 */
    InstanceScript* _instance;          /**< 副本脚本实例 */
};

/*######
## go_gong_of_bethekk - 贝瑟克之锣
######*/

/**
 * @brief 阿洛卡召唤位置
 */
Position const PosSummonArlokk[1] =
{
    { -11507.22f, -1628.062f, 41.38264f, 3.159046f }
};

/**
 * @brief 贝瑟克之锣AI
 *
 * 玩家点击铜锣后会召唤阿洛卡Boss
 */
struct go_gong_of_bethekk : public GameObjectAI
{
    /**
     * @brief 构造函数
     * @param go 游戏对象指针
     */
    go_gong_of_bethekk(GameObject* go) : GameObjectAI(go) { }

    /**
     * @brief 玩家点击铜锣时调用
     * @param player 玩家对象(未使用)
     * @return 返回true表示处理成功
     *
     * 点击铜锣后:
     * - 设置铜锣为不可选择(防止重复点击)
     * - 播放铜锣动画
     * - 召唤阿洛卡Boss
     */
    bool OnGossipHello(Player* /*player*/) override
    {
        // 设置为不可选择
        me->SetFlag(GO_FLAG_NOT_SELECTABLE);
        // 播放铜锣动画
        me->SendCustomAnim(0);
        // 召唤阿洛卡,10分钟后消失
        me->SummonCreature(NPC_ARLOKK, PosSummonArlokk[0], TEMPSUMMON_DEAD_DESPAWN, 10min);
        return true;
    }
};

void AddSC_boss_arlokk()
{
    RegisterZulGurubCreatureAI(boss_arlokk);
    RegisterZulGurubCreatureAI(npc_zulian_prowler);
    RegisterZulGurubGameObjectAI(go_gong_of_bethekk);
}
