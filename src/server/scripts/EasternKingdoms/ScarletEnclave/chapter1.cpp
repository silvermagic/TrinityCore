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
 * @file chapter1.cpp
 * @brief 血色飞地第一章：死亡骑士初始任务脚本
 *
 * 本模块实现了死亡骑士新手区域（阿彻鲁斯）的第一章任务内容，包括：
 * - 不合格的学员任务链（任务ID: 12848）
 * - 阿彻鲁斯之眼任务（任务ID: 12641）
 * - 死亡挑战任务（任务ID: 12733）
 * - 进入暗影领域任务（任务ID: 12687）
 * - 持续给予的礼物任务（任务ID: 12698）
 * - 符文熔铸任务（任务ID: 12842）
 *
 * 主要NPC交互：
 * - 不合格的学员（NPC 29519-29567）：玩家首次战斗练习对象
 * - 阿彻鲁斯之眼（NPC 28511）：侦查任务相关
 * - 死亡骑士学员（NPC 28406）：决斗挑战
 * - 黑骑士（NPC 28652）：坐骑获取任务
 * - 萨拉纳尔（NPC 28653）：马匹交付任务
 */

#include "CreatureAIImpl.h"
#include "ScriptMgr.h"
#include "CombatAI.h"
#include "CreatureTextMgr.h"
#include "G3DPosition.hpp"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Log.h"
#include "MotionMaster.h"
#include "MoveSplineInit.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PassiveAI.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "Vehicle.h"

/*######
##Quest 12848
######*/

/**
 * @brief 全局冷却时间分组ID
 * 用于死亡骑士技能事件管理
 */
#define GCD_CAST    1

/**
 * @brief 不合格学员相关枚举定义
 *
 * 不合格学员是死亡骑士新手任务的第一个战斗训练对象，
 * 玩家需要在灵魂监狱中释放并击败他们以证明自己的实力。
 */
enum UnworthyInitiate
{
    SPELL_SOUL_PRISON_CHAIN         = 54612,    ///< 灵魂监狱锁链 - 束缚学员的法术
    SPELL_DK_INITIATE_VISUAL        = 51519,    ///< 死亡骑士学员视觉 - 变身效果

    SPELL_ICY_TOUCH                 = 52372,    ///< 冰霜之触 - 死亡骑士技能
    SPELL_PLAGUE_STRIKE             = 52373,    ///< 瘟疫打击 - 死亡骑士技能
    SPELL_BLOOD_STRIKE              = 52374,    ///< 血液打击 - 死亡骑士技能
    SPELL_DEATH_COIL                = 52375,    ///< 死亡缠绕 - 死亡骑士技能

    SAY_EVENT_START                 = 0,        ///< 事件开始台词
    SAY_EVENT_ATTACK                = 1,        ///< 攻击台词

    EVENT_ICY_TOUCH                 = 1,        ///< 冰霜之触事件ID
    EVENT_PLAGUE_STRIKE             = 2,        ///< 瘟疫打击事件ID
    EVENT_BLOOD_STRIKE              = 3,        ///< 血液打击事件ID
    EVENT_DEATH_COIL                = 4         ///< 死亡缠绕事件ID
};

/**
 * @brief 不合格学员的阶段状态
 *
 * 学员从被锁链束缚到被击败的完整流程状态机
 */
enum UnworthyInitiatePhase
{
    PHASE_CHAINED,      ///< 锁链束缚阶段 - 初始状态，学员被锁在灵魂监狱中
    PHASE_TO_EQUIP,     ///< 前往装备阶段 - 准备移动到武器架位置
    PHASE_EQUIPING,     ///< 装备中阶段 - 正在移动到武器架并装备武器
    PHASE_TO_ATTACK,    ///< 准备攻击阶段 - 已装备武器，准备发起攻击
    PHASE_ATTACKING,    ///< 攻击阶段 - 正在与玩家战斗
};

/**
 * @brief 阿彻鲁斯灵魂监狱游戏对象ID数组
 *
 * 这些是阿彻鲁斯区域内用于关押不合格学员的灵魂监狱，
 * 玩家需要点击这些监狱来释放学员并与之战斗。
 */
uint32 acherus_soul_prison[12] =
{
    191577,  ///< 灵魂监狱 1
    191580,  ///< 灵魂监狱 2
    191581,  ///< 灵魂监狱 3
    191582,  ///< 灵魂监狱 4
    191583,  ///< 灵魂监狱 5
    191584,  ///< 灵魂监狱 6
    191585,  ///< 灵魂监狱 7
    191586,  ///< 灵魂监狱 8
    191587,  ///< 灵魂监狱 9
    191588,  ///< 灵魂监狱 10
    191589,  ///< 灵魂监狱 11
    191590   ///< 灵魂监狱 12
};

/**
 * @brief 不合格学员NPC ID数组
 *
 * 这些是不同种族和性别的不合格学员，玩家可以释放并挑战他们。
 * 包括各种族的女性和男性模型。
 */
uint32 acherus_unworthy_initiate[5] =
{
    29519,  ///< 不合格学员变体 1
    29520,  ///< 不合格学员变体 2
    29565,  ///< 不合格学员变体 3
    29566,  ///< 不合格学员变体 4
    29567   ///< 不合格学员变体 5
};

/**
 * @class npc_unworthy_initiate
 * @brief 不合格学员NPC脚本
 *
 * 实现死亡骑士新手任务中的战斗训练对象 - 不合格学员。
 * 玩家需要从灵魂监狱中释放他们，然后击败他们以完成任务。
 *
 * 任务流程：
 * 1. 玩家点击灵魂监狱游戏对象
 * 2. 学员从锁链中释放
 * 3. 学员移动到武器架装备武器
 * 4. 学员与玩家战斗
 * 5. 玩家击败学员完成任务目标
 */
