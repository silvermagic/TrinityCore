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
 * @file boss_lady_vashj.cpp
 * @brief 瓦斯琪女士BOSS战AI脚本
 *
 * 本文件实现了毒蛇神殿副本中最终BOSS瓦斯琪女士的战斗AI。
 * 该BOSS战分为三个阶段：
 * - 阶段1：常规战斗阶段，使用射击、冲击波、静电荷等技能
 * - 阶段2：护盾阶段（70%血量触发），BOSS无敌，需要玩家使用被污染的核心关闭4个护盾发生器
 * - 阶段3：最终阶段，BOSS失去护盾，召唤毒性孢子蝙蝠
 *
 * 核心机制：
 * - 阶段2需要通过传递被污染的核心来关闭护盾发生器
 * - 各种元素小怪在阶段2生成，需要团队分工处理
 * - 阶段3孢子蝙蝠会越来越频繁地刷新
 *
 * @see instance_serpent_shrine.cpp 关联的副本实例脚本
 */

/* ScriptData
SDName: Boss_Lady_Vashj
SD%Complete: 99
SDComment: Missing blizzlike Shield Generators coords
SDCategory: Coilfang Resevoir, Serpent Shrine Cavern
EndScriptData */

#include "ScriptMgr.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "serpent_shrine.h"
#include "Spell.h"
#include "TemporarySummon.h"
#include "WorldSession.h"

/**
 * @brief 瓦斯琪女士BOSS战相关枚举定义
 *
 * 包含BOSS的对话文本ID、技能ID和生物ID
 */
enum LadyVashj
{
    // 对话文本ID
    SAY_INTRO                   = 0,  // 引入对话
    SAY_AGGRO                   = 1,  // 开战对话
    SAY_PHASE1                  = 2,  // 阶段1对话
    SAY_PHASE2                  = 3,  // 阶段2对话
    SAY_PHASE3                  = 4,  // 阶段3对话
    SAY_BOWSHOT                 = 5,  // 射击对话
    SAY_SLAY                    = 6,  // 击杀玩家对话
    SAY_DEATH                   = 7,  // 死亡对话

    // 技能ID
    SPELL_SURGE                 = 38044,  // 涌动（附魔元素接触瓦斯琪时施放，增加伤害）
    SPELL_MULTI_SHOT            = 38310,  // 多重射击（攻击目标及其周围玩家）
    SPELL_SHOCK_BLAST           = 38509,  // 冲击波（自然伤害并眩晕5秒）
    SPELL_ENTANGLE              = 38316,  // 纠缠（范围根须效果）
    SPELL_STATIC_CHARGE_TRIGGER = 38280,  // 静电荷触发器（持续自然伤害光环）
    SPELL_FORKED_LIGHTNING      = 40088,  // 叉状闪电（阶段2主要技能）
    SPELL_SHOOT                 = 40873,  // 射击（普通远程攻击）
    SPELL_POISON_BOLT           = 40095,  // 毒性箭矢（被污染元素使用）
    SPELL_TOXIC_SPORES          = 38575,  // 毒性孢子（孢子蝙蝠召唤的地面效果）
    SPELL_MAGIC_BARRIER         = 38112,  // 魔法屏障（阶段2的无敌护盾）

    // 生物ID
    SHIED_GENERATOR_CHANNEL     = 19870,  // 护盾发生器通道（用于维持护盾）
    ENCHANTED_ELEMENTAL         = 21958,  // 附魔元素（阶段2召唤，需要阻止其接近BOSS）
    TAINTED_ELEMENTAL           = 22009,  // 被污染元素（击杀后掉落被污染的核心）
    COILFANG_STRIDER            = 22056,  // 盘牙行者（阶段2召唤的大怪）
    COILFANG_ELITE              = 22055,  // 盘牙精英（阶段2召唤的中怪）
    TOXIC_SPOREBAT              = 22140,  // 毒性孢子蝙蝠（阶段3召唤）
    TOXIC_SPORES_TRIGGER        = 22207   // 毒性孢子触发器（地面效果生物）
};

// 平台中心坐标（阶段2时BOSS传送到此位置）
#define MIDDLE_X                30.134f
#define MIDDLE_Y                -923.65f
#define MIDDLE_Z                42.9f

// 孢子蝙蝠生成位置
#define SPOREBAT_X              30.977156f
#define SPOREBAT_Y                  -925.297761f
#define SPOREBAT_Z                  77.176567f
#define SPOREBAT_O                  5.223932f

// 提示文本
#define TEXT_NOT_INITIALIZED          "Instance script not initialized"  // 实例脚本未初始化
#define TEXT_ALREADY_DEACTIVATED      "Already deactivated"              // 护盾已关闭

/**
 * @brief 元素生成位置数组
 *
 * 用于阶段2在平台周围8个位置生成附魔元素和被污染元素
 * 格式：[位置索引][X坐标, Y坐标, Z坐标, 朝向]
 */
float ElementPos[8][4] =
{
    {8.3f, -835.3f, 21.9f, 5.0f},
    {53.4f, -835.3f, 21.9f, 4.5f},
    {96.0f, -861.9f, 21.8f, 4.0f},
    {96.0f, -986.4f, 21.4f, 2.5f},
    {54.4f, -1010.6f, 22, 1.8f},
    {9.8f, -1012, 21.7f, 1.4f},
    {-35.0f, -987.6f, 21.5f, 0.8f},
    {-58.9f, -901.6f, 21.5f, 6.0f}
};

/**
 * @brief 附魔元素路径点位置数组
 *
 * 附魔元素生成后会先移动到最近的路径点，然后向平台中心移动
 * 格式：[路径点索引][X坐标, Y坐标, Z坐标]
 */
