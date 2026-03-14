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
 * @file LootItemStorage.cpp
 * @brief 战利品持久化存储模块实现
 *
 * 该文件实现了可打开物品容器的战利品持久化存储功能。
 * 主要用于确保玩家在不同时间、不同服务器上打开同一个物品时能获得相同的战利品。
 *
 * 数据库表结构：
 * - item_instance_loot_items: 存储战利品物品信息
 * - item_instance_loot_money: 存储战利品金币信息
 *
 * 实现原理：
 * 1. 当玩家首次打开可打开物品时，生成战利品并保存到数据库
 * 2. 保存时记录物品GUID、物品ID、数量、各种标志位等信息
 * 3. 后续打开时从数据库加载已保存的战利品，而不是重新生成
 * 4. 玩家拾取物品后更新或删除对应的数据库记录
 *
 * 线程安全机制：
 * - 使用读写锁（shared_mutex）保护全局战利品存储
 * - 读操作（如加载战利品）使用共享锁
 * - 写操作（如保存、删除战利品）使用独占锁
 */

#include "LootItemStorage.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Loot.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "Player.h"

#include <unordered_map>

namespace
{
    /**
     * @brief 全局战利品存储容器
     *
     * 键：容器ID（物品GUID的低32位）
     * 值：存储的战利品容器对象
     *
     * 该容器在内存中缓存所有已保存的战利品数据，
     * 避免每次打开物品都需要查询数据库
     */
    std::unordered_map<uint32, StoredLootContainer> _lootItemStore;
}

/**
 * @brief 构造函数 - 从 LootItem 对象创建存储项
 *
 * 将战利品物品的所有属性复制到存储结构中，
 * 用于后续的数据库持久化操作
 *
 * @param lootItem 源战利品物品引用
 */
StoredLootItem::StoredLootItem(LootItem const& lootItem) : ItemId(lootItem.itemid), Count(lootItem.count), ItemIndex(lootItem.itemIndex), FollowRules(lootItem.follow_loot_rules),
FFA(lootItem.freeforall), Blocked(lootItem.is_blocked), Counted(lootItem.is_counted), UnderThreshold(lootItem.is_underthreshold),
NeedsQuest(lootItem.needs_quest), RandomPropertyId(lootItem.randomPropertyId), RandomSuffix(lootItem.randomSuffix)
{
}

/**
 * @brief 获取 LootItemStorage 单例实例
 *
 * 使用静态局部变量实现线程安全的单例模式（C++11 magic statics）
 *
 * @return LootItemStorage* 单例指针
 */
LootItemStorage* LootItemStorage::instance()
{
    static LootItemStorage instance;
    return &instance;
}

/**
 * @brief 获取全局读写锁
 *
 * 返回用于保护战利品存储的读写锁指针
 * 读写锁允许多个线程同时读取，但写操作需要独占访问
 *
 * @return std::shared_mutex* 读写锁指针
 */
std::shared_mutex* LootItemStorage::GetLock()
{
    static std::shared_mutex _lock;
    return &_lock;
}

/**
 * @brief 从数据库加载所有已存储的战利品
 *
 * 服务器启动时调用此函数，从数据库加载所有已保存的战利品数据到内存缓存中。
 * 这样可以避免每次打开可打开物品时都查询数据库，提高性能。
 *
 * 主要流程：
 * 1. 清空现有的内存缓存
 * 2. 从 item_instance_loot_items 表加载所有战利品物品
 * 3. 从 item_instance_loot_money 表加载所有金币数据
 * 4. 将数据组织到内存容器中，以容器ID为键
 *
 * 数据库字段说明（item_instance_loot_items）：
 * - [0] container_id: 容器ID（物品GUID低32位）
 * - [1] item_id: 物品ID
 * - [2] item_count: 物品数量
 * - [3] item_index: 物品索引
 * - [4] follow_rules: 是否遵循战利品规则
 * - [5] ffa: 是否为自由拾取
 * - [6] blocked: 是否被阻止
 * - [7] counted: 是否已统计
 * - [8] under_threshold: 是否低于阈值
 * - [9] needs_quest: 是否需要任务
 * - [10] rnd_prop: 随机属性ID
 * - [11] rnd_suffix: 随机后缀
 *
 * 调用时机：服务器启动初始化阶段
 *
 * 性能注意事项：
 * - 如果有大量未拾取的可打开物品，加载可能需要一定时间
 * - 建议在服务器启动阶段的最后执行，避免阻塞其他初始化
 */
