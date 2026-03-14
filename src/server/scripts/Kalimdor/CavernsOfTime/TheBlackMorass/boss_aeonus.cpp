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
 * @file boss_aeonus.cpp
 * @brief 黑色沼泽副本BOSS——埃欧努斯(Aeonus)的AI脚本实现
 *
 * 模块职责:
 * - 实现BOSS埃欧努斯的AI行为逻辑
 * - 管理BOSS的技能释放时间轴
 * - 处理BOSS与时间守护者(Time Keeper)的交互
 * - 控制副本事件流程和世界状态更新
 *
 * BOSS概述:
 * 埃欧努斯是黑色沼泽副本的最终BOSS,是一条时间龙。
 * 玩家需要保护麦迪文打开黑暗传送门,同时击败从时间裂隙中出现的敌人。
 *
 * 主要技能:
 * - Sand Breath(沙尘吐息): 对面前敌人造成自然伤害
 * - Time Stop(时间停止): 使目标时间停止
 * - Enrage(狂暴): 提高攻击速度和伤害
 * - Cleave(顺劈斩): 对前方敌人造成物理伤害
 *
 * 完成度: 80%
 * 备注: 部分技能尚未实现
 * 分类: 时光之穴 - 黑色沼泽
 */

/*
Name: Boss_Aeonus
%Complete: 80
Comment: Some spells not implemented
Category: Caverns of Time, The Dark Portal
*/

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "the_black_morass.h"

/**
 * @enum Enums
 * @brief BOSS埃欧努斯使用的文本ID和法术ID枚举
 */
enum Enums
{
    // BOSS文本ID
    SAY_ENTER           = 0,    ///< 进入战斗时的对话
    SAY_AGGRO           = 1,    ///< 激活时的对话
    SAY_BANISH          = 2,    ///< 放逐时间守护者时的对话
    SAY_SLAY            = 3,    ///< 击杀玩家时的对话
    SAY_DEATH           = 4,    ///< 死亡时的对话
    EMOTE_FRENZY        = 5,    ///< 狂暴表情

    // BOSS法术ID
    SPELL_CLEAVE        = 40504,    ///< 顺劈斩 - 对前方敌人造成物理伤害
    SPELL_TIME_STOP     = 31422,    ///< 时间停止 - 使目标时间停止,无法行动
    SPELL_ENRAGE        = 37605,    ///< 狂暴 - 提高攻击速度和伤害
    SPELL_SAND_BREATH   = 31473,    ///< 沙尘吐息(普通难度) - 对面前敌人造成自然伤害
    H_SPELL_SAND_BREATH = 39049     ///< 沙尘吐息(英雄难度) - 英雄难度版本
};

/**
 * @enum Events
 * @brief BOSS埃欧努斯的事件类型枚举,用于事件调度系统
 */
enum Events
{
    EVENT_SANDBREATH    = 1,    ///< 沙尘吐息技能事件
    EVENT_TIMESTOP      = 2,    ///< 时间停止技能事件
    EVENT_FRENZY        = 3     ///< 狂暴技能事件
};

/**
 * @struct boss_aeonus
 * @brief BOSS埃欧努斯的AI实现类
 *
 * 继承自BossAI基类,实现埃欧努斯的所有战斗行为。
 * 埃欧努斯是黑色沼泽副本的最终BOSS,会主动攻击时间守护者并最终与玩家战斗。
 *
 * 战斗流程:
 * 1. BOSS会定期使用沙尘吐息攻击当前目标
 * 2. 使用时间停止技能控制玩家
 * 3. 定期进入狂暴状态提高伤害
 * 4. 死亡后完成副本事件
 */
