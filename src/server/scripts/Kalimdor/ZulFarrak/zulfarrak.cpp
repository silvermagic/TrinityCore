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
 * @file zulfarrak.cpp
 * @brief 祖尔法拉克副本辅助脚本
 *
 * 该模块实现祖尔法拉克副本中的辅助NPC和游戏对象:
 *
 * 主要内容:
 * 1. 布莱中士(NPC):金字塔事件后变为敌对,需要被击杀
 * 2. 威利·炸 fuse(NPC):炸毁最终大门的哥布林
 * 3. 巨魔牢笼(GO):打开后释放布莱和他的队友
 * 4. 浅坟墓(GO):随机生成僵尸或亡灵英雄
 * 5. 祖尔拉姆区域触发器(AT):激活祖尔拉姆Boss
 *
 * 剧情背景:
 * 玩家在金字塔事件中解救了布莱中士和他的队友,
 * 但这些NPC在事件完成后背叛玩家,变为敌对。
 * 威利可以帮助玩家炸开通往最终区域的大门。
 */

/* ScriptData
SDName: Zulfarrak
SD%Complete: 50
SDComment: Consider it temporary, no instance script made for this instance yet.
SDCategory: Zul'Farrak
EndScriptData */

/* ContentData
npc_sergeant_bly
npc_weegli_blastfuse
EndContentData */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "zulfarrak.h"

/*######
## npc_sergeant_bly
######*/

/**
 * @enum blySays
 * @brief 布莱中士的对话文本ID
 */
enum blySays
{
    SAY_1 = 0,  ///< "就是这样!我受够了帮你们。是时候在战场上解决一切了!"
    SAY_2 = 1   ///< "让我们结束这一切!"
};

/**
 * @enum blySpells
 * @brief 布莱中士使用的法术ID
 */
enum blySpells
{
    SPELL_SHIELD_BASH          = 11972,  ///< 盾牌猛击 - 造成伤害并打断施法
    SPELL_REVENGE              = 12170,  ///< 复仇 - 在招架/闪避/格挡后使用的反击
};

/**
 * @enum blygossip
 * @brief Gossip菜单相关常量
 */
enum blygossip
{
    GOSSIP_BLY_MID             = 941,  ///< Gossip菜单ID: "就是这样!我受够了帮你们..."
    GOSSIP_BLY_OID             = 1     ///< Gossip选项ID
};

/**
 * @class npc_sergeant_bly
 * @brief 布莱中士NPC脚本
 *
 * 实现布莱中士的行为:
 * - 金字塔事件中作为友方NPC
 * - 事件完成后可以对话
 * - 对话后变为敌对并攻击玩家
 */
