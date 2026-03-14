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
 * @file    boss_prince_malchezaar.cpp
 * @brief   卡拉赞副本 - 麦克扎尔王子(Prince Malchezaar)首领战AI实现
 * @details 实现麦克扎尔王子的战斗逻辑,包括:
 *          - 三阶段战斗机制
 *          - 阶段1:单手武器阶段,施放暗言术痛和虚弱效果
 *          - 阶段2:双斧阶段,装备双斧并施放破甲和顺劈斩
 *          - 阶段3:投掷斧头阶段,投掷双斧攻击随机目标
 *          - 地狱火召唤机制,在固定位置召唤地狱火
 *          - 虚弱效果将玩家生命值降为1并在结束时恢复
 */

/* ScriptData
SDName: Boss_Prince_Malchezzar
SD%Complete: 100
SDComment:
SDCategory: Karazhan
EndScriptData */

#include "ScriptMgr.h"
#include "Containers.h"
#include "karazhan.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"

/**
 * @struct InfernalPoint
 * @brief 地狱火生成点坐标结构体
 */
struct InfernalPoint
{
    float x, y; ///< X和Y坐标
};

#define INFERNAL_Z  275.5f ///< 地狱火的固定Z坐标

/**
 * @brief 地狱火生成点的坐标数组
 * @details 定义了18个地狱火可能生成的位置点
 */
static InfernalPoint InfernalPoints[] =
{
    {-10922.8f, -1985.2f},
    {-10916.2f, -1996.2f},
    {-10932.2f, -2008.1f},
    {-10948.8f, -2022.1f},
    {-10958.7f, -1997.7f},
    {-10971.5f, -1997.5f},
    {-10990.8f, -1995.1f},
    {-10989.8f, -1976.5f},
    {-10971.6f, -1973.0f},
    {-10955.5f, -1974.0f},
    {-10939.6f, -1969.8f},
    {-10958.0f, -1952.2f},
    {-10941.7f, -1954.8f},
    {-10943.1f, -1988.5f},
    {-10948.8f, -2005.1f},
    {-10984.0f, -2019.3f},
    {-10932.8f, -1979.6f},
    {-10935.7f, -1996.0f}
};

/**
 * @brief 麦克扎尔王子相关枚举定义
 * @details 虚弱效果(Enfeeble)设计说明:
 *          应该将玩家生命值降为1,并在结束时恢复到满生命值
 *          同时将治疗和恢复降低到0%
 *          当前法术效果仅降低治疗效果
 */
enum PrinceMalchezaar
{
    // 对话文本ID
    SAY_AGGRO                   = 0,  ///< 进入战斗时喊话
    SAY_AXE_TOSS1               = 1,  ///< 阶段2装备斧头时喊话
    SAY_AXE_TOSS2               = 2,  ///< 阶段3投掷斧头时喊话
//  SAY_SPECIAL1                = 3,  ///< 未使用,需要实现,但不确定使用场景
//  SAY_SPECIAL2                = 4,  ///< 未使用,需要实现,但不确定使用场景
//  SAY_SPECIAL3                = 5,  ///< 未使用,需要实现,但不确定使用场景
    SAY_SLAY                    = 6,  ///< 击杀玩家时喊话
    SAY_SUMMON                  = 7,  ///< 召唤地狱火时喊话
    SAY_DEATH                   = 8,  ///< 死亡时喊话

    TOTAL_INFERNAL_POINTS       = 18, ///< 地狱火生成点总数

    // 法术ID
    SPELL_ENFEEBLE              = 30843, ///< 虚弱效果 - 阶段1和2使用
    SPELL_ENFEEBLE_EFFECT       = 41624, ///< 虚弱效果应用法术

