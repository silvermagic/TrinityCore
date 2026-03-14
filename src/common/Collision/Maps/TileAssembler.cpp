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
 * @file TileAssembler.cpp
 * @brief 地图瓦片组装器实现文件
 *
 * 本模块实现了从原始提取数据生成优化后的VMap文件的功能。主要功能包括：
 * - 将提取的原始地图数据转换为二进制BIH树格式
 * - 处理WMO（世界模型对象）和M2（游戏模型）的几何数据
 * - 生成地图树文件（.vmtree）和瓦片文件（.vmtile）
 * - 计算模型边界框和变换后的几何数据
 * - 处理游戏对象模型列表
 *
 * 核心类：
 * - TileAssembler: 瓦片组装器，协调整个转换流程
 * - ModelPosition: 模型位置变换信息
 * - GroupModel_Raw: 原始组模型数据
 * - WorldModel_Raw: 原始世界模型数据
 *
 * 文件格式：
 * - .vmtree: 地图树文件，包含BIH树和全局模型信息
 * - .vmtile: 瓦片文件，包含特定区域的模型数据
 * - .vmo: 优化的模型文件
 */

#include "TileAssembler.h"
#include "MapTree.h"
#include "BoundingIntervalHierarchy.h"
#include "VMapDefinitions.h"

#include <set>
#include <iomanip>
#include <sstream>
#include <boost/filesystem.hpp>

using G3D::Vector3;
using G3D::AABox;
using G3D::inf;
using std::pair;

/**
 * @brief ModelSpawn指针的边界特征特化模板
 *
 * 为BIH树提供获取ModelSpawn边界框的方法。
 * 这是BoundsTrait模板的特化版本，用于从ModelSpawn对象获取轴对齐包围盒。
 */
template<> struct BoundsTrait<VMAP::ModelSpawn*>
{
    /**
     * @brief 获取ModelSpawn的边界框
     * @param obj ModelSpawn常量指针
     * @param out [输出] 输出的轴对齐包围盒
     */
    static void getBounds(VMAP::ModelSpawn const* const& obj, G3D::AABox& out) { out = obj->getBounds(); }
};

namespace VMAP
{
    /**
     * @brief 读取并验证文件块标识
     * @param rf 文件指针
     * @param dest 存储读取数据的缓冲区
     * @param compare 期望的标识字符串
     * @param len 要读取的长度
     * @return true 如果读取成功且标识匹配
     * @return false 如果读取失败或标识不匹配
     *
     * 该函数从文件中读取指定长度的数据，并与期望的标识进行比较。
     * 用于验证文件格式和数据块的完整性。
     */
    bool readChunk(FILE* rf, char *dest, const char *compare, uint32 len)
    {
        if (fread(dest, sizeof(char), len, rf) != len) return false;
        return memcmp(dest, compare, len) == 0;
    }

    /**
     * @brief 变换顶点坐标
     * @param pIn 输入的顶点坐标
     * @return 变换后的顶点坐标
     *
     * 该函数对输入顶点应用缩放和旋转变换。
     * 变换顺序：先缩放，后旋转。
     */
    Vector3 ModelPosition::transform(Vector3 const& pIn) const
    {
        Vector3 out = pIn * iScale;      // 应用缩放变换
        out = iRotation * out;            // 应用旋转变换
        return(out);
    }

    //=================================================================

    /**
     * @brief TileAssembler构造函数
     * @param pSrcDirName 源目录路径（原始提取数据）
     * @param pDestDirName 目标目录路径（输出的VMap文件）
     *
     * 初始化瓦片组装器，创建目标目录。
     * 源目录包含从游戏客户端提取的原始数据，目标目录用于存储优化后的VMap文件。
     */
    TileAssembler::TileAssembler(const std::string& pSrcDirName, const std::string& pDestDirName)
        : iDestDir(pDestDirName), iSrcDir(pSrcDirName)
    {
        boost::filesystem::create_directory(iDestDir);
        //init();
    }

    /**
     * @brief TileAssembler析构函数
     *
     * 清理组装器资源。注意：iCoordModelMapping已被移除，保留注释作为历史参考。
     */
    TileAssembler::~TileAssembler()
    {
        //delete iCoordModelMapping;
    }

