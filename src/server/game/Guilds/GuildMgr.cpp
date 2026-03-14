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
 * @file GuildMgr.cpp
 * @brief 公会管理器模块实现文件
 *
 * 本文件实现了公会管理器(GuildMgr)类的所有成员函数，包括：
 * - 公会数据的加载、查询和管理
 * - 公会ID的生成
 * - 公会相关定时任务的处理
 *
 * 加载流程（LoadGuilds）：
 * 1. 从数据库加载公会基本信息
 * 2. 加载公会等级权限设置
 * 3. 加载公会成员列表
 * 4. 加载公会银行标签页权限
 * 5. 加载公会事件日志
 * 6. 加载公会银行事件日志
 * 7. 加载公会银行标签页信息
 * 8. 加载公会银行物品
 * 9. 验证加载的公会数据
 */

#include "GuildMgr.h"
#include "DatabaseEnv.h"
#include "Guild.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Util.h"
#include "World.h"

/**
 * @brief 构造函数 - 初始化公会管理器
 *
 * 将下一个公会ID初始化为1，用于后续创建新公会时分配ID
 */
GuildMgr::GuildMgr() : NextGuildId(1)
{ }

/**
 * @brief 析构函数 - 清理公会管理器资源
 *
 * 使用默认析构函数，智能指针会自动清理公会对象
 */
GuildMgr::~GuildMgr() = default;

/**
 * @brief 向管理器添加公会
 * @param guild 要添加的公会对象指针
 *
 * 将公会对象添加到全局存储容器中，使用智能指针管理生命周期
 * 并设置公会的弱引用指针，以便公会内部可以访问自己
 */
void GuildMgr::AddGuild(Guild* guild)
{
    Trinity::unique_trackable_ptr<Guild>& ptr = GuildStore[guild->GetId()];
    ptr.reset(guild);
    guild->SetWeakPtr(ptr);
}

/**
 * @brief 从管理器移除公会
 * @param guildId 要移除的公会ID
 *
 * 从存储容器中删除指定公会的条目，智能指针会自动销毁公会对象
 */
void GuildMgr::RemoveGuild(ObjectGuid::LowType guildId)
{
    GuildStore.erase(guildId);
}

/**
 * @brief 生成新的公会ID
 * @return 新的唯一公会ID
 *
 * 返回下一个可用的公会ID并递增计数器
 * 如果ID即将溢出（接近最大值），则关闭服务器以防止数据损坏
 *
 * 注意：公会ID使用32位整数，最大值为0xFFFFFFFE
 */
ObjectGuid::LowType GuildMgr::GenerateGuildId()
{
    // 检查ID是否即将溢出，防止生成重复ID
    if (NextGuildId >= 0xFFFFFFFE)
    {
        TC_LOG_ERROR("guild", "Guild ids overflow!! Can't continue, shutting down server. ");
        World::StopNow(ERROR_EXIT_CODE);
    }
    return NextGuildId++;
}

/**
 * @brief 根据公会ID获取公会对象
 * @param guildId 公会的唯一ID
 * @return 公会对象指针，未找到返回nullptr
 *
 * 使用哈希表查找，时间复杂度O(1)
 */
Guild* GuildMgr::GetGuildById(ObjectGuid::LowType guildId) const
{
    GuildContainer::const_iterator itr = GuildStore.find(guildId);
    if (itr != GuildStore.end())
        return itr->second.get();

    return nullptr;
}

/**
 * @brief 根据公会名称获取公会对象
 * @param guildName 公会名称
 * @return 公会对象指针，未找到返回nullptr
 *
 * 遍历所有公会进行名称比较（不区分大小写）
 * 性能较低，不建议在频繁调用的代码中使用
 */
Guild* GuildMgr::GetGuildByName(std::string_view guildName) const
{
    for (auto const& [id, guild] : GuildStore)
        if (StringEqualI(guild->GetName(), guildName))
            return guild.get();

    return nullptr;
}

/**
 * @brief 根据公会ID获取公会名称
 * @param guildId 公会的唯一ID
 * @return 公会名称，未找到返回空字符串
 *
 * 先通过ID获取公会对象，再返回名称
 */
std::string GuildMgr::GetGuildNameById(ObjectGuid::LowType guildId) const
{
    if (Guild* guild = GetGuildById(guildId))
        return guild->GetName();

    return "";
}

