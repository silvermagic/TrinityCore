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
 * @file GameObjectModel.cpp
 * @brief 游戏对象碰撞模型实现
 *
 * 本文件实现了游戏对象的碰撞检测模型，是虚拟地图系统(VMAP)的核心组件。
 * 主要功能包括：
 * - 游戏对象模型的加载和管理
 * - 射线碰撞检测
 * - 区域信息和位置信息查询
 * - 液体表面高度计算
 * - 模型位置更新
 *
 * 关键技术点：
 * 1. 坐标变换：将世界坐标转换到模型局部坐标进行碰撞检测
 * 2. 包围盒计算：考虑缩放和旋转，生成世界空间包围盒
 * 3. 相位系统：根据相位掩码过滤碰撞对象
 * 4. 性能优化：使用AABox快速剔除，BIH加速三角形检测
 */

#include "GameObjectModel.h"
#include "Log.h"
#include "MapTree.h"
#include "Timer.h"
#include "VMapDefinitions.h"
#include "VMapFactory.h"
#include "VMapManager2.h"
#include "WorldModel.h"

using G3D::Vector3;
using G3D::Ray;
using G3D::AABox;

/**
 * @struct GameobjectModelData
 * @brief 游戏对象模型的基本数据
 *
 * 存储游戏对象模型的元数据，从GameObjectModels.dtree文件加载。
 * 这些数据是只读的，所有游戏对象实例共享。
 */
struct GameobjectModelData
{
    /**
     * @brief 构造函数
     * @param name_ 模型名称
     * @param nameLength 名称长度
     * @param lowBound 包围盒下界
     * @param highBound 包围盒上界
     * @param isWmo_ 是否为WMO模型
     */
    GameobjectModelData(char const* name_, uint32 nameLength, Vector3 const& lowBound, Vector3 const& highBound, bool isWmo_) :
        bound(lowBound, highBound), name(name_, nameLength), isWmo(isWmo_) { }

    AABox bound;       ///< 模型局部坐标系的包围盒
    std::string name;  ///< 模型文件名
    bool isWmo;        ///< 是否为WMO模型（世界模型对象）
};

typedef std::unordered_map<uint32, GameobjectModelData> ModelList;
ModelList model_list;  ///< 全局模型列表，displayId到模型数据的映射

/**
 * @brief 加载游戏对象模型列表
 * @param dataPath 数据文件路径
 *
 * 从GameObjectModels.dtree文件加载所有游戏对象模型的元数据。
 * 该文件由地图提取工具生成，包含每个displayId对应的模型名称、包围盒和类型。
 *
 * 文件格式：
 * - 8字节魔数（VMAP_MAGIC）
 * - 重复的记录：
 *   - uint32 displayId：显示ID
 *   - uint8 isWmo：是否为WMO模型
 *   - uint32 name_length：模型名称长度
 *   - char[name_length]：模型名称
 *   - Vector3 v1：包围盒下界
 *   - Vector3 v2：包围盒上界
 *
 * 调用时机：服务器启动时调用一次
 * 性能考虑：阻塞操作，需要读取磁盘文件
 */
