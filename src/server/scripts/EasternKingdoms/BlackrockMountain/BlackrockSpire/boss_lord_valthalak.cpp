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
 * @file boss_lord_valthalak.cpp
 * @brief 黑石塔上层首领 - 瓦塔拉克公爵 (Lord Valthalak) AI 实现
 *
 * 本模块实现了黑石塔上层副本最终首领瓦塔拉克公爵的战斗AI逻辑。
 * 瓦塔拉克公爵是一个强大的术士型首领，能召唤幽灵刺客并使用暗影魔法。
 *
 * 主要功能：
 * - 管理首领的战斗周期性技能施放
 * - 实现召唤幽灵刺客技能
 * - 实现暗影箭齐射和暗影之怒技能
 * - 实现生命值阈值触发的狂暴阶段
 * - 处理不同战斗阶段的技能切换
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 *
 * 定义瓦塔拉克公爵使用的所有法术ID
 */
enum Spells
{
    SPELL_FRENZY                    = 8269,  ///< 狂暴 - 提高攻击速度和伤害
    SPELL_SUMMON_SPECTRAL_ASSASSIN  = 27249, ///< 召唤幽灵刺客 - 召唤一个幽灵刺客协助战斗
    SPELL_SHADOW_BOLT_VOLLEY        = 27382, ///< 暗影箭齐射 - 对周围所有敌人发射暗影箭
    SPELL_SHADOW_WRATH              = 27286  ///< 暗影之怒 - 对单个目标造成暗影伤害
};

/**
 * @brief 对话文本枚举
 *
 * 定义首领使用的文本ID
 */
enum Says
{
    EMOTE_FRENZY                    = 0      ///< 狂暴表情 - 显示狂暴状态提示
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的所有事件ID，用于事件调度系统
 */
enum Events
{
    EVENT_SUMMON_SPECTRAL_ASSASSIN  = 1,     ///< 召唤幽灵刺客事件
    EVENT_SHADOW_BOLT_VOLLEY        = 2,     ///< 暗影箭齐射事件
    EVENT_SHADOW_WRATH              = 3      ///< 暗影之怒事件
};

/**
 * @brief 瓦塔拉克公爵 AI 结构体
 *
 * 继承自 BossAI 基类，实现瓦塔拉克公爵的完整战斗AI。
 * 负责管理技能施放时机、战斗阶段转换和召唤机制。
 *
 * 战斗机制：
 * - 40%生命值时进入第一次狂暴，停止召唤幽灵刺客
 * - 15%生命值时进入第二次狂暴，开始施放暗影箭齐射
 */
struct boss_lord_valthalak : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 关联的生物对象指针
     *
     * 初始化 BossAI 基类，关联首领数据为 DATA_LORD_VALTHALAK，
     * 并调用 Initialize() 初始化成员变量
     */
    boss_lord_valthalak(Creature* creature) : BossAI(creature, DATA_LORD_VALTHALAK)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 将所有狂暴标记重置为 false，用于控制战斗阶段转换。
     * 应在构造函数和 Reset() 中调用。
     */
    void Initialize()
    {
        frenzy40 = false;  ///< 40%生命值狂暴标记
        frenzy15 = false;  ///< 15%生命值狂暴标记
    }

    /**
     * @brief 重置首领状态
     *
     * 当首领脱离战斗或重置时调用。
     * 调用基类的 _Reset() 方法清理事件队列和重置战斗状态，
     * 并重新初始化成员变量。
     *
     * @note 调用时机：首领脱战、重置副本、首领死亡后重生
     */
    void Reset() override
    {
        _Reset();
        Initialize();
    }

