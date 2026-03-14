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
 * @file boss_hungarfen.cpp
 * @brief 幽暗沼泽副本首领"霍加尔芬"的AI脚本实现
 *
 * 模块职责：
 * - 实现霍加尔芬首领的战斗AI逻辑
 * - 实现幽暗蘑菇的AI行为
 * - 管理蘑菇召唤和恶臭孢子技能
 *
 * 首领信息：
 * - 位置：幽暗沼泽副本（Coilfang Reservoir - The Underbog）
 * - 类型：真菌巨人首领
 * - 难度：普通/英雄模式
 * - 战斗特点：召唤腐烂蘑菇，生命值低于20%时释放恶臭孢子
 *
 * 战斗机制：
 * - 普通模式：每10秒召唤一个蘑菇
 * - 英雄模式：每2.5秒召唤一个蘑菇，额外施放酸液喷泉
 * - 生命值低于20%时释放恶臭孢子（AOE伤害并击退）
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "SpellAuras.h"
#include "the_underbog.h"

/**
 * @brief 霍加尔芬文本枚举
 * 定义霍加尔芬使用的文本和表情
 */
enum HungarfenTexts
{
    EMOTE_ROARS                      = 0  ///< 咆哮表情 - 生命值低于20%时使用
};

/**
 * @brief 霍加尔芬法术枚举
 * 定义霍加尔芬使用的所有法术技能
 */
enum HungarfenSpells
{
    // 首领法术
    SPELL_FOUL_SPORES                = 31673, ///< 恶臭孢子 - AOE伤害并击退，生命值低于20%时施放
    SPELL_SUMMON_UNDERBOG_MUSHROOM   = 31692, ///< 召唤幽暗蘑菇 - 在随机目标位置召唤蘑菇
    SPELL_PUTRID_MUSHROOM_PRIMER     = 31693, ///< 腐烂蘑菇启动器 - 辅助法术（待实现）
    SPELL_DESPAWN_UNDERBOG_MUSHROOMS = 34874, ///< 消失幽暗蘑菇 - 清除所有召唤的蘑菇
    SPELL_ACID_GEYSER                = 38739, ///< 酸液喷泉 - 英雄模式专属技能

    // 蘑菇法术
    SPELL_SPORE_CLOUD                = 34168, ///< 孢子云 - 蘑菇成熟后释放的AOE伤害
    SPELL_PUTRID_MUSHROOM            = 31690, ///< 腐烂蘑菇 - 蘑菇的基础形态
    SPELL_SHRINK                     = 31691, ///< 缩小 - 蘑菇初始状态
    SPELL_GROW                       = 31698  ///< 生长 - 蘑菇成长阶段，可叠加
};

/**
 * @struct boss_hungarfen
 * @brief 霍加尔芬首领AI结构体
 *
 * 继承自BossAI基类，实现霍加尔芬的完整战斗逻辑。
 * 霍加尔芬是一名真菌巨人首领，擅长召唤蘑菇和释放孢子攻击。
 *
 * 战斗流程：
 * 1. 进入战斗后开始定期召唤蘑菇
 * 2. 英雄模式下额外施放酸液喷泉
 * 3. 生命值低于20%时释放恶臭孢子（仅一次）
 */
