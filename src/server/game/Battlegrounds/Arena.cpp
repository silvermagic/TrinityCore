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
 * @file Arena.cpp
 * @brief 竞技场战场类实现文件
 *
 * 本文件实现了竞技场(Arena)类的所有方法，包括:
 * - 竞技场得分数据的序列化
 * - 竞技场比赛的开始、进行和结束流程
 * - 玩家加入和离开竞技场的处理
 * - 竞技场积分和个人评分的计算
 * - 战斗胜利条件的判定和比赛结束处理
 *
 * @see Arena.h
 * @see Battleground
 */

#include "Arena.h"
#include "ArenaScore.h"
#include "ArenaTeamMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldStatePackets.h"

/**
 * @brief 将竞技场得分数据附加到数据包
 *
 * 将玩家的竞技场得分信息序列化到数据包中，
 * 包括玩家GUID、击杀数、队伍ID、伤害和治疗量。
 *
 * @param data 要填充的世界数据包引用
 *
 * @note 调用时机: 比赛结束时发送比赛统计信息
 * @note 性能说明: 时间复杂度 O(1)
 */
void ArenaScore::AppendToPacket(WorldPacket& data)
{
    data << uint64(PlayerGuid);  // 玩家GUID

    data << uint32(KillingBlows);  // 击杀数
    data << uint8(TeamId);         // 队伍ID（联盟/部落）
    data << uint32(DamageDone);    // 造成的伤害总量
    data << uint32(HealingDone);   // 治疗量总量

    BuildObjectivesBlock(data);  // 构建目标数据块
}

/**
 * @brief 构建目标数据块
 *
 * 竞技场中没有特殊目标，因此目标计数始终为0。
 * 此方法可以被子类重写以添加特定竞技场的目标数据。
 *
 * @param data 要填充的世界数据包引用
 *
 * @note 调用时机: AppendToPacket 方法内部调用
 * @note 性能说明: 时间复杂度 O(1)
 */
void ArenaScore::BuildObjectivesBlock(WorldPacket& data)
{
    data << uint32(0); // 目标计数（竞技场中始终为0）
}

/**
 * @brief 构建积分信息数据块
 *
 * 将队伍的积分变化信息序列化到数据包中，
 * 包括积分损失、积分获得和匹配评分。
 * 客户端根据这些数据显示积分变化。
 *
 * @param data 要填充的世界数据包引用
 *
 * @note 调用时机: 比赛结束时发送队伍积分变化
 * @note 性能说明: 时间复杂度 O(1)
 */
void ArenaTeamScore::BuildRatingInfoBlock(WorldPacket& data)
{
    // 计算积分损失（负值取绝对值）
    uint32 ratingLost = std::abs(std::min(RatingChange, 0));
    // 计算积分获得（正值）
    uint32 ratingWon = std::max(RatingChange, 0);

    // 应该发送旧积分和新积分，客户端自行计算变化值
    data << uint32(ratingLost);           // 积分损失
    data << uint32(ratingWon);            // 积分获得
    data << uint32(MatchmakerRating);     // 匹配评分（MMR）
}

/**
 * @brief 构建队伍信息数据块
 *
 * 将队伍名称序列化到数据包中。
 *
 * @param data 要填充的世界数据包引用
 *
 * @note 调用时机: 比赛结束时发送队伍信息
 * @note 性能说明: 时间复杂度 O(n)，n为队名长度
 */
void ArenaTeamScore::BuildTeamInfoBlock(WorldPacket& data)
{
    data << TeamName;  // 队伍名称
}

/**
 * @brief 构造函数 - 初始化竞技场基本设置
 *
 * 设置竞技场比赛开始倒计时的延迟时间和对应的提示消息:
 * - 第一次倒计时: 1分钟
 * - 第二次倒计时: 30秒
 * - 第三次倒计时: 15秒
 * - 第四次倒计时: 比赛开始
 *
 * @note 调用时机: 创建竞技场实例时
 * @note 性能说明: 时间复杂度 O(1)
 */
