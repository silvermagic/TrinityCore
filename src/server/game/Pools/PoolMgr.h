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
 * @file PoolMgr.h
 * @brief 对象池管理系统头文件
 *
 * 本模块实现了游戏世界中的对象池（Pool）系统，用于动态管理和刷新游戏对象。
 * 主要功能包括：
 * 1. 管理生物、游戏对象和子池的池组
 * 2. 控制池中对象的刷新和生成规则
 * 3. 支持显式几率和等几率两种对象选择方式
 * 4. 支持池的嵌套（池中池）
 *
 * 对象池系统在游戏中的典型应用场景：
 * - 矿点、草药等资源点的随机刷新
 * - 稀有生物的随机刷新控制
 * - 宝箱等可交互对象的动态生成
 */

#ifndef TRINITY_POOLHANDLER_H
#define TRINITY_POOLHANDLER_H

#include "Define.h"
#include "Creature.h"
#include "GameObject.h"
#include "SpawnData.h"

/**
 * @struct PoolTemplateData
 * @brief 池模板数据结构
 *
 * 存储池的基本配置信息，从数据库 pool_template 表加载
 */
struct PoolTemplateData
{
    uint32 MaxLimit;  ///< 池中最大同时生成对象数量
};

/**
 * @struct PoolObject
 * @brief 池对象数据结构
 *
 * 表示池中的单个对象，包含对象GUID和生成几率
 */
struct PoolObject
{
    ObjectGuid::LowType guid;   ///< 对象GUID（生物/游戏对象的数据库GUID，或子池ID）
    float chance;               ///< 生成几率（0-100，0表示等几率）

    /**
     * @brief 构造函数
     * @param _guid 对象GUID
     * @param _chance 生成几率，会自动取绝对值
     */
    PoolObject(ObjectGuid::LowType _guid, float _chance) : guid(_guid), chance(std::fabs(_chance)) { }
};

/**
 * @class Pool
 * @brief 池类型的占位类
 *
 * 用于模板特化，标识池中池（Pool of Pool）的情况
 * 允许一个池包含其他池作为成员
 */
class Pool
{
};

/// 已激活池对象的GUID集合类型
typedef std::set<ObjectGuid::LowType> ActivePoolObjects;
/// 已激活池的计数映射类型（键：池ID，值：该池中已激活对象数量）
typedef std::map<uint32, uint32> ActivePoolPools;

/**
 * @class ActivePoolData
 * @brief 活跃池数据管理类
 *
 * 跟踪和管理所有已生成/激活的池对象状态
 * 维护生物、游戏对象和子池的激活状态，以及每个池的激活对象计数
 */
class TC_GAME_API ActivePoolData
{
    public:
        /**
         * @brief 检查指定对象是否处于激活状态
         * @tparam T 对象类型（Creature/GameObject/Pool）
         * @param db_guid_or_pool_id 对象GUID或池ID
         * @return true 如果对象已激活
         */
        template<typename T>
        bool IsActiveObject(uint32 db_guid_or_pool_id) const;

        /**
         * @brief 获取指定池中已激活对象的数量
         * @param pool_id 池ID
         * @return 已激活对象数量
         */
        uint32 GetActiveObjectCount(uint32 pool_id) const;

        /**
         * @brief 激活指定对象
         * @tparam T 对象类型（Creature/GameObject/Pool）
         * @param db_guid_or_pool_id 对象GUID或池ID
         * @param pool_id 所属池ID
         */
        template<typename T>
        void ActivateObject(uint32 db_guid_or_pool_id, uint32 pool_id);

        /**
         * @brief 移除指定对象的激活状态
         * @tparam T 对象类型（Creature/GameObject/Pool）
         * @param db_guid_or_pool_id 对象GUID或池ID
         * @param pool_id 所属池ID
         */
        template<typename T>
        void RemoveObject(uint32 db_guid_or_pool_id, uint32 pool_id);

