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
 * @file MapInstanced.cpp
 * @brief 实例地图管理器实现文件
 *
 * 本文件实现了可实例化地图的管理功能，包括：
 * - 创建和管理地下城、团队副本实例
 * - 创建和管理战场、竞技场实例
 * - 实例生命周期管理（创建、更新、销毁）
 * - 网格引用计数管理（共享地形数据）
 *
 * 核心功能：
 * - CreateInstanceForPlayer: 为玩家创建或查找实例
 * - CreateInstance: 创建地下城/团队副本实例
 * - CreateBattleground: 创建战场实例
 * - DestroyInstance: 销毁实例
 *
 * 性能考虑：
 * - 使用网格引用计数共享地形数据
 * - 按需加载和卸载实例
 * - 支持多线程更新
 */

#include "MapInstanced.h"
#include "Battleground.h"
#include "DBCStores.h"
#include "Group.h"
#include "InstanceSaveMgr.h"
#include "Log.h"
#include "MapManager.h"
#include "MMapFactory.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "VMapFactory.h"
#include "VMapManager2.h"
#include "World.h"

/**
 * @brief MapInstanced 构造函数
 * @param id 地图ID
 * @param expiry 网格过期时间
 *
 * 初始化 MapInstanced 对象，设置基础地图属性。
 *
 * 初始化内容：
 * - 调用基类 Map 构造函数，实例ID设为0，难度设为普通
 * - 初始化网格引用计数数组为0
 *
 * 注意事项：
 * - MapInstanced 本身不是可游玩的地图，而是实例管理器
 * - 网格引用计数用于跟踪每个网格被多少实例使用
 */
MapInstanced::MapInstanced(uint32 id, time_t expiry) : Map(id, expiry, 0, DUNGEON_DIFFICULTY_NORMAL)
{
    // 用零填充网格引用计数数组
    memset(&GridMapReference, 0, MAX_NUMBER_OF_GRIDS*MAX_NUMBER_OF_GRIDS*sizeof(uint16));
}

/**
 * @brief 初始化所有实例的可见距离
 *
 * 遍历所有已存在的实例地图并初始化它们的可见距离。
 *
 * 调用时机：
 * - 地图创建后
 * - 配置变更后重新初始化
 */
void MapInstanced::InitVisibilityDistance()
{
    if (m_InstancedMaps.empty())
        return;
    // 为所有实例副本初始化可见距离
    for (InstancedMaps::iterator i = m_InstancedMaps.begin(); i != m_InstancedMaps.end(); ++i)
    {
        (*i).second->InitVisibilityDistance();
    }
}

/**
 * @brief 更新所有实例地图
 * @param t 自上次更新以来经过的时间（毫秒）
 *
 * 更新 MapInstanced 及其管理的所有实例地图。
 *
 * 处理流程：
 * 1. 更新基础地图（Map::Update）- 处理网格清理
 * 2. 遍历所有实例地图：
 *    a. 如果实例可以卸载，尝试销毁它
 *    b. 否则更新实例（单线程或多线程）
 *
 * 多线程支持：
 * - 如果 MapUpdater 激活，使用线程池并行更新实例
 * - 否则使用单线程顺序更新
 *
 * 性能考虑：
 * - 实例更新是主要的性能开销
 * - 多线程更新可显著提升多核CPU利用率
 * - 空闲实例会被自动卸载以节省资源
 */
void MapInstanced::Update(uint32 t)
{
    // 处理已加载的GridMaps（当未使用时卸载它！）
    Map::Update(t);

    // 更新实例地图
    InstancedMaps::iterator i = m_InstancedMaps.begin();

    while (i != m_InstancedMaps.end())
    {
        // 检查实例是否可以卸载
        if (i->second->CanUnload(t))
        {
            // 尝试销毁实例
            if (!DestroyInstance(i))                             // 迭代器会递增
            {
                //m_unloadTimer
                // 销毁失败（实例中仍有玩家），跳过
            }
        }
        else
        {
            // 仅在这里更新，因为删除前可能调度一些不好的事情
            if (sMapMgr->GetMapUpdater()->activated())
            {
                // 多线程更新：将实例更新任务加入线程池
                sMapMgr->GetMapUpdater()->schedule_update(*i->second, t);
            }
            else
            {
                // 单线程更新：直接更新
                i->second->Update(t);
            }
            ++i;
        }
    }
}

