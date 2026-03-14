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
 * @file    boss_nightbane.cpp
 * @brief   卡拉赞副本 - 夜之魇(Nightbane)首领战AI实现
 * @details 实现夜之魇的战斗逻辑,包括:
 *          - 地面阶段与飞行阶段的循环切换
 *          - 多种技能(顺劈斩、尾扫、灼热呼吸、焦土等)
 *          - 召唤骷髅小怪机制
 *          - 复杂的运动路径系统
 *          - 需要通过黑ened之瓮召唤Boss
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "karazhan.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

/**
 * @brief 夜之魇法术ID枚举
 */
enum NightbaneSpells
{
    SPELL_BELLOWING_ROAR        = 36922, ///< 咆哮 - 群体恐惧效果
    SPELL_CHARRED_EARTH         = 30129, ///< 焦土 - 地面持续伤害区域
    SPELL_CLEAVE                = 30131, ///< 顺劈斩 - 近战范围伤害
    SPELL_DISTRACTING_ASH       = 30130, ///< 灰烬干扰 - 降低命中
    SPELL_RAIN_OF_BONES         = 37098, ///< 骨雨 - 飞行阶段召唤骷髅
    SPELL_SMOKING_BLAST         = 30128, ///< 烟雾冲击 - 飞行阶段远程伤害
    SPELL_SMOKING_BLAST_T       = 37057, ///< 烟雾冲击(坦克版本)
    SPELL_SMOLDERING_BREATH     = 30210, ///< 灼热呼吸 - 锥形火焰吐息
    SPELL_SUMMON_SKELETON       = 30170, ///< 召唤骷髅 - 骨雨技能触发
    SPELL_TAIL_SWEEP            = 25653  ///< 尾扫 - 对身后玩家造成伤害
};

/**
 * @brief 对话和表情文本ID
 */
enum Says
{
    EMOTE_SUMMON                = 0, ///< 召唤时的表情
    YELL_AGGRO                  = 1, ///< 进入战斗时的喊话
    YELL_FLY_PHASE              = 2, ///< 起飞时的喊话
    YELL_LAND_PHASE             = 3, ///< 降落时的喊话
    EMOTE_BREATH                = 4  ///< 吐息前的表情警告
};

/**
 * @brief 路径点ID枚举
 * @details 用于Boss在空中飞行和降落时的移动路径
 */
enum NightbanePoints
{
    POINT_INTRO_START           = 0, ///< 入场动画开始点
    POINT_INTRO_END             = 1, ///< 入场动画结束点
    POINT_INTRO_LANDING         = 2, ///< 入场降落点
    POINT_PHASE_TWO_FLY         = 3, ///< 阶段2飞行点(中央)
    POINT_PHASE_TWO_PRE_FLY     = 4, ///< 阶段2预飞点(左侧或右侧)
    POINT_PHASE_TWO_LANDING     = 5, ///< 阶段2降落点
    POINT_PHASE_TWO_END         = 6  ///< 阶段2结束点
};

/**
 * @brief 样条链路径ID枚举
 * @details 定义Boss飞行的预设路径
 */
enum NightbaneSplineChain
{
    SPLINE_CHAIN_INTRO_START    = 1, ///< 入场开始路径
    SPLINE_CHAIN_INTRO_END      = 2, ///< 入场结束路径
    SPLINE_CHAIN_INTRO_LANDING  = 3, ///< 入场降落路径
    SPLINE_CHAIN_SECOND_LANDING = 4, ///< 二次降落路径
    SPLINE_CHAIN_PHASE_TWO      = 5  ///< 阶段2路径
};

/**
 * @brief 事件ID枚举
 * @details 用于事件调度系统管理技能和阶段切换
 */
enum NightbaneEvents
{
    EVENT_BELLOWING_ROAR = 1,  ///< 咆哮事件
    EVENT_CHARRED_EARTH,       ///< 焦土事件
    EVENT_CLEAVE,              ///< 顺劈斩事件
    EVENT_DISTRACTING_ASH,     ///< 灰烬干扰事件
    EVENT_EMOTE_BREATH,        ///< 吐息表情事件
    EVENT_END_INTRO,           ///< 入场结束事件
    EVENT_END_PHASE_TWO,       ///< 阶段2结束事件
    EVENT_INTRO_LANDING,       ///< 入场降落事件
    EVENT_LAND,                ///< 降落事件
    EVENT_LANDED,              ///< 已降落事件
    EVENT_PRE_FLY_END,         ///< 预飞结束事件
    EVENT_PRE_LAND,            ///< 预降落事件
    EVENT_RAIN_OF_BONES,       ///< 骨雨事件
    EVENT_SMOLDERING_BREATH,   ///< 灼热呼吸事件
    EVENT_SMOKING_BLAST,       ///< 烟雾冲击事件
    EVENT_SMOKING_BLAST_T,     ///< 烟雾冲击(坦克)事件
    EVENT_START_INTRO_PATH,    ///< 开始入场路径事件
    EVENT_TAIL_SWEEP           ///< 尾扫事件
};

