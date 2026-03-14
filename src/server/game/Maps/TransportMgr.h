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
 * @file TransportMgr.h
 * @brief 运输工具管理器（TransportMgr）模块头文件
 *
 * 本模块实现了运输工具的集中管理系统，负责：
 *   - 加载和缓存运输工具模板数据
 *   - 管理运输工具的路径生成和预计算
 *   - 创建和维护运输工具实例
 *   - 处理大陆和副本运输工具的差异化加载
 *
 * 运输工具管理器使用单例模式，确保全局只有一个实例管理所有运输工具。
 * 路径数据在服务器启动时预计算并缓存，避免每次创建运输工具时重复计算。
 *
 * 主要数据结构：
 *   - KeyFrame: 路径关键帧，包含位置、时间、事件等信息
 *   - TransportTemplate: 运输工具模板，包含完整的路径数据
 *   - TransportAnimation: 用于动态运输工具的动画数据
 *
 * @see Transport 运输工具类
 * @see TransportTemplate 运输工具模板
 * @see KeyFrame 关键帧结构
 */

#ifndef TRANSPORTMGR_H
#define TRANSPORTMGR_H

#include "DBCStores.h"
#include "ObjectGuid.h"
#include <memory>

struct KeyFrame;
struct GameObjectTemplate;
struct TransportTemplate;
class Transport;
class Map;

namespace Movement
{
    template <typename length_type> class Spline;
}

// ==================== 类型定义 ====================

/**
 * @brief 运输工具样条曲线类型
 *
 * 使用双精度浮点数的样条曲线，用于平滑运输工具的移动路径。
 * 支持 Catmull-Rom 插值，生成自然弯曲的曲线。
 */
typedef Movement::Spline<double>                 TransportSpline;

/**
 * @brief 关键帧向量类型
 *
 * 存储运输工具路径的所有关键帧，按路径顺序排列。
 */
typedef std::vector<KeyFrame>                    KeyFrameVec;

/**
 * @brief 运输工具模板映射表
 *
 * 从运输工具模板ID到模板数据的映射，用于快速查找。
 * 键：游戏对象模板ID（entry）
 * 值：运输工具模板数据
 */
typedef std::unordered_map<uint32, TransportTemplate> TransportTemplates;

/**
 * @brief 运输工具集合类型
 *
 * 存储指向运输工具实例的指针集合。
 */
typedef std::set<Transport*>                     TransportSet;

/**
 * @brief 运输工具映射表
 *
 * 从地图ID到该地图上运输工具集合的映射。
 */
typedef std::unordered_map<uint32, TransportSet>      TransportMap;

/**
 * @brief 副本运输工具映射表
 *
 * 存储需要在副本中创建的运输工具模板ID。
 * 键：地图ID
 * 值：该地图需要创建的运输工具模板ID集合
 */
typedef std::unordered_map<uint32, std::set<uint32> > TransportInstanceMap;

/**
 * @struct KeyFrame
 * @brief 路径关键帧结构
 *
 * 关键帧定义了运输工具路径上的一个重要节点，包含：
 *   - 节点位置信息（从 TaxiPathNodeEntry）
 *   - 时间计算数据（到达时间、离开时间）
 *   - 距离计算数据（距离上一帧、下一帧的距离）
 *   - 运动学数据（加速、减速相关）
 *   - 样条曲线数据（用于平滑插值）
 *
 * 关键帧类型：
 *   - 普通帧：运输工具直接通过
 *   - 停靠帧：运输工具停留一段时间
 *   - 传送帧：运输工具跨地图传送
 */
struct KeyFrame
{
    /**
     * @brief 构造函数
     *
     * @param node 出租车路径节点数据（来自DBC）
     *
     * 初始化所有成员变量为默认值，实际数据在路径生成时填充。
     */
    explicit KeyFrame(TaxiPathNodeEntry const* node) : Index(0), Node(node), InitialOrientation(0.0f),
        DistSinceStop(-1.0f), DistUntilStop(-1.0f), DistFromPrev(-1.0f), TimeFrom(0.0f), TimeTo(0.0f),
        Teleport(false), ArriveTime(0), DepartureTime(0), Spline(nullptr), NextDistFromPrev(0.0f), NextArriveTime(0)
    {
    }

