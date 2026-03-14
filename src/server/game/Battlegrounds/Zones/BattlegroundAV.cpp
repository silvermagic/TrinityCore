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
 * @file BattlegroundAV.cpp
 * @brief 奥特兰克山谷战场实现文件
 *
 * 本文件实现了奥特兰克山谷（Alterac Valley，简称AV）战场的核心逻辑。
 * 奥特兰克山谷是魔兽世界中最大规模的PvP战场，双方阵营（联盟与部落）
 * 各有600点初始资源，通过击杀敌方玩家、摧毁塔楼、击杀敌方队长等方式
 * 消耗敌方资源，先将敌方资源降至0的一方获胜，或者直接击杀敌方总指挥官获胜。
 *
 * 主要功能包括：
 * - 战场初始化与重置
 * - 墓地与塔楼的争夺与控制
 * - 矿坑系统（北矿与南矿）
 * - 队长与总指挥官系统
 * - 任务进度追踪
 * - 成就判定
 *
 * @see BattlegroundAV.h
 */

#include "BattlegroundAV.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "WorldSession.h"
#include "WorldStatePackets.h"

/**
 * @brief 构建玩家战场目标数据块
 *
 * 将玩家在奥特兰克山谷中的表现数据打包到网络数据包中，
 * 用于在战场结束后显示玩家的详细贡献。
 *
 * @param data 输出的网络数据包引用
 *
 * 数据包含以下5项统计：
 * - 进攻墓地数量
 * - 防守墓地数量
 * - 进攻塔楼数量
 * - 防守塔楼数量
 * - 占领矿坑数量
 */
void BattlegroundAVScore::BuildObjectivesBlock(WorldPacket& data)
{
    data << uint32(5); // 目标数量（固定5项统计）
    data << uint32(GraveyardsAssaulted);  // 进攻墓地次数
    data << uint32(GraveyardsDefended);   // 防守墓地次数
    data << uint32(TowersAssaulted);      // 进攻塔楼次数
    data << uint32(TowersDefended);       // 防守塔楼次数
    data << uint32(MinesCaptured);        // 占领矿坑次数
}

/**
 * @brief 奥特兰克山谷战场构造函数
 *
 * 初始化战场的基本状态，包括：
 * - 调整游戏对象和生物容器大小
 * - 初始化双方阵营的任务进度
 * - 初始化阵营分数
 * - 设置队长存活状态
 * - 初始化矿坑所有权
 * - 初始化所有节点状态
 * - 设置战场开始倒计时消息ID
 */
BattlegroundAV::BattlegroundAV()
{
    // 调整游戏对象容器大小，存储所有旗帜、光环等对象
    BgObjects.resize(BG_AV_OBJECT_MAX);
    // 调整生物容器大小，包括动态生物和静态生物位置
    BgCreatures.resize(AV_CPLACE_MAX + AsUnderlyingType(AV_STATICCPLACE_MAX));

    // 初始化双方阵营（联盟和部落）的状态
    for (uint8 i = 0; i < 2; i++)
    {
        // 初始化任务进度（共9种任务类型）
        for (uint8 j = 0; j < 9; j++)
            m_Team_QuestStatus[i][j] = 0;
        m_Team_Scores[i] = 0;              // 初始分数为0（重置时会设为600）
        m_IsInformedNearVictory[i] = false; // 是否已通知即将失败
        m_CaptainAlive[i] = true;           // 队长存活状态
        m_CaptainBuffTimer[i] = 0;          // 队长增益技能计时器
        m_Mine_Owner[i] = 0;                // 矿坑所有者（北矿和南矿）
        m_Mine_PrevOwner[i] = 0;            // 矿坑前一任所有者
        m_Mine_Reclaim_Timer[i] = 0;        // 矿坑回收计时器
    }

    m_Mine_Timer = 0;  // 矿坑资源累积计时器

    // 初始化所有节点（墓地和塔楼）的状态
    for (BG_AV_Nodes i = BG_AV_NODES_FIRSTAID_STATION; i < BG_AV_NODES_MAX; ++i)
        InitNode(i, 0, false);

    // 设置战场开始前的倒计时消息ID
    StartMessageIds[BG_STARTING_EVENT_SECOND] = BG_AV_TEXT_START_ONE_MINUTE;    // 1分钟倒计时
    StartMessageIds[BG_STARTING_EVENT_THIRD]  = BG_AV_TEXT_START_HALF_MINUTE;   // 30秒倒计时
    StartMessageIds[BG_STARTING_EVENT_FOURTH] = BG_AV_TEXT_BATTLE_HAS_BEGUN;    // 战斗开始
}

/**
 * @brief 奥特兰克山谷战场析构函数
 *
 * 析构函数为空，所有资源由基类管理
 */
BattlegroundAV::~BattlegroundAV() { }

/**
 * @brief 处理玩家击杀事件
 *
 * 当玩家在战场中击杀敌方玩家时调用。
 * 击杀敌方玩家会使被击杀方失去1点资源。
 *
 * @param player 被击杀的玩家
 * @param killer 击杀者
 *
 * @note 只有在战场进行中才会处理此事件
 */
void BattlegroundAV::HandleKillPlayer(Player* player, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    // 调用基类处理标准击杀逻辑
    Battleground::HandleKillPlayer(player, killer);
    // 被击杀玩家的阵营失去1点资源
    UpdateScore(player->GetTeam(), -1);
}

/**
 * @brief 处理生物击杀事件
 *
 * 当玩家击杀战场中的关键生物时调用，包括：
 * - 总指挥官（Boss）：击杀后直接结束战场，击杀方获胜
 * - 队长（Captain）：击杀后敌方阵营失去100点资源
 * - 矿坑Boss：击杀后占领对应矿坑
 *
 * @param unit 被击杀的生物
 * @param killer 击杀者
 *
 * 击杀不同目标的效果：
 * - 联盟总指挥官范达尔·雷矛：部落获胜
 * - 部落总指挥官德雷克塔尔：联盟获胜
 * - 联盟队长石墙上尉：联盟失去100资源
 * - 部落队长加尔范上尉：部落失去100资源
 * - 北矿Boss：占领北矿
 * - 南矿Boss：占领南矿
 */
void BattlegroundAV::HandleKillUnit(Creature* unit, Player* killer)
{
    TC_LOG_DEBUG("bg.battleground", "bg_av HandleKillUnit {}", unit->GetEntry());
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;
    uint32 entry = unit->GetEntry();

    // 击杀联盟总指挥官范达尔·雷矛 - 部落获胜
    if (entry == BG_AV_CreatureInfo[AV_NPC_A_BOSS])
    {
        CastSpellOnTeam(23658, HORDE); // 完成击杀Boss任务
        RewardReputationToTeam(729, BG_AV_REP_BOSS, HORDE);  // 奖励声望
        RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_BOSS), HORDE);  // 奖励荣誉
        EndBattleground(HORDE);  // 结束战场，部落获胜
        DelCreature(AV_CPLACE_TRIGGER17);
    }
    // 击杀部落总指挥官德雷克塔尔 - 联盟获胜
    else if (entry == BG_AV_CreatureInfo[AV_NPC_H_BOSS])
    {
        CastSpellOnTeam(23658, ALLIANCE); // 完成击杀Boss任务
        RewardReputationToTeam(730, BG_AV_REP_BOSS, ALLIANCE);  // 奖励声望
        RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_BOSS), ALLIANCE);  // 奖励荣誉
        EndBattleground(ALLIANCE);  // 结束战场，联盟获胜
        DelCreature(AV_CPLACE_TRIGGER19);
    }
    // 击杀联盟队长石墙上尉
    else if (entry == BG_AV_CreatureInfo[AV_NPC_A_CAPTAIN])
    {
        if (!m_CaptainAlive[0])
        {
            TC_LOG_ERROR("bg.battleground", "Killed a Captain twice, please report this bug, if you haven't done \".respawn\"");
            return;
        }
        m_CaptainAlive[0]=false;
        RewardReputationToTeam(729, BG_AV_REP_CAPTAIN, HORDE);  // 部落获得声望
        RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_CAPTAIN), HORDE);  // 部落获得荣誉
        UpdateScore(ALLIANCE, (-1)*BG_AV_RES_CAPTAIN);  // 联盟失去100资源
        // 生成摧毁特效
        for (uint8 i=0; i <= 9; i++)
            SpawnBGObject(BG_AV_OBJECT_BURN_BUILDING_ALLIANCE+i, RESPAWN_IMMEDIATELY);
        DelCreature(AV_CPLACE_TRIGGER16);

        // 公告联盟队长死亡
        if (Creature* herold = GetBGCreature(AV_CPLACE_HERALD))
            herold->AI()->Talk(TEXT_STORMPIKE_GENERAL_DEAD);
    }
    // 击杀部落队长加尔范上尉
    else if (entry == BG_AV_CreatureInfo[AV_NPC_H_CAPTAIN])
    {
        if (!m_CaptainAlive[1])
        {
            TC_LOG_ERROR("bg.battleground", "Killed a Captain twice, please report this bug, if you haven't done \".respawn\"");
            return;
        }
        m_CaptainAlive[1]=false;
        RewardReputationToTeam(730, BG_AV_REP_CAPTAIN, ALLIANCE);  // 联盟获得声望
        RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_CAPTAIN), ALLIANCE);  // 联盟获得荣誉
        UpdateScore(HORDE, (-1)*BG_AV_RES_CAPTAIN);  // 部落失去100资源
        // 生成摧毁特效
        for (uint8 i=0; i <= 9; i++)
            SpawnBGObject(BG_AV_OBJECT_BURN_BUILDING_HORDE+i, RESPAWN_IMMEDIATELY);
        DelCreature(AV_CPLACE_TRIGGER18);

        // 公告部落队长死亡
        if (Creature* herold = GetBGCreature(AV_CPLACE_HERALD))
            herold->AI()->Talk(TEXT_FROSTWOLF_GENERAL_DEAD);
    }
    // 击杀北矿Boss - 占领北矿
    else if (entry == BG_AV_CreatureInfo[AV_NPC_N_MINE_N_4] || entry == BG_AV_CreatureInfo[AV_NPC_N_MINE_A_4] || entry == BG_AV_CreatureInfo[AV_NPC_N_MINE_H_4])
        ChangeMineOwner(AV_NORTH_MINE, killer->GetTeam());
    // 击杀南矿Boss - 占领南矿
    else if (entry == BG_AV_CreatureInfo[AV_NPC_S_MINE_N_4] || entry == BG_AV_CreatureInfo[AV_NPC_S_MINE_A_4] || entry == BG_AV_CreatureInfo[AV_NPC_S_MINE_H_4])
        ChangeMineOwner(AV_SOUTH_MINE, killer->GetTeam());
}

/**
 * @brief 处理任务完成事件
 *
 * 当玩家完成战场相关任务时调用，用于追踪阵营的任务进度。
 * 任务进度会影响战场中的各种强化效果和增援。
 *
 * @param questid 完成的任务ID
 * @param player 完成任务的玩家
 *
 * 任务类型及其作用：
 * - 护甲碎片任务（索引0）：每完成一定数量升级墓地守卫
 * - 指挥官任务1-3（索引1-3）：解锁空军支援
 * - Boss相关任务（索引4）：召唤增援
 * - 矿坑任务（索引5-6）：解锁地面突击
 * - 骑兵任务（索引7-8）：解锁骑兵突击
 *
 * @note 只有在战场进行中才会处理任务
 */
