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
 * @file    boss_emerald_dragons.cpp
 * @brief   翡翠梦境四大绿龙世界Boss脚本
 *
 * 本文件实现了翡翠梦境的四只绿龙世界Boss：
 * - 伊森德雷（Ysondre）
 * - 莱索恩（Lethon）
 * - 艾莫莉丝（Emeriss）
 * - 泰拉尔（Taerar）
 *
 * 这些Boss在世界各地的梦境传送门附近刷新，是经典旧世时期的重要世界Boss。
 * 每只绿龙都有独特的技能和战斗机制，但共享一些基础能力。
 *
 * 主要功能：
 * - 共享基础AI（尾扫、诺克斯呼吸、梦境迷雾）
 * - 各Boss独特的阶段转换机制
 * - 自然之印标记系统（死亡玩家会被标记）
 * - 梦境迷雾NPC控制
 *
 * 性能注意事项：
 * - 作为世界Boss，可能同时面对大量玩家
 * - 需要优化AOE技能处理
 * - 使用事件调度器管理技能冷却
 */

#include "ObjectMgr.h"
#include "MotionMaster.h"
#include "PassiveAI.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

//
//  翡翠梦境龙NPC ID定义
//

/**
 * @brief 翡翠梦境龙相关NPC ID
 */
enum EmeraldDragonNPC
{
    NPC_DREAM_FOG                   = 15224,    ///< NPC：梦境迷雾（被召唤的控制型NPC）
    DRAGON_YSONDRE                  = 14887,    ///< Boss：伊森德雷
    DRAGON_LETHON                   = 14888,    ///< Boss：莱索恩
    DRAGON_EMERISS                  = 14889,    ///< Boss：艾莫莉丝
    DRAGON_TAERAR                   = 14890,    ///< Boss：泰拉尔
};

//
// 翡翠梦境龙法术ID定义
//

/**
 * @brief 翡翠梦境龙共享法术ID
 *
 * 这些法术是所有四只绿龙共用的基础能力。
 */
enum EmeraldDragonSpells
{
    SPELL_TAIL_SWEEP                = 15847,    ///< 尾扫 - 对龙身后所有目标造成伤害（每2秒施放）
    SPELL_SUMMON_PLAYER             = 24776,    ///< 召唤玩家 - 将最高仇恨玩家传送到龙面前（防止卡视角）
    SPELL_DREAM_FOG                 = 24777,    ///< 梦境迷雾 - 梦境迷雾NPC的光环法术
    SPELL_SLEEP                     = 24778,    ///< 睡眠 - 梦境迷雾触发的睡眠法术
    SPELL_SEEPING_FOG_LEFT          = 24813,    ///< 渗出迷雾 - 从左侧召唤梦境迷雾
    SPELL_SEEPING_FOG_RIGHT         = 24814,    ///< 渗出迷雾 - 从右侧召唤梦境迷雾
    SPELL_NOXIOUS_BREATH            = 24818,    ///< 诺克斯呼吸 - 前方锥形自然伤害
    SPELL_MARK_OF_NATURE            = 25040,    ///< 自然之印 - 玩家死亡时获得此标记，持续15分钟
    SPELL_MARK_OF_NATURE_AURA       = 25041,    ///< 自然之印光环 - Boss身上的被动标记测试光环，每10秒触发
    SPELL_AURA_OF_NATURE            = 25043,    ///< 自然光环 - 对有自然之印的玩家施放，昏迷2分钟
};

//
// 翡翠梦境龙事件ID定义
//

/**
 * @brief 翡翠梦境龙事件类型
 *
 * 包含所有四只绿龙共享的事件和各自独特的技能事件。
 */
enum Events
{
    // 所有绿龙共享的事件
    EVENT_SEEPING_FOG = 1,      ///< 渗出迷雾事件 - 召唤梦境迷雾NPC
    EVENT_NOXIOUS_BREATH,       ///< 诺克斯呼吸事件 - 前方锥形伤害
    EVENT_TAIL_SWEEP,           ///< 尾扫事件 - 对身后目标造成伤害

    // 伊森德雷特有事件
    EVENT_LIGHTNING_WAVE,       ///< 闪电波事件 - 对目标施放闪电链
    EVENT_SUMMON_DRUID_SPIRITS, ///< 召唤德鲁伊灵魂事件 - 在特定血量阶段召唤

    // 莱索恩特有事件
    EVENT_SHADOW_BOLT_WHIRL,    ///< 暗影箭旋风事件 - 周期性暗影箭攻击

    // 艾莫莉丝特有事件
    EVENT_VOLATILE_INFECTION,   ///< 易变感染事件 - 对目标施放疾病
    EVENT_CORRUPTION_OF_EARTH,  ///< 大地腐蚀事件 - 在特定血量阶段施放

    // 泰拉尔特有事件
    EVENT_ARCANE_BLAST,         ///< 奥术冲击事件 - 对目标施放奥术伤害
    EVENT_BELLOWING_ROAR,       ///< 咆哮事件 - 范围恐惧效果
};

/*
 * ---
 * --- 翡翠梦境龙基类AI
 * ---
 */