void LootItemStorage::LoadStorageFromDB()
{
    uint32 oldMSTime = getMSTime();             // 记录开始时间，用于统计加载耗时
    _lootItemStore.clear();                     // 清空现有存储，支持重新加载
    uint32 count = 0;                           // 加载计数器

    // 第一步：加载所有战利品物品
    CharacterDatabaseTransaction trans = CharacterDatabaseTransaction(nullptr);
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_ITEMCONTAINER_ITEMS);
    PreparedQueryResult result = CharacterDatabase.Query(stmt);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            // 获取容器ID，用作存储键
            uint32 key = fields[0].GetUInt32();
            auto itr = _lootItemStore.find(key);
            if (itr == _lootItemStore.end())
            {
                // 如果容器不存在，创建新的容器对象
                bool added;
                std::tie(itr, added) = _lootItemStore.emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(key));

                ASSERT(added);                  // 确保插入成功
            }

            StoredLootContainer& storedContainer = itr->second;

            // 从数据库字段构建 LootItem 对象
            LootItem lootItem;
            lootItem.itemid = fields[1].GetUInt32();              // 物品ID
            lootItem.count = fields[2].GetUInt32();               // 物品数量
            lootItem.itemIndex = fields[3].GetUInt32();           // 物品索引
            lootItem.follow_loot_rules = fields[4].GetBool();     // 遵循战利品规则标志
            lootItem.freeforall = fields[5].GetBool();            // 自由拾取标志
            lootItem.is_blocked = fields[6].GetBool();            // 阻止标志
            lootItem.is_counted = fields[7].GetBool();            // 已统计标志
            lootItem.is_underthreshold = fields[8].GetBool();     // 低于阈值标志
            lootItem.needs_quest = fields[9].GetBool();           // 需要任务标志
            lootItem.randomPropertyId = fields[10].GetInt32();    // 随机属性ID
            lootItem.randomSuffix = fields[11].GetUInt32();       // 随机后缀

            // 将物品添加到容器（trans为nullptr，因为这是加载阶段不需要写入数据库）
            storedContainer.AddLootItem(lootItem, trans);

            ++count;
        } while (result->NextRow());

        TC_LOG_INFO("server.loading", ">> Loaded {} stored item loots in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    }
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 stored item loots");

    // 第二步：加载所有金币数据
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_ITEMCONTAINER_MONEY);
    result = CharacterDatabase.Query(stmt);
    if (result)
    {
        count = 0;
        do
        {
            Field* fields = result->Fetch();

            // 获取容器ID
            uint32 key = fields[0].GetUInt32();
            auto itr = _lootItemStore.find(key);
            if (itr == _lootItemStore.end())
            {
                // 如果容器不存在，创建新的容器对象
                bool added;
                std::tie(itr, added) = _lootItemStore.emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(key));

                ASSERT(added);
            }

            StoredLootContainer& storedContainer = itr->second;
            // 将金币添加到容器（trans为nullptr，因为这是加载阶段）
            storedContainer.AddMoney(fields[1].GetUInt32(), trans);

            ++count;
        } while (result->NextRow());

        TC_LOG_INFO("server.loading", ">> Loaded {} stored item money in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    }
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 stored item money");
}

