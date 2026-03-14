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
 * @file blackrock_depths.cpp
 * @brief 黑石深渊副本杂项脚本实现
 *
 * 本文件包含黑石深渊副本中各种游戏对象、NPC和区域触发器的脚本实现。
 * 主要包括以下功能模块：
 * - 暗影熔炉火盆（go_shadowforge_brazier）：控制机关门的开闭
 * - 律法之环竞技场事件（at_ring_of_law, npc_grimstone）：竞技场战斗事件
 * - 法兰克斯NPC（npc_phalanx）：酒吧事件相关BOSS
 * - 洛克图斯·黑暗 bargaining者（npc_lokhtos_darkbargainer）：瑟银兄弟会军需官
 * - 罗克诺特（npc_rocknot）：酒吧事件触发NPC
 *
 * 这些脚本共同实现了黑石深渊副本中的多个关键事件和交互机制。
 */

#include "ScriptMgr.h"
#include "blackrock_depths.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "TemporarySummon.h"
#include "WorldSession.h"

/**
 * @brief 暗影熔炉火盆游戏对象脚本
 *
 * 继承自GameObjectScript，处理暗影熔炉火盆的交互逻辑。
 * 玩家点燃火盆后可以开启对应的机关门，这是进入马格姆斯区域的关键机关。
 * 有两个火盆分别控制北侧和南侧的门。
 */
class go_shadowforge_brazier : public GameObjectScript
{
    public:
        /**
         * @brief 构造函数
         * 初始化游戏对象脚本，注册名称为"go_shadowforge_brazier"
         */
        go_shadowforge_brazier() : GameObjectScript("go_shadowforge_brazier") { }

        /**
         * @brief 暗影熔炉火盆AI结构体
         *
         * 继承自GameObjectAI，管理火盆的状态和交互逻辑。
         */
        struct go_shadowforge_brazierAI : public GameObjectAI
        {
            /**
             * @brief 构造函数
             * @param go 游戏对象指针
             * 初始化AI并获取实例脚本引用
             */
            go_shadowforge_brazierAI(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

            InstanceScript* instance;  ///< 副本实例脚本指针，用于访问副本数据

            /**
             * @brief 玩家点击火盆事件处理
             * @param player 点击火盆的玩家对象（当前未使用）
             * @return 返回false表示不阻止后续处理
             *
             * 当玩家点击火盆时调用。根据当前大厅状态设置进度，
             * 并根据火盆位置开启对应的机关门。
             *
             * 调用时机：玩家右键点击火盆游戏对象时
             * 性能注意事项：轻量级操作，仅更新实例数据和操作游戏对象状态
             */
            bool OnGossipHello(Player* /*player*/) override
            {
                // 如果大厅事件正在进行，则标记为完成；否则标记为进行中
                if (instance->GetData(TYPE_LYCEUM) == IN_PROGRESS)
                    instance->SetData(TYPE_LYCEUM, DONE);
                else
                    instance->SetData(TYPE_LYCEUM, IN_PROGRESS);

                // 如果使用的是北火盆，则开启北门；如果使用的是南火盆，则开启南门
                if (me->GetGUID() == instance->GetGuidData(DATA_SF_BRAZIER_N))
                    instance->HandleGameObject(instance->GetGuidData(DATA_GOLEM_DOOR_N), true);
                else if (me->GetGUID() == instance->GetGuidData(DATA_SF_BRAZIER_S))
                    instance->HandleGameObject(instance->GetGuidData(DATA_GOLEM_DOOR_S), true);

                return false;
            }
        };

        /**
         * @brief 获取AI实例
         * @param go 游戏对象指针
         * @return 返回火盆AI实例
         */
        GameObjectAI* GetAI(GameObject* go) const override
        {
            return GetBlackrockDepthsAI<go_shadowforge_brazierAI>(go);
        }
};

/**
 * @brief 格里姆斯通NPC相关枚举
 * 定义律法之环竞技场事件中使用的关键NPC ID和数量
 */
enum Grimstone
{
    NPC_GRIMSTONE                                          = 10096,  // 格里姆斯通 - 主持竞技场事件的NPC
    NPC_THELDREN                                           = 16059,  // 瑟尔德伦 - 任务相关BOSS（待实现）

