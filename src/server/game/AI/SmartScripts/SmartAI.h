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
 * @file SmartAI.h
 * @brief SmartAI 智能AI系统头文件
 *
 * 本模块实现了智能AI系统(SmartAI)，这是一个高度灵活的脚本驱动AI框架。
 * SmartAI 允许通过数据库配置来创建复杂的生物行为，无需编写C++代码。
 *
 * 主要特性：
 * - 事件驱动的行为系统：支持多种触发事件（战斗、移动、法术、对话等）
 * - 动作执行系统：支持丰富的动作类型（施法、移动、召唤、对话等）
 * - 路径系统：支持生物按照预定路径移动和巡逻
 * - 护送系统：实现NPC护送玩家或被玩家护送的功能
 * - 跟随系统：实现NPC跟随特定目标的功能
 * - 条件系统：支持复杂的行为触发条件
 *
 * SmartAI 被广泛应用于：
 * - 任务NPC
 * - Boss战斗
 * - 世界事件
 * - 副本机制
 * - 生物巡逻
 *
 * 核心类：
 * - SmartAI: 生物智能AI实现
 * - SmartGameObjectAI: 游戏对象智能AI实现
 * - SmartScript: 核心脚本处理引擎
 */

#ifndef TRINITY_SMARTAI_H
#define TRINITY_SMARTAI_H

#include "Define.h"
#include "CreatureAI.h"
#include "GameObjectAI.h"
#include "Position.h"
#include "SmartScript.h"
#include "WaypointDefines.h"

/**
 * @brief 护送状态枚举
 *
 * 定义了SmartAI护送系统的各种状态，使用位掩码可以组合多个状态
 */
enum SmartEscortState : uint8
{
    SMART_ESCORT_NONE       = 0x00, ///< 无护送进行中，生物处于默认状态
    SMART_ESCORT_ESCORTING  = 0x01, ///< 护送任务进行中，生物正在沿路径移动
    SMART_ESCORT_RETURNING  = 0x02, ///< 战斗后返回中，生物正返回到脱战位置
    SMART_ESCORT_PAUSED     = 0x04  ///< 路径暂停状态，在移除此状态前不会继续移动到下一个路径点
};

/// 护送NPC与玩家之间的最大距离（超出此距离护送失败）
static float constexpr SMART_ESCORT_MAX_PLAYER_DIST = 60.f;

/// SmartAI协助玩家的最大距离（最大护送距离的一半）
static float constexpr SMART_MAX_AID_DIST = SMART_ESCORT_MAX_PLAYER_DIST / 2.f;

/**
 * @class SmartAI
 * @brief 智能AI类，提供强大的脚本驱动AI系统
 *
 * SmartAI 是 TrinityCore 中最灵活的 AI 实现之一，它通过数据库配置
 * 来定义生物的行为，无需编写 C++ 代码即可创建复杂的 AI 逻辑。
 *
 * 核心功能：
 * 1. 事件处理：响应各种游戏事件（进入战斗、死亡、法术命中、对话等）
 * 2. 动作执行：执行丰富的动作（施法、移动、召唤、对话、修改数据等）
 * 3. 路径系统：支持生物沿预设路径移动和巡逻
 * 4. 护送系统：实现NPC护送任务
 * 5. 跟随系统：实现NPC跟随目标
 *
 * 工作原理：
 * - SmartScript 对象负责处理事件和动作
 * - 通过数据库表 smart_scripts 定义事件-动作映射
 * - 支持条件系统来控制动作执行
 *
 * 使用场景：
 * - 任务NPC
 * - Boss战斗机制
 * - 副本事件
 * - 世界事件
 * - 复杂的NPC行为
 */
class TC_GAME_API SmartAI : public CreatureAI
{
    public:
        /**
         * @brief 析构函数
         */
        ~SmartAI() { }

        /**
         * @brief 构造函数
         * @param creature 关联的生物对象指针
         *
         * 初始化SmartAI实例，设置所有成员变量的默认值，
         * 检查是否有载具条件配置
         */
        explicit SmartAI(Creature* creature);

        /**
         * @brief 检查AI是否允许该生物类型
         * @param creature 要检查的生物对象
         * @return 返回 PERMIT_BASE_NO，表示需要通过其他方式启用
         *
         * 此函数由AI选择器调用，用于确定是否应该使用SmartAI
         */
        static int32 Permissible(Creature const* /*creature*/) { return PERMIT_BASE_NO; }

        /**
         * @brief 检查AI是否受控
         * @return 如果AI不受魅惑控制返回true，否则返回false
         *
         * 用于区分生物是自主行动还是被玩家魅惑控制
         * 被魅惑时，SmartAI的大部分功能会被限制
         */
        bool IsAIControlled() const;