/**
 * @brief 为指定物品加载已存储的战利品
 *
 * 当玩家打开可打开物品时，从内存缓存中加载之前保存的战利品数据。
 * 这确保了玩家在不同时间、不同服务器打开同一物品时获得相同的战利品。
 *
 * @param item 可打开物品对象指针
 * @param player 打开物品的玩家指针
 * @return true 成功加载战利品
 * @return false 该物品没有已存储的战利品
 *
 * 主要流程：
 * 1. 从内存缓存中查找该容器的战利品数据
 * 2. 加载金币数量到 loot 对象
 * 3. 遍历所有存储的物品，重建 LootItem 对象
 * 4. 从战利品模板复制条件信息（用于拾取权限判断）
 * 5. 如果物品在背包中，将玩家添加为允许拾取者
 * 6. 更新未拾取物品计数
 * 7. 标记物品已生成战利品，避免重复生成
 *
 * 调用时机：玩家打开可打开物品时（Item::IsOpened）
 *
 * 线程安全：使用共享锁保护读操作
 *
 * 性能注意事项：
 * - 从内存缓存加载，不涉及数据库查询
 * - 物品数量通常较少，性能影响可忽略
 */
bool LootItemStorage::LoadStoredLoot(Item* item, Player* player)
{
    Loot* loot = &item->loot;
    StoredLootContainer const* container = nullptr;

    // 使用共享锁读取战利品数据
    {
        std::shared_lock<std::shared_mutex> lock(*GetLock());

        auto itr = _lootItemStore.find(loot->containerID);
        if (itr == _lootItemStore.end())
            return false;                   // 没有找到已存储的战利品

        container = &itr->second;
    }

    // container 在此处不可能为 nullptr
    loot->gold = container->GetMoney();     // 加载金币数量

    // 获取物品的战利品模板，用于复制条件信息
    if (LootTemplate const* lt = LootTemplates_Item.GetLootFor(item->GetEntry()))
    {
        // 遍历所有存储的物品
        for (auto const& storedItemPair : container->GetLootItems())
        {
            // 构建 LootItem 对象
            LootItem li;
            li.itemid = storedItemPair.first;                     // 物品ID
            li.count = storedItemPair.second.Count;               // 物品数量
            li.itemIndex = storedItemPair.second.ItemIndex;       // 物品索引
            li.follow_loot_rules = storedItemPair.second.FollowRules;   // 遵循规则标志
            li.freeforall = storedItemPair.second.FFA;            // 自由拾取标志
            li.is_blocked = storedItemPair.second.Blocked;        // 阻止标志
            li.is_counted = storedItemPair.second.Counted;        // 已统计标志
            li.is_underthreshold = storedItemPair.second.UnderThreshold; // 低于阈值标志
            li.needs_quest = storedItemPair.second.NeedsQuest;    // 需要任务标志
            li.randomPropertyId = storedItemPair.second.RandomPropertyId; // 随机属性ID
            li.randomSuffix = storedItemPair.second.RandomSuffix; // 随机后缀

            // 从战利品模板复制条件列表到战利品物品
            // 条件用于判断玩家是否有权限拾取该物品
            lt->CopyConditions(&li);

            // 如果容器物品在背包中（bagSlot > 0），将打开者添加为允许拾取者
            // 这确保了只有打开物品的玩家才能拾取其中的战利品
            if (item->GetBagSlot())
                li.AddAllowedLooter(player);

            // 将 LootItem 添加到战利品容器
            loot->items.push_back(li);

            // 增加未拾取物品计数
            ++loot->unlootedCount;
        }
    }

    // 标记物品已生成战利品，防止下次打开时重复生成
    item->m_lootGenerated = true;
    return true;
}

/**
 * @brief 从指定容器中移除金币记录
 *
 * 玩家从可打开物品中拾取金币后调用此函数，
 * 清空内存缓存中的金币数量并删除数据库记录。
 *
 * @param containerId 容器ID（物品GUID低32位）
 *
 * 调用时机：玩家拾取可打开物品中的金币后
 *
 * 线程安全：使用独占锁保护写操作
 */
void LootItemStorage::RemoveStoredMoneyForContainer(uint32 containerId)
{
    // 使用独占锁进行写操作
    std::unique_lock<std::shared_mutex> lock(*GetLock());

    auto itr = _lootItemStore.find(containerId);
    if (itr == _lootItemStore.end())
        return;                     // 容器不存在，直接返回

    // 清空金币并删除数据库记录
    itr->second.RemoveMoney();
}

