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
 * @file boss_noth.cpp
 * @brief 纳克萨玛斯副本BOSS - 诺斯·阿肯希斯(Noth the Plaguebringer)的AI脚本
 *
 * 模块职责:
 * - 实现诺斯BOSS的战斗逻辑
 * - 管理地面阶段(Ground Phase)和阳台阶段(Balcony Phase)的切换
 * - 处理诅咒和闪烁技能
 * - 实现骷髅战士、勇士和守护者的召唤机制
 * - 防止BOSS在阳台阶段被击杀
 *
 * 战斗机制:
 * - 地面阶段:BOSS在地面战斗,施放诅咒和召唤骷髅战士,25人模式额外闪烁
 * - 阳台阶段:BOSS传送到阳台,无法被攻击,召唤大量骷髅勇士和守护者
 * - 阶段循环:地面→阳台→地面→阳台→地面,每次阳台阶段召唤的怪物不同
 * - 诅咒:对随机玩家施放,需要驱散
 * - 闪烁(仅25人):随机传送并清除仇恨列表
 */

#include "ScriptMgr.h"
#include "MotionMaster.h"
#include "naxxramas.h"
#include "ScriptedCreature.h"

/**
 * @brief 战斗阶段枚举
 */
enum Phases
{
    PHASE_NONE,       // 无阶段(初始状态)
    PHASE_GROUND,     // 地面阶段 - BOSS可被攻击,在地面战斗
    PHASE_BALCONY     // 阳台阶段 - BOSS传送到阳台,无法被攻击
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_CURSE = 1,            // 瘟疫诅咒事件
    EVENT_BLINK,                // 闪烁事件(仅25人模式)
    EVENT_WARRIOR,              // 地面阶段召唤骷髅战士事件
    EVENT_BALCONY,              // 进入阳台阶段事件
    EVENT_BALCONY_TELEPORT,     // 实际传送到阳台事件(稍有延迟)
    EVENT_WAVE,                 // 阳台阶段召唤波次事件
    EVENT_GROUND,               // 结束阳台阶段并传送回地面事件
    EVENT_GROUND_ATTACKABLE     // 地面阶段开始时变为可攻击状态,延迟以避免移动控制器问题
};

/**
 * @brief 台词枚举
 */
enum Talk
{
    SAY_AGGRO               = 0,    // 开战台词
    SAY_SUMMON              = 1,    // 召唤台词(地面阶段)
    SAY_SLAY                = 2,    // 击杀玩家台词
    SAY_DEATH               = 3,    // 死亡台词

    EMOTE_SUMMON            = 4,    // 召唤表情(地面阶段)
    EMOTE_SUMMON_WAVE       = 5,    // 召唤波次表情(阳台阶段)
    EMOTE_TELEPORT_1        = 6,    // 传送表情(地面到阳台)
    EMOTE_TELEPORT_2        = 7     // 传送表情(阳台到地面)
};

/**
 * @brief 技能ID枚举
 */
enum Spells
{
    SPELL_CURSE         = 29213,    // 瘟疫诅咒(10人)
    SPELL_CRIPPLE       = 29212,    // 致残(10人)
    SPELL_CURSE_25      = 54835,    // 瘟疫诅咒(25人)
    SPELL_CRIPPLE_25    = 54814,    // 致残(25人)

    SPELL_TELEPORT      = 29216,    // 传送到阳台
    SPELL_TELEPORT_BACK = 29231     // 传送回地面
};

/**
 * @brief 召唤生物技能数量枚举
 */
enum Adds
{
    N_WARRIOR_SPELLS = 3,       // 骷髅战士召唤技能数量
    N_CHAMPION_SPELLS = 6,      // 骷髅勇士召唤技能数量
    N_GUARDIAN_SPELLS = 3       // 骷髅守护者召唤技能数量
};

/// 骷髅战士召唤技能ID数组(每个技能对应一个召唤位置)
const uint32 SummonWarriorSpells[N_WARRIOR_SPELLS] = { 29247, 29248, 29249 };
/// 骷髅勇士召唤技能ID数组
const uint32 SummonChampionSpells[N_CHAMPION_SPELLS] = { 29238, 29255, 29257, 29258, 29262, 29267 };
/// 骷髅守护者召唤技能ID数组
const uint32 SummonGuardianSpells[N_GUARDIAN_SPELLS] = { 29239, 29256, 29268 };

/// 闪烁技能(随机选择4个闪烁法术之一)
#define SPELL_BLINK                 RAND(29208, 29209, 29210, 29211)

