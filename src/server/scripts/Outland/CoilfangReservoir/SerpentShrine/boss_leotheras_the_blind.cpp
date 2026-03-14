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
 * @file boss_leotheras_the_blind.cpp
 * @brief 盲眼者莱欧瑟拉斯BOSS战AI脚本
 *
 * 本文件实现了毒蛇神殿副本中BOSS盲眼者莱欧瑟拉斯的战斗AI。
 * 该BOSS战具有独特的人格分裂机制：
 * - 人形形态：使用旋风斩攻击，周期性切换为恶魔形态
 * - 恶魔形态：使用混乱爆炸攻击，召唤内心恶魔
 * - 最终阶段：血量低于15%时分身为两个实体同时战斗
 *
 * 核心机制：
 * - 被放逐状态：BOSS由3个灰心咒术师维持放逐，需要击杀咒术师才能激活BOSS
 * - 形态切换：在人形和恶魔形态之间周期性切换
 * - 内心恶魔：恶魔形态召唤，每个玩家必须击杀自己的内心恶魔，否则会被精神控制
 * - 最终分身：15%血量时分裂为暗夜精灵和恶魔两个实体
 *
 * @see instance_serpent_shrine.cpp 关联的副本实例脚本
 */

/* ScriptData
SDName: Boss_Leotheras_The_Blind
SD%Complete: 80
SDComment: Possesion Support
SDCategory: Coilfang Resevoir, Serpent Shrine Cavern
EndScriptData */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "serpent_shrine.h"
#include "TemporarySummon.h"

/**
 * @brief 盲眼者莱欧瑟拉斯BOSS战相关枚举定义
 *
 * 包含BOSS的技能ID、生物ID和对话文本ID
 */
enum LeotherasTheBlind
{
    // 莱欧瑟拉斯使用的技能
    SPELL_WHIRLWIND         = 37640,  // 旋风斩（人形形态主要技能，持续移动攻击随机目标）
    SPELL_CHAOS_BLAST       = 37674,  // 混乱爆炸（恶魔形态远程攻击，造成自然伤害）
    SPELL_BERSERK           = 26662,  // 狂暴（10分钟后施放，增加伤害）
    SPELL_INSIDIOUS_WHISPER = 37676,  // 阴险低语（内心恶魔给目标的debuff，需击杀恶魔才能移除）
    SPELL_DUAL_WIELD        = 42459,  // 双持（被动技能，允许双武器攻击）

    // 放逐阶段使用的技能
    BANISH_BEAM             = 38909,  // 放逐光束（咒术师对BOSS施放，维持放逐状态）
    AURA_BANISH             = 37833,  // 放逐光环（BOSS的放逐状态视觉效果）

    // 灰心咒术师使用的技能
    SPELL_EARTHSHOCK        = 39076,  // 地震术（打断施法）
    SPELL_MINDBLAST         = 37531,  // 心灵震爆（暗影伤害）

    // 内心恶魔使用的技能和生物ID
    INNER_DEMON_ID          = 21857,  // 内心恶魔生物ID
    AURA_DEMONIC_ALIGNMENT  = 37713,  // 恶魔对齐光环（视觉效果）
    SPELL_SHADOWBOLT        = 39309,  // 暗影箭（内心恶魔的远程攻击）
    SPELL_SOUL_LINK         = 38007,  // 灵魂链接（内心恶魔与目标的连接）
    SPELL_CONSUMING_MADNESS = 37749,  // 吞噬疯狂（内心恶魔存活时间结束时对目标施放）

    // 杂项
    MODEL_DEMON             = 20125,  // 恶魔模型ID
    MODEL_NIGHTELF          = 20514,  // 暗夜精灵模型ID
    DEMON_FORM              = 21875,  // 恶魔形态生物ID（15%血量时召唤的分身）
    NPC_SPELLBINDER         = 21806,  // 灰心咒术师生物ID
    INNER_DEMON_VICTIM      = 1,      // 内心恶魔受害者标识符

