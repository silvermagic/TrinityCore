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
 * @file boss_magtheridon.cpp
 * @brief 玛瑟里顿Boss脚本模块
 *
 * 本模块实现玛瑟里顿巢穴副本的最终Boss——玛瑟里顿的战斗逻辑。
 * 玛瑟里顿是一场多阶段的团队Boss战,包含独特的立方体控制机制。
 *
 * 战斗机制:
 * 阶段1(禁锢阶段): Boss被5个引导者禁锢,玩家需要击杀引导者
 * 阶段2(战斗阶段): Boss释放后开始主动攻击
 * 阶段3(崩塌阶段): Boss血量低于30%时,房间开始崩塌
 *
 * 主要技能:
 * - 顺劈斩: 对前方敌人造成物理伤害
 * - 烈焰: 在地面生成火焰区域
 * - 冲击新星: 对周围所有玩家造成伤害(需用立方体打断)
 * - 地震: 对周围玩家造成伤害并击退
 * - 禁锢机制: 5名玩家同时使用立方体可禁锢Boss
 */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "magtheridons_lair.h"
#include "PassiveAI.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellScript.h"

/**
 * @brief 对话和表情枚举
 */
enum Yells
{
    SAY_TAUNT           = 0,    // 嘲讽对话(战斗前定期喊话)
    SAY_FREE            = 1,    // 释放对话(Boss脱离禁锢时)
    SAY_SLAY            = 2,    // 击杀玩家时的对话
    SAY_BANISHED        = 3,    // 被禁锢时的对话(玩家使用立方体)
    SAY_COLLAPSE        = 4,    // 崩塌对话(血量低于30%时)
    SAY_DEATH           = 5,    // 死亡时的对话
    EMOTE_WEAKEN        = 6,    // 削弱表情(引导者被攻击时)
    EMOTE_NEARLY_FREE   = 7,    // 即将释放表情(引导者战斗期间)
    EMOTE_BREAKS_FREE   = 8,    // 释放表情(Boss脱离禁锢时)
    EMOTE_BLAST_NOVA    = 9     // 冲击新星表情(施放冲击新星前)
};

/**
 * @brief 技能ID枚举
 */
enum Spells
{
    // 玛瑟里顿技能
    SPELL_BLAST_NOVA            = 30616,    // 冲击新星 - 可被立方体打断的范围伤害
    SPELL_CLEAVE                = 30619,    // 顺劈斩 - 对前方敌人造成伤害
    SPELL_BLAZE_TARGET          = 30541,    // 烈焰目标 - 选择烈焰目标
    SPELL_CAMERA_SHAKE          = 36455,    // 镜头震动 - 房间崩塌时的视觉效果
    SPELL_BERSERK               = 27680,    // 狂暴 - 20分钟后Boss进入狂暴状态
    SPELL_QUAKE                 = 30657,    // 地震 - 对周围玩家造成伤害并击退
    SPELL_DEBRIS_SERVERSIDE     = 30630,    // 残骸(服务器端) - 召唤落石

    // 玩家或蝎尾立方体技能
    SPELL_SHADOW_CAGE           = 30168,    // 暗影牢笼 - 禁锢Boss
    SPELL_SHADOW_GRASP          = 30410,    // 暗影之握 - 玩家使用立方体时的效果
    SPELL_MIND_EXHAUSTION       = 44032,    // 精神疲劳 - 使用立方体后的负面效果

    // 地狱火团队触发器技能
    SPELL_SHADOW_GRASP_VISUAL   = 30166,    // 暗影之握视觉效果

    // 地狱火引导者技能
    SPELL_SHADOW_CAGE_C         = 30205,    // 暗影牢笼(引导者) - 禁锢Boss
    SPELL_SHADOW_GRASP_C        = 30207,    // 暗影之握(引导者) - 引导效果
    SPELL_SHADOW_BOLT_VOLLEY    = 30510,    // 暗影箭齐射
    SPELL_DARK_MENDING          = 30528,    // 黑暗治疗 - 治疗友方单位
    SPELL_BURNING_ABYSSAL       = 30511,    // 燃烧深渊 - 召唤深渊恶魔
    SPELL_SOUL_TRANSFER         = 30531,    // 灵魂转移 - 死亡时转移给Boss
    SPELL_FEAR                  = 30530,    // 恐惧

    // 世界触发器技能
    SPELL_DEBRIS_KNOCKDOWN      = 36449,    // 残骸击倒 - 崩塌时击退玩家

