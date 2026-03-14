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
 * @file boss_keristrasza.cpp
 * @brief 诺森德副本"魔枢"最终BOSS：克莉斯塔萨(Keristrasza)的AI脚本
 *
 * 模块职责：
 * 1. 实现克莉斯塔萨的AI逻辑
 * 2. 管理冻结监狱的解除机制（需要先击杀其他三个BOSS）
 * 3. 处理成就"极度寒冷"的判定逻辑
 * 4. 实现束缚之球的游戏对象交互
 *
 * 战斗机制：
 * - BOSS初始被冻结，需要激活三个束缚之球才能解除
 * - 战斗中玩家需要持续移动以避免叠加"极度寒冷"Debuff
 * - 使用水晶吐息、尾扫、水晶锁链/结晶化等技能
 * - 低血量时进入狂暴状态
 *
 * 成就"极度寒冷"：
 * - 要求玩家在整个战斗中不叠加超过1层"极度寒冷"Debuff
 * - 通过持续移动可以避免Debuff叠加
 *
 * @author TrinityCore Team
 * @date 2026
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "nexus.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

/**
 * @enum Spells
 * @brief BOSS使用的法术ID枚举
 */
enum Spells
{
    // 主要技能
    SPELL_FROZEN_PRISON                           = 47854,    // 冻结监狱：初始将BOSS冻结的状态
    SPELL_TAIL_SWEEP                              = 50155,    // 尾扫：锥形AOE物理伤害
    SPELL_CRYSTAL_CHAINS                          = 50997,    // 水晶锁链：定身目标（普通模式）
    SPELL_ENRAGE                                  = 8599,     // 狂暴：提升攻击速度
    SPELL_CRYSTALFIRE_BREATH                      = 48096,    // 水晶吐息：锥形AOE火焰伤害
    SPELL_CRYSTALLIZE                             = 48179,    // 结晶化：全体AOE定身（英雄模式）

    // 极度寒冷机制
    SPELL_INTENSE_COLD                            = 48094,    // 极度寒冷：周期性伤害光环
    SPELL_INTENSE_COLD_TRIGGERED                  = 48095     // 极度寒冷触发：叠加Debuff
};

/**
 * @enum Events
 * @brief 事件调度器使用的定时事件ID枚举
 */
enum Events
{
    EVENT_CRYSTAL_FIRE_BREATH                     = 1,        // 水晶吐息事件
    EVENT_CRYSTAL_CHAINS_CRYSTALLIZE,                         // 水晶锁链/结晶化事件
    EVENT_TAIL_SWEEP                                          // 尾扫事件
};

/**
 * @enum Yells
 * @brief BOSS的台词文本ID枚举
 */
enum Yells
{
    // Yell - 喊话
    SAY_AGGRO                                     = 0,        // 开怪台词
    SAY_SLAY                                      = 1,        // 击杀玩家台词
    SAY_ENRAGE                                    = 2,        // 狂暴台词
    SAY_DEATH                                     = 3,        // 死亡台词
    SAY_CRYSTAL_NOVA                              = 4,        // 水晶新星台词（施放技能时）
    SAY_FRENZY                                    = 5         // 狂怒台词
};

/**
 * @enum Misc
 * @brief 其他常量定义
 */
enum Misc
{
    DATA_INTENSE_COLD                             = 1,        // 极度寒冷数据ID，用于成就判定
    DATA_CONTAINMENT_SPHERES                      = 3         // 束缚之球数量
};

/**
 * @struct boss_keristrasza
 * @brief 克莉斯塔萨的AI结构体
 *
 * 职责：
 * - 管理克莉斯塔萨的战斗逻辑
 * - 处理冻结监狱的状态转换
 * - 追踪极度寒冷Debuff用于成就判定
 * - 管理战斗技能的施放
 *
 * 继承自：BossAI（提供基础BOSS AI功能）
 */