class npc_sergeant_bly : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称为"npc_sergeant_bly"
     */
    npc_sergeant_bly() : CreatureScript("npc_sergeant_bly") { }

    /**
     * @class npc_sergeant_blyAI
     * @brief 布莱中士的AI实现
     */
    struct npc_sergeant_blyAI : public ScriptedAI
    {
        /**
         * @brief 构造函数,初始化AI状态
         * @param creature NPC生物指针
         */
        npc_sergeant_blyAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
            postGossipStep = 0;
            Text_Timer = 0;
        }

        /**
         * @brief 初始化法术计时器
         */
        void Initialize()
        {
            ShieldBash_Timer = 5000;  // 盾牌猛击初始冷却5秒
            Revenge_Timer = 8000;     // 复仇初始冷却8秒
        }

        /// 副本实例脚本指针
        InstanceScript* instance;

        /// Gossip对话后的步骤(0-4)
        uint32 postGossipStep;
        /// 对话计时器(毫秒)
        uint32 Text_Timer;
        /// 盾牌猛击冷却计时器(毫秒)
        uint32 ShieldBash_Timer;
        /// 复仇冷却计时器(毫秒)
        uint32 Revenge_Timer;                                   //this is wrong, spell should never be used unless me->GetVictim() dodge, parry or block attack. Trinity support required.
        /// 触发对话的玩家GUID
        ObjectGuid PlayerGUID;

        /**
         * @brief 重置NPC状态
         *
         * 重置法术计时器并设置为友方阵营
         */
        void Reset() override
        {
            Initialize();

            me->SetFaction(FACTION_FRIENDLY);
        }

        /**
         * @brief 主更新函数,每帧调用
         * @param diff 距离上次更新的时间间隔(毫秒)
         *
         * 处理两个逻辑:
         * 1. Gossip对话后的剧情序列
         * 2. 战斗AI(法术施放和近战攻击)
         */
        void UpdateAI(uint32 diff) override
        {
            // 处理对话后的剧情序列
            if (postGossipStep>0 && postGossipStep<4)
            {
                if (Text_Timer<diff)
                {
                    switch (postGossipStep)
                    {
                        case 1:
                            // 威利不战斗 - 他去炸门
                            if (Creature* pWeegli = ObjectAccessor::GetCreature(*me, instance->GetGuidData(ENTRY_WEEGLI)))
                                pWeegli->AI()->DoAction(0);
                            Talk(SAY_1);
                            Text_Timer = 5000;
                            break;
                        case 2:
                            Talk(SAY_2);
                            Text_Timer = 5000;
                            break;
                        case 3:
                            // 变为敌对并攻击玩家
                            me->SetFaction(FACTION_MONSTER);
                            if (Player* target = ObjectAccessor::GetPlayer(*me, PlayerGUID))
                                AttackStart(target);

                            // 队友也变为敌对
                            switchFactionIfAlive(ENTRY_RAVEN);
                            switchFactionIfAlive(ENTRY_ORO);
                            switchFactionIfAlive(ENTRY_MURTA);
                    }
                    postGossipStep++;
                }
                else Text_Timer -= diff;
            }

            if (!UpdateVictim())
                return;

            // 战斗AI:施放盾牌猛击
            if (ShieldBash_Timer <= diff)
            {
                DoCastVictim(SPELL_SHIELD_BASH);
                ShieldBash_Timer = 15000;  // 15秒冷却
            }
            else
                ShieldBash_Timer -= diff;

            // 战斗AI:施放复仇
            // 注意:正确的实现应该只在目标招架/闪避/格挡后使用
            if (Revenge_Timer <= diff)
            {
                DoCastVictim(SPELL_REVENGE);
                Revenge_Timer = 10000;  // 10秒冷却
            }
            else
                Revenge_Timer -= diff;

            DoMeleeAttackIfReady();
        }

        /**
         * @brief 执行动作回调
         * @param param 动作参数(未使用)
         *
         * 由外部调用,开始对话序列
         */
        void DoAction(int32 /*param*/) override
        {
            postGossipStep=1;
            Text_Timer = 0;
        }

        /**
         * @brief 切换阵营为敌对(如果NPC存活)
         * @param entry NPC的Entry ID
         */
        void switchFactionIfAlive(uint32 entry)
        {
           if (Creature* crew = ObjectAccessor::GetCreature(*me, instance->GetGuidData(entry)))
               if (crew->IsAlive())
                   crew->SetFaction(FACTION_MONSTER);
        }

        /**
         * @brief Gossip菜单选项选择回调
         * @param player 玩家指针
         * @param menuId 菜单ID(未使用)
         * @param gossipListId Gossip选项列表ID
         * @return true表示处理完成
         *
         * 当玩家选择"战斗"选项时,触发布莱和他的队友变为敌对
         */
        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);
            if (action == GOSSIP_ACTION_INFO_DEF + 1)
            {
                CloseGossipMenuFor(player);
                PlayerGUID = player->GetGUID();
                DoAction(0);
            }
            return true;
        }

        /**
         * @brief Gossip菜单打开回调
         * @param player 玩家指针
         * @return true表示处理完成
         *
         * 根据金字塔事件进度显示不同的对话内容
         */
        bool OnGossipHello(Player* player) override
        {
            InitGossipMenuFor(player, GOSSIP_BLY_MID);
            if (instance->GetData(EVENT_PYRAMID) == PYRAMID_KILLED_ALL_TROLLS)
            {
                // 事件完成,显示战斗选项
                AddGossipItemFor(player, GOSSIP_BLY_MID, GOSSIP_BLY_OID, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                SendGossipMenuFor(player, 1517, me->GetGUID());
            }
            else
                if (instance->GetData(EVENT_PYRAMID) == PYRAMID_NOT_STARTED)
                    SendGossipMenuFor(player, 1515, me->GetGUID());  // 事件未开始
                else
                    SendGossipMenuFor(player, 1516, me->GetGUID());  // 事件进行中
            return true;
        }
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物指针
     * @return 新创建的AI对象
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetZulFarrakAI<npc_sergeant_blyAI>(creature);
    }
};

