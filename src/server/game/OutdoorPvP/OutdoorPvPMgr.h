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
 * @file OutdoorPvPMgr.h
 * @brief 户外PvP管理器头文件
 *
 * 本文件定义了户外PvP管理器(OutdoorPvPMgr)类，负责管理服务器上所有户外PvP区域。
 *
 * 主要职责：
 * 1. 初始化和管理所有户外PvP实例
 * 2. 处理玩家进入/离开户外PvP区域的事件
 * 3. 路由玩家事件到对应的户外PvP实例
 * 4. 定期更新所有户外PvP区域的状态
 *
 * 设计模式：
 * - 单例模式：全局唯一实例，通过sOutdoorPvPMgr宏访问
 * - 管理器模式：集中管理所有户外PvP实例的生命周期
 *
 * 使用流程：
 * 1. 服务器启动时调用InitOutdoorPvP()初始化
 * 2. 玩家进入/离开区域时调用相应的Handle函数
 * 3. 世界更新循环中调用Update()更新所有争夺点
 * 4. 服务器关闭时调用Die()清理资源
 */

#ifndef OUTDOOR_PVP_MGR_H_
#define OUTDOOR_PVP_MGR_H_

/// 户外PvP目标更新间隔（毫秒）
/// 控制争夺点占领进度的更新频率
#define OUTDOORPVP_OBJECTIVE_UPDATE_INTERVAL 1000

#include "OutdoorPvP.h"
#include <array>
#include <unordered_map>

// 前向声明
class Player;
class GameObject;
class Creature;
class ZoneScript;
struct GossipMenuItems;
enum LocaleConstant : uint8;

/**
 * @class OutdoorPvPMgr
 * @brief 户外PvP管理器
 *
 * 单例类，负责管理服务器上所有户外PvP区域的创建、更新和销毁。
 * 作为户外PvP系统的中央协调器，处理玩家事件路由和状态同步。
 *
 * 主要功能：
 * - 从数据库加载户外PvP模板并创建实例
 * - 管理区域ID到户外PvP实例的映射
 * - 处理玩家进入/离开/复活事件
 * - 定期更新所有户外PvP区域
 * - 路由法术、游戏对象、NPC交互事件
 *
 * 线程安全：
 * - 主线程单例，无需线程同步
 * - 所有公共方法都在世界更新线程中调用
 */
class TC_GAME_API OutdoorPvPMgr
{
    private:
        /**
         * @brief 私有构造函数（单例模式）
         */
        OutdoorPvPMgr();

        /**
         * @brief 私有析构函数
         */
        ~OutdoorPvPMgr() { };

    public:
        /**
         * @brief 获取单例实例
         * @return OutdoorPvPMgr单例指针
         */
        static OutdoorPvPMgr* instance();

        /**
         * @brief 初始化所有户外PvP事件
         *
         * 从数据库加载户外PvP模板，创建并初始化所有户外PvP实例
         * 调用时机：服务器启动时
         */
        void InitOutdoorPvP();

        /**
         * @brief 清理所有户外PvP实例
         *
         * 删除所有户外PvP实例及其关联资源
         * 调用时机：服务器关闭时
         */
        void Die();

        /**
         * @brief 处理玩家进入户外PvP区域
         * @param player 进入的玩家指针
         * @param areaflag 区域标志（区域ID）
         *
         * 查找对应的户外PvP实例并通知玩家进入事件
         */
        void HandlePlayerEnterZone(Player* player, uint32 areaflag);

        /**
         * @brief 处理玩家离开户外PvP区域
         * @param player 离开的玩家指针
         * @param areaflag 区域标志（区域ID）
         *
         * 查找对应的户外PvP实例并通知玩家离开事件
         */
        void HandlePlayerLeaveZone(Player* player, uint32 areaflag);

        /**
         * @brief 处理玩家复活
         * @param player 复活的玩家指针
         * @param areaflag 区域标志（区域ID）
         *
         * 通知对应户外PvP实例玩家复活事件
         */
        void HandlePlayerResurrects(Player* player, uint32 areaflag);

