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
 * @file OutdoorPvPHP.cpp
 * @brief 地狱火半岛户外PvP系统实现
 *
 * 本模块实现了地狱火半岛的三座瞭望塔争夺战：
 * - 破碎丘陵瞭望塔 (Broken Hill)
 * - 瞭望塔 (Overlook)
 * - 竞技场瞭望塔 (Stadium)
 *
 * 主要功能：
 * - 三座塔的争夺和占领机制
 * - 占领全部三座塔后获得阵营增益效果
 * - 击杀敌方玩家获得奖励
 * - 世界状态同步和地图标记更新
 */

#include "OutdoorPvPHP.h"
#include "GameObject.h"
#include "Map.h"
#include "OutdoorPvPMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldStatePackets.h"

/** 受PvP增益效果影响的区域数量 */
uint32 const OutdoorPvPHPBuffZonesNum = 6;

/** 受PvP增益效果影响的区域ID数组 (地狱火半岛及相关副本) */
uint32 const OutdoorPvPHPBuffZones[OutdoorPvPHPBuffZonesNum] = { 3483, 3563, 3562, 3713, 3714, 3836 }; //  HP, citadel, ramparts, blood furnace, shattered halls, mag's lair

/** 击杀积分标记ID，用于任务完成判定 */
uint32 const HP_CREDITMARKER[HP_TOWER_NUM] = { 19032, 19028, 19029 };

//uint32 const HP_CapturePointEvent_Enter[HP_TOWER_NUM] = { 11404, 11396, 11388 };
//uint32 const HP_CapturePointEvent_Leave[HP_TOWER_NUM] = { 11403, 11395, 11387 };

/** 地图显示 - 中立状态世界状态ID */
uint32 const HP_MAP_N[HP_TOWER_NUM] = { 0x9b5, 0x9b2, 0x9a8 };

/** 地图显示 - 联盟占领状态世界状态ID */
uint32 const HP_MAP_A[HP_TOWER_NUM] = { 0x9b3, 0x9b0, 0x9a7 };

/** 地图显示 - 部落占领状态世界状态ID */
uint32 const HP_MAP_H[HP_TOWER_NUM] = { 0x9b4, 0x9b1, 0x9a6 };

/** 联盟占领后的旗帜外观包ID */
uint32 const HP_TowerArtKit_A[HP_TOWER_NUM] = { 65, 62, 67 };

/** 部落占领后的旗帜外观包ID */
uint32 const HP_TowerArtKit_H[HP_TOWER_NUM] = { 64, 61, 68 };

/** 中立状态下的旗帜外观包ID */
uint32 const HP_TowerArtKit_N[HP_TOWER_NUM] = { 66, 63, 69 };

/**
 * @brief 争夺点游戏对象配置数据
 *
 * 包含三座瞭望塔的GameObject模板ID、地图ID、位置坐标和旋转角度
 */
go_type const HPCapturePoints[HP_TOWER_NUM] =
{
    { 182175, 530, { -471.462f, 3451.09f, 34.6432f,  0.174533f }, { 0.0f, 0.0f, 0.087156f,  0.996195f } },  // 0 - Broken Hill (破碎丘陵)
    { 182174, 530, { -184.889f, 3476.93f, 38.2050f, -0.017453f }, { 0.0f, 0.0f, 0.008727f, -0.999962f } },  // 1 - Overlook (瞭望塔)
    { 182173, 530, { -290.016f, 3702.42f, 56.6729f,  0.034907f }, { 0.0f, 0.0f, 0.017452f,  0.999848f } }   // 2 - Stadium (竞技场)
};

/**
 * @brief 瞭望塔旗帜游戏对象配置数据
 *
 * 用于在塔顶显示阵营旗帜
 */
go_type const HPTowerFlags[HP_TOWER_NUM] =
{
    { 183514, 530, { -467.078f, 3528.17f, 64.7121f,  3.14159f }, { 0.0f, 0.0f, 1.000000f,  0.000000f } },   // 0 broken hill (破碎丘陵旗帜)
    { 182525, 530, { -187.887f, 3459.38f, 60.0403f, -3.12414f }, { 0.0f, 0.0f, 0.999962f, -0.008727f } },   // 1 overlook (瞭望塔旗帜)
    { 183515, 530, { -289.610f, 3696.83f, 75.9447f,  3.12414f }, { 0.0f, 0.0f, 0.999962f,  0.008727f } }    // 2 stadium (竞技场旗帜)
};

