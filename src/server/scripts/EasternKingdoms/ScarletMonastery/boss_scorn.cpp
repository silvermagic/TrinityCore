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
 * @file boss_scorn.cpp
 * @brief 血色修道院BOSS斯科恩战斗脚本
 *
 * 本模块实现了血色修道院（墓地）稀有BOSS斯科恩的战斗AI：
 * - 斯科恩（亡灵巫妖）
 * - 血色修道院墓地区域的稀有刷新BOSS
 *
 * 战斗机制：
 * 1. 使用冰霜和暗影魔法进行攻击
 * 2. 定期施放巫妖之击、冰霜箭齐射、精神鞭笞和冰霜新星
 * 3. 综合能力强，既有单体伤害也有AOE技能
 *
 * 特殊事件：
 * - 无特殊触发机制，从头到尾保持稳定的技能循环
 */

#include "scarlet_monastery.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"

/**
 * @brief 斯科恩法术ID枚举
 *
 * 定义斯科恩使用的所有法术ID
 */
enum ScornSpells
{
    SPELL_LICHSLAP = 28873,          // 巫妖之击 - 强力近战攻击技能
    SPELL_FROSTBOLT_VOLLEY = 8398,   // 冰霜箭齐射 - AOE冰霜伤害技能
    SPELL_MINDFLAY = 17313,          // 精神鞭笞 - 暗影持续伤害技能
    SPELL_FROSTNOVA = 15531          // 冰霜新星 - AOE冰霜技能并减速
};

/**
 * @brief 斯科恩事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum ScornEvents
{
    EVENT_LICH_SLAP = 1,        // 巫妖之击事件
    EVENT_FROSTBOLT_VOLLEY,     // 冰霜箭齐射事件
    EVENT_MIND_FLAY,            // 精神鞭笞事件
    EVENT_FROST_NOVA            // 冰霜新星事件
};

/**
 * @brief 斯科恩BOSS AI结构体
 *
 * 实现斯科恩的战斗AI逻辑，包括：
 * - 巫妖之击、冰霜箭齐射、精神鞭笞、冰霜新星等技能
 * - 综合使用冰霜和暗影魔法
 *
 * 战斗流程：
 * 1. 进入战斗时安排技能事件
 * 2. 定期施放巫妖之击（45秒冷却）、冰霜箭齐射（20秒冷却）、精神鞭笞（20秒冷却）、冰霜新星（15秒冷却）
 * 3. 近战攻击
 *
 * 技能特点：
 * - 巫妖之击：强力的近战攻击，需要坦克注意
 * - 冰霜箭齐射：AOE伤害，需要对全队造成伤害
 * - 精神鞭笞：持续伤害，限制目标行动
 * - 冰霜新星：AOE伤害并减速，控制玩家走位
 */
struct boss_scorn : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化BOSS AI
     */
    boss_scorn(Creature* creature) : BossAI(creature, DATA_SCORN) { }

    /**
     * @brief 进入战斗事件
     * @param who 进入战斗的目标
     *
     * 当斯科恩进入战斗时：
     * - 安排技能施放事件
     *
     * 技能安排：
     * - 巫妖之击：45秒后施放
     * - 冰霜箭齐射：30秒后施放
     * - 精神鞭笞：30秒后施放
     * - 冰霜新星：30秒后施放
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_LICH_SLAP, 45s);
        events.ScheduleEvent(EVENT_FROSTBOLT_VOLLEY, 30s);
        events.ScheduleEvent(EVENT_MIND_FLAY, 30s);
        events.ScheduleEvent(EVENT_FROST_NOVA, 30s);
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
            case EVENT_LICH_SLAP:
                DoCastVictim(SPELL_LICHSLAP);
                events.Repeat(45s);
                break;
            case EVENT_FROSTBOLT_VOLLEY:
                DoCastVictim(SPELL_FROSTBOLT_VOLLEY);
                events.Repeat(20s);
                break;
            case EVENT_MIND_FLAY:
                DoCastVictim(SPELL_MINDFLAY);
                events.Repeat(20s);
                break;
            case EVENT_FROST_NOVA:
                DoCastVictim(SPELL_FROSTNOVA);
                events.Repeat(15s);
                break;
            default:
                break;
        }
    }
};

/**
 * @brief 注册BOSS脚本
 *
 * 将斯科恩的AI注册到脚本系统中
 */
void AddSC_boss_scorn()
{
    RegisterScarletMonasteryCreatureAI(boss_scorn);
}
