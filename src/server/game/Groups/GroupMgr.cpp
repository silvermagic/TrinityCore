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
 * @file GroupMgr.cpp
 * @brief 队伍管理器实现文件
 *
 * 本文件实现了 GroupMgr 类的所有方法，包括队伍的创建、查找、更新和数据库加载功能。
 *
 * 主要功能模块：
 * - 单例管理：instance() 方法实现单例模式
 * - ID 生成：GenerateGroupId() 和 GenerateNewGroupDbStoreId()
 * - 队伍查找：GetGroupByGUID() 和 GetGroupByDbStoreId()
 * - 数据库加载：LoadGroups()
 * - 更新循环：Update()
 */

#include "GroupMgr.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "InstanceSaveMgr.h"
#include "Log.h"
#include "World.h"

/**
 * @brief GroupMgr 构造函数
 *
 * 初始化队伍管理器，设置 ID 计数器的初始值。
 * 从 1 开始，因为 0 通常用于表示无效 ID。
 */
GroupMgr::GroupMgr()
{
    NextGroupDbStoreId = 1;  // 数据库存储 ID 从 1 开始
    NextGroupId = 1;         // 队伍 GUID 从 1 开始
}

/**
 * @brief GroupMgr 析构函数
 *
 * 清理所有队伍对象，释放内存。
 * 遍历 GroupStore 并删除所有队伍对象。
 *
 * @note 此析构函数在服务器关闭时调用
 */
GroupMgr::~GroupMgr()
{
    // 遍历所有队伍并删除
    for (GroupContainer::iterator itr = GroupStore.begin(); itr != GroupStore.end(); ++itr)
        delete itr->second;
}

/**
 * @brief 生成新的队伍数据库存储 ID
 * @return 新生成的存储 ID
 *
 * 分配一个唯一的数据库存储 ID，用于新队伍的数据库记录。
 * 实现了 ID 重用机制，避免 ID 耗尽。
 *
 * 算法流程：
 * 1. 记录当前 NextGroupDbStoreId 作为新 ID
 * 2. 从下一个位置开始查找可用的 ID
 * 3. 可用条件：位置超出当前容器大小 或 该位置为 nullptr
 * 4. 更新 NextGroupDbStoreId 为找到的可用位置
 *
 * 性能优化：
 * - 优先使用较小的 ID，提高缓存命中率
 * - 避免容器无限增长
 *
 * @warning 如果 ID 溢出（达到 0xFFFFFFFF），服务器将关闭
 */
uint32 GroupMgr::GenerateNewGroupDbStoreId()
{
    uint32 newStorageId = NextGroupDbStoreId;  // 记录要返回的新 ID

    // 从下一个位置开始查找可用的 ID
    for (uint32 i = ++NextGroupDbStoreId; i < 0xFFFFFFFF; ++i)
    {
        // 检查该位置是否可用（超出范围或为 nullptr）
        if ((i < GroupDbStore.size() && GroupDbStore[i] == nullptr) || i >= GroupDbStore.size())
        {
            NextGroupDbStoreId = i;  // 更新下一次搜索的起始位置
            break;
        }
    }

    // 检查是否发生溢出
    if (newStorageId == NextGroupDbStoreId)
    {
        TC_LOG_ERROR("misc", "Group storage ID overflow!! Can't continue, shutting down server. ");
        World::StopNow(ERROR_EXIT_CODE);  // 停止服务器
    }

    return newStorageId;
}

/**
 * @brief 注册队伍的数据库存储 ID
 * @param storageId 存储ID
 * @param group 队伍对象指针
 *
 * 将队伍对象添加到数据库存储索引中。
 * 如果容器不够大，会自动扩展。
 *
 * @note storageId 应该是通过 GenerateNewGroupDbStoreId() 获得的
 */
void GroupMgr::RegisterGroupDbStoreId(uint32 storageId, Group* group)
{
    // Allocate space if necessary.
    // 如果需要，扩展容器大小以容纳新的存储 ID
    if (storageId >= uint32(GroupDbStore.size()))
        GroupDbStore.resize(storageId + 1);

    // 将队伍对象存储到指定位置
    GroupDbStore[storageId] = group;
}

