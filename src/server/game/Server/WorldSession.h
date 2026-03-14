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
 * @file WorldSession.h
 * @brief 世界会话模块 - 管理玩家与服务器之间的会话连接
 *
 * WorldSession是TrinityCore服务器架构的核心组件之一，负责管理单个玩家账号
 * 与服务器之间的所有交互。每个连接的玩家都有一个对应的WorldSession实例。
 *
 * 主要职责：
 * 1. 网络通信管理
 *    - 维护与客户端的Socket连接
 *    - 接收和发送网络数据包
 *    - 管理数据包队列和处理
 *
 * 2. 会话状态管理
 *    - 认证状态（已认证、已登录、传送中等）
 *    - 登入/登出流程控制
 *    - 超时和空闲检测
 *
 * 3. 玩家数据管理
 *    - 账号信息（ID、名称、权限等级）
 *    - 角色数据（Player对象指针）
 *    - 账号缓存数据（配置、宏、快捷键等）
 *
 * 4. 权限和安全
 *    - RBAC权限控制
 *    - Warden反作弊系统
 *    - DoS攻击防护
 *
 * 5. 操作码处理
 *    - 提供数百个操作码处理函数
 *    - 处理客户端请求（移动、聊天、交易、战斗等）
 *    - 异步数据库查询回调
 *
 * 设计模式：
 * - 单例模式：每个玩家账号对应唯一的WorldSession
 * - 观察者模式：响应各种游戏事件
 * - 状态模式：不同的会话状态有不同的行为
 *
 * 生命周期：
 * 1. 创建：玩家认证成功后创建
 * 2. 活跃：玩家在线期间持续存在
 * 3. 销毁：玩家断开连接后销毁
 */

/// \addtogroup u2w
/// @{
/// \file

#ifndef __WORLDSESSION_H
#define __WORLDSESSION_H

#include "Common.h"
#include "AsyncCallbackProcessor.h"
#include "AuthDefines.h"
#include "DatabaseEnvFwd.h"
#include "LockedQueue.h"
#include "ObjectGuid.h"
#include "Packet.h"
#include "SharedDefines.h"
#include <boost/circular_buffer_fwd.hpp>
#include <string>
#include <map>
#include <memory>
#include <unordered_map>

class Creature;
class GameClient;
class GameObject;
class InstanceSave;
class Item;
class LoginQueryHolder;
class Object;
class Player;
class Quest;
class SpellCastTargets;
class Unit;
class Warden;
class WorldPacket;
class WorldSocket;
struct AddonInfo;
struct AreaTableEntry;
struct AuctionEntry;
struct DeclinedName;
struct ItemTemplate;
struct MovementInfo;
struct Petition;
struct TradeStatusInfo;
enum AuctionAction : uint8;
enum AuctionError : uint8;
enum InventoryResult : uint8;

namespace lfg
{
    struct LfgJoinResultData;
    struct LfgPlayerBoot;
    struct LfgProposal;
    struct LfgQueueStatusData;
    struct LfgPlayerRewardData;
    struct LfgRoleCheck;
    struct LfgUpdateData;
}

namespace rbac
{
class RBACData;
}

namespace WorldPackets
{
    namespace Bank
    {
        class AutoBankItem;
        class AutoStoreBankItem;
        class BuyBankSlot;
    }

    namespace Calendar
    {
        class CalendarAddEvent;
        class CalendarCopyEvent;
        class CalendarInvite;
        class CalendarModeratorStatusQuery;
        class CalendarRSVP;
        class CalendarEventSignUp;
        class CalendarStatus;
        class CalendarGetCalendar;
        class CalendarGetEvent;
        class CalendarGetNumPending;
        class CalendarGuildFilter;
        class CalendarArenaTeam;
        class CalendarRemoveEvent;
        class CalendarRemoveInvite;
        class CalendarUpdateEvent;
        class SetSavedInstanceExtend;
        class CalendarComplain;
    }

    namespace Character
    {
        class LogoutCancel;
        class LogoutRequest;
        class ShowingCloak;
        class ShowingHelm;
        class PlayerLogout;
        class PlayedTimeClient;
    }

    namespace Chat
    {
        class EmoteClient;
    }

    namespace Combat
    {
        class AttackSwing;
        class AttackStop;
        class SetSheathed;
    }

    namespace Guild
    {
        class QueryGuildInfo;
        class GuildCreate;
        class GuildInviteByName;
        class AcceptGuildInvite;
        class GuildDeclineInvitation;
        class GuildGetInfo;
        class GuildGetRoster;
        class GuildPromoteMember;
        class GuildDemoteMember;
        class GuildOfficerRemoveMember;
        class GuildLeave;
        class GuildDelete;
        class GuildUpdateMotdText;
        class GuildAddRank;
        class GuildDeleteRank;
        class GuildUpdateInfoText;
        class GuildSetMemberNote;
        class GuildEventLogQuery;
        class GuildBankRemainingWithdrawMoneyQuery;
        class GuildPermissionsQuery;
        class GuildSetRankPermissions;
        class GuildBankActivate;
        class GuildBankQueryTab;
        class GuildBankDepositMoney;
        class GuildBankWithdrawMoney;
        class GuildBankSwapItems;
        class GuildBankBuyTab;
        class GuildBankUpdateTab;
        class GuildBankLogQuery;
        class GuildBankTextQuery;
        class GuildBankSetTabText;
        class GuildSetGuildMaster;
        class SaveGuildEmblem;
    }

    namespace LFG
    {
        class LFGJoin;
        class LFGLeave;
    }

    namespace Mail
    {
        class MailCreateTextItem;
        class MailDelete;
        class MailGetList;
        class MailMarkAsRead;
        class MailQueryNextMailTime;
        class MailReturnToSender;
        class MailTakeItem;
        class MailTakeMoney;
        class SendMail;
    }

    namespace Misc
    {
        class CompleteCinematic;
        class CompleteMovie;
        class NextCinematicCamera;
        class OpeningCinematic;
        class RandomRollClient;
        class TogglePvP;
        class WorldTeleport;
        class ReclaimCorpse;
        class RepopRequest;
        class ResurrectResponse;
    }

    namespace NPC
    {
        class Hello;
        class TrainerBuySpell;
    }

    namespace Pet
    {
        class DismissCritter;
        class PetAbandon;
        class PetStopAttack;
        class PetSpellAutocast;
        class RequestPetInfo;
    }

    namespace Query
    {
        class QueryCreature;
        class QueryGameObject;
        class QueryItemSingle;
        class QuestPOIQuery;
    }

    namespace Quest
    {
        class QueryQuestInfo;
    }

    namespace Spells
    {
        class CancelCast;
        class CancelAura;
        class PetCancelAura;
        class CancelGrowthAura;
        class CancelMountAura;
        class CancelAutoRepeatSpell;
        class CancelChannelling;
    }

    namespace Talents
    {
        class ConfirmRespecWipe;
    }

    namespace Totem
    {
        class TotemDestroyed;
    }
}

/// 账号数据类型枚举 - 定义不同类型的账号缓存数据
enum AccountDataType
{
    GLOBAL_CONFIG_CACHE             = 0,                    // 全局配置缓存 (0x01)
    PER_CHARACTER_CONFIG_CACHE      = 1,                    // 角色专属配置缓存 (0x02)
    GLOBAL_BINDINGS_CACHE           = 2,                    // 全局快捷键绑定缓存 (0x04)
    PER_CHARACTER_BINDINGS_CACHE    = 3,                    // 角色专属快捷键绑定缓存 (0x08)
    GLOBAL_MACROS_CACHE             = 4,                    // 全局宏缓存 (0x10)
    PER_CHARACTER_MACROS_CACHE      = 5,                    // 角色专属宏缓存 (0x20)
    PER_CHARACTER_LAYOUT_CACHE      = 6,                    // 角色专属界面布局缓存 (0x40)
    PER_CHARACTER_CHAT_CACHE        = 7                     // 角色专属聊天设置缓存 (0x80)
};

#define NUM_ACCOUNT_DATA_TYPES        8

#define GLOBAL_CACHE_MASK           0x15
#define PER_CHARACTER_CACHE_MASK    0xEA

uint32 constexpr MAX_CHARACTERS_PER_REALM = 10; // max supported by client in char enum

/// 账号数据结构 - 存储账号相关的缓存数据
struct AccountData
{
    AccountData() : Time(0), Data("") { }

    time_t Time;        // 数据最后更新时间戳
    std::string Data;   // 数据内容字符串
};

enum PartyOperation
{
    PARTY_OP_INVITE   = 0,
    PARTY_OP_UNINVITE = 1,
    PARTY_OP_LEAVE    = 2,
    PARTY_OP_SWAP     = 4
};

enum BarberShopResult
{
    BARBER_SHOP_RESULT_SUCCESS      = 0,
    BARBER_SHOP_RESULT_NO_MONEY     = 1,
    BARBER_SHOP_RESULT_NOT_ON_CHAIR = 2,
    BARBER_SHOP_RESULT_NO_MONEY_2   = 3
};

enum BFLeaveReason
{
    BF_LEAVE_REASON_CLOSE     = 0x00000001,
    //BF_LEAVE_REASON_UNK1      = 0x00000002, (not used)
    //BF_LEAVE_REASON_UNK2      = 0x00000004, (not used)
    BF_LEAVE_REASON_EXITED    = 0x00000008,
    BF_LEAVE_REASON_LOW_LEVEL = 0x00000010
};

