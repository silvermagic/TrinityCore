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
 * @file BattlegroundIC.h
 * @brief 冬拥湖之岛（Isle of Conquest）战场模块头文件
 *
 * 本文件定义了冬拥湖之岛战场的具体实现，包括：
 * - 战场生物和游戏对象枚举
 * - 世界状态枚举
 * - 战场事件和音效枚举
 * - BattlegroundIC类定义
 *
 * 冬拥湖之岛是一个40v40的大型战场，位于诺森德大陆。
 * 战场目标是摧毁敌方将军，或通过占领据点和击杀敌人来消耗敌方增援点数。
 * 战场包含多个战略要点：码头、机库、车间、矿洞、堡垒等。
 */

#ifndef __BATTLEGROUNDIC_H
#define __BATTLEGROUNDIC_H

#include "Battleground.h"
#include "BattlegroundScore.h"
#include "Object.h"

/// 战场阵营ID
const uint32 BG_IC_Factions[2] =
{
    1732, ///< 联盟阵营ID
    1735  ///< 部落阵营ID
};

/**
 * @brief 生物NPC枚举
 *
 * 定义战场中各种NPC的ID，包括BOSS、守卫、攻城器械、飞艇炮手等
 */
enum creaturesIC
{
    NPC_HIGH_COMMANDER_HALFORD_WYRMBANE     = 34924,  ///< 联盟指挥官（联盟BOSS）
    NPC_OVERLORD_AGMAR                      = 34922,  ///< 阿格玛领主（部落BOSS）
    NPC_KOR_KRON_GUARD                      = 34918,  ///< 库卡隆守卫（部落守卫）
    NPC_SEVEN_TH_LEGION_INFANTRY            = 34919,  ///< 第七军团步兵（联盟守卫）
    NPC_KEEP_CANNON                         = 34944,  ///< 堡垒火炮
    NPC_DEMOLISHER                          = 34775,  ///< 碎石车
    NPC_SIEGE_ENGINE_H                      = 35069,  ///< 攻城机车（部落）
    NPC_SIEGE_ENGINE_A                      = 34776,  ///< 攻城机车（联盟）
    NPC_GLAIVE_THROWER_A                    = 34802,  ///< 投刃车（联盟）
    NPC_GLAIVE_THROWER_H                    = 35273,  ///< 投刃车（部落）
    NPC_CATAPULT                            = 34793,  ///< 投石车
    NPC_HORDE_GUNSHIP_CANNON                = 34935,  ///< 部落飞艇火炮
    NPC_ALLIANCE_GUNSHIP_CANNON             = 34929,  ///< 联盟飞艇火炮
    NPC_HORDE_GUNSHIP_CAPTAIN               = 35003,  ///< 部落飞艇船长
    NPC_ALLIANCE_GUNSHIP_CAPTAIN            = 34960,  ///< 联盟飞艇船长
    NPC_WORLD_TRIGGER_NOT_FLOATING          = 34984,  ///< 世界触发器（非浮空）
    NPC_WORLD_TRIGGER_ALLIANCE_FRIENDLY     = 20213,  ///< 世界触发器（联盟友方）
    NPC_WORLD_TRIGGER_HORDE_FRIENDLY        = 20212   ///< 世界触发器（部落友方）
};

/**
 * @brief 游戏对象枚举
 *
 * 定义战场中各种游戏对象的ID，包括旗帜、大门、传送门、装饰物等
 */
enum gameobjectsIC
{
    GO_ALLIANCE_BANNER                          = 195396,  ///< 联盟旗帜

    GO_ALLIANCE_GATE_1                          = 195699,  ///< 联盟大门1
    GO_ALLIANCE_GATE_2                          = 195698,  ///< 联盟大门2
    GO_ALLIANCE_GATE_3                          = 195700,  ///< 联盟大门3

    GO_ALLIANCE_GUNSHIP_PORTAL                  = 195320,  ///< 联盟飞艇传送门

    GO_ALLIANCE_GUNSHIP_PORTAL_EFFECTS          = 195705,  ///< 联盟飞艇传送门效果

    GO_BENCH_1                                  = 186896,  ///< 长凳1
    GO_BENCH_2                                  = 186922,  ///< 长凳2
    GO_BENCH_3                                  = 186899,  ///< 长凳3
    GO_BENCH_4                                  = 186904,  ///< 长凳4
    GO_BENCH_5                                  = 186897,  ///< 长凳5

    GO_BONFIRE_1                                = 195376,  ///< 篝火1
    GO_BONFIRE_2                                = 195208,  ///< 篝火2
    GO_BONFIRE_3                                = 195210,  ///< 篝火3
    GO_BONFIRE_4                                = 195207,  ///< 篝火4
    GO_BONFIRE_5                                = 195209,  ///< 篝火5
    GO_BONFIRE_6                                = 195377,  ///< 篝火6

    GO_DOCKS_BANNER                             = 195157,  ///< 码头旗帜

    GO_DOODAD_HU_PORTCULLIS01                   = 195436,  ///< 人类城门装饰

    GO_DOODAD_ND_HUMAN_GATE_CLOSEDFX_DOOR01     = 195703,  ///< 人类大门关闭效果

    GO_DOODAD_PORTCULLISACTIVE01                = 195451,  ///< 活动城门01

    GO_DOODAD_PORTCULLISACTIVE02                = 195452,  ///< 活动城门02

    GO_DOODAD_VR_PORTCULLIS01                   = 195437,  ///< 兽人城门装饰

    GO_CHAIR_1                                  = 195410,  ///< 椅子1
    GO_CHAIR_2                                  = 195414,  ///< 椅子2
    GO_CHAIR_3                                  = 160415,  ///< 椅子3
    GO_CHAIR_4                                  = 195418,  ///< 椅子4
    GO_CHAIR_5                                  = 195416,  ///< 椅子5
    GO_CHAIR_6                                  = 160410,  ///< 椅子6
    GO_CHAIR_7                                  = 160418,  ///< 椅子7
    GO_CHAIR_8                                  = 160416,  ///< 椅子8
    GO_CHAIR_9                                  = 160419,  ///< 椅子9

    GO_FLAGPOLE_1                               = 195131,  ///< 旗杆1
    GO_FLAGPOLE_2                               = 195439,  ///< 旗杆2

    GO_GUNSHIP_PORTAL_1                         = 195371,  ///< 飞艇传送门1
    GO_GUNSHIP_PORTAL_2                         = 196413,  ///< 飞艇传送门2

    GO_HANGAR_BANNER                            = 195158,  ///< 机库旗帜

    GO_HORDE_BANNER                             = 195393,  ///< 部落旗帜

    GO_HORDE_GATE_1                             = 195494,  ///< 部落大门1
    GO_HORDE_GATE_2                             = 195496,  ///< 部落大门2
    GO_HORDE_GATE_3                             = 195495,  ///< 部落大门3

    GO_HORDE_GUNSHIP_PORTAL                     = 195326,  ///< 部落飞艇传送门

    GO_HORDE_GUNSHIP_PORTAL_EFFECTS             = 195706,  ///< 部落飞艇传送门效果

    GO_HORDE_KEEP_PORTCULLIS                    = 195223,  ///< 部落堡垒城门

    GO_HUGE_SEAFORIUM_BOMB_A                    = 195332,  ///< 巨型海盐炸弹（联盟）
    GO_HUGE_SEAFORIUM_BOMB_H                    = 195333,  ///< 巨型海盐炸弹（部落）

    GO_QUARRY_BANNER                            = 195338,  ///< 采石场旗帜
    GO_REFRESHMENT_PORTAL                       = 186811,  ///< 食物传送门
    GO_SEAFORIUM_BOMBS                          = 195237,  ///< 海盐炸弹

    GO_STOVE_1                                  = 174863,  ///< 炉子1
    GO_STOVE_2                                  = 160411,  ///< 炉子2

    GO_TELEPORTER_1                             = 195314,  ///< 传送器1（部落-出口）
    GO_TELEPORTER_2                             = 195313,  ///< 传送器2（部落-入口）

    GO_TELEPORTER_3                             = 195315,  ///< 传送器3（联盟-出口）
    GO_TELEPORTER_4                             = 195316,  ///< 传送器4（联盟-入口）

    GO_TELEPORTER_EFFECTS_A                     = 195701,  ///< 传送器效果（联盟）
    GO_TELEPORTER_EFFECTS_H                     = 195702,  ///< 传送器效果（部落）

    GO_WORKSHOP_BANNER                          = 195133,  ///< 车间旗帜

    GO_BRAZIER_1                                = 195402,  ///< 火盆1
    GO_BRAZIER_2                                = 195403,  ///< 火盆2
    GO_BRAZIER_3                                = 195425,  ///< 火盆3
    GO_BRAZIER_4                                = 195424,  ///< 火盆4

    GO_REFINERY_BANNER                          = 195343,  ///< 炼油厂旗帜

    GO_DOODAD_ND_WINTERORC_WALL_GATEFX_DOOR01   = 195491,  ///< 兽人墙门效果

    // 码头旗帜（联盟/部落）
    GO_ALLIANCE_BANNER_DOCK                     = 195153,  ///< 码头联盟旗帜
    GO_ALLIANCE_BANNER_DOCK_CONT                = 195154,  ///< 码头联盟旗帜（争夺中）
    GO_HORDE_BANNER_DOCK                        = 195155,  ///< 码头部落旗帜
    GO_HORDE_BANNER_DOCK_CONT                   = 195156,  ///< 码头部落旗帜（争夺中）

    // 机库旗帜（部落/联盟）
    GO_HORDE_BANNER_HANGAR                      = 195130,  ///< 机库部落旗帜
    GO_HORDE_BANNER_HANGAR_CONT                 = 195145,  ///< 机库部落旗帜（争夺中）
    GO_ALLIANCE_BANNER_HANGAR                   = 195132,  ///< 机库联盟旗帜
    GO_ALLIANCE_BANNER_HANGAR_CONT              = 195144,  ///< 机库联盟旗帜（争夺中）

