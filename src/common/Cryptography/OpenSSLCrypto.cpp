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
 * @file OpenSSLCrypto.cpp
 * @brief OpenSSL 加密库多线程环境初始化实现
 *
 * 本文件实现了 OpenSSL 在多线程环境下的初始化和清理功能。
 * 主要职责：
 * - 管理 OpenSSL 3.0+ 版本的 Provider 生命周期
 * - 提供 Windows 平台上 Provider 模块路径设置
 * - 确保加密算法的可用性（包括 legacy 算法）
 */

#include "OpenSSLCrypto.h"
#include <openssl/crypto.h>

// OpenSSL 3.0.0 (0x30000000L) 引入了 Provider 机制
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/provider.h>

/**
 * @brief Legacy Provider 全局指针
 *
 * Legacy Provider 提供旧版加密算法支持，如 MD4、MD2、RIPEMD160 等。
 * 某些游戏协议可能需要这些算法进行兼容。
 */
OSSL_PROVIDER* LegacyProvider;

/**
 * @brief Default Provider 全局指针
 *
 * Default Provider 提供现代加密算法，包括：
 * - AES、DES、3DES 等对称加密算法
 * - RSA、DSA、DH 等非对称算法
 * - SHA-1、SHA-256、SHA-512 等哈希算法
 * - 随机数生成器
 */
OSSL_PROVIDER* DefaultProvider;
#endif

/**
 * @brief 初始化 OpenSSL 多线程环境
 *
 * 根据 OpenSSL 版本执行相应的初始化操作：
 * - OpenSSL 3.0+：
 *   - Windows 平台：设置 Provider 模块搜索路径
 *   - 加载 legacy Provider（支持旧版加密算法）
 *   - 加载 default Provider（现代加密算法）
 *
 * @param providerModulePath Provider 模块文件路径（仅 Windows 平台使用）
 *                           Windows 下需要指定 legacy.dll 和 default.dll 的搜索路径
 *
 * @note 在 Windows 上，OpenSSL 的 Provider 以 DLL 形式存在，需要设置搜索路径
 * @note Linux 上 OpenSSL 通常从系统库路径加载 Provider
 */
void OpenSSLCrypto::threadsSetup([[maybe_unused]] boost::filesystem::path const& providerModulePath)
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    // Windows 平台需要设置 Provider 模块的搜索路径
    // OpenSSL 会在该路径下查找 legacy.dll 和 default.dll
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
    OSSL_PROVIDER_set_default_search_path(nullptr, providerModulePath.string().c_str());
#endif

    // 加载 legacy Provider 以支持旧版加密算法
    // 例如：MD4、MDC2、RIPEMD160、Blowfish、CAST5、DES 等
    // 这些算法在现代版本中被标记为 legacy，但仍被某些协议使用
    LegacyProvider = OSSL_PROVIDER_load(nullptr, "legacy");

    // 加载 default Provider 以支持现代加密算法
    // 包括：AES、RSA、SHA 系列、HMAC、随机数生成等核心功能
    DefaultProvider = OSSL_PROVIDER_load(nullptr, "default");
#endif
}

/**
 * @brief 清理 OpenSSL 多线程环境资源
 *
 * 执行 OpenSSL 资源清理操作：
 * - OpenSSL 3.0+：
 *   - 卸载 legacy Provider
 *   - 卸载 default Provider
 *   - 清除 Provider 搜索路径设置
 *
 * @note 必须在所有使用 OpenSSL 的线程结束后调用
 * @note 卸载 Provider 会释放相关资源，防止内存泄漏
 */
void OpenSSLCrypto::threadsCleanup()
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    // 卸载 legacy Provider，释放相关资源
    OSSL_PROVIDER_unload(LegacyProvider);

    // 卸载 default Provider，释放相关资源
    OSSL_PROVIDER_unload(DefaultProvider);

    // 清除 Provider 默认搜索路径
    // 参数 nullptr 表示清除之前设置的搜索路径
    OSSL_PROVIDER_set_default_search_path(nullptr, nullptr);
#endif
}
