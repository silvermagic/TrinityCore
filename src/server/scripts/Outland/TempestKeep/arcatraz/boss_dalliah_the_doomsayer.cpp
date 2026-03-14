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
 * @file boss_dalliah_the_doomsayer.cpp
 * @brief 禁魔监狱副本Boss末日预言者达莉亚AI实现
 *
 * 本模块实现了末日预言者达莉亚的战斗逻辑。
 * 达莉亚与附近的Boss索克雷萨有互动关系。
 *
 * 战斗特点:
 * - 施放末日者礼物(治疗自己)
 * - 旋风斩造成范围伤害
 * - 与索克雷萨的互动台词
 *
 * @see https://wowpedia.fandom.com/wiki/Dalliah_the_Doomsayer
 */

#include "ScriptMgr.h"
#include "arcatraz.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"

/**
 * @enum Say
 * @brief 达莉亚和索克雷萨的台词枚举
 */
enum Say
{
    // 末日预言者达莉亚台词
    SAY_AGGRO                       = 1,  ///< 激活台词
    SAY_SLAY                        = 2,  ///< 击杀玩家台词
    SAY_WHIRLWIND                   = 3,  ///< 旋风斩台词
    SAY_HEAL                        = 4,  ///< 治疗台词
    SAY_DEATH                       = 5,  ///< 死亡台词
    SAY_SOCCOTHRATES_DEATH          = 7,  ///< 索克雷萨死亡时达莉亚的台词

    // 怒火占卜者索克雷萨台词
    SAY_AGGRO_DALLIAH_FIRST         = 0,  ///< 达莉亚先被激活时索克雷萨的台词
    SAY_DALLIAH_25_PERCENT          = 5   ///< 达莉亚血量低于25%时索克雷萨的嘲讽台词
};

/**
 * @enum Spells
 * @brief 达莉亚使用的法术ID枚举
 */
enum Spells
{
    SPELL_GIFT_OF_THE_DOOMSAYER     = 36173, ///< 末日者礼物 - 治疗自己
    SPELL_WHIRLWIND                 = 36142, ///< 旋风斩 - 范围伤害
    SPELL_HEAL                      = 36144, ///< 治疗
    SPELL_SHADOW_WAVE               = 39016  ///< 暗影波 - 英雄模式专用
};

/**
 * @enum Events
 * @brief 达莉亚事件ID枚举
 */
enum Events
{
    EVENT_GIFT_OF_THE_DOOMSAYER     = 1,  ///< 末日者礼物事件
    EVENT_WHIRLWIND                 = 2,  ///< 旋风斩事件
    EVENT_HEAL                      = 3,  ///< 治疗事件
    EVENT_SHADOW_WAVE               = 4,  ///< 暗影波事件(英雄模式)
    EVENT_ME_FIRST                  = 5,  ///< 我先来事件(索克雷萨互动)
    EVENT_SOCCOTHRATES_DEATH        = 6   ///< 索克雷萨死亡事件
};

/**
 * @struct boss_dalliah_the_doomsayer
 * @brief 末日预言者达莉亚Boss AI实现
 *
 * 继承自BossAI，实现达莉亚的战斗逻辑。
 * 达莉亚与附近的Boss索克雷萨有特殊互动，当达莉亚先被激活或血量低时，
 * 索克雷萨会说特定的台词。
 *
 * 战斗流程:
 * 1. 战斗开始时施放末日者礼物
 * 2. 周期性施放旋风斩
 * 3. 旋风斩后立即治疗自己
 * 4. 英雄模式额外施放暗影波
 * 5. 血量低于25%时索克雷萨会嘲讽
 */
