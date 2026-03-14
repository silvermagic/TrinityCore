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
 * @file boss_moroes.cpp
 * @brief 卡拉赞副本 - 莫罗斯(管家)BOSS脚本模块
 *
 * 本模块实现了莫罗斯BOSS及其仆从客人的战斗逻辑，包括:
 * - 莫罗斯BOSS AI
 * - 4个随机仆从客人AI（从6个可能客人中随机选择）
 *
 * 战斗机制:
 * - 莫罗斯生命值30%时进入激怒状态
 * - 莫罗斯会周期性消失并施放绞喉技能
 * - 绞喉会在消失5秒后对随机目标施放，持续5分钟
 * - 4个仆从客人协助战斗，各有不同职业和技能
 * - 莫罗斯死亡后移除所有玩家的绞喉Debuff
 *
 * 仆从客人类型:
 * - 男爵夫人多萝西·米尔斯蒂普(暗影牧师)
 * - 男爵拉夫·德鲁格(惩戒骑士)
 * - 凯特丽奥娜·冯·印迪夫人(神圣牧师)
 * - 凯拉·贝里巴克女士(神圣骑士)
 * - 罗宾·达里斯勋爵(武器战士)
 * - 克里斯宾·费伦斯勋爵(防御战士)
 */

/* ScriptData
SDName: Boss_Moroes
SD%Complete: 95
SDComment:
SDCategory: Karazhan
EndScriptData */

#include "ScriptMgr.h"
#include "Containers.h"
#include "karazhan.h"
#include "InstanceScript.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

/**
 * @brief 莫罗斯台词枚举
 *
 * 定义莫罗斯在战斗中各个阶段说的话
 */
enum Yells
{
    SAY_AGGRO           = 0,  ///< 进入战斗台词
    SAY_SPECIAL         = 1,  ///< 施放绞喉时的台词
    SAY_KILL            = 2,  ///< 击杀玩家台词
    SAY_DEATH           = 3   ///< 死亡台词
};

/**
 * @brief 莫罗斯及仆从技能ID枚举
 *
 * 定义莫罗斯及其仆从使用的所有法术技能ID
 */
enum Spells
{
    // 莫罗斯技能
    SPELL_VANISH                = 29448,  ///< 消失：进入潜行状态
    SPELL_GARROTE               = 37066,  ///< 绞喉：对目标造成持续伤害，持续5分钟
    SPELL_BLIND                 = 34694,  ///< 致盲：使目标失明10秒
    SPELL_GOUGE                 = 29425,  ///< 凿击：使目标昏迷4秒
    SPELL_FRENZY                = 37023,  ///< 狂乱：生命值30%以下时施放，提高攻击速度

    // 仆从技能 - 暗影牧师(男爵夫人多萝西)
    SPELL_MANABURN              = 29405,  ///< 法力燃烧：燃烧目标的法力值并造成伤害
    SPELL_MINDFLY               = 29570,  ///< 精神鞭笞：对目标造成暗影伤害并减速
    SPELL_SWPAIN                = 34441,  ///< 暗言术：痛：对目标造成持续暗影伤害
    SPELL_SHADOWFORM            = 29406,  ///< 暗影形态：提高暗影伤害

    // 仆从技能 - 惩戒骑士(男爵拉夫)
    SPELL_HAMMEROFJUSTICE       = 13005,  ///< 制裁之锤：使目标昏迷6秒
    SPELL_JUDGEMENTOFCOMMAND    = 29386,  ///< 命令审判：对目标造成神圣伤害
    SPELL_SEALOFCOMMAND         = 29385,  ///< 命令圣印：使近战攻击有几率造成额外神圣伤害

    // 仆从技能 - 神圣牧师(凯特丽奥娜夫人)
    SPELL_DISPELMAGIC           = 15090,  ///< 驱散魔法：移除友方单位身上的魔法效果
    SPELL_GREATERHEAL           = 29564,  ///< 强效治疗术：治疗目标
    SPELL_HOLYFIRE              = 29563,  ///< 神圣之火：对目标造成神圣伤害
    SPELL_PWSHIELD              = 29408,  ///< 真言术：盾：为目标施加护盾

    // 仆从技能 - 神圣骑士(凯拉女士)
    SPELL_CLEANSE               = 29380,  ///< 清洁术：移除友方单位的中毒、疾病和魔法效果
    SPELL_GREATERBLESSOFMIGHT   = 29381,  ///< 强效力量祝福：提高目标的攻击强度
    SPELL_HOLYLIGHT             = 29562,  ///< 圣光术：治疗目标
    SPELL_DIVINESHIELD          = 41367,  ///< 圣盾术：使施法者免疫所有伤害

