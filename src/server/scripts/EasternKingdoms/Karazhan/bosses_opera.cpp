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
 * @file bosses_opera.cpp
 * @brief 卡拉赞副本 - 歌剧事件BOSS脚本模块
 *
 * 本模块实现了卡拉赞歌剧事件的三个可选战斗:
 *
 * 1. 绿野仙踪(Wizard of Oz)事件:
 *    - 多萝西(Dorothee)和托托(Tito)
 *    - 稻草人(Strawman)
 *    - 铁皮人(Tinhead)
 *    - 狮子(Roar)
 *    - 女巫(Crone) - 最终BOSS
 *
 * 2. 小红帽(Red Riding Hood)事件:
 *    - 大灰狼(Big Bad Wolf)
 *
 * 3. 罗密欧与朱丽叶(Romulo and Julianne)事件:
 *    - 朱丽叶(Julianne)
 *    - 罗密欧(Romulo)
 *
 * 每周随机选择其中一个事件进行战斗
 */

/* ScriptData
SDName: Bosses_Opera
SD%Complete: 90
SDComment: Oz, Hood, and RAJ event implemented. RAJ event requires more testing.
SDCategory: Karazhan
EndScriptData */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "karazhan.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"

/***********************************/
/*** OPERA WIZARD OF OZ EVENT *****/
/*********************************/

/**
 * @brief 绿野仙踪事件台词枚举
 *
 * 定义各个角色的台词和表情
 */
enum Says
{
    // 多萝西台词
    SAY_DOROTHEE_DEATH          = 0,  ///< 多萝西死亡台词
    SAY_DOROTHEE_SUMMON         = 1,  ///< 多萝西召唤托托台词
    SAY_DOROTHEE_TITO_DEATH     = 2,  ///< 托托死亡时多萝西的台词
    SAY_DOROTHEE_AGGRO          = 3,  ///< 多萝西进入战斗台词

    // 狮子台词
    SAY_ROAR_AGGRO              = 0,  ///< 狮子进入战斗台词
    SAY_ROAR_DEATH              = 1,  ///< 狮子死亡台词
    SAY_ROAR_SLAY               = 2,  ///< 狮子击杀玩家台词

    // 稻草人台词
    SAY_STRAWMAN_AGGRO          = 0,  ///< 稻草人进入战斗台词
    SAY_STRAWMAN_DEATH          = 1,  ///< 稻草人死亡台词
    SAY_STRAWMAN_SLAY           = 2,  ///< 稻草人击杀玩家台词

    // 铁皮人台词
    SAY_TINHEAD_AGGRO           = 0,  ///< 铁皮人进入战斗台词
    SAY_TINHEAD_DEATH           = 1,  ///< 铁皮人死亡台词
    SAY_TINHEAD_SLAY            = 2,  ///< 铁皮人击杀玩家台词
    EMOTE_RUST                  = 3,  ///< 铁皮人生锈表情

    // 女巫台词
    SAY_CRONE_AGGRO             = 0,  ///< 女巫进入战斗台词
    SAY_CRONE_DEATH             = 1,  ///< 女巫死亡台词
    SAY_CRONE_SLAY              = 2,  ///< 女巫击杀玩家台词
};

/**
 * @brief 绿野仙踪事件法术ID枚举
 *
 * 定义所有角色使用的法术技能ID
 */
enum Spells
{
    // 多萝西技能
    SPELL_WATERBOLT         = 31012,  ///< 水箭：对目标造成冰霜伤害
    SPELL_SCREAM            = 31013,  ///< 尖叫：恐惧效果
    SPELL_SUMMONTITO        = 31014,  ///< 召唤托托

    // 托托技能
    SPELL_YIPPING           = 31015,  ///< 吠叫：对目标造成伤害

    // 稻草人技能
    SPELL_BRAIN_BASH        = 31046,  ///< 脑部重击：使目标昏迷
    SPELL_BRAIN_WIPE        = 31069,  ///< 脑部清除：清空目标法力值
    SPELL_BURNING_STRAW     = 31075,  ///< 燃烧稻草：被火焰法术击中时触发

    // 铁皮人技能
    SPELL_CLEAVE            = 31043,  ///< 顺劈斩：前方范围伤害
    SPELL_RUST              = 31086,  ///< 生锈：降低移动速度

    // 狮子技能
    SPELL_MANGLE            = 31041,  ///< 撕裂：对目标造成伤害并降低护甲
    SPELL_SHRED             = 31042,  ///< 撕碎：对目标造成伤害
    SPELL_FRIGHTENED_SCREAM = 31013,  ///< 恐惧尖叫：恐惧效果

    // 女巫技能
    SPELL_CHAIN_LIGHTNING   = 32337,  ///< 闪电链：连锁闪电伤害

    // 旋风技能
    SPELL_KNOCKBACK         = 32334,  ///< 击退：将玩家击退
    SPELL_CYCLONE_VISUAL    = 32332,  ///< 旋风视觉效果
};

/**
 * @brief 绿野仙踪事件生物ID枚举
 */
enum Creatures
{
    CREATURE_TITO           = 17548,  ///< 托托(小狗)NPC ID
    CREATURE_CYCLONE        = 18412,  ///< 旋风NPC ID
    CREATURE_CRONE          = 18168,  ///< 女巫NPC ID
};

