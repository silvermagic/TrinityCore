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
 * @file BattlegroundSA.h
 * @brief 远古海滩（Strand of the Ancients）战场模块
 *
 * 本模块实现了远古海滩战场的核心逻辑，包括：
 * - 攻防两轮制战斗机制
 * - 泰坦遗迹争夺战
 * - 动态大门和墓地系统
 * - 攻城器械和防御炮塔管理
 *
 * 远古海滩特点：
 * - 双轮制：每个阵营轮流担任攻方和守方
 * - 时间限制：每轮10分钟，通过大门摧毁速度决定胜负
 * - 战术元素：攻城炸弹、墓地控制、大门破坏
 * - 载具战斗：攻城器械和防御炮塔
 */

#ifndef __BATTLEGROUNDSA_H
#define __BATTLEGROUNDSA_H

#include "Battleground.h"
#include "BattlegroundScore.h"
#include "Object.h"

/// 旗帜数量常量
#define BG_SA_FLAG_AMOUNT           3

/// 攻城器械数量常量
#define BG_SA_DEMOLISHER_AMOUNT     4

/**
 * @brief 远古海滩战场状态枚举
 *
 * 定义战场的各个阶段状态
 */
enum BG_SA_Status
{
    BG_SA_NOT_STARTED = 0,    ///< 未开始状态
    BG_SA_WARMUP,             ///< 热身阶段（准备时间）
    BG_SA_ROUND_ONE,          ///< 第一轮战斗
    BG_SA_SECOND_WARMUP,      ///< 第二轮热身（交换攻防）
    BG_SA_ROUND_TWO,          ///< 第二轮战斗
    BG_SA_BONUS_ROUND         ///< 奖励轮次
};

/**
 * @brief 大门状态枚举
 *
 * 定义大门的损坏程度状态
 */
enum BG_SA_GateState
{
    BG_SA_GATE_OK           = 1,  ///< 大门完好
    BG_SA_GATE_DAMAGED      = 2,  ///< 大门受损
    BG_SA_GATE_DESTROYED    = 3   ///< 大门被摧毁
};

enum BG_SA_EventIds
{
    BG_SA_EVENT_BLUE_GATE_DAMAGED           = 19040,
    BG_SA_EVENT_BLUE_GATE_DESTROYED         = 19045,

    BG_SA_EVENT_GREEN_GATE_DAMAGED          = 19041,
    BG_SA_EVENT_GREEN_GATE_DESTROYED        = 19046,

    BG_SA_EVENT_RED_GATE_DAMAGED            = 19042,
    BG_SA_EVENT_RED_GATE_DESTROYED          = 19047,

    BG_SA_EVENT_PURPLE_GATE_DAMAGED         = 19043,
    BG_SA_EVENT_PURPLE_GATE_DESTROYED       = 19048,

    BG_SA_EVENT_YELLOW_GATE_DAMAGED         = 19044,
    BG_SA_EVENT_YELLOW_GATE_DESTROYED       = 19049,

    BG_SA_EVENT_ANCIENT_GATE_DAMAGED        = 19836,
    BG_SA_EVENT_ANCIENT_GATE_DESTROYED      = 19837,

    BG_SA_EVENT_TITAN_RELIC_ACTIVATED       = 22097
};

enum SASpellIds
{
    SPELL_TELEPORT_DEFENDER                 = 52364,
    SPELL_TELEPORT_ATTACKERS                = 60178,
    SPELL_END_OF_ROUND                      = 52459,
    SPELL_REMOVE_SEAFORIUM                  = 59077,
    SPELL_ALLIANCE_CONTROL_PHASE_SHIFT      = 60027,
    SPELL_HORDE_CONTROL_PHASE_SHIFT         = 60028
};

enum SACreatureIds
{
    NPC_KANRETHAD                                   = 29,
    NPC_INVISIBLE_STALKER                           = 15214,
    NPC_WORLD_TRIGGER                               = 22515,
    NPC_WORLD_TRIGGER_LARGE_AOI_NOT_IMMUNE_PC_NPC   = 23472,

    NPC_ANTI_PERSONNAL_CANNON                       = 27894,
    NPC_DEMOLISHER_SA                               = 28781,
    NPC_RIGGER_SPARKLIGHT                           = 29260,
    NPC_GORGRIL_RIGSPARK                            = 29262
};

enum SAGameObjectIds
{
    GO_GATE_OF_THE_GREEN_EMERALD            = 190722,
    GO_GATE_OF_THE_PURPLE_AMETHYST          = 190723,
    GO_GATE_OF_THE_BLUE_SAPPHIRE            = 190724,
    GO_GATE_OF_THE_RED_SUN                  = 190726,
    GO_GATE_OF_THE_YELLOW_MOON              = 190727,
    GO_CHAMBER_OF_ANCIENT_RELICS            = 192549,
};

enum BG_SA_Timers
{
    BG_SA_BOAT_START    =  60 * IN_MILLISECONDS,
    BG_SA_WARMUPLENGTH  = 120 * IN_MILLISECONDS,
    BG_SA_ROUNDLENGTH   = 600 * IN_MILLISECONDS
};