    //4 or 6 in total? 1+2+1 / 2+2+2 / 3+3. Depending on this, code should be changed.
    MAX_NPC_AMOUNT                                         = 4       // 每波小怪的最大数量
};

/**
 * @brief 竞技场小怪ID数组
 * 定义律法之环竞技场中可能出现的小怪生物ID列表
 */
uint32 RingMob[]=
{
    8925,                                                   // 挖掘蠕虫
    8926,                                                   // 深渊刺击者
    8927,                                                   // 黑暗尖叫者
    8928,                                                   // 穴居雷鸣兽
    8933,                                                   // 洞穴爬行者
    8932,                                                   // 钻孔甲虫
};

/**
 * @brief 竞技场BOSS ID数组
 * 定义律法之环竞技场最终BOSS的可能ID列表
 */
uint32 RingBoss[]=
{
    9027,                                                   // 戈洛什
    9028,                                                   // 格里兹尔
    9029,                                                   // 剔骨者
    9030,                                                   // 奥克索尔
    9031,                                                   // 阿努希亚
    9032,                                                   // 海德鲁姆
};

/**
 * @brief 律法之环区域触发器脚本
 *
 * 继承自AreaTriggerScript，检测玩家进入律法之环竞技场区域。
 * 当玩家进入时，触发竞技场事件并召唤格里姆斯通开始事件流程。
 */
class at_ring_of_law : public AreaTriggerScript
{
public:
    /**
     * @brief 构造函数
     * 初始化区域触发器脚本，注册名称为"at_ring_of_law"
     */
    at_ring_of_law() : AreaTriggerScript("at_ring_of_law") { }

