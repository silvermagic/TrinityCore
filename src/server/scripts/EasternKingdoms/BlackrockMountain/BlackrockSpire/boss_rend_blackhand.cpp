/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the option) any later version.
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
 * @file boss_rend_blackhand.cpp
 * @brief 黑石塔上层首领 - 大酋长雷德·黑手 (Warchief Rend Blackhand) AI 实现
 *
 * 本模块实现了黑石塔上层副本中雷德·黑手的战斗AI逻辑。
 * 雷德·黑手是一个复杂的多阶段战斗首领，包含事件序列、援军波次和骑乘战斗。
 *
 * 主要功能：
 * - 实现完整的事件序列流程
 * - 管理6波援军的生成和战斗
 * - 实现与维克多·奈法里奥斯的交互
 * - 处理骑乘坐骑盖斯的战斗机制
 * - 实现旋风斩、顺劈斩和致死打击等战斗技能
 * - 管理传送和路径移动逻辑
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "GameObject.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 *
 * 定义雷德·黑手使用的所有法术ID
 */
enum Spells
{
    SPELL_WHIRLWIND                 = 13736, ///< 旋风斩 - AOE近战伤害
    SPELL_CLEAVE                    = 15284, ///< 顺劈斩 - 对目标和附近敌人造成伤害
    SPELL_MORTAL_STRIKE             = 16856, ///< 致死打击 - 高伤害并降低治疗效果
    SPELL_FRENZY                    = 8269,  ///< 狂暴 - 提高攻击速度和伤害（未使用）
    SPELL_KNOCKDOWN                 = 13360  ///< 击倒 - 在盖斯战斗期间生成时施放
};

/**
 * @brief 对话文本枚举
 *
 * 定义雷德·黑手和维克多·奈法里奥斯使用的文本ID
 */
enum Says
{
    // 雷德·黑手
    SAY_BLACKHAND_1                 = 0,  ///< 雷德对话1
    SAY_BLACKHAND_2                 = 1,  ///< 雷德对话2
    EMOTE_BLACKHAND_DISMOUNT        = 2,  ///< 雷德下马表情

    // 维克多·奈法里奥斯
    SAY_NEFARIUS_0                  = 0,  ///< 奈法里乌斯对话0
    SAY_NEFARIUS_1                  = 1,  ///< 奈法里乌斯对话1
    SAY_NEFARIUS_2                  = 2,  ///< 奈法里乌斯对话2
    SAY_NEFARIUS_3                  = 3,  ///< 奈法里乌斯对话3
    SAY_NEFARIUS_4                  = 4,  ///< 奈法里乌斯对话4
    SAY_NEFARIUS_5                  = 5,  ///< 奈法里乌斯对话5
    SAY_NEFARIUS_6                  = 6,  ///< 奈法里乌斯对话6
    SAY_NEFARIUS_7                  = 7,  ///< 奈法里乌斯对话7
    SAY_NEFARIUS_8                  = 8,  ///< 奈法里乌斯对话8
    SAY_NEFARIUS_9                  = 9,  ///< 奈法里乌斯对话9
};

/**
 * @brief 援军生物ID枚举
 *
 * 定义援军波次中的生物ID
 */
enum Adds
{
    NPC_CHROMATIC_WHELP             = 10442,  ///< 彩色幼龙
    NPC_CHROMATIC_DRAGONSPAWN       = 10447,  ///< 彩色龙人
    NPC_BLACKHAND_DRAGON_HANDLER    = 10742   ///< 黑手驯龙者
};

/**
 * @brief 杂项常量枚举
 *
 * 定义路径ID和其他常量
 */
enum Misc
{
    NEFARIUS_PATH_1                 = 1379670,  ///< 奈法里乌斯路径1
    NEFARIUS_PATH_2                 = 1379671,  ///< 奈法里乌斯路径2
    NEFARIUS_PATH_3                 = 1379672,  ///< 奈法里乌斯路径3
    REND_PATH_1                     = 1379680,  ///< 雷德路径1
    REND_PATH_2                     = 1379681,  ///< 雷德路径2
};

/*
 * 注释掉的波次生成数据结构
 * 原始代码中定义了各波次的具体生成位置和生物信息
 */
