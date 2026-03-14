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
 * @file boss_darkmaster_gandling.cpp
 * @brief 通灵学院最终BOSS黑暗院长甘德林战斗脚本
 *
 * 本模块实现了通灵学院最终BOSS黑暗院长甘德林的战斗AI：
 * - 黑暗院长甘德林（最终BOSS）
 * - 暗影传送门法术系统
 * - 复活的守卫（随从）
 *
 * 战斗机制：
 * 1. 甘德林在击杀所有小BOSS后自动刷新
 * 2. 使用奥术飞弹、暗影护盾、诅咒等技能
 * 3. 暗影传送门将玩家传送到各个密室并锁门，召唤小怪
 * 4. 玩家需要击败小怪后才能返回主战场
 *
 * 特殊机制：
 * - 暗影传送门：随机传送到6个密室之一
 * - 密室挑战：每个密室有3个复活的守卫
 * - 门锁机制：传送后密室门会关闭
 *
 * 完成度：90%
 * 已知问题：门在战斗结束/重置时不会重新打开
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "scholomance.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

/**
 * @brief 对话文本ID枚举
 *
 * 定义甘德林在战斗中的对话文本ID
 */
enum Says
{
   YELL_SUMMONED = 0  // 被召唤时的喊话
};

/**
 * @brief 法术ID枚举
 *
 * 定义甘德林使用的所有法术ID
 */
enum Spells
{
    SPELL_ARCANEMISSILES = 15790,  // 奥术飞弹 - 主要攻击技能
    SPELL_SHADOWSHIELD   = 12040,  // 暗影护盾 - 保护性法术
    SPELL_CURSE          = 18702,  // 诅咒 - 减益效果
    SPELL_SHADOW_PORTAL  = 17950   // 暗影传送门 - 传送玩家到密室
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum Events
{
    EVENT_ARCANEMISSILES = 1,  // 奥术飞弹事件
    EVENT_SHADOWSHIELD   = 2,  // 暗影护盾事件
    EVENT_CURSE          = 3,  // 诅咒事件
    EVENT_SHADOW_PORTAL  = 4   // 暗影传送门事件
};

/**
 * @brief 黑暗院长甘德林脚本类
 *
 * 实现甘德林的战斗AI，包括：
 * - 常规战斗技能（奥术飞弹、暗影护盾、诅咒）
 * - 暗影传送门机制（将玩家传送到密室）
 * - 战斗开始和结束时的门控
 */
class boss_darkmaster_gandling : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册脚本名称
         */
        boss_darkmaster_gandling() : CreatureScript("boss_darkmaster_gandling") { }

        /**
         * @brief 黑暗院长甘德林AI结构体
         *
         * 实现甘德林的战斗AI逻辑，继承自BossAI
         */
        struct boss_darkmaster_gandlingAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_darkmaster_gandlingAI(Creature* creature) : BossAI(creature, DATA_DARKMASTER_GANDLING) { }

