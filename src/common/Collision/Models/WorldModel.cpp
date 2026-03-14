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
 * @file WorldModel.cpp
 * @brief 世界模型实现
 *
 * 本文件实现了世界模型的碰撞检测功能，是VMAP系统的核心组件。
 * 主要功能包括：
 * - 三角形射线碰撞检测（Möller-Trumbore算法）
 * - WMO液体表面的管理和高度计算
 * - 组模型的管理和碰撞检测
 * - 世界模型的序列化和反序列化
 *
 * 核心算法：
 * 1. Möller-Trumbore射线-三角形相交算法：高效的射线与三角形碰撞检测
 * 2. BIH(边界区间层次)：加速空间分割和碰撞检测
 * 3. 双线性插值：液体表面高度计算
 *
 * 性能优化：
 * - 使用BIH加速结构，O(log n)复杂度
 * - 快速包围盒剔除
 * - 支持射线提前终止
 */

#include "WorldModel.h"
#include "VMapDefinitions.h"
#include "MapTree.h"
#include "ModelInstance.h"
#include "ModelIgnoreFlags.h"

using G3D::Vector3;
using G3D::Ray;

/**
 * @brief GroupModel的边界特征特化
 *
 * 为BIH结构提供获取GroupModel包围盒的方法。
 */
template<> struct BoundsTrait<VMAP::GroupModel>
{
    static void getBounds(const VMAP::GroupModel& obj, G3D::AABox& out) { out = obj.GetBound(); }
};

namespace VMAP
{
    /**
     * @brief 射线与三角形相交检测
     * @param tri 三角形
     * @param points 顶点数组的起始迭代器
     * @param ray 待检测的射线
     * @param distance 输入：最大距离；输出：碰撞距离
     * @return true 如果射线与三角形相交
     *
     * 使用Möller-Trumbore算法检测射线与三角形的相交。
     * 该算法基于重心坐标，无需预先计算三角形平面方程。
     *
     * 算法原理：
     * 1. 计算射线方向与三角形边的叉积，得到辅助向量
     * 2. 计算行列式（点积），判断是否平行
     * 3. 计算重心坐标(u, v)，判断交点是否在三角形内
     * 4. 计算交点距离t
     *
     * 参考：Real-Time Rendering 2nd Edition, Chapter 13.7
     *
     * 性能：每个三角形约25-30次浮点运算
     */
    bool IntersectTriangle(MeshTriangle const& tri, std::vector<Vector3>::const_iterator points, G3D::Ray const& ray, float& distance)
    {
        static const float EPS = 1e-5f;  // 浮点误差容限

        // 参考RTR2第13.7章节的算法

        // 计算三角形的两条边向量
        const Vector3 e1 = points[tri.idx1] - points[tri.idx0];
        const Vector3 e2 = points[tri.idx2] - points[tri.idx0];

        // 计算射线方向与边e2的叉积
        const Vector3 p(ray.direction().cross(e2));

        // 计算行列式（点积）
        const float a = e1.dot(p);

        // 如果行列式接近零，射线与三角形平面平行
        if (std::fabs(a) < EPS) {
            // 行列式条件不佳，提前退出
            return false;
        }

        // 计算行列式的倒数，避免后续除法
        const float f = 1.0f / a;

        // 计算射线起点到三角形顶点的向量
        const Vector3 s(ray.origin() - points[tri.idx0]);

        // 计算重心坐标u
        const float u = f * s.dot(p);

        // 检查u是否在[0,1]范围内
        if ((u < 0.0f) || (u > 1.0f)) {
            // 射线击中了平面，但在几何体外部
            return false;
        }

        // 计算辅助向量q
        const Vector3 q(s.cross(e1));

        // 计算重心坐标v
        const float v = f * ray.direction().dot(q);

        // 检查v是否在[0,1]范围内，且u+v<=1
        if ((v < 0.0f) || ((u + v) > 1.0f)) {
            // 射线击中了平面，但在三角形外部
            return false;
        }

        // 计算交点距离t
        const float t = f * e2.dot(q);

        // 检查是否为有效的交点（在射线上，且比之前的交点更近）
        if ((t > 0.0f) && (t < distance))
        {
            // 这是一个新的、更近的交点
            distance = t;

            /* 重心坐标（可用于插值）：
            baryCoord[0] = 1.0 - u - v;
            baryCoord[1] = u;
            baryCoord[2] = v; */

            return true;
        }
        // 这个交点比之前的更远，忽略
        return false;
    }

