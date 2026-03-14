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
 * @file boss_hakkar.cpp
 * @brief 祖尔格拉布副本 - 哈卡(Boss Hakkar)战斗脚本
 *
 * 本模块实现了祖尔格拉布副本最终Boss哈卡的AI逻辑：
 * - 祖尔格拉布的主宰,血神哈卡
 * - 主要技能: 血之虹吸、堕落之血、哈卡的意志、狂暴
 * - 特殊机制: 根据已击杀的高阶祭司,获得对应的能力提升
 * - 区域触发: 玩家进入副本时哈卡会说话
 */

#include "zulgurub.h"
#include "DBCStructure.h"
#include "InstanceScript.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"

/**
 * @brief Boss 对话文本ID
 */
enum Says
{
    SAY_AGGRO                   = 0,    /**< 开战对话 */
    SAY_FLEEING                 = 1,    /**< 逃跑对话 */
    SAY_MINION_DESTROY          = 2,    /**< 手下被消灭对话 */
    SAY_PROTECT_ALTAR           = 3,    /**< 保护祭坛对话 */
    SAY_ENTRANCE                = 4     /**< 进入副本对话 */
};

/**
 * @brief Boss 使用的法术ID
 */
enum Spells
{
    SPELL_BLOOD_SIPHON          = 24322, /**< 血之虹吸 - 每90秒施放,吸收玩家生命 */
    SPELL_CORRUPTED_BLOOD       = 24328, /**< 堕落之血 - 疾病效果,在玩家间传播 */
    SPELL_CAUSE_INSANITY        = 24327, /**< 引发疯狂 - 需要脚本支持(已禁用) */
    SPELL_WILL_OF_HAKKAR        = 24178, /**< 哈卡的意志 - 精神控制 */
    SPELL_ENRAGE                = 24318, /**< 狂暴 - 10分钟后进入狂暴状态 */

    // 高阶祭司的各 aspects (能力提升)
    SPELL_ASPECT_OF_JEKLIK      = 24687, /**< 杰利克的方面 - 对应蝙蝠祭司 */
    SPELL_ASPECT_OF_VENOXIS     = 24688, /**< 维诺希斯的方面 - 对应蛇祭司 */
    SPELL_ASPECT_OF_MARLI       = 24686, /**< 玛尔里的方面 - 对应蜘蛛祭司 */
    SPELL_ASPECT_OF_THEKAL      = 24689, /**< 塞卡尔的方面 - 对应虎祭司 */
    SPELL_ASPECT_OF_ARLOKK      = 24690  /**< 阿洛卡的方面 - 对应豹祭司 */
};

/**
 * @brief Boss 事件ID
 */
enum Events
{
    EVENT_BLOOD_SIPHON          = 1,    /**< 血之虹吸事件 */
    EVENT_CORRUPTED_BLOOD       = 2,    /**< 堕落之血事件 */
    EVENT_CAUSE_INSANITY        = 3,    /**< 引发疯狂事件(已禁用) */
    EVENT_WILL_OF_HAKKAR        = 4,    /**< 哈卡的意志事件 */
    EVENT_ENRAGE                = 5,    /**< 狂暴事件 */

    // 高阶祭司的各 aspects 事件
    EVENT_ASPECT_OF_JEKLIK      = 6,    /**< 杰利克的方面事件 */
    EVENT_ASPECT_OF_VENOXIS     = 7,    /**< 维诺希西斯的方面事件 */
    EVENT_ASPECT_OF_MARLI       = 8,    /**< 玛尔里的方面事件 */
    EVENT_ASPECT_OF_THEKAL      = 9,    /**< 塞卡尔的方面事件 */
    EVENT_ASPECT_OF_ARLOKK      = 10    /**< 阿洛卡的方面事件 */
};

/**
 * @brief 哈卡Boss AI
 *
 * 实现哈卡的战斗逻辑,包括:
 * - 血之虹吸: 每90秒吸收玩家生命
 * - 堕落之血: 传播性疾病
 * - 哈卡的意志: 精神控制威胁最高的玩家
 * - 狂暴: 10分钟后进入狂暴状态
 * - 高阶祭司的能力: 如果对应的高阶祭司未被击杀,哈卡会获得对应的能力
 */
