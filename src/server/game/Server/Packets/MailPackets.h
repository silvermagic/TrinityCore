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
 * @file MailPackets.h
 * @brief 邮件系统网络包定义模块
 *
 * 本模块定义了游戏中邮件系统相关的所有客户端和服务器网络包结构。
 * 邮件系统允许玩家之间发送和接收邮件,包括文本、物品和金币等附件。
 *
 * 主要功能包括:
 * - 邮件列表的查询和展示
 * - 发送新邮件
 * - 邮件附件(物品/金币)的收取
 * - 邮件的删除、标记已读、退回等操作
 * - 新邮件到达通知
 * - 查询下一封邮件的到达时间
 *
 * 网络包结构:
 * - 客户端包(ClientPacket): 玩家发送到服务器的请求
 * - 服务器包(ServerPacket): 服务器返回给客户端的响应
 */

#ifndef MailPackets_h__
#define MailPackets_h__

#include "Packet.h"
#include "ItemDefines.h"
#include "Mail.h"
#include "Optional.h"
#include "PacketUtilities.h"

namespace WorldPackets
{
    /**
     * @brief 邮件系统网络包命名空间
     *
     * 包含所有邮件相关的网络包定义,用于客户端和服务器之间的邮件通信。
     */
    namespace Mail
    {
        /**
         * @struct MailAttachedItem
         * @brief 邮件附件物品信息结构
         *
         * 用于描述邮件中的一个附件物品,包括物品的基本属性、附魔信息、耐久度等。
         * 当玩家查看邮件列表或打开邮件时,此结构会序列化并发送给客户端。
         */
        struct MailAttachedItem
        {
            /**
             * @brief 构造函数
             * @param item 物品对象指针
             * @param pos 附件在邮件中的位置索引
             *
             * 从物品对象中提取所有必要信息,包括GUID、模板ID、随机属性、附魔等。
             */
            MailAttachedItem(::Item const* item, uint8 pos);

            /**
             * @brief 获取序列化后的数据包大小
             * @return 固定的数据包字节数
             *
             * 用于在发送前计算数据包大小,防止超出网络包大小限制。
             * 该值为编译时常量,提高性能。
             */
            static constexpr std::size_t GetPacketSize()
            {
                return sizeof(uint8) + sizeof(int32) + sizeof(int32) + MAX_INSPECTED_ENCHANTMENT_SLOT * (sizeof(int32) + sizeof(int32) + sizeof(int32))
                    + sizeof(int32) + sizeof(int32) + sizeof(int32) + sizeof(int32) + sizeof(uint32) + sizeof(int32) + sizeof(bool);
            }

            uint8 Position = 0;                                          ///< 附件在邮件中的位置索引(0-11)
            int32 AttachID = 0;                                          ///< 附件唯一标识符(物品GUID的低32位)
            int32 ItemID = 0;                                            ///< 物品模板ID(对应item_template表)
            int32 RandomPropertiesSeed = 0;                              ///< 随机属性种子(用于随机后缀物品)
            int32 RandomPropertiesID = 0;                                ///< 随机属性ID(ItemRandomProperties表)
            int32 Count = 0;                                             ///< 物品堆叠数量
            int32 Charges = 0;                                           ///< 物品剩余使用次数(消耗品)
            uint32 MaxDurability = 0;                                    ///< 最大耐久度(装备类物品)
            int32 Durability = 0;                                        ///< 当前耐久度(装备类物品)
            bool Unlocked = false;                                       ///< 物品是否已解锁(无需钥匙打开)

            /// 附魔ID数组,索引对应EnchantmentSlot枚举
            std::array<uint32, MAX_INSPECTED_ENCHANTMENT_SLOT> EnchantmentID = { };
            /// 附魔持续时间数组(秒)
            std::array<uint32, MAX_INSPECTED_ENCHANTMENT_SLOT> EnchantmentDuration = { };
            /// 附魔剩余使用次数数组
            std::array<uint32, MAX_INSPECTED_ENCHANTMENT_SLOT> EnchantmentCharges = { };
        };

