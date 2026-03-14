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
 * @file boss_cyanigosa.cpp
 * @brief 紫罗兰监狱副本 - 塞安妮苟萨（Cyanigosa）首领战脚本
 *
 * 本文件实现了紫罗兰监狱副本最终首领塞安妮苟萨的战斗逻辑。
 * 塞安妮苟萨是一只蓝龙，也是玛里苟斯的仆从，负责领导对紫罗兰监狱的攻击。
 *
 * 主要功能：
 * - 塞安妮苟萨的战斗AI和技能循环
 * - 奥术真空技能的特殊处理（传送玩家）
 * - 英雄模式特有的法力毁灭技能
 * - 成就"Defenseless"的检测支持
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "SpellScript.h"
#include "ScriptedCreature.h"
#include "violet_hold.h"

/**
 * @brief 塞安妮苟萨使用的法术ID枚举
 */
enum Spells
{
    SPELL_SUMMON_PLAYER                 = 21150,  ///< 召唤玩家 - 用于奥术真空后传送玩家
    SPELL_ARCANE_VACUUM                 = 58694,  ///< 奥术真空 - 将所有玩家传送到自己身边
    SPELL_BLIZZARD                      = 58693,  ///< 暴风雪 - 范围冰霜伤害
    SPELL_MANA_DESTRUCTION              = 59374,  ///< 法力毁灭 - 英雄模式专属，摧毁目标法力
    SPELL_TAIL_SWEEP                    = 58690,  ///< 尾部横扫 - 对身后敌人造成伤害并击退
    SPELL_UNCONTROLLABLE_ENERGY         = 58688,  ///< 无法控制的能量 - 对目标造成奥术伤害
    SPELL_TRANSFORM                     = 58668   ///< 变形 - 塞安妮苟萨从精灵形态变为龙形态
};

/**
 * @brief 塞安妮苟萨的台词和喊话枚举
 */
enum Yells
{
    SAY_AGGRO                           = 0,  ///< 开战台词
    SAY_SLAY                            = 1,  ///< 击杀玩家台词
    SAY_DEATH                           = 2,  ///< 死亡台词
    SAY_SPAWN                           = 3,  ///< 出现台词
    SAY_DISRUPTION                      = 4,  ///< 干扰台词
    SAY_BREATH_ATTACK                   = 5,  ///< 呼吸攻击台词
    SAY_SPECIAL_ATTACK                  = 6   ///< 特殊攻击台词
};

/**
 * @brief 塞安妮苟萨首领AI结构体
 *
 * 实现了塞安妮苟萨的完整战斗逻辑，包括：
 * - 技能循环：奥术真空、暴风雪、尾部横扫、无法控制的能量
 * - 英雄模式的法力毁灭技能
 * - 使用任务调度器管理技能冷却
 */
