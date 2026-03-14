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
 * @file ModelInstance.h
 * @brief 模型实例定义
 *
 * 本文件定义了VMAP系统中模型实例的相关类，是地图碰撞检测的核心组件。
 * 主要功能包括：
 * - 模型生成信息的存储和序列化
 * - 模型实例的空间变换和碰撞检测
 * - 区域信息和位置信息查询
 *
 * 模型实例分为两种类型：
 * 1. M2模型：游戏对象模型，如树木、岩石等装饰物
 * 2. WMO模型：世界模型对象，如建筑物、室内场景等
 *
 * 空间变换：
 * 模型数据在局部坐标系中定义，需要进行缩放、旋转、平移变换到世界坐标系。
 * 为了优化性能，使用逆变换将世界坐标转换到模型局部坐标进行碰撞检测。
 */

#ifndef _MODELINSTANCE_H_
#define _MODELINSTANCE_H_

#include <G3D/Matrix3.h>
#include <G3D/Vector3.h>
#include <G3D/AABox.h>
#include <G3D/Ray.h>

#include "Define.h"

namespace VMAP
{
    class WorldModel;
    struct AreaInfo;
    struct LocationInfo;
    enum class ModelIgnoreFlags : uint32;

    /**
     * @enum ModelFlags
     * @brief 模型标志位枚举
     *
     * 定义模型实例的类型和属性标志。
     */
    enum ModelFlags
    {
        MOD_M2 = 1,           ///< M2模型标志（游戏对象模型，如树木、岩石）
        MOD_WORLDSPAWN = 1<<1, ///< 世界生成标志（地图初始放置的模型）
        MOD_HAS_BOUND = 1<<2   ///< 是否有包围盒标志（WMO模型通常有预计算的包围盒）
    };

    /**
     * @class ModelSpawn
     * @brief 模型生成信息基类
     *
     * 存储模型的基本生成信息，包括位置、旋转、缩放等。
     * 这是静态数据，从地图文件中加载，所有相同ID的模型实例共享这些数据。
     *
     * 数据字段：
     * - flags: 模型类型和属性标志
     * - adtId: ADT（地图瓦片）ID
     * - ID: 模型唯一标识符
     * - iPos: 世界坐标位置
     * - iRot: 欧拉角旋转（度数）
     * - iScale: 缩放比例
     * - iBound: 世界空间包围盒
     * - name: 模型文件名
     *
     * 序列化：
     * 支持从文件读取和写入，用于地图数据的持久化存储。
     */
    class TC_COMMON_API ModelSpawn
    {
        public:
            //mapID, tileX, tileY, Flags, ID, Pos, Rot, Scale, Bound_lo, Bound_hi, name
            uint32 flags;           ///< 模型标志位（M2/WMO类型等）
            uint16 adtId;           ///< ADT瓦片ID（地图分块标识）
            uint32 ID;              ///< 模型唯一标识符
            G3D::Vector3 iPos;      ///< 世界坐标位置（x, y, z）
            G3D::Vector3 iRot;      ///< 欧拉角旋转（x, y, z），单位：度
            float iScale;           ///< 缩放比例（1.0为正常大小）
            G3D::AABox iBound;      ///< 世界空间包围盒（用于快速碰撞剔除）
            std::string name;       ///< 模型文件名

            /**
             * @brief 相等比较运算符
             * @param other 另一个模型生成信息
             * @return true 如果两个模型的ID相同
             */
            bool operator==(ModelSpawn const& other) const { return ID == other.ID; }

            /**
             * @brief 获取包围盒
             * @return 包围盒的常量引用
             */
            const G3D::AABox& getBounds() const { return iBound; }

            /**
             * @brief 从文件读取模型生成信息
             * @param rf 文件指针（已打开）
             * @param spawn 输出：模型生成信息
             * @return true 如果读取成功
             *
             * 文件格式：
             * - uint32 flags
             * - uint16 adtId
             * - uint32 ID
             * - float[3] iPos
             * - float[3] iRot
             * - float iScale
             * - 如果flags & MOD_HAS_BOUND: float[3] iBound.low, float[3] iBound.high
             * - uint32 nameLength
             * - char[nameLength] name
             */
            static bool readFromFile(FILE* rf, ModelSpawn &spawn);

