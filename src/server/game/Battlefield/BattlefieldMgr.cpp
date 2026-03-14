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
 * @file BattlefieldMgr.cpp
 * @brief 战场管理器实现文件
 *
 * 本文件实现了战场管理器（BattlefieldMgr）的核心功能，包括：
 * - 战场的创建和初始化
 * - 玩家区域事件的分发处理
 * - 战场实例的查询和管理
 * - 所有战场的定时更新
 */

#include "BattlefieldMgr.h"
#include "DatabaseEnv.h"
#include "ObjectMgr.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"

/**
 * @brief BattlefieldMgr构造函数
 *
 * 初始化更新定时器为0
 */
BattlefieldMgr::BattlefieldMgr()
{
    _updateTimer = 0;
}

/**
 * @brief BattlefieldMgr析构函数
 *
 * 清理所有战场实例，释放内存
 */
BattlefieldMgr::~BattlefieldMgr()
{
    // 删除所有战场实例
    for (BattlefieldSet::iterator itr = _battlefieldSet.begin(); itr != _battlefieldSet.end(); ++itr)
        delete *itr;

    // 清空映射表
    _battlefieldMap.clear();
}

/**
 * @brief 获取战场管理器单例实例
 * @return 战场管理器单例指针
 *
 * 使用静态局部变量实现线程安全的单例模式
 */
BattlefieldMgr* BattlefieldMgr::instance()
{
    static BattlefieldMgr instance;
    return &instance;
}

/**
 * @brief 初始化所有战场
 *
 * 从数据库加载战场模板并创建所有战场实例
 *
 * 加载流程：
 * 1. 记录开始时间
 * 2. 查询battlefield_template表
 * 3. 对每个战场模板：
 *    - 验证TypeId有效性
 *    - 根据脚本名创建战场实例
 *    - 调用SetupBattlefield初始化
 *    - 成功则加入管理列表
 * 4. 输出加载统计信息
 *
 * @note 在服务器启动时调用一次
 */
void BattlefieldMgr::InitBattlefield()
{
    uint32 oldMSTime = getMSTime();

    uint32 count = 0;

    // 从数据库查询战场模板
    if (QueryResult result = WorldDatabase.Query("SELECT TypeId, ScriptName FROM battlefield_template"))
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 typeId = fields[0].GetUInt8();

            // 验证TypeId是否有效
            if (typeId >= BATTLEFIELD_MAX)
            {
                TC_LOG_ERROR("sql.sql", "BattlefieldMgr::InitBattlefield: Invalid TypeId value {} in battlefield_template, skipped.", typeId);
                continue;
            }

            // 获取脚本ID
            uint32 scriptId = sObjectMgr->GetScriptId(fields[1].GetString());

            // 通过脚本工厂创建战场实例
            Battlefield* bf = sScriptMgr->CreateBattlefield(scriptId);
            if (!bf)
                continue;

            // 初始化战场
            if (!bf->SetupBattlefield())
            {
                TC_LOG_INFO("bg.battlefield", "Setting up battlefield with TypeId {} failed.", typeId);
                delete bf;
            }
            else
            {
                // 初始化成功，加入管理列表
                _battlefieldSet.push_back(bf);
                TC_LOG_INFO("bg.battlefield", "Setting up battlefield with TypeId {} succeeded.", typeId);
            }

            ++count;
        } while (result->NextRow());
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} battlefields in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 添加区域到战场的映射
 * @param zoneId 区域ID
 * @param bf 战场指针
 *
 * 战场初始化时调用，建立区域与战场的关联
 * 一个战场可以关联多个区域
 */
void BattlefieldMgr::AddZone(uint32 zoneId, Battlefield* bf)
{
    _battlefieldMap[zoneId] = bf;
}

/**
 * @brief 处理玩家进入战场区域
 * @param player 进入区域的玩家指针
 * @param zoneId 区域ID
 *
 * 当玩家进入一个新区域时由Player::UpdateZone调用
 *
 * 处理流程：
 * 1. 检查该区域是否关联了战场
 * 2. 检查战场是否启用
 * 3. 检查玩家是否已在战场中
 * 4. 调用战场的HandlePlayerEnterZone方法
 */