        /**
         * @brief 开始路径移动
         * @param run 是否跑步移动（true=跑步，false=行走）
         * @param pathId 路径ID（0表示使用已加载的路径）
         * @param repeat 是否循环路径
         * @param invoker 触发者（通常是玩家），用于护送任务
         * @param nodeId 起始路径点ID（默认为1）
         *
         * 开始沿指定路径移动。如果是护送任务，会清除NPC的交互标志。
         * 如果已有路径在运行，会先停止当前路径。
         *
         * @note 此函数会设置护送状态为 SMART_ESCORT_ESCORTING
         */
        void StartPath(bool run = false, uint32 pathId = 0, bool repeat = false, Unit* invoker = nullptr, uint32 nodeId = 1);

        /**
         * @brief 加载路径数据
         * @param entry 路径ID（对应waypoint_data表）
         * @return 加载成功返回true，失败返回false
         *
         * 从数据库加载指定路径的点数据，会规范化坐标并根据run状态设置移动类型
         *
         * @note 如果正在护送中，此函数会返回false
         */
        bool LoadPath(uint32 entry);

        /**
         * @brief 暂停路径移动
         * @param delay 暂停时间（毫秒）
         * @param forced 是否强制暂停（立即停止移动并更新出生点）
         *
         * 暂停当前的路径移动，触发 SMART_EVENT_WAYPOINT_PAUSED 事件
         * 如果forced为true，会立即停止移动；否则会等待到达当前路径点
         *
         * @note 暂停状态下生物仍可进入战斗
         */
        void PausePath(uint32 delay, bool forced = false);

        /**
         * @brief 检查是否可以恢复路径移动
         * @return 如果可以恢复返回true，否则返回false
         *
         * 检查当前是否处于暂停状态且护送正在进行中
         */
        bool CanResumePath();

        /**
         * @brief 停止路径移动
         * @param DespawnTime 消失时间（毫秒），0表示不消失
         * @param quest 关联任务ID，用于任务失败判定
         * @param fail 是否标记为失败（true会导致任务失败）
         *
         * 停止当前的路径移动，触发相关事件，并根据参数决定是否消失
         * 如果是护送任务且fail为true，会通知玩家任务失败
         */
        void StopPath(uint32 DespawnTime = 0, uint32 quest = 0, bool fail = false);

        /**
         * @brief 结束路径
         * @param fail 是否标记为失败
         *
         * 清理护送状态，处理任务完成/失败逻辑，触发结束事件
         * 如果设置了循环，会在AI控制下自动重新开始路径
         */
        void EndPath(bool fail = false);

        /**
         * @brief 恢复路径移动
         *
         * 从暂停状态恢复路径移动，触发 SMART_EVENT_WAYPOINT_RESUMED 事件
         * 清除暂停状态并重置相关计时器
         */
        void ResumePath();

        /**
         * @brief 检查是否具有指定的护送状态
         * @param escortState 要检查的状态（可以是组合状态）
         * @return 如果具有该状态返回true，否则返回false
         */
        bool HasEscortState(uint32 escortState) const
        {
            return (_escortState & escortState) != 0;
        }

        /**
         * @brief 添加护送状态
         * @param escortState 要添加的状态
         *
         * 使用位或操作添加新状态到当前护送状态
         */
        void AddEscortState(uint32 escortState)
        {
            _escortState |= escortState;
        }

        /**
         * @brief 移除护送状态
         * @param escortState 要移除的状态
         *
         * 使用位与非操作移除指定状态
         */
        void RemoveEscortState(uint32 escortState)
        {
            _escortState &= ~escortState;
        }

        /**
         * @brief 设置是否允许自动攻击
         * @param on true允许自动攻击，false禁止自动攻击
         *
         * 控制生物在战斗中是否自动进行近战攻击
         */
        void SetAutoAttack(bool on)
        {
            _canAutoAttack = on;
        }

        /**
         * @brief 设置战斗移动状态
         * @param on true允许战斗中移动，false禁止战斗中移动
         * @param stopMoving 是否立即停止移动（当禁止移动时）
         *
         * 控制生物在战斗中是否追击目标
         * 禁止移动时，生物会在原地攻击
         */
        void SetCombatMove(bool on, bool stopMoving = false);

        /**
         * @brief 检查是否允许战斗移动
         * @return 如果允许返回true，否则返回false
         */
        bool CanCombatMove()
        {
            return _canCombatMove;
        }

        /**
         * @brief 设置跟随目标
         * @param target 跟随目标（nullptr表示停止跟随）
         * @param dist 跟随距离
         * @param angle 跟随角度
         * @param credit 任务积分ID（用于完成任务目标）
         * @param end 到达特定生物entry时结束跟随
         * @param creditType 积分类型（0=奖励组，1=组事件）
         *
         * 设置生物跟随指定目标移动，当到达特定生物时会停止并完成任务
         */
        void SetFollow(Unit* target, float dist = 0.0f, float angle = 0.0f, uint32 credit = 0, uint32 end = 0, uint32 creditType = 0);

