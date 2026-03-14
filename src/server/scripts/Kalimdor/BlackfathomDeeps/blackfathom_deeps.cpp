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
 * @file blackfathom_deeps.cpp
 * @brief 黑暗深渊副本 - 游戏对象和事件NPC脚本
 *
 * 本文件实现了黑暗深渊副本中的以下内容：
 * - 祭坛游戏对象：玩家点击获得黑暗深渊的祝福增益效果
 * - 火焰游戏对象：点燃火焰触发小怪生成事件
 * - 副本事件小怪AI：包括各种水生生物的战斗逻辑
 * - Morridune NPC：击败最终Boss后的护送NPC，可将玩家传送至达纳苏斯
 *
 * 副本机制：
 * 1. 玩家点燃4个火焰，每个火焰会触发不同波次的小怪
 * 2. 击杀所有召唤的小怪后开启通往最终Boss的大门
 * 3. 击败最终Boss后Morridune出现，完成副本流程
 */

#include "ScriptMgr.h"
#include "blackfathom_deeps.h"
#include "InstanceScript.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Map.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"

/**
 * @brief 法术ID枚举
 *
 * 定义了游戏中使用的各种法术效果ID
 */
enum Spells
{
    SPELL_BLESSING_OF_BLACKFATHOM                           = 8733,  ///< 黑暗深渊的祝福 - 增益效果，提高属性
    SPELL_RAVAGE                                            = 8391,  ///< 撕裂 - 物理伤害技能
    SPELL_FROST_NOVA                                        = 865,   ///< 冰霜新星 - AOE冰冻效果
    SPELL_FROST_BOLT_VOLLEY                                 = 8398,  ///< 冰霜箭齐射 - 多目标冰霜伤害
    SPELL_TELEPORT_DARNASSUS                                = 9268   ///< 传送至达纳苏斯 - 完成副本后的传送法术
};

/**
 * @brief Morridune的初始位置
 *
 * 在击败Aku'mai后，Morridune会在此位置生成
 */
const Position HomePosition = {-815.817f, -145.299f, -25.870f, 0};

/**
 * @struct go_blackfathom_altar
 * @brief 黑暗深渊祭坛游戏对象AI
 *
 * 处理祭坛的交互逻辑，为玩家提供黑暗深渊的祝福增益效果。
 * 该增益效果可以提高玩家在副本中的战斗能力。
 */
struct go_blackfathom_altar : public GameObjectAI
{
    /**
     * @brief 构造函数
     * @param go 游戏对象指针
     */
    go_blackfathom_altar(GameObject* go) : GameObjectAI(go) { }

    /**
     * @brief 玩家与祭坛交互时调用
     * @param player 与祭坛交互的玩家指针
     * @return true 表示交互成功
     *
     * 当玩家点击祭坛时，如果玩家没有黑暗深渊的祝福增益，
     * 则为玩家添加该增益效果。该增益可以叠加或刷新，
     * 但本实现只检查是否存在，避免重复添加。
     */
    bool OnGossipHello(Player* player) override
    {
        if (!player->HasAura(SPELL_BLESSING_OF_BLACKFATHOM))
            player->AddAura(SPELL_BLESSING_OF_BLACKFATHOM, player);
        return true;
    }
};

/**
 * @struct go_blackfathom_fire
 * @brief 黑暗深渊火焰游戏对象AI
 *
 * 处理火焰点燃逻辑，这是副本事件的核心机制。
 * 玩家需要点燃4个火焰来触发小怪生成事件。
 * 每点燃一个火焰，都会触发不同波次的小怪群。
 */