/**
 * @brief 如果所有前置BOSS已死亡，召唤女巫
 * @param instance 副本实例脚本指针
 * @param creature 召唤者生物指针
 *
 * 功能:
 * - 每次调用增加死亡计数
 * - 当死亡计数达到4时(多萝西、稻草人、铁皮人、狮子全部死亡)，召唤女巫
 * - 女巫是绿野仙踪事件的最终BOSS
 */
void SummonCroneIfReady(InstanceScript* instance, Creature* creature)
{
    instance->SetData(DATA_OPERA_OZ_DEATHCOUNT, SPECIAL);  // 增加死亡计数

    // 当4个前置BOSS全部死亡时，召唤女巫
    if (instance->GetData(DATA_OPERA_OZ_DEATHCOUNT) == 4)
    {
        if (Creature* pCrone = creature->SummonCreature(CREATURE_CRONE, -10891.96f, -1755.95f, creature->GetPositionZ(), 4.64f, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 2h))
        {
            if (creature->GetVictim())
                pCrone->AI()->AttackStart(creature->GetVictim());
        }
    }
}

/**
 * @class boss_dorothee
 * @brief 多萝西BOSS脚本类
 *
 * 继承自CreatureScript，用于注册多萝西BOSS的AI脚本
 * 多萝西是绿野仙踪事件的第一个BOSS，会召唤她的小狗托托
 */
