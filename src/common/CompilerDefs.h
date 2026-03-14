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

// ============================================================================
// CompilerDefs.h - 编译器和平台检测
// ============================================================================
// 模块职责：
//   检测编译器和平台类型，提供统一的宏定义供其他模块使用。
//   使代码能够根据不同的编译器和平台进行条件编译。
//
// 检测内容：
//   1. 平台检测：
//      - Windows (TRINITY_PLATFORM_WINDOWS)
//      - Unix/Linux (TRINITY_PLATFORM_UNIX)
//      - Apple/macOS (TRINITY_PLATFORM_APPLE)
//      - Intel (TRINITY_PLATFORM_INTEL)
//
//   2. 编译器检测：
//      - Microsoft Visual C++ (TRINITY_COMPILER_MICROSOFT)
//      - GNU GCC (TRINITY_COMPILER_GNU)
//      - Borland C++ (TRINITY_COMPILER_BORLAND)
//      - Intel C++ (TRINITY_COMPILER_INTEL)
//
// 使用方法：
//   #if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
//       // Windows 特定代码
//   #endif
//
//   #if TRINITY_COMPILER == TRINITY_COMPILER_GNU
//       // GCC 特定代码
//   #endif
//
// 注意事项：
//   - 本文件必须在其他头文件之前包含
//   - 使用预处理器宏实现，编译时确定
// ============================================================================

#ifndef TRINITY_COMPILERDEFS_H
#define TRINITY_COMPILERDEFS_H

// ============================================================================
// 平台定义常量
// ============================================================================
// 每个平台使用唯一的整数标识符
// ============================================================================
#define TRINITY_PLATFORM_WINDOWS 0  // Windows 平台（包括 32 位和 64 位）
#define TRINITY_PLATFORM_UNIX    1  // Unix/Linux 平台
#define TRINITY_PLATFORM_APPLE   2  // Apple macOS/iOS 平台
#define TRINITY_PLATFORM_INTEL   3  // Intel 特定平台

// ============================================================================
// 平台自动检测
// ============================================================================
// 根据编译器预定义的宏自动检测当前平台
// 注意：Windows 64 位也定义了 _WIN32，所以必须先检测 _WIN64
// ============================================================================
#if defined( _WIN64 )
#  define TRINITY_PLATFORM TRINITY_PLATFORM_WINDOWS
#elif defined( __WIN32__ ) || defined( WIN32 ) || defined( _WIN32 )
#  define TRINITY_PLATFORM TRINITY_PLATFORM_WINDOWS
#elif defined( __APPLE_CC__ )
#  define TRINITY_PLATFORM TRINITY_PLATFORM_APPLE
#elif defined( __INTEL_COMPILER )
#  define TRINITY_PLATFORM TRINITY_PLATFORM_INTEL
#else
#  define TRINITY_PLATFORM TRINITY_PLATFORM_UNIX
#endif

// ============================================================================
// 编译器定义常量
// ============================================================================
// 每个编译器使用唯一的整数标识符
// ============================================================================
#define TRINITY_COMPILER_MICROSOFT 0  // Microsoft Visual C++
#define TRINITY_COMPILER_GNU       1  // GNU GCC/Clang
#define TRINITY_COMPILER_BORLAND   2  // Borland C++ Builder
#define TRINITY_COMPILER_INTEL     3  // Intel C++ Compiler

// ============================================================================
// 编译器自动检测
// ============================================================================
// 根据编译器预定义的宏自动检测当前编译器
// 对于 GCC，还定义了版本号宏用于条件编译
// ============================================================================
#ifdef _MSC_VER
#  define TRINITY_COMPILER TRINITY_COMPILER_MICROSOFT
#elif defined( __BORLANDC__ )
#  define TRINITY_COMPILER TRINITY_COMPILER_BORLAND
#elif defined( __INTEL_COMPILER )
#  define TRINITY_COMPILER TRINITY_COMPILER_INTEL
#elif defined( __GNUC__ )
#  define TRINITY_COMPILER TRINITY_COMPILER_GNU
   // GCC 版本号，格式为：主版本*10000 + 次版本*100 + 补丁版本
   // 例如：GCC 9.3.0 = 90300
#  define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#else
#  error "FATAL ERROR: Unknown compiler."
#endif

#endif
