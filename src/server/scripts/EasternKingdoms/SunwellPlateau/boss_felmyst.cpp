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
 * @file boss_felmyst.cpp
 * @brief 太阳井高地 - 菲米司（Felmyst）BOSS脚本
 *
 * 本模块实现了太阳井高地副本中的菲米司BOSS战斗逻辑。
 * 菲米司是一条由玛德里戈萨转化而成的邪能巨龙。
 *
 * 战斗机制特点：
 * 1. 地面阶段：近战攻击，施放腐蚀、气体新星、团雾等技能
 * 2. 飞行阶段：飞行绕场，召唤毒气追踪玩家，施放迷雾之息
 * 3. 召唤物：毒气（Vapor）会追踪玩家并留下轨迹，轨迹会变成不死者
 * 4. 狂暴机制：10分钟后进入狂暴状态
 *
 * 阶段循环：地面阶段（约1分钟）-> 飞行阶段（约1分钟）-> 地面阶段...
 *
 * @see https://wowpedia.fandom.com/wiki/Felmyst
 */

/* ScriptData
SDName: Boss_Felmyst
SD%Complete: 0
SDComment:
EndScriptData */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "sunwell_plateau.h"
#include "TemporarySummon.h"

/**
 * @enum Yells
 * @brief BOSS台词枚举
 *
 * 定义了菲米司在战斗中使用的各种台词
 */
enum Yells
{
    YELL_BIRTH                                    = 0,  ///< 出生台词（从玛德里戈萨转化）
    YELL_KILL                                     = 1,  ///< 击杀玩家台词
    YELL_BREATH                                   = 2,  ///< 喷吐台词
    YELL_TAKEOFF                                  = 3,  ///< 起飞台词
    YELL_BERSERK                                  = 4,  ///< 狂暴台词
    YELL_DEATH                                    = 5,  ///< 死亡台词
  //YELL_KALECGOS                                 = 6,  ///< 卡雷苟斯台词（未使用，菲米司死亡后由召唤的卡雷苟斯说出）
};

/**
 * @enum Spells
 * @brief 技能ID枚举
 *
 * 定义了菲米司战斗中使用的所有技能ID
 */
enum Spells
{
    // 光环效果
    AURA_SUNWELL_RADIANCE                         = 45769, ///< 太阳井光辉 - 降低玩家命中和躲避
    AURA_NOXIOUS_FUMES                            = 47002, ///< 有毒烟雾光环 - 持续伤害

    // 地面阶段技能
    SPELL_CLEAVE                                  = 19983, ///< 顺劈斩 - 对前方敌人造成伤害
    SPELL_CORROSION                               = 45866, ///< 腐蚀 - 降低护甲并造成伤害
    SPELL_GAS_NOVA                                = 45855, ///< 气体新星 - AOE自然伤害
    SPELL_ENCAPSULATE_CHANNEL                     = 45661, ///< 团雾通道 - 对目标施放团雾
    // SPELL_ENCAPSULATE_EFFECT                      = 45665,
    // SPELL_ENCAPSULATE_AOE                         = 45662,

    // 飞行阶段技能
    SPELL_VAPOR_SELECT                            = 45391,   ///< 选择毒气目标 - 菲米司对玩家施放，强制施放45392，50000码选择目标
    SPELL_VAPOR_SUMMON                            = 45392,   ///< 召唤毒气 - 玩家召唤毒气，半径5码
    SPELL_VAPOR_FORCE                             = 45388,   ///< 强制毒气 - 毒气对菲米司施放，强制施放45389
    SPELL_VAPOR_CHANNEL                           = 45389,   ///< 毒气通道 - 菲米司对毒气，绿色光束通道
    SPELL_VAPOR_TRIGGER                           = 45411,   ///< 毒气触发 - 链接到45389，毒气对自身，触发45410和46931
    SPELL_VAPOR_DAMAGE                            = 46931,   ///< 毒气伤害 - 4000点伤害
    SPELL_TRAIL_SUMMON                            = 45410,   ///< 召唤轨迹 - 毒气召唤轨迹
    SPELL_TRAIL_TRIGGER                           = 45399,   ///< 轨迹触发 - 轨迹对自身，触发45402
    SPELL_TRAIL_DAMAGE                            = 45402,   ///< 轨迹伤害 - 2000点伤害 + 2000点持续伤害
    SPELL_DEAD_SUMMON                             = 45400,   ///< 召唤不死者 - 召唤烈焰不死者，持续5分钟
    SPELL_DEAD_PASSIVE                            = 45415,   ///< 不死者被动效果
    SPELL_FOG_BREATH                              = 45495,   ///< 迷雾之息 - 菲米司对自身，加速爆发
    SPELL_FOG_TRIGGER                             = 45582,   ///< 迷雾触发 - 迷雾对自身，触发45782
    SPELL_FOG_FORCE                               = 45782,   ///< 强制迷雾 - 迷雾对玩家，强制施放45714
    SPELL_FOG_INFORM                              = 45714,   ///< 迷雾通知 - 玩家让菲米司施放45717，脚本效果
    SPELL_FOG_CHARM                               = 45717,   ///< 迷雾魅惑 - 菲米司对玩家，精神控制
    SPELL_FOG_CHARM2                              = 45726,   ///< 迷雾魅惑2 - 链接到45717

