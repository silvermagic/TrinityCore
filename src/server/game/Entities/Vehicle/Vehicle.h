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
 * @file Vehicle.h
 * @brief 载具系统核心头文件
 *
 * 本文件定义了 TrinityCore 的载具系统核心类 Vehicle，用于实现游戏中的载具机制。
 * 载具系统支持玩家或生物乘坐其他单位，广泛应用于坐骑、攻城武器、交通工具等场景。
 *
 * 主要功能：
 * - 乘客管理（添加、移除、弹出乘客）
 * - 座位系统（支持多座位载具）
 * - 配件系统（载具自带的NPC乘客）
 * - 坐标转换（局部坐标与全局坐标的相互转换）
 * - 延迟加入机制（通过事件系统实现）
 *
 * 典型应用场景：
 * - 玩家骑乘坐骑
 * - 玩家控制攻城武器（如冬拥湖的攻城车）
 * - NPC载运玩家（如飞行路径中的飞行坐骑）
 * - 多人载具（如多人乘坐的船只或飞行器）
 *
 * @see Vehicle.cpp 实现代码
 * @see VehicleDefines.h 载具相关定义和枚举
 * @see VehicleEntry DBC数据结构定义
 */

#ifndef __TRINITY_VEHICLE_H
#define __TRINITY_VEHICLE_H

#include "ObjectDefines.h"
#include "Object.h"
#include "UniqueTrackablePtr.h"
#include "Unit.h"
#include "VehicleDefines.h"
#include <list>

struct VehicleEntry;
class Unit;
class VehicleJoinEvent;

/**
 * @brief 载具类 - 管理可乘坐单位（如坐骑、攻城车、飞行坐骑等）
 *
 * 载具系统允许单位（玩家或生物）成为其他单位的"载体"。
 * 典型应用场景：
 * - 玩家骑乘坐骑
 * - 玩家控制攻城武器
 * - NPC载运玩家（如飞行路径）
 * - 多人载具（如多人乘坐的载具）
 *
 * 该类继承自 TransportBase，提供了乘客管理、座位系统、
 * 载具安装/卸载等核心功能。
 */
class TC_GAME_API Vehicle : public TransportBase
{
    public:
        /**
         * @brief 构造函数 - 初始化载具实例
         * @param unit 拥有此载具的单位（载体），可以是玩家或生物
         * @param vehInfo 载具的DBC数据条目，从Vehicle.dbc加载
         * @param creatureEntry 生物条目ID，用于配件召唤等功能
         *
         * @note 此构造函数仅初始化成员变量，不执行安装操作
         *       需要在构造后调用Install()方法来完成载具的初始化
         *
         * @warning unit参数必须非空，否则会导致未定义行为
         *
         * 性能说明：
         * - 时间复杂度：O(1)，仅成员初始化
         * - 内存分配：最小，仅构造对象本身
         * - 线程安全：否，必须在单位所属线程调用
         */
        Vehicle(Unit* unit, VehicleEntry const* vehInfo, uint32 creatureEntry);

        /**
         * @brief 析构函数 - 清理载具资源
         *
         * 自动移除所有乘客，清理配件和待处理事件。
         * 确保载具资源被正确释放，避免内存泄漏。
         *
         * @warning 不应直接调用析构函数
         *          应通过Unit::DestroyVehicle()来销毁载具
         *
         * 执行时机：
         * - 单位销毁时自动调用
         * - 气泡变形结束时
         * - 载具法术效果结束时
         */
        ~Vehicle();

        /**
         * @brief 安装载具 - 初始化载具系统和座位
         *
         * 执行载具的完整安装流程：
         * 1. 初始化所有座位（从DBC数据加载座位信息）
         * 2. 应用载具免疫效果（如对某些控制效果的免疫）
         * 3. 安装所有预定义配件（如炮塔操作员等NPC）
         * 4. 初始化基础单位的移动信息
         *
         * @note 必须在构造后立即调用，否则载具无法正常工作
         *
         * 执行时机：
         * - 创建新的载具实例后
         * - 单位首次获得载具光环时
         * - 气泡变形开始时
         *
         * 性能说明：
         * - 时间复杂度：O(n)，n为座位数量和配件数量之和
         * - 可能触发数据库查询（配件创建）
         * - 会发送网络包给周围玩家
         */
        void Install();

        /**
         * @brief 卸载载具 - 清理载具系统
         *
         * 执行载具的完整卸载流程：
         * 1. 设置状态为STATUS_UNINSTALLING，防止重入
         * 2. 移除所有乘客（弹出乘客到世界坐标）
         * 3. 清理所有配件
         * 4. 移除免疫效果
         * 5. 清空座位映射
         *
         * @note 卸载后载具对象仍然存在，但处于未安装状态
         *       可以重新调用Install()再次安装
         *
         * 执行时机：
         * - 单位死亡时
         * - 气泡变形结束时
         * - 载具光环被驱散或过期时
         * - 单位被移出世界时
         *
         * @warning 此方法会修改载具状态，不要在遍历乘客时调用
         */
        void Uninstall();

