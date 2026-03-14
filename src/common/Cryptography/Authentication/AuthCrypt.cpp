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
 * @file AuthCrypt.cpp
 * @brief 认证加密器实现文件
 *
 * 模块职责：
 *   实现AuthCrypt类的成员函数，提供基于ARC4-drop1024的双向加密功能。
 *   使用HMAC-SHA1从会话密钥派生独立的加密和解密密钥。
 */

#include "AuthCrypt.h"
#include "BigNumber.h"
#include "Errors.h"
#include "HMAC.h"

#include <cstring>

/**
 * @brief AuthCrypt构造函数实现
 *
 * 初始化加密器为未初始化状态。
 */
AuthCrypt::AuthCrypt() :
    _initialized(false)
{ }

/**
 * @brief 初始化加密器实现
 *
 * 主要流程：
 *   1. 定义服务器加密密钥种子常量（16字节）
 *   2. 使用HMAC-SHA1从会话密钥派生服务器加密密钥
 *   3. 定义服务器解密密钥种子常量（16字节）
 *   4. 使用HMAC-SHA1从会话密钥派生客户端解密密钥
 *   5. 执行ARC4-drop1024：丢弃前1024字节的伪随机输出
 *   6. 标记为已初始化
 *
 * 密钥派生说明：
 *   - ServerEncryptionKey: 用于派生加密发送给客户端数据的密钥
 *   - ServerDecryptionKey: 用于派生解密客户端发送数据的密钥
 *   - 两个密钥种子不同，确保双向加密独立性
 *   - HMAC-SHA1确保密钥派生的安全性
 *
 * ARC4-drop1024说明：
 *   ARC4算法的前1024字节输出存在统计学偏差，容易被攻击。
 *   WoW协议采用ARC4-drop1024变种，丢弃前1024字节输出，
 *   从第1025字节开始使用，显著提升安全性。
 */
void AuthCrypt::Init(SessionKey const& K)
{
    // 服务器加密密钥种子 - 用于派生加密发送数据的密钥
    uint8 ServerEncryptionKey[] = { 0xCC, 0x98, 0xAE, 0x04, 0xE8, 0x97, 0xEA, 0xCA, 0x12, 0xDD, 0xC0, 0x93, 0x42, 0x91, 0x53, 0x57 };
    // 使用HMAC-SHA1派生服务器加密密钥：HMAC(ServerEncryptionKey, K)
    _serverEncrypt.Init(Trinity::Crypto::HMAC_SHA1::GetDigestOf(ServerEncryptionKey, K));

    // 服务器解密密钥种子 - 用于派生解密接收数据的密钥
    uint8 ServerDecryptionKey[] = { 0xC2, 0xB3, 0x72, 0x3C, 0xC6, 0xAE, 0xD9, 0xB5, 0x34, 0x3C, 0x53, 0xEE, 0x2F, 0x43, 0x67, 0xCE };
    // 使用HMAC-SHA1派生客户端解密密钥：HMAC(ServerDecryptionKey, K)
    _clientDecrypt.Init(Trinity::Crypto::HMAC_SHA1::GetDigestOf(ServerDecryptionKey, K));

    // Drop first 1024 bytes, as WoW uses ARC4-drop1024.
    // 丢弃前1024字节的ARC4输出，消除密钥调度算法的初始偏差
    std::array<uint8, 1024> syncBuf;
    _serverEncrypt.UpdateData(syncBuf);  // 让加密器产生并丢弃1024字节
    _clientDecrypt.UpdateData(syncBuf);  // 让解密器产生并丢弃1024字节

    _initialized = true;  // 标记初始化完成
}

/**
 * @brief 解密接收到的客户端数据实现
 *
 * @param data - 数据缓冲区指针
 * @param len - 数据长度
 *
 * 实现说明：
 *   - 调用ARC4的UpdateData方法进行流式解密
 *   - 解密是原地操作，直接修改data缓冲区
 *   - ARC4是同步流加密，加密和解密使用相同的异或操作
 *
 * 性能优化：
 *   - 无内存拷贝，原地解密
 *   - 简单的异或操作，CPU缓存友好
 */
void AuthCrypt::DecryptRecv(uint8 *data, size_t len)
{
    ASSERT(_initialized);  // 确保已初始化
    _clientDecrypt.UpdateData(data, len);  // ARC4流解密
}

/**
 * @brief 加密发送给客户端的数据实现
 *
 * @param data - 数据缓冲区指针
 * @param len - 数据长度
 *
 * 实现说明：
 *   - 调用ARC4的UpdateData方法进行流式加密
 *   - 加密是原地操作，直接修改data缓冲区
 *   - ARC4是同步流加密，加密和解密使用相同的异或操作
 *
 * 性能优化：
 *   - 无内存拷贝，原地加密
 *   - 简单的异或操作，CPU缓存友好
 */
void AuthCrypt::EncryptSend(uint8 *data, size_t len)
{
    ASSERT(_initialized);  // 确保已初始化
    _serverEncrypt.UpdateData(data, len);  // ARC4流加密
}