    /**
     * @brief 执行世界地图转换的主要入口函数
     * @return true 如果转换成功
     * @return false 如果转换失败
     *
     * 该函数是整个地图转换流程的主控制函数，执行以下步骤：
     * 1. 读取地图生成点数据（dir_bin文件）
     * 2. 为每个地图构建BIH树
     * 3. 写入地图树文件（.vmtree）
     * 4. 写入瓦片文件（.vmtile）
     * 5. 导出游戏对象模型
     * 6. 转换所有引用的模型文件
     */
    bool TileAssembler::convertWorld2()
    {
        // 步骤1：读取地图生成点数据
        bool success = readMapSpawns();
        if (!success)
            return false;

        // 步骤2：导出地图数据
        for (MapData::iterator map_iter = mapData.begin(); map_iter != mapData.end() && success; ++map_iter)
        {
            // 构建全局地图树
            std::vector<ModelSpawn*> mapSpawns;
            UniqueEntryMap::iterator entry;
            printf("Calculating model bounds for map %u...\n", map_iter->first);

            // 遍历地图中的所有唯一生成点
            for (entry = map_iter->second->UniqueEntries.begin(); entry != map_iter->second->UniqueEntries.end(); ++entry)
            {
                // M2模型在WDT/ADT放置数据中没有边界框设置，我认为它们在官方服务器上根本不用于视线检测
                if (entry->second.flags & MOD_M2)
                {
                    // 为M2模型计算变换后的边界框
                    if (!calculateTransformedBound(entry->second))
                        break;
                }
                else if (entry->second.flags & MOD_WORLDSPAWN)
                {
                    // WMO地图和地形地图使用不同的原点，需要适配
                    // 注意：这里需要移除提取器hack并取消注释下面这行：
                    //entry->second.iPos += Vector3(533.33333f*32, 533.33333f*32, 0.f);
                    entry->second.iBound = entry->second.iBound + Vector3(533.33333f*32, 533.33333f*32, 0.f);
                }
                mapSpawns.push_back(&(entry->second));
                spawnedModelFiles.insert(entry->second.name);
            }

            // 构建BIH树
            printf("Creating map tree for map %u...\n", map_iter->first);
            BIH pTree;

            try
            {
                // 使用模型生成点构建BIH树
                pTree.build(mapSpawns, BoundsTrait<ModelSpawn*>::getBounds);
            }
            catch (std::exception& e)
            {
                printf("Exception ""%s"" when calling pTree.build", e.what());
                return false;
            }

            // 创建模型ID到树节点索引的映射
            // 这可能应该移到StaticMapTree类中
            std::map<uint32, uint32> modelNodeIdx;
            for (uint32 i=0; i<mapSpawns.size(); ++i)
                modelNodeIdx.insert(pair<uint32, uint32>(mapSpawns[i]->ID, i));

            // 写入地图树文件
            std::stringstream mapfilename;
            mapfilename << iDestDir << '/' << std::setfill('0') << std::setw(3) << map_iter->first << ".vmtree";
            FILE* mapfile = fopen(mapfilename.str().c_str(), "wb");
            if (!mapfile)
            {
                success = false;
                printf("Cannot open %s\n", mapfilename.str().c_str());
                break;
            }

            // 写入文件头：VMAP_MAGIC标识
            if (success && fwrite(VMAP_MAGIC, 1, 8, mapfile) != 8) success = false;

            // 确定是否为分块地图
            // tileID 65,65用于标识全局WMO（非分块地图如副本）
            uint32 globalTileID = StaticMapTree::packTileID(65, 65);
            pair<TileMap::iterator, TileMap::iterator> globalRange = map_iter->second->TileEntries.equal_range(globalTileID);
            char isTiled = globalRange.first == globalRange.second; // 只有没有地形（瓦片）的地图才有全局WMO
            if (success && fwrite(&isTiled, sizeof(char), 1, mapfile) != 1) success = false;

            // 写入节点数据：NODE块标识 + BIH树数据
            if (success && fwrite("NODE", 4, 1, mapfile) != 1) success = false;
            if (success) success = pTree.writeToFile(mapfile);

            // 写入全局地图生成点（WDT），如果有的话（大多数副本）
            if (success && fwrite("GOBJ", 4, 1, mapfile) != 1) success = false;

            for (TileMap::iterator glob = globalRange.first; glob != globalRange.second && success; ++glob)
                success = ModelSpawn::writeToFile(mapfile, map_iter->second->UniqueEntries[glob->second]);

            fclose(mapfile);

            // 写入地图瓦片文件，类似于ADT文件，但包含额外的BSP树节点信息
            TileMap &tileEntries = map_iter->second->TileEntries;
            TileMap::iterator tile;
            for (tile = tileEntries.begin(); tile != tileEntries.end(); ++tile)
            {
                ModelSpawn const& spawn = map_iter->second->UniqueEntries[tile->second];
                if (spawn.flags & MOD_WORLDSPAWN) // WDT生成点，当前保存为瓦片65/65...
                    continue;

                uint32 nSpawns = tileEntries.count(tile->first);
                std::stringstream tilefilename;
                tilefilename.fill('0');
                tilefilename << iDestDir << '/' << std::setw(3) << map_iter->first << '_';
                uint32 x, y;
                StaticMapTree::unpackTileID(tile->first, x, y);
                tilefilename << std::setw(2) << x << '_' << std::setw(2) << y << ".vmtile";

                if (FILE* tilefile = fopen(tilefilename.str().c_str(), "wb"))
                {
                    // 写入文件头：VMAP_MAGIC标识
                    if (success && fwrite(VMAP_MAGIC, 1, 8, tilefile) != 8) success = false;

                    // 写入瓦片生成点数量
                    if (success && fwrite(&nSpawns, sizeof(uint32), 1, tilefile) != 1) success = false;

                    // 写入瓦片生成点数据
                    for (uint32 s=0; s<nSpawns; ++s)
                    {
                        if (s)
                            ++tile;
                        ModelSpawn const& spawn2 = map_iter->second->UniqueEntries[tile->second];
                        success = success && ModelSpawn::writeToFile(tilefile, spawn2);

                        // 写入加载瓦片时需要更新的MapTree节点索引
                        std::map<uint32, uint32>::iterator nIdx = modelNodeIdx.find(spawn2.ID);
                        if (success && fwrite(&nIdx->second, sizeof(uint32), 1, tilefile) != 1) success = false;
                    }
                    fclose(tilefile);
                }
            }
            // break; // 测试用，只提取第一个地图；TODO：移除这行
        }

        // 步骤3：添加游戏对象模型，列在temp_gameobject_models文件中
        exportGameobjectModels();

        // 步骤4：导出对象模型
        std::cout << "\nConverting Model Files" << std::endl;
        for (std::string const& spawnedModelFile : spawnedModelFiles)
        {
            std::cout << "Converting " << spawnedModelFile << std::endl;
            if (!convertRawFile(spawnedModelFile))
            {
                std::cout << "error converting " << spawnedModelFile << std::endl;
                success = false;
                break;
            }
        }

        // 清理：释放地图数据
        for (std::pair<uint32 const, MapSpawns*>& map_iter : mapData)
            delete map_iter.second;

        return success;
    }