/*######
+## go_troll_cage
+######*/

/**
 * @class go_troll_cage
 * @brief 巨魔牢笼游戏对象脚本
 *
 * 实现牢笼的交互逻辑:
 * - 玩家打开牢笼时,释放布莱和他的队友
 * - 设置NPC为主动攻击状态并移动到楼梯顶部
 */
class go_troll_cage : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称为"go_troll_cage"
     */
    go_troll_cage() : GameObjectScript("go_troll_cage") { }

    /**
     * @class go_troll_cageAI
     * @brief 巨魔牢笼的AI实现
     */
    struct go_troll_cageAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_troll_cageAI(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

        /// 副本实例脚本指针
        InstanceScript* instance;

        /**
         * @brief 玩家交互回调
         * @param player 玩家指针(未使用)
         * @return false表示允许默认交互行为
         *
         * 打开牢笼时:
         * 1. 设置金字塔事件状态为"牢笼已打开"
         * 2. 初始化布莱和他的队友,让他们移动到楼梯顶部
         */
        bool OnGossipHello(Player* /*player*/) override
        {
            instance->SetData(EVENT_PYRAMID, PYRAMID_CAGES_OPEN);
            // 设置布莱和他的队友为主动攻击并移动到楼梯顶部
            initBlyCrewMember(ENTRY_BLY, 1884.99f, 1263, 41.52f);
            initBlyCrewMember(ENTRY_RAVEN, 1882.5f, 1263, 41.52f);
            initBlyCrewMember(ENTRY_ORO, 1886.47f, 1270.68f, 41.68f);
            initBlyCrewMember(ENTRY_WEEGLI, 1890, 1263, 41.52f);
            initBlyCrewMember(ENTRY_MURTA, 1891.19f, 1272.03f, 41.60f);
            return false;
        }

    private:
        /**
         * @brief 初始化布莱队伍成员
         * @param entry NPC的Entry ID
         * @param x 目标X坐标
         * @param y 目标Y坐标
         * @param z 目标Z坐标
         *
         * 设置NPC为主动攻击,步行移动到指定位置
         */
        void initBlyCrewMember(uint32 entry, float x, float y, float z)
        {
            if (Creature* crew = ObjectAccessor::GetCreature(*me, instance->GetGuidData(entry)))
            {
                crew->SetReactState(REACT_AGGRESSIVE);  // 设置为主动攻击
                crew->SetWalk(true);                     // 步行模式
                crew->SetHomePosition(x, y, z, 0);       // 设置初始位置
                crew->GetMotionMaster()->MovePoint(1, x, y, z);
                crew->SetFaction(FACTION_ESCORTEE_N_NEUTRAL_ACTIVE);  // 设置为中立项队
            }
        }
    };

    /**
     * @brief 获取AI实例
     * @param go 游戏对象指针
     * @return 新创建的AI对象
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return GetZulFarrakAI<go_troll_cageAI>(go);
    }
};

/*######
## npc_weegli_blastfuse
######*/

