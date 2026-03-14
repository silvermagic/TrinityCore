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
 * @file SpellPackets.h
 * @brief 法术系统网络数据包定义
 *
 * 本文件定义了法术系统相关的所有网络数据包结构，包括：
 * - 客户端发送的法术取消、光环移除等请求包
 * - 服务器发送的法术施法、引导、符文同步等响应包
 * - 法术目标数据、弹道轨迹、弹药信息等中间数据结构
 *
 * 这些数据包负责客户端与服务器之间的法术相关通信，
 * 涵盖了施法开始、施法执行、施法取消、光环管理、引导法术等核心功能。
 */

#ifndef SpellPackets_h__
#define SpellPackets_h__

#include "Packet.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "Position.h"
#include "SharedDefines.h"

namespace WorldPackets
{
    namespace Spells
    {
        /**
         * @class CancelCast
         * @brief 取消施法请求包
         *
         * 客户端发送此包请求取消正在进行的施法。
         * 当玩家主动取消施法（移动、按ESC键等）时发送。
         * 继承自 ClientPacket。
         */
        class CancelCast final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            CancelCast(WorldPacket&& packet) : ClientPacket(CMSG_CANCEL_CAST, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络包中解析施法ID和法术ID
             */
            void Read() override;

            uint8 CastID = 0;      ///< 客户端施法标识符，用于匹配客户端和服务器的施法请求
            uint32 SpellID = 0;    ///< 要取消的法术ID
        };

        /**
         * @class CancelAura
         * @brief 取消光环请求包
         *
         * 客户端发送此包请求移除自身的一个有益或有害光环（Buff/Debuff）。
         * 当玩家右键点击光环图标移除时发送。
         * 继承自 ClientPacket。
         */
        class CancelAura final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            CancelAura(WorldPacket&& packet) : ClientPacket(CMSG_CANCEL_AURA, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络包中解析要移除的法术ID
             */
            void Read() override;

            uint32 SpellID = 0;    ///< 要取消的光环对应的法术ID
        };

        /**
         * @class PetCancelAura
         * @brief 宠物取消光环请求包
         *
         * 客户端发送此包请求移除宠物身上的光环。
         * 当玩家在宠物栏中取消宠物的光环时发送。
         * 继承自 ClientPacket。
         */
        class PetCancelAura final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            PetCancelAura(WorldPacket&& packet) : ClientPacket(CMSG_PET_CANCEL_AURA, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络包中解析宠物GUID和法术ID
             */
            void Read() override;

            ObjectGuid PetGUID;     ///< 宠物的全局唯一标识符
            uint32 SpellID = 0;     ///< 要取消的光环对应的法术ID
        };

        /**
         * @class CancelGrowthAura
         * @brief 取消成长光环请求包
         *
         * 客户端发送此包请求取消成长类光环效果。
         * 用于移除特定的体型增大类法术效果。
         * 继承自 ClientPacket。
         */
        class CancelGrowthAura final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            CancelGrowthAura(WorldPacket&& packet) : ClientPacket(CMSG_CANCEL_GROWTH_AURA, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 此包不包含额外数据
             */
            void Read() override { }
        };

        /**
         * @class CancelMountAura
         * @brief 取消坐骑光环请求包
         *
         * 客户端发送此包请求取消坐骑状态。
         * 当玩家主动下马时发送（不需要额外参数）。
         * 继承自 ClientPacket。
         */
        class CancelMountAura final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            CancelMountAura(WorldPacket&& packet) : ClientPacket(CMSG_CANCEL_MOUNT_AURA, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 此包不包含额外数据
             */
            void Read() override { }
        };

        /**
         * @class CancelAutoRepeatSpell
         * @brief 取消自动重复施法请求包
         *
         * 客户端发送此包请求停止自动重复施法模式。
         * 用于停止如自动射击等持续施法的技能。
         * 继承自 ClientPacket。
         */
        class CancelAutoRepeatSpell final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            CancelAutoRepeatSpell(WorldPacket&& packet) : ClientPacket(CMSG_CANCEL_AUTO_REPEAT_SPELL, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 此包不包含额外数据
             */
            void Read() override { }
        };

        /**
         * @class CancelChannelling
         * @brief 取消引导法术请求包
         *
         * 客户端发送此包请求取消正在引导的法术。
         * 引导法术包括暴风雪、奥术飞弹等需要持续施放的法术。
         * 继承自 ClientPacket。
         */
        class CancelChannelling final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 世界包数据
             */
            CancelChannelling(WorldPacket&& packet) : ClientPacket(CMSG_CANCEL_CHANNELLING, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络包中解析引导法术ID
             */
            void Read() override;

            uint32 ChannelSpell = 0;    ///< 要取消的引导法术ID
        };

