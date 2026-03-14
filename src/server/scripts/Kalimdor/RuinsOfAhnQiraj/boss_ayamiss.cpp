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
 * @file boss_ayamiss.cpp
 * @brief 安其拉废墟BOSS阿亚米斯（Ayamiss the Hunter）的AI脚本
 *
 * 本模块实现了安其拉废墟第三个BOSS阿亚米斯的战斗逻辑：
 * - 空中阶段：BOSS在空中飞行，无法近战攻击，使用远程技能
 * - 地面阶段：当血量低于70%时降落，开始近战攻击
 * - 召唤机制：周期性召唤黄蜂和幼虫
 * - 瘫痪机制：随机瘫痪玩家并将其传送到祭坛喂食幼虫
 * - 狂暴机制：血量低于20%时进入狂暴状态
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ruins_of_ahnqiraj.h"
#include "ScriptedCreature.h"

/**
 * @brief 法术ID枚举
 */
enum Spells
{
    SPELL_STINGER_SPRAY         =  25749,  ///< 蛰刺喷射：对前方锥形区域造成自然伤害
    SPELL_POISON_STINGER        =  25748,  ///< 毒刺：对目标造成自然伤害并施加毒药效果
    SPELL_PARALYZE              =  25725,  ///< 瘫痪：使目标瘫痪，无法移动和行动
    SPELL_TRASH                 =  3391,   ///< 猛击：近战攻击技能
    SPELL_FRENZY                =  8269,   ///< 狂暴：提高攻击速度
    SPELL_LASH                  =  25852,  ///< 鞭挞：近战范围攻击
    SPELL_FEED                  =  25721   ///< 喂食：幼虫对瘫痪目标的技能
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_STINGER_SPRAY         = 1,  ///< 蛰刺喷射事件
    EVENT_POISON_STINGER        = 2,  ///< 毒刺事件
    EVENT_SUMMON_SWARMER        = 3,  ///< 召唤黄蜂事件
    EVENT_SWARMER_ATTACK        = 4,  ///< 黄蜂攻击事件
    EVENT_PARALYZE              = 5,  ///< 瘫痪事件
    EVENT_LASH                  = 6,  ///< 鞭挞事件
    EVENT_TRASH                 = 7   ///< 猛击事件
};

/**
 * @brief 表情文本枚举
 */
enum Emotes
{
    EMOTE_FRENZY                =  0   ///< 狂暴表情
};

/**
 * @brief 战斗阶段枚举
 */
enum Phases
{
    PHASE_AIR                   = 0,  ///< 空中阶段：BOSS在空中飞行
    PHASE_GROUND                = 1   ///< 地面阶段：BOSS降落到地面
};

/**
 * @brief 移动点ID枚举
 */
enum Points
{
    POINT_AIR                   = 0,  ///< 空中位置点
    POINT_GROUND                = 1,  ///< 地面位置点
    POINT_PARALYZE              = 2   ///< 瘫痪祭坛位置点
};

/// @brief 阿亚米斯空中飞行位置
const Position AyamissAirPos =  { -9689.292f, 1547.912f, 48.02729f, 0.0f };
/// @brief 祭坛位置（用于瘫痪玩家喂食）
const Position AltarPos =       { -9717.18f, 1517.72f, 27.4677f, 0.0f };
/// @todo 这些位置可能不正确，取自SD2
/// @brief 黄蜂生成位置
const Position SwarmerPos =     { -9647.352f, 1578.062f, 55.32f, 0.0f };
/// @brief 幼虫生成位置（两个可能的生成点）
const Position LarvaPos[2] =
{
    { -9674.4707f, 1528.4133f, 22.457f, 0.0f },
    { -9701.6005f, 1566.9993f, 24.118f, 0.0f }
};

/**
 * @class boss_ayamiss
 * @brief 阿亚米斯BOSS脚本类
 *
 * 负责注册和管理阿亚米斯BOSS的AI行为
 */
