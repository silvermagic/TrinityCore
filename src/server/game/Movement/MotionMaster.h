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
 * @file MotionMaster.h
 * @brief 移动主控制器定义模块
 *
 * 本模块定义了 MotionMaster 类,是单位移动系统的核心管理器。
 * MotionMaster 负责管理和调度单位的所有移动行为,包括移动生成器的生命周期管理、
 * 优先级处理、延迟操作和状态维护。
 *
 * 主要职责:
 * - 管理多个移动生成器的堆栈
 * - 处理移动生成器的添加、移除和切换
 * - 执行移动生成器的更新循环
 * - 维护延迟操作队列确保线程安全
 * - 协调不同优先级的移动请求
 *
 * 架构设计:
 * - 槽位系统: 默认槽位、活动槽位,支持多槽位并存
 * - 优先级系统: 正常、高优先级、干扰等
 * - 延迟执行: 在更新过程中对结构的修改延迟到更新完成后执行
 *
 * 典型使用场景:
 * - 生物追击玩家(ChaseMovementGenerator)
 * - 宠物跟随主人(FollowMovementGenerator)
 * - NPC巡逻路径(WaypointMovementGenerator)
 * - 逃跑行为(FleeingMovementGenerator)
 *
 * 相关模块:
 * - MovementGenerator: 具体的移动行为实现
 * - Unit: 拥有 MotionMaster 的游戏单位
 * - PathGenerator: 提供寻路支持
 */

#ifndef MOTIONMASTER_H
#define MOTIONMASTER_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "MovementDefines.h"
#include "MovementGenerator.h"
#include "SharedDefines.h"
#include <deque>
#include <functional>
#include <set>
#include <unordered_map>
#include <vector>

class PathGenerator;
class Unit;
struct Position;
struct SplineChainLink;
struct SplineChainResumeInfo;
struct WaypointPath;

namespace Movement
{
    class MoveSplineInit;
}

/**
 * @enum MotionMasterFlags
 * @brief MotionMaster 标志位枚举
 *
 * 用于跟踪 MotionMaster 的当前状态,控制延迟操作的执行
 */
enum MotionMasterFlags : uint8
{
    MOTIONMASTER_FLAG_NONE                          = 0x0,  ///< 无标志
    MOTIONMASTER_FLAG_UPDATE                        = 0x1,  ///< 更新进行中:正在执行 Update 方法
    MOTIONMASTER_FLAG_STATIC_INITIALIZATION_PENDING = 0x2,  ///< 静态初始化待处理:默认槽位尚未初始化
    MOTIONMASTER_FLAG_INITIALIZATION_PENDING        = 0x4,  ///< 初始化待处理:MotionMaster 暂停直到收到信号
    MOTIONMASTER_FLAG_INITIALIZING                  = 0x8,  ///< 正在初始化:MotionMaster 正在执行初始化

    /// 延迟标志:需要延迟执行的操作标志组合
    MOTIONMASTER_FLAG_DELAYED = MOTIONMASTER_FLAG_UPDATE | MOTIONMASTER_FLAG_INITIALIZATION_PENDING
};

/**
 * @enum MotionMasterDelayedActionType
 * @brief 延迟操作类型枚举
 *
 * 定义可以在更新循环中延迟执行的操作类型
 */
enum MotionMasterDelayedActionType : uint8
{
    MOTIONMASTER_DELAYED_CLEAR = 0,          ///< 清空所有移动生成器
    MOTIONMASTER_DELAYED_CLEAR_SLOT,         ///< 清空指定槽位的移动生成器
    MOTIONMASTER_DELAYED_CLEAR_MODE,         ///< 清空指定模式的移动生成器
    MOTIONMASTER_DELAYED_CLEAR_PRIORITY,     ///< 清空指定优先级的移动生成器
    MOTIONMASTER_DELAYED_ADD,                ///< 添加移动生成器
    MOTIONMASTER_DELAYED_REMOVE,             ///< 移除指定的移动生成器
    MOTIONMASTER_DELAYED_REMOVE_TYPE,        ///< 移除指定类型的移动生成器
    MOTIONMASTER_DELAYED_INITIALIZE          ///< 初始化 MotionMaster
};

