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
 * @file BattlegroundAB.h
 * @brief 阿拉希盆地（Arathi Basin）战场头文件
 *
 * 本文件定义了阿拉希盆地战场的核心数据结构和逻辑。
 * 阿拉希盆地是一个15v15的资源争夺战场，联盟和部落争夺5个资源点：
 * - 铁匠铺（Blacksmith）
 * - 农场（Farm）
 * - 金矿（Gold Mine）
 * - 伐木场（Lumber Mill）
 * - 马厩（Stables）
 *
 * 占领资源点的队伍会定期获得资源分数，先达到1600分的队伍获胜。
 */

#ifndef __BATTLEGROUNDAB_H
#define __BATTLEGROUNDAB_H

#include "Battleground.h"
#include "BattlegroundScore.h"
#include "Object.h"

/**
 * @brief 阿拉希盆地世界状态枚举
 *
 * 用于客户端UI显示的世界状态ID，包括：
 * - 双方占领的资源点数量
 * - 双方的资源分数
 * - 最大资源分数和警告阈值
 */
enum BG_AB_WorldStates
{
    BG_AB_OP_OCCUPIED_BASES_HORDE       = 1778,  ///< 部落占领的基地数量
    BG_AB_OP_OCCUPIED_BASES_ALLY        = 1779,  ///< 联盟占领的基地数量
    BG_AB_OP_RESOURCES_ALLY             = 1776,  ///< 联盟当前资源分数
    BG_AB_OP_RESOURCES_HORDE            = 1777,  ///< 部落当前资源分数
    BG_AB_OP_RESOURCES_MAX              = 1780,  ///< 最大资源分数（1600）
    BG_AB_OP_RESOURCES_WARNING          = 1955   ///< 即将胜利警告阈值（1400）
    /*
    以下注释掉的枚举用于每个资源点的详细地图状态：
    - 图标状态（ICON）：资源点在地图上的显示图标
    - 阵营状态（STATE）：资源点的占领状态
    - 争夺状态（CON）：资源点正在被争夺
    */
    /*
    BG_AB_OP_STABLE_ICON                = 1842,             //Stable map icon (NONE)
    BG_AB_OP_STABLE_STATE_ALIENCE       = 1767,             //Stable map state (ALIENCE)
    BG_AB_OP_STABLE_STATE_HORDE         = 1768,             //Stable map state (HORDE)
    BG_AB_OP_STABLE_STATE_CON_ALI       = 1769,             //Stable map state (CON ALIENCE)
    BG_AB_OP_STABLE_STATE_CON_HOR       = 1770,             //Stable map state (CON HORDE)
    BG_AB_OP_FARM_ICON                  = 1845,             //Farm map icon (NONE)
    BG_AB_OP_FARM_STATE_ALIENCE         = 1772,             //Farm state (ALIENCE)
    BG_AB_OP_FARM_STATE_HORDE           = 1773,             //Farm state (HORDE)
    BG_AB_OP_FARM_STATE_CON_ALI         = 1774,             //Farm state (CON ALIENCE)
    BG_AB_OP_FARM_STATE_CON_HOR         = 1775,             //Farm state (CON HORDE)

    BG_AB_OP_BLACKSMITH_ICON            = 1846,             //Blacksmith map icon (NONE)
    BG_AB_OP_BLACKSMITH_STATE_ALIENCE   = 1782,             //Blacksmith map state (ALIENCE)
    BG_AB_OP_BLACKSMITH_STATE_HORDE     = 1783,             //Blacksmith map state (HORDE)
    BG_AB_OP_BLACKSMITH_STATE_CON_ALI   = 1784,             //Blacksmith map state (CON ALIENCE)
    BG_AB_OP_BLACKSMITH_STATE_CON_HOR   = 1785,             //Blacksmith map state (CON HORDE)
    BG_AB_OP_LUMBERMILL_ICON            = 1844,             //Lumber Mill map icon (NONE)
    BG_AB_OP_LUMBERMILL_STATE_ALIENCE   = 1792,             //Lumber Mill map state (ALIENCE)
    BG_AB_OP_LUMBERMILL_STATE_HORDE     = 1793,             //Lumber Mill map state (HORDE)
    BG_AB_OP_LUMBERMILL_STATE_CON_ALI   = 1794,             //Lumber Mill map state (CON ALIENCE)
    BG_AB_OP_LUMBERMILL_STATE_CON_HOR   = 1795,             //Lumber Mill map state (CON HORDE)
    BG_AB_OP_GOLDMINE_ICON              = 1843,             //Gold Mine map icon (NONE)
    BG_AB_OP_GOLDMINE_STATE_ALIENCE     = 1787,             //Gold Mine map state (ALIENCE)
    BG_AB_OP_GOLDMINE_STATE_HORDE       = 1788,             //Gold Mine map state (HORDE)
    BG_AB_OP_GOLDMINE_STATE_CON_ALI     = 1789,             //Gold Mine map state (CON ALIENCE
    BG_AB_OP_GOLDMINE_STATE_CON_HOR     = 1790,             //Gold Mine map state (CON HORDE)
    */
};