/**
 * @struct emerald_dragonAI
 * @brief 翡翠梦境龙共享AI基类
 *
 * 为所有四只翡翠梦境绿龙提供基础AI功能，包括：
 * - 共享技能（尾扫、诺克斯呼吸、梦境迷雾召唤）
 * - 自然之印标记系统
 * - 玩家传送机制
 *
 * 继承自WorldBossAI，适用于世界Boss的特殊处理。
 */
struct emerald_dragonAI : public WorldBossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物指针
     */
    emerald_dragonAI(Creature* creature) : WorldBossAI(creature)
    {
    }

    /**
     * @brief 重置Boss状态
     *
     * 初始化Boss的基本属性和技能调度：
     * - 移除不可交互和不可攻击标记
     * - 设置为主动攻击状态
     * - 施放自然之印光环
     * - 调度共享技能事件
     */
    void Reset() override
    {
        WorldBossAI::Reset();
        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE);
        me->SetReactState(REACT_AGGRESSIVE);
        DoCast(me, SPELL_MARK_OF_NATURE_AURA, true);  // 施放自然之印光环，用于检测被标记的玩家
        events.ScheduleEvent(EVENT_TAIL_SWEEP, 4s);    // 4秒后开始尾扫
        events.ScheduleEvent(EVENT_NOXIOUS_BREATH, 7500ms, 15s);  // 7.5-15秒后施放诺克斯呼吸
        events.ScheduleEvent(EVENT_SEEPING_FOG, 12500ms, 20s);    // 12.5-20秒后召唤梦境迷雾
    }

    /**
     * @brief 击杀单位回调
     * @param who 被击杀的单位
     *
     * 当玩家在战斗中死亡时，对其施放自然之印。
     * 被标记的玩家在15分钟内再次参与战斗会被自然光环昏迷2分钟。
     */
    void KilledUnit(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER)
            who->CastSpell(who, SPELL_MARK_OF_NATURE, true);
    }

    /**
     * @brief 执行事件
     * @param eventId 事件ID
     *
     * 处理所有绿龙共享的技能事件：
     * - EVENT_SEEPING_FOG：召唤左右两个梦境迷雾NPC
     * - EVENT_NOXIOUS_BREATH：施放诺克斯呼吸
     * - EVENT_TAIL_SWEEP：施放尾扫
     *
     * 子类可以覆盖此方法处理各自的独特技能。
     */
    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_SEEPING_FOG:
                // 梦境迷雾成对出现，同时只能存在一对
                // 迷雾存在时间为2分钟，所以重新调度时间为2分钟+随机时间（最多30秒）
                DoCast(me, SPELL_SEEPING_FOG_LEFT, true);
                DoCast(me, SPELL_SEEPING_FOG_RIGHT, true);
                events.ScheduleEvent(EVENT_SEEPING_FOG, 120s, 150s);
                break;
            case EVENT_NOXIOUS_BREATH:
                // 诺克斯呼吸间隔随机，最少7.5秒
                DoCast(me, SPELL_NOXIOUS_BREATH);
                events.ScheduleEvent(EVENT_NOXIOUS_BREATH, 7500ms, 15s);
                break;
            case EVENT_TAIL_SWEEP:
                // 尾扫每2秒施放一次，不受其他因素影响
                DoCast(me, SPELL_TAIL_SWEEP);
                events.ScheduleEvent(EVENT_TAIL_SWEEP, 2s);
                break;
        }
    }

    /**
     * @brief 更新AI
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 主要逻辑：
     * 1. 检查战斗状态
     * 2. 更新事件调度器
     * 3. 处理施法状态
     * 4. 传送远离的玩家（超过50码）
     * 5. 执行近战攻击
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
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

        // 如果最高仇恨目标超过50码，将其传送到面前
        if (Unit* target = SelectTarget(SelectTargetMethod::MaxThreat, 0, -50.0f, true))
            DoCast(target, SPELL_SUMMON_PLAYER);

        DoMeleeAttackIfReady();
    }
};

/*
 * --- NPC: 梦境迷雾
 */

/**
 * @class npc_dream_fog
 * @brief 梦境迷雾NPC脚本
 *
 * 梦境迷雾是被翡翠梦境龙召唤的控制型NPC。
 * 它会在战场上随机移动，并使接近的玩家进入睡眠状态。
 *
 * 机制：
 * - 随机追逐玩家或随机游荡
 * - 移动速度较慢，玩家可以倒退行走避开
 * - 对接近的玩家施放睡眠效果
 */
