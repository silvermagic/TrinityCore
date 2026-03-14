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
 * @file boss_priestess_delrissa.cpp
 * @brief 魔导师平台副本第三BOSS - 女祭司德莉萨及其手下AI脚本
 *
 * 本模块实现了魔导师平台副本中女祭司德莉萨及其8个随机手下的战斗逻辑。
 * 德莉萨是一个牧师职业BOSS，每次战斗会随机召唤4个手下。
 *
 * 主要功能：
 * - 女祭司德莉萨的AI逻辑（牧师技能）
 * - 8个可能的手下的AI实现：
 *   - Kagani Nightstrike（盗贼）
 *   - Ellris Duskhallow（术士）
 *   - Eramas Brightblaze（武僧）
 *   - Yazzai（法师）
 *   - Warlord Salaris（战士）
 *   - Garaxxas（猎人）
 *   - Apoko（萨满）
 *   - Zelfan（工程师）
 * - 手下死亡时的特殊对话
 * - 玩家死亡时的嘲讽对话
 *
 * 战斗特点：
 * - 每次战斗随机选择4个手下
 * - 德莉萨会治疗和保护手下
 * - 手下之间会互相协助
 * - 德莉萨和手下都可以被单独击杀
 *
 * @note 当前完成度65%，缺少英雄难度支持，需要进一步测试
 */

/* ScriptData
SDName: Boss_Priestess_Delrissa
SD%Complete: 65
SDComment: No Heroic support yet. Needs further testing. Several scripts for pets disabled, not seem to require any special script.
SDCategory: Magister's Terrace
EndScriptData */

#include "ScriptMgr.h"
#include "magisters_terrace.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

/**
 * @brief 对话结构体
 *
 * 用于存储对话ID
 */
struct Speech
{
    int32 id;  ///< 对话ID
};

/**
 * @brief 手下死亡时的对话数组
 *
 * 德莉萨在每个手下死亡时会喊出不同的对话
 */
static Speech LackeyDeath[]=
{
    {1},
    {2},
    {3},
    {4},
};

/**
 * @brief 玩家死亡时的嘲讽对话数组
 *
 * 德莉萨在玩家死亡时会随机喊出不同的嘲讽对话
 */
static Speech PlayerDeath[]=
{
    {5},
    {6},
    {7},
    {8},
    {9},
};

/**
 * @brief 德莉萨的喊话枚举
 *
 * 定义了德莉萨的对话ID
 */
enum Yells
{
    SAY_AGGRO               = 0,
    SAY_DEATH               = 10,
};

/**
 * @brief 法术ID枚举
 *
 * 定义了德莉萨及其手下使用的所有法术
 */
enum Spells
{
    SPELL_DISPEL_MAGIC          = 27609,
    SPELL_FLASH_HEAL            = 17843,
    SPELL_SW_PAIN_NORMAL        = 14032,
    SPELL_SW_PAIN_HEROIC        = 15654,
    SPELL_SHIELD                = 44291,
    SPELL_RENEW_NORMAL          = 44174,
    SPELL_RENEW_HEROIC          = 46192,

    // Apoko
    SPELL_WINDFURY_TOTEM        = 27621,
    SPELL_WAR_STOMP             = 46026,
    SPELL_PURGE                 = 27626,
    SPELL_LESSER_HEALING_WAVE   = 44256,
    SPELL_FROST_SHOCK           = 21401,
    SPELL_FIRE_NOVA_TOTEM       = 44257,
    SPELL_EARTHBIND_TOTEM       = 15786
};

/**
 * @brief 杂项常量枚举
 *
 * 定义最大同时活动的手下数量
 */
enum Misc
{
    MAX_ACTIVE_LACKEY       = 4  ///< 最大同时活动的手下数量
};

const float fOrientation = 4.98f;    ///< 手下朝向角度
const float fZLocation = -19.921f;   ///< 手下Z轴位置

/**
 * @brief 手下生成位置数组
 *
 * 定义了4个手下的生成坐标（X, Y）
 */
float LackeyLocations[4][2]=
{
    {123.77f, 17.6007f},
    {131.731f, 15.0827f},
    {121.563f, 15.6213f},
    {129.988f, 17.2355f},
};

/**
 * @brief 手下生物ID数组
 *
 * 包含所有8个可能的手下的生物ID
 */
const uint32 m_auiAddEntries[] =
{
    24557,                                                  // Kagani Nightstrike（盗贼）
    24558,                                                  // Elris Duskhallow（术士）
    24554,                                                  // Eramas Brightblaze（武僧）
    24561,                                                  // Yazzaj（法师）
    24559,                                                  // Warlord Salaris（战士）
    24555,                                                  // Garaxxas（猎人）
    24553,                                                  // Apoko（萨满）
    24556,                                                  // Zelfan（工程师）
};

/**
 * @brief 女祭司德莉萨BOSS脚本
 *
 * 实现了德莉萨的主要AI逻辑，包括：
 * - 牧师职业技能（治疗、护盾、驱散、暗言术：痛）
 * - 手下的生成和管理
 * - 死亡和击杀玩家的对话
 * - 与手下的协作战斗
 */