void BattlefieldMgr::HandlePlayerEnterZone(Player* player, uint32 zoneId)
{
    // 查找该区域关联的战场
    BattlefieldMap::iterator itr = _battlefieldMap.find(zoneId);
    if (itr == _battlefieldMap.end())
        return;

    Battlefield* bf = itr->second;

    // 检查战场是否启用，以及玩家是否已在战场中
    if (!bf->IsEnabled() || bf->HasPlayer(player))
        return;

    // 调用战场的进入处理方法
    bf->HandlePlayerEnterZone(player, zoneId);
    TC_LOG_DEBUG("bg.battlefield", "Player {} entered battlefield id {}", player->GetGUID().ToString(), bf->GetTypeId());
}

/**
 * @brief 处理玩家离开战场区域
 * @param player 离开区域的玩家指针
 * @param zoneId 区域ID
 *
 * 当玩家离开一个区域时由Player::UpdateZone调用
 *
 * @note 传送时会触发两次：
 *       1. RemoveFromWorld时触发一次
 *       2. UpdateZone时触发一次
 *       所以需要检查玩家是否真的在战场中
 */
void BattlefieldMgr::HandlePlayerLeaveZone(Player* player, uint32 zoneId)
{
    // 查找该区域关联的战场
    BattlefieldMap::iterator itr = _battlefieldMap.find(zoneId);
    if (itr == _battlefieldMap.end())
        return;

    // 检查玩家是否真的在战场中（避免重复处理）
    if (!itr->second->HasPlayer(player))
        return;

    // 调用战场的离开处理方法
    itr->second->HandlePlayerLeaveZone(player, zoneId);
    TC_LOG_DEBUG("bg.battlefield", "Player {} left battlefield id {}", player->GetGUID().ToString(), itr->second->GetTypeId());
}

/**
 * @brief 根据区域ID获取战场实例
 * @param zoneId 区域ID
 * @return 战场指针，未找到或已禁用返回nullptr
 */
Battlefield* BattlefieldMgr::GetBattlefieldToZoneId(uint32 zoneId)
{
    BattlefieldMap::iterator itr = _battlefieldMap.find(zoneId);
    if (itr == _battlefieldMap.end())
    {
        // 该区域没有关联战场
        return nullptr;
    }

    // 检查战场是否启用
    if (!itr->second->IsEnabled())
        return nullptr;

    return itr->second;
}

/**
 * @brief 根据战斗ID获取战场实例
 * @param battleId 战斗ID
 * @return 战场指针，未找到返回nullptr
 *
 * 遍历所有战场查找匹配的BattleId
 */
Battlefield* BattlefieldMgr::GetBattlefieldByBattleId(uint32 battleId)
{
    for (BattlefieldSet::iterator itr = _battlefieldSet.begin(); itr != _battlefieldSet.end(); ++itr)
    {
        if ((*itr)->GetBattleId() == battleId)
            return *itr;
    }
    return nullptr;
}

/**
 * @brief 根据区域ID获取区域脚本
 * @param zoneId 区域ID
 * @return 区域脚本指针，未找到返回nullptr
 *
 * 返回该区域关联的战场实例（战场继承自ZoneScript）
 */
ZoneScript* BattlefieldMgr::GetZoneScript(uint32 zoneId)
{
    BattlefieldMap::iterator itr = _battlefieldMap.find(zoneId);
    if (itr != _battlefieldMap.end())
        return itr->second;

    return nullptr;
}

/**
 * @brief 更新所有战场
 * @param diff 距上次更新的时间间隔（毫秒）
 *
 * 由World::Update每帧调用
 *
 * 更新策略：
 * - 累积时间到_updateTimer
 * - 当累积时间超过BATTLEFIELD_OBJECTIVE_UPDATE_INTERVAL（1000ms）时
 * - 遍历所有启用的战场并调用其Update方法
 * - 重置定时器
 *
 * 这样可以确保战场每秒更新一次，而不是每帧更新
 * 提高服务器性能
 */
void BattlefieldMgr::Update(uint32 diff)
{
    _updateTimer += diff;

    // 每秒更新一次所有战场
    if (_updateTimer > BATTLEFIELD_OBJECTIVE_UPDATE_INTERVAL)
    {
        for (BattlefieldSet::iterator itr = _battlefieldSet.begin(); itr != _battlefieldSet.end(); ++itr)
            if ((*itr)->IsEnabled())
                (*itr)->Update(_updateTimer);

        _updateTimer = 0;
    }
}