class npc_dream_fog : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        npc_dream_fog() : CreatureScript("npc_dream_fog") { }

        /**
         * @class npc_dream_fogAI
         * @brief 梦境迷雾AI实现
         */
        struct npc_dream_fogAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             */
            npc_dream_fogAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             */
            void Initialize()
            {
                _roamTimer = 0;  ///< 漫游计时器
            }

            /**
             * @brief 重置AI状态
             */
            void Reset() override
            {
                Initialize();
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 控制NPC的移动行为：
             * - 计时器到期后选择新的目标或随机移动
             * - 追逐玩家但不攻击（只触发睡眠效果）
             * - 设置较慢的移动速度（玩家可以倒退避开）
             */
            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                if (!_roamTimer)
                {
                    // 追逐目标但不攻击，或者只是随机漫游
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true))
                    {
                        _roamTimer = urand(15000, 30000);  // 15-30秒后选择新目标
                        me->GetMotionMaster()->Clear();
                        me->GetMotionMaster()->MoveChase(target, 0.2f);  // 保持极小距离
                    }
                    else
                    {
                        _roamTimer = 2500;  // 2.5秒后再试
                        me->GetMotionMaster()->Clear();
                        me->GetMotionMaster()->MoveRandom(25.0f);  // 在25码范围内随机移动
                    }
                    // 渗出迷雾移动速度足够慢，玩家可以倒退行走避开
                    me->SetWalk(true);
                    me->SetSpeedRate(MOVE_WALK, 0.75f);  // 设置为正常行走速度的75%
                }
                else
                    _roamTimer -= diff;
            }

        private:
            uint32 _roamTimer;  ///< 漫游计时器，控制目标选择频率
        };

        /**
         * @brief 获取生物AI实例
         * @param creature 生物指针
         * @return 返回新创建的AI实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_dream_fogAI(creature);
        }
};

/*
 * ---
 * --- 伊森德雷（Ysondre）
 * ---
 */

/**
 * @brief 伊森德雷相关NPC ID
 */
enum YsondreNPC
{
    NPC_DEMENTED_DRUID              = 15260,    ///< NPC：疯狂的德鲁伊灵魂
};

/**
 * @brief 伊森德雷对话文本ID
 */
enum YsondreTexts
{
    SAY_YSONDRE_AGGRO               = 0,        ///< 对话：进入战斗
    SAY_YSONDRE_SUMMON_DRUIDS       = 1,        ///< 对话：召唤德鲁伊灵魂
};

/**
 * @brief 伊森德雷法术ID
 */
enum YsondreSpells
{
    SPELL_LIGHTNING_WAVE            = 24819,    ///< 法术：闪电波（连锁闪电）
    SPELL_SUMMON_DRUID_SPIRITS      = 24795,    ///< 法术：召唤德鲁伊灵魂
};

/**
 * @class boss_ysondre
 * @brief 伊森德雷Boss脚本
 *
 * 伊森德雷是翡翠梦境四绿龙之一，拥有以下能力：
 * - 共享技能：尾扫、诺克斯呼吸、梦境迷雾
 * - 独特技能：闪电波（连锁闪电）
 * - 阶段转换：在75%、50%、25%血量时召唤德鲁伊灵魂
 *
 * 战斗机制：
 * - 闪电波对目标及其周围玩家造成自然伤害
 * - 德鲁伊灵魂会攻击玩家，需要处理
 */
class boss_ysondre : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_ysondre() : CreatureScript("boss_ysondre") { }

        /**
         * @class boss_ysondreAI
         * @brief 伊森德雷AI实现
         */
        struct boss_ysondreAI : public emerald_dragonAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             */
            boss_ysondreAI(Creature* creature) : emerald_dragonAI(creature)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             */
            void Initialize()
            {
                _stage = 1;  ///< 当前阶段（1-4对应100%-75%-50%-25%）
            }

            /**
             * @brief 重置Boss状态
             */
            void Reset() override
            {
                Initialize();
                emerald_dragonAI::Reset();
                events.ScheduleEvent(EVENT_LIGHTNING_WAVE, 12s);  // 12秒后施放闪电波
            }

            /**
             * @brief 进入战斗回调
             * @param who 进入战斗的目标
             */
            void JustEngagedWith(Unit* who) override
            {
                Talk(SAY_YSONDRE_AGGRO);  // 说开场白
                WorldBossAI::JustEngagedWith(who);
            }

            /**
             * @brief 受到伤害回调
             * @param attacker 攻击者（未使用）
             * @param damage 伤害值（未使用）
             * @param damageType 伤害类型（未使用）
             * @param spellInfo 法术信息（未使用）
             *
             * 在75%、50%、25%血量时召唤10个德鲁伊灵魂。
             */
            void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                // 检查是否到达阶段转换血量
                if (!HealthAbovePct(100 - 25 * _stage))
                {
                    Talk(SAY_YSONDRE_SUMMON_DRUIDS);  // 说召唤台词

                    // 召唤10个德鲁伊灵魂
                    for (uint8 i = 0; i < 10; ++i)
                        DoCast(me, SPELL_SUMMON_DRUID_SPIRITS, true);
                    ++_stage;
                }
            }

            /**
             * @brief 执行事件
             * @param eventId 事件ID
             *
             * 处理伊森德雷独特技能：
             * - EVENT_LIGHTNING_WAVE：对当前目标施放闪电波
             */
            void ExecuteEvent(uint32 eventId) override
            {
                switch (eventId)
                {
                    case EVENT_LIGHTNING_WAVE:
                        DoCastVictim(SPELL_LIGHTNING_WAVE);
                        events.ScheduleEvent(EVENT_LIGHTNING_WAVE, 10s, 20s);  // 10-20秒后再次施放
                        break;
                    default:
                        emerald_dragonAI::ExecuteEvent(eventId);
                        break;
                }
            }

        private:
            uint8 _stage;  ///< 当前战斗阶段（1-4）
        };

        /**
         * @brief 获取生物AI实例
         * @param creature 生物指针
         * @return 返回新创建的AI实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return new boss_ysondreAI(creature);
        }
};

/*
 * ---
 * --- 莱索恩（Lethon）
 * ---
 *
 * @todo
 * - 法术：暗影箭旋风需要自定义处理（法术脚本）
 */