class boss_priestess_delrissa : public CreatureScript
{
public:
    boss_priestess_delrissa() : CreatureScript("boss_priestess_delrissa") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return AI实例指针
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetMagistersTerraceAI<boss_priestess_delrissaAI>(creature);
    }

    /**
     * @brief 女祭司德莉萨AI结构体
     *
     * 继承自ScriptedAI，实现德莉萨的所有战斗逻辑
     */
    struct boss_priestess_delrissaAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化AI，设置副本脚本，清空手下列表
         */
        boss_priestess_delrissaAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            instance = creature->GetInstanceScript();
            LackeyEntryList.clear();
        }

        /**
         * @brief 初始化成员变量
         *
         * 重置所有计时器和计数器
         */
        void Initialize()
        {
            PlayersKilled = 0;

            HealTimer = 15000;      // 治疗计时器
            RenewTimer = 10000;     // 恢复计时器
            ShieldTimer = 2000;     // 护盾计时器
            SWPainTimer = 5000;     // 暗言术：痛计时器
            DispelTimer = 7500;     // 驱散计时器
            ResetTimer = 5000;      // 脱战检查计时器
        }

        InstanceScript* instance;                     ///< 副本脚本实例指针

        std::vector<uint32> LackeyEntryList;          ///< 手下ID列表
        ObjectGuid m_auiLackeyGUID[MAX_ACTIVE_LACKEY]; ///< 手下GUID数组

        uint8 PlayersKilled;                          ///< 已击杀玩家数量

        uint32 HealTimer;                             ///< 快速治疗计时器
        uint32 RenewTimer;                            ///< 恢复计时器
        uint32 ShieldTimer;                           ///< 真言术：盾计时器
        uint32 SWPainTimer;                           ///< 暗言术：痛计时器
        uint32 DispelTimer;                           ///< 驱散魔法计时器
        uint32 ResetTimer;                            ///< 脱战检查计时器

        /**
         * @brief 重置BOSS状态
         *
         * 初始化变量并重新生成手下
         */
        void Reset() override
        {
            Initialize();

            InitializeLackeys();
        }

        /**
         * @brief 到达初始位置时调用
         *
         * 德莉萨脱战返回初始位置时，设置BOSS状态为失败
         */
        //this mean she at some point evaded
        void JustReachedHome() override
        {
            instance->SetBossState(DATA_PRIESTESS_DELRISSA, FAIL);
        }

        /**
         * @brief 进入战斗时调用
         * @param who 进入战斗的目标
         *
         * 喊出战斗对话，让所有未战斗的手下进入战斗，设置BOSS状态为进行中
         */
        void JustEngagedWith(Unit* who) override
        {
            Talk(SAY_AGGRO);

            for (uint8 i = 0; i < MAX_ACTIVE_LACKEY; ++i)
                if (Unit* pAdd = ObjectAccessor::GetUnit(*me, m_auiLackeyGUID[i]))
                    if (!pAdd->IsEngaged())
                        AddThreat(who, 0.0f, pAdd);

            instance->SetBossState(DATA_PRIESTESS_DELRISSA, IN_PROGRESS);
        }

        /**
         * @brief 初始化手下
         *
         * 生成4个随机手下：
         * - 首次调用时，从8个可能的手手中随机选择4个
         * - 后续调用时（如脱战后），重新生成之前选择的手下
         *
         * 手下生成逻辑：
         * 1. 首次调用：从m_auiAddEntries中随机选择4个ID
         * 2. 在指定位置生成手下
         * 3. 存储手下的GUID用于后续引用
         *
         * @note 如果德莉萨已死亡，不执行生成逻辑
         */
        void InitializeLackeys()
        {
            //can be called if Creature are dead, so avoid
            if (!me->IsAlive())
                return;

            uint8 j = 0;

            //it's empty, so first time
            if (LackeyEntryList.empty())
            {
                //pre-allocate size for speed
                LackeyEntryList.resize((sizeof(m_auiAddEntries) / sizeof(uint32)));

                //fill vector array with entries from Creature array
                for (uint8 i = 0; i < LackeyEntryList.size(); ++i)
                    LackeyEntryList[i] = m_auiAddEntries[i];

                //remove random entries
                while (LackeyEntryList.size() > MAX_ACTIVE_LACKEY)
                    LackeyEntryList.erase(LackeyEntryList.begin() + rand32() % LackeyEntryList.size());

                //summon all the remaining in vector
                for (std::vector<uint32>::const_iterator itr = LackeyEntryList.begin(); itr != LackeyEntryList.end(); ++itr)
                {
                    if (Creature* pAdd = me->SummonCreature((*itr), LackeyLocations[j][0], LackeyLocations[j][1], fZLocation, fOrientation, TEMPSUMMON_CORPSE_DESPAWN))
                        m_auiLackeyGUID[j] = pAdd->GetGUID();

                    ++j;
                }
            }
            else
            {
                for (std::vector<uint32>::const_iterator itr = LackeyEntryList.begin(); itr != LackeyEntryList.end(); ++itr)
                {
                    Unit* pAdd = ObjectAccessor::GetUnit(*me, m_auiLackeyGUID[j]);

                    //object already removed, not exist
                    if (!pAdd)
                    {
                        pAdd = me->SummonCreature((*itr), LackeyLocations[j][0], LackeyLocations[j][1], fZLocation, fOrientation, TEMPSUMMON_CORPSE_DESPAWN);
                        if (pAdd)
                            m_auiLackeyGUID[j] = pAdd->GetGUID();
                    }
                    ++j;
                }
            }
        }

        /**
         * @brief 击杀单位时调用
         * @param victim 被击杀的单位
         *
         * 当击杀玩家时，喊出嘲讽对话（最多5种不同的对话）
         */
        void KilledUnit(Unit* victim) override
        {
            if (victim->GetTypeId() != TYPEID_PLAYER)
                return;

            Talk(PlayerDeath[PlayersKilled].id);

            if (PlayersKilled < 4)
                ++PlayersKilled;
        }

        /**
         * @brief 死亡时调用
         * @param killer 击杀者（未使用）
         *
         * 喊出死亡对话，检查所有手下是否都已死亡：
         * - 如果所有手下都已死亡，设置BOSS状态为完成
         * - 否则，移除可拾取标志（需要等待手下全部死亡）
         */
        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_DEATH);

            if (instance->GetData(DATA_DELRISSA_DEATH_COUNT) == MAX_ACTIVE_LACKEY)
                instance->SetBossState(DATA_PRIESTESS_DELRISSA, DONE);
            else
                me->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
        }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 主要战斗逻辑循环：
         * - 检查是否有目标
         * - 定期检查是否脱战（Z轴位置检查）
         * - 快速治疗：治疗生命值最低的友方单位
         * - 恢复：随机对自己或手下施放
         * - 真言术：盾：随机对自己或手下施放
         * - 驱散魔法：随机对敌方或友方施放
         * - 暗言术：痛：对随机玩家施放
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            if (ResetTimer <= diff)
            {
                float x, y, z, o;
                me->GetHomePosition(x, y, z, o);
                if (me->GetPositionZ() >= z+10)
                {
                    EnterEvadeMode();
                    return;
                }
                ResetTimer = 5000;
            } else ResetTimer -= diff;

            if (HealTimer <= diff)
            {
                uint32 health = me->GetHealth();
                Unit* target = me;
                for (uint8 i = 0; i < MAX_ACTIVE_LACKEY; ++i)
                {
                    if (Unit* pAdd = ObjectAccessor::GetUnit(*me, m_auiLackeyGUID[i]))
                    {
                        if (pAdd->IsAlive() && pAdd->GetHealth() < health)
                            target = pAdd;
                    }
                }

                DoCast(target, SPELL_FLASH_HEAL);
                HealTimer = 15000;
            } else HealTimer -= diff;

            if (RenewTimer <= diff)
            {
                Unit* target = me;

                if (urand(0, 1))
                    if (Unit* pAdd = ObjectAccessor::GetUnit(*me, m_auiLackeyGUID[rand32() % MAX_ACTIVE_LACKEY]))
                        if (pAdd->IsAlive())
                            target = pAdd;

                DoCast(target, SPELL_RENEW_NORMAL);
                RenewTimer = 5000;
            } else RenewTimer -= diff;

            if (ShieldTimer <= diff)
            {
                Unit* target = me;

                if (urand(0, 1))
                    if (Unit* pAdd = ObjectAccessor::GetUnit(*me, m_auiLackeyGUID[rand32() % MAX_ACTIVE_LACKEY]))
                        if (pAdd->IsAlive() && !pAdd->HasAura(SPELL_SHIELD))
                            target = pAdd;

                DoCast(target, SPELL_SHIELD);
                ShieldTimer = 7500;
            } else ShieldTimer -= diff;

            if (DispelTimer <= diff)
            {
                Unit* target = nullptr;

                if (urand(0, 1))
                    target = SelectTarget(SelectTargetMethod::Random, 0, 100, true);
                else
                {
                    if (urand(0, 1))
                        target = me;
                    else
                        if (Unit* pAdd = ObjectAccessor::GetUnit(*me, m_auiLackeyGUID[rand32() % MAX_ACTIVE_LACKEY]))
                            if (pAdd->IsAlive())
                                target = pAdd;
                }

                if (target)
                    DoCast(target, SPELL_DISPEL_MAGIC);

                DispelTimer = 12000;
            } else DispelTimer -= diff;

            if (SWPainTimer <= diff)
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                    DoCast(target, SPELL_SW_PAIN_NORMAL);

                SWPainTimer = 10000;
            } else SWPainTimer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

