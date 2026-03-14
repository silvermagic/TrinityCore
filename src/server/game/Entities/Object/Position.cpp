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
 * @file Position.cpp
 * @brief 位置和坐标系统的实现文件
 *
 * 本文件实现了游戏世界中位置管理的核心功能，包括：
 * - 三维坐标（X, Y, Z）和朝向（Orientation）的基本运算
 * - 位置之间的相对关系计算（距离、角度、方向等）
 * - 几何形状判定（矩形区域、圆柱区域、扇形区域等）
 * - 坐标系的旋转变换和偏移计算
 * - 网络传输数据的序列化/反序列化
 *
 * Position 类是游戏世界中所有具有位置属性的实体的基础类，
 * 为移动、战斗、技能释放、碰撞检测等系统提供坐标支持。
 */

#include "Position.h"
#include "ByteBuffer.h"
#include "DBCStores.h"
#include "GridDefines.h"
#include "Random.h"
#include "World.h"

#include <G3D/g3dmath.h>
#include <sstream>

/**
 * @brief 位置相等比较运算符
 *
 * 使用模糊相等比较来处理浮点数精度问题
 *
 * @param a 要比较的另一个位置对象
 * @return 如果两个位置在浮点误差范围内相等则返回 true，否则返回 false
 *
 * @note 使用 G3D::fuzzyEq 进行浮点数比较，避免精度误差导致的判断错误
 * @note 性能影响：轻量级操作，调用 4 次 fuzzyEq 比较
 */
bool Position::operator==(Position const& a) const
{
    return (G3D::fuzzyEq(a.m_positionX, m_positionX) &&
        G3D::fuzzyEq(a.m_positionY, m_positionY) &&
        G3D::fuzzyEq(a.m_positionZ, m_positionZ) &&
        G3D::fuzzyEq(a.m_orientation, m_orientation));
}

/**
 * @brief 根据偏移量重新定位位置（考虑当前朝向的旋转变换）
 *
 * 将局部坐标系中的偏移量转换到世界坐标系，并应用到当前位置。
 * 这是一个坐标系变换操作，考虑了当前位置的朝向角度。
 *
 * @param offset 局部坐标系中的偏移量（相对于当前朝向的偏移）
 *
 * @note 变换公式基于二维旋转矩阵：
 *       - 新X坐标 = 原X + (偏移X * cos(朝向) + 偏移Y * sin(朝向 + π))
 *       - 新Y坐标 = 原Y + (偏移Y * cos(朝向) + 偏移X * sin(朝向))
 *       - 新朝向 = 原朝向 + 偏移朝向
 * @note 使用场景：召唤宠物、生成战利品、施放技能时的位置计算
 * @note 性能影响：中等，包含 4 次三角函数计算
 */
void Position::RelocateOffset(Position const& offset)
{
    // 应用旋转变换：将局部坐标偏移转换为世界坐标偏移
    // 注意：偏移X分量使用 sin(朝向+π)，偏移Y分量使用 sin(朝向)
    m_positionX = GetPositionX() + (offset.GetPositionX() * std::cos(GetOrientation()) + offset.GetPositionY() * std::sin(GetOrientation() + float(M_PI)));
    m_positionY = GetPositionY() + (offset.GetPositionY() * std::cos(GetOrientation()) + offset.GetPositionX() * std::sin(GetOrientation()));
    m_positionZ = GetPositionZ() + offset.GetPositionZ();
    SetOrientation(GetOrientation() + offset.GetOrientation());
}

/**
 * @brief 检查当前位置是否为有效的地图坐标
 *
 * 验证坐标值是否在合法范围内，包括：
 * - 坐标值不为 NaN 或无穷大
 * - 坐标值在地图边界范围内
 * - 朝向值在有效范围内
 *
 * @return 如果坐标有效返回 true，否则返回 false
 *
 * @note 调用时机：在设置新位置、传送、生成对象时进行验证
 * @note 性能影响：轻量级，主要是边界检查
 */
bool Position::IsPositionValid() const
{
    return Trinity::IsValidMapCoord(m_positionX, m_positionY, m_positionZ, m_orientation);
}

