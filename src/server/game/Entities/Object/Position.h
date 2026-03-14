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
 * @file Position.h
 * @brief 位置坐标系统模块
 *
 * 本文件定义了游戏中所有对象的位置坐标系统，包括：
 * - Position: 基础坐标结构，表示游戏世界中的三维位置和朝向
 * - WorldLocation: 扩展位置结构，在 Position 基础上增加地图 ID 信息
 * - TaggedPosition: 带标签的位置模板，用于网络数据包的序列化控制
 *
 * 位置系统是游戏引擎的核心基础，所有游戏对象（玩家、NPC、物品等）
 * 都依赖此系统进行空间定位、距离计算、方向判断等操作。
 */

#ifndef Trinity_game_Position_h__
#define Trinity_game_Position_h__

#include "Define.h"
#include <string>
#include <cmath>

class ByteBuffer;

/**
 * @struct Position
 * @brief 游戏对象位置坐标结构体
 *
 * Position 是游戏中所有空间坐标的基础数据结构，用于表示游戏世界中的三维位置和朝向。
 * 该结构体被广泛应用于玩家、生物、游戏对象、物品等所有需要空间定位的实体。
 *
 * 核心功能：
 * - 坐标存储：X、Y、Z 三维坐标和朝向（Orientation）
 * - 距离计算：提供精确距离和平方距离计算，支持 2D/3D 模式
 * - 角度计算：计算相对角度、绝对角度，支持弧度转换
 * - 空间判断：判断点是否在指定范围、扇形区域、矩形区域内
 * - 坐标重定位：支持相对偏移和绝对重定位
 *
 * 坐标系说明：
 * - X 轴：东西方向（东为正）
 * - Y 轴：南北方向（北为正）
 * - Z 轴：垂直方向（上为正）
 * - Orientation：朝向角度，范围 [0, 2π)，以弧度表示，0 为正东方向
 *
 * 性能考虑：
 * - 距离计算时优先使用平方距离避免 sqrt 开销
 * - 朝向值在设置时自动归一化到 [0, 2π) 范围
 * - 所有距离和角度计算均为内联函数以提高性能
 *
 * @note 朝向字段 m_orientation 被限制为私有访问，确保值始终处于归一化范围
 */
struct TC_GAME_API Position
{
    /**
     * @brief 默认构造函数
     *
     * 初始化所有坐标为 0.0f，创建原点位置
     */
    Position()
        : m_positionX(0.0f), m_positionY(0.0f), m_positionZ(0.0f), m_orientation(0.0f) { }

    /**
     * @brief 带参数构造函数
     * @param x X 坐标（东西方向）
     * @param y Y 坐标（南北方向）
     * @param z Z 坐标（垂直高度），默认为 0.0f
     * @param o 朝向角度（弧度），默认为 0.0f，会自动归一化到 [0, 2π) 范围
     */
    Position(float x, float y, float z = 0.0f, float o = 0.0f)
        : m_positionX(x), m_positionY(y), m_positionZ(z), m_orientation(NormalizeOrientation(o)) { }

    // ==================== 流器标签和模板定义 ====================
    // 用于网络数据包序列化的类型标签系统

    /** @brief XY 坐标流器标签，仅序列化 X、Y 坐标 */
    struct XY;
    /** @brief XYZ 坐标流器标签，序列化 X、Y、Z 坐标 */
    struct XYZ;
    /** @brief XYZO 坐标流器标签，序列化 X、Y、Z 坐标和朝向 */
    struct XYZO;
    /** @brief PackedXYZ 坐标流器标签，使用压缩格式序列化 X、Y、Z 坐标 */
    struct PackedXYZ;

    /**
     * @brief 只读流器模板
     * @tparam Tag 流器标签类型，控制序列化的坐标分量
     *
     * 用于将 Position 对象以指定的坐标分量格式写入 ByteBuffer
     */
    template <class Tag>
    struct ConstStreamer
    {
        explicit ConstStreamer(Position const& pos) : Pos(&pos) { }
        Position const* Pos;  ///< 指向只读 Position 对象的指针
    };

    /**
     * @brief 可读写流器模板
     * @tparam Tag 流器标签类型，控制序列化的坐标分量
     *
     * 用于从 ByteBuffer 读取数据到 Position 对象，
     * 可隐式转换为对应的 ConstStreamer 以支持只读操作
     */
    template <class Tag>
    struct Streamer
    {
        explicit Streamer(Position& pos) : Pos(&pos) { }
        operator ConstStreamer<Tag>() const { return ConstStreamer<Tag>(*Pos); }
        Position* Pos;  ///< 指向可读写 Position 对象的指针
    };

    // ==================== 成员变量 ====================

    float m_positionX;  ///< X 坐标（东西方向，东为正方向）
    float m_positionY;  ///< Y 坐标（南北方向，北为正方向）
    float m_positionZ;  ///< Z 坐标（垂直高度，上为正方向）

    // Better to limit access to _orientation field, to guarantee the value is normalized
private:
    float m_orientation;  ///< 朝向角度（弧度），范围 [0, 2π)，0 为正东方向

public:
    // ==================== 比较操作符 ====================

    /**
     * @brief 相等比较操作符
     * @param a 要比较的位置对象
     * @return 如果所有坐标分量（X、Y、Z、朝向）都相等则返回 true
     *
     * 使用浮点数精确比较，注意浮点精度问题
     */
    bool operator==(Position const& a) const;

    // ==================== 坐标重定位函数 ====================

