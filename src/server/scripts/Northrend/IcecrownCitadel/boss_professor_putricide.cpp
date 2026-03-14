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
 * @file boss_professor_putricide.cpp
 * @brief 普崔塞德教授BOSS战AI实现
 *
 * 本模块实现了冰冠城塞副本中"瘟疫区"最终BOSS普崔塞德教授的完整战斗逻辑。
 * 普崔塞德教授是一场三个阶段的战斗，玩家需要应对多种软泥和气体相关的技能。
 *
 * 主要功能：
 * - 三阶段BOSS战AI（80%和35%血量切换阶段）
 * - 协助腐面和烂肠BOSS战的辅助逻辑
 * - 召唤不稳定实验（绿色软泥和气体云）
 * - 制造淤泥水坑、窒息毒气炸弹、延展粘液等技能
 * - 英雄模式下的无绑瘟疫和变异憎恶载具机制
 *
 * 相关BOSS：
 * - 腐面(Festergut)：普崔塞德教授会在其战斗中协助释放气体
 * - 烂肠(Rotface)：普崔塞德教授会在其战斗中协助释放软泥
 *
 * @see icecrown_citadel.h 副本实例脚本定义
 */

#include "icecrown_citadel.h"
#include "Containers.h"
#include "DBCStores.h"
#include "GridNotifiers.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "TemporarySummon.h"
#include "Vehicle.h"

/**
 * @enum Say
 * @brief BOSS台词和表情枚举
 *
 * 定义了普崔塞德教授在战斗中的所有台词和表情ID
 * 包括协助腐面、烂肠时的台词以及自身战斗的台词
 */
enum Say
{
    // Festergut - 腐面相关台词
    SAY_FESTERGUT_GASEOUS_BLIGHT    = 0,    ///< 腐面释放气体灾祸时的台词
    SAY_FESTERGUT_DEATH             = 1,    ///< 腐面死亡时的台词

    // Rotface - 烂肠相关台词
    SAY_ROTFACE_OOZE_FLOOD          = 2,    ///< 烂肠释放软泥洪流时的台词
    SAY_ROTFACE_DEATH               = 3,    ///< 烂肠死亡时的台词

    // Professor Putricide - 普崔塞德教授自身台词
    SAY_AGGRO                       = 4,    ///< 开怪台词
    EMOTE_UNSTABLE_EXPERIMENT       = 5,    ///< 不稳定实验表情
    SAY_PHASE_TRANSITION_HEROIC     = 6,    ///< 英雄模式阶段转换台词
    SAY_TRANSFORM_1                 = 7,    ///< 第一次变形台词（P1->P2）
    SAY_TRANSFORM_2                 = 8,    ///< 第二次变形台词（P2->P3），始终用于P2转换，请勿与SAY_TRANSFORM_1分组
    EMOTE_MALLEABLE_GOO             = 9,    ///< 延展粘液表情
    EMOTE_CHOKING_GAS_BOMB          = 10,   ///< 窒息毒气炸弹表情
    SAY_KILL                        = 11,   ///< 击杀玩家台词
    SAY_BERSERK                     = 12,   ///< 狂暴台词
    SAY_DEATH                       = 13    ///< 死亡台词
};

/**
 * @enum Spells
 * @brief 法术ID枚举
 *
 * 定义了普崔塞德教授战斗相关的所有法术ID
 * 包括腐面、烂肠协助法术和教授自身战斗法术
 */
enum Spells
{
    // Festergut - 腐面相关法术
    SPELL_RELEASE_GAS_VISUAL                = 69125,    ///< 释放气体视觉效果
    SPELL_GASEOUS_BLIGHT_LARGE              = 69157,    ///< 大型气体灾祸
    SPELL_GASEOUS_BLIGHT_MEDIUM             = 69162,    ///< 中型气体灾祸
    SPELL_GASEOUS_BLIGHT_SMALL              = 69164,    ///< 小型气体灾祸
    SPELL_MALLEABLE_GOO_H                   = 72296,    ///< 英雄模式延展粘液
    SPELL_MALLEABLE_GOO_SUMMON              = 72299,    ///< 召唤延展粘液

    // Professor Putricide - 普崔塞德教授法术
    SPELL_SLIME_PUDDLE_TRIGGER              = 70341,    ///< 淤泥水坑触发器
    SPELL_MALLEABLE_GOO                     = 70852,    ///< 延展粘液
    SPELL_UNSTABLE_EXPERIMENT               = 70351,    ///< 不稳定实验（召唤软泥或气体）
    SPELL_TEAR_GAS                          = 71617,    ///< 催泪毒气（阶段转换）
    SPELL_TEAR_GAS_TRIGGER_MISSILE          = 71615,    ///< 催泪毒气触发导弹
    SPELL_TEAR_GAS_CREATURE                 = 71618,    ///< 催泪毒气生物效果
    SPELL_TEAR_GAS_CANCEL                   = 71620,    ///< 取消催泪毒气
    SPELL_TEAR_GAS_PERIODIC_TRIGGER         = 73170,    ///< 催泪毒气周期性触发
    SPELL_CREATE_CONCOCTION                 = 71621,    ///< 制造合剂（P1->P2转换）
    SPELL_GUZZLE_POTIONS                    = 71893,    ///< 痛饮药剂（P2->P3转换）
    SPELL_OOZE_TANK_PROTECTION              = 71770,    ///< 软泥坦克保护（保护坦克）
    SPELL_CHOKING_GAS_BOMB                  = 71255,    ///< 窒息毒气炸弹
    SPELL_OOZE_VARIABLE                     = 74118,    ///< 软泥变量（英雄模式标记）
    SPELL_GAS_VARIABLE                      = 74119,    ///< 气体变量（英雄模式标记）
    SPELL_UNBOUND_PLAGUE                    = 70911,    ///< 无绑瘟疫（英雄模式）
    SPELL_UNBOUND_PLAGUE_SEARCHER           = 70917,    ///< 无绑瘟疫搜索者
    SPELL_PLAGUE_SICKNESS                   = 70953,    ///< 瘟疫疾病
    SPELL_UNBOUND_PLAGUE_PROTECTION         = 70955,    ///< 无绑瘟疫保护
    SPELL_MUTATED_PLAGUE                    = 72451,    ///< 变异瘟疫（P3主要技能）
    SPELL_MUTATED_PLAGUE_CLEAR              = 72618,    ///< 清除变异瘟疫

    // Slime Puddle - 淤泥水坑法术
    SPELL_GROW_STACKER                      = 70345,    ///< 成长堆叠器
    SPELL_GROW                              = 70347,    ///< 成长
    SPELL_SLIME_PUDDLE_AURA                 = 70343,    ///< 淤泥水坑光环

    // Gas Cloud - 气体云法术
    SPELL_GASEOUS_BLOAT_PROC                = 70215,    ///< 气体膨胀处理
    SPELL_GASEOUS_BLOAT                     = 70672,    ///< 气体膨胀
    SPELL_GASEOUS_BLOAT_PROTECTION          = 70812,    ///< 气体膨胀保护
    SPELL_EXPUNGED_GAS                      = 70701,    ///< 排出气体

    // Volatile Ooze - 不稳定软泥法术
    SPELL_OOZE_ERUPTION                     = 70492,    ///< 软泥喷发
    SPELL_VOLATILE_OOZE_ADHESIVE            = 70447,    ///< 不稳定软泥粘合剂
    SPELL_OOZE_ERUPTION_SEARCH_PERIODIC     = 70457,    ///< 软泥喷发周期搜索
    SPELL_VOLATILE_OOZE_PROTECTION          = 70530,    ///< 不稳定软泥保护

    // Choking Gas Bomb - 窒息毒气炸弹法术
    SPELL_CHOKING_GAS_BOMB_PERIODIC         = 71259,    ///< 窒息毒气炸弹周期效果
    SPELL_CHOKING_GAS_EXPLOSION_TRIGGER     = 71280,    ///< 窒息毒气爆炸触发

    // Mutated Abomination vehicle - 变异憎恶载具法术
    SPELL_ABOMINATION_VEHICLE_POWER_DRAIN   = 70385,    ///< 憎恶载具能量消耗
    SPELL_MUTATED_TRANSFORMATION            = 70311,    ///< 变异转化（召唤憎恶）
    SPELL_MUTATED_TRANSFORMATION_DAMAGE     = 70405,    ///< 变异转化伤害
    SPELL_MUTATED_TRANSFORMATION_NAME       = 72401,    ///< 变异转化名称

    // Unholy Infusion - 邪恶灌注成就
    SPELL_UNHOLY_INFUSION_CREDIT            = 71518     ///< 邪恶灌注成就积分
};

/// 气体膨胀辅助宏，根据团队规模返回对应法术ID
#define SPELL_GASEOUS_BLOAT_HELPER RAID_MODE<uint32>(70672, 72455, 72832, 72833)

/**
 * @enum Events
 * @brief 事件ID枚举
 *
 * 定义了普崔塞德教授战斗中所有定时事件ID
 * 用于事件调度器管理技能施放时机
 */
enum Events
{
    // Festergut - 腐面相关事件
    EVENT_FESTERGUT_DIES        = 1,    ///< 腐面死亡事件
    EVENT_FESTERGUT_GOO         = 2,    ///< 腐面战斗中的延展粘液（英雄模式）

    // Rotface - 烂肠相关事件
    EVENT_ROTFACE_DIES          = 3,    ///< 烂肠死亡事件
    EVENT_ROTFACE_OOZE_FLOOD    = 5,    ///< 烂肠战斗中的软泥洪流

    // Professor Putricide - 普崔塞德教授战斗事件
    EVENT_BERSERK               = 6,    ///< 狂暴事件（所有阶段）
    EVENT_SLIME_PUDDLE          = 7,    ///< 淤泥水坑事件（所有阶段）
    EVENT_UNSTABLE_EXPERIMENT   = 8,    ///< 不稳定实验事件（P1和P2）
    EVENT_TEAR_GAS              = 9,    ///< 催泪毒气事件（非英雄模式阶段转换）
    EVENT_RESUME_ATTACK         = 10,   ///< 恢复攻击事件
    EVENT_MALLEABLE_GOO         = 11,   ///< 延展粘液事件（P2+）
    EVENT_CHOKING_GAS_BOMB      = 12,   ///< 窒息毒气炸弹事件（P2+）
    EVENT_UNBOUND_PLAGUE        = 13,   ///< 无绑瘟疫事件（英雄模式）
    EVENT_MUTATED_PLAGUE        = 14,   ///< 变异瘟疫事件（P3）
    EVENT_PHASE_TRANSITION      = 15    ///< 阶段转换完成事件
};

/**
 * @enum Phases
 * @brief 战斗阶段枚举
 *
 * 定义了普崔塞德教授战斗的各个阶段
 * 包括协助其他BOSS的阶段和自身战斗的阶段
 */
enum Phases
{
    PHASE_NONE          = 0,    ///< 无阶段
    PHASE_FESTERGUT     = 1,    ///< 协助腐面阶段
    PHASE_ROTFACE       = 2,    ///< 协助烂肠阶段
    PHASE_COMBAT_1      = 4,    ///< 战斗第一阶段（100%-80%）
    PHASE_COMBAT_2      = 5,    ///< 战斗第二阶段（80%-35%）
    PHASE_COMBAT_3      = 6     ///< 战斗第三阶段（35%-0%）
};

