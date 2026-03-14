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
 * @file MovementInfo.h
 * @brief 移动信息数据结构定义
 *
 * 本文件定义了 MovementInfo 结构体，用于存储和管理游戏实体的移动状态信息。
 * 该结构体是客户端与服务器之间移动数据同步的核心数据结构，包含了位置、速度、
 * 运输工具状态、跳跃信息等完整的移动上下文。
 *
 * 主要功能：
 * - 存储实体的当前位置和朝向
 * - 管理移动标志位（行走、奔跑、游泳、飞行等状态）
 * - 处理运输工具（船只、电梯等）相关的移动信息
 * - 记录跳跃和下落的物理参数
 * - 支持样条插值移动的高度信息
 *
 * @note 该结构体在网络包传输中使用，需要注意数据对齐和字节序
 */

#ifndef MovementInfo_h__
#define MovementInfo_h__

#include "ObjectGuid.h"
#include "Position.h"

/**
 * @struct MovementInfo
 * @brief 移动信息结构体，用于存储实体的完整移动状态
 *
 * 该结构体封装了游戏中所有与移动相关的数据，包括基础位置信息、
 * 移动标志、运输工具状态、跳跃参数等。客户端每次移动时都会发送
 * 此结构体的数据，服务器验证后广播给周围玩家。
 *
 * 数据结构设计考虑了网络传输效率，分为以下几个逻辑部分：
 * - 基础信息：GUID、位置、时间戳、移动标志
 * - 运输工具信息：用于船只、飞行器等载具上的位置计算
 * - 运动状态：俯仰角、下落时间
 * - 跳跃信息：跳跃的初始速度和角度参数
 * - 样条信息：用于插值移动的高度修正
 *
 * @note 使用前应调用构造函数或手动初始化所有字段
 * @warning 修改此结构体需要同步更新网络包的序列化/反序列化代码
 */
struct MovementInfo
{
    /** @name 基础移动信息
     * 这些字段在所有移动包中都存在
     * @{
     */

    ObjectGuid guid;        ///< 移动实体的全局唯一标识符
    uint32 flags;           ///< 移动标志位，指示实体的移动状态（行走、奔跑、游泳、飞行等）
    uint16 flags2;          ///< 扩展移动标志位，存储额外的移动状态信息
    Position pos;           ///< 实体当前的位置坐标（x, y, z）和朝向
    uint32 time;            ///< 移动时间戳（客户端时间），用于计算移动延迟和时间同步
    /** @} */

    /**
     * @struct TransportInfo
     * @brief 运输工具信息，用于存储实体在载具上的位置状态
     *
     * 当玩家处于运输工具（如船只、飞艇、电梯、矿道地铁等）上时，
     * 需要记录相对于运输工具的本地坐标，以便正确计算实体的世界坐标。
     * 运输工具会改变实体的相对位置，因此需要单独追踪。
     */
    struct TransportInfo
    {
        /**
         * @brief 重置运输工具信息到默认状态
         *
         * 将所有字段清零或设置为初始值，用于实体离开运输工具时清理状态。
         * 该操作为 O(1) 时间复杂度。
         */
        void Reset()
        {
            guid.Clear();                           // 清空运输工具的 GUID
            pos.Relocate(0.0f, 0.0f, 0.0f, 0.0f);   // 重置相对位置到原点
            seat = -1;                              // 重置座位号为无效值（-1 表示无座位）
            time = 0;                               // 清零运输工具时间戳
            time2 = 0;                              // 清零备用时间戳
        }

        ObjectGuid guid;    ///< 运输工具的全局唯一标识符
        Position pos;       ///< 相对于运输工具的本地位置坐标和朝向
        int8 seat;          ///< 座位索引，-1 表示站立（无座位），>= 0 表示具体座位编号
        uint32 time;        ///< 运输工具的运动时间戳，用于插值计算
        uint32 time2;       ///< 备用时间戳，用于特定运输工具的时间同步

    } transport;            ///< 运输工具信息实例

    /** @name 运动状态信息
     * 用于游泳、飞行和下落等特殊移动模式
     * @{
     */

