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
 * @file MiscHandler.cpp
 * @brief 杂项游戏功能网络消息处理器
 *
 * 本模块负责处理各种游戏功能的网络消息，包括但不限于：
 * - 玩家复活和尸体回收
 * - NPC对话选择
 * - 玩家查询（/who命令）
 * - 登出流程
 * - PvP状态切换
 * - 区域触发器
 * - 账户数据同步
 * - 动作按钮设置
 * - 电影和过场动画
 * - 角色观察
 * - 实例难度设置
 * - 区域灵魂医者交互
 *
 * 该处理器是游戏中最杂乱的网络消息处理模块，
 * 包含大量不相关但都归类为"杂项"的功能处理。
 *
 * @note 本文件函数较多且功能分散，建议按功能模块阅读
 */

#include "WorldSession.h"
#include "AccountMgr.h"
#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "CharacterPackets.h"
#include "Chat.h"
#include "CinematicMgr.h"
#include "Common.h"
#include "Corpse.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "GameTime.h"
#include "GossipDef.h"
#include "Group.h"
#include "GuildMgr.h"
#include "Language.h"
#include "Log.h"
#include "MapManager.h"
#include "MiscPackets.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "OutdoorPvP.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "WhoListStorage.h"
#include "World.h"
#include "WorldPacket.h"
#include <cstdarg>
#include <zlib.h>

/**
 * @brief 处理释放灵魂请求
 *
 * 当玩家死亡后点击"释放灵魂"按钮时，客户端发送此消息。
 * 玩家的灵魂会出现在最近的墓地，可以跑回尸体复活。
 *
 * @param packet 释放灵魂请求数据包（空数据包）
 *
 * 调用时机：
 * - 玩家死亡后点击"释放灵魂"按钮时
 * - 客户端发送 CMSG_REPOP_REQUEST 消息
 *
 * 处理流程：
 * 1. 验证玩家已死亡且尚未释放灵魂
 * 2. 检查是否有阻止复活的光环效果
 * 3. 处理服务器延迟导致的边界情况（玩家刚被杀死但尚未更新）
 * 4. 移除玩家的食尸鬼和宠物
 * 5. 创建玩家尸体并生成灵魂状态
 * 6. 将玩家传送至最近的墓地
 *
 * 特殊处理：
 * - 如果玩家刚死亡（JUST_DIED），先执行击杀逻辑
 * - 某些光环（如灵魂石）可能阻止释放
 *
 * @see Player::BuildPlayerRepop()
 * @see Player::RepopAtGraveyard()
 */
void WorldSession::HandleRepopRequest(WorldPackets::Misc::RepopRequest& /*packet*/)
{
    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_REPOP_REQUEST Message");

    if (GetPlayer()->IsAlive() || GetPlayer()->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
        return;

    if (GetPlayer()->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION))
        return; // silently return, client should display the error by itself

    // the world update order is sessions, players, creatures
    // the netcode runs in parallel with all of these
    // creatures can kill players
    // so if the server is lagging enough the player can
    // release spirit after he's killed but before he is updated
    if (GetPlayer()->getDeathState() == JUST_DIED)
    {
        TC_LOG_DEBUG("network", "HandleRepopRequestOpcode: got request after player {} {} was killed and before he was updated",
            GetPlayer()->GetName(), GetPlayer()->GetGUID().ToString());
        GetPlayer()->KillPlayer();
    }

    //this is spirit release confirm?
    GetPlayer()->RemoveGhoul();
    GetPlayer()->RemovePet(nullptr, PET_SAVE_NOT_IN_SLOT, true);
    GetPlayer()->BuildPlayerRepop();
    GetPlayer()->RepopAtGraveyard();
}

/**
 * @brief 处理NPC对话选项选择
 *
 * 当玩家在NPC对话菜单中选择一个选项时，客户端发送此消息。
 * 服务器会调用相应的脚本处理玩家的选择。
 *
 * @param recvData 接收的网络数据包，包含：
 *                 - guid: NPC或游戏对象的GUID
 *                 - menuId: 菜单ID
 *                 - gossipListId: 选项列表ID
 *                 - code: 可选的输入代码（用于需要输入的选项）
 *
 * 调用时机：
 * - 玩家在NPC对话窗口中选择选项时
 * - 客户端发送 CMSG_GOSSIP_SELECT_OPTION 消息
 *
 * 处理流程：
 * 1. 验证对话菜单存在
 * 2. 如果选项需要输入代码，读取代码数据
 * 3. 验证对话菜单的发送者GUID匹配（防止作弊）
 * 4. 获取NPC或游戏对象实体
 * 5. 验证实体存在且可交互
 * 6. 移除假死状态
 * 7. 检查脚本是否被重新加载
 * 8. 调用AI脚本处理选项选择
 *
 * 脚本处理：
 * - 优先调用AI脚本的OnGossipSelect/OnGossipSelectCode
 * - 如果脚本返回false，调用默认处理
 *
 * 安全措施：
 * - 验证对话菜单的发送者GUID
 * - 验证NPC或游戏对象可交互
 * - 检测脚本重新加载并关闭菜单
 *
 * @see Player::OnGossipSelect()
 * @see CreatureAI::OnGossipSelect()
 */
void WorldSession::HandleGossipSelectOptionOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_GOSSIP_SELECT_OPTION");

    uint32 gossipListId;
    uint32 menuId;
    ObjectGuid guid;
    std::string code = "";

    recvData >> guid >> menuId >> gossipListId;

    if (!_player->PlayerTalkClass->GetGossipMenu().GetItem(gossipListId))
    {
        recvData.rfinish();
        return;
    }

    if (_player->PlayerTalkClass->IsGossipOptionCoded(gossipListId))
        recvData >> code;

    // Prevent cheating on C++ scripted menus
    if (_player->PlayerTalkClass->GetGossipMenu().GetSenderGUID() != guid)
        return;

    Creature* unit = nullptr;
    GameObject* go = nullptr;
    if (guid.IsCreatureOrVehicle())
    {
        unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_GOSSIP);
        if (!unit)
        {
            TC_LOG_DEBUG("network", "WORLD: HandleGossipSelectOptionOpcode - {} not found or you can't interact with him.", guid.ToString());
            return;
        }
    }
    else if (guid.IsGameObject())
    {
        go = _player->GetGameObjectIfCanInteractWith(guid);
        if (!go)
        {
            TC_LOG_DEBUG("network", "WORLD: HandleGossipSelectOptionOpcode - {} not found or you can't interact with it.", guid.ToString());
            return;
        }
    }
    else
    {
        TC_LOG_DEBUG("network", "WORLD: HandleGossipSelectOptionOpcode - unsupported {}.", guid.ToString());
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    if ((unit && unit->GetScriptId() != unit->LastUsedScriptID) || (go && go->GetScriptId() != go->LastUsedScriptID))
    {
        TC_LOG_DEBUG("network", "WORLD: HandleGossipSelectOptionOpcode - Script reloaded while in use, ignoring and set new scipt id");
        if (unit)
            unit->LastUsedScriptID = unit->GetScriptId();
        if (go)
            go->LastUsedScriptID = go->GetScriptId();
        _player->PlayerTalkClass->SendCloseGossip();
        return;
    }
    if (!code.empty())
    {
        if (unit)
        {
            if (!unit->AI()->OnGossipSelectCode(_player, menuId, gossipListId, code.c_str()))
                _player->OnGossipSelect(unit, gossipListId, menuId);
        }
        else
        {
            if (!go->AI()->OnGossipSelectCode(_player, menuId, gossipListId, code.c_str()))
                _player->OnGossipSelect(go, gossipListId, menuId);
        }
    }
    else
    {
        if (unit)
        {
            if (!unit->AI()->OnGossipSelect(_player, menuId, gossipListId))
                _player->OnGossipSelect(unit, gossipListId, menuId);
        }
        else
        {
            if (!go->AI()->OnGossipSelect(_player, menuId, gossipListId))
                _player->OnGossipSelect(go, gossipListId, menuId);
        }
    }
}

/**
 * @brief 处理/who命令查询在线玩家
 *
 * 当玩家使用/who命令查询在线玩家时，服务器根据筛选条件返回符合条件的玩家列表。
 * 支持多种筛选条件：等级范围、名称、公会、种族、职业、区域等。
 *
 * @param recvData 接收的网络数据包，包含：
 *                 - levelMin: 最小等级
 *                 - levelMax: 最大等级
 *                 - packetPlayerName: 玩家名称过滤
 *                 - packetGuildName: 公会名称过滤
 *                 - racemask: 种族掩码
 *                 - classmask: 职业掩码
 *                 - zonesCount: 区域数量
 *                 - zoneids: 区域ID数组
 *                 - strCount: 搜索字符串数量
 *                 - str: 搜索字符串数组
 *
 * 调用时机：
 * - 玩家使用/who命令时
 * - 玩家打开社交窗口的"谁"标签时
 * - 客户端发送 CMSG_WHO 消息
 *
 * 处理流程：
 * 1. 读取并验证所有筛选条件
 * 2. 验证区域和字符串数量不超过客户端限制
 * 3. 将名称转换为小写用于模糊匹配
 * 4. 如果等级上限为100，更新为服务器最大等级（用于显示GM）
 * 5. 遍历在线玩家列表：
 *    - 检查阵营限制
 *    - 检查GM等级可见性
 *    - 检查等级范围
 *    - 检查职业和种族匹配
 *    - 检查区域匹配
 *    - 检查名称和公会名称匹配
 *    - 检查搜索字符串匹配
 * 6. 构建并发送匹配玩家列表
 *
 * 筛选规则：
 * - 默认显示同阵营玩家（除非有跨阵营权限）
 * - GM默认对普通玩家隐藏
 * - 支持模糊名称匹配
 * - 最多返回配置的最大数量（默认49）
 *
 * 性能注意事项：
 * - 使用预生成的在线玩家列表避免遍历所有玩家
 * - 字符串匹配使用宽字符小写比较
 *
 * @see WhoListStorageMgr::GetWhoList()
 */
