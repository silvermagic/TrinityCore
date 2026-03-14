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
 * @file boss_lord_marrowgar.cpp
 * @brief 冰冠堡垒第一个首领 - 玛洛加尔领主的战斗脚本
 *
 * 本模块实现了冰冠堡垒第一个首领玛洛加尔领主的完整战斗逻辑。
 *
 * 战斗机制概述：
 * 1. 白骨风暴：BOSS旋转移动，对周围玩家造成伤害，期间施放冷焰
 * 2. 白骨尖刺墓地：随机穿刺玩家，需要团队击杀尖刺解救
 * 3. 冷焰：对目标方向喷射火焰，造成持续伤害
 * 4. 白骨劈砍：替换普通攻击，对多个目标造成伤害并分摊伤害
 *
 * 英雄模式差异：
 * - 白骨风暴期间也会施放白骨尖刺墓地
 * - 白骨风暴持续时间更长
 */

#include "icecrown_citadel.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSplineInit.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PointMovementGenerator.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

/**
 * @brief 首领台词文本ID枚举
 *
 * 定义玛洛加尔领主战斗中的各种台词和表情文本
 */
enum ScriptTexts
{
    SAY_ENTER_ZONE              = 0,  ///< 进入区域时的台词
    SAY_AGGRO                   = 1,  ///< 开怪台词
    SAY_BONE_STORM              = 2,  ///< 白骨风暴台词
    SAY_BONESPIKE               = 3,  ///< 白骨尖刺台词
    SAY_KILL                    = 4,  ///< 击杀玩家台词
    SAY_DEATH                   = 5,  ///< 死亡台词
    SAY_BERSERK                 = 6,  ///< 狂暴台词
    EMOTE_BONE_STORM            = 7,  ///< 白骨风暴表情提示
};

/**
 * @brief 法术ID枚举
 *
 * 定义战斗中使用的所有法术ID
 */
enum Spells
{
    // Lord Marrowgar - 玛洛加尔领主
    SPELL_BONE_SLICE            = 69055,  ///< 白骨劈砍 - 替换普通攻击，对多个目标分摊伤害
    SPELL_BONE_STORM            = 69076,  ///< 白骨风暴 - BOSS旋转移动并造成AOE伤害
    SPELL_BONE_SPIKE_GRAVEYARD  = 69057,  ///< 白骨尖刺墓地 - 穿刺随机玩家
    SPELL_COLDFLAME_NORMAL      = 69140,  ///< 冷焰（正常阶段）- 向目标方向喷射
    SPELL_COLDFLAME_BONE_STORM  = 72705,  ///< 冷焰（白骨风暴阶段）- 向四个方向喷射

    // Bone Spike - 白骨尖刺
    SPELL_IMPALED               = 69065,  ///< 穿刺 - 被尖刺击中的玩家获得此减益
    SPELL_RIDE_VEHICLE          = 46598,  ///< 乘坐载具 - 让玩家骑乘在尖刺上

    // Coldflame - 冷焰
    SPELL_COLDFLAME_PASSIVE     = 69145,  ///< 冷焰被动 - 冷焰区域的伤害光环
    SPELL_COLDFLAME_SUMMON      = 69147,  ///< 召唤冷焰 - 生成冷焰区域
};

/**
 * @brief 白骨尖刺召唤法术ID数组
 *
 * 根据难度模式，不同模式的尖刺使用不同的法术ID
 */
uint32 const BoneSpikeSummonId[3] = {69062, 72669, 72670};

/**
 * @brief 事件ID枚举
 *
 * 定义战斗中的各种计时器事件
 */
enum Events
{
    EVENT_BONE_SPIKE_GRAVEYARD  = 1,  ///< 白骨尖刺墓地事件
    EVENT_COLDFLAME             = 2,  ///< 冷焰事件
    EVENT_BONE_STORM_BEGIN      = 3,  ///< 白骨风暴开始事件
    EVENT_BONE_STORM_MOVE       = 4,  ///< 白骨风暴移动事件
    EVENT_BONE_STORM_END        = 5,  ///< 白骨风暴结束事件
    EVENT_ENABLE_BONE_SLICE     = 6,  ///< 启用白骨劈砍事件
    EVENT_ENRAGE                = 7,  ///< 狂暴事件
    EVENT_WARN_BONE_STORM       = 8,  ///< 白骨风暴预警事件

    EVENT_COLDFLAME_TRIGGER     = 9,  ///< 冷焰触发事件（用于冷焰移动）
    EVENT_FAIL_BONED            = 10, ///< 成就失败事件（尖刺未被及时击杀）