    // ==================== 基础数据 ====================

    /**
     * @brief 关键帧索引
     *
     * 在当前路径段中的索引位置，用于样条曲线计算。
     */
    uint32 Index;

    /**
     * @brief 节点数据指针
     *
     * 指向 DBC 中的出租车路径节点数据，包含位置、地图、标志等信息。
     * 生命周期由 DBC 存储管理。
     */
    TaxiPathNodeEntry const* Node;

    /**
     * @brief 初始朝向
     *
     * 运输工具到达此关键帧时的朝向角度（弧度）。
     * 通过样条曲线导数计算得出。
     */
    float InitialOrientation;

    // ==================== 距离数据 ====================

    /**
     * @brief 距上一个停靠点的距离
     *
     * 从上一个停靠点（或起点）到当前位置的累计距离。
     * 用于计算加速阶段的运动。
     */
    float DistSinceStop;

    /**
     * @brief 距下一个停靠点的距离
     *
     * 从当前位置到下一个停靠点的累计距离。
     * 用于计算减速阶段的运动。
     */
    float DistUntilStop;

    /**
     * @brief 距上一帧的距离
     *
     * 从上一个关键帧到当前关键帧的距离。
     */
    float DistFromPrev;

    // ==================== 时间数据 ====================

    /**
     * @brief 从停靠点出发后经过的时间
     *
     * 在运动学计算中使用，表示从最近停靠点出发后经过的时间。
     */
    float TimeFrom;

    /**
     * @brief 到达下一个停靠点的时间
     *
     * 到达下一个停靠点还需要的时间。
     */
    float TimeTo;

    // ==================== 状态标志 ====================

    /**
     * @brief 是否为传送帧
     *
     * true: 此帧需要跨地图传送
     * false: 普通移动帧
     */
    bool Teleport;

    /**
     * @brief 到达时间（毫秒）
     *
     * 从路径开始到此关键帧的到达时间。
     */
    uint32 ArriveTime;

    /**
     * @brief 离开时间（毫秒）
     *
     * 从路径开始到此关键帧的离开时间。
     * 对于停靠帧，ArriveTime + Delay = DepartureTime。
     */
    uint32 DepartureTime;

    /**
     * @brief 样条曲线指针
     *
     * 当前路径段使用的样条曲线，用于平滑位置插值。
     * 使用共享指针管理，同一路径段的多个帧共享同一样条。
     */
    std::shared_ptr<TransportSpline> Spline;

    // ==================== 下一帧数据 ====================

    /**
     * @brief 下一帧距当前帧的距离
     *
     * 从当前关键帧到下一个关键帧的距离。
     */
    float NextDistFromPrev;

    /**
     * @brief 下一帧的到达时间
     *
     * 下一个关键帧的到达时间（用于优化计算）。
     */
    uint32 NextArriveTime;

    // ==================== 辅助方法 ====================

    /**
     * @brief 检查是否为传送帧
     *
     * @return true 是传送帧
     * @return false 不是传送帧
     */
    bool IsTeleportFrame() const { return Teleport; }

    /**
     * @brief 检查是否为停靠帧
     *
     * @return true 是停靠帧（节点标志为2）
     * @return false 不是停靠帧
     *
     * 停靠帧会让运输工具停留一段时间（由 Node->Delay 定义）。
     */
    bool IsStopFrame() const { return Node->Flags == 2; }
};

/**
 * @struct TransportTemplate
 * @brief 运输工具模板结构
 *
 * 存储特定类型运输工具的所有静态数据：
 *   - 路径关键帧序列
 *   - 运动学参数（速度、加速度）
 *   - 使用的地图集合
 *   - 完整路径周期时间
 *
 * 模板数据在服务器启动时从数据库加载并计算，
 * 所有同类型的运输工具实例共享同一个模板。
 */
