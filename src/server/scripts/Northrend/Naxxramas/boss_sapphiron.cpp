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
 * @file boss_sapphiron.cpp
 * @brief 纳克萨玛斯副本 - 萨菲隆 Boss 战斗脚本模块
 *
 * 本模块实现了 Boss 萨菲隆 (Sapphiron) 的完整战斗逻辑，包括：
 * - 地面阶段和飞行阶段的切换
 * - 霜冻光环、冰霜吐息、冰霜暴风雪等技能管理
 * - 玩家被冰封和解冻机制
 * - "百人俱乐部"成就判定
 *
 * 萨菲隆战斗特点：
 * - Boss 周期性飞到空中，冰封部分玩家
 * - 飞行阶段结束后施放冰霜吐息，对未躲避在冰块后的玩家造成致命伤害
 * - 全程施放霜冻光环，对玩家造成持续冰霜伤害
 * - 生命吸取技能定期吸取玩家生命并治疗 Boss
 * - "百人俱乐部"成就要求：团队中没有任何玩家的冰霜抗性超过 100
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "naxxramas.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

/**
 * @brief 萨菲隆的表情 ID 枚举
 */
enum Yells
{
    EMOTE_AIR_PHASE         = 0,    // 飞行阶段开始表情
    EMOTE_GROUND_PHASE      = 1,    // 地面阶段开始表情
    EMOTE_BREATH            = 2,    // 冰霜吐息表情
    EMOTE_ENRAGE            = 3     // 狂暴表情
};

/**
 * @brief 萨菲隆使用的法术 ID 枚举
 */
enum Spells
{
    SPELL_FROST_AURA                    = 28531,   // 霜冻光环 - 全团持续冰霜伤害
    SPELL_CLEAVE                        = 19983,   // 顺劈斩 - 对前方敌人造成伤害
    SPELL_TAIL_SWEEP                    = 55697,   // 尾部横扫 - 对身后敌人造成伤害并击退
    SPELL_SUMMON_BLIZZARD               = 28560,   // 召唤暴风雪
    SPELL_LIFE_DRAIN                    = 28542,   // 生命吸取 - 吸取生命并治疗 Boss
    SPELL_ICEBOLT                       = 28522,   // 冰霜之箭 - 冰封目标
    SPELL_FROST_BREATH_ANTICHEAT        = 29318,   // 冰霜吐息防作弊 - 对入口平台造成伤害（无视视线）
    SPELL_FROST_BREATH                  = 28524,   // 冰霜吐息 - 对 Boss 下方的玩家造成伤害
    SPELL_FROST_MISSILE                 = 30101,   // 冰霜飞弹 - 仅视觉效果
    SPELL_BERSERK                       = 26662,   // 狂暴
    SPELL_DIES                          = 29357,   // 死亡法术
    SPELL_CHECK_RESISTS                 = 60539,   // 检查抗性 - 用于成就判定
    SPELL_SUMMON_WING_BUFFET            = 29329,   // 召唤翼击
    SPELL_WING_BUFFET_PERIODIC          = 29327,   // 翼击周期性效果
    SPELL_WING_BUFFET_DESPAWN_PERIODIC  = 29330,   // 翼击消失周期性效果
    SPELL_DESPAWN_BUFFET                = 29336    // 消失翼击
};

/**
 * @brief 战斗阶段枚举
 */
enum Phases
{
    PHASE_BIRTH = 1,                    // 生成阶段（从冰块中出现）
    PHASE_GROUND,                       // 地面战斗阶段
    PHASE_FLIGHT                        // 飞行阶段
};

/**
 * @brief 战斗事件 ID 枚举，用于事件调度系统
 */
enum Events
{
    EVENT_BERSERK       = 1,            // 狂暴计时器事件
    EVENT_CLEAVE,                       // 顺劈斩事件
    EVENT_TAIL,                         // 尾部横扫事件
    EVENT_DRAIN,                        // 生命吸取事件
    EVENT_BLIZZARD,                     // 召唤暴风雪事件
    EVENT_FLIGHT,                       // 飞行阶段开始事件
    EVENT_LIFTOFF,                      // 起飞事件
    EVENT_ICEBOLT,                      // 冰霜之箭事件
    EVENT_BREATH,                       // 冰霜吐息事件
    EVENT_EXPLOSION,                    // 爆炸事件
    EVENT_LAND,                         // 降落事件
    EVENT_GROUND,                       // 返回地面事件
    EVENT_BIRTH,                        // 生成事件
    EVENT_CHECK_RESISTS                 // 检查抗性事件
};

/**
 * @brief 其他数据常量枚举
 */
enum Misc
{
    NPC_BLIZZARD            = 16474,    // 暴风雪 NPC ID
    GO_ICEBLOCK             = 181247,   // 冰块游戏对象 ID