struct boss_hungarfen : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化基类BossAI，设置首领数据ID为DATA_HUNGARFEN，
     * 并初始化咆哮标志为false。
     */
    boss_hungarfen(Creature* creature) : BossAI(creature, DATA_HUNGARFEN), _roared(false) { }

    /**
     * @brief 重置函数
     *
     * 当首领脱离战斗或重置时调用。
     * 执行以下操作：
     * 1. 调用基类Reset方法
     * 2. 重置咆哮标志
     * 3. 设置反应状态为主动攻击
     *
     * 调用时机：
     * - 首领重置时
     * - 首领脱离战斗时
     */
    void Reset() override
    {
        BossAI::Reset();
        _roared = false;                      // 重置咆哮标志，允许下次战斗再次触发
        me->SetReactState(REACT_AGGRESSIVE);  // 恢复主动攻击状态
    }

    /**
     * @brief 进入战斗处理函数
     * @param who 触发战斗的单位
     *
     * 当霍加尔芬进入战斗状态时调用。
     * 设置蘑菇召唤调度器：
     * - 普通模式：每10秒召唤一次蘑菇
     * - 英雄模式：每2.5秒召唤一次蘑菇
     *
     * 英雄模式额外添加酸液喷泉技能：
     * - 首次施放：3-5秒后
     * - 后续施放：每10-15秒
     *
     * 调用时机：首领首次进入战斗时
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);

        // 调度蘑菇召唤任务
        _scheduler.Schedule(IsHeroic() ? 2500ms : 5s, [this](TaskContext task)
        {
            /// @todo 应在此处施放SPELL_PUTRID_MUSHROOM_PRIMER并在法术脚本中处理
            // 选择随机目标并在其位置召唤蘑菇
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                target->CastSpell(target, SPELL_SUMMON_UNDERBOG_MUSHROOM, true);
            // 重复调度：英雄模式2.5秒，普通模式10秒
            task.Repeat(IsHeroic() ? 2500ms : 10s);
        });

        // 英雄模式专属：添加酸液喷泉技能
        if (IsHeroic())
        {
            _scheduler.Schedule(3s, 5s, [this](TaskContext task)
            {
                // 对随机目标施放酸液喷泉
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    DoCast(target, SPELL_ACID_GEYSER);
                // 每10-15秒重复施放
                task.Repeat(10s, 15s);
            });
        }
    }

    /**
     * @brief 进入逃避模式处理函数
     * @param why 逃避原因
     *
     * 当首领脱离战斗进入逃避模式时调用。
     * 首先清除所有召唤的蘑菇，然后调用基类的逃避逻辑。
     *
     * 调用时机：
     * - 所有玩家死亡或离开副本
     * - 首领重置
     */
    void EnterEvadeMode(EvadeReason why) override
    {
        // 清除所有召唤的蘑菇
        DoCastSelf(SPELL_DESPAWN_UNDERBOG_MUSHROOMS, true);
        BossAI::EnterEvadeMode(why);
    }

    /**
     * @brief 死亡处理函数
     * @param killer 击杀者
     *
     * 当霍加尔芬死亡时调用。
     * 首先清除所有召唤的蘑菇，然后调用基类的死亡逻辑。
     *
     * 调用时机：首领先命值降为0时
     */
    void JustDied(Unit* killer) override
    {
        // 死亡时清除所有蘑菇，避免残留
        DoCastSelf(SPELL_DESPAWN_UNDERBOG_MUSHROOMS, true);
        BossAI::JustDied(killer);
    }

    /**
     * @brief AI更新函数
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 每个服务器tick调用一次，处理霍加尔芬的主要战斗逻辑。
     *
     * 处理流程：
     * 1. 检查是否有有效的战斗目标
     * 2. 更新任务调度器
     * 3. 检查生命值是否低于20%，触发恶臭孢子（仅一次）
     *
     * 恶臭孢子触发流程：
     * - 生命值低于20%时播放咆哮表情
     * - 设置为被动状态，停止攻击
     * - 2秒后施放恶臭孢子
     * - 恢复主动攻击状态
     *
     * 性能注意事项：
     * - 使用任务调度器优化技能施放时机
     * - 避免频繁的生命值检查
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查是否有有效的攻击目标
        if (!UpdateVictim())
            return;

        // 更新任务调度器，在空闲时执行近战攻击
        _scheduler.Update(diff, [this]
        {
            DoMeleeAttackIfReady();
        });

        // 检查生命值是否低于20%且尚未咆哮
        if (!HealthAbovePct(20) && !_roared)
        {
            Talk(EMOTE_ROARS);                  // 播放咆哮表情
            _roared = true;                      // 标记已咆哮，避免重复触发
            me->SetReactState(REACT_PASSIVE);    // 设置为被动状态，准备施放恶臭孢子

            // 调度恶臭孢子施放：2秒后执行
            _scheduler.Schedule(2s, [this](TaskContext /*task*/)
            {
                DoCastSelf(SPELL_FOUL_SPORES);      // 施放恶臭孢子
                me->SetReactState(REACT_AGGRESSIVE); // 恢复主动攻击状态
            });
        }
    }

private:
    TaskScheduler _scheduler;  ///< 任务调度器，用于管理定时任务
    bool _roared;              ///< 咆哮标志，确保恶臭孢子只触发一次
};