    /**
     * @brief 玩家进入区域触发事件
     * @param player 进入区域的玩家指针
     * @param at 区域触发器条目数据（当前未使用）
     * @return 返回false表示不阻止后续处理
     *
     * 当玩家进入律法之环区域时调用。
     * 检查事件是否已开始或完成，如果未开始则启动事件。
     *
     * 调用时机：玩家进入律法之环区域触发器范围时
     */
    bool OnTrigger(Player* player, AreaTriggerEntry const* /*at*/) override
    {
        if (InstanceScript* instance = player->GetInstanceScript())
        {
            // 如果事件正在进行或已完成，则不重复触发
            if (instance->GetData(TYPE_RING_OF_LAW) == IN_PROGRESS || instance->GetData(TYPE_RING_OF_LAW) == DONE)
                return false;

            // 标记事件为进行中
            instance->SetData(TYPE_RING_OF_LAW, IN_PROGRESS);

            // 召唤格里姆斯通开始竞技场事件
            player->SummonCreature(NPC_GRIMSTONE, 625.559f, -205.618f, -52.735f, 2.609f, TEMPSUMMON_DEAD_DESPAWN);

            return false;
        }
        return false;
    }
};

/**
 * @brief 格里姆斯通文本ID枚举
 * 定义格里姆斯通在竞技场事件中使用的对话文本索引
 */
enum GrimstoneTexts
{
    SAY_TEXT1          = 0,  // 开场白
    SAY_TEXT2          = 1,  // 介绍竞技场规则
    SAY_TEXT3          = 2,  // 比赛进行中
    SAY_TEXT4          = 3,  // 介绍最终BOSS
    SAY_TEXT5          = 4,  // 事件开始
    SAY_TEXT6          = 5   // 最终BOSS出现前
};

/**
 * @brief 格里姆斯通NPC脚本
 *
 * 继承自CreatureScript，实现律法之环竞技场事件的主持人格里姆斯通的AI逻辑。
 * 格里姆斯通负责控制整个竞技场事件的流程，包括开关门、召唤小怪和BOSS。
 * 事件分为多个阶段：开场白 -> 小怪波次 -> 最终BOSS -> 结束。
 *
 * @todo 实现任务部分的事件（不同的最终BOSS）
 */
class npc_grimstone : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     * 初始化NPC脚本，注册名称为"npc_grimstone"
     */
    npc_grimstone() : CreatureScript("npc_grimstone") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回格里姆斯通AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetBlackrockDepthsAI<npc_grimstoneAI>(creature);
    }

    /**
     * @brief 格里姆斯通AI结构体
     *
     * 继承自EscortAI，实现竞技场事件的完整流程控制。
     * 使用多阶段事件系统管理事件的进度和生物召唤。
     */
    struct npc_grimstoneAI : public EscortAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         * 初始化成员变量并随机选择本次事件的小怪类型
         */
        npc_grimstoneAI(Creature* creature) : EscortAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
            MobSpawnId = rand32() % 6;  // 随机选择小怪类型索引（0-5）
        }

        /**
         * @brief 初始化成员变量
         * 将所有事件相关的计时器和计数器重置为初始状态
         */
        void Initialize()
        {
            EventPhase = 0;         // 事件阶段计数器
            Event_Timer = 1000;     // 事件计时器（毫秒）

            MobCount = 0;           // 当前存活的小怪数量
            MobDeath_Timer = 0;     // 小怪死亡检测计时器

            for (uint8 i = 0; i < MAX_NPC_AMOUNT; ++i)
                RingMobGUID[i].Clear();  // 清空小怪GUID数组

            RingBossGUID.Clear();   // 清空BOSS GUID

            CanWalk = false;        // 是否可以移动标志
        }

        InstanceScript* instance;              ///< 副本实例脚本指针

        uint8 EventPhase;                      ///< 当前事件阶段（0-10）
        uint32 Event_Timer;                    ///< 事件阶段计时器

        uint8 MobSpawnId;                      ///< 小怪类型索引（用于从RingMob数组中选择）
        uint8 MobCount;                        ///< 当前召唤的小怪数量
        uint32 MobDeath_Timer;                 ///< 小怪死亡检测计时器

        ObjectGuid RingMobGUID[4];             ///< 竞技场小怪GUID数组
        ObjectGuid RingBossGUID;               ///< 竞技场BOSS GUID

        bool CanWalk;                          ///< 是否允许移动标志

        /**
         * @brief 重置AI状态
         * 调用初始化函数重置所有成员变量
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 召唤竞技场小怪
         * 在指定位置召唤一只竞技场小怪，并记录其GUID
         *
         * @todo 将它们移动到中心位置
         */
        void SummonRingMob()
        {
            // 召唤小怪并记录GUID
            if (Creature* tmp = me->SummonCreature(RingMob[MobSpawnId], 608.960f, -235.322f, -53.907f, 1.857f, TEMPSUMMON_DEAD_DESPAWN))
                RingMobGUID[MobCount] = tmp->GetGUID();

            ++MobCount;

            // 如果已召唤满4只小怪，启动死亡检测计时器
            if (MobCount == MAX_NPC_AMOUNT)
                MobDeath_Timer = 2500;
        }

        /**
         * @brief 召唤竞技场BOSS
         * 随机选择并召唤一只竞技场BOSS
         *
         * @todo 将它们移动到中心位置
         */
        void SummonRingBoss()
        {
            // 随机选择BOSS并召唤
            if (Creature* tmp = me->SummonCreature(RingBoss[rand32() % 6], 644.300f, -175.989f, -53.739f, 3.418f, TEMPSUMMON_DEAD_DESPAWN))
                RingBossGUID = tmp->GetGUID();

            MobDeath_Timer = 2500;  // 启动死亡检测计时器
        }

        /**
         * @brief 到达路径点事件处理
         * @param waypointId 路径点ID
         * @param pathId 路径ID（当前未使用）
         *
         * 当格里姆斯通到达指定路径点时触发，用于推进事件流程和控制对话。
         */
        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            switch (waypointId)
            {
                case 0:
                    Talk(SAY_TEXT1);        // 开场白
                    CanWalk = false;
                    Event_Timer = 5000;
                    break;
                case 1:
                    Talk(SAY_TEXT2);        // 介绍规则
                    CanWalk = false;
                    Event_Timer = 5000;
                    break;
                case 2:
                    CanWalk = false;        // 等待小怪战斗
                    break;
                case 3:
                    Talk(SAY_TEXT3);        // 比赛进行中
                    break;
                case 4:
                    Talk(SAY_TEXT4);        // 介绍最终BOSS
                    CanWalk = false;
                    Event_Timer = 5000;
                    break;
                case 5:
                    // 标记事件完成并更新遭遇状态
                    instance->UpdateEncounterStateForKilledCreature(NPC_GRIMSTONE, me);
                    instance->SetData(TYPE_RING_OF_LAW, DONE);
                    TC_LOG_DEBUG("scripts", "npc_grimstone: event reached end and set complete.");
                    break;
            }
        }

        /**
         * @brief 操作游戏对象门
         * @param id 游戏对象数据ID
         * @param open true为开启，false为关闭
         */
        void HandleGameObject(uint32 id, bool open)
        {
            instance->HandleGameObject(instance->GetGuidData(id), open);
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 处理事件流程、小怪/BOSS死亡检测和路径移动。
         * 使用事件阶段系统逐步推进竞技场事件。
         */
        void UpdateAI(uint32 diff) override
        {
            // 小怪/BOSS死亡检测逻辑
            if (MobDeath_Timer)
            {
                if (MobDeath_Timer <= diff)
                {
                    MobDeath_Timer = 2500;

                    // 检查BOSS是否死亡
                    if (RingBossGUID)
                    {
                        Creature* boss = ObjectAccessor::GetCreature(*me, RingBossGUID);
                        if (boss && !boss->IsAlive() && boss->isDead())
                        {
                            RingBossGUID.Clear();
                            Event_Timer = 5000;
                            MobDeath_Timer = 0;
                            return;
                        }
                        return;
                    }

                    // 检查小怪是否全部死亡
                    for (uint8 i = 0; i < MAX_NPC_AMOUNT; ++i)
                    {
                        Creature* mob = ObjectAccessor::GetCreature(*me, RingMobGUID[i]);
                        if (mob && !mob->IsAlive() && mob->isDead())
                        {
                            RingMobGUID[i].Clear();
                            --MobCount;

                            // 所有小怪已死亡，继续事件流程
                            if (!MobCount)
                            {
                                Event_Timer = 5000;
                                MobDeath_Timer = 0;
                            }
                        }
                    }
                } else MobDeath_Timer -= diff;
            }

            // 事件阶段处理
            if (Event_Timer)
            {
                if (Event_Timer <= diff)
                {
                    switch (EventPhase)
                    {
                    case 0:
                        // 阶段0：事件开始，关闭竞技场门
                        Talk(SAY_TEXT5);
                        HandleGameObject(DATA_ARENA4, false);
                        Start(false, false);
                        CanWalk = true;
                        Event_Timer = 0;
                        break;
                    case 1:
                        // 阶段1：继续移动
                        CanWalk = true;
                        Event_Timer = 0;
                        break;
                    case 2:
                        // 阶段2：准备召唤小怪
                        Event_Timer = 2000;
                        break;
                    case 3:
                        // 阶段3：开启小怪门
                        HandleGameObject(DATA_ARENA1, true);
                        Event_Timer = 3000;
                        break;
                    case 4:
                        // 阶段4：隐身并召唤第1只小怪
                        CanWalk = true;
                        me->SetVisible(false);
                        SummonRingMob();
                        Event_Timer = 8000;
                        break;
                    case 5:
                        // 阶段5：召唤第2、3只小怪
                        SummonRingMob();
                        SummonRingMob();
                        Event_Timer = 8000;
                        break;
                    case 6:
                        // 阶段6：召唤第4只小怪
                        SummonRingMob();
                        Event_Timer = 5000;
                        break;
                    case 7:
                        // 阶段7：显身并关闭小怪门
                        me->SetVisible(true);
                        HandleGameObject(DATA_ARENA1, false);
                        Talk(SAY_TEXT6);
                        CanWalk = true;
                        Event_Timer = 5000;
                        break;
                    case 8:
                        // 阶段8：开启BOSS门
                        HandleGameObject(DATA_ARENA2, true);
                        Event_Timer = 5000;
                        break;
                    case 9:
                        // 阶段9：隐身并召唤BOSS
                        me->SetVisible(false);
                        SummonRingBoss();
                        Event_Timer = 0;
                        break;
                    case 10:
                        // 阶段10：事件结束，开启所有门
                        //if quest, complete
                        HandleGameObject(DATA_ARENA2, false);
                        HandleGameObject(DATA_ARENA3, true);
                        HandleGameObject(DATA_ARENA4, true);
                        CanWalk = true;
                        Event_Timer = 0;
                        break;
                    }
                    ++EventPhase;
                } else Event_Timer -= diff;
            }

            // 如果允许移动，更新护送AI
            if (CanWalk)
                EscortAI::UpdateAI(diff);
           }
    };
};