Arena::Arena()
{
    // 设置各阶段倒计时延迟时间
    StartDelayTimes[BG_STARTING_EVENT_FIRST]  = BG_START_DELAY_1M;   // 1分钟
    StartDelayTimes[BG_STARTING_EVENT_SECOND] = BG_START_DELAY_30S;  // 30秒
    StartDelayTimes[BG_STARTING_EVENT_THIRD]  = BG_START_DELAY_15S;  // 15秒
    StartDelayTimes[BG_STARTING_EVENT_FOURTH] = BG_START_DELAY_NONE; // 无延迟

    // 设置各阶段对应的广播文本ID
    StartMessageIds[BG_STARTING_EVENT_FIRST]  = ARENA_TEXT_START_ONE_MINUTE;        // "1分钟后开始"
    StartMessageIds[BG_STARTING_EVENT_SECOND] = ARENA_TEXT_START_THIRTY_SECONDS;    // "30秒后开始"
    StartMessageIds[BG_STARTING_EVENT_THIRD]  = ARENA_TEXT_START_FIFTEEN_SECONDS;   // "15秒后开始"
    StartMessageIds[BG_STARTING_EVENT_FOURTH] = ARENA_TEXT_START_BATTLE_HAS_BEGUN;  // "比赛开始"
}

/**
 * @brief 添加玩家到竞技场
 *
 * 将玩家加入竞技场比赛，执行以下操作:
 * 1. 检查玩家是否已在战场中
 * 2. 调用基类的添加玩家方法
 * 3. 创建玩家的竞技场得分记录（仅对新加入的玩家）
 * 4. 根据队伍分配对应的旗帜视觉效果（金色方/绿色方）
 * 5. 更新存活玩家数量的世界状态
 *
 * 旗帜分配规则:
 * - 联盟方使用金色旗帜
 * - 部落方使用绿色旗帜
 * - 玩家实际阵营与队伍阵营不同时使用对应的跨阵营旗帜
 *
 * @param player 要添加的玩家指针
 *
 * @note 调用时机: 玩家进入竞技场时
 * @note 性能说明: 时间复杂度 O(1)，但包含施法操作和世界状态更新
 */
void Arena::AddPlayer(Player* player)
{
    // 检查玩家是否已在战场中（避免重复创建得分记录）
    bool const isInBattleground = IsPlayerInBattleground(player->GetGUID());
    Battleground::AddPlayer(player);

    // 为新加入的玩家创建竞技场得分记录
    if (!isInBattleground)
        PlayerScores[player->GetGUID().GetCounter()] = new ArenaScore(player->GetGUID(), player->GetBGTeam());

    // 根据队伍分配旗帜视觉效果
    if (player->GetBGTeam() == ALLIANCE)        // 金色方（联盟方）
    {
        // 如果玩家实际是部落，使用部落金色旗帜
        if (player->GetTeam() == HORDE)
            player->CastSpell(player, SPELL_HORDE_GOLD_FLAG, true);
        else
            // 否则使用联盟金色旗帜
            player->CastSpell(player, SPELL_ALLIANCE_GOLD_FLAG, true);
    }
    else                                        // 绿色方（部落方）
    {
        // 如果玩家实际是部落，使用部落绿色旗帜
        if (player->GetTeam() == HORDE)
            player->CastSpell(player, SPELL_HORDE_GREEN_FLAG, true);
        else
            // 否则使用联盟绿色旗帜
            player->CastSpell(player, SPELL_ALLIANCE_GREEN_FLAG, true);
    }

    // 更新存活玩家数量的世界状态
    UpdateArenaWorldState();
}

/**
 * @brief 从竞技场移除玩家
 *
 * 处理玩家离开竞技场的逻辑:
 * 1. 检查比赛状态，如果正在等待离开则直接返回
 * 2. 更新存活玩家数量的世界状态
 * 3. 检查胜利条件（一方全灭则判定胜负）
 *
 * @param player 要移除的玩家指针（未使用）
 * @param guid 玩家的GUID（未使用）
 * @param team 玩家所属队伍（未使用）
 *
 * @note 调用时机: 玩家离开竞技场或断线时
 * @note 性能说明: 时间复杂度 O(n)，需要计算存活玩家数量
 * @see RemovePlayerAtLeave
 */
void Arena::RemovePlayer(Player* /*player*/, ObjectGuid /*guid*/, uint32 /*team*/)
{
    // 如果比赛状态为等待离开，不再处理
    if (GetStatus() == STATUS_WAIT_LEAVE)
        return;

    // 更新存活玩家数量的世界状态
    UpdateArenaWorldState();
    // 检查是否满足胜利条件
    CheckWinConditions();
}