class boss_dorothee : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册多萝西脚本名称
     */
    boss_dorothee() : CreatureScript("boss_dorothee") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回多萝西AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_dorotheeAI>(creature);
    }

    /**
     * @struct boss_dorotheeAI
     * @brief 多萝西BOSS的AI实现
     *
     * 继承自ScriptedAI，实现多萝西的战斗逻辑:
     * - 使用水箭攻击随机目标
     * - 周期性施放恐惧尖叫
     * - 召唤小狗托托协助战斗
     * - 托托死亡后加快施法速度
     */
    struct boss_dorotheeAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化多萝西AI并设置副本脚本
         */
        boss_dorotheeAI(Creature* creature) : ScriptedAI(creature)
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
            AggroTimer = 500;  ///< 激活计时器(0.5秒后可被攻击)

            WaterBoltTimer = 5000;    ///< 水箭计时器(5秒)
            FearTimer = 15000;        ///< 恐惧计时器(15秒)
            SummonTitoTimer = 47500;  ///< 召唤托托计时器(47.5秒)

            SummonedTito = false;  ///< 是否已召唤托托
            TitoDied = false;      ///< 托托是否死亡
        }

        InstanceScript* instance;  ///< 副本实例脚本指针

        uint32 AggroTimer;         ///< 激活计时器

        uint32 WaterBoltTimer;     ///< 水箭施法计时器
        uint32 FearTimer;          ///< 恐惧施法计时器
        uint32 SummonTitoTimer;    ///< 召唤托托计时器

        bool SummonedTito;         ///< 是否已召唤托托
        bool TitoDied;             ///< 托托是否死亡

        /**
         * @brief 重置BOSS状态
         *
         * 调用时机: BOSS脱离战斗或重置时
         * 功能: 初始化所有成员变量
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标(未使用)
         *
         * 调用时机: 多萝西进入战斗时
         * 功能: 播放战斗开始台词
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            Talk(SAY_DOROTHEE_AGGRO);
        }

        /**
         * @brief 返回出生点回调
         *
         * 调用时机: 多萝西脱离战斗返回出生点时
         * 功能: 消失多萝西
         */
        void JustReachedHome() override
        {
            me->DespawnOrUnsummon();
        }

        void SummonTito();

        /**
         * @brief 死亡回调
         * @param killer 击杀者(未使用)
         *
         * 调用时机: 多萝西死亡时
         * 功能: 播放死亡台词，检查是否召唤女巫
         */
        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_DOROTHEE_DEATH);

            SummonCroneIfReady(instance, me);
        }

        /**
         * @brief 开始攻击
         * @param who 攻击目标
         *
         * 调用时机: 开始攻击目标时
         * 功能: 如果处于不可攻击状态，不进行攻击
         */
        void AttackStart(Unit* who) override
        {
            if (me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                return;

            ScriptedAI::AttackStart(who);
        }

        /**
         * @brief 视线检测
         * @param who 进入视线的单位
         *
         * 调用时机: 单位进入视线范围时
         * 功能: 如果处于不可攻击状态，不进入战斗
         */
        void MoveInLineOfSight(Unit* who) override

        {
            if (me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                return;

            ScriptedAI::MoveInLineOfSight(who);
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * 调用时机: 每个游戏循环 tick
         * 功能:
         * - 处理激活计时器
         * - 施放水箭攻击
         * - 施放恐惧尖叫
         * - 召唤托托
         * - 执行近战攻击
         */
        void UpdateAI(uint32 diff) override
        {
            // 激活计时器：0.5秒后移除不可攻击标志
            if (AggroTimer)
            {
                if (AggroTimer <= diff)
                {
                    me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                    AggroTimer = 0;
                } else AggroTimer -= diff;
            }

            if (!UpdateVictim())
                return;

            // 水箭：托托死亡后施法频率加快
            if (WaterBoltTimer <= diff)
            {
                DoCast(SelectTarget(SelectTargetMethod::Random, 0), SPELL_WATERBOLT);
                WaterBoltTimer = TitoDied ? 1500 : 5000;
            } else WaterBoltTimer -= diff;

            // 恐惧尖叫
            if (FearTimer <= diff)
            {
                DoCastVictim(SPELL_SCREAM);
                FearTimer = 30000;
            } else FearTimer -= diff;

            // 召唤托托
            if (!SummonedTito)
            {
                if (SummonTitoTimer <= diff)
                    SummonTito();
                else SummonTitoTimer -= diff;
            }

            DoMeleeAttackIfReady();
        }
    };
};

class npc_tito : public CreatureScript
{
public:
    npc_tito() : CreatureScript("npc_tito") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<npc_titoAI>(creature);
    }

    struct npc_titoAI : public ScriptedAI
    {
        npc_titoAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            DorotheeGUID.Clear();
            YipTimer = 10000;
        }

        ObjectGuid DorotheeGUID;
        uint32 YipTimer;

        void Reset() override
        {
            Initialize();
        }

        void JustEngagedWith(Unit* /*who*/) override { }

        void JustDied(Unit* /*killer*/) override
        {
            if (DorotheeGUID)
            {
                Creature* Dorothee = (ObjectAccessor::GetCreature((*me), DorotheeGUID));
                if (Dorothee && Dorothee->IsAlive())
                {
                    ENSURE_AI(boss_dorothee::boss_dorotheeAI, Dorothee->AI())->TitoDied = true;
                    Talk(SAY_DOROTHEE_TITO_DEATH, Dorothee);
                }
            }
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            if (YipTimer <= diff)
            {
                DoCastVictim(SPELL_YIPPING);
                YipTimer = 10000;
            } else YipTimer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

void boss_dorothee::boss_dorotheeAI::SummonTito()
{
    if (Creature* pTito = me->SummonCreature(CREATURE_TITO, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 30s))
    {
        Talk(SAY_DOROTHEE_SUMMON);
        ENSURE_AI(npc_tito::npc_titoAI, pTito->AI())->DorotheeGUID = me->GetGUID();
        pTito->AI()->AttackStart(me->GetVictim());
        SummonedTito = true;
        TitoDied = false;
    }
}

class boss_strawman : public CreatureScript
{
public:
    boss_strawman() : CreatureScript("boss_strawman") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_strawmanAI>(creature);
    }

    struct boss_strawmanAI : public ScriptedAI
    {
        boss_strawmanAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
        }

        void Initialize()
        {
            AggroTimer = 13000;
            BrainBashTimer = 5000;
            BrainWipeTimer = 7000;
        }

        InstanceScript* instance;

        uint32 AggroTimer;
        uint32 BrainBashTimer;
        uint32 BrainWipeTimer;

        void Reset() override
        {
            Initialize();
        }

        void AttackStart(Unit* who) override
        {
            if (me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                return;

            ScriptedAI::AttackStart(who);
        }

        void MoveInLineOfSight(Unit* who) override

        {
            if (me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                return;

            ScriptedAI::MoveInLineOfSight(who);
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            Talk(SAY_STRAWMAN_AGGRO);
        }

        void JustReachedHome() override
        {
            me->DespawnOrUnsummon();
        }

        void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
        {
            if ((spellInfo->SchoolMask == SPELL_SCHOOL_MASK_FIRE) && (!(rand32() % 10)))
            {
                /*
                    if (not direct damage(aoe, dot))
                        return;
                */

                DoCast(me, SPELL_BURNING_STRAW, true);
            }
        }

        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_STRAWMAN_DEATH);

            SummonCroneIfReady(instance, me);
        }

        void KilledUnit(Unit* /*victim*/) override
        {
            Talk(SAY_STRAWMAN_SLAY);
        }

        void UpdateAI(uint32 diff) override
        {
            if (AggroTimer)
            {
                if (AggroTimer <= diff)
                {
                    me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                    AggroTimer = 0;
                } else AggroTimer -= diff;
            }

            if (!UpdateVictim())
                return;

            if (BrainBashTimer <= diff)
            {
                DoCastVictim(SPELL_BRAIN_BASH);
                BrainBashTimer = 15000;
            } else BrainBashTimer -= diff;

            if (BrainWipeTimer <= diff)
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                    DoCast(target, SPELL_BRAIN_WIPE);
                BrainWipeTimer = 20000;
            } else BrainWipeTimer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

class boss_tinhead : public CreatureScript
{
public:
    boss_tinhead() : CreatureScript("boss_tinhead") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_tinheadAI>(creature);
    }

    struct boss_tinheadAI : public ScriptedAI
    {
        boss_tinheadAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
        }

        void Initialize()
        {
            AggroTimer = 15000;
            CleaveTimer = 5000;
            RustTimer = 30000;

            RustCount = 0;
        }

        InstanceScript* instance;

        uint32 AggroTimer;
        uint32 CleaveTimer;
        uint32 RustTimer;

        uint8 RustCount;

        void Reset() override
        {
            Initialize();
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            Talk(SAY_TINHEAD_AGGRO);
        }

        void JustReachedHome() override
        {
            me->DespawnOrUnsummon();
        }

        void AttackStart(Unit* who) override
        {
            if (me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                return;

            ScriptedAI::AttackStart(who);
        }

        void MoveInLineOfSight(Unit* who) override

        {
            if (me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                return;

            ScriptedAI::MoveInLineOfSight(who);
        }

        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_TINHEAD_DEATH);

            SummonCroneIfReady(instance, me);
        }

        void KilledUnit(Unit* /*victim*/) override
        {
            Talk(SAY_TINHEAD_SLAY);
        }

        void UpdateAI(uint32 diff) override
        {
            if (AggroTimer)
            {
                if (AggroTimer <= diff)
                {
                    me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                    AggroTimer = 0;
                } else AggroTimer -= diff;
            }

            if (!UpdateVictim())
                return;

            if (CleaveTimer <= diff)
            {
                DoCastVictim(SPELL_CLEAVE);
                CleaveTimer = 5000;
            } else CleaveTimer -= diff;

            if (RustCount < 8)
            {
                if (RustTimer <= diff)
                {
                    ++RustCount;
                    Talk(EMOTE_RUST);
                    DoCast(me, SPELL_RUST);
                    RustTimer = 6000;
                } else RustTimer -= diff;
            }

            DoMeleeAttackIfReady();
        }
    };
};