    // 仆从技能 - 武器战士(罗宾勋爵)
    SPELL_HAMSTRING             = 9080,   ///< 断筋：使目标移动速度降低
    SPELL_MORTALSTRIKE          = 29572,  ///< 致死打击：对目标造成伤害并降低治疗效果
    SPELL_WHIRLWIND             = 29573,  ///< 旋风斩：对周围所有敌人造成伤害

    // 仆从技能 - 防御战士(克里斯宾勋爵)
    SPELL_DISARM                = 8379,   ///< 缴械：使目标无法使用武器
    SPELL_HEROICSTRIKE          = 29567,  ///< 英勇打击：强力的近战攻击
    SPELL_SHIELDBASH            = 11972,  ///< 盾击：用盾牌打击目标，打断施法
    SPELL_SHIELDWALL            = 29390   ///< 盾墙：减少受到的伤害
};

/**
 * @brief 仆从生成位置
 *
 * 定义4个仆从客人的初始生成位置
 */
Position const Locations[4] =
{
    {-10991.0f, -1884.33f, 81.73f, 0.614315f},   ///< 第一个仆从位置
    {-10989.4f, -1885.88f, 81.73f, 0.904913f},   ///< 第二个仆从位置
    {-10978.1f, -1887.07f, 81.73f, 2.035550f},   ///< 第三个仆从位置
    {-10975.9f, -1885.81f, 81.73f, 2.253890f}    ///< 第四个仆从位置
};

/**
 * @brief 仆从NPC ID数组
 *
 * 定义6个可能出现的仆从客人NPC ID
 * 每次战斗会从中随机选择4个
 */
const uint32 Adds[6]=
{
    17007,  ///< 男爵夫人多萝西·米尔斯蒂普(暗影牧师)
    19872,  ///< 男爵拉夫·德鲁格(惩戒骑士)
    19873,  ///< 凯特丽奥娜·冯·印迪夫人(神圣牧师)
    19874,  ///< 凯拉·贝里巴克女士(神圣骑士)
    19875,  ///< 罗宾·达里斯勋爵(武器战士)
    19876   ///< 克里斯宾·费伦斯勋爵(防御战士)
};

/**
 * @class boss_moroes
 * @brief 莫罗斯BOSS脚本类
 *
 * 继承自CreatureScript，用于注册莫罗斯BOSS的AI脚本
 */