/**
 * @brief 填充初始世界状态数据包
 *
 * 将竞技场的初始世界状态填充到数据包中，
 * 主要包括双方队伍的初始存活玩家数量。
 * 绿色方对应部落，金色方对应联盟。
 *
 * @param packet 世界状态初始化数据包引用
 *
 * @note 调用时机: 玩家进入竞技场时，用于同步初始状态
 * @note 性能说明: 时间复杂度 O(n)，需要统计存活玩家数量
 */
void Arena::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    // 绿色方存活玩家数量（部落方）
    packet.Worldstates.emplace_back(ARENA_WORLD_STATE_ALIVE_PLAYERS_GREEN, GetAlivePlayersCountByTeam(HORDE));
    // 金色方存活玩家数量（联盟方）
    packet.Worldstates.emplace_back(ARENA_WORLD_STATE_ALIVE_PLAYERS_GOLD, GetAlivePlayersCountByTeam(ALLIANCE));
}

/**
 * @brief 更新竞技场世界状态
 *
 * 更新客户端显示的双方队伍存活玩家数量。
 * 绿色方对应部落，金色方对应联盟。
 * 通过世界状态机制同步到所有客户端。
 *
 * @note 调用时机: 玩家加入、离开、死亡时调用
 * @note 性能说明: 时间复杂度 O(n)，需要统计存活玩家数量并更新世界状态
 */
void Arena::UpdateArenaWorldState()
{
    // 更新绿色方存活玩家数量（部落方）
    UpdateWorldState(ARENA_WORLD_STATE_ALIVE_PLAYERS_GREEN, GetAlivePlayersCountByTeam(HORDE));
    // 更新金色方存活玩家数量（联盟方）
    UpdateWorldState(ARENA_WORLD_STATE_ALIVE_PLAYERS_GOLD, GetAlivePlayersCountByTeam(ALLIANCE));
}

/**
 * @brief 处理玩家击杀事件
 *
 * 当玩家在竞技场中被击杀时调用:
 * 1. 检查比赛是否正在进行
 * 2. 调用基类的击杀处理方法
 * 3. 更新存活玩家数量的世界状态
 * 4. 检查胜利条件（一方全灭则判定胜负）
 *
 * @param player 被击杀的玩家指针
 * @param killer 击杀者玩家指针
 *
 * @note 调用时机: 玩家在竞技场中死亡时
 * @note 性能说明: 时间复杂度 O(n)，需要统计存活玩家数量
 */
void Arena::HandleKillPlayer(Player* player, Player* killer)
{
    // 如果比赛未在进行中，不处理
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    // 调用基类处理击杀逻辑（释放灵魂等）
    Battleground::HandleKillPlayer(player, killer);

    // 更新存活玩家数量的世界状态
    UpdateArenaWorldState();
    // 检查胜利条件
    CheckWinConditions();
}

/**
 * @brief 玩家离开时移除玩家
 *
 * 处理玩家离开竞技场的完整流程:
 * 1. 如果是积分赛且比赛正在进行，计算积分损失
 * 2. 标记离线玩家为失败者，扣除积分
 * 3. 调用基类的移除玩家方法完成实际移除
 *
 * 积分处理逻辑:
 * - 玩家在比赛进行中离开视为失败
 * - 根据对手的匹配评分计算积分损失
 * - 在线玩家和离线玩家使用不同的积分更新方法
 *
 * @param guid 玩家的GUID
 * @param transport 是否传送（未使用）
 * @param sendPacket 是否发送数据包通知
 *
 * @note 调用时机: 玩家主动离开、断线或被踢出竞技场时
 * @note 性能说明: 时间复杂度 O(log n)，涉及积分计算
 * @see Battleground::RemovePlayerAtLeave
 */