/**
 * @enum Points
 * @brief 移动路径点ID枚举
 *
 * 定义了普崔塞德教授移动的目标点ID
 */
enum Points
{
    POINT_FESTERGUT = 366260,   ///< 腐面观察点
    POINT_ROTFACE   = 366270,   ///< 烂肠观察点
    POINT_TABLE     = 366780    ///< 实验桌位置（阶段转换时喝药水）
};

/// 腐面观察位置 - 教授在此位置释放气体（表情432）
Position const festergutWatchPos = {4324.820f, 3166.03f, 389.3831f, 3.316126f};
/// 烂肠观察位置 - 教授在此位置释放软泥（表情432）
Position const rotfaceWatchPos   = {4390.371f, 3164.50f, 389.3890f, 5.497787f};
/// 实验桌位置 - 教授在此喝药水进行阶段转换
Position const tablePos          = {4356.190f, 3262.90f, 389.4820f, 1.483530f};

/// 软泥洪流法术ID数组，用于烂肠战斗中的四个角落轮流释放
uint32 const oozeFloodSpells[4] = {69782, 69796, 69798, 69801};

/**
 * @enum PutricideData
 * @brief 普崔塞德教授自定义数据枚举
 *
 * 定义了用于AI内部通信的数据ID
 */
enum PutricideData
{
    DATA_EXPERIMENT_STAGE   = 1,    ///< 实验阶段标记（软泥或气体）
    DATA_PHASE              = 2,    ///< 当前战斗阶段
    DATA_ABOMINATION        = 3     ///< 是否存在变异憎恶
};

/// 实验状态：软泥阶段
#define EXPERIMENT_STATE_OOZE   false
/// 实验状态：气体阶段
#define EXPERIMENT_STATE_GAS    true

/**
 * @class AbominationDespawner
 * @brief 变异憎恶移除器
 *
 * 函数对象类，用于从召唤列表中检查并移除变异憎恶
 * 在第三阶段开始时调用，清除所有玩家控制的憎恶
 *
 * 使用方法：
 * summons.DespawnIf(AbominationDespawner(me));
 */
class AbominationDespawner
{
    public:
        /**
         * @brief 构造函数
         * @param owner 所有者单位（通常是教授自己）
         */
        explicit AbominationDespawner(Unit* owner) : _owner(owner) { }

        /**
         * @brief 函数调用操作符
         * @param guid 召唤单位的GUID
         * @return true 表示应该从召唤列表中移除
         * @return false 表示应该保留在召唤列表中
         *
         * 检查逻辑：
         * 1. 如果找不到单位，返回true移除
         * 2. 如果是变异憎恶，先让乘客下车，再返回true移除
         * 3. 如果不是变异憎恶，返回false保留
         */
        bool operator()(ObjectGuid guid)
        {
            if (Unit* summon = ObjectAccessor::GetUnit(*_owner, guid))
            {
                if (summon->GetEntry() == NPC_MUTATED_ABOMINATION_10 || summon->GetEntry() == NPC_MUTATED_ABOMINATION_25)
                {
                    // 让所有乘客下车，这也会触发载具的消失
                    if (Vehicle* veh = summon->GetVehicleKit())
                        veh->RemoveAllPassengers(); // 同时会消失载具

                    // 找到的是变异憎恶，应该移除
                    return true;
                }

                // 找到的不是变异憎恶，保留它
                return false;
            }

            // 没有找到单位，从召唤列表中移除
            return true;
        }

    private:
        Unit* _owner;  ///< 所有者单位指针
};

/**
 * @struct RotfaceHeightCheck
 * @brief 烂肠高度检查器
 *
 * 函数对象结构体，用于过滤掉位置过高的软泥洪流目标
 * 烂肠战斗中需要在四个角落释放软泥，此检查器排除不在同一平面的目标
 */
struct RotfaceHeightCheck
{
    /**
     * @brief 构造函数
     * @param rotface 烂肠生物指针
     */
    RotfaceHeightCheck(Creature* rotface) : _rotface(rotface) { }

    /**
     * @brief 函数调用操作符
     * @param stalker 要检查的生物
     * @return true 如果目标高度过高，应该被移除
     * @return false 如果目标高度合适
     */
    bool operator()(Creature* stalker) const
    {
        return stalker->GetPositionZ() > _rotface->GetPositionZ() + 5.0f;
    }

private:
    Creature* _rotface;  ///< 烂肠生物指针，用于高度比较
};

/**
 * @struct boss_professor_putricide
 * @brief 普崔塞德教授BOSS AI结构体
 *
 * 实现了普崔塞德教授的完整战斗AI，包括：
 * - 三阶段战斗逻辑（80%和35%血量转换）
 * - 协助腐面和烂肠BOSS战斗
 * - 召唤各种软泥和气体生物
 * - 施放多种技能和法术
 *
 * 继承自BossAI基类，使用事件调度器管理技能施放
 */