enum SASounds
{
    SOUND_GRAVEYARD_TAKEN_HORDE     = 8174,
    SOUND_GRAVEYARD_TAKEN_ALLIANCE  = 8212,
    SOUND_DEFEAT_HORDE              = 15905,
    SOUND_VICTORY_HORDE             = 15906,
    SOUND_VICTORY_ALLIANCE          = 15907,
    SOUND_DEFEAT_ALLIANCE           = 15908,
    SOUND_WALL_DESTROYED_ALLIANCE   = 15909,
    SOUND_WALL_DESTROYED_HORDE      = 15910,
    SOUND_WALL_ATTACKED_HORDE       = 15911,
    SOUND_WALL_ATTACKED_ALLIANCE    = 15912
};

enum SATexts
{
    // Kanrethad
    TEXT_ROUND_STARTED              = 1,
    TEXT_ROUND_1_FINISHED           = 2,

    // Rigger Sparklight / Gorgril Rigspark
    TEXT_SPARKLIGHT_RIGSPARK_SPAWN  = 1,

    // World Trigger
    TEXT_BLUE_GATE_UNDER_ATTACK     = 1,
    TEXT_GREEN_GATE_UNDER_ATTACK    = 2,
    TEXT_RED_GATE_UNDER_ATTACK      = 3,
    TEXT_PURPLE_GATE_UNDER_ATTACK   = 4,
    TEXT_YELLOW_GATE_UNDER_ATTACK   = 5,
    TEXT_YELLOW_GATE_DESTROYED      = 6,
    TEXT_PURPLE_GATE_DESTROYED      = 7,
    TEXT_RED_GATE_DESTROYED         = 8,
    TEXT_GREEN_GATE_DESTROYED       = 9,
    TEXT_BLUE_GATE_DESTROYED        = 10,
    TEXT_EAST_GRAVEYARD_CAPTURED_A  = 11,
    TEXT_WEST_GRAVEYARD_CAPTURED_A  = 12,
    TEXT_SOUTH_GRAVEYARD_CAPTURED_A = 13,
    TEXT_EAST_GRAVEYARD_CAPTURED_H  = 14,
    TEXT_WEST_GRAVEYARD_CAPTURED_H  = 15,
    TEXT_SOUTH_GRAVEYARD_CAPTURED_H = 16,
    TEXT_ANCIENT_GATE_UNDER_ATTACK  = 17,
    TEXT_ANCIENT_GATE_DESTROYED     = 18
};

enum SAWorldStates
{
    BG_SA_TIMER_MINS                = 3559,
    BG_SA_TIMER_SEC_TENS            = 3560,
    BG_SA_TIMER_SEC_DECS            = 3561,
    BG_SA_ALLY_ATTACKS              = 4352,
    BG_SA_HORDE_ATTACKS             = 4353,
    BG_SA_PURPLE_GATEWS             = 3614,
    BG_SA_RED_GATEWS                = 3617,
    BG_SA_BLUE_GATEWS               = 3620,
    BG_SA_GREEN_GATEWS              = 3623,
    BG_SA_YELLOW_GATEWS             = 3638,
    BG_SA_ANCIENT_GATEWS            = 3849,
    BG_SA_LEFT_GY_ALLIANCE          = 3635,
    BG_SA_RIGHT_GY_ALLIANCE         = 3636,
    BG_SA_CENTER_GY_ALLIANCE        = 3637,
    BG_SA_RIGHT_ATT_TOKEN_ALL       = 3627,
    BG_SA_LEFT_ATT_TOKEN_ALL        = 3626,
    BG_SA_LEFT_ATT_TOKEN_HRD        = 3629,
    BG_SA_RIGHT_ATT_TOKEN_HRD       = 3628,
    BG_SA_HORDE_DEFENCE_TOKEN       = 3631,
    BG_SA_ALLIANCE_DEFENCE_TOKEN    = 3630,
    BG_SA_RIGHT_GY_HORDE            = 3632,
    BG_SA_LEFT_GY_HORDE             = 3633,
    BG_SA_CENTER_GY_HORDE           = 3634,
    BG_SA_BONUS_TIMER               = 3571,
    BG_SA_ENABLE_TIMER              = 3564
};

enum BG_SA_NPCs
{
    BG_SA_GUN_1 = 0,
    BG_SA_GUN_2,
    BG_SA_GUN_3,
    BG_SA_GUN_4,
    BG_SA_GUN_5,
    BG_SA_GUN_6,
    BG_SA_GUN_7,
    BG_SA_GUN_8,
    BG_SA_GUN_9,
    BG_SA_GUN_10,
    BG_SA_DEMOLISHER_1,
    BG_SA_DEMOLISHER_2,
    BG_SA_DEMOLISHER_3,
    BG_SA_DEMOLISHER_4,
    BG_SA_DEMOLISHER_5,
    BG_SA_DEMOLISHER_6,
    BG_SA_DEMOLISHER_7,
    BG_SA_DEMOLISHER_8,
    BG_SA_NPC_SPARKLIGHT,
    BG_SA_NPC_RIGSPARK,
    BG_SA_NPC_KANRETHAD,
    BG_SA_MAXNPC
};

enum BG_SA_Boat
{
    BG_SA_BOAT_ONE_A    = 193182,
    BG_SA_BOAT_TWO_H    = 193183,
    BG_SA_BOAT_ONE_H    = 193184,
    BG_SA_BOAT_TWO_A    = 193185
};

