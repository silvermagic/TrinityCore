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
 * @file boss_anetheron.cpp
 * @brief 海加尔山副本 - 阿纳塞隆Boss战脚本模块
 *
 * 本模块实现了阿纳塞隆（Anetheron）Boss的战斗逻辑，包括：
 * - Boss AI行为和技能施放
 * - 召唤物"塔楼地狱火"的AI控制
 * - 吸血光环法术效果处理
 * - Boss的移动路径和仇恨管理
 *
 * 阿纳塞隆是海加尔山战役中的第二个Boss，擅长使用腐肉蜂群、沉睡、地狱火等技能。
 */

#include "ScriptMgr.h"
#include "hyjal.h"
#include "hyjal_trash.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "SpellScript.h"

/**
 * @brief 阿纳塞隆Boss技能枚举定义
 */
enum Spells
{
    SPELL_CARRION_SWARM         = 31306,    // 腐肉蜂群：对随机目标施放，造成伤害并扩散
    SPELL_SLEEP                 = 31298,    // 沉睡：使3个随机目标陷入沉睡状态
    SPELL_VAMPIRIC_AURA         = 38196,    // 吸血光环：攻击时治疗自身，治疗量为造成伤害的300%
    SPELL_VAMPIRIC_AURA_HEAL    = 31285,    // 吸血光环治疗法术
    SPELL_INFERNO               = 31299,    // 地狱火：召唤塔楼地狱火攻击玩家
    SPELL_IMMOLATION            = 31303,    // 献祭：地狱火的AOE火焰伤害技能
    SPELL_INFERNO_EFFECT        = 31302     // 地狱火召唤效果法术
};

/**
 * @brief Boss对话文本ID枚举
 */
enum Texts
{
    SAY_ONDEATH         = 0,    // 死亡时台词
    SAY_ONSLAY          = 1,    // 击杀玩家时台词
    SAY_SWARM           = 2,    // 施放腐肉蜂群时台词
    SAY_SLEEP           = 3,    // 施放沉睡时台词
    SAY_INFERNO         = 4,    // 召唤地狱火时台词
    SAY_ONAGGRO         = 5,    // 进入战斗时台词
};

/**
 * @brief 阿纳塞隆Boss脚本类
 *
 * 负责注册和管理阿纳塞隆Boss的AI实例
 */
class boss_anetheron : public CreatureScript
{
public:
    /**
     * @brief 构造函数，注册Boss脚本名称
     */
    boss_anetheron() : CreatureScript("boss_anetheron") { }