    EVENT_GROUP_SPECIAL         = 1,  ///< 特殊事件组（用于事件延迟）
};

/**
 * @brief 移动路径点枚举
 *
 * 定义BOSS移动的目标点ID
 */
enum MovementPoints
{
    POINT_TARGET_BONESTORM_PLAYER   = 36612631,  ///< 白骨风暴目标玩家位置
    POINT_TARGET_COLDFLAME          = 36672631,  ///< 冷焰目标位置
};

/**
 * @brief 杂项数据枚举
 *
 * 定义用于数据传递的各种常量
 */
enum MiscInfo
{
    DATA_COLDFLAME_GUID             = 0,  ///< 冷焰目标GUID数据

    // 手动标记被白骨劈砍击中的目标，因为没有光环用于此目的
    // 这些单位是战斗中的坦克，应该对白骨尖刺墓地免疫
    DATA_SPIKE_IMMUNE               = 1,
    //DATA_SPIKE_IMMUNE_1,          = 2, // 已保留并使用
    //DATA_SPIKE_IMMUNE_2,          = 3, // 已保留并使用

    MAX_BONE_SPIKE_IMMUNE           = 3,  ///< 最大白骨尖刺免疫目标数（最多3个坦克）
};

/**
 * @brief 动作ID枚举
 *
 * 定义用于通信的动作类型
 */
enum Actions
{
    ACTION_CLEAR_SPIKE_IMMUNITIES = 1,  ///< 清除尖刺免疫列表
    ACTION_TALK_ENTER_ZONE        = 2   ///< 播放进入区域台词
};

/**
 * @class BoneSpikeTargetSelector
 * @brief 白骨尖刺目标选择器函数对象
 *
 * 用于从潜在目标列表中选择有效的白骨尖刺目标。
 * 排除已被穿刺的玩家和正在承受白骨劈砍的坦克。
 */
class BoneSpikeTargetSelector
{
    public:
        /**
         * @brief 构造函数
         * @param ai AI指针，用于获取免疫目标列表
         */
        BoneSpikeTargetSelector(UnitAI* ai) : _ai(ai) { }

        /**
         * @brief 判断单位是否可以作为白骨尖刺目标
         * @param unit 待检测的单位
         * @return true 如果单位是有效目标，false 否则
         *
         * 排除规则：
         * 1. 非玩家单位
         * 2. 已被穿刺的玩家
         * 3. 正在承受白骨劈砍的坦克
         */
        bool operator()(Unit* unit) const
        {
            // 只选择玩家目标
            if (unit->GetTypeId() != TYPEID_PLAYER)
                return false;

            // 排除已被穿刺的玩家
            if (unit->HasAura(SPELL_IMPALED))
                return false;

            // 检查是否为正在承受白骨劈砍的坦克
            for (uint32 i = 0; i < MAX_BONE_SPIKE_IMMUNE; ++i)
                if (unit->GetGUID() == _ai->GetGUID(DATA_SPIKE_IMMUNE + i))
                    return false;

            return true;
        }

    private:
        UnitAI* _ai;  ///< AI指针，用于查询免疫目标GUID
};

/**
 * @struct boss_lord_marrowgar
 * @brief 玛洛加尔领主AI
 *
 * 实现玛洛加尔领主的完整战斗逻辑，包括：
 * - 白骨风暴阶段管理
 * - 白骨尖刺墓地的施放和管理
 * - 冷焰的方向性喷射
 * - 白骨劈砍的分摊伤害机制
 */