    // 百人俱乐部成就相关
    DATA_THE_HUNDRED_CLUB   = 21462147, // 百人俱乐部成就数据标识
    MAX_FROST_RESISTANCE    = 100,      // 最大冰霜抗性限制
    ACTION_BIRTH            = 1,        // 生成动作
    DATA_BLIZZARD_TARGET                // 暴风雪目标数据标识
};

typedef std::map<ObjectGuid, ObjectGuid> IceBlockMap;  // 冰块映射类型定义

/**
 * @brief 暴风雪目标选择器类
 *
 * 用于选择暴风雪的目标，排除已被暴风雪追踪的玩家。
 */
class BlizzardTargetSelector
{
public:
    /**
     * @brief 构造函数
     * @param blizzards 当前暴风雪列表
     */
    BlizzardTargetSelector(std::vector<Unit*> const& blizzards) : _blizzards(blizzards) { }

    /**
     * @brief 选择目标判断运算符
     * @param unit 待判断的单位
     * @return 如果单位是有效目标返回 true
     *
     * 排除非玩家单位，以及已被暴风雪追踪的玩家。
     */
    bool operator()(Unit* unit) const
    {
        if (unit->GetTypeId() != TYPEID_PLAYER)
            return false;

        // 检查该单位是否已经是某个暴风雪的目标
        for (Unit* blizzard : _blizzards)
            if (blizzard->GetAI()->GetGUID(DATA_BLIZZARD_TARGET) == unit->GetGUID())
                return false;

        return true;
    }

private:
    std::vector<Unit*> const& _blizzards;  // 当前暴风雪列表
};

/**
 * @brief 萨菲隆 Boss AI 结构体
 *
 * 继承自 BossAI，实现萨菲隆的完整战斗逻辑。
 * 萨菲隆战斗的核心机制是地面和飞行阶段的切换，以及冰块躲避机制。
 */