/**
 * @brief 计算从当前位置到目标位置的相对偏移量（反向坐标变换）
 *
 * 将世界坐标系中的目标位置转换为相对于当前位置朝向的局部坐标偏移。
 * 这是 RelocateOffset 的逆操作。
 *
 * @param endPos 目标位置（世界坐标系）
 * @param retOffset [out] 输出的相对偏移量（局部坐标系）
 *
 * @note 变换公式（逆旋转矩阵）：
 *       - 偏移X = ΔX * cos(朝向) + ΔY * sin(朝向)
 *       - 偏移Y = ΔY * cos(朝向) - ΔX * sin(朝向)
 * @note 使用场景：计算相对位置、判断目标是否在前方/后方
 * @note 性能影响：中等，包含 2 次三角函数计算
 */
void Position::GetPositionOffsetTo(Position const& endPos, Position& retOffset) const
{
    // 计算世界坐标系中的位移向量
    float dx = endPos.GetPositionX() - GetPositionX();
    float dy = endPos.GetPositionY() - GetPositionY();

    // 应用逆旋转变换：将世界坐标位移转换为局部坐标偏移
    retOffset.m_positionX = dx * std::cos(GetOrientation()) + dy * std::sin(GetOrientation());
    retOffset.m_positionY = dy * std::cos(GetOrientation()) - dx * std::sin(GetOrientation());
    retOffset.m_positionZ = endPos.GetPositionZ() - GetPositionZ();
    retOffset.SetOrientation(endPos.GetOrientation() - GetOrientation());
}

/**
 * @brief 获取应用偏移量后的新位置（不修改当前位置）
 *
 * 创建当前位置的副本，应用偏移量后返回新位置对象。
 *
 * @param offset 要应用的偏移量
 * @return 应用偏移后的新位置对象
 *
 * @note 使用场景：预计算位置、移动路径规划
 * @note 性能影响：中等，包含对象拷贝和旋转变换
 */
Position Position::GetPositionWithOffset(Position const& offset) const
{
    Position ret(*this);
    ret.RelocateOffset(offset);
    return ret;
}

/**
 * @brief 计算从指定点到当前位置的方向向量的正弦和余弦值
 *
 * 根据从参考点到当前位置的方向计算单位向量的正弦和余弦分量。
 * 当两点几乎重合时，返回随机方向以避免除零错误。
 *
 * @param x 参考点的 X 坐标
 * @param y 参考点的 Y 坐标
 * @param vsin [out] 输出方向向量的正弦值（Y 分量）
 * @param vcos [out] 输出方向向量的余弦值（X 分量）
 *
 * @note 数学原理：
 *       - sin(θ) = ΔY / 距离 = 方向向量的 Y 分量
 *       - cos(θ) = ΔX / 距离 = 方向向量的 X 分量
 * @note 特殊处理：当距离小于 0.001 时使用随机方向，避免数值不稳定
 * @note 使用场景：面向目标、计算相对方向、投射物飞行方向
 * @note 性能影响：中等，包含平方根计算和可能的三角函数计算
 */
void Position::GetSinCos(const float x, const float y, float &vsin, float &vcos) const
{
    // 计算位移向量
    float dx = GetPositionX() - x;
    float dy = GetPositionY() - y;

    // 如果两点几乎重合（距离 < 0.001），使用随机方向避免除零和数值不稳定
    if (std::fabs(dx) < 0.001f && std::fabs(dy) < 0.001f)
    {
        // 生成随机角度 [0, 2π)
        float angle = (float)rand_norm()*static_cast<float>(2 * M_PI);
        vcos = std::cos(angle);
        vsin = std::sin(angle);
    }
    else
    {
        // 计算单位方向向量的分量
        float dist = std::sqrt((dx*dx) + (dy*dy));
        vcos = dx / dist;  // cos(θ) = ΔX / 距离
        vsin = dy / dist;  // sin(θ) = ΔY / 距离
    }
}

