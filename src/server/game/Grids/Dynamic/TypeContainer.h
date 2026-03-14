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

#ifndef TRINITY_TYPECONTAINER_H
#define TRINITY_TYPECONTAINER_H

/**
 * @file TypeContainer.h
 * @brief 多类型容器系统 - 提供在单一容器中存储和管理多种类型对象的能力
 *
 * 本文件实现了一系列模板容器类,允许在编译时确定的类型集合中存储不同类型的对象。
 * 这是 TrinityCore 网格系统的核心基础设施,用于高效管理游戏世界中的各类实体。
 *
 * 主要特性:
 * - 编译时类型安全: 所有支持的类型在编译期确定
 * - 零运行时开销: 使用模板特化和递归展开实现类型分发
 * - 统一访问接口: 提供一致的插入、查询、删除操作
 * - 内存高效: 避免虚函数和类型擦除带来的额外开销
 *
 * 容器类型:
 * - ContainerMapList: 基于 GridRefManager 的容器,用于网格对象管理
 * - ContainerUnorderedMap: 基于 std::unordered_map 的哈希容器
 * - TypeMapContainer: 多类型封装容器
 * - TypeUnorderedMapContainer: 多类型哈希容器
 *
 * 设计模式:
 * 使用 TypeList 递归结构和模板特化实现编译期多态,避免运行时类型检查开销。
 */

#include <map>
#include <unordered_map>
#include <vector>
#include "Define.h"
#include "Dynamic/TypeList.h"
#include "GridRefManager.h"

/**
 * @struct ContainerMapList
 * @brief 单类型网格引用管理器容器 - 存储单一类型对象的网格引用
 *
 * 这是一个基础容器模板,封装了 GridRefManager 用于管理特定类型的对象。
 * 通过模板特化和递归组合,可以构建支持多种类型的复合容器。
 *
 * 设计理念:
 * - 使用 GridRefManager 而非直接容器,支持网格对象的引用计数和自动清理
 * - 零开销抽象: 编译后直接内联为 GridRefManager 调用
 * - 类型安全: 编译期保证类型正确性
 *
 * @tparam OBJECT 容器存储的对象类型
 */
template<class OBJECT>
struct ContainerMapList
{
    GridRefManager<OBJECT> _element;  ///< 对象引用管理器,存储该类型的所有对象引用
};

/**
 * @brief TypeNull 特化版本 - 空容器终止递归
 *
 * 当 TypeList 递归展开到达末尾时,此特化版本提供终止条件。
 * 不包含任何数据成员,实现零空间占用。
 */
template<>
struct ContainerMapList<TypeNull>
{
    // 空实现 - TypeList 递归终止标记
};

/**
 * @brief TypeList 特化版本 - 递归容器组合
 *
 * 将 TypeList 的头部类型和尾部类型分别存储在两个子容器中,
 * 实现递归展开,最终形成一个包含所有类型的扁平化容器结构。
 *
 * 内存布局示例 (TypeList<Player, TypeList<Creature, GameObject>>):
 * - _elements: ContainerMapList<Player> -> 包含 Player 引用管理器
 * - _TailElements: ContainerMapList<TypeList<Creature, GameObject>>
 *   - _elements: ContainerMapList<Creature> -> 包含 Creature 引用管理器
 *   - _TailElements: ContainerMapList<GameObject> -> 包含 GameObject 引用管理器
 *
 * @tparam H TypeList 的头部类型
 * @tparam T TypeList 的尾部类型 (通常是另一个 TypeList 或 TypeNull)
 */
template<class H, class T>
struct ContainerMapList<TypeList<H, T> >
{
    ContainerMapList<H> _elements;       ///< 头部类型容器
    ContainerMapList<T> _TailElements;   ///< 尾部类型容器 (递归展开)
};

/**
 * @struct ContainerUnorderedMap
 * @brief 单类型哈希容器 - 使用键值对存储特定类型对象指针
 *
 * 基于 std::unordered_map 实现的单类型容器,提供 O(1) 平均时间复杂度的查找性能。
 * 适用于需要频繁通过键查找对象的场景,如通过 GUID 查找玩家/生物。
 *
 * 性能特点:
 * - 插入/删除/查找: 平均 O(1), 最坏 O(n)
 * - 空间开销: 每个元素额外存储哈希桶和节点指针
 * - 迭代顺序: 无序,不保证插入顺序
 *
 * @tparam OBJECT 存储的对象类型
 * @tparam KEY_TYPE 键类型 (通常是 ObjectGuid 或 uint64)
 */
