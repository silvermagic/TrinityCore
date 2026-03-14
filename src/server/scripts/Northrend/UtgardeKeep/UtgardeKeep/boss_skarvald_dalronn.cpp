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
 * @file boss_skarvald_dalronn.cpp
 * @brief 诺森德副本"乌特加德城堡"双子BOSS：建造者斯卡瓦尔德和控魂者达尔隆的AI脚本
 *
 * 模块职责：
 * 1. 实现双子BOSS的联动战斗机制
 * 2. 处理BOSS死亡后变成鬼魂继续战斗的特殊逻辑
 * 3. 管理两个BOSS之间的死亡响应和互动台词
 * 4. 实现各BOSS的独特技能组合
 *
 * 战斗机制：
 * - 两个BOSS同时参战，需要都击杀才能完成
 * - 第一个死亡的BOSS会变成鬼魂形态继续战斗
 * - 当第二个BOSS死亡时，战斗结束，两个BOSS都会消失
 * - 斯卡瓦尔德：近战型，使用冲锋和石化打击
 * - 达尔隆：法系型，使用暗影箭和虚弱诅咒
 *
 * @author TrinityCore Team
 * @date 2026
 */

/* ScriptData
SDName: Boss_Skarvald_Dalronn
SD%Complete: 95
SDComment: Needs adjustments to blizzlike timers
SDCategory: Utgarde Keep
EndScriptData */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "utgarde_keep.h"

/**
 * @enum Texts
 * @brief BOSS和其鬼魂的台词文本ID枚举
 *
 * 两个BOSS及其鬼魂形态共用这些台词ID
 */
enum Texts
{
    SAY_AGGRO                  = 0,    // 开怪台词
    SAY_DEATH                  = 1,    // 两个BOSS都死亡时说（战斗结束）
    SAY_DIED_FIRST             = 2,    // 第一个死亡的BOSS说
    SAY_KILL                   = 3,    // 击杀玩家时说
    SAY_DEATH_RESPONSE         = 4     // 第一个BOSS死后，存活的BOSS说的响应台词
};

/**
 * @enum Spells
 * @brief BOSS和相关NPC使用的法术ID枚举
 */
enum Spells
{
    // Spells of Skarvald and his Ghost - 斯卡瓦尔德及其鬼魂的技能
    SPELL_CHARGE                                = 43651,    // 冲锋：快速接近远处目标
    SPELL_STONE_STRIKE                          = 48583,    // 石化打击：物理伤害
    SPELL_ENRAGE                                = 48193,    // 狂暴：生命值低于15%时触发
    SPELL_SUMMON_SKARVALD_GHOST                 = 48613,    // 召唤斯卡瓦尔德鬼魂

    // Spells of Dalronn and his Ghost - 达尔隆及其鬼魂的技能
    SPELL_SHADOW_BOLT                           = 43649,    // 暗影箭：远程暗影伤害
    SPELL_SUMMON_SKELETONS                      = 52611,    // 召唤骷髅：英雄模式下使用
    SPELL_DEBILITATE                           = 43650,    // 虚弱：降低目标伤害
    SPELL_SUMMON_DALRONN_GHOST                  = 48612     // 召唤达尔隆鬼魂
};

/**
 * @enum Events
 * @brief 事件调度器使用的定时事件ID枚举
 */
enum Events
{
    // Skarvald the Constructor - 建造者斯卡瓦尔德的事件
    EVENT_SKARVALD_CHARGE = 1,                  // 冲锋事件
    EVENT_STONE_STRIKE,                         // 石化打击事件

    // Dalronn the Controller - 控魂者达尔隆的事件
    EVENT_SHADOW_BOLT,                          // 暗影箭事件
    EVENT_DEBILITATE,                           // 虚弱事件
    EVENT_SUMMON_SKELETONS,                     // 召唤骷髅事件
    EVENT_DELAYED_AGGRO_SAY,                    // 延迟开怪台词（避免与斯卡瓦尔德重叠）

    // Common event to both bosses - 两个BOSS共用的事件
    // 延迟SAY_DEATH_RESPONSE，避免与刚死亡的BOSS的SAY_DIED_FIRST重叠
    EVENT_DEATH_RESPONSE
};

/**
 * @enum Actions
 * @brief BOSS之间通信使用的动作ID枚举
 */
enum Actions
{
    ACTION_OTHER_JUST_DIED = 1,                 // 另一个BOSS刚死亡
    ACTION_DESPAWN_SUMMONS = 2                  // 消失召唤物（清理鬼魂）
};

/**
 * @class SkarvaldChargePredicate
 * @brief 斯卡瓦尔德冲锋目标选择谓词
 *
 * 职责：
 * - 筛选符合冲锋条件的目标
 * - 目标距离必须在5-30码之间
 *
 * 使用方式：
 * - 传入SelectTarget函数作为筛选条件
 */