struct TransportTemplate
{
    /**
     * @brief 默认构造函数
     *
     * 初始化所有成员变量为默认值。
     */
    TransportTemplate() : inInstance(false), pathTime(0), accelTime(0.0f), accelDist(0.0f), entry(0) { }

    /**
     * @brief 析构函数
     */
    ~TransportTemplate();

    /**
     * @brief 使用的地图ID集合
     *
     * 记录此运输工具路径经过的所有地图ID。
     * 用于判断是否为跨地图运输工具。
     */
    std::set<uint32> mapsUsed;

    /**
     * @brief 是否在副本中
     *
     * true: 运输工具在副本地图中运行
     * false: 运输工具在大地图中运行
     *
     * 副本运输工具随副本创建和销毁，
     * 大地图运输工具在服务器启动时创建并持久存在。
     */
    bool inInstance;

    /**
     * @brief 完整路径周期时间（毫秒）
     *
     * 运输工具从起点出发，经过所有关键帧并返回起点的总时间。
     */
    uint32 pathTime;

    /**
     * @brief 关键帧向量
     *
     * 按顺序存储所有关键帧，构成完整的路径。
     */
    KeyFrameVec keyFrames;

    /**
     * @brief 加速到最大速度所需时间（秒）
     *
     * 从静止加速到最大速度需要的理论时间。
     */
    float accelTime;

    /**
     * @brief 加速阶段移动的距离
     *
     * 从静止加速到最大速度过程中移动的距离。
     */
    float accelDist;

    /**
     * @brief 运输工具模板ID
     *
     * 对应 gameobject_template 表中的 entry。
     */
    uint32 entry;
};

// ==================== 动画相关类型定义 ====================

/**
 * @brief 运输工具动画路径容器
 *
 * 从时间索引到动画节点的映射。
 * 用于动态运输工具（如电梯）的动画播放。
 */
typedef std::map<uint32, TransportAnimationEntry const*> TransportPathContainer;

/**
 * @brief 运输工具旋转路径容器
 *
 * 从时间索引到旋转数据的映射。
 * 用于运输工具在移动过程中的旋转动画。
 */
typedef std::map<uint32, TransportRotationEntry const*> TransportPathRotationContainer;

/**
 * @struct TransportAnimation
 * @brief 运输工具动画数据结构
 *
 * 存储动态运输工具的动画数据，包括：
 *   - 位置关键帧（用于插值位置）
 *   - 旋转关键帧（用于插值旋转）
 *   - 总动画时长
 *
 * 动画数据来自 DBC 文件，用于替代静态路径计算。
 */
struct TC_GAME_API TransportAnimation
{
    /**
     * @brief 默认构造函数
     */
    TransportAnimation() : TotalTime(0) { }

    /**
     * @brief 位置路径数据
     *
     * 从时间到位置的映射，用于插值计算运输工具位置。
     */
    TransportPathContainer Path;

    /**
     * @brief 旋转路径数据
     *
     * 从时间到旋转的映射，用于插值计算运输工具朝向。
     */
    TransportPathRotationContainer Rotations;

    /**
     * @brief 总动画时长
     *
     * 完整动画循环的总时间（毫秒）。
     */
    uint32 TotalTime;

    /**
     * @brief 获取指定时间的动画节点
     *
     * @param time 时间点（毫秒）
     * @return 动画节点指针，如果找不到返回 nullptr
     *
     * 使用 lower_bound 查找大于等于指定时间的第一个节点。
     */
    TransportAnimationEntry const* GetAnimNode(uint32 time) const;

    /**
     * @brief 获取指定时间的旋转数据
     *
     * @param time 时间点（毫秒）
     * @return 旋转数据指针，如果找不到返回 nullptr
     *
     * 使用 lower_bound 查找大于等于指定时间的第一个旋转节点。
     */
    TransportRotationEntry const* GetAnimRotation(uint32 time) const;
};

