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
 * @file WorldModel.h
 * @brief 世界模型定义
 *
 * 本文件定义了世界模型(WorldModel)及其相关类，是VMAP碰撞检测系统的核心组件。
 * 主要功能包括：
 * - 存储和管理3D模型的几何数据（顶点、三角形）
 * - 提供高效的射线碰撞检测
 * - 支持区域信息和位置信息查询
 * - 支持液体表面高度计算
 * - 支持模型的序列化和反序列化
 *
 * 类层次结构：
 * - MeshTriangle: 三角形网格面片
 * - WmoLiquid: WMO模型的液体表面数据
 * - GroupModel: WMO模型的一个组（子模型）
 * - WorldModel: 完整的世界模型，包含多个GroupModel
 *
 * 碰撞检测优化：
 * - 使用BIH(边界区间层次)结构加速碰撞检测
 * - 支持快速包围盒剔除
 * - 支持射线提前终止
 *
 * 数据来源：
 * - M2模型：游戏对象模型，如树木、岩石等
 * - WMO模型：世界模型对象，如建筑物、室内场景
 */

#ifndef _WORLDMODEL_H
#define _WORLDMODEL_H

#include <G3D/HashTrait.h>
#include <G3D/Vector3.h>
#include <G3D/AABox.h>
#include <G3D/Ray.h>
#include "BoundingIntervalHierarchy.h"

#include "Define.h"

namespace VMAP
{
    class TreeNode;
    struct AreaInfo;
    struct LocationInfo;
    enum class ModelIgnoreFlags : uint32;

    /**
     * @class MeshTriangle
     * @brief 三角形网格面片
     *
     * 表示一个三角形，由三个顶点索引组成。
     * 用于构建模型的碰撞网格。
     *
     * 顶点索引：
     * - idx0, idx1, idx2: 顶点在顶点数组中的索引
     * - 顶点按逆时针顺序存储（右手坐标系）
     */
    class TC_COMMON_API MeshTriangle
    {
        public:
            /**
             * @brief 默认构造函数
             */
            MeshTriangle() : idx0(0), idx1(0), idx2(0) { }

            /**
             * @brief 构造函数
             * @param na 第一个顶点索引
             * @param nb 第二个顶点索引
             * @param nc 第三个顶点索引
             */
            MeshTriangle(uint32 na, uint32 nb, uint32 nc): idx0(na), idx1(nb), idx2(nc) { }

            uint32 idx0;  ///< 第一个顶点索引
            uint32 idx1;  ///< 第二个顶点索引
            uint32 idx2;  ///< 第三个顶点索引
    };

    /**
     * @class WmoLiquid
     * @brief WMO模型的液体表面数据
     *
     * 存储WMO模型中的液体表面信息，包括高度网格和类型。
     * 液体表面被划分成网格，每个网格瓦片可以有不同的高度。
     *
     * 数据结构：
     * - iTilesX, iTilesY: 网格瓦片数量
     * - iCorner: 网格的左下角坐标
     * - iHeight: 高度网格数组，大小为(tilesX+1)*(tilesY+1)
     * - iFlags: 瓦片标志数组，指示哪些瓦片有液体
     * - iType: 液体类型（水、岩浆等）
     *
     * 使用场景：
     * - 玩家进入建筑物内的水池
     * - 室内河流、湖泊
     * - 建筑物内的岩浆池
     */
    class TC_COMMON_API WmoLiquid
    {
        public:
            /**
             * @brief 构造函数
             * @param width X方向瓦片数量
             * @param height Y方向瓦片数量
             * @param corner 网格左下角坐标
             * @param type 液体类型
             */
            WmoLiquid(uint32 width, uint32 height, G3D::Vector3 const& corner, uint32 type);

            /**
             * @brief 拷贝构造函数
             */
            WmoLiquid(WmoLiquid const& other);

            /**
             * @brief 析构函数
             */
            ~WmoLiquid();

            /**
             * @brief 赋值运算符
             */
            WmoLiquid& operator=(WmoLiquid const& other);

            /**
             * @brief 获取液体高度
             * @param pos 查询位置（局部坐标）
             * @param liqHeight 输出：液体表面高度
             * @return true 如果该位置有液体
             *
             * 根据位置在网格中的坐标，通过双线性插值计算液体高度。
             */
            bool GetLiquidHeight(G3D::Vector3 const& pos, float& liqHeight) const;

            /**
             * @brief 获取液体类型
             * @return 液体类型ID
             */
            uint32 GetType() const { return iType; }

            /**
             * @brief 获取高度数组指针
             * @return 高度数组指针
             */
            float *GetHeightStorage() { return iHeight; }

            /**
             * @brief 获取标志数组指针
             * @return 标志数组指针
             */
            uint8 *GetFlagsStorage() { return iFlags; }

            /**
             * @brief 计算文件大小
             * @return 序列化后的文件大小
             */
            uint32 GetFileSize();

            /**
             * @brief 写入文件
             * @param wf 文件指针
             * @return true 如果写入成功
             */
            bool writeToFile(FILE* wf);

