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
 * @file BoundingIntervalHierarchy.h
 * @brief 边界区间层次结构（BIH）头文件
 *
 * 定义了 BIH 空间加速结构，用于快速射线相交测试和点查询。
 * BIH 是一种高效的空间分割数据结构，特别适用于碰撞检测系统。
 *
 * 主要功能：
 * - 构建 BIH 树结构
 * - 射线相交测试
 * - 点包含查询
 * - 树的序列化/反序列化
 */

#ifndef _BIH_H
#define _BIH_H

#include <G3D/Vector3.h>
#include <G3D/Ray.h>
#include <G3D/AABox.h>

#include "Define.h"

#include <stdexcept>
#include <vector>
#include <algorithm>
#include <limits>
#include <cmath>
#include "string.h"

/// 最大栈大小，用于限制递归深度，防止栈溢出
#define MAX_STACK_SIZE 64

// https://stackoverflow.com/a/4328396

/**
 * @brief 将浮点数的二进制表示转换为无符号整数
 *
 * 该函数用于在不改变二进制表示的情况下将 float 类型转换为 uint32 类型。
 * 常用于在树节点中存储包围盒边界值，节省空间。
 *
 * @param f 要转换的浮点数
 * @return uint32 浮点数的二进制表示作为无符号整数
 *
 * @note 使用 static_assert 确保 float 和 uint32 大小相同
 */
static inline uint32 floatToRawIntBits(float f)
{
    static_assert(sizeof(float) == sizeof(uint32), "Size of uint32 and float must be equal for this to work");
    uint32 ret;
    memcpy(&ret, &f, sizeof(float));
    return ret;
}

/**
 * @brief 将无符号整数的二进制表示转换为浮点数
 *
 * 该函数是 floatToRawIntBits 的逆操作，用于从树节点中读取包围盒边界值。
 *
 * @param i 要转换的无符号整数
 * @return float 整数的二进制表示作为浮点数
 *
 * @note 使用 static_assert 确保 float 和 uint32 大小相同
 */
static inline float intBitsToFloat(uint32 i)
{
    static_assert(sizeof(float) == sizeof(uint32), "Size of uint32 and float must be equal for this to work");
    float ret;
    memcpy(&ret, &i, sizeof(uint32));
    return ret;
}

/**
 * @brief 轴对齐包围盒结构
 *
 * 简化的包围盒表示，只存储最小点和最大点。
 * 用于 BIH 树构建过程中的临时包围盒计算。
 */
struct AABound
{
    G3D::Vector3 lo;  ///< 包围盒最小点（左下后角）
    G3D::Vector3 hi;  ///< 包围盒最大点（右上后角）
};

/**
 * @brief 边界区间层次结构（Bounding Interval Hierarchy）类
 *
 * BIH 是一种空间加速结构，用于高效地进行射线相交测试和点查询。
 * 该实现基于 Sunflow 光线追踪器（MIT/X11 许可证），由 Christopher Kulla 开发。
 *
 * 主要特点：
 * - 构建速度快，支持动态场景
 * - 空间利用率高，适合不规则物体分布
 * - 查询效率高，平均 O(log n) 时间复杂度
 *
 * 数据结构：
 * - tree: 存储树节点的扁平数组
 * - objects: 存储图元索引的数组
 * - bounds: 整个场景的包围盒
 *
 * 使用场景：
 * - 游戏场景中的碰撞检测
 * - 射线追踪渲染
 * - 空间查询和区域检测
 */
class TC_COMMON_API BIH
{
    private:
        /**
         * @brief 初始化空树
         *
         * 清空树结构并创建一个虚拟叶子节点，用于处理空场景的情况。
         *
         * @note 在构造函数和空图元数组构建时调用
         */
        void init_empty()
        {
            tree.clear();
            objects.clear();
            bounds = G3D::AABox::empty();
            // 为第一个节点创建空间
            tree.push_back(3u << 30u); // 虚拟叶子节点
            tree.insert(tree.end(), 2, 0);
        }
    public:
        /**
         * @brief 默认构造函数
         *
         * 创建一个空的 BIH 树结构
         */
        BIH() { init_empty(); }

