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
 * @file InstanceSaveMgr.h
 * @brief 副本存档管理器模块
 *
 * 本模块负责管理所有副本实例的存档数据,包括:
 * - 副本实例的创建、加载和卸载
 * - 玩家和团队与副本的绑定关系
 * - 副本重置时间的调度和管理
 * - 副本数据的持久化存储
 *
 * 主要用于支持魔兽世界中的副本系统,确保玩家进度能够正确保存和恢复。
 */

#ifndef _INSTANCESAVEMGR_H
#define _INSTANCESAVEMGR_H

#include <list>
#include <map>
#include <mutex>
#include <unordered_map>

#include "Define.h"
#include "DatabaseEnvFwd.h"
#include "DBCEnums.h"
#include "ObjectDefines.h"

struct InstanceTemplate;
struct MapEntry;
class Player;
class Group;

/**
 * @class InstanceSave
 * @brief 副本存档类,保存单个副本实例的所有必要信息
 *
 * 该类持有创建现有副本地图所需的所有信息。
 * 在以下三种情况下被引用:
 * - 单人玩家的玩家-副本绑定(不在团队中)
 * - 永久英雄/团队副本的玩家-副本绑定
 * - 团队-副本绑定(包括单人和永久绑定),缓存团队队长的玩家绑定
 *
 * 生命周期管理:
 * - 创建时机: 新实例生成时、绑定的玩家首次登录时、绑定团队加载时
 * - 销毁时机: 玩家列表和团队列表为空时、实例被重置时
 */
class TC_GAME_API InstanceSave
{
    friend class InstanceSaveManager;
    public:
        /**
         * @brief 构造函数,创建副本存档对象
         * @param MapId 地图ID
         * @param InstanceId 实例ID
         * @param difficulty 难度等级
         * @param resetTime 重置时间戳
         * @param canReset 是否可以重置
         *
         * 创建时机:
         * - 任何新实例被生成时
         * - 绑定到该实例ID的玩家首次登录时
         * - 绑定到该实例的团队被加载时
         */
        InstanceSave(uint16 MapId, uint32 InstanceId, Difficulty difficulty, time_t resetTime, bool canReset);

        /**
         * @brief 析构函数
         *
         * 当玩家列表和团队列表为空时,或实例被重置时卸载
         */
        ~InstanceSave();

        /**
         * @brief 获取绑定到该实例的在线玩家数量
         * @return 玩家数量
         */
        uint8 GetPlayerCount() const { return m_playerList.size(); }

        /**
         * @brief 获取绑定到该实例的团队数量
         * @return 团队数量
         */
        uint8 GetGroupCount() const { return m_groupList.size(); }

        /**
         * @brief 获取实例ID
         * @return 实例ID
         *
         * 注意: 与实例ID/地图ID对应的地图不一定总是存在。
         * InstanceSave对象可能在玩家登录时创建,但地图仅在玩家真正进入实例时才创建和加载。
         */
        uint32 GetInstanceId() const { return m_instanceid; }

        /**
         * @brief 获取地图ID
         * @return 地图ID
         */
        uint32 GetMapId() const { return m_mapid; }

        /**
         * @brief 将实例保存到数据库
         *
         * 当实例首次生成时调用
         */
        void SaveToDB();

        /**
         * @brief 从数据库删除实例记录
         *
         * 当实例被重置(永久删除)时调用
         */
        void DeleteFromDB();

        /**
         * @brief 获取重置时间
         * @return 重置时间戳
         *
         * 对于普通副本: 对应最大生物重生时间 + X小时
         * 对于团队/英雄副本: 缓存该地图的全局重生时间
         */
        time_t GetResetTime() const { return m_resetTime; }

        /**
         * @brief 设置重置时间
         * @param resetTime 新的重置时间戳
         */
        void SetResetTime(time_t resetTime) { m_resetTime = resetTime; }

        /**
         * @brief 获取用于数据库存储的重置时间
         * @return 数据库存储用的重置时间戳
         *
         * 仅普通副本保存重置时间,团队和英雄副本返回0
         */
        time_t GetResetTimeForDB();

