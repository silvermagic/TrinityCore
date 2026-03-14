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
 * @file Battlefield.h
 * @brief 战场系统核心头文件
 *
 * 本文件定义了战场系统的核心类和数据结构，包括：
 * - BfCapturePoint: 战场据点/旗帜控制点类
 * - BfGraveyard: 战场墓地管理类
 * - Battlefield: 战场基类，提供战场逻辑框架
 *
 * 战场系统用于管理大型世界PVP区域（如冬拥湖），提供：
 * - 战斗开始/结束管理
 * - 玩家队列和参战管理
 * - 据点占领机制
 * - 墓地和复活系统
 * - 团队管理
 */

#ifndef BATTLEFIELD_H_
#define BATTLEFIELD_H_

#include "Position.h"
#include "SharedDefines.h"
#include "ZoneScript.h"
#include <map>

/**
 * @brief 战场类型枚举
 * 定义游戏中不同类型的战场
 */
enum BattlefieldTypes
{
    BATTLEFIELD_WG  = 1,   // 冬拥湖战场 (Wintergrasp)
    BATTLEFIELD_MAX        // 战场类型总数（边界值）
};

/**
 * @brief 战场ID枚举
 * 用于唯一标识不同的战场实例
 */
enum BattlefieldIDs
{
    BATTLEFIELD_BATTLEID_WG                      = 1        // 冬拥湖战斗ID
};

/**
 * @brief 据点占领状态枚举
 * 定义据点/旗帜的不同占领状态，用于控制占领进度条显示
 */
enum BattlefieldObjectiveStates
{
    BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL = 0,                    // 中立状态
    BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE,                       // 联盟占领
    BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE,                          // 部落占领
    BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE,     // 中立被联盟挑战
    BF_CAPTUREPOINT_OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE,        // 中立被部落挑战
    BF_CAPTUREPOINT_OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE,       // 联盟被部落挑战
    BF_CAPTUREPOINT_OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE        // 部落被联盟挑战
};

/**
 * @brief 战场音效枚举
 * 定义战场中使用的各种音效ID
 */
enum BattlefieldSounds
{
    BF_SOUND_HORDE_WINS                          = 8454,   // 部落获胜音效
    BF_SOUND_ALLIANCE_WINS                       = 8455,   // 联盟获胜音效
    BF_SOUND_START                               = 3439    // 战斗开始音效
};

/**
 * @brief 战场定时器枚举
 * 定义战场系统中使用的各种时间间隔
 */
enum BattlefieldTimers
{
    BATTLEFIELD_OBJECTIVE_UPDATE_INTERVAL        = 1000    // 据点更新间隔（毫秒）
};

namespace WorldPackets
{
    namespace WorldState
    {
        class InitWorldStates;
    }
}

// 前向声明
class Battlefield;
class BfGraveyard;
class Creature;
class GameObject;
class Group;
class Map;
class Player;
class Unit;
class WorldPacket;

struct QuaternionData;
struct WorldSafeLocsEntry;

// 类型定义
typedef std::vector<BfGraveyard*> GraveyardVect;          // 墓地列表类型
typedef std::map<ObjectGuid, time_t> PlayerTimerMap;       // 玩家定时器映射类型（用于记录玩家过期时间）

/**
 * @class BfCapturePoint
 * @brief 战场据点/旗帜控制点类
 *
 * 负责管理战场中可占领的据点（如冬拥湖的工坊）。
 * 实现了占领进度条机制，玩家站在据点附近可以推进占领进度。
 *
 * 核心功能：
 * - 追踪据点范围内的玩家
 * - 根据双方玩家数量计算占领进度
 * - 更新世界状态（占领进度条UI）
 * - 处理据点易主事件
 */
class TC_GAME_API BfCapturePoint
{
    public:
        /**
         * @brief 构造函数
         * @param bf 所属战场指针
         */
        explicit BfCapturePoint(Battlefield* bf);
        BfCapturePoint(BfCapturePoint const&) = delete;
        BfCapturePoint(BfCapturePoint&&) = delete;
        BfCapturePoint& operator=(BfCapturePoint const&) = delete;
        BfCapturePoint& operator=(BfCapturePoint&&) = delete;

