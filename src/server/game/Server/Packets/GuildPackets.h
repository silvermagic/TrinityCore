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
 * @file GuildPackets.h
 * @brief 公会系统网络包定义头文件
 *
 * 本文件定义了公会系统相关的所有客户端和服务器端网络包。
 * 主要功能模块包括：
 * - 公会基本信息查询和管理
 * - 公会成员管理（邀请、踢出、晋升、降级等）
 * - 公会银行系统（存取款、物品管理、日志查询）
 * - 公会事件日志和银行日志
 * - 公会权限管理
 * - 公会徽章设置
 *
 * 网络包分为两大类：
 * - ClientPacket: 客户端发送到服务器的包
 * - ServerPacket: 服务器发送到客户端的包
 */

#ifndef GuildPackets_h__
#define GuildPackets_h__

#include "Packet.h"
#include "Guild.h"
#include "ObjectGuid.h"
#include "PacketUtilities.h"
#include "WowTime.h"
#include <array>

namespace WorldPackets
{
    namespace Guild
    {
        /**
         * @class QueryGuildInfo
         * @brief 客户端查询公会信息的请求包
         *
         * 当客户端需要获取某个公会的基本信息（如名称、等级、徽章等）时发送此包。
         * 通常在玩家查看公会详情或公会列表时触发。
         */
        class QueryGuildInfo final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            QueryGuildInfo(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_QUERY, std::move(packet)) { }

            /**
             * @brief 读取客户端发送的公会查询数据
             *
             * 从网络包中读取公会ID，用于后续查询公会详细信息。
             */
            void Read() override;

            uint32 GuildId = 0; ///< 要查询的公会ID
        };

        /**
         * @struct GuildInfo
         * @brief 公会基本信息数据结构
         *
         * 存储公会的基本信息，包括公会名称、等级名称和公会徽章样式。
         * 用于公会查询响应包中返回给客户端。
         */
        struct GuildInfo
        {
            std::string GuildName; ///< 公会名称

            std::array<std::string, GUILD_RANKS_MAX_COUNT> Ranks; ///< 所有等级名称数组（最多GUILD_RANKS_MAX_COUNT个）
            uint32 RankCount = 0; ///< 实际等级数量

            uint32 EmblemStyle = 0; ///< 徽章样式ID
            uint32 EmblemColor = 0; ///< 徽章颜色ID
            uint32 BorderStyle = 0; ///< 边框样式ID
            uint32 BorderColor = 0; ///< 边框颜色ID
            uint32 BackgroundColor = 0; ///< 背景颜色ID
        };

        /**
         * @class QueryGuildInfoResponse
         * @brief 服务器响应公会信息查询的包
         *
         * 当服务器收到公会信息查询请求后，返回此包给客户端，
         * 包含公会的详细信息（名称、等级、徽章等）。
         */
        class QueryGuildInfoResponse final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数，初始化为公会查询响应包
             */
            QueryGuildInfoResponse();

            /**
             * @brief 序列化公会信息数据到网络包
             * @return 返回构建好的网络包指针
             *
             * 将公会ID、名称、等级信息和徽章信息写入网络包。
             */
            WorldPacket const* Write() override;

