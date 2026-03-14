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
 * @file CombatPackets.h
 * @brief 战斗系统网络包定义模块
 *
 * 本模块定义了战斗系统相关的所有网络消息包结构,包括:
 * - 近战攻击的启动、停止和错误处理
 * - 武器收起状态的同步
 * - 战斗状态取消和自动攻击取消
 *
 * 这些数据包用于客户端与服务器之间的战斗状态同步和交互,
 * 是玩家近战攻击系统的核心通信协议。
 */

#ifndef CombatPackets_h__
#define CombatPackets_h__

#include "Packet.h"
#include "ObjectGuid.h"

class Unit;

namespace WorldPackets
{
    /**
     * @namespace Combat
     * @brief 战斗系统网络包命名空间
     *
     * 包含所有战斗相关的客户端和服务器消息包定义,
     * 涵盖近战攻击、战斗状态、武器收起等功能。
     */
    namespace Combat
    {
        /**
         * @class AttackSwing
         * @brief 客户端发起近战攻击请求的数据包
         *
         * 当玩家右键点击攻击目标或使用攻击技能时,客户端发送此消息包,
         * 请求服务器开始近战自动攻击。
         *
         * 继承自 ClientPacket,表示这是一个客户端到服务器的消息。
         */
        class AttackSwing final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            AttackSwing(WorldPacket&& packet) : ClientPacket(CMSG_ATTACK_SWING, std::move(packet)) { }

            /**
             * @brief 从网络包中读取攻击目标 GUID
             *
             * 从接收到的数据包中解析出攻击目标的 GUID 信息。
             */
            void Read() override;

            ObjectGuid Victim;  ///< 攻击目标的唯一标识符
        };

        /**
         * @class AttackSwingNotInRange
         * @brief 服务器通知客户端攻击失败 - 目标不在攻击范围内
         *
         * 当玩家尝试攻击一个超出近战攻击范围的目标时,
         * 服务器发送此消息通知客户端攻击失败。
         *
         * 继承自 ServerPacket,表示这是一个服务器到客户端的消息。
         * 这是一个空包,没有额外数据,仅通过操作码传递失败原因。
         */
        class AttackSwingNotInRange final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,指定操作码和数据大小为 0。
             */
            AttackSwingNotInRange() : ServerPacket(SMSG_ATTACK_SWING_NOT_IN_RANGE, 0) { }

