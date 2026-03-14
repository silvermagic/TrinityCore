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
 * @file pet_dk.cpp
 * @brief 死亡骑士宠物AI模块
 *
 * 本模块实现死亡骑士职业相关宠物的AI行为，包括：
 * - 黑锋石像鬼 (Ebon Gargoyle)：由召唤石像鬼法术产生的飞行宠物，自动攻击目标
 * - 守护者 (Guardian)：由亡者大军召唤的食尸鬼，只攻击与主人战斗的目标
 * - 符文武器 (Rune Weapon)：由符文刃舞召唤的复制武器，模仿主人攻击目标
 *
 * 这些宠物具有特殊的AI逻辑，与普通宠物不同：
 * - 石像鬼会在被解散时飞走
 * - 符文武器会复制主人的武器外观并同步攻击目标
 *
 * Ordered alphabetically using scriptname.
 * Scriptnames of files in this file should be prefixed with "npc_pet_dk_".
 */

#include "ScriptMgr.h"
#include "CombatAI.h"
#include "CellImpl.h"
#include "GridNotifiersImpl.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ScriptedCreature.h"
#include "SpellScript.h"

/**
 * @brief 死亡骑士宠物相关法术ID枚举
 *
 * 定义了死亡骑士宠物AI使用的各种法术ID，
 * 包括石像鬼和符文武器的召唤、视觉、缩放等法术。
 */
enum DeathKnightSpells
{
    SPELL_DK_SUMMON_GARGOYLE_1 = 49206,       ///< 召唤石像鬼法术1 - 用于识别石像鬼的攻击目标
    SPELL_DK_SUMMON_GARGOYLE_2 = 50514,       ///< 召唤石像鬼法术2 - 持续消耗符文能量的buff
    SPELL_DK_DISMISS_GARGOYLE = 50515,        ///< 解散石像鬼法术 - 使石像鬼飞走
    SPELL_DK_SANCTUARY = 54661,               ///< 圣域法术 - 使单位免疫攻击
    SPELL_DK_DANCING_RUNE_WEAPON = 49028,     ///< 符文刃舞主法术
    SPELL_COPY_WEAPON = 63416,                ///< 复制武器法术 - 复制主人的武器外观
    SPELL_DK_RUNE_WEAPON_MARK = 50474,        ///< 符文武器标记 - 视觉效果
    SPELL_DK_DANCING_RUNE_WEAPON_VISUAL = 53160, ///< 符文刃舞视觉效果法术
    SPELL_FAKE_AGGRO_RADIUS_8_YARD = 49812,   ///< 虚假8码仇恨范围 - 用于符文武器的仇恨机制
    SPELL_DK_RUNE_WEAPON_SCALING_01 = 51905,  ///< 符文武器缩放01 - 宠物属性缩放
    SPELL_DK_RUNE_WEAPON_SCALING = 51906,     ///< 符文武器缩放 - 宠物属性缩放
    SPELL_PET_SCALING__MASTER_SPELL_06__SPELL_HIT_EXPERTISE_SPELL_PENETRATION = 67561, ///< 宠物缩放 - 命中和法术穿透
    SPELL_DK_PET_SCALING_03 = 61697,          ///< 死亡骑士宠物缩放03 - 属性继承
    SPELL_AGGRO_8_YD_PBAE = 49813,            ///< 8码范围仇恨法术 - 用于符文武器拉仇恨
    SPELL_DISMISS_RUNEBLADE = 50707,          ///< 解散符文剑 - 目前通过持续时间自动消失
};

/**
 * @brief 黑锋石像鬼AI
 *
 * 继承自CasterAI，实现死亡骑士召唤石像鬼的行为逻辑。
 * 石像鬼是一种飞行宠物，会自动攻击被召唤石像鬼法术标记的目标。
 * 当被解散时，石像鬼会飞走而不是直接消失。
 *
 * 特殊行为：
 * - 初始化时自动寻找并攻击被召唤法术标记的目标
 * - 死亡时移除主人的喂养石像鬼buff
 * - 被解散时播放飞走动画
 */