struct boss_sapphiron : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature Boss 生物对象指针
     */
    boss_sapphiron(Creature* creature) :
        BossAI(creature, BOSS_SAPPHIRON)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 初始化延迟生命吸取和百人俱乐部成就标志。
     */
    void Initialize()
    {
        _delayedDrain = false;
        _canTheHundredClub = true;
    }

    /**
     * @brief 初始化 AI
     *
     * 如果 Boss 尚未击杀，设置初始状态。
     * 如果 Boss 尚未生成（需要通过机关激活），设置为不可见和不可攻击。
     *
     * @调用时机 AI 创建时
     */
    void InitializeAI() override
    {
        if (instance->GetBossState(BOSS_SAPPHIRON) == DONE)
            return;

        _canTheHundredClub = true;

        // 如果 Boss 尚未通过机关激活，设置为不可见和不可攻击
        if (!instance->GetData(DATA_HAD_SAPPHIRON_BIRTH))
        {
            me->SetVisible(false);
            me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            me->SetReactState(REACT_PASSIVE);
        }

        BossAI::InitializeAI();
    }

    /**
     * @brief 重置 Boss 状态
     *
     * 当 Boss 脱离战斗或重置时调用。
     * 如果在飞行阶段重置，需要移除冰霜之箭光环并降落。
     *
     * @调用时机 Boss 脱离战斗、重置副本、Boss 初始化时
     */
    void Reset() override
    {
        // 如果在飞行阶段重置，需要恢复状态
        if (events.IsInPhase(PHASE_FLIGHT))
        {
            instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_ICEBOLT, true, true);
            me->SetReactState(REACT_AGGRESSIVE);
            if (me->IsHovering())
            {
                me->HandleEmoteCommand(EMOTE_ONESHOT_LAND);
                me->SetHover(false);
            }
        }

        _Reset();
        Initialize();
    }

    /**
     * @brief 受到伤害时的回调函数
     * @param who 伤害来源（未使用）
     * @param damage 受到的伤害量（引用传递，可修改）
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     *
     * 防止 Boss 在飞行阶段死亡，将伤害限制为当前生命值-1。
     *
     * @调用时机 Boss 受到伤害时
     */
    void DamageTaken(Unit* /*who*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (damage < me->GetHealth() || !events.IsInPhase(PHASE_FLIGHT))
            return;
        damage = me->GetHealth()-1; // 不要在飞行阶段死亡
    }

    /**
     * @brief Boss 进入战斗时的回调函数
     * @param who 激活 Boss 的目标
     *
     * 初始化战斗阶段，设置事件调度，施放霜冻光环。
     *
     * @调用时机 Boss 被玩家激活进入战斗状态时
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);

        me->CastSpell(me, SPELL_FROST_AURA, true);  // 施放霜冻光环

        events.SetPhase(PHASE_GROUND);               // 设置为地面阶段
        events.ScheduleEvent(EVENT_CHECK_RESISTS, 0s);
        events.ScheduleEvent(EVENT_BERSERK, 15min);  // 15分钟后狂暴
        EnterPhaseGround(true);                      // 进入地面阶段
    }

    /**
     * @brief 法术命中目标时的回调函数
     * @param target 被命中的目标
     * @param spellInfo 法术信息
     *
     * 处理抗性检查法术，用于判定"百人俱乐部"成就。
     *
     * @调用时机 法术命中目标时
     */
    void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
    {
        Unit* unitTarget = target->ToUnit();
        if (!unitTarget)
            return;

        switch(spellInfo->Id)
        {
            case SPELL_CHECK_RESISTS:
                // 如果目标的冰霜抗性超过 100，标记"百人俱乐部"成就失败
                if (unitTarget->GetResistance(SPELL_SCHOOL_FROST) > MAX_FROST_RESISTANCE)
                    _canTheHundredClub = false;
                break;
        }
    }

    /**
     * @brief Boss 死亡时的回调函数
     * @param killer 击杀者（未使用）
     *
     * 当萨菲隆死亡时调用，执行清理工作并播放死亡法术。
     *
     * @调用时机 Boss 生命值降为 0 时
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        me->CastSpell(me, SPELL_DIES, true);         // 播放死亡法术效果
    }

    /**
     * @brief 移动通知回调函数
     * @param type 移动类型（未使用）
     * @param id 移动点 ID
     *
     * 当 Boss 移动到指定位置时调用。
     * 用于触发起飞事件。
     *
     * @调用时机 Boss 到达移动目标点时
     */
    void MovementInform(uint32 /*type*/, uint32 id) override
    {
        if (id == 1)
            events.ScheduleEvent(EVENT_LIFTOFF, 0s, 0, PHASE_FLIGHT);
    }

    /**
     * @brief 执行动作
     * @param param 动作参数
     *
     * 处理外部发送的动作消息，主要用于激活 Boss（从冰块中生成）。
     *
     * @调用时机 外部 AI 发送动作消息时
     */
    void DoAction(int32 param) override
    {
        if (param == ACTION_BIRTH)
        {
            events.SetPhase(PHASE_BIRTH);
            events.ScheduleEvent(EVENT_BIRTH, 23s);  // 23秒后生成
        }
    }

    /**
     * @brief 进入地面阶段
     * @param initial 是否为初始进入地面阶段
     *
     * 设置地面阶段的事件调度，包括顺劈斩、尾部横扫、暴风雪等技能。
     *
     * @调用时机 战斗开始、飞行阶段结束时
     */
    void EnterPhaseGround(bool initial)
    {
        me->SetReactState(REACT_AGGRESSIVE);
        events.ScheduleEvent(EVENT_CLEAVE, randtime(Seconds(5), Seconds(15)), 0, PHASE_GROUND);
        events.ScheduleEvent(EVENT_TAIL, randtime(Seconds(7), Seconds(10)), 0, PHASE_GROUND);
        events.ScheduleEvent(EVENT_BLIZZARD, randtime(Seconds(5), Seconds(10)), 0, PHASE_GROUND);
        if (initial)
        {
            events.ScheduleEvent(EVENT_DRAIN, randtime(Seconds(22), Seconds(28)));
            events.ScheduleEvent(EVENT_FLIGHT, Seconds(48) + Milliseconds(500), 0, PHASE_GROUND);
        }
        else
            events.ScheduleEvent(EVENT_FLIGHT, Minutes(1), 0, PHASE_GROUND);
    }

    /**
     * @brief 施放生命吸取
     *
     * 对周围所有玩家施放生命吸取，并调度下一次施放。
     */
    inline void CastDrain()
    {
        DoCastAOE(SPELL_LIFE_DRAIN);
        events.ScheduleEvent(EVENT_DRAIN, randtime(Seconds(22), Seconds(28)));
    }

    /**
     * @brief 获取 Boss 自定义数据
     * @param data 数据类型标识
     * @return 如果是百人俱乐部成就查询且成就仍然可能，返回 1；否则返回 0
     *
     * @调用时机 成就系统检查是否完成"百人俱乐部"成就时
     */
    uint32 GetData(uint32 data) const override
    {
        if (data == DATA_THE_HUNDRED_CLUB)
            return _canTheHundredClub;

        return 0;
    }

    /**
     * @brief 获取 GUID
     * @param data 数据类型标识
     * @return 请求的 GUID
     *
     * 用于获取暴风雪的目标 GUID。
     *
     * @调用时机 暴风雪 AI 请求目标时
     */
    ObjectGuid GetGUID(int32 data) const override
    {
        if (data == DATA_BLIZZARD_TARGET)
        {
            // 从召唤列表中筛选暴风雪
            std::vector<Unit*> blizzards;
            for (ObjectGuid summonGuid : summons)
                if (summonGuid.GetEntry() == NPC_BLIZZARD)
                    if (Unit* temp = ObjectAccessor::GetUnit(*me, summonGuid))
                        blizzards.push_back(temp);

            // 选择一个新的随机目标
            if (Unit* newTarget = me->AI()->SelectTarget(SelectTargetMethod::Random, 1, BlizzardTargetSelector(blizzards)))
                return newTarget->GetGUID();
        }

        return ObjectGuid::Empty;
    }

    /**
     * @brief Boss AI 主更新函数
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 这是 Boss AI 的核心逻辑循环，每帧调用一次。
     * 处理事件调度、阶段转换、技能施放等所有战斗逻辑。
     *
     * 地面阶段主要处理的事件：
     * - EVENT_CHECK_RESISTS: 检查玩家抗性（用于成就）
     * - EVENT_GROUND: 返回地面阶段
     * - EVENT_BERSERK: 狂暴
     * - EVENT_CLEAVE: 顺劈斩
     * - EVENT_TAIL: 尾部横扫
     * - EVENT_DRAIN: 生命吸取
     * - EVENT_BLIZZARD: 召唤暴风雪
     * - EVENT_FLIGHT: 飞行阶段开始
     *
     * 飞行阶段主要处理的事件：
     * - EVENT_LIFTOFF: 起飞
     * - EVENT_ICEBOLT: 冰霜之箭
     * - EVENT_BREATH: 冰霜吐息
     * - EVENT_EXPLOSION: 爆炸
     * - EVENT_LAND: 降落
     * - EVENT_BIRTH: 生成
     *
     * @调用时机 每帧（服务器 tick）调用一次，频率约为 50ms
     * @性能注意 频繁调用，需要保持代码简洁高效
     */
    void UpdateAI(uint32 diff) override
    {
        // 更新事件调度器
        events.Update(diff);

        // 如果不在生成阶段且没有有效目标，直接返回
        if (!events.IsInPhase(PHASE_BIRTH) && !UpdateVictim())
            return;

        // 地面阶段逻辑
        if (events.IsInPhase(PHASE_GROUND))
        {
            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_CHECK_RESISTS:
                        // 检查所有玩家的冰霜抗性（用于"百人俱乐部"成就）
                        DoCast(me, SPELL_CHECK_RESISTS);
                        events.Repeat(Seconds(30));
                        return;

                    case EVENT_GROUND:
                        // 进入地面阶段
                        EnterPhaseGround(false);
                        return;

                    case EVENT_BERSERK:
                        // 狂暴
                        Talk(EMOTE_ENRAGE);
                        DoCast(me, SPELL_BERSERK);
                        return;

                    case EVENT_CLEAVE:
                        // 顺劈斩：对当前目标施放
                        DoCastVictim(SPELL_CLEAVE);
                        events.ScheduleEvent(EVENT_CLEAVE, randtime(Seconds(5), Seconds(15)), 0, PHASE_GROUND);
                        return;

                    case EVENT_TAIL:
                        // 尾部横扫：对身后玩家施放
                        DoCastAOE(SPELL_TAIL_SWEEP);
                        events.ScheduleEvent(EVENT_TAIL, randtime(Seconds(7), Seconds(10)), 0, PHASE_GROUND);
                        return;

                    case EVENT_DRAIN:
                        // 生命吸取：吸取玩家生命并治疗 Boss
                        CastDrain();
                        return;

                    case EVENT_BLIZZARD:
                        // 召唤暴风雪：在随机位置召唤暴风雪
                        DoCastAOE(SPELL_SUMMON_BLIZZARD);
                        events.ScheduleEvent(EVENT_BLIZZARD, RAID_MODE(Seconds(20), Seconds(7)), 0, PHASE_GROUND);
                        break;

                    case EVENT_FLIGHT:
                        // 飞行阶段开始：如果生命值高于 10%，进入飞行阶段
                        if (HealthAbovePct(10))
                        {
                            _delayedDrain = false;
                            events.SetPhase(PHASE_FLIGHT);          // 切换到飞行阶段
                            me->SetReactState(REACT_PASSIVE);       // 设置为被动反应
                            me->AttackStop();                       // 停止攻击
                            // 移动到初始位置（准备起飞）
                            float x, y, z, o;
                            me->GetHomePosition(x, y, z, o);
                            me->GetMotionMaster()->MovePoint(1, x, y, z);
                            return;
                        }
                        break;
                }
            }

            // 近战攻击
            DoMeleeAttackIfReady();
        }
        else
        {
            // 飞行阶段和生成阶段逻辑
            if (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_CHECK_RESISTS:
                        // 检查玩家抗性（在飞行阶段也持续检查）
                        DoCast(me, SPELL_CHECK_RESISTS);
                        events.Repeat(Seconds(30));
                        return;

                    case EVENT_LIFTOFF:
                    {
                        // 起飞：飞到空中
                        Talk(EMOTE_AIR_PHASE);
                        DoCastSelf(SPELL_SUMMON_WING_BUFFET);       // 召唤翼击
                        me->HandleEmoteCommand(EMOTE_ONESHOT_LIFTOFF);
                        me->SetHover(true);                         // 设置为悬停状态
                        events.ScheduleEvent(EVENT_ICEBOLT, Seconds(7), 0, PHASE_FLIGHT);

                        // 选择冰霜之箭的目标
                        _iceboltTargets.clear();
                        std::list<Unit*> targets;
                        SelectTargetList(targets, RAID_MODE(2, 3), SelectTargetMethod::Random, 0, 200.0f, true);
                        for (Unit* target : targets)
                            if (target)
                                _iceboltTargets.push_back(target->GetGUID());
                        return;
                    }

                    case EVENT_ICEBOLT:
                    {
                        // 冰霜之箭：冰封目标玩家
                        if (_iceboltTargets.empty())
                        {
                            // 所有目标都已处理，2秒后施放冰霜吐息
                            events.ScheduleEvent(EVENT_BREATH, Seconds(2), 0, PHASE_FLIGHT);
                            return;
                        }
                        // 获取最后一个目标并施放冰霜之箭
                        ObjectGuid target = _iceboltTargets.back();
                        if (Player* pTarget = ObjectAccessor::GetPlayer(*me, target))
                            if (pTarget->IsAlive())
                                DoCast(pTarget, SPELL_ICEBOLT);
                        _iceboltTargets.pop_back();

                        // 检查是否还有目标需要处理
                        if (_iceboltTargets.empty())
                            events.ScheduleEvent(EVENT_BREATH, Seconds(2), 0, PHASE_FLIGHT);
                        else
                            events.Repeat(Seconds(3));              // 3秒后继续下一个目标
                        return;
                    }

                    case EVENT_BREATH:
                    {
                        // 冰霜吐息：对 Boss 下方的玩家造成致命伤害
                        Talk(EMOTE_BREATH);
                        DoCastAOE(SPELL_FROST_MISSILE);             // 播放视觉效果
                        events.ScheduleEvent(EVENT_EXPLOSION, Seconds(8), 0, PHASE_FLIGHT);
                        return;
                    }

                    case EVENT_EXPLOSION:
                        // 爆炸：施放冰霜吐息伤害
                        DoCastAOE(SPELL_FROST_BREATH);              // 对 Boss 下方的玩家造成伤害
                        DoCastAOE(SPELL_FROST_BREATH_ANTICHEAT);    // 对入口平台的玩家造成伤害（防作弊）
                        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_ICEBOLT, true, true);  // 移除冰霜之箭效果
                        events.ScheduleEvent(EVENT_LAND, Seconds(3) + Milliseconds(500), 0, PHASE_FLIGHT);
                        return;

                    case EVENT_LAND:
                        // 降落：返回地面
                        DoCastSelf(SPELL_DESPAWN_BUFFET);           // @todo: 此时应该已经消失，可能该法术在其他地方使用
                        if (_delayedDrain)
                            CastDrain();                            // 如果延迟了生命吸取，现在施放
                        me->HandleEmoteCommand(EMOTE_ONESHOT_LAND);
                        Talk(EMOTE_GROUND_PHASE);
                        me->SetHover(false);                        // 取消悬停状态
                        events.SetPhase(PHASE_GROUND);              // 切换回地面阶段
                        events.ScheduleEvent(EVENT_GROUND, Seconds(3) + Milliseconds(500), 0, PHASE_GROUND);
                        return;

                    case EVENT_BIRTH:
                        // 生成：从冰块中出现
                        me->SetVisible(true);
                        me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                        me->SetReactState(REACT_AGGRESSIVE);
                        return;

                    case EVENT_DRAIN:
                        // 延迟生命吸取（在飞行阶段期间触发的生命吸取）
                        _delayedDrain = true;
                        break;
                }
            }
        }
    }

