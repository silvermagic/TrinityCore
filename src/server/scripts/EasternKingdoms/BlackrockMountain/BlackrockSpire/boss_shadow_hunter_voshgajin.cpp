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
 * @file boss_shadow_hunter_voshgajin.cpp
 * @brief 黑石塔 - 暗影猎手沃什加斯 BOSS 脚本
 *
 * 本模块实现了黑石塔下层副本中 BOSS 暗影猎手沃什加斯的 AI 行为。
 * 该 BOSS 位于黑石塔下层的军械库区域,是一名巨魔暗影猎手。
 *
 * 主要功能:
 * - 诅咒之血: 对目标施加诅咒,降低治疗效果
 * - 妖术: 随机将目标变成青蛙
 * - 顺劈斩: 对前方敌人造成物理伤害
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "ScriptedCreature.h"

/**
 * @brief 暗影猎手沃什加斯使用的法术 ID 枚举
 */
enum Spells
{
    SPELL_CURSEOFBLOOD              = 24673,    ///< 诅咒之血 - 降低目标受到的治疗效果
    SPELL_HEX                       = 16708,    ///< 妖术 - 将目标变成青蛙
    SPELL_CLEAVE                    = 20691,    ///< 顺劈斩 - 对前方敌人造成物理伤害
};

/**
 * @brief BOSS 战斗事件 ID 枚举
 */
enum Events
{
    EVENT_CURSE_OF_BLOOD            = 1,        ///< 诅咒之血事件
    EVENT_HEX                       = 2,        ///< 妖术事件
    EVENT_CLEAVE                    = 3,        ///< 顺劈斩事件
};

/**
 * @brief 暗影猎手沃什加斯 BOSS AI 结构体
 *
 * 继承自 BossAI,实现了暗影猎手沃什加斯的战斗逻辑。
 * 该 BOSS 是一名巨魔暗影猎手,会使用诅咒、妖术和近战攻击。
 */
struct boss_shadow_hunter_voshgajin : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_shadow_hunter_voshgajin(Creature* creature) : BossAI(creature, DATA_SHADOW_HUNTER_VOSHGAJIN) { }

    /**
     * @brief 重置 BOSS 状态
     *
     * 当 BOSS 脱离战斗或重置时调用。
     * 清除所有事件计时器,重置战斗状态。
     */
    void Reset() override
    {
        _Reset();
        //DoCast(me, SPELL_ICEARMOR, true);  // 注释代码:原版可能有冰甲术,但未启用
    }

    /**
     * @brief 进入战斗
     * @param who 仇恨目标
     *
     * 当 BOSS 进入战斗时调用。
     * 初始化所有技能的施放计时器。
     *
     * 技能施放时机:
     * - 诅咒之血: 2秒后首次施放
     * - 妖术: 8秒后首次施放
     * - 顺劈斩: 14秒后首次施放
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_CURSE_OF_BLOOD, 2s);
        events.ScheduleEvent(EVENT_HEX, 8s);
        events.ScheduleEvent(EVENT_CLEAVE, 14s);
    }

    /**
     * @brief 死亡处理
     * @param killer 击杀者(未使用)
     *
     * 当 BOSS 死亡时调用。
     * 清理战斗状态和事件。
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
    }

    /**
     * @brief 更新 AI 逻辑
     * @param diff 距离上次更新的时间间隔(毫秒)
     *
     * 每个游戏循环周期调用,处理 BOSS 的战斗行为。
     *
     * 执行流程:
     * 1. 检查是否有有效的战斗目标
     * 2. 更新事件计时器
     * 3. 如果正在施法则等待
     * 4. 处理到期的技能事件
     * 5. 执行近战攻击
     *
     * 性能注意事项:
     * - 使用事件系统管理技能冷却,避免频繁的时间计算
     * - 施法状态检查确保不会打断正在施放的法术
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效的战斗目标
        if (!UpdateVictim())
            return;

        // 更新事件计时器
        events.Update(diff);

        // 如果正在施法,则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CURSE_OF_BLOOD:
                    // 对当前目标施放诅咒之血
                    DoCastVictim(SPELL_CURSEOFBLOOD);
                    // 45秒后再次施放
                    events.ScheduleEvent(EVENT_CURSE_OF_BLOOD, 45s);
                    break;
                case EVENT_HEX:
                    // 随机选择一个目标施放妖术
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                        DoCast(target, SPELL_HEX);
                    // 15秒后再次施放
                    events.ScheduleEvent(EVENT_HEX, 15s);
                    break;
                case EVENT_CLEAVE:
                    // 对当前目标施放顺劈斩
                    DoCastVictim(SPELL_CLEAVE);
                    // 7秒后再次施放
                    events.ScheduleEvent(EVENT_CLEAVE, 7s);
                    break;
            }

            // 如果开始施法,则退出循环等待下一帧
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 注册 BOSS 脚本
 *
 * 将暗影猎手沃什加斯的 AI 注册到脚本系统中。
 */
void AddSC_boss_shadowvosh()
{
    RegisterBlackrockSpireCreatureAI(boss_shadow_hunter_voshgajin);
}