class SkarvaldChargePredicate
{
    public:
        /**
         * @brief 构造函数
         * @param unit 当前单位（斯卡瓦尔德）
         */
        SkarvaldChargePredicate(Unit* unit) : _me(unit) { }

        /**
         * @brief 目标筛选函数
         * @param target 候选目标
         * @return 是否符合冲锋条件
         *
         * 条件：
         * - 距离 >= 5码（太近不需要冲锋）
         * - 距离 <= 30码（冲锋的最大范围）
         */
        bool operator() (WorldObject* target) const
        {
            return target->GetDistance2d(_me) >= 5.0f && target->GetDistance2d(_me) <= 30.0f;
        }

    private:
        Unit* _me;     // 当前单位指针
};

/**
 * @struct generic_boss_controllerAI
 * @brief 双子BOSS通用控制器AI基类
 *
 * 职责：
 * - 提供两个BOSS共用的逻辑框架
 * - 处理BOSS死亡后的联动机制
 * - 管理鬼魂形态的特殊行为
 * - 协调两个BOSS之间的死亡响应
 *
 * 继承自：BossAI（提供基础BOSS AI功能）
 */
struct generic_boss_controllerAI : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化：
     * - 设置BOSS数据类型为DATA_SKARVALD_DALRONN（双子BOSS共享）
     * - 确定是否处于鬼魂形态
     */
    generic_boss_controllerAI(Creature* creature) : BossAI(creature, DATA_SKARVALD_DALRONN)
    {
        OtherBossData = 0;      // 另一个BOSS的数据ID，由子类设置
        // 判断当前是否为鬼魂形态
        IsInGhostForm = me->GetEntry() == NPC_SKARVALD_GHOST || me->GetEntry() == NPC_DALRONN_GHOST;
    }

    /**
     * @brief 重置BOSS状态
     *
     * 调用时机：
     * - BOSS脱战时
     * - 团队重置副本时
     * - BOSS被击杀后重生时
     *
     * 功能：
     * - 鬼魂形态：强制进入战斗（因为鬼魂是召唤出来的，需要主动参战）
     * - 非鬼魂形态：调用标准重置流程
     */
    void Reset() override
    {
        if (IsInGhostForm)
        {
            // 鬼魂不是通过正常开怪进入战斗的，需要主动将周围玩家拉入战斗
            DoZoneInCombat(me);
        }
        else
            _Reset();
    }

    /**
     * @brief 进入战斗时的处理函数
     * @param who 触发战斗的单位
     *
     * 调用时机：
     * - BOSS被玩家攻击或主动攻击玩家时
     *
     * 功能：
     * - 只有非鬼魂形态才触发标准进入战斗流程
     * - 鬼魂形态已经通过Reset中的DoZoneInCombat进入战斗
     */
    void JustEngagedWith(Unit* who) override
    {
        if (!IsInGhostForm)
            BossAI::JustEngagedWith(who);
    }

    /**
     * @brief 死亡时的处理函数
     * @param killer 击杀者
     *
     * 调用时机：
     * - BOSS死亡时
     *
     * 功能：
     * - 检查另一个BOSS是否存活
     * - 如果另一个BOSS存活：变成鬼魂继续战斗，通知另一个BOSS
     * - 如果另一个BOSS已死：战斗结束，清理所有召唤物
     */
    void JustDied(Unit* /*killer*/) override
    {
        // 获取另一个BOSS
        if (Creature* otherBoss = ObjectAccessor::GetCreature(*me, instance->GetGuidData(OtherBossData)))
        {
            if (otherBoss->IsAlive())
            {
                // 另一个BOSS还活着，这是第一个死亡的BOSS
                Talk(SAY_DIED_FIRST);
                // 移除可拾取标志，防止玩家在这个BOSS身上搜刮战利品
                me->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
                // 通知另一个BOSS
                otherBoss->AI()->DoAction(ACTION_OTHER_JUST_DIED);
                // 召唤自己的鬼魂形态继续战斗
                DoCast(me, OtherBossData == DATA_DALRONN ? SPELL_SUMMON_SKARVALD_GHOST : SPELL_SUMMON_DALRONN_GHOST, true);
            }
            else
            {
                // 另一个BOSS已经死了，这是第二个死亡的BOSS，战斗结束
                Talk(SAY_DEATH);
                // 通知另一个BOSS的鬼魂消失
                otherBoss->AI()->DoAction(ACTION_DESPAWN_SUMMONS);
                _JustDied();
            }
        }
    }

    /**
     * @brief 处理外部动作事件
     * @param actionId 动作ID
     *
     * 调用时机：
     * - 由另一个BOSS在死亡时调用
     *
     * 功能：
     * - ACTION_OTHER_JUST_DIED：安排死亡响应台词
     * - ACTION_DESPAWN_SUMMONS：消失所有召唤物（鬼魂）
     */
    void DoAction(int32 actionId) override
    {
        switch (actionId)
        {
            case ACTION_OTHER_JUST_DIED:
                // 延迟2秒说响应台词，避免与死亡台词重叠
                events.ScheduleEvent(EVENT_DEATH_RESPONSE, 2s);
                break;
            case ACTION_DESPAWN_SUMMONS:
                // 战斗结束，消失所有召唤物（包括鬼魂）
                summons.DespawnAll();
                break;
            default:
                break;
        }
    }

    /**
     * @brief 执行事件
     * @param eventId 事件ID
     *
     * 调用时机：
     * - 事件调度器执行到期事件时
     *
     * 功能：
     * - 处理EVENT_DEATH_RESPONSE事件（死亡响应台词）
     * - 子类应调用此方法处理通用事件
     */
    void ExecuteEvent(uint32 eventId) override
    {
        if (eventId == EVENT_DEATH_RESPONSE)
            Talk(SAY_DEATH_RESPONSE);
    }

    /**
     * @brief 击杀单位时的处理函数
     * @param who 被击杀的单位
     *
     * 调用时机：
     * - BOSS击杀任何单位时
     *
     * 功能：
     * - 如果击杀的是玩家且不是鬼魂形态，播放击杀台词
     */
    void KilledUnit(Unit* who) override
    {
        if (!IsInGhostForm && who->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_KILL);
    }

    protected:
        uint32 OtherBossData;       // 另一个BOSS的数据ID（DATA_SKARVALD或DATA_DALRONN）
        bool IsInGhostForm;         // 是否处于鬼魂形态
};