float ElementWPPos[8][3] =
{
    {71.700752f, -883.905884f, 41.097168f},
    {45.039848f, -868.022827f, 41.097015f},
    {14.585141f, -867.894470f, 41.097061f},
    {-25.415508f, -906.737732f, 41.097061f},
    {-11.801594f, -963.405884f, 41.097067f},
    {14.556657f, -979.051514f, 41.097137f},
    {43.466549f, -979.406677f, 41.097027f},
    {69.945908f, -964.663940f, 41.097054f}
};

/**
 * @brief 孢子蝙蝠巡逻路径点数组
 *
 * 阶段3召唤的孢子蝙蝠会在这些路径点之间随机移动
 * 格式：[路径点索引][X坐标, Y坐标, Z坐标]
 */
float SporebatWPPos[8][3] =
{
    {31.6f, -896.3f, 59.1f},
    {9.1f,  -913.9f, 56.0f},
    {5.2f,  -934.4f, 52.4f},
    {20.7f, -946.9f, 49.7f},
    {41.0f, -941.9f, 51.0f},
    {47.7f, -927.3f, 55.0f},
    {42.2f, -912.4f, 51.7f},
    {27.0f, -905.9f, 50.0f}
};

/**
 * @brief 盘牙精英生成位置数组
 *
 * 阶段2会在3个位置之一随机生成盘牙精英
 * 格式：[位置索引][X坐标, Y坐标, Z坐标, 朝向]
 */
float CoilfangElitePos[3][4] =
{
    {28.84f, -923.28f, 42.9f, 6.0f},
    {31.183281f, -953.502625f, 41.523602f, 1.640957f},
    {58.895180f, -923.124268f, 41.545307f, 3.152848f}
};

/**
 * @brief 盘牙行者生成位置数组
 *
 * 阶段2会在3个位置之一随机生成盘牙行者
 * 格式：[位置索引][X坐标, Y坐标, Z坐标, 朝向]
 */
float CoilfangStriderPos[3][4] =
{
    {66.427010f, -948.778503f, 41.262245f, 2.584220f},
    {7.513962f, -959.538208f, 41.300422f, 1.034629f},
    {-12.843201f, -907.798401f, 41.239620f, 6.087094f}
};

/**
 * @brief 护盾发生器通道位置数组
 *
 * 阶段2开始时在4个位置生成护盾发生器通道，为BOSS提供无敌护盾
 * 格式：[发生器索引][X坐标, Y坐标, Z坐标, 朝向]
 */
float ShieldGeneratorChannelPos[4][4] =
{
    {49.6262f, -902.181f, 43.0975f, 3.95683f},
    {10.988f, -901.616f, 42.5371f, 5.4373f},
    {10.3859f, -944.036f, 42.5446f, 0.779888f},
    {49.3126f, -943.398f, 42.5501f, 2.40174f}
};

/**
 * @brief 瓦斯琪女士BOSS AI结构体
 *
 * 继承自BossAI，实现了瓦斯琪女士的三阶段战斗逻辑
 *
 * 战斗阶段：
 * - 阶段1（Phase=1）：70%血量前，常规战斗，使用射击、冲击波、静电荷、纠缠等技能
 * - 阶段2（Phase=2）：70%-50%血量，BOSS无敌并召唤各种小怪，需要关闭4个护盾发生器
 * - 阶段3（Phase=3）：50%血量后，BOSS失去护盾，召唤孢子蝙蝠，使用阶段1技能
 */