struct boss_professor_putricide : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化成员变量：
     * - _baseSpeed: 记录基础移动速度，用于加速移动时恢复
     * - _experimentState: 实验状态，交替召唤软泥和气体
     * - _phase: 当前战斗阶段
     * - _oozeFloodStage: 软泥洪流阶段索引
     */
    boss_professor_putricide(Creature* creature) : BossAI(creature, DATA_PROFESSOR_PUTRICIDE),
        _baseSpeed(creature->GetSpeedRate(MOVE_RUN)), _experimentState(EXPERIMENT_STATE_OOZE)
    {
        _phase = PHASE_NONE;
        _oozeFloodStage = 0;
    }

    /**
     * @brief 重置函数
     *
     * 当BOSS脱离战斗或重置时调用
     * 重置所有战斗状态，清理召唤物，恢复初始状态
     *
     * 执行操作：
     * - 如果不在协助阶段，设置BOSS状态为未开始
     * - 重置"反胃"成就标记
     * - 清空事件队列和召唤列表
     * - 重置阶段和实验状态
     * - 如果腐面和烂肠已击杀，移除无敌标志
     */
    void Reset() override
    {
        if (!(events.IsInPhase(PHASE_ROTFACE) || events.IsInPhase(PHASE_FESTERGUT)))
            instance->SetBossState(DATA_PROFESSOR_PUTRICIDE, NOT_STARTED);
        instance->SetData(DATA_NAUSEA_ACHIEVEMENT, uint32(true));

        events.Reset();
        summons.DespawnAll();
        SetPhase(PHASE_COMBAT_1);
        _experimentState = EXPERIMENT_STATE_OOZE;
        me->SetReactState(REACT_DEFENSIVE);
        me->SetWalk(false);

        // 如果腐面和烂肠都已击杀，移除无敌状态使教授可以被攻击
        if (instance->GetBossState(DATA_ROTFACE) == DONE && instance->GetBossState(DATA_FESTERGUT) == DONE)
        {
            me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
            me->SetImmuneToPC(false);
        }
    }

    /**
     * @brief 进入战斗函数
     * @param who 攻击者
     *
     * 当BOSS进入战斗时调用
     * 初始化战斗事件调度，设置第一阶段
     *
     * 调度事件：
     * - 10分钟狂暴计时器
     * - 10秒后第一个淤泥水坑
     * - 30-35秒后不稳定实验
     * - 英雄模式20秒后无绑瘟疫
     */
    void JustEngagedWith(Unit* who) override
    {
        // 如果在协助腐面或烂肠，不处理开怪
        if (events.IsInPhase(PHASE_ROTFACE) || events.IsInPhase(PHASE_FESTERGUT))
            return;

        // 检查前置BOSS是否已击杀
        if (!instance->CheckRequiredBosses(DATA_PROFESSOR_PUTRICIDE, who->ToPlayer()))
        {
            EnterEvadeMode();
            instance->DoCastSpellOnPlayers(LIGHT_S_HAMMER_TELEPORT);
            return;
        }

        me->setActive(true);
        events.Reset();
        events.ScheduleEvent(EVENT_BERSERK, 10min);
        events.ScheduleEvent(EVENT_SLIME_PUDDLE, 10s);
        events.ScheduleEvent(EVENT_UNSTABLE_EXPERIMENT, 30s, 35s);
        if (IsHeroic())
            events.ScheduleEvent(EVENT_UNBOUND_PLAGUE, 20s);

        SetPhase(PHASE_COMBAT_1);
        Talk(SAY_AGGRO);
        DoCast(me, SPELL_OOZE_TANK_PROTECTION, true);  // 施放软泥坦克保护buff
        DoZoneInCombat(me);
        me->SetCombatPulseDelay(5);
        instance->SetBossState(DATA_PROFESSOR_PUTRICIDE, IN_PROGRESS);
    }

    /**
     * @brief 到达出生点函数
     *
     * BOSS归位时调用，处理战斗失败状态
     */
    void JustReachedHome() override
    {
        _JustReachedHome();
        me->SetWalk(false);
        // 如果在自身战斗阶段归位，设置为失败
        if (events.IsInPhase(PHASE_COMBAT_1) || events.IsInPhase(PHASE_COMBAT_2) || events.IsInPhase(PHASE_COMBAT_3))
            instance->SetBossState(DATA_PROFESSOR_PUTRICIDE, FAIL);
    }

    /**
     * @brief 击杀单位函数
     * @param victim 被击杀的单位
     *
     * 击杀玩家时播放击杀台词
     */
    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_KILL);
    }

    /**
     * @brief 死亡函数
     * @param killer 击杀者
     *
     * BOSS死亡时调用，处理战利品和成就
     *
     * 执行操作：
     * - 播放死亡台词
     * - 25人模式且有暗影之锋buff时，施放邪恶灌注成就积分
     * - 清除变异瘟疫debuff
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);

        // 邪恶灌注成就：25人模式下使用暗影之锋击杀教授
        if (Is25ManRaid() && me->HasAura(SPELL_SHADOWS_FATE))
            DoCastAOE(SPELL_UNHOLY_INFUSION_CREDIT, true);

        DoCast(SPELL_MUTATED_PLAGUE_CLEAR);
    }

    /**
     * @brief 召唤生物函数
     * @param summon 被召唤的生物
     *
     * 处理各种召唤物的初始化
     *
     * 召唤物类型：
     * - 延展粘液追踪者：施放英雄模式延展粘液
     * - 成长的淤泥水坑：施放成长光环和初始堆叠
     * - 气体云：设置被动反应状态
     * - 不稳定软泥：设置被动反应状态
     * - 窒息毒气炸弹：施放周期性伤害和爆炸触发
     * - 变异憎恶：玩家载具，不额外处理
     */
    void JustSummoned(Creature* summon) override
    {
        summons.Summon(summon);
        switch (summon->GetEntry())
        {
            case NPC_MALLEABLE_OOZE_STALKER:
                DoCast(summon, SPELL_MALLEABLE_GOO_H);
                return;
            case NPC_GROWING_OOZE_PUDDLE:
                summon->CastSpell(summon, SPELL_GROW_STACKER, true);
                summon->CastSpell(summon, SPELL_SLIME_PUDDLE_AURA, true);
                // 暴雪在初始时施放7次成长法术（从sniff确认）
                for (uint8 i = 0; i < 7; ++i)
                    summon->CastSpell(summon, SPELL_GROW, true);
                break;
            case NPC_GAS_CLOUD:
                // 在sniff中未发现添加此光环状态的光环
                summon->ModifyAuraState(AURA_STATE_UNKNOWN22, true);
                summon->SetReactState(REACT_PASSIVE);
                break;
            case NPC_VOLATILE_OOZE:
                // 在sniff中未发现添加此光环状态的光环
                summon->ModifyAuraState(AURA_STATE_UNKNOWN19, true);
                summon->SetReactState(REACT_PASSIVE);
                break;
            case NPC_CHOKING_GAS_BOMB:
                summon->CastSpell(summon, SPELL_CHOKING_GAS_BOMB_PERIODIC, true);
                summon->CastSpell(summon, SPELL_CHOKING_GAS_EXPLOSION_TRIGGER, true);
                return;
            case NPC_MUTATED_ABOMINATION_10:
            case NPC_MUTATED_ABOMINATION_25:
                return;
            default:
                break;
        }

        if (me->IsInCombat())
            DoZoneInCombat(summon);
    }

    /**
     * @brief 受伤函数
     * @param attacker 攻击者
     * @param damage 伤害值（可修改）
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 根据血量百分比触发阶段转换
     * - 80%血量：从P1转换到P2
     * - 35%血量：从P2转换到P3
     *
     * 性能注意事项：
     * - 每次受伤都会调用，需要快速返回
     * - 施法状态下不检查血量
     */
    void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 施法中不处理阶段转换
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        switch (_phase)
        {
            case PHASE_COMBAT_1:
                if (HealthAbovePct(80))
                    return;
                me->SetReactState(REACT_PASSIVE);
                DoAction(ACTION_CHANGE_PHASE);
                break;
            case PHASE_COMBAT_2:
                if (HealthAbovePct(35))
                    return;
                me->SetReactState(REACT_PASSIVE);
                DoAction(ACTION_CHANGE_PHASE);
                break;
            default:
                break;
        }
    }

    /**
     * @brief 移动完成通知函数
     * @param type 移动类型
     * @param id 路径点ID
     *
     * 处理教授到达指定位置后的逻辑
     *
     * 路径点：
     * - POINT_FESTERGUT: 到达腐面观察点，释放气体灾祸
     * - POINT_ROTFACE: 到达烂肠观察点，释放软泥洪流
     * - POINT_TABLE: 到达实验桌，喝药水进行阶段转换
     */
    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE)
            return;
        switch (id)
        {
            case POINT_FESTERGUT:
                // 设置腐面BOSS为进行中（用于延迟关门）
                instance->SetBossState(DATA_FESTERGUT, IN_PROGRESS);
                me->SetSpeedRate(MOVE_RUN, _baseSpeed);
                DoAction(ACTION_FESTERGUT_GAS);
                // 让腐面施放大型气体灾祸
                if (Creature* festergut = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_FESTERGUT)))
                    festergut->CastSpell(festergut, SPELL_GASEOUS_BLIGHT_LARGE, CastSpellExtraArgs().SetOriginalCaster(festergut->GetGUID()));
                break;
            case POINT_ROTFACE:
                // 设置烂肠BOSS为进行中（用于延迟关门）
                instance->SetBossState(DATA_ROTFACE, IN_PROGRESS);
                me->SetSpeedRate(MOVE_RUN, _baseSpeed);
                DoAction(ACTION_ROTFACE_OOZE);
                events.ScheduleEvent(EVENT_ROTFACE_OOZE_FLOOD, 25s, 0, PHASE_ROTFACE);
                break;
            case POINT_TABLE:
                // 停止攻击，面向实验桌
                me->GetMotionMaster()->MoveIdle();
                me->SetSpeedRate(MOVE_RUN, _baseSpeed);
                if (GameObject* table = ObjectAccessor::GetGameObject(*me, instance->GetGuidData(DATA_PUTRICIDE_TABLE)))
                    me->SetFacingToObject(table);
                // 已经在新阶段中操作
                switch (_phase)
                {
                    case PHASE_COMBAT_2:
                    {
                        // P1->P2：制造合剂
                        SpellInfo const* spell = sSpellMgr->GetSpellInfo(SPELL_CREATE_CONCOCTION);
                        DoCast(me, SPELL_CREATE_CONCOCTION);
                        events.ScheduleEvent(EVENT_PHASE_TRANSITION, Milliseconds(sSpellMgr->GetSpellForDifficultyFromSpell(spell, me)->CalcCastTime()) + 100ms);
                        break;
                    }
                    case PHASE_COMBAT_3:
                    {
                        // P2->P3：痛饮药剂
                        SpellInfo const* spell = sSpellMgr->GetSpellInfo(SPELL_GUZZLE_POTIONS);
                        DoCast(me, SPELL_GUZZLE_POTIONS);
                        events.ScheduleEvent(EVENT_PHASE_TRANSITION, Milliseconds(sSpellMgr->GetSpellForDifficultyFromSpell(spell, me)->CalcCastTime()) + 100ms);
                        break;
                    }
                    default:
                        break;
                }
                break;
            default:
                break;
        }
    }

    /**
     * @brief 执行动作函数
     * @param action 动作ID
     *
     * 处理外部触发的各种动作
     *
     * 动作类型：
     * - ACTION_FESTERGUT_COMBAT: 腐面战斗开始，移动到观察点
     * - ACTION_FESTERGUT_GAS: 释放气体灾祸视觉效果
     * - ACTION_FESTERGUT_DEATH: 腐面死亡处理
     * - ACTION_ROTFACE_COMBAT: 烂肠战斗开始，移动到观察点并初始化软泥洪流序列
     * - ACTION_ROTFACE_OOZE: 释放软泥洪流
     * - ACTION_ROTFACE_DEATH: 烂肠死亡处理
     * - ACTION_CHANGE_PHASE: 阶段转换
     */
    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_FESTERGUT_COMBAT:
                SetPhase(PHASE_FESTERGUT);
                me->SetSpeedRate(MOVE_RUN, _baseSpeed*2.0f);  // 加速移动
                me->GetMotionMaster()->MovePoint(POINT_FESTERGUT, festergutWatchPos);
                me->SetReactState(REACT_PASSIVE);
                EngagementStart(nullptr);
                // 英雄模式下周期性施放延展粘液
                if (IsHeroic())
                    events.ScheduleEvent(EVENT_FESTERGUT_GOO, 13s, 18s, 0, PHASE_FESTERGUT);
                break;
            case ACTION_FESTERGUT_GAS:
                Talk(SAY_FESTERGUT_GASEOUS_BLIGHT);
                DoCast(me, SPELL_RELEASE_GAS_VISUAL, true);
                break;
            case ACTION_FESTERGUT_DEATH:
                events.ScheduleEvent(EVENT_FESTERGUT_DIES, 4s, 0, PHASE_FESTERGUT);
                break;
            case ACTION_ROTFACE_COMBAT:
                SetPhase(PHASE_ROTFACE);
                me->SetSpeedRate(MOVE_RUN, _baseSpeed*2.0f);  // 加速移动
                me->GetMotionMaster()->MovePoint(POINT_ROTFACE, rotfaceWatchPos);
                me->SetReactState(REACT_PASSIVE);
                EngagementStart(nullptr);
                _oozeFloodStage = 0;
                // 初始化随机的软泥洪流序列
                if (Creature* rotface = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_ROTFACE)))
                {
                    std::list<Creature*> list;
                    GetCreatureListWithEntryInGrid(list, rotface, NPC_PUDDLE_STALKER, 50.0f);
                    list.remove_if(RotfaceHeightCheck(rotface));  // 移除高度不合适的目标
                    if (list.size() > 4)
                    {
                        list.sort(Trinity::ObjectDistanceOrderPred(rotface));
                        do
                        {
                            list.pop_back();
                        } while (list.size() > 4);
                    }

                    // 随机分配4个角落的软泥洪流目标
                    uint8 i = 0;
                    while (!list.empty())
                    {
                        std::list<Creature*>::iterator itr = list.begin();
                        std::advance(itr, urand(0, list.size()-1));
                        _oozeFloodDummyGUIDs[i++] = (*itr)->GetGUID();
                        list.erase(itr);
                    }
                }
                break;
            case ACTION_ROTFACE_OOZE:
                Talk(SAY_ROTFACE_OOZE_FLOOD);
                if (Creature* dummy = ObjectAccessor::GetCreature(*me, _oozeFloodDummyGUIDs[_oozeFloodStage]))
                    // 从自己施放以通过视线检查（使用教授的GUID用于日志）
                    dummy->CastSpell(dummy, oozeFloodSpells[_oozeFloodStage], me->GetGUID());
                if (++_oozeFloodStage == 4)
                    _oozeFloodStage = 0;
                break;
            case ACTION_ROTFACE_DEATH:
                events.ScheduleEvent(EVENT_ROTFACE_DIES, 4500ms, 0, PHASE_ROTFACE);
                break;
            case ACTION_CHANGE_PHASE:
                me->SetSpeedRate(MOVE_RUN, _baseSpeed*2.0f);  // 加速移动到实验桌
                events.DelayEvents(30s);  // 延迟所有事件30秒
                me->AttackStop();
                if (!IsHeroic())
                {
                    // 普通模式：施放催泪毒气冻结所有玩家
                    DoCast(me, SPELL_TEAR_GAS);
                    events.ScheduleEvent(EVENT_TEAR_GAS, 2500ms);
                }
                else
                {
                    // 英雄模式：施放两次不稳定实验并标记玩家
                    Talk(SAY_PHASE_TRANSITION_HEROIC);
                    DoCast(me, SPELL_UNSTABLE_EXPERIMENT, true);
                    DoCast(me, SPELL_UNSTABLE_EXPERIMENT, true);
                    // 施放变量标记
                    if (Is25ManRaid())
                    {
                        std::list<Unit*> targetList;
                        {
                            for (ThreatReference const* ref : me->GetThreatManager().GetUnsortedThreatList())
                                if (Player* target = ref->GetVictim()->ToPlayer())
                                    targetList.push_back(target);
                        }

                        size_t half = targetList.size()/2;
                        // 一半玩家获得软泥变量
                        while (half < targetList.size())
                        {
                            std::list<Unit*>::iterator itr = targetList.begin();
                            advance(itr, urand(0, targetList.size() - 1));
                            (*itr)->CastSpell(*itr, SPELL_OOZE_VARIABLE, true);
                            targetList.erase(itr);
                        }
                        // 另一半玩家获得气体变量
                        for (std::list<Unit*>::iterator itr = targetList.begin(); itr != targetList.end(); ++itr)
                            (*itr)->CastSpell(*itr, SPELL_GAS_VARIABLE, true);
                    }
                    me->GetMotionMaster()->MovePoint(POINT_TABLE, tablePos);
                }
                switch (_phase)
                {
                    case PHASE_COMBAT_1:
                        SetPhase(PHASE_COMBAT_2);
                        events.ScheduleEvent(EVENT_MALLEABLE_GOO, 21s, 26s);
                        events.ScheduleEvent(EVENT_CHOKING_GAS_BOMB, 35s, 40s);
                        break;
                    case PHASE_COMBAT_2:
                        SetPhase(PHASE_COMBAT_3);
                        events.ScheduleEvent(EVENT_MUTATED_PLAGUE, 25s);
                        events.CancelEvent(EVENT_UNSTABLE_EXPERIMENT);  // P3不再召唤软泥
                        break;
                    default:
                        break;
                }
                break;
            default:
                break;
        }
    }

    /**
     * @brief 获取数据函数
     * @param type 数据类型
     * @return uint32 数据值
     *
     * 用于AI内部数据查询
     *
     * 查询类型：
     * - DATA_EXPERIMENT_STAGE: 当前实验状态（软泥或气体）
     * - DATA_PHASE: 当前战斗阶段
     * - DATA_ABOMINATION: 是否存在变异憎恶
     */
    uint32 GetData(uint32 type) const override
    {
        switch (type)
        {
            case DATA_EXPERIMENT_STAGE:
                return _experimentState;
            case DATA_PHASE:
                return _phase;
            case DATA_ABOMINATION:
                return uint32(summons.HasEntry(NPC_MUTATED_ABOMINATION_10) || summons.HasEntry(NPC_MUTATED_ABOMINATION_25));
            default:
                break;
        }

        return 0;
    }

    /**
     * @brief 设置数据函数
     * @param id 数据ID
     * @param data 数据值
     *
     * 用于AI内部数据设置
     * 主要用于切换实验状态
     */
    void SetData(uint32 id, uint32 data) override
    {
        if (id == DATA_EXPERIMENT_STAGE)
            _experimentState = data != 0;
    }

    /**
     * @brief 更新AI函数
     * @param diff 时间差（毫秒）
     *
     * 每帧调用，处理事件调度和战斗逻辑
     *
     * 主要事件处理：
     * - EVENT_FESTERGUT_DIES: 腐面死亡
     * - EVENT_FESTERGUT_GOO: 腐面战斗中的延展粘液（英雄模式）
     * - EVENT_ROTFACE_DIES: 烂肠死亡
     * - EVENT_ROTFACE_OOZE_FLOOD: 烂肠战斗中的软泥洪流
     * - EVENT_BERSERK: 狂暴
     * - EVENT_SLIME_PUDDLE: 淤泥水坑
     * - EVENT_UNSTABLE_EXPERIMENT: 不稳定实验
     * - EVENT_TEAR_GAS: 催泪毒气（阶段转换）
     * - EVENT_RESUME_ATTACK: 恢复攻击
     * - EVENT_MALLEABLE_GOO: 延展粘液
     * - EVENT_CHOKING_GAS_BOMB: 窒息毒气炸弹
     * - EVENT_UNBOUND_PLAGUE: 无绑瘟疫
     * - EVENT_MUTATED_PLAGUE: 变异瘟疫
     * - EVENT_PHASE_TRANSITION: 阶段转换完成
     *
     * 性能注意事项：
     * - 施法状态下跳过事件处理
     * - 使用while循环处理所有到期事件
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果不在协助阶段且没有目标，返回
        if (!(events.IsInPhase(PHASE_ROTFACE) || events.IsInPhase(PHASE_FESTERGUT)) && !UpdateVictim())
            return;

        events.Update(diff);

        // 施法中不处理新事件
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_FESTERGUT_DIES:
                    Talk(SAY_FESTERGUT_DEATH);
                    EnterEvadeMode();
                    break;
                case EVENT_FESTERGUT_GOO:
                    // 英雄模式下在腐面战斗中施放延展粘液
                    DoCastAOE(SPELL_MALLEABLE_GOO_SUMMON, CastSpellExtraArgs(true).AddSpellMod(SPELLVALUE_MAX_TARGETS, 1));
                    if (Is25ManRaid())
                        events.ScheduleEvent(EVENT_FESTERGUT_GOO, 10s, 15s, 0, PHASE_FESTERGUT);
                    else
                        events.ScheduleEvent(EVENT_FESTERGUT_GOO, 30s, 35s, 0, PHASE_FESTERGUT);
                    break;
                case EVENT_ROTFACE_DIES:
                    Talk(SAY_ROTFACE_DEATH);
                    EnterEvadeMode();
                    break;
                case EVENT_ROTFACE_OOZE_FLOOD:
                    DoAction(ACTION_ROTFACE_OOZE);
                    events.ScheduleEvent(EVENT_ROTFACE_OOZE_FLOOD, 25s, 0, PHASE_ROTFACE);
                    break;
                case EVENT_BERSERK:
                    Talk(SAY_BERSERK);
                    DoCast(me, SPELL_BERSERK2);
                    break;
                case EVENT_SLIME_PUDDLE:
                {
                    // 在两个随机位置施放淤泥水坑
                    std::list<Unit*> targets;
                    SelectTargetList(targets, 2, SelectTargetMethod::Random, 0, 0.0f, true);
                    if (!targets.empty())
                        for (std::list<Unit*>::iterator itr = targets.begin(); itr != targets.end(); ++itr)
                            DoCast(*itr, SPELL_SLIME_PUDDLE_TRIGGER);
                    events.ScheduleEvent(EVENT_SLIME_PUDDLE, 35s);
                    break;
                }
                case EVENT_UNSTABLE_EXPERIMENT:
                    Talk(EMOTE_UNSTABLE_EXPERIMENT);
                    DoCast(me, SPELL_UNSTABLE_EXPERIMENT);
                    events.ScheduleEvent(EVENT_UNSTABLE_EXPERIMENT, 35s, 40s);
                    break;
                case EVENT_TEAR_GAS:
                    // 非英雄模式：移动到实验桌并保持催泪毒气
                    me->GetMotionMaster()->MovePoint(POINT_TABLE, tablePos);
                    DoCast(me, SPELL_TEAR_GAS_PERIODIC_TRIGGER, true);
                    break;
                case EVENT_RESUME_ATTACK:
                    me->SetReactState(REACT_AGGRESSIVE);
                    AttackStart(me->GetVictim());
                    // 移除催泪毒气效果
                    me->RemoveAurasDueToSpell(SPELL_TEAR_GAS_PERIODIC_TRIGGER);
                    DoCastAOE(SPELL_TEAR_GAS_CANCEL);
                    instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_GAS_VARIABLE);
                    instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_OOZE_VARIABLE);
                    break;
                case EVENT_MALLEABLE_GOO:
                    // P2+阶段施放延展粘液
                    if (Is25ManRaid())
                    {
                        // 25人模式：对两个远程目标施放
                        std::list<Unit*> targets;
                        SelectTargetList(targets, 2, SelectTargetMethod::Random, 0, -7.0f, true);
                        if (!targets.empty())
                        {
                            Talk(EMOTE_MALLEABLE_GOO);
                            for (std::list<Unit*>::iterator itr = targets.begin(); itr != targets.end(); ++itr)
                                DoCast(*itr, SPELL_MALLEABLE_GOO);
                        }
                    }
                    else
                    {
                        // 10人模式：对一个远程目标施放
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1, -7.0f, true))
                        {
                            Talk(EMOTE_MALLEABLE_GOO);
                            DoCast(target, SPELL_MALLEABLE_GOO);
                        }
                    }
                    events.ScheduleEvent(EVENT_MALLEABLE_GOO, 25s, 30s);
                    break;
                case EVENT_CHOKING_GAS_BOMB:
                    // P2+阶段施放窒息毒气炸弹
                    Talk(EMOTE_CHOKING_GAS_BOMB);
                    DoCast(me, SPELL_CHOKING_GAS_BOMB);
                    events.ScheduleEvent(EVENT_CHOKING_GAS_BOMB, 35s, 40s);
                    break;
                case EVENT_UNBOUND_PLAGUE:
                    // 英雄模式：对非坦克目标施放无绑瘟疫
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, NonTankTargetSelector(me)))
                    {
                        DoCast(target, SPELL_UNBOUND_PLAGUE);
                        DoCast(target, SPELL_UNBOUND_PLAGUE_SEARCHER);
                    }
                    events.ScheduleEvent(EVENT_UNBOUND_PLAGUE, 90s);
                    break;
                case EVENT_MUTATED_PLAGUE:
                    // P3阶段对坦克施放变异瘟疫
                    DoCastVictim(SPELL_MUTATED_PLAGUE);
                    events.ScheduleEvent(EVENT_MUTATED_PLAGUE, 10s);
                    break;
                case EVENT_PHASE_TRANSITION:
                {
                    switch (_phase)
                    {
                        case PHASE_COMBAT_2:
                            // P1->P2转换完成：下跪并播放台词
                            if (Creature* face = me->FindNearestCreature(NPC_TEAR_GAS_TARGET_STALKER, 50.0f))
                                me->SetFacingToObject(face);
                            me->HandleEmoteCommand(EMOTE_ONESHOT_KNEEL);
                            Talk(SAY_TRANSFORM_1);
                            events.ScheduleEvent(EVENT_RESUME_ATTACK, 5500ms, 0, PHASE_COMBAT_2);
                            break;
                        case PHASE_COMBAT_3:
                            // P2->P3转换完成：下跪、播放台词、移除所有变异憎恶
                            if (Creature* face = me->FindNearestCreature(NPC_TEAR_GAS_TARGET_STALKER, 50.0f))
                                me->SetFacingToObject(face);
                            me->HandleEmoteCommand(EMOTE_ONESHOT_KNEEL);
                            Talk(SAY_TRANSFORM_2);
                            summons.DespawnIf(AbominationDespawner(me));  // 移除所有变异憎恶
                            events.ScheduleEvent(EVENT_RESUME_ATTACK, 8500ms, 0, PHASE_COMBAT_3);
                            break;
                        default:
                            break;
                    }
                    break;
                }
                default:
                    break;
            }

            // 施法中不继续处理事件
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    /**
     * @brief 设置阶段函数
     * @param newPhase 新阶段
     *
     * 同时设置AI内部阶段和事件映射阶段
     */
    void SetPhase(Phases newPhase)
    {
        _phase = newPhase;
        events.SetPhase(newPhase);
    }

    ObjectGuid _oozeFloodDummyGUIDs[4];  ///< 软泥洪流目标的GUID数组（4个角落）
    Phases _phase;                       ///< 当前战斗阶段（独立于EventMap，因为重置时事件阶段会被清空）
    float const _baseSpeed;              ///< 基础移动速度，用于加速后恢复
    uint8 _oozeFloodStage;               ///< 当前软泥洪流阶段索引（0-3循环）
    bool _experimentState;               ///< 实验状态（软泥或气体），交替切换
};