struct boss_hakkar : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_hakkar(Creature* creature) : BossAI(creature, DATA_HAKKAR) { }

    /**
     * @brief 重置Boss状态
     *
     * 调用父类的重置方法,清除所有事件
     */
    void Reset() override
    {
        _Reset();
    }

    /**
     * @brief Boss死亡时调用
     * @param killer 击杀者(未使用)
     *
     * 调用父类的死亡方法,更新副本进度
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     *
     * 安排各技能事件:
     * - 血之虹吸: 90秒后首次施放
     * - 堕落之血: 25秒后首次施放
     * - 引发疯狂: 15秒后首次施放(已禁用)
     * - 哈卡的意志: 15秒后首次施放
     * - 狂暴: 10分钟后首次施放
     * - 各高阶祭司的方面: 根据对应Boss是否已击杀决定是否安排
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);

        // 安排核心技能
        events.ScheduleEvent(EVENT_BLOOD_SIPHON, 90s);
        events.ScheduleEvent(EVENT_CORRUPTED_BLOOD, 25s);
        events.ScheduleEvent(EVENT_CAUSE_INSANITY, 15s);
        events.ScheduleEvent(EVENT_WILL_OF_HAKKAR, 15s);
        events.ScheduleEvent(EVENT_ENRAGE, 10min);

        // 根据对应Boss是否已击杀,安排高阶祭司的能力
        if (instance->GetBossState(DATA_JEKLIK) != DONE)
            events.ScheduleEvent(EVENT_ASPECT_OF_JEKLIK, 4s);
        if (instance->GetBossState(DATA_VENOXIS) != DONE)
            events.ScheduleEvent(EVENT_ASPECT_OF_VENOXIS, 7s);
        if (instance->GetBossState(DATA_MARLI) != DONE)
            events.ScheduleEvent(EVENT_ASPECT_OF_MARLI, 12s);
        if (instance->GetBossState(DATA_THEKAL) != DONE)
            events.ScheduleEvent(EVENT_ASPECT_OF_THEKAL, 8s);
        if (instance->GetBossState(DATA_ARLOKK) != DONE)
            events.ScheduleEvent(EVENT_ASPECT_OF_ARLOKK, 18s);

        Talk(SAY_AGGRO);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 主要处理事件调度和技能施放:
     * - 血之虹吸: 每90秒施放,对当前目标
     * - 堕落之血: 每30-45秒施放,对当前目标
     * - 引发疯狂: 已禁用
     * - 哈卡的意志: 当有多个敌人时,对威胁最高的玩家施放
     * - 狂暴: 施放狂暴光环,90秒后再次检查
     * - 各方面技能: 根据安排的事件施放
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        // 如果正在施法,不执行其他操作
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_BLOOD_SIPHON:
                    // 对当前目标施放血之虹吸
                    DoCastVictim(SPELL_BLOOD_SIPHON, true);
                    events.ScheduleEvent(EVENT_BLOOD_SIPHON, 90s);
                    break;

                case EVENT_CORRUPTED_BLOOD:
                    // 对当前目标施放堕落之血
                    DoCastVictim(SPELL_CORRUPTED_BLOOD, true);
                    events.ScheduleEvent(EVENT_CORRUPTED_BLOOD, 30s, 45s);
                    break;

                case EVENT_CAUSE_INSANITY:
                    // 引发疯狂(已禁用,需要脚本支持)
                    // DoCast(SelectTarget(SelectTargetMethod::Random, 0, 100, true), SPELL_CAUSE_INSANITY);
                    // events.ScheduleEvent(EVENT_CAUSE_INSANITY, 35s, 45s);
                    break;

                case EVENT_WILL_OF_HAKKAR:
                {
                    // 精神控制只在对哈卡战斗的单位多于1个时触发(包括宠物/守护者)
                    // 但实际只施放在威胁最高的玩家身上
                    std::list<Unit*> unitList;
                    SelectTargetList(unitList, 2, SelectTargetMethod::MaxThreat, 0, 0.0f, false);
                    if (unitList.size() > 1)
                        DoCast(SelectTarget(SelectTargetMethod::MaxThreat, 0, 100, true), SPELL_WILL_OF_HAKKAR);
                    events.ScheduleEvent(EVENT_WILL_OF_HAKKAR, 25s, 35s);
                    break;
                }

                case EVENT_ENRAGE:
                    // 如果还没有狂暴光环,施放狂暴
                    if (!me->HasAura(SPELL_ENRAGE))
                        DoCast(me, SPELL_ENRAGE);
                    events.ScheduleEvent(EVENT_ENRAGE, 90s);
                    break;

                case EVENT_ASPECT_OF_JEKLIK:
                    // 杰利克的方面
                    DoCastVictim(SPELL_ASPECT_OF_JEKLIK, true);
                    events.ScheduleEvent(EVENT_ASPECT_OF_JEKLIK, 10s, 14s);
                    break;

                case EVENT_ASPECT_OF_VENOXIS:
                    // 维诺希西斯的方面
                    DoCastVictim(SPELL_ASPECT_OF_VENOXIS, true);
                    events.ScheduleEvent(EVENT_ASPECT_OF_VENOXIS, 8s);
                    break;

                case EVENT_ASPECT_OF_MARLI:
                    // 玛尔里的方面
                    DoCastVictim(SPELL_ASPECT_OF_MARLI, true);
                    events.ScheduleEvent(EVENT_ASPECT_OF_MARLI, 10s);
                    break;

                case EVENT_ASPECT_OF_THEKAL:
                    // 塞卡尔的方面
                    DoCastVictim(SPELL_ASPECT_OF_THEKAL, true);
                    events.ScheduleEvent(EVENT_ASPECT_OF_THEKAL, 15s);
                    break;

                case EVENT_ASPECT_OF_ARLOKK:
                    // 阿洛卡的方面
                    DoCastVictim(SPELL_ASPECT_OF_ARLOKK, true);
                    events.ScheduleEvent(EVENT_ASPECT_OF_ARLOKK, 10s, 15s);
                    break;

                default:
                    break;
            }

            // 如果正在施法,退出循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 祖尔格拉布入口区域触发脚本
 *
 * 玩家进入副本的不同区域时,哈卡会说不同的话
 * 该脚本只会在玩家首次触发时执行(OnlyOnce)
 */
