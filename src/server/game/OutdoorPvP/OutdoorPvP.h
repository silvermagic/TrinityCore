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
 * @file OutdoorPvP.h
 * @brief 户外PvP系统核心头文件
 *
 * 本文件定义了户外PvP(OutdoorPvP)系统的核心类和数据结构，用于实现游戏世界中的户外PvP功能。
 * 户外PvP是指在游戏世界特定区域进行的玩家对战活动，包括：
 * - 目标争夺点(OPvPCapturePoint)：玩家争夺的特定地点
 * - 阵营控制：联盟和部落争夺区域控制权
 * - 动态世界状态：根据控制状态改变游戏世界
 *
 * 主要功能包括：
 * 1. 争夺点的占领和状态管理
 * 2. 玩家进入/离开区域的检测
 * 3. 击杀奖励和Buff系统
 * 4. 世界状态同步
 *
 * 支持的户外PvP类型：
 * - HP: Hellfire Peninsula (地狱火半岛)
 * - NA: Nagrand (纳格兰)
 * - TF: Terokkar Forest (泰罗卡森林)
 * - ZM: Zangarmarsh (赞加沼泽)
 * - SI: Silithus (希利苏斯)
 * - EP: Eastern Plaguelands (东瘟疫之地)
 */

#ifndef OUTDOOR_PVP_H_
#define OUTDOOR_PVP_H_

#include "GameObjectData.h"
#include "Position.h"
#include "SharedDefines.h"
#include "ZoneScript.h"
#include <map>

/**
 * @brief 户外PvP类型枚举
 *
 * 定义了游戏中所有户外PvP区域的类型标识符
 */
enum OutdoorPvPTypes
{
    OUTDOOR_PVP_HP = 1,    ///< Hellfire Peninsula - 地狱火半岛
    OUTDOOR_PVP_NA,        ///< Nagrand - 纳格兰（哈兰）
    OUTDOOR_PVP_TF,        ///< Terokkar Forest - 泰罗卡森林（奥金顿）
    OUTDOOR_PVP_ZM,        ///< Zangarmarsh - 赞加沼泽（盘牙湖泊）
    OUTDOOR_PVP_SI,        ///< Silithus - 希利苏斯
    OUTDOOR_PVP_EP,        ///< Eastern Plaguelands - 东瘟疫之地（瘟疫之地塔楼）

    MAX_OUTDOORPVP_TYPES   ///< 户外PvP类型数量上限
};

/**
 * @brief 目标状态枚举
 *
 * 定义争夺点的所有可能状态，用于描述争夺点的阵营控制情况
 */
enum ObjectiveStates
{
    OBJECTIVESTATE_NEUTRAL = 0,                    ///< 中立状态 - 无阵营控制
    OBJECTIVESTATE_ALLIANCE,                       ///< 联盟控制
    OBJECTIVESTATE_HORDE,                          ///< 部落控制
    OBJECTIVESTATE_NEUTRAL_ALLIANCE_CHALLENGE,     ///< 中立->联盟争夺中
    OBJECTIVESTATE_NEUTRAL_HORDE_CHALLENGE,        ///< 中立->部落争夺中
    OBJECTIVESTATE_ALLIANCE_HORDE_CHALLENGE,       ///< 联盟->部落争夺中
    OBJECTIVESTATE_HORDE_ALLIANCE_CHALLENGE        ///< 部落->联盟争夺中
};

/**
 * @brief 获取对立阵营ID的宏
 * @param a 当前阵营ID（TEAM_ALLIANCE 或 TEAM_HORDE）
 * @return 对立阵营ID
 */
#define OTHER_TEAM(a) (a == TEAM_ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE)

/**
 * @brief 游戏对象(GameObject)生成数据结构
 *
 * 用于定义户外PvP区域中需要生成的游戏对象（如旗帜、塔楼等）的模板数据
 */
struct go_type
{
    uint32 entry;       ///< GameObject模板ID（来自gameobject_template表）
    uint32 map;         ///< 地图ID
    Position pos;       ///< 位置坐标（x, y, z）
    QuaternionData rot; ///< 旋转四元数（用于对象朝向）
};

/**
 * @brief 生物(Creature)生成数据结构
 *
 * 用于定义户外PvP区域中需要生成的生物（如NPC、守卫等）的模板数据
 */
