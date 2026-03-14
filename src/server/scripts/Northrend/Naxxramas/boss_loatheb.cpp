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
 * @file boss_loatheb.cpp
 * @brief 纳克萨玛斯副本 - 洛欧塞布 Boss 战斗脚本模块
 *
 * 本模块实现了 Boss 洛欧塞布 (Loatheb) 的完整战斗逻辑，包括：
 * - 死灵光环周期性施放和管理
 * - 死亡之花和必然厄运技能管理
 * - 孢子召唤和处理
 * - "孢子败者"成就判定
 *
 * 洛欧塞布战斗特点：
 * - 死灵光环使治疗效果降低 100%，每隔 17 秒有 3 秒的治疗窗口期
 * - 死亡之花每 30 秒对所有玩家造成持续伤害，结束时造成额外伤害
 * - 必然厄运每 30 秒对所有玩家造成大量伤害，战斗后期加速施放
 * - 孢子被击杀后会给周围玩家提供有益效果，但不能击杀太多孢子（成就要求）
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "naxxramas.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "SpellScript.h"

/**
 * @brief 洛欧塞布使用的法术 ID 枚举
 */
enum Spells
{
    SPELL_NECROTIC_AURA                     = 55593,    // 死灵光环 - 使治疗效果降低 100%
    SPELL_SUMMON_SPORE                      = 29234,    // 召唤孢子
    SPELL_DEATHBLOOM                        = 29865,    // 死亡之花 - 持续伤害，结束时造成额外伤害
    SPELL_INEVITABLE_DOOM                   = 29204,    // 必然厄运 - 全团伤害
    SPELL_FUNGAL_CREEP                      = 29232,    // 真菌蔓延 - 孢子死亡时给予的增益效果

    SPELL_DEATHBLOOM_FINAL_DAMAGE           = 55594,    // 死亡之花最终伤害
};

/**
 * @brief 洛欧塞布的台词 ID 枚举
 */
enum Texts
{
    SAY_NECROTIC_AURA_APPLIED       = 0,    // 死灵光环施放时的提示
    SAY_NECROTIC_AURA_REMOVED       = 1,    // 死灵光环消失时的提示
    SAY_NECROTIC_AURA_FADING        = 2,    // 死灵光环即将消失的提示
};

/**
 * @brief 战斗事件 ID 枚举，用于事件调度系统
 */
enum Events
{
    EVENT_NECROTIC_AURA = 1,                // 死灵光环施放事件
    EVENT_DEATHBLOOM,                       // 死亡之花施放事件
    EVENT_INEVITABLE_DOOM,                  // 必然厄运施放事件
    EVENT_SPORE,                            // 召唤孢子事件
    EVENT_NECROTIC_AURA_FADING,             // 死灵光环即将消失提示事件
    EVENT_NECROTIC_AURA_FADED               // 死灵光环消失提示事件
};

/**
 * @brief 成就相关数据枚举
 */
enum Achievement
{
    DATA_ACHIEVEMENT_SPORE_LOSER    = 21822183,     // 孢子败者成就数据标识
};

/**
 * @brief 洛欧塞布 Boss AI 结构体
 *
 * 继承自 BossAI，实现洛欧塞布的完整战斗逻辑。
 * 洛欧塞布战斗的核心机制是治疗限制和逐渐增加的全团伤害。
 */