enum ChatRestrictionType
{
    ERR_CHAT_RESTRICTED = 0,
    ERR_CHAT_THROTTLED  = 1,
    ERR_USER_SQUELCHED  = 2,
    ERR_YELL_RESTRICTED = 3
};

enum DeclinedNameResult
{
    DECLINED_NAMES_RESULT_SUCCESS = 0,
    DECLINED_NAMES_RESULT_ERROR   = 1
};

enum TutorialsFlag : uint8
{
    TUTORIALS_FLAG_NONE                           = 0x00,
    TUTORIALS_FLAG_CHANGED                        = 0x01,
    TUTORIALS_FLAG_LOADED_FROM_DB                 = 0x02
};

/// 数据包过滤器基类 - 用于确定下一个数据包是否可以安全处理
/// 主要用于在Map::Update()等场景中过滤线程安全的数据包
class PacketFilter
{
public:
    explicit PacketFilter(WorldSession* pSession) : m_pSession(pSession) { }
    virtual ~PacketFilter() { }

    virtual bool Process(WorldPacket* /*packet*/) { return true; }
    virtual bool ProcessUnsafe() const { return true; }

protected:
    WorldSession* const m_pSession;

private:
    PacketFilter(PacketFilter const& right) = delete;
    PacketFilter& operator=(PacketFilter const& right) = delete;
};
/// 地图会话过滤器 - 仅处理线程安全的数据包
/// 用于在Map::Update()中处理数据包，不处理玩家登出
class MapSessionFilter : public PacketFilter
{
public:
    explicit MapSessionFilter(WorldSession* pSession) : PacketFilter(pSession) { }
    ~MapSessionFilter() { }

    virtual bool Process(WorldPacket* packet) override;
    //in Map::Update() we do not process player logout!
    virtual bool ProcessUnsafe() const override { return false; }
};

/// 世界会话过滤器 - 用于过滤线程不安全的数据包
/// 在World::UpdateSessions()中使用，只处理非线程安全的数据包
class WorldSessionFilter : public PacketFilter
{
public:
    explicit WorldSessionFilter(WorldSession* pSession) : PacketFilter(pSession) { }
    ~WorldSessionFilter() { }

    virtual bool Process(WorldPacket* packet) override;
};

/// 角色创建信息类 - 传递给回调函数的数据容器
/// 用于存储创建角色时的用户输入和服务器端数据
class CharacterCreateInfo
{
    friend class WorldSession;
    friend class Player;

    protected:
        /// 用户指定的变量 - 角色创建时用户输入的数据
        std::string Name;           // 角色名称
        uint8 Race       = 0;       // 种族ID
        uint8 Class      = 0;       // 职业ID
        uint8 Gender     = GENDER_NONE; // 性别
        uint8 Skin       = 0;       // 肤色
        uint8 Face       = 0;       // 脸型
        uint8 HairStyle  = 0;       // 发型
        uint8 HairColor  = 0;       // 发色
        uint8 FacialHair = 0;       // 面部毛发（胡须等）
        uint8 OutfitId   = 0;       // 初始装备套装ID

        /// 服务器端数据 - 由服务器生成或查询的数据
        uint8 CharCount = 0;        // 账号已有角色数量
};

/// 角色重命名信息结构 - 存储角色改名所需的数据
struct CharacterRenameInfo
{
    friend class WorldSession;

    protected:
        ObjectGuid Guid;        // 角色GUID
        std::string Name;       // 新角色名称
};

/// 角色自定义信息结构 - 存储角色外观定制数据（继承自重命名信息）
struct CharacterCustomizeInfo : public CharacterRenameInfo
{
    friend class Player;
    friend class WorldSession;

    protected:
        uint8 Gender     = GENDER_NONE; // 性别
        uint8 Skin       = 0;           // 肤色
        uint8 Face       = 0;           // 脸型
        uint8 HairStyle  = 0;           // 发型
        uint8 HairColor  = 0;           // 发色
        uint8 FacialHair = 0;           // 面部毛发
};

/// 角色阵营变更信息结构 - 存储阵营转换所需数据（继承自自定义信息）
struct CharacterFactionChangeInfo : public CharacterCustomizeInfo
{
    friend class Player;
    friend class WorldSession;

    protected:
        uint8 Race = 0;             // 新种族ID
        bool FactionChange = false; // 是否进行阵营变更
};

/// 数据包计数器结构 - 用于DoS防护中的频率统计
struct PacketCounter
{
    time_t lastReceiveTime;     // 上次接收该操作码的时间戳
    uint32 amountCounter;       // 该操作码的累计计数
};

/// 玩家会话类 - 管理玩家与服务器的连接和通信
/// WorldSession 类是服务器端的核心类之一，负责：
/// 1. 管理玩家与服务器之间的网络连接
/// 2. 处理客户端发送的各种数据包（Opcode）
/// 3. 维护玩家的账号信息、权限、状态等
/// 4. 提供登入、登出、踢人等会话管理功能
class TC_GAME_API WorldSession
{
    public:
        /// 构造函数 - 初始化玩家会话
        /// @param id 账号ID
        /// @param name 账号名称
        /// @param sock 网络套接字连接
        /// @param sec 账号安全等级
        /// @param expansion 客户端资料片版本
        /// @param mute_time 禁言结束时间
        /// @param timezoneOffset 时区偏移
        /// @param locale 客户端语言设置
        /// @param recruiter 招募者ID
        /// @param isARecruiter 是否为招募者
        WorldSession(uint32 id, std::string&& name, std::shared_ptr<WorldSocket> sock, AccountTypes sec, uint8 expansion, time_t mute_time,
            Minutes timezoneOffset, LocaleConstant locale, uint32 recruiter, bool isARecruiter);
        /// 析构函数 - 清理会话资源
        ~WorldSession();

        /// 检查玩家是否正在加载中
        bool PlayerLoading() const { return m_playerLoading; }
        /// 检查玩家是否正在登出中
        bool PlayerLogout() const { return m_playerLogout; }
        /// 检查玩家是否正在登出且需要保存
        bool PlayerLogoutWithSave() const { return m_playerLogout && m_playerSave; }
        /// 检查玩家是否最近刚登出
        bool PlayerRecentlyLoggedOut() const { return m_playerRecentlyLogout; }
        /// 检查玩家是否已断开连接（套接字为空）
        bool PlayerDisconnected() const { return !m_Socket; }

        /// 读取插件信息
        /// @param data 数据缓冲区
        void ReadAddonsInfo(ByteBuffer& data);
        /// 发送插件信息给客户端
        void SendAddonsInfo();

        /// 读取移动信息
        /// @param data 数据包
        /// @param mi 移动信息结构指针
        void ReadMovementInfo(WorldPacket& data, MovementInfo* mi);
        /// 写入移动信息（静态方法）
        /// @param data 数据包指针
        /// @param mi 移动信息结构指针
        void static WriteMovementInfo(WorldPacket* data, MovementInfo* mi);

        /// 发送数据包给客户端
        /// @param packet 要发送的数据包指针
        void SendPacket(WorldPacket const* packet);
        /// 发送通知消息给客户端 - 格式化字符串版本
        /// @param format 格式化字符串
        /// @param ... 可变参数
        void SendNotification(const char *format, ...) ATTR_PRINTF(2, 3);
        /// 发送通知消息给客户端 - 字符串ID版本
        /// @param string_id 字符串模板ID
        /// @param ... 可变参数
        void SendNotification(uint32 string_id, ...);
        /// 发送宠物名称无效错误消息
        void SendPetNameInvalid(uint32 error, std::string const& name, DeclinedName *declinedName);
        /// 发送队伍操作结果
        /// @param operation 队伍操作类型
        /// @param member 成员名称
        /// @param res 操作结果
        /// @param val 附加值
        void SendPartyResult(PartyOperation operation, std::string const& member, PartyResult res, uint32 val = 0);
        /// 发送区域触发消息
        /// @param Text 格式化文本
        /// @param ... 可变参数
        void SendAreaTriggerMessage(char const* Text, ...) ATTR_PRINTF(2, 3);
        /// 发送相位偏移设置
        /// @param phaseShift 相位ID
        void SendSetPhaseShift(uint32 phaseShift);
        /// 发送服务器时间查询响应
        void SendQueryTimeResponse();

        /// 发送认证响应给客户端
        /// @param code 认证结果代码
        /// @param shortForm 是否使用短格式
        /// @param queuePos 队列位置（默认为0）
        void SendAuthResponse(uint8 code, bool shortForm, uint32 queuePos = 0);
        /// 发送客户端缓存版本号
        /// @param version 缓存版本号
        void SendClientCacheVersion(uint32 version);

        /// 初始化会话 - 在玩家登录后设置会话状态
        void InitializeSession();
        /// 初始化会话回调 - 异步加载角色数据后的回调处理
        /// @param realmHolder 角色数据库查询结果持有者
        void InitializeSessionCallback(CharacterDatabaseQueryHolder const& realmHolder);

        /// 获取游戏客户端对象
        GameClient* GetGameClient() const { return _gameClient; };