struct creature_type
{
    uint32 entry;       ///< Creature模板ID（来自creature_template表）
    uint32 map;         ///< 地图ID
    Position pos;       ///< 位置坐标（x, y, z）
};

namespace WorldPackets
{
    namespace WorldState
    {
        class InitWorldStates;
    }
}

// 前向声明
class Creature;
class GameObject;
class Map;
class OutdoorPvP;
class Player;
class Unit;
class WorldPacket;
struct GossipMenuItems;

/**
 * @class OPvPCapturePoint
 * @brief 户外PvP争夺点基类
 *
 * 表示户外PvP中的一个可争夺目标点，如塔楼、哨所等。
 * 负责管理：
 * - 争夺点的占领进度和状态
 * - 玩家参与争夺的检测
 * - 阵营控制状态的转换
 * - 相关游戏对象和生物的生成/删除
 *
 * 核心机制：
 * 1. 进度条系统：根据双方玩家数量决定占领速度
 * 2. 状态机：管理中立、联盟、部落三种控制状态的转换
 * 3. 世界状态同步：向客户端发送UI更新
 *
 * 派生类需要实现：
 * - ChangeState(): 状态改变时的处理逻辑
 */
class TC_GAME_API OPvPCapturePoint
{
    public:
        /**
         * @brief 构造函数
         * @param pvp 所属的OutdoorPvP实例指针
         */
        OPvPCapturePoint(OutdoorPvP* pvp);

        /**
         * @brief 虚析构函数
         */
        virtual ~OPvPCapturePoint() { }