/**
 * @brief 莱索恩对话文本ID
 */
enum LethonTexts
{
    SAY_LETHON_AGGRO                = 0,        ///< 对话：进入战斗
    SAY_LETHON_DRAW_SPIRIT          = 1,        ///< 对话：抽取灵魂
};

/**
 * @brief 莱索恩法术ID
 */
enum LethonSpells
{
    SPELL_DRAW_SPIRIT               = 24811,    ///< 法术：抽取灵魂（从玩家身上抽取灵魂阴影）
    SPELL_SHADOW_BOLT_WHIRL         = 24834,    ///< 法术：暗影箭旋风（周期性暗影箭攻击）
    SPELL_DARK_OFFERING             = 24804,    ///< 法术：黑暗祭品（灵魂阴影对Boss的治疗）
};

/**
 * @brief 莱索恩相关生物ID
 */
enum LethonCreatures
{
    NPC_SPIRIT_SHADE                = 15261,    ///< NPC：灵魂阴影
};

/**
 * @class boss_lethon
 * @brief 莱索恩Boss脚本
 *
 * 莱索恩是翡翠梦境四绿龙之一，拥有以下能力：
 * - 共享技能：尾扫、诺克斯呼吸、梦境迷雾
 * - 独特技能：暗影箭旋风（周期性暗影伤害）
 * - 阶段转换：在75%、50%、25%血量时从玩家身上抽取灵魂阴影
 *
 * 战斗机制：
 * - 暗影箭旋风周期性对周围玩家造成暗影伤害
 * - 灵魂阴影会移动到Boss处治疗Boss，需要玩家拦截击杀
 */
class boss_lethon : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_lethon() : CreatureScript("boss_lethon") { }

        /**
         * @class boss_lethonAI
         * @brief 莱索恩AI实现
         */
        struct boss_lethonAI : public emerald_dragonAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             */
            boss_lethonAI(Creature* creature) : emerald_dragonAI(creature)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             */
            void Initialize()
            {
                _stage = 1;  ///< 当前阶段（1-4对应100%-75%-50%-25%）
            }

            /**
             * @brief 重置Boss状态
             */
            void Reset() override
            {
                Initialize();
                emerald_dragonAI::Reset();
                events.ScheduleEvent(EVENT_SHADOW_BOLT_WHIRL, 10s);  // 10秒后施放暗影箭旋风
            }

            /**
             * @brief 进入战斗回调
             * @param who 进入战斗的目标
             */
            void JustEngagedWith(Unit* who) override
            {
                Talk(SAY_LETHON_AGGRO);  // 说开场白
                WorldBossAI::JustEngagedWith(who);
            }

            /**
             * @brief 受到伤害回调
             * @param attacker 攻击者（未使用）
             * @param damage 伤害值（未使用）
             * @param damageType 伤害类型（未使用）
             * @param spellInfo 法术信息（未使用）
             *
             * 在75%、50%、25%血量时从玩家身上抽取灵魂阴影。
             */
            void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                // 检查是否到达阶段转换血量
                if (!HealthAbovePct(100 - 25 * _stage))
                {
                    Talk(SAY_LETHON_DRAW_SPIRIT);  // 说抽取灵魂台词
                    DoCast(me, SPELL_DRAW_SPIRIT);
                    ++_stage;
                }
            }

            /**
             * @brief 法术命中目标回调
             * @param target 目标对象
             * @param spellInfo 法术信息
             *
             * 当抽取灵魂法术命中玩家时，在该玩家位置召唤灵魂阴影NPC。
             */
            void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
            {
                // 检查是否为抽取灵魂法术且目标为玩家
                if (spellInfo->Id == SPELL_DRAW_SPIRIT && target->GetTypeId() == TYPEID_PLAYER)
                {
                    Position targetPos = target->GetPosition();
                    // 在玩家位置召唤灵魂阴影，脱战50秒后消失
                    me->SummonCreature(NPC_SPIRIT_SHADE, targetPos, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 50s);
                }
            }

            /**
             * @brief 执行事件
             * @param eventId 事件ID
             *
             * 处理莱索恩独特技能：
             * - EVENT_SHADOW_BOLT_WHIRL：施放暗影箭旋风
             */
            void ExecuteEvent(uint32 eventId) override
            {
                switch (eventId)
                {
                    case EVENT_SHADOW_BOLT_WHIRL:
                        me->CastSpell(nullptr, SPELL_SHADOW_BOLT_WHIRL, false);
                        events.ScheduleEvent(EVENT_SHADOW_BOLT_WHIRL, 15s, 30s);  // 15-30秒后再次施放
                        break;
                    default:
                        emerald_dragonAI::ExecuteEvent(eventId);
                        break;
                }
            }

        private:
            uint8 _stage;  ///< 当前战斗阶段（1-4）
        };

        /**
         * @brief 获取生物AI实例
         * @param creature 生物指针
         * @return 返回新创建的AI实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return new boss_lethonAI(creature);
        }
};

/**
 * @class npc_spirit_shade
 * @brief 灵魂阴影NPC脚本
 *
 * 灵魂阴影是从玩家身上抽取的NPC，会移动到Boss处治疗Boss。
 * 玩家需要在它到达Boss之前将其击杀。
 */
