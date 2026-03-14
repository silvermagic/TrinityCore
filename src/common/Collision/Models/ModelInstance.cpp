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
 * @file ModelInstance.cpp
 * @brief 模型实例实现
 *
 * 本文件实现了模型实例的碰撞检测功能，是VMAP系统的核心组件。
 * 主要功能包括：
 * - 模型实例的初始化和空间变换计算
 * - 射线碰撞检测（将世界坐标射线转换到模型局部坐标系）
 * - 区域信息和位置信息查询
 * - 液体表面高度计算
 * - 模型生成信息的文件序列化
 *
 * 核心算法：
 * 1. 坐标变换：使用逆旋转矩阵和逆缩放，将世界坐标转换到模型局部坐标
 * 2. 碰撞检测：在模型局部坐标系中进行精确的三角形碰撞检测
 * 3. 结果变换：将碰撞距离和位置变换回世界坐标
 *
 * 性能优化：
 * - 预计算逆旋转矩阵和逆缩放，避免运行时矩阵求逆
 * - 使用AABox进行快速粗略碰撞剔除
 * - 使用BIH加速三角形碰撞检测
 */

#include "ModelInstance.h"
#include "WorldModel.h"
#include "MapTree.h"

using G3D::Vector3;
using G3D::Ray;

namespace VMAP
{
    /**
     * @brief 构造函数
     * @param spawn 模型生成信息
     * @param model 世界模型指针
     *
     * 根据生成信息计算逆旋转矩阵和逆缩放。
     *
     * 欧拉角转换：
     * - 文件中的旋转角度单位是度，需要转换为弧度
     * - 欧拉角顺序：ZYX（先绕Z轴，再绕Y轴，最后绕X轴）
     * - 计算逆矩阵用于将世界坐标转换到模型局部坐标
     */
    ModelInstance::ModelInstance(ModelSpawn const& spawn, WorldModel* model): ModelSpawn(spawn), iModel(model)
    {
        // 从欧拉角（度数）计算旋转矩阵，然后求逆
        // 欧拉角顺序：ZYX（yaw, pitch, roll）
        // 注意：iRot的顺序是(x, y, z)，但旋转顺序是ZYX
        iInvRot = G3D::Matrix3::fromEulerAnglesZYX(G3D::pif()*iRot.y/180.f, G3D::pif()*iRot.x/180.f, G3D::pif()*iRot.z/180.f).inverse();
        iInvScale = 1.f/iScale;  // 预计算逆缩放，避免运行时除法
    }

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
     * 2. 粗略碰撞检测：使用AABox进行快速剔除
     * 3. 坐标变换：将世界坐标射线转换到模型局部坐标
     *    - 射线起点：p' = iInvRot * (p - iPos) * iInvScale
     *    - 射线方向：d' = iInvRot * d
     * 4. 距离变换：maxDist' = maxDist * iInvScale
     * 5. 精确碰撞检测：在模型局部坐标系中检测
     * 6. 结果变换：将碰撞距离变换回世界坐标
     *
     * 性能优化：
     * - 先进行AABox碰撞检测，快速剔除大部分不碰撞的情况
     * - 将射线变换到模型空间，而不是变换模型的每个三角形
     */
    bool ModelInstance::intersectRay(G3D::Ray const& pRay, float& pMaxDist, bool pStopAtFirstHit, ModelIgnoreFlags ignoreFlags) const
    {
        // 检查模型是否已加载
        if (!iModel)
        {
            //std::cout << "<object not loaded>\n";
            return false;
        }

        // 粗略碰撞检测：射线与包围盒相交测试
        float time = pRay.intersectionTime(iBound);
        if (time == G3D::finf())
        {
//            std::cout << "Ray does not hit '" << name << "'\n";
            return false;
        }

//        std::cout << "Ray crosses bound of '" << name << "'\n";
/*        std::cout << "ray from:" << pRay.origin().x << ", " << pRay.origin().y << ", " << pRay.origin().z
                  << " dir:" << pRay.direction().x << ", " << pRay.direction().y << ", " << pRay.direction().z
                  << " t/tmax:" << time << '/' << pMaxDist;
        std::cout << "\nBound lo:" << iBound.low().x << ", " << iBound.low().y << ", " << iBound.low().z << " hi: "
                  << iBound.high().x << ", " << iBound.high().y << ", " << iBound.high().z << std::endl; */

        // 将射线从世界坐标变换到模型局部坐标
        // 子模型的包围盒定义在模型局部坐标系中
        Vector3 p = iInvRot * (pRay.origin() - iPos) * iInvScale;
        Ray modRay(p, iInvRot * pRay.direction());

        // 变换最大检测距离
        float distance = pMaxDist * iInvScale;

        // 在模型局部坐标系中进行精确碰撞检测
        bool hit = iModel->IntersectRay(modRay, distance, pStopAtFirstHit, ignoreFlags);

        if (hit)
        {
            // 将碰撞距离从模型局部坐标变换回世界坐标
            distance *= iScale;
            pMaxDist = distance;
        }
        return hit;
    }