class boss_moroes : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册莫罗斯脚本名称
     */
    boss_moroes() : CreatureScript("boss_moroes") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回莫罗斯AI实例
     *
     * 调用时机: 服务器创建莫罗斯生物时
     * 功能: 创建并返回莫罗斯AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_moroesAI>(creature);
    }

    /**
     * @struct boss_moroesAI
     * @brief 莫罗斯BOSS的AI实现
     *
     * 继承自ScriptedAI，实现莫罗斯的战斗逻辑:
     * - 周期性消失并施放绞喉
     * - 施放致盲和凿击控制技能
     * - 生命值30%以下时进入激怒状态
     * - 管理4个仆从客人
     */
    struct boss_moroesAI : public ScriptedAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         *
         * 初始化莫罗斯AI，设置副本脚本并初始化成员变量
         */
        boss_moroesAI(Creature* creature) : ScriptedAI(creature)
        {
            Initialize();
            memset(AddId, 0, sizeof(AddId));

            instance = creature->GetInstanceScript();
        }

        /**
         * @brief 初始化成员变量
         *
         * 功能: 设置所有计时器和状态标志的初始值
         */
        void Initialize()
        {
            Vanish_Timer = 30000;   ///< 消失技能计时器，30秒后首次使用
            Blind_Timer = 35000;    ///< 致盲技能计时器，35秒后首次使用
            Gouge_Timer = 23000;    ///< 凿击技能计时器，23秒后首次使用
            Wait_Timer = 0;         ///< 消失后等待计时器
            CheckAdds_Timer = 5000; ///< 检查仆从状态计时器，5秒周期

            Enrage = false;         ///< 激怒状态标志
            InVanish = false;       ///< 消失状态标志
        }

        InstanceScript* instance;   ///< 副本脚本实例

        ObjectGuid AddGUID[4];      ///< 4个仆从的GUID数组

        uint32 Vanish_Timer;        ///< 消失技能计时器(毫秒)
        uint32 Blind_Timer;         ///< 致盲技能计时器(毫秒)
        uint32 Gouge_Timer;         ///< 凿击技能计时器(毫秒)
        uint32 Wait_Timer;          ///< 消失后等待计时器(毫秒)
        uint32 CheckAdds_Timer;     ///< 检查仆从状态计时器(毫秒)
        uint32 AddId[4];            ///< 4个仆从的NPC ID数组

        bool InVanish;              ///< 是否处于消失状态
        bool Enrage;                ///< 是否处于激怒状态

        /**
         * @brief 重置BOSS状态
         *
         * 调用时机: BOSS脱离战斗或重置时
         * 功能:
         * - 初始化所有成员变量
         * - 如果莫罗斯存活，重新生成仆从
         * - 设置副本BOSS状态为未开始
         */
        void Reset() override
        {
            Initialize();
            if (me->IsAlive())
                SpawnAdds();

            instance->SetBossState(DATA_MOROES, NOT_STARTED);
        }

        /**
         * @brief 开始战斗事件
         *
         * 功能:
         * - 设置副本BOSS状态为进行中
         * - 让莫罗斯进入战斗状态
         */
        void StartEvent()
        {
            instance->SetBossState(DATA_MOROES, IN_PROGRESS);

            DoZoneInCombat();
        }

        /**
         * @brief 进入战斗回调
         * @param who 进入战斗的目标(未使用)
         *
         * 调用时机: 莫罗斯进入战斗时
         * 功能:
         * - 开始战斗事件
         * - 播放战斗开始台词
         * - 让所有仆从开始攻击
         * - 进入战斗状态
         */
        void JustEngagedWith(Unit* /*who*/) override
        {
            StartEvent();

            Talk(SAY_AGGRO);
            AddsAttack();
            DoZoneInCombat();
        }

        /**
         * @brief 击杀单位回调
         * @param victim 被击杀的单位(未使用)
         *
         * 调用时机: 莫罗斯杀死一个单位时
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
         * 调用时机: 莫罗斯死亡时
         * 功能:
         * - 播放死亡台词
         * - 设置副本BOSS状态为完成
         * - 消失所有仆从
         * - 移除所有玩家身上的绞喉Debuff
         */
        void JustDied(Unit* /*killer*/) override
        {
            Talk(SAY_DEATH);

            instance->SetBossState(DATA_MOROES, DONE);

            DeSpawnAdds();

            // 莫罗斯死亡时移除所有玩家身上的绞喉Debuff
            instance->DoRemoveAurasDueToSpellOnPlayers(SPELL_GARROTE);
        }

        /**
         * @brief 生成仆从
         *
         * 功能:
         * - 先消失现有的仆从
         * - 如果仆从ID列表为空，从6个可能的仆从中随机选择4个
         * - 在预设位置生成4个仆从
         * - 记录仆从的GUID和ID
         */
        void SpawnAdds()
        {
            DeSpawnAdds();

            if (isAddlistEmpty())
            {
                // 仆从列表为空，随机选择4个仆从
                std::list<uint32> AddList;

                for (uint8 i = 0; i < 6; ++i)
                    AddList.push_back(Adds[i]);

                Trinity::Containers::RandomResize(AddList, 4);

                uint8 i = 0;
                for (std::list<uint32>::const_iterator itr = AddList.begin(); itr != AddList.end() && i < 4; ++itr, ++i)
                {
                    uint32 entry = *itr;

                    if (Creature* creature = me->SummonCreature(entry, Locations[i], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 10s))
                    {
                        AddGUID[i] = creature->GetGUID();
                        AddId[i] = entry;
                    }
                }
            }
            else
            {
                // 仆从列表不为空，使用已有的ID重新生成
                for (uint8 i = 0; i < 4; ++i)
                {
                    if (Creature* creature = me->SummonCreature(AddId[i], Locations[i], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 10s))
                        AddGUID[i] = creature->GetGUID();
                }
            }
        }

        /**
         * @brief 检查仆从列表是否为空
         * @return 如果仆从ID列表为空返回true，否则返回false
         */
        bool isAddlistEmpty()
        {
            for (uint8 i = 0; i < 4; ++i)
                if (AddId[i] == 0)
                    return true;

            return false;
        }

        /**
         * @brief 消失所有仆从
         *
         * 功能: 遍历所有仆从GUID，如果仆从存在则消失
         */
        void DeSpawnAdds()
        {
            for (uint8 i = 0; i < 4; ++i)
            {
                if (AddGUID[i])
                {
                    if (Creature* temp = ObjectAccessor::GetCreature(*me, AddGUID[i]))
                        temp->DespawnOrUnsummon();
                }
            }
        }

        /**
         * @brief 让所有仆从开始攻击
         *
         * 功能: 遍历所有仆从，让存活的仆从攻击莫罗斯的目标
         * 如果仆从不存活，进入逃避模式
         */
        void AddsAttack()
        {
            for (uint8 i = 0; i < 4; ++i)
            {
                if (AddGUID[i])
                {
                    Creature* temp = ObjectAccessor::GetCreature((*me), AddGUID[i]);
                    if (temp && temp->IsAlive())
                    {
                        temp->AI()->AttackStart(me->GetVictim());
                        DoZoneInCombat(temp);
                    } else
                        EnterEvadeMode();
                }
            }
        }

        /**
         * @brief 更新AI
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * 调用时机: 每个游戏循环 tick
         * 功能:
         * - 检查是否有有效攻击目标
         * - 生命值30%以下时施放激怒
         * - 定期检查仆从状态
         * - 处理消失、致盲、凿击技能
         * - 处理消失状态下的绞喉施放
         * - 进行近战攻击
         *
         * 性能注意: 每帧调用，保持简洁高效
         */
        void UpdateAI(uint32 diff) override
        {
            // 如果没有有效攻击目标，直接返回
            if (!UpdateVictim())
                return;

            // 生命值30%以下且未进入激怒状态时，施放激怒
            if (!Enrage && HealthBelowPct(30))
            {
                DoCast(me, SPELL_FRENZY);
                Enrage = true;
            }

            // 定期检查仆从状态，确保它们都在攻击
            if (CheckAdds_Timer <= diff)
            {
                for (uint8 i = 0; i < 4; ++i)
                {
                    if (AddGUID[i])
                    {
                        Creature* temp = ObjectAccessor::GetCreature((*me), AddGUID[i]);
                        if (temp && temp->IsAlive())
                            if (!temp->GetVictim())
                                temp->AI()->AttackStart(me->GetVictim());
                    }
                }
                CheckAdds_Timer = 5000;
            } else CheckAdds_Timer -= diff;

            // 未进入激怒状态时，可以施放消失、凿击、致盲
            if (!Enrage)
            {
                // 施放消失，然后对随机目标施放绞喉
                if (Vanish_Timer <= diff)
                {
                    DoCast(me, SPELL_VANISH);
                    InVanish = true;
                    Vanish_Timer = 30000;
                    Wait_Timer = 5000;  // 消失后等待5秒
                } else Vanish_Timer -= diff;

                // 施放凿击
                if (Gouge_Timer <= diff)
                {
                    DoCastVictim(SPELL_GOUGE);
                    Gouge_Timer = 40000;
                } else Gouge_Timer -= diff;

                // 施放致盲
                if (Blind_Timer <= diff)
                {
                    if (Unit* target = SelectTarget(SelectTargetMethod::MinDistance, 0, 0.0f, true, false))
                      DoCast(target, SPELL_BLIND);
                    Blind_Timer = 40000;
                } else Blind_Timer -= diff;
            }

            // 处于消失状态时，等待计时器结束后施放绞喉
            if (InVanish)
            {
                if (Wait_Timer <= diff)
                {
                    Talk(SAY_SPECIAL);

                    // 对随机目标施放绞喉(目标自己施放)
                    if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                        target->CastSpell(target, SPELL_GARROTE, true);

                    InVanish = false;
                } else Wait_Timer -= diff;
            }

            // 如果不在消失状态，进行近战攻击
            if (!InVanish)
                DoMeleeAttackIfReady();
        }
    };
};