class boss_roar : public CreatureScript
{
public:
    boss_roar() : CreatureScript("boss_roar") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_roarAI>(creature);
    }

    struct boss_roarAI : public ScriptedAI
    {
        boss_roarAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
        }

        void Initialize()
        {
            AggroTimer = 20000;
            MangleTimer = 5000;
            ShredTimer = 10000;
            ScreamTimer = 15000;
        }

        InstanceScript* instance;

        uint32 AggroTimer;
        uint32 MangleTimer;
        uint32 ShredTimer;
        uint32 ScreamTimer;

        void Reset() override
        {
            Initialize();
        }

        void MoveInLineOfSight(Unit* who) override

        {
            if (me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                return;

            ScriptedAI::MoveInLineOfSight(who);
        }

        void AttackStart(Unit* who) override
        {
            if (me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                return;

            ScriptedAI::AttackStart(who);
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            Talk(SAY_ROAR_AGGRO);
        }

        void JustReachedHome() override
        {
            me->DespawnOrUnsummon();
        }

        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_ROAR_DEATH);

            SummonCroneIfReady(instance, me);
        }

        void KilledUnit(Unit* /*victim*/) override
        {
            Talk(SAY_ROAR_SLAY);
        }

        void UpdateAI(uint32 diff) override
        {
            if (AggroTimer)
            {
                if (AggroTimer <= diff)
                {
                    me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
                    AggroTimer = 0;
                } else AggroTimer -= diff;
            }

            if (!UpdateVictim())
                return;

            if (MangleTimer <= diff)
            {
                DoCastVictim(SPELL_MANGLE);
                MangleTimer = urand(5000, 8000);
            } else MangleTimer -= diff;

            if (ShredTimer <= diff)
            {
                DoCastVictim(SPELL_SHRED);
                ShredTimer = urand(10000, 15000);
            } else ShredTimer -= diff;

            if (ScreamTimer <= diff)
            {
                DoCastVictim(SPELL_FRIGHTENED_SCREAM);
                ScreamTimer = urand(20000, 30000);
            } else ScreamTimer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

class boss_crone : public CreatureScript
{
public:
    boss_crone() : CreatureScript("boss_crone") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_croneAI>(creature);
    }

    struct boss_croneAI : public ScriptedAI
    {
        boss_croneAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
        }

        void Initialize()
        {
            // Hello, developer from the future! It's me again!
            // This time, you're fixing Karazhan scripts. Awesome. These are a mess of hacks. An amalgamation of hacks, so to speak. Maybe even a Patchwerk thereof.
            // Anyway, I digress.
            // @todo This line below is obviously a hack. Duh. I'm just coming in here to hackfix the encounter to actually be completable.
            // It needs a rewrite. Badly. Please, take good care of it.
            me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            me->SetImmuneToPC(false);
            CycloneTimer = 30000;
            ChainLightningTimer = 10000;
        }

        InstanceScript* instance;

        uint32 CycloneTimer;
        uint32 ChainLightningTimer;

        void Reset() override
        {
            Initialize();
        }

        void JustReachedHome() override
        {
            me->DespawnOrUnsummon();
        }

        void KilledUnit(Unit* /*victim*/) override
        {
           Talk(SAY_CRONE_SLAY);
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            Talk(SAY_CRONE_AGGRO);
        }

        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_CRONE_DEATH);
            instance->SetBossState(DATA_OPERA_PERFORMANCE, DONE);
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            if (CycloneTimer <= diff)
            {
                if (Creature* Cyclone = DoSpawnCreature(CREATURE_CYCLONE, float(urand(0, 9)), float(urand(0, 9)), 0, 0, TEMPSUMMON_TIMED_DESPAWN, 15s))
                    Cyclone->CastSpell(Cyclone, SPELL_CYCLONE_VISUAL, true);
                CycloneTimer = 30000;
            } else CycloneTimer -= diff;

            if (ChainLightningTimer <= diff)
            {
                DoCastVictim(SPELL_CHAIN_LIGHTNING);
                ChainLightningTimer = 15000;
            } else ChainLightningTimer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

class npc_cyclone : public CreatureScript
{
public:
    npc_cyclone() : CreatureScript("npc_cyclone") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<npc_cycloneAI>(creature);
    }

    struct npc_cycloneAI : public ScriptedAI
    {
        npc_cycloneAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            MoveTimer = 1000;
        }

        uint32 MoveTimer;

        void Reset() override
        {
            Initialize();
        }

        void JustEngagedWith(Unit* /*who*/) override { }

        void MoveInLineOfSight(Unit* /*who*/) override

        {
        }

        void UpdateAI(uint32 diff) override
        {
            if (!me->HasAura(SPELL_KNOCKBACK))
                DoCast(me, SPELL_KNOCKBACK, true);

            if (MoveTimer <= diff)
            {
                Position pos = me->GetRandomNearPosition(10);
                me->GetMotionMaster()->MovePoint(0, pos);
                MoveTimer = urand(5000, 8000);
            } else MoveTimer -= diff;
        }
    };
};