    /**
     * @brief 读取地图生成点数据
     * @return true 如果读取成功
     * @return false 如果读取失败
     *
     * 该函数从dir_bin文件中读取所有地图的模型生成点信息。
     * 文件格式：mapID + tileX + tileY + ModelSpawn数据
     * 每个生成点包含位置、旋转、缩放、边界框和模型名称等信息。
     */
    bool TileAssembler::readMapSpawns()
    {
        std::string fname = iSrcDir + "/dir_bin";
        FILE* dirf = fopen(fname.c_str(), "rb");
        if (!dirf)
        {
            printf("Could not read dir_bin file!\n");
            return false;
        }
        printf("Read coordinate mapping...\n");

        uint32 mapID, tileX, tileY, check;
        ModelSpawn spawn;

        // 循环读取所有生成点数据
        while (!feof(dirf))
        {
            // 读取：mapID, tileX, tileY, Flags, NameSet, UniqueId, Pos, Rot, Scale, Bound_lo, Bound_hi, name
            check = fread(&mapID, sizeof(uint32), 1, dirf);
            if (check == 0) // 到达文件末尾
                break;
            check = fread(&tileX, sizeof(uint32), 1, dirf);
            if (check == 0) // 到达文件末尾
                break;
            check = fread(&tileY, sizeof(uint32), 1, dirf);
            if (check == 0) // 到达文件末尾
                break;

            // 读取生成点详细信息
            if (!ModelSpawn::readFromFile(dirf, spawn))
                break;

            // 获取或创建该地图的数据结构
            MapSpawns *current;
            MapData::iterator map_iter = mapData.find(mapID);
            if (map_iter == mapData.end())
            {
                printf("spawning Map %u\n", mapID);
                mapData[mapID] = current = new MapSpawns();
            }
            else
                current = map_iter->second;

            // 将生成点添加到唯一条目映射和瓦片条目映射中
            current->UniqueEntries.emplace(spawn.ID, spawn);
            current->TileEntries.insert(pair<uint32, uint32>(StaticMapTree::packTileID(tileX, tileY), spawn.ID));
        }

        bool success = (ferror(dirf) == 0);
        fclose(dirf);
        return success;
    }

