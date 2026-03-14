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

/* ScriptData
SDName: Razorfen_Downs
SD%Complete: 100
SDComment: Support for Henry Stern(2 recipes)
SDCategory: Razorfen Downs
EndScriptData */

/* ContentData
npc_henry_stern
EndContentData */

/**
 * @file razorfen_downs.cpp
 * @brief 剃刀高地副本内的NPC和游戏对象脚本
 *
 * 本模块实现了剃刀高地副本内的各种NPC和游戏对象脚本：
 *
 * 1. NPC Belnistrasz：
 *    - 任务"熄灭偶像"（3525）的护送任务NPC
 *    - 引导玩家到偶像位置并保护他完成仪式
 *    - 仪式期间召唤多波敌人攻击
 *
 * 2. NPC Idol Room Spawner：
 *    - 仪式期间生成敌人的刷怪点
 *    - 最后一波召唤BOSS Plaguemaw the Rotting
 *
 * 3. NPC Tomb Creature（墓穴生物）：
 *    - 锣事件中的小怪（墓穴恶魔和墓穴掠夺者）
 *    - 死亡时通知实例脚本推进波次
 *
 * 4. GO Gong（锣）：
 *    - 点击后触发Tuten'kash召唤事件
 */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "GameObjectAI.h"
#include "GridNotifiersImpl.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "Player.h"
#include "razorfen_downs.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "TemporarySummon.h"

/*#####
## npc_belnistrasz for Quest 3525 "Extinguishing the Idol"
######*/

/**
 * @brief 偶像房间刷怪点召唤位置数组
 *
 * 定义了熄灭偶像仪式中召唤怪物的三个刷怪点位置
 */
Position const PosSummonSpawner[3] =
{
    { 2582.789f, 954.3925f, 52.48214f, 3.787364f  },
    { 2569.42f,  956.3801f, 52.27323f, 5.427974f  },
    { 2570.62f,  942.3934f, 53.7433f,  0.715585f  }
};

/**
 * @brief Belnistrasz相关枚举
 */
enum Belnistrasz
{
    // 事件ID
    EVENT_CHANNEL                = 1,   ///< 引导事件：开始引导熄灭偶像
    EVENT_IDOL_ROOM_SPAWNER      = 2,   ///< 刷怪事件：召唤新一批敌人
    EVENT_PROGRESS               = 3,   ///< 进度事件：检查仪式进度
    EVENT_COMPLETE               = 4,   ///< 完成事件：仪式完成
    EVENT_FIREBALL               = 5,   ///< 火球术事件：战斗中使用
    EVENT_FROST_NOVA             = 6,   ///< 冰霜新星事件：战斗中使用

    // 路径和路径点
    PATH_ESCORT                  = 871710,  ///< 护送路径ID
    POINT_REACH_IDOL             = 17,      ///< 到达偶像位置的路径点ID

    // 任务ID
    QUEST_EXTINGUISHING_THE_IDOL = 3525,    ///< 熄灭偶像任务ID

    // 对话ID
    SAY_QUEST_ACCEPTED           = 0,   ///< 任务接受喊话
    SAY_EVENT_START              = 1,   ///< 仪式开始喊话
    SAY_EVENT_THREE_MIN_LEFT     = 2,   ///< 剩余3分钟喊话
    SAY_EVENT_TWO_MIN_LEFT       = 3,   ///< 剩余2分钟喊话
    SAY_EVENT_ONE_MIN_LEFT       = 4,   ///< 剩余1分钟喊话
    SAY_EVENT_END                = 5,   ///< 仪式结束喊话
    SAY_AGGRO                    = 6,   ///< 进入战斗喊话
    SAY_WATCH_OUT                = 7,   ///< 警告喊话（25%几率在波次刷新时触发）

