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
 * @file Mail.h
 * @brief 邮件系统核心头文件
 *
 * 本文件定义了 TrinityCore 邮件系统的核心数据结构和类。
 * 邮件系统允许玩家之间、系统与玩家之间发送消息、物品和金币。
 *
 * 主要功能：
 * - 玩家间邮件发送（包含物品、金币）
 * - 拍卖行邮件通知
 * - 日历事件邮件
 * - NPC/GameObject 发送的邮件
 * - 货到付款(COD)功能
 *
 * 核心类：
 * - MailSender: 封装邮件发送者信息
 * - MailReceiver: 封装邮件接收者信息
 * - MailDraft: 邮件草稿，用于构建和发送邮件
 * - Mail: 邮件数据结构，存储完整的邮件信息
 */

#ifndef TRINITY_MAIL_H
#define TRINITY_MAIL_H

#include "Common.h"
#include "DatabaseEnvFwd.h"
#include "ObjectGuid.h"
#include <map>

struct AuctionEntry;
struct CalendarEvent;
class Item;
class Object;
class Player;

/// 普通信件物品模板ID - 用于显示邮件正文内容
#define MAIL_BODY_ITEM_TEMPLATE 8383                        // - plain letter, A Dusty Unsent Letter: 889

/// 每封邮件最多可附加的物品数量上限
#define MAX_MAIL_ITEMS 12

/**
 * @brief 邮件消息类型枚举
 *
 * 定义了邮件的不同来源类型，客户端会根据此类型
 * 决定如何显示邮件以及需要执行的查询操作
 */
enum MailMessageType
{
    MAIL_NORMAL         = 0,    ///< 普通邮件，来自玩家或系统
    MAIL_AUCTION        = 2,    ///< 拍卖行邮件，用于拍卖成交、过期等通知
    MAIL_CREATURE       = 3,    ///< 生物邮件，客户端会发送 CMSG_CREATURE_QUERY 查询该生物信息
    MAIL_GAMEOBJECT     = 4,    ///< 游戏对象邮件，客户端会发送 CMSG_GAMEOBJECT_QUERY 查询该对象信息
    MAIL_CALENDAR       = 5     ///< 日历邮件，来自日历系统的通知
};

/**
 * @brief 邮件检查标记枚举
 *
 * 用于标记邮件的各种状态，如已读、已退回等。
 * 这些标记会持久化到数据库中。
 */
enum MailCheckMask
{
    MAIL_CHECK_MASK_NONE        = 0x00,  ///< 无标记
    MAIL_CHECK_MASK_READ        = 0x01,  ///< 邮件已读标记
    MAIL_CHECK_MASK_RETURNED    = 0x02,  ///< 邮件已退回标记，防止再次退回造成死循环
    MAIL_CHECK_MASK_COPIED      = 0x04,  ///< 邮件物品已复制标记，防止重复复制邮件中的物品
    MAIL_CHECK_MASK_COD_PAYMENT = 0x08,  ///< 货到付款已支付标记
    MAIL_CHECK_MASK_HAS_BODY    = 0x10   ///< 邮件包含正文的标记
};

/**
 * @brief 邮件信纸样式枚举
 *
 * 定义邮件显示时使用的信纸样式。
 * 这些值对应 Stationery.dbc 数据库中的定义。
 *
 * 不同的信纸样式会在客户端显示不同的背景和装饰效果。
 */
enum MailStationery
{
    MAIL_STATIONERY_TEST    = 1,    ///< 测试用信纸
    MAIL_STATIONERY_DEFAULT = 41,   ///< 默认信纸样式
    MAIL_STATIONERY_GM      = 61,   ///< 游戏管理员(GM)专用信纸样式
    MAIL_STATIONERY_AUCTION = 62,   ///< 拍卖行信纸样式
    MAIL_STATIONERY_VAL     = 64,   ///< 情人节信纸样式 (Valentine)
    MAIL_STATIONERY_CHR     = 65,   ///< 圣诞节信纸样式 (Christmas)
    MAIL_STATIONERY_ORP     = 67    ///< 孤儿信纸样式 (Orphan) - 用于儿童周任务
};

