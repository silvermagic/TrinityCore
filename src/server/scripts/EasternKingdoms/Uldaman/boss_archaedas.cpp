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
 * @file boss_archaedas.cpp
 * @brief Uldaman 副本首领阿扎达斯 (Archaedas) 的脚本实现
 *
 * 本模块实现了阿扎达斯首领战的完整逻辑，包括：
 * - 阿扎达斯的激活机制（通过祭坛触发）
 * - 分阶段唤醒小怪系统（墙边小怪、土灵守护者、宝库行者）
 * - 生命值阈值触发的增援召唤
 * - 死亡后开启宝库大门
 *
 * 战斗机制：
 * - 初始状态为冻结状态，需要玩家激活祭坛
 * - 每10秒唤醒一个墙边小怪
 * - 生命值低于66%时唤醒6个土灵守护者
 * - 生命值低于33%时唤醒4个宝库行者
 * - 死亡时开启古代宝库大门
 */

/* ScriptData
SDName: boss_archaedas
SD%Complete: 100
SDComment: Archaedas is activated when 1 person (was 3, changed in 3.0.8) clicks on his altar.
Every 10 seconds he will awaken one of his minions along the wall.
At 66%, he will awaken the 6 Guardians.
At 33%, he will awaken the Vault Walkers
On his death the vault door opens.
EndScriptData */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "uldaman.h"

/**
 * @brief 阿扎达斯的对话文本枚举
 */
enum Says
{
    SAY_AGGRO                   = 0,  ///< 激活时的喊话
    SAY_SUMMON_GUARDIANS        = 1,  ///< 召唤土灵守护者时的喊话
    SAY_SUMMON_VAULT_WALKERS    = 2,  ///< 召唤宝库行者时的喊话
    SAY_KILL                    = 3   ///< 击杀玩家时的喊话
};

/**
 * @brief 阿扎达斯战斗使用的法术枚举
 */
enum Spells
{
    SPELL_GROUND_TREMOR                = 6524,   ///< 地震术 - 对周围敌人造成伤害并使其眩晕
    SPELL_ARCHAEDAS_AWAKEN             = 10347,  ///< 阿扎达斯觉醒 - 激活首领/小怪的视觉法术
    SPELL_BOSS_OBJECT_VISUAL           = 11206,  ///< 首领物体视觉效果 - 祭坛激活特效
    SPELL_BOSS_AGGRO                   = 10340,  ///< 首领仇恨 - 建立初始仇恨
    SPELL_SUB_BOSS_AGGRO               = 11568,  ///< 副首领仇恨 - 小怪仇恨链接
    SPELL_AWAKEN_VAULT_WALKER          = 10258,  ///< 觉醒宝库行者 - 激活宝库行者小怪
    SPELL_AWAKEN_EARTHEN_GUARDIAN      = 10252,  ///< 觉醒土灵守护者 - 激活土灵守护者小怪
    SPELL_SELF_DESTRUCT                = 9874,   ///< 自毁 - 石像守卫死亡时的爆炸法术
    SPELL_FREEZE_ANIM                  = 16245,  ///< 冻结动画 - 阿扎达斯的冻结视觉效果
    SPELL_MINION_FREEZE_ANIM           = 10255   ///< 小怪冻结动画 - 小怪的冻结视觉效果
};

/**
 * @brief 阿扎达斯首领脚本类
 *
 * 实现阿扎达斯的完整战斗AI，包括觉醒序列、阶段性召唤小怪、
 * 以及与实例脚本的数据交互。
 */
class boss_archaedas : public CreatureScript
{
    public:

        boss_archaedas()
            : CreatureScript("boss_archaedas")
        {
        }

