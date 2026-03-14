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
 * @file    boss_archimonde.cpp
 * @brief   海加尔山副本 - 阿克蒙德BOSS脚本
 * @details 该模块实现了阿克蒙德BOSS的完整战斗逻辑，包括：
 *          - 阿克蒙德的技能施放（末日之火、死亡之指、空气爆裂等）
 *          - 末日之火系统的AI控制
 *          - 灵魂充能机制
 *          - 最后阶段的守护之灵召唤
 *          - 世界之树距离检测
 *
 *          战斗机制说明：
 *          - 末日之火会随机追踪玩家，移动路径不可预测
 *          - 当玩家死亡时，阿克蒙德会获得对应的灵魂充能
 *          - 生命值低于10%时进入最后阶段，召唤守护之灵
 *          - 如果阿克蒙德靠近世界之树（75码内），会进入狂暴状态
 *
 * ScriptData
 * SDName: Boss_Archimonde
 * SD%Complete: 85
 * SDComment: Doomfires not completely offlike due to core limitations for random moving. Tyrande and second phase not fully implemented.
 * SDCategory: Caverns of Time, Mount Hyjal
 * EndScriptData
 */

#include "ScriptMgr.h"
#include "hyjal.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"

/**
 * @brief 阿克蒙德的文本枚举
 * @details 定义阿克蒙德在不同战斗阶段和事件中说的台词
 */
enum Texts
{
    SAY_AGGRO       = 1,    ///< 进入战斗时的台词
    SAY_DOOMFIRE    = 2,    ///< 施放末日之火时的台词
    SAY_AIR_BURST   = 3,    ///< 施放空气爆裂时的台词
    SAY_SLAY        = 4,    ///< 击杀玩家时的台词
    SAY_ENRAGE      = 5,    ///< 进入狂暴状态时的台词
    SAY_DEATH       = 6,    ///< 死亡时的台词
    SAY_SOUL_CHARGE = 7,    ///< 灵魂充能相关台词
    // YELL_ARCHIMONDE_INTRO = 8
};

/**
 * @brief 阿克蒙德使用的法术枚举
 * @details 包含所有阿克蒙德在战斗中使用的法术ID
 */
enum Spells
{
    // 最后阶段相关法术
    SPELL_DENOUEMENT_WISP            = 32124,  ///< 守护之灵的终结法术，用于击杀阿克蒙德
    SPELL_ANCIENT_SPARK              = 39349,  ///< 守护之灵对阿克蒙德使用的攻击法术
    SPELL_PROTECTION_OF_ELUNE        = 38528,  ///< 伊露恩的庇护，最后阶段给团队的免疫保护

    // 世界之树相关法术
    SPELL_DRAIN_WORLD_TREE           = 39140,  ///< 引导法术，从世界之树汲取能量
    SPELL_DRAIN_WORLD_TREE_TRIGGERED = 39141,  ///< 汲取世界之树的触发效果

    // 阿克蒙德主要技能
    SPELL_FINGER_OF_DEATH            = 31984,  ///< 死亡之指，对远程目标使用
    SPELL_FINGER_OF_DEATH_LAST_PHASE = 32111,  ///< 最后阶段使用的死亡之指
    SPELL_HAND_OF_DEATH              = 35354,  ///< 死亡之手，团灭技能，狂暴时使用
    SPELL_AIR_BURST                  = 32014,  ///< 空气爆裂，将目标击飞
    SPELL_GRIP_OF_THE_LEGION         = 31972,  ///< 军团之握，持续伤害法术
    SPELL_DOOMFIRE_STRIKE            = 31903,  ///< 末日之火打击，召唤两个生物
    SPELL_DOOMFIRE_SPAWN             = 32074,  ///< 末日之火生成效果
    SPELL_DOOMFIRE                   = 31945,  ///< 末日之火，在地面上持续燃烧

    // 灵魂充能相关法术（根据玩家职业分类）
    SPELL_SOUL_CHARGE_YELLOW         = 32045,  ///< 黄色灵魂充能（法师、盗贼、战士）
    SPELL_SOUL_CHARGE_GREEN          = 32051,  ///< 绿色灵魂充能（德鲁伊、萨满、猎人）
    SPELL_SOUL_CHARGE_RED            = 32052,  ///< 红色灵魂充能（牧师、圣骑士、术士）
    SPELL_UNLEASH_SOUL_YELLOW        = 32054,  ///< 释放黄色灵魂充能
    SPELL_UNLEASH_SOUL_GREEN         = 32057,  ///< 释放绿色灵魂充能
    SPELL_UNLEASH_SOUL_RED           = 32053,  ///< 释放红色灵魂充能
    SPELL_FEAR                       = 31970   ///< 恐惧法术，群体恐惧
};