    /**
     * @brief 计算模型的变换后边界框
     * @param spawn 模型生成点信息
     * @return true 如果计算成功
     * @return false 如果计算失败
     *
     * 该函数为M2模型计算经过旋转和缩放变换后的边界框。
     * 由于M2模型在提取数据中没有预设的边界框，需要从原始几何数据计算。
     */
    bool TileAssembler::calculateTransformedBound(ModelSpawn &spawn)
    {
        // 构造模型文件路径
        std::string modelFilename(iSrcDir);
        modelFilename.push_back('/');
        modelFilename.append(spawn.name);

        // 设置模型位置变换参数
        ModelPosition modelPosition;
        modelPosition.iDir = spawn.iRot;
        modelPosition.iScale = spawn.iScale;
        modelPosition.init();

        // 读取原始模型数据
        WorldModel_Raw raw_model;
        if (!raw_model.Read(modelFilename.c_str()))
            return false;

        uint32 groups = raw_model.groupsArray.size();
        if (groups != 1)
            printf("Warning: '%s' does not seem to be a M2 model!\n", modelFilename.c_str());

        AABox modelBound;
        bool boundEmpty = true;

        // 遍历所有组（M2文件应该只有一个组）
        for (uint32 g = 0; g < groups; ++g)
        {
            std::vector<Vector3>& vertices = raw_model.groupsArray[g].vertexArray;

            if (vertices.empty())
            {
                std::cout << "error: model '" << spawn.name << "' has no geometry!" << std::endl;
                continue;
            }

            // 对每个顶点应用变换并扩展边界框
            uint32 nvectors = vertices.size();
            for (uint32 i = 0; i < nvectors; ++i)
            {
                // 应用缩放和旋转变换
                Vector3 v = modelPosition.transform(vertices[i]);

                // 构建或扩展边界框
                if (boundEmpty)
                {
                    modelBound = AABox(v, v);
                    boundEmpty = false;
                }
                else
                    modelBound.merge(v);
            }
        }

        // 将边界框平移到世界坐标
        spawn.iBound = modelBound + spawn.iPos;
        spawn.flags |= MOD_HAS_BOUND;
        return true;
    }

#pragma pack(push, 1)
    /**
     * @struct WMOLiquidHeader
     * @brief WMO液体数据头部结构
     *
     * 定义WMO（世界模型对象）中液体（如水、岩浆）的头部信息。
     * 使用紧凑打包（1字节对齐）以确保二进制格式兼容。
     */
    struct WMOLiquidHeader
    {
        int xverts, yverts, xtiles, ytiles;  ///< 顶点和瓦片数量
        float pos_x;                          ///< X坐标
        float pos_y;                          ///< Y坐标
        float pos_z;                          ///< Z坐标
        short material;                       ///< 材质ID
    };
#pragma pack(pop)
    //=================================================================
    /**
     * @brief 转换原始模型文件为优化格式
     * @param pModelFilename 模型文件名（相对路径）
     * @return true 如果转换成功
     * @return false 如果转换失败
     *
     * 该函数将提取的原始模型文件转换为优化的.vmo格式。
     * 转换过程包括：
     * 1. 读取原始模型数据（顶点、三角形、液体等）
     * 2. 构建优化的WorldModel结构
     * 3. 写入二进制.vmo文件
     */
    bool TileAssembler::convertRawFile(const std::string& pModelFilename)
    {
        bool success = true;
        std::string filename = iSrcDir;
        if (filename.length() >0)
            filename.push_back('/');
        filename.append(pModelFilename);

        // 读取原始模型文件
        WorldModel_Raw raw_model;
        if (!raw_model.Read(filename.c_str()))
            return false;

        // 构建优化的WorldModel
        WorldModel model;
        model.setRootWmoID(raw_model.RootWMOID);
        if (!raw_model.groupsArray.empty())
        {
            std::vector<GroupModel> groupsArray;

            uint32 groups = raw_model.groupsArray.size();
            // 遍历所有组模型，转换数据
            for (uint32 g = 0; g < groups; ++g)
            {
                GroupModel_Raw& raw_group = raw_model.groupsArray[g];
                // 创建优化的组模型，包含标志、WMOID和边界框
                groupsArray.push_back(GroupModel(raw_group.mogpflags, raw_group.GroupWMOID, raw_group.bounds ));
                // 设置网格数据（顶点和三角形）
                groupsArray.back().setMeshData(raw_group.vertexArray, raw_group.triangles);
                // 设置液体数据
                groupsArray.back().setLiquidData(raw_group.liquid);
            }

            model.setGroupModels(groupsArray);
        }

        // 写入优化的.vmo文件
        success = model.writeFile(iDestDir + "/" + pModelFilename + ".vmo");
        //std::cout << "readRawFile2: '" << pModelFilename << "' tris: " << nElements << " nodes: " << nNodes << std::endl;
        return success;
    }