enum HealingPotion
{
    SPELL_HEALING_POTION    = 15503
};

/**
 * @brief 手下公共AI基类
 *
 * 所有8个可能的手下都继承自这个公共基类，实现了：
 * - 使用治疗药水（生命值低于25%时）
 * - 定期重置仇恨列表（特殊仇恨机制）
 * - 死亡时的处理（通知德莉萨，检查是否所有手下都已死亡）
 * - 与德莉萨和其他手下的协作
 *
 * @note 这些手下不遵循标准的仇恨系统，需要定期重置仇恨
 */
//all 8 possible lackey use this common
struct boss_priestess_lackey_commonAI : public ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化AI，设置副本脚本
     */
    boss_priestess_lackey_commonAI(Creature* creature) : ScriptedAI(creature)
    {
        Initialize();
        instance = creature->GetInstanceScript();
    }

    /**
     * @brief 初始化成员变量
     *
     * 重置药水使用标志和仇恨重置计时器
     */
    void Initialize()
    {
        UsedPotion = false;

        // 这些手下不遵循标准的仇恨系统规则
        // 后续开发中，应该制作某种替代仇恨系统
        // 我们不知道这个系统基于什么，但有一个理论是基于职业（治疗=高仇恨，DPS=中等，等）
        // 我们定期重置他们的仇恨作为替代方案，直到这样的系统存在
        ResetThreatTimer = urand(5000, 20000);
    }

    InstanceScript* instance;                       ///< 副本脚本实例指针

    ObjectGuid m_auiLackeyGUIDs[MAX_ACTIVE_LACKEY]; ///< 所有手下的GUID数组
    uint32 ResetThreatTimer;                        ///< 仇恨重置计时器

    bool UsedPotion;                                ///< 是否已使用治疗药水

    /**
     * @brief 重置状态
     *
     * 初始化变量，获取其他手下的GUID，如果德莉萨已死亡则复活她
     */
    void Reset() override
    {
        Initialize();
        AcquireGUIDs();

        // in case she is not alive and Reset was for some reason called, respawn her (most likely party wipe after killing her)
        if (Creature* delrissa = instance->GetCreature(DATA_PRIESTESS_DELRISSA))
        {
            if (!delrissa->IsAlive())
                delrissa->Respawn();
        }
    }

    /**
     * @brief 进入战斗时调用
     * @param who 进入战斗的目标
     *
     * 让所有未战斗的手下和德莉萨进入战斗
     */
    void JustEngagedWith(Unit* who) override
    {
        if (!who)
            return;

        for (uint8 i = 0; i < MAX_ACTIVE_LACKEY; ++i)
            if (Unit* pAdd = ObjectAccessor::GetUnit(*me, m_auiLackeyGUIDs[i]))
                if (!pAdd->IsEngaged() && pAdd != me)
                    AddThreat(who, 0.0f, pAdd);

        if (Creature* delrissa = instance->GetCreature(DATA_PRIESTESS_DELRISSA))
            if (delrissa->IsAlive() && !delrissa->IsEngaged())
                AddThreat(who, 0.0f, delrissa);
    }

    /**
     * @brief 死亡时调用
     * @param killer 击杀者（未使用）
     *
     * 处理手下死亡逻辑：
     * - 通知德莉萨喊出死亡对话
     * - 增加德莉萨的手下死亡计数
     * - 如果所有手下都已死亡且德莉萨已死亡，设置BOSS状态为完成
     */
    void JustDied(Unit* /*killer*/) override
    {
        Creature* delrissa = instance->GetCreature(DATA_PRIESTESS_DELRISSA);
        uint32 uiLackeyDeathCount = instance->GetData(DATA_DELRISSA_DEATH_COUNT);

        if (!delrissa)
            return;

        //should delrissa really yell if dead?
        delrissa->AI()->Talk(LackeyDeath[uiLackeyDeathCount].id);

        instance->SetData(DATA_DELRISSA_DEATH_COUNT, SPECIAL);

        //increase local var, since we now may have four dead
        ++uiLackeyDeathCount;

        if (uiLackeyDeathCount == MAX_ACTIVE_LACKEY)
        {
            //time to make her lootable and complete event if she died before lackeys
            if (!delrissa->IsAlive())
            {
                delrissa->SetDynamicFlag(UNIT_DYNFLAG_LOOTABLE);

                instance->SetBossState(DATA_PRIESTESS_DELRISSA, DONE);
            }
        }
    }

    /**
     * @brief 击杀单位时调用
     * @param victim 被击杀的单位
     *
     * 通知德莉萨喊出嘲讽对话
     */
    void KilledUnit(Unit* victim) override
    {
        if (Creature* delrissa = instance->GetCreature(DATA_PRIESTESS_DELRISSA))
            delrissa->AI()->KilledUnit(victim);
    }

    /**
     * @brief 获取所有手下的GUID
     *
     * 从德莉萨的AI中获取所有手下的GUID，用于协作战斗
     */
    void AcquireGUIDs()
    {
        if (Creature* delrissa = instance->GetCreature(DATA_PRIESTESS_DELRISSA))
        {
            for (uint8 i = 0; i < MAX_ACTIVE_LACKEY; ++i)
                m_auiLackeyGUIDs[i] = ENSURE_AI(boss_priestess_delrissa::boss_priestess_delrissaAI, delrissa->AI())->m_auiLackeyGUID[i];
        }
    }

    /**
     * @brief 更新AI逻辑
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 处理手下公共逻辑：
     * - 生命值低于25%时使用治疗药水（只能使用一次）
     * - 定期重置仇恨列表（特殊仇恨机制）
     */
    void UpdateAI(uint32 diff) override
    {
        if (!UsedPotion && HealthBelowPct(25))
        {
            DoCast(me, SPELL_HEALING_POTION);
            UsedPotion = true;
        }

        if (ResetThreatTimer <= diff)
        {
            ResetThreatList();
            ResetThreatTimer = urand(5000, 20000);
        } else ResetThreatTimer -= diff;
    }
};