    /**
     * @brief 点与模型的区域信息查询
     * @param p 查询点的世界坐标
     * @param info 输出：区域信息
     *
     * 查询点所在的区域信息，主要用于WMO模型（建筑物）。
     * M2模型通常不包含区域信息。
     *
     * 算法流程：
     * 1. 检查模型是否加载
     * 2. 检查模型类型，M2模型不包含区域信息
     * 3. 包围盒检查，快速剔除
     * 4. 将点变换到模型局部坐标
     * 5. 向下发射射线，寻找地面交点
     * 6. 将地面高度变换回世界坐标
     * 7. 如果此地面高于之前找到的地面，更新区域信息
     *
     * 使用场景：
     * - 玩家进入建筑物时确定区域ID
     * - 计算玩家在地形上的准确高度
     */
    void ModelInstance::intersectPoint(const G3D::Vector3& p, AreaInfo &info) const
    {
        if (!iModel)
        {
#ifdef VMAP_DEBUG
            std::cout << "<object not loaded>\n";
#endif
            return;
        }

        // M2文件不包含区域信息，只有WMO文件包含
        if (flags & MOD_M2)
            return;

        // 包围盒检查，快速剔除
        if (!iBound.contains(p))
            return;

        // 将点变换到模型局部坐标系
        // 子模型的包围盒定义在模型局部坐标系中
        Vector3 pModel = iInvRot * (p - iPos) * iInvScale;

        // 将向下方向变换到模型局部坐标系
        Vector3 zDirModel = iInvRot * Vector3(0.f, 0.f, -1.f);

        float zDist;
        // 在模型局部坐标系中向下发射射线，查找地面
        if (iModel->IntersectPoint(pModel, zDirModel, zDist, info))
        {
            // 计算模型局部坐标系中的地面点
            Vector3 modelGround = pModel + zDist * zDirModel;

            // 将地面高度变换回世界坐标
            // 注意矩阵运算顺序：Mat * vec == vec * Mat.transpose()
            // 对于旋转矩阵：Mat.inverse() == Mat.transpose()
            float world_Z = ((modelGround * iInvRot) * iScale + iPos).z;

            // 如果这个地面对象比之前找到的更高，更新区域信息
            // 这确保了多个重叠模型时返回最上面的地面
            if (info.ground_Z < world_Z)
            {
                info.ground_Z = world_Z;
                info.adtId = adtId;
            }
        }
    }

    /**
     * @brief 获取点的位置信息
     * @param p 查询点的世界坐标
     * @param info 输出：位置信息
     * @return true 如果成功获取位置信息
     *
     * 与intersectPoint类似，但返回更详细的位置信息，包括具体的模型引用。
     * 该引用可用于后续的液体查询等操作。
     */
    bool ModelInstance::GetLocationInfo(const G3D::Vector3& p, LocationInfo &info) const
    {
        if (!iModel)
        {
#ifdef VMAP_DEBUG
            std::cout << "<object not loaded>\n";
#endif
            return false;
        }

        // M2文件不包含区域信息，只有WMO文件包含
        if (flags & MOD_M2)
            return false;

        // 包围盒检查
        if (!iBound.contains(p))
            return false;

        // 将点变换到模型局部坐标系
        Vector3 pModel = iInvRot * (p - iPos) * iInvScale;
        Vector3 zDirModel = iInvRot * Vector3(0.f, 0.f, -1.f);

        float zDist;
        // 查询位置信息
        if (iModel->GetLocationInfo(pModel, zDirModel, zDist, info))
        {
            // 计算世界坐标地面高度
            Vector3 modelGround = pModel + zDist * zDirModel;
            float world_Z = ((modelGround * iInvRot) * iScale + iPos).z;

            // 更新最高的地面信息
            if (info.ground_Z < world_Z) // 这是否可以在交点时通过zDist自动处理？
            {
                info.ground_Z = world_Z;
                info.hitInstance = this;  // 记录碰撞的模型实例，用于后续查询
                return true;
            }
        }
        return false;
    }

    /**
     * @brief 获取液体表面高度
     * @param p 查询点的世界坐标
     * @param info 位置信息（包含模型引用）
     * @param liqHeight 输出：液体表面高度
     * @return true 如果该位置有液体
     *
     * 计算指定位置的液体表面高度。
     * 用于玩家游泳判定、船只浮力计算等。
     *
     * 注意：假设WMO模型没有倾斜（液体表面是水平的）
     */
    bool ModelInstance::GetLiquidLevel(const G3D::Vector3& p, LocationInfo &info, float &liqHeight) const
    {
        // 将点变换到模型局部坐标系
        // 子模型的包围盒定义在模型局部坐标系中
        Vector3 pModel = iInvRot * (p - iPos) * iInvScale;

        float zDist;
        // 查询液体高度（在模型局部坐标系中）
        if (info.hitModel->GetLiquidLevel(pModel, zDist))
        {
            // 计算世界坐标液体高度（zDist在模型坐标中）
            // 假设WMO没有倾斜（否则没有太大意义）
            liqHeight = zDist * iScale + iPos.z;
            return true;
        }
        return false;
    }

