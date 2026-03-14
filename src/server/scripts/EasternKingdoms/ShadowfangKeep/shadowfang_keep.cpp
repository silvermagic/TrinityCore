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
 * @file shadowfang_keep.cpp
 * @brief 影牙城堡副本主要NPC和BOSS脚本
 *
 * 该模块实现影牙城堡副本内的以下内容：
 * - NPC影牙城堡囚犯（Ash和Ada）：护送任务NPC，负责开启庭院大门
 * - NPC阿鲁高的虚空行者：芬鲁斯战斗后召唤的小怪
 * - BOSS大法师阿鲁高：副本最终BOSS，拥有传送、诅咒等技能
 * - 法术脚本：鬼魂之灵（Haunting Spirits）相关效果
 *
 * @note 影牙城堡是位于银松森林的低级副本，适合15-20级玩家
 * @note 副本故事背景：大法师阿鲁高被贪婪蒙蔽，召唤狼人诅咒了这片土地
 */

/* ScriptData
SDName: Shadowfang_Keep
SD%Complete: 75
SDComment: npc_shadowfang_prisoner using escortAI for movement to door. Might need additional code in case being attacked. Add proper texts/say().
SDCategory: Shadowfang Keep
EndScriptData */

/* ContentData
npc_shadowfang_prisoner
EndContentData */

#include "ScriptMgr.h"
#include "shadowfang_keep.h"
#include "InstanceScript.h"
#include "Player.h"
#include "ScriptedEscortAI.h"
#include "ScriptedGossip.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

/*######
## npc_shadowfang_prisoner
######*/

/**
 * @brief NPC对话文本枚举
 *
 * 定义影牙城堡囚犯（Ash和Ada）在不同场景下的对话文本ID
 */
enum Yells
{
    SAY_FREE_AS             = 0,    ///< Ash获得自由时的对话
    SAY_OPEN_DOOR_AS        = 1,    ///< Ash开门时的对话
    SAY_POST_DOOR_AS        = 2,    ///< Ash开门后的对话
    SAY_FREE_AD             = 0,    ///< Ada获得自由时的对话
    SAY_OPEN_DOOR_AD        = 1,    ///< Ada开门时的对话
    SAY_POST1_DOOR_AD       = 2,    ///< Ada开门后的对话1
    SAY_POST2_DOOR_AD       = 3     ///< Ada开门后的对话2
};

/**
 * @brief 法术ID枚举
 *
 * 定义副本内使用的各种法术ID
 */
enum Spells
{
    SPELL_UNLOCK            = 6421, ///< 开锁法术 - 用于打开庭院大门
    SPELL_DARK_OFFERING     = 7154  ///< 黑暗祭品 - 虚空行者治疗技能
};

/**
 * @brief 影牙城堡囚犯NPC脚本
 *
 * 实现护送任务NPC（Ash和Ada）的行为逻辑：
 * - 玩家与NPC对话后触发护送任务
 * - NPC移动到庭院大门并开门
 * - 开门后触发副本进度更新
 *
 * @note 继承自CreatureScript基类
 */
class npc_shadowfang_prisoner : public CreatureScript
{
public:
    npc_shadowfang_prisoner() : CreatureScript("npc_shadowfang_prisoner") { }

    /**
     * @brief 影牙城堡囚犯AI结构体
     *
     * 实现护送任务的核心AI逻辑，继承自EscortAI护送AI基类
     */
    struct npc_shadowfang_prisonerAI : public EscortAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        npc_shadowfang_prisonerAI(Creature* creature) : EscortAI(creature)
        {
            instance = creature->GetInstanceScript();
        }

        InstanceScript* instance;   ///< 副本实例脚本指针，用于访问副本数据

