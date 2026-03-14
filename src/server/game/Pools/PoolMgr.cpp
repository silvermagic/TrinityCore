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
 * @file PoolMgr.cpp
 * @brief 对象池管理系统实现文件
 *
 * 本文件实现了池管理系统的核心功能，包括：
 * 1. ActivePoolData - 活跃池对象的跟踪和管理
 * 2. PoolGroup - 池组的生成、移除和几率控制
 * 3. PoolMgr - 全局池管理器的初始化和操作
 *
 * 性能注意事项：
 * - LoadFromDB() 是耗时的初始化操作，仅在服务器启动时调用
 * - SpawnObject() 和 DespawnObject() 可能涉及地图操作，需注意性能
 * - 使用哈希映射进行快速查找，时间复杂度 O(1)
 */

#include "PoolMgr.h"
#include "Containers.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "MapManager.h"
#include "ObjectMgr.h"

////////////////////////////////////////////////////////////
// ActivePoolData 类方法实现

/**
 * @brief 获取指定池中已激活对象的数量
 * @param pool_id 池ID
 * @return 已激活对象的数量
 *
 * 通过查询 mSpawnedPools 映射获取计数
 * 如果池不存在于映射中，返回 0
 */
uint32 ActivePoolData::GetActiveObjectCount(uint32 pool_id) const
{
    ActivePoolPools::const_iterator itr = mSpawnedPools.find(pool_id);
    return itr != mSpawnedPools.end() ? itr->second : 0;
}

/**
 * @brief 检查生物是否当前已生成（模板特化）
 * @param db_guid 生物的数据库GUID
 * @return true 如果生物已生成
 *
 * 通过查询 mSpawnedCreatures 集合判断
 */
template<>
TC_GAME_API bool ActivePoolData::IsActiveObject<Creature>(uint32 db_guid) const
{
    return mSpawnedCreatures.find(db_guid) != mSpawnedCreatures.end();
}

/**
 * @brief 检查游戏对象是否当前已生成（模板特化）
 * @param db_guid 游戏对象的数据库GUID
 * @return true 如果游戏对象已生成
 *
 * 通过查询 mSpawnedGameobjects 集合判断
 */
template<>
TC_GAME_API bool ActivePoolData::IsActiveObject<GameObject>(uint32 db_guid) const
{
    return mSpawnedGameobjects.find(db_guid) != mSpawnedGameobjects.end();
}

/**
 * @brief 检查子池是否当前已激活（模板特化）
 * @param sub_pool_id 子池ID
 * @return true 如果子池已激活
 *
 * 通过查询 mSpawnedPools 映射判断
 */
template<>
TC_GAME_API bool ActivePoolData::IsActiveObject<Pool>(uint32 sub_pool_id) const
{
    return mSpawnedPools.find(sub_pool_id) != mSpawnedPools.end();
}

/**
 * @brief 激活生物对象（模板特化）
 * @param db_guid 生物GUID
 * @param pool_id 所属池ID
 *
 * 将生物GUID添加到已生成集合，并增加池的激活计数
 */
template<>
void ActivePoolData::ActivateObject<Creature>(uint32 db_guid, uint32 pool_id)
{
    mSpawnedCreatures.insert(db_guid);
    ++mSpawnedPools[pool_id];
}

/**
 * @brief 激活游戏对象（模板特化）
 * @param db_guid 游戏对象GUID
 * @param pool_id 所属池ID
 *
 * 将游戏对象GUID添加到已生成集合，并增加池的激活计数
 */
template<>
void ActivePoolData::ActivateObject<GameObject>(uint32 db_guid, uint32 pool_id)
{
    mSpawnedGameobjects.insert(db_guid);
    ++mSpawnedPools[pool_id];
}

/**
 * @brief 激活子池（模板特化）
 * @param sub_pool_id 子池ID
 * @param pool_id 母池ID
 *
 * 初始化子池的激活计数为0，并增加母池的激活计数
 */
template<>
void ActivePoolData::ActivateObject<Pool>(uint32 sub_pool_id, uint32 pool_id)
{
    mSpawnedPools[sub_pool_id] = 0;
    ++mSpawnedPools[pool_id];
}

/**
 * @brief 移除生物对象的激活状态（模板特化）
 * @param db_guid 生物GUID
 * @param pool_id 所属池ID
 *
 * 从已生成集合中移除生物，并减少池的激活计数
 */
template<>
void ActivePoolData::RemoveObject<Creature>(uint32 db_guid, uint32 pool_id)
{
    mSpawnedCreatures.erase(db_guid);
    uint32& val = mSpawnedPools[pool_id];
    if (val > 0)
        --val;
}

/**
 * @brief 移除游戏对象的激活状态（模板特化）
 * @param db_guid 游戏对象GUID
 * @param pool_id 所属池ID
 *
 * 从已生成集合中移除游戏对象，并减少池的激活计数
 */
template<>
void ActivePoolData::RemoveObject<GameObject>(uint32 db_guid, uint32 pool_id)
{
    mSpawnedGameobjects.erase(db_guid);
    uint32& val = mSpawnedPools[pool_id];
    if (val > 0)
        --val;
}

/**
 * @brief 移除子池的激活状态（模板特化）
 * @param sub_pool_id 子池ID
 * @param pool_id 母池ID
 *
 * 从已激活池映射中移除子池，并减少母池的激活计数
 */
template<>
void ActivePoolData::RemoveObject<Pool>(uint32 sub_pool_id, uint32 pool_id)
{
    mSpawnedPools.erase(sub_pool_id);
    uint32& val = mSpawnedPools[pool_id];
    if (val > 0)
        --val;
}

////////////////////////////////////////////////////////////
// PoolGroup 类方法实现