        /**
         * @brief 构建 BIH 树
         *
         * 从图元数组构建 BIH 树结构，是构建流程的入口函数
         *
         * @tparam BoundsFunc 获取图元包围盒的函数对象类型
         * @tparam PrimArray 图元数组类型
         * @param primitives 图元数组，包含所有需要索引的图元
         * @param getBounds 获取图元包围盒的函数对象
         * @param leafSize 叶子节点包含的最大图元数量，默认为 3
         * @param printStats 是否打印构建统计信息，默认为 false
         *
         * @note 构建流程：
         *       1. 初始化构建数据（索引数组、包围盒数组）
         *       2. 计算整个场景的包围盒
         *       3. 调用 buildHierarchy 递归构建树结构
         *       4. 将临时树和索引数组移动到成员变量
         *
         * @note 时间复杂度：O(n log n)，其中 n 为图元数量
         * @note 空间复杂度：O(n)
         */
        template <class BoundsFunc, class PrimArray>
        void build(PrimArray const& primitives, BoundsFunc& getBounds, uint32 leafSize = 3, bool printStats = false)
        {
            if (primitives.size() == 0)
            {
                init_empty();
                return;
            }

            // 初始化构建数据
            buildData dat;
            dat.maxPrims = leafSize;
            dat.numPrims = uint32(primitives.size());
            dat.indices = new uint32[dat.numPrims];
            dat.primBound = new G3D::AABox[dat.numPrims];

            // 计算场景包围盒和各图元包围盒
            getBounds(primitives[0], bounds);
            for (uint32 i=0; i<dat.numPrims; ++i)
            {
                dat.indices[i] = i;
                getBounds(primitives[i], dat.primBound[i]);
                bounds.merge(dat.primBound[i]);
            }

            // 构建层次结构
            std::vector<uint32> tempTree;
            BuildStats stats;
            buildHierarchy(tempTree, dat, stats);
            if (printStats)
                stats.printStats();

            // 将排序后的索引移动到成员变量
            objects.resize(dat.numPrims);
            for (uint32 i=0; i<dat.numPrims; ++i)
                objects[i] = dat.indices[i];

            // 移动临时树到成员变量
            tree = tempTree;
            delete[] dat.primBound;
            delete[] dat.indices;
        }

        /**
         * @brief 获取图元数量
         *
         * @return uint32 树中包含的图元总数
         */
        uint32 primCount() const { return uint32(objects.size()); }