/**
 * @struct MovementGeneratorDeleter
 * @brief 移动生成器删除器
 *
 * 用于智能指针的自定义删除器,负责正确释放移动生成器对象
 */
struct MovementGeneratorDeleter
{
    void operator()(MovementGenerator* a);
};

/**
 * @struct MovementGeneratorComparator
 * @brief 移动生成器比较器
 *
 * 用于 multiset 容器的排序,根据优先级和类型排序移动生成器
 */
struct MovementGeneratorComparator
{
    public:
        /**
         * @brief 比较两个移动生成器的顺序
         * @param a 第一个移动生成器
         * @param b 第二个移动生成器
         * @return true 表示 a 应该排在 b 前面
         */
        bool operator()(MovementGenerator const* a, MovementGenerator const* b) const;
};

/**
 * @struct MovementGeneratorInformation
 * @brief 移动生成器信息结构
 *
 * 用于存储和传递移动生成器的基本信息,主要用于调试和查询
 */
struct MovementGeneratorInformation
{
    MovementGeneratorInformation(MovementGeneratorType type, ObjectGuid targetGUID, std::string const& targetName);

    MovementGeneratorType Type;      ///< 移动生成器类型
    ObjectGuid TargetGUID;           ///< 目标单位的 GUID
    std::string TargetName;          ///< 目标单位名称
};

/**
 * @brief 空验证器函数
 * @return 总是返回 true
 *
 * 用于不需要验证条件的延迟操作
 */
static bool EmptyValidator()
{
    return true;
}

/**
 * @class MotionMaster
 * @brief 移动主控制器
 *
 * 管理单位所有移动行为的核心类,负责移动生成器的生命周期管理、
 * 优先级调度和状态维护。每个单位(Unit)拥有一个 MotionMaster 实例。
 *
 * 核心概念:
 * - 槽位(Slot): 默认槽位(持续行为)和活动槽位(临时行为)
 * - 优先级(Priority): 控制移动生成器的执行顺序
 * - 延迟操作: 在更新循环中对结构的修改延迟执行,确保线程安全
 *
 * 线程安全:
 * - 非线程安全,只能在主线程操作
 * - 通过延迟操作机制确保更新过程中的结构修改安全
 */
class TC_GAME_API MotionMaster
{
    public:
        /// 延迟操作定义类型:无参数无返回值的函数对象
        typedef std::function<void()> DelayedActionDefine;

        /// 延迟操作验证器类型:返回布尔值的函数对象,用于判断是否执行操作
        typedef std::function<bool()> DelayedActionValidator;

        /**
         * @class DelayedAction
         * @brief 延迟操作封装类
         *
         * 封装延迟执行的操作,包含操作本身、验证器和操作类型。
         * 在更新循环结束后执行,避免在迭代过程中修改容器。
         */
        class DelayedAction
        {
            public:
                /**
                 * @brief 构造函数(带验证器)
                 * @param action 要执行的操作
                 * @param validator 验证函数,决定是否执行操作
                 * @param type 操作类型
                 */
                explicit DelayedAction(DelayedActionDefine&& action, DelayedActionValidator&& validator, MotionMasterDelayedActionType type) : Action(std::move(action)), Validator(std::move(validator)), Type(type) { }

                /**
                 * @brief 构造函数(无验证器)
                 * @param action 要执行的操作
                 * @param type 操作类型
                 */
                explicit DelayedAction(DelayedActionDefine&& action, MotionMasterDelayedActionType type) : Action(std::move(action)), Validator(EmptyValidator), Type(type) { }

                ~DelayedAction() { }

                /**
                 * @brief 解析并执行操作
                 *
                 * 如果验证器返回 true,则执行操作
                 */
                void Resolve() { if (Validator()) Action(); }

                DelayedActionDefine Action;      ///< 要执行的操作
                DelayedActionValidator Validator; ///< 验证函数
                uint8 Type;                       ///< 操作类型
        };

