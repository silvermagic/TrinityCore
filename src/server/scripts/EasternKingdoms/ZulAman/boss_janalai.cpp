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
 * @file boss_janalai.cpp
 * @brief 祖阿曼副本 - 加纳莱Boss脚本模块
 *
 * 本模块实现了加纳莱Boss的战斗逻辑，包括：
 * - 火焰吐息技能
 * - 火焰炸弹机制（核心技能，全屏AOE）
 * - 孵化蛋机制（召唤孵化者孵化龙鹰幼崽）
 * - 狂暴机制
 *
 * 加纳莱是祖阿曼的第四个Boss，是一只龙鹰之神化身。
 * 战斗分为普通阶段和火焰炸弹阶段，Boss会定期召唤孵化者孵化龙鹰蛋。
 *
 * 特殊机制：
 * - 火焰炸弹：Boss传送至中央，在房间内随机放置40个炸弹，随后引爆
 * - 孵化蛋：Boss定期召唤2个孵化者，孵化者会走向蛋群并孵化龙鹰幼崽
 * - 35%血量：Boss会一次性孵化所有剩余的蛋
 */

/* ScriptData
SDName: Boss_Janalai
SD%Complete: 100
SDComment:
SDCategory: Zul'Aman
EndScriptData */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "SpellInfo.h"
#include "TemporarySummon.h"
#include "zulaman.h"

/**
 * @brief 对话和喊话枚举
 *
 * 定义加纳莱的各种对话ID
 */
enum Yells
{
    SAY_AGGRO                   = 0,  ///< 开战喊话
    SAY_FIRE_BOMBS              = 1,  ///< 火焰炸弹喊话
    SAY_SUMMON_HATCHER          = 2,  ///< 召唤孵化者喊话
    SAY_ALL_EGGS                = 3,  ///< 孵化所有蛋喊话
    SAY_BERSERK                 = 4,  ///< 狂暴喊话
    SAY_SLAY                    = 5,  ///< 击杀玩家
    SAY_DEATH                   = 6,  ///< 死亡喊话
    SAY_EVENT_STRANGERS         = 7,  ///< 事件对话-陌生人
    SAY_EVENT_FRIENDS           = 8   ///< 事件对话-朋友
};

/**
 * @brief 技能枚举
 *
 * 定义加纳莱使用的所有技能ID
 */
enum Spells
{
    // Jan'alai - 加纳莱技能
    SPELL_FLAME_BREATH          = 43140,  ///< 火焰吐息 - 前方锥形火焰伤害
    SPELL_FIRE_WALL             = 43113,  ///< 火焰墙 - 在炸弹阶段制造火焰墙
    SPELL_ENRAGE                = 44779,  ///< 狂乱 - 5分钟或25%血量时触发
    SPELL_SUMMON_PLAYERS        = 43097,  ///< 召唤玩家 - 将玩家传送到Boss位置
    SPELL_TELE_TO_CENTER        = 43098,  ///< 传送到中央 - Boss传送至房间中央
    SPELL_HATCH_ALL             = 43144,  ///< 孵化所有 - 孵化房间内所有蛋
    SPELL_BERSERK               = 45078,  ///< 狂暴 - 10分钟后进入狂暴

    // Fire Bob Spells - 火焰炸弹技能
    SPELL_FIRE_BOMB_CHANNEL     = 42621,  ///< 火焰炸弹引导 - 持续施法
    SPELL_FIRE_BOMB_THROW       = 42628,  ///< 火焰炸弹投掷 - 投掷视觉效果
    SPELL_FIRE_BOMB_DUMMY       = 42629,  ///< 火焰炸弹假人 - 炸弹视觉效果
    SPELL_FIRE_BOMB_DAMAGE      = 42630,  ///< 火焰炸弹伤害 - 爆炸伤害

    // Hatcher Spells - 孵化者技能
    SPELL_HATCH_EGG             = 42471,  ///< 孵化蛋 - 孵化龙鹰蛋
    SPELL_SUMMON_HATCHLING      = 42493,  ///< 召唤幼崽 - 从蛋中召唤龙鹰幼崽

    // Hatchling Spells - 幼崽技能
    SPELL_FLAMEBUFFET           = 43299   ///< 火焰打击 - 持续火焰伤害
};

/**
 * @brief 生物ID枚举
 *
 * 定义加纳莱战斗中涉及的NPC ID
 */
enum Creatures
{
    NPC_AMANI_HATCHER           = 23818,  ///< 阿曼尼孵化者NPC ID
    NPC_HATCHLING               = 23598,  ///< 龙鹰幼崽NPC ID
    NPC_EGG                     = 23817,  ///< 龙鹰蛋NPC ID
    NPC_FIRE_BOMB               = 23920   ///< 火焰炸弹NPC ID
};