    // 法术ID
    SPELL_ARCANE_INTELLECT       = 13326,  ///< 奥术智慧：提高智力
    SPELL_FIREBALL               = 9053,   ///< 火球术：造成火焰伤害
    SPELL_FROST_NOVA             = 11831,  ///< 冰霜新星：冻结周围敌人
    SPELL_IDOL_SHUTDOWN_VISUAL   = 12774,  ///< 偶像关闭视觉效果：击中单位Entry 8662
    SPELL_IDOM_ROOM_CAMERA_SHAKE = 12816   ///< 偶像房间镜头震动：需要脚本支持的视觉效果
};

/**
 * @class npc_belnistrasz
 * @brief Belnistrasz NPC脚本类
 *
 * Belnistrasz是任务"熄灭偶像"的护送任务NPC。
 * 玩家需要保护他到达偶像位置并完成4分钟的仪式。
 * 仪式期间会有多波敌人攻击。
 */
class npc_belnistrasz : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    npc_belnistrasz() : CreatureScript("npc_belnistrasz") { }

    /**
     * @class npc_belnistraszAI
     * @brief Belnistrasz的AI实现类
     *
     * 实现了Belnistrasz的护送和仪式逻辑：
     * - 接受任务后开始护送到偶像位置
     * - 到达后开始引导仪式
     * - 仪式期间召唤多波敌人
     * - 仪式完成后给予任务奖励并消失
     * - 战斗中使用火球术和冰霜新星
     */
    struct npc_belnistraszAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物指针
         */
        npc_belnistraszAI(Creature* creature) : ScriptedAI(creature)
        {
            instance = creature->GetInstanceScript();
            eventInProgress = false;    ///< 事件是否进行中
            channeling = false;         ///< 是否正在引导仪式
            eventProgress = 0;          ///< 事件进度（用于倒计时）
            spawnerCount = 0;           ///< 已召唤的刷怪波次计数
        }

        /**
         * @brief 重置NPC状态
         *
         * 当NPC脱离战斗或重置时调用
         * 如果事件未进行，恢复初始状态（添加奥术智慧光环、设置任务给予者标志）
         * @调用时机 战斗重置、NPC脱战
         */
        void Reset() override
        {
            if (!eventInProgress)
            {
                // 如果没有奥术智慧光环则施放
                if (!me->HasAura(SPELL_ARCANE_INTELLECT))
                    DoCastSelf(SPELL_ARCANE_INTELLECT);

                channeling = false;
                eventProgress = 0;
                spawnerCount  = 0;
                me->SetNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
            }
        }

        /**
         * @brief 进入战斗回调
         * @param who 仇恨目标
         *
         * 如果正在引导仪式，发出警告喊话；
         * 否则安排战斗技能事件
         * @调用时机 NPC进入战斗
         */
        void JustEngagedWith(Unit* who) override
        {
            if (channeling)
                Talk(SAY_WATCH_OUT, who);
            else
            {
                events.ScheduleEvent(EVENT_FIREBALL, 1s);
                events.ScheduleEvent(EVENT_FROST_NOVA, 8s, 12s);
                if (urand(0, 100) > 40)
                    Talk(SAY_AGGRO, who);
            }
        }

        /**
         * @brief 死亡回调
         * @param killer 击杀者（未使用）
         *
         * 当NPC死亡时，标记任务完成并消失
         * @调用时机 NPC死亡时
         */
        void JustDied(Unit* /*killer*/) override
        {
            instance->SetBossState(DATA_EXTINGUISHING_THE_IDOL, DONE);
            me->DespawnOrUnsummon(5s);
        }

        /**
         * @brief 任务接受回调
         * @param player 接受任务的玩家
         * @param quest 任务对象
         *
         * 当玩家接受"熄灭偶像"任务时，开始护送事件
         * NPC移除任务给予者标志，设置阵营，开始沿路径移动
         * @调用时机 玩家接受任务时
         */
        void OnQuestAccept(Player* /*player*/, Quest const* quest) override
        {
            if (quest->GetQuestId() == QUEST_EXTINGUISHING_THE_IDOL)
            {
                eventInProgress = true;
                Talk(SAY_QUEST_ACCEPTED);
                me->RemoveNpcFlag(UNIT_NPC_FLAG_QUESTGIVER);
                me->SetFaction(FACTION_ESCORTEE_N_NEUTRAL_ACTIVE);
                me->GetMotionMaster()->MovePath(PATH_ESCORT, false);
            }
        }

        /**
         * @brief 移动到达回调
         * @param type 移动类型
         * @param id 路径点ID
         *
         * 当NPC到达偶像位置时，开始引导仪式
         * @调用时机 NPC到达路径点时
         */
        void MovementInform(uint32 type, uint32 id) override
        {
            if (type == WAYPOINT_MOTION_TYPE && id == POINT_REACH_IDOL)
            {
                channeling = true;
                events.ScheduleEvent(EVENT_CHANNEL, 2s);
            }
        }

        /**
         * @brief 更新AI
         * @param diff 时间差（毫秒）
         *
         * 每帧调用，处理仪式和战斗逻辑
         * @调用时机 每帧更新
         * @性能注意事项 该函数每帧调用，需保持高效
         */
        void UpdateAI(uint32 diff) override
        {
            if (!eventInProgress)
                return;

            events.Update(diff);

            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_CHANNEL:
                        // 开始引导仪式
                        Talk(SAY_EVENT_START);
                        DoCastSelf(SPELL_IDOL_SHUTDOWN_VISUAL);
                        events.ScheduleEvent(EVENT_IDOL_ROOM_SPAWNER, 100ms);
                        events.ScheduleEvent(EVENT_PROGRESS, 120s);
                        break;
                    case EVENT_IDOL_ROOM_SPAWNER:
                        // 在随机位置召唤刷怪点
                        if (Creature* creature = me->SummonCreature(NPC_IDOL_ROOM_SPAWNER, PosSummonSpawner[urand(0,2)], TEMPSUMMON_TIMED_DESPAWN, 4s))
                            creature->AI()->SetData(0, spawnerCount);
                        // 最多召唤8波
                        if (++spawnerCount < 8)
                            events.ScheduleEvent(EVENT_IDOL_ROOM_SPAWNER, 35s);
                        break;
                    case EVENT_PROGRESS:
                    {
                        // 检查仪式进度并喊话
                        switch (eventProgress)
                        {
                            case 0:
                                Talk(SAY_EVENT_THREE_MIN_LEFT);
                                ++eventProgress;
                                events.ScheduleEvent(EVENT_PROGRESS, 1min);
                                break;
                            case 1:
                                Talk(SAY_EVENT_TWO_MIN_LEFT);
                                ++eventProgress;
                                events.ScheduleEvent(EVENT_PROGRESS, 1min);
                                break;
                            case 2:
                                Talk(SAY_EVENT_ONE_MIN_LEFT);
                                ++eventProgress;
                                events.ScheduleEvent(EVENT_PROGRESS, 1min);
                                break;
                            case 3:
                                // 仪式完成
                                events.CancelEvent(EVENT_IDOL_ROOM_SPAWNER);
                                me->InterruptSpell(CURRENT_CHANNELED_SPELL);
                                Talk(SAY_EVENT_END);
                                events.ScheduleEvent(EVENT_COMPLETE, 3s);
                                break;
                        }
                        break;
                    }
                    case EVENT_COMPLETE:
                    {
                        // 仪式完成：施放镜头震动效果，召唤任务完成物品
                        DoCastSelf(SPELL_IDOM_ROOM_CAMERA_SHAKE);
                        me->SummonGameObject(GO_BELNISTRASZS_BRAZIER, 2577.196f, 947.0781f, 53.16757f, 2.356195f, QuaternionData(0.f, 0.f, 0.9238796f, 0.3826832f), 1h, GO_SUMMON_TIMED_DESPAWN);

                        // 搜索周围玩家和游戏对象
                        std::list<WorldObject*> ClusterList;
                        Trinity::AllWorldObjectsInRange objects(me, 50.0f);
                        Trinity::WorldObjectListSearcher<Trinity::AllWorldObjectsInRange> searcher(me, ClusterList, objects);
                        Cell::VisitAllObjects(me, searcher, 50.0f);

                        for (std::list<WorldObject*>::const_iterator itr = ClusterList.begin(); itr != ClusterList.end(); ++itr)
                        {
                            if (Player* player = (*itr)->ToPlayer())
                            {
                                // 给任务未完成的玩家完成奖励
                                if (player->GetQuestStatus(QUEST_EXTINGUISHING_THE_IDOL) == QUEST_STATUS_INCOMPLETE)
                                    player->GroupEventHappens(QUEST_EXTINGUISHING_THE_IDOL, me);
                            }
                            else if (GameObject* go = (*itr)->ToGameObject())
                            {
                                // 删除偶像火焰
                                if (go->GetEntry() == GO_IDOL_OVEN_FIRE || go->GetEntry() == GO_IDOL_CUP_FIRE || go->GetEntry() == GO_IDOL_MOUTH_FIRE)
                                    go->Delete();
                            }
                        }
                        instance->SetBossState(DATA_EXTINGUISHING_THE_IDOL, DONE);
                        me->DespawnOrUnsummon();
                        break;
                    }
                    case EVENT_FIREBALL:
                        // 战斗中施放火球术
                        if (me->HasUnitState(UNIT_STATE_CASTING) || !UpdateVictim())
                            return;
                        DoCastVictim(SPELL_FIREBALL);
                        events.ScheduleEvent(EVENT_FIREBALL, 8s);
                        break;
                    case EVENT_FROST_NOVA:
                        // 战斗中施放冰霜新星
                        if (me->HasUnitState(UNIT_STATE_CASTING) || !UpdateVictim())
                            return;
                        DoCastAOE(SPELL_FROST_NOVA);
                        events.ScheduleEvent(EVENT_FROST_NOVA, 15s);
                        break;
                }
            }
            // 如果没有在引导仪式，进行近战攻击
            if (!channeling)
                DoMeleeAttackIfReady();
        }

    private:
        InstanceScript* instance;   ///< 实例脚本指针
        EventMap events;            ///< 事件管理器
        bool eventInProgress;       ///< 事件是否进行中
        bool channeling;            ///< 是否正在引导仪式
        uint8 eventProgress;        ///< 事件进度（0-3）
        uint8 spawnerCount;         ///< 已召唤的刷怪波次计数
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物指针
     * @return AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetRazorfenDownsAI<npc_belnistraszAI>(creature);
    }
};

