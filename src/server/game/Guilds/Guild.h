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
 * @file Guild.h
 * @brief 公会系统核心模块头文件
 *
 * 本文件定义了公会系统的核心类和数据结构，实现以下功能：
 * - 公会创建、管理和解散
 * - 公会成员管理（加入、离开、晋升、降职）
 * - 公会银行系统（物品存储、金钱管理、权限控制）
 * - 公会排名和权限系统
 * - 公会事件日志记录
 * - 公会徽章和外观定制
 *
 * 主要类说明：
 * - Guild: 公会主类，管理单个公会的所有功能
 * - Member: 公会成员类，存储成员信息和权限
 * - RankInfo: 公会排名信息类，定义排名权限
 * - BankTab: 银行标签页类，管理银行存储
 *
 * 与其他模块的关联：
 * - GuildMgr: 公会管理器，管理所有公会实例
 * - Player: 玩家类，与公会成员交互
 * - WorldSession: 世界会话，处理客户端请求
 */

#ifndef TRINITYCORE_GUILD_H
#define TRINITYCORE_GUILD_H

#include "DatabaseEnvFwd.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "SharedDefines.h"
#include "UniqueTrackablePtr.h"
#include <set>
#include <unordered_map>
#include <unordered_set>

class Item;
class Player;
class WorldPacket;
class WorldSession;
struct ItemPosCount;
enum InventoryResult : uint8;
enum LocaleConstant : uint8;

namespace WorldPackets
{
    namespace Guild
    {
        class GuildBankLogQueryResults;
        class GuildEventLogQueryResults;
        class SaveGuildEmblem;
    }
}

// 公会相关常量
enum GuildMisc
{
    GUILD_BANK_MAX_TABS                 = 6,                    // 银行最大标签页数量（客户端也发送此值用于金钱日志）
    GUILD_BANK_MAX_SLOTS                = 98,                   // 每个标签页的最大槽位数量
    GUILD_BANK_MONEY_LOGS_TAB           = 100,                  // 数据库中用于金钱日志的标签页ID
    GUILD_RANKS_MIN_COUNT               = 5,                    // 最小排名数量
    GUILD_RANKS_MAX_COUNT               = 10,                   // 最大排名数量
    GUILD_RANK_NONE                     = 0xFF,                 // 无效排名标记
    GUILD_WITHDRAW_MONEY_UNLIMITED      = 0xFFFFFFFF,           // 无限金钱提取权限
    GUILD_WITHDRAW_SLOT_UNLIMITED       = 0xFFFFFFFF,           // 无限槽位提取权限
    GUILD_EVENT_LOG_GUID_UNDEFINED      = 0xFFFFFFFF,           // 未定义的事件日志GUID
    TAB_UNDEFINED                       = 0xFF,                 // 未定义的标签页ID
};

// 公会银行金钱上限
constexpr uint64 GUILD_BANK_MONEY_LIMIT = UI64LIT(0x7FFFFFFFFFFFF);

// 公会成员数据类型枚举
enum GuildMemberData
{
    GUILD_MEMBER_DATA_ZONEID,     // 区域ID
    GUILD_MEMBER_DATA_LEVEL,      // 等级
};

// 公会默认排名枚举
enum GuildDefaultRanks
{
    // 这些排名可以修改，但不能删除
    GR_GUILDMASTER  = 0,          // 公会会长
    GR_OFFICER      = 1,          // 官员
    GR_VETERAN      = 2,          // 资深成员
    GR_MEMBER       = 3,          // 普通成员
    GR_INITIATE     = 4           // 新手成员
    // 晋升成员时服务器执行：rank--
    // 降职成员时服务器执行：rank++
};

// 公会排名权限枚举
enum GuildRankRights
{
    GR_RIGHT_EMPTY                      = 0x00000040,  // 空权限（基础标志）
    GR_RIGHT_GCHATLISTEN                = GR_RIGHT_EMPTY | 0x00000001,  // 监听公会聊天
    GR_RIGHT_GCHATSPEAK                 = GR_RIGHT_EMPTY | 0x00000002,  // 公会聊天发言
    GR_RIGHT_OFFCHATLISTEN              = GR_RIGHT_EMPTY | 0x00000004,  // 监听官员聊天
    GR_RIGHT_OFFCHATSPEAK               = GR_RIGHT_EMPTY | 0x00000008,  // 官员聊天发言
    GR_RIGHT_INVITE                     = GR_RIGHT_EMPTY | 0x00000010,  // 邀请成员
    GR_RIGHT_REMOVE                     = GR_RIGHT_EMPTY | 0x00000020,  // 移除成员
    GR_RIGHT_PROMOTE                    = GR_RIGHT_EMPTY | 0x00000080,  // 晋升成员
    GR_RIGHT_DEMOTE                     = GR_RIGHT_EMPTY | 0x00000100,  // 降职成员
    GR_RIGHT_SETMOTD                    = GR_RIGHT_EMPTY | 0x00001000,  // 设置每日消息
    GR_RIGHT_EPNOTE                     = GR_RIGHT_EMPTY | 0x00002000,  // 编辑公开备注
    GR_RIGHT_VIEWOFFNOTE                = GR_RIGHT_EMPTY | 0x00004000,  // 查看官员备注
    GR_RIGHT_EOFFNOTE                   = GR_RIGHT_EMPTY | 0x00008000,  // 编辑官员备注
    GR_RIGHT_MODIFY_GUILD_INFO          = GR_RIGHT_EMPTY | 0x00010000,  // 修改公会信息
    GR_RIGHT_WITHDRAW_GOLD_LOCK         = 0x00020000,                   // 锁定金钱提取（移除金钱提取能力）
    GR_RIGHT_WITHDRAW_REPAIR            = 0x00040000,                   // 提取金钱用于修理
    GR_RIGHT_WITHDRAW_GOLD              = 0x00080000,                   // 提取金钱
    GR_RIGHT_CREATE_GUILD_EVENT         = 0x00100000,                   // 创建公会事件（巫妖王之怒）
    GR_RIGHT_ALL                        = 0x001DF1FF                     // 所有权限
};

// 公会命令类型枚举
enum GuildCommandType
{
    GUILD_COMMAND_CREATE                = 0,    // 创建公会
    GUILD_COMMAND_INVITE                = 1,    // 邀请成员
    GUILD_COMMAND_QUIT                  = 3,    // 退出公会
    GUILD_COMMAND_ROSTER                = 5,    // 查询名册
    GUILD_COMMAND_PROMOTE               = 6,    // 晋升成员
    GUILD_COMMAND_DEMOTE                = 7,    // 降职成员
    GUILD_COMMAND_REMOVE                = 8,    // 移除成员
    GUILD_COMMAND_CHANGE_LEADER         = 10,   // 更换会长
    GUILD_COMMAND_EDIT_MOTD             = 11,   // 编辑每日消息
    GUILD_COMMAND_GUILD_CHAT            = 13,   // 公会聊天
    GUILD_COMMAND_FOUNDER               = 14,   // 创始人命令
    GUILD_COMMAND_CHANGE_RANK           = 16,   // 更改排名
    GUILD_COMMAND_PUBLIC_NOTE           = 19,   // 公开备注
    GUILD_COMMAND_VIEW_TAB              = 21,   // 查看银行标签页
    GUILD_COMMAND_MOVE_ITEM             = 22,   // 移动物品
    GUILD_COMMAND_REPAIR                = 25,   // 修理装备
};

// 公会命令错误码枚举
enum GuildCommandError
{
    ERR_GUILD_COMMAND_SUCCESS           = 0,    // 成功
    ERR_GUILD_INTERNAL                  = 1,    // 内部错误
    ERR_ALREADY_IN_GUILD                = 2,    // 已经在公会中
    ERR_ALREADY_IN_GUILD_S              = 3,    // 已经在公会中（带名称）
    ERR_INVITED_TO_GUILD                = 4,    // 已被邀请加入公会
    ERR_ALREADY_INVITED_TO_GUILD_S      = 5,    // 已被邀请加入公会（带名称）
    ERR_GUILD_NAME_INVALID              = 6,    // 公会名称无效
    ERR_GUILD_NAME_EXISTS_S             = 7,    // 公会名称已存在
    ERR_GUILD_LEADER_LEAVE              = 8,    // 会长不能离开公会
    ERR_GUILD_PERMISSIONS               = 8,    // 权限不足
    ERR_GUILD_PLAYER_NOT_IN_GUILD       = 9,    // 玩家不在公会中
    ERR_GUILD_PLAYER_NOT_IN_GUILD_S     = 10,   // 玩家不在公会中（带名称）
    ERR_GUILD_PLAYER_NOT_FOUND_S        = 11,   // 找不到玩家
    ERR_GUILD_NOT_ALLIED                = 12,   // 不是联盟
    ERR_GUILD_RANK_TOO_HIGH_S           = 13,   // 排名过高
    ERR_GUILD_RANK_TOO_LOW_S            = 14,   // 排名过低
    ERR_GUILD_RANKS_LOCKED              = 17,   // 排名已锁定
    ERR_GUILD_RANK_IN_USE               = 18,   // 排名正在使用中
    ERR_GUILD_IGNORING_YOU_S            = 19,   // 玩家正在屏蔽你
    ERR_GUILD_UNK1                      = 20,   // 强制更新名册
    ERR_GUILD_WITHDRAW_LIMIT            = 25,   // 提取限制
    ERR_GUILD_NOT_ENOUGH_MONEY          = 26,   // 金钱不足
    ERR_GUILD_BANK_FULL                 = 28,   // 银行已满
    ERR_GUILD_ITEM_NOT_FOUND            = 29,   // 找不到物品
};

// 公会事件类型枚举
enum GuildEvents
{
    GE_PROMOTION                        = 0,    // 晋升
    GE_DEMOTION                         = 1,    // 降职
    GE_MOTD                             = 2,    // 每日消息
    GE_JOINED                           = 3,    // 加入公会
    GE_LEFT                             = 4,    // 离开公会
    GE_REMOVED                          = 5,    // 被移除
    GE_LEADER_IS                        = 6,    // 会长是
    GE_LEADER_CHANGED                   = 7,    // 会长变更
    GE_DISBANDED                        = 8,    // 公会解散
    GE_TABARDCHANGE                     = 9,    // 徽章变更
    GE_RANK_UPDATED                     = 10,   // 排名更新
    GE_RANK_DELETED                     = 11,   // 排名删除
    GE_SIGNED_ON                        = 12,   // 成员上线
    GE_SIGNED_OFF                       = 13,   // 成员下线
    GE_GUILDBANKBAGSLOTS_CHANGED        = 14,   // 银行物品槽位变更（TODO: 当银行中移动物品时发送 - 所有打开银行的玩家将发送标签页查询）
    GE_BANK_TAB_PURCHASED               = 15,   // 购买银行标签页
    GE_BANK_TAB_UPDATED                 = 16,   // 银行标签页更新
    GE_BANK_MONEY_SET                   = 17,   // 设置银行金钱
    GE_BANK_TAB_AND_MONEY_UPDATED       = 18,   // 银行标签页和金钱更新
    GE_BANK_TEXT_CHANGED                = 19,   // 银行文本变更
};

