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
 * @file boss_houndmaster_loksey.cpp
 * @brief 血色修道院BOSS驯犬者洛克希战斗脚本
 *
 * 本模块实现了血色修道院（军械库）BOSS驯犬者洛克希的战斗AI：
 * - 驯犬者洛克希（血色十字军驯犬师）
 * - 血色修道院军械库区域的BOSS
 *
 * 战斗机制：
 * 1. 进入战斗时召唤血色猎犬协助作战
 * 2. 核心机制：血量低于60%时施放嗜血术增强自己
 *
 * 特殊事件：
 * - 开战立即召唤血色猎犬，玩家需要同时应对BOSS和猎犬
 * - 嗜血术会持续检查血量条件，直到触发后才会进入60秒冷却
 */

#include "scarlet_monastery.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"

/**
 * @brief 驯犬者洛克希对话文本ID枚举
 *
 * 定义洛克希在战斗中的各种对话文本ID
 */
enum HoundmasterLokseyYells
{
    SAY_AGGRO = 0,  // 进入战斗时的喊话
};

/**
 * @brief 驯犬者洛克希法术ID枚举
 *
 * 定义洛克希使用的所有法术ID
 */
enum HoundmasterLokseySpells
{
    SPELL_SUMMON_SCARLET_HOUND = 17164, // 召唤血色猎犬 - 召唤宠物协助作战
    SPELL_BLOODLUST = 6742              // 嗜血术 - 增加攻击速度和伤害
};

/**
 * @brief 驯犬者洛克希事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum HoundmasterLokseyEvents
{
    EVENT_BLOODLUST = 1  // 嗜血术事件
};

/**
 * @brief 驯犬者洛克希BOSS AI结构体
 *
 * 实现洛克希的战斗AI逻辑，包括：
 * - 召唤血色猎犬
 * - 嗜血术增益
 *
 * 战斗流程：
 * 1. 进入战斗时喊话并立即召唤血色猎犬
 * 2. 安排嗜血术检查事件（20秒后开始检查）
 * 3. 血量低于60%时施放嗜血术
 * 4. 近战攻击
 *
 * 嗜血术机制：
 * - 20秒后开始检查血量条件
 * - 如果血量低于60%，施放嗜血术并进入60秒冷却
 * - 如果血量高于60%，1秒后再次检查（持续监控）
 */
struct boss_houndmaster_loksey : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化BOSS AI
     */
    boss_houndmaster_loksey(Creature* creature) : BossAI(creature, DATA_HOUNDMASTER_LOKSEY) { }

    /**
     * @brief 进入战斗事件
     * @param who 进入战斗的目标
     *
     * 当洛克希进入战斗时：
     * - 喊出战斗台词
     * - 立即召唤血色猎犬协助作战
     * - 安排嗜血术检查事件
     *
     * 技能安排：
     * - 嗜血术：20秒后开始检查血量条件
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);
        DoCast(SPELL_SUMMON_SCARLET_HOUND);  // 立即召唤猎犬
        events.ScheduleEvent(EVENT_BLOODLUST, 20s);
    }

    /**
     * @brief 执行事件处理器
     * @param eventId 事件ID
     *
     * 处理定时触发的技能施放事件
     *
     * 嗜血术特殊机制：
     * - 不是简单的定时施放，而是检查血量条件
     * - 血量低于60%时才施放，否则1秒后再次检查
     * - 施放后进入60秒冷却
     */
    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_BLOODLUST:
                if (me->HealthBelowPct(60))
                {
                    // 血量低于60%，施放嗜血术
                    DoCastSelf(SPELL_BLOODLUST);
                    events.Repeat(60s);  // 60秒冷却
                }
                else
                {
                    // 血量还高于60%，1秒后再次检查
                    events.Repeat(1s);
                }
                break;
            default:
                break;
        }
    }
};

/**
 * @brief 注册BOSS脚本
 *
 * 将驯犬者洛克希的AI注册到脚本系统中
 */
void AddSC_boss_houndmaster_loksey()
{
    RegisterScarletMonasteryCreatureAI(boss_houndmaster_loksey);
}
