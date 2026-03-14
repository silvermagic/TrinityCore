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
 * @file MMapFactory.cpp
 * @brief MMap工厂类实现
 *
 * 本文件实现了MMapFactory工厂类，负责MMapManager单例的创建和管理。
 * 采用懒加载模式，首次请求时才创建实例。
 */

#include "MMapFactory.h"
#include "Config.h"

namespace MMAP
{
    // ######################## MMapFactory ########################

    /**
     * @brief 全局MMapManager单例指针
     *
     * 静态全局变量，保存唯一的MMapManager实例。
     * 使用裸指针配合手动内存管理，由MMapFactory::clear()负责释放。
     *
     * 注意：非线程安全，应在主线程初始化
     */
    MMapManager* g_MMapManager = nullptr;

    /**
     * @brief 创建或获取MMapManager单例
     *
     * 实现懒加载单例模式：
     * 1. 检查全局指针是否为空
     * 2. 为空则创建新实例
     * 3. 返回实例指针
     *
     * @return MMapManager实例指针
     *
     * 性能说明：
     * - 首次调用：O(1)内存分配 + MMapManager构造
     * - 后续调用：O(1)指针检查并返回
     */
    MMapManager* MMapFactory::createOrGetMMapManager()
    {
        // 懒加载：首次调用时创建实例
        if (g_MMapManager == nullptr)
            g_MMapManager = new MMapManager();

        return g_MMapManager;
    }

    /**
     * @brief 清理MMapManager单例
     *
     * 安全地销毁MMapManager实例：
     * 1. 检查指针有效性
     * 2. 调用析构函数释放所有地图数据
     * 3. 将指针置空，防止悬垂指针
     *
     * 注意事项：
     * - 调用后所有MMap数据将被释放
     * - 确保没有其他地方持有MMapManager的引用
     * - 下次调用createOrGetMMapManager将创建新实例
     */
    void MMapFactory::clear()
    {
        if (g_MMapManager)
        {
            delete g_MMapManager;       // 调用MMapManager析构函数，释放所有资源
            g_MMapManager = nullptr;    // 置空指针，防止重复释放
        }
    }
}