        /**
         * @brief 虚析构函数
         */
        virtual ~BfCapturePoint();

        /**
         * @brief 填充初始世界状态数据
         * @param packet 世界状态数据包引用
         *
         * 将据点的初始状态填充到数据包中，发送给新进入区域的玩家
         * 子类可重写此方法以添加额外的世界状态数据
         */
        virtual void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& /*packet*/) { }

        /**
         * @brief 向所有在场玩家发送世界状态更新
         * @param field 世界状态字段ID
         * @param value 新值
         *
         * 用于更新据点相关的UI显示（如占领进度条）
         */
        void SendUpdateWorldState(uint32 field, uint32 value);

        /**
         * @brief 发送目标完成通知
         * @param id 击杀怪物信贷ID
         * @param guid 相关对象的GUID
         *
         * 向控制该据点的阵营玩家发送任务完成通知
         */
        void SendObjectiveComplete(uint32 id, ObjectGuid guid);

        /**
         * @brief 处理玩家进入据点区域
         * @param player 进入的玩家指针
         * @return 是否成功添加（玩家是否已存在）
         *
         * 当玩家进入据点有效范围内时调用
         * 将玩家添加到活跃玩家列表，并更新其客户端的占领进度显示
         */
        virtual bool HandlePlayerEnter(Player* player);

        /**
         * @brief 处理玩家离开据点区域
         * @param player 离开的玩家指针
         * @return 指向下一个玩家的迭代器
         *
         * 当玩家离开据点有效范围或下线时调用
         * 从活跃玩家列表中移除玩家
         */
        virtual GuidSet::iterator HandlePlayerLeave(Player* player);

        /**
         * @brief 检查玩家是否在据点范围内
         * @param player 要检查的玩家
         * @return 是否在范围内
         */
        bool IsInsideObjective(Player* player) const;

        /**
         * @brief 更新据点状态
         * @param diff 距上次更新的时间间隔（毫秒）
         * @return 据点状态是否发生变化（如所有权变更）
         *
         * 核心更新函数，执行以下操作：
         * 1. 检测范围内玩家的进出
         * 2. 根据双方玩家数量计算占领进度变化
         * 3. 更新据点状态和所属阵营
         *
         * @note 每秒调用一次（BATTLEFIELD_OBJECTIVE_UPDATE_INTERVAL）
         */
        virtual bool Update(uint32 diff);

        /**
         * @brief 据点所有权变更时的回调
         * @param oldTeam 之前的拥有者阵营
         *
         * 子类重写此方法以处理据点易主事件
         * 如：更新NPC、游戏对象、墓地控制权等
         */
        virtual void ChangeTeam(TeamId /*oldTeam*/) { }

        /**
         * @brief 发送占领进度阶段变化
         *
         * 向所有在场玩家更新占领进度条的显示
         */
        virtual void SendChangePhase();

        /**
         * @brief 设置据点数据
         * @param capturePoint 据点对应的游戏对象指针
         * @return 是否设置成功
         *
         * 初始化据点的基础数据，包括：
         * - 最大/最小占领值
         * - 占领速度
         * - 中立百分比
         */
        bool SetCapturePointData(GameObject* capturePoint);

        /**
         * @brief 获取据点对应的游戏对象
         * @return 游戏对象指针，失败返回nullptr
         */
        GameObject* GetCapturePointGo();

        /**
         * @brief 获取据点模板ID
         * @return 据点模板ID
         */
        uint32 GetCapturePointEntry() const { return m_capturePointEntry; }

        /**
         * @brief 获取当前控制阵营
         * @return 当前控制阵营ID
         */
        TeamId GetTeamId() const { return m_team; }

    protected:
        /**
         * @brief 删除据点游戏对象
         * @return 是否删除成功
         */
        bool DelCapturePoint();

        // ==================== 成员变量 ====================

        /**
         * @brief 活跃玩家集合
         * 存储在据点范围内的玩家，按阵营分组
         * 索引0 = 联盟，索引1 = 部落
         */
        GuidSet m_activePlayers[PVP_TEAMS_COUNT];

        /**
         * @brief 完全占领所需的总进度值
         * 决定了占领进度条的满值
         */
        float m_maxValue;

        /**
         * @brief 最小占领值（中立区域边界）
         * 用于确定何时从一方占领变为中立状态
         */
        float m_minValue;

        /**
         * @brief 最大占领速度
         * 限制占领进度的变化速率，防止过快占领
         */
        float m_maxSpeed;

        /**
         * @brief 当前占领进度值
         * 正值 = 联盟偏向，负值 = 部落偏向
         * 范围：[-m_maxValue, +m_maxValue]
         */
        float m_value;

        /**
         * @brief 当前控制阵营
         */
        TeamId m_team;

        /**
         * @brief 上一次的据点状态
         * 用于检测状态变化
         */
        BattlefieldObjectiveStates m_OldState;

        /**
         * @brief 当前据点状态
         */
        BattlefieldObjectiveStates m_State;

        /**
         * @brief 中立值百分比
         * 定义占领进度条上中立区域的宽度（百分比）
         */
        uint32 m_neutralValuePct;

        /**
         * @brief 所属战场指针
         */
        Battlefield* m_Bf;

        /**
         * @brief 据点模板ID
         */
        uint32 m_capturePointEntry;

        /**
         * @brief 据点游戏对象的GUID
         */
        ObjectGuid m_capturePointGUID;
};