    // 对话文本ID
    SAY_AGGRO               = 0,      // 开战对话
    SAY_SWITCH_TO_DEMON     = 1,      // 切换到恶魔形态对话
    SAY_INNER_DEMONS        = 2,      // 召唤内心恶魔对话
    SAY_DEMON_SLAY          = 3,      // 恶魔形态击杀玩家对话
    SAY_NIGHTELF_SLAY       = 4,      // 暗夜精灵形态击杀玩家对话
    SAY_FINAL_FORM          = 5,      // 最终形态对话（15%血量）
    SAY_FREE                = 6,      // 自由对话（恶魔分身）
    SAY_DEATH               = 7       // 死亡对话
};

/**
 * @brief 内心恶魔AI结构体
 *
 * 恶魔形态下莱欧瑟拉斯召唤的特殊小怪，每个玩家都会有一个内心恶魔
 * 只有对应的玩家才能对其造成伤害，其他玩家攻击无效
 * 如果在恶魔切换回人形前没有击杀内心恶魔，玩家会被施放吞噬疯狂精神控制
 */
struct npc_inner_demon : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_inner_demon(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    /**
     * @brief 初始化计时器
     */
    void Initialize()
    {
        ShadowBolt_Timer = 10000;  // 暗影箭冷却计时器（初始10秒）
        Link_Timer = 1000;         // 灵魂链接冷却计时器（初始1秒）
    }

    uint32 ShadowBolt_Timer;       // 暗影箭施放冷却计时器（毫秒）
    uint32 Link_Timer;             // 灵魂链接施放冷却计时器（毫秒）
    ObjectGuid victimGUID;         // 内心恶魔对应的目标玩家GUID

    /**
     * @brief 重置AI状态
     */
    void Reset() override
    {
        Initialize();
    }

    /**
     * @brief 设置目标GUID
     * @param guid 目标GUID
     * @param id 标识符（INNER_DEMON_VICTIM表示设置受害者）
     *
     * 由莱欧瑟拉斯召唤时调用，设置内心恶魔对应的目标玩家
     */
    void SetGUID(ObjectGuid const& guid, int32 id) override
    {
        if (id == INNER_DEMON_VICTIM)
            victimGUID = guid;
    }

    /**
     * @brief 获取目标GUID
     * @param id 标识符
     * @return 对应的GUID，无效则返回空GUID
     */
    ObjectGuid GetGUID(int32 id/* = 0 */) const override
    {
        if (id == INNER_DEMON_VICTIM)
            return victimGUID;
        return ObjectGuid::Empty;
    }

    /**
     * @brief 死亡时移除目标的阴险低语debuff
     * @param killer 击杀者（未使用）
     *
     * 当内心恶魔被击杀时，移除对应玩家的阴险低语debuff
     * 玩家成功"战胜内心恶魔"
     */
    void JustDied(Unit* /*killer*/) override
    {
        Unit* unit = ObjectAccessor::GetUnit(*me, victimGUID);
        if (unit && unit->HasAura(SPELL_INSIDIOUS_WHISPER))
            unit->RemoveAurasDueToSpell(SPELL_INSIDIOUS_WHISPER);
    }

    /**
     * @brief 伤害处理函数
     * @param done_by 造成伤害的单位
     * @param damage 伤害值（引用，可修改）
     * @param damageType 伤害类型
     * @param spellInfo 法术信息
     *
     * 核心机制：只有对应的目标玩家才能对内心恶魔造成伤害
     * 其他玩家攻击无效，伤害被设为0并清除威胁
     */
    void DamageTaken(Unit* done_by, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override
    {
        // 只有目标玩家和恶魔自己可以对恶魔造成伤害
        if (!done_by || (done_by->GetGUID() != victimGUID && done_by->GetGUID() != me->GetGUID()))
        {
            damage = 0;  // 伤害无效
            ModifyThreatByPercent(done_by, -100);  // 清除威胁
        }
    }

    /**
     * @brief 进入战斗
     * @param who 攻击者
     */
    void JustEngagedWith(Unit* /*who*/) override
    {
        if (!victimGUID)
            return;
    }

    /**
     * @brief 更新AI逻辑（每帧调用）
     * @param diff 距离上次调用的毫秒数
     *
     * 处理内心恶魔的行为：
     * - 只攻击对应的目标玩家
     * - 定期施放灵魂链接
     * - 施放暗影箭攻击
     * - 如果目标死亡，恶魔自毁
     *
     * @performance 每帧调用，保持高效
     */
    void UpdateAI(uint32 diff) override
    {
        // 没有目标则返回
        if (!UpdateVictim() || !me->GetVictim())
            return;

        // 确保只攻击对应的目标玩家
        if (me->EnsureVictim()->GetGUID() != victimGUID)
        {
            ModifyThreatByPercent(me->GetVictim(), -100);  // 清除当前目标的威胁
            Unit* owner = ObjectAccessor::GetUnit(*me, victimGUID);
            if (owner && owner->IsAlive())
            {
                AddThreat(owner, 999999);  // 设置极高威胁
                AttackStart(owner);
            } else if (owner && owner->isDead())
            {
                // 目标玩家已死亡，恶魔自毁
                me->KillSelf();
                return;
            }
        }

        // 灵魂链接计时器
        if (Link_Timer <= diff)
        {
            DoCastVictim(SPELL_SOUL_LINK, true);
            Link_Timer = 1000;
        } else Link_Timer -= diff;

        // 保持恶魔对齐光环（视觉效果）
        if (!me->HasAura(AURA_DEMONIC_ALIGNMENT))
            DoCast(me, AURA_DEMONIC_ALIGNMENT, true);

        // 暗影箭计时器
        if (ShadowBolt_Timer <= diff)
        {
            DoCastVictim(SPELL_SHADOWBOLT, false);
            ShadowBolt_Timer = 10000;  // 10秒冷却
        } else ShadowBolt_Timer -= diff;

       DoMeleeAttackIfReady();
    }
};

//Original Leotheras the Blind AI
struct boss_leotheras_the_blind : public BossAI
{
    boss_leotheras_the_blind(Creature* creature) : BossAI(creature, BOSS_LEOTHERAS_THE_BLIND)
    {
        Initialize();
        creature->GetPosition(x, y, z);
    }

    void Initialize()
    {
        BanishTimer = 1000;
        Whirlwind_Timer = 15000;
        ChaosBlast_Timer = 1000;
        SwitchToDemon_Timer = 45000;
        SwitchToHuman_Timer = 60000;
        Berserk_Timer = 600000;
        InnerDemons_Timer = 30000;

        DealDamage = true;
        DemonForm = false;
        IsFinalForm = false;
        NeedThreatReset = false;
        EnrageUsed = false;
        for (ObjectGuid& guid : InnderDemon)
            guid.Clear();
        InnerDemon_Count = 0;
    }

    uint32 Whirlwind_Timer;
    uint32 ChaosBlast_Timer;
    uint32 SwitchToDemon_Timer;
    uint32 SwitchToHuman_Timer;
    uint32 Berserk_Timer;
    uint32 InnerDemons_Timer;
    uint32 BanishTimer;

    bool DealDamage;
    bool NeedThreatReset;
    bool DemonForm;
    bool IsFinalForm;
    bool EnrageUsed;
    float x, y, z;

    ObjectGuid InnderDemon[5];
    uint32 InnerDemon_Count;
    ObjectGuid Demon;
    ObjectGuid SpellBinderGUID[3];

    void Reset() override
    {
        CheckChannelers();
        Initialize();
        me->SetCanDualWield(true);
        me->SetSpeedRate(MOVE_RUN, 2.0f);
        me->SetDisplayId(MODEL_NIGHTELF);
        me->SetVirtualItem(0, 0);
        me->SetVirtualItem(1, 0);
        DoCast(me, SPELL_DUAL_WIELD, true);
        me->SetCorpseDelay(1000*60*60);
        _Reset();
    }

    void CheckChannelers(/*bool DoEvade = true*/)
    {
        for (uint8 i = 0; i < 3; ++i)
        {
            if (Creature* add = ObjectAccessor::GetCreature(*me, SpellBinderGUID[i]))
                add->DisappearAndDie();

            float nx = x;
            float ny = y;
            float o = 2.4f;
            if (i == 0) {nx += 10; ny -= 5; o=2.5f;}
            if (i == 1) {nx -= 8; ny -= 7; o=0.9f;}
            if (i == 2) {nx -= 3; ny += 9; o=5.0f;}
            Creature* binder = me->SummonCreature(NPC_SPELLBINDER, nx, ny, z, o, TEMPSUMMON_DEAD_DESPAWN);
            if (binder)
                SpellBinderGUID[i] = binder->GetGUID();
        }
    }
    void MoveInLineOfSight(Unit* who) override

    {
        if (me->HasAura(AURA_BANISH))
            return;

        if (!me->GetVictim() && me->CanCreatureAttack(who))
        {
            if (me->GetDistanceZ(who) > CREATURE_Z_ATTACK_RANGE)
                return;

            float attackRadius = me->GetAttackDistance(who);
            if (me->IsWithinDistInMap(who, attackRadius))
            {
                // Check first that object is in an angle in front of this one before LoS check
                if (me->HasInArc(float(M_PI) / 2.0f, who) && me->IsWithinLOSInMap(who))
                {
                    AttackStart(who);
                }
            }
        }
    }

    void StartEvent()
    {
        Talk(SAY_AGGRO);
        instance->SetBossState(BOSS_LEOTHERAS_THE_BLIND, IN_PROGRESS);
    }

    void CheckBanish()
    {
        uint8 AliveChannelers = 0;
        for (uint8 i = 0; i < 3; ++i)
        {
            Unit* add = ObjectAccessor::GetUnit(*me, SpellBinderGUID[i]);
            if (add && add->IsAlive())
                ++AliveChannelers;
        }

        // channelers == 0 remove banish aura
        if (AliveChannelers == 0 && me->HasAura(AURA_BANISH))
        {
            // removing banish aura
            me->RemoveAurasDueToSpell(AURA_BANISH);

            // Leotheras is getting immune again
            me->ApplySpellImmune(AURA_BANISH, IMMUNITY_MECHANIC, MECHANIC_BANISH, true);

            // changing model to bloodelf
            me->SetDisplayId(MODEL_NIGHTELF);

            // and reseting equipment
            me->LoadEquipment();

            if (instance->GetGuidData(DATA_LEOTHERAS_EVENT_STARTER))
            {
                if (Unit* victim = ObjectAccessor::GetUnit(*me, instance->GetGuidData(DATA_LEOTHERAS_EVENT_STARTER)))
                    AddThreat(victim, 1);
                StartEvent();
            }
        }
        else if (AliveChannelers != 0 && !me->HasAura(AURA_BANISH))
        {
            // channelers != 0 apply banish aura
            // removing Leotheras banish immune to apply AURA_BANISH
            me->ApplySpellImmune(AURA_BANISH, IMMUNITY_MECHANIC, MECHANIC_BANISH, false);
            DoCast(me, AURA_BANISH);

            // changing model
            me->SetDisplayId(MODEL_DEMON);

            // and removing weapons
            me->SetVirtualItem(0, 0);
            me->SetVirtualItem(1, 0);
        }
    }

    //Despawn all Inner Demon summoned
    void DespawnDemon()
    {
        for (uint8 i=0; i<5; ++i)
        {
            if (InnderDemon[i])
            {
                //delete creature
                Creature* creature = ObjectAccessor::GetCreature((*me), InnderDemon[i]);
                if (creature && creature->IsAlive())
                    creature->DespawnOrUnsummon();

                InnderDemon[i].Clear();
            }
        }

        InnerDemon_Count = 0;
    }

    void CastConsumingMadness() //remove this once SPELL_INSIDIOUS_WHISPER is supported by core
    {
        for (uint8 i = 0; i < 5; ++i)
        {
            if (InnderDemon[i])
            {
                Creature* unit = ObjectAccessor::GetCreature((*me), InnderDemon[i]);
                if (unit && unit->IsAlive())
                {
                    Unit* unit_target = ObjectAccessor::GetUnit(*unit, unit->AI()->GetGUID(INNER_DEMON_VICTIM));
                    if (unit_target && unit_target->IsAlive())
                    {
                        unit->CastSpell(unit_target, SPELL_CONSUMING_MADNESS, true);
                        ModifyThreatByPercent(unit_target, -100);
                    }
                }
            }
        }
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() != TYPEID_PLAYER)
            return;

        Talk(DemonForm ? SAY_DEMON_SLAY : SAY_NIGHTELF_SLAY);
    }

    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_DEATH);

        //despawn copy
        if (Demon)
        {
            if (Creature* pDemon = ObjectAccessor::GetCreature(*me, Demon))
                pDemon->DespawnOrUnsummon();
        }
        _JustDied();
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        if (me->HasAura(AURA_BANISH))
        return;

        me->LoadEquipment();
    }

    void UpdateAI(uint32 diff) override
    {
        //Return since we have no target
        if (me->HasAura(AURA_BANISH) || !UpdateVictim())
        {
            if (BanishTimer <= diff)
            {
                CheckBanish();//no need to check every update tick
                BanishTimer = 1000;
            } else BanishTimer -= diff;
            return;
        }
        if (me->HasAura(SPELL_WHIRLWIND))
        {
            if (Whirlwind_Timer <= diff)
            {
                Unit* newTarget = SelectTarget(SelectTargetMethod::Random, 0);
                if (newTarget)
                {
                    ResetThreatList();
                    me->GetMotionMaster()->Clear();
                    me->GetMotionMaster()->MovePoint(0, newTarget->GetPositionX(), newTarget->GetPositionY(), newTarget->GetPositionZ());
                }
                Whirlwind_Timer = 2000;
            } else Whirlwind_Timer -= diff;
        }

        // reseting after changing forms and after ending whirlwind
        if (NeedThreatReset && !me->HasAura(SPELL_WHIRLWIND))
        {
            // when changing forms seting timers (or when ending whirlwind - to avoid adding new variable i use Whirlwind_Timer to countdown 2s while whirlwinding)
            if (DemonForm)
                InnerDemons_Timer = 30000;
            else
                Whirlwind_Timer =  15000;

            NeedThreatReset = false;
            ResetThreatList();
            me->GetMotionMaster()->Clear();
            me->GetMotionMaster()->MoveChase(me->GetVictim());
        }

        //Enrage_Timer (10 min)
        if (Berserk_Timer < diff && !EnrageUsed)
        {
            me->InterruptNonMeleeSpells(false);
            DoCast(me, SPELL_BERSERK);
            EnrageUsed = true;
        } else Berserk_Timer -= diff;

        if (!DemonForm)
        {
            //Whirldind Timer
            if (!me->HasAura(SPELL_WHIRLWIND))
            {
                if (Whirlwind_Timer <= diff)
                {
                    DoCast(me, SPELL_WHIRLWIND);
                    // while whirlwinding this variable is used to countdown target's change
                    Whirlwind_Timer = 2000;
                    NeedThreatReset = true;
                } else Whirlwind_Timer -= diff;
            }
            //Switch_Timer

            if (!IsFinalForm)
            {
                if (SwitchToDemon_Timer <= diff)
                {
                    //switch to demon form
                    me->RemoveAurasDueToSpell(SPELL_WHIRLWIND);
                    me->SetDisplayId(MODEL_DEMON);
                    Talk(SAY_SWITCH_TO_DEMON);
                    me->SetVirtualItem(0, 0);
                    me->SetVirtualItem(1, 0);
                    DemonForm = true;
                    NeedThreatReset = true;
                    SwitchToDemon_Timer = 45000;
                } else SwitchToDemon_Timer -= diff;
            }
            DoMeleeAttackIfReady();
        }
        else
        {
            //ChaosBlast_Timer
            if (!me->GetVictim())
                return;
            if (me->IsWithinDist(me->GetVictim(), 30))
                me->StopMoving();
            if (ChaosBlast_Timer <= diff)
            {
                // will cast only when in range of spell
                if (me->IsWithinDist(me->GetVictim(), 30))
                {
                    //DoCastVictim(SPELL_CHAOS_BLAST, true);
                    me->CastSpell(me->GetVictim(), SPELL_CHAOS_BLAST, CastSpellExtraArgs().SetOriginalCaster(me->GetGUID()).AddSpellBP0(100));
                }
                ChaosBlast_Timer = 3000;
            } else ChaosBlast_Timer -= diff;
            //Summon Inner Demon
            if (InnerDemons_Timer <= diff)
            {
                ThreatManager const& mgr = me->GetThreatManager();
                std::list<Unit*> TargetList;
                Unit* currentVictim = mgr.GetLastVictim();
                for (ThreatReference const* ref : mgr.GetSortedThreatList())
                {
                    if (Player* tempTarget = ref->GetVictim()->ToPlayer())
                        if (tempTarget != currentVictim && TargetList.size()<5)
                            TargetList.push_back(tempTarget);
                }
                //SpellInfo* spell = GET_SPELL(SPELL_INSIDIOUS_WHISPER);
                for (auto itr = TargetList.begin(), end = TargetList.end(); itr != end; ++itr)
                {
                    if ((*itr) && (*itr)->IsAlive())
                    {
                        Creature* demon = me->SummonCreature(INNER_DEMON_ID, (*itr)->GetPositionX()+10, (*itr)->GetPositionY()+10, (*itr)->GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5s);
                        if (demon)
                        {
                            demon->AI()->AttackStart((*itr));
                            demon->AI()->SetGUID((*itr)->GetGUID(), INNER_DEMON_VICTIM);

                            (*itr)->AddAura(SPELL_INSIDIOUS_WHISPER, *itr);

                            if (InnerDemon_Count > 4)
                                InnerDemon_Count = 0;

                            //Safe storing of creatures
                            InnderDemon[InnerDemon_Count] = demon->GetGUID();

                            //Update demon count
                            ++InnerDemon_Count;
                        }
                    }
                }
                Talk(SAY_INNER_DEMONS);

                InnerDemons_Timer = 999999;
            } else InnerDemons_Timer -= diff;

            //Switch_Timer
            if (SwitchToHuman_Timer <= diff)
            {
                //switch to nightelf form
                me->SetDisplayId(MODEL_NIGHTELF);
                me->LoadEquipment();

                CastConsumingMadness();
                DespawnDemon();

                DemonForm = false;
                NeedThreatReset = true;

                SwitchToHuman_Timer = 60000;
            } else SwitchToHuman_Timer -= diff;
        }

        if (!IsFinalForm && HealthBelowPct(15))
        {
            //at this point he divides himself in two parts
            CastConsumingMadness();
            DespawnDemon();
            if (Creature* Copy = DoSpawnCreature(DEMON_FORM, 0, 0, 0, 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 6s))
            {
                Demon = Copy->GetGUID();
                if (me->GetVictim())
                    Copy->AI()->AttackStart(me->GetVictim());
            }
            //set nightelf final form
            IsFinalForm = true;
            DemonForm = false;

            Talk(SAY_FINAL_FORM);
            me->SetDisplayId(MODEL_NIGHTELF);
            me->LoadEquipment();
        }
    }
};