        /// 获取RBAC权限数据对象
        rbac::RBACData* GetRBACData() const;
        /// 检查是否拥有指定权限
        /// @param permissionId 权限ID
        /// @return 返回true表示拥有该权限
        bool HasPermission(uint32 permissionId);
        /// 加载权限数据 - 从数据库加载账号权限
        void LoadPermissions();
        /// 异步加载权限数据
        QueryCallback LoadPermissionsAsync();
        /// 使RBAC数据失效 - 强制下次HasPermission检查时重新加载权限
        void InvalidateRBACData();

        /// 获取账号安全等级
        AccountTypes GetSecurity() const { return _security; }
        /// 获取账号ID
        uint32 GetAccountId() const { return _accountId; }
        /// 获取账号名称
        std::string const& GetAccountName() const { return _accountName; }
        /// 获取玩家对象指针
        Player* GetPlayer() const { return _player; }
        /// 获取玩家名称
        std::string const& GetPlayerName() const;
        /// 获取玩家信息字符串（用于日志等）
        std::string GetPlayerInfo() const;

        /// 获取玩家GUID的低32位
        ObjectGuid::LowType GetGUIDLow() const;
        /// 设置账号安全等级
        /// @param security 新的安全等级
        void SetSecurity(AccountTypes security) { _security = security; }
        /// 获取客户端远程地址（IP地址）
        std::string const& GetRemoteAddress() const { return m_Address; }
        /// 设置玩家对象指针
        /// @param player 玩家对象指针
        void SetPlayer(Player* player);
        /// 获取资料片版本
        uint8 Expansion() const { return m_expansion; }

        /// 初始化Warden反作弊系统
        /// @param k 会话密钥
        /// @param os 操作系统类型
        void InitWarden(SessionKey const& k, std::string const& os);
        /// 获取Warden模块指针（可修改）
        Warden* GetWarden() { return _warden.get(); }
        /// 获取Warden模块指针（只读）
        Warden const* GetWarden() const { return _warden.get(); }

        /// 设置会话是否在认证队列中
        /// @param state 是否在队列中
        void SetInQueue(bool state) { m_inQueue = state; }

        /// 检查用户是否正在登出过程中
        bool isLogingOut() const { return _logoutTime || m_playerLogout; }

        /// 设置登出开始时间 - 启动登出流程
        /// @param requestTime 登出请求的时间戳
        void SetLogoutStartTime(time_t requestTime)
        {
            _logoutTime = requestTime;
        }

        /// 检查是否应该执行登出（登出冷却时间是否已过）
        /// @param currTime 当前时间戳
        /// @return 返回true表示登出冷却已过，可以执行登出
        bool ShouldLogOut(time_t currTime) const
        {
            return (_logoutTime > 0 && currTime >= _logoutTime + 20);
        }

        /// 登出玩家 - 执行玩家登出流程
        /// @param save 是否保存玩家数据到数据库
        void LogoutPlayer(bool save);

        /// 踢出玩家 - 强制断开玩家连接
        /// @param reason 踢人原因描述
        void KickPlayer(std::string const& reason);
        // Returns true if all contained hyperlinks are valid
        // May kick player on false depending on world config (handler should abort)
        bool ValidateHyperlinksAndMaybeKick(std::string const& str);
        // Returns true if the message contains no hyperlinks
        // May kick player on false depending on world config (handler should abort)
        bool DisallowHyperlinksAndMaybeKick(std::string const& str);

        /// 将数据包加入接收队列等待处理
        /// @param new_packet 要入队的数据包指针
        void QueuePacket(WorldPacket* new_packet);

        /// 更新会话状态 - 每帧调用，处理队列中的数据包
        /// @param diff 距离上次更新的时间间隔（毫秒）
        /// @param updater 数据包过滤器，用于筛选可处理的数据包
        /// @return 返回是否成功处理
        bool Update(uint32 diff, PacketFilter& updater);

        /// 发送认证等待队列位置信息
        /// @param position 在队列中的位置
        void SendAuthWaitQueue(uint32 position);

        /// 发送功能系统状态给客户端（包含各种系统开关状态）
        void SendFeatureSystemStatus();

        void SendNameQueryOpcode(ObjectGuid guid);

        void SendTrainerList(Creature* npc);
        void SendListInventory(ObjectGuid guid);
        void SendShowBank(ObjectGuid guid);
        bool CanOpenMailBox(ObjectGuid guid);
        void SendShowMailBox(ObjectGuid guid);
        void SendTabardVendorActivate(ObjectGuid guid);
        void SendSpiritResurrect();
        void SendBindPoint(Creature* npc);

        void SendAttackStop(Unit const* enemy);

        void SendBattleGroundList(ObjectGuid guid, BattlegroundTypeId bgTypeId = BATTLEGROUND_RB);

        void SendTradeStatus(TradeStatusInfo const& status);
        void SendUpdateTrade(bool trader_data = true);
        void SendCancelTrade(TradeStatus status);

        void SendPetitionQueryOpcode(ObjectGuid petitionguid);

        // Spell
        void HandleClientCastFlags(WorldPacket& recvPacket, uint8 castFlags, SpellCastTargets& targets);

        // Pet
        void SendQueryPetNameResponse(ObjectGuid guid, uint32 petnumber);
        void SendStablePet(ObjectGuid guid);
        void SendPetStableResult(uint8 guid);
        bool CheckStableMaster(ObjectGuid guid);

        // 账号数据管理
        /// 获取指定类型的账号数据
        /// @param type 账号数据类型
        /// @return 返回账号数据指针
        AccountData* GetAccountData(AccountDataType type) { return &m_accountData[type]; }
        /// 设置账号数据
        /// @param type 账号数据类型
        /// @param tm 时间戳
        /// @param data 数据内容
        void SetAccountData(AccountDataType type, time_t tm, std::string const& data);
        /// 发送账号数据时间戳给客户端
        /// @param mask 数据类型掩码
        void SendAccountDataTimes(uint32 mask);
        /// 从数据库加载账号数据
        /// @param result 数据库查询结果
        /// @param mask 数据类型掩码
        void LoadAccountData(PreparedQueryResult result, uint32 mask);

        // 教程数据管理
        /// 从数据库加载教程数据
        /// @param result 数据库查询结果
        void LoadTutorialsData(PreparedQueryResult result);
        /// 发送教程数据给客户端
        void SendTutorialsData();
        /// 保存教程数据到数据库
        /// @param trans 数据库事务
        void SaveTutorialsData(CharacterDatabaseTransaction trans);
        /// 获取指定索引的教程整数值
        /// @param index 教程索引（0到MAX_ACCOUNT_TUTORIAL_VALUES-1）
        /// @return 返回教程值
        uint32 GetTutorialInt(uint8 index) const { return m_Tutorials[index]; }
        /// 设置指定索引的教程整数值
        /// @param index 教程索引
        /// @param value 新的教程值
        void SetTutorialInt(uint8 index, uint32 value)
        {
            if (m_Tutorials[index] != value)
            {
                m_Tutorials[index] = value;
                m_TutorialsChanged |= TUTORIALS_FLAG_CHANGED;
            }
        }
        // 拍卖行功能
        /// 发送拍卖行NPC问候消息
        /// @param guid NPC的GUID
        /// @param unit NPC生物对象
        void SendAuctionHello(ObjectGuid guid, Creature* unit);
        /// 发送拍卖命令结果
        /// @param auctionItemId 拍卖物品ID
        /// @param command 拍卖操作类型
        /// @param errorCode 错误码
        /// @param bagResult 背包结果码
        void SendAuctionCommandResult(uint32 auctionItemId, AuctionAction command, AuctionError errorCode, InventoryResult bagResult = InventoryResult(0));
        /// 发送拍卖竞拍者通知
        void SendAuctionBidderNotification(uint32 location, uint32 auctionId, ObjectGuid bidder, uint32 bidSum, uint32 diff, uint32 item_template);
        /// 发送拍卖所有者通知
        /// @param auction 拍卖条目指针
        void SendAuctionOwnerNotification(AuctionEntry* auction);

        //Item Enchantment
        void SendEnchantmentLog(ObjectGuid target, ObjectGuid caster, uint32 itemId, uint32 enchantId);
        void SendItemEnchantTimeUpdate(ObjectGuid Playerguid, ObjectGuid Itemguid, uint32 slot, uint32 Duration);

        //Taxi
        void SendTaxiStatus(ObjectGuid guid);
        void SendTaxiMenu(Creature* unit);
        void SendDoFlight(uint32 mountDisplayId, uint32 path, uint32 pathNode = 0);
        bool SendLearnNewTaxiNode(Creature* unit);
        void SendDiscoverNewTaxiNode(uint32 nodeid);

        // Guild/Arena Team
        void SendArenaTeamCommandResult(uint32 team_action, std::string const& team, std::string const& player, uint32 error_id = 0);
        void SendNotInArenaTeamPacket(uint8 type);
        void SendPetitionShowList(ObjectGuid guid);

        void BuildPartyMemberStatsChangedPacket(Player* player, WorldPacket* data);

        void DoLootRelease(ObjectGuid lguid);

        // 账号禁言时间管理
        /// 检查玩家是否可以发言（是否已过禁言期）
        bool CanSpeak() const;
        time_t m_muteTime;                                  // 禁言结束时间 - 0表示未禁言，非0表示禁言结束的时间戳

