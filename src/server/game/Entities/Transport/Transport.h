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
 * @file Transport.h
 * @brief 运输工具（Transport）模块头文件
 *
 * 本模块实现了游戏中的动态运输工具系统，如船只、飞艇、电梯等移动平台。
 * Transport 类继承自 GameObject 和 TransportBase，负责：
 *   - 管理运输工具的移动路径和关键帧动画
 *   - 处理乘客（玩家、生物、游戏对象）的登载和离载
 *   - 实现跨地图传送和位置同步
 *   - 触发路径节点的事件脚本
 *
 * 运输工具的生命周期由 TransportMgr 管理，在服务器启动时创建并持久存在于游戏世界中。
 * 乘客系统支持动态乘客（玩家、临时召唤物）和静态乘客（地图上预定义的NPC和物体）。
 */

#ifndef TRANSPORTS_H
#define TRANSPORTS_H

#include "GameObject.h"
#include "TransportMgr.h"
#include "VehicleDefines.h"

struct CreatureData;

/**
 * @class Transport
 * @brief 动态运输工具类，表示游戏中可移动的交通工具平台
 *
 * Transport 类是 GameObject 的特殊派生类，用于实现游戏中可移动的运输工具，
 * 如船只、飞艇、电梯、矿车等。它继承自 GameObject（游戏对象基类）和
 * TransportBase（运输工具基类，提供坐标转换功能）。
 *
 * 主要职责：
 *   1. 路径管理：按照预定义的关键帧路径移动，支持加速/减速运动
 *   2. 乘客管理：处理玩家、NPC、游戏对象的登载、位置同步和离载
 *   3. 地图传送：在跨地图路径节点之间传送运输工具及其乘客
 *   4. 事件触发：在到达/离开关键帧时触发相应的脚本事件
 *
 * 设计模式：
 *   - 使用友元类 TransportMgr 来控制对象的创建
 *   - 通过 PassengerSet 管理乘客集合，支持 O(log n) 的插入/删除
 *
 * @see TransportMgr 运输工具管理器
 * @see TransportTemplate 运输工具模板
 * @see KeyFrame 关键帧数据结构
 */
class TC_GAME_API Transport : public GameObject, public TransportBase
{
        // TransportMgr 作为友元类，用于创建 Transport 实例
        friend Transport* TransportMgr::CreateTransport(uint32, ObjectGuid::LowType, Map*);

        /**
         * @brief 私有构造函数
         *
         * 构造函数设为私有，确保只能通过 TransportMgr::CreateTransport() 创建实例，
         * 这样可以保证运输工具的正确初始化和注册。
         */
        Transport();
    public:
        /**
         * @brief 乘客集合类型定义
         *
         * 使用 std::set 存储乘客指针，保证元素的唯一性和有序性。
         * 查找、插入、删除操作的时间复杂度为 O(log n)。
         */
        typedef std::set<WorldObject*> PassengerSet;

        /**
         * @brief 析构函数
         *
         * 销毁运输工具时：
         *   - 断言确保所有动态乘客已被移除（_passengers 为空）
         *   - 卸载所有静态乘客
         */
        ~Transport();

        /**
         * @brief 创建并初始化运输工具
         *
         * @param guidlow 对象的低GUID值，用于唯一标识
         * @param entry 运输工具的模板ID（对应 gameobject_template 表）
         * @param mapid 初始所在地图ID
         * @param x 初始X坐标
         * @param y 初始Y坐标
         * @param z 初始Z坐标
         * @param ang 初始朝向角度
         * @param animprogress 动画进度值
         * @return true 创建成功
         * @return false 创建失败（无效坐标、缺少模板等）
         *
         * 调用时机：由 TransportMgr::CreateTransport() 调用
         *
         * 初始化流程：
         *   1. 设置位置坐标
         *   2. 加载 GameObject 模板和 Transport 模板
         *   3. 初始化路径关键帧迭代器
         *   4. 设置各项属性（周期、显示ID、状态等）
         *   5. 创建可视化模型
         */
        bool Create(ObjectGuid::LowType guidlow, uint32 entry, uint32 mapid, float x, float y, float z, float ang, uint32 animprogress);

