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
 * @file CombatLogPackets.h
 * @brief 战斗日志数据包定义模块
 *
 * 本文件定义了战斗日志相关的网络数据包结构,用于服务器向客户端发送战斗日志信息。
 * 主要包含环境伤害日志等战斗相关消息的数据包定义。
 *
 * @module CombatLogPackets
 * @author TrinityCore Team
 */

#ifndef CombatLogPackets_h__
#define CombatLogPackets_h__

#include "Packet.h"
#include "Player.h"

namespace WorldPackets
{
    /**
     * @namespace CombatLog
     * @brief 战斗日志数据包命名空间
     *
     * 包含所有与战斗日志相关的服务器数据包类定义。
     */
    namespace CombatLog
    {
        /**
         * @class EnvironmentalDamageLog
         * @brief 环境伤害日志数据包
         *
         * 用于向客户端发送玩家受到的环境伤害信息,包括疲劳、溺水、摔落、岩浆等环境伤害。
         * 继承自 ServerPacket,表示这是一个服务器向客户端发送的单向数据包。
         *
         * @see ServerPacket 父类,提供数据包基础功能
         * @see EnviromentalDamage 环境伤害类型枚举
         *
         * @note 数据包大小计算:
         *       ObjectGuid(8字节) + Type(1字节) + Amount(4字节) +
         *       Resisted(4字节) + Absorbed(4字节) = 21字节
         *
         * @usage 在 Player::EnvironmentalDamage() 函数中构造并发送此数据包,
         *        当玩家受到环境伤害时通知客户端显示伤害数字和效果
         */
        class EnvironmentalDamageLog final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化环境伤害日志数据包,设置操作码为 SMSG_ENVIRONMENTAL_DAMAGE_LOG,
             * 并预分配 21 字节的缓冲区空间以提高性能。
             *
             * @note 使用 final 关键字表示此类不可被继承
             */
            EnvironmentalDamageLog() : ServerPacket(SMSG_ENVIRONMENTAL_DAMAGE_LOG, 21) { }

            /**
             * @brief 序列化数据包
             *
             * 将环境伤害信息序列化为网络字节流,用于网络传输。
             *
             * @return WorldPacket const* 指向序列化后的数据包对象的指针
             *
             * @note 序列化顺序: Victim -> Type -> Amount -> Resisted -> Absorbed
             * @note 此函数由网络层在发送数据包时自动调用
             */
            WorldPacket const* Write() override;

            ObjectGuid Victim;                      ///< 受害者的全局唯一标识符,指向受到环境伤害的玩家或单位
            EnviromentalDamage Type = DAMAGE_EXHAUSTED;  ///< 环境伤害类型,默认为疲劳伤害,取值见 EnviromentalDamage 枚举
            uint32 Amount = 0;                      ///< 实际伤害数值,表示此次环境伤害造成的原始伤害量
            uint32 Resisted = 0;                    ///< 抵抗的伤害值,表示通过抗性减免的伤害量
            uint32 Absorbed = 0;                    ///< 吸收的伤害值,表示通过护盾或吸收效果减免的伤害量
        };
    }
}

#endif // CombatLogPackets_h__