/**
 * @brief 添加对象到池组
 * @param poolitem 池对象
 * @param maxentries 池的最大对象数
 *
 * 根据对象几率和池配置将对象分配到不同的列表：
 * - 如果对象有显式几率（> 0）且池限制为1，则添加到 ExplicitlyChanced 列表
 * - 否则添加到 EqualChanced 列表（等几率池）
 *
 * 设计原理：
 * - 显式几率池：每个对象有明确的生成几率（如稀有怪80%，普通怪20%）
 * - 等几率池：所有对象被选中的概率相同，从列表中随机选择
 */
template <class T>
void PoolGroup<T>::AddEntry(PoolObject& poolitem, uint32 maxentries)
{
    if (poolitem.chance != 0 && maxentries == 1)
        ExplicitlyChanced.push_back(poolitem);
    else
        EqualChanced.push_back(poolitem);
}

/**
 * @brief 检查池的几率配置是否有效
 * @return true 如果几率配置正确
 *
 * 验证规则：
 * - 如果没有等几率对象，则所有显式几率对象的几率总和必须为100%或0%
 * - 如果有等几率对象，则不需要检查显式几率（可以混合使用）
 *
 * 性能：O(n)，n为显式几率对象数量
 */
template <class T>
bool PoolGroup<T>::CheckPool() const
{
    if (EqualChanced.empty())
    {
        float chance = 0;
        for (uint32 i = 0; i < ExplicitlyChanced.size(); ++i)
            chance += ExplicitlyChanced[i].chance;
        // 几率总和必须为100%或0%（空池）
        if (chance != 100 && chance != 0)
            return false;
    }
    return true;
}

/**
 * @brief 移除池中的对象
 * @param spawns 活跃池数据引用
 * @param guid 要移除的对象GUID（0表示移除所有对象）
 * @param alwaysDeleteRespawnTime 是否总是删除重生时间
 *
 * 核心移除逻辑：
 * 1. 遍历等几率对象列表和显式几率对象列表
 * 2. 如果对象已激活：
 *    - 如果guid为0或匹配当前对象，则移除对象并更新激活状态
 * 3. 如果对象未激活且alwaysDeleteRespawnTime为true，删除其重生时间
 *
 * 调用时机：
 * - 服务器关闭或池被销毁时（guid=0）
 * - 游戏事件结束移除池对象时
 * - 手动移除特定对象时
 *
 * 性能：O(n)，n为池中对象总数
 */
template<class T>
void PoolGroup<T>::DespawnObject(ActivePoolData& spawns, ObjectGuid::LowType guid, bool alwaysDeleteRespawnTime)
{
    // 处理等几率对象列表
    for (size_t i=0; i < EqualChanced.size(); ++i)
    {
        // 检查对象是否已生成
        if (spawns.IsActiveObject<T>(EqualChanced[i].guid))
        {
            // 如果guid为0（移除所有）或匹配当前对象GUID
            if (!guid || EqualChanced[i].guid == guid)
            {
                Despawn1Object(EqualChanced[i].guid, alwaysDeleteRespawnTime);
                spawns.RemoveObject<T>(EqualChanced[i].guid, poolId);
            }
        }
        else if (alwaysDeleteRespawnTime)
            RemoveRespawnTimeFromDB(EqualChanced[i].guid);
    }

    // 处理显式几率对象列表
    for (size_t i = 0; i < ExplicitlyChanced.size(); ++i)
    {
        // 检查对象是否已生成
        if (spawns.IsActiveObject<T>(ExplicitlyChanced[i].guid))
        {
            // 如果guid为0（移除所有）或匹配当前对象GUID
            if (!guid || ExplicitlyChanced[i].guid == guid)
            {
                Despawn1Object(ExplicitlyChanced[i].guid, alwaysDeleteRespawnTime);
                spawns.RemoveObject<T>(ExplicitlyChanced[i].guid, poolId);
            }
        }
        else if (alwaysDeleteRespawnTime)
            RemoveRespawnTimeFromDB(ExplicitlyChanced[i].guid);
    }
}

/**
 * @brief 移除单个生物对象（模板特化）
 * @param guid 生物GUID
 * @param alwaysDeleteRespawnTime 是否总是删除重生时间
 * @param saveRespawnTime 是否保存重生时间
 *
 * 执行步骤：
 * 1. 获取生物数据
 * 2. 从地图格子中移除生物
 * 3. 对于非副本地图：
 *    - 获取地图上该GUID的所有生物实例
 *    - 如果需要保存重生时间且非兼容模式，则保存
 *    - 将生物添加到移除列表
 * 4. 如果需要，删除重生时间记录
 *
 * 性能注意事项：
 * - 涉及地图查询和对象移除操作
 * - 不应该在频繁调用的循环中执行
 */
template<>
void PoolGroup<Creature>::Despawn1Object(ObjectGuid::LowType guid, bool alwaysDeleteRespawnTime, bool saveRespawnTime)
{
    if (CreatureData const* data = sObjectMgr->GetCreatureData(guid))
    {
        // 从地图格子中移除生物数据
        sObjectMgr->RemoveCreatureFromGrid(guid, data);

        Map* map = sMapMgr->CreateBaseMap(data->mapId);
        if (!map->Instanceable())
        {
            // 获取地图上该GUID对应的所有生物实例
            auto creatureBounds = map->GetCreatureBySpawnIdStore().equal_range(guid);
            for (auto itr = creatureBounds.first; itr != creatureBounds.second;)
            {
                Creature* creature = itr->second;
                ++itr;
                // 对于动态生成的对象，在此处保存重生时间
                if (saveRespawnTime && !creature->GetRespawnCompatibilityMode())
                    creature->SaveRespawnTime();
                creature->AddObjectToRemoveList();
            }

            // 如果需要总是删除重生时间，则从数据库移除记录
            if (alwaysDeleteRespawnTime)
                map->RemoveRespawnTime(SpawnObjectType::SPAWN_TYPE_CREATURE, guid, nullptr, true);
        }
    }
}

