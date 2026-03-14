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
 * @file LootItemStorage.h
 * @brief 战利品持久化存储模块
 *
 * 该模块实现了可打开物品容器（如礼品包、宝箱等）的战利品持久化存储功能。
 * 当玩家首次打开可打开物品时，系统会生成战利品并保存到数据库中，
 * 确保玩家在不同时间、不同服务器上打开同一个物品时能获得相同的战利品。
 *
 * 主要功能：
 * 1. 将生成的战利品保存到数据库
 * 2. 从数据库加载已保存的战利品
 * 3. 管理战利品的金币和物品数据
 * 4. 提供线程安全的读写访问机制
 *
 * 使用场景：
 * - 玩家打开可打开物品（如礼品包、任务奖励包）
 * - 服务器重启后恢复未拾取的战利品
 * - 跨服场景中保持战利品一致性
 */

#ifndef __LOOTITEMSTORAGE_H
#define __LOOTITEMSTORAGE_H

#include "Define.h"
#include "DatabaseEnvFwd.h"

#include <shared_mutex>
#include <unordered_map>

class Item;
class Player;
struct Loot;
struct LootItem;

/**
 * @brief 已存储的战利品物品结构体
 *
 * 用于持久化存储单个战利品物品的信息
 * 包含物品ID、数量、属性以及各种战利品标志位
 */
struct StoredLootItem
{
    /**
     * @brief 构造函数 - 从 LootItem 对象创建存储项
     * @param lootItem 源战利品物品引用
     */
    explicit StoredLootItem(LootItem const& lootItem);

    uint32 ItemId;              // 物品ID
    uint32 Count;               // 物品数量
    uint32 ItemIndex;           // 物品在战利品列表中的索引
    bool FollowRules;           // 是否遵循战利品规则（团队分配规则）
    bool FFA;                   // 是否为自由拾取物品（Free For All）
    bool Blocked;               // 是否被阻止拾取（已被其他玩家锁定）
    bool Counted;               // 是否已计入战利品统计
    bool UnderThreshold;        // 是否低于分配阈值（用于团队分配）
    bool NeedsQuest;            // 是否需要相关任务才能拾取
    int32 RandomPropertyId;     // 随机属性ID（附魔属性）
    uint32 RandomSuffix;        // 随机后缀ID（用于随机属性物品）
};

/**
 * @brief 已存储的战利品容器类
 *
 * 管理单个可打开物品容器（如礼品包）的所有战利品数据
 * 包括金币和多个物品项，并提供数据库持久化操作
 *
 * 线程安全：
 * - 该类的成员方法不应直接调用，应通过 LootItemStorage 单例访问
 * - LootItemStorage 会使用读写锁保护对该类数据的访问
 */
class StoredLootContainer
{
    public:
        /** @brief 战利品物品容器类型定义 - 允许同一个物品ID对应多个物品 */
        typedef std::unordered_multimap<uint32 /*itemId*/, StoredLootItem> StoredLootItemContainer;

        /**
         * @brief 构造函数
         * @param containerId 容器物品的GUID低32位
         */
        explicit StoredLootContainer(uint32 containerId) : _containerId(containerId), _money(0) { }

        /**
         * @brief 添加战利品物品到容器
         *
         * 将物品添加到内存容器并写入数据库
         *
         * @param lootItem 战利品物品引用
         * @param trans 数据库事务对象（如果为nullptr则不写入数据库）
         *
         * 调用时机：首次打开可打开物品时
         */
        void AddLootItem(LootItem const& lootItem, CharacterDatabaseTransaction trans);

        /**
         * @brief 添加金币到容器
         *
         * 设置容器的金币数量并写入数据库
         *
         * @param money 金币数量
         * @param trans 数据库事务对象（如果为nullptr则不写入数据库）
         *
         * 调用时机：首次打开可打开物品时
         */
        void AddMoney(uint32 money, CharacterDatabaseTransaction trans);

        /**
         * @brief 从容器中移除金币
         *
         * 清空容器的金币数量并从数据库删除金币记录
         *
         * 调用时机：玩家拾取金币后
         */
        void RemoveMoney();

        /**
         * @brief 从容器中移除指定物品
         *
         * 从内存容器和数据库中删除指定的物品
         *
         * @param itemId 物品ID
         * @param count 物品数量
         * @param itemIndex 物品索引（用于精确定位）
         *
         * 调用时机：玩家拾取物品后
         */
        void RemoveItem(uint32 itemId, uint32 count, uint32 itemIndex);

        /** @brief 获取容器ID */
        uint32 GetContainer() const { return _containerId; }

        /** @brief 获取容器中的金币数量 */
        uint32 GetMoney() const { return _money; }

        /** @brief 获取容器中所有战利品物品（只读） */
        StoredLootItemContainer const& GetLootItems() const { return _lootItems; }

    private:
        StoredLootItemContainer _lootItems;     // 战利品物品容器 - 存储所有已生成的物品
        uint32 const _containerId;              // 容器ID - 物品GUID的低32位
        uint32 _money;                          // 金币数量
};