void BattlegroundAV::HandleQuestComplete(uint32 questid, Player* player)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return; // 也许应该记录日志，因为这可能是作弊或严重bug
    uint8 team = GetTeamIndexByTeamId(player->GetTeam());
    /// @todo 添加声望、事件（包括任务不可用、下一个任务可用、NPC/GO刷新/消失）以及可能的荣誉
    TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed", questid);
    switch (questid)
    {
        // 护甲碎片任务 - 每次提交20个碎片
        case AV_QUEST_A_SCRAPS1:
        case AV_QUEST_A_SCRAPS2:
        case AV_QUEST_H_SCRAPS1:
        case AV_QUEST_H_SCRAPS2:
            m_Team_QuestStatus[team][0]+=20;
            // 每500点（约25次提交）升级一次墓地守卫
            if (m_Team_QuestStatus[team][0] == 500 || m_Team_QuestStatus[team][0] == 1000 || m_Team_QuestStatus[team][0] == 1500)
            {
                TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed starting with unit upgrading..", questid);
                // 升级所有己方控制的墓地的守卫
                for (BG_AV_Nodes i = BG_AV_NODES_FIRSTAID_STATION; i <= BG_AV_NODES_FROSTWOLF_HUT; ++i)
                    if (m_Nodes[i].Owner == player->GetTeam() && m_Nodes[i].State == POINT_CONTROLED)
                    {
                        DePopulateNode(i);
                        PopulateNode(i);
                            // 这可能有问题，因为会立即刷新所有墓地的生物
                     }
            }
            break;
        // 指挥官任务1 - 解锁第一个空军指挥官
        case AV_QUEST_A_COMMANDER1:
        case AV_QUEST_H_COMMANDER1:
            m_Team_QuestStatus[team][1]++;
            RewardReputationToTeam(team, 1, player->GetTeam());
            if (m_Team_QuestStatus[team][1] == 30)
                TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed (need to implement some events here", questid);
            break;
        // 指挥官任务2 - 解锁第二个空军指挥官
        case AV_QUEST_A_COMMANDER2:
        case AV_QUEST_H_COMMANDER2:
            m_Team_QuestStatus[team][2]++;
            RewardReputationToTeam(team, 1, player->GetTeam());
            if (m_Team_QuestStatus[team][2] == 60)
                TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed (need to implement some events here", questid);
            break;
        // 指挥官任务3 - 解锁第三个空军指挥官
        case AV_QUEST_A_COMMANDER3:
        case AV_QUEST_H_COMMANDER3:
            m_Team_QuestStatus[team][3]++;
            RewardReputationToTeam(team, 1, player->GetTeam());
            if (m_Team_QuestStatus[team][3] == 120)
                TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed (need to implement some events here", questid);
            break;
        // Boss相关任务 - 可以提交10个或1个物品
        case AV_QUEST_A_BOSS1:
        case AV_QUEST_H_BOSS1:
            m_Team_QuestStatus[team][4] += 9; // 提交10个物品时增加9点
            [[fallthrough]];
        case AV_QUEST_A_BOSS2:
        case AV_QUEST_H_BOSS2:
            m_Team_QuestStatus[team][4]++;
            if (m_Team_QuestStatus[team][4] >= 200)
                TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed (need to implement some events here", questid);
            break;
        // 附近矿坑任务
        case AV_QUEST_A_NEAR_MINE:
        case AV_QUEST_H_NEAR_MINE:
            m_Team_QuestStatus[team][5]++;
            if (m_Team_QuestStatus[team][5] == 28)
            {
                TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed (need to implement some events here", questid);
                if (m_Team_QuestStatus[team][6] == 7)
                    TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed (need to implement some events here - ground assault ready", questid);
            }
            break;
        // 另一个矿坑任务
        case AV_QUEST_A_OTHER_MINE:
        case AV_QUEST_H_OTHER_MINE:
            m_Team_QuestStatus[team][6]++;
            if (m_Team_QuestStatus[team][6] == 7)
            {
                TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed (need to implement some events here", questid);
                if (m_Team_QuestStatus[team][5] == 20)
                    TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed (need to implement some events here - ground assault ready", questid);
            }
            break;
        // 骑兵护甲任务
        case AV_QUEST_A_RIDER_HIDE:
        case AV_QUEST_H_RIDER_HIDE:
            m_Team_QuestStatus[team][7]++;
            if (m_Team_QuestStatus[team][7] == 25)
            {
                TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed (need to implement some events here", questid);
                if (m_Team_QuestStatus[team][8] == 25)
                    TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed (need to implement some events here - rider assault ready", questid);
            }
            break;
        // 骑兵驯服任务
        case AV_QUEST_A_RIDER_TAME:
        case AV_QUEST_H_RIDER_TAME:
            m_Team_QuestStatus[team][8]++;
            if (m_Team_QuestStatus[team][8] == 25)
            {
                TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed (need to implement some events here", questid);
                if (m_Team_QuestStatus[team][7] == 25)
                    TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed (need to implement some events here - rider assault ready", questid);
            }
            break;
        default:
            TC_LOG_DEBUG("bg.battleground", "BG_AV Quest {} completed but is not interesting at all", questid);
            return; // 不是我们关心的任务
            break;
    }
}

/**
 * @brief 更新阵营分数
 *
 * 更新指定阵营的资源分数。分数可以通过正数增加或负数减少。
 * 当某一方分数降至0时，另一方获胜。
 *
 * @param team 阵营ID（ALLIANCE或HORDE）
 * @param points 分数变化量（正数增加，负数减少）
 *
 * 特殊情况处理：
 * - 分数降至0以下时设为0并结束战场
 * - 分数降至120以下时广播即将失败警告
 *
 * @note 要移除资源，points必须为负数；要增加资源，points必须为正数
 */
void BattlegroundAV::UpdateScore(uint16 team, int16 points)
{
    ASSERT(team == ALLIANCE || team == HORDE);
    uint8 teamindex = GetTeamIndexByTeamId(team); // 0=联盟 1=部落
    m_Team_Scores[teamindex] += points;

    // 更新客户端世界状态显示
    UpdateWorldState(((teamindex == TEAM_HORDE)?AV_Horde_Score:AV_Alliance_Score), m_Team_Scores[teamindex]);

    // 只有减少分数时才检查胜负条件
    if (points < 0)
    {
        // 分数降至0或以下，该阵营失败
        if (m_Team_Scores[teamindex] < 1)
        {
            m_Team_Scores[teamindex]=0;
            EndBattleground(((teamindex == TEAM_HORDE)?ALLIANCE:HORDE));
        }
        // 分数降至120以下，广播即将失败警告
        else if (!m_IsInformedNearVictory[teamindex] && m_Team_Scores[teamindex] < SEND_MSG_NEAR_LOSE)
        {
            if (teamindex == TEAM_ALLIANCE)
                SendBroadcastText(BG_AV_TEXT_ALLIANCE_NEAR_LOSE, CHAT_MSG_BG_SYSTEM_ALLIANCE);
            else
                SendBroadcastText(BG_AV_TEXT_HORDE_NEAR_LOSE, CHAT_MSG_BG_SYSTEM_HORDE);
            PlaySoundToAll(AV_SOUND_NEAR_VICTORY);  // 播放即将胜利音效
            m_IsInformedNearVictory[teamindex] = true;  // 标记已通知
        }
    }
}

/**
 * @brief 添加奥特兰克山谷生物
 *
 * 在战场中生成指定的生物。支持动态生物和静态生物两种类型。
 * 动态生物会根据墓地/塔楼的状态刷新/消失，静态生物则始终存在。
 *
 * @param cinfoid 生物信息ID（指向生物模板数组）
 * @param type 生物位置类型索引
 * @return 生成的生物指针，失败返回nullptr
 *
 * 生物类型包括：
 * - 动态生物：墓地守卫、塔楼守卫、矿坑生物等
 * - 静态生物：队长、Boss、NPC等
 *
 * 特殊处理：
 * - 队长生物设置一天的复活时间
 * - 墓地守卫设置漫游距离
 * - 队长和Boss添加触发器用于战斗检测
 */
Creature* BattlegroundAV::AddAVCreature(uint16 cinfoid, uint16 type)
{
    bool isStatic = false;
    Creature* creature = nullptr;
    ASSERT(type < AV_CPLACE_MAX + AsUnderlyingType(AV_STATICCPLACE_MAX));

    // 静态生物（type >= AV_CPLACE_MAX）
    if (type >= AV_CPLACE_MAX)
    {
        type -= AV_CPLACE_MAX;
        cinfoid = uint16(BG_AV_StaticCreaturePos[type][4]);
        creature = AddCreature(BG_AV_StaticCreatureInfo[cinfoid],
                               type + AV_CPLACE_MAX,
                               BG_AV_StaticCreaturePos[type][0],
                               BG_AV_StaticCreaturePos[type][1],
                               BG_AV_StaticCreaturePos[type][2],
                               BG_AV_StaticCreaturePos[type][3]);
        isStatic = true;
    }
    else  // 动态生物
    {
        creature = AddCreature(BG_AV_CreatureInfo[cinfoid], type, BG_AV_CreaturePos[type]);
    }
    if (!creature)
        return nullptr;

    // 队长设置一天的复活时间（防止多次刷新）
    if (creature->GetEntry() == BG_AV_CreatureInfo[AV_NPC_A_CAPTAIN] || creature->GetEntry() == BG_AV_CreatureInfo[AV_NPC_H_CAPTAIN])
        creature->SetRespawnDelay(RESPAWN_ONE_DAY); /// @todo 检查是否可以通过数据库设置，也为飞行指挥官添加

    // 设置墓地守卫的漫游行为
    if ((isStatic && cinfoid >= 10 && cinfoid <= 14) || (!isStatic && (cinfoid <= AV_NPC_A_GRAVEDEFENSE3 || (cinfoid >= AV_NPC_H_GRAVEDEFENSE0 && cinfoid <= AV_NPC_H_GRAVEDEFENSE3))))
    {
        if (!isStatic && (cinfoid <= AV_NPC_A_GRAVEDEFENSE3 || (cinfoid >= AV_NPC_H_GRAVEDEFENSE0 && cinfoid <= AV_NPC_H_GRAVEDEFENSE3)))
        {
            CreatureData &data = sObjectMgr->NewOrExistCreatureData(creature->GetSpawnId());
            data.spawnGroupData = sObjectMgr->GetDefaultSpawnGroup();
            data.wander_distance = 5;  // 设置漫游距离为5码
        }
        // 否则漫游距离为15，生物最大移动10码
        creature->GetMotionMaster()->Initialize();
        creature->setDeathState(JUST_DIED);
        creature->Respawn();
        /// @todo 找到不杀生物就能添加motionmaster的方法
    }

    // 为队长和Boss添加触发器
    uint32 triggerSpawnID = 0;
    uint32 newFaction = 0;
    if (creature->GetEntry() == BG_AV_CreatureInfo[AV_NPC_A_CAPTAIN])
    {
        triggerSpawnID = AV_CPLACE_TRIGGER16;
        newFaction = 84;  // 联盟阵营
    }
    else if (creature->GetEntry() == BG_AV_CreatureInfo[AV_NPC_A_BOSS])
    {
        triggerSpawnID = AV_CPLACE_TRIGGER17;
        newFaction = 84;  // 联盟阵营
    }
    else if (creature->GetEntry() == BG_AV_CreatureInfo[AV_NPC_H_CAPTAIN])
    {
        triggerSpawnID = AV_CPLACE_TRIGGER18;
        newFaction = 83;  // 部落阵营
    }
    else if (creature->GetEntry() == BG_AV_CreatureInfo[AV_NPC_H_BOSS])
    {
        triggerSpawnID = AV_CPLACE_TRIGGER19;
        newFaction = 83;  // 部落阵营
    }

    // 添加触发器生物
    if (triggerSpawnID && newFaction)
    {
        if (Creature* trigger = AddCreature(WORLD_TRIGGER, triggerSpawnID, BG_AV_CreaturePos[triggerSpawnID]))
        {
            trigger->SetFaction(newFaction);
        }
    }

    return creature;
}

/**
 * @brief 战场更新实现（每帧调用）
 *
 * 处理战场进行中的各种定时事件，包括：
 * - 队长增益技能计时
 * - 矿坑资源累积
 * - 矿坑回收计时
 * - 节点（墓地/塔楼）争夺计时
 *
 * @param diff 距离上次更新的时间间隔（毫秒）
 *
 * 更新逻辑：
 * 1. 队长每2-6分钟为全队施放增益buff
 * 2. 矿坑每45秒为拥有方增加1点资源
 * 3. 矿坑在20分钟后自动回归中立
 * 4. 被争夺的节点在计时结束后被摧毁
 */
void BattlegroundAV::PostUpdateImpl(uint32 diff)
{
    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        // 处理双方阵营的队长增益计时（0=联盟，1=部落）
        for (uint8 i=0; i <= 1; i++)
        {
            if (!m_CaptainAlive[i])
                continue;
            if (m_CaptainBuffTimer[i] > diff)
                m_CaptainBuffTimer[i] -= diff;
            else
            {
                // 计时结束，施放增益buff
                if (i == 0)  // 联盟队长
                {
                    CastSpellOnTeam(AV_BUFF_A_CAPTAIN, ALLIANCE);
                    if (Creature* creature = GetBGCreature(AV_CPLACE_MAX + 61))
                        creature->AI()->DoAction(ACTION_BUFF_YELL);
                }
                else  // 部落队长
                {
                    CastSpellOnTeam(AV_BUFF_H_CAPTAIN, HORDE);
                    if (Creature* creature = GetBGCreature(AV_CPLACE_MAX + 59))
                        creature->AI()->DoAction(ACTION_BUFF_YELL);
                }
                // 设置下一次增益时间：2分钟（buff持续时间）+ 0-4分钟随机
                m_CaptainBuffTimer[i] = 120000 + urand(0, 4)* 60000;
                /// @todo 获取正确的时间
            }
        }

        // 处理矿坑资源累积和回收计时
        m_Mine_Timer -=diff;
        for (uint8 mine=0; mine <2; mine++)
        {
            if (m_Mine_Owner[mine] == ALLIANCE || m_Mine_Owner[mine] == HORDE)
            {
                // 矿坑计时结束时为拥有方增加1点资源
                if (m_Mine_Timer <= 0)
                    UpdateScore(m_Mine_Owner[mine], 1);

                // 检查矿坑回收计时
                if (m_Mine_Reclaim_Timer[mine] > diff)
                    m_Mine_Reclaim_Timer[mine] -= diff;
                else
                {
                    // 计时结束，矿坑回归中立
                    ChangeMineOwner(mine, AV_NEUTRAL_TEAM);
                }
            }
        }
        // 重置矿坑计时器（两个矿坑共用一个计时器）
        if (m_Mine_Timer <= 0)
            m_Mine_Timer = AV_MINE_TICK_TIMER;

        // 处理所有节点的争夺计时
        for (BG_AV_Nodes i = BG_AV_NODES_FIRSTAID_STATION; i < BG_AV_NODES_MAX; ++i)
            if (m_Nodes[i].State == POINT_ASSAULTED)
            {
                if (m_Nodes[i].Timer > diff)
                    m_Nodes[i].Timer -= diff;
                else
                     EventPlayerDestroyedPoint(i);  // 计时结束，节点被摧毁
            }
    }
}

/**
 * @brief 关闭战场入口大门（战斗开始前）
 *
 * 在战场初始化时关闭双方的入口大门，防止玩家提前离开起始区域。
 * 这些门会在战斗正式开始时打开。
 */
void BattlegroundAV::StartingEventCloseDoors()
{
    DoorClose(BG_AV_OBJECT_DOOR_A);  // 关闭联盟大门
    DoorClose(BG_AV_OBJECT_DOOR_H);  // 关闭部落大门
}

/**
 * @brief 打开战场入口大门（战斗开始）
 *
 * 战斗正式开始时调用，执行以下操作：
 * - 生成矿坑补给品
 * - 初始化矿坑所有权为中立
 * - 显示双方分数
 * - 打开双方大门
 * - 启动"奥特兰克闪击战"计时成就
 */
void BattlegroundAV::StartingEventOpenDoors()
{
    TC_LOG_DEBUG("bg.battleground", "BG_AV: start spawning mine stuff");
    // 生成北矿补给品
    for (uint16 i= BG_AV_OBJECT_MINE_SUPPLY_N_MIN; i <= BG_AV_OBJECT_MINE_SUPPLY_N_MAX; i++)
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
    // 生成南矿补给品
    for (uint16 i= BG_AV_OBJECT_MINE_SUPPLY_S_MIN; i <= BG_AV_OBJECT_MINE_SUPPLY_S_MAX; i++)
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
    // 初始化矿坑为中立状态
    for (uint8 mine = AV_NORTH_MINE; mine <= AV_SOUTH_MINE; mine++)
        ChangeMineOwner(mine, AV_NEUTRAL_TEAM, true);

    // 显示双方分数UI
    UpdateWorldState(AV_SHOW_H_SCORE, 1);
    UpdateWorldState(AV_SHOW_A_SCORE, 1);

    // 打开大门，战斗正式开始
    DoorOpen(BG_AV_OBJECT_DOOR_H);
    DoorOpen(BG_AV_OBJECT_DOOR_A);

    // 启动计时成就：奥特兰克闪击战
    StartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, BG_AV_EVENT_START_BATTLE);
}

/**
 * @brief 添加玩家到战场
 *
 * 当玩家进入战场时调用，创建玩家的分数记录对象。
 *
 * @param player 进入战场的玩家指针
 *
 * @note 如果玩家已在战场中（重连情况），不会创建新的分数记录
 */
void BattlegroundAV::AddPlayer(Player* player)
{
    bool const isInBattleground = IsPlayerInBattleground(player->GetGUID());
    Battleground::AddPlayer(player);
    // 只为新进入的玩家创建分数记录
    if (!isInBattleground)
        PlayerScores[player->GetGUID().GetCounter()] = new BattlegroundAVScore(player->GetGUID());
}

/**
 * @brief 结束战场
 *
 * 计算并发放战场结束时的奖励，包括：
 * - 存活塔楼的额外荣誉和声望
 * - 存活队长的额外荣誉和声望
 * 然后调用基类结束战场。
 *
 * @param winner 获胜阵营ID
 *
 * 奖励规则：
 * - 每座存活的己方塔楼：获得声望和相当于2次击杀的荣誉
 * - 存活的己方队长：获得声望和相当于2次击杀的荣誉
 */
void BattlegroundAV::EndBattleground(uint32 winner)
{
    // 计算双方阵营的额外击杀数和声望奖励
    uint8 kills[2] = {0, 0}; // 0 = 联盟 1 = 部落
    uint8 rep[2] = {0, 0};   // 0 = 联盟 1 = 部落

    // 遍历所有塔楼，计算存活塔楼奖励
    for (BG_AV_Nodes i = BG_AV_NODES_DUNBALDAR_SOUTH; i <= BG_AV_NODES_FROSTWOLF_WTOWER; ++i)
    {
            if (m_Nodes[i].State == POINT_CONTROLED)
            {
                if (m_Nodes[i].Owner == ALLIANCE)
                {
                    rep[0]   += BG_AV_REP_SURVIVING_TOWER;
                    kills[0] += BG_AV_KILL_SURVIVING_TOWER;
                }
                else
                {
                    rep[0]   += BG_AV_KILL_SURVIVING_TOWER;
                    kills[1] += BG_AV_KILL_SURVIVING_TOWER;
                }
            }
    }

    // 计算存活队长奖励
    for (int i = TEAM_ALLIANCE; i <= TEAM_HORDE; ++i)
    {
        if (m_CaptainAlive[i])
        {
            kills[i] += BG_AV_KILL_SURVIVING_CAPTAIN;
            rep[i]   += BG_AV_REP_SURVIVING_CAPTAIN;
        }
        // 发放声望奖励
        if (rep[i] != 0)
            RewardReputationToTeam(i == 0 ? 730 : 729, rep[i], i == 0 ? ALLIANCE : HORDE);
        // 发放荣誉奖励
        if (kills[i] != 0)
            RewardHonorToTeam(GetBonusHonorFromKill(kills[i]), i == 0 ? ALLIANCE : HORDE);
    }

    /// @todo 为所有攻击中的生物添加逃离战斗模式
    Battleground::EndBattleground(winner);
}

/**
 * @brief 移除玩家（玩家离开战场）
 *
 * 当玩家离开战场时调用，移除玩家身上的战场相关增益效果。
 *
 * @param player 离开战场的玩家指针
 * @param guid 玩家GUID（未使用）
 * @param team 玩家阵营（未使用）
 *
 * 移除的增益效果：
 * - AV_BUFF_ARMOR：护甲增益
 * - AV_BUFF_A_CAPTAIN：联盟队长增益
 * - AV_BUFF_H_CAPTAIN：部落队长增益
 */
void BattlegroundAV::RemovePlayer(Player* player, ObjectGuid /*guid*/, uint32 /*team*/)
{
   if (!player)
    {
        TC_LOG_ERROR("bg.battleground", "bg_AV no player at remove");
        return;
    }
    /// @todo 搜索更多buff
    player->RemoveAurasDueToSpell(AV_BUFF_ARMOR);
    player->RemoveAurasDueToSpell(AV_BUFF_A_CAPTAIN);
    player->RemoveAurasDueToSpell(AV_BUFF_H_CAPTAIN);
}

/**
 * @brief 处理区域触发器
 *
 * 当玩家触发特定区域时调用。主要用于处理战场出口传送门。
 *
 * @param player 触发区域的玩家
 * @param trigger 触发器ID
 *
 * 触发器ID说明：
 * - 95, 2608：联盟出口传送门（只有联盟玩家可以使用）
 * - 2606：部落出口传送门（只有部落玩家可以使用）
 * - 3326-3331：其他触发器（当前未实现）
 */
void BattlegroundAV::HandleAreaTrigger(Player* player, uint32 trigger)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    switch (trigger)
    {
        case 95:
        case 2608:  // 联盟出口传送门
            if (player->GetTeam() != ALLIANCE)
                player->GetSession()->SendAreaTriggerMessage("Only The Alliance can use that portal");
            else
                player->LeaveBattleground();
            break;
        case 2606:  // 部落出口传送门
            if (player->GetTeam() != HORDE)
                player->GetSession()->SendAreaTriggerMessage("Only The Horde can use that portal");
            else
                player->LeaveBattleground();
            break;
        case 3326:
        case 3327:
        case 3328:
        case 3329:
        case 3330:
        case 3331:
            // Source->Unmount();  // 可能需要下马
            break;
        default:
            Battleground::HandleAreaTrigger(player, trigger);
            break;
    }
}

/**
 * @brief 更新玩家分数
 *
 * 更新玩家在战场中的统计分数，并触发相应的成就进度更新。
 *
 * @param player 玩家指针
 * @param type 分数类型（进攻/防守墓地/塔楼等）
 * @param value 分数值
 * @param doAddHonor 是否添加荣誉
 * @return 更新成功返回true，失败返回false
 *
 * 成就更新：
 * - 进攻墓地：更新成就 AV_OBJECTIVE_ASSAULT_GRAVEYARD
 * - 防守墓地：更新成就 AV_OBJECTIVE_DEFEND_GRAVEYARD
 * - 进攻塔楼：更新成就 AV_OBJECTIVE_ASSAULT_TOWER
 * - 防守塔楼：更新成就 AV_OBJECTIVE_DEFEND_TOWER
 */
bool BattlegroundAV::UpdatePlayerScore(Player* player, uint32 type, uint32 value, bool doAddHonor)
{
    if (!Battleground::UpdatePlayerScore(player, type, value, doAddHonor))
        return false;

    switch (type)
    {
        case SCORE_GRAVEYARDS_ASSAULTED:
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE, AV_OBJECTIVE_ASSAULT_GRAVEYARD);
            break;
        case SCORE_GRAVEYARDS_DEFENDED:
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE, AV_OBJECTIVE_DEFEND_GRAVEYARD);
            break;
        case SCORE_TOWERS_ASSAULTED:
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE, AV_OBJECTIVE_ASSAULT_TOWER);
            break;
        case SCORE_TOWERS_DEFENDED:
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE, AV_OBJECTIVE_DEFEND_TOWER);
            break;
        default:
            break;
    }
    return true;
}

/**
 * @brief 处理节点被摧毁事件
 *
 * 当被争夺的节点（墓地或塔楼）计时结束时调用。
 * 塔楼被摧毁后消失并给敌方造成资源损失；
 * 墓地被摧毁后归属权转移给争夺方。
 *
 * @param node 被摧毁的节点ID
 *
 * 塔楼摧毁效果：
 * - 移除塔楼元帅（与Boss关联）
 * - 生成燃烧特效
 * - 敌方失去75点资源
 * - 争夺方获得声望和荣誉奖励
 *
 * 墓地摧毁效果：
 * - 刷新新的阵营旗帜
 * - 更新光环效果
 * - 填充墓地的守卫NPC
 * - 雪花墓地有特殊视觉效果
 */
void BattlegroundAV::EventPlayerDestroyedPoint(BG_AV_Nodes node)
{
    uint32 object = GetObjectThroughNode(node);
    TC_LOG_DEBUG("bg.battleground", "bg_av: player destroyed point node {} object {}", node, object);

    // 移除旗帜
    SpawnBGObject(object, RESPAWN_ONE_DAY);
    DestroyNode(node);
    UpdateNodeWorldState(node);

    uint32 owner = m_Nodes[node].Owner;
    if (IsTower(node))
    {
        uint8 tmp = node-BG_AV_NODES_DUNBALDAR_SOUTH;
        // 移除塔楼元帅（元帅与Boss关联，摧毁塔楼会削弱Boss）
        if (BgCreatures[AV_CPLACE_A_MARSHAL_SOUTH + tmp])
            DelCreature(AV_CPLACE_A_MARSHAL_SOUTH + tmp);
        else
            TC_LOG_ERROR("bg.battleground", "BG_AV: playerdestroyedpoint: marshal {} doesn't exist", AV_CPLACE_A_MARSHAL_SOUTH + tmp);
        // 生成燃烧特效
        for (uint8 i=0; i <= 9; i++)
            SpawnBGObject(BG_AV_OBJECT_BURN_DUNBALDAR_SOUTH + i + (tmp * 10), RESPAWN_IMMEDIATELY);

        // 敌方失去75点资源
        UpdateScore((owner == ALLIANCE) ? HORDE : ALLIANCE, -1 * BG_AV_RES_TOWER);
        // 争夺方获得声望和荣誉
        RewardReputationToTeam(owner == ALLIANCE ? 730 : 729, BG_AV_REP_TOWER, owner);
        RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_TOWER), owner);

        // 移除塔顶旗帜和光环
        SpawnBGObject(BG_AV_OBJECT_TAURA_A_DUNBALDAR_SOUTH + uint32(GetTeamIndexByTeamId(owner)) + (2 * tmp), RESPAWN_ONE_DAY);
        SpawnBGObject(BG_AV_OBJECT_TFLAG_A_DUNBALDAR_SOUTH + uint32(GetTeamIndexByTeamId(owner)) + (2 * tmp), RESPAWN_ONE_DAY);
    }
    else  // 墓地
    {
        // 刷新对应阵营的旗帜
        if (owner == ALLIANCE)
            SpawnBGObject(object-11, RESPAWN_IMMEDIATELY);
        else
            SpawnBGObject(object+11, RESPAWN_IMMEDIATELY);
        // 更新墓地光环效果
        SpawnBGObject(BG_AV_OBJECT_AURA_N_FIRSTAID_STATION + 3 * node, RESPAWN_ONE_DAY);
        SpawnBGObject(BG_AV_OBJECT_AURA_A_FIRSTAID_STATION + uint32(GetTeamIndexByTeamId(owner)) + 3 * node, RESPAWN_IMMEDIATELY);
        // 填充墓地守卫和灵魂医者
        PopulateNode(node);
        // 雪花墓地特殊视觉效果
        if (node == BG_AV_NODES_SNOWFALL_GRAVE)
        {
            for (uint8 i = 0; i < 4; i++)
            {
                SpawnBGObject(((owner == ALLIANCE)?BG_AV_OBJECT_SNOW_EYECANDY_PA : BG_AV_OBJECT_SNOW_EYECANDY_PH)+i, RESPAWN_ONE_DAY);
                SpawnBGObject(((owner == ALLIANCE)?BG_AV_OBJECT_SNOW_EYECANDY_A  : BG_AV_OBJECT_SNOW_EYECANDY_H)+i, RESPAWN_IMMEDIATELY);
            }
        }
    }

    // 广播节点被占领的消息
    if (StaticNodeInfo const* nodeInfo = GetStaticNodeInfo(node))
        if (Creature* herold = GetBGCreature(AV_CPLACE_HERALD))
            herold->AI()->Talk(owner == ALLIANCE ? nodeInfo->TextIds.AllianceCapture : nodeInfo->TextIds.HordeCapture);
}

/**
 * @brief 更改矿坑所有者
 *
 * 当矿坑Boss被击杀或矿坑回归中立时调用。
 * 更换矿坑所有权会移除原有生物并生成新所有者的生物。
 *
 * @param mine 矿坑ID（0=北矿，1=南矿）
 * @param team 新所有者阵营（ALLIANCE、HORDE或AV_NEUTRAL_TEAM）
 * @param initial 是否为初始设置（true时不移除生物）
 *
 * 矿坑系统说明：
 * - 北矿（铁深渊矿坑）：靠近联盟基地
 * - 南矿（冷牙矿坑）：靠近部落基地
 * - 占领矿坑后每45秒获得1点资源
 * - 20分钟后矿坑自动回归中立
 * - 中立势力会尝试重新夺回矿坑
 */
void BattlegroundAV::ChangeMineOwner(uint8 mine, uint32 team, bool initial)
{
    // mine=0 北矿 mine=1 南矿
    // 更改所有者会：设置当前生物的刷新时间为无限，
    // 生成新所有者的生物，更改宝箱对象使当前拥有方能使用
    ASSERT(mine == AV_NORTH_MINE || mine == AV_SOUTH_MINE);
    if (team != ALLIANCE && team != HORDE)
        team = AV_NEUTRAL_TEAM;

    if (m_Mine_Owner[mine] == team && !initial)
        return;
    m_Mine_PrevOwner[mine] = m_Mine_Owner[mine];
    m_Mine_Owner[mine] = team;

    // 非初始化时移除原有生物
    if (!initial)
    {
        TC_LOG_DEBUG("bg.battleground", "bg_av depopulating mine {} (0=north, 1=south)", mine);
        if (mine == AV_SOUTH_MINE)
            for (uint16 i=AV_CPLACE_MINE_S_S_MIN; i <= AV_CPLACE_MINE_S_S_MAX; i++)
                if (BgCreatures[i])
                    DelCreature(i); /// @todo 只需设置刷新时间为999999
        for (uint16 i=((mine == AV_NORTH_MINE)?AV_CPLACE_MINE_N_1_MIN:AV_CPLACE_MINE_S_1_MIN); i <= ((mine == AV_NORTH_MINE)?AV_CPLACE_MINE_N_3:AV_CPLACE_MINE_S_3); i++)
            if (BgCreatures[i])
                DelCreature(i); /// @todo 这里也一样
    }
    SendMineWorldStates(mine);

    TC_LOG_DEBUG("bg.battleground", "bg_av populating mine {} (0=north, 1=south)", mine);
    uint16 miner;
    // 中立势力也存在...很长时间后，中立势力会尝试夺回矿坑
    if (mine == AV_NORTH_MINE)
    {
        if (team == ALLIANCE)
            miner = AV_NPC_N_MINE_A_1;
        else if (team == HORDE)
            miner = AV_NPC_N_MINE_H_1;
        else
            miner = AV_NPC_N_MINE_N_1;
    }
    else
    {
        uint16 cinfo;
        if (team == ALLIANCE)
            miner = AV_NPC_S_MINE_A_1;
        else if (team == HORDE)
            miner = AV_NPC_S_MINE_H_1;
        else
            miner = AV_NPC_S_MINE_N_1;
       // 南矿有特殊的害虫生物
        TC_LOG_DEBUG("bg.battleground", "spawning vermin");
        if (team == ALLIANCE)
            cinfo = AV_NPC_S_MINE_A_3;
        else if (team == HORDE)
            cinfo = AV_NPC_S_MINE_H_3;
        else
            cinfo = AV_NPC_S_MINE_N_S;
        for (uint16 i=AV_CPLACE_MINE_S_S_MIN; i <= AV_CPLACE_MINE_S_S_MAX; i++)
            AddAVCreature(cinfo, i);
    }
    // 生成第一波矿工
    for (uint16 i=((mine == AV_NORTH_MINE)?AV_CPLACE_MINE_N_1_MIN:AV_CPLACE_MINE_S_1_MIN); i <= ((mine == AV_NORTH_MINE)?AV_CPLACE_MINE_N_1_MAX:AV_CPLACE_MINE_S_1_MAX); i++)
        AddAVCreature(miner, i);
    // 生成第二波矿工（随机选择两种类型之一）
    for (uint16 i=((mine == AV_NORTH_MINE)?AV_CPLACE_MINE_N_2_MIN:AV_CPLACE_MINE_S_2_MIN); i <= ((mine == AV_NORTH_MINE)?AV_CPLACE_MINE_N_2_MAX:AV_CPLACE_MINE_S_2_MAX); i++)
        AddAVCreature(miner+(urand(1, 2)), i);
    // 生成矿坑Boss
    AddAVCreature(miner+3, (mine == AV_NORTH_MINE)?AV_CPLACE_MINE_N_3:AV_CPLACE_MINE_S_3);

    // 如果被联盟或部落占领，设置回收计时器并广播消息
    if (team == ALLIANCE || team == HORDE)
    {
        m_Mine_Reclaim_Timer[mine]=AV_MINE_RECLAIM_TIMER;  // 20分钟

        if (Creature* herold = GetBGCreature(AV_CPLACE_HERALD))
        {
            if (mine == AV_NORTH_MINE)
                herold->AI()->Talk(team == ALLIANCE ? TEXT_IRONDEEP_MINE_ALLIANCE_TAKEN : TEXT_IRONDEEP_MINE_HORDE_TAKEN);
            else if (mine == AV_SOUTH_MINE)
                herold->AI()->Talk(team == ALLIANCE ? TEXT_COLDTOOTH_MINE_ALLIANCE_TAKEN : TEXT_COLDTOOTH_MINE_HORDE_TAKEN);
        }
    }
    else  // 回归中立
    {
        if (mine == AV_SOUTH_MINE)
        {
            if (Creature* creature = GetBGCreature(AV_CPLACE_MINE_S_3))
                creature->AI()->Talk(TEXT_SNIVVLE_RANDOM);
        }
    }
    return;
}

/**
 * @brief 检查游戏对象是否可以被激活
 *
 * 用于检查矿坑宝箱是否可以被特定阵营的玩家打开。
 *
 * @param GOId 游戏对象ID
 * @param team 玩家阵营
 * @return 如果可以激活返回true，否则返回false
 *
 * 只有拥有矿坑的阵营才能使用对应的矿坑宝箱。
 */
bool BattlegroundAV::CanActivateGO(int32 GOId, uint32 team) const
{
    if (GOId == BG_AV_OBJECTID_MINE_N)
         return (m_Mine_Owner[AV_NORTH_MINE] == team);
    if (GOId == BG_AV_OBJECTID_MINE_S)
         return (m_Mine_Owner[AV_SOUTH_MINE] == team);
    return true; // 非矿坑对象总是可以激活
}

/**
 * @brief 填充节点的NPC
 *
 * 根据节点所有者生成相应的守卫NPC。墓地在生成守卫的同时
 * 还会生成灵魂医者，塔楼只生成守卫。守卫的等级根据阵营的
 * 任务进度（护甲碎片提交数量）而提升。
 *
 * @param node 节点ID
 *
 * 守卫等级对应关系：
 * - 0-499：0级守卫（基础）
 * - 500-999：1级守卫
 * - 1000-1499：2级守卫
 * - 1500+：3级守卫（最强）
 */
void BattlegroundAV::PopulateNode(BG_AV_Nodes node)
{
    uint32 owner = m_Nodes[node].Owner;
    ASSERT(owner);

    uint32 c_place = AV_CPLACE_DEFENSE_STORM_AID + (4 * node);
    uint32 creatureid;
    if (IsTower(node))
        creatureid=(owner == ALLIANCE)?AV_NPC_A_TOWERDEFENSE:AV_NPC_H_TOWERDEFENSE;
    else
    {
        uint8 team2 = GetTeamIndexByTeamId(owner);
        // 根据护甲碎片任务进度决定守卫等级
        if (m_Team_QuestStatus[team2][0] < 500)
            creatureid = (owner == ALLIANCE)? AV_NPC_A_GRAVEDEFENSE0 : AV_NPC_H_GRAVEDEFENSE0;
        else if (m_Team_QuestStatus[team2][0] < 1000)
            creatureid = (owner == ALLIANCE)? AV_NPC_A_GRAVEDEFENSE1 : AV_NPC_H_GRAVEDEFENSE1;
        else if (m_Team_QuestStatus[team2][0] < 1500)
            creatureid = (owner == ALLIANCE)? AV_NPC_A_GRAVEDEFENSE2 : AV_NPC_H_GRAVEDEFENSE2;
        else
           creatureid = (owner == ALLIANCE)? AV_NPC_A_GRAVEDEFENSE3 : AV_NPC_H_GRAVEDEFENSE3;
        // 墓地需要生成灵魂医者
        if (BgCreatures[node])
            DelCreature(node);
        if (!AddSpiritGuide(node, BG_AV_CreaturePos[node], GetTeamIndexByTeamId(owner)))
            TC_LOG_ERROR("bg.battleground", "AV: couldn't spawn spiritguide at node {}", node);
    }
    // 生成4个守卫
    for (uint8 i=0; i<4; i++)
        AddAVCreature(creatureid, c_place+i);

    if (node >= BG_AV_NODES_MAX)// 安全检查
        return;
    // 获取或创建荣誉触发器生物
    Creature* trigger = GetBGCreature(node + 302, false); // 0-302 其他生物
    if (!trigger)
    {
       trigger = AddCreature(WORLD_TRIGGER,
                             node + 302,
                             BG_AV_CreaturePos[node + 302],
                             GetTeamIndexByTeamId(owner));
    }

    // 设置触发器的正确阵营
    if (trigger)
    {
        if (owner != ALLIANCE && owner != HORDE) // 节点可能是中立的，移除触发器
        {
            DelCreature(node + 302);
            return;
        }
        trigger->SetFaction(owner == ALLIANCE ? FACTION_ALLIANCE_GENERIC : FACTION_HORDE_GENERIC);
    }
}

/**
 * @brief 清空节点的NPC
 *
 * 移除节点上的所有守卫NPC和灵魂医者。当节点被争夺或
 * 所有者变更时调用。
 *
 * @param node 节点ID
 */
void BattlegroundAV::DePopulateNode(BG_AV_Nodes node)
{
    uint32 c_place = AV_CPLACE_DEFENSE_STORM_AID + (4 * node);
    // 移除所有守卫
    for (uint8 i=0; i<4; i++)
        if (BgCreatures[c_place+i])
            DelCreature(c_place+i);
    // 移除灵魂医者（仅墓地）
    if (!IsTower(node) && BgCreatures[node])
        DelCreature(node);

    // 节点丢失时移除荣誉光环触发器
    if (node < BG_AV_NODES_MAX) // 安全检查
        DelCreature(node + 302); // NULL检查在DelCreature中
}

/**
 * @brief 通过游戏对象ID获取对应的节点
 *
 * 根据旗帜对象的ID计算出对应的节点（墓地或塔楼）。
 * 这是一个逆向映射函数，与GetObjectThroughNode()对应。
 *
 * @param object 游戏对象ID
 * @return 对应的节点ID
 *
 * 对象ID与节点的映射关系比较复杂，因为存在多种旗帜类型：
 * - 联盟旗帜（0-10）
 * - 联盟争夺中旗帜（11-21）
 * - 部落争夺中旗帜（22-32）
 * - 部落旗帜（33-43）
 * - 中立雪花旗帜（44）
 */
BG_AV_Nodes BattlegroundAV::GetNodeThroughObject(uint32 object)
{
    TC_LOG_DEBUG("bg.battleground", "bg_AV getnodethroughobject {}", object);
    if (object <= BG_AV_OBJECT_FLAG_A_STONEHEART_BUNKER)
        return BG_AV_Nodes(object);
    if (object <= BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_HUT)
        return BG_AV_Nodes(object - 11);
    if (object <= BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_WTOWER)
        return BG_AV_Nodes(object - 7);
    if (object <= BG_AV_OBJECT_FLAG_C_H_STONEHEART_BUNKER)
        return BG_AV_Nodes(object -22);
    if (object <= BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT)
        return BG_AV_Nodes(object - 33);
    if (object <= BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER)
        return BG_AV_Nodes(object - 29);
    if (object == BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE)
        return BG_AV_NODES_SNOWFALL_GRAVE;
    TC_LOG_ERROR("bg.battleground", "BattlegroundAV: ERROR! GetPlace got a wrong object :(");
    ABORT();
    return BG_AV_Nodes(0);
}

/**
 * @brief 通过节点ID获取对应的游戏对象ID
 *
 * 根据节点当前的所有者和状态，计算出应该显示的旗帜对象ID。
 * 这是GetNodeThroughObject()的逆向函数。
 *
 * @param node 节点ID
 * @return 对应的游戏对象ID
 *
 * 映射规则：
 * - 联盟控制联盟墓地/塔楼：显示联盟旗帜（0-10）
 * - 联盟争夺联盟墓地：显示联盟争夺旗帜（11-21）
 * - 联盟争夺部落塔楼：显示联盟争夺旗帜（18-21）
 * - 部落控制部落墓地/塔楼：显示部落旗帜（33-43）
 * - 部落争夺部落墓地：显示部落争夺旗帜（22-32）
 * - 中立雪花墓地：显示中立旗帜（44）
 */
uint32 BattlegroundAV::GetObjectThroughNode(BG_AV_Nodes node)
{
    TC_LOG_DEBUG("bg.battleground", "bg_AV GetObjectThroughNode {}", node);
    if (m_Nodes[node].Owner == ALLIANCE)
    {
        if (m_Nodes[node].State == POINT_ASSAULTED)
        {
            if (node <= BG_AV_NODES_FROSTWOLF_HUT)
                return node+11;  // 联盟争夺中旗帜
            if (node >= BG_AV_NODES_ICEBLOOD_TOWER && node <= BG_AV_NODES_FROSTWOLF_WTOWER)
                return node+7;   // 联盟争夺部落塔楼
        }
        else if (m_Nodes[node].State == POINT_CONTROLED)
            if (node <= BG_AV_NODES_STONEHEART_BUNKER)
                return node;     // 联盟控制旗帜
    }
    else if (m_Nodes[node].Owner == HORDE)
    {
        if (m_Nodes[node].State == POINT_ASSAULTED)
        {
            if (node <= BG_AV_NODES_STONEHEART_BUNKER)
                return node+22;  // 部落争夺中旗帜
        }
        else if (m_Nodes[node].State == POINT_CONTROLED)
        {
            if (node <= BG_AV_NODES_FROSTWOLF_HUT)
                return node+33;  // 部落控制旗帜（墓地）
            if (node >= BG_AV_NODES_ICEBLOOD_TOWER && node <= BG_AV_NODES_FROSTWOLF_WTOWER)
                return node+29;  // 部落控制旗帜（塔楼）
        }
    }
    else if (m_Nodes[node].Owner == AV_NEUTRAL_TEAM)
        return BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE;  // 中立雪花旗帜
    TC_LOG_ERROR("bg.battleground", "BattlegroundAV: Error! GetPlaceNode couldn't resolve node {}", node);
    ABORT();
    return 0;
}

/**
 * @brief 处理玩家点击旗帜事件
 *
 * 当玩家点击战场中的旗帜（墓地或塔楼的旗帜）时调用。
 * 根据旗帜类型判断是进攻还是防守操作。
 *
 * @param source 点击旗帜的玩家
 * @param target_obj 被点击的游戏对象（旗帜）
 *
 * 旗帜类型：
 * - 普通旗帜：可以进攻（占领）
 * - 争夺中旗帜：可以防守（夺回）
 */
void BattlegroundAV::EventPlayerClickedOnFlag(Player* source, GameObject* target_obj)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;
    int32 object = GetObjectType(target_obj->GetGUID());
    TC_LOG_DEBUG("bg.battleground", "BG_AV using gameobject {} with type {}", target_obj->GetEntry(), object);
    if (object < 0)
        return;
    switch (target_obj->GetEntry())
    {
        case BG_AV_OBJECTID_BANNER_A:            // 联盟旗帜
        case BG_AV_OBJECTID_BANNER_A_B:          // 联盟墓地旗帜
        case BG_AV_OBJECTID_BANNER_H:            // 部落旗帜
        case BG_AV_OBJECTID_BANNER_H_B:          // 部落墓地旗帜
        case BG_AV_OBJECTID_BANNER_SNOWFALL_N:   // 中立雪花旗帜
            EventPlayerAssaultsPoint(source, object);  // 进攻
            break;
        case BG_AV_OBJECTID_BANNER_CONT_A:       // 联盟争夺中旗帜
        case BG_AV_OBJECTID_BANNER_CONT_A_B:     // 联盟墓地争夺中旗帜
        case BG_AV_OBJECTID_BANNER_CONT_H:       // 部落争夺中旗帜
        case BG_AV_OBJECTID_BANNER_CONT_H_B:     // 部落墓地争夺中旗帜
            EventPlayerDefendsPoint(source, object);    // 防守
            break;
        default:
            break;
    }
}

/**
 * @brief 处理玩家防守节点事件
 *
 * 当玩家点击己方被争夺的旗帜时调用，表示防守成功，
 * 节点回归原所有者控制。
 *
 * @param player 防守的玩家
 * @param object 旗帜对象ID
 *
 * 防守成功的条件：
 * - 节点当前处于被争夺状态
 * - 玩家阵营是节点的原所有者
 * - 节点有明确的总所有者（非中立）
 *
 * 特殊情况：
 * - 雪花墓地在没有总所有者时按进攻逻辑处理
 */
void BattlegroundAV::EventPlayerDefendsPoint(Player* player, uint32 object)
{
    ASSERT(GetStatus() == STATUS_IN_PROGRESS);
    BG_AV_Nodes node = GetNodeThroughObject(object);

    uint32 owner = m_Nodes[node].Owner; // 当前所有者（实际上是争夺者）
    uint32 team = player->GetTeam();

    // 检查是否可以防守
    if (owner == player->GetTeam() || m_Nodes[node].State != POINT_ASSAULTED)
        return;
    // 如果节点没有总所有者（如雪花墓地），按进攻处理
    if (m_Nodes[node].TotalOwner == AV_NEUTRAL_TEAM)
    {
        ASSERT(node == BG_AV_NODES_SNOWFALL_GRAVE); // 当前唯一的中立墓地
        EventPlayerAssaultsPoint(player, object);
        return;
    }
    TC_LOG_DEBUG("bg.battleground", "player defends point object: {} node: {}", object, node);
    if (m_Nodes[node].PrevOwner != team)
    {
        TC_LOG_ERROR("bg.battleground", "BG_AV: player defends point which doesn't belong to his team {}", node);
        return;
    }

    // 刷新对应阵营的旗帜
    if (m_Nodes[node].Owner == ALLIANCE)
        SpawnBGObject(object+22, RESPAWN_IMMEDIATELY); // 生成部落旗帜
    else
        SpawnBGObject(object-22, RESPAWN_IMMEDIATELY); // 生成联盟旗帜

    if (!IsTower(node))
    {
        // 更新墓地光环
        SpawnBGObject(BG_AV_OBJECT_AURA_N_FIRSTAID_STATION + 3 * node, RESPAWN_ONE_DAY);
        SpawnBGObject(BG_AV_OBJECT_AURA_A_FIRSTAID_STATION + uint32(GetTeamIndexByTeamId(team)) + 3 * node, RESPAWN_IMMEDIATELY);
    }
    // 移除旧的旗帜
    SpawnBGObject(object, RESPAWN_ONE_DAY);

    // 更新节点状态
    DefendNode(node, team);
    PopulateNode(node);
    UpdateNodeWorldState(node);

    if (IsTower(node))
    {
        // 塔楼：刷新塔顶的大旗帜和光环
        SpawnBGObject(BG_AV_OBJECT_TAURA_A_DUNBALDAR_SOUTH+(2*(node-BG_AV_NODES_DUNBALDAR_SOUTH)), (team == ALLIANCE)? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
        SpawnBGObject(BG_AV_OBJECT_TAURA_H_DUNBALDAR_SOUTH+(2*(node-BG_AV_NODES_DUNBALDAR_SOUTH)), (team == HORDE)? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
        SpawnBGObject(BG_AV_OBJECT_TFLAG_A_DUNBALDAR_SOUTH+(2*(node-BG_AV_NODES_DUNBALDAR_SOUTH)), (team == ALLIANCE)? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
        SpawnBGObject(BG_AV_OBJECT_TFLAG_H_DUNBALDAR_SOUTH+(2*(node-BG_AV_NODES_DUNBALDAR_SOUTH)), (team == HORDE)? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
    }
    else if (node == BG_AV_NODES_SNOWFALL_GRAVE)
    {
        // 雪花墓地特殊视觉效果
        for (uint8 i = 0; i < 4; i++)
        {
            SpawnBGObject(((owner == ALLIANCE)?BG_AV_OBJECT_SNOW_EYECANDY_PA : BG_AV_OBJECT_SNOW_EYECANDY_PH)+i, RESPAWN_ONE_DAY);
            SpawnBGObject(((team == ALLIANCE)?BG_AV_OBJECT_SNOW_EYECANDY_A : BG_AV_OBJECT_SNOW_EYECANDY_H)+i, RESPAWN_IMMEDIATELY);
        }
    }

    // 广播防守成功消息
    if (StaticNodeInfo const* nodeInfo = GetStaticNodeInfo(node))
        if (Creature* herold = GetBGCreature(AV_CPLACE_HERALD))
            herold->AI()->Talk(team == ALLIANCE ? nodeInfo->TextIds.AllianceCapture : nodeInfo->TextIds.HordeCapture);

    // 更新玩家统计数据
    UpdatePlayerScore(player, IsTower(node) ? SCORE_TOWERS_DEFENDED : SCORE_GRAVEYARDS_DEFENDED, 1);
}

/**
 * @brief 处理玩家进攻节点事件
 *
 * 当玩家点击敌方或中立旗帜时调用，开始争夺该节点。
 * 节点进入争夺状态后需要等待计时结束才会真正被占领。
 *
 * @param player 进攻的玩家
 * @param object 旗帜对象ID
 *
 * 进攻条件：
 * - 玩家不能是当前节点所有者
 * - 玩家不能是节点的总所有者
 *
 * 特殊情况：
 * - 雪花墓地初始争夺需要特殊处理
 * - 雪花墓地重新争夺（无总所有者时）也需要特殊处理
 */
void BattlegroundAV::EventPlayerAssaultsPoint(Player* player, uint32 object)
{
    ASSERT(GetStatus() == STATUS_IN_PROGRESS);

    BG_AV_Nodes node = GetNodeThroughObject(object);
    uint32 owner = m_Nodes[node].Owner; // 当前所有者
    uint32 team  = player->GetTeam();
    TC_LOG_DEBUG("bg.battleground", "bg_av: player assaults point object {} node {}", object, node);
    // 检查是否可以进攻（不能进攻己方节点）
    if (owner == team || team == m_Nodes[node].TotalOwner)
        return; // 可能是GM使用了该对象

    // 雪花墓地特殊处理
    if (node == BG_AV_NODES_SNOWFALL_GRAVE)
    {
        if (object == BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE) // 初始争夺
        {
            if (!(owner == AV_NEUTRAL_TEAM && m_Nodes[node].TotalOwner == AV_NEUTRAL_TEAM))
                return;

            if (team == ALLIANCE)
                SpawnBGObject(BG_AV_OBJECT_FLAG_C_A_SNOWFALL_GRAVE, RESPAWN_IMMEDIATELY);
            else
                SpawnBGObject(BG_AV_OBJECT_FLAG_C_H_SNOWFALL_GRAVE, RESPAWN_IMMEDIATELY);
            SpawnBGObject(BG_AV_OBJECT_AURA_N_FIRSTAID_STATION+3*node, RESPAWN_IMMEDIATELY); // 中立光环
        }
        else if (m_Nodes[node].TotalOwner == AV_NEUTRAL_TEAM) // 重新争夺（无总所有者）
        {
            if (!(m_Nodes[node].State != POINT_CONTROLED))
                return;

            if (team == ALLIANCE)
                SpawnBGObject(object-11, RESPAWN_IMMEDIATELY);
            else
                SpawnBGObject(object+11, RESPAWN_IMMEDIATELY);
        }
        // 雪花墓地视觉效果
        uint32 spawn, despawn;
        if (team == ALLIANCE)
        {
            despawn = (m_Nodes[node].State == POINT_ASSAULTED)?BG_AV_OBJECT_SNOW_EYECANDY_PH : BG_AV_OBJECT_SNOW_EYECANDY_H;
            spawn = BG_AV_OBJECT_SNOW_EYECANDY_PA;
        }
        else
        {
            despawn = (m_Nodes[node].State == POINT_ASSAULTED)?BG_AV_OBJECT_SNOW_EYECANDY_PA : BG_AV_OBJECT_SNOW_EYECANDY_A;
            spawn = BG_AV_OBJECT_SNOW_EYECANDY_PH;
        }
        for (uint8 i = 0; i < 4; i++)
        {
            SpawnBGObject(despawn+i, RESPAWN_ONE_DAY);
            SpawnBGObject(spawn+i, RESPAWN_IMMEDIATELY);
        }
    }

    // 非雪花墓地或雪花墓地已有总所有者的情况
    if (m_Nodes[node].TotalOwner != AV_NEUTRAL_TEAM)
    {
        ASSERT(m_Nodes[node].Owner != AV_NEUTRAL_TEAM);
        if (team == ALLIANCE)
            SpawnBGObject(object-22, RESPAWN_IMMEDIATELY);
        else
            SpawnBGObject(object+22, RESPAWN_IMMEDIATELY);
        if (IsTower(node))
        {
            // 塔楼：刷新塔顶大旗帜和光环
            SpawnBGObject(BG_AV_OBJECT_TAURA_A_DUNBALDAR_SOUTH+(2*(node-BG_AV_NODES_DUNBALDAR_SOUTH)), (team == ALLIANCE)? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
            SpawnBGObject(BG_AV_OBJECT_TAURA_H_DUNBALDAR_SOUTH+(2*(node-BG_AV_NODES_DUNBALDAR_SOUTH)), (team == HORDE)? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
            SpawnBGObject(BG_AV_OBJECT_TFLAG_A_DUNBALDAR_SOUTH+(2*(node-BG_AV_NODES_DUNBALDAR_SOUTH)), (team == ALLIANCE)? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
            SpawnBGObject(BG_AV_OBJECT_TFLAG_H_DUNBALDAR_SOUTH+(2*(node-BG_AV_NODES_DUNBALDAR_SOUTH)), (team == HORDE)? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
        }
        else
        {
            // 墓地：更新光环效果
            SpawnBGObject(BG_AV_OBJECT_AURA_N_FIRSTAID_STATION + 3 * node, RESPAWN_IMMEDIATELY); // 中立光环
            SpawnBGObject(BG_AV_OBJECT_AURA_A_FIRSTAID_STATION + uint32(GetTeamIndexByTeamId(owner)) + 3 * node, RESPAWN_ONE_DAY); // 移除阵营光环

            // 重新安置死亡的玩家（灵魂医者已移除）
            RelocateDeadPlayers(BgCreatures[node]);
        }
        // 清空节点NPC
        DePopulateNode(node);
    }

    // 移除旧旗帜，更新节点状态
    SpawnBGObject(object, RESPAWN_ONE_DAY);
    AssaultNode(node, team);
    UpdateNodeWorldState(node);

    // 广播进攻消息
    if (StaticNodeInfo const* nodeInfo = GetStaticNodeInfo(node))
        if (Creature* herold = GetBGCreature(AV_CPLACE_HERALD))
            herold->AI()->Talk(team == ALLIANCE ? nodeInfo->TextIds.AllianceAttack : nodeInfo->TextIds.HordeAttack);

    // 更新玩家统计数据
    UpdatePlayerScore(player, (IsTower(node)) ? SCORE_TOWERS_ASSAULTED : SCORE_GRAVEYARDS_ASSAULTED, 1);
}

/**
 * @brief 填充初始世界状态数据
 *
 * 当玩家进入战场时，将当前战场状态打包发送给客户端。
 * 包括所有节点的所有权状态、双方分数、矿坑状态等。
 *
 * @param packet 世界状态数据包引用
 *
 * 发送的数据包括：
 * - 每个节点的联盟进攻/控制状态
 * - 每个节点的部落进攻/控制状态
 * - 雪花墓地中立状态
 * - 双方阵营的当前分数
 * - 是否显示分数UI
 * - 矿坑所有权状态
 */
void BattlegroundAV::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    // 遍历所有节点，发送节点状态
    for (uint8 itr = BG_AV_NODES_FIRSTAID_STATION; itr < BG_AV_NODES_MAX; ++itr)
    {
        uint16 owner = m_Nodes[itr].Owner;
        BG_AV_States state = m_Nodes[itr].State;

        packet.Worldstates.emplace_back(BGAVNodeInfo[itr].WorldStateIds.AllianceAssault, (owner == ALLIANCE && state == POINT_ASSAULTED) ? 1 : 0);
        packet.Worldstates.emplace_back(BGAVNodeInfo[itr].WorldStateIds.AllianceControl, (owner == ALLIANCE && state >= POINT_DESTROYED) ? 1 : 0);
        packet.Worldstates.emplace_back(BGAVNodeInfo[itr].WorldStateIds.HordeAssault, (owner == HORDE && state == POINT_ASSAULTED) ? 1 : 0);
        packet.Worldstates.emplace_back(BGAVNodeInfo[itr].WorldStateIds.HordeControl, (owner == HORDE && state >= POINT_DESTROYED) ? 1 : 0);
    }

    // 雪花墓地特殊状态
    packet.Worldstates.emplace_back(AV_SNOWFALL_N, (m_Nodes[BG_AV_NODES_SNOWFALL_GRAVE].Owner == AV_NEUTRAL_TEAM ? 1 : 0));
    // 双方分数
    packet.Worldstates.emplace_back(AV_Alliance_Score, m_Team_Scores[0]);
    packet.Worldstates.emplace_back(AV_Horde_Score, m_Team_Scores[1]);

    // 只有游戏开始后才显示分数UI
    if (GetStatus() == STATUS_IN_PROGRESS) {
        packet.Worldstates.emplace_back(AV_SHOW_A_SCORE, 1);
        packet.Worldstates.emplace_back(AV_SHOW_H_SCORE, 1);
    }
    else
    {
        packet.Worldstates.emplace_back(AV_SHOW_A_SCORE, 0);
        packet.Worldstates.emplace_back(AV_SHOW_H_SCORE, 0);
    }

    // 发送矿坑状态
    SendMineWorldStates(AV_NORTH_MINE);
    SendMineWorldStates(AV_SOUTH_MINE);
}

/**
 * @brief 更新节点世界状态
 *
 * 当节点状态发生变化时，更新客户端的世界状态显示。
 *
 * @param node 节点ID
 *
 * 更新的状态包括：
 * - 联盟进攻状态
 * - 联盟控制状态
 * - 部落进攻状态
 * - 部落控制状态
 * - 雪花墓地中立状态（特殊处理）
 */
void BattlegroundAV::UpdateNodeWorldState(BG_AV_Nodes node)
{
    if (StaticNodeInfo const* nodeInfo = GetStaticNodeInfo(node))
    {
        uint16 owner = m_Nodes[node].Owner;
        BG_AV_States state = m_Nodes[node].State;

        UpdateWorldState(nodeInfo->WorldStateIds.AllianceAssault, owner == ALLIANCE && state == POINT_ASSAULTED);
        UpdateWorldState(nodeInfo->WorldStateIds.AllianceControl, owner == ALLIANCE && state >= POINT_DESTROYED);
        UpdateWorldState(nodeInfo->WorldStateIds.HordeAssault, owner == HORDE && state == POINT_ASSAULTED);
        UpdateWorldState(nodeInfo->WorldStateIds.HordeControl, owner == HORDE && state >= POINT_DESTROYED);
    }

    // 雪花墓地特殊状态
    if (node == BG_AV_NODES_SNOWFALL_GRAVE)
        UpdateWorldState(AV_SNOWFALL_N, m_Nodes[node].Owner == AV_NEUTRAL_TEAM);
}

/**
 * @brief 发送矿坑世界状态
 *
 * 更新客户端的矿坑所有权显示。矿坑有三种状态：
 * 联盟控制、部落控制、中立。
 *
 * @param mine 矿坑ID（AV_NORTH_MINE或AV_SOUTH_MINE）
 *
 * 世界状态映射：
 * - 0：联盟控制
 * - 1：中立
 * - 2：部落控制
 */
void BattlegroundAV::SendMineWorldStates(uint32 mine)
{
    ASSERT(mine == AV_NORTH_MINE || mine == AV_SOUTH_MINE);

    uint8 owner, prevowner, mine2; // 用于访问世界状态数组的变量
    mine2 = (mine == AV_NORTH_MINE)?0:1;
    // 将阵营ID映射到数组索引
    if (m_Mine_PrevOwner[mine] == ALLIANCE)
        prevowner = 0;
    else if (m_Mine_PrevOwner[mine] == HORDE)
        prevowner = 2;
    else
        prevowner = 1;
    if (m_Mine_Owner[mine] == ALLIANCE)
        owner = 0;
    else if (m_Mine_Owner[mine] == HORDE)
        owner = 2;
    else
        owner = 1;

    // 更新世界状态
    UpdateWorldState(BG_AV_MineWorldStates[mine2][owner], 1);
    if (prevowner != owner)
        UpdateWorldState(BG_AV_MineWorldStates[mine2][prevowner], 0);
}

/**
 * @brief 获取最近的墓地
 *
 * 当玩家死亡后，计算最近的可用复活墓地。
 * 只考虑己方控制（非争夺状态）的墓地。
 *
 * @param player 需要复活的玩家
 * @return 最近墓地的世界安全位置条目
 *
 * 选择逻辑：
 * 1. 首先设置默认墓地（阵营初始墓地）
 * 2. 遍历所有己方控制的墓地
 * 3. 选择距离最近的墓地
 */
WorldSafeLocsEntry const* BattlegroundAV::GetClosestGraveyard(Player* player)
{
    WorldSafeLocsEntry const* pGraveyard = nullptr;
    WorldSafeLocsEntry const* entry = nullptr;
    float dist = 0;
    float minDist = 0;
    float x, y;

    player->GetPosition(x, y);

    // 设置默认墓地（阵营初始墓地）
    pGraveyard = sWorldSafeLocsStore.LookupEntry(BG_AV_GraveyardIds[GetTeamIndexByTeamId(player->GetTeam())+7]);
    minDist = (pGraveyard->Loc.X - x)*(pGraveyard->Loc.X - x)+(pGraveyard->Loc.Y - y)*(pGraveyard->Loc.Y - y);

    // 遍历所有己方控制的墓地，寻找最近的
    for (uint8 i = BG_AV_NODES_FIRSTAID_STATION; i <= BG_AV_NODES_FROSTWOLF_HUT; ++i)
        if (m_Nodes[i].Owner == player->GetTeam() && m_Nodes[i].State == POINT_CONTROLED)
        {
            entry = sWorldSafeLocsStore.LookupEntry(BG_AV_GraveyardIds[i]);
            if (entry)
            {
                dist = (entry->Loc.X - x)*(entry->Loc.X - x)+(entry->Loc.Y - y)*(entry->Loc.Y - y);
                if (dist < minDist)
                {
                    minDist = dist;
                    pGraveyard = entry;
                }
            }
        }
    return pGraveyard;
}

/**
 * @brief 设置战场
 *
 * 初始化战场的所有游戏对象和生物。这是战场创建时调用的核心函数，
 * 负责生成所有的旗帜、光环、大门、矿坑补给品、燃烧特效等。
 *
 * @return 成功返回true，失败返回false
 *
 * 初始化内容包括：
 * 1. 双方入口大门
 * 2. 所有节点的旗帜和光环（墓地和塔楼）
 * 3. 塔楼顶部的旗帜和光环
 * 4. 节点被摧毁时的燃烧特效
 * 5. 队长建筑的烟雾和火焰特效
 * 6. 矿坑补给品
 * 7. 雪花墓地的视觉效果
 * 8. 所有初始NPC（守卫、队长、Boss、元帅等）
 *
 * @note 此函数失败会导致战场创建失败
 */
bool BattlegroundAV::SetupBattleground()
{
    // 创建起始大门对象
    if (// alliance gates
        !AddObject(BG_AV_OBJECT_DOOR_A, BG_AV_OBJECTID_GATE_A, BG_AV_DoorPositons[0], BG_AV_DoorRotation[0].x, BG_AV_DoorRotation[0].y, BG_AV_DoorRotation[0].z, BG_AV_DoorRotation[0].w, RESPAWN_IMMEDIATELY)
        // horde gates
        || !AddObject(BG_AV_OBJECT_DOOR_H, BG_AV_OBJECTID_GATE_H, BG_AV_DoorPositons[1], BG_AV_DoorRotation[1].x, BG_AV_DoorRotation[1].y, BG_AV_DoorRotation[1].z, BG_AV_DoorRotation[1].w, RESPAWN_IMMEDIATELY))
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundAV: Failed to spawn some object Battleground not created!1");
        return false;
    }

    // 生成节点对象（旗帜和光环）
    for (uint8 i = BG_AV_NODES_FIRSTAID_STATION; i < BG_AV_NODES_MAX; ++i)
    {
        if (i <= BG_AV_NODES_FROSTWOLF_HUT)
        {
            // 墓地节点：生成联盟旗帜、联盟争夺旗帜、部落旗帜、部落争夺旗帜和三种光环
            if (!AddObject(i, BG_AV_OBJECTID_BANNER_A_B,
                           BG_AV_ObjectPos[i],
                           0, 0, std::sin(BG_AV_ObjectPos[i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i].GetOrientation()/2), RESPAWN_ONE_DAY)
                || !AddObject(i+11, BG_AV_OBJECTID_BANNER_CONT_A_B,
                              BG_AV_ObjectPos[i],
                              0, 0, std::sin(BG_AV_ObjectPos[i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i].GetOrientation()/2), RESPAWN_ONE_DAY)
                || !AddObject(i+33, BG_AV_OBJECTID_BANNER_H_B,
                              BG_AV_ObjectPos[i],
                              0, 0, std::sin(BG_AV_ObjectPos[i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i].GetOrientation()/2), RESPAWN_ONE_DAY)
                || !AddObject(i+22, BG_AV_OBJECTID_BANNER_CONT_H_B,
                              BG_AV_ObjectPos[i],
                              0, 0, std::sin(BG_AV_ObjectPos[i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i].GetOrientation()/2), RESPAWN_ONE_DAY)
                // 墓地光环（中立、联盟、部落）
                || !AddObject(BG_AV_OBJECT_AURA_N_FIRSTAID_STATION+i*3, BG_AV_OBJECTID_AURA_N,
                              BG_AV_ObjectPos[i],
                              0, 0, std::sin(BG_AV_ObjectPos[i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i].GetOrientation()/2), RESPAWN_ONE_DAY)
                || !AddObject(BG_AV_OBJECT_AURA_A_FIRSTAID_STATION+i*3, BG_AV_OBJECTID_AURA_A,
                              BG_AV_ObjectPos[i],
                              0, 0, std::sin(BG_AV_ObjectPos[i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i].GetOrientation()/2), RESPAWN_ONE_DAY)
                || !AddObject(BG_AV_OBJECT_AURA_H_FIRSTAID_STATION+i*3, BG_AV_OBJECTID_AURA_H,
                              BG_AV_ObjectPos[i],
                              0, 0, std::sin(BG_AV_ObjectPos[i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i].GetOrientation()/2), RESPAWN_ONE_DAY))
            {
                TC_LOG_ERROR("bg.battleground", "BatteGroundAV: Failed to spawn some object Battleground not created!2");
                return false;
            }
        }
        else //towers
        {
            if (i <= BG_AV_NODES_STONEHEART_BUNKER) //alliance towers
            {
                if (!AddObject(i, BG_AV_OBJECTID_BANNER_A,
                               BG_AV_ObjectPos[i],
                               0, 0, std::sin(BG_AV_ObjectPos[i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i].GetOrientation()/2), RESPAWN_ONE_DAY)
                    || !AddObject(i+22, BG_AV_OBJECTID_BANNER_CONT_H,
                                  BG_AV_ObjectPos[i],
                                  0, 0, std::sin(BG_AV_ObjectPos[i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i].GetOrientation()/2), RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TAURA_A_DUNBALDAR_SOUTH+(2*(i-BG_AV_NODES_DUNBALDAR_SOUTH)), BG_AV_OBJECTID_AURA_A,
                                  BG_AV_ObjectPos[i+8],
                                  0, 0, std::sin(BG_AV_ObjectPos[i+8].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i+8].GetOrientation()/2), RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TAURA_H_DUNBALDAR_SOUTH+(2*(i-BG_AV_NODES_DUNBALDAR_SOUTH)), BG_AV_OBJECTID_AURA_N,
                                  BG_AV_ObjectPos[i+8],
                                  0, 0, std::sin(BG_AV_ObjectPos[i+8].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i+8].GetOrientation()/2), RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TFLAG_A_DUNBALDAR_SOUTH+(2*(i-BG_AV_NODES_DUNBALDAR_SOUTH)), BG_AV_OBJECTID_TOWER_BANNER_A,
                                  BG_AV_ObjectPos[i+8],
                                  0, 0, std::sin(BG_AV_ObjectPos[i+8].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i+8].GetOrientation()/2), RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TFLAG_H_DUNBALDAR_SOUTH+(2*(i-BG_AV_NODES_DUNBALDAR_SOUTH)), BG_AV_OBJECTID_TOWER_BANNER_PH,
                                  BG_AV_ObjectPos[i+8],
                                  0, 0, std::sin(BG_AV_ObjectPos[i+8].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i+8].GetOrientation()/2), RESPAWN_ONE_DAY))
                {
                    TC_LOG_ERROR("bg.battleground", "BatteGroundAV: Failed to spawn some object Battleground not created!3");
                    return false;
                }
            }
            else //horde towers
            {
                if (!AddObject(i+7, BG_AV_OBJECTID_BANNER_CONT_A,
                               BG_AV_ObjectPos[i],
                               0, 0, std::sin(BG_AV_ObjectPos[i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i].GetOrientation()/2), RESPAWN_ONE_DAY)
                    || !AddObject(i+29, BG_AV_OBJECTID_BANNER_H,
                                  BG_AV_ObjectPos[i],
                                  0, 0, std::sin(BG_AV_ObjectPos[i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i].GetOrientation()/2), RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TAURA_A_DUNBALDAR_SOUTH+(2*(i-BG_AV_NODES_DUNBALDAR_SOUTH)), BG_AV_OBJECTID_AURA_N,
                                  BG_AV_ObjectPos[i+8],
                                  0, 0, std::sin(BG_AV_ObjectPos[i+8].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i+8].GetOrientation()/2), RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TAURA_H_DUNBALDAR_SOUTH+(2*(i-BG_AV_NODES_DUNBALDAR_SOUTH)), BG_AV_OBJECTID_AURA_H,
                                  BG_AV_ObjectPos[i+8],
                                  0, 0, std::sin(BG_AV_ObjectPos[i+8].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i+8].GetOrientation()/2), RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TFLAG_A_DUNBALDAR_SOUTH+(2*(i-BG_AV_NODES_DUNBALDAR_SOUTH)), BG_AV_OBJECTID_TOWER_BANNER_PA,
                                  BG_AV_ObjectPos[i+8],
                                  0, 0, std::sin(BG_AV_ObjectPos[i+8].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i+8].GetOrientation()/2), RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TFLAG_H_DUNBALDAR_SOUTH+(2*(i-BG_AV_NODES_DUNBALDAR_SOUTH)), BG_AV_OBJECTID_TOWER_BANNER_H,
                                  BG_AV_ObjectPos[i+8],
                                  0, 0, std::sin(BG_AV_ObjectPos[i+8].GetOrientation()/2), std::cos(BG_AV_ObjectPos[i+8].GetOrientation()/2), RESPAWN_ONE_DAY))
                {
                    TC_LOG_ERROR("bg.battleground", "BatteGroundAV: Failed to spawn some object Battleground not created!4");
                    return false;
                }
            }
            for (uint8 j=0; j <= 9; j++) //burning aura
            {
                if (!AddObject(BG_AV_OBJECT_BURN_DUNBALDAR_SOUTH+((i-BG_AV_NODES_DUNBALDAR_SOUTH)*10)+j,
                               BG_AV_OBJECTID_FIRE,
                               BG_AV_ObjectPos[AV_OPLACE_BURN_DUNBALDAR_SOUTH+((i-BG_AV_NODES_DUNBALDAR_SOUTH)*10)+j],
                               0,
                               0,
                               std::sin(BG_AV_ObjectPos[AV_OPLACE_BURN_DUNBALDAR_SOUTH+((i-BG_AV_NODES_DUNBALDAR_SOUTH)*10)+j].GetOrientation()/2),
                               std::cos(BG_AV_ObjectPos[AV_OPLACE_BURN_DUNBALDAR_SOUTH+((i-BG_AV_NODES_DUNBALDAR_SOUTH)*10)+j].GetOrientation()/2),
                               RESPAWN_ONE_DAY))
                {
                    TC_LOG_ERROR("bg.battleground", "BatteGroundAV: Failed to spawn some object Battleground not created!5.{}", i);
                    return false;
                }
            }
        }
    }
    for (uint8 i=0; i<2; i++) //burning aura for buildings
    {
        for (uint8 j=0; j <= 9; j++)
        {
            if (j<5)
            {
                if (!AddObject(BG_AV_OBJECT_BURN_BUILDING_ALLIANCE+(i*10)+j,
                               BG_AV_OBJECTID_SMOKE,
                               BG_AV_ObjectPos[AV_OPLACE_BURN_BUILDING_A+(i*10)+j],
                               0,
                               0,
                               std::sin(BG_AV_ObjectPos[AV_OPLACE_BURN_BUILDING_A+(i*10)+j].GetOrientation()/2),
                               std::cos(BG_AV_ObjectPos[AV_OPLACE_BURN_BUILDING_A+(i*10)+j].GetOrientation()/2),
                               RESPAWN_ONE_DAY))
                {
                    TC_LOG_ERROR("bg.battleground", "BatteGroundAV: Failed to spawn some object Battleground not created!6.{}", i);
                    return false;
                }
            }
            else
            {
                if (!AddObject(BG_AV_OBJECT_BURN_BUILDING_ALLIANCE+(i*10)+j,
                               BG_AV_OBJECTID_FIRE,
                               BG_AV_ObjectPos[AV_OPLACE_BURN_BUILDING_A+(i*10)+j],
                               0,
                               0,
                               std::sin(BG_AV_ObjectPos[AV_OPLACE_BURN_BUILDING_A+(i*10)+j].GetOrientation()/2),
                               std::cos(BG_AV_ObjectPos[AV_OPLACE_BURN_BUILDING_A+(i*10)+j].GetOrientation()/2),
                               RESPAWN_ONE_DAY))
                {
                    TC_LOG_ERROR("bg.battleground", "BatteGroundAV: Failed to spawn some object Battleground not created!7.{}", i);
                    return false;
                }
            }
        }
    }
    for (uint16 i= 0; i <= (BG_AV_OBJECT_MINE_SUPPLY_N_MAX-BG_AV_OBJECT_MINE_SUPPLY_N_MIN); i++)
    {
        if (!AddObject(BG_AV_OBJECT_MINE_SUPPLY_N_MIN+i,
                       BG_AV_OBJECTID_MINE_N,
                       BG_AV_ObjectPos[AV_OPLACE_MINE_SUPPLY_N_MIN+i],
                       0,
                       0,
                       std::sin(BG_AV_ObjectPos[AV_OPLACE_MINE_SUPPLY_N_MIN+i].GetOrientation()/2),
                       std::cos(BG_AV_ObjectPos[AV_OPLACE_MINE_SUPPLY_N_MIN+i].GetOrientation()/2),
                       RESPAWN_ONE_DAY))
        {
            TC_LOG_ERROR("bg.battleground", "BatteGroundAV: Failed to spawn some mine supplies Battleground not created!7.5.{}", i);
            return false;
        }
    }
    for (uint16 i= 0; i <= (BG_AV_OBJECT_MINE_SUPPLY_S_MAX-BG_AV_OBJECT_MINE_SUPPLY_S_MIN); i++)
    {
        if (!AddObject(BG_AV_OBJECT_MINE_SUPPLY_S_MIN+i,
                       BG_AV_OBJECTID_MINE_S,
                       BG_AV_ObjectPos[AV_OPLACE_MINE_SUPPLY_S_MIN+i],
                       0,
                       0,
                       std::sin(BG_AV_ObjectPos[AV_OPLACE_MINE_SUPPLY_S_MIN+i].GetOrientation()/2),
                       std::cos(BG_AV_ObjectPos[AV_OPLACE_MINE_SUPPLY_S_MIN+i].GetOrientation()/2),
                       RESPAWN_ONE_DAY))
        {
            TC_LOG_ERROR("bg.battleground", "BatteGroundAV: Failed to spawn some mine supplies Battleground not created!7.6.{}", i);
            return false;
        }
    }

    if (!AddObject(BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE,
                   BG_AV_OBJECTID_BANNER_SNOWFALL_N,
                   BG_AV_ObjectPos[BG_AV_NODES_SNOWFALL_GRAVE],
                   0,
                   0,
                   std::sin(BG_AV_ObjectPos[BG_AV_NODES_SNOWFALL_GRAVE].GetOrientation()/2),
                   std::cos(BG_AV_ObjectPos[BG_AV_NODES_SNOWFALL_GRAVE].GetOrientation()/2),
                   RESPAWN_ONE_DAY))
    {
        TC_LOG_ERROR("bg.battleground", "BatteGroundAV: Failed to spawn some object Battleground not created!8");
        return false;
    }
    for (uint8 i = 0; i < 4; i++)
    {
        if (!AddObject(BG_AV_OBJECT_SNOW_EYECANDY_A+i, BG_AV_OBJECTID_SNOWFALL_CANDY_A,
                       BG_AV_ObjectPos[AV_OPLACE_SNOW_1+i],
                       0, 0, std::sin(BG_AV_ObjectPos[AV_OPLACE_SNOW_1+i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[AV_OPLACE_SNOW_1+i].GetOrientation()/2), RESPAWN_ONE_DAY)
            || !AddObject(BG_AV_OBJECT_SNOW_EYECANDY_PA+i, BG_AV_OBJECTID_SNOWFALL_CANDY_PA,
                          BG_AV_ObjectPos[AV_OPLACE_SNOW_1+i],
                          0, 0, std::sin(BG_AV_ObjectPos[AV_OPLACE_SNOW_1+i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[AV_OPLACE_SNOW_1+i].GetOrientation()/2), RESPAWN_ONE_DAY)
            || !AddObject(BG_AV_OBJECT_SNOW_EYECANDY_H+i, BG_AV_OBJECTID_SNOWFALL_CANDY_H,
                          BG_AV_ObjectPos[AV_OPLACE_SNOW_1+i],
                          0, 0, std::sin(BG_AV_ObjectPos[AV_OPLACE_SNOW_1+i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[AV_OPLACE_SNOW_1+i].GetOrientation()/2), RESPAWN_ONE_DAY)
            || !AddObject(BG_AV_OBJECT_SNOW_EYECANDY_PH+i, BG_AV_OBJECTID_SNOWFALL_CANDY_PH,
                          BG_AV_ObjectPos[AV_OPLACE_SNOW_1+i],
                          0, 0, std::sin(BG_AV_ObjectPos[AV_OPLACE_SNOW_1+i].GetOrientation()/2), std::cos(BG_AV_ObjectPos[AV_OPLACE_SNOW_1+i].GetOrientation()/2), RESPAWN_ONE_DAY))
        {
            TC_LOG_ERROR("bg.battleground", "BatteGroundAV: Failed to spawn some object Battleground not created!9.{}", i);
            return false;
        }
    }

    uint16 i;
    TC_LOG_DEBUG("bg.battleground", "Alterac Valley: entering state STATUS_WAIT_JOIN ...");
    // Initial Nodes
    for (i = 0; i < BG_AV_OBJECT_MAX; i++)
        SpawnBGObject(i, RESPAWN_ONE_DAY);

    for (i = BG_AV_OBJECT_FLAG_A_FIRSTAID_STATION; i <= BG_AV_OBJECT_FLAG_A_STONEHEART_GRAVE; i++)
    {
        SpawnBGObject(BG_AV_OBJECT_AURA_A_FIRSTAID_STATION+3*i, RESPAWN_IMMEDIATELY);
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
    }

    for (i = BG_AV_OBJECT_FLAG_A_DUNBALDAR_SOUTH; i <= BG_AV_OBJECT_FLAG_A_STONEHEART_BUNKER; i++)
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);

    for (i = BG_AV_OBJECT_FLAG_H_ICEBLOOD_GRAVE; i <= BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER; i++)
    {
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
        if (i <= BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT)
            SpawnBGObject(BG_AV_OBJECT_AURA_H_FIRSTAID_STATION+3*GetNodeThroughObject(i), RESPAWN_IMMEDIATELY);
    }

    for (i = BG_AV_OBJECT_TFLAG_A_DUNBALDAR_SOUTH; i <= BG_AV_OBJECT_TFLAG_A_STONEHEART_BUNKER; i+=2)
    {
        SpawnBGObject(i, RESPAWN_IMMEDIATELY); //flag
        SpawnBGObject(i+16, RESPAWN_IMMEDIATELY); //aura
    }

    for (i = BG_AV_OBJECT_TFLAG_H_ICEBLOOD_TOWER; i <= BG_AV_OBJECT_TFLAG_H_FROSTWOLF_WTOWER; i+=2)
    {
        SpawnBGObject(i, RESPAWN_IMMEDIATELY); //flag
        SpawnBGObject(i+16, RESPAWN_IMMEDIATELY); //aura
    }

    //snowfall and the doors
    for (i = BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE; i <= BG_AV_OBJECT_DOOR_A; i++)
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);

    SpawnBGObject(BG_AV_OBJECT_AURA_N_SNOWFALL_GRAVE, RESPAWN_IMMEDIATELY);

    //creatures
    TC_LOG_DEBUG("bg.battleground", "BG_AV start poputlating nodes");
    for (BG_AV_Nodes n = BG_AV_NODES_FIRSTAID_STATION; n < BG_AV_NODES_MAX; ++n)
    {
        if (m_Nodes[n].Owner)
            PopulateNode(n);
    }
    //all creatures which don't get despawned through the script are static
    TC_LOG_DEBUG("bg.battleground", "BG_AV: start spawning static creatures");
    for (i = 0; i < AV_STATICCPLACE_MAX; i++)
        AddAVCreature(0, i + AV_CPLACE_MAX);
    //mainspiritguides:
    TC_LOG_DEBUG("bg.battleground", "BG_AV: start spawning spiritguides creatures");
    AddSpiritGuide(7, BG_AV_CreaturePos[7], TEAM_ALLIANCE);
    AddSpiritGuide(8, BG_AV_CreaturePos[8], TEAM_HORDE);
    //spawn the marshals (those who get deleted, if a tower gets destroyed)
    TC_LOG_DEBUG("bg.battleground", "BG_AV: start spawning marshal creatures");
    for (i = AV_NPC_A_MARSHAL_SOUTH; i <= AV_NPC_H_MARSHAL_WTOWER; i++)
        AddAVCreature(i, AV_CPLACE_A_MARSHAL_SOUTH + (i - AV_NPC_A_MARSHAL_SOUTH));
    AddAVCreature(AV_NPC_HERALD, AV_CPLACE_HERALD);
    return true;
}

void BattlegroundAV::AssaultNode(BG_AV_Nodes node, uint16 team)
{
    if (m_Nodes[node].TotalOwner == team)
    {
        TC_LOG_FATAL("bg.battleground", "Assaulting team is TotalOwner of node");
        ABORT();
    }
    if (m_Nodes[node].Owner == team)
    {
        TC_LOG_FATAL("bg.battleground", "Assaulting team is owner of node");
        ABORT();
    }
    if (m_Nodes[node].State == POINT_DESTROYED)
    {
        TC_LOG_FATAL("bg.battleground", "Destroyed node is being assaulted");
        ABORT();
    }
    if (m_Nodes[node].State == POINT_ASSAULTED && m_Nodes[node].TotalOwner) //only assault an assaulted node if no totalowner exists
    {
        TC_LOG_FATAL("bg.battleground", "Assault on an not assaulted node with total owner");
        ABORT();
    }
    //the timer gets another time, if the previous owner was 0 == Neutral
    m_Nodes[node].Timer      = (m_Nodes[node].PrevOwner)? BG_AV_CAPTIME : BG_AV_SNOWFALL_FIRSTCAP;
    m_Nodes[node].PrevOwner  = m_Nodes[node].Owner;
    m_Nodes[node].Owner      = team;
    m_Nodes[node].PrevState  = m_Nodes[node].State;
    m_Nodes[node].State      = POINT_ASSAULTED;
}

void BattlegroundAV::DestroyNode(BG_AV_Nodes node)
{
    ASSERT(m_Nodes[node].State == POINT_ASSAULTED);

    m_Nodes[node].TotalOwner = m_Nodes[node].Owner;
    m_Nodes[node].PrevOwner  = m_Nodes[node].Owner;
    m_Nodes[node].PrevState  = m_Nodes[node].State;
    m_Nodes[node].State      = (m_Nodes[node].Tower)? POINT_DESTROYED : POINT_CONTROLED;
    m_Nodes[node].Timer      = 0;
}

void BattlegroundAV::InitNode(BG_AV_Nodes node, uint16 team, bool tower)
{
    m_Nodes[node].TotalOwner = team;
    m_Nodes[node].Owner      = team;
    m_Nodes[node].PrevOwner  = 0;
    m_Nodes[node].State      = POINT_CONTROLED;
    m_Nodes[node].PrevState  = m_Nodes[node].State;
    m_Nodes[node].State      = POINT_CONTROLED;
    m_Nodes[node].Timer      = 0;
    m_Nodes[node].Tower      = tower;
}

void BattlegroundAV::DefendNode(BG_AV_Nodes node, uint16 team)
{
    ASSERT(m_Nodes[node].TotalOwner == team);
    ASSERT(m_Nodes[node].Owner != team);
    ASSERT(m_Nodes[node].State != POINT_CONTROLED && m_Nodes[node].State != POINT_DESTROYED);
    m_Nodes[node].PrevOwner  = m_Nodes[node].Owner;
    m_Nodes[node].Owner      = team;
    m_Nodes[node].PrevState  = m_Nodes[node].State;
    m_Nodes[node].State      = POINT_CONTROLED;
    m_Nodes[node].Timer      = 0;
}

void BattlegroundAV::ResetBGSubclass()
{
    for (uint8 i=0; i<2; i++) //forloop for both teams (it just make 0 == alliance and 1 == horde also for both mines 0=north 1=south
    {
        for (uint8 j=0; j<9; j++)
            m_Team_QuestStatus[i][j]=0;
        m_Team_Scores[i]=BG_AV_SCORE_INITIAL_POINTS;
        m_IsInformedNearVictory[i]=false;
        m_CaptainAlive[i] = true;
        m_CaptainBuffTimer[i] = 120000 + urand(0, 4)* 60; //as far as i could see, the buff is randomly so i make 2minutes (thats the duration of the buff itself) + 0-4minutes @todo get the right times
        m_Mine_Owner[i] = AV_NEUTRAL_TEAM;
        m_Mine_PrevOwner[i] = m_Mine_Owner[i];
    }

    for (BG_AV_Nodes i = BG_AV_NODES_FIRSTAID_STATION; i <= BG_AV_NODES_STONEHEART_GRAVE; ++i) //alliance graves
        InitNode(i, ALLIANCE, false);
    for (BG_AV_Nodes i = BG_AV_NODES_DUNBALDAR_SOUTH; i <= BG_AV_NODES_STONEHEART_BUNKER; ++i) //alliance towers
        InitNode(i, ALLIANCE, true);
    for (BG_AV_Nodes i = BG_AV_NODES_ICEBLOOD_GRAVE; i <= BG_AV_NODES_FROSTWOLF_HUT; ++i) //horde graves
        InitNode(i, HORDE, false);
    for (BG_AV_Nodes i = BG_AV_NODES_ICEBLOOD_TOWER; i <= BG_AV_NODES_FROSTWOLF_WTOWER; ++i) //horde towers
        InitNode(i, HORDE, true);
    InitNode(BG_AV_NODES_SNOWFALL_GRAVE, AV_NEUTRAL_TEAM, false); //give snowfall neutral owner

    m_Mine_Timer = AV_MINE_TICK_TIMER;
    for (uint16 i = 0; i < AV_CPLACE_MAX + AsUnderlyingType(AV_STATICCPLACE_MAX); i++)
        if (BgCreatures[i])
            DelCreature(i);
}

bool BattlegroundAV::CheckAchievementCriteriaMeet(uint32 criteriaId, Player const* source, Unit const* target, uint32 miscValue)
{
    uint32 team = source->GetTeam();
    switch (criteriaId)
    {
        case BG_CRITERIA_CHECK_EVERYTHING_COUNTS:
            for (uint8 mine = 0; mine < 2; mine++)
                if (m_Mine_Owner[mine] != team)
                    return false;

            return true;
        case BG_CRITERIA_CHECK_AV_PERFECTION:
        {
            if (team == ALLIANCE)
            {
                for (BG_AV_Nodes i = BG_AV_NODES_DUNBALDAR_SOUTH; i <= BG_AV_NODES_STONEHEART_BUNKER; ++i) // alliance towers controlled
                {
                    if (m_Nodes[i].State == POINT_CONTROLED)
                    {
                        if (m_Nodes[i].Owner != ALLIANCE)
                            return false;
                    }
                    else
                        return false;
                }

                for (BG_AV_Nodes i = BG_AV_NODES_ICEBLOOD_TOWER; i <= BG_AV_NODES_FROSTWOLF_WTOWER; ++i) // horde towers destroyed
                    if (m_Nodes[i].State != POINT_DESTROYED)
                        return false;

                if (!m_CaptainAlive[0])
                    return false;

                return true;
            }
            else if (team == HORDE)
            {
                for (BG_AV_Nodes i = BG_AV_NODES_ICEBLOOD_TOWER; i <= BG_AV_NODES_FROSTWOLF_WTOWER; ++i) // horde towers controlled
                {
                    if (m_Nodes[i].State == POINT_CONTROLED)
                    {
                        if (m_Nodes[i].Owner != HORDE)
                            return false;
                    }
                    else
                        return false;
                }

                for (BG_AV_Nodes i = BG_AV_NODES_DUNBALDAR_SOUTH; i <= BG_AV_NODES_STONEHEART_BUNKER; ++i) // alliance towers destroyed
                    if (m_Nodes[i].State != POINT_DESTROYED)
                        return false;

                if (!m_CaptainAlive[1])
                    return false;

                return true;
            }
        }
    }

    return Battleground::CheckAchievementCriteriaMeet(criteriaId, source, target, miscValue);
}

uint32 BattlegroundAV::GetPrematureWinner()
{
    uint32 allianceScore = m_Team_Scores[GetTeamIndexByTeamId(ALLIANCE)];
    uint32 hordeScore = m_Team_Scores[GetTeamIndexByTeamId(HORDE)];

    if (allianceScore > hordeScore)
        return ALLIANCE;
    else if (hordeScore > allianceScore)
        return HORDE;

    return Battleground::GetPrematureWinner();
}