// 公会银行权限枚举
enum GuildBankRights
{
    GUILD_BANK_RIGHT_VIEW_TAB           = 0x01,  // 查看标签页
    GUILD_BANK_RIGHT_PUT_ITEM           = 0x02,  // 放入物品
    GUILD_BANK_RIGHT_UPDATE_TEXT        = 0x04,  // 更新文本

    GUILD_BANK_RIGHT_DEPOSIT_ITEM       = GUILD_BANK_RIGHT_VIEW_TAB | GUILD_BANK_RIGHT_PUT_ITEM,  // 存入物品权限
    GUILD_BANK_RIGHT_FULL               = 0xFF   // 完全权限
};

// 公会银行事件日志类型枚举
enum GuildBankEventLogTypes
{
    GUILD_BANK_LOG_DEPOSIT_ITEM         = 1,    // 存入物品
    GUILD_BANK_LOG_WITHDRAW_ITEM        = 2,    // 提取物品
    GUILD_BANK_LOG_MOVE_ITEM            = 3,    // 移动物品
    GUILD_BANK_LOG_DEPOSIT_MONEY        = 4,    // 存入金钱
    GUILD_BANK_LOG_WITHDRAW_MONEY       = 5,    // 提取金钱
    GUILD_BANK_LOG_REPAIR_MONEY         = 6,    // 修理花费
    GUILD_BANK_LOG_MOVE_ITEM2           = 7,    // 移动物品2
    GUILD_BANK_LOG_UNK1                 = 8,    // 未知类型1
    GUILD_BANK_LOG_BUY_SLOT             = 9     // 购买槽位
};

// 公会事件日志类型枚举
enum GuildEventLogTypes
{
    GUILD_EVENT_LOG_INVITE_PLAYER       = 1,    // 邀请玩家
    GUILD_EVENT_LOG_JOIN_GUILD          = 2,    // 加入公会
    GUILD_EVENT_LOG_PROMOTE_PLAYER      = 3,    // 晋升玩家
    GUILD_EVENT_LOG_DEMOTE_PLAYER       = 4,    // 降职玩家
    GUILD_EVENT_LOG_UNINVITE_PLAYER     = 5,    // 移除邀请
    GUILD_EVENT_LOG_LEAVE_GUILD         = 6     // 离开公会
};

// 公会徽章错误码枚举
enum GuildEmblemError
{
    ERR_GUILDEMBLEM_SUCCESS               = 0,  // 成功
    ERR_GUILDEMBLEM_INVALID_TABARD_COLORS = 1,  // 无效的徽章颜色
    ERR_GUILDEMBLEM_NOGUILD               = 2,  // 没有公会
    ERR_GUILDEMBLEM_NOTGUILDMASTER        = 3,  // 不是公会会长
    ERR_GUILDEMBLEM_NOTENOUGHMONEY        = 4,  // 金钱不足
    ERR_GUILDEMBLEM_INVALIDVENDOR         = 5   // 无效的商人
};

// 公会成员状态标志枚举
enum GuildMemberFlags
{
    GUILDMEMBER_STATUS_NONE             = 0x0000,  // 无状态
    GUILDMEMBER_STATUS_ONLINE           = 0x0001,  // 在线
    GUILDMEMBER_STATUS_AFK              = 0x0002,  // 离开（AFK）
    GUILDMEMBER_STATUS_DND              = 0x0004,  // 请勿打扰（DND）
    GUILDMEMBER_STATUS_MOBILE           = 0x0008,  // 移动端（来自移动应用的远程聊天）
};

/**
 * @class EmblemInfo
 * @brief 公会徽章信息类
 *
 * 用于存储和管理公会徽章的各种视觉属性
 * 徽章显示在公会战袍和公会旗帜上
 *
 * 主要功能：
 * - 存储徽章的样式、颜色等视觉属性
 * - 支持从数据库加载和保存徽章信息
 * - 支持从客户端数据包读取徽章设置
 */
class TC_GAME_API EmblemInfo
{
    public:
        /**
         * @brief 默认构造函数，初始化所有徽章属性为0
         */
        EmblemInfo() : m_style(0), m_color(0), m_borderStyle(0), m_borderColor(0), m_backgroundColor(0) { }

        /**
         * @brief 从数据库字段加载徽章信息
         * @param fields 数据库查询结果字段数组
         *
         * 从guild表的相关字段加载徽章属性
         */
        void LoadFromDB(Field* fields);

        /**
         * @brief 保存徽章信息到数据库
         * @param guildId 公会ID
         *
         * 更新guild表中该公会的徽章信息
         */
        void SaveToDB(ObjectGuid::LowType guildId) const;

        /**
         * @brief 从客户端数据包读取徽章信息
         * @param packet 客户端发送的保存徽章数据包
         *
         * 解析客户端发送的徽章设置请求
         */
        void ReadPacket(WorldPackets::Guild::SaveGuildEmblem& packet);

        // 获取徽章样式ID
        uint32 GetStyle() const { return m_style; }
        // 获取徽章颜色
        uint32 GetColor() const { return m_color; }
        // 获取边框样式ID
        uint32 GetBorderStyle() const { return m_borderStyle; }
        // 获取边框颜色
        uint32 GetBorderColor() const { return m_borderColor; }
        // 获取背景颜色
        uint32 GetBackgroundColor() const { return m_backgroundColor; }

    private:
        uint32 m_style;              // 徽章样式ID
        uint32 m_color;              // 徽章颜色
        uint32 m_borderStyle;        // 边框样式ID
        uint32 m_borderColor;        // 边框颜色
        uint32 m_backgroundColor;    // 背景颜色
};

/**
 * @class GuildBankRightsAndSlots
 * @brief 公会银行权限和槽位数据结构
 *
 * 用于存储特定标签页的访问权限和每日可提取槽位数量
 * 每个排名对每个银行标签页都有独立的权限设置
 *
 * 权限包括：
 * - 查看标签页权限
 * - 存入物品权限
 * - 提取物品权限
 * - 每日可提取槽位数量限制
 */
class GuildBankRightsAndSlots
{
    public:
        /**
         * @brief 默认构造函数
         *
         * 初始化为未定义标签页，无权限，无槽位
         */
        GuildBankRightsAndSlots() : tabId(TAB_UNDEFINED), rights(0), slots(0) { }

        /**
         * @brief 指定标签页ID的构造函数
         * @param _tabId 标签页ID
         */
        GuildBankRightsAndSlots(uint8 _tabId) : tabId(_tabId), rights(0), slots(0) { }

        /**
         * @brief 完整构造函数
         * @param _tabId 标签页ID
         * @param _rights 访问权限标志
         * @param _slots 每日可提取槽位数
         */
        GuildBankRightsAndSlots(uint8 _tabId, uint8 _rights, uint32 _slots) : tabId(_tabId), rights(_rights), slots(_slots) { }

        /**
         * @brief 设置公会会长权限值
         *
         * 公会会长拥有完全权限和无限提取
         * 用于初始化会长排名的银行权限
         */
        void SetGuildMasterValues()
        {
            rights = GUILD_BANK_RIGHT_FULL;
            slots = uint32(GUILD_WITHDRAW_SLOT_UNLIMITED);
        }

        // 设置标签页ID
        void SetTabId(uint8 _tabId) { tabId = _tabId; }
        // 设置每日可提取槽位数
        void SetSlots(uint32 _slots) { slots = _slots; }
        // 设置访问权限
        void SetRights(uint8 _rights) { rights = _rights; }

        // 获取标签页ID
        int8 GetTabId() const { return tabId; }
        // 获取每日可提取槽位数
        int32 GetSlots() const { return slots; }
        // 获取访问权限
        int8 GetRights() const { return rights; }

    private:
        uint8 tabId;     // 标签页ID
        uint8 rights;    // 访问权限标志
        uint32 slots;    // 每日可提取的槽位数量
};

using SlotIds = std::set<uint8>;

/**
 * @class Guild
 * @brief 公会类 - 管理单个公会的所有功能
 *
 * 公会是游戏中的社交组织，提供以下核心功能：
 * - 成员管理：加入、退出、晋升、降职、踢出成员
 * - 权限系统：基于排名的分层权限管理
 * - 银行系统：共享物品存储和金钱管理
 * - 事件日志：记录公会重要事件
 * - 外观定制：公会徽章和战袍设计
 *
 * 生命周期：
 * 1. 创建：玩家购买公会契约并注册
 * 2. 运营：成员管理、银行使用、事件组织
 * 3. 解散：会长主动解散或所有成员离开
 *
 * 线程安全：
 * - 公会对象在主线程操作
 * - 数据库操作使用事务保证一致性
 *
 * 性能注意事项：
 * - 成员查询使用哈希表，时间复杂度O(1)
 * - 银行物品操作需要数据库事务
 * - 事件日志有最大记录数限制，避免无限增长
 */
class TC_GAME_API Guild
{
    private:
        /**
         * @class Member
         * @brief 公会成员类 - 表示公会中的单个成员
         *
         * 存储成员的基本信息、权限、银行使用记录等
         * 每个公会成员对应一个Member实例
         *
         * 主要职责：
         * - 存储成员角色数据（名称、等级、职业等）
         * - 管理成员排名和权限
         * - 记录银行使用情况（提取次数、金钱限额）
         * - 追踪成员在线状态
         */
        class Member
        {
            public:
                /**
                 * @brief 构造函数
                 * @param guildId 公会ID
                 * @param guid 成员的玩家GUID
                 * @param rankId 成员的排名ID
                 */
                Member(ObjectGuid::LowType guildId, ObjectGuid guid, uint8 rankId);

                /**
                 * @brief 从玩家对象设置成员统计数据
                 * @param player 玩家对象指针
                 *
                 * 从在线玩家的数据更新成员信息
                 * 包括名称、等级、职业、性别、区域等
                 */
                void SetStats(Player* player);

                /**
                 * @brief 直接设置成员统计数据
                 * @param name 角色名称
                 * @param level 等级
                 * @param _class 职业枚举
                 * @param gender 性别
                 * @param zoneId 区域ID
                 * @param accountId 账号ID
                 *
                 * 用于从数据库加载成员数据时设置统计信息
                 */
                void SetStats(std::string_view name, uint8 level, uint8 _class, uint8 gender, uint32 zoneId, uint32 accountId);

                /**
                 * @brief 检查统计数据是否有效
                 * @return 如果统计数据有效返回true
                 *
                 * 验证名称、等级、职业等数据是否合法
                 */
                bool CheckStats() const;