struct boss_lady_vashj : public BossAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化BOSS的基本属性，设置初始状态为不可攻击
     */
    boss_lady_vashj(Creature* creature) : BossAI(creature, BOSS_LADY_VASHJ)
    {
        Initialize();
        instance = creature->GetInstanceScript();
        Intro = false;           // 是否已播放引入对话
        JustCreated = true;      // 是否刚创建（用于区分首次重置）
        CanAttack = false;       // 是否可以攻击
        creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE); // 设置不可攻击标志，只在创建时设置一次（团灭后不需要再次播放引入动画）
    }

    /**
     * @brief 初始化所有计时器和状态变量
     *
     * 重置所有战斗相关的计时器到初始值，
     * 在构造函数和Reset()中调用
     */
    void Initialize()
    {
        AggroTimer = 19000;                                      // 引入对话后到可攻击的等待时间
        ShockBlastTimer = 1 + rand32() % 60000;                  // 冲击波计时器（随机时间）
        EntangleTimer = 30000;                                   // 纠缠计时器
        StaticChargeTimer = 10000 + rand32() % 15000;            // 静电荷计时器
        ForkedLightningTimer = 2000;                             // 叉状闪电计时器
        CheckTimer = 15000;                                      // 检查计时器（用于近战范围检查或阶段转换）
        EnchantedElementalTimer = 5000;                          // 附魔元素召唤计时器
        TaintedElementalTimer = 50000;                           // 被污染元素召唤计时器
        CoilfangEliteTimer = 45000 + rand32() % 5000;            // 盘牙精英召唤计时器
        CoilfangStriderTimer = 60000 + rand32() % 10000;         // 盘牙行者召唤计时器
        SummonSporebatTimer = 10000;                             // 孢子蝙蝠召唤计时器
        SummonSporebatStaticTimer = 30000;                       // 孢子蝙蝠召唤基准时间（会逐渐减少）
        EnchantedElementalPos = 0;                               // 下一个附魔元素生成位置索引
        Phase = 0;                                               // 当前战斗阶段

        Entangle = false;                                        // 纠缠是否已施放（用于判断下一阶段）
    }

    InstanceScript* instance;                                    // 副本实例脚本指针

    ObjectGuid ShieldGeneratorChannel[4];                        // 4个护盾发生器通道的GUID

    uint32 AggroTimer;                                           // 引入对话后到可攻击的等待计时器
    uint32 ShockBlastTimer;                                      // 冲击波技能冷却计时器
    uint32 EntangleTimer;                                        // 纠缠技能冷却计时器
    uint32 StaticChargeTimer;                                    // 静电荷技能冷却计时器
    uint32 ForkedLightningTimer;                                 // 叉状闪电技能冷却计时器
    uint32 CheckTimer;                                           // 通用检查计时器
    uint32 EnchantedElementalTimer;                              // 附魔元素召唤冷却计时器
    uint32 TaintedElementalTimer;                                // 被污染元素召唤冷却计时器
    uint32 CoilfangEliteTimer;                                   // 盘牙精英召唤冷却计时器
    uint32 CoilfangStriderTimer;                                 // 盘牙行者召唤冷却计时器
    uint32 SummonSporebatTimer;                                  // 孢子蝙蝠召唤冷却计时器
    uint32 SummonSporebatStaticTimer;                            // 孢子蝙蝠召唤基准时间（动态调整）
    uint8 EnchantedElementalPos;                                 // 下一个附魔元素的生成位置索引（0-7循环）
    uint8 Phase;                                                 // 当前战斗阶段（0=未开始，1-3=战斗阶段）

    bool Entangle;                                               // 是否处于纠缠施放后的射击阶段
    bool Intro;                                                  // 是否已播放引入对话
    bool CanAttack;                                              // 是否可以进入战斗
    bool JustCreated;                                            // 是否刚创建（首次重置标记）

    /**
     * @brief 重置BOSS状态
     *
     * 当BOSS脱离战斗或团灭时调用，重置所有计时器和状态
     *
     * 功能：
     * - 重新初始化所有计时器
     * - 清理护盾发生器通道生物
     * - 设置尸体延迟消失时间（1小时）
     * - 区分首次重置和后续重置（首次不可攻击，后续可攻击）
     */
    void Reset() override
    {
        Initialize();

        if (JustCreated)
        {
            // 首次重置，保持不可攻击状态
            CanAttack = false;
            JustCreated = false;
        } else CanAttack = true;  // 团灭后重置，直接可攻击

        // 清理所有护盾发生器通道
        for (uint8 i = 0; i < 4; ++i)
        {
            if (ShieldGeneratorChannel[i])
            {
                if (Unit* remo = ObjectAccessor::GetUnit(*me, ShieldGeneratorChannel[i]))
                {
                    remo->setDeathState(JUST_DIED);  // 立即移除护盾发生器
                    ShieldGeneratorChannel[i].Clear();
                }
            }
        }

        _Reset();

        me->SetCorpseDelay(1000*60*60);  // 尸体保留1小时
    }

    /**
     * @brief 被污染元素死亡事件处理
     *
     * 当被污染元素死亡时调用，重置下一个被污染元素的召唤计时器
     * 确保下一个被污染元素在50秒后生成
     */
    void EventTaintedElementalDeath()
    {
        // 如果计时器还有较长时间，重置为50秒
        if (TaintedElementalTimer > 50000)
            TaintedElementalTimer = 50000;
    }

    /**
     * @brief 击杀玩家时调用
     * @param victim 被击杀的单位
     */
    void KilledUnit(Unit* /*victim*/) override
    {
        Talk(SAY_SLAY);  // 播放击杀对话
    }

    /**
     * @brief BOSS死亡时调用
     * @param killer 击杀者
     */
    void JustDied(Unit* /*killer*/) override
    {
        Talk(SAY_DEATH);  // 播放死亡对话

        _JustDied();
    }

    /**
     * @brief 开始战斗事件
     * @param who 首个攻击者
     *
     * 播放开战对话，设置阶段为1，进入战斗状态
     */
    void StartEvent(Unit* who)
    {
        Talk(SAY_AGGRO);  // 播放开战对话

        Phase = 1;  // 进入阶段1

        _JustEngagedWith(who);
    }

    /**
     * @brief 进入战斗时调用
     * @param who 攻击者
     *
     * 清理玩家背包中可能存在的旧被污染核心（防止阶段2作弊），
     * 然后开始战斗事件
     */
    void JustEngagedWith(Unit* who) override
    {
        // 移除所有玩家背包中的旧被污染核心，防止阶段2作弊
        Map::PlayerList const& PlayerList = me->GetMap()->GetPlayers();
        for (Map::PlayerList::const_iterator itr = PlayerList.begin(); itr != PlayerList.end(); ++itr)
            if (Player* player = itr->GetSource())
                player->DestroyItemCount(31088, 1, true);  // 31088是被污染核心的物品ID

        StartEvent(who);  // 开始战斗事件

        // 阶段2不主动攻击（BOSS在平台中心）
        if (Phase != 2)
            AttackStart(who);
    }

    /**
     * @brief 视线范围内移动检测
     * @param who 进入视线范围的单位
     *
     * 首次看到玩家时播放引入对话，
     * 如果玩家在攻击范围内且BOSS可攻击，则开始战斗
     */
    void MoveInLineOfSight(Unit* who) override

    {
        // 首次看到玩家，播放引入对话
        if (!Intro)
        {
            Intro = true;
            Talk(SAY_INTRO);
        }

        // 如果还不能攻击，直接返回
        if (!CanAttack)
            return;

        if (!who || me->GetVictim())
            return;

        // 检查是否可以攻击该目标
        if (me->CanCreatureAttack(who))
        {
            float attackRadius = me->GetAttackDistance(who);
            // 检查距离、高度差和视线
            if (me->IsWithinDistInMap(who, attackRadius) && me->GetDistanceZ(who) <= CREATURE_Z_ATTACK_RANGE && me->IsWithinLOSInMap(who))
            {
                // 如果还未进入战斗，开始战斗事件
                if (!me->IsInCombat())
                    StartEvent(who);

                // 阶段2不主动攻击
                if (Phase != 2)
                    AttackStart(who);
            }
        }
    }

    /**
     * @brief 施放射击或多重射击
     *
     * 随机选择射击或多重射击施放，有概率播放对话
     *
     * 使用时机：
     * - 阶段1和3：纠缠后或没有近战目标时
     * - 射击：对目标造成4097-5543物理伤害
     * - 多重射击：对目标及其周围4人造成6475-7525物理伤害
     */
    void CastShootOrMultishot()
    {
        switch (urand(0, 1))
        {
            case 0:
                // 射击：在阶段1和3使用，对目标造成4097-5543物理伤害
                DoCastVictim(SPELL_SHOOT);
                break;
            case 1:
                // 多重射击：在阶段1和3使用，对目标及其周围4人造成6475-7525物理伤害
                DoCastVictim(SPELL_MULTI_SHOT);
                break;
        }

        // 33%概率播放射击对话
        if (rand32() % 3)
        {
            Talk(SAY_BOWSHOT);
        }
    }

    /**
     * @brief 更新AI逻辑（每帧调用）
     * @param diff 距离上次调用的毫秒数
     *
     * 主循环函数，处理所有战斗逻辑：
     * - 引入对话后的可攻击计时
     * - 阶段1和3的技能施放
     * - 阶段转换
     * - 阶段2的小怪召唤
     * - 近战攻击和远程攻击选择
     *
     * @performance 此函数每帧调用，需保持高效
     */
    void UpdateAI(uint32 diff) override
    {
        // 引入对话后的可攻击计时处理
        if (!CanAttack && Intro)
        {
            if (AggroTimer <= diff)
            {
                CanAttack = true;
                me->RemoveUnitFlag(UNIT_FLAG_NON_ATTACKABLE);  // 移除不可攻击标志
                AggroTimer=19000;
            }
            else
            {
                AggroTimer -= diff;
                return;
            }
        }

        // 阶段2防护：如果没有目标且在战斗中，脱离战斗（防止玩家滥用机制）
        if (Phase == 2 && !me->GetVictim() && me->IsInCombat())
        {
            EnterEvadeMode();
            return;
        }

        // 没有目标则返回
        if (!UpdateVictim())
            return;

        // 阶段1和3的处理
        if (Phase == 1 || Phase == 3)
        {
            // 冲击波计时器处理
            if (ShockBlastTimer <= diff)
            {
                // 冲击波：在阶段1和3随机对目标施放，造成8325-9675自然伤害并眩晕5秒
                // 眩晕期间BOSS会转换目标攻击威胁列表中的下一人
                DoCastVictim(SPELL_SHOCK_BLAST);

                ShockBlastTimer = 1000 + rand32() % 14000;  // 随机冷却时间
            } else ShockBlastTimer -= diff;

            // 静电荷计时器处理
            if (StaticChargeTimer <= diff)
            {
                // 静电荷：在阶段1和3对随机玩家施放（同一时间只能有1个目标）
                // 造成2775-3225自然伤害，并对5码内所有玩家造成同样伤害，持续30秒，每1秒触发一次
                // 可被暗影斗篷、寒冰屏障、圣盾术移除，但不能被驱散
                Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 200, true);
                if (target && !target->HasAura(SPELL_STATIC_CHARGE_TRIGGER))
                    DoCast(target, SPELL_STATIC_CHARGE_TRIGGER);  // 每2秒触发一次，持续20秒

                StaticChargeTimer = 10000 + rand32() % 20000;
            } else StaticChargeTimer -= diff;

            // 纠缠计时器处理
            if (EntangleTimer <= diff)
            {
                if (!Entangle)
                {
                    // 纠缠：在阶段1和3施放，对BOSS周围15码内所有玩家施放纠缠根须
                    // 困住目标10秒，每2秒造成500伤害
                    // 不是魔法效果，不能被驱散，但可被暗影斗篷、自由祝福移除
                    DoCastVictim(SPELL_ENTANGLE);
                    Entangle = true;
                    EntangleTimer = 10000;  // 10秒后施放射击
                }
                else
                {
                    // 纠缠后施放射击或多重射击
                    CastShootOrMultishot();
                    Entangle = false;
                    EntangleTimer = 20000 + rand32() % 5000;  // 20-25秒后再次施放纠缠
                }
            } else EntangleTimer -= diff;

            // 阶段1特殊处理
            if (Phase == 1)
            {
                // 血量低于70%时进入阶段2
                if (HealthBelowPct(70))
                {
                    // 阶段2开始：瓦斯琪跑向平台中心并激活无敌护盾
                    Phase = 2;

                    me->GetMotionMaster()->Clear();
                    DoTeleportTo(MIDDLE_X, MIDDLE_Y, MIDDLE_Z);  // 传送到平台中心

                    // 召唤4个护盾发生器通道
                    for (uint8 i = 0; i < 4; ++i)
                        if (Creature* creature = me->SummonCreature(SHIED_GENERATOR_CHANNEL, ShieldGeneratorChannelPos[i][0],  ShieldGeneratorChannelPos[i][1],  ShieldGeneratorChannelPos[i][2],  ShieldGeneratorChannelPos[i][3], TEMPSUMMON_CORPSE_DESPAWN))
                            ShieldGeneratorChannel[i] = creature->GetGUID();

                    Talk(SAY_PHASE2);  // 播放阶段2对话
                }
            }
            // 阶段3特殊处理
            else
            {
                // 孢子蝙蝠召唤计时器处理
                if (SummonSporebatTimer <= diff)
                {
                    // 召唤毒性孢子蝙蝠
                    if (Creature* sporebat = me->SummonCreature(TOXIC_SPOREBAT, SPOREBAT_X, SPOREBAT_Y, SPOREBAT_Z, SPOREBAT_O, TEMPSUMMON_CORPSE_DESPAWN))
                        if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                            sporebat->AI()->AttackStart(target);

                    // 孢子蝙蝠召唤越来越快
                    if (SummonSporebatStaticTimer > 1000)
                        SummonSporebatStaticTimer -= 1000;  // 每次减少1秒

                    SummonSporebatTimer = SummonSporebatStaticTimer;

                    // 最少5秒召唤一次
                    if (SummonSporebatTimer < 5000)
                        SummonSporebatTimer = 5000;

                } else SummonSporebatTimer -= diff;
            }

            // 近战攻击
            DoMeleeAttackIfReady();

            // 检查计时器 - 用于检查是否有人在近战范围内
            if (CheckTimer <= diff)
            {
                bool inMeleeRange = false;
                // 遍历威胁列表，检查是否有目标在近战范围内
                for (auto* ref : me->GetThreatManager().GetUnsortedThreatList())
                {
                    Unit* target = ref->GetVictim();
                    if (target->IsWithinMeleeRange(me))  // 如果在近战范围内
                    {
                        inMeleeRange = true;
                        break;
                    }
                }

                // 如果没人近战，施放射击或多重射击
                if (!inMeleeRange)
                    CastShootOrMultishot();

                CheckTimer = 5000;  // 每5秒检查一次
            } else CheckTimer -= diff;
        }
        // 阶段2的处理
        else
        {
            // 叉状闪电计时器处理
            if (ForkedLightningTimer <= diff)
            {
                // 叉状闪电：阶段2持续施放，随机选择目标，对其前方约60度锥形范围内所有玩家
                // 造成2313-2687自然伤害
                Unit* target = SelectTarget(SelectTargetMethod::Random, 0);

                if (!target)
                    target = me->GetVictim();

                DoCast(target, SPELL_FORKED_LIGHTNING);

                ForkedLightningTimer = 2000 + rand32() % 6000;  // 2-8秒冷却
            } else ForkedLightningTimer -= diff;

            // 附魔元素召唤计时器处理
            if (EnchantedElementalTimer <= diff)
            {
                // 在平台周围8个位置循环召唤附魔元素
                me->SummonCreature(ENCHANTED_ELEMENTAL, ElementPos[EnchantedElementalPos][0], ElementPos[EnchantedElementalPos][1], ElementPos[EnchantedElementalPos][2], ElementPos[EnchantedElementalPos][3], TEMPSUMMON_CORPSE_DESPAWN);

                // 更新下一个生成位置（循环0-7）
                if (EnchantedElementalPos == 7)
                    EnchantedElementalPos = 0;
                else
                    ++EnchantedElementalPos;

                EnchantedElementalTimer = 10000 + rand32() % 5000;  // 10-15秒召唤一次
            } else EnchantedElementalTimer -= diff;

            // 被污染元素召唤计时器处理
            if (TaintedElementalTimer <= diff)
            {
                // 在8个位置随机召唤被污染元素
                uint32 pos = rand32() % 8;
                me->SummonCreature(TAINTED_ELEMENTAL, ElementPos[pos][0], ElementPos[pos][1], ElementPos[pos][2], ElementPos[pos][3], TEMPSUMMON_DEAD_DESPAWN);

                TaintedElementalTimer = 120000;  // 2分钟召唤一次（如果上一个被击杀则50秒）
            } else TaintedElementalTimer -= diff;

            // 盘牙精英召唤计时器处理
            if (CoilfangEliteTimer <= diff)
            {
                // 在3个位置随机召唤盘牙精英
                uint32 pos = rand32() % 3;
                Creature* coilfangElite = me->SummonCreature(COILFANG_ELITE, CoilfangElitePos[pos][0], CoilfangElitePos[pos][1], CoilfangElitePos[pos][2], CoilfangElitePos[pos][3], TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5s);
                if (coilfangElite)
                {
                    // 选择随机目标攻击
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        coilfangElite->AI()->AttackStart(target);
                    else if (me->GetVictim())
                        coilfangElite->AI()->AttackStart(me->GetVictim());
                }
                CoilfangEliteTimer = 45000 + rand32() % 5000;  // 45-50秒召唤一次
            } else CoilfangEliteTimer -= diff;

            // 盘牙行者召唤计时器处理
            if (CoilfangStriderTimer <= diff)
            {
                // 在3个位置随机召唤盘牙行者
                uint32 pos = rand32() % 3;
                if (Creature* CoilfangStrider = me->SummonCreature(COILFANG_STRIDER, CoilfangStriderPos[pos][0], CoilfangStriderPos[pos][1], CoilfangStriderPos[pos][2], CoilfangStriderPos[pos][3], TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5s))
                {
                    // 选择随机目标攻击
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                        CoilfangStrider->AI()->AttackStart(target);
                    else if (me->GetVictim())
                        CoilfangStrider->AI()->AttackStart(me->GetVictim());
                }
                CoilfangStriderTimer = 60000 + rand32() % 10000;  // 60-70秒召唤一次
            } else CoilfangStriderTimer -= diff;

            // 检查计时器 - 检查是否可以进入阶段3
            if (CheckTimer <= diff)
            {
                // 检查4个护盾发生器是否都已关闭
                if (instance->GetData(DATA_CANSTARTPHASE3))
                {
                    // 进入阶段3：将血量设置为50%
                    me->SetHealth(me->CountPctFromMaxHealth(50));

                    // 移除魔法屏障（无敌护盾）
                    me->RemoveAurasDueToSpell(SPELL_MAGIC_BARRIER);

                    Talk(SAY_PHASE3);  // 播放阶段3对话

                    Phase = 3;

                    // 追击坦克
                    me->GetMotionMaster()->MoveChase(me->GetVictim());
                }
                CheckTimer = 1000;  // 每1秒检查一次
            } else CheckTimer -= diff;
        }
    }
};

