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
 * @file boss_thekal.cpp
 * @brief 祖尔格拉布副本Boss - 高阶祭司塞卡尔(High Priest Thekal)及其随从的AI实现
 *
 * 该Boss战是祖尔格拉布中最具特色的战斗之一，具有以下独特机制：
 *
 * 1. 三人组合战斗：
 *    - 高阶祭司塞卡尔（战士型Boss）
 *    - 狂热者洛卡恩（萨满治疗者）
 *    - 狂热者扎斯（盗贼输出者）
 *
 * 2. 假死与复活机制：
 *    - 三个敌人各自独立战斗，被击败时会"假死"
 *    - 如果在10秒内没有全部击杀三个敌人，他们会复活
 *    - 只有在短时间内击杀所有三个敌人才能进入第二阶段
 *
 * 3. 两阶段战斗：
 *    - 第一阶段：巨魔形态，三人同时战斗
 *    - 第二阶段：塞卡尔变身为老虎形态，获得新的技能
 *
 * 4. 老虎召唤：第二阶段会定期召唤老虎援军
 *
 * @note 该战斗要求团队有良好的配合，需要协调击杀时机以防止敌人复活
 */

#include "zulgurub.h"
#include "CellImpl.h"
#include "GridNotifiersImpl.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"

/**
 * @brief 对话和喊话ID枚举
 */
enum Says
{
    TALK_TIGER_PHASE          = 0,  ///< 变身为老虎阶段时的喊话
    TALK_DEATH                = 1,  ///< 死亡时的喊话
    TALK_FAKE_DEATH           = 2,  ///< 假死时的喊话
    TALK_FRENZY               = 3   ///< 狂暴时的喊话
};

/**
 * @brief 法术ID枚举
 *
 * 包含高阶祭司塞卡尔及其两个随从使用的所有法术
 */
enum Spells
{
    SPELL_RESURRECT           = 24173,  ///< 复活法术（待研究具体用途）
    SPELL_RESURRECT_VISUAL    = 24171,  ///< 复活视觉效果

    // High Priest Thekal - 高阶祭司塞卡尔的法术
    // Phase 1 - 第一阶段（巨魔形态）
    SPELL_MORTALCLEAVE        = 22859,  ///< 致死顺劈 - 造成伤害并降低治疗效果
    SPELL_SILENCE             = 22666,  ///< 沉默 - 使目标无法施法
    // Phase 2 - 第二阶段（老虎形态）
    SPELL_TIGER_FORM          = 24169,  ///< 老虎形态 - 变身为老虎
    SPELL_FRENZY              = 8269,   ///< 狂暴 - 提高攻击速度和伤害
    SPELL_FORCEPUNCH          = 24189,  ///< 强力拳击 - 对目标造成伤害
    SPELL_CHARGE              = 24193,  ///< 冲锋 - 冲向目标并造成伤害
    SPELL_SUMMONTIGERS        = 24183,  ///< 召唤老虎 - 召唤援军老虎

    // Zealot Lor'Khan - 狂热者洛卡恩的法术（萨满治疗者）
    SPELL_SHIELD              = 20545,  ///< 护盾 - 减少受到的伤害
    SPELL_BLOODLUST           = 24185,  ///< 嗜血 - 提高攻击速度
    SPELL_GREATERHEAL         = 24208,  ///< 强效治疗 - 治疗目标
    SPELL_DISARM              = 6713,   ///< 缴械 - 使目标无法使用武器

    // Zealot Zath - 狂热者扎斯的法术（盗贼输出者）
    SPELL_SWEEPINGSTRIKES     = 18765,  ///< 剑刃乱舞 - 攻击多个目标
    SPELL_SINISTERSTRIKE      = 15581,  ///< 邪恶攻击 - 造成伤害
    SPELL_GOUGE               = 12540,  ///< 凿击 - 使目标昏迷
    SPELL_KICK                = 15614,  ///< 脚踢 - 打断施法
    SPELL_BLIND               = 21060,  ///< 致盲 - 使目标迷惑

    SPELL_PERMANENT_FEIGN_DEATH = 29266  ///< 永久假死 - 模拟死亡状态
};

/**
 * @brief 塞卡尔的事件ID枚举
 *
 * 用于事件调度系统，控制Boss的行为时序
 */
