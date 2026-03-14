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
 * @file boss_venoxis.cpp
 * @brief 祖尔格拉布副本Boss - 高阶祭司维诺西斯(High Priest Venoxis)的AI实现
 *
 * 高阶祭司维诺西斯是祖尔格拉布的首领之一，具有独特的两阶段战斗机制：
 *
 * 第一阶段（巨魔形态）：
 * - 使用神圣系法术进行攻击
 * - 拥有治疗和驱散能力
 * - 主要技能：神圣新星、神圣之火、神圣愤怒、驱散魔法、恢复
 *
 * 第二阶段（毒蛇形态）：
 * - 当生命值降至50%时自动变形
 * - 获得毒系技能，召唤寄生虫
 * - 主要技能：毒云、毒液喷射、召唤寄生蛇
 * - 在20%生命值时进入狂暴状态
 *
 * 特殊机制：
 * - 神圣新星需要至少3个近战目标才会施放
 * - 毒云和毒液喷射会造成持续伤害
 * - 召唤的寄生蛇会对玩家造成额外威胁
 *
 * @todo 需要进一步研究技能冷却时间以优化计时器
 */

#include "zulgurub.h"
#include "ObjectMgr.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "Spell.h"

/**
 * @brief 对话和喊话ID枚举
 */
enum Says
{
    SAY_VENOXIS_TRANSFORM           = 1,  ///< 变身时的喊话："Let the coils of hate unfurl!"（让仇恨的盘结展开吧！）
    SAY_VENOXIS_DEATH               = 2   ///< 死亡时的喊话："Ssserenity.. at lassst!"（安息...终于！）
};

/**
 * @brief 法术ID枚举
 *
 * 包含维诺西斯在两个阶段使用的所有法术
 */
enum Spells
{
    // troll form - 巨魔形态法术
    SPELL_THRASH                    = 3391,   ///< 痛击 - 额外攻击（两阶段都可用）
    SPELL_DISPEL_MAGIC              = 23859,  ///< 驱散魔法 - 驱散友方身上的有害魔法
    SPELL_RENEW                     = 23895,  ///< 恢复 - 持续治疗
    SPELL_HOLY_NOVA                 = 23858,  ///< 神圣新星 - 对周围敌人造成神圣伤害
    SPELL_HOLY_FIRE                 = 23860,  ///< 神圣之火 - 对目标造成神圣伤害
    SPELL_HOLY_WRATH                = 23979,  ///< 神圣愤怒 - 对目标造成神圣伤害

    // snake form - 毒蛇形态法术
    SPELL_POISON_CLOUD              = 23861,  ///< 毒云 - 创建毒云区域
    SPELL_VENOM_SPIT                = 23862,  ///< 毒液喷射 - 对目标喷射毒液
    SPELL_PARASITIC_SERPENT         = 23865,  ///< 寄生蛇 - 寄生效果
    SPELL_SUMMON_PARASITIC_SERPENT  = 23866,  ///< 召唤寄生蛇 - 召唤小蛇攻击玩家
    SPELL_PARASITIC_SERPENT_TRIGGER = 23867,  ///< 寄生蛇触发 - 触发效果

    // used when swapping event-stages - 阶段转换法术
    SPELL_VENOXIS_TRANSFORM         = 23849,  ///< 变形 - 在50%生命值时变形为眼镜蛇
    SPELL_FRENZY                    = 8269    ///< 狂暴 - 在20%生命值时狂暴
};

/**
 * @brief 事件ID枚举
 *
 * 用于事件调度系统，控制Boss的技能施放时序
 */
enum Events
{
    // troll form - 巨魔形态事件
    EVENT_THRASH                    = 1,   ///< 痛击事件
    EVENT_DISPEL_MAGIC              = 2,   ///< 驱散魔法事件
    EVENT_RENEW                     = 3,   ///< 恢复事件
    EVENT_HOLY_NOVA                 = 4,   ///< 神圣新星事件
    EVENT_HOLY_FIRE                 = 5,   ///< 神圣之火事件
    EVENT_HOLY_WRATH                = 6,   ///< 神圣愤怒事件

    // phase-changing - 阶段转换事件
    EVENT_TRANSFORM                 = 7,   ///< 变形事件

    // snake form events - 毒蛇形态事件
    EVENT_POISON_CLOUD              = 8,   ///< 毒云事件
    EVENT_VENOM_SPIT                = 9,   ///< 毒液喷射事件
    EVENT_PARASITIC_SERPENT         = 10,  ///< 寄生蛇事件
    EVENT_FRENZY                    = 11,  ///< 狂暴事件
};