/**
 * @brief 常量定义
 *
 * 定义火焰炸弹区域的尺寸
 */
const int area_dx = 44;  ///< 炸弹区域X轴尺寸
const int area_dy = 51;  ///< 炸弹区域Y轴尺寸

/**
 * @brief 加纳莱位置坐标
 *
 * Boss传送至中央时的位置
 */
float JanalainPos[1][3] =
{
    {-33.93f, 1149.27f, 19}
};

/**
 * @brief 火焰墙坐标
 *
 * 四个方向的火焰墙生成位置
 */
float FireWallCoords[4][4] =
{
    {-10.13f, 1149.27f, 19, 3.1415f},        ///< 北面火焰墙
    {-33.93f, 1123.90f, 19, 0.5f*3.1415f},   ///< 东面火焰墙
    {-54.80f, 1150.08f, 19, 0},               ///< 南面火焰墙
    {-33.93f, 1175.68f, 19, 1.5f*3.1415f}    ///< 西面火焰墙
};

/**
 * @brief 孵化者路径坐标
 *
 * 孵化者在两个蛋群区域的巡逻路径
 */
float hatcherway[2][5][3] =
{
    {   // 北侧蛋群路径
        {-87.46f, 1170.09f, 6},
        {-74.41f, 1154.75f, 6},
        {-52.74f, 1153.32f, 19},
        {-33.37f, 1172.46f, 19},
        {-33.09f, 1203.87f, 19}
    },
    {   // 南侧蛋群路径
        {-86.57f, 1132.85f, 6},
        {-73.94f, 1146.00f, 6},
        {-52.29f, 1146.51f, 19},
        {-33.57f, 1125.72f, 19},
        {-34.29f, 1095.22f, 19}
    }
};
class boss_janalai : public CreatureScript
{
    public:
        boss_janalai() : CreatureScript("boss_janalai") { }

        struct boss_janalaiAI : public BossAI
        {
            boss_janalaiAI(Creature* creature) : BossAI(creature, BOSS_JANALAI)
            {
                Initialize();
            }

            void Initialize()
            {
                FireBreathTimer = 8000;
                BombTimer = 30000;
                BombSequenceTimer = 1000;
                BombCount = 0;
                HatcherTimer = 10000;
                EnrageTimer = MINUTE * 5 * IN_MILLISECONDS;

                noeggs = false;
                isBombing = false;
                enraged = false;

                isFlameBreathing = false;

                for (uint8 i = 0; i < 40; ++i)
                    FireBombGUIDs[i].Clear();
            }

            uint32 FireBreathTimer;
            uint32 BombTimer;
            uint32 BombSequenceTimer;
            uint32 BombCount;
            uint32 HatcherTimer;
            uint32 EnrageTimer;

            bool noeggs;
            bool enraged;
            bool isBombing;

            bool isFlameBreathing;

            ObjectGuid FireBombGUIDs[40];

            void Reset() override
            {
                _Reset();

                Initialize();

                HatchAllEggs(1);
            }

            void JustDied(Unit* /*killer*/) override
            {
                Talk(SAY_DEATH);

                _JustDied();
            }

            void KilledUnit(Unit* /*victim*/) override
            {
                Talk(SAY_SLAY);
            }

            void JustEngagedWith(Unit* who) override
            {
                BossAI::JustEngagedWith(who);

                Talk(SAY_AGGRO);
            }

            void DamageDealt(Unit* target, uint32& damage, DamageEffectType /*damagetype*/) override
            {
                if (isFlameBreathing)
                {
                    if (!me->HasInArc(float(M_PI) / 6, target))
                        damage = 0;
                }
            }

            void FireWall()
            {
                uint8 WallNum;
                Creature* wall = nullptr;
                for (uint8 i = 0; i < 4; ++i)
                {
                    if (i == 0 || i == 2)
                        WallNum = 3;
                    else
                        WallNum = 2;

                    for (uint8 j = 0; j < WallNum; j++)
                    {
                        if (WallNum == 3)
                            wall = me->SummonCreature(NPC_FIRE_BOMB, FireWallCoords[i][0], FireWallCoords[i][1]+5*(j-1), FireWallCoords[i][2], FireWallCoords[i][3], TEMPSUMMON_TIMED_DESPAWN, 15s);
                        else
                            wall = me->SummonCreature(NPC_FIRE_BOMB, FireWallCoords[i][0]-2+4*j, FireWallCoords[i][1], FireWallCoords[i][2], FireWallCoords[i][3], TEMPSUMMON_TIMED_DESPAWN, 15s);
                        if (wall) wall->CastSpell(wall, SPELL_FIRE_WALL, true);
                    }
                }
            }

