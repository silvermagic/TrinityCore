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
 * @file boss_trollgore.cpp
 * @brief 达克萨隆要塞副本 - 托尔戈斯（Trollgore）首领战脚本
 *
 * 本文件实现了达克萨隆要塞副本中第一个首领托尔戈斯的战斗逻辑。
 * 托尔戈斯是一只被天灾军团复活的巨魔，具有吞噬尸体增强自身能力的特殊机制。
 *
 * 主要功能：
 * - 托尔戈斯的战斗AI和技能循环
 * - 吞噬技能的处理和增益叠加机制
 * - 尸体爆炸和召唤入侵者的技能实现
 * - 成就"Consumption Junction"的检测逻辑
 */

#include "ScriptMgr.h"
#include "drak_tharon_keep.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"

/**
 * @brief 托尔戈斯使用的法术ID枚举
 */
enum Spells
{
    SPELL_INFECTED_WOUND                = 49637,  ///< 感染伤口 - 降低目标的攻击速度
    SPELL_CRUSH                         = 49639,  ///< 粉碎 - 对目标造成大量物理伤害
    SPELL_CORPSE_EXPLODE                = 49555,  ///< 尸体爆炸 - 引爆尸体造成范围伤害
    SPELL_CORPSE_EXPLODE_DAMAGE         = 49618,  ///< 尸体爆炸伤害 - 实际伤害效果
    SPELL_CONSUME                       = 49380,  ///< 吞噬 - 吞噬目标并获得增益
    SPELL_CONSUME_BUFF                  = 49381,  ///< 吞噬增益（普通模式）
    SPELL_CONSUME_BUFF_H                = 59805,  ///< 吞噬增益（英雄模式）

    SPELL_SUMMON_INVADER_A              = 49456,  ///< 召唤入侵者A - 召唤德拉克瑞入侵者
    SPELL_SUMMON_INVADER_B              = 49457,  ///< 召唤入侵者B - 召唤德拉克瑞入侵者
    SPELL_SUMMON_INVADER_C              = 49458,  ///< 召唤入侵者C - 召唤德拉克瑞入侵者（未找到相关数据）

    SPELL_INVADER_TAUNT                 = 49405   ///< 入侵者嘲讽 - 强制托尔戈斯攻击入侵者
};

/**
 * @brief 吞噬增益的法术ID辅助宏
 * @note 根据副本模式自动选择普通或英雄版本的增益效果
 */
#define SPELL_CONSUME_BUFF_HELPER DUNGEON_MODE<uint32>(SPELL_CONSUME_BUFF, SPELL_CONSUME_BUFF_H)

/**
 * @brief 托尔戈斯的台词和喊话枚举
 */
enum Yells
{
    SAY_AGGRO                           = 0,  ///< 开战台词
    SAY_KILL                            = 1,  ///< 击杀玩家台词
    SAY_CONSUME                         = 2,  ///< 使用吞噬技能台词
    SAY_EXPLODE                         = 3,  ///< 使用尸体爆炸台词
    SAY_DEATH                           = 4   ///< 死亡台词
};

/**
 * @brief 其他杂项常量枚举
 */
enum Misc
{
    DATA_CONSUMPTION_JUNCTION           = 1,  ///< 成就"Consumption Junction"的数据标识
    POINT_LANDING                       = 1   ///< 入侵者降落路径点
};

/**
 * @brief 事件定时器枚举
 * @note 用于管理托尔戈斯的技能施放顺序
 */
enum Events
{
    EVENT_CONSUME = 1,          ///< 吞噬事件 - 每15秒施放一次
    EVENT_CRUSH,                ///< 粉碎事件 - 每10-15秒施放一次
    EVENT_INFECTED_WOUND,       ///< 感染伤口事件 - 每25-35秒施放一次
    EVENT_CORPSE_EXPLODE,       ///< 尸体爆炸事件 - 每15-19秒施放一次
    EVENT_SPAWN                 ///< 召唤入侵者事件 - 每30-40秒召唤一波
};

/**
 * @brief 入侵者降落位置坐标
 * @note 入侵者被召唤后会移动到此位置降落
 */
Position const Landing = { -263.0534f, -660.8658f, 26.50903f, 0.0f };