    // 采石场旗帜（联盟/部落）
    GO_ALLIANCE_BANNER_QUARRY                   = 195334,  ///< 采石场联盟旗帜
    GO_ALLIANCE_BANNER_QUARRY_CONT              = 195335,  ///< 采石场联盟旗帜（争夺中）
    GO_HORDE_BANNER_QUARRY                      = 195336,  ///< 采石场部落旗帜
    GO_HORDE_BANNER_QUARRY_CONT                 = 195337,  ///< 采石场部落旗帜（争夺中）

    GO_ALLIANCE_BANNER_REFINERY                 = 195339,
    GO_ALLIANCE_BANNER_REFINERY_CONT            = 195340,
    GO_HORDE_BANNER_REFINERY                    = 195341,
    GO_HORDE_BANNER_REFINERY_CONT               = 195342,

    GO_ALLIANCE_BANNER_WORKSHOP                 = 195149,
    GO_ALLIANCE_BANNER_WORKSHOP_CONT            = 195150,
    GO_HORDE_BANNER_WORKSHOP                    = 195151,
    GO_HORDE_BANNER_WORKSHOP_CONT               = 195152,

    GO_ALLIANCE_BANNER_GRAVEYARD_A              = 195396,
    GO_ALLIANCE_BANNER_GRAVEYARD_A_CONT         = 195397,
    GO_HORDE_BANNER_GRAVEYARD_A                 = 195398,
    GO_HORDE_BANNER_GRAVEYARD_A_CONT            = 195399,

    GO_ALLIANCE_BANNER_GRAVEYARD_H              = 195391,
    GO_ALLIANCE_BANNER_GRAVEYARD_H_CONT         = 195392,
    GO_HORDE_BANNER_GRAVEYARD_H                 = 195393,
    GO_HORDE_BANNER_GRAVEYARD_H_CONT            = 195394,

    GO_HORDE_GUNSHIP                            = 195276,
    GO_ALLIANCE_GUNSHIP                         = 195121
};

#define MAX_REINFORCEMENTS 300

enum Times
{
    WORKSHOP_UPDATE_TIME     = 180000, // 3 minutes
    DOCKS_UPDATE_TIME        = 180000, // not sure if it is 3 minutes
    IC_RESOURCE_TIME         = 45000, // not sure, need more research
    CLOSE_DOORS_TIME         = 20000,
    BANNER_STATE_CHANGE_TIME = 60000,
    TRANSPORT_PERIOD_TIME    = 120000
};

enum Actions
{
    ACTION_GUNSHIP_READY = 1
};

struct ICNpc
{
    uint32 type;
    uint32 entry;
    TeamId team;
    float x;
    float y;
    float z;
    float o;
};

enum BG_IC_GOs
{
    BG_IC_GO_ALLIANCE_BANNER = 0,

    BG_IC_GO_ALLIANCE_GATE_1,
    BG_IC_GO_ALLIANCE_GATE_2,
    BG_IC_GO_ALLIANCE_GATE_3,

    BG_IC_GO_BENCH_1,
    BG_IC_GO_BENCH_2,
    BG_IC_GO_BENCH_3,
    BG_IC_GO_BENCH_4,
    BG_IC_GO_BENCH_5,

    BG_IC_GO_BONFIRE_1,
    BG_IC_GO_BONFIRE_2,
    BG_IC_GO_BONFIRE_3,
    BG_IC_GO_BONFIRE_4,
    BG_IC_GO_BONFIRE_5,
    BG_IC_GO_BONFIRE_6,

    BG_IC_GO_BRAZIER_1,
    BG_IC_GO_BRAZIER_2,
    BG_IC_GO_BRAZIER_3,
    BG_IC_GO_BRAZIER_4,

    BG_IC_GO_CHAIR_1,
    BG_IC_GO_CHAIR_2,
    BG_IC_GO_CHAIR_3_1,
    BG_IC_GO_CHAIR_4,
    BG_IC_GO_CHAIR_5,
    BG_IC_GO_CHAIR_6_1,
    BG_IC_GO_CHAIR_7,
    BG_IC_GO_CHAIR_3_2,
    BG_IC_GO_CHAIR_6_2,
    BG_IC_GO_CHAIR_8_1,
    BG_IC_GO_CHAIR_8_2,
    BG_IC_GO_CHAIR_9,

    BG_IC_GO_DOCKS_BANNER,

    BG_IC_GO_DOODAD_HU_PORTCULLIS01_1,
    BG_IC_GO_DOODAD_HU_PORTCULLIS01_2,

    BG_IC_GO_DOODAD_ND_HUMAN_GATE_CLOSEDFX_DOOR01,
    BG_IC_GO_DOODAD_ND_HUMAN_GATE_CLOSEDFX_DOOR02,
    BG_IC_GO_DOODAD_ND_HUMAN_GATE_CLOSEDFX_DOOR03,

    BG_IC_GO_DOODAD_PORTCULLISACTIVE01,

    BG_IC_GO_DOODAD_PORTCULLISACTIVE02,

    BG_IC_GO_DOODAD_VR_PORTCULLIS01_1,
    BG_IC_GO_DOODAD_VR_PORTCULLIS01_2,

    BG_IC_GO_FLAGPOLE_1_1,
    BG_IC_GO_FLAGPOLE_2_1,
    BG_IC_GO_FLAGPOLE_2_2,
    BG_IC_GO_FLAGPOLE_1_2,
    BG_IC_GO_FLAGPOLE_1_3,
    BG_IC_GO_FLAGPOLE_1_4,
    BG_IC_GO_FLAGPOLE_1_5,
    BG_IC_GO_FLAGPOLE_1_6,

    BG_IC_GO_HANGAR_BANNER,

    BG_IC_GO_HORDE_BANNER,

    BG_IC_GO_HORDE_GATE_1,
    BG_IC_GO_HORDE_GATE_2,
    BG_IC_GO_HORDE_GATE_3,

    BG_IC_GO_HORDE_KEEP_PORTCULLIS,

    BG_IC_GO_QUARRY_BANNER,

    BG_IC_GO_STOVE_1_1,
    BG_IC_GO_STOVE_2_1,
    BG_IC_GO_STOVE_1_2,
    BG_IC_GO_STOVE_2_2,

    BG_IC_GO_WORKSHOP_BANNER,

    BG_IC_GO_REFINERY_BANNER,

    BG_IC_GO_HUGE_SEAFORIUM_BOMBS_A_1,
    BG_IC_GO_HUGE_SEAFORIUM_BOMBS_A_2,
    BG_IC_GO_HUGE_SEAFORIUM_BOMBS_A_3,
    BG_IC_GO_HUGE_SEAFORIUM_BOMBS_A_4,

    BG_IC_GO_HUGE_SEAFORIUM_BOMBS_H_1,
    BG_IC_GO_HUGE_SEAFORIUM_BOMBS_H_2,
    BG_IC_GO_HUGE_SEAFORIUM_BOMBS_H_3,
    BG_IC_GO_HUGE_SEAFORIUM_BOMBS_H_4,

    BG_IC_GO_DOODAD_ND_WINTERORC_WALL_GATEFX_DOOR01,
    BG_IC_GO_DOODAD_ND_WINTERORC_WALL_GATEFX_DOOR02,
    BG_IC_GO_DOODAD_ND_WINTERORC_WALL_GATEFX_DOOR03,

    BG_IC_GO_SEAFORIUM_BOMBS_1,
    BG_IC_GO_SEAFORIUM_BOMBS_2,

    BG_IC_GO_HANGAR_TELEPORTER_1,
    BG_IC_GO_HANGAR_TELEPORTER_2,
    BG_IC_GO_HANGAR_TELEPORTER_3,

    BG_IC_GO_HANGAR_TELEPORTER_EFFECT_1,
    BG_IC_GO_HANGAR_TELEPORTER_EFFECT_2,
    BG_IC_GO_HANGAR_TELEPORTER_EFFECT_3,

    BG_IC_GO_TELEPORTER_1_1,
    BG_IC_GO_TELEPORTER_1_2,
    BG_IC_GO_TELEPORTER_2_1,
    BG_IC_GO_TELEPORTER_3_1,
    BG_IC_GO_TELEPORTER_2_2,
    BG_IC_GO_TELEPORTER_4_1,
    BG_IC_GO_TELEPORTER_3_2,
    BG_IC_GO_TELEPORTER_3_3,
    BG_IC_GO_TELEPORTER_4_2,
    BG_IC_GO_TELEPORTER_4_3,
    BG_IC_GO_TELEPORTER_1_3,
    BG_IC_GO_TELEPORTER_2_3,

    BG_IC_GO_TELEPORTER_EFFECTS_A_1,
    BG_IC_GO_TELEPORTER_EFFECTS_A_2,
    BG_IC_GO_TELEPORTER_EFFECTS_A_3,
    BG_IC_GO_TELEPORTER_EFFECTS_A_4,
    BG_IC_GO_TELEPORTER_EFFECTS_A_5,
    BG_IC_GO_TELEPORTER_EFFECTS_A_6,

    BG_IC_GO_TELEPORTER_EFFECTS_H_1,
    BG_IC_GO_TELEPORTER_EFFECTS_H_2,
    BG_IC_GO_TELEPORTER_EFFECTS_H_3,
    BG_IC_GO_TELEPORTER_EFFECTS_H_4,
    BG_IC_GO_TELEPORTER_EFFECTS_H_5,
    BG_IC_GO_TELEPORTER_EFFECTS_H_6
};