/**************************************/
/**** Opera Red Riding Hood Event* ***/
/************************************/
enum RedRidingHood
{
    SAY_WOLF_AGGRO                  = 0,
    SAY_WOLF_SLAY                   = 1,
    SAY_WOLF_HOOD                   = 2,
    OPTION_WHAT_PHAT_LEWTS_YOU_HAVE = 7443,
    SOUND_WOLF_DEATH                = 9275,

    SPELL_LITTLE_RED_RIDING_HOOD    = 30768,
    SPELL_TERRIFYING_HOWL           = 30752,
    SPELL_WIDE_SWIPE                = 30761,

    CREATURE_BIG_BAD_WOLF           = 17521
};

class npc_grandmother : public CreatureScript
{
    public:
        npc_grandmother() : CreatureScript("npc_grandmother") { }

        struct npc_grandmotherAI : public ScriptedAI
        {
            npc_grandmotherAI(Creature* creature) : ScriptedAI(creature) { }

            bool OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId) override
            {
                if (menuId == OPTION_WHAT_PHAT_LEWTS_YOU_HAVE && gossipListId == 0)
                {
                    CloseGossipMenuFor(player);

                    if (Creature* pBigBadWolf = me->SummonCreature(CREATURE_BIG_BAD_WOLF, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation(), TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 2h))
                        pBigBadWolf->AI()->AttackStart(player);

                    me->DespawnOrUnsummon();
                }
                return false;
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetKarazhanAI<npc_grandmotherAI>(creature);
        }
};

class boss_bigbadwolf : public CreatureScript
{
public:
    boss_bigbadwolf() : CreatureScript("boss_bigbadwolf") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_bigbadwolfAI>(creature);
    }

    struct boss_bigbadwolfAI : public ScriptedAI
    {
        boss_bigbadwolfAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
        }

        void Initialize()
        {
            ChaseTimer = 30000;
            FearTimer = urand(25000, 35000);
            SwipeTimer = 5000;

            HoodGUID.Clear();
            TempThreat = 0;

            IsChasing = false;
        }

        InstanceScript* instance;

        uint32 ChaseTimer;
        uint32 FearTimer;
        uint32 SwipeTimer;

        ObjectGuid HoodGUID;
        float TempThreat;

        bool IsChasing;

        void Reset() override
        {
            Initialize();
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            Talk(SAY_WOLF_AGGRO);
        }

        void KilledUnit(Unit* /*victim*/) override
        {
            Talk(SAY_WOLF_SLAY);
        }

        void JustReachedHome() override
        {
            me->DespawnOrUnsummon();
        }

        void JustDied(Unit* /*killer*/) override
        {
            DoPlaySoundToSet(me, SOUND_WOLF_DEATH);
            instance->SetBossState(DATA_OPERA_PERFORMANCE, DONE);
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            DoMeleeAttackIfReady();

            if (ChaseTimer <= diff)
            {
                if (!IsChasing)
                {
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                    {
                        Talk(SAY_WOLF_HOOD);
                        DoCast(target, SPELL_LITTLE_RED_RIDING_HOOD, true);
                        TempThreat = GetThreat(target);
                        if (TempThreat)
                            ModifyThreatByPercent(target, -100);
                        HoodGUID = target->GetGUID();
                        AddThreat(target, 1000000.0f);
                        ChaseTimer = 20000;
                        IsChasing = true;
                    }
                }
                else
                {
                    IsChasing = false;

                    if (Unit* target = ObjectAccessor::GetUnit(*me, HoodGUID))
                    {
                        HoodGUID.Clear();
                        if (GetThreat(target))
                            ModifyThreatByPercent(target, -100);
                        AddThreat(target, TempThreat);
                        TempThreat = 0;
                    }

                    ChaseTimer = 40000;
                }
            } else ChaseTimer -= diff;

            if (IsChasing)
                return;

            if (FearTimer <= diff)
            {
                DoCastVictim(SPELL_TERRIFYING_HOWL);
                FearTimer = urand(25000, 35000);
            } else FearTimer -= diff;

            if (SwipeTimer <= diff)
            {
                DoCastVictim(SPELL_WIDE_SWIPE);
                SwipeTimer = urand(25000, 30000);
            } else SwipeTimer -= diff;
        }
    };
};

/**********************************************/
/******** Opera Romeo and Juliet Event* ******/
/********************************************/

enum JulianneRomulo
{
    /**** Speech *****/
    SAY_JULIANNE_AGGRO              = 0,
    SAY_JULIANNE_ENTER              = 1,
    SAY_JULIANNE_DEATH01            = 2,
    SAY_JULIANNE_DEATH02            = 3,
    SAY_JULIANNE_RESURRECT          = 4,
    SAY_JULIANNE_SLAY               = 5,

    SAY_ROMULO_AGGRO                = 0,
    SAY_ROMULO_DEATH                = 1,
    SAY_ROMULO_ENTER                = 2,
    SAY_ROMULO_RESURRECT            = 3,
    SAY_ROMULO_SLAY                 = 4,

    SPELL_BLINDING_PASSION          = 30890,
    SPELL_DEVOTION                  = 30887,
    SPELL_ETERNAL_AFFECTION         = 30878,
    SPELL_POWERFUL_ATTRACTION       = 30889,
    SPELL_DRINK_POISON              = 30907,