        /**
         * @struct MailListEntry
         * @brief 邮件列表条目结构
         *
         * 表示邮件列表中的一封邮件信息,包括发送者、主题、正文、附件等。
         * 用于MailListResult包,向客户端展示玩家的收件箱内容。
         */
        struct MailListEntry
        {
            /**
             * @brief 构造函数
             * @param mail 邮件对象指针
             * @param player 接收邮件的玩家对象
             *
             * 从邮件对象和玩家对象中提取邮件信息,包括发送者信息、附件物品等。
             * 根据邮件类型(玩家、NPC、拍卖行等)填充不同的发送者字段。
             */
            MailListEntry(::Mail const* mail, ::Player* player);

            /**
             * @brief 计算序列化后的数据包大小
             * @return 数据包字节数
             *
             * 动态计算包括主题、正文和附件在内的总大小,
             * 用于检测是否超出网络包大小限制。
             */
            std::size_t GetPacketSize() const;

            int32 MailID = 0;                                           ///< 邮件唯一ID
            uint8 SenderType = 0;                                       ///< 发送者类型(MAIL_NORMAL等枚举)
            Optional<ObjectGuid> SenderCharacter;                       ///< 发送者角色GUID(玩家邮件)
            Optional<uint32> AltSenderID;                               ///< 替代发送者ID(NPC、拍卖行等)
            uint32 Cod = 0;                                             ///< 货到付款金额(Cash On Delivery)
            int32 PackageID = 0;                                        ///< 包裹ID(未使用)
            int32 StationeryID = 0;                                     ///< 信纸类型ID(定义邮件外观)
            uint32 SentMoney = 0;                                       ///< 邮件附带的金币数量
            int32 Flags = 0;                                            ///< 邮件状态标志(已读、已回复等)
            float DaysLeft = 0.0f;                                      ///< 剩余天数(过期时间倒计时)
            int32 MailTemplateID = 0;                                   ///< 邮件模板ID(系统邮件)
            std::string_view Subject;                                   ///< 邮件主题(字符串视图,不拥有内存)
            std::string_view Body;                                      ///< 邮件正文(字符串视图,不拥有内存)
            std::vector<MailAttachedItem> Attachments;                  ///< 附件物品列表(最多12个)
        };

        /**
         * @class MailGetList
         * @brief 客户端请求获取邮件列表
         *
         * 当玩家打开邮箱时发送此包,请求服务器返回该玩家的所有邮件列表。
         * 服务器收到后会查询数据库并返回MailListResult包。
         *
         * 继承自ClientPacket,表示客户端发送的请求包。
         */
        class MailGetList final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 原始网络包数据
             */
            MailGetList(WorldPacket&& packet) : ClientPacket(CMSG_GET_MAIL_LIST, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络包中解析邮箱对象的GUID。
             */
            void Read() override;

            ObjectGuid Mailbox;                                         ///< 邮箱对象的GUID(信箱GameObject)
        };

        /**
         * @class MailListResult
         * @brief 服务器返回邮件列表结果
         *
         * 响应MailGetList请求,返回玩家的邮件列表。
         * 包含邮件总数和详细邮件信息列表。
         *
         * 注意事项:
         * - 单个包最多包含50封邮件(客户端限制)
         * - 包大小不能超过INT16_MAX(约32KB)
         * - 如果邮件过多会分批发送
         *
         * 继承自ServerPacket,表示服务器发送的响应包。
         */
        class MailListResult final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器包,预分配空间以提高性能。
             */
            MailListResult();

            /**
             * @brief 序列化数据包
             * @return 指向序列化后的WorldPacket指针
             *
             * 将TotalNumRecords和Mails列表写入网络包。
             * 在构造时已预填充部分数据,此处仅更新计数字段。
             */
            WorldPacket const* Write() override;