struct go_blackfathom_fire : public GameObjectAI
{
    /**
     * @brief 构造函数
     * @param go 游戏对象指针
     */
    go_blackfathom_fire(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

    InstanceScript* instance;  ///< 副本实例脚本指针，用于访问副本数据

    /**
     * @brief 玩家点燃火焰时调用
     * @param player 点燃火焰的玩家指针（未使用）
     * @return true 表示交互成功
     *
     * 执行流程：
     * 1. 将火焰状态设置为激活（视觉上点燃）
     * 2. 设置火焰为不可选择（防止重复点击）
     * 3. 通知副本实例火焰数量增加，触发相应的小怪生成事件
     *
     * @note 火焰点燃后不可取消，玩家必须应对生成的小怪
     */
    bool OnGossipHello(Player* /*player*/) override
    {
        me->SetGoState(GO_STATE_ACTIVE);
        me->SetFlag(GO_FLAG_NOT_SELECTABLE);
        instance->SetData(DATA_FIRE, instance->GetData(DATA_FIRE) + 1);
        return true;
    }
};

/**
 * @brief 事件ID枚举
 *
 * 用于小怪AI的事件调度系统
 */
enum Events
{
    EVENT_RAVAGE = 1,           ///< 撕裂技能事件
    EVENT_FROST_NOVA,           ///< 冰霜新星技能事件
    EVENT_FROST_BOLT_VOLLEY     ///< 冰霜箭齐射技能事件
};

/**
 * @struct npc_blackfathom_deeps_event
 * @brief 黑暗深渊副本事件小怪AI
 *
 * 处理点燃火焰后召唤的小怪的战斗逻辑。
 * 支持多种小怪类型，每种小怪有不同的技能组合：
 * - Aku'mai Snapjaw（鳄龟）：使用撕裂技能
 * - Aku'mai Servant（仆从）：使用冰霜新星和冰霜箭齐射
 * - Murkshallow Softshell（软壳龟）：低血量时逃跑
 * - Barbed Crustacean（刺甲蟹）：低血量时逃跑
 */
struct npc_blackfathom_deeps_event : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物指针
     */
    npc_blackfathom_deeps_event(Creature* creature) : ScriptedAI(creature), _instance(me->GetInstanceScript()), _flee(false) { }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标（未使用）
     *
     * 根据小怪类型初始化不同的技能事件：
     * - 鳄龟：安排撕裂技能，5-8秒后首次施放
     * - 仆从：安排冰霜新星（9-12秒）和冰霜箭齐射（2-4秒）
     *
     * 同时重置逃跑状态标志。
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        _flee = false;

        switch (me->GetEntry())
        {
            case NPC_AKU_MAI_SNAPJAW:
                _events.ScheduleEvent(EVENT_RAVAGE, 5s, 8s);
                break;
            case NPC_AKU_MAI_SERVANT:
                _events.ScheduleEvent(EVENT_FROST_NOVA, 9s, 12s);
                _events.ScheduleEvent(EVENT_FROST_BOLT_VOLLEY, 2s, 4s);
                break;
            default:
                break;
        }
    }

    /**
     * @brief 进入逃避模式时调用
     * @param why 逃避原因
     *
     * 重置所有事件定时器，防止脱战后技能事件继续执行。
     * 然后调用父类的逃避模式处理。
     */
    void EnterEvadeMode(EvadeReason why) override
    {
        _events.Reset();
        ScriptedAI::EnterEvadeMode(why);
    }

    /**
     * @brief 被召唤时调用
     * @param summoner 召唤者（未使用）
     *
     * 当小怪被火焰事件召唤时，立即进入战斗状态，
     * 让小怪主动攻击附近的玩家。
     * 这是副本事件的核心机制，确保召唤的小怪立即参与战斗。
     */
    void IsSummonedBy(WorldObject* /*summoner*/) override
    {
        DoZoneInCombat();
    }

    /**
     * @brief 死亡时调用
     * @param killer 击杀者（未使用）
     *
     * 如果小怪是召唤生成的（非正常刷新），
     * 则通知副本实例增加击杀计数。
     * 当击杀计数达到18时，会开启通往最终Boss的大门。
     */
    void JustDied(Unit* /*killer*/) override
    {
        if (me->IsSummon()) //we are not a normal spawn.
            _instance->SetData(DATA_EVENT, _instance->GetData(DATA_EVENT) + 1);
    }

    /**
     * @brief 受到伤害时调用
     * @param attacker 攻击者（未使用）
     * @param damage 受到的伤害值
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     *
     * 仅对Murkshallow Softshell和Barbed Crustacean生效。
     * 当生命值低于15%时，小怪会逃跑寻求援助。
     * 逃跑状态只触发一次，防止重复逃跑。
     *
     * @note 这增加了副本的战术难度，玩家需要在逃跑前击杀小怪
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (me->GetEntry() != NPC_MURKSHALLOW_SOFTSHELL && me->GetEntry() != NPC_BARBED_CRUSTACEAN)
            return;

        if (!_flee && me->HealthBelowPctDamaged(15, damage))
        {
            _flee = true;
            me->DoFleeToGetAssistance();
        }
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 主循环处理战斗中的技能施放：
     * 1. 检查是否有战斗目标，无目标则返回
     * 2. 更新事件定时器
     * 3. 如果正在施法，则跳过本次更新
     * 4. 执行到期的事件，施放相应技能：
     *    - 撕裂：对当前目标造成物理伤害，冷却9-14秒
     *    - 冰霜新星：AOE冰冻周围敌人，冷却25-30秒
     *    - 冰霜箭齐射：随机选择目标造成冰霜伤害，冷却5-8秒
     * 5. 如果没有事件需要处理，进行近战攻击
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        _events.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_RAVAGE:
                    DoCastVictim(SPELL_RAVAGE);
                    _events.Repeat(9s, 14s);
                    break;
                case EVENT_FROST_NOVA:
                    DoCastAOE(SPELL_FROST_NOVA, false);
                    _events.Repeat(25s, 30s);
                    break;
                case EVENT_FROST_BOLT_VOLLEY:
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_FROST_BOLT_VOLLEY);
                    _events.Repeat(5s, 8s);
                    break;
                default:
                    break;
            }
        }

        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;          ///< 事件映射表，管理技能冷却
    InstanceScript* _instance; ///< 副本实例脚本指针
    bool _flee;                ///< 是否已触发逃跑状态
};

/**
 * @brief Morridune相关枚举
 *
 * Morridune是在击败最终Boss Aku'mai后出现的NPC，
 * 他会护送玩家离开副本并提供传送服务。
 */