    /**
     * @brief 重定位到指定的 2D 坐标
     * @param x 新的 X 坐标
     * @param y 新的 Y 坐标
     *
     * 仅更新 X、Y 坐标，保持 Z 坐标和朝向不变
     * @note 高性能函数，无归一化处理
     */
    void Relocate(float x, float y) { m_positionX = x; m_positionY = y; }

    /**
     * @brief 重定位到指定的 3D 坐标
     * @param x 新的 X 坐标
     * @param y 新的 Y 坐标
     * @param z 新的 Z 坐标
     *
     * 更新 X、Y、Z 坐标，保持朝向不变
     * @note 高性能函数，无归一化处理
     */
    void Relocate(float x, float y, float z) { Relocate(x, y); m_positionZ = z; }

    /**
     * @brief 重定位到指定的完整坐标（包含朝向）
     * @param x 新的 X 坐标
     * @param y 新的 Y 坐标
     * @param z 新的 Z 坐标
     * @param o 新的朝向角度（弧度）
     *
     * 更新所有坐标分量，朝向会自动归一化到 [0, 2π) 范围
     * @note 朝向设置会触发归一化计算
     */
    void Relocate(float x, float y, float z, float o) { Relocate(x, y, z); SetOrientation(o); }

    /**
     * @brief 重定位到另一个位置对象的坐标
     * @param pos 源位置对象的常量引用
     *
     * 复制源位置的所有坐标分量（包括朝向）
     * @note 高性能函数，直接赋值，朝向已在源对象中归一化
     */
    void Relocate(Position const& pos) { *this = pos; }

    /**
     * @brief 重定位到另一个位置对象的坐标（指针版本）
     * @param pos 源位置对象的指针
     *
     * 复制源位置的所有坐标分量（包括朝向）
     * @pre pos 不应为 nullptr
     * @note 高性能函数，直接赋值，无空指针检查
     */
    void Relocate(Position const* pos) { *this = *pos; }

    /**
     * @brief 基于当前坐标应用相对偏移
     * @param offset 相对偏移量，X、Y、Z 分别表示各轴的偏移，朝向表示朝向变化
     *
     * 将当前位置坐标加上偏移量，得到新坐标。
     * 朝向偏移会累加到当前朝向上，并自动归一化。
     *
     * @note 常用于相对移动、位移技能等场景
     */
    void RelocateOffset(Position const& offset);

    /**
     * @brief 设置朝向角度
     * @param orientation 新的朝向角度（弧度）
     *
     * 朝向会自动归一化到 [0, 2π) 范围，
     * 确保朝向值始终有效，避免边界问题
     */
    void SetOrientation(float orientation)
    {
        m_orientation = NormalizeOrientation(orientation);
    }

    // ==================== 坐标获取函数 ====================

    /**
     * @brief 获取 X 坐标
     * @return X 坐标值（东西方向）
     */
    float GetPositionX() const { return m_positionX; }

    /**
     * @brief 获取 Y 坐标
     * @return Y 坐标值（南北方向）
     */
    float GetPositionY() const { return m_positionY; }

    /**
     * @brief 获取 Z 坐标
     * @return Z 坐标值（垂直高度）
     */
    float GetPositionZ() const { return m_positionZ; }

    /**
     * @brief 获取朝向角度
     * @return 朝向角度（弧度），范围 [0, 2π)
     */
    float GetOrientation() const { return m_orientation; }

    /**
     * @brief 获取 2D 坐标（引用参数版本）
     * @param[out] x 输出 X 坐标
     * @param[out] y 输出 Y 坐标
     */
    void GetPosition(float &x, float &y) const { x = m_positionX; y = m_positionY; }

    /**
     * @brief 获取 3D 坐标（引用参数版本）
     * @param[out] x 输出 X 坐标
     * @param[out] y 输出 Y 坐标
     * @param[out] z 输出 Z 坐标
     */
    void GetPosition(float &x, float &y, float &z) const { GetPosition(x, y); z = m_positionZ; }

    /**
     * @brief 获取完整坐标（引用参数版本）
     * @param[out] x 输出 X 坐标
     * @param[out] y 输出 Y 坐标
     * @param[out] z 输出 Z 坐标
     * @param[out] o 输出朝向角度
     */
    void GetPosition(float &x, float &y, float &z, float &o) const { GetPosition(x, y, z); o = m_orientation; }

    /**
     * @brief 获取位置对象的副本
     * @return 当前位置的完整副本
     */
    Position GetPosition() const { return *this; }

    // ==================== 流器访问函数 ====================
    // 这些函数用于网络数据包的序列化控制，通过不同的标签控制序列化的坐标分量

    /**
     * @brief 获取 XY 坐标流器（可读写）
     * @return 用于序列化 X、Y 坐标的流器对象
     * @note 用于网络数据包操作，仅处理 X、Y 两个坐标分量
     */
    Streamer<XY> PositionXYStream() { return Streamer<XY>(*this); }

    /**
     * @brief 获取 XY 坐标流器（只读）
     * @return 用于序列化 X、Y 坐标的只读流器对象
     * @note 用于网络数据包操作，仅处理 X、Y 两个坐标分量
     */
    ConstStreamer<XY> PositionXYStream() const { return ConstStreamer<XY>(*this); }

    /**
     * @brief 获取 XYZ 坐标流器（可读写）
     * @return 用于序列化 X、Y、Z 坐标的流器对象
     * @note 用于网络数据包操作，处理 X、Y、Z 三个坐标分量
     */
    Streamer<XYZ> PositionXYZStream() { return Streamer<XYZ>(*this); }