/**
 * @brief 移除单个游戏对象（模板特化）
 * @param guid 游戏对象GUID
 * @param alwaysDeleteRespawnTime 是否总是删除重生时间
 * @param saveRespawnTime 是否保存重生时间
 *
 * 执行步骤与 Despawn1Object<Creature> 类似，但处理游戏对象
 */
template<>
void PoolGroup<GameObject>::Despawn1Object(ObjectGuid::LowType guid, bool alwaysDeleteRespawnTime, bool saveRespawnTime)
{
    if (GameObjectData const* data = sObjectMgr->GetGameObjectData(guid))
    {
        // 从地图格子中移除游戏对象数据
        sObjectMgr->RemoveGameobjectFromGrid(guid, data);

        Map* map = sMapMgr->CreateBaseMap(data->mapId);
        if (!map->Instanceable())
        {
            // 获取地图上该GUID对应的所有游戏对象实例
            auto gameobjectBounds = map->GetGameObjectBySpawnIdStore().equal_range(guid);
            for (auto itr = gameobjectBounds.first; itr != gameobjectBounds.second;)
            {
                GameObject* go = itr->second;
                ++itr;

                // 对于动态生成的对象，在此处保存重生时间
                if (saveRespawnTime && !go->GetRespawnCompatibilityMode())
                    go->SaveRespawnTime();
                go->AddObjectToRemoveList();
            }

            // 如果需要总是删除重生时间，则从数据库移除记录
            if (alwaysDeleteRespawnTime)
                map->RemoveRespawnTime(SpawnObjectType::SPAWN_TYPE_GAMEOBJECT, guid, nullptr, true);
        }
    }
}

/**
 * @brief 移除单个子池（模板特化）
 * @param child_pool_id 子池ID
 * @param alwaysDeleteRespawnTime 是否总是删除重生时间
 * @param saveRespawnTime 未使用参数
 *
 * 通过调用池管理器的 DespawnPool 方法移除子池
 */
template<>
void PoolGroup<Pool>::Despawn1Object(uint32 child_pool_id, bool alwaysDeleteRespawnTime, bool /*saveRespawnTime*/)
{
    sPoolMgr->DespawnPool(child_pool_id, alwaysDeleteRespawnTime);
}

/**
 * @brief 移除一个池关系（仅用于Pool类型）
 * @param child_pool_id 要移除的子池ID
 *
 * 用于打破循环引用。当检测到池的嵌套形成循环时，
 * 从母池中移除子池的关系。
 *
 * 遍历显式几率和等几率列表，删除匹配的子池记录
 * 性能：O(n)
 */
template<>
void PoolGroup<Pool>::RemoveOneRelation(uint32 child_pool_id)
{
    // 从显式几率列表中移除
    for (PoolObjectList::iterator itr = ExplicitlyChanced.begin(); itr != ExplicitlyChanced.end(); ++itr)
    {
        if (itr->guid == child_pool_id)
        {
            ExplicitlyChanced.erase(itr);
            break;
        }
    }
    // 从等几率列表中移除
    for (PoolObjectList::iterator itr = EqualChanced.begin(); itr != EqualChanced.end(); ++itr)
    {
        if (itr->guid == child_pool_id)
        {
            EqualChanced.erase(itr);
            break;
        }
    }
}

/**
 * @brief 生成池中的对象
 * @param spawns 活跃池数据引用
 * @param limit 最大生成数量
 * @param triggerFrom 触发生成的对象GUID（0表示新生成，非0表示刷新）
 *
 * 核心生成逻辑：
 * 1. 计算需要生成的对象数量（limit - 当前激活数）
 * 2. 如果是刷新场景（triggerFrom != 0），需要额外生成一个对象
 * 3. 执行随机抽取：
 *    a. 如果有显式几率对象，按几率随机选择一个
 *    b. 如果没有选中或没有显式几率对象，从等几率对象中随机选择
 * 4. 生成选中的对象
 * 5. 如果是刷新场景且对象未被选中，则移除该对象
 *
 * 几率机制说明：
 * - 显式几率：使用累积概率法，随机一个0-100的数，依次减去对象几率
 *   当结果小于0时，选中该对象
 * - 等几率：从所有未激活对象中随机选择指定数量
 *
 * 性能注意事项：
 * - 涉及随机数生成和列表操作
 * - 对于大池可能需要优化随机选择算法
 */
template <class T>
void PoolGroup<T>::SpawnObject(ActivePoolData& spawns, uint32 limit, uint32 triggerFrom)
{
    // 计算需要生成的对象数量
    int count = limit - spawns.GetActiveObjectCount(poolId);

    // 如果是从某个对象的刷新触发生成，该对象仍被标记为已生成
    // 也被计入 m_SpawnedPoolAmount，所以需要增加1的生成计数
    if (triggerFrom)
        ++count;

    if (count > 0)
    {
        PoolObjectList rolledObjects;
        rolledObjects.reserve(count);

        // 随机选择要生成的对象
        // 优先处理显式几率对象
        if (!ExplicitlyChanced.empty())
        {
            float roll = (float)rand_chance();

            for (PoolObject& obj : ExplicitlyChanced)
            {
                roll -= obj.chance;
                // 触发对象在此时仍被标记为已生成，但也可能被选中（刷新场景）
                // 所以需要显式检查这种情况
                if (roll < 0 && (obj.guid == triggerFrom || !spawns.IsActiveObject<T>(obj.guid)))
                {
                    rolledObjects.push_back(obj);
                    break;
                }
            }
        }

        // 如果没有显式几率对象或未被选中，从等几率对象中选择
        if (!EqualChanced.empty() && rolledObjects.empty())
        {
            // 复制所有未激活或触发对象到候选列表
            std::copy_if(EqualChanced.begin(), EqualChanced.end(), std::back_inserter(rolledObjects), [triggerFrom, &spawns](PoolObject const& object)
            {
                return object.guid == triggerFrom || !spawns.IsActiveObject<T>(object.guid);
            });

            // 随机选择指定数量的对象
            Trinity::Containers::RandomResize(rolledObjects, count);
        }

        // 尝试生成选中的对象
        for (PoolObject& obj : rolledObjects)
        {
            if (obj.guid == triggerFrom)
            {
                // 刷新场景：重新生成对象
                ReSpawn1Object(&obj);
                triggerFrom = 0;
            }
            else
            {
                // 新生成场景：激活并生成对象
                spawns.ActivateObject<T>(obj.guid, poolId);
                Spawn1Object(&obj);
            }
        }
    }

    // 如果触发对象未被选中（一出一进，不增加计数），则移除它
    if (triggerFrom)
        DespawnObject(spawns, triggerFrom);
}

