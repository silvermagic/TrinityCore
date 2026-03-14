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
 * @file BattlegroundAV.h
 * @brief 奥特兰克山谷战场头文件
 *
 * 本文件定义了奥特兰克山谷（Alterac Valley）战场的核心数据结构和类。
 * 奥特兰克山谷是一个大型40v40的战场，联盟和部落为了争夺奥特兰克山谷的控制权而战。
 *
 * 主要特点：
 * - 摧毁敌方碉堡/塔楼可以获得优势
 * - 占领墓地可以控制复活点
 * - 击杀敌方将军可以获胜
 * - 矿井资源采集系统
 * - 空袭和骑兵等特殊战术
 *
 * @author TrinityCore Team
 */

#ifndef __BATTLEGROUNDAV_H
#define __BATTLEGROUNDAV_H

#include "Battleground.h"
#include "BattlegroundScore.h"
#include "GameObjectData.h"
#include "Object.h"

/**
 * @name 战场计时器常量
 * @{
 */
#define BG_AV_CAPTIME                    240000  ///< 占领旗帜所需时间：4分钟（4:00）
#define BG_AV_SNOWFALL_FIRSTCAP          300000  ///< 雪落墓地首次占领时间：5分钟（5:00），但也曾出现过4:05的情况
/** @} */

/**
 * @name 分数相关常量
 * @{
 */
#define BG_AV_SCORE_INITIAL_POINTS       600     ///< 初始分数：600分
#define SEND_MSG_NEAR_LOSE               120     ///< 接近失败时发送警告消息的分数阈值：120分
/** @} */

/**
 * @name 击杀首领奖励常量
 * @{
 */
#define BG_AV_KILL_BOSS                 4        ///< 击杀敌方将军获得的荣誉点数
#define BG_AV_REP_BOSS                  350      ///< 击杀敌方将军获得的声望值
/** @} */

/**
 * @name 击杀队长奖励常量
 * @{
 */
#define BG_AV_KILL_CAPTAIN              3        ///< 击杀敌方队长获得的荣誉点数
#define BG_AV_REP_CAPTAIN               125      ///< 击杀敌方队长获得的声望值
#define BG_AV_RES_CAPTAIN               100      ///< 击杀敌方队长获得的资源值
/** @} */

/**
 * @name 摧毁塔楼奖励常量
 * @{
 */
#define BG_AV_KILL_TOWER                3        ///< 摧毁敌方塔楼获得的荣誉点数
#define BG_AV_REP_TOWER                 12       ///< 摧毁敌方塔楼获得的声望值
#define BG_AV_RES_TOWER                 75       ///< 摧毁敌方塔楼获得的资源值
/** @} */

/**
 * @name 其他奖励常量
 * @{
 */
#define BG_AV_GET_COMMANDER            1         ///< 成功返回空军指挥官获得的奖励（用于安全的返回空军指挥官）
/** @} */

/**
 * @name 战斗结束时的奖励常量
 * @{
 */
#define BG_AV_KILL_SURVIVING_TOWER      2        ///< 战斗结束时存活的塔楼奖励荣誉点数
#define BG_AV_REP_SURVIVING_TOWER       12       ///< 战斗结束时存活的塔楼奖励声望值

#define BG_AV_KILL_SURVIVING_CAPTAIN    2        ///< 战斗结束时存活的队长奖励荣誉点数
#define BG_AV_REP_SURVIVING_CAPTAIN     125      ///< 战斗结束时存活的队长奖励声望值
/** @} */

#define BG_AV_EVENT_START_BATTLE           9166 ///< 战斗开始事件ID（成就：奥特兰克闪电战）

/**
 * @enum SharedActions
 * @brief 共享动作枚举
 *
 * 定义了在奥特兰克山谷中使用的共享动作ID
 */
enum SharedActions
{
    ACTION_BUFF_YELL    = -30001  ///< 增益喊话动作
};

/**
 * @enum BG_AV_BroadcastTexts
 * @brief 广播文本ID枚举
 *
 * 定义了奥特兰克山谷战场中使用的各种广播文本ID
 */
enum BG_AV_BroadcastTexts
{
    BG_AV_TEXT_START_ONE_MINUTE     = 10638,  ///< 战斗开始前1分钟提示
    BG_AV_TEXT_START_HALF_MINUTE    = 10639,  ///< 战斗开始前30秒提示
    BG_AV_TEXT_BATTLE_HAS_BEGUN     = 10640,  ///< 战斗已经开始提示

    BG_AV_TEXT_ALLIANCE_NEAR_LOSE   = 23210,  ///< 联盟即将失败警告
    BG_AV_TEXT_HORDE_NEAR_LOSE      = 23211   ///< 部落即将失败警告
};

/**
 * @enum BG_AV_Sounds
 * @brief 战场音效枚举
 *
 * 定义了奥特兰克山谷战场中使用的各种音效ID
 *
 * @todo 确认中立队伍占领矿井时是否有音效
 */
enum BG_AV_Sounds
{
    /*
    8212:
        alliance grave assault - 联盟墓地进攻
        alliance tower assault - 联盟塔楼进攻
        drek "mlanzenabschaum! In meiner Burg?! Toetet sie all" - nicht immer der sound
    8333:
        galv "sterbt fuer euch ist kein platz hier"

    8332:
        bal "Verschwinde, dreckiger Abschaum! Die Allianz wird im Alteractal "
    8174:
        horde tower assault - 部落塔楼进攻
        horde grave assault - 部落墓地进攻
        van "es Sturmlanzenklans, euer General wird angegriffen! Ich fordere Unterst"
    8173:
        ally grave capture/defend - 联盟墓地占领/防守
        tower destroy - 塔楼摧毁
        mine capture - 矿井占领
        ally wins - 联盟获胜
    8192:
        ally tower destroy(only iceblood - found a bug^^) - 联盟塔楼摧毁（仅冰血塔楼 - 发现了一个bug）
        ally tower defend - 联盟塔楼防守
        horde tower defend - 部落塔楼防守
    8213
    horde:
        grave defend/capture - 墓地防守/占领
        tower destroy - 塔楼摧毁
        mine capture - 矿井占领
        horde wins - 部落获胜
    */

    AV_SOUND_NEAR_VICTORY                   = 8456,  ///< 接近胜利音效 @todo 尚未确认

    AV_SOUND_ALLIANCE_ASSAULTS              = 8212,  ///< 联盟进攻音效（塔楼、墓地 + 敌方首领如果有人试图攻击他）
    AV_SOUND_HORDE_ASSAULTS                 = 8174,  ///< 部落进攻音效
    AV_SOUND_ALLIANCE_GOOD                  = 8173,  ///< 联盟好事音效（队伍发生好事时：获胜、占领矿井或墓地、摧毁塔楼、防守墓地）
    AV_SOUND_HORDE_GOOD                     = 8213,  ///< 部落好事音效
    AV_SOUND_BOTH_TOWER_DEFEND              = 8192,  ///< 双方塔楼防守音效

    AV_SOUND_ALLIANCE_CAPTAIN               = 8232,  ///< 联盟队长音效（当有人攻击他们时，以及在开始后5分钟+随机(x)*10秒时调用，可能是增益）
    AV_SOUND_HORDE_CAPTAIN                  = 8333   ///< 部落队长音效
};

/**
 * @enum BG_AV_OTHER_VALUES
 * @brief 其他常量值枚举
 *
 * 定义了奥特兰克山谷战场中使用的其他常量值
 */
enum BG_AV_OTHER_VALUES
{
    AV_STATICCPLACE_MAX        = 123,      ///< 静态生物位置最大值
    AV_NORTH_MINE              = 0,        ///< 北部矿井索引（铁深矿井）
    AV_SOUTH_MINE              = 1,        ///< 南部矿井索引（冷齿矿井）
    AV_MINE_TICK_TIMER         = 45000,    ///< 矿井产出计时器：45秒
    AV_MINE_RECLAIM_TIMER      = 1200000,  ///< 矿井回收计时器：20分钟 @todo 需要获取正确的数值
    AV_NEUTRAL_TEAM            = 0         ///< 中立队伍（雪落墓地的中立拥有者）
};
/**
 * @enum BG_AV_ObjectIds
 * @brief 游戏对象ID枚举
 *
 * 定义了奥特兰克山谷战场中使用的各种游戏对象ID
 * 由于mangos系统略有不同，我们没有为每个节点使用正确的游戏对象ID。
 * 如果想要100%像其他大型服务器，必须为每个节点使用一个对象。
 */
enum BG_AV_ObjectIds
{
    // 因为mangos系统略有不同，我们没有为每个节点使用正确的游戏对象ID
    // 如果想要100%像其他大型服务器，必须为每个节点使用一个对象
    // 雪落墓地：4面旗帜作为装饰 179424（联盟中立）
    // 旗帜 - 从 battleground_AB.h 借用 ;-)

    BG_AV_OBJECTID_BANNER_A             = 178925,  ///< 联盟旗帜（只能被部落使用）
    BG_AV_OBJECTID_BANNER_H             = 178943,  ///< 部落旗帜（只能被联盟使用）
    BG_AV_OBJECTID_BANNER_CONT_A        = 178940,  ///< 联盟争夺中旗帜（只能被部落使用）
    BG_AV_OBJECTID_BANNER_CONT_H        = 179435,  ///< 部落争夺中旗帜（只能被联盟使用）

    BG_AV_OBJECTID_BANNER_A_B           = 178365,  ///< 联盟旗帜B（用于塔楼）
    BG_AV_OBJECTID_BANNER_H_B           = 178364,  ///< 部落旗帜B（用于塔楼）
    BG_AV_OBJECTID_BANNER_CONT_A_B      = 179286,  ///< 联盟争夺中旗帜B（用于塔楼）
    BG_AV_OBJECTID_BANNER_CONT_H_B      = 179287,  ///< 部落争夺中旗帜B（用于塔楼）
    BG_AV_OBJECTID_BANNER_SNOWFALL_N    = 180418,  ///< 雪落墓地中立旗帜

    // 雪落墓地装饰旗帜
    BG_AV_OBJECTID_SNOWFALL_CANDY_A     = 179044,  ///< 雪落墓地联盟装饰旗帜
    BG_AV_OBJECTID_SNOWFALL_CANDY_PA    = 179424,  ///< 雪落墓地联盟前装饰旗帜
    BG_AV_OBJECTID_SNOWFALL_CANDY_H     = 179064,  ///< 雪落墓地部落装饰旗帜
    BG_AV_OBJECTID_SNOWFALL_CANDY_PH    = 179425,  ///< 雪落墓地部落前装饰旗帜

    // 塔楼顶部的大旗帜
    BG_AV_OBJECTID_TOWER_BANNER_A       = 178927,  ///< [PH] 联盟A1塔楼大旗帜
    BG_AV_OBJECTID_TOWER_BANNER_H       = 178955,  ///< [PH] 部落H1塔楼大旗帜
    BG_AV_OBJECTID_TOWER_BANNER_PA      = 179446,  ///< [PH] 联盟H1塔楼前大旗帜
    BG_AV_OBJECTID_TOWER_BANNER_PH      = 179436,  ///< [PH] 部落A1塔楼前大旗帜

    // 光环
    BG_AV_OBJECTID_AURA_A               = 180421,  ///< 联盟光环
    BG_AV_OBJECTID_AURA_H               = 180422,  ///< 部落光环
    BG_AV_OBJECTID_AURA_N               = 180423,  ///< 中立光环
    BG_AV_OBJECTID_AURA_A_S             = 180100,  ///< 联盟小光环
    BG_AV_OBJECTID_AURA_H_S             = 180101,  ///< 部落小光环
    BG_AV_OBJECTID_AURA_N_S             = 180102,  ///< 中立小光环

    BG_AV_OBJECTID_GATE_A               = 180424,  ///< 联盟大门
    BG_AV_OBJECTID_GATE_H               = 180424,  ///< 部落大门

    // 矿井补给品
    BG_AV_OBJECTID_MINE_N               = 178785,  ///< 北部矿井补给品
    BG_AV_OBJECTID_MINE_S               = 178784,  ///< 南部矿井补给品

    BG_AV_OBJECTID_FIRE                 = 179065,  ///< 火焰效果
    BG_AV_OBJECTID_SMOKE                = 179066   ///< 烟雾效果
};

/**
 * @enum BG_AV_Nodes
 * @brief 战场节点枚举
 *
 * 定义了奥特兰克山谷战场中的所有可占领节点
 * 包括墓地和塔楼/碉堡
 */
enum BG_AV_Nodes
{
    // 墓地节点（Graveyards）
    BG_AV_NODES_FIRSTAID_STATION        = 0,   ///< 急救站墓地（联盟初始墓地）
    BG_AV_NODES_STORMPIKE_GRAVE         = 1,   ///< 雷矛墓地
    BG_AV_NODES_STONEHEART_GRAVE        = 2,   ///< 石炉墓地
    BG_AV_NODES_SNOWFALL_GRAVE          = 3,   ///< 雪落墓地（中立墓地）
    BG_AV_NODES_ICEBLOOD_GRAVE          = 4,   ///< 冰血墓地
    BG_AV_NODES_FROSTWOLF_GRAVE         = 5,   ///< 霜狼墓地
    BG_AV_NODES_FROSTWOLF_HUT           = 6,   ///< 霜狼小屋墓地（部落初始墓地）

    // 联盟塔楼/碉堡（Alliance Towers/Bunkers）
    BG_AV_NODES_DUNBALDAR_SOUTH         = 7,   ///< 丹巴达尔南部碉堡
    BG_AV_NODES_DUNBALDAR_NORTH         = 8,   ///< 丹巴达尔北部碉堡
    BG_AV_NODES_ICEWING_BUNKER          = 9,   ///< 冰翼碉堡
    BG_AV_NODES_STONEHEART_BUNKER       = 10,  ///< 石炉碉堡

    // 部落塔楼（Horde Towers）
    BG_AV_NODES_ICEBLOOD_TOWER          = 11,  ///< 冰血哨塔
    BG_AV_NODES_TOWER_POINT             = 12,  ///< 哨塔点
    BG_AV_NODES_FROSTWOLF_ETOWER        = 13,  ///< 霜狼东塔
    BG_AV_NODES_FROSTWOLF_WTOWER        = 14,  ///< 霜狼西塔

    BG_AV_NODES_MAX                     = 15   ///< 节点总数
};

/**
 * @enum BG_AV_ObjectTypes
 * @brief 游戏对象类型枚举
 *
 * 定义了奥特兰克山谷战场中使用的所有游戏对象类型的索引
 * 这些索引用于在战场中管理和引用特定的游戏对象
 */
enum BG_AV_ObjectTypes
{
    // 联盟控制的墓地旗帜（Alliance Controlled Graveyard Flags）
    BG_AV_OBJECT_FLAG_A_FIRSTAID_STATION    = 0,   ///< 急救站联盟旗帜
    BG_AV_OBJECT_FLAG_A_STORMPIKE_GRAVE     = 1,   ///< 雷矛墓地联盟旗帜
    BG_AV_OBJECT_FLAG_A_STONEHEART_GRAVE    = 2,   ///< 石炉墓地联盟旗帜
    BG_AV_OBJECT_FLAG_A_SNOWFALL_GRAVE      = 3,   ///< 雪落墓地联盟旗帜
    BG_AV_OBJECT_FLAG_A_ICEBLOOD_GRAVE      = 4,   ///< 冰血墓地联盟旗帜
    BG_AV_OBJECT_FLAG_A_FROSTWOLF_GRAVE     = 5,   ///< 霜狼墓地联盟旗帜
    BG_AV_OBJECT_FLAG_A_FROSTWOLF_HUT       = 6,   ///< 霜狼小屋联盟旗帜
    // 联盟控制的塔楼旗帜（Alliance Controlled Tower Flags）
    BG_AV_OBJECT_FLAG_A_DUNBALDAR_SOUTH     = 7,   ///< 丹巴达尔南部碉堡联盟旗帜
    BG_AV_OBJECT_FLAG_A_DUNBALDAR_NORTH     = 8,   ///< 丹巴达尔北部碉堡联盟旗帜
    BG_AV_OBJECT_FLAG_A_ICEWING_BUNKER      = 9,   ///< 冰翼碉堡联盟旗帜
    BG_AV_OBJECT_FLAG_A_STONEHEART_BUNKER   = 10,  ///< 石炉碉堡联盟旗帜

    // 联盟争夺中的旗帜（Alliance Contested Flags）- 用于墓地和塔楼
    BG_AV_OBJECT_FLAG_C_A_FIRSTAID_STATION    = 11,  ///< 急救站联盟争夺旗帜
    BG_AV_OBJECT_FLAG_C_A_STORMPIKE_GRAVE     = 12,  ///< 雷矛墓地联盟争夺旗帜
    BG_AV_OBJECT_FLAG_C_A_STONEHEART_GRAVE    = 13,  ///< 石炉墓地联盟争夺旗帜
    BG_AV_OBJECT_FLAG_C_A_SNOWFALL_GRAVE      = 14,  ///< 雪落墓地联盟争夺旗帜
    BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_GRAVE      = 15,  ///< 冰血墓地联盟争夺旗帜
    BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_GRAVE     = 16,  ///< 霜狼墓地联盟争夺旗帜
    BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_HUT       = 17,  ///< 霜狼小屋联盟争夺旗帜
    BG_AV_OBJECT_FLAG_C_A_ICEBLOOD_TOWER      = 18,  ///< 冰血哨塔联盟争夺旗帜
    BG_AV_OBJECT_FLAG_C_A_TOWER_POINT         = 19,  ///< 哨塔点联盟争夺旗帜
    BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_ETOWER    = 20,  ///< 霜狼东塔联盟争夺旗帜
    BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_WTOWER    = 21,  ///< 霜狼西塔联盟争夺旗帜