/**
 * @struct boss_moroes_guestAI
 * @brief 莫罗斯仆从客人基类AI
 *
 * 继承自ScriptedAI，为所有仆从客人提供通用功能:
 * - 管理仆从之间的GUID引用
 * - 提供选择其他仆从作为治疗/辅助目标的功能
 * - 如果莫罗斯战斗结束，自动进入逃避模式
 */
struct boss_moroes_guestAI : public ScriptedAI
{
    InstanceScript* instance;   ///< 副本脚本实例

    ObjectGuid GuestGUID[4];    ///< 其他仆从的GUID数组

    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     *
     * 初始化仆从AI，获取副本脚本实例
     */
    boss_moroes_guestAI(Creature* creature) : ScriptedAI(creature)
    {
        instance = creature->GetInstanceScript();
    }

    /**
     * @brief 重置AI状态
     *
     * 调用时机: 仆从重置时
     * 功能: 设置莫罗斯BOSS状态为未开始
     */
    void Reset() override
    {
        instance->SetBossState(DATA_MOROES, NOT_STARTED);
    }

    /**
     * @brief 获取其他仆从的GUID
     *
     * 功能: 从莫罗斯AI获取所有仆从的GUID，用于治疗/辅助目标选择
     */
    void AcquireGUID()
    {
        if (Creature* Moroes = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_MOROES)))
            for (uint8 i = 0; i < 4; ++i)
                if (ObjectGuid GUID = ENSURE_AI(boss_moroes::boss_moroesAI, Moroes->AI())->AddGUID[i])
                    GuestGUID[i] = GUID;
    }

    /**
     * @brief 选择一个仆从作为目标
     * @return 随机选择的一个存活的仆从，如果都不存活则返回自己
     *
     * 功能: 用于治疗职业选择治疗目标
     */
    Unit* SelectGuestTarget()
    {
        ObjectGuid TempGUID = GuestGUID[rand32() % 4];
        if (TempGUID)
        {
            Unit* unit = ObjectAccessor::GetUnit(*me, TempGUID);
            if (unit && unit->IsAlive())
                return unit;
        }

        return me;
    }

    /**
     * @brief 更新AI
     * @param diff 时间差(未使用)
     *
     * 调用时机: 每个游戏循环 tick
     * 功能:
     * - 检查莫罗斯战斗是否还在进行中
     * - 如果不在进行中，进入逃避模式
     * - 进行近战攻击
     */
    void UpdateAI(uint32 /*diff*/) override
    {
        if (instance->GetBossState(DATA_MOROES) != IN_PROGRESS)
            EnterEvadeMode();

        DoMeleeAttackIfReady();
    }
};