    // 转化技能
    SPELL_TRANSFORM_TRIGGER                       = 44885,   ///< 转化触发 - 玛德里戈萨对自身，触发46350
    SPELL_TRANSFORM_VISUAL                        = 46350,   ///< 转化视觉效果 - 46411晕眩？
    SPELL_TRANSFORM_FELMYST                       = 45068,   ///< 转化菲米司 - 变成邪能龙
    SPELL_FELMYST_SUMMON                          = 45069,   ///< 召唤菲米司

    // 其他技能
    SPELL_BERSERK                                 = 45078,  ///< 狂暴 - 提高伤害
    SPELL_CLOUD_VISUAL                            = 45212,  ///< 云雾视觉效果
    SPELL_CLOUD_SUMMON                            = 45884   ///< 召唤云雾
};

/**
 * @enum PhaseFelmyst
 * @brief 菲米司战斗阶段枚举
 */
enum PhaseFelmyst
{
    PHASE_NONE,    ///< 无阶段（初始状态）
    PHASE_GROUND,  ///< 地面阶段 - 近战攻击
    PHASE_FLIGHT   ///< 飞行阶段 - 飞行绕场
};

/**
 * @enum EventFelmyst
 * @brief 菲米司事件枚举
 *
 * 定义了战斗中使用的各种事件ID
 */
enum EventFelmyst
{
    EVENT_NONE,           ///< 无事件
    EVENT_BERSERK,        ///< 狂暴事件

    EVENT_CLEAVE,         ///< 顺劈斩事件
    EVENT_CORROSION,      ///< 腐蚀事件
    EVENT_GAS_NOVA,       ///< 气体新星事件
    EVENT_ENCAPSULATE,    ///< 团雾事件
    EVENT_FLIGHT,         ///< 飞行阶段开始事件

    EVENT_FLIGHT_SEQUENCE, ///< 飞行序列事件
    EVENT_SUMMON_DEAD,     ///< 召唤不死者事件
    EVENT_SUMMON_FOG       ///< 召唤迷雾事件
};

/**
 * @struct boss_felmyst
 * @brief 菲米司BOSS AI结构体
 *
 * 继承自BossAI，实现了完整的战斗逻辑，包括地面和飞行两个阶段
 *
 * 战斗流程：
 * 1. 地面阶段：持续约1分钟，施放地面技能
 * 2. 飞行阶段：持续约1分钟，召唤毒气和迷雾之息
 * 3. 循环直到BOSS死亡或狂暴
 */