/** 联盟占领塔时的区域广播文本ID */
uint32 const HP_LANG_CAPTURE_A[HP_TOWER_NUM] = { TEXT_BROKEN_HILL_TAKEN_ALLIANCE, TEXT_OVERLOOK_TAKEN_ALLIANCE, TEXT_STADIUM_TAKEN_ALLIANCE };

/** 部落占领塔时的区域广播文本ID */
uint32 const HP_LANG_CAPTURE_H[HP_TOWER_NUM] = { TEXT_BROKEN_HILL_TAKEN_HORDE, TEXT_OVERLOOK_TAKEN_HORDE, TEXT_STADIUM_TAKEN_HORDE };

/**
 * @brief 构造函数 - 初始化瞭望塔争夺点
 *
 * @param pvp 所属的户外PvP实例
 * @param type 瞭望塔类型 (HP_TOWER_BROKEN_HILL, HP_TOWER_OVERLOOK, HP_TOWER_STADIUM)
 */
OPvPCapturePointHP::OPvPCapturePointHP(OutdoorPvP* pvp, OutdoorPvPHPTowerType type) : OPvPCapturePoint(pvp), m_TowerType(type)
{
    // 设置争夺点游戏对象数据
    SetCapturePointData(HPCapturePoints[type].entry, HPCapturePoints[type].map, HPCapturePoints[type].pos, HPCapturePoints[type].rot);
    // 添加塔顶旗帜对象
    AddObject(type, HPTowerFlags[type].entry, HPTowerFlags[type].map, HPTowerFlags[type].pos, HPTowerFlags[type].rot);
}

/**
 * @brief 构造函数 - 初始化地狱火半岛户外PvP实例
 */
OutdoorPvPHP::OutdoorPvPHP()
{
    m_TypeId = OUTDOOR_PVP_HP;
    m_AllianceTowersControlled = 0;
    m_HordeTowersControlled = 0;
    SetMapFromZone(OutdoorPvPHPBuffZones[0]);
}

/**
 * @brief 设置户外PvP环境
 *
 * 初始化三座瞭望塔的争夺点，并注册受PvP增益影响的区域
 *
 * @return 总是返回 true
 */
bool OutdoorPvPHP::SetupOutdoorPvP()
{
    m_AllianceTowersControlled = 0;
    m_HordeTowersControlled = 0;
    // 注册受PvP增益效果影响的区域
    for (uint32 i = 0; i < OutdoorPvPHPBuffZonesNum; ++i)
        RegisterZone(OutdoorPvPHPBuffZones[i]);

    // 创建三座瞭望塔的争夺点
    AddCapturePoint(new OPvPCapturePointHP(this, HP_TOWER_BROKEN_HILL));

    AddCapturePoint(new OPvPCapturePointHP(this, HP_TOWER_OVERLOOK));

    AddCapturePoint(new OPvPCapturePointHP(this, HP_TOWER_STADIUM));

    return true;
}

/**
 * @brief 处理玩家进入区域事件
 *
 * 当玩家进入地狱火半岛区域时，如果所属阵营占领了全部三座塔，
 * 则给玩家施加对应的阵营增益效果
 *
 * @param player 进入区域的玩家
 * @param zone 区域ID
 */
void OutdoorPvPHP::HandlePlayerEnterZone(Player* player, uint32 zone)
{
    // 给联盟玩家施加增益效果（如果联盟占领了全部3座塔）
    if (player->GetTeam() == ALLIANCE)
    {
        if (m_AllianceTowersControlled >=3)
            player->CastSpell(player, AllianceBuff, true);
    }
    else // 给部落玩家施加增益效果（如果部落占领了全部3座塔）
    {
        if (m_HordeTowersControlled >=3)
            player->CastSpell(player, HordeBuff, true);
    }
    OutdoorPvP::HandlePlayerEnterZone(player, zone);
}

/**
 * @brief 处理玩家离开区域事件
 *
 * 当玩家离开地狱火半岛区域时，移除阵营增益效果
 *
 * @param player 离开区域的玩家
 * @param zone 区域ID
 */