/**
 * @brief 战利品物品存储管理器类（单例模式）
 *
 * 负责管理所有可打开物品容器的战利品持久化存储
 * 实现了战利品的保存、加载、更新和删除功能
 *
 * 核心功能：
 * 1. 服务器启动时从数据库加载所有已保存的战利品
 * 2. 玩家打开可打开物品时，将生成的战利品保存到数据库
 * 3. 玩家拾取物品/金币后，更新数据库记录
 * 4. 物品被完全拾取后，从数据库删除相关记录
 *
 * 线程安全：
 * - 使用读写锁（shared_mutex）保护数据访问
 * - 读操作使用共享锁（shared_lock）
 * - 写操作使用独占锁（unique_lock）
 *
 * 性能考虑：
 * - 使用 unordered_map 实现O(1)查找
 * - 所有数据库操作使用事务批量处理
 * - 读多写少场景使用读写锁优化并发性能
 */
class LootItemStorage
{
    public:
        /**
         * @brief 获取单例实例
         * @return LootItemStorage* 单例指针
         */
        static LootItemStorage* instance();

        /**
         * @brief 获取读写锁指针
         * @return std::shared_mutex* 读写锁指针
         *
         * 用于在多线程环境下保护对战利品存储的访问
         */
        static std::shared_mutex* GetLock();

        /**
         * @brief 从数据库加载所有已存储的战利品
         *
         * 服务器启动时调用，从数据库加载所有已保存的战利品数据到内存
         *
         * 主要流程：
         * 1. 查询 item_instance_loot_items 表加载所有物品
         * 2. 查询 item_instance_loot_money 表加载所有金币
         * 3. 将数据组织到内存容器中
         *
         * 调用时机：服务器启动时
         *
         * 性能注意事项：
         * - 可能需要加载大量数据，建议在启动阶段完成
         * - 使用事务批量查询减少数据库压力
         */
        void LoadStorageFromDB();

        /**
         * @brief 为指定物品加载已存储的战利品
         *
         * 当玩家打开可打开物品时，从数据库加载之前保存的战利品
         *
         * @param item 可打开物品对象指针
         * @param player 打开物品的玩家指针
         * @return true 加载成功
         * @return false 该物品没有已存储的战利品
         *
         * 主要流程：
         * 1. 从内存存储中查找该容器的战利品
         * 2. 将金币和物品数据填充到物品的 loot 对象中
         * 3. 从战利品模板复制条件信息
         * 4. 标记物品已生成战利品
         *
         * 调用时机：玩家打开可打开物品时
         */
        bool LoadStoredLoot(Item* item, Player* player);

        /**
         * @brief 从指定容器中移除金币记录
         *
         * 玩家拾取金币后调用，删除数据库中的金币记录
         *
         * @param containerId 容器ID（物品GUID低32位）
         *
         * 调用时机：玩家从可打开物品中拾取金币后
         */
        void RemoveStoredMoneyForContainer(uint32 containerId);

        /**
         * @brief 完全删除指定容器的战利品记录
         *
         * 当物品被完全拾取或删除时调用，删除该容器的所有数据
         *
         * @param containerId 容器ID（物品GUID低32位）
         *
         * 调用时机：物品被完全拾取或物品被删除时
         */
        void RemoveStoredLootForContainer(uint32 containerId);

        /**
         * @brief 从指定容器中移除单个物品记录
         *
         * 玩家拾取某个物品后调用，从数据库删除该物品的记录
         *
         * @param containerId 容器ID（物品GUID低32位）
         * @param itemId 物品ID
         * @param count 物品数量
         * @param itemIndex 物品索引
         *
         * 调用时机：玩家从可打开物品中拾取某个物品后
         */
        void RemoveStoredLootItemForContainer(uint32 containerId, uint32 itemId, uint32 count, uint32 itemIndex);

        /**
         * @brief 为新生成的战利品创建存储记录
         *
         * 当玩家首次打开可打开物品时，将生成的战利品保存到数据库
         *
         * @param loot 战利品对象指针
         * @param player 打开物品的玩家指针
         *
         * 主要流程：
         * 1. 检查是否已存在存储记录（防止重复保存）
         * 2. 筛选玩家可见的物品（过滤阵营限制、任务限制等）
         * 3. 排除货币代币（货币代币直接发放，不存储）
         * 4. 将金币和物品保存到数据库
         *
         * 调用时机：首次打开可打开物品生成战利品后
         *
         * 注意事项：
         * - 只保存玩家允许获得的物品，避免保存无效数据
         * - 货币代币不保存，因为它们直接加入玩家货币池
         */
        void AddNewStoredLoot(Loot* loot, Player* player);

    private:
        LootItemStorage() { }       // 私有构造函数（单例模式）
        ~LootItemStorage() { }      // 私有析构函数（单例模式）
};

/** @brief 战利品存储管理器单例访问宏 */
#define sLootItemStorage LootItemStorage::instance()

#endif
