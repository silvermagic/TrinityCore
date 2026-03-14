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
 * @file VehicleDefines.h
 * @brief 载具系统核心定义文件
 *
 * 本文件定义了载具系统所需的基础数据结构、枚举类型和辅助类：
 * - 载具能量类型（PowerType）：定义载具使用的特殊能量类型
 * - 载具标志位（VehicleFlags）：控制载具移动行为和能力
 * - 座位信息结构：管理乘客数据和座位附加参数
 * - 载具附件和模板：定义载具的附属单位配置
 * - TransportBase 基类：提供载具坐标变换的数学基础
 *
 * 这些定义为 Vehicle.cpp 和相关载具逻辑提供基础数据支持。
 */

#ifndef __TRINITY_VEHICLEDEFINES_H
#define __TRINITY_VEHICLEDEFINES_H

#include "Define.h"
#include "Duration.h"
#include <vector>
#include <map>

struct VehicleSeatEntry;

/**
 * @brief 载具特殊能量类型枚举
 *
 * 定义载具使用的特殊能量类型，这些能量类型不同于玩家职业的能量类型，
 * 主要用于特定载具（如攻城车、战斗机器人等）的能量系统。
 *
 * 数值对应 DBC 文件中的能量类型 ID。
 */
enum PowerType
{
    POWER_STEAM                                  = 61,   ///< 蒸汽能量 - 用于蒸汽驱动的载具
    POWER_PYRITE                                 = 41,   ///< 黄铁矿能量 - 用于奥杜尔相关载具
    POWER_HEAT                                   = 101,  ///< 热能 - 用于火焰驱动的载具
    POWER_OOZE                                   = 121,  ///< 粘液能量 - 用于天灾军团相关载具
    POWER_BLOOD                                  = 141,  ///< 鲜血能量 - 用于鲜血女王相关载具
    POWER_WRATH                                  = 142   ///< 愤怒能量 - 用于巫妖王战斗的载具
};

/**
 * @brief 载具行为标志位枚举
 *
 * 定义载具的移动能力和行为限制。这些标志位会影响载具移动数据包中的 MOVEFLAG2 字段，
 * 控制玩家的移动操作权限和载具的物理行为。
 *
 * 标志位可通过位运算组合使用。
 */
enum VehicleFlags
{
    VEHICLE_FLAG_NO_STRAFE                       = 0x00000001,   ///< 禁止横移 - 设置 MOVEFLAG2_NO_STRAFE，玩家无法左右平移
    VEHICLE_FLAG_NO_JUMPING                      = 0x00000002,   ///< 禁止跳跃 - 设置 MOVEFLAG2_NO_JUMPING，玩家无法跳跃
    VEHICLE_FLAG_FULLSPEEDTURNING                = 0x00000004,   ///< 全速转向 - 设置 MOVEFLAG2_FULLSPEEDTURNING，转向不减速
    VEHICLE_FLAG_ALLOW_PITCHING                  = 0x00000010,   ///< 允许俯仰 - 设置 MOVEFLAG2_ALLOW_PITCHING，允许玩家控制俯仰角
    VEHICLE_FLAG_FULLSPEEDPITCHING               = 0x00000020,   ///< 全速俯仰 - 设置 MOVEFLAG2_FULLSPEEDPITCHING，俯仰时不减速
    VEHICLE_FLAG_CUSTOM_PITCH                    = 0x00000040,   ///< 自定义俯仰范围 - 使用 DBC 中的 pitchMin 和 pitchMax，否则使用默认值 ±π/2
    VEHICLE_FLAG_ADJUST_AIM_ANGLE                = 0x00000400,   ///< 可调整瞄准角度 - 对应 Lua_IsVehicleAimAngleAdjustable
    VEHICLE_FLAG_ADJUST_AIM_POWER                = 0x00000800,   ///< 可调整瞄准力度 - 对应 Lua_IsVehicleAimPowerAdjustable
    VEHICLE_FLAG_FIXED_POSITION                  = 0x00200000    ///< 固定位置 - 用于炮台等需要定身的载具
};

