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
 * @file ObjectAccessor.h
 * @brief 对象访问器模块 - 提供全局对象查找和访问功能
 *
 * 模块职责：
 * 1. 管理游戏中所有对象的快速查找索引（玩家、运输工具等）
 * 2. 提供线程安全的对象访问接口
 * 3. 支持按 GUID 或名称查找对象
 * 4. 提供地图范围内的对象查询功能
 *
 * 设计理念：
 * - HashMapHolder：使用哈希表存储对象，支持 O(1) 时间复杂度的查找
 * - 线程安全：使用读写锁（shared_mutex）保护并发访问
 * - 分层查找：先检查全局索引，再检查地图范围
 *
 * 性能考虑：
 * - Player 和 Transport 对象存储在全局哈希表中，因为需要跨地图访问
 * - 其他对象（Creature、GameObject 等）存储在各自的地图中
 * - 使用读写锁允许多个读操作并发执行
 */

#ifndef TRINITY_OBJECTACCESSOR_H
#define TRINITY_OBJECTACCESSOR_H

#include "ObjectGuid.h"
#include <shared_mutex>
#include <unordered_map>

class Corpse;
class Creature;
class DynamicObject;
class GameObject;
class Map;
class Object;
class Pet;
class Player;
class Transport;
class Unit;
class WorldObject;

/**
 * @class HashMapHolder
 * @brief 哈希表持有者模板类 - 管理特定类型对象的全局哈希索引
 *
 * 职责：
 * - 维护对象类型 T 的全局哈希表索引
 * - 提供线程安全的插入、删除、查找操作
 * - 支持读写锁机制，允许多线程并发读取
 *
 * 使用限制：
 * - 仅支持 Player 和 Transport 类型（编译时静态检查）
 * - 不能实例化，所有方法都是静态的
 *
 * 线程安全：
 * - 写操作（Insert/Remove）使用独占锁
 * - 读操作（Find）使用共享锁
 *
 * @tparam T 对象类型（必须是 Player 或 Transport）
 */
template <class T>
class TC_GAME_API HashMapHolder
{
    // 禁止实例化，仅提供静态方法
    HashMapHolder() { }

public:

    /// 哈希表类型定义：ObjectGuid -> T* 的映射
    typedef std::unordered_map<ObjectGuid, T*> MapType;

    /**
     * @brief 将对象插入哈希表
     * @param o 要插入的对象指针
     *
     * 调用时机：当对象进入游戏世界时调用
     * 性能注意：使用独占锁，会阻塞其他写操作
     */
    static void Insert(T* o);

    /**
     * @brief 从哈希表中移除对象
     * @param o 要移除的对象指针
     *
     * 调用时机：当对象离开游戏世界时调用
     * 性能注意：使用独占锁，会阻塞其他写操作
     */
    static void Remove(T* o);

    /**
     * @brief 根据 GUID 查找对象
     * @param guid 对象的全局唯一标识符
     * @return 找到的对象指针，未找到返回 nullptr
     *
     * 性能注意：O(1) 时间复杂度，使用共享锁
     */
    static T* Find(ObjectGuid guid);

    /**
     * @brief 获取哈希表容器
     * @return 哈希表的引用
     *
     * 警告：调用者必须先获取锁才能访问容器
     * 使用场景：需要遍历所有对象时
     */
    static MapType& GetContainer();

    /**
     * @brief 获取读写锁
     * @return 读写锁指针
     *
     * 使用场景：需要在访问容器前手动加锁
     */
    static std::shared_mutex* GetLock();
};

/**
 * @namespace ObjectAccessor
 * @brief 对象访问器命名空间 - 提供统一的对象查找接口
 *
 * 主要功能：
 * 1. 按地图查找对象：返回指定地图范围内的对象
 * 2. 全局查找对象：在整个游戏世界中查找对象
 * 3. 对象生命周期管理：添加/移除对象索引
 */
namespace ObjectAccessor
{
    // =========================================================================
    // 按地图查找函数
    // 这些函数只返回指定对象所在地图中的对象
    // =========================================================================

    /**
     * @brief 根据 GUID 获取世界对象
     * @param p 参考对象，用于确定地图范围
     * @param guid 目标对象的 GUID
     * @return 找到的 WorldObject 指针，未找到返回 nullptr
     *
     * 根据 GUID 的高位类型自动路由到对应的查找函数
     */
    TC_GAME_API WorldObject* GetWorldObject(WorldObject const&, ObjectGuid const&);

    /**
     * @brief 根据类型掩码获取对象
     * @param p 参考对象，用于确定地图范围
     * @param guid 目标对象的 GUID
     * @param typemask 类型掩码（TYPEMASK_* 常量）
     * @return 找到的 Object 指针，未找到或类型不匹配返回 nullptr
     *
     * 使用场景：需要按类型过滤对象时
     */
    TC_GAME_API Object* GetObjectByTypeMask(WorldObject const&, ObjectGuid const&, uint32 typemask);

    /**
     * @brief 获取尸体对象
     * @param u 参考对象，用于确定地图范围
     * @param guid 尸体的 GUID
     * @return 找到的 Corpse 指针，未找到返回 nullptr
     */
    TC_GAME_API Corpse* GetCorpse(WorldObject const& u, ObjectGuid const& guid);

    /**
     * @brief 获取游戏对象
     * @param u 参考对象，用于确定地图范围
     * @param guid 游戏对象的 GUID
     * @return 找到的 GameObject 指针，未找到返回 nullptr
     */
    TC_GAME_API GameObject* GetGameObject(WorldObject const& u, ObjectGuid const& guid);

