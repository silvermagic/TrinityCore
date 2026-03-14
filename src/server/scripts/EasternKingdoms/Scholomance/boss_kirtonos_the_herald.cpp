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
 * @file boss_kirtonos_the_herald.cpp
 * @brief 通灵学院BOSS传令官基尔图诺斯战斗脚本
 *
 * 本模块实现了通灵学院BOSS传令官基尔图诺斯的战斗AI：
 * - 传令官基尔图诺斯（可以变身为飞行形态）
 * - 俯冲、翅膀拍击、穿刺护甲、缴械等技能
 * - 暗影箭、诅咒、精神控制等法术技能
 * - 变形机制（人形态/飞行形态切换）
 *
 * 战斗机制：
 * 1. 俯冲：飞行形态技能，造成物理伤害
 * 2. 翅膀拍击：飞行形态技能，击退附近敌人
 * 3. 穿刺护甲：降低目标护甲
 * 4. 缴械：使目标无法使用武器
 * 5. 暗影箭：远程暗影伤害
 * 6. 诅咒：降低施法速度
 * 7. 精神控制：控制一个玩家
 * 8. 变形：在人形态和飞行形态之间切换
 *
 * 特殊机制：
 * - 通过点火盆召唤BOSS
 * - BOSS会飞入房间并变形为人形态
 * - 战斗中会定期变形切换形态
 * - 死亡或脱战后会打开门和重置火盆
 *
 * 完成度：100%
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "MoveSplineInit.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "scholomance.h"
#include "ScriptedCreature.h"

/**
 * @brief 对话文本ID枚举
 *
 * 定义基尔图诺斯在战斗中的对话文本ID
 */
enum Says
{
   EMOTE_SUMMONED                     = 0     // 被召唤时的表情提示
};

/**
 * @brief 法术ID枚举
 *
 * 定义基尔图诺斯使用的所有法术ID
 */
enum Spells
{
    SPELL_SWOOP                       = 18144,  // 俯冲 - 飞行形态技能
    SPELL_WING_FLAP                   = 12882,  // 翅膀拍击 - 飞行形态技能
    SPELL_PIERCE_ARMOR                = 6016,   // 穿刺护甲 - 降低护甲
    SPELL_DISARM                      = 8379,   // 缴械 - 使目标无法使用武器
    SPELL_KIRTONOS_TRANSFORM          = 16467,  // 变形 - 人形态/飞行形态切换
    SPELL_SHADOW_BOLT                 = 17228,  // 暗影箭 - 暗影伤害
    SPELL_CURSE_OF_TONGUES            = 12889,  // 诅咒 - 降低施法速度
    SPELL_DOMINATE_MIND               = 14515   // 精神控制 - 控制目标
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，包括开场动画和战斗技能
 */
enum Events
{
    INTRO_1                           = 1,      // 开场动画阶段1：开始飞行路径
    INTRO_2                           = 2,      // 开场动画阶段2：飞向降落点
    INTRO_3                           = 3,      // 开场动画阶段3：锁门
    INTRO_4                           = 4,      // 开场动画阶段4：点火盆、变形
    INTRO_5                           = 5,      // 开场动画阶段5：咆哮、装备武器
    INTRO_6                           = 6,      // 开场动画阶段6：走向战斗位置
    EVENT_SWOOP                       = 7,      // 俯冲技能事件
    EVENT_WING_FLAP                   = 8,      // 翅膀拍击技能事件
    EVENT_PIERCE_ARMOR                = 9,      // 穿刺护甲技能事件
    EVENT_DISARM                      = 10,     // 缴械技能事件
    EVENT_SHADOW_BOLT                 = 11,     // 暗影箭技能事件
    EVENT_CURSE_OF_TONGUES            = 12,     // 诅咒技能事件
    EVENT_DOMINATE_MIND               = 13,     // 精神控制技能事件
    EVENT_KIRTONOS_TRANSFORM          = 14      // 变形技能事件
};

/**
 * @brief 杂项枚举
 *
 * 定义武器ID、路径点和路径ID等杂项常量
 */
enum Misc
{
    WEAPON_KIRTONOS_STAFF             = 11365,  // 基尔图诺斯法杖ID
    POINT_KIRTONOS_LAND               = 13,     // 降落路径点
    KIRTONOS_PATH                     = 105061  // 飞行路径ID
};

/**
 * @brief 移动位置数组
 *
 * 定义基尔图诺斯在开场动画中的移动位置
 */
Position const PosMove[2] =
{
    { 299.4884f, 92.76137f, 105.6335f, 0.0f },  // 位置1：降落后的位置
    { 314.8673f, 90.30210f, 101.6459f, 0.0f }   // 位置2：战斗位置
};

/**
 * @brief 传令官基尔图诺斯脚本类
 *
 * 实现基尔图诺斯的战斗AI，包括：
 * - 复杂的开场动画（飞行入场、变形）
 * - 飞行形态和人形态技能
 * - 形态切换机制
 */
class boss_kirtonos_the_herald : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册脚本名称
         */
        boss_kirtonos_the_herald() : CreatureScript("boss_kirtonos_the_herald") { }

