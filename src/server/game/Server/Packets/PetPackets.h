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
 * @file PetPackets.h
 * @brief 宠物系统网络包定义模块
 *
 * 本模块定义了宠物系统相关的所有网络数据包结构,包括客户端到服务器(CMSG)和服务器到客户端(SMSG)的消息。
 * 主要功能包括:
 * - 宠物解散、放弃、停止攻击等操作
 * - 宠物法术学习、遗忘、自动施放设置
 * - 小动物(非战斗宠物)的解散操作
 * - 宠物信息查询请求
 *
 * 这些包用于玩家与宠物之间的交互通信,包括猎人宠物、术士宠物、法师水元素等。
 */

#ifndef PetPackets_h__
#define PetPackets_h__

#include "Packet.h"
#include "ObjectGuid.h"

namespace WorldPackets
{
    /**
     * @namespace Pet
     * @brief 宠物相关网络包命名空间
     *
     * 包含所有宠物系统的网络消息定义,涵盖客户端请求和服务器响应。
     */
    namespace Pet
    {
        /**
         * @class DismissCritter
         * @brief 解散小动物包(客户端->服务器)
         *
         * 继承自 ClientPacket,用于处理玩家解散非战斗宠物(小动物)的请求。
         * 当玩家右键点击小动物 buff 图标选择解散时发送此包。
         *
         * 调用时机:
         * - 玩家主动解散小动物时
         * - 取消召唤小动物的法术效果时
         */
        class DismissCritter final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的网络包数据
             */
            DismissCritter(WorldPacket&& packet) : ClientPacket(CMSG_DISMISS_CRITTER, std::move(packet)) { }

            /**
             * @brief 读取包数据
             *
             * 从网络包中解析小动物的 GUID。
             * 性能注意: 仅读取 8 字节的 GUID 数据,性能开销极小。
             */
            void Read() override;

            ObjectGuid CritterGUID;  ///< 要解散的小动物的全局唯一标识符
        };

        /**
         * @class PetAbandon
         * @brief 放弃宠物包(客户端->服务器)
         *
         * 继承自 ClientPacket,用于处理玩家永久放弃宠物的请求。
         * 主要用于猎人宠物,放弃后宠物将从玩家的宠物列表中永久移除。
         *
         * 调用时机:
         * - 猎人在宠物管理界面选择"放弃宠物"时
         * - 其他拥有永久宠物的职业执行放弃操作时
         *
         * 注意: 此操作不可逆,宠物将永久消失。
         */
        class PetAbandon final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的网络包数据
             */
            PetAbandon(WorldPacket&& packet) : ClientPacket(CMSG_PET_ABANDON, std::move(packet)) { }

            /**
             * @brief 读取包数据
             *
             * 从网络包中解析宠物的 GUID。
             * 性能注意: 仅读取 8 字节的 GUID 数据,性能开销极小。
             */
            void Read() override;