class npc_unworthy_initiate : public CreatureScript
{
public:
    npc_unworthy_initiate() : CreatureScript("npc_unworthy_initiate") { }

    /**
     * @struct npc_unworthy_initiateAI
     * @brief 不合格学员AI实现
     *
     * 管理学员的行为状态机，包括：
     * - 被锁链束缚的初始状态
     * - 释放后的装备过程
     * - 与玩家的战斗行为
     */
    struct npc_unworthy_initiateAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature NPC生物对象指针
         *
         * 初始化AI状态，设置为被动反应状态，
         * 并确保装备配置正确。
         */
        npc_unworthy_initiateAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            me->SetReactState(REACT_PASSIVE);
            if (!me->GetCurrentEquipmentId())
                me->SetCurrentEquipmentId(me->GetOriginalEquipmentId());

            wait_timer = 0;
            anchorX = 0.f;
            anchorY = 0.f;
        }

        /**
         * @brief 初始化AI状态变量
         *
         * 重置所有状态变量到初始值，用于Reset函数和构造函数。
         */
        void Initialize()
        {
            anchorGUID.Clear();
            phase = PHASE_CHAINED;
        }

        ObjectGuid playerGUID;                  ///< 发起事件的玩家GUID
        UnworthyInitiatePhase phase;            ///< 当前阶段状态
        uint32 wait_timer;                      ///< 等待计时器（毫秒）
        float anchorX, anchorY;                 ///< 锚点坐标（武器架位置）
        ObjectGuid anchorGUID;                  ///< 锚点NPC的GUID（用于锁链连接）

        EventMap events;                        ///< 事件映射表（用于技能冷却管理）

        /**
         * @brief 重置AI状态
         *
         * 当学员脱离战斗或重生时调用，重置所有状态到初始值：
         * - 重置事件映射表
         * - 恢复默认阵营
         * - 设置免疫玩家攻击
         * - 设置跪姿状态
         * - 卸载武器装备
         *
         * @note 性能考虑：调用频率较低，主要在任务重置时触发
         */
        void Reset() override
        {
            Initialize();
            events.Reset();
            me->SetFaction(FACTION_CREATURE);
            me->SetImmuneToPC(true);
            me->SetStandState(UNIT_STAND_STATE_KNEEL);
            me->LoadEquipment(0, true);
        }

        /**
         * @brief 进入战斗事件
         * @param who 攻击者单位指针
         *
         * 当学员进入战斗时，初始化技能事件调度。
         * 安排死亡骑士基础技能的施放时间表。
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            events.ScheduleEvent(EVENT_ICY_TOUCH, 1s, GCD_CAST);
            events.ScheduleEvent(EVENT_PLAGUE_STRIKE, 3s, GCD_CAST);
            events.ScheduleEvent(EVENT_BLOOD_STRIKE, 2s, GCD_CAST);
            events.ScheduleEvent(EVENT_DEATH_COIL, 5s, GCD_CAST);
        }

        /**
         * @brief 移动完成通知
         * @param type 移动类型
         * @param id 移动点ID
         *
         * 当学员移动到指定位置后触发。
         * 主要用于处理到达武器架后的装备流程。
         */
        void MovementInform(uint32 type, uint32 id) override
        {
            if (type != POINT_MOTION_TYPE)
                return;

            if (id == 1)
            {
                // 到达武器架位置
                wait_timer = 5000;
                me->LoadEquipment(1);  // 装备武器
                me->CastSpell(me, SPELL_DK_INITIATE_VISUAL, true);  // 施放视觉效果

                if (Player* starter = ObjectAccessor::GetPlayer(*me, playerGUID))
                    Talk(SAY_EVENT_ATTACK, starter);

                phase = PHASE_TO_ATTACK;
            }
        }

        /**
         * @brief 启动释放事件
         * @param anchor 锚点NPC（灵魂监狱锚点）
         * @param target 目标玩家
         *
         * 当玩家点击灵魂监狱时调用，启动释放流程：
         * 1. 设置等待计时器
         * 2. 改变阶段为"前往装备"
         * 3. 站起来
         * 4. 移除锁链法术
         * 5. 记录武器架位置
         * 6. 记录玩家GUID
         * 7. 说出台词
         *
         * @note 由go_acherus_soul_prison的OnGossipHello调用
         */
        void EventStart(Creature* anchor, Player* target)
        {
            wait_timer = 5000;
            phase = PHASE_TO_EQUIP;

            me->SetStandState(UNIT_STAND_STATE_STAND);
            me->RemoveAurasDueToSpell(SPELL_SOUL_PRISON_CHAIN);

            float z;
            anchor->GetContactPoint(me, anchorX, anchorY, z, 1.0f);

            playerGUID = target->GetGUID();
            Talk(SAY_EVENT_START, target);
        }

        /**
         * @brief 更新AI状态
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 主循环函数，根据当前阶段处理不同的逻辑：
         *
         * 1. PHASE_CHAINED（锁链束缚）：
         *    - 查找最近的锚点NPC并建立连接
         *    - 查找最近的灵魂监狱并重置状态
         *
         * 2. PHASE_TO_EQUIP（前往装备）：
         *    - 等待计时器到期后移动到武器架
         *
         * 3. PHASE_TO_ATTACK（准备攻击）：
         *    - 等待计时器到期后进入战斗
         *    - 设置为敌对阵营
         *    - 开始攻击玩家
         *
         * 4. PHASE_ATTACKING（攻击中）：
         *    - 执行战斗AI逻辑
         *    - 按时间表施放死亡骑士技能
         *
         * @note 每帧调用，性能关键函数
         */
        void UpdateAI(uint32 diff) override
        {
            switch (phase)
            {
            case PHASE_CHAINED:
                // 初始化：查找锚点和监狱
                if (!anchorGUID)
                {
                    // 查找附近的锚点NPC（用于锁链视觉效果）
                    if (Creature* anchor = me->FindNearestCreature(29521, 30))
                    {
                        anchor->AI()->SetGUID(me->GetGUID());
                        anchor->CastSpell(me, SPELL_SOUL_PRISON_CHAIN, true);
                        anchorGUID = anchor->GetGUID();
                    }
                    else
                        TC_LOG_ERROR("scripts", "npc_unworthy_initiateAI: unable to find anchor!");

                    // 查找最近的灵魂监狱游戏对象
                    float dist = 99.0f;
                    GameObject* prison = nullptr;

                    for (uint8 i = 0; i < 12; ++i)
                    {
                        if (GameObject* temp_prison = me->FindNearestGameObject(acherus_soul_prison[i], 30))
                        {
                            if (me->IsWithinDist(temp_prison, dist, false))
                            {
                                dist = me->GetDistance2d(temp_prison);
                                prison = temp_prison;
                            }
                        }
                    }

                    if (prison)
                        prison->ResetDoorOrButton();
                    else
                        TC_LOG_ERROR("scripts", "npc_unworthy_initiateAI: unable to find prison!");
                }
                break;
            case PHASE_TO_EQUIP:
                // 等待5秒后移动到武器架
                if (wait_timer)
                {
                    if (wait_timer > diff)
                        wait_timer -= diff;
                    else
                    {
                        me->GetMotionMaster()->MovePoint(1, anchorX, anchorY, me->GetPositionZ());
                        //TC_LOG_DEBUG("scripts", "npc_unworthy_initiateAI: move to {} {} {}", anchorX, anchorY, me->GetPositionZ());
                        phase = PHASE_EQUIPING;
                        wait_timer = 0;
                    }
                }
                break;
            case PHASE_TO_ATTACK:
                // 等待5秒后开始攻击
                if (wait_timer)
                {
                    if (wait_timer > diff)
                        wait_timer -= diff;
                    else
                    {
                        me->SetFaction(FACTION_MONSTER);
                        me->SetImmuneToPC(false);
                        me->SetReactState(REACT_AGGRESSIVE);
                        phase = PHASE_ATTACKING;

                        if (Player* target = ObjectAccessor::GetPlayer(*me, playerGUID))
                            AttackStart(target);
                        wait_timer = 0;
                    }
                }
                break;
            case PHASE_ATTACKING:
                // 战斗阶段：执行技能循环
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                    case EVENT_ICY_TOUCH:
                        DoCastVictim(SPELL_ICY_TOUCH);
                        events.DelayEvents(1s, GCD_CAST);
                        events.ScheduleEvent(EVENT_ICY_TOUCH, 5s, GCD_CAST);
                        break;
                    case EVENT_PLAGUE_STRIKE:
                        DoCastVictim(SPELL_PLAGUE_STRIKE);
                        events.DelayEvents(1s, GCD_CAST);
                        events.ScheduleEvent(EVENT_PLAGUE_STRIKE, 5s, GCD_CAST);
                        break;
                    case EVENT_BLOOD_STRIKE:
                        DoCastVictim(SPELL_BLOOD_STRIKE);
                        events.DelayEvents(1s, GCD_CAST);
                        events.ScheduleEvent(EVENT_BLOOD_STRIKE, 5s, GCD_CAST);
                        break;
                    case EVENT_DEATH_COIL:
                        DoCastVictim(SPELL_DEATH_COIL);
                        events.DelayEvents(1s, GCD_CAST);
                        events.ScheduleEvent(EVENT_DEATH_COIL, 5s, GCD_CAST);
                        break;
                    }
                }

                DoMeleeAttackIfReady();
                break;
            default:
                break;
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_unworthy_initiateAI(creature);
    }
};