/**
 * @brief 生成单个生物对象（模板特化）
 * @param obj 池对象指针
 *
 * 执行步骤：
 * 1. 获取生物数据
 * 2. 将生物添加到地图格子
 * 3. 如果地图格子已加载且非副本：
 *    - 创建生物实例
 *    - 从数据库加载生物数据
 *    - 如果加载失败，删除生物对象
 *
 * 性能注意事项：
 * - 仅在格子已加载时才真正创建生物实例
 * - 生物加载涉及数据库查询，不应频繁调用
 */
template <>
void PoolGroup<Creature>::Spawn1Object(PoolObject* obj)
{
    if (CreatureData const* data = sObjectMgr->GetCreatureData(obj->guid))
    {
        // 将生物添加到地图格子
        sObjectMgr->AddCreatureToGrid(obj->guid, data);

        // 只在格子已加载时生成（避免不必要的对象创建）
        Map* map = sMapMgr->CreateBaseMap(data->mapId);
        // 使用生成坐标来判断格子是否加载
        if (!map->Instanceable() && map->IsGridLoaded(data->spawnPoint))
        {
            Creature* creature = new Creature();
            //TC_LOG_DEBUG("pool", "Spawning creature {}", guid);
            if (!creature->LoadFromDB(obj->guid, map, true, false))
            {
                delete creature;
                return;
            }
        }
    }
}

/**
 * @brief 生成单个游戏对象（模板特化）
 * @param obj 池对象指针
 *
 * 执行步骤与 Spawn1Object<Creature> 类似，但处理游戏对象
 * 额外检查游戏对象是否默认生成
 */
template <>
void PoolGroup<GameObject>::Spawn1Object(PoolObject* obj)
{
    if (GameObjectData const* data = sObjectMgr->GetGameObjectData(obj->guid))
    {
        // 将游戏对象添加到地图格子
        sObjectMgr->AddGameobjectToGrid(obj->guid, data);
        // 只在格子已加载时生成（避免不必要的对象创建）
        // 此处检查基础地图是否为非副本，然后只检查存在性
        Map* map = sMapMgr->CreateBaseMap(data->mapId);
        // 使用当前坐标来判断格子是否加载，生物可能已经改变了格子
        if (!map->Instanceable() && map->IsGridLoaded(data->spawnPoint))
        {
            GameObject* pGameobject = new GameObject;
            //TC_LOG_DEBUG("pool", "Spawning gameobject {}", guid);
            if (!pGameobject->LoadFromDB(obj->guid, map, false))
            {
                delete pGameobject;
                return;
            }
            else
            {
                // 只有默认生成的游戏对象才添加到地图
                if (pGameobject->isSpawnedByDefault())
                    map->AddToMap(pGameobject);
            }
        }
    }
}

/**
 * @brief 生成单个子池（模板特化）
 * @param obj 池对象指针
 *
 * 通过调用池管理器的 SpawnPool 方法生成子池
 */
template <>
void PoolGroup<Pool>::Spawn1Object(PoolObject* obj)
{
    sPoolMgr->SpawnPool(obj->guid);
}

/**
 * @brief 重新生成指定的生物对象（模板特化）
 * @param obj 池对象指针
 *
 * 先移除后生成，用于刷新场景
 * 注意：不保存重生时间，因为这是立即刷新
 */
template <>
void PoolGroup<Creature>::ReSpawn1Object(PoolObject* obj)
{
    Despawn1Object(obj->guid, false, false);
    Spawn1Object(obj);
}

/**
 * @brief 重新生成指定的游戏对象（模板特化）
 * @param obj 池对象指针
 *
 * 先移除后生成，用于刷新场景
 */
template <>
void PoolGroup<GameObject>::ReSpawn1Object(PoolObject* obj)
{
    Despawn1Object(obj->guid, false, false);
    Spawn1Object(obj);
}

/**
 * @brief 重新生成指定的子池（模板特化）
 * @param obj 池对象指针
 *
 * 对于子池不需要执行任何操作
 */
template <>
void PoolGroup<Pool>::ReSpawn1Object(PoolObject* /*obj*/) { }

/**
 * @brief 从数据库中移除生物的重生时间（模板特化）
 * @param guid 生物GUID
 *
 * 仅对非副本地图执行操作
 */
template <>
void PoolGroup<Creature>::RemoveRespawnTimeFromDB(ObjectGuid::LowType guid)
{
    if (CreatureData const* data = sObjectMgr->GetCreatureData(guid))
    {
        Map* map = sMapMgr->CreateBaseMap(data->mapId);
        if (!map->Instanceable())
        {
            map->RemoveRespawnTime(SPAWN_TYPE_CREATURE, guid, nullptr, true);
        }
    }
}