enum BG_IC_NPCs
{
    BG_IC_NPC_OVERLORD_AGMAR = 0,
    BG_IC_NPC_HIGH_COMMANDER_HALFORD_WYRMBANE,
    BG_IC_NPC_KOR_KRON_GUARD_1,
    BG_IC_NPC_KOR_KRON_GUARD_2,
    BG_IC_NPC_KOR_KRON_GUARD_3,
    BG_IC_NPC_KOR_KRON_GUARD_4,
    BG_IC_NPC_SEVEN_TH_LEGION_INFANTRY_1,
    BG_IC_NPC_SEVEN_TH_LEGION_INFANTRY_2,
    BG_IC_NPC_SEVEN_TH_LEGION_INFANTRY_3,
    BG_IC_NPC_SEVEN_TH_LEGION_INFANTRY_4,
    BG_IC_NPC_KEEP_CANNON_1,
    BG_IC_NPC_KEEP_CANNON_2,
    BG_IC_NPC_KEEP_CANNON_3,
    BG_IC_NPC_KEEP_CANNON_4,
    BG_IC_NPC_KEEP_CANNON_5,
    BG_IC_NPC_KEEP_CANNON_6,
    BG_IC_NPC_KEEP_CANNON_7,
    BG_IC_NPC_KEEP_CANNON_8,
    BG_IC_NPC_KEEP_CANNON_9,
    BG_IC_NPC_KEEP_CANNON_10,
    BG_IC_NPC_KEEP_CANNON_11,
    BG_IC_NPC_KEEP_CANNON_12,
    BG_IC_NPC_KEEP_CANNON_13,
    BG_IC_NPC_KEEP_CANNON_14,
    BG_IC_NPC_KEEP_CANNON_15,
    BG_IC_NPC_KEEP_CANNON_16,
    BG_IC_NPC_KEEP_CANNON_17,
    BG_IC_NPC_KEEP_CANNON_18,
    BG_IC_NPC_KEEP_CANNON_19,
    BG_IC_NPC_KEEP_CANNON_20,
    BG_IC_NPC_KEEP_CANNON_21,
    BG_IC_NPC_KEEP_CANNON_22,
    BG_IC_NPC_KEEP_CANNON_23,
    BG_IC_NPC_KEEP_CANNON_24,

    BG_IC_NPC_SIEGE_ENGINE_A,
    BG_IC_NPC_SIEGE_ENGINE_H,

    BG_IC_NPC_DEMOLISHER_1_A,
    BG_IC_NPC_DEMOLISHER_2_A,
    BG_IC_NPC_DEMOLISHER_3_A,
    BG_IC_NPC_DEMOLISHER_4_A,

    BG_IC_NPC_DEMOLISHER_1_H,
    BG_IC_NPC_DEMOLISHER_2_H,
    BG_IC_NPC_DEMOLISHER_3_H,
    BG_IC_NPC_DEMOLISHER_4_H,

    BG_IC_NPC_GLAIVE_THROWER_1_A,
    BG_IC_NPC_GLAIVE_THROWER_2_A,
    BG_IC_NPC_GLAIVE_THROWER_1_H,
    BG_IC_NPC_GLAIVE_THROWER_2_H,

    BG_IC_NPC_CATAPULT_1_A,
    BG_IC_NPC_CATAPULT_2_A,
    BG_IC_NPC_CATAPULT_3_A,
    BG_IC_NPC_CATAPULT_4_A,

    BG_IC_NPC_CATAPULT_1_H,
    BG_IC_NPC_CATAPULT_2_H,
    BG_IC_NPC_CATAPULT_3_H,
    BG_IC_NPC_CATAPULT_4_H,

    BG_IC_NPC_WORLD_TRIGGER_NOT_FLOATING,
    BG_IC_NPC_GUNSHIP_CAPTAIN_1,
    BG_IC_NPC_GUNSHIP_CAPTAIN_2,

    BG_IC_NPC_SPIRIT_GUIDE_1,
    BG_IC_NPC_SPIRIT_GUIDE_2,
    BG_IC_NPC_SPIRIT_GUIDE_3,
    BG_IC_NPC_SPIRIT_GUIDE_4,
    BG_IC_NPC_SPIRIT_GUIDE_5,
    BG_IC_NPC_SPIRIT_GUIDE_6,
    BG_IC_NPC_SPIRIT_GUIDE_7
};

enum BannersTypes
{
    BANNER_A_CONTROLLED,
    BANNER_A_CONTESTED,
    BANNER_H_CONTROLLED,
    BANNER_H_CONTESTED
};

enum BG_IC_MaxSpawns
{
    MAX_NORMAL_GAMEOBJECTS_SPAWNS                       = BG_IC_GO_DOODAD_ND_WINTERORC_WALL_GATEFX_DOOR03+1,
    MAX_NORMAL_NPCS_SPAWNS                              = BG_IC_NPC_KEEP_CANNON_24+1,
    MAX_WORKSHOP_SPAWNS                                 = 10,
    MAX_DOCKS_SPAWNS                                    = 12,
    MAX_SPIRIT_GUIDES_SPAWNS                            = 7,
    MAX_HANGAR_TELEPORTERS_SPAWNS                       = 3,
    MAX_HANGAR_TELEPORTER_EFFECTS_SPAWNS                = 3,
    MAX_AIRSHIPS_SPAWNS                                 = 2,
    MAX_FORTRESS_GATES_SPAWNS                           = 6,
    MAX_FORTRESS_TELEPORTERS_SPAWNS                     = 12,
    MAX_FORTRESS_TELEPORTER_EFFECTS_SPAWNS              = 12,
    MAX_HANGAR_NPCS_SPAWNS                              = 3,

    // docks
    MAX_GLAIVE_THROWERS_SPAWNS_PER_FACTION              = 2,
    MAX_CATAPULTS_SPAWNS_PER_FACTION                    = 4,

    // workshop
    MAX_DEMOLISHERS_SPAWNS_PER_FACTION                  = 4,
    MAX_WORKSHOP_BOMBS_SPAWNS_PER_FACTION               = 2,

    // Hangar
    MAX_TRIGGER_SPAWNS_PER_FACTION                      = 1,
    MAX_CAPTAIN_SPAWNS_PER_FACTION                      = 2,
};

const ICNpc BG_IC_NpcSpawnlocs[MAX_NORMAL_NPCS_SPAWNS] =
{
    {BG_IC_NPC_OVERLORD_AGMAR, NPC_OVERLORD_AGMAR, TEAM_HORDE, 1295.44f, -765.733f, 70.0541f, 0.0f}, //Overlord Agmar 1
    {BG_IC_NPC_HIGH_COMMANDER_HALFORD_WYRMBANE, NPC_HIGH_COMMANDER_HALFORD_WYRMBANE, TEAM_ALLIANCE, 224.983f, -831.573f, 60.9034f, 0.0f}, //High Commander Halford Wyrmbane 2
    {BG_IC_NPC_KOR_KRON_GUARD_1, NPC_KOR_KRON_GUARD, TEAM_HORDE, 1296.01f, -773.256f, 69.958f, 0.292168f}, // 3
    {BG_IC_NPC_KOR_KRON_GUARD_2, NPC_KOR_KRON_GUARD, TEAM_HORDE, 1295.94f, -757.756f, 69.9587f, 6.02165f}, // 4
    {BG_IC_NPC_KOR_KRON_GUARD_3, NPC_KOR_KRON_GUARD, TEAM_HORDE, 1295.09f, -760.927f, 69.9587f, 5.94311f}, // 5
    {BG_IC_NPC_KOR_KRON_GUARD_4, NPC_KOR_KRON_GUARD, TEAM_HORDE, 1295.13f, -769.7f, 69.95f, 0.34f}, // 6

    {BG_IC_NPC_SEVEN_TH_LEGION_INFANTRY_1, NPC_SEVEN_TH_LEGION_INFANTRY, TEAM_ALLIANCE, 223.969f, -822.958f, 60.8151f, 0.46337f}, // 7
    {BG_IC_NPC_SEVEN_TH_LEGION_INFANTRY_2, NPC_SEVEN_TH_LEGION_INFANTRY, TEAM_ALLIANCE, 224.211f, -826.952f, 60.8188f, 6.25961f}, // 8
    {BG_IC_NPC_SEVEN_TH_LEGION_INFANTRY_3, NPC_SEVEN_TH_LEGION_INFANTRY, TEAM_ALLIANCE, 223.119f, -838.386f, 60.8145f, 5.64857f}, // 9
    {BG_IC_NPC_SEVEN_TH_LEGION_INFANTRY_4, NPC_SEVEN_TH_LEGION_INFANTRY, TEAM_ALLIANCE, 223.889f, -835.102f, 60.8201f, 6.21642f}, // 10

    {BG_IC_NPC_KEEP_CANNON_1, NPC_KEEP_CANNON, TEAM_ALLIANCE, 415.825f, -754.634f, 87.799f, 1.78024f}, // 11
    {BG_IC_NPC_KEEP_CANNON_2, NPC_KEEP_CANNON, TEAM_ALLIANCE, 410.142f, -755.332f, 87.7991f, 1.78024f}, // 12
    {BG_IC_NPC_KEEP_CANNON_3, NPC_KEEP_CANNON, TEAM_ALLIANCE, 424.33f, -879.352f, 88.0446f, 0.436332f}, // 13
    {BG_IC_NPC_KEEP_CANNON_4, NPC_KEEP_CANNON, TEAM_ALLIANCE, 425.602f, -786.646f, 87.7991f, 5.74213f}, // 14
    {BG_IC_NPC_KEEP_CANNON_5, NPC_KEEP_CANNON, TEAM_ALLIANCE, 426.743f, -884.939f, 87.9613f, 0.436332f}, // 15
    {BG_IC_NPC_KEEP_CANNON_6, NPC_KEEP_CANNON, TEAM_ALLIANCE, 404.736f, -755.495f, 87.7989f, 1.78024f}, // 16
    {BG_IC_NPC_KEEP_CANNON_7, NPC_KEEP_CANNON, TEAM_ALLIANCE, 428.375f, -780.797f, 87.7991f, 5.79449f}, // 17
    {BG_IC_NPC_KEEP_CANNON_8, NPC_KEEP_CANNON, TEAM_ALLIANCE, 429.175f, -890.436f, 88.0446f, 0.436332f}, // 18
    {BG_IC_NPC_KEEP_CANNON_9, NPC_KEEP_CANNON, TEAM_ALLIANCE, 430.872f, -775.278f, 87.7991f, 5.88176f}, // 19
    {BG_IC_NPC_KEEP_CANNON_10, NPC_KEEP_CANNON, TEAM_ALLIANCE, 408.056f, -911.283f, 88.0445f, 4.64258f}, // 20
    {BG_IC_NPC_KEEP_CANNON_11, NPC_KEEP_CANNON, TEAM_ALLIANCE, 413.609f, -911.566f, 88.0447f, 4.66003f}, // 21
    {BG_IC_NPC_KEEP_CANNON_12, NPC_KEEP_CANNON, TEAM_ALLIANCE, 402.554f, -910.557f, 88.0446f, 4.57276f}, // 22

    {BG_IC_NPC_KEEP_CANNON_13, NPC_KEEP_CANNON, TEAM_HORDE, 1158.91f, -660.144f, 87.9332f, 0.750492f}, // 23
    {BG_IC_NPC_KEEP_CANNON_14, NPC_KEEP_CANNON, TEAM_HORDE, 1156.22f, -866.809f, 87.8754f, 5.27089f}, // 24
    {BG_IC_NPC_KEEP_CANNON_15, NPC_KEEP_CANNON, TEAM_HORDE, 1163.74f, -663.67f, 88.3571f, 0.558505f}, // 25
    {BG_IC_NPC_KEEP_CANNON_16, NPC_KEEP_CANNON, TEAM_HORDE, 1135.18f, -683.896f, 88.0409f, 3.9619f}, // 26
    {BG_IC_NPC_KEEP_CANNON_17, NPC_KEEP_CANNON, TEAM_HORDE, 1138.91f, -836.359f, 88.3728f, 2.18166f}, // 27
    {BG_IC_NPC_KEEP_CANNON_18, NPC_KEEP_CANNON, TEAM_HORDE, 1162.08f, -863.717f, 88.358f, 5.48033f}, // 28
    {BG_IC_NPC_KEEP_CANNON_19, NPC_KEEP_CANNON, TEAM_HORDE, 1167.13f, -669.212f, 87.9682f, 0.383972f}, // 29
    {BG_IC_NPC_KEEP_CANNON_20, NPC_KEEP_CANNON, TEAM_HORDE, 1137.72f, -688.517f, 88.4023f, 3.9619f}, // 30
    {BG_IC_NPC_KEEP_CANNON_21, NPC_KEEP_CANNON, TEAM_HORDE, 1135.29f, -840.878f, 88.0252f, 2.30383f}, // 31
    {BG_IC_NPC_KEEP_CANNON_22, NPC_KEEP_CANNON, TEAM_HORDE, 1144.33f, -833.309f, 87.9268f, 2.14675f}, // 32
    {BG_IC_NPC_KEEP_CANNON_23, NPC_KEEP_CANNON, TEAM_HORDE, 1142.59f, -691.946f, 87.9756f, 3.9619f}, // 33
    {BG_IC_NPC_KEEP_CANNON_24, NPC_KEEP_CANNON, TEAM_HORDE, 1166.13f, -858.391f, 87.9653f, 5.63741f} // 34
};