/**
 * @class npc_unworthy_initiate_anchor
 * @brief 不合格学员锚点NPC脚本
 *
 * 这是一个不可见的辅助NPC，用于连接不合格学员和灵魂监狱。
 * 主要功能是存储关联的学员GUID，并在学员和监狱之间建立连接关系。
 */
class npc_unworthy_initiate_anchor : public CreatureScript
{
public:
    npc_unworthy_initiate_anchor() : CreatureScript("npc_unworthy_initiate_anchor") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_unworthy_initiate_anchorAI(creature);
    }

    /**
     * @struct npc_unworthy_initiate_anchorAI
     * @brief 锚点NPC的被动AI
     *
     * 不进行任何主动行为，仅用于存储和管理学员GUID。
     */
    struct npc_unworthy_initiate_anchorAI : public PassiveAI
    {
        npc_unworthy_initiate_anchorAI(Creature* creature) : PassiveAI(creature) { }

        ObjectGuid prisonerGUID;  ///< 关联的学员GUID

        /**
         * @brief 设置学员GUID
         * @param guid 学员的GUID
         * @param id 未使用的ID参数
         */
        void SetGUID(ObjectGuid const& guid, int32 /*id*/) override
        {
            prisonerGUID = guid;
        }

        /**
         * @brief 获取学员GUID
         * @param id 未使用的ID参数
         * @return 学员的GUID
         */
        ObjectGuid GetGUID(int32 /*id*/) const override
        {
            return prisonerGUID;
        }
    };
};

/**
 * @class go_acherus_soul_prison
 * @brief 阿彻鲁斯灵魂监狱游戏对象脚本
 *
 * 实现玩家与灵魂监狱交互的逻辑。
 * 当玩家点击监狱时，会释放关联的不合格学员并启动战斗事件。
 */
class go_acherus_soul_prison : public GameObjectScript
{
    public:
        go_acherus_soul_prison() : GameObjectScript("go_acherus_soul_prison") { }