                /**
                 * @brief 设置公开备注
                 * @param publicNote 公开备注内容
                 *
                 * 所有公会成员都可以看到的备注信息
                 */
                void SetPublicNote(std::string_view publicNote);

                /**
                 * @brief 设置官员备注
                 * @param officerNote 官员备注内容
                 *
                 * 只有官员和会长可以查看的备注信息
                 */
                void SetOfficerNote(std::string_view officerNote);

                // 设置所在区域ID
                void SetZoneID(uint32 id) { m_zoneId = id; }
                // 设置等级
                void SetLevel(uint8 var) { m_level = var; }

                // 添加状态标志
                void AddFlag(uint8 var) { m_flags |= var; }
                // 移除状态标志
                void RemFlag(uint8 var) { m_flags &= ~var; }
                // 重置所有状态标志
                void ResetFlags() { m_flags = GUILDMEMBER_STATUS_NONE; }

                /**
                 * @brief 从数据库加载成员数据
                 * @param fields 数据库查询结果字段数组
                 * @return 加载成功返回true
                 */
                bool LoadFromDB(Field* fields);

                /**
                 * @brief 保存成员数据到数据库
                 * @param trans 数据库事务对象
                 */
                void SaveToDB(CharacterDatabaseTransaction trans) const;

                // 获取成员GUID
                ObjectGuid GetGUID() const { return m_guid; }
                // 获取成员名称
                std::string const& GetName() const { return m_name; }
                // 获取账号ID
                uint32 GetAccountId() const { return m_accountId; }
                // 获取公会排名ID
                uint8 GetRankId() const { return m_rankId; }
                // 获取登出时间
                uint64 GetLogoutTime() const { return m_logoutTime; }
                // 获取公开备注
                std::string GetPublicNote() const { return m_publicNote; }
                // 获取官员备注
                std::string GetOfficerNote() const { return m_officerNote; }
                // 获取职业
                uint8 GetClass() const { return m_class; }
                // 获取等级
                uint8 GetLevel() const { return m_level; }
                // 获取性别
                uint8 GetGender() const { return m_gender; }
                // 获取状态标志
                uint8 GetFlags() const { return m_flags; }
                // 获取所在区域ID
                uint32 GetZoneId() const { return m_zoneId; }
                // 检查是否在线
                bool IsOnline() const { return (m_flags & GUILDMEMBER_STATUS_ONLINE); }

                /**
                 * @brief 更改成员排名
                 * @param trans 数据库事务对象
                 * @param newRank 新排名ID
                 *
                 * 更新成员排名并保存到数据库
                 * 会触发权限更新
                 */
                void ChangeRank(CharacterDatabaseTransaction trans, uint8 newRank);

                /**
                 * @brief 更新登出时间为当前时间
                 *
                 * 在玩家登出时调用，记录最后在线时间
                 */
                inline void UpdateLogoutTime();

                // 检查是否为指定排名
                inline bool IsRank(uint8 rankId) const { return m_rankId == rankId; }
                // 检查排名是否不低于指定排名（数值越小等级越高）
                inline bool IsRankNotLower(uint8 rankId) const { return m_rankId <= rankId; }
                // 检查是否为同一玩家
                inline bool IsSamePlayer(ObjectGuid guid) const { return m_guid == guid; }

                /**
                 * @brief 更新银行提取值
                 * @param trans 数据库事务对象
                 * @param tabId 银行标签页ID（使用GUILD_BANK_MAX_TABS表示金钱）
                 * @param amount 提取数量（增加已使用额度）
                 *
                 * 记录成员从银行提取物品或金钱的次数
                 */
                void UpdateBankWithdrawValue(CharacterDatabaseTransaction trans, uint8 tabId, uint32 amount);

                /**
                 * @brief 获取指定标签页的银行提取值
                 * @param tabId 银行标签页ID
                 * @return 已使用的提取额度
                 */
                int32 GetBankWithdrawValue(uint8 tabId) const;

                /**
                 * @brief 重置所有提取值为初始状态
                 *
                 * 每日重置时调用，清空当日提取记录
                 */
                void ResetValues();

                /**
                 * @brief 查找在线玩家对象
                 * @return 如果玩家在线返回玩家指针，否则返回nullptr
                 */
                Player* FindPlayer() const;

                /**
                 * @brief 查找已连接的玩家对象（包括跨服）
                 * @return 如果玩家已连接返回玩家指针，否则返回nullptr
                 */
                Player* FindConnectedPlayer() const;

            private:
                ObjectGuid::LowType m_guildId;      // 公会ID
                // 来自角色表的数据字段
                ObjectGuid m_guid;                   // 角色GUID
                std::string m_name;                  // 角色名称
                uint32 m_zoneId;                     // 所在区域ID
                uint8 m_level;                       // 角色等级
                uint8 m_class;                       // 角色职业
                uint8 m_gender;                      // 角色性别
                uint8 m_flags;                       // 状态标志（在线、AFK、DND等）
                uint64 m_logoutTime;                 // 登出时间戳
                uint32 m_accountId;                  // 账号ID
                // 来自公会成员表的数据字段
                uint8 m_rankId;                      // 公会排名ID
                std::string m_publicNote;            // 公开备注
                std::string m_officerNote;           // 官员备注

                // 银行提取记录，记录每个标签页的提取次数（最后一个元素用于金钱）
                std::array<int32, GUILD_BANK_MAX_TABS + 1> m_bankWithdraw = {};
        };

        /**
         * @class LogEntry
         * @brief 日志条目基类
         *
         * 所有公会日志条目的基类，提供通用的ID和时间戳管理
         * 派生类包括EventLogEntry（公会事件日志）和BankEventLogEntry（银行事件日志）
         *
         * 主要功能：
         * - 提供日志条目的唯一标识
         * - 记录事件发生的时间戳
         * - 定义保存到数据库的接口
         */
        class LogEntry
        {
            public:
                /**
                 * @brief 构造函数（使用当前时间戳）
                 * @param guildId 公会ID
                 * @param guid 日志条目GUID
                 */
                LogEntry(ObjectGuid::LowType guildId, uint32 guid);

                /**
                 * @brief 构造函数（指定时间戳）
                 * @param guildId 公会ID
                 * @param guid 日志条目GUID
                 * @param timestamp 事件发生时间
                 *
                 * 用于从数据库加载历史日志
                 */
                LogEntry(ObjectGuid::LowType guildId, uint32 guid, time_t timestamp) : m_guildId(guildId), m_guid(guid), m_timestamp(timestamp) { }

                // 虚析构函数
                virtual ~LogEntry() { }

                // 获取日志条目GUID
                uint32 GetGUID() const { return m_guid; }
                // 获取时间戳
                uint64 GetTimestamp() const { return m_timestamp; }

                /**
                 * @brief 保存日志到数据库
                 * @param trans 数据库事务对象
                 *
                 * 纯虚函数，由派生类实现具体的保存逻辑
                 */
                virtual void SaveToDB(CharacterDatabaseTransaction trans) const = 0;

            protected:
                ObjectGuid::LowType m_guildId;   // 公会ID
                uint32 m_guid;                    // 日志条目GUID
                uint64 m_timestamp;               // 时间戳
        };

        /**
         * @class EventLogEntry
         * @brief 公会事件日志条目类
         *
         * 记录公会相关事件，如成员加入、离开、晋升、降职等
         * 这些日志可以在公会界面中查看，帮助成员了解公会活动历史
         *
         * 支持的事件类型（GuildEventLogTypes）：
         * - 邀请玩家（GUILD_EVENT_LOG_INVITE_PLAYER）
         * - 加入公会（GUILD_EVENT_LOG_JOIN_GUILD）
         * - 晋升玩家（GUILD_EVENT_LOG_PROMOTE_PLAYER）
         * - 降职玩家（GUILD_EVENT_LOG_DEMOTE_PLAYER）
         * - 移除邀请（GUILD_EVENT_LOG_UNINVITE_PLAYER）
         * - 离开公会（GUILD_EVENT_LOG_LEAVE_GUILD）
         */
        class EventLogEntry : public LogEntry
        {
            public:
                /**
                 * @brief 构造函数（使用当前时间戳）
                 * @param guildId 公会ID
                 * @param guid 日志条目GUID
                 * @param eventType 事件类型
                 * @param playerGuid1 玩家1的GUID（发起者）
                 * @param playerGuid2 玩家2的GUID（目标）
                 * @param newRank 新排名（用于晋升/降职事件）
                 */
                EventLogEntry(ObjectGuid::LowType guildId, uint32 guid, GuildEventLogTypes eventType, ObjectGuid::LowType playerGuid1, ObjectGuid::LowType playerGuid2, uint8 newRank) :
                    LogEntry(guildId, guid), m_eventType(eventType), m_playerGuid1(playerGuid1), m_playerGuid2(playerGuid2), m_newRank(newRank) { }

                /**
                 * @brief 构造函数（指定时间戳，用于从数据库加载）
                 * @param guildId 公会ID
                 * @param guid 日志条目GUID
                 * @param timestamp 事件发生时间
                 * @param eventType 事件类型
                 * @param playerGuid1 玩家1的GUID（发起者）
                 * @param playerGuid2 玩家2的GUID（目标）
                 * @param newRank 新排名（用于晋升/降职事件）
                 */
                EventLogEntry(ObjectGuid::LowType guildId, uint32 guid, time_t timestamp, GuildEventLogTypes eventType, ObjectGuid::LowType playerGuid1, ObjectGuid::LowType playerGuid2, uint8 newRank) :
                    LogEntry(guildId, guid, timestamp), m_eventType(eventType), m_playerGuid1(playerGuid1), m_playerGuid2(playerGuid2), m_newRank(newRank) { }

                ~EventLogEntry() { }

                /**
                 * @brief 保存事件日志到数据库
                 * @param trans 数据库事务对象
                 *
                 * 将事件日志保存到guild_eventlog表
                 */
                void SaveToDB(CharacterDatabaseTransaction trans) const override;

                /**
                 * @brief 将事件日志写入数据包发送给客户端
                 * @param packet 公会事件日志查询结果数据包
                 *
                 * 将事件日志序列化到数据包中，发送给请求的客户端
                 */
                void WritePacket(WorldPackets::Guild::GuildEventLogQueryResults& packet) const;

            private:
                GuildEventLogTypes m_eventType;       // 事件类型
                ObjectGuid::LowType m_playerGuid1;    // 玩家1的GUID（发起者）
                ObjectGuid::LowType m_playerGuid2;    // 玩家2的GUID（目标）
                uint8 m_newRank;                      // 新排名（用于晋升/降职事件）
        };

