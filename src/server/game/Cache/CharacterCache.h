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
 * @file CharacterCache.h
 * @brief 角色缓存系统头文件
 *
 * 本模块实现了角色信息的内存缓存系统，用于提高角色数据访问性能。
 * 缓存数据包括：
 * - 角色基本信息（GUID、名字、账号ID、等级等）
 * - 角色种族、职业、性别
 * - 公会和竞技场队伍关联信息
 *
 * 性能优势：
 * - 避免频繁查询数据库获取角色基础信息
 * - 提供快速的GUID到名字、名字到GUID的双向查询
 * - 支持在线更新缓存数据，保持数据一致性
 *
 * 使用场景：
 * - 好友列表、公会成员列表等需要显示角色信息的界面
 * - 邮件系统、组队系统等需要查询角色信息的模块
 * - 角色选择界面显示角色列表
 */

#ifndef CharacterCache_h__
#define CharacterCache_h__

#include "Define.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include <string>

/**
 * @struct CharacterCacheEntry
 * @brief 角色缓存条目结构体
 *
 * 存储单个角色的完整缓存信息，包括基础属性和社会关系数据。
 * 每个在线或历史角色都会在缓存中维护这样一个条目。
 */
struct CharacterCacheEntry
{
    ObjectGuid Guid;              ///< 角色全局唯一标识符
    std::string Name;             ///< 角色名称
    uint32 AccountId;             ///< 所属账号ID
    uint8 Class;                  ///< 职业类型（战士、法师等）
    uint8 Race;                   ///< 种族类型（人类、兽人等）
    uint8 Sex;                    ///< 性别（男性/女性）
    uint8 Level;                  ///< 当前等级
    ObjectGuid::LowType GuildId;  ///< 所在公会ID（0表示无公会）
    uint32 ArenaTeamId[3];        ///< 竞技场队伍ID数组（索引0=2v2, 1=3v3, 2=5v5）
};

/**
 * @class CharacterCache
 * @brief 角色缓存管理器（单例模式）
 *
 * 提供角色信息的全局缓存服务，支持高效的角色数据查询和更新。
 * 采用单例模式，整个服务器进程只有一个实例。
 *
 * 核心功能：
 * - 服务器启动时从数据库加载所有角色基础信息
 * - 支持通过GUID或名字快速查询角色信息
 * - 实时更新角色数据变更（等级、公会、竞技场队伍等）
 * - 支持角色创建和删除时的缓存维护
 *
 * 性能说明：
 * - 使用哈希表存储，查询时间复杂度O(1)
 * - 维护GUID和名字两个索引，支持双向查询
 * - 不缓存不常用的数据，减少内存占用
 *
 * 线程安全：
 * - 该类不是线程安全的，应在主线程或适当加锁后使用
 */
