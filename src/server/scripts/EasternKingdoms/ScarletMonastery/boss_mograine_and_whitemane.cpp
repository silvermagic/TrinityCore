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
 * @file boss_mograine_and_whitemane.cpp
 * @brief 血色修道院最终BOSS战斗脚本 - 血色指挥官莫格莱尼和大检察官怀特迈恩
 *
 * 本模块实现了血色修道院（大教堂）的双BOSS战斗机制：
 * - 血色指挥官莫格莱尼（第一阶段BOSS）
 * - 大检察官怀特迈恩（第二阶段BOSS）
 *
 * 战斗流程：
 * 1. 玩家首先与莫格莱尼交战
 * 2. 莫格莱尼"死亡"时进入假死状态，触发大门开启
 * 3. 怀特迈恩从密室进入战斗
 * 4. 怀特迈恩血量低于50%时施放深度睡眠，复活莫格莱尼
 * 5. 玩家需要同时击杀两个BOSS才能完成战斗
 *
 * 特殊机制：
 * - 莫格莱尼的假死和复活机制
 * - 怀特迈恩的治疗和复活能力
 * - 双BOSS协同作战的AI逻辑
 */

#include "scarlet_monastery.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "Timer.h"

/**
 * @brief BOSS对话文本ID枚举
 *
 * 定义莫格莱尼和怀特迈恩在战斗中的各种对话文本ID
 */
enum MograineAndWhitemaneSays
{
    // 莫格莱尼对话
    SAY_MO_AGGRO       = 0,  // 进入战斗时的喊话
    SAY_MO_KILL        = 1,  // 击杀玩家时的喊话
    SAY_MO_RESURRECTED = 2,  // 被复活后的喊话

    // 怀特迈恩对话
    SAY_WH_INTRO       = 0,  // 进入战斗时的介绍语
    SAY_WH_KILL        = 1,  // 击杀玩家时的喊话
    SAY_WH_RESURRECT   = 2,  // 复活莫格莱尼时的喊话
};

/**
 * @brief 法术ID枚举
 *
 * 定义莫格莱尼和怀特迈恩使用的所有法术ID
 */
enum MograineAndWhitemaneSpells
{
    // 莫格莱尼法术
    SPELL_CRUSADER_STRIKE      = 14518,  // 十字军打击 - 主要攻击技能
    SPELL_HAMMER_OF_JUSTICE    = 5589,   // 制裁之锤 - 眩晕技能
    SPELL_LAY_ONHANDS          = 9257,   // 圣疗术 - 治疗技能（未使用）
    SPELL_RETRIBUTION_AURA     = 8990,   // 惩戒光环 - 被动光环效果

    // 怀特迈恩法术
    SPELL_DEEP_SLEEP           = 9256,   // 深度睡眠 - 使所有玩家昏睡
    SPELL_SCARLET_RESURRECTION = 9232,   // 血色复活 - 复活莫格莱尼
    SPELL_DOMINATE_MIND        = 14515,  // 精神控制（未使用）
    SPELL_HOLY_SMITE           = 9481,   // 神圣惩击 - 主要攻击法术
    SPELL_HEAL                 = 12039,  // 治疗术 - 治疗自己或莫格莱尼
    SPELL_POWER_WORD_SHIELD    = 22187   // 真言术：盾 - 保护自己
};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中使用的定时事件ID，用于技能冷却和施放计划
 */
enum MograineAndWhitemaneEvents
{
    // 莫格莱尼事件
    EVENT_CRUSADER_STRIKE = 1,     // 十字军打击事件
    EVENT_HAMMER_OF_JUSTICE,       // 制裁之锤事件

    // 怀特迈恩事件
    EVENT_HEAL = 1,                // 治疗术事件
    EVENT_POWER_WORD_SHIELD,       // 真言术：盾事件
    EVENT_HOLY_SMITE,              // 神圣惩击事件
};

/**
 * @brief 路点ID枚举
 *
 * 定义怀特迈恩移动到莫格莱尼身边的路点ID
 */
enum MograineAndWhitemanePoints
{
    POINT_WHITEMANE_MOVE_TO_MOGRAINE = 1,  // 怀特迈恩移动到莫格莱尼位置的路点
};

/**
 * @brief 怀特迈恩初始移动位置
 *
 * 定义怀特迈恩从密室出来后的移动目标位置
 */
Position const WhitemaneIntroMovePos = { 1163.113370f, 1398.856812f, 32.527786f, 0.f };