            void SpawnBombs()
            {
                float dx, dy;
                for (int i(0); i < 40; ++i)
                {
                    dx = float(irand(-area_dx/2, area_dx/2));
                    dy = float(irand(-area_dy/2, area_dy/2));

                    Creature* bomb = DoSpawnCreature(NPC_FIRE_BOMB, dx, dy, 0, 0, TEMPSUMMON_TIMED_DESPAWN, 15s);
                    if (bomb)
                        FireBombGUIDs[i] = bomb->GetGUID();
                }
                BombCount = 0;
            }

            bool HatchAllEggs(uint32 action) //1: reset, 2: isHatching all
            {
                std::list<Creature*> templist;

                GetCreatureListWithEntryInGrid(templist, me, NPC_EGG, 100.0f);

                //TC_LOG_ERROR("scripts", "Eggs {} at middle", templist.size());
                if (templist.empty())
                    return false;

                for (std::list<Creature*>::const_iterator i = templist.begin(); i != templist.end(); ++i)
                {
                    if (action == 1)
                       (*i)->SetDisplayId(10056);
                    else if (action == 2 &&(*i)->GetDisplayId() != 11686)
                       (*i)->CastSpell(*i, SPELL_HATCH_EGG, false);
                }
                return true;
            }

            void Boom()
            {
                std::list<Creature*> templist;

                GetCreatureListWithEntryInGrid(templist, me, NPC_FIRE_BOMB, 100.0f);

                for (std::list<Creature*>::const_iterator i = templist.begin(); i != templist.end(); ++i)
                {
                    (*i)->CastSpell(*i, SPELL_FIRE_BOMB_DAMAGE, true);
                    (*i)->RemoveAllAuras();
                }
            }

            void HandleBombSequence()
            {
                if (BombCount < 40)
                {
                    if (Unit* FireBomb = ObjectAccessor::GetUnit(*me, FireBombGUIDs[BombCount]))
                    {
                        FireBomb->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                        DoCast(FireBomb, SPELL_FIRE_BOMB_THROW, true);
                        FireBomb->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                    }
                    ++BombCount;
                    if (BombCount == 40)
                    {
                        BombSequenceTimer = 5000;
                    }
                    else
                        BombSequenceTimer = 100;
                }
                else
                {
                    Boom();
                    isBombing = false;
                    BombTimer = urand(20000, 40000);
                    me->RemoveAurasDueToSpell(SPELL_FIRE_BOMB_CHANNEL);
                    if (EnrageTimer <= 10000)
                        EnrageTimer = 0;
                    else
                        EnrageTimer -= 10000;
                }
            }