        /**
         * @struct go_acherus_soul_prisonAI
         * @brief 灵魂监狱游戏对象AI
         */
        struct go_acherus_soul_prisonAI : public GameObjectAI
        {
            go_acherus_soul_prisonAI(GameObject* go) : GameObjectAI(go) { }

            /**
             * @brief 玩家与游戏对象交互事件
             * @param player 交互的玩家指针
             * @return false - 允许继续默认交互
             *
             * 执行流程：
             * 1. 查找附近的锚点NPC
             * 2. 从锚点获取关联的学员GUID
             * 3. 获取学员NPC
             * 4. 触发学员的EventStart函数
             */
            bool OnGossipHello(Player* player) override
            {
                if (Creature* anchor = me->FindNearestCreature(29521, 15))
                    if (ObjectGuid prisonerGUID = anchor->AI()->GetGUID())
                        if (Creature* prisoner = ObjectAccessor::GetCreature(*player, prisonerGUID))
                            ENSURE_AI(npc_unworthy_initiate::npc_unworthy_initiateAI, prisoner->AI())->EventStart(anchor, player);

                return false;
            }
        };

        GameObjectAI* GetAI(GameObject* go) const override
        {
            return new go_acherus_soul_prisonAI(go);
        }
};

/**
 * @class spell_death_knight_initiate_visual
 * @brief 死亡骑士学员视觉效果法术脚本
 *
 * 法术ID: 51519
 * 当不合格学员装备武器后触发，根据学员的种族和性别
 * 施放对应的变身效果法术。
 */
class spell_death_knight_initiate_visual : public SpellScript
{
    PrepareSpellScript(spell_death_knight_initiate_visual);

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引（未使用）
     *
     * 根据目标的显示ID确定种族和性别，然后施放对应的视觉效果法术。
     * 包含所有可玩种族的女性和男性变体。
     */
    void HandleScriptEffect(SpellEffIndex /* effIndex */)
    {
        Creature* target = GetHitCreature();
        if (!target)
            return;

        uint32 spellId;
        switch (target->GetDisplayId())
        {
            case 25369: spellId = 51552; break; // 血精灵女性
            case 25373: spellId = 51551; break; // 血精灵男性
            case 25363: spellId = 51542; break; // 德莱尼女性
            case 25357: spellId = 51541; break; // 德莱尼男性
            case 25361: spellId = 51537; break; // 矮人女性
            case 25356: spellId = 51538; break; // 矮人男性
            case 25372: spellId = 51550; break; // 被遗忘者女性
            case 25367: spellId = 51549; break; // 被遗忘者男性
            case 25362: spellId = 51540; break; // 侏儒女性
            case 25359: spellId = 51539; break; // 侏儒男性
            case 25355: spellId = 51534; break; // 人类女性
            case 25354: spellId = 51520; break; // 人类男性
            case 25360: spellId = 51536; break; // 暗夜精灵女性
            case 25358: spellId = 51535; break; // 暗夜精灵男性
            case 25368: spellId = 51544; break; // 兽人女性
            case 25364: spellId = 51543; break; // 兽人男性
            case 25371: spellId = 51548; break; // 牛头人女性
            case 25366: spellId = 51547; break; // 牛头人男性
            case 25370: spellId = 51545; break; // 巨魔女性
            case 25365: spellId = 51546; break; // 巨魔男性
            default: return;
        }

        target->CastSpell(target, spellId, true);
        target->LoadEquipment();
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_death_knight_initiate_visual::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 阿彻鲁斯之眼相关枚举定义
 *
 * 阿彻鲁斯之眼是死亡骑士侦查任务的关键道具，
 * 允许玩家控制一个飞行眼球来侦查新阿瓦隆区域。
 */
enum EyeOfAcherusMisc
{
    SPELL_THE_EYE_OF_ACHERUS                = 51852,    ///< 阿彻鲁斯之眼 - 主要控制法术
    SPELL_EYE_OF_ACHERUS_VISUAL             = 51892,    ///< 阿彻鲁斯之眼视觉效果
    SPELL_EYE_OF_ACHERUS_FLIGHT_BOOST       = 51923,    ///< 阿彻鲁斯之眼飞行加速
    SPELL_EYE_OF_ACHERUS_FLIGHT             = 51890,    ///< 阿彻鲁斯之眼飞行能力
    SPELL_ROOT_SELF                         = 51860,    ///< 自我定身 - 初始状态

    EVENT_ANNOUNCE_LAUNCH_TO_DESTINATION    = 1,        ///< 宣布向目的地发射事件
    EVENT_UNROOT                            = 2,        ///< 解除定身事件
    EVENT_LAUNCH_TOWARDS_DESTINATION        = 3,        ///< 向目的地发射事件
    EVENT_GRANT_CONTROL                     = 4,        ///< 授予控制权事件

    SAY_LAUNCH_TOWARDS_DESTINATION          = 0,        ///< 向目的地发射台词
    SAY_EYE_UNDER_CONTROL                   = 1,        ///< 眼球被控制台词

    POINT_NEW_AVALON                        = 1         ///< 新阿瓦隆路径点ID
};

/// 阿彻鲁斯之眼飞行路径点数量
static constexpr uint8 const EyeOfAcherusPathSize = 4;

/**
 * @brief 阿彻鲁斯之眼飞行路径坐标
 *
 * 定义了从阿彻鲁斯到新阿瓦隆的飞行路径，
 * 包括起始点、中间点和目的地。
 */
G3D::Vector3 const EyeOfAcherusPath[EyeOfAcherusPathSize] =
{
    { 2361.21f,  -5660.45f,  496.744f  },  ///< 起始点 - 阿彻鲁斯内部
    { 2341.571f, -5672.797f, 538.3942f },  ///< 中间点 1 - 上升阶段
    { 1957.4f,   -5844.1f,   273.867f  },  ///< 中间点 2 - 飞行中
    { 1758.01f,  -5876.79f,  166.867f  }   ///< 目的地 - 新阿瓦隆上空
};

/**
 * @struct npc_eye_of_acherus
 * @brief 阿彻鲁斯之眼NPC AI
 *
 * 实现死亡骑士侦查任务的飞行眼球控制逻辑。
 * 眼球会自动从阿彻鲁斯飞往新阿瓦隆，然后玩家可以控制它进行侦查。
 *
 * 任务流程：
 * 1. 玩家施放阿彻鲁斯之眼法术
 * 2. 眼球出现在阿彻鲁斯内
 * 3. 眼球自动沿路径飞往新阿瓦隆
 * 4. 到达后授予玩家控制权
 * 5. 玩家控制眼球侦查指定地点
 */
struct npc_eye_of_acherus : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature NPC生物对象指针
     *
     * 初始化眼球的显示模型和反应状态。
     */
    npc_eye_of_acherus(Creature* creature) : ScriptedAI(creature)
    {
        creature->SetDisplayId(creature->GetCreatureTemplate()->Modelid1);
        creature->SetReactState(REACT_PASSIVE);
    }

    /**
     * @brief 初始化AI
     *
     * 在AI创建后调用，设置初始状态：
     * - 施放定身法术（眼球初始不能移动）
     * - 施放视觉效果
     * - 安排宣布发射事件
     */
    void InitializeAI() override
    {
        DoCastSelf(SPELL_ROOT_SELF);
        DoCastSelf(SPELL_EYE_OF_ACHERUS_VISUAL);
        _events.ScheduleEvent(EVENT_ANNOUNCE_LAUNCH_TO_DESTINATION, 7s);
    }

    /**
     * @brief 被魅惑/控制状态变化
     * @param apply true表示被控制，false表示控制结束
     *
     * 当玩家控制结束（取消控制或眼球被摧毁）时，
     * 移除玩家身上的相关法术效果。
     */
    void OnCharmed(bool apply) override
    {
        if (!apply)
        {
            me->GetCharmerOrOwner()->RemoveAurasDueToSpell(SPELL_THE_EYE_OF_ACHERUS);
            me->GetCharmerOrOwner()->RemoveAurasDueToSpell(SPELL_EYE_OF_ACHERUS_FLIGHT_BOOST);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_ANNOUNCE_LAUNCH_TO_DESTINATION:
                    if (Unit* owner = me->GetCharmerOrOwner())
                        Talk(SAY_LAUNCH_TOWARDS_DESTINATION, owner);
                    _events.ScheduleEvent(EVENT_UNROOT, 1s + 200ms);
                    break;
                case EVENT_UNROOT:
                    me->RemoveAurasDueToSpell(SPELL_ROOT_SELF);
                    DoCastSelf(SPELL_EYE_OF_ACHERUS_FLIGHT_BOOST);
                    _events.ScheduleEvent(EVENT_LAUNCH_TOWARDS_DESTINATION, 1s + 200ms);
                    break;
                case EVENT_LAUNCH_TOWARDS_DESTINATION:
                {
                    std::function<void(Movement::MoveSplineInit&)> initializer = [=, me = me](Movement::MoveSplineInit& init)
                    {
                        Movement::PointsArray path(EyeOfAcherusPath, EyeOfAcherusPath + EyeOfAcherusPathSize);
                        init.MovebyPath(path);
                        init.SetFly();
                        if (Unit* owner = me->GetCharmerOrOwner())
                            init.SetVelocity(owner->GetSpeed(MOVE_RUN));
                    };

                    me->GetMotionMaster()->LaunchMoveSpline(std::move(initializer), POINT_NEW_AVALON, MOTION_PRIORITY_NORMAL, POINT_MOTION_TYPE);
                    break;
                }
                case EVENT_GRANT_CONTROL:
                    me->RemoveAurasDueToSpell(SPELL_ROOT_SELF);
                    DoCastSelf(SPELL_EYE_OF_ACHERUS_FLIGHT);
                    me->RemoveAurasDueToSpell(SPELL_EYE_OF_ACHERUS_FLIGHT_BOOST);
                    if (Unit* owner = me->GetCharmerOrOwner())
                        Talk(SAY_EYE_UNDER_CONTROL, owner);
                    break;
                default:
                    break;
            }
        }
    }