            /**
             * @brief 添加邮件到列表
             * @param mail 邮件对象指针
             * @param player 接收邮件的玩家对象
             *
             * 将一封邮件添加到返回列表中。
             * 会自动检测包大小和邮件数量限制,超出限制时停止添加。
             *
             * 性能注意:
             * - 每次添加都会检查包大小,避免内存重新分配
             * - TotalNumRecords会计数所有邮件,即使未包含在当前包中
             */
            void AddMail(::Mail const* mail, Player* player);

            int32 TotalNumRecords = 0;                                  ///< 邮件总数(包括未在当前包中的邮件)
            std::vector<MailListEntry> Mails;                           ///< 邮件列表(当前包包含的邮件)

        private:
            bool _maxPacketSizeReached = false;                         ///< 是否已达到包大小限制
        };

        /**
         * @class MailCreateTextItem
         * @brief 客户端请求创建文本物品
         *
         * 玩家右键点击邮件中的文本内容时发送此包,
         * 请求将邮件正文转换为一个可阅读的物品(如信件、书籍等)。
         *
         * 用途:
         * - 玩家可以保存重要的邮件内容
         * - 系统邮件可以包含故事剧情文本
         *
         * 继承自ClientPacket,表示客户端发送的请求包。
         */
        class MailCreateTextItem final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 原始网络包数据
             */
            MailCreateTextItem(WorldPacket&& packet) : ClientPacket(CMSG_MAIL_CREATE_TEXT_ITEM, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络包中解析邮箱GUID和邮件ID。
             */
            void Read() override;

            ObjectGuid Mailbox;                                         ///< 邮箱对象的GUID
            uint32 MailID = 0;                                          ///< 目标邮件ID
        };

        /**
         * @class SendMail
         * @brief 客户端请求发送新邮件
         *
         * 玩家发送邮件时使用此包,包含收件人、主题、正文、附件和金币等信息。
         * 服务器收到后会验证各项条件,然后创建邮件并返回结果。
         *
         * 验证内容包括:
         * - 收件人是否存在
         * - 是否有发送权限
         * - 附件物品是否可邮寄
         * - 金币和COD金额是否有效
         * - 是否超出邮件附件数量限制
         *
         * 继承自ClientPacket,表示客户端发送的请求包。
         */
        class SendMail final : public ClientPacket
        {
        public:
            /**
             * @struct StructSendMail
             * @brief 发送邮件信息结构
             *
             * 包含发送邮件所需的所有数据,包括收件人、内容、附件等。
             */
            struct StructSendMail
            {
                /**
                 * @struct MailAttachment
                 * @brief 邮件附件结构
                 *
                 * 表示要附加到邮件中的一个物品。
                 */
                struct MailAttachment
                {
                    uint8 AttachPosition = 0;                          ///< 附件槽位位置(0-11)
                    ObjectGuid ItemGUID;                               ///< 物品GUID(玩家背包中的物品)
                };

                ObjectGuid Mailbox;                                    ///< 邮箱对象GUID
                int32 StationeryID = 0;                                ///< 信纸类型ID
                int32 PackageID = 0;                                   ///< 包裹ID(未使用)
                int32 SendMoney = 0;                                   ///< 附带发送的金币数量
                int32 Cod = 0;                                         ///< 货到付款金额(Cash On Delivery)
                std::string Target;                                    ///< 收件人角色名称
                String<255, Strings::NoHyperlinks> Subject;            ///< 邮件主题(最多255字符,禁止超链接)
                String<7999, Strings::NoHyperlinks> Body;              ///< 邮件正文(最多7999字符,禁止超链接)
                Array<MailAttachment, MAX_MAIL_ITEMS> Attachments;     ///< 附件物品列表(最多12个)
            };

            /**
             * @brief 构造函数
             * @param packet 原始网络包数据
             */
            SendMail(WorldPacket&& packet) : ClientPacket(CMSG_SEND_MAIL, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络包中解析所有邮件发送信息,包括收件人、内容、附件等。
             */
            void Read() override;

            StructSendMail Info;                                       ///< 邮件发送信息
        };