/**
 * @class BfGraveyard
 * @brief 战场墓地管理类
 *
 * 负责管理战场中的墓地系统，包括：
 * - 墓地控制权管理
 * - 灵魂医者（天使）管理
 * - 玩家复活队列管理
 * - 死亡玩家传送
 *
 * 每个墓地可以被联盟或部落控制，只有控制方的玩家才能在此复活。
 */
class TC_GAME_API BfGraveyard
{
    public:
        /**
         * @brief 构造函数
         * @param bf 所属战场指针
         */
        explicit BfGraveyard(Battlefield* bf);
        BfGraveyard(BfGraveyard const&) = delete;
        BfGraveyard(BfGraveyard&&) = delete;
        BfGraveyard& operator=(BfGraveyard const&) = delete;
        BfGraveyard& operator=(BfGraveyard&&) = delete;

        /**
         * @brief 虚析构函数
         */
        virtual ~BfGraveyard();

        /**
         * @brief 将墓地控制权移交给指定阵营
         * @param team 新的控制阵营
         *
         * 当墓地被占领时调用，会：
         * 1. 更新控制阵营
         * 2. 将正在等待复活的敌对阵列玩家传送到最近的友方墓地
         */
        void GiveControlTo(TeamId team);

        /**
         * @brief 获取当前控制阵营
         * @return 当前控制阵营ID
         */
        TeamId GetControlTeamId() const { return m_ControlTeam; }

        /**
         * @brief 计算墓地到玩家的距离
         * @param player 目标玩家
         * @return 距离值
         *
         * 用于为死亡玩家找到最近的友方墓地
         */
        float GetDistance(Player* player);

        /**
         * @brief 初始化墓地
         * @param startcontrol 初始控制阵营
         * @param gy 墓地ID（对应WorldSafeLocsEntry）
         */
        void Initialize(TeamId startcontrol, uint32 gy);

        /**
         * @brief 设置灵魂医者
         * @param spirit 灵魂医者生物指针
         * @param team 该灵魂医者所属阵营
         *
         * 每个墓地通常有两个灵魂医者（联盟和部落各一个）
         * 根据当前控制阵营，只有对应的灵魂医者可见
         */
        void SetSpirit(Creature* spirit, TeamId team);