struct boss_felmyst : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_felmyst(Creature* creature) : BossAI(creature, DATA_FELMYST)
    {
        Initialize();
        uiBreathCount = 0;  ///< 喷吐计数器，记录当前喷吐次数
        breathX = 0.f;      ///< 喷吐目标X坐标
        breathY = 0.f;      ///< 喷吐目标Y坐标
    }

    /**
     * @brief 初始化成员变量
     */
    void Initialize()
    {
        phase = PHASE_NONE;      ///< 当前阶段
        uiFlightCount = 0;       ///< 飞行阶段序列计数器
    }

    PhaseFelmyst phase;          ///< 当前战斗阶段

    uint32 uiFlightCount;        ///< 飞行序列计数器，用于控制飞行阶段的各种动作
    uint32 uiBreathCount;        ///< 喷吐计数器，记录在飞行阶段已施放的迷雾之息次数

    float breathX, breathY;      ///< 迷雾之息的目标坐标，用于计算喷吐方向

    /**
     * @brief 初始化AI
     *
     * 在BOSS生成时调用，处理介绍序列
     * 如果BOSS状态为SPECIAL，说明是从玛德里戈萨转化而来
     */
    void InitializeAI() override
    {
        // 用于介绍序列：从玛德里戈萨转化
        if (instance->GetBossState(DATA_FELMYST) == SPECIAL)
            if (Creature* madrigosa = instance->GetCreature(DATA_MADRIGOSA))
                me->Relocate(madrigosa);  // 移动到玛德里戈萨的位置

        me->SetDisplayId(me->GetCreatureTemplate()->Modelid1);
        me->SetNativeDisplayId(me->GetCreatureTemplate()->Modelid1);
    }

    /**
     * @brief 重置函数
     *
     * 重置BOSS状态和属性
     */
    void Reset() override
    {
        Initialize();

        BossAI::Reset();

        me->SetDisableGravity(true);    // 启用重力禁用（飞行生物）
        me->SetBoundingRadius(10);       // 设置碰撞半径
        me->SetCombatReach(10);          // 设置战斗范围
    }

    /**
     * @brief 进入战斗函数
     * @param who 进入战斗的目标
     *
     * 初始化战斗，施加光环，进入地面阶段
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);

        events.ScheduleEvent(EVENT_BERSERK, 10min);  // 10分钟后狂暴

        DoCast(me, AURA_SUNWELL_RADIANCE, true);     // 施加太阳井光辉
        DoCast(me, AURA_NOXIOUS_FUMES, true);        // 施加有毒烟雾
        EnterPhase(PHASE_GROUND);                     // 进入地面阶段
    }

    /**
     * @brief 攻击开始函数
     * @param who 攻击目标
     *
     * 在飞行阶段不执行攻击
     */
    void AttackStart(Unit* who) override
    {
        if (phase != PHASE_FLIGHT)
            BossAI::AttackStart(who);
    }

    /**
     * @brief 视线内移动检测函数
     * @param who 进入视线的单位
     *
     * 在飞行阶段不检测视线
     */
    void MoveInLineOfSight(Unit* who) override
    {
        if (phase != PHASE_FLIGHT)
            BossAI::MoveInLineOfSight(who);
    }

    /**
     * @brief 击杀单位函数
     * @param victim 被击杀的单位
     *
     * 击杀玩家时喊话
     */
    void KilledUnit(Unit* /*victim*/) override
    {
        Talk(YELL_KILL);
    }

    /**
     * @brief 出现函数
     *
     * BOSS出现时喊话（从玛德里戈萨转化后）
     */
    void JustAppeared() override
    {
        Talk(YELL_BIRTH);
    }

    /**
     * @brief 死亡函数
     * @param killer 击杀者
     *
     * BOSS死亡时喊话并处理死亡逻辑
     */
    void JustDied(Unit* killer) override
    {
        Talk(YELL_DEATH);

        BossAI::JustDied(killer);
    }

    /**
     * @brief 进入逃避模式
     * @param why 逃避原因
     *
     * 重置BOSS并在短暂延迟后消失
     */
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        Reset();
        _DespawnAtEvade();
    }

    /**
     * @brief 法术命中回调函数
     * @param caster 施法者
     * @param spellInfo 法术信息
     *
     * 处理特殊法术效果，特别是迷雾魅惑机制
     */
    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        Unit* unitCaster = caster->ToUnit();
        if (!unitCaster)
            return;

        // 迷雾魅惑机制的变通处理
        // 当玩家被迷雾击中时，创建一个被魅惑的不死者
        if (spellInfo->Id == SPELL_FOG_INFORM)
        {
            float x, y, z;
            unitCaster->GetPosition(x, y, z);
            // 在玩家位置召唤不死者
            if (Unit* summon = me->SummonCreature(NPC_DEAD, x, y, z, 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5s))
            {
                summon->SetMaxHealth(unitCaster->GetMaxHealth());
                summon->SetHealth(unitCaster->GetMaxHealth());
                summon->CastSpell(summon, SPELL_FOG_CHARM, true);   // 施加魅惑效果
                summon->CastSpell(summon, SPELL_FOG_CHARM2, true);  // 链接魅惑效果
            }
            // 杀死玩家
            Unit::DealDamage(me, unitCaster, unitCaster->GetHealth(), nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
        }
    }

    /**
     * @brief 召唤物生成回调函数
     * @param summon 被召唤的生物
     *
     * 处理召唤物的初始化，特别是不死者
     */
    void JustSummoned(Creature* summon) override
    {
        if (summon->GetEntry() == NPC_DEAD)
        {
            // 不死者攻击随机目标并进入战斗
            summon->AI()->AttackStart(SelectTarget(SelectTargetMethod::Random));
            DoZoneInCombat(summon);
            summon->CastSpell(summon, SPELL_DEAD_PASSIVE, true);  // 施加被动效果
        }

        BossAI::JustSummoned(summon);
    }

    /**
     * @brief 移动完成回调函数
     * @param type 移动类型
     * @param id 移动点ID
     *
     * 在飞行阶段，移动完成后继续执行飞行序列
     */
    void MovementInform(uint32, uint32) override
    {
        if (phase == PHASE_FLIGHT)
            events.ScheduleEvent(EVENT_FLIGHT_SEQUENCE, 1ms);
    }

    /**
     * @brief 受伤回调函数
     * @param damage 伤害值
     *
     * 在飞行阶段防止BOSS死亡（只能在地面阶段死亡）
     */
    void DamageTaken(Unit*, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (phase != PHASE_GROUND && damage >= me->GetHealth())
            damage = 0;  // 防止在飞行阶段死亡
    }

    /**
     * @brief 进入指定阶段
     * @param NextPhase 下一阶段
     *
     * 处理阶段转换逻辑：
     * - 地面阶段：安排地面技能事件
     * - 飞行阶段：安排飞行序列事件
     */
    void EnterPhase(PhaseFelmyst NextPhase)
    {
        switch (NextPhase)
        {
            case PHASE_GROUND:
                // 停止迷雾之息并落地
                me->CastStop(SPELL_FOG_BREATH);
                me->RemoveAurasDueToSpell(SPELL_FOG_BREATH);
                me->StopMoving();
                me->SetSpeedRate(MOVE_RUN, 2.0f);

                // 安排地面阶段技能
                events.ScheduleEvent(EVENT_CLEAVE, 5s, 10s);       // 顺劈斩
                events.ScheduleEvent(EVENT_CORROSION, 10s, 20s);   // 腐蚀
                events.ScheduleEvent(EVENT_GAS_NOVA, 15s, 20s);    // 气体新星
                events.ScheduleEvent(EVENT_ENCAPSULATE, 20s, 25s); // 团雾
                events.ScheduleEvent(EVENT_FLIGHT, 1min);          // 1分钟后进入飞行阶段
                break;
            case PHASE_FLIGHT:
                me->SetDisableGravity(true);
                events.ScheduleEvent(EVENT_FLIGHT_SEQUENCE, 1s);   // 开始飞行序列
                uiFlightCount = 0;  // 重置飞行计数器
                uiBreathCount = 0;  // 重置喷吐计数器
                break;
            default:
                break;
        }
        phase = NextPhase;
    }

