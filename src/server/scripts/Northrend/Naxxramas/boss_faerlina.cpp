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
 * @file boss_faerlina.cpp
 * @brief 纳克萨玛斯副本BOSS - 寡妇费琳娜(Grand Widow Faerlina)的AI脚本
 *
 * 模块职责:
 * - 实现寡妇费琳娜BOSS的战斗逻辑
 * - 管理追随者(Follower)和崇拜者(Worshipper)的生成和互动
 * - 实现寡妇之拥(Widow's Embrace)机制 - 追随者死亡可移除BOSS狂暴
 * - 处理毒箭齐射、火焰之雨和狂暴技能
 * - 管理"妈妈说把你打趴下"成就(不通过寡妇之拥驱散任何狂暴)
 *
 * 战斗机制:
 * - BOSS会周期性狂暴,增加伤害
 * - 10人模式:崇拜者死亡时会对BOSS施放寡妇之拥,移除狂暴
 * - 25人模式:追随者死亡时会对BOSS施放寡妇之拥,移除狂暴
 * - 寡妇之拥期间BOSS不会施放毒箭齐射
 * - 成就要求:不使用寡妇之拥驱散任何狂暴完成击杀
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "naxxramas.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"

/**
 * @brief 费琳娜的台词枚举
 */
enum Yells
{
    SAY_GREET           = 0,    // 问候台词(玩家进入区域触发)
    SAY_AGGRO           = 1,    // 开战台词
    SAY_SLAY            = 2,    // 击杀玩家台词
    SAY_DEATH           = 3,    // 死亡台词

    EMOTE_WIDOW_EMBRACE = 4,    // 寡妇之拥表情
    EMOTE_FRENZY        = 5     // 狂暴表情

};

/**
 * @brief 技能ID枚举
 */
enum Spells
{
    SPELL_POISON_BOLT_VOLLEY    = 28796,    // 毒箭齐射 - 对所有敌人造成自然伤害
    SPELL_RAIN_OF_FIRE          = 28794,    // 火焰之雨 - 对随机目标区域造成火焰伤害
    SPELL_FRENZY                = 28798,    // 狂暴 - 提高攻击速度和伤害
    SPELL_WIDOWS_EMBRACE        = 28732,    // 寡妇之拥 - 移除狂暴效果

    SPELL_ADD_FIREBALL          = 54095     // 追随者/崇拜者的火球术 - 25人模式: 54096
};

/// 寡妇之拥辅助技能ID(根据难度选择)
#define SPELL_WIDOWS_EMBRACE_HELPER RAID_MODE<uint32>(28732, 54097)

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_POISON    = 1,    // 毒箭齐射事件
    EVENT_FIRE      = 2,    // 火焰之雨事件
    EVENT_FRENZY    = 3     // 狂暴事件
};

/**
 * @brief 召唤组枚举
 */
enum SummonGroups
{
    SUMMON_GROUP_WORSHIPPERS    = 1,    // 崇拜者召唤组(10人和25人都有)
    SUMMON_GROUP_FOLLOWERS      = 2     // 追随者召唤组(仅25人模式)
};

/**
 * @brief 杂项枚举
 */
enum Misc
{
    DATA_FRENZY_DISPELS         = 1     // 狂暴驱散次数数据ID,用于成就判定
};

/**
 * @struct boss_faerlina
 * @brief 寡妇费琳娜BOSS的AI实现
 *
 * 继承自BossAI,实现了寡妇费琳娜的完整战斗逻辑,包括:
 * - 毒箭齐射和火焰之雨技能的施放
 * - 狂暴机制的管理
 * - 寡妇之拥机制的处理(追随者/崇拜者死亡时触发)
 * - 狂暴驱散计数,用于成就判定
 */
