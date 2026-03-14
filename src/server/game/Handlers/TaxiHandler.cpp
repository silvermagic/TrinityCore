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
 * @file TaxiHandler.cpp
 * @brief 飞行坐骑（出租车）系统处理器模块
 *
 * @模块职责:
 *   处理游戏中飞行坐骑（出租车）系统的所有网络消息，包括：
 *   - 查询飞行点状态
 *   - 学习新飞行路线
 *   - 激活飞行路径
 *   - 处理飞行过程中的移动同步
 *
 * @主要功能:
 *   1. 飞行点管理：查询和管理玩家已知的飞行点
 *   2. 飞行路线学习：当玩家首次到达飞行点时自动学习
 *   3. 飞行激活：处理玩家选择飞行路线并起飞
 *   4. 飞行移动：处理飞行过程中的地图切换和终点到达
 *   5. 特殊飞行：处理特殊NPC（如死亡骑士的格里姆温）的飞行服务
 *
 * @性能注意事项:
 *   - 飞行点查询需要计算最近节点，涉及距离计算
 *   - 多地图飞行需要处理跨地图传送
 *   - 飞行状态需要同步给周围玩家
 *
 * @游戏机制:
 *   - 玩家首次与飞行管理员对话会学习该飞行点
 *   - 飞行需要支付金币费用
 *   - 某些特殊飞行点（如死亡骑士起始区域）允许飞行到未学习的点
 *   - 飞行过程中玩家无法控制移动
 */

#include "WorldSession.h"
#include "Common.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "FlightPathMovementGenerator.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"

/**
 * @brief 处理查询飞行点状态操作码
 *
 * @职责:
 *   处理玩家查询飞行点是否已知的网络请求。当玩家靠近飞行管理员时，
 *   客户端会发送此消息以获取该飞行点的状态（已知/未知）。
 *
 * @参数:
 *   recvData - 接收到的网络数据包，包含：
 *              - guid: 飞行管理员NPC的GUID
 *
 * @返回值: 无
 *
 * @调用时机:
 *   当玩家靠近飞行管理员NPC时，客户端自动发送CMSG_TAXINODE_STATUS_QUERY消息。
 *
 * @主要流程:
 *   1. 从数据包中读取飞行管理员的GUID
 *   2. 调用SendTaxiStatus发送飞行点状态给客户端
 */
void WorldSession::HandleTaxiNodeStatusQueryOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_TAXINODE_STATUS_QUERY");

    ObjectGuid guid;

    recvData >> guid;
    SendTaxiStatus(guid);
}

/**
 * @brief 发送飞行点状态给客户端
 *
 * @职责:
 *   向客户端发送指定飞行管理员的飞行点状态（已知/未知）。
 *   客户端根据此状态显示不同的图标（绿色=已知，灰色=未知）。
 *
 * @参数:
 *   guid - 飞行管理员NPC的GUID
 *
 * @返回值: 无
 *
 * @调用时机:
 *   - HandleTaxiNodeStatusQueryOpcode处理查询请求时调用
 *   - 学习新飞行点后更新状态时调用
 *
 * @主要流程:
 *   1. 获取飞行管理员NPC对象
 *   2. 验证NPC是否存在、可交互、是飞行管理员
 *   3. 查找最近的飞行点节点ID
 *   4. 检查玩家是否已知该飞行点
 *   5. 构建并发送状态数据包（0=未知，1=已知）
 *
 * @性能注意事项:
 *   - GetNearestTaxiNode需要遍历所有飞行点计算距离
 *   - 飞行点状态存储在玩家的taximask位图中，查询效率高
 */
void WorldSession::SendTaxiStatus(ObjectGuid guid)
{
    Player* const player = GetPlayer();
    // 获取飞行管理员NPC
    Creature* unit = ObjectAccessor::GetCreature(*player, guid);
    // 验证NPC存在、非敌对、有飞行管理员标志
    if (!unit || unit->IsHostileTo(player) || !unit->HasNpcFlag(UNIT_NPC_FLAG_FLIGHTMASTER))
    {
        TC_LOG_DEBUG("network", "WorldSession::SendTaxiStatus - {} not found or you can't interact with him.", guid.ToString());
        return;
    }

    // 查找最近的飞行点节点
    uint32 nearest = sObjectMgr->GetNearestTaxiNode(unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), unit->GetMapId(), player->GetTeam());
    if (!nearest)
        return;

    // 构建状态数据包
    WorldPacket data(SMSG_TAXINODE_STATUS, 9);
    data << guid;
    // 发送飞行点状态：1=已知（绿色图标），0=未知（灰色图标）
    data << uint8(player->m_taxi.IsTaximaskNodeKnown(nearest) ? 1 : 0);
    SendPacket(&data);
}