        /**
         * @brief 阿扎达斯AI结构体
         *
         * 负责管理首领的战斗逻辑、计时器和状态标志。
         */
        struct boss_archaedasAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_archaedasAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
                instance = me->GetInstanceScript();
            }

            /**
             * @brief 初始化所有计时器和状态标志
             *
             * 在构造函数和Reset()中被调用，确保状态一致。
             */
            void Initialize()
            {
                uiTremorTimer = 60000;     ///< 地震术计时器，初始60秒（首次使用后会改为45秒）
                iAwakenTimer = 0;           ///< 觉醒动画剩余时间
                uiWallMinionTimer = 10000;  ///< 墙边小怪唤醒计时器，每10秒唤醒一个

                bWakingUp = false;          ///< 是否正在执行觉醒动画
                bGuardiansAwake = false;    ///< 土灵守护者是否已被唤醒
                bVaultWalkersAwake = false; ///< 宝库行者是否已被唤醒
            }

            uint32 uiTremorTimer;       ///< 地震术冷却计时器
            int32  iAwakenTimer;        ///< 觉醒动画剩余时间（毫秒）
            uint32 uiWallMinionTimer;   ///< 墙边小怪唤醒计时器
            bool bWakingUp;             ///< 正在执行觉醒动画标志

            bool bGuardiansAwake;       ///< 土灵守护者已唤醒标志（66%血量触发）
            bool bVaultWalkersAwake;    ///< 宝库行者已唤醒标志（33%血量触发）
            InstanceScript* instance;   ///< 实例脚本指针，用于与副本交互

            /**
             * @brief 重置首领状态
             *
             * 在战斗结束或脱战后调用，恢复首领到初始冻结状态。
             * 同时通知实例脚本重生死亡的小怪。
             */
            void Reset() override
            {
                Initialize();

                instance->SetData(0, 5);    // 通知实例重生任何死亡的小怪
                me->SetFaction(FACTION_FRIENDLY);              // 设置为友好阵营（不可攻击）
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);     // 设置为不可交互
                me->SetControlled(true, UNIT_STATE_ROOT);      // 定身在原地
                me->AddAura(SPELL_FREEZE_ANIM, me);            // 添加冻结视觉效果
            }

            /**
             * @brief 激活指定的小怪
             * @param uiGuid 小怪的GUID
             * @param flag 是否施放觉醒法术标志（最后一个召唤不需要额外法术）
             *
             * 该方法执行以下操作：
             * 1. 施放觉醒法术（视觉效果）
             * 2. 移除冻结状态和不可交互标志
             * 3. 解除定身
             * 4. 设置为敌对阵营
             * 5. 移除冻结动画效果
             */
            void ActivateMinion(ObjectGuid uiGuid, bool flag)
            {
                Unit* minion = ObjectAccessor::GetUnit(*me, uiGuid);

                if (minion && minion->IsAlive())
                {
                    DoCast(minion, SPELL_AWAKEN_VAULT_WALKER, flag);
                    minion->CastSpell(minion, SPELL_ARCHAEDAS_AWAKEN, true);
                    minion->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                    minion->SetControlled(false, UNIT_STATE_ROOT);
                    minion->SetFaction(FACTION_MONSTER);
                    minion->RemoveAura(SPELL_MINION_FREEZE_ANIM);
                }
            }

            /**
             * @brief 进入战斗时的处理
             * @param who 进入战斗的目标（未使用）
             *
             * 当首领进入战斗状态时，设置阵营为怪物阵营，
             * 移除不可交互标志和定身状态。
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                me->SetFaction(FACTION_MONSTER);
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->SetControlled(false, UNIT_STATE_ROOT);
            }

            /**
             * @brief 法术命中时的处理
             * @param caster 施法者（未使用）
             * @param spellInfo 命中的法术信息
             *
             * 当阿扎达斯觉醒法术命中时，开始觉醒序列。
             * 首领会喊话并开始4秒的觉醒动画。
             */
            void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
            {
                // 被祭坛唤醒，开始觉醒序列
                if (spellInfo->Id == SPELL_ARCHAEDAS_AWAKEN)
                {
                    Talk(SAY_AGGRO);
                    iAwakenTimer = 4000;    // 4秒觉醒动画
                    bWakingUp = true;
                }
            }

            /**
             * @brief 击杀玩家时的处理
             * @param victim 被击杀的目标（未使用）
             */
            void KilledUnit(Unit* /*victim*/) override
            {
                Talk(SAY_KILL);
            }

            /**
             * @brief 更新AI逻辑
             * @param uiDiff 自上次更新以来经过的时间（毫秒）
             *
             * 主要逻辑流程：
             * 1. 处理觉醒动画（如果正在进行）
             * 2. 检查是否有有效目标
             * 3. 每10秒唤醒一个墙边小怪
             * 4. 在66%血量时唤醒土灵守护者
             * 5. 在33%血量时唤醒宝库行者
             * 6. 定时施放地震术
             */
            void UpdateAI(uint32 uiDiff) override
            {
                // 正在执行觉醒动画
                if (bWakingUp && iAwakenTimer >= 0)
                {
                    iAwakenTimer -= uiDiff;
                    return;        // 动画结束前不执行其他操作
                } else if (bWakingUp && iAwakenTimer <= 0)
                {
                    bWakingUp = false;
                    // 攻击激活祭坛的玩家
                    AttackStart(ObjectAccessor::GetUnit(*me, instance->GetGuidData(0)));
                    return;     // 完成AttackStart后返回，避免继续执行
                }

                // 没有目标则返回
                if (!UpdateVictim())
                    return;

                // 唤醒墙边小怪
                if (uiWallMinionTimer <= uiDiff)
                {
                    instance->SetData(DATA_MINIONS, IN_PROGRESS);

                    uiWallMinionTimer = 10000;
                } else uiWallMinionTimer -= uiDiff;

                // 如果血量低于66%召唤土灵守护者
                if (!bGuardiansAwake && !HealthAbovePct(66))
                {
                    ActivateMinion(instance->GetGuidData(5), true);   // EarthenGuardian1
                    ActivateMinion(instance->GetGuidData(6), true);   // EarthenGuardian2
                    ActivateMinion(instance->GetGuidData(7), true);   // EarthenGuardian3
                    ActivateMinion(instance->GetGuidData(8), true);   // EarthenGuardian4
                    ActivateMinion(instance->GetGuidData(9), true);   // EarthenGuardian5
                    ActivateMinion(instance->GetGuidData(10), false); // EarthenGuardian6（最后一个不施放额外法术）
                    Talk(SAY_SUMMON_GUARDIANS);
                    bGuardiansAwake = true;
                }

                // 如果血量低于33%召唤宝库行者
                if (!bVaultWalkersAwake && !HealthAbovePct(33))
                {
                    ActivateMinion(instance->GetGuidData(1), true);    // VaultWalker1
                    ActivateMinion(instance->GetGuidData(2), true);    // VaultWalker2
                    ActivateMinion(instance->GetGuidData(3), true);    // VaultWalker3
                    ActivateMinion(instance->GetGuidData(4), false);   // VaultWalker4（最后一个不施放额外法术）
                    Talk(SAY_SUMMON_VAULT_WALKERS);
                    bVaultWalkersAwake = true;
                }

                // 地震术计时器
                if (uiTremorTimer <= uiDiff)
                {
                    // 施放地震术
                    DoCastVictim(SPELL_GROUND_TREMOR);

                    // 45秒后再次施放
                    uiTremorTimer  = 45000;
                } else uiTremorTimer  -= uiDiff;

                DoMeleeAttackIfReady();
            }

            /**
             * @brief 死亡时的处理
             * @param killer 击杀者（未使用）
             *
             * 死亡时开启古代宝库大门，并停用所有活跃的小怪。
             */
            void JustDied (Unit* /*killer*/) override
            {
                instance->SetData(DATA_ANCIENT_DOOR, DONE);      // 开启宝库大门
                instance->SetData(DATA_MINIONS, SPECIAL);        // 停用小怪
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUldamanAI<boss_archaedasAI>(creature);
        }
};

