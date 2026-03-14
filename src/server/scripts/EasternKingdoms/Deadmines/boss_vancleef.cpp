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
 * @file    boss_vancleef.cpp
 * @brief   死亡矿坑副本Boss - 艾德温·范克里夫(Edwin VanCleef)的AI实现
 *
 * @details 该模块实现了死亡矿坑副本的最终Boss艾德温·范克里夫的战斗逻辑。
 *          该Boss具有以下战斗特点：
 *          - 双持武器和痛击技能
 *          - 在血量降至50%时召唤黑铁卫士增援
 *          - 在不同血量阈值触发特定对话
 *          - 拥有多个黑铁卫士作为护卫
 *
 * @note    艾德温·范克里夫是迪菲亚兄弟会的领袖，死亡矿坑副本的最终Boss。
 */

#include "ScriptMgr.h"
#include "deadmines.h"
#include "ScriptedCreature.h"

/**
 * @brief 范克里夫Boss相关数据和法术枚举定义
 */
enum VanCleefData
{
    SPELL_DUAL_WIELD = 674,      ///< 双持技能 - 允许副手武器攻击
    SPELL_THRASH = 12787,        ///< 痛击技能 - 额外的近战攻击
    SPELL_VANCLEEFS_ALLIES = 5200 ///< 范克里夫的盟友 - 召唤黑铁卫士的法术
};

/**
 * @brief 对话文本ID枚举定义
 */
enum Speech
{
    SAY_AGGRO  = 0,    ///< 开战对话 - "None may challenge the Brotherhood!"（没人能挑战兄弟会！）
    SAY_ONE    = 1,    ///< 血量66%对话 - "Lapdogs, all of you!"（你们都是走狗！）
    SAY_SUMMON = 2,    ///< 召唤增援对话 - "calls more of his allies out of the shadows."（从阴影中召唤更多盟友）
    SAY_TWO    = 3,    ///< 血量33%对话 - "Fools! Our cause is righteous!"（愚蠢！我们的事业是正义的！）
    SAY_KILL   = 4,    ///< 击杀玩家对话 - "And stay down!"（倒下吧！）
    SAY_THREE  = 5     ///< 血量25%对话 - "The Brotherhood shall prevail!"（兄弟会必将胜利！）
};

/**
 * @brief 黑铁卫士的生成位置坐标
 *
 * @details 定义了两个黑铁卫士在Boss房间中的初始位置。
 *          这些卫士在Boss重置时被召唤，并在Boss进入战斗后参与战斗。
 */
Position const BlackguardPositions[] =
{
    { -78.2791f, -824.784f, 40.0007f, 2.93215f },  ///< 第一个黑铁卫士位置
    { -77.8071f, -815.097f, 40.0188f, 3.26377f }   ///< 第二个黑铁卫士位置
};

/**
 * @struct boss_vancleef
 * @brief 艾德温·范克里夫Boss AI实现
 *
 * @details 该结构体实现了范克里夫Boss的战斗逻辑，包括：
 *          - 双持武器和痛击被动技能
 *          - 在血量降至50%时召唤黑铁卫士增援
 *          - 在66%、33%、25%血量时触发对话
 *          - 管理黑铁卫士护卫的生成和战斗
 */
struct boss_vancleef : public BossAI
{
    public:
        /**
         * @brief 构造函数，初始化Boss AI和状态标志
         *
         * @param creature 生物对象指针
         *
         * @details 初始化BossAI基类，设置Boss ID为BOSS_VANCLEEF。
         *          初始化血量检查标志和增援召唤标志为false。
         */
        boss_vancleef(Creature* creature) : BossAI(creature, BOSS_VANCLEEF), _guardsCalled(false), _health25(false), _health33(false), _health66(false) { }

        /**
         * @brief 重置Boss状态
         *
         * @details 在Boss重置或脱离战斗时调用。
         *          重置所有血量检查标志和增援召唤标志。
         *          施放双持和痛击技能（被动）。
         *          召唤黑铁卫士护卫。
         *
         * @note 性能注意事项：召唤护卫会创建新的生物实体，应确保在重置时清理旧实体。
         */
        void Reset() override
        {
            BossAI::Reset();

            // 重置所有状态标志
            _guardsCalled = false;
            _health25 = false;
            _health33 = false;
            _health66 = false;

            // 施放被动技能（双持和痛击）
            DoCastSelf(SPELL_DUAL_WIELD, true);
            DoCastSelf(SPELL_THRASH, true);

            // 召唤黑铁卫士护卫
            SummonBlackguards();
        }