            ObjectGuid PetGUID;  ///< 要放弃的宠物的全局唯一标识符
        };

        /**
         * @class PetStopAttack
         * @brief 宠物停止攻击包(客户端->服务器)
         *
         * 继承自 ClientPacket,用于处理玩家命令宠物停止攻击的请求。
         * 宠物将停止当前的战斗行为并返回到跟随主人状态。
         *
         * 调用时机:
         * - 玩家按下"宠物停止攻击"快捷键或点击相应按钮时
         * - 玩家在宠物动作栏选择停止攻击命令时
         *
         * 效果:
         * - 宠物停止攻击当前目标
         * - 如果宠物在追击,会停止移动
         * - 宠物不会自动切换到其他目标
         */
        class PetStopAttack final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的网络包数据
             */
            PetStopAttack(WorldPacket&& packet) : ClientPacket(CMSG_PET_STOP_ATTACK, std::move(packet)) { }

            /**
             * @brief 读取包数据
             *
             * 从网络包中解析宠物的 GUID。
             * 性能注意: 仅读取 8 字节的 GUID 数据,性能开销极小。
             */
            void Read() override;

            ObjectGuid PetGUID;  ///< 要停止攻击的宠物的全局唯一标识符
        };

        /**
         * @class PetSpellAutocast
         * @brief 宠物法术自动施放设置包(客户端->服务器)
         *
         * 继承自 ClientPacket,用于处理玩家切换宠物法术自动施放状态的请求。
         * 玩家可以在宠物法术书中开启或关闭某个法术的自动施放功能。
         *
         * 调用时机:
         * - 玩家在宠物法术书界面右键点击法术图标时
         * - 玩家通过快捷键切换宠物法术自动施放时
         *
         * 功能说明:
         * - 开启自动施放后,宠物会根据 AI 逻辑自动使用该法术
         * - 关闭后,法术只能通过手动命令来施放
         * - 并非所有宠物法术都支持自动施放
         */
        class PetSpellAutocast final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的网络包数据
             */
            PetSpellAutocast(WorldPacket&& packet) : ClientPacket(CMSG_PET_SPELL_AUTOCAST, std::move(packet)) { }

            /**
             * @brief 读取包数据
             *
             * 从网络包中依次解析宠物 GUID、法术 ID 和自动施放状态。
             * 性能注意: 读取约 13 字节数据(GUID 8字节 + SpellID 4字节 + bool 1字节),性能开销小。
             */
            void Read() override;

            ObjectGuid PetGUID;         ///< 宠物的全局唯一标识符
            uint32 SpellID = 0;         ///< 要设置的宠物法术 ID
            bool AutocastEnabled = false;  ///< 是否启用自动施放,true 为开启,false 为关闭
        };

        /**
         * @class PetLearnedSpell
         * @brief 宠物学习法术通知包(服务器->客户端)
         *
         * 继承自 ServerPacket,用于通知客户端宠物学会了新的法术。
         * 客户端收到此包后会在宠物法术书中添加新法术的图标。
         *
         * 调用时机:
         * - 宠物升级学到新法术时
         * - 通过训练师教会宠物新技能时
         * - 使用技能书或道具让宠物学习法术时
         *
         * 效果:
         * - 客户端在宠物法术书中添加新法术
         * - 如果法术支持自动施放,默认可能处于开启状态
         * - 可能触发相应的视觉效果和音效
         */
        class PetLearnedSpell final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器包,设置包类型为 SMSG_PET_LEARNED_SPELL,预留 4 字节空间。
             */
            PetLearnedSpell() : ServerPacket(SMSG_PET_LEARNED_SPELL, 4) { }

            /**
             * @brief 写入包数据
             * @return 返回写入完成后的网络包指针
             *
             * 将法术 ID 写入网络包。
             * 性能注意: 仅写入 4 字节数据,性能开销极小。
             */
            WorldPacket const* Write() override;

            uint32 SpellID = 0;  ///< 宠物新学会的法术 ID
        };

        /**
         * @class PetUnlearnedSpell
         * @brief 宠物遗忘法术通知包(服务器->客户端)
         *
         * 继承自 ServerPacket,用于通知客户端宠物遗忘了某个法术。
         * 客户端收到此包后会在宠物法术书中移除对应的法术图标。
         *
         * 调用时机:
         * - 宠物被训练师重置天赋/技能时
         * - 特殊机制导致宠物遗忘法术时
         * - 某些宠物变形或状态改变导致法术丢失时
         *
         * 效果:
         * - 客户端从宠物法术书中移除指定法术
         * - 如果该法术正在自动施放,会停止自动施放
         * - 如果宠物正在施放该法术,会打断施法
         */
        class PetUnlearnedSpell final : public ServerPacket
        {
        public:
            /**
             * @brief 构造函数
             *
             * 初始化服务器包,设置包类型为 SMSG_PET_UNLEARNED_SPELL,预留 4 字节空间。
             */
            PetUnlearnedSpell() : ServerPacket(SMSG_PET_UNLEARNED_SPELL, 4) { }

            /**
             * @brief 写入包数据
             * @return 返回写入完成后的网络包指针
             *
             * 将法术 ID 写入网络包。
             * 性能注意: 仅写入 4 字节数据,性能开销极小。
             */
            WorldPacket const* Write() override;

            uint32 SpellID = 0;  ///< 宠物遗忘的法术 ID
        };

        /**
         * @class RequestPetInfo
         * @brief 请求宠物信息包(客户端->服务器)
         *
         * 继承自 ClientPacket,用于客户端主动请求宠物的详细信息。
         * 服务器收到此请求后会发送完整的宠物状态数据。
         *
         * 调用时机:
         * - 玩家登录后首次查看宠物信息时
         * - 某些特殊情况下客户端需要刷新宠物数据时
         * - UI 界面需要同步宠物状态时
         *
         * 特点:
         * - 此包不包含任何数据字段
         * - 仅作为触发信号,通知服务器发送宠物详细信息
         * - 服务器会通过其他包(如 PetNameQuery、PetStats 等)响应
         */
        class RequestPetInfo final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的网络包数据
             */
            RequestPetInfo(WorldPacket&& packet) : ClientPacket(CMSG_REQUEST_PET_INFO, std::move(packet)) { }

            /**
             * @brief 读取包数据
             *
             * 空实现,因为此包不包含任何数据字段。
             * 仅作为请求信号使用。
             */
            void Read() override { }
        };
    }
}

#endif // PetPackets_h__