template<class OBJECT, class KEY_TYPE>
struct ContainerUnorderedMap
{
    std::unordered_map<KEY_TYPE, OBJECT*> _element;  ///< 对象指针哈希表,键到对象指针的映射
};

/**
 * @brief TypeNull 特化版本 - 空哈希容器终止递归
 *
 * 为 TypeList 递归展开提供终止条件,不占用任何空间。
 */
template<class KEY_TYPE>
struct ContainerUnorderedMap<TypeNull, KEY_TYPE>
{
    // 空实现 - TypeList 递归终止标记
};

/**
 * @brief TypeList 特化版本 - 递归哈希容器组合
 *
 * 类似于 ContainerMapList 的 TypeList 特化,但使用哈希表存储。
 * 每种类型维护独立的哈希表,保证类型安全和高效查找。
 *
 * 应用场景:
 * - 网格中快速查找特定 GUID 的实体
 * - 按类型分类存储并支持快速查询
 *
 * @tparam H TypeList 的头部类型
 * @tparam T TypeList 的尾部类型
 * @tparam KEY_TYPE 所有哈希表共用的键类型
 */
template<class H, class T, class KEY_TYPE>
struct ContainerUnorderedMap<TypeList<H, T>, KEY_TYPE>
{
    ContainerUnorderedMap<H, KEY_TYPE> _elements;       ///< 头部类型哈希容器
    ContainerUnorderedMap<T, KEY_TYPE> _TailElements;   ///< 尾部类型哈希容器 (递归展开)
};

#include "TypeContainerFunctions.h"

/**
 * @class TypeMapContainer
 * @brief 多类型网格容器 - 封装 ContainerMapList 提供统一的多类型对象管理接口
 *
 * 这是游戏网格系统的核心容器类,用于在单个网格单元格中存储多种类型的游戏对象。
 * 类型集合在编译时通过模板参数确定,提供类型安全和零运行时开销的多态。
 *
 * 核心职责:
 * 1. 管理多种类型对象的存储和生命周期
 * 2. 提供类型安全的插入和查询操作
 * 3. 与网格系统集成,支持对象引用计数
 *
 * 设计特点:
 * - 编译时类型检查: 只能插入 OBJECT_TYPES 中定义的类型
 * - 零虚函数开销: 使用模板和函数重载实现多态
 * - 内存紧凑: 所有类型的容器连续存储
 * - 支持移动语义: 可高效转移所有权
 *
 * 典型使用场景:
 * @code
 * // 定义支持的类型列表
 * using GridObjectTypes = TypeList<Player, TypeList<Creature, TypeList<GameObject, TypeNull>>>;
 *
 * // 创建容器
 * TypeMapContainer<GridObjectTypes> gridObjects;
 *
 * // 插入对象 (编译期类型检查)
 * gridObjects.insert(player);
 * gridObjects.insert(creature);
 *
 * // 统计特定类型对象数量
 * size_t playerCount = gridObjects.Count<Player>();
 * @endcode
 *
 * 性能说明:
 * - 插入: O(1) 直接追加到引用管理器
 * - 计数: O(n) 需要遍历引用链表
 * - 内存: 每种类型独立一个 GridRefManager,无额外开销
 *
 * @tparam OBJECT_TYPES 支持的对象类型列表,通常是 TypeList<...> 形式
 */
template<class OBJECT_TYPES>
class TypeMapContainer
{
public:
    /**
     * @brief 默认构造函数
     *
     * 初始化所有类型的 ContainerMapList 子对象。
     * 由于使用了 GridRefManager,构造为空容器,无需额外初始化。
     */
    TypeMapContainer();

    /**
     * @brief 拷贝构造函数 (默认)
     *
     * 深度拷贝所有内部容器。注意: 这不会复制对象本身,仅复制引用管理器状态。
     * 实际使用中应避免拷贝大型容器。
     */
    TypeMapContainer(TypeMapContainer const&) = default;

    /**
     * @brief 移动构造函数 (默认)
     *
     * 高效转移容器所有权,适用于容器所有权转移场景。
     * 移动后源对象处于有效但未定义状态。
     */
    TypeMapContainer(TypeMapContainer&&) noexcept = default;

    /**
     * @brief 拷贝赋值运算符 (默认)
     */
    TypeMapContainer& operator=(TypeMapContainer const&) = default;

    /**
     * @brief 移动赋值运算符 (默认)
     */
    TypeMapContainer& operator=(TypeMapContainer&&) noexcept = default;

    /**
     * @brief 析构函数
     *
     * 自动清理所有内部容器。注意: 这不会删除存储的对象指针,
     * 对象生命周期由外部管理(如 Map/Grid 系统)。
     */
    ~TypeMapContainer();