/**
 * @class npc_putricide_oozeAI
 * @brief 普崔塞德教授召唤的软泥基类AI
 *
 * 为不稳定软泥和气体云提供共享的AI逻辑
 * 主要功能：
 * - 随机选择目标并施放主法术
 * - 响应催泪毒气冻结效果
 * - 管理目标选择定时器
 *
 * 子类需要实现 CastMainSpell() 方法
 */
class npc_putricide_oozeAI : public ScriptedAI
{
    public:
        /**
         * @brief 构造函数
         * @param creature 生物对象
         * @param auraSpellId 光环法术ID（周期性搜索效果）
         * @param hitTargetSpellId 命中目标时触发的法术ID
         */
        npc_putricide_oozeAI(Creature* creature, uint32 auraSpellId, uint32 hitTargetSpellId) : ScriptedAI(creature),
            _auraSpellId(auraSpellId), _hitTargetSpellId(hitTargetSpellId), _newTargetSelectTimer(0), _instance(creature->GetInstanceScript()) { }

        /**
         * @brief 法术命中目标回调
         * @param target 目标
         * @param spellInfo 法术信息
         *
         * 当主法术命中目标时，设置1秒后选择新目标
         */
        void SpellHitTarget(WorldObject* /*target*/, SpellInfo const* spellInfo) override
        {
            if (!_newTargetSelectTimer && spellInfo->Id == sSpellMgr->GetSpellIdForDifficulty(_hitTargetSpellId, me))
            {
                _newTargetSelectTimer = 1000;
                // 进入被动状态直到选择下一个目标
                me->SetReactState(REACT_PASSIVE);
            }
        }

