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
 * @file boss_azgalor.cpp
 * @brief 海加尔山副本 - 阿兹加洛Boss战脚本模块
 *
 * 本模块实现了阿兹加洛（Azgalor）Boss的战斗逻辑，包括：
 * - Boss AI行为和技能施放
 * - 召唤物"小型末日守卫"的AI控制
 * - Boss的移动路径和仇恨管理
 * - 狂暴机制
 *
 * 阿兹加洛是海加尔山战役中的第四个Boss，擅长使用火焰雨、末日、咆哮等技能。
 */

#include "ScriptMgr.h"
#include "hyjal.h"
#include "hyjal_trash.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"

/**
 * @brief 阿兹加洛Boss技能枚举定义
 */
enum Spells
{
    SPELL_RAIN_OF_FIRE          = 31340,    // 火焰雨：对随机目标位置施放的AOE火焰伤害
    SPELL_DOOM                  = 31347,    // 末日：对随机非坦克目标施放，死亡后召唤末日守卫
    SPELL_HOWL_OF_AZGALOR       = 31344,    // 阿兹加洛咆哮：沉默附近所有玩家
    SPELL_CLEAVE                = 31345,    // 顺劈斩：对当前目标及其附近敌人造成伤害
    SPELL_BERSERK               = 26662,    // 狂暴：增加攻击速度和伤害

    // 小型末日守卫技能
    SPELL_THRASH                = 12787,    // 痛击：额外攻击两次
    SPELL_CRIPPLE               = 31406,    // 致残：降低目标移动和攻击速度
    SPELL_WARSTOMP              = 31408,    // 战争践踏：AOE击晕效果
};

/**
 * @brief Boss对话文本ID枚举
 */
enum Texts
{
    SAY_ONDEATH             = 0,    // 死亡时台词
    SAY_ONSLAY              = 1,    // 击杀玩家时台词
    SAY_DOOM                = 2,    // 施放末日时台词（未使用？）
    SAY_ONAGGRO             = 3,    // 进入战斗时台词
};

/**
 * @brief 阿兹加洛Boss脚本类
 *
 * 负责注册和管理阿兹加洛Boss的AI实例
 */
class boss_azgalor : public CreatureScript
{
public:
    /**
     * @brief 构造函数，注册Boss脚本名称
     */
    boss_azgalor() : CreatureScript("boss_azgalor") { }