/**
 * @brief 托尔戈斯首领AI结构体
 *
 * 实现了托尔戈斯的完整战斗逻辑，包括：
 * - 技能循环：吞噬、粉碎、感染伤口、尸体爆炸、召唤入侵者
 * - 吞噬增益叠加检测（用于成就判断）
 * - 入侵者召唤和管理
 */
struct boss_trollgore : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 首领生物对象指针
     */
    boss_trollgore(Creature* creature) : BossAI(creature, DATA_TROLLGORE)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     * @note 在构造函数和重置时调用，确保状态正确
     */
    void Initialize()
    {
        _consumptionJunction = true;  ///< 初始化成就标志为true，表示尚未达到失败条件
    }

    /**
     * @brief 重置首领状态
     *
     * 当战斗结束或首领脱离战斗时调用：
     * - 重置所有事件定时器
     * - 重置成就相关变量
     *
     * @调用时机 战斗结束、首领脱战、重置副本时
     */
    void Reset() override
    {
        _Reset();
        Initialize();
    }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标单位
     *
     * @调用时机 当首领被玩家攻击或主动攻击玩家时
     * @性能注意事项 启动多个事件定时器，注意定时器的内存管理
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);  ///< 播放开战台词

        // 初始化技能施放定时器
        events.ScheduleEvent(EVENT_CONSUME, 15s);           ///< 15秒后施放吞噬
        events.ScheduleEvent(EVENT_CRUSH, 1s, 5s);          ///< 1-5秒后施放粉碎
        events.ScheduleEvent(EVENT_INFECTED_WOUND, 10s, 60s); ///< 10-60秒后施放感染伤口
        events.ScheduleEvent(EVENT_CORPSE_EXPLODE, 3s);     ///< 3秒后施放尸体爆炸
        events.ScheduleEvent(EVENT_SPAWN, 30s, 40s);        ///< 30-40秒后召唤入侵者
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 主循环函数，每帧调用一次，负责：
     * - 检查战斗状态
     * - 处理事件定时器
     * - 施放技能
     * - 检测成就条件
     * - 执行近战攻击
     *
     * @调用时机 每个游戏帧（约每50毫秒）
     * @性能注意事项 避免在此函数中进行耗时操作，影响服务器性能
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效的攻击目标
        if (!UpdateVictim())
            return;

        events.Update(diff);

        // 如果正在施法，则等待施法完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 处理事件队列
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_CONSUME:
                    // 吞噬：对周围所有敌人施放吞噬效果
                    Talk(SAY_CONSUME);
                    DoCastAOE(SPELL_CONSUME);
                    events.ScheduleEvent(EVENT_CONSUME, 15s);
                    break;
                case EVENT_CRUSH:
                    // 粉碎：对当前目标造成大量物理伤害
                    DoCastVictim(SPELL_CRUSH);
                    events.ScheduleEvent(EVENT_CRUSH, 10s, 15s);
                    break;
                case EVENT_INFECTED_WOUND:
                    // 感染伤口：降低目标的攻击速度
                    DoCastVictim(SPELL_INFECTED_WOUND);
                    events.ScheduleEvent(EVENT_INFECTED_WOUND, 25s, 35s);
                    break;
                case EVENT_CORPSE_EXPLODE:
                    // 尸体爆炸：引爆场上的尸体造成范围伤害
                    Talk(SAY_EXPLODE);
                    DoCastAOE(SPELL_CORPSE_EXPLODE);
                    events.ScheduleEvent(EVENT_CORPSE_EXPLODE, 15s, 19s);
                    break;
                case EVENT_SPAWN:
                    // 召唤入侵者：从三个召唤点各召唤一个德拉克瑞入侵者
                    for (uint8 i = 0; i < 3; ++i)
                        if (Creature* trigger = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_TROLLGORE_INVADER_SUMMONER_1 + i)))
                            trigger->CastSpell(trigger, RAND(SPELL_SUMMON_INVADER_A, SPELL_SUMMON_INVADER_B, SPELL_SUMMON_INVADER_C), me->GetGUID());

                    events.ScheduleEvent(EVENT_SPAWN, 30s, 40s);
                    break;
                default:
                    break;
            }

            // 施法后再次检查是否正在施法，避免打断
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        // 成就检测：如果吞噬增益叠加超过9层，则成就失败
        if (_consumptionJunction)
        {
            Aura* ConsumeAura = me->GetAura(SPELL_CONSUME_BUFF_HELPER);
            if (ConsumeAura && ConsumeAura->GetStackAmount() > 9)
                _consumptionJunction = false;
        }

        DoMeleeAttackIfReady();
    }

    /**
     * @brief 首领死亡
     * @param killer 击杀首领的单位（可能为nullptr）
     *
     * @调用时机 当首领生命值降至0时
     */
    void JustDied(Unit* /*killer*/) override
    {
        _JustDied();
        Talk(SAY_DEATH);  ///< 播放死亡台词
    }

    /**
     * @brief 获取自定义数据
     * @param type 数据类型标识
     * @return 返回请求的数据值，失败返回0
     *
     * 用于成就系统查询吞噬增益叠加状态
     */
    uint32 GetData(uint32 type) const override
    {
        if (type == DATA_CONSUMPTION_JUNCTION)
            return _consumptionJunction ? 1 : 0;

        return 0;
    }

    /**
     * @brief 击杀单位
     * @param victim 被击杀的单位
     *
     * @调用时机 当托尔戈斯击杀任何单位时
     */
    void KilledUnit(Unit* victim) override
    {
        // 只对玩家击杀播放台词
        if (victim->GetTypeId() != TYPEID_PLAYER)
            return;

        Talk(SAY_KILL);
    }

    /**
     * @brief 召唤生物回调
     * @param summon 被召唤的生物对象
     *
     * @调用时机 当托尔戈斯召唤入侵者时
     * @note 让入侵者移动到降落位置
     */
    void JustSummoned(Creature* summon) override
    {
        summon->GetMotionMaster()->MovePoint(POINT_LANDING, Landing);
        summons.Summon(summon);
    }

    private:
        bool _consumptionJunction;  ///< 成就标志：true表示吞噬增益未超过9层，成就可完成
};

