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
 * @file M2Stores.cpp
 * @brief M2 模型相机数据存储系统实现
 *
 * 本文件实现了从 M2 模型文件中提取和存储过场动画相机数据的功能。
 * M2 是魔兽世界使用的 3D 模型格式,包含模型、动画、相机等信息。
 *
 * 主要功能:
 * - 加载和解析 M2 文件中的相机定义
 * - 将相机样条数据转换为游戏世界坐标
 * - 存储飞行相机路径点供过场动画系统使用
 *
 * @see M2Stores.h - 相关头文件
 * @see M2Structure.h - M2 文件格式定义
 * @see DBCStores.h - 数据库客户端数据存储
 */

#include "M2Stores.h"
#include "Containers.h"
#include "DBCStores.h"
#include "Log.h"
#include "M2Structure.h"
#include "Timer.h"
#include <boost/filesystem/path.hpp>
#include <G3D/Vector4.h>
#include <fstream>

/**
 * @brief 飞行相机集合类型定义
 *
 * 存储单个过场动画的所有相机路径点
 */
typedef std::vector<FlyByCamera> FlyByCameraCollection;

/**
 * @brief 全局飞行相机存储
 *
 * 键值对映射: 相机ID -> 相机路径点集合
 * 存储所有加载的过场动画相机路径数据
 */
std::unordered_map<uint32, FlyByCameraCollection> sFlyByCameraStore;

/**
 * @brief 将样条坐标转换为游戏世界坐标
 *
 * M2 文件中的相机位置使用相对坐标系统,需要通过此函数转换到游戏世界的绝对坐标。
 * 转换过程包括:
 * 1. 将基础位置与样条向量相加得到相对位置
 * 2. 计算相对位置的极坐标(距离和角度)
 * 3. 根据 DBC 中定义的相机原点和朝向进行旋转变换
 * 4. 得到最终的世界坐标
 *
 * @param DBCPosition DBC 中定义的相机原点信息
 *        - x, y, z: 原点在世界坐标系中的位置
 *        - w: 原点的朝向角度(弧度)
 * @param basePosition 相机的基础位置偏移(从 M2 文件读取)
 * @param splineVector 样条向量点(从 M2 文件读取的插值点)
 *
 * @return G3D::Vector3 转换后的游戏世界坐标 (x, y, z)
 *
 * @note 转换公式:
 *       - 距离 = sqrt((相对x)^2 + (相对y)^2)
 *       - 角度 = atan2(相对x, 相对y) - 朝向
 *       - 世界x = 原点x + 距离 * sin(角度)
 *       - 世界y = 原点y + 距离 * cos(角度)
 *       - 世界z = 原点z + 相对z
 */
G3D::Vector3 TranslateLocation(G3D::Vector4 const* DBCPosition, G3D::Vector3 const* basePosition, G3D::Vector3 const* splineVector)
{
    G3D::Vector3 work;

    // 计算相对于基础位置的偏移
    float x = basePosition->x + splineVector->x;
    float y = basePosition->y + splineVector->y;
    float z = basePosition->z + splineVector->z;

    // 计算极坐标距离和角度
    float const distance = sqrt((x * x) + (y * y));
    float angle = std::atan2(x, y) - DBCPosition->w;

    // 角度标准化到 [0, 2π] 范围
    if (angle < 0)
        angle += 2 * float(M_PI);

    // 计算最终世界坐标
    work.x = DBCPosition->x + (distance * sin(angle));
    work.y = DBCPosition->y + (distance * cos(angle));
    work.z = DBCPosition->z + z;

    return work;
}

/**
 * @brief 读取单个相机数据
 *
 * 从 M2 文件的相机定义中提取完整的飞行相机路径,包括:
 * - 相机位置时间序列
 * - 相机目标位置时间序列
 * - 计算相机在每个时间点的朝向(朝向目标)
 *
 * 处理流程:
 * 1. 读取目标位置序列(用于计算相机朝向)
 * 2. 读取相机位置序列
 * 3. 对每个相机位置点,找到对应的目标位置并计算朝向
 * 4. 如果目标与相机时间戳不同,进行线性插值
 *
 * @param cam 指向 M2 相机结构的指针
 * @param buffSize 缓冲区大小(用于边界检查)
 * @param header M2 文件头指针
 * @param dbcentry DBC 中的过场动画相机条目
 *
 * @return bool 读取成功返回 true,文件损坏或越界访问返回 false
 *
 * @note 在 3.3.5 版本中,多个相机未被使用,只处理第一个相机
 * @note 数据验证: 所有偏移量都会进行边界检查,防止越界访问
 */