    // 部落争夺中的旗帜（Horde Contested Flags）- 用于墓地和塔楼
    BG_AV_OBJECT_FLAG_C_H_FIRSTAID_STATION    = 22,  ///< 急救站部落争夺旗帜
    BG_AV_OBJECT_FLAG_C_H_STORMPIKE_GRAVE     = 23,  ///< 雷矛墓地部落争夺旗帜
    BG_AV_OBJECT_FLAG_C_H_STONEHEART_GRAVE    = 24,  ///< 石炉墓地部落争夺旗帜
    BG_AV_OBJECT_FLAG_C_H_SNOWFALL_GRAVE      = 25,  ///< 雪落墓地部落争夺旗帜
    BG_AV_OBJECT_FLAG_C_H_ICEBLOOD_GRAVE      = 26,  ///< 冰血墓地部落争夺旗帜
    BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_GRAVE     = 27,  ///< 霜狼墓地部落争夺旗帜
    BG_AV_OBJECT_FLAG_C_H_FROSTWOLF_HUT       = 28,  ///< 霜狼小屋部落争夺旗帜
    BG_AV_OBJECT_FLAG_C_H_DUNBALDAR_SOUTH     = 29,  ///< 丹巴达尔南部碉堡部落争夺旗帜
    BG_AV_OBJECT_FLAG_C_H_DUNBALDAR_NORTH     = 30,  ///< 丹巴达尔北部碉堡部落争夺旗帜
    BG_AV_OBJECT_FLAG_C_H_ICEWING_BUNKER      = 31,  ///< 冰翼碉堡部落争夺旗帜
    BG_AV_OBJECT_FLAG_C_H_STONEHEART_BUNKER   = 32,  ///< 石炉碉堡部落争夺旗帜

    // 部落控制的旗帜（Horde Controlled Flags）- 用于墓地和塔楼
    BG_AV_OBJECT_FLAG_H_FIRSTAID_STATION    = 33,  ///< 急救站部落旗帜
    BG_AV_OBJECT_FLAG_H_STORMPIKE_GRAVE     = 34,  ///< 雷矛墓地部落旗帜
    BG_AV_OBJECT_FLAG_H_STONEHEART_GRAVE    = 35,  ///< 石炉墓地部落旗帜
    BG_AV_OBJECT_FLAG_H_SNOWFALL_GRAVE      = 36,  ///< 雪落墓地部落旗帜
    BG_AV_OBJECT_FLAG_H_ICEBLOOD_GRAVE      = 37,  ///< 冰血墓地部落旗帜
    BG_AV_OBJECT_FLAG_H_FROSTWOLF_GRAVE     = 38,  ///< 霜狼墓地部落旗帜
    BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT       = 39,  ///< 霜狼小屋部落旗帜
    BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER      = 40,  ///< 冰血哨塔部落旗帜
    BG_AV_OBJECT_FLAG_H_TOWER_POINT         = 41,  ///< 哨塔点部落旗帜
    BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER    = 42,  ///< 霜狼东塔部落旗帜
    BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER    = 43,  ///< 霜狼西塔部落旗帜

    // 中立旗帜（Neutral Flags）
    BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE      = 44,  ///< 雪落墓地中立旗帜

    // 大门（Doors）
    BG_AV_OBJECT_DOOR_H                     = 45,  ///< 部落大门
    BG_AV_OBJECT_DOOR_A                     = 46,  ///< 联盟大门

    // 墓地光环（Graveyard Auras）- 每个墓地3个光环（中立、联盟、部落）
    BG_AV_OBJECT_AURA_N_FIRSTAID_STATION    = 47,  ///< 急救站中立光环
    BG_AV_OBJECT_AURA_A_FIRSTAID_STATION    = 48,  ///< 急救站联盟光环
    BG_AV_OBJECT_AURA_H_FIRSTAID_STATION    = 49,  ///< 急救站部落光环
    BG_AV_OBJECT_AURA_N_STORMPIKE_GRAVE     = 50,  ///< 雷矛墓地中立光环
    BG_AV_OBJECT_AURA_A_STORMPIKE_GRAVE     = 51,  ///< 雷矛墓地联盟光环
    BG_AV_OBJECT_AURA_H_STORMPIKE_GRAVE     = 52,  ///< 雷矛墓地部落光环
    BG_AV_OBJECT_AURA_N_STONEHEART_GRAVE    = 53,  ///< 石炉墓地中立光环
    BG_AV_OBJECT_AURA_A_STONEHEART_GRAVE    = 54,  ///< 石炉墓地联盟光环
    BG_AV_OBJECT_AURA_H_STONEHEART_GRAVE    = 55,  ///< 石炉墓地部落光环
    BG_AV_OBJECT_AURA_N_SNOWFALL_GRAVE      = 56,  ///< 雪落墓地中立光环
    BG_AV_OBJECT_AURA_A_SNOWFALL_GRAVE      = 57,  ///< 雪落墓地联盟光环
    BG_AV_OBJECT_AURA_H_SNOWFALL_GRAVE      = 58,  ///< 雪落墓地部落光环
    BG_AV_OBJECT_AURA_N_ICEBLOOD_GRAVE      = 59,  ///< 冰血墓地中立光环
    BG_AV_OBJECT_AURA_A_ICEBLOOD_GRAVE      = 60,  ///< 冰血墓地联盟光环
    BG_AV_OBJECT_AURA_H_ICEBLOOD_GRAVE      = 61,  ///< 冰血墓地部落光环
    BG_AV_OBJECT_AURA_N_FROSTWOLF_GRAVE     = 62,  ///< 霜狼墓地中立光环
    BG_AV_OBJECT_AURA_A_FROSTWOLF_GRAVE     = 63,  ///< 霜狼墓地联盟光环
    BG_AV_OBJECT_AURA_H_FROSTWOLF_GRAVE     = 64,  ///< 霜狼墓地部落光环
    BG_AV_OBJECT_AURA_N_FROSTWOLF_HUT       = 65,  ///< 霜狼小屋中立光环
    BG_AV_OBJECT_AURA_A_FROSTWOLF_HUT       = 66,  ///< 霜狼小屋联盟光环
    BG_AV_OBJECT_AURA_H_FROSTWOLF_HUT       = 67,  ///< 霜狼小屋部落光环

    // 塔楼顶部的大旗帜（Tower Big Flags）- 每个塔楼2面旗帜（争夺、联盟/部落）+ 2个光环
    BG_AV_OBJECT_TFLAG_A_DUNBALDAR_SOUTH     = 67,  ///< 丹巴达尔南部碉堡联盟大旗帜
    BG_AV_OBJECT_TFLAG_H_DUNBALDAR_SOUTH     = 68,  ///< 丹巴达尔南部碉堡部落大旗帜
    BG_AV_OBJECT_TFLAG_A_DUNBALDAR_NORTH     = 69,  ///< 丹巴达尔北部碉堡联盟大旗帜
    BG_AV_OBJECT_TFLAG_H_DUNBALDAR_NORTH     = 70,  ///< 丹巴达尔北部碉堡部落大旗帜
    BG_AV_OBJECT_TFLAG_A_ICEWING_BUNKER      = 71,  ///< 冰翼碉堡联盟大旗帜
    BG_AV_OBJECT_TFLAG_H_ICEWING_BUNKER      = 72,  ///< 冰翼碉堡部落大旗帜
    BG_AV_OBJECT_TFLAG_A_STONEHEART_BUNKER   = 73,  ///< 石炉碉堡联盟大旗帜
    BG_AV_OBJECT_TFLAG_H_STONEHEART_BUNKER   = 74,  ///< 石炉碉堡部落大旗帜
    BG_AV_OBJECT_TFLAG_A_ICEBLOOD_TOWER      = 75,  ///< 冰血哨塔联盟大旗帜
    BG_AV_OBJECT_TFLAG_H_ICEBLOOD_TOWER      = 76,  ///< 冰血哨塔部落大旗帜
    BG_AV_OBJECT_TFLAG_A_TOWER_POINT         = 77,  ///< 哨塔点联盟大旗帜
    BG_AV_OBJECT_TFLAG_H_TOWER_POINT         = 78,  ///< 哨塔点部落大旗帜
    BG_AV_OBJECT_TFLAG_A_FROSTWOLF_ETOWER    = 79,  ///< 霜狼东塔联盟大旗帜
    BG_AV_OBJECT_TFLAG_H_FROSTWOLF_ETOWER    = 80,  ///< 霜狼东塔部落大旗帜
    BG_AV_OBJECT_TFLAG_A_FROSTWOLF_WTOWER    = 81,  ///< 霜狼西塔联盟大旗帜
    BG_AV_OBJECT_TFLAG_H_FROSTWOLF_WTOWER    = 82,  ///< 霜狼西塔部落大旗帜

    // 塔楼光环（Tower Auras）
    BG_AV_OBJECT_TAURA_A_DUNBALDAR_SOUTH     = 83,  ///< 丹巴达尔南部碉堡联盟光环
    BG_AV_OBJECT_TAURA_H_DUNBALDAR_SOUTH     = 84,  ///< 丹巴达尔南部碉堡部落光环
    BG_AV_OBJECT_TAURA_A_DUNBALDAR_NORTH     = 85,  ///< 丹巴达尔北部碉堡联盟光环
    BG_AV_OBJECT_TAURA_H_DUNBALDAR_NORTH     = 86,  ///< 丹巴达尔北部碉堡部落光环
    BG_AV_OBJECT_TAURA_A_ICEWING_BUNKER      = 87,  ///< 冰翼碉堡联盟光环
    BG_AV_OBJECT_TAURA_H_ICEWING_BUNKER      = 88,  ///< 冰翼碉堡部落光环
    BG_AV_OBJECT_TAURA_A_STONEHEART_BUNKER   = 89,  ///< 石炉碉堡联盟光环
    BG_AV_OBJECT_TAURA_H_STONEHEART_BUNKER   = 90,  ///< 石炉碉堡部落光环
    BG_AV_OBJECT_TAURA_A_ICEBLOOD_TOWER      = 91,  ///< 冰血哨塔联盟光环
    BG_AV_OBJECT_TAURA_H_ICEBLOOD_TOWER      = 92,  ///< 冰血哨塔部落光环
    BG_AV_OBJECT_TAURA_A_TOWER_POINT         = 93,  ///< 哨塔点联盟光环
    BG_AV_OBJECT_TAURA_H_TOWER_POINT         = 94,  ///< 哨塔点部落光环
    BG_AV_OBJECT_TAURA_A_FROSTWOLF_ETOWER    = 95,  ///< 霜狼东塔联盟光环
    BG_AV_OBJECT_TAURA_H_FROSTWOLF_ETOWER    = 96,  ///< 霜狼东塔部落光环
    BG_AV_OBJECT_TAURA_A_FROSTWOLF_WTOWER    = 97,  ///< 霜狼西塔联盟光环
    BG_AV_OBJECT_TAURA_H_FROSTWOLF_WTOWER    = 98,  ///< 霜狼西塔部落光环

    // 燃烧效果（Burning Effects）- 用于塔楼被摧毁时的视觉效果
    BG_AV_OBJECT_BURN_DUNBALDAR_SOUTH        = 99,   ///< 丹巴达尔南部碉堡燃烧效果
    BG_AV_OBJECT_BURN_DUNBALDAR_NORTH        = 109,  ///< 丹巴达尔北部碉堡燃烧效果
    BG_AV_OBJECT_BURN_ICEWING_BUNKER         = 119,  ///< 冰翼碉堡燃烧效果
    BG_AV_OBJECT_BURN_STONEHEART_BUNKER      = 129,  ///< 石炉碉堡燃烧效果
    BG_AV_OBJECT_BURN_ICEBLOOD_TOWER         = 139,  ///< 冰血哨塔燃烧效果
    BG_AV_OBJECT_BURN_TOWER_POINT            = 149,  ///< 哨塔点燃烧效果
    BG_AV_OBJECT_BURN_FROSTWOLF_ETWOER       = 159,  ///< 霜狼东塔燃烧效果
    BG_AV_OBJECT_BURN_FROSTWOLF_WTOWER       = 169,  ///< 霜狼西塔燃烧效果
    BG_AV_OBJECT_BURN_BUILDING_ALLIANCE      = 179,  ///< 联盟建筑燃烧效果
    BG_AV_OBJECT_BURN_BUILDING_HORDE         = 189,  ///< 部落建筑燃烧效果

    // 雪落墓地装饰旗帜（Snowfall Eyecandy Banners）
    BG_AV_OBJECT_SNOW_EYECANDY_A             = 199,  ///< 雪落墓地联盟装饰
    BG_AV_OBJECT_SNOW_EYECANDY_PA            = 203,  ///< 雪落墓地联盟前装饰
    BG_AV_OBJECT_SNOW_EYECANDY_H             = 207,  ///< 雪落墓地部落装饰
    BG_AV_OBJECT_SNOW_EYECANDY_PH            = 211,  ///< 雪落墓地部落前装饰

    // 矿井补给品（Mine Supplies）
    BG_AV_OBJECT_MINE_SUPPLY_N_MIN           = 215,  ///< 北部矿井补给品最小索引
    BG_AV_OBJECT_MINE_SUPPLY_N_MAX           = 224,  ///< 北部矿井补给品最大索引
    BG_AV_OBJECT_MINE_SUPPLY_S_MIN           = 225,  ///< 南部矿井补给品最小索引
    BG_AV_OBJECT_MINE_SUPPLY_S_MAX           = 236,  ///< 南部矿井补给品最大索引

    BG_AV_OBJECT_MAX                         = 237   ///< 对象类型总数
};

/**
 * @enum BG_AV_OBJECTS
 * @brief 游戏对象位置枚举
 *
 * 定义了奥特兰克山谷战场中游戏对象的放置位置索引
 * 用于定位旗帜、光环、燃烧效果和矿井补给品等对象
 */
enum BG_AV_OBJECTS
{
    // 墓地旗帜位置（Graveyard Flag Positions）
    AV_OPLACE_FIRSTAID_STATION              = 0,   ///< 急救站墓地旗帜位置
    AV_OPLACE_STORMPIKE_GRAVE               = 1,   ///< 雷矛墓地旗帜位置
    AV_OPLACE_STONEHEART_GRAVE              = 2,   ///< 石炉墓地旗帜位置
    AV_OPLACE_SNOWFALL_GRAVE                = 3,   ///< 雪落墓地旗帜位置
    AV_OPLACE_ICEBLOOD_GRAVE                = 4,   ///< 冰血墓地旗帜位置
    AV_OPLACE_FROSTWOLF_GRAVE               = 5,   ///< 霜狼墓地旗帜位置
    AV_OPLACE_FROSTWOLF_HUT                 = 6,   ///< 霜狼小屋墓地旗帜位置

    // 塔楼旗帜位置（Tower Flag Positions）
    AV_OPLACE_DUNBALDAR_SOUTH               = 7,   ///< 丹巴达尔南部碉堡旗帜位置
    AV_OPLACE_DUNBALDAR_NORTH               = 8,   ///< 丹巴达尔北部碉堡旗帜位置
    AV_OPLACE_ICEWING_BUNKER                = 9,   ///< 冰翼碉堡旗帜位置
    AV_OPLACE_STONEHEART_BUNKER             = 10,  ///< 石炉碉堡旗帜位置
    AV_OPLACE_ICEBLOOD_TOWER                = 11,  ///< 冰血哨塔旗帜位置
    AV_OPLACE_TOWER_POINT                   = 12,  ///< 哨塔点旗帜位置
    AV_OPLACE_FROSTWOLF_ETOWER              = 13,  ///< 霜狼东塔旗帜位置
    AV_OPLACE_FROSTWOLF_WTOWER              = 14,  ///< 霜狼西塔旗帜位置

    // 塔楼大旗帜位置（Tower Big Banner Positions）
    AV_OPLACE_BIGBANNER_DUNBALDAR_SOUTH     = 15,  ///< 丹巴达尔南部碉堡大旗帜位置
    AV_OPLACE_BIGBANNER_DUNBALDAR_NORTH     = 16,  ///< 丹巴达尔北部碉堡大旗帜位置
    AV_OPLACE_BIGBANNER_ICEWING_BUNKER      = 17,  ///< 冰翼碉堡大旗帜位置
    AV_OPLACE_BIGBANNER_STONEHEART_BUNKER   = 18,  ///< 石炉碉堡大旗帜位置
    AV_OPLACE_BIGBANNER_ICEBLOOD_TOWER      = 19,  ///< 冰血哨塔大旗帜位置
    AV_OPLACE_BIGBANNER_TOWER_POINT         = 20,  ///< 哨塔点大旗帜位置
    AV_OPLACE_BIGBANNER_FROSTWOLF_ETOWER    = 21,  ///< 霜狼东塔大旗帜位置
    AV_OPLACE_BIGBANNER_FROSTWOLF_WTOWER    = 22,  ///< 霜狼西塔大旗帜位置

    // 燃烧效果位置（Burning Effect Positions）
    AV_OPLACE_BURN_DUNBALDAR_SOUTH          = 23,  ///< 丹巴达尔南部碉堡燃烧效果起始位置
    AV_OPLACE_BURN_DUNBALDAR_NORTH          = 33,  ///< 丹巴达尔北部碉堡燃烧效果起始位置
    AV_OPLACE_BURN_ICEWING_BUNKER           = 43,  ///< 冰翼碉堡燃烧效果起始位置
    AV_OPLACE_BURN_STONEHEART_BUNKER        = 53,  ///< 石炉碉堡燃烧效果起始位置
    AV_OPLACE_BURN_ICEBLOOD_TOWER           = 63,  ///< 冰血哨塔燃烧效果起始位置
    AV_OPLACE_BURN_TOWER_POINT              = 73,  ///< 哨塔点燃烧效果起始位置
    AV_OPLACE_BURN_FROSTWOLF_ETOWER         = 83,  ///< 霜狼东塔燃烧效果起始位置
    AV_OPLACE_BURN_FROSTWOLF_WTOWER         = 93,  ///< 霜狼西塔燃烧效果起始位置
    AV_OPLACE_BURN_BUILDING_A               = 103, ///< 联盟建筑燃烧效果起始位置
    AV_OPLACE_BURN_BUILDING_H               = 113, ///< 部落建筑燃烧效果起始位置