uint32 const BG_SA_NpcEntries[BG_SA_MAXNPC] =
{
    NPC_ANTI_PERSONNAL_CANNON,
    NPC_ANTI_PERSONNAL_CANNON,
    NPC_ANTI_PERSONNAL_CANNON,
    NPC_ANTI_PERSONNAL_CANNON,
    NPC_ANTI_PERSONNAL_CANNON,
    NPC_ANTI_PERSONNAL_CANNON,
    NPC_ANTI_PERSONNAL_CANNON,
    NPC_ANTI_PERSONNAL_CANNON,
    NPC_ANTI_PERSONNAL_CANNON,
    NPC_ANTI_PERSONNAL_CANNON,
    // 4 beach demolishers
    NPC_DEMOLISHER_SA,
    NPC_DEMOLISHER_SA,
    NPC_DEMOLISHER_SA,
    NPC_DEMOLISHER_SA,
    // 4 factory demolishers
    NPC_DEMOLISHER_SA,
    NPC_DEMOLISHER_SA,
    NPC_DEMOLISHER_SA,
    NPC_DEMOLISHER_SA,
    // Used Demolisher Salesman
    NPC_RIGGER_SPARKLIGHT,
    NPC_GORGRIL_RIGSPARK,
    // Kanrethad
    NPC_KANRETHAD
};

Position const BG_SA_NpcSpawnlocs[BG_SA_MAXNPC] =
{
    // Cannons
    { 1436.429f, 110.05f, 41.407f, 5.4f },
    { 1404.9023f, 84.758f, 41.183f, 5.46f },
    { 1068.693f, -86.951f, 93.81f, 0.02f },
    { 1068.83f, -127.56f, 96.45f, 0.0912f },
    { 1422.115f, -196.433f, 42.1825f, 1.0222f },
    { 1454.887f, -220.454f, 41.956f, 0.9627f },
    { 1232.345f, -187.517f, 66.945f, 0.45f },
    { 1249.634f, -224.189f, 66.72f, 0.635f },
    { 1236.213f, 92.287f, 64.965f, 5.751f },
    { 1215.11f, 57.772f, 64.739f, 5.78f },
    // Demolishers
    { 1611.597656f, -117.270073f, 8.719355f, 2.513274f},
    { 1575.562500f, -158.421875f, 5.024450f, 2.129302f},
    { 1618.047729f, 61.424641f, 7.248210f, 3.979351f},
    { 1575.103149f, 98.873344f, 2.830360f, 3.752458f},
    // Demolishers 2
    { 1371.055786f, -317.071136f, 35.007359f, 1.947460f},
    { 1424.034912f, -260.195190f, 31.084425f, 2.820013f},
    { 1353.139893f, 223.745438f, 35.265411f, 4.343684f},
    { 1404.809570f, 197.027237f, 32.046032f, 3.605401f},
    // Npcs
    { 1348.644165f, -298.786469f, 31.080130f, 1.710423f},
    { 1358.191040f, 195.527786f, 31.018187f, 4.171337f},
    { 841.921f, -134.194f, 196.838f, 6.23082f }
};

enum BG_SA_Objects
{
    BG_SA_GREEN_GATE = 0,
    BG_SA_YELLOW_GATE,
    BG_SA_BLUE_GATE,
    BG_SA_RED_GATE,
    BG_SA_PURPLE_GATE,
    BG_SA_ANCIENT_GATE,
    BG_SA_TITAN_RELIC,
    BG_SA_PORTAL_DEFFENDER_BLUE,
    BG_SA_PORTAL_DEFFENDER_GREEN,
    BG_SA_PORTAL_DEFFENDER_YELLOW,
    BG_SA_PORTAL_DEFFENDER_PURPLE,
    BG_SA_PORTAL_DEFFENDER_RED,
    BG_SA_BOAT_ONE,
    BG_SA_BOAT_TWO,
    BG_SA_SIGIL_1,
    BG_SA_SIGIL_2,
    BG_SA_SIGIL_3,
    BG_SA_SIGIL_4,
    BG_SA_SIGIL_5,
    BG_SA_CENTRAL_FLAGPOLE,
    BG_SA_RIGHT_FLAGPOLE,
    BG_SA_LEFT_FLAGPOLE,
    BG_SA_CENTRAL_FLAG,
    BG_SA_RIGHT_FLAG,
    BG_SA_LEFT_FLAG,
    BG_SA_BOMB,
    BG_SA_MAXOBJ = BG_SA_BOMB+68
};