    /**
     * @brief 统计特定类型对象的数量
     *
     * 遍历指定类型的 GridRefManager 并计数所有引用。
     * 时间复杂度: O(n),其中 n 是该类型对象的数量
     *
     * @tparam SPECIFIC_TYPE 要统计的对象类型,必须在 OBJECT_TYPES 中定义
     * @return 该类型对象的数量,如果类型不在列表中则返回 0
     *
     * @note 对于频繁查询场景,考虑缓存计数结果
     */
    template<class SPECIFIC_TYPE>
    size_t Count() const;

    /**
     * @brief 将对象插入到容器中
     *
     * 将对象指针添加到对应类型的 GridRefManager 中。
     * 插入操作会建立对象到网格的引用关系,支持网格遍历。
     *
     * 时间复杂度: O(1) 直接追加到引用链表
     *
     * @tparam SPECIFIC_TYPE 要插入的对象类型,编译器可自动推导
     * @param obj 要插入的对象指针,不能为 nullptr
     * @return 插入成功返回 true,失败返回 false
     *
     * @pre obj 必须是有效的对象指针
     * @pre SPECIFIC_TYPE 必须在 OBJECT_TYPES 中定义
     * @post 对象被添加到网格引用管理器中
     *
     * @note 对象不会被容器拥有,生命周期由外部管理
     */
    template<class SPECIFIC_TYPE>
    bool insert(SPECIFIC_TYPE *obj);

    ///  Removes the object from the container, and returns the removed object
    //template<class SPECIFIC_TYPE>
    //bool remove(SPECIFIC_TYPE* obj)
    //{
    //    SPECIFIC_TYPE* t = Trinity::Remove(i_elements, obj);
    //    return (t != nullptr);
    //}

    /**
     * @brief 获取内部容器元素的可修改引用
     *
     * 返回底层 ContainerMapList 的引用,用于高级操作如遍历所有类型。
     * 通常供内部系统使用,普通用户应使用 insert/Count 等接口。
     *
     * @return 内部容器的引用
     *
     * @warning 直接操作内部容器可能破坏数据一致性,谨慎使用
     */
    ContainerMapList<OBJECT_TYPES>& GetElements(void);

    /**
     * @brief 获取内部容器元素的只读引用
     *
     * 返回底层 ContainerMapList 的常量引用,用于只读访问。
     *
     * @return 内部容器的常量引用
     */
    const ContainerMapList<OBJECT_TYPES>& GetElements(void) const;

private:
    ContainerMapList<OBJECT_TYPES> i_elements;  ///< 多类型对象存储,编译时展开为多个 GridRefManager
};

// ==================== TypeMapContainer 模板实现 ====================

template <class OBJECT_TYPES>
TypeMapContainer<OBJECT_TYPES>::TypeMapContainer() = default;

template <class OBJECT_TYPES>
TypeMapContainer<OBJECT_TYPES>::~TypeMapContainer() = default;

template <class OBJECT_TYPES>
template <class SPECIFIC_TYPE>
size_t TypeMapContainer<OBJECT_TYPES>::Count() const
{
    // 调用 TypeContainerFunctions.h 中的 Count 函数
    // 通过模板参数推导定位到正确的 GridRefManager 并遍历计数
    return Trinity::Count(i_elements, (SPECIFIC_TYPE*)nullptr);
}

template <class OBJECT_TYPES>
template <class SPECIFIC_TYPE>
bool TypeMapContainer<OBJECT_TYPES>::insert(SPECIFIC_TYPE* obj)
{
    // 调用 TypeContainerFunctions.h 中的 Insert 函数
    // 将对象添加到对应类型的 GridRefManager 中,建立网格引用关系
    SPECIFIC_TYPE* t = Trinity::Insert(i_elements, obj);
    return (t != nullptr);
}

template <class OBJECT_TYPES>
ContainerMapList<OBJECT_TYPES>& TypeMapContainer<OBJECT_TYPES>::GetElements()
{
    return i_elements;
}

template <class OBJECT_TYPES>
const ContainerMapList<OBJECT_TYPES>& TypeMapContainer<OBJECT_TYPES>::GetElements() const
{
    return i_elements;
}