struct boss_lord_marrowgar : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化BOSS的基本属性，包括白骨风暴持续时间和基础移动速度
     */
    boss_lord_marrowgar(Creature* creature) : BossAI(creature, DATA_LORD_MARROWGAR)
    {
        // 白骨风暴持续时间：10人普通20秒，25人普通30秒，10人英雄20秒，25人英雄30秒
        _boneStormDuration = RAID_MODE(20s, 30s, 20s, 30s);
        _baseSpeed = creature->GetSpeedRate(MOVE_RUN);
        _coldflameLastPos.Relocate(creature);
        _boneSlice = false;
    }

    /**
     * @brief 重置函数
     *
     * 当BOSS脱离战斗或被重置时调用，恢复所有状态并初始化事件调度
     *
     * 调用时机：
     * - BOSS脱离战斗
     * - 团队灭团
     * - 手动重置实例
     */
    void Reset() override
    {
        _Reset();
        // 恢复基础移动速度
        me->SetSpeedRate(MOVE_RUN, _baseSpeed);
        // 移除白骨风暴和狂暴光环
        me->RemoveAurasDueToSpell(SPELL_BONE_STORM);
        me->RemoveAurasDueToSpell(SPELL_BERSERK);
        // 事件调度：10秒后启用白骨劈砍
        events.ScheduleEvent(EVENT_ENABLE_BONE_SLICE, 10s);
        // 事件调度：15秒后施放白骨尖刺墓地（特殊事件组）
        events.ScheduleEvent(EVENT_BONE_SPIKE_GRAVEYARD, 15s, EVENT_GROUP_SPECIAL);
        // 事件调度：5秒后施放冷焰（特殊事件组）
        events.ScheduleEvent(EVENT_COLDFLAME, 5s, EVENT_GROUP_SPECIAL);
        // 事件调度：45-50秒后预警白骨风暴
        events.ScheduleEvent(EVENT_WARN_BONE_STORM, 45s, 50s);
        // 事件调度：10分钟后狂暴
        events.ScheduleEvent(EVENT_ENRAGE, 10min);
        _boneSlice = false;
        _boneSpikeImmune.clear();
    }

    /**
     * @brief 进入战斗函数
     * @param who 触发战斗的单位
     *
     * 当BOSS进入战斗状态时调用
     *
     * 调用时机：BOSS被玩家攻击或主动攻击玩家
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        Talk(SAY_AGGRO);

        me->setActive(true);
        DoZoneInCombat();
        instance->SetBossState(DATA_LORD_MARROWGAR, IN_PROGRESS);
    }

    /**
     * @brief 死亡函数
     * @param killer 击杀者
     *
     * 当BOSS死亡时调用，处理战利品和成就
     *
     * 调用时机：BOSS生命值降为0
     */
    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_DEATH);

        _JustDied();
    }

    /**
     * @brief 返回出生点函数
     *
     * 当BOSS脱离战斗返回出生点时调用
     *
     * 调用时机：BOSS脱战后返回初始位置
     */
    void JustReachedHome() override
    {
        _JustReachedHome();
        instance->SetBossState(DATA_LORD_MARROWGAR, FAIL);
        instance->SetData(DATA_BONED_ACHIEVEMENT, uint32(true));    // 重置成就状态
    }

    /**
     * @brief 击杀单位函数
     * @param victim 被击杀的单位
     *
     * 当BOSS击杀玩家时调用，播放击杀台词
     *
     * 调用时机：BOSS击杀玩家
     */
    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_KILL);
    }

    /**
     * @brief 更新AI函数
     * @param diff 时间差（毫秒）
     *
     * 每个游戏循环调用，处理事件调度和战斗逻辑
     *
     * 性能注意事项：此函数每帧调用，避免复杂计算
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        events.Update(diff);

        // 如果正在施法，不处理事件
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_BONE_SPIKE_GRAVEYARD:
                    // 英雄模式下白骨风暴期间也施放尖刺，普通模式只在非风暴期间施放
                    if (IsHeroic() || !me->HasAura(SPELL_BONE_STORM))
                        DoCast(me, SPELL_BONE_SPIKE_GRAVEYARD);
                    events.ScheduleEvent(EVENT_BONE_SPIKE_GRAVEYARD, 15s, 20s, EVENT_GROUP_SPECIAL);
                    break;
                case EVENT_COLDFLAME:
                    // 记录当前冷焰位置
                    _coldflameLastPos.Relocate(me);
                    _coldflameTarget.Clear();
                    // 根据是否在白骨风暴中选择不同的冷焰法术
                    if (!me->HasAura(SPELL_BONE_STORM))
                        DoCastAOE(SPELL_COLDFLAME_NORMAL);
                    else
                        DoCast(me, SPELL_COLDFLAME_BONE_STORM);
                    events.ScheduleEvent(EVENT_COLDFLAME, 5s, EVENT_GROUP_SPECIAL);
                    break;
                case EVENT_WARN_BONE_STORM:
                    // 白骨风暴预警阶段
                    _boneSlice = false;
                    Talk(EMOTE_BONE_STORM);
                    // 取消当前近战法术
                    me->FinishSpell(CURRENT_MELEE_SPELL, false);
                    DoCast(me, SPELL_BONE_STORM);
                    // 延迟特殊事件组3秒
                    events.DelayEvents(3s, EVENT_GROUP_SPECIAL);
                    events.ScheduleEvent(EVENT_BONE_STORM_BEGIN, 3050ms);
                    events.ScheduleEvent(EVENT_WARN_BONE_STORM, 90s, 95s);
                    break;
                case EVENT_BONE_STORM_BEGIN:
                    // 白骨风暴正式开始
                    if (Aura* pStorm = me->GetAura(SPELL_BONE_STORM))
                        pStorm->SetDuration(int32(_boneStormDuration.count()));
                    // 白骨风暴期间移动速度提升3倍
                    me->SetSpeedRate(MOVE_RUN, _baseSpeed*3.0f);
                    Talk(SAY_BONE_STORM);
                    events.ScheduleEvent(EVENT_BONE_STORM_END, _boneStormDuration + 1ms);
                    [[fallthrough]];
                case EVENT_BONE_STORM_MOVE:
                {
                    // 白骨风暴期间定期移动到随机玩家位置
                    events.ScheduleEvent(EVENT_BONE_STORM_MOVE, _boneStormDuration/3);
                    // 优先选择非坦克目标
                    Unit* unit = SelectTarget(SelectTargetMethod::Random, 0, NonTankTargetSelector(me));
                    if (!unit)
                        unit = SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true);
                    if (unit)
                        me->GetMotionMaster()->MovePoint(POINT_TARGET_BONESTORM_PLAYER, *unit);
                    break;
                }
                case EVENT_BONE_STORM_END:
                    // 白骨风暴结束，恢复正常战斗状态
                    // 移除白骨风暴移动生成器
                    if (MovementGenerator* movement = me->GetMotionMaster()->GetMovementGenerator([](MovementGenerator const* a) -> bool
                    {
                        if (a->GetMovementGeneratorType() == POINT_MOTION_TYPE)
                        {
                            PointMovementGenerator<Creature> const* pointMovement = dynamic_cast<PointMovementGenerator<Creature> const*>(a);
                            return pointMovement && pointMovement->GetId() == POINT_TARGET_BONESTORM_PLAYER;
                        }
                        return false;
                    }))
                        me->GetMotionMaster()->Remove(movement);
                    // 恢复追逐当前目标
                    me->GetMotionMaster()->MoveChase(me->GetVictim());
                    // 恢复基础移动速度
                    me->SetSpeedRate(MOVE_RUN, _baseSpeed);
                    events.CancelEvent(EVENT_BONE_STORM_MOVE);
                    events.ScheduleEvent(EVENT_ENABLE_BONE_SLICE, 10s);
                    // 非英雄模式下重新调度白骨尖刺墓地
                    if (!IsHeroic())
                        events.RescheduleEvent(EVENT_BONE_SPIKE_GRAVEYARD, 15s, EVENT_GROUP_SPECIAL);
                    break;
                case EVENT_ENABLE_BONE_SLICE:
                    // 启用白骨劈砍替换普通攻击
                    _boneSlice = true;
                    break;
                case EVENT_ENRAGE:
                    // 10分钟狂暴
                    DoCast(me, SPELL_BERSERK, true);
                    Talk(SAY_BERSERK);
                    break;
            }

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 白骨风暴期间不进行近战攻击
        if (me->HasAura(SPELL_BONE_STORM))
            return;

        // 战斗开始10秒后，白骨劈砍替换普通攻击
        if (_boneSlice && !me->GetCurrentSpell(CURRENT_MELEE_SPELL))
            DoCastVictim(SPELL_BONE_SLICE);

        DoMeleeAttackIfReady();
    }

    /**
     * @brief 移动通知函数
     * @param type 移动类型
     * @param id 移动点ID
     *
     * 当BOSS到达目标点时调用
     *
     * 调用时机：BOSS完成移动
     */
    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE || id != POINT_TARGET_BONESTORM_PLAYER)
            return;

        // 锁定移动，等待下一个移动指令
        me->GetMotionMaster()->MoveIdle();
    }

    /**
     * @brief 获取最后冷焰位置
     * @return 最后冷焰位置的常量指针
     *
     * 用于冷焰AI确定冷焰的起始位置
     */
    Position const* GetLastColdflamePosition() const
    {
        return &_coldflameLastPos;
    }

    /**
     * @brief 获取GUID数据
     * @param type 数据类型
     * @return 对应的GUID，如果不存在返回空GUID
     *
     * 用于其他AI查询特定数据
     */
    ObjectGuid GetGUID(int32 type /*= 0 */) const override
    {
        switch (type)
        {
            case DATA_COLDFLAME_GUID:
                return _coldflameTarget;
            case DATA_SPIKE_IMMUNE + 0:
            case DATA_SPIKE_IMMUNE + 1:
            case DATA_SPIKE_IMMUNE + 2:
            {
                uint32 index = uint32(type - DATA_SPIKE_IMMUNE);
                if (index < _boneSpikeImmune.size())
                    return _boneSpikeImmune[index];

                break;
            }
        }

        return ObjectGuid::Empty;
    }

    /**
     * @brief 设置GUID数据
     * @param guid 要设置的GUID
     * @param id 数据类型ID
     *
     * 用于其他AI传递数据给本AI
     */
    void SetGUID(ObjectGuid const& guid, int32 id) override
    {
        switch (id)
        {
            case DATA_COLDFLAME_GUID:
                _coldflameTarget = guid;
                break;
            case DATA_SPIKE_IMMUNE:
                _boneSpikeImmune.push_back(guid);
                break;
        }
    }

    /**
     * @brief 执行动作
     * @param action 动作ID
     *
     * 用于接收外部AI的动作指令
     */
    void DoAction(int32 action) override
    {
        switch (action)
        {
            case ACTION_CLEAR_SPIKE_IMMUNITIES:
                _boneSpikeImmune.clear();
                break;
            case ACTION_TALK_ENTER_ZONE:
                if (me->IsAlive())
                    Talk(SAY_ENTER_ZONE);
                break;
            default:
                break;
        }
    }