/**
 * @brief 战斗阶段枚举
 */
enum NightbanePhases
{
    PHASE_INTRO = 0,  ///< 入场阶段
    PHASE_GROUND,     ///< 地面战斗阶段
    PHASE_FLY         ///< 飞行阶段
};

/**
 * @brief 事件分组枚举
 * @details 用于批量取消某阶段的所有事件
 */
enum NightbaneGroups
{
    GROUP_GROUND = 1, ///< 地面阶段事件组
    GROUP_FLY         ///< 飞行阶段事件组
};

/**
 * @brief 其他常量枚举
 */
enum NightbaneMisc
{
    ACTION_SUMMON  = 0,      ///< 召唤动作ID
    PATH_PHASE_TWO = 13547500 ///< 阶段2路径ID
};

///< 飞行阶段的位置坐标
Position const FlyPosition = { -11160.13f, -1870.683f, 97.73876f, 0.0f };       ///< 中央飞行位置
Position const FlyPositionLeft = { -11094.42f, -1866.992f, 107.8375f, 0.0f };  ///< 左侧飞行位置
Position const FlyPositionRight = { -11193.77f, -1921.983f, 107.9845f, 0.0f }; ///< 右侧飞行位置

/**
 * @class boss_nightbane
 * @brief 夜之魇首领脚本类
 * @details 注册和管理夜之魇的AI实例
 */