    /**
     * @class TriBoundFunc
     * @brief 三角形包围盒计算函数对象
     *
     * 用于BIH构建时计算每个三角形的包围盒。
     */
    class TriBoundFunc
    {
        public:
            /**
             * @brief 构造函数
             * @param vert 顶点数组
             */
            TriBoundFunc(std::vector<Vector3>& vert): vertices(vert.begin()) { }

            /**
             * @brief 计算三角形的包围盒
             * @param tri 三角形
             * @param out 输出：包围盒
             */
            void operator()(MeshTriangle const& tri, G3D::AABox& out) const
            {
                // 从第一个顶点开始
                G3D::Vector3 lo = vertices[tri.idx0];
                G3D::Vector3 hi = lo;

                // 计算三个顶点的最小和最大值
                lo = (lo.min(vertices[tri.idx1])).min(vertices[tri.idx2]);
                hi = (hi.max(vertices[tri.idx1])).max(vertices[tri.idx2]);

                out = G3D::AABox(lo, hi);
            }
        protected:
            const std::vector<Vector3>::const_iterator vertices;  ///< 顶点数组迭代器
    };

    // ===================== WmoLiquid ==================================

    /**
     * @brief WmoLiquid构造函数
     * @param width X方向瓦片数量
     * @param height Y方向瓦片数量
     * @param corner 网格左下角坐标
     * @param type 液体类型
     *
     * 根据瓦片数量分配高度数组和标志数组。
     * 高度数组大小为(tilesX+1)*(tilesY+1)，因为需要存储网格点而不是瓦片。
     */
    WmoLiquid::WmoLiquid(uint32 width, uint32 height, Vector3 const& corner, uint32 type) :
        iTilesX(width), iTilesY(height), iCorner(corner), iType(type)
    {
        if (width && height)
        {
            // 分配高度网格和标志数组
            iHeight = new float[(width + 1) * (height + 1)];
            iFlags = new uint8[width * height];
        }
        else
        {
            // 特殊情况：单个高度值（整个液体表面高度相同）
            iHeight = new float[1];
            iFlags = nullptr;
        }
    }

    /**
     * @brief 拷贝构造函数
     */
    WmoLiquid::WmoLiquid(WmoLiquid const& other): iHeight(nullptr), iFlags(nullptr)
    {
        *this = other; // 使用赋值运算符
    }

    /**
     * @brief 析构函数
     *
     * 释放高度数组和标志数组。
     */
    WmoLiquid::~WmoLiquid()
    {
        delete[] iHeight;
        delete[] iFlags;
    }

    /**
     * @brief 赋值运算符
     * @param other 源对象
     * @return 当前对象引用
     *
     * 深拷贝所有数据成员，包括动态分配的数组。
     */
    WmoLiquid& WmoLiquid::operator=(WmoLiquid const& other)
    {
        if (this == &other)
            return *this;

        // 复制基本属性
        iTilesX = other.iTilesX;
        iTilesY = other.iTilesY;
        iCorner = other.iCorner;
        iType = other.iType;

        // 释放旧数组
        delete[] iHeight;
        delete[] iFlags;

        // 深拷贝高度数组
        if (other.iHeight)
        {
            iHeight = new float[(iTilesX+1)*(iTilesY+1)];
            memcpy(iHeight, other.iHeight, (iTilesX+1)*(iTilesY+1)*sizeof(float));
        }
        else
            iHeight = nullptr;

        // 深拷贝标志数组
        if (other.iFlags)
        {
            iFlags = new uint8[iTilesX * iTilesY];
            memcpy(iFlags, other.iFlags, iTilesX * iTilesY);
        }
        else
            iFlags = nullptr;

        return *this;
    }