/**
 * @brief 附魔元素AI结构体
 *
 * 阶段2召唤的小怪，会从平台边缘移动到平台中心
 * 如果接触到瓦斯琪，会增加其5%的伤害
 * 玩家需要阻止其接近BOSS
 */
struct npc_enchanted_elemental : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_enchanted_elemental(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        instance = creature->GetInstanceScript();
    }

    /**
     * @brief 初始化移动状态和目标位置
     */
    void Initialize()
    {
        Move = 0;        // 移动计时器
        Phase = 1;       // 移动阶段（1=移向路径点，2=移向中心）

        // 初始化为第一个路径点坐标
        X = ElementWPPos[0][0];
        Y = ElementWPPos[0][1];
        Z = ElementWPPos[0][2];
    }

    InstanceScript* instance;     // 副本实例脚本指针
    uint32 Move;                  // 移动计时器（毫秒）
    uint32 Phase;                 // 移动阶段
    float X, Y, Z;                // 目标路径点坐标

    ObjectGuid VashjGUID;         // 瓦斯琪的GUID

    /**
     * @brief 重置时设置移动速度并查找最近路径点
     *
     * 将移动速度设置为正常速度的60%，使其缓慢向BOSS移动
     */
    void Reset() override
    {
        me->SetSpeedRate(MOVE_WALK, 0.6f);  // 行走速度60%
        me->SetSpeedRate(MOVE_RUN, 0.6f);   // 跑步速度60%
        Initialize();

        // 搜索最近的路径点（平台上方）
        for (uint32 i = 1; i < 8; ++i)
        {
            if (me->GetDistance(ElementWPPos[i][0], ElementWPPos[i][1], ElementWPPos[i][2]) < me->GetDistance(X, Y, Z))
            {
                X = ElementWPPos[i][0];
                Y = ElementWPPos[i][1];
                Z = ElementWPPos[i][2];
            }
        }

        VashjGUID = instance->GetGuidData(DATA_LADYVASHJ);  // 获取瓦斯琪的GUID
    }

    void JustEngagedWith(Unit* /*who*/) override { }

    void MoveInLineOfSight(Unit* /*who*/) override { }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次调用的毫秒数
     *
     * 控制附魔元素的移动逻辑：
     * - 阶段1：移动到最近的路径点
     * - 阶段2：移动到平台中心
     * - 接近中心时施放涌动技能增加BOSS伤害
     */
    void UpdateAI(uint32 diff) override
    {
        if (!VashjGUID)
            return;

        if (Move <= diff)
        {
            me->SetWalk(true);
            if (Phase == 1)
                me->GetMotionMaster()->MovePoint(0, X, Y, Z);  // 移向路径点
            if (Phase == 1 && me->IsWithinDist3d(X, Y, Z, 0.1f))
                Phase = 2;  // 到达路径点，进入阶段2
            if (Phase == 2)
            {
                me->GetMotionMaster()->MovePoint(0, MIDDLE_X, MIDDLE_Y, MIDDLE_Z);  // 移向中心
                Phase = 3;
            }
            if (Phase == 3)
            {
                me->GetMotionMaster()->MovePoint(0, MIDDLE_X, MIDDLE_Y, MIDDLE_Z);
                // 接近中心时施放涌动，增加瓦斯琪伤害
                if (me->IsWithinDist3d(MIDDLE_X, MIDDLE_Y, MIDDLE_Z, 3))
                    DoCast(me, SPELL_SURGE);
            }
            // 检查瓦斯琪状态，如果不在阶段2或死亡，自己死亡
            if (Creature* vashj = ObjectAccessor::GetCreature(*me, VashjGUID))
                if (!vashj->IsInCombat() || ENSURE_AI(boss_lady_vashj, vashj->AI())->Phase != 2 || vashj->isDead())
                    me->KillSelf();
            Move = 1000;  // 每1秒检查一次
        } else Move -= diff;
    }
};