        /**
         * @brief 将玩家添加到复活队列
         * @param player_guid 玩家GUID
         *
         * 当玩家死亡并选择在此墓地复活时调用
         * 会给玩家施加"等待复活"的法术效果
         */
        void AddPlayer(ObjectGuid player_guid);

        /**
         * @brief 将玩家从复活队列移除
         * @param player_guid 玩家GUID
         *
         * 当玩家取消复活或被传送走时调用
         */
        void RemovePlayer(ObjectGuid player_guid);

        /**
         * @brief 复活所有排队中的玩家
         *
         * 定期调用（约30秒一次），复活所有在队列中等待的玩家
         * 复活后清空队列
         */
        void Resurrect();

        /**
         * @brief 重新定位死亡玩家
         *
         * 当墓地控制权变更时，将敌对阵营的死亡玩家
         * 传送到最近的友方墓地继续等待复活
         */
        void RelocateDeadPlayers();

        /**
         * @brief 检查墓地是否有指定的NPC（灵魂医者）
         * @param guid NPC的GUID
         * @return 是否存在该NPC
         */
        bool HasNpc(ObjectGuid guid);

        /**
         * @brief 检查玩家是否在复活队列中
         * @param guid 玩家GUID
         * @return 是否在队列中
         */
        bool HasPlayer(ObjectGuid guid) { return m_ResurrectQueue.find(guid) != m_ResurrectQueue.end(); }

        /**
         * @brief 获取墓地ID
         * @return 墓地ID
         */
        uint32 GetGraveyardId() const { return m_GraveyardId; }

    protected:
        // ==================== 成员变量 ====================

        /**
         * @brief 当前控制阵营
         */
        TeamId m_ControlTeam;

        /**
         * @brief 墓地ID
         * 对应WorldSafeLocs数据库表中的ID
         */
        uint32 m_GraveyardId;

        /**
         * @brief 灵魂医者GUID数组
         * 索引0 = 联盟灵魂医者，索引1 = 部落灵魂医者
         */
        ObjectGuid m_SpiritGuide[PVP_TEAMS_COUNT];

        /**
         * @brief 复活等待队列
         * 存储所有正在等待复活的玩家GUID
         */
        GuidSet m_ResurrectQueue;

        /**
         * @brief 所属战场指针
         */
        Battlefield* m_Bf;
};

/**
 * @class Battlefield
 * @brief 战场基类
 *
 * 这是所有战场（如冬拥湖）的基类，继承自ZoneScript。
 * 提供了战场系统的完整框架，包括：
 *
 * 核心功能：
 * - 战斗周期管理（开始/结束/定时）
 * - 玩家管理（进入/离开/队列/参战）
 * - 据点占领系统
 * - 墓地和复活系统
 * - 团队管理系统
 * - 世界状态同步
 *
 * 子类需要实现：
 * - SetupBattlefield(): 初始化战场配置
 * - FillInitialWorldStates(): 填充初始世界状态
 * - SendInitWorldStatesToAll(): 向所有玩家发送世界状态
 * - 各种OnXXX回调函数
 *
 * @note 这是抽象基类，不能直接实例化
 */
class TC_GAME_API Battlefield : public ZoneScript
{
    friend class BattlefieldMgr;

    public:
        /// 构造函数
        Battlefield();
        Battlefield(Battlefield const&) = delete;
        Battlefield(Battlefield&&) = delete;
        Battlefield& operator=(Battlefield const&) = delete;
        Battlefield& operator=(Battlefield&&) = delete;

        /// 虚析构函数
        virtual ~Battlefield();

        /// 据点映射类型定义
        /// Key: 据点GUID的低32位，Value: 据点对象指针
        typedef std::map<ObjectGuid::LowType /*lowguid */, BfCapturePoint*> BfCapturePointMap;

        /**
         * @brief 初始化战场
         * @return 是否初始化成功
         *
         * 子类必须重写此方法以完成战场初始化：
         * - 设置战场参数（地图ID、区域ID等）
         * - 创建据点
         * - 初始化墓地
         * - 生成NPC和游戏对象
         */
        virtual bool SetupBattlefield() { return true; }