/**
 * @brief 运输工具动画容器
 *
 * 从运输工具模板ID到动画数据的映射。
 */
typedef std::map<uint32, TransportAnimation> TransportAnimationContainer;

/**
 * @class TransportMgr
 * @brief 运输工具管理器类
 *
 * TransportMgr 是运输工具系统的核心管理类，采用单例模式设计。
 * 负责运输工具模板的加载、路径生成、实例创建和生命周期管理。
 *
 * 主要职责：
 *   1. 模板管理：加载、存储和查询运输工具模板
 *   2. 路径生成：计算运输工具的完整路径和运动学参数
 *   3. 实例创建：根据模板创建运输工具实例
 *   4. 动画管理：加载和管理运输工具动画数据
 *
 * 使用方式：
 *   - 全局访问：sTransportMgr 宏
 *   - 单例获取：TransportMgr::instance()
 *
 * 初始化顺序：
 *   1. LoadTransportTemplates() - 加载模板并生成路径
 *   2. LoadTransportAnimationAndRotation() - 加载动画数据
 *   3. SpawnContinentTransports() - 生成大地图运输工具
 *   4. CreateInstanceTransports() - 副本地图按需创建
 */
class TC_GAME_API TransportMgr
{
    public:
        /**
         * @brief 获取单例实例
         *
         * @return TransportMgr 单例指针
         *
         * 使用静态局部变量实现线程安全的单例模式。
         */
        static TransportMgr* instance();

        /**
         * @brief 卸载所有数据
         *
         * 清空所有运输工具模板和动画数据。
         * 通常在服务器关闭时调用。
         */
        void Unload();

        /**
         * @brief 加载运输工具模板
         *
         * 从数据库加载所有运输工具模板（gameobject_template 表中 type=15 的记录），
         * 并为每个模板生成完整的路径数据。
         *
         * 加载流程：
         *   1. 查询所有运输工具类型的游戏对象
         *   2. 验证路径有效性
         *   3. 生成路径和关键帧
         *   4. 分类存储（大地图/副本）
         *
         * 调用时机：服务器启动时
         */
        void LoadTransportTemplates();

        /**
         * @brief 加载运输工具动画和旋转数据
         *
         * 从 DBC 文件加载运输工具的动画数据（TransportAnimation.dbc）
         * 和旋转数据（TransportRotation.dbc）。
         *
         * 这些数据用于动态运输工具（如电梯）的精确动画播放。
         *
         * 调用时机：服务器启动时，在 LoadTransportTemplates 之后
         */
        void LoadTransportAnimationAndRotation();

        /**
         * @brief 创建运输工具实例
         *
         * @param entry 运输工具模板ID（游戏对象模板entry）
         * @param guid 对象GUID低值（可选，0表示自动生成）
         * @param map 目标地图指针（可选，nullptr表示从模板获取）
         * @return 创建的运输工具指针，失败返回 nullptr
         *
         * 根据模板创建运输工具实例：
         *   1. 获取运输工具模板
         *   2. 创建 Transport 对象
         *   3. 初始化位置和属性
         *   4. 添加到地图
         *
         * 调用时机：
         *   - SpawnContinentTransports() 中创建大地图运输工具
         *   - CreateInstanceTransports() 中创建副本运输工具
         */
        Transport* CreateTransport(uint32 entry, ObjectGuid::LowType guid = 0, Map* map = nullptr);

        /**
         * @brief 生成所有大地图运输工具
         *
         * 在服务器启动时创建所有大地图（非副本）上的运输工具。
         * 这些运输工具持久存在于游戏世界中。
         *
         * 从 transports 表读取预定义的运输工具GUID和模板ID，
         * 确保运输工具的GUID在服务器重启后保持一致。
         *
         * 调用时机：服务器启动时，地图初始化完成后
         */
        void SpawnContinentTransports();