/**
 * @brief 事件枚举
 * @details 定义阿克蒙德战斗中的各种事件，用于定时器调度
 */
enum Events
{
    EVENT_HAND_OF_DEATH = 1,        ///< 死亡之手事件（团灭技能）
    EVENT_UNLEASH_SOUL_CHARGE,      ///< 释放灵魂充能事件
    EVENT_FINGER_OF_DEATH,          ///< 死亡之指事件
    EVENT_GRIP_OF_THE_LEGION,       ///< 军团之握事件
    EVENT_FEAR,                     ///< 恐惧事件
    EVENT_AIR_BURST,                ///< 空气爆裂事件
    EVENT_DOOMFIRE,                 ///< 末日之火事件
    EVENT_DISTANCE_CHECK,           ///< 距离检测事件（检测是否太靠近世界之树，75码内则狂暴）
    EVENT_SUMMON_WHISP,             ///< 召唤守护之灵事件
    EVENT_PROTECTION_OF_ELUNE,      ///< 伊露恩的庇护事件
    EVENT_FINGER_OF_DEATH_LAST_PHASE ///< 最后阶段的死亡之指事件
};

/**
 * @brief 召唤生物枚举
 * @details 定义阿克蒙德战斗中召唤的各种NPC
 */
enum Summons
{
    NPC_DOOMFIRE               = 18095,  ///< 末日之火NPC，负责施放火焰效果
    NPC_DOOMFIRE_SPIRIT        = 18104,  ///< 末日之火精灵，控制末日之火的移动方向
    NPC_ANCIENT_WISP           = 17946   ///< 远古守护之灵，最后阶段召唤
};

/**
 * @brief 动作枚举
 * @details 定义用于AI通信的动作类型
 */
enum Actions
{
    ACTION_ENRAGE,              ///< 进入狂暴状态
    ACTION_CHANNEL_WORLD_TREE   ///< 引导世界之树汲取
};

/// 诺达希尔（世界之树）的位置坐标
Position const NordrassilLoc = { 5503.713f, -3523.436f, 1608.781f, 0.0f };

/**
 * @class   npc_ancient_wisp
 * @brief   远古守护之灵NPC脚本
 * @details 守护之灵在阿克蒙德生命值低于10%后召唤，
 *          它们会攻击阿克蒙德，当召唤足够数量（约30个）或阿克蒙德生命值低于2%时，
 *          守护之灵会施放终结法术击杀阿克蒙德。
 */
class npc_ancient_wisp : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    npc_ancient_wisp() : CreatureScript("npc_ancient_wisp") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回守护之灵AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetHyjalAI<npc_ancient_wispAI>(creature);
    }

    /**
     * @struct  npc_ancient_wispAI
     * @brief   远古守护之灵AI实现
     * @details 实现守护之灵的行为逻辑：
     *          - 定期检测阿克蒙德的生命值
     *          - 当阿克蒙德生命值低于2%或已死亡时，施放终结法术
     *          - 否则对阿克蒙德施放攻击法术
     */
    struct npc_ancient_wispAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        npc_ancient_wispAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
        }

        /**
         * @brief 初始化成员变量
         * @details 重置所有状态变量为初始值
         */
        void Initialize()
        {
            CheckTimer = 1000;        ///< 检测定时器，1秒检测一次
            ArchimondeGUID.Clear();   ///< 阿克蒙德的GUID
        }

        InstanceScript* instance;     ///< 副本脚本实例指针
        ObjectGuid ArchimondeGUID;    ///< 阿克蒙德的GUID，用于获取阿克蒙德对象
        uint32 CheckTimer;            ///< 检测定时器，单位毫秒

        /**
         * @brief 重置函数
         * @details 当生物重置时调用，初始化状态并设置不可攻击标志
         */
        void Reset() override
        {
            Initialize();

            // 获取阿克蒙德的GUID
            ArchimondeGUID = instance->GetGuidData(DATA_ARCHIMONDE);

            // 设置为不可攻击状态，防止被玩家攻击
            me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
        }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标
         * @note 守护之灵不会真正进入战斗
         */
        void JustEngagedWith(Unit* /*who*/) override { }

        /**
         * @brief 受到伤害回调
         * @param done_by 伤害来源
         * @param damage 伤害值（会被置为0）
         * @param damageType 伤害类型
         * @param spellInfo 法术信息
         * @details 守护之灵免疫所有伤害
         */
        void DamageTaken(Unit* /*done_by*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            damage = 0;  // 免疫所有伤害
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差（毫秒）
         * @details 每秒检测阿克蒙德状态：
         *          - 如果阿克蒙德生命值低于2%或已死亡，施放终结法术
         *          - 否则对阿克蒙德施放攻击法术
         */
        void UpdateAI(uint32 diff) override
        {
            if (CheckTimer <= diff)
            {
                // 获取阿克蒙德对象
                if (Creature* Archimonde = instance->GetCreature(DATA_ARCHIMONDE))
                {
                    // 如果阿克蒙德生命值低于2%或已死亡，施放终结法术
                    if (Archimonde->HealthBelowPct(2) || !Archimonde->IsAlive())
                        DoCast(me, SPELL_DENOUEMENT_WISP);
                    else
                        // 否则对阿克蒙德施放攻击法术
                        DoCast(Archimonde, SPELL_ANCIENT_SPARK);
                }
                CheckTimer = 1000;  // 重置检测定时器为1秒
            } else CheckTimer -= diff;
        }
    };
};