            void UpdateAI(uint32 diff) override
            {
                if (isFlameBreathing)
                {
                    if (!me->IsNonMeleeSpellCast(false))
                        isFlameBreathing = false;
                    else
                        return;
                }

                if (isBombing)
                {
                    if (BombSequenceTimer <= diff)
                        HandleBombSequence();
                    else
                        BombSequenceTimer -= diff;
                    return;
                }

                if (!UpdateVictim())
                    return;

                //enrage if under 25% hp before 5 min.
                if (!enraged && HealthBelowPct(25))
                    EnrageTimer = 0;

                if (EnrageTimer <= diff)
                {
                    if (!enraged)
                    {
                        DoCast(me, SPELL_ENRAGE, true);
                        enraged = true;
                        EnrageTimer = 300000;
                    }
                    else
                    {
                        Talk(SAY_BERSERK);
                        DoCast(me, SPELL_BERSERK, true);
                        EnrageTimer = 300000;
                    }
                }
                else
                    EnrageTimer -= diff;

                if (BombTimer <= diff)
                {
                    Talk(SAY_FIRE_BOMBS);

                    me->AttackStop();
                    me->GetMotionMaster()->Clear();
                    DoTeleportTo(JanalainPos[0][0], JanalainPos[0][1], JanalainPos[0][2]);
                    me->StopMoving();
                    DoCast(me, SPELL_FIRE_BOMB_CHANNEL, false);
                    //DoTeleportPlayer(me, JanalainPos[0][0], JanalainPos[0][1], JanalainPos[0][2], 0);
                    //DoCast(me, SPELL_TELE_TO_CENTER, true);

                    FireWall();
                    SpawnBombs();
                    isBombing = true;
                    BombSequenceTimer = 100;

                    //Teleport every Player into the middle
                    Map* map = me->GetMap();
                    if (!map->IsDungeon())
                        return;

                    Map::PlayerList const& PlayerList = map->GetPlayers();
                    for (Map::PlayerList::const_iterator i = PlayerList.begin(); i != PlayerList.end(); ++i)
                        if (Player* i_pl = i->GetSource())
                            if (i_pl->IsAlive())
                                DoTeleportPlayer(i_pl, JanalainPos[0][0] - 5 + rand32() % 10, JanalainPos[0][1] - 5 + rand32() % 10, JanalainPos[0][2], 0);
                    //DoCast(Temp, SPELL_SUMMON_PLAYERS, true) // core bug, spell does not work if too far
                    return;
                }
                else
                    BombTimer -= diff;

                if (!noeggs)
                {
                    if (HealthBelowPct(35))
                    {
                        Talk(SAY_ALL_EGGS);

                        me->AttackStop();
                        me->GetMotionMaster()->Clear();
                        DoTeleportTo(JanalainPos[0][0], JanalainPos[0][1], JanalainPos[0][2]);
                        me->StopMoving();
                        DoCast(me, SPELL_HATCH_ALL, false);
                        HatchAllEggs(2);
                        noeggs = true;
                    }
                    else if (HatcherTimer <= diff)
                    {
                        if (HatchAllEggs(0))
                        {
                            Talk(SAY_SUMMON_HATCHER);
                            me->SummonCreature(NPC_AMANI_HATCHER, hatcherway[0][0][0], hatcherway[0][0][1], hatcherway[0][0][2], 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 10s);
                            me->SummonCreature(NPC_AMANI_HATCHER, hatcherway[1][0][0], hatcherway[1][0][1], hatcherway[1][0][2], 0, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 10s);
                            HatcherTimer = 90000;
                        }
                        else
                            noeggs = true;
                    } else HatcherTimer -= diff;
                }

                DoMeleeAttackIfReady();

                if (FireBreathTimer <= diff)
                {
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                    {
                        me->AttackStop();
                        me->GetMotionMaster()->Clear();
                        DoCast(target, SPELL_FLAME_BREATH, false);
                        me->StopMoving();
                        isFlameBreathing = true;
                    }
                    FireBreathTimer = 8000;
                }
                else
                    FireBreathTimer -= diff;
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetZulAmanAI<boss_janalaiAI>(creature);
        }
};

class npc_janalai_firebomb : public CreatureScript
{
    public:
        npc_janalai_firebomb() : CreatureScript("npc_janalai_firebomb") { }

        struct npc_janalai_firebombAI : public ScriptedAI
        {
            npc_janalai_firebombAI(Creature* creature) : ScriptedAI(creature) { }

            void Reset() override { }

            void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
            {
                if (spellInfo->Id == SPELL_FIRE_BOMB_THROW)
                    DoCastSelf(SPELL_FIRE_BOMB_DUMMY, true);
            }

            void JustEngagedWith(Unit* /*who*/) override { }

            void AttackStart(Unit* /*who*/) override { }

            void MoveInLineOfSight(Unit* /*who*/) override { }

            void UpdateAI(uint32 /*diff*/) override { }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetZulAmanAI<npc_janalai_firebombAI>(creature);
        }
};

class npc_janalai_hatcher : public CreatureScript
{
    public:
        npc_janalai_hatcher() : CreatureScript("npc_janalai_hatcher") { }

        struct npc_janalai_hatcherAI : public ScriptedAI
        {
            npc_janalai_hatcherAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
                instance = creature->GetInstanceScript();
            }

            void Initialize()
            {
                waypoint = 0;
                isHatching = false;
                hasChangedSide = false;
                WaitTimer = 1;
                HatchNum = 0;
                side = false;
            }

            InstanceScript* instance;

            uint32 waypoint;
            uint32 HatchNum;
            uint32 WaitTimer;

            bool side;
            bool hasChangedSide;
            bool isHatching;

            void Reset() override
            {
                me->SetWalk(true);
                Initialize();
                side =(me->GetPositionY() < 1150);
            }

            bool HatchEggs(uint32 num)
            {
                std::list<Creature*> templist;

                GetCreatureListWithEntryInGrid(templist, me, NPC_EGG, 50.0f);

                //TC_LOG_ERROR("scripts", "Eggs {} at {}", templist.size(), side);

                for (std::list<Creature*>::const_iterator i = templist.begin(); i != templist.end() && num > 0; ++i)
                    if ((*i)->GetDisplayId() != 11686)
                    {
                        (*i)->CastSpell(*i, SPELL_HATCH_EGG, false);
                        num--;
                    }

                return num == 0;   // if num == 0, no more templist
            }