/**
 * @brief 处理查询可用飞行点操作码
 *
 * @职责:
 *   处理玩家与飞行管理员对话时查询可用飞行路线的网络请求。
 *   如果是未知的飞行点，会自动学习；如果是已知飞行点，显示飞行菜单。
 *
 * @参数:
 *   recvData - 接收到的网络数据包，包含：
 *              - guid: 飞行管理员NPC的GUID
 *
 * @返回值: 无
 *
 * @调用时机:
 *   玩家右键点击飞行管理员NPC时，客户端发送CMSG_TAXIQUERYAVAILABLENODES消息。
 *
 * @主要流程:
 *   1. 从数据包中读取飞行管理员的GUID
 *   2. 验证NPC是否存在且可交互（防作弊检查）
 *   3. 移除玩家的假死状态
 *   4. 如果是未知飞行点，学习新飞行点并通知客户端
 *   5. 如果是已知飞行点，显示飞行菜单
 *
 * @性能注意事项:
 *   - GetNPCIfCanInteractWith包含完整的交互验证
 *   - 学习新飞行点会更新数据库
 */
void WorldSession::HandleTaxiQueryAvailableNodes(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_TAXIQUERYAVAILABLENODES");

    ObjectGuid guid;
    recvData >> guid;

    // 防作弊检查：验证NPC存在、可交互、有飞行管理员标志
    Creature* unit = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_FLIGHTMASTER);
    if (!unit)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleTaxiQueryAvailableNodes - {} not found or you can't interact with him.", guid.ToString());
        return;
    }

    // 移除假死状态（假死状态下不能交互）
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // 如果是未知飞行点，学习新飞行点并发送通知
    if (SendLearnNewTaxiNode(unit))
        return;

    // 如果是已知飞行点，显示飞行菜单
    SendTaxiMenu(unit);
}

/**
 * @brief 发送飞行菜单给客户端
 *
 * @职责:
 *   向客户端发送飞行路线选择界面，显示当前飞行点的所有可达目的地。
 *   客户端根据接收到的数据绘制飞行地图界面。
 *
 * @参数:
 *   unit - 飞行管理员NPC对象
 *
 * @返回值: 无
 *
 * @调用时机:
 *   HandleTaxiQueryAvailableNodes处理已知飞行点时调用。
 *
 * @主要流程:
 *   1. 查找当前飞行点节点ID
 *   2. 处理特殊NPC（如死亡骑士区域的格里姆温）
 *   3. 构建飞行菜单数据包，包含：
 *      - 当前节点ID
 *      - 玩家已知的所有飞行点掩码
 *   4. 发送数据包给客户端
 *   5. 恢复玩家的飞行作弊状态
 *
 * @性能注意事项:
 *   - 飞行点掩码是一个位图，大小约8字节（64个飞行点）
 *   - 特殊NPC处理允许飞行到未学习的点（如死亡骑士任务）
 *
 * @游戏机制:
 *   - 飞行作弊模式（GM模式）允许飞行到所有飞行点
 *   - 格里姆温（NPC ID 29480）是死亡骑士起始区域的特殊飞行管理员
 */
void WorldSession::SendTaxiMenu(Creature* unit)
{
    // 查找当前飞行点节点ID
    uint32 curloc = sObjectMgr->GetNearestTaxiNode(unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), unit->GetMapId(), GetPlayer()->GetTeam());

    if (curloc == 0)
        return;

    // 保存玩家当前的飞行作弊状态
    bool lastTaxiCheaterState = GetPlayer()->isTaxiCheater();
    // 特殊处理：格里姆温（死亡骑士区域的飞行管理员）
    // 允许飞行到未学习的飞行点（包括祖阿曼，虽然WoWHead说不应包含）
    if (unit->GetEntry() == 29480) GetPlayer()->SetTaxiCheater(true);

    TC_LOG_DEBUG("network", "WORLD: CMSG_TAXINODE_STATUS_QUERY {} ", curloc);

    // 构建飞行菜单数据包
    WorldPacket data(SMSG_SHOWTAXINODES, (4 + 8 + 4 + 8 * 4));
    data << uint32(1);                                    // 显示类型：1=显示飞行菜单
    data << uint64(unit->GetGUID());                      // 飞行管理员GUID
    data << uint32(curloc);                               // 当前飞行点ID
    // 追加玩家已知的飞行点掩码（飞行作弊模式下显示所有飞行点）
    GetPlayer()->m_taxi.AppendTaximaskTo(data, GetPlayer()->isTaxiCheater());
    SendPacket(&data);

    TC_LOG_DEBUG("network", "WORLD: Sent SMSG_SHOWTAXINODES");

    // 恢复玩家原来的飞行作弊状态
    GetPlayer()->SetTaxiCheater(lastTaxiCheaterState);
}