    void MovementInform(uint32 movementType, uint32 pointId) override
    {
        if (movementType != POINT_MOTION_TYPE)
            return;

        switch (pointId)
        {
            case POINT_NEW_AVALON:
                DoCastSelf(SPELL_ROOT_SELF);
                _events.ScheduleEvent(EVENT_GRANT_CONTROL, 2s + 500ms);
                break;
            default:
                break;
        }
    }

private:
    EventMap _events;
};

/*######
## npc_death_knight_initiate
######*/

enum Spells_DKI
{
    SPELL_DUEL                  = 52996,
    //SPELL_DUEL_TRIGGERED        = 52990,
    SPELL_DUEL_VICTORY          = 52994,
    SPELL_DUEL_FLAG             = 52991,
    SPELL_GROVEL                = 7267,
};

enum Says_VBM
{
    SAY_DUEL                    = 0,
};

enum Misc_VBN
{
    QUEST_DEATH_CHALLENGE       = 12733
};

class npc_death_knight_initiate : public CreatureScript
{
public:
    npc_death_knight_initiate() : CreatureScript("npc_death_knight_initiate") { }

    struct npc_death_knight_initiateAI : public CombatAI
    {
        npc_death_knight_initiateAI(Creature* creature) : CombatAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            m_uiDuelerGUID.Clear();
            m_uiDuelTimer = 5000;
            m_bIsDuelInProgress = false;
            lose = false;
        }

        bool lose;
        ObjectGuid m_uiDuelerGUID;
        uint32 m_uiDuelTimer;
        bool m_bIsDuelInProgress;

