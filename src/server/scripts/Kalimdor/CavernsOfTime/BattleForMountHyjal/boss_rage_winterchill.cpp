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
 * @file boss_rage_winterchill.cpp
 * @brief 海加尔山副本 - 雷基·冬寒Boss战脚本模块
 *
 * 本模块实现了雷基·冬寒（Rage Winterchill）Boss的战斗逻辑，包括：
 * - Boss AI行为和技能施放
 * - Boss的移动路径和仇恨管理
 * - 冰霜技能组合使用
 *
 * 雷基·冬寒是海加尔山战役中的第一个Boss，擅长使用冰霜魔法，
 * 包括冰霜护甲、死亡凋零、冰霜新星和寒冰箭等技能。
 */

#include "ScriptMgr.h"
#include "hyjal.h"
#include "hyjal_trash.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"

/**
 * @brief 雷基·冬寒Boss技能枚举定义
 */
enum Spells
{
    SPELL_FROST_ARMOR           = 31256,    // 冰霜护甲：增益自身，降低攻击者的移动速度
    SPELL_DEATH_AND_DECAY       = 31258,    // 死亡凋零：AOE伤害技能，持续造成伤害
    SPELL_FROST_NOVA            = 31250,    // 冰霜新星：AOE冰冻效果，冻结周围敌人
    SPELL_ICEBOLT               = 31249     // 寒冰箭：对随机目标施放的冰霜伤害法术
};

/**
 * @brief Boss对话文本ID枚举
 */
enum Texts
{
    SAY_ONDEATH                 = 0,    // 死亡时台词
    SAY_ONSLAY                  = 1,    // 击杀玩家时台词
    SAY_DECAY                   = 2,    // 施放死亡凋零时台词
    SAY_NOVA                    = 3,    // 施放冰霜新星时台词
    SAY_ONAGGRO                 = 4     // 进入战斗时台词
};

/**
 * @brief 雷基·冬寒Boss脚本类
 *
 * 负责注册和管理雷基·冬寒Boss的AI实例
 */
class boss_rage_winterchill : public CreatureScript
{
public:
    /**
     * @brief 构造函数，注册Boss脚本名称
     */
    boss_rage_winterchill() : CreatureScript("boss_rage_winterchill") { }