        /**
         * @brief 停止跟随
         * @param complete 是否完成跟随任务
         *
         * 停止跟随目标，如果complete为true会给予任务积分并消失
         */
        void StopFollow(bool complete);

        /**
         * @brief 检查护送触发者是否在范围内
         * @return 如果在范围内返回true，否则返回false
         *
         * 检查护送任务的触发玩家是否在最大距离内
         * 在副本中，允许距离是普通情况的两倍
         *
         * @note 如果没有存储触发者，总是返回true
         */
        bool IsEscortInvokerInRange();

        /**
         * @brief 路径点到达回调
         * @param nodeId 到达的路径点ID
         * @param pathId 路径ID
         *
         * 当生物到达路径点时调用，触发 SMART_EVENT_WAYPOINT_REACHED 事件
         * 如果设置了暂停计时器且非强制暂停，会在此处暂停移动
         */
        void WaypointReached(uint32 nodeId, uint32 pathId) override;

        /**
         * @brief 路径结束回调
         * @param nodeId 最后的路径点ID
         * @param pathId 路径ID
         *
         * 当路径的所有点都走完时调用，触发 SMART_EVENT_WAYPOINT_ENDED 事件
         */
        void WaypointPathEnded(uint32 nodeId, uint32 pathId) override;

        /**
         * @brief 设置定时动作列表
         * @param e SmartScript持有对象引用
         * @param entry 动作列表ID
         * @param invoker 触发者
         *
         * 设置一个定时执行的SmartScript动作列表
         */
        void SetTimedActionList(SmartScriptHolder& e, uint32 entry, Unit* invoker);

        /**
         * @brief 获取SmartScript对象
         * @return SmartScript对象的指针
         *
         * 返回关联的SmartScript实例，用于访问脚本功能
         */
        SmartScript* GetScript()
        {
            return &_script;
        }

        /**
         * @brief 到达出生点回调
         *
         * 在生物逃避后返回出生点时调用
         * 重置AI状态并恢复默认行为（如路径巡逻）
         * 触发 SMART_EVENT_REACHED_HOME 事件
         */
        void JustReachedHome() override;

        /**
         * @brief 进入战斗回调
         * @param enemy 敌对目标（可能为nullptr）
         *
         * 当生物首次进入战斗时调用
         * 触发 SMART_EVENT_AGGRO 事件
         * 如果AI受控，会中断非近战法术
         */
        void JustEngagedWith(Unit* enemy) override;

        /**
         * @brief 进入逃避模式回调
         * @param why 逃避原因
         *
         * 当生物脱战时调用
         * 如果逃避被禁用，仅触发事件不执行逃避
         * 根据当前状态决定返回出生点、继续护送或跟随目标
         */
        void EnterEvadeMode(EvadeReason why = EVADE_REASON_OTHER) override;

        /**
         * @brief 死亡回调
         * @param killer 击杀者
         *
         * 当生物死亡时调用
         * 如果在护送中，会结束护送路径（失败）
         * 触发 SMART_EVENT_DEATH 事件
         */
        void JustDied(Unit* killer) override;

        /**
         * @brief 击杀单位回调
         * @param victim 被击杀的单位
         *
         * 当生物击杀其他单位时调用
         * 触发 SMART_EVENT_KILL 事件
         */
        void KilledUnit(Unit* victim) override;

        /**
         * @brief 召唤生物回调
         * @param creature 被召唤的生物
         *
         * 当生物成功召唤其他生物时调用
         * 触发 SMART_EVENT_SUMMONED_UNIT 事件
         */
        void JustSummoned(Creature* creature) override;

        /**
         * @brief 召唤物死亡回调
         * @param summon 死亡的召唤物
         * @param killer 击杀者
         *
         * 当被召唤的单位死亡时调用
         * 触发 SMART_EVENT_SUMMONED_UNIT_DIES 事件
         */
        void SummonedCreatureDies(Creature* summon, Unit* killer) override;

        /**
         * @brief 开始攻击
         * @param who 攻击目标
         *
         * 指示生物攻击并追击目标
         * 如果AI不受控，仅设置攻击目标不移动
         * 如果允许战斗移动，会追击目标
         */
        void AttackStart(Unit* who) override;

        /**
         * @brief 视线内移动回调
         * @param who 进入视线范围的单位
         *
         * 当单位进入生物视线范围时调用
         * 如果在护送中，会尝试协助玩家战斗
         */
        void MoveInLineOfSight(Unit* who) override;

        /**
         * @brief 被法术命中回调
         * @param caster 施法者
         * @param spellInfo 法术信息
         *
         * 当生物被法术命中时调用
         * 触发 SMART_EVENT_SPELLHIT 事件
         */
        void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override;