/**
 * @class   npc_doomfire
 * @brief   末日之火NPC脚本
 * @details 末日之火是阿克蒙德的主要技能之一，该NPC是一个视觉效果和伤害触发器。
 *          它会跟随末日之火精灵移动，在地面产生持续的火焰效果。
 *          末日之火的移动路径是不可预测的，这使得战斗更加困难。
 */
class npc_doomfire : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    npc_doomfire() : CreatureScript("npc_doomfire") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回末日之火AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetHyjalAI<npc_doomfireAI>(creature);
    }

    /**
     * @struct  npc_doomfireAI
     * @brief   末日之火AI实现
     * @details 末日之火的AI非常简单，它只是跟随末日之火精灵移动，
     *          并在路径上留下持续伤害的火焰效果。
     */
    struct npc_doomfireAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        npc_doomfireAI(Creature* creature) : ScriptedAI(creature) { }

        /**
         * @brief 重置函数
         */
        void Reset() override { }

        /**
         * @brief 视线检测回调
         * @param who 进入视线的单位
         * @note 末日之火不会对玩家做出反应
         */
        void MoveInLineOfSight(Unit* /*who*/) override { }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标
         * @note 末日之火不会进入战斗状态
         */
        void JustEngagedWith(Unit* /*who*/) override { }

        /**
         * @brief 受到伤害回调
         * @param done_by 伤害来源
         * @param damage 伤害值（会被置为0）
         * @param damageType 伤害类型
         * @param spellInfo 法术信息
         * @details 末日之火免疫所有伤害
         */
        void DamageTaken(Unit* /*done_by*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            damage = 0;  // 免疫所有伤害
        }
    };
};

/**
 * @class   npc_doomfire_targetting
 * @brief   末日之火精灵NPC脚本（控制末日之火的移动）
 * @details 末日之火精灵是控制末日之火移动路径的NPC。
 *          它会随机选择一个玩家作为目标并跟随该玩家，
 *          如果找不到目标，则随机移动。
 *          末日之火NPC会跟随这个精灵，从而实现不可预测的移动路径。
 */
class npc_doomfire_targetting : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    npc_doomfire_targetting() : CreatureScript("npc_doomfire_targetting") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回末日之火精灵AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetHyjalAI<npc_doomfire_targettingAI>(creature);
    }

    /**
     * @struct  npc_doomfire_targettingAI
     * @brief   末日之火精灵AI实现
     * @details 实现末日之火的移动控制逻辑：
     *          - 定期切换目标或随机移动
     *          - 优先追踪玩家
     *          - 如果没有目标则随机游走
     */
    struct npc_doomfire_targettingAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        npc_doomfire_targettingAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         */
        void Initialize()
        {
            TargetGUID.Clear();     ///< 当前目标玩家的GUID
            ChangeTargetTimer = 5000; ///< 目标切换定时器，5秒切换一次
        }

        ObjectGuid TargetGUID;       ///< 当前追踪目标的GUID
        uint32 ChangeTargetTimer;    ///< 目标切换定时器（毫秒）

        /**
         * @brief 重置函数
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 视线检测回调
         * @param who 进入视线的单位
         * @details 当玩家进入视线时，如果没有当前目标，则选择该玩家作为目标
         */
        void MoveInLineOfSight(Unit* who) override
        {
            // 当TargetGUID为空时，如果玩家进入视线，则选择该玩家作为目标
            // 如果没有人移动（不太可能），UpdateAI会强制选择随机点
            if (!TargetGUID && who->GetTypeId() == TYPEID_PLAYER)
                TargetGUID = who->GetGUID();
        }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标
         */
        void JustEngagedWith(Unit* /*who*/) override { }

        /**
         * @brief 受到伤害回调
         * @param done_by 伤害来源
         * @param damage 伤害值（会被置为0）
         * @param damageType 伤害类型
         * @param spellInfo 法术信息
         * @details 末日之火精灵免疫所有伤害
         */
        void DamageTaken(Unit* /*done_by*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            damage = 0;  // 免疫所有伤害
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差（毫秒）
         * @details 每5秒执行一次：
         *          - 如果有目标玩家，跟随该玩家
         *          - 如果没有目标，随机移动到附近位置
         */
        void UpdateAI(uint32 diff) override
        {
            if (ChangeTargetTimer <= diff)
            {
                // 尝试获取目标玩家
                if (Unit* temp = ObjectAccessor::GetUnit(*me, TargetGUID))
                {
                    // 跟随目标玩家
                    me->GetMotionMaster()->MoveFollow(temp, 0.0f, 0.0f);
                    TargetGUID.Clear();  // 清空目标，下次重新选择
                }
                else
                {
                    // 没有目标，随机移动到附近位置（40码范围内）
                    Position pos = me->GetRandomNearPosition(40);
                    me->GetMotionMaster()->MovePoint(0, pos.m_positionX, pos.m_positionY, pos.m_positionZ);
                }

                ChangeTargetTimer = 5000;  // 重置切换定时器为5秒
            } else ChangeTargetTimer -= diff;
        }
    };
};