        /**
         * @brief 获取副本模板信息
         * @return 副本模板指针
         */
        InstanceTemplate const* GetTemplate();

        /**
         * @brief 获取地图条目信息
         * @return 地图条目指针
         */
        MapEntry const* GetMapEntry();

        /**
         * @brief 添加绑定到实例的在线玩家
         * @param player 玩家指针
         *
         * 在线玩家绑定到实例(永久/单人)
         * 不包括团队成员,除非他们有永久绑定
         */
        void AddPlayer(Player* player)
        {
            std::lock_guard<std::mutex> lock(_playerListLock);
            m_playerList.push_back(player);
        }

        /**
         * @brief 移除绑定的玩家
         * @param player 玩家指针
         * @return 实例存档是否仍然有效
         *
         * 性能注意: 使用锁保护玩家列表操作,删除操作在锁释放后进行
         */
        bool RemovePlayer(Player* player)
        {
            _playerListLock.lock();
            m_playerList.remove(player);
            bool isStillValid = UnloadIfEmpty();
            _playerListLock.unlock();

            // 在释放锁之后删除(如果需要)
            if (m_toDelete)
                delete this;

            return isStillValid;
        }

        /**
         * @brief 添加绑定到实例的团队
         * @param group 团队指针
         */
        void AddGroup(Group* group) { m_groupList.push_back(group); }

        /**
         * @brief 移除绑定的团队
         * @param group 团队指针
         * @return 实例存档是否仍然有效
         */
        bool RemoveGroup(Group* group)
        {
            m_groupList.remove(group);
            bool isStillValid = UnloadIfEmpty();
            if (m_toDelete)
                delete this;
            return isStillValid;
        }

        /**
         * @brief 检查实例是否可以重置
         * @return 是否可以重置
         *
         * 如果有玩家永久绑定到实例,则实例不能重置(全局重置时间除外)
         * 这个状态被缓存以应对这些玩家离线的情况
         */
        bool CanReset() const { return m_canReset; }

        /**
         * @brief 设置是否可以重置
         * @param canReset 是否可以重置
         */
        void SetCanReset(bool canReset) { m_canReset = canReset; }

        /**
         * @brief 获取难度等级
         * @return 难度等级
         */
        Difficulty GetDifficulty() const { return m_difficulty; }

        typedef std::list<Player*> PlayerListType;   ///< 玩家列表类型
        typedef std::list<Group*> GroupListType;     ///< 团队列表类型

    private:
        /**
         * @brief 如果玩家列表和团队列表为空则卸载实例
         * @return 实例存档是否仍然有效
         */
        bool UnloadIfEmpty();

        /**
         * @brief 标记实例存档待删除
         * @param toDelete 是否待删除
         *
         * 用于标记InstanceSave对象待删除,让调用者可以在释放锁后安全删除
         */
        void SetToDelete(bool toDelete)
        {
            m_toDelete = toDelete;
        }

        PlayerListType m_playerList;     ///< 绑定到实例的在线玩家列表
        GroupListType m_groupList;       ///< 绑定到实例的团队列表
        time_t m_resetTime;              ///< 重置时间戳
        uint32 m_instanceid;             ///< 实例ID
        uint32 m_mapid;                  ///< 地图ID
        Difficulty m_difficulty;         ///< 难度等级
        bool m_canReset;                 ///< 是否可以重置
        bool m_toDelete;                 ///< 是否待删除标记

        std::mutex _playerListLock;      ///< 玩家列表锁,保护并发访问
};

typedef std::unordered_map<uint32 /*PAIR32(map, difficulty)*/, time_t /*resetTime*/> ResetTimeByMapDifficultyMap;

/**
 * @class InstanceSaveManager
 * @brief 副本存档管理器,全局单例类
 *
 * 负责管理所有副本实例的存档数据,包括:
 * - 实例存档的创建、加载、卸载和删除
 * - 副本重置时间的调度和执行
 * - 玩家和团队绑定关系的维护
 * - 全局重置时间管理
 *
 * 该类是单例模式,通过sInstanceSaveMgr宏访问
 */