        /**
         * @brief 射线相交测试
         *
         * 测试射线与树中所有图元的相交情况，是最常用的查询函数之一。
         * 使用迭代方式遍历树结构，避免递归开销。
         *
         * @tparam RayCallback 射线回调函数对象类型
         * @param r 待测试的射线
         * @param intersectCallback 相交回调函数，当射线与图元相交时被调用
         * @param maxDist 输入/输出参数，最大检测距离，可能会被更新为实际相交距离
         * @param stopAtFirst 是否在第一次相交时停止，默认为 false
         *
         * @note 算法流程：
         *       1. 计算射线与整个场景包围盒的相交区间
         *       2. 初始化遍历方向偏移量（根据射线方向优化）
         *       3. 迭代遍历树结构，使用栈模拟递归
         *       4. 对每个叶子节点中的图元调用回调函数
         *
         * @note 性能优化：
         *       - 使用方向符号位计算偏移量，减少分支判断
         *       - 提前终止：当区间无效或超出最大距离时停止
         *       - 区间裁剪：在遍历过程中不断缩小相交区间
         *
         * @note 时间复杂度：平均 O(log n)，最坏 O(n)
         */
        template<typename RayCallback>
        void intersectRay(const G3D::Ray &r, RayCallback& intersectCallback, float &maxDist, bool stopAtFirst = false) const
        {
            float intervalMin = -1.f;
            float intervalMax = -1.f;
            G3D::Vector3 org = r.origin();
            G3D::Vector3 dir = r.direction();
            G3D::Vector3 invDir;

            // 计算射线与场景包围盒的相交区间
            for (int i=0; i<3; ++i)
            {
                invDir[i] = 1.f / dir[i];
                if (G3D::fuzzyNe(dir[i], 0.0f))
                {
                    float t1 = (bounds.low()[i]  - org[i]) * invDir[i];
                    float t2 = (bounds.high()[i] - org[i]) * invDir[i];
                    if (t1 > t2)
                        std::swap(t1, t2);
                    if (t1 > intervalMin)
                        intervalMin = t1;
                    if (t2 < intervalMax || intervalMax < 0.f)
                        intervalMax = t2;
                    // intervalMax 只会变小，intervalMin 只会变大，提前终止
                    if (intervalMax <= 0 || intervalMin >= maxDist)
                        return;
                }
            }

            if (intervalMin > intervalMax)
                return;
            intervalMin = std::max(intervalMin, 0.f);
            intervalMax = std::min(intervalMax, maxDist);

            uint32 offsetFront[3];
            uint32 offsetBack[3];
            uint32 offsetFront3[3];
            uint32 offsetBack3[3];

            // 根据射线方向符号位计算自定义偏移量，用于优化遍历顺序
            for (int i=0; i<3; ++i)
            {
                offsetFront[i] = floatToRawIntBits(dir[i]) >> 31;
                offsetBack[i] = offsetFront[i] ^ 1;
                offsetFront3[i] = offsetFront[i] * 3;
                offsetBack3[i] = offsetBack[i] * 3;

                // 预先加 1，避免内部循环中重复加法
                ++offsetFront[i];
                ++offsetBack[i];
            }

            StackNode stack[MAX_STACK_SIZE];
            int stackPos = 0;
            int node = 0;

            // 主遍历循环
            while (true) {
                while (true)
                {
                    uint32 tn = tree[node];
                    uint32 axis = (tn & (3 << 30)) >> 30;  // 提取轴信息（0-2 表示 X/Y/Z，3 表示叶子节点）
                    bool BVH2 = (tn & (1 << 29)) != 0;     // 是否为 BVH2 节点
                    int offset = tn & ~(7 << 29);          // 提取偏移量

                    if (!BVH2)
                    {
                        if (axis < 3)
                        {
                            // "正常"内部节点
                            float tf = (intBitsToFloat(tree[node + offsetFront[axis]]) - org[axis]) * invDir[axis];
                            float tb = (intBitsToFloat(tree[node + offsetBack[axis]]) - org[axis]) * invDir[axis];

                            // 射线从裁剪区间之间穿过
                            if (tf < intervalMin && tb > intervalMax)
                                break;

                            int back = offset + offsetBack3[axis];
                            node = back;

                            // 射线只穿过远端节点
                            if (tf < intervalMin) {
                                intervalMin = (tb >= intervalMin) ? tb : intervalMin;
                                continue;
                            }

                            node = offset + offsetFront3[axis]; // 近端节点

                            // 射线只穿过近端节点
                            if (tb > intervalMax) {
                                intervalMax = (tf <= intervalMax) ? tf : intervalMax;
                                continue;
                            }

                            // 射线同时穿过两个节点，将远端节点压栈
                            stack[stackPos].node = back;
                            stack[stackPos].tnear = (tb >= intervalMin) ? tb : intervalMin;
                            stack[stackPos].tfar = intervalMax;
                            stackPos++;

                            // 更新近端节点的射线区间
                            intervalMax = (tf <= intervalMax) ? tf : intervalMax;
                            continue;
                        }
                        else
                        {
                            // 叶子节点 - 测试对象
                            int n = tree[node + 1];
                            while (n > 0) {
                                bool hit = intersectCallback(r, objects[offset], maxDist, stopAtFirst);
                                if (stopAtFirst && hit) return;
                                --n;
                                ++offset;
                            }
                            break;
                        }
                    }
                    else
                    {
                        // BVH2 节点（裁剪空白区域）
                        if (axis>2)
                            return; // 不应该发生
                        float tf = (intBitsToFloat(tree[node + offsetFront[axis]]) - org[axis]) * invDir[axis];
                        float tb = (intBitsToFloat(tree[node + offsetBack[axis]]) - org[axis]) * invDir[axis];
                        node = offset;
                        intervalMin = (tf >= intervalMin) ? tf : intervalMin;
                        intervalMax = (tb <= intervalMax) ? tb : intervalMax;
                        if (intervalMin > intervalMax)
                            break;
                        continue;
                    }
                } // 遍历循环

                // 从栈中弹出节点
                do
                {
                    // 栈是否为空？
                    if (stackPos == 0)
                        return;
                    // 回到栈的上一级
                    stackPos--;
                    intervalMin = stack[stackPos].tnear;
                    if (maxDist < intervalMin)
                        continue;
                    node = stack[stackPos].node;
                    intervalMax = stack[stackPos].tfar;
                    break;
                } while (true);
            }
        }

