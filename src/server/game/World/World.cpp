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
 * @file World.cpp
 * @ingroup world
 *
 * @brief 世界管理器实现文件
 *
 * 本文件实现了 World 类，这是 TrinityCore 服务器的核心组件，负责：
 * - 管理所有玩家会话（WorldSession）
 * - 处理世界更新循环（Update 循环）
 * - 管理服务器配置和倍率设置
 * - 处理服务器关闭和重启逻辑
 * - 管理定时任务（拍卖行更新、尸体清理、游戏事件等）
 * - 处理全局消息广播
 * - 初始化游戏世界（加载 DBC、数据库数据等）
 *
 * @note 本文件包含服务器启动的核心流程，加载超过 100 种不同的数据表
 * @note 主要更新循环每帧调用 Update() 方法，处理所有会话和定时器
 */

#include "World.h"
#include "AccountMgr.h"
#include "AchievementMgr.h"
#include "AddonMgr.h"
#include "ArenaTeamMgr.h"
#include "AuctionHouseBot.h"
#include "AuctionHouseMgr.h"
#include "BattlefieldMgr.h"
#include "BattlegroundMgr.h"
#include "CalendarMgr.h"
#include "ChannelMgr.h"
#include "CharacterCache.h"
#include "CharacterDatabaseCleaner.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "ChatPackets.h"
#include "Config.h"
#include "CreatureAIRegistry.h"
#include "CreatureGroups.h"
#include "CreatureTextMgr.h"
#include "DatabaseEnv.h"
#include "DisableMgr.h"
#include "GameEventMgr.h"
#include "GameObjectModel.h"
#include "GameTime.h"
#include "GitRevision.h"
#include "GridNotifiersImpl.h"
#include "GroupMgr.h"
#include "GuildMgr.h"
#include "InstanceSaveMgr.h"
#include "IPLocation.h"
#include "Language.h"
#include "LFGMgr.h"
#include "Log.h"
#include "LootItemStorage.h"
#include "LootMgr.h"
#include "M2Stores.h"
#include "MapManager.h"
#include "Memory.h"
#include "Metric.h"
#include "MMapFactory.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "OutdoorPvPMgr.h"
#include "PetitionMgr.h"
#include "Player.h"
#include "PlayerDump.h"
#include "PoolMgr.h"
#include "QueryCallback.h"
#include "QuestPools.h"
#include "Realm.h"
#include "ScriptMgr.h"
#include "ScriptReloadMgr.h"
#include "ServerMotd.h"
#include "SkillDiscovery.h"
#include "SkillExtraItems.h"
#include "SmartScriptMgr.h"
#include "SpellMgr.h"
#include "TicketMgr.h"
#include "TransportMgr.h"
#include "Unit.h"
#include "UpdateTime.h"
#include "VMapFactory.h"
#include "VMapManager2.h"
#include "WardenCheckMgr.h"
#include "WaypointManager.h"
#include "WeatherMgr.h"
#include "WhoListStorage.h"
#include "WorldSession.h"

#include <boost/asio/ip/address.hpp>
#include <boost/algorithm/string.hpp>

// ============================================================================
// 静态成员变量初始化
// ============================================================================

/// 停止事件标志 - 当设置为 true 时，主循环将退出
TC_GAME_API std::atomic<bool> World::m_stopEvent(false);

/// 退出代码 - 用于指示服务器关闭的原因
TC_GAME_API uint8 World::m_ExitCode = SHUTDOWN_EXIT_CODE;

/// 世界循环计数器 - 用于性能统计和调试
TC_GAME_API std::atomic<uint32> World::m_worldLoopCounter(0);

// ============================================================================
// 可见性距离静态成员 - 控制玩家能看到其他对象的距离
// ============================================================================

/// 大陆上的最大可见距离（户外世界）
TC_GAME_API float World::m_MaxVisibleDistanceOnContinents = DEFAULT_VISIBILITY_DISTANCE;

/// 副本中的最大可见距离
TC_GAME_API float World::m_MaxVisibleDistanceInInstances  = DEFAULT_VISIBILITY_INSTANCE;

/// 战场中的最大可见距离
TC_GAME_API float World::m_MaxVisibleDistanceInBG         = DEFAULT_VISIBILITY_BGARENAS;

/// 竞技场中的最大可见距离
TC_GAME_API float World::m_MaxVisibleDistanceInArenas     = DEFAULT_VISIBILITY_BGARENAS;

// ============================================================================
// 可见性通知周期静态成员 - 控制多久检查一次可见性更新
// ============================================================================

/// 大陆上的可见性通知周期（毫秒）
TC_GAME_API int32 World::m_visibility_notify_periodOnContinents = DEFAULT_VISIBILITY_NOTIFY_PERIOD;

/// 副本中的可见性通知周期（毫秒）
TC_GAME_API int32 World::m_visibility_notify_periodInInstances  = DEFAULT_VISIBILITY_NOTIFY_PERIOD;

/// 战场中的可见性通知周期（毫秒）
TC_GAME_API int32 World::m_visibility_notify_periodInBG         = DEFAULT_VISIBILITY_NOTIFY_PERIOD;

/// 竞技场中的可见性通知周期（毫秒）
TC_GAME_API int32 World::m_visibility_notify_periodInArenas     = DEFAULT_VISIBILITY_NOTIFY_PERIOD;

/**
 * @brief World 构造函数
 *
 * 初始化世界管理器的所有成员变量为默认值。
 * 这是服务器启动的第一步，在 main() 函数中调用。
 *
 * 初始化内容包括：
 * - 玩家限制和安全等级
 * - 会话计数器
 * - 定时器
 * - 配置数组
 * - GUID 警告系统
 *
 * @note 此构造函数不加载任何数据，仅初始化变量
 * @see SetInitialWorldSettings() 加载实际游戏数据
 */
World::World()
{
    // 玩家限制和安全等级
    m_playerLimit = 0;                          // 玩家数量限制（0 = 无限制）
    m_allowedSecurityLevel = SEC_PLAYER;        // 允许登录的最低安全等级
    m_allowMovement = true;                     // 是否允许生物移动（调试用）

    // 关闭和重启相关
    m_ShutdownMask = 0;                         // 关闭掩码（重启、空闲关闭等）
    m_ShutdownTimer = 0;                        // 关闭倒计时（秒）

    // 会话统计
    m_maxActiveSessionCount = 0;                // 最大活跃会话数（统计用）
    m_maxQueuedSessionCount = 0;                // 最大排队会话数（统计用）
    m_PlayerCount = 0;                          // 当前在线玩家数
    m_MaxPlayerCount = 0;                       // 最大在线玩家数（统计用）

    // 任务重置时间
    m_NextDailyQuestReset = 0;                  // 下次日常任务重置时间
    m_NextWeeklyQuestReset = 0;                 // 下次周常任务重置时间
    m_NextMonthlyQuestReset = 0;                // 下次月常任务重置时间
    m_NextRandomBGReset = 0;                    // 下次随机战场重置时间
    m_NextCalendarOldEventsDeletionTime = 0;    // 下次日历旧事件删除时间
    m_NextGuildReset = 0;                       // 下次公会重置时间

    // 语言设置
    m_defaultDbcLocale = LOCALE_enUS;           // 默认 DBC 语言（美式英语）
    m_availableDbcLocaleMask = 0;               // 可用的 DBC 语言掩码

    // 邮件系统定时器
    mail_timer = 0;                             // 邮件定时器
    mail_timer_expires = 0;                     // 邮件定时器过期时间

    // 服务器状态
    m_isClosed = false;                         // 服务器是否关闭（拒绝新连接）

    // 清理标志
    m_CleaningFlags = 0;                        // 数据库清理标志

    // 初始化所有配置数组为 0
    memset(rate_values, 0, sizeof(rate_values));          // 倍率值数组
    memset(m_int_configs, 0, sizeof(m_int_configs));      // 整数配置数组
    memset(m_bool_configs, 0, sizeof(m_bool_configs));    // 布尔配置数组
    memset(m_float_configs, 0, sizeof(m_float_configs));  // 浮点数配置数组

    // GUID 警告系统
    _guidWarn = false;                          // GUID 警告标志（接近上限）
    _guidAlert = false;                         // GUID 警报标志（严重接近上限）
    _warnDiff = 0;                              // 警告间隔计数器
    _warnShutdownTime = GameTime::GetGameTime();// 警告关闭时间
}

/**
 * @brief World 析构函数
 *
 * 清理世界管理器资源，包括：
 * - 删除所有剩余的会话对象
 * - 清理 CLI 命令队列
 * - 释放 VMap 和 MMap 资源
 *
 * @note 此函数在服务器关闭时调用
 * @warning 必须确保所有地图和会话已经正确清理
 */
World::~World()
{
    /// 清空踢出的会话集合
    /// 删除所有剩余的会话对象
    while (!m_sessions.empty())
    {
        // 不从队列中移除，防止加载新会话
        delete m_sessions.begin()->second;
        m_sessions.erase(m_sessions.begin());
    }

    // 清理 CLI 命令队列
    CliCommandHolder* command = nullptr;
    while (cliCmdQueue.next(command))
        delete command;

    // 清理 VMap（视线碰撞检测）资源
    VMAP::VMapFactory::clear();
    // 清理 MMap（寻路网格）资源
    MMAP::MMapFactory::clear();

    /// @todo 释放 addSessQueue（异步会话添加队列）
}

/**
 * @brief 获取世界实例（单例模式）
 *
 * 返回 World 类的唯一实例。使用静态局部变量实现线程安全的单例模式。
 *
 * @return World* 世界管理器的唯一实例指针
 *
 * @note 此方法是获取世界管理器的唯一方式
 * @note 通过 sWorld 宏可以更方便地访问
 */
World* World::instance()
{
    static World instance;
    return &instance;
}

/**
 * @brief 在指定区域查找玩家
 *
 * 遍历所有活跃会话，查找第一个位于指定区域的玩家。
 *
 * @param zone 区域 ID（AreaTable.dbc 中的 ID）
 * @return Player* 找到的玩家指针，如果未找到则返回 nullptr
 *
 * @note 此方法用于检查特定区域是否有玩家在线
 * @note 只返回第一个找到的玩家
 *
 * @性能 遍历所有会话，时间复杂度 O(n)
 */
Player* World::FindPlayerInZone(uint32 zone)
{
    ///- 遍历所有活跃会话，返回第一个在指定区域的玩家
    SessionMap::const_iterator itr;
    for (itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
    {
        if (!itr->second)
            continue;

        Player* player = itr->second->GetPlayer();
        if (!player)
            continue;

        // 检查玩家是否在世界中且在指定区域
        if (player->IsInWorld() && player->GetZoneId() == zone)
            return player;
    }
    return nullptr;
}

/**
 * @brief 检查服务器是否已关闭
 *
 * 返回服务器的关闭状态。当服务器关闭时，拒绝新的客户端连接。
 *
 * @return true 服务器已关闭，拒绝新连接
 * @return false 服务器开放，接受新连接
 *
 * @note 可通过 SetClosed() 方法设置关闭状态
 */
bool World::IsClosed() const
{
    return m_isClosed;
}

/**
 * @brief 设置服务器关闭状态
 *
 * 设置服务器的关闭状态，并触发脚本事件。
 *
 * @param val true 表示关闭服务器（拒绝新连接），false 表示开放服务器
 *
 * @note 会触发脚本回调 OnOpenStateChange
 * @note 值会被反转传递给脚本（方便脚本编写者）
 */
void World::SetClosed(bool val)
{
    m_isClosed = val;

    // 反转值，简化脚本编写者的使用
    sScriptMgr->OnOpenStateChange(!val);
}

/**
 * @brief 从数据库加载允许的安全等级
 *
 * 从登录数据库的 realmlist 表加载此领域允许的最低安全等级。
 * 用于限制只有特定 GM 等级以上的账号才能登录。
 *
 * @note 如果数据库中没有记录，则使用默认值 SEC_PLAYER
 * @note 在服务器启动时调用
 */
void World::LoadDBAllowedSecurityLevel()
{
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_REALMLIST_SECURITY_LEVEL);
    stmt->setInt32(0, int32(realm.Id.Realm));
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if (result)
        SetPlayerSecurityLimit(AccountTypes(result->Fetch()->GetUInt8()));
}

/**
 * @brief 设置玩家安全等级限制
 *
 * 设置允许登录的最低安全等级。如果新等级更严格，会踢出当前在线的低等级账号。
 *
 * @param _sec 安全等级（AccountTypes 枚举）
 *
 * @note 等级必须是 SEC_PLAYER 到 SEC_CONSOLE 之间
 * @note 如果新等级比当前更严格，会踢出所有低于新等级的玩家
 */
void World::SetPlayerSecurityLimit(AccountTypes _sec)
{
    // 确保等级在有效范围内（最大为 SEC_PLAYER）
    AccountTypes sec = _sec < SEC_CONSOLE ? _sec : SEC_PLAYER;
    bool update = sec > m_allowedSecurityLevel;
    m_allowedSecurityLevel = sec;
    // 如果需要更新且新等级更严格，踢出所有低于新等级的玩家
    if (update)
        KickAllLess(m_allowedSecurityLevel);
}

// ============================================================================
// GUID 警告和警报系统
// ============================================================================

/**
 * @brief 触发 GUID 警告
 *
 * 当 GUID 使用量接近上限时触发警告，安排服务器在安静时间重启。
 * 这是预防性措施，防止 GUID 耗尽导致服务器崩溃。
 *
 * @note GUID 是游戏中所有对象的唯一标识符
 * @note 警告等级通常设置为 1200 万（上限 1677 万）
 * @note 会在安静时间（凌晨）安排重启
 *
 * @性能 使用互斥锁防止多个地图同时触发
 */
void World::TriggerGuidWarning()
{
    // 锁定互斥锁，防止多个地图同时触发
    std::lock_guard<std::mutex> lock(_guidAlertLock);

    time_t gameTime = GameTime::GetGameTime();
    time_t today = (gameTime / DAY) * DAY;

    // 检查今天的重启窗口是否已过（安静时间前 5 分钟）
    while (gameTime >= GetLocalHourTimestamp(today, getIntConfig(CONFIG_RESPAWN_RESTARTQUIETTIME)) - 1810)
        today += DAY;

    // 安排在安静时间前 30 分钟重启
    _warnShutdownTime = GetLocalHourTimestamp(today, getIntConfig(CONFIG_RESPAWN_RESTARTQUIETTIME)) - 1800;

    _guidWarn = true;
    SendGuidWarning();
}

/**
 * @brief 触发 GUID 警报
 *
 * 当 GUID 使用量非常接近上限时触发警报，立即安排紧急重启。
 * 这是紧急措施，必须在短时间内重启服务器。
 *
 * @note 警报等级通常设置为 1600 万（上限 1677 万）
 * @note 会在 5 分钟内强制重启服务器
 *
 * @性能 使用互斥锁防止多个地图同时触发
 */
void World::TriggerGuidAlert()
{
    // 锁定互斥锁，防止多个地图同时触发
    std::lock_guard<std::mutex> lock(_guidAlertLock);

    // 执行紧急重启（5 分钟倒计时）
    DoGuidAlertRestart();
    _guidAlert = true;
    _guidWarn = false;
}

/**
 * @brief 执行 GUID 警告重启
 *
 * 如果当前没有正在进行的关闭流程，启动 30 分钟倒计时的重启。
 * 这是正常的预防性重启。
 *
 * @note 只在警告等级触发，给玩家足够时间准备
 */
void World::DoGuidWarningRestart()
{
    // 如果已经有关闭流程在进行，直接返回
    if (m_ShutdownTimer)
        return;

    // 启动 30 分钟倒计时重启
    ShutdownServ(1800, SHUTDOWN_MASK_RESTART, RESTART_EXIT_CODE);
    _warnShutdownTime += HOUR;  // 推迟警告时间，避免重复触发
}

/**
 * @brief 执行 GUID 警报重启
 *
 * 如果当前没有正在进行的关闭流程，启动 5 分钟倒计时的紧急重启。
 * 这是紧急重启，必须尽快执行。
 *
 * @note 只在警报等级触发，时间紧迫
 */
void World::DoGuidAlertRestart()
{
    // 如果已经有关闭流程在进行，直接返回
    if (m_ShutdownTimer)
        return;

    // 启动 5 分钟紧急重启
    ShutdownServ(300, SHUTDOWN_MASK_RESTART, RESTART_EXIT_CODE, _alertRestartReason);
}

/**
 * @brief 发送 GUID 警告消息
 *
 * 向所有在线玩家发送 GUID 警告消息，通知即将进行的维护重启。
 *
 * @note 只有在警告标志设置且没有关闭流程时才发送
 * @note 消息频率由 CONFIG_RESPAWN_GUIDWARNING_FREQUENCY 控制
 */
void World::SendGuidWarning()
{
    if (!m_ShutdownTimer && _guidWarn && getIntConfig(CONFIG_RESPAWN_GUIDWARNING_FREQUENCY) > 0)
        SendServerMessage(SERVER_MSG_STRING, _guidWarningMsg.c_str());
    _warnDiff = 0;
}

// ============================================================================
// 会话管理
// ============================================================================

/**
 * @brief 根据账号 ID 查找会话
 *
 * 在会话映射中查找指定账号 ID 的会话对象。
 *
 * @param id 账号 ID
 * @return WorldSession* 会话指针，如果未找到则返回 nullptr
 *
 * @note 可能返回 nullptr（被踢出的会话）
 *
 * @性能 使用 unordered_map，平均时间复杂度 O(1)
 */
WorldSession* World::FindSession(uint32 id) const
{
    SessionMap::const_iterator itr = m_sessions.find(id);

    if (itr != m_sessions.end())
        return itr->second;                                 // 也可能返回 nullptr（被踢出的会话）
    else
        return nullptr;
}

/**
 * @brief 移除指定会话
 *
 * 根据账号 ID 移除会话。会先踢出玩家，但不会立即删除会话对象
 * （在下次世界更新时删除）。
 *
 * @param id 账号 ID
 * @return true 成功移除或会话不存在
 * @return false 玩家正在加载中，无法移除
 *
 * @note 如果玩家正在加载中，会返回 false 以防止数据损坏
 * @note 会话对象不会立即删除，而是标记为待删除
 */
bool World::RemoveSession(uint32 id)
{
    ///- 查找会话，踢出用户，但不能立即删除会话以防止迭代器失效
    SessionMap::const_iterator itr = m_sessions.find(id);

    if (itr != m_sessions.end() && itr->second)
    {
        // 如果玩家正在加载中，返回 false
        if (itr->second->PlayerLoading())
            return false;

        // 踢出玩家
        itr->second->KickPlayer("World::RemoveSession");
    }

    return true;
}

/**
 * @brief 添加会话到异步队列
 *
 * 将新会话添加到异步队列中，在下次世界更新时处理。
 * 这样可以避免在 socket 线程中直接操作会话映射。
 *
 * @param s 要添加的会话指针
 *
 * @note 线程安全：可以从任何线程调用
 * @see AddSession_() 实际处理会话的方法
 */
void World::AddSession(WorldSession* s)
{
    addSessQueue.add(s);
}

/**
 * @brief 实际添加会话到世界（内部实现）
 *
 * 处理会话添加的核心逻辑：
 * 1. 踢出已存在的同名账号会话
 * 2. 检查服务器是否已满
 * 3. 如果已满，将玩家加入队列
 * 4. 否则初始化会话
 *
 * @param s 要添加的会话指针
 *
 * @warning 此方法必须在世界更新线程中调用
 * @note 会检查玩家数量限制和排队机制
 *
 * @性能 可能触发数据库查询（更新在线计数）
 */
void World::AddSession_(WorldSession* s)
{
    ASSERT(s);

    // 注意 - WorldSession* 在 Socket 中仍存在竞争条件

    ///- 踢出已存在的同名账号会话（如果有的话）
    ///- 如果玩家正在加载中又尝试加载，直接返回
    if (!RemoveSession(s->GetAccountId()))
    {
        s->KickPlayer("World::AddSession_ Couldn't remove the other session while on loading screen");
        delete s;                                           // 会话还未加入会话列表，所以不在队列中
        return;
    }

    // 只在非重连情况下减少会话计数
    bool decrease_session = true;

    // 如果会话已存在，准备在下次世界更新时删除
    // 注意 - KickPlayer() 应该在 RemoveSession() 中对"旧"会话调用
    {
        SessionMap::const_iterator old = m_sessions.find(s->GetAccountId());

        if (old != m_sessions.end())
        {
            // 如果会话在队列中，不减少会话计数
            if (RemoveQueuedPlayer(old->second))
                decrease_session = false;
            // 不从队列中移除被替换的会话（如果已列出）
            delete old->second;
        }
    }

    // 将会话添加到映射
    m_sessions[s->GetAccountId()] = s;

    uint32 Sessions = GetActiveAndQueuedSessionCount();
    uint32 pLimit = GetPlayerAmountLimit();
    uint32 QueueSize = GetQueuedSessionCount(); // 队列中的玩家数量

    // 不计算正在尝试登录的用户为会话
    if (decrease_session)
        --Sessions;

    // 检查是否需要排队
    if (pLimit > 0 && Sessions >= pLimit && !s->HasPermission(rbac::RBAC_PERM_SKIP_QUEUE) && !HasRecentlyDisconnected(s))
    {
        AddQueuedPlayer(s);
        UpdateMaxSessionCounters();
        TC_LOG_INFO("misc", "PlayerQueue: Account id {} is in Queue Position ({}).", s->GetAccountId(), ++QueueSize);
        return;
    }

    // 初始化会话（发送角色列表等）
    s->InitializeSession();

    UpdateMaxSessionCounters();

    // 更新人口统计
    if (pLimit > 0)
    {
        float popu = (float)GetActiveSessionCount();              // 更新后的用户数量
        popu /= pLimit;
        popu *= 2;
        TC_LOG_INFO("misc", "Server Population ({}).", popu);
    }
}

/**
 * @brief 检查会话是否最近断开连接
 *
 * 检查指定账号是否在断开连接容忍时间内。
 * 如果是，则允许直接重新连接而无需排队。
 *
 * @param session 要检查的会话
 * @return true 最近断开连接（可跳过排队）
 * @return false 未在容忍时间内断开连接
 *
 * @note 容忍时间由 CONFIG_INTERVAL_DISCONNECT_TOLERANCE 配置
 * @note 用于处理短暂断线重连的情况
 */
bool World::HasRecentlyDisconnected(WorldSession* session)
{
    if (!session)
        return false;

    if (uint32 tolerance = getIntConfig(CONFIG_INTERVAL_DISCONNECT_TOLERANCE))
    {
        for (DisconnectMap::iterator i = m_disconnects.begin(); i != m_disconnects.end();)
        {
            if (difftime(i->second, GameTime::GetGameTime()) < tolerance)
            {
                if (i->first == session->GetAccountId())
                    return true;
                ++i;
            }
            else
                m_disconnects.erase(i++);  // 移除过期的断开连接记录
        }
    }
    return false;
 }

/**
 * @brief 获取玩家在队列中的位置
 *
 * 遍历排队队列，查找指定会话的位置。
 *
 * @param sess 要查找的会话
 * @return int32 队列位置（从 1 开始），如果不在队列中则返回 0
 *
 * @性能 遍历队列，时间复杂度 O(n)
 */
int32 World::GetQueuePos(WorldSession* sess)
{
    uint32 position = 1;

    for (Queue::const_iterator iter = m_QueuedPlayer.begin(); iter != m_QueuedPlayer.end(); ++iter, ++position)
        if ((*iter) == sess)
            return position;

    return 0;
}

/**
 * @brief 添加玩家到排队队列
 *
 * 当服务器已满时，将玩家添加到等待队列中。
 * 会发送排队通知给客户端。
 *
 * @param sess 要添加的会话
 *
 * @note 队列中的玩家会定期收到位置更新
 */
void World::AddQueuedPlayer(WorldSession* sess)
{
    sess->SetInQueue(true);
    m_QueuedPlayer.push_back(sess);

    // 第一个 SMSG_AUTH_RESPONSE 需要包含其他信息
    sess->SendAuthResponse(AUTH_WAIT_QUEUE, false, GetQueuePos(sess));
}

/**
 * @brief 从排队队列移除玩家
 *
 * 从队列中移除指定会话，并处理队列中的下一个玩家。
 *
 * @param sess 要移除的会话
 * @return true 成功从队列中移除
 * @return false 会话不在队列中
 *
 * @note 如果有空位，会让队列中的下一个玩家进入游戏
 * @note 会更新队列中所有后续玩家的位置
 */
bool World::RemoveQueuedPlayer(WorldSession* sess)
{
    // 包括要移除的排队会话的会话计数
    uint32 sessions = GetActiveSessionCount();

    uint32 position = 1;
    Queue::iterator iter = m_QueuedPlayer.begin();

    // 搜索并移除，同时计数跳过的位置
    bool found = false;

    for (; iter != m_QueuedPlayer.end(); ++iter, ++position)
    {
        if (*iter == sess)
        {
            sess->SetInQueue(false);
            sess->ResetTimeOutTime(false);
            iter = m_QueuedPlayer.erase(iter);
            found = true;                                   // 移除排队的会话
            break;
        }
    }

    // iter 指向移除后的下一个 socket 或 end()
    // position 存储移除的 socket 的位置，然后是下一个 socket 的新位置

    // 如果会话不在队列中，需要减少会话计数
    if (!found && sessions)
        --sessions;

    // 接受队列中的第一个玩家
    if ((!m_playerLimit || sessions < m_playerLimit) && !m_QueuedPlayer.empty())
    {
        WorldSession* pop_sess = m_QueuedPlayer.front();
        pop_sess->InitializeSession();
        m_QueuedPlayer.pop_front();

        // 更新 iter 指向第一个排队 socket，如果队列空了则指向 end()
        iter = m_QueuedPlayer.begin();
        position = 1;
    }

    // 从 iter 到 end() 更新位置
    // iter 指向第一个未更新的 socket，position 存储新位置
    for (; iter != m_QueuedPlayer.end(); ++iter, ++position)
        (*iter)->SendAuthWaitQueue(position);

    return found;
}

/**
 * @brief 初始化配置设置
 *
 * 从 worldserver.conf 配置文件加载所有服务器配置项。
 * 包括玩家限制、倍率、游戏规则、PvP 设置等数百个配置项。
 *
 * @param reload 是否为重新加载配置（true）或首次加载（false）
 *
 * @调用时机：
 * - 服务器启动时（reload = false）
 * - 执行 .reload config 命令时（reload = true）
 *
 * @主要配置分类：
 * 1. 玩家限制和欢迎消息
 * 2. 倍率设置（经验、金币、声望等）
 * 3. 角色创建规则
 * 4. GM 权限设置
 * 5. 战场和竞技场设置
 * 6. 服务器性能参数
 * 7. 反作弊和 Warden 设置
 *
 * @note 某些配置项不能在运行时修改（如端口、数据路径等）
 * @note 重新加载会触发脚本回调 OnConfigLoad
 *
 * @性能 加载数百个配置项，首次加载较慢
 */