enum ThekalEvents
{
    EVENT_MORTALCLEAVE = 1,      ///< 致死顺劈事件
    EVENT_SILENCE,               ///< 沉默事件
    EVENT_RESURRECT_TIMER,       ///< 复活计时器事件
    EVENT_CHANGE_PHASE_1,        ///< 变身阶段1事件
    EVENT_CHANGE_PHASE_2,        ///< 变身阶段2事件
    EVENT_CHANGE_PHASE_3,        ///< 变身阶段3事件

    EVENT_FORCEPUNCH,            ///< 强力拳击事件
    EVENT_SPELL_CHARGE,          ///< 冲锋事件
    EVENT_SUMMONTIGERS           ///< 召唤老虎事件
};

/**
 * @brief 战斗阶段枚举
 */
enum Phases
{
    PHASE_ONE                 = 1,  ///< 第一阶段 - 巨魔形态（三人战斗）
    PHASE_TWO                 = 2   ///< 第二阶段 - 老虎形态
};

/**
 * @brief 自定义数据枚举
 *
 * 用于Boss之间的通信，处理复活机制
 */
enum Data
{
    DATA_FAKE_DEATH = 1,   ///< 假死数据 - 标识某个生物进入假死状态
    DATA_RESURRECTED       ///< 已复活数据 - 标识某个生物已经复活
};

/// 伤害增加百分比（变身后）
float const DamageIncrease = 40.0f;
/// 伤害减少百分比（用于在重置时恢复正常伤害）
float const DamageDecrease = 100.f / (1.f + DamageIncrease / 100.f) - 100.f;

/**
 * @brief 高阶祭司塞卡尔Boss AI结构体
 *
 * 实现塞卡尔的核心战斗逻辑，包括：
 * - 两阶段战斗（巨魔形态和老虎形态）
 * - 假死和复活机制的协调
 * - 与两个随从的通信和同步
 *
 * 该AI负责管理整个战斗的流程控制，包括复活计时和阶段转换。
 */