const Position BG_IC_WorkshopVehicles[5] =
{
    {751.8281f, -852.732666f, 12.5250978f, 1.46607661f}, // Demolisher
    {761.809f, -854.2274f, 12.5263243f, 1.46607661f}, // Demolisher
    {783.4722f, -853.9601f, 12.54775f, 1.71042264f}, // Demolisher
    {793.055542f, -852.71875f, 12.5671329f, 1.71042264f}, // Demolisher
    {773.680542f, -884.092041f, 16.8090363f, 1.58824956f} // Siege Engine
};

const Position BG_IC_DocksVehiclesGlaives[2] =
{
    {779.3125f, -342.972229f, 12.2104874f, 4.712389f}, // Glaive Throwers
    {790.029541f, -342.899323f, 12.2128582f, 4.71238f} // Glaive Throwers
};

const Position BG_IC_DocksVehiclesCatapults[4] =
{
    {757.283f, -341.7795f, 12.2113762f, 4.729842f}, // Catapults
    {766.947937f, -342.053833f, 12.2009945f, 4.694f}, // Catapults
    {800.3785f, -342.607635f, 12.1669979f, 4.6774f}, // Catapults
    {810.7257f, -342.083344f, 12.1675768f, 4.6600f} // Catapults
};

const Position BG_IC_HangarTeleporters[3] =
{
    {827.9219f, -993.3249f, 134.1972f, 3.141593f}, // Gunship Portal
    {739.0226f, -1106.661f, 134.7551f, 2.426008f}, // Gunship Portal
    {672.0799f, -1156.776f, 133.7057f, 1.832595f} // Gunship Portal
};

const Position BG_IC_HangarTeleporterEffects[3] =
{
    {827.9236f, -993.2986f, 134.2002f, 3.141593f}, // Gunship Portal Effect
    {739.0139f, -1106.661f, 134.7548f, 3.141593f}, // Gunship Portal Effect
    {672.0868f, -1156.786f, 133.7057f, 3.141593f} // Gunship Portal Effect
};

const Position BG_IC_HangarTrigger[2] =
{
    {11.69965f, 0.034146f, 20.62076f, 3.211406f},
    {7.305609f, -0.095246f, 34.51022f, 3.159046f}
};

const Position BG_IC_HangarCaptains[4] =
{
    {825.6667f, -994.00520f, 134.3569f, 3.403392f},
    {53.65112f, -0.1139221f, 30.09546f, 3.106686f},
    {826.2205f, -994.40280f, 134.2812f, 3.351032f},
    {10.89952f, 4.88029700f, 20.49038f, 4.840575f}
};

struct ICGo
{
    uint32 type;
    uint32 entry;
    float x;
    float y;
    float z;
    float o;
};

const ICGo BG_IC_Teleporters[MAX_FORTRESS_TELEPORTERS_SPAWNS] =
{
    {BG_IC_GO_TELEPORTER_1_1, GO_TELEPORTER_1, 1143.25f, -779.599f, 48.629f, 1.64061f}, // Teleporter
    {BG_IC_GO_TELEPORTER_1_2, GO_TELEPORTER_1, 1236.53f, -669.415f, 48.2729f, 0.104719f}, // Teleporter
    {BG_IC_GO_TELEPORTER_2_1, GO_TELEPORTER_2, 1233.27f, -844.526f, 48.8824f, -0.0174525f}, // Teleporter
    {BG_IC_GO_TELEPORTER_3_1, GO_TELEPORTER_3, 311.92f, -913.972f, 48.8159f, 3.08918f}, // Teleporter
    {BG_IC_GO_TELEPORTER_2_2, GO_TELEPORTER_2, 1235.53f, -683.872f, 49.304f, -3.08918f}, // Teleporter
    {BG_IC_GO_TELEPORTER_4_1, GO_TELEPORTER_4, 397.089f, -859.382f, 48.8993f, 1.64061f}, // Teleporter
    {BG_IC_GO_TELEPORTER_3_2, GO_TELEPORTER_3, 324.635f, -749.128f, 49.3602f, 0.0174525f}, // Teleporter
    {BG_IC_GO_TELEPORTER_3_3, GO_TELEPORTER_3, 425.675f, -857.09f, 48.5104f, -1.6057f}, // Teleporter
    {BG_IC_GO_TELEPORTER_4_2, GO_TELEPORTER_4, 323.54f, -888.361f, 48.9197f, 0.0349063f}, // Teleporter
    {BG_IC_GO_TELEPORTER_4_3, GO_TELEPORTER_4, 326.285f, -777.366f, 49.0208f, 3.12412f}, // Teleporter
    {BG_IC_GO_TELEPORTER_1_3, GO_TELEPORTER_1, 1235.09f, -857.898f, 48.9163f, 3.07177f}, // Teleporter
    {BG_IC_GO_TELEPORTER_2_3, GO_TELEPORTER_2, 1158.76f, -746.182f, 48.6277f, -1.51844f} // Teleporter
};

const ICGo BG_IC_TeleporterEffects[MAX_FORTRESS_TELEPORTER_EFFECTS_SPAWNS] =
{
    {BG_IC_GO_TELEPORTER_EFFECTS_A_1, GO_TELEPORTER_EFFECTS_A, 425.686f, -857.092f, 48.51f, -1.62316f}, // Teleporter Effects (Alliance)
    {BG_IC_GO_TELEPORTER_EFFECTS_A_2, GO_TELEPORTER_EFFECTS_A, 324.634f, -749.148f, 49.359f, 0.0174525f}, // Teleporter Effects (Alliance)
    {BG_IC_GO_TELEPORTER_EFFECTS_A_3, GO_TELEPORTER_EFFECTS_A, 311.911f, -913.986f, 48.8157f, 3.08918f}, // Teleporter Effects (Alliance)
    {BG_IC_GO_TELEPORTER_EFFECTS_A_4, GO_TELEPORTER_EFFECTS_A, 326.266f, -777.347f, 49.0215f, 3.12412f}, // Teleporter Effects (Alliance)
    {BG_IC_GO_TELEPORTER_EFFECTS_A_5, GO_TELEPORTER_EFFECTS_A, 323.55f, -888.347f, 48.9198f, 0.0174525f}, // Teleporter Effects (Alliance)
    {BG_IC_GO_TELEPORTER_EFFECTS_A_6, GO_TELEPORTER_EFFECTS_A, 397.116f, -859.378f, 48.8989f, 1.64061f}, // Teleporter Effects (Alliance)

    {BG_IC_GO_TELEPORTER_EFFECTS_H_1, GO_TELEPORTER_EFFECTS_H, 1143.25f, -779.623f, 48.6291f, 1.62316f}, // Teleporter Effects (Horde)
    {BG_IC_GO_TELEPORTER_EFFECTS_H_2, GO_TELEPORTER_EFFECTS_H, 1158.64f, -746.148f, 48.6277f, -1.50098f}, // Teleporter Effects (Horde)
    {BG_IC_GO_TELEPORTER_EFFECTS_H_3, GO_TELEPORTER_EFFECTS_H, 1233.25f, -844.573f, 48.8836f, 0.0174525f}, // Teleporter Effects (Horde)
    {BG_IC_GO_TELEPORTER_EFFECTS_H_4, GO_TELEPORTER_EFFECTS_H, 1235.07f, -857.957f, 48.9163f, 3.05433f}, // Teleporter Effects (Horde)
    {BG_IC_GO_TELEPORTER_EFFECTS_H_5, GO_TELEPORTER_EFFECTS_H, 1236.46f, -669.344f, 48.2684f, 0.087266f}, // Teleporter Effects (Horde)
    {BG_IC_GO_TELEPORTER_EFFECTS_H_6, GO_TELEPORTER_EFFECTS_H, 1235.6f, -683.806f, 49.3028f, -3.07177f} // Teleporter Effects (Horde)
};