void World::LoadConfigSettings(bool reload)
{
    // 如果是重新加载，先重新加载配置文件
    if (reload)
    {
        std::vector<std::string> configErrors;
        if (!sConfigMgr->Reload(configErrors))
        {
            for (std::string const& configError : configErrors)
                TC_LOG_ERROR("misc", "World settings reload fail: {}.", configError);

            return;
        }
        // 重新加载日志和监控配置
        sLog->LoadFromConfig();
        sMetric->LoadFromConfigs();
    }

    ///- 从配置文件读取玩家限制和今日消息（MOTD）
    SetPlayerAmountLimit(sConfigMgr->GetIntDefault("PlayerLimit", 100));
    Motd::SetMotd(sConfigMgr->GetStringDefault("Motd", "Welcome to a Trinity Core Server."));

    ///- 从配置文件读取工单系统设置
    m_bool_configs[CONFIG_ALLOW_TICKETS] = sConfigMgr->GetBoolDefault("AllowTickets", true);
    m_bool_configs[CONFIG_DELETE_CHARACTER_TICKET_TRACE] = sConfigMgr->GetBoolDefault("DeletedCharacterTicketTrace", false);

    ///- 获取新登录角色的欢迎字符串（首次创建角色时显示）
    SetNewCharString(sConfigMgr->GetStringDefault("PlayerStart.String", ""));

    ///- Send server info on login?
    m_int_configs[CONFIG_ENABLE_SINFO_LOGIN] = sConfigMgr->GetIntDefault("Server.LoginInfo", 0);

    ///- Read all rates from the config file
    rate_values[RATE_HEALTH]      = sConfigMgr->GetFloatDefault("Rate.Health", 1.0f);
    if (rate_values[RATE_HEALTH] < 0)
    {
        TC_LOG_ERROR("server.loading", "Rate.Health ({}) must be > 0. Using 1 instead.", rate_values[RATE_HEALTH]);
        rate_values[RATE_HEALTH] = 1;
    }
    rate_values[RATE_POWER_MANA]  = sConfigMgr->GetFloatDefault("Rate.Mana", 1.0f);
    if (rate_values[RATE_POWER_MANA] < 0)
    {
        TC_LOG_ERROR("server.loading", "Rate.Mana ({}) must be > 0. Using 1 instead.", rate_values[RATE_POWER_MANA]);
        rate_values[RATE_POWER_MANA] = 1;
    }
    rate_values[RATE_POWER_RAGE_INCOME] = sConfigMgr->GetFloatDefault("Rate.Rage.Income", 1.0f);
    rate_values[RATE_POWER_RAGE_LOSS]   = sConfigMgr->GetFloatDefault("Rate.Rage.Loss", 1.0f);
    if (rate_values[RATE_POWER_RAGE_LOSS] < 0)
    {
        TC_LOG_ERROR("server.loading", "Rate.Rage.Loss ({}) must be > 0. Using 1 instead.", rate_values[RATE_POWER_RAGE_LOSS]);
        rate_values[RATE_POWER_RAGE_LOSS] = 1;
    }
    rate_values[RATE_POWER_RUNICPOWER_INCOME] = sConfigMgr->GetFloatDefault("Rate.RunicPower.Income", 1.0f);
    rate_values[RATE_POWER_RUNICPOWER_LOSS]   = sConfigMgr->GetFloatDefault("Rate.RunicPower.Loss", 1.0f);
    if (rate_values[RATE_POWER_RUNICPOWER_LOSS] < 0)
    {
        TC_LOG_ERROR("server.loading", "Rate.RunicPower.Loss ({}) must be > 0. Using 1 instead.", rate_values[RATE_POWER_RUNICPOWER_LOSS]);
        rate_values[RATE_POWER_RUNICPOWER_LOSS] = 1;
    }
    rate_values[RATE_POWER_FOCUS]  = sConfigMgr->GetFloatDefault("Rate.Focus", 1.0f);
    rate_values[RATE_POWER_ENERGY] = sConfigMgr->GetFloatDefault("Rate.Energy", 1.0f);

    rate_values[RATE_SKILL_DISCOVERY]      = sConfigMgr->GetFloatDefault("Rate.Skill.Discovery", 1.0f);

    rate_values[RATE_DROP_ITEM_POOR]       = sConfigMgr->GetFloatDefault("Rate.Drop.Item.Poor", 1.0f);
    rate_values[RATE_DROP_ITEM_NORMAL]     = sConfigMgr->GetFloatDefault("Rate.Drop.Item.Normal", 1.0f);
    rate_values[RATE_DROP_ITEM_UNCOMMON]   = sConfigMgr->GetFloatDefault("Rate.Drop.Item.Uncommon", 1.0f);
    rate_values[RATE_DROP_ITEM_RARE]       = sConfigMgr->GetFloatDefault("Rate.Drop.Item.Rare", 1.0f);
    rate_values[RATE_DROP_ITEM_EPIC]       = sConfigMgr->GetFloatDefault("Rate.Drop.Item.Epic", 1.0f);
    rate_values[RATE_DROP_ITEM_LEGENDARY]  = sConfigMgr->GetFloatDefault("Rate.Drop.Item.Legendary", 1.0f);
    rate_values[RATE_DROP_ITEM_ARTIFACT]   = sConfigMgr->GetFloatDefault("Rate.Drop.Item.Artifact", 1.0f);
    rate_values[RATE_DROP_ITEM_REFERENCED] = sConfigMgr->GetFloatDefault("Rate.Drop.Item.Referenced", 1.0f);
    rate_values[RATE_DROP_ITEM_REFERENCED_AMOUNT] = sConfigMgr->GetFloatDefault("Rate.Drop.Item.ReferencedAmount", 1.0f);
    rate_values[RATE_DROP_MONEY]  = sConfigMgr->GetFloatDefault("Rate.Drop.Money", 1.0f);
    rate_values[RATE_XP_KILL]     = sConfigMgr->GetFloatDefault("Rate.XP.Kill", 1.0f);
    rate_values[RATE_XP_BG_KILL]  = sConfigMgr->GetFloatDefault("Rate.XP.BattlegroundKill", 1.0f);
    rate_values[RATE_XP_QUEST]    = sConfigMgr->GetFloatDefault("Rate.XP.Quest", 1.0f);
    rate_values[RATE_XP_EXPLORE]  = sConfigMgr->GetFloatDefault("Rate.XP.Explore", 1.0f);

    m_int_configs[CONFIG_XP_BOOST_DAYMASK] = sConfigMgr->GetIntDefault("XP.Boost.Daymask", 0);
    rate_values[RATE_XP_BOOST] = sConfigMgr->GetFloatDefault("XP.Boost.Rate", 2.0f);

    rate_values[RATE_REPAIRCOST]  = sConfigMgr->GetFloatDefault("Rate.RepairCost", 1.0f);
    if (rate_values[RATE_REPAIRCOST] < 0.0f)
    {
        TC_LOG_ERROR("server.loading", "Rate.RepairCost ({}) must be >=0. Using 0.0 instead.", rate_values[RATE_REPAIRCOST]);
        rate_values[RATE_REPAIRCOST] = 0.0f;
    }
    rate_values[RATE_REPUTATION_GAIN]  = sConfigMgr->GetFloatDefault("Rate.Reputation.Gain", 1.0f);
    rate_values[RATE_REPUTATION_LOWLEVEL_KILL]  = sConfigMgr->GetFloatDefault("Rate.Reputation.LowLevel.Kill", 1.0f);
    rate_values[RATE_REPUTATION_LOWLEVEL_QUEST]  = sConfigMgr->GetFloatDefault("Rate.Reputation.LowLevel.Quest", 1.0f);
    rate_values[RATE_REPUTATION_RECRUIT_A_FRIEND_BONUS] = sConfigMgr->GetFloatDefault("Rate.Reputation.RecruitAFriendBonus", 0.1f);
    rate_values[RATE_CREATURE_NORMAL_DAMAGE]          = sConfigMgr->GetFloatDefault("Rate.Creature.Normal.Damage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_ELITE_DAMAGE]     = sConfigMgr->GetFloatDefault("Rate.Creature.Elite.Elite.Damage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_RAREELITE_DAMAGE] = sConfigMgr->GetFloatDefault("Rate.Creature.Elite.RAREELITE.Damage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_WORLDBOSS_DAMAGE] = sConfigMgr->GetFloatDefault("Rate.Creature.Elite.WORLDBOSS.Damage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_RARE_DAMAGE]      = sConfigMgr->GetFloatDefault("Rate.Creature.Elite.RARE.Damage", 1.0f);
    rate_values[RATE_CREATURE_NORMAL_HP]          = sConfigMgr->GetFloatDefault("Rate.Creature.Normal.HP", 1.0f);
    rate_values[RATE_CREATURE_ELITE_ELITE_HP]     = sConfigMgr->GetFloatDefault("Rate.Creature.Elite.Elite.HP", 1.0f);
    rate_values[RATE_CREATURE_ELITE_RAREELITE_HP] = sConfigMgr->GetFloatDefault("Rate.Creature.Elite.RAREELITE.HP", 1.0f);
    rate_values[RATE_CREATURE_ELITE_WORLDBOSS_HP] = sConfigMgr->GetFloatDefault("Rate.Creature.Elite.WORLDBOSS.HP", 1.0f);
    rate_values[RATE_CREATURE_ELITE_RARE_HP]      = sConfigMgr->GetFloatDefault("Rate.Creature.Elite.RARE.HP", 1.0f);
    rate_values[RATE_CREATURE_NORMAL_SPELLDAMAGE]          = sConfigMgr->GetFloatDefault("Rate.Creature.Normal.SpellDamage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_ELITE_SPELLDAMAGE]     = sConfigMgr->GetFloatDefault("Rate.Creature.Elite.Elite.SpellDamage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_RAREELITE_SPELLDAMAGE] = sConfigMgr->GetFloatDefault("Rate.Creature.Elite.RAREELITE.SpellDamage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_WORLDBOSS_SPELLDAMAGE] = sConfigMgr->GetFloatDefault("Rate.Creature.Elite.WORLDBOSS.SpellDamage", 1.0f);
    rate_values[RATE_CREATURE_ELITE_RARE_SPELLDAMAGE]      = sConfigMgr->GetFloatDefault("Rate.Creature.Elite.RARE.SpellDamage", 1.0f);
    rate_values[RATE_CREATURE_AGGRO]  = sConfigMgr->GetFloatDefault("Rate.Creature.Aggro", 1.0f);
    rate_values[RATE_REST_INGAME]                    = sConfigMgr->GetFloatDefault("Rate.Rest.InGame", 1.0f);
    rate_values[RATE_REST_OFFLINE_IN_TAVERN_OR_CITY] = sConfigMgr->GetFloatDefault("Rate.Rest.Offline.InTavernOrCity", 1.0f);
    rate_values[RATE_REST_OFFLINE_IN_WILDERNESS]     = sConfigMgr->GetFloatDefault("Rate.Rest.Offline.InWilderness", 1.0f);
    rate_values[RATE_DAMAGE_FALL]  = sConfigMgr->GetFloatDefault("Rate.Damage.Fall", 1.0f);
    rate_values[RATE_AUCTION_TIME]  = sConfigMgr->GetFloatDefault("Rate.Auction.Time", 1.0f);
    rate_values[RATE_AUCTION_DEPOSIT] = sConfigMgr->GetFloatDefault("Rate.Auction.Deposit", 1.0f);
    rate_values[RATE_AUCTION_CUT] = sConfigMgr->GetFloatDefault("Rate.Auction.Cut", 1.0f);
    rate_values[RATE_HONOR] = sConfigMgr->GetFloatDefault("Rate.Honor", 1.0f);
    rate_values[RATE_ARENA_POINTS] = sConfigMgr->GetFloatDefault("Rate.ArenaPoints", 1.0f);
    rate_values[RATE_INSTANCE_RESET_TIME] = sConfigMgr->GetFloatDefault("Rate.InstanceResetTime", 1.0f);
    rate_values[RATE_TALENT] = sConfigMgr->GetFloatDefault("Rate.Talent", 1.0f);
    if (rate_values[RATE_TALENT] < 0.0f)
    {
        TC_LOG_ERROR("server.loading", "Rate.Talent ({}) must be > 0. Using 1 instead.", rate_values[RATE_TALENT]);
        rate_values[RATE_TALENT] = 1.0f;
    }
    rate_values[RATE_MOVESPEED] = sConfigMgr->GetFloatDefault("Rate.MoveSpeed", 1.0f);
    if (rate_values[RATE_MOVESPEED] < 0)
    {
        TC_LOG_ERROR("server.loading", "Rate.MoveSpeed ({}) must be > 0. Using 1 instead.", rate_values[RATE_MOVESPEED]);
        rate_values[RATE_MOVESPEED] = 1.0f;
    }
    for (uint8 i = 0; i < MAX_MOVE_TYPE; ++i) playerBaseMoveSpeed[i] = baseMoveSpeed[i] * rate_values[RATE_MOVESPEED];
    rate_values[RATE_CORPSE_DECAY_LOOTED] = sConfigMgr->GetFloatDefault("Rate.Corpse.Decay.Looted", 0.5f);

    rate_values[RATE_DURABILITY_LOSS_ON_DEATH]  = sConfigMgr->GetFloatDefault("DurabilityLoss.OnDeath", 10.0f);
    if (rate_values[RATE_DURABILITY_LOSS_ON_DEATH] < 0.0f)
    {
        TC_LOG_ERROR("server.loading", "DurabilityLoss.OnDeath ({}) must be >=0. Using 0.0 instead.", rate_values[RATE_DURABILITY_LOSS_ON_DEATH]);
        rate_values[RATE_DURABILITY_LOSS_ON_DEATH] = 0.0f;
    }
    if (rate_values[RATE_DURABILITY_LOSS_ON_DEATH] > 100.0f)
    {
        TC_LOG_ERROR("server.loading", "DurabilityLoss.OnDeath ({}) must be <= 100. Using 100.0 instead.", rate_values[RATE_DURABILITY_LOSS_ON_DEATH]);
        rate_values[RATE_DURABILITY_LOSS_ON_DEATH] = 0.0f;
    }
    rate_values[RATE_DURABILITY_LOSS_ON_DEATH] = rate_values[RATE_DURABILITY_LOSS_ON_DEATH] / 100.0f;

    rate_values[RATE_DURABILITY_LOSS_DAMAGE] = sConfigMgr->GetFloatDefault("DurabilityLossChance.Damage", 0.5f);
    if (rate_values[RATE_DURABILITY_LOSS_DAMAGE] < 0.0f)
    {
        TC_LOG_ERROR("server.loading", "DurabilityLossChance.Damage ({}) must be >=0. Using 0.0 instead.", rate_values[RATE_DURABILITY_LOSS_DAMAGE]);
        rate_values[RATE_DURABILITY_LOSS_DAMAGE] = 0.0f;
    }
    rate_values[RATE_DURABILITY_LOSS_ABSORB] = sConfigMgr->GetFloatDefault("DurabilityLossChance.Absorb", 0.5f);
    if (rate_values[RATE_DURABILITY_LOSS_ABSORB] < 0.0f)
    {
        TC_LOG_ERROR("server.loading", "DurabilityLossChance.Absorb ({}) must be >=0. Using 0.0 instead.", rate_values[RATE_DURABILITY_LOSS_ABSORB]);
        rate_values[RATE_DURABILITY_LOSS_ABSORB] = 0.0f;
    }
    rate_values[RATE_DURABILITY_LOSS_PARRY] = sConfigMgr->GetFloatDefault("DurabilityLossChance.Parry", 0.05f);
    if (rate_values[RATE_DURABILITY_LOSS_PARRY] < 0.0f)
    {
        TC_LOG_ERROR("server.loading", "DurabilityLossChance.Parry ({}) must be >=0. Using 0.0 instead.", rate_values[RATE_DURABILITY_LOSS_PARRY]);
        rate_values[RATE_DURABILITY_LOSS_PARRY] = 0.0f;
    }
    rate_values[RATE_DURABILITY_LOSS_BLOCK] = sConfigMgr->GetFloatDefault("DurabilityLossChance.Block", 0.05f);
    if (rate_values[RATE_DURABILITY_LOSS_BLOCK] < 0.0f)
    {
        TC_LOG_ERROR("server.loading", "DurabilityLossChance.Block ({}) must be >=0. Using 0.0 instead.", rate_values[RATE_DURABILITY_LOSS_BLOCK]);
        rate_values[RATE_DURABILITY_LOSS_BLOCK] = 0.0f;
    }
    rate_values[RATE_MONEY_QUEST] = sConfigMgr->GetFloatDefault("Rate.Quest.Money.Reward", 1.0f);
    if (rate_values[RATE_MONEY_QUEST] < 0.0f)
    {
        TC_LOG_ERROR("server.loading", "Rate.Quest.Money.Reward ({}) must be >=0. Using 0 instead.", rate_values[RATE_MONEY_QUEST]);
        rate_values[RATE_MONEY_QUEST] = 0.0f;
    }
    rate_values[RATE_MONEY_MAX_LEVEL_QUEST] = sConfigMgr->GetFloatDefault("Rate.Quest.Money.Max.Level.Reward", 1.0f);
    if (rate_values[RATE_MONEY_MAX_LEVEL_QUEST] < 0.0f)
    {
        TC_LOG_ERROR("server.loading", "Rate.Quest.Money.Max.Level.Reward ({}) must be >=0. Using 0 instead.", rate_values[RATE_MONEY_MAX_LEVEL_QUEST]);
        rate_values[RATE_MONEY_MAX_LEVEL_QUEST] = 0.0f;
    }
    ///- Read other configuration items from the config file

    m_bool_configs[CONFIG_DURABILITY_LOSS_IN_PVP] = sConfigMgr->GetBoolDefault("DurabilityLoss.InPvP", false);

    m_int_configs[CONFIG_COMPRESSION] = sConfigMgr->GetIntDefault("Compression", 1);
    if (m_int_configs[CONFIG_COMPRESSION] < 1 || m_int_configs[CONFIG_COMPRESSION] > 9)
    {
        TC_LOG_ERROR("server.loading", "Compression level ({}) must be in range 1..9. Using default compression level (1).", m_int_configs[CONFIG_COMPRESSION]);
        m_int_configs[CONFIG_COMPRESSION] = 1;
    }
    m_bool_configs[CONFIG_ADDON_CHANNEL] = sConfigMgr->GetBoolDefault("AddonChannel", true);
    m_bool_configs[CONFIG_CLEAN_CHARACTER_DB] = sConfigMgr->GetBoolDefault("CleanCharacterDB", false);
    m_int_configs[CONFIG_PERSISTENT_CHARACTER_CLEAN_FLAGS] = sConfigMgr->GetIntDefault("PersistentCharacterCleanFlags", 0);
    m_int_configs[CONFIG_AUCTION_GETALL_DELAY] = sConfigMgr->GetIntDefault("Auction.GetAllScanDelay", 900);
    m_int_configs[CONFIG_AUCTION_SEARCH_DELAY] = sConfigMgr->GetIntDefault("Auction.SearchDelay", 300);
    if (m_int_configs[CONFIG_AUCTION_SEARCH_DELAY] < 100 || m_int_configs[CONFIG_AUCTION_SEARCH_DELAY] > 10000)
    {
        TC_LOG_ERROR("server.loading", "Auction.SearchDelay ({}) must be between 100 and 10000. Using default of 300ms", m_int_configs[CONFIG_AUCTION_SEARCH_DELAY]);
        m_int_configs[CONFIG_AUCTION_SEARCH_DELAY] = 300;
    }
    m_int_configs[CONFIG_CHAT_CHANNEL_LEVEL_REQ] = sConfigMgr->GetIntDefault("ChatLevelReq.Channel", 1);
    m_int_configs[CONFIG_CHAT_WHISPER_LEVEL_REQ] = sConfigMgr->GetIntDefault("ChatLevelReq.Whisper", 1);
    m_int_configs[CONFIG_CHAT_EMOTE_LEVEL_REQ] = sConfigMgr->GetIntDefault("ChatLevelReq.Emote", 1);
    m_int_configs[CONFIG_CHAT_SAY_LEVEL_REQ] = sConfigMgr->GetIntDefault("ChatLevelReq.Say", 1);
    m_int_configs[CONFIG_CHAT_YELL_LEVEL_REQ] = sConfigMgr->GetIntDefault("ChatLevelReq.Yell", 1);
    m_int_configs[CONFIG_PARTY_LEVEL_REQ] = sConfigMgr->GetIntDefault("PartyLevelReq", 1);
    m_int_configs[CONFIG_TRADE_LEVEL_REQ] = sConfigMgr->GetIntDefault("LevelReq.Trade", 1);
    m_int_configs[CONFIG_TICKET_LEVEL_REQ] = sConfigMgr->GetIntDefault("LevelReq.Ticket", 1);
    m_int_configs[CONFIG_AUCTION_LEVEL_REQ] = sConfigMgr->GetIntDefault("LevelReq.Auction", 1);
    m_int_configs[CONFIG_MAIL_LEVEL_REQ] = sConfigMgr->GetIntDefault("LevelReq.Mail", 1);
    m_bool_configs[CONFIG_PRESERVE_CUSTOM_CHANNELS] = sConfigMgr->GetBoolDefault("PreserveCustomChannels", false);
    m_int_configs[CONFIG_PRESERVE_CUSTOM_CHANNEL_DURATION] = sConfigMgr->GetIntDefault("PreserveCustomChannelDuration", 14);
    m_int_configs[CONFIG_PRESERVE_CUSTOM_CHANNEL_INTERVAL] = sConfigMgr->GetIntDefault("PreserveCustomChannelInterval", 5);
    m_bool_configs[CONFIG_GRID_UNLOAD] = sConfigMgr->GetBoolDefault("GridUnload", true);
    m_bool_configs[CONFIG_BASEMAP_LOAD_GRIDS] = sConfigMgr->GetBoolDefault("BaseMapLoadAllGrids", false);
    if (m_bool_configs[CONFIG_BASEMAP_LOAD_GRIDS] && m_bool_configs[CONFIG_GRID_UNLOAD])
    {
        TC_LOG_ERROR("server.loading", "BaseMapLoadAllGrids enabled, but GridUnload also enabled. GridUnload must be disabled to enable base map pre-loading. Base map pre-loading disabled");
        m_bool_configs[CONFIG_BASEMAP_LOAD_GRIDS] = false;
    }
    m_bool_configs[CONFIG_INSTANCEMAP_LOAD_GRIDS] = sConfigMgr->GetBoolDefault("InstanceMapLoadAllGrids", false);
    if (m_bool_configs[CONFIG_INSTANCEMAP_LOAD_GRIDS] && m_bool_configs[CONFIG_GRID_UNLOAD])
    {
        TC_LOG_ERROR("server.loading", "InstanceMapLoadAllGrids enabled, but GridUnload also enabled. GridUnload must be disabled to enable instance map pre-loading. Instance map pre-loading disabled");
        m_bool_configs[CONFIG_INSTANCEMAP_LOAD_GRIDS] = false;
    }
    m_int_configs[CONFIG_INTERVAL_SAVE] = sConfigMgr->GetIntDefault("PlayerSaveInterval", 15 * MINUTE * IN_MILLISECONDS);
    m_int_configs[CONFIG_INTERVAL_DISCONNECT_TOLERANCE] = sConfigMgr->GetIntDefault("DisconnectToleranceInterval", 0);
    m_bool_configs[CONFIG_STATS_SAVE_ONLY_ON_LOGOUT] = sConfigMgr->GetBoolDefault("PlayerSave.Stats.SaveOnlyOnLogout", true);

    m_int_configs[CONFIG_MIN_LEVEL_STAT_SAVE] = sConfigMgr->GetIntDefault("PlayerSave.Stats.MinLevel", 0);
    if (m_int_configs[CONFIG_MIN_LEVEL_STAT_SAVE] > MAX_LEVEL)
    {
        TC_LOG_ERROR("server.loading", "PlayerSave.Stats.MinLevel ({}) must be in range 0..80. Using default, do not save character stats (0).", m_int_configs[CONFIG_MIN_LEVEL_STAT_SAVE]);
        m_int_configs[CONFIG_MIN_LEVEL_STAT_SAVE] = 0;
    }

    m_int_configs[CONFIG_INTERVAL_GRIDCLEAN] = sConfigMgr->GetIntDefault("GridCleanUpDelay", 5 * MINUTE * IN_MILLISECONDS);
    if (m_int_configs[CONFIG_INTERVAL_GRIDCLEAN] < MIN_GRID_DELAY)
    {
        TC_LOG_ERROR("server.loading", "GridCleanUpDelay ({}) must be greater {}. Use this minimal value.", m_int_configs[CONFIG_INTERVAL_GRIDCLEAN], MIN_GRID_DELAY);
        m_int_configs[CONFIG_INTERVAL_GRIDCLEAN] = MIN_GRID_DELAY;
    }
    if (reload)
        sMapMgr->SetGridCleanUpDelay(m_int_configs[CONFIG_INTERVAL_GRIDCLEAN]);

    m_int_configs[CONFIG_INTERVAL_MAPUPDATE] = sConfigMgr->GetIntDefault("MapUpdateInterval", 10);
    if (m_int_configs[CONFIG_INTERVAL_MAPUPDATE] < MIN_MAP_UPDATE_DELAY)
    {
        TC_LOG_ERROR("server.loading", "MapUpdateInterval ({}) must be greater {}. Use this minimal value.", m_int_configs[CONFIG_INTERVAL_MAPUPDATE], MIN_MAP_UPDATE_DELAY);
        m_int_configs[CONFIG_INTERVAL_MAPUPDATE] = MIN_MAP_UPDATE_DELAY;
    }
    if (reload)
        sMapMgr->SetMapUpdateInterval(m_int_configs[CONFIG_INTERVAL_MAPUPDATE]);

    m_int_configs[CONFIG_INTERVAL_CHANGEWEATHER] = sConfigMgr->GetIntDefault("ChangeWeatherInterval", 10 * MINUTE * IN_MILLISECONDS);

    if (reload)
    {
        uint32 val = sConfigMgr->GetIntDefault("WorldServerPort", 8085);
        if (val != m_int_configs[CONFIG_PORT_WORLD])
            TC_LOG_ERROR("server.loading", "WorldServerPort option can't be changed at worldserver.conf reload, using current value ({}).", m_int_configs[CONFIG_PORT_WORLD]);
    }
    else
        m_int_configs[CONFIG_PORT_WORLD] = sConfigMgr->GetIntDefault("WorldServerPort", 8085);

    // Config values are in "milliseconds" but we handle SocketTimeOut only as "seconds" so divide by 1000
    m_int_configs[CONFIG_SOCKET_TIMEOUTTIME] = sConfigMgr->GetIntDefault("SocketTimeOutTime", 900000) / 1000;
    m_int_configs[CONFIG_SOCKET_TIMEOUTTIME_ACTIVE] = sConfigMgr->GetIntDefault("SocketTimeOutTimeActive", 60000) / 1000;

    m_int_configs[CONFIG_SESSION_ADD_DELAY] = sConfigMgr->GetIntDefault("SessionAddDelay", 10000);

    m_float_configs[CONFIG_GROUP_XP_DISTANCE] = sConfigMgr->GetFloatDefault("MaxGroupXPDistance", 74.0f);
    m_float_configs[CONFIG_MAX_RECRUIT_A_FRIEND_DISTANCE] = sConfigMgr->GetFloatDefault("MaxRecruitAFriendBonusDistance", 100.0f);

    m_int_configs[CONFIG_MIN_QUEST_SCALED_XP_RATIO] = sConfigMgr->GetIntDefault("MinQuestScaledXPRatio", 0);
    if (m_int_configs[CONFIG_MIN_QUEST_SCALED_XP_RATIO] > 100)
    {
        TC_LOG_ERROR("server.loading", "MinQuestScaledXPRatio ({}) must be in range 0..100. Set to 0.", m_int_configs[CONFIG_MIN_QUEST_SCALED_XP_RATIO]);
        m_int_configs[CONFIG_MIN_QUEST_SCALED_XP_RATIO] = 0;
    }

    m_int_configs[CONFIG_MIN_CREATURE_SCALED_XP_RATIO] = sConfigMgr->GetIntDefault("MinCreatureScaledXPRatio", 0);
    if (m_int_configs[CONFIG_MIN_CREATURE_SCALED_XP_RATIO] > 100)
    {
        TC_LOG_ERROR("server.loading", "MinCreatureScaledXPRatio ({}) must be in range 0..100. Set to 0.", m_int_configs[CONFIG_MIN_CREATURE_SCALED_XP_RATIO]);
        m_int_configs[CONFIG_MIN_CREATURE_SCALED_XP_RATIO] = 0;
    }

    m_int_configs[CONFIG_MIN_DISCOVERED_SCALED_XP_RATIO] = sConfigMgr->GetIntDefault("MinDiscoveredScaledXPRatio", 0);
    if (m_int_configs[CONFIG_MIN_DISCOVERED_SCALED_XP_RATIO] > 100)
    {
        TC_LOG_ERROR("server.loading", "MinDiscoveredScaledXPRatio ({}) must be in range 0..100. Set to 0.", m_int_configs[CONFIG_MIN_DISCOVERED_SCALED_XP_RATIO]);
        m_int_configs[CONFIG_MIN_DISCOVERED_SCALED_XP_RATIO] = 0;
    }

    /// @todo Add MonsterSight (with meaning) in worldserver.conf or put them as define
    m_float_configs[CONFIG_SIGHT_MONSTER] = sConfigMgr->GetFloatDefault("MonsterSight", 50.0f);

    m_bool_configs[CONFIG_REGEN_HP_CANNOT_REACH_TARGET_IN_RAID] = sConfigMgr->GetBoolDefault("Creature.RegenHPCannotReachTargetInRaid", true);

    if (reload)
    {
        uint32 val = sConfigMgr->GetIntDefault("GameType", 0);
        if (val != m_int_configs[CONFIG_GAME_TYPE])
            TC_LOG_ERROR("server.loading", "GameType option can't be changed at worldserver.conf reload, using current value ({}).", m_int_configs[CONFIG_GAME_TYPE]);
    }
    else
        m_int_configs[CONFIG_GAME_TYPE] = sConfigMgr->GetIntDefault("GameType", 0);

    if (reload)
    {
        uint32 val = sConfigMgr->GetIntDefault("RealmZone", REALM_ZONE_DEVELOPMENT);
        if (val != m_int_configs[CONFIG_REALM_ZONE])
            TC_LOG_ERROR("server.loading", "RealmZone option can't be changed at worldserver.conf reload, using current value ({}).", m_int_configs[CONFIG_REALM_ZONE]);
    }
    else
        m_int_configs[CONFIG_REALM_ZONE] = sConfigMgr->GetIntDefault("RealmZone", REALM_ZONE_DEVELOPMENT);

    m_bool_configs[CONFIG_ALLOW_TWO_SIDE_INTERACTION_CALENDAR]= sConfigMgr->GetBoolDefault("AllowTwoSide.Interaction.Calendar", false);
    m_bool_configs[CONFIG_ALLOW_TWO_SIDE_INTERACTION_CHANNEL] = sConfigMgr->GetBoolDefault("AllowTwoSide.Interaction.Channel", false);
    m_bool_configs[CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP]   = sConfigMgr->GetBoolDefault("AllowTwoSide.Interaction.Group", false);
    m_bool_configs[CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD]   = sConfigMgr->GetBoolDefault("AllowTwoSide.Interaction.Guild", false);
    m_bool_configs[CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION] = sConfigMgr->GetBoolDefault("AllowTwoSide.Interaction.Auction", false);
    m_bool_configs[CONFIG_ALLOW_TWO_SIDE_TRADE]               = sConfigMgr->GetBoolDefault("AllowTwoSide.Trade", false);
    m_int_configs[CONFIG_STRICT_PLAYER_NAMES]                 = sConfigMgr->GetIntDefault ("StrictPlayerNames",  0);
    m_int_configs[CONFIG_STRICT_CHARTER_NAMES]                = sConfigMgr->GetIntDefault ("StrictCharterNames", 0);
    m_int_configs[CONFIG_STRICT_PET_NAMES]                    = sConfigMgr->GetIntDefault ("StrictPetNames",     0);

    m_int_configs[CONFIG_MIN_PLAYER_NAME]                     = sConfigMgr->GetIntDefault ("MinPlayerName",  2);
    if (m_int_configs[CONFIG_MIN_PLAYER_NAME] < 1 || m_int_configs[CONFIG_MIN_PLAYER_NAME] > MAX_PLAYER_NAME)
    {
        TC_LOG_ERROR("server.loading", "MinPlayerName ({}) must be in range 1..{}. Set to 2.", m_int_configs[CONFIG_MIN_PLAYER_NAME], MAX_PLAYER_NAME);
        m_int_configs[CONFIG_MIN_PLAYER_NAME] = 2;
    }

    m_int_configs[CONFIG_MIN_CHARTER_NAME]                    = sConfigMgr->GetIntDefault ("MinCharterName", 2);
    if (m_int_configs[CONFIG_MIN_CHARTER_NAME] < 1 || m_int_configs[CONFIG_MIN_CHARTER_NAME] > MAX_CHARTER_NAME)
    {
        TC_LOG_ERROR("server.loading", "MinCharterName ({}) must be in range 1..{}. Set to 2.", m_int_configs[CONFIG_MIN_CHARTER_NAME], MAX_CHARTER_NAME);
        m_int_configs[CONFIG_MIN_CHARTER_NAME] = 2;
    }

    m_int_configs[CONFIG_MIN_PET_NAME]                        = sConfigMgr->GetIntDefault ("MinPetName",     2);
    if (m_int_configs[CONFIG_MIN_PET_NAME] < 1 || m_int_configs[CONFIG_MIN_PET_NAME] > MAX_PET_NAME)
    {
        TC_LOG_ERROR("server.loading", "MinPetName ({}) must be in range 1..{}. Set to 2.", m_int_configs[CONFIG_MIN_PET_NAME], MAX_PET_NAME);
        m_int_configs[CONFIG_MIN_PET_NAME] = 2;
    }

    m_int_configs[CONFIG_CHARTER_COST_GUILD] = sConfigMgr->GetIntDefault("Guild.CharterCost", 1000);
    m_int_configs[CONFIG_CHARTER_COST_ARENA_2v2] = sConfigMgr->GetIntDefault("ArenaTeam.CharterCost.2v2", 800000);
    m_int_configs[CONFIG_CHARTER_COST_ARENA_3v3] = sConfigMgr->GetIntDefault("ArenaTeam.CharterCost.3v3", 1200000);
    m_int_configs[CONFIG_CHARTER_COST_ARENA_5v5] = sConfigMgr->GetIntDefault("ArenaTeam.CharterCost.5v5", 2000000);

    m_int_configs[CONFIG_CHARACTER_CREATING_DISABLED] = sConfigMgr->GetIntDefault("CharacterCreating.Disabled", 0);
    m_int_configs[CONFIG_CHARACTER_CREATING_DISABLED_RACEMASK] = sConfigMgr->GetIntDefault("CharacterCreating.Disabled.RaceMask", 0);
    m_int_configs[CONFIG_CHARACTER_CREATING_DISABLED_CLASSMASK] = sConfigMgr->GetIntDefault("CharacterCreating.Disabled.ClassMask", 0);

    m_int_configs[CONFIG_CHARACTERS_PER_REALM] = sConfigMgr->GetIntDefault("CharactersPerRealm", MAX_CHARACTERS_PER_REALM);
    if (m_int_configs[CONFIG_CHARACTERS_PER_REALM] < 1 || m_int_configs[CONFIG_CHARACTERS_PER_REALM] > MAX_CHARACTERS_PER_REALM)
    {
        TC_LOG_ERROR("server.loading", "CharactersPerRealm ({}) must be in range 1..{}. Set to {}.", m_int_configs[CONFIG_CHARACTERS_PER_REALM], MAX_CHARACTERS_PER_REALM, MAX_CHARACTERS_PER_REALM);
        m_int_configs[CONFIG_CHARACTERS_PER_REALM] = MAX_CHARACTERS_PER_REALM;
    }

    // must be after CONFIG_CHARACTERS_PER_REALM
    m_int_configs[CONFIG_CHARACTERS_PER_ACCOUNT] = sConfigMgr->GetIntDefault("CharactersPerAccount", 50);
    if (m_int_configs[CONFIG_CHARACTERS_PER_ACCOUNT] < m_int_configs[CONFIG_CHARACTERS_PER_REALM])
    {
        TC_LOG_ERROR("server.loading", "CharactersPerAccount ({}) can't be less than CharactersPerRealm ({}).", m_int_configs[CONFIG_CHARACTERS_PER_ACCOUNT], m_int_configs[CONFIG_CHARACTERS_PER_REALM]);
        m_int_configs[CONFIG_CHARACTERS_PER_ACCOUNT] = m_int_configs[CONFIG_CHARACTERS_PER_REALM];
    }

    m_int_configs[CONFIG_DEATH_KNIGHTS_PER_REALM] = sConfigMgr->GetIntDefault("DeathKnightsPerRealm", 1);
    if (int32(m_int_configs[CONFIG_DEATH_KNIGHTS_PER_REALM]) < 0 || m_int_configs[CONFIG_DEATH_KNIGHTS_PER_REALM] > 10)
    {
        TC_LOG_ERROR("server.loading", "DeathKnightsPerRealm ({}) must be in range 0..10. Set to 1.", m_int_configs[CONFIG_DEATH_KNIGHTS_PER_REALM]);
        m_int_configs[CONFIG_DEATH_KNIGHTS_PER_REALM] = 1;
    }

    m_int_configs[CONFIG_CHARACTER_CREATING_MIN_LEVEL_FOR_DEATH_KNIGHT] = sConfigMgr->GetIntDefault("CharacterCreating.MinLevelForDeathKnight", 55);

    m_int_configs[CONFIG_SKIP_CINEMATICS] = sConfigMgr->GetIntDefault("SkipCinematics", 0);
    if (int32(m_int_configs[CONFIG_SKIP_CINEMATICS]) < 0 || m_int_configs[CONFIG_SKIP_CINEMATICS] > 2)
    {
        TC_LOG_ERROR("server.loading", "SkipCinematics ({}) must be in range 0..2. Set to 0.", m_int_configs[CONFIG_SKIP_CINEMATICS]);
        m_int_configs[CONFIG_SKIP_CINEMATICS] = 0;
    }

    if (reload)
    {
        uint32 val = sConfigMgr->GetIntDefault("MaxPlayerLevel", DEFAULT_MAX_LEVEL);
        if (val != m_int_configs[CONFIG_MAX_PLAYER_LEVEL])
            TC_LOG_ERROR("server.loading", "MaxPlayerLevel option can't be changed at config reload, using current value ({}).", m_int_configs[CONFIG_MAX_PLAYER_LEVEL]);
    }
    else
        m_int_configs[CONFIG_MAX_PLAYER_LEVEL] = sConfigMgr->GetIntDefault("MaxPlayerLevel", DEFAULT_MAX_LEVEL);

    if (m_int_configs[CONFIG_MAX_PLAYER_LEVEL] > MAX_LEVEL)
    {
        TC_LOG_ERROR("server.loading", "MaxPlayerLevel ({}) must be in range 1..{}. Set to {}.", m_int_configs[CONFIG_MAX_PLAYER_LEVEL], MAX_LEVEL, MAX_LEVEL);
        m_int_configs[CONFIG_MAX_PLAYER_LEVEL] = MAX_LEVEL;
    }

    m_int_configs[CONFIG_MIN_DUALSPEC_LEVEL] = sConfigMgr->GetIntDefault("MinDualSpecLevel", 40);

    m_int_configs[CONFIG_START_PLAYER_LEVEL] = sConfigMgr->GetIntDefault("StartPlayerLevel", 1);
    if (m_int_configs[CONFIG_START_PLAYER_LEVEL] < 1)
    {
        TC_LOG_ERROR("server.loading", "StartPlayerLevel ({}) must be in range 1..MaxPlayerLevel({}). Set to 1.", m_int_configs[CONFIG_START_PLAYER_LEVEL], m_int_configs[CONFIG_MAX_PLAYER_LEVEL]);
        m_int_configs[CONFIG_START_PLAYER_LEVEL] = 1;
    }
    else if (m_int_configs[CONFIG_START_PLAYER_LEVEL] > m_int_configs[CONFIG_MAX_PLAYER_LEVEL])
    {
        TC_LOG_ERROR("server.loading", "StartPlayerLevel ({}) must be in range 1..MaxPlayerLevel({}). Set to {}.", m_int_configs[CONFIG_START_PLAYER_LEVEL], m_int_configs[CONFIG_MAX_PLAYER_LEVEL], m_int_configs[CONFIG_MAX_PLAYER_LEVEL]);
        m_int_configs[CONFIG_START_PLAYER_LEVEL] = m_int_configs[CONFIG_MAX_PLAYER_LEVEL];
    }

    m_int_configs[CONFIG_START_DEATH_KNIGHT_PLAYER_LEVEL] = sConfigMgr->GetIntDefault("StartDeathKnightPlayerLevel", 55);
    if (m_int_configs[CONFIG_START_DEATH_KNIGHT_PLAYER_LEVEL] < 1)
    {
        TC_LOG_ERROR("server.loading", "StartDeathKnightPlayerLevel ({}) must be in range 1..MaxPlayerLevel({}). Set to 55.",
            m_int_configs[CONFIG_START_DEATH_KNIGHT_PLAYER_LEVEL], m_int_configs[CONFIG_MAX_PLAYER_LEVEL]);
        m_int_configs[CONFIG_START_DEATH_KNIGHT_PLAYER_LEVEL] = 55;
    }
    else if (m_int_configs[CONFIG_START_DEATH_KNIGHT_PLAYER_LEVEL] > m_int_configs[CONFIG_MAX_PLAYER_LEVEL])
    {
        TC_LOG_ERROR("server.loading", "StartDeathKnightPlayerLevel ({}) must be in range 1..MaxPlayerLevel({}). Set to {}.",
            m_int_configs[CONFIG_START_DEATH_KNIGHT_PLAYER_LEVEL], m_int_configs[CONFIG_MAX_PLAYER_LEVEL], m_int_configs[CONFIG_MAX_PLAYER_LEVEL]);
        m_int_configs[CONFIG_START_DEATH_KNIGHT_PLAYER_LEVEL] = m_int_configs[CONFIG_MAX_PLAYER_LEVEL];
    }

    m_int_configs[CONFIG_START_PLAYER_MONEY] = sConfigMgr->GetIntDefault("StartPlayerMoney", 0);
    if (int32(m_int_configs[CONFIG_START_PLAYER_MONEY]) < 0)
    {
        TC_LOG_ERROR("server.loading", "StartPlayerMoney ({}) must be in range 0..{}. Set to {}.", m_int_configs[CONFIG_START_PLAYER_MONEY], MAX_MONEY_AMOUNT, 0);
        m_int_configs[CONFIG_START_PLAYER_MONEY] = 0;
    }
    else if (m_int_configs[CONFIG_START_PLAYER_MONEY] > MAX_MONEY_AMOUNT)
    {
        TC_LOG_ERROR("server.loading", "StartPlayerMoney ({}) must be in range 0..{}. Set to {}.",
            m_int_configs[CONFIG_START_PLAYER_MONEY], MAX_MONEY_AMOUNT, MAX_MONEY_AMOUNT);
        m_int_configs[CONFIG_START_PLAYER_MONEY] = MAX_MONEY_AMOUNT;
    }

    m_int_configs[CONFIG_MAX_HONOR_POINTS] = sConfigMgr->GetIntDefault("MaxHonorPoints", 75000);
    if (int32(m_int_configs[CONFIG_MAX_HONOR_POINTS]) < 0)
    {
        TC_LOG_ERROR("server.loading", "MaxHonorPoints ({}) can't be negative. Set to 0.", m_int_configs[CONFIG_MAX_HONOR_POINTS]);
        m_int_configs[CONFIG_MAX_HONOR_POINTS] = 0;
    }

    m_int_configs[CONFIG_START_HONOR_POINTS] = sConfigMgr->GetIntDefault("StartHonorPoints", 0);
    if (int32(m_int_configs[CONFIG_START_HONOR_POINTS]) < 0)
    {
        TC_LOG_ERROR("server.loading", "StartHonorPoints ({}) must be in range 0..MaxHonorPoints({}). Set to {}.",
            m_int_configs[CONFIG_START_HONOR_POINTS], m_int_configs[CONFIG_MAX_HONOR_POINTS], 0);
        m_int_configs[CONFIG_START_HONOR_POINTS] = 0;
    }
    else if (m_int_configs[CONFIG_START_HONOR_POINTS] > m_int_configs[CONFIG_MAX_HONOR_POINTS])
    {
        TC_LOG_ERROR("server.loading", "StartHonorPoints ({}) must be in range 0..MaxHonorPoints({}). Set to {}.",
            m_int_configs[CONFIG_START_HONOR_POINTS], m_int_configs[CONFIG_MAX_HONOR_POINTS], m_int_configs[CONFIG_MAX_HONOR_POINTS]);
        m_int_configs[CONFIG_START_HONOR_POINTS] = m_int_configs[CONFIG_MAX_HONOR_POINTS];
    }

    m_int_configs[CONFIG_MAX_ARENA_POINTS] = sConfigMgr->GetIntDefault("MaxArenaPoints", 10000);
    if (int32(m_int_configs[CONFIG_MAX_ARENA_POINTS]) < 0)
    {
        TC_LOG_ERROR("server.loading", "MaxArenaPoints ({}) can't be negative. Set to 0.", m_int_configs[CONFIG_MAX_ARENA_POINTS]);
        m_int_configs[CONFIG_MAX_ARENA_POINTS] = 0;
    }

    m_int_configs[CONFIG_START_ARENA_POINTS] = sConfigMgr->GetIntDefault("StartArenaPoints", 0);
    if (int32(m_int_configs[CONFIG_START_ARENA_POINTS]) < 0)
    {
        TC_LOG_ERROR("server.loading", "StartArenaPoints ({}) must be in range 0..MaxArenaPoints({}). Set to {}.",
            m_int_configs[CONFIG_START_ARENA_POINTS], m_int_configs[CONFIG_MAX_ARENA_POINTS], 0);
        m_int_configs[CONFIG_START_ARENA_POINTS] = 0;
    }
    else if (m_int_configs[CONFIG_START_ARENA_POINTS] > m_int_configs[CONFIG_MAX_ARENA_POINTS])
    {
        TC_LOG_ERROR("server.loading", "StartArenaPoints ({}) must be in range 0..MaxArenaPoints({}). Set to {}.",
            m_int_configs[CONFIG_START_ARENA_POINTS], m_int_configs[CONFIG_MAX_ARENA_POINTS], m_int_configs[CONFIG_MAX_ARENA_POINTS]);
        m_int_configs[CONFIG_START_ARENA_POINTS] = m_int_configs[CONFIG_MAX_ARENA_POINTS];
    }

    m_int_configs[CONFIG_MAX_RECRUIT_A_FRIEND_BONUS_PLAYER_LEVEL] = sConfigMgr->GetIntDefault("RecruitAFriend.MaxLevel", 60);
    if (m_int_configs[CONFIG_MAX_RECRUIT_A_FRIEND_BONUS_PLAYER_LEVEL] > m_int_configs[CONFIG_MAX_PLAYER_LEVEL])
    {
        TC_LOG_ERROR("server.loading", "RecruitAFriend.MaxLevel ({}) must be in the range 0..MaxLevel({}). Set to {}.",
            m_int_configs[CONFIG_MAX_RECRUIT_A_FRIEND_BONUS_PLAYER_LEVEL], m_int_configs[CONFIG_MAX_PLAYER_LEVEL], 60);
        m_int_configs[CONFIG_MAX_RECRUIT_A_FRIEND_BONUS_PLAYER_LEVEL] = 60;
    }

    m_int_configs[CONFIG_MAX_RECRUIT_A_FRIEND_BONUS_PLAYER_LEVEL_DIFFERENCE] = sConfigMgr->GetIntDefault("RecruitAFriend.MaxDifference", 4);
    m_bool_configs[CONFIG_ALL_TAXI_PATHS] = sConfigMgr->GetBoolDefault("AllFlightPaths", false);
    m_bool_configs[CONFIG_INSTANT_TAXI] = sConfigMgr->GetBoolDefault("InstantFlightPaths", false);

    m_bool_configs[CONFIG_INSTANCE_IGNORE_LEVEL] = sConfigMgr->GetBoolDefault("Instance.IgnoreLevel", false);
    m_bool_configs[CONFIG_INSTANCE_IGNORE_RAID]  = sConfigMgr->GetBoolDefault("Instance.IgnoreRaid", false);

    m_bool_configs[CONFIG_CAST_UNSTUCK] = sConfigMgr->GetBoolDefault("CastUnstuck", true);
    m_int_configs[CONFIG_INSTANCE_RESET_TIME_HOUR]  = sConfigMgr->GetIntDefault("Instance.ResetTimeHour", 4);
    m_int_configs[CONFIG_INSTANCE_UNLOAD_DELAY] = sConfigMgr->GetIntDefault("Instance.UnloadDelay", 30 * MINUTE * IN_MILLISECONDS);

    m_int_configs[CONFIG_DAILY_QUEST_RESET_TIME_HOUR] = sConfigMgr->GetIntDefault("Quests.DailyResetTime", 3);
    if (m_int_configs[CONFIG_DAILY_QUEST_RESET_TIME_HOUR] > 23)
    {
        TC_LOG_ERROR("server.loading", "Quests.DailyResetTime ({}) must be in range 0..23. Set to 3.", m_int_configs[CONFIG_DAILY_QUEST_RESET_TIME_HOUR]);
        m_int_configs[CONFIG_DAILY_QUEST_RESET_TIME_HOUR] = 3;
    }

    m_int_configs[CONFIG_WEEKLY_QUEST_RESET_TIME_WDAY] = sConfigMgr->GetIntDefault("Quests.WeeklyResetWDay", 3);
    if (m_int_configs[CONFIG_WEEKLY_QUEST_RESET_TIME_WDAY] > 6)
    {
        TC_LOG_ERROR("server.loading", "Quests.WeeklyResetDay ({}) must be in range 0..6. Set to 3 (Wednesday).", m_int_configs[CONFIG_WEEKLY_QUEST_RESET_TIME_WDAY]);
        m_int_configs[CONFIG_WEEKLY_QUEST_RESET_TIME_WDAY] = 3;
    }

    m_int_configs[CONFIG_MAX_PRIMARY_TRADE_SKILL] = sConfigMgr->GetIntDefault("MaxPrimaryTradeSkill", 2);
    m_int_configs[CONFIG_MIN_PETITION_SIGNS] = sConfigMgr->GetIntDefault("MinPetitionSigns", 9);
    if (m_int_configs[CONFIG_MIN_PETITION_SIGNS] > 9)
    {
        TC_LOG_ERROR("server.loading", "MinPetitionSigns ({}) must be in range 0..9. Set to 9.", m_int_configs[CONFIG_MIN_PETITION_SIGNS]);
        m_int_configs[CONFIG_MIN_PETITION_SIGNS] = 9;
    }

    m_int_configs[CONFIG_GM_LOGIN_STATE]        = sConfigMgr->GetIntDefault("GM.LoginState", 2);
    m_int_configs[CONFIG_GM_VISIBLE_STATE]      = sConfigMgr->GetIntDefault("GM.Visible", 2);
    m_int_configs[CONFIG_GM_CHAT]               = sConfigMgr->GetIntDefault("GM.Chat", 2);
    m_int_configs[CONFIG_GM_WHISPERING_TO]      = sConfigMgr->GetIntDefault("GM.WhisperingTo", 2);
    m_int_configs[CONFIG_GM_FREEZE_DURATION]    = sConfigMgr->GetIntDefault("GM.FreezeAuraDuration", 0);

    m_int_configs[CONFIG_GM_LEVEL_IN_GM_LIST]   = sConfigMgr->GetIntDefault("GM.InGMList.Level", SEC_ADMINISTRATOR);
    m_int_configs[CONFIG_GM_LEVEL_IN_WHO_LIST]  = sConfigMgr->GetIntDefault("GM.InWhoList.Level", SEC_ADMINISTRATOR);
    m_int_configs[CONFIG_START_GM_LEVEL]        = sConfigMgr->GetIntDefault("GM.StartLevel", 1);
    if (m_int_configs[CONFIG_START_GM_LEVEL] < m_int_configs[CONFIG_START_PLAYER_LEVEL])
    {
        TC_LOG_ERROR("server.loading", "GM.StartLevel ({}) must be in range StartPlayerLevel({})..{}. Set to {}.",
            m_int_configs[CONFIG_START_GM_LEVEL], m_int_configs[CONFIG_START_PLAYER_LEVEL], MAX_LEVEL, m_int_configs[CONFIG_START_PLAYER_LEVEL]);
        m_int_configs[CONFIG_START_GM_LEVEL] = m_int_configs[CONFIG_START_PLAYER_LEVEL];
    }
    else if (m_int_configs[CONFIG_START_GM_LEVEL] > MAX_LEVEL)
    {
        TC_LOG_ERROR("server.loading", "GM.StartLevel ({}) must be in range 1..{}. Set to {}.", m_int_configs[CONFIG_START_GM_LEVEL], MAX_LEVEL, MAX_LEVEL);
        m_int_configs[CONFIG_START_GM_LEVEL] = MAX_LEVEL;
    }
    m_bool_configs[CONFIG_ALLOW_GM_GROUP]       = sConfigMgr->GetBoolDefault("GM.AllowInvite", false);
    m_bool_configs[CONFIG_GM_LOWER_SECURITY] = sConfigMgr->GetBoolDefault("GM.LowerSecurity", false);
    m_float_configs[CONFIG_CHANCE_OF_GM_SURVEY] = sConfigMgr->GetFloatDefault("GM.TicketSystem.ChanceOfGMSurvey", 50.0f);
    m_int_configs[CONFIG_FORCE_SHUTDOWN_THRESHOLD] = sConfigMgr->GetIntDefault("GM.ForceShutdownThreshold", 30);

    m_int_configs[CONFIG_GROUP_VISIBILITY] = sConfigMgr->GetIntDefault("Visibility.GroupMode", 1);

    m_int_configs[CONFIG_MAIL_DELIVERY_DELAY] = sConfigMgr->GetIntDefault("MailDeliveryDelay", HOUR);
    m_int_configs[CONFIG_CLEAN_OLD_MAIL_TIME] = sConfigMgr->GetIntDefault("CleanOldMailTime", 4);
    if (m_int_configs[CONFIG_CLEAN_OLD_MAIL_TIME] > 23)
    {
        TC_LOG_ERROR("server.loading", "CleanOldMailTime ({}) must be an hour, between 0 and 23. Set to 4.", m_int_configs[CONFIG_CLEAN_OLD_MAIL_TIME]);
        m_int_configs[CONFIG_CLEAN_OLD_MAIL_TIME] = 4;
    }

    m_int_configs[CONFIG_UPTIME_UPDATE] = sConfigMgr->GetIntDefault("UpdateUptimeInterval", 10);
    if (int32(m_int_configs[CONFIG_UPTIME_UPDATE]) <= 0)
    {
        TC_LOG_ERROR("server.loading", "UpdateUptimeInterval ({}) must be > 0, set to default 10.", m_int_configs[CONFIG_UPTIME_UPDATE]);
        m_int_configs[CONFIG_UPTIME_UPDATE] = 10;
    }
    if (reload)
    {
        m_timers[WUPDATE_UPTIME].SetInterval(m_int_configs[CONFIG_UPTIME_UPDATE]*MINUTE*IN_MILLISECONDS);
        m_timers[WUPDATE_UPTIME].Reset();
    }

    // log db cleanup interval
    m_int_configs[CONFIG_LOGDB_CLEARINTERVAL] = sConfigMgr->GetIntDefault("LogDB.Opt.ClearInterval", 10);
    if (int32(m_int_configs[CONFIG_LOGDB_CLEARINTERVAL]) <= 0)
    {
        TC_LOG_ERROR("server.loading", "LogDB.Opt.ClearInterval ({}) must be > 0, set to default 10.", m_int_configs[CONFIG_LOGDB_CLEARINTERVAL]);
        m_int_configs[CONFIG_LOGDB_CLEARINTERVAL] = 10;
    }
    if (reload)
    {
        m_timers[WUPDATE_CLEANDB].SetInterval(m_int_configs[CONFIG_LOGDB_CLEARINTERVAL] * MINUTE * IN_MILLISECONDS);
        m_timers[WUPDATE_CLEANDB].Reset();
    }
    m_int_configs[CONFIG_LOGDB_CLEARTIME] = sConfigMgr->GetIntDefault("LogDB.Opt.ClearTime", 1209600); // 14 days default
    TC_LOG_INFO("server.loading", "Will clear `logs` table of entries older than {} seconds every {} minutes.",
        m_int_configs[CONFIG_LOGDB_CLEARTIME], m_int_configs[CONFIG_LOGDB_CLEARINTERVAL]);

    m_int_configs[CONFIG_SKILL_CHANCE_ORANGE] = sConfigMgr->GetIntDefault("SkillChance.Orange", 100);
    m_int_configs[CONFIG_SKILL_CHANCE_YELLOW] = sConfigMgr->GetIntDefault("SkillChance.Yellow", 75);
    m_int_configs[CONFIG_SKILL_CHANCE_GREEN]  = sConfigMgr->GetIntDefault("SkillChance.Green", 25);
    m_int_configs[CONFIG_SKILL_CHANCE_GREY]   = sConfigMgr->GetIntDefault("SkillChance.Grey", 0);

    m_int_configs[CONFIG_SKILL_CHANCE_MINING_STEPS]  = sConfigMgr->GetIntDefault("SkillChance.MiningSteps", 75);
    m_int_configs[CONFIG_SKILL_CHANCE_SKINNING_STEPS]   = sConfigMgr->GetIntDefault("SkillChance.SkinningSteps", 75);

    m_bool_configs[CONFIG_SKILL_PROSPECTING] = sConfigMgr->GetBoolDefault("SkillChance.Prospecting", false);
    m_bool_configs[CONFIG_SKILL_MILLING] = sConfigMgr->GetBoolDefault("SkillChance.Milling", false);

    m_int_configs[CONFIG_SKILL_GAIN_CRAFTING]  = sConfigMgr->GetIntDefault("SkillGain.Crafting", 1);

    m_int_configs[CONFIG_SKILL_GAIN_DEFENSE]  = sConfigMgr->GetIntDefault("SkillGain.Defense", 1);

    m_int_configs[CONFIG_SKILL_GAIN_GATHERING]  = sConfigMgr->GetIntDefault("SkillGain.Gathering", 1);

    m_int_configs[CONFIG_SKILL_GAIN_WEAPON]  = sConfigMgr->GetIntDefault("SkillGain.Weapon", 1);

    m_int_configs[CONFIG_MAX_OVERSPEED_PINGS] = sConfigMgr->GetIntDefault("MaxOverspeedPings", 2);
    if (m_int_configs[CONFIG_MAX_OVERSPEED_PINGS] != 0 && m_int_configs[CONFIG_MAX_OVERSPEED_PINGS] < 2)
    {
        TC_LOG_ERROR("server.loading", "MaxOverspeedPings ({}) must be in range 2..infinity (or 0 to disable check). Set to 2.", m_int_configs[CONFIG_MAX_OVERSPEED_PINGS]);
        m_int_configs[CONFIG_MAX_OVERSPEED_PINGS] = 2;
    }

    m_bool_configs[CONFIG_WEATHER] = sConfigMgr->GetBoolDefault("ActivateWeather", true);

    m_int_configs[CONFIG_DISABLE_BREATHING] = sConfigMgr->GetIntDefault("DisableWaterBreath", SEC_CONSOLE);

    m_bool_configs[CONFIG_ALWAYS_MAX_SKILL_FOR_LEVEL] = sConfigMgr->GetBoolDefault("AlwaysMaxSkillForLevel", false);

    if (reload)
    {
        uint32 val = sConfigMgr->GetIntDefault("Expansion", 2);
        if (val != m_int_configs[CONFIG_EXPANSION])
            TC_LOG_ERROR("server.loading", "Expansion option can't be changed at worldserver.conf reload, using current value ({}).", m_int_configs[CONFIG_EXPANSION]);
    }
    else
        m_int_configs[CONFIG_EXPANSION] = sConfigMgr->GetIntDefault("Expansion", 2);

    m_int_configs[CONFIG_CHATFLOOD_MESSAGE_COUNT] = sConfigMgr->GetIntDefault("ChatFlood.MessageCount", 10);
    m_int_configs[CONFIG_CHATFLOOD_MESSAGE_DELAY] = sConfigMgr->GetIntDefault("ChatFlood.MessageDelay", 1);
    m_int_configs[CONFIG_CHATFLOOD_ADDON_MESSAGE_COUNT] = sConfigMgr->GetIntDefault("ChatFlood.AddonMessageCount", 100);
    m_int_configs[CONFIG_CHATFLOOD_ADDON_MESSAGE_DELAY] = sConfigMgr->GetIntDefault("ChatFlood.AddonMessageDelay", 1);
    m_int_configs[CONFIG_CHATFLOOD_MUTE_TIME]     = sConfigMgr->GetIntDefault("ChatFlood.MuteTime", 10);

    m_bool_configs[CONFIG_EVENT_ANNOUNCE] = sConfigMgr->GetBoolDefault("Event.Announce", false);

    m_float_configs[CONFIG_CREATURE_FAMILY_FLEE_ASSISTANCE_RADIUS] = sConfigMgr->GetFloatDefault("CreatureFamilyFleeAssistanceRadius", 30.0f);
    m_float_configs[CONFIG_CREATURE_FAMILY_ASSISTANCE_RADIUS] = sConfigMgr->GetFloatDefault("CreatureFamilyAssistanceRadius", 10.0f);
    m_int_configs[CONFIG_CREATURE_FAMILY_ASSISTANCE_DELAY]  = sConfigMgr->GetIntDefault("CreatureFamilyAssistanceDelay", 1500);
    m_int_configs[CONFIG_CREATURE_FAMILY_FLEE_DELAY]        = sConfigMgr->GetIntDefault("CreatureFamilyFleeDelay", 7000);

    m_int_configs[CONFIG_WORLD_BOSS_LEVEL_DIFF] = sConfigMgr->GetIntDefault("WorldBossLevelDiff", 3);

    m_bool_configs[CONFIG_QUEST_ENABLE_QUEST_TRACKER] = sConfigMgr->GetBoolDefault("Quests.EnableQuestTracker", false);

    // note: disable value (-1) will assigned as 0xFFFFFFF, to prevent overflow at calculations limit it to max possible player level MAX_LEVEL(100)
    m_int_configs[CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF] = sConfigMgr->GetIntDefault("Quests.LowLevelHideDiff", 4);
    if (m_int_configs[CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF] > MAX_LEVEL)
        m_int_configs[CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF] = MAX_LEVEL;
    m_int_configs[CONFIG_QUEST_HIGH_LEVEL_HIDE_DIFF] = sConfigMgr->GetIntDefault("Quests.HighLevelHideDiff", 7);
    if (m_int_configs[CONFIG_QUEST_HIGH_LEVEL_HIDE_DIFF] > MAX_LEVEL)
        m_int_configs[CONFIG_QUEST_HIGH_LEVEL_HIDE_DIFF] = MAX_LEVEL;
    m_bool_configs[CONFIG_QUEST_IGNORE_RAID] = sConfigMgr->GetBoolDefault("Quests.IgnoreRaid", false);
    m_bool_configs[CONFIG_QUEST_IGNORE_AUTO_ACCEPT] = sConfigMgr->GetBoolDefault("Quests.IgnoreAutoAccept", false);
    m_bool_configs[CONFIG_QUEST_IGNORE_AUTO_COMPLETE] = sConfigMgr->GetBoolDefault("Quests.IgnoreAutoComplete", false);

    m_int_configs[CONFIG_RANDOM_BG_RESET_HOUR] = sConfigMgr->GetIntDefault("Battleground.Random.ResetHour", 6);
    if (m_int_configs[CONFIG_RANDOM_BG_RESET_HOUR] > 23)
    {
        TC_LOG_ERROR("server.loading", "Battleground.Random.ResetHour ({}) can't be load. Set to 6.", m_int_configs[CONFIG_RANDOM_BG_RESET_HOUR]);
        m_int_configs[CONFIG_RANDOM_BG_RESET_HOUR] = 6;
    }

    m_int_configs[CONFIG_CALENDAR_DELETE_OLD_EVENTS_HOUR] = sConfigMgr->GetIntDefault("Calendar.DeleteOldEventsHour", 6);
    if (m_int_configs[CONFIG_CALENDAR_DELETE_OLD_EVENTS_HOUR] > 23)
    {
        TC_LOG_ERROR("misc", "Calendar.DeleteOldEventsHour ({}) can't be load. Set to 6.", m_int_configs[CONFIG_CALENDAR_DELETE_OLD_EVENTS_HOUR]);
        m_int_configs[CONFIG_CALENDAR_DELETE_OLD_EVENTS_HOUR] = 6;
    }

    m_int_configs[CONFIG_GUILD_RESET_HOUR] = sConfigMgr->GetIntDefault("Guild.ResetHour", 6);
    if (m_int_configs[CONFIG_GUILD_RESET_HOUR] > 23)
    {
        TC_LOG_ERROR("misc", "Guild.ResetHour ({}) can't be load. Set to 6.", m_int_configs[CONFIG_GUILD_RESET_HOUR]);
        m_int_configs[CONFIG_GUILD_RESET_HOUR] = 6;
    }

    m_bool_configs[CONFIG_DETECT_POS_COLLISION] = sConfigMgr->GetBoolDefault("DetectPosCollision", true);

    m_bool_configs[CONFIG_RESTRICTED_LFG_CHANNEL]      = sConfigMgr->GetBoolDefault("Channel.RestrictedLfg", true);
    m_int_configs[CONFIG_TALENTS_INSPECTING]           = sConfigMgr->GetIntDefault("TalentsInspecting", 1);
    m_bool_configs[CONFIG_CHAT_FAKE_MESSAGE_PREVENTING] = sConfigMgr->GetBoolDefault("ChatFakeMessagePreventing", false);
    m_int_configs[CONFIG_CHAT_STRICT_LINK_CHECKING_SEVERITY] = sConfigMgr->GetIntDefault("ChatStrictLinkChecking.Severity", 0);
    m_int_configs[CONFIG_CHAT_STRICT_LINK_CHECKING_KICK] = sConfigMgr->GetIntDefault("ChatStrictLinkChecking.Kick", 0);

    m_int_configs[CONFIG_CORPSE_DECAY_NORMAL]    = sConfigMgr->GetIntDefault("Corpse.Decay.NORMAL", 60);
    m_int_configs[CONFIG_CORPSE_DECAY_RARE]      = sConfigMgr->GetIntDefault("Corpse.Decay.RARE", 300);
    m_int_configs[CONFIG_CORPSE_DECAY_ELITE]     = sConfigMgr->GetIntDefault("Corpse.Decay.ELITE", 300);
    m_int_configs[CONFIG_CORPSE_DECAY_RAREELITE] = sConfigMgr->GetIntDefault("Corpse.Decay.RAREELITE", 300);
    m_int_configs[CONFIG_CORPSE_DECAY_WORLDBOSS] = sConfigMgr->GetIntDefault("Corpse.Decay.WORLDBOSS", 3600);

    m_int_configs[CONFIG_DEATH_SICKNESS_LEVEL]           = sConfigMgr->GetIntDefault ("Death.SicknessLevel", 11);
    m_bool_configs[CONFIG_DEATH_CORPSE_RECLAIM_DELAY_PVP] = sConfigMgr->GetBoolDefault("Death.CorpseReclaimDelay.PvP", true);
    m_bool_configs[CONFIG_DEATH_CORPSE_RECLAIM_DELAY_PVE] = sConfigMgr->GetBoolDefault("Death.CorpseReclaimDelay.PvE", true);
    m_bool_configs[CONFIG_DEATH_BONES_WORLD]              = sConfigMgr->GetBoolDefault("Death.Bones.World", true);
    m_bool_configs[CONFIG_DEATH_BONES_BG_OR_ARENA]        = sConfigMgr->GetBoolDefault("Death.Bones.BattlegroundOrArena", true);

    m_bool_configs[CONFIG_DIE_COMMAND_MODE] = sConfigMgr->GetBoolDefault("Die.Command.Mode", true);

    m_float_configs[CONFIG_THREAT_RADIUS] = sConfigMgr->GetFloatDefault("ThreatRadius", 60.0f);

    // always use declined names in the russian client
    m_bool_configs[CONFIG_DECLINED_NAMES_USED] =
        (m_int_configs[CONFIG_REALM_ZONE] == REALM_ZONE_RUSSIAN) ? true : sConfigMgr->GetBoolDefault("DeclinedNames", false);

    m_float_configs[CONFIG_LISTEN_RANGE_SAY]       = sConfigMgr->GetFloatDefault("ListenRange.Say", 25.0f);
    m_float_configs[CONFIG_LISTEN_RANGE_TEXTEMOTE] = sConfigMgr->GetFloatDefault("ListenRange.TextEmote", 25.0f);
    m_float_configs[CONFIG_LISTEN_RANGE_YELL]      = sConfigMgr->GetFloatDefault("ListenRange.Yell", 300.0f);

    m_bool_configs[CONFIG_BATTLEGROUND_CAST_DESERTER]                = sConfigMgr->GetBoolDefault("Battleground.CastDeserter", true);
    m_bool_configs[CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_ENABLE]       = sConfigMgr->GetBoolDefault("Battleground.QueueAnnouncer.Enable", false);
    m_bool_configs[CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_PLAYERONLY]   = sConfigMgr->GetBoolDefault("Battleground.QueueAnnouncer.PlayerOnly", false);
    m_bool_configs[CONFIG_BATTLEGROUND_STORE_STATISTICS_ENABLE]      = sConfigMgr->GetBoolDefault("Battleground.StoreStatistics.Enable", false);
    m_bool_configs[CONFIG_BATTLEGROUND_TRACK_DESERTERS]              = sConfigMgr->GetBoolDefault("Battleground.TrackDeserters.Enable", false);
    m_int_configs[CONFIG_BATTLEGROUND_REPORT_AFK]                    = sConfigMgr->GetIntDefault("Battleground.ReportAFK", 3);
    if (m_int_configs[CONFIG_BATTLEGROUND_REPORT_AFK] < 1)
    {
        TC_LOG_ERROR("server.loading", "Battleground.ReportAFK ({}) must be >0. Using 3 instead.", m_int_configs[CONFIG_BATTLEGROUND_REPORT_AFK]);
        m_int_configs[CONFIG_BATTLEGROUND_REPORT_AFK] = 3;
    }
    if (m_int_configs[CONFIG_BATTLEGROUND_REPORT_AFK] > 9)
    {
        TC_LOG_ERROR("server.loading", "Battleground.ReportAFK ({}) must be <10. Using 3 instead.", m_int_configs[CONFIG_BATTLEGROUND_REPORT_AFK]);
        m_int_configs[CONFIG_BATTLEGROUND_REPORT_AFK] = 3;
    }
    m_int_configs[CONFIG_BATTLEGROUND_INVITATION_TYPE]               = sConfigMgr->GetIntDefault ("Battleground.InvitationType", 0);
    m_int_configs[CONFIG_BATTLEGROUND_PREMATURE_FINISH_TIMER]        = sConfigMgr->GetIntDefault ("Battleground.PrematureFinishTimer", 5 * MINUTE * IN_MILLISECONDS);
    m_int_configs[CONFIG_BATTLEGROUND_PREMADE_GROUP_WAIT_FOR_MATCH]  = sConfigMgr->GetIntDefault ("Battleground.PremadeGroupWaitForMatch", 30 * MINUTE * IN_MILLISECONDS);
    m_bool_configs[CONFIG_BG_XP_FOR_KILL]                            = sConfigMgr->GetBoolDefault("Battleground.GiveXPForKills", false);
    m_int_configs[CONFIG_ARENA_MAX_RATING_DIFFERENCE]                = sConfigMgr->GetIntDefault ("Arena.MaxRatingDifference", 150);
    m_int_configs[CONFIG_ARENA_RATING_DISCARD_TIMER]                 = sConfigMgr->GetIntDefault ("Arena.RatingDiscardTimer", 10 * MINUTE * IN_MILLISECONDS);
    m_int_configs[CONFIG_ARENA_PREV_OPPONENTS_DISCARD_TIMER]         = sConfigMgr->GetIntDefault ("Arena.PreviousOpponentsDiscardTimer", 2 * MINUTE * IN_MILLISECONDS);
    m_int_configs[CONFIG_ARENA_RATED_UPDATE_TIMER]                   = sConfigMgr->GetIntDefault ("Arena.RatedUpdateTimer", 5 * IN_MILLISECONDS);
    m_bool_configs[CONFIG_ARENA_AUTO_DISTRIBUTE_POINTS]              = sConfigMgr->GetBoolDefault("Arena.AutoDistributePoints", false);
    m_int_configs[CONFIG_ARENA_AUTO_DISTRIBUTE_INTERVAL_DAYS]        = sConfigMgr->GetIntDefault ("Arena.AutoDistributeInterval", 7);
    m_bool_configs[CONFIG_ARENA_QUEUE_ANNOUNCER_ENABLE]              = sConfigMgr->GetBoolDefault("Arena.QueueAnnouncer.Enable", false);
    m_int_configs[CONFIG_ARENA_SEASON_ID]                            = sConfigMgr->GetIntDefault ("Arena.ArenaSeason.ID", 1);
    m_int_configs[CONFIG_ARENA_START_RATING]                         = sConfigMgr->GetIntDefault ("Arena.ArenaStartRating", 0);
    m_int_configs[CONFIG_ARENA_START_PERSONAL_RATING]                = sConfigMgr->GetIntDefault ("Arena.ArenaStartPersonalRating", 1000);
    m_int_configs[CONFIG_ARENA_START_MATCHMAKER_RATING]              = sConfigMgr->GetIntDefault ("Arena.ArenaStartMatchmakerRating", 1500);
    m_bool_configs[CONFIG_ARENA_SEASON_IN_PROGRESS]                  = sConfigMgr->GetBoolDefault("Arena.ArenaSeason.InProgress", true);
    m_bool_configs[CONFIG_ARENA_LOG_EXTENDED_INFO]                   = sConfigMgr->GetBoolDefault("ArenaLog.ExtendedInfo", false);
    m_float_configs[CONFIG_ARENA_WIN_RATING_MODIFIER_1]              = sConfigMgr->GetFloatDefault("Arena.ArenaWinRatingModifier1", 48.0f);
    m_float_configs[CONFIG_ARENA_WIN_RATING_MODIFIER_2]              = sConfigMgr->GetFloatDefault("Arena.ArenaWinRatingModifier2", 24.0f);
    m_float_configs[CONFIG_ARENA_LOSE_RATING_MODIFIER]               = sConfigMgr->GetFloatDefault("Arena.ArenaLoseRatingModifier", 24.0f);
    m_float_configs[CONFIG_ARENA_MATCHMAKER_RATING_MODIFIER]         = sConfigMgr->GetFloatDefault("Arena.ArenaMatchmakerRatingModifier", 24.0f);

    m_bool_configs[CONFIG_OFFHAND_CHECK_AT_SPELL_UNLEARN]            = sConfigMgr->GetBoolDefault("OffhandCheckAtSpellUnlearn", true);

    m_int_configs[CONFIG_CREATURE_PICKPOCKET_REFILL] = sConfigMgr->GetIntDefault("Creature.PickPocketRefillDelay", 10 * MINUTE);
    m_int_configs[CONFIG_CREATURE_STOP_FOR_PLAYER] = sConfigMgr->GetIntDefault("Creature.MovingStopTimeForPlayer", 3 * MINUTE * IN_MILLISECONDS);

    if (int32 clientCacheId = sConfigMgr->GetIntDefault("ClientCacheVersion", 0))
    {
        // overwrite DB/old value
        if (clientCacheId > 0)
            m_int_configs[CONFIG_CLIENTCACHE_VERSION] = clientCacheId;
        else
            TC_LOG_ERROR("server.loading", "ClientCacheVersion can't be negative {}, ignored.", clientCacheId);
    }
    TC_LOG_INFO("server.loading", "Client cache version set to: {}", m_int_configs[CONFIG_CLIENTCACHE_VERSION]);

    m_int_configs[CONFIG_GUILD_EVENT_LOG_COUNT] = sConfigMgr->GetIntDefault("Guild.EventLogRecordsCount", GUILD_EVENTLOG_MAX_RECORDS);
    if (m_int_configs[CONFIG_GUILD_EVENT_LOG_COUNT] > GUILD_EVENTLOG_MAX_RECORDS)
        m_int_configs[CONFIG_GUILD_EVENT_LOG_COUNT] = GUILD_EVENTLOG_MAX_RECORDS;
    m_int_configs[CONFIG_GUILD_BANK_EVENT_LOG_COUNT] = sConfigMgr->GetIntDefault("Guild.BankEventLogRecordsCount", GUILD_BANKLOG_MAX_RECORDS);
    if (m_int_configs[CONFIG_GUILD_BANK_EVENT_LOG_COUNT] > GUILD_BANKLOG_MAX_RECORDS)
        m_int_configs[CONFIG_GUILD_BANK_EVENT_LOG_COUNT] = GUILD_BANKLOG_MAX_RECORDS;

    // visibility on continents
    m_MaxVisibleDistanceOnContinents = sConfigMgr->GetFloatDefault("Visibility.Distance.Continents", DEFAULT_VISIBILITY_DISTANCE);
    if (m_MaxVisibleDistanceOnContinents < 45*sWorld->getRate(RATE_CREATURE_AGGRO))
    {
        TC_LOG_ERROR("server.loading", "Visibility.Distance.Continents can't be less max aggro radius {}", 45*sWorld->getRate(RATE_CREATURE_AGGRO));
        m_MaxVisibleDistanceOnContinents = 45*sWorld->getRate(RATE_CREATURE_AGGRO);
    }
    else if (m_MaxVisibleDistanceOnContinents > MAX_VISIBILITY_DISTANCE)
    {
        TC_LOG_ERROR("server.loading", "Visibility.Distance.Continents can't be greater {}", MAX_VISIBILITY_DISTANCE);
        m_MaxVisibleDistanceOnContinents = MAX_VISIBILITY_DISTANCE;
    }

    // visibility in instances
    m_MaxVisibleDistanceInInstances = sConfigMgr->GetFloatDefault("Visibility.Distance.Instances", DEFAULT_VISIBILITY_INSTANCE);
    if (m_MaxVisibleDistanceInInstances < 45*sWorld->getRate(RATE_CREATURE_AGGRO))
    {
        TC_LOG_ERROR("server.loading", "Visibility.Distance.Instances can't be less max aggro radius {}", 45*sWorld->getRate(RATE_CREATURE_AGGRO));
        m_MaxVisibleDistanceInInstances = 45*sWorld->getRate(RATE_CREATURE_AGGRO);
    }
    else if (m_MaxVisibleDistanceInInstances > MAX_VISIBILITY_DISTANCE)
    {
        TC_LOG_ERROR("server.loading", "Visibility.Distance.Instances can't be greater {}", MAX_VISIBILITY_DISTANCE);
        m_MaxVisibleDistanceInInstances = MAX_VISIBILITY_DISTANCE;
    }

    // visibility in BG
    m_MaxVisibleDistanceInBG = sConfigMgr->GetFloatDefault("Visibility.Distance.BG", DEFAULT_VISIBILITY_BGARENAS);
    if (m_MaxVisibleDistanceInBG < 45*sWorld->getRate(RATE_CREATURE_AGGRO))
    {
        TC_LOG_ERROR("server.loading", "Visibility.Distance.BG can't be less max aggro radius {}", 45*sWorld->getRate(RATE_CREATURE_AGGRO));
        m_MaxVisibleDistanceInBG = 45*sWorld->getRate(RATE_CREATURE_AGGRO);
    }
    else if (m_MaxVisibleDistanceInBG > MAX_VISIBILITY_DISTANCE)
    {
        TC_LOG_ERROR("server.loading", "Visibility.Distance.BG can't be greater {}", MAX_VISIBILITY_DISTANCE);
        m_MaxVisibleDistanceInBG = MAX_VISIBILITY_DISTANCE;
    }

    // Visibility in Arenas
    m_MaxVisibleDistanceInArenas = sConfigMgr->GetFloatDefault("Visibility.Distance.Arenas", DEFAULT_VISIBILITY_BGARENAS);
    if (m_MaxVisibleDistanceInArenas < 45*sWorld->getRate(RATE_CREATURE_AGGRO))
    {
        TC_LOG_ERROR("server.loading", "Visibility.Distance.Arenas can't be less max aggro radius {}", 45*sWorld->getRate(RATE_CREATURE_AGGRO));
        m_MaxVisibleDistanceInArenas = 45*sWorld->getRate(RATE_CREATURE_AGGRO);
    }
    else if (m_MaxVisibleDistanceInArenas > MAX_VISIBILITY_DISTANCE)
    {
        TC_LOG_ERROR("server.loading", "Visibility.Distance.Arenas can't be greater {}", MAX_VISIBILITY_DISTANCE);
        m_MaxVisibleDistanceInArenas = MAX_VISIBILITY_DISTANCE;
    }

    m_visibility_notify_periodOnContinents = sConfigMgr->GetIntDefault("Visibility.Notify.Period.OnContinents", DEFAULT_VISIBILITY_NOTIFY_PERIOD);
    m_visibility_notify_periodInInstances  = sConfigMgr->GetIntDefault("Visibility.Notify.Period.InInstances",  DEFAULT_VISIBILITY_NOTIFY_PERIOD);
    m_visibility_notify_periodInBG         = sConfigMgr->GetIntDefault("Visibility.Notify.Period.InBG",         DEFAULT_VISIBILITY_NOTIFY_PERIOD);
    m_visibility_notify_periodInArenas     = sConfigMgr->GetIntDefault("Visibility.Notify.Period.InArenas",     DEFAULT_VISIBILITY_NOTIFY_PERIOD);

    ///- Load the CharDelete related config options
    m_int_configs[CONFIG_CHARDELETE_METHOD] = sConfigMgr->GetIntDefault("CharDelete.Method", 0);
    m_int_configs[CONFIG_CHARDELETE_MIN_LEVEL] = sConfigMgr->GetIntDefault("CharDelete.MinLevel", 0);
    m_int_configs[CONFIG_CHARDELETE_DEATH_KNIGHT_MIN_LEVEL] = sConfigMgr->GetIntDefault("CharDelete.DeathKnight.MinLevel", 0);
    m_int_configs[CONFIG_CHARDELETE_KEEP_DAYS] = sConfigMgr->GetIntDefault("CharDelete.KeepDays", 30);

    // No aggro from gray mobs
    m_int_configs[CONFIG_NO_GRAY_AGGRO_ABOVE] = sConfigMgr->GetIntDefault("NoGrayAggro.Above", 0);
    m_int_configs[CONFIG_NO_GRAY_AGGRO_BELOW] = sConfigMgr->GetIntDefault("NoGrayAggro.Below", 0);
    if (m_int_configs[CONFIG_NO_GRAY_AGGRO_ABOVE] > m_int_configs[CONFIG_MAX_PLAYER_LEVEL])
    {
       TC_LOG_ERROR("server.loading", "NoGrayAggro.Above ({}) must be in range 0..{}. Set to {}.", m_int_configs[CONFIG_NO_GRAY_AGGRO_ABOVE], m_int_configs[CONFIG_MAX_PLAYER_LEVEL], m_int_configs[CONFIG_MAX_PLAYER_LEVEL]);
       m_int_configs[CONFIG_NO_GRAY_AGGRO_ABOVE] = m_int_configs[CONFIG_MAX_PLAYER_LEVEL];
    }
    if (m_int_configs[CONFIG_NO_GRAY_AGGRO_BELOW] > m_int_configs[CONFIG_MAX_PLAYER_LEVEL])
    {
       TC_LOG_ERROR("server.loading", "NoGrayAggro.Below ({}) must be in range 0..{}. Set to {}.", m_int_configs[CONFIG_NO_GRAY_AGGRO_BELOW], m_int_configs[CONFIG_MAX_PLAYER_LEVEL], m_int_configs[CONFIG_MAX_PLAYER_LEVEL]);
       m_int_configs[CONFIG_NO_GRAY_AGGRO_BELOW] = m_int_configs[CONFIG_MAX_PLAYER_LEVEL];
    }
    if (m_int_configs[CONFIG_NO_GRAY_AGGRO_ABOVE] > 0 && m_int_configs[CONFIG_NO_GRAY_AGGRO_ABOVE] < m_int_configs[CONFIG_NO_GRAY_AGGRO_BELOW])
    {
       TC_LOG_ERROR("server.loading", "NoGrayAggro.Below ({}) cannot be greater than NoGrayAggro.Above ({}). Set to {}.", m_int_configs[CONFIG_NO_GRAY_AGGRO_BELOW], m_int_configs[CONFIG_NO_GRAY_AGGRO_ABOVE], m_int_configs[CONFIG_NO_GRAY_AGGRO_ABOVE]);
       m_int_configs[CONFIG_NO_GRAY_AGGRO_BELOW] = m_int_configs[CONFIG_NO_GRAY_AGGRO_ABOVE];
    }

    // Respawn Settings
    m_int_configs[CONFIG_RESPAWN_MINCHECKINTERVALMS] = sConfigMgr->GetIntDefault("Respawn.MinCheckIntervalMS", 5000);
    m_int_configs[CONFIG_RESPAWN_DYNAMICMODE] = sConfigMgr->GetIntDefault("Respawn.DynamicMode", 0);
    if (m_int_configs[CONFIG_RESPAWN_DYNAMICMODE] > 1)
    {
        TC_LOG_ERROR("server.loading", "Invalid value for Respawn.DynamicMode ({}). Set to 0.", m_int_configs[CONFIG_RESPAWN_DYNAMICMODE]);
        m_int_configs[CONFIG_RESPAWN_DYNAMICMODE] = 0;
    }
    m_bool_configs[CONFIG_RESPAWN_DYNAMIC_ESCORTNPC] = sConfigMgr->GetBoolDefault("Respawn.DynamicEscortNPC", false);
    m_int_configs[CONFIG_RESPAWN_GUIDWARNLEVEL] = sConfigMgr->GetIntDefault("Respawn.GuidWarnLevel", 12000000);
    if (m_int_configs[CONFIG_RESPAWN_GUIDWARNLEVEL] > 16777215)
    {
        TC_LOG_ERROR("server.loading", "Respawn.GuidWarnLevel ({}) cannot be greater than maximum GUID (16777215). Set to 12000000.", m_int_configs[CONFIG_RESPAWN_GUIDWARNLEVEL]);
        m_int_configs[CONFIG_RESPAWN_GUIDWARNLEVEL] = 12000000;
    }
    m_int_configs[CONFIG_RESPAWN_GUIDALERTLEVEL] = sConfigMgr->GetIntDefault("Respawn.GuidAlertLevel", 16000000);
    if (m_int_configs[CONFIG_RESPAWN_GUIDALERTLEVEL] > 16777215)
    {
        TC_LOG_ERROR("server.loading", "Respawn.GuidWarnLevel ({}) cannot be greater than maximum GUID (16777215). Set to 16000000.", m_int_configs[CONFIG_RESPAWN_GUIDALERTLEVEL]);
        m_int_configs[CONFIG_RESPAWN_GUIDALERTLEVEL] = 16000000;
    }
    m_int_configs[CONFIG_RESPAWN_RESTARTQUIETTIME] = sConfigMgr->GetIntDefault("Respawn.RestartQuietTime", 3);
    if (m_int_configs[CONFIG_RESPAWN_RESTARTQUIETTIME] > 23)
    {
        TC_LOG_ERROR("server.loading", "Respawn.RestartQuietTime ({}) must be an hour, between 0 and 23. Set to 3.", m_int_configs[CONFIG_RESPAWN_RESTARTQUIETTIME]);
        m_int_configs[CONFIG_RESPAWN_RESTARTQUIETTIME] = 3;
    }
    m_float_configs[CONFIG_RESPAWN_DYNAMICRATE_CREATURE] = sConfigMgr->GetFloatDefault("Respawn.DynamicRateCreature", 10.0f);
    if (m_float_configs[CONFIG_RESPAWN_DYNAMICRATE_CREATURE] < 0.0f)
    {
        TC_LOG_ERROR("server.loading", "Respawn.DynamicRateCreature ({}) must be positive. Set to 10.", m_float_configs[CONFIG_RESPAWN_DYNAMICRATE_CREATURE]);
        m_float_configs[CONFIG_RESPAWN_DYNAMICRATE_CREATURE] = 10.0f;
    }
    m_int_configs[CONFIG_RESPAWN_DYNAMICMINIMUM_CREATURE] = sConfigMgr->GetIntDefault("Respawn.DynamicMinimumCreature", 10);
    m_float_configs[CONFIG_RESPAWN_DYNAMICRATE_GAMEOBJECT] = sConfigMgr->GetFloatDefault("Respawn.DynamicRateGameObject", 10.0f);
    if (m_float_configs[CONFIG_RESPAWN_DYNAMICRATE_GAMEOBJECT] < 0.0f)
    {
        TC_LOG_ERROR("server.loading", "Respawn.DynamicRateGameObject ({}) must be positive. Set to 10.", m_float_configs[CONFIG_RESPAWN_DYNAMICRATE_GAMEOBJECT]);
        m_float_configs[CONFIG_RESPAWN_DYNAMICRATE_GAMEOBJECT] = 10.0f;
    }
    m_int_configs[CONFIG_RESPAWN_DYNAMICMINIMUM_GAMEOBJECT] = sConfigMgr->GetIntDefault("Respawn.DynamicMinimumGameObject", 10);
    _guidWarningMsg = sConfigMgr->GetStringDefault("Respawn.WarningMessage", "There will be an unscheduled server restart at 03:00. The server will be available again shortly after.");
    _alertRestartReason = sConfigMgr->GetStringDefault("Respawn.AlertRestartReason", "Urgent Maintenance");
    m_int_configs[CONFIG_RESPAWN_GUIDWARNING_FREQUENCY] = sConfigMgr->GetIntDefault("Respawn.WarningFrequency", 1800);
    ///- Read the "Data" directory from the config file
    std::string dataPath = sConfigMgr->GetStringDefault("DataDir", "./");
    if (dataPath.empty() || (dataPath.at(dataPath.length()-1) != '/' && dataPath.at(dataPath.length()-1) != '\\'))
        dataPath.push_back('/');

#if TRINITY_PLATFORM == TRINITY_PLATFORM_UNIX || TRINITY_PLATFORM == TRINITY_PLATFORM_APPLE
    if (dataPath[0] == '~')
    {
        char const* home = getenv("HOME");
        if (home)
            dataPath.replace(0, 1, home);
    }
#endif

    if (reload)
    {
        if (dataPath != m_dataPath)
            TC_LOG_ERROR("server.loading", "DataDir option can't be changed at worldserver.conf reload, using current value ({}).", m_dataPath);
    }
    else
    {
        m_dataPath = dataPath;
        TC_LOG_INFO("server.loading", "Using DataDir {}", m_dataPath);
    }

    m_bool_configs[CONFIG_ENABLE_MMAPS] = sConfigMgr->GetBoolDefault("mmap.enablePathFinding", true);
    TC_LOG_INFO("server.loading", "WORLD: MMap data directory is: {}mmaps", m_dataPath);

    m_bool_configs[CONFIG_VMAP_INDOOR_CHECK] = sConfigMgr->GetBoolDefault("vmap.enableIndoorCheck", false);
    bool enableIndoor = sConfigMgr->GetBoolDefault("vmap.enableIndoorCheck", true);
    bool enableLOS = sConfigMgr->GetBoolDefault("vmap.enableLOS", true);
    bool enableHeight = sConfigMgr->GetBoolDefault("vmap.enableHeight", true);

    if (!enableHeight)
        TC_LOG_ERROR("server.loading", "VMap height checking disabled! Creatures movements and other various things WILL be broken! Expect no support.");

    VMAP::VMapFactory::createOrGetVMapManager()->setEnableLineOfSightCalc(enableLOS);
    VMAP::VMapFactory::createOrGetVMapManager()->setEnableHeightCalc(enableHeight);
    TC_LOG_INFO("server.loading", "VMap support included. LineOfSight: {}, getHeight: {}, indoorCheck: {}", enableLOS, enableHeight, enableIndoor);
    TC_LOG_INFO("server.loading", "VMap data directory is: {}vmaps", m_dataPath);

    m_int_configs[CONFIG_MAX_WHO] = sConfigMgr->GetIntDefault("MaxWhoListReturns", 49);
    m_bool_configs[CONFIG_START_ALL_SPELLS] = sConfigMgr->GetBoolDefault("PlayerStart.AllSpells", false);
    m_int_configs[CONFIG_HONOR_AFTER_DUEL] = sConfigMgr->GetIntDefault("HonorPointsAfterDuel", 0);
    m_bool_configs[CONFIG_RESET_DUEL_COOLDOWNS] = sConfigMgr->GetBoolDefault("ResetDuelCooldowns", false);
    m_bool_configs[CONFIG_RESET_DUEL_HEALTH_MANA] = sConfigMgr->GetBoolDefault("ResetDuelHealthMana", false);
    m_bool_configs[CONFIG_START_ALL_EXPLORED] = sConfigMgr->GetBoolDefault("PlayerStart.MapsExplored", false);
    m_bool_configs[CONFIG_START_ALL_REP] = sConfigMgr->GetBoolDefault("PlayerStart.AllReputation", false);
    m_bool_configs[CONFIG_ALWAYS_MAXSKILL] = sConfigMgr->GetBoolDefault("AlwaysMaxWeaponSkill", false);
    m_bool_configs[CONFIG_PVP_TOKEN_ENABLE] = sConfigMgr->GetBoolDefault("PvPToken.Enable", false);
    m_int_configs[CONFIG_PVP_TOKEN_MAP_TYPE] = sConfigMgr->GetIntDefault("PvPToken.MapAllowType", 4);
    m_int_configs[CONFIG_PVP_TOKEN_ID] = sConfigMgr->GetIntDefault("PvPToken.ItemID", 29434);
    m_int_configs[CONFIG_PVP_TOKEN_COUNT] = sConfigMgr->GetIntDefault("PvPToken.ItemCount", 1);
    if (m_int_configs[CONFIG_PVP_TOKEN_COUNT] < 1)
        m_int_configs[CONFIG_PVP_TOKEN_COUNT] = 1;

    m_bool_configs[CONFIG_ALLOW_TRACK_BOTH_RESOURCES] = sConfigMgr->GetBoolDefault("AllowTrackBothResources", false);
    m_bool_configs[CONFIG_NO_RESET_TALENT_COST] = sConfigMgr->GetBoolDefault("NoResetTalentsCost", false);
    m_bool_configs[CONFIG_SHOW_KICK_IN_WORLD] = sConfigMgr->GetBoolDefault("ShowKickInWorld", false);
    m_bool_configs[CONFIG_SHOW_MUTE_IN_WORLD] = sConfigMgr->GetBoolDefault("ShowMuteInWorld", false);
    m_bool_configs[CONFIG_SHOW_BAN_IN_WORLD] = sConfigMgr->GetBoolDefault("ShowBanInWorld", false);
    m_int_configs[CONFIG_NUMTHREADS] = sConfigMgr->GetIntDefault("MapUpdate.Threads", 1);
    m_int_configs[CONFIG_MAX_RESULTS_LOOKUP_COMMANDS] = sConfigMgr->GetIntDefault("Command.LookupMaxResults", 0);

    // Warden
    m_bool_configs[CONFIG_WARDEN_ENABLED]              = sConfigMgr->GetBoolDefault("Warden.Enabled", false);
    m_int_configs[CONFIG_WARDEN_NUM_INJECT_CHECKS]     = sConfigMgr->GetIntDefault("Warden.NumInjectionChecks", 9);
    m_int_configs[CONFIG_WARDEN_NUM_LUA_CHECKS]        = sConfigMgr->GetIntDefault("Warden.NumLuaSandboxChecks", 1);
    m_int_configs[CONFIG_WARDEN_NUM_CLIENT_MOD_CHECKS] = sConfigMgr->GetIntDefault("Warden.NumClientModChecks", 1);
    m_int_configs[CONFIG_WARDEN_CLIENT_BAN_DURATION]   = sConfigMgr->GetIntDefault("Warden.BanDuration", 86400);
    m_int_configs[CONFIG_WARDEN_CLIENT_CHECK_HOLDOFF]  = sConfigMgr->GetIntDefault("Warden.ClientCheckHoldOff", 30);
    m_int_configs[CONFIG_WARDEN_CLIENT_FAIL_ACTION]    = sConfigMgr->GetIntDefault("Warden.ClientCheckFailAction", 0);
    m_int_configs[CONFIG_WARDEN_CLIENT_RESPONSE_DELAY] = sConfigMgr->GetIntDefault("Warden.ClientResponseDelay", 600);

    // Dungeon finder
    m_int_configs[CONFIG_LFG_OPTIONSMASK] = sConfigMgr->GetIntDefault("DungeonFinder.OptionsMask", 1);

    // DBC_ItemAttributes
    m_bool_configs[CONFIG_DBC_ENFORCE_ITEM_ATTRIBUTES] = sConfigMgr->GetBoolDefault("DBC.EnforceItemAttributes", true);

    // Accountpassword Secruity
    m_int_configs[CONFIG_ACC_PASSCHANGESEC] = sConfigMgr->GetIntDefault("Account.PasswordChangeSecurity", 0);

    // Random Battleground Rewards
    m_int_configs[CONFIG_BG_REWARD_WINNER_HONOR_FIRST] = sConfigMgr->GetIntDefault("Battleground.RewardWinnerHonorFirst", 30);
    m_int_configs[CONFIG_BG_REWARD_WINNER_ARENA_FIRST] = sConfigMgr->GetIntDefault("Battleground.RewardWinnerArenaFirst", 25);
    m_int_configs[CONFIG_BG_REWARD_WINNER_HONOR_LAST]  = sConfigMgr->GetIntDefault("Battleground.RewardWinnerHonorLast", 15);
    m_int_configs[CONFIG_BG_REWARD_WINNER_ARENA_LAST]  = sConfigMgr->GetIntDefault("Battleground.RewardWinnerArenaLast", 0);
    m_int_configs[CONFIG_BG_REWARD_LOSER_HONOR_FIRST]  = sConfigMgr->GetIntDefault("Battleground.RewardLoserHonorFirst", 5);
    m_int_configs[CONFIG_BG_REWARD_LOSER_HONOR_LAST]   = sConfigMgr->GetIntDefault("Battleground.RewardLoserHonorLast", 5);

    // Max instances per hour
    m_int_configs[CONFIG_MAX_INSTANCES_PER_HOUR] = sConfigMgr->GetIntDefault("AccountInstancesPerHour", 5);

    // Anounce reset of instance to whole party
    m_bool_configs[CONFIG_INSTANCES_RESET_ANNOUNCE] = sConfigMgr->GetBoolDefault("InstancesResetAnnounce", false);

    // AutoBroadcast
    m_bool_configs[CONFIG_AUTOBROADCAST] = sConfigMgr->GetBoolDefault("AutoBroadcast.On", false);
    m_int_configs[CONFIG_AUTOBROADCAST_CENTER] = sConfigMgr->GetIntDefault("AutoBroadcast.Center", 0);
    m_int_configs[CONFIG_AUTOBROADCAST_INTERVAL] = sConfigMgr->GetIntDefault("AutoBroadcast.Timer", 60000);
    if (reload)
    {
        m_timers[WUPDATE_AUTOBROADCAST].SetInterval(m_int_configs[CONFIG_AUTOBROADCAST_INTERVAL]);
        m_timers[WUPDATE_AUTOBROADCAST].Reset();
    }

    // MySQL ping time interval
    m_int_configs[CONFIG_DB_PING_INTERVAL] = sConfigMgr->GetIntDefault("MaxPingTime", 30);

    // misc
    m_bool_configs[CONFIG_PDUMP_NO_PATHS] = sConfigMgr->GetBoolDefault("PlayerDump.DisallowPaths", true);
    m_bool_configs[CONFIG_PDUMP_NO_OVERWRITE] = sConfigMgr->GetBoolDefault("PlayerDump.DisallowOverwrite", true);

    // Wintergrasp battlefield
    m_bool_configs[CONFIG_WINTERGRASP_ENABLE] = sConfigMgr->GetBoolDefault("Wintergrasp.Enable", false);
    m_int_configs[CONFIG_WINTERGRASP_PLR_MAX] = sConfigMgr->GetIntDefault("Wintergrasp.PlayerMax", 100);
    m_int_configs[CONFIG_WINTERGRASP_PLR_MIN] = sConfigMgr->GetIntDefault("Wintergrasp.PlayerMin", 0);
    m_int_configs[CONFIG_WINTERGRASP_PLR_MIN_LVL] = sConfigMgr->GetIntDefault("Wintergrasp.PlayerMinLvl", 77);
    m_int_configs[CONFIG_WINTERGRASP_BATTLETIME] = sConfigMgr->GetIntDefault("Wintergrasp.BattleTimer", 30);
    m_int_configs[CONFIG_WINTERGRASP_NOBATTLETIME] = sConfigMgr->GetIntDefault("Wintergrasp.NoBattleTimer", 150);
    m_int_configs[CONFIG_WINTERGRASP_RESTART_AFTER_CRASH] = sConfigMgr->GetIntDefault("Wintergrasp.CrashRestartTimer", 10);

    // Stats limits
    m_bool_configs[CONFIG_STATS_LIMITS_ENABLE] = sConfigMgr->GetBoolDefault("Stats.Limits.Enable", false);
    m_float_configs[CONFIG_STATS_LIMITS_DODGE] = sConfigMgr->GetFloatDefault("Stats.Limits.Dodge", 95.0f);
    m_float_configs[CONFIG_STATS_LIMITS_PARRY] = sConfigMgr->GetFloatDefault("Stats.Limits.Parry", 95.0f);
    m_float_configs[CONFIG_STATS_LIMITS_BLOCK] = sConfigMgr->GetFloatDefault("Stats.Limits.Block", 95.0f);
    m_float_configs[CONFIG_STATS_LIMITS_CRIT] = sConfigMgr->GetFloatDefault("Stats.Limits.Crit", 95.0f);

    //packet spoof punishment
    m_int_configs[CONFIG_PACKET_SPOOF_POLICY] = sConfigMgr->GetIntDefault("PacketSpoof.Policy", (uint32)WorldSession::DosProtection::POLICY_KICK);
    m_int_configs[CONFIG_PACKET_SPOOF_BANMODE] = sConfigMgr->GetIntDefault("PacketSpoof.BanMode", (uint32)BAN_ACCOUNT);
    if (m_int_configs[CONFIG_PACKET_SPOOF_BANMODE] == BAN_CHARACTER || m_int_configs[CONFIG_PACKET_SPOOF_BANMODE] > BAN_IP)
        m_int_configs[CONFIG_PACKET_SPOOF_BANMODE] = BAN_ACCOUNT;

    m_int_configs[CONFIG_PACKET_SPOOF_BANDURATION] = sConfigMgr->GetIntDefault("PacketSpoof.BanDuration", 86400);

    m_int_configs[CONFIG_BIRTHDAY_TIME] = sConfigMgr->GetIntDefault("BirthdayTime", 1222964635);

    m_bool_configs[CONFIG_IP_BASED_ACTION_LOGGING] = sConfigMgr->GetBoolDefault("Allow.IP.Based.Action.Logging", false);

    // AHBot
    m_int_configs[CONFIG_AHBOT_UPDATE_INTERVAL] = sConfigMgr->GetIntDefault("AuctionHouseBot.Update.Interval", 20);

    m_bool_configs[CONFIG_CALCULATE_CREATURE_ZONE_AREA_DATA] = sConfigMgr->GetBoolDefault("Calculate.Creature.Zone.Area.Data", false);
    m_bool_configs[CONFIG_CALCULATE_GAMEOBJECT_ZONE_AREA_DATA] = sConfigMgr->GetBoolDefault("Calculate.Gameoject.Zone.Area.Data", false);

    // HotSwap
    m_bool_configs[CONFIG_HOTSWAP_ENABLED] = sConfigMgr->GetBoolDefault("HotSwap.Enabled", true);
    m_bool_configs[CONFIG_HOTSWAP_RECOMPILER_ENABLED] = sConfigMgr->GetBoolDefault("HotSwap.EnableReCompiler", true);
    m_bool_configs[CONFIG_HOTSWAP_EARLY_TERMINATION_ENABLED] = sConfigMgr->GetBoolDefault("HotSwap.EnableEarlyTermination", true);
    m_bool_configs[CONFIG_HOTSWAP_BUILD_FILE_RECREATION_ENABLED] = sConfigMgr->GetBoolDefault("HotSwap.EnableBuildFileRecreation", true);
    m_bool_configs[CONFIG_HOTSWAP_INSTALL_ENABLED] = sConfigMgr->GetBoolDefault("HotSwap.EnableInstall", true);
    m_bool_configs[CONFIG_HOTSWAP_PREFIX_CORRECTION_ENABLED] = sConfigMgr->GetBoolDefault("HotSwap.EnablePrefixCorrection", true);

    // prevent character rename on character customization
    m_bool_configs[CONFIG_PREVENT_RENAME_CUSTOMIZATION] = sConfigMgr->GetBoolDefault("PreventRenameCharacterOnCustomization", false);

    // Allow 5-man parties to use raid warnings
    m_bool_configs[CONFIG_CHAT_PARTY_RAID_WARNINGS] = sConfigMgr->GetBoolDefault("PartyRaidWarnings", false);

    // Allow to cache data queries
    m_bool_configs[CONFIG_CACHE_DATA_QUERIES] = sConfigMgr->GetBoolDefault("CacheDataQueries", true);

    // Whether to use LoS from game objects
    m_bool_configs[CONFIG_CHECK_GOBJECT_LOS] = sConfigMgr->GetBoolDefault("CheckGameObjectLoS", true);

    // Anti movement cheat measure. Time each client have to acknowledge a movement change until they are kicked
    m_int_configs[CONFIG_PENDING_MOVE_CHANGES_TIMEOUT] = sConfigMgr->GetIntDefault("AntiCheat.PendingMoveChangesTimeoutTime", 0);

    // Specifies if IP addresses can be logged to the database
    m_bool_configs[CONFIG_ALLOW_LOGGING_IP_ADDRESSES_IN_DATABASE] = sConfigMgr->GetBoolDefault("AllowLoggingIPAddressesInDatabase", true, true);

    std::string debugSpellIdsStr = sConfigMgr->GetStringDefault("Debug.spell.ids", "");
    debugSpellIds.clear();
    std::vector<std::string> tokens;
    boost::split(tokens, debugSpellIdsStr, boost::is_any_of(","), boost::token_compress_off);
    for (auto& token : tokens)
    {
        boost::algorithm::trim(token); // 去掉首尾空格
        if (!token.empty())
        {
            try
            {
                uint32 id = static_cast<uint32>(std::stoul(token));
                debugSpellIds.push_back(id);
            }
            catch (...)
            {
                TC_LOG_ERROR("spells", "Invalid spell id in Debug.spell.ids: '{}'", token);
            }
        }
    }

    // call ScriptMgr if we're reloading the configuration
    if (reload)
        sScriptMgr->OnConfigLoad(reload);
}

/// Initialize the World
// === 世界初始化设置 ===
// 职责：执行游戏世界的完整初始化流程，加载所有必要的游戏数据
// 调用时机：在 worldserver 启动时，由 main() 函数调用
// 主要步骤：
//   1. 配置和随机数初始化
//   2. DBC 数据加载（法术、技能、物品等游戏数据）
//   3. 数据库数据加载（任务、生物、游戏对象等）
//   4. 脚本系统初始化
// 注意：此函数包含 100+ 个加载步骤，启动时间通常需要数十秒
void World::SetInitialWorldSettings()
{
    // 设置 Realm ID（用于日志记录）
    if (uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0)) // 0 reserved for auth
        sLog->SetRealmId(realmId);

    ///- Server startup begin - 记录启动开始时间
    uint32 startupBegin = getMSTime();

    ///- 初始化随机数生成器（用于游戏中的随机事件）
    srand((unsigned int)GameTime::GetGameTime());

    ///- 初始化 Detour 内存管理（寻路系统的内存分配器）
    dtAllocSetCustom(dtCustomAlloc, dtCustomFree);

    ///- 初始化 VMapManager 函数指针（解决游戏/碰撞检测的循环依赖）
    VMAP::VMapManager2* vmmgr2 = VMAP::VMapFactory::createOrGetVMapManager();
    vmmgr2->GetLiquidFlagsPtr = &GetLiquidFlags;
    vmmgr2->IsVMAPDisabledForPtr = &DisableMgr::IsVMAPDisabledFor;

    ///- 加载配置设置（从 worldserver.conf）
    LoadConfigSettings();

    ///- 加载允许的安全等级（GM 权限等级）
    LoadDBAllowedSecurityLevel();

    ///- 在任何表加载之前初始化最高 GUID，防止某些代码使用未初始化的 GUID
    sObjectMgr->SetHighestGuids();

    ///- 检查所有种族出生点地图文件是否存在（关键启动区域）
    if (!MapManager::ExistMapAndVMap(0, -6240.32f, 331.033f)      // 人类出生点（艾尔文森林）
        || !MapManager::ExistMapAndVMap(0, -8949.95f, -132.493f)  // 兽人出生点（杜隆塔尔）
        || !MapManager::ExistMapAndVMap(1, -618.518f, -4251.67f)  // 暗夜精灵出生点（泰达希尔）
        || !MapManager::ExistMapAndVMap(0, 1676.35f, 1677.45f)    // 矮人出生点（丹莫罗）
        || !MapManager::ExistMapAndVMap(1, 10311.3f, 832.463f)    // 牛头人出生点（莫高雷）
        || !MapManager::ExistMapAndVMap(1, -2917.58f, -257.98f)   // 亡灵出生点（提瑞斯法）
        || (m_int_configs[CONFIG_EXPANSION] && (                   // 资料片出生点（如果启用）
            !MapManager::ExistMapAndVMap(530, 10349.6f, -6357.29f) ||  // 血精灵出生点（永歌森林）
            !MapManager::ExistMapAndVMap(530, -3961.64f, -13931.2f))))  // 德莱尼出生点（秘蓝岛）
    {
        TC_LOG_FATAL("server.loading", "Unable to load critical files - server shutting down !!!");
        exit(1);
    }

    ///- 初始化对象池管理器（用于生成点、矿点等刷新池）
    sPoolMgr->Initialize();

    ///- 初始化游戏事件管理器（节日事件、世界事件等）
    sGameEventMgr->Initialize();

    ///- 加载 Trinity 字符串（错误消息、系统消息等）
    ///  如果没有记录，服务器必须停止，因为无法输出错误消息
    TC_LOG_INFO("server.loading", "Loading Trinity strings...");
    if (!sObjectMgr->LoadTrinityStrings())
        exit(1);                                            // 错误消息已在函数中显示

    ///- 更新数据库中的 Realm 条目（服务器类型和时区）
    ///  无 SQL 注入风险，因为值被当作整数处理
    // 不发送自定义类型 REALM_FFA_PVP 到 Realm 列表
    uint32 server_type = IsFFAPvPRealm() ? uint32(REALM_TYPE_PVP) : getIntConfig(CONFIG_GAME_TYPE);
    uint32 realm_zone = getIntConfig(CONFIG_REALM_ZONE);

    LoginDatabase.PExecute("UPDATE realmlist SET icon = {}, timezone = {} WHERE id = '{}'", server_type, realm_zone, realm.Id.Realm);      // 一次性查询

    ///- 加载 DBC 文件（客户端数据文件：法术、技能、物品等）
    TC_LOG_INFO("server.loading", "Initialize data stores...");
    LoadDBCStores(m_dataPath);
    DetectDBCLang();

    // 加载过场动画摄像机数据
    LoadM2Cameras(m_dataPath);

    // 加载 IP 地理位置数据库（用于显示玩家地理位置）
    sIPLocation->Load();

    // 初始化 VMap 和 MMap（碰撞检测和寻路地图）
    std::vector<uint32> mapIds;
    for (uint32 mapId = 0; mapId < sMapStore.GetNumRows(); mapId++)
        if (sMapStore.LookupEntry(mapId))
            mapIds.push_back(mapId);

    vmmgr2->InitializeThreadUnsafe(mapIds);  // 初始化 VMap（视线碰撞）

    MMAP::MMapManager* mmmgr = MMAP::MMapFactory::createOrGetMMapManager();
    mmmgr->InitializeThreadUnsafe(mapIds);  // 初始化 MMap（寻路网格）

    TC_LOG_INFO("server.loading", "Initializing PlayerDump tables...");
    PlayerDump::InitializeTables();  // 初始化角色导出表

    ///- 初始化静态辅助结构（AI 注册表）
    AIRegistry::Initialize();

    // ========== 法术系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading SpellInfo store...");
    sSpellMgr->LoadSpellInfoStore();  // 加载法术信息存储（从 DBC）

    TC_LOG_INFO("server.loading", "Loading SpellInfo corrections...");
    sSpellMgr->LoadSpellInfoCorrections();  // 加载法术修正数据（数据库覆盖）

    TC_LOG_INFO("server.loading", "Loading SkillLineAbilityMultiMap Data...");
    sSpellMgr->LoadSkillLineAbilityMap();  // 加载技能线能力映射（技能与法术的关联）

    TC_LOG_INFO("server.loading", "Loading SpellInfo custom attributes...");
    sSpellMgr->LoadSpellInfoCustomAttributes();  // 加载自定义法术属性

    TC_LOG_INFO("server.loading", "Loading SpellInfo diminishing infos...");
    sSpellMgr->LoadSpellInfoDiminishing();  // 加载递减效果信息（PVP 递减）

    TC_LOG_INFO("server.loading", "Loading SpellInfo immunity infos...");
    sSpellMgr->LoadSpellInfoImmunities();  // 加载免疫信息

    TC_LOG_INFO("server.loading", "Loading Player Totem models...");
    sObjectMgr->LoadPlayerTotemModels();  // 加载玩家图腾模型

    TC_LOG_INFO("server.loading", "Loading GameObject models...");
    LoadGameObjectModelList(m_dataPath);  // 加载游戏对象模型（碰撞检测用）

    // ========== 脚本和实例系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading Script Names...");
    sObjectMgr->LoadScriptNames();  // 加载脚本名称

    TC_LOG_INFO("server.loading", "Loading Instance Template...");
    sObjectMgr->LoadInstanceTemplate();  // 加载副本模板

    // 必须在加载重生数据之前调用
    TC_LOG_INFO("server.loading", "Loading instances...");
    sInstanceSaveMgr->LoadInstances();  // 加载副本保存数据

    // 必须在公会和竞技场队伍之前加载
    TC_LOG_INFO("server.loading", "Loading character cache store...");
    sCharacterCache->LoadCharacterCacheStorage();  // 加载角色缓存（快速查询）

    TC_LOG_INFO("server.loading", "Loading Broadcast texts...");
    sObjectMgr->LoadBroadcastTexts();  // 加载广播文本
    sObjectMgr->LoadBroadcastTextLocales();  // 加载广播文本本地化

    // ========== 本地化字符串加载 ==========
    TC_LOG_INFO("server.loading", "Loading Localization strings...");
    uint32 oldMSTime = getMSTime();
    sObjectMgr->LoadCreatureLocales();  // 加载生物本地化名称
    sObjectMgr->LoadGameObjectLocales();  // 加载游戏对象本地化名称
    sObjectMgr->LoadItemLocales();  // 加载物品本地化名称
    sObjectMgr->LoadItemSetNameLocales();  // 加载套装名称本地化
    sObjectMgr->LoadQuestLocales();  // 加载任务本地化
    sObjectMgr->LoadQuestOfferRewardLocale();  // 加载任务奖励本地化
    sObjectMgr->LoadQuestRequestItemsLocale();  // 加载任务需求物品本地化
    sObjectMgr->LoadNpcTextLocales();  // 加载 NPC 文本本地化
    sObjectMgr->LoadPageTextLocales();  // 加载书本页面本地化
    sObjectMgr->LoadGossipMenuItemsLocales();  // 加载菜单项本地化
    sObjectMgr->LoadPointOfInterestLocales();  // 加载兴趣点本地化
    sObjectMgr->LoadQuestGreetingLocales();  // 加载任务问候语本地化

    sObjectMgr->SetDBCLocaleIndex(GetDefaultDbcLocale());        // 获取 DBC 语言的区域索引（控制台/广播）
    TC_LOG_INFO("server.loading", ">> Localization strings loaded in {} ms", GetMSTimeDiffToNow(oldMSTime));

    // ========== 账号和权限系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading Account Roles and Permissions...");
    sAccountMgr->LoadRBAC();  // 加载基于角色的访问控制（RBAC）

    // ========== 页面文本和游戏对象加载 ==========
    TC_LOG_INFO("server.loading", "Loading Page Texts...");
    sObjectMgr->LoadPageTexts();  // 加载书本页面文本

    TC_LOG_INFO("server.loading", "Loading Game Object Templates...");         // 必须在 LoadPageTexts 之后
    sObjectMgr->LoadGameObjectTemplate();  // 加载游戏对象模板（箱子、门、矿点等）

    TC_LOG_INFO("server.loading", "Loading Game Object template addons...");
    sObjectMgr->LoadGameObjectTemplateAddons();  // 加载游戏对象模板附加数据

    TC_LOG_INFO("server.loading", "Loading Transport templates...");
    sTransportMgr->LoadTransportTemplates();  // 加载交通工具模板（船只、飞艇等）

    TC_LOG_INFO("server.loading", "Loading Transport animations and rotations...");
    sTransportMgr->LoadTransportAnimationAndRotation();  // 加载交通工具动画和旋转

    // ========== 法术系统详细加载 ==========
    TC_LOG_INFO("server.loading", "Loading Spell Rank Data...");
    sSpellMgr->LoadSpellRanks();  // 加载法术等级数据

    TC_LOG_INFO("server.loading", "Loading Spell Required Data...");
    sSpellMgr->LoadSpellRequired();  // 加载法术需求（前置法术）

    TC_LOG_INFO("server.loading", "Loading Spell Group types...");
    sSpellMgr->LoadSpellGroups();  // 加载法术分组（同名法术不同等级）

    TC_LOG_INFO("server.loading", "Loading Spell Learn Skills...");
    sSpellMgr->LoadSpellLearnSkills();                           // 必须在 LoadSpellRanks 之后

    TC_LOG_INFO("server.loading", "Loading SpellInfo SpellSpecific and AuraState...");
    sSpellMgr->LoadSpellInfoSpellSpecificAndAuraState();         // 必须在 LoadSpellRanks 之后

    TC_LOG_INFO("server.loading", "Loading Spell Learn Spells...");
    sSpellMgr->LoadSpellLearnSpells();  // 加载法术学习关联（学习时自动学会的法术）

    TC_LOG_INFO("server.loading", "Loading Spell Proc conditions and data...");
    sSpellMgr->LoadSpellProcs();  // 加载触发法术条件和数据

    TC_LOG_INFO("server.loading", "Loading Spell Bonus Data...");
    sSpellMgr->LoadSpellBonuses();  // 加载法术奖励数据（法术强度加成）

    TC_LOG_INFO("server.loading", "Loading Aggro Spells Definitions...");
    sSpellMgr->LoadSpellThreats();  // 加载产生威胁的法术定义

    TC_LOG_INFO("server.loading", "Loading Spell Group Stack Rules...");
    sSpellMgr->LoadSpellGroupStackRules();  // 加载法术组叠加规则

    // ========== NPC 文本和物品系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading NPC Texts...");
    sObjectMgr->LoadGossipText();  // 加载 NPC 对话文本

    TC_LOG_INFO("server.loading", "Loading Enchant Spells Proc datas...");
    sSpellMgr->LoadSpellEnchantProcData();  // 加载附魔触发法术数据

    TC_LOG_INFO("server.loading", "Loading Item Random Enchantments Table...");
    LoadRandomEnchantmentsTable();  // 加载物品随机附魔表

    TC_LOG_INFO("server.loading", "Loading Disables");                         // 必须在加载任务和物品之前
    DisableMgr::LoadDisables();  // 加载禁用项（禁用的任务、物品、法术等）

    TC_LOG_INFO("server.loading", "Loading Items...");                         // 必须在 LoadRandomEnchantmentsTable 和 LoadPageTexts 之后
    sObjectMgr->LoadItemTemplates();  // 加载物品模板（所有物品数据）

    TC_LOG_INFO("server.loading", "Loading Item set names...");                // 必须在 LoadItemPrototypes 之后
    sObjectMgr->LoadItemSetNames();  // 加载套装名称

    // ========== 生物系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading Creature Model Based Info Data...");
    sObjectMgr->LoadCreatureModelInfo();  // 加载生物模型信息（血量、伤害等）

    TC_LOG_INFO("server.loading", "Loading Creature templates...");
    sObjectMgr->LoadCreatureTemplates();  // 加载生物模板（所有生物定义）

    TC_LOG_INFO("server.loading", "Loading Equipment templates...");           // 必须在 LoadCreatureTemplates 之后
    sObjectMgr->LoadEquipmentTemplates();  // 加载装备模板（生物装备）

    TC_LOG_INFO("server.loading", "Loading Creature template addons...");
    sObjectMgr->LoadCreatureTemplateAddons();  // 加载生物模板附加数据

    // ========== 声望系统和兴趣点加载 ==========
    TC_LOG_INFO("server.loading", "Loading Reputation Reward Rates...");
    sObjectMgr->LoadReputationRewardRate();  // 加载声望奖励率

    TC_LOG_INFO("server.loading", "Loading Creature Reputation OnKill Data...");
    sObjectMgr->LoadReputationOnKill();  // 加载击杀生物获得的声望

    TC_LOG_INFO("server.loading", "Loading Reputation Spillover Data...");
    sObjectMgr->LoadReputationSpilloverTemplate();  // 加载声望溢出模板（阵营声望关联）

    TC_LOG_INFO("server.loading", "Loading Points Of Interest Data...");
    sObjectMgr->LoadPointsOfInterest();  // 加载兴趣点（地图标记）

    TC_LOG_INFO("server.loading", "Loading Creature Base Stats...");
    sObjectMgr->LoadCreatureClassLevelStats();  // 加载生物基础属性（职业等级属性）

    TC_LOG_INFO("server.loading", "Loading Spawn Group Templates...");
    sObjectMgr->LoadSpawnGroupTemplates();  // 加载生成组模板

    // ========== 生物生成数据加载 ==========
    TC_LOG_INFO("server.loading", "Loading Creature Data...");
    sObjectMgr->LoadCreatures();  // 加载生物生成数据（所有地图中的生物）

    TC_LOG_INFO("server.loading", "Loading Temporary Summon Data...");
    sObjectMgr->LoadTempSummons();                               // 必须在 LoadCreatureTemplates() 和 LoadGameObjectTemplates() 之后

    TC_LOG_INFO("server.loading", "Loading pet levelup spells...");
    sSpellMgr->LoadPetLevelupSpellMap();  // 加载宠物升级法术

    TC_LOG_INFO("server.loading", "Loading pet default spells additional to levelup spells...");
    sSpellMgr->LoadPetDefaultSpells();  // 加载宠物默认法术

    TC_LOG_INFO("server.loading", "Loading Creature Addon Data...");
    sObjectMgr->LoadCreatureAddons();                            // 必须在 LoadCreatureTemplates() 和 LoadCreatures() 之后

    TC_LOG_INFO("server.loading", "Loading Creature Movement Overrides...");
    sObjectMgr->LoadCreatureMovementOverrides();                 // 必须在 LoadCreatures() 之后

    // ========== 游戏对象生成数据加载 ==========
    TC_LOG_INFO("server.loading", "Loading Gameobject Data...");
    sObjectMgr->LoadGameObjects();  // 加载游戏对象生成数据（所有地图中的对象）

    TC_LOG_INFO("server.loading", "Loading Spawn Group Data...");
    sObjectMgr->LoadSpawnGroups();  // 加载生成组数据

    TC_LOG_INFO("server.loading", "Loading instance spawn groups...");
    sObjectMgr->LoadInstanceSpawnGroups();  // 加载副本生成组

    TC_LOG_INFO("server.loading", "Loading GameObject Addon Data...");
    sObjectMgr->LoadGameObjectAddons();                          // 必须在 LoadGameObjects() 之后

    TC_LOG_INFO("server.loading", "Loading GameObject faction and flags overrides...");
    sObjectMgr->LoadGameObjectOverrides();                       // 必须在 LoadGameObjects() 之后

    TC_LOG_INFO("server.loading", "Loading GameObject Quest Items...");
    sObjectMgr->LoadGameObjectQuestItems();  // 加载游戏对象任务物品

    TC_LOG_INFO("server.loading", "Loading Creature Quest Items...");
    sObjectMgr->LoadCreatureQuestItems();  // 加载生物任务物品

    TC_LOG_INFO("server.loading", "Loading Creature Linked Respawn...");
    sObjectMgr->LoadLinkedRespawn();                             // 必须在 LoadCreatures(), LoadGameObjects() 之后

    // ========== 天气和任务系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading Weather Data...");
    WeatherMgr::LoadWeatherData();  // 加载天气数据

    TC_LOG_INFO("server.loading", "Loading Quests...");
    sObjectMgr->LoadQuests();                                    // 必须在 DBCs、creature_template、item_template、gameobject 表之后加载

    TC_LOG_INFO("server.loading", "Checking Quest Disables");
    DisableMgr::CheckQuestDisables();                           // 必须在加载任务之后

    TC_LOG_INFO("server.loading", "Loading Quest POI");
    sObjectMgr->LoadQuestPOI();  // 加载任务兴趣点（地图标记）

    TC_LOG_INFO("server.loading", "Loading Quests Starters and Enders...");
    sObjectMgr->LoadQuestStartersAndEnders();                    // 必须在任务加载之后

    TC_LOG_INFO("server.loading", "Loading Quests Greetings...");
    sObjectMgr->LoadQuestGreetings();                           // 必须在 creature_template、gameobject_template 表之后加载

    // ========== 对象池和游戏事件系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading Objects Pooling Data...");
    sPoolMgr->LoadFromDB();  // 加载对象池数据（刷新池）
    TC_LOG_INFO("server.loading", "Loading Quest Pooling Data...");
    sQuestPoolMgr->LoadFromDB();                                // 必须在任务模板之后

    TC_LOG_INFO("server.loading", "Loading Game Event Data...");               // 必须在完全加载池之后
    sGameEventMgr->LoadHolidayDates();                           // 必须在加载 DBC 之后
    sGameEventMgr->LoadFromDB();                                 // 必须在加载节日日期之后

    // ========== 车辆系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading UNIT_NPC_FLAG_SPELLCLICK Data..."); // 必须在 LoadQuests 之后
    sObjectMgr->LoadNPCSpellClickSpells();  // 加载 NPC 点击法术数据

    TC_LOG_INFO("server.loading", "Loading Vehicle Templates...");
    sObjectMgr->LoadVehicleTemplate();                          // 必须在 LoadCreatureTemplates() 之后

    TC_LOG_INFO("server.loading", "Loading Vehicle Template Accessories...");
    sObjectMgr->LoadVehicleTemplateAccessories();                // 必须在 LoadCreatureTemplates() 和 LoadNPCSpellClickSpells() 之后

    TC_LOG_INFO("server.loading", "Loading Vehicle Accessories...");
    sObjectMgr->LoadVehicleAccessories();                       // 必须在 LoadCreatureTemplates() 和 LoadNPCSpellClickSpells() 之后

    TC_LOG_INFO("server.loading", "Loading Vehicle Seat Addon Data...");
    sObjectMgr->LoadVehicleSeatAddon();                         // 必须在加载 DBC 之后

    // ========== 区域触发和副本系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading SpellArea Data...");                // 必须在任务加载之后
    sSpellMgr->LoadSpellAreas();  // 加载法术区域数据

    TC_LOG_INFO("server.loading", "Loading Area Trigger Teleports definitions...");
    sObjectMgr->LoadAreaTriggerTeleports();  // 加载区域触发传送定义

    TC_LOG_INFO("server.loading", "Loading Access Requirements...");
    sObjectMgr->LoadAccessRequirements();                        // 必须在物品模板加载之后

    TC_LOG_INFO("server.loading", "Loading Quest Area Triggers...");
    sObjectMgr->LoadQuestAreaTriggers();                         // 必须在 LoadQuests 之后

    TC_LOG_INFO("server.loading", "Loading Tavern Area Triggers...");
    sObjectMgr->LoadTavernAreaTriggers();  // 加载旅店区域触发（休息区）

    TC_LOG_INFO("server.loading", "Loading AreaTrigger script names...");
    sObjectMgr->LoadAreaTriggerScripts();  // 加载区域触发脚本名称

    TC_LOG_INFO("server.loading", "Loading LFG entrance positions..."); // 必须在区域触发之后
    sLFGMgr->LoadLFGDungeons();  // 加载随机副本入口位置

    TC_LOG_INFO("server.loading", "Loading Dungeon boss data...");
    sObjectMgr->LoadInstanceEncounters();  // 加载副本 BOSS 数据

    TC_LOG_INFO("server.loading", "Loading LFG rewards...");
    sLFGMgr->LoadRewards();  // 加载随机副本奖励

    TC_LOG_INFO("server.loading", "Loading Graveyard-zone links...");
    sObjectMgr->LoadGraveyardZones();  // 加载墓地-区域关联（死亡复活点）

    // ========== 法术附加数据加载 ==========
    TC_LOG_INFO("server.loading", "Loading spell pet auras...");
    sSpellMgr->LoadSpellPetAuras();  // 加载法术宠物光环

    TC_LOG_INFO("server.loading", "Loading Spell target coordinates...");
    sSpellMgr->LoadSpellTargetPositions();  // 加载法术目标坐标（传送法术等）

    TC_LOG_INFO("server.loading", "Loading enchant custom attributes...");
    sSpellMgr->LoadEnchantCustomAttr();  // 加载附魔自定义属性

    TC_LOG_INFO("server.loading", "Loading linked spells...");
    sSpellMgr->LoadSpellLinked();  // 加载关联法术（触发链）

    // ========== 玩家和宠物数据加载 ==========
    TC_LOG_INFO("server.loading", "Loading Player Create Data...");
    sObjectMgr->LoadPlayerInfo();  // 加载玩家创建数据（初始装备、技能等）

    TC_LOG_INFO("server.loading", "Loading Exploration BaseXP Data...");
    sObjectMgr->LoadExplorationBaseXP();  // 加载探索基础经验值

    TC_LOG_INFO("server.loading", "Loading Pet Name Parts...");
    sObjectMgr->LoadPetNames();  // 加载宠物名称部件

    CharacterDatabaseCleaner::CleanDatabase();  // 清理角色数据库（删除过期数据）

    TC_LOG_INFO("server.loading", "Loading the max pet number...");
    sObjectMgr->LoadPetNumber();  // 加载最大宠物编号

    TC_LOG_INFO("server.loading", "Loading pet level stats...");
    sObjectMgr->LoadPetLevelInfo();  // 加载宠物等级属性

    TC_LOG_INFO("server.loading", "Loading Player level dependent mail rewards...");
    sObjectMgr->LoadMailLevelRewards();  // 加载玩家等级邮件奖励

    // ========== 掉落表和技能系统加载 ==========
    LoadLootTables();  // 加载所有掉落表（生物、物品、钓鱼等）

    TC_LOG_INFO("server.loading", "Loading Skill Discovery Table...");
    LoadSkillDiscoveryTable();  // 加载技能发现表（技能揭示配方）

    TC_LOG_INFO("server.loading", "Loading Skill Extra Item Table...");
    LoadSkillExtraItemTable();  // 加载技能额外物品表（制造额外物品）

    TC_LOG_INFO("server.loading", "Loading Skill Perfection Data Table...");
    LoadSkillPerfectItemTable();  // 加载技能完美数据表（完美制造）

    TC_LOG_INFO("server.loading", "Loading Skill Fishing base level requirements...");
    sObjectMgr->LoadFishingBaseSkillLevel();  // 加载钓鱼技能基础等级需求

    // ========== 成就系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading Achievements...");
    sAchievementMgr->LoadAchievementReferenceList();  // 加载成就引用列表
    TC_LOG_INFO("server.loading", "Loading Achievement Criteria Lists...");
    sAchievementMgr->LoadAchievementCriteriaList();  // 加载成就标准列表
    TC_LOG_INFO("server.loading", "Loading Achievement Criteria Data...");
    sAchievementMgr->LoadAchievementCriteriaData();  // 加载成就标准数据
    TC_LOG_INFO("server.loading", "Loading Achievement Rewards...");
    sAchievementMgr->LoadRewards();  // 加载成就奖励
    TC_LOG_INFO("server.loading", "Loading Achievement Reward Locales...");
    sAchievementMgr->LoadRewardLocales();  // 加载成就奖励本地化
    TC_LOG_INFO("server.loading", "Loading Completed Achievements...");
    sAchievementMgr->LoadCompletedAchievements();  // 加载已完成成就

    ///- 从数据库加载动态数据表
    TC_LOG_INFO("server.loading", "Loading Item Auctions...");
    sAuctionMgr->LoadAuctionItems();  // 加载拍卖物品

    TC_LOG_INFO("server.loading", "Loading Auctions...");
    sAuctionMgr->LoadAuctions();  // 加载拍卖行数据

    TC_LOG_INFO("server.loading", "Loading Guilds...");
    sGuildMgr->LoadGuilds();  // 加载公会数据

    TC_LOG_INFO("server.loading", "Loading ArenaTeams...");
    sArenaTeamMgr->LoadArenaTeams();  // 加载竞技场队伍

    TC_LOG_INFO("server.loading", "Loading Groups...");
    sGroupMgr->LoadGroups();  // 加载队伍数据

    TC_LOG_INFO("server.loading", "Loading ReservedNames...");
    sObjectMgr->LoadReservedPlayersNames();  // 加载保留的角色名称

    TC_LOG_INFO("server.loading", "Loading GameObjects for quests...");
    sObjectMgr->LoadGameObjectForQuests();  // 加载任务相关的游戏对象

    TC_LOG_INFO("server.loading", "Loading BattleMasters...");
    sBattlegroundMgr->LoadBattleMastersEntry();                 // 必须在加载 CreatureTemplate 之后

    TC_LOG_INFO("server.loading", "Loading GameTeleports...");
    sObjectMgr->LoadGameTele();  // 加载游戏传送点（GM 传送）

    TC_LOG_INFO("server.loading", "Loading Trainers...");       // 必须在 LoadCreatureTemplates 之后
    sObjectMgr->LoadTrainers();  // 加载训练师数据

    TC_LOG_INFO("server.loading", "Loading Creature default trainers...");
    sObjectMgr->LoadCreatureDefaultTrainers();  // 加载生物默认训练师

    TC_LOG_INFO("server.loading", "Loading Gossip menu...");
    sObjectMgr->LoadGossipMenu();  // 加载对话菜单

    TC_LOG_INFO("server.loading", "Loading Gossip menu options...");
    sObjectMgr->LoadGossipMenuItems();                           // 必须在 LoadTrainers 之后

    TC_LOG_INFO("server.loading", "Loading Vendors...");
    sObjectMgr->LoadVendors();                                   // 必须在加载 CreatureTemplate 和 ItemTemplate 之后

    // ========== 移动和阵型系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading Waypoints...");
    sWaypointMgr->Load();  // 加载路径点

    TC_LOG_INFO("server.loading", "Loading SmartAI Waypoints...");
    sSmartWaypointMgr->LoadFromDB();  // 加载 SmartAI 路径点

    TC_LOG_INFO("server.loading", "Loading Creature Formations...");
    sFormationMgr->LoadCreatureFormations();  // 加载生物阵型

    // ========== 世界状态和条件系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading World States...");              // 必须在战场、户外 PVP 和条件之前加载
    LoadWorldStates();  // 加载世界状态

    TC_LOG_INFO("server.loading", "Loading Conditions...");
    sConditionMgr->LoadConditions();  // 加载条件系统

    // ========== 阵营转换系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading faction change achievement pairs...");
    sObjectMgr->LoadFactionChangeAchievements();  // 加载阵营转换成就配对

    TC_LOG_INFO("server.loading", "Loading faction change spell pairs...");
    sObjectMgr->LoadFactionChangeSpells();  // 加载阵营转换法术配对

    TC_LOG_INFO("server.loading", "Loading faction change quest pairs...");
    sObjectMgr->LoadFactionChangeQuests();  // 加载阵营转换任务配对

    TC_LOG_INFO("server.loading", "Loading faction change item pairs...");
    sObjectMgr->LoadFactionChangeItems();  // 加载阵营转换物品配对

    TC_LOG_INFO("server.loading", "Loading faction change reputation pairs...");
    sObjectMgr->LoadFactionChangeReputations();  // 加载阵营转换声望配对

    TC_LOG_INFO("server.loading", "Loading faction change title pairs...");
    sObjectMgr->LoadFactionChangeTitles();  // 加载阵营转换头衔配对

    // ========== GM 工具和插件系统加载 ==========
    TC_LOG_INFO("server.loading", "Loading GM tickets...");
    sTicketMgr->LoadTickets();  // 加载 GM 工单

    TC_LOG_INFO("server.loading", "Loading GM surveys...");
    sTicketMgr->LoadSurveys();  // 加载 GM 调查问卷

    TC_LOG_INFO("server.loading", "Loading client addons...");
    AddonMgr::LoadFromDB();  // 加载客户端插件数据

    ///- 处理过期邮件（删除/退回）
    TC_LOG_INFO("server.loading", "Returning old mails...");
    sObjectMgr->ReturnOrDeleteOldMails(false);  // 退回或删除过期邮件

    TC_LOG_INFO("server.loading", "Loading Autobroadcasts...");
    LoadAutobroadcasts();  // 加载自动广播消息

    ///- 加载并初始化脚本系统
    sObjectMgr->LoadSpellScripts();                              // 必须在加载 Creature/Gameobject(Template/Data) 之后
    sObjectMgr->LoadEventScripts();                              // 必须在加载 Creature/Gameobject(Template/Data) 之后
    sObjectMgr->LoadWaypointScripts();  // 加载路径点脚本

    TC_LOG_INFO("server.loading", "Loading spell script names...");
    sObjectMgr->LoadSpellScriptNames();  // 加载法术脚本名称

    TC_LOG_INFO("server.loading", "Loading Creature Texts...");
    sCreatureTextMgr->LoadCreatureTexts();  // 加载生物文本

    TC_LOG_INFO("server.loading", "Loading Creature Text Locales...");
    sCreatureTextMgr->LoadCreatureTextLocales();  // 加载生物文本本地化

    TC_LOG_INFO("server.loading", "Initializing Scripts...");
    sScriptMgr->Initialize();  // 初始化脚本管理器
    sScriptMgr->OnConfigLoad(false);                                // 必须在 ScriptMgr 正确初始化后执行

    TC_LOG_INFO("server.loading", "Validating spell scripts...");
    sObjectMgr->ValidateSpellScripts();  // 验证法术脚本

    TC_LOG_INFO("server.loading", "Loading SmartAI scripts...");
    sSmartScriptMgr->LoadSmartAIFromDB();  // 加载 SmartAI 脚本

    TC_LOG_INFO("server.loading", "Loading Calendar data...");
    sCalendarMgr->LoadFromDB();  // 加载日历数据

    TC_LOG_INFO("server.loading", "Loading Petitions...");
    sPetitionMgr->LoadPetitions();  // 加载请愿书（公会创建申请）

    TC_LOG_INFO("server.loading", "Loading Signatures...");
    sPetitionMgr->LoadSignatures();  // 加载签名

    TC_LOG_INFO("server.loading", "Loading Item loot...");
    sLootItemStorage->LoadStorageFromDB();  // 加载物品掉落存储

    TC_LOG_INFO("server.loading", "Initialize query data...");
    sObjectMgr->InitializeQueriesData(QUERY_DATA_ALL);  // 初始化查询数据（预编译查询）

    TC_LOG_INFO("server.loading", "Initialize commands...");
    Trinity::ChatCommands::LoadCommandMap();  // 初始化聊天命令映射

    ///- 初始化游戏时间和定时器
    TC_LOG_INFO("server.loading", "Initialize game time and timers");
    GameTime::UpdateGameTimers();  // 更新游戏计时器

    // 记录服务器启动时间到数据库
    LoginDatabase.PExecute("INSERT INTO uptime (realmid, starttime, uptime, revision) VALUES({}, {}, 0, '{}')",
                            realm.Id.Realm, uint32(GameTime::GetStartTime()), GitRevision::GetFullVersion());       // 一次性查询

    // ========== 初始化世界定时器 ==========
    // 拍卖行更新定时器（每 1 分钟）
    m_timers[WUPDATE_AUCTIONS].SetInterval(MINUTE*IN_MILLISECONDS);
    // 拍卖行待处理更新定时器（每 250 毫秒）
    m_timers[WUPDATE_AUCTIONS_PENDING].SetInterval(250);
    // 运行时间更新定时器（根据配置，默认每 10 分钟）
    m_timers[WUPDATE_UPTIME].SetInterval(m_int_configs[CONFIG_UPTIME_UPDATE]*MINUTE*IN_MILLISECONDS);
                                                            // 根据配置项更新 "uptime" 表（分钟）
    // 尸体清理定时器（每 20 分钟）
    m_timers[WUPDATE_CORPSES].SetInterval(20 * MINUTE * IN_MILLISECONDS);
                                                            // 每 20 分钟清理尸体
    // 数据库清理定时器（根据配置，默认每 14 天）
    m_timers[WUPDATE_CLEANDB].SetInterval(m_int_configs[CONFIG_LOGDB_CLEARINTERVAL]*MINUTE*IN_MILLISECONDS);
                                                            // 默认每 14 天清理日志表
    // 自动广播定时器（根据配置间隔）
    m_timers[WUPDATE_AUTOBROADCAST].SetInterval(getIntConfig(CONFIG_AUTOBROADCAST_INTERVAL));
    // 角色删除检查定时器（每天）
    m_timers[WUPDATE_DELETECHARS].SetInterval(DAY*IN_MILLISECONDS); // 每天检查待删除角色

    // 拍卖行机器人定时器（根据配置，默认每 20 秒）
    m_timers[WUPDATE_AHBOT].SetInterval(getIntConfig(CONFIG_AHBOT_UPDATE_INTERVAL) * IN_MILLISECONDS); // 每 20 秒

    // 数据库心跳定时器（根据配置，默认每 30 分钟）
    m_timers[WUPDATE_PINGDB].SetInterval(getIntConfig(CONFIG_DB_PING_INTERVAL)*MINUTE*IN_MILLISECONDS);    // MySQL 心跳时间（分钟）

    // 文件变更检查定时器（每 500 毫秒）
    m_timers[WUPDATE_CHECK_FILECHANGES].SetInterval(500);

    // 在线列表缓存更新定时器（每 5 秒）
    m_timers[WUPDATE_WHO_LIST].SetInterval(5 * IN_MILLISECONDS); // 每 5 秒更新在线列表缓存

    // 自定义频道保存定时器（根据配置）
    m_timers[WUPDATE_CHANNEL_SAVE].SetInterval(getIntConfig(CONFIG_PRESERVE_CUSTOM_CHANNEL_INTERVAL) * MINUTE * IN_MILLISECONDS);

    // 设置邮件定时器，每天凌晨 4-5 点之间退回邮件
    // 邮件定时器在更新拍卖行时增加
    // 一秒等于 1000 毫秒（在 Windows 系统上测试）
    /// @todo 移除魔法数字
    tm localTm;
    time_t gameTime = GameTime::GetGameTime();
    localtime_r(&gameTime, &localTm);
    uint8 CleanOldMailsTime = getIntConfig(CONFIG_CLEAN_OLD_MAIL_TIME);
    mail_timer = ((((localTm.tm_hour + (24 - CleanOldMailsTime)) % 24)* HOUR * IN_MILLISECONDS) / m_timers[WUPDATE_AUCTIONS].GetInterval());
                                                            // 1440
    mail_timer_expires = ((DAY * IN_MILLISECONDS) / (m_timers[WUPDATE_AUCTIONS].GetInterval()));
    TC_LOG_INFO("server.loading", "Mail timer set to: {}, mail return is called every {} minutes", uint64(mail_timer), uint64(mail_timer_expires));

    ///- 初始化地图管理器
    TC_LOG_INFO("server.loading", "Starting Map System");
    sMapMgr->Initialize();  // 初始化地图系统

    TC_LOG_INFO("server.loading", "Starting Game Event system...");
    uint32 nextGameEvent = sGameEventMgr->StartSystem();  // 启动游戏事件系统
    m_timers[WUPDATE_EVENTS].SetInterval(nextGameEvent);    // 依赖下一个事件

    // 删除 X 天前已删除的角色
    Player::DeleteOldCharacters();

    TC_LOG_INFO("server.loading", "Initialize AuctionHouseBot...");
    sAuctionBot->Initialize();  // 初始化拍卖行机器人

    TC_LOG_INFO("server.loading", "Initializing chat channels...");
    ChannelMgr::LoadFromDB();  // 从数据库加载聊天频道

    TC_LOG_INFO("server.loading", "Initializing Opcodes...");
    opcodeTable.Initialize();  // 初始化操作码表（网络包处理）

    TC_LOG_INFO("server.loading", "Starting Arena Season...");
    sGameEventMgr->StartArenaSeason();  // 启动竞技场赛季

    sTicketMgr->Initialize();  // 初始化 GM 工单管理器

    ///- 初始化战场系统
    TC_LOG_INFO("server.loading", "Starting Battleground System");
    sBattlegroundMgr->LoadBattlegroundTemplates();  // 加载战场模板
    sBattlegroundMgr->InitAutomaticArenaPointDistribution();  // 初始化自动竞技场点数分配

    ///- 初始化户外 PVP
    TC_LOG_INFO("server.loading", "Starting Outdoor PvP System");
    sOutdoorPvPMgr->InitOutdoorPvP();  // 初始化户外 PVP 系统

    ///- 初始化战场
    TC_LOG_INFO("server.loading", "Starting Battlefield System");
    sBattlefieldMgr->InitBattlefield();  // 初始化战场系统（冬拥湖等）

    TC_LOG_INFO("server.loading", "Loading Transports...");
    sTransportMgr->SpawnContinentTransports();  // 生成大陆交通工具（船只、飞艇）

    ///- 初始化 Warden 反作弊系统
    TC_LOG_INFO("server.loading", "Loading Warden Checks...");
    sWardenCheckMgr->LoadWardenChecks();  // 加载 Warden 检查项

    TC_LOG_INFO("server.loading", "Loading Warden Action Overrides...");
    sWardenCheckMgr->LoadWardenOverrides();  // 加载 Warden 动作覆盖

    TC_LOG_INFO("server.loading", "Deleting expired bans...");
    LoginDatabase.Execute("DELETE FROM ip_banned WHERE unbandate <= UNIX_TIMESTAMP() AND unbandate<>bandate");      // 一次性查询，删除过期封禁

    TC_LOG_INFO("server.loading", "Initializing quest reset times...");
    InitQuestResetTimes();  // 初始化任务重置时间
    CheckQuestResetTimes();  // 检查任务重置时间

    TC_LOG_INFO("server.loading", "Calculate random battleground reset time...");
    InitRandomBGResetTime();  // 计算随机战场重置时间

    TC_LOG_INFO("server.loading", "Calculate deletion of old calendar events time...");
    InitCalendarOldEventsDeletionTime();  // 计算删除旧日历事件的时间

    TC_LOG_INFO("server.loading", "Calculate guild limitation(s) reset time...");
    InitGuildResetTime();  // 计算公会限制重置时间

    // 如果配置要求，预加载基础地图的所有网格
    if (sWorld->getBoolConfig(CONFIG_BASEMAP_LOAD_GRIDS))
    {
        sMapMgr->DoForAllMaps([](Map* map)
        {
            if (!map->Instanceable())  // 只处理非副本地图
            {
                TC_LOG_INFO("server.loading", "Pre-loading base map data for map {}", map->GetId());
                map->LoadAllCells();  // 预加载所有单元格（提高性能但消耗内存）
            }
        });
    }

    // 计算并输出启动耗时
    uint32 startupDuration = GetMSTimeDiffToNow(startupBegin);

    TC_LOG_INFO("server.worldserver", "World initialized in {} minutes {} seconds", (startupDuration / 60000), ((startupDuration % 60000) / 1000));

    TC_METRIC_EVENT("events", "World initialized", "World initialized in " + std::to_string(startupDuration / 60000) + " minutes " + std::to_string((startupDuration % 60000) / 1000) + " seconds");
}
// === World::SetInitialWorldSettings() 函数结束 ===