    private:
        ActivePoolObjects mSpawnedCreatures;    ///< 已生成的生物GUID集合
        ActivePoolObjects mSpawnedGameobjects;  ///< 已生成的游戏对象GUID集合
        ActivePoolPools   mSpawnedPools;        ///< 已激活的池及其对象计数
};

/**
 * @class PoolGroup
 * @brief 池组管理模板类
 *
 * 管理单个池中特定类型对象（生物、游戏对象或子池）的集合
 * 负责对象的生成、刷新、移除以及几率控制
 *
 * @tparam T 池中对象类型（Creature/GameObject/Pool）
 */
template <class T>
class TC_GAME_API PoolGroup
{
    typedef std::vector<PoolObject> PoolObjectList;  ///< 池对象列表类型

    public:
        /**
         * @brief 默认构造函数
         */
        explicit PoolGroup() : poolId(0) { }

        /**
         * @brief 设置池ID
         * @param pool_id 池ID
         */
        void SetPoolId(uint32 pool_id) { poolId = pool_id; }

        /**
         * @brief 析构函数
         */
        ~PoolGroup() { };

        /**
         * @brief 检查池组是否为空
         * @return true 如果池组没有任何对象
         */
        bool isEmpty() const { return ExplicitlyChanced.empty() && EqualChanced.empty(); }

        /**
         * @brief 添加对象到池组
         * @param poolitem 池对象
         * @param maxentries 池的最大对象数
         *
         * 根据几率值将对象添加到不同的列表：
         * - 有显式几率且池限制为1：添加到 ExplicitlyChanced
         * - 否则：添加到 EqualChanced
         */
        void AddEntry(PoolObject& poolitem, uint32 maxentries);

        /**
         * @brief 检查池的几率配置是否有效
         * @return true 如果几率总和正确（100%或0%）
         *
         * 验证规则：
         * - 如果没有等几率对象，显式几率对象的总和必须为100%或0%
         */
        bool CheckPool() const;

        /**
         * @brief 生成池中的对象
         * @param spawns 活跃池数据引用
         * @param limit 最大生成数量
         * @param triggerFrom 触发生成的对象GUID（用于刷新场景，0表示新生成）
         *
         * 根据几率规则选择并生成对象，处理显式几率和等几率两种情况
         */
        void SpawnObject(ActivePoolData& spawns, uint32 limit, uint32 triggerFrom);

        /**
         * @brief 移除池中的对象
         * @param spawns 活跃池数据引用
         * @param guid 要移除的对象GUID（0表示移除所有对象）
         * @param alwaysDeleteRespawnTime 是否总是删除重生时间
         */
        void DespawnObject(ActivePoolData& spawns, ObjectGuid::LowType guid=0, bool alwaysDeleteRespawnTime = false);

        /**
         * @brief 移除单个对象
         * @param guid 对象GUID
         * @param alwaysDeleteRespawnTime 是否总是删除重生时间
         * @param saveRespawnTime 是否保存重生时间
         */
        void Despawn1Object(ObjectGuid::LowType guid, bool alwaysDeleteRespawnTime = false, bool saveRespawnTime = true);

        /**
         * @brief 从数据库中移除对象的重生时间
         * @param guid 对象GUID
         */
        void RemoveRespawnTimeFromDB(ObjectGuid::LowType guid);

        /**
         * @brief 生成单个对象
         * @param obj 池对象指针
         */
        void Spawn1Object(PoolObject* obj);

        /**
         * @brief 重新生成单个对象
         * @param obj 池对象指针
         *
         * 先移除再生成，用于刷新场景
         */
        void ReSpawn1Object(PoolObject* obj);

        /**
         * @brief 移除一个池关系（仅用于Pool类型）
         * @param child_pool_id 子池ID
         *
         * 用于打破循环引用
         */
        void RemoveOneRelation(uint32 child_pool_id);

        /**
         * @brief 获取第一个等几率对象的ID
         * @return 第一个等几率对象的GUID，如果没有则返回0
         */
        uint32 GetFirstEqualChancedObjectId()
        {
            if (EqualChanced.empty())
                return 0;
            return EqualChanced.front().guid;
        }