class TC_GAME_API InstanceSaveManager
{
    friend class InstanceSave;

    private:
        /**
         * @brief 私有构造函数(单例模式)
         */
        InstanceSaveManager() : lock_instLists(false) { };

        /**
         * @brief 析构函数
         */
        ~InstanceSaveManager();

    public:
        typedef std::unordered_map<uint32 /*InstanceId*/, InstanceSave*> InstanceSaveHashMap;   ///< 实例存档哈希映射类型

        /**
         * @brief 获取单例实例
         * @return InstanceSaveManager单例指针
         */
        static InstanceSaveManager* instance();

        /**
         * @brief 卸载所有实例存档
         *
         * 在服务器关闭时调用,清理所有实例存档数据
         */
        void Unload();

        /**
         * @struct InstResetEvent
         * @brief 实例重置事件结构
         *
         * 重置时间是每个(团队/英雄)地图的全局属性
         * 该地图的所有实例在同一时间重置
         */
        struct InstResetEvent
        {
            uint8 type;              ///< 事件类型(0:普通实例重置,1-4:团队/英雄副本重置警告或重置)
            Difficulty difficulty;   ///< 难度等级
            uint16 mapid;            ///< 地图ID
            uint16 instanceId;       ///< 实例ID

            /**
             * @brief 默认构造函数
             */
            InstResetEvent() : type(0), difficulty(DUNGEON_DIFFICULTY_NORMAL), mapid(0), instanceId(0) { }

            /**
             * @brief 构造函数
             * @param t 事件类型
             * @param _mapid 地图ID
             * @param d 难度
             * @param _instanceid 实例ID
             */
            InstResetEvent(uint8 t, uint32 _mapid, Difficulty d, uint16 _instanceid)
                : type(t), difficulty(d), mapid(_mapid), instanceId(_instanceid) { }

            /**
             * @brief 相等比较运算符
             * @param e 另一个事件
             * @return 是否相等
             */
            bool operator==(InstResetEvent const& e) const { return e.instanceId == instanceId; }
        };

        typedef std::multimap<time_t /*resetTime*/, InstResetEvent> ResetTimeQueue;   ///< 重置时间队列类型

        /**
         * @brief 加载所有实例数据
         *
         * 在服务器启动时调用,从数据库加载实例数据
         * 包括清理过期实例、初始化实例ID存储、加载重置时间
         */
        void LoadInstances();

        /**
         * @brief 加载重置时间
         *
         * 从数据库加载全局重置时间,并调度重置事件
         */
        void LoadResetTimes();

        /**
         * @brief 获取指定地图和难度的重置时间
         * @param mapid 地图ID
         * @param d 难度等级
         * @return 重置时间戳,不存在则返回0
         */
        time_t GetResetTimeFor(uint32 mapid, Difficulty d) const
        {
            ResetTimeByMapDifficultyMap::const_iterator itr  = m_resetTimeByMapDifficulty.find(MAKE_PAIR32(mapid, d));
            return itr != m_resetTimeByMapDifficulty.end() ? itr->second : 0;
        }

        /**
         * @brief 获取后续重置时间
         * @param mapid 地图ID
         * @param difficulty 难度等级
         * @param resetTime 当前重置时间
         * @return 下一次重置时间戳
         */
        time_t GetSubsequentResetTime(uint32 mapid, Difficulty difficulty, time_t resetTime) const;

        /**
         * @brief 初始化重置时间(仅在启动时使用)
         * @param mapid 地图ID
         * @param d 难度等级
         * @param t 重置时间戳
         */
        void InitializeResetTimeFor(uint32 mapid, Difficulty d, time_t t)
        {
            m_resetTimeByMapDifficulty[MAKE_PAIR32(mapid, d)] = t;
        }

        /**
         * @brief 设置重置时间(仅用于更新现有重置时间)
         * @param mapid 地图ID
         * @param d 难度等级
         * @param t 重置时间戳
         */
        void SetResetTimeFor(uint32 mapid, Difficulty d, time_t t);

        /**
         * @brief 获取重置时间映射表
         * @return 重置时间映射表的常量引用
         */
        ResetTimeByMapDifficultyMap const& GetResetTimeMap() const
        {
            return m_resetTimeByMapDifficulty;
        }