/* ScriptData
SDName: npc_archaedas_minions
SD%Complete: 100
SDComment: These mobs are initially frozen until Archaedas awakens them
one at a time.
EndScriptData */

/**
 * @brief 阿扎达斯小怪脚本类
 *
 * 这些小怪初始处于冻结状态，等待阿扎达斯逐一唤醒。
 * 包括：土灵守护者 (Earthen Guardian)、宝库行者 (Vault Walker) 等。
 */
class npc_archaedas_minions : public CreatureScript
{
    public:

        npc_archaedas_minions()
            : CreatureScript("npc_archaedas_minions")
        {
        }

        /**
         * @brief 阿扎达斯小怪AI结构体
         */
        struct npc_archaedas_minionsAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            npc_archaedas_minionsAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
                instance = me->GetInstanceScript();
            }

            /**
             * @brief 初始化计时器和状态标志
             */
            void Initialize()
            {
                uiArcing_Timer = 3000;  ///< 弧形攻击计时器（未在当前代码中使用）
                iAwakenTimer = 0;       ///< 觉醒动画剩余时间

                bWakingUp = false;      ///< 是否正在觉醒
                bAmIAwake = false;      ///< 是否已完全觉醒
            }

            uint32 uiArcing_Timer;      ///< 弧形攻击计时器
            int32 iAwakenTimer;         ///< 觉醒动画剩余时间
            bool bWakingUp;             ///< 正在觉醒标志

            bool bAmIAwake;             ///< 已觉醒标志
            InstanceScript* instance;   ///< 实例脚本指针

            /**
             * @brief 重置小怪状态
             *
             * 将小怪恢复到初始冻结状态：
             * - 设置为友好阵营
             * - 添加不可交互标志
             * - 定身在原地
             * - 移除所有光环并添加冻结动画
             */
            void Reset() override
            {
                Initialize();

                me->SetFaction(FACTION_FRIENDLY);
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->SetControlled(true, UNIT_STATE_ROOT);
                me->RemoveAllAuras();
                me->AddAura(SPELL_MINION_FREEZE_ANIM, me);
            }

            /**
             * @brief 进入战斗时的处理
             * @param who 进入战斗的目标（未使用）
             *
             * 当被唤醒进入战斗时，设置阵营为怪物阵营，
             * 移除所有视觉效果和限制。
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                me->SetFaction(FACTION_MONSTER);
                me->RemoveAllAuras();
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->SetControlled(false, UNIT_STATE_ROOT);
                bAmIAwake = true;
            }

            /**
             * @brief 法术命中时的处理
             * @param caster 施法者（未使用）
             * @param spellInfo 命中的法术信息
             *
             * 当被阿扎达斯觉醒法术命中时，开始5秒的觉醒动画。
             */
            void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
            {
                // 觉醒时间到，开始动画
                if (spellInfo->Id == SPELL_ARCHAEDAS_AWAKEN)
                {
                    iAwakenTimer = 5000;    // 5秒觉醒动画
                    bWakingUp = true;
                }
            }

            /**
             * @brief 视线检测处理
             * @param who 进入视线的单位
             *
             * 只有在完全觉醒后才会进行视线检测（避免冻结状态下触发战斗）。
             */
            void MoveInLineOfSight(Unit* who) override

            {
                if (bAmIAwake)
                    ScriptedAI::MoveInLineOfSight(who);
            }

            /**
             * @brief 更新AI逻辑
             * @param uiDiff 自上次更新以来经过的时间（毫秒）
             *
             * 处理觉醒动画和战斗逻辑：
             * 1. 等待觉醒动画完成
             * 2. 动画完成后攻击激活祭坛的玩家
             * 3. 执行近战攻击
             */
            void UpdateAI(uint32 uiDiff) override
            {
                // 正在执行觉醒动画
                if (bWakingUp && iAwakenTimer >= 0)
                {
                    iAwakenTimer -= uiDiff;
                    return;        // 动画结束前不执行其他操作
                } else if (bWakingUp && iAwakenTimer <= 0)
                {
                    bWakingUp = false;
                    bAmIAwake = true;
                    // 攻击激活祭坛的玩家
                    AttackStart(ObjectAccessor::GetUnit(*me, instance->GetGuidData(0))); // whoWokeArchaedasGUID
                    return;     // 完成AttackStart后返回
                }

                // 没有目标则返回
                if (!UpdateVictim())
                    return;

                DoMeleeAttackIfReady();
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUldamanAI<npc_archaedas_minionsAI>(creature);
        }
};

