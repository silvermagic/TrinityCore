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
 * @file boss_halycon.cpp
 * @brief 黑石尖塔副本BOSS：哈雷卡尔(Halycon)的AI脚本实现
 *
 * 哈雷卡尔是黑石尖塔（下）的BOSS之一，是一只巨大的黑狼。
 * 她是奴役者基兹卢尔的同伴，死亡后会召唤基兹卢尔来复仇。
 *
 * BOSS特点：
 * - 使用撕裂和痛击等狼类技能
 * - 死亡时会召唤奴役者基兹卢尔
 * - 与基兹卢尔形成连续BOSS战斗
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 * 定义哈雷卡尔使用的所有法术ID
 */
enum Spells
{
    SPELL_REND                      = 13738,  // 撕裂 - 造成物理伤害并使目标流血
    SPELL_THRASH                    = 3391,   // 痛击 - 额外攻击，造成物理伤害
};

/**
 * @brief 对话文本枚举
 * 定义BOSS的对话和表情文本ID
 */
enum Says
{
    EMOTE_DEATH                     = 0       // 死亡表情 - 用于召唤基兹卢尔
};

/**
 * @brief 事件ID枚举
 * 定义战斗事件调度器使用的事件类型
 */
enum Events
{
    EVENT_REND                      = 1,      // 撕裂事件
    EVENT_THRASH                    = 2,      // 痛击事件
};

/**
 * @brief 召唤位置常量
 * 定义奴役者基兹卢尔被召唤时的位置坐标和朝向
 */
const Position SummonLocation = { -167.9561f, -411.7844f, 76.23057f, 1.53589f };

/**
 * @brief 哈雷卡尔BOSS AI结构体
 *
 * 继承自BossAI，实现哈雷卡尔的战斗AI逻辑。
 * 该AI控制BOSS的法术释放时机、基兹卢尔召唤机制和战斗状态。
 */
struct boss_halycon : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     * 初始化BOSS AI，关联哈雷卡尔的副本数据
     */
    boss_halycon(Creature* creature) : BossAI(creature, DATA_HALYCON)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     * 将召唤标志重置为false
     */
    void Initialize()
    {
        Summoned = false;
    }

    void Reset() override
    {
        _Reset();
        Initialize();
    }

    /**
     * @brief 进入战斗事件处理
     * @param who 进入战斗的目标单位
     *
     * 当BOSS进入战斗状态时调用。
     * 调用基类的进入战斗方法，并调度所有战斗法术的初始释放时间。
     *
     * 调用时机：BOSS被攻击或主动攻击玩家时
     * 性能注意事项：事件调度为轻量级操作，无性能影响
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_REND, 17s, 20s);     // 17-20秒后首次施放撕裂
        events.ScheduleEvent(EVENT_THRASH, 10s, 12s);   // 10-12秒后首次施放痛击
    }

    /**
     * @brief 死亡事件处理
     * @param killer 击杀者单位（当前未使用）
     *
     * 当BOSS死亡时，在预设位置召唤奴役者基兹卢尔。
     * 基兹卢尔会在5分钟后自动消失。
     * 同时播放死亡表情文本。
     *
     * 调用时机：BOSS被击杀时
     */
    void JustDied(Unit* /*killer*/) override
    {
        // 在预设位置召唤奴役者基兹卢尔，5分钟后消失
        me->SummonCreature(NPC_GIZRUL_THE_SLAVENER, SummonLocation, TEMPSUMMON_TIMED_DESPAWN, 5min);
        // 播放死亡表情
        Talk(EMOTE_DEATH);

        Summoned = true;
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 每个游戏循环周期调用一次，处理战斗逻辑。
     * 包括检查战斗状态、更新事件计时器、执行法术释放。
     *
     * 调用时机：每个游戏Tick（约每50毫秒）
     * 性能注意事项：频繁调用，已优化处理逻辑
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果没有有效的攻击目标，则不执行任何操作
        if (!UpdateVictim())
            return;

        // 更新事件计时器
        events.Update(diff);

        // 如果正在施法，则不执行其他动作
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 执行所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_REND:
                    // 施放撕裂，造成物理伤害并使目标流血
                    DoCastVictim(SPELL_REND);
                    events.ScheduleEvent(EVENT_REND, 8s, 10s);
                    break;
                case EVENT_THRASH:
                    // 施放痛击，获得额外攻击
                    DoCast(me, SPELL_THRASH);
                    break;
                default:
                    break;
            }

            // 如果施法后进入施法状态，则退出循环等待下一帧
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }
        DoMeleeAttackIfReady();
    }
    private:
        bool Summoned;  ///< 是否已召唤基兹卢尔标志（当前未使用，可能用于防止重复召唤）
};

/**
 * @brief 注册哈雷卡尔脚本
 *
 * 将哈雷卡尔BOSS脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_boss_halycon()
{
    RegisterBlackrockSpireCreatureAI(boss_halycon);
}