/**
 * @brief 从数据库中移除游戏对象的重生时间（模板特化）
 * @param guid 游戏对象GUID
 *
 * 仅对非副本地图执行操作
 */
template <>
void PoolGroup<GameObject>::RemoveRespawnTimeFromDB(ObjectGuid::LowType guid)
{
    if (GameObjectData const* data = sObjectMgr->GetGameObjectData(guid))
    {
        Map* map = sMapMgr->CreateBaseMap(data->mapId);
        if (!map->Instanceable())
        {
            map->RemoveRespawnTime(SPAWN_TYPE_GAMEOBJECT, guid, nullptr, true);
        }
    }
}

/**
 * @brief 从数据库中移除子池的重生时间（模板特化）
 * @param guid 未使用参数
 *
 * 子池没有重生时间，不需要执行任何操作
 */
template <>
void PoolGroup<Pool>::RemoveRespawnTimeFromDB(ObjectGuid::LowType /*guid*/) { }

////////////////////////////////////////////////////////////
// PoolMgr 类方法实现

/**
 * @brief 私有构造函数
 *
 * 单例模式的构造函数，不执行初始化操作
 * 初始化工作在 Initialize() 和 LoadFromDB() 中完成
 */
PoolMgr::PoolMgr() { }

/**
 * @brief 初始化池管理器
 *
 * 清空所有搜索映射，为重新加载数据做准备
 * 在重新加载池数据前调用
 */
void PoolMgr::Initialize()
{
    mGameobjectSearchMap.clear();
    mCreatureSearchMap.clear();
}

/**
 * @brief 获取单例实例
 * @return PoolMgr单例指针
 *
 * 使用静态局部变量实现线程安全的单例模式
 */
PoolMgr* PoolMgr::instance()
{
    static PoolMgr instance;
    return &instance;
}

/**
 * @brief 从数据库加载池数据
 *
 * 这是服务器启动时的核心初始化方法，负责加载所有池配置数据并初始化池系统。
 *
 * 加载流程：
 * 1. 加载池模板数据（pool_template表）
 *    - 读取池ID和最大对象数限制
 *
 * 2. 加载生物池成员（pool_members type=0）
 *    - 验证生物GUID是否存在
 *    - 验证池ID是否在模板中
 *    - 验证几率值是否有效（0-100）
 *    - 将生物添加到对应池组
 *
 * 3. 加载游戏对象池成员（pool_members type=1）
 *    - 验证游戏对象GUID是否存在
 *    - 验证游戏对象类型（必须是宝箱、采集物或鱼点）
 *    - 验证池ID和几率值
 *
 * 4. 加载子池关系（pool_members type=2）
 *    - 验证母池和子池是否存在
 *    - 检测自引用（池包含自身）
 *    - 检测循环引用并自动修复
 *
 * 5. 初始化并生成非事件池
 *    - 只生成不属于任何游戏事件且不属于其他池的顶级池
 *    - 子池会在母池生成时递归生成
 *
 * 性能注意事项：
 * - 这是耗时的初始化操作，仅在服务器启动时调用一次
 * - 涉及多次数据库查询和大量数据验证
 * - 循环引用检测算法时间复杂度较高
 *
 * @note 此方法必须在 ObjectMgr 加载完所有生物和游戏对象数据后调用
 */
