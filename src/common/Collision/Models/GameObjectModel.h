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
 * @file GameObjectModel.h
 * @brief 游戏对象碰撞模型定义
 *
 * 本文件定义了游戏对象(GameObject)的碰撞检测模型，是VMAP(虚拟地图)系统的核心组件之一。
 * 主要负责管理游戏世界中动态物体的碰撞检测，包括：
 * - 游戏对象的位置、旋转、缩放变换
 * - 与射线的碰撞检测
 * - 区域信息和位置信息的查询
 * - 液体表面高度计算
 *
 * 游戏对象模型可以是M2模型（如树木、岩石等装饰物）或WMO模型（建筑物）。
 * 这些模型支持相位系统，可以根据玩家的相位掩码显示或隐藏。
 *
 * 性能考虑：
 * - 使用轴对齐包围盒(AABox)进行快速粗略碰撞检测
 * - 通过BIH(边界区间层次)结构加速三角形网格碰撞检测
 * - 支持射线提前终止以优化视距检测
 */

#ifndef _GAMEOBJECT_MODEL_H
#define _GAMEOBJECT_MODEL_H

#include <G3D/Matrix3.h>
#include <G3D/Vector3.h>
#include <G3D/AABox.h>
#include <G3D/Ray.h>

#include "Define.h"
#include <memory>

namespace VMAP
{
    class WorldModel;
    struct AreaInfo;
    struct LocationInfo;
    enum class ModelIgnoreFlags : uint32;
}

class GameObject;
struct GameObjectDisplayInfoEntry;

/**
 * @class GameObjectModelOwnerBase
 * @brief 游戏对象模型所有者的抽象基类
 *
 * 定义了游戏对象模型所有者必须实现的接口。
 * 采用接口设计模式，将GameObjectModel与具体的游戏对象类型解耦。
 * 这样可以让不同的游戏对象类型（如GameObject、Creature等）复用碰撞检测逻辑。
 *
 * 主要职责：
 * - 提供游戏对象的基本属性（位置、朝向、缩放等）
 * - 提供对象的显示ID和相位掩码
 * - 提供调试可视化接口
 */
class TC_COMMON_API GameObjectModelOwnerBase
{
public:
    virtual ~GameObjectModelOwnerBase() = default;

    /**
     * @brief 检查对象是否已生成
     * @return true 如果对象已生成并在世界中可见
     *
     * 用于碰撞检测时过滤未生成的对象
     */
    virtual bool IsSpawned() const = 0;

    /**
     * @brief 获取对象的显示ID
     * @return 显示ID，用于查找对应的模型数据
     *
     * 显示ID对应GameObjectDisplayInfo.dbc中的条目
     */
    virtual uint32 GetDisplayId() const = 0;

    /**
     * @brief 获取对象的相位掩码
     * @return 相位掩码，用于相位系统判断对象可见性
     *
     * 相位掩码决定了哪些玩家可以看到这个对象
     */
    virtual uint32 GetPhaseMask() const = 0;

    /**
     * @brief 获取对象的世界坐标位置
     * @return 三维世界坐标向量
     */
    virtual G3D::Vector3 GetPosition() const = 0;

    /**
     * @brief 获取对象的朝向角度
     * @return 朝向角度（弧度）
     *
     * 朝向角度用于计算模型的旋转矩阵
     */
    virtual float GetOrientation() const = 0;

    /**
     * @brief 获取对象的缩放比例
     * @return 缩放比例（1.0为正常大小）
     *
     * 缩放比例影响碰撞体的包围盒大小
     */
    virtual float GetScale() const = 0;

    /**
     * @brief 调试可视化角点
     * @param corner 包围盒角点的世界坐标
     *
     * 用于调试时可视化显示包围盒的角点
     */
    virtual void DebugVisualizeCorner(G3D::Vector3 const& /*corner*/) const = 0;
};