/**
 * @brief 完全删除指定容器的所有战利品记录
 *
 * 当可打开物品被完全拾取或被删除时调用此函数，
 * 从内存缓存和数据库中彻底删除该容器的所有数据。
 *
 * @param containerId 容器ID（物品GUID低32位）
 *
 * 主要流程：
 * 1. 从内存缓存中删除容器
 * 2. 从数据库删除所有物品记录
 * 3. 从数据库删除金币记录
 *
 * 调用时机：
 * - 物品中的所有战利品都被拾取后
 * - 可打开物品被玩家删除时
 * - 可打开物品过期被系统删除时
 *
 * 线程安全：使用独占锁保护写操作
 *
 * 性能注意事项：
 * - 使用数据库事务批量删除，减少数据库交互
 */
void LootItemStorage::RemoveStoredLootForContainer(uint32 containerId)
{
    // 使用独占锁进行写操作
    {
        std::unique_lock<std::shared_mutex> lock(*GetLock());
        _lootItemStore.erase(containerId);      // 从内存缓存中删除
    }

    // 使用数据库事务批量删除
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 删除所有物品记录
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEMCONTAINER_ITEMS);
    stmt->setUInt32(0, containerId);
    trans->Append(stmt);

    // 删除金币记录
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEMCONTAINER_MONEY);
    stmt->setUInt32(0, containerId);
    trans->Append(stmt);

    // 提交事务
    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 从指定容器中移除单个物品记录
 *
 * 玩家从可打开物品中拾取某个物品后调用此函数，
 * 从内存缓存和数据库中删除该物品的记录。
 *
 * @param containerId 容器ID（物品GUID低32位）
 * @param itemId 物品ID
 * @param count 物品数量
 * @param itemIndex 物品索引（用于精确定位，因为同一物品ID可能有多个实例）
 *
 * 调用时机：玩家拾取可打开物品中的某个物品后
 *
 * 线程安全：使用独占锁保护写操作
 *
 * 注意事项：
 * - 由于使用 multimap 存储物品，同一物品ID可能有多个实例
 * - 使用数量和索引双重匹配确保删除正确的物品
 */
void LootItemStorage::RemoveStoredLootItemForContainer(uint32 containerId, uint32 itemId, uint32 count, uint32 itemIndex)
{
    // 使用独占锁进行写操作
    std::unique_lock<std::shared_mutex> lock(*GetLock());

    auto itr = _lootItemStore.find(containerId);
    if (itr == _lootItemStore.end())
        return;                     // 容器不存在，直接返回

    // 从容器中删除指定物品
    itr->second.RemoveItem(itemId, count, itemIndex);
}

/**
 * @brief 为新生成的战利品创建存储记录
 *
 * 当玩家首次打开可打开物品时，将生成的战利品保存到内存缓存和数据库。
 * 这确保了玩家在不同时间、不同服务器打开同一物品时获得相同的战利品。
 *
 * @param loot 战利品对象指针
 * @param player 打开物品的玩家指针
 *
 * 主要流程：
 * 1. 检查战利品是否为空（全部被拾取）
 * 2. 检查是否已存在存储记录（防止重复保存）
 * 3. 创建新的战利品容器
 * 4. 保存金币数据
 * 5. 遍历所有战利品物品：
 *    - 检查玩家是否有权限获得该物品（阵营、任务等条件）
 *    - 排除货币代币（货币代币直接加入玩家货币池，不需要存储）
 *    - 保存符合条件的物品
 * 6. 提交数据库事务
 * 7. 将容器添加到内存缓存
 *
 * 调用时机：首次打开可打开物品并生成战利品后
 *
 * 线程安全：
 * - 先使用共享锁检查是否已存在
 * - 后使用独占锁插入新记录
 *
 * 过滤逻辑说明：
 * - 阵营限制：联盟玩家不会获得部落专属物品，反之亦然
 * - 任务限制：没有相关任务的玩家不会获得任务物品
 * - 货币代币：直接加入玩家货币池，不保存到战利品存储
 *
 * 性能注意事项：
 * - 使用数据库事务批量插入，减少数据库交互
 * - 先删除旧记录再插入新记录，确保数据一致性
 */