void Arena::RemovePlayerAtLeave(ObjectGuid guid, bool transport, bool sendPacket)
{
    // 如果是积分赛且比赛正在进行，需要计算积分损失
    if (isRated() && GetStatus() == STATUS_IN_PROGRESS)
    {
        // 查找玩家数据
        BattlegroundPlayerMap::const_iterator itr = m_Players.find(guid);
        if (itr != m_Players.end()) // 检查玩家是否为比赛参与者，排除通过GM命令进入的玩家
        {
            // 获取玩家所属队伍
            uint32 team = itr->second.Team;

            // 获取胜负双方队伍信息
            ArenaTeam* winnerArenaTeam = sArenaTeamMgr->GetArenaTeamById(GetArenaTeamIdForTeam(GetOtherTeam(team)));
            ArenaTeam* loserArenaTeam = sArenaTeamMgr->GetArenaTeamById(GetArenaTeamIdForTeam(team));

            // 在积分赛进行中离开视为失败者，计算积分损失
            if (winnerArenaTeam && loserArenaTeam && winnerArenaTeam != loserArenaTeam)
            {
                // 根据玩家是否在线选择不同的积分更新方法
                if (Player* player = _GetPlayer(itr->first, itr->second.OfflineRemoveTime != 0, "Arena::RemovePlayerAtLeave"))
                    loserArenaTeam->MemberLost(player, GetArenaMatchmakerRating(GetOtherTeam(team)));
                else
                    loserArenaTeam->OfflineMemberLost(guid, GetArenaMatchmakerRating(GetOtherTeam(team)));
            }
        }
    }

    // 调用基类方法移除玩家
    Battleground::RemovePlayerAtLeave(guid, transport, sendPacket);
}

/**
 * @brief 检查胜利条件
 *
 * 检查竞技场是否满足胜利条件:
 * - 如果联盟方全灭且部落方仍有玩家，部落获胜
 * - 如果部落方全灭且联盟方仍有玩家，联盟获胜
 *
 * 平局情况在 EndBattleground 方法中处理（比赛超时）。
 *
 * @note 调用时机: 玩家死亡或离开竞技场时
 * @note 性能说明: 时间复杂度 O(n)，需要统计双方存活玩家数量
 */
void Arena::CheckWinConditions()
{
    // 如果联盟方全灭且部落方仍有玩家，部落获胜
    if (!GetAlivePlayersCountByTeam(ALLIANCE) && GetPlayersCountByTeam(HORDE))
        EndBattleground(HORDE);
    // 如果部落方全灭且联盟方仍有玩家，联盟获胜
    else if (GetPlayersCountByTeam(ALLIANCE) && !GetAlivePlayersCountByTeam(HORDE))
        EndBattleground(ALLIANCE);
}

/**
 * @brief 结束竞技场比赛
 *
 * 结束竞技场比赛并进行积分结算:
 * 1. 如果是积分赛，计算双方队伍的积分变化
 * 2. 更新个人评分和匹配评分
 * 3. 处理平局情况（比赛超时，双方各扣16分）
 * 4. 更新成就进度（获胜玩家、最后幸存者等）
 * 5. 保存队伍数据到数据库
 * 6. 发送统计信息给所有队员
 * 7. 调用基类结束比赛
 *
 * 积分计算使用ELO评分系统:
 * - 获胜队伍积分增加，失败队伍积分减少
 * - 积分变化量取决于双方评分差距
 * - 匹配评分（MMR）用于匹配，与队伍评分分开计算
 *
 * 平局处理:
 * - 比赛时间达到45分钟上限后视为平局
 * - 双方队伍各扣除16分积分
 *
 * @param winner 获胜队伍ID（ALLIANCE/HORDE，0表示平局）
 *
 * @note 调用时机: 一方全灭或比赛时间达到上限时
 * @note 性能说明: 时间复杂度 O(n*m)，n为玩家数量，m为队伍成员数量
 * @note 积分计算使用ELO评分系统
 */