    /**
     * @brief 获取 XYZ 坐标流器（只读）
     * @return 用于序列化 X、Y、Z 坐标的只读流器对象
     * @note 用于网络数据包操作，处理 X、Y、Z 三个坐标分量
     */
    ConstStreamer<XYZ> PositionXYZStream() const { return ConstStreamer<XYZ>(*this); }

    /**
     * @brief 获取 XYZO 坐标流器（可读写）
     * @return 用于序列化 X、Y、Z 坐标和朝向的流器对象
     * @note 用于网络数据包操作，处理完整的坐标和朝向信息
     */
    Streamer<XYZO> PositionXYZOStream() { return Streamer<XYZO>(*this); }

    /**
     * @brief 获取 XYZO 坐标流器（只读）
     * @return 用于序列化 X、Y、Z 坐标和朝向的只读流器对象
     * @note 用于网络数据包操作，处理完整的坐标和朝向信息
     */
    ConstStreamer<XYZO> PositionXYZOStream() const { return ConstStreamer<XYZO>(*this); }

    /**
     * @brief 获取压缩 XYZ 坐标流器（可读写）
     * @return 用于压缩格式序列化 X、Y、Z 坐标的流器对象
     * @note 使用压缩格式以减少网络传输数据量，精度较低
     */
    Streamer<PackedXYZ> PositionPackedXYZStream() { return Streamer<PackedXYZ>(*this); }

    /**
     * @brief 获取压缩 XYZ 坐标流器（只读）
     * @return 用于压缩格式序列化 X、Y、Z 坐标的只读流器对象
     * @note 使用压缩格式以减少网络传输数据量，精度较低
     */
    ConstStreamer<PackedXYZ> PositionPackedXYZStream() const { return ConstStreamer<PackedXYZ>(*this); }

    // ==================== 坐标有效性验证 ====================

    /**
     * @brief 检查坐标是否有效
     * @return 如果坐标在合理范围内返回 true，否则返回 false
     *
     * 验证坐标值是否符合游戏世界的物理规则：
     * - 检查坐标是否为 NaN 或无穷大
     * - 检查坐标是否超出地图边界
     * - 检查 Z 坐标是否在合理的高度范围内
     *
     * @note 应在设置坐标后调用此函数进行验证，避免无效坐标导致游戏逻辑错误
     */
    bool IsPositionValid() const;

    // ==================== 2D 距离计算函数 ====================

    /**
     * @brief 计算 2D 平面距离的平方（坐标参数版本）
     * @param x 目标点的 X 坐标
     * @param y 目标点的 Y 坐标
     * @return 当前位置到目标点的 2D 距离平方
     *
     * 计算 XY 平面上的欧几里得距离平方：dx² + dy²
     * 不包含 Z 轴高度差，适用于忽略高度的平面距离判断
     *
     * @note 性能优化：避免 sqrt 开销，优先用于距离比较
     * @see GetExactDist2d() 需要实际距离时使用
     */
    float GetExactDist2dSq(const float x, const float y) const
    {
        float dx = x - m_positionX;
        float dy = y - m_positionY;
        return dx*dx + dy*dy;
    }

    /**
     * @brief 计算 2D 平面距离的平方（Position 引用版本）
     * @param pos 目标位置对象的常量引用
     * @return 当前位置到目标点的 2D 距离平方
     * @note 内联函数，高性能，避免 sqrt 开销
     */
    float GetExactDist2dSq(Position const& pos) const { return GetExactDist2dSq(pos.m_positionX, pos.m_positionY); }

    /**
     * @brief 计算 2D 平面距离的平方（Position 指针版本）
     * @param pos 目标位置对象的指针
     * @return 当前位置到目标点的 2D 距离平方
     * @pre pos 不应为 nullptr
     * @note 内联函数，高性能，避免 sqrt 开销
     */
    float GetExactDist2dSq(Position const* pos) const { return GetExactDist2dSq(*pos); }

    /**
     * @brief 计算 2D 平面实际距离（坐标参数版本）
     * @param x 目标点的 X 坐标
     * @param y 目标点的 Y 坐标
     * @return 当前位置到目标点的 2D 实际距离
     *
     * 计算 XY 平面上的欧几里得距离：sqrt(dx² + dy²)
     * 包含 sqrt 计算，仅在需要精确距离值时使用
     *
     * @note 性能提示：如果仅需比较距离，使用 GetExactDist2dSq() 更高效
     */
    float GetExactDist2d(const float x, const float y) const { return std::sqrt(GetExactDist2dSq(x, y)); }

    /**
     * @brief 计算 2D 平面实际距离（Position 引用版本）
     * @param pos 目标位置对象的常量引用
     * @return 当前位置到目标点的 2D 实际距离
     */
    float GetExactDist2d(Position const& pos) const { return GetExactDist2d(pos.m_positionX, pos.m_positionY); }

    /**
     * @brief 计算 2D 平面实际距离（Position 指针版本）
     * @param pos 目标位置对象的指针
     * @return 当前位置到目标点的 2D 实际距离
     * @pre pos 不应为 nullptr
     */
    float GetExactDist2d(Position const* pos) const { return GetExactDist2d(*pos); }

    // ==================== 3D 距离计算函数 ====================