    float pitch;            ///< 俯仰角度（弧度），用于游泳和飞行时的仰角/俯角
    uint32 fallTime;        ///< 下落持续时间（毫秒），从开始下落时计时，用于计算下落伤害
    /** @} */

    /**
     * @struct JumpInfo
     * @brief 跳跃信息，存储玩家跳跃时的物理参数
     *
     * 该结构体记录跳跃的初始运动学参数，用于服务器验证跳跃轨迹的合法性，
     * 以及预测落点位置。客户端在发送跳跃移动包时会填充这些参数。
     *
     * 跳跃运动学：
     * - 垂直速度 (zspeed) 控制跳跃高度
     * - 水平速度 (xyspeed) 和角度决定水平位移
     * - sinAngle/cosAngle 存储跳跃方向的三角函数值，避免重复计算
     */
    struct JumpInfo
    {
        /**
         * @brief 重置跳跃信息到默认状态
         *
         * 将所有跳跃参数清零，用于非跳跃状态下清理数据。
         * 该操作为 O(1) 时间复杂度。
         */
        void Reset()
        {
            zspeed = sinAngle = cosAngle = xyspeed = 0.0f;  // 所有速度参数归零
        }

        float zspeed;       ///< 垂直方向初速度（Z轴），正值表示向上跳跃
        float sinAngle;     ///< 跳跃方向的正弦值，用于计算水平分量
        float cosAngle;     ///< 跳跃方向的余弦值，用于计算水平分量
        float xyspeed;      ///< 水平面上的初速度，结合角度计算 X/Y 方向的速度

    } jump;                 ///< 跳跃信息实例

    /** @name 样条移动信息
     * 用于插值移动的高度计算
     * @{
     */

    float splineElevation;  ///< 样条插值高度偏移量，用于平滑地形高度变化
    /** @} */

    /**
     * @brief 默认构造函数，初始化所有移动信息字段
     *
     * 将所有成员变量初始化为安全的默认值：
     * - GUID 清零
     * - 标志位清零
     * - 位置设置到原点
     * - 嵌套结构体调用各自的 Reset() 方法
     *
     * 该构造函数保证对象创建后处于有效状态，所有字段已初始化。
     *
     * @note 时间复杂度为 O(1)
     */
    MovementInfo() :
        guid(), flags(0), flags2(0), time(0), pitch(0.0f), fallTime(0), splineElevation(0.0f)
    {
        pos.Relocate(0.0f, 0.0f, 0.0f, 0.0f);   // 初始化位置到世界原点
        transport.Reset();                       // 重置运输工具信息
        jump.Reset();                            // 重置跳跃信息
    }

    /**
     * @brief 获取移动标志位
     * @return 当前移动标志位的值
     *
     * 返回完整的移动标志位字段，包含行走、奔跑、游泳、飞行等状态信息。
     *
     * @note 时间复杂度 O(1)，仅返回成员变量的值
     * @see PLAYER_FIELD_MOVE_FLAGS 中定义的各种移动标志常量
     */
    uint32 GetMovementFlags() const { return flags; }

    /**
     * @brief 设置移动标志位
     * @param flag 要设置的新标志位值（完全覆盖原有值）
     *
     * 该方法会完全替换当前的移动标志位，适用于需要重置所有移动状态的场景。
     *
     * @warning 该操作会清除所有原有标志，谨慎使用
     * @note 时间复杂度 O(1)
     */
    void SetMovementFlags(uint32 flag) { flags = flag; }

    /**
     * @brief 添加移动标志位
     * @param flag 要添加的标志位（可组合多个标志）
     *
     * 使用位或操作添加新的移动标志，不影响已设置的其他标志。
     * 这是修改移动状态的安全方式。
     *
     * @note 时间复杂度 O(1)
     * @code
     * // 同时添加游泳和潜水标志
     * movementInfo.AddMovementFlag(MOVEMENTFLAG_SWIMMING | MOVEMENTFLAG_DIVING);
     * @endcode
     */
    void AddMovementFlag(uint32 flag) { flags |= flag; }