    /**
     * @brief 获取Boss AI实例
     * @param creature 生物对象指针
     * @return 返回Boss AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetHyjalAI<boss_azgalorAI>(creature);
    }

    /**
     * @brief 阿兹加洛Boss AI结构体
     *
     * 继承自hyjal_trashAI，实现了阿兹加洛的战斗逻辑：
     * - 技能计时器管理
     * - 路径导航到萨尔处
     * - 战斗状态同步到实例脚本
     * - 狂暴机制（10分钟后触发）
     */
    struct boss_azgalorAI : public hyjal_trashAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化Boss的基本属性和实例数据
         */
        boss_azgalorAI(Creature* creature) : hyjal_trashAI(creature)
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
            RainTimer = 20000;      // 火焰雨初始计时器20秒
            DoomTimer = 50000;      // 末日初始计时器50秒
            HowlTimer = 30000;      // 咆哮初始计时器30秒
            CleaveTimer = 10000;    // 顺劈斩初始计时器10秒
            EnrageTimer = 600000;   // 狂暴计时器10分钟
            enraged = false;        // 狂暴状态标志
        }

        uint32 RainTimer;       // 火焰雨技能冷却计时器
        uint32 DoomTimer;       // 末日技能冷却计时器
        uint32 HowlTimer;       // 咆哮技能冷却计时器
        uint32 CleaveTimer;     // 顺劈斩技能冷却计时器
        uint32 EnrageTimer;     // 狂暴计时器
        bool enraged;           // 是否已狂暴

        bool go;                // 路径点是否已初始化标志

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
                instance->SetBossState(DATA_AZGALOR, NOT_STARTED);
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
                instance->SetBossState(DATA_AZGALOR, IN_PROGRESS);

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
         * - 路径点7：到达营地，将萨尔设为仇恨目标
         */
        void WaypointReached(uint32 waypointId, uint32 /*pathId*/) override
        {
            if (waypointId == 7 && instance)
            {
                // 获取萨尔并添加仇恨，使Boss向她移动
                Creature* target = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_THRALL));
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
                instance->SetBossState(DATA_AZGALOR, DONE);
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
         * 4. 检查狂暴条件
         * 5. 执行近战攻击
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
                    // 设置从部落营地入口到萨尔位置的路径点
                    AddWaypoint(0, 5492.91f,    -2404.61f,    1462.63f);
                    AddWaypoint(1, 5531.76f,    -2460.87f,    1469.55f);
                    AddWaypoint(2, 5554.58f,    -2514.66f,    1476.12f);
                    AddWaypoint(3, 5554.16f,    -2567.23f,    1479.90f);
                    AddWaypoint(4, 5540.67f,    -2625.99f,    1480.89f);
                    AddWaypoint(5, 5508.16f,    -2659.2f,    1480.15f);
                    AddWaypoint(6, 5489.62f,    -2704.05f,    1482.18f);
                    AddWaypoint(7, 5457.04f,    -2726.26f,    1485.10f);
                    Start(false, true);
                    SetDespawnAtEnd(false);
                }
            }

            // 如果没有目标则返回
            if (!UpdateVictim())
                return;

            // 火焰雨：对随机目标位置施放
            if (RainTimer <= diff)
            {
                DoCast(SelectTarget(SelectTargetMethod::Random, 0, 30, true), SPELL_RAIN_OF_FIRE);
                RainTimer = 20000 + rand32() % 15000;  // 20-35秒冷却
            } else RainTimer -= diff;

            // 末日：对随机非坦克目标施放（选择第2个目标，避免选中坦克）
            if (DoomTimer <= diff)
            {
                DoCast(SelectTarget(SelectTargetMethod::Random, 1, 100, true), SPELL_DOOM);  // 永远不施放在坦克身上
                DoomTimer = 45000 + rand32() % 5000;  // 45-50秒冷却
            } else DoomTimer -= diff;

            // 咆哮：AOE沉默效果
            if (HowlTimer <= diff)
            {
                DoCast(me, SPELL_HOWL_OF_AZGALOR);
                HowlTimer = 30000;  // 30秒冷却
            } else HowlTimer -= diff;

            // 顺劈斩：对当前目标施放
            if (CleaveTimer <= diff)
            {
                DoCastVictim(SPELL_CLEAVE);
                CleaveTimer = 10000 + rand32() % 5000;  // 10-15秒冷却
            } else CleaveTimer -= diff;

            // 狂暴：10分钟后触发
            if (EnrageTimer < diff && !enraged)
            {
                me->InterruptNonMeleeSpells(false);
                DoCast(me, SPELL_BERSERK, true);
                enraged = true;
                EnrageTimer = 600000;  // 狂暴后重置计时器（实际上不会再次触发）
            } else EnrageTimer -= diff;

            // 执行近战攻击
            DoMeleeAttackIfReady();
        }
    };

};

/**
 * @brief 小型末日守卫NPC脚本类
 *
 * 负责管理阿兹加洛末日技能召唤的末日守卫AI行为
 * 末日守卫会在目标死亡后出现，并在Boss死亡后自动消失
 */
class npc_lesser_doomguard : public CreatureScript
{
public:
    /**
     * @brief 构造函数，注册NPC脚本名称
     */
    npc_lesser_doomguard() : CreatureScript("npc_lesser_doomguard") { }

