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
 * @file CharacterCache.cpp
 * @brief 角色缓存系统实现文件
 *
 * 本文件实现了角色缓存管理器的所有功能，包括：
 * - 服务器启动时从数据库加载角色数据
 * - 缓存数据的增删改查操作
 * - 双向索引的维护（GUID和名字）
 *
 * 数据结构设计：
 * - _characterCacheStore: GUID -> 缓存条目的映射
 * - _characterCacheByNameStore: 名字 -> 缓存条目指针的映射
 *
 * 维护说明：
 * - 两个索引必须保持同步
 * - 添加/删除时同时更新两个索引
 * - 名字变更时需要更新名字索引
 */

#include "CharacterCache.h"
#include "ArenaTeam.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "MiscPackets.h"
#include "Player.h"
#include "Timer.h"
#include "World.h"
#include "WorldPacket.h"
#include <unordered_map>

namespace
{
    /// 角色缓存存储器：GUID -> CharacterCacheEntry 的哈希映射
    std::unordered_map<ObjectGuid, CharacterCacheEntry> _characterCacheStore;
    /// 角色名字索引：角色名 -> 缓存条目指针的哈希映射
    std::unordered_map<std::string, CharacterCacheEntry*> _characterCacheByNameStore;
}

CharacterCache::CharacterCache()
{
}

CharacterCache::~CharacterCache()
{
}

/**
 * @brief 获取单例实例
 * @return CharacterCache* 单例指针
 *
 * 使用Meyer's Singleton模式（静态局部变量），C++11保证线程安全。
 * 整个服务器进程只有一个CharacterCache实例。
 */
CharacterCache* CharacterCache::instance()
{
    static CharacterCache instance;
    return &instance;
}

/**
 * @brief 从数据库加载所有角色缓存数据
 *
 * 在服务器启动时调用，执行一次性全量加载。
 * 查询characters表获取所有角色的基础信息并构建缓存索引。
 *
 * 加载流程：
 * 1. 清空现有缓存
 * 2. 执行数据库查询
 * 3. 遍历结果集，逐个添加缓存条目
 * 4. 记录加载统计信息
 *
 * 性能考虑：
 * - 执行全表扫描，可能消耗较多时间
 * - 加载期间不阻塞其他初始化，但缓存未完成时查询会失败
 * - 建议在服务器低负载时执行（启动阶段）
 *
 * @note 公会和竞技场队伍ID在此处初始化为0，
 *       后续会由公会和竞技场模块加载时更新
 */