        /**
         * @class BankEventLogEntry
         * @brief 银行事件日志条目类
         *
         * 记录公会银行相关操作，如物品存取、金钱存取、修理等
         * 每个银行标签页都有独立的日志记录
         *
         * 支持的事件类型（GuildBankEventLogTypes）：
         * - 存入物品（GUILD_BANK_LOG_DEPOSIT_ITEM）
         * - 提取物品（GUILD_BANK_LOG_WITHDRAW_ITEM）
         * - 移动物品（GUILD_BANK_LOG_MOVE_ITEM）
         * - 存入金钱（GUILD_BANK_LOG_DEPOSIT_MONEY）
         * - 提取金钱（GUILD_BANK_LOG_WITHDRAW_MONEY）
         * - 修理花费（GUILD_BANK_LOG_REPAIR_MONEY）
         * - 购买槽位（GUILD_BANK_LOG_BUY_SLOT）
         */
        class BankEventLogEntry : public LogEntry
        {
            public:
                /**
                 * @brief 静态方法：检查是否为金钱相关事件
                 * @param eventType 银行事件类型
                 * @return 如果是金钱相关事件返回true
                 */
                static bool IsMoneyEvent(GuildBankEventLogTypes eventType)
                {
                    return
                        eventType == GUILD_BANK_LOG_DEPOSIT_MONEY ||
                        eventType == GUILD_BANK_LOG_WITHDRAW_MONEY ||
                        eventType == GUILD_BANK_LOG_REPAIR_MONEY;
                }

                /**
                 * @brief 检查当前事件是否为金钱相关事件
                 * @return 如果是金钱相关事件返回true
                 */
                bool IsMoneyEvent() const
                {
                    return IsMoneyEvent(m_eventType);
                }

                /**
                 * @brief 构造函数（使用当前时间戳）
                 * @param guildId 公会ID
                 * @param guid 日志条目GUID
                 * @param eventType 银行事件类型
                 * @param tabId 银行标签页ID
                 * @param playerGuid 操作玩家GUID
                 * @param itemOrMoney 物品ID或金钱数量
                 * @param itemStackCount 物品堆叠数量
                 * @param destTabId 目标标签页ID（用于移动物品）
                 */
                BankEventLogEntry(ObjectGuid::LowType guildId, uint32 guid, GuildBankEventLogTypes eventType, uint8 tabId, ObjectGuid::LowType playerGuid, uint32 itemOrMoney, uint16 itemStackCount, uint8 destTabId) :
                    LogEntry(guildId, guid), m_eventType(eventType), m_bankTabId(tabId), m_playerGuid(playerGuid),
                    m_itemOrMoney(itemOrMoney), m_itemStackCount(itemStackCount), m_destTabId(destTabId) { }

                /**
                 * @brief 构造函数（指定时间戳，用于从数据库加载）
                 * @param guildId 公会ID
                 * @param guid 日志条目GUID
                 * @param timestamp 事件发生时间
                 * @param tabId 银行标签页ID
                 * @param eventType 银行事件类型
                 * @param playerGuid 操作玩家GUID
                 * @param itemOrMoney 物品ID或金钱数量
                 * @param itemStackCount 物品堆叠数量
                 * @param destTabId 目标标签页ID（用于移动物品）
                 */
                BankEventLogEntry(ObjectGuid::LowType guildId, uint32 guid, time_t timestamp, uint8 tabId, GuildBankEventLogTypes eventType, ObjectGuid::LowType playerGuid, uint32 itemOrMoney, uint16 itemStackCount, uint8 destTabId) :
                    LogEntry(guildId, guid, timestamp), m_eventType(eventType), m_bankTabId(tabId), m_playerGuid(playerGuid),
                    m_itemOrMoney(itemOrMoney), m_itemStackCount(itemStackCount), m_destTabId(destTabId) { }

                ~BankEventLogEntry() { }

                /**
                 * @brief 保存银行事件日志到数据库
                 * @param trans 数据库事务对象
                 *
                 * 将银行事件日志保存到guild_bank_eventlog表
                 */
                void SaveToDB(CharacterDatabaseTransaction trans) const override;

                /**
                 * @brief 将银行事件日志写入数据包发送给客户端
                 * @param packet 公会银行日志查询结果数据包
                 *
                 * 将银行事件日志序列化到数据包中，发送给请求的客户端
                 */
                void WritePacket(WorldPackets::Guild::GuildBankLogQueryResults& packet) const;

            private:
                GuildBankEventLogTypes m_eventType;     // 银行事件类型
                uint8  m_bankTabId;                     // 银行标签页ID
                ObjectGuid::LowType m_playerGuid;       // 操作玩家GUID
                uint32 m_itemOrMoney;                   // 物品ID或金钱数量
                uint16 m_itemStackCount;                // 物品堆叠数量
                uint8  m_destTabId;                     // 目标标签页ID（用于移动物品）
        };

        /**
         * @class LogHolder
         * @brief 日志容器模板类
         *
         * 用于管理日志条目的集合，支持添加、加载和遍历日志
         * 是一个模板类，可以存储EventLogEntry或BankEventLogEntry
         *
         * 主要功能：
         * - 限制日志最大数量，避免无限增长
         * - 自动分配日志条目GUID
         * - 支持从数据库批量加载日志
         * - 自动保存新日志到数据库
         *
         * 性能注意事项：
         * - 使用std::list存储日志，便于插入和删除
         * - 最旧的日志在列表前端，新日志在末尾
         */
        template <typename Entry>
        class LogHolder
        {
            public:
                LogHolder();

                /**
                 * @brief 检查是否可以添加新的日志条目
                 * @return 如果未达到最大记录数返回true
                 */
                bool CanInsert() const { return m_log.size() < m_maxRecords; }

                /**
                 * @brief 从数据库加载事件日志到集合
                 * @param args 传递给日志条目构造函数的参数
                 *
                 * 模板方法，支持变参，用于从数据库字段构造日志条目
                 */
                template <typename... Ts>
                void LoadEvent(Ts&&... args);

                /**
                 * @brief 添加新事件到集合并保存到数据库
                 * @param trans 数据库事务对象
                 * @param args 传递给日志条目构造函数的参数
                 *
                 * 创建新的日志条目，添加到集合，并保存到数据库
                 */
                template <typename... Ts>
                void AddEvent(CharacterDatabaseTransaction trans, Ts&&... args);

                // 获取下一个可用的GUID
                uint32 GetNextGUID();
                // 获取日志列表（可修改）
                std::list<Entry>& GetGuildLog() { return m_log; }
                // 获取日志列表（只读）
                std::list<Entry> const& GetGuildLog() const { return m_log; }

            private:
                std::list<Entry> m_log;            // 日志条目列表
                uint32 const m_maxRecords;          // 最大记录数
                uint32 m_nextGUID;                  // 下一个可用的GUID
        };

        /**
         * @class RankInfo
         * @brief 公会排名信息类
         *
         * 存储单个公会排名的权限和设置信息
         * 每个公会可以定义多个排名（如会长、官员、成员等）
         * 每个排名有独立的权限设置
         *
         * 权限包括：
         * - 公会聊天权限（监听、发言）
         * - 官员聊天权限
         * - 成员管理权限（邀请、移除、晋升、降职）
         * - 公会信息管理权限（每日消息、公告）
         * - 银行权限（查看、存取、提取限额）
         *
         * 注意：公会会长（rankId=0）拥有所有权限且不可修改
         */
        class RankInfo
        {
            public:
                /**
                 * @brief 默认构造函数
                 *
                 * 初始化为无效排名
                 */
                RankInfo(): m_guildId(0), m_rankId(GUILD_RANK_NONE), m_rights(GR_RIGHT_EMPTY), m_bankMoneyPerDay(0) { }

                /**
                 * @brief 指定公会ID的构造函数
                 * @param guildId 公会ID
                 */
                RankInfo(ObjectGuid::LowType guildId) : m_guildId(guildId), m_rankId(GUILD_RANK_NONE), m_rights(GR_RIGHT_EMPTY), m_bankMoneyPerDay(0) { }

                /**
                 * @brief 完整构造函数
                 * @param guildId 公会ID
                 * @param rankId 排名ID
                 * @param name 排名名称
                 * @param rights 排名权限标志
                 * @param money 每日可提取银行金钱数量
                 *
                 * 如果是会长排名，则设置为无限金钱提取权限
                 */
                RankInfo(ObjectGuid::LowType guildId, uint8 rankId, std::string_view name, uint32 rights, uint32 money) :
                    m_guildId(guildId), m_rankId(rankId), m_name(name), m_rights(rights),
                    m_bankMoneyPerDay(rankId != GR_GUILDMASTER ? money : GUILD_WITHDRAW_MONEY_UNLIMITED) { }

                /**
                 * @brief 从数据库字段加载排名信息
                 * @param fields 数据库查询结果字段数组
                 */
                void LoadFromDB(Field* fields);

                /**
                 * @brief 保存排名信息到数据库
                 * @param trans 数据库事务对象
                 */
                void SaveToDB(CharacterDatabaseTransaction trans) const;

                // 获取排名ID
                uint8 GetId() const { return m_rankId; }

                // 获取排名名称
                std::string const& GetName() const { return m_name; }

                /**
                 * @brief 设置排名名称
                 * @param name 新的排名名称
                 */
                void SetName(std::string_view name);

                // 获取排名权限
                uint32 GetRights() const { return m_rights; }

                /**
                 * @brief 设置排名权限
                 * @param rights 新的权限标志
                 */
                void SetRights(uint32 rights);

                // 获取每日可提取银行金钱数量
                int32 GetBankMoneyPerDay() const { return m_bankMoneyPerDay; }

                /**
                 * @brief 设置每日可提取银行金钱数量
                 * @param money 每日可提取的金钱数量
                 */
                void SetBankMoneyPerDay(uint32 money);

                /**
                 * @brief 获取指定银行标签页的访问权限
                 * @param tabId 银行标签页ID
                 * @return 访问权限标志，如果标签页ID无效返回0
                 */
                inline int8 GetBankTabRights(uint8 tabId) const
                {
                    return tabId < GUILD_BANK_MAX_TABS ? m_bankTabRightsAndSlots[tabId].GetRights() : 0;
                }

                /**
                 * @brief 获取指定银行标签页每日可提取槽位数
                 * @param tabId 银行标签页ID
                 * @return 每日可提取槽位数，如果标签页ID无效返回0
                 */
                inline int32 GetBankTabSlotsPerDay(uint8 tabId) const
                {
                    return tabId < GUILD_BANK_MAX_TABS ? m_bankTabRightsAndSlots[tabId].GetSlots() : 0;
                }

                /**
                 * @brief 设置银行标签页的槽位和权限
                 * @param rightsAndSlots 权限和槽位设置
                 * @param saveToDB 是否立即保存到数据库
                 */
                void SetBankTabSlotsAndRights(GuildBankRightsAndSlots rightsAndSlots, bool saveToDB);