        /**
         * @brief 为副本创建所有运输工具
         *
         * @param map 副本地图指针
         *
         * 当副本地图创建时，为其创建预定义的所有运输工具。
         * 运输工具随副本销毁而销毁。
         *
         * 调用时机：副本地图创建时
         */
        void CreateInstanceTransports(Map* map);

        /**
         * @brief 获取运输工具模板
         *
         * @param entry 运输工具模板ID
         * @return 模板常量指针，找不到返回 nullptr
         *
         * 从缓存中快速查找运输工具模板数据。
         */
        TransportTemplate const* GetTransportTemplate(uint32 entry) const
        {
            TransportTemplates::const_iterator itr = _transportTemplates.find(entry);
            if (itr != _transportTemplates.end())
                return &itr->second;
            return nullptr;
        }

        /**
         * @brief 获取运输工具动画信息
         *
         * @param entry 运输工具模板ID
         * @return 动画数据常量指针，找不到返回 nullptr
         *
         * 获取运输工具的动画数据，用于动态运输工具的播放。
         */
        TransportAnimation const* GetTransportAnimInfo(uint32 entry) const
        {
            TransportAnimationContainer::const_iterator itr = _transportAnimations.find(entry);
            if (itr != _transportAnimations.end())
                return &itr->second;

            return nullptr;
        }

    private:
        /**
         * @brief 私有构造函数
         *
         * 单例模式，禁止外部实例化。
         */
        TransportMgr();

        /**
         * @brief 私有析构函数
         */
        ~TransportMgr();

        // 禁止拷贝和赋值
        TransportMgr(TransportMgr const&) = delete;
        TransportMgr& operator=(TransportMgr const&) = delete;

        /**
         * @brief 生成运输工具路径
         *
         * @param goInfo 游戏对象模板数据
         * @param transport 输出的运输工具模板
         *
         * 根据出租车路径节点生成完整的运输工具路径：
         *   1. 加载路径节点
         *   2. 创建样条曲线
         *   3. 计算关键帧数据
         *   4. 计算运动学参数（加速、减速）
         *   5. 计算时间数据
         *
         * 路径生成算法：
         *   - 使用 Catmull-Rom 样条平滑路径
         *   - 模拟真实的加速/减速运动
         *   - 处理跨地图传送节点
         *
         * 性能优化：路径在加载时预计算，避免运行时重复计算
         */
        void GeneratePath(GameObjectTemplate const* goInfo, TransportTemplate* transport);

        /**
         * @brief 添加路径节点到运输工具动画
         *
         * @param transportEntry 运输工具模板ID
         * @param timeSeg 时间段索引
         * @param node 动画节点数据
         *
         * 将 DBC 中的动画节点添加到运输工具动画数据中。
         */
        void AddPathNodeToTransport(uint32 transportEntry, uint32 timeSeg, TransportAnimationEntry const* node);

        /**
         * @brief 添加旋转数据到运输工具动画
         *
         * @param transportEntry 运输工具模板ID
         * @param timeSeg 时间段索引
         * @param node 旋转数据
         *
         * 将 DBC 中的旋转数据添加到运输工具动画数据中。
         */
        void AddPathRotationToTransport(uint32 transportEntry, uint32 timeSeg, TransportRotationEntry const* node)
        {
            _transportAnimations[transportEntry].Rotations[timeSeg] = node;
        }

        // ==================== 成员变量 ====================

        /**
         * @brief 运输工具模板容器
         *
         * 存储所有加载的运输工具模板，以模板ID为键。
         */
        TransportTemplates _transportTemplates;

        /**
         * @brief 副本运输工具映射
         *
         * 存储需要在副本地图中创建的运输工具模板ID。
         * 键：地图ID
         * 值：该地图需要创建的运输工具模板ID集合
         */
        TransportInstanceMap _instanceTransports;

        /**
         * @brief 运输工具动画容器
         *
         * 存储运输工具的动画数据，用于动态运输工具。
         */
        TransportAnimationContainer _transportAnimations;
};

// 全局访问宏
#define sTransportMgr TransportMgr::instance()

#endif // TRANSPORTMGR_H