void WorldSession::HandleWhoOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_WHO Message");

    uint32 matchCount = 0;

    uint32 levelMin, levelMax, racemask, classmask, zonesCount, strCount;
    uint32 zoneids[10];                                     // 10 is client limit
    std::string packetPlayerName, packetGuildName;

    recvData >> levelMin;                                   // maximal player level, default 0
    recvData >> levelMax;                                   // minimal player level, default 100 (MAX_LEVEL)
    recvData >> packetPlayerName;                           // player name, case sensitive...

    recvData >> packetGuildName;                            // guild name, case sensitive...

    recvData >> racemask;                                   // race mask
    recvData >> classmask;                                  // class mask
    recvData >> zonesCount;                                 // zones count, client limit = 10 (2.0.10)

    if (zonesCount > 10)
        return;                                             // can't be received from real client or broken packet

    for (uint32 i = 0; i < zonesCount; ++i)
    {
        uint32 temp;
        recvData >> temp;                                   // zone id, 0 if zone is unknown...
        zoneids[i] = temp;
        TC_LOG_DEBUG("network", "Zone {}: {}", i, zoneids[i]);
    }

    recvData >> strCount;                                   // user entered strings count, client limit=4 (checked on 2.0.10)

    if (strCount > 4)
        return;                                             // can't be received from real client or broken packet

    TC_LOG_DEBUG("network", "Minlvl {}, maxlvl {}, name {}, guild {}, racemask {}, classmask {}, zones {}, strings {}", levelMin, levelMax, packetPlayerName, packetGuildName, racemask, classmask, zonesCount, strCount);

    std::wstring str[4];                                    // 4 is client limit
    for (uint32 i = 0; i < strCount; ++i)
    {
        std::string temp;
        recvData >> temp;                                   // user entered string, it used as universal search pattern(guild+player name)?

        if (!Utf8toWStr(temp, str[i]))
            continue;

        wstrToLower(str[i]);

        TC_LOG_DEBUG("network", "String {}: {}", i, temp);
    }

    std::wstring wpacketPlayerName;
    std::wstring wpacketGuildName;
    if (!(Utf8toWStr(packetPlayerName, wpacketPlayerName) && Utf8toWStr(packetGuildName, wpacketGuildName)))
        return;

    wstrToLower(wpacketPlayerName);
    wstrToLower(wpacketGuildName);

    // client send in case not set max level value 100 but Trinity supports 255 max level,
    // update it to show GMs with characters after 100 level
    if (levelMax >= MAX_LEVEL)
        levelMax = STRONG_MAX_LEVEL;

    uint32 team = _player->GetTeam();

    uint32 gmLevelInWhoList  = sWorld->getIntConfig(CONFIG_GM_LEVEL_IN_WHO_LIST);
    uint32 displayCount = 0;

    WorldPacket data(SMSG_WHO, 500);                      // guess size
    data << uint32(matchCount);                           // placeholder, count of players matching criteria
    data << uint32(displayCount);                         // placeholder, count of players displayed

    WhoListInfoVector const& whoList = sWhoListStorageMgr->GetWhoList();
    for (WhoListPlayerInfo const& target : whoList)
    {
        // player can see member of other team only if CONFIG_ALLOW_TWO_SIDE_WHO_LIST
        if (target.GetTeam() != team && !HasPermission(rbac::RBAC_PERM_TWO_SIDE_WHO_LIST))
            continue;

        // player can see MODERATOR, GAME MASTER, ADMINISTRATOR only if CONFIG_GM_IN_WHO_LIST
        if (!HasPermission(rbac::RBAC_PERM_WHO_SEE_ALL_SEC_LEVELS) && target.GetSecurity() > AccountTypes(gmLevelInWhoList))
            continue;

        // check if target is globally visible for player
        if (_player->GetGUID() != target.GetGuid() && !target.IsVisible())
            if (AccountMgr::IsPlayerAccount(_player->GetSession()->GetSecurity()) || target.GetSecurity() > _player->GetSession()->GetSecurity())
                continue;

        // check if target's level is in level range
        uint8 lvl = target.GetLevel();
        if (lvl < levelMin || lvl > levelMax)
            continue;

        // check if class matches classmask
        uint8 class_ = target.GetClass();
        if (!(classmask & (1 << class_)))
            continue;

        // check if race matches racemask
        uint32 race = target.GetRace();
        if (!(racemask & (1 << race)))
            continue;

        uint32 playerZoneId = target.GetZoneId();
        uint8 gender = target.GetGender();

        bool showZones = true;
        for (uint32 i = 0; i < zonesCount; ++i)
        {
            if (zoneids[i] == playerZoneId)
            {
                showZones = true;
                break;
            }

            showZones = false;
        }
        if (!showZones)
            continue;

        std::wstring const& wideplayername = target.GetWidePlayerName();
        if (!(wpacketPlayerName.empty() || wideplayername.find(wpacketPlayerName) != std::wstring::npos))
            continue;

        std::wstring const& wideguildname = target.GetWideGuildName();
        if (!(wpacketGuildName.empty() || wideguildname.find(wpacketGuildName) != std::wstring::npos))
            continue;

        std::string aname;
        if (AreaTableEntry const* areaEntry = sAreaTableStore.LookupEntry(playerZoneId))
            aname = areaEntry->AreaName[GetSessionDbcLocale()];

        bool s_show = true;
        for (uint32 i = 0; i < strCount; ++i)
        {
            if (!str[i].empty())
            {
                if (wideguildname.find(str[i]) != std::wstring::npos ||
                    wideplayername.find(str[i]) != std::wstring::npos ||
                    Utf8FitTo(aname, str[i]))
                {
                    s_show = true;
                    break;
                }
                s_show = false;
            }
        }
        if (!s_show)
            continue;

        // 49 is maximum player count sent to client - can be overridden
        // through config, but is unstable
        if ((matchCount++) >= sWorld->getIntConfig(CONFIG_MAX_WHO))
            continue;

        data << target.GetPlayerName();                   // player name
        data << target.GetGuildName();                    // guild name
        data << uint32(lvl);                              // player level
        data << uint32(class_);                           // player class
        data << uint32(race);                             // player race
        data << uint8(gender);                            // player gender
        data << uint32(playerZoneId);                     // player zone id

        ++displayCount;
    }

    data.put(0, displayCount);                            // insert right count, count displayed
    data.put(4, matchCount);                              // insert right count, count of matches

    SendPacket(&data);
    TC_LOG_DEBUG("network", "WORLD: Send SMSG_WHO Message");
}

/**
 * @brief 处理登出请求
 *
 * 当玩家点击"退出游戏"或按下ESC键时，客户端发送登出请求。
 * 根据玩家状态，可能是即时登出或需要等待20秒倒计时。
 *
 * @param logoutRequest 登出请求数据包
 *
 * 调用时机：
 * - 玩家点击"退出游戏"按钮时
 * - 玩家按下ESC键时
 * - 客户端发送 CMSG_LOGOUT_REQUEST 消息
 *
 * 处理流程：
 * 1. 如果玩家正在拾取战利品，先释放战利品
 * 2. 判断是否可以即时登出：
 *    - 在休息区域（旅店/城市）且不在战斗中
 *    - 正在飞行中
 *    - 有GM即时登出权限
 * 3. 检查登出限制条件：
 *    - 是否在战斗中
 *    - 是否正在下落
 *    - 是否在决斗中
 *    - 是否被GM冻结
 * 4. 如果有登出限制，发送错误原因并拒绝登出
 * 5. 如果可以即时登出，立即执行登出
 * 6. 否则：
 *    - 设置玩家坐下状态
 *    - 定住玩家
 *    - 开始20秒登出倒计时
 *
 * 登出限制：
 * - 战斗中不能登出（除非在休息区域）
 * - 下落中不能登出
 * - 决斗中不能登出
 * - 被冻结不能登出
 *
 * 即时登出条件：
 * - 在旅店/城市休息区域
 * - 正在飞行（乘坐飞行路线）
 * - GM权限
 *
 * @see LogoutPlayer()
 * @see SetLogoutStartTime()
 */
void WorldSession::HandleLogoutRequestOpcode(WorldPackets::Character::LogoutRequest& /*logoutRequest*/)
{
    if (ObjectGuid lguid = GetPlayer()->GetLootGUID())
        DoLootRelease(lguid);

    bool instantLogout = (GetPlayer()->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING) && !GetPlayer()->IsInCombat()) ||
                         GetPlayer()->IsInFlight() || HasPermission(rbac::RBAC_PERM_INSTANT_LOGOUT);

    /// TODO: Possibly add RBAC permission to log out in combat
    bool canLogoutInCombat = GetPlayer()->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING);

    uint32 reason = 0;
    if (GetPlayer()->IsInCombat() && !canLogoutInCombat)
        reason = 1;
    else if (GetPlayer()->m_movementInfo.HasMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR))
        reason = 3;                                         // is jumping or falling
    else if (GetPlayer()->duel || GetPlayer()->HasAura(9454)) // is dueling or frozen by GM via freeze command
        reason = 2;                                         // FIXME - Need the correct value

    WorldPackets::Character::LogoutResponse logoutResponse;
    logoutResponse.LogoutResult = reason;
    logoutResponse.Instant = instantLogout;
    SendPacket(logoutResponse.Write());

    if (reason)
    {
        SetLogoutStartTime(0);
        return;
    }

    // instant logout in taverns/cities or on taxi or for admins, gm's, mod's if its enabled in worldserver.conf
    if (instantLogout)
    {
        LogoutPlayer(true);
        return;
    }

    // not set flags if player can't free move to prevent lost state at logout cancel
    if (GetPlayer()->CanFreeMove())
    {
        if (GetPlayer()->GetStandState() == UNIT_STAND_STATE_STAND)
            GetPlayer()->SetStandState(UNIT_STAND_STATE_SIT);
        GetPlayer()->SetRooted(true);
        GetPlayer()->SetUnitFlag(UNIT_FLAG_STUNNED);
    }

    SetLogoutStartTime(GameTime::GetGameTime());
}