/**
 * @struct boss_noth
 * @brief 诺斯·阿肯希斯BOSS的AI实现
 *
 * 继承自BossAI,实现了诺斯的完整战斗逻辑,包括:
 * - 地面阶段和阳台阶段的切换
 * - 瘟疫诅咒和闪烁技能的施放
 * - 骷髅战士、勇士和守护者的召唤机制
 * - 防止在阳台阶段被击杀的保护机制
 */
struct boss_noth : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature BOSS生物对象指针
     *
     * 初始化BOSS并复制召唤技能数组到成员变量
     */
    boss_noth(Creature* creature) : BossAI(creature, BOSS_NOTH), balconyCount(0), justBlinked(false)
    {
        // 复制召唤技能数组到本地成员变量,用于随机选择时打乱顺序
        std::copy(SummonWarriorSpells, SummonWarriorSpells + N_WARRIOR_SPELLS, _SummonWarriorSpells);
        std::copy(SummonChampionSpells, SummonChampionSpells + N_CHAMPION_SPELLS, _SummonChampionSpells);
        std::copy(SummonGuardianSpells, SummonGuardianSpells + N_GUARDIAN_SPELLS, _SummonGuardianSpells);

        events.SetPhase(PHASE_NONE);
    }

    /**
     * @brief 进入躲避模式
     * @param why 躲避原因
     *
     * 如果在阳台阶段重置,先传送回地面再脱离战斗
     * 调用时机: BOSS脱离战斗时
     */
    void EnterEvadeMode(EvadeReason why) override
    {
        // 如果在阳台阶段重置,先传送回地面
        if (events.IsInPhase(PHASE_BALCONY))
            DoCastAOE(SPELL_TELEPORT_BACK);
        BossAI::EnterEvadeMode(why);
    }

    /**
     * @brief 重置BOSS状态
     *
     * 清空事件计时器,重置阶段计数和状态
     * 调用时机: BOSS脱离战斗或副本重置时
     */
    void Reset() override
    {
        _Reset();

        // 恢复攻击性和可交互状态
        me->SetReactState(REACT_AGGRESSIVE);
        me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

        balconyCount = 0;
        events.SetPhase(PHASE_NONE);
        justBlinked = false;
    }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标
     *
     * 初始化战斗并进入地面阶段
     * 调用时机: BOSS进入战斗时
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);
        EnterPhaseGround();
    }

    /**
     * @brief 进入地面阶段
     *
     * 安排地面阶段的所有事件计时器,包括诅咒、召唤战士和进入阳台的时间
     * 地面阶段持续时间随阳台次数增加而延长
     */
    void EnterPhaseGround()
    {
        events.SetPhase(PHASE_GROUND);

        DoZoneInCombat();

        if (!me->IsThreatened())
            EnterEvadeMode(EVADE_REASON_NO_HOSTILES);
        else
        {
            // 地面阶段持续时间根据阳台次数确定
            uint8 timeGround;
            switch (balconyCount)
            {
                case 0:
                    timeGround =  90;    // 第1次地面阶段:90秒
                    break;
                case 1:
                    timeGround = 110;    // 第2次地面阶段:110秒
                    break;
                case 2:
                default:
                    timeGround = 180;    // 第3次及以后:180秒
            }
            // 2秒后变为可攻击状态(延迟以避免移动控制器问题)
            events.ScheduleEvent(EVENT_GROUND_ATTACKABLE, Seconds(2), 0, PHASE_GROUND);
            // 安排进入阳台阶段
            events.ScheduleEvent(EVENT_BALCONY, Seconds(timeGround), 0, PHASE_GROUND);
            // 安排诅咒技能 - 10-25秒后随机时间
            events.ScheduleEvent(EVENT_CURSE, randtime(Seconds(10), Seconds(25)), 0, PHASE_GROUND);
            // 安排召唤战士 - 20-30秒后随机时间
            events.ScheduleEvent(EVENT_WARRIOR, randtime(Seconds(20), Seconds(30)), 0, PHASE_GROUND);
            // 25人模式安排闪烁技能
            if (GetDifficulty() == RAID_DIFFICULTY_25MAN_NORMAL)
                events.ScheduleEvent(EVENT_BLINK, randtime(Seconds(20), Seconds(30)), 0, PHASE_GROUND);
        }
    }

    /**
     * @brief 击杀单位的处理
     * @param victim 被击杀的单位
     *
     * 当击杀玩家时播放击杀台词
     * 调用时机: BOSS击杀单位时
     */
    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_SLAY);
    }

    /**
     * @brief 召唤生物时的处理
     * @param summon 被召唤的生物
     *
     * 将召唤生物加入管理列表,并让其进入战斗
     * 调用时机: 召唤生物时
     */
    void JustSummoned(Creature* summon) override
    {
        summons.Summon(summon);
        summon->setActive(true);
        summon->SetFarVisible(true);
        summon->AI()->DoZoneInCombat();
    }

    /**
     * @brief BOSS死亡处理
     * @param killer 击杀者
     *
     * 播放死亡台词
     * 调用时机: BOSS死亡时
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);
    }

    /**
     * @brief 受到伤害时的处理
     * @param who 造成伤害的单位
     * @param damage 伤害值(引用,可修改)
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 防止BOSS在阳台阶段被击杀
     * 如果在阳台阶段伤害会导致死亡,则将伤害设为0,血量设为1
     * 调用时机: BOSS受到伤害时
     */
    void DamageTaken(Unit* /*who*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 只在阳台阶段保护BOSS
        if (!events.IsInPhase(PHASE_BALCONY))
            return;
        // 如果伤害不足以击杀BOSS,则不处理
        if (damage < me->GetHealth())
            return;

        // 防止BOSS在阳台阶段死亡
        me->SetHealth(1u);
        damage = 0u;
    }

    /**
     * @brief 处理召唤生物
     * @param spellsList 召唤技能ID数组
     * @param nSpells 技能数组长度
     * @param num 要召唤的生物数量
     *
     * 随机选择召唤技能,确保不会在相同位置召唤多个生物
     * 通过打乱技能数组来实现位置分散
     */
    void HandleSummon(uint32* spellsList, const uint8 nSpells, uint8 num)
    {
        // 确保不会在相同位置召唤多个生物(如果可能的话)
        while (num)
            for (uint8 it = 0; it < nSpells && num; ++it)
            {
                num--;
                // 从剩余技能中随机选择一个
                uint8 selected = urand(it, nSpells - 1);
                DoCastAOE(spellsList[selected]);
                // 将选中的技能与当前位置交换,避免重复选择
                if (selected != it)
                    std::swap(spellsList[selected], spellsList[it]);
            }
    }

    /**
     * @brief 施放召唤技能
     * @param nWarrior 骷髅战士数量
     * @param nChampion 骷髅勇士数量
     * @param nGuardian 骷髅守护者数量
     *
     * 召唤指定数量的各种骷髅生物
     */
    void CastSummon(uint8 nWarrior, uint8 nChampion, uint8 nGuardian)
    {
        HandleSummon(_SummonWarriorSpells, N_WARRIOR_SPELLS, nWarrior);
        HandleSummon(_SummonChampionSpells, N_CHAMPION_SPELLS, nChampion);
        HandleSummon(_SummonGuardianSpells, N_GUARDIAN_SPELLS, nGuardian);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 处理所有战斗事件的执行,包括诅咒、召唤、闪烁和阶段切换
     * 调用时机: 每个世界更新周期(默认约50ms)
     * 性能注意事项: 该函数会被频繁调用,应避免复杂计算
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效目标,没有则返回
        if (!UpdateVictim())
            return;

        // 更新事件计时器
        events.Update(diff);

        // 如果正在施法,则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CURSE:
                {
                    // 施放瘟疫诅咒(根据难度选择)
                    DoCastAOE(RAID_MODE(SPELL_CURSE, SPELL_CURSE_25));
                    // 安排下一次诅咒,50-70秒后随机时间
                    events.Repeat(randtime(Seconds(50), Seconds(70)));
                    break;
                }
                case EVENT_WARRIOR:
                    // 播放召唤台词和表情
                    Talk(SAY_SUMMON);
                    Talk(EMOTE_SUMMON);

                    // 召唤骷髅战士:10人模式2个,25人模式3个
                    CastSummon(RAID_MODE(2, 3), 0, 0);

                    // 安排下一次召唤,40秒后
                    events.Repeat(Seconds(40));
                    break;
                case EVENT_BLINK:
                    // 施放致残(降低目标移动速度)
                    DoCastAOE(RAID_MODE(SPELL_CRIPPLE, SPELL_CRIPPLE_25), true);
                    // 施放闪烁(随机传送)
                    DoCastAOE(SPELL_BLINK);
                    // 清除仇恨列表
                    ResetThreatList();
                    justBlinked = true;

                    // 安排下一次闪烁,40秒后
                    events.Repeat(Seconds(40));
                    break;
                case EVENT_BALCONY:
                    // 切换到阳台阶段
                    events.SetPhase(PHASE_BALCONY);
                    // 设置为被动状态
                    me->SetReactState(REACT_PASSIVE);
                    // 设置为不可交互状态(无法被攻击)
                    me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                    // 停止攻击和移动
                    me->AttackStop();
                    me->StopMoving();
                    // 移除所有光环
                    me->RemoveAllAuras();

                    // 3秒后传送到阳台
                    events.ScheduleEvent(EVENT_BALCONY_TELEPORT, Seconds(3), 0, PHASE_BALCONY);
                    // 5-8秒后召唤第一波怪物
                    events.ScheduleEvent(EVENT_WAVE, randtime(Seconds(5), Seconds(8)), 0, PHASE_BALCONY);

                    // 阳台阶段持续时间根据阳台次数确定
                    uint8 timeBalcony;
                    switch (balconyCount)
                    {
                        case 0:
                            timeBalcony = 70;    // 第1次阳台阶段:70秒
                            break;
                        case 1:
                            timeBalcony = 97;    // 第2次阳台阶段:97秒
                            break;
                        case 2:
                        default:
                            timeBalcony = 120;   // 第3次及以后:120秒
                            break;
                    }
                    // 安排返回地面
                    events.ScheduleEvent(EVENT_GROUND, Seconds(timeBalcony), 0, PHASE_BALCONY);
                    break;
                case EVENT_BALCONY_TELEPORT:
                    // 播放传送表情并传送到阳台
                    Talk(EMOTE_TELEPORT_1);
                    DoCastAOE(SPELL_TELEPORT);
                    break;
                case EVENT_WAVE:
                    // 播放召唤波次表情
                    Talk(EMOTE_SUMMON_WAVE);
                    // 根据阳台次数召唤不同的怪物组合
                    switch (balconyCount)
                    {
                        case 0:
                            // 第1次阳台:只召唤勇士,10人2个,25人4个
                            CastSummon(0, RAID_MODE(2, 4), 0);
                            break;
                        case 1:
                            // 第2次阳台:召唤勇士和守护者各一半
                            CastSummon(0, RAID_MODE(1, 2), RAID_MODE(1, 2));
                            break;
                        case 2:
                            // 第3次阳台:只召唤守护者,10人2个,25人4个
                            CastSummon(0, 0, RAID_MODE(2, 4));
                            break;
                        default:
                            // 第4次及以后:大量召唤勇士和守护者
                            CastSummon(0, RAID_MODE(5, 10), RAID_MODE(5, 10));
                            break;
                    }
                    // 安排下一波召唤,30-45秒后随机时间
                    events.Repeat(randtime(Seconds(30), Seconds(45)));
                    break;
                case EVENT_GROUND:
                    // 增加阳台次数计数
                    ++balconyCount;

                    // 传送回地面
                    DoCastAOE(SPELL_TELEPORT_BACK);
                    Talk(EMOTE_TELEPORT_2);

                    // 进入地面阶段
                    EnterPhaseGround();
                    break;
                case EVENT_GROUND_ATTACKABLE:
                    // 恢复可攻击和主动攻击状态
                    me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                    me->SetReactState(REACT_AGGRESSIVE);
                    break;
            }

            // 如果开始施法,则退出事件处理循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 只有在地面阶段才进行近战攻击
        if (events.IsInPhase(PHASE_GROUND))
        {
            /*
             * 闪烁后移动追踪的workaround
             * 如果没有这个处理,诺斯闪烁后会站在原地,除非目标移动
             */
            if (justBlinked && me->GetVictim() && !me->IsWithinMeleeRange(me->EnsureVictim()))
            {
                // 清空移动控制器并重新追踪目标
                me->GetMotionMaster()->Clear();
                me->GetMotionMaster()->MoveChase(me->EnsureVictim());
                justBlinked = false;
            }
            else
                DoMeleeAttackIfReady();
        }
    }

    private:
        uint32 balconyCount;    ///< 阳台阶段计数器,用于确定阶段持续时间和召唤组合

        bool justBlinked;       ///< 是否刚刚闪烁,用于修复移动追踪问题

        uint32 _SummonWarriorSpells[N_WARRIOR_SPELLS];      ///< 骷髅战士召唤技能数组副本
        uint32 _SummonChampionSpells[N_CHAMPION_SPELLS];    ///< 骷髅勇士召唤技能数组副本
        uint32 _SummonGuardianSpells[N_GUARDIAN_SPELLS];    ///< 骷髅守护者召唤技能数组副本
};

/**
 * @brief 注册诺斯BOSS脚本
 *
 * 注册BOSS AI到脚本系统
 */
void AddSC_boss_noth()
{
    RegisterNaxxramasCreatureAI(boss_noth);
}