        /**
         * @brief 向指定玩家发送初始世界状态
         * @param player 目标玩家
         *
         * 当玩家进入战场区域时调用，同步所有世界状态数据
         */
        void SendInitWorldStatesTo(Player* player);

        /**
         * @brief 向区域内所有玩家更新世界状态
         * @param field 世界状态字段ID
         * @param value 新值
         */
        void SendUpdateWorldState(uint32 field, uint32 value);

        /**
         * @brief 战场主更新函数
         * @param diff 距上次更新的时间间隔（毫秒）
         * @return 据点状态是否发生变化
         *
         * 核心更新循环，处理：
         * 1. 战斗开始/结束定时器
         * 2. 战斗开始前的组队邀请
         * 3. AFK玩家踢出
         * 4. 据点状态更新
         * 5. 墓地复活定时器
         *
         * @note 每秒调用一次
         */
        virtual bool Update(uint32 diff);

        /**
         * @brief 邀请区域内所有玩家加入队列
         *
         * 在战斗开始前几分钟调用，邀请区域内的玩家加入战斗队列
         */
        void InvitePlayersInZoneToQueue();

        /**
         * @brief 邀请队列中的玩家加入战斗
         *
         * 战斗开始时调用，邀请所有在队列中的玩家正式参战
         */
        void InvitePlayersInQueueToWar();

        /**
         * @brief 邀请区域内所有玩家加入战斗
         *
         * 战斗进行中持续调用，邀请新进入区域的玩家参战
         */
        void InvitePlayersInZoneToWar();

        /**
         * @brief 处理击杀事件
         * @param killer 击杀者
         * @param killed 被击杀单位
         *
         * 当单位在战场区域内被击杀时调用
         * 子类可重写此方法以实现荣誉、奖励等逻辑
         */
        virtual void HandleKill(Player* /*killer*/, Unit* /*killed*/) { };

        uint32 GetTypeId() const { return m_TypeId; }
        uint32 GetZoneId() const { return m_ZoneId; }

        /**
         * @brief 为指定阵营应用增益法术
         * @param team 目标阵营
         * @param spellId 主要法术ID
         * @param spellId2 次要法术ID（可选）
         */
        void TeamApplyBuff(TeamId team, uint32 spellId, uint32 spellId2 = 0);

        /// 检查战斗是否正在进行
        bool IsWarTime() const { return m_isActive; }

        /// 启用或禁用战场
        void ToggleBattlefield(bool enable) { m_IsEnabled = enable; }

        /// 检查战场是否启用
        bool IsEnabled() const { return m_IsEnabled; }

        /**
         * @brief 将玩家踢出战场
         * @param guid 玩家GUID
         *
         * 将玩家传送出战场区域（传送到KickPosition）
         * 用于踢出AFK玩家或拒绝邀请的玩家
         */
        void KickPlayerFromBattlefield(ObjectGuid guid);

        /**
         * @brief 处理玩家进入战场区域
         * @param player 进入的玩家
         * @param zone 区域ID
         *
         * 当玩家进入战场区域时调用：
         * - 如果战斗进行中，邀请参战或加入队列
         * - 如果战斗即将开始，邀请加入队列
         * - 将玩家添加到区域玩家列表
         */
        void HandlePlayerEnterZone(Player* player, uint32 zone);

        /**
         * @brief 处理玩家离开战场区域
         * @param player 离开的玩家
         * @param zone 区域ID
         *
         * 当玩家离开战场区域时调用：
         * - 如果正在参战，从战斗中移除
         * - 从所有列表中移除（队列、据点、复活队列等）
         * - 清理世界状态
         */
        void HandlePlayerLeaveZone(Player* player, uint32 zone);

        // ==================== 数据存储接口 ====================