        /**
         * @brief 进入战斗回调
         *
         * @param victim 仇恨目标
         *
         * @details 当Boss进入战斗时触发。
         *          调用基类的进入战斗逻辑。
         *          让所有召唤的黑铁卫士进入战斗状态。
         *          播放开战对话。
         */
        void JustEngagedWith(Unit* victim) override
        {
            BossAI::JustEngagedWith(victim);
            summons.DoZoneInCombat();  // 让所有召唤的生物进入战斗

            Talk(SAY_AGGRO);
        }

        /**
         * @brief 击杀单位回调
         *
         * @param victim 被击杀的单位
         *
         * @details 当Boss击杀一个单位时触发。
         *          如果被击杀的是玩家，播放击杀对话。
         */
        void KilledUnit(Unit* victim) override
        {
            if (victim->GetTypeId() == TYPEID_PLAYER)
                Talk(SAY_KILL);
        }

        /**
         * @brief 进入躲避模式回调
         *
         * @param why 躲避原因
         *
         * @details 当Boss脱离战斗（躲避）时触发。
         *          消失所有召唤的生物。
         *          调用基类的躲避处理逻辑。
         */
        void EnterEvadeMode(EvadeReason /*why*/) override
        {
            summons.DespawnAll();  // 消失所有召唤物
            _DespawnAtEvade();     // 执行躲避逻辑
        }

        /**
         * @brief 召唤黑铁卫士护卫
         *
         * @details 在Boss重置时调用，召唤两个黑铁卫士作为护卫。
         *          黑铁卫士会在Boss进入战斗后参与战斗。
         *          召唤的生物会在死亡后保留尸体1分钟。
         */
        void SummonBlackguards()
        {
            for (Position BlackguardPosition : BlackguardPositions)
                DoSummon(NPC_BLACKGUARD, BlackguardPosition, 1min, TEMPSUMMON_CORPSE_TIMED_DESPAWN);
        }

        /**
         * @brief 受到伤害回调
         *
         * @param attacker 攻击者（未使用）
         * @param damage 伤害值（未使用）
         * @param damageType 伤害类型（未使用）
         * @param spellInfo 法术信息（未使用）
         *
         * @details 在Boss受到伤害时检查血量并触发相应事件：
         *          - 50%血量：召唤增援并播放对话
         *          - 66%血量：播放对话
         *          - 33%血量：播放对话
         *          - 25%血量：播放对话
         *
         * @note 血量检查顺序很重要，必须从低血量开始检查，避免重复触发。
         */
        void DamageTaken(Unit* /*attacker*/, uint32& /*damage*/, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            // 检查是否需要召唤增援（50%血量，仅触发一次）
            if (!_guardsCalled && HealthBelowPct(50))
            {
                Talk(SAY_SUMMON);
                DoCastSelf(SPELL_VANCLEEFS_ALLIES);  // 施放召唤增援法术
                _guardsCalled = true;
            }

            // 从低血量开始检查，确保对话按正确顺序播放
            if (!_health25 && HealthBelowPct(25))
            {
                Talk(SAY_THREE);
                _health25 = true;
            }
            else if (!_health33 && HealthBelowPct(33))
            {
                Talk(SAY_TWO);
                _health33 = true;
            }
            else if (!_health66 && HealthBelowPct(66))
            {
                Talk(SAY_ONE);
                _health66 = true;
            }
        }

    private:
        bool _guardsCalled;  ///< 是否已召唤增援标志（50%血量触发）
        bool _health25;      ///< 是否已触发25%血量对话标志
        bool _health33;      ///< 是否已触发33%血量对话标志
        bool _health66;      ///< 是否已触发66%血量对话标志
};

/**
 * @brief 注册艾德温·范克里夫Boss脚本
 *
 * @details 该函数在服务器启动时被调用，用于注册Boss脚本实例。
 *          这是TrinityCore脚本系统的标准入口点。
 */
void AddSC_boss_vancleef()
{
    RegisterDeadminesCreatureAI(boss_vancleef);
}