            /**
             * @brief 重置AI状态
             *
             * 当战斗重置时调用：
             * - 重置BOSS状态
             * - 打开甘德林大门
             *
             * 调用时机：战斗重置或BOSS脱离战斗时
             */
            void Reset() override
            {
                _Reset();
                // 打开甘德林大门
                if (GameObject* gate = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_GATE_GANDLING)))
                    gate->SetGoState(GO_STATE_ACTIVE);
            }

            /**
             * @brief 死亡事件
             * @param killer 击杀者
             *
             * 当甘德林死亡时：
             * - 处理BOSS死亡逻辑
             * - 打开甘德林大门
             *
             * 调用时机：BOSS被击杀时
             */
            void JustDied(Unit* /*killer*/) override
            {
                _JustDied();
                // 打开甘德林大门
                if (GameObject* gate = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_GATE_GANDLING)))
                    gate->SetGoState(GO_STATE_ACTIVE);
            }

            /**
             * @brief 进入战斗事件
             * @param who 进入战斗的目标
             *
             * 当甘德林进入战斗时：
             * - 安排技能施放事件
             * - 关闭甘德林大门（锁门）
             *
             * 调用时机：BOSS进入战斗时
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);

                // 安排技能施放时间表
                events.ScheduleEvent(EVENT_ARCANEMISSILES, 4500ms);  // 4.5秒后施放奥术飞弹
                events.ScheduleEvent(EVENT_SHADOWSHIELD, 12s);       // 12秒后施放暗影护盾
                events.ScheduleEvent(EVENT_CURSE, 2s);               // 2秒后施放诅咒
                events.ScheduleEvent(EVENT_SHADOW_PORTAL, 15s);      // 15秒后施放暗影传送门

                // 关闭甘德林大门（锁门）
                if (GameObject* gate = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_GATE_GANDLING)))
                    gate->SetGoState(GO_STATE_READY);
            }

            /**
             * @brief 被召唤事件
             * @param summoner 召唤者
             *
             * 当甘德林被召唤时：
             * - 喊出召唤台词
             * - 开始随机移动
             *
             * 调用时机：击杀所有小BOSS后由副本脚本召唤
             */
            void IsSummonedBy(WorldObject* /*summoner*/) override
            {
                Talk(YELL_SUMMONED);  // 喊出召唤台词
                me->GetMotionMaster()->MoveRandom(5);  // 在5码范围内随机移动
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 每帧调用，处理战斗逻辑：
             * - 检查是否有战斗目标
             * - 更新事件计时器
             * - 检查施法状态
             * - 执行技能事件
             * - 进行近战攻击
             *
             * 调用时机：每帧由核心代码调用
             */
            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())  // 没有战斗目标则返回
                    return;

                events.Update(diff);  // 更新事件计时器

                if (me->HasUnitState(UNIT_STATE_CASTING))  // 正在施法则等待
                    return;

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_ARCANEMISSILES:
                            // 对当前目标施放奥术飞弹
                            DoCastVictim(SPELL_ARCANEMISSILES, true);
                            events.ScheduleEvent(EVENT_ARCANEMISSILES, 8s);  // 8秒后再次施放
                            break;
                        case EVENT_SHADOWSHIELD:
                            // 对自己施放暗影护盾
                            DoCast(me, SPELL_SHADOWSHIELD);
                            events.ScheduleEvent(EVENT_SHADOWSHIELD, 14s, 28s);  // 14-28秒后再次施放
                            break;
                        case EVENT_CURSE:
                            // 对当前目标施放诅咒
                            DoCastVictim(SPELL_CURSE, true);
                            events.ScheduleEvent(EVENT_CURSE, 15s, 27s);  // 15-27秒后再次施放
                            break;
                        case EVENT_SHADOW_PORTAL:
                            // 只要血量高于3%就施放暗影传送门
                            if (HealthAbovePct(3))
                            {
                                // 随机选择一个玩家施放暗影传送门
                                DoCast(SelectTarget(SelectTargetMethod::Random, 0, 100, true), SPELL_SHADOW_PORTAL, true);
                                events.ScheduleEvent(EVENT_SHADOW_PORTAL, 17s, 27s);  // 17-27秒后再次施放
                            }
                            break;
                    }

                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                DoMeleeAttackIfReady();  // 如果可以，进行近战攻击
            }
        };

        /**
         * @brief 获取AI
         * @param creature 生物对象指针
         * @return AI对象指针
         *
         * 创建并返回甘德林AI对象
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetScholomanceAI<boss_darkmaster_gandlingAI>(creature);
        }
};

/**
 * @brief 暗影传送门密室枚举
 *
 * 定义暗影传送门可以传送到的6个密室
 */
enum Rooms
{
    ROOM_HALL_OF_SECRETS        = 0,  // 秘密大厅
    ROOM_HALL_OF_THE_DAMNED     = 1,  // 诅咒大厅
    ROOM_THE_COVEN              = 2,  // 女巫集会
    ROOM_THE_SHADOW_VAULT       = 3,  // 暗影仓库
    ROOM_BAROV_FAMILY_VAULT     = 4,  // 巴罗夫家族仓库
    ROOM_VAULT_OF_THE_RAVENIAN  = 5   // 雷文尼亚仓库
};

/**
 * @brief 暗影传送门法术ID枚举
 *
 * 定义传送到各个密室的具体法术ID
 */