                /**
                 * @brief 如果需要，创建缺失的银行标签页权限设置
                 * @param ranks 排名数量
                 * @param trans 数据库事务对象
                 * @param logOnCreate 是否记录日志
                 *
                 * 当购买新的银行标签页时，为所有排名创建默认权限设置
                 */
                void CreateMissingTabsIfNeeded(uint8 ranks, CharacterDatabaseTransaction trans, bool logOnCreate = false);

            private:
                ObjectGuid::LowType m_guildId;      // 公会ID

                uint8  m_rankId;                    // 排名ID
                std::string m_name;                 // 排名名称
                uint32 m_rights;                    // 排名权限标志
                uint32 m_bankMoneyPerDay;           // 每日可提取银行金钱数量
                // 每个银行标签页的权限和槽位设置
                std::array<GuildBankRightsAndSlots, GUILD_BANK_MAX_TABS> m_bankTabRightsAndSlots = {};
        };

        /**
         * @class BankTab
         * @brief 银行标签页类
         *
         * 管理公会银行的单个标签页，包括物品存储和标签页信息
         * 每个公会最多可以有6个银行标签页
         * 每个标签页最多可以存储98个物品
         *
         * 主要功能：
         * - 存储物品（使用98个槽位的数组）
         * - 管理标签页名称、图标和描述文本
         * - 支持从数据库加载和保存物品
         *
         * 性能注意事项：
         * - 物品访问使用数组索引，时间复杂度O(1)
         * - 物品移动需要数据库事务
         */
        class BankTab
        {
            public:
                /**
                 * @brief 构造函数
                 * @param guildId 公会ID
                 * @param tabId 标签页ID
                 */
                BankTab(ObjectGuid::LowType guildId, uint8 tabId);

                /**
                 * @brief 从数据库字段加载标签页信息
                 * @param fields 数据库查询结果字段数组
                 *
                 * 加载标签页的名称、图标和文本描述
                 */
                void LoadFromDB(Field* fields);

                /**
                 * @brief 从数据库字段加载物品信息
                 * @param fields 数据库查询结果字段数组
                 * @return 加载成功返回true
                 *
                 * 加载物品实例并放置到对应的槽位
                 */
                bool LoadItemFromDB(Field* fields);

                /**
                 * @brief 删除标签页及其物品
                 * @param trans 数据库事务对象
                 * @param removeItemsFromDB 是否从数据库删除物品记录
                 *
                 * 清空标签页并删除数据库记录
                 */
                void Delete(CharacterDatabaseTransaction trans, bool removeItemsFromDB = false);

                /**
                 * @brief 设置标签页信息
                 * @param name 标签页名称
                 * @param icon 标签页图标路径
                 *
                 * 更新标签页的显示名称和图标
                 */
                void SetInfo(std::string_view name, std::string_view icon);

                /**
                 * @brief 设置标签页文本描述
                 * @param text 标签页文本内容
                 */
                void SetText(std::string_view text);

                /**
                 * @brief 发送标签页文本给指定会话
                 * @param guild 公会对象指针
                 * @param session 世界会话对象
                 *
                 * 将标签页描述文本发送给请求的客户端
                 */
                void SendText(Guild const* guild, WorldSession* session) const;

                // 获取标签页名称
                std::string const& GetName() const { return m_name; }
                // 获取标签页图标
                std::string const& GetIcon() const { return m_icon; }
                // 获取标签页文本
                std::string const& GetText() const { return m_text; }

                /**
                 * @brief 获取指定槽位的物品
                 * @param slotId 槽位ID（0-97）
                 * @return 物品指针，如果槽位为空或ID无效返回nullptr
                 */
                inline Item* GetItem(uint8 slotId) const { return slotId < GUILD_BANK_MAX_SLOTS ?  m_items[slotId] : nullptr; }

                /**
                 * @brief 设置指定槽位的物品
                 * @param trans 数据库事务对象
                 * @param slotId 槽位ID
                 * @param pItem 物品指针（可以为nullptr表示清空）
                 * @return 设置成功返回true
                 */
                bool SetItem(CharacterDatabaseTransaction trans, uint8 slotId, Item* pItem);

            private:
                ObjectGuid::LowType m_guildId;      // 公会ID
                uint8 m_tabId;                      // 标签页ID

                std::array<Item*, GUILD_BANK_MAX_SLOTS> m_items = {};  // 物品数组（98个槽位）
                std::string m_name;                 // 标签页名称
                std::string m_icon;                 // 标签页图标路径
                std::string m_text;                 // 标签页文本描述
        };

        /**
         * @class MoveItemData
         * @brief 物品移动数据基类
         *
         * 用于处理物品在银行和背包之间的移动操作
         * 提供统一的接口来处理不同容器（银行标签页或玩家背包）的物品移动
         *
         * 主要功能：
         * - 初始化和验证物品
         * - 检查存储权限
         * - 执行物品移动和分割
         * - 记录操作日志
         *
         * 设计模式：
         * - 使用模板方法模式，基类定义流程，子类实现具体细节
         * - 支持物品分割（堆叠物品的部分移动）
         *
         * 使用场景：
         * - 银行与背包之间的物品交换
         * - 银行标签页之间的物品移动
         * - 背包内物品整理
         */
        class MoveItemData
        {
            public:
                /**
                 * @brief 构造函数
                 * @param guild 公会对象指针
                 * @param player 玩家对象指针
                 * @param container 容器ID
                 * @param slotId 槽位ID
                 */
                MoveItemData(Guild* guild, Player* player, uint8 container, uint8 slotId);

                // 虚析构函数
                virtual ~MoveItemData();

                /**
                 * @brief 判断是否为银行容器
                 * @return 如果是银行容器返回true，否则为玩家背包
                 */
                virtual bool IsBank() const = 0;

                /**
                 * @brief 初始化物品指针
                 * @return 如果物品存在返回true，否则返回false
                 *
                 * 从容器中获取指定槽位的物品
                 */
                virtual bool InitItem() = 0;

                /**
                 * @brief 检查分割数量是否有效
                 * @param splitedAmount 分割数量（输入输出参数）
                 * @return 如果分割数量有效返回true
                 *
                 * 分割数量不能超过堆叠中的物品数量
                 * 如果数量有效，会被调整到合法范围
                 */
                virtual bool CheckItem(uint32& splitedAmount);

                /**
                 * @brief 判断玩家是否有权限保存物品到容器
                 * @param pOther 另一个移动数据对象（源或目标）
                 * @return 如果有权限返回true
                 */
                virtual bool HasStoreRights(MoveItemData* /*pOther*/) const { return true; }

                /**
                 * @brief 判断玩家是否有权限从容器提取物品
                 * @param pOther 另一个移动数据对象（源或目标）
                 * @return 如果有权限返回true
                 */
                virtual bool HasWithdrawRights(MoveItemData* /*pOther*/) const { return true; }

                /**
                 * @brief 检查容器是否可以存储指定物品
                 * @param pItem 物品指针
                 * @param swap 是否为交换操作
                 * @param sendError 是否发送错误消息给客户端
                 * @return 如果可以存储返回true
                 */
                bool CanStore(Item* pItem, bool swap, bool sendError);

                /**
                 * @brief 克隆存储的物品
                 * @param count 克隆的数量
                 * @return 克隆成功返回true
                 *
                 * 用于物品分割操作，创建物品的副本
                 */
                bool CloneItem(uint32 count);

                /**
                 * @brief 从容器移除物品
                 * @param trans 数据库事务对象
                 * @param pOther 另一个移动数据对象
                 * @param splitedAmount 分割数量（0表示完整移动）
                 *
                 * 纯虚函数，由子类实现具体的移除逻辑
                 */
                virtual void RemoveItem(CharacterDatabaseTransaction trans, MoveItemData* pOther, uint32 splitedAmount = 0) = 0;

                /**
                 * @brief 保存物品到容器
                 * @param trans 数据库事务对象
                 * @param pItem 物品指针
                 * @return 保存后的物品指针
                 *
                 * 纯虚函数，由子类实现具体的保存逻辑
                 */
                virtual Item* StoreItem(CharacterDatabaseTransaction trans, Item* pItem) = 0;

                /**
                 * @brief 记录银行事件日志
                 * @param trans 数据库事务对象
                 * @param pFrom 源移动数据对象
                 * @param count 物品数量
                 *
                 * 纯虚函数，由子类实现具体的日志记录
                 */
                virtual void LogBankEvent(CharacterDatabaseTransaction trans, MoveItemData* pFrom, uint32 count) const = 0;

                /**
                 * @brief 记录GM操作日志
                 * @param pFrom 源移动数据对象
                 *
                 * 如果是GM操作，记录到GM日志
                 */
                virtual void LogAction(MoveItemData* pFrom) const;

                /**
                 * @brief 从位置向量复制槽位ID
                 * @param ids 槽位ID集合
                 */
                void CopySlots(SlotIds& ids) const;

                // 获取物品指针（可指定是否为克隆物品）
                Item* GetItem(bool isCloned = false) const { return isCloned ? m_pClonedItem : m_pItem; }
                // 获取容器ID
                uint8 GetContainer() const { return m_container; }
                // 获取槽位ID
                uint8 GetSlotId() const { return m_slotId; }

            protected:
                /**
                 * @brief 检查是否可以存储物品
                 * @param pItem 物品指针
                 * @param swap 是否为交换操作
                 * @return 库存操作结果码
                 *
                 * 纯虚函数，由子类实现具体的存储检查逻辑
                 */
                virtual InventoryResult CanStore(Item* pItem, bool swap) = 0;

                Guild* m_pGuild;                   // 公会指针
                Player* m_pPlayer;                 // 玩家指针
                uint8 m_container;                 // 容器ID
                uint8 m_slotId;                    // 槽位ID
                Item* m_pItem;                     // 物品指针
                Item* m_pClonedItem;               // 克隆物品指针（用于分割堆叠）
                std::vector<ItemPosCount> m_vec;   // 位置向量（存储物品时的位置信息）
        };

        /**
         * @class PlayerMoveItemData
         * @brief 玩家背包物品移动数据类
         *
         * 处理玩家背包中的物品移动操作
         * 实现了MoveItemData的背包相关功能
         *
         * 主要职责：
         * - 从玩家背包获取和存储物品
         * - 检查背包空间是否足够
         * - 记录与银行交互的日志
         */
        class PlayerMoveItemData : public MoveItemData
        {
            public:
                /**
                 * @brief 构造函数
                 * @param guild 公会对象指针
                 * @param player 玩家对象指针
                 * @param container 容器ID（背包ID）
                 * @param slotId 槽位ID
                 */
                PlayerMoveItemData(Guild* guild, Player* player, uint8 container, uint8 slotId) :
                    MoveItemData(guild, player, container, slotId) { }

                // 不是银行容器
                bool IsBank() const override { return false; }