    /**
     * @brief 获取Boss AI实例
     * @param creature 生物对象指针
     * @return 返回Boss AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetHyjalAI<boss_anetheronAI>(creature);
    }

    /**
     * @brief 阿纳塞隆Boss AI结构体
     *
     * 继承自hyjal_trashAI，实现了阿纳塞隆的战斗逻辑：
     * - 技能计时器管理
     * - 路径导航到吉安娜处
     * - 战斗状态同步到实例脚本
     */
    struct boss_anetheronAI : public hyjal_trashAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化Boss的基本属性和实例数据
         */
        boss_anetheronAI(Creature* creature) : hyjal_trashAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
            go = false;
        }

        /**
         * @brief 初始化技能计时器和状态变量
         *
         * 在Reset()时调用，重置所有技能计时器到初始值
         */
        void Initialize()
        {
            SwarmTimer = 45000;     // 腐肉蜂群初始计时器45秒
            SleepTimer = 60000;     // 沉睡初始计时器60秒
            AuraTimer = 5000;       // 吸血光环初始计时器5秒
            InfernoTimer = 45000;   // 地狱火初始计时器45秒
            damageTaken = 0;        // 受到的伤害累计
        }

        uint32 SwarmTimer;      // 腐肉蜂群技能冷却计时器
        uint32 SleepTimer;      // 沉睡技能冷却计时器
        uint32 AuraTimer;       // 吸血光环技能冷却计时器
        uint32 InfernoTimer;    // 地狱火召唤冷却计时器
        bool go;                // 路径点是否已初始化标志

        /**
         * @brief 重置Boss状态
         *
         * 当Boss脱离战斗或重置时调用：
         * - 重置所有技能计时器
         * - 如果是事件模式，设置Boss状态为NOT_STARTED
         */
        void Reset() override
        {
            Initialize();

            if (IsEvent)
                instance->SetBossState(DATA_ANETHERON, NOT_STARTED);
        }

        /**
         * @brief 进入战斗回调
         * @param who 攻击目标（未使用）
         *
         * 当Boss进入战斗时调用：
         * - 设置实例Boss状态为IN_PROGRESS
         * - 播放进入战斗台词
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            if (IsEvent)
                instance->SetBossState(DATA_ANETHERON, IN_PROGRESS);

            Talk(SAY_ONAGGRO);
        }

        /**
         * @brief 击杀单位回调
         * @param who 被击杀的单位
         *
         * 当Boss击杀玩家时播放击杀台词
         */
        void KilledUnit(Unit* who) override
        {
            if (who->GetTypeId() == TYPEID_PLAYER)
                Talk(SAY_ONSLAY);
        }

        /**
         * @brief 到达路径点回调
         * @param waypointId 路径点ID
         * @param pathId 路径ID（未使用）
         *
         * 当Boss到达指定路径点时触发：
         * - 路径点7：到达营地，将吉安娜设为仇恨目标
         */
        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            if (waypointId == 7)
            {
                // 获取吉安娜并添加仇恨，使Boss向她移动
                Creature* target = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_JAINAPROUDMOORE));
                if (target && target->IsAlive())
                    AddThreat(target, 0.0f);
            }
        }

        /**
         * @brief Boss死亡回调
         * @param killer 击杀者
         *
         * 当Boss死亡时调用：
         * - 调用父类死亡处理
         * - 设置实例Boss状态为DONE
         * - 播放死亡台词
         */
        void JustDied(Unit* killer) override
        {
            hyjal_trashAI::JustDied(killer);
            if (IsEvent)
                instance->SetBossState(DATA_ANETHERON, DONE);
            Talk(SAY_ONDEATH);
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 主要AI更新循环：
         * 1. 如果是事件模式，更新护送AI并设置移动路径
         * 2. 更新所有技能计时器
         * 3. 根据计时器施放技能
         * 4. 执行近战攻击
         *
         * 性能注意事项：
         * - 每帧都会调用此函数
         * - 技能选择使用随机目标，需考虑性能影响
         */
        void UpdateAI(uint32 diff) override
        {
            if (IsEvent)
            {
                // 必须更新护送AI以处理路径移动
                EscortAI::UpdateAI(diff);
                if (!go)
                {
                    go = true;
                    // 设置从联盟营地入口到吉安娜位置的路径点
                    AddWaypoint(0, 4896.08f,    -1576.35f,    1333.65f);
                    AddWaypoint(1, 4898.68f,    -1615.02f,    1329.48f);
                    AddWaypoint(2, 4907.12f,    -1667.08f,    1321.00f);
                    AddWaypoint(3, 4963.18f,    -1699.35f,    1340.51f);
                    AddWaypoint(4, 4989.16f,    -1716.67f,    1335.74f);
                    AddWaypoint(5, 5026.27f,    -1736.89f,    1323.02f);
                    AddWaypoint(6, 5037.77f,    -1770.56f,    1324.36f);
                    AddWaypoint(7, 5067.23f,    -1789.95f,    1321.17f);
                    Start(false, true);
                    SetDespawnAtEnd(false);
                }
            }

            // 如果没有目标则返回
            if (!UpdateVictim())
                return;

            // 腐肉蜂群：对随机目标施放
            if (SwarmTimer <= diff)
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                    DoCast(target, SPELL_CARRION_SWARM);

                SwarmTimer = urand(45000, 60000);  // 45-60秒冷却
                Talk(SAY_SWARM);
            } else SwarmTimer -= diff;

            // 沉睡：使3个随机目标沉睡
            if (SleepTimer <= diff)
            {
                for (uint8 i = 0; i < 3; ++i)
                {
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                        target->CastSpell(target, SPELL_SLEEP, true);
                }
                SleepTimer = 60000;  // 60秒冷却
                Talk(SAY_SLEEP);
            } else SleepTimer -= diff;

            // 吸血光环：增益自身，攻击时治疗
            if (AuraTimer <= diff)
            {
                DoCast(me, SPELL_VAMPIRIC_AURA, true);
                AuraTimer = urand(10000, 20000);  // 10-20秒冷却
            } else AuraTimer -= diff;

            // 地狱火：召唤塔楼地狱火
            if (InfernoTimer <= diff)
            {
                DoCast(SelectTarget(SelectTargetMethod::Random, 0, 100, true), SPELL_INFERNO);
                InfernoTimer = 45000;  // 45秒冷却
                Talk(SAY_INFERNO);
            } else InfernoTimer -= diff;

            // 执行近战攻击
            DoMeleeAttackIfReady();
        }
    };
};