enum SPSpells
{
    SPELL_SHADOW_PORTAL_HALLOFSECRETS          = 17863,  // 传送到秘密大厅
    SPELL_SHADOW_PORTAL_HALLOFTHEDAMNED        = 17939,  // 传送到诅咒大厅
    SPELL_SHADOW_PORTAL_THECOVEN               = 17943,  // 传送到女巫集会
    SPELL_SHADOW_PORTAL_THESHADOWVAULT         = 17944,  // 传送到暗影仓库
    SPELL_SHADOW_PORTAL_BAROVFAMILYVAULT       = 17946,  // 传送到巴罗夫家族仓库
    SPELL_SHADOW_PORTAL_VAULTOFTHERAVENIAN     = 17948   // 传送到雷文尼亚仓库
};

/**
 * @brief 暗影传送门法术脚本（法术ID：17950）
 *
 * 实现暗影传送门的传送逻辑：
 * 1. 随机选择一个密室
 * 2. 检查密室门是否开启（确保密室未被占用）
 * 3. 施放对应的传送法术
 *
 * 调用时机：甘德林施放暗影传送门法术时
 */
class spell_shadow_portal : public SpellScriptLoader
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册法术脚本名称
         */
        spell_shadow_portal() : SpellScriptLoader("spell_shadow_portal") { }

        /**
         * @brief 暗影传送门法术脚本实现类
         */
        class spell_shadow_portal_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_shadow_portal_SpellScript);

            /**
             * @brief 加载脚本
             * @return 是否加载成功
             *
             * 检查施法者是否在正确的副本中
             */
            bool Load() override
            {
                _instance = GetCaster()->GetInstanceScript();
                return InstanceHasScript(GetCaster(), ScholomanceScriptName);
            }

            /**
             * @brief 处理施法效果
             * @param effIndex 法术效果索引
             *
             * 核心逻辑：随机选择一个可用的密室并施放传送法术
             * - 最多尝试6次选择密室
             * - 检查密室门是否开启
             * - 施放对应的传送法术
             *
             * 调用时机：法术效果触发时
             */
            void HandleCast(SpellEffIndex /*effIndex*/)
            {
                Unit* caster = GetCaster();
                uint8 attempts = 0;
                uint32 spellId = 0;

                // 尝试选择一个可用的密室
                while (!spellId)
                {
                    if (attempts++ >= 6) break;  // 最多尝试6次

                    switch (urand(0, 5))  // 随机选择0-5之间的密室
                    {
                        case ROOM_HALL_OF_SECRETS:
                            // 检查秘密大厅的门是否开启
                            if (GameObject* go = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(GO_GATE_RAVENIAN)))
                                if (go->GetGoState() == GO_STATE_ACTIVE)
                                    spellId = SPELL_SHADOW_PORTAL_HALLOFSECRETS;
                            break;
                        case ROOM_HALL_OF_THE_DAMNED:
                            // 检查诅咒大厅的门是否开启
                            if (GameObject* go = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(GO_GATE_THEOLEN)))
                                if (go->GetGoState() == GO_STATE_ACTIVE)
                                    spellId = SPELL_SHADOW_PORTAL_HALLOFTHEDAMNED;
                            break;
                        case ROOM_THE_COVEN:
                            // 检查女巫集会的门是否开启
                            if (GameObject* go = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(GO_GATE_MALICIA)))
                                if (go->GetGoState() == GO_STATE_ACTIVE)
                                    spellId = SPELL_SHADOW_PORTAL_THECOVEN;
                            break;
                        case ROOM_THE_SHADOW_VAULT:
                            // 检查暗影仓库的门是否开启
                            if (GameObject* go = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(GO_GATE_ILLUCIA)))
                                if (go->GetGoState() == GO_STATE_ACTIVE)
                                    spellId = SPELL_SHADOW_PORTAL_THESHADOWVAULT;
                            break;
                        case ROOM_BAROV_FAMILY_VAULT:
                            // 检查巴罗夫家族仓库的门是否开启
                            if (GameObject* go = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(GO_GATE_BAROV)))
                                if (go->GetGoState() == GO_STATE_ACTIVE)
                                    spellId = SPELL_SHADOW_PORTAL_BAROVFAMILYVAULT;
                            break;
                        case ROOM_VAULT_OF_THE_RAVENIAN:
                            // 检查雷文尼亚仓库的门是否开启
                            if (GameObject* go = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(GO_GATE_POLKELT)))
                                if (go->GetGoState() == GO_STATE_ACTIVE)
                                    spellId = SPELL_SHADOW_PORTAL_VAULTOFTHERAVENIAN;
                            break;
                    }

                    // 如果找到了可用的密室，施放传送法术
                    if (spellId)
                        GetHitUnit()->CastSpell(GetHitUnit(), spellId);
                }
            }

            /**
             * @brief 注册法术效果处理函数
             */
            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_shadow_portal_SpellScript::HandleCast, EFFECT_0, SPELL_EFFECT_DUMMY);
            }

            InstanceScript* _instance = nullptr;  // 副本脚本实例
        };

        /**
         * @brief 获取法术脚本
         * @return 法术脚本对象指针
         */
        SpellScript* GetSpellScript() const override
        {
            return new spell_shadow_portal_SpellScript();
        }
};