                /**
                 * @brief 初始化物品指针
                 * @return 如果物品存在返回true
                 *
                 * 从玩家背包指定槽位获取物品
                 */
                bool InitItem() override;

                /**
                 * @brief 从背包移除物品
                 * @param trans 数据库事务对象
                 * @param pOther 另一个移动数据对象
                 * @param splitedAmount 分割数量（0表示完整移除）
                 *
                 * 如果是分割操作，减少堆叠数量；否则完全移除物品
                 */
                void RemoveItem(CharacterDatabaseTransaction trans, MoveItemData* pOther, uint32 splitedAmount = 0) override;

                /**
                 * @brief 保存物品到背包
                 * @param trans 数据库事务对象
                 * @param pItem 物品指针
                 * @return 保存后的物品指针
                 */
                Item* StoreItem(CharacterDatabaseTransaction trans, Item* pItem) override;

                /**
                 * @brief 记录银行事件日志
                 * @param trans 数据库事务对象
                 * @param pFrom 源移动数据对象（通常是银行）
                 * @param count 物品数量
                 *
                 * 记录从银行提取物品的操作
                 */
                void LogBankEvent(CharacterDatabaseTransaction trans, MoveItemData* pFrom, uint32 count) const override;

            protected:
                /**
                 * @brief 检查背包是否可以存储物品
                 * @param pItem 物品指针
                 * @param swap 是否为交换操作
                 * @return 库存操作结果码
                 *
                 * 检查背包空间是否足够，是否有合适的位置
                 */
                InventoryResult CanStore(Item* pItem, bool swap) override;
        };

        /**
         * @class BankMoveItemData
         * @brief 银行物品移动数据类
         *
         * 处理银行标签页中的物品移动操作
         * 实现了MoveItemData的银行相关功能
         *
         * 主要职责：
         * - 从银行标签页获取和存储物品
         * - 检查银行权限（存取权限、每日限额）
         * - 检查银行空间是否足够
         * - 记录银行操作日志
         *
         * 权限检查：
         * - 检查排名是否有存取权限
         * - 检查是否超过每日提取限额
         */
        class BankMoveItemData : public MoveItemData
        {
            public:
                /**
                 * @brief 构造函数
                 * @param guild 公会对象指针
                 * @param player 玩家对象指针
                 * @param container 容器ID（银行标签页ID）
                 * @param slotId 槽位ID
                 */
                BankMoveItemData(Guild* guild, Player* player, uint8 container, uint8 slotId) :
                    MoveItemData(guild, player, container, slotId) { }

                // 是银行容器
                bool IsBank() const override { return true; }

                /**
                 * @brief 初始化物品指针
                 * @return 如果物品存在返回true
                 *
                 * 从银行标签页指定槽位获取物品
                 */
                bool InitItem() override;

                /**
                 * @brief 检查是否有保存物品权限
                 * @param pOther 另一个移动数据对象
                 * @return 如果有权限返回true
                 *
                 * 检查玩家排名是否有存入权限
                 */
                bool HasStoreRights(MoveItemData* pOther) const override;

                /**
                 * @brief 检查是否有提取物品权限
                 * @param pOther 另一个移动数据对象
                 * @return 如果有权限返回true
                 *
                 * 检查玩家排名是否有提取权限和剩余提取次数
                 */
                bool HasWithdrawRights(MoveItemData* pOther) const override;

                /**
                 * @brief 从银行移除物品
                 * @param trans 数据库事务对象
                 * @param pOther 另一个移动数据对象
                 * @param splitedAmount 分割数量
                 *
                 * 从银行标签页移除物品，并更新提取记录
                 */
                void RemoveItem(CharacterDatabaseTransaction trans, MoveItemData* pOther, uint32 splitedAmount) override;

                /**
                 * @brief 保存物品到银行
                 * @param trans 数据库事务对象
                 * @param pItem 物品指针
                 * @return 保存后的物品指针
                 */
                Item* StoreItem(CharacterDatabaseTransaction trans, Item* pItem) override;

                /**
                 * @brief 记录银行事件日志
                 * @param trans 数据库事务对象
                 * @param pFrom 源移动数据对象
                 * @param count 物品数量
                 *
                 * 记录存入、提取或移动物品的操作
                 */
                void LogBankEvent(CharacterDatabaseTransaction trans, MoveItemData* pFrom, uint32 count) const override;

                /**
                 * @brief 记录GM操作日志
                 * @param pFrom 源移动数据对象
                 *
                 * 如果是GM操作，记录到GM日志
                 */
                void LogAction(MoveItemData* pFrom) const override;

            protected:
                /**
                 * @brief 检查银行是否可以存储物品
                 * @param pItem 物品指针
                 * @param swap 是否为交换操作
                 * @return 库存操作结果码
                 *
                 * 检查银行标签页是否有空闲槽位或可合并的堆叠
                 */
                InventoryResult CanStore(Item* pItem, bool swap) override;

            private:
                /**
                 * @brief 内部方法：存储物品到指定标签页和位置
                 * @param trans 数据库事务对象
                 * @param pTab 银行标签页指针
                 * @param pItem 物品指针
                 * @param pos 物品位置信息
                 * @param clone 是否为克隆物品
                 * @return 存储后的物品指针
                 */
                Item* _StoreItem(CharacterDatabaseTransaction trans, BankTab* pTab, Item* pItem, ItemPosCount& pos, bool clone) const;

                /**
                 * @brief 内部方法：预留空间
                 * @param slotId 槽位ID
                 * @param pItem 物品指针
                 * @param pItemDest 目标槽位物品指针
                 * @param count 物品数量
                 * @return 预留成功返回true
                 */
                bool _ReserveSpace(uint8 slotId, Item* pItem, Item* pItemDest, uint32& count);

                /**
                 * @brief 内部方法：检查是否可以在标签页中存储物品
                 * @param pItem 物品指针
                 * @param skipSlotId 要跳过的槽位ID
                 * @param merge 是否尝试合并
                 * @param count 物品数量
                 *
                 * 遍历标签页槽位，寻找可存储的位置
                 */
                void CanStoreItemInTab(Item* pItem, uint8 skipSlotId, bool merge, uint32& count);
        };

    public:
        /**
         * @brief 发送公会命令结果给客户端
         * @param session 世界会话对象
         * @param type 公会命令类型
         * @param errCode 错误码
         * @param param 附加参数（通常是玩家名称或公会名称）
         *
         * 静态方法，用于向客户端发送公会操作的结果
         * 例如：创建公会失败、邀请成员成功等
         */
        static void SendCommandResult(WorldSession* session, GuildCommandType type, GuildCommandError errCode, std::string_view param = "");

        /**
         * @brief 发送保存徽章结果给客户端
         * @param session 世界会话对象
         * @param errCode 错误码
         *
         * 静态方法，用于向客户端发送徽章设置的结果
         */
        static void SendSaveEmblemResult(WorldSession* session, GuildEmblemError errCode);

        /**
         * @brief 构造函数
         *
         * 初始化公会对象，设置默认值
         * 注意：创建后需要调用Create方法才能真正创建公会
         */
        Guild();

        /**
         * @brief 析构函数
         *
         * 清理公会资源，包括成员、银行物品等
         */
        ~Guild();

        /**
         * @brief 创建公会
         * @param pLeader 公会会长玩家对象
         * @param name 公会名称
         * @return 创建成功返回true
         *
         * 执行公会的创建流程：
         * 1. 验证名称合法性
         * 2. 分配公会ID
         * 3. 创建默认排名
         * 4. 添加会长为第一个成员
         * 5. 保存到数据库
         */
        bool Create(Player* pLeader, std::string_view name);

        /**
         * @brief 解散公会
         *
         * 执行公会解散流程：
         * 1. 通知所有成员
         * 2. 清理银行物品
         * 3. 从数据库删除公会数据
         * 4. 从公会管理器移除
         */
        void Disband();

        // ========== 获取器方法 ==========

        // 获取公会ID
        ObjectGuid::LowType GetId() const { return m_id; }
        // 获取会长GUID
        ObjectGuid GetLeaderGUID() const { return m_leaderGuid; }
        // 获取公会名称
        std::string const& GetName() const { return m_name; }
        // 获取每日消息（Message of the Day）
        std::string const& GetMOTD() const { return m_motd; }
        // 获取公会信息描述
        std::string const& GetInfo() const { return m_info; }
        // 获取成员数量
        uint32 GetMemberCount() const { return m_members.size(); }
        // 获取创建日期
        time_t GetCreatedDate() const { return m_createdDate; }
        // 获取银行金钱数量
        uint64 GetBankMoney() const { return m_bankMoney; }

        /**
         * @brief 设置公会名称
         * @param name 新的公会名称
         * @return 设置成功返回true
         *
         * 检查名称合法性并更新公会名称
         */
        bool SetName(std::string_view name);

        // ========== 处理客户端命令的方法 ==========

        /**
         * @brief 处理名册查询请求
         * @param session 世界会话对象
         *
         * 发送公会成员列表给请求的客户端
         * 包括所有成员的名称、等级、职业、排名等信息
         */
        void HandleRoster(WorldSession* session);

        /**
         * @brief 处理公会查询请求
         * @param session 世界会话对象
         *
         * 发送公会基本信息给客户端
         */
        void HandleQuery(WorldSession* session);

        /**
         * @brief 处理设置每日消息请求
         * @param session 世界会话对象
         * @param motd 每日消息内容
         *
         * 更新公会的每日消息，需要相应权限
         */
        void HandleSetMOTD(WorldSession* session, std::string_view motd);

        /**
         * @brief 处理设置公会信息请求
         * @param session 世界会话对象
         * @param info 公会信息内容
         *
         * 更新公会的描述信息，需要相应权限
         */
        void HandleSetInfo(WorldSession* session, std::string_view info);

        /**
         * @brief 处理设置徽章请求
         * @param session 世界会话对象
         * @param emblemInfo 徽章信息
         *
         * 设置公会的徽章外观，需要会长权限和足够的金钱
         */
        void HandleSetEmblem(WorldSession* session, EmblemInfo const& emblemInfo);

        /**
         * @brief 处理设置会长请求
         * @param session 世界会话对象
         * @param name 新会长的角色名称
         *
         * 转移会长权限给其他成员，需要会长权限
         */
        void HandleSetLeader(WorldSession* session, std::string_view name);

        /**
         * @brief 处理设置银行标签页信息请求
         * @param session 世界会话对象
         * @param tabId 标签页ID
         * @param name 标签页名称
         * @param icon 标签页图标路径
         */
        void HandleSetBankTabInfo(WorldSession* session, uint8 tabId, std::string_view name, std::string_view icon);

        /**
         * @brief 处理设置成员备注请求
         * @param session 世界会话对象
         * @param name 成员名称
         * @param note 备注内容
         * @param officer 是否为官员备注
         */
        void HandleSetMemberNote(WorldSession* session, std::string_view name, std::string_view note, bool officer);