void OutdoorPvPHP::HandlePlayerLeaveZone(Player* player, uint32 zone)
{
    // 移除阵营增益效果
    if (player->GetTeam() == ALLIANCE)
    {
        player->RemoveAurasDueToSpell(AllianceBuff);
    }
    else
    {
        player->RemoveAurasDueToSpell(HordeBuff);
    }
    OutdoorPvP::HandlePlayerLeaveZone(player, zone);
}

/**
 * @brief 更新户外PvP状态
 *
 * 定期更新所有争夺点的状态，当阵营控制塔数量发生变化时，
 * 更新增益效果和世界状态UI
 *
 * @param diff 距离上次更新的时间差（毫秒）
 * @return 如果状态发生变化返回 true，否则返回 false
 */
bool OutdoorPvPHP::Update(uint32 diff)
{
    bool changed = OutdoorPvP::Update(diff);
    if (changed)
    {
        // 联盟占领全部三座塔
        if (m_AllianceTowersControlled == 3)
            TeamApplyBuff(TEAM_ALLIANCE, AllianceBuff, HordeBuff);
        // 部落占领全部三座塔
        else if (m_HordeTowersControlled == 3)
            TeamApplyBuff(TEAM_HORDE, HordeBuff, AllianceBuff);
        // 没有阵营占领全部塔
        else
        {
            TeamCastSpell(TEAM_ALLIANCE, -AllianceBuff);
            TeamCastSpell(TEAM_HORDE, -HordeBuff);
        }
        // 更新UI显示的塔数量
        SendUpdateWorldState(HP_UI_TOWER_COUNT_A, m_AllianceTowersControlled);
        SendUpdateWorldState(HP_UI_TOWER_COUNT_H, m_HordeTowersControlled);
    }
    return changed;
}

/**
 * @brief 移除玩家的世界状态显示
 *
 * 当玩家离开地狱火半岛时，清除所有相关的世界状态UI
 *
 * @param player 目标玩家
 */
void OutdoorPvPHP::SendRemoveWorldStates(Player* player)
{
    player->SendUpdateWorldState(HP_UI_TOWER_DISPLAY_A, 0);
    player->SendUpdateWorldState(HP_UI_TOWER_DISPLAY_H, 0);
    player->SendUpdateWorldState(HP_UI_TOWER_COUNT_H, 0);
    player->SendUpdateWorldState(HP_UI_TOWER_COUNT_A, 0);

    // 清除三座塔的地图显示状态
    for (int i = 0; i < HP_TOWER_NUM; ++i)
    {
        player->SendUpdateWorldState(HP_MAP_N[i], 0);
        player->SendUpdateWorldState(HP_MAP_A[i], 0);
        player->SendUpdateWorldState(HP_MAP_H[i], 0);
    }
}

/**
 * @brief 填充初始世界状态数据
 *
 * 当玩家进入地狱火半岛时，发送当前PvP状态的世界状态数据
 *
 * @param packet 世界状态数据包
 */
void OutdoorPvPHP::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(HP_UI_TOWER_DISPLAY_A, 1);
    packet.Worldstates.emplace_back(HP_UI_TOWER_DISPLAY_H, 1);
    packet.Worldstates.emplace_back(HP_UI_TOWER_COUNT_A, m_AllianceTowersControlled);
    packet.Worldstates.emplace_back(HP_UI_TOWER_COUNT_H, m_HordeTowersControlled);

    // 填充每座塔的状态
    for (OPvPCapturePointMap::iterator itr = m_capturePoints.begin(); itr != m_capturePoints.end(); ++itr)
        itr->second->FillInitialWorldStates(packet);
}

/**
 * @brief 处理瞭望塔状态变化
 *
 * 当瞭望塔的占领状态发生变化时调用，更新：
 * - 阵营控制塔的数量统计
 * - 游戏对象外观（旗帜颜色）
 * - 世界状态UI显示
 * - 区域广播消息
 * - 任务完成判定
 */