        /**
         * @brief 重置函数
         *
         * 如果教授战斗不在进行中，立即消失
         * 否则进入战斗并施放光环效果
         */
        void Reset() override
        {
            if (_instance->GetBossState(DATA_PROFESSOR_PUTRICIDE) != IN_PROGRESS)
                me->DespawnOrUnsummon();

            DoZoneInCombat();
            DoCastAOE(_auraSpellId, true);
        }

        /**
         * @brief 法术命中回调
         * @param caster 施法者
         * @param spellInfo 法术信息
         *
         * 被催泪毒气命中时，设置选择新目标的定时器
         */
        void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
        {
            if (spellInfo->Id == SPELL_TEAR_GAS_CREATURE)
                _newTargetSelectTimer = 1000;
        }

        /**
         * @brief 更新AI函数
         * @param diff 时间差（毫秒）
         *
         * 处理目标选择定时器和近战攻击
         * 当没有施法且没有定时器时，自动触发选择新目标
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim() && !_newTargetSelectTimer)
                return;

            // 如果没有定时器且没有施法，设置选择新目标的定时器
            if (!_newTargetSelectTimer && !me->IsNonMeleeSpellCast(false, false, true, false, true))
                _newTargetSelectTimer = 1000;

            DoMeleeAttackIfReady();

            if (!_newTargetSelectTimer)
                return;

            // 如果被催泪毒气冻结，等待
            if (me->HasAura(SPELL_TEAR_GAS_CREATURE))
                return;

            if (_newTargetSelectTimer <= diff)
            {
                _newTargetSelectTimer = 0;
                CastMainSpell();
            }
            else
                _newTargetSelectTimer -= diff;
        }

        /**
         * @brief 施放主法术（纯虚函数）
         *
         * 子类实现具体的目标选择和法术施放逻辑
         */
        virtual void CastMainSpell() = 0;

    private:
        uint32 _auraSpellId;           ///< 光环法术ID
        uint32 _hitTargetSpellId;      ///< 命中目标时触发的法术ID
        uint32 _newTargetSelectTimer;  ///< 新目标选择定时器（毫秒）
        InstanceScript* _instance;     ///< 副本实例脚本指针
};

/**
 * @struct npc_volatile_ooze
 * @brief 不稳定软泥AI结构体
 *
 * 绿色软泥，会随机选择目标并施放粘合剂
 * 被粘合剂粘住的玩家会受到软泥喷发伤害
 *
 * 继承自 npc_putricide_oozeAI
 */
struct npc_volatile_ooze : public npc_putricide_oozeAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象
     *
     * 初始化基类：
     * - 光环法术：软泥喷发周期搜索
     * - 命中触发法术：软泥喷发
     */
    npc_volatile_ooze(Creature* creature) : npc_putricide_oozeAI(creature, SPELL_OOZE_ERUPTION_SEARCH_PERIODIC, SPELL_OOZE_ERUPTION) { }

    /**
     * @brief 施放主法术
     *
     * 对自己施放不稳定软泥粘合剂，触发目标选择
     */
    void CastMainSpell() override
    {
        me->CastSpell(me, SPELL_VOLATILE_OOZE_ADHESIVE, false);
    }
};

/**
 * @struct npc_gas_cloud
 * @brief 气体云AI结构体
 *
 * 橙色气体云，会随机选择目标并施放气体膨胀
 * 气体膨胀会周期性造成伤害并叠加debuff
 *
 * 继承自 npc_putricide_oozeAI
 */
struct npc_gas_cloud : public npc_putricide_oozeAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象
     *
     * 初始化基类：
     * - 光环法术：气体膨胀处理
     * - 命中触发法术：排出气体
     */
    npc_gas_cloud(Creature* creature) : npc_putricide_oozeAI(creature, SPELL_GASEOUS_BLOAT_PROC, SPELL_EXPUNGED_GAS)
    {
        _newTargetSelectTimer = 0;
    }

    /**
     * @brief 施放主法术
     *
     * 对自己施放气体膨胀，带10层初始堆叠
     */
    void CastMainSpell() override
    {
        CastSpellExtraArgs args;
        args.AddSpellMod(SPELLVALUE_AURA_STACK, 10);
        me->CastSpell(me, SPELL_GASEOUS_BLOAT, args);
    }

private:
    uint32 _newTargetSelectTimer;  ///< 新目标选择定时器（覆盖基类）
};

/**
 * @class spell_putricide_gaseous_bloat
 * @brief 气体膨胀光环脚本（法术ID: 70672, 72455, 72832, 72833）
 *
 * 处理气体云施加的气体膨胀debuff
 * 周期性移除一层堆叠，当完全消失时让气体云重新施放
 * 被攻击时触发排出气体伤害
 */
class spell_putricide_gaseous_bloat : public AuraScript
{
    PrepareAuraScript(spell_putricide_gaseous_bloat);

    /**
     * @brief 处理周期性效果
     * @param aurEff 光环效果
     *
     * 每次周期性伤害时移除一层堆叠
     * 如果debuff完全消失，让气体云重新施放带10层的新debuff
     */
    void HandleExtraEffect(AuraEffect const* /*aurEff*/)
    {
        Unit* target = GetTarget();
        if (Unit* caster = GetCaster())
        {
            target->RemoveAuraFromStack(GetSpellInfo()->Id, GetCasterGUID());
            if (!target->HasAura(GetId()))
            {
                // 重新施放带10层堆叠的气体膨胀
                CastSpellExtraArgs args;
                args.AddSpellMod(SPELLVALUE_AURA_STACK, 10);
                caster->CastSpell(caster, SPELL_GASEOUS_BLOAT, args);
            }
        }
    }

    /**
     * @brief 处理触发事件
     * @param eventInfo 触发事件信息
     *
     * 当玩家攻击气体云时，根据堆叠层数计算排出气体伤害
     * 伤害公式：sum(mod * i) for i in 1..stack
     * - 10人模式：mod = 1250
     * - 25人模式：mod = 1500
     */
    void HandleProc(ProcEventInfo& eventInfo)
    {
        uint32 stack = GetStackAmount();
        Unit* caster = eventInfo.GetActor();

        int32 const mod = caster->GetMap()->Is25ManRaid() ? 1500 : 1250;
        int32 dmg = 0;
        for (uint8 i = 1; i <= stack; ++i)
            dmg += mod * i;

        CastSpellExtraArgs args;
        args.AddSpellBP0(dmg);
        caster->CastSpell(nullptr, SPELL_EXPUNGED_GAS, args);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_putricide_gaseous_bloat::HandleExtraEffect, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE);
        OnProc += AuraProcFn(spell_putricide_gaseous_bloat::HandleProc);
    }
};

/**
 * @class spell_putricide_ooze_channel
 * @brief 软泥引导法术脚本
 *
 * 处理不稳定软泥粘合剂（70447, 72836, 72837, 72838）和气体膨胀（70672, 72455, 72832, 72833）
 * 这些法术需要随机选择一个目标并锁定攻击
 *
 * 主要功能：
 * - 从范围内随机选择一个目标
 * - 锁定该目标（高仇恨值）
 * - 开始攻击
 */
class spell_putricide_ooze_channel : public SpellScript
{
    PrepareSpellScript(spell_putricide_ooze_channel);

    /**
     * @brief 验证法术
     * @param spell 法术信息
     * @return true 验证成功
     *
     * 验证排除目标光环法术是否存在
     */
    bool Validate(SpellInfo const* spell) override
    {
        return ValidateSpellInfo({ spell->ExcludeTargetAuraSpell });
    }

    /**
     * @brief 加载函数
     * @return true 如果施法者是生物
     *
     * 设置初始变量并检查施法者是否为生物
     * 这允许在整个脚本中安全地使用 ToCreature() 转换
     */
    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_UNIT;
    }

    /**
     * @brief 选择目标
     * @param targets 目标列表
     *
     * 从范围内随机选择一个目标
     * 如果没有目标，施法失败并消失软泥
     */
    void SelectTarget(std::list<WorldObject*>& targets)
    {
        if (targets.empty())
        {
            FinishCast(SPELL_FAILED_NO_VALID_TARGETS);
            GetCaster()->ToCreature()->DespawnOrUnsummon(1ms);    // 下次更新时消失
            return;
        }

        WorldObject* target = Trinity::Containers::SelectRandomContainerElement(targets);
        targets.clear();
        targets.push_back(target);
        _target = target;
    }

    /**
     * @brief 设置目标
     * @param targets 目标列表
     *
     * 确保后续效果作用于同一个目标
     */
    void SetTarget(std::list<WorldObject*>& targets)
    {
        targets.clear();
        if (_target)
            targets.push_back(_target);
    }

    /**
     * @brief 开始攻击
     *
     * 法术命中后，清除施法状态，设置高仇恨值并锁定目标
     * 仇恨值设为500000000（从sniff中确认的数值）
     */
    void StartAttack()
    {
        GetCaster()->ClearUnitState(UNIT_STATE_CASTING);
        GetCaster()->GetThreatManager().ResetAllThreat();
        GetCaster()->ToCreature()->AI()->AttackStart(GetHitUnit());
        GetCaster()->GetThreatManager().AddThreat(GetHitUnit(), 500000000.0f, nullptr, true, true);    // sniff中的数值
        GetCaster()->GetThreatManager().FixateTarget(GetHitUnit());
        GetCaster()->ToCreature()->SetReactState(REACT_AGGRESSIVE);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_putricide_ooze_channel::SelectTarget, EFFECT_0, TARGET_UNIT_SRC_AREA_ENEMY);
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_putricide_ooze_channel::SetTarget, EFFECT_1, TARGET_UNIT_SRC_AREA_ENEMY);
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_putricide_ooze_channel::SetTarget, EFFECT_2, TARGET_UNIT_SRC_AREA_ENEMY);
        AfterHit += SpellHitFn(spell_putricide_ooze_channel::StartAttack);
    }

    WorldObject* _target = nullptr;  ///< 选中的目标
};