/**
 * @brief 获取公会管理器单例实例
 * @return 公会管理器的全局唯一实例指针
 *
 * 使用静态局部变量实现线程安全的单例模式（C++11 magic statics）
 */
GuildMgr* GuildMgr::instance()
{
    static GuildMgr instance;
    return &instance;
}

/**
 * @brief 根据会长GUID获取公会
 * @param guid 会长玩家的GUID
 * @return 公会对象指针，未找到返回nullptr
 *
 * 遍历所有公会查找匹配的会长
 * 用于验证玩家是否已经是某个公会的会长
 */
Guild* GuildMgr::GetGuildByLeader(ObjectGuid guid) const
{
    for (GuildContainer::const_iterator itr = GuildStore.begin(); itr != GuildStore.end(); ++itr)
        if (itr->second->GetLeaderGUID() == guid)
            return itr->second.get();

    return nullptr;
}

/**
 * @brief 从数据库加载所有公会数据
 *
 * 这是服务器启动时的关键加载函数，按顺序执行以下步骤：
 * 1. 加载公会基本信息（名称、会长、会徽等）
 * 2. 加载公会等级权限设置
 * 3. 加载公会成员列表
 * 4. 加载公会银行标签页权限
 * 5. 加载公会事件日志
 * 6. 加载公会银行事件日志
 * 7. 加载公会银行标签页信息
 * 8. 加载公会银行物品
 * 9. 验证加载的公会数据完整性
 *
 * 每个步骤都会记录加载时间和数量，用于性能监控
 * 如果发现孤立的数据（属于不存在的公会），会自动清理
 *
 * 性能注意事项：
 * - 这是重量级操作，仅在服务器启动时执行
 * - 使用多个SQL查询分批加载不同类型的数据
 * - 删除孤立数据操作使用DirectExecute避免事务开销
 */