    SPELL_SHADOWNOVA            = 30852, ///< 暗影新星 - 所有阶段使用
    SPELL_SW_PAIN               = 30854, ///< 暗言术:痛 - 阶段1和3使用(目标选择规则不同)
    SPELL_THRASH_PASSIVE        = 12787, ///< 殴打被动 - 阶段2额外攻击几率
    SPELL_SUNDER_ARMOR          = 30901, ///< 破甲 - 阶段2使用
    SPELL_THRASH_AURA           = 12787, ///< 殴打光环 - 殴打的被动触发几率
    SPELL_EQUIP_AXES            = 30857, ///< 装备斧头 - 装备斧头的视觉效果
    SPELL_AMPLIFY_DAMAGE        = 39095, ///< 放大伤害 - 阶段3使用
    SPELL_CLEAVE                = 30131, ///< 顺劈斩 - 与夜之魇相同
    SPELL_HELLFIRE              = 30859, ///< 地狱火 - 地狱火的献祭光环
    NETHERSPITE_INFERNAL        = 17646, ///< 虚空幽龙地狱火生物ID
    MALCHEZARS_AXE              = 17650, ///< 麦克扎尔之斧生物ID - 阶段3召唤的斧头

    INFERNAL_MODEL_INVISIBLE    = 11686, ///< 地狱火隐形模型ID - 地狱火效果
    SPELL_INFERNAL_RELAY        = 30834, ///< 地狱火中继法术

    EQUIP_ID_AXE                = 33542 ///< 斧头装备ID
};

/**
 * @class netherspite_infernal
 * @brief 虚空幽龙地狱火生物脚本
 * @details 处理地狱火的AI逻辑,包括地狱火光环和自动清理机制
 */
class netherspite_infernal : public CreatureScript
{
public:
    netherspite_infernal() : CreatureScript("netherspite_infernal") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<netherspite_infernalAI>(creature);
    }

    /**
     * @struct netherspite_infernalAI
     * @brief 虚空幽龙地狱火AI结构体
     * @details 管理地狱火的行为,包括延迟施放地狱火光环和定时消失
     */
    struct netherspite_infernalAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        netherspite_infernalAI(Creature* creature) : ScriptedAI(creature),
            HellfireTimer(0), CleanupTimer(0), point(nullptr) { }

        uint32 HellfireTimer;      ///< 地狱火施放计时器
        uint32 CleanupTimer;       ///< 清理计时器
        ObjectGuid malchezaar;     ///< 麦克扎尔王子的GUID
        InfernalPoint *point;      ///< 地狱火生成的位置点

        void Reset() override { }
        void JustEngagedWith(Unit* /*who*/) override { }
        void MoveInLineOfSight(Unit* /*who*/) override { }

        /**
         * @brief AI更新函数
         * @param diff 距离上次调用的时间间隔(毫秒)
         * @details 管理地狱火光环的施放和自动清理
         */
        void UpdateAI(uint32 diff) override
        {
            if (HellfireTimer)
            {
                if (HellfireTimer <= diff)
                {
                    DoCast(me, SPELL_HELLFIRE);
                    HellfireTimer = 0;
                }
                else HellfireTimer -= diff;
            }

            if (CleanupTimer)
            {
                if (CleanupTimer <= diff)
                {
                    Cleanup();
                    CleanupTimer = 0;
                } else CleanupTimer -= diff;
            }
        }

        /**
         * @brief 击杀单位时调用
         * @param who 被击杀的单位
         * @details 将击杀事件传递给麦克扎尔王子,以便Boss喊话
         */
        void KilledUnit(Unit* who) override
        {
            if (Unit* unit = ObjectAccessor::GetUnit(*me, malchezaar))
                if (Creature* creature = unit->ToCreature())
                    creature->AI()->KilledUnit(who);
        }

        /**
         * @brief 被法术击中时调用
         * @param caster 施法者
         * @param spellInfo 法术信息
         * @details 当被地狱火中继法术击中时,显示模型并开始地狱火光环
         */
        void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
        {
            if (spellInfo->Id == SPELL_INFERNAL_RELAY)
            {
                me->SetDisplayId(me->GetUInt32Value(UNIT_FIELD_NATIVEDISPLAYID));
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                HellfireTimer = 4000;  ///< 4秒后开始地狱火
                CleanupTimer = 170000; ///< 170秒后自动清理
            }
        }

        /**
         * @brief 受到伤害时调用
         * @param done_by 伤害来源
         * @param damage 伤害值(可修改)
         * @param damageType 伤害类型
         * @param spellInfo 法术信息
         * @details 只有麦克扎尔王子可以伤害地狱火(用于清理机制)
         */
        void DamageTaken(Unit* done_by, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            if (!done_by || done_by->GetGUID() != malchezaar)
                damage = 0;
        }

        void Cleanup();
    };
};