        void Reset() override
        {
            Initialize();

            me->RestoreFaction();
            CombatAI::Reset();
            me->SetUnitFlag(UNIT_FLAG_CAN_SWIM);
        }

        void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
        {
            if (!m_bIsDuelInProgress && spellInfo->Id == SPELL_DUEL)
            {
                m_uiDuelerGUID = caster->GetGUID();
                Talk(SAY_DUEL, caster);
                m_bIsDuelInProgress = true;
            }
        }

       void DamageTaken(Unit* pDoneBy, uint32 &uiDamage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            if (m_bIsDuelInProgress && pDoneBy && pDoneBy->IsControlledByPlayer())
            {
                if (pDoneBy->GetGUID() != m_uiDuelerGUID && pDoneBy->GetOwnerGUID() != m_uiDuelerGUID) // other players cannot help
                    uiDamage = 0;
                else if (uiDamage >= me->GetHealth())
                {
                    uiDamage = 0;

                    if (!lose)
                    {
                        pDoneBy->RemoveGameObject(SPELL_DUEL_FLAG, true);
                        pDoneBy->AttackStop();
                        me->CastSpell(pDoneBy, SPELL_DUEL_VICTORY, true);
                        lose = true;
                        me->CastSpell(me, SPELL_GROVEL, true);
                        me->RestoreFaction();
                    }
                }
            }
        }

        void UpdateAI(uint32 uiDiff) override
        {
            if (!UpdateVictim())
            {
                if (m_bIsDuelInProgress)
                {
                    if (m_uiDuelTimer <= uiDiff)
                    {
                        me->SetFaction(FACTION_UNDEAD_SCOURGE_2);

                        if (Unit* unit = ObjectAccessor::GetUnit(*me, m_uiDuelerGUID))
                            AttackStart(unit);
                    }
                    else
                        m_uiDuelTimer -= uiDiff;
                }
                return;
            }

            if (m_bIsDuelInProgress)
            {
                if (lose)
                {
                    if (!me->HasAura(SPELL_GROVEL))
                        EnterEvadeMode();
                    return;
                }
                else if (me->GetVictim() && me->EnsureVictim()->GetTypeId() == TYPEID_PLAYER && me->EnsureVictim()->HealthBelowPct(10))
                {
                    me->EnsureVictim()->CastSpell(me->GetVictim(), SPELL_GROVEL, true); // beg
                    me->EnsureVictim()->RemoveGameObject(SPELL_DUEL_FLAG, true);
                    EnterEvadeMode();
                    return;
                }
            }

            /// @todo spells

            CombatAI::UpdateAI(uiDiff);
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);
            if (action == GOSSIP_ACTION_INFO_DEF)
            {
                CloseGossipMenuFor(player);

                if (player->IsInCombat() || me->IsInCombat())
                    return true;

                if (m_bIsDuelInProgress)
                    return true;

                me->SetImmuneToPC(false);
                me->RemoveUnitFlag(UNIT_FLAG_CAN_SWIM);

                player->CastSpell(me, SPELL_DUEL, false);
                player->CastSpell(player, SPELL_DUEL_FLAG, true);
            }
            return true;
        }

        bool OnGossipHello(Player* player) override
        {
            uint32 gossipMenuId = Player::GetDefaultGossipMenuForSource(me);
            InitGossipMenuFor(player, gossipMenuId);
            if (player->GetQuestStatus(QUEST_DEATH_CHALLENGE) == QUEST_STATUS_INCOMPLETE && me->IsFullHealth())
            {
                if (player->HealthBelowPct(10))
                    return true;

                if (player->IsInCombat() || me->IsInCombat())
                    return true;

                AddGossipItemFor(player, gossipMenuId, 0, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
                SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
            }
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_death_knight_initiateAI(creature);
    }
};

/*######
## npc_dark_rider_of_acherus
######*/

enum DarkRiderOfAcherus
{
    SAY_DARK_RIDER              = 0,

    EVENT_START_MOVING          = 1,
    EVENT_DESPAWN_HORSE         = 2,
    EVENT_END_SCRIPT            = 3,

    SPELL_DESPAWN_HORSE         = 52267
};

struct npc_dark_rider_of_acherus : public ScriptedAI
{
    npc_dark_rider_of_acherus(Creature* creature) : ScriptedAI(creature) { }

    void JustAppeared() override
    {
        if (TempSummon* summon = me->ToTempSummon())
            _horseGUID = summon->GetSummonerGUID();

        _events.ScheduleEvent(EVENT_START_MOVING, 1s);
    }

    void Reset() override
    {
        _events.Reset();
    }

    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_START_MOVING:
                    me->SetTarget(_horseGUID);
                    if (Creature* horse = ObjectAccessor::GetCreature(*me, _horseGUID))
                        me->GetMotionMaster()->MoveChase(horse);
                    _events.ScheduleEvent(EVENT_DESPAWN_HORSE, 5s);
                    break;
                case EVENT_DESPAWN_HORSE:
                    Talk(SAY_DARK_RIDER);
                    if (Creature* horse = ObjectAccessor::GetCreature(*me, _horseGUID))
                        DoCast(horse, SPELL_DESPAWN_HORSE, true);
                    _events.ScheduleEvent(EVENT_END_SCRIPT, 2s);
                    break;
                case EVENT_END_SCRIPT:
                    me->DespawnOrUnsummon();
                    break;
                default:
                    break;
            }
        }
    }

    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_DESPAWN_HORSE && target->GetGUID() == _horseGUID)
            if (Creature* creature = target->ToCreature())
                creature->DespawnOrUnsummon(2s);
    }

private:
    ObjectGuid _horseGUID;
    EventMap _events;
};

/*######
## npc_salanar_the_horseman
######*/

