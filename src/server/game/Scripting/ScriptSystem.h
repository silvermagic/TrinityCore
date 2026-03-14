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
 * @file ScriptSystem.h
 * @brief 脚本系统管理器头文件
 *
 * 本模块负责管理脚本相关的路径数据，包括：
 * - 脚本路点（Waypoint）数据的存储和查询
 * - 样条链（Spline Chain）数据的存储和查询
 *
 * 这些数据主要用于NPC的移动脚本，控制生物在地图上的巡逻路径。
 */

#ifndef SC_SYSTEM_H
#define SC_SYSTEM_H

#include "Define.h"
#include "Hash.h"
#include "WaypointDefines.h"
#include <unordered_map>
#include <vector>

class Creature;
struct SplineChainLink;

/**
 * @class SystemMgr
 * @brief 脚本系统管理器（单例模式）
 *
 * 负责管理和存储脚本相关的移动数据：
 * - 脚本路点数据：定义生物的巡逻路径点
 * - 样条链数据：定义平滑的移动轨迹
 *
 * 该类在服务器启动时从数据库加载所有脚本移动数据，
 * 并在运行时提供快速查询接口。
 *
 * 使用方式：
 * - 单例访问：sScriptSystemMgr->GetPath(creatureEntry)
 * - 服务器启动时调用 LoadScriptWaypoints() 和 LoadScriptSplineChains()
 */
class TC_GAME_API SystemMgr
{
    public:
        /**
         * @brief 获取单例实例
         * @return SystemMgr单例指针
         *
         * 使用静态局部变量实现线程安全的单例模式（C++11 magic statics）
         */
        static SystemMgr* instance();

        // ==================== 数据库加载函数 ====================

        /**
         * @brief 从数据库加载脚本路点数据
         *
         * 从 script_waypoint 表中加载所有生物的路点数据。
         * 每个路点包含位置坐标和等待时间。
         *
         * @note 调用时机：服务器启动初始化阶段
         * @note 性能：会清空现有数据，需要谨慎调用（通常只在启动时调用一次）
         */
        void LoadScriptWaypoints();

        /**
         * @brief 从数据库加载脚本样条链数据
         *
         * 从 script_spline_chain_meta 和 script_spline_chain_waypoints 表中
         * 加载样条链元数据和路点数据，用于平滑移动。
         *
         * @note 调用时机：服务器启动初始化阶段
         * @note 性能：会清空现有数据，需要谨慎调用（通常只在启动时调用一次）
         */
        void LoadScriptSplineChains();

        // ==================== 数据查询函数 ====================

        /**
         * @brief 获取指定生物的路点路径
         * @param creatureEntry 生物模板ID（entry）
         * @return 路点路径指针，如果不存在则返回nullptr
         *
         * 用于获取生物的完整巡逻路径数据
         */
        WaypointPath const* GetPath(uint32 creatureEntry) const;

        /**
         * @brief 获取指定生物的样条链数据
         * @param entry 生物模板ID
         * @param chainId 样条链ID
         * @return 样条链数据指针，如果不存在则返回nullptr
         *
         * 用于获取生物的平滑移动轨迹
         */
        std::vector<SplineChainLink> const* GetSplineChain(uint32 entry, uint16 chainId) const;

        /**
         * @brief 获取指定生物对象的样条链数据（重载版本）
         * @param who 生物对象指针
         * @param id 样条链ID
         * @return 样条链数据指针，如果不存在则返回nullptr
         *
         * 便捷函数，通过生物对象获取样条链
         */
        std::vector<SplineChainLink> const* GetSplineChain(Creature const* who, uint16 id) const;

    private:
        /** @brief 样条链键类型：生物entry + 链ID的组合 */
        typedef std::pair<uint32, uint16> ChainKeyType;

        /** @brief 私有构造函数（单例模式） */
        SystemMgr();
        /** @brief 私有析构函数（单例模式） */
        ~SystemMgr();

        /** @brief 禁用拷贝构造 */
        SystemMgr(SystemMgr const&) = delete;
        /** @brief 禁用赋值运算符 */
        SystemMgr& operator=(SystemMgr const&) = delete;

        /** @brief 路点数据存储：生物entry -> 路点路径 */
        std::unordered_map<uint32, WaypointPath> _waypointStore;

        /** @brief 样条链数据存储：(生物entry, 链ID) -> 样条链数据 */
        std::unordered_map<ChainKeyType, std::vector<SplineChainLink>> m_mSplineChainsMap;
};

#define sScriptSystemMgr SystemMgr::instance()

#endif