/**
 * @brief 释放队伍的数据库存储 ID
 * @param group 要释放的队伍对象
 *
 * 将存储 ID 标记为可用，允许重用。
 * 同时更新 NextGroupDbStoreId，确保优先重用较小的 ID。
 *
 * @note 此方法在队伍解散时调用
 */
void GroupMgr::FreeGroupDbStoreId(Group* group)
{
    uint32 storageId = group->GetDbStoreId();  // 获取队伍的存储 ID

    // 如果释放的 ID 小于当前的 NextGroupDbStoreId，更新它
    // 这样可以在下次分配时优先使用这个较小的 ID
    if (storageId < NextGroupDbStoreId)
        NextGroupDbStoreId = storageId;

    // 将该位置标记为可用（nullptr）
    GroupDbStore[storageId] = nullptr;
}

/**
 * @brief 根据数据库存储 ID 获取队伍对象
 * @param storageId 数据库存储 ID
 * @return 队伍对象指针，如果不存在则返回 nullptr
 *
 * 使用 vector 的下标访问，时间复杂度 O(1)。
 * 这是查找队伍的最快方式。
 *
 * @note 会检查边界，超出范围返回 nullptr
 */
Group* GroupMgr::GetGroupByDbStoreId(uint32 storageId) const
{
    // 边界检查
    if (storageId < GroupDbStore.size())
        return GroupDbStore[storageId];  // 返回队伍指针（可能为 nullptr）

    return nullptr;  // 超出范围，返回 nullptr
}

/**
 * @brief 生成新的队伍 GUID
 * @return 新生成的队伍 GUID 低值
 *
 * 为新队伍分配一个全局唯一的标识符。
 * 每次调用都会递增计数器。
 *
 * @warning 如果 GUID 达到最大值（0xFFFFFFFE），服务器将关闭
 *          这通常在创建超过 40 亿次队伍后才会发生
 */
ObjectGuid::LowType GroupMgr::GenerateGroupId()
{
    // 检查是否达到最大值
    if (NextGroupId >= 0xFFFFFFFE)
    {
        TC_LOG_ERROR("misc", "Group guid overflow!! Can't continue, shutting down server. ");
        World::StopNow(ERROR_EXIT_CODE);  // 停止服务器
    }
    return NextGroupId++;  // 返回当前值并递增
}

/**
 * @brief 获取队伍管理器单例实例
 * @return 队伍管理器的唯一实例指针
 *
 * 实现单例模式，使用 C++11 的静态局部变量保证线程安全。
 * 整个服务器只有一个 GroupMgr 实例。
 *
 * @return 静态实例的指针
 *
 * @note 使用 sGroupMgr 宏访问，而不是直接调用此方法
 */
GroupMgr* GroupMgr::instance()
{
    static GroupMgr instance;  // C++11 保证线程安全的静态初始化
    return &instance;
}

/**
 * @brief 根据队伍 GUID 获取队伍对象
 * @param groupId 队伍的低 GUID
 * @return 队伍对象指针，如果不存在则返回 nullptr
 *
 * 使用 map 的 find 方法，时间复杂度 O(log n)。
 *
 * 调用时机：
 * - 处理队伍相关数据包时
 * - 查找特定队伍时
 */
Group* GroupMgr::GetGroupByGUID(ObjectGuid::LowType groupId) const
{
    // 在 map 中查找
    GroupContainer::const_iterator itr = GroupStore.find(groupId);
    if (itr != GroupStore.end())
        return itr->second;  // 找到，返回队伍指针

    return nullptr;  // 未找到，返回 nullptr
}

/**
 * @brief 更新所有队伍
 * @param diff 距离上次更新的时间差（毫秒）
 *
 * 遍历所有队伍并调用其 Update 方法。
 * 用于处理队伍的定时任务，例如：
 * - 队长离线检测
 * - 战利品掷骰超时
 * - 其他定时事件
 *
 * 性能注意事项：
 * - 时间复杂度 O(n)
 * - 如果队伍数量很多，可能需要优化
 */
void GroupMgr::Update(uint32 diff)
{
    // 遍历所有队伍并更新
    for (auto group : GroupStore)
        group.second->Update(diff);
}

/**
 * @brief 添加队伍到管理器
 * @param group 要添加的队伍对象
 *
 * 将队伍添加到 GUID 索引中。
 * 键为队伍的低 GUID，值为队伍指针。
 *
 * @note 不会添加到数据库存储索引，需要单独调用 RegisterGroupDbStoreId()
 */