        /**
         * @class MailCommandResult
         * @brief 服务器返回邮件操作结果
         *
         * 响应各种邮件操作请求,返回操作是否成功及错误码。
         * 包括发送邮件、收取附件、删除邮件等操作的结果反馈。
         *
         * 常见操作类型(Command):
         * - MAIL_SEND: 发送邮件
         * - MAIL_DELETE: 删除邮件
         * - MAIL_MARK_AS_READ: 标记已读
         * - MAIL_RETURN_TO_SENDER: 退回发件人
         * - MAIL_ITEM_TAKEN: 收取附件物品
         * - MAIL_MONEY_TAKEN: 收取金币
         *
         * 继承自ServerPacket,表示服务器发送的响应包。
         */
        class MailCommandResult final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             */
            MailCommandResult() : ServerPacket(SMSG_SEND_MAIL_RESULT) { }

            /**
             * @brief 序列化数据包
             * @return 指向序列化后的WorldPacket指针
             *
             * 根据错误码和操作类型选择性写入额外字段。
             * 不同的错误情况需要发送不同的数据给客户端。
             */
            WorldPacket const* Write() override;

            uint32 MailID = 0;                                         ///< 邮件ID
            uint32 Command = 0;                                        ///< 操作类型(MAIL_SEND/MAIL_DELETE等)
            uint32 ErrorCode = 0;                                      ///< 错误码(MAIL_OK/MAIL_ERR_*等)
            uint32 BagResult = 0;                                      ///< 背包错误码(ErrorCode=MAIL_ERR_EQUIP_ERROR时使用)
            uint32 AttachID = 0;                                       ///< 附件ID(收取附件时使用)
            uint32 QtyInInventory = 0;                                 ///< 背包中物品数量(收取堆叠物品时使用)
        };

        /**
         * @class MailReturnToSender
         * @brief 客户端请求退回邮件给发送者
         *
         * 玩家选择退回邮件时发送此包,将邮件退回给原发送者。
         * 通常用于拒收货到付款(COD)邮件或不需要的邮件。
         *
         * 注意事项:
         * - 已被收取的邮件无法退回
         * - 过期的邮件会自动退回
         * - COD邮件退回会返还物品给发送者
         *
         * 继承自ClientPacket,表示客户端发送的请求包。
         */
        class MailReturnToSender final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 原始网络包数据
             */
            MailReturnToSender(WorldPacket&& packet) : ClientPacket(CMSG_MAIL_RETURN_TO_SENDER, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络包中解析邮箱GUID、邮件ID和发送者GUID。
             */
            void Read() override;

            ObjectGuid Mailbox;                                        ///< 邮箱对象GUID
            int32 MailID = 0;                                          ///< 要退回的邮件ID
            ObjectGuid SenderGUID;                                     ///< 原发送者GUID
        };

        /**
         * @class MailMarkAsRead
         * @brief 客户端请求标记邮件为已读
         *
         * 玩家打开邮件阅读后发送此包,将邮件标记为已读状态。
         * 已读邮件在邮件列表中会显示不同的图标。
         *
         * 注意事项:
         * - 已读邮件在邮件列表中图标会变暗
         * - 标记已读会更新数据库中的邮件状态
         * - 不影响邮件的过期时间
         *
         * 继承自ClientPacket,表示客户端发送的请求包。
         */
        class MailMarkAsRead final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 原始网络包数据
             */
            MailMarkAsRead(WorldPacket&& packet) : ClientPacket(CMSG_MAIL_MARK_AS_READ, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络包中解析邮箱GUID和邮件ID。
             */
            void Read() override;

            ObjectGuid Mailbox;                                        ///< 邮箱对象GUID
            int32 MailID = 0;                                          ///< 要标记的邮件ID
        };