        // Locales
        LocaleConstant GetSessionDbcLocale() const { return m_sessionDbcLocale; }
        LocaleConstant GetSessionDbLocaleIndex() const { return m_sessionDbLocaleIndex; }

        Minutes GetTimezoneOffset() const { return _timezoneOffset; }

        char const* GetTrinityString(uint32 entry) const;

        /// 获取网络延迟（毫秒）
        uint32 GetLatency() const { return m_latency; }
        /// 设置网络延迟（毫秒）
        void SetLatency(uint32 latency) { m_latency = latency; }

        std::atomic<time_t> m_timeOutTime;                  // 会话超时时间 - 用于检测空闲连接

        /// 重置超时时间
        /// @param onlyActive 是否仅针对活跃连接
        void ResetTimeOutTime(bool onlyActive);

        /// 检查连接是否空闲
        bool IsConnectionIdle() const;

        // 招募好友（Recruit-A-Friend）功能处理
        /// 获取招募者ID
        uint32 GetRecruiterId() const { return recruiterId; }
        /// 检查是否为招募者
        bool IsARecruiter() const { return isRecruiter; }

        // 时间同步功能
        /// 重置时间同步计数器
        void ResetTimeSync();
        /// 发送时间同步数据包给客户端
        void SendTimeSync();

        // 数据包冷却时间
        /// 获取日历事件创建冷却时间
        time_t GetCalendarEventCreationCooldown() const { return _calendarEventCreationCooldown; }
        /// 设置日历事件创建冷却时间
        /// @param cooldown 冷却结束时间戳
        void SetCalendarEventCreationCooldown(time_t cooldown) { _calendarEventCreationCooldown = cooldown; }

    public:                                                 // 操作码处理器 - 处理各种客户端请求

        void Handle_NULL(WorldPacket& recvPacket);          // 未使用的操作码处理器
        void Handle_EarlyProccess(WorldPacket& recvPacket); // 早期处理 - 在WorldSocket::ReadDataHandler中标记已处理的数据包
        void Handle_ServerSide(WorldPacket& recvPacket);    // 服务器端专用 - 不应从客户端接收
        void Handle_Deprecated(WorldPacket& recvPacket);    // 已弃用的操作码 - 客户端不再使用

        void HandleCharEnumOpcode(WorldPacket& recvPacket);
        void HandleCharDeleteOpcode(WorldPacket& recvPacket);
        void HandleCharCreateOpcode(WorldPacket& recvPacket);
        void HandlePlayerLoginOpcode(WorldPacket& recvPacket);
        void HandleCharEnum(PreparedQueryResult result);
        void HandlePlayerLogin(LoginQueryHolder const& holder);
        void HandleCharFactionOrRaceChange(WorldPacket& recvData);
        void HandleCharFactionOrRaceChangeCallback(std::shared_ptr<CharacterFactionChangeInfo> factionChangeInfo, PreparedQueryResult result);
        void HandleCharRenameOpcode(WorldPacket& recvData);
        void HandleCharRenameCallBack(std::shared_ptr<CharacterRenameInfo> renameInfo, PreparedQueryResult result);
        void HandleSetPlayerDeclinedNames(WorldPacket& recvData);
        void HandleAlterAppearance(WorldPacket& recvData);
        void HandleCharCustomize(WorldPacket& recvData);
        void HandleCharCustomizeCallback(std::shared_ptr<CharacterCustomizeInfo> customizeInfo, PreparedQueryResult result);
        void HandleOpeningCinematic(WorldPackets::Misc::OpeningCinematic& packet);

        void SendCharCreate(ResponseCodes result);
        void SendCharDelete(ResponseCodes result);
        void SendCharRename(ResponseCodes result, CharacterRenameInfo const* renameInfo);
        void SendCharCustomize(ResponseCodes result, CharacterCustomizeInfo const* customizeInfo);
        void SendCharFactionChange(ResponseCodes result, CharacterFactionChangeInfo const* factionChangeInfo);
        void SendSetPlayerDeclinedNamesResult(DeclinedNameResult result, ObjectGuid guid);
        void SendBarberShopResult(BarberShopResult result);

        // played time
        void HandlePlayedTime(WorldPackets::Character::PlayedTimeClient& packet);

        // new inspect
        void HandleInspectOpcode(WorldPacket& recvPacket);

        // new party stats
        void HandleInspectHonorStatsOpcode(WorldPacket& recvPacket);

        void HandleForceSpeedChangeAck(WorldPacket& recvData);
        void HandleMoveKnockBackAck(WorldPacket& recvPacket);
        void HandleMoveTeleportAck(WorldPacket& recvPacket);
        void HandleMoveWaterWalkAck(WorldPacket& recvPacket);
        void HandleFeatherFallAck(WorldPacket& recvData);
        void HandleMoveHoverAck(WorldPacket& recvData);
        void HandleMoveUnRootAck(WorldPacket& recvPacket);
        void HandleMoveRootAck(WorldPacket& recvPacket);
        void HandleMoveSetCanFlyAckOpcode(WorldPacket& recvData);
        void HandleMoveSetCanTransitionBetweenSwinAndFlyAck(WorldPacket& recvData);
        void HandleMoveGravityDisableAck(WorldPacket& recvData);
        void HandleMoveGravityEnableAck(WorldPacket& recvData);
        void HandleMoveSetCollisionHgtAck(WorldPacket& recvData);

        void HandleMountSpecialAnimOpcode(WorldPacket& recvdata);

        // character view
        void HandleShowingHelmOpcode(WorldPackets::Character::ShowingHelm& packet);
        void HandleShowingCloakOpcode(WorldPackets::Character::ShowingCloak& packet);

        // repair
        void HandleRepairItemOpcode(WorldPacket& recvPacket);

        void HandleRepopRequest(WorldPackets::Misc::RepopRequest& packet);
        void HandleAutostoreLootItemOpcode(WorldPacket& recvPacket);
        void HandleLootMoneyOpcode(WorldPacket& recvPacket);
        void HandleLootOpcode(WorldPacket& recvPacket);
        void HandleLootReleaseOpcode(WorldPacket& recvPacket);
        void HandleLootMasterGiveOpcode(WorldPacket& recvPacket);
        void HandleWhoOpcode(WorldPacket& recvPacket);
        void HandleLogoutRequestOpcode(WorldPackets::Character::LogoutRequest& logoutRequest);
        void HandlePlayerLogoutOpcode(WorldPackets::Character::PlayerLogout& playerLogout);
        void HandleLogoutCancelOpcode(WorldPackets::Character::LogoutCancel& logoutCancel);

        // GM Ticket opcodes
        void HandleGMTicketCreateOpcode(WorldPacket& recvPacket);
        void HandleGMTicketUpdateOpcode(WorldPacket& recvPacket);
        void HandleGMTicketDeleteOpcode(WorldPacket& recvPacket);
        void HandleGMTicketGetTicketOpcode(WorldPacket& recvPacket);
        void HandleGMTicketSystemStatusOpcode(WorldPacket& recvPacket);
        void HandleGMSurveySubmit(WorldPacket& recvPacket);
        void HandleReportLag(WorldPacket& recvPacket);
        void HandleGMResponseResolve(WorldPacket& recvPacket);

        void HandleTogglePvP(WorldPackets::Misc::TogglePvP& togglePvP);

        void HandleZoneUpdateOpcode(WorldPacket& recvPacket);
        void HandleSetSelectionOpcode(WorldPacket& recvPacket);
        void HandleStandStateChangeOpcode(WorldPacket& recvPacket);
        void HandleEmoteOpcode(WorldPackets::Chat::EmoteClient& packet);

        // Social
        void HandleContactListOpcode(WorldPacket& recvPacket);
        void HandleAddFriendOpcode(WorldPacket& recvPacket);
        void HandleDelFriendOpcode(WorldPacket& recvPacket);
        void HandleAddIgnoreOpcode(WorldPacket& recvPacket);
        void HandleDelIgnoreOpcode(WorldPacket& recvPacket);
        void HandleSetContactNotesOpcode(WorldPacket& recvPacket);
        void HandleBugOpcode(WorldPacket& recvPacket);
        void HandleSetAmmoOpcode(WorldPacket& recvPacket);
        void HandleItemNameQueryOpcode(WorldPacket& recvPacket);

        void HandleAreaTriggerOpcode(WorldPacket& recvPacket);

        void HandleSetFactionAtWar(WorldPacket& recvData);
        void HandleSetFactionCheat(WorldPacket& recvData);
        void HandleSetWatchedFactionOpcode(WorldPacket& recvData);
        void HandleSetFactionInactiveOpcode(WorldPacket& recvData);

        void HandleUpdateAccountData(WorldPacket& recvPacket);
        void HandleRequestAccountData(WorldPacket& recvPacket);
        void HandleSetActionButtonOpcode(WorldPacket& recvPacket);

        void HandleGameObjectUseOpcode(WorldPacket& recPacket);
        void HandleGameobjectReportUse(WorldPacket& recvPacket);

        void HandleNameQueryOpcode(WorldPacket& recvPacket);

        void HandleQueryTimeOpcode(WorldPacket& recvPacket);

        void HandleCreatureQueryOpcode(WorldPackets::Query::QueryCreature& query);

        void HandleGameObjectQueryOpcode(WorldPackets::Query::QueryGameObject& query);

        void HandleMoveWorldportAckOpcode(WorldPacket& recvPacket);
        void HandleMoveWorldportAck();                // for server-side calls