/**
 * @class boss_baroness_dorothea_millstipe
 * @brief 男爵夫人多萝西·米尔斯蒂普(暗影牧师)脚本类
 *
 * 继承自CreatureScript，用于注册暗影牧师仆从的AI脚本
 */
class boss_baroness_dorothea_millstipe : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册暗影牧师脚本名称
     */
    boss_baroness_dorothea_millstipe() : CreatureScript("boss_baroness_dorothea_millstipe") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回暗影牧师AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_baroness_dorothea_millstipeAI>(creature);
    }

    /**
     * @struct boss_baroness_dorothea_millstipeAI
     * @brief 暗影牧师仆从AI实现
     *
     * 继承自boss_moroes_guestAI，实现暗影牧师的战斗逻辑:
     * - 施放暗影形态增强伤害
     * - 施放精神鞭笞、法力燃烧、暗言术：痛
     */
    struct boss_baroness_dorothea_millstipeAI : public boss_moroes_guestAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_baroness_dorothea_millstipeAI(Creature* creature) : boss_moroes_guestAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         */
        void Initialize()
        {
            ManaBurn_Timer = 7000;        ///< 法力燃烧计时器
            MindFlay_Timer = 1000;        ///< 精神鞭笞计时器
            ShadowWordPain_Timer = 6000;  ///< 暗言术：痛计时器
        }

        uint32 ManaBurn_Timer;        ///< 法力燃烧计时器(毫秒)
        uint32 MindFlay_Timer;        ///< 精神鞭笞计时器(毫秒)
        uint32 ShadowWordPain_Timer;  ///< 暗言术：痛计时器(毫秒)

        /**
         * @brief 重置AI状态
         *
         * 功能: 初始化计时器并施放暗影形态
         */
        void Reset() override
        {
            Initialize();

            DoCast(me, SPELL_SHADOWFORM, true);

            boss_moroes_guestAI::Reset();
        }

        /**
         * @brief 更新AI
         * @param diff 时间差(毫秒)
         *
         * 功能: 处理精神鞭笞、法力燃烧、暗言术：痛的施放
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_moroes_guestAI::UpdateAI(diff);

            // 施放精神鞭笞(3秒引导)
            if (MindFlay_Timer <= diff)
            {
                DoCastVictim(SPELL_MINDFLY);
                MindFlay_Timer = 12000;
            } else MindFlay_Timer -= diff;

            // 施放法力燃烧(3秒施法)
            if (ManaBurn_Timer <= diff)
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                    if (target->GetPowerType() == POWER_MANA)
                        DoCast(target, SPELL_MANABURN);
                ManaBurn_Timer = 5000;
            } else ManaBurn_Timer -= diff;

            // 施放暗言术：痛
            if (ShadowWordPain_Timer <= diff)
            {
                if (Unit* target = SelectTarget(SelectTargetMethod::Random, 0, 100, true))
                {
                    DoCast(target, SPELL_SWPAIN);
                    ShadowWordPain_Timer = 7000;
                }
            } else ShadowWordPain_Timer -= diff;
        }
    };
};

/**
 * @class boss_baron_rafe_dreuger
 * @brief 男爵拉夫·德鲁格(惩戒骑士)脚本类
 *
 * 继承自CreatureScript，用于注册惩戒骑士仆从的AI脚本
 */
