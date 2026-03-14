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
 * @file VMapFactory.h
 * @brief VMap工厂类定义
 *
 * 本文件定义了VMap系统的工厂类，用于创建和管理VMapManager2单例。
 * VMap（Virtual Map）系统提供游戏世界的碰撞检测、视线计算和高度查询功能。
 *
 * 主要功能：
 * - 提供VMapManager2单例的访问入口
 * - 管理VMapManager2的生命周期
 * - 隐藏全局VMap数据的实现细节
 *
 * VMap与MMap的关系：
 * - VMap：处理碰撞检测、视线计算、高度查询
 * - MMap：处理寻路导航、路径计算
 * 两者协同工作，为游戏世界提供完整的物理和导航支持
 *
 * 使用方式：
 * @code
 * // 获取VMapManager2实例
 * VMapManager2* manager = VMapFactory::createOrGetVMapManager();
 *
 * // 使用manager进行操作...
 *
 * // 程序退出时清理
 * VMapFactory::clear();
 * @endcode
 */

#ifndef _VMAPFACTORY_H
#define _VMAPFACTORY_H

#include "IVMapManager.h"

namespace VMAP
{
    // 前向声明
    class VMapManager2;

    /**
     * @class VMapFactory
     * @brief VMap管理器工厂类
     *
     * 静态工具类，作为VMapManager2单例的访问点。
     * 负责VMapManager2的创建、获取和销毁。
     *
     * 设计模式：工厂模式 + 单例模式
     *
     * 线程安全说明：
     * - createOrGetVMapManager() 非线程安全，应在主线程调用
     * - clear() 非线程安全，应在所有VMap操作完成后调用
     */
    class TC_COMMON_API VMapFactory
    {
        public:
            /**
             * @brief 创建或获取VMapManager2单例
             *
             * @return VMapManager2指针，调用者不应删除此指针
             *
             * 如果单例不存在则创建，存在则直接返回。
             * 首次调用时会创建VMapManager2实例。
             *
             * 调用时机：服务器初始化时、需要访问碰撞数据时
             * 性能注意事项：首次调用有内存分配开销，后续调用为简单指针返回
             */
            static VMapManager2* createOrGetVMapManager();

            /**
             * @brief 清理并销毁VMapManager2单例
             *
             * 删除VMapManager2实例并释放所有相关资源。
             * 调用后，createOrGetVMapManager()将创建新的实例。
             *
             * 调用时机：服务器关闭时或需要完全重置碰撞系统时
             * 注意事项：确保没有其他线程正在使用VMapManager2
             */
            static void clear();
    };

}
#endif