/* ScriptData
SDName: npc_stonekeepers
SD%Complete: 100
SDComment: After activating the altar of the keepers, the stone keepers will
wake up one by one.
EndScriptData */

/**
 * @brief 石像守卫脚本类
 *
 * 石像守卫位于守护者祭坛附近，玩家激活祭坛后会逐一唤醒。
 * 每个石像守卫死亡时会触发下一个守卫的激活。
 * 全部击杀后开启通往阿扎达斯区域的门。
 */
class npc_stonekeepers : public CreatureScript
{
    public:

        npc_stonekeepers()
            : CreatureScript("npc_stonekeepers")
        {
        }

        /**
         * @brief 石像守卫AI结构体
         */
        struct npc_stonekeepersAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            npc_stonekeepersAI(Creature* creature) : ScriptedAI(creature)
            {
                instance = me->GetInstanceScript();
            }

            InstanceScript* instance;   ///< 实例脚本指针

            /**
             * @brief 重置守卫状态
             *
             * 将守卫恢复到初始冻结状态，等待被激活。
             */
            void Reset() override
            {
                me->SetFaction(FACTION_FRIENDLY);
                me->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->SetControlled(true, UNIT_STATE_ROOT);
                me->RemoveAllAuras();
                me->AddAura(SPELL_MINION_FREEZE_ANIM, me);
            }