/**
 * @brief 载具相关法术枚举
 *
 * 定义载具系统使用的硬编码法术 ID。这些法术用于处理玩家上下载具、
 * 降落伞等特殊载具交互行为。
 */
enum VehicleSpells
{
    VEHICLE_SPELL_RIDE_HARDCODED                 = 46598,  ///< 硬编码骑乘法术 - 用于某些特殊载具的骑乘状态
    VEHICLE_SPELL_PARACHUTE                      = 45472   ///< 降落伞法术 - 玩家离开飞行载具时自动施放降落伞
};

/**
 * @brief 载具退出参数类型枚举
 *
 * 定义玩家离开载具时坐标参数的解释方式。用于 VehicleSeatAddon 结构，
 * 决定 ExitParameterX/Y/Z/O 的含义。
 *
 * 使用示例：
 * - VehicleExitParamOffset: ExitParameterX/Y/Z 为相对于载具的偏移量
 * - VehicleExitParamDest: ExitParameterX/Y/Z 为世界绝对坐标
 */
enum class VehicleExitParameters
{
    VehicleExitParamNone    = 0,  ///< 无参数 - 提供的参数将被忽略，使用默认退出逻辑
    VehicleExitParamOffset  = 1,  ///< 偏移模式 - 参数作为相对于载具的偏移值使用
    VehicleExitParamDest    = 2,  ///< 目标模式 - 参数作为绝对目标坐标使用
    VehicleExitParamMax              ///< 枚举边界值，用于参数校验
};

/**
 * @brief 乘客信息结构
 *
 * 存储载具座位上的乘客数据。每个 VehicleSeat 包含一个 PassengerInfo 实例，
 * 用于跟踪乘客的 GUID 和交互状态。
 */
struct PassengerInfo
{
    ObjectGuid Guid;            ///< 乘客的全局唯一标识符
    bool IsUninteractible;      ///< 是否不可交互 - 为 true 时其他玩家无法与该乘客交互

    /**
     * @brief 重置乘客信息
     *
     * 清空 GUID 并将交互状态重置为默认值（可交互）。
     * 当乘客离开座位时调用。
     */
    void Reset()
    {
        Guid.Clear();
        IsUninteractible = false;
    }
};

/**
 * @brief 载具座位附加数据结构
 *
 * 存储座位的扩展参数，包括座位朝向偏移和玩家退出载具时的位置参数。
 * 这些数据由数据库或 DBC 文件加载，用于定制座位的特殊行为。
 *
 * 与 VehicleSeatEntry（DBC 数据）配合使用，提供额外的座位配置能力。
 */
struct VehicleSeatAddon
{
    /**
     * @brief 默认构造函数
     *
     * 初始化所有参数为默认值（零偏移，无退出参数）。
     */
    VehicleSeatAddon() { }

    /**
     * @brief 参数化构造函数
     *
     * @param orientatonOffset 座位朝向偏移角度（弧度）
     * @param exitX 退出参数 X 坐标（含义由 ExitParameter 决定）
     * @param exitY 退出参数 Y 坐标（含义由 ExitParameter 决定）
     * @param exitZ 退出参数 Z 坐标（含义由 ExitParameter 决定）
     * @param exitO 退出朝向角度（弧度）
     * @param param 退出参数类型，转换为 VehicleExitParameters 枚举
     */
    VehicleSeatAddon(float orientatonOffset, float exitX, float exitY, float exitZ, float exitO, uint8 param) :
        SeatOrientationOffset(orientatonOffset), ExitParameterX(exitX), ExitParameterY(exitY), ExitParameterZ(exitZ),
        ExitParameterO(exitO), ExitParameter(VehicleExitParameters(param)) { }