void LoadGameObjectModelList(std::string const& dataPath)
{
    uint32 oldMSTime = getMSTime();

    // 打开模型列表文件
    FILE* model_list_file = fopen((dataPath + "vmaps/" + VMAP::GAMEOBJECT_MODELS).c_str(), "rb");
    if (!model_list_file)
    {
        TC_LOG_ERROR("misc", "Unable to open '{}' file.", VMAP::GAMEOBJECT_MODELS);
        return;
    }

    // 验证文件魔数，确保文件格式正确
    char magic[8];
    if (fread(magic, 1, 8, model_list_file) != 8
        || memcmp(magic, VMAP::VMAP_MAGIC, 8) != 0)
    {
        TC_LOG_ERROR("misc", "File '{}' has wrong header, expected {}.", VMAP::GAMEOBJECT_MODELS, VMAP::VMAP_MAGIC);
        fclose(model_list_file);
        return;
    }

    uint32 name_length, displayId;
    uint8 isWmo;
    char buff[500];

    // 循环读取所有模型记录
    while (true)
    {
        Vector3 v1, v2;

        // 读取displayId，如果到达文件末尾则退出
        if (fread(&displayId, sizeof(uint32), 1, model_list_file) != 1)
            if (feof(model_list_file))  // EOF标志只在读取失败后设置
                break;

        // 读取模型类型、名称和包围盒
        if (fread(&isWmo, sizeof(uint8), 1, model_list_file) != 1
            || fread(&name_length, sizeof(uint32), 1, model_list_file) != 1
            || name_length >= sizeof(buff)
            || fread(&buff, sizeof(char), name_length, model_list_file) != name_length
            || fread(&v1, sizeof(Vector3), 1, model_list_file) != 1
            || fread(&v2, sizeof(Vector3), 1, model_list_file) != 1)
        {
            TC_LOG_ERROR("misc", "File '{}' seems to be corrupted!", VMAP::GAMEOBJECT_MODELS);
            break;
        }

        // 验证包围盒数据有效性，跳过无效数据
        if (v1.isNaN() || v2.isNaN())
        {
            TC_LOG_ERROR("misc", "File '{}' Model '{}' has invalid v1{} v2{} values!", VMAP::GAMEOBJECT_MODELS, std::string(buff, name_length), v1.toString(), v2.toString());
            continue;
        }

        // 将模型数据添加到全局列表中
        model_list.emplace(std::piecewise_construct, std::forward_as_tuple(displayId), std::forward_as_tuple(&buff[0], name_length, v1, v2, isWmo != 0));
    }

    fclose(model_list_file);
    TC_LOG_INFO("server.loading", ">> Loaded {} GameObject models in {} ms", uint32(model_list.size()), GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 析构函数
 *
 * 释放模型实例的引用计数。
 * 如果这是最后一个引用，VMapManager会卸载模型数据。
 */
GameObjectModel::~GameObjectModel()
{
    if (iModel)
        VMAP::VMapFactory::createOrGetVMapManager()->releaseModelInstance(name);
}

/**
 * @brief 初始化游戏对象模型
 * @param modelOwner 模型所有者
 * @param dataPath 数据文件路径
 * @return true 如果初始化成功
 *
 * 初始化流程：
 * 1. 根据displayId查找模型元数据
 * 2. 加载WorldModel几何数据
 * 3. 计算旋转矩阵和逆矩阵
 * 4. 计算世界空间包围盒（考虑缩放和旋转）
 * 5. 存储所有者引用
 *
 * 包围盒计算步骤：
 * 1. 缩放原始包围盒
 * 2. 对8个角点应用旋转变换
 * 3. 计算新的轴对齐包围盒
 * 4. 平移到世界位置
 */
bool GameObjectModel::initialize(std::unique_ptr<GameObjectModelOwnerBase> modelOwner, std::string const& dataPath)
{
    // 根据displayId查找模型元数据
    ModelList::const_iterator it = model_list.find(modelOwner->GetDisplayId());
    if (it == model_list.end())
        return false;

    G3D::AABox mdl_box(it->second.bound);

    // 忽略零包围盒的模型（无效模型）
    if (mdl_box == G3D::AABox::zero())
    {
        TC_LOG_ERROR("misc", "GameObject model {} has zero bounds, loading skipped", it->second.name);
        return false;
    }

    // 从VMapManager获取模型实例（引用计数）
    iModel = VMAP::VMapFactory::createOrGetVMapManager()->acquireModelInstance(dataPath + "vmaps/", it->second.name);

    if (!iModel)
        return false;

    // 保存模型基本信息
    name = it->second.name;
    iPos = modelOwner->GetPosition();
    phasemask = modelOwner->GetPhaseMask();
    iScale = modelOwner->GetScale();
    iInvScale = 1.f / iScale;  // 预计算逆缩放，避免运行时除法

    // 计算旋转矩阵和逆旋转矩阵
    // 欧拉角顺序：ZYX（先绕Z轴，再绕Y轴，最后绕X轴）
    // 游戏对象通常只有Z轴旋转（偏航角）
    G3D::Matrix3 iRotation = G3D::Matrix3::fromEulerAnglesZYX(modelOwner->GetOrientation(), 0, 0);
    iInvRot = iRotation.inverse();  // 逆矩阵用于将世界坐标转换到模型局部坐标

    // 计算变换后的包围盒
    // 步骤1：缩放原始包围盒
    mdl_box = AABox(mdl_box.low() * iScale, mdl_box.high() * iScale);

    // 步骤2：对8个角点应用旋转，计算新的轴对齐包围盒
    AABox rotated_bounds;
    for (int i = 0; i < 8; ++i)
        rotated_bounds.merge(iRotation * mdl_box.corner(i));

    // 步骤3：平移到世界位置
    iBound = rotated_bounds + iPos;

#ifdef SPAWN_CORNERS
    // 调试代码：可视化包围盒的8个角点
    for (int i = 0; i < 8; ++i)
    {
        Vector3 pos(iBound.corner(i));
        modelOwner->DebugVisualizeCorner(pos);
    }
#endif

    owner = std::move(modelOwner);
    isWmo = it->second.isWmo;
    return true;
}

/**
 * @brief 创建游戏对象模型（工厂方法）
 * @param modelOwner 模型所有者
 * @param dataPath 数据文件路径
 * @return 创建的模型实例，失败返回nullptr
 */
GameObjectModel* GameObjectModel::Create(std::unique_ptr<GameObjectModelOwnerBase> modelOwner, std::string const& dataPath)
{
    GameObjectModel* mdl = new GameObjectModel();
    if (!mdl->initialize(std::move(modelOwner), dataPath))
    {
        delete mdl;
        return nullptr;
    }

    return mdl;
}

/**
 * @brief 射线碰撞检测
 * @param ray 待检测的射线
 * @param MaxDist 输入：最大检测距离；输出：实际碰撞距离
 * @param StopAtFirstHit 是否在首次碰撞时停止
 * @param ph_mask 相位掩码
 * @param ignoreFlags 忽略标志
 * @return true 如果射线与模型相交
 *
 * 碰撞检测流程：
 * 1. 相位检查：确保对象在查询的相位中可见
 * 2. 生成检查：确保对象已生成
 * 3. 粗略碰撞检测：使用AABox快速剔除
 * 4. 坐标变换：将世界坐标射线转换到模型局部坐标
 * 5. 精确碰撞检测：调用WorldModel检测三角形碰撞
 *
 * 坐标变换原理：
 * 世界坐标 -> 模型局部坐标：
 * 1. 平移：p' = p - iPos
 * 2. 逆旋转：p'' = iInvRot * p'
 * 3. 逆缩放：p''' = p'' * iInvScale
 *
 * 性能优化：
 * - 先进行AABox碰撞，快速剔除大部分不碰撞的情况
 * - 将射线变换到模型空间，而不是将模型变换到世界空间
 * - 避免了对每个三角形进行变换的开销
 */
bool GameObjectModel::intersectRay(const G3D::Ray& ray, float& MaxDist, bool StopAtFirstHit, uint32 ph_mask, VMAP::ModelIgnoreFlags ignoreFlags) const
{
    // 相位检查：对象的相位掩码必须与查询掩码有交集
    if (!(phasemask & ph_mask) || !owner->IsSpawned())
        return false;

    // 粗略碰撞检测：射线与包围盒相交测试
    float time = ray.intersectionTime(iBound);
    if (time == G3D::finf())
        return false;

    // 将射线从世界坐标变换到模型局部坐标
    // 子模型的包围盒定义在模型局部坐标系中
    Vector3 p = iInvRot * (ray.origin() - iPos) * iInvScale;
    Ray modRay(p, iInvRot * ray.direction());

    // 变换最大距离
    float distance = MaxDist * iInvScale;

    // 在模型局部坐标系中进行精确碰撞检测
    bool hit = iModel->IntersectRay(modRay, distance, StopAtFirstHit, ignoreFlags);

    if (hit)
    {
        // 将碰撞距离从模型局部坐标变换回世界坐标
        distance *= iScale;
        MaxDist = distance;
    }
    return hit;
}

/**
 * @brief 点与模型的区域信息查询
 * @param point 查询点的世界坐标
 * @param info 输出：区域信息
 * @param ph_mask 相位掩码
 *
 * 查询点所在的区域信息，主要用于WMO模型（建筑物）。
 * M2模型通常不包含区域信息。
 *
 * 算法流程：
 * 1. 相位和生成状态检查
 * 2. 包围盒检查，快速剔除
 * 3. 将点变换到模型局部坐标
 * 4. 向下发射射线，寻找地面交点
 * 5. 将地面高度变换回世界坐标
 * 6. 如果此地面高于之前找到的地面，更新区域信息
 *
 * 使用场景：
 * - 玩家进入建筑物时确定区域ID
 * - 计算玩家在地形上的准确高度
 */
void GameObjectModel::intersectPoint(G3D::Vector3 const& point, VMAP::AreaInfo& info, uint32 ph_mask) const
{
    // 前置条件检查
    if (!(phasemask & ph_mask) || !owner->IsSpawned() || !isMapObject())
        return;

    // 包围盒检查，快速剔除
    if (!iBound.contains(point))
        return;

    // 将点变换到模型局部坐标系
    // 子模型的包围盒定义在模型局部坐标系中
    Vector3 pModel = iInvRot * (point - iPos) * iInvScale;

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
            info.ground_Z = world_Z;
    }
}

/**
 * @brief 获取点的位置信息
 * @param point 查询点的世界坐标
 * @param info 输出：位置信息
 * @param ph_mask 相位掩码
 * @return true 如果成功获取位置信息
 *
 * 与intersectPoint类似，但返回更详细的位置信息，包括具体的模型引用。
 * 该引用可用于后续的液体查询等操作。
 */
bool GameObjectModel::GetLocationInfo(G3D::Vector3 const& point, VMAP::LocationInfo& info, uint32 ph_mask) const
{
    // 前置条件检查
    if (!(phasemask & ph_mask) || !owner->IsSpawned() || !isMapObject())
        return false;

    // 包围盒检查
    if (!iBound.contains(point))
        return false;

    // 将点变换到模型局部坐标系
    Vector3 pModel = iInvRot * (point - iPos) * iInvScale;
    Vector3 zDirModel = iInvRot * Vector3(0.f, 0.f, -1.f);

    float zDist;
    // 查询位置信息
    if (iModel->GetLocationInfo(pModel, zDirModel, zDist, info))
    {
        // 计算世界坐标地面高度
        Vector3 modelGround = pModel + zDist * zDirModel;
        float world_Z = ((modelGround * iInvRot) * iScale + iPos).z;

        // 更新最高的地面信息
        if (info.ground_Z < world_Z)
        {
            info.ground_Z = world_Z;
            return true;
        }
    }

    return false;
}

/**
 * @brief 获取液体表面高度
 * @param point 查询点的世界坐标
 * @param info 位置信息（包含模型引用）
 * @param liqHeight 输出：液体表面高度
 * @return true 如果该位置有液体
 *
 * 计算指定位置的液体表面高度。
 * 用于玩家游泳判定、船只浮力计算等。
 *
 * 注意：假设WMO模型没有倾斜（液体表面是水平的）
 */
bool GameObjectModel::GetLiquidLevel(G3D::Vector3 const& point, VMAP::LocationInfo& info, float& liqHeight) const
{
    // 将点变换到模型局部坐标系
    // 子模型的包围盒定义在模型局部坐标系中
    Vector3 pModel = iInvRot * (point - iPos) * iInvScale;

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
 * @brief 更新模型位置
 * @return true 如果更新成功
 *
 * 当游戏对象移动或旋转时，调用此方法更新碰撞模型的变换信息。
 * 重新计算：
 * - 世界坐标位置
 * - 旋转矩阵和逆矩阵
 * - 世界空间包围盒
 *
 * 调用时机：游戏对象移动、旋转或缩放时
 * 性能考虑：这是一个相对昂贵的操作，涉及包围盒重新计算
 */
bool GameObjectModel::UpdatePosition()
{
    if (!iModel)
        return false;

    // 获取模型元数据
    ModelList::const_iterator it = model_list.find(owner->GetDisplayId());
    if (it == model_list.end())
        return false;

    G3D::AABox mdl_box(it->second.bound);

    // 忽略零包围盒的模型
    if (mdl_box == G3D::AABox::zero())
    {
        TC_LOG_ERROR("misc", "GameObject model {} has zero bounds, loading skipped", it->second.name);
        return false;
    }

    // 更新世界坐标位置
    iPos = owner->GetPosition();

    // 重新计算旋转矩阵
    G3D::Matrix3 iRotation = G3D::Matrix3::fromEulerAnglesZYX(owner->GetOrientation(), 0, 0);
    iInvRot = iRotation.inverse();

    // 重新计算世界空间包围盒
    // 步骤1：缩放原始包围盒
    mdl_box = AABox(mdl_box.low() * iScale, mdl_box.high() * iScale);

    // 步骤2：对8个角点应用旋转
    AABox rotated_bounds;
    for (int i = 0; i < 8; ++i)
        rotated_bounds.merge(iRotation * mdl_box.corner(i));

    // 步骤3：平移到世界位置
    iBound = rotated_bounds + iPos;

#ifdef SPAWN_CORNERS
    // 调试代码：可视化包围盒的8个角点
    for (int i = 0; i < 8; ++i)
    {
        Vector3 pos(iBound.corner(i));
        owner->DebugVisualizeCorner(pos);
    }
#endif

    return true;
}