/**
 * @brief 战斗阶段枚举
 */
enum Phases
{
    PHASE_ONE                       = 1,  ///< 第一阶段 - 巨魔形态
    PHASE_TWO                       = 2   ///< 第二阶段 - 毒蛇形态
};

/**
 * @brief NPC ID枚举
 */
enum NPCs
{
    NPC_PARASITIC_SERPENT           = 14884  ///< 寄生蛇的NPC ID
};

/**
 * @brief 高阶祭司维诺西斯Boss AI结构体
 *
 * 实现维诺西斯的核心战斗逻辑，包括：
 * - 两阶段战斗（巨魔形态和毒蛇形态）
 * - 智能的神圣新星施放机制（需要足够多的近战目标）
 * - 自动变形和狂暴触发
 */
struct boss_venoxis : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_venoxis(Creature* creature) : BossAI(creature, DATA_VENOXIS)
    {
        Initialize();
    }

    /**
     * @brief 初始化所有成员变量
     *
     * 设置所有状态标志的初始值，确保每次战斗开始时状态一致
     */
    void Initialize()
    {
        _inMeleeRange = 0;      ///< 近战范围内的目标计数
        _transformed = false;   ///< 是否已变形为毒蛇形态
        _frenzied = false;      ///< 是否已进入狂暴状态
    }

    /**
     * @brief 重置Boss状态
     *
     * 清除所有战斗状态、光环和标志，恢复到初始待战斗状态
     */
    void Reset() override
    {
        _Reset();
        // 移除之前战斗的所有法术和光环
        me->RemoveAllAuras();
        me->SetReactState(REACT_PASSIVE);
        // 设置内部使用的变量为默认值
        Initialize();
        events.SetPhase(PHASE_ONE);
    }

    /**
     * @brief Boss死亡处理
     * @param killer 击杀者（未使用）
     *
     * 当Boss被击败时：
     * 1. 通知实例Boss已死亡
     * 2. 播放死亡台词
     * 3. 清除所有光环效果
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_VENOXIS_DEATH);
        me->RemoveAllAuras();
    }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标
     *
     * 当Boss被玩家攻击时，初始化第一阶段的事件调度
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        me->SetReactState(REACT_AGGRESSIVE);
        // 始终运行的事件（两阶段都可用）
        events.ScheduleEvent(EVENT_THRASH, 5s);
        // 第一阶段事件（巨魔形态）
        events.ScheduleEvent(EVENT_HOLY_NOVA, 5s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_DISPEL_MAGIC, 35s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_HOLY_FIRE, 10s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_RENEW, 30s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_HOLY_WRATH, 1min, 0, PHASE_ONE);

        events.SetPhase(PHASE_ONE);

        // 将整个区域设为战斗状态
        DoZoneInCombat();
    }

    /**
     * @brief 受到伤害处理
     * @param attacker 攻击者（未使用）
     * @param damage 伤害值（未使用）
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     *
     * 处理两个关键机制：
     * 1. 50%生命值：触发变形为毒蛇形态
     * 2. 20%生命值：触发狂暴状态
     */
    void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 检查维诺西斯是否准备变形（50%生命值）
        if (!_transformed && !HealthAbovePct(50))
        {
            _transformed = true;
            // 安排变形事件
            events.ScheduleEvent(EVENT_TRANSFORM, 100ms);
        }
        // 生命值过低，进入狂暴（20%生命值）
        else if (!_frenzied && !HealthAbovePct(20))
        {
            _frenzied = true;
            events.ScheduleEvent(EVENT_FRENZY, 100ms);
        }
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 处理所有事件的调度和执行，包括：
     * - 第一阶段的巨魔法术
     * - 阶段转换
     * - 第二阶段的毒蛇法术
     * - 特殊的神圣新星计数逻辑
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果没有战斗目标，直接返回
        if (!UpdateVictim())
            return;

        events.Update(diff);

        // 如果正在施法，等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                // 痛击 - 两阶段都可用
                case EVENT_THRASH:
                    DoCast(me, SPELL_THRASH, true);
                    events.ScheduleEvent(EVENT_THRASH, 10s, 20s);
                    break;

                // 巨魔形态法术和动作（第一阶段）
                case EVENT_DISPEL_MAGIC:
                    // 驱散自己身上的有害魔法
                    DoCast(me, SPELL_DISPEL_MAGIC);
                    events.ScheduleEvent(EVENT_DISPEL_MAGIC, 15s, 20s, 0, PHASE_ONE);
                    break;
                case EVENT_RENEW:
                    // 对自己施放恢复治疗
                    DoCast(me, SPELL_RENEW);
                    events.ScheduleEvent(EVENT_RENEW, 25s, 30s, 0, PHASE_ONE);
                    break;
                case EVENT_HOLY_NOVA:
                    // 神圣新星：需要检查近战范围内的目标数量
                    _inMeleeRange = 0;

                    // 检查前10个威胁列表中的目标
                    for (uint8 i = 0; i < 10; ++i)
                    {
                        if (Unit* target = SelectTarget(SelectTargetMethod::MaxThreat, i))
                            // 检查目标是否在近战距离内
                            if (me->IsWithinMeleeRange(target))
                                ++_inMeleeRange;
                    }

                    // 只有在近战范围内有3个或更多目标时才施放神圣新星
                    if (_inMeleeRange >= 3)
                        DoCastVictim(SPELL_HOLY_NOVA);

                    events.ScheduleEvent(EVENT_HOLY_NOVA, 45s, 75s, 0, PHASE_ONE);
                    break;
                case EVENT_HOLY_FIRE:
                    // 对随机目标施放神圣之火
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random))
                        DoCast(target, SPELL_HOLY_FIRE);
                    events.ScheduleEvent(EVENT_HOLY_FIRE, 45s, 60s, 0, PHASE_ONE);
                    break;
                case EVENT_HOLY_WRATH:
                    // 对随机目标施放神圣愤怒
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random))
                        DoCast(target, SPELL_HOLY_WRATH);
                    events.ScheduleEvent(EVENT_HOLY_WRATH, 45s, 60s, 0, PHASE_ONE);
                    break;

                //
                // 毒蛇形态法术和动作
                //

                case EVENT_VENOM_SPIT:
                    // 对随机目标喷射毒液
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random))
                        DoCast(target, SPELL_VENOM_SPIT);
                    events.ScheduleEvent(EVENT_VENOM_SPIT, 5s, 15s, 0, PHASE_TWO);
                    break;
                case EVENT_POISON_CLOUD:
                    // 在随机目标位置创建毒云
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random))
                        DoCast(target, SPELL_POISON_CLOUD);
                    events.ScheduleEvent(EVENT_POISON_CLOUD, 15s, 20s, 0, PHASE_TWO);
                    break;
                case EVENT_PARASITIC_SERPENT:
                    // 召唤寄生蛇攻击随机目标
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random))
                        DoCast(target, SPELL_SUMMON_PARASITIC_SERPENT);
                    events.ScheduleEvent(EVENT_PARASITIC_SERPENT, 15s, 0, PHASE_TWO);
                    break;
                case EVENT_FRENZY:
                    // 在20%生命值时狂暴
                    DoCast(me, SPELL_FRENZY, true);
                    break;

                //
                // 变形和阶段转换
                //

                case EVENT_TRANSFORM:
                    // 在50%生命值时变形
                    DoCast(me, SPELL_VENOXIS_TRANSFORM);
                    Talk(SAY_VENOXIS_TRANSFORM);
                    ResetThreatList();

                    // 第二阶段事件（毒蛇形态）
                    events.ScheduleEvent(EVENT_VENOM_SPIT, 5s, 0, PHASE_TWO);
                    events.ScheduleEvent(EVENT_POISON_CLOUD, 10s, 0, PHASE_TWO);
                    events.ScheduleEvent(EVENT_PARASITIC_SERPENT, 30s, 0, PHASE_TWO);

                    // 变形完成，开始第二阶段
                    events.SetPhase(PHASE_TWO);

                    break;
                default:
                    break;
            }

            // 如果施法后仍在施法状态，退出循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    uint8 _inMeleeRange;    ///< 近战范围内的目标数量计数器
    bool _transformed;      ///< 是否已变形为毒蛇形态
    bool _frenzied;         ///< 是否已进入狂暴状态
};

/**
 * @brief 注册维诺西斯Boss脚本
 *
 * 将维诺西斯的AI注册到脚本系统中，使其在游戏中可以被正确调用。
 */
void AddSC_boss_venoxis()
{
    RegisterZulGurubCreatureAI(boss_venoxis);
}