        /// 通用64位数据存储接口
        virtual uint64 GetData64(uint32 dataId) const override { return m_Data64[dataId]; }
        virtual void SetData64(uint32 dataId, uint64 value) override { m_Data64[dataId] = value; }

        /// 通用32位数据存储接口
        virtual uint32 GetData(uint32 dataId) const override { return m_Data32[dataId]; }
        virtual void SetData(uint32 dataId, uint32 value) override { m_Data32[dataId] = value; }
        virtual void UpdateData(uint32 index, int32 pad) { m_Data32[index] += pad; }

        // ==================== 阵营相关方法 ====================

        TeamId GetDefenderTeam() const { return m_DefenderTeam; }
        TeamId GetAttackerTeam() const { return TeamId(1 - m_DefenderTeam); }
        TeamId GetOtherTeam(TeamId team) const { return (team == TEAM_HORDE ? TEAM_ALLIANCE : TEAM_HORDE); }
        void SetDefenderTeam(TeamId team) { m_DefenderTeam = team; }

        // ==================== 团队管理方法 ====================

        /**
         * @brief 寻找或创建可用的战场团队
         * @param TeamId 目标阵营
         * @return 未满的团队指针，没有则创建新团队
         *
         * 战场中的玩家会被自动编入团队，方便协作
         */
        Group* GetFreeBfRaid(TeamId TeamId);

        /**
         * @brief 获取玩家所在的战场团队
         * @param guid 玩家GUID
         * @param TeamId 玩家阵营
         * @return 团队指针，未找到返回nullptr
         */
        Group* GetGroupPlayer(ObjectGuid guid, TeamId TeamId);

        /**
         * @brief 将玩家加入正确的战场团队
         * @param player 玩家指针
         * @return 是否成功加入
         *
         * 玩家接受战斗邀请后调用，将其编入团队
         */
        bool AddOrSetPlayerToCorrectBfGroup(Player* player);

        // ==================== 墓地管理方法 ====================

        /**
         * @brief 获取最近的友方墓地
         * @param player 需要复活的玩家
         * @return 墓地位置数据，未找到返回nullptr
         */
        WorldSafeLocsEntry const* GetClosestGraveyard(Player* player);

        /**
         * @brief 将玩家添加到复活队列
         * @param npc_guid 灵魂医者GUID
         * @param player_guid 玩家GUID
         */
        virtual void AddPlayerToResurrectQueue(ObjectGuid npc_guid, ObjectGuid player_guid);

        /**
         * @brief 将玩家从复活队列移除
         * @param player_guid 玩家GUID
         */
        void RemovePlayerFromResurrectQueue(ObjectGuid player_guid);

        /// 设置墓地数量
        void SetGraveyardNumber(uint32 number) { m_GraveyardList.resize(number); }

        /**
         * @brief 根据ID获取墓地对象
         * @param id 墓地ID
         * @return 墓地对象指针
         */
        BfGraveyard* GetGraveyardById(uint32 id) const;

        // ==================== 生物/游戏对象管理方法 ====================

        /**
         * @brief 在战场地图上生成生物
         * @param entry 生物模板ID
         * @param pos 生成位置
         * @return 生成的生物指针，失败返回nullptr
         */
        Creature* SpawnCreature(uint32 entry, Position const& pos);

        /**
         * @brief 在战场地图上生成游戏对象
         * @param entry 游戏对象模板ID
         * @param pos 生成位置
         * @param rot 旋转四元数
         * @return 生成的游戏对象指针，失败返回nullptr
         */
        GameObject* SpawnGameObject(uint32 entry, Position const& pos, QuaternionData const& rot);

        /**
         * @brief 根据GUID获取生物
         * @param guid 生物GUID
         * @return 生物指针
         */
        Creature* GetCreature(ObjectGuid guid);

        /**
         * @brief 根据GUID获取游戏对象
         * @param guid 游戏对象GUID
         * @return 游戏对象指针
         */
        GameObject* GetGameObject(ObjectGuid guid);

        // ==================== 脚本回调接口 ====================