/*
struct Wave
{
    uint32 entry;
    float  x_pos;
    float  y_pos;
    float  z_pos;
    float  o_pos;
};

static Wave Wave2[]= // 22 sec
{
    { 10447, 209.8637f, -428.2729f, 110.9877f, 0.6632251f },
    { 10442, 209.3122f, -430.8724f, 110.9814f, 2.9147f    },
    { 10442, 211.3309f, -425.9111f, 111.0006f, 1.727876f  }
};

static Wave Wave3[]= // 60 sec
{
    { 10742, 208.6493f, -424.5787f, 110.9872f, 5.8294f    },
    { 10447, 203.9482f, -428.9446f, 110.982f,  4.677482f  },
    { 10442, 203.3441f, -426.8668f, 110.9772f, 4.712389f  },
    { 10442, 206.3079f, -424.7509f, 110.9943f, 4.08407f   }
};

static Wave Wave4[]= // 49 sec
{
    { 10742, 212.3541f, -412.6826f, 111.0352f, 5.88176f   },
    { 10447, 212.5754f, -410.2841f, 111.0296f, 2.740167f  },
    { 10442, 212.3449f, -414.8659f, 111.0348f, 2.356194f  },
    { 10442, 210.6568f, -412.1552f, 111.0124f, 0.9773844f }
};

static Wave Wave5[]= // 60 sec
{
    { 10742, 210.2188f, -410.6686f, 111.0211f, 5.8294f    },
    { 10447, 209.4078f, -414.13f,   111.0264f, 4.677482f  },
    { 10442, 208.0858f, -409.3145f, 111.0118f, 4.642576f  },
    { 10442, 207.9811f, -413.0728f, 111.0098f, 5.288348f  },
    { 10442, 208.0854f, -412.1505f, 111.0057f, 4.08407f   }
};

static Wave Wave6[]= // 27 sec
{
    { 10742, 213.9138f, -426.512f,  111.0013f, 3.316126f  },
    { 10447, 213.7121f, -429.8102f, 110.9888f, 1.413717f  },
    { 10447, 213.7157f, -424.4268f, 111.009f,  3.001966f  },
    { 10442, 210.8935f, -423.913f,  111.0125f, 5.969026f  },
    { 10442, 212.2642f, -430.7648f, 110.9807f, 5.934119f  }
};
*/

/// 盖斯生成位置
Position const GythLoc =      { 211.762f,  -397.5885f, 111.1817f,  4.747295f   };
/// 传送位置1
Position const Teleport1Loc = { 194.2993f, -474.0814f, 121.4505f, -0.01225555f };
/// 传送位置2
Position const Teleport2Loc = { 216.485f,  -434.93f,   110.888f,  -0.01225555f };

/**
 * @brief 事件ID枚举
 *
 * 定义整个事件流程和战斗中使用的所有事件ID
 */
enum Events
{
    EVENT_START_1                   = 1,  ///< 开始事件1
    EVENT_START_2                   = 2,  ///< 开始事件2
    EVENT_START_3                   = 3,  ///< 开始事件3
    EVENT_TURN_TO_REND              = 4,  ///< 转向雷德
    EVENT_TURN_TO_PLAYER            = 5,  ///< 转向玩家
    EVENT_TURN_TO_FACING_1          = 6,  ///< 转向朝向1
    EVENT_TURN_TO_FACING_2          = 7,  ///< 转向朝向2
    EVENT_TURN_TO_FACING_3          = 8,  ///< 转向朝向3
    EVENT_WAVE_1                    = 9,  ///< 第1波援军
    EVENT_WAVE_2                    = 10, ///< 第2波援军
    EVENT_WAVE_3                    = 11, ///< 第3波援军
    EVENT_WAVE_4                    = 12, ///< 第4波援军
    EVENT_WAVE_5                    = 13, ///< 第5波援军
    EVENT_WAVE_6                    = 14, ///< 第6波援军
    EVENT_WAVES_TEXT_1              = 15, ///< 援军文本1
    EVENT_WAVES_TEXT_2              = 16, ///< 援军文本2
    EVENT_WAVES_TEXT_3              = 17, ///< 援军文本3
    EVENT_WAVES_TEXT_4              = 18, ///< 援军文本4
    EVENT_WAVES_TEXT_5              = 19, ///< 援军文本5
    EVENT_WAVES_COMPLETE_TEXT_1     = 20, ///< 援军完成文本1
    EVENT_WAVES_COMPLETE_TEXT_2     = 21, ///< 援军完成文本2
    EVENT_WAVES_COMPLETE_TEXT_3     = 22, ///< 援军完成文本3
    EVENT_WAVES_EMOTE_1             = 23, ///< 援军表情1
    EVENT_WAVES_EMOTE_2             = 24, ///< 援军表情2
    EVENT_PATH_REND                 = 25, ///< 雷德路径
    EVENT_PATH_NEFARIUS             = 26, ///< 奈法里乌斯路径
    EVENT_TELEPORT_1                = 27, ///< 传送1
    EVENT_TELEPORT_2                = 28, ///< 传送2
    EVENT_WHIRLWIND                 = 29, ///< 旋风斩
    EVENT_CLEAVE                    = 30, ///< 顺劈斩
    EVENT_MORTAL_STRIKE             = 31, ///< 致死打击
};