            /**
             * @brief 从文件读取
             * @param rf 文件指针
             * @param liquid 输出：液体对象指针
             * @return true 如果读取成功
             */
            static bool readFromFile(FILE* rf, WmoLiquid* &liquid);

            /**
             * @brief 获取位置信息
             * @param tilesX 输出：X方向瓦片数量
             * @param tilesY 输出：Y方向瓦片数量
             * @param corner 输出：左下角坐标
             */
            void getPosInfo(uint32 &tilesX, uint32 &tilesY, G3D::Vector3 &corner) const;

        private:
            /**
             * @brief 默认构造函数（私有）
             */
            WmoLiquid() : iTilesX(0), iTilesY(0), iCorner(), iType(0), iHeight(nullptr), iFlags(nullptr) { }

            uint32 iTilesX;       ///< X方向瓦片数量
            uint32 iTilesY;       ///< Y方向瓦片数量
            G3D::Vector3 iCorner; ///< 网格左下角坐标
            uint32 iType;         ///< 液体类型（水、岩浆等）
            float *iHeight;       ///< 高度网格，大小为(tilesX+1)*(tilesY+1)
            uint8 *iFlags;        ///< 瓦片标志，指示哪些瓦片有液体
    };

    /**
     * @class GroupModel
     * @brief WMO模型的组（子模型）
     *
     * WMO(World Model Object)通常由多个组(GroupModel)组成。
     * 每个组代表建筑物的一个部分，如一个房间、一层楼等。
     *
     * 数据成员：
     * - iBound: 组的包围盒
     * - iMogpFlags: 组标志（室内/室外等）
     * - iGroupWMOID: 组的WMO ID
     * - vertices: 顶点数组
     * - triangles: 三角形数组
     * - meshTree: 碰撞检测加速结构(BIH)
     * - iLiquid: 液体数据（如果有）
     *
     * 标志位说明：
     * - 0x8: 室外
     * - 0x2000: 室内
     *
     * 使用场景：
     * - 室内场景的碰撞检测
     * - 建筑物内不同房间的区域判定
     */
    class TC_COMMON_API GroupModel
    {
        public:
            /**
             * @brief 默认构造函数
             */
            GroupModel() : iBound(), iMogpFlags(0), iGroupWMOID(0), iLiquid(nullptr) { }

            /**
             * @brief 拷贝构造函数
             */
            GroupModel(GroupModel const& other);

            /**
             * @brief 构造函数
             * @param mogpFlags 组标志
             * @param groupWMOID 组的WMO ID
             * @param bound 包围盒
             */
            GroupModel(uint32 mogpFlags, uint32 groupWMOID, G3D::AABox const& bound):
                        iBound(bound), iMogpFlags(mogpFlags), iGroupWMOID(groupWMOID), iLiquid(nullptr) { }

            /**
             * @brief 析构函数
             */
            ~GroupModel() { delete iLiquid; }

            /**
             * @brief 设置网格数据
             * @param vert 顶点数组（会被交换）
             * @param tri 三角形数组（会被交换）
             *
             * 传入的向量会与当前几何数据交换，并构建BIH加速结构。
             */
            void setMeshData(std::vector<G3D::Vector3> &vert, std::vector<MeshTriangle> &tri);

            /**
             * @brief 设置液体数据
             * @param liquid 液体对象指针（所有权转移）
             */
            void setLiquidData(WmoLiquid*& liquid) { iLiquid = liquid; liquid = nullptr; }

            /**
             * @brief 射线碰撞检测
             * @param ray 待检测的射线
             * @param distance 输入：最大距离；输出：碰撞距离
             * @param stopAtFirstHit 是否在首次碰撞时停止
             * @return true 如果射线与模型相交
             */
            bool IntersectRay(const G3D::Ray &ray, float &distance, bool stopAtFirstHit) const;

            /**
             * @brief 判断点是否在对象内部
             * @param pos 查询点
             * @param down 向下方向向量
             * @param z_dist 输出：到地面的距离
             * @return true 如果点在对象内部
             */
            bool IsInsideObject(const G3D::Vector3 &pos, const G3D::Vector3 &down, float &z_dist) const;

            /**
             * @brief 获取液体高度
             * @param pos 查询位置
             * @param liqHeight 输出：液体表面高度
             * @return true 如果该位置有液体
             */
            bool GetLiquidLevel(const G3D::Vector3 &pos, float &liqHeight) const;

            /**
             * @brief 获取液体类型
             * @return 液体类型ID
             */
            uint32 GetLiquidType() const;

            /**
             * @brief 写入文件
             * @param wf 文件指针
             * @return true 如果写入成功
             */
            bool writeToFile(FILE* wf);

            /**
             * @brief 从文件读取
             * @param rf 文件指针
             * @return true 如果读取成功
             */
            bool readFromFile(FILE* rf);

            /**
             * @brief 获取包围盒
             * @return 包围盒的常量引用
             */
            const G3D::AABox& GetBound() const { return iBound; }

            /**
             * @brief 获取组标志
             * @return 组标志位
             */
            uint32 GetMogpFlags() const { return iMogpFlags; }