struct boss_thekal : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_thekal(Creature* creature) : BossAI(creature, DATA_THEKAL)
    {
        Initialize();
    }

    /**
     * @brief 初始化所有成员变量
     *
     * 设置所有状态标志的初始值，确保每次战斗开始时状态一致
     */
    void Initialize()
    {
        _enraged = false;                ///< 是否已狂暴
        _isThekalDead = false;           ///< 塞卡尔是否处于假死状态
        _isLorkhanDead = false;          ///< 洛卡恩是否处于假死状态
        _isZathDead = false;             ///< 扎斯是否处于假死状态
        _isResurrectTimerActive = false; ///< 复活计时器是否激活
        _isChangingPhase = false;        ///< 是否正在变换阶段
    }

    /**
     * @brief 进入规避模式
     * @param why 规避原因
     *
     * 当战斗意外结束（如团队团灭）时调用，让Boss返回初始位置
     */
    void EnterEvadeMode(EvadeReason why) override
    {
        if (!_EnterEvadeMode(why))
            return;
        me->AddUnitState(UNIT_STATE_EVADE);
        me->GetMotionMaster()->MoveTargetedHome();
        Reset();
    }

    /**
     * @brief 重置Boss状态
     *
     * 清除所有战斗状态、光环和标志，恢复到初始待战斗状态
     */
    void Reset() override
    {
        // 如果在第二阶段，需要移除伤害加成
        if (events.IsInPhase(PHASE_TWO))
            me->ApplyStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, DamageDecrease);
        me->SetControlled(false, UNIT_STATE_ROOT);
        events.Reset();
        _Reset();
        Initialize();
        me->RemoveAurasDueToSpell(SPELL_PERMANENT_FEIGN_DEATH);
        me->RemoveUnitFlag(UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
    }

    /**
     * @brief Boss死亡处理
     * @param killer 击杀者（未使用）
     *
     * 当Boss被击败时：
     * 1. 通知实例Boss已死亡
     * 2. 播放死亡台词
     * 3. 确保两个随从也真正死亡
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(TALK_DEATH);

        // 随从可能还在假死状态，战斗结束时确保他们也死亡
        if (Creature* creature = instance->GetCreature(DATA_LORKHAN))
            creature->KillSelf();
        if (Creature* creature = instance->GetCreature(DATA_ZATH))
            creature->KillSelf();
        instance->SetBossState(DATA_LORKHAN, DONE);
        instance->SetBossState(DATA_ZATH, DONE);
    }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标
     *
     * 当Boss被玩家攻击时，初始化第一阶段的事件调度
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.SetPhase(PHASE_ONE);
        events.ScheduleEvent(EVENT_MORTALCLEAVE, 4s, 0, PHASE_ONE);
        events.ScheduleEvent(EVENT_SILENCE, 9s, 0, PHASE_ONE);
    }

    /**
     * @brief 设置自定义数据
     * @param type 数据类型
     * @param data 数据值
     *
     * 用于与随从AI通信，处理假死和复活机制：
     * - DATA_FAKE_DEATH: 当某个生物假死时，记录状态
     * - DATA_RESURRECTED: 当某个生物复活时，移除假死状态
     *
     * 关键逻辑：
     * - 如果所有三个生物都假死，进入阶段转换
     * - 如果只有部分假死，启动复活计时器（10秒）
     * - 复活计时器触发后，所有假死的生物复活
     */
    void SetData(uint32 type, uint32 data) override
    {
        if (type == DATA_FAKE_DEATH)
        {
            // 记录哪个生物进入假死状态
            switch (data)
            {
                case NPC_HIGH_PRIEST_THEKAL:
                    _isThekalDead = true;
                    break;
                case NPC_ZEALOT_LORKHAN:
                    _isLorkhanDead = true;
                    break;
                case NPC_ZEALOT_ZATH:
                    _isZathDead = true;
                    break;
                default:
                    return;
            }

            // 检查是否所有三个生物都已假死
            if (_isThekalDead && _isLorkhanDead && _isZathDead)
            {
                // 所有人都假死了，准备进入第二阶段
                _isResurrectTimerActive = false;
                events.Reset();
                events.ScheduleEvent(EVENT_CHANGE_PHASE_1, 3s);
            }
            else
            {
                // 只有部分假死，启动复活计时器
                // 如果计时器未激活，则启动；否则忽略（避免重复计时）
                if (!_isResurrectTimerActive)
                {
                    events.ScheduleEvent(EVENT_RESURRECT_TIMER, 10s);
                    _isResurrectTimerActive = true;
                }
            }
        }
        else if (type == DATA_RESURRECTED)
        {
            // 处理复活逻辑
            Creature* creature = nullptr;
            if (data == NPC_HIGH_PRIEST_THEKAL)
                creature = me;
            else if (data == NPC_ZEALOT_LORKHAN)
                creature = instance->GetCreature(DATA_LORKHAN);
            else if (data == NPC_ZEALOT_ZATH)
                creature = instance->GetCreature(DATA_ZATH);

            // 执行复活：移除假死光环，恢复生命值和可交互状态
            if (creature)
            {
                creature->RemoveAurasDueToSpell(SPELL_PERMANENT_FEIGN_DEATH);
                creature->SetFullHealth();
                creature->SetImmuneToPC(false, true);
                creature->SetImmuneToNPC(false, true);
            }
        }
    }

    /**
     * @brief 受到伤害处理
     * @param attacker 攻击者（未使用）
     * @param damage 伤害值（可能被修改）
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     *
     * 处理两个关键机制：
     * 1. 第一阶段假死：当生命值降为0时，不立即死亡，而是进入假死状态
     * 2. 第二阶段狂暴：当生命值低于10%时，进入狂暴状态
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 第一阶段：生命值降为0时进入假死状态
        if (damage >= me->GetHealth() && events.IsInPhase(PHASE_ONE))
        {
            Talk(TALK_FAKE_DEATH);
            me->RemoveAllAuras();
            me->SetImmuneToPC(true, true);
            me->SetImmuneToNPC(true, true);
            DoCastSelf(SPELL_PERMANENT_FEIGN_DEATH, true);
            events.DelayEvents(10s);
            SetData(DATA_FAKE_DEATH, me->GetEntry());
            damage = 0;  // 将伤害设为0，避免真正死亡
        }
        // 第二阶段：生命值低于10%时进入狂暴
        else if (events.IsInPhase(PHASE_TWO) && !_enraged && me->HealthBelowPct(10))
        {
            DoCastSelf(SPELL_FRENZY);
            Talk(TALK_FRENZY);
            _enraged = true;
        }
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 处理所有事件的调度和执行，包括：
     * - 第一阶段的战斗技能
     * - 复活计时器
     * - 阶段转换序列
     * - 第二阶段的老虎技能
     */
    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        // 如果正在施法，等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_MORTALCLEAVE:
                    // 对当前目标施放致死顺劈
                    DoCastVictim(SPELL_MORTALCLEAVE);
                    events.ScheduleEvent(EVENT_MORTALCLEAVE, 15s, 20s, 0, PHASE_ONE);
                    break;
                case EVENT_SILENCE:
                    // 对当前目标施放沉默
                    DoCastVictim(SPELL_SILENCE);
                    events.ScheduleEvent(EVENT_SILENCE, 20s, 25s, 0, PHASE_ONE);
                    break;
                case EVENT_RESURRECT_TIMER:
                {
                    // 复活计时器到期，复活所有假死的生物
                    // 这发生在团队未能及时击杀所有三个目标时
                    _isResurrectTimerActive = false;

                    // 复活塞卡尔
                    if (_isThekalDead)
                    {
                        DoCastSelf(SPELL_RESURRECT_VISUAL);
                        SetData(DATA_RESURRECTED, me->GetEntry());
                        _isThekalDead = false;
                    }

                    // 复活洛卡恩
                    if (_isLorkhanDead)
                    {
                        if (Creature* lorkhan = instance->GetCreature(DATA_LORKHAN))
                        {
                            lorkhan->AI()->DoCastSelf(SPELL_RESURRECT_VISUAL);
                            SetData(DATA_RESURRECTED, lorkhan->GetEntry());
                        }
                        _isLorkhanDead = false;
                    }

                    // 复活扎斯
                    if (_isZathDead)
                    {
                        if (Creature* zath = instance->GetCreature(DATA_ZATH))
                        {
                            zath->AI()->DoCastSelf(SPELL_RESURRECT_VISUAL);
                            SetData(DATA_RESURRECTED, zath->GetEntry());
                        }
                        _isZathDead = false;
                    }
                    break;
                }
                case EVENT_CHANGE_PHASE_1:
                    // 阶段转换步骤1：准备变身
                    _isChangingPhase = true;
                    me->SetControlled(true, UNIT_STATE_ROOT);  // 定身
                    me->RemoveAurasDueToSpell(SPELL_PERMANENT_FEIGN_DEATH);
                    me->SetFullHealth();
                    DoCastSelf(SPELL_RESURRECT_VISUAL);
                    events.ScheduleEvent(EVENT_CHANGE_PHASE_2, 1s);
                    break;
                case EVENT_CHANGE_PHASE_2:
                    // 阶段转换步骤2：喊话
                    Talk(TALK_TIGER_PHASE);
                    events.ScheduleEvent(EVENT_CHANGE_PHASE_3, 1s);
                    break;
                case EVENT_CHANGE_PHASE_3:
                {
                    // 阶段转换步骤3：完成变身
                    _isChangingPhase = false;
                    DoCastSelf(SPELL_TIGER_FORM);
                    me->RemoveUnitFlag(UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
                    // 提高伤害40%
                    me->ApplyStatPctModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, DamageIncrease);
                    ResetThreatList();
                    me->SetControlled(false, UNIT_STATE_ROOT);  // 解除定身
                    // 安排第二阶段的技能
                    events.ScheduleEvent(EVENT_FORCEPUNCH, 4s, 0, PHASE_TWO);
                    events.ScheduleEvent(EVENT_SPELL_CHARGE, 12s, 0, PHASE_TWO);
                    events.ScheduleEvent(EVENT_SUMMONTIGERS, 25s, 0, PHASE_TWO);
                    events.SetPhase(PHASE_TWO);
                    break;
                }
                case EVENT_FORCEPUNCH:
                    // 强力拳击：对当前目标造成伤害
                    DoCastVictim(SPELL_FORCEPUNCH, true);
                    events.ScheduleEvent(EVENT_FORCEPUNCH, 16s, 21s, 0, PHASE_TWO);
                    break;
                case EVENT_CHARGE:
                    // 冲锋：随机选择目标并冲锋
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 0.f, true))
                    {
                        ResetThreatList();
                        AttackStart(target);
                        DoCast(target, SPELL_CHARGE);
                    }
                    events.ScheduleEvent(EVENT_CHARGE, 15s, 22s, 0, PHASE_TWO);
                    break;
                case EVENT_SUMMONTIGERS:
                    // 召唤老虎援军
                    DoCastVictim(SPELL_SUMMONTIGERS, true);
                    events.ScheduleEvent(EVENT_SUMMONTIGERS, 10s, 14s, 0, PHASE_TWO);
                    break;
                default:
                    break;
            }
        }

        // 如果正在变换阶段，不执行常规攻击
        if (_isChangingPhase)
            return;

        // 如果没有战斗目标，返回
        if (!UpdateVictim())
            return;

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    bool _enraged;                ///< 是否已进入狂暴状态
    bool _isThekalDead;           ///< 塞卡尔是否处于假死状态
    bool _isLorkhanDead;          ///< 洛卡恩是否处于假死状态
    bool _isZathDead;             ///< 扎斯是否处于假死状态
    bool _isResurrectTimerActive; ///< 复活计时器是否正在运行
    bool _isChangingPhase;        ///< 是否正在进行阶段转换
};

