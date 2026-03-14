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
 * @file boss_aku_mai.cpp
 * @brief 黑暗深渊副本 - Boss Aku'mai（阿库麦尔）脚本
 *
 * Aku'mai是黑暗深渊副本的最终Boss，一只巨大的海蛇。
 * 战斗机制：
 * - 周期性施放毒云技能，对目标造成持续自然伤害
 * - 当生命值低于30%时，进入狂暴状态，提高攻击力
 *
 * 这是副本的最终挑战，击败后会召唤NPC Morridune，
 * 为玩家提供离开副本的传送服务。
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "blackfathom_deeps.h"

/**
 * @brief 法术ID枚举
 */
enum Spells
{
    SPELL_POISON_CLOUD     = 3815,  ///< 毒云 - 在目标位置制造毒云，造成持续自然伤害
    SPELL_FRENZIED_RAGE    = 3490   ///< 狂暴之怒 - 提高攻击力和攻击速度
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_POISON_CLOUD     = 1,     ///< 毒云施放事件
    EVENT_FRENZIED_RAGE             ///< 狂暴施放事件（未使用，狂暴由伤害触发）
};

/**
 * @struct boss_aku_mai
 * @brief Boss Aku'mai AI实现
 *
 * 继承自BossAI基类，实现最终Boss的战斗逻辑。
 * 战斗分为两个阶段：
 * - 正常阶段：周期性施放毒云
 * - 狂暴阶段：生命值低于30%时触发，大幅提升伤害
 */
struct boss_aku_mai : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物指针
     */
    boss_aku_mai(Creature* creature) : BossAI(creature, DATA_AKU_MAI)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     *
     * 将狂暴状态标志重置为false，确保每次战斗开始时状态正确。
     * 在构造函数和Reset()中都会调用此函数。
     */
    void Initialize()
    {
        IsEnraged = false;
    }

    /**
     * @brief 重置Boss状态
     *
     * 当战斗重置时调用（如脱离战斗、团队灭团）。
     * 执行：
     * 1. 初始化成员变量（重置狂暴状态）
     * 2. 调用父类的_Reset()处理标准的Boss重置逻辑
     */
    void Reset() override
    {
        Initialize();
        _Reset();
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     *
     * 当Boss被攻击或主动攻击玩家时触发。
     * 执行：
     * 1. 调用父类的JustEngagedWith()处理标准的进入战斗逻辑
     * 2. 安排首次毒云施放，5-9秒后执行
     *
     * @note 毒云的随机延迟增加了战斗的不可预测性
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_POISON_CLOUD, 5s, 9s);
    }

    /**
     * @brief 受到伤害时调用
     * @param attacker 攻击者（未使用）
     * @param damage 受到的伤害值
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     *
     * 监控Boss的生命值，当生命值低于30%且未进入狂暴状态时：
     * 1. 对自己施放狂暴之怒，提高攻击力
     * 2. 设置狂暴状态标志，防止重复触发
     *
     * @note 这是Boss战的关键转折点，治疗需要加强治疗量
     */
    void DamageTaken(Unit* /*atacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        if (!IsEnraged && me->HealthBelowPctDamaged(30, damage))
        {
            DoCast(me, SPELL_FRENZIED_RAGE);
            IsEnraged = true;
        }
    }

    /**
     * @brief 执行事件
     * @param eventId 事件ID
     *
     * 处理事件调度系统触发的事件：
     * - EVENT_POISON_CLOUD:
     *   1. 对当前目标施放毒云
     *   2. 安排下一次毒云，25-50秒后执行
     *
     * 毒云的长冷却时间给玩家提供了治疗和调整站位的时间窗口。
     */
    void ExecuteEvent(uint32 eventId) override
    {
        switch (eventId)
        {
            case EVENT_POISON_CLOUD:
                DoCastVictim(SPELL_POISON_CLOUD);
                events.ScheduleEvent(EVENT_POISON_CLOUD, 25s, 50s);
                break;
            default:
                break;
        }
    }

    private:
        bool IsEnraged;  ///< 是否已进入狂暴状态
};

/**
 * @brief 注册Boss Aku'mai脚本
 *
 * 使用宏注册Boss AI，使其在副本中生效。
 * 该宏会自动处理AI的创建和注册流程。
 */
void AddSC_boss_aku_mai()
{
    RegisterBlackfathomDeepsCreatureAI(boss_aku_mai);
}
