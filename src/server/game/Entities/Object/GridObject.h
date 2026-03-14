/**
 * @file GridObject.h
 * @brief 网格对象基类 - 提供游戏对象在网格系统中的引用管理功能
 *
 * 该文件定义了 GridObject 模板类,用于管理游戏对象在 TrinityCore 网格系统中的
 * 添加、移除和状态查询操作。网格系统是世界服务器管理和组织游戏对象的核心机制,
 * 通过将地图划分为网格单元来优化对象查找和空间管理性能。
 *
 * 主要功能:
 * - 管理对象在网格引用管理器中的注册与注销
 * - 提供对象是否在网格中的状态查询
 * - 通过 GridReference 实现双向引用链表结构
 *
 * 使用场景:
 * - 所有需要在网格系统中进行空间管理的游戏对象(如 Player, Creature, GameObject 等)
 *   都应继承此基类
 * - 对象进入地图时调用 AddToGrid,离开地图时调用 RemoveFromGrid
 *
 * @see GridReference 网格引用类,实现双向链表节点功能
 * @see GridRefManager 网格引用管理器,管理同一网格内的所有对象
 */

#ifndef _GRIDOBJECT_H
#define _GRIDOBJECT_H

#include "GridReference.h"
#include "GridRefManager.h"

/**
 * @class GridObject
 * @brief 网格对象模板基类 - 为游戏对象提供网格系统的集成能力
 *
 * 该模板类为游戏对象提供与网格系统交互的标准接口和机制。
 * 继承此类的对象可以被添加到网格引用管理器中,从而参与网格系统
 * 的空间组织和管理。
 *
 * 模板参数:
 * @tparam T 继承此基类的具体游戏对象类型(如 Player, Creature, GameObject 等)
 *          使用 CRTP(奇异递归模板模式)设计,允许基类访问派生类类型信息
 *
 * 继承关系:
 * - 这是一个纯抽象基类,提供虚析构函数确保正确的析构行为
 * - 派生类示例: Player, Creature, GameObject, Corpse, DynamicObject 等
 *
 * 设计模式:
 * - 使用 GridReference 成员实现组合模式,管理对象在双向链表中的节点
 * - ASSERT 宏用于调试模式下验证网格操作的合法性
 *
 * 线程安全:
 * - 该类的所有方法应在地图网格锁保护下调用
 * - 非线程安全,外部调用者需确保同步机制
 *
 * @note 对象的生命周期管理由派生类负责,此类仅管理网格引用关系
 */
template<class T>
class GridObject
{
    public:
        /**
         * @brief 虚析构函数 - 确保派生类对象通过基类指针正确析构
         *
         * 定义为虚函数以支持多态删除。析构时不自动移除网格引用,
         * 派生类应在析构前显式调用 RemoveFromGrid()。
         *
         * @note 派生类有责任在析构前确保对象已从网格中移除
         */
        virtual ~GridObject() { }

        /**
         * @brief 检查对象当前是否在网格中
         *
         * 通过验证内部网格引用的有效性来判断对象是否已注册到某个网格引用管理器中。
         * 该方法是快速查询操作,仅检查引用状态,不修改任何状态。
         *
         * @return bool
         *         - true: 对象当前已添加到网格中,网格引用有效
         *         - false: 对象未在网格中,网格引用无效
         *
         * @note 此方法是 const 方法,不会修改对象状态
         * @note 时间复杂度: O(1),仅检查一个布尔标志
         *
         * 使用场景:
         * - 在执行依赖网格存在的操作前进行验证
         * - 调试和断言检查
         * - 查询对象的空间管理状态
         */
        bool IsInGrid() const { return _gridRef.isValid(); }

        /**
         * @brief 将对象添加到指定的网格引用管理器中
         *
         * 通过建立网格引用关系,将对象注册到指定的网格引用管理器中。
         * 对象将被添加到管理器的双向链表中,从而参与网格系统的空间管理。
         *
         * @param m 目标网格引用管理器的引用,对象将添加到此管理器中
         *          该管理器代表一个网格单元中的所有对象集合
         *
         * @pre 对象当前必须不在任何网格中,即 IsInGrid() == false
         * @post 对象成功添加后,IsInGrid() == true
         * @post 对象被链接到管理器的双向链表中
         *
         * @note 使用 ASSERT 宏在调试模式下验证前置条件
         *       如果对象已在网格中,会触发断言失败
         * @note 时间复杂度: O(1),链表插入操作
         * @note 内部使用 C 风格强制转换 (T*)this,利用 CRTP 模式
         *
         * 调用时机:
         * - 对象进入地图或跨越网格边界时
         * - 由地图管理系统在对象加载时调用
         *
         * 使用示例:
         * @code
         * // 对象进入地图时的典型调用流程
         * GridRefManager<Creature>& gridManager = map.GetGrid(x, y);
         * creature->AddToGrid(gridManager);
         * @endcode
         */
        void AddToGrid(GridRefManager<T>& m) { ASSERT(!IsInGrid()); _gridRef.link(&m, (T*)this); }

        /**
         * @brief 从网格引用管理器中移除对象
         *
         * 断开对象的网格引用关系,将对象从网格引用管理器的双向链表中移除。
         * 移除后对象不再参与该网格单元的空间管理。
         *
         * @pre 对象当前必须在网格中,即 IsInGrid() == true
         * @post 对象成功移除后,IsInGrid() == false
         * @post 对象从管理器的双向链表中断开
         *
         * @note 使用 ASSERT 宏在调试模式下验证前置条件
         *       如果对象不在网格中,会触发断言失败
         * @note 时间复杂度: O(1),链表删除操作
         * @note 移除操作不删除对象本身,仅解除引用关系
         *
         * 调用时机:
         * - 对象离开地图或跨越网格边界时
         * - 对象被删除或卸载前
         * - 由地图管理系统在对象卸载时调用
         *
         * 使用示例:
         * @code
         * // 对象离开地图或删除前的典型调用流程
         * ASSERT(creature->IsInGrid());
         * creature->RemoveFromGrid();
         * // 现在对象可以安全地移动到其他网格或删除
         * @endcode
         */
        void RemoveFromGrid() { ASSERT(IsInGrid()); _gridRef.unlink(); }

    private:
        /**
         * @brief 网格引用对象 - 管理对象在网格系统中的链表节点关系
         *
         * 该成员变量实现了对象与网格引用管理器之间的双向链表连接。
         * GridReference 是一个智能引用类,负责维护对象在链表中的位置和关系。
         *
         * 关键特性:
         * - 封装了双向链表的前驱和后继指针
         * - 提供 isValid() 方法检查引用是否有效
         * - 提供 link() 和 unlink() 方法管理链表连接
         *
         * 状态说明:
         * - 未链接状态: isValid() == false,对象不在任何网格中
         * - 已链接状态: isValid() == true,对象在某个 GridRefManager 中
         *
         * @note 此成员不应在类外部直接访问,所有操作通过公共接口进行
         * @note GridReference 的设计确保了链表操作的 O(1) 时间复杂度
         */
        GridReference<T> _gridRef;
};

#endif
