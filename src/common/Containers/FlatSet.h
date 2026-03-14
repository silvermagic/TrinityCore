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
 * @file FlatSet.h
 * @brief 扁平化集合容器实现
 *
 * 本模块实现了一个基于连续内存的有序集合容器 FlatSet。
 * FlatSet 使用连续存储（通常是 vector）来保存元素，并保持元素有序。
 *
 * 主要特点：
 * - 内存连续：相比基于树的 std::set，内存占用更紧凑，缓存友好性更好
 * - 有序存储：元素按比较器排序，支持二分查找
 * - 唯一性：不允许重复元素（类似 std::set）
 * - 适用场景：元素数量较少或读多写少的场景
 *
 * 性能权衡：
 * - 查找：O(log n) - 使用二分查找
 * - 插入：O(n) - 需要移动元素
 * - 删除：O(n) - 需要移动元素
 * - 遍历：O(n) - 连续内存，缓存友好
 *
 * 适合用于：
 * - 小规模集合（插入/删除成本相对较低）
 * - 频繁遍历的场景（缓存友好）
 * - 需要与 C API 交互的场景（内存连续）
 */

#ifndef TRINITYCORE_FLAT_SET_H
#define TRINITYCORE_FLAT_SET_H

#include <functional>
#include <vector>

namespace Trinity::Containers
{
/**
 * @class FlatSet
 * @brief 扁平化有序集合容器
 *
 * 基于连续存储的有序集合实现，使用二分查找维护元素的有序性和唯一性。
 * 模板参数允许自定义键类型、比较器和底层容器类型。
 *
 * @tparam Key 存储的元素类型
 * @tparam Compare 元素比较器，默认为 std::less<Key>（升序）
 * @tparam KeyContainer 底层容器类型，默认为 std::vector<Key>
 *
 * @note 该类提供类似 std::set 的接口，但使用连续内存存储
 * @note 元素在容器中始终保持有序状态
 */
template <class Key, class Compare = std::less<Key>, class KeyContainer = std::vector<Key>>
class FlatSet
{
public:
    /// 迭代器类型，直接使用底层容器的迭代器
    using iterator = typename KeyContainer::iterator;
    /// 常量迭代器类型，用于只读访问
    using const_iterator = typename KeyContainer::const_iterator;

    /**
     * @brief 检查集合是否为空
     *
     * @return bool 如果集合中没有任何元素，返回 true；否则返回 false
     *
     * @note 时间复杂度：O(1)
     */
    bool empty() const { return _storage.empty(); }

    /**
     * @brief 获取集合中的元素数量
     *
     * @return auto 元素数量（类型由底层容器决定，通常是 size_t）
     *
     * @note 时间复杂度：O(1)
     */
    auto size() const { return _storage.size(); }

    /**
     * @brief 获取指向集合起始位置的迭代器
     *
     * @return auto 指向第一个元素的迭代器
     *
     * @note 如果集合为空，返回的迭代器等于 end()
     */
    auto begin()  { return _storage.begin(); }

    /**
     * @brief 获取指向集合起始位置的常量迭代器
     *
     * @return auto 指向第一个元素的常量迭代器
     *
     * @note 如果集合为空，返回的迭代器等于 end()
     */
    auto begin() const { return _storage.begin(); }

    /**
     * @brief 获取指向集合末尾位置的迭代器
     *
     * @return auto 指向最后一个元素之后位置的迭代器（哨兵迭代器）
     *
     * @note 不要解引用此迭代器，它仅用于标记结束位置
     */
    auto end()  { return _storage.end(); }

    /**
     * @brief 获取指向集合末尾位置的常量迭代器
     *
     * @return auto 指向最后一个元素之后位置的常量迭代器（哨兵迭代器）
     *
     * @note 不要解引用此迭代器，它仅用于标记结束位置
     */
    auto end() const { return _storage.end(); }

    /**
     * @brief 查找元素（常量版本）
     *
     * 使用二分查找在有序集合中查找指定元素。
     * 由于集合元素是有序的，查找效率为对数级别。
     *
     * @param value 要查找的元素值
     * @return auto 指向找到元素的迭代器，如果未找到则返回 end()
     *
     * @note 时间复杂度：O(log n)
     * @note 使用 std::lower_bound 进行二分查找
     */
    auto find(Key const& value) const
    {
        auto end = this->end();
        // 使用二分查找定位第一个不小于 value 的位置
        auto itr = std::lower_bound(this->begin(), end, value, Compare());
        // 检查找到的位置是否确实等于 value
        // lower_bound 返回的位置可能大于 value，需要验证
        if (itr != end && Compare()(value, *itr))
            itr = end;  // value 不在集合中，返回 end()

        return itr;
    }

    /**
     * @brief 查找元素（非常量版本）
     *
     * 使用二分查找在有序集合中查找指定元素。
     * 由于集合元素是有序的，查找效率为对数级别。
     *
     * @param value 要查找的元素值
     * @return auto 指向找到元素的迭代器，如果未找到则返回 end()
     *
     * @note 时间复杂度：O(log n)
     * @note 使用 std::lower_bound 进行二分查找
     */
    auto find(Key const& value)
    {
        auto end = this->end();
        // 使用二分查找定位第一个不小于 value 的位置
        auto itr = std::lower_bound(this->begin(), end, value, Compare());
        // 检查找到的位置是否确实等于 value
        // lower_bound 返回的位置可能大于 value，需要验证
        if (itr != end && Compare()(value, *itr))
            itr = end;  // value 不在集合中，返回 end()

        return itr;
    }