    /**
     * @brief 计算 3D 空间距离的平方（坐标参数版本）
     * @param x 目标点的 X 坐标
     * @param y 目标点的 Y 坐标
     * @param z 目标点的 Z 坐标
     * @return 当前位置到目标点的 3D 距离平方
     *
     * 计算三维欧几里得距离平方：dx² + dy² + dz²
     * 包含高度差，适用于需要精确空间距离的场景
     *
     * @note 性能优化：避免 sqrt 开销，优先用于距离比较
     * @see GetExactDist() 需要实际距离时使用
     */
    float GetExactDistSq(float x, float y, float z) const
    {
        float dz = z - m_positionZ;
        return GetExactDist2dSq(x, y) + dz*dz;
    }

    /**
     * @brief 计算 3D 空间距离的平方（Position 引用版本）
     * @param pos 目标位置对象的常量引用
     * @return 当前位置到目标点的 3D 距离平方
     * @note 内联函数，高性能，避免 sqrt 开销
     */
    float GetExactDistSq(Position const& pos) const { return GetExactDistSq(pos.m_positionX, pos.m_positionY, pos.m_positionZ); }

    /**
     * @brief 计算 3D 空间距离的平方（Position 指针版本）
     * @param pos 目标位置对象的指针
     * @return 当前位置到目标点的 3D 距离平方
     * @pre pos 不应为 nullptr
     * @note 内联函数，高性能，避免 sqrt 开销
     */
    float GetExactDistSq(Position const* pos) const { return GetExactDistSq(*pos); }

    /**
     * @brief 计算 3D 空间实际距离（坐标参数版本）
     * @param x 目标点的 X 坐标
     * @param y 目标点的 Y 坐标
     * @param z 目标点的 Z 坐标
     * @return 当前位置到目标点的 3D 实际距离
     *
     * 计算三维欧几里得距离：sqrt(dx² + dy² + dz²)
     * 包含 sqrt 计算，仅在需要精确距离值时使用
     *
     * @note 性能提示：如果仅需比较距离，使用 GetExactDistSq() 更高效
     */
    float GetExactDist(float x, float y, float z) const { return std::sqrt(GetExactDistSq(x, y, z)); }

    /**
     * @brief 计算 3D 空间实际距离（Position 引用版本）
     * @param pos 目标位置对象的常量引用
     * @return 当前位置到目标点的 3D 实际距离
     */
    float GetExactDist(Position const& pos) const { return GetExactDist(pos.m_positionX, pos.m_positionY, pos.m_positionZ); }

    /**
     * @brief 计算 3D 空间实际距离（Position 指针版本）
     * @param pos 目标位置对象的指针
     * @return 当前位置到目标点的 3D 实际距离
     * @pre pos 不应为 nullptr
     */
    float GetExactDist(Position const* pos) const { return GetExactDist(*pos); }

    // ==================== 坐标偏移计算函数 ====================

    /**
     * @brief 计算从当前位置到目标位置的偏移量
     * @param endPos 目标终点位置
     * @param[out] retOffset 输出偏移量（终点坐标 - 当前坐标）
     *
     * 计算各坐标分量的差值，包括朝向差异。
     * 结果的朝向会被归一化到 [0, 2π) 范围。
     *
     * @note 常用于计算相对位移、移动向量等
     */
    void GetPositionOffsetTo(Position const & endPos, Position & retOffset) const;

    /**
     * @brief 应用偏移量获取新位置
     * @param offset 相对偏移量
     * @return 应用偏移后的新位置对象
     *
     * 返回一个新位置，其坐标为当前坐标加上偏移量。
     * 朝向偏移会累加并归一化。
     *
     * @note 不修改当前位置，返回新对象
     * @see RelocateOffset() 如果需要修改当前位置使用该函数
     */
    Position GetPositionWithOffset(Position const& offset) const;

    // ==================== 角度计算函数 ====================

    /**
     * @brief 计算到目标点的绝对角度（坐标参数版本）
     * @param x 目标点的 X 坐标
     * @param y 目标点的 Y 坐标
     * @return 从当前位置指向目标点的绝对角度（弧度），范围 [0, 2π)
     *
     * 使用 atan2 计算从当前位置指向目标点的角度，
     * 结果为世界坐标系中的绝对角度（相对于正东方向）。
     *
     * 计算公式：angle = atan2(dy, dx)
     * - dx = target.x - this.x
     * - dy = target.y - this.y
     * - 0 弧度指向正东方向
     * - π/2 弧度指向正北方向
     *
     * @note 结果已归一化，适用于导航、朝向计算等场景
     */
    float GetAbsoluteAngle(float x, float y) const
    {
        float dx = x - m_positionX;
        float dy = y - m_positionY;
        return NormalizeOrientation(std::atan2(dy, dx));
    }

    /**
     * @brief 计算到目标点的绝对角度（Position 引用版本）
     * @param pos 目标位置对象的常量引用
     * @return 从当前位置指向目标点的绝对角度（弧度），范围 [0, 2π)
     */
    float GetAbsoluteAngle(Position const& pos) const { return GetAbsoluteAngle(pos.m_positionX, pos.m_positionY); }

    /**
     * @brief 计算到目标点的绝对角度（Position 指针版本）
     * @param pos 目标位置对象的指针
     * @return 从当前位置指向目标点的绝对角度（弧度），范围 [0, 2π)
     * @pre pos 不应为 nullptr
     */
    float GetAbsoluteAngle(Position const* pos) const { return GetAbsoluteAngle(*pos); }

