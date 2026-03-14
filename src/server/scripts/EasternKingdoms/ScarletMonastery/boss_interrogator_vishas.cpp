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
 * @file boss_interrogator_vishas.cpp
 * @brief 血色修道院审讯官维希斯BOSS脚本
 *
 * 本模块实现了血色修道院（图书馆）的第一个BOSS - 审讯官维希斯的战斗AI：
 * - 基础战斗技能（暗言术：痛）
 * - 血量阶段喊话机制（60%和30%血量触发）
 * - 死亡后触发沃雷尔事件的剧情
 *
 * BOSS背景：
 * 审讯官维希斯是血色修道院图书馆区域的BOSS，负责审讯和折磨囚犯。
 * 击杀他后会触发沃雷尔（Vorrel）的灵魂出现并发表感谢台词。
 *
 * 战斗机制：
 * 1. 开怪时喊话并施放暗言术：痛
 * 2. 定期对当前目标施放暗言术：痛（5-15秒间隔）
 * 3. 血量降至60%和30%时喊话警告
 * 4. 死亡时触发沃雷尔灵魂出现
 */

#include "scarlet_monastery.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"

/**
 * @brief 审讯官维希斯对话文本ID枚举
 *
 * 定义审讯官维希斯在战斗中的各种对话文本ID
 */
enum InterrogatorVishasSays
{
    SAY_AGGRO = 0,          // 进入战斗时的喊话
    SAY_HEALTH1 = 1,        // 血量低于60%时的喊话
    SAY_HEALTH2 = 2,        // 血量低于30%时的喊话
    SAY_KILL = 3,           // 击杀玩家时的喊话
    SAY_TRIGGER_VORREL = 0  // 触发沃雷尔灵魂的对话ID
};

/**
 * @brief 法术ID枚举
 *
 * 定义审讯官维希斯使用的法术ID
 */
enum InterrogatorVishasSpells
{
    SPELL_SHADOW_WORD_PAIN = 2767  // 暗言术：痛 - 持续伤害法术
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能施放计划
 */
enum InterrogatorVishasEvents
{
    EVENT_SHADOW_WORD_PAIN = 1  // 暗言术：痛施放事件
};

/**
 * @brief 审讯官维希斯AI结构体
 *
 * 实现审讯官维希斯的战斗AI逻辑，包括：
 * - 暗言术：痛的定期施放
 * - 血量阶段喊话机制
 * - 死亡后触发沃雷尔灵魂出现
 *
 * 战斗流程：
 * 1. 进入战斗后喊话，5秒后施放第一个暗言术：痛
 * 2. 每5-15秒对当前目标施放暗言术：痛
 * 3. 血量降至60%时喊出第一次警告
 * 4. 血量降至30%时喊出第二次警告
 * 5. 死亡时触发沃雷尔灵魂出现并发表感谢台词
 */
struct boss_interrogator_vishas : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化BOSS AI，设置数据类型为DATA_INTERROGATOR_VISHAS
     */
    boss_interrogator_vishas(Creature* creature) : BossAI(creature, DATA_INTERROGATOR_VISHAS)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 将喊话计数器重置为0，用于血量阶段喊话
     */
    void Initialize()
    {
        _yellCount = 0;  // 喊话计数器，记录已经喊话的次数（最多2次）
    }

    /**
     * @brief 重置AI状态
     *
     * 当战斗重置时调用，恢复审讯官维希斯到初始状态：
     * - 重置喊话计数器
     * - 调用基类的Reset方法清理战斗数据
     *
     * 调用时机：战斗重置、BOSS脱离战斗、副本重置
     */
    void Reset() override
    {
        Initialize();
        _Reset();
    }

    /**
     * @brief 进入战斗事件
     * @param who 进入战斗的目标
     *
     * 当审讯官维希斯进入战斗时：
     * - 喊出战斗台词
     * - 调用基类的JustEngagedWith方法初始化战斗
     * - 安排5秒后施放第一个暗言术：痛
     *
     * 调用时机：BOSS被玩家攻击或主动攻击玩家
     */
    void JustEngagedWith(Unit* who) override
    {
        Talk(SAY_AGGRO);  // 喊出战斗台词
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_SHADOW_WORD_PAIN, 5s);  // 5秒后施放暗言术：痛
    }

    /**
     * @brief 击杀单位事件
     * @param victim 被击杀的单位
     *
     * 当审讯官维希斯击杀玩家时喊话
     *
     * 调用时机：BOSS击杀玩家单位时
     */
    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)  // 只对玩家喊话
            Talk(SAY_KILL);
    }

    /**
     * @brief 死亡事件
     * @param killer 击杀者
     *
     * 当审讯官维希斯死亡时：
     * - 调用基类的JustDied方法处理战利品和副本状态
     * - 触发沃雷尔灵魂出现并发表感谢台词
     *
     * 剧情背景：
     * 沃雷尔是被维希斯折磨致死的囚犯之一，维希斯死后他的灵魂出现感谢玩家
     *
     * 调用时机：BOSS被击杀时
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        // 触发沃雷尔灵魂出现并喊话
        if (Creature* vorrel = instance->GetCreature(DATA_VORREL))
        {
            if (vorrel->AI())
                vorrel->AI()->Talk(SAY_TRIGGER_VORREL);
        }
    }

    /**
     * @brief 受到伤害事件
     * @param attacker 造成伤害的来源
     * @param damage 伤害值（可修改）
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 核心机制：处理血量阶段喊话逻辑
     * - 血量低于60%时喊出第一次警告
     * - 血量低于30%时喊出第二次警告
     * - 每个阶段只会喊话一次
     *
     * 调用时机：BOSS每次受到伤害时
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 血量低于60%且还未喊过第一次话
        if (me->HealthBelowPctDamaged(60, damage) && _yellCount < 1)
        {
            Talk(SAY_HEALTH1);
            ++_yellCount;  // 增加喊话计数，防止重复喊话
        }

        // 血量低于30%且还未喊过第二次话
        if (me->HealthBelowPctDamaged(30, damage) && _yellCount < 2)
        {
            Talk(SAY_HEALTH2);
            ++_yellCount;  // 增加喊话计数，防止重复喊话
        }
    }

    /**
     * @brief 执行事件处理器
     * @param eventId 事件ID
     *
     * 处理定时触发的技能施放事件：
     * - EVENT_SHADOW_WORD_PAIN: 对当前目标施放暗言术：痛，5-15秒后再次施放
     *
     * 调用时机：事件计时器到期时自动调用
     */
    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_SHADOW_WORD_PAIN:
                DoCastVictim(SPELL_SHADOW_WORD_PAIN);           // 对当前目标施放暗言术：痛
                events.ScheduleEvent(EVENT_SHADOW_WORD_PAIN, 5s, 15s);  // 5-15秒后再次施放
                break;
            default:
                break;
        }
    }

private:
    uint8 _yellCount;  // 喊话计数器，记录已经喊话的次数（最多2次，60%和30%各一次）
};

/**
 * @brief 注册BOSS脚本
 *
 * 将审讯官维希斯的AI注册到脚本系统中
 */
void AddSC_boss_interrogator_vishas()
{
    RegisterScarletMonasteryCreatureAI(boss_interrogator_vishas);
}
