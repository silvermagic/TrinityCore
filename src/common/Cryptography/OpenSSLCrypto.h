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
 * @file OpenSSLCrypto.h
 * @brief OpenSSL 加密库多线程环境初始化模块
 *
 * 本文件提供了 OpenSSL 加密库在多线程环境下的初始化和清理功能。
 * OpenSSL 在多线程环境下需要特殊的初始化，否则会导致程序崩溃。
 * 主要职责：
 * - 在使用 OpenSSL 的线程启动前进行必要的设置
 * - 在所有使用 OpenSSL 的线程结束后进行清理
 * - 管理 OpenSSL 3.0+ 版本的 Provider 加载（legacy 和 default）
 */

#ifndef TRINITY_OPENSSL_CRYPTO_H
#define TRINITY_OPENSSL_CRYPTO_H

#include "Define.h"
#include <boost/filesystem/path.hpp>

/**
 * @namespace OpenSSLCrypto
 * @brief OpenSSL 加密库多线程支持命名空间
 *
 * 提供了一组用于设置 OpenSSL 在多线程环境下正常工作的函数。
 * 如果不正确设置，OpenSSL 将在多线程环境下崩溃。
 */
namespace OpenSSLCrypto
{
    /**
     * @brief 初始化 OpenSSL 多线程环境
     *
     * 此函数必须在创建使用 OpenSSL 的线程之前调用。
     * 主要功能：
     * - OpenSSL 3.0+ 版本：加载 legacy 和 default Provider
     * - Windows 平台：设置 Provider 模块的搜索路径
     *
     * @param providerModulePath Provider 模块文件的路径（仅在 Windows 平台使用）
     *                           用于定位 OpenSSL 的 legacy 和 default provider 模块
     *
     * @note 此函数必须是线程启动前调用的，确保 OpenSSL 在多线程环境下的安全性
     * @note OpenSSL 3.0+ 需要显式加载 Provider 才能使用某些加密算法
     * @see threadsCleanup()
     */
    TC_COMMON_API void threadsSetup(boost::filesystem::path const& providerModulePath);

    /**
     * @brief 清理 OpenSSL 多线程环境资源
     *
     * 此函数必须在所有使用 OpenSSL 的线程结束后调用。
     * 主要功能：
     * - OpenSSL 3.0+ 版本：卸载 legacy 和 default Provider
     * - 清理 Provider 搜索路径设置
     *
     * @note 此函数必须是线程结束后调用的，避免资源泄漏
     * @note 调用顺序：先创建线程 -> 使用 OpenSSL -> 结束线程 -> 调用清理
     * @see threadsSetup()
     */
    TC_COMMON_API void threadsCleanup();
}

#endif