enum RogueSpells
{
    SPELL_KIDNEY_SHOT       = 27615,
    SPELL_GOUGE             = 12540,
    SPELL_KICK              = 27613,
    SPELL_VANISH            = 44290,
    SPELL_BACKSTAB          = 15657,
    SPELL_EVISCERATE        = 27611
};

/**
 * @brief Kagani Nightstrike（盗贼）BOSS脚本
 *
 * 盗贼职业手下，使用以下技能：
 * - 消失：进入潜行状态并重置仇恨
 * - 背刺：在潜行状态下对目标造成高额伤害
 * - 肾击：在潜行状态下眩晕目标
 * - 凿击：眩晕当前目标
 * - 脚踢：打断施法
 * - 剔骨：造成伤害
 */
class boss_kagani_nightstrike : public CreatureScript
{
public:
    boss_kagani_nightstrike() : CreatureScript("boss_kagani_nightstrike") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetMagistersTerraceAI<boss_kagani_nightstrikeAI>(creature);
    }

    struct boss_kagani_nightstrikeAI : public boss_priestess_lackey_commonAI
    {
        // Rogue（盗贼）
        boss_kagani_nightstrikeAI(Creature* creature) : boss_priestess_lackey_commonAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            Gouge_Timer = 5500;
            Kick_Timer = 7000;
            Vanish_Timer = 2000;
            Eviscerate_Timer = 6000;
            Wait_Timer = 5000;
            InVanish = false;
        }

        uint32 Gouge_Timer;
        uint32 Kick_Timer;
        uint32 Vanish_Timer;
        uint32 Eviscerate_Timer;
        uint32 Wait_Timer;
        bool InVanish;

        void Reset() override
        {
            Initialize();
            me->SetVisible(true);

            boss_priestess_lackey_commonAI::Reset();
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_priestess_lackey_commonAI::UpdateAI(diff);

            if (Vanish_Timer <= diff)
            {
                DoCast(me, SPELL_VANISH);

                Unit* unit = SelectTarget(SelectTargetMethod::Random, 0);

                ResetThreatList();

                if (unit)
                    AddThreat(unit, 1000.0f);

                InVanish = true;
                Vanish_Timer = 30000;
                Wait_Timer = 10000;
            } else Vanish_Timer -= diff;

            if (InVanish)
            {
                if (Wait_Timer <= diff)
                {
                    DoCastVictim(SPELL_BACKSTAB, true);
                    DoCastVictim(SPELL_KIDNEY_SHOT, true);
                    me->SetVisible(true);       // ...? Hacklike
                    InVanish = false;
                } else Wait_Timer -= diff;
            }

            if (Gouge_Timer <= diff)
            {
                DoCastVictim(SPELL_GOUGE);
                Gouge_Timer = 5500;
            } else Gouge_Timer -= diff;

            if (Kick_Timer <= diff)
            {
                DoCastVictim(SPELL_KICK);
                Kick_Timer = 7000;
            } else Kick_Timer -= diff;

            if (Eviscerate_Timer <= diff)
            {
                DoCastVictim(SPELL_EVISCERATE);
                Eviscerate_Timer = 4000;
            } else Eviscerate_Timer -= diff;

            if (!InVanish)
                DoMeleeAttackIfReady();
        }
    };
};

enum WarlockSpells
{
    SPELL_IMMOLATE              = 44267,
    SPELL_SHADOW_BOLT           = 12471,
    SPELL_SEED_OF_CORRUPTION    = 44141,
    SPELL_CURSE_OF_AGONY        = 14875,
    SPELL_FEAR                  = 38595,
    SPELL_IMP_FIREBALL          = 44164,
    SPELL_SUMMON_IMP            = 44163
};