/**
 * @brief 血色指挥官莫格莱尼AI结构体
 *
 * 实现血色指挥官莫格莱尼的战斗AI逻辑，包括：
 * - 常规战斗技能（十字军打击、制裁之锤）
 * - 假死机制
 * - 复活后重新进入战斗
 *
 * 战斗流程：
 * 1. 进入战斗后施放惩戒光环并呼叫援助
 * 2. 定期使用十字军打击和制裁之锤
 * 3. 血量降为0时触发假死，等待怀特迈恩复活
 * 4. 被复活后重新加入战斗
 */
struct boss_scarlet_commander_mograine : public BossAI
{
public:
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_scarlet_commander_mograine(Creature* creature) : BossAI(creature, DATA_MOGRAINE_AND_WHITE_EVENT), _killYellTimer(0s)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 将假死和可死亡状态重置为初始值
     */
    void Initialize()
    {
        _fakeDeath = false;  // 假死状态标志
        _canDie = true;      // 是否可以真正死亡
    }

    /**
     * @brief 重置AI状态
     *
     * 当战斗重置时调用，恢复莫格莱尼到初始状态：
     * - 重置所有标志位
     * - 施放惩戒光环
     * - 移除不可交互和不可攻击标志
     * - 设置站立状态和主动攻击模式
     * - 关闭高检察官的大门
     */
    void Reset() override
    {
        Initialize();

        _Reset();
        _killYellTimer.Reset(0s);

        DoCastSelf(SPELL_RETRIBUTION_AURA, true);  // 施放惩戒光环
        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE);  // 移除不可交互和不可攻击标志
        me->SetStandState(UNIT_STAND_STATE_STAND);  // 设置站立状态
        me->SetReactState(REACT_AGGRESSIVE);        // 设置主动攻击模式