/**
 * @struct npc_underbog_mushroom
 * @brief 幽暗蘑菇AI结构体
 *
 * 继承自ScriptedAI，实现霍加尔芬召唤的腐烂蘑菇的行为逻辑。
 * 蘑菇会从小到大生长，成熟后释放孢子云，最后消失。
 *
 * 生命周期：
 * 1. 初始化：设置被动状态，施放缩小和腐烂蘑菇形态
 * 2. 生长阶段：每2秒施放一次生长法术，直到达到随机层数（8-10层）
 * 3. 成熟阶段：施放孢子云，造成AOE伤害
 * 4. 消失阶段：移除生长光环，4秒后消失
 *
 * 注意：蘑菇不会移动或攻击，只是一个被动的环境危害
 */
struct npc_underbog_mushroom : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化蘑菇AI，设置计数器初始值为0。
     */
    npc_underbog_mushroom(Creature* creature) : ScriptedAI(creature), _counter(0) { }

    /**
     * @brief AI初始化函数
     *
     * 当蘑菇被创建时调用，设置蘑菇的初始状态和生长流程。
     *
     * 初始化流程：
     * 1. 随机设置生长目标层数（8、9或10层）
     * 2. 设置被动反应状态（不主动攻击）
     * 3. 施放缩小法术（初始状态）
     * 4. 施放腐烂蘑菇法术（基础形态）
     * 5. 调度生长任务：
     *    - 1秒后开始生长
     *    - 每2秒施放一次生长法术
     *    - 达到目标层数后施放孢子云
     *    - 孢子云持续4秒后消失
     *
     * 调用时机：蘑菇被召唤时
     *
     * 设计说明：
     * - 生长层数是随机的（基于数据包分析），使蘑菇大小不同
     * - 使用堆叠数而非重复计数器，因为第一次堆叠不是在任务重复时应用
     */
    void InitializeAI() override
    {
        // 随机选择生长目标层数（8、9或10），决定蘑菇最终大小
        _counter = RAND(8, 9, 10);
        me->SetReactState(REACT_PASSIVE);  // 设置为被动状态，不主动攻击
        DoCastSelf(SPELL_SHRINK);          // 施放缩小法术，设置初始大小
        DoCastSelf(SPELL_PUTRID_MUSHROOM); // 施放腐烂蘑菇形态

        // 调度生长任务
        _scheduler.Schedule(1s, [this](TaskContext task)
        {
            DoCastSelf(SPELL_GROW);  // 施放生长法术，增加一层

            // 检查生长光环是否达到目标层数
            // 注意：使用堆叠数而非重复计数器，因为第一次堆叠在任务第一次执行时就已经应用
            Aura* growAura = me->GetAura(SPELL_GROW);
            if (growAura && growAura->GetStackAmount() != _counter)
            {
                // 未达到目标层数，继续生长
                task.Repeat(2s);
            }
            else
            {
                // 已达到目标层数，进入成熟阶段
                task.Schedule(1s, [this](TaskContext task)
                {
                    DoCastSelf(SPELL_SPORE_CLOUD);  // 施放孢子云，开始造成伤害

                    // 调度消失任务：4秒后清除光环并消失
                    task.Schedule(4s, [this](TaskContext /*task*/)
                    {
                        me->RemoveAurasDueToSpell(SPELL_GROW);  // 移除生长光环
                        me->DespawnOrUnsummon(4s);              // 4秒后消失
                    });
                });
            }
        });
    }

    /**
     * @brief AI更新函数
     * @param diff 自上次更新以来经过的时间（毫秒）
     *
     * 每个服务器tick调用一次，更新任务调度器。
     * 蘑菇不需要攻击逻辑，只需要处理定时任务。
     *
     * 调用时机：每个服务器帧
     */
    void UpdateAI(uint32 diff) override
    {
        _scheduler.Update(diff);
    }

private:
    TaskScheduler _scheduler;  ///< 任务调度器，用于管理生长和消失任务
    uint32 _counter;           ///< 生长目标层数（8-10层），决定蘑菇最终大小
};

/**
 * @brief 注册脚本函数
 *
 * 这是脚本的入口点函数，用于将霍加尔芬和幽暗蘑菇的AI注册到脚本系统中。
 * 当服务器启动时，脚本系统会调用此函数来注册所有相关AI。
 *
 * 注册内容：
 * - boss_hungarfen: 霍加尔芬首领AI
 * - npc_underbog_mushroom: 幽暗蘑菇AI
 *
 * 调用时机：服务器启动时，在脚本初始化阶段
 */
void AddSC_boss_hungarfen()
{
    RegisterTheUnderbogCreatureAI(boss_hungarfen);
    RegisterTheUnderbogCreatureAI(npc_underbog_mushroom);
}