            void JustEngagedWith(Unit* /*who*/) override { }
            void AttackStart(Unit* /*who*/) override { }
            void MoveInLineOfSight(Unit* /*who*/) override { }

            void MovementInform(uint32, uint32) override
            {
                if (waypoint == 5)
                {
                    isHatching = true;
                    HatchNum = 1;
                    WaitTimer = 5000;
                }
                else
                    WaitTimer = 1;
            }

            void UpdateAI(uint32 diff) override
            {
                if (!instance || !(instance->GetBossState(BOSS_JANALAI) == IN_PROGRESS))
                {
                    me->DisappearAndDie();
                    return;
                }

                if (!isHatching)
                {
                    if (WaitTimer)
                    {
                        me->GetMotionMaster()->Clear();
                        me->GetMotionMaster()->MovePoint(0, hatcherway[side][waypoint][0], hatcherway[side][waypoint][1], hatcherway[side][waypoint][2]);
                        ++waypoint;
                        WaitTimer = 0;
                    }
                }
                else
                {
                    if (WaitTimer <= diff)
                    {
                        if (HatchEggs(HatchNum))
                        {
                            ++HatchNum;
                            WaitTimer = 10000;
                        }
                        else if (!hasChangedSide)
                        {
                            side = side ? 0 : 1;
                            isHatching = false;
                            waypoint = 3;
                            WaitTimer = 1;
                            hasChangedSide = true;
                        }
                        else
                            me->DisappearAndDie();

                    }
                    else
                        WaitTimer -= diff;
                }
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetZulAmanAI<npc_janalai_hatcherAI>(creature);
        }
};

class npc_janalai_hatchling : public CreatureScript
{
    public:
        npc_janalai_hatchling() : CreatureScript("npc_janalai_hatchling") { }

        struct npc_janalai_hatchlingAI : public ScriptedAI
        {
            npc_janalai_hatchlingAI(Creature* creature) : ScriptedAI(creature)
            {
                Initialize();
                instance = creature->GetInstanceScript();
            }

            void Initialize()
            {
                BuffetTimer = 7000;
            }

            InstanceScript* instance;
            uint32 BuffetTimer;

            void Reset() override
            {
                Initialize();
                if (me->GetPositionY() > 1150)
                    me->GetMotionMaster()->MovePoint(0, hatcherway[0][3][0] + rand32() % 4 - 2, 1150.0f + rand32() % 4 - 2, hatcherway[0][3][2]);
                else
                    me->GetMotionMaster()->MovePoint(0, hatcherway[1][3][0] + rand32() % 4 - 2, 1150.0f + rand32() % 4 - 2, hatcherway[1][3][2]);

                me->SetDisableGravity(true);
            }

            void JustEngagedWith(Unit* /*who*/) override {/*DoZoneInCombat();*/ }

            void UpdateAI(uint32 diff) override
            {
                if (!instance || !(instance->GetBossState(BOSS_JANALAI) == IN_PROGRESS))
                {
                    me->DisappearAndDie();
                    return;
                }

                if (!UpdateVictim())
                    return;

                if (BuffetTimer <= diff)
                {
                    DoCastVictim(SPELL_FLAMEBUFFET, false);
                    BuffetTimer = 10000;
                }
                else
                    BuffetTimer -= diff;

                DoMeleeAttackIfReady();
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetZulAmanAI<npc_janalai_hatchlingAI>(creature);
        }
};

class npc_janalai_egg : public CreatureScript
{
    public:
        npc_janalai_egg(): CreatureScript("npc_janalai_egg") { }

        struct npc_janalai_eggAI : public ScriptedAI
        {
            npc_janalai_eggAI(Creature* creature) : ScriptedAI(creature) { }

            void Reset() override { }

            void UpdateAI(uint32 /*diff*/) override { }

            void SpellHit(WorldObject* /*caster*/, SpellInfo const* spellInfo) override
            {
                if (spellInfo->Id == SPELL_HATCH_EGG)
                    DoCast(SPELL_SUMMON_HATCHLING);
            }
        };

        CreatureAI* GetAI(Creature* creature) const override
        {
            return GetZulAmanAI<npc_janalai_eggAI>(creature);
        }
};

void AddSC_boss_janalai()
{
    new boss_janalai();
    new npc_janalai_firebomb();
    new npc_janalai_hatcher();
    new npc_janalai_hatchling();
    new npc_janalai_egg();
}