struct boss_faerlina : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature BOSS生物对象指针
     */
    boss_faerlina(Creature* creature) : BossAI(creature, BOSS_FAERLINA), _frenzyDispels(0) { }

    /**
     * @brief 召唤追随者/崇拜者
     *
     * 召唤崇拜者组,25人模式额外召唤追随者组
     * 这些随从在BOSS初始生成时就会出现
     */
    void SummonAdds()
    {
        me->SummonCreatureGroup(SUMMON_GROUP_WORSHIPPERS);
        if (Is25ManRaid())
            me->SummonCreatureGroup(SUMMON_GROUP_FOLLOWERS);
    }

    /**
     * @brief 初始化AI
     *
     * 在BOSS非死亡状态且副本状态未完成时,执行重置并召唤随从
     * 调用时机: 生物创建时或副本重置时
     */
    void InitializeAI() override
    {
        if (!me->isDead() && instance->GetBossState(BOSS_FAERLINA) != DONE)
        {
            Reset();
            SummonAdds();
        }
    }

    /**
     * @brief BOSS返回出生点
     *
     * 当BOSS脱离战斗返回出生点后,重新召唤随从
     * 调用时机: BOSS脱离战斗返回出生点时
     */
    void JustReachedHome() override
    {
        _JustReachedHome();
        SummonAdds();
    }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标
     *
     * 初始化战斗事件计时器,安排毒箭齐射、火焰之雨和狂暴
     * 调用时机: BOSS进入战斗时
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);
        // 让所有随从进入战斗
        summons.DoZoneInCombat();
        // 安排毒箭齐射 - 10-15秒后随机时间
        events.ScheduleEvent(EVENT_POISON, randtime(Seconds(10), Seconds(15)));
        // 安排火焰之雨 - 6-18秒后随机时间
        events.ScheduleEvent(EVENT_FIRE, randtime(Seconds(6), Seconds(18)));
        // 安排第一次狂暴 - 1分到1分20秒后随机时间
        events.ScheduleEvent(EVENT_FRENZY, Minutes(1)+randtime(0s, Seconds(20)));
    }

    /**
     * @brief 重置BOSS状态
     *
     * 清空事件计时器和狂暴驱散计数
     * 调用时机: BOSS脱离战斗或副本重置时
     */
    void Reset() override
    {
        _Reset();
        _frenzyDispels = 0;
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
     * @brief 被法术击中时的处理
     * @param caster 施法者
     * @param spellInfo 法术信息
     *
     * 当被寡妇之拥击中时,增加狂暴驱散计数并击杀施法者
     * 这是10人模式崇拜者死亡时的机制
     * 调用时机: BOSS被法术击中时
     */
    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        Unit* unitCaster = caster->ToUnit();
        if (!unitCaster)
            return;

        // 如果被寡妇之拥抱击中
        if (spellInfo->Id == SPELL_WIDOWS_EMBRACE_HELPER)
        {
            ++_frenzyDispels; // 增加狂暴驱散计数
            Talk(EMOTE_WIDOW_EMBRACE, caster);
            Unit::Kill(me, unitCaster); // 击杀施法者(崇拜者)
        }
    }

    /**
     * @brief 获取数据
     * @param type 数据类型
     * @return 对应的数据值
     *
     * 返回狂暴驱散次数,用于成就判定
     * 调用时机: 成就检查时
     */
    uint32 GetData(uint32 type) const override
    {
        if (type == DATA_FRENZY_DISPELS)
            return _frenzyDispels;

        return 0;
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 处理所有战斗事件的执行,包括毒箭齐射、火焰之雨和狂暴
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
                case EVENT_POISON:
                    // 如果BOSS没有寡妇之拥效果,则施放毒箭齐射
                    // 寡妇之拥期间无法施放毒箭齐射
                    if (!me->HasAura(SPELL_WIDOWS_EMBRACE_HELPER))
                        DoCastAOE(SPELL_POISON_BOLT_VOLLEY);
                    // 安排下一次毒箭齐射,8-15秒后
                    events.Repeat(randtime(Seconds(8), Seconds(15)));
                    break;
                case EVENT_FIRE:
                    // 对随机目标施放火焰之雨
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_RAIN_OF_FIRE);
                    // 安排下一次火焰之雨,6-18秒后
                    events.Repeat(randtime(Seconds(6), Seconds(18)));
                    break;
                case EVENT_FRENZY:
                    // 如果BOSS有寡妇之拥效果,延迟狂暴直到效果结束
                    if (Aura* widowsEmbrace = me->GetAura(SPELL_WIDOWS_EMBRACE_HELPER))
                        events.ScheduleEvent(EVENT_FRENZY, Milliseconds(widowsEmbrace->GetDuration()+1));
                    else
                    {
                        // 施放狂暴技能
                        DoCast(SPELL_FRENZY);
                        Talk(EMOTE_FRENZY);
                        // 安排下一次狂暴,1分到1分20秒后
                        events.Repeat(Minutes(1) + randtime(0s, Seconds(20)));
                    }
                    break;
            }

            // 如果开始施法,则退出事件处理循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 进行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    uint32 _frenzyDispels;    ///< 狂暴驱散计数,用于成就"妈妈说把你打趴下"判定
};

/**
 * @struct npc_faerlina_add
 * @brief 费琳娜的随从AI实现(崇拜者和追随者)
 *
 * 继承自ScriptedAI,实现了崇拜者和追随者的AI逻辑,包括:
 * - 对BOSS施放寡妇之拥(10人模式,崇拜者死亡时)
 * - 免疫精神控制(10人模式)
 * - 持续施放火球术攻击目标
 */
