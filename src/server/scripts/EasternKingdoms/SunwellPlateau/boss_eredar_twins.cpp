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
 * @file boss_eredar_twins.cpp
 * @brief 太阳井高地 - 埃雷达尔双子（Eredar Twins）BOSS脚本
 *
 * 本模块实现了太阳井高地副本中的埃雷达尔双子BOSS战斗逻辑。
 * 该BOSS战包含两个协作的BOSS：
 * - 萨克拉什女士（Lady Sacrolash）：暗影属性BOSS，近战攻击者
 * - 阿莉耶丝女士（Grand Warlock Alythess）：火焰属性BOSS，远程施法者
 *
 * 战斗机制特点：
 * 1. 双子共享仇恨列表，需同时击败
 * 2. 火焰/暗影触染机制：玩家被火焰或暗影技能击中后会获得相应debuff
 *    被另一种属性技能击中时会移除debuff并转化为"黑暗烈焰"（受到双倍伤害）
 * 3. 当一方死亡后，另一方会获得"强化"增益，继承对方的技能
 * 4. 暗影影像是萨克拉什召唤的小怪，会攻击随机玩家
 *
 * @see https://wowpedia.fandom.com/wiki/Eredar_Twins
 */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "sunwell_plateau.h"

/**
 * @enum Quotes
 * @brief BOSS台词和表情枚举
 *
 * 定义了萨克拉什和阿莉耶丝在战斗中使用的各种台词和表情
 */
enum Quotes
{
    // 萨克拉什女士的台词
    YELL_INTRO_SAC_1            = 0,  ///< 介绍台词1
    YELL_INTRO_SAC_3            = 1,  ///< 介绍台词3
    YELL_INTRO_SAC_5            = 2,  ///< 介绍台词5
    YELL_INTRO_SAC_7            = 3,  ///< 介绍台词7
    YELL_SAC_DEAD               = 4,  ///< 萨克拉什死亡台词
    EMOTE_SHADOW_NOVA           = 5,  ///< 暗影新星表情
    YELL_ENRAGE                 = 6,  ///< 狂暴台词
    YELL_SISTER_ALYTHESS_DEAD   = 7,  ///< 姐妹阿莉耶丝死亡台词
    YELL_SAC_KILL               = 8,  ///< 萨克拉什击杀玩家台词
    YELL_SHADOW_NOVA            = 9,  ///< 暗影新星台词

    // 阿莉耶丝女士的台词
    YELL_INTRO_ALY_2            = 0,  ///< 介绍台词2
    YELL_INTRO_ALY_4            = 1,  ///< 介绍台词4
    YELL_INTRO_ALY_6            = 2,  ///< 介绍台词6
    YELL_INTRO_ALY_8            = 3,  ///< 介绍台词8
    EMOTE_CONFLAGRATION         = 4,  ///< 混乱表情
    YELL_ALY_KILL               = 5,  ///< 阿莉耶丝击杀玩家台词
    YELL_ALY_DEAD               = 6,  ///< 阿莉耶丝死亡台词
    YELL_SISTER_SACROLASH_DEAD  = 7,  ///< 姐妹萨克拉什死亡台词
    YELL_CANFLAGRATION          = 8,  ///< 混乱台词
    YELL_BERSERK                = 9   ///< 狂暴台词
};

/**
 * @enum Spells
 * @brief 技能ID枚举
 *
 * 定义了埃雷达尔双子战斗中使用的所有技能ID
 */
enum Spells
{
    // 萨克拉什女士的技能
    SPELL_DARK_TOUCHED          = 45347, ///< 暗影触染 - 受到暗影伤害后获得的debuff
    SPELL_SHADOW_BLADES         = 45248, ///< 暗影之刃 - 10秒冷却，对全体玩家造成暗影伤害
    SPELL_DARK_STRIKE           = 45271, ///< 黑暗打击 - 暗影影像的近战技能
    SPELL_SHADOW_NOVA           = 45329, ///< 暗影新星 - 30-35秒冷却，对随机目标造成暗影伤害
    SPELL_CONFOUNDING_BLOW      = 45256, ///< 迷惑之击 - 25秒冷却，使目标迷惑

    // 暗影影像的技能
    SPELL_SHADOW_FURY           = 45270, ///< 暗影之怒 - 暗影影像使用的AOE技能
    SPELL_IMAGE_VISUAL          = 45263, ///< 影像视觉效果 - 暗影影像的外观效果

    // 通用技能
    SPELL_ENRAGE                = 46587, ///< 狂暴 - 6分钟后触发，提高伤害
    SPELL_EMPOWER               = 45366, ///< 强化 - 当姐妹死亡后获得的增益
    SPELL_DARK_FLAME            = 45345, ///< 黑暗烈焰 - 火焰和暗影debuff结合后的效果