/**
 * @brief 被污染元素AI结构体
 *
 * 阶段2召唤的特殊小怪，生命值约7900，不会移动
 * 会对随机目标发射毒性箭矢，造成3000自然伤害，并施放持续伤害效果（每2秒2000伤害）
 * 击杀后会掉落被污染的核心，用于关闭护盾发生器
 * 会频繁切换目标或锁定单个玩家
 */
struct npc_tainted_elemental : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_tainted_elemental(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        instance = creature->GetInstanceScript();
    }

    /**
     * @brief 初始化计时器
     */
    void Initialize()
    {
        PoisonBoltTimer = 5000 + rand32() % 5000;  // 毒性箭矢计时器（5-10秒）
        DespawnTimer = 30000;                       // 自动消失计时器（30秒）
    }

    InstanceScript* instance;         // 副本实例脚本指针

    uint32 PoisonBoltTimer;           // 毒性箭矢冷却计时器
    uint32 DespawnTimer;              // 自动消失计时器

    void Reset() override
    {
        Initialize();
    }

    /**
     * @brief 死亡时通知瓦斯琪重置召唤计时器
     * @param killer 击杀者
     */
    void JustDied(Unit* /*killer*/) override
    {
        // 通知瓦斯琪，以便下一个被污染元素在50秒后生成
        if (Creature* vashj = ObjectAccessor::GetCreature((*me), instance->GetGuidData(DATA_LADYVASHJ)))
            ENSURE_AI(boss_lady_vashj, vashj->AI())->EventTaintedElementalDeath();
    }

    /**
     * @brief 进入战斗时添加少量威胁值
     * @param who 攻击者
     */
    void JustEngagedWith(Unit* who) override
    {
        AddThreat(who, 0.1f);
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次调用的毫秒数
     *
     * 定期发射毒性箭矢，30秒后自动消失
     */
    void UpdateAI(uint32 diff) override
    {
        // 毒性箭矢计时器处理
        if (PoisonBoltTimer <= diff)
        {
            Unit* target = SelectTarget(SelectTargetMethod::Random, 0);

            // 对30码内随机目标发射毒性箭矢
            if (target && target->IsWithinDistInMap(me, 30))
                DoCast(target, SPELL_POISON_BOLT);

            PoisonBoltTimer = 5000 + rand32() % 5000;  // 5-10秒冷却
        } else PoisonBoltTimer -= diff;

        // 自动消失计时器处理
        if (DespawnTimer <= diff)
        {
            // 调用取消召唤函数
            me->setDeathState(DEAD);

            // 防止崩溃的重置计时器
            DespawnTimer = 1000;
        } else DespawnTimer -= diff;
    }
};

