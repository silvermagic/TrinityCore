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
 * @file BoundingIntervalHierarchyWrapper.h
 * @brief BIH 包装器类，提供延迟构建和动态更新功能
 *
 * 本文件定义了 BIHWrap 类，它是对基础 BIH 类的高级封装。
 * 主要功能：
 * - 延迟构建：只在需要时才重新构建树
 * - 动态更新：支持插入和删除对象
 * - 自动平衡：在多次修改后自动重新构建
 */

#ifndef _BIH_WRAP
#define _BIH_WRAP

#include "BoundingIntervalHierarchy.h"
#include <G3D/Table.h>
#include <G3D/Array.h>
#include <G3D/Set.h>

/**
 * @brief BIH 包装器模板类
 *
 * 提供对 BIH 树的高级封装，支持延迟构建和动态更新。
 * 该类通过维护对象列表和索引映射，实现高效的插入和删除操作。
 *
 * @tparam T 对象类型
 * @tparam BoundsFunc 获取对象包围盒的函数对象类型，默认为 BoundsTrait<T>
 *
 * 使用场景：
 * - 动态场景中的碰撞检测
 * - 需要频繁添加/删除对象的场景
 * - 游戏对象的射线检测
 *
 * 特性：
 * - 延迟构建：只有在调用查询函数时才会触发树的重建
 * - 增量更新：支持单独添加或删除对象
 * - 自动平衡：在对象修改次数达到阈值后自动重建树
 */
template<class T, class BoundsFunc = BoundsTrait<T> >
class BIHWrap
{
    /**
     * @brief 模型回调包装器
     *
     * 将对象的射线/点相交回调转换为 BIH 树需要的回调格式。
     * 该结构充当适配器，将对象指针数组的索引转换为实际对象引用。
     *
     * @tparam RayCallback 用户提供的回调函数类型
     */
    template<class RayCallback>
    struct MDLCallback
    {
        const T* const* objects;      ///< 对象指针数组
        RayCallback& _callback;       ///< 用户提供的回调函数
        uint32 objects_size;          ///< 对象数组大小

        /**
         * @brief 构造函数
         * @param callback 用户回调函数
         * @param objects_array 对象指针数组
         * @param objects_size 数组大小
         */
        MDLCallback(RayCallback& callback, const T* const* objects_array, uint32 objects_size ) : objects(objects_array), _callback(callback), objects_size(objects_size) { }

        /**
         * @brief 射线相交回调
         *
         * @param ray 射线
         * @param idx 对象在数组中的索引
         * @param maxDist 最大距离（输入/输出）
         * @param stopAtFirst 是否在第一次相交时停止
         * @return true 发生相交
         * @return false 未相交
         */
        bool operator() (const G3D::Ray& ray, uint32 idx, float& maxDist, bool /*stopAtFirst*/)
        {
            if (idx >= objects_size)
                return false;
            if (const T* obj = objects[idx])
                return _callback(ray, *obj, maxDist/*, stopAtFirst*/);
            return false;
        }

        /**
         * @brief 点相交回调
         *
         * @param p 点坐标
         * @param idx 对象在数组中的索引
         */
        void operator() (const G3D::Vector3& p, uint32 idx)
        {
            if (idx >= objects_size)
                return;
            if (const T* obj = objects[idx])
                _callback(p, *obj);
        }
    };

    typedef G3D::Array<const T*> ObjArray;  ///< 对象数组类型定义

    BIH m_tree;                             ///< 底层 BIH 树结构
    ObjArray m_objects;                     ///< 对象指针数组
    G3D::Table<const T*, uint32> m_obj2Idx; ///< 对象到索引的映射表
    G3D::Set<const T*> m_objects_to_push;   ///< 待添加的对象集合
    int unbalanced_times;                   ///< 未平衡的修改次数

public:
    /**
     * @brief 构造函数
     *
     * 初始化一个空的 BIH 包装器
     */
    BIHWrap() : unbalanced_times(0) { }

    /**
     * @brief 插入对象
     *
     * 将对象添加到待插入集合，不会立即重建树。
     * 树会在下次查询时自动重建。
     *
     * @param obj 要插入的对象引用
     *
     * @note 延迟插入：对象被添加到 m_objects_to_push 集合，
     *       在 balance() 时才会真正加入树结构
     */
    void insert(const T& obj)
    {
        ++unbalanced_times;
        m_objects_to_push.insert(&obj);
    }

    /**
     * @brief 删除对象
     *
     * 从树中移除对象，如果对象在映射表中则标记为 nullptr，
     * 否则从待插入集合中移除。
     *
     * @param obj 要删除的对象引用
     *
     * @note 删除操作通过将对象指针设为 nullptr 实现，
     *       实际的树重建会延迟到下次查询时
     */
    void remove(const T& obj)
    {
        ++unbalanced_times;
        uint32 Idx = 0;
        const T * temp;
        if (m_obj2Idx.getRemove(&obj, temp, Idx))
            m_objects[Idx] = nullptr;  // 标记为已删除
        else
            m_objects_to_push.remove(&obj);  // 从待插入集合中移除
    }

    /**
     * @brief 平衡树
     *
     * 重新构建 BIH 树，将所有有效对象（包括待插入的）加入树中。
     * 只有在有修改时才会执行重建。
     *
     * @note 平衡流程：
     *       1. 清空对象数组
     *       2. 从映射表和待插入集合中收集所有对象
     *       3. 重新构建 BIH 树
     *
     * @note 性能考虑：重建树的代价较高，应避免频繁调用
     */
    void balance()
    {
        if (unbalanced_times == 0)
            return;

        unbalanced_times = 0;
        m_objects.fastClear();
        m_obj2Idx.getKeys(m_objects);  // 获取已存在的对象
        m_objects_to_push.getMembers(m_objects);  // 添加待插入的对象

        // 重新构建树
        m_tree.build(m_objects, BoundsFunc::getBounds2);
    }

    /**
     * @brief 射线相交测试
     *
     * 测试射线与树中所有对象的相交情况。
     * 会自动触发树的平衡（如果有待处理的修改）。
     *
     * @tparam RayCallback 回调函数类型
     * @param ray 射线
     * @param intersectCallback 相交回调函数
     * @param maxDist 最大检测距离（输入/输出）
     *
     * @note 在查询前会自动调用 balance() 确保树是最新的
     */
    template<typename RayCallback>
    void intersectRay(const G3D::Ray& ray, RayCallback& intersectCallback, float& maxDist)
    {
        balance();  // 确保树是最新的
        MDLCallback<RayCallback> temp_cb(intersectCallback, m_objects.getCArray(), m_objects.size());
        m_tree.intersectRay(ray, temp_cb, maxDist, true);
    }

    /**
     * @brief 点相交测试
     *
     * 测试点是否在树中任何对象内部。
     * 会自动触发树的平衡（如果有待处理的修改）。
     *
     * @tparam IsectCallback 回调函数类型
     * @param point 点坐标
     * @param intersectCallback 相交回调函数
     *
     * @note 在查询前会自动调用 balance() 确保树是最新的
     */
    template<typename IsectCallback>
    void intersectPoint(const G3D::Vector3& point, IsectCallback& intersectCallback)
    {
        balance();  // 确保树是最新的
        MDLCallback<IsectCallback> callback(intersectCallback, m_objects.getCArray(), m_objects.size());
        m_tree.intersectPoint(point, callback);
    }
};

#endif // _BIH_WRAP
