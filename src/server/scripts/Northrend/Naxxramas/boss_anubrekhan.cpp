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
 * @file boss_anubrekhan.cpp
 * @brief 纳克萨玛斯副本BOSS - 阿努布雷坎(Anub'rekhan)的AI脚本
 *
 * 模块职责:
 * - 实现阿努布雷坎BOSS的战斗逻辑
 * - 管理地穴守卫(Crypt Guard)的生成和死亡处理
 * - 处理尸体甲虫(Corpse Scarab)的生成机制
 * - 实现蝗虫群(Locust Swarm)阶段的转换
 * - 管理穿刺(Impale)技能的释放
 *
 * 战斗机制:
 * - 阿努布雷坎是一只地穴领主,主要技能包括穿刺和蝗虫群
 * - 蝗虫群阶段BOSS会沉默并周期性造成自然伤害,期间无法进行近战攻击
 * - 击杀玩家或地穴守卫死亡后会产生尸体甲虫
 * - 10人模式下会有延迟生成的地穴守卫,25人模式初始就有多个地穴守卫
 * - 击杀BOSS后启动20分钟内击杀麦克斯纳的成就计时器
 */

#include "ScriptMgr.h"
#include "Containers.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "naxxramas.h"
#include "Player.h"
#include "ScriptedCreature.h"

/**
 * @brief 阿努布雷坎的台词枚举
 */
enum AnubSays
{
    SAY_AGGRO           = 0,    // 开战台词
    SAY_GREET           = 1,    // 问候台词(玩家进入区域触发)
    SAY_SLAY            = 2,    // 击杀玩家台词

    EMOTE_LOCUST        = 3     // 蝗虫群表情
};

/**
 * @brief 地穴守卫的表情枚举
 */
enum GuardSays
{
    EMOTE_FRENZY        = 0,    // 狂暴表情
    EMOTE_SPAWN         = 1,    // 生成表情
    EMOTE_SCARAB        = 2     // 尸体甲虫表情
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_IMPALE                    = 1,        // 对随机目标施放穿刺技能
    EVENT_LOCUST,                               // 开始引导蝗虫群
    EVENT_LOCUST_ENDS,                          // 蝗虫群消散
    EVENT_SPAWN_GUARD,                          // 10人模式专用 - 地穴守卫延迟生成; 同时也用于蝗虫群期间的地穴守卫生成
    EVENT_SCARABS,                              // 生成尸体甲虫
    EVENT_BERSERK                               // 狂暴
};

/**
 * @brief 技能ID枚举
 */
enum Spells
{
    SPELL_IMPALE                    = 28783,    // 穿刺技能 - 25人模式: 56090
    SPELL_LOCUST_SWARM              = 28785,    // 蝗虫群技能 - 25人模式: 54021
    SPELL_SUMMON_CORPSE_SCARABS_PLR = 29105,    // 在玩家尸体上生成5只尸体甲虫
    SPELL_SUMMON_CORPSE_SCARABS_MOB = 28864,    // 在地穴守卫尸体上生成10只尸体甲虫
    SPELL_BERSERK                   = 27680     // 狂暴技能
};

/**
 * @brief 生成组枚举
 */
enum SpawnGroups
{
    GROUP_INITIAL_25M       = 1,    // 25人模式初始生成组
    GROUP_SINGLE_SPAWN      = 2     // 单个生成组
};

/**
 * @brief 杂项枚举
 */
enum Misc
{
    ACHIEV_TIMED_START_EVENT                      = 9891    // 成就计时开始事件ID
};

/**
 * @brief 战斗阶段枚举
 */
enum Phases
{
    PHASE_NORMAL    = 1,    // 正常阶段 - BOSS可进行近战攻击和施放穿刺
    PHASE_SWARM             // 蝗虫群阶段 - BOSS引导蝗虫群,无法近战攻击
};

/**
 * @struct boss_anubrekhan
 * @brief 阿努布雷坎BOSS的AI实现
 *
 * 继承自BossAI,实现了阿努布雷坎的完整战斗逻辑,包括:
 * - 穿刺技能的施放
 * - 蝗虫群阶段的管理
 * - 地穴守卫的生成和管理
 * - 尸体甲虫的生成机制
 */
struct boss_anubrekhan : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature BOSS生物对象指针
     */
    boss_anubrekhan(Creature* creature) : BossAI(creature, BOSS_ANUBREKHAN) { }

    /**
     * @brief 召唤地穴守卫
     *
     * 在25人模式下召唤初始的地穴守卫
     * 这些守卫在BOSS初始生成时就会出现
     */
    void SummonGuards()
    {
        if (Is25ManRaid())
            me->SummonCreatureGroup(GROUP_INITIAL_25M);
    }

    /**
     * @brief 初始化AI
     *
     * 在BOSS非死亡状态且副本状态未完成时,执行重置并召唤守卫
     * 调用时机: 生物创建时或副本重置时
     */
    void InitializeAI() override
    {
        if (!me->isDead() && instance->GetBossState(BOSS_ANUBREKHAN) != DONE)
        {
            Reset();
            SummonGuards();
        }
    }

    /**
     * @brief 重置BOSS状态
     *
     * 清空事件计时器和地穴守卫尸体列表
     * 调用时机: BOSS脱离战斗或副本重置时
     */
    void Reset() override
    {
        _Reset();
        guardCorpses.clear();
    }

    /**
     * @brief BOSS返回出生点
     *
     * 当BOSS脱离战斗返回出生点后,重新召唤地穴守卫
     * 调用时机: BOSS脱离战斗返回出生点时
     */
    void JustReachedHome() override
    {
        _JustReachedHome();
        SummonGuards();
    }

    /**
     * @brief 生物被召唤时的处理
     * @param summon 被召唤的生物对象
     *
     * 当地穴守卫在战斗中被召唤时,播放生成表情
     * 调用时机: 召唤生物时
     */
    void JustSummoned(Creature* summon) override
    {
        BossAI::JustSummoned(summon);

        if (me->IsInCombat())
            if (summon->GetEntry() == NPC_CRYPT_GUARD)
                summon->AI()->Talk(EMOTE_SPAWN, me);
    }

    /**
     * @brief 召唤生物死亡时的处理
     * @param summon 死亡的召唤生物
     * @param killer 击杀者
     *
     * 当地穴守卫死亡时,将其GUID加入尸体列表,用于后续生成尸体甲虫
     * 调用时机: 召唤生物死亡时
     */
    void SummonedCreatureDies(Creature* summon, Unit* killer) override
    {
        BossAI::SummonedCreatureDies(summon, killer);

        if (summon->GetEntry() == NPC_CRYPT_GUARD)
            guardCorpses.insert(summon->GetGUID());
    }

    /**
     * @brief 召唤生物消失时的处理
     * @param summon 消失的召唤生物
     *
     * 当地穴守卫消失时,从尸体列表中移除
     * 调用时机: 召唤生物消失或刷新时
     */
    void SummonedCreatureDespawn(Creature* summon) override
    {
        BossAI::SummonedCreatureDespawn(summon);

        if (summon->GetEntry() == NPC_CRYPT_GUARD)
            guardCorpses.erase(summon->GetGUID());
    }

    /**
     * @brief 击杀单位的处理
     * @param victim 被击杀的单位
     *
     * 当击杀玩家时,在玩家尸体上生成5只尸体甲虫
     * 并播放击杀台词
     * 调用时机: BOSS击杀单位时
     */
    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            victim->CastSpell(victim, SPELL_SUMMON_CORPSE_SCARABS_PLR, me->GetGUID());

        Talk(SAY_SLAY);
    }

    /**
     * @brief BOSS死亡处理
     * @param killer 击杀者
     *
     * 启动20分钟内击杀麦克斯纳的成就计时器
     * 调用时机: BOSS死亡时
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();

        // 启动成就计时器(在20分钟内击杀麦克斯纳)
        instance->DoStartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, ACHIEV_TIMED_START_EVENT);
    }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标
     *
     * 初始化战斗事件计时器,包括穿刺、尸体甲虫、蝗虫群和狂暴
     * 10人模式额外安排地穴守卫的延迟生成
     * 调用时机: BOSS进入战斗时
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);

        // 让所有召唤物进入战斗
        summons.DoZoneInCombat();

        // 设置战斗阶段为正常阶段
        events.SetPhase(PHASE_NORMAL);
        // 安排穿刺技能 - 10-20秒后随机时间施放
        events.ScheduleEvent(EVENT_IMPALE, randtime(Seconds(10), Seconds(20)), 0, PHASE_NORMAL);
        // 安排尸体甲虫生成 - 20-30秒后随机时间
        events.ScheduleEvent(EVENT_SCARABS, randtime(Seconds(20), Seconds(30)), 0, PHASE_NORMAL);
        // 安排蝗虫群 - 1分40秒到2分钟后随机时间
        events.ScheduleEvent(EVENT_LOCUST, Minutes(1)+randtime(Seconds(40), Seconds(60)), 0, PHASE_NORMAL);
        // 安排狂暴 - 10分钟后
        events.ScheduleEvent(EVENT_BERSERK, 10min);

        // 10人模式下安排延迟生成的地穴守卫
        if (!Is25ManRaid())
            events.ScheduleEvent(EVENT_SPAWN_GUARD, randtime(Seconds(15), Seconds(20)));
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 处理所有战斗事件的执行,包括穿刺、蝗虫群、尸体甲虫生成等
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

        // 处理所有到期的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_IMPALE:
                    // 如果距离下一次蝗虫群少于5秒,则跳过穿刺,避免坦克连续被控
                    if (events.GetTimeUntilEvent(EVENT_LOCUST) < 5s)
                        break; // 不要在蝗虫群前穿刺坦克
                    // 随机选择目标施放穿刺
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_IMPALE);
                    else
                        EnterEvadeMode(); // 没有有效目标则脱离战斗

                    // 安排下一次穿刺,10-20秒后
                    events.Repeat(randtime(Seconds(10), Seconds(20)));
                    break;
                case EVENT_SCARABS:
                    // 如果有地穴守卫尸体,随机选择一个生成尸体甲虫
                    if (!guardCorpses.empty())
                    {
                        // 随机选择一个尸体
                        if (ObjectGuid target = Trinity::Containers::SelectRandomContainerElement(guardCorpses))
                            if (Creature* creatureTarget = ObjectAccessor::GetCreature(*me, target))
                            {
                                // 在尸体位置生成10只尸体甲虫
                                creatureTarget->CastSpell(creatureTarget, SPELL_SUMMON_CORPSE_SCARABS_MOB, me->GetGUID());
                                creatureTarget->AI()->Talk(EMOTE_SCARAB);
                                creatureTarget->DespawnOrUnsummon(); // 移除尸体
                            }
                    }
                    // 安排下一次尸体甲虫生成,40-60秒后
                    events.Repeat(randtime(Seconds(40), Seconds(60)));
                    break;
                case EVENT_LOCUST:
                    // 播放蝗虫群表情
                    Talk(EMOTE_LOCUST);
                    // 切换到蝗虫群阶段
                    events.SetPhase(PHASE_SWARM);
                    // 施放蝗虫群技能
                    DoCast(me, SPELL_LOCUST_SWARM);

                    // 3秒后生成一个地穴守卫
                    events.ScheduleEvent(EVENT_SPAWN_GUARD, 3s);
                    // 安排蝗虫群结束事件,10人模式19秒,25人模式23秒
                    events.ScheduleEvent(EVENT_LOCUST_ENDS, RAID_MODE(Seconds(19), Seconds(23)));
                    // 安排下一次蝗虫群,1分30秒后
                    events.Repeat(Minutes(1)+Seconds(30));
                    break;
                case EVENT_LOCUST_ENDS:
                    // 切换回正常阶段
                    events.SetPhase(PHASE_NORMAL);
                    // 重新安排正常阶段的技能
                    events.ScheduleEvent(EVENT_IMPALE, randtime(Seconds(10), Seconds(20)), 0, PHASE_NORMAL);
                    events.ScheduleEvent(EVENT_SCARABS, randtime(Seconds(20), Seconds(30)), 0, PHASE_NORMAL);
                    break;
                case EVENT_SPAWN_GUARD:
                    // 生成地穴守卫(用于蝗虫群期间的守卫和10人模式的延迟守卫)
                    me->SummonCreatureGroup(GROUP_SINGLE_SPAWN);
                    break;
                case EVENT_BERSERK:
                    // 施放狂暴技能
                    DoCast(me, SPELL_BERSERK, true);
                    // 每10分钟叠加一次狂暴
                    events.ScheduleEvent(EVENT_BERSERK, 10min);
                    break;
            }
        }

        // 只有在正常阶段才进行近战攻击
        if (events.IsInPhase(PHASE_NORMAL))
            DoMeleeAttackIfReady();
    }

    private:
        GuidSet guardCorpses;    ///< 地穴守卫尸体GUID集合,用于尸体甲虫生成机制
};

/**
 * @class at_anubrekhan_entrance
 * @brief 阿努布雷坎入口区域触发脚本
 *
 * 当玩家首次进入阿努布雷坎的房间区域时,触发BOSS的问候台词
 * 继承自OnlyOnceAreaTriggerScript,确保每个玩家只触发一次
 */