/**
 * @brief 开始执行飞行路径
 *
 * @职责:
 *   让玩家骑乘飞行坐骑并开始沿指定飞行路径移动。这是实际执行飞行的核心函数。
 *
 * @参数:
 *   mountDisplayId - 飞行坐骑的显示ID（模型ID），0表示不显示坐骑
 *   path - 飞行路径ID（在TaxiPath.dbc中定义）
 *   pathNode - 起始节点索引（默认为0，用于跨地图飞行时跳过已完成的节点）
 *
 * @返回值: 无
 *
 * @调用时机:
 *   - 玩家确认飞行路线后，通过ActivateTaxiPathTo调用
 *   - 跨地图飞行传送后，从新的起始节点继续飞行
 *
 * @主要流程:
 *   1. 移除玩家的假死状态
 *   2. 让玩家骑乘飞行坐骑（应用坐骑模型）
 *   3. 创建飞行移动生成器并开始移动
 *
 * @性能注意事项:
 *   - 飞行路径是预先定义的，不需要实时计算
 *   - 飞行过程中玩家无法控制移动，由服务器控制
 *   - 跨地图飞行会触发传送，需要加载新地图资源
 */
void WorldSession::SendDoFlight(uint32 mountDisplayId, uint32 path, uint32 pathNode)
{
    // 移除假死状态（假死状态下不能飞行）
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // 让玩家骑乘飞行坐骑（应用坐骑模型）
    if (mountDisplayId)
        GetPlayer()->Mount(mountDisplayId);

    // 创建飞行移动生成器并开始飞行
    // pathNode用于跨地图飞行时指定起始节点
    GetPlayer()->GetMotionMaster()->MoveTaxiFlight(path, pathNode);
}

/**
 * @brief 学习新飞行点并发送通知
 *
 * @职责:
 *   当玩家首次与飞行管理员对话时，学习该飞行点并向客户端发送学习成功通知。
 *   返回值指示是否成功学习（用于决定后续是否显示飞行菜单）。
 *
 * @参数:
 *   unit - 飞行管理员NPC对象
 *
 * @返回值:
 *   true - 成功学习了新飞行点或飞行点不存在
 *   false - 飞行点已知，需要显示飞行菜单
 *
 * @调用时机:
 *   HandleTaxiQueryAvailableNodes处理未知飞行点时调用。
 *
 * @主要流程:
 *   1. 查找当前飞行点节点ID
 *   2. 如果节点不存在，返回true避免重复搜索
 *   3. 尝试设置飞行点掩码（学习飞行点）
 *   4. 如果学习成功，发送两种通知：
 *      a. SMSG_NEW_TAXI_PATH：新飞行路线通知
 *      b. SMSG_TAXINODE_STATUS：更新飞行点状态为已知
 *
 * @性能注意事项:
 *   - SetTaximaskNode是位操作，效率很高
 *   - 学习飞行点会立即更新数据库
 *   - 返回true可以避免HandleTaxiQueryAvailableNodes中重复搜索节点
 */
bool WorldSession::SendLearnNewTaxiNode(Creature* unit)
{
    // 查找当前飞行点节点ID
    uint32 curloc = sObjectMgr->GetNearestTaxiNode(unit->GetPositionX(), unit->GetPositionY(), unit->GetPositionZ(), unit->GetMapId(), GetPlayer()->GetTeam());

    if (curloc == 0)
        return true;  // 返回true以避免HandleTaxiQueryAvailableNodes再次搜索并调用SendTaxiMenu

    // 尝试学习飞行点（如果已知则返回false）
    if (GetPlayer()->m_taxi.SetTaximaskNode(curloc))
    {
        // 发送新飞行路线通知（客户端会显示"发现新飞行路线"消息）
        WorldPacket msg(SMSG_NEW_TAXI_PATH, 0);
        SendPacket(&msg);

        // 发送更新后的飞行点状态（将图标从灰色变为绿色）
        WorldPacket update(SMSG_TAXINODE_STATUS, 9);
        update << uint64(unit->GetGUID());
        update << uint8(1);  // 1=已知
        SendPacket(&update);

        return true;
    }
    else
        return false;  // 飞行点已知，需要显示飞行菜单
}