class boss_nightbane : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    boss_nightbane() : CreatureScript("boss_nightbane") { }

    /**
     * @struct boss_nightbaneAI
     * @brief 夜之魇AI结构体
     * @details 实现夜之魇的战斗逻辑,包括地面阶段、飞行阶段和复杂的移动路径
     */
    struct boss_nightbaneAI : public BossAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_nightbaneAI(Creature* creature) : BossAI(creature, DATA_NIGHTBANE), _flyCount(0) { }

        /**
         * @brief 重置Boss状态
         * @details 清理状态,设置飞行模式,打开门,重置瓮的状态
         *          调用时机:战斗结束或重置
         */
        void Reset() override
        {
            _Reset();
            _flyCount = 0;
            me->SetDisableGravity(true);
            HandleTerraceDoors(true);
            if (GameObject* urn = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(DATA_GO_BLACKENED_URN)))
                urn->RemoveFlag(GO_FLAG_IN_USE);
        }

        /**
         * @brief 进入规避模式
         * @param why 规避原因
         * @details 确保在规避时保持飞行状态
         */
        void EnterEvadeMode(EvadeReason why) override
        {
            me->SetDisableGravity(true);
            CreatureAI::EnterEvadeMode(why);
        }

        /**
         * @brief 返回初始位置后调用
         * @details 在Boss返回出生点后消失
         */
        void JustReachedHome() override
        {
            _DespawnAtEvade();
        }

        /**
         * @brief Boss死亡时调用
         * @param killer 击杀者
         */
        void JustDied(Unit* /*killer*/) override
        {
            _JustDied();
            HandleTerraceDoors(true);
        }

        /**
         * @brief 处理外部动作
         * @param action 动作ID
         * @details 当玩家点击黑ened之瓮时触发召唤动作
         */
        void DoAction(int32 action) override
        {
            if (action == ACTION_SUMMON)
            {
                Talk(EMOTE_SUMMON);
                events.SetPhase(PHASE_INTRO);
                me->setActive(true);
                me->SetFarVisible(true);
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->GetMotionMaster()->MoveAlongSplineChain(POINT_INTRO_START, SPLINE_CHAIN_INTRO_START, false);
                HandleTerraceDoors(false);
            }
        }

        /**
         * @brief 设置地面阶段事件
         * @details 调度地面阶段的所有技能事件
         *          调用时机:进入地面阶段
         */
        void SetupGroundPhase()
        {
            events.SetPhase(PHASE_GROUND);
            events.ScheduleEvent(EVENT_CLEAVE, 0s, Seconds(15), GROUP_GROUND);
            events.ScheduleEvent(EVENT_TAIL_SWEEP, Seconds(4), Seconds(23), GROUP_GROUND);
            events.ScheduleEvent(EVENT_BELLOWING_ROAR, Seconds(48), GROUP_GROUND);
            events.ScheduleEvent(EVENT_CHARRED_EARTH, Seconds(12), Seconds(18), GROUP_GROUND);
            events.ScheduleEvent(EVENT_SMOLDERING_BREATH, Seconds(26), Seconds(30), GROUP_GROUND);
            events.ScheduleEvent(EVENT_DISTRACTING_ASH, Seconds(82), GROUP_GROUND);
        }

        /**
         * @brief 处理露台门的开闭
         * @param open true为打开,false为关闭
         */
        void HandleTerraceDoors(bool open)
        {
            instance->HandleGameObject(instance->GetGuidData(DATA_MASTERS_TERRACE_DOOR_1), open);
            instance->HandleGameObject(instance->GetGuidData(DATA_MASTERS_TERRACE_DOOR_2), open);
        }

        /**
         * @brief 进入战斗时调用
         * @param who 仇恨目标
         */
        void JustEngagedWith(Unit* who) override
        {
            BossAI::JustEngagedWith(who);
            Talk(YELL_AGGRO);
            SetupGroundPhase();
        }

        /**
         * @brief 受到伤害时调用
         * @param attacker 攻击者
         * @param damage 伤害值(可修改)
         * @param damageType 伤害类型
         * @param spellInfo 法术信息
         * @details 在飞行阶段防止死亡,并在血量达到75%/50%/25%时触发飞行阶段
         */
        void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            if (events.IsInPhase(PHASE_FLY))
            {
                // 飞行阶段不死亡
                if (damage >= me->GetHealth())
                    damage = me->GetHealth() -1;
                return;
            }

            // 血量达到阈值时触发飞行阶段
            if ((_flyCount == 0 && HealthBelowPct(75)) || (_flyCount == 1 && HealthBelowPct(50)) || (_flyCount == 2 && HealthBelowPct(25)))
            {
                events.SetPhase(PHASE_FLY);
                StartPhaseFly();
            }
        }

        /**
         * @brief 移动完成通知
         * @param type 移动类型
         * @param pointId 路径点ID
         * @details 处理Boss移动路径中的各个关键节点,调度后续动作
         */
        void MovementInform(uint32 type, uint32 pointId) override
        {
            if (type == SPLINE_CHAIN_MOTION_TYPE)
            {
                switch (pointId)
                {
                    case POINT_INTRO_START:
                        me->SetStandState(UNIT_STAND_STATE_STAND);
                        events.ScheduleEvent(EVENT_START_INTRO_PATH, Milliseconds(1));
                        break;
                    case POINT_INTRO_END:
                        events.ScheduleEvent(EVENT_END_INTRO, 2s);
                        break;
                    case POINT_INTRO_LANDING:
                        me->SetDisableGravity(false);
                        me->HandleEmoteCommand(EMOTE_ONESHOT_LAND);
                        events.ScheduleEvent(EVENT_INTRO_LANDING, 3s);
                        break;
                    case POINT_PHASE_TWO_LANDING:
                        events.SetPhase(PHASE_GROUND);
                        me->SetDisableGravity(false);
                        me->HandleEmoteCommand(EMOTE_ONESHOT_LAND);
                        events.ScheduleEvent(EVENT_LANDED, 3s);
                        break;
                    case POINT_PHASE_TWO_END:
                        events.ScheduleEvent(EVENT_END_PHASE_TWO, Milliseconds(1));
                        break;
                    default:
                        break;
                }
            }
            else if (type == POINT_MOTION_TYPE)
            {
                if (pointId == POINT_PHASE_TWO_FLY)
                {
                    // 飞行阶段技能调度
                    events.ScheduleEvent(EVENT_PRE_LAND, Seconds(33), GROUP_FLY);
                    events.ScheduleEvent(EVENT_EMOTE_BREATH, Seconds(2), GROUP_FLY);
                    events.ScheduleEvent(EVENT_SMOKING_BLAST_T, Seconds(21), GROUP_FLY);
                    events.ScheduleEvent(EVENT_SMOKING_BLAST, Seconds(17), GROUP_FLY);
                }
                else if (pointId == POINT_PHASE_TWO_PRE_FLY)
                    events.ScheduleEvent(EVENT_PRE_FLY_END, Milliseconds(1));
            }
        }

        /**
         * @brief 开始飞行阶段
         * @details 中断地面技能,起飞并飞往最近的飞行位置
         *          调用时机:血量达到75%/50%/25%时
         */
        void StartPhaseFly()
        {
            ++_flyCount;
            Talk(YELL_FLY_PHASE);
            events.CancelEventGroup(GROUP_GROUND);
            me->InterruptNonMeleeSpells(false);
            me->HandleEmoteCommand(EMOTE_ONESHOT_LIFTOFF);
            me->SetDisableGravity(true);
            me->SetReactState(REACT_PASSIVE);
            me->AttackStop();

            // 选择最近的飞行位置
            if (me->GetDistance(FlyPositionLeft) < me->GetDistance(FlyPosition))
                me->GetMotionMaster()->MovePoint(POINT_PHASE_TWO_PRE_FLY, FlyPositionLeft, true);
            else if (me->GetDistance(FlyPositionRight) < me->GetDistance(FlyPosition))
                me->GetMotionMaster()->MovePoint(POINT_PHASE_TWO_PRE_FLY, FlyPositionRight, true);
            else
                me->GetMotionMaster()->MovePoint(POINT_PHASE_TWO_FLY, FlyPosition, true);
         }

        /**
         * @brief 执行事件
         * @param eventId 事件ID
         * @details 处理所有技能和阶段切换事件
         */
        void ExecuteEvent(uint32 eventId) override
        {
            switch (eventId)
            {
                case EVENT_BELLOWING_ROAR:
                    DoCastAOE(SPELL_BELLOWING_ROAR);
                    break;
                case EVENT_CHARRED_EARTH:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                        DoCast(target, SPELL_CHARRED_EARTH);
                    events.Repeat(Seconds(18), Seconds(21));
                    break;
                case EVENT_CLEAVE:
                    DoCastVictim(SPELL_CLEAVE);
                    events.Repeat(Seconds(6), Seconds(15));
                    break;
                case EVENT_DISTRACTING_ASH:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                        DoCast(target, SPELL_DISTRACTING_ASH);
                    break;
                case EVENT_EMOTE_BREATH:
                    Talk(EMOTE_BREATH);
                    events.ScheduleEvent(EVENT_RAIN_OF_BONES, Seconds(3), GROUP_FLY);
                    break;
                case EVENT_END_INTRO:
                    me->GetMotionMaster()->MoveAlongSplineChain(POINT_INTRO_LANDING, SPLINE_CHAIN_INTRO_LANDING, false);
                    break;
                case EVENT_END_PHASE_TWO:
                    me->GetMotionMaster()->MoveAlongSplineChain(POINT_PHASE_TWO_LANDING, SPLINE_CHAIN_SECOND_LANDING, false);
                    break;
                case EVENT_INTRO_LANDING:
                    me->SetImmuneToPC(false);
                    DoZoneInCombat();
                    break;
                case EVENT_LAND:
                    Talk(YELL_LAND_PHASE);
                    me->SetDisableGravity(true);
                    me->GetMotionMaster()->MoveAlongSplineChain(POINT_PHASE_TWO_END, SPLINE_CHAIN_PHASE_TWO, false);
                    break;
                case EVENT_LANDED:
                    SetupGroundPhase();
                    me->SetReactState(REACT_AGGRESSIVE);
                    break;
                case EVENT_PRE_FLY_END:
                    me->GetMotionMaster()->MovePoint(POINT_PHASE_TWO_FLY, FlyPosition, true);
                    break;
                case EVENT_PRE_LAND:
                    events.CancelEventGroup(GROUP_FLY);
                    events.ScheduleEvent(EVENT_LAND, Seconds(2), GROUP_GROUND);
                    break;
                case EVENT_START_INTRO_PATH:
                    me->GetMotionMaster()->MoveAlongSplineChain(POINT_INTRO_END, SPLINE_CHAIN_INTRO_END, false);
                    break;
                case EVENT_RAIN_OF_BONES:
                    ResetThreatList();
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                    {
                        me->SetFacingToObject(target);
                        DoCast(target, SPELL_RAIN_OF_BONES);
                    }
                    break;
                case EVENT_SMOLDERING_BREATH:
                    DoCastVictim(SPELL_SMOLDERING_BREATH);
                    events.Repeat(Seconds(28), Seconds(40));
                    break;
                case EVENT_SMOKING_BLAST:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                        DoCast(target, SPELL_SMOKING_BLAST);
                    events.Repeat(Milliseconds(1400));
                    break;
                case EVENT_SMOKING_BLAST_T:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                        DoCast(target, SPELL_SMOKING_BLAST_T);
                    events.Repeat(Seconds(5), Seconds(7));
                    break;
                case EVENT_TAIL_SWEEP:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                        if (!me->HasInArc(float(M_PI), target))
                            DoCast(target, SPELL_TAIL_SWEEP);
                    events.Repeat(Seconds(20), Seconds(30));
                    break;
                default:
                    break;
            }
        }

        /**
         * @brief AI更新函数
         * @param diff 距离上次调用的时间间隔(毫秒)
         * @details 处理事件调度和近战攻击
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim() && !events.IsInPhase(PHASE_INTRO))
                return;

            events.Update(diff);

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            while (uint32 eventId = events.ExecuteEvent())
            {
                ExecuteEvent(eventId);
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;
            }

            DoMeleeAttackIfReady();
        }

        private:
            uint8 _flyCount; ///< 飞行阶段计数器(最多3次)
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回夜之魇AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_nightbaneAI>(creature);
    }
};

/**
 * @class spell_rain_of_bones
 * @brief 骨雨法术脚本
 * @details 处理骨雨技能的周期性触发效果,每5跳召唤一个骷髅小怪
 */