/**
 * @class GameObjectModel
 * @brief 游戏对象碰撞模型
 *
 * 表示一个游戏对象的碰撞模型实例，封装了模型的几何数据、变换信息和碰撞检测功能。
 * 支持M2模型和WMO模型两种类型。
 *
 * 主要职责：
 * - 管理模型的包围盒和变换矩阵
 * - 提供射线碰撞检测接口
 * - 提供区域信息和位置信息查询
 * - 支持液体表面高度计算
 * - 支持相位系统，根据相位掩码控制碰撞检测
 *
 * 变换处理：
 * 模型数据在局部坐标系中定义，需要进行以下变换到世界坐标系：
 * 1. 缩放变换（scale）
 * 2. 旋转变换（orientation -> 旋转矩阵）
 * 3. 平移变换（position）
 *
 * 性能优化：
 * - 使用逆矩阵和逆缩放将世界坐标射线转换到模型局部坐标系
 * - 使用AABox进行快速粗略碰撞剔除
 * - 使用BIH加速三角形网格碰撞检测
 */
class TC_COMMON_API GameObjectModel /*, public Intersectable*/
{
    GameObjectModel() : phasemask(0), iInvScale(0), iScale(0), iModel(nullptr), isWmo(false) { }
public:
    std::string name;  ///< 模型名称，用于调试和日志记录

    /**
     * @brief 获取模型的世界空间包围盒
     * @return 轴对齐包围盒的常量引用
     *
     * 包围盒已经考虑了缩放、旋转和平移变换，在世界坐标系中表示。
     * 用于快速碰撞剔除和视锥体剔除。
     */
    const G3D::AABox& getBounds() const { return iBound; }

    ~GameObjectModel();

    /**
     * @brief 获取模型的世界坐标位置
     * @return 世界坐标位置向量
     */
    const G3D::Vector3& getPosition() const { return iPos;}

    /**
     * @brief 禁用碰撞检测
     *
     * 将相位掩码设置为0，使该模型不参与任何碰撞检测。
     * 用于临时禁用对象碰撞，如对象被销毁时。
     */
    void disable() { phasemask = 0;}

    /**
     * @brief 启用碰撞检测
     * @param ph_mask 相位掩码
     *
     * 设置相位掩码以启用碰撞检测。
     * 只有当查询的相位掩码与此掩码有交集时，碰撞才有效。
     */
    void enable(uint32 ph_mask) { phasemask = ph_mask;}

    /**
     * @brief 检查碰撞检测是否启用
     * @return true 如果启用了碰撞检测
     */
    bool isEnabled() const {return phasemask != 0;}

    /**
     * @brief 检查是否为世界模型对象(WMO)
     * @return true 如果是WMO模型
     *
     * WMO模型通常包含室内场景和复杂建筑物，M2模型通常是单个装饰物。
     * WMO模型包含区域信息，M2模型通常不包含。
     */
    bool isMapObject() const { return isWmo; }

    /**
     * @brief 射线碰撞检测
     * @param Ray 待检测的射线
     * @param MaxDist 输入：最大检测距离；输出：实际碰撞距离
     * @param StopAtFirstHit 是否在首次碰撞时停止
     * @param ph_mask 相位掩码，用于过滤对象
     * @param ignoreFlags 忽略标志，用于忽略特定类型模型
     * @return true 如果射线与模型相交
     *
     * 这是主要的碰撞检测方法，用于视距检测、移动碰撞检测等。
     *
     * 性能考虑：
     * - 首先检测AABox，快速剔除不相交的情况
     * - 使用逆变换将射线转换到模型局部坐标系
     * - 调用WorldModel进行精确的三角形碰撞检测
     */
    bool intersectRay(const G3D::Ray& Ray, float& MaxDist, bool StopAtFirstHit, uint32 ph_mask, VMAP::ModelIgnoreFlags ignoreFlags) const;

    /**
     * @brief 点与模型的区域信息查询
     * @param point 查询点的世界坐标
     * @param info 输出：区域信息结构体
     * @param ph_mask 相位掩码
     *
     * 查询点所在的区域信息，如地面高度、区域ID等。
     * 仅对WMO模型有效，M2模型不包含区域信息。
     *
     * 使用场景：
     * - 玩家进入建筑物时获取室内区域信息
     * - 计算玩家在地形上的高度
     */
    void intersectPoint(G3D::Vector3 const& point, VMAP::AreaInfo& info, uint32 ph_mask) const;

