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
 * @file AddonHandler.h
 * @brief 插件(Addon)处理器模块
 *
 * 本模块负责处理客户端插件相关的网络包:
 * - 接收客户端发送的插件信息包
 * - 解压缩插件数据
 * - 验证插件的有效性
 * - 构建并发送插件响应包给客户端
 *
 * 插件系统允许第三方UI扩展(如DBM, Recount等)与服务器进行通信,
 * 服务器需要验证这些插件的合法性并返回相应的状态信息。
 */

#ifndef __ADDONHANDLER_H
#define __ADDONHANDLER_H

#include "Common.h"
#include "Config.h"
#include "WorldPacket.h"

/**
 * @class AddonHandler
 * @brief 插件处理器类(单例模式)
 *
 * 负责处理客户端插件相关的网络包,包括:
 * - 解析客户端发送的插件列表
 * - 验证插件CRC校验码
 * - 构建服务器端的插件响应包
 *
 * 使用单例模式,通过sAddOnHandler宏全局访问
 */
class AddonHandler
{
    public:
        /**
         * @brief 获取AddonHandler单例实例
         * @return AddonHandler单例指针
         *
         * 使用静态局部变量实现线程安全的单例模式
         */
        static AddonHandler* instance();

        /**
         * @brief 构建插件信息响应包
         * @param Source 源数据包(客户端发送的压缩插件数据)
         * @param Target 目标数据包(服务器构建的响应包)
         * @return true表示构建成功,false表示构建失败
         *
         * 调用时机: 当收到CMSG_ADDON_INFO消息时调用
         *
         * 处理流程:
         * 1. 从源数据包读取压缩后的插件数据大小
         * 2. 使用zlib解压缩插件数据
         * 3. 遍历所有插件,验证每个插件的状态和CRC
         * 4. 构建SMSG_ADDON_INFO响应包返回给客户端
         *
         * 性能注意:
         * - 涉及zlib解压缩操作,CPU密集型
         * - 256字节的固定密钥表会被拷贝到每个非标准插件响应中
         */
        bool BuildAddonPacket(WorldPacket* Source, WorldPacket* Target);

    private:
        /**
         * @brief 私有构造函数(单例模式)
         */
        AddonHandler() { }

        /**
         * @brief 私有析构函数(单例模式)
         */
        ~AddonHandler() { }
};

/**
 * @brief 全局访问AddonHandler单例的宏
 */
#define sAddOnHandler AddonHandler::instance()

#endif