        void HandleMovementOpcodes(WorldPacket& recvPacket);
        void HandleSetActiveMoverOpcode(WorldPacket& recvData);
        void HandleMoveNotActiveMover(WorldPacket& recvData);
        void HandleDismissControlledVehicle(WorldPacket& recvData);
        void HandleRequestVehicleExit(WorldPacket& recvData);
        void HandleChangeSeatsOnControlledVehicle(WorldPacket& recvData);
        void HandleMoveTimeSkippedOpcode(WorldPacket& recvData);

        void HandleRequestRaidInfoOpcode(WorldPacket& recvData);

        void HandleBattlefieldStatusOpcode(WorldPacket& recvData);

        void HandleGroupInviteOpcode(WorldPacket& recvPacket);
        void HandleGroupAcceptOpcode(WorldPacket& recvPacket);
        void HandleGroupDeclineOpcode(WorldPacket& recvPacket);
        void HandleGroupUninviteOpcode(WorldPacket& recvPacket);
        void HandleGroupUninviteGuidOpcode(WorldPacket& recvPacket);
        void HandleGroupSetLeaderOpcode(WorldPacket& recvPacket);
        void HandleGroupDisbandOpcode(WorldPacket& recvPacket);
        void HandleOptOutOfLootOpcode(WorldPacket& recvData);
        void HandleLootMethodOpcode(WorldPacket& recvPacket);
        void HandleLootRoll(WorldPacket& recvData);
        void HandleRequestPartyMemberStatsOpcode(WorldPacket& recvData);
        void HandleRaidTargetUpdateOpcode(WorldPacket& recvData);
        void HandleRaidReadyCheckOpcode(WorldPacket& recvData);
        void HandleRaidReadyCheckFinishedOpcode(WorldPacket& recvData);
        void HandleGroupRaidConvertOpcode(WorldPacket& recvData);
        void HandleGroupChangeSubGroupOpcode(WorldPacket& recvData);
        void HandleGroupAssistantLeaderOpcode(WorldPacket& recvData);
        void HandlePartyAssignmentOpcode(WorldPacket& recvData);

        void HandlePetitionBuyOpcode(WorldPacket& recvData);
        void HandlePetitionShowSignatures(WorldPacket& recvData);
        void SendPetitionSigns(Petition const* petition, Player* sendTo);
        void HandleQueryPetition(WorldPacket& recvData);
        void HandlePetitionRenameGuild(WorldPacket& recvData);
        void HandleSignPetition(WorldPacket& recvData);
        void HandleDeclinePetition(WorldPacket& recvData);
        void HandleOfferPetitionOpcode(WorldPacket& recvData);
        void HandleTurnInPetitionOpcode(WorldPacket& recvData);

        void HandleGuildQueryOpcode(WorldPackets::Guild::QueryGuildInfo& query);
        void HandleGuildCreateOpcode(WorldPackets::Guild::GuildCreate& packet);
        void HandleGuildInviteOpcode(WorldPackets::Guild::GuildInviteByName& packet);
        void HandleGuildRemoveOpcode(WorldPackets::Guild::GuildOfficerRemoveMember& packet);
        void HandleGuildAcceptOpcode(WorldPackets::Guild::AcceptGuildInvite& invite);
        void HandleGuildDeclineOpcode(WorldPackets::Guild::GuildDeclineInvitation& decline);
        void HandleGuildInfoOpcode(WorldPackets::Guild::GuildGetInfo& packet);
        void HandleGuildEventLogQueryOpcode(WorldPackets::Guild::GuildEventLogQuery& packet);
        void HandleGuildRosterOpcode(WorldPackets::Guild::GuildGetRoster& packet);
        void HandleGuildPromoteOpcode(WorldPackets::Guild::GuildPromoteMember& promote);
        void HandleGuildDemoteOpcode(WorldPackets::Guild::GuildDemoteMember& demote);
        void HandleGuildLeaveOpcode(WorldPackets::Guild::GuildLeave& leave);
        void HandleGuildDelete(WorldPackets::Guild::GuildDelete& packet);
        void HandleGuildSetGuildMaster(WorldPackets::Guild::GuildSetGuildMaster& packet);
        void HandleGuildUpdateMotdText(WorldPackets::Guild::GuildUpdateMotdText& packet);
        void HandleGuildSetPublicNoteOpcode(WorldPackets::Guild::GuildSetMemberNote& packet);
        void HandleGuildSetOfficerNoteOpcode(WorldPackets::Guild::GuildSetMemberNote& packet);
        void HandleGuildSetRankPermissions(WorldPackets::Guild::GuildSetRankPermissions& packet);
        void HandleGuildAddRankOpcode(WorldPackets::Guild::GuildAddRank& packet);
        void HandleGuildDeleteRank(WorldPackets::Guild::GuildDeleteRank& packet);
        void HandleGuildUpdateInfoText(WorldPackets::Guild::GuildUpdateInfoText& packet);
        void HandleSaveGuildEmblemOpcode(WorldPackets::Guild::SaveGuildEmblem& packet);

        void HandleTaxiNodeStatusQueryOpcode(WorldPacket& recvPacket);
        void HandleTaxiQueryAvailableNodes(WorldPacket& recvPacket);
        void HandleActivateTaxiOpcode(WorldPacket& recvPacket);
        void HandleActivateTaxiExpressOpcode(WorldPacket& recvPacket);
        void HandleMoveSplineDoneOpcode(WorldPacket& recvPacket);
        void SendActivateTaxiReply(ActivateTaxiReply reply);

        void HandleTabardVendorActivateOpcode(WorldPacket& recvPacket);
        void HandleTrainerListOpcode(WorldPackets::NPC::Hello& packet);
        void HandleTrainerBuySpellOpcode(WorldPackets::NPC::TrainerBuySpell& packet);
        void HandlePetitionShowListOpcode(WorldPacket& recvPacket);
        void HandleGossipHelloOpcode(WorldPacket& recvPacket);
        void HandleGossipSelectOptionOpcode(WorldPacket& recvPacket);
        void HandleSpiritHealerActivateOpcode(WorldPacket& recvPacket);
        void HandleNpcTextQueryOpcode(WorldPacket& recvPacket);
        void HandleBinderActivateOpcode(WorldPacket& recvPacket);
        void HandleRequestStabledPets(WorldPacket& recvPacket);
        void HandleStablePet(WorldPacket& recvPacket);
        void HandleUnstablePet(WorldPacket& recvPacket);
        void HandleBuyStableSlot(WorldPacket& recvPacket);
        void HandleStableRevivePet(WorldPacket& recvPacket);
        void HandleStableSwapPet(WorldPacket& recvPacket);

        void HandleDuelAcceptedOpcode(WorldPacket& recvPacket);
        void HandleDuelCancelledOpcode(WorldPacket& recvPacket);

        void HandleAcceptTradeOpcode(WorldPacket& recvPacket);
        void HandleBeginTradeOpcode(WorldPacket& recvPacket);
        void HandleBusyTradeOpcode(WorldPacket& recvPacket);
        void HandleCancelTradeOpcode(WorldPacket& recvPacket);
        void HandleClearTradeItemOpcode(WorldPacket& recvPacket);
        void HandleIgnoreTradeOpcode(WorldPacket& recvPacket);
        void HandleInitiateTradeOpcode(WorldPacket& recvPacket);
        void HandleSetTradeGoldOpcode(WorldPacket& recvPacket);
        void HandleSetTradeItemOpcode(WorldPacket& recvPacket);
        void HandleUnacceptTradeOpcode(WorldPacket& recvPacket);

        void HandleAuctionHelloOpcode(WorldPacket& recvPacket);
        void HandleAuctionListItems(WorldPacket& recvData);
        void HandleAuctionListBidderItems(WorldPacket& recvData);
        void HandleAuctionSellItem(WorldPacket& recvData);
        void HandleAuctionRemoveItem(WorldPacket& recvData);
        void HandleAuctionListOwnerItems(WorldPacket& recvData);
        void HandleAuctionPlaceBid(WorldPacket& recvData);
        void HandleAuctionListPendingSales(WorldPacket& recvData);

        // Bank
        void HandleBankerActivateOpcode(WorldPackets::NPC::Hello& packet);
        void HandleAutoBankItemOpcode(WorldPackets::Bank::AutoBankItem& packet);
        void HandleAutoStoreBankItemOpcode(WorldPackets::Bank::AutoStoreBankItem& packet);
        void HandleBuyBankSlotOpcode(WorldPackets::Bank::BuyBankSlot& buyBankSlot);

        void HandleGetMailList(WorldPackets::Mail::MailGetList& getList);
        void HandleSendMail(WorldPackets::Mail::SendMail& sendMail);
        void HandleMailTakeMoney(WorldPackets::Mail::MailTakeMoney& takeMoney);
        void HandleMailTakeItem(WorldPackets::Mail::MailTakeItem& takeItem);
        void HandleMailMarkAsRead(WorldPackets::Mail::MailMarkAsRead& markAsRead);
        void HandleMailReturnToSender(WorldPackets::Mail::MailReturnToSender& returnToSender);
        void HandleMailDelete(WorldPackets::Mail::MailDelete& mailDelete);
        void HandleItemTextQuery(WorldPacket& recvData);
        void HandleMailCreateTextItem(WorldPackets::Mail::MailCreateTextItem& createTextItem);
        void HandleQueryNextMailTime(WorldPackets::Mail::MailQueryNextMailTime& queryNextMailTime);