        /**
         * @brief 传令官基尔图诺斯AI结构体
         *
         * 实现基尔图诺斯的战斗AI逻辑，继承自BossAI
         */
        struct boss_kirtonos_the_heraldAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_kirtonos_the_heraldAI(Creature* creature) : BossAI(creature, DATA_KIRTONOS) { }

            /**
             * @brief 重置AI状态
             *
             * 当战斗重置时调用：
             * - 重置BOSS状态
             *
             * 调用时机：战斗重置或BOSS脱离战斗时
             */
            void Reset() override
            {
                _Reset();
            }

            /**
             * @brief 进入战斗事件
             * @param who 进入战斗的目标
             *
             * 当基尔图诺斯进入战斗时：
             * - 安排俯冲技能（8秒后）
             * - 安排翅膀拍击技能（15秒后）
             * - 安排穿刺护甲技能（18秒后）
             * - 安排缴械技能（22秒后）
             * - 安排暗影箭技能（42秒后）
             * - 安排诅咒技能（53秒后）
             * - 安排精神控制技能（34-48秒后）
             * - 安排变形技能（20秒后）
             *
             * 调用时机：BOSS进入战斗时
             */
            void JustEngagedWith(Unit* who) override
            {
                events.ScheduleEvent(EVENT_SWOOP, 8s, 8s);
                events.ScheduleEvent(EVENT_WING_FLAP, 15s, 15s);
                events.ScheduleEvent(EVENT_PIERCE_ARMOR, 18s, 18s);
                events.ScheduleEvent(EVENT_DISARM, 22s, 22s);
                events.ScheduleEvent(EVENT_SHADOW_BOLT, 42s, 42s);
                events.ScheduleEvent(EVENT_CURSE_OF_TONGUES, 53s, 53s);
                events.ScheduleEvent(EVENT_DOMINATE_MIND, 34s, 48s);
                events.ScheduleEvent(EVENT_KIRTONOS_TRANSFORM, 20s, 20s);
                BossAI::JustEngagedWith(who);
            }