        /**
         * @brief 填充初始世界状态数据
         * @param packet 世界状态初始化包
         *
         * 当玩家进入区域时调用，用于初始化客户端的世界状态UI
         */
        virtual void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& /*packet*/) { }

        /**
         * @brief 向所有在场玩家发送世界状态更新
         * @param field 世界状态字段ID
         * @param value 新的值
         *
         * 调用时机：当争夺点状态改变时
         */
        void SendUpdateWorldState(uint32 field, uint32 value);

        /**
         * @brief 发送目标完成通知
         * @param id 怪物凭证ID
         * @param guid 对象GUID
         *
         * 向控制阵营的在场玩家发送击杀奖励凭证
         */
        void SendObjectiveComplete(uint32 id, ObjectGuid guid);

        /**
         * @brief 处理玩家进入争夺区域
         * @param player 进入的玩家指针
         * @return 是否成功添加到活跃玩家列表
         *
         * 当玩家进入争夺点的检测范围时调用
         * - 发送当前占领进度UI
         * - 将玩家添加到活跃玩家列表
         */
        virtual bool HandlePlayerEnter(Player* player);

        /**
         * @brief 处理玩家离开争夺区域
         * @param player 离开的玩家指针
         *
         * 当玩家离开争夺点的检测范围时调用
         * - 移除占领进度UI
         * - 从活跃玩家列表中移除
         */
        virtual void HandlePlayerLeave(Player* player);

        /**
         * @brief 检查玩家是否在争夺目标范围内
         * @param player 要检查的玩家
         * @return 是否在范围内
         */
        bool IsInsideObjective(Player* player) const;

        /**
         * @brief 处理自定义法术
         * @param player 施法玩家
         * @param spellId 法术ID
         * @param go 目标游戏对象（可能为空）
         * @return 是否成功处理
         */
        virtual bool HandleCustomSpell(Player* player, uint32 spellId, GameObject* go);

        /**
         * @brief 处理打开游戏对象
         * @param player 操作玩家
         * @param go 被打开的游戏对象
         * @return 游戏对象类型ID，-1表示未找到
         */
        virtual int32 HandleOpenGo(Player* player, GameObject* go);

        /**
         * @brief 更新争夺点状态
         * @param diff 距离上次更新的时间间隔（毫秒）
         * @return 状态是否发生改变
         *
         * 核心更新逻辑：
         * 1. 检测区域内玩家并更新活跃列表
         * 2. 根据双方人数计算占领进度变化
         * 3. 更新争夺点状态
         * 4. 触发状态改变事件
         *
         * 性能注意：每次更新会进行区域玩家搜索，需控制更新频率
         */
        virtual bool Update(uint32 diff);

        /**
         * @brief 状态改变事件
         *
         * 纯虚函数，派生类必须实现
         * 当争夺点的控制状态改变时调用（如从中立变为联盟控制）
         */
        virtual void ChangeState() = 0;

        /**
         * @brief 阵营改变事件
         * @param oldTeam 旧的阵营ID
         *
         * 当争夺点的控制阵营改变时调用
         */
        virtual void ChangeTeam(TeamId /*oldTeam*/) { }

        /**
         * @brief 发送阶段改变通知
         *
         * 向客户端发送占领进度条的更新
         */
        virtual void SendChangePhase();

        /**
         * @brief 处理玩家与NPC对话选项
         * @param player 玩家指针
         * @param guid NPC的GUID
         * @param gossipid 选项ID
         * @return 是否成功处理
         */
        virtual bool HandleGossipOption(Player* player, Creature* guid, uint32 gossipid);

        /**
         * @brief 检查玩家是否可以与NPC对话
         * @param player 玩家指针
         * @param c NPC指针
         * @param gso 对话菜单项
         * @return 是否可以对话
         */
        virtual bool CanTalkTo(Player* player, Creature* c, GossipMenuItems const& gso);

        /**
         * @brief 处理玩家丢弃旗帜
         * @param player 玩家指针
         * @param spellId 法术ID
         * @return 是否成功处理
         */
        virtual bool HandleDropFlag(Player* player, uint32 spellId);

        /**
         * @brief 删除所有生成的对象和生物
         *
         * 清理争夺点创建的所有游戏对象和生物
         */
        virtual void DeleteSpawns();

        ObjectGuid::LowType m_capturePointSpawnId;  ///< 争夺点游戏对象的生成ID

        GameObject* m_capturePoint;                  ///< 争夺点游戏对象指针

        /**
         * @brief 添加游戏对象到管理列表
         * @param type 对象类型标识符
         * @param guid 对象的低GUID（spawnId）
         * @param entry 对象模板ID（可选，为0时从数据库查询）
         */
        void AddGO(uint32 type, ObjectGuid::LowType guid, uint32 entry = 0);

        /**
         * @brief 添加生物到管理列表
         * @param type 生物类型标识符
         * @param guid 生物的低GUID（spawnId）
         * @param entry 生物模板ID（可选，为0时从数据库查询）
         */
        void AddCre(uint32 type, ObjectGuid::LowType guid, uint32 entry = 0);

        /**
         * @brief 设置争夺点数据
         * @param entry 游戏对象模板ID
         * @param map 地图ID
         * @param pos 位置
         * @param rot 旋转四元数
         * @return 是否设置成功
         *
         * 从游戏对象模板读取争夺点参数：
         * - 最大进度值
         * - 占领速度
         * - 中性百分比
         */
        bool SetCapturePointData(uint32 entry, uint32 map, Position const& pos, QuaternionData const& rot);

    protected:
        /**
         * @brief 添加游戏对象到世界
         * @param type 对象类型标识符
         * @param entry 对象模板ID
         * @param map 地图ID
         * @param pos 位置
         * @param rot 旋转四元数
         * @return 是否成功创建
         */
        bool AddObject(uint32 type, uint32 entry, uint32 map, Position const& pos, QuaternionData const& rot);

        /**
         * @brief 添加生物到世界
         * @param type 生物类型标识符
         * @param entry 生物模板ID
         * @param map 地图ID
         * @param pos 位置
         * @param teamId 阵营ID（默认中立）
         * @param spawntimedelay 生成延迟时间
         * @return 是否成功创建
         */
        bool AddCreature(uint32 type, uint32 entry, uint32 map, Position const& pos, TeamId teamId = TEAM_NEUTRAL, uint32 spawntimedelay = 0);

        /**
         * @brief 从世界中删除游戏对象
         * @param type 对象类型标识符
         * @return 是否成功删除
         */
        bool DelObject(uint32 type);

        /**
         * @brief 从世界中删除生物
         * @param type 生物类型标识符
         * @return 是否成功删除
         */
        bool DelCreature(uint32 type);

        /**
         * @brief 删除争夺点游戏对象
         * @return 是否成功删除
         */
        bool DelCapturePoint();

    protected:
        GuidSet m_activePlayers[2];            ///< 活跃玩家列表 [0]=联盟, [1]=部落

        float m_maxValue;                      ///< 完全占领所需的最大进度值
        float m_minValue;                      ///< 中性区域的最小值（m_maxValue * neutralPercent）

        float m_maxSpeed;                      ///< 最大占领速度（进度/毫秒）

        float m_value;                         ///< 当前占领进度值（负=部落倾向，正=联盟倾向）

        TeamId m_team;                         ///< 当前控制阵营

        ObjectiveStates m_OldState;            ///< 上一帧的状态
        ObjectiveStates m_State;               ///< 当前状态

        uint32 m_neutralValuePct;              ///< 中性区域百分比（进度条中心区域）

        OutdoorPvP* m_PvP;                     ///< 所属的OutdoorPvP实例指针

        // 对象和生物的存储映射
        std::map<uint32, ObjectGuid::LowType> m_Objects;         ///< 类型->spawnId映射
        std::map<uint32, ObjectGuid::LowType> m_Creatures;       ///< 类型->spawnId映射
        std::map<ObjectGuid::LowType, uint32> m_ObjectTypes;     ///< spawnId->类型反向映射
        std::map<ObjectGuid::LowType, uint32> m_CreatureTypes;   ///< spawnId->类型反向映射
};