const ICGo BG_IC_ObjSpawnlocs[MAX_NORMAL_GAMEOBJECTS_SPAWNS] =
{
    {BG_IC_GO_ALLIANCE_GATE_1, GO_ALLIANCE_GATE_1, 351.615f, -762.75f, 48.9162f, -1.5708f}, // Alliance Gate || Left
    {BG_IC_GO_ALLIANCE_GATE_2, GO_ALLIANCE_GATE_2, 351.024f, -903.326f, 48.9247f, 1.5708f}, // Alliance Gate || Right
    {BG_IC_GO_ALLIANCE_GATE_3, GO_ALLIANCE_GATE_3, 413.479f, -833.95f, 48.5238f, 3.14159f}, // Alliance Gate || Front

    {BG_IC_GO_HORDE_GATE_1, GO_HORDE_GATE_1, 1150.9f, -762.606f, 47.5077f, 3.14159f}, // Horde Gate || Front
    {BG_IC_GO_HORDE_GATE_2, GO_HORDE_GATE_2, 1218.74f, -851.155f, 48.2533f, -1.5708f}, // Horde Gate || Left
    {BG_IC_GO_HORDE_GATE_3, GO_HORDE_GATE_3, 1217.9f, -676.948f, 47.6341f, 1.5708f}, // Horde Gate || Right

    {BG_IC_GO_HORDE_BANNER, GO_HORDE_BANNER, 1284.76f, -705.668f, 48.9163f, -3.08918f}, // Horde Banner
    {BG_IC_GO_ALLIANCE_BANNER, GO_ALLIANCE_BANNER, 299.153f, -784.589f, 48.9162f, -0.157079f}, // Alliance Banner

    {BG_IC_GO_WORKSHOP_BANNER, GO_WORKSHOP_BANNER, 776.229f, -804.283f, 6.45052f, 1.6057f}, // Workshop Banner
    {BG_IC_GO_DOCKS_BANNER, GO_DOCKS_BANNER, 726.385f, -360.205f, 17.8153f, -1.62316f}, // Docks Banner
    {BG_IC_GO_HANGAR_BANNER, GO_HANGAR_BANNER, 807.78f, -1000.07f, 132.381f, -1.93732f}, // Hangar Banner
    {BG_IC_GO_QUARRY_BANNER, GO_QUARRY_BANNER, 251.016f, -1159.32f, 17.2376f, -2.25147f}, // Quarry Banner
    {BG_IC_GO_REFINERY_BANNER, GO_REFINERY_BANNER, 1269.5f, -400.809f, 37.6253f, -1.76278f}, // Refinery Banner

    {BG_IC_GO_BENCH_1, GO_BENCH_1, 834.208f, -461.826f, 22.3067f, 1.5708f}, // Bench
    {BG_IC_GO_BENCH_2, GO_BENCH_2, 826.153f, -461.985f, 22.5149f, 1.5708f}, // Bench
    {BG_IC_GO_BENCH_3, GO_BENCH_3, 817.446f, -470.47f, 25.372f, -1.56207f}, // Bench
    {BG_IC_GO_BENCH_4, GO_BENCH_4, 827.001f, -474.415f, 25.372f, 1.57952f}, // Bench
    {BG_IC_GO_BENCH_5, GO_BENCH_5, 819.264f, -461.961f, 22.7614f, 1.57952f}, // Bench

    {BG_IC_GO_BONFIRE_1, GO_BONFIRE_1, 1162.91f, -734.578f, 48.8948f, -2.9845f}, // Bonfire
    {BG_IC_GO_BONFIRE_2, GO_BONFIRE_2, 1282.34f, -799.762f, 87.1357f, -3.13286f}, // Bonfire
    {BG_IC_GO_BONFIRE_3, GO_BONFIRE_3, 1358.06f, -732.178f, 87.1606f, -3.13284f}, // Bonfire
    {BG_IC_GO_BONFIRE_4, GO_BONFIRE_4, 1281.76f, -732.844f, 87.1574f, -3.13246f}, // Bonfire
    {BG_IC_GO_BONFIRE_5, GO_BONFIRE_5, 1358.81f, -797.899f, 87.2953f, 3.13312f}, // Bonfire
    {BG_IC_GO_BONFIRE_6, GO_BONFIRE_6, 1162.21f, -790.543f, 48.9162f, 2.27765f}, // Bonfire

    {BG_IC_GO_BRAZIER_1, GO_BRAZIER_1, 1262.21f, -751.358f, 48.8133f, 2.26893f}, // Brazier
    {BG_IC_GO_BRAZIER_2, GO_BRAZIER_2, 1262.58f, -781.861f, 48.8132f, 2.04203f}, // Brazier
    {BG_IC_GO_BRAZIER_3, GO_BRAZIER_3, 223.818f, -839.352f, 60.7917f, 1.09083f}, // Brazier
    {BG_IC_GO_BRAZIER_4, GO_BRAZIER_4, 224.277f, -822.77f, 60.7917f, 2.06822f}, // Brazier

    {BG_IC_GO_CHAIR_1, GO_CHAIR_1, 632.876f, -282.461f, 5.45364f, -0.851094f}, // Chair
    {BG_IC_GO_CHAIR_2, GO_CHAIR_2, 635.796f, -276.295f, 5.48659f, -3.03273f}, // Chair
    {BG_IC_GO_CHAIR_3_1, GO_CHAIR_3, 762.245f, -444.795f, 22.8526f, -1.98095f}, // Chair
    {BG_IC_GO_CHAIR_4, GO_CHAIR_4, 632.156f, -304.503f, 5.4879f, 1.15603f}, // Chair
    {BG_IC_GO_CHAIR_5, GO_CHAIR_5, 643.86f, -270.204f, 5.48898f, 2.36903f}, // Chair
    {BG_IC_GO_CHAIR_6_1, GO_CHAIR_6, 902.234f, -455.508f, 18.3935f, -1.00356f}, // Chair
    {BG_IC_GO_CHAIR_7, GO_CHAIR_7, 810.237f, -461.2f, 25.4627f, 1.5708f}, // Chair
    {BG_IC_GO_CHAIR_3_2, GO_CHAIR_3, 1117.19f, -365.674f, 18.8456f, 0.968657f}, // Chair
    {BG_IC_GO_CHAIR_6_2, GO_CHAIR_6, 1066.19f, -337.214f, 18.8225f, 0.453785f}, // Chair
    {BG_IC_GO_CHAIR_8_1, GO_CHAIR_8, 798.324f, -444.951f, 22.5601f, -1.02102f}, // Chair
    {BG_IC_GO_CHAIR_8_2, GO_CHAIR_8, 1081.81f, -358.637f, 18.5531f, 1.92859f}, // Chair
    {BG_IC_GO_CHAIR_9, GO_CHAIR_9, 814.931f, -470.816f, 33.6373f, -3.12412f}, // Chair

    {BG_IC_GO_DOODAD_HU_PORTCULLIS01_1, GO_DOODAD_HU_PORTCULLIS01, 401.024f, -780.724f, 49.9482f, -2.52896f}, // Doodad_HU_Portcullis01
    {BG_IC_GO_DOODAD_HU_PORTCULLIS01_2, GO_DOODAD_HU_PORTCULLIS01, 399.802f, -885.208f, 50.1939f, 2.516f}, // Doodad_HU_Portcullis01

    {BG_IC_GO_DOODAD_ND_HUMAN_GATE_CLOSEDFX_DOOR01, GO_DOODAD_ND_HUMAN_GATE_CLOSEDFX_DOOR01, 413.479f, -833.95f, 48.5238f, 3.14159f}, // Doodad_ND_Human_Gate_ClosedFX_Door01
    {BG_IC_GO_DOODAD_ND_HUMAN_GATE_CLOSEDFX_DOOR02, GO_DOODAD_ND_HUMAN_GATE_CLOSEDFX_DOOR01, 351.615f, -762.75f, 48.91625f, 4.71292f}, // Doodad_ND_Human_Gate_ClosedFX_Door01
    {BG_IC_GO_DOODAD_ND_HUMAN_GATE_CLOSEDFX_DOOR03, GO_DOODAD_ND_HUMAN_GATE_CLOSEDFX_DOOR01, 351.024f, -903.33f, 48.92472f, 1.570796f}, // Doodad_ND_Human_Gate_ClosedFX_Door01

    {BG_IC_GO_DOODAD_PORTCULLISACTIVE01, GO_DOODAD_PORTCULLISACTIVE01, -832.595f, 51.4109f, -0.0261791f, 0.0f}, // Doodad_PortcullisActive01

    {BG_IC_GO_DOODAD_PORTCULLISACTIVE02, GO_DOODAD_PORTCULLISACTIVE02, 273.033f, -832.199f, 51.4109f, -0.0261791f}, // Doodad_PortcullisActive02

    {BG_IC_GO_DOODAD_VR_PORTCULLIS01_1, GO_DOODAD_VR_PORTCULLIS01, 1156.89f, -843.998f, 48.6322f, 0.732934f}, // Doodad_VR_Portcullis01
    {BG_IC_GO_DOODAD_VR_PORTCULLIS01_2, GO_DOODAD_VR_PORTCULLIS01, 1157.05f, -682.36f, 48.6322f, -0.829132f}, // Doodad_VR_Portcullis01

    {BG_IC_GO_FLAGPOLE_1_1, GO_FLAGPOLE_1, -400.809f, 37.6253f, -1.76278f, 0.0f}, // Flagpole
    {BG_IC_GO_FLAGPOLE_2_1, GO_FLAGPOLE_2, 1284.76f, -705.668f, 48.9163f, -3.08918f}, // Flagpole
    {BG_IC_GO_FLAGPOLE_2_2, GO_FLAGPOLE_2, 299.153f, -784.589f, 48.9162f, -0.157079f}, // Flagpole
    {BG_IC_GO_FLAGPOLE_1_2, GO_FLAGPOLE_1, 726.385f, -360.205f, 17.8153f, -1.6057f}, // Flagpole
    {BG_IC_GO_FLAGPOLE_1_3, GO_FLAGPOLE_1, 807.78f, -1000.07f, 132.381f, -1.91986f}, // Flagpole
    {BG_IC_GO_FLAGPOLE_1_4, GO_FLAGPOLE_1, 776.229f, -804.283f, 6.45052f, 1.6057f}, // Flagpole
    {BG_IC_GO_FLAGPOLE_1_5, GO_FLAGPOLE_1, 251.016f, -1159.32f, 17.2376f, -2.25147f}, // Flagpole
    {BG_IC_GO_FLAGPOLE_1_6, GO_FLAGPOLE_1, 1269.502f, -400.809f, 37.62525f, -1.762782f}, // Flagpole

    {BG_IC_GO_HORDE_KEEP_PORTCULLIS, GO_HORDE_KEEP_PORTCULLIS, 1283.05f, -765.878f, 50.8297f, -3.13286f}, // Horde Keep Portcullis

    {BG_IC_GO_STOVE_1_1, GO_STOVE_1, 903.291f, -457.345f, 18.1356f, 2.23402f}, // Stove
    {BG_IC_GO_STOVE_2_1, GO_STOVE_2, 761.462f, -446.684f, 22.5602f, 0.244344f}, // Stove
    {BG_IC_GO_STOVE_1_2, GO_STOVE_1, 11068.13f, -336.373f, 18.5647f, -2.59181f}, // Stove
    {BG_IC_GO_STOVE_2_2, GO_STOVE_2, 1118.32f, -363.969f, 18.5532f, -3.08918f}, // Stove

    {BG_IC_GO_HUGE_SEAFORIUM_BOMBS_A_1, GO_HUGE_SEAFORIUM_BOMB_A, 297.3212f, -851.321167f, 48.91627f, -0.94247663f},
    {BG_IC_GO_HUGE_SEAFORIUM_BOMBS_A_2, GO_HUGE_SEAFORIUM_BOMB_A, 298.104156f, -861.026062f, 48.916275f, -2.75761318f},
    {BG_IC_GO_HUGE_SEAFORIUM_BOMBS_A_3, GO_HUGE_SEAFORIUM_BOMB_A, 300.371521f, -818.732666f, 48.91625f, 0.785396755f},
    {BG_IC_GO_HUGE_SEAFORIUM_BOMBS_A_4, GO_HUGE_SEAFORIUM_BOMB_A, 302.1354f, -810.7083f, 48.91625f, -1.04719758f},

    {BG_IC_GO_HUGE_SEAFORIUM_BOMBS_H_1, GO_HUGE_SEAFORIUM_BOMB_H, 1268.30908f, -745.783f, 48.9187775f, 0.785396755f},
    {BG_IC_GO_HUGE_SEAFORIUM_BOMBS_H_2, GO_HUGE_SEAFORIUM_BOMB_H, 1268.50867f, -738.1215f, 48.9175f, -1.04719758f},
    {BG_IC_GO_HUGE_SEAFORIUM_BOMBS_H_3, GO_HUGE_SEAFORIUM_BOMB_H, 1273.066f, -786.572937f, 48.9419174f, -0.94247663f},
    {BG_IC_GO_HUGE_SEAFORIUM_BOMBS_H_4, GO_HUGE_SEAFORIUM_BOMB_H, 1273.849f, -796.2778f, 48.9364281f, -2.75761318f},

    {BG_IC_GO_DOODAD_ND_WINTERORC_WALL_GATEFX_DOOR01, GO_DOODAD_ND_WINTERORC_WALL_GATEFX_DOOR01, 1150.903f, -762.6059f, 47.50768f, 3.141593f},
    {BG_IC_GO_DOODAD_ND_WINTERORC_WALL_GATEFX_DOOR02, GO_DOODAD_ND_WINTERORC_WALL_GATEFX_DOOR01, 1217.899f, -676.9479f, 47.63408f, 1.570796f},
    {BG_IC_GO_DOODAD_ND_WINTERORC_WALL_GATEFX_DOOR03, GO_DOODAD_ND_WINTERORC_WALL_GATEFX_DOOR01, 1218.743f, -851.1545f, 48.25328f, 4.712392f}
};