/**
 * @brief 毒性孢子蝙蝠AI结构体
 *
 * 阶段3召唤的飞行生物，在平台上方随机巡逻
 * 定期向随机玩家位置投掷毒性孢子，造成持续自然伤害
 * 当瓦斯琪死亡或离开阶段3时自动消失
 */
struct npc_toxic_sporebat : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_toxic_sporebat(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        instance = creature->GetInstanceScript();
        EnterEvadeMode();
    }

    /**
     * @brief 初始化计时器
     */
    void Initialize()
    {
        MovementTimer = 0;       // 移动计时器
        ToxicSporeTimer = 5000;  // 毒性孢子计时器（未使用）
        BoltTimer = 5500;        // 毒性箭矢计时器
        CheckTimer = 1000;       // 检查计时器
    }

    InstanceScript* instance;      // 副本实例脚本指针

    uint32 MovementTimer;          // 移动计时器
    uint32 ToxicSporeTimer;        // 毒性孢子计时器
    uint32 BoltTimer;              // 毒性箭矢计时器
    uint32 CheckTimer;             // 检查计时器

    /**
     * @brief 重置时设置重力禁用和阵营
     */
    void Reset() override
    {
        me->SetDisableGravity(true);      // 禁用重力，允许飞行
        me->SetFaction(FACTION_MONSTER);  // 设置为怪物阵营
        Initialize();
    }

    void MoveInLineOfSight(Unit* /*who*/) override
    {
    }

    /**
     * @brief 移动完成通知
     * @param type 移动类型
     * @param id 移动点ID
     *
     * 当到达移动点时重置移动计时器
     */
    void MovementInform(uint32 type, uint32 id) override
    {
        if (type != POINT_MOTION_TYPE)
            return;

        if (id == 1)
            MovementTimer = 0;  // 重置移动计时器，准备下一次移动
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次调用的毫秒数
     *
     * 控制孢子蝙蝠的随机移动和毒性孢子施放
     */
    void UpdateAI(uint32 diff) override
    {
        // 随机移动处理
        if (MovementTimer <= diff)
        {
            uint32 rndpos = rand32() % 8;
            me->GetMotionMaster()->MovePoint(1, SporebatWPPos[rndpos][0], SporebatWPPos[rndpos][1], SporebatWPPos[rndpos][2]);
            MovementTimer = 6000;  // 6秒后移动到下一个位置
        } else MovementTimer -= diff;

        // 毒性孢子施放处理
        if (BoltTimer <= diff)
        {
            if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
            {
                // 在目标位置召唤毒性孢子触发器
                if (Creature* trig = me->SummonCreature(TOXIC_SPORES_TRIGGER, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN, 30s))
                {
                    trig->SetFaction(FACTION_MONSTER);
                    // 施放毒性孢子法术，对站在地面的玩家造成每秒2775-3225自然伤害
                    trig->CastSpell(trig, SPELL_TOXIC_SPORES, true);
                }
            }
            BoltTimer = 10000 + rand32() % 5000;  // 10-15秒冷却
        }
        else BoltTimer -= diff;

        // 检查计时器处理
        if (CheckTimer <= diff)
        {
            // 检查瓦斯琪是否死亡或不在阶段3
            Unit* Vashj = ObjectAccessor::GetUnit(*me, instance->GetGuidData(DATA_LADYVASHJ));
            if (!Vashj || !Vashj->IsAlive() || ENSURE_AI(boss_lady_vashj, Vashj->ToCreature()->AI())->Phase != 3)
            {
                // 变为友好阵营并消失
                me->SetFaction(FACTION_FRIENDLY);
                me->DespawnOrUnsummon();
                return;
            }

            CheckTimer = 1000;  // 每1秒检查一次
        }
        else
            CheckTimer -= diff;
    }
};

