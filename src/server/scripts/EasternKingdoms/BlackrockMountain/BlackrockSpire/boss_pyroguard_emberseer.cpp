/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public License as published by the
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
 * @file boss_pyroguard_emberseer.cpp
 * @brief 黑石塔上层首领 - 烈焰卫士埃博希尔 (Pyroguard Emberseer) AI 实现
 *
 * 本模块实现了黑石塔上层副本中熔岩卫士埃博希尔的战斗AI逻辑。
 * 埃博希尔是一个火焰元素首领，需要通过特殊的激活事件才能开始战斗。
 *
 * 主要功能：
 * - 实现首领的激活事件流程
 * - 管理黑手狱卒的交互逻辑
 * - 实现火焰护盾、火焰新星、火焰冲击和炎爆术技能
 * - 处理战斗前的能量充能过程
 * - 管理符文石的状态变化
 */

#include "ScriptMgr.h"
#include "blackrock_spire.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"

/**
 * @brief 对话文本枚举
 *
 * 定义埃博希尔使用的文本ID
 */
enum Text
{
    EMOTE_ONE_STACK                 = 0,  ///< 单层充能表情
    EMOTE_TEN_STACK                 = 1,  ///< 十层充能表情
    EMOTE_FREE_OF_BONDS             = 2,  ///< 解脱束缚表情
    YELL_FREE_OF_BONDS              = 3   ///< 解脱束缚喊话
};

/**
 * @brief 法术ID枚举
 *
 * 定义埃博希尔和黑手狱卒使用的所有法术ID
 */
enum Spells
{
    // 埃博希尔法术
    SPELL_ENCAGED_EMBERSEER         = 15282, ///< 囚禁埃博希尔 - 初始状态光环
    SPELL_FIRE_SHIELD_TRIGGER       = 13377, ///< 火焰护盾触发器 - 每3秒触发火焰护盾
    SPELL_FIRE_SHIELD               = 13376, ///< 火焰护盾 - 对周围敌人造成火焰伤害
    SPELL_FREEZE_ANIM               = 16245, ///< 冻结动画 - 激活过程中的视觉效果
    SPELL_EMBERSEER_GROWING         = 16048, ///< 埃博希尔成长 - 充能过程
    SPELL_EMBERSEER_GROWING_TRIGGER = 16049, ///< 埃博希尔成长触发器
    SPELL_EMBERSEER_FULL_STRENGTH   = 16047, ///< 埃博希尔完全力量 - 充能完成
    SPELL_FIRENOVA                  = 23462, ///< 火焰新星 - AOE火焰伤害
    SPELL_FLAMEBUFFET               = 23341, ///< 火焰冲击 - 减少火焰抗性
    SPELL_PYROBLAST                 = 17274, ///< 炎爆术 - 高伤害火焰法术

    // 黑手狱卒法术
    SPELL_ENCAGE_EMBERSEER          = 15281, ///< 囚禁埃博希尔 - 狱卒施放
    SPELL_STRIKE                    = 15580, ///< 打击 - 普通攻击技能
    SPELL_ENCAGE                    = 16045, ///< 囚禁 - 控制玩家技能