    /**
     * @brief 将相对角度转换为绝对角度
     * @param relAngle 相对于当前朝向的角度（弧度）
     * @return 绝对角度（弧度），范围 [0, 2π)
     *
     * 将基于当前朝向的相对角度转换为世界坐标系中的绝对角度。
     * 计算公式：absAngle = relAngle + m_orientation
     *
     * @note 结果已归一化到 [0, 2π) 范围
     */
    float ToAbsoluteAngle(float relAngle) const { return NormalizeOrientation(relAngle + m_orientation); }

    /**
     * @brief 将绝对角度转换为相对角度
     * @param absAngle 绝对角度（弧度）
     * @return 相对于当前朝向的相对角度（弧度），范围 [0, 2π)
     *
     * 将世界坐标系中的绝对角度转换为基于当前朝向的相对角度。
     * 计算公式：relAngle = absAngle - m_orientation
     *
     * @note 结果已归一化到 [0, 2π) 范围
     */
    float ToRelativeAngle(float absAngle) const { return NormalizeOrientation(absAngle - m_orientation); }

    /**
     * @brief 计算到目标点的相对角度（坐标参数版本）
     * @param x 目标点的 X 坐标
     * @param y 目标点的 Y 坐标
     * @return 相对于当前朝向的相对角度（弧度），范围 [0, 2π)
     *
     * 先计算绝对角度，再转换为相对于当前朝向的角度。
     * 0 表示目标在正前方，π/2 表示目标在左侧，π 表示目标在正后方。
     *
     * @note 常用于判断目标相对于自身的方位（前方、左侧、后方等）
     */
    float GetRelativeAngle(float x, float y) const { return ToRelativeAngle(GetAbsoluteAngle(x, y)); }

    /**
     * @brief 计算到目标点的相对角度（Position 引用版本）
     * @param pos 目标位置对象的常量引用
     * @return 相对于当前朝向的相对角度（弧度），范围 [0, 2π)
     */
    float GetRelativeAngle(Position const& pos) const { return ToRelativeAngle(GetAbsoluteAngle(pos)); }

    /**
     * @brief 计算到目标点的相对角度（Position 指针版本）
     * @param pos 目标位置对象的指针
     * @return 相对于当前朝向的相对角度（弧度），范围 [0, 2π)
     * @pre pos 不应为 nullptr
     */
    float GetRelativeAngle(Position const* pos) const { return ToRelativeAngle(GetAbsoluteAngle(pos)); }

    /**
     * @brief 计算到目标点的正弦和余弦值
     * @param x 目标点的 X 坐标
     * @param y 目标点的 Y 坐标
     * @param[out] vsin 输出正弦值（sin(angle)）
     * @param[out] vcos 输出余弦值（cos(angle)）
     *
     * 计算从当前位置指向目标点的角度的正弦和余弦值。
     * 可用于方向向量、移动计算等场景，避免重复三角函数计算。
     *
     * @note 性能优化：一次计算同时获取 sin 和 cos，减少函数调用开销
     */
    void GetSinCos(float x, float y, float &vsin, float &vcos) const;

    // ==================== 距离判断函数 ====================

    /**
     * @brief 判断目标点是否在指定的 2D 距离范围内（坐标参数版本）
     * @param x 目标点的 X 坐标
     * @param y 目标点的 Y 坐标
     * @param dist 距离阈值
     * @return 如果距离小于 dist 则返回 true，否则返回 false
     *
     * 使用平方距离比较，避免 sqrt 计算，高性能。
     * 仅考虑 XY 平面距离，忽略 Z 轴高度差。
     *
     * @note 性能优化：使用 GetExactDist2dSq() 避免开方运算
     */
    bool IsInDist2d(float x, float y, float dist) const { return GetExactDist2dSq(x, y) < dist * dist; }

    /**
     * @brief 判断目标点是否在指定的 2D 距离范围内（Position 指针版本）
     * @param pos 目标位置对象的指针
     * @param dist 距离阈值
     * @return 如果距离小于 dist 则返回 true，否则返回 false
     * @pre pos 不应为 nullptr
     */
    bool IsInDist2d(Position const* pos, float dist) const { return GetExactDist2dSq(pos) < dist * dist; }

    /**
     * @brief 判断目标点是否在指定的 3D 距离范围内（坐标参数版本）
     * @param x 目标点的 X 坐标
     * @param y 目标点的 Y 坐标
     * @param z 目标点的 Z 坐标
     * @param dist 距离阈值
     * @return 如果距离小于 dist 则返回 true，否则返回 false
     *
     * 使用平方距离比较，避免 sqrt 计算，高性能。
     * 考虑完整的 3D 空间距离，包含高度差。
     *
     * @note 性能优化：使用 GetExactDistSq() 避免开方运算
     */
    bool IsInDist(float x, float y, float z, float dist) const { return GetExactDistSq(x, y, z) < dist * dist; }

    /**
     * @brief 判断目标点是否在指定的 3D 距离范围内（Position 引用版本）
     * @param pos 目标位置对象的常量引用
     * @param dist 距离阈值
     * @return 如果距离小于 dist 则返回 true，否则返回 false
     */
    bool IsInDist(Position const& pos, float dist) const { return GetExactDistSq(pos) < dist * dist; }

    /**
     * @brief 判断目标点是否在指定的 3D 距离范围内（Position 指针版本）
     * @param pos 目标位置对象的指针
     * @param dist 距离阈值
     * @return 如果距离小于 dist 则返回 true，否则返回 false
     * @pre pos 不应为 nullptr
     */
    bool IsInDist(Position const* pos, float dist) const { return GetExactDistSq(pos) < dist * dist; }

