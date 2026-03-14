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
 * @file boss_kazrogal.cpp
 * @brief 海加尔山副本 - 卡兹洛加Boss战脚本模块
 *
 * 本模块实现了卡兹洛加（Kaz'rogal）Boss的战斗逻辑，包括：
 * - Boss AI行为和技能施放
 * - 卡兹洛加印记法术机制
 * - Boss的移动路径和仇恨管理
 *
 * 卡兹洛加是海加尔山战役中的第三个Boss，擅长使用顺劈斩、战争践踏和卡兹洛加印记。
 * 卡兹洛加印记是核心机制：会燃烧目标的法力值，当法力耗尽时造成大量伤害。
 */

#include "ScriptMgr.h"
#include "hyjal.h"
#include "hyjal_trash.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

/**
 * @brief 卡兹洛加Boss技能枚举定义
 */
enum Spells
{
    SPELL_CLEAVE        = 31436,    // 顺劈斩：对当前目标及其附近敌人造成伤害
    SPELL_WARSTOMP      = 31480,    // 战争践踏：AOE击晕效果
    SPELL_MARK          = 31447,    // 卡兹洛加印记：燃烧法力值，法力耗尽时造成伤害
    SPELL_MARK_DAMAGE   = 31463     // 卡兹洛加印记伤害法术：法力耗尽时触发
};

/**
 * @brief Boss对话文本ID枚举
 */
enum Texts
{
    SAY_ONSLAY          = 0,    // 击杀玩家时台词
    SAY_MARK            = 1,    // 施放印记时台词
    SAY_ONAGGRO         = 2,    // 进入战斗时台词
};

/**
 * @brief Boss音效ID枚举
 */
enum Sounds
{
    SOUND_ONDEATH       = 11018,    // 死亡时音效
};

/**
 * @brief 卡兹洛加Boss脚本类
 *
 * 负责注册和管理卡兹洛加Boss的AI实例
 */
class boss_kazrogal : public CreatureScript
{
public:
    /**
     * @brief 构造函数，注册Boss脚本名称
     */
    boss_kazrogal() : CreatureScript("boss_kazrogal") { }

    /**
     * @brief 获取Boss AI实例
     * @param creature 生物对象指针
     * @return 返回Boss AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetHyjalAI<boss_kazrogalAI>(creature);
    }

    /**
     * @brief 卡兹洛加Boss AI结构体
     *
     * 继承自hyjal_trashAI，实现了卡兹洛加的战斗逻辑：
     * - 技能计时器管理
     * - 路径导航到萨尔处
     * - 战斗状态同步到实例脚本
     * - 卡兹洛加印记机制（逐步加快施放频率）
     */
    struct boss_kazrogalAI : public hyjal_trashAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化Boss的基本属性和实例数据
         */
        boss_kazrogalAI(Creature* creature) : hyjal_trashAI(creature)
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
            damageTaken = 0;
            CleaveTimer = 5000;         // 顺劈斩初始计时器5秒
            WarStompTimer = 15000;      // 战争践踏初始计时器15秒
            MarkTimer = 45000;          // 印记初始计时器45秒
            MarkTimerBase = 45000;      // 印记基础计时器（会逐步减少）
        }

        uint32 CleaveTimer;     // 顺劈斩技能冷却计时器
        uint32 WarStompTimer;   // 战争践踏冷却计时器
        uint32 MarkTimer;       // 卡兹洛加印记冷却计时器
        uint32 MarkTimerBase;   // 卡兹洛加印记基础计时器（战斗中会减少）
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
                instance->SetBossState(DATA_KAZROGAL, NOT_STARTED);
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
                instance->SetBossState(DATA_KAZROGAL, IN_PROGRESS);
            Talk(SAY_ONAGGRO);
        }

        /**
         * @brief 击杀单位回调
         * @param victim 被击杀的单位（未使用）
         *
         * 当Boss击杀玩家时播放击杀台词
         */
        void KilledUnit(Unit* /*victim*/) override
        {
            Talk(SAY_ONSLAY);
        }

        /**
         * @brief 到达路径点回调
         * @param waypointId 路径点ID
         * @param pathId 路径ID（未使用）
         *
         * 当Boss到达指定路径点时触发：
         * - 路径点7：到达营地，将萨尔设为仇恨目标
         */
        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            if (waypointId == 7 && instance)
            {
                // 获取萨尔并添加仇恨，使Boss向她移动
                Creature* target = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_THRALL));
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
         * - 播放死亡音效
         */
        void JustDied(Unit* killer) override
        {
            hyjal_trashAI::JustDied(killer);
            if (IsEvent)
                instance->SetBossState(DATA_KAZROGAL, DONE);
            DoPlaySoundToSet(me, SOUND_ONDEATH);
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 主要AI更新循环：
         * 1. 如果是事件模式，更新护送AI并设置移动路径
         * 2. 更新所有技能计时器
         * 3. 根据计时器施放技能
         * 4. 卡兹洛加印记会逐步加快施放频率
         * 5. 执行近战攻击
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
                    // 设置从部落营地入口到萨尔位置的路径点
                    AddWaypoint(0, 5492.91f,    -2404.61f,    1462.63f);
                    AddWaypoint(1, 5531.76f,    -2460.87f,    1469.55f);
                    AddWaypoint(2, 5554.58f,    -2514.66f,    1476.12f);
                    AddWaypoint(3, 5554.16f,    -2567.23f,    1479.90f);
                    AddWaypoint(4, 5540.67f,    -2625.99f,    1480.89f);
                    AddWaypoint(5, 5508.16f,    -2659.2f,    1480.15f);
                    AddWaypoint(6, 5489.62f,    -2704.05f,    1482.18f);
                    AddWaypoint(7, 5457.04f,    -2726.26f,    1485.10f);
                    Start(false, true);
                    SetDespawnAtEnd(false);
                }
            }

            // 如果没有目标则返回
            if (!UpdateVictim())
                return;

            // 顺劈斩：对当前目标施放
            if (CleaveTimer <= diff)
            {
                DoCast(me, SPELL_CLEAVE);
                CleaveTimer = 6000 + rand32() % 15000;  // 6-21秒冷却
            } else CleaveTimer -= diff;

            // 战争践踏：AOE击晕
            if (WarStompTimer <= diff)
            {
                DoCast(me, SPELL_WARSTOMP);
                WarStompTimer = 60000;  // 60秒冷却
            } else WarStompTimer -= diff;

            // 卡兹洛加印记：燃烧法力值
            if (MarkTimer <= diff)
            {
                DoCastAOE(SPELL_MARK);

                // 每次施放后减少基础计时器，加快施放频率
                MarkTimerBase -= 5000;  // 减少5秒
                if (MarkTimerBase < 5500)
                    MarkTimerBase = 5500;  // 最低5.5秒
                MarkTimer = MarkTimerBase;
                Talk(SAY_MARK);
            } else MarkTimer -= diff;

            // 执行近战攻击
            DoMeleeAttackIfReady();
        }
    };

};