        /**
         * @brief 构造函数
         * @param unit 拥有此 MotionMaster 的单位
         */
        explicit MotionMaster(Unit* unit);

        /**
         * @brief 析构函数
         */
        ~MotionMaster();

        /**
         * @brief 初始化 MotionMaster
         *
         * 初始化默认移动生成器,必须在单位添加到世界后调用
         */
        void Initialize();

        /**
         * @brief 初始化默认移动生成器
         *
         * 为默认槽位创建空闲移动生成器
         */
        void InitializeDefault();

        /**
         * @brief 单位加入世界时调用
         *
         * 执行必要的初始化和状态恢复
         */
        void AddToWorld();

        // ========================================================================
        // 查询方法
        // ========================================================================

        /**
         * @brief 检查移动生成器容器是否为空
         * @return true 表示容器为空
         */
        bool Empty() const;

        /**
         * @brief 获取移动生成器数量
         * @return 移动生成器的总数
         */
        uint32 Size() const;

        /**
         * @brief 获取所有移动生成器的信息
         * @return 移动生成器信息数组
         */
        std::vector<MovementGeneratorInformation> GetMovementGeneratorsInformation() const;

        /**
         * @brief 获取当前活动的槽位
         * @return 当前槽位枚举值
         */
        MovementSlot GetCurrentSlot() const;

        /**
         * @brief 获取当前活动的移动生成器
         * @return 当前移动生成器指针,如果不存在返回 nullptr
         */
        MovementGenerator* GetCurrentMovementGenerator() const;

        /**
         * @brief 获取当前移动生成器的类型
         * @return 移动生成器类型枚举值
         */
        MovementGeneratorType GetCurrentMovementGeneratorType() const;

        /**
         * @brief 获取指定槽位的移动生成器类型
         * @param slot 槽位索引
         * @return 移动生成器类型枚举值
         */
        MovementGeneratorType GetCurrentMovementGeneratorType(MovementSlot slot) const;

        /**
         * @brief 获取指定槽位的移动生成器
         * @param slot 槽位索引
         * @return 移动生成器指针,如果不存在返回 nullptr
         */
        MovementGenerator* GetCurrentMovementGenerator(MovementSlot slot) const;

        /**
         * @brief 根据条件查找移动生成器
         * @param filter 过滤函数,返回 true 表示匹配
         * @param slot 要搜索的槽位,默认为活动槽位
         * @return 第一个匹配的移动生成器指针,如果未找到返回 nullptr
         */
        MovementGenerator* GetMovementGenerator(std::function<bool(MovementGenerator const*)> const& filter, MovementSlot slot = MOTION_SLOT_ACTIVE) const;

        /**
         * @brief 检查是否存在匹配条件的移动生成器
         * @param filter 过滤函数,返回 true 表示匹配
         * @param slot 要搜索的槽位,默认为活动槽位
         * @return true 表示存在匹配的移动生成器
         */
        bool HasMovementGenerator(std::function<bool(MovementGenerator const*)> const& filter, MovementSlot slot = MOTION_SLOT_ACTIVE) const;

        // ========================================================================
        // 更新方法
        // ========================================================================

        /**
         * @brief 更新移动逻辑
         * @param diff 距离上次更新的时间间隔(毫秒)
         *
         * 每个游戏循环调用一次,更新当前活动的移动生成器。
         * 调用时机:Unit::Update 中
         * 性能注意:频繁调用,需要优化性能
         */
        void Update(uint32 diff);

        /**
         * @brief 添加移动生成器
         * @param movement 移动生成器指针
         * @param slot 目标槽位,默认为活动槽位
         *
         * 将移动生成器添加到指定槽位,如果正在更新中则延迟执行。
         */
        void Add(MovementGenerator* movement, MovementSlot slot = MOTION_SLOT_ACTIVE);

        /**
         * @brief 移除指定的移动生成器
         * @param movement 要移除的移动生成器
         * @param slot 槽位索引,默认为活动槽位
         *
         * 注意:移除默认槽位的移动生成器后,会自动填充空闲移动生成器
         */
        void Remove(MovementGenerator* movement, MovementSlot slot = MOTION_SLOT_ACTIVE);