void LootItemStorage::AddNewStoredLoot(Loot* loot, Player* player)
{
    // 如果战利品已被完全拾取，不需要保存
    if (loot->isLooted()) // 没有金币也没有物品
        return;

    // 先使用共享锁检查是否已存在存储记录
    {
        std::shared_lock<std::shared_mutex> lock(*GetLock());

        auto itr = _lootItemStore.find(loot->containerID);
        if (itr != _lootItemStore.end())
        {
            // 已存在存储记录，记录错误日志（这不应该发生）
            TC_LOG_ERROR("misc", "Trying to store item loot by player: {} for container id: {} that is already in storage!", player->GetGUID().ToString(), loot->containerID);
            return;
        }
    }

    // 创建新的战利品容器
    StoredLootContainer container(loot->containerID);

    // 开始数据库事务
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    // 保存金币数据
    if (loot->gold)
        container.AddMoney(loot->gold, trans);

    // 先删除该容器的旧物品记录（如果存在）
    // 这确保了数据的干净状态，避免重复数据
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEMCONTAINER_ITEMS);
    stmt->setUInt32(0, loot->containerID);
    trans->Append(stmt);

    // 遍历所有战利品物品并保存
    for (LootItem const& li : loot->items)
    {
        // 战利品条件检查说明：
        // 战利品生成时不检查条件，条件只在发送给玩家时检查。
        // 对于可打开物品，战利品立即保存到数据库，这可能导致保存了
        // 玩家本不该获得的物品。以下检查防止这种情况，确保只有玩家
        // 应该获得的物品才会保存到数据库。
        // 例如：部落专属物品不会为联盟玩家保存
        if (!li.AllowedForPlayer(player, loot->lootOwnerGUID))
            continue;

        // 不保存货币代币
        // 货币代币（如正义点数、征服点数）在生成时直接加入玩家货币池
        // 不需要保存到战利品存储中
        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(li.itemid);
        if (!itemTemplate || itemTemplate->IsCurrencyToken())
            continue;

        // 保存物品到容器
        container.AddLootItem(li, trans);
    }

    // 提交数据库事务
    CharacterDatabase.CommitTransaction(trans);

    // 使用独占锁将容器添加到内存缓存
    {
        std::unique_lock<std::shared_mutex> lock(*GetLock());
        _lootItemStore.emplace(loot->containerID, std::move(container));
    }
}

/**
 * @brief 添加战利品物品到容器
 *
 * 将物品添加到内存容器（multimap），并可选地写入数据库。
 * 使用 multimap 允许同一个物品ID对应多个物品实例。
 *
 * @param lootItem 战利品物品引用
 * @param trans 数据库事务对象（如果为nullptr则不写入数据库）
 *
 * 数据库字段：
 * - container_id: 容器ID
 * - item_id: 物品ID
 * - item_count: 物品数量
 * - item_index: 物品索引
 * - follow_rules: 遵循战利品规则标志
 * - ffa: 自由拾取标志
 * - blocked: 阻止标志
 * - counted: 已统计标志
 * - under_threshold: 低于阈值标志
 * - needs_quest: 需要任务标志
 * - rnd_prop: 随机属性ID
 * - rnd_suffix: 随机后缀
 *
 * 调用时机：
 * - 服务器启动加载数据时（trans为nullptr）
 * - 首次打开可打开物品时（trans有效）
 */
void StoredLootContainer::AddLootItem(LootItem const& lootItem, CharacterDatabaseTransaction trans)
{
    // 将物品添加到内存容器（使用 multimap 允许同ID多个实例）
    _lootItems.emplace(std::piecewise_construct, std::forward_as_tuple(lootItem.itemid), std::forward_as_tuple(lootItem));

    // 如果没有数据库事务，则不写入数据库（加载阶段）
    if (!trans)
        return;

    // 准备数据库插入语句
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_ITEMCONTAINER_ITEMS);

    // 设置所有字段
    stmt->setUInt32(0, _containerId);                       // 容器ID
    stmt->setUInt32(1, lootItem.itemid);                    // 物品ID
    stmt->setUInt32(2, lootItem.count);                     // 物品数量
    stmt->setUInt32(3, lootItem.itemIndex);                 // 物品索引
    stmt->setBool(4, lootItem.follow_loot_rules);           // 遵循规则标志
    stmt->setBool(5, lootItem.freeforall);                  // 自由拾取标志
    stmt->setBool(6, lootItem.is_blocked);                  // 阻止标志
    stmt->setBool(7, lootItem.is_counted);                  // 已统计标志
    stmt->setBool(8, lootItem.is_underthreshold);           // 低于阈值标志
    stmt->setBool(9, lootItem.needs_quest);                 // 需要任务标志
    stmt->setInt32(10, lootItem.randomPropertyId);          // 随机属性ID
    stmt->setUInt32(11, lootItem.randomSuffix);             // 随机后缀

    // 将语句添加到事务
    trans->Append(stmt);
}