const Position workshopBombs[2] =
{
    {750.601f, -864.597f, 13.4754f, 1.93731f},
    {785.509f, -864.715f, 13.3993f, 2.47837f}
};

enum Spells
{
    SPELL_OIL_REFINERY                      = 68719,
    SPELL_QUARRY                            = 68720,
    SPELL_PARACHUTE                         = 66656,
    SPELL_SLOW_FALL                         = 12438,
    SPELL_DESTROYED_VEHICLE_ACHIEVEMENT     = 68357,
    SPELL_BACK_DOOR_JOB_ACHIEVEMENT         = 68502,
    SPELL_DRIVING_CREDIT_DEMOLISHER         = 68365,
    SPELL_DRIVING_CREDIT_GLAIVE             = 68363,
    SPELL_DRIVING_CREDIT_SIEGE              = 68364,
    SPELL_DRIVING_CREDIT_CATAPULT           = 68362,
    SPELL_SIMPLE_TELEPORT                   = 12980,
    SPELL_TELEPORT_VISUAL_ONLY              = 51347,
    SPELL_PARACHUTE_IC                      = 66657,
    SPELL_LAUNCH_NO_FALLING_DAMAGE          = 66251
};

enum BG_IC_Objectives
{
    IC_OBJECTIVE_ASSAULT_BASE   = 245,
    IC_OBJECTIVE_DEFEND_BASE    = 246
};

enum ICWorldStates
{
    BG_IC_ALLIANCE_RENFORT_SET          = 4221,
    BG_IC_HORDE_RENFORT_SET             = 4222,
    BG_IC_ALLIANCE_RENFORT              = 4226,
    BG_IC_HORDE_RENFORT                 = 4227,
    BG_IC_GATE_FRONT_H_WS_CLOSED        = 4317,
    BG_IC_GATE_WEST_H_WS_CLOSED         = 4318,
    BG_IC_GATE_EAST_H_WS_CLOSED         = 4319,
    BG_IC_GATE_FRONT_A_WS_CLOSED        = 4328,
    BG_IC_GATE_WEST_A_WS_CLOSED         = 4327,
    BG_IC_GATE_EAST_A_WS_CLOSED         = 4326,
    BG_IC_GATE_FRONT_H_WS_OPEN          = 4322,
    BG_IC_GATE_WEST_H_WS_OPEN           = 4321,
    BG_IC_GATE_EAST_H_WS_OPEN           = 4320,
    BG_IC_GATE_FRONT_A_WS_OPEN          = 4323,
    BG_IC_GATE_WEST_A_WS_OPEN           = 4324,
    BG_IC_GATE_EAST_A_WS_OPEN           = 4325,

    BG_IC_DOCKS_UNCONTROLLED            = 4301,
    BG_IC_DOCKS_CONFLICT_A              = 4305,
    BG_IC_DOCKS_CONFLICT_H              = 4302,
    BG_IC_DOCKS_CONTROLLED_A            = 4304,
    BG_IC_DOCKS_CONTROLLED_H            = 4303,

    BG_IC_HANGAR_UNCONTROLLED           = 4296,
    BG_IC_HANGAR_CONFLICT_A             = 4300,
    BG_IC_HANGAR_CONFLICT_H             = 4297,
    BG_IC_HANGAR_CONTROLLED_A           = 4299,
    BG_IC_HANGAR_CONTROLLED_H           = 4298,

    BG_IC_QUARRY_UNCONTROLLED           = 4306,
    BG_IC_QUARRY_CONFLICT_A             = 4310,
    BG_IC_QUARRY_CONFLICT_H             = 4307,
    BG_IC_QUARRY_CONTROLLED_A           = 4309,
    BG_IC_QUARRY_CONTROLLED_H           = 4308,

    BG_IC_REFINERY_UNCONTROLLED         = 4311,
    BG_IC_REFINERY_CONFLICT_A           = 4315,
    BG_IC_REFINERY_CONFLICT_H           = 4312,
    BG_IC_REFINERY_CONTROLLED_A         = 4314,
    BG_IC_REFINERY_CONTROLLED_H         = 4313,

    BG_IC_WORKSHOP_UNCONTROLLED         = 4294,
    BG_IC_WORKSHOP_CONFLICT_A           = 4228,
    BG_IC_WORKSHOP_CONFLICT_H           = 4293,
    BG_IC_WORKSHOP_CONTROLLED_A         = 4229,
    BG_IC_WORKSHOP_CONTROLLED_H         = 4230,

    BG_IC_ALLIANCE_KEEP_UNCONTROLLED    = 4341,
    BG_IC_ALLIANCE_KEEP_CONFLICT_A      = 4342,
    BG_IC_ALLIANCE_KEEP_CONFLICT_H      = 4343,
    BG_IC_ALLIANCE_KEEP_CONTROLLED_A    = 4339,
    BG_IC_ALLIANCE_KEEP_CONTROLLED_H    = 4340,

