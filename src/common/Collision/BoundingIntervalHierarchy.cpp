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
 * @file BoundingIntervalHierarchy.cpp
 * @brief 边界区间层次结构（BIH）实现文件
 *
 * 本文件实现了 BIH 碰撞检测加速结构的核心算法。BIH 是一种类似于 BVH（包围盒层次）的空间分割结构，
 * 用于加速射线与物体、点与物体的相交测试。主要特点：
 * 1. 构建效率高，适合动态场景
 * 2. 空间分割方式灵活，可处理不规则分布的物体
 * 3. 支持高效的射线追踪和点查询
 *
 * 算法来源：基于 Sunflow 光线追踪器（MIT/X11 许可证）
 * 原作者：Christopher Kulla (2003-2007)
 */

#include "BoundingIntervalHierarchy.h"

// 跨平台 NaN 检测宏定义
#ifdef _MSC_VER
  #define isnan _isnan  // MSVC 使用 _isnan
#else
  #define isnan std::isnan  // 其他编译器使用 std::isnan
#endif

/**
 * @brief 构建层次结构树
 *
 * 初始化并构建 BIH 树结构，是构建流程的入口函数
 *
 * @param tempTree 输出参数，临时树结构数组，存储构建过程中的节点数据
 * @param dat 构建数据，包含图元索引和包围盒信息
 * @param stats 构建统计信息，记录节点数、深度等信息
 *
 * @note 函数会创建根节点并递归调用 subdivide 完成整个树的构建
 * @note 临时树会在构建完成后移动到成员变量 tree 中
 */
void BIH::buildHierarchy(std::vector<uint32> &tempTree, buildData &dat, BuildStats &stats)
{
    // 为第一个节点创建空间
    tempTree.push_back(uint32(3 << 30)); // 虚拟叶子节点，用于占位
    tempTree.insert(tempTree.end(), 2, 0);  // 添加两个 0 作为初始数据

    // 初始化包围盒，整个场景的边界框
    AABound gridBox = { bounds.low(), bounds.high() };
    AABound nodeBox = gridBox;  // 当前节点的包围盒，初始与整个场景相同

    // 启动递归分割函数，从索引 0 开始处理所有图元
    subdivide(0, dat.numPrims - 1, tempTree, dat, gridBox, nodeBox, 0, 1, stats);
}

/**
 * @brief 递归分割函数，构建 BIH 树的核心算法
 *
 * 该函数是 BIH 树构建的核心，通过递归地将空间分割成子区域来构建层次结构。
 * 分割策略基于最长轴，并根据图元的中心位置进行左右子树的划分。
 *
 * @param left 当前处理的图元索引范围左边界
 * @param right 当前处理的图元索引范围右边界
 * @param tempTree 临时树结构数组，存储节点数据
 * @param dat 构建数据，包含图元索引和包围盒信息
 * @param gridBox 当前网格的包围盒
 * @param nodeBox 当前节点的包围盒
 * @param nodeIndex 当前节点在树数组中的索引
 * @param depth 当前递归深度
 * @param stats 构建统计信息
 *
 * @note 该函数使用迭代和递归相结合的方式，处理多种分割情况：
 *       - 正常分割：图元被分成左右两部分
 *       - BVH2 节点：当发现大量空白区域时，创建裁剪节点
 *       - 叶子节点：当图元数量少于阈值或深度过大时停止分割
 * @note 性能注意事项：递归深度受 MAX_STACK_SIZE 限制，防止栈溢出
 */