struct boss_keristrasza : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_keristrasza(Creature* creature) : BossAI(creature, DATA_KERISTRASZA)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 功能：
     * - 重置狂暴状态为false
     * - 重置极度寒冷标志为true
     */
    void Initialize()
    {
        _enrage = false;
        _intenseCold = true;
    }

    /**
     * @brief 重置BOSS状态
     *
     * 调用时机：
     * - BOSS脱战时
     * - 团队重置副本时
     * - BOSS被击杀后重生时
     *
     * 功能：
     * - 重置所有状态变量
     * - 清空极度寒冷追踪列表
     * - 检查束缚之球状态，决定是否解除冻结监狱
     * - 调用父类的Reset方法
     */
    void Reset() override
    {
        Initialize();
        _intenseColdList.clear();

        // 检查所有束缚之球是否已激活
        RemovePrison(CheckContainmentSpheres());
        _Reset();
    }

    /**
     * @brief 进入战斗时的处理函数
     * @param who 触发战斗的单位
     *
     * 调用时机：
     * - BOSS被玩家攻击或主动攻击玩家时
     *
     * 功能：
     * - 播放开怪台词
     * - 对所有玩家施放极度寒冷光环
     * - 调用父类的进入战斗方法
     * - 安排所有技能的事件调度
     */
    void JustEngagedWith(Unit* who) override
    {
        Talk(SAY_AGGRO);
        DoCastAOE(SPELL_INTENSE_COLD);          // 施放极度寒冷光环
        BossAI::JustEngagedWith(who);

        events.ScheduleEvent(EVENT_CRYSTAL_FIRE_BREATH, 14s);                           // 14秒后水晶吐息
        events.ScheduleEvent(EVENT_CRYSTAL_CHAINS_CRYSTALLIZE, DUNGEON_MODE(30s, 11s)); // 普通30秒/英雄11秒后水晶锁链/结晶化
        events.ScheduleEvent(EVENT_TAIL_SWEEP, 5s);                                      // 5秒后尾扫
    }

    /**
     * @brief 死亡时的处理函数
     * @param killer 击杀者
     *
     * 调用时机：
     * - BOSS被击杀时
     *
     * 功能：
     * - 播放死亡台词
     * - 通知实例BOSS已死亡
     */
    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_DEATH);
        _JustDied();
    }

    /**
     * @brief 击杀单位时的处理函数
     * @param who 被击杀的单位
     *
     * 调用时机：
     * - BOSS击杀任何单位时
     *
     * 功能：
     * - 如果击杀的是玩家，播放击杀台词
     */
    void KilledUnit(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_SLAY);
    }

    /**
     * @brief 检查束缚之球状态
     * @param remove_prison 是否在检查通过后移除监狱
     * @return 所有束缚之球是否都已激活
     *
     * 调用时机：
     * - Reset时检查是否应该解除冻结
     * - 束缚之球被激活时再次检查
     *
     * 功能：
     * - 从实例获取三个束缚之球的GUID
     * - 检查每个束缚之球是否处于激活状态
     * - 如果全部激活且remove_prison为true，解除冻结监狱
     *
     * @note 束缚之球对应三个前置BOSS：
     * - 大魔导师泰蕾斯塔（Magus Telestra）
     * - 异常者（Anomalus）
     * - 奥摩洛克（Ormorok）
     */
    bool CheckContainmentSpheres(bool remove_prison = false)
    {
        // 获取三个束缚之球的GUID
        ContainmentSphereGUIDs[0] = instance->GetGuidData(ANOMALUS_CONTAINMENT_SPHERE);
        ContainmentSphereGUIDs[1] = instance->GetGuidData(ORMOROKS_CONTAINMENT_SPHERE);
        ContainmentSphereGUIDs[2] = instance->GetGuidData(TELESTRAS_CONTAINMENT_SPHERE);

        // 检查每个束缚之球
        for (uint8 i = 0; i < DATA_CONTAINMENT_SPHERES; ++i)
        {
            GameObject* ContainmentSphere = ObjectAccessor::GetGameObject(*me, ContainmentSphereGUIDs[i]);
            if (!ContainmentSphere)
                return false;
            // 检查是否处于激活状态
            if (ContainmentSphere->GetGoState() != GO_STATE_ACTIVE)
                return false;
        }
        // 所有束缚之球都已激活
        if (remove_prison)
            RemovePrison(true);
        return true;
    }

    /**
     * @brief 移除或添加冻结监狱
     * @param remove true为移除监狱，false为添加监狱
     *
     * 功能：
     * - 移除时：取消免疫状态，移除冻结监狱光环
     * - 添加时：设置免疫状态，施放冻结监狱光环
     */
    void RemovePrison(bool remove)
    {
        if (remove)
        {
            // 解除冻结：取消免疫，移除监狱光环
            me->SetImmuneToAll(false);
            if (me->HasAura(SPELL_FROZEN_PRISON))
                me->RemoveAurasDueToSpell(SPELL_FROZEN_PRISON);
        }
        else
        {
            // 冻结：设置免疫，施放监狱光环
            me->SetImmuneToAll(true);
            DoCast(me, SPELL_FROZEN_PRISON, false);
        }
    }

    /**
     * @brief 设置GUID数据
     * @param guid 要设置的GUID
     * @param id 数据类型ID
     *
     * 调用时机：
     * - 由极度寒冷光环脚本调用，当玩家叠加超过1层Debuff时
     *
     * 功能：
     * - 将触发极度寒冷Debuff叠加的玩家GUID添加到追踪列表
     * - 用于成就判定：列表中的玩家无法获得成就
     */
    void SetGUID(ObjectGuid const& guid, int32 id) override
    {
        if (id == DATA_INTENSE_COLD)
            _intenseColdList.push_back(guid);
    }

    /**
     * @brief 处理伤害接收事件
     * @param attacker 攻击者
     * @param damage 伤害值
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 调用时机：
     * - BOSS受到伤害时
     *
     * 功能：
     * - 检测生命值是否低于25%且未狂暴
     * - 触发狂暴状态
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 生命值低于25%时触发狂暴
        if (!_enrage && me->HealthBelowPctDamaged(25, damage))
        {
            Talk(SAY_ENRAGE);
            Talk(SAY_FRENZY);
            DoCast(me, SPELL_ENRAGE);
            _enrage = true;
        }
    }

    /**
     * @brief 主更新函数，每帧调用一次
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 调用时机：
     * - 每个服务器tick（通常为每秒多次）
     *
     * 功能：
     * - 更新事件调度器
     * - 执行到期的事件
     * - 处理所有技能的施放
     * - 在非施法状态下执行近战攻击
     *
     * 性能注意事项：
     * - 此函数高频调用，应避免复杂计算
     * - 施法状态下跳过事件处理，减少CPU占用
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果没有战斗目标，不执行后续逻辑
        if (!UpdateVictim())
            return;

        events.Update(diff);

        // 如果正在施法，暂停事件处理，避免打断施法
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 执行所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CRYSTAL_FIRE_BREATH:
                    // 对当前目标施放水晶吐息（锥形AOE火焰伤害）
                    DoCastVictim(SPELL_CRYSTALFIRE_BREATH);
                    events.ScheduleEvent(EVENT_CRYSTAL_FIRE_BREATH, 14s);
                    break;
                case EVENT_CRYSTAL_CHAINS_CRYSTALLIZE:
                    // 施放尾扫（这个事件名称有误，实际是尾扫技能）
                    DoCast(me, SPELL_TAIL_SWEEP);
                    events.ScheduleEvent(EVENT_CRYSTAL_CHAINS_CRYSTALLIZE, 5s);
                    break;
                case EVENT_TAIL_SWEEP:
                    // 水晶锁链/结晶化技能
                    Talk(SAY_CRYSTAL_NOVA);
                    if (IsHeroic())
                    {
                        // 英雄模式：施放结晶化（全体AOE定身）
                        DoCast(me, SPELL_CRYSTALLIZE);
                    }
                    else if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100.0f, true))
                    {
                        // 普通模式：对随机目标施放水晶锁链（单体定身）
                        DoCast(target, SPELL_CRYSTAL_CHAINS);
                    }
                    events.ScheduleEvent(EVENT_TAIL_SWEEP, DUNGEON_MODE(30s, 11s));
                    break;
                default:
                    break;
            }

            // 如果施法后进入施法状态，暂停后续事件处理
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    bool _intenseCold;                                                      // 极度寒冷标志（未使用）
    bool _enrage;                                                           // 是否已狂暴
    ObjectGuid ContainmentSphereGUIDs[DATA_CONTAINMENT_SPHERES];            // 束缚之球GUID数组
public:
    GuidList _intenseColdList;                                              // 触发极度寒冷Debuff叠加的玩家列表，用于成就判定
};

/**
 * @struct containment_sphere
 * @brief 束缚之球的AI结构体
 *
 * 职责：
 * - 处理玩家与束缚之球的交互
 * - 激活时通知克莉斯塔萨检查所有束缚之球状态
 *
 * 继承自：GameObjectAI
 *
 * 机制说明：
 * - 每个前置BOSS死后会激活对应的束缚之球
 * - 玩家需要激活所有三个束缚之球才能解除克莉斯塔萨的冻结监狱
 */