    // 玛瑟里顿房间技能
    SPELL_DEBRIS_VISUAL         = 30632,    // 残骸视觉效果
    SPELL_DEBRIS_DAMAGE         = 30631,    // 残骸伤害

    // 目标触发器技能
    SPELL_BLAZE                 = 30542     // 烈焰 - 在地面生成火焰
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    // 玛瑟里顿事件
    EVENT_BERSERK = 1,          // 狂暴事件
    EVENT_CLEAVE,               // 顺劈斩事件
    EVENT_BLAZE,                // 烈焰事件
    EVENT_BLAST_NOVA,           // 冲击新星事件
    EVENT_QUAKE,                // 地震事件
    EVENT_START_FIGHT,          // 开始战斗事件
    EVENT_RELEASED,             // 释放事件(Boss脱离禁锢)
    EVENT_COLLAPSE,             // 崩塌事件
    EVENT_DEBRIS_KNOCKDOWN,     // 残骸击倒事件
    EVENT_DEBRIS,               // 残骸事件
    EVENT_NEARLY_EMOTE,         // 即将释放表情事件
    EVENT_TAUNT,                // 嘲讽事件
    // 地狱火引导者事件
    EVENT_SHADOWBOLT,           // 暗影箭事件
    EVENT_FEAR,                 // 恐惧事件
    EVENT_CHECK_FRIEND,         // 检查友方事件(治疗)
    EVENT_DARK_MENDING,         // 黑暗治疗冷却事件
    EVENT_ABYSSAL               // 深渊恶魔事件
};

/**
 * @brief 战斗阶段枚举
 */
enum Phases
{
    PHASE_BANISH = 1,   // 禁锢阶段 - Boss被引导者禁锢
    PHASE_1,            // 阶段1 - 引导者战斗阶段
    PHASE_2,            // 阶段2 - Boss主动战斗阶段
    PHASE_3             // 阶段3 - 房间崩塌阶段
};

/**
 * @brief 其他常量枚举
 */
enum Misc
{
    SUMMON_GROUP_CHANNELERS       = 1,  // 引导者召唤组ID
    ACTION_START_CHANNELERS_EVENT = 2   // 启动引导者事件动作
};

/**
 * @brief 玛瑟里顿Boss AI结构体
 *
 * 实现玛瑟里顿的多阶段战斗逻辑:
 * - 禁锢阶段:等待引导者被击杀
 * - 战斗阶段:使用各种技能攻击玩家
 * - 崩塌阶段:房间崩塌并持续召唤残骸
 */
