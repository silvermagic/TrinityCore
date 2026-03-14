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
 * @file ObjectAccessor.cpp
 * @brief 对象访问器实现文件 - 实现对象查找和管理的核心逻辑
 *
 * 主要功能：
 * 1. HashMapHolder 模板实现：全局对象索引的底层实现
 * 2. PlayerNameMapHolder：玩家名称索引实现
 * 3. ObjectAccessor 接口实现：提供统一的查找接口
 *
 * 关键设计：
 * - 使用静态局部变量实现单例模式（线程安全）
 * - 使用读写锁优化并发性能
 * - Player 对象维护双重索引（GUID 和名称）
 */

#include "ObjectAccessor.h"
#include "Corpse.h"
#include "Creature.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "Item.h"
#include "Map.h"
#include "ObjectDefines.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "Transport.h"
#include "World.h"

/**
 * @brief 将对象插入全局哈希表
 * @tparam T 对象类型（Player 或 Transport）
 * @param o 要插入的对象指针
 *
 * 实现细节：
 * - 编译时静态断言确保只有 Player 和 Transport 可以使用全局索引
 * - 使用独占锁（unique_lock）保护写操作
 * - 将 GUID 作为键，对象指针作为值存入哈希表
 */
template<class T>
void HashMapHolder<T>::Insert(T* o)
{
    // 编译时检查：只有 Player 和 Transport 可以注册到全局 HashMapHolder
    static_assert(std::is_same<Player, T>::value
        || std::is_same<Transport, T>::value,
        "Only Player and Transport can be registered in global HashMapHolder");

    // 获取独占锁，确保线程安全
    std::unique_lock<std::shared_mutex> lock(*GetLock());

    // 将对象插入哈希表
    GetContainer()[o->GetGUID()] = o;
}

/**
 * @brief 从全局哈希表中移除对象
 * @tparam T 对象类型（Player 或 Transport）
 * @param o 要移除的对象指针
 *
 * 实现细节：
 * - 使用独占锁保护写操作
 * - 根据 GUID 从哈希表中删除条目
 */
template<class T>
void HashMapHolder<T>::Remove(T* o)
{
    // 获取独占锁，确保线程安全
    std::unique_lock<std::shared_mutex> lock(*GetLock());

    // 从哈希表中删除对象
    GetContainer().erase(o->GetGUID());
}

/**
 * @brief 根据 GUID 查找对象
 * @tparam T 对象类型（Player 或 Transport）
 * @param guid 要查找的 GUID
 * @return 找到的对象指针，未找到返回 nullptr
 *
 * 实现细节：
 * - 使用共享锁（shared_lock）允许多线程并发读取
 * - 使用哈希表的 find 方法，时间复杂度 O(1)
 */
template<class T>
T* HashMapHolder<T>::Find(ObjectGuid guid)
{
    // 获取共享锁，允许多个线程同时读取
    std::shared_lock<std::shared_mutex> lock(*GetLock());

    // 在哈希表中查找
    typename MapType::iterator itr = GetContainer().find(guid);
    return (itr != GetContainer().end()) ? itr->second : nullptr;
}

/**
 * @brief 获取哈希表容器
 * @tparam T 对象类型
 * @return 哈希表的引用
 *
 * 实现细节：
 * - 使用静态局部变量实现单例模式
 * - 每个类型 T 有自己独立的哈希表实例
 */
template<class T>
auto HashMapHolder<T>::GetContainer() -> MapType&
{
    // 静态局部变量，每个类型 T 有独立实例
    static MapType _objectMap;
    return _objectMap;
}

/**
 * @brief 获取读写锁
 * @tparam T 对象类型
 * @return 读写锁指针
 *
 * 实现细节：
 * - 使用静态局部变量实现单例模式
 * - 每个类型 T 有自己独立的锁实例
 */
template<class T>
std::shared_mutex* HashMapHolder<T>::GetLock()
{
    // 静态局部变量，每个类型 T 有独立实例
    static std::shared_mutex _lock;
    return &_lock;
}

/**
 * @brief 获取所有玩家的哈希表
 * @return 玩家哈希表的常量引用
 *
 * 警告：调用者必须先获取锁
 */
HashMapHolder<Player>::MapType const& ObjectAccessor::GetPlayers()
{
    return HashMapHolder<Player>::GetContainer();
}

// 显式实例化模板类，确保链接时可用
template class TC_GAME_API HashMapHolder<Player>;
template class TC_GAME_API HashMapHolder<Transport>;