void Arena::EndBattleground(uint32 winner)
{
    // 竞技场积分计算
    if (isRated())
    {
        // 初始化积分和评分变量
        uint32 loserTeamRating        = 0;   // 失败方队伍评分
        uint32 loserMatchmakerRating  = 0;   // 失败方匹配评分
        int32  loserChange            = 0;   // 失败方积分变化量
        int32  loserMatchmakerChange  = 0;   // 失败方匹配评分变化量
        uint32 winnerTeamRating       = 0;   // 获胜方队伍评分
        uint32 winnerMatchmakerRating = 0;   // 获胜方匹配评分
        int32  winnerChange           = 0;   // 获胜方积分变化量
        int32  winnerMatchmakerChange = 0;   // 获胜方匹配评分变化量

        // 平局情况下，按以下逻辑处理:
        // winnerArenaTeam => 联盟方, loserArenaTeam => 部落方
        ArenaTeam* winnerArenaTeam = sArenaTeamMgr->GetArenaTeamById(GetArenaTeamIdForTeam(winner == 0 ? uint32(ALLIANCE) : winner));
        ArenaTeam* loserArenaTeam = sArenaTeamMgr->GetArenaTeamById(GetArenaTeamIdForTeam(winner == 0 ? uint32(HORDE) : GetOtherTeam(winner)));

        // 只有当两个队伍都存在且不是同一队伍时才进行积分计算
        if (winnerArenaTeam && loserArenaTeam && winnerArenaTeam != loserArenaTeam)
        {
            // 平局情况下，按以下逻辑处理:
            // winnerMatchmakerRating => 联盟方, loserMatchmakerRating => 部落方
            loserTeamRating = loserArenaTeam->GetRating();
            loserMatchmakerRating = GetArenaMatchmakerRating(winner == 0 ? uint32(HORDE) : GetOtherTeam(winner));
            winnerTeamRating = winnerArenaTeam->GetRating();
            winnerMatchmakerRating = GetArenaMatchmakerRating(winner == 0 ? uint32(ALLIANCE) : winner);

            // 有明确胜负的情况
            if (winner != 0)
            {
                // 计算双方积分变化
                winnerMatchmakerChange = winnerArenaTeam->WonAgainst(winnerMatchmakerRating, loserMatchmakerRating, winnerChange);
                loserMatchmakerChange = loserArenaTeam->LostAgainst(loserMatchmakerRating, winnerMatchmakerRating, loserChange);

                // 记录积分变化日志
                TC_LOG_DEBUG("bg.arena", "match Type: {} --- Winner: old rating: {}, rating gain: {}, old MMR: {}, MMR gain: {} --- Loser: old rating: {}, rating loss: {}, old MMR: {}, MMR loss: {} ---",
                    GetArenaType(), winnerTeamRating, winnerChange, winnerMatchmakerRating, winnerMatchmakerChange,
                    loserTeamRating, loserChange, loserMatchmakerRating, loserMatchmakerChange);

                // 更新双方的匹配评分
                SetArenaMatchmakerRating(winner, winnerMatchmakerRating + winnerMatchmakerChange);
                SetArenaMatchmakerRating(GetOtherTeam(winner), loserMatchmakerRating + loserMatchmakerChange);

                // 客户端期望的队伍ID与TeamId不同
                // alliance 1, horde 0
                uint8 winnerTeam = winner == ALLIANCE ? PVP_TEAM_ALLIANCE : PVP_TEAM_HORDE;
                uint8 loserTeam = winner == ALLIANCE ? PVP_TEAM_HORDE : PVP_TEAM_ALLIANCE;

                // 设置积分变化数据，用于发送给客户端
                _arenaTeamScores[winnerTeam].Assign(winnerChange, winnerMatchmakerRating, winnerArenaTeam->GetName());
                _arenaTeamScores[loserTeam].Assign(loserChange, loserMatchmakerRating, loserArenaTeam->GetName());

                TC_LOG_DEBUG("bg.arena", "Arena match Type: {} for Team1Id: {} - Team2Id: {} ended. WinnerTeamId: {}. Winner rating: +{}, Loser rating: {}",
                    GetArenaType(), GetArenaTeamIdByIndex(TEAM_ALLIANCE), GetArenaTeamIdByIndex(TEAM_HORDE), winnerArenaTeam->GetId(), winnerChange, loserChange);

                // 如果启用了扩展竞技场日志，记录每个玩家的详细信息
                if (sWorld->getBoolConfig(CONFIG_ARENA_LOG_EXTENDED_INFO))
                    for (auto const& score : PlayerScores)
                        if (Player* player = ObjectAccessor::FindConnectedPlayer(ObjectGuid(HighGuid::Player, score.first)))
                        {
                            TC_LOG_DEBUG("bg.arena", "Statistics match Type: {} for {} (GUID: {}, Team: {}, IP: {}): {}",
                                GetArenaType(), player->GetName(), score.first, player->GetArenaTeamId(GetArenaType() == 5 ? 2 : GetArenaType() == 3),
                                player->GetSession()->GetRemoteAddress(), score.second->ToString());
                        }
            }
            // 平局情况：比赛时间达到45+2分钟后，双方各扣16分
            else
            {
                // 设置平局的积分损失数据
                _arenaTeamScores[PVP_TEAM_ALLIANCE].Assign(ARENA_TIMELIMIT_POINTS_LOSS, winnerMatchmakerRating, winnerArenaTeam->GetName());
                _arenaTeamScores[PVP_TEAM_HORDE].Assign(ARENA_TIMELIMIT_POINTS_LOSS, loserMatchmakerRating, loserArenaTeam->GetName());

                // 双方各扣16分
                winnerArenaTeam->FinishGame(ARENA_TIMELIMIT_POINTS_LOSS);
                loserArenaTeam->FinishGame(ARENA_TIMELIMIT_POINTS_LOSS);
            }

            // 获取获胜方存活玩家数量（用于"最后幸存者"成就判断）
            uint8 aliveWinners = GetAlivePlayersCountByTeam(winner);

            // 遍历所有参赛玩家，更新个人评分和成就
            for (auto const& i : GetPlayers())
            {
                uint32 team = i.second.Team;

                // 处理离线玩家
                if (i.second.OfflineRemoveTime)
                {
                    // 如果是积分赛，标记离线玩家为失败
                    if (team == winner)
                        winnerArenaTeam->OfflineMemberLost(i.first, loserMatchmakerRating, winnerMatchmakerChange);
                    else
                    {
                        // 平局时，双方离线玩家都扣分
                        if (winner == 0)
                            winnerArenaTeam->OfflineMemberLost(i.first, loserMatchmakerRating, winnerMatchmakerChange);

                        loserArenaTeam->OfflineMemberLost(i.first, winnerMatchmakerRating, loserMatchmakerChange);
                    }
                    continue;
                }

                // 获取在线玩家
                Player* player = _GetPlayer(i.first, i.second.OfflineRemoveTime != 0, "Arena::EndBattleground");
                if (!player)
                    continue;

                // 针对每个玩家的计算
                if (team == winner)
                {
                    // 获胜方玩家处理
                    // 在更新个人评分前先更新成就
                    uint32 rating = player->GetArenaPersonalRating(winnerArenaTeam->GetSlot());
                    player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WIN_RATED_ARENA, rating ? rating : 1);
                    player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_WIN_ARENA, GetMapId());

                    // 最后幸存者成就：评级5v5竞技场且是唯一的存活玩家
                    if (GetArenaType() == ARENA_TYPE_5v5 && aliveWinners == 1 && player->IsAlive())
                        player->CastSpell(player, SPELL_LAST_MAN_STANDING, true);

                    // 更新获胜玩家的个人评分和匹配评分
                    winnerArenaTeam->MemberWon(player, loserMatchmakerRating, winnerMatchmakerChange);
                }
                else
                {
                    // 失败方玩家处理
                    // 平局时，双方玩家都按失败处理
                    if (winner == 0)
                        winnerArenaTeam->MemberLost(player, loserMatchmakerRating, winnerMatchmakerChange);

                    loserArenaTeam->MemberLost(player, winnerMatchmakerRating, loserMatchmakerChange);

                    // 竞技场失败 => 重置具有"未失败"条件的获胜成就
                    player->ResetAchievementCriteria(ACHIEVEMENT_CRITERIA_CONDITION_NO_LOSE, 0);
                }
            }

            // 更新双方队伍的上一次对手ID（用于匹配系统）
            winnerArenaTeam->SetPreviousOpponents(loserArenaTeam->GetId());
            loserArenaTeam->SetPreviousOpponents(winnerArenaTeam->GetId());

            // 保存积分变化到数据库
            winnerArenaTeam->SaveToDB();
            loserArenaTeam->SaveToDB();

            // 发送更新的竞技场队伍统计信息给所有队员
            // 这样所有队员都会收到通知，而不仅仅是参加比赛的队员
            winnerArenaTeam->NotifyStatsChanged();
            loserArenaTeam->NotifyStatsChanged();
        }
    }

    // 结束战场
    Battleground::EndBattleground(winner);
}