        void HandleSplitItemOpcode(WorldPacket& recvPacket);
        void HandleSwapInvItemOpcode(WorldPacket& recvPacket);
        void HandleDestroyItemOpcode(WorldPacket& recvPacket);
        void HandleAutoEquipItemOpcode(WorldPacket& recvPacket);
        void HandleItemQuerySingleOpcode(WorldPackets::Query::QueryItemSingle& query);
        void HandleSellItemOpcode(WorldPacket& recvPacket);
        void HandleBuyItemInSlotOpcode(WorldPacket& recvPacket);
        void HandleBuyItemOpcode(WorldPacket& recvPacket);
        void HandleListInventoryOpcode(WorldPacket& recvPacket);
        void HandleAutoStoreBagItemOpcode(WorldPacket& recvPacket);
        void HandleReadItem(WorldPacket& recvPacket);
        void HandleAutoEquipItemSlotOpcode(WorldPacket& recvPacket);
        void HandleSwapItem(WorldPacket& recvPacket);
        void HandleBuybackItem(WorldPacket& recvPacket);
        void HandleWrapItemOpcode(WorldPacket& recvPacket);

        void HandleAttackSwingOpcode(WorldPackets::Combat::AttackSwing& packet);
        void HandleAttackStopOpcode(WorldPackets::Combat::AttackStop& packet);
        void HandleSetSheathedOpcode(WorldPackets::Combat::SetSheathed& packet);

        void HandleUseItemOpcode(WorldPacket& recvPacket);
        void HandleOpenItemOpcode(WorldPacket& recvPacket);
        void HandleOpenWrappedItemCallback(uint16 pos, ObjectGuid itemGuid, PreparedQueryResult result);
        void HandleCastSpellOpcode(WorldPacket& recvPacket);
        void HandleCancelCastOpcode(WorldPackets::Spells::CancelCast& cancelCast);
        void HandleCancelAuraOpcode(WorldPackets::Spells::CancelAura& cancelAura);
        void HandleCancelGrowthAuraOpcode(WorldPackets::Spells::CancelGrowthAura& cancelGrowthAura);
        void HandleCancelMountAuraOpcode(WorldPackets::Spells::CancelMountAura& cancelMountAura);
        void HandleCancelAutoRepeatSpellOpcode(WorldPackets::Spells::CancelAutoRepeatSpell& cancelAutoRepeatSpell);
        void HandleCancelChanneling(WorldPackets::Spells::CancelChannelling& cancelChanneling);

        void HandleLearnTalentOpcode(WorldPacket& recvPacket);
        void HandleLearnPreviewTalents(WorldPacket& recvPacket);
        void HandleTalentWipeConfirmOpcode(WorldPackets::Talents::ConfirmRespecWipe& confirmRespecWipe);
        void HandleUnlearnSkillOpcode(WorldPacket& recvPacket);

        void HandleQuestgiverStatusQueryOpcode(WorldPacket& recvPacket);
        void HandleQuestgiverStatusMultipleQuery(WorldPacket& recvPacket);
        void HandleQuestgiverHelloOpcode(WorldPacket& recvPacket);
        void HandleQuestgiverAcceptQuestOpcode(WorldPacket& recvPacket);
        void HandleQuestgiverQueryQuestOpcode(WorldPacket& recvPacket);
        void HandleQuestgiverChooseRewardOpcode(WorldPacket& recvPacket);
        void HandleQuestgiverRequestRewardOpcode(WorldPacket& recvPacket);
        void HandleQuestQueryOpcode(WorldPackets::Quest::QueryQuestInfo& query);
        void HandleQuestgiverCancel(WorldPacket& recvData);
        void HandleQuestLogSwapQuest(WorldPacket& recvData);
        void HandleQuestLogRemoveQuest(WorldPacket& recvData);
        void HandleQuestConfirmAccept(WorldPacket& recvData);
        void HandleQuestgiverCompleteQuest(WorldPacket& recvData);
        void HandleQuestgiverQuestAutoLaunch(WorldPacket& recvPacket);
        void HandlePushQuestToParty(WorldPacket& recvPacket);
        void HandleQuestPushResult(WorldPacket& recvPacket);

        void HandleMessagechatOpcode(WorldPacket& recvPacket);
        void SendPlayerNotFoundNotice(std::string const& name);
        void SendPlayerAmbiguousNotice(std::string const& name);
        void SendWrongFactionNotice();
        void SendChatRestrictedNotice(ChatRestrictionType restriction);
        void HandleTextEmoteOpcode(WorldPacket& recvPacket);
        void HandleChatIgnoredOpcode(WorldPacket& recvPacket);

        void HandleReclaimCorpse(WorldPackets::Misc::ReclaimCorpse& packet);
        void HandleCorpseQueryOpcode(WorldPacket& recvPacket);
        void HandleCorpseMapPositionQuery(WorldPacket& recvPacket);
        void HandleResurrectResponse(WorldPackets::Misc::ResurrectResponse& packet);
        void HandleSummonResponseOpcode(WorldPacket& recvData);

        void HandleJoinChannel(WorldPacket& recvPacket);
        void HandleLeaveChannel(WorldPacket& recvPacket);
        void HandleChannelList(WorldPacket& recvPacket);
        void HandleChannelPassword(WorldPacket& recvPacket);
        void HandleChannelSetOwner(WorldPacket& recvPacket);
        void HandleChannelOwner(WorldPacket& recvPacket);
        void HandleChannelModerator(WorldPacket& recvPacket);
        void HandleChannelUnmoderator(WorldPacket& recvPacket);
        void HandleChannelMute(WorldPacket& recvPacket);
        void HandleChannelUnmute(WorldPacket& recvPacket);
        void HandleChannelInvite(WorldPacket& recvPacket);
        void HandleChannelKick(WorldPacket& recvPacket);
        void HandleChannelBan(WorldPacket& recvPacket);
        void HandleChannelUnban(WorldPacket& recvPacket);
        void HandleChannelAnnouncements(WorldPacket& recvPacket);
        void HandleChannelDeclineInvite(WorldPacket& recvPacket);
        void HandleChannelDisplayListQuery(WorldPacket& recvPacket);
        void HandleGetChannelMemberCount(WorldPacket& recvPacket);
        void HandleSetChannelWatch(WorldPacket& recvPacket);

        void HandleCompleteCinematic(WorldPackets::Misc::CompleteCinematic& packet);
        void HandleNextCinematicCamera(WorldPackets::Misc::NextCinematicCamera& packet);
        void HandleCompleteMovie(WorldPackets::Misc::CompleteMovie& packet);

        void HandleQueryPageText(WorldPacket& recvPacket);

        void HandleTutorialFlag (WorldPacket& recvData);
        void HandleTutorialClear(WorldPacket& recvData);
        void HandleTutorialReset(WorldPacket& recvData);

        //Pet
        void HandlePetAction(WorldPacket& recvData);
        void HandlePetStopAttack(WorldPackets::Pet::PetStopAttack& packet);
        void HandlePetActionHelper(Unit* pet, ObjectGuid guid1, uint32 spellid, uint16 flag, ObjectGuid guid2);
        void HandleQueryPetName(WorldPacket& recvData);
        void HandlePetSetAction(WorldPacket& recvData);
        void HandlePetAbandon(WorldPackets::Pet::PetAbandon& packet);
        void HandlePetRename(WorldPacket& recvData);
        void HandlePetCancelAuraOpcode(WorldPackets::Spells::PetCancelAura& packet);
        void HandlePetSpellAutocastOpcode(WorldPackets::Pet::PetSpellAutocast& packet);
        void HandlePetCastSpellOpcode(WorldPacket& recvPacket);
        void HandlePetLearnTalent(WorldPacket& recvPacket);
        void HandleLearnPreviewTalentsPet(WorldPacket& recvPacket);

        void HandleSetActionBarToggles(WorldPacket& recvData);

        void HandleTotemDestroyed(WorldPackets::Totem::TotemDestroyed& totemDestroyed);
        void HandleDismissCritter(WorldPackets::Pet::DismissCritter& dismissCritter);

        //Battleground
        void HandleBattlemasterHelloOpcode(WorldPacket& recvData);
        void HandleBattlemasterJoinOpcode(WorldPacket& recvData);
        void HandleBattlegroundPlayerPositionsOpcode(WorldPacket& recvData);
        void HandlePVPLogDataOpcode(WorldPacket& recvData);
        void HandleBattleFieldPortOpcode(WorldPacket& recvData);
        void HandleBattlefieldListOpcode(WorldPacket& recvData);
        void HandleBattlefieldLeaveOpcode(WorldPacket& recvData);
        void HandleBattlemasterJoinArena(WorldPacket& recvData);
        void HandleReportPvPAFK(WorldPacket& recvData);

        // Battlefield
        void SendBfInvitePlayerToWar(uint32 battleId, uint32 zoneId, uint32 time);
        void SendBfInvitePlayerToQueue(uint32 battleId);
        void SendBfQueueInviteResponse(uint32 battleId, uint32 zoneId, bool canQueue = true, bool full = false);
        void SendBfEntered(uint32 battleId);
        void SendBfLeaveMessage(uint32 battleId, BFLeaveReason reason = BF_LEAVE_REASON_EXITED);
        void HandleBfQueueInviteResponse(WorldPacket& recvData);
        void HandleBfEntryInviteResponse(WorldPacket& recvData);
        void HandleBfQueueExitRequest(WorldPacket& recvData);

