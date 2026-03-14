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
 * @file boss_halazzi.cpp
 * @brief 祖阿曼副本 - 哈拉兹Boss脚本模块
 *
 * 本模块实现了哈拉兹Boss的战斗逻辑，包括：
 * - 多阶段形态转换（山猫形态、人形态、分裂形态）
 * - 猛兽突袭技能（高伤害技能，需要分摊）
 * - 狂暴机制
 * - 召唤灵魂山猫
 * - 召唤图腾
 *
 * 哈拉兹是祖阿曼的第三个Boss，是一个山猫之神化身。
 * 战斗分为多个阶段，Boss会在山猫和人形态之间转换。
 *
 * 战斗机制：
 * - 山猫形态：使用猛兽突袭（需要2个坦克分摊）和狂乱
 * - 分裂阶段：每25%血量触发一次，分离出灵魂山猫
 * - 人形态：Boss使用图腾和冲击技能，灵魂山猫独立战斗
 * - 合并阶段：Boss与灵魂山猫合并，恢复山猫形态
 * - 狂暴阶段：3次分裂后进入最终狂暴阶段
 */

#include "ScriptMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "zulaman.h"

/**
 * @brief 技能枚举
 *
 * 定义哈拉兹使用的所有技能ID
 */
enum Spells
{
    SPELL_DUAL_WIELD            = 29651,  ///< 双持 - 被动技能
    SPELL_SABER_LASH            = 43267,  ///< 猛兽突袭 - 对前方目标造成大量伤害，需要分摊
    SPELL_FRENZY                = 43139,  ///< 狂乱 - 提高攻击速度
    SPELL_FLAMESHOCK            = 43303,  ///< 火焰冲击 - 持续火焰伤害
    SPELL_EARTHSHOCK            = 43305,  ///< 大地冲击 - 打断施法
    SPELL_TRANSFORM_SPLIT       = 43142,  ///< 分裂变形 - 分裂出灵魂山猫
    SPELL_TRANSFORM_SPLIT2      = 43573,  ///< 分裂变形2 - 触发人形态
    SPELL_TRANSFORM_MERGE       = 43271,  ///< 合并变形 - 合并回山猫形态
    SPELL_SUMMON_LYNX           = 43143,  ///< 召唤山猫
    SPELL_SUMMON_TOTEM          = 43302,  ///< 召唤图腾
    SPELL_BERSERK               = 45078,  ///< 狂暴 - 10分钟后进入狂暴
    SPELL_LYNX_FRENZY           = 43290,  ///< 山猫狂乱 - 灵魂山猫使用的技能
    SPELL_SHRED_ARMOR           = 43243   ///< 撕裂护甲 - 灵魂山猫使用的技能
};

/**
 * @brief 生物ID枚举
 *
 * 定义哈拉兹战斗中涉及的NPC ID
 */
enum Hal_CreatureIds
{
    NPC_SPIRIT_LYNX             = 24143,  ///< 灵魂山猫NPC ID
    NPC_TOTEM                   = 24224   ///< 图腾NPC ID
};

/**
 * @brief 战斗阶段枚举
 *
 * 定义哈拉兹战斗的各个阶段
 */
enum PhaseHalazzi
{
    PHASE_NONE                  = 0,  ///< 无阶段
    PHASE_LYNX                  = 1,  ///< 山猫形态阶段（主阶段）
    PHASE_SPLIT                 = 2,  ///< 分裂阶段（正在分裂）
    PHASE_HUMAN                 = 3,  ///< 人形态阶段（灵魂山猫分离）
    PHASE_MERGE                 = 4,  ///< 合并阶段（正在合并）
    PHASE_ENRAGE                = 5   ///< 狂暴阶段（最终阶段）
};

/**
 * @brief 对话和喊话枚举
 *
 * 定义哈拉兹的各种对话ID
 */