void GroupMgr::AddGroup(Group* group)
{
    GroupStore[group->GetLowGUID()] = group;
}

/**
 * @brief 从管理器移除队伍
 * @param group 要移除的队伍对象
 *
 * 从 GUID 索引中移除队伍。
 * 只是从 map 中删除条目，不会删除队伍对象本身。
 *
 * @note 通常在队伍解散时调用
 */
void GroupMgr::RemoveGroup(Group* group)
{
    GroupStore.erase(group->GetLowGUID());
}

/**
 * @brief 从数据库加载所有队伍
 *
 * 在服务器启动时调用，从数据库加载所有持久化的队伍数据。
 * 分为三个阶段：
 * 1. 加载队伍基本信息
 * 2. 加载队伍成员信息
 * 3. 加载队伍副本绑定
 *
 * 加载流程：
 * 1. 执行数据清理 SQL，删除无效数据
 * 2. 查询并加载队伍基本信息
 * 3. 查询并加载成员信息
 * 4. 查询并加载副本绑定
 *
 * 数据一致性保证：
 * - 删除队长不存在的队伍
 * - 删除成员少于 2 人的队伍
 * - 删除引用不存在队伍的成员记录
 * - 删除不存在的成员记录
 *
 * @note 此方法仅在服务器启动时调用一次
 */
