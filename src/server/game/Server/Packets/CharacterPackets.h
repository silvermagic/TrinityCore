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
 * @file CharacterPackets.h
 * @brief 角色相关网络数据包定义
 *
 * 本文件定义了角色管理相关的客户端和服务器网络数据包结构,包括:
 * - 外观显示控制(头盔、披风显示/隐藏)
 * - 登录世界验证
 * - 登出流程(请求、响应、完成、取消)
 * - 游戏时间查询
 *
 * 这些数据包类用于客户端与服务器之间的角色状态同步和控制。
 * 所有类都继承自基础数据包类(ClientPacket或ServerPacket)。
 */

#ifndef CharacterPackets_h__
#define CharacterPackets_h__

#include "Packet.h"
#include "Position.h"

namespace WorldPackets
{
    /**
     * @namespace Character
     * @brief 角色相关数据包命名空间
     *
     * 包含所有角色管理相关的网络数据包类定义
     */
    namespace Character
    {
        /**
         * @class ShowingCloak
         * @brief 披风显示状态数据包(客户端->服务器)
         *
         * 用于客户端切换角色披风的显示/隐藏状态
         * 继承自ClientPacket,表示这是一个客户端发送的数据包
         */
        class ShowingCloak final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             *
             * 初始化数据包类型为CMSG_SHOWING_CLOAK(客户端披风显示消息)
             */
            ShowingCloak(WorldPacket&& packet) : ClientPacket(CMSG_SHOWING_CLOAK, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络数据包中读取披风显示状态标志
             * 调用时机:当客户端发送切换披风显示状态的请求时
             */
            void Read() override;

            bool ShowCloak = false;  ///< 是否显示披风,true为显示,false为隐藏
        };

        /**
         * @class ShowingHelm
         * @brief 头盔显示状态数据包(客户端->服务器)
         *
         * 用于客户端切换角色头盔的显示/隐藏状态
         * 继承自ClientPacket,表示这是一个客户端发送的数据包
         */
        class ShowingHelm final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             *
             * 初始化数据包类型为CMSG_SHOWING_HELM(客户端头盔显示消息)
             */
            ShowingHelm(WorldPacket&& packet) : ClientPacket(CMSG_SHOWING_HELM, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络数据包中读取头盔显示状态标志
             * 调用时机:当客户端发送切换头盔显示状态的请求时
             */
            void Read() override;

            bool ShowHelm = false;  ///< 是否显示头盔,true为显示,false为隐藏
        };

        /**
         * @class LoginVerifyWorld
         * @brief 登录世界验证数据包(服务器->客户端)
         *
         * 服务器在角色登录时发送此数据包,通知客户端当前所处的地图和位置
         * 继承自ServerPacket,表示这是一个服务器发送的数据包
         * 数据包大小:4字节(地图ID) + 16字节(位置坐标XYZ + 朝向O) = 20字节
         */
        class LoginVerifyWorld final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包类型为SMSG_LOGIN_VERIFY_WORLD,预分配20字节缓冲区
             */
            LoginVerifyWorld() : ServerPacket(SMSG_LOGIN_VERIFY_WORLD, 4 + 4 * 4) { }

            /**
             * @brief 序列化数据包内容
             * @return 返回序列化后的数据包指针
             *
             * 将地图ID和位置信息写入数据包缓冲区
             * 调用时机:角色登录进入游戏世界时
             * 性能注意:数据包大小固定,无需动态扩容
             */
            WorldPacket const* Write() override;

            int32 MapID = -1;                          ///< 当前地图ID,-1表示无效地图
            TaggedPosition<Position::XYZO> Pos;        ///< 角色位置(包含X、Y、Z坐标和朝向O)
        };

        /**
         * @class LogoutRequest
         * @brief 登出请求数据包(客户端->服务器)
         *
         * 客户端发送此数据包请求登出游戏
         * 继承自ClientPacket,表示这是一个客户端发送的数据包
         * 注意:此数据包不包含任何数据,只是一个信号
         */
        class LogoutRequest final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             *
             * 根据数据包头部自动确定数据包类型
             */
            LogoutRequest(WorldPacket&& packet) : ClientPacket(std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 空实现,因为此数据包不包含任何负载数据
             * 调用时机:当玩家点击"退出游戏"或使用/logout命令时
             */
            void Read() override { }
        };

        /**
         * @class LogoutResponse
         * @brief 登出响应数据包(服务器->客户端)
         *
         * 服务器响应客户端的登出请求,告知登出结果和是否立即登出
         * 继承自ServerPacket,表示这是一个服务器发送的数据包
         * 数据包大小:4字节(结果码) + 1字节(是否立即) = 5字节
         */
        class LogoutResponse final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包类型为SMSG_LOGOUT_RESPONSE,预分配5字节缓冲区
             */
            LogoutResponse() : ServerPacket(SMSG_LOGOUT_RESPONSE, 4 + 1) { }

            /**
             * @brief 序列化数据包内容
             * @return 返回序列化后的数据包指针
             *
             * 将登出结果码和立即登出标志写入数据包缓冲区
             * 调用时机:服务器处理完登出请求后响应客户端
             * 性能注意:数据包大小固定,无需动态扩容
             */
            WorldPacket const* Write() override;

