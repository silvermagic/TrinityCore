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
 * @file GroupMgr.h
 * @brief 队伍管理器头文件
 *
 * 本文件定义了 GroupMgr 类，这是队伍系统的全局管理器，负责管理服务器上所有队伍的生命周期。
 * 采用单例模式，确保整个服务器只有一个队伍管理器实例。
 *
 * 主要功能：
 * - 队伍的创建、销毁和查找
 * - 队伍 GUID 和数据库存储 ID 的生成
 * - 从数据库加载队伍信息
 * - 队伍更新循环
 *
 * 设计模式：
 * - 单例模式：通过 sGroupMgr 宏访问唯一实例
 * - 两级索引：GUID 索引和数据库存储 ID 索引
 *
 * 性能注意事项：
 * - GUID 查找使用 map，时间复杂度 O(log n)
 * - 数据库存储 ID 查找使用 vector，时间复杂度 O(1)
 * - 适合读多写少的场景
 */

#ifndef _GROUPMGR_H
#define _GROUPMGR_H

#include "Group.h"

/**
 * @brief 队伍管理器类 - 管理服务器上所有队伍
 *
 * GroupMgr 是队伍系统的核心管理类，负责协调所有队伍的创建、查找、更新和销毁。
 * 它维护了两个索引系统：GUID 索引和数据库存储 ID 索引。
 *
 * 架构设计：
 * - 单例模式：全局唯一实例，通过 sGroupMgr 访问
 * - 双索引系统：
 *   - GroupStore: 按 GUID 查找，用于运行时查找
 *   - GroupDbStore: 按数据库存储 ID 查找，用于数据库操作
 *
 * 使用场景：
 * - 玩家创建/加入队伍
 * - 从数据库加载队伍数据
 * - 队伍定期更新
 * - 队伍解散
 *
 * 线程安全：
 * - 该类不是线程安全的，应该在主线程中调用
 * - 所有队伍操作都应在主线程执行
 *
 * @note 此类在服务器启动时加载所有持久化的队伍数据
 */
class TC_GAME_API GroupMgr
{
private:
    /**
     * @brief 私有构造函数（单例模式）
     *
     * 初始化队伍管理器，设置初始 GUID 和存储 ID 计数器。
     */
    GroupMgr();

    /**
     * @brief 私有析构函数
     *
     * 清理所有队伍对象，释放内存。
     */
    ~GroupMgr();

public:
    /**
     * @brief 获取单例实例
     * @return 队伍管理器的唯一实例指针
     *
     * 这是访问队伍管理器的标准方式。
     *
     * @note 线程安全的单例实现（C++11 静态局部变量）
     */
    static GroupMgr* instance();

    /** 队伍容器类型定义（按 GUID 索引） */
    typedef std::map<ObjectGuid::LowType, Group*> GroupContainer;
    /** 队伍数据库存储容器类型定义（按存储 ID 索引） */
    typedef std::vector<Group*>      GroupDbContainer;

    /**
     * @brief 根据队伍 GUID 获取队伍对象
     * @param guid 队伍的低 GUID
     * @return 队伍对象指针，如果不存在则返回 nullptr
     *
     * 使用 map 进行查找，时间复杂度 O(log n)。
     *
     * 调用时机：
     * - 玩家加入已存在的队伍
     * - 查找特定队伍
     * - 处理队伍相关操作
     *
     * 性能注意事项：
     * - 使用 map 查找，比 vector 稍慢但更灵活
     * - 避免在频繁调用的路径中使用
     */
    Group* GetGroupByGUID(ObjectGuid::LowType guid) const;

    /**
     * @brief 生成新的队伍数据库存储 ID
     * @return 新生成的存储 ID
     *
     * 分配一个唯一的数据库存储 ID 用于新队伍。
     * 会重用已释放的 ID，避免 ID 耗尽。
     *
     * 算法：
     * 1. 从 NextGroupDbStoreId 开始
     * 2. 查找下一个可用的 ID
     * 3. 更新 NextGroupDbStoreId 为下一个待检查的值
     *
     * 性能注意事项：
     * - 最坏情况下需要遍历整个容器
     * - 平均情况下 O(1)
     * - 如果 ID 耗尽会停止服务器
     *
     * @warning ID 耗尽会导致服务器关闭
     */
    uint32 GenerateNewGroupDbStoreId();

    /**
     * @brief 注册队伍的数据库存储 ID
     * @param storageId 存储ID
     * @param group 队伍对象指针
     *
     * 将队伍对象注册到数据库存储 ID 索引中。
     * 会自动扩展容器大小以容纳新的 ID。
     *
     * 调用时机：
     * - 创建新队伍时
     * - 从数据库加载队伍时
     */
    void   RegisterGroupDbStoreId(uint32 storageId, Group* group);