/**
 * @brief 大酋长雷德·黑手 AI 结构体
 *
 * 继承自 BossAI 基类，实现雷德·黑手的完整战斗AI。
 * 负责管理复杂的事件序列、援军波次和战斗技能。
 *
 * 战斗流程：
 * 1. 玩家触发黑石体育场区域
 * 2. 维克多·奈法里乌斯开始对话序列
 * 3. 开启6波援军攻击玩家
 * 4. 所有援军被击败后，雷德离开并召唤盖斯
 * 5. 雷德骑乘盖斯与玩家战斗
 * 6. 盖斯被击败后，雷德下马与玩家进行最后的战斗
 */
struct boss_rend_blackhand : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 关联的生物对象指针
     *
     * 初始化 BossAI 基类，关联首领数据为 DATA_WARCHIEF_REND_BLACKHAND，
     * 并初始化成员变量
     */
    boss_rend_blackhand(Creature* creature) : BossAI(creature, DATA_WARCHIEF_REND_BLACKHAND)
    {
        gythEvent = false;
        victorGUID.Clear();
        portcullisGUID.Clear();
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
        gythEvent = false;
        victorGUID.Clear();
        portcullisGUID.Clear();
    }

    /**
     * @brief 进入战斗回调
     * @param who 触发战斗的单位（通常是第一个攻击者）
     *
     * 当雷德进入战斗状态时调用（下马后）。
     * 安排战斗技能的施放时机。
     *
     * @note 调用时机：雷德下马后主动攻击玩家
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_WHIRLWIND, 13s, 15s);
        events.ScheduleEvent(EVENT_CLEAVE, 15s, 17s);
        events.ScheduleEvent(EVENT_MORTAL_STRIKE, 17s, 19s);
    }

    /**
     * @brief 被召唤回调
     * @param summoner 召唤者对象（未使用）
     *
     * 当雷德被召唤时调用。
     * 移除对玩家的免疫状态并立即进入战斗。
     *
     * @note 调用时机：雷德被召唤生成（下马后）
     */
    void IsSummonedBy(WorldObject* /*summoner*/) override
    {
        me->SetImmuneToPC(false);
        DoZoneInCombat();
    }

    /**
     * @brief 死亡回调
     * @param killer 击杀首领的单位（未使用）
     *
     * 当雷德死亡时调用。
     * 通知维克多·奈法里乌斯首领已被击败。
     *
     * @note 调用时机：雷德生命值降为0
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        if (Creature* victor = me->FindNearestCreature(NPC_LORD_VICTOR_NEFARIUS, 75.0f, true))
            victor->AI()->SetData(1, 2);
    }

    /**
     * @brief 设置数据回调
     * @param type 数据类型
     * @param data 数据值
     *
     * 用于从外部触发特定事件。
     * 当玩家进入黑石体育场区域时触发事件序列。
     *
     * @note 调用时机：玩家触发区域触发器
     */
    void SetData(uint32 type, uint32 data) override
    {
        if (type == AREATRIGGER && data == AREATRIGGER_BLACKROCK_STADIUM)
        {
            if (!gythEvent)
            {
                gythEvent = true;

                // 查找维克多·奈法里乌斯
                if (Creature* victor = me->FindNearestCreature(NPC_LORD_VICTOR_NEFARIUS, 5.0f, true))
                    victorGUID = victor->GetGUID();

                // 查找大门
                if (GameObject* portcullis = me->FindNearestGameObject(GO_DR_PORTCULLIS, 50.0f))
                    portcullisGUID = portcullis->GetGUID();

                // 开始事件序列
                events.ScheduleEvent(EVENT_TURN_TO_PLAYER, 0s);
                events.ScheduleEvent(EVENT_START_1, 1s);
            }
        }
    }

    /**
     * @brief 移动信息回调
     * @param type 移动类型
     * @param id 路径点ID
     *
     * 当雷德到达特定路径点时调用。
     * 处理传送和召唤盖斯的逻辑。
     *
     * @note 调用时机：雷德沿路径移动到达特定点
     */
    void MovementInform(uint32 type, uint32 id) override
    {
        if (type == WAYPOINT_MOTION_TYPE)
        {
            switch (id)
            {
                case 5:
                    // 到达路径点5，传送到第一个位置
                    events.ScheduleEvent(EVENT_TELEPORT_1, 2s);
                    break;
                case 11:
                    // 到达路径点11，召唤盖斯并消失
                    if (Creature* gyth = me->FindNearestCreature(NPC_GYTH, 10.0f, true))
                        gyth->AI()->SetData(1, 1);
                    me->DespawnOrUnsummon(1s, 7_days);
                    break;
            }
        }
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 每个服务器tick调用一次，处理AI的主逻辑循环。
     * 分为事件序列阶段和战斗阶段两个部分。
     *
     * 事件序列阶段：
     * - 处理对话序列
     * - 管理援军波次
     * - 处理传送和路径移动
     * - 控制大门开关
     *
     * 战斗阶段：
     * - 处理战斗技能的施放
     * - 管理旋风斩、顺劈斩和致死打击
     *
     * @note 性能注意事项：避免在此函数中进行耗时操作，保持高效执行
     */
    void UpdateAI(uint32 diff) override
    {
        // 处理盖斯事件序列
        if (gythEvent)
        {
            events.Update(diff);

            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_START_1:
                        // 维克多·奈法里乌斯开始对话
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            victor->AI()->Talk(SAY_NEFARIUS_0);
                        events.ScheduleEvent(EVENT_START_2, 4s);
                        break;
                    case EVENT_START_2:
                        // 维克多指向玩家
                        events.ScheduleEvent(EVENT_TURN_TO_PLAYER, 0s);
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            victor->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
                        events.ScheduleEvent(EVENT_START_3, 4s);
                        break;
                    case EVENT_START_3:
                        // 维克多继续对话并开始第一波援军
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            victor->AI()->Talk(SAY_NEFARIUS_1);
                        events.ScheduleEvent(EVENT_WAVE_1, 2s);
                        events.ScheduleEvent(EVENT_TURN_TO_REND, 4s);
                        events.ScheduleEvent(EVENT_WAVES_TEXT_1, 20s);
                        break;
                    case EVENT_TURN_TO_REND:
                        // 维克多转向雷德
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                        {
                            victor->SetFacingToObject(me);
                            victor->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
                        }
                        break;
                    case EVENT_TURN_TO_PLAYER:
                        // 维克多转向玩家
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            if (Unit* player = victor->SelectNearestPlayer(60.0f))
                                victor->SetFacingToObject(player);
                        break;
                    case EVENT_TURN_TO_FACING_1:
                        // 维克多转向固定朝向
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            victor->SetFacingTo(1.518436f);
                        break;
                    case EVENT_TURN_TO_FACING_2:
                        // 雷德转向固定朝向
                        me->SetFacingTo(1.658063f);
                        break;
                    case EVENT_TURN_TO_FACING_3:
                        // 雷德转向固定朝向
                        me->SetFacingTo(1.500983f);
                        break;
                    case EVENT_WAVES_EMOTE_1:
                        // 维克多播放疑问表情
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            victor->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
                        break;
                    case EVENT_WAVES_EMOTE_2:
                        // 雷德播放咆哮表情
                        me->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
                        break;
                    case EVENT_WAVES_TEXT_1:
                        // 第一波援军对话
                        events.ScheduleEvent(EVENT_TURN_TO_PLAYER, 0s);
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            victor->AI()->Talk(SAY_NEFARIUS_2);
                        me->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
                        events.ScheduleEvent(EVENT_TURN_TO_FACING_1, 4s);
                        events.ScheduleEvent(EVENT_WAVES_EMOTE_1, 5s);
                        events.ScheduleEvent(EVENT_WAVE_2, 2s);
                        events.ScheduleEvent(EVENT_WAVES_TEXT_2, 20s);
                        break;
                    case EVENT_WAVES_TEXT_2:
                        // 第二波援军对话
                        events.ScheduleEvent(EVENT_TURN_TO_PLAYER, 0s);
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            victor->AI()->Talk(SAY_NEFARIUS_3);
                        events.ScheduleEvent(EVENT_TURN_TO_FACING_1, 4s);
                        events.ScheduleEvent(EVENT_WAVE_3, 2s);
                        events.ScheduleEvent(EVENT_WAVES_TEXT_3, 20s);
                        break;
                    case EVENT_WAVES_TEXT_3:
                        // 第三波援军对话
                        events.ScheduleEvent(EVENT_TURN_TO_PLAYER, 0s);
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            victor->AI()->Talk(SAY_NEFARIUS_4);
                        events.ScheduleEvent(EVENT_TURN_TO_FACING_1, 4s);
                        events.ScheduleEvent(EVENT_WAVE_4, 2s);
                        events.ScheduleEvent(EVENT_WAVES_TEXT_4, 20s);
                        break;
                    case EVENT_WAVES_TEXT_4:
                        // 第四波援军对话，雷德开始说话
                        Talk(SAY_BLACKHAND_1);
                        events.ScheduleEvent(EVENT_WAVES_EMOTE_2, 4s);
                        events.ScheduleEvent(EVENT_TURN_TO_FACING_3, 8s);
                        events.ScheduleEvent(EVENT_WAVE_5, 2s);
                        events.ScheduleEvent(EVENT_WAVES_TEXT_5, 20s);
                        break;
                    case EVENT_WAVES_TEXT_5:
                        // 第五波援军对话
                        events.ScheduleEvent(EVENT_TURN_TO_PLAYER, 0s);
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            victor->AI()->Talk(SAY_NEFARIUS_5);
                        events.ScheduleEvent(EVENT_TURN_TO_FACING_1, 4s);
                        events.ScheduleEvent(EVENT_WAVE_6, 2s);
                        events.ScheduleEvent(EVENT_WAVES_COMPLETE_TEXT_1, 20s);
                        break;
                    case EVENT_WAVES_COMPLETE_TEXT_1:
                        // 第六波援军完成对话
                        events.ScheduleEvent(EVENT_TURN_TO_PLAYER, 0s);
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            victor->AI()->Talk(SAY_NEFARIUS_6);
                        events.ScheduleEvent(EVENT_TURN_TO_FACING_1, 4s);
                        events.ScheduleEvent(EVENT_WAVES_COMPLETE_TEXT_2, 13s);
                        break;
                    case EVENT_WAVES_COMPLETE_TEXT_2:
                        // 所有援军完成对话，准备召唤盖斯
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            victor->AI()->Talk(SAY_NEFARIUS_7);
                        Talk(SAY_BLACKHAND_2);
                        events.ScheduleEvent(EVENT_PATH_REND, 1s);
                        events.ScheduleEvent(EVENT_WAVES_COMPLETE_TEXT_3, 4s);
                        break;
                    case EVENT_WAVES_COMPLETE_TEXT_3:
                        // 最后对话，维克多和雷德离开
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            victor->AI()->Talk(SAY_NEFARIUS_8);
                        events.ScheduleEvent(EVENT_PATH_NEFARIUS, 1s);
                        events.ScheduleEvent(EVENT_PATH_REND, 1s);
                        break;
                    case EVENT_PATH_NEFARIUS:
                        // 维克多开始移动路径
                        if (Creature* victor = ObjectAccessor::GetCreature(*me, victorGUID))
                            victor->GetMotionMaster()->MovePath(NEFARIUS_PATH_1, true);
                        break;
                    case EVENT_PATH_REND:
                        // 雷德开始移动路径
                        me->GetMotionMaster()->MovePath(REND_PATH_1, false);
                        break;
                    case EVENT_TELEPORT_1:
                        // 雷德传送到第一个位置
                        me->NearTeleportTo(194.2993f, -474.0814f, 121.4505f, -0.01225555f);
                        events.ScheduleEvent(EVENT_TELEPORT_2, 50s);
                        break;
                    case EVENT_TELEPORT_2:
                        // 雷德传送到第二个位置并召唤盖斯
                        me->NearTeleportTo(216.485f, -434.93f, 110.888f, -0.01225555f);
                        me->SummonCreature(NPC_GYTH, 211.762f, -397.5885f, 111.1817f, 4.747295f);
                        break;
                    case EVENT_WAVE_1:
                        // 第一波援军：打开大门
                        if (GameObject* portcullis = ObjectAccessor::GetGameObject(*me, portcullisGUID))
                            portcullis->UseDoorOrButton();
                        // 移动波次生物
                        break;
                    case EVENT_WAVE_2:
                        // 第二波援军：生成援军
                        // spawn wave
                        if (GameObject* portcullis = ObjectAccessor::GetGameObject(*me, portcullisGUID))
                            portcullis->UseDoorOrButton();
                        // 移动波次生物
                        break;
                    case EVENT_WAVE_3:
                        // 第三波援军：生成援军
                        // spawn wave
                        if (GameObject* portcullis = ObjectAccessor::GetGameObject(*me, portcullisGUID))
                            portcullis->UseDoorOrButton();
                        // 移动波次生物
                        break;
                    case EVENT_WAVE_4:
                        // 第四波援军：生成援军
                        // spawn wave
                        if (GameObject* portcullis = ObjectAccessor::GetGameObject(*me, portcullisGUID))
                            portcullis->UseDoorOrButton();
                        // 移动波次生物
                        break;
                    case EVENT_WAVE_5:
                        // 第五波援军：生成援军
                        // spawn wave
                        if (GameObject* portcullis = ObjectAccessor::GetGameObject(*me, portcullisGUID))
                            portcullis->UseDoorOrButton();
                        // 移动波次生物
                        break;
                    case EVENT_WAVE_6:
                        // 第六波援军：生成援军
                        // spawn wave
                        if (GameObject* portcullis = ObjectAccessor::GetGameObject(*me, portcullisGUID))
                            portcullis->UseDoorOrButton();
                        // 移动波次生物
                        break;
                    default:
                        break;
                }
            }
        }

        // 战斗阶段处理
        if (!UpdateVictim())
            return;

        events.Update(diff);

        // 如果正在施法，则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有到期事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_WHIRLWIND:
                    // 施放旋风斩
                    DoCast(SPELL_WHIRLWIND);
                    events.ScheduleEvent(EVENT_WHIRLWIND, 13s, 18s);
                    break;
                case EVENT_CLEAVE:
                    // 对当前目标施放顺劈斩
                    DoCastVictim(SPELL_CLEAVE);
                    events.ScheduleEvent(EVENT_CLEAVE, 10s, 14s);
                    break;
                case EVENT_MORTAL_STRIKE:
                    // 对当前目标施放致死打击
                    DoCastVictim(SPELL_MORTAL_STRIKE);
                    events.ScheduleEvent(EVENT_MORTAL_STRIKE, 14s, 16s);
                    break;
            }

            // 如果在事件处理过程中开始施法，则退出循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 如果没有在施法且准备就绪，进行近战攻击
        DoMeleeAttackIfReady();
    }

    private:
        bool   gythEvent;        ///< 盖斯事件是否已触发标记
        ObjectGuid victorGUID;   ///< 维克多·奈法里乌斯的GUID
        ObjectGuid portcullisGUID; ///< 大门的GUID
};

/**
 * @brief 注册大酋长雷德·黑手 AI
 *
 * 此函数将大酋长雷德·黑手的AI注册到脚本系统中，
 * 使游戏服务器能够正确加载和运行该首领的AI逻辑。
 */
void AddSC_boss_rend_blackhand()
{
    RegisterBlackrockSpireCreatureAI(boss_rend_blackhand);
}