void OPvPCapturePointHP::ChangeState()
{
    uint32 field = 0;
    // 首先处理旧状态，清除旧状态的世界状态标记并更新阵营控制塔数量
    switch (m_OldState)
    {
    case OBJECTIVESTATE_NEUTRAL:
        field = HP_MAP_N[m_TowerType];
        break;
    case OBJECTIVESTATE_ALLIANCE:
        field = HP_MAP_A[m_TowerType];
        if (uint32 alliance_towers = ((OutdoorPvPHP*)m_PvP)->GetAllianceTowersControlled())
            ((OutdoorPvPHP*)m_PvP)->SetAllianceTowersControlled(--alliance_towers);
        break;
    case OBJECTIVESTATE_HORDE:
        field = HP_MAP_H[m_TowerType];
        if (uint32 horde_towers = ((OutdoorPvPHP*)m_PvP)->GetHordeTowersControlled())
            ((OutdoorPvPHP*)m_PvP)->SetHordeTowersControlled(--horde_towers);
        break;
    case OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE:
        field = HP_MAP_N[m_TowerType];
        break;
    case OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE:
        field = HP_MAP_N[m_TowerType];
        break;
    case OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE:
        field = HP_MAP_A[m_TowerType];
        break;
    case OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE:
        field = HP_MAP_H[m_TowerType];
        break;
    }

    // 清除旧状态的世界状态标记
    if (field)
    {
        m_PvP->SendUpdateWorldState(field, 0);
        field = 0;
    }

    // 设置新状态的外观和世界状态
    uint32 artkit = 21;
    uint32 artkit2 = HP_TowerArtKit_N[m_TowerType];
    switch (m_State)
    {
    case OBJECTIVESTATE_NEUTRAL:
        field = HP_MAP_N[m_TowerType];
        break;
    case OBJECTIVESTATE_ALLIANCE:
    {
        field = HP_MAP_A[m_TowerType];
        artkit = 2;  // 联盟外观包
        artkit2 = HP_TowerArtKit_A[m_TowerType];
        // 增加联盟控制的塔数量
        uint32 alliance_towers = ((OutdoorPvPHP*)m_PvP)->GetAllianceTowersControlled();
        if (alliance_towers < 3)
            ((OutdoorPvPHP*)m_PvP)->SetAllianceTowersControlled(++alliance_towers);
        // 发送区域广播消息
        m_PvP->SendDefenseMessage(OutdoorPvPHPBuffZones[0], HP_LANG_CAPTURE_A[m_TowerType]);
        break;
    }
    case OBJECTIVESTATE_HORDE:
    {
        field = HP_MAP_H[m_TowerType];
        artkit = 1;  // 部落外观包
        artkit2 = HP_TowerArtKit_H[m_TowerType];
        // 增加部落控制的塔数量
        uint32 horde_towers = ((OutdoorPvPHP*)m_PvP)->GetHordeTowersControlled();
        if (horde_towers < 3)
            ((OutdoorPvPHP*)m_PvP)->SetHordeTowersControlled(++horde_towers);
        // 发送区域广播消息
        m_PvP->SendDefenseMessage(OutdoorPvPHPBuffZones[0], HP_LANG_CAPTURE_H[m_TowerType]);
        break;
    }
    case OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE:
        field = HP_MAP_N[m_TowerType];
        break;
    case OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE:
        field = HP_MAP_N[m_TowerType];
        break;
    case OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE:
        field = HP_MAP_A[m_TowerType];
        artkit = 2;
        artkit2 = HP_TowerArtKit_A[m_TowerType];
        break;
    case OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE:
        field = HP_MAP_H[m_TowerType];
        artkit = 1;
        artkit2 = HP_TowerArtKit_H[m_TowerType];
        break;
    }

    // 更新争夺点游戏对象的外观（旗帜颜色）
    Map* map = m_PvP->GetMap();
    auto bounds = map->GetGameObjectBySpawnIdStore().equal_range(m_capturePointSpawnId);
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
        itr->second->SetGoArtKit(artkit);

    // 更新塔顶旗帜的外观
    bounds = map->GetGameObjectBySpawnIdStore().equal_range(m_Objects[m_TowerType]);
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
        itr->second->SetGoArtKit(artkit2);

    // 发送新状态的世界状态标记
    if (field)
        m_PvP->SendUpdateWorldState(field, 1);

    // 完成任务目标（占领塔的任务）
    if (m_State == OBJECTIVESTATE_ALLIANCE || m_State == OBJECTIVESTATE_HORDE)
        SendObjectiveComplete(HP_CREDITMARKER[m_TowerType], ObjectGuid::Empty);
}