/**
 * @class npc_idol_room_spawner
 * @brief 偶像房间刷怪点NPC脚本类
 *
 * 这是一个辅助NPC，用于在熄灭偶像仪式期间召唤敌人。
 * 根据波次计数召唤不同类型的怪物：
 * - 波次0-6：召唤小怪组合（枯萎战斗野猪、死亡之头地卜师、枯萎刺毛守卫）
 * - 波次7：召唤BOSS Plaguemaw the Rotting
 */
class npc_idol_room_spawner : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    npc_idol_room_spawner() : CreatureScript("npc_idol_room_spawner") { }

    /**
     * @class npc_idol_room_spawnerAI
     * @brief 偶像房间刷怪点的AI实现类
     *
     * 实现了刷怪逻辑，根据波次计数召唤不同的敌人组合
     */
    struct npc_idol_room_spawnerAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物指针
         */
        npc_idol_room_spawnerAI(Creature* creature) : ScriptedAI(creature)
        {
            instance = creature->GetInstanceScript();
        }

        /**
         * @brief 重置回调（空实现）
         */
        void Reset() override { }

        /**
         * @brief 设置数据回调
         * @param type 数据类型（未使用）
         * @param data 波次计数
         *
         * 根据波次计数召唤不同类型的敌人：
         * - 波次0：召唤2个枯萎战斗野猪（如果朝向<4.0）
         * - 波次1-6：召唤1个枯萎战斗野猪、1个死亡之头地卜师、1个枯萎刺毛守卫
         * - 波次7：召唤BOSS Plaguemaw the Rotting
         *
         * @调用时机 Belnistrasz仪式期间
         */
        void SetData(uint32 /*type*/, uint32 data) override
        {
            if (data < 7)
            {
                // 召唤枯萎战斗野猪（在刷怪点位置）
                me->SummonCreature(NPC_WITHERED_BATTLE_BOAR, me->GetPositionX(),  me->GetPositionY(),  me->GetPositionZ(),  me->GetOrientation());
                // 如果波次>0且朝向<4.0，召唤第二个枯萎战斗野猪
                if (data > 0 && me->GetOrientation() < 4.0f)
                    me->SummonCreature(NPC_WITHERED_BATTLE_BOAR, me->GetPositionX(),  me->GetPositionY(),  me->GetPositionZ(),  me->GetOrientation());
                // 召唤死亡之头地卜师（在左侧2码处）
                me->SummonCreature(NPC_DEATHS_HEAD_GEOMANCER, me->GetPositionX() + (std::cos(me->GetOrientation() - (float(M_PI) / 2)) * 2), me->GetPositionY() + (std::sin(me->GetOrientation() - (float(M_PI) / 2)) * 2), me->GetPositionZ(), me->GetOrientation());
                // 召唤枯萎刺毛守卫（在右侧2码处）
                me->SummonCreature(NPC_WITHERED_QUILGUARD, me->GetPositionX() + (std::cos(me->GetOrientation() + (float(M_PI) / 2)) * 2), me->GetPositionY() + (std::sin(me->GetOrientation() + (float(M_PI) / 2)) * 2), me->GetPositionZ(), me->GetOrientation());
            }
            else if (data == 7)
            {
                // 最后一波：召唤BOSS Plaguemaw the Rotting
                me->SummonCreature(NPC_PLAGUEMAW_THE_ROTTING, me->GetPositionX(),  me->GetPositionY(),  me->GetPositionZ(),  me->GetOrientation());
            }
        }

    private:
        InstanceScript* instance;   ///< 实例脚本指针
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物指针
     * @return AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetRazorfenDownsAI<npc_idol_room_spawnerAI>(creature);
    }
};