Position const BG_SA_ObjSpawnlocs[BG_SA_MAXOBJ] =
{
    { 1411.57f, 108.163f, 28.692f, 5.441f },
    { 1055.452f, -108.1f, 82.134f, 0.034f },
    { 1431.3413f, -219.437f, 30.893f, 0.9736f },
    { 1227.667f, -212.555f, 55.372f, 0.5023f },
    { 1214.681f, 81.21f, 53.413f, 5.745f },
    { 878.555f, -108.2f, 117.845f, 0.0f },
    { 836.5f, -108.8f, 120.219f, 0.0f },
    // Portal
    {1468.380005f, -225.798996f, 30.896200f, 0.0f}, //blue
    {1394.270020f, 72.551399f, 31.054300f, 0.0f}, //green
    {1065.260010f, -89.79501f, 81.073402f, 0.0f}, //yellow
    {1216.069946f, 47.904301f, 54.278198f, 0.0f}, //purple
    {1255.569946f, -233.548996f, 56.43699f, 0.0f}, //red
    // Ships
    { 2679.696777f, -826.891235f, 3.712860f, 5.78367f}, //rot2 1 rot3 0.0002f
    { 2574.003662f, 981.261475f, 2.603424f, 0.807696f},
    // Sigils
    { 1414.054f, 106.72f, 41.442f, 5.441f },
    { 1060.63f, -107.8f, 94.7f, 0.034f },
    { 1433.383f, -216.4f, 43.642f, 0.9736f },
    { 1230.75f, -210.724f, 67.611f, 0.5023f },
    { 1217.8f, 79.532f, 66.58f, 5.745f },
    // Flagpoles
    { 1215.114258f, -65.711861f, 70.084267f, -3.124123f},
    {1338.863892f, -153.336533f, 30.895121f, -2.530723f},
    {1309.124268f, 9.410645f, 30.893402f, -1.623156f},
    // Flags
    { 1215.108032f, -65.715767f, 70.084267f, -3.124123f},
    { 1338.859253f, -153.327316f, 30.895077f, -2.530723f},
    { 1309.192017f, 9.416233f, 30.893402f, 1.518436f},
    // Bombs
    {1333.45f, 211.354f, 31.0538f, 5.03666f},
    {1334.29f, 209.582f, 31.0532f, 1.28088f},
    {1332.72f, 210.049f, 31.0532f, 1.28088f},
    {1334.28f, 210.78f, 31.0538f, 3.85856f},
    {1332.64f, 211.39f, 31.0532f, 1.29266f},
    {1371.41f, 194.028f, 31.5107f, 0.753095f},
    {1372.39f, 194.951f, 31.4679f, 0.753095f},
    {1371.58f, 196.942f, 30.9349f, 1.01777f},
    {1370.43f, 196.614f, 30.9349f, 0.957299f},
    {1369.46f, 196.877f, 30.9351f, 2.45348f},
    {1370.35f, 197.361f, 30.9349f, 1.08689f},
    {1369.47f, 197.941f, 30.9349f, 0.984787f},
    {1592.49f, 47.5969f, 7.52271f, 4.63218f},
    {1593.91f, 47.8036f, 7.65856f, 4.63218f},
    {1593.13f, 46.8106f, 7.54073f, 4.63218f},
    {1589.22f, 36.3616f, 7.45975f, 4.64396f},
    {1588.24f, 35.5842f, 7.55613f, 4.79564f},
    {1588.14f, 36.7611f, 7.49675f, 4.79564f},
    {1595.74f, 35.5278f, 7.46602f, 4.90246f},
    {1596, 36.6475f, 7.47991f, 4.90246f},
    {1597.03f, 36.2356f, 7.48631f, 4.90246f},
    {1597.93f, 37.1214f, 7.51725f, 4.90246f},
    {1598.16f, 35.888f, 7.50018f, 4.90246f},
    {1579.6f, -98.0917f, 8.48478f, 1.37996f},
    {1581.2f, -98.401f, 8.47483f, 1.37996f},
    {1580.38f, -98.9556f, 8.4772f, 1.38781f},
    {1585.68f, -104.966f, 8.88551f, 0.493246f},
    {1586.15f, -106.033f, 9.10616f, 0.493246f},
    {1584.88f, -105.394f, 8.82985f, 0.493246f},
    {1581.87f, -100.899f, 8.46164f, 0.929142f},
    {1581.48f, -99.4657f, 8.46926f, 0.929142f},
    {1583.2f, -91.2291f, 8.49227f, 1.40038f},
    {1581.94f, -91.0119f, 8.49977f, 1.40038f},
    {1582.33f, -91.951f, 8.49353f, 1.1844f},
    {1342.06f, -304.049f, 30.9532f, 5.59507f},
    {1340.96f, -304.536f, 30.9458f, 1.28323f},
    {1341.22f, -303.316f, 30.9413f, 0.486051f},
    {1342.22f, -302.939f, 30.986f, 4.87643f},
    {1382.16f, -287.466f, 32.3063f, 4.80968f},
    {1381, -287.58f, 32.2805f, 4.80968f},
    {1381.55f, -286.536f, 32.3929f, 2.84225f},
    {1382.75f, -286.354f, 32.4099f, 1.00442f},
    {1379.92f, -287.34f, 32.2872f, 3.81615f},
    {1100.52f, -2.41391f, 70.2984f, 0.131054f},
    {1099.35f, -2.13851f, 70.3375f, 4.4586f},
    {1099.59f, -1.00329f, 70.238f, 2.49903f},
    {1097.79f, 0.571316f, 70.159f, 4.00307f},
    {1098.74f, -7.23252f, 70.7972f, 4.1523f},
    {1098.46f, -5.91443f, 70.6715f, 4.1523f},
    {1097.53f, -7.39704f, 70.7959f, 4.1523f},
    {1097.32f, -6.64233f, 70.7424f, 4.1523f},
    {1096.45f, -5.96664f, 70.7242f, 4.1523f},
    {971.725f, 0.496763f, 86.8467f, 2.09233f},
    {973.589f, 0.119518f, 86.7985f, 3.17225f},
    {972.524f, 1.25333f, 86.8351f, 5.28497f},
    {971.993f, 2.05668f, 86.8584f, 5.28497f},
    {973.635f, 2.11805f, 86.8197f, 2.36722f},
    {974.791f, 1.74679f, 86.7942f, 1.5936f},
    {974.771f, 3.0445f, 86.8125f, 0.647199f},
    {979.554f, 3.6037f, 86.7923f, 1.69178f},
    {979.758f, 2.57519f, 86.7748f, 1.76639f},
    {980.769f, 3.48904f, 86.7939f, 1.76639f},
    {979.122f, 2.87109f, 86.7794f, 1.76639f},
    {986.167f, 4.85363f, 86.8439f, 1.5779f},
    {986.176f, 3.50367f, 86.8217f, 1.5779f},
    {987.33f, 4.67389f, 86.8486f, 1.5779f},
    {985.23f, 4.65898f, 86.8368f, 1.5779f},
    {984.556f, 3.54097f, 86.8137f, 1.5779f},
};