/**
 * @brief 洛卡恩的事件ID枚举
 */
enum LorkhanEvents
{
    EVENT_SHIELD = 1,        ///< 护盾事件
    EVENT_BLOODLUST,         ///< 嗜血事件
    EVENT_GREATER_HEAL,      ///< 强效治疗事件
    EVENT_DISARM             ///< 缴械事件
};

/**
 * @brief 洛卡恩治疗目标选择器
 *
 * 用于智能选择需要治疗的生物，优先治疗损失生命值最多的目标。
 * 可选择的目标包括：塞卡尔、洛卡恩自己、扎斯。
 *
 * 选择标准：
 * 1. 必须是活着的生物
 * 2. 必须在战斗中
 * 3. 必须是三个Boss之一
 * 4. 不能处于假死状态
 * 5. 优先选择损失生命值最多的目标
 */
class LorKhanSelectTargetToHeal
{
    public:
        /**
         * @brief 构造函数
         * @param reference 参考单位（通常是洛卡恩自己）
         * @param range 搜索范围
         */
        LorKhanSelectTargetToHeal(Unit const* reference, float range) : _reference(reference), _range(range), _hp(0) { }

        /**
         * @brief 判断一个单位是否符合治疗条件
         * @param object 待检查的单位
         * @return 如果该单位应该被治疗则返回true
         */
        bool operator()(Unit* object)
        {
            // 必须是生物，活着，且在战斗中
            if (object->GetTypeId() != TYPEID_UNIT || !object->IsAlive() || !object->IsInCombat())
                return false;

            // 必须是三个Boss之一
            if (object->ToCreature()->GetEntry() != NPC_HIGH_PRIEST_THEKAL && object->GetEntry() != NPC_ZEALOT_LORKHAN && object->GetEntry() != NPC_ZEALOT_ZATH)
                return false;

            // 不能是处于假死等待复活状态的目标
            if (object->HasAura(SPELL_PERMANENT_FEIGN_DEATH))
                return false;

            // 选择损失生命值最多且在范围内的目标
            if ((object->GetMaxHealth() - object->GetHealth() > _hp) && _reference->IsWithinDistInMap(object, _range))
            {
                _hp = object->GetMaxHealth() - object->GetHealth();
                return true;
            }

            return false;
        }