/**
 * @brief 处理玩家登出消息
 *
 * 这是一个空消息处理器，在登出流程中客户端会发送此消息，
 * 但服务器不需要执行任何特殊处理。
 *
 * @param playerLogout 玩家登出数据包（空数据包）
 *
 * @note 该消息是登出流程的一部分，实际登出逻辑在其他地方处理
 */
void WorldSession::HandlePlayerLogoutOpcode(WorldPackets::Character::PlayerLogout& /*playerLogout*/)
{
}

/**
 * @brief 处理取消登出请求
 *
 * 当玩家在登出倒计时期间移动或点击取消按钮时，取消登出流程。
 * 恢复玩家的正常状态，取消坐下和定身效果。
 *
 * @param logoutCancel 取消登出数据包（空数据包）
 *
 * 调用时机：
 * - 玩家在登出倒计时期间移动时
 * - 玩家点击取消按钮时
 * - 客户端发送 CMSG_LOGOUT_CANCEL 消息
 *
 * 处理流程：
 * 1. 检查玩家是否仍在线（防止重复取消）
 * 2. 清除登出开始时间
 * 3. 发送取消确认给客户端
 * 4. 如果玩家可以自由移动：
 *    - 解除定身状态
 *    - 设置玩家站立
 *    - 移除眩晕标志
 *
 * @note 只有在登出请求中设置了限制标志，才会在此移除
 */
void WorldSession::HandleLogoutCancelOpcode(WorldPackets::Character::LogoutCancel& /*logoutCancel*/)
{
    // Player have already logged out serverside, too late to cancel
    if (!GetPlayer())
        return;

    SetLogoutStartTime(0);

    SendPacket(WorldPackets::Character::LogoutCancelAck().Write());

    // not remove flags if can't free move - its not set in Logout request code.
    if (GetPlayer()->CanFreeMove())
    {
        //!we can move again
        GetPlayer()->SetRooted(false);

        //! Stand Up
        GetPlayer()->SetStandState(UNIT_STAND_STATE_STAND);

        //! DISABLE_ROTATE
        GetPlayer()->RemoveUnitFlag(UNIT_FLAG_STUNNED);
    }
}

/**
 * @brief 处理切换PvP状态的请求
 *
 * 当玩家切换PvP标志时，更新玩家的PvP状态和计时器。
 * PvP状态影响玩家是否可以被敌对阵营攻击。
 *
 * @param togglePvP 切换PvP数据包，可选包含启用/禁用标志
 *
 * 调用时机：
 * - 玩家点击自己的头像框切换PvP标志时
 * - 玩家输入/pvp命令时
 * - 客户端发送 CMSG_TOGGLE_PVP 消息
 *
 * 处理流程：
 * 1. 如果数据包包含明确的启用标志：
 *    - 设置或清除PvP标志
 *    - 设置或清除PvP计时器标志
 * 2. 如果数据包没有启用标志（切换模式）：
 *    - 切换PvP标志状态
 *    - 切换PvP计时器标志状态
 * 3. 如果启用了PvP标志：
 *    - 立即更新PvP状态
 * 4. 如果禁用了PvP标志且玩家不处于敌对状态：
 *    - 开始PvP关闭倒计时（5分钟）
 *
 * PvP规则：
 * - 启用PvP后，敌对阵营玩家可以攻击你
 * - 禁用PvP后，需要5分钟无战斗才会真正关闭PvP
 * - 在战斗中或敌对状态下不能关闭PvP
 *
 * @see Player::UpdatePvP()
 */
void WorldSession::HandleTogglePvP(WorldPackets::Misc::TogglePvP& togglePvP)
{
    // this opcode can be used in two ways: Either set explicit new status or toggle old status
    if (togglePvP.Enable)
    {
        GetPlayer()->ApplyModFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP, *togglePvP.Enable);
        GetPlayer()->ApplyModFlag(PLAYER_FLAGS, PLAYER_FLAGS_PVP_TIMER, !*togglePvP.Enable);
    }
    else
    {
        GetPlayer()->ToggleFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP);
        GetPlayer()->ToggleFlag(PLAYER_FLAGS, PLAYER_FLAGS_PVP_TIMER);
    }

    if (GetPlayer()->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP))
    {
        if (!GetPlayer()->IsPvP() || GetPlayer()->pvpInfo.EndTimer)
            GetPlayer()->UpdatePvP(true, true);
    }
    else
    {
        if (!GetPlayer()->pvpInfo.IsHostile && GetPlayer()->IsPvP())
            GetPlayer()->pvpInfo.EndTimer = GameTime::GetGameTime();     // start toggle-off
    }
}

/**
 * @brief 处理区域更新消息
 *
 * 当客户端检测到玩家进入新区域时发送此消息。
 * 服务器会标记需要更新区域数据，实际更新在玩家位置更新时执行。
 *
 * @param recvData 接收的网络数据包，包含新区域ID
 *
 * 调用时机：
 * - 客户端检测到玩家进入新区域时
 * - 客户端发送 MSG_ZONE_UPDATE 消息
 *
 * 处理流程：
 * 1. 读取客户端发送的新区域ID
 * 2. 标记玩家需要区域更新
 * 3. 实际的区域更新在 Player::UpdatePosition() 中执行
 *
 * @note 服务器使用自己的位置数据来确定区域，客户端数据仅用于参考
 *
 * @see Player::SetNeedsZoneUpdate()
 * @see Player::UpdatePosition()
 */
void WorldSession::HandleZoneUpdateOpcode(WorldPacket& recvData)
{
    uint32 newZone;
    recvData >> newZone;

    TC_LOG_DEBUG("network", "WORLD: Recvd ZONE_UPDATE: {}", newZone);

    // use server side data, but only after update the player position. See Player::UpdatePosition().
    GetPlayer()->SetNeedsZoneUpdate(true);

    //GetPlayer()->SendInitWorldStates(true, newZone);
}

/**
 * @brief 处理设置选中目标的请求
 *
 * 当玩家选择一个目标时（点击或使用快捷键），客户端发送此消息。
 * 服务端更新玩家的选中目标GUID。
 *
 * @param recvData 接收的网络数据包，包含目标的GUID
 *
 * 调用时机：
 * - 玩家点击一个单位时
 * - 玩家使用Tab键选择目标时
 * - 玩家使用宏选择目标时
 * - 客户端发送 CMSG_SET_SELECTION 消息
 *
 * @see Player::SetSelection()
 */
void WorldSession::HandleSetSelectionOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    _player->SetSelection(guid);
}

/**
 * @brief 处理站立状态改变的请求
 *
 * 当玩家切换站立/坐下/睡觉/跪下状态时，客户端发送此消息。
 * 服务端验证状态的有效性并更新玩家的动画状态。
 *
 * @param recvData 接收的网络数据包，包含动画状态ID
 *
 * 调用时机：
 * - 玩家使用/emote命令时
 * - 玩家点击坐下按钮时
 * - 客户端发送 CMSG_STANDSTATE_CHANGE 消息
 *
 * 有效状态：
 * - UNIT_STAND_STATE_STAND: 站立
 * - UNIT_STAND_STATE_SIT: 坐下
 * - UNIT_STAND_STATE_SLEEP: 睡觉
 * - UNIT_STAND_STATE_KNEEL: 跪下
 *
 * @see Player::SetStandState()
 */
void WorldSession::HandleStandStateChangeOpcode(WorldPacket& recvData)
{
    uint32 animstate;
    recvData >> animstate;

    switch (animstate)
    {
        case UNIT_STAND_STATE_STAND:
        case UNIT_STAND_STATE_SIT:
        case UNIT_STAND_STATE_SLEEP:
        case UNIT_STAND_STATE_KNEEL:
            break;
        default:
            return;
    }

    _player->SetStandState(UnitStandStateType(animstate));
}

/**
 * @brief 处理Bug报告提交
 *
 * 当玩家使用Bug报告功能提交问题时，系统将报告内容保存到数据库。
 * 支持Bug报告和建议两种类型。
 *
 * @param recvData 接收的网络数据包，包含：
 *                 - suggestion: 是否为建议（0=Bug，1=建议）
 *                 - contentlen: 内容长度
 *                 - content: 报告内容
 *                 - typelen: 类型长度
 *                 - type: 报告类型
 *
 * 调用时机：
 * - 玩家通过帮助菜单提交Bug报告时
 * - 客户端发送 CMSG_BUG 消息
 *
 * @note 报告内容保存在 character_bug_report 表中
 */
void WorldSession::HandleBugOpcode(WorldPacket& recvData)
{
    uint32 suggestion, contentlen, typelen;
    std::string content, type;

    recvData >> suggestion >> contentlen >> content;

    recvData >> typelen >> type;

    if (suggestion == 0)
        TC_LOG_DEBUG("network", "WORLD: Received CMSG_BUG [Bug Report]");
    else
        TC_LOG_DEBUG("network", "WORLD: Received CMSG_BUG [Suggestion]");

    TC_LOG_DEBUG("network", "{}", type);
    TC_LOG_DEBUG("network", "{}", content);

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_BUG_REPORT);

    stmt->setString(0, type);
    stmt->setString(1, content);

    CharacterDatabase.Execute(stmt);
}

