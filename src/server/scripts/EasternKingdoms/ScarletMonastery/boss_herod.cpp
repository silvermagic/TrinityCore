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
 * @file boss_herod.cpp
 * @brief 血色修道院BOSS赫罗德战斗脚本
 *
 * 本模块实现了血色修道院（军械库）最终BOSS赫罗德的战斗AI：
 * - 赫罗德（血色十字军勇士）
 * - 小怪：血色新兵（死亡后召唤）
 *
 * 战斗机制：
 * 1. 进入战斗时使用冲锋技能
 * 2. 定期使用顺劈斩和旋风斩
 * 3. 血量低于30%时进入狂暴状态
 * 4. 死亡后召唤20个血色新兵
 *
 * 特殊事件：
 * - 玩家击杀赫罗德后会触发大量新兵刷新，是副本的经典事件
 */

#include "scarlet_monastery.h"
#include "ScriptedCreature.h"
#include "ScriptedEscortAI.h"
#include "ScriptMgr.h"

/**
 * @brief 赫罗德对话文本ID枚举
 *
 * 定义赫罗德在战斗中的各种对话文本ID
 */
enum HerodSays
{
    SAY_AGGRO = 0,      // 进入战斗时的喊话
    SAY_WHIRLWIND = 1,  // 施放旋风斩时的喊话
    SAY_ENRAGE = 2,     // 进入狂暴状态时的喊话
    SAY_KILL = 3,       // 击杀玩家时的喊话
    EMOTE_ENRAGE = 4    // 狂暴表情
};

/**
 * @brief 赫罗德法术ID枚举
 *
 * 定义赫罗德使用的所有法术ID
 */
enum HerodSpells
{
    SPELL_RUSHINGCHARGE = 8260,  // 冲锋 - 进入战斗时施放
    SPELL_CLEAVE = 15496,        // 顺劈斩 - 主要攻击技能
    SPELL_WHIRLWIND = 8989,      // 旋风斩 - AOE攻击技能
    SPELL_FRENZY = 8269          // 狂暴 - 血量低于30%时施放
};

/**
 * @brief 赫罗德相关NPC枚举
 *
 * 定义战斗中涉及的NPC ID
 */
enum HerodNpcs
{
    NPC_SCARLET_TRAINEE = 6575,  // 血色新兵 - 赫罗德死亡后召唤
    NPC_SCARLET_MYRMIDON = 4295  // 血色士兵 - 副本小怪（未使用）
};

/**
 * @brief 赫罗德事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum HerodEvents
{
    EVENT_CLEAVE = 1,     // 顺劈斩事件
    EVENT_WHIRLWIND       // 旋风斩事件
};

/**
 * @brief 血色新兵刷新位置
 *
 * 定义赫罗德死亡后血色新兵的刷新中心位置
 */
Position const ScarletTraineePos = { 1939.18f, -431.58f, 17.09f, 6.22f };

/**
 * @brief 赫罗德BOSS AI结构体
 *
 * 实现赫罗德的战斗AI逻辑，包括：
 * - 冲锋、顺劈斩、旋风斩等战斗技能
 * - 狂暴机制（血量低于30%）
 * - 死亡后召唤血色新兵
 *
 * 战斗流程：
 * 1. 进入战斗时施放冲锋并喊话
 * 2. 定期使用顺劈斩（12秒冷却）和旋风斩（30秒冷却）
 * 3. 血量低于30%时进入狂暴状态
 * 4. 死亡后召唤20个血色新兵
 */