/**
 * @brief 邮件状态枚举
 *
 * 用于跟踪邮件对象在内存中的修改状态，
 * 决定是否需要同步到数据库。
 */
enum MailState
{
    MAIL_STATE_UNCHANGED = 1,  ///< 邮件未修改，无需同步数据库
    MAIL_STATE_CHANGED   = 2,  ///< 邮件已修改，需要同步数据库
    MAIL_STATE_DELETED   = 3   ///< 邮件已删除，需要从数据库中移除
};

/**
 * @brief 邮件显示标记枚举
 *
 * 控制邮件在客户端界面上的显示方式，
 * 决定显示哪些操作按钮。
 */
enum MailShowFlags
{
    MAIL_SHOW_UNK0    = 0x0001,  ///< 未知标记0
    MAIL_SHOW_DELETE  = 0x0002,  ///< 强制显示删除按钮而非退回按钮
    MAIL_SHOW_AUCTION = 0x0004,  ///< 拍卖相关标记（旧注释遗留）
    MAIL_SHOW_UNK2    = 0x0008,  ///< 未知标记2，即使没有此标记也会显示COD
    MAIL_SHOW_RETURN  = 0x0010   ///< 显示退回按钮标记
};

/**
 * @brief 邮件发送者封装类
 *
 * 封装邮件发送者的相关信息，包括发送者类型、ID和信纸样式。
 * 支持多种发送者来源：玩家、NPC、GameObject、拍卖行、日历事件等。
 *
 * 使用方式：
 * 1. 通过构造函数直接指定发送者信息
 * 2. 通过传入具体的对象（Player、AuctionEntry等）自动推断发送者信息
 *
 * 性能说明：
 * - 构造函数为轻量级操作，仅进行简单的成员赋值
 * - 从对象推断类型时会进行类型检查，开销较小
 */
class TC_GAME_API MailSender
{
    public:                                                 // Constructors
        /**
         * @brief 构造函数 - 直接指定发送者参数
         *
         * 直接指定邮件类型、发送者ID和信纸样式。
         *
         * @param messageType 邮件消息类型，决定邮件来源类型
         * @param sender_guidlow_or_entry 发送者标识：
         *        - 对于玩家邮件：玩家GUID的低32位
         *        - 对于NPC/GameObject：其Entry ID
         *        - 对于拍卖行：拍卖行ID
         *        - 对于日历：事件ID
         * @param stationery 信纸样式，默认为默认样式
         */
        MailSender(MailMessageType messageType, ObjectGuid::LowType sender_guidlow_or_entry, MailStationery stationery = MAIL_STATIONERY_DEFAULT)
            : m_messageType(messageType), m_senderId(sender_guidlow_or_entry), m_stationery(stationery)
        {
        }

        /**
         * @brief 构造函数 - 从通用对象推断发送者信息
         * @param sender 发送者对象指针，可以是Unit、GameObject或Player
         * @param stationery 信纸样式，默认为默认样式
         *
         * 根据对象的类型自动设置邮件类型和发送者ID：
         * - TYPEID_UNIT -> MAIL_CREATURE，使用Entry ID
         * - TYPEID_GAMEOBJECT -> MAIL_GAMEOBJECT，使用Entry ID
         * - TYPEID_PLAYER -> MAIL_NORMAL，使用玩家GUID
         */
        MailSender(Object* sender, MailStationery stationery = MAIL_STATIONERY_DEFAULT);

        /**
         * @brief 构造函数 - 从日历事件创建发送者
         * @param sender 日历事件指针
         *
         * 设置邮件类型为 MAIL_CALENDAR，发送者ID为事件ID
         */
        MailSender(CalendarEvent* sender);

        /**
         * @brief 构造函数 - 从拍卖条目创建发送者
         * @param sender 拍卖条目指针
         *
         * 设置邮件类型为 MAIL_AUCTION，发送者ID为拍卖行ID
         */
        MailSender(AuctionEntry* sender);

        /**
         * @brief 构造函数 - 从玩家对象创建发送者
         * @param sender 玩家指针
         *
         * 根据玩家是否为GM自动选择信纸样式：
         * - GM玩家使用 MAIL_STATIONERY_GM
         * - 普通玩家使用 MAIL_STATIONERY_DEFAULT
         */
        MailSender(Player* sender);