        void HandleWardenDataOpcode(WorldPacket& recvData);
        void HandleWorldTeleportOpcode(WorldPackets::Misc::WorldTeleport& worldTeleport);
        void HandleMinimapPingOpcode(WorldPacket& recvData);
        void HandleRandomRollOpcode(WorldPackets::Misc::RandomRollClient& packet);
        void HandleFarSightOpcode(WorldPacket& recvData);
        void HandleSetDungeonDifficultyOpcode(WorldPacket& recvData);
        void HandleSetRaidDifficultyOpcode(WorldPacket& recvData);
        void HandleSetTitleOpcode(WorldPacket& recvData);
        void HandleRealmSplitOpcode(WorldPacket& recvData);
        void HandleTimeSyncResponse(WorldPacket& recvData);
        void HandleWhoIsOpcode(WorldPacket& recvData);
        void HandleResetInstancesOpcode(WorldPacket& recvData);
        void HandleHearthAndResurrect(WorldPacket& recvData);
        void HandleInstanceLockResponse(WorldPacket& recvPacket);

        // Looking for Dungeon/Raid
        void HandleLfgSetCommentOpcode(WorldPacket& recvData);
        void HandleLfgPlayerLockInfoRequestOpcode(WorldPacket& recvData);
        void HandleLfgPartyLockInfoRequestOpcode(WorldPacket& recvData);
        void HandleLfgJoinOpcode(WorldPackets::LFG::LFGJoin& lfgJoin);
        void HandleLfgLeaveOpcode(WorldPackets::LFG::LFGLeave& lfgleave);
        void HandleLfgSetRolesOpcode(WorldPacket& recvData);
        void HandleLfgProposalResultOpcode(WorldPacket& recvData);
        void HandleLfgSetBootVoteOpcode(WorldPacket& recvData);
        void HandleLfgTeleportOpcode(WorldPacket& recvData);
        void HandleLfrJoinOpcode(WorldPacket& recvData);
        void HandleLfrLeaveOpcode(WorldPacket& recvData);
        void HandleLfgGetStatus(WorldPacket& recvData);

        void SendLfgUpdatePlayer(lfg::LfgUpdateData const& updateData);
        void SendLfgUpdateParty(lfg::LfgUpdateData const& updateData);
        void SendLfgRoleChosen(ObjectGuid guid, uint8 roles);
        void SendLfgRoleCheckUpdate(lfg::LfgRoleCheck const& pRoleCheck);
        void SendLfgLfrList(bool update);
        void SendLfgJoinResult(lfg::LfgJoinResultData const& joinData);
        void SendLfgQueueStatus(lfg::LfgQueueStatusData const& queueData);
        void SendLfgPlayerReward(lfg::LfgPlayerRewardData const& lfgPlayerRewardData);
        void SendLfgBootProposalUpdate(lfg::LfgPlayerBoot const& boot);
        void SendLfgUpdateProposal(lfg::LfgProposal const& proposal);
        void SendLfgDisabled();
        void SendLfgOfferContinue(uint32 dungeonEntry);
        void SendLfgTeleportError(uint8 err);

        // Arena Team
        void HandleInspectArenaTeamsOpcode(WorldPacket& recvData);
        void HandleArenaTeamQueryOpcode(WorldPacket& recvData);
        void HandleArenaTeamRosterOpcode(WorldPacket& recvData);
        void HandleArenaTeamInviteOpcode(WorldPacket& recvData);
        void HandleArenaTeamAcceptOpcode(WorldPacket& recvData);
        void HandleArenaTeamDeclineOpcode(WorldPacket& recvData);
        void HandleArenaTeamLeaveOpcode(WorldPacket& recvData);
        void HandleArenaTeamRemoveOpcode(WorldPacket& recvData);
        void HandleArenaTeamDisbandOpcode(WorldPacket& recvData);
        void HandleArenaTeamLeaderOpcode(WorldPacket& recvData);

        void HandleAreaSpiritHealerQueryOpcode(WorldPacket& recvData);
        void HandleAreaSpiritHealerQueueOpcode(WorldPacket& recvData);
        void HandleSelfResOpcode(WorldPacket& recvData);
        void HandleComplainOpcode(WorldPacket& recvData);
        void HandleRequestPetInfo(WorldPackets::Pet::RequestPetInfo& packet);

        // Socket gem
        void HandleSocketOpcode(WorldPacket& recvData);

        void HandleCancelTempEnchantmentOpcode(WorldPacket& recvData);

        void HandleItemRefundInfoRequest(WorldPacket& recvData);
        void HandleItemRefund(WorldPacket& recvData);

        void HandleChannelVoiceOnOpcode(WorldPacket& recvData);
        void HandleVoiceSessionEnableOpcode(WorldPacket& recvData);
        void HandleSetActiveVoiceChannel(WorldPacket& recvData);
        void HandleSetTaxiBenchmarkOpcode(WorldPacket& recvData);

        // Guild Bank
        void HandleGuildPermissionsQuery(WorldPackets::Guild::GuildPermissionsQuery& packet);
        void HandleGuildBankMoneyWithdrawn(WorldPackets::Guild::GuildBankRemainingWithdrawMoneyQuery& packet);
        void HandleGuildBankActivate(WorldPackets::Guild::GuildBankActivate& packet);
        void HandleGuildBankQueryTab(WorldPackets::Guild::GuildBankQueryTab& packet);
        void HandleGuildBankLogQuery(WorldPackets::Guild::GuildBankLogQuery& packet);
        void HandleGuildBankDepositMoney(WorldPackets::Guild::GuildBankDepositMoney& packet);
        void HandleGuildBankWithdrawMoney(WorldPackets::Guild::GuildBankWithdrawMoney& packet);
        void HandleGuildBankSwapItems(WorldPackets::Guild::GuildBankSwapItems& packet);

        void HandleGuildBankUpdateTab(WorldPackets::Guild::GuildBankUpdateTab& packet);
        void HandleGuildBankBuyTab(WorldPackets::Guild::GuildBankBuyTab& packet);
        void HandleGuildBankTextQuery(WorldPackets::Guild::GuildBankTextQuery& packet);
        void HandleGuildBankSetTabText(WorldPackets::Guild::GuildBankSetTabText& packet);

        // Refer-a-Friend
        void HandleGrantLevel(WorldPacket& recvData);
        void HandleAcceptGrantLevel(WorldPacket& recvData);

        // Calendar
        void HandleCalendarGetCalendar(WorldPackets::Calendar::CalendarGetCalendar& calendarGetCalendar);
        void HandleCalendarGetEvent(WorldPackets::Calendar::CalendarGetEvent& calendarGetEvent);
        void HandleCalendarGuildFilter(WorldPackets::Calendar::CalendarGuildFilter& calendarGuildFilter);
        void HandleCalendarArenaTeam(WorldPackets::Calendar::CalendarArenaTeam& calendarArenaTeam);
        void HandleCalendarAddEvent(WorldPackets::Calendar::CalendarAddEvent& calendarAddEvent);
        void HandleCalendarUpdateEvent(WorldPackets::Calendar::CalendarUpdateEvent& calendarUpdateEvent);
        void HandleCalendarRemoveEvent(WorldPackets::Calendar::CalendarRemoveEvent& calendarRemoveEvent);
        void HandleCalendarCopyEvent(WorldPackets::Calendar::CalendarCopyEvent& calendarCopyEvent);
        void HandleCalendarEventInvite(WorldPackets::Calendar::CalendarInvite& calendarEventInvite);
        void HandleCalendarEventRsvp(WorldPackets::Calendar::CalendarRSVP& calendarRSVP);
        void HandleCalendarEventRemoveInvite(WorldPackets::Calendar::CalendarRemoveInvite& calendarRemoveInvite);
        void HandleCalendarEventStatus(WorldPackets::Calendar::CalendarStatus& calendarStatus);
        void HandleCalendarEventModeratorStatus(WorldPackets::Calendar::CalendarModeratorStatusQuery& calendarModeratorStatus);
        void HandleCalendarComplain(WorldPackets::Calendar::CalendarComplain& calendarComplain);
        void HandleCalendarGetNumPending(WorldPackets::Calendar::CalendarGetNumPending& calendarGetNumPending);
        void HandleCalendarEventSignup(WorldPackets::Calendar::CalendarEventSignUp& calendarEventSignUp);

        void SendCalendarRaidLockoutAdded(InstanceSave const* save);
        void SendCalendarRaidLockoutRemoved(InstanceSave const* save);
        void SendCalendarRaidLockoutUpdated(InstanceSave const* save);
        void HandleSetSavedInstanceExtend(WorldPackets::Calendar::SetSavedInstanceExtend& setSavedInstanceExtend);