/// 资源点状态世界状态ID数组（马厩、铁匠铺、农场、伐木场、金矿）
const uint32 BG_AB_OP_NODESTATES[5] =    {1767, 1782, 1772, 1792, 1787};

/// 资源点图标世界状态ID数组（马厩、铁匠铺、农场、伐木场、金矿）
const uint32 BG_AB_OP_NODEICONS[5]  =    {1842, 1846, 1845, 1844, 1843};

/**
 * @brief 资源点旗帜游戏对象ID枚举
 *
 * 每个资源点都有对应的旗帜对象ID，用于显示在地图上
 * 注意：这些ID必须是连续的，代码中依赖此特性
 */
enum BG_AB_NodeObjectId
{
    BG_AB_OBJECTID_NODE_BANNER_0    = 180087,       ///< 马厩旗帜
    BG_AB_OBJECTID_NODE_BANNER_1    = 180088,       ///< 铁匠铺旗帜
    BG_AB_OBJECTID_NODE_BANNER_2    = 180089,       ///< 农场旗帜
    BG_AB_OBJECTID_NODE_BANNER_3    = 180090,       ///< 伐木场旗帜
    BG_AB_OBJECTID_NODE_BANNER_4    = 180091        ///< 金矿旗帜
};

/**
 * @brief 阿拉希盆地游戏对象类型枚举
 *
 * 定义了战场中所有游戏对象的索引，包括：
 * - 每个资源点的旗帜和光环对象（8个对象 × 5个资源点 = 40个对象）
 * - 双方起始大门
 * - 各资源点的增益buff
 */