struct boss_magtheridon : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物指针
     *
     * 初始化Boss AI,设置引导者计数为5
     */
    boss_magtheridon(Creature* creature) : BossAI(creature, DATA_MAGTHERIDON), _channelersCount(5) { }

    /**
     * @brief 重置Boss状态
     *
     * 恢复Boss到初始状态:
     * 1. 调用基类重置函数
     * 2. 对自己施放暗影牢笼(禁锢状态)
     * 3. 召唤5个引导者
     * 4. 设置阶段为禁锢阶段
     * 5. 重置引导者计数
     * 6. 启动嘲讽事件(定期喊话)
     *
     * @调用时机 战斗结束、团灭重置、副本重置时
     */
    void Reset() override
    {
        _Reset();
        DoCastSelf(SPELL_SHADOW_CAGE_C);  // 施放暗影牢笼,处于禁锢状态
        me->SummonCreatureGroup(SUMMON_GROUP_CHANNELERS);  // 召唤5个引导者
        events.SetPhase(PHASE_BANISH);  // 设置为禁锢阶段
        _channelersCount = 5;  // 重置引导者计数
        events.ScheduleEvent(EVENT_TAUNT, Minutes(4), Minutes(5));  // 4-5分钟后嘲讽
    }

    /**
     * @brief 战斗开始
     *
     * 当所有引导者被击杀后触发:
     * 1. 取消战斗开始相关事件
     * 2. 6秒后释放Boss
     * 3. 播放释放表情和对话
     * 4. 移除暗影牢笼效果
     *
     * @调用时机 所有引导者被击杀时(由SummonedCreatureDies调用)
     */
    void CombatStart()
    {
        events.CancelEvent(EVENT_START_FIGHT);
        events.CancelEvent(EVENT_NEARLY_EMOTE);
        events.ScheduleEvent(EVENT_RELEASED, 6s);  // 6秒后释放
        Talk(EMOTE_BREAKS_FREE, me);  // 释放表情
        Talk(SAY_FREE);  // 释放对话
        me->RemoveAurasDueToSpell(SPELL_SHADOW_CAGE_C);  // 移除暗影牢笼
    }

    /**
     * @brief 进入逃避模式
     * @param why 逃避原因
     *
     * 战斗失败时重置副本状态:
     * 1. 清除所有召唤物
     * 2. 重置事件系统
     * 3. 禁用立方体和崩塌机制
     * 4. 执行逃避后的消散处理
     *
     * @调用时机 团灭或战斗重置时
     */
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        summons.DespawnAll();  // 清除所有召唤物
        events.Reset();  // 重置事件系统
        instance->SetData(DATA_MANTICRON_CUBE, ACTION_DISABLE);  // 禁用立方体
        instance->SetData(DATA_COLLAPSE, ACTION_DISABLE);  // 禁用崩塌阶段1
        instance->SetData(DATA_COLLAPSE_2, ACTION_DISABLE);  // 禁用崩塌阶段2

        _DespawnAtEvade();  // 执行消散处理
    }

    /**
     * @brief 召唤物死亡时调用
     * @param summon 死亡的召唤物
     * @param killer 击杀者(未使用)
     *
     * 监控引导者死亡事件:
     * - 如果死亡的是引导者且处于阶段1
     * - 减少引导者计数
     * - 所有引导者死亡后触发战斗开始
     *
     * @调用时机 任何召唤物死亡时
     */
    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        if (summon->GetEntry() == NPC_HELLFIRE_CHANNELLER && events.IsInPhase(PHASE_1))
        {
            _channelersCount--;  // 减少引导者计数

            // 所有引导者已击杀,开始战斗
            if (_channelersCount == 0)
                CombatStart();
        }
    }

    /**
     * @brief 执行动作
     * @param action 动作ID
     *
     * 处理外部触发的动作:
     * - ACTION_START_CHANNELERS_EVENT: 引导者被攻击,开始战斗流程
     *
     * 触发后的处理:
     * 1. 切换到阶段1
     * 2. 播放削弱表情
     * 3. 让所有引导者进入战斗
     * 4. 设置2分钟后强制开始战斗(防卡怪)
     * 5. 1分钟后播放即将释放表情
     * 6. 停止嘲讽事件
     * 7. 设置Boss状态为进行中
     * 8. 启用看守者召唤
     *
     * @调用时机 第一个引导者进入战斗时(由引导者AI调用)
     */
    void DoAction(int32 action) override
    {
        if (action == ACTION_START_CHANNELERS_EVENT && events.IsInPhase(PHASE_BANISH))
        {
            events.SetPhase(PHASE_1);  // 切换到阶段1
            Talk(EMOTE_WEAKEN, me);  // 播放削弱表情
            summons.DoZoneInCombat(NPC_HELLFIRE_CHANNELLER);  // 所有引导者进入战斗
            events.ScheduleEvent(EVENT_START_FIGHT, 2min);  // 2分钟后强制开始
            events.ScheduleEvent(EVENT_NEARLY_EMOTE, 1min);  // 1分钟后播放表情
            events.CancelEvent(EVENT_TAUNT);  // 取消嘲讽事件
            instance->SetBossState(DATA_MAGTHERIDON, IN_PROGRESS);  // 设置Boss状态
            instance->SetData(DATA_CALL_WARDERS, ACTION_ENABLE);  // 启用看守者
        }
    }

    /**
     * @brief 法术命中时调用
     * @param caster 施法者(未使用)
     * @param spellInfo 法术信息
     *
     * 当暗影牢笼法术命中时播放被禁锢对话
     *
     * @调用时机 任何法术命中Boss时
     */
    void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_SHADOW_CAGE)
            Talk(SAY_BANISHED);  // 播放被禁锢对话
    }

    /**
     * @brief 受到伤害时调用
     * @param who 攻击者(未使用)
     * @param damage 伤害值(引用)
     * @param damageType 伤害类型(未使用)
     * @param spellInfo 法术信息(未使用)
     *
     * 监控Boss血量,当血量低于30%时触发崩塌阶段:
     * 1. 切换到阶段3
     * 2. 设置为被动状态
     * 3. 停止攻击
     * 4. 播放崩塌对话
     * 5. 启用房间崩塌
     * 6. 施放镜头震动效果
     * 7. 6秒后触发崩塌事件
     *
     * @调用时机 Boss受到任何伤害时
     */
    void DamageTaken(Unit* /*who*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 血量低于30%且未进入阶段3时触发
        if (me->HealthBelowPctDamaged(30, damage) && !events.IsInPhase(PHASE_3))
        {
            events.SetPhase(PHASE_3);  // 切换到阶段3
            me->SetReactState(REACT_PASSIVE);  // 设置为被动状态
            me->AttackStop();  // 停止攻击
            Talk(SAY_COLLAPSE);  // 播放崩塌对话
            instance->SetData(DATA_COLLAPSE, ACTION_ENABLE);  // 启用崩塌
            DoCastAOE(SPELL_CAMERA_SHAKE);  // 镜头震动效果
            events.ScheduleEvent(EVENT_COLLAPSE, 6s);  // 6秒后触发崩塌
        }
    }

    /**
     * @brief Boss死亡时调用
     * @param killer 击杀者(未使用)
     *
     * 执行死亡处理:
     * 1. 播放死亡对话
     * 2. 调用基类死亡函数
     * 3. 禁用立方体
     *
     * @调用时机 Boss血量降为0时
     */
    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_DEATH);  // 播放死亡对话
        _JustDied();  // 调用基类死亡处理
        instance->SetData(DATA_MANTICRON_CUBE, ACTION_DISABLE);  // 禁用立方体
    }

    /**
     * @brief 击杀单位时调用
     * @param who 被击杀的单位
     *
     * 击杀玩家时播放击杀对话
     *
     * @调用时机 Boss击杀任何单位时
     */
    void KilledUnit(Unit* who) override
    {
        if (who->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_SLAY);  // 播放击杀对话
    }

    /**
     * @brief 更新AI状态(每帧调用)
     * @param diff 距离上次调用的时间(毫秒)
     *
     * 主循环函数,处理玛瑟里顿的所有行为逻辑:
     *
     * 事件处理:
     * - EVENT_BERSERK: 20分钟后进入狂暴状态
     * - EVENT_CLEAVE: 顺劈斩,10秒冷却
     * - EVENT_BLAZE: 烈焰,20秒冷却
     * - EVENT_QUAKE: 地震,60秒冷却
     * - EVENT_BLAST_NOVA: 冲击新星,55秒冷却
     * - EVENT_START_FIGHT: 引导者战斗2分钟后强制开始
     * - EVENT_RELEASED: Boss释放,进入阶段2
     * - EVENT_COLLAPSE: 触发崩塌阶段
     * - EVENT_DEBRIS_KNOCKDOWN: 残骸击倒
     * - EVENT_DEBRIS: 持续召唤残骸
     * - EVENT_NEARLY_EMOTE: 即将释放表情
     * - EVENT_TAUNT: 定期嘲讽
     *
     * @调用时机 游戏主循环每帧调用
     * @性能注意事项 高频调用函数,避免复杂计算
     */
    void UpdateAI(uint32 diff) override
    {
        // 禁锢和阶段1不需要目标,其他阶段需要目标
        if (!events.IsInPhase(PHASE_BANISH) && !events.IsInPhase(PHASE_1) && !UpdateVictim())
            return;

        events.Update(diff);

        // 正在施法时不执行其他动作
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理事件队列
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_BERSERK:
                    // 进入狂暴状态
                    DoCastSelf(SPELL_BERSERK);
                    break;
                case EVENT_CLEAVE:
                    // 顺劈斩攻击
                    DoCastVictim(SPELL_CLEAVE);
                    events.Repeat(Seconds(10));  // 10秒冷却
                    break;
                case EVENT_BLAZE:
                    // 对随机目标施放烈焰
                    DoCastAOE(SPELL_BLAZE_TARGET, { SPELLVALUE_MAX_TARGETS, 1 });
                    events.Repeat(Seconds(20));  // 20秒冷却
                    break;
                case EVENT_QUAKE:
                    // 地震攻击周围玩家
                    DoCastAOE(SPELL_QUAKE, { SPELLVALUE_MAX_TARGETS, 5 });
                    events.Repeat(Seconds(60));  // 60秒冷却
                    break;
                case EVENT_START_FIGHT:
                    // 引导者战斗2分钟后强制开始
                    CombatStart();
                    break;
                case EVENT_RELEASED:
                    // Boss释放,进入阶段2
                    me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);  // 移除不可交互标志
                    me->SetImmuneToPC(false);  // 取消对玩家的免疫
                    DoZoneInCombat();  // 进入战斗
                    events.SetPhase(PHASE_2);  // 切换到阶段2
                    instance->SetData(DATA_MANTICRON_CUBE, ACTION_ENABLE);  // 启用立方体
                    // 安排技能事件
                    events.ScheduleEvent(EVENT_CLEAVE, 10s);  // 10秒后顺劈斩
                    events.ScheduleEvent(EVENT_BLAST_NOVA, 1min);  // 1分钟后冲击新星
                    events.ScheduleEvent(EVENT_BLAZE, 20s);  // 20秒后烈焰
                    events.ScheduleEvent(EVENT_QUAKE, 35s);  // 35秒后地震
                    events.ScheduleEvent(EVENT_BERSERK, 20min);  // 20分钟后狂暴
                    break;
                case EVENT_COLLAPSE:
                    // 启用崩塌阶段2
                    instance->SetData(DATA_COLLAPSE_2, ACTION_ENABLE);
                    events.ScheduleEvent(EVENT_DEBRIS_KNOCKDOWN, 4s);  // 4秒后残骸击倒
                    break;
                case EVENT_DEBRIS_KNOCKDOWN:
                    // 残骸击倒(崩塌开始)
                    if (Creature* trigger = instance->GetCreature(DATA_WORLD_TRIGGER))
                    {
                        trigger->CastSpell(trigger, SPELL_DEBRIS_KNOCKDOWN, true);  // 击退玩家
                        me->SetReactState(REACT_AGGRESSIVE);  // 恢复攻击状态
                        events.ScheduleEvent(EVENT_DEBRIS, 20s);  // 20秒后开始落石
                    }
                    break;
                case EVENT_DEBRIS:
                    // 持续召唤残骸
                    DoCastAOE(SPELL_DEBRIS_SERVERSIDE);
                    events.Repeat(Seconds(20));  // 每20秒一次
                    break;
                case EVENT_NEARLY_EMOTE:
                    // 播放即将释放表情
                    Talk(EMOTE_NEARLY_FREE, me);
                    break;
                case EVENT_BLAST_NOVA:
                    // 冲击新星(关键技能,需要用立方体打断)
                    Talk(EMOTE_BLAST_NOVA, me);  // 播放表情提示玩家
                    DoCastAOE(SPELL_BLAST_NOVA);
                    events.Repeat(Seconds(55));  // 55秒冷却
                    break;
                case EVENT_TAUNT:
                    // 定期嘲讽(战斗前)
                    Talk(SAY_TAUNT);
                    events.Repeat(Minutes(4), Minutes(5));  // 4-5分钟后再次嘲讽
                    break;
                default:
                    break;
            }

            // 施法时不继续执行
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    uint8 _channelersCount;  ///< 剩余引导者数量,用于判断何时释放Boss
};