/**
 * @brief 添加金币到容器
 *
 * 设置容器的金币数量并写入数据库。
 * 如果已存在金币记录，会先删除旧记录再插入新记录。
 *
 * @param money 金币数量
 * @param trans 数据库事务对象（如果为nullptr则不写入数据库）
 *
 * 调用时机：
 * - 服务器启动加载数据时（trans为nullptr）
 * - 首次打开可打开物品时（trans有效）
 *
 * 数据库操作：
 * - 先删除旧的金币记录（如果存在）
 * - 再插入新的金币记录
 */
void StoredLootContainer::AddMoney(uint32 money, CharacterDatabaseTransaction trans)
{
    _money = money;                                         // 更新内存中的金币数量

    // 如果没有数据库事务，则不写入数据库（加载阶段）
    if (!trans)
        return;

    // 先删除旧的金币记录
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEMCONTAINER_MONEY);
    stmt->setUInt32(0, _containerId);
    trans->Append(stmt);

    // 插入新的金币记录
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_ITEMCONTAINER_MONEY);
    stmt->setUInt32(0, _containerId);                       // 容器ID
    stmt->setUInt32(1, _money);                             // 金币数量
    trans->Append(stmt);
}

/**
 * @brief 从容器中移除金币
 *
 * 清空容器的金币数量并从数据库删除金币记录。
 * 玩家拾取金币后调用此函数。
 *
 * 调用时机：玩家从可打开物品中拾取金币后
 *
 * 数据库操作：直接执行删除语句（不使用事务）
 */
void StoredLootContainer::RemoveMoney()
{
    _money = 0;                                             // 清空内存中的金币数量

    // 从数据库删除金币记录
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEMCONTAINER_MONEY);
    stmt->setUInt32(0, _containerId);
    CharacterDatabase.Execute(stmt);
}

/**
 * @brief 从容器中移除指定物品
 *
 * 从内存容器和数据库中删除指定的物品。
 * 由于使用 multimap 存储物品，需要通过数量来精确定位物品实例。
 *
 * @param itemId 物品ID
 * @param count 物品数量
 * @param itemIndex 物品索引
 *
 * 主要流程：
 * 1. 在 multimap 中查找所有匹配物品ID的条目
 * 2. 遍历找到的条目，匹配数量以定位具体实例
 * 3. 从内存容器中删除找到的条目
 * 4. 从数据库中删除对应记录
 *
 * 调用时机：玩家从可打开物品中拾取某个物品后
 *
 * 注意事项：
 * - 使用数量匹配而非索引匹配，因为索引可能变化
 * - 同一物品ID可能存在多个实例（如打开礼包获得多个相同物品）
 */
void StoredLootContainer::RemoveItem(uint32 itemId, uint32 count, uint32 itemIndex)
{
    // 获取所有匹配物品ID的条目范围
    auto bounds = _lootItems.equal_range(itemId);

    // 遍历所有匹配的条目，通过数量定位具体实例
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
    {
        if (itr->second.Count == count)
        {
            // 找到匹配的物品，从内存容器中删除
            _lootItems.erase(itr);
            break;
        }
    }

    // 从数据库删除物品记录
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEMCONTAINER_ITEM);
    stmt->setUInt32(0, _containerId);                       // 容器ID
    stmt->setUInt32(1, itemId);                             // 物品ID
    stmt->setUInt32(2, count);                              // 物品数量
    stmt->setUInt32(3, itemIndex);                          // 物品索引
    CharacterDatabase.Execute(stmt);
}
