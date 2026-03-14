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
 * @file TileAssembler.h
 * @brief 地图瓦片组装器模块
 *
 * 本模块负责将原始向量数据转换为平衡的BSP树格式：
 * - 读取从客户端提取的原始地图数据
 * - 计算模型的包围盒和变换
 * - 构建BIH树并生成.vmtree和.vmtile文件
 * - 处理游戏对象模型
 *
 * 使用流程：
 * 1. 构造TileAssembler对象，指定源目录和目标目录
 * 2. 调用convertWorld2()开始转换过程
 * 3. 转换完成后，目标目录包含所有.vmtree和.vmtile文件
 *
 * 这是离线工具，用于生成服务器端使用的VMap数据。
 */

#ifndef _TILEASSEMBLER_H_
#define _TILEASSEMBLER_H_

#include <G3D/Vector3.h>
#include <G3D/Matrix3.h>
#include <map>
#include <set>

#include "ModelInstance.h"
#include "WorldModel.h"

namespace VMAP
{
    /**
     * @class ModelPosition
     * @brief 模型位置和变换信息类
     *
     * 存储模型在世界中的位置、旋转和缩放信息，
     * 提供坐标变换功能。
     */
    class TC_COMMON_API ModelPosition
    {
        private:
            G3D::Matrix3 iRotation;     ///< 旋转矩阵，由iDir计算得出
        public:
            /**
             * @brief 默认构造函数
             */
            ModelPosition(): iScale(0.0f) { }

            G3D::Vector3 iPos;   ///< 模型位置
            G3D::Vector3 iDir;   ///< 模型旋转角度（欧拉角，度数）
            float iScale;        ///< 模型缩放比例

            /**
             * @brief 初始化旋转矩阵
             *
             * 根据iDir（欧拉角）计算旋转矩阵。
             * 使用ZYX顺序的欧拉角（偏航-俯仰-翻滚）
             */
            void init()
            {
                iRotation = G3D::Matrix3::fromEulerAnglesZYX(G3D::pif()*iDir.y/180.f, G3D::pif()*iDir.x/180.f, G3D::pif()*iDir.z/180.f);
            }

            /**
             * @brief 变换顶点坐标
             * @param pIn 输入顶点（模型空间坐标）
             * @return 输出顶点（世界空间坐标，不含平移）
             *
             * 应用缩放和旋转，不包含平移（平移在包围盒合并时处理）
             */
            G3D::Vector3 transform(const G3D::Vector3& pIn) const;

            /**
             * @brief 移动到基础位置
             * @param pBasePos 基础位置偏移
             *
             * 将位置减去基础位置，用于坐标系转换
             */
            void moveToBasePos(const G3D::Vector3& pBasePos) { iPos -= pBasePos; }
    };

    /// 唯一实体映射表：实体ID -> 模型生成数据
    typedef std::map<uint32, ModelSpawn> UniqueEntryMap;
    /// 瓦片映射表：瓦片ID -> 实体ID（多对多关系）
    typedef std::multimap<uint32, uint32> TileMap;

    /**
     * @struct MapSpawns
     * @brief 地图生成数据结构
     *
     * 存储单个地图中所有的模型生成点和瓦片映射关系
     */
    struct TC_COMMON_API MapSpawns
    {
        UniqueEntryMap UniqueEntries;   ///< 所有唯一的模型生成点
        TileMap TileEntries;            ///< 瓦片到生成点的映射
    };

    /// 地图数据映射表：地图ID -> 地图生成数据
    typedef std::map<uint32, MapSpawns*> MapData;

    /**
     * @struct GroupModel_Raw
     * @brief 原始组模型数据结构
     *
     * 用于从原始数据文件读取WMO组模型数据
     */
    struct TC_COMMON_API GroupModel_Raw
    {
        uint32 mogpflags;               ///< WMO组标志
        uint32 GroupWMOID;              ///< WMO组ID

        G3D::AABox bounds;              ///< 包围盒
        uint32 liquidflags;             ///< 液体标志
        std::vector<MeshTriangle> triangles;    ///< 三角形索引数组
        std::vector<G3D::Vector3> vertexArray;  ///< 顶点数组
        class WmoLiquid* liquid;        ///< 液体数据指针