/**
 * @brief 地狱火引导者AI结构体
 *
 * 引导者是玛瑟里顿战斗中的重要小怪:
 * - 初始状态持续引导暗影之握禁锢Boss
 * - 被攻击时触发战斗流程
 * - 拥有暗影箭齐射、恐惧、治疗等技能
 * - 死亡时触发Boss的引导者计数减少
 */
struct npc_hellfire_channeler : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物指针
     *
     * 初始化实例脚本引用和治疗标志
     */
    npc_hellfire_channeler(Creature* creature) : ScriptedAI(creature), _instance(me->GetInstanceScript()), _canCastDarkMending(true)
    {
        SetBoundary(_instance->GetBossBoundary(DATA_MAGTHERIDON));  // 设置战斗边界
    }

    /**
     * @brief 重置引导者状态
     *
     * 恢复引导者到初始状态:
     * 1. 重置事件系统
     * 2. 施放暗影之握(禁锢Boss)
     * 3. 设置为防御反应状态
     *
     * @调用时机 战斗结束、团灭重置、副本重置时
     */
    void Reset() override
    {
        _events.Reset();
        DoCastSelf(SPELL_SHADOW_GRASP_C);  // 施放暗影之握
        me->SetReactState(REACT_DEFENSIVE);  // 防御状态
    }

    /**
     * @brief 进入战斗时调用
     * @param who 攻击者(未使用)
     *
     * 当引导者被攻击时:
     * 1. 中断正在施放的法术
     * 2. 通知Boss开始战斗流程
     * 3. 安排技能事件
     *
     * @调用时机 引导者被玩家攻击时
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        me->InterruptNonMeleeSpells(false);  // 中断引导法术

        // 通知Boss开始战斗流程
        if (Creature* magtheridon = _instance->GetCreature(DATA_MAGTHERIDON))
            magtheridon->AI()->DoAction(ACTION_START_CHANNELERS_EVENT);

        // 安排技能事件
        _events.ScheduleEvent(EVENT_SHADOWBOLT, 20s);  // 20秒后暗影箭齐射
        _events.ScheduleEvent(EVENT_CHECK_FRIEND, 1s);  // 1秒后检查友方(治疗)
        _events.ScheduleEvent(EVENT_ABYSSAL, 30s);  // 30秒后召唤深渊恶魔
        _events.ScheduleEvent(EVENT_FEAR, 15s, 20s);  // 15-20秒后恐惧

    }

    /**
     * @brief 引导者死亡时调用
     * @param killer 击杀者(未使用)
     *
     * 死亡时:
     * 1. 施放灵魂转移(转移给Boss)
     * 2. 再次触发Boss战斗事件(确保触发)
     *
     * @调用时机 引导者血量降为0时
     */
    void JustDied(Unit* /*killer*/) override
    {
        DoCastAOE(SPELL_SOUL_TRANSFER);  // 灵魂转移

        // 再次触发Boss战斗事件(防卡怪,Cata+版本需要)
        // Channelers killed by "Hit Kill" need trigger combat event too. It's needed for Cata+
        if (Creature* magtheridon = _instance->GetCreature(DATA_MAGTHERIDON))
            magtheridon->AI()->DoAction(ACTION_START_CHANNELERS_EVENT);
    }

    /**
     * @brief 召唤生物时调用
     * @param summon 召唤的生物
     *
     * 将召唤的生物加入Boss的召唤列表并进入战斗
     *
     * @调用时机 引导者召唤任何生物时
     */
    void JustSummoned(Creature* summon) override
    {
        // 将召唤物加入Boss的召唤列表
        if (Creature* magtheridon = _instance->GetCreature(DATA_MAGTHERIDON))
            magtheridon->AI()->JustSummoned(summon);

        DoZoneInCombat(summon);  // 让召唤物进入战斗
    }

    /**
     * @brief 进入逃避模式
     * @param why 逃避原因
     *
     * 如果Boss战斗正在进行,触发Boss逃避
     *
     * @调用时机 引导者脱离战斗时
     */
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        // 如果Boss战斗正在进行,触发Boss逃避
        if (_instance->GetBossState(DATA_MAGTHERIDON) == IN_PROGRESS)
            if (Creature* magtheridon = _instance->GetCreature(DATA_MAGTHERIDON))
                magtheridon->AI()->EnterEvadeMode(EVADE_REASON_OTHER);
    }

    /**
     * @brief 更新AI状态(每帧调用)
     * @param diff 距离上次调用的时间(毫秒)
     *
     * 主循环函数,处理引导者的行为逻辑:
     *
     * 事件处理:
     * - EVENT_SHADOWBOLT: 暗影箭齐射,15-20秒冷却
     * - EVENT_FEAR: 恐惧随机目标,25-40秒冷却
     * - EVENT_CHECK_FRIEND: 检查友方血量,每秒检查
     * - EVENT_DARK_MENDING: 黑暗治疗冷却结束
     * - EVENT_ABYSSAL: 召唤燃烧深渊,60秒冷却
     *
     * @调用时机 游戏主循环每帧调用
     * @性能注意事项 高频调用函数,避免复杂计算
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        // 正在施法时不执行其他动作
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        _events.Update(diff);

        // 处理事件队列
        while (uint32 eventId = _events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_SHADOWBOLT:
                    // 暗影箭齐射
                    DoCastAOE(SPELL_SHADOW_BOLT_VOLLEY);
                    _events.Repeat(Seconds(15), Seconds(20));  // 15-20秒冷却
                    break;
                case EVENT_FEAR:
                    // 对随机目标施放恐惧
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 1))
                        DoCast(target, SPELL_FEAR);
                    _events.Repeat(Seconds(25), Seconds(40));  // 25-40秒冷却
                    break;
                case EVENT_CHECK_FRIEND:
                    // 检查是否有友方单位需要治疗
                    if (_canCastDarkMending)
                    {
                        // 查找血量低于51%的友方引导者
                        if (Unit* target = DoSelectBelowHpPctFriendlyWithEntry(NPC_HELLFIRE_CHANNELLER, 30.0f, 51, false))
                        {
                            DoCast(target, SPELL_DARK_MENDING);  // 施放黑暗治疗
                            _canCastDarkMending = false;  // 标记不可施放治疗
                            _events.ScheduleEvent(EVENT_DARK_MENDING, 10s, 20s);  // 10-20秒后冷却结束
                        }
                    }
                    _events.Repeat(Seconds(1));  // 每秒检查一次
                    break;
                case EVENT_DARK_MENDING:
                    // 黑暗治疗冷却结束
                    _canCastDarkMending = true;
                    break;
                case EVENT_ABYSSAL:
                    // 召唤燃烧深渊(召唤深渊恶魔)
                    DoCastVictim(SPELL_BURNING_ABYSSAL);
                    _events.Repeat(Seconds(60));  // 60秒冷却
                    break;
                default:
                    break;
            }

            // 施法时不继续执行
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    InstanceScript* _instance;          ///< 实例脚本指针
    EventMap _events;                   ///< 事件映射表
    bool _canCastDarkMending;           ///< 是否可以施放黑暗治疗
};

/**
 * @brief 玛瑟里顿房间AI结构体
 *
 * 处理房间崩塌时的残骸效果:
 * - 显示残骸视觉效果
 * - 延迟后造成残骸伤害
 */