        /**
         * @brief 点包含查询
         *
         * 测试点是否在树中任何图元内部，用于空间位置查询。
         * 使用迭代方式遍历树结构，与射线测试类似但更简单。
         *
         * @tparam IsectCallback 相交回调函数对象类型
         * @param p 待测试的点
         * @param intersectCallback 相交回调函数，当点在图元内部时被调用
         *
         * @note 算法流程：
         *       1. 检查点是否在场景包围盒内
         *       2. 迭代遍历树结构，使用栈模拟递归
         *       3. 对每个叶子节点中的图元调用回调函数
         *
         * @note 与射线测试的区别：
         *       - 不需要计算相交区间，只判断点与节点边界的关系
         *       - 遍历顺序不重要，无需优化
         *
         * @note 时间复杂度：平均 O(log n)，最坏 O(n)
         */
        template<typename IsectCallback>
        void intersectPoint(const G3D::Vector3 &p, IsectCallback& intersectCallback) const
        {
            // 快速排除：点不在场景包围盒内
            if (!bounds.contains(p))
                return;

            StackNode stack[MAX_STACK_SIZE];
            int stackPos = 0;
            int node = 0;

            // 主遍历循环
            while (true) {
                while (true)
                {
                    uint32 tn = tree[node];
                    uint32 axis = (tn & (3 << 30)) >> 30;  // 提取轴信息
                    bool BVH2 = (tn & (1 << 29)) != 0;     // 是否为 BVH2 节点
                    int offset = tn & ~(7 << 29);          // 提取偏移量

                    if (!BVH2)
                    {
                        if (axis < 3)
                        {
                            // "正常"内部节点
                            float tl = intBitsToFloat(tree[node + 1]);
                            float tr = intBitsToFloat(tree[node + 2]);

                            // 点在裁剪区间之间
                            if (tl < p[axis] && tr > p[axis])
                                break;

                            int right = offset + 3;
                            node = right;

                            // 点只在右节点内
                            if (tl < p[axis]) {
                                continue;
                            }

                            node = offset; // 左节点

                            // 点只在左节点内
                            if (tr > p[axis]) {
                                continue;
                            }

                            // 点同时在两个节点内，将右节点压栈
                            stack[stackPos].node = right;
                            stackPos++;
                            continue;
                        }
                        else
                        {
                            // 叶子节点 - 测试对象
                            int n = tree[node + 1];
                            while (n > 0) {
                                intersectCallback(p, objects[offset]);
                                --n;
                                ++offset;
                            }
                            break;
                        }
                    }
                    else // BVH2 节点（左右两侧裁剪了空白区域）
                    {
                        if (axis>2)
                            return; // 不应该发生
                        float tl = intBitsToFloat(tree[node + 1]);
                        float tr = intBitsToFloat(tree[node + 2]);
                        node = offset;
                        // 点不在裁剪区间内
                        if (tl > p[axis] || tr < p[axis])
                            break;
                        continue;
                    }
                } // 遍历循环

                // 栈是否为空？
                if (stackPos == 0)
                    return;
                // 回到栈的上一级
                stackPos--;
                node = stack[stackPos].node;
            }
        }

        /**
         * @brief 将 BIH 树写入文件
         *
         * @param wf 写入文件的指针
         * @return true 写入成功
         * @return false 写入失败
         */
        bool writeToFile(FILE* wf) const;

        /**
         * @brief 从文件读取 BIH 树
         *
         * @param rf 读取文件的指针
         * @return true 读取成功
         * @return false 读取失败
         */
        bool readFromFile(FILE* rf);

    protected:
        std::vector<uint32> tree;      ///< 树节点数组，扁平化存储树结构
        std::vector<uint32> objects;   ///< 图元索引数组，存储排序后的图元索引
        G3D::AABox bounds;             ///< 整个场景的包围盒