/**
 * @brief 延迟更新所有实例地图
 * @param diff 自上次更新以来经过的时间（毫秒）
 *
 * 执行所有实例地图的延迟更新操作。
 *
 * 处理流程：
 * 1. 遍历所有实例地图，调用它们的 DelayedUpdate
 * 2. 调用基类的 DelayedUpdate
 *
 * 调用时机：
 * - 在主更新之后调用
 * - 处理需要在主更新循环之外执行的操作
 */
void MapInstanced::DelayedUpdate(uint32 diff)
{
    // 更新所有实例地图
    for (InstancedMaps::iterator i = m_InstancedMaps.begin(); i != m_InstancedMaps.end(); ++i)
        i->second->DelayedUpdate(diff);

    // 调用基类的延迟更新（这可能会被移除）
    Map::DelayedUpdate(diff);
}

/*
void MapInstanced::RelocationNotify()
{
    for (InstancedMaps::iterator i = m_InstancedMaps.begin(); i != m_InstancedMaps.end(); ++i)
        i->second->RelocationNotify();
}
*/

/**
 * @brief 卸载所有实例地图和网格
 *
 * 清理所有实例地图及其资源。
 *
 * 处理流程：
 * 1. 遍历所有实例地图：
 *    a. 调用 UnloadAll 卸载网格
 *    b. 触发脚本回调 OnDestroyMap
 * 2. 清空实例映射
 * 3. 卸载基础地图的网格（用于卸载GridMaps的占位网格）
 *
 * 调用时机：
 * - 服务器关闭时
 * - MapInstanced 对象销毁时
 */
void MapInstanced::UnloadAll()
{
    // 卸载实例地图
    for (InstancedMaps::iterator i = m_InstancedMaps.begin(); i != m_InstancedMaps.end(); ++i)
    {
        i->second->UnloadAll();

        sScriptMgr->OnDestroyMap(i->second.get());
    }

    m_InstancedMaps.clear();

    // 卸载自己的网格（只是虚拟（占位）网格，必须卸载GridMaps！）
    Map::UnloadAll();
}

/**
 * @brief 为玩家创建或查找实例地图
 * @param mapId 地图ID
 * @param player 请求进入的玩家
 * @param loginInstanceId 登录时的实例ID（可选，默认为0）
 * @return 指向实例地图的指针，失败返回 nullptr
 *
 * 这是玩家进入实例的主要入口点。
 * 根据地图类型（战场或地下城）和玩家状态，创建或查找合适的实例。
 *
 * 处理流程：
 *
 * 对于战场/竞技场：
 * 1. 获取玩家的战场ID
 * 2. 查找现有战场地图
 * 3. 如果不存在，创建新的战场地图
 * 4. 如果战场对象不存在，传送玩家到入口点
 *
 * 对于地下城/团队副本：
 * 优先级顺序：
 * 1. 玩家的永久绑定
 * 2. 登录时玩家的当前实例ID
 * 3. 队伍的当前绑定
 * 4. 玩家的当前绑定
 *
 * 详细流程：
 * 1. 检查玩家的绑定实例
 * 2. 如果没有永久绑定：
 *    a. 如果提供了 loginInstanceId，使用该实例或返回 nullptr
 *    b. 检查队伍绑定，使用队伍实例
 * 3. 如果有存档（pSave），查找或创建对应实例
 * 4. 否则生成新实例ID并创建新实例
 *
 * 调用时机：
 * - 玩家传送进入可实例化地图时
 * - 玩家登录时恢复到上次所在的实例
 *
 * 注意事项：
 * - 玩家实际并未添加到实例中（仅在 InstanceMap::Add 中添加）
 * - 队伍绑定时会清除单人的临时绑定
 * - 实例ID通过 InstanceSaveMgr 或 MapManager 管理
 *
 * 性能考虑：
 * - 可能需要查询数据库获取实例存档
 * - 创建新实例需要初始化大量数据
 */