        /**
         * @brief 默认构造函数
         */
        GroupModel_Raw() : mogpflags(0), GroupWMOID(0), liquidflags(0),
            liquid(nullptr) { }

        /**
         * @brief 析构函数
         */
        ~GroupModel_Raw();

        /**
         * @brief 从文件读取组模型数据
         * @param f 文件指针
         * @return 读取成功返回true，失败返回false
         */
        bool Read(FILE* f);
    };

    /**
     * @struct WorldModel_Raw
     * @brief 原始世界模型数据结构
     *
     * 用于从原始数据文件读取完整的世界模型数据
     */
    struct TC_COMMON_API WorldModel_Raw
    {
        uint32 RootWMOID;                       ///< 根WMO ID
        std::vector<GroupModel_Raw> groupsArray; ///< 所有组模型数组

        /**
         * @brief 从文件读取世界模型数据
         * @param path 文件路径
         * @return 读取成功返回true，失败返回false
         */
        bool Read(const char * path);
    };

    /**
     * @class TileAssembler
     * @brief 瓦片组装器类，将原始向量数据转换为平衡的BSP树
     *
     * 该类是地图数据转换的核心类，负责：
     * - 读取从客户端提取的原始地图数据
     * - 计算模型的变换和包围盒
     * - 构建空间索引树
     * - 生成服务器使用的.vmtree和.vmtile文件
     *
     * 使用流程：
     * 1. 构造对象，指定源目录和目标目录
     * 2. 调用convertWorld2()开始转换
     * 3. 检查返回值确认转换结果
     *
     * 性能注意事项：
     * - convertWorld2()是重量级操作，需要处理大量数据
     * - 转换过程可能需要几分钟到几十分钟
     */
    class TC_COMMON_API TileAssembler
    {
        private:
            std::string iDestDir;              ///< 目标目录路径
            std::string iSrcDir;               ///< 源目录路径
            MapData mapData;                   ///< 地图数据映射表
            std::set<std::string> spawnedModelFiles;  ///< 已生成的模型文件集合

        public:
            /**
             * @brief 构造函数
             * @param pSrcDirName 源目录名称（包含原始数据）
             * @param pDestDirName 目标目录名称（输出.vmtree和.vmtile文件）
             */
            TileAssembler(const std::string& pSrcDirName, const std::string& pDestDirName);

            /**
             * @brief 虚析构函数
             */
            virtual ~TileAssembler();

            /**
             * @brief 转换世界地图数据（主入口函数）
             * @return 转换成功返回true，失败返回false
             *
             * 执行完整的转换流程：
             * 1. 读取地图生成数据（readMapSpawns）
             * 2. 为每个地图构建BIH树
             * 3. 写入.vmtree和.vmtile文件
             * 4. 导出游戏对象模型（exportGameobjectModels）
             * 5. 转换所有原始模型文件（convertRawFile）
             *
             * 调用时机：在准备好源数据后调用一次
             */
            bool convertWorld2();

            /**
             * @brief 读取地图生成数据
             * @return 读取成功返回true，失败返回false
             *
             * 从dir_bin文件读取所有地图的模型生成信息
             */
            bool readMapSpawns();

            /**
             * @brief 计算变换后的包围盒
             * @param spawn 模型生成数据（输入/输出）
             * @return 计算成功返回true，失败返回false
             *
             * 对于M2模型，需要从模型文件读取顶点并计算变换后的包围盒
             */
            bool calculateTransformedBound(ModelSpawn &spawn);

            /**
             * @brief 导出游戏对象模型
             *
             * 处理temp_gameobject_models文件中的游戏对象模型，
             * 计算包围盒并输出到GAMEOBJECT_MODELS文件
             */
            void exportGameobjectModels();

            /**
             * @brief 转换原始模型文件
             * @param pModelFilename 模型文件名（相对路径）
             * @return 转换成功返回true，失败返回false
             *
             * 将原始模型数据转换为.vmo格式
             */
            bool convertRawFile(const std::string& pModelFilename);
    };

}                                                           // VMAP
#endif                                                      /*_TILEASSEMBLER_H_*/