/**
 * @enum weegliSpells
 * @brief 威利使用的法术ID
 */
enum weegliSpells
{
    SPELL_BOMB                  = 8858,   ///< 炸弹 - 投掷炸弹造成伤害
    SPELL_GOBLIN_LAND_MINE      = 21688,  ///< 哥布林地雷(未使用)
    SPELL_SHOOT                 = 6660,   ///< 射击 - 远程攻击
    SPELL_WEEGLIS_BARREL        = 10772   ///< 威利的炸药桶(未使用)
};

/**
 * @enum weegliSays
 * @brief 威利的对话文本ID
 */
enum weegliSays
{
    SAY_WEEGLI_OHNO             = 0,  ///< "噢不!怪物来了!"
    SAY_WEEGLI_OK_I_GO          = 1   ///< "好的,我去炸门!"
};

/**
 * @enum weegligossip
 * @brief Gossip菜单相关常量
 */
enum weegligossip
{
    GOSSIP_WEEGLI_MID             = 940,  ///< Gossip菜单ID: "你现在会炸门吗?"
    GOSSIP_WEEGLI_OID             = 0     ///< Gossip选项ID
};

/**
 * @class npc_weegli_blastfuse
 * @brief 威利·炸 fuse NPC脚本
 *
 * 实现威利的行为:
 * - 金字塔事件中作为友方远程攻击者
 * - 事件完成后可以对话
 * - 对话后移动到大门并炸毁它
 */