enum Morridune
{
    SAY_MORRIDUNE_1 = 0,  ///< Morridune的第一句对话（初始出现时）
    SAY_MORRIDUNE_2 = 1   ///< Morridune的第二句对话（到达目的地时）
};

/**
 * @struct npc_morridune
 * @brief Morridune护送NPC AI
 *
 * Morridune是在击败最终Boss Aku'mai后出现的夜精灵NPC。
 * 他会自动沿路径移动到副本出口附近，然后提供传送至达纳苏斯的服务。
 * 这是完成副本后的标准退出流程。
 */
struct npc_morridune : public EscortAI
{
    /**
     * @brief 构造函数
     * @param creature 生物指针
     */
    npc_morridune(Creature* creature) : EscortAI(creature) { }

    /**
     * @brief 重置时调用
     *
     * 初始化护送NPC的行为：
     * 1. 说出第一句对话，向玩家打招呼
     * 2. 移除对话标志，防止在移动过程中被玩家交互
     * 3. 开始沿路径移动，不攻击敌人（参数false）
     *
     * @note 护送AI会自动处理移动和暂停逻辑
     */
    void Reset() override
    {
        Talk(SAY_MORRIDUNE_1);
        me->RemoveNpcFlag(UNIT_NPC_FLAG_GOSSIP);
        Start(false);
    }

    /**
     * @brief 到达路径点时调用
     * @param waypointId 到达的路径点ID
     * @param pathId 路径ID（未使用）
     *
     * 在第4个路径点（副本出口附近）执行：
     * 1. 暂停护送，停止移动
     * 2. 设置面向方向（朝向玩家）
     * 3. 添加对话标志，允许玩家交互
     * 4. 说出第二句对话，提示玩家可以传送
     *
     * @note 路径点在waypoint数据中定义，索引从0开始
     */
    void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
    {
        switch (waypointId)
        {
            case 4:
                SetEscortPaused(true);
                me->SetFacingTo(1.775791f);
                me->SetNpcFlag(UNIT_NPC_FLAG_GOSSIP);
                Talk(SAY_MORRIDUNE_2);
                break;
        }
    }

    /**
     * @brief 玩家选择对话选项时调用
     * @param player 与NPC交互的玩家指针
     * @param menuId 菜单ID（未使用）
     * @param gossipListId 对话选项ID（未使用）
     * @return false 表示不继续处理对话
     *
     * 对玩家施放传送至达纳苏斯的法术，
     * 完成副本后的传送服务。
     *
     * @note 传送是单向的，玩家到达达纳苏斯后需要自行返回
     */
    bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 /*gossipListId*/) override
    {
        DoCast(player, SPELL_TELEPORT_DARNASSUS);
        return false;
    }
};

/**
 * @brief 注册黑暗深渊副本脚本
 *
 * 注册以下脚本：
 * - go_blackfathom_altar: 祭坛游戏对象
 * - go_blackfathom_fire: 火焰游戏对象
 * - npc_blackfathom_deeps_event: 副本事件小怪
 * - npc_morridune: 护送NPC
 */
void AddSC_blackfathom_deeps()
{
    RegisterBlackfathomDeepsGameObjectAI(go_blackfathom_altar);
    RegisterBlackfathomDeepsGameObjectAI(go_blackfathom_fire);
    RegisterBlackfathomDeepsCreatureAI(npc_blackfathom_deeps_event);
    RegisterBlackfathomDeepsCreatureAI(npc_morridune);
}
