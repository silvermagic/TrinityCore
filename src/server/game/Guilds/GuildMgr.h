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
 * @file GuildMgr.h
 * @brief 公会管理器模块头文件
 *
 * 本文件定义了公会管理器(GuildMgr)类，负责全局范围内的公会管理功能：
 * - 公会对象的创建、销毁和查询
 * - 公会ID的生成和管理
 * - 服务器启动时的公会数据加载
 * - 定时任务处理（如每日重置）
 *
 * 公会管理器采用单例模式，确保全局只有一个实例。
 */

#ifndef _GUILDMGR_H
#define _GUILDMGR_H

#include "Define.h"
#include "ObjectGuid.h"
#include "UniqueTrackablePtr.h"
#include <unordered_map>
#include <vector>

class Guild;

/**
 * @class GuildMgr
 * @brief 公会管理器类 - 负责管理服务器上所有公会
 *
 * 公会管理器是公会的全局管理协调中心，负责：
 * - 维护所有公会实例的存储和查询
 * - 分配唯一的公会ID
 * - 在服务器启动时从数据库加载公会数据
 * - 处理公会相关的定时任务（如每日重置）
 *
 * 使用方式：
 * 通过 sGuildMgr 宏访问单例实例，例如：sGuildMgr->GetGuildById(guildId)
 *
 * 性能注意事项：
 * - 公会查询操作使用哈希表，时间复杂度为O(1)
 * - LoadGuilds() 在服务器启动时调用，可能耗时较长
 */
class TC_GAME_API GuildMgr
{
private:
    /**
     * @brief 私有构造函数（单例模式）
     *
     * 初始化公会管理器，设置初始公会ID
     */
    GuildMgr();

    /**
     * @brief 私有析构函数
     *
     * 清理所有公会对象
     */
    ~GuildMgr();

    // 禁用拷贝构造和赋值操作（单例模式）
    GuildMgr(GuildMgr const&) = delete;
    GuildMgr& operator=(GuildMgr const&) = delete;

public:
    /**
     * @brief 获取公会管理器单例实例
     * @return 公会管理器的全局唯一实例指针
     *
     * 使用懒汉模式创建单例，线程安全
     */
    static GuildMgr* instance();

    /**
     * @brief 根据公会会长GUID获取公会
     * @param guid 公会会长的玩家GUID
     * @return 公会对象指针，如果未找到返回nullptr
     *
     * 用于验证某个玩家是否已经是某个公会的会长
     * 每个玩家最多只能是一个公会的会长
     */
    Guild* GetGuildByLeader(ObjectGuid guid) const;

    /**
     * @brief 根据公会ID获取公会
     * @param guildId 公会的低阶ID（数据库中的主键）
     * @return 公会对象指针，如果未找到返回nullptr
     *
     * 这是最常用的公会查询方法，时间复杂度O(1)
     */
    Guild* GetGuildById(ObjectGuid::LowType guildId) const;

    /**
     * @brief 根据公会名称获取公会
     * @param guildName 公会名称（不区分大小写）
     * @return 公会对象指针，如果未找到返回nullptr
     *
     * 用于创建公会时检查名称是否已存在
     * 需要遍历所有公会，性能较低，不建议频繁调用
     */
    Guild* GetGuildByName(std::string_view guildName) const;

    /**
     * @brief 根据公会ID获取公会名称
     * @param guildId 公会的低阶ID
     * @return 公会名称字符串，如果未找到返回空字符串
     *
     * 用于快速获取公会名称而不需要加载完整公会对象
     */
    std::string GetGuildNameById(ObjectGuid::LowType guildId) const;

    /**
     * @brief 从数据库加载所有公会数据
     *
     * 在服务器启动时调用，执行以下操作：
     * 1. 从数据库加载所有公会基本信息
     * 2. 加载公会成员列表
     * 3. 加载公会银行数据
     * 4. 加载公会日志和事件记录
     *
     * 这是一个重量级操作，仅在服务器启动时执行一次
     */
    void LoadGuilds();

    /**
     * @brief 向管理器添加公会
     * @param guild 要添加的公会对象指针
     *
     * 当新公会创建时调用，将公会加入全局管理列表
     * 公会对象由管理器接管，使用智能指针自动管理生命周期
     */
    void AddGuild(Guild* guild);

    /**
     * @brief 从管理器移除公会
     * @param guildId 要移除的公会ID
     *
     * 当公会解散时调用，从全局列表中移除公会
     * 注意：这会触发公会对象的析构
     */
    void RemoveGuild(ObjectGuid::LowType guildId);

    /**
     * @brief 生成新的公会ID
     * @return 新的未使用的公会ID
     *
     * 用于创建新公会时分配唯一ID
     * ID是递增的，保证唯一性
     */
    ObjectGuid::LowType GenerateGuildId();

    /**
     * @brief 设置下一个公会ID
     * @param Id 起始公会ID
     *
     * 在数据库加载完成后，设置下一个可用的公会ID
     * 确保新创建的公会ID不会与现有公会冲突
     */
    void SetNextGuildId(ObjectGuid::LowType Id) { NextGuildId = Id; }

    /**
     * @brief 重置公会定时相关数据
     *
     * 执行每日重置操作，包括：
     * - 重置公会银行每日提款限额
     * - 清理过期的日志记录
     *
     * 通常在每日服务器维护时调用
     */
    void ResetTimes();

protected:
    // 公会容器类型定义：使用哈希表存储公会，键为公会ID
    typedef std::unordered_map<ObjectGuid::LowType, Trinity::unique_trackable_ptr<Guild>> GuildContainer;

    ObjectGuid::LowType NextGuildId;     ///< 下一个可用的公会ID，用于创建新公会
    GuildContainer GuildStore;           ///< 公会存储容器，保存所有活跃公会的实例
};

#define sGuildMgr GuildMgr::instance()

#endif