            uint32 GuildId = 0; ///< 查询的公会ID
            GuildInfo Info; ///< 公会详细信息
        };

        /**
         * @class GuildCreate
         * @brief 客户端请求创建公会的包
         *
         * 当玩家尝试创建新公会时发送此包。
         * 公会创建需要消耗一定金币并满足其他条件。
         */
        class GuildCreate final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildCreate(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_CREATE, std::move(packet)) { }

            /**
             * @brief 读取公会创建数据
             *
             * 从网络包中读取要创建的公会名称。
             */
            void Read() override;

            std::string GuildName; ///< 要创建的公会名称
        };

        /**
         * @class GuildGetInfo
         * @brief 客户端请求获取公会基础信息的包
         *
         * 当玩家打开公会信息界面时发送此包。
         * 服务器将返回公会名称、创建日期、成员数量等基础信息。
         */
        class GuildGetInfo final : ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildGetInfo(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_INFO, std::move(packet)) { }

            /**
             * @brief 读取数据（此包无额外数据）
             */
            void Read() override { }
        };

        /**
         * @class GuildInfoResponse
         * @brief 服务器响应公会基础信息的包
         *
         * 返回公会的基础统计信息，包括创建日期、成员数量和账户数量。
         * 在玩家查看公会详细信息时显示。
         */
        class GuildInfoResponse final : ServerPacket
        {
        public:
            /**
             * @brief 构造函数，初始化包大小为123字节
             */
            GuildInfoResponse() : ServerPacket(SMSG_GUILD_INFO, 123) { }

            /**
             * @brief 序列化公会基础信息到网络包
             * @return 返回构建好的网络包指针
             */
            WorldPacket const* Write() override;

            std::string GuildName; ///< 公会名称
            WowTime CreateDate; ///< 公会创建日期
            int32 NumMembers = 0; ///< 公会成员总数
            int32 NumAccounts = 0; ///< 公会账户总数（一个账户可能有多个角色）
        };

        /**
         * @class GuildGetRoster
         * @brief 客户端请求获取公会花名册的包
         *
         * 当玩家打开公会成员列表时发送此包。
         * 服务器将返回所有公会成员的详细信息列表。
         */
        class GuildGetRoster final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildGetRoster(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_ROSTER, std::move(packet)) { }

            /**
             * @brief 读取数据（此包无额外数据）
             */
            void Read() override { }
        };

        /**
         * @struct GuildRosterMemberData
         * @brief 公会成员详细数据结构
         *
         * 存储单个公会成员的所有信息，包括角色属性、公会贡献和状态。
         * 用于公会花名册响应包中传输成员列表数据。
         */
        struct GuildRosterMemberData
        {
            ObjectGuid Guid; ///< 成员角色GUID
            int64 WeeklyXP = 0; ///< 本周公会经验贡献
            int64 TotalXP = 0; ///< 总公会经验贡献
            int32 RankID = 0; ///< 公会等级ID
            int32 AreaID = 0; ///< 当前所在区域ID
            float LastSave = 0.0f; ///< 上次保存时间（离线时使用）
            std::string Name; ///< 角色名称
            std::string Note; ///< 成员备注（所有成员可见）
            std::string OfficerNote; ///< 官员备注（仅官员可见）
            uint8 Status = 0; ///< 在线状态标志
            uint8 Level = 0; ///< 角色等级
            uint8 ClassID = 0; ///< 职业ID
            uint8 Gender = 0; ///< 性别
        };

        /**
         * @struct GuildRankData
         * @brief 公会等级权限数据结构
         *
         * 存储某个公会等级的所有权限设置，包括金币提取限制和银行标签页权限。
         * 用于公会花名册响应包中传输等级权限信息。
         */
        struct GuildRankData
        {
            uint32 Flags = 0; ///< 等级权限标志位
            uint32 WithdrawGoldLimit = 0; ///< 每日金币提取上限
            uint32 TabFlags[GUILD_BANK_MAX_TABS]; ///< 每个银行标签页的权限标志
            uint32 TabWithdrawItemLimit[GUILD_BANK_MAX_TABS]; ///< 每个银行标签页的物品提取上限
        };

        /**
         * @class GuildRoster
         * @brief 服务器响应公会花名册的包
         *
         * 返回公会所有成员的详细列表，包括等级权限、欢迎文本和信息文本。
         * 这是公会界面中显示成员列表的主要数据来源。
         *
         * 性能注意：此包数据量较大，包含所有成员的详细信息，
         * 应避免频繁发送。
         */
        class GuildRoster final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数，初始化包基础大小
             */
            GuildRoster() : ServerPacket(SMSG_GUILD_ROSTER, 4 + 4 + 4 + 4) { }

            /**
             * @brief 序列化公会花名册数据到网络包
             * @return 返回构建好的网络包指针
             *
             * 按顺序写入：成员数量、欢迎文本、信息文本、等级数量、
             * 等级数据列表、成员数据列表。
             */
            WorldPacket const* Write() override;

            std::vector<GuildRosterMemberData> MemberData; ///< 成员数据列表
            std::vector<GuildRankData> RankData; ///< 等级数据列表
            std::string WelcomeText; ///< 公会欢迎文本（MOTD）
            std::string InfoText; ///< 公会信息文本
        };

        /**
         * @class GuildUpdateMotdText
         * @brief 客户端请求更新公会每日消息（MOTD）的包
         *
         * 当公会官员修改公会欢迎消息时发送此包。
         * MOTD会在所有公会成员登录时显示。
         */
        class GuildUpdateMotdText final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildUpdateMotdText(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_MOTD, std::move(packet)) { }

            /**
             * @brief 读取MOTD文本数据
             *
             * 从网络包中读取新的公会欢迎消息文本。
             * 文本最大长度128字符，禁止超链接。
             */
            void Read() override;

            String<128, Strings::NoHyperlinks> MotdText; ///< 新的公会每日消息文本
        };

        /**
         * @class GuildCommandResult
         * @brief 服务器返回公会命令执行结果的包
         *
         * 当客户端执行公会相关命令（如邀请、踢出、晋升等）后，
         * 服务器返回此包通知命令执行结果（成功或失败原因）。
         */
        class GuildCommandResult final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数，初始化包大小为9字节
             */
            GuildCommandResult() : ServerPacket(SMSG_GUILD_COMMAND_RESULT, 9) { }

            /**
             * @brief 序列化命令结果数据到网络包
             * @return 返回构建好的网络包指针
             */
            WorldPacket const* Write() override;

            std::string Name; ///< 相关玩家名称
            int32 Result = 0; ///< 命令执行结果码（0=成功，其他=错误码）
            int32 Command = 0; ///< 命令类型ID
        };

        /**
         * @class AcceptGuildInvite
         * @brief 客户端接受公会邀请的包
         *
         * 当玩家接受公会邀请时发送此包。
         * 玩家将成为公会成员。
         */
        class AcceptGuildInvite final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            AcceptGuildInvite(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_ACCEPT, std::move(packet)) { }

            /**
             * @brief 读取数据（此包无额外数据）
             */
            void Read() override { }
        };

        /**
         * @class GuildDeclineInvitation
         * @brief 客户端拒绝公会邀请的包
         *
         * 当玩家拒绝公会邀请时发送此包。
         * 邀请者会收到拒绝通知。
         */
        class GuildDeclineInvitation final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildDeclineInvitation(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_DECLINE, std::move(packet)) { }

            /**
             * @brief 读取数据（此包无额外数据）
             */
            void Read() override { }
        };

        /**
         * @class GuildInviteByName
         * @brief 客户端请求邀请玩家加入公会的包
         *
         * 当公会官员邀请某个玩家加入公会时发送此包。
         * 目标玩家会收到邀请提示。
         */
        class GuildInviteByName final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildInviteByName(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_INVITE, std::move(packet)) { }

            /**
             * @brief 读取被邀请玩家名称
             *
             * 从网络包中读取要邀请的玩家角色名称。
             */
            void Read() override;

            String<48> Name; ///< 被邀请玩家的角色名称
        };

        /**
         * @class GuildInvite
         * @brief 服务器发送公会邀请通知的包
         *
         * 当玩家收到公会邀请时，服务器发送此包给被邀请者，
         * 包含邀请者名称和公会名称。
         */
        class GuildInvite final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数，初始化包大小为144字节
             */
            GuildInvite() : ServerPacket(SMSG_GUILD_INVITE, 144) { }

            /**
             * @brief 序列化邀请信息到网络包
             * @return 返回构建好的网络包指针
             */
            WorldPacket const* Write() override;

            std::string InviterName; ///< 邀请者名称
            std::string GuildName; ///< 公会名称
        };

        /**
         * @class GuildEvent
         * @brief 服务器广播公会事件的包
         *
         * 当公会发生特定事件时（如成员加入、离开、上线、下线等），
         * 服务器向所有在线公会成员广播此事件通知。
         */
        class GuildEvent final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             */
            GuildEvent() : ServerPacket(SMSG_GUILD_EVENT) { }

            /**
             * @brief 序列化公会事件数据到网络包
             * @return 返回构建好的网络包指针
             *
             * 根据事件类型写入不同的数据：
             * - 加入/离开/上线/下线事件：包含玩家GUID
             * - 其他事件：包含参数字符串列表
             */
            WorldPacket const* Write() override;

            uint8 Type = 0; ///< 事件类型（GE_JOINED, GE_LEFT等）
            Array<std::string_view, 3> Params; ///< 事件参数列表（最多3个）
            ObjectGuid Guid; ///< 相关玩家GUID
        };

        /**
         * @struct GuildEventEntry
         * @brief 公会事件日志条目数据结构
         *
         * 存储公会事件日志的单条记录，包括成员加入、离开、晋升、降级等事件。
         * 用于公会事件日志查询结果中返回历史记录。
         */
        struct GuildEventEntry
        {
            ObjectGuid PlayerGUID; ///< 事件相关玩家GUID
            ObjectGuid OtherGUID; ///< 其他相关玩家GUID（如晋升操作的执行者）
            uint8 TransactionType = 0; ///< 事件类型（加入、离开、晋升、降级等）
            uint8 RankID = 0; ///< 相关等级ID（晋升/降级事件使用）
            uint32 TransactionDate = 0; ///< 事件发生时间戳
        };

        /**
         * @class GuildEventLogQuery
         * @brief 客户端请求查询公会事件日志的包
         *
         * 当玩家查看公会事件日志时发送此包。
         * 服务器将返回最近的公会事件记录列表。
         */
        class GuildEventLogQuery final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildEventLogQuery(WorldPacket&& packet) : ClientPacket(MSG_GUILD_EVENT_LOG_QUERY, std::move(packet)) { }

            /**
             * @brief 读取数据（此包无额外数据）
             */
            void Read() override { }
        };

        /**
         * @class GuildEventLogQueryResults
         * @brief 服务器返回公会事件日志查询结果的包
         *
         * 返回公会事件日志的历史记录列表，包括成员加入、离开、晋升、降级等事件。
         * 日志条目数量受配置限制。
         */
        class GuildEventLogQueryResults final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数，初始化包大小为4字节
             */
            GuildEventLogQueryResults() : ServerPacket(MSG_GUILD_EVENT_LOG_QUERY, 4) { }

            /**
             * @brief 序列化事件日志数据到网络包
             * @return 返回构建好的网络包指针
             *
             * 遍历所有事件条目，根据事件类型写入不同的数据字段。
             */
            WorldPacket const* Write() override;

            std::vector<GuildEventEntry> Entry; ///< 事件日志条目列表
        };

        /**
         * @class GuildPermissionsQuery
         * @brief 客户端请求查询公会权限的包
         *
         * 当玩家查看自己或他人的公会等级权限时发送此包。
         * 服务器将返回该等级的详细权限设置。
         */
        class GuildPermissionsQuery final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildPermissionsQuery(WorldPacket&& packet) : ClientPacket(MSG_GUILD_PERMISSIONS, std::move(packet)) { }

            /**
             * @brief 读取数据（此包无额外数据）
             */
            void Read() override { }
        };

        /**
         * @class GuildPermissionsQueryResults
         * @brief 服务器返回公会权限查询结果的包
         *
         * 返回指定公会等级的所有权限设置，包括金币提取限制、
         * 每个银行标签页的访问权限和物品提取限制。
         */
        class GuildPermissionsQueryResults final : public ServerPacket
        {
        public:
            /**
             * @struct GuildRankTabPermissions
             * @brief 公会等级银行标签页权限数据结构
             *
             * 存储某个银行标签页的权限设置，包括访问权限和物品提取限制。
             */
            struct GuildRankTabPermissions
            {
                int32 Flags = 0; ///< 标签页权限标志
                int32 WithdrawItemLimit = 0; ///< 物品提取上限
            };

            /**
             * @brief 构造函数，初始化包大小为20字节
             */
            GuildPermissionsQueryResults() : ServerPacket(MSG_GUILD_PERMISSIONS, 20) { }

            /**
             * @brief 序列化权限数据到网络包
             * @return 返回构建好的网络包指针
             */
            WorldPacket const* Write() override;

            int8 NumTabs = 0; ///< 银行标签页总数
            int32 WithdrawGoldLimit = 0; ///< 金币提取上限
            int32 Flags = 0; ///< 总体权限标志
            uint32 RankID = 0; ///< 等级ID
            std::array<GuildRankTabPermissions, GUILD_BANK_MAX_TABS> Tab; ///< 每个标签页的权限设置
        };

        /**
         * @class GuildSetRankPermissions
         * @brief 客户端请求设置公会等级权限的包
         *
         * 当公会会长修改某个等级的权限时发送此包。
         * 包括等级名称、总体权限、金币限制和每个银行标签页的权限。
         */
        class GuildSetRankPermissions final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildSetRankPermissions(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_RANK, std::move(packet)) { }

            /**
             * @brief 读取等级权限设置数据
             *
             * 从网络包中读取等级ID、权限标志、等级名称、金币限制
             * 以及每个银行标签页的权限设置。
             */
            void Read() override;

            uint32 RankID = 0; ///< 要修改的等级ID
            uint32 WithdrawGoldLimit = 0; ///< 金币提取上限
            uint32 Flags = 0; ///< 总体权限标志
            uint32 TabFlags[GUILD_BANK_MAX_TABS]; ///< 每个银行标签页的权限标志
            uint32 TabWithdrawItemLimit[GUILD_BANK_MAX_TABS]; ///< 每个银行标签页的物品提取上限
            String<15, Strings::NoHyperlinks> RankName; ///< 等级名称（最多15字符，禁止超链接）
        };

        /**
         * @class GuildAddRank
         * @brief 客户端请求添加公会等级的包
         *
         * 当公会会长创建新的公会等级时发送此包。
         * 公会等级数量有上限限制。
         */
        class GuildAddRank final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildAddRank(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_ADD_RANK, std::move(packet)) { }

            /**
             * @brief 读取新等级名称
             */
            void Read() override;

            String<15, Strings::NoHyperlinks> Name; ///< 新等级名称（最多15字符，禁止超链接）
        };

        /**
         * @class GuildDeleteRank
         * @brief 客户端请求删除公会等级的包
         *
         * 当公会会长删除某个公会等级时发送此包。
         * 不能删除仍有成员的等级。
         */
        class GuildDeleteRank final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildDeleteRank(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_DEL_RANK, std::move(packet)) { }

            /**
             * @brief 读取数据（此包无额外数据）
             */
            void Read() override { }
        };

        /**
         * @class GuildUpdateInfoText
         * @brief 客户端请求更新公会信息文本的包
         *
         * 当公会官员修改公会详细信息时发送此包。
         * 公会信息文本在查看公会详情时显示。
         */
        class GuildUpdateInfoText final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildUpdateInfoText(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_INFO_TEXT, std::move(packet)) { }

            /**
             * @brief 读取公会信息文本
             */
            void Read() override;

            String<500, Strings::NoHyperlinks> InfoText; ///< 公会信息文本（最多500字符，禁止超链接）
        };

        /**
         * @class GuildSetMemberNote
         * @brief 客户端请求设置成员备注的包
         *
         * 当公会官员或成员修改某个成员的备注时发送此包。
         * 备注分为普通备注（所有成员可见）和官员备注（仅官员可见）。
         */
        class GuildSetMemberNote final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildSetMemberNote(WorldPacket&& packet) : ClientPacket(std::move(packet)) { }

            /**
             * @brief 读取成员备注数据
             *
             * 从网络包中读取目标成员名称和备注文本。
             */
            void Read() override;

            std::string NoteeName; ///< 目标成员名称
            String<31, Strings::NoHyperlinks> Note; ///< 备注文本（最多31字符，禁止超链接）
        };

        /**
         * @class GuildDelete
         * @brief 客户端请求解散公会的包
         *
         * 当公会会长解散公会时发送此包。
         * 解散后所有成员将被移出公会，公会数据将被删除。
         */
        class GuildDelete final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildDelete(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_DISBAND, std::move(packet)) { }

            /**
             * @brief 读取数据（此包无额外数据）
             */
            void Read() override { }
        };

        /**
         * @class GuildDemoteMember
         * @brief 客户端请求降级公会成员的包
         *
         * 当有权限的公会官员降低某个成员的等级时发送此包。
         * 成员等级会下降一级。
         */
        class GuildDemoteMember final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildDemoteMember(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_DEMOTE, std::move(packet)) { }

            /**
             * @brief 读取要降级的成员名称
             */
            void Read() override;

            std::string Demotee; ///< 要降级的成员名称
        };

        /**
         * @class GuildPromoteMember
         * @brief 客户端请求晋升公会成员的包
         *
         * 当有权限的公会官员提升某个成员的等级时发送此包。
         * 成员等级会上升一级。
         */
        class GuildPromoteMember final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildPromoteMember(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_PROMOTE, std::move(packet)) { }

            /**
             * @brief 读取要晋升的成员名称
             */
            void Read() override;

            std::string Promotee; ///< 要晋升的成员名称
        };

        /**
         * @class GuildOfficerRemoveMember
         * @brief 客户端请求踢出公会成员的包
         *
         * 当有权限的公会官员将某个成员踢出公会时发送此包。
         * 被踢出的成员会立即失去公会成员身份。
         */
        class GuildOfficerRemoveMember : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildOfficerRemoveMember(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_REMOVE, std::move(packet)) { }

            /**
             * @brief 读取要踢出的成员名称
             */
            void Read() override;

            std::string Removee; ///< 要踢出的成员名称
        };

        /**
         * @class GuildLeave
         * @brief 客户端请求退出公会的包
         *
         * 当玩家主动退出公会时发送此包。
         * 会长不能退出公会，只能解散或转让会长。
         */
        class GuildLeave final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildLeave(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_LEAVE, std::move(packet)) { }

            /**
             * @brief 读取数据（此包无额外数据）
             */
            void Read() override { }
        };

        /**
         * @class GuildBankActivate
         * @brief 客户端请求激活公会银行界面的包
         *
         * 当玩家与公会银行NPC交互打开银行界面时发送此包。
         * 服务器会返回银行标签页和物品信息。
         */
        class GuildBankActivate final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildBankActivate(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_BANKER_ACTIVATE, std::move(packet)) { }

            /**
             * @brief 读取银行激活数据
             *
             * 从网络包中读取银行NPC的GUID和是否需要完整更新的标志。
             */
            void Read() override;

            ObjectGuid Banker; ///< 银行NPC的GUID
            bool FullUpdate = false; ///< 是否需要完整更新（首次打开时为true）
        };

        /**
         * @class GuildBankBuyTab
         * @brief 客户端请求购买公会银行标签页的包
         *
         * 当公会购买新的银行标签页时发送此包。
         * 购买需要消耗金币，价格随已购买的标签页数量递增。
         */
        class GuildBankBuyTab final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildBankBuyTab(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_BANK_BUY_TAB, std::move(packet)) { }

            /**
             * @brief 读取购买标签页数据
             */
            void Read() override;

            ObjectGuid Banker; ///< 银行NPC的GUID
            uint8 BankTab = 0; ///< 要购买的标签页编号
        };

        /**
         * @class GuildBankUpdateTab
         * @brief 客户端请求更新公会银行标签页信息的包
         *
         * 当有权限的成员修改某个银行标签页的名称或图标时发送此包。
         */
        class GuildBankUpdateTab final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildBankUpdateTab(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_BANK_UPDATE_TAB, std::move(packet)) { }

            /**
             * @brief 读取标签页更新数据
             */
            void Read() override;

            ObjectGuid Banker; ///< 银行NPC的GUID
            uint8 BankTab = 0; ///< 要更新的标签页编号
            String<16, Strings::NoHyperlinks> Name; ///< 标签页名称（最多16字符，禁止超链接）
            String<100> Icon; ///< 标签页图标路径（最多100字符）
        };

        /**
         * @class GuildBankDepositMoney
         * @brief 客户端请求向公会银行存入金币的包
         *
         * 当玩家向公会银行存入金币时发送此包。
         * 存入的金币将成为公会财产。
         */
        class GuildBankDepositMoney final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildBankDepositMoney(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_BANK_DEPOSIT_MONEY, std::move(packet)) { }

            /**
             * @brief 读取存款数据
             */
            void Read() override;

            ObjectGuid Banker; ///< 银行NPC的GUID
            uint32 Money = 0; ///< 要存入的金币数量
        };

        /**
         * @class GuildBankQueryTab
         * @brief 客户端请求查询公会银行标签页内容的包
         *
         * 当玩家切换银行标签页时发送此包。
         * 服务器会返回该标签页的所有物品信息。
         */
        class GuildBankQueryTab final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildBankQueryTab(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_BANK_QUERY_TAB, std::move(packet)) { }

            /**
             * @brief 读取标签页查询数据
             */
            void Read() override;

            ObjectGuid Banker; ///< 银行NPC的GUID
            uint8 Tab = 0; ///< 要查询的标签页编号
            bool FullUpdate = false; ///< 是否需要完整更新
        };

        /**
         * @class GuildBankRemainingWithdrawMoneyQuery
         * @brief 客户端请求查询剩余金币提取额度的包
         *
         * 当玩家需要查看自己今日还能提取多少金币时发送此包。
         * 每个等级都有每日金币提取上限。
         */
        class GuildBankRemainingWithdrawMoneyQuery final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildBankRemainingWithdrawMoneyQuery(WorldPacket&& packet) : ClientPacket(MSG_GUILD_BANK_MONEY_WITHDRAWN, std::move(packet)) { }

            /**
             * @brief 读取数据（此包无额外数据）
             */
            void Read() override { }
        };

        /**
         * @class GuildBankRemainingWithdrawMoney
         * @brief 服务器返回剩余金币提取额度的包
         *
         * 返回玩家今日还可以从公会银行提取的金币数量。
         */
        class GuildBankRemainingWithdrawMoney final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数，初始化包大小为8字节
             */
            GuildBankRemainingWithdrawMoney() : ServerPacket(MSG_GUILD_BANK_MONEY_WITHDRAWN, 8) { }

            /**
             * @brief 序列化剩余提取额度到网络包
             * @return 返回构建好的网络包指针
             */
            WorldPacket const* Write() override;

            int32 RemainingWithdrawMoney = 0; ///< 剩余可提取金币数量
        };

        /**
         * @class GuildBankWithdrawMoney
         * @brief 客户端请求从公会银行取出金币的包
         *
         * 当玩家从公会银行提取金币时发送此包。
         * 提取受每日额度限制，并记录到银行日志。
         */
        class GuildBankWithdrawMoney final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildBankWithdrawMoney(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_BANK_WITHDRAW_MONEY, std::move(packet)) { }

            /**
             * @brief 读取取款数据
             */
            void Read() override;

            ObjectGuid Banker; ///< 银行NPC的GUID
            uint32 Money = 0; ///< 要取出的金币数量
        };

        /**
         * @struct GuildBankSocketEnchant
         * @brief 公会银行物品宝石镶嵌数据结构
         *
         * 存储物品上某个插槽的宝石镶嵌信息。
         */
        struct GuildBankSocketEnchant
        {
            uint8 SocketIndex = 0; ///< 插槽索引（0-2）
            int32 SocketEnchantID = 0; ///< 宝石附魔ID
        };

        /**
         * @struct GuildBankItemInfo
         * @brief 公会银行物品详细信息数据结构
         *
         * 存储公会银行中某个物品槽位的所有信息，包括物品ID、数量、
         * 附魔、宝石镶嵌等详细属性。
         */
        struct GuildBankItemInfo
        {
            uint32 ItemID = 0; ///< 物品ID（0表示空槽位）
            int32 RandomPropertiesSeed = 0; ///< 随机属性种子
            int32 RandomPropertiesID = 0; ///< 随机属性ID
            uint8 Slot = 0; ///< 槽位编号
            int32 Count = 0; ///< 物品堆叠数量
            int32 EnchantmentID = 0; ///< 附魔ID
            int32 Charges = 0; ///< 剩余使用次数
            int32 Flags = 0; ///< 物品标志位
            std::vector<GuildBankSocketEnchant> SocketEnchant; ///< 宝石镶嵌列表
        };

        /**
         * @struct GuildBankTabInfo
         * @brief 公会银行标签页信息数据结构
         *
         * 存储某个银行标签页的基本信息，包括名称和图标。
         */
        struct GuildBankTabInfo
        {
            std::string Name; ///< 标签页名称
            std::string Icon; ///< 标签页图标路径
        };

        /**
         * @class GuildBankQueryResults
         * @brief 服务器返回公会银行查询结果的包
         *
         * 返回指定银行标签页的物品列表和详细信息。
         * 首次打开银行时（FullUpdate=true）会包含所有标签页的基本信息。
         *
         * 性能注意：此包数据量可能较大，包含大量物品信息，
         * 应避免频繁请求完整更新。
         */
        class GuildBankQueryResults final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数，初始化包大小为25字节
             */
            GuildBankQueryResults() : ServerPacket(SMSG_GUILD_BANK_LIST, 25) { }

            /**
             * @brief 序列化银行查询结果到网络包
             * @return 返回构建好的网络包指针
             *
             * 写入顺序：公会金币、标签页编号、剩余提取次数、完整更新标志、
             * 标签页信息（仅首次）、物品信息列表。
             */
            WorldPacket const* Write() override;

            std::vector<GuildBankItemInfo> ItemInfo; ///< 物品信息列表
            std::vector<GuildBankTabInfo> TabInfo; ///< 标签页信息列表
            int32 WithdrawalsRemaining = 0; ///< 剩余物品提取次数
            uint8 Tab = 0; ///< 当前标签页编号
            uint64 Money = 0; ///< 公会银行金币总数
            bool FullUpdate = false; ///< 是否为完整更新（首次打开）

            /**
             * @brief 设置剩余提取次数
             * @param withdrawalsRemaining 剩余提取次数
             *
             * 此方法会更新已写入网络包中的提取次数字段。
             * 用于在构建包的过程中动态更新数据。
             */
            void SetWithdrawalsRemaining(int32 withdrawalsRemaining);

        private:
            std::size_t _withdrawalsRemainingPos = 0; ///< 剩余提取次数字段在包中的位置
        };

        /**
         * @class GuildBankSwapItems
         * @brief 客户端请求交换公会银行物品的包
         *
         * 处理公会银行中的物品交换操作，包括：
         * - 银行内物品移动
         * - 玩家背包与银行之间的物品转移
         * - 物品拆分堆叠
         * - 物品自动存储
         *
         * 包结构根据操作类型（BankOnly、AutoStore）有所不同。
         */
        class GuildBankSwapItems final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildBankSwapItems(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_BANK_SWAP_ITEMS, std::move(packet)) { }

            /**
             * @brief 读取物品交换数据
             *
             * 根据BankOnly标志读取不同的数据结构：
             * - BankOnly=true: 银行内物品移动
             * - BankOnly=false: 玩家与银行之间的物品交换
             */
            void Read() override;

            ObjectGuid Banker; ///< 银行NPC的GUID
            int32 StackCount = 0; ///< 堆叠数量（拆分物品时使用）
            int32 BankItemCount = 0; ///< 银行物品数量
            uint32 ItemID = 0; ///< 源物品ID
            uint32 ItemID1 = 0; ///< 目标物品ID
            uint8 ToSlot = 0; ///< 目标槽位
            uint8 BankSlot = 0; ///< 银行槽位
            uint8 BankSlot1 = 0; ///< 银行槽位1（银行内移动时使用）
            uint8 BankTab = 0; ///< 银行标签页
            uint8 BankTab1 = 0; ///< 银行标签页1（银行内移动时使用）
            uint8 ContainerSlot = 0; ///< 容器槽位（背包）
            uint8 ContainerItemSlot = 0; ///< 容器物品槽位（背包）
            bool AutoStore = false; ///< 是否自动存储
            bool BankOnly = false; ///< 是否仅在银行内操作
        };

        /**
         * @class GuildBankLogQuery
         * @brief 客户端请求查询公会银行日志的包
         *
         * 当玩家查看公会银行操作日志时发送此包。
         * 服务器将返回指定标签页的最近操作记录。
         */
        class GuildBankLogQuery final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildBankLogQuery(WorldPacket&& packet) : ClientPacket(MSG_GUILD_BANK_LOG_QUERY, std::move(packet)) { }

            /**
             * @brief 读取要查询的标签页编号
             */
            void Read() override;

            uint8 Tab = 0; ///< 要查询的标签页编号（255表示金币日志）
        };

        /**
         * @struct GuildBankLogEntry
         * @brief 公会银行日志条目数据结构
         *
         * 存储单条银行操作日志记录，包括金币存取、物品存取、
         * 物品移动等操作的详细信息。
         */
        struct GuildBankLogEntry
        {
            ObjectGuid PlayerGUID; ///< 操作玩家GUID
            uint32 TimeOffset = 0; ///< 时间偏移（相对当前时间的秒数）
            int8 EntryType = 0; ///< 日志类型（存入、取出、移动等）
            uint32 Money = 0; ///< 金币数量（金币操作使用）
            int32 ItemID = 0; ///< 物品ID（物品操作使用）
            int32 Count = 0; ///< 物品数量
            int8 OtherTab = 0; ///< 其他标签页编号（物品移动时使用）
        };

        /**
         * @class GuildBankLogQueryResults
         * @brief 服务器返回公会银行日志查询结果的包
         *
         * 返回指定标签页或金币操作的日志记录列表。
         * 日志条目数量受配置限制。
         */
        class GuildBankLogQueryResults final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数，初始化包大小为25字节
             */
            GuildBankLogQueryResults() : ServerPacket(MSG_GUILD_BANK_LOG_QUERY, 25) { }

            /**
             * @brief 序列化银行日志数据到网络包
             * @return 返回构建好的网络包指针
             *
             * 根据日志类型写入不同的数据字段（金币或物品信息）。
             */
            WorldPacket const* Write() override;

            uint8 Tab = 0; ///< 查询的标签页编号
            std::vector<GuildBankLogEntry> Entry; ///< 日志条目列表
        };

        /**
         * @class GuildBankTextQuery
         * @brief 客户端请求查询公会银行标签页文本的包
         *
         * 当玩家查看某个银行标签页的备注文本时发送此包。
         * 每个标签页都可以设置一个备注文本。
         */
        class GuildBankTextQuery final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildBankTextQuery(WorldPacket&& packet) : ClientPacket(MSG_QUERY_GUILD_BANK_TEXT, std::move(packet)) { }

            /**
             * @brief 读取要查询的标签页编号
             */
            void Read() override;

            uint8 Tab = 0; ///< 要查询的标签页编号
        };

        /**
         * @class GuildBankTextQueryResult
         * @brief 服务器返回公会银行标签页文本的包
         *
         * 返回指定银行标签页的备注文本内容。
         */
        class GuildBankTextQueryResult : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数，初始化包大小
             */
            GuildBankTextQueryResult() : ServerPacket(MSG_QUERY_GUILD_BANK_TEXT, 4 + 2) { }

            /**
             * @brief 序列化标签页文本到网络包
             * @return 返回构建好的网络包指针
             */
            WorldPacket const* Write() override;

            uint8 Tab = 0; ///< 标签页编号
            std::string Text; ///< 标签页备注文本
        };

        /**
         * @class GuildBankSetTabText
         * @brief 客户端请求设置公会银行标签页文本的包
         *
         * 当有权限的成员修改某个银行标签页的备注文本时发送此包。
         */
        class GuildBankSetTabText final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildBankSetTabText(WorldPacket&& packet) : ClientPacket(CMSG_SET_GUILD_BANK_TEXT, std::move(packet)) { }

            /**
             * @brief 读取标签页文本设置数据
             */
            void Read() override;

            uint8 Tab = 0; ///< 要设置的标签页编号
            String<500, Strings::NoHyperlinks> TabText; ///< 标签页备注文本（最多500字符，禁止超链接）
        };

        /**
         * @class GuildSetGuildMaster
         * @brief 客户端请求转让会长职位的包
         *
         * 当公会会长将会长职位转让给其他成员时发送此包。
         * 原会长会降级为普通成员。
         */
        class GuildSetGuildMaster final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            GuildSetGuildMaster(WorldPacket&& packet) : ClientPacket(CMSG_GUILD_LEADER, std::move(packet)) { }

            /**
             * @brief 读取新会长名称
             */
            void Read() override;

            std::string NewMasterName; ///< 新会长的角色名称
        };

        /**
         * @class SaveGuildEmblem
         * @brief 客户端请求保存公会徽章的包
         *
         * 当公会会长修改公会徽章样式时发送此包。
         * 徽章包括边框、图案、背景颜色等元素。
         */
        class SaveGuildEmblem final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            SaveGuildEmblem(WorldPacket&& packet) : ClientPacket(MSG_SAVE_GUILD_EMBLEM, std::move(packet)) { }

            /**
             * @brief 读取徽章样式数据
             *
             * 从网络包中读取徽章的各个样式元素ID。
             */
            void Read() override;

            ObjectGuid Vendor; ///< 徽章设计NPC的GUID
            int32 BStyle = 0; ///< 边框样式ID
            int32 EStyle = 0; ///< 徽章图案样式ID
            int32 BColor = 0; ///< 边框颜色ID
            int32 EColor = 0; ///< 徽章图案颜色ID
            int32 Bg = 0; ///< 背景颜色ID
        };

        /**
         * @class PlayerSaveGuildEmblem
         * @brief 服务器返回保存公会徽章结果的包
         *
         * 返回徽章保存操作的结果（成功或错误码）。
         */
        class PlayerSaveGuildEmblem final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数，初始化包大小为4字节
             */
            PlayerSaveGuildEmblem() : ServerPacket(MSG_SAVE_GUILD_EMBLEM, 4) { }

            /**
             * @brief 序列化保存结果到网络包
             * @return 返回构建好的网络包指针
             */
            WorldPacket const* Write() override;

            int32 Error = 0; ///< 错误码（0=成功，其他=错误）
        };
    }
}

/**
 * @brief 序列化公会成员数据到字节缓冲区
 * @param data 字节缓冲区引用
 * @param rosterMemberData 公会成员数据
 * @return 返回字节缓冲区引用
 *
 * 将成员GUID、状态、名称、等级、职业、性别、区域、
 * 上次保存时间、备注等信息写入缓冲区。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Guild::GuildRosterMemberData const& rosterMemberData);

/**
 * @brief 序列化公会等级数据到字节缓冲区
 * @param data 字节缓冲区引用
 * @param rankData 公会等级数据
 * @return 返回字节缓冲区引用
 *
 * 将等级权限标志、金币提取限制、每个银行标签页的权限等写入缓冲区。
 */
ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::Guild::GuildRankData const& rankData);

#endif // GuildPackets_h__