    /**
     * @brief 原地构造并插入元素
     *
     * 在集合中原地构造一个新元素。如果元素已存在，则不插入。
     * 元素会被插入到正确的位置以保持集合有序。
     *
     * @tparam Args 构造函数参数类型
     * @param args 传递给元素构造函数的参数
     * @return std::pair<iterator, bool> 返回一个 pair：
     *         - first: 指向插入元素或已存在元素的迭代器
     *         - second: 如果插入成功返回 true，如果元素已存在返回 false
     *
     * @note 时间复杂度：O(n) - 需要进行二分查找 O(log n) 和可能的元素移动 O(n)
     * @note 性能说明：插入操作可能导致底层容器重新分配内存，触发元素拷贝/移动
     */
    template <class... Args>
    std::pair<iterator, bool> emplace(Args&&... args)
    {
        // 使用传入的参数构造新元素
        Key newElement(std::forward<Args>(args)...);
        auto end = this->end();
        // 查找插入位置：第一个不小于 newElement 的位置
        auto itr = std::lower_bound(this->begin(), end, newElement, Compare());
        // 检查是否已存在相等的元素
        // 如果 itr 不是 end 且 newElement 不小于 *itr，说明它们相等
        if (itr != end && !Compare()(newElement, *itr))
            return { itr, false };  // 元素已存在，返回已存在元素的迭代器和 false

        // 在正确位置插入新元素，保持有序
        return { _storage.emplace(itr, std::move(newElement)), true };
    }

    /**
     * @brief 插入元素
     *
     * 将指定元素插入集合。如果元素已存在，则不插入。
     * 这是 emplace 的便捷包装，适用于已有元素对象的情况。
     *
     * @param key 要插入的元素
     * @return std::pair<iterator, bool> 返回一个 pair：
     *         - first: 指向插入元素或已存在元素的迭代器
     *         - second: 如果插入成功返回 true，如果元素已存在返回 false
     *
     * @note 时间复杂度：O(n)
     * @note 内部调用 emplace 实现
     */
    std::pair<iterator, bool> insert(Key const& key) { return emplace(key); }

    /**
     * @brief 删除指定值的元素
     *
     * 从集合中删除等于指定值的所有元素（由于集合保证唯一性，最多删除一个）。
     *
     * @param key 要删除的元素值
     * @return std::size_t 实际删除的元素数量（0 或 1）
     *
     * @note 时间复杂度：O(n) - 查找 O(log n) + 删除并移动元素 O(n)
     */
    std::size_t erase(Key const& key)
    {
        // 查找要删除的元素
        auto itr = this->find(key);
        // 如果元素不存在，返回 0
        if (itr == this->end())
            return 0;

        // 删除元素并返回删除数量 1
        this->erase(itr);
        return 1;
    }

    /**
     * @brief 删除指定位置的元素
     *
     * 删除迭代器指向位置的元素。
     *
     * @param itr 指向要删除元素的迭代器
     * @return auto 指向被删除元素之后元素的迭代器
     *
     * @note 时间复杂度：O(n) - 可能需要移动元素
     * @warning 传入的迭代器必须是有效的且可解引用的
     */
    auto erase(const_iterator itr) { return _storage.erase(itr); }

    /**
     * @brief 清空集合
     *
     * 删除集合中的所有元素，使集合变为空。
     *
     * @note 时间复杂度：O(n) - 需要销毁所有元素
     * @note 操作后 size() 返回 0，empty() 返回 true
     */
    void clear() { _storage.clear(); }

    /**
     * @brief 释放未使用的内存
     *
     * 请求底层容器减少容量以匹配实际大小，释放未使用的内存。
     * 这是一个非绑定请求，具体行为取决于底层容器实现。
     *
     * @note 对于 vector，这会减少 capacity 到等于 size
     * @note 时间复杂度：取决于容器实现，通常涉及重新分配
     * @note 性能说明：可以在大量删除操作后调用以节省内存
     */
    void shrink_to_fit() { _storage.shrink_to_fit(); }

    /**
     * @brief 相等比较运算符
     *
     * 比较两个 FlatSet 是否相等。两个 FlatSet 相等当且仅当：
     * - 它们包含相同数量的元素
     * - 对应位置的元素都相等
     *
     * 由于 FlatSet 保持元素有序，可以直接比较底层容器。
     *
     * @param left 左侧 FlatSet
     * @param right 右侧 FlatSet
     * @return bool 如果两个集合相等返回 true，否则返回 false
     *
     * @note 时间复杂度：O(n)
     */
    friend bool operator==(FlatSet const& left, FlatSet const& right)
    {
        return left._storage == right._storage;
    }

    /**
     * @brief 不等比较运算符
     *
     * 比较两个 FlatSet 是否不相等。
     *
     * @param left 左侧 FlatSet
     * @param right 右侧 FlatSet
     * @return bool 如果两个集合不相等返回 true，否则返回 false
     *
     * @note 时间复杂度：O(n)
     * @note 内部调用 operator== 并取反
     */
    friend bool operator!=(FlatSet const& left, FlatSet const& right)
    {
        return !(left == right);
    }

private:
    /**
     * @brief 底层存储容器
     *
     * 用于存储集合元素的连续内存容器，默认为 std::vector。
     * 元素始终保持有序状态。
     */
    KeyContainer _storage;
};

} // namespace Trinity::Containers

#endif // TRINITYCORE_FLAT_SET_H