/**
 * @brief 处理回收尸体的请求
 *
 * 当玩家灵魂状态跑回尸体并点击复活时，客户端发送此消息。
 * 服务端验证尸体位置和时间限制，然后复活玩家。
 *
 * @param packet 回收尸体请求数据包（空数据包）
 *
 * 调用时机：
 * - 玩家灵魂状态下接近尸体并点击"复活"按钮时
 * - 客户端发送 CMSG_RECLAIM_CORPSE 消息
 *
 * 处理流程：
 * 1. 验证玩家已死亡且处于灵魂状态
 * 2. 检查是否在竞技场中（不允许回收尸体）
 * 3. 获取玩家尸体对象
 * 4. 检查尸体回收延迟（最少30秒）
 * 5. 检查玩家与尸体的距离
 * 6. 复活玩家（战场满血，其他50%血）
 * 7. 生成骨骼（移除尸体）
 *
 * 复活规则：
 * - 必须等待至少30秒才能回收尸体
 * - 必须在尸体附近（CORPSE_RECLAIM_RADIUS）
 * - 竞技场中不能回收尸体
 * - 战场复活恢复100%生命值
 * - 普通复活恢复50%生命值
 *
 * @see Player::ResurrectPlayer()
 * @see Player::SpawnCorpseBones()
 */
void WorldSession::HandleReclaimCorpse(WorldPackets::Misc::ReclaimCorpse& /*packet*/)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_RECLAIM_CORPSE");

    if (_player->IsAlive())
        return;

    // do not allow corpse reclaim in arena
    if (_player->InArena())
        return;

    // body not released yet
    if (!_player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
        return;

    Corpse* corpse = _player->GetCorpse();
    if (!corpse)
        return;

    // prevent resurrect before 30-sec delay after body release not finished
    if (time_t(corpse->GetGhostTime() + _player->GetCorpseReclaimDelay(corpse->GetType() == CORPSE_RESURRECTABLE_PVP)) > GameTime::GetGameTime())
        return;

    if (!corpse->IsWithinDistInMap(_player, CORPSE_RECLAIM_RADIUS, true))
        return;

    // resurrect
    _player->ResurrectPlayer(_player->InBattleground() ? 1.0f : 0.5f);

    // spawn bones
    _player->SpawnCorpseBones();
}

/**
 * @brief 处理复活响应
 *
 * 当玩家接受或拒绝其他玩家的复活请求时，客户端发送此消息。
 * 如果接受，玩家在安全位置复活；如果拒绝，清除复活请求。
 *
 * @param packet 复活响应数据包，包含响应值（0=拒绝，1=接受）和复活者GUID
 *
 * 调用时机：
 * - 玩家收到复活请求弹窗并点击接受或拒绝时
 * - 客户端发送 CMSG_RESURRECT_RESPONSE 消息
 *
 * 处理流程：
 * 1. 验证玩家已死亡
 * 2. 如果响应为拒绝，清除复活请求数据
 * 3. 如果响应为接受：
 *    - 验证复活请求来自指定的复活者
 *    - 使用请求数据复活玩家
 *
 * @see Player::ResurrectUsingRequestData()
 * @see Player::ClearResurrectRequestData()
 */
void WorldSession::HandleResurrectResponse(WorldPackets::Misc::ResurrectResponse& packet)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_RESURRECT_RESPONSE");

    if (GetPlayer()->IsAlive())
        return;

    if (packet.Response == 0)
    {
        GetPlayer()->ClearResurrectRequestData();           // reject
        return;
    }

    if (!GetPlayer()->IsResurrectRequestedBy(packet.Resurrecter))
        return;

    GetPlayer()->ResurrectUsingRequestData();
}

/**
 * @brief 发送区域触发消息给客户端
 *
 * 向玩家客户端发送一条区域触发消息，显示在屏幕中央。
 * 这是一个辅助函数，支持格式化字符串。
 *
 * @param Text 格式化文本字符串
 * @param ... 可变参数列表
 *
 * 使用场景：
 * - 玩家进入特殊区域时显示提示
 * - 任务相关区域提示
 * - 副本进入提示
 *
 * @note 消息最大长度为1024字符
 */
void WorldSession::SendAreaTriggerMessage(char const* Text, ...)
{
    va_list ap;
    char szStr [1024];
    szStr[0] = '\0';

    va_start(ap, Text);
    vsnprintf(szStr, 1024, Text, ap);
    va_end(ap);

    uint32 length = strlen(szStr)+1;
    WorldPacket data(SMSG_AREA_TRIGGER_MESSAGE, 4+length);
    data << length;
    data << szStr;
    SendPacket(&data);
}

/**
 * @brief 处理区域触发器消息
 *
 * 当玩家进入区域触发器范围时，客户端发送此消息。
 * 区域触发器用于各种游戏机制：传送、任务完成、旅馆休息等。
 *
 * @param recvData 接收的网络数据包，包含区域触发器ID
 *
 * 调用时机：
 * - 玩家进入区域触发器范围时
 * - 客户端发送 CMSG_AREATRIGGER 消息
 *
 * 处理流程：
 * 1. 验证玩家不在飞行中
 * 2. 查找区域触发器定义
 * 3. 验证玩家在触发器范围内
 * 4. 检查条件是否满足
 * 5. 调用脚本处理（如果存在）
 * 6. 处理任务区域触发器
 * 7. 处理旅馆区域触发器（进入休息状态）
 * 8. 处理战场区域触发器
 * 9. 处理户外PvP区域触发器
 * 10. 处理传送区域触发器
 *
 * 区域触发器类型：
 * - 任务触发器：完成探索任务
 * - 旅馆触发器：设置休息状态
 * - 传送触发器：传送玩家到其他位置
 * - 战场触发器：战场特定逻辑
 *
 * 传送处理：
 * - 检查目标地图进入权限
 * - 处理副本进入限制
 * - 处理团队和难度设置
 * - 在尸体所在地图入口复活死亡玩家
 *
 * @see Player::TeleportTo()
 * @see ScriptMgr::OnAreaTrigger()
 */