    /**
     * @brief 从文件读取模型生成信息
     * @param rf 文件指针（已打开）
     * @param spawn 输出：模型生成信息
     * @return true 如果读取成功
     *
     * 文件格式：
     * - uint32 flags: 模型标志位
     * - uint16 adtId: ADT瓦片ID
     * - uint32 ID: 模型唯一标识符
     * - float[3] iPos: 世界坐标位置
     * - float[3] iRot: 欧拉角旋转（度）
     * - float iScale: 缩放比例
     * - 如果flags & MOD_HAS_BOUND:
     *   - float[3] iBound.low: 包围盒下界
     *   - float[3] iBound.high: 包围盒上界
     * - uint32 nameLen: 模型名称长度
     * - char[nameLen] name: 模型名称
     *
     * 错误处理：
     * - 检查文件读取错误
     * - 验证字段数量
     * - 检查文件名长度（不超过500字符）
     */
    bool ModelSpawn::readFromFile(FILE* rf, ModelSpawn &spawn)
    {
        uint32 check = 0, nameLen;

        // 读取基本字段
        check += fread(&spawn.flags, sizeof(uint32), 1, rf);

        // 检查文件结束
        if (!check)
        {
            if (ferror(rf))
                std::cout << "Error reading ModelSpawn!\n";
            return false;
        }

        // 继续读取其他字段
        check += fread(&spawn.adtId, sizeof(uint16), 1, rf);
        check += fread(&spawn.ID, sizeof(uint32), 1, rf);
        check += fread(&spawn.iPos, sizeof(float), 3, rf);
        check += fread(&spawn.iRot, sizeof(float), 3, rf);
        check += fread(&spawn.iScale, sizeof(float), 1, rf);

        // 检查是否有预计算的包围盒（仅WMO模型）
        bool has_bound = (spawn.flags & MOD_HAS_BOUND) != 0;
        if (has_bound) // 只有WMO在MPQ中有包围盒，仅在计算后可用
        {
            Vector3 bLow, bHigh;
            check += fread(&bLow, sizeof(float), 3, rf);
            check += fread(&bHigh, sizeof(float), 3, rf);
            spawn.iBound = G3D::AABox(bLow, bHigh);
        }

        // 读取模型名称
        check += fread(&nameLen, sizeof(uint32), 1, rf);

        // 验证读取的字段数量
        if (check != uint32(has_bound ? 17 : 11))
        {
            std::cout << "Error reading ModelSpawn!\n";
            return false;
        }

        // 读取名称字符串
        char nameBuff[500];
        if (nameLen > 500) // 文件名不应该这么长，一定是文件错误
        {
            std::cout << "Error reading ModelSpawn, file name too long!\n";
            return false;
        }

        check = fread(nameBuff, sizeof(char), nameLen, rf);
        if (check != nameLen)
        {
            std::cout << "Error reading ModelSpawn!\n";
            return false;
        }

        spawn.name = std::string(nameBuff, nameLen);
        return true;
    }

    /**
     * @brief 将模型生成信息写入文件
     * @param wf 文件指针（已打开）
     * @param spawn 模型生成信息
     * @return true 如果写入成功
     *
     * 文件格式与readFromFile对应。
     */
    bool ModelSpawn::writeToFile(FILE* wf, ModelSpawn const& spawn)
    {
        uint32 check=0;

        // 写入基本字段
        check += fwrite(&spawn.flags, sizeof(uint32), 1, wf);
        check += fwrite(&spawn.adtId, sizeof(uint16), 1, wf);
        check += fwrite(&spawn.ID, sizeof(uint32), 1, wf);
        check += fwrite(&spawn.iPos, sizeof(float), 3, wf);
        check += fwrite(&spawn.iRot, sizeof(float), 3, wf);
        check += fwrite(&spawn.iScale, sizeof(float), 1, wf);

        // 写入包围盒（仅WMO模型有）
        bool has_bound = (spawn.flags & MOD_HAS_BOUND) != 0;
        if (has_bound) // 只有WMO在MPQ中有包围盒，仅在计算后可用
        {
            check += fwrite(&spawn.iBound.low(), sizeof(float), 3, wf);
            check += fwrite(&spawn.iBound.high(), sizeof(float), 3, wf);
        }

        // 写入模型名称
        uint32 nameLen = spawn.name.length();
        check += fwrite(&nameLen, sizeof(uint32), 1, wf);

        // 验证写入的字段数量
        if (check != uint32(has_bound ? 17 : 11)) return false;

        // 写入名称字符串
        check = fwrite(spawn.name.c_str(), sizeof(char), nameLen, wf);
        if (check != nameLen) return false;

        return true;
    }

}
