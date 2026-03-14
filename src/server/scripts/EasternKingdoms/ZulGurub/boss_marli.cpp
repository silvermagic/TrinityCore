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
 * @file boss_marli.cpp
 * @brief 祖尔格拉布副本 - 玛尔里(Boss Mar'li)战斗脚本
 *
 * 本模块实现了祖尔格拉布副本中蜘蛛祭司玛尔里的AI逻辑：
 * - 阶段1(巨魔形态): 召唤蜘蛛、施放毒素齐射、孵化蜘蛛蛋
 * - 阶段2(蜘蛛形态): 变身为蜘蛛,使用纠缠之网、冲锋法力用户
 * - 阶段3(变回巨魔): 切换回巨魔形态,继续施放技能
 * - 特殊机制: 蜘蛛蛋孵化、蜘蛛随从成长机制
 */

#include "zulgurub.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Object.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

/**
 * @brief Boss 对话文本ID
 */
enum Says
{
    SAY_AGGRO                 = 0,    /**< 开战对话 */
    SAY_TRANSFORM             = 1,    /**< 变形对话 */
    SAY_SPIDER_SPAWN          = 2,    /**< 召唤蜘蛛对话 */
    SAY_DEATH                 = 3     /**< 死亡对话 */
};

/**
 * @brief Boss 使用的法术ID
 */
enum Spells
{
    SPELL_CHARGE              = 22911, /**< 冲锋 - 冲向法力用户 */
    SPELL_ASPECT_OF_MARLI     = 24686, /**< 玛尔里的方面 - 眩晕法术 */
    SPELL_ENVOLWINGWEB        = 24110, /**< 纠缠之网 - 定身效果 */
    SPELL_POISON_VOLLEY       = 24099, /**< 毒素齐射 - 范围毒伤害 */
    SPELL_SPIDER_FORM         = 24084, /**< 蜘蛛形态 - 变身为蜘蛛 */
    SPELL_HATCH_EGGS          = 24083, /**< 孵化蛋 - 孵化蜘蛛蛋 */
    SPELL_HATCH_SPIDER_EGG    = 24082, /**< 孵化蜘蛛蛋 - 单个蛋孵化 */

    // 蜘蛛技能
    SPELL_LEVELUP             = 24312  /**< 升级 - 蜘蛛成长技能(可能不是正确的法术) */
};

/**
 * @brief Boss 事件ID
 */
enum Events
{
    EVENT_SPAWN_START_SPIDERS = 1, /**< 召唤起始蜘蛛 - 第一阶段 */
    EVENT_POISON_VOLLEY       = 2, /**< 毒素齐射 - 所有阶段 */
    EVENT_HATCH_SPIDER_EGG    = 3, /**< 孵化蜘蛛蛋 - 所有阶段 */
    EVENT_CHARGE_PLAYER       = 4, /**< 冲锋玩家 - 第三阶段 */
    EVENT_ASPECT_OF_MARLI     = 5, /**< 玛尔里的方面 - 第二阶段 */
    EVENT_TRANSFORM           = 6, /**< 变形 - 第二阶段 */
    EVENT_TRANSFORM_BACK      = 7  /**< 变回巨魔 - 第三阶段 */
};

/**
 * @brief Boss 战斗阶段
 */
enum Phases
{
    PHASE_ONE                 = 1,    /**< 第一阶段 - 召唤蜘蛛 */
    PHASE_TWO                 = 2,    /**< 第二阶段 - 巨魔形态 */
    PHASE_THREE               = 3     /**< 第三阶段 - 蜘蛛形态 */
};

/**
 * @brief 其他常量
 */
enum Misc
{
    NPC_SPIDER                = 15041,    /**< 蜘蛛NPC ID */
    GOB_SPIDER_EGG            = 179985,   /**< 蜘蛛蛋游戏对象ID */
};

/**
 * @brief 伤害修正常量 (临时方案)
 *
 * 注意：这是一个临时解决方案,用于增加/减少玛尔里35%的伤害
 * 可能缺少某个光环效果,需要在后续版本中修复
 */
float const DamageIncrease = 35.0f;
float const DamageDecrease = 100.f / (1.f + DamageIncrease / 100.f) - 100.f;

/**
 * @brief 玛尔里Boss AI
 *
 * 实现玛尔里的战斗逻辑,包括:
 * - 召唤蜘蛛: 从蜘蛛蛋中孵化蜘蛛
 * - 变形机制: 在巨魔和蜘蛛形态之间切换
 * - 伤害调整: 蜘蛛形态下伤害提高35%
 */
struct boss_marli : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_marli(Creature* creature) : BossAI(creature, DATA_MARLI) { }

    /**
     * @brief 重置Boss状态
     *
     * 在战斗重置时调用:
     * - 如果在蜘蛛形态,切换回巨魔形态并减少伤害
     * - 重生所有蜘蛛蛋
     * - 清除所有召唤物
     */
    void Reset() override
    {
        // 如果在第三阶段(蜘蛛形态),需要移除伤害加成
        if (events.IsInPhase(PHASE_THREE))
            me->ApplyStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, DamageDecrease); // 临时方案

        // 重生所有蜘蛛蛋
        std::list<GameObject*> eggs;
        me->GetGameObjectListWithEntryInGrid(eggs, GOB_SPIDER_EGG);
        for (GameObject* egg : eggs)
        {
            egg->Respawn();
            egg->UpdateObjectVisibility(true);
        }

        summons.DespawnAll();
        _Reset();
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
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     *
     * 安排召唤起始蜘蛛事件并说开战对话
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_SPAWN_START_SPIDERS, 1s, 0, PHASE_ONE);
        Talk(SAY_AGGRO);
    }

    /**
     * @brief 召唤生物时调用
     * @param creature 被召唤的生物
     *
     * 让召唤的蜘蛛攻击随机目标
     */
    void JustSummoned(Creature* creature) override
    {
        creature->AI()->AttackStart(SelectTarget(SelectTargetMethod::Random, 0, 0.f, true));
        summons.Summon(creature);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 主要处理事件调度和技能施放:
     * - 第一阶段: 召唤起始蜘蛛
     * - 第二阶段: 毒素齐射、孵化蜘蛛蛋、玛尔里的方面、变形
     * - 第三阶段: 冲锋法力用户
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
                case EVENT_SPAWN_START_SPIDERS:
                    // 召唤起始蜘蛛
                    Talk(SAY_SPIDER_SPAWN);
                    DoCastAOE(SPELL_HATCH_EGGS);

                    // 安排第二阶段技能
                    events.ScheduleEvent(EVENT_ASPECT_OF_MARLI, 12s, 0, PHASE_TWO);
                    events.ScheduleEvent(EVENT_TRANSFORM, 45s, 0, PHASE_TWO);
                    events.ScheduleEvent(EVENT_POISON_VOLLEY, 15s);
                    events.ScheduleEvent(EVENT_HATCH_SPIDER_EGG, 30s);
                    events.ScheduleEvent(EVENT_TRANSFORM, 45s, 0, PHASE_TWO);
                    events.SetPhase(PHASE_TWO);
                    break;

                case EVENT_POISON_VOLLEY:
                    // 对当前目标施放毒素齐射
                    DoCastVictim(SPELL_POISON_VOLLEY, true);
                    events.ScheduleEvent(EVENT_POISON_VOLLEY, 10s, 20s);
                    break;

                case EVENT_ASPECT_OF_MARLI:
                    // 对当前目标施放玛尔里的方面
                    DoCastVictim(SPELL_ASPECT_OF_MARLI, true);
                    events.ScheduleEvent(EVENT_ASPECT_OF_MARLI, 13s, 18s, 0, PHASE_TWO);
                    break;

                case EVENT_HATCH_SPIDER_EGG:
                    // 孵化蜘蛛蛋
                    me->CastSpell(me, SPELL_HATCH_SPIDER_EGG);
                    events.ScheduleEvent(EVENT_HATCH_SPIDER_EGG, 12s, 17s);
                    break;

                case EVENT_TRANSFORM:
                {
                    // 变身为蜘蛛形态
                    Talk(SAY_TRANSFORM);
                    DoCast(me, SPELL_SPIDER_FORM); // 施放变形光环

                    /*
                    // 旧代码: 直接修改武器伤害
                    CreatureTemplate const* cinfo = me->GetCreatureTemplate();
                    me->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, (cinfo->mindmg +((cinfo->mindmg/100) * 35)));
                    me->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, (cinfo->maxdmg +((cinfo->maxdmg/100) * 35)));
                    me->UpdateDamagePhysical(BASE_ATTACK);
                    */

                    // 增加伤害(临时方案)
                    me->ApplyStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, DamageIncrease);

                    // 施放纠缠之网并重置仇恨
                    DoCastVictim(SPELL_ENVOLWINGWEB);
                    if (GetThreat(me->GetVictim()))
                        ModifyThreatByPercent(me->GetVictim(), -100);

                    // 安排第三阶段技能
                    events.ScheduleEvent(EVENT_CHARGE_PLAYER, 1500ms, 0, PHASE_THREE);
                    events.ScheduleEvent(EVENT_TRANSFORM_BACK, 25s, 0, PHASE_THREE);
                    events.CancelEvent(EVENT_HATCH_SPIDER_EGG);
                    events.SetPhase(PHASE_THREE);
                    break;
                }

                case EVENT_CHARGE_PLAYER:
                {
                    // 寻找法力用户并冲锋
                    Unit* target = nullptr;
                    int i = 0;
                    while (i++ < 3) // 最多尝试3次寻找有法力的目标
                    {
                        target = SelectTarget(SelectTargetMethod::Random, 1, 100, true);  // 不是仇恨最高者
                        if (target && target->GetPowerType() == POWER_MANA)
                            break;
                    }
                    if (target)
                    {
                        DoCast(target, SPELL_CHARGE);
                        AttackStart(target);
                    }
                    events.ScheduleEvent(EVENT_CHARGE_PLAYER, 8s, 0, PHASE_THREE);
                    break;
                }

                case EVENT_TRANSFORM_BACK:
                {
                    // 变回巨魔形态
                    me->RemoveAura(SPELL_SPIDER_FORM);

                    /*
                    // 旧代码: 恢复武器伤害
                    CreatureTemplate const* cinfo = me->GetCreatureTemplate();
                    me->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, (cinfo->mindmg +((cinfo->mindmg/100) * 1)));
                    me->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, (cinfo->maxdmg +((cinfo->maxdmg/100) * 1)));
                    me->UpdateDamagePhysical(BASE_ATTACK);
                    */

                    // 减少伤害(临时方案)
                    me->ApplyStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, DamageDecrease);

                    // 重新安排第二阶段技能
                    events.ScheduleEvent(EVENT_ASPECT_OF_MARLI, 12s, 0, PHASE_TWO);
                    events.ScheduleEvent(EVENT_TRANSFORM, 45s, 0, PHASE_TWO);
                    events.ScheduleEvent(EVENT_POISON_VOLLEY, 15s);
                    events.ScheduleEvent(EVENT_HATCH_SPIDER_EGG, 12s, 17s);
                    events.ScheduleEvent(EVENT_TRANSFORM, 35s, 60s, 0, PHASE_TWO);
                    events.SetPhase(PHASE_TWO);
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
};