void WorldSession::HandleAreaTriggerOpcode(WorldPacket& recvData)
{
    uint32 triggerId;
    recvData >> triggerId;

    TC_LOG_DEBUG("network", "CMSG_AREATRIGGER. Trigger ID: {}", triggerId);

    Player* player = GetPlayer();
    if (player->IsInFlight())
    {
        TC_LOG_DEBUG("network", "HandleAreaTriggerOpcode: Player '{}' {} in flight, ignore Area Trigger ID:{}",
            player->GetName(), player->GetGUID().ToString(), triggerId);
        return;
    }

    AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(triggerId);
    if (!atEntry)
    {
        TC_LOG_DEBUG("network", "HandleAreaTriggerOpcode: Player '{}' {} send unknown (by DBC) Area Trigger ID:{}",
            player->GetName(), player->GetGUID().ToString(), triggerId);
        return;
    }

    if (!player->IsInAreaTriggerRadius(atEntry))
    {
        TC_LOG_DEBUG("network", "HandleAreaTriggerOpcode: Player '{}' {} too far, ignore Area Trigger ID: {}",
            player->GetName(), player->GetGUID().ToString(), triggerId);
        return;
    }

    if (player->isDebugAreaTriggers)
        ChatHandler(player->GetSession()).PSendSysMessage(LANG_DEBUG_AREATRIGGER_REACHED, triggerId);

    if (!sConditionMgr->IsObjectMeetingNotGroupedConditions(CONDITION_SOURCE_TYPE_AREATRIGGER_CLIENT_TRIGGERED, atEntry->ID, player))
        return;

    if (sScriptMgr->OnAreaTrigger(player, atEntry))
        return;

    if (player->IsAlive())
        if (uint32 questId = sObjectMgr->GetQuestForAreaTrigger(triggerId))
            if (player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
                player->AreaExploredOrEventHappens(questId);

    if (sObjectMgr->IsTavernAreaTrigger(triggerId))
    {
        // set resting flag we are in the inn
        player->SetRestFlag(REST_FLAG_IN_TAVERN, atEntry->ID);

        if (sWorld->IsFFAPvPRealm())
            player->RemovePvpFlag(UNIT_BYTE2_FLAG_FFA_PVP);

        return;
    }

    if (Battleground* bg = player->GetBattleground())
        if (bg->GetStatus() == STATUS_IN_PROGRESS)
            bg->HandleAreaTrigger(player, triggerId);

    if (OutdoorPvP* pvp = player->GetOutdoorPvP())
        if (pvp->HandleAreaTrigger(_player, triggerId))
            return;

    AreaTrigger const* at = sObjectMgr->GetAreaTrigger(triggerId);
    if (!at)
        return;

    bool teleported = false;
    if (player->GetMapId() != at->target_mapId)
    {
        if (Map::EnterState denyReason = sMapMgr->PlayerCannotEnter(at->target_mapId, player, false))
        {
            bool reviveAtTrigger = false; // should we revive the player if he is trying to enter the correct instance?
            switch (denyReason)
            {
                case Map::CANNOT_ENTER_NO_ENTRY:
                    TC_LOG_DEBUG("maps", "MAP: Player '{}' attempted to enter map with id {} which has no entry", player->GetName(), at->target_mapId);
                    break;
                case Map::CANNOT_ENTER_UNINSTANCED_DUNGEON:
                    TC_LOG_DEBUG("maps", "MAP: Player '{}' attempted to enter dungeon map {} but no instance template was found", player->GetName(), at->target_mapId);
                    break;
                case Map::CANNOT_ENTER_DIFFICULTY_UNAVAILABLE:
                    TC_LOG_DEBUG("maps", "MAP: Player '{}' attempted to enter instance map {} but the requested difficulty was not found", player->GetName(), at->target_mapId);
                    if (MapEntry const* entry = sMapStore.LookupEntry(at->target_mapId))
                        player->SendTransferAborted(entry->ID, TRANSFER_ABORT_DIFFICULTY, player->GetDifficulty(entry->IsRaid()));
                    break;
                case Map::CANNOT_ENTER_NOT_IN_RAID:
                {
                    WorldPacket data(SMSG_RAID_GROUP_ONLY, 4 + 4);
                    data << uint32(0);
                    data << uint32(2); // You must be in a raid group to enter this instance.
                    player->SendDirectMessage(&data);
                    TC_LOG_DEBUG("maps", "MAP: Player '{}' must be in a raid group to enter instance map {}", player->GetName(), at->target_mapId);
                    reviveAtTrigger = true;
                    break;
                }
                case Map::CANNOT_ENTER_CORPSE_IN_DIFFERENT_INSTANCE:
                {
                    WorldPacket data(SMSG_CORPSE_NOT_IN_INSTANCE);
                    player->SendDirectMessage(&data);
                    TC_LOG_DEBUG("maps", "MAP: Player '{}' does not have a corpse in instance map {} and cannot enter", player->GetName(), at->target_mapId);
                    break;
                }
                case Map::CANNOT_ENTER_INSTANCE_BIND_MISMATCH:
                    if (MapEntry const* entry = sMapStore.LookupEntry(at->target_mapId))
                    {
                        char const* mapName = entry->MapName[player->GetSession()->GetSessionDbcLocale()];
                        TC_LOG_DEBUG("maps", "MAP: Player '{}' cannot enter instance map '{}' because their permanent bind is incompatible with their group's", player->GetName(), mapName);
                        // is there a special opcode for this?
                        // @todo figure out how to get player localized difficulty string (e.g. "10 player", "Heroic" etc)
                        ChatHandler(player->GetSession()).PSendSysMessage(player->GetSession()->GetTrinityString(LANG_INSTANCE_BIND_MISMATCH), mapName);
                    }
                    reviveAtTrigger = true;
                    break;
                case Map::CANNOT_ENTER_TOO_MANY_INSTANCES:
                    player->SendTransferAborted(at->target_mapId, TRANSFER_ABORT_TOO_MANY_INSTANCES);
                    TC_LOG_DEBUG("maps", "MAP: Player '{}' cannot enter instance map {} because he has exceeded the maximum number of instances per hour.", player->GetName(), at->target_mapId);
                    reviveAtTrigger = true;
                    break;
                case Map::CANNOT_ENTER_MAX_PLAYERS:
                    player->SendTransferAborted(at->target_mapId, TRANSFER_ABORT_MAX_PLAYERS);
                    reviveAtTrigger = true;
                    break;
                case Map::CANNOT_ENTER_ZONE_IN_COMBAT:
                    player->SendTransferAborted(at->target_mapId, TRANSFER_ABORT_ZONE_IN_COMBAT);
                    reviveAtTrigger = true;
                    break;
                default:
                    break;
            }

            if (reviveAtTrigger) // check if the player is touching the areatrigger leading to the map his corpse is on
                if (!player->IsAlive() && player->HasCorpse())
                    if (player->GetCorpseLocation().GetMapId() == at->target_mapId)
                    {
                        player->ResurrectPlayer(0.5f);
                        player->SpawnCorpseBones();
                    }

            return;
        }

        if (Group* group = player->GetGroup())
            if (group->isLFGGroup() && player->GetMap()->IsDungeon())
                teleported = player->TeleportToBGEntryPoint();
    }

    if (!teleported)
        player->TeleportTo(at->target_mapId, at->target_X, at->target_Y, at->target_Z, at->target_Orientation, TELE_TO_NOT_LEAVE_TRANSPORT);
}

/**
 * @brief 处理更新账户数据的请求
 *
 * 当客户端同步账户数据（如宏、界面设置等）到服务器时，处理数据解压和保存。
 * 账户数据用于跨角色共享设置和备份。
 *
 * @param recvData 接收的网络数据包，包含：
 *                 - type: 数据类型（0-7）
 *                 - timestamp: 时间戳
 *                 - decompressedSize: 解压后大小
 *                 - 压缩的数据内容
 *
 * 调用时机：
 * - 玩家修改宏或界面设置时
 * - 客户端发送 CMSG_UPDATE_ACCOUNT_DATA 消息
 *
 * 数据类型：
 * - 宏配置
 * - 界面布局
 * - 快捷键设置
 * - 等等
 *
 * 处理流程：
 * 1. 验证数据类型有效
 * 2. 如果解压大小为0，清除该类型数据
 * 3. 如果解压大小超过限制，拒绝更新
 * 4. 解压缩数据
 * 5. 保存账户数据
 * 6. 发送更新完成确认
 *
 * @see SetAccountData()
 */
void WorldSession::HandleUpdateAccountData(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_UPDATE_ACCOUNT_DATA");

    uint32 type, timestamp, decompressedSize;
    recvData >> type >> timestamp >> decompressedSize;

    TC_LOG_DEBUG("network", "UAD: type {}, time {}, decompressedSize {}", type, timestamp, decompressedSize);

    if (type >= NUM_ACCOUNT_DATA_TYPES)
        return;

    if (decompressedSize == 0)                               // erase
    {
        SetAccountData(AccountDataType(type), 0, "");

        WorldPacket data(SMSG_UPDATE_ACCOUNT_DATA_COMPLETE, 4+4);
        data << uint32(type);
        data << uint32(0);
        SendPacket(&data);

        return;
    }

    if (decompressedSize > 0xFFFF)
    {
        recvData.rfinish();                   // unnneded warning spam in this case
        TC_LOG_ERROR("network", "UAD: Account data packet too big, size {}", decompressedSize);
        return;
    }

    ByteBuffer dest;
    dest.resize(decompressedSize);

    uLongf realSize = decompressedSize;
    if (uncompress(dest.contents(), &realSize, recvData.contents() + recvData.rpos(), recvData.size() - recvData.rpos()) != Z_OK)
    {
        recvData.rfinish();                   // unnneded warning spam in this case
        TC_LOG_ERROR("network", "UAD: Failed to decompress account data");
        return;
    }

    recvData.rfinish();                       // uncompress read (recvData.size() - recvData.rpos())

    std::string adata;
    dest >> adata;

    SetAccountData(AccountDataType(type), timestamp, adata);

    WorldPacket data(SMSG_UPDATE_ACCOUNT_DATA_COMPLETE, 4+4);
    data << uint32(type);
    data << uint32(0);
    SendPacket(&data);
}

/**
 * @brief 处理请求账户数据的请求
 *
 * 当客户端需要获取账户数据时，服务器从数据库读取数据、压缩并发送给客户端。
 *
 * @param recvData 接收的网络数据包，包含数据类型
 *
 * 调用时机：
 * - 玩家登录时
 * - 玩家请求同步设置时
 * - 客户端发送 CMSG_REQUEST_ACCOUNT_DATA 消息
 *
 * 处理流程：
 * 1. 验证数据类型有效
 * 2. 获取账户数据
 * 3. 压缩数据
 * 4. 发送数据给客户端
 *
 * @see GetAccountData()
 */
void WorldSession::HandleRequestAccountData(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_REQUEST_ACCOUNT_DATA");

    uint32 type;
    recvData >> type;

    TC_LOG_DEBUG("network", "RAD: type {}", type);

    if (type >= NUM_ACCOUNT_DATA_TYPES)
        return;

    AccountData* adata = GetAccountData(AccountDataType(type));

    uint32 size = adata->Data.size();

    uLongf destSize = compressBound(size);

    ByteBuffer dest;
    dest.resize(destSize);

    if (size && compress(dest.contents(), &destSize, (uint8 const*)adata->Data.c_str(), size) != Z_OK)
    {
        TC_LOG_DEBUG("network", "RAD: Failed to compress account data");
        return;
    }

    dest.resize(destSize);

    WorldPacket data(SMSG_UPDATE_ACCOUNT_DATA, 8+4+4+4+destSize);
    data << uint64(_player ? _player->GetGUID() : ObjectGuid::Empty);
    data << uint32(type);                                   // type (0-7)
    data << uint32(adata->Time);                            // unix time
    data << uint32(size);                                   // decompressed length
    data.append(dest);                                      // compressed data
    SendPacket(&data);
}

/**
 * @brief 处理设置动作按钮的请求
 *
 * 当玩家拖拽技能或物品到动作条时，更新动作按钮配置。
 * 也用于移除动作按钮（设置数据为0）。
 *
 * @param recvData 接收的网络数据包，包含：
 *                 - button: 按钮位置索引
 *                 - packetData: 按钮数据（包含动作ID和类型）
 *
 * 调用时机：
 * - 玩家拖拽技能/物品到动作条时
 * - 玩家右键点击动作按钮移除时
 * - 客户端发送 CMSG_SET_ACTION_BUTTON 消息
 *
 * @see Player::addActionButton()
 * @see Player::removeActionButton()
 */
void WorldSession::HandleSetActionButtonOpcode(WorldPacket& recvData)
{
    uint8 button;
    uint32 packetData;
    recvData >> button >> packetData;
    TC_LOG_DEBUG("network", "CMSG_SET_ACTION_BUTTON Button: {} Data: {}", button, packetData);

    if (!packetData)
        GetPlayer()->removeActionButton(button);
    else
        GetPlayer()->addActionButton(button, ACTION_BUTTON_ACTION(packetData), ACTION_BUTTON_TYPE(packetData));
}

/**
 * @brief 处理过场动画完成消息
 *
 * 当过场动画播放完毕时，客户端发送此消息。
 * 服务器清理过场动画相关的资源，如视觉路点NPC。
 *
 * @param packet 完成过场动画数据包（空数据包）
 *
 * @see CinematicMgr::EndCinematic()
 */
void WorldSession::HandleCompleteCinematic(WorldPackets::Misc::CompleteCinematic& /*packet*/)
{
    // If player has sight bound to visual waypoint NPC we should remove it
    GetPlayer()->GetCinematicMgr()->EndCinematic();
}

/**
 * @brief 处理下一个过场动画摄像机消息
 *
 * 当过场动画实际开始播放时，客户端发送此消息。
 * 服务器开始服务端的过场动画处理流程。
 *
 * @param packet 下一个摄像机数据包（空数据包）
 *
 * @note 客户端在过场动画开始时发送此消息
 *
 * @see CinematicMgr::BeginCinematic()
 */
void WorldSession::HandleNextCinematicCamera(WorldPackets::Misc::NextCinematicCamera& /*packet*/)
{
    // Sent by client when cinematic actually begun. So we begin the server side process
    GetPlayer()->GetCinematicMgr()->BeginCinematic();
}

/**
 * @brief 处理电影完成消息
 *
 * 当电影播放完毕时，客户端发送此消息。
 * 服务器触发电影完成脚本事件。
 *
 * @param packet 完成电影数据包（空数据包）
 *
 * @see ScriptMgr::OnMovieComplete()
 */
void WorldSession::HandleCompleteMovie(WorldPackets::Misc::CompleteMovie& /*packet*/)
{
    uint32 movie = _player->GetMovie();
    if (!movie)
        return;

    _player->SetMovie(0);
    sScriptMgr->OnMovieComplete(_player, movie);
}

/**
 * @brief 处理动作条显示切换
 *
 * 当玩家切换动作条的显示/隐藏状态时，保存设置到角色数据。
 *
 * @param recvData 接收的网络数据包，包含动作条显示标志
 *
 * @see Player::SetByteValue()
 */
void WorldSession::HandleSetActionBarToggles(WorldPacket& recvData)
{
    uint8 actionBar;
    recvData >> actionBar;

    if (!GetPlayer())                                        // ignore until not logged (check needed because STATUS_AUTHED)
    {
        if (actionBar != 0)
            TC_LOG_ERROR("network", "WorldSession::HandleSetActionBarToggles in not logged state with value: {}, ignored", uint32(actionBar));
        return;
    }

    GetPlayer()->SetByteValue(PLAYER_FIELD_BYTES, PLAYER_FIELD_BYTES_OFFSET_ACTION_BAR_TOGGLES, actionBar);
}

/**
 * @brief 处理查询游戏时间请求
 *
 * 当玩家请求查看游戏时间时，返回总游戏时间和当前等级游戏时间。
 *
 * @param packet 游戏时间请求包，包含是否触发脚本事件标志
 *
 * @see Player::GetTotalPlayedTime()
 * @see Player::GetLevelPlayedTime()
 */
void WorldSession::HandlePlayedTime(WorldPackets::Character::PlayedTimeClient& packet)
{
    WorldPackets::Character::PlayedTime playedTime;
    playedTime.TotalTime = _player->GetTotalPlayedTime();
    playedTime.LevelTime = _player->GetLevelPlayedTime();
    playedTime.TriggerScriptEvent = packet.TriggerScriptEvent;  // 0-1 - will not show in chat frame
    SendPacket(playedTime.Write());
}

/**
 * @brief 处理观察玩家装备请求
 *
 * 当玩家右键点击其他玩家并选择"观察"时，服务器发送目标玩家的装备和天赋信息。
 *
 * @param recvData 接收的网络数据包，包含目标玩家的GUID
 *
 * 调用时机：
 * - 玩家右键点击其他玩家选择"观察"时
 * - 客户端发送 CMSG_INSPECT 消息
 *
 * 处理流程：
 * 1. 查找目标玩家
 * 2. 验证距离在观察范围内
 * 3. 验证不能观察敌对目标
 * 4. 发送天赋和装备信息
 *
 * 权限检查：
 * - GM可以观察所有玩家
 * - 普通玩家只能观察同阵营玩家的详细天赋
 *
 * @see Player::BuildPlayerTalentsInfoData()
 * @see Player::BuildEnchantmentsInfoData()
 */
void WorldSession::HandleInspectOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_INSPECT");

    Player* player = ObjectAccessor::GetPlayer(*_player, guid);
    if (!player)
    {
        TC_LOG_DEBUG("network", "CMSG_INSPECT: No player found from {}", guid.ToString());
        return;
    }

    if (!GetPlayer()->IsWithinDistInMap(player, INSPECT_DISTANCE, false))
        return;

    if (GetPlayer()->IsValidAttackTarget(player))
        return;

    uint32 talent_points = 0x47;
    uint32 guid_size = player->GetPackGUID().size();
    WorldPacket data(SMSG_INSPECT_TALENT, guid_size+4+talent_points);
    data << player->GetPackGUID();

    if (GetPlayer()->CanBeGameMaster() || sWorld->getIntConfig(CONFIG_TALENTS_INSPECTING) + (GetPlayer()->GetTeamId() == player->GetTeamId()) > 1)
        player->BuildPlayerTalentsInfoData(&data);
    else
    {
        data << uint32(0);                                  // unspentTalentPoints
        data << uint8(0);                                   // talentGroupCount
        data << uint8(0);                                   // talentGroupIndex
    }

    player->BuildEnchantmentsInfoData(&data);
    SendPacket(&data);
}