        /**
         * @struct SpellMissStatus
         * @brief 法术未命中状态信息
         *
         * 记录法术施放时未命中目标的详细状态，
         * 包括未命中原因和反射状态（如果是反射）。
         */
        struct SpellMissStatus
        {
            ObjectGuid TargetGUID;       ///< 目标的全局唯一标识符
            uint8 Reason = 0;            ///< 未命中原因（SPELL_MISS_*枚举值）
            uint8 ReflectStatus = 0;     ///< 反射状态（仅当Reason为SPELL_MISS_REFLECT时有效）
        };

        /**
         * @struct RuneData
         * @brief 符文数据结构
         *
         * 死亡骑士职业符文系统的冷却数据，
         * 用于向客户端同步符文状态。
         */
        struct RuneData
        {
            uint8 Start = 0;                    ///< 起始符文索引
            uint8 Count = 0;                    ///< 符文数量
            std::vector<uint8> Cooldowns;       ///< 各符文的冷却时间列表
        };

        /**
         * @struct MissileTrajectoryResult
         * @brief 导弹轨迹结果数据
         *
         * 记录法术导弹的弹道信息，
         * 包括飞行时间和发射仰角。
         */
        struct MissileTrajectoryResult
        {
            uint32 TravelTime = 0;      ///< 飞行时间（毫秒）
            float Pitch = 0.0f;         ///< 发射仰角（弧度）
        };

        /**
         * @struct SpellAmmo
         * @brief 法术弹药显示数据
         *
         * 定义法术施放时使用的弹药（箭矢、子弹等）的显示信息，
         * 用于客户端正确显示射击类法术的视觉效果。
         */
        struct SpellAmmo
        {
            uint32 DisplayID = 0;          ///< 弹药显示模型ID
            uint32 InventoryType = 0;      ///< 背包槽位类型（INVTYPE_*枚举值）
        };

        /**
         * @struct CreatureImmunities
         * @brief 生物免疫信息
         *
         * 记录生物对特定法术类型的免疫属性，
         * 用于客户端显示免疫状态。
         */
        struct CreatureImmunities
        {
            uint32 School = 0;     ///< 法术类型掩码（SPELL_SCHOOL_*枚举值）
            uint32 Value = 0;      ///< 免疫值/免疫类型
        };

        /**
         * @struct TargetLocation
         * @brief 目标位置数据
         *
         * 定义法术目标的地理位置信息，
         * 可以是世界坐标或相对于载具的坐标。
         */
        struct TargetLocation
        {
            ObjectGuid Transport;      ///< 载具GUID（如果目标在载具上，否则为空）
            Position Location;         ///< 目标位置坐标（x, y, z, orientation）
        };

        /**
         * @struct SpellTargetData
         * @brief 法术目标数据结构
         *
         * 完整描述法术施放时的所有目标信息，
         * 包括单位目标、物品目标、源位置和目标位置等。
         * 根据目标标志的不同，包含不同的目标数据。
         */
        struct SpellTargetData
        {
            uint32 Flags = 0;                      ///< 目标标志位（TARGET_FLAG_*枚举值）
            Optional<ObjectGuid> Unit;             ///< 单位目标GUID（玩家、NPC等）
            Optional<ObjectGuid> Item;             ///< 物品目标GUID
            Optional<TargetLocation> SrcLocation;  ///< 源位置（用于某些需要源位置的法术）
            Optional<TargetLocation> DstLocation;  ///< 目标位置（用于区域法术）
            Optional<std::string> Name;            ///< 目标名称（用于某些特殊法术）
        };