    // 阿莉耶丝女士的技能
    SPELL_PYROGENICS            = 45230, ///< 焦热 - 15秒冷却，提高火焰伤害
    SPELL_FLAME_TOUCHED         = 45348, ///< 火焰触染 - 受到火焰伤害后获得的debuff
    SPELL_CONFLAGRATION         = 45342, ///< 混乱 - 30-35秒冷却，对随机目标造成火焰伤害
    SPELL_BLAZE                 = 45235, ///< 烈焰 - 每3秒对主目标施放
    SPELL_FLAME_SEAR            = 46771, ///< 灼烧之焰 - AOE火焰技能
    SPELL_BLAZE_SUMMON          = 45236, ///< 召唤烈焰 - 生成火焰游戏对象(187366)
    SPELL_BLAZE_BURN            = 45246  ///< 烈焰燃烧 - 地面火焰的伤害效果
};

/**
 * @class boss_sacrolash
 * @brief 萨克拉什女士BOSS脚本
 *
 * 实现了萨克拉什女士（暗影属性双子）的AI逻辑
 * 主要职责：
 * - 近战攻击主坦克
 * - 定期施放暗影之刃（AOE暗影伤害）
 * - 对随机目标施放暗影新星
 * - 对随机目标施放迷惑之击
 * - 召唤暗影影像
 * - 在姐妹死亡后获得强化并继承火焰技能
 */
class boss_sacrolash : public CreatureScript
{
public:
    boss_sacrolash() : CreatureScript("boss_sacrolash") { }