        /**
         * @brief 根据区域ID获取对应的户外PvP实例
         * @param zoneid 区域ID
         * @return 对应的OutdoorPvP指针，无则返回nullptr
         */
        OutdoorPvP* GetOutdoorPvPToZoneId(uint32 zoneid);

        /**
         * @brief 处理自定义法术
         * @param player 施法玩家指针
         * @param spellId 法术ID
         * @param go 目标游戏对象（可能为空）
         * @return 是否成功处理
         *
         * 处理不在DBC中的自定义法术
         */
        bool HandleCustomSpell(Player* player, uint32 spellId, GameObject* go);

        /**
         * @brief 处理打开游戏对象
         * @param player 操作玩家指针
         * @param go 被打开的游戏对象指针
         * @return 是否成功处理
         */
        bool HandleOpenGo(Player* player, GameObject* go);

        /**
         * @brief 获取区域的脚本实例
         * @param zoneId 区域ID
         * @return ZoneScript指针，无则返回nullptr
         */
        ZoneScript* GetZoneScript(uint32 zoneId);

        /**
         * @brief 添加区域到户外PvP映射
         * @param zoneid 区域ID
         * @param handle 户外PvP实例指针
         */
        void AddZone(uint32 zoneid, OutdoorPvP* handle);

        /**
         * @brief 更新所有户外PvP实例
         * @param diff 距离上次更新的时间间隔（毫秒）
         *
         * 定期调用，更新所有争夺点的占领进度
         */
        void Update(uint32 diff);

        /**
         * @brief 处理NPC对话选项
         * @param player 玩家指针
         * @param creature NPC指针
         * @param gossipid 选项ID
         */
        void HandleGossipOption(Player* player, Creature* creature, uint32 gossipid);

        /**
         * @brief 检查玩家是否可以与NPC对话
         * @param player 玩家指针
         * @param creature NPC指针
         * @param gso 对话菜单项
         * @return 是否可以对话
         */
        bool CanTalkTo(Player* player, Creature* creature, GossipMenuItems const& gso);

        /**
         * @brief 处理玩家丢弃旗帜
         * @param player 玩家指针
         * @param spellId 法术ID
         */
        void HandleDropFlag(Player* player, uint32 spellId);

        /**
         * @brief 获取防御消息文本
         * @param zoneId 区域ID
         * @param id 广播文本ID
         * @param locale 语言区域设置
         * @return 本地化的消息文本
         */
        std::string GetDefenseMessage(uint32 zoneId, uint32 id, LocaleConstant locale) const;

    private:
        /// 户外PvP实例集合类型
        typedef std::vector<OutdoorPvP*> OutdoorPvPSet;
        /// 区域ID到户外PvP实例的映射类型
        typedef std::unordered_map<uint32 /*zoneid*/, OutdoorPvP*> OutdoorPvPMap;
        /// 户外PvP脚本ID数组类型
        typedef std::array<uint32, MAX_OUTDOORPVP_TYPES> OutdoorPvPScriptIds;

        OutdoorPvPSet m_OutdoorPvPSet;             ///< 所有已初始化的户外PvP实例集合
                                                   ///< 用于初始化和清理时遍历所有实例

        OutdoorPvPMap m_OutdoorPvPMap;             ///< 区域ID到户外PvP实例的映射
                                                   ///< 用于快速查找玩家所在区域的户外PvP实例

        OutdoorPvPScriptIds m_OutdoorPvPDatas = {}; ///< 户外PvP模板脚本ID数组
                                                    ///< 索引对应OutdoorPvPTypes枚举，值为脚本ID

        uint32 m_UpdateTimer;                      ///< 更新计时器（毫秒）
                                                   ///< 用于控制更新频率
};

/// 全局访问宏，获取OutdoorPvPMgr单例实例
#define sOutdoorPvPMgr OutdoorPvPMgr::instance()

#endif /*OUTDOOR_PVP_MGR_H_*/