void BIH::subdivide(int left, int right, std::vector<uint32> &tempTree, buildData &dat, AABound &gridBox, AABound &nodeBox, int nodeIndex, int depth, BuildStats &stats)
{
    // 终止条件：图元数量少于阈值或达到最大深度
    if ((right - left + 1) <= dat.maxPrims || depth >= MAX_STACK_SIZE)
    {
        // 创建叶子节点
        stats.updateLeaf(depth, right - left + 1);
        createNode(tempTree, nodeIndex, left, right);
        return;
    }

    // 初始化分割参数
    int axis = -1, prevAxis, rightOrig;
    float clipL = G3D::fnan(), clipR = G3D::fnan(), prevClip = G3D::fnan();
    float split = G3D::fnan(), prevSplit;
    bool wasLeft = true;

    // 主分割循环
    while (true)
    {
        prevAxis = axis;
        prevSplit = split;

        // 执行快速一致性检查
        G3D::Vector3 d( gridBox.hi - gridBox.lo );
        if (d.x < 0 || d.y < 0 || d.z < 0)
            throw std::logic_error("negative node extents");

        // 检查节点包围盒与网格包围盒的重叠关系
        for (int i = 0; i < 3; i++)
        {
            if (nodeBox.hi[i] < gridBox.lo[i] || nodeBox.lo[i] > gridBox.hi[i])
            {
                //UI.printError(Module.ACCEL, "Reached tree area in error - discarding node with: %d objects", right - left + 1);
                throw std::logic_error("invalid node overlap");
            }
        }

        // 找到最长轴作为分割轴
        axis = d.primaryAxis();
        split = 0.5f * (gridBox.lo[axis] + gridBox.hi[axis]);  // 分割平面位置

        // 分割左右子集
        clipL = -G3D::finf();
        clipR = G3D::finf();
        rightOrig = right; // 保存原始右边界，供后续使用
        float nodeL = G3D::finf();
        float nodeR = -G3D::finf();

        // 遍历所有图元，根据中心点位置分配到左右子树
        for (int i = left; i <= right;)
        {
            int obj = dat.indices[i];
            float minb = dat.primBound[obj].low()[axis];
            float maxb = dat.primBound[obj].high()[axis];
            float center = (minb + maxb) * 0.5f;

            if (center <= split)
            {
                // 图元中心在分割平面左侧，保持在左侧
                i++;
                if (clipL < maxb)
                    clipL = maxb;  // 更新左侧裁剪平面
            }
            else
            {
                // 图元中心在分割平面右侧，移动到最右侧
                int t = dat.indices[i];
                dat.indices[i] = dat.indices[right];
                dat.indices[right] = t;
                right--;
                if (clipR > minb)
                    clipR = minb;  // 更新右侧裁剪平面
            }
            nodeL = std::min(nodeL, minb);  // 记录所有图元的最小边界
            nodeR = std::max(nodeR, maxb);  // 记录所有图元的最大边界
        }

        // 检查是否存在空白区域（节点包围盒比图元实际占用空间大很多）
        if (nodeL > nodeBox.lo[axis] && nodeR < nodeBox.hi[axis])
        {
            float nodeBoxW = nodeBox.hi[axis] - nodeBox.lo[axis];
            float nodeNewW = nodeR - nodeL;
            // 如果节点包围盒比实际占用空间大30%以上，创建 BVH2 裁剪节点
            if (1.3f * nodeNewW < nodeBoxW)
            {
                stats.updateBVH2();
                int nextIndex = tempTree.size();

                // 为子节点分配空间
                tempTree.push_back(0);
                tempTree.push_back(0);
                tempTree.push_back(0);

                // 写入 BVH2 裁剪节点
                stats.updateInner();
                tempTree[nodeIndex + 0] = (axis << 30) | (1 << 29) | nextIndex;
                tempTree[nodeIndex + 1] = floatToRawIntBits(nodeL);
                tempTree[nodeIndex + 2] = floatToRawIntBits(nodeR);

                // 更新节点包围盒并递归
                nodeBox.lo[axis] = nodeL;
                nodeBox.hi[axis] = nodeR;
                subdivide(left, rightOrig, tempTree, dat, gridBox, nodeBox, nextIndex, depth + 1, stats);
                return;
            }
        }

        // 确保分割在持续进行
        if (right == rightOrig)
        {
            // 所有图元都在左侧
            if (prevAxis == axis && G3D::fuzzyEq(prevSplit, split)) {
                // 卡住了 - 创建叶子节点
                stats.updateLeaf(depth, right - left + 1);
                createNode(tempTree, nodeIndex, left, right);
                return;
            }
            if (clipL <= split) {
                // 继续在左半部分循环
                gridBox.hi[axis] = split;
                prevClip = clipL;
                wasLeft = true;
                continue;
            }
            gridBox.hi[axis] = split;
            prevClip = G3D::fnan();
        }
        else if (left > right)
        {
            // 所有图元都在右侧
            right = rightOrig;
            if (prevAxis == axis && G3D::fuzzyEq(prevSplit, split)) {
                // 卡住了 - 创建叶子节点
                stats.updateLeaf(depth, right - left + 1);
                createNode(tempTree, nodeIndex, left, right);
                return;
            }
            if (clipR >= split) {
                // 继续在右半部分循环
                gridBox.lo[axis] = split;
                prevClip = clipR;
                wasLeft = false;
                continue;
            }
            gridBox.lo[axis] = split;
            prevClip = G3D::fnan();
        }
        else
        {
            // 实际进行了分割
            if (prevAxis != -1 && !isnan(prevClip))
            {
                // 第二次通过 - 创建之前的分割节点（因为它产生了空白区域）
                int nextIndex = tempTree.size();

                // 为子节点分配空间
                tempTree.push_back(0);
                tempTree.push_back(0);
                tempTree.push_back(0);

                if (wasLeft) {
                    // 创建只有左子树的节点
                    stats.updateInner();
                    tempTree[nodeIndex + 0] = (prevAxis << 30) | nextIndex;
                    tempTree[nodeIndex + 1] = floatToRawIntBits(prevClip);
                    tempTree[nodeIndex + 2] = floatToRawIntBits(G3D::finf());
                } else {
                    // 创建只有右子树的节点
                    stats.updateInner();
                    tempTree[nodeIndex + 0] = (prevAxis << 30) | (nextIndex - 3);
                    tempTree[nodeIndex + 1] = floatToRawIntBits(-G3D::finf());
                    tempTree[nodeIndex + 2] = floatToRawIntBits(prevClip);
                }

                // 统计未使用的叶子节点
                depth++;
                stats.updateLeaf(depth, 0);

                // 更新节点索引，继续处理
                nodeIndex = nextIndex;
            }
            break;
        }
    }

    // 计算子节点索引
    int nextIndex = tempTree.size();

    // 分配左子节点空间
    int nl = right - left + 1;  // 左侧图元数量
    int nr = rightOrig - (right + 1) + 1;  // 右侧图元数量
    if (nl > 0) {
        tempTree.push_back(0);
        tempTree.push_back(0);
        tempTree.push_back(0);
    } else
        nextIndex -= 3;

    // 分配右子节点空间
    if (nr > 0) {
        tempTree.push_back(0);
        tempTree.push_back(0);
        tempTree.push_back(0);
    }

    // 写入内部节点
    stats.updateInner();
    tempTree[nodeIndex + 0] = (axis << 30) | nextIndex;
    tempTree[nodeIndex + 1] = floatToRawIntBits(clipL);
    tempTree[nodeIndex + 2] = floatToRawIntBits(clipR);

    // 准备左右子节点的包围盒
    AABound gridBoxL(gridBox), gridBoxR(gridBox);
    AABound nodeBoxL(nodeBox), nodeBoxR(nodeBox);
    gridBoxL.hi[axis] = gridBoxR.lo[axis] = split;
    nodeBoxL.hi[axis] = clipL;
    nodeBoxR.lo[axis] = clipR;

    // 递归处理左右子树
    if (nl > 0)
        subdivide(left, right, tempTree, dat, gridBoxL, nodeBoxL, nextIndex, depth + 1, stats);
    else
        stats.updateLeaf(depth + 1, 0);

    if (nr > 0)
        subdivide(right + 1, rightOrig, tempTree, dat, gridBoxR, nodeBoxR, nextIndex + 3, depth + 1, stats);
    else
        stats.updateLeaf(depth + 1, 0);
}