/**
 * @brief 复活的守卫刷新位置数组
 *
 * 定义各个密室中复活的守卫的刷新位置（共18个位置，每个密室3个）
 * 注意：秘密大厅和雷文尼亚仓库的位置尚未定义
 */
Position const SummonPos[18] =
{
    // Hall of Secrects - 秘密大厅（尚未定义）

    // The Hall of the damned - 诅咒大厅
    { 177.9624f, -68.23893f, 84.95197f, 3.228859f },
    { 183.7705f, -61.43489f, 84.92424f, 5.148721f },
    { 184.7035f, -77.74805f, 84.92424f, 4.660029f },
    // The Coven - 女巫集会
    { 111.7203f, -1.105035f, 85.45985f, 3.961897f },
    { 118.0079f, 6.430664f, 85.31169f, 2.408554f },
    { 120.0276f, -7.496636f, 85.31169f, 2.984513f },
    // The Shadow Vault - 暗影仓库
    { 245.3716f, 0.628038f, 72.73877f, 0.01745329f },
    { 240.9920f, 3.405653f, 72.73877f, 6.143559f },
    { 240.9543f, -3.182943f, 72.73877f, 0.2268928f },
    // Barov Family Vault - 巴罗夫家族仓库
    { 181.8245f, -42.58117f, 75.4812f, 4.660029f },
    { 177.7456f, -42.74745f, 75.4812f, 4.886922f },
    { 185.6157f, -42.91200f, 75.4812f, 4.45059f },
    // Vault of the Ravenian - 雷文尼亚仓库（尚未定义）

};

/**
 * @brief 生物ID枚举
 *
 * 定义战斗中涉及的NPC ID
 */
enum Creatures
{
    NPC_RISEN_GUARDIAN = 11598  // 复活的守卫 - 密室中的小怪
};

/**
 * @brief 脚本事件ID枚举
 *
 * 定义各个密室传送法术对应的事件ID
 */
enum ScriptEventId
{
    SPELL_EVENT_HALLOFSECRETS          = 5618,  // 秘密大厅事件
    SPELL_EVENT_HALLOFTHEDAMNED        = 5619,  // 诅咒大厅事件
    SPELL_EVENT_THECOVEN               = 5620,  // 女巫集会事件
    SPELL_EVENT_THESHADOWVAULT         = 5621,  // 暗影仓库事件
    SPELL_EVENT_BAROVFAMILYVAULT       = 5622,  // 巴罗夫家族仓库事件
    SPELL_EVENT_VAULTOFTHERAVENIAN     = 5623   // 雷文尼亚仓库事件
};

/**
 * @brief 暗影传送门密室法术脚本（法术ID：17863, 17939, 17943, 17944, 17946, 17948）
 *
 * 实现传送到各个密室后的处理逻辑：
 * 1. 在密室中召唤3个复活的守卫
 * 2. 关闭密室门（锁门）
 * 3. 守卫开始随机移动
 *
 * 调用时机：玩家被传送到密室后
 */