/**
 * @struct boss_skarvald_the_constructor
 * @brief 建造者斯卡瓦尔德的AI结构体
 *
 * 职责：
 * - 实现斯卡瓦尔德的战斗逻辑
 * - 管理近战技能（冲锋、石化打击）
 * - 处理低血量狂暴
 *
 * 继承自：generic_boss_controllerAI
 *
 * 战斗风格：
 * - 近战型战士
 * - 能够冲锋远处目标
 * - 低血量时会狂暴
 */
struct boss_skarvald_the_constructor : public generic_boss_controllerAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_skarvald_the_constructor(Creature* creature) : generic_boss_controllerAI(creature)
    {
        OtherBossData = DATA_DALRONN;   // 设置另一个BOSS为达尔隆
        Enraged = false;
    }

    /**
     * @brief 重置AI状态
     *
     * 调用时机：
     * - BOSS脱战时
     * - AI初始化时
     *
     * 功能：
     * - 重置狂暴状态
     * - 调用父类的Reset方法
     */
    void Reset() override
    {
        Enraged = false;
        generic_boss_controllerAI::Reset();
    }

    /**
     * @brief 进入战斗时的处理函数
     * @param who 触发战斗的单位
     *
     * 调用时机：
     * - BOSS被玩家攻击或主动攻击玩家时
     *
     * 功能：
     * - 调用父类的进入战斗方法
     * - 非鬼魂形态时播放开怪台词
     * - 安排冲锋和石化打击技能事件
     */
    void JustEngagedWith(Unit* who) override
    {
        generic_boss_controllerAI::JustEngagedWith(who);

        if (!IsInGhostForm)
            Talk(SAY_AGGRO);

        events.ScheduleEvent(EVENT_SKARVALD_CHARGE, 5s);    // 5秒后冲锋
        events.ScheduleEvent(EVENT_STONE_STRIKE, 10s);      // 10秒后石化打击
    }

    /**
     * @brief 执行事件
     * @param eventId 事件ID
     *
     * 调用时机：
     * - 事件调度器执行到期事件时
     *
     * 功能：
     * - EVENT_SKARVALD_CHARGE：对远处目标施放冲锋
     * - EVENT_STONE_STRIKE：对当前目标施放石化打击
     */
    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_SKARVALD_CHARGE:
                // 选择5-30码范围内的随机目标冲锋
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, SkarvaldChargePredicate(me)))
                    DoCast(target, SPELL_CHARGE);
                events.ScheduleEvent(EVENT_CHARGE, 5s, 10s);
                break;
            case EVENT_STONE_STRIKE:
                // 对当前目标施放石化打击
                DoCastVictim(SPELL_STONE_STRIKE);
                events.ScheduleEvent(EVENT_STONE_STRIKE, 5s, 10s);
                break;
            default:
                // 处理父类的通用事件
                generic_boss_controllerAI::ExecuteEvent(eventId);
                break;
        }
    }

    /**
     * @brief 处理伤害接收事件
     * @param attacker 攻击者
     * @param damage 伤害值
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 调用时机：
     * - BOSS受到伤害时
     *
     * 功能：
     * - 检测生命值是否低于15%且未狂暴
     * - 触发狂暴状态（非鬼魂形态）
     */
    void DamageTaken(Unit* /*attacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 生命值低于15%时触发狂暴，鬼魂形态不会狂暴
        if (!Enraged && !IsInGhostForm && me->HealthBelowPctDamaged(15, damage))
        {
            Enraged = true;
            DoCast(me, SPELL_ENRAGE);
        }
    }

    private:
        bool Enraged;       // 是否已狂暴
};

/**
 * @struct boss_dalronn_the_controller
 * @brief 控魂者达尔隆的AI结构体
 *
 * 职责：
 * - 实现达尔隆的战斗逻辑
 * - 管理法术技能（暗影箭、虚弱、召唤骷髅）
 * - 处理延迟开怪台词（避免与斯卡瓦尔德重叠）
 *
 * 继承自：generic_boss_controllerAI
 *
 * 战斗风格：
 * - 法系施法者
 * - 持续施放暗影箭
 * - 偶尔施放虚弱诅咒
 * - 英雄模式下会召唤骷髅
 */
struct boss_dalronn_the_controller : public generic_boss_controllerAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_dalronn_the_controller(Creature* creature) : generic_boss_controllerAI(creature)
    {
        OtherBossData = DATA_SKARVALD;   // 设置另一个BOSS为斯卡瓦尔德
    }

    /**
     * @brief 进入战斗时的处理函数
     * @param who 触发战斗的单位
     *
     * 调用时机：
     * - BOSS被玩家攻击或主动攻击玩家时
     *
     * 功能：
     * - 调用父类的进入战斗方法
     * - 安排暗影箭和虚弱技能事件
     * - 非鬼魂形态时安排延迟开怪台词（5秒后说，避免与斯卡瓦尔德重叠）
     * - 英雄模式下安排召唤骷髅事件
     */
    void JustEngagedWith(Unit* who) override
    {
        generic_boss_controllerAI::JustEngagedWith(who);

        events.ScheduleEvent(EVENT_SHADOW_BOLT, 1s);        // 1秒后开始施放暗影箭
        events.ScheduleEvent(EVENT_DEBILITATE, 5s);         // 5秒后施放虚弱

        if (!IsInGhostForm)
            events.ScheduleEvent(EVENT_DELAYED_AGGRO_SAY, 5s);  // 5秒后说开怪台词

        if (IsHeroic())
            events.ScheduleEvent(EVENT_SUMMON_SKELETONS, 10s);  // 英雄模式：10秒后召唤骷髅
    }

    /**
     * @brief 执行事件
     * @param eventId 事件ID
     *
     * 调用时机：
     * - 事件调度器执行到期事件时
     *
     * 功能：
     * - EVENT_SHADOW_BOLT：对随机目标施放暗影箭
     * - EVENT_DEBILITATE：对随机目标施放虚弱
     * - EVENT_SUMMON_SKELETONS：召唤骷髅（英雄模式）
     * - EVENT_DELAYED_AGGRO_SAY：播放延迟的开怪台词
     */
    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_SHADOW_BOLT:
                // 选择45码范围内的随机目标施放暗影箭
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 45.0f, true))
                    DoCast(target, SPELL_SHADOW_BOLT);
                // 2.1秒后再次施放（留出100ms间隙尝试施放其他法术）
                events.ScheduleEvent(EVENT_SHADOW_BOLT, 2100ms);
                break;
            case EVENT_DEBILITATE:
                // 选择50码范围内的随机目标施放虚弱
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 50.0f, true))
                    DoCast(target, SPELL_DEBILITATE);
                events.ScheduleEvent(EVENT_DEBILITATE, 5s, 10s);
                break;
            case EVENT_SUMMON_SKELETONS:
                // 召唤骷髅助战
                DoCast(me, SPELL_SUMMON_SKELETONS);
                events.ScheduleEvent(EVENT_SUMMON_SKELETONS, 10s, 30s);
                break;
            case EVENT_DELAYED_AGGRO_SAY:
                // 播放延迟的开怪台词
                Talk(SAY_AGGRO);
                break;
            default:
                // 处理父类的通用事件
                generic_boss_controllerAI::ExecuteEvent(eventId);
                break;
        }
    }
};

/**
 * @brief 注册所有AI脚本
 *
 * 调用时机：
 * - 服务器启动时，脚本加载系统会调用此函数
 *
 * 功能：
 * - 注册建造者斯卡瓦尔德BOSS AI
 * - 注册控魂者达尔隆BOSS AI
 */
void AddSC_boss_skarvald_dalronn()
{
    RegisterUtgardeKeepCreatureAI(boss_skarvald_the_constructor);
    RegisterUtgardeKeepCreatureAI(boss_dalronn_the_controller);
}
