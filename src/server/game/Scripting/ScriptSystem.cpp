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
 * @file ScriptSystem.cpp
 * @brief 脚本系统管理器实现文件
 *
 * 实现脚本路点和样条链数据的加载与查询功能。
 * 这些数据用于控制NPC生物的巡逻移动行为。
 */

#include "ScriptSystem.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"
#include "SplineChain.h"

// ==================== 构造函数和析构函数 ====================

SystemMgr::SystemMgr() = default;
SystemMgr::~SystemMgr() = default;

/**
 * @brief 获取单例实例
 *
 * 使用Meyers单例模式（C++11 magic statics）
 * 线程安全且无需手动管理内存
 */
SystemMgr* SystemMgr::instance()
{
    static SystemMgr instance;
    return &instance;
}

/**
 * @brief 从数据库加载脚本路点数据
 *
 * 从 script_waypoint 表中加载所有生物的巡逻路点数据。
 * 每个路点定义了生物移动路径上的一个节点，包括位置坐标和停留时间。
 *
 * 数据表结构：
 * - entry: 生物模板ID
 * - pointid: 路点ID（序号）
 * - location_x/y/z: 路点坐标
 * - waittime: 在该路点的等待时间（毫秒）
 *
 * @note 此函数会清空现有的路点数据，应在服务器启动时调用
 */
void SystemMgr::LoadScriptWaypoints()
{
    uint32 oldMSTime = getMSTime();

    // 清空现有路点列表，准备重新加载
    _waypointStore.clear();

    uint64 entryCount = 0;

    // 首先统计有多少个不同的生物entry，用于日志输出
    QueryResult result = WorldDatabase.Query("SELECT COUNT(entry) FROM script_waypoint GROUP BY entry");
    if (result)
        entryCount = result->GetRowCount();

    TC_LOG_INFO("server.loading", "Loading Script Waypoints for {} creature(s)...", entryCount);

    // 查询所有路点数据，按pointid排序确保路点顺序正确
    //                                     0       1         2           3           4           5
    result = WorldDatabase.Query("SELECT entry, pointid, location_x, location_y, location_z, waittime FROM script_waypoint ORDER BY pointid");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 Script Waypoints. DB table `script_waypoint` is empty.");
        return;
    }
    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();
        uint32 entry = fields[0].GetUInt32();       // 生物模板ID
        uint32 id = fields[1].GetUInt32();          // 路点ID
        float x = fields[2].GetFloat();             // X坐标
        float y = fields[3].GetFloat();             // Y坐标
        float z = fields[4].GetFloat();             // Z坐标
        uint32 waitTime = fields[5].GetUInt32();    // 等待时间（毫秒）

        // 验证生物模板是否存在
        CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(entry);
        if (!info)
        {
            TC_LOG_ERROR("sql.sql", "SystemMgr: DB table script_waypoint has waypoint for non-existant creature entry {}", entry);
            continue;
        }

        // 警告：如果生物没有定义脚本名称，路点数据将无效
        if (!info->ScriptID)
            TC_LOG_ERROR("sql.sql", "SystemMgr: DB table script_waypoint has waypoint for creature entry {}, but creature does not have ScriptName defined and then useless.", entry);

        // 将路点添加到对应生物的路径中
        WaypointPath& path = _waypointStore[entry];
        path.id = entry;
        path.nodes.emplace_back(id, x, y, z, std::nullopt, waitTime);

        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} Script Waypoint nodes in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 从数据库加载脚本样条链数据
 *
 * 样条链（Spline Chain）用于创建平滑的生物移动轨迹，
 * 比传统路点移动更加流畅自然。
 *
 * 数据来源：
 * - script_spline_chain_meta: 样条链元数据（持续时间、速度等）
 * - script_spline_chain_waypoints: 样条链路点坐标
 *
 * 加载流程：
 * 1. 清空现有样条链数据
 * 2. 加载元数据，构建样条链框架
 * 3. 加载路点数据，填充样条链内容
 *
 * @note 此函数会清空现有数据，应在服务器启动时调用
 */