/**
 * @class   boss_archimonde
 * @brief   阿克蒙德BOSS主脚本
 * @details 阿克蒙德是海加尔山副本的最终BOSS。
 *          战斗机制主要包括：
 *          - 定时施放各种技能（末日之火、死亡之指、空气爆裂、恐惧等）
 *          - 灵魂充能系统：当玩家死亡时，根据职业获得不同颜色的灵魂充能
 *          - 末日之火追踪系统：召唤末日之火追踪玩家
 *          - 距离检测：如果太靠近世界之树（75码内），进入狂暴状态
 *          - 最后阶段：生命值低于10%时召唤守护之灵
 *
 *          战斗流程：
 *          1. 进入战斗后开始引导世界之树汲取法术
 *          2. 定时施放各种技能攻击团队
 *          3. 当玩家死亡时获得灵魂充能，可以释放对团队造成伤害
 *          4. 生命值低于10%时进入最后阶段，召唤守护之灵
 *          5. 守护之灵数量达到约30个或阿克蒙德生命值极低时，阿克蒙德死亡
 */
class boss_archimonde : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     */
    boss_archimonde() : CreatureScript("boss_archimonde") { }

    /**
     * @struct  boss_archimondeAI
     * @brief   阿克蒙德AI实现
     * @details 继承自BossAI，实现阿克蒙德的完整战斗逻辑
     */
    struct boss_archimondeAI : public BossAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_archimondeAI(Creature* creature) : BossAI(creature, DATA_ARCHIMONDE)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         * @details 重置所有状态变量为初始值
         */
        void Initialize()
        {
            SoulChargeCount = 0;    ///< 灵魂充能数量
            WispCount = 0;          ///< 已召唤的守护之灵数量，达到约30个时阿克蒙德死亡
            _unleashSpell = 0;      ///< 释放灵魂充能的法术ID
            _chargeSpell = 0;       ///< 当前灵魂充能的法术ID

            Enraged = false;        ///< 是否已进入狂暴状态
            HasProtected = false;   ///< 是否已获得伊露恩的庇护保护
        }

        /**
         * @brief 初始化AI
         * @details 调用父类初始化
         */
        void InitializeAI() override
        {
            BossAI::InitializeAI();
        }

        /**
         * @brief 重置函数
         * @details 当BOSS重置时调用：
         *          - 清空所有状态
         *          - 清除召唤物
         *          - 重新开始引导世界之树汲取法术
         */
        void Reset() override
        {
            Initialize();
            _Reset();
            DoomfireSpiritGUID.Clear();
            summons.DespawnAll();

            // 获取世界之树引导目标并开始引导
            WorldtreeTragetGUID = instance->GetGuidData(DATA_CHANNEL_TARGET);
            if (Creature* WorldtreeTraget = ObjectAccessor::GetCreature(*me, WorldtreeTragetGUID))
            {
                DoCast(WorldtreeTraget, SPELL_DRAIN_WORLD_TREE);
            }
            me->RemoveAllAuras();  // 重置灵魂充能光环
        }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标
         * @details 进入战斗时：
         *          - 中断当前引导的法术
         *          - 说开场白
         *          - 调度所有技能事件
         */
        void JustEngagedWith(Unit* who) override
        {
            // 中断引导中的法术
            me->InterruptSpell(CURRENT_CHANNELED_SPELL);
            Talk(SAY_AGGRO);
            BossAI::JustEngagedWith(who);

            // 调度所有技能事件
            events.ScheduleEvent(EVENT_FEAR, 42s);                      // 恐惧，42秒冷却
            events.ScheduleEvent(EVENT_AIR_BURST, 30s);                 // 空气爆裂，30秒冷却
            events.ScheduleEvent(EVENT_GRIP_OF_THE_LEGION, 5s, 25s);    // 军团之握，5-25秒随机
            events.ScheduleEvent(EVENT_DOOMFIRE, 20s);                  // 末日之火，20秒冷却
            events.ScheduleEvent(EVENT_UNLEASH_SOUL_CHARGE, 2s, 30s);   // 释放灵魂充能，2-30秒随机
            events.ScheduleEvent(EVENT_FINGER_OF_DEATH, 15s);           // 死亡之指，15秒冷却
            events.ScheduleEvent(EVENT_HAND_OF_DEATH, 10min);           // 死亡之手（团灭技能），10分钟后
            events.ScheduleEvent(EVENT_DISTANCE_CHECK, 30s);            // 距离检测，30秒间隔
        }

        /**
         * @brief 执行事件
         * @param eventId 事件ID
         * @details 根据事件ID执行对应的技能或逻辑
         */
        void ExecuteEvent(uint32 eventId) override
        {
            switch (eventId)
            {
                case EVENT_HAND_OF_DEATH:
                    // 死亡之手：团灭技能，对全团造成巨额伤害
                    DoCastAOE(SPELL_HAND_OF_DEATH);
                    events.ScheduleEvent(EVENT_HAND_OF_DEATH, 2s);
                    break;

                case EVENT_UNLEASH_SOUL_CHARGE:
                    // 释放灵魂充能
                    _chargeSpell = 0;
                    _unleashSpell = 0;
                    me->InterruptNonMeleeSpells(false);

                    // 随机选择一种颜色的灵魂充能
                    switch (urand(0, 2))
                    {
                        case 0:  // 红色（牧师、圣骑士、术士）
                            _chargeSpell = SPELL_SOUL_CHARGE_RED;
                            _unleashSpell = SPELL_UNLEASH_SOUL_RED;
                            break;
                        case 1:  // 黄色（法师、盗贼、战士）
                            _chargeSpell = SPELL_SOUL_CHARGE_YELLOW;
                            _unleashSpell = SPELL_UNLEASH_SOUL_YELLOW;
                            break;
                        case 2:  // 绿色（德鲁伊、萨满、猎人）
                            _chargeSpell = SPELL_SOUL_CHARGE_GREEN;
                            _unleashSpell = SPELL_UNLEASH_SOUL_GREEN;
                            break;
                    }

                    // 如果存在该颜色的灵魂充能，则释放
                    if (me->HasAura(_chargeSpell))
                    {
                        me->RemoveAuraFromStack(_chargeSpell);
                        DoCastVictim(_unleashSpell);
                        SoulChargeCount--;
                        events.ScheduleEvent(EVENT_UNLEASH_SOUL_CHARGE, 2s, 30s);
                    }
                    break;

                case EVENT_FINGER_OF_DEATH:
                    // 死亡之指：如果近战范围内没有目标，则对随机目标施放
                    if (!SelectTarget(SelectTargetMethod::Random, 0, 5.0f))  // 检查近战范围（5码）内是否有目标
                    {
                        // 近战范围内没有目标，对随机目标施放死亡之指
                        DoCast(SelectTarget(SelectTargetMethod::Random, 0), SPELL_FINGER_OF_DEATH);
                        events.ScheduleEvent(EVENT_FINGER_OF_DEATH, 1s);
                    }
                    else
                        // 近战范围内有目标，延迟5秒再检测
                        events.ScheduleEvent(EVENT_FINGER_OF_DEATH, 5s);
                    break;

                case EVENT_GRIP_OF_THE_LEGION:
                    // 军团之握：对随机目标施放持续伤害法术
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_GRIP_OF_THE_LEGION);
                    events.ScheduleEvent(EVENT_GRIP_OF_THE_LEGION, 5s, 25s);
                    break;

                case EVENT_AIR_BURST:
                    // 空气爆裂：将目标击飞（不选择坦克）
                    Talk(SAY_AIR_BURST);
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1))  // 选择非主目标
                        DoCast(target, SPELL_AIR_BURST);
                    events.ScheduleEvent(EVENT_AIR_BURST, 25s, 40s);
                    break;

                case EVENT_FEAR:
                    // 恐惧：群体恐惧效果
                    DoCastAOE(SPELL_FEAR);
                    events.ScheduleEvent(EVENT_FEAR, 42s);
                    break;

                case EVENT_DOOMFIRE:
                    // 末日之火：召唤末日之火追踪目标
                    Talk(SAY_DOOMFIRE);
                    if (Unit* temp = SelectTarget(SelectTargetMethod::Random, 1))  // 选择非主目标
                        SummonDoomfire(temp);
                    else
                        SummonDoomfire(me->GetVictim());  // 如果没有其他目标，选择坦克
                    events.ScheduleEvent(EVENT_DOOMFIRE, 20s);
                    break;

                case EVENT_DISTANCE_CHECK:
                    // 距离检测：检测是否太靠近世界之树（75码内）
                    if (Creature* channelTrigger = instance->GetCreature(DATA_CHANNEL_TARGET))
                        if (me->IsWithinDistInMap(channelTrigger, 75.0f))
                            DoAction(ACTION_ENRAGE);  // 进入狂暴状态
                    events.ScheduleEvent(EVENT_DISTANCE_CHECK, 5s);
                    break;

                case EVENT_PROTECTION_OF_ELUNE:
                    // 伊露恩的庇护：最后阶段，重置所有事件，只使用死亡之指和死亡之手
                    events.Reset();
                    events.ScheduleEvent(EVENT_HAND_OF_DEATH, 1s);
                    events.ScheduleEvent(EVENT_FINGER_OF_DEATH_LAST_PHASE, 1s);
                    events.ScheduleEvent(EVENT_SUMMON_WHISP, 1s);
                    DoCastAOE(SPELL_PROTECTION_OF_ELUNE);
                    break;

                case EVENT_SUMMON_WHISP:
                    // 召唤守护之灵
                    DoSpawnCreature(NPC_ANCIENT_WISP, float(rand32() % 40), float(rand32() % 40), 0, 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 15s);
                    ++WispCount;
                    // 当召唤的守护之灵达到30个时，阿克蒙德死亡
                    if (WispCount >= 30)
                    {
                        me->KillSelf();
                        return;
                    }
                    events.ScheduleEvent(EVENT_SUMMON_WHISP, 1500ms);
                    break;

                case EVENT_FINGER_OF_DEATH_LAST_PHASE:
                    // 最后阶段的死亡之指：对随机目标施放
                    DoCast(SelectTarget(SelectTargetMethod::Random, 0), SPELL_FINGER_OF_DEATH_LAST_PHASE);
                    events.ScheduleEvent(EVENT_FINGER_OF_DEATH_LAST_PHASE, 1s);
                    break;

                default:
                    break;
            }
        }

        /**
         * @brief 受到伤害回调
         * @param attacker 攻击者
         * @param damage 伤害值
         * @param damageType 伤害类型
         * @param spellInfo 法术信息
         * @details 当生命值低于10%时：
         *          - 如果未狂暴，则进入狂暴状态
         *          - 如果未获得保护，则停止移动并调度伊露恩的庇护事件
         */
        void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            if (me->HealthBelowPctDamaged(10, damage))
            {
                // 如果未进入狂暴状态，则狂暴
                if (!Enraged)
                    DoAction(ACTION_ENRAGE);

                // 如果未获得保护，则进入最后阶段
                if (!HasProtected)
                {
                    me->GetMotionMaster()->Clear();
                    me->GetMotionMaster()->MoveIdle();
                    // 所有团队成员必须获得这个buff（免疫阿克蒙德的攻击）
                    events.ScheduleEvent(EVENT_PROTECTION_OF_ELUNE, 1ms);
                    HasProtected = true;
                }
            }
        }

        /**
         * @brief 击杀单位回调
         * @param victim 被击杀的单位
         * @details 当玩家被击杀时，根据玩家职业给予阿克蒙德对应的灵魂充能：
         *          - 红色：牧师、圣骑士、术士
         *          - 黄色：法师、盗贼、战士
         *          - 绿色：德鲁伊、萨满、猎人
         */
        void KilledUnit(Unit* victim) override
        {
            Talk(SAY_SLAY);

            // 只对玩家有效
            if (victim->GetTypeId() == TYPEID_PLAYER)
            {
                // 根据职业分配灵魂充能颜色
                switch (victim->GetClass())
                {
                    case CLASS_PRIEST:
                    case CLASS_PALADIN:
                    case CLASS_WARLOCK:
                        // 红色灵魂充能
                        victim->CastSpell(me, SPELL_SOUL_CHARGE_RED, true);
                        break;
                    case CLASS_MAGE:
                    case CLASS_ROGUE:
                    case CLASS_WARRIOR:
                        // 黄色灵魂充能
                        victim->CastSpell(me, SPELL_SOUL_CHARGE_YELLOW, true);
                        break;
                    case CLASS_DRUID:
                    case CLASS_SHAMAN:
                    case CLASS_HUNTER:
                        // 绿色灵魂充能
                        victim->CastSpell(me, SPELL_SOUL_CHARGE_GREEN, true);
                        break;
                }

                // 调度释放灵魂充能事件
                events.ScheduleEvent(EVENT_UNLEASH_SOUL_CHARGE, 2s, 30s);
                ++SoulChargeCount;
            }
        }

        /**
         * @brief 回到出生点回调
         * @details 重置后开始引导世界之树汲取法术
         */
        void JustReachedHome() override
        {
            DoAction(ACTION_CHANNEL_WORLD_TREE);
        }

        /**
         * @brief 死亡回调
         * @param killer 击杀者
         * @details 死亡时清理召唤物并标记副本进度
         */
        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_DEATH);
            summons.DespawnAll();
            _JustDied();
            // @todo: 当副本脚本更新后移除此行，仅为兼容性保留
            instance->SetData(DATA_ARCHIMONDE, DONE);
        }

        /**
         * @brief 召唤生物回调
         * @param summoned 被召唤的生物
         * @details 根据召唤的生物类型设置不同的行为：
         *          - 守护之灵：攻击阿克蒙德
         *          - 末日之火精灵：记录GUID供末日之火跟随
         *          - 末日之火：施放火焰效果并跟随末日之火精灵
         */
        void JustSummoned(Creature* summoned) override
        {
            switch (summoned->GetEntry())
            {
                case NPC_ANCIENT_WISP:
                    // 守护之灵攻击阿克蒙德
                    summoned->AI()->AttackStart(me);
                    break;

                case NPC_DOOMFIRE_SPIRIT:
                    // 记录末日之火精灵的GUID
                    DoomfireSpiritGUID = summoned->GetGUID();
                    break;

                case NPC_DOOMFIRE:
                {
                    // 施放末日之火生成效果
                    summoned->CastSpell(summoned, SPELL_DOOMFIRE_SPAWN, false);
                    // 施放末日之火法术
                    summoned->CastSpell(summoned, SPELL_DOOMFIRE, me->GetGUID());

                    // 让末日之火跟随末日之火精灵
                    if (Unit* DoomfireSpirit = ObjectAccessor::GetUnit(*me, DoomfireSpiritGUID))
                    {
                        summoned->GetMotionMaster()->MoveFollow(DoomfireSpirit, 0.0f, 0.0f);
                        DoomfireSpiritGUID.Clear();
                    }
                    break;
                }
                default:
                    break;
            }
        }

        /**
         * @brief 执行动作
         * @param actionId 动作ID
         * @details 处理外部触发的动作：
         *          - ACTION_ENRAGE：进入狂暴状态
         *          - ACTION_CHANNEL_WORLD_TREE：引导世界之树汲取
         */
        void DoAction(int32 actionId) override
        {
            switch (actionId)
            {
                case ACTION_ENRAGE:
                    // 进入狂暴状态：停止移动，说狂暴台词
                    me->GetMotionMaster()->Clear();
                    me->GetMotionMaster()->MoveIdle();
                    Enraged = true;
                    Talk(SAY_ENRAGE);
                    break;

                case ACTION_CHANNEL_WORLD_TREE:
                    // 引导世界之树汲取法术
                    DoCastAOE(SPELL_DRAIN_WORLD_TREE, true);
                    break;

                default:
                    break;
            }
        }

        /**
         * @brief 召唤末日之火
         * @param target 目标单位
         * @details 在目标位置附近召唤末日之火精灵和末日之火
         *          这个函数模拟了法术31903的效果
         */
        void SummonDoomfire(Unit* target)
        {
            if (!target)
                return;

            // 召唤末日之火精灵（在目标位置偏移+15处）
            me->SummonCreature(NPC_DOOMFIRE_SPIRIT,
                target->GetPositionX()+15.0f, target->GetPositionY()+15.0f, target->GetPositionZ(), 0,
                TEMPSUMMON_TIMED_DESPAWN, 27s);

            // 召唤末日之火（在目标位置偏移-15处）
            me->SummonCreature(NPC_DOOMFIRE,
                target->GetPositionX()-15.0f, target->GetPositionY()-15.0f, target->GetPositionZ(), 0,
                TEMPSUMMON_TIMED_DESPAWN, 27s);
        }

    private:
        ObjectGuid DoomfireSpiritGUID;   ///< 末日之火精灵的GUID
        ObjectGuid WorldtreeTragetGUID;  ///< 世界之树引导目标的GUID
        uint8 SoulChargeCount;           ///< 当前灵魂充能数量
        uint8 WispCount;                 ///< 已召唤的守护之灵数量
        uint32 _chargeSpell;             ///< 当前选择的灵魂充能法术ID
        uint32 _unleashSpell;            ///< 当前选择的释放灵魂法术ID
        bool Enraged;                    ///< 是否已进入狂暴状态
        bool HasProtected;               ///< 是否已获得伊露恩的庇护保护
    };

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回阿克蒙德AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetHyjalAI<boss_archimondeAI>(creature);
    }
};