struct npc_faerlina_add : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_faerlina_add(Creature* creature) : ScriptedAI(creature),
        _instance(creature->GetInstanceScript())
    {
    }

    /**
     * @brief 重置随从状态
     *
     * 在10人模式下,使随从免疫精神控制效果
     * 调用时机: 随从重置时
     */
    void Reset() override
    {
        if (!Is25ManRaid()) {
            // 10人模式下免疫束缚和精神控制效果
            me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_BIND, true);
            me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_CHARM, true);
        }
    }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标
     *
     * 让BOSS进入战斗状态
     * 调用时机: 随从进入战斗时
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        if (Creature* faerlina = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_FAERLINA)))
            faerlina->AI()->DoZoneInCombat();
    }

    /**
     * @brief 随从死亡处理
     * @param killer 击杀者
     *
     * 在10人模式下,死亡时对BOSS施放寡妇之拥
     * 调用时机: 随从死亡时
     */
    void JustDied(Unit* /*killer*/) override
    {
        if (!Is25ManRaid())
            // 10人模式:崇拜者死亡时对BOSS施放寡妇之拥
            if (Creature* faerlina = ObjectAccessor::GetCreature(*me, _instance->GetGuidData(DATA_FAERLINA)))
                DoCast(faerlina, SPELL_WIDOWS_EMBRACE);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 持续对当前目标施放火球术
     * 调用时机: 每个世界更新周期
     * 性能注意事项: 该函数会被频繁调用,应避免复杂计算
     */
    void UpdateAI(uint32 /*diff*/) override
    {
        // 检查是否有有效目标,没有则返回
        if (!UpdateVictim())
            return;
        // 如果正在施法,则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 对当前目标施放火球术
        DoCastVictim(SPELL_ADD_FIREBALL);
        // 如果火球术施放失败,则进行近战攻击
        DoMeleeAttackIfReady(); // 仅在火球术施放失败时发生
    }

private:
    InstanceScript* const _instance;    ///< 副本脚本实例指针
};

/**
 * @class achievement_momma_said_knock_you_out
 * @brief 成就"妈妈说把你打趴下"判定脚本
 *
 * 检查是否在没有通过寡妇之拥驱散任何狂暴的情况下击杀费琳娜
 * 成就要求: 不使用随从死亡时的寡妇之拥机制来驱散BOSS的狂暴
 */
class achievement_momma_said_knock_you_out : public AchievementCriteriaScript
{
    public:
        /**
         * @brief 构造函数
         */
        achievement_momma_said_knock_you_out() : AchievementCriteriaScript("achievement_momma_said_knock_you_out") { }

        /**
         * @brief 成就条件检查
         * @param source 触发成就的玩家
         * @param target 目标单位(BOSS)
         * @return true表示成就条件满足
         *
         * 检查狂暴驱散次数是否为0
         * 调用时机: 成就进度更新时
         */
        bool OnCheck(Player* /*source*/, Unit* target) override
        {
            return target && !target->GetAI()->GetData(DATA_FRENZY_DISPELS);
        }
};

/**
 * @class at_faerlina_entrance
 * @brief 费琳娜入口区域触发脚本
 *
 * 当玩家首次进入费琳娜的房间区域时,触发BOSS的问候台词
 * 继承自OnlyOnceAreaTriggerScript,确保每个玩家只触发一次
 */
class at_faerlina_entrance : public OnlyOnceAreaTriggerScript
{
    public:
        /**
         * @brief 构造函数
         */
        at_faerlina_entrance() : OnlyOnceAreaTriggerScript("at_faerlina_entrance") { }

        /**
         * @brief 处理区域触发
         * @param player 触发区域的玩家
         * @param areaTrigger 区域触发数据
         * @return true表示处理成功
         *
         * 当玩家进入费琳娜房间且BOSS未被激活时,播放问候台词
         * 调用时机: 玩家首次进入指定区域时
         */
        bool TryHandleOnce(Player* player, AreaTriggerEntry const* /*areaTrigger*/) override
        {
            InstanceScript* instance = player->GetInstanceScript();
            // 检查副本状态,如果BOSS已激活则不触发
            if (!instance || instance->GetBossState(BOSS_FAERLINA) != NOT_STARTED)
                return true;

            // 获取费琳娜并播放问候台词
            if (Creature* faerlina = ObjectAccessor::GetCreature(*player, instance->GetGuidData(DATA_FAERLINA)))
                faerlina->AI()->Talk(SAY_GREET);

            return true;
        }
};

/**
 * @brief 注册费琳娜BOSS脚本
 *
 * 注册BOSS AI、随从AI、区域触发脚本和成就脚本到脚本系统
 */
void AddSC_boss_faerlina()
{
    RegisterNaxxramasCreatureAI(boss_faerlina);
    RegisterNaxxramasCreatureAI(npc_faerlina_add);
    new at_faerlina_entrance();
    new achievement_momma_said_knock_you_out();
}