/**
 * @brief Ellris Duskhallow（术士）BOSS脚本
 *
 * 术士职业手下，使用以下技能：
 * - 召唤小鬼：战斗开始时召唤小鬼宠物
 * - 献祭：对目标造成持续火焰伤害
 * - 暗影箭：对目标造成暗影伤害
 * - 腐蚀之种：对目标及其周围敌人造成暗影伤害
 * - 痛苦诅咒：对目标造成持续暗影伤害
 * - 恐惧：使目标恐惧逃跑
 */
class boss_ellris_duskhallow : public CreatureScript
{
public:
    boss_ellris_duskhallow() : CreatureScript("boss_ellris_duskhallow") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetMagistersTerraceAI<boss_ellris_duskhallowAI>(creature);
    }

    struct boss_ellris_duskhallowAI : public boss_priestess_lackey_commonAI
    {
        //Warlock
        boss_ellris_duskhallowAI(Creature* creature) : boss_priestess_lackey_commonAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            Immolate_Timer = 6000;
            Shadow_Bolt_Timer = 3000;
            Seed_of_Corruption_Timer = 2000;
            Curse_of_Agony_Timer = 1000;
            Fear_Timer = 10000;
        }

        uint32 Immolate_Timer;
        uint32 Shadow_Bolt_Timer;
        uint32 Seed_of_Corruption_Timer;
        uint32 Curse_of_Agony_Timer;
        uint32 Fear_Timer;

        void Reset() override
        {
            Initialize();

            boss_priestess_lackey_commonAI::Reset();
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            DoCast(me, SPELL_SUMMON_IMP);
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_priestess_lackey_commonAI::UpdateAI(diff);

            if (Immolate_Timer <= diff)
            {
                DoCastVictim(SPELL_IMMOLATE);
                Immolate_Timer = 6000;
            } else Immolate_Timer -= diff;

            if (Shadow_Bolt_Timer <= diff)
            {
                DoCastVictim(SPELL_SHADOW_BOLT);
                Shadow_Bolt_Timer = 5000;
            } else Shadow_Bolt_Timer -= diff;

            if (Seed_of_Corruption_Timer <= diff)
            {
                if (Unit* unit = SelectTarget(SelectTargetMethod::Random, 0))
                    DoCast(unit, SPELL_SEED_OF_CORRUPTION);

                Seed_of_Corruption_Timer = 10000;
            } else Seed_of_Corruption_Timer -= diff;

            if (Curse_of_Agony_Timer <= diff)
            {
                if (Unit* unit = SelectTarget(SelectTargetMethod::Random, 0))
                    DoCast(unit, SPELL_CURSE_OF_AGONY);

                Curse_of_Agony_Timer = 13000;
            } else Curse_of_Agony_Timer -= diff;

            if (Fear_Timer <= diff)
            {
                if (Unit* unit = SelectTarget(SelectTargetMethod::Random, 0))
                    DoCast(unit, SPELL_FEAR);

                Fear_Timer = 10000;
            } else Fear_Timer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

enum KickDown
{
    SPELL_KNOCKDOWN     = 11428,
    SPELL_SNAP_KICK     = 46182
};

/**
 * @brief Eramas Brightblaze（武僧）BOSS脚本
 *
 * 武僧职业手下，使用以下技能：
 * - 击倒：击倒目标并造成伤害
 * - 快速踢击：对目标造成物理伤害
 */
class boss_eramas_brightblaze : public CreatureScript
{
public:
    boss_eramas_brightblaze() : CreatureScript("boss_eramas_brightblaze") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetMagistersTerraceAI<boss_eramas_brightblazeAI>(creature);
    }

    struct boss_eramas_brightblazeAI : public boss_priestess_lackey_commonAI
    {
        //Monk
        boss_eramas_brightblazeAI(Creature* creature) : boss_priestess_lackey_commonAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            Knockdown_Timer = 6000;
            Snap_Kick_Timer = 4500;
        }

        uint32 Knockdown_Timer;
        uint32 Snap_Kick_Timer;

        void Reset() override
        {
            Initialize();

            boss_priestess_lackey_commonAI::Reset();
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_priestess_lackey_commonAI::UpdateAI(diff);

            if (Knockdown_Timer <= diff)
            {
                DoCastVictim(SPELL_KNOCKDOWN);
                Knockdown_Timer = 6000;
            } else Knockdown_Timer -= diff;

            if (Snap_Kick_Timer <= diff)
            {
                DoCastVictim(SPELL_SNAP_KICK);
                Snap_Kick_Timer  = 4500;
            } else Snap_Kick_Timer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

enum MageSpells
{
    SPELL_POLYMORPH         = 13323,
    SPELL_ICE_BLOCK         = 27619,
    SPELL_BLIZZARD          = 44178,
    SPELL_ICE_LANCE         = 46194,
    SPELL_CONE_OF_COLD      = 38384,
    SPELL_FROSTBOLT         = 15043,
    SPELL_BLINK             = 14514
};

/**
 * @brief Yazzai（法师）BOSS脚本
 *
 * 法师职业手下，使用以下技能：
 * - 变形术：将随机目标变形为绵羊
 * - 寒冰屏障：生命值低于35%时进入无敌状态
 * - 暴风雪：对目标区域造成持续冰霜伤害
 * - 冰枪术：对目标造成冰霜伤害
 * - 冰锥术：对前方锥形区域造成冰霜伤害
 * - 寒冰箭：对目标造成冰霜伤害并减速
 * - 闪烁：当有敌人在近战范围时瞬移逃脱
 */
class boss_yazzai : public CreatureScript
{
public:
    boss_yazzai() : CreatureScript("boss_yazzai") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetMagistersTerraceAI<boss_yazzaiAI>(creature);
    }

    struct boss_yazzaiAI : public boss_priestess_lackey_commonAI
    {
        //Mage
        boss_yazzaiAI(Creature* creature) : boss_priestess_lackey_commonAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            HasIceBlocked = false;

            Polymorph_Timer = 1000;
            Ice_Block_Timer = 20000;
            Wait_Timer = 10000;
            Blizzard_Timer = 8000;
            Ice_Lance_Timer = 12000;
            Cone_of_Cold_Timer = 10000;
            Frostbolt_Timer = 3000;
            Blink_Timer = 8000;
        }

        bool HasIceBlocked;

        uint32 Polymorph_Timer;
        uint32 Ice_Block_Timer;
        uint32 Wait_Timer;
        uint32 Blizzard_Timer;
        uint32 Ice_Lance_Timer;
        uint32 Cone_of_Cold_Timer;
        uint32 Frostbolt_Timer;
        uint32 Blink_Timer;

        void Reset() override
        {
            Initialize();

            boss_priestess_lackey_commonAI::Reset();
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_priestess_lackey_commonAI::UpdateAI(diff);

            if (Polymorph_Timer <= diff)
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0))
                {
                    DoCast(target, SPELL_POLYMORPH);
                    Polymorph_Timer = 20000;
                }
            } else Polymorph_Timer -= diff;

            if (HealthBelowPct(35) && !HasIceBlocked)
            {
                DoCast(me, SPELL_ICE_BLOCK);
                HasIceBlocked = true;
            }

            if (Blizzard_Timer <= diff)
            {
                if (Unit* unit = SelectTarget(SelectTargetMethod::Random, 0))
                    DoCast(unit, SPELL_BLIZZARD);

                Blizzard_Timer = 8000;
            } else Blizzard_Timer -= diff;

            if (Ice_Lance_Timer <= diff)
            {
                DoCastVictim(SPELL_ICE_LANCE);
                Ice_Lance_Timer = 12000;
            } else Ice_Lance_Timer -= diff;

            if (Cone_of_Cold_Timer <= diff)
            {
                DoCastVictim(SPELL_CONE_OF_COLD);
                Cone_of_Cold_Timer = 10000;
            } else Cone_of_Cold_Timer -= diff;

            if (Frostbolt_Timer <= diff)
            {
                DoCastVictim(SPELL_FROSTBOLT);
                Frostbolt_Timer = 8000;
            } else Frostbolt_Timer -= diff;

            if (Blink_Timer <= diff)
            {
                bool InMeleeRange = false;
                for (auto const& pair : me->GetCombatManager().GetPvECombatRefs())
                {
                    if (pair.second->GetOther(me)->IsWithinMeleeRange(me))
                    {
                        InMeleeRange = true;
                        break;
                    }
                }

                //if anybody is in melee range than escape by blink
                if (InMeleeRange)
                    DoCast(me, SPELL_BLINK);

                Blink_Timer = 8000;
            } else Blink_Timer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