        /**
         * @class MailDelete
         * @brief 客户端请求删除邮件
         *
         * 玩家删除邮件时发送此包,请求永久删除指定邮件。
         * 删除邮件会同时删除所有附件物品和金币。
         *
         * 注意事项:
         * - 删除操作不可逆,物品和金币将永久丢失
         * - COD邮件在未支付前不能删除
         * - 带有未收取附件的邮件删除会警告玩家
         * - 删除原因用于统计和日志记录
         *
         * 继承自ClientPacket,表示客户端发送的请求包。
         */
        class MailDelete final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 原始网络包数据
             */
            MailDelete(WorldPacket&& packet) : ClientPacket(CMSG_MAIL_DELETE, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络包中解析邮箱GUID、邮件ID和删除原因。
             */
            void Read() override;

            ObjectGuid Mailbox;                                        ///< 邮箱对象GUID
            int32 MailID = 0;                                          ///< 要删除的邮件ID
            int32 DeleteReason = 0;                                    ///< 删除原因(用于日志和统计)
        };

        /**
         * @class MailTakeItem
         * @brief 客户端请求收取邮件附件物品
         *
         * 玩家点击收取附件物品时发送此包,将邮件中的物品转移到背包。
         * 如果是货到付款(COD)邮件,收取物品时会扣除相应金币。
         *
         * 注意事项:
         * - 需要足够的背包空间
         * - COD邮件需要足够的金币
         * - 物品会转移到玩家的背包中
         * - 收取后附件会从邮件中移除
         *
         * 继承自ClientPacket,表示客户端发送的请求包。
         */
        class MailTakeItem final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 原始网络包数据
             */
            MailTakeItem(WorldPacket&& packet) : ClientPacket(CMSG_MAIL_TAKE_ITEM, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络包中解析邮箱GUID、邮件ID和附件ID。
             */
            void Read() override;

            ObjectGuid Mailbox;                                        ///< 邮箱对象GUID
            int32 MailID = 0;                                          ///< 邮件ID
            int32 AttachID = 0;                                        ///< 附件ID(物品GUID)
        };

        /**
         * @class MailTakeMoney
         * @brief 客户端请求收取邮件中的金币
         *
         * 玩家点击收取金币时发送此包,将邮件中的金币转移到玩家钱包。
         * 收取后金币会从邮件中移除,但其他附件仍保留。
         *
         * 注意事项:
         * - 金币会直接添加到玩家的钱包
         * - 不会超过金币上限
         * - 收取后邮件仍保留,直到被删除或过期
         *
         * 继承自ClientPacket,表示客户端发送的请求包。
         */
        class MailTakeMoney final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 原始网络包数据
             */
            MailTakeMoney(WorldPacket&& packet) : ClientPacket(CMSG_MAIL_TAKE_MONEY, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络包中解析邮箱GUID和邮件ID。
             */
            void Read() override;

            ObjectGuid Mailbox;                                        ///< 邮箱对象GUID
            int32 MailID = 0;                                          ///< 邮件ID
        };

        /**
         * @class MailQueryNextMailTime
         * @brief 客户端查询下一封邮件到达时间
         *
         * 玩家打开邮箱时可能发送此包,查询即将到达的新邮件时间。
         * 用于显示"新邮件即将到达"的提示。
         *
         * 用途:
         * - 显示即将到达邮件的倒计时
         * - 提示玩家有新邮件即将到达
         * - 用于拍卖行邮件延迟通知
         *
         * 继承自ClientPacket,表示客户端发送的请求包。
         */
        class MailQueryNextMailTime final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 原始网络包数据
             */
            MailQueryNextMailTime(WorldPacket&& packet) : ClientPacket(MSG_QUERY_NEXT_MAIL_TIME, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 此包不包含任何数据,仅作为请求标识。
             */
            void Read() override { }
        };