class npc_weegli_blastfuse : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称为"npc_weegli_blastfuse"
     */
    npc_weegli_blastfuse() : CreatureScript("npc_weegli_blastfuse") { }

    /**
     * @class npc_weegli_blastfuseAI
     * @brief 威利的AI实现
     */
    struct npc_weegli_blastfuseAI : public ScriptedAI
    {
        /**
         * @brief 构造函数,初始化AI状态
         * @param creature NPC生物指针
         */
        npc_weegli_blastfuseAI(Creature* creature) : ScriptedAI(creature)
        {
            instance = creature->GetInstanceScript();
            destroyingDoor = false;
            Bomb_Timer = 10000;
            LandMine_Timer = 30000;
        }

        /// 炸弹冷却计时器(毫秒)
        uint32 Bomb_Timer;
        /// 地雷冷却计时器(毫秒)
        uint32 LandMine_Timer;
        /// 是否正在炸门
        bool destroyingDoor;
        /// 副本实例脚本指针
        InstanceScript* instance;

        /**
         * @brief 重置NPC状态
         */
        void Reset() override
        {
            /*instance->SetData(0, NOT_STARTED);*/
        }

        /**
         * @brief 开始攻击回调
         * @param victim 目标
         *
         * 使用远程攻击模式,保持10码距离
         */
        void AttackStart(Unit* victim) override
        {
            AttackStartCaster(victim, 10);//保持距离 & 投掷炸弹/射击
        }

        /**
         * @brief 死亡回调
         * @param killer 击杀者(未使用)
         */
        void JustDied(Unit* /*killer*/) override
        {
            /*instance->SetData(0, DONE);*/
        }

        /**
         * @brief 主更新函数,每帧调用
         * @param diff 距离上次更新的时间间隔(毫秒)
         *
         * 处理战斗AI:
         * - 定期投掷炸弹
         * - 超出近战范围时使用射击
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            if (Bomb_Timer < diff)
            {
                DoCastVictim(SPELL_BOMB);
                Bomb_Timer = 10000;  // 10秒冷却
            }
            else
                Bomb_Timer -= diff;

            // 如果目标超出近战范围,使用射击
            if (me->isAttackReady() && !me->IsWithinMeleeRange(me->GetVictim()))
            {
                DoCastVictim(SPELL_SHOOT);
                me->SetSheath(SHEATH_STATE_RANGED);
            }
            else
            {
                me->SetSheath(SHEATH_STATE_MELEE);
                DoMeleeAttackIfReady();
            }
        }

        /**
         * @brief 移动完成回调
         * @param type 移动类型(未使用)
         * @param id 移动点ID(未使用)
         *
         * 处理两种情况:
         * 1. 到达楼梯顶部:喊话并触发金字塔事件
         * 2. 到达大门:炸毁大门并消失
         */
        void MovementInform(uint32 /*type*/, uint32 /*id*/) override
        {
            if (instance->GetData(EVENT_PYRAMID) == PYRAMID_CAGES_OPEN)
            {
                // 到达楼梯顶部,触发金字塔事件
                instance->SetData(EVENT_PYRAMID, PYRAMID_ARRIVED_AT_STAIR);
                Talk(SAY_WEEGLI_OHNO);
                me->SetHomePosition(1882.69f, 1272.28f, 41.87f, 0);
            }
            else
                if (destroyingDoor)
                {
                    // 到达大门,炸毁它并消失
                    instance->DoUseDoorOrButton(instance->GetGuidData(GO_END_DOOR));
                    /// @todo 离开这个区域...
                    me->DespawnOrUnsummon();
                };
        }

        /**
         * @brief 开始炸门任务
         *
         * 设置为友方阵营,移动到大门位置
         */
        void DestroyDoor()
        {
            if (me->IsAlive())
            {
                me->SetFaction(FACTION_FRIENDLY);
                me->GetMotionMaster()->MovePoint(0, 1858.57f, 1146.35f, 14.745f);
                me->SetHomePosition(1858.57f, 1146.35f, 14.745f, 3.85f); // 以防他被打断
                Talk(SAY_WEEGLI_OK_I_GO);
                destroyingDoor = true;
            }
        }

        /**
         * @brief Gossip菜单选项选择回调
         * @param player 玩家指针
         * @param menuId 菜单ID(未使用)
         * @param gossipListId Gossip选项列表ID
         * @return true表示处理完成
         *
         * 当玩家选择"炸门"选项时,威利移动到大门并炸毁它
         */
        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);
            if (action == GOSSIP_ACTION_INFO_DEF + 1)
            {
                CloseGossipMenuFor(player);
                // 这里让他跑到门边,设置炸药并跑开
                DestroyDoor();
            }
            return true;
        }

        /**
         * @brief Gossip菜单打开回调
         * @param player 玩家指针
         * @return true表示处理完成
         *
         * 根据金字塔事件进度显示不同的对话内容
         */
        bool OnGossipHello(Player* player) override
        {
            InitGossipMenuFor(player, GOSSIP_WEEGLI_MID);
            switch (instance->GetData(EVENT_PYRAMID))
            {
                case PYRAMID_KILLED_ALL_TROLLS:
                    AddGossipItemFor(player, GOSSIP_WEEGLI_MID, GOSSIP_WEEGLI_OID, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
                    SendGossipMenuFor(player, 1514, me->GetGUID());  // 如果事件可以继续到结束
                    break;
                case PYRAMID_NOT_STARTED:
                    SendGossipMenuFor(player, 1511, me->GetGUID());  // 如果事件未开始
                    break;
                default:
                    SendGossipMenuFor(player, 1513, me->GetGUID());  // 如果事件进行中
            }
            return true;
        }
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物指针
     * @return 新创建的AI对象
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetZulFarrakAI<npc_weegli_blastfuseAI>(creature);
    }
};

/*######
## go_shallow_grave
######*/

/**
 * @enum ShallowGrave
 * @brief 浅坟墓相关常量
 */
enum ShallowGrave
{
    NPC_ZOMBIE          = 7286,  ///< 僵尸NPC ID
    NPC_DEAD_HERO       = 7276,  ///< 亡灵英雄NPC ID
    CHANCE_ZOMBIE       = 65,    ///< 僵尸刷新概率(65%)
    CHANCE_DEAD_HERO    = 10     ///< 亡灵英雄刷新概率(10%)
};