/**
 * @brief 塔楼地狱火NPC脚本类
 *
 * 负责管理阿纳塞隆召唤的地狱火生物的AI行为
 * 地狱火会在Boss死亡后自动消失
 */
class npc_towering_infernal : public CreatureScript
{
public:
    /**
     * @brief 构造函数，注册NPC脚本名称
     */
    npc_towering_infernal() : CreatureScript("npc_towering_infernal") { }

    /**
     * @brief 获取NPC AI实例
     * @param creature 生物对象指针
     * @return 返回NPC AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetHyjalAI<npc_towering_infernalAI>(creature);
    }

    /**
     * @brief 塔楼地狱火AI结构体
     *
     * 实现了地狱火的行为逻辑：
     * - 定期施放献祭技能
     * - 检查Boss是否存活，Boss死亡时自动消失
     * - 主动攻击50码范围内的敌对目标
     */
    struct npc_towering_infernalAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化计时器和实例数据
         */
        npc_towering_infernalAI(Creature* creature) : ScriptedAI(creature)
        {
            ImmolationTimer = 5000;     // 献祭技能初始计时器5秒
            CheckTimer = 5000;          // Boss存活检查计时器5秒
            instance = creature->GetInstanceScript();
        }

        uint32 ImmolationTimer;         // 献祭技能冷却计时器
        uint32 CheckTimer;              // Boss存活检查计时器
        InstanceScript* instance;       // 实例脚本指针

        /**
         * @brief 重置NPC状态
         *
         * 重置时：
         * - 施放地狱火视觉效果
         * - 重置计时器
         */
        void Reset() override
        {
            DoCast(me, SPELL_INFERNO_EFFECT);
            ImmolationTimer = 5000;
            CheckTimer = 5000;
        }

        /**
         * @brief 进入战斗回调（空实现）
         * @param who 攻击目标（未使用）
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
        }

        /**
         * @brief 击杀单位回调（空实现）
         * @param victim 被击杀单位（未使用）
         */
        void KilledUnit(Unit* /*victim*/) override
        {
        }

        /**
         * @brief 死亡回调（空实现）
         * @param killer 击杀者（未使用）
         */
        void JustDied(Unit* /*killer*/) override
        {
        }

        /**
         * @brief 视线移动回调
         * @param who 进入视线的单位
         *
         * 当单位进入视线时：
         * - 检查距离是否在50码内
         * - 如果不在战斗中且目标是有效攻击目标，则发起攻击
         */
        void MoveInLineOfSight(Unit* who) override

