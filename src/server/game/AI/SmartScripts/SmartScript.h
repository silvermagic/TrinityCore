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
 * @file SmartScript.h
 * @brief SmartAI脚本系统核心类定义
 *
 * 本模块实现了TrinityCore的SmartAI系统，这是一个基于事件驱动的AI脚本框架。
 * SmartAI允许通过数据库配置来创建复杂的AI行为，而无需编写C++代码。
 *
 * 主要功能：
 * - 事件驱动架构：支持多种事件类型（战斗、时间、死亡、进入视野等）
 * - 动作执行系统：支持各种动作（施法、移动、说话、召唤等）
 * - 目标选择系统：灵活的目标选择机制
 * - 相位系统：支持多相位AI行为
 * - 计数器系统：用于实现复杂的条件逻辑
 * - 存储目标系统：保存和复用目标引用
 *
 * 使用场景：
 * - NPC行为定制
 * - 副本BOSS战机制
 * - 任务NPC交互
 * - 区域触发器逻辑
 * - 游戏对象交互
 */

#ifndef TRINITY_SMARTSCRIPT_H
#define TRINITY_SMARTSCRIPT_H

#include "Define.h"
#include "SmartScriptMgr.h"

// 前向声明
class Creature;        // 生物类
class GameObject;      // 游戏对象类
class Player;          // 玩家类
class SpellInfo;       // 法术信息类
class Unit;            // 单位基类（包含生物和玩家）
class WorldObject;     // 世界对象基类
struct AreaTriggerEntry; // 区域触发器数据结构

/**
 * @class SmartScript
 * @brief SmartAI脚本系统核心类
 *
 * SmartScript是SmartAI系统的核心执行引擎，负责管理和执行智能AI事件和动作。
 * 每个使用SmartAI的生物、游戏对象或区域触发器都拥有一个SmartScript实例。
 *
 * 职责：
 * 1. 加载和管理SmartAI事件配置
 * 2. 处理各种游戏事件（战斗、移动、时间等）
 * 3. 执行事件关联的动作
 * 4. 管理事件相位系统
 * 5. 维护计数器和存储目标
 *
 * 生命周期：
 * - 构造时初始化所有成员
 * - 通过OnInitialize()绑定到具体的游戏对象
 * - 随游戏对象生命周期更新和执行
 * - 析构时清理资源
 *
 * 线程安全性：
 * - 每个SmartScript实例绑定到特定的游戏对象
 * - 在地图线程中执行，无需额外同步
 */