/**
 * @brief 法兰克斯法术枚举
 * 定义法兰克斯BOSS使用的法术ID
 */
enum PhalanxSpells
{
    SPELL_THUNDERCLAP                   = 8732,   // 雷霆一击 - 范围伤害并降低攻击速度
    SPELL_FIREBALLVOLLEY                = 22425,  // 火球齐射 - 范围火焰伤害
    SPELL_MIGHTYBLOW                    = 14099   // 强力打击 - 近战伤害技能
};

/**
 * @brief 法兰克斯NPC脚本
 *
 * 继承自CreatureScript，实现酒吧事件相关的BOSS法兰克斯的AI。
 * 法兰克斯原本是中立NPC，当酒吧事件触发后会变为敌对并攻击玩家。
 * 该BOSS使用雷霆一击、火球齐射和强力打击等技能。
 */
class npc_phalanx : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     * 初始化NPC脚本，注册名称为"npc_phalanx"
     */
    npc_phalanx() : CreatureScript("npc_phalanx") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回法兰克斯AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetBlackrockDepthsAI<npc_phalanxAI>(creature);
    }

    /**
     * @brief 法兰克斯AI结构体
     *
     * 继承自ScriptedAI，实现法兰克斯的战斗逻辑。
     * 法兰克斯是一个使用雷霆一击、火球齐射和强力打击的组合型BOSS。
     */
    struct npc_phalanxAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        npc_phalanxAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         * 设置技能计时器的初始值
         */
        void Initialize()
        {
            ThunderClap_Timer = 12000;    // 雷霆一击计时器（12秒）
            FireballVolley_Timer = 0;      // 火球齐射计时器（仅在血量低于50%时使用）
            MightyBlow_Timer = 15000;      // 强力打击计时器（15秒）
        }

        uint32 ThunderClap_Timer;      ///< 雷霆一击冷却计时器
        uint32 FireballVolley_Timer;   ///< 火球齐射冷却计时器
        uint32 MightyBlow_Timer;       ///< 强力打击冷却计时器

        /**
         * @brief 重置AI状态
         * 重新初始化所有计时器
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 处理技能释放逻辑：
         * - 雷霆一击：周期性释放，降低攻击速度
         * - 火球齐射：仅在血量低于51%时使用
         * - 强力打击：周期性释放的高伤害技能
         */
        void UpdateAI(uint32 diff) override
        {
            // 如果没有攻击目标，则不执行任何操作
            if (!UpdateVictim())
                return;

            // 雷霆一击计时器处理
            if (ThunderClap_Timer <= diff)
            {
                DoCastVictim(SPELL_THUNDERCLAP);
                ThunderClap_Timer = 10000;  // 10秒冷却
            } else ThunderClap_Timer -= diff;

            // 火球齐射计时器处理（仅在血量低于51%时使用）
            if (HealthBelowPct(51))
            {
                if (FireballVolley_Timer <= diff)
                {
                    DoCastVictim(SPELL_FIREBALLVOLLEY);
                    FireballVolley_Timer = 15000;  // 15秒冷却
                } else FireballVolley_Timer -= diff;
            }

            // 强力打击计时器处理
            if (MightyBlow_Timer <= diff)
            {
                DoCastVictim(SPELL_MIGHTYBLOW);
                MightyBlow_Timer = 10000;  // 10秒冷却
            } else MightyBlow_Timer -= diff;

            // 如果技能都在冷却中，执行普通攻击
            DoMeleeAttackIfReady();
        }
    };
};