struct npc_pet_dk_ebon_gargoyle : CasterAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_pet_dk_ebon_gargoyle(Creature* creature) : CasterAI(creature) { }

    /**
     * @brief 初始化AI
     *
     * 调用时机：宠物被创建后立即调用
     *
     * 功能：
     * 1. 调用父类初始化
     * 2. 在30码范围内搜索带有召唤石像鬼法术debuff的目标
     * 3. 找到后立即开始攻击该目标
     *
     * 性能注意：使用Cell访问器遍历附近对象，时间复杂度O(n)，n为附近对象数量
     */
    void InitializeAI() override
    {
        CasterAI::InitializeAI();
        ObjectGuid ownerGuid = me->GetOwnerGUID();
        if (!ownerGuid)
            return;

        // 寻找召唤石像鬼法术的目标
        // 遍历30码范围内的所有敌对单位
        std::list<Unit*> targets;
        Trinity::AnyUnfriendlyUnitInObjectRangeCheck u_check(me, me, 30.0f);
        Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(me, targets, u_check);
        Cell::VisitAllObjects(me, searcher, 30.0f);
        for (Unit* target : targets)
        {
            // 检查目标是否被主人标记了召唤石像鬼法术
            if (target->HasAura(SPELL_DK_SUMMON_GARGOYLE_1, ownerGuid))
            {
                me->Attack(target, false);
                break;
            }
        }
    }

    /**
     * @brief 死亡回调
     * @param killer 击杀者（未使用）
     *
     * 调用时机：石像鬼死亡时
     *
     * 功能：移除主人身上的喂养石像鬼buff，停止符文能量消耗
     */
    void JustDied(Unit* /*killer*/) override
    {
        // 石像鬼死亡时停止喂养石像鬼效果
        if (Unit* owner = me->GetOwner())
            owner->RemoveAurasDueToSpell(SPELL_DK_SUMMON_GARGOYLE_2);
    }

    /**
     * @brief 法术命中回调
     * @param caster 施法者
     * @param spellInfo 法术信息
     *
     * 调用时机：当法术命中该生物时
     *
     * 功能：处理解散石像鬼法术，使石像鬼飞走
     * - 设置不可攻击状态
     * - 施加圣域效果免疫伤害
     * - 设置被动反应状态
     * - 启用飞行并计算飞走路径
     * - 4秒后消失
     *
     * 性能注意：涉及坐标计算和移动路径设置
     */
    // Fly away when dismissed
    void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override
    {
        // 只处理解散石像鬼法术
        if (spellInfo->Id != SPELL_DK_DISMISS_GARGOYLE || !me->IsAlive())
            return;

        Unit* owner = me->GetOwner();
        if (!owner || owner != caster)
            return;

        // 停止战斗
        me->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);

        // 施加圣域效果，免疫伤害
        me->CastSpell(me, SPELL_DK_SANCTUARY, true);
        me->SetReactState(REACT_PASSIVE);

        //! HACK: 生物无法拥有MOVEMENTFLAG_FLYING标志
        // 飞走动画
        me->SetCanFly(true);
        me->SetSpeedRate(MOVE_FLIGHT, 0.75f);
        me->SetSpeedRate(MOVE_RUN, 0.75f);
        // 计算飞走的目标位置：向前20码，向上40码
        float x = me->GetPositionX() + 20 * std::cos(me->GetOrientation());
        float y = me->GetPositionY() + 20 * std::sin(me->GetOrientation());
        float z = me->GetPositionZ() + 40;
        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MovePoint(0, x, y, z);

        // 尽快消失
        me->DespawnOrUnsummon(Seconds(4));
    }
};

/**
 * @brief 死亡骑士守护者AI
 *
 * 继承自AggressorAI，实现死亡骑士守护者（如亡者大军召唤的食尸鬼）的行为逻辑。
 * 守护者只会攻击与主人处于战斗状态的目标，不会主动拉新怪。
 *
 * 主要用于亡者大军技能召唤的食尸鬼。
 */
struct npc_pet_dk_guardian : public AggressorAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_pet_dk_guardian(Creature* creature) : AggressorAI(creature) { }

    /**
     * @brief 检查AI是否可以攻击目标
     * @param target 目标单位
     * @return 是否可以攻击
     *
     * 调用时机：每次选择攻击目标时
     *
     * 功能：
     * 1. 检查目标是否有效
     * 2. 确保目标与主人处于战斗状态
     * 3. 调用父类检查
     *
     * 性能注意：该函数在每次目标选择时都会调用，应保持高效
     */
    bool CanAIAttack(Unit const* target) const override
    {
        if (!target)
            return false;
        Unit* owner = me->GetOwner();
        // 只攻击与主人战斗的目标
        if (owner && !target->IsInCombatWith(owner))
            return false;
        return AggressorAI::CanAIAttack(target);
    }
};