class boss_baron_rafe_dreuger : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册惩戒骑士脚本名称
     */
    boss_baron_rafe_dreuger() : CreatureScript("boss_baron_rafe_dreuger") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回惩戒骑士AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_baron_rafe_dreugerAI>(creature);
    }

    /**
     * @struct boss_baron_rafe_dreugerAI
     * @brief 惩戒骑士仆从AI实现
     *
     * 继承自boss_moroes_guestAI，实现惩戒骑士的战斗逻辑:
     * - 施放命令圣印、命令审判、制裁之锤
     */
    struct boss_baron_rafe_dreugerAI : public boss_moroes_guestAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_baron_rafe_dreugerAI(Creature* creature) : boss_moroes_guestAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         */
        void Initialize()
        {
            HammerOfJustice_Timer = 1000;
            SealOfCommand_Timer = 7000;
            JudgementOfCommand_Timer = SealOfCommand_Timer + 29000;  // 审判在圣印之后29秒
        }

        uint32 HammerOfJustice_Timer;     ///< 制裁之锤计时器(毫秒)
        uint32 SealOfCommand_Timer;       ///< 命令圣印计时器(毫秒)
        uint32 JudgementOfCommand_Timer;  ///< 命令审判计时器(毫秒)

        /**
         * @brief 重置AI状态
         */
        void Reset() override
        {
            Initialize();

            boss_moroes_guestAI::Reset();
        }

        /**
         * @brief 更新AI
         * @param diff 时间差(毫秒)
         *
         * 功能: 处理命令圣印、命令审判、制裁之锤的施放
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_moroes_guestAI::UpdateAI(diff);

            // 施放命令圣印
            if (SealOfCommand_Timer <= diff)
            {
                DoCast(me, SPELL_SEALOFCOMMAND);
                SealOfCommand_Timer = 32000;
                JudgementOfCommand_Timer = 29000;
            } else SealOfCommand_Timer -= diff;

            // 施放命令审判
            if (JudgementOfCommand_Timer <= diff)
            {
                DoCastVictim(SPELL_JUDGEMENTOFCOMMAND);
                JudgementOfCommand_Timer = SealOfCommand_Timer + 29000;
            } else JudgementOfCommand_Timer -= diff;

            // 施放制裁之锤
            if (HammerOfJustice_Timer <= diff)
            {
                DoCastVictim(SPELL_HAMMEROFJUSTICE);
                HammerOfJustice_Timer = 12000;
            } else HammerOfJustice_Timer -= diff;
        }
    };
};

/**
 * @class boss_lady_catriona_von_indi
 * @brief 凯特丽奥娜·冯·印迪夫人(神圣牧师)脚本类
 *
 * 继承自CreatureScript，用于注册神圣牧师仆从的AI脚本
 */
class boss_lady_catriona_von_indi : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册神圣牧师脚本名称
     */
    boss_lady_catriona_von_indi() : CreatureScript("boss_lady_catriona_von_indi") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回神圣牧师AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_lady_catriona_von_indiAI>(creature);
    }

    /**
     * @struct boss_lady_catriona_von_indiAI
     * @brief 神圣牧师仆从AI实现
     *
     * 继承自boss_moroes_guestAI，实现神圣牧师的战斗逻辑:
     * - 治疗自己和队友
     * - 施放神圣之火、驱散魔法、真言术：盾
     */
    struct boss_lady_catriona_von_indiAI : public boss_moroes_guestAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_lady_catriona_von_indiAI(Creature* creature) : boss_moroes_guestAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         */
        void Initialize()
        {
            DispelMagic_Timer = 11000;
            GreaterHeal_Timer = 1500;
            HolyFire_Timer = 5000;
            PowerWordShield_Timer = 1000;
        }

        uint32 DispelMagic_Timer;     ///< 驱散魔法计时器(毫秒)
        uint32 GreaterHeal_Timer;     ///< 强效治疗术计时器(毫秒)
        uint32 HolyFire_Timer;        ///< 神圣之火计时器(毫秒)
        uint32 PowerWordShield_Timer; ///< 真言术：盾计时器(毫秒)

        /**
         * @brief 重置AI状态
         *
         * 功能: 初始化计时器并获取其他仆从的GUID
         */
        void Reset() override
        {
            Initialize();

            AcquireGUID();

            boss_moroes_guestAI::Reset();
        }

        /**
         * @brief 更新AI
         * @param diff 时间差(毫秒)
         *
         * 功能: 处理真言术：盾、强效治疗术、神圣之火、驱散魔法的施放
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_moroes_guestAI::UpdateAI(diff);

            // 施放真言术：盾
            if (PowerWordShield_Timer <= diff)
            {
                DoCast(me, SPELL_PWSHIELD);
                PowerWordShield_Timer = 15000;
            } else PowerWordShield_Timer -= diff;

            // 施放强效治疗术
            if (GreaterHeal_Timer <= diff)
            {
                Unit* target = SelectGuestTarget();

                DoCast(target, SPELL_GREATERHEAL);
                GreaterHeal_Timer = 17000;
            } else GreaterHeal_Timer -= diff;

            // 施放神圣之火
            if (HolyFire_Timer <= diff)
            {
                DoCastVictim(SPELL_HOLYFIRE);
                HolyFire_Timer = 22000;
            } else HolyFire_Timer -= diff;

            // 施放驱散魔法
            if (DispelMagic_Timer <= diff)
            {
                if (Unit* target = RAND(SelectGuestTarget(), SelectTarget(SelectTargetMethod::Random, 0, 100, true)))
                    DoCast(target, SPELL_DISPELMAGIC);

                DispelMagic_Timer = 25000;
            } else DispelMagic_Timer -= diff;
        }
    };
};

/**
 * @class boss_lady_keira_berrybuck
 * @brief 凯拉·贝里巴克女士(神圣骑士)脚本类
 *
 * 继承自CreatureScript，用于注册神圣骑士仆从的AI脚本
 */