    float SeatOrientationOffset = 0.f;    ///< 座位朝向偏移角度（弧度），叠加在载具基础朝向上
    float ExitParameterX = 0.f;           ///< 退出参数 X - 根据退出模式为偏移量或绝对坐标
    float ExitParameterY = 0.f;           ///< 退出参数 Y - 根据退出模式为偏移量或绝对坐标
    float ExitParameterZ = 0.f;           ///< 退出参数 Z - 根据退出模式为偏移量或绝对坐标
    float ExitParameterO = 0.f;           ///< 退出朝向角度（弧度）
    VehicleExitParameters ExitParameter = VehicleExitParameters::VehicleExitParamNone;  ///< 退出参数类型
};

/**
 * @brief 载具座位结构
 *
 * 表示载具的一个座位，包含座位基础信息（DBC）、附加数据和当前乘客信息。
 * 每个载具实例维护一个 SeatMap（座位映射表），管理所有座位的乘客关系。
 *
 * 座位生命周期：
 * - 构造时初始化 DBC 数据和附加数据，重置乘客信息
 * - 玩家进入载具时更新 Passenger.Guid
 * - 玩家离开载具时调用 Passenger.Reset()
 */
struct VehicleSeat
{
    /**
     * @brief 构造函数
     *
     * 初始化座位的基础数据和附加数据，并重置乘客信息为空状态。
     *
     * @param seatInfo DBC 座位数据指针（不可为 nullptr）
     * @param seatAddon 座位附加数据指针（可为 nullptr，表示无附加数据）
     */
    explicit VehicleSeat(VehicleSeatEntry const* seatInfo, VehicleSeatAddon const* seatAddon) : SeatInfo(seatInfo), SeatAddon(seatAddon)
    {
        Passenger.Reset();
    }

    /**
     * @brief 检查座位是否为空
     *
     * @return true 座位上没有乘客
     * @return false 座位已被占用
     *
     * @note 性能说明：内联函数，直接检查 GUID 是否为空，复杂度 O(1)
     */
    bool IsEmpty() const { return Passenger.Guid.IsEmpty(); }

    VehicleSeatEntry const* SeatInfo;    ///< DBC 座位基础数据（包含座位 ID、标志位等）
    VehicleSeatAddon const* SeatAddon;   ///< 座位附加数据（可为 nullptr）
    PassengerInfo Passenger;             ///< 当前乘客信息
};

/**
 * @brief 载具附件结构
 *
 * 定义载具的附属单位配置。载具附件是预先配置的生物，在载具生成时自动召唤并放置到指定座位。
 * 例如：坦克载具的炮手、副驾驶等。
 *
 * 数据来源：从数据库表 `vehicle_template_accessory` 和 `vehicle_accessory` 加载。
 * 触发时机：载具初始化时根据配置自动召唤附件。
 */
struct VehicleAccessory
{
    /**
     * @brief 构造函数
     *
     * @param entry 附件生物模板 ID（creature_template.entry）
     * @param seatId 目标座位 ID（对应 VehicleSeatEntry.ID）
     * @param isMinion 是否为仆从（影响仇恨和跟随逻辑）
     * @param summonType 召唤类型（定义召唤后的行为）
     * @param summonTime 召唤持续时间（毫秒），0 表示永久存在
     */
    VehicleAccessory(uint32 entry, int8 seatId, bool isMinion, uint8 summonType, uint32 summonTime) :
        AccessoryEntry(entry), IsMinion(isMinion), SummonTime(summonTime), SeatId(seatId), SummonedType(summonType) { }

    uint32 AccessoryEntry;  ///< 附件生物模板 ID
    bool IsMinion;          ///< 是否为仆从 - 仆从会跟随载具的仇恨目标
    uint32 SummonTime;      ///< 召唤持续时间（毫秒），0 为永久
    int8 SeatId;            ///< 目标座位 ID，-1 表示自动分配空座位
    uint8 SummonedType;     ///< 召唤类型（定义生物的初始化行为）
};