    // 雪落墓地装饰位置（Snowfall Eyecandy Positions）
    AV_OPLACE_SNOW_1                        = 123, ///< 雪落墓地装饰位置1
    AV_OPLACE_SNOW_2                        = 124, ///< 雪落墓地装饰位置2
    AV_OPLACE_SNOW_3                        = 125, ///< 雪落墓地装饰位置3
    AV_OPLACE_SNOW_4                        = 126, ///< 雪落墓地装饰位置4

    // 矿井补给品位置（Mine Supply Positions）
    AV_OPLACE_MINE_SUPPLY_N_MIN             = 127, ///< 北部矿井补给品最小位置
    AV_OPLACE_MINE_SUPPLY_N_MAX             = 136, ///< 北部矿井补给品最大位置
    AV_OPLACE_MINE_SUPPLY_S_MIN             = 137, ///< 南部矿井补给品最小位置
    AV_OPLACE_MINE_SUPPLY_S_MAX             = 148, ///< 南部矿井补给品最大位置

    AV_OPLACE_MAX                           = 149  ///< 对象位置总数
};

/**
 * @brief 游戏对象位置数组
 *
 * 定义了奥特兰克山谷战场中所有游戏对象的世界坐标位置
 * 包括墓地旗帜、塔楼旗帜、大旗帜、燃烧效果、雪落装饰和矿井补给品等
 */
Position const BG_AV_ObjectPos[AV_OPLACE_MAX] =
{
    {638.592f, -32.422f, 46.0608f, -1.62316f }, //firstaid station
    {669.007f, -294.078f, 30.2909f, 2.77507f }, //stormpike
    {77.8013f, -404.7f, 46.7549f, -0.872665f }, //stone grave
    {-202.581f, -112.73f, 78.4876f, -0.715585f }, //snowfall
    {-611.962f, -396.17f, 60.8351f, 2.53682f},  //iceblood grave
    {-1082.45f, -346.823f, 54.9219f, -1.53589f }, //frostwolf grave
    {-1402.21f, -307.431f, 89.4424f, 0.191986f }, //frostwolf hut
    {553.779f, -78.6566f, 51.9378f, -1.22173f }, //dunnbaldar south
    {674.001f, -143.125f, 63.6615f, 0.994838f }, //dunbaldar north
    {203.281f, -360.366f, 56.3869f, -0.925024f }, //icew
    {-152.437f, -441.758f, 40.3982f, -1.95477f }, //stone
    {-571.88f, -262.777f, 75.0087f, -0.802851f }, //ice tower
    {-768.907f, -363.71f, 90.8949f, 1.07991f},  //tower point
    {-1302.9f, -316.981f, 113.867f, 2.00713f }, //frostwolf etower
    {-1297.5f, -266.767f, 114.15f, 3.31044f},   //frostwolf wtower
    //bigbanner:
    {555.848f, -84.4151f, 64.4397f, 3.12414f }, //duns
    {679.339f, -136.468f, 73.9626f, -2.16421f }, //dunn
    {208.973f, -365.971f, 66.7409f, -0.244346f }, //icew
    {-155.832f, -449.401f, 52.7306f, 0.610865f }, //stone
    {-572.329f, -262.476f, 88.6496f, -0.575959f }, //icetower
    {-768.199f, -363.105f, 104.537f, 0.10472f }, //towerp
    {-1302.84f, -316.582f, 127.516f, 0.122173f }, //etower
    {-1297.87f, -266.762f, 127.796f, 0.0698132f }, //wtower
    //burning auras towers have 9*179065 captain-buildings have 5*179066+5*179065
    //dunns
    {562.632f, -88.1815f, 61.993f, 0.383972f },
    {562.523f, -74.5028f, 37.9474f, -0.0523599f },
    {558.097f, -70.9842f, 52.4876f, 0.820305f },
    {578.167f, -71.8191f, 38.1514f, 2.72271f },
    {556.028f, -94.9242f, 44.8191f, 3.05433f },
    {572.451f, -94.3655f, 37.9443f, -1.72788f },
    {549.263f, -79.3645f, 44.8191f, 0.436332f },
    {543.513f, -94.4006f, 52.4819f, 0.0349066f },
    {572.149f, -93.7862f, 52.5726f, 0.541052f },
    {582.162f, -81.2375f, 37.9216f, 0.0872665f },
    //dunn
    {664.797f, -143.65f, 64.1784f, -0.453786f},
    {664.505f, -139.452f, 49.6696f, -0.0349067f},
    {676.067f, -124.319f, 49.6726f, -1.01229f},
    {693.004f, -144.025f, 64.1755f, 2.44346f},
    {661.175f, -117.691f, 49.645f, 1.91986f},
    {684.423f, -146.582f, 63.6662f, 0.994838f},
    {682.791f, -127.769f, 62.4155f, 1.09956f},
    {674.576f, -147.101f, 56.5425f, -1.6057f},
    {655.719f, -126.673f, 49.8138f, 2.80998f},
    {0, 0, 0, 0},
    //icew
    {231.503f, -356.688f, 42.3704f, 0.296706f},
    {224.989f, -348.175f, 42.5607f, 1.50098f},
    {205.782f, -351.335f, 56.8998f, 1.01229f},
    {196.605f, -369.187f, 56.3914f, 2.46091f},
    {210.619f, -376.938f, 49.2677f, 2.86234f},
    {209.647f, -352.632f, 42.3959f, -0.698132f},
    {220.65f, -368.132f, 42.3978f, -0.2618f},
    {224.682f, -374.031f, 57.0679f, 0.541052f},
    {200.26f, -359.968f, 49.2677f, -2.89725f},
    {196.619f, -378.016f, 56.9131f, 1.01229f},
    //stone
    {-155.488f, -437.356f, 33.2796f, 2.60054f},
    {-163.441f, -454.188f, 33.2796f, 1.93732f},
    {-143.977f, -445.148f, 26.4097f, -1.8675f},
    {-135.764f, -464.708f, 26.3823f, 2.25147f},
    {-154.076f, -466.929f, 41.0636f, -1.8675f},
    {-149.908f, -460.332f, 26.4083f, -2.09439f},
    {-151.638f, -439.521f, 40.3797f, 0.436332f},
    {-131.301f, -454.905f, 26.5771f, 2.93215f},
    {-171.291f, -444.684f, 40.9211f, 2.30383f},
    {-143.591f, -439.75f, 40.9275f, -1.72788f},
    //iceblood
    {-572.667f, -267.923f, 56.8542f, 2.35619f},
    {-561.021f, -262.689f, 68.4589f, 1.37881f},
    {-572.538f, -262.649f, 88.6197f, 1.8326f},
    {-574.77f, -251.45f, 74.9422f, -1.18682f},
    {-578.625f, -267.571f, 68.4696f, 0.506145f},
    {-571.476f, -257.234f, 63.3223f, 3.10669f},
    {-566.035f, -273.907f, 52.9582f, -0.890118f},
    {-580.948f, -259.77f, 68.4696f, 1.46608f},
    {-568.318f, -267.1f, 75.0008f, 1.01229f},
    {-559.621f, -268.597f, 52.8986f, 0.0523599f},
    //towerp
    {-776.072f, -368.046f, 84.3558f, 2.63545f},
    {-777.564f, -368.521f, 90.6701f, 1.72788f},
    {-765.461f, -357.711f, 90.888f, 0.314159f},
    {-768.763f, -362.735f, 104.612f, 1.81514f},
    {-760.356f, -358.896f, 84.3558f, 2.1293f},
    {-771.967f, -352.838f, 84.3484f, 1.74533f},
    {-773.333f, -364.653f, 79.2351f, -1.64061f},
    {-764.109f, -366.069f, 70.0934f, 0.383972f},
    {-767.103f, -350.737f, 68.7933f, 2.80998f},
    {-760.115f, -353.845f, 68.8633f, 1.79769f},
    //froste
    {-1304.87f, -304.525f, 91.8366f, -0.680679f},
    {-1301.77f, -310.974f, 95.8252f, 0.907571f},
    {-1305.58f, -320.625f, 102.166f, -0.558505f},
    {-1294.27f, -323.468f, 113.893f, -1.67552f},
    {-1302.65f, -317.192f, 127.487f, 2.30383f},
    {-1293.89f, -313.478f, 107.328f, 1.6057f},
    {-1312.41f, -312.999f, 107.328f, 1.5708f},
    {-1311.57f, -308.08f, 91.7666f, -1.85005f},
    {-1314.7f, -322.131f, 107.36f, 0.645772f},
    {-1304.6f, -310.754f, 113.859f, -0.401426f},
    //frostw
    {-1308.24f, -273.26f, 92.0514f, -0.139626f},
    {-1302.26f, -262.858f, 95.9269f, 0.418879f},
    {-1297.28f, -267.773f, 126.756f, 2.23402f},
    {-1299.08f, -256.89f, 114.108f, -2.44346f},
    {-1303.41f, -268.237f, 114.151f, -1.23918f},
    {-1304.43f, -273.682f, 107.612f, 0.244346f},
    {-1309.53f, -265.951f, 92.1418f, -2.49582f},
    {-1295.55f, -263.865f, 105.033f, 0.925024f},
    {-1294.71f, -281.466f, 107.664f, -1.50098f},
    {-1289.69f, -259.521f, 107.612f, -2.19912f},

    //the two buildings of the captains
    //alliance
    {-64.4987f, -289.33f, 33.4616f, -2.82743f},
    {-5.98025f, -326.144f, 38.8538f, 0},
    {-2.67893f, -306.998f, 33.4165f, 0},
    {-60.25f, -309.232f, 50.2408f, -1.46608f},
    {-48.7941f, -266.533f, 47.7916f, 2.44346f},
    {-3.40929f, -306.288f, 33.34f, 0},
    {-48.619f, -266.917f, 47.8168f, 0},
    {-62.9474f, -286.212f, 66.7288f, 0},
    {-5.05132f, -325.323f, 38.8536f, 0},
    {-64.2677f, -289.412f, 33.469f, 0},
//horde
    {-524.276f, -199.6f, 82.8733f, -1.46608f},
    {-518.196f, -173.085f, 102.43f, 0},
    {-500.732f, -145.358f, 88.5337f, 2.44346f},
    {-501.084f, -150.784f, 80.8506f, 0},
    {-518.309f, -163.963f, 102.521f, 2.96706f},
    {-517.053f, -200.429f, 80.759f, 0},
    {-514.361f, -163.864f, 104.163f, 0},
    {-568.04f, -188.707f, 81.55f, 0},
    {-501.775f, -151.581f, 81.2027f, 0},
    {-509.975f, -191.652f, 83.2978f, 0},

//snowfall eyecandy
    {-191.153f, -129.868f, 78.5595f, -1.25664f },
    {-201.282f, -134.319f, 78.6753f, -0.942478f },
    {-215.981f, -91.4101f, 80.8702f, -1.74533f },
    {-200.465f, -96.418f, 79.7587f, 1.36136f },
    // 矿井补给品位置（Mine Supplies Positions）
    // 铁深矿井（Irondeep Mine）
    {870.899f, -388.434f, 61.6406f, -1.22173f},
    {825.214f, -320.174f, 63.712f, -2.82743f},
    {837.117f, -452.556f, 47.2331f, -3.12414f},
    {869.755f, -448.867f, 52.5448f, -0.855212f},
    {949.877f, -458.198f, 56.4874f, 0.314159f},
    {900.35f, -479.024f, 58.3553f, 0.122173f},
    {854.449f, -442.255f, 50.6589f, 0.401426f},
    {886.685f, -442.358f, 54.6962f, -1.22173f},
    {817.509f, -457.331f, 48.4666f, 2.07694f},
    {793.411f, -326.281f, 63.1117f, -2.79253f},
    // 冷齿矿井（Coldtooth Mine）
    {-934.212f, -57.3517f, 80.277f, -0.0174535f},
    {-916.281f, -36.8579f, 77.0227f, 0.122173f},
    {-902.73f, -103.868f, 75.4378f, -1.58825f},
    {-900.514f, -143.527f, 75.9686f, 1.8675f},
    {-862.882f, -0.353299f, 72.1526f, -2.51327f},
    {-854.932f, -85.9184f, 68.6056f, -2.04204f},
    {-851.833f, -118.959f, 63.8672f, -0.0698131f},
    {-849.832f, -20.8421f, 70.4672f, -1.81514f},
    {-844.25f, -60.0374f, 72.1031f, -2.19912f},
    {-820.644f, -136.043f, 63.1977f, 2.40855f},
    {-947.642f, -208.807f, 77.0101f, 1.36136f},
    {-951.394f, -193.695f, 67.634f, 0.802851f}
};

/**
 * @brief 大门位置数组
 *
 * 定义了联盟和部落大门的世界坐标位置
 */
Position const BG_AV_DoorPositons[2] =
{
    {794.64310f, -493.4745f, 99.77789f, -0.122173f}, ///< 联盟大门位置
    {-1382.057f, -545.9169f, 54.90467f, 0.7679439f}  ///< 部落大门位置
};

/**
 * @brief 大门旋转四元数数组
 *
 * 定义了联盟和部落大门的旋转数据
 */
QuaternionData const BG_AV_DoorRotation[2] =
{
    {0.0f, 0.0f, -0.06104851f, 0.9981348f}, ///< 联盟大门旋转
    {0.0f, 0.0f, 0.374606100f, 0.9271840f}  ///< 部落大门旋转
};

/**
 * @enum BG_AV_CreaturePlace
 * @brief 生物位置枚举
 *
 * 定义了奥特兰克山谷战场中所有生物的位置索引
 * 包括灵魂医者、防御者、元帅、矿井生物等
 * 与 BG_AV_CreaturePos 数组相关联
 */
enum BG_AV_CreaturePlace
{
    // 灵魂医者（Spirit Guides）- 每个墓地一个
    AV_CPLACE_SPIRIT_STORM_AID      = 0,   ///< 急救站墓地灵魂医者
    AV_CPLACE_SPIRIT_STORM_GRAVE    = 1,   ///< 雷矛墓地灵魂医者
    AV_CPLACE_SPIRIT_STONE_GRAVE    = 2,   ///< 石炉墓地灵魂医者
    AV_CPLACE_SPIRIT_SNOWFALL       = 3,   ///< 雪落墓地灵魂医者
    AV_CPLACE_SPIRIT_ICE_GRAVE      = 4,   ///< 冰血墓地灵魂医者
    AV_CPLACE_SPIRIT_FROSTWOLF      = 5,   ///< 霜狼墓地灵魂医者
    AV_CPLACE_SPIRIT_FROST_HUT      = 6,   ///< 霜狼小屋墓地灵魂医者
    AV_CPLACE_SPIRIT_MAIN_ALLIANCE  = 7,   ///< 联盟主基地灵魂医者
    AV_CPLACE_SPIRIT_MAIN_HORDE     = 8,   ///< 部落主基地灵魂医者

    // 墓地防御者（Graveyard Defenders）- 我没有为所有4个位置添加变量，我认为一个足够计算其余的
    AV_CPLACE_DEFENSE_STORM_AID      = 9,   ///< 急救站墓地防御者起始位置
    AV_CPLACE_DEFEMSE_STORM_GRAVE    = 13,  ///< 雷矛墓地防御者起始位置
    AV_CPLACE_DEFENSE_STONE_GRAVE    = 17,  ///< 石炉墓地防御者起始位置
    AV_CPLACE_DEFENSE_SNOWFALL       = 21,  ///< 雪落墓地防御者起始位置
    AV_CPLACE_DEFENSE_FROSTWOLF      = 25,  ///< 霜狼墓地防御者起始位置
    AV_CPLACE_DEFENSE_ICE_GRAVE      = 29,  ///< 冰血墓地防御者起始位置
    AV_CPLACE_DEFENSE_FROST_HUT      = 33,  ///< 霜狼小屋墓地防御者起始位置

    // 塔楼防御者（Tower Defenders）- 弓箭手
    AV_CPLACE_DEFENSE_DUN_S          = 37,  ///< 丹巴达尔南部碉堡防御者
    AV_CPLACE_DEFENSE_DUN_N          = 41,  ///< 丹巴达尔北部碉堡防御者
    AV_CPLACE_DEFENSE_ICEWING        = 45,  ///< 冰翼碉堡防御者
    AV_CPLACE_DEFENSE_STONE_TOWER    = 49,  ///< 石炉碉堡防御者
    AV_CPLACE_DEFENSE_ICE_TOWER      = 53,  ///< 冰血哨塔防御者
    AV_CPLACE_DEFENSE_TOWERPOINT     = 57,  ///< 哨塔点防御者
    AV_CPLACE_DEFENSE_FROST_E        = 61,  ///< 霜狼东塔防御者
    AV_CPLACE_DEFENSE_FROST_t        = 65,  ///< 霜狼西塔防御者

    // 联盟元帅（Alliance Marshals）- 每个碉堡一个，保护将军
    AV_CPLACE_A_MARSHAL_SOUTH       = 69,  ///< 丹巴达尔南部碉堡元帅
    AV_CPLACE_A_MARSHAL_NORTH       = 70,  ///< 丹巴达尔北部碉堡元帅
    AV_CPLACE_A_MARSHAL_ICE         = 71,  ///< 冰翼碉堡元帅
    AV_CPLACE_A_MARSHAL_STONE       = 72,  ///< 石炉碉堡元帅