enum BG_AB_ObjectType
{
    // 每个资源点有8个对象（5个资源点共40个对象）
    BG_AB_OBJECT_BANNER_NEUTRAL          = 0,   ///< 中立旗帜对象索引
    BG_AB_OBJECT_BANNER_CONT_A           = 1,   ///< 联盟争夺中旗帜对象索引
    BG_AB_OBJECT_BANNER_CONT_H           = 2,   ///< 部落争夺中旗帜对象索引
    BG_AB_OBJECT_BANNER_ALLY             = 3,   ///< 联盟占领旗帜对象索引
    BG_AB_OBJECT_BANNER_HORDE            = 4,   ///< 部落占领旗帜对象索引
    BG_AB_OBJECT_AURA_ALLY               = 5,   ///< 联盟占领光环对象索引
    BG_AB_OBJECT_AURA_HORDE              = 6,   ///< 部落占领光环对象索引
    BG_AB_OBJECT_AURA_CONTESTED          = 7,   ///< 争夺中光环对象索引
    //大门
    BG_AB_OBJECT_GATE_A                  = 40,  ///< 联盟起始大门索引
    BG_AB_OBJECT_GATE_H                  = 41,  ///< 部落起始大门索引
    //增益buff（每个资源点有3种buff：速度、回复、狂暴）
    BG_AB_OBJECT_SPEEDBUFF_STABLES       = 42,  ///< 马厩速度buff
    BG_AB_OBJECT_REGENBUFF_STABLES       = 43,  ///< 马厩回复buff
    BG_AB_OBJECT_BERSERKBUFF_STABLES     = 44,  ///< 马厩狂暴buff
    BG_AB_OBJECT_SPEEDBUFF_BLACKSMITH    = 45,  ///< 铁匠铺速度buff
    BG_AB_OBJECT_REGENBUFF_BLACKSMITH    = 46,  ///< 铁匠铺回复buff
    BG_AB_OBJECT_BERSERKBUFF_BLACKSMITH  = 47,  ///< 铁匠铺狂暴buff
    BG_AB_OBJECT_SPEEDBUFF_FARM          = 48,  ///< 农场速度buff
    BG_AB_OBJECT_REGENBUFF_FARM          = 49,  ///< 农场回复buff
    BG_AB_OBJECT_BERSERKBUFF_FARM        = 50,  ///< 农场狂暴buff
    BG_AB_OBJECT_SPEEDBUFF_LUMBER_MILL   = 51,  ///< 伐木场速度buff
    BG_AB_OBJECT_REGENBUFF_LUMBER_MILL   = 52,  ///< 伐木场回复buff
    BG_AB_OBJECT_BERSERKBUFF_LUMBER_MILL = 53,  ///< 伐木场狂暴buff
    BG_AB_OBJECT_SPEEDBUFF_GOLD_MINE     = 54,  ///< 金矿速度buff
    BG_AB_OBJECT_REGENBUFF_GOLD_MINE     = 55,  ///< 金矿回复buff
    BG_AB_OBJECT_BERSERKBUFF_GOLD_MINE   = 56,  ///< 金矿狂暴buff
    BG_AB_OBJECT_MAX                     = 57   ///< 对象总数
};

/**
 * @brief 游戏对象模板ID枚举（从数据库读取）
 *
 * 定义了各种游戏对象的模板ID，用于创建游戏对象
 */
enum BG_AB_ObjectTypes
{
    BG_AB_OBJECTID_BANNER_A             = 180058,  ///< 联盟占领旗帜模板ID
    BG_AB_OBJECTID_BANNER_CONT_A        = 180059,  ///< 联盟争夺中旗帜模板ID
    BG_AB_OBJECTID_BANNER_H             = 180060,  ///< 部落占领旗帜模板ID
    BG_AB_OBJECTID_BANNER_CONT_H        = 180061,  ///< 部落争夺中旗帜模板ID

    BG_AB_OBJECTID_AURA_A               = 180100,  ///< 联盟光环模板ID
    BG_AB_OBJECTID_AURA_H               = 180101,  ///< 部落光环模板ID
    BG_AB_OBJECTID_AURA_C               = 180102,  ///< 争夺中光环模板ID

    BG_AB_OBJECTID_GATE_A               = 180255,  ///< 联盟大门模板ID
    BG_AB_OBJECTID_GATE_H               = 180256   ///< 部落大门模板ID
};

/**
 * @brief 阿拉希盆地时间相关常量
 */
enum BG_AB_Timers
{
    BG_AB_FLAG_CAPTURING_TIME           = 60000   ///< 旗帜占领时间（60秒，从争夺状态变为占领状态）
};

/**
 * @brief 阿拉希盆地分数相关常量
 */
enum BG_AB_Score
{
    BG_AB_WARNING_NEAR_VICTORY_SCORE    = 1400,   ///< 即将胜利警告分数阈值
    BG_AB_MAX_TEAM_SCORE                = 1600    ///< 最大团队分数（达到此分数即获胜）
};