        void HandleSpellClick(WorldPacket& recvData);
        void HandleMirrorImageDataRequest(WorldPacket& recvData);
        void HandleRemoveGlyph(WorldPacket& recvData);
        void HandleQueryInspectAchievements(WorldPacket& recvData);
        void HandleEquipmentSetSave(WorldPacket& recvData);
        void HandleEquipmentSetDelete(WorldPacket& recvData);
        void HandleEquipmentSetUse(WorldPacket& recvData);
        void HandleWorldStateUITimerUpdate(WorldPacket& recvData);
        void HandleReadyForAccountDataTimes(WorldPacket& recvData);
        void HandleQueryQuestsCompleted(WorldPacket& recvData);
        void HandleQuestPOIQuery(WorldPackets::Query::QuestPOIQuery& query);
        void HandleEjectPassenger(WorldPacket& data);
        void HandleEnterPlayerVehicle(WorldPacket& data);
        void HandleUpdateProjectilePosition(WorldPacket& recvPacket);
        void HandleUpdateMissileTrajectory(WorldPacket& recvPacket);

    public:
        /// 获取查询处理器引用 - 用于处理异步数据库查询
        QueryCallbackProcessor& GetQueryProcessor() { return _queryProcessor; }
        /// 添加事务回调 - 用于异步数据库事务处理
        TransactionCallback& AddTransactionCallback(TransactionCallback&& callback);
        /// 添加查询持有者回调 - 用于处理包含多个查询的复杂操作
        SQLQueryHolderCallback& AddQueryHolderCallback(SQLQueryHolderCallback&& callback);

    private:
        /// 处理查询回调 - 每帧调用，处理已完成的异步查询
        void ProcessQueryCallbacks();

        QueryCallbackProcessor _queryProcessor;                                 // 查询回调处理器
        AsyncCallbackProcessor<TransactionCallback> _transactionCallbacks;      // 事务回调处理器
        AsyncCallbackProcessor<SQLQueryHolderCallback> _queryHolderProcessor;   // 查询持有者回调处理器

    friend class World;
    protected:
        /// DoS攻击防护类 - 检测和防止拒绝服务攻击
        /// 通过监控数据包频率来识别恶意客户端
        class DosProtection
        {
            friend class World;
            public:
                DosProtection(WorldSession* s);
                /// 评估操作码是否超过频率限制
                /// @param p 数据包引用
                /// @param time 当前时间
                /// @return 返回true表示数据包可以处理
                bool EvaluateOpcode(WorldPacket& p, time_t time) const;
            protected:
                /// 防护策略枚举
                enum Policy
                {
                    POLICY_LOG,     // 仅记录日志
                    POLICY_KICK,    // 踢出玩家
                    POLICY_BAN,     // 封禁账号
                };

                /// 获取指定操作码允许的最大数据包计数
                /// @param opcode 操作码
                /// @return 最大允许的数据包数量
                uint32 GetMaxPacketCounterAllowed(uint16 opcode) const;

                WorldSession* Session;      // 关联的会话指针

            private:
                Policy _policy;             // 当前防护策略
                typedef std::unordered_map<uint16, PacketCounter> PacketThrottlingMap;
                // 标记为"mutable"以便在const函数中也能修改
                mutable PacketThrottlingMap _PacketThrottlingMap;   // 数据包节流映射表

                DosProtection(DosProtection const& right) = delete;
                DosProtection& operator=(DosProtection const& right) = delete;
        } AntiDOS;          // 反DoS攻击实例

    private:
        // 私有交易方法
        /// 移动物品 - 在交易过程中交换物品
        /// @param myItems 我的物品数组
        /// @param hisItems 对方的物品数组
        void moveItems(Item* myItems[], Item* hisItems[]);

        /// 检查是否可以使用银行
        /// @param bankerGUID 银行家GUID（默认为空）
        /// @return 返回true表示可以使用银行
        bool CanUseBank(ObjectGuid bankerGUID = ObjectGuid::Empty) const;

        // 日志辅助方法
        /// 记录意外的操作码
        /// @param packet 数据包指针
        /// @param status 状态描述
        /// @param reason 原因描述
        void LogUnexpectedOpcode(WorldPacket* packet, char const* status, const char *reason);
        /// 记录未处理的数据包尾部
        /// @param packet 数据包指针
        void LogUnprocessedTail(WorldPacket* packet);

        // EnumData helpers
        bool IsLegitCharacterForAccount(ObjectGuid lowGUID)
        {
            return _legitCharacters.find(lowGUID) != _legitCharacters.end();
        }

        // 移动辅助方法
        /// 检查是否是正确的被移动单位
        /// @param guid 单位GUID
        /// @return 返回true表示是正确的单位
        bool IsRightUnitBeingMoved(ObjectGuid guid);

        // 存储可以登录的角色GUID集合
        // 在Player::BuildEnumData中失败的角色不应该登录
        GuidSet _legitCharacters;

        ObjectGuid::LowType m_GUIDLow;                      // 当前登录或最近登出玩家的GUID低32位（当m_playerRecentlyLogout设置时有效）
        Player* _player;                                    // 玩家对象指针 - 指向当前登录的玩家角色实例
        std::shared_ptr<WorldSocket> m_Socket;              // 网络套接字 - 管理与客户端的网络连接
        std::string m_Address;                              // 当前远程地址 - 客户端的IP地址
     // std::string m_LAddress;                             // Last Attempted Remote Adress - we can not set attempted ip for a non-existing session!

        AccountTypes _security;                             // 账号安全等级 - 用于权限控制（如GM权限）
        uint32 _accountId;                                  // 账号ID - 唯一标识符
        std::string _accountName;                           // 账号名称
        uint8 m_expansion;                                  // 资料片版本 - 客户端支持的资料片等级

        // Warden 反作弊系统
        std::unique_ptr<Warden> _warden;                    // Warden模块指针 - 如果配置未启用则为NULL，用于检测作弊

        time_t _logoutTime;                                 // 登出开始时间 - 记录玩家开始登出的时间戳，用于计算登出冷却
        bool m_inQueue;                                     // 会话是否在认证队列中等待
        bool m_playerLoading;                               // 玩家是否正在加载中（LoginPlayer处理中）
        bool m_playerLogout;                                // 玩家是否正在登出中（LogoutPlayer处理中）
        bool m_playerRecentlyLogout;                        // 玩家是否最近刚登出
        bool m_playerSave;                                  // 登出时是否需要保存玩家数据
        LocaleConstant m_sessionDbcLocale;                  // DBC语言设置 - 客户端DBC文件的语言
        LocaleConstant m_sessionDbLocaleIndex;              // 数据库语言索引 - 用于本地化字符串查询
        Minutes _timezoneOffset;                            // 时区偏移 - 玩家的时区信息
        std::atomic<uint32> m_latency;                      // 网络延迟 - 客户端与服务器之间的延迟（毫秒）
        AccountData m_accountData[NUM_ACCOUNT_DATA_TYPES];  // 账号数据缓存 - 存储配置、宏、快捷键等
        uint32 m_Tutorials[MAX_ACCOUNT_TUTORIAL_VALUES];    // 教程数据 - 存储玩家教程进度
        uint8  m_TutorialsChanged;                          // 教程数据变更标志
        /// 插件（Addon）相关数据结构
        struct Addons
        {
            /// 安全插件信息结构
            struct SecureAddonInfo
            {
                /// 安全插件状态枚举
                enum SecureAddonStatus : uint8
                {
                    BANNED          = 0,    // 被禁用的插件
                    SECURE_VISIBLE  = 1,    // 安全可见插件
                    SECURE_HIDDEN   = 2     // 安全隐藏插件
                };

                std::string Name;           // 插件名称
                SecureAddonStatus Status = BANNED;  // 插件状态
                bool HasKey = false;        // 是否拥有密钥
            };

            static uint32 constexpr MaxSecureAddons = 25;   // 最大安全插件数量

            std::vector<SecureAddonInfo> SecureAddons;      // 安全插件列表
            uint32 LastBannedAddOnTimestamp = 0;            // 上次检测到禁用插件的时间戳
        } _addons;          // 插件数据实例
        uint32 recruiterId;                                 // 招募者ID - 招募该账号的玩家ID
        bool isRecruiter;                                   // 是否为招募者 - 该账号是否招募过其他玩家
        LockedQueue<WorldPacket*> _recvQueue;               // 接收队列 - 存储待处理的数据包
        rbac::RBACData* _RBACData;                          // RBAC权限数据 - 基于角色的访问控制数据
        uint32 expireTime;                                  // 会话过期时间
        bool forceExit;                                     // 强制退出标志
        ObjectGuid m_currentBankerGUID;                     // 当前正在交互的银行家GUID

        // 时间同步相关数据
        // 时间同步时钟差值队列 - first: 时钟差值, second: 用于计算该差值的数据包交换延迟
        std::unique_ptr<boost::circular_buffer<std::pair<int64, uint32>>> _timeSyncClockDeltaQueue;
        int64 _timeSyncClockDelta;                          // 当前时间同步时钟差值
        void ComputeNewClockDelta();                        // 计算新的时钟差值

        std::map<uint32, uint32> _pendingTimeSyncRequests;  // 待处理的时间同步请求映射表 (key: 计数器, value: 发送时的服务器时间)
        uint32 _timeSyncNextCounter;                        // 下一个时间同步计数器值
        uint32 _timeSyncTimer;                              // 时间同步定时器

        // Packets cooldown
        time_t _calendarEventCreationCooldown;
        GameClient* _gameClient;

        WorldSession(WorldSession const& right) = delete;
        WorldSession& operator=(WorldSession const& right) = delete;
};
#endif
/// @}