/**
 * @namespace PlayerNameMapHolder
 * @brief 玩家名称索引持有者
 *
 * 职责：
 * - 维护玩家名称到玩家对象的映射
 * - 支持 O(1) 时间复杂度的名称查找
 * - 与 HashMapHolder<Player> 配合使用，提供双重索引
 *
 * 注意：此命名空间的操作不是线程安全的，依赖外部同步
 */
namespace PlayerNameMapHolder
{
    /// 名称映射类型：玩家名称 -> 玩家对象指针
    typedef std::unordered_map<std::string, Player*> MapType;

    /// 静态哈希表，存储名称索引
    static MapType PlayerNameMap;

    /**
     * @brief 插入玩家到名称索引
     * @param p 玩家指针
     */
    void Insert(Player* p)
    {
        PlayerNameMap[p->GetName()] = p;
    }

    /**
     * @brief 从名称索引移除玩家
     * @param p 玩家指针
     */
    void Remove(Player* p)
    {
        PlayerNameMap.erase(p->GetName());
    }

    /**
     * @brief 按名称查找玩家
     * @param name 玩家名称
     * @return 找到的玩家指针，未找到返回 nullptr
     *
     * 实现细节：
     * - 先规范化玩家名称（首字母大写，其余小写）
     * - 在名称映射中查找
     */
    Player* Find(std::string_view name)
    {
        std::string charName(name);
        // 规范化玩家名称格式
        if (!normalizePlayerName(charName))
            return nullptr;

        // 在名称映射中查找
        auto itr = PlayerNameMap.find(charName);
        return (itr != PlayerNameMap.end()) ? itr->second : nullptr;
    }
} // namespace PlayerNameMapHolder

/**
 * @brief 根据 GUID 获取世界对象（通用查找入口）
 * @param p 参考对象，用于确定地图范围
 * @param guid 目标对象的 GUID
 * @return 找到的 WorldObject 指针，未找到返回 nullptr
 *
 * 实现细节：
 * 根据 GUID 的高位类型自动路由到对应的查找函数
 */
WorldObject* ObjectAccessor::GetWorldObject(WorldObject const& p, ObjectGuid const& guid)
{
    switch (guid.GetHigh())
    {
        case HighGuid::Player:        return GetPlayer(p, guid);
        case HighGuid::Transport:
        case HighGuid::Mo_Transport:
        case HighGuid::GameObject:    return GetGameObject(p, guid);
        case HighGuid::Vehicle:
        case HighGuid::Unit:          return GetCreature(p, guid);
        case HighGuid::Pet:           return GetPet(p, guid);
        case HighGuid::DynamicObject: return GetDynamicObject(p, guid);
        case HighGuid::Corpse:        return GetCorpse(p, guid);
        default:                     return nullptr;
    }
}

/**
 * @brief 根据类型掩码获取对象
 * @param p 参考对象，用于确定地图范围
 * @param guid 目标对象的 GUID
 * @param typemask 类型掩码（TYPEMASK_* 常量）
 * @return 找到的 Object 指针，未找到或类型不匹配返回 nullptr
 *
 * 使用场景：需要按类型过滤对象时
 * 例如：查找任意类型的单位（TYPEMASK_UNIT）
 */
Object* ObjectAccessor::GetObjectByTypeMask(WorldObject const& p, ObjectGuid const& guid, uint32 typemask)
{
    switch (guid.GetHigh())
    {
        case HighGuid::Item:
            // 物品对象需要玩家上下文
            if (typemask & TYPEMASK_ITEM && p.GetTypeId() == TYPEID_PLAYER)
                return ((Player const&)p).GetItemByGuid(guid);
            break;
        case HighGuid::Player:
            if (typemask & TYPEMASK_PLAYER)
                return GetPlayer(p, guid);
            break;
        case HighGuid::Transport:
        case HighGuid::Mo_Transport:
        case HighGuid::GameObject:
            if (typemask & TYPEMASK_GAMEOBJECT)
                return GetGameObject(p, guid);
            break;
        case HighGuid::Unit:
        case HighGuid::Vehicle:
            if (typemask & TYPEMASK_UNIT)
                return GetCreature(p, guid);
            break;
        case HighGuid::Pet:
            if (typemask & TYPEMASK_UNIT)
                return GetPet(p, guid);
            break;
        case HighGuid::DynamicObject:
            if (typemask & TYPEMASK_DYNAMICOBJECT)
                return GetDynamicObject(p, guid);
            break;
        case HighGuid::Corpse:
            break;
        default:
            break;
    }

    return nullptr;
}