        /// 战斗开始时调用
        virtual void OnBattleStart() { }

        /// 战斗结束时调用
        virtual void OnBattleEnd(bool /*endByTimer*/) { }

        /// 开始组队时调用（战斗开始前几分钟）
        virtual void OnStartGrouping() { }

        /// 玩家加入战斗时调用
        virtual void OnPlayerJoinWar(Player* /*player*/) { }

        /// 玩家离开战斗时调用
        virtual void OnPlayerLeaveWar(Player* /*player*/) { }

        /// 玩家离开战场区域时调用
        virtual void OnPlayerLeaveZone(Player* /*player*/) { }

        /// 玩家进入战场区域时调用
        virtual void OnPlayerEnterZone(Player* /*player*/) { }

        /**
         * @brief 发送战场警告消息
         * @param id 消息文本ID
         * @param target 目标对象（可选）
         *
         * 通过Stalker NPC发送区域消息
         */
        void SendWarning(uint8 id, WorldObject const* target = nullptr);

        /// 玩家接受加入队列邀请
        void PlayerAcceptInviteToQueue(Player* player);

        /// 玩家接受加入战斗邀请
        void PlayerAcceptInviteToWar(Player* player);

        uint32 GetBattleId() const { return m_BattleId; }

        /// 玩家请求离开队列
        void AskToLeaveQueue(Player* player);

        /// 玩家请求离开战场
        void PlayerAskToLeave(Player* player);

        /// 完成或递增成就
        virtual void DoCompleteOrIncrementAchievement(uint32 /*achievement*/, Player* /*player*/, uint8 /*incrementNumber = 1*/) { }

        /// 向所有区域内的玩家发送世界状态（纯虚函数，子类必须实现）
        virtual void SendInitWorldStatesToAll() = 0;

