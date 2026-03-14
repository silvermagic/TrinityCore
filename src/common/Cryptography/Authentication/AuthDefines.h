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
 * @file AuthDefines.h
 * @brief 认证模块基础类型定义
 *
 * 模块职责：
 *   定义认证系统的基础数据类型和常量，主要是会话密钥相关的类型定义。
 *   该文件是认证加密系统的基础设施，被AuthCrypt和SRP6等模块使用。
 *
 * 使用场景：
 *   - SRP6协议认证成功后生成的会话密钥
 *   - AuthCrypt初始化时接收会话密钥作为加密种子
 *   - 客户端与服务器之间建立加密通信通道
 */

#ifndef TRINITY_AUTHDEFINES_H
#define TRINITY_AUTHDEFINES_H

#include "Define.h"
#include <array>

/**
 * @brief 会话密钥长度（字节）
 *
 * 定义会话密钥的固定长度为40字节（320位）。
 * 该长度由SRP6协议的密钥派生算法决定。
 */
constexpr size_t SESSION_KEY_LENGTH = 40;

/**
 * @brief 会话密钥类型定义
 *
 * 使用std::array<uint8, 40>作为会话密钥的存储类型。
 * 会话密钥在SRP6认证成功后生成，用于初始化AuthCrypt加密器，
 * 保护后续客户端与服务器之间的游戏通信数据。
 *
 * 生命周期：
 *   1. SRP6认证成功后生成
 *   2. 用于初始化AuthCrypt
 *   3. 在整个游戏会话期间持续使用
 */
using SessionKey = std::array<uint8, SESSION_KEY_LENGTH>;

#endif