    /**
     * @struct boss_sacrolashAI
     * @brief 萨克拉什女士的AI结构体
     *
     * 继承自ScriptedAI，实现了完整的战斗逻辑
     */
    struct boss_sacrolashAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_sacrolashAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
        }

        /**
         * @brief 初始化所有成员变量
         *
         * 设置技能冷却时间和状态标志为初始值
         */
        void Initialize()
        {
            ShadowbladesTimer = 10000;      ///< 暗影之刃计时器，初始10秒
            ShadownovaTimer = 30000;        ///< 暗影新星计时器，初始30秒
            ConfoundingblowTimer = 25000;   ///< 迷惑之击计时器，初始25秒
            ShadowimageTimer = 20000;       ///< 暗影影像计时器，初始20秒
            ConflagrationTimer = 30000;     ///< 混乱计时器，初始30秒（姐妹死后使用）
            EnrageTimer = 360000;           ///< 狂暴计时器，6分钟
            SisterDeath = false;            ///< 姐妹是否死亡
            Enraged = false;                ///< 是否已狂暴
        }

        InstanceScript* instance;           ///< 副本实例脚本指针

        bool SisterDeath;                   ///< 标记姐妹（阿莉耶丝）是否已死亡
        bool Enraged;                       ///< 标记是否已进入狂暴状态

        uint32 ShadowbladesTimer;           ///< 暗影之刃技能冷却计时器（毫秒）
        uint32 ShadownovaTimer;             ///< 暗影新星技能冷却计时器（毫秒）
        uint32 ConfoundingblowTimer;        ///< 迷惑之击技能冷却计时器（毫秒）
        uint32 ShadowimageTimer;            ///< 召唤暗影影像冷却计时器（毫秒）
        uint32 ConflagrationTimer;          ///< 混乱技能冷却计时器（毫秒，姐妹死后使用）
        uint32 EnrageTimer;                 ///< 狂暴计时器（毫秒，6分钟）

        /**
         * @brief 重置函数，在BOSS脱离战斗或重置时调用
         *
         * 重置BOSS状态，并确保姐妹BOSS也正确重置
         * 如果姐妹死亡则复活她
         */
        void Reset() override
        {
            Enraged = false;

            // 获取阿莉耶丝并确保她处于正确状态
            if (Creature* temp = instance->GetCreature(DATA_ALYTHESS))
            {
                if (temp->isDead())
                    temp->Respawn();  // 如果姐妹已死亡则复活
                else if (temp->GetVictim())
                    AddThreat(temp->GetVictim(), 0.0f);  // 同步仇恨列表
            }

            // 仅在非战斗状态下初始化计时器
            if (!me->IsInCombat())
            {
                Initialize();
            }

            instance->SetBossState(DATA_EREDAR_TWINS, NOT_STARTED);
        }

        /**
         * @brief 进入战斗函数
         * @param who 进入战斗的目标
         *
         * 当BOSS进入战斗时，同时让姐妹BOSS也进入战斗
         * 这样可以确保双子的仇恨列表同步
         */
        void JustEngagedWith(Unit* who) override
        {
            DoZoneInCombat();

            // 让阿莉耶丝也加入战斗
            Creature* temp = instance->GetCreature(DATA_ALYTHESS);
            if (temp && temp->IsAlive() && !temp->GetVictim())
                temp->AI()->AttackStart(who);

            instance->SetBossState(DATA_EREDAR_TWINS, IN_PROGRESS);
        }

        /**
         * @brief 击杀单位函数
         * @param victim 被击杀的单位
         *
         * 当BOSS击杀玩家时有25%概率喊话
         */
        void KilledUnit(Unit* /*victim*/) override
        {
            if (rand32() % 4 == 0)  // 25%概率喊话
                Talk(YELL_SAC_KILL);
        }

        /**
         * @brief 死亡函数
         * @param killer 击杀者
         *
         * 处理BOSS死亡逻辑：
         * - 如果姐妹已死亡，则战斗结束，可以拾取战利品
         * - 如果姐妹还活着，则无法拾取，等待姐妹也被击杀
         */
        void JustDied(Unit* /*killer*/) override
        {
            // 只有在姐妹已死亡的情况下才算真正击败双子
            if (SisterDeath)
            {
                Talk(YELL_SAC_DEAD);
                instance->SetBossState(DATA_EREDAR_TWINS, DONE);
            }
            else
                me->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);  // 移除拾取标记，防止玩家提前拾取
        }

        /**
         * @brief 法术命中目标回调函数
         * @param target 法术命中的目标
         * @param spellInfo 法术信息
         *
         * 当萨克拉什的法术命中目标时，处理触染机制
         * 暗影技能会施加暗影触染，混乱技能会施加火焰触染
         */
        void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
        {
            Unit* unitTarget = target->ToUnit();
            if (!unitTarget)
                return;

            switch (spellInfo->Id)
            {
                case SPELL_SHADOW_BLADES:       // 暗影之刃
                case SPELL_SHADOW_NOVA:         // 暗影新星
                case SPELL_CONFOUNDING_BLOW:    // 迷惑之击
                case SPELL_SHADOW_FURY:         // 暗影之怒
                    HandleTouchedSpells(unitTarget, SPELL_DARK_TOUCHED);  // 施加暗影触染
                    break;
                case SPELL_CONFLAGRATION:       // 混乱（姐妹死后继承的技能）
                    HandleTouchedSpells(unitTarget, SPELL_FLAME_TOUCHED); // 施加火焰触染
                    break;
            }
        }

        /**
         * @brief 处理触染法术逻辑
         * @param target 目标单位
         * @param TouchedType 触染类型（暗影触染或火焰触染）
         *
         * 核心机制：
         * 1. 如果目标已被相反属性的触染效果影响，移除触染并施加"黑暗烈焰"
         * 2. 如果目标没有被任何触染效果影响，直接施加触染
         * 3. 如果目标已有黑暗烈焰，则不再施加任何效果
         *
         * 黑暗烈焰使目标受到的伤害翻倍，是战斗的核心机制
         */
        void HandleTouchedSpells(Unit* target, uint32 TouchedType)
        {
            switch (TouchedType)
            {
                case SPELL_FLAME_TOUCHED:  // 火焰触染
                    if (!target->HasAura(SPELL_DARK_FLAME))  // 如果没有黑暗烈焰
                    {
                        if (target->HasAura(SPELL_DARK_TOUCHED))  // 如果已有暗影触染
                        {
                            // 移除暗影触染，施加黑暗烈焰（双属性结合）
                            target->RemoveAurasDueToSpell(SPELL_DARK_TOUCHED);
                            target->CastSpell(target, SPELL_DARK_FLAME, true);
                        } else target->CastSpell(target, SPELL_FLAME_TOUCHED, true);  // 否则直接施加火焰触染
                    }
                    break;
                case SPELL_DARK_TOUCHED:   // 暗影触染
                    if (!target->HasAura(SPELL_DARK_FLAME))  // 如果没有黑暗烈焰
                    {
                        if (target->HasAura(SPELL_FLAME_TOUCHED))  // 如果已有火焰触染
                        {
                            // 移除火焰触染，施加黑暗烈焰（双属性结合）
                            target->RemoveAurasDueToSpell(SPELL_FLAME_TOUCHED);
                            target->CastSpell(target, SPELL_DARK_FLAME, true);
                        } else target->CastSpell(target, SPELL_DARK_TOUCHED, true);  // 否则直接施加暗影触染
                    }
                    break;
            }
        }

        /**
         * @brief 更新AI函数
         * @param diff 自上次更新以来经过的时间（毫秒）
         *
         * 每个游戏周期调用，处理BOSS的所有行为逻辑：
         * 1. 检测姐妹死亡状态
         * 2. 根据姐妹状态选择技能循环
         * 3. 处理技能冷却和施放
         * 4. 处理狂暴机制
         * 5. 执行近战攻击
         *
         * 性能注意事项：此函数每帧调用，应避免复杂计算
         */
        void UpdateAI(uint32 diff) override
        {
            // 检测阿莉耶丝是否死亡，如果死亡则获得强化
            if (!SisterDeath)
            {
                Unit* Temp = instance->GetCreature(DATA_ALYTHESS);
                if (Temp && Temp->isDead())
                {
                    Talk(YELL_SISTER_ALYTHESS_DEAD);  // 喊话：姐妹死亡
                    DoCast(me, SPELL_EMPOWER);         // 施加强化增益
                    me->InterruptSpell(CURRENT_GENERIC_SPELL);  // 中断当前施法
                    SisterDeath = true;
                }
            }

            // 确保有攻击目标
            if (!UpdateVictim())
                return;

            // 如果姐妹已死，使用混乱技能（继承自阿莉耶丝）
            if (SisterDeath)
            {
                if (ConflagrationTimer <= diff)
                {
                    if (!me->IsNonMeleeSpellCast(false))
                    {
                        me->InterruptSpell(CURRENT_GENERIC_SPELL);
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                            DoCast(target, SPELL_CONFLAGRATION);  // 对随机目标施放混乱
                        ConflagrationTimer = 30000 + (rand32() % 5000);  // 30-35秒冷却
                    }
                } else ConflagrationTimer -= diff;
            }
            else  // 姐妹存活时使用暗影新星
            {
                if (ShadownovaTimer <= diff)
                {
                    if (!me->IsNonMeleeSpellCast(false))
                    {
                        Unit* target = SelectTarget(SelectTargetMethod::Random, 0);
                        if (target)
                            DoCast(target, SPELL_SHADOW_NOVA);  // 对随机目标施放暗影新星

                        // 喊话和表情
                        if (!SisterDeath)
                        {
                            if (target)
                                Talk(EMOTE_SHADOW_NOVA, target);
                            Talk(YELL_SHADOW_NOVA);
                        }
                        ShadownovaTimer = 30000 + (rand32() % 5000);  // 30-35秒冷却
                    }
                } else ShadownovaTimer -=diff;
            }

            // 迷惑之击 - 对随机目标施放
            if (ConfoundingblowTimer <= diff)
            {
                if (!me->IsNonMeleeSpellCast(false))
                {
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(target, SPELL_CONFOUNDING_BLOW);
                    ConfoundingblowTimer = 20000 + (rand32() % 5000);  // 20-25秒冷却
                }
            } else ConfoundingblowTimer -=diff;

            // 召唤暗影影像 - 召唤3个小怪
            if (ShadowimageTimer <= diff)
            {
                Unit* target = nullptr;
                Creature* temp = nullptr;
                for (uint8 i = 0; i<3; ++i)  // 召唤3个暗影影像
                {
                    target = SelectTarget(SelectTargetMethod::Random, 0);
                    temp = DoSpawnCreature(NPC_SHADOW_IMAGE, 0, 0, 0, 0, TEMPSUMMON_CORPSE_DESPAWN, 10s);
                    if (temp && target)
                    {
                        AddThreat(target, 1000000.0f, temp); // 设置大量仇恨，防止暗影影像切换目标（特别是治疗者）
                        temp->AI()->AttackStart(target);
                    }
                }
                ShadowimageTimer = 20000;  // 20秒冷却
            } else ShadowimageTimer -=diff;

            // 暗影之刃 - AOE技能
            if (ShadowbladesTimer <= diff)
            {
                if (!me->IsNonMeleeSpellCast(false))
                {
                    DoCast(me, SPELL_SHADOW_BLADES);
                    ShadowbladesTimer = 10000;  // 10秒冷却
                }
            } else ShadowbladesTimer -=diff;

            // 狂暴检测 - 6分钟后狂暴
            if (EnrageTimer < diff && !Enraged)
            {
                me->InterruptSpell(CURRENT_GENERIC_SPELL);
                Talk(YELL_ENRAGE);
                DoCast(me, SPELL_ENRAGE);
                Enraged = true;
            } else EnrageTimer -= diff;

            // 近战攻击逻辑
            if (me->isAttackReady() && !me->IsNonMeleeSpellCast(false))
            {
                // 如果在近战范围内，执行攻击
                if (me->IsWithinMeleeRange(me->GetVictim()))
                {
                    HandleTouchedSpells(me->GetVictim(), SPELL_DARK_TOUCHED);  // 近战攻击施加暗影触染
                    me->AttackerStateUpdate(me->GetVictim());
                    me->resetAttackTimer();
                }
            }
        }
    };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象指针
         * @return 返回AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetSunwellPlateauAI<boss_sacrolashAI>(creature);
        };
};