class TC_GAME_API SmartScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化SmartScript实例的所有成员变量为默认状态
         */
        SmartScript();

        /**
         * @brief 析构函数
         *
         * 清理SmartScript实例占用的资源
         */
        ~SmartScript();

        /**
         * @brief 初始化SmartScript实例
         *
         * 将SmartScript绑定到具体的游戏对象（生物、游戏对象或区域触发器）
         *
         * @param obj 要绑定的世界对象（生物、游戏对象或玩家触发器）
         * @param at 区域触发器数据，如果obj是区域触发器则传入，默认为nullptr
         *
         * 调用时机：
         * - 生物：在SmartAI::JustAppeared()中调用
         * - 游戏对象：在SmartGameObjectAI::InitializeAI()中调用
         * - 区域触发器：在SmartTrigger::OnInitialize()中调用
         *
         * 注意：
         * - 必须在使用任何其他功能之前调用
         * - 会调用GetScript()加载事件配置
         */
        void OnInitialize(WorldObject* obj, AreaTriggerEntry const* at = nullptr);

        /**
         * @brief 从数据库加载SmartAI事件配置
         *
         * 根据对象的entry或GUID从数据库加载对应的SmartAI事件列表
         *
         * 调用时机：
         * - 在OnInitialize()内部自动调用
         * - 在SetTimedActionList()中加载定时动作列表时调用
         */
        void GetScript();

        /**
         * @brief 填充SmartAI事件列表
         *
         * 将事件列表填充到脚本中并进行排序和初始化
         *
         * @param e 要填充的事件列表
         * @param obj 关联的世界对象
         * @param at 区域触发器数据（可选）
         *
         * 调用时机：
         * - 在GetScript()内部调用
         * - 在需要动态添加事件时调用
         */
        void FillScript(SmartAIEventList e, WorldObject* obj, AreaTriggerEntry const* at);

        /**
         * @brief 处理指定类型的所有事件
         *
         * 遍历所有事件，检查并执行匹配指定类型的所有事件
         *
         * @param e 事件类型（如SMART_EVENT_UPDATE、SMART_EVENT_DEATH等）
         * @param unit 相关的单位（如击杀者、目标等），默认nullptr
         * @param var0 事件参数0，具体含义取决于事件类型
         * @param var1 事件参数1，具体含义取决于事件类型
         * @param bvar 布尔参数，用于某些事件类型
         * @param spell 相关的法术信息，默认nullptr
         * @param gob 相关的游戏对象，默认nullptr
         *
         * 调用时机：
         * - 在各种游戏事件发生时调用（如OnUpdate、OnDeath、OnSpellHit等）
         *
         * 性能注意：
         * - 避免在循环中递归调用导致无限循环
         * - 使用mNestedEventsCounter防止嵌套过深（最大MAX_NESTED_EVENTS层）
         */
        void ProcessEventsFor(SMART_EVENT e, Unit* unit = nullptr, uint32 var0 = 0, uint32 var1 = 0, bool bvar = false, SpellInfo const* spell = nullptr, GameObject* gob = nullptr);

        /**
         * @brief 处理单个SmartAI事件
         *
         * 检查单个事件的触发条件，如果满足则执行关联的动作
         *
         * @param e 要处理的事件
         * @param unit 相关的单位
         * @param var0 事件参数0
         * @param var1 事件参数1
         * @param bvar 布尔参数
         * @param spell 相关的法术信息
         * @param gob 相关的游戏对象
         *
         * 处理流程：
         * 1. 检查事件是否满足触发条件（相位、冷却、概率等）
         * 2. 如果满足，调用ProcessAction()执行动作
         * 3. 重置事件定时器
         */
        void ProcessEvent(SmartScriptHolder& e, Unit* unit = nullptr, uint32 var0 = 0, uint32 var1 = 0, bool bvar = false, SpellInfo const* spell = nullptr, GameObject* gob = nullptr);

        /**
         * @brief 检查事件定时器是否已到期
         *
         * @param e 要检查的事件
         * @return true 如果定时器已到期或无定时器要求
         * @return false 如果定时器尚未到期
         */
        bool CheckTimer(SmartScriptHolder const& e) const;

        /**
         * @brief 重新计算事件定时器
         *
         * 根据最小和最大延迟时间重新设置事件的定时器
         *
         * @param e 要重新计算定时器的事件
         * @param min 最小延迟（毫秒）
         * @param max 最大延迟（毫秒）
         *
         * 说明：定时器会在[min, max]范围内随机取值
         */
        static void RecalcTimer(SmartScriptHolder& e, uint32 min, uint32 max);

        /**
         * @brief 更新事件定时器
         *
         * 减少事件的定时器时间
         *
         * @param e 要更新的事件
         * @param diff 经过的毫秒数
         */
        void UpdateTimer(SmartScriptHolder& e, uint32 const diff);

        /**
         * @brief 初始化事件定时器
         *
         * 根据事件参数设置初始定时器值
         *
         * @param e 要初始化的事件
         */
        static void InitTimer(SmartScriptHolder& e);
        /**
         * @brief 处理事件关联的动作
         *
         * 执行SmartAI事件关联的所有动作
         *
         * @param e 包含动作信息的事件
         * @param unit 相关的单位
         * @param var0 动作参数0
         * @param var1 动作参数1
         * @param bvar 布尔参数
         * @param spell 相关的法术信息
         * @param gob 相关的游戏对象
         *
         * 处理动作包括：
         * - 施放法术、说话、移动、召唤、修改相位等
         * - 支持链式动作（一个动作可以触发另一个事件）
         */
        void ProcessAction(SmartScriptHolder& e, Unit* unit = nullptr, uint32 var0 = 0, uint32 var1 = 0, bool bvar = false, SpellInfo const* spell = nullptr, GameObject* gob = nullptr);

        /**
         * @brief 处理定时动作
         *
         * 设置定时器并在到期后执行动作
         *
         * @param e 包含动作信息的事件
         * @param min 最小延迟（毫秒）
         * @param max 最大延迟（毫秒）
         * @param unit 相关的单位
         * @param var0 动作参数0
         * @param var1 动作参数1
         * @param bvar 布尔参数
         * @param spell 相关的法术信息
         * @param gob 相关的游戏对象
         */
        void ProcessTimedAction(SmartScriptHolder& e, uint32 const& min, uint32 const& max, Unit* unit = nullptr, uint32 var0 = 0, uint32 var1 = 0, bool bvar = false, SpellInfo const* spell = nullptr, GameObject* gob = nullptr);

        /**
         * @brief 获取动作的目标列表
         *
         * 根据事件的目标类型获取目标对象列表
         *
         * @param targets [out] 输出的目标对象列表
         * @param e 包含目标信息的事件
         * @param invoker 触发者对象，默认nullptr
         *
         * 目标类型包括：
         * - 自身、攻击目标、最近敌人、随机玩家等
         * - 存储目标、计数器相关的条件目标
         * - 位置、距离等几何条件目标
         */
        void GetTargets(ObjectVector& targets, SmartScriptHolder const& e, WorldObject* invoker = nullptr) const;

        /**
         * @brief 获取指定距离内的所有世界对象
         *
         * @param objects [out] 输出的对象列表
         * @param dist 搜索距离
         */
        void GetWorldObjectsInDist(ObjectVector& objects, float dist) const;

        /**
         * @brief 创建SmartAI事件（用于调试和测试）
         *
         * @param e 事件类型
         * @param event_flags 事件标志
         * @param event_param1-5 事件参数1-5
         * @param action 动作类型
         * @param action_param1-6 动作参数1-6
         * @param t 目标类型
         * @param target_param1-4 目标参数1-4
         * @param phaseMask 相位掩码
         * @return 创建的事件结构
         */
        static SmartScriptHolder CreateSmartEvent(SMART_EVENT e, uint32 event_flags, uint32 event_param1, uint32 event_param2, uint32 event_param3, uint32 event_param4, uint32 event_param5, SMART_ACTION action, uint32 action_param1, uint32 action_param2, uint32 action_param3, uint32 action_param4, uint32 action_param5, uint32 action_param6, SMARTAI_TARGETS t, uint32 target_param1, uint32 target_param2, uint32 target_param3, uint32 target_param4, uint32 phaseMask);

        /**
         * @brief 设置路径ID
         * @param id 路径ID（用于移动动作）
         */
        void SetPathId(uint32 id) { mPathId = id; }

        /**
         * @brief 获取路径ID
         * @return 当前路径ID
         */
        uint32 GetPathId() const { return mPathId; }

        /**
         * @brief 获取基础对象
         * @return 绑定的世界对象（生物或游戏对象）
         */
        WorldObject* GetBaseObject() const;

        /**
         * @brief 获取基础对象或玩家触发器
         * @return 绑定的世界对象，对于区域触发器返回触发的玩家
         */
        WorldObject* GetBaseObjectOrPlayerTrigger() const;

        /**
         * @brief 检查是否存在指定标志的事件
         * @param flag 要检查的标志
         * @return true 如果存在包含该标志的事件
         */
        bool HasAnyEventWithFlag(uint32 flag) const { return mAllEventFlags & flag; }

        // 类型检查工具函数
        static bool IsUnit(WorldObject* obj);           ///< 检查对象是否为单位
        static bool IsPlayer(WorldObject* obj);         ///< 检查对象是否为玩家
        static bool IsCreature(WorldObject* obj);       ///< 检查对象是否为生物
        static bool IsCharmedCreature(WorldObject* obj);///< 检查对象是否为被魅惑的生物
        static bool IsGameObject(WorldObject* obj);     ///< 检查对象是否为游戏对象

        /**
         * @brief 更新函数，每帧调用
         *
         * 处理定时器更新和定时事件
         *
         * @param diff 自上次更新以来经过的毫秒数
         *
         * 调用时机：
         * - 在SmartAI::UpdateAI()中每帧调用
         * - 在SmartGameObjectAI::UpdateAI()中每帧调用
         */
        void OnUpdate(const uint32 diff);

        /**
         * @brief 视线范围内移动事件
         *
         * 当单位进入视线范围时触发
         *
         * @param who 进入视野的单位
         *
         * 调用时机：
         * - 在SmartAI::MoveInLineOfSight()中调用
         */
        void OnMoveInLineOfSight(Unit* who);

        // ==================== 友方单位查找函数 ====================

        /**
         * @brief 选择范围内生命值最低的友方单位
         *
         * @param range 搜索范围（码）
         * @param MinHPDiff 最小生命值差值（当前生命值与最大生命值的差）
         * @return 生命值最低且满足条件的友方单位，未找到则返回nullptr
         */
        Unit* DoSelectLowestHpFriendly(float range, uint32 MinHPDiff) const;

        /**
         * @brief 选择范围内生命值百分比最低的友方单位
         *
         * @param range 搜索范围（码）
         * @param minHpPct 最小生命值百分比
         * @param maxHpPct 最大生命值百分比
         * @return 生命值百分比在[minHpPct, maxHpPct]范围内最低的友方单位
         */
        Unit* DoSelectLowestHpPercentFriendly(float range, uint32 minHpPct, uint32 maxHpPct) const;

        /**
         * @brief 查找范围内受到控制效果的友方单位
         *
         * @param creatures [out] 输出的友方生物列表
         * @param range 搜索范围（码）
         */
        void DoFindFriendlyCC(std::vector<Creature*>& creatures, float range) const;

        /**
         * @brief 查找范围内缺少指定Buff的友方单位
         *
         * @param creatures [out] 输出的友方生物列表
         * @param range 搜索范围（码）
         * @param spellid Buff法术ID
         */
        void DoFindFriendlyMissingBuff(std::vector<Creature*>& creatures, float range, uint32 spellid) const;

        /**
         * @brief 查找最近的友方单位
         *
         * @param range 搜索范围（码）
         * @param playerOnly 是否只搜索玩家
         * @return 最近的友方单位
         */
        Unit* DoFindClosestFriendlyInRange(float range, bool playerOnly) const;

        // ==================== SmartAI类型检查 ====================

        /**
         * @brief 检查生物是否使用SmartAI
         *
         * @param c 要检查的生物
         * @param silent 是否静默模式（不输出错误日志）
         * @return true 如果生物使用SmartAI
         */
        bool IsSmart(Creature* c, bool silent = false) const;

        /**
         * @brief 检查游戏对象是否使用SmartAI
         *
         * @param g 要检查的游戏对象
         * @param silent 是否静默模式
         * @return true 如果游戏对象使用SmartAI
         */
        bool IsSmart(GameObject* g, bool silent = false) const;

        /**
         * @brief 检查当前对象是否使用SmartAI
         *
         * @param silent 是否静默模式
         * @return true 如果当前对象使用SmartAI
         */
        bool IsSmart(bool silent = false) const;

        // ==================== 存储目标系统 ====================

        /**
         * @brief 存储目标列表
         *
         * 将目标列表保存到存储系统中，供后续事件使用
         *
         * @param targets 要存储的目标列表
         * @param id 存储ID（用于后续检索）
         */
        void StoreTargetList(ObjectVector const& targets, uint32 id);

        /**
         * @brief 添加目标到已存储的目标列表
         *
         * @param targets 要添加的目标列表
         * @param id 存储ID
         */
        void AddToStoredTargetList(ObjectVector const& targets, uint32 id);

        /**
         * @brief 获取已存储的目标列表
         *
         * @param id 存储ID
         * @param ref 参考对象（用于验证目标有效性）
         * @return 存储的目标列表指针，不存在则返回nullptr
         */
        ObjectVector const* GetStoredTargetVector(uint32 id, WorldObject const& ref) const;

        // ==================== 计数器系统 ====================

        /**
         * @brief 存储或更新计数器
         *
         * @param id 计数器ID
         * @param value 要增加的值
         * @param reset 重置模式（0=累加，1=设置，2=重置为0）
         */
        void StoreCounter(uint32 id, uint32 value, uint32 reset);

        /**
         * @brief 获取计数器值
         *
         * @param id 计数器ID
         * @return 计数器当前值，不存在返回0
         */
        uint32 GetCounterValue(uint32 id) const;

        // ==================== 对象查找工具 ====================

        /**
         * @brief 在附近查找游戏对象
         *
         * @param searchObject 搜索中心对象
         * @param guid 游戏对象的GUID
         * @return 找到的游戏对象，未找到返回nullptr
         */
        GameObject* FindGameObjectNear(WorldObject* searchObject, ObjectGuid::LowType guid) const;

        /**
         * @brief 在附近查找生物
         *
         * @param searchObject 搜索中心对象
         * @param guid 生物的GUID
         * @return 找到的生物，未找到返回nullptr
         */
        Creature* FindCreatureNear(WorldObject* searchObject, ObjectGuid::LowType guid) const;

        // ==================== 重置函数 ====================

        /**
         * @brief 重置脚本状态
         *
         * 重置所有事件定时器、相位、计数器等状态
         *
         * 调用时机：
         * - 在SmartAI::JustReachedHome()中调用（生物归位）
         * - 在副本重置时
         */
        void OnReset();

        /**
         * @brief 重置基础对象
         *
         * 清除当前绑定的对象引用，重新获取
         */
        void ResetBaseObject();

        // ==================== 定时动作列表 ====================

        /**
         * @brief 设置定时动作列表
         *
         * 加载并执行一个定时动作列表（Timed Actionlist）
         *
         * @param e 触发事件
         * @param entry 动作列表entry
         * @param invoker 触发者
         */
        void SetTimedActionList(SmartScriptHolder& e, uint32 entry, Unit* invoker);

        /**
         * @brief 获取最后的触发者
         *
         * @param invoker 当前触发者（可选）
         * @return 最后的触发者单位
         */
        Unit* GetLastInvoker(Unit* invoker = nullptr) const;

        // ==================== 公共成员变量 ====================

        ObjectGuid mLastInvoker;  ///< 最后的触发者GUID

        typedef std::unordered_map<uint32, uint32> CounterMap; ///< 计数器映射类型
        CounterMap mCounterList;  ///< 计数器列表（ID -> 值）

    private:
        // ==================== 相位管理 ====================

        /**
         * @brief 增加事件相位
         *
         * @param p 要增加的相位值
         */
        void IncPhase(uint32 p);

        /**
         * @brief 减少事件相位
         *
         * @param p 要减少的相位值
         */
        void DecPhase(uint32 p);

        /**
         * @brief 设置事件相位
         *
         * @param p 要设置的相位值
         */
        void SetPhase(uint32 p);

        /**
         * @brief 检查是否在指定相位中
         *
         * @param p 要检查的相位
         * @return true 如果当前相位匹配
         */
        bool IsInPhase(uint32 p) const;

        // ==================== 事件管理 ====================

        /**
         * @brief 对事件列表进行排序
         *
         * 按优先级和事件ID对事件进行排序
         *
         * @param events 要排序的事件列表
         */
        void SortEvents(SmartAIEventList& events);

        /**
         * @brief 提升事件优先级
         *
         * 提高事件的执行优先级，使其更快执行
         *
         * @param e 要提升优先级的事件
         */
        void RaisePriority(SmartScriptHolder& e);

        /**
         * @brief 稍后重试事件
         *
         * 将事件标记为稍后重试
         *
         * @param e 要重试的事件
         * @param ignoreChanceRoll 是否忽略概率检定
         */
        void RetryLater(SmartScriptHolder& e, bool ignoreChanceRoll = false);

        // ==================== 成员变量 ====================

        SmartAIEventList mEvents;              ///< 主事件列表（当前活跃的事件）
        SmartAIEventList mInstallEvents;       ///< 待安装的事件列表（用于延迟安装）
        SmartAIEventList mTimedActionList;     ///< 定时动作列表事件
        ObjectGuid mTimedActionListInvoker;    ///< 定时动作列表的触发者
        bool isProcessingTimedActionList;      ///< 是否正在处理定时动作列表

        // ==================== 对象引用 ====================

        Creature* me;           ///< 绑定的生物对象（如果类型是生物）
        ObjectGuid meOrigGUID;  ///< 生物的原始GUID（用于检测重置）
        GameObject* go;         ///< 绑定的游戏对象（如果类型是游戏对象）
        ObjectGuid goOrigGUID;  ///< 游戏对象的原始GUID
        Player* atPlayer;       ///< 区域触发器的触发玩家
        AreaTriggerEntry const* trigger; ///< 区域触发器数据

        // ==================== 脚本状态 ====================

        SmartScriptType mScriptType;  ///< 脚本类型（生物/游戏对象/区域触发器）
        uint32 mEventPhase;           ///< 当前事件相位

        uint32 mPathId;               ///< 当前路径ID（用于移动动作）
        SmartAIEventStoredList mStoredEvents; ///< 已存储的事件列表（用于动态事件）
        std::vector<uint32> mRemIDs;  ///< 要移除的事件ID列表

        // ==================== 文本系统 ====================

        uint32 mTextTimer;     ///< 文本显示定时器
        uint32 mLastTextID;    ///< 最后显示的文本ID
        uint32 mTalkerEntry;   ///< 说话者entry
        bool mUseTextTimer;    ///< 是否使用文本定时器

        // ==================== 事件处理控制 ====================

        uint32 mCurrentPriority;     ///< 当前处理优先级
        bool mEventSortingRequired;  ///< 是否需要重新排序事件
        uint32 mNestedEventsCounter; ///< 嵌套事件计数器（防止无限循环）
        uint32 mAllEventFlags;       ///< 所有事件的标志位或运算结果

        /// 最大嵌套事件深度，用于防止无限循环
        static constexpr uint32 MAX_NESTED_EVENTS = 10;

        // ==================== 存储目标系统 ====================

        ObjectVectorMap _storedTargets; ///< 存储的目标映射（ID -> 目标列表）

        // ==================== 私有辅助函数 ====================

        /**
         * @brief 安装事件
         *
         * 将待安装事件列表中的事件安装到主事件列表
         */
        void InstallEvents();

        /**
         * @brief 移除已存储的事件
         *
         * @param id 要移除的事件ID
         */
        void RemoveStoredEvent(uint32 id);
};

#endif