void GroupMgr::LoadGroups()
{
    // ========== 第一阶段：加载队伍基本信息 ==========
    {
        uint32 oldMSTime = getMSTime();  // 记录开始时间

        // Delete all groups whose leader does not exist
        // 删除队长不存在的队伍（数据一致性清理）
        CharacterDatabase.DirectExecute("DELETE FROM `groups` WHERE leaderGuid NOT IN (SELECT guid FROM characters)");
        // Delete all groups with less than 2 members
        // 删除成员少于 2 人的队伍（队伍至少需要 2 人）
        CharacterDatabase.DirectExecute("DELETE FROM `groups` WHERE guid NOT IN (SELECT guid FROM group_member GROUP BY guid HAVING COUNT(guid) > 1)");

        //                                                        0              1           2             3                 4      5          6      7         8       9
        // 查询队伍基本信息，包括队长、战利品设置、目标图标、类型、难度等
        QueryResult result = CharacterDatabase.Query("SELECT g.leaderGuid, g.lootMethod, g.looterGuid, g.lootThreshold, g.icon1, g.icon2, g.icon3, g.icon4, g.icon5, g.icon6"
            //  10         11          12         13              14                  15            16        17          18
            ", g.icon7, g.icon8, g.groupType, g.difficulty, g.raidDifficulty, g.masterLooterGuid, g.guid, lfg.dungeon, lfg.state FROM `groups` g LEFT JOIN lfg_data lfg ON lfg.guid = g.guid ORDER BY g.guid ASC");
        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 group definitions. DB table `groups` is empty!");
            return;  // 没有队伍数据，直接返回
        }

        uint32 count = 0;
        do
        {
            Field* fields = result->Fetch();
            Group* group = new Group;  // 创建新队伍对象
            group->LoadGroupFromDB(fields);  // 从数据库字段加载队伍信息
            AddGroup(group);  // 添加到管理器

            // Get the ID used for storing the group in the database and register it in the pool.
            // 获取队伍的数据库存储 ID 并注册到索引池中
            uint32 storageId = group->GetDbStoreId();

            RegisterGroupDbStoreId(storageId, group);

            // Increase the next available storage ID
            // 更新下一个可用的存储 ID
            if (storageId == NextGroupDbStoreId)
                NextGroupDbStoreId++;

            ++count;
        }
        while (result->NextRow());

        TC_LOG_INFO("server.loading", ">> Loaded {} group definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    }

    // ========== 第二阶段：加载队伍成员信息 ==========
    TC_LOG_INFO("server.loading", "Loading Group members...");
    {
        uint32 oldMSTime = getMSTime();

        // Delete all rows from group_member or group_instance with no group
        // 删除引用不存在队伍的成员记录和副本记录（数据一致性清理）
        CharacterDatabase.DirectExecute("DELETE FROM group_member WHERE guid NOT IN (SELECT guid FROM `groups`)");
        CharacterDatabase.DirectExecute("DELETE FROM group_instance WHERE guid NOT IN (SELECT guid FROM `groups`)");
        // Delete all members that does not exist
        // 删除不存在的成员记录（角色已被删除）
        CharacterDatabase.DirectExecute("DELETE FROM group_member WHERE memberGuid NOT IN (SELECT guid FROM characters)");

        //                                                    0        1           2            3       4
        // 查询成员信息：队伍存储 ID、成员 GUID、成员标志、子组编号、角色
        QueryResult result = CharacterDatabase.Query("SELECT guid, memberGuid, memberFlags, subgroup, roles FROM group_member ORDER BY guid");
        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 group members. DB table `group_member` is empty!");
            return;
        }

        uint32 count = 0;

        do
        {
            Field* fields = result->Fetch();
            Group* group = GetGroupByDbStoreId(fields[0].GetUInt32());  // 根据存储 ID 查找队伍

            if (group)
                // 加载成员信息：成员 GUID、标志、子组、角色
                group->LoadMemberFromDB(fields[1].GetUInt32(), fields[2].GetUInt8(), fields[3].GetUInt8(), fields[4].GetUInt8());
            else
                // 这种情况不应该发生（已经执行了一致性 SQL）
                TC_LOG_ERROR("misc", "GroupMgr::LoadGroups: Consistency failed, can't find group (storage id: {})", fields[0].GetUInt32());

            ++count;
        }
        while (result->NextRow());

        TC_LOG_INFO("server.loading", ">> Loaded {} group members in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    }

    // ========== 第三阶段：加载队伍副本绑定 ==========
    TC_LOG_INFO("server.loading", "Loading Group instance saves...");
    {
        uint32 oldMSTime = getMSTime();

        //                                                   0        1      2            3             4             5
        // 查询队伍副本绑定：队伍存储 ID、地图 ID、副本实例 ID、是否永久绑定、难度、重置时间
        QueryResult result = CharacterDatabase.Query("SELECT gi.guid, i.map, gi.instance, gi.permanent, i.difficulty, i.resettime, "
            //           6
            // 子查询：检查是否有队长对该副本有永久绑定（用于判断队伍绑定是否永久）
            "(SELECT COUNT(1) FROM character_instance ci LEFT JOIN `groups` g ON ci.guid = g.leaderGuid WHERE ci.instance = gi.instance AND ci.permanent = 1 LIMIT 1) "
            "FROM group_instance gi LEFT JOIN instance i ON gi.instance = i.id ORDER BY guid");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 group-instance saves. DB table `group_instance` is empty!");
            return;
        }

        uint32 count = 0;
        do
        {
            Field* fields = result->Fetch();
            Group* group = GetGroupByDbStoreId(fields[0].GetUInt32());
            // group will never be NULL (we have run consistency sql's before loading)
            // 队伍不会为 NULL（已经执行了一致性 SQL）
            ASSERT(group);

            // 验证地图条目
            MapEntry const* mapEntry = sMapStore.LookupEntry(fields[1].GetUInt16());
            if (!mapEntry || !mapEntry->IsDungeon())
            {
                TC_LOG_ERROR("sql.sql", "Incorrect entry in group_instance table : no dungeon map {}", fields[1].GetUInt16());
                continue;  // 跳过无效的地图
            }

            // 验证难度值
            uint32 diff = fields[4].GetUInt8();
            if (diff >= uint32(mapEntry->IsRaid() ? MAX_RAID_DIFFICULTY : MAX_DUNGEON_DIFFICULTY))
            {
                TC_LOG_ERROR("sql.sql", "Wrong dungeon difficulty use in group_instance table: {}", diff + 1);
                diff = 0;                                   // default for both difficaly types
                // 使用默认难度 0
            }

            // 创建或获取副本存档，并绑定到队伍
            InstanceSave* save = sInstanceSaveMgr->AddInstanceSave(mapEntry->ID, fields[2].GetUInt32(), Difficulty(diff), time_t(fields[5].GetUInt64()), fields[6].GetUInt64() == 0, true);
            group->BindToInstance(save, fields[3].GetBool(), true);
            ++count;
        }
        while (result->NextRow());

        TC_LOG_INFO("server.loading", ">> Loaded {} group-instance saves in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    }
}