        /**
         * @brief 路径点到达回调函数
         *
         * 当NPC到达指定路径点时触发，执行相应剧情事件：
         * - 路径点0：触发获得自由对话
         * - 路径点10：触发开门对话
         * - 路径点11：Ash施放开锁法术
         * - 路径点12：触发开门后对话，更新副本进度
         * - 路径点13：Ada的额外对话
         *
         * @param waypointId 到达的路径点ID
         * @param pathId 路径ID（未使用）
         */
        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            if (Player* player = GetPlayerForEscort())
            {
                switch (waypointId)
                {
                    case 0:
                        // 根据NPC类型选择不同对话
                        if (me->GetEntry() == NPC_ASH)
                            Talk(SAY_FREE_AS, player);
                        else
                            Talk(SAY_FREE_AD, player);
                        break;
                    case 10:
                        // 到达门前触发的对话
                        if (me->GetEntry() == NPC_ASH)
                            Talk(SAY_OPEN_DOOR_AS, player);
                        else
                            Talk(SAY_OPEN_DOOR_AD, player);
                        break;
                    case 11:
                        // Ash施放开锁法术打开庭院大门
                        if (me->GetEntry() == NPC_ASH)
                            DoCast(me, SPELL_UNLOCK);
                        break;
                    case 12:
                        // 开门后的对话，并标记副本进度
                        if (me->GetEntry() == NPC_ASH)
                            Talk(SAY_POST_DOOR_AS, player);
                        else
                            Talk(SAY_POST1_DOOR_AD, player);

                        // 更新副本进度，开启庭院大门
                        instance->SetData(TYPE_FREE_NPC, DONE);
                        break;
                    case 13:
                        // Ada的额外对话
                        if (me->GetEntry() != NPC_ASH)
                            Talk(SAY_POST2_DOOR_AD, player);
                        break;
                }
            }
        }

        /**
         * @brief 重置回调
         *
         * 当NPC脱离战斗或重置时调用
         */
        void Reset() override { }

        /**
         * @brief 进入战斗回调
         * @param who 攻击者（未使用）
         */
        void JustEngagedWith(Unit* /*who*/) override { }

        /**
         * @brief 菜单选项选择回调
         *
         * 处理玩家选择对话菜单选项的事件
         *
         * @param player 玩家对象指针
         * @param menuId 菜单ID（未使用）
         * @param gossipListId 对话选项列表ID
         * @return true 处理成功
         */
        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);
            if (action == GOSSIP_ACTION_INFO_DEF + 1)
            {
                CloseGossipMenuFor(player);
                // 开始护送任务，不逃跑、不攻击，以玩家为引导者
                Start(false, false, player->GetGUID());
            }
            return true;
        }

        /**
         * @brief 对话问候回调
         *
         * 当玩家与NPC对话时触发，显示对话菜单
         *
         * @param player 玩家对象指针
         * @return true 处理成功
         *
         * @note 仅在Rethilgore被击杀且囚犯尚未自由时显示护送选项
         */
        bool OnGossipHello(Player* player) override
        {
            uint32 gossipMenuId = Player::GetDefaultGossipMenuForSource(me);
            InitGossipMenuFor(player, gossipMenuId);
            // 检查前置条件：囚犯未自由且Rethilgore已死亡
            if (instance->GetData(TYPE_FREE_NPC) != DONE && instance->GetData(TYPE_RETHILGORE) == DONE)
                AddGossipItemFor(player, gossipMenuId, 0, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

            SendGossipMenuFor(player, player->GetGossipTextId(me), me->GetGUID());
            return true;
        }
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetShadowfangKeepAI<npc_shadowfang_prisonerAI>(creature);
    }
};

/**
 * @brief 阿鲁高的虚空行者NPC脚本
 *
 * 实现阿鲁高召唤的虚空行者的行为逻辑：
 * - 使用黑暗祭品技能互相治疗
 * - 死亡后通知副本脚本更新芬鲁斯战斗进度
 *
 * @note 这些虚空行者在芬鲁斯BOSS死亡后被召唤，必须全部击杀才能打开巫师门
 */