/**
 * @brief 蜘蛛蛋游戏对象AI
 *
 * 当蜘蛛蛋被孵化时,召唤蜘蛛并通知玛尔里
 */
struct gob_spider_egg : public GameObjectAI
{
    /**
     * @brief 构造函数
     * @param gob 游戏对象指针
     */
    gob_spider_egg(GameObject* gob) : GameObjectAI(gob), _instance(gob->GetInstanceScript()) { }

    /**
     * @brief 召唤生物时调用
     * @param creature 被召唤的生物
     *
     * 通知玛尔里有蜘蛛被召唤,并设置蛋为重生兼容模式
     */
    void JustSummoned(Creature* creature) override
    {
        if (Creature * marli = _instance->GetCreature(DATA_MARLI))
            marli->AI()->JustSummoned(creature);

        me->SetRespawnCompatibilityMode(true);
    }

private:
    InstanceScript* const _instance;  /**< 副本脚本实例 */
};

/**
 * @brief 玛尔里的子嗣AI
 *
 * 被玛尔里召唤的蜘蛛,会不断成长
 */
struct npc_spawn_of_marli : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_spawn_of_marli(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     */
    void Initialize()
    {
        _levelUpTimer = 3000;
    }

    /**
     * @brief 重置AI状态
     */
    void Reset() override
    {
        Initialize();
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 定期施放升级技能,使蜘蛛不断成长
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_levelUpTimer <= diff)
        {
            DoCast(me, SPELL_LEVELUP);
            _levelUpTimer = 3000;
        }
        else
            _levelUpTimer -= diff;

        DoMeleeAttackIfReady();
    }