        /**
         * @brief 重置载具状态 - 恢复载具到初始状态
         * @param evading 是否为逃避状态下的重置
         *                true：生物逃避回家时重置，保留配件
         *                false：完全重置，重新安装配件
         *
         * 重置操作：
         * - 移除所有乘客（配件除外）
         * - 如果evading为false，重新安装所有配件
         * - 重置座位状态
         *
         * 执行时机：
         * - 生物逃避回家后（evading=true）
         * - 载具重置法术触发时（evading=false）
         * - 脚本主动调用时
         *
         * 性能说明：
         * - 时间复杂度：O(n)，n为乘客数量
         * - evading=false时可能触发配件召唤
         */
        void Reset(bool evading = false);

        /**
         * @brief 安装所有配件 - 根据DBC数据或数据库配置安装载具配件
         * @param evading 是否为逃避状态
         *                true：逃避状态下不重新安装配件
         *                false：正常安装所有配件
         *
         * 配件（Accessory）是载具自带的NPC乘客，典型例子：
         * - 攻城车的炮塔操作员
         * - 多人载具的驾驶员座位NPC
         * - 固定武器的控制者
         *
         * 配件来源：
         * 1. Vehicle.dbc中定义的默认配件
         * 2. 数据库表vehicle_accessory中的自定义配件
         *
         * 执行时机：
         * - Install()时被调用
         * - Reset(false)时被调用
         * - 脚本主动调用时
         *
         * 性能说明：
         * - 时间复杂度：O(n)，n为配件数量
         * - 会触发数据库查询
         * - 会创建新的Creature对象
         * - 会发送网络包给周围玩家
         */
        void InstallAllAccessories(bool evading);

        /**
         * @brief 应用所有免疫效果 - 为载具添加特定的免疫光环
         *
         * 载具通常对某些控制效果免疫，包括但不限于：
         * - 媚惑效果
         * - 恐惧效果
         * - 变形效果
         * - 眩晕效果（部分载具）
         * - 击退效果
         *
         * 免疫来源：
         * - Vehicle.dbc中定义的免疫标志位
         * - 特定载具类型的硬编码免疫
         *
         * 执行时机：
         * - Install()时被调用
         * - 载具类型变更时可能重新应用
         *
         * @note 免疫效果通过施加特定的光环实现
         *       某些载具可能没有免疫效果
         */
        void ApplyAllImmunities();

        /**
         * @brief 安装单个配件 - 动态添加配件到载具
         * @param entry 配件生物的条目ID（Creature ID）
         * @param seatId 目标座位ID（-1表示自动分配）
         * @param minion 是否为仆从类型
         *               true：配件作为基础单位的仆从，死亡时会消失
         *               false：配件为独立生物
         * @param type 配件类型，影响配件的行为和属性
         * @param summonTime 召唤持续时间（毫秒），0表示永久存在
         *
         * 此方法可从脚本调用，用于动态添加配件。
         * 典型应用：
         * - 脚本控制的载具配件
         * - 任务特定的载具NPC
         * - 动态生成的武器操作员
         *
         * 执行流程：
         * 1. 在基础单位位置召唤生物
         * 2. 将生物添加到载具指定座位
         * 3. 设置配件的从属关系
         * 4. 如果summonTime > 0，设置定时消失
         *
         * 性能说明：
         * - 时间复杂度：O(1)
         * - 会创建新的Creature对象
         * - 会触发数据库查询（生物数据）
         * - 会发送网络包给周围玩家
         */
        void InstallAccessory(uint32 entry, int8 seatId, bool minion, uint8 type, uint32 summonTime);

        /**
         * @brief 获取载具的基础单位
         * @return 拥有此载具的单位指针
         *
         * 基础单位是载具的"载体"，可以是：
         * - 玩家（Player）
         * - 生物（Creature）
         *
         * @note 返回值永不为空（载具生命周期内）
         *
         * 使用示例：
         * @code
         * Unit* base = vehicle->GetBase();
         * if (base->IsPlayer())
         *     // 处理玩家载具逻辑
         * @endcode
         */
        Unit* GetBase() const { return _me; }

        /**
         * @brief 获取载具的DBC信息
         * @return 载具条目数据指针
         */
        VehicleEntry const* GetVehicleInfo() const { return _vehicleInfo; }

        /**
         * @brief 获取生物条目ID
         * @return 生物条目ID
         */
        uint32 GetCreatureEntry() const { return _creatureEntry; }