class npc_spirit_shade : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        npc_spirit_shade() : CreatureScript("npc_spirit_shade") { }

        /**
         * @class npc_spirit_shadeAI
         * @brief 灵魂阴影AI实现（被动AI）
         */
        struct npc_spirit_shadeAI : public PassiveAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             */
            npc_spirit_shadeAI(Creature* creature) : PassiveAI(creature), _summonerGuid()
            {
            }

            /**
             * @brief 被召唤时回调
             * @param summonerWO 召唤者对象
             *
             * 开始跟随召唤者（Boss）移动。
             */
            void IsSummonedBy(WorldObject* summonerWO) override
            {
                Unit* summoner = summonerWO->ToUnit();
                if (!summoner)
                    return;

                _summonerGuid = summoner->GetGUID();
                me->GetMotionMaster()->MoveFollow(summoner, 0.0f, 0.0f);  // 紧密跟随
            }

            /**
             * @brief 移动完成通知
             * @param moveType 移动类型
             * @param data 移动数据
             *
             * 当到达Boss位置时，施放黑暗祭品治疗Boss并消失。
             */
            void MovementInform(uint32 moveType, uint32 data) override
            {
                // 检查是否为跟随移动完成且目标是召唤者
                if (moveType == FOLLOW_MOTION_TYPE && data == _summonerGuid.GetCounter())
                {
                    me->CastSpell(nullptr, SPELL_DARK_OFFERING, false);  // 治疗Boss
                    me->DespawnOrUnsummon(1s);  // 1秒后消失
                }
            }

        private:
            ObjectGuid _summonerGuid;  ///< 召唤者GUID（Boss）
        };

        /**
         * @brief 获取生物AI实例
         * @param creature 生物指针
         * @return 返回新创建的AI实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return new npc_spirit_shadeAI(creature);
        }
};

/*
 * ---
 * --- 艾莫莉丝（Emeriss）
 * ---
 */

/**
 * @brief 艾莫莉丝对话文本ID
 */
enum EmerissTexts
{
    SAY_EMERISS_AGGRO               = 0,        ///< 对话：进入战斗
    SAY_EMERISS_CAST_CORRUPTION     = 1,        ///< 对话：施放大地腐蚀
};

/**
 * @brief 艾莫莉丝法术ID
 */
enum EmerissSpells
{
    SPELL_PUTRID_MUSHROOM           = 24904,    ///< 法术：腐化蘑菇（在死亡玩家位置召唤）
    SPELL_CORRUPTION_OF_EARTH       = 24910,    ///< 法术：大地腐蚀（阶段转换技能）
    SPELL_VOLATILE_INFECTION        = 24928,    ///< 法术：易变感染（疾病效果）
};

/**
 * @class boss_emeriss
 * @brief 艾莫莉丝Boss脚本
 *
 * 艾莫莉丝是翡翠梦境四绿龙之一，拥有以下能力：
 * - 共享技能：尾扫、诺克斯呼吸、梦境迷雾
 * - 独特技能：易变感染（对目标施放疾病）
 * - 阶段转换：在75%、50%、25%血量时施放大地腐蚀
 * - 死亡效果：被击杀的玩家位置会生成腐化蘑菇
 *
 * 战斗机制：
 * - 易变感染是持续性疾病效果，需要驱散
 * - 大地腐蚀在Boss位置造成范围自然伤害
 * - 腐化蘑菇会对周围玩家造成伤害
 */
class boss_emeriss : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_emeriss() : CreatureScript("boss_emeriss") { }

        /**
         * @class boss_emerissAI
         * @brief 艾莫莉丝AI实现
         */
        struct boss_emerissAI : public emerald_dragonAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             */
            boss_emerissAI(Creature* creature) : emerald_dragonAI(creature)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             */
            void Initialize()
            {
                _stage = 1;  ///< 当前阶段（1-4对应100%-75%-50%-25%）
            }

            /**
             * @brief 重置Boss状态
             */
            void Reset() override
            {
                Initialize();
                emerald_dragonAI::Reset();
                events.ScheduleEvent(EVENT_VOLATILE_INFECTION, 12s);  // 12秒后施放易变感染
            }

            /**
             * @brief 击杀单位回调
             * @param who 被击杀的单位
             *
             * 当玩家被击杀时，在其位置召唤腐化蘑菇。
             */
            void KilledUnit(Unit* who) override
            {
                if (who->GetTypeId() == TYPEID_PLAYER)
                    DoCast(who, SPELL_PUTRID_MUSHROOM, true);  // 在玩家位置召唤腐化蘑菇
                emerald_dragonAI::KilledUnit(who);  // 调用基类实现（自然之印）
            }

            /**
             * @brief 进入战斗回调
             * @param who 进入战斗的目标
             */
            void JustEngagedWith(Unit* who) override
            {
                Talk(SAY_EMERISS_AGGRO);  // 说开场白
                WorldBossAI::JustEngagedWith(who);
            }

            /**
             * @brief 受到伤害回调
             * @param attacker 攻击者（未使用）
             * @param damage 伤害值（未使用）
             * @param damageType 伤害类型（未使用）
             * @param spellInfo 法术信息（未使用）
             *
             * 在75%、50%、25%血量时施放大地腐蚀。
             */
            void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                // 检查是否到达阶段转换血量
                if (!HealthAbovePct(100 - 25 * _stage))
                {
                    Talk(SAY_EMERISS_CAST_CORRUPTION);  // 说大地腐蚀台词
                    DoCast(me, SPELL_CORRUPTION_OF_EARTH, true);
                    ++_stage;
                }
            }

            /**
             * @brief 执行事件
             * @param eventId 事件ID
             *
             * 处理艾莫莉丝独特技能：
             * - EVENT_VOLATILE_INFECTION：对当前目标施放易变感染
             */
            void ExecuteEvent(uint32 eventId) override
            {
                switch (eventId)
                {
                    case EVENT_VOLATILE_INFECTION:
                        DoCastVictim(SPELL_VOLATILE_INFECTION);
                        events.ScheduleEvent(EVENT_VOLATILE_INFECTION, 120s);  // 120秒后再次施放
                        break;
                    default:
                        emerald_dragonAI::ExecuteEvent(eventId);
                        break;
                }
            }

        private:
            uint8 _stage;  ///< 当前战斗阶段（1-4）
        };

        /**
         * @brief 获取生物AI实例
         * @param creature 生物指针
         * @return 返回新创建的AI实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return new boss_emerissAI(creature);
        }
};

/*
 * ---
 * --- 泰拉尔（Taerar）
 * ---
 */