enum SalanarTheHorseman
{
    SALANAR_SAY                       = 0,
    QUEST_INTO_REALM_OF_SHADOWS       = 12687,
    SPELL_EFFECT_STOLEN_HORSE         = 52263,
    SPELL_DELIVER_STOLEN_HORSE        = 52264,
    SPELL_CALL_DARK_RIDER             = 52266,
    SPELL_EFFECT_OVERTAKE             = 52349,
    SPELL_REALM_OF_SHADOWS            = 52693
};

class npc_salanar_the_horseman : public CreatureScript
{
public:
    npc_salanar_the_horseman() : CreatureScript("npc_salanar_the_horseman") { }

    struct npc_salanar_the_horsemanAI : public ScriptedAI
    {
        npc_salanar_the_horsemanAI(Creature* creature) : ScriptedAI(creature) { }

        void MoveInLineOfSight(Unit* who) override
        {
            ScriptedAI::MoveInLineOfSight(who);

            if (who->GetTypeId() == TYPEID_UNIT && who->IsVehicle() && me->IsWithinDistInMap(who, 5.0f))
            {
                if (Unit* charmer = who->GetCharmer())
                {
                    if (Player* player = charmer->ToPlayer())
                    {
                        if (player->GetQuestStatus(QUEST_INTO_REALM_OF_SHADOWS) == QUEST_STATUS_INCOMPLETE)
                        {
                            player->GroupEventHappens(QUEST_INTO_REALM_OF_SHADOWS, me);
                            Talk(SALANAR_SAY);
                            charmer->RemoveAurasDueToSpell(SPELL_EFFECT_OVERTAKE);
                            if (Creature* creature = who->ToCreature())
                            {
                                creature->DespawnOrUnsummon();
                                //creature->Respawn(true);
                            }
                        }

                        player->RemoveAurasDueToSpell(SPELL_REALM_OF_SHADOWS);
                    }
                }
            }
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_salanar_the_horsemanAI(creature);
    }
};

enum HorseSeats
{
    SEAT_ID_0   = 0
};

// 52265 - Repo
class spell_stable_master_repo : public AuraScript
{
    PrepareAuraScript(spell_stable_master_repo);

    void AfterApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Creature* creature = GetTarget()->ToCreature();
        if (!creature)
            return;

        if (Vehicle* vehicleKit = creature->GetVehicleKit())
            if (Unit* passenger = vehicleKit->GetPassenger(SEAT_ID_0))
                GetCaster()->EngageWithTarget(passenger);

        creature->DespawnOrUnsummon(1s);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_stable_master_repo::AfterApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

// 52264 - Deliver Stolen Horse
class spell_deliver_stolen_horse : public SpellScript
{
    PrepareSpellScript(spell_deliver_stolen_horse);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_DELIVER_STOLEN_HORSE, SPELL_EFFECT_STOLEN_HORSE });
    }

    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        target->RemoveAurasDueToSpell(SPELL_EFFECT_STOLEN_HORSE);

        Unit* caster = GetCaster();
        caster->RemoveAurasDueToSpell(SPELL_EFFECT_STOLEN_HORSE);
        caster->RemoveNpcFlag(UNIT_NPC_FLAG_SPELLCLICK);
        caster->SetFaction(FACTION_FRIENDLY);

        caster->CastSpell(caster, SPELL_CALL_DARK_RIDER, true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_deliver_stolen_horse::HandleScriptEffect, EFFECT_1, SPELL_EFFECT_KILL_CREDIT2);
    }
};

/*######
## npc_ros_dark_rider
######*/

class npc_ros_dark_rider : public CreatureScript
{
public:
    npc_ros_dark_rider() : CreatureScript("npc_ros_dark_rider") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_ros_dark_riderAI(creature);
    }

    struct npc_ros_dark_riderAI : public ScriptedAI
    {
        npc_ros_dark_riderAI(Creature* creature) : ScriptedAI(creature) { }

        void JustEngagedWith(Unit* /*who*/) override
        {
            me->ExitVehicle();
        }

        void Reset() override
        {
            Creature* deathcharger = me->FindNearestCreature(28782, 30);
            if (!deathcharger)
                return;

            deathcharger->RestoreFaction();
            deathcharger->RemoveNpcFlag(UNIT_NPC_FLAG_SPELLCLICK);
            deathcharger->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
            if (!me->GetVehicle() && deathcharger->IsVehicle() && deathcharger->GetVehicleKit()->HasEmptySeat(0))
                me->EnterVehicle(deathcharger);
        }

        void JustDied(Unit* killer) override
        {
            Creature* deathcharger = me->FindNearestCreature(28782, 30);
            if (!deathcharger || !killer)
                return;

            if (killer->GetTypeId() == TYPEID_PLAYER && deathcharger->GetTypeId() == TYPEID_UNIT && deathcharger->IsVehicle())
            {
                deathcharger->SetNpcFlag(UNIT_NPC_FLAG_SPELLCLICK);
                deathcharger->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                deathcharger->SetFaction(FACTION_SCARLET_CRUSADE_2);
            }
        }
    };

};

// correct way: 52312 52314 52555 ...
enum TheGiftThatKeepsOnGiving
{
    SAY_LINE_0  = 0,

    NPC_GHOULS  = 28845,
    NPC_GHOSTS  = 28846,
};