        /**
         * @brief 法术命中目标回调
         * @param target 目标对象
         * @param spellInfo 法术信息
         *
         * 当生物施放的法术命中目标时调用
         * 触发 SMART_EVENT_SPELLHIT_TARGET 事件
         */
        void SpellHitTarget(WorldObject* target, SpellInfo const* spellInfo) override;

        /**
         * @brief 法术施放完成回调
         * @param spellInfo 法术信息
         *
         * 当法术施放完成时调用
         * 触发 SMART_EVENT_ON_SPELL_CAST 事件
         */
        void OnSpellCast(SpellInfo const* spellInfo) override;

        /**
         * @brief 法术施放失败回调
         * @param spellInfo 法术信息
         *
         * 当法术施放失败时调用
         * 触发 SMART_EVENT_ON_SPELL_FAILED 事件
         */
        void OnSpellFailed(SpellInfo const* spellInfo) override;

        /**
         * @brief 法术开始施放回调
         * @param spellInfo 法术信息
         *
         * 当法术开始施放时调用
         * 触发 SMART_EVENT_ON_SPELL_START 事件
         */
        void OnSpellStart(SpellInfo const* spellInfo) override;

        /**
         * @brief 受到伤害回调
         * @param doneBy 伤害来源
         * @param damage 伤害值（可修改）
         * @param damageType 伤害类型
         * @param spellInfo 法术信息（可能为nullptr）
         *
         * 当生物受到伤害时调用（伤害应用前）
         * 触发 SMART_EVENT_DAMAGED 事件
         * 如果设置了无敌HP等级，会限制伤害不超过该值
         */
        void DamageTaken(Unit* doneBy, uint32& damage, DamageEffectType /*damageType*/, SpellInfo const* /*spellInfo = nullptr*/) override;

        /**
         * @brief 接受治疗回调
         * @param doneBy 治疗来源
         * @param addhealth 治疗量
         *
         * 当生物接受治疗时调用
         * 触发 SMART_EVENT_RECEIVE_HEAL 事件
         */
        void HealReceived(Unit* doneBy, uint32& addhealth) override;

        /**
         * @brief 更新AI
         * @param diff 距上次更新的时间差（毫秒）
         *
         * 每个世界更新周期调用一次
         * 更新SmartScript、路径、跟随和消失状态
         * 如果有战斗目标且允许自动攻击，执行近战攻击
         */
        void UpdateAI(uint32 diff) override;

        /**
         * @brief 接收表情回调
         * @param player 发送表情的玩家
         * @param textEmote 表情ID
         *
         * 当玩家对生物做表情时调用
         * 触发 SMART_EVENT_RECEIVE_EMOTE 事件
         */
        void ReceiveEmote(Player* player, uint32 textEmote) override;

        /**
         * @brief 移动信息回调
         * @param MovementType 移动类型
         * @param Data 移动数据
         *
         * 当路径点到达或点移动完成时调用
         * 如果是返回脱战位置移动完成，清除逃避状态
         * 触发 SMART_EVENT_MOVEMENTINFORM 事件
         */
        void MovementInform(uint32 MovementType, uint32 Data) override;

        /**
         * @brief 被召唤回调
         * @param summoner 召唤者
         *
         * 当生物被其他单位召唤时调用
         * 触发 SMART_EVENT_JUST_SUMMONED 事件
         */
        void IsSummonedBy(WorldObject* summoner) override;

        /**
         * @brief 造成伤害回调
         * @param doneTo 受害者
         * @param damage 伤害值
         * @param damagetype 伤害类型
         *
         * 当生物对其他单位造成伤害时调用（伤害应用前）
         * 触发 SMART_EVENT_DAMAGED_TARGET 事件
         */
        void DamageDealt(Unit* doneTo, uint32& damage, DamageEffectType /*damagetype*/) override;

        /**
         * @brief 召唤物消失回调
         * @param unit 消失的召唤物
         *
         * 当召唤的生物消失时调用
         * 触发 SMART_EVENT_SUMMON_DESPAWNED 事件
         */
        void SummonedCreatureDespawn(Creature* unit) override;

        /**
         * @brief 尸体移除回调
         * @param respawnDelay 重生延迟（可修改）
         *
         * 当生物尸体被移除时调用
         * 触发 SMART_EVENT_CORPSE_REMOVED 事件
         */
        void CorpseRemoved(uint32& respawnDelay) override;

        /**
         * @brief 消失回调
         *
         * 当生物即将从世界中移除时调用（消失、网格卸载、尸体消失）
         * 触发 SMART_EVENT_ON_DESPAWN 事件
         */
        void OnDespawn() override;

        /**
         * @brief 乘客登载回调
         * @param who 乘客单位
         * @param seatId 座位ID
         * @param apply true=登载，false=离开
         *
         * 当玩家或生物进入/离开载具时调用
         * 触发 SMART_EVENT_PASSENGER_BOARDED 或 SMART_EVENT_PASSENGER_REMOVED 事件
         */
        void PassengerBoarded(Unit* who, int8 seatId, bool apply) override;