/**
 * @brief 获取尸体对象
 * @param u 参考对象，用于确定地图范围
 * @param guid 尸体的 GUID
 * @return 找到的 Corpse 指针，未找到返回 nullptr
 *
 * 实现细节：委托给地图对象查找
 */
Corpse* ObjectAccessor::GetCorpse(WorldObject const& u, ObjectGuid const& guid)
{
    return u.GetMap()->GetCorpse(guid);
}

/**
 * @brief 获取游戏对象
 * @param u 参考对象，用于确定地图范围
 * @param guid 游戏对象的 GUID
 * @return 找到的 GameObject 指针，未找到返回 nullptr
 */
GameObject* ObjectAccessor::GetGameObject(WorldObject const& u, ObjectGuid const& guid)
{
    return u.GetMap()->GetGameObject(guid);
}

/**
 * @brief 获取运输工具对象
 * @param u 参考对象，用于确定地图范围
 * @param guid 运输工具的 GUID
 * @return 找到的 Transport 指针，未找到返回 nullptr
 */
Transport* ObjectAccessor::GetTransport(WorldObject const& u, ObjectGuid const& guid)
{
    return u.GetMap()->GetTransport(guid);
}

/**
 * @brief 获取动态对象
 * @param u 参考对象，用于确定地图范围
 * @param guid 动态对象的 GUID
 * @return 找到的 DynamicObject 指针，未找到返回 nullptr
 */
DynamicObject* ObjectAccessor::GetDynamicObject(WorldObject const& u, ObjectGuid const& guid)
{
    return u.GetMap()->GetDynamicObject(guid);
}

/**
 * @brief 获取单位对象（玩家/宠物/生物）
 * @param u 参考对象，用于确定地图范围
 * @param guid 单位的 GUID
 * @return 找到的 Unit 指针，未找到返回 nullptr
 *
 * 实现细节：
 * 根据 GUID 类型自动路由：
 * - Player GUID -> GetPlayer
 * - Pet GUID -> GetPet
 * - 其他 -> GetCreature
 */
Unit* ObjectAccessor::GetUnit(WorldObject const& u, ObjectGuid const& guid)
{
    // 空 GUID 检查
    if (guid.IsEmpty())
        return nullptr;

    // 根据 GUID 类型路由
    if (guid.IsPlayer())
        return GetPlayer(u, guid);

    if (guid.IsPet())
        return GetPet(u, guid);

    // 默认查找生物
    return GetCreature(u, guid);
}

/**
 * @brief 获取生物对象
 * @param u 参考对象，用于确定地图范围
 * @param guid 生物的 GUID
 * @return 找到的 Creature 指针，未找到返回 nullptr
 */
Creature* ObjectAccessor::GetCreature(WorldObject const& u, ObjectGuid const& guid)
{
    return u.GetMap()->GetCreature(guid);
}

/**
 * @brief 获取宠物对象
 * @param u 参考对象，用于确定地图范围
 * @param guid 宠物的 GUID
 * @return 找到的 Pet 指针，未找到返回 nullptr
 */
Pet* ObjectAccessor::GetPet(WorldObject const& u, ObjectGuid const& guid)
{
    return u.GetMap()->GetPet(guid);
}

/**
 * @brief 获取玩家对象（按地图）
 * @param m 地图指针
 * @param guid 玩家的 GUID
 * @return 找到的 Player 指针，未找到或不在指定地图返回 nullptr
 *
 * 实现细节：
 * 1. 在全局哈希表中查找玩家
 * 2. 验证玩家是否已进入世界
 * 3. 验证玩家是否在指定地图中
 */
Player* ObjectAccessor::GetPlayer(Map const* m, ObjectGuid const& guid)
{
    // 在全局索引中查找
    if (Player* player = HashMapHolder<Player>::Find(guid))
        // 验证玩家是否在指定地图中且已进入世界
        if (player->IsInWorld() && player->GetMap() == m)
            return player;

    return nullptr;
}

/**
 * @brief 获取玩家对象（按参考对象所在地图）
 * @param u 参考对象，用于确定地图范围
 * @param guid 玩家的 GUID
 * @return 找到的 Player 指针，未找到返回 nullptr
 */
Player* ObjectAccessor::GetPlayer(WorldObject const& u, ObjectGuid const& guid)
{
    return GetPlayer(u.GetMap(), guid);
}