class spell_rain_of_bones : public SpellScriptLoader
{
    public:
        spell_rain_of_bones() : SpellScriptLoader("spell_rain_of_bones") { }

        /**
         * @class spell_rain_of_bones_AuraScript
         * @brief 骨雨光环脚本
         */
        class spell_rain_of_bones_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_rain_of_bones_AuraScript);

            /**
             * @brief 验证法术信息
             * @param spellInfo 法术信息
             * @return 法术信息是否有效
             */
            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_SUMMON_SKELETON });
            }

            /**
             * @brief 周期性触发回调
             * @param aurEff 光环效果
             * @details 每5次触发时召唤一个骷髅
             */
            void OnTrigger(AuraEffect const* aurEff)
            {
                if (aurEff->GetTickNumber() % 5 == 0)
                    GetTarget()->CastSpell(GetTarget(), SPELL_SUMMON_SKELETON, true);
            }

            /**
             * @brief 注册回调函数
             */
            void Register() override
            {
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_rain_of_bones_AuraScript::OnTrigger, EFFECT_1, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
            }
        };

        /**
         * @brief 获取光环脚本实例
         * @return 光环脚本指针
         */
        AuraScript* GetAuraScript() const override
        {
            return new spell_rain_of_bones_AuraScript();
        }
};