enum WarriorSpells
{
    SPELL_INTERCEPT_STUN        = 27577,
    SPELL_DISARM                = 27581,
    SPELL_PIERCING_HOWL         = 23600,
    SPELL_FRIGHTENING_SHOUT     = 19134,
    SPELL_HAMSTRING             = 27584,
    SPELL_BATTLE_SHOUT          = 27578,
    SPELL_MORTAL_STRIKE         = 44268
};

/**
 * @brief Warlord Salaris（战士）BOSS脚本
 *
 * 战士职业手下，使用以下技能：
 * - 战斗怒吼：进入战斗时提升攻击强度
 * - 拦截眩晕：当无目标在近战范围时冲锋并眩晕目标
 * - 缴械：缴械当前目标
 * - 刺耳怒吼：使周围敌人减速
 * - 恐惧怒吼：使周围敌人恐惧
 * - 断筋：使当前目标减速
 * - 致死打击：对目标造成高额伤害并降低治疗效果
 */
class boss_warlord_salaris : public CreatureScript
{
public:
    boss_warlord_salaris() : CreatureScript("boss_warlord_salaris") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetMagistersTerraceAI<boss_warlord_salarisAI>(creature);
    }

    struct boss_warlord_salarisAI : public boss_priestess_lackey_commonAI
    {
        //Warrior
        boss_warlord_salarisAI(Creature* creature) : boss_priestess_lackey_commonAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            Intercept_Stun_Timer = 500;
            Disarm_Timer = 6000;
            Piercing_Howl_Timer = 10000;
            Frightening_Shout_Timer = 18000;
            Hamstring_Timer = 4500;
            Mortal_Strike_Timer = 8000;
        }

        uint32 Intercept_Stun_Timer;
        uint32 Disarm_Timer;
        uint32 Piercing_Howl_Timer;
        uint32 Frightening_Shout_Timer;
        uint32 Hamstring_Timer;
        uint32 Mortal_Strike_Timer;

        void Reset() override
        {
            Initialize();

            boss_priestess_lackey_commonAI::Reset();
        }

        void JustEngagedWith(Unit* /*who*/) override
        {
            DoCast(me, SPELL_BATTLE_SHOUT);
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_priestess_lackey_commonAI::UpdateAI(diff);

            if (Intercept_Stun_Timer <= diff)
            {
                bool InMeleeRange = false;
                for (auto const& pair : me->GetCombatManager().GetPvECombatRefs())
                {
                    if (pair.second->GetOther(me)->IsWithinMeleeRange(me))
                    {
                        InMeleeRange = true;
                        break;
                    }
                }

                //if nobody is in melee range than try to use Intercept
                if (!InMeleeRange)
                {
                    if (Unit* unit = SelectTarget(SelectTargetMethod::Random, 0))
                        DoCast(unit, SPELL_INTERCEPT_STUN);
                }

                Intercept_Stun_Timer = 10000;
            } else Intercept_Stun_Timer -= diff;

            if (Disarm_Timer <= diff)
            {
                DoCastVictim(SPELL_DISARM);
                Disarm_Timer = 6000;
            } else Disarm_Timer -= diff;

            if (Hamstring_Timer <= diff)
            {
                DoCastVictim(SPELL_HAMSTRING);
                Hamstring_Timer = 4500;
            } else Hamstring_Timer -= diff;

            if (Mortal_Strike_Timer <= diff)
            {
                DoCastVictim(SPELL_MORTAL_STRIKE);
                Mortal_Strike_Timer = 4500;
            } else Mortal_Strike_Timer -= diff;

            if (Piercing_Howl_Timer <= diff)
            {
                DoCastVictim(SPELL_PIERCING_HOWL);
                Piercing_Howl_Timer = 10000;
            } else Piercing_Howl_Timer -= diff;

            if (Frightening_Shout_Timer <= diff)
            {
                DoCastVictim(SPELL_FRIGHTENING_SHOUT);
                Frightening_Shout_Timer = 18000;
            } else Frightening_Shout_Timer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

enum HunterSpells
{
    SPELL_AIMED_SHOT            = 44271,
    SPELL_SHOOT                 = 15620,
    SPELL_CONCUSSIVE_SHOT       = 27634,
    SPELL_MULTI_SHOT            = 31942,
    SPELL_WING_CLIP             = 44286,
    SPELL_FREEZING_TRAP         = 44136,

    NPC_SLIVER                  = 24552
};

/**
 * @brief Garaxxas（猎人）BOSS脚本
 *
 * 猎人职业手下，使用以下技能：
 * - 召唤宠物Sliver：战斗开始时召唤宠物
 * - 瞄准射击：对目标造成高额远程伤害
 * - 射击：对目标造成远程伤害
 * - 震荡射击：使目标减速
 * - 多重射击：对目标及其周围敌人造成伤害
 * - 摔绊：使近战范围内的目标减速
 * - 冰冻陷阱：在近战范围内放置冰冻陷阱，使目标冻结
 */
class boss_garaxxas : public CreatureScript
{
public:
    boss_garaxxas() : CreatureScript("boss_garaxxas") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetMagistersTerraceAI<boss_garaxxasAI>(creature);
    }

    struct boss_garaxxasAI : public boss_priestess_lackey_commonAI
    {
        //Hunter
        boss_garaxxasAI(Creature* creature) : boss_priestess_lackey_commonAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            Aimed_Shot_Timer = 6000;
            Shoot_Timer = 2500;
            Concussive_Shot_Timer = 8000;
            Multi_Shot_Timer = 10000;
            Wing_Clip_Timer = 4000;
            Freezing_Trap_Timer = 15000;
        }

        ObjectGuid m_uiPetGUID;

        uint32 Aimed_Shot_Timer;
        uint32 Shoot_Timer;
        uint32 Concussive_Shot_Timer;
        uint32 Multi_Shot_Timer;
        uint32 Wing_Clip_Timer;
        uint32 Freezing_Trap_Timer;

        void Reset() override
        {
            Initialize();

            Unit* pPet = ObjectAccessor::GetUnit(*me, m_uiPetGUID);
            if (!pPet)
                me->SummonCreature(NPC_SLIVER, 0.0f, 0.0f, 0.0f, 0.0f, TEMPSUMMON_CORPSE_DESPAWN);

            boss_priestess_lackey_commonAI::Reset();
        }

        void JustSummoned(Creature* summoned) override
        {
            m_uiPetGUID = summoned->GetGUID();
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_priestess_lackey_commonAI::UpdateAI(diff);

            if (me->IsWithinDistInMap(me->GetVictim(), ATTACK_DISTANCE))
            {
                if (Wing_Clip_Timer <= diff)
                {
                    DoCastVictim(SPELL_WING_CLIP);
                    Wing_Clip_Timer = 4000;
                } else Wing_Clip_Timer -= diff;

                if (Freezing_Trap_Timer <= diff)
                {
                    //attempt find go summoned from spell (cast by me)
                    GameObject* go = me->GetGameObject(SPELL_FREEZING_TRAP);

                    //if we have a go, we need to wait (only one trap at a time)
                    if (go)
                        Freezing_Trap_Timer = 2500;
                    else
                    {
                        //if go does not exist, then we can cast
                        DoCastVictim(SPELL_FREEZING_TRAP);
                        Freezing_Trap_Timer = 15000;
                    }
                } else Freezing_Trap_Timer -= diff;

                DoMeleeAttackIfReady();
            }
            else
            {
                if (Concussive_Shot_Timer <= diff)
                {
                    DoCastVictim(SPELL_CONCUSSIVE_SHOT);
                    Concussive_Shot_Timer = 8000;
                } else Concussive_Shot_Timer -= diff;

                if (Multi_Shot_Timer <= diff)
                {
                    DoCastVictim(SPELL_MULTI_SHOT);
                    Multi_Shot_Timer = 10000;
                } else Multi_Shot_Timer -= diff;

                if (Aimed_Shot_Timer <= diff)
                {
                    DoCastVictim(SPELL_AIMED_SHOT);
                    Aimed_Shot_Timer = 6000;
                } else Aimed_Shot_Timer -= diff;

                if (Shoot_Timer <= diff)
                {
                    DoCastVictim(SPELL_SHOOT);
                    Shoot_Timer = 2500;
                } else Shoot_Timer -= diff;
            }
        }
    };
};