/**
 * @brief 泰拉尔对话文本ID
 */
enum TaerarTexts
{
    SAY_TAERAR_AGGRO                = 0,        ///< 对话：进入战斗
    SAY_TAERAR_SUMMON_SHADES        = 1,        ///< 对话：召唤阴影
};

/**
 * @brief 泰拉尔法术ID
 */
enum TaerarSpells
{
    SPELL_BELLOWING_ROAR            = 22686,    ///< 法术：咆哮（范围恐惧）
    SPELL_SHADE                     = 24313,    ///< 法术：阴影（Boss消失时的视觉效果）
    SPELL_SUMMON_SHADE_1            = 24841,    ///< 法术：召唤阴影1
    SPELL_SUMMON_SHADE_2            = 24842,    ///< 法术：召唤阴影2
    SPELL_SUMMON_SHADE_3            = 24843,    ///< 法术：召唤阴影3
    SPELL_ARCANE_BLAST              = 24857,    ///< 法术：奥术冲击
};

/// 泰拉尔阴影召唤法术数组
uint32 const TaerarShadeSpells[] =
{
    SPELL_SUMMON_SHADE_1, SPELL_SUMMON_SHADE_2, SPELL_SUMMON_SHADE_3
};

/**
 * @class boss_taerar
 * @brief 泰拉尔Boss脚本
 *
 * 泰拉尔是翡翠梦境四绿龙之一，拥有以下能力：
 * - 共享技能：尾扫、诺克斯呼吸、梦境迷雾
 * - 独特技能：奥术冲击、咆哮（范围恐惧）
 * - 阶段转换：在75%、50%、25%血量时消失并召唤3个阴影分身
 *
 * 战斗机制：
 * - 奥术冲击对目标造成奥术伤害
 * - 咆哮使周围玩家恐惧
 * - 阴影分身需要被全部击杀，或等待60秒后泰拉尔重新出现
 */