    // 祭坛施放给玩家的法术
    SPELL_EMBERSEER_OBJECT_VISUAL   = 16532  ///< 埃博希尔对象视觉效果
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的所有事件ID，用于事件调度系统
 */
enum Events
{
    // 重生
    EVENT_RESPAWN                   = 1,  ///< 重生事件
    // 战前阶段
    EVENT_PRE_FIGHT_1               = 2,  ///< 战前阶段1 - 激活狱卒
    EVENT_PRE_FIGHT_2               = 3,  ///< 战前阶段2 - 开始充能
    // 战斗阶段
    EVENT_FIRENOVA                  = 4,  ///< 火焰新星事件
    EVENT_FLAMEBUFFET               = 5,  ///< 火焰冲击事件
    EVENT_PYROBLAST                 = 6,  ///< 炎爆术事件
    // 由于触发法术不在DBC中的补丁处理
    EVENT_FIRE_SHIELD               = 7,  ///< 火焰护盾事件
    // 确保所有玩家都有祭坛光环
    EVENT_PLAYER_CHECK              = 8,  ///< 玩家检查事件
    EVENT_ENTER_COMBAT              = 9   ///< 进入战斗事件
};

/**
 * @brief 烈焰卫士埃博希尔 AI 结构体
 *
 * 继承自 BossAI 基类，实现埃博希尔的完整战斗AI。
 * 负责管理激活事件流程、充能过程和战斗技能施放。
 *
 * 激活流程：
 * 1. 玩家与祭坛交互获得光环
 * 2. 检测到玩家光环后激活所有黑手狱卒
 * 3. 狱卒进入战斗并停止囚禁法术
 * 4. 埃博希尔移除囚禁光环并开始充能
 * 5. 充能20层后解除冻结，获得完全力量
 * 6. 进入可攻击状态
 */
struct boss_pyroguard_emberseer : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 关联的生物对象指针
     *
     * 初始化 BossAI 基类，关联首领数据为 DATA_PYROGAURD_EMBERSEER
     */
    boss_pyroguard_emberseer(Creature* creature) : BossAI(creature, DATA_PYROGAURD_EMBERSEER) { }

    /**
     * @brief 重置首领状态
     *
     * 当首领脱离战斗或重置时调用。
     * 设置首领为不可交互和免疫玩家攻击状态，
     * 重置事件队列，移除充能相关光环，
     * 安排重生事件和火焰护盾事件。
     *
     * @note 调用时机：首领脱战、重置副本、首领死亡后重生
     */
    void Reset() override
    {
        // 设置首领不可交互和免疫玩家
        me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
        me->SetImmuneToPC(true);
        events.Reset();

        // 在生成和重置时应用光环
        // DoCast(me, SPELL_FIRE_SHIELD_TRIGGER); // 需要在旧的DBC中找到这个法术
        me->RemoveAura(SPELL_EMBERSEER_FULL_STRENGTH);
        me->RemoveAura(SPELL_EMBERSEER_GROWING);
        me->RemoveAura(SPELL_EMBERSEER_GROWING_TRIGGER);

        // 安排重生事件，5秒后
        events.ScheduleEvent(EVENT_RESPAWN, 5s);
        // 由于缺少触发法术的补丁处理
        events.ScheduleEvent(EVENT_FIRE_SHIELD, 3s);
    }

    /**
     * @brief 设置数据回调
     * @param type 数据类型（未使用）
     * @param data 数据值
     *
     * 用于从外部触发特定事件。
     * 当 data 为 1 时，安排玩家检查事件。
     *
     * @note 调用时机：由其他AI或游戏事件触发
     */
    void SetData(uint32 /*type*/, uint32 data) override
    {
        switch (data)
        {
            case 1:
                // 安排玩家检查，5秒后
                events.ScheduleEvent(EVENT_PLAYER_CHECK, 5s);
                break;
            default:
                break;
        }
    }

    /**
     * @brief 进入战斗回调
     * @param who 触发战斗的单位（未使用）
     *
     * 当首领进入战斗状态时调用。
     * 安排战斗技能的施放时机。
     *
     * @note 调用时机：首领充能完成后主动攻击玩家
     * @note TODO 检查战斗时序
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        // ### TODO 检查战斗时序 ###
        events.ScheduleEvent(EVENT_FIRENOVA, 6s);
        events.ScheduleEvent(EVENT_FLAMEBUFFET, 3s);
        events.ScheduleEvent(EVENT_PYROBLAST, 14s);
    }

    /**
     * @brief 死亡回调
     * @param killer 击杀首领的单位（未使用）
     *
     * 当首领死亡时调用。
     * 激活所有符文石并完成副本事件。
     *
     * @note 调用时机：首领生命值降为0
     */
    void JustDied(Unit* /*killer*/) override
    {
        // 激活所有符文石
        UpdateRunes(GO_STATE_READY);
        // 完成副本事件
        instance->SetBossState(DATA_PYROGAURD_EMBERSEER, DONE);
    }