/* Ships:
 * 193182 - ally
 * 193183 - horde
 * 193184 - horde
 * 193185 - ally
 * Banners:
 * 191308 - left one,
 * 191306 - right one,
 * 191310 - central,
 * Ally ones, substract 1
 * to get horde ones.
 */

uint32 const BG_SA_ObjEntries[BG_SA_MAXOBJ + BG_SA_FLAG_AMOUNT] =
{
    190722,
    190727,
    190724,
    190726,
    190723,
    192549,
    192834,
    192819,
    192819,
    192819,
    192819,
    192819,
    0, // Boat
    0, // Boat
    192687,
    192685,
    192689,
    192690,
    192691,
    191311,
    191311,
    191311,
    191310,
    191306,
    191308,
    190753
};

uint32 const BG_SA_Factions[2] =
{
    1732,
    1735,
};

enum BG_SA_Graveyards
{
    BG_SA_BEACH_GY = 0,
    BG_SA_DEFENDER_LAST_GY,
    BG_SA_RIGHT_CAPTURABLE_GY,
    BG_SA_LEFT_CAPTURABLE_GY,
    BG_SA_CENTRAL_CAPTURABLE_GY,
    BG_SA_MAX_GY
};

const uint32 BG_SA_GYEntries[BG_SA_MAX_GY] =
{
    1350,
    1349,
    1347,
    1346,
    1348,
};

float const BG_SA_GYOrientation[BG_SA_MAX_GY] =
{
    6.202f,
    1.926f, // right capturable GY
    3.917f, // left capturable GY
    3.104f, // center, capturable
    6.148f, // defender last GY
};

enum BG_SA_BroadcastTexts
{
    BG_SA_TEXT_ALLIANCE_CAPTURED_TITAN_PORTAL   = 28944,
    BG_SA_TEXT_HORDE_CAPTURED_TITAN_PORTAL      = 28945,

    BG_SA_TEXT_ROUND_TWO_START_ONE_MINUTE       = 29448,
    BG_SA_TEXT_ROUND_TWO_START_HALF_MINUTE      = 29449
};

/**
 * @struct GateInfo
 * @brief 大门信息结构
 *
 * 存储单个大门的基本信息，包括：
 * - 大门ID和游戏对象ID
 * - 世界状态ID
 * - 损坏和摧毁时的文本提示
 */
struct GateInfo
{
    uint8 GateId;          ///< 大门ID（在BG_SA_Objects枚举中的索引）
    uint32 GameObjectId;   ///< 游戏对象模板ID
    uint32 WorldState;     ///< 世界状态ID
    uint8 DamagedText;     ///< 大门受损时的广播文本ID
    uint8 DestroyedText;   ///< 大门被摧毁时的广播文本ID
};

/// 大门总数
#define MAX_GATES 6

/// 所有大门的配置信息数组
GateInfo const Gates[MAX_GATES] =
{
    { BG_SA_GREEN_GATE,   GO_GATE_OF_THE_GREEN_EMERALD,   BG_SA_GREEN_GATEWS,   TEXT_GREEN_GATE_UNDER_ATTACK,   TEXT_GREEN_GATE_DESTROYED   },
    { BG_SA_YELLOW_GATE,  GO_GATE_OF_THE_YELLOW_MOON,     BG_SA_YELLOW_GATEWS,  TEXT_YELLOW_GATE_UNDER_ATTACK,  TEXT_YELLOW_GATE_DESTROYED  },
    { BG_SA_BLUE_GATE,    GO_GATE_OF_THE_BLUE_SAPPHIRE,   BG_SA_BLUE_GATEWS,    TEXT_BLUE_GATE_UNDER_ATTACK,    TEXT_BLUE_GATE_DESTROYED    },
    { BG_SA_RED_GATE,     GO_GATE_OF_THE_RED_SUN,         BG_SA_RED_GATEWS,     TEXT_RED_GATE_UNDER_ATTACK,     TEXT_RED_GATE_DESTROYED     },
    { BG_SA_PURPLE_GATE,  GO_GATE_OF_THE_PURPLE_AMETHYST, BG_SA_PURPLE_GATEWS,  TEXT_PURPLE_GATE_UNDER_ATTACK,  TEXT_PURPLE_GATE_DESTROYED  },
    { BG_SA_ANCIENT_GATE, GO_CHAMBER_OF_ANCIENT_RELICS,   BG_SA_ANCIENT_GATEWS, TEXT_ANCIENT_GATE_UNDER_ATTACK, TEXT_ANCIENT_GATE_DESTROYED }
};