    /**
     * @brief 导出游戏对象模型列表
     *
     * 该函数处理temp_gameobject_models文件，该文件包含游戏中所有可放置的游戏对象模型列表。
     * 函数会：
     * 1. 读取模型列表
     * 2. 为每个模型计算边界框
     * 3. 生成优化的游戏对象模型文件（GameObjectModels.dtree）
     *
     * 这些模型用于游戏对象（如箱子、门等）的碰撞检测。
     */
    void TileAssembler::exportGameobjectModels()
    {
        // 打开游戏对象模型列表文件
        FILE* model_list = fopen((iSrcDir + "/" + "temp_gameobject_models").c_str(), "rb");
        if (!model_list)
            return;

        // 验证文件格式标识
        char ident[8];
        if (fread(ident, 1, 8, model_list) != 8 || memcmp(ident, VMAP::RAW_VMAP_MAGIC, 8) != 0)
        {
            fclose(model_list);
            return;
        }

        // 打开输出文件
        FILE* model_list_copy = fopen((iDestDir + "/" + GAMEOBJECT_MODELS).c_str(), "wb");
        if (!model_list_copy)
        {
            fclose(model_list);
            return;
        }

        // 写入文件格式标识
        fwrite(VMAP::VMAP_MAGIC, 1, 8, model_list_copy);

        uint32 name_length, displayId;
        uint8 isWmo;
        char buff[500];

        // 遍历模型列表
        while (true)
        {
            // 读取显示ID
            if (fread(&displayId, sizeof(uint32), 1, model_list) != 1)
                if (feof(model_list))   // EOF标志只有在读取失败后才会设置
                    break;

            // 读取模型类型和名称
            if (fread(&isWmo, sizeof(uint8), 1, model_list) != 1
                || fread(&name_length, sizeof(uint32), 1, model_list) != 1
                || name_length >= sizeof(buff)
                || fread(&buff, sizeof(char), name_length, model_list) != name_length)
            {
                std::cout << "\nFile 'temp_gameobject_models' seems to be corrupted" << std::endl;
                break;
            }

            std::string model_name(buff, name_length);

            // 读取原始模型数据
            WorldModel_Raw raw_model;
            if (!raw_model.Read((iSrcDir + "/" + model_name).c_str()) )
                continue;

            // 记录已生成的模型文件
            spawnedModelFiles.insert(model_name);

            // 计算模型边界框
            AABox bounds;
            bool boundEmpty = true;
            for (GroupModel_Raw& g : raw_model.groupsArray)
            {
                for (Vector3& v : g.vertexArray)
                {
                    if (boundEmpty)
                        bounds = AABox(v, v), boundEmpty = false;
                    else
                        bounds.merge(v);
                }
            }

            // 验证边界框有效性
            if (bounds.isEmpty())
            {
                std::cout << "\nModel " << std::string(buff, name_length) << " has empty bounding box" << std::endl;
                continue;
            }

            if (!bounds.isFinite())
            {
                std::cout << "\nModel " << std::string(buff, name_length) << " has invalid bounding box" << std::endl;
                continue;
            }

            // 写入游戏对象模型信息：displayId + isWmo + name_length + name + bounds
            fwrite(&displayId, sizeof(uint32), 1, model_list_copy);
            fwrite(&isWmo, sizeof(uint8), 1, model_list_copy);
            fwrite(&name_length, sizeof(uint32), 1, model_list_copy);
            fwrite(&buff, sizeof(char), name_length, model_list_copy);
            fwrite(&bounds.low(), sizeof(Vector3), 1, model_list_copy);
            fwrite(&bounds.high(), sizeof(Vector3), 1, model_list_copy);
        }

        fclose(model_list);
        fclose(model_list_copy);
    }

// 临时使用宏定义来简化读取/检查代码（失败时关闭文件并返回）
#define READ_OR_RETURN(V, S) if (fread((V), (S), 1, rf) != 1) { \
                                fclose(rf); printf("readfail, op = %i\n", readOperation); return(false); }