private:
    GuidVector _iceboltTargets;         // 冰霜之箭目标列表
    bool _delayedDrain;                 // 是否延迟生命吸取（飞行阶段时触发的生命吸取）
    bool _canTheHundredClub;            // "百人俱乐部"成就是否仍可能完成（true = 尚无玩家抗性超过 100）
};

/**
 * @brief 萨菲隆暴风雪 NPC AI 结构体
 *
 * 管理暴风雪的行为，暴风雪会追踪一个目标玩家。
 */
struct npc_sapphiron_blizzard : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature NPC 生物对象指针
     */
    npc_sapphiron_blizzard(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 重置 AI 状态
     *
     * 设置为被动反应状态，并调度周期性寒冰箭施放。
     *
     * @调用时机 NPC 初始化或重置时
     */
    void Reset() override
    {
        me->SetReactState(REACT_PASSIVE);           // 设置为被动反应
        // 每 3 秒施放一次寒冰箭
        _scheduler.Schedule(Seconds(3), [this](TaskContext chill)
        {
            DoCastSelf(me->m_spells[0], true);      // 施放寒冰箭
            chill.Repeat();                         // 重复调度
        });
    }

    /**
     * @brief 获取 GUID
     * @param data 数据类型标识
     * @return 如果是暴风雪目标查询，返回目标 GUID；否则返回空 GUID
     */
    ObjectGuid GetGUID(int32 data) const override
    {
        return data == DATA_BLIZZARD_TARGET ? _targetGuid : ObjectGuid::Empty;
    }

    /**
     * @brief 设置 GUID
     * @param guid GUID 值
     * @param id 数据类型标识
     *
     * 用于设置暴风雪追踪的目标玩家 GUID。
     */
    void SetGUID(ObjectGuid const& guid, int32 id) override
    {
        if (id == DATA_BLIZZARD_TARGET)
            _targetGuid = guid;
    }

    /**
     * @brief AI 主更新函数
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 更新任务调度器，处理周期性寒冰箭施放。
     */
    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);
    }