void World::DetectDBCLang()
{
    uint8 m_lang_confid = sConfigMgr->GetIntDefault("DBC.Locale", 255);

    if (m_lang_confid != 255 && m_lang_confid >= TOTAL_LOCALES)
    {
        TC_LOG_ERROR("server.loading", "Incorrect DBC.Locale! Must be >= 0 and < {} (set to 0)", TOTAL_LOCALES);
        m_lang_confid = LOCALE_enUS;
    }

    ChrRacesEntry const* race = sChrRacesStore.AssertEntry(1);

    std::string availableLocalsStr;

    uint8 default_locale = TOTAL_LOCALES;
    for (uint8 i = LOCALE_enUS; i < TOTAL_LOCALES; ++i)
    {
        if (race->Name[i][0] != '\0')                     // check by race names
        {
            // Mark the first found locale as default locale
            if (default_locale == TOTAL_LOCALES)
                default_locale = i;

            m_availableDbcLocaleMask |= (1 << i);
            availableLocalsStr += localeNames[i];
            availableLocalsStr += " ";
        }
    }

    if (m_availableDbcLocaleMask == 0)
    {
        TC_LOG_ERROR("server.loading", "Unable to determine your DBC Locale! (corrupt DBC?)");
        exit(1);
    }

    if (default_locale != m_lang_confid && m_lang_confid < TOTAL_LOCALES &&
        (m_availableDbcLocaleMask & (1 << m_lang_confid)))
    {
        default_locale = m_lang_confid;
    }

    m_defaultDbcLocale = LocaleConstant(default_locale);

    TC_LOG_INFO("server.loading", "Using {} DBC Locale as default. All available DBC locales: {}", localeNames[m_defaultDbcLocale], availableLocalsStr.empty() ? "<none>" : availableLocalsStr);
}