/**
 * @brief 洛克图斯·黑暗 bargaining者相关枚举
 * 定义瑟银兄弟会相关的任务、物品和法术ID
 */
enum Lokhtos
{
    QUEST_A_BINDING_CONTRACT                               = 7604,   // 任务：束缚契约
    ITEM_SULFURON_INGOT                                    = 17203,  // 物品：萨弗隆锭
    ITEM_THRORIUM_BROTHERHOOD_CONTRACT                     = 18628,  // 物品：瑟银兄弟会契约
    SPELL_CREATE_THORIUM_BROTHERHOOD_CONTRACT_DND          = 23059,  // 法术：创建瑟银兄弟会契约
    GOSSIP_ITEM_SHOW_ACCESS_MID                            = 4781,   // 闲聊菜单ID：让我看看我有什么权限，洛克图斯
    GOSSIP_ITEM_SHOW_ACCESS_OID                            = 0,      // 闲聊选项ID
};

#define GOSSIP_ITEM_GET_CONTRACT    "Get Thorium Brotherhood Contract"  // 获取瑟银兄弟会契约（数据库中缺失，可能需要添加）

/**
 * @brief 洛克图斯·黑暗 bargaining者NPC脚本
 *
 * 继承自CreatureScript，实现瑟银兄弟会军需官洛克图斯的交互逻辑。
 * 洛克图斯是瑟银兄弟会的声望军需官，玩家可以用萨弗隆锭兑换瑟银兄弟会契约。
 * 只有友善及以上声望的玩家才能访问他的商店。
 */