void SystemMgr::LoadScriptSplineChains()
{
    uint32 oldMSTime = getMSTime();

    // 清空现有样条链数据
    m_mSplineChainsMap.clear();

    // 查询样条链元数据，按entry/chainId/splineId升序排列确保正确构建
    //                                                   0      1        2         3                 4            5
    QueryResult resultMeta = WorldDatabase.Query("SELECT entry, chainId, splineId, expectedDuration, msUntilNext, velocity FROM script_spline_chain_meta ORDER BY entry asc, chainId asc, splineId asc");

    // 查询样条链路点数据
    //                                                 0      1        2         3     4  5  6
    QueryResult resultWP = WorldDatabase.Query("SELECT entry, chainId, splineId, wpId, x, y, z FROM script_spline_chain_waypoints ORDER BY entry asc, chainId asc, splineId asc, wpId asc");

    if (!resultMeta || !resultWP)
    {
        TC_LOG_INFO("server.loading", ">> Loaded spline chain data for 0 chains, consisting of 0 splines with 0 waypoints. DB tables `script_spline_chain_meta` and `script_spline_chain_waypoints` are empty.");
    }
    else
    {
        uint32 chainCount = 0, splineCount = 0, wpCount = 0;

        // 第一阶段：加载元数据，构建样条链框架
        do
        {
            Field* fieldsMeta = resultMeta->Fetch();
            uint32 entry = fieldsMeta[0].GetUInt32();               // 生物模板ID
            uint16 chainId = fieldsMeta[1].GetUInt16();             // 链ID
            uint8 splineId = fieldsMeta[2].GetUInt8();              // 样条ID（链中的索引）
            std::vector<SplineChainLink>& chain = m_mSplineChainsMap[{entry, chainId}];

            // 检查样条ID是否连续，防止数据缺失
            if (splineId != chain.size())
            {
                TC_LOG_WARN("server.loading", "Creature #{}: Chain {} has orphaned spline {}, skipped.", entry, chainId, splineId);
                continue;
            }

            uint32 expectedDuration = fieldsMeta[3].GetUInt32();    // 预期持续时间（毫秒）
            uint32 msUntilNext = fieldsMeta[4].GetUInt32();         // 到下一样条的等待时间
            float velocity = fieldsMeta[5].GetFloat();              // 移动速度

            // 添加样条链接到链中
            chain.emplace_back(expectedDuration, msUntilNext, velocity);

            // 统计链数量（每个链的第一个样条时计数）
            if (splineId == 0)
                ++chainCount;
            ++splineCount;
        } while (resultMeta->NextRow());

        // 第二阶段：加载路点数据，填充样条链内容
        do
        {
            Field* fieldsWP = resultWP->Fetch();
            uint32 entry = fieldsWP[0].GetUInt32();                 // 生物模板ID
            uint16 chainId = fieldsWP[1].GetUInt16();               // 链ID
            uint8 splineId = fieldsWP[2].GetUInt8(), wpId = fieldsWP[3].GetUInt8();  // 样条ID和路点ID
            float posX = fieldsWP[4].GetFloat(), posY = fieldsWP[5].GetFloat(), posZ = fieldsWP[6].GetFloat();  // 坐标

            // 查找对应的样条链
            auto it = m_mSplineChainsMap.find({entry,chainId});
            if (it == m_mSplineChainsMap.end())
            {
                TC_LOG_WARN("server.loading", "Creature #{} has waypoint data for spline chain {}. No such chain exists - entry skipped.", entry, chainId);
                continue;
            }

            std::vector<SplineChainLink>& chain = it->second;

            // 验证样条索引是否有效
            if (splineId >= chain.size())
            {
                TC_LOG_WARN("server.loading", "Creature #{} has waypoint data for spline ({},{}). The specified chain does not have a spline with this index - entry skipped.", entry, chainId, splineId);
                continue;
            }

            SplineChainLink& spline = chain[splineId];

            // 检查路点ID是否连续，确保数据完整性
            if (wpId != spline.Points.size())
            {
                TC_LOG_WARN("server.loading", "Creature #{} has orphaned waypoint data in spline ({},{}) at index {}. Skipped.", entry, chainId, splineId, wpId);
                continue;
            }

            // 添加路点到样条
            spline.Points.emplace_back(posX, posY, posZ);
            ++wpCount;
        } while (resultWP->NextRow());

        TC_LOG_INFO("server.loading", ">> Loaded spline chain data for {} chains, consisting of {} splines with {} waypoints in {} ms", chainCount, splineCount, wpCount, GetMSTimeDiffToNow(oldMSTime));
    }
}

/**
 * @brief 获取指定生物的路点路径
 * @param creatureEntry 生物模板ID
 * @return 路点路径的常量指针，如果不存在则返回nullptr
 *
 * 通过生物模板ID查找其对应的巡逻路径数据。
 * 返回的路径包含所有路点节点及其顺序。
 *
 * @note 性能：O(1)时间复杂度的哈希查找
 */
WaypointPath const* SystemMgr::GetPath(uint32 creatureEntry) const
{
    auto itr = _waypointStore.find(creatureEntry);
    if (itr == _waypointStore.end())
        return nullptr;

    return &itr->second;
}

/**
 * @brief 获取指定生物的样条链数据
 * @param entry 生物模板ID
 * @param chainId 样条链ID
 * @return 样条链数据向量的常量指针，如果不存在则返回nullptr
 *
 * 通过生物模板ID和链ID查找对应的样条链数据。
 * 样条链包含一系列平滑移动的轨迹点。
 *
 * @note 性能：O(1)时间复杂度的哈希查找
 */
std::vector<SplineChainLink> const* SystemMgr::GetSplineChain(uint32 entry, uint16 chainId) const
{
    auto it = m_mSplineChainsMap.find({ entry, chainId });
    if (it == m_mSplineChainsMap.end())
        return nullptr;
    return &it->second;
}

/**
 * @brief 获取指定生物对象的样条链数据（便捷重载版本）
 * @param who 生物对象指针
 * @param id 样条链ID
 * @return 样条链数据向量的常量指针，如果不存在则返回nullptr
 *
 * 这是GetSplineChain(uint32, uint16)的便捷封装版本，
 * 直接接受生物对象指针，内部提取其entry进行查询。
 */
std::vector<SplineChainLink> const* SystemMgr::GetSplineChain(Creature const* who, uint16 id) const
{
    return GetSplineChain(who->GetEntry(), id);
}