            /**
             * @brief 死亡事件
             * @param killer 击杀者
             *
             * 当基尔图诺斯死亡时：
             * - 打开基尔图诺斯大门
             * - 重置火盆状态
             *
             * 调用时机：BOSS被击杀时
             */
            void JustDied(Unit* /*killer*/) override
            {
                // 打开基尔图诺斯大门
                if (GameObject* gate = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_GATE_KIRTONOS)))
                    gate->SetGoState(GO_STATE_ACTIVE);
                // 重置火盆
                if (GameObject* brazier = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_BRAZIER_OF_THE_HERALD)))
                {
                    brazier->ResetDoorOrButton();
                    brazier->SetGoState(GO_STATE_READY);
                }
                _JustDied();
            }

            /**
             * @brief 进入脱战模式
             * @param why 脱战原因
             *
             * 当基尔图诺斯脱离战斗时：
             * - 打开基尔图诺斯大门
             * - 重置火盆状态
             * - 5秒后消失
             *
             * 调用时机：BOSS脱离战斗时
             */
            void EnterEvadeMode(EvadeReason /*why*/) override
            {
                // 打开基尔图诺斯大门
                if (GameObject* gate = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_GATE_KIRTONOS)))
                    gate->SetGoState(GO_STATE_ACTIVE);
                // 重置火盆
                if (GameObject* brazier = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_BRAZIER_OF_THE_HERALD)))
                {
                    brazier->ResetDoorOrButton();
                    brazier->SetGoState(GO_STATE_READY);
                }
                me->DespawnOrUnsummon(5s);
            }

            /**
             * @brief 被召唤事件
             * @param summoner 召唤者
             *
             * 当基尔图诺斯被召唤时（玩家点火盆）：
             * - 安排开场动画阶段1（0.5秒后）
             * - 设置为飞行状态
             * - 设置为被动反应
             * - 设置为不可攻击、不可交互
             * - 发出被召唤的表情提示
             *
             * 调用时机：玩家点击火盆召唤BOSS时
             */
            void IsSummonedBy(WorldObject* /*summoner*/) override
            {
                events.ScheduleEvent(INTRO_1, 500ms);
                me->SetDisableGravity(true);  // 启用无重力模式（飞行）
                me->SetReactState(REACT_PASSIVE);  // 设置为被动反应
                me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE|UNIT_FLAG_UNINTERACTIBLE);  // 不可攻击、不可交互
                Talk(EMOTE_SUMMONED);  // 发出被召唤的表情提示
            }

            /**
             * @brief 召唤生物事件
             * @param summon 被召唤的生物
             *
             * 当召唤生物时调用
             *
             * 调用时机：召唤生物时
             */
            void JustSummoned(Creature* summon) override
            {
                BossAI::JustSummoned(summon);
            }

            /**
             * @brief 移动信息通知
             * @param type 移动类型
             * @param id 移动点ID
             *
             * 当BOSS移动到特定点时调用：
             * - 检查是否到达降落点
             * - 安排开场动画阶段2
             *
             * 调用时机：BOSS到达移动目标点时
             */
            void MovementInform(uint32 type, uint32 id) override
            {
                // 如果是路径点移动且到达降落点
                if (type == WAYPOINT_MOTION_TYPE && id == POINT_KIRTONOS_LAND)
                    events.ScheduleEvent(INTRO_2, 1500ms);
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 每帧调用，处理战斗逻辑：
             * - 更新事件计时器
             * - 处理开场动画（6个阶段）
             * - 检查是否有战斗目标
             * - 检查施法状态
             * - 执行技能事件（俯冲、翅膀拍击、穿刺护甲、缴械、暗影箭、诅咒、精神控制、变形）
             * - 进行近战攻击
             *
             * 开场动画流程：
             * 1. INTRO_1：开始沿飞行路径移动
             * 2. INTRO_2：飞向降落点
             * 3. INTRO_3：关闭大门
             * 4. INTRO_4：点火盆、变形为人形态
             * 5. INTRO_5：咆哮、装备武器、移除不可攻击标记
             * 6. INTRO_6：走向战斗位置
             *
             * 调用时机：每帧由核心代码调用
             */
            void UpdateAI(uint32 diff) override
            {
                events.Update(diff);

                if (!UpdateVictim())  // 没有战斗目标，处理开场动画
                {
                    while (uint32 eventId = events.ExecuteEvent())
                    {
                        switch (eventId)
                        {
                            case INTRO_1:
                                // 阶段1：开始沿飞行路径移动
                                me->GetMotionMaster()->MovePath(KIRTONOS_PATH, false);
                                break;
                            case INTRO_2:
                                // 阶段2：飞向降落点
                                me->GetMotionMaster()->MovePoint(0, PosMove[0]);
                                events.ScheduleEvent(INTRO_3, 1s);
                                break;
                            case INTRO_3:
                                // 阶段3：关闭大门
                                if (GameObject* gate = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_GATE_KIRTONOS)))
                                    gate->SetGoState(GO_STATE_READY);
                                me->SetFacingTo(0.01745329f);  // 面向指定方向
                                events.ScheduleEvent(INTRO_4, 3s);
                                break;
                            case INTRO_4:
                                // 阶段4：点火盆、变形为人形态
                                if (GameObject* brazier = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_BRAZIER_OF_THE_HERALD)))
                                    brazier->SetGoState(GO_STATE_READY);
                                me->SetWalk(true);  // 设置为行走模式
                                me->SetDisableGravity(false);  // 禁用无重力模式
                                DoCast(me, SPELL_KIRTONOS_TRANSFORM);  // 变形为人形态
                                me->SetCanFly(false);  // 取消飞行能力
                                events.ScheduleEvent(INTRO_5, 1s);
                                break;
                            case INTRO_5:
                                // 阶段5：咆哮、装备武器、移除不可攻击标记
                                me->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);  // 咆哮表情
                                me->SetVirtualItem(0, uint32(WEAPON_KIRTONOS_STAFF));  // 装备法杖
                                me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE|UNIT_FLAG_UNINTERACTIBLE);  // 移除不可攻击、不可交互标记
                                me->SetReactState(REACT_AGGRESSIVE);  // 设置为攻击性反应
                                events.ScheduleEvent(INTRO_6, 5s);
                                break;
                            case INTRO_6:
                                // 阶段6：走向战斗位置
                                me->GetMotionMaster()->MovePoint(0, PosMove[1]);
                                break;
                            default:
                                break;
                        }
                    }

                    return;
                }

                if (me->HasUnitState(UNIT_STATE_CASTING))  // 正在施法则等待
                    return;

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_SWOOP:
                            // 对自己施放俯冲
                            DoCast(me, SPELL_SWOOP);
                            events.ScheduleEvent(EVENT_SWOOP, 15s);  // 15秒后再次施放
                            break;
                        case EVENT_WING_FLAP:
                            // 对自己施放翅膀拍击
                            DoCast(me, SPELL_WING_FLAP);
                            events.ScheduleEvent(EVENT_WING_FLAP, 13s);  // 13秒后再次施放
                            break;
                        case EVENT_PIERCE_ARMOR:
                            // 对当前目标施放穿刺护甲
                            DoCastVictim(SPELL_PIERCE_ARMOR, true);
                            events.ScheduleEvent(EVENT_PIERCE_ARMOR, 12s);  // 12秒后再次施放
                            break;
                        case EVENT_DISARM:
                            // 对当前目标施放缴械
                            DoCastVictim(SPELL_DISARM, true);
                            events.ScheduleEvent(EVENT_DISARM, 11s);  // 11秒后再次施放
                            break;
                        case EVENT_SHADOW_BOLT:
                            // 对当前目标施放暗影箭
                            DoCastVictim(SPELL_SHADOW_BOLT, true);
                            events.ScheduleEvent(EVENT_SHADOW_BOLT, 42s);  // 42秒后再次施放
                            break;
                        case EVENT_CURSE_OF_TONGUES:
                            // 对当前目标施放诅咒
                            DoCastVictim(SPELL_CURSE_OF_TONGUES, true);
                            events.ScheduleEvent(EVENT_CURSE_OF_TONGUES, 35s);  // 35秒后再次施放
                            break;
                        case EVENT_DOMINATE_MIND:
                            // 对当前目标施放精神控制
                            DoCastVictim(SPELL_DOMINATE_MIND, true);
                            events.ScheduleEvent(EVENT_DOMINATE_MIND, 44s, 48s);  // 44-48秒后再次施放
                            break;
                        case EVENT_KIRTONOS_TRANSFORM:
                            // 形态切换
                            if (me->HasAura(SPELL_KIRTONOS_TRANSFORM))
                            {
                                // 如果有人形态光环，切换为飞行形态
                                me->RemoveAura(SPELL_KIRTONOS_TRANSFORM);  // 移除人形态光环
                                me->SetVirtualItem(0, uint32(0));  // 卸下武器
                                me->SetCanFly(false);  // 取消飞行能力
                            }
                            else
                            {
                                // 如果没有形态光环，切换为人形态
                                DoCast(me, SPELL_KIRTONOS_TRANSFORM);  // 施放变形法术
                                me->SetVirtualItem(0, uint32(WEAPON_KIRTONOS_STAFF));  // 装备法杖
                                me->SetCanFly(true);  // 启用飞行能力
                            }
                            events.ScheduleEvent(EVENT_KIRTONOS_TRANSFORM, 16s, 18s);  // 16-18秒后再次变形
                            break;
                        default:
                            break;
                    }

                    if (me->HasUnitState(UNIT_STATE_CASTING))
                        return;
                }

                DoMeleeAttackIfReady();  // 如果可以，进行近战攻击
            }
        };

        /**
         * @brief 获取AI
         * @param creature 生物对象指针
         * @return AI对象指针
         *
         * 创建并返回基尔图诺斯AI对象
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetScholomanceAI<boss_kirtonos_the_heraldAI>(creature);
        }
};

/**
 * @brief 传令官火盆相关枚举
 *
 * 定义传令官火盆涉及的NPC和音效ID
 */
