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
 * @file SocialMgr.h
 * @brief 玩家社交系统管理模块
 *
 * 本模块负责管理玩家的社交功能，包括：
 * - 好友列表管理（添加、删除、状态更新）
 * - 忽略列表管理（屏蔽玩家）
 * - 屏蔽列表管理（静音玩家）
 * - 好友在线状态通知和广播
 * - 社交数据的数据库持久化
 *
 * 主要类：
 * - PlayerSocial: 单个玩家的社交数据管理
 * - SocialMgr: 全局社交管理器（单例模式）
 * - FriendInfo: 好友信息结构体
 */

#ifndef __TRINITY_SOCIALMGR_H
#define __TRINITY_SOCIALMGR_H

#include "DatabaseEnvFwd.h"
#include "Common.h"
#include "ObjectGuid.h"
#include <map>

class Player;
class WorldPacket;

/**
 * @brief 好友在线状态枚举
 *
 * 定义好友的各种在线状态，包括离线、在线、离开、忙碌等
 */
enum FriendStatus
{
    FRIEND_STATUS_OFFLINE   = 0x00,  ///< 离线状态
    FRIEND_STATUS_ONLINE    = 0x01,  ///< 在线状态
    FRIEND_STATUS_AFK       = 0x02,  ///< 离开状态（AFK = Away From Keyboard）
    FRIEND_STATUS_DND       = 0x04,  ///< 请勿打扰状态（DND = Do Not Disturb）
    FRIEND_STATUS_RAF       = 0x08   ///< 招募好友状态（RaF = Recruit-A-Friend）
};

/**
 * @brief 社交关系标志枚举
 *
 * 定义玩家之间的社交关系类型，可以组合使用
 */
enum SocialFlag
{
    SOCIAL_FLAG_FRIEND      = 0x01,  ///< 好友关系标志
    SOCIAL_FLAG_IGNORED     = 0x02,  ///< 忽略（屏蔽）关系标志
    SOCIAL_FLAG_MUTED       = 0x04,  ///< 静音关系标志（推测）
    SOCIAL_FLAG_UNK         = 0x08,  ///< 未知标志，不是招募好友标志

    SOCIAL_FLAG_ALL         = SOCIAL_FLAG_FRIEND | SOCIAL_FLAG_IGNORED | SOCIAL_FLAG_MUTED  ///< 所有社交标志的组合
};

/**
 * @brief 好友信息结构体
 *
 * 存储好友的详细信息，包括在线状态、等级、职业、区域和备注等
 */
struct FriendInfo
{
    FriendStatus Status;    ///< 在线状态
    uint8 Flags;            ///< 社交关系标志
    uint32 Area;            ///< 所在区域ID
    uint8 Level;            ///< 角色等级
    uint8 Class;            ///< 角色职业
    std::string Note;       ///< 好友备注（最大48字符）

    /**
     * @brief 默认构造函数
     *
     * 初始化好友信息为离线状态的空信息
     */
    FriendInfo() : Status(FRIEND_STATUS_OFFLINE), Flags(0), Area(0), Level(0), Class(0), Note()
    { }

    /**
     * @brief 带参数构造函数
     * @param flags 社交关系标志
     * @param note 好友备注字符串
     */
    FriendInfo(uint8 flags, std::string const& note) : Status(FRIEND_STATUS_OFFLINE), Flags(flags), Area(0), Level(0), Class(0), Note(note)
    { }
};

/**
 * @brief 好友相关操作的结果码枚举
 *
 * 定义好友操作（添加、删除、忽略等）返回给客户端的结果码
 * 客户端根据这些结果码显示相应的提示信息
 */