        instance->HandleGameObject(ObjectGuid::Empty, false, instance->GetGameObject(DATA_HIGH_INQUISITORS_DOOR));  // 关闭大门
    }

    /**
     * @brief 进入战斗事件
     * @param who 进入战斗的目标
     *
     * 当莫格莱尼进入战斗时：
     * - 喊出战斗台词
     * - 呼叫附近的盟友支援
     * - 停止默认移动（保持静止）
     * - 安排技能施放事件
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);

        Talk(SAY_MO_AGGRO);  // 喊出战斗台词

        // Call for help (from old script)
        me->CallForHelp(VISIBLE_RANGE);  // 呼叫视野范围内的盟友

        // Just to be sure it's MOTION_SLOT_DEFAULT is static
        me->GetMotionMaster()->MoveIdle();  // 停止移动

        // Schedule events
        events.ScheduleEvent(EVENT_CRUSADER_STRIKE, 10s, 15s);   // 10-15秒后施放十字军打击
        events.ScheduleEvent(EVENT_HAMMER_OF_JUSTICE, 10s, 15s); // 10-15秒后施放制裁之锤
    }

    /**
     * @brief 击杀单位事件
     * @param who 被击杀的单位
     *
     * 当莫格莱尼击杀玩家时喊话，有5秒冷却时间防止频繁喊话
     */
    void KilledUnit(Unit* who) override
    {
        if (who->GetTypeId() != TYPEID_PLAYER)  // 只对玩家喊话
            return;

        if (_killYellTimer.Passed())
        {
            Talk(SAY_MO_KILL);
            _killYellTimer.Reset(5s);  // 5秒冷却
        }
    }

    /**
     * @brief 执行事件处理器
     * @param eventId 事件ID
     *
     * 处理定时触发的技能施放事件
     */
    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_CRUSADER_STRIKE:
                DoCastVictim(SPELL_CRUSADER_STRIKE);  // 对当前目标施放十字军打击
                events.Repeat(10s);                     // 10秒后再次施放
                break;
            case EVENT_HAMMER_OF_JUSTICE:
                DoCastVictim(SPELL_HAMMER_OF_JUSTICE);  // 对当前目标施放制裁之锤
                events.Repeat(60s);                      // 60秒后再次施放
                break;
            default:
                break;
        }
    }

    /**
     * @brief 受到伤害事件
     * @param who 造成伤害的来源
     * @param damage 伤害值（可修改）
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 核心机制：处理莫格莱尼的假死逻辑
     * - 当莫格莱尼受到致命伤害且未处于假死状态时，触发假死
     * - 打开大门让怀特迈恩进入
     * - 设置为不可交互和不可攻击状态
     * - 防止真正死亡直到复活后
     */
    void DamageTaken(Unit* /*who*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (damage >= me->GetHealth() && !_fakeDeath)
        {
            // 触发假死机制
            _fakeDeath = true;
            _canDie = false;

            // Open the door - 打开大门让怀特迈恩进入
            instance->HandleGameObject(ObjectGuid::Empty, true, instance->GetGameObject(DATA_HIGH_INQUISITORS_DOOR));

            // Tell whitemane to move - 通知怀特迈恩移动到战场
            if (Creature* whitemane = instance->GetCreature(DATA_WHITEMANE))
            {
                whitemane->GetMotionMaster()->MovePoint(0, WhitemaneIntroMovePos);
                DoZoneInCombat(whitemane);  // 让怀特迈恩进入战斗
            }

            // 清除所有战斗状态
            me->InterruptNonMeleeSpells(true);      // 打断所有非近战法术
            me->ClearComboPointHolders();           // 清除连击点持有者
            me->RemoveAllAuras();                   // 移除所有光环
            me->ClearAllReactives();                // 清除所有反应性法术
            me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_NON_ATTACKABLE);  // 设置为不可交互和不可攻击
            me->SetStandState(UNIT_STAND_STATE_DEAD);    // 设置死亡站立状态（假死）
            me->SetReactState(REACT_PASSIVE);        // 设置被动反应模式，防止假死时攻击

            // Stop moving - 停止移动
            me->GetMotionMaster()->Clear();
        }

        // 如果不能死亡，将伤害设为0
        if (!_canDie && damage >= me->GetHealth())
            damage = 0;
    }

    /**
     * @brief 被法术击中事件
     * @param caster 施法者
     * @param spellInfo 法术信息
     *
     * 处理被血色复活法术击中后的复活逻辑：
     * - 3秒后喊话并站立
     * - 5秒后重新进入战斗，恢复所有技能
     */
    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        // Casted from Whitemane - 来自怀特迈恩的施法
        if (spellInfo->Id == SPELL_SCARLET_RESURRECTION)
        {
            // 3秒后喊话并站立
            scheduler.Schedule(3s, [this](TaskContext /*context*/)
            {
                // Say text
                Talk(SAY_MO_RESURRECTED);

                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);  // 移除不可交互标志
                me->SetStandState(UNIT_STAND_STATE_STAND);     // 设置站立状态
            });

            // 5秒后重新进入战斗
            scheduler.Schedule(5s, [this](TaskContext /*context*/)
            {
                // Schedule events after ressurrect - 复活后安排技能事件
                events.ScheduleEvent(EVENT_CRUSADER_STRIKE, 10s, 15s);
                events.ScheduleEvent(EVENT_HAMMER_OF_JUSTICE, 10s, 15s);

                // We can now die - 现在可以真正死亡了
                me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);  // 移除不可攻击标志
                me->SetReactState(REACT_AGGRESSIVE);            // 设置主动攻击模式
                _canDie = true;
                DoCastSelf(SPELL_RETRIBUTION_AURA, true);      // 重新施放惩戒光环
            });
        }
    }

    /**
     * @brief 更新AI
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 每帧调用，处理事件和战斗逻辑
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())  // 没有战斗目标则返回
            return;

        events.Update(diff);      // 更新事件计时器
        scheduler.Update(diff);   // 更新调度器

        if (me->HasUnitState(UNIT_STATE_CASTING))  // 如果正在施法，等待
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            ExecuteEvent(eventId);
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();  // 如果可以，进行近战攻击
    }

private:
    TimeTracker _killYellTimer;  // 击杀喊话冷却计时器
    bool _fakeDeath;             // 是否处于假死状态
    bool _canDie;                // 是否可以真正死亡
};

/**
 * @brief 大检察官怀特迈恩AI结构体
 *
 * 实现大检察官怀特迈恩的战斗AI逻辑，包括：
 * - 治疗和辅助技能（治疗术、真言术：盾）
 * - 攻击技能（神圣惩击）
 * - 深度睡眠和复活莫格莱尼的特殊机制
 *
 * 战斗流程：
 * 1. 进入战斗后喊话，5秒后开始施放技能
 * 2. 优先治疗自己和莫格莱尼
 * 3. 血量低于50%时施放深度睡眠，移动到莫格莱尼身边复活他
 * 4. 复活完成后继续战斗直到被击杀
 */