private:
    uint32 _levelUpTimer;  /**< 升级计时器 */
};

/**
 * @brief 孵化蜘蛛法术脚本
 *
 * 法术ID: 24083 - 孵化蛋
 *
 * 选择距离施法者最近的几个蜘蛛蛋进行孵化
 */
class spell_hatch_spiders : public SpellScript
{
    PrepareSpellScript(spell_hatch_spiders);

    /**
     * @brief 处理目标选择
     * @param targets 目标列表(蜘蛛蛋)
     *
     * 按距离排序,只选择最近的几个蛋
     */
    void HandleObjectAreaTargetSelect(std::list<WorldObject*>& targets)
    {
        targets.sort(Trinity::ObjectDistanceOrderPred(GetCaster()));
        targets.resize(GetSpellInfo()->MaxAffectedTargets);
    }

    /**
     * @brief 注册法术脚本
     */
    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_hatch_spiders::HandleObjectAreaTargetSelect, EFFECT_0, TARGET_GAMEOBJECT_DEST_AREA);
    }
};

/**
 * @brief 注册Boss和NPC脚本
 *
 * 注册以下脚本:
 * - boss_marli: 玛尔里Boss
 * - npc_spawn_of_marli: 玛尔里的子嗣
 * - gob_spider_egg: 蜘蛛蛋
 * - spell_hatch_spiders: 孵化蜘蛛法术
 */
void AddSC_boss_marli()
{
    RegisterZulGurubCreatureAI(boss_marli);
    RegisterZulGurubCreatureAI(npc_spawn_of_marli);
    RegisterZulGurubGameObjectAI(gob_spider_egg);
    RegisterSpellScript(spell_hatch_spiders);
}
