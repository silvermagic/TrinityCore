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
 * @file boss_shade_of_aran.cpp
 * @brief 卡拉赞副本 - 埃兰之影BOSS脚本模块
 *
 * 本模块实现了埃兰之影BOSS及其召唤生物的战斗逻辑，包括:
 * - 埃兰之影BOSS AI
 * - 水元素AI
 *
 * 战斗机制:
 * - 埃兰之影是一名法师BOSS，使用冰霜、火焰、奥术三种魔法
 * - 会周期性施放超级法术：烈焰花环、暴风雪、奥术爆炸
 * - 当法力值低于20%时会召唤食物和水进行恢复
 * - 生命值低于40%时召唤4个水元素协助战斗
 * - 12分钟后进入狂暴，召唤影子分身
 * - 玩家可以打断埃兰的法术，打断后相应派系的法术会进入冷却
 *
 * 特殊技能:
 * - 烈焰花环：对3个玩家施放，移动会受到伤害
 * - 暴风雪：随机移动的暴风雪区域
 * - 奥术爆炸：将所有玩家拉到中心并减速，然后施放大范围爆炸
 */

/* ScriptData
SDName: Boss_Shade_of_Aran
SD%Complete: 95
SDComment: Flame wreath missing cast animation, mods won't triggere.
SDCategory: Karazhan
EndScriptData */

#include "ScriptMgr.h"
#include "karazhan.h"
#include "InstanceScript.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"

/**
 * @brief 埃兰之影相关枚举定义
 */
enum ShadeOfAran
{
    // 台词
    SAY_AGGRO                   = 0,  ///< 进入战斗台词
    SAY_FLAMEWREATH             = 1,  ///< 施放烈焰花环台词
    SAY_BLIZZARD                = 2,  ///< 施放暴风雪台词
    SAY_EXPLOSION               = 3,  ///< 施放奥术爆炸台词
    SAY_DRINK                   = 4,  ///< 开始喝水恢复法力台词
    SAY_ELEMENTALS              = 5,  ///< 召唤水元素台词
    SAY_KILL                    = 6,  ///< 击杀玩家台词
    SAY_TIMEOVER                = 7,  ///< 时间结束(狂暴)台词
    SAY_DEATH                   = 8,  ///< 死亡台词
//  SAY_ATIESH                  = 9, ///< 未使用

    // 埃兰之影法术
    SPELL_FROSTBOLT             = 29954,  ///< 寒冰箭：冰霜系基础法术
    SPELL_FIREBALL              = 29953,  ///< 火球术：火焰系基础法术
    SPELL_ARCMISSLE             = 29955,  ///< 奥术飞弹：奥术系基础法术
    SPELL_CHAINSOFICE           = 29991,  ///< 冰霜锁链：减速技能
    SPELL_DRAGONSBREATH         = 29964,  ///< 龙息术：锥形火焰伤害
    SPELL_MASSSLOW              = 30035,  ///< 群体减速：配合奥术爆炸使用
    SPELL_FLAME_WREATH          = 29946,  ///< 烈焰花环：困住玩家
    SPELL_AOE_CS                = 29961,  ///< 范围反制：打断施法
    SPELL_PLAYERPULL            = 32265,  ///< 玩家拉扯：将玩家拉到中心
    SPELL_AEXPLOSION            = 29973,  ///< 奥术爆炸：大范围奥术伤害
    SPELL_MASS_POLY             = 29963,  ///< 群体变形：法力低时使用
    SPELL_BLINK_CENTER          = 29967,  ///< 闪现至中心：配合奥术爆炸
    SPELL_ELEMENTALS            = 29962,  ///< 召唤水元素
    SPELL_CONJURE               = 29975,  ///< 制造食物和水
    SPELL_DRINK                 = 30024,  ///< 喝水：恢复法力
    SPELL_POTION                = 32453,  ///< 法力药水
    SPELL_AOE_PYROBLAST         = 29978,  ///< 范围炎爆术：喝水后施放