private:
    Position _coldflameLastPos;          ///< 最后冷焰施放位置
    GuidVector _boneSpikeImmune;         ///< 白骨尖刺免疫目标列表（被白骨劈砍击中的坦克）
    ObjectGuid _coldflameTarget;         ///< 冷焰目标GUID
    Milliseconds _boneStormDuration;     ///< 白骨风暴持续时间
    float _baseSpeed;                    ///< 基础移动速度
    bool _boneSlice;                     ///< 是否启用白骨劈砍
};

typedef boss_lord_marrowgar MarrowgarAI;

/**
 * @struct npc_coldflame
 * @brief 冷焰AI
 *
 * 实现冷焰的移动和伤害逻辑。冷焰会在地上持续移动，
 * 玩家站在其中会受到持续伤害。
 *
 * 工作原理：
 * 1. 被召唤时确定初始位置和方向
 * 2. 每500毫秒向前移动5码并施放新的冷焰区域
 * 3. 持续移动直到被清理
 */
struct npc_coldflame : public ScriptedAI
{
    npc_coldflame(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 被召唤时调用
     * @param ownerWO 召唤者
     *
     * 初始化冷焰的位置和移动方向
     *
     * 调用时机：冷焰生物被召唤
     */
    void IsSummonedBy(WorldObject* ownerWO) override
    {
        Creature* owner = ownerWO->ToCreature();
        if (!owner)
            return;

        Position pos;
        // 获取玛洛加尔领主AI中的最后冷焰位置
        if (MarrowgarAI* marrowgarAI = CAST_AI(MarrowgarAI, owner->GetAI()))
            pos.Relocate(marrowgarAI->GetLastColdflamePosition());
        else
            pos.Relocate(owner);

        // 白骨风暴期间的冷焰处理
        if (owner->HasAura(SPELL_BONE_STORM))
        {
            // 面向冷焰生物的方向
            float ang = pos.GetAbsoluteAngle(me);
            me->SetOrientation(ang);
            owner->GetNearPoint2D(nullptr, pos.m_positionX, pos.m_positionY, 5.0f - owner->GetCombatReach(), ang);
        }
        else
        {
            // 正常阶段，获取目标玩家位置
            Player* target = ObjectAccessor::GetPlayer(*owner, owner->GetAI()->GetGUID(DATA_COLDFLAME_GUID));
            if (!target)
            {
                me->DespawnOrUnsummon();
                return;
            }

            float ang = pos.GetAbsoluteAngle(target);
            me->SetOrientation(ang);
            owner->GetNearPoint2D(nullptr, pos.m_positionX, pos.m_positionY, 15.0f - owner->GetCombatReach(), ang);
        }

        // 传送到计算的位置并开始生成冷焰区域
        me->NearTeleportTo(pos.GetPositionX(), pos.GetPositionY(), me->GetPositionZ(), me->GetOrientation());
        DoCast(SPELL_COLDFLAME_SUMMON);
        _events.ScheduleEvent(EVENT_COLDFLAME_TRIGGER, 500ms);
    }

    /**
     * @brief 更新AI
     * @param diff 时间差（毫秒）
     *
     * 处理冷焰的定期移动和区域生成
     */
    void UpdateAI(uint32 diff) override
    {
        _events.Update(diff);

        if (_events.ExecuteEvent() == EVENT_COLDFLAME_TRIGGER)
        {
            // 向前方移动5码
            Position newPos = me->GetNearPosition(5.0f, 0.0f);
            me->NearTeleportTo(newPos.GetPositionX(), newPos.GetPositionY(), me->GetPositionZ(), me->GetOrientation());
            // 在新位置生成冷焰区域
            DoCast(SPELL_COLDFLAME_SUMMON);
            _events.ScheduleEvent(EVENT_COLDFLAME_TRIGGER, 500ms);
        }
    }

private:
    EventMap _events;  ///< 事件映射表
};

/**
 * @struct npc_bone_spike
 * @brief 白骨尖刺AI
 *
 * 实现白骨尖刺的穿刺逻辑。白骨尖刺会穿刺随机玩家，
 * 团队需要击杀尖刺才能解救被穿刺的玩家。
 *
 * 工作原理：
 * 1. 被召唤时穿刺目标玩家
 * 2. 玩家骑乘在尖刺上
 * 3. 尖刺被击杀时释放玩家
 * 4. 如果8秒内未被击杀，失败成就条件
 */
struct npc_bone_spike : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_bone_spike(Creature* creature) : ScriptedAI(creature), _hasTrappedUnit(false)
    {
        ASSERT(creature->GetVehicleKit());