class npc_lokhtos_darkbargainer : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         * 初始化NPC脚本，注册名称为"npc_lokhtos_darkbargainer"
         */
        npc_lokhtos_darkbargainer() : CreatureScript("npc_lokhtos_darkbargainer") { }

        /**
         * @brief 洛克图斯AI结构体
         *
         * 继承自ScriptedAI，实现闲聊菜单和物品兑换逻辑。
         */
        struct npc_lokhtos_darkbargainerAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            npc_lokhtos_darkbargainerAI(Creature* creature) : ScriptedAI(creature) { }

            /**
             * @brief 闲聊选项选择事件处理
             * @param player 选择闲聊选项的玩家指针
             * @param menuId 闲聊菜单ID（当前未使用）
             * @param gossipListId 闲聊列表ID
             * @return 返回true表示已处理该事件
             *
             * 处理玩家选择的闲聊选项：
             * - 兑换瑟银兄弟会契约
             * - 打开商店界面
             */
            bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
            {
                uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);

                ClearGossipMenuFor(player);

                // 如果选择兑换契约，施放创建契约的法术
                if (action == GOSSIP_ACTION_INFO_DEF + 1)
                {
                    CloseGossipMenuFor(player);
                    player->CastSpell(player, SPELL_CREATE_THORIUM_BROTHERHOOD_CONTRACT_DND, false);
                }

                // 如果选择打开商店，发送物品列表
                if (action == GOSSIP_ACTION_TRADE)
                    player->GetSession()->SendListInventory(me->GetGUID());

                return true;
            }

            /**
             * @brief 闲聊问候事件处理
             * @ player 与NPC交互的玩家指针
             * @return 返回true表示已处理该事件
             *
             * 根据玩家的声望和任务进度显示不同的闲聊选项：
             * - 友善及以上声望：显示商店选项
             * - 有萨弗隆锭且未完成任务：显示兑换契约选项
             * - 不同声望等级显示不同的对话文本
             */
            bool OnGossipHello(Player* player) override
            {
                InitGossipMenuFor(player, GOSSIP_ITEM_SHOW_ACCESS_MID);

                // 如果NPC是任务发布者，准备任务菜单
                if (me->IsQuestGiver())
                    player->PrepareQuestMenu(me->GetGUID());

                // 如果是商人且玩家瑟银兄弟会声望达到友善，显示商店选项
                if (me->IsVendor() && player->GetReputationRank(59) >= REP_FRIENDLY)
                    AddGossipItemFor(player, GOSSIP_ITEM_SHOW_ACCESS_MID, GOSSIP_ITEM_SHOW_ACCESS_OID, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRADE);

                // 如果玩家有萨弗隆锭且未完成任务且没有契约，显示兑换选项
                if (!player->GetQuestRewardStatus(QUEST_A_BINDING_CONTRACT) &&
                    !player->HasItemCount(ITEM_THRORIUM_BROTHERHOOD_CONTRACT, 1, true) &&
                    player->HasItemCount(ITEM_SULFURON_INGOT))
                {
                    AddGossipItemFor(player, GOSSIP_ICON_CHAT, GOSSIP_ITEM_GET_CONTRACT, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                }

                // 根据声望等级显示不同的对话文本
                if (player->GetReputationRank(59) < REP_FRIENDLY)
                    SendGossipMenuFor(player, 3673, me->GetGUID());  // 低声望对话
                else
                    SendGossipMenuFor(player, 3677, me->GetGUID());  // 友善及以上声望对话

                return true;
            }
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象指针
         * @return 返回洛克图斯AI实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetBlackrockDepthsAI<npc_lokhtos_darkbargainerAI>(creature);
        }
};