/**
     * @brief 处理飞行序列
     *
     * 实现飞行阶段的详细流程：
     * 0. 起飞动画和喊话
     * 1. 上升到空中
     * 2. 召唤第一个毒气
     * 3. 召唤第二个毒气
     * 4. 清理毒气轨迹
     * 5-8. 迷雾之息（重复3次）
     * 9. 移动到地面目标
     * 10. 降落并进入地面阶段
     *
     * 每个步骤通过uiFlightCount控制，由EVENT_FLIGHT_SEQUENCE触发
     */
    void HandleFlightSequence()
    {
        switch (uiFlightCount)
        {
            case 0:  // 起飞
                //me->AttackStop();
                me->GetMotionMaster()->Clear();
                me->HandleEmoteCommand(EMOTE_ONESHOT_LIFTOFF);  // 起飞动画
                me->StopMoving();
                Talk(YELL_TAKEOFF);  // 起飞喊话
                events.ScheduleEvent(EVENT_FLIGHT_SEQUENCE, 2s);
                break;
            case 1:  // 上升到空中
                me->GetMotionMaster()->MovePoint(0, me->GetPositionX()+1, me->GetPositionY(), me->GetPositionZ()+10);
                break;
            case 2:  // 召唤第一个毒气
            {
                Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 150, true);
                if (!target)
                    target = ObjectAccessor::GetUnit(*me, instance->GetGuidData(DATA_PLAYER_GUID));

                if (!target)
                {
                    EnterEvadeMode(EVADE_REASON_NO_HOSTILES);
                    return;
                }

                // 在目标附近随机位置召唤毒气
                if (Creature* Vapor = me->SummonCreature(NPC_VAPOR, target->GetPositionX() - 5 + rand32() % 10, target->GetPositionY() - 5 + rand32() % 10, target->GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN, 9s))
                {
                    Vapor->AI()->AttackStart(target);
                    me->InterruptNonMeleeSpells(false);
                    DoCast(Vapor, SPELL_VAPOR_CHANNEL, false); // 核心bug修复
                    Vapor->CastSpell(Vapor, SPELL_VAPOR_TRIGGER, true);
                }

                events.ScheduleEvent(EVENT_FLIGHT_SEQUENCE, 10s);
                break;
            }
            case 3:  // 召唤第二个毒气
            {
                DespawnSummons(NPC_VAPOR_TRAIL);  // 清理之前的毒气轨迹
                //DoCast(me, SPELL_VAPOR_SELECT); 需要核心支持

                Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 150, true);
                if (!target)
                    target = ObjectAccessor::GetUnit(*me, instance->GetGuidData(DATA_PLAYER_GUID));

                if (!target)
                {
                    EnterEvadeMode(EVADE_REASON_NO_HOSTILES);
                    return;
                }

                //target->CastSpell(target, SPELL_VAPOR_SUMMON, true); 需要核心支持
                if (Creature* pVapor = me->SummonCreature(NPC_VAPOR, target->GetPositionX() - 5 + rand32() % 10, target->GetPositionY() - 5 + rand32() % 10, target->GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN, 9s))
                {
                    if (pVapor->AI())
                        pVapor->AI()->AttackStart(target);
                    me->InterruptNonMeleeSpells(false);
                    DoCast(pVapor, SPELL_VAPOR_CHANNEL, false); // 核心bug修复
                    pVapor->CastSpell(pVapor, SPELL_VAPOR_TRIGGER, true);
                }

                events.ScheduleEvent(EVENT_FLIGHT_SEQUENCE, 10s);
                break;
            }
            case 4:  // 清理毒气轨迹，准备迷雾之息
                DespawnSummons(NPC_VAPOR_TRAIL);
                events.ScheduleEvent(EVENT_FLIGHT_SEQUENCE, 1ms);
                break;
            case 5:  // 移动到迷雾之息目标位置
            {
                Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 150, true);
                if (!target)
                    target = ObjectAccessor::GetUnit(*me, instance->GetGuidData(DATA_PLAYER_GUID));

                if (!target)
                {
                    EnterEvadeMode(EVADE_REASON_NO_HOSTILES);
                    return;
                }

                breathX = target->GetPositionX();
                breathY = target->GetPositionY();
                float x, y, z;
                target->GetContactPoint(me, x, y, z, 70);
                me->GetMotionMaster()->MovePoint(0, x, y, z+10);
                break;
            }
            case 6:  // 面向喷吐方向
                me->SetFacingTo(me->GetAbsoluteAngle(breathX, breathY));
                //DoTextEmote("takes a deep breath.", nullptr);
                events.ScheduleEvent(EVENT_FLIGHT_SEQUENCE, 10s);
                break;
            case 7:  // 施放迷雾之息并飞行
            {
                DoCast(me, SPELL_FOG_BREATH, true);
                float x, y, z;
                me->GetPosition(x, y, z);
                x = 2 * breathX - x;  // 计算飞行终点（目标另一侧）
                y = 2 * breathY - y;
                me->GetMotionMaster()->MovePoint(0, x, y, z);
                events.ScheduleEvent(EVENT_SUMMON_FOG, 1ms);  // 持续生成迷雾
                break;
            }
            case 8:  // 完成一次迷雾之息
                me->CastStop(SPELL_FOG_BREATH);
                me->RemoveAurasDueToSpell(SPELL_FOG_BREATH);
                ++uiBreathCount;
                events.ScheduleEvent(EVENT_FLIGHT_SEQUENCE, 1ms);
                if (uiBreathCount < 3)  // 施放3次迷雾之息
                    uiFlightCount = 4;  // 回到步骤5，再次施放迷雾之息
                break;
            case 9:  // 飞向地面目标
                if (Unit* target = SelectTarget(SelectTargetMethod::MaxThreat))
                    DoStartMovement(target);
                else
                {
                    EnterEvadeMode(EVADE_REASON_NO_HOSTILES);
                    return;
                }
                break;
            case 10:  // 降落并进入地面阶段
                me->SetDisableGravity(false);
                me->HandleEmoteCommand(EMOTE_ONESHOT_LAND);  // 降落动画
                EnterPhase(PHASE_GROUND);
                AttackStart(SelectTarget(SelectTargetMethod::MaxThreat));
                break;
        }
        ++uiFlightCount;
    }