        /**
         * @brief 构造函数 - 从生物Entry ID创建发送者
         * @param senderEntry 生物的Entry ID
         *
         * 用于NPC发送邮件，类型设置为 MAIL_CREATURE
         */
        MailSender(uint32 senderEntry);

    public:                                                 // Accessors
        /**
         * @brief 获取邮件消息类型
         * @return 邮件消息类型枚举值
         */
        MailMessageType GetMailMessageType() const { return m_messageType; }

        /**
         * @brief 获取发送者ID
         * @return 发送者ID（玩家GUID低32位或对象Entry ID）
         */
        ObjectGuid::LowType GetSenderId() const { return m_senderId; }

        /**
         * @brief 获取信纸样式
         * @return 信纸样式枚举值
         */
        MailStationery GetStationery() const { return m_stationery; }

    private:
        MailMessageType m_messageType;              ///< 邮件消息类型，决定邮件来源
        ObjectGuid::LowType m_senderId;             ///< 发送者ID：玩家低GUID或其他对象Entry
        MailStationery m_stationery;                ///< 信纸样式，影响邮件显示外观
};

/**
 * @brief 邮件接收者封装类
 *
 * 封装邮件接收者的相关信息，包括玩家对象指针和GUID。
 * 支持在线玩家和离线玩家（仅通过GUID标识）。
 *
 * 设计说明：
 * - 可以只提供GUID，用于发送给离线玩家
 * - 如果提供了玩家对象，会自动关联并验证GUID一致性
 *
 * 使用场景：
 * - 在线玩家：直接传入 Player* 对象
 * - 离线玩家：仅传入 GUID 低32位
 */
class TC_GAME_API MailReceiver
{
    public:                                                 // Constructors
        /**
         * @brief 构造函数 - 仅使用GUID创建接收者
         * @param receiver_lowguid 接收者的玩家GUID低32位
         *
         * 用于发送邮件给离线玩家，不需要玩家对象
         */
        explicit MailReceiver(ObjectGuid::LowType receiver_lowguid) : m_receiver(nullptr), m_receiver_lowguid(receiver_lowguid) { }

        /**
         * @brief 构造函数 - 使用玩家对象创建接收者
         * @param receiver 玩家对象指针
         *
         * 自动从玩家对象获取GUID，用于发送给在线玩家
         */
        MailReceiver(Player* receiver);

        /**
         * @brief 构造函数 - 同时提供玩家对象和GUID
         * @param receiver 玩家对象指针（可以为nullptr）
         * @param receiver_lowguid 接收者的玩家GUID低32位
         *
         * 当玩家对象和GUID同时提供时，会验证二者是否一致。
         * 如果玩家在线则传入对象，否则传入nullptr和GUID。
         */
        MailReceiver(Player* receiver, ObjectGuid::LowType receiver_lowguid);

    public:                                                 // Accessors
        /**
         * @brief 获取玩家对象指针
         * @return 玩家对象指针，离线玩家返回nullptr
         */
        Player* GetPlayer() const { return m_receiver; }

        /**
         * @brief 获取接收者GUID低32位
         * @return 玩家GUID的低32位
         */
        ObjectGuid::LowType GetPlayerGUIDLow() const { return m_receiver_lowguid; }

    private:
        Player* m_receiver;                        ///< 接收者玩家对象指针，离线玩家为nullptr
        ObjectGuid::LowType m_receiver_lowguid;    ///< 接收者玩家GUID低32位，用于数据库操作
};

/**
 * @brief 邮件草稿类
 *
 * 用于构建和发送邮件的核心类，封装了邮件的主题、正文、
 * 附加物品、金币和货到付款金额等信息。
 *
 * 主要功能：
 * - 支持基于邮件模板创建邮件
 * - 支持自定义主题和正文
 * - 管理附加物品列表
 * - 处理金币和COD（货到付款）
 * - 提供发送邮件和退回邮件的方法
 *
 * 使用流程：
 * 1. 创建 MailDraft 对象（使用模板ID或自定义主题/正文）
 * 2. 通过 AddItem/AddMoney/AddCOD 添加附件
 * 3. 调用 SendMailTo 发送邮件
 *
 * 性能注意事项：
 * - 发送邮件涉及数据库事务，应尽量批量处理
 * - 物品添加后会保存到数据库，发送失败时会删除
 * - 在线玩家会立即收到邮件通知，离线玩家下次登录时加载
 */