void World::LoadAutobroadcasts()
{
    uint32 oldMSTime = getMSTime();

    m_Autobroadcasts.clear();
    m_AutobroadcastsWeights.clear();

    uint32 realmId = sConfigMgr->GetIntDefault("RealmID", 0);
    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_AUTOBROADCAST);
    stmt->setInt32(0, realmId);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 autobroadcasts definitions. DB table `autobroadcast` is empty for this realm!");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        uint8 id = fields[0].GetUInt8();

        m_Autobroadcasts[id] = fields[2].GetString();
        m_AutobroadcastsWeights[id] = fields[1].GetUInt8();

        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} autobroadcast definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/// Update the World !
// === 世界更新主循环 ===
// 职责：驱动整个游戏世界的更新，是主循环的核心入口
// 参数：diff - 距离上一帧的时间差（毫秒）
// 调用时机：在 WorldUpdateLoop() 中每帧调用一次，默认 50ms (20 FPS)
// 性能注意：此函数是性能关键路径，应避免阻塞操作
void World::Update(uint32 diff)
{
    TC_METRIC_TIMER("world_update_time_total");

    ///- 更新游戏时间并检查关闭时间
    _UpdateGameTime();
    time_t currentGameTime = GameTime::GetGameTime();

    sWorldUpdateTime.UpdateWithDiff(diff);

    ///- 更新所有定时器（拍卖行、邮件、尸体清理等）
    for (int i = 0; i < WUPDATE_COUNT; ++i)
    {
        if (m_timers[i].GetCurrent() >= 0)
            m_timers[i].Update(diff);  // 减少计时器
        else
            m_timers[i].SetCurrent(0);  // 重置为 0
    }

    ///- 更新在线玩家列表（每 5 秒）
    if (m_timers[WUPDATE_WHO_LIST].Passed())
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update who list"));
        m_timers[WUPDATE_WHO_LIST].Reset();  // 重置定时器
        sWhoListStorageMgr->Update();         // 更新在线列表
    }

    if (IsStopped() || m_timers[WUPDATE_CHANNEL_SAVE].Passed())
    {
        m_timers[WUPDATE_CHANNEL_SAVE].Reset();

        if (sWorld->getBoolConfig(CONFIG_PRESERVE_CUSTOM_CHANNELS))
        {
            TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Save custom channels"));
            ChannelMgr* mgr1 = ASSERT_NOTNULL(ChannelMgr::forTeam(ALLIANCE));
            mgr1->SaveToDB();
            ChannelMgr* mgr2 = ASSERT_NOTNULL(ChannelMgr::forTeam(HORDE));
            if (mgr1 != mgr2)
                mgr2->SaveToDB();
        }
    }

    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Check quest reset times"));
        CheckQuestResetTimes();
    }

    if (currentGameTime > m_NextRandomBGReset)
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Reset random BG"));
        ResetRandomBG();
    }

    if (currentGameTime > m_NextCalendarOldEventsDeletionTime)
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Delete old calendar events"));
        CalendarDeleteOldEvents();
    }

    if (currentGameTime > m_NextGuildReset)
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Reset guild cap"));
        ResetGuildCap();
    }

    ///- 处理拍卖行（每 1 分钟）
    if (m_timers[WUPDATE_AUCTIONS].Passed())
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update expired auctions"));
        m_timers[WUPDATE_AUCTIONS].Reset();

        ///- 更新邮件（退回或删除过期邮件）
        if (++mail_timer > mail_timer_expires)
        {
            mail_timer = 0;
            sObjectMgr->ReturnOrDeleteOldMails(true);
        }

        ///- 处理过期拍卖
        sAuctionMgr->Update();
    }

    if (m_timers[WUPDATE_AUCTIONS_PENDING].Passed())
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update pending auctions"));
        m_timers[WUPDATE_AUCTIONS_PENDING].Reset();

        sAuctionMgr->UpdatePendingAuctions();
    }

    /// <li> Handle AHBot operations
    if (m_timers[WUPDATE_AHBOT].Passed())
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update AHBot"));
        sAuctionBot->Update();
        m_timers[WUPDATE_AHBOT].Reset();
    }

    /// <li> Handle file changes
    if (m_timers[WUPDATE_CHECK_FILECHANGES].Passed())
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update HotSwap"));
        sScriptReloadMgr->Update();
        m_timers[WUPDATE_CHECK_FILECHANGES].Reset();
    }

    {
        ///- 更新所有玩家会话（关键路径，每帧执行）
        ///  这是核心更新路径之一，处理所有在线玩家的数据包和状态更新
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update sessions"));
        UpdateSessions(diff);
    }

    /// <li> Update uptime table
    if (m_timers[WUPDATE_UPTIME].Passed())
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update uptime"));
        uint32 tmpDiff = GameTime::GetUptime();
        uint32 maxOnlinePlayers = GetMaxPlayerCount();

        m_timers[WUPDATE_UPTIME].Reset();

        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_UPTIME_PLAYERS);

        stmt->setUInt32(0, tmpDiff);
        stmt->setUInt16(1, uint16(maxOnlinePlayers));
        stmt->setUInt32(2, realm.Id.Realm);
        stmt->setUInt32(3, uint32(GameTime::GetStartTime()));

        LoginDatabase.Execute(stmt);
    }

    /// <li> Clean logs table
    if (sWorld->getIntConfig(CONFIG_LOGDB_CLEARTIME) > 0) // if not enabled, ignore the timer
    {
        if (m_timers[WUPDATE_CLEANDB].Passed())
        {
            TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Clean logs table"));
            m_timers[WUPDATE_CLEANDB].Reset();

            LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_OLD_LOGS);

            stmt->setUInt32(0, sWorld->getIntConfig(CONFIG_LOGDB_CLEARTIME));
            stmt->setUInt32(1, uint32(time(0)));
            stmt->setUInt32(2, realm.Id.Realm);

            LoginDatabase.Execute(stmt);
        }
    }

    ///- 更新所有地图（关键路径，并行执行）
    ///  这是核心更新路径之一，使用线程池并行更新所有地图
    ///  每个地图独立更新：生物、游戏对象、玩家、脚本等
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update maps"));
        sMapMgr->Update(diff);
    }

    if (sWorld->getBoolConfig(CONFIG_AUTOBROADCAST))
    {
        if (m_timers[WUPDATE_AUTOBROADCAST].Passed())
        {
            TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Send autobroadcast"));
            m_timers[WUPDATE_AUTOBROADCAST].Reset();
            SendAutoBroadcast();
        }
    }

    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update battlegrounds"));
        sBattlegroundMgr->Update(diff);
    }

    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update outdoor pvp"));
        sOutdoorPvPMgr->Update(diff);
    }

    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update battlefields"));
        sBattlefieldMgr->Update(diff);
    }

    ///- Delete all characters which have been deleted X days before
    if (m_timers[WUPDATE_DELETECHARS].Passed())
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Delete old characters"));
        m_timers[WUPDATE_DELETECHARS].Reset();
        Player::DeleteOldCharacters();
    }

    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update groups"));
        sGroupMgr->Update(diff);
    }

    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update LFG"));
        sLFGMgr->Update(diff);
    }

    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Process query callbacks"));
        // execute callbacks from sql queries that were queued recently
        ProcessQueryCallbacks();
    }

    ///- Erase corpses once every 20 minutes
    if (m_timers[WUPDATE_CORPSES].Passed())
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Remove old corpses"));
        m_timers[WUPDATE_CORPSES].Reset();
        sMapMgr->DoForAllMaps([](Map* map)
        {
            map->RemoveOldCorpses();
        });
    }

    ///- Process Game events when necessary
    if (m_timers[WUPDATE_EVENTS].Passed())
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update game events"));
        m_timers[WUPDATE_EVENTS].Reset();                   // to give time for Update() to be processed
        uint32 nextGameEvent = sGameEventMgr->Update();
        m_timers[WUPDATE_EVENTS].SetInterval(nextGameEvent);
        m_timers[WUPDATE_EVENTS].Reset();
    }

    ///- Ping to keep MySQL connections alive
    if (m_timers[WUPDATE_PINGDB].Passed())
    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Ping MySQL"));
        m_timers[WUPDATE_PINGDB].Reset();
        TC_LOG_DEBUG("misc", "Ping MySQL to keep connection alive");
        CharacterDatabase.KeepAlive();
        LoginDatabase.KeepAlive();
        WorldDatabase.KeepAlive();
    }

    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update instance reset times"));
        // update the instance reset times
        sInstanceSaveMgr->Update();
    }

    // Check for shutdown warning
    if (_guidWarn && !_guidAlert)
    {
        _warnDiff += diff;
        if (GameTime::GetGameTime() >= _warnShutdownTime)
            DoGuidWarningRestart();
        else if (_warnDiff > getIntConfig(CONFIG_RESPAWN_GUIDWARNING_FREQUENCY) * IN_MILLISECONDS)
            SendGuidWarning();
    }

    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Process cli commands"));
        // And last, but not least handle the issued cli commands
        ProcessCliCommands();
    }

    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update world scripts"));
        sScriptMgr->OnWorldUpdate(diff);
    }

    {
        TC_METRIC_TIMER("world_update_time", TC_METRIC_TAG("type", "Update metrics"));
        // Stats logger update
        sMetric->Update();
        TC_METRIC_VALUE("update_time_diff", diff);
    }
}