/**
 * @brief 阿拉希盆地战场资源点枚举
 *
 * 定义了战场中的所有节点（资源点和墓地）
 * 注意：顺序不可更改，否则会导致错误行为
 */
enum BG_AB_BattlegroundNodes
{
    BG_AB_NODE_STABLES          = 0,   ///< 马厩（Stables）
    BG_AB_NODE_BLACKSMITH       = 1,   ///< 铁匠铺（Blacksmith）
    BG_AB_NODE_FARM             = 2,   ///< 农场（Farm）
    BG_AB_NODE_LUMBER_MILL      = 3,   ///< 伐木场（Lumber Mill）
    BG_AB_NODE_GOLD_MINE        = 4,   ///< 金矿（Gold Mine）

    BG_AB_DYNAMIC_NODES_COUNT   = 5,   ///< 可争夺的动态资源点数量

    BG_AB_SPIRIT_ALIANCE        = 5,   ///< 联盟起始基地灵魂医者
    BG_AB_SPIRIT_HORDE          = 6,   ///< 部落起始基地灵魂医者

    BG_AB_ALL_NODES_COUNT       = 7    ///< 所有节点数量（包括动态和静态）
};

/**
 * @brief 阿拉希盆地广播文本ID枚举
 */
enum BG_AB_BroadcastTexts
{
    BG_AB_TEXT_ALLIANCE_NEAR_VICTORY    = 10598,  ///< 联盟即将胜利文本ID
    BG_AB_TEXT_HORDE_NEAR_VICTORY       = 10599,  ///< 部落即将胜利文本ID
};

/**
 * @brief 资源点信息结构体
 *
 * 存储每个资源点的广播文本ID，用于在不同占领状态下发送系统消息
 */
struct ABNodeInfo
{
    uint32 NodeId;                 ///< 资源点ID
    uint32 TextAllianceAssaulted;  ///< 联盟突袭文本ID
    uint32 TextHordeAssaulted;     ///< 部落突袭文本ID
    uint32 TextAllianceTaken;      ///< 联盟占领文本ID
    uint32 TextHordeTaken;         ///< 部落占领文本ID
    uint32 TextAllianceDefended;   ///< 联盟防守文本ID
    uint32 TextHordeDefended;      ///< 部落防守文本ID
    uint32 TextAllianceClaims;     ///< 联盟宣示文本ID
    uint32 TextHordeClaims;        ///< 部落宣示文本ID
};

/// 5个资源点的信息数组（马厩、铁匠铺、农场、伐木场、金矿）
ABNodeInfo const ABNodes[BG_AB_DYNAMIC_NODES_COUNT] =
{
    { BG_AB_NODE_STABLES,     10199, 10200, 10203, 10204, 10201, 10202, 10286, 10287 },
    { BG_AB_NODE_BLACKSMITH,  10211, 10212, 10213, 10214, 10215, 10216, 10290, 10291 },
    { BG_AB_NODE_FARM,        10217, 10218, 10219, 10220, 10221, 10222, 10288, 10289 },
    { BG_AB_NODE_LUMBER_MILL, 10224, 10225, 10226, 10227, 10228, 10229, 10284, 10285 },
    { BG_AB_NODE_GOLD_MINE,   10230, 10231, 10232, 10233, 10234, 10235, 10282, 10283 }
};

/**
 * @brief 资源点占领状态枚举
 *
 * 定义了资源点的不同占领状态
 */
enum BG_AB_NodeStatus
{
    BG_AB_NODE_TYPE_NEUTRAL             = 0,  ///< 中立状态（未被任何阵营占领）
    BG_AB_NODE_TYPE_CONTESTED           = 1,  ///< 争夺状态（正在被某个阵营占领）
    BG_AB_NODE_STATUS_ALLY_CONTESTED    = 1,  ///< 联盟正在争夺
    BG_AB_NODE_STATUS_HORDE_CONTESTED   = 2,  ///< 部落正在争夺
    BG_AB_NODE_TYPE_OCCUPIED            = 3,  ///< 已占领状态
    BG_AB_NODE_STATUS_ALLY_OCCUPIED     = 3,  ///< 联盟已占领
    BG_AB_NODE_STATUS_HORDE_OCCUPIED    = 4   ///< 部落已占领
};