            /**
             * @brief 获取组的WMO ID
             * @return WMO ID
             */
            uint32 GetWmoID() const { return iGroupWMOID; }

            /**
             * @brief 获取网格数据
             * @param outVertices 输出：顶点数组
             * @param outTriangles 输出：三角形数组
             * @param liquid 输出：液体数据指针
             */
            void getMeshData(std::vector<G3D::Vector3>& outVertices, std::vector<MeshTriangle>& outTriangles, WmoLiquid*& liquid);

        protected:
            G3D::AABox iBound;                ///< 组的包围盒
            uint32 iMogpFlags;                 ///< 组标志（0x8:室外, 0x2000:室内）
            uint32 iGroupWMOID;                ///< 组的WMO ID
            std::vector<G3D::Vector3> vertices;      ///< 顶点数组
            std::vector<MeshTriangle> triangles;     ///< 三角形数组
            BIH meshTree;                      ///< 碰撞检测加速结构
            WmoLiquid* iLiquid;                ///< 液体数据（如果有）
    };

    /**
     * @class WorldModel
     * @brief 世界模型
     *
     * 表示一个完整的3D模型，可以是M2模型或WMO模型。
     * 在原始坐标系中定义，不包含位置、旋转、缩放变换。
     *
     * 数据成员：
     * - Flags: 模型标志（M2或WMO）
     * - RootWMOID: 根WMO ID（仅WMO模型）
     * - groupModels: 组模型数组（WMO有多个组，M2只有一个）
     * - groupTree: 组的BIH加速结构
     *
     * 模型类型：
     * - M2模型：游戏对象模型，如树木、岩石等，通常只有一个组
     * - WMO模型：世界模型对象，如建筑物，可以有多个组（每个房间一个组）
     *
     * 序列化：
     * - writeFile/readFile: 将模型数据保存到.vmo文件
     * - 文件格式：自定义二进制格式，包含顶点、三角形、BIH结构
     *
     * 使用流程：
     * 1. 加载模型文件（readFile）
     * 2. 创建ModelInstance，关联WorldModel
     * 3. 通过ModelInstance进行碰撞检测
     */
    class TC_COMMON_API WorldModel
    {
        public:
            /**
             * @brief 默认构造函数
             */
            WorldModel(): Flags(0), RootWMOID(0) { }

            /**
             * @brief 设置组模型
             * @param models 组模型数组（会被交换）
             *
             * 传入的向量会与当前组模型交换，并构建BIH加速结构。
             */
            void setGroupModels(std::vector<GroupModel> &models);

            /**
             * @brief 设置根WMO ID
             * @param id WMO ID
             */
            void setRootWmoID(uint32 id) { RootWMOID = id; }

            /**
             * @brief 射线碰撞检测
             * @param ray 待检测的射线
             * @param distance 输入：最大距离；输出：碰撞距离
             * @param stopAtFirstHit 是否在首次碰撞时停止
             * @param ignoreFlags 忽略标志
             * @return true 如果射线与模型相交
             *
             * 在模型局部坐标系中进行碰撞检测。
             * 如果ignoreFlags包含M2标志，则跳过M2模型。
             */
            bool IntersectRay(const G3D::Ray &ray, float &distance, bool stopAtFirstHit, ModelIgnoreFlags ignoreFlags) const;

            /**
             * @brief 点与模型的相交检测
             * @param p 查询点（模型局部坐标）
             * @param down 向下方向向量
             * @param dist 输出：到地面的距离
             * @param info 输出：区域信息
             * @return true 如果点在模型内部
             *
             * 用于查询点所在的区域信息。
             */
            bool IntersectPoint(const G3D::Vector3 &p, const G3D::Vector3 &down, float &dist, AreaInfo &info) const;

            /**
             * @brief 获取位置信息
             * @param p 查询点（模型局部坐标）
             * @param down 向下方向向量
             * @param dist 输出：到地面的距离
             * @param info 输出：位置信息
             * @return true 如果成功获取位置信息
             *
             * 类似IntersectPoint，但返回更详细的位置信息。
             */
            bool GetLocationInfo(const G3D::Vector3 &p, const G3D::Vector3 &down, float &dist, LocationInfo &info) const;

            /**
             * @brief 写入文件
             * @param filename 文件名
             * @return true 如果写入成功
             *
             * 将模型数据序列化到.vmo文件。
             */
            bool writeFile(const std::string &filename);

            /**
             * @brief 从文件读取
             * @param filename 文件名
             * @return true 如果读取成功
             *
             * 从.vmo文件加载模型数据。
             */
            bool readFile(const std::string &filename);

            /**
             * @brief 获取组模型
             * @param outGroupModels 输出：组模型数组
             */
            void getGroupModels(std::vector<GroupModel>& outGroupModels);

            uint32 Flags;  ///< 模型标志（M2或WMO）

        protected:
            uint32 RootWMOID;                  ///< 根WMO ID
            std::vector<GroupModel> groupModels;     ///< 组模型数组
            BIH groupTree;                     ///< 组的BIH加速结构
    };
} // namespace VMAP

#endif // _WORLDMODEL_H