void World::ForceGameEventUpdate()
{
    m_timers[WUPDATE_EVENTS].Reset();                   // to give time for Update() to be processed
    uint32 nextGameEvent = sGameEventMgr->Update();
    m_timers[WUPDATE_EVENTS].SetInterval(nextGameEvent);
    m_timers[WUPDATE_EVENTS].Reset();
}

/// Send a packet to all players (except self if mentioned)
void World::SendGlobalMessage(WorldPacket const* packet, WorldSession* self, uint32 team)
{
    SessionMap::const_iterator itr;
    for (itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
    {
        if (itr->second &&
            itr->second->GetPlayer() &&
            itr->second->GetPlayer()->IsInWorld() &&
            itr->second != self &&
            (team == 0 || itr->second->GetPlayer()->GetTeam() == team))
        {
            itr->second->SendPacket(packet);
        }
    }
}

/// Send a packet to all GMs (except self if mentioned)
void World::SendGlobalGMMessage(WorldPacket const* packet, WorldSession* self, uint32 team)
{
    for (SessionMap::const_iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
    {
        // check if session and can receive global GM Messages and its not self
        WorldSession* session = itr->second;
        if (!session || session == self || !session->HasPermission(rbac::RBAC_PERM_RECEIVE_GLOBAL_GM_TEXTMESSAGE))
            continue;

        // Player should be in world
        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld())
            continue;

        // Send only to same team, if team is given
        if (!team || player->GetTeam() == team)
            session->SendPacket(packet);
    }
}

namespace Trinity
{
    class WorldWorldTextBuilder
    {
        public:
            typedef std::vector<WorldPacket*> WorldPacketList;
            explicit WorldWorldTextBuilder(uint32 textId, va_list* args = nullptr) : i_textId(textId), i_args(args) { }
            void operator()(WorldPacketList& data_list, LocaleConstant loc_idx)
            {
                char const* text = sObjectMgr->GetTrinityString(i_textId, loc_idx);

                if (i_args)
                {
                    // we need copy va_list before use or original va_list will corrupted
                    va_list ap;
                    va_copy(ap, *i_args);

                    char str[2048];
                    vsnprintf(str, 2048, text, ap);
                    va_end(ap);

                    do_helper(data_list, &str[0]);
                }
                else
                    do_helper(data_list, (char*)text);
            }
        private:
            char* lineFromMessage(char*& pos) { char* start = strtok(pos, "\n"); pos = nullptr; return start; }
            void do_helper(WorldPacketList& data_list, char* text)
            {
                char* pos = text;
                while (char* line = lineFromMessage(pos))
                {
                    WorldPacket* data = new WorldPacket();
                    ChatHandler::BuildChatPacket(*data, CHAT_MSG_SYSTEM, LANG_UNIVERSAL, nullptr, nullptr, line);
                    data_list.push_back(data);
                }
            }

            uint32 i_textId;
            va_list* i_args;
    };
}                                                           // namespace Trinity

/// Send a System Message to all players (except self if mentioned)
void World::SendWorldText(uint32 string_id, ...)
{
    va_list ap;
    va_start(ap, string_id);

    Trinity::WorldWorldTextBuilder wt_builder(string_id, &ap);
    Trinity::LocalizedPacketListDo<Trinity::WorldWorldTextBuilder> wt_do(wt_builder);
    for (SessionMap::const_iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
    {
        if (!itr->second || !itr->second->GetPlayer() || !itr->second->GetPlayer()->IsInWorld())
            continue;

        wt_do(itr->second->GetPlayer());
    }

    va_end(ap);
}

/// Send a System Message to all GMs (except self if mentioned)
void World::SendGMText(uint32 string_id, ...)
{
    va_list ap;
    va_start(ap, string_id);

    Trinity::WorldWorldTextBuilder wt_builder(string_id, &ap);
    Trinity::LocalizedPacketListDo<Trinity::WorldWorldTextBuilder> wt_do(wt_builder);
    for (SessionMap::const_iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
    {
        // Session should have permissions to receive global gm messages
        WorldSession* session = itr->second;
        if (!session || !session->HasPermission(rbac::RBAC_PERM_RECEIVE_GLOBAL_GM_TEXTMESSAGE))
            continue;

        // Player should be in world
        Player* player = session->GetPlayer();
        if (!player || !player->IsInWorld())
            continue;

        wt_do(player);
    }

    va_end(ap);
}

/// DEPRECATED, only for debug purpose. Send a System Message to all players (except self if mentioned)
void World::SendGlobalText(char const* text, WorldSession* self)
{
    WorldPacket data;

    // need copy to prevent corruption by strtok call in LineFromMessage original string
    char* buf = strdup(text);
    char* pos = buf;

    while (char* line = ChatHandler::LineFromMessage(pos))
    {
        ChatHandler::BuildChatPacket(data, CHAT_MSG_SYSTEM, LANG_UNIVERSAL, nullptr, nullptr, line);
        SendGlobalMessage(&data, self);
    }

    free(buf);
}

/// Kick (and save) all players
void World::KickAll()
{
    m_QueuedPlayer.clear();                                 // prevent send queue update packet and login queued sessions

    // session not removed at kick and will removed in next update tick
    for (SessionMap::const_iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        itr->second->KickPlayer("World::KickAll");
}

/// Kick (and save) all players with security level less `sec`
void World::KickAllLess(AccountTypes sec)
{
    // session not removed at kick and will removed in next update tick
    for (SessionMap::const_iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        if (itr->second->GetSecurity() < sec)
            itr->second->KickPlayer("World::KickAllLess");
}

/// Ban an account or ban an IP address, duration will be parsed using TimeStringToSecs if it is positive, otherwise permban
BanReturn World::BanAccount(BanMode mode, std::string const& nameOrIP, std::string const& duration, std::string const& reason, std::string const& author)
{
    uint32 duration_secs = TimeStringToSecs(duration);
    return BanAccount(mode, nameOrIP, duration_secs, reason, author);
}

/// Ban an account or ban an IP address, duration is in seconds if positive, otherwise permban
BanReturn World::BanAccount(BanMode mode, std::string const& nameOrIP, uint32 duration_secs, std::string const& reason, std::string const& author)
{
    PreparedQueryResult resultAccounts = PreparedQueryResult(nullptr); //used for kicking

    // Prevent banning an already banned account
    if (mode == BAN_ACCOUNT && AccountMgr::IsBannedAccount(nameOrIP))
        return BAN_EXISTS;

    ///- Update the database with ban information
    switch (mode)
    {
        case BAN_IP:
        {
            // No SQL injection with prepared statements
            LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_BY_IP);
            stmt->setString(0, nameOrIP);
            resultAccounts = LoginDatabase.Query(stmt);
            stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_IP_BANNED);
            stmt->setString(0, nameOrIP);
            stmt->setUInt32(1, duration_secs);
            stmt->setString(2, author);
            stmt->setString(3, reason);
            LoginDatabase.Execute(stmt);
            break;
        }
        case BAN_ACCOUNT:
        {
            // No SQL injection with prepared statements
            LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_ID_BY_NAME);
            stmt->setString(0, nameOrIP);
            resultAccounts = LoginDatabase.Query(stmt);
            break;
        }
        case BAN_CHARACTER:
        {
            // No SQL injection with prepared statements
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_ACCOUNT_BY_NAME);
            stmt->setString(0, nameOrIP);
            resultAccounts = CharacterDatabase.Query(stmt);
            break;
        }
        default:
            return BAN_SYNTAX_ERROR;
    }

    if (!resultAccounts)
    {
        if (mode == BAN_IP)
            return BAN_SUCCESS;                             // ip correctly banned but nobody affected (yet)
        else
            return BAN_NOTFOUND;                            // Nobody to ban
    }

    ///- Disconnect all affected players (for IP it can be several)
    LoginDatabaseTransaction trans = LoginDatabase.BeginTransaction();
    do
    {
        Field* fieldsAccount = resultAccounts->Fetch();
        uint32 account = fieldsAccount[0].GetUInt32();

        if (mode != BAN_IP)
        {
            // make sure there is only one active ban
            LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_ACCOUNT_NOT_BANNED);
            stmt->setUInt32(0, account);
            trans->Append(stmt);
            // No SQL injection with prepared statements
            stmt = LoginDatabase.GetPreparedStatement(LOGIN_INS_ACCOUNT_BANNED);
            stmt->setUInt32(0, account);
            stmt->setUInt32(1, duration_secs);
            stmt->setString(2, author);
            stmt->setString(3, reason);
            trans->Append(stmt);
        }

        if (WorldSession* sess = FindSession(account))
            if (std::string(sess->GetPlayerName()) != author)
                sess->KickPlayer("World::BanAccount Banning account");
    } while (resultAccounts->NextRow());

    LoginDatabase.CommitTransaction(trans);

    return BAN_SUCCESS;
}