void PoolMgr::LoadFromDB()
{
    // 第一步：加载池模板数据
    {
        uint32 oldMSTime = getMSTime();

        QueryResult result = WorldDatabase.Query("SELECT entry, max_limit FROM pool_template");
        if (!result)
        {
            mPoolTemplate.clear();
            TC_LOG_INFO("server.loading", ">> Loaded 0 object pools. DB table `pool_template` is empty.");
            return;
        }

        uint32 count = 0;
        do
        {
            Field* fields = result->Fetch();

            uint32 pool_id = fields[0].GetUInt32();

            PoolTemplateData& pPoolTemplate = mPoolTemplate[pool_id];
            pPoolTemplate.MaxLimit  = fields[1].GetUInt32();

            ++count;
        }
        while (result->NextRow());

        TC_LOG_INFO("server.loading", ">> Loaded {} objects pools in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
    }

    // 第二步：加载生物池数据

    TC_LOG_INFO("server.loading", "Loading Creatures Pooling Data...");
    {
        uint32 oldMSTime = getMSTime();

        // 查询生物池成员：spawnId, poolSpawnId, chance
        QueryResult result = WorldDatabase.Query("SELECT spawnId, poolSpawnId, chance FROM pool_members WHERE type = 0");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 creatures in pools. DB table `pool_creature` is empty.");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                ObjectGuid::LowType guid    = fields[0].GetUInt32();
                uint32 pool_id = fields[1].GetUInt32();
                float chance   = fields[2].GetFloat();

                // 验证生物数据是否存在
                CreatureData const* data = sObjectMgr->GetCreatureData(guid);
                if (!data)
                {
                    TC_LOG_ERROR("sql.sql", "`pool_creature` has a non existing creature spawn (GUID: {}) defined for pool id ({}), skipped.", guid, pool_id);
                    continue;
                }
                // 验证池ID是否存在
                auto it = mPoolTemplate.find(pool_id);
                if (it == mPoolTemplate.end())
                {
                    TC_LOG_ERROR("sql.sql", "`pool_creature` pool id ({}) is not in `pool_template`, skipped.", pool_id);
                    continue;
                }
                // 验证几率值有效性
                if (chance < 0 || chance > 100)
                {
                    TC_LOG_ERROR("sql.sql", "`pool_creature` has an invalid chance ({}) for creature guid ({}) in pool id ({}), skipped.", chance, guid, pool_id);
                    continue;
                }
                // 将生物添加到池组
                PoolTemplateData* pPoolTemplate = &mPoolTemplate[pool_id];
                PoolObject plObject = PoolObject(guid, chance);
                PoolGroup<Creature>& cregroup = mPoolCreatureGroups[pool_id];
                cregroup.SetPoolId(pool_id);
                cregroup.AddEntry(plObject, pPoolTemplate->MaxLimit);
                // 建立生物GUID到池ID的映射，用于快速查找
                SearchPair p(guid, pool_id);
                mCreatureSearchMap.insert(p);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} creatures in pools in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // 第三步：加载游戏对象池数据

    TC_LOG_INFO("server.loading", "Loading Gameobject Pooling Data...");
    {
        uint32 oldMSTime = getMSTime();

        // 查询游戏对象池成员：spawnId, poolSpawnId, chance
        QueryResult result = WorldDatabase.Query("SELECT spawnId, poolSpawnId, chance FROM pool_members WHERE type = 1");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 gameobjects in pools. DB table `pool_gameobject` is empty.");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                ObjectGuid::LowType guid    = fields[0].GetUInt32();
                uint32 pool_id = fields[1].GetUInt32();
                float chance   = fields[2].GetFloat();

                // 验证游戏对象数据是否存在
                GameObjectData const* data = sObjectMgr->GetGameObjectData(guid);
                if (!data)
                {
                    TC_LOG_ERROR("sql.sql", "`pool_gameobject` has a non existing gameobject spawn (GUID: {}) defined for pool id ({}), skipped.", guid, pool_id);
                    continue;
                }

                // 验证游戏对象类型，只有特定类型可以加入池
                GameObjectTemplate const* goinfo = sObjectMgr->GetGameObjectTemplate(data->id);
                ASSERT(goinfo);
                if (goinfo->type != GAMEOBJECT_TYPE_CHEST &&
                    goinfo->type != GAMEOBJECT_TYPE_GOOBER &&
                    goinfo->type != GAMEOBJECT_TYPE_FISHINGHOLE)
                {
                    TC_LOG_ERROR("sql.sql", "`pool_gameobject` has a not lootable gameobject spawn (GUID: {}, type: {}) defined for pool id ({}), skipped.", guid, goinfo->type, pool_id);
                    continue;
                }

                // 验证池ID是否存在
                auto it = mPoolTemplate.find(pool_id);
                if (it == mPoolTemplate.end())
                {
                    TC_LOG_ERROR("sql.sql", "`pool_gameobject` pool id ({}) is not in `pool_template`, skipped.", pool_id);
                    continue;
                }

                // 验证几率值有效性
                if (chance < 0 || chance > 100)
                {
                    TC_LOG_ERROR("sql.sql", "`pool_gameobject` has an invalid chance ({}) for gameobject guid ({}) in pool id ({}), skipped.", chance, guid, pool_id);
                    continue;
                }

                // 将游戏对象添加到池组
                PoolTemplateData* pPoolTemplate = &mPoolTemplate[pool_id];
                PoolObject plObject = PoolObject(guid, chance);
                PoolGroup<GameObject>& gogroup = mPoolGameobjectGroups[pool_id];
                gogroup.SetPoolId(pool_id);
                gogroup.AddEntry(plObject, pPoolTemplate->MaxLimit);
                // 建立游戏对象GUID到池ID的映射，用于快速查找
                SearchPair p(guid, pool_id);
                mGameobjectSearchMap.insert(p);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} gameobject in pools in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // 第四步：加载池中池（母池）数据

    TC_LOG_INFO("server.loading", "Loading Mother Pooling Data...");
    {
        uint32 oldMSTime = getMSTime();

        // 查询池中池关系：spawnId, poolSpawnId, chance
        QueryResult result = WorldDatabase.Query("SELECT spawnId, poolSpawnId, chance FROM pool_members WHERE type = 2");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 pools in pools");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();

                uint32 child_pool_id  = fields[0].GetUInt32();
                uint32 mother_pool_id = fields[1].GetUInt32();
                float chance          = fields[2].GetFloat();

                // 验证母池ID是否存在
                {
                    auto it = mPoolTemplate.find(mother_pool_id);
                    if (it == mPoolTemplate.end())
                    {
                        TC_LOG_ERROR("sql.sql", "`pool_pool` mother_pool id ({}) is not in `pool_template`, skipped.", mother_pool_id);
                        continue;
                    }
                }
                // 验证子池ID是否存在
                {
                    auto it = mPoolTemplate.find(child_pool_id);
                    if (it == mPoolTemplate.end())
                    {
                        TC_LOG_ERROR("sql.sql", "`pool_pool` included pool_id ({}) is not in `pool_template`, skipped.", child_pool_id);
                        continue;
                    }
                }
                // 检测自引用（池包含自身）
                if (mother_pool_id == child_pool_id)
                {
                    TC_LOG_ERROR("sql.sql", "`pool_pool` pool_id ({}) includes itself, dead-lock detected, skipped.", child_pool_id);
                    continue;
                }
                // 验证几率值有效性
                if (chance < 0 || chance > 100)
                {
                    TC_LOG_ERROR("sql.sql", "`pool_pool` has an invalid chance ({}) for pool id ({}) in mother pool id ({}), skipped.", chance, child_pool_id, mother_pool_id);
                    continue;
                }
                // 将子池添加到母池组
                PoolTemplateData* pPoolTemplateMother = &mPoolTemplate[mother_pool_id];
                PoolObject plObject = PoolObject(child_pool_id, chance);
                PoolGroup<Pool>& plgroup = mPoolPoolGroups[mother_pool_id];
                plgroup.SetPoolId(mother_pool_id);
                plgroup.AddEntry(plObject, pPoolTemplateMother->MaxLimit);
                // 建立子池ID到母池ID的映射
                SearchPair p(child_pool_id, mother_pool_id);
                mPoolSearchMap.insert(p);

                ++count;
            }
            while (result->NextRow());

            // 循环引用检测算法
            // 遍历所有池模板，检测是否存在循环引用链
            // 所有 pool_id 都已存在于 pool_template 中
            for (auto const& it : mPoolTemplate)
            {
                std::set<uint32> checkedPools;
                // 沿着池链向下追踪，直到找不到父池或检测到循环
                for (SearchMap::iterator poolItr = mPoolSearchMap.find(it.first); poolItr != mPoolSearchMap.end(); poolItr = mPoolSearchMap.find(poolItr->second))
                {
                    checkedPools.insert(poolItr->first);
                    // 如果在已检查池集合中发现了父池ID，说明存在循环引用
                    if (checkedPools.find(poolItr->second) != checkedPools.end())
                    {
                        // 构建错误信息
                        std::ostringstream ss;
                        ss << "The pool(s) ";
                        for (std::set<uint32>::const_iterator itr = checkedPools.begin(); itr != checkedPools.end(); ++itr)
                            ss << *itr << ' ';
                        ss << "create(s) a circular reference, which can cause the server to freeze.\nRemoving the last link between mother pool "
                            << poolItr->first << " and child pool " << poolItr->second;
                        TC_LOG_ERROR("sql.sql", "{}", ss.str());
                        // 移除循环引用的最后一个链接
                        mPoolPoolGroups[poolItr->second].RemoveOneRelation(poolItr->first);
                        mPoolSearchMap.erase(poolItr);
                        --count;
                        break;
                    }
                }
            }

            TC_LOG_INFO("server.loading", ">> Loaded {} pools in mother pools in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // 第五步：初始化并生成非事件池
    // 初始化方法将生成所有不在事件中且不在其他池中的池
    // 这就是为什么有两个左连接和两个NULL检查

    TC_LOG_INFO("server.loading", "Starting objects pooling system...");
    {
        uint32 oldMSTime = getMSTime();

        // 查询顶级池（不属于任何游戏事件且不属于其他池的池）
        // 使用左连接确保池不在 game_event_pool 和 pool_members（type=2）中
        QueryResult result = WorldDatabase.Query("SELECT DISTINCT pool_template.entry, pool_members.spawnId, pool_members.poolSpawnId FROM pool_template"
            " LEFT JOIN game_event_pool ON pool_template.entry = game_event_pool.pool_entry"
            " LEFT JOIN pool_members ON pool_members.type = 2 AND pool_template.entry = pool_members.spawnId WHERE game_event_pool.pool_entry IS NULL");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Pool handling system initialized, 0 pools spawned.");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32 pool_entry = fields[0].GetUInt32();
                uint32 pool_pool_id = fields[1].GetUInt32();

                // 检查池配置是否有效
                if (!CheckPool(pool_entry))
                {
                    if (pool_pool_id)
                        // 该池是 pool_pool 表中的子池。理想情况下应该从池处理器中移除它以确保永远不会生成，
                        // 但这可能会递归地使整个母池链失效。将来可以这样做，但现在我们什么也不做。
                        TC_LOG_ERROR("sql.sql", "Pool Id {} has no equal chance pooled entites defined and explicit chance sum is not 100. This broken pool is a child pool of Id {} and cannot be safely removed.", pool_entry, fields[2].GetUInt32());
                    else
                        TC_LOG_ERROR("sql.sql", "Pool Id {} has no equal chance pooled entites defined and explicit chance sum is not 100. The pool will not be spawned.", pool_entry);
                    continue;
                }

                // 不生成子池，它们由父池递归生成
                if (!pool_pool_id)
                {
                    SpawnPool(pool_entry);
                    count++;
                }
            }
            while (result->NextRow());

            TC_LOG_DEBUG("pool", "Pool handling system initialized, {} pools spawned in {} ms", count, GetMSTimeDiffToNow(oldMSTime));

        }
    }
}