struct boss_herod : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_herod(Creature* creature) : BossAI(creature, DATA_HEROD)
    {
        _enrage = false;
    }

    /**
     * @brief 重置AI状态
     *
     * 当战斗重置时调用，将狂暴状态重置为false
     */
    void Reset() override
    {
        _enrage = false;
        _Reset();
    }

    /**
     * @brief 进入战斗事件
     * @param who 进入战斗的目标
     *
     * 当赫罗德进入战斗时：
     * - 喊出战斗台词
     * - 对自己施放冲锋法术
     * - 安排技能施放事件
     */
    void JustEngagedWith(Unit* who) override
    {
        Talk(SAY_AGGRO);                // 喊出战斗台词
        DoCast(me, SPELL_RUSHINGCHARGE); // 对自己施放冲锋
        BossAI::JustEngagedWith(who);

        events.ScheduleEvent(EVENT_CLEAVE, 12s);     // 12秒后施放顺劈斩
        events.ScheduleEvent(EVENT_WHIRLWIND, 1min); // 1分钟后施放旋风斩
    }

    /**
     * @brief 击杀单位事件
     * @param victim 被击杀的单位
     *
     * 当赫罗德击杀玩家时喊话
     */
    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_KILL);
    }

    /**
     * @brief 死亡事件
     * @param killer 击杀者
     *
     * 当赫罗德死亡时，在指定位置召唤20个血色新兵
     * 这是副本的经典事件，玩家需要在大量新兵中存活
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();

        // 召唤20个血色新兵
        for (uint8 itr = 0; itr < 20; ++itr)
        {
            // 在刷新位置附近随机位置生成新兵
            Position randomNearPosition = me->GetRandomPoint(ScarletTraineePos, 5.f);
            randomNearPosition.SetOrientation(ScarletTraineePos.GetOrientation());
            me->SummonCreature(NPC_SCARLET_TRAINEE, randomNearPosition, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 10min);
        }
    }

    /**
     * @brief 受到伤害事件
     * @param attacker 攻击者
     * @param damage 伤害值
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 核心机制：处理赫罗德的狂暴逻辑
     * - 当血量低于30%且未处于狂暴状态时，触发狂暴
     * - 喊出狂暴表情和台词
     * - 施放狂暴法术增加伤害
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (!_enrage && me->HealthBelowPctDamaged(30, damage))
        {
            Talk(EMOTE_ENRAGE);     // 狂暴表情
            Talk(SAY_ENRAGE);       // 狂暴台词
            DoCastSelf(SPELL_FRENZY); // 施放狂暴法术
            _enrage = true;
        }
    }

    /**
     * @brief 执行事件处理器
     * @param eventId 事件ID
     *
     * 处理定时触发的技能施放事件
     */
    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_CLEAVE:
                DoCastVictim(SPELL_CLEAVE);  // 对当前目标施放顺劈斩
                events.Repeat(12s);           // 12秒后再次施放
                break;
            case EVENT_WHIRLWIND:
                Talk(SAY_WHIRLWIND);          // 施放旋风斩时的喊话
                DoCastVictim(SPELL_WHIRLWIND); // 对当前目标施放旋风斩
                events.Repeat(30s);            // 30秒后再次施放
                break;
            default:
                break;
        }
    }

private:
    bool _enrage;  // 是否已进入狂暴状态
};

/**
 * @brief 血色新兵AI结构体
 *
 * 实现血色新兵的AI逻辑，这是一个护卫AI（EscortAI）：
 * - 赫罗德死亡后刷新
 * - 延迟1-6秒后开始沿固定路径巡逻
 *
 * 战斗流程：
 * 1. 刷新后等待随机时间（1-6秒）
 * 2. 开始沿路径巡逻
 * 3. 遇到玩家时进入战斗
 */
struct npc_scarlet_trainee : public EscortAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_scarlet_trainee(Creature* creature) : EscortAI(creature)
    {
        _startTimer = urand(1000, 6000);  // 随机1-6秒的启动延迟
    }

    /**
     * @brief 更新AI
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 每帧调用，处理启动延迟和护卫AI逻辑
     */
    void UpdateAI(uint32 diff) override
    {
        // 处理启动延迟
        if (_startTimer)
        {
            if (_startTimer <= diff)
            {
                Start(true, true);  // 开始护卫路径，立即执行，没有玩家触发
                _startTimer = 0;
            }
            else
                _startTimer -= diff;
        }

        EscortAI::UpdateAI(diff);  // 更新护卫AI基类
    }

private:
    uint32 _startTimer;  // 启动延迟计时器
};

/**
 * @brief 注册BOSS脚本
 *
 * 将赫罗德和血色新兵的AI注册到脚本系统中
 */
void AddSC_boss_herod()
{
    RegisterScarletMonasteryCreatureAI(boss_herod);
    RegisterScarletMonasteryCreatureAI(npc_scarlet_trainee);
}