class at_anubrekhan_entrance : public OnlyOnceAreaTriggerScript
{
    public:
        /**
         * @brief 构造函数
         */
        at_anubrekhan_entrance() : OnlyOnceAreaTriggerScript("at_anubrekhan_entrance") { }

        /**
         * @brief 处理区域触发
         * @param player 触发区域的玩家
         * @param areaTrigger 区域触发数据
         * @return true表示处理成功
         *
         * 当玩家进入阿努布雷坎房间且BOSS未被激活时,播放问候台词
         * 调用时机: 玩家首次进入指定区域时
         */
        bool TryHandleOnce(Player* player, AreaTriggerEntry const* /*areaTrigger*/) override
        {
            InstanceScript* instance = player->GetInstanceScript();
            // 检查副本状态,如果BOSS已激活则不触发
            if (!instance || instance->GetBossState(BOSS_ANUBREKHAN) != NOT_STARTED)
                return true;

            // 获取阿努布雷坎并播放问候台词
            if (Creature* anub = ObjectAccessor::GetCreature(*player, instance->GetGuidData(DATA_ANUBREKHAN)))
                anub->AI()->Talk(SAY_GREET);

            return true;
        }
};

/**
 * @brief 注册阿努布雷坎BOSS脚本
 *
 * 注册BOSS AI和区域触发脚本到脚本系统
 */
void AddSC_boss_anubrekhan()
{
    RegisterNaxxramasCreatureAI(boss_anubrekhan);

    new at_anubrekhan_entrance();
}