//Leotheras the Blind Demon Form AI
struct boss_leotheras_the_blind_demonform : public ScriptedAI
{
    boss_leotheras_the_blind_demonform(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    void Initialize()
    {
        ChaosBlast_Timer = 1000;
        DealDamage = true;
    }

    uint32 ChaosBlast_Timer;
    bool DealDamage;

    void Reset() override
    {
        Initialize();
    }

    void StartEvent()
    {
        Talk(SAY_FREE);
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() != TYPEID_PLAYER)
            return;

        Talk(SAY_DEMON_SLAY);
    }

    void JustDied(Unit* /*killer*/) override
    {
        //invisibility (blizzlike, at the end of the fight he doesn't die, he disappears)
        DoCast(me, 8149, true);
    }

    void JustEngagedWith(Unit* /*who*/) override
    {
        StartEvent();
    }

    void UpdateAI(uint32 diff) override
    {
        //Return since we have no target
        if (!UpdateVictim())
            return;
        //ChaosBlast_Timer
        if (me->IsWithinDist(me->GetVictim(), 30))
            me->StopMoving();

        if (ChaosBlast_Timer <= diff)
         {
            // will cast only when in range od spell
            if (me->IsWithinDist(me->GetVictim(), 30))
            {
                //DoCastVictim(SPELL_CHAOS_BLAST, true);
                me->CastSpell(me->GetVictim(), SPELL_CHAOS_BLAST, CastSpellExtraArgs().SetOriginalCaster(me->GetGUID()).AddSpellBP0(100));
                ChaosBlast_Timer = 3000;
            }
         } else ChaosBlast_Timer -= diff;

        //Do NOT deal any melee damage to the target.
    }
};