    /**
     * @brief 获取NPC AI实例
     * @param creature 生物对象指针
     * @return 返回NPC AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetHyjalAI<npc_lesser_doomguardAI>(creature);
    }

    /**
     * @brief 小型末日守卫AI结构体
     *
     * 实现了末日守卫的行为逻辑：
     * - 定期施放战争践踏和致残技能
     * - 检查Boss是否存活，Boss死亡时自动消失
     * - 主动攻击50码范围内的敌对目标
     */
    struct npc_lesser_doomguardAI : public hyjal_trashAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化计时器和Boss GUID
         */
        npc_lesser_doomguardAI(Creature* creature) : hyjal_trashAI(creature)
        {
            CrippleTimer = 50000;       // 致残技能初始计时器50秒
            WarstompTimer = 10000;      // 战争践踏初始计时器10秒
            CheckTimer = 5000;          // Boss存活检查计时器5秒
            AzgalorGUID = instance->GetGuidData(DATA_AZGALOR);
        }

        uint32 CrippleTimer;            // 致残技能冷却计时器
        uint32 WarstompTimer;           // 战争践踏冷却计时器
        uint32 CheckTimer;              // Boss存活检查计时器
        ObjectGuid AzgalorGUID;         // 阿兹加洛的GUID

        /**
         * @brief 重置NPC状态
         *
         * 重置时：
         * - 施放痛击被动技能
         * - 重置计时器
         */
        void Reset() override
        {
            CrippleTimer = 50000;
            WarstompTimer = 10000;
            DoCast(me, SPELL_THRASH);   // 施放痛击被动效果
            CheckTimer = 5000;
        }

        /**
         * @brief 进入战斗回调（空实现）
         * @param who 攻击目标（未使用）
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
        }

        /**
         * @brief 击杀单位回调（空实现）
         * @param victim 被击杀单位（未使用）
         */
        void KilledUnit(Unit* /*victim*/) override
        {
        }

        /**
         * @brief 视线移动回调
         * @param who 进入视线的单位
         *
         * 当单位进入视线时：
         * - 检查距离是否在50码内
         * - 如果不在战斗中且目标是有效攻击目标，则发起攻击
         */
        void MoveInLineOfSight(Unit* who) override

        {
            if (me->IsWithinDist(who, 50) && !me->IsInCombat() && me->IsValidAttackTarget(who))
                AttackStart(who);
        }

        /**
         * @brief 死亡回调（空实现）
         * @param killer 击杀者（未使用）
         */
        void JustDied(Unit* /*killer*/) override
        {
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 主要更新逻辑：
         * 1. 定期检查Boss是否存活，若Boss死亡则消失
         * 2. 定期施放战争践踏和致残技能
         * 3. 执行近战攻击
         *
         * 性能注意事项：
         * - 每5秒检查一次Boss状态，避免频繁查询
         */
        void UpdateAI(uint32 diff) override
        {
            // 检查Boss是否存活
            if (CheckTimer <= diff)
            {
                if (AzgalorGUID)
                {
                    Creature* boss = ObjectAccessor::GetCreature(*me, AzgalorGUID);
                    if (!boss || boss->isDead())
                    {
                        me->DespawnOrUnsummon();  // Boss死亡时末日守卫消失
                        return;
                    }
                }
                CheckTimer = 5000;  // 每5秒检查一次
            } else CheckTimer -= diff;

            // 如果没有目标则返回
            if (!UpdateVictim())
                return;

            // 战争践踏：AOE击晕
            if (WarstompTimer <= diff)
            {
                DoCast(me, SPELL_WARSTOMP);
                WarstompTimer = 10000 + rand32() % 5000;  // 10-15秒冷却
            } else WarstompTimer -= diff;

            // 致残：对随机目标施放
            if (CrippleTimer <= diff)
            {
                DoCast(SelectTarget(SelectTargetMethod::Random, 0, 100, true), SPELL_CRIPPLE);
                CrippleTimer = 25000 + rand32() % 5000;  // 25-30秒冷却
            } else CrippleTimer -= diff;

            // 执行近战攻击
            DoMeleeAttackIfReady();
        }
    };

};

/**
 * @brief 注册阿兹加洛Boss相关脚本
 *
 * 此函数在世界服务器启动时被调用，注册：
 * - Boss AI脚本
 * - 召唤物NPC脚本
 */
void AddSC_boss_azgalor()
{
    new boss_azgalor();
    new npc_lesser_doomguard();
}