/**
 * @brief 获取生物、宠物或载具
 * @param u 参考对象，用于确定地图范围
 * @param guid 目标 GUID
 * @return 找到的 Creature 指针，未找到返回 nullptr
 *
 * 实现细节：根据 GUID 类型判断并调用对应查找函数
 */
Creature* ObjectAccessor::GetCreatureOrPetOrVehicle(WorldObject const& u, ObjectGuid const& guid)
{
    if (guid.IsPet())
        return GetPet(u, guid);

    if (guid.IsCreatureOrVehicle())
        return GetCreature(u, guid);

    return nullptr;
}

/**
 * @brief 在整个世界中查找玩家
 * @param guid 玩家的 GUID
 * @return 找到的 Player 指针，未找到或不在线返回 nullptr
 *
 * 实现细节：只返回已经进入世界的玩家
 */
Player* ObjectAccessor::FindPlayer(ObjectGuid const& guid)
{
    Player* player = HashMapHolder<Player>::Find(guid);
    // 确保玩家已进入世界
    return player && player->IsInWorld() ? player : nullptr;
}

/**
 * @brief 按名称在整个世界中查找玩家
 * @param name 玩家名称
 * @return 找到的 Player 指针，未找到或不在线返回 nullptr
 *
 * 实现细节：使用名称索引查找，确保玩家已进入世界
 */
Player* ObjectAccessor::FindPlayerByName(std::string_view name)
{
    Player* player = PlayerNameMapHolder::Find(name);
    if (!player || !player->IsInWorld())
        return nullptr;

    return player;
}

/**
 * @brief 按低 ID 在整个世界中查找玩家
 * @param lowguid 玩家的低 ID（数据库 ID）
 * @return 找到的 Player 指针，未找到或不在线返回 nullptr
 *
 * 实现细节：构造完整 GUID 后调用 FindPlayer
 */
Player* ObjectAccessor::FindPlayerByLowGUID(ObjectGuid::LowType lowguid)
{
    // 构造完整的玩家 GUID
    ObjectGuid guid(HighGuid::Player, lowguid);
    return ObjectAccessor::FindPlayer(guid);
}

/**
 * @brief 查找已连接的玩家（即使不在世界中）
 * @param guid 玩家的 GUID
 * @return 找到的 Player 指针，未找到返回 nullptr
 *
 * 特殊用途：返回正在传送等暂时不在世界中的玩家
 */
Player* ObjectAccessor::FindConnectedPlayer(ObjectGuid const& guid)
{
    // 直接从全局索引返回，不检查是否在世界中
    return HashMapHolder<Player>::Find(guid);
}

/**
 * @brief 按名称查找已连接的玩家
 * @param name 玩家名称
 * @return 找到的 Player 指针，未找到返回 nullptr
 */
Player* ObjectAccessor::FindConnectedPlayerByName(std::string_view name)
{
    return PlayerNameMapHolder::Find(name);
}

/**
 * @brief 保存所有玩家数据到数据库
 *
 * 实现细节：
 * 1. 获取共享锁保护遍历
 * 2. 遍历所有玩家并调用 SaveToDB
 *
 * 调用时机：服务器关闭、定时保存等
 */
void ObjectAccessor::SaveAllPlayers()
{
    // 获取共享锁，允许其他线程读取
    std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());

    // 遍历所有玩家并保存
    HashMapHolder<Player>::MapType const& m = GetPlayers();
    for (HashMapHolder<Player>::MapType::const_iterator itr = m.begin(); itr != m.end(); ++itr)
        itr->second->SaveToDB();
}

/**
 * @brief 添加玩家到全局索引（特化版本）
 * @param player 玩家指针
 *
 * 实现细节：
 * 同时更新 GUID 索引和名称索引
 */
template<>
void ObjectAccessor::AddObject(Player* player)
{
    // 添加到 GUID 索引
    HashMapHolder<Player>::Insert(player);
    // 添加到名称索引
    PlayerNameMapHolder::Insert(player);
}

/**
 * @brief 从全局索引移除玩家（特化版本）
 * @param player 玩家指针
 *
 * 实现细节：
 * 同时从 GUID 索引和名称索引中移除
 */
template<>
void ObjectAccessor::RemoveObject(Player* player)
{
    // 从 GUID 索引移除
    HashMapHolder<Player>::Remove(player);
    // 从名称索引移除
    PlayerNameMapHolder::Remove(player);
}