enum FriendsResult : uint8
{
    FRIEND_DB_ERROR         = 0x00,  ///< 数据库错误
    FRIEND_LIST_FULL        = 0x01,  ///< 好友列表已满
    FRIEND_ONLINE           = 0x02,  ///< 好友上线
    FRIEND_OFFLINE          = 0x03,  ///< 好友下线
    FRIEND_NOT_FOUND        = 0x04,  ///< 未找到好友
    FRIEND_REMOVED          = 0x05,  ///< 好友已删除
    FRIEND_ADDED_ONLINE     = 0x06,  ///< 好友添加成功（在线）
    FRIEND_ADDED_OFFLINE    = 0x07,  ///< 好友添加成功（离线）
    FRIEND_ALREADY          = 0x08,  ///< 已经是好友
    FRIEND_SELF             = 0x09,  ///< 不能添加自己为好友
    FRIEND_ENEMY            = 0x0A,  ///< 不能添加敌对阵营玩家
    FRIEND_IGNORE_FULL      = 0x0B,  ///< 忽略列表已满
    FRIEND_IGNORE_SELF      = 0x0C,  ///< 不能忽略自己
    FRIEND_IGNORE_NOT_FOUND = 0x0D,  ///< 未找到要忽略的玩家
    FRIEND_IGNORE_ALREADY   = 0x0E,  ///< 已经在忽略列表中
    FRIEND_IGNORE_ADDED     = 0x0F,  ///< 玩家已添加到忽略列表
    FRIEND_IGNORE_REMOVED   = 0x10,  ///< 玩家已从忽略列表移除
    FRIEND_IGNORE_AMBIGUOUS = 0x11,  ///< 名称歧义，请输入更多服务器名信息
    FRIEND_MUTE_FULL        = 0x12,  ///< 静音列表已满
    FRIEND_MUTE_SELF        = 0x13,  ///< 不能静音自己
    FRIEND_MUTE_NOT_FOUND   = 0x14,  ///< 未找到要静音的玩家
    FRIEND_MUTE_ALREADY     = 0x15,  ///< 已经在静音列表中
    FRIEND_MUTE_ADDED       = 0x16,  ///< 玩家已添加到静音列表
    FRIEND_MUTE_REMOVED     = 0x17,  ///< 玩家已从静音列表移除
    FRIEND_MUTE_AMBIGUOUS   = 0x18,  ///< 名称歧义，请输入更多服务器名信息
    FRIEND_UNK1             = 0x19,  ///< 未知结果码1（客户端无消息）
    FRIEND_UNK2             = 0x1A,  ///< 未知结果码2
    FRIEND_UNK3             = 0x1B,  ///< 未知结果码3
    FRIEND_UNKNOWN          = 0x1C   ///< 未知的_friend响应
};

/// 好友列表最大容量限制
#define SOCIALMGR_FRIEND_LIMIT  50u
/// 忽略列表最大容量限制
#define SOCIALMGR_IGNORE_LIMIT  50u

/**
 * @brief 玩家社交数据管理类
 *
 * 管理单个玩家的社交数据，包括好友列表、忽略列表、静音列表等。
 * 提供社交关系的增删改查操作，并与数据库进行同步。
 *
 * 职责：
 * - 维护玩家的社交关系映射表
 * - 处理好友/忽略/静音列表的添加和删除
 * - 管理好友备注
 * - 发送社交列表数据包给客户端
 * - 提供社交关系查询接口
 */
class TC_GAME_API PlayerSocial
{
    friend class SocialMgr;  ///< SocialMgr需要访问私有成员

    public:
        /**
         * @brief 构造函数
         *
         * 初始化玩家社交数据对象
         */
        PlayerSocial();

        /**
         * @brief 添加社交关系
         * @param guid 目标玩家GUID
         * @param flag 社交关系标志
         * @return 添加成功返回true，超过限制或失败返回false
         *
         * 将目标玩家添加到好友/忽略/静音列表。
         * 会检查客户端限制（好友50人，忽略50人），
         * 并同步更新数据库。
         *
         * @note 调用时机：玩家执行添加好友/忽略/静音操作时
         */
        bool AddToSocialList(ObjectGuid const& guid, SocialFlag flag);