class npc_arugal_voidwalker : public CreatureScript
{
public:
    npc_arugal_voidwalker() : CreatureScript("npc_arugal_voidwalker") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetShadowfangKeepAI<npc_arugal_voidwalkerAI>(creature);
    }

    /**
     * @brief 阿鲁高的虚空行者AI结构体
     *
     * 实现虚空行者的战斗AI，主要特点是使用黑暗祭品治疗同伴
     */
    struct npc_arugal_voidwalkerAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        npc_arugal_voidwalkerAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
        }

        /**
         * @brief 初始化成员变量
         *
         * 设置黑暗祭品技能的初始冷却时间
         */
        void Initialize()
        {
            uiDarkOffering = urand(200, 1000);  ///< 随机初始冷却时间（200-1000毫秒）
        }

        InstanceScript* instance;       ///< 副本实例脚本指针
        uint32 uiDarkOffering;          ///< 黑暗祭品技能冷却计时器（毫秒）

        /**
         * @brief 重置回调
         *
         * 重置技能冷却计时器
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 更新AI
         *
         * 处理虚空行者的战斗逻辑：
         * - 定期对同伴或自己施放黑暗祭品治疗法术
         * - 执行近战攻击
         *
         * @param uiDiff 距离上次更新的时间差（毫秒）
         */
        void UpdateAI(uint32 uiDiff) override
        {
            if (!UpdateVictim())
                return;

            // 黑暗祭品技能冷却处理
            if (uiDarkOffering <= uiDiff)
            {
                // 优先治疗附近的同类单位，否则治疗自己
                if (Creature* pFriend = me->FindNearestCreature(me->GetEntry(), 25.0f, true))
                    DoCast(pFriend, SPELL_DARK_OFFERING);
                else
                    DoCast(me, SPELL_DARK_OFFERING);
                // 设置下一次施放时间（4.4-12.5秒）
                uiDarkOffering = urand(4400, 12500);
            } else uiDarkOffering -= uiDiff;

            DoMeleeAttackIfReady();
        }

        /**
         * @brief 死亡回调
         * @param killer 击杀者（未使用）
         *
         * 通知副本脚本增加虚空行者击杀计数
         *
         * @note 当所有虚空行者死亡后，副本会打开巫师门
         */
        void JustDied(Unit* /*killer*/) override
        {
            // 增加虚空行者死亡计数，当达到4时打开巫师门
            instance->SetData(TYPE_FENRUS, instance->GetData(TYPE_FENRUS) + 1);
        }
    };

};

/**
 * @brief 阿鲁高法术ID枚举
 *
 * 定义大法师阿鲁高BOSS使用的各种法术ID
 */
enum ArugalSpells
{
    SPELL_TELE_UPPER    = 7587,     ///< 传送到上层平台的法术
    SPELL_TELE_SPAWN    = 7586,     ///< 传送到初始生成点的法术
    SPELL_TELE_STAIRS   = 7136,     ///< 传送到楼梯位置的法术
    NUM_TELEPORT_SPELLS = 3,        ///< 传送法术数量
    SPELL_ARUGAL_CURSE  = 7621,     ///< 阿鲁高的诅咒 - 将玩家变成狼人
    SPELL_THUNDERSHOCK  = 7803,     ///< 雷霆震击 - AOE伤害技能
    SPELL_VOIDBOLT      = 7588      ///< 虚空箭 - 主要攻击法术
};

/**
 * @brief 阿鲁高对话文本枚举
 *
 * 定义大法师阿鲁高的各种对话文本ID
 */
enum ArugalTexts
{
    SAY_AGGRO       = 1,    ///< 进入战斗："你也将为我效劳！"
    SAY_TRANSFORM   = 2,    ///< 施放诅咒："释放你的愤怒！"
    SAY_SLAY        = 3     ///< 击杀玩家："又一个倒下了！"
};

/**
 * @brief 阿鲁高事件ID枚举
 *
 * 定义BOSS战斗中使用的各种事件计时器ID
 */
enum ArugalEvents
{
    EVENT_VOID_BOLT = 1,    ///< 虚空箭事件
    EVENT_TELEPORT,         ///< 传送事件
    EVENT_THUNDERSHOCK,     ///< 雷霆震击事件
    EVENT_CURSE             ///< 诅咒事件
};

/**
 * @brief 大法师阿鲁高BOSS脚本
 *
 * 实现影牙城堡最终BOSS大法师阿鲁高的战斗逻辑：
 * - 主要技能：虚空箭、阿鲁高的诅咒、雷霆震击、传送
 * - 传送机制：在三个固定位置间随机传送，避免连续传送至同一位置
 * - 诅咒效果：将随机玩家变成狼人
 * - 战斗风格：法师型BOSS，保持距离攻击
 *
 * @note 阿鲁高是影牙城堡的最终BOSS，等级24-26
 */