    /**
     * @brief 获取液体高度
     * @param pos 查询位置（模型局部坐标）
     * @param liqHeight 输出：液体表面高度
     * @return true 如果该位置有液体
     *
     * 根据位置在网格中的坐标，通过双线性插值计算液体高度。
     *
     * 算法：
     * 1. 将位置转换为网格坐标
     * 2. 检查瓦片标志，确定是否有液体
     * 3. 将瓦片细分为两个三角形
     * 4. 根据位置在三角形中的位置进行插值
     *
     * 瓦片细分：
     * 每个网格瓦片被细分为两个三角形：
     *
     *      ^ dy
     *      |
     *    1 x---------x (1, 1)
     *      | (b)   / |
     *      |     /   |
     *      |   /     |
     *      | /   (a) |
     *      x---------x---> dx
     *    0           1
     *
     * 三角形(a)：dx > dy
     * 三角形(b)：dx <= dy
     */
    bool WmoLiquid::GetLiquidHeight(Vector3 const& pos, float &liqHeight) const
    {
        // 简单情况：整个液体表面高度相同
        if (!iFlags)
        {
            liqHeight = iHeight[0];
            return true;
        }

        // 计算网格坐标（浮点数）
        float tx_f = (pos.x - iCorner.x)/LIQUID_TILE_SIZE;
        uint32 tx = uint32(tx_f);
        if (tx_f < 0.0f || tx >= iTilesX)
            return false;

        float ty_f = (pos.y - iCorner.y)/LIQUID_TILE_SIZE;
        uint32 ty = uint32(ty_f);
        if (ty_f < 0.0f || ty >= iTilesY)
            return false;

        // 检查瓦片标志，确定是否应该使用该瓦片的液体高度
        // 检查0x08可能就足够了，但禁用的瓦片总是0x?F
        if ((iFlags[tx + ty*iTilesX] & 0x0F) == 0x0F)
            return false;

        // 计算瓦片内的相对坐标(dx, dy)，范围[0, 1]
        float dx = tx_f - (float)tx;
        float dy = ty_f - (float)ty;

        /* 将瓦片细分为两个三角形（不确定客户端是否完全这样做）

            ^ dy
            |
          1 x---------x (1, 1)
            | (b)   / |
            |     /   |
            |   /     |
            | /   (a) |
            x---------x---> dx
          0           1
        */

        uint32 const rowOffset = iTilesX + 1;

        if (dx > dy) // 情况(a)：右上三角形
        {
            // 沿x方向的斜率
            float sx = iHeight[tx+1 +  ty    * rowOffset] - iHeight[tx   + ty * rowOffset];
            // 沿y方向的斜率
            float sy = iHeight[tx+1 + (ty+1) * rowOffset] - iHeight[tx+1 + ty * rowOffset];
            // 插值计算高度
            liqHeight = iHeight[tx + ty * rowOffset] + dx * sx + dy * sy;
        }
        else // 情况(b)：左下三角形
        {
            // 沿x方向的斜率
            float sx = iHeight[tx+1 + (ty+1) * rowOffset] - iHeight[tx + (ty+1) * rowOffset];
            // 沿y方向的斜率
            float sy = iHeight[tx   + (ty+1) * rowOffset] - iHeight[tx +  ty    * rowOffset];
            // 插值计算高度
            liqHeight = iHeight[tx + ty * rowOffset] + dx * sx + dy * sy;
        }
        return true;
    }

    /**
     * @brief 计算文件大小
     * @return 序列化后的文件大小（字节）
     */
    uint32 WmoLiquid::GetFileSize()
    {
        return 2 * sizeof(uint32) +              // iTilesX, iTilesY
                sizeof(Vector3) +                 // iCorner
                sizeof(uint32) +                  // iType
                (iFlags ? ((iTilesX + 1) * (iTilesY + 1) * sizeof(float) + iTilesX * iTilesY) : sizeof(float));
    }

    /**
     * @brief 写入文件
     * @param wf 文件指针
     * @return true 如果写入成功
     *
     * 文件格式：
     * - uint32 iTilesX
     * - uint32 iTilesY
     * - Vector3 iCorner
     * - uint32 iType
     * - 如果有瓦片：
     *   - float[] iHeight (大小为(tilesX+1)*(tilesY+1))
     *   - uint8[] iFlags (大小为tilesX*tilesY)
     * - 否则：
     *   - float iHeight[0]
     */
    bool WmoLiquid::writeToFile(FILE* wf)
    {
        bool result = false;

        // 写入基本属性
        if (fwrite(&iTilesX, sizeof(uint32), 1, wf) == 1 &&
            fwrite(&iTilesY, sizeof(uint32), 1, wf) == 1 &&
            fwrite(&iCorner, sizeof(Vector3), 1, wf) == 1 &&
            fwrite(&iType, sizeof(uint32), 1, wf) == 1)
        {
            if (iTilesX && iTilesY)
            {
                // 写入高度网格
                uint32 size = (iTilesX + 1) * (iTilesY + 1);
                if (fwrite(iHeight, sizeof(float), size, wf) == size)
                {
                    // 写入标志数组
                    size = iTilesX * iTilesY;
                    result = fwrite(iFlags, sizeof(uint8), size, wf) == size;
                }
            }
            else
            {
                // 特殊情况：单个高度值
                result = fwrite(iHeight, sizeof(float), 1, wf) == 1;
            }
        }

        return result;
    }