        /**
         * @brief 初始化AI回调
         *
         * 当AI首次初始化时调用
         * 初始化SmartScript，重置所有状态变量
         */
        void InitializeAI() override;

        /**
         * @brief 出现回调
         *
         * 当生物完全添加到世界时调用
         * 触发 SMART_EVENT_RESPAWN 事件并重置脚本
         */
        void JustAppeared() override;

        /**
         * @brief 被魅惑回调
         * @param isNew 是否是新魅惑
         *
         * 当生物被魅惑或魅惑解除时调用
         * 魅惑时会停止护送路径，解除时会根据情况恢复
         * 触发 SMART_EVENT_CHARMED 事件
         */
        void OnCharmed(bool isNew) override;

        /**
         * @brief 执行动作
         * @param param 动作参数
         *
         * 用于脚本间通信，触发 SMART_EVENT_ACTION_DONE 事件
         */
        void DoAction(int32 param = 0) override;

        /**
         * @brief 获取数据
         * @param id 数据ID
         * @return 数据值（当前实现总是返回0）
         *
         * 用于脚本间数据共享
         */
        uint32 GetData(uint32 id = 0) const override;

        /**
         * @brief 设置数据
         * @param id 数据ID
         * @param value 数据值
         *
         * 用于脚本间数据共享，触发 SMART_EVENT_DATA_SET 事件
         */
        void SetData(uint32 id, uint32 value) override { SetData(id, value, nullptr); }

        /**
         * @brief 设置数据（带触发者）
         * @param id 数据ID
         * @param value 数据值
         * @param invoker 触发者
         *
         * 用于脚本间数据共享，触发 SMART_EVENT_DATA_SET 事件
         */
        void SetData(uint32 id, uint32 value, Unit* invoker);

        /**
         * @brief 设置GUID
         * @param guid GUID值
         * @param id GUID的标识ID
         *
         * 用于脚本间共享GUID数据（当前实现为空）
         */
        void SetGUID(ObjectGuid const& guid, int32 id = 0) override;

        /**
         * @brief 获取GUID
         * @param id GUID的标识ID
         * @return GUID值（当前实现总是返回空GUID）
         *
         * 用于脚本间共享GUID数据
         */
        ObjectGuid GetGUID(int32 id = 0) const override;

        /**
         * @brief 设置跑步/行走模式
         * @param run true=跑步，false=行走
         *
         * 设置生物的移动模式，并更新所有路径点的移动类型
         */
        void SetRun(bool run = true);

        /**
         * @brief 设置禁用重力
         * @param disable true=禁用重力（飞行），false=启用重力
         *
         * 控制生物是否受重力影响
         */
        void SetDisableGravity(bool disable = true);

        /**
         * @brief 设置禁用逃避
         * @param disable true=禁用逃避，false=允许逃避
         *
         * 控制生物在脱战时是否逃避回出生点
         */
        void SetEvadeDisabled(bool disable = true);

        /**
         * @brief 设置无敌HP等级
         * @param level 无敌HP等级（生物HP不低于此值）
         *
         * 设置生物的最低HP阈值，伤害不会使HP低于此值
         */
        void SetInvincibilityHpLevel(uint32 level)
        {
            _invincibilityHPLevel = level;
        }

        /**
         * @brief 对话问候回调
         * @param player 打开对话的玩家
         * @return 如果处理了对话返回true，否则返回false
         *
         * 当玩家打开与生物的对话时调用
         * 触发 SMART_EVENT_GOSSIP_HELLO 事件
         */
        bool OnGossipHello(Player* player) override;