/// Remove a ban from an account or IP address
bool World::RemoveBanAccount(BanMode mode, std::string const& nameOrIP)
{
    LoginDatabasePreparedStatement* stmt = nullptr;
    if (mode == BAN_IP)
    {
        stmt = LoginDatabase.GetPreparedStatement(LOGIN_DEL_IP_NOT_BANNED);
        stmt->setString(0, nameOrIP);
        LoginDatabase.Execute(stmt);
    }
    else
    {
        uint32 account = 0;
        if (mode == BAN_ACCOUNT)
            account = AccountMgr::GetId(nameOrIP);
        else if (mode == BAN_CHARACTER)
            account = sCharacterCache->GetCharacterAccountIdByName(nameOrIP);

        if (!account)
            return false;

        //NO SQL injection as account is uint32
        stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_ACCOUNT_NOT_BANNED);
        stmt->setUInt32(0, account);
        LoginDatabase.Execute(stmt);
    }
    return true;
}

/// Ban an account or ban an IP address, duration will be parsed using TimeStringToSecs if it is positive, otherwise permban
BanReturn World::BanCharacter(std::string const& name, std::string const& duration, std::string const& reason, std::string const& author)
{
    Player* banned = ObjectAccessor::FindConnectedPlayerByName(name);
    ObjectGuid::LowType guid = 0;

    uint32 duration_secs = TimeStringToSecs(duration);

    /// Pick a player to ban if not online
    if (!banned)
    {
        ObjectGuid fullGuid = sCharacterCache->GetCharacterGuidByName(name);
        if (fullGuid.IsEmpty())
            return BAN_NOTFOUND;                                    // Nobody to ban

        guid = fullGuid.GetCounter();
    }
    else
        guid = banned->GetGUID().GetCounter();
    //Use transaction in order to ensure the order of the queries
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    // make sure there is only one active ban
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_BAN);
    stmt->setUInt32(0, guid);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHARACTER_BAN);
    stmt->setUInt32(0, guid);
    stmt->setUInt32(1, duration_secs);
    stmt->setString(2, author);
    stmt->setString(3, reason);
    trans->Append(stmt);
    CharacterDatabase.CommitTransaction(trans);

    if (banned)
        banned->GetSession()->KickPlayer("World::BanCharacter Banning character");

    return BAN_SUCCESS;
}