private:
    TaskScheduler _scheduler;           // 任务调度器
    ObjectGuid _targetGuid;             // 暴风雪追踪的目标玩家 GUID
};

/**
 * @brief 萨菲隆翼击 NPC AI 结构体
 *
 * 管理翼击的行为，翼击是萨菲隆起飞时产生的视觉效果。
 */
struct npc_sapphiron_wing_buffet : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature NPC 生物对象指针
     */
    npc_sapphiron_wing_buffet(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 初始化 AI
     *
     * 设置为被动反应状态。
     */
    void InitializeAI() override
    {
        me->SetReactState(REACT_PASSIVE);
    }

    /**
     * @brief NPC 出现时的回调函数
     *
     * 施放翼击周期性效果和消失周期性效果。
     *
     * @调用时机 NPC 生成时
     */
    void JustAppeared() override
    {
        DoCastSelf(SPELL_WING_BUFFET_PERIODIC);            // 施放翼击周期性效果
        DoCastSelf(SPELL_WING_BUFFET_DESPAWN_PERIODIC);    // 施放消失周期性效果
    }
};

/**
 * @brief 萨菲隆生成游戏对象 AI 结构体
 *
 * 管理萨菲隆生成机关（游戏对象），玩家激活机关后会触发萨菲隆生成。
 */