    // 召唤生物法术
    SPELL_CIRCULAR_BLIZZARD     = 29951,  ///< 环形暴风雪：由暴风雪生物施放
    SPELL_WATERBOLT             = 31012,  ///< 水箭：水元素攻击技能
    SPELL_SHADOW_PYRO           = 29978,  ///< 暗影炎爆：影子分身使用

    // 生物ID
    CREATURE_WATER_ELEMENTAL    = 17167,  ///< 水元素NPC ID
    CREATURE_SHADOW_OF_ARAN     = 18254,  ///< 埃兰之影分身NPC ID
    CREATURE_ARAN_BLIZZARD      = 17161,  ///< 暴风雪NPC ID
};

/**
 * @brief 超级法术类型枚举
 *
 * 定义埃兰之影的三种超级法术类型
 */
enum SuperSpell
{
    SUPER_FLAME = 0,    ///< 烈焰花环
    SUPER_BLIZZARD,     ///< 暴风雪
    SUPER_AE,           ///< 奥术爆炸
};

/**
 * @class boss_shade_of_aran
 * @brief 埃兰之影BOSS脚本类
 *
 * 继承自CreatureScript，用于注册埃兰之影BOSS的AI脚本
 */
class boss_shade_of_aran : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册埃兰之影脚本名称
     */
    boss_shade_of_aran() : CreatureScript("boss_shade_of_aran") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回埃兰之影AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_aranAI>(creature);
    }

    /**
     * @struct boss_aranAI
     * @brief 埃兰之影BOSS的AI实现
     *
     * 继承自ScriptedAI，实现埃兰之影的战斗逻辑:
     * - 使用冰霜、火焰、奥术三种基础法术循环攻击
     * - 周期性施放超级法术（烈焰花环、暴风雪、奥术爆炸）
     * - 法力低于20%时喝水恢复
     * - 生命值低于40%时召唤水元素
     * - 12分钟后狂暴召唤影子分身
     */
    struct boss_aranAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化埃兰之影AI并设置副本脚本
         */
        boss_aranAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
        }

        /**
         * @brief 初始化成员变量
         *
         * 功能: 设置所有计时器和状态标志的初始值
         */
        void Initialize()
        {
            SecondarySpellTimer = 5000;   ///< 次要法术计时器(冰霜锁链/范围反制)
            NormalCastTimer = 0;          ///< 普通法术计时器
            SuperCastTimer = 35000;       ///< 超级法术计时器(35秒)
            BerserkTimer = 720000;        ///< 狂暴计时器(12分钟)
            CloseDoorTimer = 15000;       ///< 关门计时器(15秒后关门，允许玩家进入)

            LastSuperSpell = rand32() % 3;  ///< 上一个超级法术类型

            FlameWreathTimer = 0;         ///< 烈焰花环持续时间
            FlameWreathCheckTime = 0;     ///< 烈焰花环检查间隔

            CurrentNormalSpell = 0;       ///< 当前正在施放的普通法术
            ArcaneCooldown = 0;           ///< 奥术系法术冷却(被断法后)
            FireCooldown = 0;             ///< 火焰系法术冷却
            FrostCooldown = 0;            ///< 冰霜系法术冷却

            DrinkInterruptTimer = 10000;  ///< 喝水被打断的延迟时间

            ElementalsSpawned = false;    ///< 是否已召唤水元素
            Drinking = false;             ///< 是否正在喝水
            DrinkInturrupted = false;     ///< 喝水是否被打断
        }

        InstanceScript* instance;         ///< 副本实例脚本指针

        uint32 SecondarySpellTimer;       ///< 次要法术计时器
        uint32 NormalCastTimer;           ///< 普通法术计时器
        uint32 SuperCastTimer;            ///< 超级法术计时器
        uint32 BerserkTimer;              ///< 狂暴计时器
        uint32 CloseDoorTimer;            ///< 关门计时器

        uint8 LastSuperSpell;             ///< 上一个超级法术类型

        uint32 FlameWreathTimer;          ///< 烈焰花环持续时间
        uint32 FlameWreathCheckTime;      ///< 烈焰花环检查间隔
        ObjectGuid FlameWreathTarget[3];  ///< 烈焰花环目标的GUID
        float FWTargPosX[3];              ///< 烈焰花环目标的X坐标
        float FWTargPosY[3];              ///< 烈焰花环目标的Y坐标

        uint32 CurrentNormalSpell;        ///< 当前正在施放的普通法术ID
        uint32 ArcaneCooldown;            ///< 奥术系法术冷却时间
        uint32 FireCooldown;              ///< 火焰系法术冷却时间
        uint32 FrostCooldown;             ///< 冰霜系法术冷却时间

        uint32 DrinkInterruptTimer;       ///< 喝水被打断的延迟时间

        bool ElementalsSpawned;           ///< 是否已召唤水元素
        bool Drinking;                    ///< 是否正在喝水
        bool DrinkInturrupted;            ///< 喝水是否被打断

        /**
         * @brief 重置BOSS状态
         *
         * 调用时机: BOSS脱离战斗或重置时
         * 功能: 初始化变量，设置副本状态为未开始，打开图书馆门
         */
        void Reset() override
        {
            Initialize();

            // 设置副本状态为未开始
            instance->SetBossState(DATA_ARAN, NOT_STARTED);
            instance->HandleGameObject(instance->GetGuidData(DATA_GO_LIBRARY_DOOR), true);
        }

        /**
         * @brief 击杀单位回调
         * @param victim 被击杀的单位(未使用)
         *
         * 调用时机: 埃兰之影杀死一个单位时
         * 功能: 播放击杀台词
         */
        void KilledUnit(Unit* /*victim*/) override
        {
            Talk(SAY_KILL);
        }

        /**
         * @brief 死亡回调
         * @param killer 击杀者(未使用)
         *
         * 调用时机: 埃兰之影死亡时
         * 功能: 播放死亡台词，设置副本状态为完成，打开图书馆门
         */
        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_DEATH);

            instance->SetBossState(DATA_ARAN, DONE);
            instance->HandleGameObject(instance->GetGuidData(DATA_GO_LIBRARY_DOOR), true);
        }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标(未使用)
         *
         * 调用时机: 埃兰之影进入战斗时
         * 功能: 播放战斗开始台词，设置副本状态为进行中，关闭图书馆门
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            Talk(SAY_AGGRO);

            instance->SetBossState(DATA_ARAN, IN_PROGRESS);
            instance->HandleGameObject(instance->GetGuidData(DATA_GO_LIBRARY_DOOR), false);
        }

        /**
         * @brief 施放烈焰花环效果
         *
         * 功能:
         * - 从威胁列表中随机选择最多3个玩家
         * - 记录他们的位置和GUID
         * - 对其施放烈焰花环法术
         * - 玩家移动会触发伤害
         */
        void FlameWreathEffect()
        {
            std::vector<Unit*> targets;
            // 将威胁列表存储到临时容器中
            for (auto* ref : me->GetThreatManager().GetUnsortedThreatList())
            {
                Unit* target = ref->GetVictim();
                if (ref->GetVictim()->GetTypeId() == TYPEID_PLAYER && ref->GetVictim()->IsAlive())
                    targets.push_back(target);
            }

            // 如果目标超过3个，随机移除多余的
            while (targets.size() > 3)
                targets.erase(targets.begin() + rand32() % targets.size());

            // 记录目标信息并施放烈焰花环
            uint32 i = 0;
            for (std::vector<Unit*>::const_iterator itr = targets.begin(); itr!= targets.end(); ++itr)
            {
                if (*itr)
                {
                    FlameWreathTarget[i] = (*itr)->GetGUID();
                    FWTargPosX[i] = (*itr)->GetPositionX();
                    FWTargPosY[i] = (*itr)->GetPositionY();
                    DoCast((*itr), SPELL_FLAME_WREATH, true);
                    ++i;
                }
            }
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * 调用时机: 每个游戏循环 tick
         * 功能: 处理埃兰之影的所有战斗逻辑:
         * - 关门计时器
         * - 法术冷却更新
         * - 法力恢复机制
         * - 普通法术施放
         * - 次要法术施放
         * - 超级法术施放
         * - 水元素召唤
         * - 狂暴机制
         * - 烈焰花环检测
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            // 关门计时器：15秒后关闭图书馆门
            if (CloseDoorTimer)
            {
                if (CloseDoorTimer <= diff)
                {
                    instance->HandleGameObject(instance->GetGuidData(DATA_GO_LIBRARY_DOOR), false);
                    CloseDoorTimer = 0;
                } else CloseDoorTimer -= diff;
            }

            // 更新各派系法术冷却时间
            if (ArcaneCooldown)
            {
                if (ArcaneCooldown >= diff)
                    ArcaneCooldown -= diff;
            else ArcaneCooldown = 0;
            }

            if (FireCooldown)
            {
                if (FireCooldown >= diff)
                    FireCooldown -= diff;
            else FireCooldown = 0;
            }

            if (FrostCooldown)
            {
                if (FrostCooldown >= diff)
                    FrostCooldown -= diff;
            else FrostCooldown = 0;
            }

            // 法力低于20%时开始喝水恢复
            if (!Drinking && me->GetMaxPower(POWER_MANA) && me->GetPowerPct(POWER_MANA) < 20.f)
            {
                Drinking = true;
                me->InterruptNonMeleeSpells(false);

                Talk(SAY_DRINK);

                if (!DrinkInturrupted)
                {
                    DoCast(me, SPELL_MASS_POLY, true);  // 群体变形
                    DoCast(me, SPELL_CONJURE, false);   // 制造食物和水
                    DoCast(me, SPELL_DRINK, false);     // 开始喝水
                    me->SetStandState(UNIT_STAND_STATE_SIT);  // 坐下
                    DrinkInterruptTimer = 10000;
                }
            }

            // 喝水被打断时的处理
            if (Drinking && DrinkInturrupted)
            {
                Drinking = false;
                me->RemoveAurasDueToSpell(SPELL_DRINK);
                me->SetStandState(UNIT_STAND_STATE_STAND);
                me->SetPower(POWER_MANA, me->GetMaxPower(POWER_MANA)-32000);
                DoCast(me, SPELL_POTION, false);  // 使用法力药水
            }

            // 喝水计时器：10秒后施放炎爆术
            if (Drinking && !DrinkInturrupted)
            {
                if (DrinkInterruptTimer >= diff)
                    DrinkInterruptTimer -= diff;
                else
                {
                    me->SetStandState(UNIT_STAND_STATE_STAND);
                    DoCast(me, SPELL_POTION, true);
                    DoCast(me, SPELL_AOE_PYROBLAST, false);  // 范围炎爆术
                    DrinkInturrupted = true;
                    Drinking = false;
                }
            }

            // 如果正在喝水，不执行后续逻辑
            if (Drinking)
                return;

            // 普通法术施放：选择可用的法术施放
            if (NormalCastTimer <= diff)
            {
                if (!me->IsNonMeleeSpellCast(false))
                {
                    Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true);
                    if (!target)
                        return;

                    uint32 Spells[3];
                    uint8 AvailableSpells = 0;

                    // 检查哪些法术不在冷却中
                    if (!ArcaneCooldown)
                    {
                        Spells[AvailableSpells] = SPELL_ARCMISSLE;
                        ++AvailableSpells;
                    }
                    if (!FireCooldown)
                    {
                        Spells[AvailableSpells] = SPELL_FIREBALL;
                        ++AvailableSpells;
                    }
                    if (!FrostCooldown)
                    {
                        Spells[AvailableSpells] = SPELL_FROSTBOLT;
                        ++AvailableSpells;
                    }

                    // 如果有可用法术，随机施放一个
                    if (AvailableSpells)
                    {
                        CurrentNormalSpell = Spells[rand32() % AvailableSpells];
                        DoCast(target, CurrentNormalSpell);
                    }
                }
                NormalCastTimer = 1000;
            } else NormalCastTimer -= diff;

            // 次要法术施放：冰霜锁链或范围反制
            if (SecondarySpellTimer <= diff)
            {
                switch (urand(0, 1))
                {
                    case 0:
                        DoCast(me, SPELL_AOE_CS);  // 范围反制
                        break;
                    case 1:
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                            DoCast(target, SPELL_CHAINSOFICE);  // 冰霜锁链
                        break;
                }
                SecondarySpellTimer = urand(5000, 20000);
            } else SecondarySpellTimer -= diff;

            // 超级法术施放：烈焰花环、暴风雪或奥术爆炸
            if (SuperCastTimer <= diff)
            {
                uint8 Available[2];

                // 选择与上次不同的超级法术
                switch (LastSuperSpell)
                {
                    case SUPER_AE:
                        Available[0] = SUPER_FLAME;
                        Available[1] = SUPER_BLIZZARD;
                        break;
                    case SUPER_FLAME:
                        Available[0] = SUPER_AE;
                        Available[1] = SUPER_BLIZZARD;
                        break;
                    case SUPER_BLIZZARD:
                        Available[0] = SUPER_FLAME;
                        Available[1] = SUPER_AE;
                        break;
                    default:
                        Available[0] = 0;
                        Available[1] = 0;
                        break;
                }

                LastSuperSpell = Available[urand(0, 1)];

                switch (LastSuperSpell)
                {
                    case SUPER_AE:  // 奥术爆炸
                        Talk(SAY_EXPLOSION);

                        DoCast(me, SPELL_BLINK_CENTER, true);  // 闪现至中心
                        DoCast(me, SPELL_PLAYERPULL, true);    // 拉玩家到中心
                        DoCast(me, SPELL_MASSSLOW, true);      // 群体减速
                        DoCast(me, SPELL_AEXPLOSION, false);   // 奥术爆炸
                        break;

                    case SUPER_FLAME:  // 烈焰花环
                        Talk(SAY_FLAMEWREATH);

                        FlameWreathTimer = 20000;
                        FlameWreathCheckTime = 500;

                        FlameWreathTarget[0].Clear();
                        FlameWreathTarget[1].Clear();
                        FlameWreathTarget[2].Clear();

                        FlameWreathEffect();
                        break;

                    case SUPER_BLIZZARD:  // 暴风雪
                        Talk(SAY_BLIZZARD);

                        if (Creature* pSpawn = me->SummonCreature(CREATURE_ARAN_BLIZZARD, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_TIMED_DESPAWN, 25s))
                        {
                            pSpawn->SetFaction(me->GetFaction());
                            pSpawn->CastSpell(pSpawn, SPELL_CIRCULAR_BLIZZARD, false);
                        }
                        break;
                }

                SuperCastTimer = urand(35000, 40000);
            } else SuperCastTimer -= diff;

            // 生命值低于40%时召唤4个水元素
            if (!ElementalsSpawned && HealthBelowPct(40))
            {
                ElementalsSpawned = true;

                for (uint32 i = 0; i < 4; ++i)
                {
                    if (Creature* unit = me->SummonCreature(CREATURE_WATER_ELEMENTAL, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_TIMED_DESPAWN, 90s))
                    {
                        unit->Attack(me->GetVictim(), true);
                        unit->SetFaction(me->GetFaction());
                    }
                }

                Talk(SAY_ELEMENTALS);
            }

            // 12分钟后狂暴：召唤5个影子分身
            if (BerserkTimer <= diff)
            {
                for (uint32 i = 0; i < 5; ++i)
                {
                    if (Creature* unit = me->SummonCreature(CREATURE_SHADOW_OF_ARAN, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5s))
                    {
                        unit->Attack(me->GetVictim(), true);
                        unit->SetFaction(me->GetFaction());
                    }
                }

                Talk(SAY_TIMEOVER);

                BerserkTimer = 60000;  // 每分钟召唤一次
            } else BerserkTimer -= diff;

            // 烈焰花环检测：检查目标是否移动
            if (FlameWreathTimer)
            {
                if (FlameWreathTimer >= diff)
                    FlameWreathTimer -= diff;
                else FlameWreathTimer = 0;

                if (FlameWreathCheckTime <= diff)
                {
                    for (uint8 i = 0; i < 3; ++i)
                    {
                        if (!FlameWreathTarget[i])
                            continue;

                        Unit* unit = ObjectAccessor::GetUnit(*me, FlameWreathTarget[i]);
                        // 如果目标移动超过3码，触发伤害
                        if (unit && !unit->IsWithinDist2d(FWTargPosX[i], FWTargPosY[i], 3))
                        {
                            unit->CastSpell(unit, 20476, me->GetGUID());  // 烈焰花环伤害
                            unit->CastSpell(unit, 11027, true);           // 击退效果
                            FlameWreathTarget[i].Clear();
                        }
                    }
                    FlameWreathCheckTime = 500;  // 每0.5秒检查一次
                } else FlameWreathCheckTime -= diff;
            }

            // 如果所有派系法术都在冷却中，进行近战攻击
            if (ArcaneCooldown && FireCooldown && FrostCooldown)
                DoMeleeAttackIfReady();
        }

        /**
         * @brief 受到伤害回调
         * @param pAttacker 攻击者(未使用)
         * @param damage 伤害值
         * @param damageType 伤害类型(未使用)
         * @param spellInfo 法术信息(未使用)
         *
         * 调用时机: 埃兰之影受到伤害时
         * 功能: 如果正在喝水且受到伤害，打断喝水状态
         */
        void DamageTaken(Unit* /*pAttacker*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            if (!DrinkInturrupted && Drinking && damage)
                DrinkInturrupted = true;
        }

        /**
         * @brief 法术命中回调
         * @param caster 施法者(未使用)
         * @param spellInfo 法术信息
         *
         * 调用时机: 法术命中埃兰之影时
         * 功能:
         * - 只关心打断效果的法术
         * - 打断当前正在施放的法术
         * - 根据被打断的法术类型设置相应派系的冷却时间
         */
        void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
        {
            // 只处理打断施法效果的法术，且埃兰正在施放法术
            if (!spellInfo->HasEffect(SPELL_EFFECT_INTERRUPT_CAST) || !me->IsNonMeleeSpellCast(false))
                return;

            // 打断当前施法
            me->InterruptNonMeleeSpells(false);

            // 根据当前施放的法术类型设置对应派系的冷却时间
            switch (CurrentNormalSpell)
            {
                case SPELL_ARCMISSLE: ArcaneCooldown = 5000; break;   // 奥术系冷却5秒
                case SPELL_FIREBALL: FireCooldown = 5000; break;      // 火焰系冷却5秒
                case SPELL_FROSTBOLT: FrostCooldown = 5000; break;    // 冰霜系冷却5秒
            }
        }
    };
};