    SPELL_BACKWARD_LUNGE            = 30815,
    SPELL_DARING                    = 30841,
    SPELL_DEADLY_SWATHE             = 30817,
    SPELL_POISON_THRUST             = 30822,

    SPELL_UNDYING_LOVE              = 30951,
    SPELL_RES_VISUAL                = 24171,

    CREATURE_ROMULO                 = 17533,
    ROMULO_X                        = -10900,
    ROMULO_Y                        = -1758,
};

enum RAJPhase
{
    PHASE_JULIANNE      = 0,
    PHASE_ROMULO        = 1,
    PHASE_BOTH          = 2,
};

void PretendToDie(Creature* creature)
{
    creature->InterruptNonMeleeSpells(true);
    creature->RemoveAllAuras();
    creature->SetHealth(0);
    creature->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
    creature->GetMotionMaster()->Clear();
    creature->GetMotionMaster()->MoveIdle();
    creature->SetStandState(UNIT_STAND_STATE_DEAD);
}

void Resurrect(Creature* target)
{
    target->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
    target->SetFullHealth();
    target->SetStandState(UNIT_STAND_STATE_STAND);
    target->CastSpell(target, SPELL_RES_VISUAL, true);
    if (target->GetVictim())
    {
        target->GetMotionMaster()->MoveChase(target->GetVictim());
        target->AI()->AttackStart(target->GetVictim());
    }
        else
            target->GetMotionMaster()->Initialize();
}

class boss_julianne : public CreatureScript
{
public:
    boss_julianne() : CreatureScript("boss_julianne") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_julianneAI>(creature);
    }

    struct boss_julianneAI : public ScriptedAI
    {
        boss_julianneAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
            EntryYellTimer = 1000;
            AggroYellTimer = 10000;
            IsFakingDeath = false;
            ResurrectTimer = 0;
        }

        void Initialize()
        {
            RomuloGUID.Clear();
            Phase = PHASE_JULIANNE;

            BlindingPassionTimer = 30000;
            DevotionTimer = 15000;
            EternalAffectionTimer = 25000;
            PowerfulAttractionTimer = 5000;
            SummonRomuloTimer = 10000;
            DrinkPoisonTimer = 0;
            ResurrectSelfTimer = 0;

            SummonedRomulo = false;
            RomuloDead = false;
        }

        InstanceScript* instance;

        uint32 EntryYellTimer;
        uint32 AggroYellTimer;

        ObjectGuid RomuloGUID;

        uint32 Phase;

        uint32 BlindingPassionTimer;
        uint32 DevotionTimer;
        uint32 EternalAffectionTimer;
        uint32 PowerfulAttractionTimer;
        uint32 SummonRomuloTimer;
        uint32 ResurrectTimer;
        uint32 DrinkPoisonTimer;
        uint32 ResurrectSelfTimer;

        bool IsFakingDeath;
        bool SummonedRomulo;
        bool RomuloDead;

        void Reset() override
        {
            Initialize();
            if (IsFakingDeath)
            {
                Resurrect(me);
                IsFakingDeath = false;
            }
        }

        void JustEngagedWith(Unit* /*who*/) override { }

        void AttackStart(Unit* who) override
        {
            if (me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                return;

            ScriptedAI::AttackStart(who);
        }

        void MoveInLineOfSight(Unit* who) override

        {
            if (me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                return;

            ScriptedAI::MoveInLineOfSight(who);
        }

        void JustReachedHome() override
        {
            me->DespawnOrUnsummon();
        }

        void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
        {
            if (spellInfo->Id == SPELL_DRINK_POISON)
            {
                Talk(SAY_JULIANNE_DEATH01);
                DrinkPoisonTimer = 2500;
            }
        }

        void DamageTaken(Unit* /*done_by*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override;

        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_JULIANNE_DEATH02);
            instance->SetBossState(DATA_OPERA_PERFORMANCE, DONE);
        }

        void KilledUnit(Unit* /*victim*/) override
        {
           Talk(SAY_JULIANNE_SLAY);
        }

        void UpdateAI(uint32 diff) override;
    };
};