            /**
             * @brief 将模型生成信息写入文件
             * @param wf 文件指针（已打开）
             * @param spawn 模型生成信息
             * @return true 如果写入成功
             */
            static bool writeToFile(FILE* rw, ModelSpawn const& spawn);
    };

    /**
     * @class ModelInstance
     * @brief 模型实例类
     *
     * 继承自ModelSpawn，添加了运行时碰撞检测功能。
     * 每个模型实例都关联一个WorldModel对象，后者包含实际的几何数据。
     *
     * 主要职责：
     * - 管理模型的空间变换（旋转、缩放）
     * - 提供射线碰撞检测接口
     * - 提供区域信息和位置信息查询
     * - 支持液体表面高度计算
     *
     * 变换处理：
     * - 使用逆旋转矩阵(iInvRot)将世界坐标转换到模型局部坐标
     * - 使用逆缩放(iInvScale)调整距离
     * - 避免对每个三角形进行变换，提高性能
     *
     * 性能优化：
     * - 使用AABox进行快速粗略碰撞剔除
     * - 预计算逆旋转矩阵和逆缩放
     * - 使用BIH加速三角形碰撞检测
     */
    class TC_COMMON_API ModelInstance: public ModelSpawn
    {
        public:
            /**
             * @brief 默认构造函数
             */
            ModelInstance(): iInvScale(0.0f), iModel(nullptr) { }

            /**
             * @brief 构造函数
             * @param spawn 模型生成信息
             * @param model 世界模型指针（包含几何数据）
             *
             * 根据生成信息计算逆旋转矩阵和逆缩放。
             */
            ModelInstance(ModelSpawn const& spawn, WorldModel* model);

            /**
             * @brief 设置模型为未加载状态
             *
             * 当模型数据被卸载时调用，清除模型指针。
             */
            void setUnloaded() { iModel = nullptr; }

            /**
             * @brief 射线碰撞检测
             * @param pRay 待检测的射线
             * @param pMaxDist 输入：最大检测距离；输出：实际碰撞距离
             * @param pStopAtFirstHit 是否在首次碰撞时停止
             * @param ignoreFlags 忽略标志
             * @return true 如果射线与模型相交
             *
             * 碰撞检测流程：
             * 1. 检查模型是否加载
             * 2. 粗略碰撞检测：AABox相交测试
             * 3. 坐标变换：世界坐标 -> 模型局部坐标
             * 4. 精确碰撞检测：调用WorldModel
             * 5. 距离变换：模型局部距离 -> 世界距离
             */
            bool intersectRay(G3D::Ray const& pRay, float& pMaxDist, bool pStopAtFirstHit, ModelIgnoreFlags ignoreFlags) const;

            /**
             * @brief 点与模型的区域信息查询
             * @param p 查询点的世界坐标
             * @param info 输出：区域信息
             *
             * 查询点所在的区域信息，如地面高度、区域ID等。
             * 仅对WMO模型有效，M2模型不包含区域信息。
             */
            void intersectPoint(G3D::Vector3 const& p, AreaInfo &info) const;

            /**
             * @brief 获取点的位置信息
             * @param p 查询点的世界坐标
             * @param info 输出：位置信息
             * @return true 如果成功获取位置信息
             *
             * 类似intersectPoint，但返回更详细的位置信息。
             */
            bool GetLocationInfo(G3D::Vector3 const& p, LocationInfo &info) const;

            /**
             * @brief 获取液体表面高度
             * @param p 查询点的世界坐标
             * @param info 位置信息
             * @param liqHeight 输出：液体表面高度
             * @return true 如果该位置有液体
             */
            bool GetLiquidLevel(G3D::Vector3 const& p, LocationInfo &info, float &liqHeight) const;

            /**
             * @brief 获取世界模型指针
             * @return 世界模型指针
             */
            WorldModel* getWorldModel() { return iModel; }

        protected:
            G3D::Matrix3 iInvRot;   ///< 逆旋转矩阵，用于将世界坐标转换到模型局部坐标
            float iInvScale;         ///< 逆缩放比例，用于距离变换
            WorldModel* iModel;      ///< 世界模型指针，包含实际的几何数据
    };
} // namespace VMAP

#endif // _MODELINSTANCE