    /**
     * @brief 从文件读取
     * @param rf 文件指针
     * @param out 输出：液体对象指针
     * @return true 如果读取成功
     */
    bool WmoLiquid::readFromFile(FILE* rf, WmoLiquid* &out)
    {
        bool result = false;
        WmoLiquid* liquid = new WmoLiquid();

        // 读取基本属性
        if (fread(&liquid->iTilesX, sizeof(uint32), 1, rf) == 1 &&
            fread(&liquid->iTilesY, sizeof(uint32), 1, rf) == 1 &&
            fread(&liquid->iCorner, sizeof(Vector3), 1, rf) == 1 &&
            fread(&liquid->iType, sizeof(uint32), 1, rf) == 1)
        {
            if (liquid->iTilesX && liquid->iTilesY)
            {
                // 读取高度网格
                uint32 size = (liquid->iTilesX + 1) * (liquid->iTilesY + 1);
                liquid->iHeight = new float[size];
                if (fread(liquid->iHeight, sizeof(float), size, rf) == size)
                {
                    // 读取标志数组
                    size = liquid->iTilesX * liquid->iTilesY;
                    liquid->iFlags = new uint8[size];
                    result = fread(liquid->iFlags, sizeof(uint8), size, rf) == size;
                }
            }
            else
            {
                // 特殊情况：单个高度值
                liquid->iHeight = new float[1];
                result = fread(liquid->iHeight, sizeof(float), 1, rf) == 1;
            }
        }

        // 清理或返回结果
        if (!result)
            delete liquid;
        else
            out = liquid;

        return result;
    }

    /**
     * @brief 获取位置信息
     * @param tilesX 输出：X方向瓦片数量
     * @param tilesY 输出：Y方向瓦片数量
     * @param corner 输出：左下角坐标
     */
    void WmoLiquid::getPosInfo(uint32 &tilesX, uint32 &tilesY, G3D::Vector3 &corner) const
    {
        tilesX = iTilesX;
        tilesY = iTilesY;
        corner = iCorner;
    }

    // ===================== GroupModel ==================================

    /**
     * @brief GroupModel拷贝构造函数
     * @param other 源对象
     *
     * 深拷贝所有数据成员，包括液体数据。
     */
    GroupModel::GroupModel(GroupModel const& other):
        iBound(other.iBound), iMogpFlags(other.iMogpFlags), iGroupWMOID(other.iGroupWMOID),
        vertices(other.vertices), triangles(other.triangles), meshTree(other.meshTree), iLiquid(nullptr)
    {
        // 深拷贝液体数据
        if (other.iLiquid)
            iLiquid = new WmoLiquid(*other.iLiquid);
    }

    /**
     * @brief 设置网格数据
     * @param vert 顶点数组（会被交换）
     * @param tri 三角形数组（会被交换）
     *
     * 传入的向量会与当前几何数据交换，并构建BIH加速结构。
     * 使用swap避免数据拷贝，提高性能。
     */
    void GroupModel::setMeshData(std::vector<Vector3> &vert, std::vector<MeshTriangle> &tri)
    {
        vertices.swap(vert);
        triangles.swap(tri);

        // 构建BIH加速结构
        TriBoundFunc bFunc(vertices);
        meshTree.build(triangles, bFunc);
    }