            /**
             * @brief 序列化数据包
             * @return 返回只包含操作码的空数据包指针
             *
             * 由于此消息无需携带额外数据,直接返回空的数据包。
             */
            WorldPacket const* Write() override { return &_worldPacket; }
        };

        /**
         * @class AttackSwingBadFacing
         * @brief 服务器通知客户端攻击失败 - 目标不在面对方向
         *
         * 当玩家尝试攻击一个不在自己面对方向的目标时,
         * 服务器发送此消息通知客户端攻击失败。
         * 角色必须面对目标才能进行近战攻击。
         *
         * 继承自 ServerPacket,表示这是一个服务器到客户端的消息。
         * 这是一个空包,没有额外数据。
         */
        class AttackSwingBadFacing final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,指定操作码和数据大小为 0。
             */
            AttackSwingBadFacing() : ServerPacket(SMSG_ATTACK_SWING_BAD_FACING, 0) { }

            /**
             * @brief 序列化数据包
             * @return 返回只包含操作码的空数据包指针
             */
            WorldPacket const* Write() override { return &_worldPacket; }
        };

        /**
         * @class AttackSwingDeadTarget
         * @brief 服务器通知客户端攻击失败 - 目标已死亡
         *
         * 当玩家尝试攻击一个已经死亡的目标时,
         * 服务器发送此消息通知客户端攻击失败。
         *
         * 继承自 ServerPacket,表示这是一个服务器到客户端的消息。
         * 这是一个空包,没有额外数据。
         */
        class AttackSwingDeadTarget final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,指定操作码和数据大小为 0。
             */
            AttackSwingDeadTarget() : ServerPacket(SMSG_ATTACK_SWING_DEAD_TARGET, 0) { }

            /**
             * @brief 序列化数据包
             * @return 返回只包含操作码的空数据包指针
             */
            WorldPacket const* Write() override { return &_worldPacket; }
        };

        /**
         * @class AttackSwingCantAttack
         * @brief 服务器通知客户端攻击失败 - 无法攻击目标
         *
         * 当玩家尝试攻击一个无法被攻击的目标时(例如友好单位、无敌状态等),
         * 服务器发送此消息通知客户端攻击失败。
         *
         * 继承自 ServerPacket,表示这是一个服务器到客户端的消息。
         * 这是一个空包,没有额外数据。
         */
        class AttackSwingCantAttack final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,指定操作码和数据大小为 0。
             */
            AttackSwingCantAttack() : ServerPacket(SMSG_ATTACK_SWING_CANT_ATTACK, 0) { }

            /**
             * @brief 序列化数据包
             * @return 返回只包含操作码的空数据包指针
             */
            WorldPacket const* Write() override { return &_worldPacket; }
        };

        /**
         * @class AttackStop
         * @brief 客户端请求停止近战攻击的数据包
         *
         * 当玩家主动取消攻击(例如使用 Escape 键、切换目标或移动)时,
         * 客户端发送此消息请求服务器停止自动攻击。
         *
         * 继承自 ClientPacket,表示这是一个客户端到服务器的消息。
         * 这是一个空包,没有额外数据,仅通过操作码传递停止攻击的意图。
         */
        class AttackStop final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            AttackStop(WorldPacket&& packet) : ClientPacket(CMSG_ATTACK_STOP, std::move(packet)) { }

            /**
             * @brief 从网络包中读取数据
             *
             * 由于此消息不携带额外数据,Read() 方法为空实现。
             */
            void Read() override { }
        };

        /**
         * @class AttackStart
         * @brief 服务器通知客户端攻击开始的数据包
         *
         * 当攻击者开始攻击某个目标时,服务器广播此消息给周围玩家,
         * 用于同步战斗状态的开始。例如,当怪物开始攻击玩家时,
         * 玩家客户端会收到此消息。
         *
         * 继承自 ServerPacket,表示这是一个服务器到客户端的消息。
         * 包含攻击者和目标的完整 GUID。
         */
        class AttackStart final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,指定操作码和预估数据大小(16字节,两个 GUID)。
             */
            AttackStart() : ServerPacket(SMSG_ATTACK_START, 8 + 8) { }

            /**
             * @brief 序列化数据包
             * @return 返回序列化后的数据包指针
             *
             * 将攻击者和目标的 GUID 写入数据包。
             */
            WorldPacket const* Write() override;

            ObjectGuid Attacker;  ///< 发起攻击的单位 GUID
            ObjectGuid Victim;    ///< 被攻击的目标 GUID
        };

        /**
         * @class SAttackStop
         * @brief 服务器通知客户端攻击停止的数据包
         *
         * 当攻击者停止攻击某个目标时,服务器广播此消息给周围玩家,
         * 用于同步战斗状态的结束。包含攻击者和目标信息,
         * 以及目标是否已经死亡的标志。
         *
         * 继承自 ServerPacket,表示这是一个服务器到客户端的消息。
         * 使用 PackedGuid 优化网络传输大小。
         *
         * @note 与 AttackStop 不同,这是服务器发送的消息包,
         *       而 AttackStop 是客户端发送的请求包。
         */
        class SAttackStop final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化服务器数据包,指定操作码和预估数据大小。
             */
            SAttackStop() : ServerPacket(SMSG_ATTACK_STOP, 8 + 8 + 4) { }

            /**
             * @brief 便捷构造函数
             * @param attacker 攻击者单位指针
             * @param victim 被攻击目标单位指针(可为空)
             *
             * 从 Unit 对象直接构造数据包,自动提取 GUID 和死亡状态。
             * 这是推荐的构造方式,可避免手动设置成员变量。
             */
            SAttackStop(Unit const* attacker, Unit const* victim);

            /**
             * @brief 序列化数据包
             * @return 返回序列化后的数据包指针
             *
             * 将攻击者 GUID、目标 GUID 和死亡标志写入数据包。
             */
            WorldPacket const* Write() override;

            PackedGuid Attacker;     ///< 攻击者的压缩 GUID
            PackedGuid Victim;       ///< 目标的压缩 GUID
            bool NowDead = false;    ///< 目标是否已死亡(死亡时客户端播放不同动画)
        };

        /**
         * @class CancelCombat
         * @brief 服务器通知客户端取消战斗状态的数据包
         *
         * 当单位从战斗状态退出时(例如脱离战斗、逃跑等),
         * 服务器发送此消息通知客户端清除战斗状态。
         * 这会移除战斗相关的 UI 元素(如攻击目标框体)。
         *
         * 继承自 ServerPacket,表示这是一个服务器到客户端的消息。
         * 这是一个空包,没有额外数据。
         *
         * @note 与 SAttackStop 不同,此消息表示完全退出战斗状态,
         *       而 SAttackStop 仅表示停止攻击特定目标。
         */
        class CancelCombat final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,指定操作码和数据大小为 0。
             */
            CancelCombat() : ServerPacket(SMSG_CANCEL_COMBAT, 0) { }

            /**
             * @brief 序列化数据包
             * @return 返回只包含操作码的空数据包指针
             */
            WorldPacket const* Write() override { return &_worldPacket; }
        };

        /**
         * @class CancelAutoRepeat
         * @brief 服务器通知客户端取消自动射击的数据包
         *
         * 当自动射击或自动攻击被取消时(例如切换目标、移动、使用技能等),
         * 服务器发送此消息通知客户端停止自动攻击循环。
         *
         * 继承自 ServerPacket,表示这是一个服务器到客户端的消息。
         * 包含取消自动攻击的单位 GUID。
         *
         * @note 这主要用于猎人自动射击等远程自动攻击技能,
         *       近战自动攻击通常使用 SAttackStop 来处理。
         */
        class CancelAutoRepeat final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器数据包,指定操作码和预估数据大小(8字节)。
             */
            CancelAutoRepeat() : ServerPacket(SMSG_CANCEL_AUTO_REPEAT, 8) { }

            /**
             * @brief 序列化数据包
             * @return 返回序列化后的数据包指针
             *
             * 将单位的压缩 GUID 写入数据包。
             */
            WorldPacket const* Write() override;

            PackedGuid Guid;  ///< 取消自动攻击的单位 GUID
        };

        /**
         * @class SetSheathed
         * @brief 客户端请求设置武器收起状态的数据包
         *
         * 当玩家切换武器的收起状态时(例如按下 Z 键或使用快捷键),
         * 客户端发送此消息请求服务器更新武器的显示状态。
         *
         * 武器收起状态包括:
         * - 0: 武器收起状态(在背后/腰间)
         * - 1: 武器装备状态(在手中)
         * - 2: 远程武器状态
         *
         * 继承自 ClientPacket,表示这是一个客户端到服务器的消息。
         */
        class SetSheathed final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             */
            SetSheathed(WorldPacket&& packet) : ClientPacket(CMSG_SET_SHEATHED, std::move(packet)) { }

            /**
             * @brief 从网络包中读取收起状态
             *
             * 从接收到的数据包中解析出武器收起状态值。
             */
            void Read() override;

            uint32 CurrentSheathState = 0;  ///< 当前的武器收起状态(0=收起, 1=装备, 2=远程)
        };
    }
}

#endif // CombatPackets_h__