/**
 * @brief 判断当前位置是否在指定的旋转矩形盒区域内
 *
 * 检测当前位置是否位于以 center 为中心、具有指定半径和朝向的三维矩形盒内。
 * 矩形盒可以朝向任意方向，通过旋转变换实现判定。
 *
 * @param center 矩形盒的中心位置（包含朝向信息）
 * @param xradius X 轴方向的半径（半宽）
 * @param yradius Y 轴方向的半径（半长）
 * @param zradius Z 轴方向的半径（半高）
 * @return 如果当前位置在矩形盒内返回 true，否则返回 false
 *
 * @note 算法原理：
 *       1. 将当前位置坐标旋转到以中心点朝向为基准的局部坐标系
 *       2. 在局部坐标系中，矩形盒的边与坐标轴平行，可简化为独立的轴向判断
 *       3. 分别检查 X、Y、Z 三个方向是否在边界内
 * @note 游戏内朝向为逆时针方向，因此旋转角度为 2π - center.朝向
 * @note 使用场景：AOE 技能判定、区域触发器、战斗区域限制
 * @note 性能影响：中等，包含 2 次三角函数计算
 */
bool Position::IsWithinBox(Position const& center, float xradius, float yradius, float zradius) const
{
    // 旋转当前位置坐标而非旋转整个矩形盒，这样可以简化计算：
    // 只需计算一个点的旋转，而非计算矩形盒的四个角点

    // 游戏内朝向为逆时针方向（2π = 360°）
    // 计算旋转角度：将世界坐标旋转到局部坐标系
    double rotation = 2 * M_PI - center.GetOrientation();
    double sinVal = std::sin(rotation);
    double cosVal = std::cos(rotation);

    // 计算当前位置相对于中心点的位移
    float BoxDistX = GetPositionX() - center.GetPositionX();
    float BoxDistY = GetPositionY() - center.GetPositionY();

    // 应用旋转变换，得到局部坐标系中的位置
    float rotX = float(center.GetPositionX() + BoxDistX * cosVal - BoxDistY*sinVal);
    float rotY = float(center.GetPositionY() + BoxDistY * cosVal + BoxDistX*sinVal);

    // 在局部坐标系中，矩形盒的边与坐标轴平行，可以独立检查每个维度
    float dz = GetPositionZ() - center.GetPositionZ();
    float dx = rotX - center.GetPositionX();
    float dy = rotY - center.GetPositionY();

    // 检查是否在矩形盒边界内
    if ((std::fabs(dx) > xradius) ||
        (std::fabs(dy) > yradius) ||
        (std::fabs(dz) > zradius))
        return false;

    return true;
}

/**
 * @brief 判断当前位置是否在指定的双垂直圆柱区域内
 *
 * 检测当前位置是否位于以 center 为中心、指定半径和高度的垂直圆柱体内。
 * 圆柱体的轴向与 Z 轴平行。
 *
 * @param center 圆柱体的中心位置
 * @param radius 圆柱体的半径
 * @param height 圆柱体的半高度（从中心向上和向下各延伸 height 单位）
 * @return 如果当前位置在圆柱体内返回 true，否则返回 false
 *
 * @note 判定条件：
 *       1. 水平距离（2D 距离）<= radius
 *       2. 垂直距离的绝对值 <= height
 * @note 使用场景：圆柱形 AOE 技能、采集物距离判定、NPC 交互范围
 * @note 性能影响：轻量级，包含一次 2D 距离计算
 */
bool Position::IsWithinDoubleVerticalCylinder(Position const* center, float radius, float height) const
{
    // 计算垂直方向的距离差
    float verticalDelta = GetPositionZ() - center->GetPositionZ();

    // 同时满足水平距离和垂直距离的约束
    return IsInDist2d(center, radius) && std::abs(verticalDelta) <= height;
}

