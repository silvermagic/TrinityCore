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

#ifndef _ARENATEAMMGR_H
#define _ARENATEAMMGR_H

/**
 * @file ArenaTeamMgr.h
 * @brief 竞技场队伍管理器头文件
 *
 * 本文件定义了竞技场队伍管理器类 ArenaTeamMgr，负责管理服务器中所有的竞技场队伍。
 * 主要功能包括：
 * - 竞技场队伍的加载、存储和查询
 * - 竞技场队伍ID的生成和管理
 * - 竞技场点数的分发
 *
 * 该管理器采用单例模式，通过 sArenaTeamMgr 宏访问全局实例。
 */

#include "ArenaTeam.h"
#include <unordered_map>

/**
 * @class ArenaTeamMgr
 * @brief 竞技场队伍管理器
 *
 * 竞技场队伍管理器是一个单例类，负责管理服务器中所有竞技场队伍的全局状态。
 * 提供竞技场队伍的创建、查询、删除以及竞技场点数分发等功能。
 *
 * 使用方式：
 * @code
 * // 获取竞技场队伍管理器实例
 * ArenaTeamMgr* mgr = sArenaTeamMgr;
 *
 * // 通过ID查询竞技场队伍
 * ArenaTeam* team = mgr->GetArenaTeamById(teamId);
 * @endcode
 *
 * @note 该类采用单例模式，不应手动创建实例，应通过 sArenaTeamMgr 宏访问。
 * @see ArenaTeam
 */
class TC_GAME_API ArenaTeamMgr
{
private:
    /**
     * @brief 私有构造函数
     *
     * 单例模式，禁止外部实例化。初始化竞技场队伍管理器的内部状态。
     */
    ArenaTeamMgr();

    /**
     * @brief 私有析构函数
     *
     * 清理竞技场队伍管理器资源，释放所有存储的竞技场队伍对象。
     */
    ~ArenaTeamMgr();

public:
    /**
     * @brief 获取竞技场队伍管理器单例实例
     *
     * 返回全局唯一的竞技场队伍管理器实例。
     * 如果实例不存在，会自动创建。
     *
     * @return ArenaTeamMgr* 竞技场队伍管理器实例指针
     *
     * @note 通常使用 sArenaTeamMgr 宏而非直接调用此方法
     */
    static ArenaTeamMgr* instance();

    /**
     * @brief 竞技场队伍容器类型定义
     *
     * 使用哈希映射存储竞技场队伍，键为竞技场队伍ID，值为竞技场队伍指针。
     */
    typedef std::unordered_map<uint32, ArenaTeam*> ArenaTeamContainer;

    /**
     * @brief 根据ID获取竞技场队伍
     *
     * 从竞技场队伍存储中查找指定ID的竞技场队伍。
     *
     * @param arenaTeamId 竞技场队伍ID
     * @return ArenaTeam* 找到的竞技场队伍指针，如果未找到则返回 nullptr
     *
     * @note 返回的指针由管理器管理，调用者不应删除
     *
     * @par 示例：
     * @code
     * ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(12345);
     * if (team)
     * {
     *     // 使用竞技场队伍
     * }
     * @endcode
     */
    ArenaTeam* GetArenaTeamById(uint32 arenaTeamId) const;

    /**
     * @brief 根据名称获取竞技场队伍
     *
     * 通过竞技场队伍名称查找竞技场队伍。
     * 名称比较不区分大小写。
     *
     * @param arenaTeamName 竞技场队伍名称
     * @return ArenaTeam* 找到的竞技场队伍指针，如果未找到则返回 nullptr
     *
     * @note 返回的指针由管理器管理，调用者不应删除
     */
    ArenaTeam* GetArenaTeamByName(std::string_view arenaTeamName) const;

    /**
     * @brief 根据队长获取竞技场队伍
     *
     * 通过竞技场队伍队长的GUID查找竞技场队伍。
     * 每个玩家最多只能担任一个竞技场队伍的队长。
     *
     * @param guid 队长的对象GUID
     * @return ArenaTeam* 找到的竞技场队伍指针，如果未找到则返回 nullptr
     *
     * @note 返回的指针由管理器管理，调用者不应删除
     */
    ArenaTeam* GetArenaTeamByCaptain(ObjectGuid guid) const;