class TC_GAME_API CharacterCache
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化角色缓存管理器，通常由单例模式调用
         */
        CharacterCache();

        /**
         * @brief 析构函数
         *
         * 清理缓存数据
         */
        ~CharacterCache();

        /**
         * @brief 获取单例实例
         * @return CharacterCache* 单例指针
         *
         * 使用静态局部变量实现线程安全的单例模式
         */
        static CharacterCache* instance();

        /**
         * @brief 从数据库加载所有角色缓存数据
         *
         * 在服务器启动时调用，从characters表加载所有角色的基础信息。
         * 包括GUID、名字、账号ID、种族、性别、职业和等级。
         * 公会和竞技场队伍信息会在各自的模块加载时填充。
         *
         * 调用时机：服务器启动初始化阶段
         * 性能注意：会执行全表扫描，加载所有角色记录
         */
        void LoadCharacterCacheStorage();

        /**
         * @brief 添加角色缓存条目
         * @param guid 角色GUID
         * @param accountId 账号ID
         * @param name 角色名称
         * @param gender 性别
         * @param race 种族
         * @param playerClass 职业
         * @param level 等级
         *
         * 当创建新角色或加载角色数据时调用。
         * 会同时更新GUID索引和名字索引。
         */
        void AddCharacterCacheEntry(ObjectGuid const& guid, uint32 accountId, std::string const& name, uint8 gender, uint8 race, uint8 playerClass, uint8 level);

        /**
         * @brief 删除角色缓存条目
         * @param guid 角色GUID
         * @param name 角色名称
         *
         * 当角色被删除时调用，清除内存中的缓存数据。
         * 会同时从GUID索引和名字索引中移除。
         */
        void DeleteCharacterCacheEntry(ObjectGuid const& guid, std::string const& name);

        /**
         * @brief 更新角色基础数据
         * @param guid 角色GUID
         * @param name 新名字（角色改名时使用）
         * @param gender 新性别（可选）
         * @param race 新种族（可选，阵营转换时使用）
         *
         * 更新角色的名字、性别或种族信息。
         * 如果名字改变，会同时更新名字索引。
         * 会向所有在线玩家发送角色数据失效通知。
         *
         * 调用时机：角色改名、阵营转换、性别转换等操作
         */
        void UpdateCharacterData(ObjectGuid const& guid, std::string const& name, Optional<uint8> gender = {}, Optional<uint8> race = {});

        /**
         * @brief 更新角色等级
         * @param guid 角色GUID
         * @param level 新等级
         *
         * 当角色升级或降级时调用，更新缓存中的等级信息。
         *
         * 调用时机：角色升级、经验值调整等
         */
        void UpdateCharacterLevel(ObjectGuid const& guid, uint8 level);

        /**
         * @brief 更新角色账号ID
         * @param guid 角色GUID
         * @param accountId 新账号ID
         *
         * 当角色转移到其他账号时调用。
         *
         * 调用时机：角色转服、账号转移等操作
         */
        void UpdateCharacterAccountId(ObjectGuid const& guid, uint32 accountId);

        /**
         * @brief 更新角色公会ID
         * @param guid 角色GUID
         * @param guildId 公会ID（0表示退出公会）
         *
         * 当角色加入或退出公会时调用。
         *
         * 调用时机：加入公会、退出公会、公会解散等
         */
        void UpdateCharacterGuildId(ObjectGuid const& guid, ObjectGuid::LowType guildId);

        /**
         * @brief 更新角色竞技场队伍ID
         * @param guid 角色GUID
         * @param slot 竞技场槽位（0=2v2, 1=3v3, 2=5v5）
         * @param arenaTeamId 竞技场队伍ID（0表示退出队伍）
         *
         * 当角色加入或退出竞技场队伍时调用。
         *
         * 调用时机：竞技场队伍创建、加入、退出、解散等
         */
        void UpdateCharacterArenaTeamId(ObjectGuid const& guid, uint8 slot, uint32 arenaTeamId);

        /**
         * @brief 检查角色缓存是否存在
         * @param guid 角色GUID
         * @return true 如果缓存中存在该角色
         *
         * 用于快速判断某个角色是否在缓存中。
         * 性能：O(1)时间复杂度
         */
        bool HasCharacterCacheEntry(ObjectGuid const& guid) const;

        /**
         * @brief 通过GUID获取角色缓存
         * @param guid 角色GUID
         * @return CharacterCacheEntry const* 缓存条目指针，不存在则返回nullptr
         *
         * 获取角色的完整缓存信息。
         * 性能：O(1)时间复杂度
         *
         * 使用示例：
         * @code
         * CharacterCacheEntry const* info = sCharacterCache->GetCharacterCacheByGuid(playerGuid);
         * if (info)
         *     printf("角色名: %s, 等级: %u\n", info->Name.c_str(), info->Level);
         * @endcode
         */
        CharacterCacheEntry const* GetCharacterCacheByGuid(ObjectGuid const& guid) const;

        /**
         * @brief 通过名字获取角色缓存
         * @param name 角色名称
         * @return CharacterCacheEntry const* 缓存条目指针，不存在则返回nullptr
         *
         * 通过角色名字查询缓存信息。
         * 性能：O(1)时间复杂度
         */
        CharacterCacheEntry const* GetCharacterCacheByName(std::string const& name) const;

        /**
         * @brief 通过名字获取角色GUID
         * @param name 角色名称
         * @return ObjectGuid 角色GUID，不存在则返回空GUID
         *
         * 用于名字到GUID的转换查询。
         * 性能：O(1)时间复杂度
         */
        ObjectGuid GetCharacterGuidByName(std::string const& name) const;

        /**
         * @brief 通过GUID获取角色名字
         * @param guid 角色GUID
         * @param name 输出参数，存储角色名字
         * @return true 如果找到角色，false 如果角色不存在
         *
         * 用于GUID到名字的转换查询。
         * 性能：O(1)时间复杂度
         */
        bool GetCharacterNameByGuid(ObjectGuid guid, std::string& name) const;

        /**
         * @brief 通过GUID获取角色所属阵营
         * @param guid 角色GUID
         * @return uint32 阵营ID（ALLIANCE/HORDE），不存在返回0
         *
         * 根据角色的种族计算所属阵营。
         * 性能：O(1)时间复杂度
         */
        uint32 GetCharacterTeamByGuid(ObjectGuid guid) const;

        /**
         * @brief 通过GUID获取账号ID
         * @param guid 角色GUID
         * @return uint32 账号ID，不存在返回0
         *
         * 查询角色所属的账号ID。
         * 性能：O(1)时间复杂度
         */
        uint32 GetCharacterAccountIdByGuid(ObjectGuid guid) const;

        /**
         * @brief 通过名字获取账号ID
         * @param name 角色名称
         * @return uint32 账号ID，不存在返回0
         *
         * 通过角色名字查询账号ID。
         * 性能：O(1)时间复杂度
         */
        uint32 GetCharacterAccountIdByName(std::string const& name) const;

        /**
         * @brief 通过GUID获取角色等级
         * @param guid 角色GUID
         * @return uint8 角色等级，不存在返回0
         *
         * 查询角色的当前等级。
         * 性能：O(1)时间复杂度
         */
        uint8 GetCharacterLevelByGuid(ObjectGuid guid) const;

        /**
         * @brief 通过GUID获取角色公会ID
         * @param guid 角色GUID
         * @return ObjectGuid::LowType 公会ID，不存在或无公会返回0
         *
         * 查询角色所在的公会ID。
         * 性能：O(1)时间复杂度
         */
        ObjectGuid::LowType GetCharacterGuildIdByGuid(ObjectGuid guid) const;

        /**
         * @brief 通过GUID获取角色竞技场队伍ID
         * @param guid 角色GUID
         * @param type 竞技场类型（2=2v2, 3=3v3, 5=5v5）
         * @return uint32 竞技场队伍ID，不存在或无队伍返回0
         *
         * 查询角色在指定类型竞技场中的队伍ID。
         * 性能：O(1)时间复杂度
         */
        uint32 GetCharacterArenaTeamIdByGuid(ObjectGuid guid, uint8 type) const;
};

#define sCharacterCache CharacterCache::instance()

#endif // CharacterCache_h__