        /**
         * @brief 从社交列表移除
         * @param guid 目标玩家GUID
         * @param flag 要移除的社交关系标志
         *
         * 从指定社交列表中移除目标玩家。
         * 如果移除后该玩家无任何社交关系，则从映射表中完全删除。
         * 同步更新数据库。
         *
         * @note 调用时机：玩家执行删除好友/取消忽略/取消静音操作时
         */
        void RemoveFromSocialList(ObjectGuid const& guid, SocialFlag flag);

        /**
         * @brief 设置好友备注
         * @param guid 好友玩家GUID
         * @param note 备注字符串（最大48字符）
         *
         * 为好友设置备注信息，备注长度限制为48字符。
         * 同步更新到数据库。
         *
         * @note 调用时机：玩家修改好友备注时
         */
        void SetFriendNote(ObjectGuid const& guid, std::string const& note);

        /**
         * @brief 发送社交列表到客户端
         * @param player 接收列表的玩家对象
         * @param flags 要发送的列表类型标志位掩码
         *
         * 构建并发送社交列表数据包给客户端。
         * 数据包包含好友信息、忽略列表、静音列表等。
         * 会检查客户端容量限制并过滤超出的项目。
         *
         * @note 调用时机：玩家登录、请求好友列表时
         * @note 性能注意事项：遍历整个社交映射表，O(n)复杂度
         */
        void SendSocialList(Player* player, uint32 flags);

        /**
         * @brief 检查是否为好友关系
         * @param friendGuid 目标玩家GUID
         * @return 是好友返回true，否则返回false
         */
        bool HasFriend(ObjectGuid const& friendGuid);

        /**
         * @brief 检查是否在忽略列表中
         * @param ignoreGuid 目标玩家GUID
         * @return 在忽略列表返回true，否则返回false
         */
        bool HasIgnore(ObjectGuid const& ignoreGuid);

        /**
         * @brief 获取玩家GUID
         * @return 玩家GUID的常量引用
         */
        ObjectGuid const& GetPlayerGUID() const { return _playerGUID; }

        /**
         * @brief 设置玩家GUID
         * @param guid 要设置的玩家GUID
         */
        void SetPlayerGUID(ObjectGuid const& guid) { _playerGUID = guid; }

        /**
         * @brief 获取指定类型的社交关系数量
         * @param flag 社交关系标志
         * @return 符合条件的社交关系数量
         *
         * @note 性能注意事项：遍历整个社交映射表，O(n)复杂度
         */
        uint32 GetNumberOfSocialsWithFlag(SocialFlag flag);

    private:
        /**
         * @brief 内部函数：检查是否存在指定社交关系
         * @param guid 目标玩家GUID
         * @param flags 社交关系标志位掩码
         * @return 存在指定关系返回true，否则返回false
         */
        bool _HasContact(ObjectGuid const& guid, SocialFlag flags);

        /// 社交关系映射表类型定义（GUID -> 好友信息）
        typedef std::map<ObjectGuid, FriendInfo> PlayerSocialMap;
        PlayerSocialMap _playerSocialMap;  ///< 社交关系映射表，存储所有社交关系

        ObjectGuid _playerGUID;  ///< 本社交数据所属玩家的GUID
};

/**
 * @brief 全局社交管理器类（单例模式）
 *
 * 负责管理所有玩家的社交数据，提供社交功能的全局服务。
 *
 * 职责：
 * - 维护所有玩家的社交数据映射表
 * - 从数据库加载玩家社交数据
 * - 获取好友详细信息（在线状态、等级、区域等）
 * - 发送好友状态变更通知
 * - 广播数据包给好友列表中的玩家
 * - 管理玩家社交数据的生命周期
 *
 * 使用方式：通过 sSocialMgr 宏访问单例实例
 */
class SocialMgr
{
    private:
        /**
         * @brief 私有构造函数（单例模式）
         */
        SocialMgr() { }