struct boss_loatheb : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature Boss 生物对象指针
     */
    boss_loatheb(Creature* creature) : BossAI(creature, BOSS_LOATHEB), _doomCounter(0), _sporeLoser(true) { }

    /**
     * @brief 重置 Boss 状态
     *
     * 当 Boss 脱离战斗或重置时调用，恢复 Boss 到初始状态。
     * 移除玩家身上的真菌蔓延增益效果。
     *
     * @调用时机 Boss 脱离战斗、重置副本、Boss 初始化时
     */
    void Reset() override
    {
        _Reset();                                                       // 调用父类重置函数
        instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_FUNGAL_CREEP); // 移除所有玩家的真菌蔓延效果
        _doomCounter = 0;                                               // 重置必然厄运计数器
        _sporeLoser = true;                                             // 重置孢子败者成就状态
    }

    /**
     * @brief Boss 进入战斗时的回调函数
     * @param who 激活 Boss 的目标
     *
     * 初始化战斗阶段，设置事件调度。
     * 主要工作：
     * 1. 17 秒后施放第一次死灵光环
     * 2. 5 秒后施放第一次死亡之花
     * 3. 18 秒后召唤第一个孢子
     * 4. 2 分钟后施放第一次必然厄运
     *
     * @调用时机 Boss 被玩家激活进入战斗状态时
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_NECROTIC_AURA, 17s);        // 17秒后施放死灵光环
        events.ScheduleEvent(EVENT_DEATHBLOOM, 5s);            // 5秒后施放死亡之花
        events.ScheduleEvent(EVENT_SPORE, 18s);                // 18秒后召唤孢子
        events.ScheduleEvent(EVENT_INEVITABLE_DOOM, 2min);     // 2分钟后施放必然厄运
    }

    /**
     * @brief 召唤生物死亡时的回调函数
     * @param summon 死亡的召唤生物（孢子）
     * @param killer 击杀者（未使用）
     *
     * 当孢子被击杀时调用。
     * 标记"孢子败者"成就失败，并在孢子位置施放真菌蔓延效果。
     *
     * @调用时机 召唤的孢子生物死亡时
     */
    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        _sporeLoser = false;                                   // 标记孢子败者成就失败
        summon->CastSpell(summon, SPELL_FUNGAL_CREEP, true);   // 孢子死亡时施放真菌蔓延增益
    }

    /**
     * @brief 获取 Boss 自定义数据
     * @param id 数据类型标识
     * @return 如果是孢子败者成就查询且成就仍然可能，返回 1；否则返回 0
     *
     * @调用时机 成就系统检查是否完成"孢子败者"成就时
     */
    uint32 GetData(uint32 id) const override
    {
        return (_sporeLoser && id == DATA_ACHIEVEMENT_SPORE_LOSER) ? 1u : 0u;
    }

    /**
     * @brief Boss AI 主更新函数
     * @param diff 距离上次更新的时间间隔（毫秒）
     *
     * 这是 Boss AI 的核心逻辑循环，每帧调用一次。
     * 处理事件调度、技能施放等所有战斗逻辑。
     *
     * 主要处理的事件：
     * - EVENT_NECROTIC_AURA: 死灵光环，每 20 秒施放一次，持续 17 秒
     * - EVENT_DEATHBLOOM: 死亡之花，每 30 秒施放一次
     * - EVENT_INEVITABLE_DOOM: 必然厄运，前 6 次每 30 秒一次，之后加速
     * - EVENT_SPORE: 召唤孢子，10人模式 36 秒一次，25人模式 15 秒一次
     * - EVENT_NECROTIC_AURA_FADING: 死灵光环即将消失提示（14秒后）
     * - EVENT_NECROTIC_AURA_FADED: 死灵光环消失提示（17秒后）
     *
     * @调用时机 每帧（服务器 tick）调用一次，频率约为 50ms
     * @性能注意 频繁调用，需要保持代码简洁高效
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效目标，如果没有则直接返回
        if (!UpdateVictim())
            return;

        // 更新事件调度器
        events.Update(diff);

        // 处理所有待执行的事件
        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_NECROTIC_AURA:
                    // 死灵光环：使所有治疗效果降低 100%，持续 17 秒
                    DoCastAOE(SPELL_NECROTIC_AURA);
                    Talk(SAY_NECROTIC_AURA_APPLIED);              // 提示玩家光环已施放
                    events.ScheduleEvent(EVENT_NECROTIC_AURA_FADING, 14s);  // 14秒后提示即将消失
                    events.ScheduleEvent(EVENT_NECROTIC_AURA_FADED, 17s);   // 17秒后提示已消失
                    events.Repeat(Seconds(20));                    // 20秒后再次施放
                    break;

                case EVENT_DEATHBLOOM:
                    // 死亡之花：对所有玩家造成持续伤害，30秒后造成最终伤害
                    DoCastAOE(SPELL_DEATHBLOOM);
                    events.Repeat(Seconds(30));                    // 30秒后再次施放
                    break;

                case EVENT_INEVITABLE_DOOM:
                    // 必然厄运：对所有玩家造成大量伤害
                    ++_doomCounter;                                // 增加计数器
                    DoCastAOE(SPELL_INEVITABLE_DOOM);
                    // 前 6 次 30 秒一次，之后交替使用 14 秒和 17 秒间隔
                    if (_doomCounter > 6)
                        events.Repeat((_doomCounter & 1) ? Seconds(14) : Seconds(17));
                    else
                        events.Repeat(Seconds(30));
                    break;

                case EVENT_SPORE:
                    // 召唤孢子：孢子被击杀后会给周围玩家增益
                    DoCast(me, SPELL_SUMMON_SPORE, false);
                    events.Repeat(RAID_MODE(Seconds(36), Seconds(15)));  // 10人 36 秒，25人 15 秒
                    break;

                case EVENT_NECROTIC_AURA_FADING:
                    // 死灵光环即将消失提示：提前 3 秒通知玩家准备治疗
                    Talk(SAY_NECROTIC_AURA_FADING);
                    break;

                case EVENT_NECROTIC_AURA_FADED:
                    // 死灵光环消失提示：通知玩家可以治疗
                    Talk(SAY_NECROTIC_AURA_REMOVED);
                    break;

                default:
                    break;
            }
        }

        // 如果准备就绪，执行近战攻击
        DoMeleeAttackIfReady();
    }

private:
    uint8 _doomCounter;         // 必然厄运施放计数器，用于控制后期加速施放
    bool _sporeLoser;           // 孢子败者成就是否仍可能完成（true = 尚未击杀孢子）
};

/**
 * @brief 孢子败者成就脚本
 *
 * 实现"孢子败者"成就的判定逻辑。
 * 成就要求：在整个战斗过程中不击杀任何孢子。
 */