struct npc_magtheridon_room : public PassiveAI
{
    /**
     * @brief 构造函数
     * @param creature 生物指针
     */
    npc_magtheridon_room(Creature* creature) : PassiveAI(creature) { }

    /**
     * @brief 重置时调用
     *
     * 施放残骸视觉效果,并在5秒后造成残骸伤害
     *
     * @调用时机 生物刷新或重置时
     */
    void Reset() override
    {
        DoCastSelf(SPELL_DEBRIS_VISUAL);  // 残骸视觉效果

        // 5秒后造成残骸伤害
        _scheduler.Schedule(Seconds(5), [this](TaskContext /*context*/)
        {
            DoCastAOE(SPELL_DEBRIS_DAMAGE);
        });
    }

    /**
     * @brief 更新AI状态
     * @param diff 距离上次调用的时间(毫秒)
     *
     * 更新任务调度器
     *
     * @调用时机 游戏主循环每帧调用
     */
    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);
    }

private:
    TaskScheduler _scheduler;  ///< 任务调度器
};

/**
 * @brief 蝎尾立方体游戏对象AI结构体
 *
 * 实现蝎尾立方体的交互逻辑:
 * - 玩家点击立方体时施放暗影之握
 * - 5名玩家同时使用时可禁锢Boss
 * - 使用后会获得精神疲劳,一段时间内不能再次使用
 */