    /**
     * @brief 写入文件
     * @param wf 文件指针
     * @return true 如果写入成功
     *
     * 文件格式：
     * - AABox iBound
     * - uint32 iMogpFlags
     * - uint32 iGroupWMOID
     * - "VERT"块：
     *   - uint32 chunkSize
     *   - uint32 count
     *   - Vector3[count] vertices
     * - "TRIM"块：
     *   - uint32 chunkSize
     *   - uint32 count
     *   - MeshTriangle[count] triangles
     * - "MBIH"块：meshTree
     * - "LIQU"块：
     *   - uint32 chunkSize (0表示无液体)
     *   - 如果有液体：WmoLiquid数据
     */
    bool GroupModel::writeToFile(FILE* wf)
    {
        bool result = true;
        uint32 chunkSize, count;

        // 写入包围盒和标志
        if (result && fwrite(&iBound, sizeof(G3D::AABox), 1, wf) != 1) result = false;
        if (result && fwrite(&iMogpFlags, sizeof(uint32), 1, wf) != 1) result = false;
        if (result && fwrite(&iGroupWMOID, sizeof(uint32), 1, wf) != 1) result = false;

        // 写入顶点数据块
        if (result && fwrite("VERT", 1, 4, wf) != 4) result = false;
        count = vertices.size();
        chunkSize = sizeof(uint32)+ sizeof(Vector3)*count;
        if (result && fwrite(&chunkSize, sizeof(uint32), 1, wf) != 1) result = false;
        if (result && fwrite(&count, sizeof(uint32), 1, wf) != 1) result = false;

        // 没有碰撞几何体的模型在这里结束，不确定是否有用
        if (!count)
            return result;

        if (result && fwrite(&vertices[0], sizeof(Vector3), count, wf) != count) result = false;

        // 写入三角形网格数据块
        if (result && fwrite("TRIM", 1, 4, wf) != 4) result = false;
        count = triangles.size();
        chunkSize = sizeof(uint32)+ sizeof(MeshTriangle)*count;
        if (result && fwrite(&chunkSize, sizeof(uint32), 1, wf) != 1) result = false;
        if (result && fwrite(&count, sizeof(uint32), 1, wf) != 1) result = false;
        if (result && fwrite(&triangles[0], sizeof(MeshTriangle), count, wf) != count) result = false;

        // 写入网格BIH数据块
        if (result && fwrite("MBIH", 1, 4, wf) != 4) result = false;
        if (result) result = meshTree.writeToFile(wf);

        // 写入液体数据块
        if (result && fwrite("LIQU", 1, 4, wf) != 4) result = false;
        if (!iLiquid)
        {
            chunkSize = 0;
            if (result && fwrite(&chunkSize, sizeof(uint32), 1, wf) != 1) result = false;
            return result;
        }

        chunkSize = iLiquid->GetFileSize();
        if (result && fwrite(&chunkSize, sizeof(uint32), 1, wf) != 1) result = false;
        if (result) result = iLiquid->writeToFile(wf);

        return result;
    }

    /**
     * @brief 从文件读取
     * @param rf 文件指针
     * @return true 如果读取成功
     */
    bool GroupModel::readFromFile(FILE* rf)
    {
        char chunk[8];
        bool result = true;
        uint32 chunkSize = 0;
        uint32 count = 0;

        // 清理旧数据
        triangles.clear();
        vertices.clear();
        delete iLiquid;
        iLiquid = nullptr;

        // 读取包围盒和标志
        if (result && fread(&iBound, sizeof(G3D::AABox), 1, rf) != 1) result = false;
        if (result && fread(&iMogpFlags, sizeof(uint32), 1, rf) != 1) result = false;
        if (result && fread(&iGroupWMOID, sizeof(uint32), 1, rf) != 1) result = false;

        // 读取顶点数据块
        if (result && !readChunk(rf, chunk, "VERT", 4)) result = false;
        if (result && fread(&chunkSize, sizeof(uint32), 1, rf) != 1) result = false;
        if (result && fread(&count, sizeof(uint32), 1, rf) != 1) result = false;

        // 没有碰撞几何体的模型在这里结束
        if (!count)
            return result;

        if (result) vertices.resize(count);
        if (result && fread(&vertices[0], sizeof(Vector3), count, rf) != count) result = false;

        // 读取三角形网格数据块
        if (result && !readChunk(rf, chunk, "TRIM", 4)) result = false;
        if (result && fread(&chunkSize, sizeof(uint32), 1, rf) != 1) result = false;
        if (result && fread(&count, sizeof(uint32), 1, rf) != 1) result = false;
        if (result) triangles.resize(count);
        if (result && fread(&triangles[0], sizeof(MeshTriangle), count, rf) != count) result = false;

        // 读取网格BIH数据块
        if (result && !readChunk(rf, chunk, "MBIH", 4)) result = false;
        if (result) result = meshTree.readFromFile(rf);

        // 读取液体数据块
        if (result && !readChunk(rf, chunk, "LIQU", 4)) result = false;
        if (result && fread(&chunkSize, sizeof(uint32), 1, rf) != 1) result = false;
        if (result && chunkSize > 0)
            result = WmoLiquid::readFromFile(rf, iLiquid);

        return result;
    }

    /**
     * @struct GModelRayCallback
     * @brief 组模型射线碰撞回调函数对象
     *
     * 用于BIH遍历时回调处理射线与三角形的碰撞检测。
     */
    struct GModelRayCallback
    {
        /**
         * @brief 构造函数
         * @param tris 三角形数组
         * @param vert 顶点数组
         */
        GModelRayCallback(std::vector<MeshTriangle> const& tris, const std::vector<Vector3> &vert):
            vertices(vert.begin()), triangles(tris.begin()), hit(false) { }