/*
- 返回对象的正确实例，基于其实例ID
- 如果尚未创建，则创建实例
- 玩家实际上并未添加到实例中（仅在 InstanceMap::Add 中添加）
*/
Map* MapInstanced::CreateInstanceForPlayer(uint32 mapId, Player* player, uint32 loginInstanceId /*= 0*/)
{
    if (GetId() != mapId || !player)
        return nullptr;

    Map* map = nullptr;
    uint32 newInstanceId = 0;                       // 结果地图的实例ID

    // 处理战场/竞技场
    if (IsBattlegroundOrArena())
    {
        // 为玩家实例化或查找现有的战场地图
        // 实例ID设置在战场ID中
        newInstanceId = player->GetBattlegroundId();
        if (!newInstanceId)
            return nullptr;

        map = sMapMgr->FindMap(mapId, newInstanceId);
        if (!map)
        {
            if (Battleground* bg = player->GetBattleground())
                map = CreateBattleground(newInstanceId, bg);
            else
            {
                // 战场对象不存在，传送玩家到入口点
                player->TeleportToBGEntryPoint();
                return nullptr;
            }
        }
    }
    else
    {
        // 处理地下城/团队副本
        // 获取玩家对该地图的绑定
        InstancePlayerBind* pBind = player->GetBoundInstance(GetId(), player->GetDifficulty(IsRaid()));
        InstanceSave* pSave = pBind ? pBind->save : nullptr;

        // 优先级：
        // 1. 玩家的永久绑定
        // 2. 登录时玩家的当前实例ID
        // 3. 队伍的当前绑定
        // 4. 玩家的当前绑定
        if (!pBind || !pBind->perm)
        {
            // 如果玩家在登录时有保存的实例ID，我们要么使用这个实例，要么将他传送出去（返回 null）
            if (loginInstanceId)
            {
                map = FindInstanceMap(loginInstanceId);
                if (!map && pSave && pSave->GetInstanceId() == loginInstanceId)
                    map = CreateInstance(loginInstanceId, pSave, pSave->GetDifficulty(), player->GetTeamId());
                return map;
            }

            InstanceGroupBind* groupBind = nullptr;
            Group* group = player->GetGroup();
            // 使用玩家的难度设置（可能与队伍不同）
            if (group)
            {
                groupBind = group->GetBoundInstance(this);
                if (groupBind)
                {
                    // 进入队伍实例时应重置单人存档
                    player->UnbindInstance(GetId(), player->GetDifficulty(IsRaid()));
                    pSave = groupBind->save;
                }
            }
        }

        // 使用找到的存档创建或查找实例
        if (pSave)
        {
            // 单人/永久/队伍存档
            newInstanceId = pSave->GetInstanceId();
            map = FindInstanceMap(newInstanceId);
            // 存档可能存在但地图不存在
            if (!map)
                map = CreateInstance(newInstanceId, pSave, pSave->GetDifficulty(), player->GetTeamId());
        }
        else
        {
            // 如果通过队伍成员或实例存档未找到实例ID
            // 将首次创建实例
            newInstanceId = sMapMgr->GenerateInstanceId();

            Difficulty diff = player->GetGroup() ? player->GetGroup()->GetDifficulty(IsRaid()) : player->GetDifficulty(IsRaid());
            // 现在这似乎是可能的，但我不知道是否应该允许
            //ASSERT(!FindInstanceMap(NewInstanceId));
            map = FindInstanceMap(newInstanceId);
            if (!map)
                map = CreateInstance(newInstanceId, nullptr, diff, player->GetTeamId());
        }
    }

    return map;
}