#define READ_OR_RETURN_WITH_DELETE(V, S) if (fread((V), (S), 1, rf) != 1) { \
                                fclose(rf); printf("readfail, op = %i\n", readOperation); delete[] V; return(false); };
#define CMP_OR_RETURN(V, S)  if (strcmp((V), (S)) != 0)        { \
                                fclose(rf); printf("cmpfail, %s!=%s\n", V, S);return(false); }

    /**
     * @brief 从文件读取原始组模型数据
     * @param rf 文件指针
     * @return true 如果读取成功
     * @return false 如果读取失败
     *
     * 该函数从原始模型文件中读取组模型数据，包括：
     * - 组标志和WMO ID
     * - 边界框
     * - 液体标志
     * - BSP树分支信息（GRP块）
     * - 三角形索引数据（INDX块）
     * - 顶点数据（VERT块）
     * - 液体数据（LIQU块，如果存在）
     */
    bool GroupModel_Raw::Read(FILE* rf)
    {
        char blockId[5];
        blockId[4] = 0;
        int blocksize;
        int readOperation = 0;

        // 读取组模型标志和WMO ID
        READ_OR_RETURN(&mogpflags, sizeof(uint32));
        READ_OR_RETURN(&GroupWMOID, sizeof(uint32));

        // 读取边界框
        Vector3 vec1, vec2;
        READ_OR_RETURN(&vec1, sizeof(Vector3));
        READ_OR_RETURN(&vec2, sizeof(Vector3));
        bounds.set(vec1, vec2);

        // 读取液体标志
        READ_OR_RETURN(&liquidflags, sizeof(uint32));

        // 读取GRP块：BSP树分支信息（目前未使用）
        uint32 branches;
        READ_OR_RETURN(&blockId, 4);
        CMP_OR_RETURN(blockId, "GRP ");
        READ_OR_RETURN(&blocksize, sizeof(int));
        READ_OR_RETURN(&branches, sizeof(uint32));
        for (uint32 b=0; b<branches; ++b)
        {
            uint32 indexes;
            // 每个分支的索引（目前未使用）
            READ_OR_RETURN(&indexes, sizeof(uint32));
        }

        // 读取INDX块：三角形索引数据
        READ_OR_RETURN(&blockId, 4);
        CMP_OR_RETURN(blockId, "INDX");
        READ_OR_RETURN(&blocksize, sizeof(int));
        uint32 nindexes;
        READ_OR_RETURN(&nindexes, sizeof(uint32));

        if (nindexes >0)
        {
            // 读取索引数组并构建三角形列表
            uint16 *indexarray = new uint16[nindexes];
            READ_OR_RETURN_WITH_DELETE(indexarray, nindexes*sizeof(uint16));
            triangles.reserve(nindexes / 3);

            // 每3个索引组成一个三角形
            for (uint32 i=0; i<nindexes; i+=3)
                triangles.push_back(MeshTriangle(indexarray[i], indexarray[i+1], indexarray[i+2]));

            delete[] indexarray;
        }

        // 读取VERT块：顶点数据
        READ_OR_RETURN(&blockId, 4);
        CMP_OR_RETURN(blockId, "VERT");
        READ_OR_RETURN(&blocksize, sizeof(int));
        uint32 nvectors;
        READ_OR_RETURN(&nvectors, sizeof(uint32));

        if (nvectors >0)
        {
            // 读取顶点数组（每个顶点3个float：x, y, z）
            float *vectorarray = new float[nvectors*3];
            READ_OR_RETURN_WITH_DELETE(vectorarray, nvectors*sizeof(float)*3);

            // 构建顶点列表
            for (uint32 i=0; i<nvectors; ++i)
                vertexArray.push_back( Vector3(vectorarray + 3*i) );

            delete[] vectorarray;
        }

        // 读取LIQU块：液体数据（如果存在）
        liquid = nullptr;
        if (liquidflags & 3)
        {
            READ_OR_RETURN(&blockId, 4);
            CMP_OR_RETURN(blockId, "LIQU");
            READ_OR_RETURN(&blocksize, sizeof(int));
            uint32 liquidType;
            READ_OR_RETURN(&liquidType, sizeof(uint32));

            if (liquidflags & 1)
            {
                // 有液体几何数据，读取液体头部和高度/标志数据
                WMOLiquidHeader hlq;
                READ_OR_RETURN(&hlq, sizeof(WMOLiquidHeader));
                liquid = new WmoLiquid(hlq.xtiles, hlq.ytiles, Vector3(hlq.pos_x, hlq.pos_y, hlq.pos_z), liquidType);

                // 读取高度数据
                uint32 size = hlq.xverts * hlq.yverts;
                READ_OR_RETURN(liquid->GetHeightStorage(), size * sizeof(float));

                // 读取标志数据
                size = hlq.xtiles * hlq.ytiles;
                READ_OR_RETURN(liquid->GetFlagsStorage(), size);
            }
            else
            {
                // 没有液体几何数据，只有液体类型（通常是简单平面）
                liquid = new WmoLiquid(0, 0, Vector3::zero(), liquidType);
                liquid->GetHeightStorage()[0] = bounds.high().z;
            }
        }

        return true;
    }

    /**
     * @brief GroupModel_Raw析构函数
     *
     * 释放液体数据内存。
     */
    GroupModel_Raw::~GroupModel_Raw()
    {
        delete liquid;
    }

    /**
     * @brief 从文件读取原始世界模型数据
     * @param path 模型文件路径
     * @return true 如果读取成功
     * @return false 如果读取失败
     *
     * 该函数从原始模型文件中读取完整的WMO或M2模型数据。
     * 文件格式：
     * - RAW_VMAP_MAGIC标识（8字节）
     * - 临时顶点数量（4字节，用于提取器，此处跳过）
     * - 组数量（4字节）
     * - 根WMO ID（4字节）
     * - 各个组模型数据
     */
    bool WorldModel_Raw::Read(const char * path)
    {
        FILE* rf = fopen(path, "rb");
        if (!rf)
        {
            printf("ERROR: Can't open raw model file: %s\n", path);
            return false;
        }

        char ident[9];
        ident[8] = '\0';
        int readOperation = 0;

        // 读取并验证文件格式标识
        READ_OR_RETURN(&ident, 8);
        CMP_OR_RETURN(ident, RAW_VMAP_MAGIC);

        // 读取一个int，这在导出时需要，但这里需要跳过
        uint32 tempNVectors;
        READ_OR_RETURN(&tempNVectors, sizeof(tempNVectors));

        // 读取组数量和根WMO ID
        uint32 groups;
        READ_OR_RETURN(&groups, sizeof(uint32));
        READ_OR_RETURN(&RootWMOID, sizeof(uint32));

        // 读取所有组模型数据
        groupsArray.resize(groups);
        bool succeed = true;
        for (uint32 g = 0; g < groups && succeed; ++g)
            succeed = groupsArray[g].Read(rf);

        // 如果成功，关闭文件；否则会在Read函数内部关闭
        if (succeed)
            fclose(rf);
        return succeed;
    }

    // 释放临时使用的宏定义
    #undef READ_OR_RETURN
    #undef CMP_OR_RETURN
}
