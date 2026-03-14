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
 * @file pet_mage.cpp
 * @brief 法师宠物AI模块
 *
 * 本模块实现法师职业相关宠物的AI行为，包括：
 * - 镜像 (Mirror Image)：由镜像技能召唤的复制体，模仿法师施放寒冰箭和火焰冲击
 *
 * 镜像的特殊行为：
 * - 召唤时复制法师外观
 * - 自动攻击法师战斗中的目标
 * - 优先攻击PvP目标（玩家），然后是PvE目标（仇恨最高的）
 * - 不会因脱战而重置模型（防止视觉丢失）
 *
 * Ordered alphabetically using scriptname.
 * Scriptnames of files in this file should be prefixed with "npc_pet_mag_".
 */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "CombatAI.h"
#include "GridNotifiersImpl.h"
#include "MotionMaster.h"
#include "Pet.h"
#include "PetAI.h"
#include "ScriptedCreature.h"
#include "SpellHistory.h"
#include "Timer.h"

/**
 * @brief 法师法术ID枚举
 *
 * 定义镜像宠物使用的法术ID
 */
enum MageSpells
{
    SPELL_MAGE_CLONE_ME                 = 45204,  ///< 复制我法术 - 复制法师外观给镜像
    SPELL_MAGE_MASTERS_THREAT_LIST      = 58838,  ///< 主人仇恨列表 - 镜像使用的仇恨机制
    SPELL_MAGE_FROST_BOLT               = 59638,  ///< 寒冰箭 - 镜像主要攻击法术
    SPELL_MAGE_FIRE_BLAST               = 59637   ///< 火焰冲击 - 镜像次要攻击法术
};

/**
 * @brief 镜像计时器枚举
 *
 * 定义镜像施放法术的时间间隔
 */
enum MirrorImageTimers
{
    TIMER_MIRROR_IMAGE_FIRE_BLAST       = 6500    ///< 火焰冲击冷却时间（毫秒）
};

/**
 * @brief 法师镜像AI
 *
 * 继承自ScriptedAI，实现法师镜像技能召唤的镜像行为逻辑。
 * 镜像会复制法师外观，并自动攻击法师当前的目标或仇恨最高的目标。
 *
 * 特殊行为：
 * - 召唤时复制法师外观（施放复制我法术）
 * - 定期施放寒冰箭，每6.5秒施放一次火焰冲击
 * - 智能选择攻击目标（优先PvP目标，然后是PvE目标）
 * - 不会因脱战而重置模型（防止视觉丢失）
 */
struct npc_pet_mage_mirror_image : ScriptedAI
{
    const float CHASE_DISTANCE = 35.0f;  ///< 追击距离（码）

    /**
     * @brief 构造函数
     * @param creature 生物对象指针
     */
    npc_pet_mage_mirror_image(Creature* creature) : ScriptedAI(creature), _fireBlastTimer(0) { }

    /**
     * @brief 初始化AI
     *
     * 调用时机：宠物被创建后立即调用
     *
     * 功能：
     * 1. 获取主人
     * 2. 主人对镜像施放复制我法术，复制外观
     *
     * 注意：
     * - 这里镜像会对召唤者施放不在客户端DBC中的法术49866
     * - 应该有不在客户端DBC中的光环：35657, 35658, 35659, 35660 由镜像自身施放（与属性相关？）
     *
     * 性能注意：召唤时一次性调用
     */
    void InitializeAI() override
    {
        Unit* owner = me->GetOwner();
        if (!owner)
            return;

        // 这里镜像会对召唤者施放法术（不在客户端DBC中）49866
        // 这里应该有光环（不在客户端DBC中）：35657, 35658, 35659, 35660 由镜像自身施放（与属性相关？）
        // 复制我！
        owner->CastSpell(me, SPELL_MAGE_CLONE_ME, true);
    }

    /**
     * @brief 更新AI
     * @param diff 时间差（毫秒）
     *
     * 调用时机：每个游戏循环tick
     *
     * 功能：
     * 1. 检查主人是否存在，不存在则消失
     * 2. 更新火焰冲击计时器
     * 3. 更新攻击目标
     * 4. 如果正在施法，等待完成
     * 5. 施放法术：
     *    - 火焰冲击冷却完成时施放火焰冲击
     *    - 否则施放寒冰箭
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

        // 更新火焰冲击计时器
        if (!_fireBlastTimer.Passed())
            _fireBlastTimer.Update(diff);

        // 更新攻击目标
        if (!UpdateImageVictim())
            return;

        // 如果正在施法，等待完成
        if (me->HasUnitState(UNIT_STATE_CASTING))
            return;

        // 施放法术
        if (_fireBlastTimer.Passed())
        {
            // 火焰冲击冷却完成，施放火焰冲击
            DoCastVictim(SPELL_MAGE_FIRE_BLAST);
            _fireBlastTimer.Reset(TIMER_MIRROR_IMAGE_FIRE_BLAST);
        }
        else
            // 否则施放寒冰箭
            DoCastVictim(SPELL_MAGE_FROST_BOLT);
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
     * 1. 如果死亡，结束战斗
     * 2. 清理战斗状态
     * 3. 跟随主人（不重置模型，防止视觉丢失）
     *
     * 性能注意：脱战时一次性调用
     */
    // Do not reload Creature templates on evade mode enter - prevent visual lost
    void EnterEvadeMode(EvadeReason /*why*/) override
    {
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
     * 2. PvP目标（选择距离主人最近的玩家）
     * 3. PvE目标（选择主人仇恨最高的）
     *
     * 目标选择基于主人对目标的仇恨值，确保镜像攻击主人最想攻击的目标。
     *
     * 性能注意：每帧调用，遍历战斗引用列表
     */
    // custom UpdateVictim implementation to handle special target selection
    // we prioritize between things that are in combat with owner based on the owner's threat to them
    bool UpdateImageVictim()
    {
        Unit* owner = me->GetOwner();
        if (!owner)
            return false;

        // 如果未施法、未交战且主人不在战斗中，返回
        if (!me->HasUnitState(UNIT_STATE_CASTING) && !me->IsEngaged() && !owner->IsInCombat())
            return false;

        // 检查当前目标是否仍然有效
        Unit* currentTarget = me->GetVictim();
        if (currentTarget && !CanAIAttack(currentTarget))
        {
            me->InterruptNonMeleeSpells(true); // 不要在无效目标上完成施法
            me->AttackStop();
            currentTarget = nullptr;
        }

        // 如果当前正在施法，不要重新选择目标
        if (currentTarget && me->HasUnitState(UNIT_STATE_CASTING))
            return true;

        Unit* selectedTarget = nullptr;
        CombatManager const& mgr = owner->GetCombatManager();
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
            AttackStartCaster(selectedTarget, CHASE_DISTANCE);
        return true;
    }

    TimeTracker _fireBlastTimer;  ///< 火焰冲击冷却计时器
};

/**
 * @brief 注册法师宠物脚本
 *
 * 调用时机：服务器启动时由脚本加载器调用
 *
 * 功能：注册所有法师宠物AI到脚本系统
 */
void AddSC_mage_pet_scripts()
{
    RegisterCreatureAI(npc_pet_mage_mirror_image);
}