/**
 * @brief 阿拉希盆地音效枚举
 *
 * 定义了各种游戏事件对应的音效ID
 */
enum BG_AB_Sounds
{
    BG_AB_SOUND_NODE_CLAIMED            = 8192,  ///< 资源点被宣示音效
    BG_AB_SOUND_NODE_CAPTURED_ALLIANCE  = 8173,  ///< 联盟占领资源点音效
    BG_AB_SOUND_NODE_CAPTURED_HORDE     = 8213,  ///< 部落占领资源点音效
    BG_AB_SOUND_NODE_ASSAULTED_ALLIANCE = 8212,  ///< 联盟突袭资源点音效
    BG_AB_SOUND_NODE_ASSAULTED_HORDE    = 8174,  ///< 部落突袭资源点音效
    BG_AB_SOUND_NEAR_VICTORY_ALLIANCE   = 8456,  ///< 联盟即将胜利音效
    BG_AB_SOUND_NEAR_VICTORY_HORDE      = 8457   ///< 部落即将胜利音效
};

/**
 * @brief 阿拉希盆地目标类型枚举
 *
 * 用于成就系统的目标类型
 */
enum BG_AB_Objectives
{
    AB_OBJECTIVE_ASSAULT_BASE           = 122,  ///< 突袭基地目标
    AB_OBJECTIVE_DEFEND_BASE            = 123   ///< 防守基地目标
};

/// 非战场周末荣誉点数间隔
#define BG_AB_NotABBGWeekendHonorTicks      260
/// 战场周末荣誉点数间隔
#define BG_AB_ABBGWeekendHonorTicks         160
/// 非战场周末声望点数间隔
#define BG_AB_NotABBGWeekendReputationTicks 160
/// 战场周末声望点数间隔
#define BG_AB_ABBGWeekendReputationTicks    120

/// 战斗开始事件ID（成就：Let's Get This Done）
#define AB_EVENT_START_BATTLE               9158

/// 5个资源点的位置坐标数组（马厩、铁匠铺、农场、伐木场、金矿）
Position const BG_AB_NodePositions[BG_AB_DYNAMIC_NODES_COUNT] =
{
    {1166.785f, 1200.132f, -56.70859f, 0.9075713f},         // 马厩
    {977.0156f, 1046.616f, -44.80923f, -2.600541f},         // 铁匠铺
    {806.1821f, 874.2723f, -55.99371f, -2.303835f},         // 农场
    {856.1419f, 1148.902f, 11.18469f, -2.303835f},          // 伐木场
    {1146.923f, 848.1782f, -110.917f, -0.7330382f}          // 金矿
};

/// 大门位置坐标数组（联盟、部落）
/// 格式：x, y, z, o, rot0, rot1, rot2, rot3
const float BG_AB_DoorPositions[2][8] =
{
    {1284.597f, 1281.167f, -15.97792f, 0.7068594f, 0.012957f, -0.060288f, 0.344959f, 0.93659f},   // 联盟大门
    {708.0903f, 708.4479f, -17.8342f, -2.391099f, 0.050291f, 0.015127f, 0.929217f, -0.365784f}    // 部落大门
};

/**
 * @brief 资源获取间隔和点数数组
 *
 * 根据占领的资源点数量决定资源获取的间隔时间和每次获得的点数
 * 数组索引对应占领的资源点数量（0-5）
 * - 占领1个资源点：每12秒获得10点
 * - 占领2个资源点：每9秒获得10点
 * - 占领3个资源点：每6秒获得10点
 * - 占领4个资源点：每3秒获得10点
 * - 占领5个资源点：每1秒获得30点
 */