class boss_ayamiss : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_ayamiss() : CreatureScript("boss_ayamiss") { }

        /**
         * @class boss_ayamissAI
         * @brief 阿亚米斯BOSS的AI实现类
         *
         * 实现了阿亚米斯的两阶段战斗逻辑：
         * 1. 空中阶段：使用毒刺和蛰刺喷射，召唤黄蜂，瘫痪玩家
         * 2. 地面阶段：近战攻击，使用鞭挞和猛击
         */
        struct boss_ayamissAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             */
            boss_ayamissAI(Creature* creature) : BossAI(creature, DATA_AYAMISS)
            {
                Initialize();
            }

            /**
             * @brief 初始化成员变量
             *
             * 在构造函数和重置时调用，用于初始化战斗状态变量
             */
            void Initialize()
            {
                _phase = PHASE_AIR;     ///< 当前战斗阶段，初始为空中阶段
                _enraged = false;       ///< 是否已进入狂暴状态
            }

            /**
             * @brief 重置BOSS状态
             *
             * 当BOSS脱离战斗或重置时调用，用于恢复初始状态
             * @调用时机 战斗重置、BOSS脱战
             */
            void Reset() override
            {
                _Reset();
                Initialize();
                SetCombatMovement(false);  // 空中阶段不允许移动
            }

            /**
             * @brief 召唤生物回调
             * @param who 被召唤的生物
             *
             * 当BOSS召唤黄蜂、幼虫或大黄蜂时调用，用于设置其行为
             * @调用时机 召唤生物时
             */
            void JustSummoned(Creature* who) override
            {
                switch (who->GetEntry())
                {
                    case NPC_SWARMER:
                        // 将黄蜂添加到列表中，等待攻击指令
                        _swarmers.push_back(who->GetGUID());
                        break;
                    case NPC_LARVA:
                        // 幼虫移动到祭坛位置准备喂食
                        who->GetMotionMaster()->MovePoint(POINT_PARALYZE, AltarPos);
                        break;
                    case NPC_HORNET:
                        // 大黄蜂立即攻击随机目标
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random))
                            who->AI()->AttackStart(target);
                        break;
                }
            }

            /**
             * @brief 移动完成回调
             * @param type 移动类型
             * @param id 移动点ID
             *
             * 当BOSS到达指定位置时调用，用于控制空中和地面状态切换
             * @调用时机 生物到达移动目标点时
             */
            void MovementInform(uint32 type, uint32 id) override
            {
                if (type == POINT_MOTION_TYPE)
                {
                    switch (id)
                    {
                        case POINT_AIR:
                            // 到达空中位置后，设置为根除状态防止移动
                            me->AddUnitState(UNIT_STATE_ROOT);
                            break;
                        case POINT_GROUND:
                            // 到达地面后，清除根除状态允许移动
                            me->ClearUnitState(UNIT_STATE_ROOT);
                            break;
                    }
                }
            }

            /**
             * @brief 进入脱战模式
             * @param why 脱战原因
             *
             * 当BOSS脱离战斗时调用，清除根除状态
             * @调用时机 BOSS脱战、重置
             */
            void EnterEvadeMode(EvadeReason why) override
            {
                me->ClearUnitState(UNIT_STATE_ROOT);
                BossAI::EnterEvadeMode(why);
            }

            /**
             * @brief 进入战斗
             * @param attacker 攻击者
             *
             * 当BOSS被攻击并进入战斗时调用，初始化战斗事件
             * @调用时机 BOSS进入战斗
             */
            void JustEngagedWith(Unit* attacker) override
            {
                BossAI::JustEngagedWith(attacker);

                // 安排技能事件
                events.ScheduleEvent(EVENT_STINGER_SPRAY, 20s, 30s);   // 蛰刺喷射
                events.ScheduleEvent(EVENT_POISON_STINGER, 5s);        // 毒刺
                events.ScheduleEvent(EVENT_SUMMON_SWARMER, 5s);        // 召唤黄蜂
                events.ScheduleEvent(EVENT_SWARMER_ATTACK, 1min);      // 黄蜂攻击
                events.ScheduleEvent(EVENT_PARALYZE, 15s);             // 瘫痪玩家

                // 设置飞行状态并移动到空中位置
                me->SetCanFly(true);
                me->SetDisableGravity(true);
                me->GetMotionMaster()->MovePoint(POINT_AIR, AyamissAirPos);
            }

            /**
             * @brief 更新AI
             * @param diff 时间差（毫秒）
             *
             * 每帧调用，处理BOSS的战斗逻辑
             * @调用时机 每帧更新
             * @性能注意事项 该函数每帧调用，需保持高效
             */
            void UpdateAI(uint32 diff) override
            {
                // 如果没有目标则返回
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                // 阶段转换：当血量低于70%时从空中阶段转为地面阶段
                if (_phase == PHASE_AIR && me->GetHealthPct() < 70.0f)
                {
                    _phase = PHASE_GROUND;
                    SetCombatMovement(true);        // 允许移动
                    me->SetCanFly(false);           // 禁用飞行
                    if (me->GetVictim())
                    {
                        // 移动到当前目标位置
                        Position VictimPos = me->EnsureVictim()->GetPosition();
                        me->GetMotionMaster()->MovePoint(POINT_GROUND, VictimPos);
                    }
                    ResetThreatList();              // 重置威胁列表
                    // 安排地面阶段技能
                    events.ScheduleEvent(EVENT_LASH, 5s, 8s);
                    events.ScheduleEvent(EVENT_TRASH, 3s, 6s);
                    events.CancelEvent(EVENT_POISON_STINGER);  // 取消毒刺技能
                }
                else
                {
                    // 地面阶段执行近战攻击
                    DoMeleeAttackIfReady();
                }

                // 狂暴机制：血量低于20%时进入狂暴状态
                if (!_enraged && me->GetHealthPct() < 20.0f)
                {
                    DoCast(me, SPELL_FRENZY);
                    Talk(EMOTE_FRENZY);
                    _enraged = true;
                }

                // 处理事件队列
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_STINGER_SPRAY:
                            // 施放蛰刺喷射，对前方锥形区域造成伤害
                            DoCast(me, SPELL_STINGER_SPRAY);
                            events.ScheduleEvent(EVENT_STINGER_SPRAY, 15s, 20s);
                            break;
                        case EVENT_POISON_STINGER:
                            // 对当前目标施放毒刺（仅空中阶段）
                            DoCastVictim(SPELL_POISON_STINGER);
                            events.ScheduleEvent(EVENT_POISON_STINGER, 2s, 3s);
                            break;
                        case EVENT_PARALYZE:
                            // 随机瘫痪一个玩家并召唤幼虫
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0, true))
                            {
                                DoCast(target, SPELL_PARALYZE);
                                instance->SetGuidData(DATA_PARALYZED, target->GetGUID());
                                // 随机选择一个幼虫生成点
                                uint8 Index = urand(0, 1);
                                me->SummonCreature(NPC_LARVA, LarvaPos[Index], TEMPSUMMON_TIMED_OR_CORPSE_DESPAWN, 30s);
                            }
                            events.ScheduleEvent(EVENT_PARALYZE, 15s);
                            break;
                        case EVENT_SWARMER_ATTACK:
                            // 让所有黄蜂攻击随机目标
                            for (GuidList::iterator i = _swarmers.begin(); i != _swarmers.end(); ++i)
                                if (Creature* swarmer = ObjectAccessor::GetCreature(*me, *i))
                                    if (Unit* target = SelectTarget(SelectTargetMethod::Random))
                                        swarmer->AI()->AttackStart(target);

                            _swarmers.clear();  // 清空黄蜂列表
                            events.ScheduleEvent(EVENT_SWARMER_ATTACK, 1min);
                            break;
                        case EVENT_SUMMON_SWARMER:
                        {
                            // 在黄蜂生成点附近随机位置召唤黄蜂
                            Position Pos = me->GetRandomPoint(SwarmerPos, 80.0f);
                            me->SummonCreature(NPC_SWARMER, Pos);
                            events.ScheduleEvent(EVENT_SUMMON_SWARMER, 5s);
                            break;
                        }
                        case EVENT_TRASH:
                            // 地面阶段施放猛击
                            DoCastVictim(SPELL_TRASH);
                            events.ScheduleEvent(EVENT_TRASH, 5s, 7s);
                            break;
                        case EVENT_LASH:
                            // 地面阶段施放鞭挞
                            DoCastVictim(SPELL_LASH);
                            events.ScheduleEvent(EVENT_LASH, 8s, 15s);
                            break;
                    }
                }
            }
        private:
            GuidList _swarmers;     ///< 已召唤的黄蜂GUID列表，用于后续控制攻击
            uint8 _phase;           ///< 当前战斗阶段（空中/地面）
            bool _enraged;          ///< 是否已进入狂暴状态
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物指针
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetAQ20AI<boss_ayamissAI>(creature);
        }
};