        /**
         * @brief 构建数据结构
         *
         * 临时数据结构，用于构建过程中存储中间结果
         */
        struct buildData
        {
            uint32 *indices;       ///< 图元索引数组，构建过程中会被重排序
            G3D::AABox *primBound; ///< 各图元的包围盒数组
            uint32 numPrims;       ///< 图元总数
            int maxPrims;          ///< 叶子节点最大图元数量
        };

        /**
         * @brief 栈节点结构
         *
         * 用于迭代遍历时的栈元素，存储待处理的节点信息
         */
        struct StackNode
        {
            uint32 node;   ///< 节点索引
            float tnear;   ///< 近端相交距离
            float tfar;    ///< 远端相交距离
        };

        /**
         * @brief 构建统计信息类
         *
         * 收集和输出构建过程的统计信息，用于调试和性能分析
         */
        class BuildStats
        {
            private:
                int numNodes;       ///< 内部节点数量
                int numLeaves;      ///< 叶子节点数量
                int sumObjects;     ///< 图元总数（用于计算平均值）
                int minObjects;     ///< 叶子节点中最少图元数
                int maxObjects;     ///< 叶子节点中最多图元数
                int sumDepth;       ///< 深度总和（用于计算平均值）
                int minDepth;       ///< 最小深度
                int maxDepth;       ///< 最大深度
                int numLeavesN[6];  ///< 包含 0, 1, 2, 3, 4, >4 个图元的叶子节点数量
                int numBVH2;        ///< BVH2 节点数量

            public:
            /**
             * @brief 构造函数，初始化所有统计值为 0 或极值
             */
            BuildStats():
                numNodes(0), numLeaves(0), sumObjects(0), minObjects(0x0FFFFFFF),
                maxObjects(0xFFFFFFFF), sumDepth(0), minDepth(0x0FFFFFFF),
                maxDepth(0xFFFFFFFF), numBVH2(0)
            {
                for (int i=0; i<6; ++i) numLeavesN[i] = 0;
            }

            /**
             * @brief 更新内部节点计数
             */
            void updateInner() { numNodes++; }

            /**
             * @brief 更新 BVH2 节点计数
             */
            void updateBVH2() { numBVH2++; }

            /**
             * @brief 更新叶子节点统计
             * @param depth 叶子节点深度
             * @param n 叶子节点包含的图元数量
             */
            void updateLeaf(int depth, int n);

            /**
             * @brief 打印统计信息
             */
            void printStats();
        };

        /**
         * @brief 构建层次结构
         *
         * @param tempTree 临时树数组
         * @param dat 构建数据
         * @param stats 统计信息
         */
        void buildHierarchy(std::vector<uint32> &tempTree, buildData &dat, BuildStats &stats);

        /**
         * @brief 创建叶子节点
         *
         * @param tempTree 临时树数组
         * @param nodeIndex 节点索引
         * @param left 图元索引范围左边界
         * @param right 图元索引范围右边界
         *
         * @note 叶子节点格式：
         *       - tempTree[nodeIndex + 0]: (3 << 30) | left，表示叶子节点
         *       - tempTree[nodeIndex + 1]: right - left + 1，表示图元数量
         */
        void createNode(std::vector<uint32> &tempTree, int nodeIndex, uint32 left, uint32 right) const
        {
            // 写入叶子节点
            tempTree[nodeIndex + 0] = (3 << 30) | left;
            tempTree[nodeIndex + 1] = right - left + 1;
        }

        /**
         * @brief 递归分割函数
         *
         * @param left 图元索引范围左边界
         * @param right 图元索引范围右边界
         * @param tempTree 临时树数组
         * @param dat 构建数据
         * @param gridBox 网格包围盒
         * @param nodeBox 节点包围盒
         * @param nodeIndex 节点索引
         * @param depth 当前深度
         * @param stats 统计信息
         */
        void subdivide(int left, int right, std::vector<uint32> &tempTree, buildData &dat, AABound &gridBox, AABound &nodeBox, int nodeIndex, int depth, BuildStats &stats);
};

#endif // _BIH_H