struct boss_cyanigosa : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 首领生物对象指针
     */
    boss_cyanigosa(Creature* creature) : BossAI(creature, DATA_CYANIGOSA) { }

    /**
     * @brief 进入战斗
     * @param who 进入战斗的目标单位
     *
     * @调用时机 当塞安妮苟萨被玩家攻击或主动攻击玩家时
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        Talk(SAY_AGGRO);  ///< 播放开战台词
    }

    /**
     * @brief 击杀单位
     * @param victim 被击杀的单位
     *
     * @调用时机 当塞安妮苟萨击杀玩家时
     */
    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() == TYPEID_PLAYER)
            Talk(SAY_SLAY);
    }

    /**
     * @brief 首领死亡
     * @param killer 击杀首领的单位（可能为nullptr）
     *
     * @调用时机 当塞安妮苟萨生命值降至0时
     */
    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_DEATH);
        _JustDied();
    }

    /**
     * @brief 视线内移动检测
     * @param who 进入视线范围的单位（未使用）
     *
     * @note 塞安妮苟萨不主动攻击进入视线的玩家，
     *       因为她是由副本事件触发出现的
     */
    void MoveInLineOfSight(Unit* /*who*/) override { }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 主循环函数，使用任务调度器管理技能施放
     *
     * @调用时机 每个游戏帧（约每50毫秒）
     * @性能注意事项 使用调度器而非传统事件系统，简化代码结构
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        // 更新任务调度器，如果没有任务在执行则进行近战攻击
        scheduler.Update(diff,
            std::bind(&BossAI::DoMeleeAttackIfReady, this));
    }

    /**
     * @brief 安排技能任务
     *
     * 在进入战斗时调用，设置所有技能的施放循环
     *
     * @调用时机 JustEngagedWith之后自动调用
     * @note 使用Lambda表达式简化任务定义
     */
    void ScheduleTasks() override
    {
        // 奥术真空：每10秒施放一次，将所有玩家传送到身边
        scheduler.Schedule(Seconds(10), [this](TaskContext task)
        {
            DoCastAOE(SPELL_ARCANE_VACUUM);
            task.Repeat();
        });

        // 暴风雪：每15秒对随机目标施放，45码范围内
        scheduler.Schedule(Seconds(15), [this](TaskContext task)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 45.0f, true))
                DoCast(target, SPELL_BLIZZARD);
            task.Repeat();
        });

        // 尾部横扫：每20秒对当前目标施放
        scheduler.Schedule(Seconds(20), [this](TaskContext task)
        {
            DoCastVictim(SPELL_TAIL_SWEEP);
            task.Repeat();
        });

        // 无法控制的能量：每25秒对当前目标施放
        scheduler.Schedule(Seconds(25), [this](TaskContext task)
        {
            DoCastVictim(SPELL_UNCONTROLLABLE_ENERGY);
            task.Repeat();
        });

        // 英雄模式专属：法力毁灭
        if (IsHeroic())
        {
            // 每30秒对随机目标施放，50码范围内
            scheduler.Schedule(Seconds(30), [this](TaskContext task)
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 50.0f, true))
                    DoCast(target, SPELL_MANA_DESTRUCTION);
                task.Repeat();
            });
        }
    }
};

/**
 * @brief "Defenseless"成就脚本
 *
 * 检测玩家是否在没有使用防御水晶的情况下击败塞安妮苟萨
 */
class achievement_defenseless : public AchievementCriteriaScript
{
    public:
        /**
         * @brief 构造函数
         */
        achievement_defenseless() : AchievementCriteriaScript("achievement_defenseless") { }

        /**
         * @brief 检查成就条件
         * @param player 玩家对象（未使用）
         * @param target 目标单位（应该是塞安妮苟萨）
         * @return true表示成就条件满足，false表示不满足
         *
         * @调用时机 当成就进度需要更新时
         */
        bool OnCheck(Player* /*player*/, Unit* target) override
        {
            if (!target)
                return false;

            InstanceScript* instance = target->GetInstanceScript();
            if (!instance)
                return false;

            // 查询副本数据中的DEFENSELESS标志
            return instance->GetData(DATA_DEFENSELESS) != 0;
        }
};

/**
 * @brief 奥术真空法术脚本（58694）
 *
 * 处理奥术真空技能的特殊效果：
 * - 将被命中的玩家传送到塞安妮苟萨身边
 */
class spell_cyanigosa_arcane_vacuum : public SpellScript
{
    PrepareSpellScript(spell_cyanigosa_arcane_vacuum);

    /**
     * @brief 验证法术依赖
     * @param spellInfo 法术信息
     * @return 验证是否成功
     */
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_SUMMON_PLAYER });
    }

    /**
     * @brief 处理脚本效果
     * @param effIndex 效果索引
     *
     * @调用时机 当奥术真空法术命中目标时
     * @note 对每个被命中的玩家施放召唤法术，将其传送到施法者身边
     */
    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        GetCaster()->CastSpell(GetHitUnit(), SPELL_SUMMON_PLAYER, true);
    }

    /**
     * @brief 注册法术效果处理函数
     */
    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_cyanigosa_arcane_vacuum::HandleScript, EFFECT_0, SPELL_EFFECT_DUMMY);
    }
};

/**
 * @brief 注册脚本
 *
 * 将所有脚本注册到脚本系统中，包括：
 * - 塞安妮苟萨首领AI
 * - "Defenseless"成就脚本
 * - 奥术真空法术脚本
 */
void AddSC_boss_cyanigosa()
{
    RegisterVioletHoldCreatureAI(boss_cyanigosa);
    new achievement_defenseless();
    RegisterSpellScript(spell_cyanigosa_arcane_vacuum);
}