        {
            if (me->IsWithinDist(who, 50) && !me->IsInCombat() && me->IsValidAttackTarget(who))
                AttackStart(who);
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 主要更新逻辑：
         * 1. 定期检查Boss是否存活，若Boss死亡则消失
         * 2. 定期施放献祭技能
         * 3. 执行近战攻击
         *
         * 性能注意事项：
         * - 每5秒检查一次Boss状态，避免频繁查询
         */
        void UpdateAI(uint32 diff) override
        {
            // 检查Boss是否存活
            if (CheckTimer <= diff)
            {
                Creature* boss = instance->GetCreature(DATA_ANETHERON);
                if (!boss || boss->isDead())
                {
                    me->DespawnOrUnsummon();  // Boss死亡时地狱火消失
                    return;
                }
                CheckTimer = 5000;  // 每5秒检查一次
            } else CheckTimer -= diff;

            // 如果没有目标则返回
            if (!UpdateVictim())
                return;

            // 施放献祭技能
            if (ImmolationTimer <= diff)
            {
                DoCast(me, SPELL_IMMOLATION);
                ImmolationTimer = 5000;  // 5秒冷却
            } else ImmolationTimer -= diff;

            // 执行近战攻击
            DoMeleeAttackIfReady();
        }
    };
};

/**
 * @brief 吸血光环法术脚本（Spell ID: 38196）
 *
 * 处理阿纳塞隆的吸血光环效果：
 * - 当Boss造成伤害时，触发治疗
 * - 治疗量为造成伤害的300%
 * - 使用触发机制实现伤害到治疗的转换
 */
class spell_anetheron_vampiric_aura : public SpellScriptLoader
{
    public:
        /**
         * @brief 构造函数，注册法术脚本名称
         */
        spell_anetheron_vampiric_aura() : SpellScriptLoader("spell_anetheron_vampiric_aura") { }

        /**
         * @brief 吸血光环光环脚本类
         *
         * 处理光环的触发效果，在Boss造成伤害时进行治疗
         */
        class spell_anetheron_vampiric_aura_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_anetheron_vampiric_aura_AuraScript);

            /**
             * @brief 验证法术信息
             * @param spellInfo 法术信息（未使用）
             * @return 验证是否成功
             *
             * 验证治疗法术是否存在
             */
            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_VAMPIRIC_AURA_HEAL });
            }

            /**
             * @brief 处理触发效果
             * @param aurEff 光环效果指针
             * @param eventInfo 触发事件信息
             *
             * 当光环被触发时：
             * 1. 阻止默认行为
             * 2. 获取伤害信息
             * 3. 计算治疗量（伤害的300%）
             * 4. 对Boss施放治疗法术
             *
             * 性能注意事项：
             * - 每次造成伤害都会触发此函数
             * - 需要高效处理以避免性能问题
             */
            void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
            {
                PreventDefaultAction();
                DamageInfo* damageInfo = eventInfo.GetDamageInfo();
                if (!damageInfo || !damageInfo->GetDamage())
                    return;

                Unit* actor = eventInfo.GetActor();
                CastSpellExtraArgs args(aurEff);
                args.AddSpellMod(SPELLVALUE_BASE_POINT0, damageInfo->GetDamage() * 3);  // 治疗量为伤害的300%
                actor->CastSpell(actor, SPELL_VAMPIRIC_AURA_HEAL, args);
            }

            /**
             * @brief 注册光环效果触发器
             */
            void Register() override
            {
                OnEffectProc += AuraEffectProcFn(spell_anetheron_vampiric_aura_AuraScript::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
            }
        };

        /**
         * @brief 获取光环脚本实例
         * @return 光环脚本指针
         */
        AuraScript* GetAuraScript() const override
        {
            return new spell_anetheron_vampiric_aura_AuraScript();
        }
};

/**
 * @brief 注册阿纳塞隆Boss相关脚本
 *
 * 此函数在世界服务器启动时被调用，注册：
 * - Boss AI脚本
 * - 召唤物NPC脚本
 * - 法术脚本
 */
void AddSC_boss_anetheron()
{
    new boss_anetheron();
    new npc_towering_infernal();
    new spell_anetheron_vampiric_aura();
}