        /**
         * @brief 检查指定座位是否为空
         * @param seatId 座位ID
         * @return 座位是否空闲
         */
        bool HasEmptySeat(int8 seatId) const;

        /**
         * @brief 获取指定座位的乘客
         * @param seatId 座位ID
         * @return 乘客单位指针，无乘客则返回nullptr
         */
        Unit* GetPassenger(int8 seatId) const;

        /**
         * @brief 获取下一个空座位
         * @param seatId 起始座位ID
         * @param next 是否向前查找（true为下一个，false为上一个）
         * @return 指向空座位的迭代器
         */
        SeatMap::const_iterator GetNextEmptySeat(int8 seatId, bool next) const;

        /**
         * @brief 获取乘客所在座位的附加信息
         * @param passenger 乘客单位
         * @return 座位附加信息指针
         */
        VehicleSeatAddon const* GetSeatAddonForSeatOfPassenger(Unit const* passenger) const;

        /**
         * @brief 获取可用座位数量
         * @return 空闲座位的数量
         */
        uint8 GetAvailableSeatCount() const;

        /**
         * @brief 添加乘客到载具
         * @param passenger 要添加的乘客
         * @param seatId 目标座位ID（-1表示自动分配）
         * @return 是否成功添加
         */
        bool AddPassenger(Unit* passenger, int8 seatId = -1);

        /**
         * @brief 弹出乘客
         * @param passenger 要弹出的乘客
         * @param controller 控制者（触发弹出的单位）
         */
        void EjectPassenger(Unit* passenger, Unit* controller);

        /**
         * @brief 移除乘客
         * @param passenger 要移除的乘客
         * @return 载具指针（用于链式调用）
         */
        Vehicle* RemovePassenger(Unit* passenger);

        /**
         * @brief 重定位所有乘客
         *
         * 更新所有乘客的位置坐标，使其与载具位置同步
         */
        void RelocatePassengers();

        /**
         * @brief 移除所有乘客
         *
         * 清空载具上的所有乘客
         */
        void RemoveAllPassengers();

        /**
         * @brief 检查载具是否在使用中
         * @return 是否有乘客
         */
        bool IsVehicleInUse() const;

        /**
         * @brief 检查是否为可控制载具
         * @return 是否可被玩家控制
         */
        bool IsControllableVehicle() const;

        SeatMap Seats;                                      ///< 座位映射表，存储所有座位的详细信息
                                                            ///< 键为座位ID，值为VehicleSeat结构
                                                            ///< 包含空闲座位和已占用座位
                                                            ///< 遍历此映射可获取所有座位状态

        /**
         * @brief 获取乘客所在的座位数据
         * @param passenger 乘客单位
         * @return 座位条目数据指针
         */
        VehicleSeatEntry const* GetSeatForPassenger(Unit const* passenger) const;

        /**
         * @brief 移除乘客的待处理事件
         * @param passenger 乘客单位
         */
        void RemovePendingEventsForPassenger(Unit* passenger);

        /**
         * @brief 获取消失延迟时间
         * @return 消失延迟（毫秒）
         */
        Milliseconds GetDespawnDelay();

        /**
         * @brief 获取调试信息字符串
         * @return 调试信息
         */
        std::string GetDebugInfo() const;

        /**
         * @brief 获取载具的弱指针
         * @return 弱指针引用
         */
        Trinity::unique_weak_ptr<Vehicle> GetWeakPtr() const;

    protected:
        friend class VehicleJoinEvent;                      ///< 友元类，允许VehicleJoinEvent访问私有成员
        uint32 UsableSeatNum;                               ///< 玩家可使用的座位数量
                                                            ///< 用于客户端正确显示载具交互标志
                                                            ///< 仅统计可被玩家乘坐的座位数量

    private:
        /**
         * @brief 载具状态枚举
         */
        enum Status
        {
            STATUS_NONE,           ///< 无状态（未安装）
            STATUS_INSTALLED,      ///< 已安装
            STATUS_UNINSTALLING,   ///< 正在卸载中
        };

        /**
         * @brief 获取乘客的座位迭代器
         * @param passenger 乘客单位
         * @return 座位迭代器
         */
        SeatMap::iterator GetSeatIteratorForPassenger(Unit* passenger);

        /**
         * @brief 初始化基础单位的移动信息
         */
        void InitMovementInfoForBase();

        /**
         * @brief 将运输工具偏移坐标转换为全局坐标
         * @param x X坐标（输入偏移，输出全局坐标）
         * @param y Y坐标（输入偏移，输出全局坐标）
         * @param z Z坐标（输入偏移，输出全局坐标）
         * @param o 朝向（可选，输入偏移，输出全局朝向）
         *
         * 此方法将相对于载具的局部坐标转换为世界全局坐标
         */
        void CalculatePassengerPosition(float& x, float& y, float& z, float* o /*= nullptr*/) const override
        {
            TransportBase::CalculatePassengerPosition(x, y, z, o,
                GetBase()->GetPositionX(), GetBase()->GetPositionY(),
                GetBase()->GetPositionZ(), GetBase()->GetOrientation());
        }