/**
 * @struct BG_SA_RoundScore
 * @brief 回合得分结构
 *
 * 记录单个回合的胜负结果和用时
 */
struct BG_SA_RoundScore
{
    TeamId winner;  ///< 回合胜利方
    uint32 time;    ///< 回合用时（毫秒）
};

/**
 * @struct BattlegroundSAScore
 * @brief 远古海滩玩家得分结构
 *
 * 继承自BattlegroundScore，添加远古海滩特有的得分统计：
 * - 摧毁攻城器械数量
 * - 摧毁大门数量
 */
struct BattlegroundSAScore final : public BattlegroundScore
{
    friend class BattlegroundSA;

    protected:
        /**
         * @brief 构造函数
         * @param playerGuid 玩家GUID
         */
        BattlegroundSAScore(ObjectGuid playerGuid) : BattlegroundScore(playerGuid), DemolishersDestroyed(0), GatesDestroyed(0) { }

        /**
         * @brief 更新得分
         * @param type 得分类型
         * @param value 得分值
         *
         * 处理远古海滩特有的得分类型：
         * - SCORE_DESTROYED_DEMOLISHER：摧毁攻城器械
         * - SCORE_DESTROYED_WALL：摧毁大门
         */
        void UpdateScore(uint32 type, uint32 value) override
        {
            switch (type)
            {
                case SCORE_DESTROYED_DEMOLISHER:
                    DemolishersDestroyed += value;
                    break;
                case SCORE_DESTROYED_WALL:
                    GatesDestroyed += value;
                    break;
                default:
                    BattlegroundScore::UpdateScore(type, value);
                    break;
            }
        }

        /**
         * @brief 构建目标数据块
         * @param data 数据包
         *
         * 将玩家得分数据序列化到数据包中
         */
        void BuildObjectivesBlock(WorldPacket& data) final override;

        uint32 GetAttr1() const final override { return DemolishersDestroyed; }
        uint32 GetAttr2() const final override { return GatesDestroyed; }

        uint32 DemolishersDestroyed;  ///< 摧毁的攻城器械数量
        uint32 GatesDestroyed;         ///< 摧毁的大门数量
};

/**
 * @class BattlegroundSA
 * @brief 远古海滩战场管理类
 *
 * 继承自Battleground，实现远古海滩战场的完整逻辑：
 *
 * 核心机制：
 * - 双轮制战斗：双方轮流担任攻方和守方
 * - 大门系统：6道大门，从海滩到泰坦遗迹
 * - 墓地控制：3个可占领墓地，提供复活点和攻城器械
 * - 泰坦遗迹：攻方目标，激活即获胜
 *
 * 战斗流程：
 * 1. 热身阶段（2分钟）：玩家准备，船只出发
 * 2. 第一轮（10分钟）：随机一方进攻
 * 3. 热身阶段（1分钟）：交换攻防
 * 4. 第二轮（10分钟）：另一方进攻
 * 5. 结算：比较两轮用时，时间短者获胜
 *
 * 特殊机制：
 * - 攻城炸弹：海滩和墓地提供的炸弹用于炸门
 * - 攻城器械：可驾驶的投石车，对大门造成伤害
 * - 防御炮塔：守方可使用的固定炮台
 */
