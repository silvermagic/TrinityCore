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
 * @file boss_gehennas.cpp
 * @brief 熔火之心副本 - 基赫纳斯 (Gehennas) BOSS脚本
 *
 * 本模块实现了熔火之心副本中基赫纳斯BOSS的AI逻辑。
 * 基赫纳斯是一个火焰领主BOSS，拥有以下主要技能：
 * - 基赫纳斯的诅咒(Gehennas Curse): 对目标施加诅咒
 * - 火焰之雨(Rain of Fire): 对随机目标区域施放火焰雨
 * - 暗影箭(Shadow Bolt): 对随机目标施放暗影箭
 *
 * BOSS的特殊机制：
 * - 该BOSS周围有随从小怪（未完全实现）
 * - 需要优先处理小怪
 *
 * 开发进度: 90%
 * 存在问题: 随从小怪的心控机制尚未实现
 */

/* ScriptData
SDName: Boss_Gehennas
SD%Complete: 90
SDComment: Adds MC NYI
SDCategory: Molten Core
EndScriptData */

#include "ScriptMgr.h"
#include "molten_core.h"
#include "ObjectMgr.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 */
enum Spells
{
    SPELL_GEHENNAS_CURSE    = 19716,    ///< 基赫纳斯的诅咒 - 对目标施加诅咒效果
    SPELL_RAIN_OF_FIRE      = 19717,    ///< 火焰之雨 - AOE伤害技能
    SPELL_SHADOW_BOLT       = 19728,    ///< 暗影箭 - 单体伤害技能
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_GEHENNAS_CURSE    = 1,    ///< 基赫纳斯的诅咒事件
    EVENT_RAIN_OF_FIRE      = 2,    ///< 火焰之雨事件
    EVENT_SHADOW_BOLT       = 3,    ///< 暗影箭事件
};

/**
 * @brief 基赫纳斯BOSS AI结构体
 *
 * 继承自BossAI，实现了基赫纳斯的战斗逻辑。
 * 该BOSS主要使用诅咒、火焰雨和暗影箭进行战斗。
 */
struct boss_gehennas : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_gehennas(Creature* creature) : BossAI(creature, BOSS_GEHENNAS)
    {
    }

    /**
     * @brief 进入战斗时调用
     * @param victim 战斗目标
     *
     * 初始化战斗事件调度器，安排各个技能的施放时间。
     * 调用时机: 当BOSS进入战斗状态时触发
     */
    void JustEngagedWith(Unit* victim) override
    {
        BossAI::JustEngagedWith(victim);
        // 初始化技能冷却时间
        events.ScheduleEvent(EVENT_GEHENNAS_CURSE, 12s);    // 基赫纳斯的诅咒，12秒后首次施放
        events.ScheduleEvent(EVENT_RAIN_OF_FIRE, 10s);      // 火焰之雨，10秒后首次施放
        events.ScheduleEvent(EVENT_SHADOW_BOLT, 6s);        // 暗影箭，6秒后首次施放
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主要职责:
     * 1. 检查BOSS是否有有效目标
     * 2. 处理技能事件队列
     * 3. 执行近战攻击
     *
     * 性能注意事项:
     * - 每帧都会调用此函数，应避免复杂计算
     * - 使用事件系统管理技能冷却，避免频繁创建计时器
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效战斗目标
        if (!UpdateVictim())
            return;

        // 更新事件系统
        events.Update(diff);

        // 如果正在施法，等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理事件队列
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_GEHENNAS_CURSE:
                    // 基赫纳斯的诅咒：对当前目标施放
                    DoCastVictim(SPELL_GEHENNAS_CURSE);
                    // 22-30秒后再次施放
                    events.ScheduleEvent(EVENT_GEHENNAS_CURSE, 22s, 30s);
                    break;
                case EVENT_RAIN_OF_FIRE:
                    // 火焰之雨：随机选择一个目标区域施放
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_RAIN_OF_FIRE);
                    // 4-12秒后再次施放
                    events.ScheduleEvent(EVENT_RAIN_OF_FIRE, 4s, 12s);
                    break;
                case EVENT_SHADOW_BOLT:
                    // 暗影箭：随机选择一个非首要仇恨目标
                    // SelectTarget参数说明:
                    // - SelectTargetMethod::Random: 随机选择
                    // - 1: 跳过第一个目标（即跳过坦克），选择其他目标
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1))
                        DoCast(target, SPELL_SHADOW_BOLT);
                    // 7秒后再次施放
                    events.ScheduleEvent(EVENT_SHADOW_BOLT, 7s);
                    break;
                default:
                    break;
            }

            // 如果施法后进入施法状态，退出循环等待下一帧
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 如果没有施法且可以近战，执行近战攻击
        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 注册基赫纳斯BOSS脚本
 *
 * 此函数用于将BOSS AI注册到脚本系统中。
 * 在服务器启动时由脚本加载器调用。
 */
void AddSC_boss_gehennas()
{
    // 注册基赫纳斯的AI
    RegisterMoltenCoreCreatureAI(boss_gehennas);
}