/**
 * @brief 载具模板结构
 *
 * 定义载具实例的模板数据。这些参数从数据库加载，控制载具的基本行为。
 * 每个载具创建时会关联一个 VehicleTemplate 实例。
 *
 * 数据来源：从数据库表 `vehicle_template` 加载。
 */
struct VehicleTemplate
{
    Milliseconds DespawnDelay = Milliseconds::zero();  ///< 消失延迟时间 - 载具所有乘客离开后延迟消失的时间
};

/// 载具附件列表类型 - 单个载具的所有附件配置
typedef std::vector<VehicleAccessory> VehicleAccessoryList;

/// 载具附件容器类型 - 以载具模板 ID 为键的附件映射表
typedef std::map<uint32, VehicleAccessoryList> VehicleAccessoryContainer;

/// 座位映射表类型 - 以座位 ID 为键的座位实例映射
typedef std::map<int8, VehicleSeat> SeatMap;

/**
 * @brief 传送基类
 *
 * 提供载具坐标变换的数学基础。所有可承载乘客的实体（Vehicle、Transport 等）
 * 都应继承此类，实现局部坐标与世界坐标的双向转换。
 *
 * 核心功能：
 * - 将乘客的局部坐标（相对于载具）转换为世界坐标
 * - 将乘客的世界坐标转换为局部坐标（相对于载具）
 *
 * 使用场景：
 * - 玩家在移动的载具上时，需要实时更新世界坐标
 * - 载具移动时，需要更新所有乘客的世界坐标
 * - 玩家离开载具时，计算其应该出现的世界位置
 *
 * 继承关系：
 * - Vehicle 类继承此接口，实现载具的坐标变换
 * - Transport 类继承此接口，实现船只、飞艇等交通载具的坐标变换
 */
class TransportBase
{
protected:
    /**
     * @brief 默认构造函数（受保护）
     *
     * 仅允许派生类构造，禁止直接实例化 TransportBase。
     */
    TransportBase() { }

    /**
     * @brief 虚析构函数（受保护）
     *
     * 确保派生类可以正确析构，避免内存泄漏。
     */
    virtual ~TransportBase() { }

public:
    /**
     * @brief 将局部坐标转换为世界坐标（纯虚函数）
     *
     * 将相对于载具的偏移坐标转换为全局世界坐标。
     * 例如：乘客在载具内部的局部位置 -> 乘客在世界地图中的位置
     *
     * @param[in,out] x 局部 X 坐标（输入）/ 世界 X 坐标（输出）
     * @param[in,out] y 局部 Y 坐标（输入）/ 世界 Y 坐标（输出）
     * @param[in,out] z 局部 Z 坐标（输入）/ 世界 Z 坐标（输出）
     * @param[in,out] o 局部朝向（输入）/ 世界朝向（输出），可为 nullptr
     *
     * @note 必须由派生类实现，通常调用静态版本的辅助函数
     * @note 性能说明：频繁调用（每个载具乘客每帧），需保证高效
     */
    virtual void CalculatePassengerPosition(float& x, float& y, float& z, float* o = nullptr) const = 0;