/**
 * @brief 填充瞭望塔的初始世界状态数据
 *
 * 根据当前瞭望塔的占领状态，向客户端发送相应的世界状态信息，
 * 用于在地图上显示塔的控制状态
 *
 * @param packet 世界状态数据包
 */
void OPvPCapturePointHP::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    switch (m_State)
    {
        case OBJECTIVESTATE_ALLIANCE:
        case OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE:
            // 联盟控制状态
            packet.Worldstates.emplace_back(HP_MAP_N[m_TowerType], 0);
            packet.Worldstates.emplace_back(HP_MAP_A[m_TowerType], 1);
            packet.Worldstates.emplace_back(HP_MAP_H[m_TowerType], 0);
            break;
        case OBJECTIVESTATE_HORDE:
        case OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE:
            // 部落控制状态
            packet.Worldstates.emplace_back(HP_MAP_N[m_TowerType], 0);
            packet.Worldstates.emplace_back(HP_MAP_A[m_TowerType], 0);
            packet.Worldstates.emplace_back(HP_MAP_H[m_TowerType], 1);
            break;
        case OBJECTIVESTATE_NEUTRAL:
        case OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE:
        case OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE:
        default:
            // 中立状态
            packet.Worldstates.emplace_back(HP_MAP_N[m_TowerType], 1);
            packet.Worldstates.emplace_back(HP_MAP_A[m_TowerType], 0);
            packet.Worldstates.emplace_back(HP_MAP_H[m_TowerType], 0);
            break;
    }
}

/**
 * @brief 处理玩家击杀事件
 *
 * 当玩家在地狱火半岛击杀敌方玩家时，给予相应的奖励法术
 *
 * @param player 击杀者
 * @param killed 被击杀单位
 */
void OutdoorPvPHP::HandleKillImpl(Player* player, Unit* killed)
{
    // 只处理玩家击杀玩家的情况
    if (killed->GetTypeId() != TYPEID_PLAYER)
        return;

    // 根据击杀者阵营给予奖励
    if (player->GetTeam() == ALLIANCE && killed->ToPlayer()->GetTeam() != ALLIANCE)
        player->CastSpell(player, AlliancePlayerKillReward, true);
    else if (player->GetTeam() == HORDE && killed->ToPlayer()->GetTeam() != HORDE)
        player->CastSpell(player, HordePlayerKillReward, true);
}

/**
 * @brief 获取联盟控制的塔数量
 *
 * @return 联盟控制的瞭望塔数量
 */
uint32 OutdoorPvPHP::GetAllianceTowersControlled() const
{
    return m_AllianceTowersControlled;
}

/**
 * @brief 设置联盟控制的塔数量
 *
 * @param count 新的控制数量
 */
void OutdoorPvPHP::SetAllianceTowersControlled(uint32 count)
{
    m_AllianceTowersControlled = count;
}

/**
 * @brief 获取部落控制的塔数量
 *
 * @return 部落控制的瞭望塔数量
 */
uint32 OutdoorPvPHP::GetHordeTowersControlled() const
{
    return m_HordeTowersControlled;
}

/**
 * @brief 设置部落控制的塔数量
 *
 * @param count 新的控制数量
 */
void OutdoorPvPHP::SetHordeTowersControlled(uint32 count)
{
    m_HordeTowersControlled = count;
}

/**
 * @brief 地狱火半岛户外PvP脚本类
 *
 * 注册脚本系统，用于创建地狱火半岛PvP实例
 */
class OutdoorPvP_hellfire_peninsula : public OutdoorPvPScript
{
    public:
        OutdoorPvP_hellfire_peninsula() : OutdoorPvPScript("outdoorpvp_hp") { }

        /**
         * @brief 创建户外PvP实例
         *
         * @return 新创建的地狱火半岛PvP实例指针
         */
        OutdoorPvP* GetOutdoorPvP() const override
        {
            return new OutdoorPvPHP();
        }
};

/**
 * @brief 注册地狱火半岛户外PvP脚本
 *
 * 此函数由脚本加载器调用，注册脚本到系统
 */
void AddSC_outdoorpvp_hp()
{
    new OutdoorPvP_hellfire_peninsula();
}