    private:
        Unit const* _reference;  ///< 参考单位
        float const _range;      ///< 搜索范围
        uint32 _hp;              ///< 记录的最大损失生命值
};

/**
 * @brief 狂热者洛卡恩AI结构体
 *
 * 洛卡恩是萨满治疗者，负责为团队提供治疗和增益。
 * 他的技能包括：
 * - 护盾：减少受到的伤害
 * - 嗜血：提高攻击速度
 * - 强效治疗：治疗受伤的队友
 * - 缴械：使目标无法使用武器
 *
 * 当生命值降为0时，他会进入假死状态而非真正死亡，
 * 等待塞卡尔的复活机制处理。
 */
struct npc_zealot_lorkhan : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_zealot_lorkhan(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()) { }

    /**
     * @brief 重置状态
     *
     * 清除所有事件、光环和标志，恢复到初始状态
     */
    void Reset() override
    {
        _events.Reset();
        me->RemoveAurasDueToSpell(SPELL_PERMANENT_FEIGN_DEATH);
        me->RemoveUnitFlag(UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_IMMUNE_TO_PC);
    }

    /**
     * @brief 受到伤害处理
     * @param attacker 攻击者（未使用）
     * @param damage 伤害值（可能被修改）
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     *
     * 当生命值降为0时进入假死状态，而非真正死亡。
     * 通知塞卡尔自己已假死。
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (damage >= me->GetHealth())
        {
            Talk(TALK_FAKE_DEATH);
            me->RemoveAllAuras();
            me->SetImmuneToPC(true, true);
            me->SetImmuneToNPC(true, true);
            DoCastSelf(SPELL_PERMANENT_FEIGN_DEATH, true);
            me->AttackStop();
            // 通知塞卡尔自己已假死
            if (Creature* thekal = _instance->GetCreature(DATA_THEKAL))
                thekal->AI()->SetData(DATA_FAKE_DEATH, me->GetEntry());
            _events.DelayEvents(10s);
            damage = 0;  // 将伤害设为0，避免真正死亡
        }
    }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标（未使用）
     *
     * 安排所有技能的初始施放时间
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        _events.ScheduleEvent(EVENT_SHIELD, 1s);
        _events.ScheduleEvent(EVENT_BLOODLUST, 16s);
        _events.ScheduleEvent(EVENT_GREATER_HEAL, 32s);
        _events.ScheduleEvent(EVENT_DISARM, 6s);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 处理所有技能的施放：
     * - 护盾：每61秒对自己施放
     * - 嗜血：每20-28秒对自己施放
     * - 强效治疗：每15-20秒治疗最需要的目标
     * - 缴械：每15-25秒对当前目标施放
     */
    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        // 如果正在施法，等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理事件
        if (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SHIELD:
                    // 对自己施放护盾
                    DoCastSelf(SPELL_SHIELD);
                    _events.ScheduleEvent(EVENT_SHIELD, 61s);
                    break;
                case EVENT_BLOODLUST:
                    // 对自己施放嗜血
                    DoCastSelf(SPELL_BLOODLUST);
                    _events.ScheduleEvent(EVENT_BLOODLUST, 20s, 28s);
                    break;
                case EVENT_GREATER_HEAL:
                {
                    // 智能选择治疗目标
                    Unit* target = nullptr;
                    LorKhanSelectTargetToHeal check(me, 100.0f);
                    Trinity::UnitLastSearcher<LorKhanSelectTargetToHeal> searcher(me, target, check);
                    Cell::VisitAllObjects(me, searcher, 100.0f);

                    if (target)
                        DoCast(target, SPELL_GREATERHEAL);

                    _events.ScheduleEvent(EVENT_GREATER_HEAL, 15s, 20s);
                    break;
                }
                case EVENT_DISARM:
                    // 对当前目标施放缴械
                    DoCastVictim(SPELL_DISARM);
                    _events.ScheduleEvent(EVENT_DISARM, 15s, 25s);
                    break;
                default:
                    break;
            }
        }

        // 如果没有战斗目标，返回
        if (!UpdateVictim())
            return;

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;           ///< 事件调度器
    InstanceScript* _instance;  ///< 实例脚本指针
};

