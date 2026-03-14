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
 * @file boss_mandokir.cpp
 * @brief 祖尔格拉布副本 - 曼多基尔(Boss Mandokir)战斗脚本
 *
 * 本模块实现了祖尔格拉布副本中血领主曼多基尔的AI逻辑：
 * - 骑乘状态和战斗状态的切换
 * - 主要技能: 冲锋、压制、恐惧、旋风斩、致死打击、狂暴
 * - 特殊机制: 观察玩家(被观察的玩家不能移动,否则会被冲锋)、杀敌升级
 * - 召唤奥根(Oghan): 宠物迅猛龙协助战斗
 * - 召唤锁链灵魂: 用于复活死亡的玩家
 */

#include "zulgurub.h"
#include "InstanceScript.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

/**
 * @brief Boss 对话文本ID
 */
enum Says
{
    SAY_AGGRO                 = 0,    /**< 开战对话 */
    SAY_DING_KILL             = 1,    /**< 杀敌升级对话 */
    SAY_WATCH                 = 2,    /**< 观察玩家对话 */
    SAY_WATCH_WHISPER         = 3,    /**< 观察玩家密语 */
    SAY_OHGAN_DEAD            = 4,    /**< 奥根死亡对话 */

    SAY_GRATS_JINDO           = 0     /**< 金度的祝贺对话 */
};

/**
 * @brief Boss 使用的法术ID
 */
enum Spells
{
    SPELL_CHARGE              = 24408, /**< 冲锋 - 冲向目标 */
    SPELL_OVERPOWER           = 24407, /**< 压制 - 招架后施放 */
    SPELL_FEAR                = 29321, /**< 恐惧 - 群体恐惧 */
    SPELL_WHIRLWIND           = 13736, /**< 旋风斩 - 触发15589 */
    SPELL_MORTAL_STRIKE       = 16856, /**< 致死打击 - 高伤害技能 */
    SPELL_FRENZY              = 24318, /**< 狂暴 - 奥根死后进入狂暴 */
    SPELL_WATCH               = 24314, /**< 观察 - 观察24315, 24316 */
    SPELL_WATCH_CHARGE        = 24315, /**< 观察冲锋 - 触发24316 */
    SPELL_LEVEL_UP            = 24312  /**< 升级 - 杀敌升级 */
};

/**
 * @brief Boss 事件ID
 */
enum Events
{
    EVENT_CHECK_SPEAKER       = 1,    /**< 检查演说者事件 */
    EVENT_CHECK_START,                /**< 检查开始事件 */
    EVENT_STARTED,                    /**< 已开始事件 */
    EVENT_OVERPOWER,                  /**< 压制事件 */
    EVENT_MORTAL_STRIKE,              /**< 致死打击事件 */
    EVENT_WHIRLWIND,                  /**< 旋风斩事件 */
    EVENT_WATCH_PLAYER,               /**< 观察玩家事件 */
    EVENT_CHARGE_PLAYER               /**< 冲锋玩家事件 */
};

/**
 * @brief 其他常量
 */
enum Misc
{
    MODEL_OHGAN_MOUNT         = 15271,    /**< 奥根坐骑模型ID */
    PATH_MANDOKIR             = 492861,   /**< 曼多基尔路径ID */
    POINT_MANDOKIR_END        = 24,       /**< 曼多基尔终点路径点 */
    CHAINED_SPIRT_COUNT       = 20        /**< 锁链灵魂数量 */
};

/**
 * @brief 锁链灵魂召唤位置
 *
 * 20个锁链灵魂的生成位置,用于复活死亡的玩家
 */
Position const PosSummonChainedSpirits[CHAINED_SPIRT_COUNT] =
{
    { -12167.17f, -1979.330f, 133.0992f, 2.268928f },
    { -12262.74f, -1953.394f, 133.5496f, 0.593412f },
    { -12176.89f, -1983.068f, 133.7841f, 2.129302f },
    { -12226.45f, -1977.933f, 132.7982f, 1.466077f },
    { -12204.74f, -1890.431f, 135.7569f, 4.415683f },
    { -12216.70f, -1891.806f, 136.3496f, 4.677482f },
    { -12236.19f, -1892.034f, 134.1041f, 5.044002f },
    { -12248.24f, -1893.424f, 134.1182f, 5.270895f },
    { -12257.36f, -1897.663f, 133.1484f, 5.462881f },
    { -12265.84f, -1903.077f, 133.1649f, 5.654867f },
    { -12158.69f, -1972.707f, 133.8751f, 2.408554f },
    { -12178.82f, -1891.974f, 134.1786f, 3.944444f },
    { -12193.36f, -1890.039f, 135.1441f, 4.188790f },
    { -12275.59f, -1932.845f, 134.9017f, 0.174533f },
    { -12273.51f, -1941.539f, 136.1262f, 0.314159f },
    { -12247.02f, -1963.497f, 133.9476f, 0.872665f },
    { -12238.68f, -1969.574f, 133.6273f, 1.134464f },
    { -12192.78f, -1982.116f, 132.6966f, 1.919862f },
    { -12210.81f, -1979.316f, 133.8700f, 1.797689f },
    { -12283.51f, -1924.839f, 133.5170f, 0.069813f }
};