class BattlegroundSA : public Battleground
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化战场成员变量，设置初始状态
         */
        BattlegroundSA();

        /**
         * @brief 析构函数
         */
        ~BattlegroundSA();

        /**
         * @brief 战场更新实现
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 核心更新逻辑，处理：
         * - 状态机流转（热身、第一轮、第二轮）
         * - 计时器更新和同步
         * - 回合切换和胜负判定
         *
         * 调用时机：每次战场更新循环
         */
        void PostUpdateImpl(uint32 diff) override;

        /* 继承自Battleground的虚函数 */

        /**
         * @brief 添加玩家到战场
         * @param player 要添加的玩家
         *
         * 创建玩家得分记录，发送船只初始化包，传送玩家到入口
         *
         * 调用时机：玩家进入战场时
         */
        void AddPlayer(Player* player) override;

        /**
         * @brief 关门事件（准备阶段）
         *
         * 准备阶段的初始化操作
         *
         * 调用时机：战场准备阶段开始
         */
        void StartingEventCloseDoors() override;

        /**
         * @brief 开门事件（战斗开始）
         *
         * 战斗开始时的操作
         *
         * 调用时机：战场正式开始
         */
        void StartingEventOpenDoors() override;

        /**
         * @brief 设置战场
         * @return 设置成功返回true，否则返回false
         *
         * 创建所有战场游戏对象和生物：
         * - 大门、船只、泰坦遗迹
         * - 攻城器械、防御炮塔
         * - 墓地旗帜、攻城炸弹
         * - 灵魂 healer
         *
         * 调用时机：战场初始化时
         */
        bool SetupBattleground() override;

        /**
         * @brief 重置战场
         *
         * 重置所有状态变量，随机选择攻击方
         *
         * 调用时机：战场重置时
         */
        void Reset() override;

        /**
         * @brief 填充初始世界状态
         * @param packet 世界状态数据包
         *
         * 向客户端发送战场的初始世界状态信息
         *
         * 调用时机：玩家进入战场时
         */
        void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;

        /**
         * @brief 处理击杀单位
         * @param creature 被击杀的生物
         * @param killer 击杀者
         *
         * 处理攻城器械被摧毁时的得分和成就判定
         *
         * 调用时机：生物死亡时
         */
        void HandleKillUnit(Creature* creature, Player* killer) override;

        /**
         * @brief 获取最近的墓地
         * @param player 需要复活的玩家
         * @return 最近的墓地位置信息
         *
         * 根据玩家位置和墓地控制权，返回最近的可用墓地
         *
         * 调用时机：玩家死亡需要选择复活点时
         */
        WorldSafeLocsEntry const* GetClosestGraveyard(Player* player) override;

        /**
         * @brief 处理事件
         * @param obj 触发事件的对象
         * @param eventId 事件ID
         * @param invoker 触发者
         *
         * 处理游戏对象事件：
         * - 大门损坏/摧毁
         * - 泰坦遗迹激活
         *
         * 调用时机：游戏对象触发事件时
         */
        void ProcessEvent(WorldObject* /*obj*/, uint32 /*eventId*/, WorldObject* /*invoker*/ = nullptr) override;

        /**
         * @brief 玩家点击旗帜事件
         * @param source 点击旗帜的玩家
         * @param go 被点击的游戏对象
         *
         * 处理墓地旗帜点击，占领墓地
         *
         * 调用时机：玩家点击墓地旗帜时
         */
        void EventPlayerClickedOnFlag(Player* source, GameObject* go) override;

        /**
         * @brief 泰坦遗迹激活
         * @param clicker 激活遗迹的玩家
         *
         * 处理泰坦遗迹激活，判定回合胜利
         *
         * 调用时机：玩家激活泰坦遗迹时
         */
        void TitanRelicActivated(Player* clicker);

        /**
         * @brief 获取大门信息
         * @param entry 游戏对象模板ID
         * @return 大门信息结构指针，未找到返回nullptr
         *
         * 根据游戏对象ID查找对应的大门信息
         */
        GateInfo const* GetGate(uint32 entry)
        {
            for (uint8 i = 0; i < MAX_GATES; ++i)
                if (Gates[i].GameObjectId == entry)
                    return &Gates[i];
            return nullptr;
        }

        /**
         * @brief 结束战场
         * @param winner 获胜方阵营ID（ALLIANCE/HORDE/0表示平局）
         *
         * 发放荣誉奖励，调用基类结束逻辑
         *
         * 调用时机：战斗结束判定时
         */
        void EndBattleground(uint32 winner) override;

        /**
         * @brief 移除玩家
         * @param player 离开的玩家
         * @param guid 玩家GUID
         * @param team 玩家阵营
         *
         * 处理玩家离开战场
         *
         * 调用时机：玩家离开战场时
         */
        void RemovePlayer(Player* player, ObjectGuid guid, uint32 team) override;

        /**
         * @brief 处理区域触发
         * @param Source 触发区域的玩家
         * @param Trigger 区域触发器ID
         *
         * 处理玩家进入特定区域的事件
         *
         * 调用时机：玩家进入区域触发器范围时
         */
        void HandleAreaTrigger(Player* Source, uint32 Trigger) override;

        /* 成就系统 */

        /**
         * @brief 检查成就条件是否满足
         * @param criteriaId 成就条件ID
         * @param source 检查成就的玩家
         * @param target 目标单位
         * @param miscValue 额外数值
         * @return 满足条件返回true，否则返回false
         *
         * 检查远古海滩特有成就：
         * - BG_CRITERIA_CHECK_NOT_EVEN_A_SCRATCH：无伤胜利
         * - BG_CRITERIA_CHECK_DEFENSE_OF_THE_ANCIENTS：远古守护者
         */
        bool CheckAchievementCriteriaMeet(uint32 criteriaId, Player const* source, Unit const* target = nullptr, uint32 miscValue = 0) override;

        /**
         * @brief 检查法术是否允许
         * @param spellId 法术ID
         * @param player 施法玩家
         * @return 允许返回true，否则返回false
         *
         * 控制相位转换法术的施放条件
         */
        bool IsSpellAllowed(uint32 spellId, Player const* player) const override;

    private:

        /**
         * @brief 重置所有游戏对象
         * @return 重置成功返回true，否则返回false
         *
         * 删除并重新创建所有游戏对象和生物：
         * - 移除旧对象
         * - 根据当前攻击方重新设置阵营
         * - 重置大门、旗帜、炸弹等
         *
         * 调用时机：战场初始化和两轮之间
         */
        bool ResetObjs();

        /**
         * @brief 启动船只移动
         *
         * 打开船只门，发送更新包给所有玩家
         *
         * 调用时机：热身阶段结束后
         */
        void StartShips();

        /**
         * @brief 传送所有玩家
         *
         * 在两轮之间传送玩家到正确的起始位置：
         * - 攻方传送到船只或海滩
         * - 守方传送到防御位置
         * - 复活所有死亡玩家
         * - 施放准备法术
         *
         * 调用时机：两轮切换时
         */
        void TeleportPlayers();

        /**
         * @brief 传送玩家到入口位置
         * @param player 要传送的玩家
         *
         * 根据玩家阵营（攻方/守方）传送到对应入口：
         * - 攻方：船只（如果船未开）或海滩
         * - 守方：防御者房间
         */
        void TeleportToEntrancePosition(Player* player);

        /**
         * @brief 重写炮塔阵营
         *
         * 设置所有防御炮塔和攻城器械的阵营
         * 防止阵营覆盖问题
         *
         * 调用时机：战场初始化和两轮切换时
         */
        void OverrideGunFaction();

        /**
         * @brief 设置攻城器械状态
         * @param start true=初始状态（不可攻击），false=战斗状态（可攻击）
         *
         * 控制海滩攻城器械的可交互性：
         * - 初始状态：不可攻击，等待船只到达
         * - 战斗状态：可攻击，玩家可驾驶
         *
         * 调用时机：战场开始和船只到达时
         */
        void DemolisherStartState(bool start);

        /**
         * @brief 检查是否可以与对象交互
         * @param objectId 对象ID
         * @return 可以交互返回true，否则返回false
         *
         * 检查特定条件：
         * - 泰坦遗迹：黄色和远古大门必须被摧毁
         * - 中央旗帜：红色或紫色大门必须被摧毁
         * - 左右旗帜：绿色或蓝色大门必须被摧毁
         */
        bool CanInteractWithObject(uint32 objectId);

        /**
         * @brief 更新对象交互标志
         * @param objectId 对象ID
         *
         * 根据CanInteractWithObject的结果设置GO_FLAG_NOT_SELECTABLE
         */
        void UpdateObjectInteractionFlags(uint32 objectId);

        /**
         * @brief 更新所有关键对象的交互标志
         *
         * 更新旗帜和泰坦遗迹的可交互状态
         */
        void UpdateObjectInteractionFlags();

        /**
         * @brief 摧毁大门
         * @param player 摧毁大门的玩家
         * @param go 被摧毁的大门对象
         *
         * 处理大门摧毁事件：
         * - 给予荣誉奖励
         * - 更新世界状态
         * - 删除大门前的视觉效果对象
         *
         * 调用时机：大门被摧毁时
         */
        void DestroyGate(Player* player, GameObject* go) override;

        /**
         * @brief 发送时间
         *
         * 更新客户端计时器显示
         */
        void SendTime();

        /**
         * @brief 占领墓地
         * @param i 墓地ID
         * @param Source 占领墓地的玩家
         *
         * 处理墓地占领：
         * - 更新灵魂 healer
         * - 更新旗帜对象
         * - 更新世界状态
         * - 生成攻城器械
         * - 发送广播消息
         *
         * 调用时机：玩家点击墓地旗帜并满足条件时
         */
        void CaptureGraveyard(BG_SA_Graveyards i, Player* Source);

        /**
         * @brief 切换计时器
         *
         * 开启或关闭客户端计时器显示
         */
        void ToggleTimer();

        /**
         * @brief 更新攻城器械刷新
         *
         * 检查死亡的攻城器械，30秒后复活
         *
         * 调用时机：战场更新循环
         */
        void UpdateDemolisherSpawns();

        /**
         * @brief 发送运输工具初始化
         * @param player 目标玩家
         *
         * 向玩家发送船只创建数据包
         *
         * 调用时机：玩家进入战场时
         */
        void SendTransportInit(Player* player);

        /**
         * @brief 发送运输工具移除
         * @param player 目标玩家
         *
         * 向玩家发送船只移除数据包
         *
         * 调用时机：玩家离开或战场重置时
         */
        void SendTransportsRemove(Player* player);

        TeamId Attackers;          ///< 当前攻击方阵营ID

        uint32 TotalTime;          ///< 当前回合总用时（毫秒）
        uint32 EndRoundTimer;      ///< 回合结束计时器（毫秒）
        bool ShipsStarted;         ///< 船只是否已启动

        BG_SA_GateState GateStatus[MAX_GATES];  ///< 各大门状态数组

        BG_SA_Status Status;       ///< 战场当前状态
        TeamId GraveyardStatus[BG_SA_MAX_GY];   ///< 各墓地控制方数组

        BG_SA_RoundScore RoundScores[2];  ///< 两轮得分记录

        bool TimerEnabled;         ///< 计时器是否启用
        uint32 UpdateWaitTimer;    ///< 更新等待计时器
        bool SignaledRoundTwo;     ///< 是否已发送第二轮1分钟警告
        bool SignaledRoundTwoHalfMin;  ///< 是否已发送第二轮30秒警告
        bool InitSecondRound;      ///< 是否已初始化第二轮

        std::map<uint32/*id*/, uint32/*timer*/> DemoliserRespawnList;  ///< 攻城器械复活列表

        // 成就：Defense of the Ancients（远古守护者）
        bool _gateDestroyed;       ///< 是否有大门被摧毁（成就条件）

        // 成就：Not Even a Scratch（毫发无损）
        bool _allVehiclesAlive[PVP_TEAMS_COUNT];  ///< 各阵营是否所有载具存活
};
#endif