struct containment_sphere : public GameObjectAI
{
    /**
     * @brief 构造函数
     * @param go 游戏对象指针
     */
    containment_sphere(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

    InstanceScript* instance;        // 实例脚本指针

    /**
     * @brief 玩家交互处理函数
     * @param player 交互的玩家
     * @return 是否成功处理
     *
     * 调用时机：
     * - 玩家点击/交互束缚之球时
     *
     * 功能：
     * - 获取克莉斯塔萨的引用
     * - 设置游戏对象为不可选择和激活状态
     * - 通知克莉斯塔萨检查所有束缚之球是否都已激活
     */
    bool OnGossipHello(Player* /*player*/) override
    {
        Creature* keristrasza = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_KERISTRASZA));
        if (keristrasza && keristrasza->IsAlive())
        {
            // 设置游戏对象状态
            me->SetFlag(GO_FLAG_NOT_SELECTABLE);        // 设为不可选择
            me->SetGoState(GO_STATE_ACTIVE);            // 设为激活状态

            // 通知克莉斯塔萨检查束缚之球状态
            ENSURE_AI(boss_keristrasza, keristrasza->AI())->CheckContainmentSpheres(true);
        }
        return true;
    }
};

/**
 * @class spell_intense_cold
 * @brief 极度寒冷光环脚本（Spell ID: 48095）
 *
 * 职责：
 * - 处理极度寒冷Debuff的叠加检测
 * - 当叠加层数达到2层或以上时，通知BOSS记录该玩家
 * - 用于成就"极度寒冷"的判定
 *
 * 继承自：AuraScript
 */