/**
 * @class boss_malchezaar
 * @brief 麦克扎尔王子首领脚本类
 * @details 注册和管理麦克扎尔王子的AI实例
 */
class boss_malchezaar : public CreatureScript
{
public:
    boss_malchezaar() : CreatureScript("boss_malchezaar") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_malchezaarAI>(creature);
    }

    /**
     * @struct boss_malchezaarAI
     * @brief 麦克扎尔王子AI结构体
     * @details 实现三阶段战斗逻辑:
     *          - 阶段1(100%-60%):单手武器,虚弱和暗言术痛
     *          - 阶段2(60%-30%):双斧阶段,破甲和顺劈斩
     *          - 阶段3(30%-0%):投掷斧头,放大伤害和更多地狱火
     */
    struct boss_malchezaarAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_malchezaarAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();

            instance = creature->GetInstanceScript();
        }

        /**
         * @brief 初始化成员变量
         * @details 设置技能计时器和状态标志的初始值
         */
        void Initialize()
        {
            EnfeebleTimer = 30000;        ///< 虚弱效果计时器 - 30秒
            EnfeebleResetTimer = 38000;   ///< 虚弱重置计时器 - 38秒
            ShadowNovaTimer = 35500;      ///< 暗影新星计时器 - 35.5秒
            SWPainTimer = 20000;          ///< 暗言术痛计时器 - 20秒
            AmplifyDamageTimer = 5000;    ///< 放大伤害计时器 - 5秒
            Cleave_Timer = 8000;          ///< 顺劈斩计时器 - 8秒
            InfernalTimer = 40000;        ///< 地狱火计时器 - 40秒
            InfernalCleanupTimer = 47000; ///< 地狱火清理计时器 - 47秒
            AxesTargetSwitchTimer = urand(7500, 20000); ///< 斧头目标切换计时器
            SunderArmorTimer = urand(5000, 10000);      ///< 破甲计时器
            phase = 1; ///< 当前阶段

            for (uint8 i = 0; i < 5; ++i)
            {
                enfeeble_targets[i].Clear();
                enfeeble_health[i] = 0;
            }
        }

        InstanceScript* instance;     ///< 副本实例脚本指针
        uint32 EnfeebleTimer;         ///< 虚弱效果施放计时器
        uint32 EnfeebleResetTimer;    ///< 虚弱效果重置计时器
        uint32 ShadowNovaTimer;       ///< 暗影新星计时器
        uint32 SWPainTimer;           ///< 暗言术痛计时器
        uint32 SunderArmorTimer;      ///< 破甲计时器
        uint32 AmplifyDamageTimer;    ///< 放大伤害计时器
        uint32 Cleave_Timer;          ///< 顺劈斩计时器
        uint32 InfernalTimer;         ///< 地狱火召唤计时器
        uint32 AxesTargetSwitchTimer; ///< 斧头目标切换计时器
        uint32 InfernalCleanupTimer;  ///< 地狱火清理计时器

        GuidVector infernals;                   ///< 活跃地狱火的GUID列表
        std::vector<InfernalPoint*> positions;  ///< 可用的地狱火生成位置列表

        ObjectGuid axes[2];             ///< 两把斧头的GUID
        ObjectGuid enfeeble_targets[5]; ///< 虚弱效果目标的GUID(最多5个)
        uint64 enfeeble_health[5];      ///< 虚弱效果前的生命值(用于恢复)

        uint32 phase; ///< 当前战斗阶段(1/2/3)

        /**
         * @brief 重置Boss状态
         * @details 清理所有召唤物,武器和地狱火,重置副本状态
         *          调用时机:战斗结束或重置
         */
        void Reset() override
        {
            AxesCleanup();
            ClearWeapons();
            InfernalCleanup();
            positions.clear();

            Initialize();

            for (uint8 i = 0; i < TOTAL_INFERNAL_POINTS; ++i)
                positions.push_back(&InfernalPoints[i]);

            instance->HandleGameObject(instance->GetGuidData(DATA_GO_NETHER_DOOR), true);
            instance->SetBossState(DATA_MALCHEZZAR, NOT_STARTED);
        }

        /**
         * @brief 击杀单位时调用
         * @param victim 被击杀的单位
         */
        void KilledUnit(Unit* /*victim*/) override
        {
            Talk(SAY_SLAY);
        }

        /**
         * @brief Boss死亡时调用
         * @param killer 击杀者
         */
        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_DEATH);

            AxesCleanup();
            ClearWeapons();
            InfernalCleanup();
            positions.clear();

            for (uint8 i = 0; i < TOTAL_INFERNAL_POINTS; ++i)
                positions.push_back(&InfernalPoints[i]);

            instance->HandleGameObject(instance->GetGuidData(DATA_GO_NETHER_DOOR), true);
            instance->SetBossState(DATA_MALCHEZZAR, DONE);
        }

        /**
         * @brief 进入战斗时调用
         * @param who 仇恨目标
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            Talk(SAY_AGGRO);

            instance->HandleGameObject(instance->GetGuidData(DATA_GO_NETHER_DOOR), false); // 打开通往更深处的门
            instance->SetBossState(DATA_MALCHEZZAR, IN_PROGRESS);
        }

        /**
         * @brief 清理所有地狱火
         * @details 移除所有活跃的地狱火生物
         */
        void InfernalCleanup()
        {
            // 地狱火清理
            for (GuidVector::const_iterator itr = infernals.begin(); itr != infernals.end(); ++itr)
                if (Unit* pInfernal = ObjectAccessor::GetUnit(*me, *itr))
                    if (pInfernal->IsAlive())
                    {
                        pInfernal->SetVisible(false);
                        pInfernal->setDeathState(JUST_DIED);
                    }

            infernals.clear();
        }

        /**
         * @brief 清理斧头
         * @details 移除阶段3召唤的两把斧头
         */
        void AxesCleanup()
        {
            for (uint8 i = 0; i < 2; ++i)
            {
                Unit* axe = ObjectAccessor::GetUnit(*me, axes[i]);
                if (axe && axe->IsAlive())
                    axe->KillSelf();
                axes[i].Clear();
            }
        }

        /**
         * @brief 清除武器装备
         * @details 移除Boss的武器,关闭双持模式
         */
        void ClearWeapons()
        {
            SetEquipmentSlots(false, EQUIP_UNEQUIP, EQUIP_UNEQUIP, EQUIP_NO_CHANGE);
            me->SetCanDualWield(false);
        }

        /**
         * @brief 施放虚弱效果
         * @details 选择最多5个非坦克玩家,将其生命值降为1并记录原始生命值
         *          调用时机:阶段1和2每30秒
         */
        void EnfeebleHealthEffect()
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_ENFEEBLE_EFFECT);
            if (!info)
                return;

            Unit* tank = me->GetThreatManager().GetCurrentVictim();
            std::vector<Unit*> targets;

            // 从威胁列表中选择非坦克的玩家目标
            for (ThreatReference const* ref : me->GetThreatManager().GetUnsortedThreatList())
            {
                Unit* target = ref->GetVictim();
                if (target != tank && target->IsAlive() && target->GetTypeId() == TYPEID_PLAYER)
                    targets.push_back(target);
            }

            if (targets.empty())
              return;

            // 如果超过5个目标,随机移除多余的
            while (targets.size() > 5)
                targets.erase(targets.begin() + rand32() % targets.size());

            uint32 i = 0;
            for (std::vector<Unit*>::const_iterator iter = targets.begin(); iter != targets.end(); ++iter, ++i)
                if (Unit* target = *iter)
                {
                    enfeeble_targets[i] = target->GetGUID();
                    enfeeble_health[i] = target->GetHealth(); // 保存原始生命值

                    // 施放虚弱效果并将生命值降为1
                    CastSpellExtraArgs args;
                    args.TriggerFlags = TRIGGERED_FULL_MASK;
                    args.OriginalCaster = me->GetGUID();
                    target->CastSpell(target, SPELL_ENFEEBLE, args);
                    target->SetHealth(1);
                }
        }

        /**
         * @brief 重置虚弱效果的生命值
         * @details 虚弱效果结束时,将玩家的生命值恢复到施放前的值
         */
        void EnfeebleResetHealth()
        {
            for (uint8 i = 0; i < 5; ++i)
            {
                Unit* target = ObjectAccessor::GetUnit(*me, enfeeble_targets[i]);
                if (target && target->IsAlive())
                    target->SetHealth(enfeeble_health[i]);
                enfeeble_targets[i].Clear();
                enfeeble_health[i] = 0;
            }
        }

        /**
         * @brief 召唤地狱火
         * @param diff 时间差(未使用)
         * @details 在随机位置召唤地狱火,优先使用预设的18个位置
         *          调用时机:阶段1和2每45秒,阶段3每15秒
         */
        void SummonInfernal(const uint32 /*diff*/)
        {
            InfernalPoint *point = nullptr;
            Position pos;
            if ((me->GetMapId() != 532) || positions.empty())
                pos = me->GetRandomNearPosition(60); // 非卡拉赞地图或无可用位置时随机生成
            else
            {
                // 从可用位置中随机选择一个
                point = Trinity::Containers::SelectRandomContainerElement(positions);
                pos.Relocate(point->x, point->y, INFERNAL_Z, frand(0.0f, float(M_PI * 2)));
            }

            Creature* infernal = me->SummonCreature(NETHERSPITE_INFERNAL, pos, TEMPSUMMON_TIMED_DESPAWN, 3min);

            if (infernal)
            {
                infernal->SetDisplayId(INFERNAL_MODEL_INVISIBLE);
                infernal->SetFaction(me->GetFaction());
                if (point)
                    ENSURE_AI(netherspite_infernal::netherspite_infernalAI, infernal->AI())->point = point;
                ENSURE_AI(netherspite_infernal::netherspite_infernalAI, infernal->AI())->malchezaar = me->GetGUID();

                infernals.push_back(infernal->GetGUID());
                DoCast(infernal, SPELL_INFERNAL_RELAY);
            }

            Talk(SAY_SUMMON);
        }

        /**
         * @brief AI更新函数,每帧调用
         * @param diff 距离上次调用的时间间隔(毫秒)
         * @details 处理三阶段战斗逻辑:
         *          - 阶段切换检测
         *          - 各阶段专属技能
         *          - 地狱火召唤
         *          - 虚弱效果管理
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            // 虚弱效果生命值重置检查
            if (EnfeebleResetTimer && EnfeebleResetTimer <= diff)
            {
                EnfeebleResetHealth();
                EnfeebleResetTimer = 0;
            } else EnfeebleResetTimer -= diff;

            // 阶段2转换时Boss会眩晕自己
            if (me->HasUnitState(UNIT_STATE_STUNNED))
                return;

            // 确保Boss始终以当前坦克为目标
            if (me->GetVictim() && me->GetTarget() != me->EnsureVictim()->GetGUID())
                me->SetTarget(me->EnsureVictim()->GetGUID());

            // 阶段1逻辑 (100%-60%血量)
            if (phase == 1)
            {
                if (HealthBelowPct(60))
                {
                    me->InterruptNonMeleeSpells(false);

                    phase = 2;

                    // 播放装备斧头动画
                    DoCast(me, SPELL_EQUIP_AXES);

                    // 喊话
                    Talk(SAY_AXE_TOSS1);

                    // 施放殴打被动光环
                    DoCast(me, SPELL_THRASH_AURA, true);

                    // 装备双斧模型
                    SetEquipmentSlots(false, EQUIP_ID_AXE, EQUIP_ID_AXE, EQUIP_NO_CHANGE);

                    // 调整副手攻击速度为基础攻击速度的150%
                    me->SetAttackTime(OFF_ATTACK, (me->GetAttackTime(BASE_ATTACK)*150)/100);
                    me->SetCanDualWield(true);
                }
            }
            // 阶段2逻辑 (60%-30%血量)
            else if (phase == 2)
            {
                if (HealthBelowPct(30))
                {
                    InfernalTimer = 15000; // 阶段3地狱火召唤间隔更短

                    phase = 3;

                    ClearWeapons();

                    // 移除殴打光环
                    me->RemoveAurasDueToSpell(SPELL_THRASH_AURA);

                    Talk(SAY_AXE_TOSS2);

                    // 召唤两把飞斧
                    Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true);
                    for (uint8 i = 0; i < 2; ++i)
                    {
                        Creature* axe = me->SummonCreature(MALCHEZARS_AXE, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 1s);
                        if (axe)
                        {
                            axe->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                            axe->SetFaction(me->GetFaction());
                            axes[i] = axe->GetGUID();
                            if (target)
                            {
                                axe->AI()->AttackStart(target);
                                AddThreat(target, 10000000.0f, axe);
                            }
                        }
                    }

                    // 调整暗影新星计时器
                    if (ShadowNovaTimer > 35000)
                        ShadowNovaTimer = EnfeebleTimer + 5000;

                    return;
                }

                // 破甲技能 - 阶段2专属
                if (SunderArmorTimer <= diff)
                {
                    DoCastVictim(SPELL_SUNDER_ARMOR);
                    SunderArmorTimer = urand(10000, 18000);
                } else SunderArmorTimer -= diff;

                // 顺劈斩 - 阶段2专属
                if (Cleave_Timer <= diff)
                {
                    DoCastVictim(SPELL_CLEAVE);
                    Cleave_Timer = urand(6000, 12000);
                } else Cleave_Timer -= diff;
            }
            // 阶段3逻辑 (30%-0%血量)
            else
            {
                // 斧头目标切换
                if (AxesTargetSwitchTimer <= diff)
                {
                    AxesTargetSwitchTimer = urand(7500, 20000);

                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                    {
                        for (uint8 i = 0; i < 2; ++i)
                        {
                            if (Unit* axe = ObjectAccessor::GetUnit(*me, axes[i]))
                            {
                                if (axe->GetVictim())
                                    ResetThreat(axe->GetVictim(), axe);
                                AddThreat(target, 1000000.0f, axe);
                            }
                        }
                    }
                } else AxesTargetSwitchTimer -= diff;

                // 放大伤害 - 阶段3专属
                if (AmplifyDamageTimer <= diff)
                {
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                        DoCast(target, SPELL_AMPLIFY_DAMAGE);
                    AmplifyDamageTimer = urand(20000, 30000);
                } else AmplifyDamageTimer -= diff;
            }

            // 全局计时器 - 地狱火召唤
            if (InfernalTimer <= diff)
            {
                SummonInfernal(diff);
                InfernalTimer = phase == 3 ? 14500 : 44500;    // 阶段3每15秒,其他阶段每45秒
            } else InfernalTimer -= diff;

            // 暗影新星 - 虚弱效果后5秒施放
            if (ShadowNovaTimer <= diff)
            {
                DoCastVictim(SPELL_SHADOWNOVA);
                ShadowNovaTimer = phase == 3 ? 31000 : uint32(-1); // 阶段3循环施放,其他阶段仅虚弱后
            } else ShadowNovaTimer -= diff;

            // 暗言术:痛 - 阶段1和3使用
            if (phase != 2)
            {
                if (SWPainTimer <= diff)
                {
                    Unit* target = nullptr;
                    if (phase == 1)
                        target = me->GetVictim();        // 阶段1只对坦克施放
                    else                                          // 阶段3对随机非坦克施放
                        target = SelectTarget(SelectTargetMethod::Random, 1, 100, true);

                    if (target)
                        DoCast(target, SPELL_SW_PAIN);

                    SWPainTimer = 20000;
                } else SWPainTimer -= diff;
            }

            // 虚弱效果 - 阶段1和2使用
            if (phase != 3)
            {
                if (EnfeebleTimer <= diff)
                {
                    EnfeebleHealthEffect();
                    EnfeebleTimer = 30000;
                    ShadowNovaTimer = 5000;     // 虚弱后5秒施放暗影新星
                    EnfeebleResetTimer = 9000;  // 9秒后恢复生命值
                } else EnfeebleTimer -= diff;
            }

            // 阶段2使用双持攻击,其他阶段使用普通攻击
            if (phase == 2)
                DoMeleeAttacksIfReady();
            else
                DoMeleeAttackIfReady();
        }

        /**
         * @brief 执行双持近战攻击
         * @details 分别检查主手和副手的攻击就绪状态并执行攻击
         *          用于阶段2的双斧战斗
         */
        void DoMeleeAttacksIfReady()
        {
            if (me->IsWithinMeleeRange(me->GetVictim()) && !me->IsNonMeleeSpellCast(false))
            {
                // 检查主手攻击
                if (me->isAttackReady() && me->GetVictim())
                {
                    me->AttackerStateUpdate(me->GetVictim());
                    me->resetAttackTimer();
                }
                // 检查副手攻击
                if (me->isAttackReady(OFF_ATTACK) && me->GetVictim())
                {
                    me->AttackerStateUpdate(me->GetVictim(), OFF_ATTACK);
                    me->resetAttackTimer(OFF_ATTACK);
                }
            }
        }

        /**
         * @brief 清理地狱火
         * @param infernal 地狱火生物指针
         * @param point 地狱火的位置点
         * @details 从活跃地狱火列表中移除,并将位置点归还到可用位置池
         */
        void Cleanup(Creature* infernal, InfernalPoint *point)
        {
            for (GuidVector::iterator itr = infernals.begin(); itr!= infernals.end(); ++itr)
            {
                if (*itr == infernal->GetGUID())
                {
                    infernals.erase(itr);
                    break;
                }
            }

            positions.push_back(point);
        }
    };
};

/**
 * @brief 地狱火清理函数实现
 * @details 通知麦克扎尔王子清理自己
 */
void netherspite_infernal::netherspite_infernalAI::Cleanup()
{
    Creature* pMalchezaar = ObjectAccessor::GetCreature(*me, malchezaar);

    if (pMalchezaar && pMalchezaar->IsAlive())
        ENSURE_AI(boss_malchezaar::boss_malchezaarAI, pMalchezaar->AI())->Cleanup(me, point);
}

/**
 * @brief 注册麦克扎尔王子首领脚本
 */
void AddSC_boss_malchezaar()
{
    new boss_malchezaar();
    new netherspite_infernal();
}