        /**
         * @brief 移除指定类型的移动生成器
         * @param type 移动生成器类型
         * @param slot 槽位索引,默认为活动槽位
         *
         * 移除第一个找到的匹配类型的移动生成器。
         * 注意:移除默认槽位的移动生成器后,会自动填充空闲移动生成器
         */
        void Remove(MovementGeneratorType type, MovementSlot slot = MOTION_SLOT_ACTIVE);

        /**
         * @brief 清空所有移动生成器
         *
         * 注意:不影响默认槽位
         */
        void Clear();

        /**
         * @brief 清空指定槽位的所有移动生成器
         * @param slot 槽位索引
         *
         * 注意:清空默认槽位后,会自动填充空闲移动生成器
         */
        void Clear(MovementSlot slot);

        /**
         * @brief 清空指定模式的所有移动生成器
         * @param mode 移动生成器模式
         *
         * 注意:不影响默认槽位
         */
        void Clear(MovementGeneratorMode mode);

        /**
         * @brief 清空指定优先级的所有移动生成器
         * @param priority 移动生成器优先级
         *
         * 注意:不影响默认槽位
         */
        void Clear(MovementGeneratorPriority priority);

        /**
         * @brief 传播速度变化
         *
         * 当单位速度改变时通知当前移动生成器
         */
        void PropagateSpeedChange();

        /**
         * @brief 获取当前移动目标位置
         * @param x [out] X坐标
         * @param y [out] Y坐标
         * @param z [out] Z坐标
         * @return true 表示成功获取目标位置
         */
        bool GetDestination(float &x, float &y, float &z);

        /**
         * @brief 单位死亡时停止移动
         * @return true 表示成功停止移动
         */
        bool StopOnDeath();

        // ========================================================================
        // 移动命令方法
        // ========================================================================

        /**
         * @brief 移动到空闲状态
         *
         * 停止当前移动,进入空闲状态
         */
        void MoveIdle();

        /**
         * @brief 移动返回出生点
         *
         * 使单位返回其出生位置或重置位置
         */
        void MoveTargetedHome();

        /**
         * @brief 随机移动
         * @param wanderDistance 游荡距离,0表示使用默认值
         *
         * 在当前位置附近随机游荡
         */
        void MoveRandom(float wanderDistance = 0.0f);

        /**
         * @brief 跟随目标
         * @param target 跟随的目标单位
         * @param dist 保持的距离
         * @param angle 保持的角度
         * @param slot 使用的槽位,默认为活动槽位
         */
        void MoveFollow(Unit* target, float dist, ChaseAngle angle, MovementSlot slot = MOTION_SLOT_ACTIVE);

        /**
         * @brief 追击目标
         * @param target 追击的目标单位
         * @param dist 追击距离范围(可选)
         * @param angle 追击角度(可选)
         */
        void MoveChase(Unit* target, Optional<ChaseRange> dist = {}, Optional<ChaseAngle> angle = {});

        /**
         * @brief 追击目标(重载版本)
         * @param target 追击的目标单位
         * @param dist 追击距离
         * @param angle 追击角度
         */
        void MoveChase(Unit* target, float dist, float angle) { MoveChase(target, ChaseRange(dist), ChaseAngle(angle)); }

        /**
         * @brief 追击目标(重载版本)
         * @param target 追击的目标单位
         * @param dist 追击距离
         */
        void MoveChase(Unit* target, float dist) { MoveChase(target, ChaseRange(dist)); }

        /**
         * @brief 进入混乱移动状态
         *
         * 单位随机移动,表示混乱或眩晕状态
         */
        void MoveConfused();

        /**
         * @brief 逃离敌人
         * @param enemy 要逃离的敌人
         * @param time 逃离持续时间(毫秒),0表示无限期
         */
        void MoveFleeing(Unit* enemy, uint32 time = 0);

        /**
         * @brief 移动到指定位置
         * @param id 移动标识符
         * @param pos 目标位置
         * @param generatePath 是否生成路径,true表示使用寻路
         * @param finalOrient 到达后的朝向(可选)
         */
        void MovePoint(uint32 id, Position const& pos, bool generatePath = true, Optional<float> finalOrient = {});