/**
 * @brief 墓穴生物相关枚举
 */
enum TombCreature
{
    EVENT_WEB                   = 7,    ///< 蛛网事件ID
    SPELL_POISON_PROC           = 3616, ///< 毒素光环：墓穴恶魔的被动毒素效果
    SPELL_VIRULENT_POISON_PROC  = 12254,///< 剧毒光环：墓穴掠夺者的被动剧毒效果
    SPELL_WEB                   = 745   ///< 蛛网：定身目标
};

/**
 * @class npc_tomb_creature
 * @brief 墓穴生物NPC脚本类
 *
 * 墓穴生物是锣事件中的小怪，包括：
 * - 墓穴恶魔（Tomb Fiend）：具有毒素光环，使用蛛网定身
 * - 墓穴掠夺者（Tomb Reaver）：具有剧毒光环，使用蛛网定身
 *
 * 死亡时通知实例脚本推进波次进度
 */
class npc_tomb_creature : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    npc_tomb_creature() : CreatureScript("npc_tomb_creature") { }

    /**
     * @class npc_tomb_creatureAI
     * @brief 墓穴生物的AI实现类
     *
     * 实现了墓穴生物的战斗逻辑：
     * - 墓穴恶魔施放毒素光环
     * - 墓穴掠夺者施放剧毒光环
     * - 都使用蛛网定身目标
     * - 死亡时通知实例脚本
     */
    struct npc_tomb_creatureAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物指针
         */
        npc_tomb_creatureAI(Creature* creature) : ScriptedAI(creature)
        {
            instance = creature->GetInstanceScript();
        }

        /**
         * @brief 重置回调
         *
         * 根据生物类型施放对应的毒素光环
         * @调用时机 战斗重置、NPC脱战
         */
        void Reset() override
        {
            // 墓穴恶魔施放毒素光环
            if (!me->HasAura(SPELL_POISON_PROC) && me->GetEntry() == NPC_TOMB_FIEND)
                DoCast(me, SPELL_POISON_PROC);

            // 墓穴掠夺者施放剧毒光环
            if (!me->HasAura(SPELL_VIRULENT_POISON_PROC) && me->GetEntry() == NPC_TOMB_REAVER)
                DoCast(me, SPELL_VIRULENT_POISON_PROC);
        }

        /**
         * @brief 死亡回调
         * @param killer 击杀者（未使用）
         *
         * 通知实例脚本该类型怪物被击杀，用于推进锣事件波次
         * @调用时机 NPC死亡时
         */
        void JustDied(Unit* /*killer*/) override
        {
            instance->SetData(DATA_WAVE, me->GetEntry());
        }

        /**
         * @brief 进入战斗回调
         * @param who 仇恨目标（未使用）
         *
         * 安排蛛网事件
         * @调用时机 NPC进入战斗
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            events.ScheduleEvent(EVENT_WEB, 5s, 8s);
        }

        /**
         * @brief 更新AI
         * @param diff 时间差（毫秒）
         *
         * 每帧调用，处理战斗逻辑
         * @调用时机 每帧更新
         * @性能注意事项 该函数每帧调用，需保持高效
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            events.Update(diff);

            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_WEB:
                        // 对当前目标施放蛛网
                        DoCastVictim(SPELL_WEB);
                        events.ScheduleEvent(EVENT_WEB, 7s, 16s);
                        break;
                }
            }
            DoMeleeAttackIfReady();
        }

    private:
        InstanceScript* instance;   ///< 实例脚本指针
        EventMap events;            ///< 事件管理器
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物指针
     * @return AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetRazorfenDownsAI<npc_tomb_creatureAI>(creature);
    }
};

/*######
## go_gong
######*/