struct boss_high_inquisitor_whitemane : public ScriptedAI
{
public:
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_high_inquisitor_whitemane(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()), _killYellTimer(0s)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 将复活进行中和可死亡状态重置为初始值
     */
    void Initialize()
    {
        _ressurectionInProgress = false;  // 复活是否正在进行中
        _canDie = true;                   // 是否可以真正死亡
    }

    /**
     * @brief 重置AI状态
     *
     * 当战斗重置时调用，恢复怀特迈恩到初始状态：
     * - 重置所有标志位
     * - 施放惩戒光环
     * - 设置主动攻击模式
     */
    void Reset() override
    {
        Initialize();

        _events.Reset();           // 重置事件
        _scheduler.CancelAll();    // 取消所有调度任务
        _killYellTimer.Reset(0s);  // 重置击杀喊话计时器

        DoCastSelf(SPELL_RETRIBUTION_AURA);  // 施放惩戒光环
        me->SetReactState(REACT_AGGRESSIVE);  // 设置主动攻击模式
    }

    /**
     * @brief 进入战斗事件
     * @param who 进入战斗的目标
     *
     * 当怀特迈恩进入战斗时：
     * - 喊出战斗台词
     * - 停止默认移动
     * - 5秒后开始安排技能施放
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        Talk(SAY_WH_INTRO);  // 喊出战斗台词

        // Just to be sure it's MOTION_SLOT_DEFAULT is static
        me->GetMotionMaster()->MoveIdle();  // 停止移动

        // Start events after 5 seconds - 5秒后开始施放技能
        _scheduler.Schedule(5s, [this](TaskContext /*context*/)
        {
            _events.ScheduleEvent(EVENT_HEAL, 10s);             // 治疗术
            _events.ScheduleEvent(EVENT_POWER_WORD_SHIELD, 15s); // 真言术：盾
            _events.ScheduleEvent(EVENT_HOLY_SMITE, 6s);         // 神圣惩击
        });
    }

    /**
     * @brief 击杀单位事件
     * @param who 被击杀的单位
     *
     * 当怀特迈恩击杀玩家时喊话，有5秒冷却时间
     */
    void KilledUnit(Unit* who) override
    {
        if (who->GetTypeId() != TYPEID_PLAYER)
            return;

        if (_killYellTimer.Passed())
        {
            Talk(SAY_WH_KILL);
            _killYellTimer.Reset(5s);
        }
    }

    /**
     * @brief 更新AI
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 每帧调用，处理事件和战斗逻辑
     */
    void UpdateAI(const uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        _events.Update(diff);      // 更新事件计时器
        _scheduler.Update(diff);   // 更新调度器

        if (!_killYellTimer.Passed())
            _killYellTimer.Update(diff);

        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_HEAL:
                {
                    // 智能治疗逻辑：优先治疗血量低于75%的目标
                    Creature* target = nullptr;
                    if (HealthBelowPct(75))
                        target = me;  // 自己血量低于75%，治疗自己
                    else if (Creature* mograine = _instance->GetCreature(DATA_MOGRAINE))
                    {
                        if (mograine->IsAlive() && mograine->HealthBelowPct(75))
                            target = mograine;  // 莫格莱尼血量低于75%，治疗莫格莱尼
                    }

                    if (target)
                        DoCast(target, SPELL_HEAL);

                    _events.Repeat(13s);
                    break;
                }
                case EVENT_POWER_WORD_SHIELD:
                    DoCastSelf(SPELL_POWER_WORD_SHIELD);  // 给自己施放真言术：盾
                    _events.Repeat(15s);
                    break;
                case EVENT_HOLY_SMITE:
                    DoCastVictim(SPELL_HOLY_SMITE);  // 对当前目标施放神圣惩击
                    _events.Repeat(6s);
                    break;
                default:
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        if (me->HasReactState(REACT_AGGRESSIVE))
            DoMeleeAttackIfReady();  // 如果可以，进行近战攻击
    }

    /**
     * @brief 受到伤害事件
     * @param who 造成伤害的来源
     * @param damage 伤害值（可修改）
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 核心机制：处理怀特迈恩的复活莫格莱尼逻辑
     * - 当怀特迈恩血量低于50%且未处于复活流程时，触发复活机制
     * - 取消所有战斗事件
     * - 施放深度睡眠让所有玩家昏睡
     * - 移动到莫格莱尼身边准备复活
     * - 防止真正死亡直到复活完成
     */
    void DamageTaken(Unit* /*who*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // When Whitemane falls below 50% cast Deep sleep and schedule to ressurrect
        // 当怀特迈恩血量低于50%时，施放深度睡眠并准备复活
        if (me->HealthBelowPctDamaged(50, damage) && !_ressurectionInProgress)
        {
            _ressurectionInProgress = true;
            _canDie = false;

            // Cancel all combat events - 取消所有战斗事件
            _events.CancelEvent(EVENT_HEAL);
            _events.CancelEvent(EVENT_POWER_WORD_SHIELD);
            _events.CancelEvent(EVENT_HOLY_SMITE);

            me->InterruptNonMeleeSpells(true);  // 打断所有非近战法术

            // Sleep all players - 让所有玩家昏睡
            DoCastAOE(SPELL_DEEP_SLEEP);

            if (Creature* mograine = _instance->GetCreature(DATA_MOGRAINE))
            {
                me->SetReactState(REACT_PASSIVE);  // 设置被动反应模式
                me->GetMotionMaster()->Clear(MOTION_PRIORITY_NORMAL);  // 清除移动

                Position movePosition = mograine->GetPosition();

                // Get a position within 2 yards of mograine, and facing him
                // 获取距离莫格莱尼2码内的位置，并面向他
                me->MovePosition(movePosition, 2.0f, me->GetRelativeAngle(movePosition));
                me->GetMotionMaster()->MovePoint(POINT_WHITEMANE_MOVE_TO_MOGRAINE, movePosition);
            }
        }

        // 如果不能死亡，将伤害设为0
        if (!_canDie && damage >= me->GetHealth())
            damage = 0;
    }

    /**
     * @brief 移动完成通知事件
     * @param type 移动类型
     * @param id 路点ID
     *
     * 当怀特迈恩移动到莫格莱尼身边后：
     * - 面向莫格莱尼
     * - 3秒后施放血色复活法术
     */
    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE || id != POINT_WHITEMANE_MOVE_TO_MOGRAINE)
            return;

        if (Creature* mograine = _instance->GetCreature(DATA_MOGRAINE))
            me->SetFacingToObject(mograine);  // 面向莫格莱尼

        // After 3 seconds cast scarlet ressurection - 3秒后施放血色复活
        _scheduler.Schedule(3s, [this](TaskContext /*context*/)
        {
            if (Creature* mograine = _instance->GetCreature(DATA_MOGRAINE))
                DoCast(mograine, SPELL_SCARLET_RESURRECTION);
            else
                MograineResurrected();
        });
    }

    /**
     * @brief 法术击中目标事件
     * @param target 被击中的目标
     * @param spellInfo 法术信息
     *
     * 当血色复活法术击中莫格莱尼后，调用复活完成函数
     */
    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        if (target->GetEntry() == NPC_MOGRAINE && spellInfo->Id == SPELL_SCARLET_RESURRECTION)
            MograineResurrected();
    }