            /**
             * @brief 进入战斗时的处理
             * @param who 进入战斗的目标（未使用）
             *
             * 移除所有冻结效果，开始战斗。
             */
            void JustEngagedWith(Unit* /*who*/) override
            {
                me->SetFaction(FACTION_MONSTER);
                me->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                me->SetControlled(false, UNIT_STATE_ROOT);
            }

            /**
             * @brief 更新AI逻辑
             * @param diff 自上次更新以来经过的时间（毫秒，未使用）
             *
             * 简单的近战AI，检测目标并执行近战攻击。
             */
            void UpdateAI(uint32 /*diff*/) override
            {
                // 没有目标则返回
                if (!UpdateVictim())
                    return;

                DoMeleeAttackIfReady();
            }

            /**
             * @brief 死亡时的处理
             * @param killer 击杀者（未使用）
             *
             * 死亡时施放自毁法术（视觉效果），并通知实例脚本
             * 激活下一个石像守卫。
             */
            void JustDied(Unit* /*killer*/) override
            {
                DoCast (me, SPELL_SELF_DESTRUCT, true);
                instance->SetData(DATA_STONE_KEEPERS, IN_PROGRESS);    // 激活下一个石像守卫
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetUldamanAI<npc_stonekeepersAI>(creature);
        }
};

/* ScriptData
SDName: go_altar_archaedas
SD%Complete: 100
SDComment: Needs 1 person to activate the Archaedas script
SDCategory: Uldaman
EndScriptData */

/**
 * @brief 阿扎达斯祭坛游戏对象脚本类
 *
 * 玩家点击祭坛后会触发阿扎达斯的激活序列。
 * 自3.0.8版本起，只需要1人点击即可激活（之前需要3人）。
 */
class go_altar_of_archaedas : public GameObjectScript
{
    public:
        go_altar_of_archaedas() : GameObjectScript("go_altar_of_archaedas") { }

        /**
         * @brief 阿扎达斯祭坛AI结构体
         */
        struct go_altar_of_archaedasAI : public GameObjectAI
        {
            /**
             * @brief 构造函数
             * @param go 游戏对象指针
             */
            go_altar_of_archaedasAI(GameObject* go) : GameObjectAI(go), instance(go->GetInstanceScript()) { }

            InstanceScript* instance;   ///< 实例脚本指针

            /**
             * @brief 玩家点击游戏对象时的处理
             * @param player 点击祭坛的玩家
             * @return false 表示允许默认行为继续
             *
             * 执行以下操作：
             * 1. 对玩家施放视觉法术（激活特效）
             * 2. 通知实例脚本激活阿扎达斯，并记录玩家GUID
             */
            bool OnGossipHello(Player* player) override
            {
                player->CastSpell(player, SPELL_BOSS_OBJECT_VISUAL, false);

                instance->SetGuidData(0, player->GetGUID());     // 激活阿扎达斯
                return false;
            }
        };

        GameObjectAI* GetAI(GameObject* go) const override
        {
            return GetUldamanAI<go_altar_of_archaedasAI>(go);
        }
};

/**
 * @brief 脚本注册函数
 *
 * 此函数在脚本初始化时被调用一次。
 * 注册所有本文件中定义的脚本类。
 */
void AddSC_boss_archaedas()
{
    new boss_archaedas();
    new npc_archaedas_minions();
    new npc_stonekeepers();
    new go_altar_of_archaedas();
}