        /**
         * @brief 将全局坐标转换为运输工具偏移坐标
         * @param x X坐标（输入全局，输出偏移坐标）
         * @param y Y坐标（输入全局，输出偏移坐标）
         * @param z Z坐标（输入全局，输出偏移坐标）
         * @param o 朝向（可选，输入全局，输出偏移朝向）
         *
         * 此方法将世界全局坐标转换为相对于载具的局部坐标
         */
        void CalculatePassengerOffset(float& x, float& y, float& z, float* o /*= nullptr*/) const override
        {
            TransportBase::CalculatePassengerOffset(x, y, z, o,
                GetBase()->GetPositionX(), GetBase()->GetPositionY(),
                GetBase()->GetPositionZ(), GetBase()->GetOrientation());
        }

        /**
         * @brief 移除待处理的加入事件
         * @param e 要移除的事件指针
         */
        void RemovePendingEvent(VehicleJoinEvent* e);

        /**
         * @brief 移除指定座位的所有待处理事件
         * @param seatId 座位ID
         */
        void RemovePendingEventsForSeat(int8 seatId);

        /**
         * @brief 检查座位是否有待处理事件
         * @param seatId 座位ID
         * @return 是否有待处理事件
         */
        bool HasPendingEventForSeat(int8 seatId) const;

    private:
        Unit* _me;                                          ///< 载具的基础单位（载体）
                                                            ///< 可以是玩家或生物（Creature）
                                                            ///< 此单位"拥有"载具，载具数据附加在其上
                                                            ///< 生命周期与基础单位绑定

        VehicleEntry const* _vehicleInfo;                   ///< 载具的DBC数据条目
                                                            ///< 从Vehicle.dbc加载的静态数据
                                                            ///< 包含载具类型、座位数量等信息
                                                            ///< 整个载具生命周期内不会改变

        GuidSet vehiclePlayers;                             ///< 当前载具上的玩家GUID集合
                                                            ///< 用于快速查找载具上的所有玩家
                                                            ///< 主要用于多人载具场景
                                                            ///< 当玩家加入/离开载具时更新

        uint32 _creatureEntry;                              ///< 生物条目ID
                                                            ///< 当载体为玩家时，此值可能与_me的实际条目不同
                                                            ///< 用于某些需要特定生物数据的功能
                                                            ///< 例如：载具配件的召唤逻辑

        Status _status;                                     ///< 载具内部状态
                                                            ///< 用于完整性检查和状态机管理
                                                            ///< 防止在未安装状态调用载具功能
                                                            ///< 或在卸载过程中执行操作

        typedef std::list<VehicleJoinEvent*> PendingJoinEventContainer;
        PendingJoinEventContainer _pendingJoinEvents;       ///< 待处理的乘客加入事件列表
                                                            ///< 实现延迟加入机制
                                                            ///< 当乘客尝试进入载具但需要等待时使用
                                                            ///< 例如：等待其他乘客离开、等待动画完成
                                                            ///< 事件完成后会从此列表移除
};

/**
 * @brief 载具加入事件类 - 处理延迟的乘客加入操作
 *
 * 当乘客尝试进入载具时，可能需要延迟处理（如等待动画完成）。
 * 此事件类封装了延迟加入的逻辑。
 */
class TC_GAME_API VehicleJoinEvent : public BasicEvent
{
    friend class Vehicle;
    protected:
        /**
         * @brief 构造函数
         * @param v 目标载具
         * @param u 乘客单位
         */
        VehicleJoinEvent(Vehicle* v, Unit* u) : Target(v), Passenger(u), Seat(Target->Seats.end()) { }

        /**
         * @brief 执行事件 - 实际将乘客加入载具
         * @param 时间参数（未使用）
         * @param 时间参数（未使用）
         * @return 是否成功
         */
        bool Execute(uint64, uint32) override;

        /**
         * @brief 中止事件 - 处理加入被取消的情况
         * @param 时间参数（未使用）
         */
        void Abort(uint64) override;

        Vehicle* Target;                                    ///< 目标载具指针
                                                            ///< 乘客将要加入的载具
                                                            ///< 事件执行时会将乘客添加到此载具

        Unit* Passenger;                                    ///< 要加入的乘客单位
                                                            ///< 可以是玩家或生物
                                                            ///< 事件执行时会将此单位添加到载具

        SeatMap::iterator Seat;                             ///< 目标座位的迭代器
                                                            ///< 指向Seats映射中的特定座位
                                                            ///< 用于确定乘客的最终座位
                                                            ///< 如果为Seats.end()表示需要重新分配
};

#endif