enum Yells
{
    SAY_AGGRO                   = 0,  ///< 开战喊话
    SAY_SABER                   = 1,  ///< 猛兽突袭喊话（未使用）
    SAY_SPLIT                   = 2,  ///< 分裂喊话
    SAY_MERGE                   = 3,  ///< 合并喊话
    SAY_KILL                    = 4,  ///< 击杀玩家
    SAY_DEATH                   = 5,  ///< 死亡喊话
    SAY_BERSERK                 = 6   ///< 狂暴喊话
};

/**
 * @brief 哈拉兹Boss脚本类
 *
 * 实现哈拉兹的完整战斗逻辑，包括多阶段形态转换
 */
class boss_halazzi : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        boss_halazzi() : CreatureScript("boss_halazzi") { }

        /**
         * @brief 哈拉兹AI类
         *
         * 继承自BossAI，实现哈拉兹的多阶段战斗行为
         */
        struct boss_halazziAI : public BossAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            boss_halazziAI(Creature* creature) : BossAI(creature, BOSS_HALAZZI)
            {
                Initialize();
                Phase = PHASE_NONE;
                FrenzyTimer = 0;
                SaberlashTimer = 0;
                ShockTimer = 0;
                TotemTimer = 0;
            }

            /**
             * @brief 初始化所有变量
             */
            void Initialize()
            {
                LynxGUID.Clear();
                TransformCount = 0;      // 变形次数计数器
                BerserkTimer = 600000;   // 狂暴计时器：10分钟
                CheckTimer = 1000;       // 检查计时器：1秒
            }

            PhaseHalazzi Phase;  ///< 当前战斗阶段

            // 技能计时器
            uint32 FrenzyTimer;      ///< 狂乱冷却计时器
            uint32 SaberlashTimer;   ///< 猛兽突袭冷却计时器
            uint32 ShockTimer;       ///< 冲击技能冷却计时器
            uint32 TotemTimer;       ///< 图腾冷却计时器
            uint32 CheckTimer;       ///< 阶段检查计时器
            uint32 BerserkTimer;     ///< 狂暴计时器
            uint32 TransformCount;   ///< 变形次数（影响血量计算）

            ObjectGuid LynxGUID;     ///< 灵魂山猫的GUID

            /**
             * @brief 重置Boss状态
             *
             * 当Boss脱离战斗或重置时调用
             */
            void Reset() override
            {
                _Reset();
                Initialize();

                // 施放双持被动技能
                DoCast(me, SPELL_DUAL_WIELD, true);

                Phase = PHASE_NONE;
                EnterPhase(PHASE_LYNX);  // 进入山猫形态
            }

            /**
             * @brief 进入战斗
             * @param who 进入战斗的目标
             */
            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);
                Talk(SAY_AGGRO);
                EnterPhase(PHASE_LYNX);
            }

            /**
             * @brief 召唤单位处理
             * @param summon 被召唤的单位
             *
             * 当Boss召唤单位时调用，设置召唤物攻击目标
             */
            void JustSummoned(Creature* summon) override
            {
                summon->AI()->AttackStart(me->GetVictim());
                if (summon->GetEntry() == NPC_SPIRIT_LYNX)
                    LynxGUID = summon->GetGUID();  // 保存灵魂山猫GUID
                summons.Summon(summon);
            }

            /**
             * @brief 伤害处理
             * @param done_by 伤害来源（未使用）
             * @param damage 伤害值（引用传递，可修改）
             * @param damageType 伤害类型（未使用）
             * @param spellInfo 技能信息（未使用）
             *
             * 防止Boss在非狂暴阶段死亡
             * 将致命伤害设为0，保持Boss存活直到进入狂暴阶段
             */
            void DamageTaken(Unit* /*done_by*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                if (damage >= me->GetHealth() && Phase != PHASE_ENRAGE)
                    damage = 0;  // 不在狂暴阶段时阻止致命伤害
            }

            /**
             * @brief 技能命中处理
             * @param caster 施法者（未使用）
             * @param spellInfo 技能信息
             *
             * 当技能命中Boss时触发
             * 检测分裂变形2技能，进入人形态阶段
             */
            void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
            {
                if (spellInfo->Id == SPELL_TRANSFORM_SPLIT2)
                    EnterPhase(PHASE_HUMAN);
            }

            /**
             * @brief 攻击开始
             * @param who 攻击目标
             *
             * 在合并阶段不执行攻击
             */
            void AttackStart(Unit* who) override
            {
                if (Phase != PHASE_MERGE)
                    ScriptedAI::AttackStart(who);
            }

            /**
             * @brief 进入指定阶段
             * @param NextPhase 下一个阶段
             *
             * 处理阶段转换逻辑，包括：
             * - 山猫形态：主战斗形态，使用猛兽突袭和狂乱
             * - 分裂阶段：分离出灵魂山猫
             * - 人形态：Boss变成人形态，使用图腾和冲击技能
             * - 合并阶段：Boss和灵魂山猫合并
             * - 狂暴阶段：最终阶段，使用所有技能
             *
             * @note 每次分裂后Boss的最大血量会减少
             */
            void EnterPhase(PhaseHalazzi NextPhase)
            {
                switch (NextPhase)
                {
                    case PHASE_LYNX:
                    case PHASE_ENRAGE:
                        if (Phase == PHASE_MERGE)
                        {
                            // 从合并阶段恢复山猫形态
                            DoCast(me, SPELL_TRANSFORM_MERGE, true);
                            me->Attack(me->GetVictim(), true);
                            me->GetMotionMaster()->MoveChase(me->GetVictim());
                        }
                        // 清除已存在的灵魂山猫
                        if (Creature* Lynx = ObjectAccessor::GetCreature(*me, LynxGUID))
                            Lynx->DisappearAndDie();
                        // 每次分裂后血量减少150000
                        me->SetMaxHealth(600000);
                        me->SetHealth(600000 - 150000 * TransformCount);
                        // 重置技能计时器
                        FrenzyTimer = 16000;
                        SaberlashTimer = 20000;
                        ShockTimer = 10000;
                        TotemTimer = 12000;
                        break;
                    case PHASE_SPLIT:
                        // 分裂阶段：施放分裂变形技能
                        Talk(SAY_SPLIT);
                        DoCast(me, SPELL_TRANSFORM_SPLIT, true);
                        break;
                    case PHASE_HUMAN:
                        // 人形态：召唤灵魂山猫
                        //DoCast(me, SPELL_SUMMON_LYNX, true);
                        DoSpawnCreature(NPC_SPIRIT_LYNX, 5, 5, 0, 0, TEMPSUMMON_CORPSE_DESPAWN, 0s);
                        me->SetMaxHealth(400000);
                        me->SetHealth(400000);
                        ShockTimer = 10000;
                        TotemTimer = 12000;
                        break;
                    case PHASE_MERGE:
                        // 合并阶段：Boss和灵魂山猫相互靠近
                        if (Unit* pLynx = ObjectAccessor::GetUnit(*me, LynxGUID))
                        {
                            Talk(SAY_MERGE);
                            pLynx->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);  // 灵魂山猫变为不可攻击
                            pLynx->GetMotionMaster()->Clear();
                            pLynx->GetMotionMaster()->MoveFollow(me, 0, 0);  // 跟随Boss
                            me->GetMotionMaster()->Clear();
                            me->GetMotionMaster()->MoveFollow(pLynx, 0, 0);  // Boss跟随灵魂山猫
                            ++TransformCount;  // 增加变形次数
                        }
                        break;
                    default:
                        break;
                }
                Phase = NextPhase;
            }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 主更新函数，根据当前阶段执行不同的战斗逻辑
             *
             * 阶段转换条件：
             * - 山猫形态：血量低于特定百分比（75%、50%、25%）时分裂
             * - 人形态：Boss或灵魂山猫血量低于20%时合并
             * - 合并阶段：Boss和灵魂山猫距离小于6码时完成合并
             * - 最终阶段：分裂3次后进入狂暴阶段
             */
            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                // 狂暴计时器
                if (BerserkTimer <= diff)
                {
                    Talk(SAY_BERSERK);
                    DoCast(me, SPELL_BERSERK, true);
                    BerserkTimer = 60000;  // 狂暴后每60秒再次施放
                }
                else
                    BerserkTimer -= diff;

                // 山猫形态或狂暴阶段
                if (Phase == PHASE_LYNX || Phase == PHASE_ENRAGE)
                {
                    // 猛兽突袭：高伤害技能，需要坦克分摊
                    if (SaberlashTimer <= diff)
                    {
                        // 防御技能490以上的坦克应该不会被暴击
                        //DoCast(me, 41296, true);
                        DoCastVictim(SPELL_SABER_LASH, true);
                        //me->RemoveAurasDueToSpell(41296);
                        SaberlashTimer = 30000;
                    }
                    else
                        SaberlashTimer -= diff;

                    // 狂乱：提高攻击速度
                    if (FrenzyTimer <= diff)
                    {
                        DoCast(me, SPELL_FRENZY);
                        FrenzyTimer = urand(10000, 15000);
                    }
                    else
                        FrenzyTimer -= diff;

                    // 山猫形态下的阶段转换检查
                    if (Phase == PHASE_LYNX)
                    {
                        if (CheckTimer <= diff)
                        {
                            // 根据变形次数计算分裂血量阈值
                            // 第1次分裂：75%，第2次：50%，第3次：25%
                            if (HealthBelowPct(25 * (3 - TransformCount)))
                                EnterPhase(PHASE_SPLIT);
                            CheckTimer = 1000;
                        }
                        else
                            CheckTimer -= diff;
                    }
                }

                // 人形态或狂暴阶段
                if (Phase == PHASE_HUMAN || Phase == PHASE_ENRAGE)
                {
                    // 召唤图腾
                    if (TotemTimer <= diff)
                    {
                        DoCast(me, SPELL_SUMMON_TOTEM);
                        TotemTimer = 20000;
                    }
                    else
                        TotemTimer -= diff;

                    // 冲击技能：根据目标施法状态选择火焰冲击或大地冲击
                    if (ShockTimer <= diff)
                    {
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        {
                            // 如果目标正在施法，使用大地冲击打断
                            if (target->IsNonMeleeSpellCast(false))
                                DoCast(target, SPELL_EARTHSHOCK);
                            else
                                DoCast(target, SPELL_FLAMESHOCK);
                            ShockTimer = urand(10000, 15000);
                        }
                    }
                    else
                        ShockTimer -= diff;

                    // 人形态下的合并检查
                    if (Phase == PHASE_HUMAN)
                    {
                        if (CheckTimer <= diff)
                        {
                            // Boss或灵魂山猫血量低于20%时合并
                            if (!HealthAbovePct(20))
                                EnterPhase(PHASE_MERGE);
                            else
                            {
                                Unit* Lynx = ObjectAccessor::GetUnit(*me, LynxGUID);
                                if (Lynx && !Lynx->HealthAbovePct(20))
                                    EnterPhase(PHASE_MERGE);
                            }
                            CheckTimer = 1000;
                        }
                        else
                            CheckTimer -= diff;
                    }
                }

                // 合并阶段
                if (Phase == PHASE_MERGE)
                {
                    if (CheckTimer <= diff)
                    {
                        Unit* Lynx = ObjectAccessor::GetUnit(*me, LynxGUID);
                        if (Lynx)
                        {
                            // 双方相互靠近
                            Lynx->GetMotionMaster()->MoveFollow(me, 0, 0);
                            me->GetMotionMaster()->MoveFollow(Lynx, 0, 0);
                            // 距离小于6码时完成合并
                            if (me->IsWithinDistInMap(Lynx, 6.0f))
                            {
                                if (TransformCount < 3)
                                    EnterPhase(PHASE_LYNX);   // 继续分裂循环
                                else
                                    EnterPhase(PHASE_ENRAGE); // 3次分裂后进入狂暴阶段
                            }
                        }
                        CheckTimer = 1000;
                    }
                    else
                        CheckTimer -= diff;
                }

                DoMeleeAttackIfReady();
            }

            /**
             * @brief 击杀单位
             * @param victim 被击杀的单位
             */
            void KilledUnit(Unit* victim) override
            {
                if (victim->GetTypeId() != TYPEID_PLAYER)
                    return;

                Talk(SAY_KILL);
            }

            /**
             * @brief Boss死亡
             * @param killer 击杀者（未使用）
             */
            void JustDied(Unit* /*killer*/) override
            {
                _JustDied();
                Talk(SAY_DEATH);
            }
        };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象指针
         * @return 返回哈拉兹AI实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetZulAmanAI<boss_halazziAI>(creature);
        }
};