        /**
         * @brief 对话选项选择回调
         * @param player 选择对话的玩家
         * @param menuId 菜单ID
         * @param gossipListId 对话列表ID
         * @return 如果处理了选择返回true，否则返回false
         *
         * 当玩家选择对话选项时调用
         * 触发 SMART_EVENT_GOSSIP_SELECT 事件
         */
        bool OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId) override;

        /**
         * @brief 对话代码输入回调
         * @param player 输入代码的玩家
         * @param menuId 菜单ID
         * @param gossipListId 对话列表ID
         * @param code 输入的代码字符串
         * @return 当前实现总是返回false
         *
         * 当玩家在对话中输入代码时调用（当前未实现）
         */
        bool OnGossipSelectCode(Player* player, uint32 menuId, uint32 gossipListId, char const* code) override;

        /**
         * @brief 任务接受回调
         * @param player 接受任务的玩家
         * @param quest 任务对象
         *
         * 当玩家接受任务时调用
         * 触发 SMART_EVENT_ACCEPTED_QUEST 事件
         */
        void OnQuestAccept(Player* player, Quest const* quest) override;

        /**
         * @brief 任务奖励回调
         * @param player 完成任务的玩家
         * @param quest 任务对象
         * @param opt 选项索引
         *
         * 当玩家获得任务奖励时调用
         * 触发 SMART_EVENT_REWARD_QUEST 事件
         */
        void OnQuestReward(Player* player, Quest const* quest, uint32 opt) override;

        /**
         * @brief 游戏事件回调
         * @param start true=事件开始，false=事件结束
         * @param eventId 游戏事件ID
         *
         * 当游戏事件开始或结束时调用
         * 触发 SMART_EVENT_GAME_EVENT_START 或 SMART_EVENT_GAME_EVENT_END 事件
         */
        void OnGameEvent(bool start, uint16 eventId) override;

        /**
         * @brief 设置消失时间
         * @param t 消失时间（毫秒），0表示取消消失
         *
         * 设置生物在指定时间后消失
         * 如果t>0，将消失状态设为1（等待）；如果t=0，将消失状态设为0（不消失）
         */
        void SetDespawnTime (uint32 t)
        {
            _despawnTime = t;
            _despawnState = t ? 1 : 0;
        }

        /**
         * @brief 开始消失过程
         *
         * 将消失状态设为2，开始消失计时
         */
        void StartDespawn()
        {
            _despawnState = 2;
        }

        /**
         * @brief 法术点击回调
         * @param clicker 点击者
         * @param spellClickHandled 法术点击是否已处理
         *
         * 当玩家点击生物触发法术时调用
         * 触发 SMART_EVENT_ON_SPELLCLICK 事件
         */
        void OnSpellClick(Unit* clicker, bool spellClickHandled) override;

        /**
         * @brief 设置路径点暂停计时器
         * @param time 暂停时间（毫秒）
         *
         * 设置路径点之间的暂停时间
         */
        void SetWPPauseTimer(uint32 time)
        {
            _waypointPauseTimer = time;
        }

        /**
         * @brief 设置对话返回值
         * @param val 返回值（true表示已处理对话）
         *
         * 控制对话系统是否应该继续处理对话菜单
         */
        void SetGossipReturn(bool val)
        {
            _gossipReturn = val;
        }

        /**
         * @brief 设置护送任务ID
         * @param questID 任务ID
         *
         * 设置当前护送任务的任务ID，用于任务完成/失败判定
         */
        void SetEscortQuest(uint32 questID)
        {
            _escortQuestId = questID;
        }

    private:
        /**
         * @brief 协助玩家战斗
         * @param who 潜在的敌对目标
         * @return 如果协助成功返回true，否则返回false
         *
         * 在护送过程中检查是否应该协助玩家攻击敌对目标
         * 只有满足条件的敌对目标才会被攻击：
         * - 生物不是被动状态
         * - 敌对目标正在攻击玩家
         * - 生物类型标记允许协助
         * - 距离和视线符合要求
         */
        bool AssistPlayerInCombatAgainst(Unit* who);

        /**
         * @brief 返回最后脱战位置
         *
         * 在战斗结束后，生物移动到最后一次脱战的位置
         * 只有AI受控时才会执行
         */
        void ReturnToLastOOCPos();

        /**
         * @brief 检查载具条件
         * @param diff 时间差（毫秒）
         *
         * 定期检查载具乘客是否满足条件
         * 如果乘客不满足条件，会被踢出载具
         */
        void CheckConditions(uint32 diff);

        /**
         * @brief 更新路径状态
         * @param diff 时间差（毫秒）
         *
         * 处理护送路径的更新逻辑：
         * - 检查护送触发者是否在范围内
         * - 处理路径点暂停计时器
         * - 处理战斗后返回脱战位置
         * - 处理路径结束
         */
        void UpdatePath(uint32 diff);

        /**
         * @brief 更新跟随状态
         * @param diff 时间差（毫秒）
         *
         * 处理跟随逻辑：
         * - 检查是否到达目标生物
         * - 到达后完成跟随并给予任务积分
         */
        void UpdateFollow(uint32 diff);

        /**
         * @brief 更新消失状态
         * @param diff 时间差（毫秒）
         *
         * 处理生物消失过程：
         * - 状态2：隐藏生物模型
         * - 状态3：彻底移除生物
         */
        void UpdateDespawn(uint32 diff);

        /// SmartScript脚本引擎实例，处理所有事件和动作
        SmartScript _script;

        /// 是否被魅惑（玩家控制时为true）
        bool _charmed;

        /// 跟随积分类型（0=奖励组，1=组事件）
        uint32 _followCreditType;

        /// 跟随到达检查计时器（毫秒）
        uint32 _followArrivedTimer;

        /// 跟随完成时给予的任务积分ID
        uint32 _followCredit;

        /// 跟随结束时需要到达的目标生物entry
        uint32 _followArrivedEntry;

        /// 跟随目标的GUID
        ObjectGuid _followGUID;

        /// 跟随距离
        float _followDistance;

        /// 跟随角度
        float _followAngle;

        /// 护送状态（SmartEscortState枚举的组合）
        uint32 _escortState;

        /// 护送开始前的NPC标志，护送期间临时保存
        uint32 _escortNPCFlags;

        /// 护送触发者距离检查计时器（毫秒）
        uint32 _escortInvokerCheckTimer;

        /// 当前路径的点数据
        WaypointPath _path;

        /// 当前路径点ID
        uint32 _currentWaypointNode;

        /// 是否已到达当前路径点
        bool _waypointReached;

        /// 路径点暂停计时器（毫秒）
        uint32 _waypointPauseTimer;

        /// 是否强制暂停路径（立即停止移动）
        bool _waypointPauseForced;

        /// 是否循环路径
        bool _repeatWaypointPath;

        /// 是否已到达脱战位置
        bool _OOCReached;

        /// 路径是否已结束
        bool _waypointPathEnded;

        /// 移动模式（true=跑步，false=行走）
        bool _run;

        /// 是否禁用逃避
        bool _evadeDisabled;

        /// 是否允许自动攻击
        bool _canAutoAttack;

        /// 是否允许战斗中移动
        bool _canCombatMove;

        /// 无敌HP等级（HP不会低于此值）
        uint32 _invincibilityHPLevel;

        /// 消失计时器（毫秒）
        uint32 _despawnTime;

        /// 消失状态（0=不消失，1=等待消失，2=隐藏中，3=移除）
        uint32 _despawnState;

        /// 是否有载具条件配置
        bool _vehicleConditions;

        /// 载具条件检查计时器（毫秒）
        uint32 _vehicleConditionsTimer;

        /// 对话返回值（控制对话是否被处理）
        bool _gossipReturn;

        /// 护送任务ID
        uint32 _escortQuestId;
};