struct go_manticron_cube : public GameObjectAI
{
    /**
     * @brief 构造函数
     * @param go 游戏对象指针
     */
    go_manticron_cube(GameObject* go) : GameObjectAI(go) { }

    /**
     * @brief 玩家点击游戏对象时调用
     * @param player 点击的玩家
     * @return true表示处理成功
     *
     * 处理玩家使用立方体的逻辑:
     * 1. 检查玩家是否有精神疲劳或暗影之握效果
     * 2. 找到附近的地狱火团队触发器并施放视觉效果
     * 3. 让玩家施放暗影之握
     *
     * @调用时机 玩家右键点击立方体时
     */
    bool OnGossipHello(Player* player) override
    {
        // 检查玩家是否有精神疲劳或暗影之握(有则不能使用)
        if (player->HasAura(SPELL_MIND_EXHAUSTION) || player->HasAura(SPELL_SHADOW_GRASP))
            return true;

        // 找到附近的触发器并施放视觉效果
        if (Creature* trigger = player->FindNearestCreature(NPC_HELFIRE_RAID_TRIGGER, 10.0f))
            trigger->CastSpell(nullptr, SPELL_SHADOW_GRASP_VISUAL);

        // 让玩家施放暗影之握
        player->CastSpell(nullptr, SPELL_SHADOW_GRASP, true);
        return true;
    }
};