/**
 * @brief 卡兹洛加印记目标过滤器
 *
 * 用于过滤出拥有法力值的目标，只有有法力的目标才会受到印记影响
 */
class MarkTargetFilter
{
    public:
        /**
         * @brief 过滤操作符
         * @param target 目标对象
         * @return true表示过滤掉该目标（没有法力），false表示保留该目标（有法力）
         */
        bool operator()(WorldObject* target) const
        {
            if (Unit* unit = target->ToUnit())
                return unit->GetPowerType() != POWER_MANA;  // 过滤掉非法力职业
            return false;
        }
};

/**
 * @brief 卡兹洛加印记法术脚本（Spell ID: 31447）
 *
 * 处理卡兹洛加印记的法术逻辑：
 * - 只对有法力值的目标生效
 * - 周期性燃烧目标的法力值
 * - 当目标法力耗尽时，造成大量伤害并移除印记
 */
class spell_mark_of_kazrogal : public SpellScriptLoader
{
    public:
        /**
         * @brief 构造函数，注册法术脚本名称
         */
        spell_mark_of_kazrogal() : SpellScriptLoader("spell_mark_of_kazrogal") { }

        /**
         * @brief 卡兹洛加印记法术脚本类
         *
         * 处理法术的目标选择，过滤掉非法力职业
         */
        class spell_mark_of_kazrogal_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_mark_of_kazrogal_SpellScript);

            /**
             * @brief 过滤目标列表
             * @param targets 目标列表
             *
             * 从目标列表中移除没有法力值的目标（如战士、盗贼等）
             */
            void FilterTargets(std::list<WorldObject*>& targets)
            {
                targets.remove_if(MarkTargetFilter());
            }

            /**
             * @brief 注册法术效果触发器
             */
            void Register() override
            {
                OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_mark_of_kazrogal_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
            }
        };

        /**
         * @brief 卡兹洛加印记光环脚本类
         *
         * 处理印记的持续效果，周期性检查法力值
         */
        class spell_mark_of_kazrogal_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_mark_of_kazrogal_AuraScript);

            /**
             * @brief 验证法术信息
             * @param spell 法术信息（未使用）
             * @return 验证是否成功
             *
             * 验证伤害法术是否存在
             */
            bool Validate(SpellInfo const* /*spell*/) override
            {
                return ValidateSpellInfo({ SPELL_MARK_DAMAGE });
            }

            /**
             * @brief 周期性效果处理
             * @param aurEff 光环效果指针
             *
             * 每个周期检查目标法力值：
             * - 如果法力值为0，施放伤害法术并移除印记
             * - 否则继续燃烧法力
             */
            void OnPeriodic(AuraEffect const* aurEff)
            {
                Unit* target = GetTarget();

                if (target->GetPower(POWER_MANA) == 0)
                {
                    target->CastSpell(target, SPELL_MARK_DAMAGE, aurEff);  // 施放伤害法术
                    // 移除光环
                    SetDuration(0);
                }
            }

            /**
             * @brief 注册光环效果触发器
             */
            void Register() override
            {
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_mark_of_kazrogal_AuraScript::OnPeriodic, EFFECT_0, SPELL_AURA_PERIODIC_MANA_LEECH);
            }
        };

        /**
         * @brief 获取法术脚本实例
         * @return 法术脚本指针
         */
        SpellScript* GetSpellScript() const override
        {
            return new spell_mark_of_kazrogal_SpellScript();
        }

        /**
         * @brief 获取光环脚本实例
         * @return 光环脚本指针
         */
        AuraScript* GetAuraScript() const override
        {
            return new spell_mark_of_kazrogal_AuraScript();
        }
};

/**
 * @brief 注册卡兹洛加Boss相关脚本
 *
 * 此函数在世界服务器启动时被调用，注册：
 * - Boss AI脚本
 * - 卡兹洛加印记法术脚本
 */
void AddSC_boss_kazrogal()
{
    new boss_kazrogal();
    new spell_mark_of_kazrogal();
}