        /**
         * @brief 删除前的清理工作
         *
         * @param finalCleanup 是否为最终清理（默认 true）
         *
         * 清理流程：
         *   1. 卸载所有静态乘客
         *   2. 移除所有动态乘客
         *   3. 调用父类的清理方法
         */
        void CleanupsBeforeDelete(bool finalCleanup = true) override;

        /**
         * @brief 更新运输工具状态（主更新循环）
         *
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 更新流程：
         *   1. 更新 AI（如果有）
         *   2. 推进路径进度计时器
         *   3. 检查是否到达/离开关键帧
         *   4. 触发相应事件
         *   5. 计算并更新位置（使用样条插值）
         *   6. 处理地图切换（如需要）
         *   7. 管理乘客加载/卸载（基于网格活跃状态）
         *
         * 调用时机：每帧由 Map::Update() 调用
         * 性能注意：位置更新有 200ms 延迟，避免频繁计算
         */
        void Update(uint32 diff) override;

        /**
         * @brief 延迟更新，处理跨地图传送
         *
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 用于处理需要在新地图线程中执行的传送操作。
         * 当运输工具需要跨地图传送时，会设置延迟传送标志，
         * 然后在此函数中执行实际的地图切换操作。
         *
         * 调用时机：在主 Update() 之后调用
         */
        void DelayedUpdate(uint32 diff);

        /**
         * @brief 构建更新数据包
         *
         * @param data_map 更新数据映射表（玩家 -> 更新数据）
         *
         * 为地图上的所有玩家构建运输工具的更新数据包，
         * 用于同步运输工具的状态变化。
         *
         * 调用时机：对象状态变化时
         */
        void BuildUpdate(UpdateDataMapType& data_map) override;

        /**
         * @brief 添加乘客到运输工具
         *
         * @param passenger 要添加的乘客对象指针
         *
         * 将乘客注册到运输工具的乘客列表中：
         *   - 设置乘客的运输工具引用
         *   - 添加"在运输工具上"移动标志
         *   - 记录运输工具GUID到乘客的移动信息中
         *
         * 调用时机：
         *   - 玩家登船时
         *   - NPC/物体被创建到运输工具上时
         */
        void AddPassenger(WorldObject* passenger);

        /**
         * @brief 从运输工具移除乘客
         *
         * @param passenger 要移除的乘客对象指针
         *
         * 从乘客列表中移除乘客：
         *   - 清除运输工具引用
         *   - 移除"在运输工具上"移动标志
         *   - 重置乘客的运输工具移动信息
         *
         * 调用时机：
         *   - 玩家下船时
         *   - 乘客被删除时
         *   - 网格卸载时
         */
        void RemovePassenger(WorldObject* passenger);

        /**
         * @brief 获取当前所有乘客
         *
         * @return 乘客集合的常量引用
         *
         * 返回动态乘客集合，包括玩家、临时召唤物等。
         * 不包括静态乘客（预定义的NPC和物体）。
         */
        PassengerSet const& GetPassengers() const { return _passengers; }

        /**
         * @brief 创建NPC乘客
         *
         * @param guid NPC的生成ID（对应 creature 表）
         * @param data NPC的生成数据
         * @return 创建的生物指针，失败返回 nullptr
         *
         * 从数据库数据创建NPC并将其放置在运输工具上。
         * NPC的位置坐标为相对于运输工具的偏移量。
         *
         * 调用时机：LoadStaticPassengers() 加载静态乘客时
         */
        Creature* CreateNPCPassenger(ObjectGuid::LowType guid, CreatureData const* data);

        /**
         * @brief 创建游戏对象乘客
         *
         * @param guid 游戏对象的生成ID（对应 gameobject 表）
         * @param data 游戏对象的生成数据
         * @return 创建的游戏对象指针，失败返回 nullptr
         *
         * 从数据库数据创建游戏对象并将其放置在运输工具上。
         * 游戏对象的位置坐标为相对于运输工具的偏移量。
         *
         * 调用时机：LoadStaticPassengers() 加载静态乘客时
         */
        GameObject* CreateGOPassenger(ObjectGuid::LowType guid, GameObjectData const* data);

