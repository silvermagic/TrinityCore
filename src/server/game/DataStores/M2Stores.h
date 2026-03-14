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

#ifndef TRINITY_M2STORES_H
#define TRINITY_M2STORES_H

#include "Define.h"
#include "Position.h"
#include <vector>

/**
 * @file M2Stores.h
 * @brief M2 模型相机数据存储系统
 *
 * 本文件定义了用于存储和管理 M2 模型文件中相机数据的结构和方法。
 * M2 是魔兽世界使用的 3D 模型格式,包含了模型的几何数据、动画、相机等信息。
 *
 * 该系统主要用于处理游戏中的过场动画(cinematic)相机路径,
 * 这些相机路径定义了电影序列中相机的移动轨迹和时间戳。
 *
 * @see Position.h 位置坐标系统
 */

/**
 * @struct FlyByCamera
 * @brief 飞越相机数据结构
 *
 * 该结构体存储单个相机关键帧的数据,包括时间戳和位置信息。
 * 在过场动画中,相机沿着由多个关键帧定义的路径移动,
 * 每个 FlyByCamera 实例代表路径上的一个关键点。
 *
 * 这些数据通常从 M2 模型文件中提取,用于创建平滑的相机运动,
 * 如电影序列中的飞越镜头、开场动画等。
 */
struct FlyByCamera
{
    uint32 timeStamp;      ///< 时间戳(毫秒),表示该关键帧在动画序列中的时间点
    Position locations;    ///< 相机在该关键帧的三维位置坐标(包括 X、Y、Z 坐标和朝向)
};

/**
 * @brief 加载 M2 相机数据
 *
 * 从客户端数据文件中读取并解析所有 M2 模型的相机数据。
 * 该函数在服务器启动时调用,将所有过场动画相机路径加载到内存中,
 * 供后续查询使用。
 *
 * 加载过程包括:
 * - 遍历客户端数据目录中的 M2 文件
 * - 解析每个 M2 文件中的相机定义
 * - 提取相机路径关键帧数据
 * - 将数据存储到内部缓存结构中
 *
 * @param dataPath 客户端数据目录路径,通常指向包含 MPQ 或提取后的数据文件的目录
 *
 * @note 此函数应在服务器初始化期间调用,且只调用一次
 * @note 如果加载失败,将在日志中记录错误信息,但不会中断服务器启动
 *
 * @see GetFlyByCameras 用于获取已加载的相机数据
 */
TC_GAME_API void LoadM2Cameras(std::string const& dataPath);

/**
 * @brief 获取指定过场动画相机的飞越相机路径数据
 *
 * 根据过场动画相机 ID 检索对应的相机路径关键帧序列。
 * 这些数据用于控制游戏内电影序列中的相机运动。
 *
 * 返回的向量包含了按时间顺序排列的相机关键帧,
 * 游戏引擎可以通过插值这些关键帧来生成平滑的相机运动轨迹。
 *
 * @param cinematicCameraId 过场动画相机 ID,对应于 DBC 文件中的 CinematicCamera.dbc 条目
 *
 * @return 指向飞越相机向量的常量指针,包含该相机的所有关键帧数据;
 *         如果指定的相机 ID 不存在,则返回 nullptr
 *
 * @note 返回的指针指向内部缓存的只读数据,调用者不应修改或释放该内存
 * @note 返回的向量已按 timeStamp 排序
 *
 * @see FlyByCamera 关键帧数据结构
 * @see LoadM2Cameras 数据加载函数
 *
 * @par 使用示例:
 * @code
 * if (auto cameras = GetFlyByCameras(cameraId))
 * {
 *     for (FlyByCamera const& cam : *cameras)
 *     {
 *         // 处理每个相机关键帧
 *         ProcessKeyframe(cam.timeStamp, cam.locations);
 *     }
 * }
 * @endcode
 */
TC_GAME_API std::vector<FlyByCamera> const* GetFlyByCameras(uint32 cinematicCameraId);

#endif