class TC_GAME_API MailDraft
{
    typedef std::map<ObjectGuid::LowType, Item*> MailItemMap;

    public:                                                 // Constructors
        /**
         * @brief 构造函数 - 使用邮件模板ID创建草稿
         *
         * 基于预定义的邮件模板创建邮件草稿。
         * 模板定义了邮件的主题、正文和可能的附件物品。
         *
         * @param mailTemplateId 邮件模板ID，对应数据库中的模板定义
         * @param need_items 是否需要生成模板中的物品，默认为true
         *
         * 使用场景：
         * - 任务奖励邮件
         * - 系统通知邮件
         * - 预定义的批量邮件
         */
        explicit MailDraft(uint16 mailTemplateId, bool need_items = true)
            : m_mailTemplateId(mailTemplateId), m_mailTemplateItemsNeed(need_items), m_money(0), m_COD(0)
        { }

        /**
         * @brief 构造函数 - 使用自定义主题和正文创建草稿
         *
         * 创建自定义邮件，需要手动指定主题和正文。
         *
         * @param subject 邮件主题/标题
         * @param body 邮件正文内容
         *
         * 使用场景：
         * - 玩家发送的邮件
         * - GM发送的个性化邮件
         * - 动态生成的系统邮件
         */
        MailDraft(std::string const& subject, std::string const& body)
            : m_mailTemplateId(0), m_mailTemplateItemsNeed(false), m_subject(subject), m_body(body), m_money(0), m_COD(0) { }

    public:                                                 // Accessors
        /**
         * @brief 获取邮件模板ID
         * @return 模板ID，自定义邮件返回0
         */
        uint16 GetMailTemplateId() const { return m_mailTemplateId; }

        /**
         * @brief 获取邮件主题
         * @return 邮件主题字符串引用
         */
        std::string const& GetSubject() const { return m_subject; }

        /**
         * @brief 获取附加金币数量
         * @return 金币数量（铜币单位）
         */
        uint32 GetMoney() const { return m_money; }

        /**
         * @brief 获取货到付款金额
         * @return COD金额（铜币单位）
         */
        uint32 GetCOD() const { return m_COD; }

        /**
         * @brief 获取邮件正文
         * @return 正文内容字符串引用
         */
        std::string const& GetBody() const { return m_body; }

    public:                                                 // modifiers
        /**
         * @brief 添加附件物品
         *
         * 将物品添加到邮件附件列表。物品以其GUID作为唯一标识存储。
         *
         * @param item 要附加的物品指针
         * @return 返回当前对象的引用，支持链式调用
         *
         * 注意事项：
         * - 物品会被保存到数据库，发送失败时会删除
         * - 最多支持12个附件物品
         * - 相同GUID的物品会被覆盖
         */
        MailDraft& AddItem(Item* item);

        /**
         * @brief 设置附加金币
         *
         * 设置邮件中附带的金币数量。
         *
         * @param money 金币数量（铜币单位）
         * @return 返回当前对象的引用，支持链式调用
         */
        MailDraft& AddMoney(uint32 money) { m_money = money; return *this; }

        /**
         * @brief 设置货到付款金额
         *
         * 设置收件人需要支付的金币数量才能领取邮件物品。
         *
         * @param COD 货到付款金额（铜币单位）
         * @return 返回当前对象的引用，支持链式调用
         *
         * 注意事项：
         * - COD邮件有效期为3天
         * - 收件人支付后金币会自动发送给发件人
         * - COD邮件不能被退回
         */
        MailDraft& AddCOD(uint32 COD) { m_COD = COD; return *this; }