void GuildMgr::LoadGuilds()
{
    // 步骤1: 加载所有公会基本信息
    TC_LOG_INFO("server.loading", "Loading guilds definitions...");
    {
        uint32 oldMSTime = getMSTime();

                                                     //          0          1       2             3              4              5              6
        QueryResult result = CharacterDatabase.Query("SELECT g.guildid, g.name, g.leaderguid, g.EmblemStyle, g.EmblemColor, g.BorderStyle, g.BorderColor, "
                                                     //   7                  8       9       10            11           12
                                                     "g.BackgroundColor, g.info, g.motd, g.createdate, g.BankMoney, COUNT(gbt.guildid) "
                                                     "FROM guild g LEFT JOIN guild_bank_tab gbt ON g.guildid = gbt.guildid GROUP BY g.guildid ORDER BY g.guildid ASC");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 guild definitions. DB table `guild` is empty.");
            return;
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();
                Guild* guild = new Guild();

                // 如果加载失败，删除公会对象并继续下一个
                if (!guild->LoadFromDB(fields))
                {
                    delete guild;
                    continue;
                }

                // 将公会添加到管理器
                AddGuild(guild);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} guild definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // 步骤2: 加载所有公会等级权限设置
    TC_LOG_INFO("server.loading", "Loading guild ranks...");
    {
        uint32 oldMSTime = getMSTime();

        // 删除孤立的等级记录（属于不存在公会的等级）
        CharacterDatabase.DirectExecute("DELETE gr FROM guild_rank gr LEFT JOIN guild g ON gr.guildId = g.guildId WHERE g.guildId IS NULL");

        //                                                         0    1      2       3                4
        QueryResult result = CharacterDatabase.Query("SELECT guildid, rid, rname, rights, BankMoneyPerDay FROM guild_rank ORDER BY guildid ASC, rid ASC");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 guild ranks. DB table `guild_rank` is empty.");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32 guildId = fields[0].GetUInt32();

                // 只加载属于已加载公会的等级信息
                if (Guild* guild = GetGuildById(guildId))
                    guild->LoadRankFromDB(fields);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} guild ranks in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // 步骤3: 加载所有公会成员
    TC_LOG_INFO("server.loading", "Loading guild members...");
    {
        uint32 oldMSTime = getMSTime();

        // 删除孤立的成员记录（属于不存在公会的成员）
        CharacterDatabase.DirectExecute("DELETE gm FROM guild_member gm LEFT JOIN guild g ON gm.guildId = g.guildId WHERE g.guildId IS NULL");
        CharacterDatabase.DirectExecute("DELETE gm FROM guild_member_withdraw gm LEFT JOIN guild_member g ON gm.guid = g.guid WHERE g.guid IS NULL");

                                                //           0        1         2      3      4        5       6       7       8       9       10
        QueryResult result = CharacterDatabase.Query("SELECT guildid, gm.guid, `rank` , pnote, offnote, w.tab0, w.tab1, w.tab2, w.tab3, w.tab4, w.tab5, "
                                                //    11       12      13       14       15        16      17         18
                                                     "w.money, c.name, c.level, c.class, c.gender, c.zone, c.account, c.logout_time "
                                                     "FROM guild_member gm "
                                                     "LEFT JOIN guild_member_withdraw w ON gm.guid = w.guid "
                                                     "LEFT JOIN characters c ON c.guid = gm.guid ORDER BY guildid ASC");

        if (!result)
            TC_LOG_INFO("server.loading", ">> Loaded 0 guild members. DB table `guild_member` is empty.");
        else
        {
            uint32 count = 0;

            do
            {
                Field* fields = result->Fetch();
                uint32 guildId = fields[0].GetUInt32();

                // 只加载属于已加载公会的成员信息
                if (Guild* guild = GetGuildById(guildId))
                    guild->LoadMemberFromDB(fields);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} guild members in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // 步骤4: 加载所有公会银行标签页权限
    TC_LOG_INFO("server.loading", "Loading bank tab rights...");
    {
        uint32 oldMSTime = getMSTime();

        // 删除孤立的银行权限记录（属于不存在公会的权限）
        CharacterDatabase.DirectExecute("DELETE gbr FROM guild_bank_right gbr LEFT JOIN guild g ON gbr.guildId = g.guildId WHERE g.guildId IS NULL");

                                                     //      0        1      2    3        4
        QueryResult result = CharacterDatabase.Query("SELECT guildid, TabId, rid, gbright, SlotPerDay FROM guild_bank_right ORDER BY guildid ASC, TabId ASC");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 guild bank tab rights. DB table `guild_bank_right` is empty.");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32 guildId = fields[0].GetUInt32();

                // 只加载属于已加载公会的银行权限
                if (Guild* guild = GetGuildById(guildId))
                    guild->LoadBankRightFromDB(fields);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} bank tab rights in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // 步骤5: 加载所有公会事件日志
    TC_LOG_INFO("server.loading", "Loading guild event logs...");
    {
        uint32 oldMSTime = getMSTime();

        // 删除超出配置限制的旧日志记录，避免数据积累过多
        CharacterDatabase.DirectPExecute("DELETE FROM guild_eventlog WHERE LogGuid > {}", sWorld->getIntConfig(CONFIG_GUILD_EVENT_LOG_COUNT));

                                                     //          0        1        2          3            4            5        6
        QueryResult result = CharacterDatabase.Query("SELECT guildid, LogGuid, EventType, PlayerGuid1, PlayerGuid2, NewRank, TimeStamp FROM guild_eventlog ORDER BY TimeStamp DESC, LogGuid DESC");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 guild event logs. DB table `guild_eventlog` is empty.");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32 guildId = fields[0].GetUInt32();

                // 只加载属于已加载公会的事件日志
                if (Guild* guild = GetGuildById(guildId))
                    guild->LoadEventLogFromDB(fields);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} guild event logs in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // 步骤6: 加载所有公会银行事件日志
    TC_LOG_INFO("server.loading", "Loading guild bank event logs...");
    {
        uint32 oldMSTime = getMSTime();

        // 删除超出配置限制的旧银行日志记录
        CharacterDatabase.DirectPExecute("DELETE FROM guild_bank_eventlog WHERE LogGuid > {}", sWorld->getIntConfig(CONFIG_GUILD_BANK_EVENT_LOG_COUNT));

                                                     //          0        1      2        3          4           5            6               7          8
        QueryResult result = CharacterDatabase.Query("SELECT guildid, TabId, LogGuid, EventType, PlayerGuid, ItemOrMoney, ItemStackCount, DestTabId, TimeStamp FROM guild_bank_eventlog ORDER BY TimeStamp DESC, LogGuid DESC");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 guild bank event logs. DB table `guild_bank_eventlog` is empty.");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32 guildId = fields[0].GetUInt32();

                // 只加载属于已加载公会的银行事件日志
                if (Guild* guild = GetGuildById(guildId))
                    guild->LoadBankEventLogFromDB(fields);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} guild bank event logs in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // 步骤7: 加载所有公会银行标签页
    TC_LOG_INFO("server.loading", "Loading guild bank tabs...");
    {
        uint32 oldMSTime = getMSTime();

        // 删除孤立的银行标签页记录（属于不存在公会的标签页）
        CharacterDatabase.DirectExecute("DELETE gbt FROM guild_bank_tab gbt LEFT JOIN guild g ON gbt.guildId = g.guildId WHERE g.guildId IS NULL");

                                                     //         0        1      2        3        4
        QueryResult result = CharacterDatabase.Query("SELECT guildid, TabId, TabName, TabIcon, TabText FROM guild_bank_tab ORDER BY guildid ASC, TabId ASC");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 guild bank tabs. DB table `guild_bank_tab` is empty.");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32 guildId = fields[0].GetUInt32();

                // 只加载属于已加载公会的银行标签页
                if (Guild* guild = GetGuildById(guildId))
                    guild->LoadBankTabFromDB(fields);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} guild bank tabs in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // 步骤8: 加载所有公会银行物品
    TC_LOG_INFO("guild", "Filling bank tabs with items...");
    {
        uint32 oldMSTime = getMSTime();

        // 删除孤立的银行物品记录（属于不存在公会的物品）
        CharacterDatabase.DirectExecute("DELETE gbi FROM guild_bank_item gbi LEFT JOIN guild g ON gbi.guildId = g.guildId WHERE g.guildId IS NULL");

                                                     //          0            1                2      3         4        5      6             7                 8           9           10
        QueryResult result = CharacterDatabase.Query("SELECT creatorGuid, giftCreatorGuid, count, duration, charges, flags, enchantments, randomPropertyId, durability, playedTime, text, "
                                                     //   11       12     13      14         15
                                                     "guildid, TabId, SlotId, item_guid, itemEntry FROM guild_bank_item gbi INNER JOIN item_instance ii ON gbi.item_guid = ii.guid");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 guild bank tab items. DB table `guild_bank_item` or `item_instance` is empty.");
        }
        else
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32 guildId = fields[11].GetUInt32();

                // 只加载属于已加载公会的银行物品
                if (Guild* guild = GetGuildById(guildId))
                    guild->LoadBankItemFromDB(fields);

                ++count;
            }
            while (result->NextRow());

            TC_LOG_INFO("server.loading", ">> Loaded {} guild bank tab items in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
        }
    }

    // 步骤9: 验证加载的公会数据完整性
    TC_LOG_INFO("guild", "Validating data of loaded guilds...");
    {
        uint32 oldMSTime = getMSTime();

        // 遍历所有公会，验证数据一致性
        for (GuildContainer::iterator itr = GuildStore.begin(); itr != GuildStore.end();)
        {
            Guild* guild = itr->second.get();
            ++itr;
            if (guild)
                guild->Validate();
        }

        TC_LOG_INFO("server.loading", ">> Validated data of loaded guilds in {} ms", GetMSTimeDiffToNow(oldMSTime));
    }
}

/**
 * @brief 重置所有公会的定时相关数据
 *
 * 执行每日重置操作，包括：
 * 1. 遍历所有公会，调用各公会的ResetTimes方法
 * 2. 清空公会成员提款记录表（guild_member_withdraw）
 *
 * 这会重置：
 * - 公会银行每日提款限额
 * - 公会成员每日物品提取次数
 *
 * 通常在每日服务器维护时调用（如凌晨4点）
 */
void GuildMgr::ResetTimes()
{
    // 遍历所有公会，执行各自的定时重置
    for (GuildContainer::const_iterator itr = GuildStore.begin(); itr != GuildStore.end(); ++itr)
        if (Guild* guild = itr->second.get())
            guild->ResetTimes();

    // 清空成员提款记录表，为新的一天做准备
    CharacterDatabase.DirectExecute("TRUNCATE guild_member_withdraw");
}