    /**
     * @brief 法术命中回调
     * @param caster 施法者对象
     * @param spellInfo 法术信息
     *
     * 当首领被法术命中时调用。
     * 处理囚禁法术和充能触发法术的特殊逻辑。
     *
     * 特殊处理：
     * - 囚禁法术：应用囚禁光环并重置
     * - 成长触发：追踪充能层数，达到20层时解除束缚
     *
     * @note 调用时机：首领被任何法术命中
     */
    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        // 处理囚禁法术
        if (spellInfo->Id == SPELL_ENCAGE_EMBERSEER)
        {
            // 如果还没有囚禁光环，应用它
            if (!me->GetAuraCount(SPELL_ENCAGED_EMBERSEER))
            {
                me->CastSpell(me, SPELL_ENCAGED_EMBERSEER);
                Reset();
            }
        }

        // 处理充能触发法术
        if (spellInfo->Id == SPELL_EMBERSEER_GROWING_TRIGGER)
        {
            // 当达到10层时显示表情
            if (me->GetAuraCount(SPELL_EMBERSEER_GROWING_TRIGGER) == 10)
                Talk(EMOTE_TEN_STACK);

            // 当达到20层时完成充能
            if (me->GetAuraCount(SPELL_EMBERSEER_GROWING_TRIGGER) == 20)
            {
                // 移除冻结动画
                me->RemoveAura(SPELL_FREEZE_ANIM);
                // 施放完全力量法术
                me->CastSpell(me, SPELL_EMBERSEER_FULL_STRENGTH);
                // 显示解脱表情和喊话
                Talk(EMOTE_FREE_OF_BONDS);
                Talk(YELL_FREE_OF_BONDS);
                // 解除不可交互和免疫状态
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->SetImmuneToPC(false);
                // 安排进入战斗，2秒后
                events.ScheduleEvent(EVENT_ENTER_COMBAT, 2s);
            }
        }
    }

    /**
     * @brief 更新符文石状态
     * @param state 符文石的目标状态
     *
     * 更新所有7个符文石的激活状态。
     * 在首领死亡时激活符文石。
     *
     * @note 此函数通过访问副本数据获取符文石的GUID
     */
    void UpdateRunes(GOState state)
    {
        // 更新所有符文石状态
        if (GameObject* rune1 = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_EMBERSEER_RUNE_1)))
            rune1->SetGoState(state);
        if (GameObject* rune2 = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_EMBERSEER_RUNE_2)))
            rune2->SetGoState(state);
        if (GameObject* rune3 = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_EMBERSEER_RUNE_3)))
            rune3->SetGoState(state);
        if (GameObject* rune4 = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_EMBERSEER_RUNE_4)))
            rune4->SetGoState(state);
        if (GameObject* rune5 = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_EMBERSEER_RUNE_5)))
            rune5->SetGoState(state);
        if (GameObject* rune6 = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_EMBERSEER_RUNE_6)))
            rune6->SetGoState(state);
        if (GameObject* rune7 = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(GO_EMBERSEER_RUNE_7)))
            rune7->SetGoState(state);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 每个服务器tick调用一次，处理AI的主逻辑循环。
     * 分为非战斗阶段和战斗阶段两个部分。
     *
     * 非战斗阶段：
     * - 处理重生、激活流程和充能过程
     * - 检测玩家是否有祭坛光环
     * - 管理黑手狱卒的激活
     *
     * 战斗阶段：
     * - 处理战斗技能的施放
     * - 管理火焰护盾、火焰新星、火焰冲击和炎爆术
     *
     * @note 性能注意事项：避免在此函数中进行耗时操作，保持高效执行
     */
    void UpdateAI(uint32 diff) override
    {
        // 非战斗阶段处理
        if (!UpdateVictim())
        {
            events.Update(diff);

            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_RESPAWN:
                    {
                        // 设置黑手狱卒数据，通知副本埃博希尔已重生
                        instance->SetData(DATA_BLACKHAND_INCARCERATOR, 1);
                        instance->SetBossState(DATA_PYROGAURD_EMBERSEER, NOT_STARTED);
                        break;
                    }
                    case EVENT_PRE_FIGHT_1:
                    {
                        // 激活所有黑手狱卒进入战斗
                        std::list<Creature*> creatureList;
                        GetCreatureListWithEntryInGrid(creatureList, me, NPC_BLACKHAND_INCARCERATOR, 35.0f);
                        for (std::list<Creature*>::iterator itr = creatureList.begin(); itr != creatureList.end(); ++itr)
                        {
                            if (Creature* creature = *itr)
                            {
                                // 移除免疫状态
                                creature->SetImmuneToAll(false);
                                // 打断引导法术
                                creature->InterruptSpell(CURRENT_CHANNELED_SPELL);
                                // 使其进入战斗
                                DoZoneInCombat(creature);
                            }
                        }
                        // 移除埃博希尔的囚禁光环
                        me->RemoveAura(SPELL_ENCAGED_EMBERSEER);
                        // 安排战前阶段2，32秒后
                        events.ScheduleEvent(EVENT_PRE_FIGHT_2, 32s);
                        break;
                    }
                    case EVENT_PRE_FIGHT_2:
                        // 施放冻结动画和充能法术
                        me->CastSpell(me, SPELL_FREEZE_ANIM);
                        me->CastSpell(me, SPELL_EMBERSEER_GROWING);
                        // 显示单层充能表情
                        Talk(EMOTE_ONE_STACK);
                        break;
                    case EVENT_FIRE_SHIELD:
                        // #### 法术没有造成任何伤害 ??? ####
                        // 施放火焰护盾（补丁处理）
                        DoCast(me, SPELL_FIRE_SHIELD);
                        // 安排下一次火焰护盾，3秒后
                        events.ScheduleEvent(EVENT_FIRE_SHIELD, 3s);
                        break;
                    case EVENT_PLAYER_CHECK:
                    {
                        // 检查是否有玩家激活了祭坛
                        // 从3.0.8版本开始，只需要一个人引导祭坛
                        bool _hasAura = false;
                        Map::PlayerList const& players = me->GetMap()->GetPlayers();
                        for (Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
                            if (Player* player = itr->GetSource()->ToPlayer())
                                if (player->HasAura(SPELL_EMBERSEER_OBJECT_VISUAL))
                                {
                                    _hasAura = true;
                                    break;
                                }

                        // 如果有玩家激活祭坛，开始激活流程
                        if (_hasAura)
                        {
                            events.ScheduleEvent(EVENT_PRE_FIGHT_1, 1s);
                            instance->SetBossState(DATA_PYROGAURD_EMBERSEER, IN_PROGRESS);
                        }
                        break;
                    }
                    case EVENT_ENTER_COMBAT:
                        // 选择最近的玩家并开始攻击
                        AttackStart(me->SelectNearestPlayer(30.0f));
                        break;
                    default:
                        break;
                }
            }
            return;
        }

        // 战斗阶段处理
        events.Update(diff);

        // 如果正在施法，则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有到期事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FIRE_SHIELD:
                    // 施放火焰护盾
                    DoCast(me, SPELL_FIRE_SHIELD);
                    // 重复事件，3秒后
                    events.Repeat(Seconds(3));
                    break;
                case EVENT_FIRENOVA:
                    // 施放火焰新星
                    DoCast(me, SPELL_FIRENOVA);
                    // 重复事件，6秒后
                    events.Repeat(Seconds(6));
                    break;
                case EVENT_FLAMEBUFFET:
                    // 施放火焰冲击
                    DoCast(me, SPELL_FLAMEBUFFET);
                    // 重复事件，14秒后
                    events.Repeat(Seconds(14));
                    break;
                case EVENT_PYROBLAST:
                    // 随机选择一个有效目标施放炎爆术
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                        DoCast(target, SPELL_PYROBLAST);
                    // 重复事件，15秒后
                    events.Repeat(Seconds(15));
                    break;
                default:
                    break;
            }

            // 如果在事件处理过程中开始施法，则退出循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 如果没有在施法且准备就绪，进行近战攻击
        DoMeleeAttackIfReady();
    }
};