void CharacterCache::LoadCharacterCacheStorage()
{
    // 清空现有缓存数据
    _characterCacheStore.clear();
    uint32 oldMSTime = getMSTime();

    // 查询数据库获取所有角色的基础信息
    // 字段顺序：guid, name, account, race, gender, class, level
    QueryResult result = CharacterDatabase.Query("SELECT guid, name, account, race, gender, class, level FROM characters");
    if (!result)
    {
        TC_LOG_INFO("server.loading", "No character name data loaded, empty query");
        return;
    }

    // 遍历查询结果，逐个添加缓存条目
    do
    {
        Field* fields = result->Fetch();
        // 构造Player GUID并添加到缓存
        // 注意：公会ID和竞技场队伍ID初始化为0，稍后由各自模块填充
        AddCharacterCacheEntry(ObjectGuid::Create<HighGuid::Player>(fields[0].GetUInt32()) /*guid*/, fields[2].GetUInt32() /*account*/, fields[1].GetString() /*name*/,
            fields[4].GetUInt8() /*gender*/, fields[3].GetUInt8() /*race*/, fields[5].GetUInt8() /*class*/, fields[6].GetUInt8() /*level*/);
    } while (result->NextRow());

    // 记录加载性能统计
    TC_LOG_INFO("server.loading", "Loaded character infos for {} characters in {} ms", _characterCacheStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

/*
 * ============================================================
 * 修改类函数实现
 * ============================================================
 */

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
 * 创建新的缓存条目并添加到两个索引中。
 * 初始化公会和竞技场队伍ID为0，由后续模块填充。
 *
 * 调用时机：
 * - LoadCharacterCacheStorage() 加载所有角色时
 * - 创建新角色时
 */
void CharacterCache::AddCharacterCacheEntry(ObjectGuid const& guid, uint32 accountId, std::string const& name, uint8 gender, uint8 race, uint8 playerClass, uint8 level)
{
    // 在GUID索引中创建或获取缓存条目
    CharacterCacheEntry& data = _characterCacheStore[guid];
    data.Guid = guid;
    data.Name = name;
    data.AccountId = accountId;
    data.Race = race;
    data.Sex = gender;
    data.Class = playerClass;
    data.Level = level;
    data.GuildId = 0;                           // 公会ID将在公会模块加载时设置
    for (uint8 i = 0; i < MAX_ARENA_SLOT; ++i)
        data.ArenaTeamId[i] = 0;                // 竞技场队伍ID将在竞技场模块加载时设置

    // 维护名字索引：名字 -> 缓存条目指针
    _characterCacheByNameStore[name] = &data;
}

/**
 * @brief 删除角色缓存条目
 * @param guid 角色GUID
 * @param name 角色名称
 *
 * 从两个索引中移除缓存条目。
 * 注意：两个参数都需要提供，确保索引同步删除。
 *
 * 调用时机：角色被删除时
 */
void CharacterCache::DeleteCharacterCacheEntry(ObjectGuid const& guid, std::string const& name)
{
    // 从GUID索引中删除
    _characterCacheStore.erase(guid);
    // 从名字索引中删除
    _characterCacheByNameStore.erase(name);
}

/**
 * @brief 更新角色基础数据
 * @param guid 角色GUID
 * @param name 新名字
 * @param gender 新性别（可选）
 * @param race 新种族（可选）
 *
 * 更新角色的名字、性别或种族，并维护名字索引。
 * 名字变更时需要同时更新两个索引以保持同步。
 *
 * 调用时机：
 * - 角色改名
 * - 阵营转换（改变种族）
 * - 性别转换
 *
 * 性能考虑：会向所有在线玩家广播角色数据失效通知
 */
void CharacterCache::UpdateCharacterData(ObjectGuid const& guid, std::string const& name, Optional<uint8> gender /*= {}*/, Optional<uint8> race /*= {}*/)
{
    // 查找缓存条目
    auto itr = _characterCacheStore.find(guid);
    if (itr == _characterCacheStore.end())
        return;

    // 保存旧名字用于更新索引
    std::string oldName = itr->second.Name;
    itr->second.Name = name;

    // 更新性别（如果提供）
    if (gender)
        itr->second.Sex = *gender;

    // 更新种族（如果提供）
    if (race)
        itr->second.Race = *race;

    // 广播角色数据失效通知，强制客户端刷新
    WorldPackets::Misc::InvalidatePlayer packet(guid);
    sWorld->SendGlobalMessage(packet.Write());

    // 更新名字索引：删除旧名字映射，添加新名字映射
    _characterCacheByNameStore.erase(oldName);
    _characterCacheByNameStore[name] = &itr->second;
}

/**
 * @brief 更新角色等级
 * @param guid 角色GUID
 * @param level 新等级
 *
 * 调用时机：角色升级、降级、经验调整等
 */
void CharacterCache::UpdateCharacterLevel(ObjectGuid const& guid, uint8 level)
{
    auto itr = _characterCacheStore.find(guid);
    if (itr == _characterCacheStore.end())
        return;

    itr->second.Level = level;
}

/**
 * @brief 更新角色账号ID
 * @param guid 角色GUID
 * @param accountId 新账号ID
 *
 * 调用时机：角色转移账号（角色转服、账号合并等）
 */
void CharacterCache::UpdateCharacterAccountId(ObjectGuid const& guid, uint32 accountId)
{
    auto itr = _characterCacheStore.find(guid);
    if (itr == _characterCacheStore.end())
        return;

    itr->second.AccountId = accountId;
}

/**
 * @brief 更新角色公会ID
 * @param guid 角色GUID
 * @param guildId 公会ID（0表示无公会）
 *
 * 调用时机：
 * - 角色加入公会
 * - 角色退出公会
 * - 公会解散
 */
void CharacterCache::UpdateCharacterGuildId(ObjectGuid const& guid, ObjectGuid::LowType guildId)
{
    auto itr = _characterCacheStore.find(guid);
    if (itr == _characterCacheStore.end())
        return;

    itr->second.GuildId = guildId;
}

/**
 * @brief 更新角色竞技场队伍ID
 * @param guid 角色GUID
 * @param slot 竞技场槽位（0=2v2, 1=3v3, 2=5v5）
 * @param arenaTeamId 竞技场队伍ID（0表示无队伍）
 *
 * 调用时机：
 * - 创建或加入竞技场队伍
 * - 退出竞技场队伍
 * - 队伍解散
 */
void CharacterCache::UpdateCharacterArenaTeamId(ObjectGuid const& guid, uint8 slot, uint32 arenaTeamId)
{
    auto itr = _characterCacheStore.find(guid);
    if (itr == _characterCacheStore.end())
        return;

    // 断言检查槽位有效性（必须小于3）
    ASSERT(slot < 3);
    itr->second.ArenaTeamId[slot] = arenaTeamId;
}

/*
 * ============================================================
 * 查询类函数实现
 * ============================================================
 */

/**
 * @brief 检查角色缓存是否存在
 * @param guid 角色GUID
 * @return true 缓存中存在该角色，false 不存在
 *
 * 性能：O(1)时间复杂度
 */
bool CharacterCache::HasCharacterCacheEntry(ObjectGuid const& guid) const
{
    return _characterCacheStore.find(guid) != _characterCacheStore.end();
}

/**
 * @brief 通过GUID获取角色缓存完整信息
 * @param guid 角色GUID
 * @return CharacterCacheEntry const* 缓存条目指针，不存在返回nullptr
 *
 * 性能：O(1)时间复杂度
 */
CharacterCacheEntry const* CharacterCache::GetCharacterCacheByGuid(ObjectGuid const& guid) const
{
    auto itr = _characterCacheStore.find(guid);
    if (itr != _characterCacheStore.end())
        return &itr->second;

    return nullptr;
}

/**
 * @brief 通过名字获取角色缓存完整信息
 * @param name 角色名称
 * @return CharacterCacheEntry const* 缓存条目指针，不存在返回nullptr
 *
 * 性能：O(1)时间复杂度
 */
CharacterCacheEntry const* CharacterCache::GetCharacterCacheByName(std::string const& name) const
{
    auto itr = _characterCacheByNameStore.find(name);
    if (itr != _characterCacheByNameStore.end())
        return itr->second;

    return nullptr;
}

/**
 * @brief 通过名字获取角色GUID
 * @param name 角色名称
 * @return ObjectGuid 角色GUID，不存在返回空GUID
 *
 * 性能：O(1)时间复杂度
 */
ObjectGuid CharacterCache::GetCharacterGuidByName(std::string const& name) const
{
    auto itr = _characterCacheByNameStore.find(name);
    if (itr != _characterCacheByNameStore.end())
        return itr->second->Guid;

    return ObjectGuid::Empty;
}

/**
 * @brief 通过GUID获取角色名字
 * @param guid 角色GUID
 * @param name 输出参数，存储角色名字
 * @return true 成功获取名字，false 角色不存在
 *
 * 性能：O(1)时间复杂度
 */
bool CharacterCache::GetCharacterNameByGuid(ObjectGuid guid, std::string& name) const
{
    auto itr = _characterCacheStore.find(guid);
    if (itr == _characterCacheStore.end())
        return false;

    name = itr->second.Name;
    return true;
}

/**
 * @brief 通过GUID获取角色所属阵营
 * @param guid 角色GUID
 * @return uint32 阵营ID（ALLIANCE/HORDE），不存在返回0
 *
 * 根据种族计算阵营归属。
 * 性能：O(1)时间复杂度
 */
uint32 CharacterCache::GetCharacterTeamByGuid(ObjectGuid guid) const
{
    auto itr = _characterCacheStore.find(guid);
    if (itr == _characterCacheStore.end())
        return 0;

    // 根据种族返回所属阵营
    return Player::TeamForRace(itr->second.Race);
}

/**
 * @brief 通过GUID获取账号ID
 * @param guid 角色GUID
 * @return uint32 账号ID，不存在返回0
 *
 * 性能：O(1)时间复杂度
 */
uint32 CharacterCache::GetCharacterAccountIdByGuid(ObjectGuid guid) const
{
    auto itr = _characterCacheStore.find(guid);
    if (itr == _characterCacheStore.end())
        return 0;

    return itr->second.AccountId;
}

/**
 * @brief 通过名字获取账号ID
 * @param name 角色名称
 * @return uint32 账号ID，不存在返回0
 *
 * 性能：O(1)时间复杂度
 */
uint32 CharacterCache::GetCharacterAccountIdByName(std::string const& name) const
{
    auto itr = _characterCacheByNameStore.find(name);
    if (itr != _characterCacheByNameStore.end())
        return itr->second->AccountId;

    return 0;
}

/**
 * @brief 通过GUID获取角色等级
 * @param guid 角色GUID
 * @return uint8 角色等级，不存在返回0
 *
 * 性能：O(1)时间复杂度
 */
uint8 CharacterCache::GetCharacterLevelByGuid(ObjectGuid guid) const
{
    auto itr = _characterCacheStore.find(guid);
    if (itr == _characterCacheStore.end())
        return 0;

    return itr->second.Level;
}

/**
 * @brief 通过GUID获取角色公会ID
 * @param guid 角色GUID
 * @return ObjectGuid::LowType 公会ID，不存在或无公会返回0
 *
 * 性能：O(1)时间复杂度
 */
ObjectGuid::LowType CharacterCache::GetCharacterGuildIdByGuid(ObjectGuid guid) const
{
    auto itr = _characterCacheStore.find(guid);
    if (itr == _characterCacheStore.end())
        return 0;

    return itr->second.GuildId;
}

/**
 * @brief 通过GUID获取角色竞技场队伍ID
 * @param guid 角色GUID
 * @param type 竞技场类型（2=2v2, 3=3v3, 5=5v5）
 * @return uint32 竞技场队伍ID，不存在或无队伍返回0
 *
 * 将竞技场类型（队伍人数）转换为槽位索引后查询。
 * 性能：O(1)时间复杂度
 */
uint32 CharacterCache::GetCharacterArenaTeamIdByGuid(ObjectGuid guid, uint8 type) const
{
    auto itr = _characterCacheStore.find(guid);
    if (itr == _characterCacheStore.end())
        return 0;

    // 将竞技场类型转换为槽位索引（2->0, 3->1, 5->2）
    uint8 slot = ArenaTeam::GetSlotByType(type);
    ASSERT(slot < 3);
    return itr->second.ArenaTeamId[slot];
}