        /**
         * @brief 私有析构函数（单例模式）
         */
        ~SocialMgr() { }

    public:
        /**
         * @brief 获取单例实例
         * @return SocialMgr单例指针
         */
        static SocialMgr* instance();

        /**
         * @brief 移除玩家的社交数据
         * @param guid 要移除的玩家GUID
         *
         * 当玩家离线或删除角色时调用，从内存中移除其社交数据
         *
         * @note 调用时机：玩家下线、删除角色时
         */
        void RemovePlayerSocial(ObjectGuid const& guid) { _socialMap.erase(guid); }

        /**
         * @brief 获取好友详细信息
         * @param player 查询者玩家对象
         * @param friendGUID 目标好友GUID
         * @param friendInfo [out] 输出的好友信息结构体
         *
         * 获取好友的详细信息，包括在线状态、等级、职业、区域等。
         * 会进行权限检查（GM级别、阵营限制等）和可见性判断。
         *
         * 处理逻辑：
         * 1. 检查目标玩家是否在线
         * 2. 检查查询者是否有权限查看目标（GM级别、阵营等）
         * 3. 获取目标的在线状态（在线/AFK/DND）
         * 4. 检查是否为招募好友关系
         * 5. 填充区域、等级、职业等信息
         *
         * @note 调用时机：发送好友列表、好友状态变更时
         * @note 性能注意事项：会查找在线玩家对象，可能涉及全局访问
         */
        static void GetFriendInfo(Player* player, ObjectGuid const& friendGUID, FriendInfo& friendInfo);

        /**
         * @brief 发送好友状态通知
         * @param player 玩家对象
         * @param result 操作结果码
         * @param friendGuid 相关好友的GUID
         * @param broadcast 是否广播给好友列表中的玩家，默认false
         *
         * 构建并发送好友状态变更数据包。
         * 根据操作结果码填充不同的数据内容。
         * 如果broadcast为true，则广播给所有将此玩家加为好友的玩家。
         *
         * @note 调用时机：添加/删除好友、好友上下线、状态变更时
         */
        void SendFriendStatus(Player* player, FriendsResult result, ObjectGuid const& friendGuid, bool broadcast = false);

        /**
         * @brief 广播数据包给好友列表中的玩家
         * @param player 发送源玩家对象
         * @param packet 要广播的数据包
         *
         * 将数据包发送给所有将此玩家加为好友的在线玩家。
         * 会进行权限和阵营检查，确保发送者对接收者可见。
         *
         * 处理逻辑：
         * 1. 遍历所有玩家的社交数据
         * 2. 检查是否将发送者加为好友
         * 3. 检查接收者是否有权限查看发送者
         * 4. 检查阵营限制
         * 5. 发送数据包
         *
         * @note 调用时机：好友上下线、状态变更需要通知时
         * @note 性能注意事项：遍历所有玩家的社交数据，O(n*m)复杂度
         */
        void BroadcastToFriendListers(Player* player, WorldPacket const* packet);

        /**
         * @brief 从数据库加载玩家社交数据
         * @param result 数据库查询结果
         * @param guid 玩家GUID
         * @return 加载的PlayerSocial对象指针
         *
         * 从数据库加载玩家的社交关系数据，包括好友、忽略、静音列表。
         * 如果玩家已有社交数据对象则复用，否则创建新的。
         *
         * @note 调用时机：玩家登录加载角色数据时
         */
        PlayerSocial* LoadFromDB(PreparedQueryResult result, ObjectGuid const& guid);

    private:
        /// 社交数据映射表类型定义（玩家GUID -> 社交数据）
        typedef std::map<ObjectGuid, PlayerSocial> SocialMap;
        SocialMap _socialMap;  ///< 存储所有玩家的社交数据
};

/// 获取SocialMgr单例的全局访问宏
#define sSocialMgr SocialMgr::instance()

#endif