struct go_sapphiron_birth : public GameObjectAI
{
    /**
     * @brief 构造函数
     * @param go 游戏对象指针
     */
    go_sapphiron_birth(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

    /**
     * @brief 战利品状态改变时的回调函数
     * @param state 新状态
     * @param who 激活者
     *
     * 当机关被激活时，触发萨菲隆生成。
     *
     * @调用时机 游戏对象状态改变时
     */
    void OnLootStateChanged(uint32 state, Unit* who) override
    {
        if (state == GO_ACTIVATED)
        {
            if (who)
            {
                // 获取萨菲隆实例并触发生成动作
                if (Creature* sapphiron = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_SAPPHIRON)))
                    sapphiron->AI()->DoAction(ACTION_BIRTH);
                instance->SetData(DATA_HAD_SAPPHIRON_BIRTH, 1u);  // 标记萨菲隆已生成
            }
        }
        else if (state == GO_JUST_DEACTIVATED)
        {
            // 防止游戏对象回到 _READY 状态并重置客户端动画
            me->SetRespawnTime(0);
            me->Delete();
        }
    }

    InstanceScript* instance;           // 副本脚本实例
};

/**
 * @brief 梦境迷雾光环脚本（法术 ID: 24780）
 *
 * 处理暴风雪目标切换的逻辑，周期性地为暴风雪选择新的追踪目标。
 */
class spell_sapphiron_change_blizzard_target : public AuraScript
{
    PrepareAuraScript(spell_sapphiron_change_blizzard_target);

    /**
     * @brief 处理周期性效果
     * @param eff 光环效果（未使用）
     *
     * 周期性地为暴风雪选择新的追踪目标，如果没有有效目标则停止移动。
     *
     * @调用时机 光环周期性效果触发时
     */
    void HandlePeriodic(AuraEffect const* /*eff*/)
    {
        TempSummon* me = GetTarget()->ToTempSummon();
        if (Creature* owner = me ? me->GetSummonerCreatureBase() : nullptr)
        {
            me->GetAI()->SetGUID(ObjectGuid::Empty, DATA_BLIZZARD_TARGET);
            // 从召唤者（萨菲隆）获取新的暴风雪目标
            if (Unit* newTarget = ObjectAccessor::GetUnit(*owner, owner->AI()->GetGUID(DATA_BLIZZARD_TARGET)))
            {
                me->GetAI()->SetGUID(newTarget->GetGUID(), DATA_BLIZZARD_TARGET);
                me->GetMotionMaster()->MoveFollow(newTarget, 0.1f, 0.0f);  // 追踪新目标
            }
            else
            {
                // 没有有效目标，停止移动
                me->StopMoving();
                me->GetMotionMaster()->Clear();
            }
        }
    }