private:
    /**
     * @brief 莫格莱尼复活完成处理
     *
     * 当莫格莱尼被成功复活后：
     * - 喊出复活台词
     * - 重新安排所有战斗技能
     * - 恢复可死亡状态
     * - 追击当前目标
     */
    void MograineResurrected()
    {
        Talk(SAY_WH_RESURRECT);

        // Schedule events again - 重新安排技能事件
        _events.ScheduleEvent(EVENT_HEAL, 10s);
        _events.ScheduleEvent(EVENT_POWER_WORD_SHIELD, 15s);
        _events.ScheduleEvent(EVENT_HOLY_SMITE, 6s);

        _canDie = true;  // 现在可以真正死亡了

        me->SetReactState(REACT_AGGRESSIVE);  // 恢复主动攻击模式

        if (me->GetVictim())
            me->GetMotionMaster()->MoveChase(me->GetVictim());  // 追击当前目标
    }

    InstanceScript* _instance;          // 副本脚本实例
    EventMap _events;                   // 事件映射表
    TaskScheduler _scheduler;           // 任务调度器
    TimeTracker _killYellTimer;         // 击杀喊话冷却计时器
    bool _ressurectionInProgress;       // 复活是否正在进行中
    bool _canDie;                       // 是否可以真正死亡
};

/**
 * @brief 注册BOSS脚本
 *
 * 将莫格莱尼和怀特迈恩的AI注册到脚本系统中
 */
void AddSC_boss_mograine_and_whitemane()
{
    RegisterScarletMonasteryCreatureAI(boss_scarlet_commander_mograine);
    RegisterScarletMonasteryCreatureAI(boss_high_inquisitor_whitemane);
}