bool readCamera(M2Camera const* cam, uint32 buffSize, M2Header const* header, CinematicCameraEntry const* dbcentry)
{
    // 获取文件缓冲区起始地址
    char const* buffer = reinterpret_cast<char const*>(header);

    // 存储相机路径和目标路径
    FlyByCameraCollection cameras;
    FlyByCameraCollection targetcam;

    // 从 DBC 提取相机原点数据
    G3D::Vector4 DBCData;
    DBCData.x = dbcentry->Origin.X;
    DBCData.y = dbcentry->Origin.Y;
    DBCData.z = dbcentry->Origin.Z;
    DBCData.w = dbcentry->OriginFacing;

    // ========================================
    // 第一步: 读取目标位置序列
    // 目标位置用于计算相机的朝向
    // ========================================
    for (uint32 k = 0; k < cam->target_positions.timestamps.number; ++k)
    {
        // 提取目标位置时间戳数组
        if (cam->target_positions.timestamps.offset_elements + sizeof(M2Array) > buffSize)
            return false;
        M2Array const* targTsArray = reinterpret_cast<M2Array const*>(buffer + cam->target_positions.timestamps.offset_elements);

        // 边界检查
        if (targTsArray->offset_elements + sizeof(uint32) > buffSize || cam->target_positions.values.offset_elements + sizeof(M2Array) > buffSize)
            return false;

        uint32 const* targTimestamps = reinterpret_cast<uint32 const*>(buffer + targTsArray->offset_elements);
        M2Array const* targArray = reinterpret_cast<M2Array const*>(buffer + cam->target_positions.values.offset_elements);

        if (targArray->offset_elements + sizeof(M2SplineKey<G3D::Vector3>) > buffSize)
            return false;

        M2SplineKey<G3D::Vector3> const* targPositions = reinterpret_cast<M2SplineKey<G3D::Vector3> const*>(buffer + targArray->offset_elements);

        // 遍历所有目标位置点
        uint32 currPos = targArray->offset_elements;
        for (uint32 i = 0; i < targTsArray->number; ++i)
        {
            if (currPos + sizeof(M2SplineKey<G3D::Vector3>) > buffSize)
                return false;

            // 将样条坐标转换为世界坐标
            G3D::Vector3 newPos = TranslateLocation(&DBCData, &cam->target_position_base, &targPositions->p0);

            // 创建目标相机点并添加到集合
            FlyByCamera thisCam;
            thisCam.timeStamp = targTimestamps[i];
            thisCam.locations.Relocate(newPos.x, newPos.y, newPos.z, 0.0f);
            targetcam.push_back(thisCam);

            targPositions++;
            currPos += sizeof(M2SplineKey<G3D::Vector3>);
        }
    }

    // ========================================
    // 第二步: 读取相机位置序列并计算朝向
    // ========================================
    for (uint32 k = 0; k < cam->positions.timestamps.number; ++k)
    {
        // 提取相机位置时间戳数组
        if (cam->positions.timestamps.offset_elements + sizeof(M2Array) > buffSize)
            return false;
        M2Array const* posTsArray = reinterpret_cast<M2Array const*>(buffer + cam->positions.timestamps.offset_elements);

        // 边界检查
        if (posTsArray->offset_elements + sizeof(uint32) > buffSize || cam->positions.values.offset_elements + sizeof(M2Array) > buffSize)
            return false;

        uint32 const* posTimestamps = reinterpret_cast<uint32 const*>(buffer + posTsArray->offset_elements);
        M2Array const* posArray = reinterpret_cast<M2Array const*>(buffer + cam->positions.values.offset_elements);
        if (posArray->offset_elements + sizeof(M2SplineKey<G3D::Vector3>) > buffSize)
            return false;

        M2SplineKey<G3D::Vector3> const* positions = reinterpret_cast<M2SplineKey<G3D::Vector3> const*>(buffer + posArray->offset_elements);

        // 遍历所有相机位置点
        uint32 currPos = posArray->offset_elements;
        for (uint32 i = 0; i < posTsArray->number; ++i)
        {
            if (currPos + sizeof(M2SplineKey<G3D::Vector3>) > buffSize)
                return false;

            // 将样条坐标转换为世界坐标
            G3D::Vector3 newPos = TranslateLocation(&DBCData, &cam->position_base, &positions->p0);

            // 创建相机路径点
            FlyByCamera thisCam;
            thisCam.timeStamp = posTimestamps[i];
            thisCam.locations.Relocate(newPos.x, newPos.y, newPos.z);

            // 计算相机朝向(朝向目标位置)
            if (targetcam.size() > 0)
            {
                // 查找当前时间戳之前和之后的目标位置点
                FlyByCamera lastTarget;
                FlyByCamera nextTarget;

                // 初始化为第一个目标点
                lastTarget = targetcam[0];
                nextTarget = targetcam[0];

                // 遍历找到包围当前时间点的两个目标点
                for (uint32 j = 0; j < targetcam.size(); ++j)
                {
                    nextTarget = targetcam[j];
                    if (targetcam[j].timeStamp > posTimestamps[i])
                        break;

                    lastTarget = targetcam[j];
                }

                float x, y, z;
                lastTarget.locations.GetPosition(x, y, z);

                // 如果目标点时间戳与相机时间戳不同,进行线性插值
                if (lastTarget.timeStamp != posTimestamps[i])
                {
                    // 计算时间差和插值比例
                    uint32 timeDiffTarget = nextTarget.timeStamp - lastTarget.timeStamp;
                    uint32 timeDiffThis = posTimestamps[i] - lastTarget.timeStamp;

                    // 计算位置差
                    float xDiff = nextTarget.locations.GetPositionX() - lastTarget.locations.GetPositionX();
                    float yDiff = nextTarget.locations.GetPositionY() - lastTarget.locations.GetPositionY();

                    // 线性插值计算目标位置
                    x = lastTarget.locations.GetPositionX() + (xDiff * (float(timeDiffThis) / float(timeDiffTarget)));
                    y = lastTarget.locations.GetPositionY() + (yDiff * (float(timeDiffThis) / float(timeDiffTarget)));
                }

                // 计算相机朝向: 从相机位置指向目标位置
                float xDiff = x - thisCam.locations.GetPositionX();
                float yDiff = y - thisCam.locations.GetPositionY();
                thisCam.locations.SetOrientation(std::atan2(yDiff, xDiff));
            }

            cameras.push_back(thisCam);
            positions++;
            currPos += sizeof(M2SplineKey<G3D::Vector3>);
        }
    }

    // 将解析的相机路径存入全局存储
    sFlyByCameraStore[dbcentry->ID] = cameras;
    return true;
}