/**
 * @brief 烈焰目标法术脚本
 *
 * 法术ID: 30541 - Blaze
 *
 * 当玛瑟里顿施放烈焰时:
 * 1. 选择随机目标
 * 2. 在目标位置生成烈焰区域
 */
// 30541 - Blaze
class spell_magtheridon_blaze_target : public SpellScript
{
    PrepareSpellScript(spell_magtheridon_blaze_target);

    /**
     * @brief 验证法术
     * @param spell 法术信息(未使用)
     * @return 验证是否成功
     *
     * 验证烈焰法术是否存在
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_BLAZE });
    }

    /**
     * @brief 处理光环
     *
     * 让被命中的目标对自己施放烈焰法术
     * 在目标脚下生成烈焰区域
     */
    void HandleAura()
    {
        GetHitUnit()->CastSpell(GetHitUnit(), SPELL_BLAZE);
    }

    /**
     * @brief 注册法术脚本
     *
     * 注册命中时的处理函数
     */
    void Register() override
    {
        OnHit += SpellHitFn(spell_magtheridon_blaze_target::HandleAura);
    }
};

/**
 * @brief 暗影之握光环脚本
 *
 * 法术ID: 30410 - Shadow Grasp
 *
 * 当玩家使用蝎尾立方体时获得此光环:
 * - 光环移除时中断玩家的施法
 * - 给玩家施加精神疲劳效果
 */