    // 部落督军（Horde Warmasters）- 每个塔楼一个，保护将军
    AV_CPLACE_H_MARSHAL_ICE         = 73,  ///< 冰血哨塔督军
    AV_CPLACE_H_MARSHAL_TOWER       = 74,  ///< 哨塔点督军
    AV_CPLACE_H_MARSHAL_ETOWER      = 75,  ///< 霜狼东塔督军
    AV_CPLACE_H_MARSHAL_WTOWER      = 76,  ///< 霜狼西塔督军

    // 铁深矿井（Irondeep Mine）
    // 矿工（Miners）
    AV_CPLACE_MINE_N_1_MIN      = 77,   ///< 铁深矿井矿工最小索引
    AV_CPLACE_MINE_N_1_MAX      = 136,  ///< 铁深矿井矿工最大索引
    // 特殊类型（Special Types）
    AV_CPLACE_MINE_N_2_MIN      = 137,  ///< 铁深矿井特殊类型最小索引
    AV_CPLACE_MINE_N_2_MAX      = 192,  ///< 铁深矿井特殊类型最大索引
    // 首领（Boss）
    AV_CPLACE_MINE_N_3          = 193,  ///< 铁深矿井首领位置

    // 冷齿矿井（Coldtooth Mine）
    // 矿工（Miners）
    AV_CPLACE_MINE_S_1_MIN      = 194,  ///< 冷齿矿井矿工最小索引
    AV_CPLACE_MINE_S_1_MAX      = 250,  ///< 冷齿矿井矿工最大索引
    // 特殊类型（Special Types）
    AV_CPLACE_MINE_S_2_MIN      = 251,  ///< 冷齿矿井特殊类型最小索引
    AV_CPLACE_MINE_S_2_MAX      = 289,  ///< 冷齿矿井特殊类型最大索引
    // 害虫（Vermin）
    AV_CPLACE_MINE_S_S_MIN      = 290,  ///< 冷齿矿井害虫最小索引
    AV_CPLACE_MINE_S_S_MAX      = 299,  ///< 冷齿矿井害虫最大索引
    // 首领（Boss）
    AV_CPLACE_MINE_S_3          = 300,  ///< 冷齿矿井首领位置

    // 传令官（Herald）
    AV_CPLACE_HERALD          = 301,  ///< 战场传令官位置

    // 节点光环触发器（Node Aura Triggers）
    AV_CPLACE_TRIGGER01       = 302,  ///< 节点触发器1
    AV_CPLACE_TRIGGER02       = 303,  ///< 节点触发器2
    AV_CPLACE_TRIGGER03       = 304,  ///< 节点触发器3
    AV_CPLACE_TRIGGER04       = 305,  ///< 节点触发器4
    AV_CPLACE_TRIGGER05       = 306,  ///< 节点触发器5
    AV_CPLACE_TRIGGER06       = 307,  ///< 节点触发器6
    AV_CPLACE_TRIGGER07       = 308,  ///< 节点触发器7
    AV_CPLACE_TRIGGER08       = 309,  ///< 节点触发器8
    AV_CPLACE_TRIGGER09       = 310,  ///< 节点触发器9
    AV_CPLACE_TRIGGER10       = 311,  ///< 节点触发器10
    AV_CPLACE_TRIGGER11       = 312,  ///< 节点触发器11
    AV_CPLACE_TRIGGER12       = 313,  ///< 节点触发器12
    AV_CPLACE_TRIGGER13       = 314,  ///< 节点触发器13
    AV_CPLACE_TRIGGER14       = 315,  ///< 节点触发器14
    AV_CPLACE_TRIGGER15       = 316,  ///< 节点触发器15

    // 首领和队长触发器（Boss and Captain Triggers）
    AV_CPLACE_TRIGGER16       = 317,  ///< 首领/队长触发器16
    AV_CPLACE_TRIGGER17       = 318,  ///< 首领/队长触发器17
    AV_CPLACE_TRIGGER18       = 319,  ///< 首领/队长触发器18
    AV_CPLACE_TRIGGER19       = 320,  ///< 首领/队长触发器19

    AV_CPLACE_MAX = 321  ///< 生物位置总数
};

/**
 * @brief 生物位置数组
 *
 * 定义了奥特兰克山谷战场中所有生物的世界坐标位置
 * 包括灵魂医者、防御者、元帅、矿井生物、传令官和触发器等
 */
Position const BG_AV_CreaturePos[AV_CPLACE_MAX] =
{
    //spiritguides
    {643.000000f, 44.000000f, 69.740196f, -0.001854f},
    {676.000000f, -374.000000f, 30.000000f, -0.001854f},
    {73.417755f, -496.433105f, 48.731918f, -0.001854f},
    {-157.409195f, 31.206272f, 77.050598f, -0.001854f},
    {-531.217834f, -405.231384f, 49.551376f, -0.001854f},
    {-1090.476807f, -253.308670f, 57.672371f, -0.001854f},
    {-1496.065063f, -333.338409f, 101.134804f, -0.001854f},
    {873.001770f, -491.283630f, 96.541931f, -0.001854f},
    {-1437.670044f, -610.088989f, 51.161900f, -0.001854f},
 //grave
 //firstaid
    {635.17f, -29.5594f, 46.5056f, 4.81711f},
    {642.488f, -32.9437f, 46.365f, 4.67748f},
    {642.326f, -27.9442f, 46.9211f, 4.59022f},
    {635.945f, -33.6171f, 45.7164f, 4.97419f},
   //stormpike
    {669.272f, -297.304f, 30.291f, 4.66604f},
    {674.08f, -292.328f, 30.4817f, 0.0918785f},
    {667.01f, -288.532f, 29.8809f, 1.81583f},
    {664.153f, -294.042f, 30.2851f, 3.28531f},
  //stone
    {81.7027f, -406.135f, 47.7843f, 0.598464f},
    {78.1431f, -409.215f, 48.0401f, 5.05953f},
    {73.4135f, -407.035f, 46.7527f, 3.34736f},
    {78.2258f, -401.859f, 46.4202f, 2.05852f},
  //snowfall
    {-207.412f, -110.616f, 78.7959f, 2.43251f},
    {-197.95f, -112.205f, 78.5686f, 6.22441f},
    {-202.709f, -116.829f, 78.4358f, 5.13742f},
    {-202.059f, -108.314f, 78.5783f, 5.91968f},
  //ice
    {-615.501f, -393.802f, 60.4299f, 3.06147f},
    {-608.513f, -392.717f, 62.5724f, 2.06323f},
    {-609.769f, -400.072f, 60.7174f, 5.22367f},
    {-616.093f, -398.293f, 60.5628f, 3.73613f},
  //frost
    {-1077.7f, -340.21f, 55.4682f, 6.25569f},
    {-1082.74f, -333.821f, 54.7962f, 2.05459f},
    {-1090.66f, -341.267f, 54.6768f, 3.27746f},
    {-1081.58f, -344.63f, 55.256f, 4.75636f},
  //frost hut
    {-1408.95f, -311.69f, 89.2536f, 4.49954f},
    {-1407.15f, -305.323f, 89.1993f, 2.86827f},
    {-1400.64f, -304.3f, 89.7008f, 1.0595f},
    {-1400.4f, -311.35f, 89.3028f, 4.99434f},
  //towers
  //dun south - OK
    {569.395f, -101.064f, 52.8296f, 2.34974f},
    {574.85f, -92.9842f, 52.5869f, 3.09325f},
    {575.411f, -83.597f, 52.3626f, 6.26573f},
    {571.352f, -75.6582f, 52.479f, 0.523599f},
    //dun north - OK
    {668.60f, -122.53f, 64.12f, 2.34f}, /// @todo: To be confirm - Not completely okay
    {662.253f, -129.105f, 64.1794f, 2.77507f},
    {661.209f, -138.877f, 64.2251f, 3.38594f},
    {665.481f, -146.857f, 64.1271f, 3.75246f},
    //icewing - OK
    {225.228f, -368.909f, 56.9983f, 6.23806f},
    {191.36f, -369.899f, 57.1524f, 3.24631f},
    {215.518f, -384.019f, 56.9889f, 5.09636f},
    {199.625f, -382.177f, 56.8691f, 4.08407f},
    //stone
    {-172.851f, -452.366f, 40.8725f, 3.31829f},
    {-147.147f, -435.053f, 40.8022f, 0.599238f},
    {-169.456f, -440.325f, 40.985f, 2.59101f},
    {-163.494f, -434.904f, 41.0725f, 1.84174f},
    //ice - OK
    {-573.522f, -271.854f, 75.0078f, 3.9619f},
    {-565.616f, -269.051f, 74.9952f, 5.02655f},
    {-562.825f, -261.087f, 74.9898f, 5.95157f},
    {-569.176f, -254.446f, 74.8771f, 0.820305f},
    //towerpoint
    {-763.04f, -371.032f, 90.7933f, 5.25979f},
    {-759.764f, -358.264f, 90.8681f, 0.289795f},
    {-768.808f, -353.056f, 90.8811f, 1.52601f},
    {-775.944f, -362.639f, 90.8949f, 2.59573f},
    //frost etower
    {-1294.13f, -313.045f, 107.328f, 0.270162f},
    {-1306.5f, -308.105f, 113.767f, 1.78755f},
    {-1294.78f, -319.966f, 113.79f, 5.94545f},
    {-1294.83f, -312.241f, 113.799f, 0.295293f},
    //frost wtower
    {-1300.96f, -275.111f, 114.058f, 4.12804f},
    {-1302.41f, -259.256f, 114.065f, 1.67602f},
    {-1287.97f, -262.087f, 114.165f, 6.18264f},
    {-1291.59f, -271.166f, 114.151f, 5.28257f},

    //alliance marshall
    {721.104f, -7.64155f, 50.7046f, 3.45575f}, // south
    {723.058f, -14.1548f, 50.7046f, 3.40339f}, // north
    {715.691f, -4.72233f, 50.2187f, 3.47321f}, // icewing
    {720.046f, -19.9413f, 50.2187f, 3.36849f}, // stone
/// horde @todo: Confirm positions
    {-1363.99f, -221.99f, 98.4053f, 4.93012f},
    {-1370.96f, -223.532f, 98.4266f, 4.93012f},
    {-1378.37f, -228.614f, 99.3546f, 5.38565f},
    {-1358.02f, -228.998f, 98.868f, 3.87768f},

 //irondeep mine
 //Irondeep Trogg
    {971.671f, -442.657f, 57.6951f, 3.1765f},
    {969.979f, -457.148f, 58.1119f, 4.5204f},
    {958.692f, -333.477f, 63.2276f, 5.77704f},
    {957.113f, -325.92f, 61.7589f, 1.13446f},
    {948.25f, -448.268f, 56.9009f, 5.60251f},
    {934.727f, -385.802f, 63.0344f, 3.75246f},
    {931.751f, -403.458f, 59.6737f, 5.63741f},
    {931.146f, -359.666f, 66.0294f, 3.9619f},
    {929.702f, -412.401f, 56.8776f, 5.89921f},
    {926.849f, -379.074f, 63.5286f, 2.0944f},
    {921.972f, -358.597f, 66.4313f, 2.93215f},
    {921.449f, -341.981f, 67.1264f, 3.4383f},
    {921.1f, -395.812f, 60.4615f, 2.71695f},
    {919.274f, -394.986f, 60.3478f, 2.71696f},
    {916.852f, -393.891f, 60.1726f, 2.71695f},
    {914.568f, -326.21f, 66.1733f, 2.25147f},
    {913.064f, -395.773f, 60.1364f, 4.41568f},
    {909.246f, -474.576f, 58.2067f, 0.226893f},
    {909.246f, -474.576f, 58.2901f, 0.226893f},
    {907.209f, -428.267f, 59.8065f, 1.8675f},
    {905.973f, -459.528f, 58.7594f, 1.37189f},
    {905.067f, -396.074f, 60.2085f, 5.07891f},
    {901.809f, -457.709f, 59.0116f, 3.52557f},
    {900.962f, -427.44f, 59.0842f, 1.50098f},
    {897.929f, -471.742f, 59.7729f, 2.54818f},
    {893.376f, -343.171f, 68.1499f, 5.35816f},
    {890.584f, -406.049f, 61.1925f, 5.67232f},
    {888.208f, -332.564f, 68.148f, 1.93732f},
    {887.647f, -391.537f, 61.8734f, 1.37881f},
    {885.109f, -343.338f, 67.0867f, 3.78979f},
    {881.618f, -419.948f, 53.5228f, 0.593412f},
    {878.675f, -345.36f, 66.1052f, 3.45651f},
    {877.127f, -351.8f, 66.5296f, 5.74213f},
    {876.778f, -345.97f, 65.7724f, 3.45262f},
    {874.577f, -414.786f, 52.7817f, 1.67552f},
    {868.247f, -343.136f, 64.9894f, 1.6057f},
    {859.03f, -367.231f, 47.4655f, 0.0174533f},
    {857.513f, -351.817f, 65.1867f, 4.39823f},
    {852.632f, -372.416f, 48.1657f, 3.66519f},
    {849.86f, -340.944f, 66.2447f, 0.401426f},
    {847.99f, -386.287f, 60.9277f, 2.32374f},
    {847.601f, -423.072f, 50.0852f, 4.57276f},
    {847.135f, -411.307f, 50.2106f, 1.5708f},
    {835.077f, -379.418f, 48.2755f, 5.93412f},
    {834.87f, -453.304f, 47.9075f, 0.226893f},
    {834.634f, -365.981f, 62.8801f, 1.32645f},
    {834.354f, -355.526f, 48.1491f, 6.07375f},
    {833.702f, -327.506f, 65.0439f, 0.331613f},
    {833.151f, -374.228f, 63.0938f, 3.66519f},
    {831.711f, -346.785f, 47.2975f, 0.226893f},
    {827.874f, -413.624f, 48.5818f, 1.49241f},
    {827.728f, -415.483f, 48.5593f, 1.49238f},
    {827.016f, -424.543f, 48.2856f, 1.49236f},
    {823.222f, -334.283f, 65.6306f, 4.88692f},
    {821.892f, -464.723f, 48.9451f, 4.66003f},
    {821.006f, -387.635f, 49.0728f, 3.15905f},
    {817.26f, -447.432f, 49.4308f, 2.18166f},
    {805.399f, -320.146f, 52.7712f, 0.296706f},
    {801.405f, -328.055f, 53.0195f, 4.31096f},
    //irondeep skullthumber irondeep shaman
    {955.812f, -440.302f, 55.3411f, 3.19395f},
    {937.378f, -377.816f, 65.3919f, 3.56047f},
    {925.059f, -331.347f, 65.7564f, 3.66519f},
    {922.918f, -396.634f, 60.3942f, 2.71695f},
    {909.99f, -462.154f, 59.0811f, 3.7001f},
    {907.893f, -388.787f, 61.7923f, 5.74213f},
    {898.801f, -437.105f, 58.5266f, 0.959931f},
    {884.237f, -407.597f, 61.566f, 0.820305f},
    {880.744f, -344.683f, 66.4086f, 3.4644f},
    {876.047f, -341.857f, 65.8743f, 4.45059f},
    {874.674f, -402.077f, 61.7573f, 0.26341f},
    {871.914f, -404.209f, 62.1269f, 6.06163f},
    {871.606f, -403.665f, 62.0795f, 0.765774f},
    {871.561f, -404.114f, 62.1297f, 0.00981727f},
    {871.528f, -404.248f, 62.1455f, 0.498032f},
    {871.493f, -404.122f, 62.1331f, 5.65727f},
    {871.282f, -403.843f, 62.1108f, 0.788382f},
    {868.294f, -392.395f, 61.4772f, 4.38685f},
    {868.256f, -392.363f, 61.4803f, 0.732738f},
    {867.804f, -392.51f, 61.5089f, 2.30167f},
    {867.612f, -392.371f, 61.524f, 2.86149f},
    {858.593f, -439.614f, 50.2184f, 0.872665f},
    {851.471f, -362.52f, 47.314f, 4.06662f},
    {846.939f, -347.279f, 66.2876f, 0.942478f},
    {842.08f, -421.775f, 48.2659f, 1.0821f},
    {838.358f, -371.212f, 63.3299f, 4.04916f},
    {827.57f, -417.483f, 48.4538f, 1.49237f},
    {827.012f, -457.397f, 48.9331f, 2.35619f},
    {825.535f, -322.373f, 63.9357f, 4.76475f},
    {867.635f, -443.605f, 51.3347f, 1.38626f},
    {957.293f, -455.039f, 56.7395f, 5.79449f},
    {950.077f, -326.672f, 61.6552f, 5.48033f},
    {936.692f, -356.78f, 65.9835f, 2.75762f},
    {926.475f, -419.345f, 56.1833f, 2.0944f},
    {924.729f, -397.453f, 60.213f, 2.71695f},
    {902.195f, -475.891f, 58.312f, 1.39626f},
    {897.464f, -338.758f, 68.1715f, 2.94961f},
    {884.237f, -407.597f, 61.566f, 0.820305f},
    {882.517f, -344.111f, 66.7887f, 3.46962f},
    {881.437f, -400.254f, 61.2028f, 0.263427f},
    {880.156f, -400.678f, 61.3113f, 3.41373f},
    {877.989f, -418.051f, 52.9753f, 4.46804f},
    {871.212f, -404.12f, 62.1433f, 3.6554f},
    {871.036f, -404.119f, 62.2237f, 4.50295f},
    {857.396f, -395.766f, 61.263f, 4.78684f},
    {857.276f, -395.395f, 61.2418f, 0.0845553f},
    {857.231f, -394.577f, 61.2174f, 1.96817f},
    {857.108f, -395.682f, 61.2317f, 4.87022f},
    {856.709f, -395.28f, 61.1814f, 2.54913f},
    {850.922f, -390.399f, 60.8771f, 2.85405f},
    {847.556f, -388.228f, 60.9438f, 2.56872f},
    {842.031f, -384.663f, 61.6028f, 2.56871f},
    {832.035f, -389.301f, 47.5567f, 2.11185f},
    {827.415f, -419.468f, 48.3322f, 1.49232f},
    {826.402f, -349.454f, 47.2722f, 1.51844f},
    {817.83f, -455.715f, 48.4207f, 0.925025f},
    {808.953f, -325.964f, 52.4043f, 3.01942f},
    // Morloch
    {865.554f, -438.735f, 50.7333f, 2.12431f},
    //coldtooth mine
    //miner/digger
    {-917.648f, -46.8922f, 77.0872f, 5.27089f},
    {-912.689f, -45.4494f, 76.2277f, 4.60767f},
    {-905.455f, -84.5179f, 75.3642f, 3.29867f},
    {-904.332f, -111.509f, 75.5925f, 2.47837f},
    {-904.27f, -160.419f, 61.9876f, 3.61192f},
    {-904.023f, -90.4558f, 75.3706f, 3.40339f},
    {-978.678f, -37.3136f, 75.8364f, 2.84489f},
    {-973.076f, -36.5013f, 77.5047f, 1.0821f},
    {-963.951f, -87.734f, 81.5555f, 0.575959f},
    {-961.941f, -90.7252f, 81.6629f, 0.820305f},
    {-957.623f, -186.582f, 66.6021f, 1.95477f},
    {-952.476f, -179.778f, 78.6771f, 4.5204f},
    {-950.427f, -115.007f, 79.6127f, 3.68264f},
    {-950.25f, -151.95f, 79.4598f, -1.81423f},
    {-950.169f, -188.099f, 66.6184f, 5.55015f},
    {-949.944f, -142.977f, 80.5382f, 2.70526f},
    {-947.854f, -170.5f, 79.7618f, 0.942478f},
    {-946.738f, -139.567f, 80.0904f, 2.3911f},
    {-945.503f, -65.0654f, 79.7907f, 5.02655f},
    {-943.678f, -110.986f, 80.2557f, 0.959931f},
    {-942.993f, -56.9881f, 79.8915f, 5.65487f},
    {-938.197f, -155.838f, 61.3111f, 1.65806f},
    {-930.488f, -214.524f, 72.1431f, 2.1236f},
    {-929.947f, -154.449f, 61.5084f, 1.67552f},
    {-927.412f, -135.313f, 61.1987f, 3.29867f},
    {-920.677f, -156.859f, 62.8033f, 3.15306f},
    {-916.75f, -136.094f, 62.2357f, 0.0698132f},
    {-915.319f, -132.718f, 62.562f, 1.16984f},
    {-913.589f, -146.794f, 76.9366f, 1.8675f},
    {-907.572f, -148.937f, 76.6898f, 4.76475f},
    {-902.02f, -64.6174f, 73.9707f, 1.19169f},
    {-899.489f, -61.7252f, 73.2498f, 5.09636f},
    {-894.792f, -127.141f, 75.3834f, 6.14356f},
    {-892.408f, -162.525f, 64.1212f, 2.69884f},
    {-892.326f, -123.158f, 76.0318f, 5.5676f},
    {-888.468f, -148.462f, 61.8012f, 1.65806f},
    {-883.268f, -159.738f, 63.5311f, 5.20108f},
    {-877.76f, -118.07f, 65.215f, 2.94961f},
    {-876.792f, -128.646f, 64.1045f, 3.40339f},
    {-874.901f, -36.6579f, 69.4246f, 2.00713f},
    {-874.856f, -151.351f, 62.7537f, 3.57875f},
    {-872.135f, -150.08f, 62.7513f, 3.57201f},
    {-870.288f, -149.217f, 62.5413f, 3.56624f},
    {-870.03f, -6.27443f, 70.3867f, 2.3911f},
    {-869.023f, -82.2118f, 69.5848f, 3.22886f},
    {-866.354f, -40.2455f, 70.842f, 0.0698132f},
    {-865.305f, -152.302f, 63.5044f, 4.86947f},
    {-861.926f, -79.0519f, 71.4178f, 0.20944f},
    {-857.292f, -152.277f, 63.2114f, 4.18879f},
    {-853.357f, -0.696194f, 72.0655f, 0.994838f},
    {-850.685f, -14.2596f, 70.2298f, 0.20944f},
    {-839.987f, -67.7695f, 72.7916f, 4.93928f},
    {-839.199f, -57.0558f, 73.4891f, 1.67552f},
    {-836.963f, -153.224f, 63.3821f, 4.46804f},
    {-832.721f, -67.7555f, 72.9062f, 4.99164f},
    {-821.496f, -143.095f, 63.1292f, 0.541052f},
    {-818.829f, -153.004f, 62.1757f, 6.12611f},
    //special
    {-954.622f, -110.958f, 80.7911f, 6.24828f},
    {-951.477f, -53.9647f, 80.0235f, 5.32325f},
    {-946.812f, -126.04f, 78.8601f, 5.15265f},
    {-940.689f, -140.707f, 79.9225f, 2.79253f},
    {-933.954f, -159.632f, 60.778f, 2.56563f},
    {-922.537f, -130.291f, 61.3756f, 4.95674f},
    {-915.862f, -151.74f, 76.9427f, 0.942478f},
    {-888.321f, -159.831f, 62.5303f, 1.20428f},
    {-874.361f, -42.4751f, 69.4316f, 0.785398f},
    {-873.19f, -50.4899f, 70.0568f, -2.41288f},
    {-868.511f, -148.386f, 62.3547f, 3.57875f},
    {-868.44f, -121.649f, 64.5056f, 3.33358f},
    {-868.324f, -77.7196f, 71.4768f, 5.41052f},
    {-859.846f, -19.6549f, 70.7304f, 1.97222f},
    {-828.05f, -150.508f, 62.2019f, 2.14675f},
    {-826.254f, -58.6911f, 72.0041f, 3.68264f},
    {-976.086f, -44.1775f, 76.029f, 1.46608f},
    {-971.864f, -87.4223f, 81.4954f, 5.8294f},
    {-966.551f, -74.1111f, 80.0243f, 4.2129f},
    {-958.509f, -173.652f, 77.9013f, 6.24828f},
    {-951.511f, -181.242f, 65.529f, 4.39823f},
    {-940.967f, -186.243f, 77.698f, 1.28164f},
    {-930.004f, -65.0898f, 79.077f, 0.0581657f},
    {-920.864f, -40.2009f, 78.256f, 5.16617f},
    {-919.089f, -148.021f, 62.0317f, 2.59327f},
    {-901.516f, -116.329f, 75.6876f, 0.471239f},
    {-897.864f, -84.4348f, 74.083f, 3.00197f},
    {-897.617f, -52.0457f, 71.9503f, 4.36332f},
    {-894.891f, -153.951f, 61.6827f, 3.23569f},
    {-893.933f, -111.625f, 75.6591f, 4.22536f},
    {-883.265f, -152.854f, 61.8384f, 0.0941087f},
    {-868.293f, -147.243f, 62.1097f, 3.2056f},
    {-867.501f, -11.8709f, 70.018f, 6.14356f},
    {-866.699f, -147.54f, 62.1646f, 3.57878f},
    {-866.566f, -91.1916f, 67.4414f, 4.56707f},
    {-857.272f, -141.142f, 61.7356f, 4.17134f},
    {-847.446f, -98.0061f, 68.5131f, 3.24631f},
    {-837.026f, -140.729f, 62.5141f, 5.51524f},
    {-824.204f, -65.053f, 72.3381f, 3.01942f},
    //vermin (s.th special for this mine)
    {-951.955f, -197.5f, 77.212f, 5.63741f},
    {-944.837f, -199.608f, 77.0737f, 4.97419f},
    {-933.494f, -209.063f, 73.7803f, 5.88176f},
    {-929.666f, -201.308f, 73.7032f, 5.02655f},
    {-978.997f, -249.356f, 65.4345f, 5.05464f},
    {-974.565f, -224.828f, 69.5858f, 4.88846f},
    {-946.514f, -259.239f, 66.0874f, 3.78132f},
    {-918.402f, -250.439f, 69.5271f, 2.21352f},
    {-910.14f, -229.959f, 72.9279f, 0.27677f},
    {-851.563f, -88.6527f, 68.5983f, 3.61896f},
    //boss
    {-848.902f, -92.931f, 68.6325f, 3.33350f},
    //herald
    {-48.459f, -288.802f, 55.47f, 1.0f},
    //triggers
    {637.083f, -32.6603f, 45.9715f, 1.14353f},         //firstaid_station
    {669.007f, -294.078f, 30.2909f, 2.77507f},     //stormpike_grave
    {77.8013f, -404.7f, 46.7549f, -0.872665f},     //stoneheart_grave
    {-202.581f, -112.73f, 78.4876f, -0.715585f},   //snowfall_grave
    {-611.962f, -396.17f, 60.8351f, 2.53682f},     //iceblood_grave
    {-1082.45f, -346.823f, 54.9219f, -1.53589f},   //frostwolf_grave
    {-1402.21f, -307.431f, 89.4424f, 0.191986f},   //frostwolf_hut
    {553.779f, -78.6566f, 51.9378f, -1.22173f},    //dunbaldar_south
    {674.001f, -143.125f, 63.6615f, 0.994838f},    //dunbaldar_north
    {203.281f, -360.366f, 56.3869f, -0.925024f},    //icewing_bunker
    {-152.437f, -441.758f, 40.3982f, -1.95477f},   //stoneheart_bunker
    {-571.88f, -262.777f, 75.0087f, -0.802851f},   //iceblood_tower
    {-768.907f, -363.71f, 90.8949f, 1.07991f},     //tower_point
    {-1302.9f, -316.981f, 113.867f, 2.00713f},     //frostwolf_etower
    {-1297.5f, -266.767f, 114.15f, 3.31044f},      //frostwolf_wtower
    {-57.7891f, -286.597f, 15.6479f, 6.02139f},    //AV_NPC_A_CAPTAIN balinda
    {722.43f, -10.9982f, 50.7046f, 3.42085f},      //AV_NPC_A_BOSS vanndar
    {-545.23f, -165.35f, 57.7886f, 3.01145f},      //AV_NPC_H_CAPTAIN galvangar
    {-1370.9f, -219.793f, 98.4258f, 5.04381f}      //AV_NPC_H_BOSS drek thar
};