    // ==================== 空间区域判断函数 ====================

    /**
     * @brief 判断当前位置是否在指定的矩形盒子内
     * @param center 盒子中心位置
     * @param xradius X 轴方向的半径（半宽度）
     * @param yradius Y 轴方向的半径（半深度）
     * @param zradius Z 轴方向的半径（半高度）
     * @return 如果当前位置在盒子内则返回 true，否则返回 false
     *
     * 判断逻辑：
     * - |dx| < xradius
     * - |dy| < yradius
     * - |dz| < zradius
     *
     * 盒子是一个轴对齐边界框（AABB），不受朝向影响。
     *
     * @note 常用于区域触发器、碰撞检测、技能范围判定等
     */
    bool IsWithinBox(Position const& center, float xradius, float yradius, float zradius) const;

    /**
     * @brief 判断当前位置是否在指定的双垂直圆柱体内
     * @param center 圆柱体中心位置
     * @param radius 圆柱体半径
     * @param height 圆柱体高度（从中心向上下的延伸距离）
     * @return 如果当前位置在圆柱体内则返回 true，否则返回 false
     *
     * 判断逻辑：
     * - dist2d < radius (XY 平面距离小于半径)
     * - |dz| < height (Z 轴高度差小于高度)
     *
     * 这是一个双垂直圆柱体，从中心向上下各延伸 height 单位。
     *
     * @note 常用于技能范围判定、视野检测、碰撞体积等
     */
    // dist2d < radius && abs(dz) < height
    bool IsWithinDoubleVerticalCylinder(Position const* center, float radius, float height) const;

    /**
     * @brief 判断目标位置是否在当前朝向的扇形区域内
     * @param arcangle 扇形角度（弧度），表示扇形的总张开角度
     * @param pos 目标位置对象的指针
     * @param border 边界容差值，默认 2.0f
     * @return 如果目标在扇形区域内则返回 true，否则返回 false
     *
     * 扇形判定逻辑：
     * - 计算目标相对于当前位置的相对角度
     * - 判断相对角度是否在 ±arcangle/2 范围内
     * - border 参数用于扩大判定范围，处理边界情况
     *
     * 典型应用：
     * - 前方锥形技能（如顺劈斩、正面吐息）
     * - 视野范围判定（如怪物的警戒视野）
     *
     * @pre pos 不应为 nullptr
     */
    bool HasInArc(float arcangle, Position const* pos, float border = 2.0f) const;

    /**
     * @brief 判断目标位置是否在一条线段上
     * @param pos 目标位置对象的指针
     * @param objSize 目标对象的尺寸（半径）
     * @param width 线条的宽度
     * @return 如果目标在线段上则返回 true，否则返回 false
     *
     * 判断逻辑：
     * - 目标位置在当前朝向方向上
     * - 目标中心到线条的距离小于 (objSize + width/2)
     * - 目标在线条延伸方向上
     *
     * 常用于：
     * - 直线技能判定（如冲锋、直线射击）
     * - 狭长区域碰撞检测
     *
     * @pre pos 不应为 nullptr
     */
    bool HasInLine(Position const* pos, float objSize, float width) const;

    /**
     * @brief 将位置信息转换为字符串
     * @return 格式化的位置字符串，包含 X、Y、Z 坐标和朝向信息
     *
     * 输出格式通常为："X: xxx Y: xxx Z: xxx O: xxx"
     *
     * @note 主要用于调试日志输出、错误信息显示等
     */
    std::string ToString() const;

    // ==================== 静态工具函数 ====================

    /**
     * @brief 将任意弧度角度归一化到 [0, 2π) 范围
     * @param o 输入的弧度角度，可以是任意值（正数、负数或超出范围）
     * @return 归一化后的弧度角度，范围 [0, 2π)
     *
     * 归一化算法：
     * - 对于负角度：循环加 2π 直到为正
     * - 对于过大角度：循环减 2π 直到在范围内
     *
     * 用途：
     * - 确保朝向值始终有效
     * - 角度计算结果的标准化
     * - 避免角度溢出问题
     *
     * @note 静态函数，可直接调用：Position::NormalizeOrientation(angle)
     * @warning 输入为 NaN 或无穷大时，结果未定义
     */
    // constrain arbitrary radian orientation to interval [0,2*PI)
    static float NormalizeOrientation(float o);
};

/**
 * @def MAPID_INVALID
 * @brief 无效地图 ID 常量
 *
 * 用于表示未初始化或无效的地图 ID，值为 0xFFFFFFFF（uint32 最大值）
 */
#define MAPID_INVALID 0xFFFFFFFF

/**
 * @class WorldLocation
 * @brief 世界位置类，扩展 Position 增加地图 ID 信息
 *
 * WorldLocation 继承自 Position，在三维坐标和朝向的基础上增加地图 ID 字段，
 * 用于表示跨地图的完整位置信息。这是游戏中需要记录完整位置信息时的标准数据结构。
 *
 * 核心功能：
 * - 继承 Position 的所有坐标操作能力
 * - 增加地图 ID 标识，支持跨地图位置记录
 * - 提供完整的位置重定位和获取接口
 *
 * 典型应用场景：
 * - 玩家的传送位置记录（炉石绑定点、副本入口等）
 * - 物品的落点位置（跨地图邮件、拍卖行物品等）
 * - 任务目标的完整位置（跨地图任务标记）
 * - 召唤、复活等需要跨地图传送的场景
 *
 * 地图 ID 说明：
 * - 0: 东部王国（Eastern Kingdoms）
 * - 1: 卡利姆多（Kalimdor）
 * - 530: 外域（Outland）
 * - 571: 诺森德（Northrend）
 * - 其他 ID 对应副本、战场等特殊地图
 *
 * @see Position 基础位置结构
 */