        /**
         * @brief 在运输工具上临时召唤生物
         *
         * @param entry 生物模板ID（对应 creature_template 表）
         * @param pos 初始位置（相对于运输工具的偏移坐标）
         * @param summonType 召唤类型（定义生物的存在方式和持续时间）
         * @param properties 召唤属性（定义生物的控制类型、标题等）
         * @param duration 持续时间（毫秒），具体含义取决于 summonType
         * @param summoner 召唤者（用于AI目标和归属判断）
         * @param spellId 召唤法术ID
         * @param vehId 载具ID（若设置则覆盖 creature_template 中的载具ID）
         * @return 召唤的生物指针，失败返回 nullptr
         *
         * 在运输工具上创建临时召唤的生物，支持多种召唤类型：
         *   - 守护者（Guardian）：跟随召唤者
         *   - 图腾（Totem）：固定位置
         *   - 小宠物（Minion）：跟随主人
         *   - 傀儡（Puppet）：由召唤者控制
         *
         * 调用时机：脚本或法术需要动态创建运输工具上的NPC时
         */
        TempSummon* SummonPassenger(uint32 entry, Position const& pos, TempSummonType summonType, SummonPropertiesEntry const* properties = nullptr, uint32 duration = 0, Unit* summoner = nullptr, uint32 spellId = 0, uint32 vehId = 0);

        /**
         * @brief 将运输工具本地坐标转换为世界坐标
         *
         * @param x [in/out] 本地X坐标 -> 世界X坐标
         * @param y [in/out] 本地Y坐标 -> 世界Y坐标
         * @param z [in/out] 本地Z坐标 -> 世界Z坐标
         * @param o [out] 可选，本地朝向 -> 世界朝向
         *
         * 使用运输工具当前位置和朝向，将相对于运输工具的本地坐标
         * 转换为地图的绝对世界坐标。
         *
         * 调用时机：计算乘客的世界坐标位置时
         */
        void CalculatePassengerPosition(float& x, float& y, float& z, float* o = nullptr) const override
        {
            TransportBase::CalculatePassengerPosition(x, y, z, o, GetPositionX(), GetPositionY(), GetPositionZ(), GetOrientation());
        }

        /**
         * @brief 将世界坐标转换为运输工具本地坐标
         *
         * @param x [in/out] 世界X坐标 -> 本地X坐标
         * @param y [in/out] 世界Y坐标 -> 本地Y坐标
         * @param z [in/out] 世界Z坐标 -> 本地Z坐标
         * @param o [out] 可选，世界朝向 -> 本地朝向
         *
         * 使用运输工具当前位置和朝向，将地图的绝对世界坐标
         * 转换为相对于运输工具的本地坐标。
         *
         * 调用时机：计算乘客相对运输工具的位置时
         */
        void CalculatePassengerOffset(float& x, float& y, float& z, float* o = nullptr) const override
        {
            TransportBase::CalculatePassengerOffset(x, y, z, o, GetPositionX(), GetPositionY(), GetPositionZ(), GetOrientation());
        }

        /**
         * @brief 获取运输工具的运行周期
         *
         * @return 一个完整路径循环的时间（毫秒）
         *
         * 周期存储在 GAMEOBJECT_LEVEL 字段中，由模板数据初始化。
         */
        uint32 GetTransportPeriod() const override { return GetUInt32Value(GAMEOBJECT_LEVEL); }

        /**
         * @brief 设置运输工具的运行周期
         *
         * @param period 周期时间（毫秒）
         *
         * 通常由 Create() 函数调用，从模板数据设置周期。
         */
        void SetPeriod(uint32 period) { SetLevel(period); }

        /**
         * @brief 获取当前路径进度计时器
         *
         * @return 当前周期内的已过时间（毫秒）
         *
         * 用于确定运输工具在路径中的当前位置。
         */
        uint32 GetTimer() const { return GetGOValue()->Transport.PathProgress; }

        /**
         * @brief 获取所有关键帧
         *
         * @return 关键帧向量的常量引用
         *
         * 返回运输工具路径的所有关键帧数据，用于路径计算。
         */
        KeyFrameVec const& GetKeyFrames() const { return _transportInfo->keyFrames; }