    /**
     * @brief 获取运输工具对象
     * @param u 参考对象，用于确定地图范围
     * @param guid 运输工具的 GUID
     * @return 找到的 Transport 指针，未找到返回 nullptr
     */
    TC_GAME_API Transport* GetTransport(WorldObject const& u, ObjectGuid const& guid);

    /**
     * @brief 获取动态对象
     * @param u 参考对象，用于确定地图范围
     * @param guid 动态对象的 GUID
     * @return 找到的 DynamicObject 指针，未找到返回 nullptr
     */
    TC_GAME_API DynamicObject* GetDynamicObject(WorldObject const& u, ObjectGuid const& guid);

    /**
     * @brief 获取单位对象（玩家/宠物/生物）
     * @param u 参考对象，用于确定地图范围
     * @param guid 单位的 GUID
     * @return 找到的 Unit 指针，未找到返回 nullptr
     *
     * 自动判断 GUID 类型并路由到对应的查找函数
     */
    TC_GAME_API Unit* GetUnit(WorldObject const&, ObjectGuid const& guid);

    /**
     * @brief 获取生物对象
     * @param u 参考对象，用于确定地图范围
     * @param guid 生物的 GUID
     * @return 找到的 Creature 指针，未找到返回 nullptr
     */
    TC_GAME_API Creature* GetCreature(WorldObject const& u, ObjectGuid const& guid);

    /**
     * @brief 获取宠物对象
     * @param u 参考对象，用于确定地图范围
     * @param guid 宠物的 GUID
     * @return 找到的 Pet 指针，未找到返回 nullptr
     */
    TC_GAME_API Pet* GetPet(WorldObject const&, ObjectGuid const& guid);

    /**
     * @brief 获取玩家对象（按地图）
     * @param m 地图指针
     * @param guid 玩家的 GUID
     * @return 找到的 Player 指针，未找到或不在指定地图返回 nullptr
     *
     * 检查玩家是否在指定地图中且已进入世界
     */
    TC_GAME_API Player* GetPlayer(Map const*, ObjectGuid const& guid);

    /**
     * @brief 获取玩家对象（按参考对象所在地图）
     * @param u 参考对象，用于确定地图范围
     * @param guid 玩家的 GUID
     * @return 找到的 Player 指针，未找到返回 nullptr
     */
    TC_GAME_API Player* GetPlayer(WorldObject const&, ObjectGuid const& guid);

    /**
     * @brief 获取生物、宠物或载具
     * @param u 参考对象，用于确定地图范围
     * @param guid 目标 GUID
     * @return 找到的 Creature 指针，未找到返回 nullptr
     *
     * 根据 GUID 类型自动判断并查找
     */
    TC_GAME_API Creature* GetCreatureOrPetOrVehicle(WorldObject const&, ObjectGuid const&);

    // =========================================================================
    // 全局查找函数
    // 这些函数在整个游戏世界中查找对象
    // 警告：全局查找不是线程安全的，需要特别注意
    // =========================================================================

    /**
     * @brief 在整个世界中查找玩家
     * @param guid 玩家的 GUID
     * @return 找到的 Player 指针，未找到或不在线返回 nullptr
     *
     * 警告：只返回已经进入世界的玩家
     */
    TC_GAME_API Player* FindPlayer(ObjectGuid const&);

    /**
     * @brief 按名称在整个世界中查找玩家
     * @param name 玩家名称
     * @return 找到的 Player 指针，未找到或不在线返回 nullptr
     *
     * 性能注意：使用名称索引查找，O(1) 时间复杂度
     */
    TC_GAME_API Player* FindPlayerByName(std::string_view name);

    /**
     * @brief 按低 ID 在整个世界中查找玩家
     * @param lowguid 玩家的低 ID（数据库 ID）
     * @return 找到的 Player 指针，未找到或不在线返回 nullptr
     */
    TC_GAME_API Player* FindPlayerByLowGUID(ObjectGuid::LowType lowguid);

    /**
     * @brief 查找已连接的玩家（即使不在世界中）
     * @param guid 玩家的 GUID
     * @return 找到的 Player 指针，未找到返回 nullptr
     *
     * 特殊用途：返回正在传送等暂时不在世界中的玩家
     * 例如：玩家传送过程中仍然可以查找到
     */
    TC_GAME_API Player* FindConnectedPlayer(ObjectGuid const&);

    /**
     * @brief 按名称查找已连接的玩家
     * @param name 玩家名称
     * @return 找到的 Player 指针，未找到返回 nullptr
     */
    TC_GAME_API Player* FindConnectedPlayerByName(std::string_view name);

    /**
     * @brief 获取所有玩家的哈希表
     * @return 玩家哈希表的常量引用
     *
     * 警告：使用前必须获取 HashMapHolder 的锁
     * 使用场景：需要遍历所有玩家时
     */
    TC_GAME_API HashMapHolder<Player>::MapType const& GetPlayers();

    /**
     * @brief 添加对象到全局索引
     * @param object 要添加的对象指针
     *
     * 调用时机：对象创建并进入游戏世界时
     */
    template<class T>
    void AddObject(T* object)
    {
        HashMapHolder<T>::Insert(object);
    }

    /**
     * @brief 从全局索引移除对象
     * @param object 要移除的对象指针
     *
     * 调用时机：对象离开游戏世界或销毁时
     */
    template<class T>
    void RemoveObject(T* object)
    {
        HashMapHolder<T>::Remove(object);
    }

    /// Player 特化版本：同时更新名称索引
    template<>
    void AddObject(Player* player);

    /// Player 特化版本：同时更新名称索引
    template<>
    void RemoveObject(Player* player);

    /**
     * @brief 保存所有玩家数据到数据库
     *
     * 调用时机：服务器关闭、定时保存等
     * 性能注意：遍历所有玩家并保存，可能耗时较长
     */
    TC_GAME_API void SaveAllPlayers();
};

#endif