// 48095 - Intense Cold
class spell_intense_cold : public AuraScript
{
    PrepareAuraScript(spell_intense_cold);

    /**
     * @brief 处理周期性触发
     * @param aurEff 光环效果
     *
     * 调用时机：
     * - 每次周期性伤害触发时
     *
     * 功能：
     * - 检查Debuff叠加层数
     * - 如果层数 >= 2，通知BOSS记录该玩家
     * - 记录的玩家将无法获得成就
     */
    void HandlePeriodicTick(AuraEffect const* aurEff)
    {
        // 如果叠加层数小于2，不记录
        if (aurEff->GetBase()->GetStackAmount() < 2)
            return;
        Unit* caster = GetCaster();
        /// @todo the caster should be boss but not the player
        // 注意：这里的caster应该是BOSS而不是玩家
        if (!caster || !caster->GetAI())
            return;
        // 通知BOSS记录触发叠加的玩家
        caster->GetAI()->SetGUID(GetTarget()->GetGUID(), DATA_INTENSE_COLD);
    }

    /**
     * @brief 注册光环效果钩子
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_intense_cold::HandlePeriodicTick, EFFECT_1, SPELL_AURA_PERIODIC_DAMAGE);
    }
};

/**
 * @class achievement_intense_cold
 * @brief 成就"极度寒冷"判定脚本
 *
 * 职责：
 * - 检查玩家是否满足成就条件
 * - 条件：在整个战斗中不叠加超过1层极度寒冷Debuff
 *
 * 继承自：AchievementCriteriaScript
 */
class achievement_intense_cold : public AchievementCriteriaScript
{
    public:
        /**
         * @brief 构造函数
         */
        achievement_intense_cold() : AchievementCriteriaScript("achievement_intense_cold")
        {
        }

        /**
         * @brief 检查成就条件
         * @param player 玩家
         * @param target 目标（克莉斯塔萨）
         * @return 是否满足成就条件
         *
         * 调用时机：
         * - BOSS被击杀时，成就系统会检查每个玩家的成就条件
         *
         * 功能：
         * - 检查玩家是否在极度寒冷追踪列表中
         * - 如果不在列表中（未叠加超过1层Debuff），返回true，获得成就
         * - 如果在列表中（叠加了2层或以上Debuff），返回false，不获得成就
         */
        bool OnCheck(Player* player, Unit* target) override
        {
            if (!target)
                return false;

            // 获取BOSS的极度寒冷追踪列表
            GuidList _intenseColdList = ENSURE_AI(boss_keristrasza, target->ToCreature()->AI())->_intenseColdList;
            // 检查玩家是否在列表中
            if (!_intenseColdList.empty())
                for (GuidList::iterator itr = _intenseColdList.begin(); itr != _intenseColdList.end(); ++itr)
                    if (player->GetGUID() == *itr)
                        return false;   // 玩家在追踪列表中，不满足成就条件

            return true;    // 玩家不在追踪列表中，满足成就条件
        }
};

/**
 * @brief 注册所有AI和法术脚本
 *
 * 调用时机：
 * - 服务器启动时，脚本加载系统会调用此函数
 *
 * 功能：
 * - 注册克莉斯塔萨BOSS AI
 * - 注册束缚之球游戏对象AI
 * - 注册极度寒冷光环脚本
 * - 注册极度寒冷成就判定脚本
 */
void AddSC_boss_keristrasza()
{
    RegisterNexusCreatureAI(boss_keristrasza);
    RegisterNexusGameObjectAI(containment_sphere);
    RegisterSpellScript(spell_intense_cold);
    new achievement_intense_cold();
}