class achievement_spore_loser : public AchievementCriteriaScript
{
    public:
        /**
         * @brief 构造函数
         */
        achievement_spore_loser() : AchievementCriteriaScript("achievement_spore_loser") { }

        /**
         * @brief 检查是否满足成就条件
         * @param source 玩家对象（未使用）
         * @param target 目标单位（应为洛欧塞布 Boss）
         * @return 如果满足成就条件返回 true，否则返回 false
         *
         * 通过查询洛欧塞布 AI 的 _sporeLoser 标志来判断是否击杀了孢子。
         *
         * @调用时机 成就系统检查该成就是否完成时
         */
        bool OnCheck(Player* /*source*/, Unit* target) override
        {
            return target && target->GetAI()->GetData(DATA_ACHIEVEMENT_SPORE_LOSER);
        }
};

/**
 * @brief 死亡之花光环脚本（法术 ID: 29865, 55053）
 *
 * 处理死亡之花的逻辑：在光环到期时造成最终伤害。
 * 死亡之花会在玩家身上持续 30 秒，每秒造成伤害，到期时造成一次性大量伤害。
 */
class spell_loatheb_deathbloom : public AuraScript
{
    PrepareAuraScript(spell_loatheb_deathbloom);

    /**
     * @brief 验证法术信息
     * @param spell 法术信息（未使用）
     * @return 如果法术验证通过返回 true
     *
     * 验证死亡之花最终伤害法术是否存在。
     */
    bool Validate(SpellInfo const* /*spell*/) override
    {
        return ValidateSpellInfo({ SPELL_DEATHBLOOM_FINAL_DAMAGE });
    }

    /**
     * @brief 光环移除后的处理函数
     * @param eff 光环效果
     * @param mode 光环效果处理模式（未使用）
     *
     * 当死亡之花光环被移除时调用。
     * 如果是因为到期而移除，则对目标施放最终伤害法术。
     *
     * @调用时机 光环效果被移除时
     */
    void AfterRemove(AuraEffect const* eff, AuraEffectHandleModes /*mode*/)
    {
        // 只在光环到期时施放最终伤害，而不是被驱散或其他方式移除
        if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_EXPIRE)
            return;

        // 对目标施放死亡之花最终伤害
        GetTarget()->CastSpell(nullptr, SPELL_DEATHBLOOM_FINAL_DAMAGE, CastSpellExtraArgs(eff).SetOriginalCaster(GetCasterGUID()));
    }

    /**
     * @brief 注册光环效果处理函数
     *
     * 将 AfterRemove 函数注册到周期性伤害效果的移除处理上。
     */
    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_loatheb_deathbloom::AfterRemove, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 注册洛欧塞布 Boss 脚本
 *
 * 将所有洛欧塞布相关的脚本注册到系统中，包括：
 * - Boss AI
 * - 成就脚本
 * - 法术脚本
 *
 * @调用时机 服务器启动时，由脚本加载系统自动调用
 */
void AddSC_boss_loatheb()
{
    RegisterNaxxramasCreatureAI(boss_loatheb);              // 注册洛欧塞布 Boss AI
    new achievement_spore_loser();                          // 注册孢子败者成就脚本
    RegisterSpellScript(spell_loatheb_deathbloom);          // 注册死亡之花法术脚本
}
