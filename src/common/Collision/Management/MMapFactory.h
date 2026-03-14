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
 * @file MMapFactory.h
 * @brief MMap（Move Map，移动地图）工厂类定义
 *
 * 本文件定义了MMap系统的工厂类，用于创建和管理MMapManager单例。
 * MMap系统基于Detour导航网格库，为游戏中的寻路系统提供支持。
 *
 * 主要功能：
 * - 提供MMapManager单例的访问入口
 * - 管理MMapManager的生命周期
 * - 隐藏全局MMap数据的实现细节
 *
 * MMap与VMap的关系：
 * - VMap：处理碰撞检测、视线计算、高度查询
 * - MMap：处理寻路导航、路径计算
 * 两者协同工作，为游戏世界提供完整的物理和导航支持
 */

#ifndef _MMAP_FACTORY_H
#define _MMAP_FACTORY_H

#include "Define.h"
#include "MMapManager.h"
#include "DetourAlloc.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include <unordered_map>

namespace MMAP
{
    /**
     * @enum MMAP_LOAD_RESULT
     * @brief MMap加载结果枚举
     *
     * 用于表示导航地图加载操作的结果状态
     */
    enum MMAP_LOAD_RESULT
    {
        MMAP_LOAD_RESULT_ERROR,     ///< 加载过程中发生错误
        MMAP_LOAD_RESULT_OK,        ///< 加载成功
        MMAP_LOAD_RESULT_IGNORED    ///< 加载请求被忽略（如已加载或配置禁用）
    };

    /**
     * @class MMapFactory
     * @brief MMap管理器工厂类
     *
     * 静态工具类，作为MMapManager单例的访问点。
     * 负责MMapManager的创建、获取和销毁。
     *
     * 设计模式：工厂模式 + 单例模式
     *
     * 使用方式：
     * @code
     * // 获取MMapManager实例
     * MMapManager* manager = MMapFactory::createOrGetMMapManager();
     *
     * // 使用manager进行操作...
     *
     * // 程序退出时清理
     * MMapFactory::clear();
     * @endcode
     *
     * 线程安全说明：
     * - createOrGetMMapManager() 非线程安全，应在主线程调用
     * - clear() 非线程安全，应在所有地图操作完成后调用
     */
    class TC_COMMON_API MMapFactory
    {
        public:
            /**
             * @brief 创建或获取MMapManager单例
             *
             * @return MMapManager指针，调用者不应删除此指针
             *
             * 如果单例不存在则创建，存在则直接返回。
             * 首次调用时会创建MMapManager实例。
             *
             * 调用时机：服务器初始化时、需要访问导航数据时
             * 性能注意事项：首次调用有内存分配开销，后续调用为简单指针返回
             */
            static MMapManager* createOrGetMMapManager();

            /**
             * @brief 清理并销毁MMapManager单例
             *
             * 删除MMapManager实例并释放所有相关资源。
             * 调用后，createOrGetMMapManager()将创建新的实例。
             *
             * 调用时机：服务器关闭时或需要完全重置导航系统时
             * 注意事项：确保没有其他线程正在使用MMapManager
             */
            static void clear();
    };
}

#endif