/**
 * @brief 曼多基尔位置
 *
 * [0]: 初始位置
 * [1]: 战斗位置
 */
Position const PosMandokir[2] =
{
    { -12167.8f, -1927.25f, 153.73f, 3.76991f },
    { -12197.86f, -1949.392f, 130.2745f, 0.0f }
};

/**
 * @brief 血领主曼多基尔Boss AI
 *
 * 实现曼多基尔的战斗逻辑,包括:
 * - 开场序列: 骑乘状态,下马进入战斗
 * - 观察机制: 观察玩家,如果玩家移动则冲锋攻击
 * - 杀敌升级: 每击杀3个玩家升级一次
 * - 宠物奥根: 召唤迅猛龙协助战斗,死亡后Boss狂暴
 */
struct boss_mandokir : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    boss_mandokir(Creature* creature) : BossAI(creature, DATA_MANDOKIR)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     */
    void Initialize()
    {
        _killCount = 0;
    }

    /**
     * @brief 重置Boss状态
     *
     * 在战斗重置时调用:
     * - 如果在高处(初始位置),设置为免疫状态并安排检查开始事件
     * - 重置杀敌计数
     * - 重新骑乘奥根
     * - 清除所有召唤物
     */
    void Reset() override
    {
        // 如果在高处(初始位置)
        if (me->GetPositionZ() > 140.0f)
        {
            _Reset();
            Initialize();
            me->SetImmuneToAll(true);
            events.ScheduleEvent(EVENT_CHECK_START, 1s);

            // 如果演说者已死亡,复活它
            if (Creature* speaker = instance->GetCreature(DATA_VILEBRANCH_SPEAKER))
                if (!speaker->IsAlive())
                    speaker->Respawn(true);
        }
        summons.DespawnAll();
        me->Mount(MODEL_OHGAN_MOUNT);
    }

    /**
     * @brief Boss死亡时调用
     * @param killer 击杀者(未使用)
     *
     * 清除所有锁链灵魂,设置Boss状态为完成
     */
    void JustDied(Unit* /*killer*/) override
    {
        summons.DespawnEntry(NPC_CHAINED_SPIRT);
        instance->SetBossState(DATA_MANDOKIR, DONE);
        instance->SaveToDB();
    }

    /**
     * @brief 返回出生点时调用
     *
     * 解除免疫状态
     */
    void JustReachedHome() override
    {
        me->SetImmuneToAll(false);
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     *
     * 初始化战斗状态:
     * - 说开战对话
     * - 下马
     * - 安排所有技能事件
     * - 召唤奥根(宠物迅猛龙)
     * - 召唤锁链灵魂
     */
    void JustEngagedWith(Unit* who) override
    {
        BossAI::JustEngagedWith(who);
        events.ScheduleEvent(EVENT_OVERPOWER, 7s, 9s);
        events.ScheduleEvent(EVENT_MORTAL_STRIKE, 12s, 18s);
        events.ScheduleEvent(EVENT_WHIRLWIND, 24s, 30s);
        events.ScheduleEvent(EVENT_WATCH_PLAYER, 13s, 15s);
        events.ScheduleEvent(EVENT_CHARGE_PLAYER, 33s, 38s);
        me->SetHomePosition(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), me->GetOrientation());
        Talk(SAY_AGGRO);
        me->Dismount();

        // 召唤奥根(法术缺失) 临时方案
        me->SummonCreature(NPC_OHGAN, me->GetPositionX() - 3, me->GetPositionY(), me->GetPositionZ(), me->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 35s);

        // 召唤锁链灵魂
        for (int i = 0; i < CHAINED_SPIRT_COUNT; ++i)
            me->SummonCreature(NPC_CHAINED_SPIRT, PosSummonChainedSpirits[i], TEMPSUMMON_CORPSE_DESPAWN);

        DoZoneInCombat();
    }

    /**
     * @brief 击杀单位时调用
     * @param victim 被击杀的单位
     *
     * 监控玩家击杀数量,每击杀3个玩家升级一次
     */
    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() != TYPEID_PLAYER)
            return;

        if (++_killCount == 3)
        {
            Talk(SAY_DING_KILL);
            // 如果金度还活着,让他祝贺
            if (Creature* jindo = instance->GetCreature(DATA_JINDO))
                if (jindo->IsAlive())
                    jindo->AI()->Talk(SAY_GRATS_JINDO);
            DoCast(me, SPELL_LEVEL_UP, true);
            _killCount = 0;
        }
    }

    /**
     * @brief 召唤物死亡时调用
     * @param summon 死亡的召唤物
     * @param killer 击杀者(未使用)
     *
     * 如果奥根死亡,Boss进入狂暴状态
     */
    void SummonedCreatureDies(Creature* summon, Unit* /*killer*/) override
    {
        if (summon->GetEntry() == NPC_OHGAN)
        {
            DoCast(me, SPELL_FRENZY);
            Talk(SAY_OHGAN_DEAD);
        }
    }

    /**
     * @brief 移动到达目的地时调用
     * @param type 移动类型
     * @param id 路径点ID
     *
     * 处理开场移动序列
     */
    void MovementInform(uint32 type, uint32 id) override
    {
        if (type == WAYPOINT_MOTION_TYPE)
        {
            me->SetWalk(false);
            if (id == POINT_MANDOKIR_END)
            {
                me->SetHomePosition(PosMandokir[1]);
                me->GetMotionMaster()->MoveTargetedHome();
                instance->SetBossState(DATA_MANDOKIR, NOT_STARTED);
            }
        }
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 主要处理:
     * - 开场序列: 检查演说者状态,移动到战斗位置
     * - 战斗技能: 压制、致死打击、旋风斩、观察玩家、冲锋
     */
    void UpdateAI(uint32 diff) override
    {
        events.Update(diff);

        if (!UpdateVictim())
        {
            // 开场序列处理
            if (instance->GetBossState(DATA_MANDOKIR) == NOT_STARTED || instance->GetBossState(DATA_MANDOKIR) == SPECIAL)
            {
                while (uint32 eventId = events.ExecuteEvent())
                {
                    switch (eventId)
                    {
                        case EVENT_CHECK_START:
                            // 如果演说者已死亡,开始移动
                            if (instance->GetBossState(DATA_MANDOKIR) == SPECIAL)
                            {
                                me->GetMotionMaster()->MovePoint(0, PosMandokir[1]);
                                events.ScheduleEvent(EVENT_STARTED, 6s);
                            }
                            else
                                events.ScheduleEvent(EVENT_CHECK_START, 1s);
                            break;
                        case EVENT_STARTED:
                            // 开始沿路径移动
                            me->SetImmuneToAll(false);
                            me->GetMotionMaster()->MovePath(PATH_MANDOKIR, false);
                            break;
                        default:
                            break;
                    }
                }
            }
            return;
        }

        // 如果正在施法,不执行其他操作
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        while (uint32 eventId = events.ExecuteEvent())
        {
            switch (eventId)
            {
                case EVENT_OVERPOWER:
                    // 对当前目标施放压制
                    DoCastVictim(SPELL_OVERPOWER, true);
                    events.ScheduleEvent(EVENT_OVERPOWER, 6s, 12s);
                    break;

                case EVENT_MORTAL_STRIKE:
                    // 如果当前目标血量低于50%,施放致死打击
                    if (me->GetVictim() && me->EnsureVictim()->HealthBelowPct(50))
                        DoCastVictim(SPELL_MORTAL_STRIKE, true);
                    events.ScheduleEvent(EVENT_MORTAL_STRIKE, 12s, 18s);
                    break;

                case EVENT_WHIRLWIND:
                    // 施放旋风斩
                    DoCast(me, SPELL_WHIRLWIND);
                    events.ScheduleEvent(EVENT_WHIRLWIND, 22s, 26s);
                    break;

                case EVENT_WATCH_PLAYER:
                    // 观察一个随机玩家
                    if (Unit* player = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                    {
                        DoCast(player, SPELL_WATCH);
                        Talk(SAY_WATCH, player);
                    }
                    events.ScheduleEvent(EVENT_WATCH_PLAYER, 12s, 15s);
                    break;

                case EVENT_CHARGE_PLAYER:
                    // 对随机玩家施放冲锋
                    DoCast(SelectTarget(SelectTargetMethod::Random, 0, 40, true), SPELL_CHARGE);
                    events.ScheduleEvent(EVENT_CHARGE_PLAYER, 22s, 30s);
                    break;

                default:
                    break;
            }

            // 如果正在施法,退出循环
            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;
        }

        DoMeleeAttackIfReady();
    }

private:
    uint8 _killCount;  /**< 击杀玩家计数 */
};

/**
 * @brief 奥根(迅猛龙)使用的法术ID
 */
enum OhganSpells
{
    SPELL_SUNDERARMOR         = 24317  /**< 破甲 - 降低目标护甲 */
};

/**
 * @brief 奥根AI
 *
 * 曼多基尔的宠物迅猛龙,协助战斗:
 * - 使用破甲技能降低目标护甲
 * - 死亡后会让曼多基尔进入狂暴
 */
struct npc_ohgan : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_ohgan(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     */
    void Initialize()
    {
        _sunderArmorTimer = 5000;
    }

    /**
     * @brief 重置AI状态
     */
    void Reset() override
    {
        Initialize();
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 定期施放破甲技能
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UpdateVictim())
            return;

        if (_sunderArmorTimer <= diff)
        {
            DoCastVictim(SPELL_SUNDERARMOR, true);
            _sunderArmorTimer = urand(10000, 15000);
        }
        else
            _sunderArmorTimer -= diff;

        DoMeleeAttackIfReady();
    }

private:
    uint32 _sunderArmorTimer;  /**< 破甲技能计时器 */
};