const uint32 BG_AB_TickIntervals[6] = {0, 12000, 9000, 6000, 3000, 1000};
const uint32 BG_AB_TickPoints[6] = {0, 10, 10, 10, 10, 30};

/// 墓地世界安全位置ID数组（5个资源点墓地 + 联盟起始墓地 + 部落起始墓地）
const uint32 BG_AB_GraveyardIds[BG_AB_ALL_NODES_COUNT] = {895, 894, 893, 897, 896, 898, 899};

/// 各资源点增益buff位置坐标数组（x, y, z, o）
const float BG_AB_BuffPositions[BG_AB_DYNAMIC_NODES_COUNT][4] =
{
    {1185.566f, 1184.629f, -56.36329f, 2.303831f},         // 马厩
    {990.1131f, 1008.73f,  -42.60328f, 0.8203033f},         // 铁匠铺
    {818.0089f, 842.3543f, -56.54062f, 3.176533f},         // 农场
    {808.8463f, 1185.417f,  11.92161f, 5.619962f},         // 伐木场
    {1147.091f, 816.8362f, -98.39896f, 6.056293f}          // 金矿
};

/// 灵魂医者位置数组（5个资源点 + 联盟起始基地 + 部落起始基地）
Position const BG_AB_SpiritGuidePos[BG_AB_ALL_NODES_COUNT] =
{
    {1200.03f, 1171.09f, -56.47f, 5.15f},       // 马厩墓地
    {1017.43f, 960.61f, -42.95f, 4.88f},        // 铁匠铺墓地
    {833.00f, 793.00f, -57.25f, 5.27f},         // 农场墓地
    {775.17f, 1206.40f, 15.79f, 1.90f},         // 伐木场墓地
    {1207.48f, 787.00f, -83.36f, 5.51f},        // 金矿墓地
    {1354.05f, 1275.48f, -11.30f, 4.77f},       // 联盟起始基地
    {714.61f, 646.15f, -10.87f, 4.34f}          // 部落起始基地
};

/**
 * @brief 旗帜定时器结构体
 *
 * 用于管理旗帜状态切换的定时器
 */
struct BG_AB_BannerTimer
{
    uint32      timer;      ///< 剩余时间（毫秒）
    uint8       type;       ///< 旗帜类型
    uint8       teamIndex;  ///< 队伍索引（0=联盟，1=部落）
};

/**
 * @brief 阿拉希盆地玩家分数结构体
 *
 * 继承自BattlegroundScore，记录玩家在阿拉希盆地中的表现数据
 * 包括突袭基地次数和防守基地次数
 */
struct BattlegroundABScore final : public BattlegroundScore
{
    friend class BattlegroundAB;

    protected:
        /**
         * @brief 构造函数
         * @param playerGuid 玩家GUID
         */
        BattlegroundABScore(ObjectGuid playerGuid) : BattlegroundScore(playerGuid), BasesAssaulted(0), BasesDefended(0) { }

        /**
         * @brief 更新玩家分数
         * @param type 分数类型（SCORE_BASES_ASSAULTED或SCORE_BASES_DEFENDED）
         * @param value 增加的值
         */
        void UpdateScore(uint32 type, uint32 value) override
        {
            switch (type)
            {
                case SCORE_BASES_ASSAULTED:
                    BasesAssaulted += value;  ///< 突袭基地次数
                    break;
                case SCORE_BASES_DEFENDED:
                    BasesDefended += value;   ///< 防守基地次数
                    break;
                default:
                    BattlegroundScore::UpdateScore(type, value);
                    break;
            }
        }

        /**
         * @brief 构建目标数据块
         * @param data 世界数据包
         */
        void BuildObjectivesBlock(WorldPacket& data) final override;

