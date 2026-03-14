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
 * @file boss_ironaya.cpp
 * @brief Uldaman 副本首领艾隆纳亚 (Ironaya) 的脚本实现
 *
 * 本模块实现了艾隆纳亚首领战的完整逻辑，包括：
 * - 通过基石 (Keystone) 激活后的移动和战斗
 * - 基于生命值阈值的特殊技能触发
 * - 使用 TaskScheduler 进行技能调度
 *
 * 战斗机制：
 * - 初始处于冻结状态，需要插入基石才能激活
 * - 激活后移动到指定位置开始战斗
 * - 每13秒施放弧形斩击
 * - 生命值低于50%时施放击退并重置仇恨
 * - 生命值低于25%时施放战争践踏
 */

/* ScriptData
SDName: Boss_Ironaya
SD%Complete: 100
SDComment:
SDCategory: Uldaman
EndScriptData */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "uldaman.h"

/**
 * @brief 艾隆纳亚使用的法术和标识枚举
 */
enum Ironaya
{
    SPELL_ARCINGSMASH           = 8374,   ///< 弧形斩击 - 对前方锥形区域造成伤害
    SPELL_KNOCKAWAY             = 10101,  ///< 击退 - 击退当前目标并降低其仇恨
    SPELL_WSTOMP                = 11876   ///< 战争践踏 - 对周围所有敌人造成伤害并使其眩晕
};

/**
 * @brief 艾隆纳亚首领AI结构体
 *
 * 继承自 ScriptedAI，实现艾隆纳亚的战斗逻辑。
 * 使用 TaskScheduler 进行技能调度，支持基于生命值的特殊技能触发。
 */
struct boss_ironaya : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_ironaya(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    /**
     * @brief 初始化技能使用标志
     *
     * 这些标志用于确保基于血量的技能只触发一次。
     */
    void Initialize()
    {
        _hasCastKnockaway = false;  ///< 是否已施放过击退（50%血量触发）
        _hasCastWstomp = false;     ///< 是否已施放过战争践踏（25%血量触发）
    }

    /**
     * @brief 重置首领状态
     *
     * 清除所有计划任务并重置技能标志。
     */
    void Reset() override
    {
        _scheduler.CancelAll();
        Initialize();
    }

    /**
     * @brief 受到伤害时的处理
     * @param attacker 攻击者（未使用）
     * @param damage 伤害值（未使用）
     * @param damageType 伤害类型（未使用）
     * @param spellInfo 法术信息（未使用）
     *
     * 基于当前生命值百分比触发特殊技能：
     * - 低于50%：施放击退并重置当前目标的仇恨
     * - 低于25%：施放战争践踏
     *
     * 这些技能在每次战斗中只会触发一次。
     */
    void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 血量低于50%且尚未施放过击退
        if (!_hasCastKnockaway && HealthBelowPct(50) && me->GetVictim())
        {
            _hasCastKnockaway = true;
            DoCastVictim(SPELL_KNOCKAWAY, true);
            ResetThreat(me->GetVictim(), me);  // 重置当前目标的仇恨
        }

        // 血量低于25%且尚未施放过战争践踏
        if (!_hasCastWstomp && HealthBelowPct(25))
        {
            _hasCastWstomp = true;
            DoCastSelf(SPELL_WSTOMP);
        }
    }

    /**
     * @brief 进入战斗时的处理
     * @param who 进入战斗的目标（未使用）
     *
     * 设置弧形斩击的定时任务，每13秒施放一次。
     * 使用 TaskScheduler 进行调度，支持精确的时间控制。
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        _scheduler.Schedule(3s, [this](TaskContext task)
        {
            DoCastSelf(SPELL_ARCINGSMASH);
            task.Repeat(13s);  // 每13秒重复施放
        });
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 使用 TaskScheduler 更新计时器并执行计划的任务。
     * 如果没有有效目标则直接返回。
     *
     * @note TaskScheduler 的回调在执行 DoMeleeAttackIfReady() 之前被调用，
     *       确保近战攻击不会与法术施放冲突。
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        _scheduler.Update(diff, [this]
        {
            DoMeleeAttackIfReady();
        });
    }

private:
    TaskScheduler _scheduler;    ///< 任务调度器，用于管理定时技能
    bool _hasCastKnockaway;      ///< 已施放击退标志（防止重复触发）
    bool _hasCastWstomp;         ///< 已施放战争践踏标志（防止重复触发）
};

/**
 * @brief 脚本注册函数
 *
 * 此函数在脚本初始化时被调用一次。
 * 使用 RegisterUldamanCreatureAI 宏注册艾隆纳亚的AI。
 */
void AddSC_boss_ironaya()
{
    RegisterUldamanCreatureAI(boss_ironaya);
}