/**
 * @class ExactDistanceCheck
 * @brief 精确距离检查器
 *
 * 函数对象类，用于检查单位是否超过指定距离
 * 主要用于淤泥水坑的范围检查，根据水坑大小动态调整
 */
class ExactDistanceCheck
{
    public:
        /**
         * @brief 构造函数
         * @param source 源单位
         * @param dist 距离阈值
         */
        ExactDistanceCheck(Unit* source, float dist) : _source(source), _dist(dist) { }

        /**
         * @brief 函数调用操作符
         * @param unit 要检查的对象
         * @return true 如果距离超过阈值
         */
        bool operator()(WorldObject* unit) const
        {
            return _source->GetExactDist2d(unit) > _dist;
        }

    private:
        Unit* _source;  ///< 源单位指针
        float _dist;    ///< 距离阈值
};

/**
 * @class spell_putricide_slime_puddle
 * @brief 淤泥水坑法术脚本（法术ID: 70346, 72456, 72868, 72869）
 *
 * 处理淤泥水坑的伤害范围
 * 水坑会随时间成长，范围根据水坑的缩放比例动态调整
 *
 * 范围计算：2.5 * ObjectScale
 */
class spell_putricide_slime_puddle : public SpellScript
{
    PrepareSpellScript(spell_putricide_slime_puddle);

    /**
     * @brief 缩放范围
     * @param targets 目标列表
     *
     * 根据施法者的缩放比例调整有效范围
     * 移除超出范围的单位
     */
    void ScaleRange(std::list<WorldObject*>& targets)
    {
        targets.remove_if(ExactDistanceCheck(GetCaster(), 2.5f * GetCaster()->GetObjectScale()));
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_putricide_slime_puddle::ScaleRange, EFFECT_0, TARGET_UNIT_DEST_AREA_ENEMY);
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_putricide_slime_puddle::ScaleRange, EFFECT_1, TARGET_UNIT_DEST_AREA_ENTRY);
    }
};

/**
 * @class spell_putricide_slime_puddle_aura
 * @brief 淤泥水坑光环法术脚本（法术ID: 72868, 72869）
 *
 * 此脚本存在的唯一原因是零售服中ICC并没有真正进入英雄模式
 * 根据副本模式选择正确的法术ID应用光环
 * - 英雄模式：72456
 * - 普通模式：70346
 */
class spell_putricide_slime_puddle_aura : public SpellScript
{
    PrepareSpellScript(spell_putricide_slime_puddle_aura);

    /**
     * @brief 替换光环
     *
     * 根据副本模式（英雄/普通）应用正确的水坑光环
     */
    void ReplaceAura()
    {
        if (Unit* target = GetHitUnit())
            GetCaster()->AddAura((GetCaster()->GetMap()->GetSpawnMode() & 1) ? 72456 : 70346, target);
    }

    void Register() override
    {
        OnHit += SpellHitFn(spell_putricide_slime_puddle_aura::ReplaceAura);
    }
};

/**
 * @class spell_putricide_unstable_experiment
 * @brief 不稳定实验法术脚本（法术ID: 70351, 71966, 71967, 71968）
 *
 * 处理普崔塞德教授的不稳定实验技能
 * 交替召唤绿色软泥和橙色气体云
 *
 * 召唤逻辑：
 * - 根据实验状态（DATA_EXPERIMENT_STAGE）决定召唤类型
 * - stage=false：绿色软泥（在绿色区域）
 * - stage=true：橙色气体云（在橙色区域）
 * - 每次施放后切换状态
 */
class spell_putricide_unstable_experiment : public SpellScript
{
    PrepareSpellScript(spell_putricide_unstable_experiment);

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引
     *
     * 找到正确的召唤位置并施放召唤法术
     * 使用暴雪的"两个科学家追踪者在绿色区域"技巧来判断位置
     */
    void HandleScript(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        if (GetCaster()->GetTypeId() != TYPEID_UNIT)
            return;

        Creature* creature = GetCaster()->ToCreature();

        // 获取当前实验状态并切换
        uint32 stage = creature->AI()->GetData(DATA_EXPERIMENT_STAGE);
        creature->AI()->SetData(DATA_EXPERIMENT_STAGE, stage ^ true);

        // 找到正确的召唤位置（科学家追踪者）
        Creature* target = nullptr;
        std::list<Creature*> creList;
        GetCreatureListWithEntryInGrid(creList, GetCaster(), NPC_ABOMINATION_WING_MAD_SCIENTIST_STALKER, 200.0f);
        // 其中两个生成在绿色区域 - 暴雪的奇怪技巧
        for (std::list<Creature*>::iterator itr = creList.begin(); itr != creList.end(); ++itr)
        {
            target = *itr;
            std::list<Creature*> tmp;
            GetCreatureListWithEntryInGrid(tmp, target, NPC_ABOMINATION_WING_MAD_SCIENTIST_STALKER, 10.0f);
            // 如果是软泥阶段（stage=0），需要附近有另一个追踪者的位置（绿色区域）
            // 如果是气体阶段（stage=1），需要单独的追踪者位置（橙色区域）
            if ((!stage && tmp.size() > 1) || (stage && tmp.size() == 1))
                break;
        }

        // 施放对应的召唤法术
        GetCaster()->CastSpell(target, uint32(GetEffectInfo(SpellEffIndex(stage)).CalcValue()), true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_putricide_unstable_experiment::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @class spell_putricide_ooze_eruption_searcher
 * @brief 软泥喷发搜索效果法术脚本（法术ID: 70459）
 *
 * 处理不稳定软泥的目标搜索和喷发机制
 * 当找到带有粘合剂的玩家时，触发软泥喷发并移除粘合剂
 */
class spell_putricide_ooze_eruption_searcher : public SpellScript
{
    PrepareSpellScript(spell_putricide_ooze_eruption_searcher);

    /**
     * @brief 处理虚拟效果
     * @param effIndex 效果索引
     *
     * 检查目标是否有不稳定软泥粘合剂
     * 如果有，移除粘合剂并对目标造成软泥喷发伤害
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        uint32 adhesiveId = sSpellMgr->GetSpellIdForDifficulty(SPELL_VOLATILE_OOZE_ADHESIVE, GetCaster());
        if (GetHitUnit()->HasAura(adhesiveId))
        {
            // 移除粘合剂
            GetHitUnit()->RemoveAurasDueToSpell(adhesiveId, GetCaster()->GetGUID(), 0, AURA_REMOVE_BY_ENEMY_SPELL);
            // 造成软泥喷发伤害
            GetCaster()->CastSpell(GetHitUnit(), SPELL_OOZE_ERUPTION, true);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_putricide_ooze_eruption_searcher::HandleDummy, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @class spell_putricide_ooze_tank_protection
 * @brief 软泥坦克保护光环脚本（法术ID: 71770）
 *
 * 保护坦克免受软泥攻击的伤害
 * 当软泥攻击时触发保护法术减少伤害
 */
class spell_putricide_ooze_tank_protection : public AuraScript
{
    PrepareAuraScript(spell_putricide_ooze_tank_protection);

    /**
     * @brief 验证法术
     * @param spellInfo 法术信息
     * @return true 验证成功
     */
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ spellInfo->GetEffect(EFFECT_0).TriggerSpell, spellInfo->GetEffect(EFFECT_1).TriggerSpell });
    }

    /**
     * @brief 处理触发
     * @param aurEff 光环效果
     * @param eventInfo 触发事件信息
     *
     * 阻止默认动作，改为施放触发的保护法术
     */
    void HandleProc(AuraEffect const* aurEff, ProcEventInfo& eventInfo)
    {
        PreventDefaultAction();

        Unit* actionTarget = eventInfo.GetActionTarget();
        actionTarget->CastSpell(nullptr, aurEff->GetSpellEffectInfo().TriggerSpell, aurEff);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_putricide_ooze_tank_protection::HandleProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
        OnEffectProc += AuraEffectProcFn(spell_putricide_ooze_tank_protection::HandleProc, EFFECT_1, SPELL_AURA_PROC_TRIGGER_SPELL);
    }
};

/**
 * @class spell_putricide_choking_gas_bomb
 * @brief 窒息毒气炸弹法术脚本（法术ID: 71255）
 *
 * 处理窒息毒气炸弹的随机效果
 * 随机跳过3个效果中的一个，只施放另外两个
 * 这创造了炸弹效果的随机性
 */
class spell_putricide_choking_gas_bomb : public SpellScript
{
    PrepareSpellScript(spell_putricide_choking_gas_bomb);

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引
     *
     * 随机选择一个效果索引跳过，施放其他两个效果对应的法术
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        uint32 skipIndex = urand(0, 2);
        for (SpellEffectInfo const& spellEffectInfo : GetSpellInfo()->GetEffects())
        {
            if (spellEffectInfo.EffectIndex == skipIndex)
                continue;

            uint32 spellId = uint32(spellEffectInfo.CalcValue());
            GetCaster()->CastSpell(GetCaster(), spellId, GetCaster()->GetGUID());
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_putricide_choking_gas_bomb::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @class spell_putricide_unbound_plague
 * @brief 无绑瘟疫搜索效果法术脚本（法术ID: 70920）
 *
 * 英雄模式专属技能
 * 玩家之间传递瘟疫的机制：
 * 1. 携带瘟疫的玩家需要传递给其他玩家
 * 2. 传递时瘟疫保留持续时间
 * 3. 传递后原玩家获得瘟疫疾病和保护效果
 *
 * 关键机制：
 * - 前两次tick不选择目标（给予初始时间）
 * - 选择没有瘟疫的玩家
 * - 转移瘟疫并保留持续时间
 */
class spell_putricide_unbound_plague : public SpellScript
{
    PrepareSpellScript(spell_putricide_unbound_plague);

    /**
     * @brief 验证法术
     * @param spell 法术信息
     * @return true 验证成功
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_UNBOUND_PLAGUE, SPELL_UNBOUND_PLAGUE_SEARCHER });
    }

    /**
     * @brief 过滤目标
     * @param targets 目标列表
     *
     * 选择传递瘟疫的目标：
     * - 前两次tick不选择（给玩家时间反应）
     * - 移除已有瘟疫的玩家
     * - 随机选择一个目标
     */
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        // 前两次tick不选择目标
        if (AuraEffect const* eff = GetCaster()->GetAuraEffect(SPELL_UNBOUND_PLAGUE_SEARCHER, EFFECT_0))
        {
            if (eff->GetTickNumber() < 2)
            {
                targets.clear();
                return;
            }
        }

        // 移除已有瘟疫的玩家
        targets.remove_if(Trinity::UnitAuraCheck(true, sSpellMgr->GetSpellIdForDifficulty(SPELL_UNBOUND_PLAGUE, GetCaster())));
        // 随机选择一个
        Trinity::Containers::RandomResize(targets, 1);
    }

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引
     *
     * 将瘟疫转移给目标玩家：
     * 1. 从原玩家移除瘟疫
     * 2. 给新玩家添加瘟疫（保留持续时间）
     * 3. 原玩家获得瘟疫疾病和保护效果
     * 4. 新玩家获得搜索效果
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (!GetHitUnit())
            return;

        InstanceScript* instance = GetCaster()->GetInstanceScript();
        if (!instance)
            return;

        uint32 plagueId = sSpellMgr->GetSpellIdForDifficulty(SPELL_UNBOUND_PLAGUE, GetCaster());

        if (!GetHitUnit()->HasAura(plagueId))
        {
            if (Creature* professor = ObjectAccessor::GetCreature(*GetCaster(), instance->GetGuidData(DATA_PROFESSOR_PUTRICIDE)))
            {
                if (Aura* oldPlague = GetCaster()->GetAura(plagueId, professor->GetGUID()))
                {
                    // 给新玩家添加瘟疫
                    if (Aura* newPlague = professor->AddAura(plagueId, GetHitUnit()))
                    {
                        // 保留原来的持续时间
                        newPlague->SetMaxDuration(oldPlague->GetMaxDuration());
                        newPlague->SetDuration(oldPlague->GetDuration());
                        oldPlague->Remove();
                        // 原玩家移除搜索效果，添加疾病和保护
                        GetCaster()->RemoveAurasDueToSpell(SPELL_UNBOUND_PLAGUE_SEARCHER);
                        GetCaster()->CastSpell(GetCaster(), SPELL_PLAGUE_SICKNESS, true);
                        GetCaster()->CastSpell(GetCaster(), SPELL_UNBOUND_PLAGUE_PROTECTION, true);
                        // 新玩家获得搜索效果
                        professor->CastSpell(GetHitUnit(), SPELL_UNBOUND_PLAGUE_SEARCHER, true);
                    }
                }
            }
        }
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_putricide_unbound_plague::FilterTargets, EFFECT_0, TARGET_UNIT_SRC_AREA_ALLY);
        OnEffectHitTarget += SpellEffectFn(spell_putricide_unbound_plague::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @class spell_putricide_eat_ooze
 * @brief 吞噬软泥法术脚本（法术ID: 70360, 72527）
 *
 * 变异憎恶的特殊技能
 * 允许憎恶吞噬淤泥水坑，减少水坑大小或完全消除
 *
 * 机制：
 * - 每次吞噬移除3层成长效果
 * - 如果成长层数小于3，水坑完全消失
 * - 否则减少3层
 */
class spell_putricide_eat_ooze : public SpellScript
{
    PrepareSpellScript(spell_putricide_eat_ooze);

    /**
     * @brief 选择目标
     * @param targets 目标列表
     *
     * 选择最近的淤泥水坑作为目标
     */
    void SelectTarget(std::list<WorldObject*>& targets)
    {
        if (targets.empty())
            return;

        // 选择最近的水坑
        targets.sort(Trinity::ObjectDistanceOrderPred(GetCaster()));
        WorldObject* target = targets.front();
        targets.clear();
        targets.push_back(target);
    }

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引
     *
     * 吞噬水坑：
     * - 如果成长层数 < 3：完全消失
     * - 否则：移除3层
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        Creature* target = GetHitCreature();
        if (!target)
            return;

        if (Aura* grow = target->GetAura(uint32(GetEffectValue())))
        {
            if (grow->GetStackAmount() < 3)
            {
                // 层数太少，完全消失
                target->RemoveAurasDueToSpell(SPELL_GROW_STACKER);
                target->RemoveAura(grow);
                target->DespawnOrUnsummon(1ms);
            }
            else
                // 移除3层
                grow->ModStackAmount(-3);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_putricide_eat_ooze::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_putricide_eat_ooze::SelectTarget, EFFECT_0, TARGET_UNIT_DEST_AREA_ENTRY);
    }
};

/**
 * @class spell_putricide_mutated_plague
 * @brief 变异瘟疫光环脚本（法术ID: 72451, 72463, 72671, 72672）
 *
 * P3阶段的主要技能
 * 周期性对全团造成伤害，伤害随堆叠层数指数增长
 *
 * 伤害计算：
 * - 基础伤害 * multiplier^stack * 1.5
 * - 普通模式：multiplier = 2.0
 * - 英雄模式：multiplier = 3.0
 *
 * 移除时治疗全团：
 * - 治疗量 = 基础治疗 * 堆叠层数
 */
class spell_putricide_mutated_plague : public AuraScript
{
    PrepareAuraScript(spell_putricide_mutated_plague);