enum Brazier_Of_The_Herald
{
    NPC_KIRTONOS  = 10506,  // 传令官基尔图诺斯NPC ID
    SOUND_SCREECH = 557     // 尖叫声音效ID
};

/**
 * @brief 召唤位置数组
 *
 * 定义基尔图诺斯的召唤位置
 */
Position const PosSummon[1] =
{
    { 315.028f, 70.53845f, 102.1496f, 0.3859715f }  // 召唤位置
};

/**
 * @brief 传令官火盆游戏对象脚本
 *
 * 实现传令官火盆的交互逻辑：
 * - 玩家点击火盆后召唤基尔图诺斯
 * - 播放尖叫声音效
 */
class go_brazier_of_the_herald : public GameObjectScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册游戏对象脚本名称
         */
        go_brazier_of_the_herald() : GameObjectScript("go_brazier_of_the_herald") { }

        /**
         * @brief 传令官火盆AI结构体
         *
         * 实现传令官火盆的游戏对象AI逻辑，继承自GameObjectAI
         */
        struct go_brazier_of_the_heraldAI : public GameObjectAI
        {
            /**
             * @brief 构造函数
             * @param go 游戏对象指针
             */
            go_brazier_of_the_heraldAI(GameObject* go) : GameObjectAI(go) { }

            /**
             * @brief 玩家点击游戏对象事件
             * @param player 点击的玩家
             * @return 是否成功处理
             *
             * 当玩家点击火盆时：
             * - 激活火盆（播放开启动画）
             * - 播放尖叫声音效
             * - 召唤基尔图诺斯（死亡后消失，存在时间15分钟）
             *
             * 调用时机：玩家右键点击火盆时
             */
            bool OnGossipHello(Player* player) override
            {
                me->UseDoorOrButton();  // 激活火盆（播放开启动画）
                me->PlayDirectSound(SOUND_SCREECH, 0);  // 播放尖叫声音效
                // 召唤基尔图诺斯（死亡后消失，存在时间15分钟）
                player->SummonCreature(NPC_KIRTONOS, PosSummon[0], TEMPSUMMON_DEAD_DESPAWN, 15min);
                return true;
            }
        };

        /**
         * @brief 获取AI
         * @param go 游戏对象指针
         * @return AI对象指针
         *
         * 创建并返回传令官火盆AI对象
         */
        GameObjectAI* GetAI(GameObject* go) const override
        {
            return GetScholomanceAI<go_brazier_of_the_heraldAI>(go);
        }
};

/**
 * @brief 注册BOSS和游戏对象脚本
 *
 * 将基尔图诺斯AI和传令官火盆脚本注册到脚本系统中
 */
void AddSC_boss_kirtonos_the_herald()
{
    new boss_kirtonos_the_herald();     // 注册基尔图诺斯BOSS脚本
    new go_brazier_of_the_herald;       // 注册传令官火盆游戏对象脚本
}