    /**
     * @brief 注册光环效果处理函数
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_sapphiron_change_blizzard_target::HandlePeriodic, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

/**
 * @brief 冰霜之箭光环脚本（法术 ID: 28522）
 *
 * 处理冰霜之箭的逻辑：冰封目标玩家，并在目标停止移动后生成冰块。
 */
class spell_sapphiron_icebolt : public AuraScript
{
    PrepareAuraScript(spell_sapphiron_icebolt);

    /**
     * @brief 光环应用时的处理函数
     * @param eff 光环效果（未使用）
     * @param mode 光环效果处理模式（未使用）
     *
     * 为目标添加冰霜伤害免疫。
     *
     * @调用时机 光环效果应用时
     */
    void HandleApply(AuraEffect const* /*eff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->ApplySpellImmune(SPELL_ICEBOLT, IMMUNITY_DAMAGE, SPELL_SCHOOL_MASK_FROST, true);
    }

    /**
     * @brief 光环移除时的处理函数
     * @param eff 光环效果（未使用）
     * @param mode 光环效果处理模式（未使用）
     *
     * 删除冰块游戏对象，并移除冰霜伤害免疫。
     *
     * @调用时机 光环效果移除时
     */
    void HandleRemove(AuraEffect const* /*eff*/, AuraEffectHandleModes /*mode*/)
    {
        if (_block)
            if (GameObject* oBlock = ObjectAccessor::GetGameObject(*GetTarget(), _block))
                oBlock->Delete();
        GetTarget()->ApplySpellImmune(SPELL_ICEBOLT, IMMUNITY_DAMAGE, SPELL_SCHOOL_MASK_FROST, false);
    }

    /**
     * @brief 处理周期性效果
     * @param eff 光环效果（未使用）
     *
     * 当目标停止移动时，生成冰块游戏对象。
     *
     * @调用时机 光环周期性效果触发时
     */
    void HandlePeriodic(AuraEffect const* /*eff*/)
    {
        if (_block)                                     // 已经生成冰块，直接返回
            return;
        if (GetTarget()->isMoving())                    // 目标还在移动，不生成冰块
            return;
        // 在目标位置生成冰块
        float x, y, z;
        GetTarget()->GetPosition(x, y, z);
        if (GameObject* block = GetTarget()->SummonGameObject(GO_ICEBLOCK, x, y, z, 0.f, QuaternionData(), 25s))
            _block = block->GetGUID();
    }

    /**
     * @brief 注册光环效果处理函数
     */
    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_sapphiron_icebolt::HandleApply, EFFECT_0, SPELL_AURA_MOD_STUN, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_sapphiron_icebolt::HandleRemove, EFFECT_0, SPELL_AURA_MOD_STUN, AURA_EFFECT_HANDLE_REAL);
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_sapphiron_icebolt::HandlePeriodic, EFFECT_2, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }

    ObjectGuid _block;              // 生成的冰块游戏对象 GUID
};

/**
 * @brief 召唤暴风雪法术脚本（法术 ID: 28560）
 *
 * 处理召唤暴风雪的逻辑，在目标位置生成暴风雪 NPC。
 */
class spell_sapphiron_summon_blizzard : public SpellScript
{
    PrepareSpellScript(spell_sapphiron_summon_blizzard);

    /**
     * @brief 验证法术信息
     * @param spell 法术信息（未使用）
     * @return 如果法术验证通过返回 true
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_BLIZZARD });
    }

    /**
     * @brief 处理虚拟效果
     * @param effIndex 法术效果索引（未使用）
     *
     * 在目标位置召唤暴风雪 NPC，并设置其追踪目标。
     *
     * @调用时机 法术效果命中目标时
     */
    void HandleDummy(SpellEffIndex /*effIndex*/)
    {
        if (Unit* target = GetHitUnit())
            if (Creature* blizzard = GetCaster()->SummonCreature(NPC_BLIZZARD, *target, TEMPSUMMON_TIMED_DESPAWN, randtime(25s, 30s)))
            {
                // 施放暴风雪的第一个法术
                blizzard->CastSpell(nullptr, blizzard->m_spells[0], TRIGGERED_NONE);
                if (Creature* creatureCaster = GetCaster()->ToCreature())
                {
                    // 从召唤者（萨菲隆）获取暴风雪目标
                    blizzard->AI()->SetGUID(ObjectGuid::Empty, DATA_BLIZZARD_TARGET);
                    if (Unit* newTarget = ObjectAccessor::GetUnit(*creatureCaster, creatureCaster->AI()->GetGUID(DATA_BLIZZARD_TARGET)))
                    {
                        blizzard->AI()->SetGUID(newTarget->GetGUID(), DATA_BLIZZARD_TARGET);
                        blizzard->GetMotionMaster()->MoveFollow(newTarget, 0.1f, 0.0f);
                        return;
                    }
                }
                // 如果没有从萨菲隆获取到目标，使用法术目标
                blizzard->GetMotionMaster()->MoveFollow(target, 0.1f, 0.0f);
            }
    }