        /**
         * @brief 获取池ID
         * @return 池ID
         */
        uint32 GetPoolId() const { return poolId; }

    private:
        uint32 poolId;                     ///< 池ID
        PoolObjectList ExplicitlyChanced;   ///< 显式几率对象列表（chance > 0）
        PoolObjectList EqualChanced;        ///< 等几率对象列表（chance = 0）
};

/**
 * @class PoolMgr
 * @brief 池管理器单例类
 *
 * 全局池系统的核心管理器，负责：
 * 1. 从数据库加载池配置数据
 * 2. 管理所有池组（生物池、游戏对象池、子池）
 * 3. 提供池的生成、移除和更新接口
 * 4. 维护对象到池的映射关系
 * 5. 跟踪活跃池对象状态
 *
 * 使用单例模式，通过 sPoolMgr 宏访问全局实例
 */
class TC_GAME_API PoolMgr
{
    private:
        /**
         * @brief 私有构造函数（单例模式）
         */
        PoolMgr();

        /**
         * @brief 私有析构函数
         */
        ~PoolMgr() { };

    public:
        /**
         * @brief 获取单例实例
         * @return PoolMgr单例指针
         */
        static PoolMgr* instance();

        /**
         * @brief 从数据库加载池数据
         *
         * 加载流程：
         * 1. 加载池模板数据（pool_template表）
         * 2. 加载生物池成员（pool_members type=0）
         * 3. 加载游戏对象池成员（pool_members type=1）
         * 4. 加载子池关系（pool_members type=2）
         * 5. 检测并修复循环引用
         * 6. 初始化并生成非事件池
         *
         * @note 此方法在服务器启动时调用，是耗时的初始化操作
         */
        void LoadFromDB();

        /**
         * @brief 初始化池管理器
         *
         * 清空搜索映射，准备重新加载数据
         */
        void Initialize();

        /**
         * @brief 检查对象是否属于某个池
         * @tparam T 对象类型（Creature/GameObject/Pool）
         * @param db_guid_or_pool_id 对象GUID或池ID
         * @return 所属池ID，如果不属于任何池则返回0
         */
        template<typename T>
        uint32 IsPartOfAPool(uint32 db_guid_or_pool_id) const;

        /**
         * @brief 根据生成对象类型检查是否属于池（非模板版本）
         * @param type 生成对象类型
         * @param spawnId 生成ID
         * @return 所属池ID，如果不属于任何池则返回0
         */
        uint32 IsPartOfAPool(SpawnObjectType type, ObjectGuid::LowType spawnId) const;

        /**
         * @brief 检查对象是否已生成
         * @tparam T 对象类型（Creature/GameObject/Pool）
         * @param db_guid_or_pool_id 对象GUID或池ID
         * @return true 如果对象当前已生成
         */
        template<typename T>
        bool IsSpawnedObject(uint32 db_guid_or_pool_id) const { return mSpawnedData.IsActiveObject<T>(db_guid_or_pool_id); }

        /**
         * @brief 检查池配置是否有效
         * @param pool_id 池ID
         * @return true 如果池的几率配置正确
         */
        bool CheckPool(uint32 pool_id) const;

        /**
         * @brief 生成池中的所有对象
         * @param pool_id 池ID
         *
         * 依次生成子池、游戏对象和生物
         */
        void SpawnPool(uint32 pool_id);

        /**
         * @brief 移除池中的所有对象
         * @param pool_id 池ID
         * @param alwaysDeleteRespawnTime 是否总是删除重生时间
         *
         * 依次移除生物、游戏对象和子池
         */
        void DespawnPool(uint32 pool_id, bool alwaysDeleteRespawnTime = false);

        /**
         * @brief 更新池状态（对象重生时调用）
         * @tparam T 对象类型（Creature/GameObject/Pool）
         * @param pool_id 池ID
         * @param db_guid_or_pool_id 触发更新的对象GUID或池ID
         *
         * 如果池属于母池，则在母池中更新；否则在当前池中更新
         */
        template<typename T>
        void UpdatePool(uint32 pool_id, uint32 db_guid_or_pool_id);