/// Remove a ban from a character
bool World::RemoveBanCharacter(std::string const& name)
{
    Player* banned = ObjectAccessor::FindConnectedPlayerByName(name);
    ObjectGuid::LowType guid = 0;

    /// Pick a player to ban if not online
    if (!banned)
    {
        ObjectGuid fullGuid = sCharacterCache->GetCharacterGuidByName(name);
        if (fullGuid.IsEmpty())
            return false;

        guid = fullGuid.GetCounter();
    }
    else
        guid = banned->GetGUID().GetCounter();

    if (!guid)
        return false;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHARACTER_BAN);
    stmt->setUInt32(0, guid);
    CharacterDatabase.Execute(stmt);
    return true;
}

/// Update the game time
void World::_UpdateGameTime()
{
    ///- update the time
    time_t lastGameTime = GameTime::GetGameTime();
    GameTime::UpdateGameTimers();

    uint32 elapsed = uint32(GameTime::GetGameTime() - lastGameTime);

    ///- if there is a shutdown timer
    if (!IsStopped() && m_ShutdownTimer > 0 && elapsed > 0)
    {
        ///- ... and it is overdue, stop the world (set m_stopEvent)
        if (m_ShutdownTimer <= elapsed)
        {
            if (!(m_ShutdownMask & SHUTDOWN_MASK_IDLE) || GetActiveAndQueuedSessionCount() == 0)
                m_stopEvent = true;                         // exist code already set
            else
                m_ShutdownTimer = 1;                        // minimum timer value to wait idle state
        }
        ///- ... else decrease it and if necessary display a shutdown countdown to the users
        else
        {
            m_ShutdownTimer -= elapsed;

            ShutdownMsg();
        }
    }
}