/**
 * @brief 符文刃舞杂项枚举
 *
 * 定义符文武器AI使用的任务组和数据ID
 */
enum DancingRuneWeaponMisc
{
    TASK_GROUP_COMBAT = 1,          ///< 战斗任务组ID - 用于取消战斗相关定时任务
    DATA_INITIAL_TARGET_GUID = 1,   ///< 初始目标GUID数据ID - 用于传递初始攻击目标
};

/**
 * @brief 符文武器AI
 *
 * 继承自ScriptedAI，实现死亡骑士符文刃舞技能召唤的符文武器行为逻辑。
 * 符文武器会复制主人的武器外观，并自动攻击主人当前的目标或仇恨最高的目标。
 *
 * 特殊行为：
 * - 召唤时复制主人武器外观
 * - 定期施放视觉效果法术
 * - 智能选择攻击目标（优先初始目标，然后是PvP目标，最后是PvE目标）
 * - 不会因脱战而重置模型（防止视觉丢失）
 */
struct npc_pet_dk_rune_weapon : ScriptedAI
{
    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_pet_dk_rune_weapon(Creature* creature) : ScriptedAI(creature) { }

    /**
     * @brief 被召唤时的回调
     * @param summoner 召唤者对象
     *
     * 调用时机：符文武器被召唤时
     *
     * 功能：
     * 1. 设置被动状态（等待初始化）
     * 2. 施放各种初始化法术：
     *    - 复制主人武器外观
     *    - 施加符文武器标记
     *    - 施加视觉效果
     *    - 施加属性缩放法术
     * 3. 调度器安排：
     *    - 500ms后激活攻击状态
     *    - 每6秒刷新视觉效果
     *
     * 性能注意：召唤时一次性施放多个法术
     */
    void IsSummonedBy(WorldObject* summoner) override
    {
        // 初始设置为被动，等待初始化完成
        me->SetReactState(REACT_PASSIVE);

        // 确保召唤者是单位（玩家）
        if (summoner->GetTypeId() != TYPEID_UNIT)
            return;

        Unit* unitSummoner = summoner->ToUnit();

        // 施放初始化法术
        DoCast(unitSummoner, SPELL_COPY_WEAPON, true);      // 复制主人武器外观
        DoCast(unitSummoner, SPELL_DK_RUNE_WEAPON_MARK, true); // 施加符文武器标记
        DoCastSelf(SPELL_DK_DANCING_RUNE_WEAPON_VISUAL, true); // 施加视觉效果
        DoCastSelf(SPELL_FAKE_AGGRO_RADIUS_8_YARD, true);   // 8码仇恨范围
        DoCastSelf(SPELL_DK_RUNE_WEAPON_SCALING_01, true);  // 属性缩放01
        DoCastSelf(SPELL_DK_RUNE_WEAPON_SCALING, true);     // 属性缩放
        DoCastSelf(SPELL_PET_SCALING__MASTER_SPELL_06__SPELL_HIT_EXPERTISE_SPELL_PENETRATION, true); // 命中和穿透
        DoCastSelf(SPELL_DK_PET_SCALING_03, true);          // 属性继承

        // 调度任务
        _scheduler.Schedule(500ms, [this](TaskContext /*activate*/)
        {
            // 500ms后激活攻击状态
            me->SetReactState(REACT_AGGRESSIVE);
            // 如果有初始目标，开始攻击
            if (!_targetGUID.IsEmpty())
            {
                if (Unit* target = ObjectAccessor::GetUnit(*me, _targetGUID))
                    me->EngageWithTarget(target);
            }
        }).Schedule(6s, [this](TaskContext visual)
        {
            // 每6秒刷新视觉效果
            DoCastSelf(SPELL_DK_DANCING_RUNE_WEAPON_VISUAL, true);
            visual.Repeat();
        });
    }