            uint32 LogoutResult = 0;    ///< 登出结果码,0表示成功,非0表示错误码
            bool Instant = false;       ///< 是否立即登出,true表示无需等待20秒倒计时
        };

        /**
         * @class LogoutComplete
         * @brief 登出完成数据包(服务器->客户端)
         *
         * 服务器通知客户端登出流程已完成,客户端可以安全退出
         * 继承自ServerPacket,表示这是一个服务器发送的数据包
         * 注意:此数据包不包含任何数据,只是一个确认信号
         */
        class LogoutComplete final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包类型为SMSG_LOGOUT_COMPLETE,无负载数据
             */
            LogoutComplete() : ServerPacket(SMSG_LOGOUT_COMPLETE, 0) { }

            /**
             * @brief 序列化数据包内容
             * @return 返回序列化后的数据包指针
             *
             * 空实现,直接返回空数据包
             * 调用时机:登出倒计时结束或立即登出时,通知客户端可以退出
             */
            WorldPacket const* Write() override { return &_worldPacket; }
        };

        /**
         * @class LogoutCancel
         * @brief 取消登出请求数据包(客户端->服务器)
         *
         * 客户端发送此数据包取消正在进行的登出流程
         * 继承自ClientPacket,表示这是一个客户端发送的数据包
         * 注意:此数据包不包含任何数据,只是一个信号
         */
        class LogoutCancel final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             *
             * 根据数据包头部自动确定数据包类型
             */
            LogoutCancel(WorldPacket&& packet) : ClientPacket(std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 空实现,因为此数据包不包含任何负载数据
             * 调用时机:玩家在登出倒计时期间移动、攻击或主动取消登出时
             */
            void Read() override { }
        };

        /**
         * @class LogoutCancelAck
         * @brief 取消登出确认数据包(服务器->客户端)
         *
         * 服务器确认客户端的取消登出请求,告知登出流程已中止
         * 继承自ServerPacket,表示这是一个服务器发送的数据包
         * 注意:此数据包不包含任何数据,只是一个确认信号
         */
        class LogoutCancelAck final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包类型为SMSG_LOGOUT_CANCEL_ACK,无负载数据
             */
            LogoutCancelAck() : ServerPacket(SMSG_LOGOUT_CANCEL_ACK, 0) { }

            /**
             * @brief 序列化数据包内容
             * @return 返回序列化后的数据包指针
             *
             * 空实现,直接返回空数据包
             * 调用时机:服务器成功取消登出流程后通知客户端
             */
            WorldPacket const* Write() override { return &_worldPacket; }
        };

        /**
         * @class PlayerLogout
         * @brief 玩家登出数据包(客户端->服务器)
         *
         * 客户端发送此数据包通知服务器玩家即将登出
         * 与LogoutRequest不同,此数据包用于特定场景下的登出处理
         * 继承自ClientPacket,表示这是一个客户端发送的数据包
         * 注意:此数据包不包含任何数据,只是一个信号
         */
        class PlayerLogout final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             *
             * 根据数据包头部自动确定数据包类型
             */
            PlayerLogout(WorldPacket&& packet) : ClientPacket(std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 空实现,因为此数据包不包含任何负载数据
             * 调用时机:特定登出场景(如角色选择界面返回)
             */
            void Read() override { }
        };

        /**
         * @class PlayedTimeClient
         * @brief 游戏时间查询请求数据包(客户端->服务器)
         *
         * 客户端发送此数据包请求查询角色的游戏时间统计
         * 继承自ClientPacket,表示这是一个客户端发送的数据包
         */
        class PlayedTimeClient final : public ClientPacket
        {
        public:
            /**
             * @brief 构造函数
             * @param packet 接收到的原始网络数据包
             *
             * 初始化数据包类型为CMSG_PLAYED_TIME(客户端游戏时间查询)
             */
            PlayedTimeClient(WorldPacket&& packet) : ClientPacket(CMSG_PLAYED_TIME, std::move(packet)) { }

            /**
             * @brief 读取数据包内容
             *
             * 从网络数据包中读取是否触发脚本事件的标志
             * 调用时机:玩家打开角色信息界面或使用/played命令时
             */
            void Read() override;

            bool TriggerScriptEvent = false;  ///< 是否触发脚本事件,true表示需要触发OnPlayTime事件
        };

        /**
         * @class PlayedTime
         * @brief 游戏时间统计数据包(服务器->客户端)
         *
         * 服务器响应客户端的游戏时间查询请求,返回角色的总游戏时间和当前等级游戏时间
         * 继承自ServerPacket,表示这是一个服务器发送的数据包
         * 数据包大小:4字节(总时间) + 4字节(等级时间) + 1字节(脚本事件标志) = 9字节
         */
        class PlayedTime final : public ServerPacket
        {
        public:
            /**
             * @brief 默认构造函数
             *
             * 初始化数据包类型为SMSG_PLAYED_TIME,预分配9字节缓冲区
             */
            PlayedTime() : ServerPacket(SMSG_PLAYED_TIME, 9) { }

            /**
             * @brief 序列化数据包内容
             * @return 返回序列化后的数据包指针
             *
             * 将总游戏时间、等级游戏时间和脚本事件标志写入数据包缓冲区
             * 调用时机:服务器处理完游戏时间查询请求后响应客户端
             * 性能注意:数据包大小固定,无需动态扩容
             */
            WorldPacket const* Write() override;

            uint32 TotalTime = 0;          ///< 角色总游戏时间(秒)
            uint32 LevelTime = 0;          ///< 当前等级的游戏时间(秒)
            bool TriggerScriptEvent = false;  ///< 是否触发脚本事件,与客户端请求对应
        };
    }
}

#endif // CharacterPackets_h__