// 30410 - Shadow Grasp
class spell_magtheridon_shadow_grasp : public AuraScript
{
    PrepareAuraScript(spell_magtheridon_shadow_grasp);

    /**
     * @brief 验证法术
     * @param spell 法术信息(未使用)
     * @return 验证是否成功
     *
     * 验证精神疲劳法术是否存在
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_MIND_EXHAUSTION });
    }

    /**
     * @brief 光环移除时调用
     * @param aurEff 光环效果(未使用)
     * @param mode 处理模式(未使用)
     *
     * 当暗影之握效果消失时:
     * 1. 中断玩家的非近战法术
     * 2. 对施法者(立方体使用者)施放精神疲劳
     */
    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->InterruptNonMeleeSpells(false);  // 中断施法
        if (Unit* caster = GetCaster())
            caster->CastSpell(caster, SPELL_MIND_EXHAUSTION, true);  // 施放精神疲劳
    }

    /**
     * @brief 注册光环脚本
     *
     * 注册效果移除时的处理函数
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_magtheridon_shadow_grasp::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 暗影之握视觉效果光环脚本
 *
 * 法术ID: 30166 - Shadow Grasp (Visual Effect)
 *
 * 当玩家使用立方体时产生视觉效果:
 * - 当5名玩家同时使用立方体时,触发暗影牢笼禁锢Boss
 * - 光环移除时移除暗影牢笼
 */
// 30166 - Shadow Grasp (Visual Effect)
class spell_magtheridon_shadow_grasp_visual : public AuraScript
{
    PrepareAuraScript(spell_magtheridon_shadow_grasp_visual);

    /**
     * @brief 验证法术
     * @param spell 法术信息(未使用)
     * @return 验证是否成功
     *
     * 验证暗影牢笼和暗影之握视觉效果法术是否存在
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo(
        {
            SPELL_SHADOW_CAGE,
            SPELL_SHADOW_GRASP_VISUAL
        });
    }

    /**
     * @brief 光环应用时调用
     * @param aurEff 光环效果(未使用)
     * @param mode 处理模式(未使用)
     *
     * 当暗影之握视觉效果应用时:
     * - 检查目标身上的暗影之握视觉效果数量
     * - 如果达到5层,施放暗影牢笼禁锢Boss
     */
    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();

        // 检查是否有5层暗影之握视觉效果(5名玩家同时使用立方体)
        if (target->GetAuraCount(SPELL_SHADOW_GRASP_VISUAL) == 5)
            target->CastSpell(target, SPELL_SHADOW_CAGE, true);  // 施放暗影牢笼禁锢Boss
    }

    /**
     * @brief 光环移除时调用
     * @param aurEff 光环效果(未使用)
     * @param mode 处理模式(未使用)
     *
     * 移除目标身上的暗影牢笼效果
     */
    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        GetTarget()->RemoveAurasDueToSpell(SPELL_SHADOW_CAGE);
    }

    /**
     * @brief 注册光环脚本
     *
     * 注册效果应用和移除时的处理函数
     */
    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_magtheridon_shadow_grasp_visual::OnApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_magtheridon_shadow_grasp_visual::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 注册脚本函数
 *
 * 将玛瑟里顿相关的所有AI和法术脚本注册到脚本系统:
 * - 玛瑟里顿Boss AI
 * - 地狱火引导者AI
 * - 玛瑟里顿房间AI
 * - 蝎尾立方体AI
 * - 烈焰目标法术脚本
 * - 暗影之握光环脚本
 * - 暗影之握视觉效果脚本
 *
 * @调用时机 服务器启动时,脚本系统初始化阶段
 */
void AddSC_boss_magtheridon()
{
    RegisterMagtheridonsLairCreatureAI(boss_magtheridon);
    RegisterMagtheridonsLairCreatureAI(npc_hellfire_channeler);
    RegisterMagtheridonsLairCreatureAI(npc_magtheridon_room);
    RegisterMagtheridonsLairGameObjectAI(go_manticron_cube);
    RegisterSpellScript(spell_magtheridon_blaze_target);
    RegisterSpellScript(spell_magtheridon_shadow_grasp);
    RegisterSpellScript(spell_magtheridon_shadow_grasp_visual);
}