/**
 * @class boss_alythess
 * @brief 阿莉耶丝女士BOSS脚本
 *
 * 实现了阿莉耶丝女士（火焰属性双子）的AI逻辑
 * 主要职责：
 * - 远程火焰攻击
 * - 定期施放灼烧之焰（AOE火焰伤害）
 * - 对随机目标施放混乱
 * - 定期施放焦热增益
 * - 在主目标脚下召唤烈焰
 * - 在姐妹死亡后获得强化并继承暗影技能
 *
 * 与萨克拉什不同，阿莉耶丝不进行近战攻击，而是远程施法
 */
class boss_alythess : public CreatureScript
{
public:
    boss_alythess() : CreatureScript("boss_alythess") { }

    /**
     * @struct boss_alythessAI
     * @brief 阿莉耶丝女士的AI结构体
     *
     * 继承自ScriptedAI，实现了完整的战斗逻辑
     */
    struct boss_alythessAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_alythessAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            SetCombatMovement(false);  // 阿莉耶丝不移动，原地施法

            instance = creature->GetInstanceScript();
            IntroStepCounter = 10;  // 初始化为10表示不在介绍阶段
        }

        /**
         * @brief 初始化所有成员变量
         *
         * 设置技能冷却时间和状态标志为初始值
         */
        void Initialize()
        {
            ConflagrationTimer = 45000;     ///< 混乱计时器，初始45秒
            BlazeTimer = 100;               ///< 烈焰计时器，初始0.1秒
            PyrogenicsTimer = 15000;        ///< 焦热计时器，初始15秒
            ShadownovaTimer = 40000;        ///< 暗影新星计时器，初始40秒（姐妹死后使用）
            EnrageTimer = 360000;           ///< 狂暴计时器，6分钟
            FlamesearTimer = 15000;         ///< 灼烧之焰计时器，初始15秒
            IntroYellTimer = 10000;         ///< 介绍台词计时器

            SisterDeath = false;            ///< 姐妹是否死亡
            Enraged = false;                ///< 是否已狂暴
        }

        InstanceScript* instance;           ///< 副本实例脚本指针

        bool SisterDeath;                   ///< 标记姐妹（萨克拉什）是否已死亡
        bool Enraged;                       ///< 标记是否已进入狂暴状态

        uint32 IntroStepCounter;            ///< 介绍阶段计数器，用于控制介绍台词序列
        uint32 IntroYellTimer;              ///< 介绍台词计时器

        uint32 ConflagrationTimer;          ///< 混乱技能冷却计时器（毫秒）
        uint32 BlazeTimer;                  ///< 烈焰技能冷却计时器（毫秒）
        uint32 PyrogenicsTimer;             ///< 焦热技能冷却计时器（毫秒）
        uint32 ShadownovaTimer;             ///< 暗影新星技能冷却计时器（毫秒，姐妹死后使用）
        uint32 FlamesearTimer;              ///< 灼烧之焰技能冷却计时器（毫秒）
        uint32 EnrageTimer;                 ///< 狂暴计时器（毫秒，6分钟）

        /**
         * @brief 重置函数
         *
         * 重置BOSS状态，并确保姐妹BOSS也正确重置
         * 如果姐妹死亡则复活她
         */
        void Reset() override
        {
            Enraged = false;

            // 获取萨克拉什并确保她处于正确状态
            if (Creature* temp = instance->GetCreature(DATA_SACROLASH))
            {
                if (temp->isDead())
                    temp->Respawn();  // 如果姐妹已死亡则复活
                else if (temp->GetVictim())
                    AddThreat(temp->GetVictim(), 0.0f);  // 同步仇恨列表
            }

            // 仅在非战斗状态下初始化计时器
            if (!me->IsInCombat())
            {
                Initialize();
            }

            instance->SetBossState(DATA_EREDAR_TWINS, NOT_STARTED);
        }

        /**
         * @brief 进入战斗函数
         * @param who 进入战斗的目标
         *
         * 当BOSS进入战斗时，同时让姐妹BOSS也进入战斗
         */
        void JustEngagedWith(Unit* who) override
        {
            DoZoneInCombat();

            // 让萨克拉什也加入战斗
            Creature* temp = instance->GetCreature(DATA_SACROLASH);
            if (temp && temp->IsAlive() && !temp->GetVictim())
                temp->AI()->AttackStart(who);

            instance->SetBossState(DATA_EREDAR_TWINS, IN_PROGRESS);
        }

        /**
         * @brief 攻击开始函数
         * @param who 攻击目标
         *
         * 重写此函数以防止阿莉耶丝移动
         * 她是远程施法者，不需要靠近目标
         */
        void AttackStart(Unit* who) override
        {
            if (!me->IsInCombat())
                ScriptedAI::AttackStart(who);
        }

        /**
         * @brief 视线内移动检测函数
         * @param who 进入视线的单位
         *
         * 处理两个逻辑：
         * 1. 战斗开始：在攻击范围内但不移动（阿莉耶丝原地施法）
         * 2. 介绍触发：当玩家进入30码范围且计数器为10时，开始介绍台词序列
         */
        void MoveInLineOfSight(Unit* who) override
        {
            if (!who || me->GetVictim())
                return;

            if (me->CanCreatureAttack(who))
            {
                float attackRadius = me->GetAttackDistance(who);
                if (me->IsWithinDistInMap(who, attackRadius) && me->GetDistanceZ(who) <= CREATURE_Z_ATTACK_RANGE && me->IsWithinLOSInMap(who))
                {
                    if (!me->IsInCombat())
                    {
                        DoStartNoMovement(who);  // 不移动，原地开始战斗
                    }
                }
            }
            // 触发介绍序列：当玩家接近且介绍未开始时
            else if (IntroStepCounter == 10 && me->IsWithinLOSInMap(who)&& me->IsWithinDistInMap(who, 30))
                IntroStepCounter = 0;  // 开始介绍序列
        }

        /**
         * @brief 击杀单位函数
         * @param victim 被击杀的单位
         *
         * 当BOSS击杀玩家时有25%概率喊话
         */
        void KilledUnit(Unit* /*victim*/) override
        {
            if (rand32() % 4 == 0)  // 25%概率喊话
                Talk(YELL_ALY_KILL);
        }

        /**
         * @brief 死亡函数
         * @param killer 击杀者
         *
         * 处理BOSS死亡逻辑：
         * - 如果姐妹已死亡，则战斗结束，可以拾取战利品
         * - 如果姐妹还活着，则无法拾取，等待姐妹也被击杀
         */
        void JustDied(Unit* /*killer*/) override
        {
            if (SisterDeath)
            {
                Talk(YELL_ALY_DEAD);
                instance->SetBossState(DATA_EREDAR_TWINS, DONE);
            }
            else
                me->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);  // 移除拾取标记
        }

        /**
         * @brief 法术命中目标回调函数
         * @param target 法术命中的目标
         * @param spellInfo 法术信息
         *
         * 当阿莉耶丝的法术命中目标时，处理触染机制和烈焰召唤
         */
        void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
        {
            Unit* unitTarget = target->ToUnit();
            if (!unitTarget)
                return;

            switch (spellInfo->Id)
            {
                case SPELL_BLAZE:           // 烈焰：在目标位置召唤火焰
                    target->CastSpell(unitTarget, SPELL_BLAZE_SUMMON, true);
                    break;
                case SPELL_CONFLAGRATION:   // 混乱
                case SPELL_FLAME_SEAR:      // 灼烧之焰
                    HandleTouchedSpells(unitTarget, SPELL_FLAME_TOUCHED);  // 施加火焰触染
                    break;
                case SPELL_SHADOW_NOVA:     // 暗影新星（姐妹死后继承的技能）
                    HandleTouchedSpells(unitTarget, SPELL_DARK_TOUCHED);   // 施加暗影触染
                    break;
            }
        }

        /**
         * @brief 处理触染法术逻辑
         * @param target 目标单位
         * @param TouchedType 触染类型（暗影触染或火焰触染）
         *
         * 与萨克拉什的触染处理逻辑相同
         */
        void HandleTouchedSpells(Unit* target, uint32 TouchedType)
        {
            switch (TouchedType)
            {
                case SPELL_FLAME_TOUCHED:  // 火焰触染
                    if (!target->HasAura(SPELL_DARK_FLAME))
                    {
                        if (target->HasAura(SPELL_DARK_TOUCHED))
                        {
                            target->RemoveAurasDueToSpell(SPELL_DARK_TOUCHED);
                            target->CastSpell(target, SPELL_DARK_FLAME, true);
                        }
                        else
                            target->CastSpell(target, SPELL_FLAME_TOUCHED, true);
                    }
                    break;
                case SPELL_DARK_TOUCHED:   // 暗影触染
                    if (!target->HasAura(SPELL_DARK_FLAME))
                    {
                        if (target->HasAura(SPELL_FLAME_TOUCHED))
                        {
                            target->RemoveAurasDueToSpell(SPELL_FLAME_TOUCHED);
                            target->CastSpell(target, SPELL_DARK_FLAME, true);
                        }
                        else
                            target->CastSpell(target, SPELL_DARK_TOUCHED, true);
                    }
                    break;
            }
        }

        /**
         * @brief 介绍步骤处理函数
         * @param step 当前步骤编号
         * @return 返回下一步的延迟时间（毫秒）
         *
         * 实现了双子BOSS的介绍台词序列：
         * 萨克拉什和阿莉耶丝交替喊话，营造氛围
         *
         * 步骤序列：
         * 1. 萨克拉什喊话 -> 1秒延迟
         * 2. 阿莉耶丝喊话 -> 1秒延迟
         * 3. 萨克拉什喊话 -> 2秒延迟
         * ...以此类推
         */
        uint32 IntroStep(uint32 step)
        {
            Creature* Sacrolash = instance->GetCreature(DATA_SACROLASH);
            switch (step)
            {
                case 0:
                    return 0;  // 初始状态，不延迟
                case 1:  // 萨克拉什喊话
                    if (Sacrolash)
                        Sacrolash->AI()->Talk(YELL_INTRO_SAC_1);
                    return 1000;  // 1秒后下一步
                case 2:  // 阿莉耶丝喊话
                    Talk(YELL_INTRO_ALY_2);
                    return 1000;
                case 3:  // 萨克拉什喊话
                    if (Sacrolash)
                        Sacrolash->AI()->Talk(YELL_INTRO_SAC_3);
                    return 2000;  // 2秒后下一步
                case 4:  // 阿莉耶丝喊话
                    Talk(YELL_INTRO_ALY_4);
                    return 1000;
                case 5:  // 萨克拉什喊话
                    if (Sacrolash)
                        Sacrolash->AI()->Talk(YELL_INTRO_SAC_5);
                    return 2000;
                case 6:  // 阿莉耶丝喊话
                    Talk(YELL_INTRO_ALY_6);
                    return 1000;
                case 7:  // 萨克拉什喊话
                    if (Sacrolash)
                        Sacrolash->AI()->Talk(YELL_INTRO_SAC_7);
                    return 3000;  // 3秒后下一步
                case 8:  // 阿莉耶丝最后喊话
                    Talk(YELL_INTRO_ALY_8);
                    return 900000;  // 15分钟后才重复（实际上不会重复）
            }
            return 10000;  // 默认10秒延迟
        }

        /**
         * @brief 更新AI函数
         * @param diff 自上次更新以来经过的时间（毫秒）
         *
         * 每个游戏周期调用，处理BOSS的所有行为逻辑：
         * 1. 处理介绍台词序列
         * 2. 检测姐妹死亡状态
         * 3. 根据姐妹状态选择技能循环
         * 4. 处理技能冷却和施放
         * 5. 处理狂暴机制
         *
         * 性能注意事项：此函数每帧调用，应避免复杂计算
         */
        void UpdateAI(uint32 diff) override
        {
            // 处理介绍台词序列（步骤0-8）
            if (IntroStepCounter < 9)
            {
                if (IntroYellTimer <= diff)
                {
                    IntroYellTimer = IntroStep(++IntroStepCounter);  // 执行下一步介绍
                } else IntroYellTimer -= diff;
            }

            // 检测萨克拉什是否死亡，如果死亡则获得强化
            if (!SisterDeath)
            {
                Unit* Temp = instance->GetCreature(DATA_SACROLASH);
                if (Temp && Temp->isDead())
                {
                    Talk(YELL_SISTER_SACROLASH_DEAD);  // 喊话：姐妹死亡
                    DoCast(me, SPELL_EMPOWER);          // 施加强化增益
                    me->InterruptSpell(CURRENT_GENERIC_SPELL);  // 中断当前施法
                    SisterDeath = true;
                }
            }

            // 如果没有当前目标，尝试跟随萨克拉什的目标
            if (!me->GetVictim())
            {
                Creature* sisiter = instance->GetCreature(DATA_SACROLASH);
                if (sisiter && !sisiter->isDead() && sisiter->GetVictim())
                {
                    AddThreat(sisiter->GetVictim(), 0.0f);
                    DoStartNoMovement(sisiter->GetVictim());  // 不移动，原地攻击
                    me->Attack(sisiter->GetVictim(), false);
                }
            }

            // 确保有攻击目标
            if (!UpdateVictim())
                return;

            // 如果姐妹已死，使用暗影新星技能（继承自萨克拉什）
            if (SisterDeath)
            {
                if (ShadownovaTimer <= diff)
                {
                    if (!me->IsNonMeleeSpellCast(false))
                    {
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                            DoCast(target, SPELL_SHADOW_NOVA);  // 对随机目标施放暗影新星
                        ShadownovaTimer = 30000 + (rand32() % 5000);  // 30-35秒冷却
                    }
                } else ShadownovaTimer -=diff;
            }
            else  // 姐妹存活时使用混乱
            {
                if (ConflagrationTimer <= diff)
                {
                    if (!me->IsNonMeleeSpellCast(false))
                    {
                        me->InterruptSpell(CURRENT_GENERIC_SPELL);
                        Unit* target = SelectTarget(SelectTargetMethod::Random, 0);
                        if (target)
                            DoCast(target, SPELL_CONFLAGRATION);  // 对随机目标施放混乱
                        ConflagrationTimer = 30000 + (rand32() % 5000);  // 30-35秒冷却

                        // 喊话和表情
                        if (!SisterDeath)
                        {
                            if (target)
                                Talk(EMOTE_CONFLAGRATION, target);
                            Talk(YELL_CANFLAGRATION);
                        }

                        BlazeTimer = 4000;  // 混乱后4秒施放烈焰
                    }
                } else ConflagrationTimer -= diff;
            }

            // 灼烧之焰 - AOE火焰技能
            if (FlamesearTimer <= diff)
            {
                if (!me->IsNonMeleeSpellCast(false))
                {
                    DoCast(me, SPELL_FLAME_SEAR);
                    FlamesearTimer = 15000;  // 15秒冷却
                }
            } else FlamesearTimer -=diff;

            // 焦热 - 自身增益，提高火焰伤害
            if (PyrogenicsTimer <= diff)
            {
                if (!me->IsNonMeleeSpellCast(false))
                {
                    DoCast(me, SPELL_PYROGENICS, true);
                    PyrogenicsTimer = 15000;  // 15秒冷却
                }
            } else PyrogenicsTimer -= diff;

            // 烈焰 - 在主目标脚下召唤火焰
            if (BlazeTimer <= diff)
            {
                if (!me->IsNonMeleeSpellCast(false))
                {
                    DoCastVictim(SPELL_BLAZE);  // 对主目标施放烈焰
                    BlazeTimer = 3800;  // 约3.8秒冷却
                }
            } else BlazeTimer -= diff;

            // 狂暴检测 - 6分钟后狂暴
            if (EnrageTimer < diff && !Enraged)
            {
                me->InterruptSpell(CURRENT_GENERIC_SPELL);
                Talk(YELL_BERSERK);
                DoCast(me, SPELL_ENRAGE);
                Enraged = true;
            } else EnrageTimer -= diff;
        }
    };

        /**
         * @brief 获取AI实例
         * @param creature 生物对象指针
         * @return 返回AI实例指针
         */
        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetSunwellPlateauAI<boss_alythessAI>(creature);
        };
};