/**
 * @class SmartGameObjectAI
 * @brief 游戏对象智能AI类
 *
 * SmartGameObjectAI 为游戏对象提供智能AI功能，与SmartAI类似，
 * 但专门针对游戏对象（如宝箱、门、机关等）设计。
 *
 * 功能特性：
 * - 事件处理：响应游戏对象相关事件（对话、法术命中、被使用等）
 * - 动作执行：执行游戏对象相关动作（召唤生物、触发事件等）
 * - 任务支持：支持从游戏对象接受和完成任务
 *
 * 使用场景：
 * - 可交互的游戏对象
 * - 任务物品
 * - 触发器
 * - 机关装置
 */
class TC_GAME_API SmartGameObjectAI : public GameObjectAI
{
    public:
        /**
         * @brief 构造函数
         * @param go 关联的游戏对象指针
         *
         * 初始化SmartGameObjectAI实例，设置对话返回值为false
         */
        SmartGameObjectAI(GameObject* go) : GameObjectAI(go), _gossipReturn(false) { }

        /**
         * @brief 析构函数
         */
        ~SmartGameObjectAI() { }

        /**
         * @brief 更新AI
         * @param diff 距上次更新的时间差（毫秒）
         *
         * 每个世界更新周期调用一次，更新SmartScript
         */
        void UpdateAI(uint32 diff) override;

        /**
         * @brief 初始化AI
         *
         * 初始化SmartScript，如果游戏对象已生成，触发重生事件
         */
        void InitializeAI() override;

        /**
         * @brief 重置AI
         *
         * 重置SmartScript状态
         */
        void Reset() override;

        /**
         * @brief 获取SmartScript对象
         * @return SmartScript对象的指针
         */
        SmartScript* GetScript()
        {
            return &_script;
        }

        /**
         * @brief 检查AI是否允许该游戏对象类型
         * @param go 要检查的游戏对象
         * @return 返回 PERMIT_BASE_NO，表示需要通过其他方式启用
         */
        static int32 Permissible(GameObject const* /*go*/)
        {
            return PERMIT_BASE_NO;
        }

        /**
         * @brief 对话问候回调
         * @param player 打开对话的玩家
         * @return 如果处理了对话返回true，否则返回false
         *
         * 当玩家打开与游戏对象的对话时调用
         * 触发 SMART_EVENT_GOSSIP_HELLO 事件
         */
        bool OnGossipHello(Player* player) override;

        /**
         * @brief 对话选项选择回调
         * @param player 选择对话的玩家
         * @param menuId 菜单ID（sender）
         * @param gossipListId 对话列表ID（action）
         * @return 如果处理了选择返回true，否则返回false
         *
         * 当玩家选择对话选项时调用
         * 触发 SMART_EVENT_GOSSIP_SELECT 事件
         */
        bool OnGossipSelect(Player* player, uint32 menuId, uint32 gossipListId) override;