        /**
         * @struct SpellCastData
         * @brief 法术施放完整数据结构
         *
         * 包含法术施放过程中的所有必要信息，
         * 用于 SpellStart（施法开始）和 SpellGo（施法完成）数据包。
         * 这是法术系统的核心数据结构之一。
         */
        struct SpellCastData
        {
            ObjectGuid CasterGUID;                                    ///< 施法者GUID（可能是物品、游戏对象等）
            ObjectGuid CasterUnit;                                    ///< 施法单位GUID（实际施法的单位）
            uint8 CastID = 0;                                         ///< 客户端施法标识符
            uint32 SpellID = 0;                                       ///< 法术ID
            uint32 CastFlags = 0;                                     ///< 施法标志位（CAST_FLAG_*枚举值）
            uint32 CastTime = 0;                                      ///< 施法时间（毫秒）
            mutable Optional<std::vector<ObjectGuid>> HitTargets;    ///< 命中的目标列表（mutable允许在Write时修改）
            mutable Optional<std::vector<SpellMissStatus>> MissStatus; ///< 未命中目标的状态列表
            SpellTargetData Target;                                   ///< 目标数据
            Optional<uint32> RemainingPower;                          ///< 剩余能量值（用于显示）
            Optional<RuneData> RemainingRunes;                        ///< 剩余符文数据（死亡骑士专用）
            Optional<MissileTrajectoryResult> MissileTrajectory;      ///< 导弹轨迹数据（用于投射物法术）
            Optional<SpellAmmo> Ammo;                                 ///< 弹药显示数据（用于射击类法术）
            Optional<CreatureImmunities> Immunities;                  ///< 免疫信息（某些特殊法术需要）
        };

        /**
         * @class SpellGo
         * @brief 法术施放完成数据包
         *
         * 服务器发送此包通知客户端法术施放已完成。
         * 包含完整的施法数据和所有目标信息（命中和未命中）。
         * 这是法术系统的核心服务器包之一。
         * 继承自 ServerPacket。
         */
        class SpellGo final : public ServerPacket
        {
            public:
                /**
                 * @brief 构造函数
                 *
                 * 初始化服务器包并预分配命中目标和未命中状态列表
                 */
                SpellGo() : ServerPacket(SMSG_SPELL_GO)
                {
                    Cast.HitTargets.emplace();
                    Cast.MissStatus.emplace();
                }

                /**
                 * @brief 写入数据包内容
                 * @return 写入完成后的世界包指针
                 *
                 * 将施法数据序列化到网络包中发送给客户端
                 */
                WorldPacket const* Write() override;

                SpellCastData Cast;     ///< 施法数据
        };

        /**
         * @class SpellStart
         * @brief 法术施法开始数据包
         *
         * 服务器发送此包通知客户端法术施法已开始。
         * 用于需要施法时间的法术，让客户端显示施法条和动画。
         * 继承自 ServerPacket。
         */
        class SpellStart final : public ServerPacket
        {
            public:
                /**
                 * @brief 构造函数
                 */
                SpellStart() : ServerPacket(SMSG_SPELL_START) { }

                /**
                 * @brief 写入数据包内容
                 * @return 写入完成后的世界包指针
                 *
                 * 将施法数据序列化到网络包中发送给客户端
                 */
                WorldPacket const* Write() override;

                SpellCastData Cast;     ///< 施法数据
        };

        /**
         * @struct ResyncRune
         * @brief 符文重同步数据
         *
         * 单个符文的状态信息，
         * 用于向客户端同步死亡骑士的符文状态。
         */
        struct ResyncRune
        {
            uint8 RuneType = 0;     ///< 符文类型（血、冰、邪等）
            uint8 Cooldown = 0;     ///< 冷却时间
        };

        /**
         * @class ResyncRunes
         * @brief 符文重同步数据包
         *
         * 服务器发送此包向客户端同步死亡骑士的所有符文状态。
         * 当符文状态需要完全更新时发送（如登录、重置等）。
         * 继承自 ServerPacket。
         */
        class ResyncRunes final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 预分配包大小：4字节计数 + 2字节 * MAX_RUNES
             */
            ResyncRunes() : ServerPacket(SMSG_RESYNC_RUNES, 4 + 2 * MAX_RUNES) { }

            /**
             * @brief 写入数据包内容
             * @return 写入完成后的世界包指针
             *
             * 将所有符文数据序列化到网络包中
             */
            WorldPacket const* Write() override;

            uint32 Count = 0;                   ///< 符文数量
            std::vector<ResyncRune> Runes;      ///< 符文列表
        };

        /**
         * @class MountResult
         * @brief 坐骑结果数据包
         *
         * 服务器发送此包通知客户端坐骑操作的结果。
         * 当玩家尝试召唤坐骑时，服务器返回成功或失败状态。
         * 继承自 ServerPacket。
         */
        class MountResult final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 预分配包大小：4字节结果值
             */
            MountResult() : ServerPacket(SMSG_MOUNT_RESULT, 4) { }

            /**
             * @brief 写入数据包内容
             * @return 写入完成后的世界包指针
             *
             * 将坐骑结果序列化到网络包中
             */
            WorldPacket const* Write() override;

            uint32 Result = 0;     ///< 坐骑结果码（成功/失败原因）
        };
    }
}

#endif // SpellPackets_h__