/**
 * @brief 护盾发生器通道AI结构体
 *
 * 阶段2开始时召唤的不可见生物，用于为瓦斯琪提供魔法屏障（无敌护盾）
 * 每个护盾发生器对应一个护盾发生器游戏对象，玩家需要使用被污染的核心关闭它们
 */
struct npc_shield_generator_channel : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_shield_generator_channel(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        instance = creature->GetInstanceScript();
    }

    /**
     * @brief 初始化计时器和状态
     */
    void Initialize()
    {
        CheckTimer = 0;  // 检查计时器
        Cast = false;    // 是否已施放护盾
    }

    InstanceScript* instance;   // 副本实例脚本指针
    uint32 CheckTimer;          // 检查计时器
    bool Cast;                  // 是否已施放护盾法术

    /**
     * @brief 重置时设置为不可见模型
     */
    void Reset() override
    {
        Initialize();
        me->SetDisplayId(11686);  // 设置为不可见模型
    }

    void MoveInLineOfSight(Unit* /*who*/) override { }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次调用的毫秒数
     *
     * 持续为瓦斯琪施放魔法屏障护盾
     */
    void UpdateAI(uint32 diff) override
    {
        if (CheckTimer <= diff)
        {
            Unit* vashj = ObjectAccessor::GetUnit(*me, instance->GetGuidData(DATA_LADYVASHJ));

            if (vashj && vashj->IsAlive())
            {
                // 开始视觉通道效果
                if (!Cast || !vashj->HasAura(SPELL_MAGIC_BARRIER))
                {
                    DoCast(vashj, SPELL_MAGIC_BARRIER, true);  // 施放魔法屏障护盾
                    Cast = true;
                }
            }
            CheckTimer = 1000;  // 每1秒检查一次
        } else CheckTimer -= diff;
    }
};