class boss_archmage_arugal : public CreatureScript
{
    public:
        boss_archmage_arugal() : CreatureScript("boss_archmage_arugal") { }

        /**
         * @brief 大法师阿鲁高AI结构体
         *
         * 继承自BossAI，实现BOSS战斗的核心逻辑
         */
        struct boss_archmage_arugalAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_archmage_arugalAI(Creature* creature) : BossAI(creature, BOSS_ARUGAL) { }

            /**
             * @brief 传送法术数组
             *
             * 存储三个传送法术ID，用于随机传送逻辑
             * 通过交换数组元素确保不会连续两次传送至同一位置
             */
            uint32 teleportSpells[NUM_TELEPORT_SPELLS] =
            {
                SPELL_TELE_SPAWN,   ///< 初始生成点
                SPELL_TELE_UPPER,   ///< 上层平台
                SPELL_TELE_STAIRS   ///< 楼梯位置
            };

            /**
             * @brief 击杀单位回调
             * @param who 被击杀的单位
             *
             * 当击杀玩家时触发击杀对话
             */
            void KilledUnit(Unit* who) override
            {
                if (who->GetTypeId() == TYPEID_PLAYER)
                    Talk(SAY_SLAY);
            }

            /**
             * @brief 法术命中目标回调
             * @param target 目标对象（未使用）
             * @param spellInfo 法术信息
             *
             * 当施放阿鲁高的诅咒时触发变身对话
             */
            void SpellHitTarget(WorldObject* /*target*/, SpellInfo const* spellInfo) override
            {
                if (spellInfo->Id == SPELL_ARUGAL_CURSE)
                    Talk(SAY_TRANSFORM);
            }

            /**
             * @brief 进入战斗回调
             * @param who 进入战斗的目标
             *
             * 初始化BOSS战斗事件计时器：
             * - 7秒后施放诅咒
             * - 15秒后传送
             * - 1秒后施放虚空箭
             * - 10秒后施放雷霆震击
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                Talk(SAY_AGGRO);
                events.ScheduleEvent(EVENT_CURSE, 7s);
                events.ScheduleEvent(EVENT_TELEPORT, 15s);
                events.ScheduleEvent(EVENT_VOID_BOLT, 1s);
                events.ScheduleEvent(EVENT_THUNDERSHOCK, 10s);
            }

            /**
             * @brief 开始攻击回调
             * @param who 攻击目标
             *
             * 设置法师型BOSS的攻击行为，保持100码距离施法
             */
            void AttackStart(Unit* who) override
            {
                AttackStartCaster(who, 100.0f); // 虚空箭射程为100码
            }

            /**
             * @brief 更新AI
             *
             * 处理BOSS战斗的核心逻辑：
             * - 虚空箭：对当前目标施放，5秒冷却
             * - 诅咒：随机选择目标施放狼人诅咒，15秒冷却
             * - 传送：随机传送到三个位置之一，20秒冷却
             * - 雷霆震击：AOE伤害，30秒冷却
             *
             * @param diff 距离上次更新的时间差（毫秒）
             */
            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                events.Update(diff);

                // 施法时不执行其他动作
                if (me->HasUnitState(UNIT_STATE_CASTING))
                    return;

                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_CURSE:
                            // 对随机玩家施放狼人诅咒（30码范围内）
                            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 30.0f, true))
                                DoCast(target, SPELL_ARUGAL_CURSE);
                            events.Repeat(Seconds(15));
                            break;
                        case EVENT_TELEPORT:
                        {
                            // 确保不会连续两次传送至同一位置
                            uint8 spellIndex = urand(1, NUM_TELEPORT_SPELLS-1);
                            std::swap(teleportSpells[0], teleportSpells[spellIndex]);
                            DoCast(teleportSpells[0]);
                            events.Repeat(Seconds(20));
                            break;
                        }
                        case EVENT_THUNDERSHOCK:
                            // 施放AOE雷霆震击
                            DoCastAOE(SPELL_THUNDERSHOCK);
                            events.Repeat(Seconds(30));
                            break;
                        case EVENT_VOID_BOLT:
                            // 对当前目标施放虚空箭
                            DoCastVictim(SPELL_VOIDBOLT);
                            events.Repeat(Seconds(5));
                            break;
                    }
                }
                DoMeleeAttackIfReady();
            }
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象指针
         * @return AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetShadowfangKeepAI<boss_archmage_arugalAI>(creature);
        }
};