/**
 * @class TypeUnorderedMapContainer
 * @brief 多类型哈希容器 - 封装 ContainerUnorderedMap 提供快速的键值查找能力
 *
 * 基于 std::unordered_map 实现的多类型容器,支持通过键(如 GUID)快速查找特定对象。
 * 适用于需要频繁按 ID 查询对象的场景,性能优于线性遍历的 TypeMapContainer。
 *
 * 核心职责:
 * 1. 提供类型安全的键值对存储
 * 2. 支持高效的插入/删除/查找操作
 * 3. 按类型分类管理对象,避免运行时类型检查
 *
 * 与 TypeMapContainer 的区别:
 * - TypeMapContainer: 基于 GridRefManager,适合网格遍历,插入 O(1),查找 O(n)
 * - TypeUnorderedMapContainer: 基于 unordered_map,适合快速查找,所有操作平均 O(1)
 *
 * 典型使用场景:
 * @code
 * // 定义容器: 支持玩家和生物,键类型为 ObjectGuid
 * using ObjectTypes = TypeList<Player, TypeList<Creature, TypeNull>>;
 * TypeUnorderedMapContainer<ObjectTypes, ObjectGuid> objectMap;
 *
 * // 插入对象
 * objectMap.Insert(player->GetGUID(), player);
 *
 * // 快速查找
 * Player* found = objectMap.Find<Player>(playerGuid);
 *
 * // 删除对象
 * objectMap.Remove<Player>(playerGuid);
 * @endcode
 *
 * 性能特点:
 * - 插入: 平均 O(1), 最坏 O(n) (哈希冲突时)
 * - 删除: 平均 O(1), 最坏 O(n)
 * - 查找: 平均 O(1), 最坏 O(n)
 * - 空间: 每个元素额外存储哈希桶和链表指针
 *
 * 线程安全:
 * 非线程安全,外部调用需确保同步。在多线程环境中使用时需要加锁。
 *
 * @tparam OBJECT_TYPES 支持的对象类型列表
 * @tparam KEY_TYPE 键类型,通常是 ObjectGuid 或 uint64
 */
template<class OBJECT_TYPES, class KEY_TYPE>
class TypeUnorderedMapContainer
{
public:
    /**
     * @brief 默认构造函数
     *
     * 初始化所有类型的哈希表为空状态。
     */
    TypeUnorderedMapContainer();

    /**
     * @brief 拷贝构造函数 (默认)
     *
     * 深度拷贝所有哈希表。注意: 这会复制所有键值对,但不会复制对象本身。
     * 对于大型容器,拷贝操作开销较大,应考虑使用移动语义。
     */
    TypeUnorderedMapContainer(TypeUnorderedMapContainer const&) = default;

    /**
     * @brief 移动构造函数 (默认)
     *
     * 高效转移哈希表所有权,O(1) 复杂度。
     * 移动后源对象为空但有效。
     */
    TypeUnorderedMapContainer(TypeUnorderedMapContainer&&) noexcept = default;

    /**
     * @brief 拷贝赋值运算符 (默认)
     */
    TypeUnorderedMapContainer& operator=(TypeUnorderedMapContainer const&) = default;

    /**
     * @brief 移动赋值运算符 (默认)
     */
    TypeUnorderedMapContainer& operator=(TypeUnorderedMapContainer&&) noexcept = default;

    /**
     * @brief 析构函数
     *
     * 清理所有哈希表,但不删除存储的对象指针。
     * 对象生命周期由外部管理。
     */
    ~TypeUnorderedMapContainer();

    /**
     * @brief 插入键值对到容器中
     *
     * 将对象指针与指定键关联并存储到对应类型的哈希表中。
     * 如果键已存在,插入失败并返回 false。
     *
     * 时间复杂度: 平均 O(1)
     *
     * @tparam SPECIFIC_TYPE 要插入的对象类型
     * @param handle 键值,用于后续查找和删除
     * @param obj 对象指针,不能为 nullptr
     * @return 插入成功返回 true,键已存在或类型不匹配返回 false
     *
     * @pre handle 必须唯一,不应与现有键重复
     * @pre obj 不能为 nullptr
     * @post 对象可通过 handle 查找到
     *
     * @note 容器不拥有对象所有权,外部需确保对象生命周期
     */
    template<class SPECIFIC_TYPE>
    bool Insert(KEY_TYPE const& handle, SPECIFIC_TYPE* obj);

    /**
     * @brief 从容器中删除指定键的对象
     *
     * 移除键值对,但不删除对象本身。
     *
     * 时间复杂度: 平均 O(1)
     *
     * @tparam SPECIFIC_TYPE 要删除的对象类型
     * @param handle 要删除的键
     * @return 删除成功返回 true,键不存在返回 false
     *
     * @post 键值对从哈希表中移除,对象指针仍然有效
     */
    template<class SPECIFIC_TYPE>
    bool Remove(KEY_TYPE const& handle);