/**
 * @brief 被污染核心物品脚本
 *
 * 实现被污染核心物品的使用逻辑，用于关闭护盾发生器
 * 玩家可以将核心传递给其他玩家（投掷）
 */
class item_tainted_core : public ItemScript
{
public:
    /**
     * @brief 构造函数
     */
    item_tainted_core() : ItemScript("item_tainted_core") { }

    /**
     * @brief 物品使用事件处理
     * @param player 使用物品的玩家
     * @param item 物品对象
     * @param targets 施法目标
     * @return 是否消耗物品使用次数
     *
     * 功能：
     * - 对护盾发生器使用：关闭护盾发生器并移除核心
     * - 对玩家使用：将核心传递给目标玩家
     * - 对生物使用：不允许
     */
    bool OnUse(Player* player, Item* /*item*/, SpellCastTargets const& targets) override
    {
        InstanceScript* instance = player->GetInstanceScript();
        if (!instance)
        {
            player->GetSession()->SendNotification(TEXT_NOT_INITIALIZED);
            return true;
        }

        Creature* vashj = ObjectAccessor::GetCreature((*player), instance->GetGuidData(DATA_LADYVASHJ));
        // 只在阶段2允许使用
        if (vashj && (ENSURE_AI(boss_lady_vashj, vashj->AI())->Phase == 2))
        {
            if (GameObject* gObj = targets.GetGOTarget())
            {
                uint32 identifier;
                uint8 channelIdentifier;
                // 根据游戏对象ID确定护盾发生器编号
                switch (gObj->GetEntry())
                {
                    case 185052:  // 护盾发生器1
                        identifier = DATA_SHIELDGENERATOR1;
                        channelIdentifier = 0;
                        break;
                    case 185053:  // 护盾发生器2
                        identifier = DATA_SHIELDGENERATOR2;
                        channelIdentifier = 1;
                        break;
                    case 185051:  // 护盾发生器3
                        identifier = DATA_SHIELDGENERATOR3;
                        channelIdentifier = 2;
                        break;
                    case 185054:  // 护盾发生器4
                        identifier = DATA_SHIELDGENERATOR4;
                        channelIdentifier = 3;
                        break;
                    default:
                        return true;
                }

                // 检查护盾发生器是否已关闭
                if (instance->GetData(identifier))
                {
                    player->GetSession()->SendNotification(TEXT_ALREADY_DEACTIVATED);
                    return true;
                }

                // 获取并移除对应的护盾发生器通道
                if (Unit* channel = ObjectAccessor::GetCreature(*vashj, ENSURE_AI(boss_lady_vashj, vashj->AI())->ShieldGeneratorChannel[channelIdentifier]))
                    channel->setDeathState(JUST_DIED);  // 调用取消召唤

                // 设置护盾发生器已关闭
                instance->SetData(identifier, 1);

                // 移除核心物品
                player->DestroyItemCount(31088, 1, true);
                return true;
            }
            else if (targets.GetUnitTarget()->GetTypeId() == TYPEID_UNIT)
                return false;  // 对生物使用，不允许
            else if (targets.GetUnitTarget()->GetTypeId() == TYPEID_PLAYER)
            {
                // 对玩家使用，传递核心
                player->DestroyItemCount(31088, 1, true);
                player->CastSpell(targets.GetUnitTarget(), 38134, true);  // 投掷核心法术
                return true;
            }
        }
        return true;
    }

};

void AddSC_boss_lady_vashj()
{
    RegisterSerpentshrineCavernCreatureAI(boss_lady_vashj);
    RegisterSerpentshrineCavernCreatureAI(npc_enchanted_elemental);
    RegisterSerpentshrineCavernCreatureAI(npc_tainted_elemental);
    RegisterSerpentshrineCavernCreatureAI(npc_toxic_sporebat);
    RegisterSerpentshrineCavernCreatureAI(npc_shield_generator_channel);
    new item_tainted_core();
}