        /**
         * @brief 处理设置排名信息请求
         * @param session 世界会话对象
         * @param rankId 排名ID
         * @param name 排名名称
         * @param rights 排名权限
         * @param moneyPerDay 每日可提取金钱数量
         * @param rightsAndSlots 各银行标签页的权限和槽位设置
         */
        void HandleSetRankInfo(WorldSession* session, uint8 rankId, std::string_view name, uint32 rights, uint32 moneyPerDay, std::array<GuildBankRightsAndSlots, GUILD_BANK_MAX_TABS> const& rightsAndSlots);

        /**
         * @brief 处理购买银行标签页请求
         * @param session 世界会话对象
         * @param tabId 要购买的标签页ID
         *
         * 购买新的银行标签页，需要足够的金钱
         */
        void HandleBuyBankTab(WorldSession* session, uint8 tabId);

        /**
         * @brief 处理邀请成员请求
         * @param session 世界会话对象
         * @param name 被邀请玩家的名称
         *
         * 邀请玩家加入公会，需要邀请权限
         */
        void HandleInviteMember(WorldSession* session, std::string_view name);

        /**
         * @brief 处理接受成员邀请请求
         * @param session 世界会话对象
         *
         * 接受公会邀请，正式加入公会
         */
        void HandleAcceptMember(WorldSession* session);

        /**
         * @brief 处理成员离开公会请求
         * @param session 世界会话对象
         *
         * 成员主动离开公会
         * 注意：会长不能离开公会，必须先转移会长或解散公会
         */
        void HandleLeaveMember(WorldSession* session);

        /**
         * @brief 处理移除成员请求
         * @param session 世界会话对象
         * @param name 要移除的成员名称
         *
         * 将成员从公会中移除（踢出），需要相应权限
         */
        void HandleRemoveMember(WorldSession* session, std::string_view name);

        /**
         * @brief 处理更新成员排名请求
         * @param session 世界会话对象
         * @param name 成员名称
         * @param demote 是否为降职（true为降职，false为晋升）
         */
        void HandleUpdateMemberRank(WorldSession* session, std::string_view name, bool demote);

        /**
         * @brief 处理添加新排名请求
         * @param session 世界会话对象
         * @param name 新排名名称
         *
         * 添加新的公会排名，最大数量限制为10个
         */
        void HandleAddNewRank(WorldSession* session, std::string_view name);

        /**
         * @brief 处理移除排名请求
         * @param session 世界会话对象
         * @param rankId 要移除的排名ID
         */
        void HandleRemoveRank(WorldSession* session, uint8 rankId);

        /**
         * @brief 处理移除最低排名请求
         * @param session 世界会话对象
         *
         * 移除最低级别的排名（编号最大的排名）
         */
        void HandleRemoveLowestRank(WorldSession* session);

        /**
         * @brief 处理成员存入金钱请求
         * @param session 世界会话对象
         * @param amount 存入金额
         *
         * 将金钱存入公会银行
         */
        void HandleMemberDepositMoney(WorldSession* session, uint32 amount);

        /**
         * @brief 处理成员提取金钱请求
         * @param session 世界会话对象
         * @param amount 提取金额
         * @param repair 是否用于修理装备
         * @return 提取成功返回true
         *
         * 从公会银行提取金钱，需要相应权限和剩余额度
         */
        bool HandleMemberWithdrawMoney(WorldSession* session, uint32 amount, bool repair = false);

        /**
         * @brief 处理成员登出
         * @param session 世界会话对象
         *
         * 更新成员的登出时间，通知其他成员该成员下线
         */
        void HandleMemberLogout(WorldSession* session);

        /**
         * @brief 处理解散公会请求
         * @param session 世界会话对象
         *
         * 解散公会，需要会长权限
         */
        void HandleDisband(WorldSession* session);

        /**
         * @brief 更新成员数据
         * @param player 玩家对象
         * @param dataid 数据类型ID
         * @param value 新值
         *
         * 更新成员的区域ID、等级等信息
         */
        void UpdateMemberData(Player* player, uint8 dataid, uint32 value);

        /**
         * @brief 玩家状态改变时调用
         * @param player 玩家对象
         * @param flag 状态标志
         * @param state 新状态（true为设置，false为清除）
         *
         * 处理AFK、DND等状态变化
         */
        void OnPlayerStatusChange(Player* player, uint32 flag, bool state);

        // ========== 发送信息给客户端的方法 ==========

        /**
         * @brief 发送公会信息给客户端
         * @param session 世界会话对象
         *
         * 发送公会的详细信息给请求的客户端
         */
        void SendInfo(WorldSession* session) const;

        /**
         * @brief 发送事件日志给客户端
         * @param session 世界会话对象
         *
         * 发送公会事件日志（成员加入、离开、晋升等）
         */
        void SendEventLog(WorldSession* session) const;

        /**
         * @brief 发送银行日志给客户端
         * @param session 世界会话对象
         * @param tabId 银行标签页ID
         *
         * 发送指定标签页的银行操作日志
         */
        void SendBankLog(WorldSession* session, uint8 tabId) const;

        /**
         * @brief 发送银行标签页信息给客户端
         * @param session 世界会话对象
         * @param showTabs 是否显示标签页内容
         *
         * 发送银行的整体信息，包括标签页数量、金钱等
         */
        void SendBankTabsInfo(WorldSession* session, bool showTabs = false) const;

        /**
         * @brief 发送银行标签页数据给客户端
         * @param session 世界会话对象
         * @param tabId 标签页ID
         * @param sendAllSlots 是否发送所有槽位数据
         *
         * 发送指定标签页的物品数据
         */
        void SendBankTabData(WorldSession* session, uint8 tabId, bool sendAllSlots) const;

        /**
         * @brief 发送银行标签页文本给客户端
         * @param session 世界会话对象
         * @param tabId 标签页ID
         *
         * 发送标签页的描述文本
         */
        void SendBankTabText(WorldSession* session, uint8 tabId) const;

        /**
         * @brief 发送权限信息给客户端
         * @param session 世界会话对象
         *
         * 发送成员的公会权限信息
         */
        void SendPermissions(WorldSession* session) const;

        /**
         * @brief 发送金钱信息给客户端
         * @param session 世界会话对象
         *
         * 发送成员的剩余可提取金钱额度
         */
        void SendMoneyInfo(WorldSession* session) const;

        /**
         * @brief 发送登录信息给客户端
         * @param session 世界会话对象
         *
         * 发送公会相关的登录信息，包括MOTD、公会事件等
         */
        void SendLoginInfo(WorldSession* session);

        // ========== 从数据库加载数据的方法 ==========

        /**
         * @brief 从数据库加载公会基本信息
         * @param fields 数据库查询结果字段数组
         * @return 加载成功返回true
         *
         * 加载公会的ID、名称、会长、徽章等基本信息
         */
        bool LoadFromDB(Field* fields);

        /**
         * @brief 从数据库加载排名信息
         * @param fields 数据库查询结果字段数组
         *
         * 加载排名的ID、名称、权限、银行限额等
         */
        void LoadRankFromDB(Field* fields);

        /**
         * @brief 从数据库加载成员信息
         * @param fields 数据库查询结果字段数组
         * @return 加载成功返回true
         *
         * 加载成员的角色信息和公会信息
         */
        bool LoadMemberFromDB(Field* fields);

        /**
         * @brief 从数据库加载事件日志
         * @param fields 数据库查询结果字段数组
         * @return 加载成功返回true
         */
        bool LoadEventLogFromDB(Field* fields);

        /**
         * @brief 从数据库加载银行权限
         * @param fields 数据库查询结果字段数组
         *
         * 加载排名对银行标签页的访问权限
         */
        void LoadBankRightFromDB(Field* fields);

        /**
         * @brief 从数据库加载银行标签页
         * @param fields 数据库查询结果字段数组
         *
         * 加载银行标签页的名称、图标、文本等
         */
        void LoadBankTabFromDB(Field* fields);

        /**
         * @brief 从数据库加载银行事件日志
         * @param fields 数据库查询结果字段数组
         * @return 加载成功返回true
         */
        bool LoadBankEventLogFromDB(Field* fields);

        /**
         * @brief 从数据库加载银行物品
         * @param fields 数据库查询结果字段数组
         * @return 加载成功返回true
         *
         * 加载银行中的物品实例
         */
        bool LoadBankItemFromDB(Field* fields);

        /**
         * @brief 验证加载数据的有效性
         * @return 数据有效返回true
         *
         * 检查公会数据的完整性，如会长是否存在、排名是否合理等
         */
        bool Validate();

        // ========== 广播方法 ==========

        /**
         * @brief 广播消息给公会成员
         * @param session 世界会话对象
         * @param officerOnly 是否仅广播给官员
         * @param msg 消息内容
         * @param language 语言类型
         *
         * 向公会频道或官员频道发送消息
         */
        void BroadcastToGuild(WorldSession* session, bool officerOnly, std::string_view msg, uint32 language = LANG_UNIVERSAL) const;

        /**
         * @brief 广播数据包给指定排名的成员
         * @param packet 数据包指针
         * @param rankId 排名ID
         */
        void BroadcastPacketToRank(WorldPacket const* packet, uint8 rankId) const;

        /**
         * @brief 广播数据包给所有在线成员
         * @param packet 数据包指针
         */
        void BroadcastPacket(WorldPacket const* packet) const;

        /**
         * @brief 批量邀请到事件
         * @param session 世界会话对象
         * @param minLevel 最低等级
         * @param maxLevel 最高等级
         * @param minRank 最低排名
         *
         * 用于公会活动邀请符合条件的成员
         */
        void MassInviteToEvent(WorldSession* session, uint32 minLevel, uint32 maxLevel, uint32 minRank);

        /**
         * @brief 广播工作者模板方法
         * @tparam Do 可调用对象类型
         * @param _do 要执行的操作
         * @param except 要排除的玩家对象
         *
         * 遍历所有在线成员并执行指定操作
         * 用于实现自定义的广播逻辑
         */
        template<class Do>
        void BroadcastWorker(Do& _do, Player* except = nullptr)
        {
            for (auto itr = m_members.begin(); itr != m_members.end(); ++itr)
                if (Player* player = itr->second.FindConnectedPlayer())
                    if (player != except)
                        _do(player);
        }

        // ========== 成员管理方法 ==========

        /**
         * @brief 添加成员到公会
         * @param trans 数据库事务对象
         * @param guid 成员的玩家GUID
         * @param rankId 排名ID（如果为GUILD_RANK_NONE，则分配最低排名）
         * @return 添加成功返回true
         *
         * 将玩家添加到公会，创建Member对象并保存到数据库
         */
        bool AddMember(CharacterDatabaseTransaction trans, ObjectGuid guid, uint8 rankId = GUILD_RANK_NONE);