    /**
     * @brief 注册法术效果处理函数
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_sapphiron_summon_blizzard::HandleDummy, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 萨菲隆翼击消失周期性光环脚本（法术 ID: 29330）
 *
 * 处理翼击 NPC 的周期性消失逻辑。
 */
class spell_sapphiron_wing_buffet_despawn_periodic : public AuraScript
{
    PrepareAuraScript(spell_sapphiron_wing_buffet_despawn_periodic);

    /**
     * @brief 处理周期性效果
     * @param aurEff 光环效果（未使用）
     *
     * 周期性地让目标生物消失。
     *
     * @调用时机 光环周期性效果触发时
     */
    void PeriodicTick(AuraEffect const* /*aurEff*/)
    {
        Unit* target = GetTarget();
        if (Creature* creature = target->ToCreature())
            creature->DespawnOrUnsummon();
    }

    /**
     * @brief 注册光环效果处理函数
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_sapphiron_wing_buffet_despawn_periodic::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_TRIGGER_SPELL);
    }
};

/**
 * @brief 消失翼击法术脚本（法术 ID: 29336）
 *
 * 处理让翼击 NPC 消失的逻辑。
 */
class spell_sapphiron_despawn_buffet : public SpellScript
{
    PrepareSpellScript(spell_sapphiron_despawn_buffet);

    /**
     * @brief 处理脚本效果
     * @param effIndex 法术效果索引（未使用）
     *
     * 让目标生物消失。
     *
     * @调用时机 法术效果命中目标时
     */
    void HandleScriptEffect(SpellEffIndex /* effIndex */)
    {
        if (Creature* target = GetHitCreature())
            target->DespawnOrUnsummon();
    }

    /**
     * @brief 注册法术效果处理函数
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_sapphiron_despawn_buffet::HandleScriptEffect, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 百人俱乐部成就脚本
 *
 * 实现"百人俱乐部"成就的判定逻辑。
 * 成就要求：团队中没有任何玩家的冰霜抗性超过 100。
 */
class achievement_the_hundred_club : public AchievementCriteriaScript
{
    public:
        /**
         * @brief 构造函数
         */
        achievement_the_hundred_club() : AchievementCriteriaScript("achievement_the_hundred_club") { }

        /**
         * @brief 检查是否满足成就条件
         * @param source 玩家对象（未使用）
         * @param target 目标单位（应为萨菲隆 Boss）
         * @return 如果满足成就条件返回 true，否则返回 false
         *
         * 通过查询萨菲隆 AI 的 _canTheHundredClub 标志来判断是否有玩家抗性超过 100。
         *
         * @调用时机 成就系统检查该成就是否完成时
         */
        bool OnCheck(Player* /*source*/, Unit* target) override
        {
            return target && target->GetAI()->GetData(DATA_THE_HUNDRED_CLUB);
        }
};

/**
 * @brief 注册萨菲隆 Boss 脚本
 *
 * 将所有萨菲隆相关的脚本注册到系统中，包括：
 * - Boss AI
 * - 暴风雪 NPC AI
 * - 翼击 NPC AI
 * - 生成机关游戏对象 AI
 * - 法术脚本
 * - 成就脚本
 *
 * @调用时机 服务器启动时，由脚本加载系统自动调用
 */
void AddSC_boss_sapphiron()
{
    RegisterNaxxramasCreatureAI(boss_sapphiron);
    RegisterNaxxramasCreatureAI(npc_sapphiron_blizzard);
    RegisterNaxxramasCreatureAI(npc_sapphiron_wing_buffet);
    RegisterNaxxramasGameObjectAI(go_sapphiron_birth);
    RegisterSpellScript(spell_sapphiron_change_blizzard_target);
    RegisterSpellScript(spell_sapphiron_icebolt);
    RegisterSpellScript(spell_sapphiron_summon_blizzard);
    RegisterSpellScript(spell_sapphiron_wing_buffet_despawn_periodic);
    RegisterSpellScript(spell_sapphiron_despawn_buffet);
    new achievement_the_hundred_club();
}