    /**
     * @brief 设置GUID数据
     * @param guid 要设置的GUID
     * @param id 数据ID
     *
     * 调用时机：外部设置初始攻击目标时
     *
     * 功能：存储初始攻击目标的GUID，用于召唤后的目标选择
     */
    void SetGUID(ObjectGuid const& guid, int32 id) override
    {
        if (id == DATA_INITIAL_TARGET_GUID)
            _targetGUID = guid;
    }

    /**
     * @brief 进入战斗回调
     * @param who 进入战斗的目标
     *
     * 调用时机：符文武器开始战斗时
     *
     * 功能：安排每秒施放8码范围仇恨法术，用于拉取附近敌人
     */
    void JustEnteredCombat(Unit* who) override
    {
        ScriptedAI::JustEnteredCombat(who);

        // 安排每秒施放仇恨法术
        // 需要进一步调查这些施法是否由任何拥有的光环完成，无论如何SMSG_SPELL_GO每X秒发送一次
        _scheduler.Schedule(1s, TASK_GROUP_COMBAT, [this](TaskContext aggro8YD)
        {
            // 每秒施放
            if (Unit* victim = me->GetVictim())
                DoCast(victim, SPELL_AGGRO_8_YD_PBAE, true);
            aggro8YD.Repeat();
        });
    }

    /**
     * @brief 更新AI
     * @param diff 时间差（毫秒）
     *
     * 调用时机：每个游戏循环tick
     *
     * 功能：
     * 1. 检查主人是否存在，不存在则消失
     * 2. 更新调度器
     * 3. 更新攻击目标
     * 4. 执行近战攻击
     *
     * 性能注意：每帧调用，应保持高效
     */
    void UpdateAI(uint32 diff) override
    {
        // 检查主人
        Unit* owner = me->GetOwner();
        if (!owner)
        {
            me->DespawnOrUnsummon();
            return;
        }

        // 更新调度器
        _scheduler.Update(diff);

        // 更新攻击目标
        if (!UpdateRuneWeaponVictim())
            return;

        // 执行近战攻击
        DoMeleeAttackIfReady();
    }

    /**
     * @brief 检查AI是否可以攻击目标
     * @param who 目标单位
     * @return 是否可以攻击
     *
     * 调用时机：每次选择攻击目标时
     *
     * 检查条件：
     * 1. 主人存在
     * 2. 目标存活
     * 3. 目标有效攻击目标
     * 4. 目标没有可被伤害打破的控制光环
     * 5. 目标与主人处于战斗状态
     * 6. 父类检查通过
     *
     * 性能注意：每次目标选择时调用，需保持高效
     */
    bool CanAIAttack(Unit const* who) const override
    {
        Unit* owner = me->GetOwner();
        return owner && who->IsAlive() && me->IsValidAttackTarget(who) && !who->HasBreakableByDamageCrowdControlAura() && who->IsInCombatWith(owner) && ScriptedAI::CanAIAttack(who);
    }

    /**
     * @brief 进入脱战模式
     * @param why 脱战原因
     *
     * 调用时机：失去所有敌对目标或目标变为无效时
     *
     * 功能：
     * 1. 取消战斗任务组
     * 2. 如果死亡，结束战斗
     * 3. 清理战斗状态
     * 4. 跟随主人（不重置模型，防止视觉丢失）
     *
     * 性能注意：脱战时一次性调用
     */
    // Do not reload Creature templates on evade mode enter - prevent visual lost
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
        // 取消战斗相关任务
        _scheduler.CancelGroup(TASK_GROUP_COMBAT);

        if (!me->IsAlive())
        {
            EngagementOver();
            return;
        }

        Unit* owner = me->GetCharmerOrOwner();

        // 清理战斗状态
        me->CombatStop(true);
        me->SetLootRecipient(nullptr);
        me->ResetPlayerDamageReq();
        me->SetLastDamagedTime(0);
        me->SetCannotReachTarget(false);
        me->DoNotReacquireSpellFocusTarget();
        me->SetTarget(ObjectGuid::Empty);
        EngagementOver();

        // 跟随主人（不重新加载生物模板，防止视觉丢失）
        if (owner && !me->HasUnitState(UNIT_STATE_FOLLOW))
        {
            me->GetMotionMaster()->Clear();
            me->GetMotionMaster()->MoveFollow(owner, PET_FOLLOW_DIST, me->GetFollowAngle());
        }
    }