/**
 * @brief 扎斯的事件ID枚举
 */
enum ZathEvents
{
    EVENT_SWEEPING_STRIKES = 1,  ///< 剑刃乱舞事件
    EVENT_SINISTER_STRIKE,       ///< 邪恶攻击事件
    EVENT_GOUGE,                 ///< 凿击事件
    EVENT_KICK,                  ///< 脚踢事件
    EVENT_BLIND,                 ///< 致盲事件
};

/**
 * @brief 狂热者扎斯AI结构体
 *
 * 扎斯是盗贼输出者，专注于对目标造成伤害和控制。
 * 他的技能包括：
 * - 剑刃乱舞：对多个目标造成伤害
 * - 邪恶攻击：基础伤害技能
 * - 凿击：使目标昏迷并清除威胁值
 * - 脚踢：打断施法
 * - 致盲：使目标迷惑
 *
 * 当生命值降为0时，他会进入假死状态而非真正死亡，
 * 等待塞卡尔的复活机制处理。
 */
struct npc_zealot_zath : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_zealot_zath(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript()) { }

    /**
     * @brief 重置状态
     *
     * 清除所有事件、光环和标志，恢复到初始状态
     */
    void Reset() override
    {
        _events.Reset();
        me->RemoveAurasDueToSpell(SPELL_PERMANENT_FEIGN_DEATH);
        me->RemoveUnitFlag(UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_IMMUNE_TO_NPC);
    }

    /**
     * @brief 受到伤害处理
     * @param attacker 攻击者（未使用）
     * @param damage 伤害值（可能被修改）
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     *
     * 当生命值降为0时进入假死状态，而非真正死亡。
     * 通知塞卡尔自己已假死。
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (damage >= me->GetHealth())
        {
            Talk(TALK_FAKE_DEATH);
            me->RemoveAllAuras();
            me->SetImmuneToPC(true, true);
            me->SetImmuneToNPC(true, true);
            DoCastSelf(SPELL_PERMANENT_FEIGN_DEATH, true);
            me->AttackStop();
            // 通知塞卡尔自己已假死
            if (Creature* thekal = _instance->GetCreature(DATA_THEKAL))
                thekal->AI()->SetData(DATA_FAKE_DEATH, me->GetEntry());
            _events.DelayEvents(10s);
            damage = 0;  // 将伤害设为0，避免真正死亡
        }
    }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标（未使用）
     *
     * 安排所有技能的初始施放时间
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        _events.ScheduleEvent(EVENT_SWEEPING_STRIKES, 13s);
        _events.ScheduleEvent(EVENT_SINISTER_STRIKE, 8s);
        _events.ScheduleEvent(EVENT_GOUGE, 25s);
        _events.ScheduleEvent(EVENT_KICK, 18s);
        _events.ScheduleEvent(EVENT_BLIND, 5s);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 处理所有技能的施放：
     * - 剑刃乱舞：每22-26秒对当前目标施放
     * - 邪恶攻击：每8-16秒对当前目标施放
     * - 凿击：每17-27秒对当前目标施放，并清除目标威胁值
     * - 脚踢：每15-25秒对当前目标施放
     * - 致盲：每10-20秒对当前目标施放
     */
    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        // 处理事件
        if (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SWEEPING_STRIKES:
                    // 对当前目标施放剑刃乱舞
                    DoCastVictim(SPELL_SWEEPINGSTRIKES);
                    _events.ScheduleEvent(EVENT_SWEEPING_STRIKES, 22s, 26s);
                    break;
                case EVENT_SINISTER_STRIKE:
                    // 对当前目标施放邪恶攻击
                    DoCastVictim(SPELL_SINISTERSTRIKE);
                    _events.ScheduleEvent(EVENT_SINISTER_STRIKE, 8s, 16s);
                    break;
                case EVENT_GOUGE:
                    // 凿击：造成伤害并清除目标的威胁值
                    DoCastVictim(SPELL_GOUGE);
                    if (GetThreat(me->GetVictim()))
                        ModifyThreatByPercent(me->GetVictim(), -100);
                    _events.ScheduleEvent(EVENT_GOUGE, 17s, 27s);
                    break;
                case EVENT_KICK:
                    // 对当前目标施放脚踢
                    DoCastVictim(SPELL_KICK);
                    _events.ScheduleEvent(EVENT_KICK, 15s, 25s);
                    break;
                case EVENT_BLIND:
                    // 对当前目标施放致盲
                    DoCastVictim(SPELL_BLIND);
                    _events.ScheduleEvent(EVENT_BLIND, 10s, 20s);
                    break;
                default:
                    break;
            }
        }

        // 如果没有战斗目标，返回
        if (!UpdateVictim())
            return;

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    EventMap _events;           ///< 事件调度器
    InstanceScript* _instance;  ///< 实例脚本指针
};

/**
 * @brief 注册塞卡尔及其随从的Boss脚本
 *
 * 将三个生物的AI注册到脚本系统中：
 * - 高阶祭司塞卡尔（主Boss）
 * - 狂热者洛卡恩（治疗者）
 * - 狂热者扎斯（输出者）
 */
void AddSC_boss_thekal()
{
    RegisterZulGurubCreatureAI(boss_thekal);
    RegisterZulGurubCreatureAI(npc_zealot_lorkhan);
    RegisterZulGurubCreatureAI(npc_zealot_zath);
}