        /**
         * @brief 删除成员
         * @param trans 数据库事务对象
         * @param guid 成员的玩家GUID
         * @param isDisbanding 是否正在解散公会
         * @param isKicked 是否被踢出
         * @return 删除成功返回true
         *
         * 从公会中移除成员，更新数据库
         */
        bool DeleteMember(CharacterDatabaseTransaction trans, ObjectGuid guid, bool isDisbanding = false, bool isKicked = false);

        /**
         * @brief 更改成员排名
         * @param trans 数据库事务对象
         * @param guid 成员的玩家GUID
         * @param newRank 新排名ID
         * @return 更改成功返回true
         */
        bool ChangeMemberRank(CharacterDatabaseTransaction trans, ObjectGuid guid, uint8 newRank);

        /**
         * @brief 获取成员可用的修理金钱
         * @param guid 成员的玩家GUID
         * @return 成员可用的修理金钱数量
         *
         * 根据成员排名的每日限额和已使用额度计算剩余可用金额
         */
        uint64 GetMemberAvailableMoneyForRepairItems(ObjectGuid guid) const;

        // ========== 银行操作方法 ==========

        /**
         * @brief 交换银行物品
         * @param player 玩家对象
         * @param tabId 源标签页ID
         * @param slotId 源槽位ID
         * @param destTabId 目标标签页ID
         * @param destSlotId 目标槽位ID
         * @param splitedAmount 分割数量（0表示完整移动）
         *
         * 在银行标签页之间移动物品
         */
        void SwapItems(Player* player, uint8 tabId, uint8 slotId, uint8 destTabId, uint8 destSlotId, uint32 splitedAmount);

        /**
         * @brief 交换银行物品与背包物品
         * @param player 玩家对象
         * @param toChar 是否移动到玩家背包
         * @param tabId 银行标签页ID
         * @param slotId 银行槽位ID
         * @param playerBag 玩家背包ID
         * @param playerSlotId 玩家背包槽位ID
         * @param splitedAmount 分割数量
         *
         * 在银行和玩家背包之间移动物品
         */
        void SwapItemsWithInventory(Player* player, bool toChar, uint8 tabId, uint8 slotId, uint8 playerBag, uint8 playerSlotId, uint32 splitedAmount);

        // ========== 银行标签页方法 ==========

        /**
         * @brief 设置银行标签页文本
         * @param tabId 标签页ID
         * @param text 文本内容
         */
        void SetBankTabText(uint8 tabId, std::string_view text);

        /**
         * @brief 重置每日使用次数
         *
         * 每日重置时调用，清空所有成员的银行提取记录
         */
        void ResetTimes();

        // 获取弱引用指针
        Trinity::unique_weak_ptr<Guild> GetWeakPtr() const { return m_weakRef; }
        // 设置弱引用指针
        void SetWeakPtr(Trinity::unique_weak_ptr<Guild> weakRef) { m_weakRef = std::move(weakRef); }

    protected:
        ObjectGuid::LowType m_id;                    // 公会ID（数据库中的低GUID）
        std::string m_name;                          // 公会名称
        ObjectGuid m_leaderGuid;                     // 公会会长的GUID
        std::string m_motd;                          // 每日消息（Message of the Day）
        std::string m_info;                          // 公会信息描述
        time_t m_createdDate;                        // 公会创建日期

        EmblemInfo m_emblemInfo;                     // 公会徽章信息
        uint32 m_accountsNumber;                     // 公会中的账号数量（去重后的）
        uint64 m_bankMoney;                          // 公会银行中的金钱数量

        std::vector<RankInfo> m_ranks;               // 公会排名列表
        std::unordered_map<uint32, Member> m_members; // 公会成员映射表（key为玩家GUID的低32位）
        std::vector<BankTab> m_bankTabs;             // 公会银行标签页列表

        // 这些是有序列表，第一个元素是最旧的条目
        LogHolder<EventLogEntry> m_eventLog;         // 公会事件日志容器
        std::array<LogHolder<BankEventLogEntry>, GUILD_BANK_MAX_TABS + 1> m_bankEventLog = {}; // 银行事件日志数组（每个标签页一个，最后一个用于金钱日志）

        Trinity::unique_weak_ptr<Guild> m_weakRef;   // 公会对象的弱引用指针

    private:
        // 获取排名数量
        inline uint8 _GetRanksSize() const { return uint8(m_ranks.size()); }
        // 获取排名信息（只读）
        inline RankInfo const* GetRankInfo(uint8 rankId) const { return rankId < _GetRanksSize() ? &m_ranks[rankId] : nullptr; }
        // 获取排名信息（可修改）
        inline RankInfo* GetRankInfo(uint8 rankId) { return rankId < _GetRanksSize() ? &m_ranks[rankId] : nullptr; }
        // 检查玩家是否拥有指定排名权限
        bool _HasRankRight(Player* player, uint32 right) const;

        // 获取最低排名ID
        inline uint8 _GetLowestRankId() const { return uint8(m_ranks.size() - 1); }

        // 获取已购买的银行标签页数量
        inline uint8 _GetPurchasedTabsSize() const { return uint8(m_bankTabs.size()); }
        // 获取银行标签页（可修改）
        inline BankTab* GetBankTab(uint8 tabId) { return tabId < m_bankTabs.size() ? &m_bankTabs[tabId] : nullptr; }
        // 获取银行标签页（只读）
        inline BankTab const* GetBankTab(uint8 tabId) const { return tabId < m_bankTabs.size() ? &m_bankTabs[tabId] : nullptr; }

        // 根据GUID获取成员（只读）
        inline Member const* GetMember(ObjectGuid guid) const
        {
            auto itr = m_members.find(guid.GetCounter());
            return (itr != m_members.end()) ? &itr->second : nullptr;
        }

        // 根据GUID获取成员（可修改）
        inline Member* GetMember(ObjectGuid guid)
        {
            auto itr = m_members.find(guid.GetCounter());
            return (itr != m_members.end()) ? &itr->second : nullptr;
        }

        // 根据名称获取成员
        inline Member* GetMember(std::string_view name)
        {
            for (auto itr = m_members.begin(); itr != m_members.end(); ++itr)
                if (itr->second.GetName() == name)
                    return &itr->second;

            return nullptr;
        }

        // 从数据库删除成员记录
        static void _DeleteMemberFromDB(CharacterDatabaseTransaction trans, ObjectGuid::LowType lowguid);

        // 创建新的银行标签页
        void _CreateNewBankTab();
        // 创建默认公会排名（使用指定语言环境的名称）
        void _CreateDefaultGuildRanks(CharacterDatabaseTransaction trans, LocaleConstant loc);
        // 创建新排名
        bool _CreateRank(CharacterDatabaseTransaction trans, std::string_view name, uint32 rights);
        // 当成员加入或离开公会时更新账号数量
        void _UpdateAccountsNumber();
        // 检查玩家是否为会长
        bool _IsLeader(Player* player) const;
        // 删除银行物品
        void _DeleteBankItems(CharacterDatabaseTransaction trans, bool removeItemsFromDB = false);
        // 修改银行金钱（增加或减少）
        bool _ModifyBankMoney(CharacterDatabaseTransaction trans, uint64 amount, bool add);
        // 设置会长GUID
        void _SetLeaderGUID(Member& pLeader);

        // 设置排名的每日银行金钱提取额度
        void _SetRankBankMoneyPerDay(uint8 rankId, uint32 moneyPerDay);
        // 设置排名的银行标签页权限和槽位
        void _SetRankBankTabRightsAndSlots(uint8 rankId, GuildBankRightsAndSlots rightsAndSlots, bool saveToDB = true);
        // 获取排名对指定银行标签页的访问权限
        int8 _GetRankBankTabRights(uint8 rankId, uint8 tabId) const;
        // 获取排名的权限标志
        uint32 _GetRankRights(uint8 rankId) const;
        // 获取排名的每日银行金钱提取额度
        int32 _GetRankBankMoneyPerDay(uint8 rankId) const;
        // 获取排名对指定银行标签页的每日可提取槽位数
        int32 _GetRankBankTabSlotsPerDay(uint8 rankId, uint8 tabId) const;
        // 获取排名名称
        std::string _GetRankName(uint8 rankId) const;

        // 获取成员在指定标签页的剩余可提取槽位数
        int32 _GetMemberRemainingSlots(Member const& member, uint8 tabId) const;
        // 获取成员的剩余可提取金钱数
        int32 _GetMemberRemainingMoney(Member const& member) const;
        // 更新成员的银行提取槽位记录
        void _UpdateMemberWithdrawSlots(CharacterDatabaseTransaction trans, ObjectGuid guid, uint8 tabId);
        // 检查成员是否有指定银行标签页的权限
        bool _MemberHasTabRights(ObjectGuid guid, uint8 tabId, uint32 rights) const;

        // 记录公会事件日志
        void _LogEvent(GuildEventLogTypes eventType, ObjectGuid::LowType playerGuid1, ObjectGuid::LowType playerGuid2 = 0, uint8 newRank = 0);
        // 记录银行事件日志
        void _LogBankEvent(CharacterDatabaseTransaction trans, GuildBankEventLogTypes eventType, uint8 tabId, ObjectGuid::LowType playerGuid, uint32 itemOrMoney, uint16 itemStackCount = 0, uint8 destTabId = 0);

        // 获取银行指定标签页和槽位的物品
        Item* _GetItem(uint8 tabId, uint8 slotId) const;
        // 移除银行指定标签页和槽位的物品
        void _RemoveItem(CharacterDatabaseTransaction trans, uint8 tabId, uint8 slotId);
        // 移动物品（源到目标）
        void _MoveItems(MoveItemData* pSrc, MoveItemData* pDest, uint32 splitedAmount);
        // 执行物品移动操作
        bool _DoItemsMove(MoveItemData* pSrc, MoveItemData* pDest, bool sendError, uint32 splitedAmount = 0);

        // 发送银行内容给客户端
        void _SendBankContent(WorldSession* session, uint8 tabId, bool sendAllSlots) const;
        // 发送银行金钱更新给客户端
        void _SendBankMoneyUpdate(WorldSession* session) const;
        // 发送银行内容更新（基于移动数据）
        void _SendBankContentUpdate(MoveItemData* pSrc, MoveItemData* pDest) const;
        // 发送银行内容更新（指定标签页和槽位）
        void _SendBankContentUpdate(uint8 tabId, SlotIds slots) const;
        // 发送银行列表给客户端
        void _SendBankList(WorldSession* session = nullptr, uint8 tabId = 0, bool sendFullSlots = false, SlotIds* slots = nullptr) const;

        // 广播公会事件给所有在线成员
        void _BroadcastEvent(GuildEvents guildEvent, ObjectGuid guid, Optional<std::string_view> param1 = {}, Optional<std::string_view> param2 = {}, Optional<std::string_view> param3 = {}) const;
};
#endif