/**
 * @brief 将 BIH 树写入文件
 *
 * 将构建好的 BIH 树结构序列化到二进制文件中，用于持久化存储和快速加载
 *
 * @param wf 写入文件的指针（必须已打开为二进制写入模式）
 * @return true 写入成功
 * @return false 写入失败
 *
 * @note 文件格式：[包围盒最小点(3个float)] [包围盒最大点(3个float)]
 *                 [树大小(uint32)] [树数据] [对象数量(uint32)] [对象索引数组]
 * @note 写入失败可能导致数据不完整，调用方应检查返回值
 */
bool BIH::writeToFile(FILE* wf) const
{
    uint32 treeSize = tree.size();
    uint32 check=0, count;

    // 写入包围盒边界
    check += fwrite(&bounds.low(), sizeof(float), 3, wf);
    check += fwrite(&bounds.high(), sizeof(float), 3, wf);

    // 写入树结构
    check += fwrite(&treeSize, sizeof(uint32), 1, wf);
    check += fwrite(&tree[0], sizeof(uint32), treeSize, wf);

    // 写入对象索引数组
    count = objects.size();
    check += fwrite(&count, sizeof(uint32), 1, wf);
    check += fwrite(&objects[0], sizeof(uint32), count, wf);

    // 验证写入的字段数量是否正确
    return check == (3 + 3 + 2 + treeSize + count);
}