/**
 * @class npc_shadow_image
 * @brief 暗影影像NPC脚本
 *
 * 实现了萨克拉什召唤的暗影影像小怪的AI逻辑
 * 主要职责：
 * - 攻击随机目标（被召唤时指定）
 * - 定期施放暗影之怒（AOE晕眩）
 * - 近战攻击施加黑暗打击
 * - 15秒后自动死亡
 *
 * 暗影影像的威胁度被设置为极高，防止切换目标
 */
class npc_shadow_image : public CreatureScript
{
public:
    npc_shadow_image() : CreatureScript("npc_shadow_image") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetSunwellPlateauAI<npc_shadow_imageAI>(creature);
    };

    /**
     * @struct npc_shadow_imageAI
     * @brief 暗影影像的AI结构体
     *
     * 简单的攻击AI，专注于攻击指定目标并施加暗影触染效果
     */
    struct npc_shadow_imageAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        npc_shadow_imageAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         */
        void Initialize()
        {
            ShadowfuryTimer = 5000 + (rand32() % 15000);  ///< 暗影之怒计时器，5-20秒随机
            DarkstrikeTimer = 3000;                         ///< 黑暗打击计时器，3秒
            KillTimer = 15000;                              ///< 死亡计时器，15秒后自动死亡
        }

        uint32 ShadowfuryTimer;   ///< 暗影之怒技能冷却计时器（毫秒）
        uint32 KillTimer;         ///< 生命周期计时器（毫秒）
        uint32 DarkstrikeTimer;   ///< 黑暗打击技能冷却计时器（毫秒）

        /**
         * @brief 重置函数
         */
        void Reset() override
        {
            Initialize();
        }

        /**
         * @brief 进入战斗函数
         * @param who 进入战斗的目标
         *
         * 暗影影像不需要额外的战斗逻辑
         */
        void JustEngagedWith(Unit* /*who*/) override { }

        /**
         * @brief 法术命中目标回调函数
         * @param target 法术命中的目标
         * @param spellInfo 法术信息
         *
         * 处理暗影技能的触染效果
         */
        void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override
        {
            Unit* unitTarget = target->ToUnit();
            if (!unitTarget)
                return;

            switch (spellInfo->Id)
            {
                case SPELL_SHADOW_FURY:   // 暗影之怒
                case SPELL_DARK_STRIKE:   // 黑暗打击
                    if (!unitTarget->HasAura(SPELL_DARK_FLAME))
                    {
                        if (unitTarget->HasAura(SPELL_FLAME_TOUCHED))
                        {
                            // 移除火焰触染，施加黑暗烈焰
                            unitTarget->RemoveAurasDueToSpell(SPELL_FLAME_TOUCHED);
                            unitTarget->CastSpell(unitTarget, SPELL_DARK_FLAME, true);
                        }
                        else
                            unitTarget->CastSpell(unitTarget, SPELL_DARK_TOUCHED, true);  // 施加暗影触染
                    }
                    break;
            }
        }

        /**
         * @brief 更新AI函数
         * @param diff 自上次更新以来经过的时间（毫秒）
         *
         * 处理暗影影像的行为：
         * 1. 施加视觉效果
         * 2. 15秒后自动死亡
         * 3. 定期施放暗影之怒
         * 4. 近战攻击并施放黑暗打击
         *
         * 性能注意事项：简单的计时器逻辑，性能影响小
         */
        void UpdateAI(uint32 diff) override
        {
            // 施加暗影影像的视觉效果
            if (!me->HasAura(SPELL_IMAGE_VISUAL))
                DoCast(me, SPELL_IMAGE_VISUAL);

            // 15秒后自动死亡
            if (KillTimer <= diff)
            {
                me->KillSelf();
                KillTimer = 9999999;  // 防止重复死亡
            } else KillTimer -= diff;

            // 确保有攻击目标
            if (!UpdateVictim())
                return;

            // 暗影之怒 - AOE晕眩技能
            if (ShadowfuryTimer <= diff)
            {
                DoCast(me, SPELL_SHADOW_FURY);
                ShadowfuryTimer = 10000;  // 10秒冷却
            } else ShadowfuryTimer -=diff;

            // 黑暗打击 - 近战攻击技能
            if (DarkstrikeTimer <= diff)
            {
                if (!me->IsNonMeleeSpellCast(false))
                {
                    // 如果在近战范围内，施放黑暗打击
                    if (me->IsWithinMeleeRange(me->GetVictim()))
                        DoCastVictim(SPELL_DARK_STRIKE);
                }
                DarkstrikeTimer = 3000;  // 3秒冷却
            } else DarkstrikeTimer -= diff;
        }
    };
};

/**
 * @brief 注册BOSS脚本
 *
 * 将埃雷达尔双子的所有脚本注册到系统中
 * 包括两个BOSS和一个召唤物
 */
void AddSC_boss_eredar_twins()
{
    new boss_sacrolash();      // 萨克拉什女士
    new boss_alythess();       // 阿莉耶丝女士
    new npc_shadow_image();    // 暗影影像召唤物
}