/**
 * @class go_blackened_urn
 * @brief 黑ened之瓮游戏对象脚本
 * @details 处理玩家点击瓮召唤夜之魇的逻辑
 */
class go_blackened_urn : public GameObjectScript
{
    public:
        go_blackened_urn() : GameObjectScript("go_blackened_urn") { }

        /**
         * @struct go_blackened_urnAI
         * @brief 黑ened之瓮AI结构体
         */
        struct go_blackened_urnAI : GameObjectAI
        {
            go_blackened_urnAI(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

            InstanceScript* instance; ///< 副本实例脚本指针

            /**
             * @brief 玩家点击游戏对象时调用
             * @param player 点击的玩家
             * @return 是否处理了该交互
             * @details 检查条件并触发夜之魇召唤
             */
            bool OnGossipHello(Player* /*player*/) override
            {
                // 检查瓮是否已被使用
                if (me->HasFlag(GO_FLAG_IN_USE))
                    return false;

                // 检查Boss状态
                if (instance->GetBossState(DATA_NIGHTBANE) == DONE || instance->GetBossState(DATA_NIGHTBANE) == IN_PROGRESS)
                    return false;

                // 触发召唤动作
                if (Creature* nightbane = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_NIGHTBANE)))
                {
                    me->SetFlag(GO_FLAG_IN_USE);
                    nightbane->AI()->DoAction(ACTION_SUMMON);
                }
                return false;
            }
        };

        /**
         * @brief 获取AI实例
         * @param go 游戏对象指针
         * @return 返回黑ened之瓮AI实例
         */
        GameObjectAI* GetAI(GameObject* go) const override
        {
            return GetKarazhanAI<go_blackened_urnAI>(go);
        }
};

/**
 * @brief 注册夜之魇首领脚本
 */
void AddSC_boss_nightbane()
{
    new boss_nightbane();
    new spell_rain_of_bones();
    new go_blackened_urn();
}