/**
 * @brief 灵魂山猫NPC脚本类
 *
 * 实现灵魂山猫的行为逻辑
 * 灵魂山猫在人形态阶段与Boss分离，独立攻击玩家
 */
class npc_halazzi_lynx : public CreatureScript
{
    public:
        /**
         * @brief 构造函数
         */
        npc_halazzi_lynx() : CreatureScript("npc_halazzi_lynx") { }

        /**
         * @brief 灵魂山猫AI类
         *
         * 继承自ScriptedAI，实现灵魂山猫的攻击行为
         */
        struct npc_halazzi_lynxAI : public ScriptedAI
        {
            /**
             * @brief 构造函数
             * @param creature 生物对象指针
             */
            npc_halazzi_lynxAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
            }

            /**
             * @brief 初始化计时器
             */
            void Initialize()
            {
                FrenzyTimer = urand(30000, 50000);  // 狂乱冷却：30-50秒
                shredder_timer = 4000;               // 撕裂护甲冷却：4秒
            }

            uint32 FrenzyTimer;     ///< 狂乱计时器
            uint32 shredder_timer;  ///< 撕裂护甲计时器

            /**
             * @brief 重置状态
             */
            void Reset() override
            {
                Initialize();
            }

            /**
             * @brief 伤害处理
             * @param done_by 伤害来源（未使用）
             * @param damage 伤害值（引用传递，可修改）
             * @param damageType 伤害类型（未使用）
             * @param spellInfo 技能信息（未使用）
             *
             * 防止灵魂山猫真正死亡
             * 灵魂山猫在人形态阶段被"杀死"时触发合并
             */
            void DamageTaken(Unit* /*done_by*/, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
            {
                if (damage >= me->GetHealth())
                    damage = 0;  // 阻止致命伤害
            }

            /**
             * @brief 攻击开始
             * @param who 攻击目标
             *
             * 在不可攻击状态下不执行攻击
             */
            void AttackStart(Unit* who) override
            {
                if (!me->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE))
                    ScriptedAI::AttackStart(who);
            }