        /**
         * @brief 移动到指定位置(重载版本)
         * @param id 移动标识符
         * @param x 目标X坐标
         * @param y 目标Y坐标
         * @param z 目标Z坐标
         * @param generatePath 是否生成路径,true表示使用寻路
         * @param finalOrient 到达后的朝向(可选)
         */
        void MovePoint(uint32 id, float x, float y, float z, bool generatePath = true, Optional<float> finalOrient = {});

        /**
         * @brief 接近目标并停止
         * @param id 移动标识符
         * @param target 目标单位
         * @param distance 停止距离
         *
         * 使单位向目标移动直到达到指定距离,然后停止。
         * 仅在2D平面工作,不考虑目标的移动,适用于静止目标。
         */
        void MoveCloserAndStop(uint32 id, Unit* target, float distance);

        /**
         * @brief 降落移动
         * @param id 移动标识符
         * @param pos 目标位置
         * @param velocity 降落速度(可选)
         *
         * 仅适用于有降落动画的生物
         */
        void MoveLand(uint32 id, Position const& pos, Optional<float> velocity = {});

        /**
         * @brief 起飞移动
         * @param id 移动标识符
         * @param pos 目标位置
         * @param velocity 起飞速度(可选)
         *
         * 仅适用于有起飞动画的生物
         */
        void MoveTakeoff(uint32 id, Position const& pos, Optional<float> velocity = {});

        /**
         * @brief 冲锋移动
         * @param x 目标X坐标
         * @param y 目标Y坐标
         * @param z 目标Z坐标
         * @param speed 冲锋速度,默认为标准冲锋速度
         * @param id 移动标识符,默认为冲锋事件ID
         * @param generatePath 是否生成路径,默认为false
         */
        void MoveCharge(float x, float y, float z, float speed = SPEED_CHARGE, uint32 id = EVENT_CHARGE, bool generatePath = false);

        /**
         * @brief 冲锋移动(使用预计算的路径)
         * @param path 预计算的路径
         * @param speed 冲锋速度,默认为标准冲锋速度
         */
        void MoveCharge(PathGenerator const& path, float speed = SPEED_CHARGE);

        /**
         * @brief 击退移动
         * @param srcX 击退源X坐标
         * @param srcY 击退源Y坐标
         * @param speedXY 水平速度
         * @param speedZ 垂直速度
         */
        void MoveKnockbackFrom(float srcX, float srcY, float speedXY, float speedZ);

        /**
         * @brief 跳跃到指定角度
         * @param angle 跳跃角度
         * @param speedXY 水平速度
         * @param speedZ 垂直速度
         */
        void MoveJumpTo(float angle, float speedXY, float speedZ);

        /**
         * @brief 跳跃到指定位置
         * @param pos 目标位置
         * @param speedXY 水平速度
         * @param speedZ 垂直速度
         * @param id 移动标识符,默认为跳跃事件ID
         * @param hasOrientation 是否保持朝向
         */
        void MoveJump(Position const& pos, float speedXY, float speedZ, uint32 id = EVENT_JUMP, bool hasOrientation = false);

        /**
         * @brief 跳跃到指定位置(重载版本)
         * @param x 目标X坐标
         * @param y 目标Y坐标
         * @param z 目标Z坐标
         * @param o 朝向
         * @param speedXY 水平速度
         * @param speedZ 垂直速度
         * @param id 移动标识符,默认为跳跃事件ID
         * @param hasOrientation 是否保持朝向
         */
        void MoveJump(float x, float y, float z, float o, float speedXY, float speedZ, uint32 id = EVENT_JUMP, bool hasOrientation = false);

        /**
         * @brief 圆形路径移动
         * @param x 圆心X坐标
         * @param y 圆心Y坐标
         * @param z 圆心Z坐标
         * @param radius 圆形半径
         * @param clockwise true表示顺时针,false表示逆时针
         * @param stepCount 路径点数量
         */
        void MoveCirclePath(float x, float y, float z, float radius, bool clockwise, uint8 stepCount);