        /**
         * @brief 回调函数
         * @param ray 射线
         * @param entry 三角形索引
         * @param distance 距离
         * @param pStopAtFirstHit 是否在首次碰撞时停止
         * @return true 如果发生碰撞
         */
        bool operator()(G3D::Ray const& ray, uint32 entry, float& distance, bool /*pStopAtFirstHit*/)
        {
            // 检测射线与三角形的碰撞
            hit = IntersectTriangle(triangles[entry], vertices, ray, distance) || hit;
            return hit;
        }

        std::vector<Vector3>::const_iterator vertices;      ///< 顶点数组迭代器
        std::vector<MeshTriangle>::const_iterator triangles; ///< 三角形数组迭代器
        bool hit;                                             ///< 是否发生碰撞
    };

    /**
     * @brief 射线碰撞检测
     * @param ray 待检测的射线
     * @param distance 输入：最大距离；输出：碰撞距离
     * @param stopAtFirstHit 是否在首次碰撞时停止
     * @return true 如果射线与模型相交
     *
     * 使用BIH加速结构进行射线碰撞检测。
     */
    bool GroupModel::IntersectRay(G3D::Ray const& ray, float& distance, bool stopAtFirstHit) const
    {
        if (triangles.empty())
            return false;

        GModelRayCallback callback(triangles, vertices);
        meshTree.intersectRay(ray, callback, distance, stopAtFirstHit);
        return callback.hit;
    }

    /**
     * @brief 判断点是否在对象内部
     * @param pos 查询点
     * @param down 向下方向向量
     * @param z_dist 输出：到地面的距离
     * @return true 如果点在对象内部
     *
     * 通过向下发射射线，检测点是否在地面上。
     * 起点稍微上移，避免数值精度问题。
     */
    bool GroupModel::IsInsideObject(Vector3 const& pos, Vector3 const& down, float& z_dist) const
    {
        if (triangles.empty() || !iBound.contains(pos))
            return false;

        // 起点稍微上移0.1，避免自相交
        Vector3 rPos = pos - 0.1f * down;
        float dist = G3D::finf();
        G3D::Ray ray(rPos, down);
        bool hit = IntersectRay(ray, dist, false);

        if (hit)
            z_dist = dist - 0.1f;  // 减去上移的距离

        return hit;
    }

    /**
     * @brief 获取液体高度
     * @param pos 查询位置
     * @param liqHeight 输出：液体表面高度
     * @return true 如果该位置有液体
     */
    bool GroupModel::GetLiquidLevel(Vector3 const& pos, float& liqHeight) const
    {
        if (iLiquid)
            return iLiquid->GetLiquidHeight(pos, liqHeight);
        return false;
    }

    /**
     * @brief 获取液体类型
     * @return 液体类型ID，如果没有液体返回0
     */
    uint32 GroupModel::GetLiquidType() const
    {
        if (iLiquid)
            return iLiquid->GetType();
        return 0;
    }

    /**
     * @brief 获取网格数据
     * @param outVertices 输出：顶点数组
     * @param outTriangles 输出：三角形数组
     * @param liquid 输出：液体数据指针
     */
    void GroupModel::getMeshData(std::vector<G3D::Vector3>& outVertices, std::vector<MeshTriangle>& outTriangles, WmoLiquid*& liquid)
    {
        outVertices = vertices;
        outTriangles = triangles;
        liquid = iLiquid;
    }

    // ===================== WorldModel ==================================

    /**
     * @brief 设置组模型
     * @param models 组模型数组（会被交换）
     *
     * 传入的向量会与当前组模型交换，并构建BIH加速结构。
     */
    void WorldModel::setGroupModels(std::vector<GroupModel>& models)
    {
        groupModels.swap(models);
        // 构建组的BIH加速结构
        groupTree.build(groupModels, BoundsTrait<GroupModel>::getBounds, 1);
    }

    /**
     * @struct WModelRayCallBack
     * @brief 世界模型射线碰撞回调函数对象
     *
     * 用于BIH遍历时回调处理射线与组模型的碰撞检测。
     */
    struct WModelRayCallBack
    {
        /**
         * @brief 构造函数
         * @param mod 组模型数组
         */
        WModelRayCallBack(std::vector<GroupModel> const& mod): models(mod.begin()), hit(false) { }