        /// 获取属性1（突袭基地次数）
        uint32 GetAttr1() const final override { return BasesAssaulted; }
        /// 获取属性2（防守基地次数）
        uint32 GetAttr2() const final override { return BasesDefended; }

        uint32 BasesAssaulted;   ///< 突袭基地次数
        uint32 BasesDefended;    ///< 防守基地次数
};

/**
 * @brief 阿拉希盆地战场类
 *
 * 继承自Battleground，实现阿拉希盆地战场的核心逻辑
 * 包括资源点占领、资源分数计算、胜负判定等功能
 */
class BattlegroundAB : public Battleground
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化战场成员变量，设置对象和生物容器大小
         */
        BattlegroundAB();

        /**
         * @brief 析构函数
         */
        ~BattlegroundAB();

        /**
         * @brief 添加玩家到战场
         * @param player 玩家指针
         *
         * 当玩家进入战场时调用，创建玩家分数记录
         */
        void AddPlayer(Player* player) override;

        /**
         * @brief 开始事件：关闭大门
         *
         * 在战场开始前调用，生成中立旗帜、关闭大门、生成灵魂医者
         */
        void StartingEventCloseDoors() override;

        /**
         * @brief 开始事件：打开大门
         *
         * 战场开始时调用，打开大门，生成可争夺的旗帜和增益buff
         */
        void StartingEventOpenDoors() override;

        /**
         * @brief 移除玩家
         * @param player 玩家指针
         * @param guid 玩家GUID
         * @param team 队伍ID
         *
         * 当玩家离开战场时调用（目前未实现特殊逻辑）
         */
        void RemovePlayer(Player* player, ObjectGuid guid, uint32 team) override;

        /**
         * @brief 处理区域触发器
         * @param Source 触发玩家
         * @param Trigger 触发器ID
         *
         * 处理玩家进入特定区域触发器的事件，如离开战场的传送门
         */
        void HandleAreaTrigger(Player* Source, uint32 Trigger) override;

        /**
         * @brief 设置战场
         * @return 设置成功返回true，否则返回false
         *
         * 生成战场中的所有游戏对象（旗帜、大门、增益buff等）
         */
        bool SetupBattleground() override;

        /**
         * @brief 重置战场
         *
         * 将战场重置到初始状态，清空所有分数和节点状态
         */
        void Reset() override;

        /**
         * @brief 结束战场
         * @param winner 获胜队伍ID
         *
         * 战场结束时调用，发放荣誉奖励
         */
        void EndBattleground(uint32 winner) override;

        /**
         * @brief 获取最近的墓地
         * @param player 玩家指针
         * @return 墓地位置信息
         *
         * 根据玩家位置和阵营占领的资源点，返回最近的墓地位置
         */
        WorldSafeLocsEntry const* GetClosestGraveyard(Player* player) override;

        /**
         * @brief 更新玩家分数
         * @param player 玩家指针
         * @param type 分数类型
         * @param value 增加的值
         * @param doAddHonor 是否添加荣誉
         * @return 更新成功返回true，否则返回false
         *
         * 更新玩家分数并触发相关成就
         */
        bool UpdatePlayerScore(Player* player, uint32 type, uint32 value, bool doAddHonor = true) override;

        /**
         * @brief 填充初始世界状态
         * @param packet 世界状态数据包
         *
         * 初始化客户端的世界状态，包括资源点状态和双方分数
         */
        void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet) override;

        /**
         * @brief 玩家点击旗帜事件
         * @param source 点击玩家
         * @param target_obj 旗帜游戏对象
         *
         * 当玩家点击旗帜时调用，处理资源点占领逻辑
         */
        void EventPlayerClickedOnFlag(Player* source, GameObject* target_obj) override;

        /**
         * @brief 检查某队是否控制所有资源点
         * @param team 队伍ID
         * @return 如果控制所有资源点返回true，否则返回false
         *
         * 用于成就判定
         */
        bool IsAllNodesControlledByTeam(uint32 team) const override;

        /**
         * @brief 检查成就条件是否满足
         * @param criteriaId 成就条件ID
         * @param player 玩家指针
         * @param target 目标单位
         * @param miscvalue1 杂项值
         * @return 满足条件返回true，否则返回false
         */
        bool CheckAchievementCriteriaMeet(uint32 /*criteriaId*/, Player const* /*player*/, Unit const* /*target*/ = nullptr, uint32 /*miscvalue1*/ = 0) override;

        /**
         * @brief 获取提前结束时的获胜者
         * @return 获胜队伍ID
         *
         * 当战场因玩家不足而提前结束时，根据占领的资源点数量判定获胜者
         */
        uint32 GetPrematureWinner() override;

    private:
        /**
         * @brief 战场更新实现
         * @param diff 时间差（毫秒）
         *
         * 每帧调用，处理资源点占领计时、资源分数累积、胜负判定
         */
        void PostUpdateImpl(uint32 diff) override;

        /**
         * @brief 创建旗帜
         * @param node 资源点索引
         * @param type 旗帜类型
         * @param teamIndex 队伍索引
         * @param delay 是否延迟生成
         *
         * 在指定资源点生成对应状态的旗帜
         */
        void _CreateBanner(uint8 node, uint8 type, uint8 teamIndex, bool delay);

        /**
         * @brief 删除旗帜
         * @param node 资源点索引
         * @param type 旗帜类型
         * @param teamIndex 队伍索引
         *
         * 移除指定资源点的旗帜
         */
        void _DelBanner(uint8 node, uint8 type, uint8 teamIndex);

        /**
         * @brief 发送节点更新
         * @param node 资源点索引
         *
         * 向所有玩家发送资源点状态更新的世界状态消息
         */
        void _SendNodeUpdate(uint8 node);

        /**
         * @brief 节点被占领
         * @param node 资源点索引
         * @param team 占领队伍
         *
         * 当资源点被占领时调用，生成灵魂医者和荣誉奖励光环
         */
        void _NodeOccupied(uint8 node, Team team);

        /**
         * @brief 节点失去占领
         * @param node 资源点索引
         *
         * 当资源点失去占领时调用，移除灵魂医者和荣誉奖励光环
         */
        void _NodeDeOccupied(uint8 node);

        /**
         * @brief 节点状态数组
         *
         * 存储每个资源点的当前状态：
         * - 0: 中立
         * - 1: 联盟争夺中
         * - 2: 部落争夺中
         * - 3: 联盟占领
         * - 4: 部落占领
         */
        uint8               m_Nodes[BG_AB_DYNAMIC_NODES_COUNT];

        /// 节点的前一状态（用于状态回退）
        uint8               m_prevNodes[BG_AB_DYNAMIC_NODES_COUNT];

        /// 旗帜生成定时器数组
        BG_AB_BannerTimer   m_BannerTimers[BG_AB_DYNAMIC_NODES_COUNT];

        /// 节点占领计时器数组（毫秒）
        uint32              m_NodeTimers[BG_AB_DYNAMIC_NODES_COUNT];

        /// 上次资源更新时间（毫秒）
        uint32              m_lastTick[PVP_TEAMS_COUNT];

        /// 荣誉分数累计器
        uint32              m_HonorScoreTics[PVP_TEAMS_COUNT];

        /// 声望分数累计器
        uint32              m_ReputationScoreTics[PVP_TEAMS_COUNT];

        /// 是否已发送即将胜利警告
        bool                m_IsInformedNearVictory;

        /// 荣誉点数间隔（根据是否为战场周末而定）
        uint32              m_HonorTics;

        /// 声望点数间隔（根据是否为战场周末而定）
        uint32              m_ReputationTics;

        /// 队伍分数劣势标志（用于成就判定：队伍分数落后500分以上）
        bool                m_TeamScores500Disadvantage[PVP_TEAMS_COUNT];
};
#endif