        /**
         * @brief 调度重置事件
         * @param add true:添加事件, false:移除事件
         * @param time 重置时间戳
         * @param event 重置事件对象
         */
        void ScheduleReset(bool add, time_t time, InstResetEvent event);

        /**
         * @brief 强制全局重置
         * @param mapId 地图ID
         * @param difficulty 难度等级
         *
         * 立即强制重置指定地图和难度的所有实例
         */
        void ForceGlobalReset(uint32 mapId, Difficulty difficulty);

        /**
         * @brief 更新重置队列
         *
         * 每个世界更新周期调用,检查并执行到期的重置事件
         */
        void Update();

        /**
         * @brief 添加实例存档
         * @param mapId 地图ID
         * @param instanceId 实例ID
         * @param difficulty 难度等级
         * @param resetTime 重置时间
         * @param canReset 是否可重置
         * @param load 是否从数据库加载(默认false)
         * @return 实例存档指针
         *
         * 如果实例存档已存在,则返回现有存档
         */
        InstanceSave* AddInstanceSave(uint32 mapId, uint32 instanceId, Difficulty difficulty, time_t resetTime,
            bool canReset, bool load = false);

        /**
         * @brief 移除实例存档
         * @param InstanceId 实例ID
         *
         * 从管理器中移除实例存档,并保存重置时间到数据库
         */
        void RemoveInstanceSave(uint32 InstanceId);

        /**
         * @brief 卸载实例存档
         * @param InstanceId 实例ID
         *
         * 如果实例存档为空则卸载
         */
        void UnloadInstanceSave(uint32 InstanceId);

        /**
         * @brief 从数据库删除实例记录
         * @param instanceid 实例ID
         *
         * 静态方法,直接删除数据库中的实例记录
         */
        static void DeleteInstanceFromDB(uint32 instanceid);

        /**
         * @brief 获取实例存档
         * @param InstanceId 实例ID
         * @return 实例存档指针,不存在则返回nullptr
         */
        InstanceSave* GetInstanceSave(uint32 InstanceId);

        /**
         * @brief 获取实例存档总数
         * @return 存档数量
         */
        uint32 GetNumInstanceSaves() const { return uint32(m_instanceSaveById.size()); }

        /**
         * @brief 获取绑定玩家总数
         * @return 绑定玩家总数
         */
        uint32 GetNumBoundPlayersTotal() const;

        /**
         * @brief 获取绑定团队总数
         * @return 绑定团队总数
         */
        uint32 GetNumBoundGroupsTotal() const;

    protected:
        static uint16 ResetTimeDelay[];   ///< 重置延迟数组,存储重置前的警告时间间隔

    private:
        /**
         * @brief 重置或警告所有实例
         * @param mapid 地图ID
         * @param difficulty 难度等级
         * @param warn 是否仅发送警告
         * @param resetTime 重置时间
         *
         * 执行全局重置或发送重置警告
         */
        void _ResetOrWarnAll(uint32 mapid, Difficulty difficulty, bool warn, time_t resetTime);

        /**
         * @brief 重置单个实例
         * @param mapid 地图ID
         * @param instanceId 实例ID
         *
         * 重置指定的实例,删除相关数据
         */
        void _ResetInstance(uint32 mapid, uint32 instanceId);

        /**
         * @brief 重置存档
         * @param itr 实例存档迭代器
         *
         * 解绑所有玩家和团队,根据条件删除或保留存档
         */
        void _ResetSave(InstanceSaveHashMap::iterator &itr);

        bool lock_instLists;                          ///< 实例列表锁定标志,用于全局实例重置期间
        InstanceSaveHashMap m_instanceSaveById;       ///< 实例ID到实例存档的快速查找映射
        ResetTimeByMapDifficultyMap m_resetTimeByMapDifficulty;  ///< 重置时间快速查找映射
        ResetTimeQueue m_resetTimeQueue;              ///< 重置时间优先队列
};

#define sInstanceSaveMgr InstanceSaveManager::instance()
#endif