    /**
     * @brief 处理触发法术
     * @param aurEff 光环效果
     *
     * 计算并施放伤害法术
     * 伤害随堆叠层数指数增长
     */
    void HandleTriggerSpell(AuraEffect const* aurEff)
    {
        PreventDefaultAction();
        Unit* caster = GetCaster();
        if (!caster)
            return;

        uint32 triggerSpell = aurEff->GetSpellEffectInfo().TriggerSpell;
        SpellInfo const* spell = sSpellMgr->AssertSpellInfo(triggerSpell);
        spell = sSpellMgr->GetSpellForDifficultyFromSpell(spell, caster);

        int32 damage = spell->GetEffect(EFFECT_0).CalcValue(caster);
        float multiplier = 2.0f;
        if (GetTarget()->GetMap()->GetSpawnMode() & 1)
            multiplier = 3.0f;

        // 指数增长伤害
        damage *= int32(pow(multiplier, GetStackAmount()));
        damage = int32(damage * 1.5f);

        CastSpellExtraArgs args(aurEff);
        args.OriginalCaster = GetCasterGUID();
        args.AddSpellBP0(damage);
        GetTarget()->CastSpell(GetTarget(), triggerSpell, args);
    }

    /**
     * @brief 移除时处理
     * @param aurEff 光环效果
     * @param mode 处理模式
     *
     * 光环移除时治疗全团
     * 治疗量等于基础治疗 * 堆叠层数
     */
    void OnRemove(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        uint32 healSpell = uint32(aurEff->GetSpellEffectInfo().CalcValue());
        SpellInfo const* healSpellInfo = sSpellMgr->GetSpellInfo(healSpell);

        if (!healSpellInfo)
            return;

        int32 heal = healSpellInfo->GetEffect(EFFECT_0).CalcValue() * GetStackAmount();
        CastSpellExtraArgs args(GetCasterGUID());
        args.AddSpellBP0(heal);
        GetTarget()->CastSpell(GetTarget(), healSpell, args);
    }

    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_putricide_mutated_plague::HandleTriggerSpell, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_putricide_mutated_plague::OnRemove, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @class spell_putricide_mutation_init
 * @brief 变异转化（初始化）法术脚本（法术ID: 70308）
 *
 * 处理玩家饮用药水变成变异憎恶的初始检查
 * 玩家点击桌子上的药水时触发
 *
 * 检查条件：
 * - 目标必须是玩家
 * - 必须在P1或P2阶段（P3阶段药水已用完）
 * - 不能已有憎恶存在
 */
class spell_putricide_mutation_init : public SpellScript
{
    PrepareSpellScript(spell_putricide_mutation_init);

    /**
     * @brief 内部需求检查
     * @param extendedError 扩展错误码
     * @return SpellCastResult 施法结果
     *
     * 检查是否可以变成憎恶：
     * - 教授是否存活且不在P3
     * - 是否已有憎恶存在
     */
    SpellCastResult CheckRequirementInternal(SpellCustomErrors& extendedError)
    {
        InstanceScript* instance = GetExplTargetUnit()->GetInstanceScript();
        if (!instance)
            return SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW;

        Creature* professor = ObjectAccessor::GetCreature(*GetExplTargetUnit(), instance->GetGuidData(DATA_PROFESSOR_PUTRICIDE));
        if (!professor)
            return SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW;

        // P3阶段药水已用完
        if (professor->AI()->GetData(DATA_PHASE) == PHASE_COMBAT_3 || !professor->IsAlive())
        {
            extendedError = SPELL_CUSTOM_ERROR_ALL_POTIONS_USED;
            return SPELL_FAILED_CUSTOM_ERROR;
        }

        // 只能有一个憎恶
        if (professor->AI()->GetData(DATA_ABOMINATION))
        {
            extendedError = SPELL_CUSTOM_ERROR_TOO_MANY_ABOMINATIONS;
            return SPELL_FAILED_CUSTOM_ERROR;
        }

        return SPELL_CAST_OK;
    }

    /**
     * @brief 检查需求
     * @return SpellCastResult 施法结果
     *
     * 执行检查并向玩家发送错误消息
     */
    SpellCastResult CheckRequirement()
    {
        if (!GetExplTargetUnit())
            return SPELL_FAILED_BAD_TARGETS;

        if (GetExplTargetUnit()->GetTypeId() != TYPEID_PLAYER)
            return SPELL_FAILED_TARGET_NOT_PLAYER;

        SpellCustomErrors extension = SPELL_CUSTOM_ERROR_NONE;
        SpellCastResult result = CheckRequirementInternal(extension);
        if (result != SPELL_CAST_OK)
        {
            Spell::SendCastResult(GetExplTargetUnit()->ToPlayer(), GetSpellInfo(), 0, result, extension);
            return result;
        }

        return SPELL_CAST_OK;
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_putricide_mutation_init::CheckRequirement);
    }
};

/**
 * @class spell_putricide_mutation_init_aura
 * @brief 变异转化初始化光环脚本
 *
 * 当饮用药水的引导完成后，触发实际的变异转化
 * 根据副本模式选择正确的法术ID
 */
class spell_putricide_mutation_init_aura : public AuraScript
{
    PrepareAuraScript(spell_putricide_mutation_init_aura);

    /**
     * @brief 移除时处理
     * @param aurEff 光环效果
     * @param mode 处理模式
     *
     * 引导完成后施放变异转化法术
     */
    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        uint32 spellId = 70311;
        if (GetTarget()->GetMap()->GetSpawnMode() & 1)
            spellId = 71503;

        GetTarget()->CastSpell(GetTarget(), spellId, true);
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_putricide_mutation_init_aura::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @class spell_putricide_mutated_transformation_dismiss
 * @brief 变异转化（解散）光环脚本（法术ID: 70405, 72508, 72509, 72510）
 *
 * 处理变异憎恶的解散
 * 当光环移除时，让所有乘客下车（玩家离开载具）
 */
class spell_putricide_mutated_transformation_dismiss : public AuraScript
{
    PrepareAuraScript(spell_putricide_mutated_transformation_dismiss);

    /**
     * @brief 移除时处理
     * @param aurEff 光环效果
     * @param mode 处理模式
     *
     * 移除载具上的所有乘客
     */
    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Vehicle* veh = GetTarget()->GetVehicleKit())
            veh->RemoveAllPassengers();
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_putricide_mutated_transformation_dismiss::OnRemove, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @class spell_putricide_mutated_transformation
 * @brief 变异转化法术脚本（法术ID: 70311, 71503）
 *
 * 处理玩家变成变异憎恶的过程
 * 召唤憎恶载具，让玩家骑乘，设置各种初始效果
 *
 * 主要步骤：
 * 1. 检查是否已有憎恶（双重保险）
 * 2. 召唤变异憎恶载具
 * 3. 施放初始法术（能量消耗、伤害、名称）
 * 4. 玩家进入载具
 * 5. 注册到教授的召唤列表
 */
class spell_putricide_mutated_transformation : public SpellScript
{
    PrepareSpellScript(spell_putricide_mutated_transformation);