/**
 * @enum BG_AV_CreatureIds
 * @brief 生物ID枚举
 *
 * 定义了奥特兰克山谷战场中使用的生物类型索引
 * 用于生成和管理战场中的各种NPC
 */
enum BG_AV_CreatureIds
{
    // 联盟墓地防御者（Alliance Graveyard Defenders）
    AV_NPC_A_GRAVEDEFENSE0 = 0,     ///< 雷矛防御者（Stormpike Defender）
    AV_NPC_A_GRAVEDEFENSE1 = 1,     ///< 老练的防御者（Seasoned Defender）
    AV_NPC_A_GRAVEDEFENSE2 = 2,     ///< 资深防御者（Veteran Defender）
    AV_NPC_A_GRAVEDEFENSE3 = 3,     ///< 勇士防御者（Champion Defender）
    AV_NPC_A_TOWERDEFENSE  = 4,     ///< 雷矛弓箭手（Stormpike Bowman）
    AV_NPC_A_CAPTAIN       = 5,     ///< 巴琳达（Balinda）- 联盟队长
    AV_NPC_A_BOSS          = 6,     ///< 范达尔（Vanndar）- 联盟将军

    // 部落墓地防御者（Horde Graveyard Defenders）
    AV_NPC_H_GRAVEDEFENSE0 = 7,     ///< 霜狼守卫者（Frostwolf Guardian）
    AV_NPC_H_GRAVEDEFENSE1 = 8,     ///< 老练的守卫者（Seasoned Guardian）
    AV_NPC_H_GRAVEDEFENSE2 = 9,     ///< 资深守卫者（Veteran Guardian）
    AV_NPC_H_GRAVEDEFENSE3 = 10,    ///< 勇士守卫者（Champion Guardian）
    AV_NPC_H_TOWERDEFENSE  = 11,    ///< 霜狼弓箭手（Frostwolf Bowman）
    AV_NPC_H_CAPTAIN       = 12,    ///< 加尔范（Galvangar）- 部落队长
    AV_NPC_H_BOSS          = 13,    ///< 德雷克塔尔（Drek'Thar）- 部落将军

    // 联盟元帅（Alliance Marshals）
    AV_NPC_A_MARSHAL_SOUTH = 14,    ///< 丹巴达尔南部碉堡元帅
    AV_NPC_MARSHAL_NORTH   = 15,    ///< 丹巴达尔北部碉堡元帅
    AV_NPC_A_MARSHAL_ICE   = 16,    ///< 冰翼碉堡元帅
    AV_NPC_A_MARSHAL_STONE = 17,    ///< 石炉碉堡元帅

    // 部落督军（Horde Warmasters）
    AV_NPC_H_MARSHAL_ICE   = 18,    ///< 冰血哨塔督军
    AV_NPC_H_MARSHAL_TOWER = 19,    ///< 哨塔点督军
    AV_NPC_MARSHAL_ETOWER  = 20,    ///< 霜狼东塔督军
    AV_NPC_H_MARSHAL_WTOWER= 21,    ///< 霜狼西塔督军

    // 北部矿井 - 铁深矿井（North Mine - Irondeep）
    AV_NPC_N_MINE_N_1      = 22,    ///< 铁深矿井中立生物类型1
    AV_NPC_N_MINE_N_2      = 23,    ///< 铁深矿井中立生物类型2
    AV_NPC_N_MINE_N_3      = 24,    ///< 铁深矿井中立生物类型3
    AV_NPC_N_MINE_N_4      = 25,    ///< 铁深矿井中立生物类型4
    AV_NPC_N_MINE_A_1      = 26,    ///< 铁深矿井联盟生物类型1
    AV_NPC_N_MINE_A_2      = 27,    ///< 铁深矿井联盟生物类型2
    AV_NPC_N_MINE_A_3      = 28,    ///< 铁深矿井联盟生物类型3
    AV_NPC_N_MINE_A_4      = 29,    ///< 铁深矿井联盟生物类型4
    AV_NPC_N_MINE_H_1      = 30,    ///< 铁深矿井部落生物类型1
    AV_NPC_N_MINE_H_2      = 31,    ///< 铁深矿井部落生物类型2
    AV_NPC_N_MINE_H_3      = 32,    ///< 铁深矿井部落生物类型3
    AV_NPC_N_MINE_H_4      = 33,    ///< 铁深矿井部落生物类型4

    // 南部矿井 - 冷齿矿井（South Mine - Coldtooth）
    AV_NPC_S_MINE_N_1      = 34,    ///< 冷齿矿井中立生物类型1
    AV_NPC_S_MINE_N_2      = 35,    ///< 冷齿矿井中立生物类型2
    AV_NPC_S_MINE_N_3      = 36,    ///< 冷齿矿井中立生物类型3
    AV_NPC_S_MINE_N_4      = 37,    ///< 冷齿矿井中立生物类型4
    AV_NPC_S_MINE_N_S      = 38,    ///< 冷齿矿井中立特殊生物
    AV_NPC_S_MINE_A_1      = 39,    ///< 冷齿矿井联盟生物类型1
    AV_NPC_S_MINE_A_2      = 40,    ///< 冷齿矿井联盟生物类型2
    AV_NPC_S_MINE_A_3      = 41,    ///< 冷齿矿井联盟生物类型3
    AV_NPC_S_MINE_A_4      = 42,    ///< 冷齿矿井联盟生物类型4
    AV_NPC_S_MINE_H_1      = 43,    ///< 冷齿矿井部落生物类型1
    AV_NPC_S_MINE_H_2      = 44,    ///< 冷齿矿井部落生物类型2
    AV_NPC_S_MINE_H_3      = 45,    ///< 冷齿矿井部落生物类型3
    AV_NPC_S_MINE_H_4      = 46,    ///< 冷齿矿井部落生物类型4

    AV_NPC_HERALD          = 47,    ///< 传令官
    AV_NPC_INFO_MAX        = 48     ///< 生物信息总数
};

/**
 * @brief 生物信息数组
 *
 * 定义了奥特兰克山谷战场中各种生物的entry ID
 * 用于生成生物和处理击杀单位事件
 *
 * @todo 这个数组应该被移除，唯一需要的是entry（用于生成和处理击杀单位）
 */
const uint32 BG_AV_CreatureInfo[AV_NPC_INFO_MAX] =
{
    12050, // Stormpike Defender
    13326, // Seasoned Defender
    13331, // Veteran Defender
    13422, // Champion Defender
    13358, // Stormpike Bowman /// @todo: Confirm if this is correct. Author assumpted 60, 61 & 69, 70, but wouldn't work here
    11949, // not spawned with this data, but used for handlekillunit
    11948, // not spawned with this data, but used for handlekillunit
    12053, // Frostwolf Guardian
    13328, // Seasoned Guardian
    13332, // Veteran Guardian
    13421, // Champion Guardian
    13359, // Frostwolf Bowman
    11947, // not spawned with this data, but used for handlekillunit
    11946, // not spawned with this data, but used for handlekillunit
    14763, // Dun Baldar South Marshal
    14762, // Dun Baldar North Marshal
    14764, // Icewing Marshal
    14765, // Stonehearth Marshal

    14773, // Iceblood Warmaster
    14776, // Tower Point Warmaster
    14772, // East Frostwolf Warmaster
    14777, // West Frostwolf Warmaster

    10987, // Irondeep Trogg
    11600, // Irondeep Shaman
    11602, // Irondeep Skullthumper
    11657, // Morloch

    13396, // irondeep alliance /// @todo: Correct and give correct ids
    13080,
    13098,
    13078,

    13397, // irondeep horde
    13099,
    13081,
    13079,

    11603, // south mine neutral
    11604,
    11605,
    11677,
    10982, // vermin

    13317, // alliance
    13096, // explorer
    13087, // invader
    13086,

    13316, // horde
    13097, // surveypr
    13089, // guard
    13088,
    14848  // Herald
};