/**
 * @brief 罗克诺特相关枚举
 * 定义酒吧事件相关的对话、任务和法术ID
 */
enum Rocknot
{
    SAY_GOT_BEER       = 0,      // 对话文本ID：得到啤酒了！
    QUEST_ALE          = 4295,   // 任务：黑铁啤酒
    SPELL_DRUNKEN_RAGE = 14872   // 法术：醉酒狂暴
};

/**
 * @brief 罗克诺特NPC脚本
 *
 * 继承自CreatureScript，实现酒吧事件触发NPC罗克诺特的AI逻辑。
 * 玩家通过完成"黑铁啤酒"任务提供啤酒给罗克诺特，累计3次后会触发酒吧事件。
 * 罗克诺特会醉酒发狂，破坏酒桶和门，导致酒吧内的NPC变为敌对。
 */
class npc_rocknot : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     * 初始化NPC脚本，注册名称为"npc_rocknot"
     */
    npc_rocknot() : CreatureScript("npc_rocknot") { }

    /**
     * @brief 罗克诺特AI结构体
     *
     * 继承自EscortAI，实现酒吧事件的完整流程控制。
     * 使用路径点系统控制罗克诺特移动到酒桶和门的位置并执行破坏动作。
     */
    struct npc_rocknotAI : public EscortAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         * 初始化成员变量并获取副本实例脚本
         */
        npc_rocknotAI(Creature* creature) : EscortAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
        }

        /**
         * @brief 初始化成员变量
         * 重置所有计时器为初始状态
         */
        void Initialize()
        {
            BreakKeg_Timer = 0;   // 破坏酒桶计时器
            BreakDoor_Timer = 0;  // 破坏门计时器
        }

        InstanceScript* instance;      ///< 副本实例脚本指针

        uint32 BreakKeg_Timer;         ///< 破坏酒桶计时器
        uint32 BreakDoor_Timer;        ///< 破坏门计时器

        /**
         * @brief 重置AI状态
         * 如果正在护送中，则不重置；否则调用初始化函数
         */
        void Reset() override
        {
            if (HasEscortState(STATE_ESCORT_ESCORTING))
                return;

            Initialize();
        }

        /**
         * @brief 操作游戏对象状态
         * @param id 游戏对象数据ID
         * @param state 游戏对象状态值
         *
         * 设置指定游戏对象的状态（如开启/关闭门）
         */
        void DoGo(uint32 id, uint32 state)
        {
            if (GameObject* go = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(id)))
                go->SetGoState((GOState)state);
        }

        /**
         * @brief 到达路径点事件处理
         * @param waypointId 路径点ID
         * @param pathId 路径ID（当前未使用）
         *
         * 当罗克诺特到达指定路径点时执行相应的动作：
         * - 路径点1-4：踢击和攻击动作（模拟破坏过程）
         * - 路径点5：踢击动作并启动破坏酒桶计时器
         */
        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            switch (waypointId)
            {
                case 1:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_KICK);  // 踢击动作
                    break;
                case 2:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK_UNARMED);  // 徒手攻击动作
                    break;
                case 3:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK_UNARMED);  // 徒手攻击动作
                    break;
                case 4:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_KICK);  // 踢击动作
                    break;
                case 5:
                    me->HandleEmoteCommand(EMOTE_ONESHOT_KICK);  // 踢击动作
                    BreakKeg_Timer = 2000;  // 2秒后破坏酒桶
                    break;
            }
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 处理酒桶和门的破坏事件：
         * - 破坏酒桶：设置酒桶游戏对象状态为激活
         * - 破坏门：设置门状态为开启，激活陷阱，使法兰克斯变为敌对，标记事件完成
         */
        void UpdateAI(uint32 diff) override
        {
            // 酒桶破坏计时器处理
            if (BreakKeg_Timer)
            {
                if (BreakKeg_Timer <= diff)
                {
                    DoGo(DATA_GO_BAR_KEG, 0);  // 激活酒桶
                    BreakKeg_Timer = 0;
                    BreakDoor_Timer = 1000;    // 1秒后破坏门
                } else BreakKeg_Timer -= diff;
            }

            // 门破坏计时器处理
            if (BreakDoor_Timer)
            {
                if (BreakDoor_Timer <= diff)
                {
                    DoGo(DATA_GO_BAR_DOOR, 2);  // 开启酒吧门
                    DoGo(DATA_GO_BAR_KEG_TRAP, 0);  // 激活酒桶陷阱（目前不太有效，留待将来改进）
                    //spell by trap has effect61, this indicate the bar go hostile

                    // 将法兰克斯设置为敌对状态
                    if (Unit* tmp = ObjectAccessor::GetUnit(*me, instance->GetGuidData(DATA_PHALANX)))
                        tmp->SetFaction(FACTION_MONSTER);

                    //for later, this event(s) has alot more to it.
                    //optionally, DONE can trigger bar to go hostile.
                    instance->SetData(TYPE_BAR, DONE);  // 标记酒吧事件完成

                    BreakDoor_Timer = 0;
                } else BreakDoor_Timer -= diff;
            }

            // 更新护送AI（处理路径点移动）
            EscortAI::UpdateAI(diff);
        }

        /**
         * @brief 任务奖励事件处理
         * @param player 完成任务的玩家指针（当前未使用）
         * @param quest 完成的任务对象
         * @param item 任务物品ID（当前未使用）
         *
         * 当玩家提交"黑铁啤酒"任务时触发。
         * 累计提交3次后，罗克诺特会开始醉酒发狂事件。
         */
        void OnQuestReward(Player* /*player*/, Quest const* quest, uint32 /*item*/) override
        {
            // 如果酒吧事件已完成或正在进行中，则不处理
            if (instance->GetData(TYPE_BAR) == DONE || instance->GetData(TYPE_BAR) == SPECIAL)
                return;

            // 检查是否为"黑铁啤酒"任务
            if (quest->GetQuestId() == QUEST_ALE)
            {
                // 如果事件未开始，标记为进行中
                if (instance->GetData(TYPE_BAR) != IN_PROGRESS)
                    instance->SetData(TYPE_BAR, IN_PROGRESS);

                // 增加啤酒计数
                instance->SetData(TYPE_BAR, SPECIAL);

                //keep track of amount in instance script, returns SPECIAL if amount ok and event in progress
                // 如果累计啤酒数量达到要求（3次），开始事件
                if (instance->GetData(TYPE_BAR) == SPECIAL)
                {
                    Talk(SAY_GOT_BEER);  // 说出台词
                    DoCastSelf(SPELL_DRUNKEN_RAGE, false);  // 施放醉酒狂暴

                    Start(false, false);  // 开始护送路径
                }
            }
        }
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回罗克诺特AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetBlackrockDepthsAI<npc_rocknotAI>(creature);
    }
};

/**
 * @brief 注册黑石深渊脚本
 *
 * 将黑石深渊副本中的所有脚本注册到脚本系统中。
 * 该函数在服务器启动时被脚本加载器调用，用于初始化所有脚本。
 *
 * 调用时机：服务器启动时的脚本加载阶段
 * 性能注意事项：仅启动时调用一次，无运行时性能影响
 */
void AddSC_blackrock_depths()
{
    new go_shadowforge_brazier();        // 暗影熔炉火盆
    new at_ring_of_law();                // 律法之环区域触发器
    new npc_grimstone();                 // 格里姆斯通（竞技场主持人）
    new npc_phalanx();                   // 法兰克斯（酒吧BOSS）
    new npc_lokhtos_darkbargainer();     // 洛克图斯·黑暗 bargaining者（瑟银兄弟会军需官）
    new npc_rocknot();                   // 罗克诺特（酒吧事件NPC）
}