    /**
     * @brief 将世界坐标转换为局部坐标（纯虚函数）
     *
     * 将全局世界坐标转换为相对于载具的偏移坐标。
     * 例如：乘客在世界地图中的位置 -> 乘客在载具内部的局部位置
     *
     * @param[in,out] x 世界 X 坐标（输入）/ 局部 X 坐标（输出）
     * @param[in,out] y 世界 Y 坐标（输入）/ 局部 Y 坐标（输出）
     * @param[in,out] z 世界 Z 坐标（输入）/ 局部 Z 坐标（输出）
     * @param[in,out] o 世界朝向（输入）/ 局部朝向（输出），可为 nullptr
     *
     * @note 必须由派生类实现，通常调用静态版本的辅助函数
     * @note 性能说明：用于计算相对位置，调用频率较低
     */
    virtual void CalculatePassengerOffset(float& x, float& y, float& z, float* o = nullptr) const = 0;

protected:
    /**
     * @brief 局部坐标转世界坐标的静态实现
     *
     * 使用旋转矩阵和位移向量，将局部坐标转换为世界坐标。
     * 数学公式：
     * - 世界X = 载具X + 局部X * cos(载具O) - 局部Y * sin(载具O)
     * - 世界Y = 载具Y + 局部Y * cos(载具O) + 局部X * sin(载具O)
     * - 世界Z = 载具Z + 局部Z
     * - 世界O = 载具O + 局部O（归一化到 [0, 2π)）
     *
     * @param[in,out] x 局部 X 坐标（输入）/ 世界 X 坐标（输出）
     * @param[in,out] y 局部 Y 坐标（输入）/ 世界 Y 坐标（输出）
     * @param[in,out] z 局部 Z 坐标（输入）/ 世界 Z 坐标（输出）
     * @param[in,out] o 局部朝向（输入）/ 世界朝向（输出），可为 nullptr
     * @param transX 载具的世界 X 坐标
     * @param transY 载具的世界 Y 坐标
     * @param transZ 载具的世界 Z 坐标
     * @param transO 载具的世界朝向
     *
     * @note 此函数为纯数学计算，不依赖对象状态
     * @note 性能说明：使用三角函数，约 2 次 sin/cos 调用，复杂度 O(1)
     */
    static void CalculatePassengerPosition(float& x, float& y, float& z, float* o, float transX, float transY, float transZ, float transO)
    {
        float inx = x, iny = y, inz = z;
        if (o)
            *o = Position::NormalizeOrientation(transO + *o);  // 朝向叠加并归一化

        // 应用旋转变换：旋转矩阵 [cos(θ), -sin(θ); sin(θ), cos(θ)]
        x = transX + inx * std::cos(transO) - iny * std::sin(transO);
        y = transY + iny * std::cos(transO) + inx * std::sin(transO);
        z = transZ + inz;  // Z 轴仅做位移，不做旋转
    }

    /**
     * @brief 世界坐标转局部坐标的静态实现
     *
     * 逆向变换，将世界坐标转换为相对于载具的局部坐标。
     * 数学公式（逆矩阵）：
     * - 局部Z = 世界Z - 载具Z
     * - 局部O = 世界O - 载具O（归一化）
     * - 局部X/Y 通过解方程组获得
     *
     * @param[in,out] x 世界 X 坐标（输入）/ 局部 X 坐标（输出）
     * @param[in,out] y 世界 Y 坐标（输入）/ 局部 Y 坐标（输出）
     * @param[in,out] z 世界 Z 坐标（输入）/ 局部 Z 坐标（输出）
     * @param[in,out] o 世界朝向（输入）/ 局部朝向（输出），可为 nullptr
     * @param transX 载具的世界 X 坐标
     * @param transY 载具的世界 Y 坐标
     * @param transZ 载具的世界 Z 坐标
     * @param transO 载具的世界朝向
     *
     * @note 此函数为纯数学计算，不依赖对象状态
     * @note 性能说明：使用三角函数和 tan()，约 3 次三角函数调用，复杂度 O(1)
     */
    static void CalculatePassengerOffset(float& x, float& y, float& z, float* o, float transX, float transY, float transZ, float transO)
    {
        if (o)
            *o = Position::NormalizeOrientation(*o - transO);  // 朝向相减并归一化

        // 逆向位移
        z -= transZ;
        y -= transY;    // y = searchedY * std::cos(o) + searchedX * std::sin(o)
        x -= transX;    // x = searchedX * std::cos(o) + searchedY * std::sin(o + pi)
        float inx = x, iny = y;

        // 逆向旋转：解线性方程组
        // 数学推导：从旋转矩阵的逆变换推导而来
        y = (iny - inx * std::tan(transO)) / (std::cos(transO) + std::sin(transO) * std::tan(transO));
        x = (inx + iny * std::tan(transO)) / (std::cos(transO) + std::sin(transO) * std::tan(transO));
    }
};

#endif