/**
 * @class   spell_archimonde_drain_world_tree_dummy
 * @brief   汲取世界之树法术脚本
 * @details 法术ID: 39142 - 汲取世界之树虚设法术
 *          这个法术用于触发目标施放汲取世界之树效果回到施法者身上
 */
class spell_archimonde_drain_world_tree_dummy : public SpellScriptLoader
{
    public:
        /**
         * @brief 构造函数
         */
        spell_archimonde_drain_world_tree_dummy() : SpellScriptLoader("spell_archimonde_drain_world_tree_dummy") { }

        /**
         * @class   spell_archimonde_drain_world_tree_dummy_SpellScript
         * @brief   汲取世界之树法术脚本实现
         */
        class spell_archimonde_drain_world_tree_dummy_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_archimonde_drain_world_tree_dummy_SpellScript);

            /**
             * @brief 验证法术
             * @param spellInfo 法术信息
             * @return 验证是否成功
             */
            bool Validate(SpellInfo const* /*spellInfo*/) override
            {
                return ValidateSpellInfo({ SPELL_DRAIN_WORLD_TREE_TRIGGERED });
            }

            /**
             * @brief 处理脚本效果
             * @param effIndex 效果索引
             * @details 让目标对施法者施放汲取世界之树触发法术
             */
            void HandleScript(SpellEffIndex /*effIndex*/)
            {
                if (Unit* target = GetHitUnit())
                    target->CastSpell(GetCaster(), SPELL_DRAIN_WORLD_TREE_TRIGGERED, true);
            }

            /**
             * @brief 注册法术效果回调
             */
            void Register() override
            {
                OnEffectHitTarget += SpellEffectFn(spell_archimonde_drain_world_tree_dummy_SpellScript::HandleScript, EFFECT_0, SPELL_EFFECT_DUMMY);
            }
        };

        /**
         * @brief 获取法术脚本实例
         * @return 返回法术脚本实例
         */
        SpellScript* GetSpellScript() const override
        {
            return new spell_archimonde_drain_world_tree_dummy_SpellScript();
        }
};