/**
     * @brief 更新AI函数
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 每个游戏周期调用，处理BOSS的所有行为逻辑：
     * 1. 检查战斗状态
     * 2. 更新事件计时器
     * 3. 根据阶段执行不同的技能循环
     *
     * 性能注意事项：此函数每帧调用，应避免复杂计算
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有战斗目标
        if (!UpdateVictim())
        {
            // 在飞行阶段如果没有目标则逃避
            if (phase == PHASE_FLIGHT && !me->IsInEvadeMode())
                EnterEvadeMode(EVADE_REASON_NO_HOSTILES);
            return;
        }

        events.Update(diff);

        // 如果正在施法，等待施法完成
        if (me->IsNonMeleeSpellCast(false))
            return;

        // 地面阶段逻辑
        if (phase == PHASE_GROUND)
        {
            switch (events.ExecuteEvent())
            {
                case EVENT_BERSERK:  // 狂暴
                    Talk(YELL_BERSERK);
                    DoCast(me, SPELL_BERSERK, true);
                    events.ScheduleEvent(EVENT_BERSERK, 10s);
                    break;
                case EVENT_CLEAVE:  // 顺劈斩
                    DoCastVictim(SPELL_CLEAVE, false);
                    events.ScheduleEvent(EVENT_CLEAVE, 5s, 10s);
                    break;
                case EVENT_CORROSION:  // 腐蚀
                    DoCastVictim(SPELL_CORROSION, false);
                    events.ScheduleEvent(EVENT_CORROSION, 20s, 30s);
                    break;
                case EVENT_GAS_NOVA:  // 气体新星
                    DoCast(me, SPELL_GAS_NOVA, false);
                    events.ScheduleEvent(EVENT_GAS_NOVA, 20s, 25s);
                    break;
                case EVENT_ENCAPSULATE:  // 团雾
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 150, true))
                        DoCast(target, SPELL_ENCAPSULATE_CHANNEL, false);
                    events.ScheduleEvent(EVENT_ENCAPSULATE, 25s, 30s);
                    break;
                case EVENT_FLIGHT:  // 进入飞行阶段
                    EnterPhase(PHASE_FLIGHT);
                    break;
                default:
                    DoMeleeAttackIfReady();
                    break;
            }
        }

        // 飞行阶段逻辑
        if (phase == PHASE_FLIGHT)
        {
            switch (events.ExecuteEvent())
            {
                case EVENT_BERSERK:  // 狂暴
                    Talk(YELL_BERSERK);
                    DoCast(me, SPELL_BERSERK, true);
                    break;
                case EVENT_FLIGHT_SEQUENCE:  // 执行飞行序列下一步
                    HandleFlightSequence();
                    break;
                case EVENT_SUMMON_FOG:  // 在迷雾之息期间生成迷雾
                    {
                        float x, y, z;
                        me->GetPosition(x, y, z);
                        me->UpdateGroundPositionZ(x, y, z);
                        if (Creature* Fog = me->SummonCreature(NPC_VAPOR_TRAIL, x, y, z, 0, TEMPSUMMON_TIMED_DESPAWN, 10s))
                        {
                            Fog->RemoveAurasDueToSpell(SPELL_TRAIL_TRIGGER);
                            Fog->CastSpell(Fog, SPELL_FOG_TRIGGER, true);
                            me->CastSpell(Fog, SPELL_FOG_FORCE, true);
                        }
                    }
                    events.ScheduleEvent(EVENT_SUMMON_FOG, 1s);  // 每秒生成一次迷雾
                    break;
            }
        }
    }

    /**
     * @brief 消失召唤物
     * @param entry 召唤物的生物ID
     *
     * 消失所有指定ID的召唤物
     * 对于毒气轨迹，消失后会在原位置生成不死者
     */
    void DespawnSummons(uint32 entry)
    {
        std::vector<Position> unyieldingDeadPositions;
        summons.DespawnIf([&](ObjectGuid guid)
        {
            if (guid.GetEntry() != entry)
                return false;

            // 如果是毒气轨迹且在飞行阶段，记录位置用于生成不死者
            if (guid.GetEntry() == NPC_VAPOR_TRAIL && phase == PHASE_FLIGHT)
                if (Creature const* vapor = ObjectAccessor::GetCreature(*me, guid))
                    unyieldingDeadPositions.push_back(vapor->GetPosition());

            return true;
        });

        // 在毒气轨迹位置生成不死者
        for (Position const& unyieldingDeadPosition : unyieldingDeadPositions)
            me->SummonCreature(NPC_DEAD, unyieldingDeadPosition, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5s);
    }
};