        /**
         * @brief 更新运输工具位置
         *
         * @param x 新X坐标
         * @param y 新Y坐标
         * @param z 新Z坐标
         * @param o 新朝向
         *
         * 更新运输工具的世界位置并同步所有乘客的位置。
         * 同时检查网格状态变化，决定加载或卸载静态乘客。
         *
         * 调用时机：
         *   - Update() 中计算新位置后
         *   - 地图传送后
         */
        void UpdatePosition(float x, float y, float z, float o);

        /**
         * @brief 加载静态乘客
         *
         * 当运输工具移动到活跃网格时，加载预定义的NPC和游戏对象。
         * 静态乘客是从数据库预先定义好的，在运输工具上的固定位置。
         *
         * 调用时机：
         *   - 运输工具移动到活跃网格
         *   - 运输工具所在网格变为活跃
         */
        void LoadStaticPassengers();

        /**
         * @brief 卸载静态乘客
         *
         * 当运输工具进入非活跃网格时，卸载所有静态乘客。
         * 这样可以节省内存和CPU资源。
         *
         * 调用时机：
         *   - 运输工具移动到非活跃网格
         *   - 运输工具所在网格卸载
         *   - 运输工具被删除前
         */
        void UnloadStaticPassengers();

        /**
         * @brief 启用/禁用移动
         *
         * @param enabled true启用移动，false停止移动
         *
         * 用于可停止的运输工具（如电梯）。
         * 当设置为 false 时，运输工具将在下一个停靠点停止。
         *
         * 前置条件：运输工具模板的 canBeStopped 属性为 true
         */
        void EnableMovement(bool enabled);

        /**
         * @brief 设置延迟添加模型标志
         *
         * 用于跨地图传送后，延迟将运输工具模型添加到地图。
         * 防止在传送过程中出现模型位置不一致的问题。
         */
        void SetDelayedAddModelToMap() { _delayedAddModel = true; }

        /**
         * @brief 获取运输工具模板
         *
         * @return 运输工具模板的常量指针
         */
        TransportTemplate const* GetTransportTemplate() const { return _transportInfo; }

        /**
         * @brief 获取调试信息
         *
         * @return 调试信息字符串
         *
         * 返回包含运输工具详细信息的字符串，用于GM调试。
         */
        std::string GetDebugInfo() const override;

    private:
        /**
         * @brief 移动到下一个路径关键帧
         *
         * 更新 _currentFrame 和 _nextFrame 迭代器，
         * 并重置到达/离开事件标志。
         *
         * 调用时机：当前关键帧时间结束时
         */
        void MoveToNextWaypoint();

        /**
         * @brief 计算当前路径段的位置百分比
         *
         * @param perc 当前时间（秒）
         * @return 在当前路径段的位置百分比 [0.0, 1.0]
         *
         * 根据加速/减速运动学公式，计算运输工具在当前路径段的精确位置。
         * 考虑了从停靠点出发和到达停靠点的加减速过程。
         */
        float CalculateSegmentPos(float perc);

        /**
         * @brief 传送运输工具到新地图
         *
         * @param newMapid 目标地图ID
         * @param x 目标X坐标
         * @param y 目标Y坐标
         * @param z 目标Z坐标
         * @param o 目标朝向
         * @return true 需要延迟传送（跨地图）
         * @return false 已完成传送（同地图）
         *
         * 处理运输工具的地图切换：
         *   - 同地图：直接更新位置并传送乘客中的玩家
         *   - 跨地图：设置延迟传送标志，在 DelayedTeleportTransport() 中处理
         *
         * 调用时机：到达传送类型的关键帧时
         */
        bool TeleportTransport(uint32 newMapid, float x, float y, float z, float o);

        /**
         * @brief 执行延迟的地图传送
         *
         * 在跨地图传送时，实际的地图切换操作在此执行：
         *   1. 从当前地图移除运输工具
         *   2. 设置新地图
         *   3. 传送所有乘客到新地图
         *   4. 将运输工具添加到新地图
         *
         * 调用时机：DelayedUpdate() 中检测到 _delayedTeleport 标志时
         */
        void DelayedTeleportTransport();