/**
 * @brief 处理观察荣誉统计请求
 *
 * 当玩家查看其他玩家的荣誉统计时，发送目标的荣誉点数和击杀数据。
 *
 * @param recvData 接收的网络数据包，包含目标玩家的GUID
 *
 * @see HandleInspectOpcode()
 */
void WorldSession::HandleInspectHonorStatsOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    Player* player = ObjectAccessor::GetPlayer(*_player, guid);

    if (!player)
    {
        TC_LOG_DEBUG("network", "MSG_INSPECT_HONOR_STATS: No player found from {}", guid.ToString());
        return;
    }

    if (!GetPlayer()->IsWithinDistInMap(player, INSPECT_DISTANCE, false))
        return;

    if (GetPlayer()->IsValidAttackTarget(player))
        return;

    WorldPacket data(MSG_INSPECT_HONOR_STATS, 8+1+4*4);
    data << uint64(player->GetGUID());
    data << uint8(player->GetHonorPoints());
    data << uint32(player->GetUInt32Value(PLAYER_FIELD_KILLS));
    data << uint32(player->GetUInt32Value(PLAYER_FIELD_TODAY_CONTRIBUTION));
    data << uint32(player->GetUInt32Value(PLAYER_FIELD_YESTERDAY_CONTRIBUTION));
    data << uint32(player->GetUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS));
    SendPacket(&data);
}

/**
 * @brief 处理世界传送请求
 *
 * GM专用命令，允许直接传送到指定坐标。
 * 需要特定的RBAC权限才能使用。
 *
 * @param worldTeleport 世界传送数据包，包含时间、地图ID、坐标和朝向
 *
 * @see Player::TeleportTo()
 */
void WorldSession::HandleWorldTeleportOpcode(WorldPackets::Misc::WorldTeleport& worldTeleport)
{
    if (_player->IsInFlight())
    {
        TC_LOG_DEBUG("network", "Player '{}' ({}) in flight, ignore worldport command.",
            _player->GetName(), _player->GetGUID().ToString());
        return;
    }

    WorldLocation loc(worldTeleport.MapID, worldTeleport.Pos);
    loc.SetOrientation(worldTeleport.Facing);
    TC_LOG_DEBUG("network", "CMSG_WORLD_TELEPORT: Player = {}, time = {}, map = {}, pos = {}",
        _player->GetName(), worldTeleport.Time, worldTeleport.MapID, loc.ToString());

    if (HasPermission(rbac::RBAC_PERM_OPCODE_WORLD_TELEPORT))
        _player->TeleportTo(loc);
    else
        SendNotification(LANG_YOU_NOT_HAVE_PERMISSION);
}

/**
 * @brief 处理WhoIs查询请求
 *
 * GM专用命令，查询指定角色的账户信息。
 * 需要特定的RBAC权限才能使用。
 *
 * @param recvData 接收的网络数据包，包含角色名称
 *
 * 返回信息：
 * - 角色账户名
 * - 注册邮箱
 * - 最后登录IP
 *
 * @see Player::GetSession()
 */
void WorldSession::HandleWhoIsOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "Received opcode CMSG_WHOIS");
    std::string charname;
    recvData >> charname;

    if (!HasPermission(rbac::RBAC_PERM_OPCODE_WHOIS))
    {
        SendNotification(LANG_YOU_NOT_HAVE_PERMISSION);
        return;
    }

    if (charname.empty() || !normalizePlayerName (charname))
    {
        SendNotification(LANG_NEED_CHARACTER_NAME);
        return;
    }

    Player* player = ObjectAccessor::FindConnectedPlayerByName(charname);

    if (!player)
    {
        SendNotification(LANG_PLAYER_NOT_EXIST_OR_OFFLINE, charname.c_str());
        return;
    }

    uint32 accid = player->GetSession()->GetAccountId();

    LoginDatabasePreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_WHOIS);

    stmt->setUInt32(0, accid);

    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if (!result)
    {
        SendNotification(LANG_ACCOUNT_FOR_PLAYER_NOT_FOUND, charname.c_str());
        return;
    }

    Field* fields = result->Fetch();
    std::string acc = fields[0].GetString();
    if (acc.empty())
        acc = "Unknown";
    std::string email = fields[1].GetString();
    if (email.empty())
        email = "Unknown";
    std::string lastip = fields[2].GetString();
    if (lastip.empty())
        lastip = "Unknown";

    std::string msg = charname + "'s " + "account is " + acc + ", e-mail: " + email + ", last ip: " + lastip;

    WorldPacket data(SMSG_WHOIS, msg.size()+1);
    data << msg;
    SendPacket(&data);

    TC_LOG_DEBUG("network", "Received whois command from player {} for character {}",
        GetPlayer()->GetName(), charname);
}

/**
 * @brief 处理投诉请求
 *
 * 当玩家举报其他玩家的垃圾信息或不当行为时，记录投诉信息。
 * 支持邮件投诉和聊天投诉两种类型。
 *
 * @param recvData 接收的网络数据包，包含：
 *                 - spam_type: 投诉类型（0=邮件，1=聊天）
 *                 - spammer_guid: 被投诉玩家GUID
 *                 - 其他类型相关的数据
 *
 * 处理效果：
 * - 邮件投诉：自动删除该发送者的所有邮件
 * - 聊天投诉：自动忽略该发送者的所有聊天消息直到登出
 *
 * @note 服务器仅记录投诉信息，不做进一步处理
 */