        /**
         * @brief 平滑路径移动
         * @param pointId 路径点标识符
         * @param pathPoints 路径点数组
         * @param pathSize 路径点数量
         * @param walk true表示行走,false表示奔跑
         */
        void MoveSmoothPath(uint32 pointId, Position const* pathPoints, size_t pathSize, bool walk);

        /**
         * @brief 沿数据库中的样条链移动
         * @param pointId 路径点标识符
         * @param dbChainId 数据库中的样条链ID
         * @param walk true表示行走,false表示奔跑
         *
         * 使用存储在 script_spline_chain_meta 和 script_spline_chain_waypoints 表中的数据
         */
        void MoveAlongSplineChain(uint32 pointId, uint16 dbChainId, bool walk);

        /**
         * @brief 沿自定义样条链移动
         * @param pointId 路径点标识符
         * @param chain 样条链数据
         * @param walk true表示行走,false表示奔跑
         */
        void MoveAlongSplineChain(uint32 pointId, std::vector<SplineChainLink> const& chain, bool walk);

        /**
         * @brief 恢复样条链移动
         * @param info 样条链恢复信息
         */
        void ResumeSplineChain(SplineChainResumeInfo const& info);

        /**
         * @brief 下落移动
         * @param id 移动标识符,默认为0
         */
        void MoveFall(uint32 id = 0);

        /**
         * @brief 寻求援助移动
         * @param x 目标X坐标
         * @param y 目标Y坐标
         * @param z 目标Z坐标
         *
         * 使单位移动到指定位置寻求其他单位的援助
         */
        void MoveSeekAssistance(float x, float y, float z);

        /**
         * @brief 寻求援助时的分心移动
         * @param timer 分心时间(毫秒)
         */
        void MoveSeekAssistanceDistract(uint32 timer);

        /**
         * @brief 出租车飞行路径
         * @param path 飞行路径ID
         * @param pathnode 路径节点ID
         */
        void MoveTaxiFlight(uint32 path, uint32 pathnode);

        /**
         * @brief 分心移动
         * @param time 分心时间(毫秒)
         * @param orientation 朝向角度
         *
         * 单位转向指定方向并在指定时间内保持不动
         */
        void MoveDistract(uint32 time, float orientation);

        /**
         * @brief 路径点移动
         * @param pathId 路径ID
         * @param repeatable true表示循环路径,false表示单次执行
         */
        void MovePath(uint32 pathId, bool repeatable);

        /**
         * @brief 路径点移动(重载版本)
         * @param path 路径数据引用
         * @param repeatable true表示循环路径,false表示单次执行
         */
        void MovePath(WaypointPath& path, bool repeatable);

        /**
         * @brief 旋转移动
         * @param id 移动标识符
         * @param time 旋转时间(毫秒)
         * @param direction 旋转方向
         */
        void MoveRotate(uint32 id, uint32 time, RotateDirection direction);

        /**
         * @brief 编队移动
         * @param leader 编队领导者
         * @param range 保持范围
         * @param angle 保持角度
         * @param point1 路径点1
         * @param point2 路径点2
         */
        void MoveFormation(Unit* leader, float range, float angle, uint32 point1, uint32 point2);

        /**
         * @brief 启动移动样条
         * @param initializer 初始化函数,用于配置移动样条
         * @param id 移动标识符,默认为0
         * @param priority 移动生成器优先级,默认为正常优先级
         * @param type 移动生成器类型,默认为效果移动类型
         *
         * 低级接口,用于直接配置移动样条参数
         */
        void LaunchMoveSpline(std::function<void(Movement::MoveSplineInit& init)>&& initializer, uint32 id = 0, MovementGeneratorPriority priority = MOTION_PRIORITY_NORMAL, MovementGeneratorType type = EFFECT_MOTION_TYPE);
    private:
        // ========================================================================
        // 类型定义
        // ========================================================================

        /// 移动生成器智能指针类型(使用自定义删除器)
        typedef std::unique_ptr<MovementGenerator, MovementGeneratorDeleter> MovementGeneratorPointer;