        /**
         * @brief 根据生成对象类型更新池（非模板版本）
         * @param pool_id 池ID
         * @param type 生成对象类型
         * @param spawnId 生成ID
         */
        void UpdatePool(uint32 pool_id, SpawnObjectType type, ObjectGuid::LowType spawnId);

    private:
        /**
         * @brief 生成池中指定类型的对象（内部方法）
         * @tparam T 对象类型（Creature/GameObject/Pool）
         * @param pool_id 池ID
         * @param db_guid_or_pool_id 触发生成的对象GUID（0表示新生成）
         */
        template<typename T>
        void SpawnPool(uint32 pool_id, uint32 db_guid_or_pool_id);

        /// 池模板数据映射类型
        typedef std::unordered_map<uint32, PoolTemplateData>      PoolTemplateDataMap;
        /// 生物池组映射类型
        typedef std::unordered_map<uint32, PoolGroup<Creature>>   PoolGroupCreatureMap;
        /// 游戏对象池组映射类型
        typedef std::unordered_map<uint32, PoolGroup<GameObject>> PoolGroupGameObjectMap;
        /// 子池组映射类型
        typedef std::unordered_map<uint32, PoolGroup<Pool>>       PoolGroupPoolMap;
        /// 搜索键值对类型
        typedef std::pair<uint32, uint32>           SearchPair;
        /// 搜索映射类型
        typedef std::map<uint32, uint32>            SearchMap;

        PoolTemplateDataMap    mPoolTemplate;          ///< 池模板数据映射
        PoolGroupCreatureMap   mPoolCreatureGroups;    ///< 生物池组映射
        PoolGroupGameObjectMap mPoolGameobjectGroups;  ///< 游戏对象池组映射
        PoolGroupPoolMap       mPoolPoolGroups;        ///< 子池组映射
        SearchMap mCreatureSearchMap;                  ///< 生物GUID到池ID的映射
        SearchMap mGameobjectSearchMap;                ///< 游戏对象GUID到池ID的映射
        SearchMap mPoolSearchMap;                      ///< 子池ID到母池ID的映射

        // 动态数据
        ActivePoolData mSpawnedData;                   ///< 已激活池对象数据
};

/// 全局池管理器访问宏
#define sPoolMgr PoolMgr::instance()

/**
 * @brief 检查生物是否属于某个池（模板特化）
 * @param db_guid 生物的数据库GUID
 * @return 所属池ID，如果不属于任何池则返回0
 *
 * 通过查询 mCreatureSearchMap 快速定位生物所属的池
 */
template<>
inline uint32 PoolMgr::IsPartOfAPool<Creature>(uint32 db_guid) const
{
    SearchMap::const_iterator itr = mCreatureSearchMap.find(db_guid);
    if (itr != mCreatureSearchMap.end())
        return itr->second;

    return 0;
}

/**
 * @brief 检查游戏对象是否属于某个池（模板特化）
 * @param db_guid 游戏对象的数据库GUID
 * @return 所属池ID，如果不属于任何池则返回0
 *
 * 通过查询 mGameobjectSearchMap 快速定位游戏对象所属的池
 */
template<>
inline uint32 PoolMgr::IsPartOfAPool<GameObject>(uint32 db_guid) const
{
    SearchMap::const_iterator itr = mGameobjectSearchMap.find(db_guid);
    if (itr != mGameobjectSearchMap.end())
        return itr->second;

    return 0;
}

/**
 * @brief 检查池是否属于另一个池（模板特化）
 * @param pool_id 子池ID
 * @return 母池ID，如果不属于任何池则返回0
 *
 * 通过查询 mPoolSearchMap 快速定位子池所属的母池
 * 用于支持池的嵌套结构
 */
template<>
inline uint32 PoolMgr::IsPartOfAPool<Pool>(uint32 pool_id) const
{
    SearchMap::const_iterator itr = mPoolSearchMap.find(pool_id);
    if (itr != mPoolSearchMap.end())
        return itr->second;

    return 0;
}

#endif