/**
 * @brief 德拉克瑞入侵者AI结构体
 *
 * 处理入侵者的行为逻辑，包括：
 * - 移动到降落位置
 * - 降落后下坐骑并嘲讽首领
 */
struct npc_drakkari_invader : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 入侵者生物对象指针
     */
    npc_drakkari_invader(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 移动完成通知
     * @param type 移动类型
     * @param pointId 路径点ID
     *
     * @调用时机 当入侵者到达降落位置时
     * @note 入侵者会下坐骑、取消免疫并嘲讽托尔戈斯
     */
    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type == POINT_MOTION_TYPE && pointId == POINT_LANDING)
        {
            me->Dismount();                ///< 下坐骑
            me->SetImmuneToAll(false);     ///< 取消所有免疫状态，可以被攻击
            DoCastAOE(SPELL_INVADER_TAUNT); ///< 施放嘲讽，强制托尔戈斯攻击自己
        }
    }
};

/**
 * @brief 吞噬法术脚本（49380, 59803）
 *
 * 处理吞噬法术的效果：
 * - 被吞噬的目标会反过来给托尔戈斯施加增益效果
 */
class spell_trollgore_consume : public SpellScript
{
    PrepareSpellScript(spell_trollgore_consume);

    /**
     * @brief 验证法术依赖
     * @param spellInfo 法术信息
     * @return 验证是否成功
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_CONSUME_BUFF });
    }

    /**
     * @brief 处理吞噬效果
     * @param effIndex 效果索引
     *
     * @调用时机 当吞噬法术命中目标时
     * @note 让目标给施法者施加吞噬增益效果
     */
    void HandleConsume(SpellEffIndex /*effIndex*/)
    {
        if (Unit* target = GetHitUnit())
            target->CastSpell(GetCaster(), SPELL_CONSUME_BUFF, true);
    }

    /**
     * @brief 注册法术效果处理函数
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_trollgore_consume::HandleConsume, EFFECT_1, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief 尸体爆炸光环脚本（49555, 59807）
 *
 * 处理尸体爆炸的周期性效果：
 * - 第2次触发时造成伤害
 * - 光环移除时让尸体消失
 */
class spell_trollgore_corpse_explode : public AuraScript
{
    PrepareAuraScript(spell_trollgore_corpse_explode);