class boss_lady_keira_berrybuck : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册神圣骑士脚本名称
     */
    boss_lady_keira_berrybuck() : CreatureScript("boss_lady_keira_berrybuck") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回神圣骑士AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_lady_keira_berrybuckAI>(creature);
    }

    /**
     * @struct boss_lady_keira_berrybuckAI
     * @brief 神圣骑士仆从AI实现
     *
     * 继承自boss_moroes_guestAI，实现神圣骑士的战斗逻辑:
     * - 治疗自己和队友
     * - 施放清洁术、强效力量祝福、圣光术、圣盾术
     */
    struct boss_lady_keira_berrybuckAI : public boss_moroes_guestAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_lady_keira_berrybuckAI(Creature* creature) : boss_moroes_guestAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         */
        void Initialize()
        {
            Cleanse_Timer = 13000;
            GreaterBless_Timer = 1000;
            HolyLight_Timer = 7000;
            DivineShield_Timer = 31000;
        }

        uint32 Cleanse_Timer;       ///< 清洁术计时器(毫秒)
        uint32 GreaterBless_Timer;  ///< 强效力量祝福计时器(毫秒)
        uint32 HolyLight_Timer;     ///< 圣光术计时器(毫秒)
        uint32 DivineShield_Timer;  ///< 圣盾术计时器(毫秒)

        /**
         * @brief 重置AI状态
         *
         * 功能: 初始化计时器并获取其他仆从的GUID
         */
        void Reset() override
        {
            Initialize();

            AcquireGUID();

            boss_moroes_guestAI::Reset();
        }

        /**
         * @brief 更新AI
         * @param diff 时间差(毫秒)
         *
         * 功能: 处理圣盾术、圣光术、强效力量祝福、清洁术的施放
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_moroes_guestAI::UpdateAI(diff);

            // 施放圣盾术
            if (DivineShield_Timer <= diff)
            {
                DoCast(me, SPELL_DIVINESHIELD);
                DivineShield_Timer = 31000;
            } else DivineShield_Timer -= diff;

            // 施放圣光术
            if (HolyLight_Timer <= diff)
            {
                Unit* target = SelectGuestTarget();

                DoCast(target, SPELL_HOLYLIGHT);
                HolyLight_Timer = 10000;
            } else HolyLight_Timer -= diff;

            // 施放强效力量祝福
            if (GreaterBless_Timer <= diff)
            {
                Unit* target = SelectGuestTarget();

                DoCast(target, SPELL_GREATERBLESSOFMIGHT);

                GreaterBless_Timer = 50000;
            } else GreaterBless_Timer -= diff;

            // 施放清洁术
            if (Cleanse_Timer <= diff)
            {
                Unit* target = SelectGuestTarget();

                DoCast(target, SPELL_CLEANSE);

                Cleanse_Timer = 10000;
            } else Cleanse_Timer -= diff;
        }
    };
};

/**
 * @class boss_lord_robin_daris
 * @brief 罗宾·达里斯勋爵(武器战士)脚本类
 *
 * 继承自CreatureScript，用于注册武器战士仆从的AI脚本
 */