//x, y, z, o, static_creature_info-id
const float BG_AV_StaticCreaturePos[AV_STATICCPLACE_MAX][5] =
{
    //static creatures
    {-1235.31f, -340.777f, 60.5088f, 3.31613f, 0 }, //2225 - Zora Guthrek
    {-1244.02f, -323.795f, 61.0485f, 5.21853f, 1 }, //3343 - Grelkor
    {-1235.16f, -332.302f, 60.2985f, 2.96706f, 2 }, //3625 - Rarck
    {587.303f, -42.8257f, 37.5615f, 5.23599f, 3 }, //4255 - Brogus Thunderbrew
    {643.635f, -58.3987f, 41.7405f, 4.72984f, 4 }, //4257 - Lana Thunderbrew
    {591.464f, -44.452f, 37.6166f, 5.65487f, 5 }, //5134 - Jonivera Farmountain
    {608.515f, -33.3935f, 42.0003f, 5.41052f, 6 }, //5135 - Svalbrad Farmountain
    {617.656f, -32.0701f, 42.7168f, 4.06662f, 7 }, //5139 - Kurdrum Barleybeard
    {-1183.76f, -268.295f, 72.8233f, 3.28122f, 8 }, //10364 - Yaelika Farclaw
    {-1187.86f, -275.31f, 73.0481f, 3.63028f, 9 }, //10367 - Shrye Ragefist
    {-1008.42f, -368.006f, 55.3426f, 5.95647f, 10 }, //10981 - Frostwolf
    {-1091.92f, -424.28f, 53.0139f, 2.93958f, 10 }, //10981 - Frostwolf
    {-558.455f, -198.768f, 58.1755f, 4.97946f, 10 }, //10981 - Frostwolf
    {-861.247f, -312.51f, 55.1427f, 3.35382f, 10 }, //10981 - Frostwolf
    {-1003.81f, -395.913f, 50.4736f, 2.85631f, 10 }, //10981 - Frostwolf
    {-904.5f, -289.815f, 65.1222f, 5.7847f, 10 }, //10981 - Frostwolf
    {-1064.41f, -438.839f, 51.3614f, 1.88857f, 10 }, //10981 - Frostwolf
    {258.814f, 76.2017f, 18.6468f, 6.19052f, 11 }, //10986 - Snowblind Harpy
    {265.838f, -315.846f, -16.5429f, 3.15917f, 11 }, //10986 - Snowblind Harpy
    {426.485f, -51.1927f, -5.66286f, 1.60347f, 11 }, //10986 - Snowblind Harpy
    {452.044f, -33.9594f, -0.044651f, 2.72815f, 11 }, //10986 - Snowblind Harpy
    {266.032f, -315.639f, -16.5429f, 4.67962f, 11 }, //10986 - Snowblind Harpy
    {532.64f, -54.5863f, 20.7024f, 2.93215f, 11 }, //10986 - Snowblind Harpy
    {295.183f, -299.908f, -34.6123f, 0.135851f, 12 }, //10990 - Alterac Ram
    {421.08f, -225.006f, -23.73f, 0.166754f, 12 }, //10990 - Alterac Ram
    {-55.7766f, -192.498f, 20.4352f, 6.12221f, 12 }, //10990 - Alterac Ram
    {527.887f, -477.223f, 62.3559f, 0.170935f, 12 }, //10990 - Alterac Ram
    {389.144f, -346.508f, -30.334f, 4.14117f, 12 }, //10990 - Alterac Ram
    {108.121f, -322.248f, 37.5655f, 4.46788f, 12 }, //10990 - Alterac Ram
    {507.479f, -67.9403f, 10.3571f, 3.26304f, 12 }, //10990 - Alterac Ram
    {329.071f, -185.016f, -29.1542f, 0.356943f, 12 }, //10990 - Alterac Ram
    {252.449f, -422.313f, 35.1404f, 4.53771f, 12 }, //10990 - Alterac Ram
    {358.882f, -118.061f, -24.9119f, 2.29257f, 12 }, //10990 - Alterac Ram
    {487.151f, -174.229f, 14.7558f, 4.73192f, 12 }, //10990 - Alterac Ram
    {449.652f, -123.561f, 6.14273f, 6.12029f, 12 }, //10990 - Alterac Ram
    {272.419f, -261.802f, -41.8835f, 3.66559f, 12 }, //10990 - Alterac Ram
    {359.021f, -210.954f, -29.3483f, 4.31339f, 12 }, //10990 - Alterac Ram
    {450.598f, -318.048f, -37.7548f, 0.655219f, 12 }, //10990 - Alterac Ram
    {509.333f, -218.2f, 3.05439f, 3.66292f, 12 }, //10990 - Alterac Ram
    {485.771f, -223.613f, -1.53f, 2.04862f, 12 }, //10990 - Alterac Ram
    {486.636f, -452.172f, 39.6592f, 2.3341f, 12 }, //10990 - Alterac Ram
    {702.783f, -257.494f, 25.9777f, 1.68329f, 12 }, //10990 - Alterac Ram
    {460.942f, -199.263f, -6.0149f, 0.380506f, 12 }, //10990 - Alterac Ram
    {483.108f, -115.307f, 10.1056f, 3.69701f, 12 }, //10990 - Alterac Ram
    {471.601f, -154.174f, 14.0702f, 5.5807f, 12 }, //10990 - Alterac Ram
    {213.938f, -420.793f, 41.2549f, 5.71394f, 12 }, //10990 - Alterac Ram
    {289.387f, -294.685f, -33.9073f, 0.555494f, 12 }, //10990 - Alterac Ram
    {155.649f, -402.891f, 43.3915f, 5.94838f, 12 }, //10990 - Alterac Ram
    {517.184f, -295.105f, -9.78195f, 6.05668f, 12 }, //10990 - Alterac Ram
    {102.334f, -332.165f, 38.9812f, 3.31445f, 12 }, //10990 - Alterac Ram
    {320.244f, -107.793f, -42.6357f, -1.00311f, 12 }, //10990 - Alterac Ram
    {217.976f, 110.774f, 15.7603f, 4.56793f, 13 }, //11675 - Snowblind Windcaller
    {269.872f, 6.66684f, 20.7592f, 0.381212f, 13 }, //11675 - Snowblind Windcaller
    {313.528f, -319.041f, -27.2373f, 0.554098f, 13 }, //11675 - Snowblind Windcaller
    {435.441f, -39.9289f, -0.169651f, 0.549454f, 13 }, //11675 - Snowblind Windcaller
    {315.115f, -317.62f, -29.1123f, 0.90111f, 13 }, //11675 - Snowblind Windcaller
    {428.091f, -122.731f, 3.40332f, 6.05901f, 14 }, //11678 - Snowblind Ambusher
    {235.05f, 85.5705f, 18.3079f, -0.914255f, 14 }, //11678 - Snowblind Ambusher
    {-1553.04f, -344.342f, 64.4163f, 6.09933f, 15 }, //11839 - Wildpaw Brute
    {-545.23f, -165.35f, 57.7886f, 3.01145f, 16 }, //11947 - Captain Galvangar
    {722.43f, -10.9982f, 50.7046f, 3.42085f, 17 }, //11948 - Vanndar Stormpike
    {-57.7891f, -286.597f, 15.6479f, 6.02139f, 18 }, //11949 - Captain Balinda Stonehearth
    {930.498f, -520.755f, 93.7334f, 1.8326f, 19 }, //11997 - Stormpike Herald
    {-776.092f, -345.161f, 67.4092f, 1.89257f, 20 }, //12051 - Frostwolf Legionnaire
    {-1224.63f, -308.144f, 65.0087f, 4.01139f, 20 }, //12051 - Frostwolf Legionnaire
    {-713.039f, -442.515f, 82.8638f, 0.68724f, 20 }, //12051 - Frostwolf Legionnaire
    {-711.783f, -444.061f, 82.7039f, 0.683494f, 20 }, //12051 - Frostwolf Legionnaire
    {587.633f, -45.9816f, 37.5438f, 5.81195f, 21 }, //12096 - Stormpike Quartermaster
    {-1293.79f, -194.407f, 72.4398f, 5.84685f, 22 }, //12097 - Frostwolf Quartermaster
    {446.163f, -377.119f, -1.12725f, 0.209526f, 23 }, //12127 - Stormpike Guardsman
    {549.348f, -399.254f, 53.3537f, 3.24729f, 23 }, //12127 - Stormpike Guardsman
    {549.801f, -401.217f, 53.8305f, 3.24729f, 23 }, //12127 - Stormpike Guardsman
    {192.704f, -406.874f, 42.9183f, 6.10696f, 23 }, //12127 - Stormpike Guardsman
    {441.305f, -435.765f, 28.2385f, 2.14472f, 23 }, //12127 - Stormpike Guardsman
    {192.982f, -404.891f, 43.0132f, 6.1061f, 23 }, //12127 - Stormpike Guardsman
    {355.342f, -391.989f, -0.486707f, 3.00643f, 23 }, //12127 - Stormpike Guardsman
    {446.035f, -375.104f, -1.12725f, 0.21033f, 23 }, //12127 - Stormpike Guardsman
    {697.864f, -433.238f, 62.7914f, 1.65776f, 23 }, //12127 - Stormpike Guardsman
    {610.74f, -331.585f, 30.8021f, 5.14253f, 23 }, //12127 - Stormpike Guardsman
    {609.815f, -329.775f, 30.9271f, -2.38829f, 23 }, //12127 - Stormpike Guardsman
    {695.874f, -433.434f, 62.8543f, 1.65776f, 23 }, //12127 - Stormpike Guardsman
    {443.337f, -435.283f, 28.6842f, 2.13768f, 23 }, //12127 - Stormpike Guardsman
    {-1251.5f, -316.327f, 62.6565f, 5.02655f, 24 }, //13176 - Smith Regzar
    {-1332.0f, -331.243f, 91.2631f, 1.50098f, 25 }, //13179 - Wing Commander Guse
    {569.983f, -94.9992f, 38.0325f, 1.39626f, 26 }, //13216 - Gaelden Hammersmith
    {-1244.92f, -308.916f, 63.2525f, 1.62316f, 27 }, //13218 - Grunnda Wolfheart
    {-1319.56f, -342.675f, 60.3404f, 1.20428f, 28 }, //13236 - Primalist Thurloga
    {647.61f, -61.1548f, 41.7405f, 4.24115f, 29 }, //13257 - Murgot Deepforge
    {-1321.64f, -343.73f, 60.4833f, 1.01229f, 30 }, //13284 - Frostwolf Shaman
    {-1317.61f, -342.853f, 60.3726f, 2.47837f, 30 }, //13284 - Frostwolf Shaman
    {-1319.31f, -344.475f, 60.3825f, 1.72788f, 30 }, //13284 - Frostwolf Shaman
    {569.963f, -42.0218f, 37.7581f, 4.27606f, 31 }, //13438 - Wing Commander Slidore
    {729.2f, -78.812f, 51.6335f, 3.97935f, 32 }, //13442 - Arch Druid Renferal
    {729.118f, -82.8713f, 51.6335f, 2.53073f, 33 }, //13443 - Druid of the Grove
    {725.554f, -79.4973f, 51.6335f, 5.27089f, 33 }, //13443 - Druid of the Grove
    {724.768f, -84.1642f, 51.6335f, 0.733038f, 33 }, //13443 - Druid of the Grove
    {596.68f, -83.0633f, 39.0051f, 6.24828f, 34 }, //13447 - Corporal Noreg Stormpike
    {600.032f, -2.92475f, 42.0788f, 5.00909f, 35 }, //13577 - Stormpike Ram Rider Commander
    {610.239f, -21.8454f, 43.272f, 4.90438f, 36 }, //13617 - Stormpike Stable Master
    {613.422f, -150.764f, 33.4517f, 5.55015f, 37 }, //13797 - Mountaineer Boombellow
    {-1213.91f, -370.619f, 56.4455f, 0.837758f, 38 }, //13798 - Jotek
    {704.35f, -22.9071f, 50.2187f, 0.785398f, 39 }, //13816 - Prospector Stonehewer
    {-1271.24f, -335.766f, 62.3971f, 5.75959f, 40 }, //14185 - Najak Hexxen
    {-1268.64f, -332.688f, 62.6171f, 5.28835f, 41 }, //14186 - Ravak Grimtotem
    {648.363f, -65.2233f, 41.7405f, 3.12414f, 42 }, //14187 - Athramanis
    {648.238f, -67.8931f, 41.7405f, 2.60054f, 43 }, //14188 - Dirk Swindle
    {-1223.44f, -309.833f, 64.9331f, 4.0131f, 44 }, //14282 - Frostwolf Bloodhound
    {-1226.4f, -307.136f, 64.9706f, 4.0145f, 44 }, //14282 - Frostwolf Bloodhound
    {356.001f, -389.969f, -0.438796f, 3.0334f, 45 }, //14283 - Stormpike Owl
    {355.835f, -394.005f, -0.60149f, 3.02498f, 45 }, //14283 - Stormpike Owl
    {882.266f, -496.378f, 96.7707f, 4.83248f, 45 }, //14283 - Stormpike Owl
    {878.649f, -495.917f, 96.6171f, 4.67693f, 45 }, //14283 - Stormpike Owl
    {932.851f, -511.017f, 93.6748f, 3.61004f, 45 }, //14283 - Stormpike Owl
    {935.806f, -513.983f, 93.7436f, 3.61788f, 45 }, //14283 - Stormpike Owl
    {947.412f, -509.982f, 95.1098f, 2.82743f, 46 }, //14284 - Stormpike Battleguard
    {934.557f, -512.395f, 93.662f, 3.61004f, 46 }, //14284 - Stormpike Battleguard
    {939.42f, -502.777f, 94.5887f, 5.14872f, 46 }, //14284 - Stormpike Battleguard
    {854.276f, -494.241f, 96.8017f, 5.44543f, 46 }, //14284 - Stormpike Battleguard
    {776.621f, -487.775f, 99.4049f, 3.50811f, 46 }, //14284 - Stormpike Battleguard
    {880.169f, -495.699f, 96.6204f, 4.8325f, 46 }, //14284 - Stormpike Battleguard
    {773.651f, -497.482f, 99.0408f, 2.11185f, 46 }, //14284 - Stormpike Battleguard
    {949.1f, -506.913f, 95.4237f, 3.31613f, 46 }, //14284 - Stormpike Battleguard
    {-1370.9f, -219.793f, 98.4258f, 5.04381f, 47}, //drek thar
};

const uint32 BG_AV_StaticCreatureInfo[51] =
{
    2225,  // Zora Guthrek
    3343,  // Grelkor
    3625,  // Rarck
    4255,  // Brogus Thunderbrew
    4257,  // Lana Thunderbrew
    5134,  // Jonivera Farmountain
    5135,  // Svalbrad Farmountain
    5139,  // Kurdrum Barleybeard
    10364, // Yaelika Farclaw
    10367, // Shrye Ragefist
    10981, // Frostwolf
    10986, // Snowblind Harpy
    10990, // Alterac Ram
    11675, // Snowblind Windcaller
    11678, // Snowblind Ambusher
    11839, // Wildpaw Brute
    11947, // Captain Galvangar
    11948, // Vanndar Stormpike
    11949, // Captain Balinda Stonehearth
    11997, // Stormpike Herald
    12051, // Frostwolf Legionnaire
    12096, // Stormpike Quartermaster
    12097, // Frostwolf Quartermaster
    12127, // Stormpike Guardsman
    13176, // Smith Regzar
    13179, // Wing Commander Guse
    13216, // Gaelden Hammersmith
    13218, // Grunnda Wolfheart
    13236, // Primalist Thurloga
    13257, // Murgot Deepforge
    13284, // Frostwolf Shaman
    13438, // Wing Commander Slidore
    13442, // Arch Druid Renferal
    13443, // Druid of the Grove
    13447, // Corporal Noreg Stormpike
    13577, // Stormpike Ram Rider Commander
    13617, // Stormpike Stable Master
    13797, // Mountaineer Boombellow
    13798, // Jotek
    13816, // Prospector Stonehewer
    14185, // Najak Hexxen
    14186, // Ravak Grimtotem
    14187, // Athramanis
    14188, // Dirk Swindle
    14282, // Frostwolf Bloodhound
    14283, // Stormpike Owl
    14284, // Stormpike Battleguard
    11946, // Drek'Thar
    11948, // Vanndar Stormpike
    11947, // Captain Galvangar
    11949, // Captain Balinda Stonehearth
};

/**
 * @enum BG_AV_Graveyards
 * @brief 墓地ID枚举
 *
 * 定义了奥特兰克山谷战场中所有墓地的世界状态ID
 * 这些ID用于管理墓地的归属和控制状态
 */