        SetCombatMovement(false);
    }

    /**
     * @brief 死亡时调用
     * @param killer 击杀者
     *
     * 尖刺死亡时释放被穿刺的玩家
     */
    void JustDied(Unit* /*killer*/) override
    {
        if (TempSummon* summ = me->ToTempSummon())
            if (Unit* trapped = summ->GetSummonerUnit())
                trapped->RemoveAurasDueToSpell(SPELL_IMPALED);

        me->DespawnOrUnsummon();
    }

    /**
     * @brief 击杀单位时调用
     * @param victim 被击杀的单位
     *
     * 尖刺击杀玩家时消失并清除穿刺效果
     */
    void KilledUnit(Unit* victim) override
    {
        me->DespawnOrUnsummon();
        victim->RemoveAurasDueToSpell(SPELL_IMPALED);
    }

    /**
     * @brief 被召唤时调用
     * @param summonerWO 召唤者
     *
     * 初始化穿刺效果
     *
     * 调用时机：白骨尖刺被召唤
     */
    void IsSummonedBy(WorldObject* summonerWO) override
    {
        Unit* summoner = summonerWO->ToUnit();
        if (!summoner)
            return;
        // 对召唤者施放穿刺效果
        DoCast(summoner, SPELL_IMPALED);
        // 让召唤者骑乘在尖刺上
        summoner->CastSpell(me, SPELL_RIDE_VEHICLE, true);
        // 8秒后触发成就失败事件
        _events.ScheduleEvent(EVENT_FAIL_BONED, 8s);
        _hasTrappedUnit = true;
    }