/**
 * @class go_shallow_grave
 * @brief 浅坟墓游戏对象脚本
 *
 * 实现浅坟墓的随机生成机制:
 * - 第一次交互时有概率生成僵尸或亡灵英雄
 * - 剩余25%概率不生成任何东西
 */
class go_shallow_grave : public GameObjectScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称为"go_shallow_grave"
     */
    go_shallow_grave() : GameObjectScript("go_shallow_grave") { }

    /**
     * @class go_shallow_graveAI
     * @brief 浅坟墓的AI实现
     */
    struct go_shallow_graveAI : public GameObjectAI
    {
        /**
         * @brief 构造函数
         * @param go 游戏对象指针
         */
        go_shallow_graveAI(GameObject* go) : GameObjectAI(go) { }

        /**
         * @brief 玩家交互回调
         * @param player 玩家指针(未使用)
         * @return false表示允许默认交互行为
         *
         * 第一次使用坟墓时随机生成怪物:
         * - 65%概率生成僵尸
         * - 10%概率生成亡灵英雄
         * - 25%概率什么都不生成
         */
        bool OnGossipHello(Player* /*player*/) override
        {
            // 第一次使用坟墓时随机生成僵尸或亡灵英雄
            if (me->GetUseCount() == 0)
            {
                uint32 randomchance = urand(0, 100);
                if (randomchance < CHANCE_ZOMBIE)
                    me->SummonCreature(NPC_ZOMBIE, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30s);
                else
                    if ((randomchance - CHANCE_ZOMBIE) < CHANCE_DEAD_HERO)
                        me->SummonCreature(NPC_DEAD_HERO, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 30s);
            }
            me->AddUse();  // 增加使用次数
            return false;
        }
    };

    /**
     * @brief 获取AI实例
     * @param go 游戏对象指针
     * @return 新创建的AI对象
     */
    GameObjectAI* GetAI(GameObject* go) const override
    {
        return GetZulFarrakAI<go_shallow_graveAI>(go);
    }
};

/*######
## at_zumrah
######*/

/**
 * @enum zumrahConsts
 * @brief 祖尔拉姆区域触发器常量
 */
enum zumrahConsts
{
    ZUMRAH_ID = 7271  ///< 祖尔拉姆Boss NPC ID
};

/**
 * @class at_zumrah
 * @brief 祖尔拉姆区域触发器脚本
 *
 * 实现Boss激活机制:
 * - 当玩家进入区域时,祖尔拉姆从友方变为敌对
 * - 这样设计是为了让Boss在未被触发前处于中立状态
 */
class at_zumrah : public AreaTriggerScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称为"at_zumrah"
     */
    at_zumrah() : AreaTriggerScript("at_zumrah") { }

    /**
     * @brief 玩家触发区域回调
     * @param player 触发的玩家
     * @param at 区域触发器数据(未使用)
     * @return true表示处理完成
     *
     * 当玩家进入区域时:
     * 1. 查找30码范围内的祖尔拉姆
     * 2. 如果找到,将其阵营设置为敌对
     */
    bool OnTrigger(Player* player, AreaTriggerEntry const* /*at*/) override
    {
        Creature* pZumrah = player->FindNearestCreature(ZUMRAH_ID, 30.0f);

        if (!pZumrah)
            return false;

        pZumrah->SetFaction(FACTION_TROLL_FROSTMANE);
        return true;
    }

};

/**
 * @brief 注册祖尔法拉克脚本
 *
 * 此函数在脚本加载时被调用,创建所有脚本对象
 */
void AddSC_zulfarrak()
{
    new npc_sergeant_bly();
    new npc_weegli_blastfuse();
    new go_shallow_grave();
    new at_zumrah();
    new go_troll_cage();
}