    /**
     * @brief 进入战斗回调
     * @param who 触发战斗的单位（通常是第一个攻击者）
     *
     * 当首领进入战斗状态时调用。
     * 初始化战斗事件调度：
     * - 召唤幽灵刺客：6-8秒后首次施放
     * - 暗影之怒：9-18秒后首次施放
     *
     * @note 调用时机：首领被玩家攻击或主动攻击玩家
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_SUMMON_SPECTRAL_ASSASSIN, 6s, 8s);
        events.ScheduleEvent(EVENT_SHADOW_WRATH, 9s, 18s);
    }

    /**
     * @brief 死亡回调
     * @param killer 击杀首领的单位（可为nullptr）
     *
     * 当首领死亡时调用。
     * 设置副本数据为 DONE 状态，触发副本完成事件。
     *
     * @note 调用时机：首领生命值降为0
     */
    void JustDied(Unit* /*killer*/) override
    {
        instance->SetData(DATA_LORD_VALTHALAK, DONE);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 每个服务器tick调用一次，处理战斗AI的主逻辑循环。
     *
     * 执行流程：
     * 1. 检查是否有有效攻击目标，无则返回
     * 2. 更新事件队列
     * 3. 如果正在施法则暂停技能处理
     * 4. 执行到期事件并施放相应技能
     * 5. 检查生命值阈值并触发狂暴阶段
     * 6. 如果未施法则进行近战攻击
     *
     * 阶段转换：
     * - 40%生命值：施放狂暴，停止召唤幽灵刺客
     * - 15%生命值：施放狂暴，开始施放暗影箭齐射
     *
     * 技能循环：
     * - 召唤幽灵刺客：每30-35秒施放一次（40%生命值前）
     * - 暗影箭齐射：每4-6秒施放一次（15%生命值后）
     * - 暗影之怒：每19-24秒施放一次
     *
     * @note 性能注意事项：避免在此函数中进行耗时操作，保持高效执行
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效攻击目标
        if (!UpdateVictim())
            return;

        // 更新事件队列时间
        events.Update(diff);

        // 如果正在施法，则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有到期事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SUMMON_SPECTRAL_ASSASSIN:
                    // 召唤幽灵刺客协助战斗
                    DoCast(me, SPELL_SUMMON_SPECTRAL_ASSASSIN);
                    // 安排下一次召唤，30-35秒后
                    events.ScheduleEvent(EVENT_SUMMON_SPECTRAL_ASSASSIN, 30s, 35s);
                    break;
                case EVENT_SHADOW_BOLT_VOLLEY:
                    // 对当前目标施放暗影箭齐射
                    DoCastVictim(SPELL_SHADOW_BOLT_VOLLEY);
                    // 安排下一次暗影箭齐射，4-6秒后
                    events.ScheduleEvent(EVENT_SHADOW_BOLT_VOLLEY, 4s, 6s);
                    break;
                case EVENT_SHADOW_WRATH:
                    // 对当前目标施放暗影之怒
                    DoCastVictim(SPELL_SHADOW_WRATH);
                    // 安排下一次暗影之怒，19-24秒后
                    events.ScheduleEvent(EVENT_SHADOW_WRATH, 19s, 24s);
                    break;
                default:
                    break;
            }

            // 如果在事件处理过程中开始施法，则退出循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 检查40%生命值狂暴阶段
        if (!frenzy40)
        {
            if (HealthBelowPct(40))
            {
                // 施放狂暴技能进入第一阶段
                DoCast(me, SPELL_FRENZY);
                // 停止召唤幽灵刺客
                events.CancelEvent(EVENT_SUMMON_SPECTRAL_ASSASSIN);
                frenzy40 = true;
            }
        }

        // 检查15%生命值狂暴阶段
        if (!frenzy15)
        {
            if (HealthBelowPct(15))
            {
                // 施放狂暴技能进入第二阶段
                DoCast(me, SPELL_FRENZY);
                // 开始施放暗影箭齐射
                events.ScheduleEvent(EVENT_SHADOW_BOLT_VOLLEY, 7s, 14s);
                frenzy15 = true;
            }
        }

        // 如果没有在施法且准备就绪，进行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    bool frenzy40;  ///< 40%生命值狂暴标记，控制第一阶段转换
    bool frenzy15;  ///< 15%生命值狂暴标记，控制第二阶段转换
};

/**
 * @brief 注册瓦塔拉克公爵 AI
 *
 * 此函数将瓦塔拉克公爵的AI注册到脚本系统中，
 * 使游戏服务器能够正确加载和运行该首领的AI逻辑。
 */
void AddSC_boss_lord_valthalak()
{
    RegisterBlackrockSpireCreatureAI(boss_lord_valthalak);
}