    /**
     * @brief 从数据库加载所有竞技场队伍
     *
     * 从数据库加载服务器中所有竞技场队伍的数据，包括队伍信息和成员信息。
     * 此方法在服务器启动时调用。
     *
     * @note 此方法会清空当前存储的所有竞技场队伍并重新加载
     */
    void LoadArenaTeams();

    /**
     * @brief 添加竞技场队伍到管理器
     *
     * 将一个新的竞技场队伍添加到管理器的存储中。
     * 通常在创建新竞技场队伍时调用。
     *
     * @param arenaTeam 要添加的竞技场队伍指针
     *
     * @warning 管理器接管竞技场队伍的所有权，不应在外部删除该对象
     */
    void AddArenaTeam(ArenaTeam* arenaTeam);

    /**
     * @brief 从管理器移除竞技场队伍
     *
     * 从管理器存储中移除指定ID的竞技场队伍，并删除该竞技场队伍对象。
     * 通常在解散竞技场队伍时调用。
     *
     * @param Id 要移除的竞技场队伍ID
     *
     * @note 此方法会删除竞技场队伍对象，调用后指针将失效
     */
    void RemoveArenaTeam(uint32 Id);

    /**
     * @brief 获取所有竞技场队伍
     *
     * 返回竞技场队伍容器的常引用，用于遍历所有竞技场队伍。
     *
     * @return const ArenaTeamContainer& 竞技场队伍容器的常引用
     *
     * @par 示例：
     * @code
     * for (auto const& pair : sArenaTeamMgr->GetArenaTeams())
     * {
     *     ArenaTeam* team = pair.second;
     *     // 处理竞技场队伍
     * }
     * @endcode
     */
    ArenaTeamContainer const& GetArenaTeams() const { return ArenaTeamStore; }

    /**
     * @brief 分发竞技场点数
     *
     * 根据竞技场队伍的等级和排名，每周分发竞技场点数给队伍成员。
     * 此方法通常在每周重置时自动调用。
     *
     * 分发规则：
     * - 根据队伍等级计算获得的竞技场点数
     * - 需要满足最低场次要求才能获得点数
     * - 高等级队伍获得更多点数
     */
    void DistributeArenaPoints();

    /**
     * @brief 生成新的竞技场队伍ID
     *
     * 生成一个唯一的竞技场队伍ID，用于创建新的竞技场队伍。
     * ID是递增生成的，保证唯一性。
     *
     * @return uint32 新生成的竞技场队伍ID
     */
    uint32 GenerateArenaTeamId();

    /**
     * @brief 设置下一个竞技场队伍ID
     *
     * 设置下一个要分配的竞技场队伍ID。
     * 通常在从数据库加载竞技场队伍后调用，以确保ID生成的连续性。
     *
     * @param Id 下一个竞技场队伍ID的起始值
     *
     * @note 此方法应谨慎使用，通常仅在初始化时调用
     */
    void SetNextArenaTeamId(uint32 Id) { NextArenaTeamId = Id; }

protected:
    uint32 NextArenaTeamId;              ///< 下一个要分配的竞技场队伍ID
    ArenaTeamContainer ArenaTeamStore;   ///< 竞技场队伍存储容器，键为队伍ID，值为队伍对象指针
};

/**
 * @def sArenaTeamMgr
 * @brief 竞技场队伍管理器全局访问宏
 *
 * 提供便捷的方式访问竞技场队伍管理器的单例实例。
 * 这是访问竞技场队伍管理器的推荐方式。
 *
 * @par 使用示例：
 * @code
 * // 获取竞技场队伍
 * ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(12345);
 *
 * // 遍历所有竞技场队伍
 * for (auto const& pair : sArenaTeamMgr->GetArenaTeams())
 * {
 *     ArenaTeam* team = pair.second;
 *     // 处理竞技场队伍
 * }
 * @endcode
 */
#define sArenaTeamMgr ArenaTeamMgr::instance()

#endif
