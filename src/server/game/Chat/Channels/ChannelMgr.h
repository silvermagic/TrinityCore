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
 * @file ChannelMgr.h
 * @brief 聊天频道管理器头文件
 *
 * 本模块实现了聊天频道管理器，负责：
 * - 管理所有频道的创建、查找和销毁
 * - 按阵营分离频道（联盟/部落）
 * - 管理内置频道和自定义频道
 * - 处理频道数据的数据库加载和保存
 * - 提供频道查询接口
 *
 * 设计要点：
 * - 每个阵营有独立的频道管理器实例
 * - 可配置是否允许跨阵营频道交互
 * - 自定义频道支持持久化
 * - 内置频道按区域区分（区域相关频道）
 */

#ifndef __TRINITY_CHANNELMGR_H
#define __TRINITY_CHANNELMGR_H

#include "Define.h"
#include "Hash.h"
#include <string>
#include <unordered_map>

class Channel;
class Player;
class WorldPacket;
struct AreaTableEntry;

/**
 * @class ChannelMgr
 * @brief 聊天频道管理器类
 *
 * 管理所有聊天频道的创建、查找和销毁。
 * 每个阵营（联盟/部落）有独立的频道管理器实例。
 *
 * 主要职责：
 * - 管理内置频道（系统频道）和自定义频道
 * - 从数据库加载和保存自定义频道
 * - 提供频道查找接口
 * - 处理玩家的频道查询请求
 *
 * 内置频道：
 * - 由 DBC 定义，按频道 ID 和区域 ID 标识
 * - 名称根据客户端语言环境变化
 * - 不保存到数据库
 *
 * 自定义频道：
 * - 由玩家创建，按名称标识（不区分大小写）
 * - 数据持久化到数据库
 * - 支持密码、所有者、封禁列表等属性
 */
class TC_GAME_API ChannelMgr
{
    typedef std::unordered_map<std::wstring, Channel*> CustomChannelContainer;  ///< 自定义频道容器（按名称索引，宽字符支持大小写不敏感）
    typedef std::unordered_map<std::pair<uint32 /*channelId*/, uint32 /*zoneId*/>, Channel*> BuiltinChannelContainer;  ///< 内置频道容器（按频道ID和区域ID索引）

    protected:
        /**
         * @brief 构造函数
         * @param team 阵营（ALLIANCE 或 HORDE）
         */
        explicit ChannelMgr(uint32 team) : _team(team) { }

        /**
         * @brief 析构函数
         *
         * 清理所有频道实例。
         */
        ~ChannelMgr();

    public:
        /**
         * @brief 从数据库加载自定义频道
         *
         * 在服务器启动时调用，加载所有自定义频道的设置。
         * 包括频道名称、阵营、公告开关、所有权开关、密码和封禁列表。
         * 会自动清理过期的频道。
         */
        static void LoadFromDB();

        /**
         * @brief 获取指定阵营的频道管理器
         * @param team 阵营（ALLIANCE 或 HORDE）
         * @return 频道管理器指针，如果阵营无效则返回 nullptr
         *
         * 如果配置了跨阵营频道交互，则返回联盟频道管理器。
         */
        static ChannelMgr* forTeam(uint32 team);

        /**
         * @brief 根据名称部分匹配查找玩家所在的频道
         * @param namePart 频道名称部分（不区分大小写）
         * @param playerSearcher 查找玩家（用于获取已加入的频道列表）
         * @return 匹配的频道指针，如果没有匹配则返回 nullptr
         */
        static Channel* GetChannelForPlayerByNamePart(std::string const& namePart, Player* playerSearcher);

        /**
         * @brief 保存所有自定义频道到数据库
         *
         * 定期调用以保存所有自定义频道的当前状态。
         */
        void SaveToDB();

        /**
         * @brief 获取或创建系统频道
         * @param channelId 频道 ID（来自 ChatChannels.dbc）
         * @param zoneEntry 区域信息，用于区域相关频道，默认为 nullptr
         * @return 系统频道指针
         *
         * 如果频道不存在则自动创建。
         */
        Channel* GetSystemChannel(uint32 channelId, AreaTableEntry const* zoneEntry = nullptr);

        /**
         * @brief 创建自定义频道
         * @param name 频道名称
         * @return 新创建的频道指针，如果频道已存在则返回 nullptr
         */
        Channel* CreateCustomChannel(std::string const& name);

        /**
         * @brief 获取自定义频道
         * @param name 频道名称（不区分大小写）
         * @return 频道指针，如果不存在则返回 nullptr
         */
        Channel* GetCustomChannel(std::string const& name) const;

        /**
         * @brief 获取频道（通用接口）
         * @param channelId 频道 ID，0 表示自定义频道
         * @param name 频道名称（仅对自定义频道使用）
         * @param player 请求玩家（用于发送错误消息）
         * @param pkt 是否发送错误消息，默认为 true
         * @param zoneEntry 区域信息（仅对内置频道使用），默认为 nullptr
         * @return 频道指针，如果不存在则返回 nullptr 并发送错误消息
         */
        Channel* GetChannel(uint32 channelId, std::string const& name, Player* player, bool pkt = true, AreaTableEntry const* zoneEntry = nullptr) const;

        /**
         * @brief 处理频道离开后的清理
         * @param channelId 频道 ID
         * @param zoneEntry 区域信息
         *
         * 如果频道中没有玩家，则销毁该频道。
         */
        void LeftChannel(uint32 channelId, AreaTableEntry const* zoneEntry);

    private:
        /**
         * @brief 构建"不在频道中"数据包
         * @param data 输出数据包
         * @param name 频道名称
         */
        static void MakeNotOnPacket(WorldPacket* data, std::string const& name);

        CustomChannelContainer _customChannels;  ///< 自定义频道列表
        BuiltinChannelContainer _channels;       ///< 内置频道列表
        uint32 const _team;                      ///< 阵营（ALLIANCE 或 HORDE）
};

#endif