void WorldSession::HandleComplainOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_COMPLAIN");

    uint8 spam_type;                                        // 0 - mail, 1 - chat
    ObjectGuid spammer_guid;
    uint32 unk1 = 0;
    uint32 unk2 = 0;
    uint32 unk3 = 0;
    uint32 unk4 = 0;
    std::string description = "";
    recvData >> spam_type;                                 // unk 0x01 const, may be spam type (mail/chat)
    recvData >> spammer_guid;                              // player guid
    switch (spam_type)
    {
        case 0:
            recvData >> unk1;                              // const 0
            recvData >> unk2;                              // probably mail id
            recvData >> unk3;                              // const 0
            break;
        case 1:
            recvData >> unk1;                              // probably language
            recvData >> unk2;                              // message type?
            recvData >> unk3;                              // probably channel id
            recvData >> unk4;                              // time
            recvData >> description;                       // spam description string (messagetype, channel name, player name, message)
            break;
    }

    // NOTE: all chat messages from this spammer automatically ignored by spam reporter until logout in case chat spam.
    // 如果是邮件垃圾信息，客户端会自动删除该发送者的所有邮件
    // if it's mail spam - ALL mails from this spammer automatically removed by client

    // 发送投诉已收到消息 / Complaint Received message
    WorldPacket data(SMSG_COMPLAIN_RESULT, 1);
    data << uint8(0);
    SendPacket(&data);

    TC_LOG_DEBUG("network", "REPORT SPAM: type {}, {}, unk1 {}, unk2 {}, unk3 {}, unk4 {}, message {}",
        spam_type, spammer_guid.ToString(), unk1, unk2, unk3, unk4, description);
}

/**
 * @brief 处理服务器分割状态查询
 *
 * 查询服务器是否处于分割状态。用于暴雪官方服务器分割通知。
 * 私服通常返回正常状态。
 *
 * @param recvData 接收的网络数据包，包含未知参数
 *
 * 状态码：
 * - 0x0: 服务器正常
 * - 0x1: 服务器已分割
 * - 0x2: 服务器分割待定
 */
void WorldSession::HandleRealmSplitOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_REALM_SPLIT");

    uint32 unk;
    std::string split_date = "01/01/01";
    recvData >> unk;

    WorldPacket data(SMSG_REALM_SPLIT, 4+4+split_date.size()+1);
    data << unk;
    data << uint32(0x00000000);                             // realm split state
    // split states:
    // 0x0 realm normal
    // 0x1 realm split
    // 0x2 realm split pending
    data << split_date;
    SendPacket(&data);
    //TC_LOG_DEBUG("response sent {}", unk);
}

/**
 * @brief 处理远视技能请求
 *
 * 当玩家使用远视技能切换视角时，更新玩家的观察者设置。
 *
 * @param recvData 接收的网络数据包，包含是否应用远视
 *
 * @see Player::SetSeer()
 * @see Player::UpdateVisibilityForPlayer()
 */
void WorldSession::HandleFarSightOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_FAR_SIGHT");

    bool apply;
    recvData >> apply;

    if (apply)
    {
        TC_LOG_DEBUG("network", "Added FarSight {} to player {}", _player->GetGuidValue(PLAYER_FARSIGHT).ToString(), _player->GetGUID().ToString());
        if (WorldObject* target = _player->GetViewpoint())
            _player->SetSeer(target);
        else
            TC_LOG_DEBUG("network", "Player {} {} requests non-existing seer {}", _player->GetName(), _player->GetGUID().ToString(), _player->GetGuidValue(PLAYER_FARSIGHT).ToString());
    }
    else
    {
        TC_LOG_DEBUG("network", "Player {} set vision to self", _player->GetGUID().ToString());
        _player->SetSeer(_player);
    }

    GetPlayer()->UpdateVisibilityForPlayer();
}

/**
 * @brief 处理设置称号请求
 *
 * 当玩家选择显示哪个称号时，更新玩家当前显示的称号。
 *
 * @param recvData 接收的网络数据包，包含称号ID（-1表示无称号）
 *
 * @see Player::HasTitle()
 */
void WorldSession::HandleSetTitleOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_SET_TITLE");

    int32 title;
    recvData >> title;

    // -1 at none
    if (title > 0 && title < MAX_TITLE_INDEX)
    {
       if (!GetPlayer()->HasTitle(title))
            return;
    }
    else
        title = 0;

    GetPlayer()->SetUInt32Value(PLAYER_CHOSEN_TITLE, title);
}

/**
 * @brief 处理重置副本请求
 *
 * 当玩家请求重置所有绑定的副本时，重置副本进度。
 * 如果玩家在团队中，只有队长可以执行此操作。
 *
 * @param recvData 接收的网络数据包（空数据包）
 *
 * 调用时机：
 * - 玩家右键点击自己的头像选择"重置所有副本"时
 * - 客户端发送 CMSG_RESET_INSTANCES 消息
 *
 * @see Group::ResetInstances()
 * @see Player::ResetInstances()
 */
void WorldSession::HandleResetInstancesOpcode(WorldPacket& /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_RESET_INSTANCES");

    if (Group* group = _player->GetGroup())
    {
        if (group->IsLeader(_player->GetGUID()))
            group->ResetInstances(INSTANCE_RESET_ALL, false, _player);
    }
    else
        _player->ResetInstances(INSTANCE_RESET_ALL, false);
}

/**
 * @brief 处理设置地下城难度请求
 *
 * 当玩家更改地下城难度设置时，更新难度并重置受影响的副本。
 * 如果玩家在团队中，只有队长可以更改设置。
 *
 * @param recvData 接收的网络数据包，包含难度模式
 *
 * 难度模式：
 * - 0: 普通模式
 * - 1: 英雄模式
 *
 * 处理流程：
 * 1. 验证难度值有效
 * 2. 验证玩家不在副本中
 * 3. 如果在团队中，验证所有成员都不在副本中
 * 4. 重置副本进度
 * 5. 更新难度设置
 *
 * @see Group::SetDungeonDifficulty()
 * @see Player::SetDungeonDifficulty()
 */
void WorldSession::HandleSetDungeonDifficultyOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "MSG_SET_DUNGEON_DIFFICULTY");

    uint32 mode;
    recvData >> mode;

    if (mode >= MAX_DUNGEON_DIFFICULTY)
    {
        TC_LOG_DEBUG("network", "WorldSession::HandleSetDungeonDifficultyOpcode: player {} sent an invalid instance mode {}!", _player->GetGUID().ToString(), mode);
        return;
    }

    if (Difficulty(mode) == _player->GetDungeonDifficulty())
        return;

    // cannot reset while in an instance
    Map* map = _player->FindMap();
    if (map && map->IsDungeon())
    {
        TC_LOG_DEBUG("network", "WorldSession::HandleSetDungeonDifficultyOpcode: player (Name: {}, {}) tried to reset the instance while player is inside!",
            _player->GetName(), _player->GetGUID().ToString());
        return;
    }

    Group* group = _player->GetGroup();
    if (group)
    {
        if (group->IsLeader(_player->GetGUID()))
        {
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* groupGuy = itr->GetSource();
                if (!groupGuy)
                    continue;

                if (!groupGuy->IsInWorld())
                    return;

                if (groupGuy->GetMap()->IsNonRaidDungeon())
                {
                    TC_LOG_DEBUG("network", "WorldSession::HandleSetDungeonDifficultyOpcode: player {} tried to reset the instance while group member (Name: {}, {}) is inside!",
                        _player->GetGUID().ToString(), groupGuy->GetName(), groupGuy->GetGUID().ToString());
                    return;
                }
            }
            // the difficulty is set even if the instances can't be reset
            //_player->SendDungeonDifficulty(true);
            group->ResetInstances(INSTANCE_RESET_CHANGE_DIFFICULTY, false, _player);
            group->SetDungeonDifficulty(Difficulty(mode));
        }
    }
    else
    {
        _player->ResetInstances(INSTANCE_RESET_CHANGE_DIFFICULTY, false);
        _player->SetDungeonDifficulty(Difficulty(mode));
    }
}

/**
 * @brief 处理设置团队副本难度请求
 *
 * 当玩家更改团队副本难度设置时，更新难度并重置受影响的副本。
 * 如果玩家在团队中，只有队长可以更改设置。
 *
 * @param recvData 接收的网络数据包，包含难度模式
 *
 * 难度模式：
 * - 0: 10人普通
 * - 1: 25人普通
 * - 2: 10人英雄
 * - 3: 25人英雄
 *
 * @see HandleSetDungeonDifficultyOpcode()
 */
void WorldSession::HandleSetRaidDifficultyOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "MSG_SET_RAID_DIFFICULTY");

    uint32 mode;
    recvData >> mode;

    if (mode >= MAX_RAID_DIFFICULTY)
    {
        TC_LOG_ERROR("network", "WorldSession::HandleSetRaidDifficultyOpcode: player {} sent an invalid instance mode {}!", _player->GetGUID().ToString(), mode);
        return;
    }

    // cannot reset while in an instance
    Map* map = _player->FindMap();
    if (map && map->IsDungeon())
    {
        TC_LOG_DEBUG("network", "WorldSession::HandleSetRaidDifficultyOpcode: player {} tried to reset the instance while inside!", _player->GetGUID().ToString());
        return;
    }

    if (Difficulty(mode) == _player->GetRaidDifficulty())
        return;

    Group* group = _player->GetGroup();
    if (group)
    {
        if (group->IsLeader(_player->GetGUID()))
        {
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* groupGuy = itr->GetSource();
                if (!groupGuy)
                    continue;

                if (!groupGuy->IsInWorld())
                    return;

                if (groupGuy->GetMap()->IsRaid())
                {
                    TC_LOG_DEBUG("network", "WorldSession::HandleSetRaidDifficultyOpcode: player {} tried to reset the instance while inside!", _player->GetGUID().ToString());
                    return;
                }
            }
            // the difficulty is set even if the instances can't be reset
            //_player->SendDungeonDifficulty(true);
            group->ResetInstances(INSTANCE_RESET_CHANGE_DIFFICULTY, true, _player);
            group->SetRaidDifficulty(Difficulty(mode));
        }
    }
    else
    {
        _player->ResetInstances(INSTANCE_RESET_CHANGE_DIFFICULTY, true);
        _player->SetRaidDifficulty(Difficulty(mode));
    }
}