class at_zulgurub_entrance : public OnlyOnceAreaTriggerScript
{
public:
    /**
     * @brief 构造函数
     */
    at_zulgurub_entrance() : OnlyOnceAreaTriggerScript("at_zulgurub_entrance") { }

    /**
     * @brief 处理区域触发
     * @param player 触发区域的玩家
     * @param areaTrigger 区域触发器数据
     * @return 返回true表示处理成功
     *
     * 根据不同的区域触发器ID,让哈卡说不同的话:
     * - AREA_TRIGGER_1: 进入副本时的对话
     * - AREA_TRIGGER_2: 保护祭坛的对话
     * - AREA_TRIGGER_3: 手下被消灭的对话
     */
    bool TryHandleOnce(Player* player, AreaTriggerEntry const* areaTrigger) override
    {
        InstanceScript* instance = player->GetInstanceScript();
        // 如果没有副本脚本或哈卡已死亡,直接返回
        if (!instance || instance->GetBossState(DATA_HAKKAR) == DONE)
            return true;

        // 获取哈卡实体
        if (Creature* hakkar = instance->GetCreature(DATA_HAKKAR))
        {
            switch (areaTrigger->ID)
            {
                case AREA_TRIGGER_1:
                    // 进入副本时的对话
                    hakkar->AI()->Talk(SAY_ENTRANCE);
                    break;
                case AREA_TRIGGER_2:
                    // 保护祭坛的对话
                    hakkar->AI()->Talk(SAY_PROTECT_ALTAR);
                    break;
                case AREA_TRIGGER_3:
                    // 手下被消灭的对话
                    hakkar->AI()->Talk(SAY_MINION_DESTROY);
                    break;
                default:
                    break;
            }
        }

        return true;
    }
};

void AddSC_boss_hakkar()
{
    RegisterZulGurubCreatureAI(boss_hakkar);
    new at_zulgurub_entrance();
}