class boss_taerar : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_taerar() : CreatureScript("boss_taerar") { }

        /**
         * @class boss_taerarAI
         * @brief 泰拉尔AI实现
         */
        struct boss_taerarAI : public emerald_dragonAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             */
            boss_taerarAI(Creature* creature) : emerald_dragonAI(creature)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             */
            void Initialize()
            {
                _stage = 1;           ///< 当前阶段（1-4对应100%-75%-50%-25%）
                _shades = 0;          ///< 存活的阴影数量
                _banished = false;    ///< Boss是否处于消失状态
                _banishedTimer = 0;   ///< 消失状态计时器
            }

            /**
             * @brief 重置Boss状态
             */
            void Reset() override
            {
                me->RemoveAurasDueToSpell(SPELL_SHADE);  // 移除阴影视觉效果

                Initialize();

                emerald_dragonAI::Reset();
                events.ScheduleEvent(EVENT_ARCANE_BLAST, 12s);    // 12秒后施放奥术冲击
                events.ScheduleEvent(EVENT_BELLOWING_ROAR, 30s);  // 30秒后施放咆哮
            }

            /**
             * @brief 进入战斗回调
             * @param who 进入战斗的目标
             */
            void JustEngagedWith(Unit* who) override
            {
                Talk(SAY_TAERAR_AGGRO);  // 说开场白
                emerald_dragonAI::JustEngagedWith(who);
            }

            /**
             * @brief 召唤生物死亡回调
             * @param summon 死亡的召唤生物
             * @param killer 击杀者（未使用）
             *
             * 当阴影分身死亡时减少计数器。
             */
            void SummonedCreatureDies(Creature* /*summon*/, Unit* /*killer*/) override
            {
                --_shades;  // 减少存活阴影数量
            }

            /**
             * @brief 受到伤害回调
             * @param attacker 攻击者（未使用）
             * @param damage 伤害值（未使用）
             * @param damageType 伤害类型（未使用）
             * @param spellInfo 法术信息（未使用）
             *
             * 在75%、50%、25%血量时消失并召唤3个阴影分身。
             */
            void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                // 在75%、50%或25%血量时，激活阴影并进入消失状态
                // 注意：_stage记录了阴影被召唤的次数
                if (!_banished && !HealthAbovePct(100 - 25 * _stage))
                {
                    _banished = true;
                    _banishedTimer = 60000;  // 60秒超时

                    me->InterruptNonMeleeSpells(false);  // 中断所有施法
                    DoStopAttack();  // 停止攻击

                    Talk(SAY_TAERAR_SUMMON_SHADES);  // 说召唤台词

                    // 召唤3个阴影分身
                    uint32 count = sizeof(TaerarShadeSpells) / sizeof(uint32);
                    for (uint32 i = 0; i < count; ++i)
                        DoCastVictim(TaerarShadeSpells[i], true);
                    _shades += count;

                    // Boss进入消失状态
                    DoCast(SPELL_SHADE);  // 施放阴影视觉效果
                    me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE);  // 设置为不可交互
                    me->SetReactState(REACT_PASSIVE);  // 被动状态

                    ++_stage;
                }
            }

            /**
             * @brief 执行事件
             * @param eventId 事件ID
             *
             * 处理泰拉尔独特技能：
             * - EVENT_ARCANE_BLAST：施放奥术冲击
             * - EVENT_BELLOWING_ROAR：施放咆哮
             */
            void ExecuteEvent(uint32 eventId) override
            {
                switch (eventId)
                {
                    case EVENT_ARCANE_BLAST:
                        DoCast(SPELL_ARCANE_BLAST);
                        events.ScheduleEvent(EVENT_ARCANE_BLAST, 7s, 12s);  // 7-12秒后再次施放
                        break;
                    case EVENT_BELLOWING_ROAR:
                        DoCast(SPELL_BELLOWING_ROAR);
                        events.ScheduleEvent(EVENT_BELLOWING_ROAR, 20s, 30s);  // 20-30秒后再次施放
                        break;
                    default:
                        emerald_dragonAI::ExecuteEvent(eventId);
                        break;
                }
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 处理消失状态逻辑：
             * - 当所有阴影被击杀或超时，Boss重新出现
             * - 否则继续等待
             */
            void UpdateAI(uint32 diff) override
            {
                if (!me->IsInCombat())
                    return;

                // 处理消失状态
                if (_banished)
                {
                    // 如果所有阴影都死亡，或者超时，结束当前事件让泰拉尔回归战斗
                    if (_banishedTimer <= diff || !_shades)
                    {
                        _banished = false;

                        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE);
                        me->RemoveAurasDueToSpell(SPELL_SHADE);  // 移除阴影效果
                        me->SetReactState(REACT_AGGRESSIVE);  // 恢复攻击状态
                    }
                    // 超时未到，且仍有存活的阴影
                    else
                        _banishedTimer -= diff;

                    // 更新事件调度器（如果在消失状态检查之外则由emerald_dragonAI::UpdateAI处理）
                    events.Update(diff);

                    return;
                }

                emerald_dragonAI::UpdateAI(diff);
            }

        private:
            bool   _banished;       ///< Boss是否处于消失状态
            uint32 _banishedTimer;  ///< 消失状态计时器（毫秒）
            uint8  _shades;         ///< 存活的阴影数量
            uint8  _stage;          ///< 当前战斗阶段（1-4，对应75%-50%-25%血量计数）
        };

        /**
         * @brief 获取生物AI实例
         * @param creature 生物指针
         * @return 返回新创建的AI实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return new boss_taerarAI(creature);
        }
};

/*
 * --- 法术：梦境迷雾睡眠
 */

/**
 * @class DreamFogTargetSelector
 * @brief 梦境迷雾目标选择器
 *
 * 用于过滤已经处于睡眠状态的目标。
 * 如果目标已经有睡眠效果，则从目标列表中移除。
 */
class DreamFogTargetSelector
{
    public:
        /**
         * @brief 构造函数
         */
        DreamFogTargetSelector() { }

        /**
         * @brief 目标过滤操作符
         * @param object 待检测的世界对象
         * @return true表示应该移除该目标，false表示保留
         */
        bool operator()(WorldObject* object)
        {
            if (Unit* unit = object->ToUnit())
                return unit->HasAura(SPELL_SLEEP);  // 如果已有睡眠效果，移除
            return true;  // 非单位对象也移除
        }
};

/**
 * @class spell_dream_fog_sleep
 * @brief 梦境迷雾睡眠法术脚本
 *
 * 法术ID: 24778 - 睡眠
 *
 * 过滤目标，不对已经处于睡眠状态的玩家施放。
 * 这样可以避免对同一目标重复施放睡眠效果。
 */
class spell_dream_fog_sleep : public SpellScriptLoader
{
    public:
        /**
         * @brief 构造函数
         */
        spell_dream_fog_sleep() : SpellScriptLoader("spell_dream_fog_sleep") { }