private:
    /**
     * @brief 自定义更新目标实现
     * @return 是否有有效目标
     *
     * 调用时机：每次UpdateAI调用
     *
     * 功能：
     * 自定义的目标选择逻辑，优先级如下：
     * 1. 当前目标（如果仍然有效）
     * 2. 初始目标（如果仍然有效）
     * 3. PvP目标（选择距离主人最近的）
     * 4. PvE目标（选择主人仇恨最高的）
     *
     * 目标选择基于主人对目标的仇恨值，确保符文武器攻击主人最想攻击的目标。
     *
     * 性能注意：每帧调用，遍历战斗引用列表
     */
    // custom UpdateVictim implementation to handle special target selection
    // we prioritize between things that are in combat with owner based on the owner's threat to them
    bool UpdateRuneWeaponVictim()
    {
        Unit* owner = me->GetOwner();
        if (!owner)
            return false;

        // 如果未交战且主人不在战斗中，返回
        if (!me->IsEngaged() && !owner->IsInCombat())
            return false;

        // 检查当前目标是否仍然有效
        Unit* currentTarget = me->GetVictim();
        if (currentTarget && !CanAIAttack(currentTarget))
        {
            me->InterruptNonMeleeSpells(true); // 不要在无效目标上完成施法
            me->AttackStop();
            currentTarget = nullptr;
        }

        Unit* selectedTarget = nullptr;

        // 首先尝试获取初始目标
        if (Unit* initialTarget = ObjectAccessor::GetUnit(*me, _targetGUID))
        {
            if (CanAIAttack(initialTarget))
                selectedTarget = initialTarget;
        }
        else if (!_targetGUID.IsEmpty())
            _targetGUID.Clear(); // 目标已不存在，清除GUID

        // 如果没有初始目标，从战斗引用中选择
        CombatManager const& mgr = owner->GetCombatManager();
        if (!selectedTarget)
        {
            if (mgr.HasPvPCombat())
            {
                // 选择PvP目标（玩家），优先最近的
                float minDistance = 0.f;
                for (auto const& pair : mgr.GetPvPCombatRefs())
                {
                    Unit* target = pair.second->GetOther(owner);
                    // 只选择玩家目标
                    if (target->GetTypeId() != TYPEID_PLAYER)
                        continue;
                    if (!CanAIAttack(target))
                        continue;

                    float dist = owner->GetDistance(target);
                    if (!selectedTarget || dist < minDistance)
                    {
                        selectedTarget = target;
                        minDistance = dist;
                    }
                }
            }
        }

        // 如果没有PvP目标，选择PvE目标
        if (!selectedTarget)
        {
            // 选择主人仇恨最高的目标
            float maxThreat = 0.f;
            for (auto const& pair : mgr.GetPvECombatRefs())
            {
                Unit* target = pair.second->GetOther(owner);
                if (!CanAIAttack(target))
                    continue;

                float threat = target->GetThreatManager().GetThreat(owner);
                if (threat >= maxThreat)
                {
                    selectedTarget = target;
                    maxThreat = threat;
                }
            }
        }

        // 如果没有找到有效目标，脱战
        if (!selectedTarget)
        {
            EnterEvadeMode(EVADE_REASON_NO_HOSTILES);
            return false;
        }

        // 如果目标改变，开始攻击新目标
        if (selectedTarget != me->GetVictim())
            AttackStart(selectedTarget);
        return true;
    }

    TaskScheduler _scheduler;   ///< 任务调度器，用于管理定时任务
    ObjectGuid _targetGUID;     ///< 初始攻击目标的GUID
};

/**
 * @brief 注册死亡骑士宠物脚本
 *
 * 调用时机：服务器启动时由脚本加载器调用
 *
 * 功能：注册所有死亡骑士宠物AI到脚本系统
 */
void AddSC_deathknight_pet_scripts()
{
    RegisterCreatureAI(npc_pet_dk_ebon_gargoyle);
    RegisterCreatureAI(npc_pet_dk_guardian);
    RegisterCreatureAI(npc_pet_dk_rune_weapon);
}