/**
 * @brief 创建新的地下城/团队副本实例
 * @param InstanceId 新实例的ID
 * @param save 实例存档对象（可为 nullptr）
 * @param difficulty 实例难度
 * @param InstanceTeam 实例所属队伍（联盟或部落）
 * @return 指向新创建的 InstanceMap 的指针
 *
 * 创建一个新的 InstanceMap 对象，用于地下城或团队副本。
 *
 * 处理流程：
 * 1. 锁定地图映射（线程安全）
 * 2. 验证地图条目和模板是否存在
 * 3. 调整难度（某些实例只有一种难度）
 * 4. 创建 InstanceMap 对象
 * 5. 加载重生时间和尸体数据
 * 6. 创建实例脚本数据
 * 7. 可选：加载所有网格单元
 * 8. 添加到实例映射中
 * 9. 触发脚本回调
 *
 * 线程安全：
 * - 使用 _mapLock 互斥锁保护实例创建过程
 *
 * 调用时机：
 * - CreateInstanceForPlayer 确定需要创建新实例时
 * - 加载已有实例存档时
 *
 * 性能考虑：
 * - 创建实例可能耗时较长
 * - 如果启用了 CONFIG_INSTANCEMAP_LOAD_GRIDS，会预加载所有网格
 */
InstanceMap* MapInstanced::CreateInstance(uint32 InstanceId, InstanceSave* save, Difficulty difficulty, TeamId InstanceTeam)
{
    // 加载/创建地图
    std::lock_guard<std::mutex> lock(_mapLock);

    // 确保我们有有效的地图ID
    MapEntry const* entry = sMapStore.LookupEntry(GetId());
    if (!entry)
    {
        TC_LOG_ERROR("maps", "CreateInstance: no entry for map {}", GetId());
        ABORT();
    }
    InstanceTemplate const* iTemplate = sObjectMgr->GetInstanceTemplate(GetId());
    if (!iTemplate)
    {
        TC_LOG_ERROR("maps", "CreateInstance: no instance template for map {}", GetId());
        ABORT();
    }

    // 某些实例只有一种难度，需要降级
    GetDownscaledMapDifficultyData(GetId(), difficulty);

    TC_LOG_DEBUG("maps", "MapInstanced::CreateInstance: {} map instance {} for {} created with difficulty {}", save ? "" : "new ", InstanceId, GetId(), static_cast<uint32>(difficulty));

    // 创建 InstanceMap 对象
    InstanceMap* map = new InstanceMap(GetId(), GetGridExpiry(), InstanceId, difficulty, this, InstanceTeam);
    ASSERT(map->IsDungeon());

    // 加载重生时间和尸体数据
    map->LoadRespawnTimes();
    map->LoadCorpseData();

    // 创建实例脚本数据（如果有存档则加载数据）
    bool load_data = save != nullptr;
    map->CreateInstanceData(load_data);

    // 可选：预加载所有网格单元
    if (sWorld->getBoolConfig(CONFIG_INSTANCEMAP_LOAD_GRIDS))
        map->LoadAllCells();

    // 添加到实例映射中
    Trinity::unique_trackable_ptr<Map>& ptr = m_InstancedMaps[InstanceId];
    ptr.reset(map);
    map->SetWeakPtr(ptr);

    // 触发脚本回调
    sScriptMgr->OnCreateMap(map);
    return map;
}

/**
 * @brief 创建新的战场地图
 * @param InstanceId 新实例的ID
 * @param bg 战场对象指针
 * @return 指向新创建的 BattlegroundMap 的指针
 *
 * 创建一个新的 BattlegroundMap 对象，用于战场或竞技场。
 *
 * 处理流程：
 * 1. 锁定地图映射（线程安全）
 * 2. 根据战场等级获取PvP难度条目
 * 3. 确定生成模式（难度）
 * 4. 创建 BattlegroundMap 对象
 * 5. 关联战场对象和地图对象
 * 6. 添加到实例映射中
 * 7. 触发脚本回调
 *
 * 线程安全：
 * - 使用 _mapLock 互斥锁保护实例创建过程
 *
 * 调用时机：
 * - 战场队列系统创建新战场时
 * - 玩家加入已有战场时（如果地图不存在）
 *
 * 性能考虑：
 * - 战场地图相对轻量级
 * - 不需要预加载网格
 */