/**
 * @class   spell_protection_of_elune
 * @brief   伊露恩的庇护光环脚本
 * @details 法术ID: 38528 - 伊露恩的庇护
 *          这个光环在阿克蒙德生命值低于10%时施放给所有团队成员，
 *          提供对死亡之手、死亡之指等法术的免疫。
 */
class spell_protection_of_elune : public AuraScript
{
    PrepareAuraScript(spell_protection_of_elune);

    /**
     * @brief 验证法术
     * @param spellInfo 法术信息
     * @return 验证是否成功
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_PROTECTION_OF_ELUNE
        });
    }

    /**
     * @brief 处理光环应用效果
     * @param aurEff 光环效果
     * @param mode 处理模式
     * @details 当光环应用时，设置对阿克蒙德特定法术的免疫
     */
    void HandleEffectApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        // 设置对死亡之手、死亡之指等法术的免疫
        target->ApplySpellImmune(SPELL_HAND_OF_DEATH, IMMUNITY_ID, SPELL_HAND_OF_DEATH, true);
        target->ApplySpellImmune(SPELL_FINGER_OF_DEATH, IMMUNITY_ID, SPELL_FINGER_OF_DEATH, true);
        target->ApplySpellImmune(SPELL_FINGER_OF_DEATH_LAST_PHASE, IMMUNITY_ID, SPELL_FINGER_OF_DEATH_LAST_PHASE, true);
        target->ApplySpellImmune(0, IMMUNITY_ID, SPELL_FINGER_OF_DEATH_LAST_PHASE, true);
    }

    /**
     * @brief 处理光环移除效果
     * @param aurEff 光环效果
     * @param mode 处理模式
     * @details 当光环移除时，清除免疫效果
     */
    void HandleEffectRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        // 移除免疫效果
        target->ApplySpellImmune(SPELL_HAND_OF_DEATH, IMMUNITY_ID, SPELL_HAND_OF_DEATH, false);
        target->ApplySpellImmune(SPELL_FINGER_OF_DEATH, IMMUNITY_ID, SPELL_FINGER_OF_DEATH, false);
        target->ApplySpellImmune(SPELL_FINGER_OF_DEATH_LAST_PHASE, IMMUNITY_ID, SPELL_FINGER_OF_DEATH_LAST_PHASE, false);
        target->ApplySpellImmune(0, IMMUNITY_ID, SPELL_FINGER_OF_DEATH_LAST_PHASE, false);
    }

    /**
     * @brief 注册光环效果回调
     */
    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_protection_of_elune::HandleEffectApply, EFFECT_0, SPELL_AURA_SCHOOL_IMMUNITY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_protection_of_elune::HandleEffectRemove, EFFECT_0, SPELL_AURA_SCHOOL_IMMUNITY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 添加阿克蒙德脚本到系统
 * @details 注册所有阿克蒙德相关的脚本到脚本系统中
 */
void AddSC_boss_archimonde()
{
    new boss_archimonde();
    new npc_doomfire();
    new npc_doomfire_targetting();
    new npc_ancient_wisp();
    new spell_archimonde_drain_world_tree_dummy();
    RegisterSpellScript(spell_protection_of_elune);
}
