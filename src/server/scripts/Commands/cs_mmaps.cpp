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
 * @file cs_mmaps.cpp
 * @brief 移动地图(MMap)命令模块
 *
 * 本模块实现了与导航网格(Mesh Map)相关的 GM 命令,主要用于:
 * - 路径查找和可视化
 * - 导航网格瓦片位置查询
 * - 加载的瓦片列表显示
 * - 导航网格统计信息
 * - 区域路径测试
 *
 * MMap 是 TrinityCore 的寻路系统,基于 Detour/Recast 库实现,
 * 用于生物和玩家的智能寻路和移动。
 */

#include "ScriptMgr.h"
#include "CellImpl.h"
#include "Chat.h"
#include "DisableMgr.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "MMapFactory.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PointMovementGenerator.h"
#include "RBAC.h"

#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

/**
 * @class mmaps_commandscript
 * @brief 移动地图命令脚本类
 *
 * 继承自 CommandScript,负责注册和处理所有 MMap 相关的 GM 命令。
 * 提供导航网格调试和测试功能。
 */
class mmaps_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化移动地图命令脚本,设置脚本名称为 "mmaps_commandscript"
     */
    mmaps_commandscript() : CommandScript("mmaps_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回所有 MMap 命令的注册表
     *
     * 注册所有 mmap 相关的子命令,包括:
     * - mmap loadedtiles: 显示当前地图已加载的导航网格瓦片
     * - mmap loc: 显示当前位置的导航网格瓦片坐标
     * - mmap path: 计算并可视化路径
     * - mmap stats: 显示导航网格统计信息
     * - mmap testarea: 测试区域内所有生物的路径生成
     */
    std::vector<ChatCommand> GetCommands() const override
    {
        static std::vector<ChatCommand> mmapCommandTable =
        {
            { "loadedtiles", rbac::RBAC_PERM_COMMAND_MMAP_LOADEDTILES, false, &HandleMmapLoadedTilesCommand, "" },
            { "loc",         rbac::RBAC_PERM_COMMAND_MMAP_LOC,         false, &HandleMmapLocCommand,         "" },
            { "path",        rbac::RBAC_PERM_COMMAND_MMAP_PATH,        false, &HandleMmapPathCommand,        "" },
            { "stats",       rbac::RBAC_PERM_COMMAND_MMAP_STATS,       false, &HandleMmapStatsCommand,       "" },
            { "testarea",    rbac::RBAC_PERM_COMMAND_MMAP_TESTAREA,    false, &HandleMmapTestArea,           "" },
        };

        static std::vector<ChatCommand> commandTable =
        {
            { "mmap", rbac::RBAC_PERM_COMMAND_MMAP, true, nullptr, "", mmapCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 处理路径计算和可视化命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 可选参数,用于指定路径类型:
     *        - "true": 使用直线路径
     *        - "line"/"ray"/"raycast": 使用射线投射路径
     *        - 无参数: 使用平滑路径(默认)
     * @return true 表示命令执行成功
     *
     * 调用时机: 当 GM 使用 .mmap path 命令时
     * 性能注意事项:
     * - 需要计算从选中单位到玩家的路径
     * - 路径可视化会生成临时生物,需要定期清理
     *
     * 输出信息包括:
     * - 路径生成结果(成功/失败)
     * - 路径点数量和类型
     * - 起始位置、目标位置和实际终点位置
     * - 路径点可视化(GM模式下生成可见的路点生物)
     */
    static bool HandleMmapPathCommand(ChatHandler* handler, char const* args)
    {
        if (!MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(handler->GetSession()->GetPlayer()->GetMapId()))
        {
            handler->PSendSysMessage("NavMesh not loaded for current map.");
            return true;
        }

        handler->PSendSysMessage("mmap path:");

        // units
        Player* player = handler->GetSession()->GetPlayer();
        Unit* target = handler->getSelectedUnit();
        if (!player || !target)
        {
            handler->PSendSysMessage("Invalid target/source selection.");
            return true;
        }

        char* para = strtok((char*)args, " ");

        bool useStraightPath = false;
        if (para && strcmp(para, "true") == 0)
            useStraightPath = true;

        bool useRaycast = false;
        if (para && (strcmp(para, "line") == 0 || strcmp(para, "ray") == 0 || strcmp(para, "raycast") == 0))
            useRaycast = true;

        // unit locations
        float x, y, z;
        player->GetPosition(x, y, z);

        // path
        PathGenerator path(target);
        path.SetUseStraightPath(useStraightPath);
        path.SetUseRaycast(useRaycast);
        bool result = path.CalculatePath(x, y, z, false);

        Movement::PointsArray const& pointPath = path.GetPath();
        handler->PSendSysMessage("%s's path to %s:", target->GetName().c_str(), player->GetName().c_str());
        handler->PSendSysMessage("Building: %s", useStraightPath ? "StraightPath" : useRaycast ? "Raycast" : "SmoothPath");
        handler->PSendSysMessage("Result: %s - Length: %zu - Type: %u", (result ? "true" : "false"), pointPath.size(), path.GetPathType());

        G3D::Vector3 const& start = path.GetStartPosition();
        G3D::Vector3 const& end = path.GetEndPosition();
        G3D::Vector3 const& actualEnd = path.GetActualEndPosition();

        handler->PSendSysMessage("StartPosition     (%.3f, %.3f, %.3f)", start.x, start.y, start.z);
        handler->PSendSysMessage("EndPosition       (%.3f, %.3f, %.3f)", end.x, end.y, end.z);
        handler->PSendSysMessage("ActualEndPosition (%.3f, %.3f, %.3f)", actualEnd.x, actualEnd.y, actualEnd.z);

        if (!player->IsGameMaster())
            handler->PSendSysMessage("Enable GM mode to see the path points.");

        for (uint32 i = 0; i < pointPath.size(); ++i)
            player->SummonCreature(VISUAL_WAYPOINT, pointPath[i].x, pointPath[i].y, pointPath[i].z, 0, TEMPSUMMON_TIMED_DESPAWN, 9s);

        return true;
    }

    /**
     * @brief 处理导航网格瓦片位置查询命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 未使用的参数
     * @return true 表示命令执行成功
     *
     * 调用时机: 当 GM 使用 .mmap loc 命令时
     * 性能注意事项:
     * - 查询导航网格瓦片信息
     * - 执行最近多边形查找操作
     *
     * 输出信息包括:
     * - 瓦片文件名格式(MapId_Gx_Gy.mmtile)
     * - 网格坐标位置
     * - 计算的瓦片坐标
     * - Detour 导航网格中的瓦片坐标
     *
     * 用途: 用于调试导航网格加载和瓦片定位问题
     */
    static bool HandleMmapLocCommand(ChatHandler* handler, char const* /*args*/)
    {
        handler->PSendSysMessage("mmap tileloc:");

        // grid tile location
        Player* player = handler->GetSession()->GetPlayer();

        int32 gx = 32 - player->GetPositionX() / SIZE_OF_GRIDS;
        int32 gy = 32 - player->GetPositionY() / SIZE_OF_GRIDS;

        handler->PSendSysMessage("%03u%02i%02i.mmtile", player->GetMapId(), gx, gy);
        handler->PSendSysMessage("tileloc [%i, %i]", gy, gx);

        // calculate navmesh tile location
        dtNavMesh const* navmesh = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(handler->GetSession()->GetPlayer()->GetMapId());
        dtNavMeshQuery const* navmeshquery = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMeshQuery(handler->GetSession()->GetPlayer()->GetMapId(), player->GetInstanceId());
        if (!navmesh || !navmeshquery)
        {
            handler->PSendSysMessage("NavMesh not loaded for current map.");
            return true;
        }

        float const* min = navmesh->getParams()->orig;
        float x, y, z;
        player->GetPosition(x, y, z);
        float location[VERTEX_SIZE] = {y, z, x};
        float extents[VERTEX_SIZE] = {3.0f, 5.0f, 3.0f};

        int32 tilex = int32((y - min[0]) / SIZE_OF_GRIDS);
        int32 tiley = int32((x - min[2]) / SIZE_OF_GRIDS);

        handler->PSendSysMessage("Calc   [%02i, %02i]", tilex, tiley);

        // navmesh poly -> navmesh tile location
        dtQueryFilter filter = dtQueryFilter();
        dtPolyRef polyRef = INVALID_POLYREF;
        if (dtStatusFailed(navmeshquery->findNearestPoly(location, extents, &filter, &polyRef, nullptr)))
        {
            handler->PSendSysMessage("Dt     [??,??] (invalid poly, probably no tile loaded)");
            return true;
        }

        if (polyRef == INVALID_POLYREF)
            handler->PSendSysMessage("Dt     [??, ??] (invalid poly, probably no tile loaded)");
        else
        {
            dtMeshTile const* tile;
            dtPoly const* poly;
            if (dtStatusSucceed(navmesh->getTileAndPolyByRef(polyRef, &tile, &poly)))
            {
                if (tile)
                {
                    handler->PSendSysMessage("Dt     [%02i,%02i]", tile->header->x, tile->header->y);
                    return true;
                }
            }

            handler->PSendSysMessage("Dt     [??,??] (no tile loaded)");
        }

        return true;
    }

    /**
     * @brief 处理已加载瓦片列表显示命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 未使用的参数
     * @return true 表示命令执行成功
     *
     * 调用时机: 当 GM 使用 .mmap loadedtiles 命令时
     * 性能注意事项:
     * - 遍历导航网格中的所有瓦片
     * - 仅查询内存中的数据,不涉及磁盘IO
     *
     * 输出格式: 列出当前地图所有已加载瓦片的坐标 [x, y]
     *
     * 用途: 检查特定区域的导航网格是否已加载
     */
    static bool HandleMmapLoadedTilesCommand(ChatHandler* handler, char const* /*args*/)
    {
        uint32 mapid = handler->GetSession()->GetPlayer()->GetMapId();
        dtNavMesh const* navmesh = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMesh(mapid);
        dtNavMeshQuery const* navmeshquery = MMAP::MMapFactory::createOrGetMMapManager()->GetNavMeshQuery(mapid, handler->GetSession()->GetPlayer()->GetInstanceId());
        if (!navmesh || !navmeshquery)
        {
            handler->PSendSysMessage("NavMesh not loaded for current map.");
            return true;
        }

        handler->PSendSysMessage("mmap loadedtiles:");

        for (int32 i = 0; i < navmesh->getMaxTiles(); ++i)
        {
            dtMeshTile const* tile = navmesh->getTile(i);
            if (!tile || !tile->header)
                continue;

            handler->PSendSysMessage("[%02i, %02i]", tile->header->x, tile->header->y);
        }

        return true;
    }

    /**
     * @brief 处理导航网格统计信息显示命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 未使用的参数
     * @return true 表示命令执行成功
     *
     * 调用时机: 当 GM 使用 .mmap stats 命令时
     * 性能注意事项:
     * - 遍历所有瓦片统计数据
     * - 仅统计内存中的数据
     *
     * 输出信息包括:
     * - 全局寻路启用状态
     * - 已加载地图数量和瓦片总数
     * - 当前地图瓦片统计:
     *   - 瓦片数量
     *   - BVTree 节点数量
     *   - 多边形和顶点数量
     *   - 三角形和顶点数量
     *   - 数据大小(MB)
     *
     * 用途: 监控导航网格内存使用情况和加载状态
     */
    static bool HandleMmapStatsCommand(ChatHandler* handler, char const* /*args*/)
    {
        uint32 mapId = handler->GetSession()->GetPlayer()->GetMapId();
        handler->PSendSysMessage("mmap stats:");
        handler->PSendSysMessage("  global mmap pathfinding is %sabled", DisableMgr::IsPathfindingEnabled(mapId) ? "en" : "dis");

        MMAP::MMapManager* manager = MMAP::MMapFactory::createOrGetMMapManager();
        handler->PSendSysMessage(" %u maps loaded with %u tiles overall", manager->getLoadedMapsCount(), manager->getLoadedTilesCount());

        dtNavMesh const* navmesh = manager->GetNavMesh(handler->GetSession()->GetPlayer()->GetMapId());
        if (!navmesh)
        {
            handler->PSendSysMessage("NavMesh not loaded for current map.");
            return true;
        }

        uint32 tileCount = 0;
        uint32 nodeCount = 0;
        uint32 polyCount = 0;
        uint32 vertCount = 0;
        uint32 triCount = 0;
        uint32 triVertCount = 0;
        uint32 dataSize = 0;
        for (int32 i = 0; i < navmesh->getMaxTiles(); ++i)
        {
            dtMeshTile const* tile = navmesh->getTile(i);
            if (!tile || !tile->header)
                continue;

            tileCount++;
            nodeCount += tile->header->bvNodeCount;
            polyCount += tile->header->polyCount;
            vertCount += tile->header->vertCount;
            triCount += tile->header->detailTriCount;
            triVertCount += tile->header->detailVertCount;
            dataSize += tile->dataSize;
        }

        handler->PSendSysMessage("Navmesh stats:");
        handler->PSendSysMessage(" %u tiles loaded", tileCount);
        handler->PSendSysMessage(" %u BVTree nodes", nodeCount);
        handler->PSendSysMessage(" %u polygons (%u vertices)", polyCount, vertCount);
        handler->PSendSysMessage(" %u triangles (%u vertices)", triCount, triVertCount);
        handler->PSendSysMessage(" %.2f MB of data (not including pointers)", ((float)dataSize / sizeof(unsigned char)) / 1048576);

        return true;
    }

    /**
     * @brief 处理区域路径测试命令
     * @param handler 聊天处理器,用于发送消息和获取会话信息
     * @param args 未使用的参数
     * @return true 表示命令执行成功
     *
     * 调用时机: 当 GM 使用 .mmap testarea 命令时
     * 性能注意事项:
     * - 在半径40码范围内搜索所有生物
     * - 为每个生物计算到玩家位置的路径
     * - 性能开销取决于区域内生物数量
     *
     * 输出信息包括:
     * - 找到的生物数量
     * - 生成的路径数量
     * - 路径生成总耗时(毫秒)
     *
     * 用途: 批量测试区域内生物的寻路性能和准确性
     */
    static bool HandleMmapTestArea(ChatHandler* handler, char const* /*args*/)
    {
        float radius = 40.0f;
        WorldObject* object = handler->GetSession()->GetPlayer();

        // Get Creatures
        std::list<Creature*> creatureList;
        Trinity::AnyUnitInObjectRangeCheck go_check(object, radius);
        Trinity::CreatureListSearcher<Trinity::AnyUnitInObjectRangeCheck> go_search(object, creatureList, go_check);
        Cell::VisitGridObjects(object, go_search, radius);

        if (!creatureList.empty())
        {
            handler->PSendSysMessage("Found %zu Creatures.", creatureList.size());

            uint32 paths = 0;
            uint32 uStartTime = getMSTime();

            float gx, gy, gz;
            object->GetPosition(gx, gy, gz);
            for (std::list<Creature*>::iterator itr = creatureList.begin(); itr != creatureList.end(); ++itr)
            {
                PathGenerator path(*itr);
                path.CalculatePath(gx, gy, gz);
                ++paths;
            }

            uint32 uPathLoadTime = getMSTimeDiff(uStartTime, getMSTime());
            handler->PSendSysMessage("Generated %i paths in %i ms", paths, uPathLoadTime);
        }
        else
            handler->PSendSysMessage("No creatures in %f yard range.", radius);

        return true;
    }
};

/**
 * @brief 注册移动地图命令脚本
 *
 * 此函数由脚本系统在启动时调用,用于创建并注册 mmaps_commandscript 实例。
 * 使移动地图命令在游戏中可用。
 */
void AddSC_mmaps_commandscript()
{
    new mmaps_commandscript();
}