BattlegroundMap* MapInstanced::CreateBattleground(uint32 InstanceId, Battleground* bg)
{
    // 加载/创建地图
    std::lock_guard<std::mutex> lock(_mapLock);

    TC_LOG_DEBUG("maps", "MapInstanced::CreateBattleground: map bg {} for {} created.", InstanceId, GetId());

    // 根据战场等级获取PvP难度条目
    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), bg->GetMinLevel());

    uint8 spawnMode;

    if (bracketEntry)
        spawnMode = bracketEntry->Difficulty;
    else
        spawnMode = REGULAR_DIFFICULTY;

    // 创建 BattlegroundMap 对象
    BattlegroundMap* map = new BattlegroundMap(GetId(), GetGridExpiry(), InstanceId, this, spawnMode);
    ASSERT(map->IsBattlegroundOrArena());

    // 关联战场对象和地图对象
    map->SetBG(bg);
    bg->SetBgMap(map);

    // 添加到实例映射中
    Trinity::unique_trackable_ptr<Map>& ptr = m_InstancedMaps[InstanceId];
    ptr.reset(map);
    map->SetWeakPtr(ptr);

    // 触发脚本回调
    sScriptMgr->OnCreateMap(map);
    return map;
}

/**
 * @brief 销毁实例地图
 * @param itr 指向要销毁的实例的迭代器（会被递增）
 * @return 成功销毁返回 true，失败返回 false
 *
 * 卸载并销毁一个实例地图及其所有资源。
 *
 * 处理流程：
 * 1. 移除所有玩家
 * 2. 如果仍有玩家，返回 false（无法销毁）
 * 3. 卸载所有网格和资源
 * 4. 如果是最后一个实例且启用了网格卸载：
 *    a. 卸载 VMap 和 MMap 数据
 *    b. 卸载基础地图的网格
 * 5. 触发脚本回调
 * 6. 如果是战场/竞技场，释放实例ID
 * 7. 从实例映射中删除
 *
 * 注意事项：
 * - 迭代器会在删除后自动递增
 * - 只有在实例中没有玩家时才能销毁
 * - 战场/竞技场的实例ID可重用
 * - 其他实例的ID由 InstanceSaveMgr 管理
 *
 * 调用时机：
 * - 实例地图可以卸载时（CanUnload返回true）
 * - 手动销毁实例时
 *
 * 性能考虑：
 * - 卸载所有网格可能耗时较长
 * - 清理VMap和MMap数据减少内存占用
 */
// 删除后递增迭代器
bool MapInstanced::DestroyInstance(InstancedMaps::iterator &itr)
{
    // 移除所有玩家
    itr->second->RemoveAllPlayers();
    if (itr->second->HavePlayers())
    {
        ++itr;
        return false;  // 仍有玩家，无法销毁
    }

    // 卸载所有网格和资源
    itr->second->UnloadAll();

    // 仅当这是最后一个实例且启用了网格卸载时才卸载VMaps
    if (m_InstancedMaps.size() <= 1 && sWorld->getBoolConfig(CONFIG_GRID_UNLOAD))
    {
        VMAP::VMapFactory::createOrGetVMapManager()->unloadMap(itr->second->GetId());
        MMAP::MMapFactory::createOrGetMMapManager()->unloadMap(itr->second->GetId());
        // 在这种情况下，也卸载基础地图的网格
        // 这样在下一次创建地图时，（实际上是EnsureGridCreated）VMaps将被重新加载
        Map::UnloadAll();
    }

    // 触发脚本回调
    sScriptMgr->OnDestroyMap(itr->second.get());

    // 释放实例ID，允许战场和竞技场重用（其他实例由InstanceSaveMgr处理）
    if (itr->second->IsBattlegroundOrArena())
        sMapMgr->FreeInstanceId(itr->second->GetInstanceId());

    // 删除地图
    m_InstancedMaps.erase(itr++);

    return true;
}

/**
 * @brief 检查玩家是否可以进入该地图
 * @param player 尝试进入的玩家
 * @return 进入状态（总是返回 CAN_ENTER）
 *
 * 对于 MapInstanced，通常总是允许进入，
 * 因为具体的进入检查在各个实例地图中进行。
 *
 * 调用时机：
 * - 玩家尝试传送进入地图时
 */
Map::EnterState MapInstanced::CannotEnter(Player* /*player*/)
{
    //ABORT();
    return CAN_ENTER;
}
