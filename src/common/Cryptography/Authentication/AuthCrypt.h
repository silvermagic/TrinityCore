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
 * @file AuthCrypt.h
 * @brief 认证加密器头文件
 *
 * 模块职责：
 *   提供游戏客户端与服务器之间通信数据的加密和解密功能。
 *   使用ARC4-drop1024流加密算法保护网络传输数据的安全性。
 *
 * 设计思路：
 *   - 基于SRP6认证成功后生成的会话密钥进行初始化
 *   - 使用HMAC-SHA1从会话密钥派生出两个独立的ARC4密钥
 *   - 服务器加密密钥用于加密发送给客户端的数据
 *   - 客户端解密密钥用于解密客户端发送的数据
 *   - 采用ARC4-drop1024算法（丢弃前1024字节）增强安全性
 */

#ifndef _AUTHCRYPT_H
#define _AUTHCRYPT_H

#include "ARC4.h"
#include "AuthDefines.h"
#include <array>

/**
 * @class AuthCrypt
 * @brief 认证加密器类
 *
 * 职责：
 *   管理游戏通信数据的加密和解密。该类封装了ARC4流加密算法，
 *   提供双向加密通道：服务器发送数据的加密和客户端接收数据的解密。
 *
 * 生命周期：
 *   1. 在认证会话建立时创建
 *   2. SRP6认证成功后调用Init初始化
 *   3. 在整个游戏会话期间持续使用
 *   4. 会话结束时销毁
 *
 * 线程安全性：
 *   非线程安全。每个网络连接应拥有独立的AuthCrypt实例。
 *
 * 使用示例：
 *   AuthCrypt crypt;
 *   crypt.Init(sessionKey);
 *   crypt.EncryptSend(packetData, packetSize);
 *   crypt.DecryptRecv(receivedData, receivedSize);
 */
class TC_COMMON_API AuthCrypt
{
    public:
        /**
         * @brief 构造函数
         *
         * 初始化AuthCrypt对象，设置未初始化状态。
         * 必须在调用Init()之后才能使用加密/解密功能。
         *
         * 调用时机：
         *   在认证会话对象创建时调用，通常在连接建立时。
         *
         * 性能注意事项：
         *   构造函数开销极小，仅设置初始化标志为false。
         */
        AuthCrypt();

        /**
         * @brief 初始化加密器
         *
         * @param K - 会话密钥（40字节），由SRP6认证成功后生成
         *
         * 使用HMAC-SHA1从会话密钥派生出两个独立的ARC4密钥，
         * 并执行ARC4-drop1024算法的初始化（丢弃前1024字节输出）。
         *
         * 调用时机：
         *   SRP6认证成功后立即调用，在开始游戏数据通信之前。
         *   只能调用一次，重复调用会导致未定义行为。
         *
         * 性能注意事项：
         *   - 执行两次HMAC-SHA1计算
         *   - 生成和丢弃2048字节的ARC4输出
         *   - 初始化时间约几毫秒，属于一次性开销
         *
         * 安全性说明：
         *   - 使用不同的HMAC密钥派生服务器加密密钥和客户端解密密钥
         *   - ARC4-drop1024消除了ARC4密钥调度算法的前1024字节偏差
         */
        void Init(SessionKey const& K);

        /**
         * @brief 解密接收到的客户端数据
         *
         * @param data - 指向待解密数据的指针，解密操作原地执行
         * @param len - 数据长度（字节）
         *
         * 使用客户端解密ARC4流解密来自客户端的网络数据包。
         *
         * 调用时机：
         *   收到客户端的网络数据包时，在解析数据包内容之前调用。
         *   每个数据包都需要解密。
         *
         * 性能注意事项：
         *   - 时间复杂度：O(n)，n为数据长度
         *   - 空间复杂度：O(1)，原地解密
         *   - 每字节约需几次异或操作，性能影响极小
         *
         * 前置条件：
         *   必须已调用Init()完成初始化。
         *
         * 异常：
         *   如果未初始化，触发ASSERT断言失败。
         */
        void DecryptRecv(uint8* data, size_t len);

        /**
         * @brief 加密发送给客户端的数据
         *
         * @param data - 指向待加密数据的指针，加密操作原地执行
         * @param len - 数据长度（字节）
         *
         * 使用服务器加密ARC4流加密发送给客户端的网络数据包。
         *
         * 调用时机：
         *   发送网络数据包给客户端之前，在数据包序列化完成后调用。
         *   每个数据包都需要加密。
         *
         * 性能注意事项：
         *   - 时间复杂度：O(n)，n为数据长度
         *   - 空间复杂度：O(1)，原地加密
         *   - 每字节约需几次异或操作，性能影响极小
         *
         * 前置条件：
         *   必须已调用Init()完成初始化。
         *
         * 异常：
         *   如果未初始化，触发ASSERT断言失败。
         */
        void EncryptSend(uint8* data, size_t len);

        /**
         * @brief 检查加密器是否已初始化
         *
         * @return bool - 如果已初始化返回true，否则返回false
         *
         * 调用时机：
         *   在调用加密/解密函数之前检查，确保已初始化。
         *
         * 性能注意事项：
         *   内联函数，仅返回一个bool值，性能开销极小。
         */
        bool IsInitialized() const { return _initialized; }

    private:
        /**
         * @brief 客户端数据解密器
         *
         * ARC4流加密器实例，用于解密客户端发送的数据。
         * 使用HMAC-SHA1从会话密钥派生的密钥初始化。
         *
         * 加密方向：客户端 -> 服务器（解密）
         */
        Trinity::Crypto::ARC4 _clientDecrypt;

        /**
         * @brief 服务器数据加密器
         *
         * ARC4流加密器实例，用于加密服务器发送给客户端的数据。
         * 使用HMAC-SHA1从会话密钥派生的密钥初始化。
         *
         * 加密方向：服务器 -> 客户端（加密）
         */
        Trinity::Crypto::ARC4 _serverEncrypt;

        /**
         * @brief 初始化状态标志
         *
         * 标记加密器是否已完成初始化。
         * true: 已调用Init()，可以使用加密/解密功能
         * false: 未初始化，禁止使用加密/解密功能
         */
        bool _initialized;
};
#endif