enum BG_AV_Graveyards
{
    AV_GRAVE_STORM_AID         = 751,  ///< 急救站墓地
    AV_GRAVE_STORM_GRAVE       = 689,  ///< 雷矛墓地
    AV_GRAVE_STONE_GRAVE       = 729,  ///< 石炉墓地
    AV_GRAVE_SNOWFALL          = 169,  ///< 雪落墓地
    AV_GRAVE_ICE_GRAVE         = 749,  ///< 冰血墓地
    AV_GRAVE_FROSTWOLF         = 690,  ///< 霜狼墓地
    AV_GRAVE_FROST_HUT         = 750,  ///< 霜狼小屋墓地
    AV_GRAVE_MAIN_ALLIANCE     = 611,  ///< 联盟主基地墓地
    AV_GRAVE_MAIN_HORDE        = 610   ///< 部落主基地墓地
};

/**
 * @brief 墓地ID数组
 *
 * 包含所有墓地的ID，用于快速访问和遍历
 */
const uint32 BG_AV_GraveyardIds[9]=
{
    AV_GRAVE_STORM_AID,
    AV_GRAVE_STORM_GRAVE,
    AV_GRAVE_STONE_GRAVE,
    AV_GRAVE_SNOWFALL,
    AV_GRAVE_ICE_GRAVE,
    AV_GRAVE_FROSTWOLF,
    AV_GRAVE_FROST_HUT,
    AV_GRAVE_MAIN_ALLIANCE,
    AV_GRAVE_MAIN_HORDE
};

/**
 * @enum BG_AV_BUFF
 * @brief 增益效果枚举
 *
 * 定义了奥特兰克山谷战场中使用的各种增益效果
 *
 * @todo 添加其他所有增益效果
 */
enum BG_AV_BUFF
{
    AV_BUFF_ARMOR = 21163,      ///< 护甲增益
    AV_BUFF_A_CAPTAIN = 23693,  ///< 联盟队长增益
    AV_BUFF_H_CAPTAIN = 22751   ///< 部落队长增益
};

/**
 * @enum BG_AV_States
 * @brief 节点状态枚举
 *
 * 定义了奥特兰克山谷战场中节点（墓地、塔楼）的可能状态
 */
enum BG_AV_States
{
    POINT_NEUTRAL              =  0,  ///< 中立状态
    POINT_ASSAULTED            =  1,  ///< 被进攻状态
    POINT_DESTROYED            =  2,  ///< 被摧毁状态（仅适用于塔楼）
    POINT_CONTROLED            =  3   ///< 被控制状态
};

/**
 * @enum BG_AV_WorldStates
 * @brief 世界状态枚举
 *
 * 定义了奥特兰克山谷战场中使用的各种世界状态ID
 * 用于向客户端同步战场状态信息（分数、节点控制等）
 */
enum BG_AV_WorldStates
{
    AV_Alliance_Score               = 3127,  ///< 联盟分数
    AV_Horde_Score                  = 3128,  ///< 部落分数
    AV_SHOW_H_SCORE                 = 3133,  ///< 显示部落分数
    AV_SHOW_A_SCORE                 = 3134,  ///< 显示联盟分数

    /*
    // 状态后面的注释显示哪个图标覆盖另一个，但到目前为止未使用，可能不是一个好的解决方案（但能提供一些性能）

    // 墓地（Graves）

    // 联盟（Alliance）
    // 急救站（Stormpike first aid station）
    AV_AID_A_C                      = 1325,  ///< 急救站联盟控制
    AV_AID_A_A                      = 1326,  ///< 急救站联盟进攻
    AV_AID_H_C                      = 1327,  ///< 急救站部落控制
    AV_AID_H_A                      = 1328,  ///< 急救站部落进攻
    // 雷矛墓地（Stormpike Graveyard）
    AV_PIKEGRAVE_A_C                = 1333,  ///< 雷矛墓地联盟控制
    AV_PIKEGRAVE_A_A                = 1335,  ///< 雷矛墓地联盟进攻
    AV_PIKEGRAVE_H_C                = 1334,  ///< 雷矛墓地部落控制
    AV_PIKEGRAVE_H_A                = 1336,  ///< 雷矛墓地部落进攻
    // 石炉墓地（Stoneheart Grave）
    AV_STONEHEART_A_C               = 1302,  ///< 石炉墓地联盟控制
    AV_STONEHEART_A_A               = 1304,  ///< 石炉墓地联盟进攻（覆盖部落控制）
    AV_STONEHEART_H_C               = 1301,  ///< 石炉墓地部落控制（覆盖联盟控制）
    AV_STONEHEART_H_A               = 1303,  ///< 石炉墓地部落进攻（覆盖联盟进攻）
    // 中立（Neutral）
    // 雪落墓地（Snowfall Grave）
    */
    AV_SNOWFALL_N                   = 1966   ///< 雪落墓地中立状态（覆盖联盟进攻）
    /*
    AV_SNOWFALL_A_C                 = 1341,  ///< 雪落墓地联盟控制（覆盖部落控制）
    AV_SNOWFALL_A_A                 = 1343,  ///< 雪落墓地联盟进攻（覆盖部落进攻）
    AV_SNOWFALL_H_C                 = 1342,  ///< 雪落墓地部落控制
    AV_SNOWFALL_H_A                 = 1344,  ///< 雪落墓地部落进攻（覆盖联盟控制）
    // 部落（Horde）
    // 冰血墓地（Iceblood grave）
    AV_ICEBLOOD_A_C                 = 1346,  ///< 冰血墓地联盟控制（覆盖部落控制）
    AV_ICEBLOOD_A_A                 = 1348,  ///< 冰血墓地联盟进攻（覆盖联盟控制）
    AV_ICEBLOOD_H_C                 = 1347,  ///< 冰血墓地部落控制
    AV_ICEBLOOD_H_A                 = 1349,  ///< 冰血墓地部落进攻（覆盖联盟进攻）
    // 霜狼墓地（Frostwolf Grave）
    AV_FROSTWOLF_A_C                = 1337,  ///< 霜狼墓地联盟控制（覆盖部落控制）
    AV_FROSTWOLF_A_A                = 1339,  ///< 霜狼墓地联盟进攻（覆盖联盟控制）
    AV_FROSTWOLF_H_C                = 1338,  ///< 霜狼墓地部落控制
    AV_FROSTWOLF_H_A                = 1340,  ///< 霜狼墓地部落进攻（覆盖联盟进攻）
    // 霜狼小屋（Frostwolf Hut）
    AV_FROSTWOLFHUT_A_C             = 1329,  ///< 霜狼小屋联盟控制（覆盖部落控制）
    AV_FROSTWOLFHUT_A_A             = 1331,  ///< 霜狼小屋联盟进攻（覆盖部落进攻）
    AV_FROSTWOLFHUT_H_C             = 1330,  ///< 霜狼小屋部落控制
    AV_FROSTWOLFHUT_H_A             = 1332,  ///< 霜狼小屋部落进攻（覆盖联盟控制）

    // 塔楼（Towers）
    // 联盟（Alliance）
    // 丹巴达尔南部碉堡（Dunbaldar South Bunker）
    AV_DUNS_CONTROLLED              = 1361,  ///< 丹巴达尔南部碉堡控制
    AV_DUNS_DESTROYED               = 1370,  ///< 丹巴达尔南部碉堡摧毁
    AV_DUNS_ASSAULTED               = 1378,  ///< 丹巴达尔南部碉堡进攻
    // 丹巴达尔北部碉堡（Dunbaldar North Bunker）
    AV_DUNN_CONTROLLED              = 1362,  ///< 丹巴达尔北部碉堡控制
    AV_DUNN_DESTROYED               = 1371,  ///< 丹巴达尔北部碉堡摧毁
    AV_DUNN_ASSAULTED               = 1379,  ///< 丹巴达尔北部碉堡进攻
    // 冰翼碉堡（Icewing Bunker）
    AV_ICEWING_CONTROLLED           = 1363,  ///< 冰翼碉堡控制
    AV_ICEWING_DESTROYED            = 1372,  ///< 冰翼碉堡摧毁
    AV_ICEWING_ASSAULTED            = 1380,  ///< 冰翼碉堡进攻
    // 石炉碉堡（Stoneheart Bunker）
    AV_STONEH_CONTROLLED            = 1364,  ///< 石炉碉堡控制
    AV_STONEH_DESTROYED             = 1373,  ///< 石炉碉堡摧毁
    AV_STONEH_ASSAULTED             = 1381,  ///< 石炉碉堡进攻
    // 部落（Horde）
    // 冰血哨塔（Iceblood Tower）
    AV_ICEBLOOD_CONTROLLED          = 1385,  ///< 冰血哨塔控制
    AV_ICEBLOOD_DESTROYED           = 1368,  ///< 冰血哨塔摧毁
    AV_ICEBLOOD_ASSAULTED           = 1390,  ///< 冰血哨塔进攻
    // 哨塔点（Tower Point）
    AV_TOWERPOINT_CONTROLLED        = 1384,  ///< 哨塔点控制
    AV_TOWERPOINT_DESTROYED         = 1367,  ///< 哨塔点摧毁（覆盖控制）
    AV_TOWERPOINT_ASSAULTED         = 1389,  ///< 哨塔点进攻（覆盖摧毁）
    // 霜狼西塔（Frostwolf West）
    AV_FROSTWOLFW_CONTROLLED        = 1382,  ///< 霜狼西塔控制
    AV_FROSTWOLFW_DESTROYED         = 1365,  ///< 霜狼西塔摧毁（覆盖控制）
    AV_FROSTWOLFW_ASSAULTED         = 1387,  ///< 霜狼西塔进攻（覆盖摧毁）
    // 霜狼东塔（Frostwolf East）
    AV_FROSTWOLFE_CONTROLLED        = 1383,  ///< 霜狼东塔控制
    AV_FROSTWOLFE_DESTROYED         = 1366,  ///< 霜狼东塔摧毁
    AV_FROSTWOLFE_ASSAULTED         = 1388,  ///< 霜狼东塔进攻

    // 矿井（mines）
    AV_N_MINE_N              = 1360,  ///< 北部矿井中立
    AV_N_MINE_A              = 1358,  ///< 北部矿井联盟
    AV_N_MINE_H              = 1359,  ///< 北部矿井部落

    AV_S_MINE_N                     = 1357,  ///< 南部矿井中立
    AV_S_MINE_A                     = 1355,  ///< 南部矿井联盟
    AV_S_MINE_H                     = 1356,  ///< 南部矿井部落

    // 被己方队伍进攻的塔楼（未使用）（towers assaulted by own team - unused）
    AV_STONEH_UNUSED                = 1377,  ///< 石炉碉堡未使用
    AV_ICEWING_UNUSED               = 1376,  ///< 冰翼碉堡未使用
    AV_DUNS_UNUSED                  = 1375,  ///< 丹巴达尔南部碉堡未使用
    AV_DUNN_UNUSED                  = 1374,  ///< 丹巴达尔北部碉堡未使用

    AV_ICEBLOOD_UNUSED              = 1395,  ///< 冰血哨塔未使用
    AV_TOWERPOINT_UNUSED            = 1394,  ///< 哨塔点未使用
    AV_FROSTWOLFE_UNUSED            = 1393,  ///< 霜狼东塔未使用
    AV_FROSTWOLFW_UNUSED            = 1392   ///< 霜狼西塔未使用
    */
};

/**
 * @brief 矿井世界状态数组
 *
 * 定义了矿井的世界状态ID
 * 格式：[联盟控制, 中立控制, 部落控制]
 */
const uint32 BG_AV_MineWorldStates[2][3] =
{
    {1358, 1360, 1359},  ///< 北部矿井（铁深矿井）
    {1355, 1357, 1356}   ///< 南部矿井（冷齿矿井）
};

/**
 * @enum BG_AV_QuestIds
 * @brief 任务ID枚举
 *
 * 定义了奥特兰克山谷战场中的所有任务ID
 * 这些任务可以提供战场增益和资源
 */
enum BG_AV_QuestIds
{
    // 护甲碎片任务（Armor Scraps Quests）
    AV_QUEST_A_SCRAPS1      = 7223,  ///< 联盟护甲碎片任务1
    AV_QUEST_A_SCRAPS2      = 6781,  ///< 联盟护甲碎片任务2
    AV_QUEST_H_SCRAPS1      = 7224,  ///< 部落护甲碎片任务1
    AV_QUEST_H_SCRAPS2      = 6741,  ///< 部落护甲碎片任务2

    // 空军指挥官任务（Wing Commander Quests）
    AV_QUEST_A_COMMANDER1   = 6942,  ///< 联盟空军指挥官任务1（士兵）
    AV_QUEST_H_COMMANDER1   = 6825,  ///< 部落空军指挥官任务1
    AV_QUEST_A_COMMANDER2   = 6941,  ///< 联盟空军指挥官任务2（中尉）
    AV_QUEST_H_COMMANDER2   = 6826,  ///< 部落空军指挥官任务2
    AV_QUEST_A_COMMANDER3   = 6943,  ///< 联盟空军指挥官任务3（指挥官）
    AV_QUEST_H_COMMANDER3   = 6827,  ///< 部落空军指挥官任务3

    // 首领召唤任务（Boss Summon Quests）
    AV_QUEST_A_BOSS1        = 7386,  ///< 联盟首领任务1（5个水晶/血）
    AV_QUEST_H_BOSS1        = 7385,  ///< 部落首领任务1
    AV_QUEST_A_BOSS2        = 6881,  ///< 联盟首领任务2（1个）
    AV_QUEST_H_BOSS2        = 6801,  ///< 部落首领任务2

    // 矿井任务（Mine Quests）
    AV_QUEST_A_NEAR_MINE    = 5892,  ///< 联盟附近矿井任务（队伍起始位置附近的矿井）
    AV_QUEST_H_NEAR_MINE    = 5893,  ///< 部落附近矿井任务
    AV_QUEST_A_OTHER_MINE   = 6982,  ///< 联盟另一个矿井任务
    AV_QUEST_H_OTHER_MINE   = 6985,  ///< 部落另一个矿井任务

    // 骑兵任务（Rider Quests）
    AV_QUEST_A_RIDER_HIDE   = 7026,  ///< 联盟骑兵任务（羊皮）
    AV_QUEST_H_RIDER_HIDE   = 7002,  ///< 部落骑兵任务
    AV_QUEST_A_RIDER_TAME   = 7027,  ///< 联盟骑兵任务（驯服）
    AV_QUEST_H_RIDER_TAME   = 7001   ///< 部落骑兵任务
};

/**
 * @enum BG_AV_Objectives
 * @brief 目标类型枚举
 *
 * 定义了奥特兰克山谷战场中的目标类型
 * 用于成就和计分系统
 */
enum BG_AV_Objectives
{
    AV_OBJECTIVE_ASSAULT_TOWER      = 61,  ///< 进攻塔楼
    AV_OBJECTIVE_ASSAULT_GRAVEYARD  = 63,  ///< 进攻墓地
    AV_OBJECTIVE_DEFEND_TOWER       = 64,  ///< 防守塔楼
    AV_OBJECTIVE_DEFEND_GRAVEYARD   = 65   ///< 防守墓地
};

/**
 * @struct StaticNodeInfo
 * @brief 静态节点信息结构
 *
 * 存储每个节点的静态信息，包括文本ID和世界状态ID
 */
struct StaticNodeInfo
{
    BG_AV_Nodes NodeId;  ///< 节点ID

    /**
     * @brief 文本ID结构
     *
     * 包含节点状态变化时的广播文本ID
     */
    struct
    {
        uint8 AllianceCapture;  ///< 联盟占领文本ID
        uint8 AllianceAttack;   ///< 联盟进攻文本ID
        uint8 HordeCapture;     ///< 部落占领文本ID
        uint8 HordeAttack;      ///< 部落进攻文本ID
    } TextIds;

    /**
     * @brief 世界状态ID结构
     *
     * 包含节点不同状态的世界状态ID
     */
    struct
    {
        uint32 AllianceControl;   ///< 联盟控制状态ID
        uint32 AllianceAssault;   ///< 联盟进攻状态ID
        uint32 HordeControl;      ///< 部落控制状态ID
        uint32 HordeAssault;      ///< 部落进攻状态ID
    } WorldStateIds;
};

/**
 * @brief 静态节点信息数组
 *
 * 包含所有节点的静态配置信息
 * 每个条目定义了节点的ID、文本ID和世界状态ID
 */
static StaticNodeInfo const BGAVNodeInfo[] =
{
    { BG_AV_NODES_FIRSTAID_STATION,  { 47, 48, 45, 46 }, { 1325, 1326, 1327, 1328 } }, ///< 雷矛急救站
    { BG_AV_NODES_STORMPIKE_GRAVE,   {  1,  2,  3,  4 }, { 1333, 1335, 1334, 1336 } }, ///< 雷矛墓地
    { BG_AV_NODES_STONEHEART_GRAVE,  { 55, 56, 53, 54 }, { 1302, 1304, 1301, 1303 } }, ///< 石炉墓地
    { BG_AV_NODES_SNOWFALL_GRAVE,    {  5,  6,  7,  8 }, { 1341, 1343, 1342, 1344 } }, ///< 雪落墓地
    { BG_AV_NODES_ICEBLOOD_GRAVE,    { 59, 60, 57, 58 }, { 1346, 1348, 1347, 1349 } }, ///< 冰血墓地
    { BG_AV_NODES_FROSTWOLF_GRAVE,   {  9, 10, 11, 12 }, { 1337, 1339, 1338, 1340 } }, ///< 霜狼墓地
    { BG_AV_NODES_FROSTWOLF_HUT,     { 51, 52, 49, 50 }, { 1329, 1331, 1330, 1332 } }, ///< 霜狼小屋
    { BG_AV_NODES_DUNBALDAR_SOUTH,   { 16, 15, 14, 13 }, { 1361, 1375, 1370, 1378 } }, ///< 丹巴达尔南部碉堡
    { BG_AV_NODES_DUNBALDAR_NORTH,   { 20, 19, 18, 17 }, { 1362, 1374, 1371, 1379 } }, ///< 丹巴达尔北部碉堡
    { BG_AV_NODES_ICEWING_BUNKER,    { 24, 23, 22, 21 }, { 1363, 1376, 1372, 1380 } }, ///< 冰翼碉堡
    { BG_AV_NODES_STONEHEART_BUNKER, { 28, 27, 26, 25 }, { 1364, 1377, 1373, 1381 } }, ///< 石炉碉堡
    { BG_AV_NODES_ICEBLOOD_TOWER,    { 44, 43, 42, 41 }, { 1368, 1390, 1385, 1395 } }, ///< 冰血哨塔
    { BG_AV_NODES_TOWER_POINT,       { 40, 39, 38, 37 }, { 1367, 1389, 1384, 1394 } }, ///< 哨塔点
    { BG_AV_NODES_FROSTWOLF_ETOWER,  { 36, 35, 34, 33 }, { 1366, 1388, 1383, 1393 } }, ///< 霜狼东塔
    { BG_AV_NODES_FROSTWOLF_WTOWER,  { 32, 31, 30, 29 }, { 1365, 1387, 1382, 1392 } }, ///< 霜狼西塔
};