/**
 * @brief 判断目标对象是否在当前实体的扇形视野范围内
 *
 * 检测目标对象相对于当前朝向是否在指定的扇形角度内。
 * 扇形以当前实体为中心，当前朝向为中轴线。
 *
 * @param arc 扇形的角度范围（弧度），取值范围 [0, 2π]
 * @param obj 要检测的目标对象
 * @param border 角度缩放因子，用于调整扇形的实际宽度（通常为 2.0）
 * @return 如果目标在扇形范围内返回 true，否则返回 false
 *
 * @note 算法原理：
 *       1. 特殊情况：目标是自己时始终返回 true
 *       2. 将扇形角度归一化到 [0, 2π] 范围
 *       3. 计算目标相对角度，转换到 [-π, +π] 范围
 *       4. 判断相对角度是否在 [-arc/border, +arc/border] 范围内
 * @note border 参数说明：
 *       - border = 2.0：标准用法，检测对象是否在扇形中心线左右各 arc/2 的范围内
 *       - border = 1.0：检测对象是否在 [-arc, +arc] 范围内
 * @note 使用场景：视野检测、怪物仇恨范围、技能施放方向判定
 * @note 性能影响：轻量级，包含角度归一化和相对角度计算
 */
bool Position::HasInArc(float arc, Position const* obj, float border) const
{
    // 特殊情况：自己始终在自己的视野范围内
    if (obj == this)
        return true;

    // 将扇形角度归一化到 [0, 2π] 范围
    arc = NormalizeOrientation(arc);

    // 计算目标相对于当前朝向的角度，并转换到 [-π, +π] 范围
    float angle = GetRelativeAngle(obj);
    if (angle > float(M_PI))
        angle -= 2.0f * float(M_PI);

    // 计算扇形的左右边界
    // lborder 在 [-π, 0] 范围，rborder 在 [0, π] 范围
    float lborder = -1 * (arc / border);                        // 左边界（负方向）
    float rborder = (arc / border);                             // 右边界（正方向）

    // 判断相对角度是否在扇形范围内
    return ((angle >= lborder) && (angle <= rborder));
}

/**
 * @brief 判断目标是否在当前实体前方的直线路径上
 *
 * 检测目标对象是否位于以当前实体为起点的直线范围内。
 * 直线范围由宽度和目标大小共同决定。
 *
 * @param pos 目标对象的位置
 * @param objSize 目标对象的碰撞体大小（半径）
 * @param width 直线路径的宽度
 * @return 如果目标在直线路径上返回 true，否则返回 false
 *
 * @note 算法原理：
 *       1. 首先检查目标是否在前方 180° 范围内（排除后方目标）
 *       2. 计算目标相对于当前朝向的角度
 *       3. 使用正弦定理：垂直距离 = sin(角度) * 直线距离
 *       4. 如果垂直距离小于（宽度 + 目标大小），则认为在直线上
 * @note 使用场景：直线技能判定（如冲锋、穿刺攻击）、视线检测
 * @note 性能影响：中等，包含三角函数和距离计算
 */
bool Position::HasInLine(Position const* pos, float objSize, float width) const
{
    // 首先检查目标是否在前方 180° 范围内
    if (!HasInArc(float(M_PI), pos))
        return false;

    // 将目标碰撞体大小加入宽度，形成总判定宽度
    width += objSize;

    // 计算目标相对于当前朝向的角度
    float angle = GetRelativeAngle(pos);

    // 使用正弦定理计算目标相对于直线路径的垂直距离
    // 如果垂直距离小于总宽度，则认为在直线路径上
    return std::fabs(std::sin(angle)) * GetExactDist2d(pos->GetPositionX(), pos->GetPositionY()) < width;
}

/**
 * @brief 将位置信息转换为可读的字符串格式
 *
 * 生成包含 X、Y、Z 坐标和朝向的格式化字符串，用于日志输出和调试。
 *
 * @return 格式化的字符串，例如："X: 1234.5 Y: 5678.9 Z: 12.3 O: 1.57"
 *
 * @note 输出格式："X: [x值] Y: [y值] Z: [z值] O: [朝向值]"
 * @note 使用场景：日志记录、调试输出、错误信息
 * @note 性能影响：中等，包含字符串格式化操作
 */