/**
 * @brief 鬼魂之灵法术脚本 (Spell ID: 7057)
 *
 * 实现鬼魂之灵光环效果：
 * - 随机周期的周期性触发效果
 * - 周期时间在30-90秒之间随机变化
 * - 每次周期触发时施放指定法术
 *
 * @note 该法术用于影牙城堡中的某些亡灵生物
 */
class spell_shadowfang_keep_haunting_spirits : public SpellScriptLoader
{
    public:
        spell_shadowfang_keep_haunting_spirits() : SpellScriptLoader("spell_shadowfang_keep_haunting_spirits") { }

        /**
         * @brief 鬼魂之灵光环脚本
         */
        class spell_shadowfang_keep_haunting_spirits_AuraScript : public AuraScript
        {
            PrepareAuraScript(spell_shadowfang_keep_haunting_spirits_AuraScript);

            /**
             * @brief 计算周期性参数
             *
             * 设置光环为周期性触发，并随机化周期时间
             *
             * @param aurEff 光环效果
             * @param isPeriodic [out] 是否为周期性
             * @param amplitude [out] 周期时间（毫秒）
             */
            void CalcPeriodic(AuraEffect const* /*aurEff*/, bool& isPeriodic, int32& amplitude)
            {
                isPeriodic = true;
                // 随机周期时间：30-90秒
                amplitude = (irand(0, 60) + 30) * IN_MILLISECONDS;
            }

            /**
             * @brief 处理周期性触发
             *
             * 每次周期触发时施放效果值指定的法术
             *
             * @param aurEff 光环效果，包含要施放的法术ID
             */
            void HandleDummyTick(AuraEffect const* aurEff)
            {
                GetTarget()->CastSpell(nullptr, aurEff->GetAmount(), true);
            }

            /**
             * @brief 更新周期性参数
             *
             * 每次触发后重新计算周期时间，实现随机间隔
             *
             * @param aurEff 光环效果
             */
            void HandleUpdatePeriodic(AuraEffect* aurEff)
            {
                aurEff->CalculatePeriodic(GetCaster());
            }

            /**
             * @brief 注册回调函数
             */
            void Register() override
            {
                DoEffectCalcPeriodic += AuraEffectCalcPeriodicFn(spell_shadowfang_keep_haunting_spirits_AuraScript::CalcPeriodic, EFFECT_0, SPELL_AURA_DUMMY);
                OnEffectPeriodic += AuraEffectPeriodicFn(spell_shadowfang_keep_haunting_spirits_AuraScript::HandleDummyTick, EFFECT_0, SPELL_AURA_DUMMY);
                OnEffectUpdatePeriodic += AuraEffectUpdatePeriodicFn(spell_shadowfang_keep_haunting_spirits_AuraScript::HandleUpdatePeriodic, EFFECT_0, SPELL_AURA_DUMMY);
            }
        };

        /**
         * @brief 获取光环脚本实例
         * @return 光环脚本指针
         */
        AuraScript* GetAuraScript() const override
        {
            return new spell_shadowfang_keep_haunting_spirits_AuraScript();
        }
};

/**
 * @brief 注册所有影牙城堡脚本
 *
 * 该函数由脚本系统在启动时调用，注册以下脚本：
 * - npc_shadowfang_prisoner：影牙城堡囚犯NPC
 * - npc_arugal_voidwalker：阿鲁高的虚空行者
 * - boss_archmage_arugal：大法师阿鲁高BOSS
 * - spell_shadowfang_keep_haunting_spirits：鬼魂之灵法术
 */
void AddSC_shadowfang_keep()
{
    new npc_shadowfang_prisoner();
    new npc_arugal_voidwalker();
    new boss_archmage_arugal();
    new spell_shadowfang_keep_haunting_spirits();
}
