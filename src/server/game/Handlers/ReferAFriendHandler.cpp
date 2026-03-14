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
 * @file ReferAFriendHandler.cpp
 * @brief 招募好友(Recruit-A-Friend, RaF) 功能的数据包处理器
 *
 * 本文件实现了招募好友系统的核心网络消息处理逻辑，主要功能包括：
 * - 处理玩家赠送等级请求 (CMSG_GRANT_LEVEL)
 * - 处理玩家接受赠送等级 (CMSG_ACCEPT_LEVEL_GRANT)
 *
 * 招募好友系统允许老玩家招募新玩家，当新玩家通过招募链接创建账号后，
 * 双方可以获得额外的经验值加成和赠送等级等福利。
 *
 * @see WorldSession
 * @see Player::GetGrantableLevels
 */

#include "WorldSession.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "World.h"

/**
 * @brief 处理玩家赠送等级请求 (CMSG_GRANT_LEVEL)
 *
 * 当招募者尝试向被招募者赠送等级时调用此函数。该函数执行一系列严格的验证，
 * 确保赠送行为符合招募好友系统的规则，然后向目标玩家发送等级赠送提议。
 *
 * @param recvData 客户端发送的数据包，包含目标玩家的 GUID (压缩格式)
 *
 * @note 验证规则包括：
 *   1. 目标玩家必须存在且在线
 *   2. 赠送者必须拥有可赠送的等级数量
 *   3. 目标玩家必须是被赠送者招募的账号
 *   4. 双方阵营必须相同(除非服务器配置允许跨阵营交互)
 *   5. 目标玩家等级不能超过赠送者
 *   6. 目标玩家等级不能超过 RaF 系统的最大等级限制
 *   7. 双方必须在同一队伍或团队中
 *
 * @note 性能说明：
 *   - 使用 ObjectAccessor::GetPlayer 进行玩家查找，时间复杂度 O(1)
 *   - 所有验证都是轻量级的数值比较，不会产生性能瓶颈
 *
 * @see WorldSession::HandleAcceptGrantLevel 接受赠送等级的处理函数
 * @see Player::GetGrantableLevels 获取可赠送等级数量
 * @see SMSG_PROPOSE_LEVEL_GRANT 服务端发送的等级赠送提议消息
 */
void WorldSession::HandleGrantLevel(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_GRANT_LEVEL");

    // 从数据包中读取目标玩家的 GUID (压缩格式)
    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    // 通过 GUID 查找目标玩家对象
    Player* target = ObjectAccessor::GetPlayer(*_player, guid);

    // === 开始一系列合法性验证 ===
    // 获取赠送者当前可赠送的等级数量
    uint8 levels = _player->GetGrantableLevels();
    uint8 error = 0;

    // 验证 1: 目标玩家是否存在
    if (!target)
        error = ERR_REFER_A_FRIEND_NO_TARGET;
    // 验证 2: 是否有足够的可赠送等级
    else if (levels == 0)
        error = ERR_REFER_A_FRIEND_INSUFFICIENT_GRANTABLE_LEVELS;
    // 验证 3: 目标玩家是否是被赠送者招募的账号
    // 注意：赠送者的招募者 ID 必须等于目标玩家的账号 ID
    else if (GetRecruiterId() != target->GetSession()->GetAccountId())
        error = ERR_REFER_A_FRIEND_NOT_REFERRED_BY;
    // 验证 4: 阵营检查 (除非服务器允许跨阵营交互)
    else if (target->GetTeamId() != _player->GetTeamId() && !sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP))
        error = ERR_REFER_A_FRIEND_DIFFERENT_FACTION;
    // 验证 5: 目标玩家等级不能超过赠送者
    else if (target->GetLevel() >= _player->GetLevel())
        error = ERR_REFER_A_FRIEND_TARGET_TOO_HIGH;
    // 验证 6: 目标玩家等级不能超过 RaF 系统配置的最大等级
    else if (target->GetLevel() >= sWorld->getIntConfig(CONFIG_MAX_RECRUIT_A_FRIEND_BONUS_PLAYER_LEVEL))
        error = ERR_REFER_A_FRIEND_GRANT_LEVEL_MAX_I;
    // 验证 7: 双方必须在同一队伍或团队中
    else if (!target->IsInSameRaidWith(_player))
        error = ERR_REFER_A_FRIEND_NOT_IN_GROUP;

    // 如果有任何验证失败，发送错误消息给客户端
    if (error)
    {
        WorldPacket data(SMSG_REFER_A_FRIEND_FAILURE, 24);
        data << uint32(error);
        // 特殊处理：如果是不在同组，附加目标玩家名称
        if (error == ERR_REFER_A_FRIEND_NOT_IN_GROUP)
            data << target->GetName();

        SendPacket(&data);
        return;
    }

    // 所有验证通过，向目标玩家发送等级赠送提议
    // 目标玩家将看到确认对话框，可以选择接受或拒绝
    WorldPacket data2(SMSG_PROPOSE_LEVEL_GRANT, 8);
    data2 << _player->GetPackGUID();  // 附带赠送者的 GUID
    target->SendDirectMessage(&data2);
}