        /**
         * @brief 对话代码输入回调
         * @param player 输入代码的玩家
         * @param menuId 菜单ID
         * @param gossipListId 对话列表ID
         * @param code 输入的代码字符串
         * @return 当前实现总是返回false
         */
        bool OnGossipSelectCode(Player* player, uint32 menuId, uint32 gossipListId, char const* code) override;

        /**
         * @brief 任务接受回调
         * @param player 接受任务的玩家
         * @param quest 任务对象
         *
         * 当玩家从游戏对象接受任务时调用
         * 触发 SMART_EVENT_ACCEPTED_QUEST 事件
         */
        void OnQuestAccept(Player* player, Quest const* quest) override;

        /**
         * @brief 任务奖励回调
         * @param player 完成任务的玩家
         * @param quest 任务对象
         * @param opt 选项索引
         *
         * 当玩家从游戏对象获得任务奖励时调用
         * 触发 SMART_EVENT_REWARD_QUEST 事件
         */
        void OnQuestReward(Player* player, Quest const* quest, uint32 opt) override;

        /**
         * @brief 报告使用回调
         * @param player 使用游戏对象的玩家
         * @return 如果处理了使用返回true，否则返回false
         *
         * 当玩家报告使用游戏对象时调用
         * 触发 SMART_EVENT_GOSSIP_HELLO 事件（带特殊参数）
         */
        bool OnReportUse(Player* player) override;

        /**
         * @brief 被摧毁回调
         * @param attacker 攻击者（可能为nullptr）
         * @param eventId 事件ID
         *
         * 当可破坏的游戏对象被摧毁时调用
         * 触发 SMART_EVENT_DEATH 事件
         */
        void Destroyed(WorldObject* attacker, uint32 eventId) override;

        /**
         * @brief 设置数据（带触发者）
         * @param id 数据ID
         * @param value 数据值
         * @param invoker 触发者
         *
         * 触发 SMART_EVENT_DATA_SET 事件
         */
        void SetData(uint32 id, uint32 value, Unit* invoker);

        /**
         * @brief 设置数据
         * @param id 数据ID
         * @param value 数据值
         *
         * 触发 SMART_EVENT_DATA_SET 事件
         */
        void SetData(uint32 id, uint32 value) override { SetData(id, value, nullptr); }

        /**
         * @brief 设置定时动作列表
         * @param e SmartScript持有对象引用
         * @param entry 动作列表ID
         * @param invoker 触发者
         */
        void SetTimedActionList(SmartScriptHolder& e, uint32 entry, Unit* invoker);

        /**
         * @brief 游戏事件回调
         * @param start true=事件开始，false=事件结束
         * @param eventId 游戏事件ID
         *
         * 触发相应的事件
         */
        void OnGameEvent(bool start, uint16 eventId) override;

        /**
         * @brief 拾取状态改变回调
         * @param state 新的拾取状态
         * @param unit 相关单位
         *
         * 当游戏对象的拾取状态改变时调用
         * 触发 SMART_EVENT_GO_LOOT_STATE_CHANGED 事件
         */
        void OnLootStateChanged(uint32 state, Unit* unit) override;

        /**
         * @brief 事件通知回调
         * @param eventId 事件ID
         *
         * 当游戏对象收到事件通知时调用
         * 触发 SMART_EVENT_GO_EVENT_INFORM 事件
         */
        void EventInform(uint32 eventId) override;

        /**
         * @brief 被法术命中回调
         * @param caster 施法者
         * @param spellInfo 法术信息
         *
         * 触发 SMART_EVENT_SPELLHIT 事件
         */
        void SpellHit(WorldObject* caster, SpellInfo const* spellInfo) override;

        /**
         * @brief 召唤生物回调
         * @param creature 被召唤的生物
         *
         * 当游戏对象召唤其他生物时调用
         * 触发 SMART_EVENT_SUMMONED_UNIT 事件
         */
        void JustSummoned(Creature* creature) override;

        /**
         * @brief 召唤物死亡回调
         * @param summon 死亡的召唤物
         * @param killer 击杀者
         *
         * 触发 SMART_EVENT_SUMMONED_UNIT_DIES 事件
         */
        void SummonedCreatureDies(Creature* summon, Unit* killer) override;

        /**
         * @brief 召唤物消失回调
         * @param unit 消失的召唤物
         *
         * 触发 SMART_EVENT_SUMMON_DESPAWNED 事件
         */
        void SummonedCreatureDespawn(Creature* unit) override;

        /**
         * @brief 设置对话返回值
         * @param val 返回值
         */
        void SetGossipReturn(bool val) { _gossipReturn = val; }

    private:
        /// SmartScript脚本引擎实例
        SmartScript _script;

        /// 对话返回值（控制对话是否被处理）
        bool _gossipReturn;
};

/// Registers scripts required by the SAI scripting system
void AddSC_SmartScripts();

#endif