        /**
         * @class MailQueryNextTimeResult
         * @brief 服务器返回下一封邮件到达时间
         *
         * 响应MailQueryNextMailTime请求,返回即将到达的新邮件信息。
         * 包含下一封邮件的预计到达时间和发送者信息。
         *
         * 用途:
         * - 显示新邮件倒计时通知
         * - 显示拍卖行邮件到达时间
         * - 提示玩家有新邮件即将到达
         *
         * 继承自ServerPacket,表示服务器发送的响应包。
         */
        class MailQueryNextTimeResult final : public ServerPacket
        {
        public:
            /**
             * @struct MailNextTimeEntry
             * @brief 即将到达邮件的信息条目
             *
             * 表示一封即将到达的邮件的基本信息。
             */
            struct MailNextTimeEntry
            {
                /**
                 * @brief 构造函数
                 * @param mail 邮件对象指针
                 *
                 * 从邮件对象中提取发送者信息和到达时间。
                 */
                MailNextTimeEntry(::Mail const* mail);

                ObjectGuid SenderGuid;                                 ///< 发送者GUID(玩家邮件)
                float TimeLeft = 0.0f;                                 ///< 剩余到达时间(秒)
                int32 AltSenderID = 0;                                 ///< 替代发送者ID(NPC、拍卖行等)
                int32 AltSenderType = 0;                               ///< 替代发送者类型
                int32 StationeryID = 0;                                ///< 信纸类型ID
            };

            /**
             * @brief 构造函数
             *
             * 初始化服务器包,预分配空间。
             */
            MailQueryNextTimeResult() : ServerPacket(MSG_QUERY_NEXT_MAIL_TIME, 8) { }

            /**
             * @brief 序列化数据包
             * @return 指向序列化后的WorldPacket指针
             *
             * 将NextMailTime和Next列表写入网络包。
             */
            WorldPacket const* Write() override;

            float NextMailTime = 0.0f;                                 ///< 下一封邮件到达时间(秒)
            std::vector<MailNextTimeEntry> Next;                       ///< 即将到达的邮件列表
        };

        /**
         * @class NotifyReceivedMail
         * @brief 服务器通知客户端收到新邮件
         *
         * 当玩家收到新邮件时,服务器主动推送此包通知客户端。
         * 客户端收到后会显示"新邮件"图标和提示音。
         *
         * 调用时机:
         * - 玩家收到其他玩家发送的邮件
         * - 系统发送邮件给玩家(拍卖行、GM等)
         * - 邮件到达延迟时间结束
         *
         * 注意:
         * - Delay字段表示显示通知前的延迟时间
         * - 延迟用于拍卖行邮件等场景,避免立即通知
         *
         * 继承自ServerPacket,表示服务器主动推送的包。
         */
        class NotifyReceivedMail : ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器包,预分配空间。
             */
            NotifyReceivedMail() : ServerPacket(SMSG_RECEIVED_MAIL, 4) { }

            /**
             * @brief 序列化数据包
             * @return 指向序列化后的WorldPacket指针
             *
             * 将延迟时间写入网络包。
             */
            WorldPacket const* Write() override;

            float Delay = 0.0f;                                        ///< 显示通知的延迟时间(秒)
        };

        /**
         * @class ShowMailbox
         * @brief 服务器通知客户端显示邮箱界面
         *
         * 服务器发送此包请求客户端打开邮箱界面。
         * 通常在玩家点击邮箱GameObject时由服务器发送。
         *
         * 调用时机:
         * - 玩家与邮箱GameObject交互
         * - 玩家与NPC邮递员交互
         * - 脚本触发打开邮箱
         *
         * 继承自ServerPacket,表示服务器主动推送的包。
         */
        class ShowMailbox final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器包,预分配16字节空间(用于GUID)。
             */
            ShowMailbox() : ServerPacket(SMSG_SHOW_MAILBOX, 16) { }

            /**
             * @brief 序列化数据包
             * @return 指向序列化后的WorldPacket指针
             *
             * 将邮递员GUID写入网络包。
             */
            WorldPacket const* Write() override;

            ObjectGuid PostmasterGUID;                                 ///< 邮递员或邮箱对象的GUID
        };
    }
}

#endif // MailPackets_h__