    /**
     * @brief 获取点的位置信息
     * @param point 查询点的世界坐标
     * @param info 输出：位置信息结构体
     * @param ph_mask 相位掩码
     * @return true 如果成功获取位置信息
     *
     * 类似intersectPoint，但返回更详细的位置信息。
     * 包含具体的模型引用，用于后续的液体查询等。
     */
    bool GetLocationInfo(G3D::Vector3 const& point, VMAP::LocationInfo& info, uint32 ph_mask) const;

    /**
     * @brief 获取液体表面高度
     * @param point 查询点的世界坐标
     * @param info 位置信息（需要先通过GetLocationInfo获取）
     * @param liqHeight 输出：液体表面高度
     * @return true 如果该位置有液体
     *
     * 计算指定位置的液体表面高度。
     * 用于玩家游泳、船只浮力等场景。
     *
     * 注意：调用前需要确保info.hitModel有效
     */
    bool GetLiquidLevel(G3D::Vector3 const& point, VMAP::LocationInfo& info, float& liqHeight) const;

    /**
     * @brief 创建游戏对象模型实例
     * @param modelOwner 模型所有者（拥有游戏对象属性）
     * @param dataPath 数据文件路径
     * @return 创建的模型实例指针，失败返回nullptr
     *
     * 工厂方法，创建并初始化游戏对象模型。
     *
     * 初始化流程：
     * 1. 根据displayId查找模型数据
     * 2. 加载WorldModel几何数据
     * 3. 计算变换矩阵和包围盒
     * 4. 存储所有者引用
     *
     * @note 返回的对象需要调用者负责删除
     */
    static GameObjectModel* Create(std::unique_ptr<GameObjectModelOwnerBase> modelOwner, std::string const& dataPath);

    /**
     * @brief 更新模型位置
     * @return true 如果更新成功
     *
     * 当游戏对象移动或旋转时，需要调用此方法更新碰撞模型。
     * 重新计算变换矩阵和包围盒。
     *
     * 性能考虑：这是一个相对昂贵的操作，仅在必要时调用。
     */
    bool UpdatePosition();

private:
    /**
     * @brief 初始化游戏对象模型
     * @param modelOwner 模型所有者
     * @param dataPath 数据文件路径
     * @return true 如果初始化成功
     *
     * 内部初始化方法，由Create调用。
     */
    bool initialize(std::unique_ptr<GameObjectModelOwnerBase> modelOwner, std::string const& dataPath);

    uint32 phasemask;           ///< 相位掩码，控制对象的可见性和碰撞
    G3D::AABox iBound;          ///< 世界空间包围盒，用于快速碰撞剔除
    G3D::Matrix3 iInvRot;       ///< 逆旋转矩阵，用于将世界坐标转换到模型局部坐标
    G3D::Vector3 iPos;          ///< 世界坐标位置
    float iInvScale;            ///< 逆缩放比例，用于坐标变换
    float iScale;               ///< 缩放比例
    VMAP::WorldModel* iModel;   ///< 指向世界模型的指针，包含实际的几何数据
    std::unique_ptr<GameObjectModelOwnerBase> owner;  ///< 模型所有者，提供游戏对象属性
    bool isWmo;                 ///< 是否为WMO模型（世界模型对象）
};

/**
 * @brief 加载游戏对象模型列表
 * @param dataPath 数据文件路径
 *
 * 从vmaps/GameObjectModels.dtree文件加载所有游戏对象模型的基本信息。
 * 包括模型名称、包围盒和类型信息。
 *
 * 此函数应在服务器启动时调用一次，加载的模型列表用于后续的游戏对象模型创建。
 *
 * 性能考虑：
 * - 文件读取是阻塞操作，应在启动阶段完成
 * - 加载的模型列表存储在全局变量中，所有地图共享
 */
TC_COMMON_API void LoadGameObjectModelList(std::string const& dataPath);

#endif // _GAMEOBJECT_MODEL_H