        /**
         * @class spell_dream_fog_sleep_SpellScript
         * @brief 睡眠法术脚本实现
         */
        class spell_dream_fog_sleep_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_dream_fog_sleep_SpellScript);

            /**
             * @brief 过滤目标
             * @param targets 目标列表
             *
             * 移除已经处于睡眠状态的目标。
             */
            void FilterTargets(std::list<WorldObject*>& targets)
            {
                targets.remove_if(DreamFogTargetSelector());
            }

            /**
             * @brief 注册法术脚本钩子
             */
            void Register() override
            {
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_dream_fog_sleep_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_DEST_AREA_ENEMY);
            }
        };

        /**
         * @brief 获取法术脚本实例
         * @return 返回新创建的法术脚本
         */
        SpellScript* GetSpellScript() const override
        {
            return new spell_dream_fog_sleep_SpellScript();
        }
};

/*
 * --- 法术：自然之印
 */

/**
 * @class MarkOfNatureTargetSelector
 * @brief 自然之印目标选择器
 *
 * 用于过滤符合自然之印效果条件的目标。
 * 只对有自然之印标记但还没有自然光环的目标生效。
 */
class MarkOfNatureTargetSelector
{
    public:
        /**
         * @brief 构造函数
         */
        MarkOfNatureTargetSelector() { }

        /**
         * @brief 目标过滤操作符
         * @param object 待检测的世界对象
         * @return true表示应该移除该目标，false表示保留
         *
         * 移除不满足条件的目标：
         * - 没有自然之印标记的目标
         * - 已经有自然光环效果的目标
         */
        bool operator()(WorldObject* object)
        {
            // 返回那些没有被标记或已经受自然光环影响的目标
            if (Unit* unit = object->ToUnit())
                return !(unit->HasAura(SPELL_MARK_OF_NATURE) && !unit->HasAura(SPELL_AURA_OF_NATURE));
            return true;  // 非单位对象移除
        }
};

/**
 * @class spell_mark_of_nature
 * @brief 自然之印触发法术脚本
 *
 * 法术ID: 25042 - 自然之印触发法术
 *
 * 这个法术由Boss的自然之印光环每10秒触发一次。
 * 它会检查周围的敌对玩家，对那些有自然之印标记的玩家施放自然光环（昏迷2分钟）。
 *
 * 机制说明：
 * 1. 玩家在战斗中死亡会获得自然之印标记（持续15分钟）
 * 2. Boss的光环每10秒触发此法术
 * 3. 此法术检查周围玩家是否有标记
 * 4. 对有标记的玩家施放自然光环（昏迷2分钟）
 */
class spell_mark_of_nature : public SpellScriptLoader
{
    public:
        /**
         * @brief 构造函数
         */
        spell_mark_of_nature() : SpellScriptLoader("spell_mark_of_nature") { }

        /**
         * @class spell_mark_of_nature_SpellScript
         * @brief 自然之印触发法术脚本实现
         */
        class spell_mark_of_nature_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_mark_of_nature_SpellScript);

            /**
             * @brief 验证法术依赖
             * @param spellInfo 法术信息（未使用）
             * @return 验证是否成功
             */
            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo(
                {
                    SPELL_MARK_OF_NATURE,
                    SPELL_AURA_OF_NATURE
                });
            }

            /**
             * @brief 过滤目标
             * @param targets 目标列表
             *
             * 只保留有自然之印标记且没有自然光环的玩家。
             */
            void FilterTargets(std::list<WorldObject*>& targets)
            {
                targets.remove_if(MarkOfNatureTargetSelector());
            }

            /**
             * @brief 处理法术效果
             * @param effIndex 效果索引
             *
             * 阻止默认效果，改为对目标施放自然光环。
             */
            void HandleEffect(SpellEffIndex effIndex)
            {
                PreventHitDefaultEffect(effIndex);
                GetHitUnit()->CastSpell(GetHitUnit(), SPELL_AURA_OF_NATURE, true);  // 对目标施放自然光环
            }

            /**
             * @brief 注册法术脚本钩子
             */
            void Register() override
            {
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_mark_of_nature_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
                OnEffectHitTarget += SpellEffectFn(spell_mark_of_nature_SpellScript::HandleEffect, EFFECT_0, SPELL_EFFECT_APPLY_AURA);
            }
        };

        /**
         * @brief 获取法术脚本实例
         * @return 返回新创建的法术脚本
         */
        SpellScript* GetSpellScript() const override
        {
            return new spell_mark_of_nature_SpellScript();
        }
};

/**
 * @brief 注册翡翠梦境龙脚本
 *
 * 该函数由脚本系统在启动时调用，用于注册本文件中定义的所有脚本。
 * 包括：
 * - 辅助NPC脚本（梦境迷雾、灵魂阴影）
 * - 四只翡翠梦境龙Boss脚本
 * - 相关法术脚本
 */
void AddSC_emerald_dragons()
{
    // 辅助NPC脚本
    new npc_dream_fog();     // 梦境迷雾
    new npc_spirit_shade();  // 灵魂阴影（莱索恩召唤）

    // 四只翡翠梦境龙Boss
    new boss_ysondre();      // 伊森德雷
    new boss_taerar();       // 泰拉尔
    new boss_emeriss();      // 艾莫莉丝
    new boss_lethon();       // 莱索恩

    // 龙相关法术脚本
    new spell_dream_fog_sleep();   // 梦境迷雾睡眠
    new spell_mark_of_nature();    // 自然之印
}