        /**
         * @brief 回调函数
         * @param ray 射线
         * @param entry 组模型索引
         * @param distance 距离
         * @param pStopAtFirstHit 是否在首次碰撞时停止
         * @return true 如果发生碰撞
         */
        bool operator()(G3D::Ray const& ray, uint32 entry, float& distance, bool pStopAtFirstHit)
        {
            bool result = models[entry].IntersectRay(ray, distance, pStopAtFirstHit);
            if (result)
                hit = true;
            return hit;
        }

        std::vector<GroupModel>::const_iterator models; ///< 组模型数组迭代器
        bool hit;                                        ///< 是否发生碰撞
    };

    /**
     * @brief 射线碰撞检测
     * @param ray 待检测的射线
     * @param distance 输入：最大距离；输出：碰撞距离
     * @param stopAtFirstHit 是否在首次碰撞时停止
     * @param ignoreFlags 忽略标志
     * @return true 如果射线与模型相交
     *
     * 如果ignoreFlags包含M2标志，则跳过M2模型的碰撞检测。
     * 对于M2模型（只有一个组），直接检测以提高性能。
     */
    bool WorldModel::IntersectRay(G3D::Ray const& ray, float& distance, bool stopAtFirstHit, ModelIgnoreFlags ignoreFlags) const
    {
        // 如果调用者要求忽略某些对象，检查标志
        if ((ignoreFlags & ModelIgnoreFlags::M2) != ModelIgnoreFlags::Nothing)
        {
            // 如果调用者请求忽略M2模型，则M2模型不计入视距计算
            if (Flags & MOD_M2)
                return false;
        }

        // M2模型的简单优化：如果只有一个子模型，不需要使用包围盒树
        // 也许最好创建一个单独的类，使用虚函数进行相交测试
        if (groupModels.size() == 1)
            return groupModels[0].IntersectRay(ray, distance, stopAtFirstHit);

        // 使用BIH加速结构进行碰撞检测
        WModelRayCallBack isc(groupModels);
        groupTree.intersectRay(ray, isc, distance, stopAtFirstHit);
        return isc.hit;
    }

    /**
     * @class WModelAreaCallback
     * @brief 世界模型区域查询回调函数对象
     *
     * 用于BIH遍历时回调处理点与组模型的区域查询。
     * 查找最上面的地面对象（最小的z距离）。
     */
    class WModelAreaCallback {
        public:
            /**
             * @brief 构造函数
             * @param vals 组模型数组
             * @param down 向下方向向量
             */
            WModelAreaCallback(std::vector<GroupModel> const& vals, Vector3 const& down) :
                prims(vals.begin()), hit(vals.end()), minVol(G3D::finf()), zDist(G3D::finf()), zVec(down) { }

            std::vector<GroupModel>::const_iterator prims; ///< 组模型数组迭代器
            std::vector<GroupModel>::const_iterator hit;   ///< 碰撞的组模型迭代器
            float minVol;                                   ///< 最小体积（未使用）
            float zDist;                                    ///< 最小z距离
            Vector3 zVec;                                   ///< 向下方向向量

            /**
             * @brief 回调函数
             * @param point 查询点
             * @param entry 组模型索引
             *
             * 查找最上面的地面对象。
             */
            void operator()(Vector3 const& point, uint32 entry)
            {
                float group_Z;

                // 检查点是否在组模型内部
                if (prims[entry].IsInsideObject(point, zVec, group_Z))
                {
                    // 更新最小z距离和碰撞的组模型
                    if (group_Z < zDist)
                    {
                        zDist = group_Z;
                        hit = prims + entry;
                    }
#ifdef VMAP_DEBUG
                    GroupModel const& gm = prims[entry];
                    printf("%10u %8X %7.3f, %7.3f, %7.3f | %7.3f, %7.3f, %7.3f | z=%f, p_z=%f\n", gm.GetWmoID(), gm.GetMogpFlags(),
                    gm.GetBound().low().x, gm.GetBound().low().y, gm.GetBound().low().z,
                    gm.GetBound().high().x, gm.GetBound().high().y, gm.GetBound().high().z, group_Z, point.z);
#endif
                }
            }
    };

    /**
     * @brief 点与模型的相交检测
     * @param p 查询点（模型局部坐标）
     * @param down 向下方向向量
     * @param dist 输出：到地面的距离
     * @param info 输出：区域信息
     * @return true 如果点在模型内部
     *
     * 使用BIH加速结构查找点所在的组模型，并返回区域信息。
     */
    bool WorldModel::IntersectPoint(const G3D::Vector3 &p, const G3D::Vector3 &down, float &dist, AreaInfo &info) const
    {
        if (groupModels.empty())
            return false;

        WModelAreaCallback callback(groupModels, down);
        groupTree.intersectPoint(p, callback);

        if (callback.hit != groupModels.end())
        {
            info.rootId = RootWMOID;
            info.groupId = callback.hit->GetWmoID();
            info.flags = callback.hit->GetMogpFlags();
            info.result = true;
            dist = callback.zDist;
            return true;
        }
        return false;
    }