    /**
     * @brief 移除移动标志位
     * @param flag 要移除的标志位（可组合多个标志）
     *
     * 使用位与操作移除指定的移动标志，不影响其他已设置的标志。
     * 这是清除特定移动状态的安全方式。
     *
     * @note 时间复杂度 O(1)
     * @code
     * // 移除飞行标志
     * movementInfo.RemoveMovementFlag(MOVEMENTFLAG_FLYING);
     * @endcode
     */
    void RemoveMovementFlag(uint32 flag) { flags &= ~flag; }

    /**
     * @brief 检查是否具有指定的移动标志位
     * @param flag 要检查的标志位（可组合多个标志）
     * @return 如果任意一个指定的标志位被设置则返回 true，否则返回 false
     *
     * 使用位与操作检查指定的移动标志是否存在。
     *
     * @note 时间复杂度 O(1)
     * @code
     * // 检查是否在游泳
     * if (movementInfo.HasMovementFlag(MOVEMENTFLAG_SWIMMING))
     * {
     *     // 处理游泳状态
     * }
     * @endcode
     */
    bool HasMovementFlag(uint32 flag) const { return (flags & flag) != 0; }

    /**
     * @brief 获取扩展移动标志位
     * @return 当前扩展移动标志位的值
     *
     * 扩展标志位用于存储基础标志位无法容纳的额外移动状态信息。
     *
     * @note 时间复杂度 O(1)
     * @see PLAYER_FIELD_MOVE_EXTRA_FLAGS 中定义的各种扩展移动标志常量
     */
    uint16 GetExtraMovementFlags() const { return flags2; }

    /**
     * @brief 设置扩展移动标志位
     * @param flag 要设置的新扩展标志位值（完全覆盖原有值）
     *
     * 该方法会完全替换当前的扩展移动标志位。
     *
     * @warning 该操作会清除所有原有扩展标志，谨慎使用
     * @note 时间复杂度 O(1)
     */
    void SetExtraMovementFlags(uint16 flag) { flags2 = flag; }

    /**
     * @brief 添加扩展移动标志位
     * @param flag 要添加的扩展标志位（可组合多个标志）
     *
     * 使用位或操作添加新的扩展移动标志，不影响已设置的其他扩展标志。
     *
     * @note 时间复杂度 O(1)
     */
    void AddExtraMovementFlag(uint16 flag) { flags2 |= flag; }

    /**
     * @brief 移除扩展移动标志位
     * @param flag 要移除的扩展标志位（可组合多个标志）
     *
     * 使用位与操作移除指定的扩展移动标志，不影响其他已设置的扩展标志。
     *
     * @note 时间复杂度 O(1)
     */
    void RemoveExtraMovementFlag(uint16 flag) { flags2 &= ~flag; }

    /**
     * @brief 检查是否具有指定的扩展移动标志位
     * @param flag 要检查的扩展标志位（可组合多个标志）
     * @return 如果任意一个指定的扩展标志位被设置则返回 true，否则返回 false
     *
     * 使用位与操作检查指定的扩展移动标志是否存在。
     *
     * @note 时间复杂度 O(1)
     */
    bool HasExtraMovementFlag(uint16 flag) const { return (flags2 & flag) != 0; }

    /**
     * @brief 设置下落时间
     * @param val 下落时间值（毫秒）
     *
     * 更新实体的下落持续时间，用于计算下落伤害。
     * 通常在实体开始下落时设置为 0，然后由客户端递增。
     *
     * @note 时间复杂度 O(1)
     * @warning 下落时间超过阈值会触发下落伤害计算
     */
    void SetFallTime(uint32 val) { fallTime = val; }

    /**
     * @brief 输出调试信息到日志
     *
     * 将当前 MovementInfo 的所有字段值格式化输出到日志系统，
     * 用于调试移动相关的问题。输出内容包括：
     * - GUID 和位置信息
     * - 移动标志位状态
     * - 运输工具信息（如果有）
     * - 跳跃信息（如果有）
     *
     * 该方法仅在调试构建中启用，不应在生产环境中频繁调用。
     *
     * @note 仅在 Debug 构建中有效，Release 构建中可能被优化为空函数
     * @warning 频繁调用会影响性能，仅用于临时调试
     */
    void OutDebug();
};

#endif // MovementInfo_h__