    BG_IC_HORDE_KEEP_UNCONTROLLED       = 4346,
    BG_IC_HORDE_KEEP_CONFLICT_A         = 4347,
    BG_IC_HORDE_KEEP_CONFLICT_H         = 4348,
    BG_IC_HORDE_KEEP_CONTROLLED_A       = 4344,
    BG_IC_HORDE_KEEP_CONTROLLED_H       = 4345
};

enum BG_IC_GateState
{
    BG_IC_GATE_OK           = 1,
    BG_IC_GATE_DAMAGED      = 2,
    BG_IC_GATE_DESTROYED    = 3
};

enum ICDoorList
{
    BG_IC_H_FRONT,
    BG_IC_H_WEST,
    BG_IC_H_EAST,
    BG_IC_A_FRONT,
    BG_IC_A_WEST,
    BG_IC_A_EAST,
    BG_IC_MAXDOOR
};

enum ICNodePointType
{
    NODE_TYPE_REFINERY,
    NODE_TYPE_QUARRY,
    NODE_TYPE_DOCKS,
    NODE_TYPE_HANGAR,
    NODE_TYPE_WORKSHOP,

    // Graveyards
    NODE_TYPE_GRAVEYARD_A,
    NODE_TYPE_GRAVEYARD_H,

    MAX_NODE_TYPES
};

enum ICNodeState
{
    NODE_STATE_UNCONTROLLED = 0,
    NODE_STATE_CONFLICT_A,
    NODE_STATE_CONFLICT_H,
    NODE_STATE_CONTROLLED_A,
    NODE_STATE_CONTROLLED_H
};

const uint32 BG_IC_GraveyardIds[MAX_NODE_TYPES+2] = {0, 0, 1480, 1481, 1482, 1485, 1486, 1483, 1484};

Position const BG_IC_SpiritGuidePos[MAX_NODE_TYPES+2] =
{
    {0.0f, 0.0f, 0.0f, 0.0f},                     // no grave
    {0.0f, 0.0f, 0.0f, 0.0f},                     // no grave
    {629.57f, -279.83f, 11.33f, 0.0f},            // dock
    {780.729f, -1103.08f, 135.51f, 2.27f},        // hangar
    {775.74f, -652.77f, 9.31f, 4.27f},            // workshop
    {278.42f, -883.20f, 49.89f, 1.53f},           // alliance starting base
    {1300.91f, -834.04f, 48.91f, 1.69f},          // horde starting base
    {438.86f, -310.04f, 51.81f, 5.87f},           // last resort alliance
    {1148.65f, -1250.98f, 16.60f, 1.74f},         // last resort horde
};

enum ICBroadcastTexts
{
    BG_IC_TEXT_FRONT_GATE_HORDE_DESTROYED       = 35409,
    BG_IC_TEXT_FRONT_GATE_ALLIANCE_DESTROYED    = 35410,
    BG_IC_TEXT_WEST_GATE_HORDE_DESTROYED        = 35411,
    BG_IC_TEXT_WEST_GATE_ALLIANCE_DESTROYED     = 35412,
    BG_IC_TEXT_EAST_GATE_HORDE_DESTROYED        = 35413,
    BG_IC_TEXT_EAST_GATE_ALLIANCE_DESTROYED     = 35414
};

struct ICNodeInfo
{
    uint32 NodeId;
    uint32 TextAssaulted;
    uint32 TextDefended;
    uint32 TextAllianceTaken;
    uint32 TextHordeTaken;
};

ICNodeInfo const ICNodes[MAX_NODE_TYPES] =
{
    { NODE_TYPE_REFINERY,    35377, 35378, 35379, 35380 },
    { NODE_TYPE_QUARRY,      35373, 35374, 35375, 35376 },
    { NODE_TYPE_DOCKS,       35365, 35366, 35367, 35368 },
    { NODE_TYPE_HANGAR,      35369, 35370, 35371, 35372 },
    { NODE_TYPE_WORKSHOP,    35278, 35286, 35279, 35280 },
    { NODE_TYPE_GRAVEYARD_A, 35461, 35459, 35463, 35466 },
    { NODE_TYPE_GRAVEYARD_H, 35462, 35460, 35464, 35465 }
};

// I.E: Hangar, Quarry, Graveyards .. etc
struct ICNodePoint
{
    uint32 gameobject_type; // with this we will get the GameObject of that point
    uint32 gameobject_entry; // what gameobject entry is active here.
    TeamId faction; // who has this node
    ICNodePointType nodeType; // here we can specify if it is graveyards, hangar etc...
    uint32 banners[4]; // the banners that have this point
    bool needChange; // this is used for the 1 minute time period after the point is captured
    uint32 timer; // the same use for needChange
    uint32 last_entry; // the last gameobject_entry
    uint32 worldStates[5]; // the worldstates that represent the node in the map
    ICNodeState nodeState;
};

const ICNodePoint nodePointInitial[MAX_NODE_TYPES] =
{
    {BG_IC_GO_REFINERY_BANNER, GO_REFINERY_BANNER, TEAM_NEUTRAL, NODE_TYPE_REFINERY, {GO_ALLIANCE_BANNER_REFINERY, GO_ALLIANCE_BANNER_REFINERY_CONT, GO_HORDE_BANNER_REFINERY, GO_HORDE_BANNER_REFINERY_CONT}, false, 0, 0, {BG_IC_REFINERY_UNCONTROLLED, BG_IC_REFINERY_CONFLICT_A, BG_IC_REFINERY_CONFLICT_H, BG_IC_REFINERY_CONTROLLED_A, BG_IC_REFINERY_CONTROLLED_H}, NODE_STATE_UNCONTROLLED},
    {BG_IC_GO_QUARRY_BANNER, GO_QUARRY_BANNER, TEAM_NEUTRAL, NODE_TYPE_QUARRY, {GO_ALLIANCE_BANNER_QUARRY, GO_ALLIANCE_BANNER_QUARRY_CONT, GO_HORDE_BANNER_QUARRY, GO_HORDE_BANNER_QUARRY_CONT}, false, 0, 0, {BG_IC_QUARRY_UNCONTROLLED, BG_IC_QUARRY_CONFLICT_A, BG_IC_QUARRY_CONFLICT_H, BG_IC_QUARRY_CONTROLLED_A, BG_IC_QUARRY_CONTROLLED_H}, NODE_STATE_UNCONTROLLED},
    {BG_IC_GO_DOCKS_BANNER, GO_DOCKS_BANNER, TEAM_NEUTRAL, NODE_TYPE_DOCKS, {GO_ALLIANCE_BANNER_DOCK, GO_ALLIANCE_BANNER_DOCK_CONT, GO_HORDE_BANNER_DOCK, GO_HORDE_BANNER_DOCK_CONT}, false, 0, 0, {BG_IC_DOCKS_UNCONTROLLED, BG_IC_DOCKS_CONFLICT_A, BG_IC_DOCKS_CONFLICT_H, BG_IC_DOCKS_CONTROLLED_A, BG_IC_DOCKS_CONTROLLED_H}, NODE_STATE_UNCONTROLLED},
    {BG_IC_GO_HANGAR_BANNER, GO_HANGAR_BANNER, TEAM_NEUTRAL, NODE_TYPE_HANGAR, {GO_ALLIANCE_BANNER_HANGAR, GO_ALLIANCE_BANNER_HANGAR_CONT, GO_HORDE_BANNER_HANGAR, GO_HORDE_BANNER_HANGAR_CONT}, false, 0, 0, {BG_IC_HANGAR_UNCONTROLLED, BG_IC_HANGAR_CONFLICT_A, BG_IC_HANGAR_CONFLICT_H, BG_IC_HANGAR_CONTROLLED_A, BG_IC_HANGAR_CONTROLLED_H}, NODE_STATE_UNCONTROLLED},
    {BG_IC_GO_WORKSHOP_BANNER, GO_WORKSHOP_BANNER, TEAM_NEUTRAL, NODE_TYPE_WORKSHOP, {GO_ALLIANCE_BANNER_WORKSHOP, GO_ALLIANCE_BANNER_WORKSHOP_CONT, GO_HORDE_BANNER_WORKSHOP, GO_HORDE_BANNER_WORKSHOP_CONT}, false, 0, 0, {BG_IC_WORKSHOP_UNCONTROLLED, BG_IC_WORKSHOP_CONFLICT_A, BG_IC_WORKSHOP_CONFLICT_H, BG_IC_WORKSHOP_CONTROLLED_A, BG_IC_WORKSHOP_CONTROLLED_H}, NODE_STATE_UNCONTROLLED},
    {BG_IC_GO_ALLIANCE_BANNER, GO_ALLIANCE_BANNER, TEAM_ALLIANCE, NODE_TYPE_GRAVEYARD_A, {GO_ALLIANCE_BANNER_GRAVEYARD_A, GO_ALLIANCE_BANNER_GRAVEYARD_A_CONT, GO_HORDE_BANNER_GRAVEYARD_A, GO_HORDE_BANNER_GRAVEYARD_A_CONT}, false, 0, 0, {BG_IC_ALLIANCE_KEEP_UNCONTROLLED, BG_IC_ALLIANCE_KEEP_CONFLICT_A, BG_IC_ALLIANCE_KEEP_CONFLICT_H, BG_IC_ALLIANCE_KEEP_CONTROLLED_A, BG_IC_ALLIANCE_KEEP_CONTROLLED_H}, NODE_STATE_CONTROLLED_A},
    {BG_IC_GO_HORDE_BANNER, GO_HORDE_BANNER, TEAM_HORDE, NODE_TYPE_GRAVEYARD_H, {GO_ALLIANCE_BANNER_GRAVEYARD_H, GO_ALLIANCE_BANNER_GRAVEYARD_H_CONT, GO_HORDE_BANNER_GRAVEYARD_H, GO_HORDE_BANNER_GRAVEYARD_H_CONT}, false, 0, 0, {BG_IC_HORDE_KEEP_UNCONTROLLED, BG_IC_HORDE_KEEP_CONFLICT_A, BG_IC_HORDE_KEEP_CONFLICT_H, BG_IC_HORDE_KEEP_CONTROLLED_A, BG_IC_HORDE_KEEP_CONTROLLED_H}, NODE_STATE_CONTROLLED_H}
};