            /**
             * @brief 进入战斗
             * @param who 进入战斗的目标（未使用）
             */
            void JustEngagedWith(Unit* /*who*/) override {/*DoZoneInCombat();*/ }

            /**
             * @brief 更新AI
             * @param diff 距离上次更新的时间间隔（毫秒）
             *
             * 灵魂山猫的攻击逻辑：
             * - 定期施放狂乱提高攻击速度
             * - 频繁施放撕裂护甲降低坦克防御
             */
            void UpdateAI(uint32 diff) override
            {
                if (!UpdateVictim())
                    return;

                // 狂乱：提高攻击速度
                if (FrenzyTimer <= diff)
                {
                    DoCast(me, SPELL_LYNX_FRENZY);
                    FrenzyTimer = urand(30000, 50000);
                } else FrenzyTimer -= diff;

                // 撕裂护甲：降低目标护甲
                if (shredder_timer <= diff)
                {
                    DoCastVictim(SPELL_SHRED_ARMOR);
                    shredder_timer = 4000;
                } else shredder_timer -= diff;

                DoMeleeAttackIfReady();
            }

        };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象指针
         * @return 返回灵魂山猫AI实例
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetZulAmanAI<npc_halazzi_lynxAI>(creature);
        }
};

/**
 * @brief 注册哈拉兹Boss脚本
 *
 * 将哈拉兹Boss和灵魂山猫脚本注册到脚本系统中
 */
void AddSC_boss_halazzi()
{
    new boss_halazzi();
    new npc_halazzi_lynx();
}