/**
 * @class npc_aran_elemental
 * @brief 埃兰之影水元素NPC脚本类
 *
 * 继承自CreatureScript，用于注册水元素的AI脚本
 * 水元素在埃兰之影生命值低于40%时被召唤，协助战斗
 */
class npc_aran_elemental : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册水元素脚本名称
     */
    npc_aran_elemental() : CreatureScript("npc_aran_elemental") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回水元素AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<water_elementalAI>(creature);
    }

    /**
     * @struct water_elementalAI
     * @brief 水元素AI实现
     *
     * 继承自ScriptedAI，实现水元素的战斗逻辑:
     * - 使用水箭攻击目标
     */
    struct water_elementalAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化水元素AI
         */
        water_elementalAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         *
         * 功能: 设置施法计时器的随机初始值(2-5秒)
         */
        void Initialize()
        {
            CastTimer = 2000 + (rand32() % 3000);
        }

        uint32 CastTimer;  ///< 水箭施法计时器

        /**
         * @brief 重置状态
         *
         * 调用时机: 生物重置时
         * 功能: 初始化施法计时器
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标(未使用)
         */
        void JustEngagedWith(Unit* /*who*/) override { }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * 调用时机: 每个游戏循环 tick
         * 功能: 定期施放水箭攻击目标
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            // 施放水箭
            if (CastTimer <= diff)
            {
                DoCastVictim(SPELL_WATERBOLT);
                CastTimer = urand(2000, 5000);
            } else CastTimer -= diff;
        }
    };
};

/**
 * @brief 注册埃兰之影BOSS脚本
 *
 * 调用时机: 服务器启动时加载脚本模块
 * 功能: 创建埃兰之影和水元素脚本实例，注册到脚本系统
 */
void AddSC_boss_shade_of_aran()
{
    new boss_shade_of_aran();
    new npc_aran_elemental();
}