class boss_lord_robin_daris : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册武器战士脚本名称
     */
    boss_lord_robin_daris() : CreatureScript("boss_lord_robin_daris") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回武器战士AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_lord_robin_darisAI>(creature);
    }

    /**
     * @struct boss_lord_robin_darisAI
     * @brief 武器战士仆从AI实现
     *
     * 继承自boss_moroes_guestAI，实现武器战士的战斗逻辑:
     * - 施放断筋、致死打击、旋风斩
     */
    struct boss_lord_robin_darisAI : public boss_moroes_guestAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_lord_robin_darisAI(Creature* creature) : boss_moroes_guestAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         */
        void Initialize()
        {
            Hamstring_Timer = 7000;
            MortalStrike_Timer = 10000;
            WhirlWind_Timer = 21000;
        }

        uint32 Hamstring_Timer;     ///< 断筋计时器(毫秒)
        uint32 MortalStrike_Timer;  ///< 致死打击计时器(毫秒)
        uint32 WhirlWind_Timer;     ///< 旋风斩计时器(毫秒)

        /**
         * @brief 重置AI状态
         */
        void Reset() override
        {
            Initialize();

            boss_moroes_guestAI::Reset();
        }

        /**
         * @brief 更新AI
         * @param diff 时间差(毫秒)
         *
         * 功能: 处理断筋、致死打击、旋风斩的施放
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_moroes_guestAI::UpdateAI(diff);

            // 施放断筋
            if (Hamstring_Timer <= diff)
            {
                DoCastVictim(SPELL_HAMSTRING);
                Hamstring_Timer = 12000;
            } else Hamstring_Timer -= diff;

            // 施放致死打击
            if (MortalStrike_Timer <= diff)
            {
                DoCastVictim(SPELL_MORTALSTRIKE);
                MortalStrike_Timer = 18000;
            } else MortalStrike_Timer -= diff;

            // 施放旋风斩
            if (WhirlWind_Timer <= diff)
            {
                DoCast(me, SPELL_WHIRLWIND);
                WhirlWind_Timer = 21000;
            } else WhirlWind_Timer -= diff;
        }
    };
};

/**
 * @class boss_lord_crispin_ference
 * @brief 克里斯宾·费伦斯勋爵(防御战士)脚本类
 *
 * 继承自CreatureScript，用于注册防御战士仆从的AI脚本
 */
class boss_lord_crispin_ference : public CreatureScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册防御战士脚本名称
     */
    boss_lord_crispin_ference() : CreatureScript("boss_lord_crispin_ference") { }

    /**
     * @brief 获取AI实例
     * @param creature 生物对象指针
     * @return 返回防御战士AI实例
     */
    CreatureAI* GetAI(Creature* creature) const override
    {
        return GetKarazhanAI<boss_lord_crispin_ferenceAI>(creature);
    }

    /**
     * @struct boss_lord_crispin_ferenceAI
     * @brief 防御战士仆从AI实现
     *
     * 继承自boss_moroes_guestAI，实现防御战士的战斗逻辑:
     * - 施放缴械、英勇打击、盾击、盾墙
     */
    struct boss_lord_crispin_ferenceAI : public boss_moroes_guestAI
    {
        /**
         * @brief 构造函数
         * @param creature 生物对象指针
         */
        boss_lord_crispin_ferenceAI(Creature* creature) : boss_moroes_guestAI(creature)
        {
            Initialize();
        }

        /**
         * @brief 初始化成员变量
         */
        void Initialize()
        {
            Disarm_Timer = 6000;
            HeroicStrike_Timer = 10000;
            ShieldBash_Timer = 8000;
            ShieldWall_Timer = 4000;
        }

        uint32 Disarm_Timer;        ///< 缴械计时器(毫秒)
        uint32 HeroicStrike_Timer;  ///< 英勇打击计时器(毫秒)
        uint32 ShieldBash_Timer;    ///< 盾击计时器(毫秒)
        uint32 ShieldWall_Timer;    ///< 盾墙计时器(毫秒)

        /**
         * @brief 重置AI状态
         */
        void Reset() override
        {
            Initialize();

            boss_moroes_guestAI::Reset();
        }

        /**
         * @brief 更新AI
         * @param diff 时间差(毫秒)
         *
         * 功能: 处理缴械、英勇打击、盾击、盾墙的施放
         */
        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictim())
                return;

            boss_moroes_guestAI::UpdateAI(diff);

            // 施放缴械
            if (Disarm_Timer <= diff)
            {
                DoCastVictim(SPELL_DISARM);
                Disarm_Timer = 12000;
            } else Disarm_Timer -= diff;

            // 施放英勇打击
            if (HeroicStrike_Timer <= diff)
            {
                DoCastVictim(SPELL_HEROICSTRIKE);
                HeroicStrike_Timer = 10000;
            } else HeroicStrike_Timer -= diff;

            // 施放盾击
            if (ShieldBash_Timer <= diff)
            {
                DoCastVictim(SPELL_SHIELDBASH);
                ShieldBash_Timer = 13000;
            } else ShieldBash_Timer -= diff;

            // 施放盾墙
            if (ShieldWall_Timer <= diff)
            {
                DoCast(me, SPELL_SHIELDWALL);
                ShieldWall_Timer = 21000;
            } else ShieldWall_Timer -= diff;
        }
    };
};

/**
 * @brief 注册莫罗斯BOSS脚本
 *
 * 调用时机: 服务器启动时加载脚本模块
 * 功能: 创建莫罗斯和所有仆从脚本实例，注册到脚本系统
 */
void AddSC_boss_moroes()
{
    new boss_moroes();
    new boss_baroness_dorothea_millstipe();
    new boss_baron_rafe_dreuger();
    new boss_lady_catriona_von_indi();
    new boss_lady_keira_berrybuck();
    new boss_lord_robin_daris();
    new boss_lord_crispin_ference();
}