struct npc_greyheart_spellbinder : public ScriptedAI
{
    npc_greyheart_spellbinder(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        instance = creature->GetInstanceScript();
        AddedBanish = false;
    }

    void Initialize()
    {
        Mindblast_Timer = urand(3000, 8000);
        Earthshock_Timer = urand(5000, 10000);
    }

    InstanceScript* instance;

    ObjectGuid leotherasGUID;

    uint32 Mindblast_Timer;
    uint32 Earthshock_Timer;

    bool AddedBanish;

    void Reset() override
    {
        Initialize();

        instance->SetGuidData(DATA_LEOTHERAS_EVENT_STARTER, ObjectGuid::Empty);
        Creature* leotheras = ObjectAccessor::GetCreature(*me, leotherasGUID);
        if (leotheras && leotheras->IsAlive())
            ENSURE_AI(boss_leotheras_the_blind, leotheras->AI())->CheckChannelers(/*false*/);
    }

    void JustEngagedWith(Unit* who) override
    {
        me->InterruptNonMeleeSpells(false);
        instance->SetGuidData(DATA_LEOTHERAS_EVENT_STARTER, who->GetGUID());
    }

    void JustAppeared() override
    {
        AddedBanish = false;
        Reset();
    }

    void CastChanneling()
    {
        if (!me->IsInCombat() && !me->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        {
            if (leotherasGUID)
            {
                Creature* leotheras = ObjectAccessor::GetCreature(*me, leotherasGUID);
                if (leotheras && leotheras->IsAlive())
                    DoCast(leotheras, BANISH_BEAM);
            }
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if (!leotherasGUID)
            leotherasGUID = instance->GetGuidData(DATA_LEOTHERAS);

        if (!me->IsInCombat() && instance->GetGuidData(DATA_LEOTHERAS_EVENT_STARTER))
        {
            if (Unit* victim = ObjectAccessor::GetUnit(*me, instance->GetGuidData(DATA_LEOTHERAS_EVENT_STARTER)))
                AttackStart(victim);
        }

        if (!UpdateVictim())
        {
            CastChanneling();
            return;
        }

        if (!instance->GetGuidData(DATA_LEOTHERAS_EVENT_STARTER))
        {
            EnterEvadeMode();
            return;
        }

        if (Mindblast_Timer <= diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                DoCast(target, SPELL_MINDBLAST);

            Mindblast_Timer = urand(10000, 15000);
        } else Mindblast_Timer -= diff;

        if (Earthshock_Timer <= diff)
        {
            Map::PlayerList const& PlayerList = me->GetMap()->GetPlayers();
            for (Map::PlayerList::const_iterator itr = PlayerList.begin(); itr != PlayerList.end(); ++itr)
            {
                if (Player* i_pl = itr->GetSource())
                {
                    bool isCasting = false;
                    for (uint8 i = 0; i < CURRENT_MAX_SPELL; ++i)
                        if (i_pl->GetCurrentSpell(i))
                            isCasting = true;

                    if (isCasting)
                    {
                        DoCast(i_pl, SPELL_EARTHSHOCK);
                        break;
                    }
                }
            }
            Earthshock_Timer = urand(8000, 15000);
        } else Earthshock_Timer -= diff;
        DoMeleeAttackIfReady();
    }
};

void AddSC_boss_leotheras_the_blind()
{
    RegisterSerpentshrineCavernCreatureAI(boss_leotheras_the_blind);
    RegisterSerpentshrineCavernCreatureAI(boss_leotheras_the_blind_demonform);
    RegisterSerpentshrineCavernCreatureAI(npc_greyheart_spellbinder);
    RegisterSerpentshrineCavernCreatureAI(npc_inner_demon);
}