/**
 * @brief Apoko（萨满）BOSS脚本
 *
 * 萨满职业手下，使用以下技能：
 * - 风怒图腾、火焰新星图腾、地缚图腾：随机施放图腾
 * - 战争践踏：击晕周围敌人
 * - 净化：驱散目标的增益效果
 * - 次级治疗波：治疗自己
 * - 冰霜震击：对目标造成冰霜伤害并减速
 */
class boss_apoko : public CreatureScript
{
public:
    boss_apoko() : CreatureScript("boss_apoko") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetMagistersTerraceAI<boss_apokoAI>(creature);
    }

    struct boss_apokoAI : public boss_priestess_lackey_commonAI
    {
        //Shaman
        boss_apokoAI(Creature* creature) : boss_priestess_lackey_commonAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            Totem_Timer = 2000;
            Totem_Amount = 1;
            War_Stomp_Timer = 10000;
            Purge_Timer = 8000;
            Healing_Wave_Timer = 5000;
            Frost_Shock_Timer = 7000;
        }

        uint32 Totem_Timer;
        uint8  Totem_Amount;
        uint32 War_Stomp_Timer;
        uint32 Purge_Timer;
        uint32 Healing_Wave_Timer;
        uint32 Frost_Shock_Timer;

        void Reset() override
        {
            Initialize();

            boss_priestess_lackey_commonAI::Reset();
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_priestess_lackey_commonAI::UpdateAI(diff);

            if (Totem_Timer <= diff)
            {
                DoCast(me, RAND(SPELL_WINDFURY_TOTEM, SPELL_FIRE_NOVA_TOTEM, SPELL_EARTHBIND_TOTEM));
                ++Totem_Amount;
                Totem_Timer = Totem_Amount*2000;
            } else Totem_Timer -= diff;

            if (War_Stomp_Timer <= diff)
            {
                DoCast(me, SPELL_WAR_STOMP);
                War_Stomp_Timer = 10000;
            } else War_Stomp_Timer -= diff;

            if (Purge_Timer <= diff)
            {
                if (Unit* unit = SelectTarget(SelectTargetMethod::Random, 0))
                    DoCast(unit, SPELL_PURGE);

                Purge_Timer = 15000;
            } else Purge_Timer -= diff;

            if (Frost_Shock_Timer <= diff)
            {
                DoCastVictim(SPELL_FROST_SHOCK);
                Frost_Shock_Timer = 7000;
            } else Frost_Shock_Timer -= diff;

            if (Healing_Wave_Timer <= diff)
            {
                DoCast(me, SPELL_LESSER_HEALING_WAVE);
                Healing_Wave_Timer = 5000;
            } else Healing_Wave_Timer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

enum EngineerSpells
{
    SPELL_GOBLIN_DRAGON_GUN     = 44272,
    SPELL_ROCKET_LAUNCH         = 44137,
    SPELL_RECOMBOBULATE         = 44274,
    SPELL_HIGH_EXPLOSIVE_SHEEP  = 44276,
    SPELL_FEL_IRON_BOMB         = 46024,
    SPELL_SHEEP_EXPLOSION       = 44279
};

/**
 * @brief Zelfan（工程师）BOSS脚本
 *
 * 工程师职业手下，使用以下技能：
 * - 地精龙枪：对前方锥形区域造成火焰伤害
 * - 火箭发射：对目标造成火焰伤害
 * - 邪铁炸弹：对目标及其周围敌人造成火焰伤害
 * - 重组剂：解除队友的变形效果
 * - 高爆绵羊：召唤一只爆炸绵羊
 */
class boss_zelfan : public CreatureScript
{
public:
    boss_zelfan() : CreatureScript("boss_zelfan") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetMagistersTerraceAI<boss_zelfanAI>(creature);
    }

    struct boss_zelfanAI : public boss_priestess_lackey_commonAI
    {
        //Engineer
        boss_zelfanAI(Creature* creature) : boss_priestess_lackey_commonAI(creature)
        {
            Initialize();
        }

        void Initialize()
        {
            Goblin_Dragon_Gun_Timer = 20000;
            Rocket_Launch_Timer = 7000;
            Recombobulate_Timer = 4000;
            High_Explosive_Sheep_Timer = 10000;
            Fel_Iron_Bomb_Timer = 15000;
        }

        uint32 Goblin_Dragon_Gun_Timer;
        uint32 Rocket_Launch_Timer;
        uint32 Recombobulate_Timer;
        uint32 High_Explosive_Sheep_Timer;
        uint32 Fel_Iron_Bomb_Timer;

        void Reset() override
        {
            Initialize();

            boss_priestess_lackey_commonAI::Reset();
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_priestess_lackey_commonAI::UpdateAI(diff);

            if (Goblin_Dragon_Gun_Timer <= diff)
            {
                DoCastVictim(SPELL_GOBLIN_DRAGON_GUN);
                Goblin_Dragon_Gun_Timer = 10000;
            } else Goblin_Dragon_Gun_Timer -= diff;

            if (Rocket_Launch_Timer <= diff)
            {
                DoCastVictim(SPELL_ROCKET_LAUNCH);
                Rocket_Launch_Timer = 9000;
            } else Rocket_Launch_Timer -= diff;

            if (Fel_Iron_Bomb_Timer <= diff)
            {
                DoCastVictim(SPELL_FEL_IRON_BOMB);
                Fel_Iron_Bomb_Timer = 15000;
            } else Fel_Iron_Bomb_Timer -= diff;

            if (Recombobulate_Timer <= diff)
            {
                for (uint8 i = 0; i < MAX_ACTIVE_LACKEY; ++i)
                {
                    if (Unit* pAdd = ObjectAccessor::GetUnit(*me, m_auiLackeyGUIDs[i]))
                    {
                        if (pAdd->IsPolymorphed())
                        {
                            DoCast(pAdd, SPELL_RECOMBOBULATE);
                            break;
                        }
                    }
                }
                Recombobulate_Timer = 2000;
            } else Recombobulate_Timer -= diff;

            if (High_Explosive_Sheep_Timer <= diff)
            {
                DoCast(me, SPELL_HIGH_EXPLOSIVE_SHEEP);
                High_Explosive_Sheep_Timer = 65000;
            } else High_Explosive_Sheep_Timer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

/*
class npc_high_explosive_sheep : public CreatureScript
{
public:
    npc_high_explosive_sheep() : CreatureScript("npc_high_explosive_sheep") { }

    //CreatureAI* GetAI(Creature* creature) const override
    //{
    //    return GetMagistersTerraceAI<npc_high_explosive_sheepAI>(creature);
    //};
};
*/

/**
 * @brief 注册女祭司德莉萨及相关脚本
 *
 * 注册以下脚本：
 * - boss_priestess_delrissa：女祭司德莉萨BOSS AI
 * - boss_kagani_nightstrike：Kagani Nightstrike（盗贼）AI
 * - boss_ellris_duskhallow：Ellris Duskhallow（术士）AI
 * - boss_eramas_brightblaze：Eramas Brightblaze（武僧）AI
 * - boss_yazzai：Yazzai（法师）AI
 * - boss_warlord_salaris：Warlord Salaris（战士）AI
 * - boss_garaxxas：Garaxxas（猎人）AI
 * - boss_apoko：Apoko（萨满）AI
 * - boss_zelfan：Zelfan（工程师）AI
 */
void AddSC_boss_priestess_delrissa()
{
    new boss_priestess_delrissa();
    new boss_kagani_nightstrike();
    new boss_ellris_duskhallow();
    new boss_eramas_brightblaze();
    new boss_yazzai();
    new boss_warlord_salaris();
    new boss_garaxxas();
    new boss_apoko();
    new boss_zelfan();
    // new npc_high_explosive_sheep();
}