    public:                                                 // finishers
        /**
         * @brief 将邮件退回给发件人
         *
         * 当收件人拒绝邮件或邮件过期时，将邮件退回给原始发件人。
         *
         * @param sender_acc 发件人的账号ID
         * @param sender_guid 发件人的玩家GUID低32位
         * @param receiver_guid 收件人的玩家GUID低32位（即当前邮件的所有者）
         * @param trans 数据库事务对象
         *
         * 处理逻辑：
         * 1. 检查发件人是否存在
         * 2. 如果发件人不存在，删除邮件和所有附件物品
         * 3. 如果发件人存在，更新物品所有者并发送退回邮件
         * 4. 如果发件人和收件人在不同账号，退回邮件会有延迟
         *
         * 性能说明：
         * - 涉及数据库更新操作，应使用事务
         * - 在线发件人会立即收到通知
         */
        void SendReturnToSender(uint32 sender_acc, ObjectGuid::LowType sender_guid, ObjectGuid::LowType receiver_guid, CharacterDatabaseTransaction trans);

        /**
         * @brief 发送邮件到指定接收者
         *
         * 完成邮件草稿的发送，将邮件数据写入数据库，
         * 并在接收者在线时立即更新其邮件列表。
         *
         * @param trans 数据库事务对象
         * @param receiver 邮件接收者封装对象
         * @param sender 邮件发送者封装对象
         * @param checked 邮件检查标记，默认为MAIL_CHECK_MASK_NONE
         * @param deliver_delay 邮件送达延迟时间（秒），默认为0表示立即送达
         *
         * 处理流程：
         * 1. 生成唯一邮件ID
         * 2. 计算送达时间和过期时间
         * 3. 将邮件信息写入数据库
         * 4. 将所有附件物品信息写入数据库
         * 5. 如果接收者在线，更新其内存中的邮件列表
         * 6. 如果接收者离线且有附件，清理内存中的物品对象
         *
         * 过期时间计算规则：
         * - COD邮件：3天
         * - 普通邮件：30天（GM发送的为90天）
         * - 拍卖行空邮件：1小时
         * - 战场奖励邮件：1天
         *
         * 性能说明：
         * - 使用数据库事务确保数据一致性
         * - 在线接收者会立即收到新邮件通知
         * - 离线接收者下次登录时从数据库加载邮件
         */
        void SendMailTo(CharacterDatabaseTransaction trans, MailReceiver const& receiver, MailSender const& sender, MailCheckMask checked = MAIL_CHECK_MASK_NONE, uint32 deliver_delay = 0);

    private:
        /**
         * @brief 删除邮件中包含的所有物品
         *
         * 清理邮件草稿中的所有附件物品。
         *
         * @param trans 数据库事务对象
         * @param inDB 是否同时从数据库删除物品记录，默认为false
         *
         * 调用时机：
         * - 邮件发送失败时
         * - 退回邮件但发件人不存在时
         * - 接收者离线时（物品已在数据库中）
         */
        void deleteIncludedItems(CharacterDatabaseTransaction trans, bool inDB = false);

        /**
         * @brief 准备邮件模板中的物品
         *
         * 根据邮件模板ID生成对应的附件物品。
         * 使用战利品系统从模板中生成物品。
         *
         * @param receiver 接收者玩家对象
         * @param trans 数据库事务对象
         *
         * 调用时机：
         * - SendMailTo 函数内部，在接收者在线时调用
         *
         * 处理逻辑：
         * 1. 检查是否有邮件模板ID和是否需要生成物品
         * 2. 特殊处理模板ID 123（包含100金币的任务奖励）
         * 3. 使用战利品系统生成物品
         * 4. 限制物品数量不超过 MAX_MAIL_ITEMS
         */
        void prepareItems(Player* receiver, CharacterDatabaseTransaction trans);                // called from SendMailTo for generate mailTemplateBase items

        uint16      m_mailTemplateId;          ///< 邮件模板ID，自定义邮件为0
        bool        m_mailTemplateItemsNeed;   ///< 是否需要从模板生成物品
        std::string m_subject;                 ///< 邮件主题/标题
        std::string m_body;                    ///< 邮件正文内容