enum HonorRewards
{
    RESOURCE_HONOR_AMOUNT   = 12,
    WINNER_HONOR_AMOUNT     = 500
};

/**
 * @struct BattlegroundICScore
 * @brief 冬拥湖之岛玩家分数结构体
 *
 * 继承自BattlegroundScore，添加冬拥湖之岛特有的分数统计
 */
struct BattlegroundICScore final : public BattlegroundScore
{
    friend class BattlegroundIC;

    protected:
        /**
         * @brief 构造函数
         * @param playerGuid 玩家GUID
         */
        BattlegroundICScore(ObjectGuid playerGuid) : BattlegroundScore(playerGuid), BasesAssaulted(0), BasesDefended(0) { }

        /**
         * @brief 更新分数
         * @param type 分数类型
         * @param value 分数值
         */
        void UpdateScore(uint32 type, uint32 value) override
        {
            switch (type)
            {
                case SCORE_BASES_ASSAULTED:  ///< 进攻基地次数
                    BasesAssaulted += value;
                    break;
                case SCORE_BASES_DEFENDED:   ///< 防守基地次数
                    BasesDefended += value;
                    break;
                default:
                    BattlegroundScore::UpdateScore(type, value);
                    break;
            }
        }

        /**
         * @brief 构建目标数据块
         * @param data 数据包引用
         */
        void BuildObjectivesBlock(WorldPacket& data) final override;

        /**
         * @brief 获取属性1（进攻基地次数）
         * @return 进攻基地次数
         */
        uint32 GetAttr1() const final override { return BasesAssaulted; }

        /**
         * @brief 获取属性2（防守基地次数）
         * @return 防守基地次数
         */
        uint32 GetAttr2() const final override { return BasesDefended; }

        uint32 BasesAssaulted;  ///< 进攻基地次数
        uint32 BasesDefended;   ///< 防守基地次数
};

/**
 * @class BattlegroundIC
 * @brief 冬拥湖之岛战场类
 *
 * 继承自Battleground类，实现冬拥湖之岛战场的具体逻辑，包括：
 * - 码头、机库、车间、采石场、炼油厂等战略要点的占领
 * - 攻城器械的生成和使用
 * - 飞艇的使用
 * - 大门的破坏
 * - 将军的击杀
 * - 增援点数的管理
 *
 * 冬拥湖之岛特点：
 * - 40v40大型战场
 * - 目标：摧毁敌方将军或消耗敌方增援点数
 * - 多个战略要点，每个要点提供不同的战略优势
 * - 码头：投刃车和投石车
 * - 机库：飞艇和空降
 * - 车间：碎石车
 * - 采石场/炼油厂：增援点数加成
 */
class BattlegroundIC : public Battleground
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化战场成员变量
         */
        BattlegroundIC();

        /**
         * @brief 析构函数
         */
        ~BattlegroundIC();

        /* inherited from BattlegroundClass - 继承自战场基类的方法 */

        /**
         * @brief 添加玩家到战场
         * @param player 玩家指针
         */
        void AddPlayer(Player* player) override;

        /**
         * @brief 开始事件 - 关闭大门
         */
        void StartingEventCloseDoors() override;

        /**
         * @brief 开始事件 - 打开大门
         */
        void StartingEventOpenDoors() override;

        /**
         * @brief 战场更新实现
         * @param diff 距离上次更新的时间间隔（毫秒）
         */
        void PostUpdateImpl(uint32 diff) override;

        /**
         * @brief 移除玩家
         * @param player 玩家指针
         * @param guid 玩家GUID
         * @param team 队伍ID
         */
        void RemovePlayer(Player* player, ObjectGuid guid, uint32 team) override;

        /**
         * @brief 处理区域触发器
         * @param player 玩家指针
         * @param trigger 触发器ID
         */
        void HandleAreaTrigger(Player* player, uint32 trigger) override;

        /**
         * @brief 设置战场
         * @return 成功返回true，失败返回false
         */
        bool SetupBattleground() override;

        /**
         * @brief 生成领袖
         * @param teamid 团队ID
         */
        void SpawnLeader(uint32 teamid);

        /**
         * @brief 处理击杀单位
         * @param unit 被击杀的单位
         * @param killer 击杀者
         */
        void HandleKillUnit(Creature* unit, Player* killer) override;

        /**
         * @brief 处理击杀玩家
         * @param player 被击杀的玩家
         * @param killer 击杀者
         */
        void HandleKillPlayer(Player* player, Player* killer) override;

        /**
         * @brief 玩家点击旗帜事件
         * @param source 玩家指针
         * @param target_obj 旗帜游戏对象
         */
        void EventPlayerClickedOnFlag(Player* source, GameObject* /*target_obj*/) override;

        /**
         * @brief 摧毁大门
         * @param player 玩家指针
         * @param go 游戏对象（大门）
         */
        void DestroyGate(Player* player, GameObject* go) override;

        /**
         * @brief 获取最近的墓地
         * @param player 玩家指针
         * @return 墓地信息
         */
        WorldSafeLocsEntry const* GetClosestGraveyard(Player* player) override;

        /* Scorekeeping - 分数记录 */

        /**
         * @brief 填充初始世界状态
         * @param packet 世界状态数据包引用
         */
        void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;

        /**
         * @brief 处理玩家复活
         * @param player 玩家指针
         */
        void HandlePlayerResurrect(Player* player) override;

        /**
         * @brief 获取节点状态
         * @param nodeType 节点类型
         * @return 节点状态
         */
        uint32 GetNodeState(uint8 nodeType) const { return (uint8)nodePoint[nodeType].nodeState; }

        /**
         * @brief 检查是否所有节点被同一阵营控制
         * @param team 团队ID
         * @return 是返回true
         */
        bool IsAllNodesControlledByTeam(uint32 team) const override;

        /**
         * @brief 检查法术是否被允许
         * @param spellId 法术ID
         * @param player 玩家指针
         * @return 允许返回true
         */
        bool IsSpellAllowed(uint32 spellId, Player const* player) const override;

    private:
        uint32 closeFortressDoorsTimer;       ///< 关闭堡垒大门计时器
        bool doorsClosed;                     ///< 大门是否已关闭
        uint32 docksTimer;                    ///< 码头计时器
        uint32 resourceTimer;                 ///< 资源计时器
        uint32 siegeEngineWorkshopTimer;      ///< 攻城机车间计时器
        uint16 factionReinforcements[2];      ///< 阵营增援点数[联盟, 部落]
        BG_IC_GateState GateStatus[6];        ///< 大门状态[6扇门]
        ICNodePoint nodePoint[7];             ///< 节点信息[7个节点]

        Transport* gunshipAlliance;           ///< 联盟飞艇
        Transport* gunshipHorde;              ///< 部落飞艇

        /**
         * @brief 获取下一个旗帜
         * @param node 节点指针
         * @param team 团队ID
         * @param returnDefinitve 是否返回确定状态
         * @return 旗帜ID
         */
        uint32 GetNextBanner(ICNodePoint* node, uint32 team, bool returnDefinitve);

        /**
         * @brief 根据游戏对象ID获取大门ID
         * @param id 游戏对象ID
         * @return 大门ID
         */
        uint32 GetGateIDFromEntry(uint32 id)
        {
            uint32 i = 0;
            switch (id)
            {
                case GO_HORDE_GATE_1: i = BG_IC_H_FRONT ;break;
                case GO_HORDE_GATE_2: i = BG_IC_H_WEST ;break;
                case GO_HORDE_GATE_3: i = BG_IC_H_EAST ;break;
                case GO_ALLIANCE_GATE_3: i = BG_IC_A_FRONT ;break;
                case GO_ALLIANCE_GATE_1: i = BG_IC_A_WEST ;break;
                case GO_ALLIANCE_GATE_2: i = BG_IC_A_EAST ;break;
            }
            return i;
        }

        /**
         * @brief 根据大门游戏对象ID获取世界状态
         * @param id 游戏对象ID
         * @param open 是否打开
         * @return 世界状态ID
         */
        uint32 GetWorldStateFromGateEntry(uint32 id, bool open)
        {
            uint32 uws = 0;

            switch (id)
            {
                case GO_HORDE_GATE_1:
                    uws = (open ? BG_IC_GATE_FRONT_H_WS_OPEN : BG_IC_GATE_FRONT_H_WS_CLOSED);
                    break;
                case GO_HORDE_GATE_2:
                    uws = (open ? BG_IC_GATE_WEST_H_WS_OPEN : BG_IC_GATE_WEST_H_WS_CLOSED);
                    break;
                case GO_HORDE_GATE_3:
                    uws = (open ? BG_IC_GATE_EAST_H_WS_OPEN : BG_IC_GATE_EAST_H_WS_CLOSED);
                    break;
                case GO_ALLIANCE_GATE_3:
                    uws = (open ? BG_IC_GATE_FRONT_A_WS_OPEN : BG_IC_GATE_FRONT_A_WS_CLOSED);
                    break;
                case GO_ALLIANCE_GATE_1:
                    uws = (open ? BG_IC_GATE_WEST_A_WS_OPEN : BG_IC_GATE_WEST_A_WS_CLOSED);
                    break;
                case GO_ALLIANCE_GATE_2:
                    uws = (open ? BG_IC_GATE_EAST_A_WS_OPEN : BG_IC_GATE_EAST_A_WS_CLOSED);
                    break;
            }
            return uws;
        }

        /**
         * @brief 更新节点世界状态
         * @param node 节点指针
         */
        void UpdateNodeWorldState(ICNodePoint* node);

        /**
         * @brief 处理已占领的节点
         * @param node 节点指针
         * @param recapture 是否是重新占领
         */
        void HandleCapturedNodes(ICNodePoint* node, bool recapture);

        /**
         * @brief 处理争夺中的节点
         * @param node 节点指针
         */
        void HandleContestedNodes(ICNodePoint* node);
};

#endif