struct boss_dalliah_the_doomsayer : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_dalliah_the_doomsayer(Creature* creature) : BossAI(creature, DATA_DALLIAH)
    {
        soccothratesTaunt = false;   // 索克雷萨是否已嘲讽
        soccothratesDeath = false;   // 索克雷萨是否已死亡
    }

    /**
     * @brief 重置Boss状态
     *
     * @调用时机 Boss脱离战斗时
     */
    void Reset() override
    {
        _Reset();
        soccothratesTaunt = false;   // 重置嘲讽标志
        soccothratesDeath = false;   // 重置死亡标志
    }

    /**
     * @brief Boss死亡时调用
     * @param killer 击杀者
     *
     * 如果索克雷萨还活着且不在战斗中，通知他
     * @调用时机 Boss死亡时
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);  // 说死亡台词

        // 如果索克雷萨还活着且不在战斗中，通知他
        if (Creature* soccothrates = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_SOCCOTHRATES)))
            if (soccothrates->IsAlive() && !soccothrates->IsInCombat())
                soccothrates->AI()->SetData(1, 1);  // 触发索克雷萨的反应
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     *
     * 调度所有技能事件并触发索克雷萨的互动
     * @调用时机 Boss进入战斗时
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_GIFT_OF_THE_DOOMSAYER, 1s, 4s);   // 末日者礼物
        events.ScheduleEvent(EVENT_WHIRLWIND, 7s, 9s);               // 旋风斩
        if (IsHeroic())
            events.ScheduleEvent(EVENT_SHADOW_WAVE, 11s, 16s);       // 英雄模式暗影波
        events.ScheduleEvent(EVENT_ME_FIRST, 6s);                    // 索克雷萨互动
        Talk(SAY_AGGRO);  // 说激活台词
    }

    /**
     * @brief 击杀单位时调用
     * @param victim 被击杀的单位
     */
    void KilledUnit(Unit* /*victim*/) override
    {
        Talk(SAY_SLAY);  // 说击杀台词
    }

    /**
     * @brief 设置数据
     * @param type 数据类型
     * @param data 数据值
     *
     * 接收索克雷萨死亡的通知
     * @调用时机 索克雷萨死亡时
     */
    void SetData(uint32 /*type*/, uint32 data) override
    {
        switch (data)
        {
            case 1:
                // 索克雷萨死亡，6秒后说台词
                events.ScheduleEvent(EVENT_SOCCOTHRATES_DEATH, 6s);
                soccothratesDeath = true;
                break;
            default:
                break;
        }
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 主AI更新循环，处理所有技能事件和索克雷萨互动
     * @调用时机 每个游戏循环tick
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
        {
            // 如果不在战斗中但索克雷萨已死亡，处理相关事件
            if (soccothratesDeath)
            {
                events.Update(diff);

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_SOCCOTHRATES_DEATH:
                            Talk(SAY_SOCCOTHRATES_DEATH);  // 说索克雷萨死亡相关台词
                            break;
                        default:
                            break;
                    }
                }
            }

            return;
        }

        events.Update(diff);

        // 如果正在施法，则等待
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_GIFT_OF_THE_DOOMSAYER:
                    // 施放末日者礼物
                    DoCastVictim(SPELL_GIFT_OF_THE_DOOMSAYER, true);
                    events.ScheduleEvent(EVENT_GIFT_OF_THE_DOOMSAYER, 16s, 21s);
                    break;
                case EVENT_WHIRLWIND:
                    // 施放旋风斩
                    DoCast(me, SPELL_WHIRLWIND);
                    Talk(SAY_WHIRLWIND);  // 说旋风斩台词
                    events.ScheduleEvent(EVENT_WHIRLWIND, 19s, 21s);
                    events.ScheduleEvent(EVENT_HEAL, 6s);  // 6秒后治疗
                    break;
                case EVENT_HEAL:
                    // 施放治疗
                    DoCast(me, SPELL_HEAL);
                    Talk(SAY_HEAL);  // 说治疗台词
                    break;
                case EVENT_SHADOW_WAVE:
                    // 英雄模式暗影波
                    DoCastVictim(SPELL_SHADOW_WAVE, true);
                    events.ScheduleEvent(EVENT_SHADOW_WAVE, 11s, 16s);
                    break;
                case EVENT_ME_FIRST:
                    // 让索克雷萨说我先来的台词
                    if (Creature* soccothrates = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_SOCCOTHRATES)))
                        if (soccothrates->IsAlive() && !soccothrates->IsInCombat())
                            soccothrates->AI()->Talk(SAY_AGGRO_DALLIAH_FIRST);
                    break;
                default:
                    break;
            }

            // 如果开始施法，则退出事件循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 血量低于25%时触发索克雷萨嘲讽
        if (HealthBelowPct(25) && !soccothratesTaunt)
        {
            if (Creature* soccothrates = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_SOCCOTHRATES)))
                soccothrates->AI()->Talk(SAY_DALLIAH_25_PERCENT);  // 索克雷萨嘲讽达莉亚
            soccothratesTaunt = true;
        }

        DoMeleeAttackIfReady();
    }

private:
    bool soccothratesTaunt;   ///< 索克雷萨是否已嘲讽(达莉亚血量低于25%时)
    bool soccothratesDeath;   ///< 索克雷萨是否已死亡(用于触发达莉亚的台词)
};

/**
 * @brief 注册末日预言者达莉亚Boss脚本
 *
 * 此函数由脚本系统在服务器启动时调用，注册达莉亚AI
 */
void AddSC_boss_dalliah_the_doomsayer()
{
    RegisterArcatrazCreatureAI(boss_dalliah_the_doomsayer);
}