    /**
     * @brief 获取位置信息
     * @param p 查询点（模型局部坐标）
     * @param down 向下方向向量
     * @param dist 输出：到地面的距离
     * @param info 输出：位置信息
     * @return true 如果成功获取位置信息
     *
     * 类似IntersectPoint，但返回更详细的位置信息，包括具体的模型引用。
     */
    bool WorldModel::GetLocationInfo(const G3D::Vector3 &p, const G3D::Vector3 &down, float &dist, LocationInfo &info) const
    {
        if (groupModels.empty())
            return false;

        WModelAreaCallback callback(groupModels, down);
        groupTree.intersectPoint(p, callback);

        if (callback.hit != groupModels.end())
        {
            info.rootId = RootWMOID;
            info.hitModel = &(*callback.hit);
            dist = callback.zDist;
            return true;
        }
        return false;
    }

    /**
     * @brief 写入文件
     * @param filename 文件名
     * @return true 如果写入成功
     *
     * 文件格式：
     * - char[8] VMAP_MAGIC：魔数
     * - "WMOD"块：
     *   - uint32 chunkSize
     *   - uint32 RootWMOID
     * - "GMOD"块：
     *   - uint32 count
     *   - GroupModel[count]
     * - "GBIH"块：groupTree
     */
    bool WorldModel::writeFile(const std::string &filename)
    {
        FILE* wf = fopen(filename.c_str(), "wb");
        if (!wf)
            return false;

        uint32 chunkSize, count;
        bool result = fwrite(VMAP_MAGIC, 1, 8, wf) == 8;

        // 写入WMOD块
        if (result && fwrite("WMOD", 1, 4, wf) != 4) result = false;
        chunkSize = sizeof(uint32) + sizeof(uint32);
        if (result && fwrite(&chunkSize, sizeof(uint32), 1, wf) != 1) result = false;
        if (result && fwrite(&RootWMOID, sizeof(uint32), 1, wf) != 1) result = false;

        // 写入组模型
        count = groupModels.size();
        if (count)
        {
            // 写入GMOD块
            if (result && fwrite("GMOD", 1, 4, wf) != 4) result = false;
            if (result && fwrite(&count, sizeof(uint32), 1, wf) != 1) result = false;

            // 写入每个组模型
            for (uint32 i=0; i<groupModels.size() && result; ++i)
                result = groupModels[i].writeToFile(wf);

            // 写入组BIH
            if (result && fwrite("GBIH", 1, 4, wf) != 4) result = false;
            if (result) result = groupTree.writeToFile(wf);
        }

        fclose(wf);
        return result;
    }

    /**
     * @brief 从文件读取
     * @param filename 文件名
     * @return true 如果读取成功
     */
    bool WorldModel::readFile(const std::string &filename)
    {
        FILE* rf = fopen(filename.c_str(), "rb");
        if (!rf)
            return false;

        bool result = true;
        uint32 chunkSize = 0;
        uint32 count = 0;
        char chunk[8];

        // 读取魔数
        if (!readChunk(rf, chunk, VMAP_MAGIC, 8)) result = false;

        // 读取WMOD块
        if (result && !readChunk(rf, chunk, "WMOD", 4)) result = false;
        if (result && fread(&chunkSize, sizeof(uint32), 1, rf) != 1) result = false;
        if (result && fread(&RootWMOID, sizeof(uint32), 1, rf) != 1) result = false;

        // 读取组模型
        if (result && readChunk(rf, chunk, "GMOD", 4))
        {
            // 读取组数量
            if (result && fread(&count, sizeof(uint32), 1, rf) != 1) result = false;
            if (result) groupModels.resize(count);

            // 读取每个组模型
            for (uint32 i=0; i<count && result; ++i)
                result = groupModels[i].readFromFile(rf);

            // 读取组BIH
            if (result && !readChunk(rf, chunk, "GBIH", 4)) result = false;
            if (result) result = groupTree.readFromFile(rf);
        }

        fclose(rf);
        return result;
    }

    /**
     * @brief 获取组模型
     * @param outGroupModels 输出：组模型数组
     */
    void WorldModel::getGroupModels(std::vector<GroupModel>& outGroupModels)
    {
        outGroupModels = groupModels;
    }
}