/**
 * @brief 加载所有 M2 相机文件
 *
 * 遍历 CinematicCamera.dbc 中定义的所有相机条目,加载对应的 M2 模型文件,
 * 提取相机路径数据并存储到全局存储中。
 *
 * 加载流程:
 * 1. 清空现有相机存储
 * 2. 遍历 DBC 中的所有相机定义
 * 3. 构建文件路径并转换为系统原生格式
 * 4. 打开 M2 文件并进行格式验证
 * 5. 读取文件内容并解析相机数据
 * 6. 存储到全局相机存储
 *
 * @param dataPath 游戏数据目录路径(如 "data/")
 *
 * @note 文件路径处理:
 *       - DBC 中的路径使用反斜杠,转换为正斜杠
 *       - .mdx 扩展名转换为 .m2
 *       - 使用 boost::filesystem 处理跨平台路径
 *
 * @note 错误处理:
 *       - 文件不存在时跳过(不报错)
 *       - 文件过小、魔数错误、数据损坏时记录错误并跳过
 *       - 继续处理其他文件而不中断
 */
void LoadM2Cameras(std::string const& dataPath)
{
    // 清空现有数据
    sFlyByCameraStore.clear();
    TC_LOG_INFO("server.loading", ">> Loading Cinematic Camera files");

    uint32 oldMSTime = getMSTime();

    // 遍历所有过场动画相机定义
    for (CinematicCameraEntry const* dbcentry : sCinematicCameraStore)
    {
        // 构建文件完整路径
        std::string filenameWork = dataPath;
        filenameWork.append(dbcentry->Model);

        // 统一路径分隔符为正斜杠(boost::filesystem 要求)
        std::replace(filenameWork.begin(), filenameWork.end(), '\\', '/');

        boost::filesystem::path filename = filenameWork;

        // 转换为系统原生路径格式
        filename.make_preferred();

        // 将 .mdx 扩展名替换为 .m2
        // (MDX 是 M2 格式的另一种称呼,文件内容相同)
        filename.replace_extension("m2");

        // 打开 M2 文件(二进制模式)
        std::ifstream m2file(filename.string().c_str(), std::ios::in | std::ios::binary);
        if (!m2file.is_open())
            continue;

        // 获取文件大小
        m2file.seekg(0, std::ios::end);
        std::streamoff fileSize = m2file.tellg();

        // 验证文件大小至少包含文件头
        if (static_cast<uint32>(fileSize) < sizeof(M2Header))
        {
            TC_LOG_ERROR("server.loading", "Camera file {} is damaged. File is smaller than header size", filename.string());
            m2file.close();
            continue;
        }

        // 读取并验证文件魔数(MD20)
        m2file.seekg(0, std::ios::beg);
        char fileCheck[5];
        m2file.read(fileCheck, 4);
        fileCheck[4] = 0;

        // M2 文件的魔数为 "MD20"
        if (strcmp(fileCheck, "MD20"))
        {
            TC_LOG_ERROR("server.loading", "Camera file {} is damaged. File identifier not found", filename.string());
            m2file.close();
            continue;
        }

        // 读取整个文件到内存
        std::vector<char> buffer(fileSize);
        m2file.seekg(0, std::ios::beg);
        if (!m2file.read(buffer.data(), fileSize))
        {
            m2file.close();
            continue;
        }
        m2file.close();

        // 解析文件头
        M2Header const* header = reinterpret_cast<M2Header const*>(buffer.data());

        // 验证相机数据偏移量有效性
        if (header->ofsCameras + sizeof(M2Camera) > static_cast<uint32>(fileSize))
        {
            TC_LOG_ERROR("server.loading", "Camera file {} is damaged. Camera references position beyond file end", filename.string());
            continue;
        }

        // 获取相机数据指针并读取
        M2Camera const* cam = reinterpret_cast<M2Camera const*>(buffer.data() + header->ofsCameras);
        if (!readCamera(cam, fileSize, header, dbcentry))
            TC_LOG_ERROR("server.loading", "Camera file {} is damaged. Camera references position beyond file end", filename.string());
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} cinematic waypoint sets in {} ms", (uint32)sFlyByCameraStore.size(), GetMSTimeDiffToNow(oldMSTime));
}

/**
 * @brief 获取指定过场动画相机的路径点集合
 *
 * 从全局存储中查询并返回指定 ID 的相机路径数据,
 * 用于过场动画播放时控制相机移动。
 *
 * @param cinematicCameraId 过场动画相机 ID(来自 CinematicCamera.dbc)
 *
 * @return const std::vector<FlyByCamera>* 相机路径点集合的指针
 *         如果未找到返回 nullptr
 *
 * @note 返回的指针指向全局存储中的数据,调用者不应修改或释放
 * @note 线程安全: 应只在服务器启动时调用 LoadM2Cameras,运行时只读
 *
 * @example
 * @code
 * // 获取相机 ID 1 的路径点
 * std::vector<FlyByCamera> const* cameras = GetFlyByCameras(1);
 * if (cameras)
 * {
 *     for (FlyByCamera const& cam : *cameras)
 *     {
 *         // 使用 cam.timeStamp 和 cam.locations
 *     }
 * }
 * @endcode
 */
std::vector<FlyByCamera> const* GetFlyByCameras(uint32 cinematicCameraId)
{
    return Trinity::Containers::MapGetValuePtr(sFlyByCameraStore, cinematicCameraId);
}