/**
 * @brief 发现并学习新飞行点
 *
 * @职责:
 *   在探索过程中自动发现并学习飞行点。与SendLearnNewTaxiNode不同，
 *   此函数不发送飞行点状态更新，只发送发现通知。
 *
 * @参数:
 *   nodeid - 飞行点节点ID
 *
 * @返回值: 无
 *
 * @调用时机:
 *   当玩家探索到新区域并发现飞行点时调用（非交互式学习）。
 *
 * @主要流程:
 *   1. 尝试设置飞行点掩码
 *   2. 如果成功，发送新飞行路线通知
 *
 * @性能注意事项:
 *   - 不需要查找最近节点，直接使用提供的节点ID
 *   - 只在飞行点未知时才发送通知
 */
void WorldSession::SendDiscoverNewTaxiNode(uint32 nodeid)
{
    // 尝试学习飞行点
    if (GetPlayer()->m_taxi.SetTaximaskNode(nodeid))
    {
        // 发送新飞行路线通知
        WorldPacket msg(SMSG_NEW_TAXI_PATH, 0);
        SendPacket(&msg);
    }
}

/**
 * @brief 处理快速激活飞行路径操作码
 *
 * @职责:
 *   处理玩家选择多站飞行路线的网络请求。玩家可以在飞行地图上选择多个途经点，
 *   形成一条多站飞行路线。
 *
 * @参数:
 *   recvData - 接收到的网络数据包，包含：
 *              - guid: 飞行管理员NPC的GUID
 *              - node_count: 飞行节点数量
 *              - 多个飞行节点ID
 *
 * @返回值: 无
 *
 * @调用时机:
 *   玩家在飞行地图上选择多个目的地并确认飞行时，客户端发送CMSG_ACTIVATETAXIEXPRESS消息。
 *
 * @主要流程:
 *   1. 从数据包中读取飞行管理员GUID和节点数量
 *   2. 验证飞行管理员是否存在且可交互
 *   3. 循环读取每个飞行节点
 *   4. 验证每个节点是否已知（除非飞行作弊模式）
 *   5. 如果有未知节点，发送错误消息并返回
 *   6. 调用ActivateTaxiPathTo开始飞行
 *
 * @性能注意事项:
 *   - 每个节点都需要验证是否已知
 *   - 飞行作弊模式（GM模式）跳过节点验证
 *   - 节点数量受客户端限制，无需额外检查
 *
 * @错误处理:
 *   - ERR_TAXITOOFARAWAY: 飞行管理员太远或不可交互
 *   - ERR_TAXINOTVISITED: 飞行点未知
 */
void WorldSession::HandleActivateTaxiExpressOpcode (WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_ACTIVATETAXIEXPRESS");

    ObjectGuid guid;
    uint32 node_count;

    recvData >> guid >> node_count;

    // 验证飞行管理员是否存在且可交互
    Creature* npc = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_FLIGHTMASTER);
    if (!npc)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleActivateTaxiExpressOpcode - {} not found or you can't interact with it.", guid.ToString());
        SendActivateTaxiReply(ERR_TAXITOOFARAWAY);
        return;
    }
    std::vector<uint32> nodes;

    // 读取并验证每个飞行节点
    for (uint32 i = 0; i < node_count; ++i)
    {
        uint32 node;
        recvData >> node;

        // 验证飞行点是否已知（飞行作弊模式跳过验证）
        if (!GetPlayer()->m_taxi.IsTaximaskNodeKnown(node) && !GetPlayer()->isTaxiCheater())
        {
            SendActivateTaxiReply(ERR_TAXINOTVISITED);
            recvData.rfinish();
            return;
        }

        nodes.push_back(node);
    }

    if (nodes.empty())
        return;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_ACTIVATETAXIEXPRESS from {} to {}", nodes.front(), nodes.back());

    // 激活飞行路径
    GetPlayer()->ActivateTaxiPathTo(nodes, npc);
}