/**
 * @brief 从文件读取 BIH 树
 *
 * 从二进制文件反序列化 BIH 树结构，用于快速加载预构建的碰撞数据
 *
 * @param rf 读取文件的指针（必须已打开为二进制读取模式）
 * @return true 读取成功
 * @return false 读取失败
 *
 * @note 文件格式：[包围盒最小点(3个float)] [包围盒最大点(3个float)]
 *                 [树大小(uint32)] [树数据] [对象数量(uint32)] [对象索引数组]
 * @note 读取失败时，树的状态可能不完整，调用方应丢弃当前树实例
 * @note 使用 uint64 比较，避免大文件时的整数溢出
 */
bool BIH::readFromFile(FILE* rf)
{
    uint32 treeSize;
    G3D::Vector3 lo, hi;
    uint32 check=0, count=0;

    // 读取包围盒边界
    check += fread(&lo, sizeof(float), 3, rf);
    check += fread(&hi, sizeof(float), 3, rf);
    bounds = G3D::AABox(lo, hi);

    // 读取树结构
    check += fread(&treeSize, sizeof(uint32), 1, rf);
    tree.resize(treeSize);
    check += fread(&tree[0], sizeof(uint32), treeSize, rf);

    // 读取对象索引数组
    check += fread(&count, sizeof(uint32), 1, rf);
    objects.resize(count);
    check += fread(&objects[0], sizeof(uint32), count, rf);

    // 使用 uint64 比较，避免大文件时的整数溢出
    return uint64(check) == uint64(3 + 3 + 1 + 1 + uint64(treeSize) + uint64(count));
}

/**
 * @brief 更新叶子节点统计信息
 *
 * 记录新创建的叶子节点的深度和对象数量统计信息
 *
 * @param depth 叶子节点的深度
 * @param n 叶子节点包含的图元数量
 *
 * @note 该函数在每个叶子节点创建时调用，用于收集构建统计信息
 * @note 统计信息包括：叶子节点数量、深度分布、对象数量分布等
 */
void BIH::BuildStats::updateLeaf(int depth, int n)
{
    numLeaves++;
    minDepth = std::min(depth, minDepth);
    maxDepth = std::max(depth, maxDepth);
    sumDepth += depth;
    minObjects = std::min(n, minObjects);
    maxObjects = std::max(n, maxObjects);
    sumObjects += n;

    // 统计包含不同数量对象的叶子节点分布（0, 1, 2, 3, 4, >4）
    int nl = std::min(n, 5);
    ++numLeavesN[nl];
}

/**
 * @brief 打印构建统计信息
 *
 * 输出 BIH 树构建后的详细统计信息，用于调试和性能分析
 *
 * @note 输出信息包括：
 *       - 内部节点和叶子节点数量
 *       - 对象数量的最小、平均、最大值
 *       - 深度的最小、平均、最大值
 *       - 叶子节点对象数量分布（百分比）
 *       - BVH2 节点数量和占比
 */
void BIH::BuildStats::printStats()
{
    printf("Tree stats:\n");
    printf("  * Nodes:          %d\n", numNodes);
    printf("  * Leaves:         %d\n", numLeaves);
    printf("  * Objects: min    %d\n", minObjects);
    printf("             avg    %.2f\n", (float) sumObjects / numLeaves);
    printf("           avg(n>0) %.2f\n", (float) sumObjects / (numLeaves - numLeavesN[0]));
    printf("             max    %d\n", maxObjects);
    printf("  * Depth:   min    %d\n", minDepth);
    printf("             avg    %.2f\n", (float) sumDepth / numLeaves);
    printf("             max    %d\n", maxDepth);

    // 输出叶子节点对象数量分布
    printf("  * Leaves w/: N=0  %3d%%\n", 100 * numLeavesN[0] / numLeaves);
    printf("               N=1  %3d%%\n", 100 * numLeavesN[1] / numLeaves);
    printf("               N=2  %3d%%\n", 100 * numLeavesN[2] / numLeaves);
    printf("               N=3  %3d%%\n", 100 * numLeavesN[3] / numLeaves);
    printf("               N=4  %3d%%\n", 100 * numLeavesN[4] / numLeaves);
    printf("               N>4  %3d%%\n", 100 * numLeavesN[5] / numLeaves);

    // 输出 BVH2 节点占比
    printf("  * BVH2 nodes:     %d (%3d%%)\n", numBVH2, 100 * numBVH2 / (numNodes + numLeaves - 2 * numBVH2));
}