/*####
## npc_blackhand_incarcerator
####*/

/**
 * @brief 黑手狱卒事件ID枚举
 *
 * 定义黑手狱卒使用的所有事件ID
 */
enum IncarceratorEvents
{
    // 非战斗状态
    EVENT_ENCAGED_EMBERSEER         = 1,  ///< 囚禁埃博希尔事件
    // 战斗状态
    EVENT_STRIKE                    = 2,  ///< 打击事件
    EVENT_ENCAGE                    = 3   ///< 囚禁事件
};

/**
 * @brief 黑手狱卒 AI 结构体
 *
 * 继承自 ScriptedAI 基类，实现黑手狱卒的AI逻辑。
 * 黑手狱卒负责囚禁埃博希尔，在玩家激活事件后进入战斗。
 */
struct npc_blackhand_incarcerator : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 关联的生物对象指针
     *
     * 初始化 ScriptedAI 基类
     */
    npc_blackhand_incarcerator(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 刚出现回调
     *
     * 当生物生成时调用。
     * 立即施放囚禁埃博希尔法术。
     *
     * @note 调用时机：生物生成或重生
     */
    void JustAppeared() override
    {
        DoCast(SPELL_ENCAGE_EMBERSEER);
    }

    /**
     * @brief 进入战斗回调
     * @param who 触发战斗的单位（未使用）
     *
     * 当黑手狱卒进入战斗状态时调用。
     * 呼叫附近所有黑手狱卒进入战斗，
     * 并安排战斗技能的施放时机。
     *
     * @note 调用时机：黑手狱卒被玩家攻击或激活事件触发
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        // 必须这样做，因为CallForHelp会忽略任何没有LOS的NPC
        std::list<Creature*> creatureList;
        GetCreatureListWithEntryInGrid(creatureList, me, NPC_BLACKHAND_INCARCERATOR, 60.0f);
        for (std::list<Creature*>::iterator itr = creatureList.begin(); itr != creatureList.end(); ++itr)
        {
            if (Creature* creature = *itr)
                DoZoneInCombat(creature);    // AI()->AttackStart(me->GetVictim());
        }

        // 安排战斗技能
        _events.ScheduleEvent(EVENT_STRIKE, 8s, 16s);
        _events.ScheduleEvent(EVENT_ENCAGE, 10s, 20s);
    }

    /**
     * @brief 返回初始位置回调
     *
     * 当黑手狱卒脱战后返回初始位置时调用。
     * 重新施放囚禁埃博希尔法术并设置免疫状态。
     *
     * @note 调用时机：黑手狱卒脱战并返回初始位置
     */
    void JustReachedHome() override
    {
        DoCast(SPELL_ENCAGE_EMBERSEER);

        me->SetImmuneToAll(true);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 每个服务器tick调用一次，处理战斗AI的主逻辑循环。
     *
     * 技能循环：
     * - 打击：每14-23秒施放一次
     * - 囚禁：每6-12秒施放一次
     *
     * @note 性能注意事项：避免在此函数中进行耗时操作，保持高效执行
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效攻击目标
        if (!UpdateVictim())
            return;

        // 更新事件队列时间
        _events.Update(diff);

        // 处理所有到期事件
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_STRIKE:
                    // 对当前目标施放打击
                    DoCastVictim(SPELL_STRIKE, true);
                    // 重复事件，14-23秒后
                    _events.Repeat(Seconds(14), Seconds(23));
                    break;
                case EVENT_ENCAGE:
                    // 随机选择一个有效目标施放囚禁
                    DoCast(SelectTarget(SelectTargetMethod::Random, 0, 100, true), SPELL_ENCAGE, true);
                    // 重复事件，6-12秒后
                    _events.Repeat(Seconds(6), Seconds(12));
                    break;
                default:
                    break;
            }
        }

        // 如果没有在施法且准备就绪，进行近战攻击
        DoMeleeAttackIfReady();
    }

    private:
        EventMap _events;  ///< 事件映射表，管理技能施放时机
};

/**
 * @brief 注册烈焰卫士埃博希尔和黑手狱卒 AI
 *
 * 此函数将烈焰卫士埃博希尔和黑手狱卒的AI注册到脚本系统中，
 * 使游戏服务器能够正确加载和运行这些生物的AI逻辑。
 */
void AddSC_boss_pyroguard_emberseer()
{
    RegisterBlackrockSpireCreatureAI(boss_pyroguard_emberseer);
    RegisterBlackrockSpireCreatureAI(npc_blackhand_incarcerator);
}