/**
 * @brief 处理移动样条完成操作码
 *
 * @职责:
 *   处理飞行路径中节点移动完成的通知。此消息在两种情况下发送：
 *   1. 多站飞行的中间节点完成（继续下一个节点）
 *   2. 跨地图飞行时到达地图边界（需要传送）
 *
 * @参数:
 *   recvData - 接收到的网络数据包，包含：
 *              - guid: 移动单位的GUID（打包格式）
 *              - movementInfo: 移动信息
 *              - spline id: 样条ID
 *
 * @返回值: 无
 *
 * @调用时机:
 *   当飞行单位完成一个飞行节点移动时，客户端发送CMSG_MOVE_SPLINE_DONE消息。
 *
 * @主要流程:
 *   1. 读取移动单位的GUID（打包格式）
 *   2. 验证是否是正确的移动单位
 *   3. 读取移动信息和样条ID
 *   4. 检查当前飞行目的地：
 *      a. 如果有目的地且地图不同：执行跨地图传送
 *      b. 如果没有目的地且只剩1个节点：结束飞行
 *
 * @性能注意事项:
 *   - 跨地图传送需要加载新地图，开销较大
 *   - 飞行结束后需要清理飞行状态并恢复玩家控制
 *   - 只有最终目的地才需要处理飞行结束逻辑
 *
 * @游戏机制:
 *   - 跨地图飞行（如从东部王国到卡利姆多）需要传送
 *   - 飞行结束后会自动下坐骑
 *   - 在PVP区域飞行结束后会施放PVP旗帜法术
 */
void WorldSession::HandleMoveSplineDoneOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_MOVE_SPLINE_DONE");

    ObjectGuid guid;
    recvData >> guid.ReadAsPacked();

    // 验证是否是正确的移动单位（防止作弊）
    if (!IsRightUnitBeingMoved(guid))
    {
        recvData.rfinish();  // 防止日志垃圾信息
        return;
    }

    MovementInfo movementInfo;  // 仅用于正确读取数据包
    movementInfo.guid = guid;
    ReadMovementInfo(recvData, &movementInfo);

    recvData.read_skip<uint32>();  // 跳过样条ID

    // 飞行过程中收到此数据包有两种情况：
    // 1) 多站飞行的中间节点完成（需要继续飞行）
    // 2) 跨地图飞行到达地图边界（需要传送）
    // 我们只需要处理情况2

    uint32 curDest = GetPlayer()->m_taxi.GetTaxiDestination();
    if (curDest)
    {
        TaxiNodesEntry const* curDestNode = sTaxiNodesStore.LookupEntry(curDest);

        // 跨地图传送的情况
        if (curDestNode && curDestNode->ContinentID != GetPlayer()->GetMapId() && GetPlayer()->GetMotionMaster()->GetCurrentMovementGeneratorType() == FLIGHT_MOTION_TYPE)
        {
            if (FlightPathMovementGenerator* flight = dynamic_cast<FlightPathMovementGenerator*>(GetPlayer()->GetMotionMaster()->GetCurrentMovementGenerator()))
            {
                // 准备继续飞行：设置传送后的起始节点
                flight->SetCurrentNodeAfterTeleport();
                TaxiPathNodeEntry const* node = flight->GetPath()[flight->GetCurrentNode()];
                flight->SkipCurrentNode();

                // 传送到新地图的飞行节点位置
                GetPlayer()->TeleportTo(curDestNode->ContinentID, node->Loc.X, node->Loc.Y, node->Loc.Z, GetPlayer()->GetOrientation());
            }
        }

        return;
    }

    // 此时应该只剩1个节点（最终目的地）
    if (GetPlayer()->m_taxi.GetPath().size() != 1)
        return;

    // 清理飞行状态：下坐骑、恢复控制等
    GetPlayer()->CleanupAfterTaxiFlight();
    // 设置下落信息（防止掉落伤害）
    GetPlayer()->SetFallInformation(0, GetPlayer()->GetPositionZ());
    // 如果在PVP区域，施放PVP旗帜法术（法术ID: 2479）
    if (GetPlayer()->pvpInfo.IsHostile)
        GetPlayer()->CastSpell(GetPlayer(), 2479, true);
}