/**
 * @brief 生成生物池中的对象（模板特化）
 * @param pool_id 池ID
 * @param db_guid 触发生成的生物GUID（0表示新生成）
 *
 * 调用池组的 SpawnObject 方法生成生物
 */
template<>
void PoolMgr::SpawnPool<Creature>(uint32 pool_id, uint32 db_guid)
{
    auto it = mPoolCreatureGroups.find(pool_id);
    if (it != mPoolCreatureGroups.end() && !it->second.isEmpty())
        it->second.SpawnObject(mSpawnedData, mPoolTemplate[pool_id].MaxLimit, db_guid);
}

/**
 * @brief 生成游戏对象池中的对象（模板特化）
 * @param pool_id 池ID
 * @param db_guid 触发生成的游戏对象GUID（0表示新生成）
 *
 * 调用池组的 SpawnObject 方法生成游戏对象
 */
template<>
void PoolMgr::SpawnPool<GameObject>(uint32 pool_id, uint32 db_guid)
{
    auto it = mPoolGameobjectGroups.find(pool_id);
    if (it != mPoolGameobjectGroups.end() && !it->second.isEmpty())
        it->second.SpawnObject(mSpawnedData, mPoolTemplate[pool_id].MaxLimit, db_guid);
}

/**
 * @brief 生成子池（模板特化）
 * @param pool_id 母池ID
 * @param sub_pool_id 触发生成的子池ID（0表示新生成）
 *
 * 调用池组的 SpawnObject 方法生成子池
 */
template<>
void PoolMgr::SpawnPool<Pool>(uint32 pool_id, uint32 sub_pool_id)
{
    auto it = mPoolPoolGroups.find(pool_id);
    if (it != mPoolPoolGroups.end() && !it->second.isEmpty())
        it->second.SpawnObject(mSpawnedData, mPoolTemplate[pool_id].MaxLimit, sub_pool_id);
}