    /**
     * @brief 验证法术依赖
     * @param spellInfo 法术信息
     * @return 验证是否成功
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_CORPSE_EXPLODE_DAMAGE });
    }

    /**
     * @brief 周期性触发处理
     * @param aurEff 光环效果
     *
     * @调用时机 每次周期性触发时
     * @note 在第2次触发时施放伤害效果
     */
    void PeriodicTick(AuraEffect const* aurEff)
    {
        if (aurEff->GetTickNumber() == 2)
            if (Unit* caster = GetCaster())
                caster->CastSpell(GetTarget(), SPELL_CORPSE_EXPLODE_DAMAGE, aurEff);
    }

    /**
     * @brief 光环移除处理
     * @param aurEff 光环效果
     * @param mode 处理模式
     *
     * @调用时机 光环效果被移除时
     * @note 让目标尸体消失
     */
    void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Creature* target = GetTarget()->ToCreature())
            target->DespawnOrUnsummon();
    }

    /**
     * @brief 注册光环效果处理函数
     */
    void Register() override
    {
        OnEffectPeriodic += AuraEffectPeriodicFn(spell_trollgore_corpse_explode::PeriodicTick, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY);
        AfterEffectRemove += AuraEffectRemoveFn(spell_trollgore_corpse_explode::HandleRemove, EFFECT_0, SPELL_AURA_PERIODIC_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 入侵者嘲讽触发法术脚本（49405）
 *
 * 处理入侵者的嘲讽机制：
 * - 让被嘲讽的目标反过来嘲讽施法者
 */
class spell_trollgore_invader_taunt : public SpellScript
{
    PrepareSpellScript(spell_trollgore_invader_taunt);

    /**
     * @brief 验证法术依赖
     * @param spellInfo 法术信息
     * @return 验证是否成功
     */
    bool Validate(SpellInfo const* spellInfo) override
    {
        return ValidateSpellInfo({ static_cast<uint32>(spellInfo->GetEffect(EFFECT_0).CalcValue()) });
    }

    /**
     * @brief 处理嘲讽效果
     * @param effIndex 效果索引
     *
     * @调用时机 法术命中目标时
     * @note 让目标对施法者施放嘲讽效果
     */
    void HandleTaunt(SpellEffIndex /*effIndex*/)
    {
        if (Unit* target = GetHitUnit())
            target->CastSpell(GetCaster(), uint32(GetEffectValue()), true);
    }

    /**
     * @brief 注册法术效果处理函数
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_trollgore_invader_taunt::HandleTaunt, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

/**
 * @brief "Consumption Junction"成就脚本
 *
 * 检测玩家是否在没有让托尔戈斯的吞噬增益叠加超过9层的情况下击败他
 */
class achievement_consumption_junction : public AchievementCriteriaScript
{
    public:
        /**
         * @brief 构造函数
         */
        achievement_consumption_junction() : AchievementCriteriaScript("achievement_consumption_junction")
        {
        }

        /**
         * @brief 检查成就条件
         * @param player 玩家对象（未使用）
         * @param target 目标单位（应该是托尔戈斯）
         * @return true表示成就条件满足，false表示不满足
         *
         * @调用时机 当成就进度需要更新时
         */
        bool OnCheck(Player* /*player*/, Unit* target) override
        {
            if (!target)
                return false;

            if (Creature* Trollgore = target->ToCreature())
                if (Trollgore->AI()->GetData(DATA_CONSUMPTION_JUNCTION))
                    return true;

            return false;
        }
};

/**
 * @brief 注册脚本
 *
 * 将所有脚本注册到脚本系统中，包括：
 * - 托尔戈斯首领AI
 * - 德拉克瑞入侵者AI
 * - 各种法术脚本
 * - 成就脚本
 */
void AddSC_boss_trollgore()
{
    RegisterDrakTharonKeepCreatureAI(boss_trollgore);
    RegisterDrakTharonKeepCreatureAI(npc_drakkari_invader);
    RegisterSpellScript(spell_trollgore_consume);
    RegisterSpellScript(spell_trollgore_corpse_explode);
    RegisterSpellScript(spell_trollgore_invader_taunt);
    new achievement_consumption_junction();
}