        /// 填充初始世界状态数据（纯虚函数，子类必须实现）
        virtual void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& /*packet*/) = 0;

        /// 检查是否可以在战场飞行（战斗中禁止）
        bool CanFlyIn() { return !m_isActive; }

        /// 发送灵魂医者查询响应
        void SendAreaSpiritHealerQueryOpcode(Player* player, ObjectGuid guid);

        /// 开始战斗
        void StartBattle();

        /// 结束战斗
        void EndBattle(bool endByTimer);

        /// 隐藏NPC（通过相位和可见性）
        void HideNpc(Creature* creature);

        /// 显示NPC
        void ShowNpc(Creature* creature, bool aggressive);

        /// 获取墓地列表
        GraveyardVect GetGraveyardVector() const { return m_GraveyardList; }

        uint32 GetTimer() const { return m_Timer; }
        void SetTimer(uint32 timer) { m_Timer = timer; }

        /// 向所有参战玩家播放音效
        void DoPlaySoundToAll(uint32 soundID);

        /// 邀请玩家加入队列
        void InvitePlayerToQueue(Player* player);

        /// 邀请玩家加入战斗
        void InvitePlayerToWar(Player* player);

        /// 初始化追踪者NPC（用于发送区域消息）
        void InitStalker(uint32 entry, Position const& pos);

    protected:
        // ==================== 成员变量 ====================

        ObjectGuid StalkerGuid;                               ///< 追踪者NPC的GUID（用于发送区域消息）
        uint32 m_Timer;                                       ///< 全局计时器（毫秒）
        bool m_IsEnabled;                                     ///< 战场是否启用
        bool m_isActive;                                      ///< 战斗是否正在进行
        TeamId m_DefenderTeam;                                ///< 防守方阵营

        /// 据点映射表
        BfCapturePointMap m_capturePoints;

        // ==================== 玩家信息映射表 ====================

        GuidUnorderedSet m_players[PVP_TEAMS_COUNT];          ///< 区域内的玩家（按阵营分）
        GuidUnorderedSet m_PlayersInQueue[PVP_TEAMS_COUNT];   ///< 队列中的玩家
        GuidUnorderedSet m_PlayersInWar[PVP_TEAMS_COUNT];     ///< 参战中的玩家
        PlayerTimerMap m_InvitedPlayers[PVP_TEAMS_COUNT];     ///< 已邀请的玩家（含过期时间）
        PlayerTimerMap m_PlayersWillBeKick[PVP_TEAMS_COUNT];  ///< 待踢出的玩家（含过期时间）

        // ==================== 战场配置变量 ====================

        uint32 m_TypeId;                                      ///< 战场类型ID（BattlefieldTypes枚举）
        uint32 m_BattleId;                                    ///< 战斗ID（用于客户端数据包）
        uint32 m_ZoneId;                                      ///< 区域ID（如冬拥湖=4197）
        uint32 m_MapId;                                       ///< 地图ID
        Map* m_Map;                                           ///< 地图指针
        uint32 m_MaxPlayer;                                   ///< 每个阵营最大玩家数
        uint32 m_MinPlayer;                                   ///< 战场开始的最小玩家数
        uint32 m_MinLevel;                                    ///< 参与所需最低等级
        uint32 m_BattleTime;                                  ///< 战斗持续时间（毫秒）
        uint32 m_NoWarBattleTime;                             ///< 两场战斗之间的间隔时间（毫秒）
        uint32 m_RestartAfterCrash;                           ///< 服务器崩溃后重启延迟
        uint32 m_TimeForAcceptInvite;                         ///< 接受邀请的时限（秒）
        uint32 m_uiKickDontAcceptTimer;                       ///< 踢出未响应玩家的定时器
        WorldLocation KickPosition;                           ///< 踢出位置（玩家被传送至此）

        uint32 m_uiKickAfkPlayersTimer;                       ///< AFK玩家检查定时器

        // ==================== 墓地相关变量 ====================

        GraveyardVect m_GraveyardList;                        ///< 墓地列表
        uint32 m_LastResurrectTimer;                          ///< 上次复活检查时间（30秒间隔）

        uint32 m_StartGroupingTimer;                          ///< 组队邀请开始时间（战斗开始前多久）
        bool m_StartGrouping;                                 ///< 是否已开始组队邀请

        GuidUnorderedSet m_Groups[PVP_TEAMS_COUNT];           ///< 各阵营的团队列表

        /// 通用数据存储
        std::vector<uint64> m_Data64;
        std::vector<uint32> m_Data32;

        // ==================== 保护方法 ====================

        /// 踢出AFK玩家
        void KickAfkPlayers();

        /// 移除玩家的世界状态（离开区域时）
        virtual void SendRemoveWorldStates(Player* /*player*/) { }

        // ==================== 数据包广播方法 ====================

        /// 向区域内所有玩家广播数据包
        void BroadcastPacketToZone(WorldPacket const* data) const;

        /// 向队列中的玩家广播数据包
        void BroadcastPacketToQueue(WorldPacket const* data) const;

        /// 向参战玩家广播数据包
        void BroadcastPacketToWar(WorldPacket const* data) const;

        // ==================== 据点系统方法 ====================

        /// 添加据点到映射表
        void AddCapturePoint(BfCapturePoint* cp) { m_capturePoints[cp->GetCapturePointEntry()] = cp; }

        /**
         * @brief 根据GUID低32位获取据点
         * @param lowguid 据点GUID的低32位
         * @return 据点指针，未找到返回nullptr
         */
        BfCapturePoint* GetCapturePoint(ObjectGuid::LowType lowguid) const
        {
            Battlefield::BfCapturePointMap::const_iterator itr = m_capturePoints.find(lowguid);
            if (itr != m_capturePoints.end())
                return itr->second;
            return nullptr;
        }

        /// 注册区域到战场管理器
        void RegisterZone(uint32 zoneid);

        /// 检查玩家是否在战场区域内
        bool HasPlayer(Player* player) const;

        /// 为指定阵营的所有玩家施放/移除法术
        void TeamCastSpell(TeamId team, int32 spellId);
};

#endif