class npc_dkc1_gothik : public CreatureScript
{
public:
    npc_dkc1_gothik() : CreatureScript("npc_dkc1_gothik") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_dkc1_gothikAI(creature);
    }

    struct npc_dkc1_gothikAI : public ScriptedAI
    {
        npc_dkc1_gothikAI(Creature* creature) : ScriptedAI(creature) { }

        void MoveInLineOfSight(Unit* who) override

        {
            ScriptedAI::MoveInLineOfSight(who);

            if (who->GetEntry() == NPC_GHOULS && me->IsWithinDistInMap(who, 10.0f))
            {
                if (Unit* owner = who->GetOwner())
                {
                    if (Player* player = owner->ToPlayer())
                    {
                        Creature* creature = who->ToCreature();
                        if (player->GetQuestStatus(12698) == QUEST_STATUS_INCOMPLETE)
                            creature->CastSpell(owner, 52517, true);

                        /// @todo Creatures must not be removed, but, must instead
                        //      stand next to Gothik and be commanded into the pit
                        //      and dig into the ground.
                        creature->DespawnOrUnsummon();

                        if (player->GetQuestStatus(12698) == QUEST_STATUS_COMPLETE)
                            owner->RemoveAllMinionsByEntry(NPC_GHOSTS);
                    }
                }
            }
        }
    };

};

struct npc_scarlet_ghoul : public ScriptedAI
{
    npc_scarlet_ghoul(Creature* creature) : ScriptedAI(creature)
    {
        me->SetReactState(REACT_DEFENSIVE);
    }

    void JustAppeared() override
    {
        CreatureAI::JustAppeared();

        if (urand(0, 1))
            if (Unit* owner = me->GetOwner())
                Talk(SAY_LINE_0, owner);
    }

    void FindMinions(Unit* owner)
    {
        std::list<Creature*> MinionList;
        owner->GetAllMinionsByEntry(MinionList, NPC_GHOULS);

        if (!MinionList.empty())
        {
            for (Creature* creature : MinionList)
            {
                if (creature->GetOwner()->GetGUID() == me->GetOwner()->GetGUID())
                {
                    if (creature->IsInCombat() && creature->getAttackerForHelper())
                    {
                        AttackStart(creature->getAttackerForHelper());
                    }
                }
            }
        }
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (!me->IsInCombat())
        {
            if (Unit* owner = me->GetOwner())
            {
                Player* plrOwner = owner->ToPlayer();
                if (plrOwner && plrOwner->IsInCombat())
                {
                    if (plrOwner->getAttackerForHelper() && plrOwner->getAttackerForHelper()->GetEntry() == NPC_GHOSTS)
                        AttackStart(plrOwner->getAttackerForHelper());
                    else
                        FindMinions(owner);
                }
            }
        }

        if (!UpdateVictim() || !me->GetVictim())
            return;

        //ScriptedAI::UpdateAI(diff);
        //Check if we have a current target
        if (me->EnsureVictim()->GetEntry() == NPC_GHOSTS)
        {
            if (me->isAttackReady())
            {
                //If we are within range melee the target
                if (me->IsWithinMeleeRange(me->GetVictim()))
                {
                    me->AttackerStateUpdate(me->GetVictim());
                    me->resetAttackTimer();
                }
            }
        }
    }
};

enum GiftOfTheHarvester
{
    SPELL_GHOUL_TRANFORM    = 52490,
    SPELL_GHOST_TRANSFORM   = 52505
};

// 52479 - Gift of the Harvester
class spell_gift_of_the_harvester : public SpellScript
{
    PrepareSpellScript(spell_gift_of_the_harvester);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_GHOUL_TRANFORM,
            SPELL_GHOST_TRANSFORM
        });
    }

    void HandleScriptEffect(SpellEffIndex /*effIndex*/)
    {
        Unit* originalCaster = GetOriginalCaster();
        Unit* target = GetHitUnit();

        if (originalCaster && target)
            originalCaster->CastSpell(target, RAND(SPELL_GHOUL_TRANFORM, SPELL_GHOST_TRANSFORM), true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_gift_of_the_harvester::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/*######
## Quest 12842: Runeforging: Preparation For Battle
######*/

enum Runeforging
{
    SPELL_RUNEFORGING_CREDIT     = 54586,
    QUEST_RUNEFORGING            = 12842
};

/* 53323 - Rune of Swordshattering
   53331 - Rune of Lichbane
   53341 - Rune of Cinderglacier
   53342 - Rune of Spellshattering
   53343 - Rune of Razorice
   53344 - Rune of the Fallen Crusader
   54446 - Rune of Swordbreaking
   54447 - Rune of Spellbreaking
   62158 - Rune of the Stoneskin Gargoyle
   70164 - Rune of the Nerubian Carapace */
class spell_chapter1_runeforging_credit : public SpellScript
{
    PrepareSpellScript(spell_chapter1_runeforging_credit);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_RUNEFORGING_CREDIT }) &&
            sObjectMgr->GetQuestTemplate(QUEST_RUNEFORGING);
    }

    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Player* caster = GetCaster()->ToPlayer())
            if (caster->GetQuestStatus(QUEST_RUNEFORGING) == QUEST_STATUS_INCOMPLETE)
                caster->CastSpell(caster, SPELL_RUNEFORGING_CREDIT);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_chapter1_runeforging_credit::HandleDummy, EFFECT_1, SPELL_EFFECT_DUMMY);
    }
};

void AddSC_the_scarlet_enclave_c1()
{
    new npc_unworthy_initiate();
    new npc_unworthy_initiate_anchor();
    new go_acherus_soul_prison();
    RegisterSpellScript(spell_death_knight_initiate_visual);
    RegisterCreatureAI(npc_eye_of_acherus);
    new npc_death_knight_initiate();
    RegisterCreatureAI(npc_dark_rider_of_acherus);
    new npc_salanar_the_horseman();
    RegisterSpellScript(spell_stable_master_repo);
    RegisterSpellScript(spell_deliver_stolen_horse);
    new npc_ros_dark_rider();
    new npc_dkc1_gothik();
    RegisterCreatureAI(npc_scarlet_ghoul);
    RegisterSpellScript(spell_gift_of_the_harvester);
    RegisterSpellScript(spell_chapter1_runeforging_credit);
}