class WorldLocation : public Position
{
    public:
        /**
         * @brief 默认构造函数
         *
         * 初始化地图 ID 为 MAPID_INVALID（无效值），坐标初始化为原点。
         * 用于创建待初始化的世界位置对象。
         */
        explicit WorldLocation()
            : m_mapId(MAPID_INVALID) { }

        /**
         * @brief 完整参数构造函数
         * @param _mapId 地图 ID
         * @param x X 坐标（东西方向）
         * @param y Y 坐标（南北方向）
         * @param z Z 坐标（垂直高度），默认为 0.0f
         * @param o 朝向角度（弧度），默认为 0.0f
         *
         * 创建包含完整地图和坐标信息的世界位置对象。
         */
        explicit WorldLocation(uint32 _mapId, float x, float y, float z = 0.0f, float o = 0.0f)
            : Position(x, y, z, o), m_mapId(_mapId) { }

        /**
         * @brief 从地图 ID 和 Position 构造
         * @param mapId 地图 ID
         * @param position 位置对象的常量引用
         *
         * 将已有的 Position 对象扩展为 WorldLocation，添加地图 ID 信息。
         */
        WorldLocation(uint32 mapId, Position const& position)
            : Position(position), m_mapId(mapId) { }

        // ==================== 世界位置重定位函数 ====================

        /**
         * @brief 重定位到另一个 WorldLocation 的完整位置（引用版本）
         * @param loc 源世界位置对象的常量引用
         *
         * 复制地图 ID 和所有坐标分量（X、Y、Z、朝向）
         */
        void WorldRelocate(WorldLocation const& loc) { m_mapId = loc.GetMapId(); Relocate(loc); }

        /**
         * @brief 重定位到另一个 WorldLocation 的完整位置（指针版本）
         * @param loc 源世界位置对象的指针
         *
         * 复制地图 ID 和所有坐标分量
         * @pre loc 不应为 nullptr
         */
        void WorldRelocate(WorldLocation const* loc) { m_mapId = loc->GetMapId(); Relocate(loc); }

        /**
         * @brief 重定位到指定地图的指定位置
         * @param mapId 目标地图 ID
         * @param pos 目标位置对象的常量引用
         *
         * 设置新的地图 ID 并复制坐标信息
         */
        void WorldRelocate(uint32 mapId, Position const& pos) { m_mapId = mapId; Relocate(pos); }

        /**
         * @brief 重定位到指定的完整世界位置（参数版本）
         * @param mapId 目标地图 ID，默认为 MAPID_INVALID
         * @param x 目标 X 坐标，默认为 0.0f
         * @param y 目标 Y 坐标，默认为 0.0f
         * @param z 目标 Z 坐标，默认为 0.0f
         * @param o 目标朝向角度，默认为 0.0f
         *
         * 设置完整的地图和坐标信息，朝向会自动归一化。
         */
        void WorldRelocate(uint32 mapId = MAPID_INVALID, float x = 0.f, float y = 0.f, float z = 0.f, float o = 0.f)
        {
            m_mapId = mapId;
            Relocate(x, y, z, o);
        }

        // ==================== 世界位置获取函数 ====================

        /**
         * @brief 获取当前世界位置的副本
         * @return 包含地图 ID 和坐标的完整 WorldLocation 对象副本
         *
         * @note 返回对象副本，不影响当前对象
         */
        WorldLocation GetWorldLocation() const
        {
            return *this;
        }

        /**
         * @brief 获取地图 ID
         * @return 当前位置的地图 ID
         */
        uint32 GetMapId() const { return m_mapId; }

        // ==================== 成员变量 ====================

        uint32 m_mapId;  ///< 地图 ID，标识当前所在的地图实例

        // ==================== 调试函数 ====================

        /**
         * @brief 获取调试信息字符串
         * @return 格式化的调试信息，包含地图 ID、坐标和朝向
         *
         * 输出格式通常为："MapID: xxx X: xxx Y: xxx Z: xxx O: xxx"
         *
         * @note 主要用于调试日志、错误信息显示等场景
         */
        std::string GetDebugInfo() const;
};

// ==================== ByteBuffer 序列化操作符声明 ====================

/**
 * @brief ByteBuffer 输出操作符：将 Position 的 XY 坐标写入缓冲区
 * @param buf ByteBuffer 缓冲区引用
 * @param streamer 包含 Position 对象的只读流器
 * @return ByteBuffer 引用，支持链式调用
 */
TC_GAME_API ByteBuffer& operator<<(ByteBuffer& buf, Position::ConstStreamer<Position::XY> const& streamer);

/**
 * @brief ByteBuffer 输入操作符：从缓冲区读取 XY 坐标到 Position
 * @param buf ByteBuffer 缓冲区引用
 * @param streamer 包含 Position 对象的可读写流器
 * @return ByteBuffer 引用，支持链式调用
 */
TC_GAME_API ByteBuffer& operator>>(ByteBuffer& buf, Position::Streamer<Position::XY> const& streamer);