/**
 * @brief 处理设置飞行基准测试模式
 *
 * 当玩家使用/timetest命令时，切换飞行基准测试模式。
 * 用于测试飞行路线的性能。
 *
 * @param recvData 接收的网络数据包，包含模式标志（0=关闭，1=开启）
 *
 * @see PLAYER_FLAGS_TAXI_BENCHMARK
 */
void WorldSession::HandleSetTaxiBenchmarkOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_SET_TAXI_BENCHMARK_MODE");

    uint8 mode;
    recvData >> mode;

    mode ? _player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_TAXI_BENCHMARK) : _player->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_TAXI_BENCHMARK);

    TC_LOG_DEBUG("network", "Client used \"/timetest {}\" command", mode);
}

/**
 * @brief 处理查询观察成就请求
 *
 * 当玩家观察其他玩家时，请求目标玩家的成就数据。
 *
 * @param recvData 接收的网络数据包，包含目标玩家的GUID
 *
 * @see Player::SendRespondInspectAchievements()
 */
void WorldSession::HandleQueryInspectAchievements(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    TC_LOG_DEBUG("network", "CMSG_QUERY_INSPECT_ACHIEVEMENTS [{}] Inspected Player [{}]", _player->GetGUID().ToString(), guid.ToString());
    Player* player = ObjectAccessor::GetPlayer(*_player, guid);
    if (!player)
        return;

    if (!GetPlayer()->IsWithinDistInMap(player, INSPECT_DISTANCE, false))
        return;

    if (GetPlayer()->IsValidAttackTarget(player))
        return;

    player->SendRespondInspectAchievements(_player);
}

/**
 * @brief 处理世界状态UI时间更新请求
 *
 * 客户端请求当前游戏时间用于UI显示。
 *
 * @param recvData 接收的网络数据包（空数据包）
 *
 * @see GameTime::GetGameTime()
 */
void WorldSession::HandleWorldStateUITimerUpdate(WorldPacket& /*recvData*/)
{
    // empty opcode
    TC_LOG_DEBUG("network", "WORLD: CMSG_WORLD_STATE_UI_TIMER_UPDATE");

    WorldPackets::Misc::UITime response;
    response.Time = GameTime::GetGameTime();
    SendPacket(response.Write());
}

/**
 * @brief 处理准备接收账户数据时间戳
 *
 * 客户端表示已准备好接收账户数据时间戳。
 * 服务器发送所有账户数据类型的时间戳。
 *
 * @param recvData 接收的网络数据包（空数据包）
 *
 * @see SendAccountDataTimes()
 */
void WorldSession::HandleReadyForAccountDataTimes(WorldPacket& /*recvData*/)
{
    // empty opcode
    TC_LOG_DEBUG("network", "WORLD: CMSG_READY_FOR_ACCOUNT_DATA_TIMES");

    SendAccountDataTimes(GLOBAL_CACHE_MASK);
}

/**
 * @brief 发送相位偏移设置给客户端
 *
 * 设置玩家的相位偏移，用于多相位系统。
 * 不同相位的玩家看不到彼此。
 *
 * @param PhaseShift 相位偏移值
 *
 * @see SMSG_SET_PHASE_SHIFT
 */
void WorldSession::SendSetPhaseShift(uint32 PhaseShift)
{
    WorldPacket data(SMSG_SET_PHASE_SHIFT, 4);
    data << uint32(PhaseShift);
    SendPacket(&data);
}

/**
 * @brief 处理区域灵魂医者查询请求
 *
 * 当玩家与战场或战场的灵魂医者交互时，查询复活等待时间。
 *
 * @param recvData 接收的网络数据包，包含灵魂医者的GUID
 *
 * @see BattlegroundMgr::SendAreaSpiritHealerQueryOpcode()
 */
// Battlefield and Battleground
void WorldSession::HandleAreaSpiritHealerQueryOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_AREA_SPIRIT_HEALER_QUERY");

    Battleground* bg = _player->GetBattleground();

    ObjectGuid guid;
    recvData >> guid;

    Creature* unit = GetPlayer()->GetMap()->GetCreature(guid);
    if (!unit)
        return;

    if (!unit->IsSpiritService())                            // it's not spirit service
        return;

    if (bg)
        sBattlegroundMgr->SendAreaSpiritHealerQueryOpcode(_player, bg, guid);

    if (Battlefield* bf = sBattlefieldMgr->GetBattlefieldToZoneId(_player->GetZoneId()))
        bf->SendAreaSpiritHealerQueryOpcode(_player, guid);
}

/**
 * @brief 处理区域灵魂医者排队请求
 *
 * 当玩家点击灵魂医者请求复活时，将玩家加入复活队列。
 *
 * @param recvData 接收的网络数据包，包含灵魂医者的GUID
 *
 * @see Battleground::AddPlayerToResurrectQueue()
 */
void WorldSession::HandleAreaSpiritHealerQueueOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_AREA_SPIRIT_HEALER_QUEUE");

    Battleground* bg = _player->GetBattleground();

    ObjectGuid guid;
    recvData >> guid;

    Creature* unit = GetPlayer()->GetMap()->GetCreature(guid);
    if (!unit)
        return;

    if (!unit->IsSpiritService())                            // it's not spirit service
        return;

    if (bg)
        bg->AddPlayerToResurrectQueue(guid, _player->GetGUID());

    if (Battlefield* bf = sBattlefieldMgr->GetBattlefieldToZoneId(_player->GetZoneId()))
        bf->AddPlayerToResurrectQueue(guid, _player->GetGUID());
}

/**
 * @brief 处理炉石和复活请求
 *
 * 在冬拥湖等战场中，当玩家请求离开时，传送回绑定炉石位置并复活。
 *
 * @param recvData 接收的网络数据包（空数据包）
 *
 * @see Player::TeleportTo()
 * @see Player::ResurrectPlayer()
 */
void WorldSession::HandleHearthAndResurrect(WorldPacket& /*recvData*/)
{
    if (_player->IsInFlight())
        return;

    if (Battlefield* bf = sBattlefieldMgr->GetBattlefieldToZoneId(_player->GetZoneId()))
    {
        bf->PlayerAskToLeave(_player);
        return;
    }

    AreaTableEntry const* atEntry = sAreaTableStore.LookupEntry(_player->GetAreaId());
    if (!atEntry || !(atEntry->Flags & AREA_FLAG_WINTERGRASP_2))
        return;

    _player->BuildPlayerRepop();
    _player->ResurrectPlayer(1.0f);
    _player->TeleportTo(_player->m_homebindMapId, _player->m_homebindX, _player->m_homebindY, _player->m_homebindZ, _player->GetOrientation());
}

/**
 * @brief 处理副本锁定响应
 *
 * 当玩家尝试进入已有进度的副本时，显示确认对话框。
 * 玩家可以选择接受绑定或传送到墓地。
 *
 * @param recvPacket 接收的网络数据包，包含接受标志（0=拒绝，1=接受）
 *
 * @see Player::BindToInstance()
 * @see Player::RepopAtGraveyard()
 */
void WorldSession::HandleInstanceLockResponse(WorldPacket& recvPacket)
{
    uint8 accept;
    recvPacket >> accept;

    if (!_player->HasPendingBind())
    {
        TC_LOG_INFO("network", "InstanceLockResponse: Player {} {} tried to bind himself/teleport to graveyard without a pending bind!",
            _player->GetName(), _player->GetGUID().ToString());
        return;
    }

    if (accept)
        _player->BindToInstance();
    else
        _player->RepopAtGraveyard();

    _player->SetPendingBind(0, 0);
}

/**
 * @brief 处理更新导弹轨迹请求
 *
 * 当玩家施放有弹道效果的法术时，客户端发送导弹轨迹数据。
 * 服务器更新法术目标的轨迹信息。
 *
 * @param recvPacket 接收的网络数据包，包含：
 *                   - guid: 施法者GUID
 *                   - spellId: 法术ID
 *                   - elevation: 仰角
 *                   - speed: 速度
 *                   - firePos: 发射位置
 *                   - impactPos: 落点位置
 *                   - moveStop: 是否停止移动
 *
 * 处理流程：
 * 1. 获取施法者和当前施放的法术
 * 2. 验证法术匹配
 * 3. 更新法术目标的发射和落点位置
 * 4. 设置弹道高度和速度
 * 5. 如果包含移动停止标志，处理移动数据包
 *
 * @see Spell::m_targets
 * @see HandleMovementOpcodes()
 */
void WorldSession::HandleUpdateMissileTrajectory(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_UPDATE_MISSILE_TRAJECTORY");

    ObjectGuid guid;
    uint32 spellId;
    float elevation, speed;
    TaggedPosition<Position::XYZ> firePos;
    TaggedPosition<Position::XYZ> impactPos;
    uint8 moveStop;

    recvPacket >> guid >> spellId >> elevation >> speed;
    recvPacket >> firePos;
    recvPacket >> impactPos;
    recvPacket >> moveStop;

    Unit* caster = ObjectAccessor::GetUnit(*_player, guid);
    Spell* spell = caster ? caster->GetCurrentSpell(CURRENT_GENERIC_SPELL) : nullptr;
    if (!spell || spell->m_spellInfo->Id != spellId || !spell->m_targets.HasDst() || !spell->m_targets.HasSrc())
    {
        recvPacket.rfinish();
        return;
    }

    spell->m_targets.ModSrc(firePos);
    spell->m_targets.ModDst(impactPos);

    spell->m_targets.SetElevation(elevation);
    spell->m_targets.SetSpeed(speed);

    if (moveStop)
    {
        uint32 opcode;
        recvPacket >> opcode;
        recvPacket.SetOpcode(opcode);
        HandleMovementOpcodes(recvPacket);
    }
}