struct boss_aeonus : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_aeonus(Creature* creature) : BossAI(creature, TYPE_AEONUS) { }

    /**
     * @brief 重置BOSS状态
     *
     * 当BOSS脱离战斗或被重置时调用。
     * 清除所有仇恨和事件状态。
     *
     * @调用时机: BOSS脱离战斗、重置副本、BOSS被重置时
     */
    void Reset() override { }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标(通常是最先攻击BOSS的玩家)
     *
     * 初始化战斗事件调度:
     * - 沙尘吐息: 15-30秒后首次施放
     * - 时间停止: 10-15秒后首次施放
     * - 狂暴: 30-45秒后首次施放
     *
     * @调用时机: BOSS被玩家攻击并成功建立仇恨时
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        // 调度沙尘吐息技能,15-30秒后首次施放
        events.ScheduleEvent(EVENT_SANDBREATH, 15s, 30s);
        // 调度时间停止技能,10-15秒后首次施放
        events.ScheduleEvent(EVENT_TIMESTOP, 10s, 15s);
        // 调度狂暴技能,30-45秒后首次施放
        events.ScheduleEvent(EVENT_FRENZY, 30s, 45s);

        // 播放进入战斗的对话
        Talk(SAY_AGGRO);
    }

    /**
     * @brief 视线范围内检测函数
     * @param who 进入视线范围的单位
     *
     * 特殊功能:检测并消灭时间守护者(Time Keeper)
     * 当时间守护者进入BOSS的20码范围内时,BOSS会直接消灭它。
     * 这是副本机制的一部分,时间守护者可以帮助玩家防守,
     * 但BOSS会优先消灭这些助手。
     *
     * @调用时机: 每个游戏帧,当有单位进入BOSS视线范围时
     * @性能注意: 该函数频繁调用,应避免复杂计算
     */
    void MoveInLineOfSight(Unit* who) override

    {
        // 检测是否为时间守护者NPC
        if (who->GetTypeId() == TYPEID_UNIT && who->GetEntry() == NPC_TIME_KEEPER)
        {
            // 如果时间守护者在20码范围内
            if (me->IsWithinDistInMap(who, 20.0f))
            {
                // 播放放逐对话
                Talk(SAY_BANISH);
                // 直接对时间守护者造成其生命值上限的伤害,秒杀
                Unit::DealDamage(me, who, who->GetHealth(), nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
            }
        }

        // 调用父类的视线检测函数,继续正常行为
        ScriptedAI::MoveInLineOfSight(who);
    }

    /**
     * @brief BOSS死亡时调用
     * @param killer 击杀BOSS的单位(可为nullptr)
     *
     * 完成副本事件:
     * - 标记时间裂隙事件为完成
     * - 标记麦迪文事件为完成(临时解决方案)
     * - 播放死亡对话
     *
     * @调用时机: BOSS生命值降至0时
     */
    void JustDied(Unit* /*killer*/) override
    {
        // 播放死亡对话
        Talk(SAY_DEATH);

        // 标记时间裂隙事件为完成
        instance->SetData(TYPE_RIFT, DONE);
        // 标记麦迪文事件为完成
        // FIXME: 这应该在后续移除,应该在更合适的地方处理
        instance->SetData(TYPE_MEDIVH, DONE);
    }

    /**
     * @brief 击杀单位时调用
     * @param who 被击杀的单位
     *
     * 当BOSS击杀玩家时播放击杀对话。
     * 只在击杀玩家时触发,不对NPC触发。
     *
     * @调用时机: BOSS击杀任何单位时
     */
    void KilledUnit(Unit* who) override
    {
        // 只对玩家播放击杀对话
        if (who->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_SLAY);
    }

    /**
     * @brief AI更新函数,每个游戏帧调用
     * @param diff 距离上次更新的时间间隔(毫秒)
     *
     * 主要逻辑:
     * 1. 检查是否有有效目标,无目标则返回
     * 2. 更新事件调度器
     * 3. 如果正在施法则等待
     * 4. 处理事件队列:
     *    - 沙尘吐息: 对当前目标施放
     *    - 时间停止: 对当前目标施放
     *    - 狂暴: 对自己施放并播放表情
     * 5. 执行近战攻击
     *
     * @调用时机: 每个游戏帧,频率取决于服务器帧率(通常约50-100ms)
     * @性能注意: 此函数每帧调用,应避免复杂计算,保持高效
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果没有有效目标,直接返回
        if (!UpdateVictim())
            return;

        // 更新事件调度器的时间
        events.Update(diff);

        // 如果正在施法,暂停其他动作
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有待执行的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SANDBREATH:
                    // 对当前目标施放沙尘吐息
                    DoCastVictim(SPELL_SAND_BREATH);
                    // 调度下一次沙尘吐息,15-25秒后
                    events.ScheduleEvent(EVENT_SANDBREATH, 15s, 25s);
                    break;
                case EVENT_TIMESTOP:
                    // 对当前目标施放时间停止
                    DoCastVictim(SPELL_TIME_STOP);
                    // 调度下一次时间停止,20-35秒后
                    events.ScheduleEvent(EVENT_TIMESTOP, 20s, 35s);
                    break;
                case EVENT_FRENZY:
                    // 播放狂暴表情
                     Talk(EMOTE_FRENZY);
                     // 对自己施放狂暴增益
                     DoCast(me, SPELL_ENRAGE);
                    // 调度下一次狂暴,20-35秒后
                    events.ScheduleEvent(EVENT_FRENZY, 20s, 35s);
                    break;
                default:
                    break;
            }

            // 如果事件处理后正在施法,暂停后续事件处理
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 如果准备好进行近战攻击,则执行
        DoMeleeAttackIfReady();
    }
};

/**
 * @brief 注册BOSS埃欧努斯的AI脚本
 *
 * 此函数由脚本系统在服务器启动时调用,用于注册BOSS的AI。
 * 使用RegisterBlackMorassCreatureAI宏将boss_aeonus AI注册到脚本系统。
 *
 * @调用时机: 服务器启动时,脚本加载阶段
 */
void AddSC_boss_aeonus()
{
    RegisterBlackMorassCreatureAI(boss_aeonus);
}