    /**
     * @brief 处理召唤
     * @param effIndex 效果索引
     *
     * 自定义召唤逻辑，创建载具并让玩家骑乘
     */
    void HandleSummon(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        Unit* caster = GetOriginalCaster();
        if (!caster)
            return;

        InstanceScript* instance = caster->GetInstanceScript();
        if (!instance)
            return;

        Creature* putricide = ObjectAccessor::GetCreature(*caster, instance->GetGuidData(DATA_PROFESSOR_PUTRICIDE));
        if (!putricide)
            return;

        // 双重检查是否已有憎恶
        if (putricide->AI()->GetData(DATA_ABOMINATION))
        {
            if (Player* player = caster->ToPlayer())
                Spell::SendCastResult(player, GetSpellInfo(), 0, SPELL_FAILED_CUSTOM_ERROR, SPELL_CUSTOM_ERROR_TOO_MANY_ABOMINATIONS);
            return;
        }

        // 召唤变异憎恶
        uint32 entry = uint32(GetEffectInfo().MiscValue);
        SummonPropertiesEntry const* properties = sSummonPropertiesStore.LookupEntry(uint32(GetEffectInfo().MiscValueB));
        uint32 duration = uint32(GetSpellInfo()->GetDuration());

        Position pos = caster->GetPosition();
        TempSummon* summon = caster->GetMap()->SummonCreature(entry, pos, properties, duration, caster, GetSpellInfo()->Id);
        if (!summon || !summon->IsVehicle())
            return;

        // 施放初始法术
        summon->CastSpell(summon, SPELL_ABOMINATION_VEHICLE_POWER_DRAIN, true);
        summon->CastSpell(summon, SPELL_MUTATED_TRANSFORMATION_DAMAGE, true);
        caster->CastSpell(summon, SPELL_MUTATED_TRANSFORMATION_NAME, true);

        // 玩家进入载具（根据sniff使用硬编码的骑乘ID）
        caster->EnterVehicle(summon, 0);
        summon->SetCreatorGUID(caster->GetGUID());
        putricide->AI()->JustSummoned(summon);
    }

    void Register() override
    {
        OnEffectHit += SpellEffectFn(spell_putricide_mutated_transformation::HandleSummon, EFFECT_0, SPELL_EFFECT_SUMMON);
    }
};

/**
 * @class spell_putricide_mutated_transformation_dmg
 * @brief 变异转化伤害法术脚本（法术ID: 70402, 72511, 72512, 72513）
 *
 * 处理变异转化时对周围玩家造成的伤害
 * 排除载具的创建者（玩家自己）
 */
class spell_putricide_mutated_transformation_dmg : public SpellScript
{
    PrepareSpellScript(spell_putricide_mutated_transformation_dmg);

    /**
     * @brief 过滤初始目标
     * @param targets 目标列表
     *
     * 移除载具的创建者，避免伤害自己
     */
    void FilterTargetsInitial(std::list<WorldObject*>& targets)
    {
        if (Unit* owner = ObjectAccessor::GetUnit(*GetCaster(), GetCaster()->GetCreatorGUID()))
            targets.remove(owner);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_putricide_mutated_transformation_dmg::FilterTargetsInitial, EFFECT_0, TARGET_UNIT_SRC_AREA_ALLY);
    }
};

/**
 * @class spell_putricide_regurgitated_ooze
 * @brief 反刍软泥法术脚本（法术ID: 70539, 72457, 72875, 72876）
 *
 * 变异憎恶的特殊技能
 * 对目标施放减速效果
 *
 * 此钩子的唯一目的是使"反胃"成就失败
 */
class spell_putricide_regurgitated_ooze : public SpellScript
{
    PrepareSpellScript(spell_putricide_regurgitated_ooze);

    /**
     * @brief 额外效果
     * @param effIndex 效果索引
     *
     * 使"反胃"成就失败
     */
    void ExtraEffect(SpellEffIndex /*effIndex*/)
    {
        if (InstanceScript* instance = GetCaster()->GetInstanceScript())
            instance->SetData(DATA_NAUSEA_ACHIEVEMENT, uint32(false));
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_putricide_regurgitated_ooze::ExtraEffect, EFFECT_0, SPELL_EFFECT_APPLY_AURA);
    }
};

/**
 * @class spell_putricide_clear_aura_effect_value
 * @brief 清除光环效果值法术脚本
 *
 * 移除效果值中存储的光环ID
 * 法术ID:
 * - 71620: 取消催泪毒气
 * - 72618: 清除变异瘟疫
 */
class spell_putricide_clear_aura_effect_value : public SpellScript
{
    PrepareSpellScript(spell_putricide_clear_aura_effect_value);

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引
     *
     * 移除两个光环：效果值和效果1的值
     */
    void HandleScript(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        Unit* target = GetHitUnit();
        uint32 auraId = sSpellMgr->GetSpellIdForDifficulty(uint32(GetEffectValue()), GetCaster());
        target->RemoveAurasDueToSpell(auraId);
        uint32 auraId2 = GetEffectInfo(EFFECT_1).CalcValue();
        target->RemoveAurasDueToSpell(auraId2);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_putricide_clear_aura_effect_value::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @class spell_stinky_precious_decimate
 * @brief 毁灭法术脚本（法术ID: 71123）
 *
 * 臭臭和宝贝的技能（腐面和烂肠的宠物）
 * 此法术在这里是因为它被两者共用
 *
 * 将目标生命值降至指定百分比（如果高于该百分比）
 */
class spell_stinky_precious_decimate : public SpellScript
{
    PrepareSpellScript(spell_stinky_precious_decimate);

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引
     *
     * 如果目标生命值百分比高于效果值，将其设为该百分比
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (GetHitUnit()->GetHealthPct() > float(GetEffectValue()))
        {
            uint32 newHealth = GetHitUnit()->GetMaxHealth() * uint32(GetEffectValue()) / 100;
            GetHitUnit()->SetHealth(newHealth);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_stinky_precious_decimate::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @class spell_abomination_mutated_transformation
 * @brief 憎恶变异转化法术脚本（法术ID: 70402, 72511, 72512, 72513）
 *
 * 处理变异转化伤害的抗性计算
 * 憎恶受到暗影和自然双重抗性保护
 */
class spell_abomination_mutated_transformation : public SpellScript
{
    PrepareSpellScript(spell_abomination_mutated_transformation);

    /**
     * @brief 处理抗性
     * @param damageInfo 伤害信息
     * @param resistAmount 抗性量（输出参数）
     * @param absorbAmount 吸收量
     *
     * 计算暗影和自然抗性减免的伤害
     */
    void HandleResistance(DamageInfo const& damageInfo, uint32& resistAmount, int32& /*absorbAmount*/)
    {
        Unit* caster = damageInfo.GetAttacker();;
        Unit* target = damageInfo.GetVictim();
        uint32 damage = damageInfo.GetDamage();
        uint32 resistedDamage = Unit::CalcSpellResistedDamage(caster, target, damage, SPELL_SCHOOL_MASK_SHADOW, nullptr);
        resistedDamage += Unit::CalcSpellResistedDamage(caster, target, damage, SPELL_SCHOOL_MASK_NATURE, nullptr);
        resistAmount = resistedDamage;
    }

    void Register() override
    {
        OnCalculateResistAbsorb += SpellOnResistAbsorbCalculateFn(spell_abomination_mutated_transformation::HandleResistance);
    }
};

/**
 * @class spell_putricide_choking_gas_filter
 * @brief 窒息毒气过滤法术脚本
 *
 * 法术ID:
 * - 71278, 72460, 72619, 72620: 窒息毒气
 * - 71279, 72459, 72621, 72622: 窒息毒气爆炸
 *
 * 过滤载具上的玩家并移除变量标记
 */
class spell_putricide_choking_gas_filter : public SpellScript
{
    PrepareSpellScript(spell_putricide_choking_gas_filter);

    /**
     * @brief 验证法术
     * @param spell 法术信息
     * @return true 验证成功
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_OOZE_VARIABLE, SPELL_GAS_VARIABLE });
    }

    /**
     * @brief 过滤目标
     * @param targets 目标列表
     *
     * 移除在载具上的单位
     */
    void FilterTargets(std::list<WorldObject*>& targets)
    {
        targets.remove_if([](WorldObject* obj)
        {
            return obj->ToUnit() && obj->ToUnit()->GetVehicle();
        });
    }

    /**
     * @brief 处理驱散
     * @param effIndex 效果索引
     *
     * 移除软泥和气体变量标记
     */
    void HandleDispel(SpellEffIndex /*effIndex*/)
    {
        Unit* target = GetHitUnit();
        target->RemoveAurasDueToSpell(SPELL_OOZE_VARIABLE);
        target->RemoveAurasDueToSpell(SPELL_GAS_VARIABLE);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_putricide_choking_gas_filter::FilterTargets, EFFECT_ALL, TARGET_UNIT_SRC_AREA_ENTRY);
        OnEffectHitTarget += SpellEffectFn(spell_putricide_choking_gas_filter::HandleDispel, EFFECT_1, SPELL_EFFECT_APPLY_AURA);
    }
};

/**
 * @brief 添加普崔塞德教授脚本
 *
 * 注册所有生物AI和法术脚本
 *
 * 注册内容：
 * - 生物AI：普崔塞德教授、不稳定软泥、气体云
 * - 法术脚本：处理各种技能效果
 */
void AddSC_boss_professor_putricide()
{
    // Creatures - 生物AI注册
    RegisterIcecrownCitadelCreatureAI(boss_professor_putricide);
    RegisterIcecrownCitadelCreatureAI(npc_volatile_ooze);
    RegisterIcecrownCitadelCreatureAI(npc_gas_cloud);

    // Spells - 法术脚本注册
    RegisterSpellScript(spell_putricide_gaseous_bloat);
    RegisterSpellScript(spell_putricide_ooze_channel);
    RegisterSpellScript(spell_putricide_slime_puddle);
    RegisterSpellScript(spell_putricide_slime_puddle_aura);
    RegisterSpellScript(spell_putricide_unstable_experiment);
    RegisterSpellScript(spell_putricide_ooze_eruption_searcher);
    RegisterSpellScript(spell_putricide_ooze_tank_protection);
    RegisterSpellScript(spell_putricide_choking_gas_bomb);
    RegisterSpellScript(spell_putricide_unbound_plague);
    RegisterSpellScript(spell_putricide_eat_ooze);
    RegisterSpellScript(spell_putricide_mutated_plague);
    RegisterSpellAndAuraScriptPair(spell_putricide_mutation_init, spell_putricide_mutation_init_aura);
    RegisterSpellScript(spell_putricide_mutated_transformation_dismiss);
    RegisterSpellScript(spell_putricide_mutated_transformation);
    RegisterSpellScript(spell_putricide_mutated_transformation_dmg);
    RegisterSpellScript(spell_putricide_regurgitated_ooze);
    RegisterSpellScript(spell_putricide_clear_aura_effect_value);
    RegisterSpellScript(spell_stinky_precious_decimate);
    RegisterSpellScript(spell_abomination_mutated_transformation);
    RegisterSpellScript(spell_putricide_choking_gas_filter);
}