/**
 * @enum Texts
 * @brief 文本ID枚举
 *
 * 定义了奥特兰克山谷战场中使用的各种文本ID
 */
enum Texts
{
    // 传令官（Herald）
    // 塔楼/墓地 = 1 - 60
    TEXT_COLDTOOTH_MINE_ALLIANCE_TAKEN  = 61,  ///< 冷齿矿井被联盟占领
    TEXT_IRONDEEP_MINE_ALLIANCE_TAKEN   = 62,  ///< 铁深矿井被联盟占领
    TEXT_COLDTOOTH_MINE_HORDE_TAKEN     = 63,  ///< 冷齿矿井被部落占领
    TEXT_IRONDEEP_MINE_HORDE_TAKEN      = 64,  ///< 铁深矿井被部落占领
    TEXT_FROSTWOLF_GENERAL_DEAD         = 65,  ///< 霜狼将军死亡 @todo 缺少音效
    TEXT_STORMPIKE_GENERAL_DEAD         = 66,  ///< 雷矛将军死亡 @todo 缺少音效
    TEXT_ALLIANCE_WINS                  = 67,  ///< 联盟获胜（未实现） @todo 缺少音效
    TEXT_HORDE_WINS                     = 68,  ///< 部落获胜（未实现） @todo 缺少音效

    // 监工斯尼维尔（Taskmaster Snivvle）
    TEXT_SNIVVLE_RANDOM                 = 0    ///< 斯尼维尔随机文本
};

/**
 * @struct BG_AV_NodeInfo
 * @brief 节点运行时信息结构
 *
 * 存储每个节点的运行时状态信息，包括当前状态、所有者和计时器
 */
struct BG_AV_NodeInfo
{
    BG_AV_States State;      ///< 当前状态
    BG_AV_States PrevState;  ///< 前一状态
    uint32       Timer;      ///< 状态计时器（用于占领倒计时）
    uint16       TotalOwner; ///< 总拥有者（最终控制者）
    uint16       Owner;      ///< 当前拥有者
    uint16       PrevOwner;  ///< 前一拥有者
    bool         Tower;      ///< 是否为塔楼（true为塔楼，false为墓地）
};

/**
 * @brief 节点枚举递增运算符
 *
 * 允许对 BG_AV_Nodes 枚举进行递增操作
 *
 * @param i 节点枚举引用
 * @return 递增后的节点枚举引用
 */
inline BG_AV_Nodes &operator++(BG_AV_Nodes& i) { return i = BG_AV_Nodes(i + 1); }

/**
 * @struct BattlegroundAVScore
 * @brief 奥特兰克山谷战场分数结构
 *
 * 继承自 BattlegroundScore，存储奥特兰克山谷特有的计分数据
 * 包括墓地进攻、防守、塔楼进攻、防守和矿井占领数量
 */
struct BattlegroundAVScore final : public BattlegroundScore
{
    friend class BattlegroundAV;  ///< 允许 BattlegroundAV 类访问私有成员

    protected:
        /**
         * @brief 构造函数
         *
         * 初始化所有奥特兰克山谷特有的计分项为0
         *
         * @param playerGuid 玩家GUID
         */
        BattlegroundAVScore(ObjectGuid playerGuid) : BattlegroundScore(playerGuid), GraveyardsAssaulted(0), GraveyardsDefended(0), TowersAssaulted(0), TowersDefended(0), MinesCaptured(0) { }

        /**
         * @brief 更新分数
         *
         * 根据分数类型更新玩家的计分数据
         *
         * @param type 分数类型
         * @param value 分数值
         */
        void UpdateScore(uint32 type, uint32 value) override
        {
            switch (type)
            {
                case SCORE_GRAVEYARDS_ASSAULTED:
                    GraveyardsAssaulted += value;  ///< 墓地进攻数
                    break;
                case SCORE_GRAVEYARDS_DEFENDED:
                    GraveyardsDefended += value;   ///< 墓地防守数
                    break;
                case SCORE_TOWERS_ASSAULTED:
                    TowersAssaulted += value;      ///< 塔楼进攻数
                    break;
                case SCORE_TOWERS_DEFENDED:
                    TowersDefended += value;       ///< 塔楼防守数
                    break;
                case SCORE_MINES_CAPTURED:
                    MinesCaptured += value;        ///< 矿井占领数
                    break;
                default:
                    BattlegroundScore::UpdateScore(type, value);
                    break;
            }
        }

        /**
         * @brief 构建目标数据块
         *
         * 将玩家的目标数据序列化到数据包中
         *
         * @param data 世界数据包
         */
        void BuildObjectivesBlock(WorldPacket& data) final override;

        /**
         * @brief 获取属性1
         * @return 墓地进攻数量
         */
        uint32 GetAttr1() const final override { return GraveyardsAssaulted; }

        /**
         * @brief 获取属性2
         * @return 墓地防守数量
         */
        uint32 GetAttr2() const final override { return GraveyardsDefended; }

        /**
         * @brief 获取属性3
         * @return 塔楼进攻数量
         */
        uint32 GetAttr3() const final override { return TowersAssaulted; }

        /**
         * @brief 获取属性4
         * @return 塔楼防守数量
         */
        uint32 GetAttr4() const final override { return TowersDefended; }

        /**
         * @brief 获取属性5
         * @return 矿井占领数量
         */
        uint32 GetAttr5() const final override { return MinesCaptured; }

        uint32 GraveyardsAssaulted;   ///< 进攻墓地次数
        uint32 GraveyardsDefended;    ///< 防守墓地次数
        uint32 TowersAssaulted;       ///< 进攻塔楼次数
        uint32 TowersDefended;        ///< 防守塔楼次数
        uint32 MinesCaptured;         ///< 占领矿井次数
};

/**
 * @class BattlegroundAV
 * @brief 奥特兰克山谷战场类
 *
 * 继承自 Battleground 基类，实现了奥特兰克山谷战场的核心逻辑
 * 奥特兰克山谷是一个大型40v40战场，玩家需要占领墓地和摧毁塔楼，
 * 最终击杀敌方将军以获得胜利。
 */
class BattlegroundAV : public Battleground
{
    public:
        BattlegroundAV();   ///< 构造函数
        ~BattlegroundAV();  ///< 析构函数

        /* 继承自 Battleground 类的方法 */

        /**
         * @brief 添加玩家到战场
         * @param player 玩家指针
         */
        void AddPlayer(Player* player) override;

        /**
         * @brief 开始事件关闭大门
         *
         * 在战斗开始前关闭起始区域的大门
         */
        void StartingEventCloseDoors() override;

        /**
         * @brief 开始事件打开大门
         *
         * 战斗开始时打开大门，允许玩家离开起始区域
         */
        void StartingEventOpenDoors() override;

        /**
         * @brief 移除玩家
         *
         * 当玩家离开战场时调用
         *
         * @param player 玩家指针
         * @param guid 玩家GUID
         * @param team 玩家队伍
         */
        void RemovePlayer(Player* player, ObjectGuid guid, uint32 team) override;

        /**
         * @brief 处理区域触发
         *
         * 当玩家进入特定区域触发器时调用
         *
         * @param player 玩家指针
         * @param trigger 触发器ID
         */
        void HandleAreaTrigger(Player* player, uint32 trigger) override;

        /**
         * @brief 设置战场
         *
         * 初始化战场，生成所有必需的游戏对象和生物
         *
         * @return 设置成功返回true，否则返回false
         */
        bool SetupBattleground() override;

        /**
         * @brief 重置战场子类数据
         *
         * 重置奥特兰克山谷特有的数据
         */
        void ResetBGSubclass() override;

        /* 通用功能 */

        /**
         * @brief 更新分数
         *
         * 更新指定队伍的分数
         *
         * @param team 队伍ID
         * @param points 分数变化量（可以为负数）
         */
        void UpdateScore(uint16 team, int16 points);

        /**
         * @brief 更新玩家分数
         *
         * 更新玩家的计分数据
         *
         * @param player 玩家指针
         * @param type 分数类型
         * @param value 分数值
         * @param doAddHonor 是否添加荣誉，默认为true
         * @return 更新成功返回true
         */
        bool UpdatePlayerScore(Player* player, uint32 type, uint32 value, bool doAddHonor = true) override;

        /* 事件处理函数 - 这些函数从外部调用 */

        /**
         * @brief 玩家点击旗帜事件
         *
         * 当玩家点击旗帜时触发
         *
         * @param source 玩家指针
         * @param target_obj 目标游戏对象
         */
        void EventPlayerClickedOnFlag(Player* source, GameObject* target_obj) override;

        /**
         * @brief 处理玩家击杀
         *
         * 当玩家击杀其他玩家时调用
         *
         * @param player 被击杀的玩家
         * @param killer 击杀者
         */
        void HandleKillPlayer(Player* player, Player* killer) override;

        /**
         * @brief 处理单位击杀
         *
         * 当玩家击杀NPC单位时调用（如队长、将军等）
         *
         * @param unit 被击杀的单位
         * @param killer 击杀者
         */
        void HandleKillUnit(Creature* unit, Player* killer) override;

        /**
         * @brief 处理任务完成
         *
         * 当玩家完成战场任务时调用
         *
         * @param questid 任务ID
         * @param player 玩家指针
         */
        void HandleQuestComplete(uint32 questid, Player* player) override;

        /**
         * @brief 判断是否可以激活游戏对象
         *
         * 检查指定队伍是否可以激活特定游戏对象
         *
         * @param GOId 游戏对象ID
         * @param team 队伍ID
         * @return 可以激活返回true，否则返回false
         */
        bool CanActivateGO(int32 GOId, uint32 team) const override;

        /**
         * @brief 结束战场
         *
         * 奥特兰克山谷特有的结束逻辑
         *
         * @param winner 获胜队伍ID
         */
        void EndBattleground(uint32 winner) override;

        /**
         * @brief 获取最近的墓地
         *
         * 为玩家找到最近的可用墓地
         *
         * @param player 玩家指针
         * @return 墓地位置信息
         */
        WorldSafeLocsEntry const* GetClosestGraveyard(Player* player) override;

        /**
         * @brief 检查成就条件
         *
         * 检查是否满足成就条件
         * 成就：奥特兰克完美胜利和一切都很重要
         *
         * @param criteriaId 成就条件ID
         * @param source 源玩家
         * @param target 目标单位
         * @param miscvalue1 额外值1
         * @return 满足条件返回true
         */
        bool CheckAchievementCriteriaMeet(uint32 criteriaId, Player const* source, Unit const* target = nullptr, uint32 miscvalue1 = 0) override;

        /**
         * @brief 获取提前结束获胜者
         *
         * 当战场提前结束时确定获胜者
         *
         * @return 获胜队伍ID
         */
        uint32 GetPrematureWinner() override;

    private:
        /**
         * @brief 战场更新实现
         *
         * 每帧调用的更新函数，处理所有计时器和状态更新
         *
         * @param diff 距离上次更新的时间差（毫秒）
         */
        void PostUpdateImpl(uint32 diff) override;

        /* 节点占领相关 */

        /**
         * @brief 玩家进攻节点事件
         *
         * 当玩家开始进攻某个节点时调用
         *
         * @param player 玩家指针
         * @param object 游戏对象ID
         */
        void EventPlayerAssaultsPoint(Player* player, uint32 object);

        /**
         * @brief 玩家防守节点事件
         *
         * 当玩家防守某个节点时调用
         *
         * @param player 玩家指针
         * @param object 游戏对象ID
         */
        void EventPlayerDefendsPoint(Player* player, uint32 object);

        /**
         * @brief 节点被摧毁事件
         *
         * 当节点（塔楼）被摧毁时调用
         *
         * @param node 节点ID
         */
        void EventPlayerDestroyedPoint(BG_AV_Nodes node);

        /**
         * @brief 进攻节点
         *
         * 设置节点为被进攻状态
         *
         * @param node 节点ID
         * @param team 进攻队伍ID
         */
        void AssaultNode(BG_AV_Nodes node, uint16 team);

        /**
         * @brief 摧毁节点
         *
         * 将节点设置为摧毁状态（仅用于塔楼）
         *
         * @param node 节点ID
         */
        void DestroyNode(BG_AV_Nodes node);

        /**
         * @brief 初始化节点
         *
         * 设置节点的初始状态
         *
         * @param node 节点ID
         * @param team 所有者队伍ID
         * @param tower 是否为塔楼
         */
        void InitNode(BG_AV_Nodes node, uint16 team, bool tower);

        /**
         * @brief 防守节点
         *
         * 成功防守节点，恢复到控制状态
         *
         * @param node 节点ID
         * @param team 防守队伍ID
         */
        void DefendNode(BG_AV_Nodes node, uint16 team);

        /**
         * @brief 填充节点
         *
         * 在节点上生成防御NPC
         *
         * @param node 节点ID
         */
        void PopulateNode(BG_AV_Nodes node);

        /**
         * @brief 清空节点
         *
         * 移除节点上的防御NPC
         *
         * @param node 节点ID
         */
        void DePopulateNode(BG_AV_Nodes node);

        /**
         * @brief 获取静态节点信息
         *
         * 查找并返回指定节点的静态配置信息
         *
         * @param node 节点ID
         * @return 静态节点信息指针，未找到返回nullptr
         */
        StaticNodeInfo const* GetStaticNodeInfo(BG_AV_Nodes node) const
        {
            for (uint8 i = 0; i < BG_AV_NODES_MAX; ++i)
                if (BGAVNodeInfo[i].NodeId == node)
                    return &BGAVNodeInfo[i];
            return nullptr;
        }

        /**
         * @brief 通过游戏对象获取节点ID
         *
         * 根据游戏对象ID查找对应的节点
         *
         * @param object 游戏对象ID
         * @return 节点ID
         */
        BG_AV_Nodes GetNodeThroughObject(uint32 object);

        /**
         * @brief 通过节点获取游戏对象ID
         *
         * 根据节点ID查找对应的游戏对象
         *
         * @param node 节点ID
         * @return 游戏对象ID
         */
        uint32 GetObjectThroughNode(BG_AV_Nodes node);

        /**
         * @brief 判断是否为塔楼
         *
         * 检查指定节点是否为塔楼
         *
         * @param node 节点ID
         * @return 是塔楼返回true，否则返回false
         */
        bool IsTower(BG_AV_Nodes node) { return m_Nodes[node].Tower; }

        /* 矿井相关 */

        /**
         * @brief 更改矿井所有者
         *
         * 更改矿井的控制权
         *
         * @param mine 矿井索引（0=北部，1=南部）
         * @param team 新所有者队伍ID
         * @param initial 是否为初始设置，默认为false
         */
        void ChangeMineOwner(uint8 mine, uint32 team, bool initial = false);

        /* 世界状态相关 */

        /**
         * @brief 填充初始世界状态
         *
         * 向客户端发送战场初始世界状态
         *
         * @param packet 世界状态数据包
         */
        void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;

        /**
         * @brief 发送矿井世界状态
         *
         * 更新并发送指定矿井的世界状态
         *
         * @param mine 矿井索引
         */
        void SendMineWorldStates(uint32 mine);

        /**
         * @brief 更新节点世界状态
         *
         * 更新指定节点的世界状态
         *
         * @param node 节点ID
         */
        void UpdateNodeWorldState(BG_AV_Nodes node);

        /* 通用功能 */

        /**
         * @brief 添加奥特兰克山谷生物
         *
         * 生成并添加生物到战场
         *
         * @param cinfoid 生物信息ID
         * @param type 生物类型
         * @return 生成的生物指针
         */
        Creature* AddAVCreature(uint16 cinfoid, uint16 type);

        /* 成员变量 */

        int32 m_Team_Scores[2];                    ///< 队伍分数 [联盟, 部落]
        uint32 m_Team_QuestStatus[2][9];           ///< 队伍任务进度 [队伍][任务计数器]

        BG_AV_NodeInfo m_Nodes[BG_AV_NODES_MAX];   ///< 所有节点的运行时信息

        uint32 m_Mine_Owner[2];                    ///< 矿井所有者 [北部, 南部]
        uint32 m_Mine_PrevOwner[2];                ///< 矿井前一所有者（仅用于世界状态）
        int32 m_Mine_Timer;                        ///< 矿井计时器（两个队伍共用）
        uint32 m_Mine_Reclaim_Timer[2];            ///< 矿井回收计时器 [北部, 南部]
        uint32 m_CaptainBuffTimer[2];               ///< 队长增益计时器 [联盟, 部落]
        bool m_CaptainAlive[2];                     ///< 队长是否存活 [联盟, 部落]

        bool m_IsInformedNearVictory[2];            ///< 是否已通知即将获胜 [联盟, 部落]
};

#endif