/**
 * @struct npc_felmyst_vapor
 * @brief 毒气NPC AI结构体
 *
 * 毒气是菲米司在飞行阶段召唤的小怪
 * 会追踪随机玩家并留下毒气轨迹
 */
struct npc_felmyst_vapor : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_felmyst_vapor(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 重置函数
     */
    void Reset() override { }

    /**
     * @brief 进入战斗函数
     * @param who 进入战斗的目标
     *
     * 将毒气加入战斗并进入战斗状态
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        DoZoneInCombat();
        //DoCast(me, SPELL_VAPOR_FORCE, true); 核心bug
    }

    /**
     * @brief 更新AI函数
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 如果没有目标，随机选择一个目标攻击
     */
    void UpdateAI(uint32 /*diff*/) override
    {
        if (!me->GetVictim())
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                AttackStart(target);
    }
};

/**
 * @struct npc_felmyst_trail
 * @brief 毒气轨迹NPC AI结构体
 *
 * 毒气轨迹是毒气移动时留下的痕迹
 * 会对站在上面的玩家造成伤害
 * 最终会转化为不死者
 */
struct npc_felmyst_trail : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化时就施放轨迹触发法术
     */
    npc_felmyst_trail(Creature* creature) : ScriptedAI(creature)
    {
        DoCast(me, SPELL_TRAIL_TRIGGER, true);
        me->SetTarget(me->GetGUID());
        me->SetBoundingRadius(0.01f); // 核心bug修复
    }

    /**
     * @brief 重置函数
     */
    void Reset() override { }

    /**
     * @brief 进入战斗函数
     * @param who 进入战斗的目标
     */
    void JustEngagedWith(Unit* /*who*/) override { }

    /**
     * @brief 攻击开始函数
     * @param who 攻击目标
     *
     * 毒气轨迹不主动攻击
     */
    void AttackStart(Unit* /*who*/) override { }

    /**
     * @brief 视线内移动检测函数
     * @param who 进入视线的单位
     *
     * 毒气轨迹不检测视线
     */
    void MoveInLineOfSight(Unit* /*who*/) override { }

    /**
     * @brief 更新AI函数
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 毒气轨迹不需要主动行为
     */
    void UpdateAI(uint32 /*diff*/) override { }
};

/**
 * @brief 注册BOSS脚本
 *
 * 将菲米司及其召唤物的所有脚本注册到系统中
 */
void AddSC_boss_felmyst()
{
    RegisterSunwellPlateauCreatureAI(boss_felmyst);
    RegisterSunwellPlateauCreatureAI(npc_felmyst_vapor);
    RegisterSunwellPlateauCreatureAI(npc_felmyst_trail);
}