        /// 移动生成器容器类型(多集合,按优先级排序)
        typedef std::multiset<MovementGenerator*, MovementGeneratorComparator> MotionMasterContainer;

        /// 单位状态映射容器类型(用于跟踪每个移动生成器添加的单位状态)
        typedef std::unordered_multimap<uint32, MovementGenerator const*> MotionMasterUnitStatesContainer;

        // ========================================================================
        // 标志位操作方法
        // ========================================================================

        /**
         * @brief 添加标志位
         * @param flag 要添加的标志位
         */
        void AddFlag(uint8 const flag) { _flags |= flag; }

        /**
         * @brief 检查是否具有指定标志位
         * @param flag 要检查的标志位
         * @return true 表示具有该标志位
         */
        bool HasFlag(uint8 const flag) const { return (_flags & flag) != 0; }

        /**
         * @brief 移除标志位
         * @param flag 要移除的标志位
         */
        void RemoveFlag(uint8 const flag) { _flags &= ~flag; }

        // ========================================================================
        // 核心私有方法
        // ========================================================================

        /**
         * @brief 解析并执行所有延迟操作
         *
         * 在更新循环结束后执行所有待处理的延迟操作
         */
        void ResolveDelayedActions();

        /**
         * @brief 移除指定迭代器位置的移动生成器
         * @param iterator 移动生成器迭代器
         * @param active 是否处于活动状态
         * @param movementInform 是否发送移动完成通知
         */
        void Remove(MotionMasterContainer::iterator iterator, bool active, bool movementInform);

        /**
         * @brief 弹出当前移动生成器
         * @param active 是否处于活动状态
         * @param movementInform 是否发送移动完成通知
         */
        void Pop(bool active, bool movementInform);

        /**
         * @brief 直接初始化(非延迟)
         *
         * 立即执行初始化,不加入延迟队列
         */
        void DirectInitialize();

        /**
         * @brief 直接清空所有移动生成器(非延迟)
         */
        void DirectClear();

        /**
         * @brief 直接清空默认槽位(非延迟)
         */
        void DirectClearDefault();

        /**
         * @brief 直接清空匹配过滤条件的移动生成器(非延迟)
         * @param filter 过滤函数
         */
        void DirectClear(std::function<bool(MovementGenerator*)> const& filter);

        /**
         * @brief 直接添加移动生成器(非延迟)
         * @param movement 移动生成器指针
         * @param slot 目标槽位
         */
        void DirectAdd(MovementGenerator* movement, MovementSlot slot);

        /**
         * @brief 删除移动生成器
         * @param movement 要删除的移动生成器
         * @param active 是否处于活动状态
         * @param movementInform 是否发送移动完成通知
         */
        void Delete(MovementGenerator* movement, bool active, bool movementInform);

        /**
         * @brief 删除默认槽位的移动生成器
         * @param active 是否处于活动状态
         * @param movementInform 是否发送移动完成通知
         */
        void DeleteDefault(bool active, bool movementInform);

        /**
         * @brief 添加基础单位状态
         * @param movement 移动生成器
         *
         * 将移动生成器的基础单位状态应用到单位
         */
        void AddBaseUnitState(MovementGenerator const* movement);

        /**
         * @brief 清除移动生成器的基础单位状态
         * @param movement 移动生成器
         */
        void ClearBaseUnitState(MovementGenerator const* movement);

        /**
         * @brief 清除所有基础单位状态
         */
        void ClearBaseUnitStates();

        // ========================================================================
        // 成员变量
        // ========================================================================

        Unit* _owner;                                      ///< 拥有此 MotionMaster 的单位
        MovementGeneratorPointer _defaultGenerator;        ///< 默认槽位的移动生成器(单独存储)
        MotionMasterContainer _generators;                 ///< 活动槽位的移动生成器集合
        MotionMasterUnitStatesContainer _baseUnitStatesMap;///< 单位状态映射表
        std::deque<DelayedAction> _delayedActions;         ///< 延迟操作队列
        uint8 _flags;                                      ///< MotionMaster 标志位
};

#endif // MOTIONMASTER_H