    /**
     * @brief 查找指定键的对象
     *
     * 在指定类型的哈希表中查找对象。
     *
     * 时间复杂度: 平均 O(1)
     *
     * @tparam SPECIFIC_TYPE 要查找的对象类型
     * @param handle 查找的键
     * @return 找到返回对象指针,未找到返回 nullptr
     *
     * @note 返回的指针在对象被删除或容器修改后可能失效
     */
    template<class SPECIFIC_TYPE>
    SPECIFIC_TYPE* Find(KEY_TYPE const& handle);

    /**
     * @brief 统计特定类型对象的数量
     *
     * 返回指定类型哈希表中的元素数量。
     *
     * 时间复杂度: O(1) (unordered_map 的 size() 方法)
     *
     * @tparam SPECIFIC_TYPE 要统计的对象类型
     * @return 该类型对象的数量
     */
    template<class SPECIFIC_TYPE>
    std::size_t Size() const;

    /**
     * @brief 获取内部容器的可修改引用
     *
     * 返回底层 ContainerUnorderedMap 的引用,用于高级操作。
     *
     * @return 内部容器的引用
     *
     * @warning 直接操作可能破坏数据一致性
     */
    ContainerUnorderedMap<OBJECT_TYPES, KEY_TYPE>& GetElements();

    /**
     * @brief 获取内部容器的只读引用
     *
     * @return 内部容器的常量引用
     */
    ContainerUnorderedMap<OBJECT_TYPES, KEY_TYPE> const& GetElements() const;

private:
    ContainerUnorderedMap<OBJECT_TYPES, KEY_TYPE> _elements;  ///< 多类型哈希表存储
};

// ==================== TypeUnorderedMapContainer 模板实现 ====================

template <class OBJECT_TYPES, class KEY_TYPE>
TypeUnorderedMapContainer<OBJECT_TYPES, KEY_TYPE>::TypeUnorderedMapContainer() = default;

template <class OBJECT_TYPES, class KEY_TYPE>
TypeUnorderedMapContainer<OBJECT_TYPES, KEY_TYPE>::~TypeUnorderedMapContainer() = default;

template <class OBJECT_TYPES, class KEY_TYPE>
template <class SPECIFIC_TYPE>
bool TypeUnorderedMapContainer<OBJECT_TYPES, KEY_TYPE>::Insert(KEY_TYPE const& handle, SPECIFIC_TYPE* obj)
{
    // 调用 TypeContainerFunctions.h 中的 Insert 函数
    // 通过模板参数推导定位到正确的哈希表并插入键值对
    return Trinity::Insert(_elements, handle, obj);
}

template <class OBJECT_TYPES, class KEY_TYPE>
template <class SPECIFIC_TYPE>
bool TypeUnorderedMapContainer<OBJECT_TYPES, KEY_TYPE>::Remove(KEY_TYPE const& handle)
{
    // 调用 TypeContainerFunctions.h 中的 Remove 函数
    // 从对应类型的哈希表中移除键值对
    return Trinity::Remove(_elements, handle, (SPECIFIC_TYPE*)nullptr);
}

template <class OBJECT_TYPES, class KEY_TYPE>
template <class SPECIFIC_TYPE>
SPECIFIC_TYPE* TypeUnorderedMapContainer<OBJECT_TYPES, KEY_TYPE>::Find(KEY_TYPE const& handle)
{
    // 调用 TypeContainerFunctions.h 中的 Find 函数
    // 在对应类型的哈希表中查找对象指针
    return Trinity::Find(_elements, handle, (SPECIFIC_TYPE*)nullptr);
}

template <class OBJECT_TYPES, class KEY_TYPE>
template <class SPECIFIC_TYPE>
std::size_t TypeUnorderedMapContainer<OBJECT_TYPES, KEY_TYPE>::Size() const
{
    // 调用 TypeContainerFunctions.h 中的 Size 函数
    // 获取指定类型哈希表的大小
    std::size_t size = 0;
    Trinity::Size(_elements, &size, (SPECIFIC_TYPE*)nullptr);
    return size;
}

template <class OBJECT_TYPES, class KEY_TYPE>
ContainerUnorderedMap<OBJECT_TYPES, KEY_TYPE>& TypeUnorderedMapContainer<OBJECT_TYPES, KEY_TYPE>::GetElements()
{
    return _elements;
}

template <class OBJECT_TYPES, class KEY_TYPE>
ContainerUnorderedMap<OBJECT_TYPES, KEY_TYPE> const& TypeUnorderedMapContainer<OBJECT_TYPES, KEY_TYPE>::GetElements() const
{
    return _elements;
}

#endif