/**
 * @brief 处理激活飞行路径操作码
 *
 * @职责:
 *   处理玩家选择起点和终点飞行路线的网络请求。与快速激活不同，
 *   此操作码只包含起点和终点两个节点，客户端会自动计算最优路径。
 *
 * @参数:
 *   recvData - 接收到的网络数据包，包含：
 *              - guid: 飞行管理员NPC的GUID
 *              - nodes[0]: 起点飞行节点ID
 *              - nodes[1]: 终点飞行节点ID
 *
 * @返回值: 无
 *
 * @调用时机:
 *   玩家在飞行地图上直接点击目的地飞行点时，客户端发送CMSG_ACTIVATETAXI消息。
 *
 * @主要流程:
 *   1. 从数据包中读取飞行管理员GUID和两个节点ID
 *   2. 验证飞行管理员是否存在且可交互
 *   3. 验证起点和终点是否已知（除非飞行作弊模式）
 *   4. 调用ActivateTaxiPathTo开始飞行
 *
 * @性能注意事项:
 *   - 只有两个节点，验证开销小
 *   - 服务端会根据起点和终点计算完整路径
 *
 * @错误处理:
 *   - ERR_TAXITOOFARAWAY: 飞行管理员太远或不可交互
 *   - ERR_TAXINOTVISITED: 起点或终点飞行点未知
 */
void WorldSession::HandleActivateTaxiOpcode(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_ACTIVATETAXI");

    ObjectGuid guid;
    std::vector<uint32> nodes;
    nodes.resize(2);

    // 读取飞行管理员GUID和起点终点节点ID
    recvData >> guid >> nodes[0] >> nodes[1];
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_ACTIVATETAXI from {} to {}", nodes[0], nodes[1]);

    // 验证飞行管理员是否存在且可交互
    Creature* npc = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_FLIGHTMASTER);
    if (!npc)
    {
        TC_LOG_DEBUG("network", "WORLD: HandleActivateTaxiOpcode - {} not found or you can't interact with it.", guid.ToString());
        SendActivateTaxiReply(ERR_TAXITOOFARAWAY);
        return;
    }

    // 验证起点和终点是否已知（飞行作弊模式跳过验证）
    if (!GetPlayer()->isTaxiCheater())
    {
        if (!GetPlayer()->m_taxi.IsTaximaskNodeKnown(nodes[0]) || !GetPlayer()->m_taxi.IsTaximaskNodeKnown(nodes[1]))
        {
            SendActivateTaxiReply(ERR_TAXINOTVISITED);
            return;
        }
    }

    // 激活飞行路径（内部会计算最优路径、扣费、开始飞行）
    GetPlayer()->ActivateTaxiPathTo(nodes, npc);
}

/**
 * @brief 发送激活飞行回复给客户端
 *
 * @职责:
 *   向客户端发送飞行激活的结果状态。客户端根据回复类型显示相应的错误消息或继续飞行流程。
 *
 * @参数:
 *   reply - 激活飞行回复类型，可能的值包括：
 *           - ERR_TAXI_OK: 成功，可以开始飞行
 *           - ERR_TAXIUNSPECIFIEDSERVERERROR: 服务器错误
 *           - ERR_TAXINOSUCHPATH: 没有此飞行路径
 *           - ERR_TAXINOTENOUGHMONEY: 金币不足
 *           - ERR_TAXITOOFARAWAY: 飞行管理员太远
 *           - ERR_TAXINOVENDORNEARBY: 附近没有飞行管理员
 *           - ERR_TAXINOTVISITED: 飞行点未知
 *           - ERR_TAXIPLAYERBUSY: 玩家忙碌
 *           - ERR_TAXIPLAYERALREADYMOUNTED: 玩家已骑乘
 *           - ERR_TAXIPLAYERSHAPESHIFTED: 玩家变形中
 *           - ERR_TAXIPLAYERMOVING: 玩家移动中
 *           - ERR_TAXISAMENODE: 起点和终点相同
 *           - ERR_TAXINOTSTANDING: 玩家未站立
 *
 * @返回值: 无
 *
 * @调用时机:
 *   当飞行激活失败时调用，发送错误消息给客户端。
 *   成功时通常不需要发送回复，直接开始飞行。
 *
 * @性能注意事项:
 *   - 数据包很小，只有4字节
 *   - 客户端会根据错误类型显示本地化的错误消息
 */
void WorldSession::SendActivateTaxiReply(ActivateTaxiReply reply)
{
    WorldPacket data(SMSG_ACTIVATETAXIREPLY, 4);
    data << uint32(reply);
    SendPacket(&data);
}