    /**
     * @brief 乘客登载时调用
     * @param passenger 乘客
     * @param seat 座位ID
     * @param apply 是否应用
     *
     * 调整被穿刺玩家的位置偏移
     */
    void PassengerBoarded(Unit* passenger, int8 /*seat*/, bool apply) override
    {
        if (!apply)
            return;

        /// @HACK - 修改乘客偏移为从嗅探数据中直接获取的值
        /// 当实现正确的计算后移除此代码
        /// 这修复了对被穿刺玩家的治疗
        std::function<void(Movement::MoveSplineInit&)> initializer = [](Movement::MoveSplineInit& init)
        {
            init.DisableTransportPathTransformations();
            init.MoveTo(-0.02206125f, -0.02132235f, 5.514783f, false);
        };
        passenger->GetMotionMaster()->LaunchMoveSpline(std::move(initializer), EVENT_VEHICLE_BOARD, MOTION_PRIORITY_HIGHEST);
    }

    /**
     * @brief 更新AI
     * @param diff 时间差（毫秒）
     *
     * 检查成就失败条件
     */
    void UpdateAI(uint32 diff) override
    {
        if (!_hasTrappedUnit)
            return;

        _events.Update(diff);

        // 8秒内未被击杀，触发成就失败
        if (_events.ExecuteEvent() == EVENT_FAIL_BONED)
            if (InstanceScript* instance = me->GetInstanceScript())
                instance->SetData(DATA_BONED_ACHIEVEMENT, uint32(false));
    }

private:
    EventMap _events;       ///< 事件映射表
    bool _hasTrappedUnit;   ///< 是否成功捕获了单位
};

// 69140 - Coldflame
class spell_marrowgar_coldflame : public SpellScript
{
    PrepareSpellScript(spell_marrowgar_coldflame);

