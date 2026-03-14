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
 * @file WorldStatePackets.h
 * @brief 世界状态数据包定义模块
 *
 * 本文件定义了与世界状态(World State)相关的网络数据包结构。
 * 世界状态用于在服务器和客户端之间同步游戏世界的动态状态信息，
 * 例如战场分数、冬拥湖湖状态、区域控制状态等。
 *
 * 主要功能:
 * - 初始化玩家的世界状态视图(InitWorldStates)
 * - 更新单个世界状态变量(UpdateWorldState)
 *
 * 世界状态变量通常用于:
 * - 战场系统: 显示联盟/部落的资源分数、占领进度
 * - 冬拥湖: 显示防守方、攻城武器数量等
 * - 其他区域控制相关的UI显示
 *
 * @see WorldStateMgr 世界状态管理器
 */

#ifndef WorldStatePackets_h__
#define WorldStatePackets_h__

#include "Packet.h"

namespace WorldPackets
{
    namespace WorldState
    {
        /**
         * @class InitWorldStates
         * @brief 初始化世界状态数据包
         *
         * 此数据包用于在玩家进入地图、区域或改变区域时,
         * 向客户端发送完整的世界状态变量列表。
         * 客户端收到此包后会初始化本地存储的世界状态值,
         * 并更新相应的UI显示。
         *
         * 继承自 ServerPacket,表示这是一个服务器下发的数据包。
         *
         * 调用时机:
         * - 玩家登录进入游戏时
         * - 玩家传送至新地图时
         * - 玩家进入新区域时(通过区域触发器)
         * - 进入战场或竞技场时
         *
         * 性能注意事项:
         * - 世界状态列表可能包含数十个变量,需合理设置包大小
         * - 避免频繁发送,仅在必要时触发
         */
        class InitWorldStates final : public ServerPacket
        {
        public:
            /**
             * @struct WorldStateInfo
             * @brief 世界状态变量信息结构
             *
             * 用于存储单个世界状态变量的ID和对应的值。
             * VariableID 是世界状态变量的唯一标识符,
             * 通常在 DBC 文件中定义或在代码中硬编码。
             *
             * 例如:
             * - 世界状态ID 1234 可能表示"联盟资源分数"
             * - 世界状态ID 1235 可能表示"部落资源分数"
             */
            struct WorldStateInfo
            {
                /**
                 * @brief 构造函数
                 * @param variableID 世界状态变量ID
                 * @param value 变量当前值
                 */
                WorldStateInfo(int32 variableID, int32 value) : VariableID(variableID), Value(value) { }

                int32 VariableID;  ///< 世界状态变量ID,用于标识特定的世界状态
                int32 Value;       ///< 世界状态变量的当前值
            };

            /**
             * @brief 构造函数
             *
             * 初始化数据包,设置操作码为 SMSG_INIT_WORLD_STATES,
             * 并预分配最小包大小(地图ID + 区域ID + 区域ID + 数量字段)。
             */
            InitWorldStates();

            /**
             * @brief 序列化数据包
             * @return 返回序列化后的 WorldPacket 指针
             *
             * 将世界状态信息序列化为网络数据包格式:
             * 1. 写入地图ID、区域ID、区域ID
             * 2. 写入世界状态变量数量
             * 3. 遍历并写入每个变量的ID和值
             *
             * 序列化格式:
             * - int32: 地图ID
             * - int32: 区域ID
             * - int32: 区域ID
             * - uint16: 世界状态变量数量
             * - 对每个变量:
             *   - int32: 变量ID
             *   - int32: 变量值
             */
            WorldPacket const* Write() override;

            int32 MapID = 0;   ///< 地图ID,标识当前地图实例
            int32 ZoneID = 0;  ///< 区域ID,标识当前区域(如暴风城、奥格瑞玛等)
            int32 AreaID = 0;  ///< 区域ID,更细粒度的区域标识(如区域内的子区域)

            std::vector<WorldStateInfo> Worldstates;  ///< 世界状态变量列表,存储所有需要同步的状态
        };

        /**
         * @class UpdateWorldState
         * @brief 更新世界状态数据包
         *
         * 此数据包用于在游戏运行过程中更新单个世界状态变量。
         * 当某个世界状态发生变化时,服务器只需发送此包通知客户端,
         * 而不需要重新发送完整的世界状态列表。
         *
         * 继承自 ServerPacket,表示这是一个服务器下发的数据包。
         *
         * 调用时机:
         * - 战场中某阵营占领资源点时
         * - 冬拥湖中攻城武器数量变化时
         * - 任何世界状态变量值发生变化时
         *
         * 性能注意事项:
         * - 此包非常轻量(仅8字节),可以频繁发送
         * - 相比 InitWorldStates,更适合实时更新场景
         */
        class UpdateWorldState final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化数据包,设置操作码为 SMSG_UPDATE_WORLD_STATE,
             * 并预分配包大小(变量ID + 变量值)。
             */
            UpdateWorldState() : ServerPacket(SMSG_UPDATE_WORLD_STATE, 4 + 4) { }

            /**
             * @brief 序列化数据包
             * @return 返回序列化后的 WorldPacket 指针
             *
             * 将更新信息序列化为网络数据包格式:
             * - int32: 变量ID
             * - int32: 新值
             *
             * 客户端收到后会更新对应的UI显示。
             */
            WorldPacket const* Write() override;

            int32 VariableID = 0;  ///< 要更新的世界状态变量ID
            int32 Value = 0;       ///< 变量的新值
        };
    }
}

#endif // WorldStatePackets_h__