/**
 * @class go_gong
 * @brief 锣游戏对象脚本类
 *
 * 锣是剃刀高地中触发Tuten'kash召唤事件的游戏对象。
 * 玩家点击锣后会触发：
 * 1. 第一波：召唤10个墓穴恶魔
 * 2. 第二波：召唤4个墓穴掠夺者
 * 3. 第三波：召唤BOSS Tuten'kash
 */
class go_gong : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     */
    go_gong() : GameObjectScript("go_gong") { }

    /**
     * @class go_gongAI
     * @brief 锣的AI实现类
     *
     * 处理玩家点击锣的事件
     */
    struct go_gongAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_gongAI(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

        InstanceScript* instance;   ///< 实例脚本指针

        /**
         * @brief 玩家交互回调
         * @param player 交互的玩家（未使用）
         * @return 是否处理成功
         *
         * 当玩家点击锣时：
         * 1. 播放锣的动画
         * 2. 通知实例脚本开始新一波召唤
         *
         * @调用时机 玩家点击锣时
         */
        bool OnGossipHello(Player* /*player*/) override
        {
            me->SendCustomAnim(0);  // 播放锣的动画
            instance->SetData(DATA_WAVE, IN_PROGRESS);  // 触发新一波召唤
            return true;
        }
    };

    /**
     * @brief 获取AI实例
     * @param go 游戏对象指针
     * @return AI实例指针
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return GetRazorfenDownsAI<go_gongAI>(go);
    }
};

/**
 * @brief 注册脚本
 *
 * 将剃刀高地相关脚本注册到脚本系统
 */
void AddSC_razorfen_downs()
{
    new npc_belnistrasz();        // Belnistrasz（护送任务NPC）
    new npc_idol_room_spawner();  // 偶像房间刷怪点
    new npc_tomb_creature();      // 墓穴生物（锣事件小怪）
    new go_gong();                // 锣（触发Tuten'kash事件）
}