class spell_shadow_portal_rooms : public SpellScriptLoader
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册法术脚本名称
         */
        spell_shadow_portal_rooms() : SpellScriptLoader("spell_shadow_portal_rooms") { }

        /**
         * @brief 暗影传送门密室法术脚本实现类
         */
        class spell_shadow_portal_rooms_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_shadow_portal_rooms_SpellScript);

            /**
             * @brief 加载脚本
             * @return 是否加载成功
             *
             * 检查施法者是否在正确的副本中
             */
            bool Load() override
            {
                _instance = GetCaster()->GetInstanceScript();
                return InstanceHasScript(GetCaster(), ScholomanceScriptName);
            }

            /**
             * @brief 处理发送事件
             * @param effIndex 法术效果索引
             *
             * 核心逻辑：
             * 1. 根据事件ID确定密室参数（刷新位置、阶段、要关闭的门）
             * 2. 在密室中召唤3个复活的守卫
             * 3. 关闭密室门
             *
             * 调用时机：法术效果触发时
             */
            void HandleSendEvent(SpellEffIndex /*effIndex*/)
            {
                // If only one player in threat list fail spell
                // 如果威胁列表中只有一个玩家，法术失败

                Unit* caster = GetCaster();

                int8 pos_to_summon = 0;    // 刷新位置索引
                int8 phase_to_set = 0;      // 要设置的阶段
                int32 gate_to_close = 0;    // 要关闭的门ID

                // 根据事件ID确定密室参数
                switch (GetEffectInfo().MiscValue)
                {
                    case SPELL_EVENT_HALLOFSECRETS:
                        pos_to_summon = 0;  // Not yet spawned - 尚未定义
                        phase_to_set = 1;
                        gate_to_close = GO_GATE_RAVENIAN;
                        break;
                    case SPELL_EVENT_HALLOFTHEDAMNED:
                        pos_to_summon = 0;
                        phase_to_set = 2;
                        gate_to_close = GO_GATE_THEOLEN;
                        break;
                    case SPELL_EVENT_THECOVEN:
                        pos_to_summon = 3;
                        phase_to_set = 3;
                        gate_to_close = GO_GATE_MALICIA;
                        break;
                    case SPELL_EVENT_THESHADOWVAULT:
                        pos_to_summon = 6;
                        phase_to_set = 4;
                        gate_to_close = GO_GATE_ILLUCIA;
                        break;
                    case SPELL_EVENT_BAROVFAMILYVAULT:
                        pos_to_summon = 9;
                        phase_to_set = 5;
                        gate_to_close = GO_GATE_BAROV;
                        break;
                    case SPELL_EVENT_VAULTOFTHERAVENIAN:
                        pos_to_summon = 0;  // Not yet spawned - 尚未定义
                        phase_to_set = 6;
                        gate_to_close = GO_GATE_POLKELT;
                        break;
                    default:
                        break;
                }

                // 如果有要关闭的门
                if (gate_to_close)
                {
                    // 召唤3个复活的守卫
                    for (uint8 i = 0; i < 3; ++i)
                    {
                        if (Creature* Summoned = caster->SummonCreature(NPC_RISEN_GUARDIAN, SummonPos[pos_to_summon++], TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 2min))
                        {
                            Summoned->GetMotionMaster()->MoveRandom(5);  // 在5码范围内随机移动
                            Summoned->AI()->SetData(0, phase_to_set);    // 设置阶段数据
                        }
                    }

                    // 关闭密室门
                    if (GameObject* gate = ObjectAccessor::GetGameObject(*caster, _instance->GetGuidData(gate_to_close)))
                        gate->SetGoState(GO_STATE_READY);
                }
            }

            /**
             * @brief 注册法术效果处理函数
             */
            void Register() override
            {
                OnEffectHit += SpellEffectFn(spell_shadow_portal_rooms_SpellScript::HandleSendEvent, EFFECT_1, SPELL_EFFECT_SEND_EVENT);
            }

            InstanceScript* _instance = nullptr;  // 副本脚本实例
        };

        /**
         * @brief 获取法术脚本
         * @return 法术脚本对象指针
         */
        SpellScript* GetSpellScript() const override
        {
            return new spell_shadow_portal_rooms_SpellScript();
        }
};

/**
 * @brief 注册BOSS和法术脚本
 *
 * 将甘德林AI和暗影传送门相关法术脚本注册到脚本系统中
 */
void AddSC_boss_darkmaster_gandling()
{
    new boss_darkmaster_gandling();  // 注册甘德林BOSS脚本
    new spell_shadow_portal();       // 注册暗影传送门法术脚本
    new spell_shadow_portal_rooms(); // 注册暗影传送门密室法术脚本
}