/**
 * @brief 邪枝演说者使用的法术ID
 */
enum VilebranchSpells
{
    SPELL_DEMORALIZING_SHOUT  = 13730, /**< 挫志怒吼 - 降低周围敌人的攻击强度 */
    SPELL_CLEAVE              = 15284  /**< 顺劈斩 - 攻击面前多个目标 */
};

/**
 * @brief 邪枝演说者AI
 *
 * 开场Boss,玩家击杀后会触发曼多基尔开始移动
 */
struct npc_vilebranch_speaker : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_vilebranch_speaker(Creature* creature) : ScriptedAI(creature), _instance(creature->GetInstanceScript())
    {
        Initialize();
    }

    /**
     * @brief 初始化成员变量
     */
    void Initialize()
    {
        _demoralizingShoutTimer = urand(2000, 4000);
        _cleaveTimer = urand(5000, 8000);
    }

    /**
     * @brief 重置AI状态
     */
    void Reset() override
    {
        Initialize();
    }

    /**
     * @brief 死亡时调用
     * @param killer 击杀者(未使用)
     *
     * 设置Boss状态为特殊,触发曼多基尔开始移动
     */
    void JustDied(Unit* /*killer*/) override
    {
        _instance->SetBossState(DATA_MANDOKIR, SPECIAL);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差(毫秒)
     *
     * 定期施放挫志怒吼和顺劈斩
     */
    void UpdateAI(uint32 diff) override
    {
        // 如果没有目标,返回
        if (!UpdateVictim())
            return;

        if (_demoralizingShoutTimer <= diff)
        {
            DoCast(me, SPELL_DEMORALIZING_SHOUT);
            _demoralizingShoutTimer = urand(22000, 30000);
        }
        else
            _demoralizingShoutTimer -= diff;

        if (_cleaveTimer <= diff)
        {
            DoCastVictim(SPELL_CLEAVE, true);
            _cleaveTimer = urand(6000, 9000);
        }
        else
            _cleaveTimer -= diff;

        DoMeleeAttackIfReady();
    }

private:
    uint32 _demoralizingShoutTimer;  /**< 挫志怒吼计时器 */
    uint32 _cleaveTimer;             /**< 顺劈斩计时器 */
    InstanceScript* _instance;       /**< 副本脚本实例 */
};