    void SelectTarget(std::list<WorldObject*>& targets)
    {
        targets.clear();
        // select any unit but not the tank
        Unit* target = GetCaster()->GetAI()->SelectTarget(SelectTargetMethod::Random, 0, -GetCaster()->GetCombatReach(), true, false, -SPELL_IMPALED);
        if (!target)
            target = GetCaster()->GetAI()->SelectTarget(SelectTargetMethod::Random, 0, 0.0f, true); // or the tank if its solo
        if (!target)
            return;

        GetCaster()->GetAI()->SetGUID(target->GetGUID(), DATA_COLDFLAME_GUID);
        targets.push_back(target);
    }

    void HandleScriptEffect(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        GetCaster()->CastSpell(GetHitUnit(), uint32(GetEffectValue()), true);
    }

    void Register() override
    {
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_marrowgar_coldflame::SelectTarget, EFFECT_0, TARGET_UNIT_DEST_AREA_ENEMY);
        OnEffectHitTarget += SpellEffectFn(spell_marrowgar_coldflame::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 72705 - Coldflame (Bonestorm)
class spell_marrowgar_coldflame_bonestorm : public SpellScript
{
    PrepareSpellScript(spell_marrowgar_coldflame_bonestorm);

    void HandleScriptEffect(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        for (uint8 i = 0; i < 4; ++i)
            GetCaster()->CastSpell(GetHitUnit(), uint32(GetEffectValue() + i), true);
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_marrowgar_coldflame_bonestorm::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

// 69146, 70823, 70824, 70825 - Coldflame (Damage)
class spell_marrowgar_coldflame_damage : public AuraScript
{
    PrepareAuraScript(spell_marrowgar_coldflame_damage);

    bool CanBeAppliedOn(Unit* target)
    {
        if (target->HasAura(SPELL_IMPALED))
            return false;

        if (target->GetExactDist2d(GetOwner()) > GetEffectInfo(EFFECT_0).CalcRadius())
            return false;

        if (Aura* aur = target->GetAura(GetId()))
            if (aur->GetOwner() != GetOwner())
                return false;

        return true;
    }

    void Register() override
    {
        DoCheckAreaTarget += AuraCheckAreaTargetFn(spell_marrowgar_coldflame_damage::CanBeAppliedOn);
    }
};

// 69057, 70826, 72088, 72089 - Bone Spike Graveyard
class spell_marrowgar_bone_spike_graveyard : public SpellScript
{
    PrepareSpellScript(spell_marrowgar_bone_spike_graveyard);

    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(BoneSpikeSummonId);
    }

    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_UNIT && GetCaster()->IsAIEnabled();
    }

    SpellCastResult CheckCast()
    {
        return GetCaster()->GetAI()->SelectTarget(SelectTargetMethod::Random, 0, BoneSpikeTargetSelector(GetCaster()->GetAI())) ? SPELL_CAST_OK : SPELL_FAILED_NO_VALID_TARGETS;
    }

    void HandleSpikes(SpellEffIndex effIndex)
    {
        PreventHitDefaultEffect(effIndex);
        if (Creature* marrowgar = GetCaster()->ToCreature())
        {
            CreatureAI* marrowgarAI = marrowgar->AI();
            uint8 boneSpikeCount = uint8(GetCaster()->GetMap()->GetSpawnMode() & 1 ? 3 : 1);

            std::list<Unit*> targets;
            marrowgarAI->SelectTargetList(targets, boneSpikeCount, SelectTargetMethod::Random, 1, BoneSpikeTargetSelector(marrowgarAI));
            if (targets.empty())
                return;

            uint32 i = 0;
            for (std::list<Unit*>::const_iterator itr = targets.begin(); itr != targets.end(); ++itr, ++i)
            {
                Unit* target = *itr;
                target->CastSpell(target, BoneSpikeSummonId[i], true);
                if (!target->IsAlive()) // make sure we don't get any stuck spikes on dead targets
                {
                    if (Aura* aura = target->GetAura(SPELL_IMPALED))
                    {
                        if (Creature* spike = ObjectAccessor::GetCreature(*target, aura->GetCasterGUID()))
                            spike->DespawnOrUnsummon();
                        aura->Remove();
                    }
                }
            }

            marrowgarAI->Talk(SAY_BONESPIKE);
        }
    }

    void Register() override
    {
        OnCheckCast += SpellCheckCastFn(spell_marrowgar_bone_spike_graveyard::CheckCast);
        OnEffectHitTarget += SpellEffectFn(spell_marrowgar_bone_spike_graveyard::HandleSpikes, EFFECT_1, SPELL_EFFECT_APPLY_AURA);
    }
};

// 69075, 70834, 70835, 70836 - Bone Storm
class spell_marrowgar_bone_storm : public SpellScript
{
    PrepareSpellScript(spell_marrowgar_bone_storm);

    void RecalculateDamage()
    {
        SetHitDamage(int32(GetHitDamage() / std::max(std::sqrt(GetHitUnit()->GetExactDist2d(GetCaster())), 1.0f)));
    }

    void Register() override
    {
        OnHit += SpellHitFn(spell_marrowgar_bone_storm::RecalculateDamage);
    }
};

// 69055, 70814 - Bone Slice
/**
 * @class spell_marrowgar_bone_slice
 * @brief 白骨劈砍法术脚本
 *
 * 处理白骨劈砍的伤害分摊机制，并标记被击中的坦克
 */
class spell_marrowgar_bone_slice : public SpellScript
{
    PrepareSpellScript(spell_marrowgar_bone_slice);

public:
    spell_marrowgar_bone_slice()
    {
        _targetCount = 0;
    }

private:
    /**
     * @brief 清除尖刺免疫列表
     *
     * 每次施放白骨劈砍前清除之前的免疫列表
     */
    void ClearSpikeImmunities()
    {
        GetCaster()->GetAI()->DoAction(ACTION_CLEAR_SPIKE_IMMUNITIES);
    }

    /**
     * @brief 计算目标数量
     * @param targets 目标列表
     *
     * 记录实际被击中的目标数量，用于伤害分摊计算
     */
    void CountTargets(std::list<WorldObject*>& targets)
    {
        _targetCount = std::min<uint32>(targets.size(), GetSpellInfo()->MaxAffectedTargets);
    }

    /**
     * @brief 分摊伤害
     *
     * 将总伤害分摊到所有目标上，并标记目标为尖刺免疫
     */
    void SplitDamage()
    {
        // 标记单位已被击中，即使法术未命中或被躲闪/招架
        GetCaster()->GetAI()->SetGUID(GetHitUnit()->GetGUID(), DATA_SPIKE_IMMUNE);

        if (!_targetCount)
            return; // 这个法术可能未命中所有目标

        SetHitDamage(GetHitDamage() / _targetCount);
    }

    void Register() override
    {
        BeforeCast += SpellCastFn(spell_marrowgar_bone_slice::ClearSpikeImmunities);
        OnObjectAreaTargetSelect += SpellObjectAreaTargetSelectFn(spell_marrowgar_bone_slice::CountTargets, EFFECT_0, TARGET_UNIT_DEST_AREA_ENEMY);
        OnHit += SpellHitFn(spell_marrowgar_bone_slice::SplitDamage);
    }

    uint32 _targetCount;  ///< 目标计数
};

/**
 * @class at_lord_marrowgar_entrance
 * @brief 玛洛加尔领主入口区域触发器
 *
 * 当玩家进入区域时触发玛洛加尔领主的台词
 */
class at_lord_marrowgar_entrance : public OnlyOnceAreaTriggerScript
{
    public:
        at_lord_marrowgar_entrance() : OnlyOnceAreaTriggerScript("at_lord_marrowgar_entrance") { }

        /**
         * @brief 尝试处理一次
         * @param player 触发的玩家
         * @param areaTrigger 区域触发器数据
         * @return true 表示成功处理
         */
        bool TryHandleOnce(Player* player, AreaTriggerEntry const* /*areaTrigger*/) override
        {
            if (InstanceScript* instance = player->GetInstanceScript())
                if (Creature* lordMarrowgar = ObjectAccessor::GetCreature(*player, instance->GetGuidData(DATA_LORD_MARROWGAR)))
                    lordMarrowgar->AI()->DoAction(ACTION_TALK_ENTER_ZONE);

            return true;
        }

};

/**
 * @brief 注册玛洛加尔领主脚本
 *
 * 注册所有生物AI和法术脚本
 */
void AddSC_boss_lord_marrowgar()
{
    // 注册生物AI
    RegisterIcecrownCitadelCreatureAI(boss_lord_marrowgar);
    RegisterIcecrownCitadelCreatureAI(npc_coldflame);
    RegisterIcecrownCitadelCreatureAI(npc_bone_spike);

    // 注册法术脚本
    RegisterSpellScript(spell_marrowgar_coldflame);
    RegisterSpellScript(spell_marrowgar_coldflame_bonestorm);
    RegisterSpellScript(spell_marrowgar_coldflame_damage);
    RegisterSpellScript(spell_marrowgar_bone_spike_graveyard);
    RegisterSpellScript(spell_marrowgar_bone_storm);
    RegisterSpellScript(spell_marrowgar_bone_slice);

    // 注册区域触发器
    new at_lord_marrowgar_entrance();
}