        MailItemMap m_items;                   ///< 附件物品映射表，键为物品GUID低32位，值为物品对象指针。
                                               ///< 使用映射表避免重复GUID，仅存储GUID的低32位

        uint32 m_money;                        ///< 附带金币数量（铜币单位）
        uint32 m_COD;                          ///< 货到付款金额（铜币单位）
};

/**
 * @brief 邮件物品信息结构体
 *
 * 存储邮件中单个附件物品的简要信息。
 * 用于在邮件数据中记录物品的GUID和模板ID。
 */
struct MailItemInfo
{
    ObjectGuid::LowType item_guid;      ///< 物品GUID的低32位，用于定位具体物品实例
    uint32 item_template;               ///< 物品模板ID，对应 item_template 表
};

/// 邮件物品信息向量类型定义
typedef std::vector<MailItemInfo> MailItemInfoVec;

/**
 * @brief 邮件数据结构体
 *
 * 存储完整的邮件信息，包括邮件元数据、内容、附件和状态。
 * 这是邮件系统在内存中的核心数据结构。
 *
 * 生命周期：
 * 1. 创建：从数据库加载或新邮件发送时创建
 * 2. 使用：玩家查看、操作邮件时读取/修改
 * 3. 销毁：邮件被删除或玩家下线时释放
 *
 * 数据持久化：
 * - 邮件数据存储在 character_database 的 mail 表中
 * - 附件物品信息存储在 mail_items 表中
 * - 状态变更（如已读标记）会同步到数据库
 */
struct TC_GAME_API Mail
{
    uint32 messageID;                   ///< 邮件唯一标识ID
    uint8 messageType;                  ///< 邮件消息类型（MailMessageType枚举值）
    uint8 stationery;                   ///< 信纸样式（MailStationery枚举值）
    uint16 mailTemplateId;              ///< 邮件模板ID，自定义邮件为0
    ObjectGuid::LowType sender;         ///< 发送者ID（玩家GUID低32位或对象Entry ID）
    ObjectGuid::LowType receiver;       ///< 接收者玩家GUID低32位
    std::string subject;                ///< 邮件主题/标题
    std::string body;                   ///< 邮件正文内容
    std::vector<MailItemInfo> items;    ///< 附件物品信息列表
    std::vector<ObjectGuid::LowType> removedItems;  ///< 已移除物品的GUID列表，用于追踪删除的附件
    time_t expire_time;                 ///< 邮件过期时间戳（Unix时间）
    time_t deliver_time;                ///< 邮件送达时间戳（Unix时间）
    uint32 money;                       ///< 附带金币数量（铜币单位）
    uint32 COD;                         ///< 货到付款金额（铜币单位）
    uint32 checked;                     ///< 邮件检查标记（MailCheckMask枚举值的组合）
    MailState state;                    ///< 邮件状态（用于数据库同步）

    /**
     * @brief 添加物品信息到邮件
     *
     * 向邮件的附件列表中添加一个物品信息。
     *
     * @param itemGuidLow 物品GUID的低32位
     * @param item_template 物品模板ID
     *
     * 调用时机：
     * - 发送新邮件时
     * - 从数据库加载邮件时
     */
    void AddItem(ObjectGuid::LowType itemGuidLow, uint32 item_template)
    {
        MailItemInfo mii;
        mii.item_guid = itemGuidLow;
        mii.item_template = item_template;
        items.push_back(mii);
    }

    /**
     * @brief 从邮件中移除物品
     *
     * 从邮件的附件列表中移除指定物品。
     *
     * @param item_guid 要移除物品的GUID低32位
     * @return 如果找到并移除成功返回true，否则返回false
     *
     * 调用时机：
     * - 玩家取走邮件附件时
     * - 退回邮件时
     */
    bool RemoveItem(ObjectGuid::LowType item_guid)
    {
        for (MailItemInfoVec::iterator itr = items.begin(); itr != items.end(); ++itr)
        {
            if (itr->item_guid == item_guid)
            {
                items.erase(itr);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief 检查邮件是否有附件物品
     * @return 如果有附件物品返回true，否则返回false
     */
    bool HasItems() const { return !items.empty(); }
};

#endif