        /**
         * @brief 更新所有乘客位置
         *
         * @param passengers 要更新的乘客集合
         *
         * 遍历乘客集合，根据运输工具的新位置计算并更新每个乘客的世界坐标。
         * 处理不同类型乘客的位置更新：
         *   - 生物：使用 CreatureRelocation
         *   - 玩家：使用 PlayerRelocation
         *   - 游戏对象：使用 GameObjectRelocation
         *   - 动态对象：使用 DynamicObjectRelocation
         *
         * 调用时机：运输工具位置更新后
         */
        void UpdatePassengerPositions(PassengerSet& passengers);

        /**
         * @brief 触发关键帧事件（如果有）
         *
         * @param node 关键帧数据
         * @param departure true为离开事件，false为到达事件
         *
         * 检查关键帧是否有到达/离开事件ID，如果有则触发相应的脚本事件。
         *
         * 调用时机：到达或离开关键帧时
         */
        void DoEventIfAny(KeyFrame const& node, bool departure);

        /**
         * @brief 检查运输工具是否正在移动
         *
         * @return true 正在移动
         * @return false 已停止
         *
         * 用于区分停靠状态和移动状态。
         */
        bool IsMoving() const { return _isMoving; }

        /**
         * @brief 设置移动状态
         *
         * @param val 移动状态
         */
        void SetMoving(bool val) { _isMoving = val; }

        // ==================== 成员变量 ====================

        /**
         * @brief 运输工具模板数据
         *
         * 包含路径、关键帧、加速参数等静态数据。
         * 生命周期由 TransportMgr 管理。
         */
        TransportTemplate const* _transportInfo;

        /**
         * @brief 当前关键帧迭代器
         *
         * 指向运输工具当前所在或刚离开的关键帧。
         */
        KeyFrameVec::const_iterator _currentFrame;

        /**
         * @brief 下一个关键帧迭代器
         *
         * 指向运输工具即将到达的下一个关键帧。
         */
        KeyFrameVec::const_iterator _nextFrame;

        /**
         * @brief 位置更新计时器
         *
         * 控制位置更新的频率（默认200ms间隔），
         * 避免频繁的位置计算和同步。
         */
        TimeTracker _positionChangeTimer;

        /**
         * @brief 移动状态标志
         *
         * true: 运输工具正在路径上移动
         * false: 运输工具停靠在某个关键帧
         */
        bool _isMoving;

        /**
         * @brief 等待停止标志
         *
         * 当 EnableMovement(false) 被调用时设置为 true，
         * 运输工具将在下一个停靠点停止。
         */
        bool _pendingStop;

        /**
         * @brief 到达事件已触发标志
         *
         * 确保每个关键帧的到达事件只触发一次。
         * 在 MoveToNextWaypoint() 中重置。
         */
        bool _triggeredArrivalEvent;

        /**
         * @brief 离开事件已触发标志
         *
         * 确保每个关键帧的离开事件只触发一次。
         * 在 MoveToNextWaypoint() 中重置。
         */
        bool _triggeredDepartureEvent;

        /**
         * @brief 动态乘客集合
         *
         * 包含玩家、临时召唤物等动态加入的乘客。
         * 这些乘客可以自由上下运输工具。
         */
        PassengerSet _passengers;

        /**
         * @brief 传送过程中的乘客迭代器
         *
         * 在跨地图传送遍历乘客时使用，防止迭代器失效。
         */
        PassengerSet::iterator _passengerTeleportItr;

        /**
         * @brief 静态乘客集合
         *
         * 包含预定义在运输工具上的NPC和游戏对象。
         * 这些乘客随运输工具进入活跃网格时加载，离开时卸载。
         */
        PassengerSet _staticPassengers;

        /**
         * @brief 延迟添加模型标志
         *
         * 跨地图传送后，延迟将模型添加到地图，确保状态一致。
         */
        bool _delayedAddModel;

        /**
         * @brief 延迟传送标志
         *
         * 当需要跨地图传送时设置为 true，
         * 在 DelayedUpdate() 中执行实际传送。
         */
        bool _delayedTeleport;
};

#endif