/**
 * @brief ByteBuffer 输出操作符：将 Position 的 XYZ 坐标写入缓冲区
 * @param buf ByteBuffer 缓冲区引用
 * @param streamer 包含 Position 对象的只读流器
 * @return ByteBuffer 引用，支持链式调用
 */
TC_GAME_API ByteBuffer& operator<<(ByteBuffer& buf, Position::ConstStreamer<Position::XYZ> const& streamer);

/**
 * @brief ByteBuffer 输入操作符：从缓冲区读取 XYZ 坐标到 Position
 * @param buf ByteBuffer 缓冲区引用
 * @param streamer 包含 Position 对象的可读写流器
 * @return ByteBuffer 引用，支持链式调用
 */
TC_GAME_API ByteBuffer& operator>>(ByteBuffer& buf, Position::Streamer<Position::XYZ> const& streamer);

/**
 * @brief ByteBuffer 输出操作符：将 Position 的 XYZ 坐标和朝向写入缓冲区
 * @param buf ByteBuffer 缓冲区引用
 * @param streamer 包含 Position 对象的只读流器
 * @return ByteBuffer 引用，支持链式调用
 */
TC_GAME_API ByteBuffer& operator<<(ByteBuffer& buf, Position::ConstStreamer<Position::XYZO> const& streamer);

/**
 * @brief ByteBuffer 输入操作符：从缓冲区读取 XYZ 坐标和朝向到 Position
 * @param buf ByteBuffer 缓冲区引用
 * @param streamer 包含 Position 对象的可读写流器
 * @return ByteBuffer 引用，支持链式调用
 */
TC_GAME_API ByteBuffer& operator>>(ByteBuffer& buf, Position::Streamer<Position::XYZO> const& streamer);

/**
 * @brief ByteBuffer 输出操作符：将 Position 的压缩 XYZ 坐标写入缓冲区
 * @param buf ByteBuffer 缓冲区引用
 * @param streamer 包含 Position 对象的只读流器
 * @return ByteBuffer 引用，支持链式调用
 * @note 使用压缩格式，减少网络传输数据量，但精度较低
 */
TC_GAME_API ByteBuffer& operator<<(ByteBuffer& buf, Position::ConstStreamer<Position::PackedXYZ> const& streamer);

/**
 * @struct TaggedPosition
 * @brief 带标签的位置模板，用于控制网络序列化的坐标分量
 * @tparam Tag 流器标签类型（XY、XYZ、XYZO、PackedXYZ）
 *
 * TaggedPosition 是一个模板包装器，将 Position 对象与序列化标签绑定，
 * 使得在 ByteBuffer 操作时自动应用正确的序列化格式。
 *
 * 设计目的：
 * - 提供类型安全的序列化控制
 * - 避免手动调用 PositionXXXStream() 的繁琐
 * - 支持隐式转换，使用便捷
 *
 * 使用示例：
 * @code
 * TaggedPosition<Position::XYZ> taggedPos(1.0f, 2.0f, 3.0f);
 * ByteBuffer buf;
 * buf << taggedPos;  // 自动使用 XYZ 格式序列化
 * @endcode
 */
template <class Tag>
struct TaggedPosition
{
    /**
     * @brief 默认构造函数
     *
     * 创建一个初始化为原点的位置
     */
    TaggedPosition() { }

    /**
     * @brief 带参数构造函数
     * @param x X 坐标
     * @param y Y 坐标
     * @param z Z 坐标，默认为 0.0f
     * @param o 朝向角度，默认为 0.0f
     */
    TaggedPosition(float x, float y, float z = 0.0f, float o = 0.0f) : Pos(x, y, z, o) { }

    /**
     * @brief 从 Position 构造
     * @param pos 源位置对象的常量引用
     */
    TaggedPosition(Position const& pos) : Pos(pos) { }

    /**
     * @brief 赋值操作符
     * @param pos 源位置对象的常量引用
     * @return 当前对象的引用，支持链式赋值
     *
     * 将 Position 对象的坐标复制到内部的 Pos 成员
     */
    TaggedPosition& operator=(Position const& pos)
    {
        Pos.Relocate(pos);
        return *this;
    }

    /**
     * @brief 隐式转换为 Position
     * @return 内部 Position 对象的副本
     *
     * 允许 TaggedPosition 在需要 Position 的地方自动转换
     */
    operator Position() const { return Pos; }

    /**
     * @brief ByteBuffer 输出操作符（友元函数）
     * @param buf ByteBuffer 缓冲区引用
     * @param tagged TaggedPosition 对象的常量引用
     * @return ByteBuffer 引用，支持链式调用
     *
     * 使用标签类型对应的序列化格式将位置数据写入缓冲区
     */
    friend ByteBuffer& operator<<(ByteBuffer& buf, TaggedPosition const& tagged) { return buf << Position::ConstStreamer<Tag>(tagged.Pos); }

    /**
     * @brief ByteBuffer 输入操作符（友元函数）
     * @param buf ByteBuffer 缓冲区引用
     * @param tagged TaggedPosition 对象的引用
     * @return ByteBuffer 引用，支持链式调用
     *
     * 使用标签类型对应的序列化格式从缓冲区读取位置数据
     */
    friend ByteBuffer& operator>>(ByteBuffer& buf, TaggedPosition& tagged) { return buf >> Position::Streamer<Tag>(tagged.Pos); }

    Position Pos;  ///< 内部存储的位置对象
};

#endif // Trinity_game_Position_h__