/**
 * @brief 生成池中的所有对象
 * @param pool_id 池ID
 *
 * 按顺序生成：子池 -> 游戏对象 -> 生物
 * 此顺序确保依赖关系正确处理
 */
void PoolMgr::SpawnPool(uint32 pool_id)
{
    SpawnPool<Pool>(pool_id, 0);
    SpawnPool<GameObject>(pool_id, 0);
    SpawnPool<Creature>(pool_id, 0);
}

/**
 * @brief 移除池中的所有对象
 * @param pool_id 池ID
 * @param alwaysDeleteRespawnTime 是否总是删除重生时间
 *
 * 按顺序移除：生物 -> 游戏对象 -> 子池
 * 此顺序与生成顺序相反，确保正确清理
 */
void PoolMgr::DespawnPool(uint32 pool_id, bool alwaysDeleteRespawnTime)
{
    // 移除生物
    {
        auto it = mPoolCreatureGroups.find(pool_id);
        if (it != mPoolCreatureGroups.end() && !it->second.isEmpty())
            it->second.DespawnObject(mSpawnedData, 0, alwaysDeleteRespawnTime);
    }
    // 移除游戏对象
    {
        auto it = mPoolGameobjectGroups.find(pool_id);
        if (it != mPoolGameobjectGroups.end() && !it->second.isEmpty())
            it->second.DespawnObject(mSpawnedData, 0, alwaysDeleteRespawnTime);
    }
    // 移除子池
    {
        auto it = mPoolPoolGroups.find(pool_id);
        if (it != mPoolPoolGroups.end() && !it->second.isEmpty())
            it->second.DespawnObject(mSpawnedData, 0, alwaysDeleteRespawnTime);
    }
}

/**
 * @brief 根据生成对象类型检查是否属于池（非模板版本）
 * @param type 生成对象类型
 * @param spawnId 生成ID
 * @return 所属池ID，如果不属于任何池则返回0
 *
 * 根据类型调用对应的模板特化版本
 * 如果传入无效类型，会终止程序
 */
uint32 PoolMgr::IsPartOfAPool(SpawnObjectType type, ObjectGuid::LowType spawnId) const
{
    switch (type)
    {
        case SPAWN_TYPE_CREATURE:
            return IsPartOfAPool<Creature>(spawnId);
        case SPAWN_TYPE_GAMEOBJECT:
            return IsPartOfAPool<GameObject>(spawnId);
        default:
            ABORT_MSG("Invalid spawn type %u passed to PoolMgr::IsPartOfPool (with spawnId %u)", uint32(type), spawnId);
            return 0;
    }
}

/**
 * @brief 检查池的几率配置是否有效
 * @param pool_id 池ID
 * @return true 如果所有成员池组的几率配置都正确
 *
 * 分别检查游戏对象池组、生物池组和子池组的几率配置
 * 只要有一个池组配置无效，整个池就无效
 */
bool PoolMgr::CheckPool(uint32 pool_id) const
{
    // 检查游戏对象池组
    {
        auto it = mPoolGameobjectGroups.find(pool_id);
        if (it != mPoolGameobjectGroups.end() && !it->second.CheckPool())
            return false;
    }
    // 检查生物池组
    {
        auto it = mPoolCreatureGroups.find(pool_id);
        if (it != mPoolCreatureGroups.end() && !it->second.CheckPool())
            return false;
    }
    // 检查子池组
    {
        auto it = mPoolPoolGroups.find(pool_id);
        if (it != mPoolPoolGroups.end() && !it->second.CheckPool())
            return false;
    }
    return true;
}

/**
 * @brief 更新池状态（对象重生时调用）
 * @tparam T 对象类型（Creature/GameObject/Pool）
 * @param pool_id 池ID
 * @param db_guid_or_pool_id 触发更新的对象GUID或池ID
 *
 * 当池中的某个对象准备好重生时调用此方法。
 * 如果该池本身是另一个池的子池，则在母池中更新；
 * 否则在当前池中更新。
 *
 * 调用时机：
 * - 池中的生物或游戏对象重生时
 * - 子池需要重新生成时
 *
 * 设计原理：
 * - 支持池的嵌套结构
 * - 确保子池的更新在母池上下文中进行
 */
template<typename T>
void PoolMgr::UpdatePool(uint32 pool_id, uint32 db_guid_or_pool_id)
{
    // 如果该池是另一个池的子池，则在母池中更新
    if (uint32 motherpoolid = IsPartOfAPool<Pool>(pool_id))
        SpawnPool<Pool>(motherpoolid, pool_id);
    else
        // 否则在当前池中更新
        SpawnPool<T>(pool_id, db_guid_or_pool_id);
}

// 显式模板实例化
template void PoolMgr::UpdatePool<Pool>(uint32 pool_id, uint32 db_guid_or_pool_id);
template void PoolMgr::UpdatePool<GameObject>(uint32 pool_id, uint32 db_guid_or_pool_id);
template void PoolMgr::UpdatePool<Creature>(uint32 pool_id, uint32 db_guid_or_pool_id);

/**
 * @brief 根据生成对象类型更新池（非模板版本）
 * @param pool_id 池ID
 * @param type 生成对象类型
 * @param spawnId 生成ID
 *
 * 根据类型调用对应的模板版本
 * 如果传入无效类型，会终止程序
 */
void PoolMgr::UpdatePool(uint32 pool_id, SpawnObjectType type, uint32 spawnId)
{
    switch (type)
    {
        case SPAWN_TYPE_CREATURE:
            UpdatePool<Creature>(pool_id, spawnId);
            break;
        case SPAWN_TYPE_GAMEOBJECT:
            UpdatePool<GameObject>(pool_id, spawnId);
            break;
        default:
            ABORT_MSG("Invalid spawn type %u passed to PoolMgr::IsPartOfPool (with spawnId %u)", uint32(type), spawnId);
    }
}