/**
 * @class OutdoorPvP
 * @brief 户外PvP系统基类
 *
 * 继承自ZoneScript，作为特定户外PvP区域的处理器基类。
 * 每个户外PvP区域（如地狱火半岛、纳格兰等）都有一个派生类实现。
 *
 * 主要职责：
 * 1. 管理该区域内的所有争夺点（OPvPCapturePoint）
 * 2. 处理玩家进入/离开区域的事件
 * 3. 管理区域内玩家列表
 * 4. 处理击杀事件和奖励
 * 5. 同步世界状态给客户端
 *
 * 生命周期：
 * - 服务器启动时由OutdoorPvPMgr创建并初始化（SetupOutdoorPvP）
 * - 服务器关闭时由OutdoorPvPMgr销毁
 *
 * 派生类需要实现：
 * - SetupOutdoorPvP(): 初始化争夺点和相关对象
 * - HandleKillImpl(): 处理击杀奖励逻辑
 * - SendRemoveWorldStates(): 清理客户端世界状态
 */
class TC_GAME_API OutdoorPvP : public ZoneScript
{
    friend class OutdoorPvPMgr;  ///< OutdoorPvPMgr需要访问私有成员

    public:
        /**
         * @brief 构造函数
         */
        OutdoorPvP();

        /**
         * @brief 虚析构函数
         *
         * 析构时自动调用DeleteSpawns()清理所有生成的对象
         */
        virtual ~OutdoorPvP();

        /**
         * @brief 删除所有生成的游戏对象和生物
         *
         * 清理：
         * - 所有争夺点及其关联对象
         * - 脚本注册的游戏对象和生物
         */
        void DeleteSpawns();

        /// 争夺点映射类型：spawnId -> OPvPCapturePoint指针
        typedef std::map<ObjectGuid::LowType/*spawnId*/, OPvPCapturePoint*> OPvPCapturePointMap;
        /// 游戏对象脚本对类型
        typedef std::pair<ObjectGuid::LowType, GameObject*> GoScriptPair;
        /// 生物脚本对类型
        typedef std::pair<ObjectGuid::LowType, Creature*> CreatureScriptPair;