/**
 * @brief 处理玩家接受等级赠送 (CMSG_ACCEPT_LEVEL_GRANT)
 *
 * 当被招募玩家接受赠送等级时调用此函数。该函数验证接受行为的合法性，
 * 扣除赠送者的可赠送等级数量，并为接受者提升一级。
 *
 * @param recvData 客户端发送的数据包，包含赠送者玩家的 GUID (压缩格式)
 *
 * @note 执行流程：
 *   1. 从数据包获取赠送者 GUID 并查找玩家对象
 *   2. 验证赠送者存在且在线
 *   3. 验证接受者的账号 ID 与赠送者的招募者 ID 匹配
 *   	(确保只有被招募者才能接受赠送)
 *   4. 扣除赠送者的可赠送等级数量
 *   5. 标记接受者已通过 RaF 获得等级赠送
 *   6. 为接受者提升一级
 *
 * @note 安全性说明：
 *   - 所有验证失败均采用静默返回，不向客户端发送错误消息
 *   - 这可以防止恶意玩家通过猜测 GUID 进行探测攻击
 *
 * @note 性能说明：
 *   - 玩家查找使用 ObjectAccessor::GetPlayer，时间复杂度 O(1)
 *   - 等级提升操作 GiveLevel 可能触发大量后续逻辑（学习技能、属性重算等）
 *
 * @see WorldSession::HandleGrantLevel 发起赠送等级的处理函数
 * @see Player::GiveLevel 提升玩家等级的核心函数
 * @see Player::SetGrantableLevels 设置可赠送等级数量
 */
void WorldSession::HandleAcceptGrantLevel(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_ACCEPT_LEVEL_GRANT");

    // 从数据包中读取赠送者的 GUID (压缩格式)
    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    // 查找赠送者玩家对象
    Player* other = ObjectAccessor::GetPlayer(*_player, guid);

    // 验证 1: 赠送者必须存在且在线
    if (!(other && other->GetSession()))
        return;

    // 验证 2: 确保接受者确实是被赠送者招募的玩家
    // 接受者的账号 ID 必须等于赠送者的招募者 ID
    if (GetAccountId() != other->GetSession()->GetRecruiterId())
        return;

    // 验证 3: 确保赠送者仍有可赠送的等级
    // 如果有，则扣除一个可赠送等级；否则静默返回
    if (other->GetGrantableLevels())
        other->SetGrantableLevels(other->GetGrantableLevels() - 1);
    else
        return;

    // 标记当前玩家已通过 RaF 系统获得等级赠送
    // 这可能影响某些游戏逻辑（如成就追踪、统计等）
    _player->SetBeenGrantedLevelsFromRaF();

    // 为当前玩家提升一级
    // GiveLevel 内部会处理所有等级提升相关的逻辑：
    // - 更新属性
    // - 学习新技能
    // - 解锁新功能
    // - 触发相关事件等
    _player->GiveLevel(_player->GetLevel() + 1);
}