class boss_romulo : public CreatureScript
{
public:
    boss_romulo() : CreatureScript("boss_romulo") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_romuloAI>(creature);
    }

    struct boss_romuloAI : public ScriptedAI
    {
        boss_romuloAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
            EntryYellTimer = 8000;
            AggroYellTimer = 15000;
        }

        void Initialize()
        {
            JulianneGUID.Clear();
            Phase = PHASE_ROMULO;

            BackwardLungeTimer = 15000;
            DaringTimer = 20000;
            DeadlySwatheTimer = 25000;
            PoisonThrustTimer = 10000;
            ResurrectTimer = 10000;

            IsFakingDeath = false;
            JulianneDead = false;
        }

        InstanceScript* instance;

        ObjectGuid JulianneGUID;
        uint32 Phase;

        uint32 EntryYellTimer;
        uint32 AggroYellTimer;
        uint32 BackwardLungeTimer;
        uint32 DaringTimer;
        uint32 DeadlySwatheTimer;
        uint32 PoisonThrustTimer;
        uint32 ResurrectTimer;

        bool IsFakingDeath;
        bool JulianneDead;

        void Reset() override
        {
            Initialize();
        }

        void JustReachedHome() override
        {
            me->DespawnOrUnsummon();
        }

        void DamageTaken(Unit* /*done_by*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
        {
            if (damage < me->GetHealth())
                return;

            //anything below only used if incoming damage will kill

            if (Phase == PHASE_ROMULO)
            {
                Talk(SAY_ROMULO_DEATH);
                PretendToDie(me);
                IsFakingDeath = true;
                Phase = PHASE_BOTH;

                if (Creature* Julianne = (ObjectAccessor::GetCreature((*me), JulianneGUID)))
                {
                    ENSURE_AI(boss_julianne::boss_julianneAI, Julianne->AI())->RomuloDead = true;
                    ENSURE_AI(boss_julianne::boss_julianneAI, Julianne->AI())->ResurrectSelfTimer = 10000;
                }

                damage = 0;
                return;
            }

            if (Phase == PHASE_BOTH)
            {
                if (JulianneDead)
                {
                    if (Creature* Julianne = (ObjectAccessor::GetCreature((*me), JulianneGUID)))
                    {
                        Julianne->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                        Julianne->GetMotionMaster()->Clear();
                        Julianne->setDeathState(JUST_DIED);
                        Julianne->CombatStop(true);
                        Julianne->ReplaceAllDynamicFlags(UNIT_DYNFLAG_LOOTABLE);
                    }
                    return;
                }

                if (Creature* Julianne = (ObjectAccessor::GetCreature((*me), JulianneGUID)))
                {
                    PretendToDie(me);
                    IsFakingDeath = true;
                    ENSURE_AI(boss_julianne::boss_julianneAI, Julianne->AI())->ResurrectTimer = 10000;
                    ENSURE_AI(boss_julianne::boss_julianneAI, Julianne->AI())->RomuloDead = true;
                    damage = 0;
                    return;
                }
            }

            TC_LOG_ERROR("scripts", "boss_romuloAI: DamageTaken reach end of code, that should not happen.");
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            Talk(SAY_ROMULO_AGGRO);
            if (JulianneGUID)
            {
                Creature* Julianne = (ObjectAccessor::GetCreature((*me), JulianneGUID));
                if (Julianne && Julianne->GetVictim())
                {
                    AddThreat(Julianne->GetVictim(), 1.0f);
                    AttackStart(Julianne->GetVictim());
                }
            }
        }

        void MoveInLineOfSight(Unit* who) override

        {
            if (me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                return;

            ScriptedAI::MoveInLineOfSight(who);
        }

        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_ROMULO_DEATH);
            instance->SetBossState(DATA_OPERA_PERFORMANCE, DONE);
        }

        void KilledUnit(Unit* /*victim*/) override
        {
            Talk(SAY_ROMULO_SLAY);
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim() || IsFakingDeath)
                return;

            if (JulianneDead)
            {
                if (ResurrectTimer <= diff)
                {
                    Creature* Julianne = (ObjectAccessor::GetCreature((*me), JulianneGUID));
                    if (Julianne && ENSURE_AI(boss_julianne::boss_julianneAI, Julianne->AI())->IsFakingDeath)
                    {
                        Talk(SAY_ROMULO_RESURRECT);
                        Resurrect(Julianne);
                        ENSURE_AI(boss_julianne::boss_julianneAI, Julianne->AI())->IsFakingDeath = false;
                        JulianneDead = false;
                        ResurrectTimer = 10000;
                    }
                } else ResurrectTimer -= diff;
            }

            if (BackwardLungeTimer <= diff)
            {
                Unit* target = SelectTarget(SelectTargetMethod::Random, 1, 100, true);
                if (target && !me->HasInArc(float(M_PI), target))
                {
                    DoCast(target, SPELL_BACKWARD_LUNGE);
                    BackwardLungeTimer = urand(15000, 30000);
                }
            } else BackwardLungeTimer -= diff;

            if (DaringTimer <= diff)
            {
                DoCast(me, SPELL_DARING);
                DaringTimer = urand(20000, 40000);
            } else DaringTimer -= diff;

            if (DeadlySwatheTimer <= diff)
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                    DoCast(target, SPELL_DEADLY_SWATHE);
                DeadlySwatheTimer = urand(15000, 25000);
            } else DeadlySwatheTimer -= diff;

            if (PoisonThrustTimer <= diff)
            {
                DoCastVictim(SPELL_POISON_THRUST);
                PoisonThrustTimer = urand(10000, 20000);
            } else PoisonThrustTimer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