        /**
         * @brief 填充初始世界状态
         * @param packet 世界状态初始化包
         *
         * 当玩家进入区域时，向其发送当前的世界状态数据
         */
        virtual void FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& /*packet*/) { }

        /**
         * @brief 处理区域触发器
         * @param player 触发玩家
         * @param trigger 触发器ID
         * @return 是否成功处理
         */
        virtual bool HandleAreaTrigger(Player* player, uint32 trigger);

        /**
         * @brief 处理自定义法术
         * @param player 施法玩家
         * @param spellId 法术ID
         * @param go 目标游戏对象
         * @return 是否成功处理
         */
        virtual bool HandleCustomSpell(Player* player, uint32 spellId, GameObject* go);

        /**
         * @brief 处理打开游戏对象
         * @param player 操作玩家
         * @param go 被打开的游戏对象
         * @return 是否成功处理
         */
        virtual bool HandleOpenGo(Player* player, GameObject* go);

        /**
         * @brief 初始化户外PvP
         * @return 是否初始化成功
         *
         * 派生类实现此函数以：
         * - 创建争夺点
         * - 注册区域
         * - 初始化游戏对象和生物
         */
        virtual bool SetupOutdoorPvP() {return true;}

        /// 游戏对象创建时调用（实现ZoneScript接口）
        void OnGameObjectCreate(GameObject* go) override;
        /// 游戏对象移除时调用（实现ZoneScript接口）
        void OnGameObjectRemove(GameObject* go) override;
        /// 生物创建时调用（实现ZoneScript接口）
        void OnCreatureCreate(Creature*) override;
        /// 生物移除时调用（实现ZoneScript接口）
        void OnCreatureRemove(Creature*) override;

        /**
         * @brief 向区域内所有玩家发送世界状态更新
         * @param field 世界状态字段ID
         * @param value 新值
         */
        void SendUpdateWorldState(uint32 field, uint32 value);

        /**
         * @brief 更新所有争夺点状态
         * @param diff 距离上次更新的时间间隔（毫秒）
         * @return 是否有争夺点状态改变
         *
         * 由OutdoorPvPMgr定期调用，更新所有争夺点的占领进度
         */
        virtual bool Update(uint32 diff);

        /**
         * @brief 处理击杀事件
         * @param killer 击杀者
         * @param killed 被击杀者
         *
         * 处理玩家或NPC的击杀，向小队成员分发奖励
         */
        virtual void HandleKill(Player* killer, Unit* killed);

        /**
         * @brief 击杀处理的具体实现
         * @param killer 击杀者
         * @param killed 被击杀者
         *
         * 派生类实现具体的击杀奖励逻辑
         */
        virtual void HandleKillImpl(Player* /*killer*/, Unit* /*killed*/) { }

        /**
         * @brief 检查玩家是否在任何争夺点范围内
         * @param player 要检查的玩家
         * @return 是否在某个争夺点范围内
         */
        bool IsInsideObjective(Player* player) const;

        /**
         * @brief 授予击杀奖励
         * @param player 获得奖励的玩家
         */
        virtual void AwardKillBonus(Player* /*player*/) { }

        /**
         * @brief 获取户外PvP类型ID
         * @return 类型ID
         */
        uint32 GetTypeId() const {return m_TypeId;}

        /**
         * @brief 处理丢弃旗帜
         * @param player 玩家指针
         * @param spellId 法术ID
         * @return 是否成功处理
         */
        virtual bool HandleDropFlag(Player* player, uint32 spellId);

        /**
         * @brief 处理NPC对话选项
         * @param player 玩家指针
         * @param creature NPC指针
         * @param gossipid 选项ID
         * @return 是否成功处理
         */
        virtual bool HandleGossipOption(Player* player, Creature* creature, uint32 gossipid);

        /**
         * @brief 检查玩家是否可以与NPC对话
         * @param player 玩家指针
         * @param c NPC指针
         * @param gso 对话菜单项
         * @return 是否可以对话
         */
        virtual bool CanTalkTo(Player* player, Creature* c, GossipMenuItems const& gso);

        /**
         * @brief 为指定阵营应用Buff
         * @param team 获得正面Buff的阵营
         * @param spellId 正面Buff法术ID
         * @param spellId2 对立阵营获得的负面效果ID（可选）
         *
         * 便捷方法，给一个阵营添加Buff，另一阵营移除或添加Debuff
         */
        void TeamApplyBuff(TeamId team, uint32 spellId, uint32 spellId2 = 0);

        /**
         * @brief 将团队常量转换为TeamId
         * @param team 团队常量（ALLIANCE/HORDE）
         * @return 对应的TeamId枚举值
         */
        static TeamId GetTeamIdByTeam(uint32 team)
        {
            switch (team)
            {
                case ALLIANCE:
                    return TEAM_ALLIANCE;
                case HORDE:
                    return TEAM_HORDE;
                default:
                    return TEAM_NEUTRAL;
            }
        }

        /**
         * @brief 发送防御消息
         * @param zoneId 区域ID
         * @param id 广播文本ID
         *
         * 向区域内玩家发送防御相关的广播消息
         */
        void SendDefenseMessage(uint32 zoneId, uint32 id);

        /**
         * @brief 获取地图实例
         * @return 地图指针
         */
        Map* GetMap() const { return m_map; }

    protected:
        OPvPCapturePointMap m_capturePoints;  ///< 争夺点映射：spawnId -> OPvPCapturePoint

        GuidSet m_players[2];                 ///< 区域内玩家列表 [0]=联盟, [1]=部落

        uint32 m_TypeId;                      ///< 户外PvP类型ID（对应OutdoorPvPTypes枚举）

        bool m_sendUpdate;                    ///< 是否发送世界状态更新（用于控制广播）

        /**
         * @brief 移除玩家的世界状态
         * @param player 要移除世界状态的玩家
         *
         * 当玩家离开区域时调用，清理客户端的世界状态UI
         */
        virtual void SendRemoveWorldStates(Player* /*player*/) { }

        /**
         * @brief 向区域内所有玩家广播数据包
         * @param data 要发送的世界数据包
         */
        void BroadcastPacket(WorldPacket & data) const;

        /**
         * @brief 处理玩家进入区域
         * @param player 进入的玩家
         * @param zone 区域ID
         *
         * 将玩家添加到阵营玩家列表
         */
        virtual void HandlePlayerEnterZone(Player* player, uint32 zone);

        /**
         * @brief 处理玩家离开区域
         * @param player 离开的玩家
         * @param zone 区域ID
         *
         * 从所有争夺点移除玩家，清理世界状态，从玩家列表移除
         */
        virtual void HandlePlayerLeaveZone(Player* player, uint32 zone);

        /**
         * @brief 处理玩家复活
         * @param player 复活的玩家
         * @param zone 区域ID
         */
        virtual void HandlePlayerResurrects(Player* player, uint32 zone);

        /**
         * @brief 添加争夺点到管理列表
         * @param cp 争夺点指针
         */
        void AddCapturePoint(OPvPCapturePoint* cp)
        {
            m_capturePoints[cp->m_capturePointSpawnId] = cp;
        }

        /**
         * @brief 获取争夺点
         * @param guid 争夺点的spawnId
         * @return 争夺点指针，未找到返回nullptr
         */
        OPvPCapturePoint* GetCapturePoint(ObjectGuid::LowType guid) const
        {
            OutdoorPvP::OPvPCapturePointMap::const_iterator itr = m_capturePoints.find(guid);
            if (itr != m_capturePoints.end())
                return itr->second;
            return nullptr;
        }

        /**
         * @brief 注册区域到管理器
         * @param zoneid 区域ID
         *
         * 将此户外PvP实例注册到指定区域
         */
        void RegisterZone(uint32 zoneid);

        /**
         * @brief 检查玩家是否在此区域
         * @param player 要检查的玩家
         * @return 是否在此区域
         */
        bool HasPlayer(Player const* player) const;

        /**
         * @brief 为指定阵营施放法术
         * @param team 阵营ID
         * @param spellId 法术ID（正数施放，负数移除）
         *
         * 遍历阵营玩家列表施放法术
         */
        void TeamCastSpell(TeamId team, int32 spellId);

        /**
         * @brief 向区域内玩家广播工作任务
         * @tparam Worker 工作函数对象类型
         * @param _worker 工作对象
         * @param zoneId 区域ID
         */
        template<class Worker>
        void BroadcastWorker(Worker& _worker, uint32 zoneId);

        /**
         * @brief 从区域ID设置地图实例
         * @param zone 区域ID
         *
         * 通过区域ID查找对应的大陆地图并存储
         */
        void SetMapFromZone(uint32 zone);

        std::map<ObjectGuid::LowType, GameObject*> m_GoScriptStore;     ///< 游戏对象脚本存储
        std::map<ObjectGuid::LowType, Creature*> m_CreatureScriptStore; ///< 生物脚本存储

        Map* m_map;  ///< 地图实例指针
};

#endif /*OUTDOOR_PVP_H_*/
