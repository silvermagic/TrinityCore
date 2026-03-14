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
 * @file BattlefieldMgr.h
 * @brief 战场管理器头文件
 *
 * 本文件定义了战场管理器（BattlefieldMgr）类，负责：
 * - 管理所有战场实例的创建和销毁
 * - 处理玩家进入/离开战场区域的事件
 * - 将区域ID映射到对应的战场实例
 * - 协调所有战场的更新
 *
 * 战场管理器是一个单例类，通过sBattlefieldMgr宏访问
 */

#ifndef BATTLEFIELD_MGR_H_
#define BATTLEFIELD_MGR_H_

#include "Battlefield.h"

class Player;
class ZoneScript;

/**
 * @class BattlefieldMgr
 * @brief 战场管理器类
 *
 * 单例模式的战场管理器，负责管理游戏中的所有战场实例。
 *
 * 主要职责：
 * 1. 初始化战场：从数据库加载战场模板并创建实例
 * 2. 区域映射：维护区域ID到战场实例的映射关系
 * 3. 事件处理：处理玩家进入/离开战场区域的触发事件
 * 4. 更新协调：定期更新所有活跃的战场实例
 *
 * 使用方式：
 * - 通过sBattlefieldMgr宏访问单例实例
 * - 服务器启动时调用InitBattlefield初始化
 * - 世界更新循环中调用Update
 * - 玩法区域切换时自动触发HandlePlayerEnterZone/HandlePlayerLeaveZone
 */
class TC_GAME_API BattlefieldMgr
{
    public:
        /**
         * @brief 获取单例实例
         * @return 战场管理器单例指针
         */
        static BattlefieldMgr* instance();

        /**
         * @brief 初始化所有战场
         *
         * 从数据库加载战场模板，创建并初始化所有战场实例
         * 在服务器启动时调用
         *
         * 加载流程：
         * 1. 查询battlefield_template表
         * 2. 根据脚本名创建战场实例
         * 3. 调用每个战场的SetupBattlefield方法
         * 4. 将成功的实例加入管理列表
         */
        void InitBattlefield();

        /**
         * @brief 处理玩家进入战场区域
         * @param player 进入区域的玩家指针
         * @param zoneId 区域ID
         *
         * 当玩家进入一个新区域时由Player::UpdateZone调用
         * 如果该区域关联了战场，则调用战场的HandlePlayerEnterZone方法
         */
        void HandlePlayerEnterZone(Player* player, uint32 zoneId);

        /**
         * @brief 处理玩家离开战场区域
         * @param player 离开区域的玩家指针
         * @param zoneId 区域ID
         *
         * 当玩家离开一个区域时由Player::UpdateZone调用
         * 如果该区域关联了战场，则调用战场的HandlePlayerLeaveZone方法
         */
        void HandlePlayerLeaveZone(Player* player, uint32 zoneId);

        /**
         * @brief 根据区域ID获取战场实例
         * @param zoneId 区域ID
         * @return 战场指针，未找到返回nullptr
         */
        Battlefield* GetBattlefieldToZoneId(uint32 zoneId);

        /**
         * @brief 根据战斗ID获取战场实例
         * @param battleId 战斗ID
         * @return 战场指针，未找到返回nullptr
         */
        Battlefield* GetBattlefieldByBattleId(uint32 battleId);

        /**
         * @brief 根据区域ID获取区域脚本
         * @param zoneId 区域ID
         * @return 区域脚本指针，未找到返回nullptr
         *
         * 用于获取该区域关联的脚本接口
         */
        ZoneScript* GetZoneScript(uint32 zoneId);

        /**
         * @brief 添加区域到战场的映射
         * @param zoneId 区域ID
         * @param bf 战场指针
         *
         * 战场初始化时调用，建立区域与战场的关联
         */
        void AddZone(uint32 zoneId, Battlefield* bf);

        /**
         * @brief 更新所有战场
         * @param diff 距上次更新的时间间隔（毫秒）
         *
         * 由World::Update每帧调用
         * 每隔BATTLEFIELD_OBJECTIVE_UPDATE_INTERVAL（1000ms）更新一次所有战场
         */
        void Update(uint32 diff);

    private:
        BattlefieldMgr();
        ~BattlefieldMgr();

        /// 战场集合类型（用于存储所有战场实例）
        typedef std::vector<Battlefield*> BattlefieldSet;
        /// 区域到战场的映射类型
        typedef std::map<uint32 /*zoneId*/, Battlefield*> BattlefieldMap;

        /**
         * @brief 所有战场实例集合
         * 存储所有已初始化的战场实例
         * 用于初始化和清理时遍历所有战场
         */
        BattlefieldSet _battlefieldSet;

        /**
         * @brief 区域ID到战场的映射表
         * 快速查找某个区域是否关联了战场
         * 用于玩家进入/离开区域时的快速查找
         */
        BattlefieldMap _battlefieldMap;

        /**
         * @brief 更新定时器
         * 累积时间，达到BATTLEFIELD_OBJECTIVE_UPDATE_INTERVAL时触发更新
         */
        uint32 _updateTimer;
};

/// 全局访问宏
#define sBattlefieldMgr BattlefieldMgr::instance()

#endif // BATTLEFIELD_MGR_H_