/// Shutdown the server
void World::ShutdownServ(uint32 time, uint32 options, uint8 exitcode, const std::string& reason)
{
    // ignore if server shutdown at next tick
    if (IsStopped())
        return;

    m_ShutdownMask = options;
    m_ExitCode = exitcode;

    ///- If the shutdown time is 0, evaluate shutdown on next tick (no message)
    if (time == 0)
        m_ShutdownTimer = 1;
    ///- Else set the shutdown timer and warn users
    else
    {
        m_ShutdownTimer = time;
        ShutdownMsg(true, nullptr, reason);
    }

    sScriptMgr->OnShutdownInitiate(ShutdownExitCode(exitcode), ShutdownMask(options));
}

/// Display a shutdown message to the user(s)
void World::ShutdownMsg(bool show, Player* player, const std::string& reason)
{
    // not show messages for idle shutdown mode
    if (m_ShutdownMask & SHUTDOWN_MASK_IDLE)
        return;

    ///- Display a message every 12 hours, hours, 5 minutes, minute, 5 seconds and finally seconds
    if (show ||
        (m_ShutdownTimer < 5* MINUTE && (m_ShutdownTimer % 15) == 0) || // < 5 min; every 15 sec
        (m_ShutdownTimer < 15 * MINUTE && (m_ShutdownTimer % MINUTE) == 0) || // < 15 min ; every 1 min
        (m_ShutdownTimer < 30 * MINUTE && (m_ShutdownTimer % (5 * MINUTE)) == 0) || // < 30 min ; every 5 min
        (m_ShutdownTimer < 12 * HOUR && (m_ShutdownTimer % HOUR) == 0) || // < 12 h ; every 1 h
        (m_ShutdownTimer > 12 * HOUR && (m_ShutdownTimer % (12 * HOUR)) == 0)) // > 12 h ; every 12 h
    {
        std::string str = secsToTimeString(m_ShutdownTimer, TimeFormat::Numeric);
        if (!reason.empty())
            str += " - " + reason;

        ServerMessageType msgid = (m_ShutdownMask & SHUTDOWN_MASK_RESTART) ? SERVER_MSG_RESTART_TIME : SERVER_MSG_SHUTDOWN_TIME;

        SendServerMessage(msgid, str, player);
        TC_LOG_DEBUG("misc", "Server is {} in {}", (m_ShutdownMask & SHUTDOWN_MASK_RESTART ? "restart" : "shuttingdown"), str);
    }
}

/// Cancel a planned server shutdown
uint32 World::ShutdownCancel()
{
    // nothing cancel or too late
    if (!m_ShutdownTimer || m_stopEvent)
        return 0;

    ServerMessageType msgid = (m_ShutdownMask & SHUTDOWN_MASK_RESTART) ? SERVER_MSG_RESTART_CANCELLED : SERVER_MSG_SHUTDOWN_CANCELLED;

    uint32 oldTimer = m_ShutdownTimer;
    m_ShutdownMask = 0;
    m_ShutdownTimer = 0;
    m_ExitCode = SHUTDOWN_EXIT_CODE;                       // to default value
    SendServerMessage(msgid);

    TC_LOG_DEBUG("misc", "Server {} cancelled.", (m_ShutdownMask & SHUTDOWN_MASK_RESTART ? "restart" : "shutdown"));

    sScriptMgr->OnShutdownCancel();
    return oldTimer;
}

/// Send a server message to the user(s)
void World::SendServerMessage(ServerMessageType messageID, std::string stringParam /*= ""*/, Player* player /*= nullptr*/)
{
    WorldPackets::Chat::ChatServerMessage chatServerMessage;
    chatServerMessage.MessageID = int32(messageID);
    if (messageID <= SERVER_MSG_STRING)
        chatServerMessage.StringParam = stringParam;

    if (player)
        player->SendDirectMessage(chatServerMessage.Write());
    else
        SendGlobalMessage(chatServerMessage.Write());
}

void World::UpdateSessions(uint32 diff)
{
    {
        TC_METRIC_DETAILED_NO_THRESHOLD_TIMER("world_update_time",
            TC_METRIC_TAG("type", "Add sessions"),
            TC_METRIC_TAG("parent_type", "Update sessions"));
        ///- Add new sessions
        WorldSession* sess = nullptr;
        while (addSessQueue.next(sess))
            AddSession_(sess);
    }

    ///- Then send an update signal to remaining ones
    for (SessionMap::iterator itr = m_sessions.begin(), next; itr != m_sessions.end(); itr = next)
    {
        next = itr;
        ++next;

        ///- and remove not active sessions from the list
        WorldSession* pSession = itr->second;
        WorldSessionFilter updater(pSession);

        [[maybe_unused]] uint32 currentSessionId = itr->first;
        TC_METRIC_DETAILED_TIMER("world_update_sessions_time", TC_METRIC_TAG("account_id", std::to_string(currentSessionId)));

        if (!pSession->Update(diff, updater))    // As interval = 0
        {
            if (!RemoveQueuedPlayer(itr->second) && itr->second && getIntConfig(CONFIG_INTERVAL_DISCONNECT_TOLERANCE))
                m_disconnects[itr->second->GetAccountId()] = GameTime::GetGameTime();
            RemoveQueuedPlayer(pSession);
            m_sessions.erase(itr);
            delete pSession;

        }
    }
}

// This handles the issued and queued CLI commands
void World::ProcessCliCommands()
{
    CliCommandHolder::Print zprint = nullptr;
    void* callbackArg = nullptr;
    CliCommandHolder* command = nullptr;
    while (cliCmdQueue.next(command))
    {
        TC_LOG_INFO("misc", "CLI command under processing...");
        zprint = command->m_print;
        callbackArg = command->m_callbackArg;
        CliHandler handler(callbackArg, zprint);
        handler.ParseCommands(command->m_command);
        if (command->m_commandFinished)
            command->m_commandFinished(callbackArg, !handler.HasSentErrorMessage());
        delete command;
    }
}

void World::SendAutoBroadcast()
{
    if (m_Autobroadcasts.empty())
        return;

    uint32 weight = 0;
    AutobroadcastsWeightMap selectionWeights;
    std::string msg;

    for (AutobroadcastsWeightMap::const_iterator it = m_AutobroadcastsWeights.begin(); it != m_AutobroadcastsWeights.end(); ++it)
    {
        if (it->second)
        {
            weight += it->second;
            selectionWeights[it->first] = it->second;
        }
    }

    if (weight)
    {
        uint32 selectedWeight = urand(0, weight - 1);
        weight = 0;
        for (AutobroadcastsWeightMap::const_iterator it = selectionWeights.begin(); it != selectionWeights.end(); ++it)
        {
            weight += it->second;
            if (selectedWeight < weight)
            {
                msg = m_Autobroadcasts[it->first];
                break;
            }
        }
    }
    else
        msg = m_Autobroadcasts[urand(0, m_Autobroadcasts.size())];

    uint32 abcenter = sWorld->getIntConfig(CONFIG_AUTOBROADCAST_CENTER);

    if (abcenter == 0)
        sWorld->SendWorldText(LANG_AUTO_BROADCAST, msg.c_str());
    else if (abcenter == 1)
    {
        WorldPacket data(SMSG_NOTIFICATION, (msg.size()+1));
        data << msg;
        sWorld->SendGlobalMessage(&data);
    }
    else if (abcenter == 2)
    {
        sWorld->SendWorldText(LANG_AUTO_BROADCAST, msg.c_str());

        WorldPacket data(SMSG_NOTIFICATION, (msg.size()+1));
        data << msg;
        sWorld->SendGlobalMessage(&data);
    }

    TC_LOG_DEBUG("misc", "AutoBroadcast: '{}'", msg);
}

void World::UpdateRealmCharCount(uint32 accountId)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_COUNT);
    stmt->setUInt32(0, accountId);
    _queryProcessor.AddCallback(CharacterDatabase.AsyncQuery(stmt).WithPreparedCallback(std::bind(&World::_UpdateRealmCharCount, this, std::placeholders::_1)));
}

void World::_UpdateRealmCharCount(PreparedQueryResult resultCharCount)
{
    if (resultCharCount)
    {
        Field* fields = resultCharCount->Fetch();
        uint32 accountId = fields[0].GetUInt32();
        uint8 charCount = uint8(fields[1].GetUInt64());

        LoginDatabaseTransaction trans = LoginDatabase.BeginTransaction();

        LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_REP_REALM_CHARACTERS);
        stmt->setUInt8(0, charCount);
        stmt->setUInt32(1, accountId);
        stmt->setUInt32(2, realm.Id.Realm);
        trans->Append(stmt);

        LoginDatabase.CommitTransaction(trans);
    }
}

void World::InitQuestResetTimes()
{
    m_NextDailyQuestReset = sWorld->getWorldState(WS_DAILY_QUEST_RESET_TIME);
    m_NextWeeklyQuestReset = sWorld->getWorldState(WS_WEEKLY_QUEST_RESET_TIME);
    m_NextMonthlyQuestReset = sWorld->getWorldState(WS_MONTHLY_QUEST_RESET_TIME);
}

static time_t GetNextDailyResetTime(time_t t)
{
    return GetLocalHourTimestamp(t, sWorld->getIntConfig(CONFIG_DAILY_QUEST_RESET_TIME_HOUR), true);
}

void World::ResetDailyQuests()
{
    // reset all saved quest status
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_RESET_CHARACTER_QUESTSTATUS_DAILY);
    CharacterDatabase.Execute(stmt);
    // reset all quest status in memory
    for (SessionMap::const_iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        if (Player* player = itr->second->GetPlayer())
            player->ResetDailyQuestStatus();

    // reselect pools
    sQuestPoolMgr->ChangeDailyQuests();

    // store next reset time
    time_t now = GameTime::GetGameTime();
    time_t next = GetNextDailyResetTime(now);
    ASSERT(now < next);

    m_NextDailyQuestReset = next;
    sWorld->setWorldState(WS_DAILY_QUEST_RESET_TIME, uint64(next));

    TC_LOG_INFO("misc", "Daily quests for all characters have been reset.");
}

static time_t GetNextWeeklyResetTime(time_t t)
{
    t = GetNextDailyResetTime(t);
    tm time = TimeBreakdown(t);
    int wday = time.tm_wday;
    int target = sWorld->getIntConfig(CONFIG_WEEKLY_QUEST_RESET_TIME_WDAY);
    if (target < wday)
        wday -= 7;
    t += (DAY * (target - wday));
    return t;
}

void World::ResetWeeklyQuests()
{
    // reset all saved quest status
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_RESET_CHARACTER_QUESTSTATUS_WEEKLY);
    CharacterDatabase.Execute(stmt);
    // reset all quest status in memory
    for (SessionMap::const_iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        if (Player* player = itr->second->GetPlayer())
            player->ResetWeeklyQuestStatus();

    // reselect pools
    sQuestPoolMgr->ChangeWeeklyQuests();

    // store next reset time
    time_t now = GameTime::GetGameTime();
    time_t next = GetNextWeeklyResetTime(now);
    ASSERT(now < next);

    m_NextWeeklyQuestReset = next;
    sWorld->setWorldState(WS_WEEKLY_QUEST_RESET_TIME, uint64(next));

    TC_LOG_INFO("misc", "Weekly quests for all characters have been reset.");
}

static time_t GetNextMonthlyResetTime(time_t t)
{
    t = GetNextDailyResetTime(t);
    tm time = TimeBreakdown(t);
    if (time.tm_mday == 1)
        return t;

    time.tm_mday = 1;
    time.tm_mon += 1;
    return mktime(&time);
}

void World::ResetMonthlyQuests()
{
    // reset all saved quest status
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_RESET_CHARACTER_QUESTSTATUS_MONTHLY);
    CharacterDatabase.Execute(stmt);
    // reset all quest status in memory
    for (SessionMap::const_iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        if (Player* player = itr->second->GetPlayer())
            player->ResetMonthlyQuestStatus();

    // reselect pools
    sQuestPoolMgr->ChangeMonthlyQuests();

    // store next reset time
    time_t now = GameTime::GetGameTime();
    time_t next = GetNextMonthlyResetTime(now);
    ASSERT(now < next);

    m_NextMonthlyQuestReset = next;
    sWorld->setWorldState(WS_MONTHLY_QUEST_RESET_TIME, uint64(next));

    TC_LOG_INFO("misc", "Monthly quests for all characters have been reset.");
}

void World::CheckQuestResetTimes()
{
    time_t const now = GameTime::GetGameTime();
    if (m_NextDailyQuestReset <= now)
        ResetDailyQuests();
    if (m_NextWeeklyQuestReset <= now)
        ResetWeeklyQuests();
    if (m_NextMonthlyQuestReset <= now)
        ResetMonthlyQuests();
}

void World::InitRandomBGResetTime()
{
    time_t bgtime = uint64(sWorld->getWorldState(WS_BG_DAILY_RESET_TIME));
    if (!bgtime)
        m_NextRandomBGReset = GameTime::GetGameTime();         // game time not yet init

    // generate time by config
    time_t curTime = GameTime::GetGameTime();
    tm localTm;
    localtime_r(&curTime, &localTm);
    localTm.tm_hour = getIntConfig(CONFIG_RANDOM_BG_RESET_HOUR);
    localTm.tm_min = 0;
    localTm.tm_sec = 0;

    // current day reset time
    time_t nextDayResetTime = mktime(&localTm);

    // next reset time before current moment
    if (curTime >= nextDayResetTime)
        nextDayResetTime += DAY;

    // normalize reset time
    m_NextRandomBGReset = bgtime < curTime ? nextDayResetTime - DAY : nextDayResetTime;

    if (!bgtime)
        sWorld->setWorldState(WS_BG_DAILY_RESET_TIME, uint64(m_NextRandomBGReset));
}

void World::InitCalendarOldEventsDeletionTime()
{
    time_t now = GameTime::GetGameTime();
    time_t nextDeletionTime = GetLocalHourTimestamp(now, getIntConfig(CONFIG_CALENDAR_DELETE_OLD_EVENTS_HOUR));
    time_t currentDeletionTime = getWorldState(WS_DAILY_CALENDAR_DELETION_OLD_EVENTS_TIME);

    // If the reset time saved in the worldstate is before now it means the server was offline when the reset was supposed to occur.
    // In this case we set the reset time in the past and next world update will do the reset and schedule next one in the future.
    if (currentDeletionTime < now)
        m_NextCalendarOldEventsDeletionTime = nextDeletionTime - DAY;
    else
        m_NextCalendarOldEventsDeletionTime = nextDeletionTime;

    if (!currentDeletionTime)
        sWorld->setWorldState(WS_DAILY_CALENDAR_DELETION_OLD_EVENTS_TIME, uint64(m_NextCalendarOldEventsDeletionTime));
}

void World::InitGuildResetTime()
{
    time_t gtime = uint64(getWorldState(WS_GUILD_DAILY_RESET_TIME));
    if (!gtime)
        m_NextGuildReset = GameTime::GetGameTime();         // game time not yet init

    // generate time by config
    time_t curTime = GameTime::GetGameTime();
    tm localTm;
    localtime_r(&curTime, &localTm);
    localTm.tm_hour = getIntConfig(CONFIG_GUILD_RESET_HOUR);
    localTm.tm_min = 0;
    localTm.tm_sec = 0;

    // current day reset time
    time_t nextDayResetTime = mktime(&localTm);

    // next reset time before current moment
    if (curTime >= nextDayResetTime)
        nextDayResetTime += DAY;

    // normalize reset time
    m_NextGuildReset = gtime < curTime ? nextDayResetTime - DAY : nextDayResetTime;

    if (!gtime)
        sWorld->setWorldState(WS_GUILD_DAILY_RESET_TIME, uint64(m_NextGuildReset));
}

void World::ResetEventSeasonalQuests(uint16 event_id)
{
    TC_LOG_INFO("misc", "Seasonal quests reset for all characters.");

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_RESET_CHARACTER_QUESTSTATUS_SEASONAL_BY_EVENT);
    stmt->setUInt16(0, event_id);
    CharacterDatabase.Execute(stmt);

    for (SessionMap::const_iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        if (itr->second->GetPlayer())
            itr->second->GetPlayer()->ResetSeasonalQuestStatus(event_id);
}

void World::ResetRandomBG()
{
    TC_LOG_INFO("misc", "Random BG status reset for all characters.");

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_BATTLEGROUND_RANDOM_ALL);
    CharacterDatabase.Execute(stmt);

    for (SessionMap::const_iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        if (itr->second->GetPlayer())
            itr->second->GetPlayer()->SetRandomWinner(false);

    m_NextRandomBGReset = time_t(m_NextRandomBGReset + DAY);
    sWorld->setWorldState(WS_BG_DAILY_RESET_TIME, uint64(m_NextRandomBGReset));
}

void World::CalendarDeleteOldEvents()
{
    TC_LOG_INFO("misc", "Calendar deletion of old events.");

    m_NextCalendarOldEventsDeletionTime = time_t(m_NextCalendarOldEventsDeletionTime + DAY);
    sWorld->setWorldState(WS_DAILY_CALENDAR_DELETION_OLD_EVENTS_TIME, uint64(m_NextCalendarOldEventsDeletionTime));
    sCalendarMgr->DeleteOldEvents();
}

void World::ResetGuildCap()
{
    TC_LOG_INFO("misc", "Guild Daily Cap reset.");

    m_NextGuildReset = time_t(m_NextGuildReset + DAY);
    sWorld->setWorldState(WS_GUILD_DAILY_RESET_TIME, uint64(m_NextGuildReset));
    sGuildMgr->ResetTimes();
}

void World::UpdateMaxSessionCounters()
{
    m_maxActiveSessionCount = std::max(m_maxActiveSessionCount, uint32(m_sessions.size()-m_QueuedPlayer.size()));
    m_maxQueuedSessionCount = std::max(m_maxQueuedSessionCount, uint32(m_QueuedPlayer.size()));
}

void World::LoadDBVersion()
{
    QueryResult result = WorldDatabase.Query("SELECT db_version, cache_id FROM version LIMIT 1");
    if (result)
    {
        Field* fields = result->Fetch();

        m_DBVersion = fields[0].GetString();
        // will be overwrite by config values if different and non-0
        m_int_configs[CONFIG_CLIENTCACHE_VERSION] = fields[1].GetUInt32();
    }

    if (m_DBVersion.empty())
        m_DBVersion = "Unknown world database.";
}

void World::UpdateAreaDependentAuras()
{
    SessionMap::const_iterator itr;
    for (itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        if (itr->second && itr->second->GetPlayer() && itr->second->GetPlayer()->IsInWorld())
        {
            itr->second->GetPlayer()->UpdateAreaDependentAuras(itr->second->GetPlayer()->GetAreaId());
            itr->second->GetPlayer()->UpdateZoneDependentAuras(itr->second->GetPlayer()->GetZoneId());
        }
}

void World::LoadWorldStates()
{
    uint32 oldMSTime = getMSTime();

    QueryResult result = CharacterDatabase.Query("SELECT entry, value FROM worldstates");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 world states. DB table `worldstates` is empty!");

        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        m_worldstates[fields[0].GetUInt32()] = fields[1].GetUInt32();
        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} world states in {} ms", count, GetMSTimeDiffToNow(oldMSTime));

}

bool World::IsPvPRealm() const
{
    return (getIntConfig(CONFIG_GAME_TYPE) == REALM_TYPE_PVP || getIntConfig(CONFIG_GAME_TYPE) == REALM_TYPE_RPPVP || getIntConfig(CONFIG_GAME_TYPE) == REALM_TYPE_FFA_PVP);
}

bool World::IsFFAPvPRealm() const
{
    return getIntConfig(CONFIG_GAME_TYPE) == REALM_TYPE_FFA_PVP;
}

// Setting a worldstate will save it to DB
void World::setWorldState(uint32 index, uint64 value)
{
    WorldStatesMap::const_iterator it = m_worldstates.find(index);
    if (it != m_worldstates.end())
    {
        if (it->second == value)
            return;

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_WORLDSTATE);

        stmt->setUInt32(0, uint32(value));
        stmt->setUInt32(1, index);

        CharacterDatabase.Execute(stmt);
    }
    else
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_WORLDSTATE);

        stmt->setUInt32(0, index);
        stmt->setUInt32(1, uint32(value));

        CharacterDatabase.Execute(stmt);
    }
    m_worldstates[index] = value;
}

uint64 World::getWorldState(uint32 index) const
{
    WorldStatesMap::const_iterator it = m_worldstates.find(index);
    return it != m_worldstates.end() ? it->second : 0;
}

void World::ProcessQueryCallbacks()
{
    _queryProcessor.ProcessReadyCallbacks();
}

void World::ReloadRBAC()
{
    // Passive reload, we mark the data as invalidated and next time a permission is checked it will be reloaded
    TC_LOG_INFO("rbac", "World::ReloadRBAC()");
    for (SessionMap::const_iterator itr = m_sessions.begin(); itr != m_sessions.end(); ++itr)
        if (WorldSession* session = itr->second)
            session->InvalidateRBACData();
}

void World::RemoveOldCorpses()
{
    m_timers[WUPDATE_CORPSES].SetCurrent(m_timers[WUPDATE_CORPSES].GetInterval());
}

Realm realm;

CliCommandHolder::CliCommandHolder(void* callbackArg, char const* command, Print zprint, CommandFinished commandFinished)
    : m_callbackArg(callbackArg), m_command(strdup(command)), m_print(zprint), m_commandFinished(commandFinished)
{
}

CliCommandHolder::~CliCommandHolder()
{
    free(m_command);
}