void boss_julianne::boss_julianneAI::UpdateAI(uint32 diff)
{
    if (EntryYellTimer)
    {
        if (EntryYellTimer <= diff)
        {
            Talk(SAY_JULIANNE_ENTER);
            EntryYellTimer = 0;
        } else EntryYellTimer -= diff;
    }

    if (AggroYellTimer)
    {
        if (AggroYellTimer <= diff)
        {
            Talk(SAY_JULIANNE_AGGRO);
            me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
            me->SetFaction(FACTION_MONSTER_2);
            AggroYellTimer = 0;
        } else AggroYellTimer -= diff;
    }

    if (DrinkPoisonTimer)
    {
        //will do this 2secs after spell hit. this is time to display visual as expected
        if (DrinkPoisonTimer <= diff)
        {
            PretendToDie(me);
            Phase = PHASE_ROMULO;
            SummonRomuloTimer = 10000;
            DrinkPoisonTimer = 0;
        } else DrinkPoisonTimer -= diff;
    }

    if (Phase == PHASE_ROMULO && !SummonedRomulo)
    {
        if (SummonRomuloTimer <= diff)
        {
            if (Creature* pRomulo = me->SummonCreature(CREATURE_ROMULO, ROMULO_X, ROMULO_Y, me->GetPositionZ(), 0, TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 2h))
            {
                RomuloGUID = pRomulo->GetGUID();
                ENSURE_AI(boss_romulo::boss_romuloAI, pRomulo->AI())->JulianneGUID = me->GetGUID();
                ENSURE_AI(boss_romulo::boss_romuloAI, pRomulo->AI())->Phase = PHASE_ROMULO;
                DoZoneInCombat(pRomulo);

                pRomulo->SetFaction(FACTION_MONSTER_2);
            }
            SummonedRomulo = true;
        } else SummonRomuloTimer -= diff;
    }

    if (ResurrectSelfTimer)
    {
        if (ResurrectSelfTimer <= diff)
        {
            Resurrect(me);
            Phase = PHASE_BOTH;
            IsFakingDeath = false;

            if (me->GetVictim())
                AttackStart(me->GetVictim());

            ResurrectSelfTimer = 0;
            ResurrectTimer = 1000;
        } else ResurrectSelfTimer -= diff;
    }

    if (!UpdateVictim() || IsFakingDeath)
        return;

    if (RomuloDead)
    {
        if (ResurrectTimer <= diff)
        {
            Creature* Romulo = (ObjectAccessor::GetCreature((*me), RomuloGUID));
            if (Romulo && ENSURE_AI(boss_romulo::boss_romuloAI, Romulo->AI())->IsFakingDeath)
            {
                Talk(SAY_JULIANNE_RESURRECT);
                Resurrect(Romulo);
                ENSURE_AI(boss_romulo::boss_romuloAI, Romulo->AI())->IsFakingDeath = false;
                RomuloDead = false;
                ResurrectTimer = 10000;
            }
        } else ResurrectTimer -= diff;
    }

    if (BlindingPassionTimer <= diff)
    {
        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
            DoCast(target, SPELL_BLINDING_PASSION);
        BlindingPassionTimer = urand(30000, 45000);
    } else BlindingPassionTimer -= diff;

    if (DevotionTimer <= diff)
    {
        DoCast(me, SPELL_DEVOTION);
        DevotionTimer = urand(15000, 45000);
    } else DevotionTimer -= diff;

    if (PowerfulAttractionTimer <= diff)
    {
        DoCast(SelectTarget(SelectTargetMethod::Random, 0), SPELL_POWERFUL_ATTRACTION);
        PowerfulAttractionTimer = urand(5000, 30000);
    } else PowerfulAttractionTimer -= diff;

    if (EternalAffectionTimer <= diff)
    {
        if (urand(0, 1) && SummonedRomulo)
        {
            Creature* Romulo = (ObjectAccessor::GetCreature((*me), RomuloGUID));
            if (Romulo && Romulo->IsAlive() && !RomuloDead)
                DoCast(Romulo, SPELL_ETERNAL_AFFECTION);
        } else DoCast(me, SPELL_ETERNAL_AFFECTION);

        EternalAffectionTimer = urand(45000, 60000);
    } else EternalAffectionTimer -= diff;

    DoMeleeAttackIfReady();
}

void boss_julianne::boss_julianneAI::DamageTaken(Unit* /*done_by*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/)
{
    if (damage < me->GetHealth())
        return;

    //anything below only used if incoming damage will kill

    if (Phase == PHASE_JULIANNE)
    {
        damage = 0;

        //this means already drinking, so return
        if (IsFakingDeath)
            return;

        me->InterruptNonMeleeSpells(true);
        DoCast(me, SPELL_DRINK_POISON);

        IsFakingDeath = true;
        //IS THIS USEFULL? Creature* Julianne = (ObjectAccessor::GetCreature((*me), JulianneGUID));
        return;
    }

    if (Phase == PHASE_ROMULO)
    {
        TC_LOG_ERROR("scripts", "boss_julianneAI: cannot take damage in PHASE_ROMULO, why was i here?");
        damage = 0;
        return;
    }

    if (Phase == PHASE_BOTH)
    {
        //if this is true then we have to kill romulo too
        if (RomuloDead)
        {
            if (Creature* Romulo = (ObjectAccessor::GetCreature((*me), RomuloGUID)))
            {
                Romulo->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                Romulo->GetMotionMaster()->Clear();
                Romulo->setDeathState(JUST_DIED);
                Romulo->CombatStop(true);
                Romulo->ReplaceAllDynamicFlags(UNIT_DYNFLAG_LOOTABLE);
            }

            return;
        }

        //if not already returned, then romulo is alive and we can pretend die
        if (Creature* Romulo = (ObjectAccessor::GetCreature((*me), RomuloGUID)))
        {
            PretendToDie(me);
            IsFakingDeath = true;
            ENSURE_AI(boss_romulo::boss_romuloAI, Romulo->AI())->ResurrectTimer = 10000;
            ENSURE_AI(boss_romulo::boss_romuloAI, Romulo->AI())->JulianneDead = true;
            damage = 0;
            return;
        }
    }
    TC_LOG_ERROR("scripts", "boss_julianneAI: DamageTaken reach end of code, that should not happen.");
}

void AddSC_bosses_opera()
{
    new boss_dorothee();
    new boss_strawman();
    new boss_tinhead();
    new boss_roar();
    new boss_crone();
    new npc_tito();
    new npc_cyclone();
    new npc_grandmother();
    new boss_bigbadwolf();
    new boss_julianne();
    new boss_romulo();
}