std::string Position::ToString() const
{
    std::stringstream sstr;
    sstr << "X: " << m_positionX << " Y: " << m_positionY << " Z: " << m_positionZ << " O: " << m_orientation;
    return sstr.str();
}

/**
 * @brief 将朝向角度归一化到 [0, 2π) 范围
 *
 * 将任意角度值转换为等效的标准范围 [0, 2π) 内的角度。
 * 处理负数角度和大于 2π 的角度。
 *
 * @param o 要归一化的角度值（弧度）
 * @return 归一化后的角度值，范围 [0, 2π)
 *
 * @note 算法原理：
 *       - 正数角度：使用 fmod(o, 2π) 取模
 *       - 负数角度：先取绝对值并模 2π，然后取负加 2π 转换到正范围
 * @note 示例：
 *       - NormalizeOrientation(3π) = π
 *       - NormalizeOrientation(-π/2) = 3π/2
 *       - NormalizeOrientation(2π) = 0
 * @note 使用场景：角度比较、朝向同步、方向计算
 * @note 性能影响：轻量级，包含取模运算
 * @note 注意：std::fmod 只支持正数，因此负数需要特殊处理
 */
float Position::NormalizeOrientation(float o)
{
    // std::fmod 只支持正数，因此需要特殊处理负数情况
    if (o < 0)
    {
        // 处理负角度：取绝对值后模 2π，然后转换到 [0, 2π) 范围
        float mod = o *-1;
        mod = std::fmod(mod, 2.0f * static_cast<float>(M_PI));
        mod = -mod + 2.0f * static_cast<float>(M_PI);
        return mod;
    }
    // 正角度直接模 2π
    return std::fmod(o, 2.0f * static_cast<float>(M_PI));
}

ByteBuffer& operator<<(ByteBuffer& buf, Position::ConstStreamer<Position::XY> const& streamer)
{
    buf << streamer.Pos->GetPositionX();
    buf << streamer.Pos->GetPositionY();
    return buf;
}

ByteBuffer& operator>>(ByteBuffer& buf, Position::Streamer<Position::XY> const& streamer)
{
    float x, y;
    buf >> x >> y;
    streamer.Pos->Relocate(x, y);
    return buf;
}

ByteBuffer& operator<<(ByteBuffer& buf, Position::ConstStreamer<Position::XYZ> const& streamer)
{
    buf << streamer.Pos->GetPositionX();
    buf << streamer.Pos->GetPositionY();
    buf << streamer.Pos->GetPositionZ();
    return buf;
}

ByteBuffer& operator>>(ByteBuffer& buf, Position::Streamer<Position::XYZ> const& streamer)
{
    float x, y, z;
    buf >> x >> y >> z;
    streamer.Pos->Relocate(x, y, z);
    return buf;
}

ByteBuffer& operator<<(ByteBuffer& buf, Position::ConstStreamer<Position::XYZO> const& streamer)
{
    buf << streamer.Pos->GetPositionX();
    buf << streamer.Pos->GetPositionY();
    buf << streamer.Pos->GetPositionZ();
    buf << streamer.Pos->GetOrientation();
    return buf;
}

ByteBuffer& operator>>(ByteBuffer& buf, Position::Streamer<Position::XYZO> const& streamer)
{
    float x, y, z, o;
    buf >> x >> y >> z >> o;
    streamer.Pos->Relocate(x, y, z, o);
    return buf;
}

ByteBuffer& operator<<(ByteBuffer& buf, Position::ConstStreamer<Position::PackedXYZ> const& streamer)
{
    buf.appendPackXYZ(streamer.Pos->GetPositionX(), streamer.Pos->GetPositionY(), streamer.Pos->GetPositionZ());
    return buf;
}

std::string WorldLocation::GetDebugInfo() const
{
    std::stringstream sstr;
    MapEntry const* mapEntry = sMapStore.LookupEntry(m_mapId);
    sstr << "MapID: " << m_mapId << " Map name: '" << (mapEntry ? mapEntry->MapName[sWorld->GetDefaultDbcLocale()] : "<not found>") <<"' " << Position::ToString();
    return sstr.str();
}