/**
 * @brief 威胁凝视法术脚本
 *
 * 法术ID: 24314 - 威胁凝视
 *
 * 当玩家移动时(光环被移除不是通过过期或死亡),
 * Boss会冲锋攻击该玩家
 */
class spell_threatening_gaze : public AuraScript
{
    PrepareAuraScript(spell_threatening_gaze);

    /**
     * @brief 光环移除时调用
     * @param aurEff 光环效果(未使用)
     * @param mode 光环效果处理模式(未使用)
     *
     * 如果光环不是因为过期或死亡而被移除,
     * 让施法者对目标施放观察冲锋
     */
    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (Unit* caster = GetCaster())
            if (Unit* target = GetTarget())
                if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_EXPIRE && GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_DEATH)
                    caster->CastSpell(target, SPELL_WATCH_CHARGE);
    }

    /**
     * @brief 注册法术脚本
     */
    void Register() override
    {
        OnEffectRemove += AuraEffectRemoveFn(spell_threatening_gaze::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

/**
 * @brief 注册Boss和NPC脚本
 *
 * 注册以下脚本:
 * - boss_mandokir: 曼多基尔Boss
 * - npc_ohgan: 奥根(迅猛龙)
 * - npc_vilebranch_speaker: 邪枝演说者
 * - spell_threatening_gaze: 威胁凝视法术
 */
void AddSC_boss_mandokir()
{
    RegisterZulGurubCreatureAI(boss_mandokir);
    RegisterZulGurubCreatureAI(npc_ohgan);
    RegisterZulGurubCreatureAI(npc_vilebranch_speaker);
    RegisterSpellScript(spell_threatening_gaze);
}