    /**
     * @brief 获取Boss AI实例
     * @param creature 生物对象指针
     * @return 返回Boss AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetHyjalAI<boss_rage_winterchillAI>(creature);
    }

    /**
     * @brief 雷基·冬寒Boss AI结构体
     *
     * 继承自hyjal_trashAI，实现了雷基·冬寒的战斗逻辑：
     * - 技能计时器管理
     * - 路径导航到吉安娜处
     * - 战斗状态同步到实例脚本
     * - 冰霜魔法技能组合
     */
    struct boss_rage_winterchillAI : public hyjal_trashAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化Boss的基本属性和实例数据
         */
        boss_rage_winterchillAI(Creature* creature) : hyjal_trashAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
            go = false;
        }

        /**
         * @brief 初始化技能计时器和状态变量
         *
         * 在Reset()时调用，重置所有技能计时器到初始值
         */
        void Initialize()
        {
            damageTaken = 0;
            FrostArmorTimer = 37000;    // 冰霜护甲初始计时器37秒
            DecayTimer = 45000;         // 死亡凋零初始计时器45秒
            NovaTimer = 15000;          // 冰霜新星初始计时器15秒
            IceboltTimer = 10000;       // 寒冰箭初始计时器10秒
        }

        uint32 FrostArmorTimer;     // 冰霜护甲技能冷却计时器
        uint32 DecayTimer;          // 死亡凋零技能冷却计时器
        uint32 NovaTimer;           // 冰霜新星技能冷却计时器
        uint32 IceboltTimer;        // 寒冰箭技能冷却计时器
        bool go;                    // 路径点是否已初始化标志

        /**
         * @brief 重置Boss状态
         *
         * 当Boss脱离战斗或重置时调用：
         * - 重置所有技能计时器
         * - 如果是事件模式，设置Boss状态为NOT_STARTED
         */
        void Reset() override
        {
            Initialize();

            if (IsEvent)
                instance->SetBossState(DATA_RAGEWINTERCHILL, NOT_STARTED);
        }

        /**
         * @brief 进入战斗回调
         * @param who 攻击目标（未使用）
         *
         * 当Boss进入战斗时调用：
         * - 设置实例Boss状态为IN_PROGRESS
         * - 播放进入战斗台词
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            if (IsEvent)
                instance->SetBossState(DATA_RAGEWINTERCHILL, IN_PROGRESS);
            Talk(SAY_ONAGGRO);
        }

        /**
         * @brief 击杀单位回调
         * @param victim 被击杀的单位（未使用）
         *
         * 当Boss击杀玩家时播放击杀台词
         */
        void KilledUnit(Unit* /*victim*/) override
        {
            Talk(SAY_ONSLAY);
        }

        /**
         * @brief 到达路径点回调
         * @param waypointId 路径点ID
         * @param pathId 路径ID（未使用）
         *
         * 当Boss到达指定路径点时触发：
         * - 路径点7：到达营地，将吉安娜设为仇恨目标
         */
        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            if (waypointId == 7 && instance)
            {
                // 获取吉安娜并添加仇恨，使Boss向她移动
                Creature* target = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_JAINAPROUDMOORE));
                if (target && target->IsAlive())
                    AddThreat(target, 0.0f);
            }
        }

        /**
         * @brief Boss死亡回调
         * @param killer 击杀者
         *
         * 当Boss死亡时调用：
         * - 调用父类死亡处理
         * - 设置实例Boss状态为DONE
         * - 播放死亡台词
         */
        void JustDied(Unit* killer) override
        {
            hyjal_trashAI::JustDied(killer);
            if (IsEvent)
                instance->SetBossState(DATA_RAGEWINTERCHILL, DONE);
            Talk(SAY_ONDEATH);
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 主要AI更新循环：
         * 1. 如果是事件模式，更新护送AI并设置移动路径
         * 2. 更新所有技能计时器
         * 3. 根据计时器施放技能
         * 4. 执行近战攻击
         *
         * 性能注意事项：
         * - 每帧都会调用此函数
         * - 技能选择使用随机目标，需考虑性能影响
         */
        void UpdateAI(uint32 diff) override
        {
            if (IsEvent)
            {
                // 必须更新护送AI以处理路径移动
                EscortAI::UpdateAI(diff);
                if (!go)
                {
                    go = true;
                    // 设置从联盟营地入口到吉安娜位置的路径点
                    AddWaypoint(0, 4896.08f,    -1576.35f,    1333.65f);
                    AddWaypoint(1, 4898.68f,    -1615.02f,    1329.48f);
                    AddWaypoint(2, 4907.12f,    -1667.08f,    1321.00f);
                    AddWaypoint(3, 4963.18f,    -1699.35f,    1340.51f);
                    AddWaypoint(4, 4989.16f,    -1716.67f,    1335.74f);
                    AddWaypoint(5, 5026.27f,    -1736.89f,    1323.02f);
                    AddWaypoint(6, 5037.77f,    -1770.56f,    1324.36f);
                    AddWaypoint(7, 5067.23f,    -1789.95f,    1321.17f);
                    Start(false, true);
                    SetDespawnAtEnd(false);
                }
            }

            // 如果没有目标则返回
            if (!UpdateVictim())
                return;

            // 冰霜护甲：增益自身
            if (FrostArmorTimer <= diff)
            {
                DoCast(me, SPELL_FROST_ARMOR);
                FrostArmorTimer = 40000 + rand32() % 20000;  // 40-60秒冷却
            } else FrostArmorTimer -= diff;

            // 死亡凋零：AOE持续伤害
            if (DecayTimer <= diff)
            {
                DoCastVictim(SPELL_DEATH_AND_DECAY);
                DecayTimer = 60000 + rand32() % 20000;  // 60-80秒冷却
                Talk(SAY_DECAY);
            } else DecayTimer -= diff;

            // 冰霜新星：AOE冰冻
            if (NovaTimer <= diff)
            {
                DoCastVictim(SPELL_FROST_NOVA);
                NovaTimer = 30000 + rand32() % 15000;  // 30-45秒冷却
                Talk(SAY_NOVA);
            } else NovaTimer -= diff;

            // 寒冰箭：对随机目标施放
            if (IceboltTimer <= diff)
            {
                DoCast(SelectTarget(SelectTargetMethod::Random, 0, 40, true), SPELL_ICEBOLT);
                IceboltTimer = 11000 + rand32() % 20000;  // 11-31秒冷却
            } else IceboltTimer -= diff;

            // 执行近战攻击
            DoMeleeAttackIfReady();
        }
    };
};

/**
 * @brief 注册雷基·冬寒Boss相关脚本
 *
 * 此函数在世界服务器启动时被调用，注册Boss AI脚本
 */
void AddSC_boss_rage_winterchill()
{
    new boss_rage_winterchill();
}