/**
 * @class npc_hive_zara_larva
 * @brief 希拉克扎幼虫脚本类
 *
 * 负责管理幼虫的行为，主要是移动到祭坛并喂食瘫痪的玩家
 */
class npc_hive_zara_larva : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        npc_hive_zara_larva() : CreatureScript("npc_hive_zara_larva") { }

        /**
         * @class npc_hive_zara_larvaAI
         * @brief 希拉克扎幼虫AI实现类
         *
         * 实现幼虫的行为逻辑：
         * - 在BOSS战斗期间移动到祭坛喂食瘫痪玩家
         * - 在非战斗期间正常行动
         */
        struct npc_hive_zara_larvaAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物指针
             */
            npc_hive_zara_larvaAI(Creature* creature) : ScriptedAI(creature)
            {
                _instance = me->GetInstanceScript();
            }

            /**
             * @brief 移动完成回调
             * @param type 移动类型
             * @param id 移动点ID
             *
             * 当幼虫到达祭坛位置时，对瘫痪目标施放喂食技能
             * @调用时机 生物到达移动目标点时
             */
            void MovementInform(uint32 type, uint32 id) override
            {
                if (type == POINT_MOTION_TYPE)
                    if (id == POINT_PARALYZE)
                        if (Player* target = ObjectAccessor::GetPlayer(*me, _instance->GetGuidData(DATA_PARALYZED)))
                            DoCast(target, SPELL_FEED); // 喂食瘫痪玩家
            }

            /**
             * @brief 视线检测回调
             * @param who 进入视线的单位
             *
             * 在BOSS战斗期间不响应视线检测，避免干扰喂食行为
             * @调用时机 单位进入视线范围时
             */
            void MoveInLineOfSight(Unit* who) override

            {
                // 如果阿亚米斯BOSS正在战斗，幼虫不主动攻击
                if (_instance->GetBossState(DATA_AYAMISS) == IN_PROGRESS)
                    return;

                ScriptedAI::MoveInLineOfSight(who);
            }

            /**
             * @brief 开始攻击
             * @param victim 目标单位
             *
             * 在BOSS战斗期间不响应攻击指令
             * @调用时机 收到攻击指令时
             */
            void AttackStart(Unit* victim) override
            {
                // 如果阿亚米斯BOSS正在战斗，幼虫不主动攻击
                if (_instance->GetBossState(DATA_AYAMISS) == IN_PROGRESS)
                    return;

                ScriptedAI::AttackStart(victim);
            }

            /**
             * @brief 更新AI
             * @param diff 时间差（毫秒）
             *
             * 在BOSS战斗期间不执行常规AI更新
             * @调用时机 每帧更新
             */
            void UpdateAI(uint32 diff) override
            {
                // 如果阿亚米斯BOSS正在战斗，幼虫不执行常规AI
                if (_instance->GetBossState(DATA_AYAMISS) == IN_PROGRESS)
                    return;

                ScriptedAI::UpdateAI(diff);
            }
        private:
            InstanceScript* _instance;  ///< 副本脚本实例，用于查询BOSS状态和瘫痪目标
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物指针
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetAQ20AI<npc_hive_zara_larvaAI>(creature);
        }
};

/**
 * @brief 注册脚本
 *
 * 将阿亚米斯BOSS和幼虫脚本注册到脚本系统
 */
void AddSC_boss_ayamiss()
{
    new boss_ayamiss();
    new npc_hive_zara_larva();
}