    /**
     * @brief 释放队伍的数据库存储 ID
     * @param group 要释放的队伍对象
     *
     * 将存储 ID 标记为可用，以便重用。
     * 同时更新 NextGroupDbStoreId 以允许 ID 重用。
     *
     * 调用时机：
     * - 队伍解散时
     * - 队伍被删除时
     */
    void   FreeGroupDbStoreId(Group* group);

    /**
     * @brief 设置下一个队伍数据库存储 ID
     * @param storageId 起始存储 ID
     *
     * 用于从数据库加载后恢复存储 ID 计数器。
     *
     * @note 仅在服务器启动加载数据库时调用
     */
    void   SetNextGroupDbStoreId(uint32 storageId) { NextGroupDbStoreId = storageId; };

    /**
     * @brief 根据数据库存储 ID 获取队伍对象
     * @param storageId 数据库存储 ID
     * @return 队伍对象指针，如果不存在则返回 nullptr
     *
     * 使用 vector 进行查找，时间复杂度 O(1)。
     * 比 GUID 查找更快，适合频繁调用的场景。
     *
     * 调用时机：
     * - 处理数据库操作时
     * - 快速查找队伍（已知存储 ID）
     */
    Group* GetGroupByDbStoreId(uint32 storageId) const;

    /**
     * @brief 设置队伍数据库存储容器大小
     * @param newSize 新的容器大小
     *
     * 预分配容器空间，提高性能。
     *
     * @note 仅在服务器启动时调用
     */
    void   SetGroupDbStoreSize(uint32 newSize) { GroupDbStore.resize(newSize); }

    /**
     * @brief 更新所有队伍
     * @param diff 距离上次更新的时间差（毫秒）
     *
     * 遍历所有队伍并调用其 Update 方法。
     * 用于处理队伍的定时任务，如队长离线检测。
     *
     * 调用时机：
     * - 每个世界更新周期（通常每 50ms）
     *
     * 性能注意事项：
     * - 时间复杂度 O(n)，n 为队伍数量
     * - 队伍较多时可能影响性能
     */
    void Update(uint32 diff);

    /**
     * @brief 从数据库加载所有队伍
     *
     * 在服务器启动时调用，从数据库加载所有持久化的队伍数据。
     * 包括队伍基本信息、成员信息和副本绑定信息。
     *
     * 加载流程：
     * 1. 清理无效的队伍数据（队长不存在、成员不足）
     * 2. 加载队伍基本信息
     * 3. 加载队伍成员信息
     * 4. 加载队伍副本绑定
     *
     * @note 仅在服务器启动时调用一次
     */
    void   LoadGroups();

    /**
     * @brief 生成新的队伍 GUID
     * @return 新生成的队伍 GUID 低值
     *
     * 为新队伍分配全局唯一的标识符。
     *
     * @warning GUID 耗尽会导致服务器关闭
     */
    ObjectGuid::LowType GenerateGroupId();

    /**
     * @brief 添加队伍到管理器
     * @param group 要添加的队伍对象
     *
     * 将队伍注册到 GUID 索引中。
     *
     * 调用时机：
     * - 创建新队伍时
     * - 从数据库加载队伍时
     */
    void   AddGroup(Group* group);

    /**
     * @brief 从管理器移除队伍
     * @param group 要移除的队伍对象
     *
     * 从 GUID 索引中移除队伍。
     * 不会删除队伍对象本身，仅从管理器中取消注册。
     *
     * 调用时机：
     * - 队伍解散时
     * - 删除队伍前
     */
    void   RemoveGroup(Group* group);

protected:
    /**
     * @brief 下一个队伍 GUID
     *
     * 用于生成新的队伍 GUID。每创建一个队伍就递增。
     * 范围：1 到 0xFFFFFFFE
     */
    ObjectGuid::LowType           NextGroupId;

    /**
     * @brief 下一个队伍数据库存储 ID
     *
     * 用于生成新的数据库存储 ID。会重用已释放的 ID。
     */
    uint32           NextGroupDbStoreId;

    /**
     * @brief 队伍容器（按 GUID 索引）
     *
     * 存储所有队伍对象，键为队伍的低 GUID。
     * 用于按 GUID 查找队伍。
     */
    GroupContainer   GroupStore;

    /**
     * @brief 队伍数据库存储容器（按存储 ID 索引）
     *
     * 存储所有队伍对象，索引为数据库存储 ID。
     * 用于快速按存储 ID 查找队伍。
     * 可能为 nullptr（表示 ID 已释放）。
     */
    GroupDbContainer GroupDbStore;
};

/**
 * @brief 队伍管理器全局访问宏
 *
 * 这是访问队伍管理器单例的标准方式。
 *
 * 使用示例：
 * @code
 * Group* group = sGroupMgr->GetGroupByGUID(guid);
 * @endcode
 */
#define sGroupMgr GroupMgr::instance()

#endif
