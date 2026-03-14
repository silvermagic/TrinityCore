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
 * @file commonPCH.h
 * @brief 公共模块预编译头文件
 *
 * @details 本文件是 TrinityCore 公共模块(common)的预编译头文件(PCH)。
 *
 * 预编译头文件原理:
 * - 预编译头通过预先编译一组相对稳定的头文件，生成预编译的二进制文件(.gch/.pch)
 * - 当其他源文件包含此预编译头时，编译器直接加载预编译结果，跳过重复编译过程
 * - 这可以显著减少编译时间，特别是在大型项目中，因为大多数系统头文件和公共项目头文件很少改变
 *
 * 主要功能:
 * 1. 提高编译速度: 减少重复解析和编译相同头文件的时间
 * 2. 统一包含: 确保所有公共模块源文件都能访问相同的常用定义
 * 3. 减少代码冗余: 避免在每个源文件中重复包含相同的头文件
 *
 * 使用注意事项:
 * - 预编译头应只包含稳定的、变化频率低的头文件(如系统库、第三方库)
 * - 避免包含频繁变化的头文件，否则会降低预编译效果
 * - 必须在编译器选项中启用预编译头功能(USE_COREPCH)
 * - 预编译头通常需要放在源文件的最开始位置
 * - 修改预编译头文件会导致整个模块重新编译
 * - 在 CMake 中通过 USE_COREPCH 选项控制是否使用预编译头
 */

/**
 * @defgroup CommonPCH 公共模块预编译头包含
 * @brief 预编译头包含的所有模块和库
 * @{
 */

/* ==================== TrinityCore 核心头文件 ==================== */

/**
 * @brief 边界间隔层次结构 (BIH)
 *
 * 用于高效的空间分割和碰撞检测，是虚拟地图系统的重要组成部分。
 * BIH 是一种加速结构，用于快速剔除不需要检测的对象。
 */
#include "BoundingIntervalHierarchy.h"

/**
 * @brief 公共定义和工具
 *
 * 包含项目范围的公共定义、类型别名、工具函数等。
 * 这是项目中最基础的公共头文件之一。
 */
#include "Common.h"

/**
 * @brief 配置文件管理
 *
 * 提供 INI 格式配置文件的读取和解析功能。
 * 用于加载和管理服务器配置选项。
 */
#include "Config.h"

/**
 * @brief 基本类型定义和平台相关定义
 *
 * 定义项目使用的基本数据类型、整数类型、平台检测宏等。
 * 确保跨平台兼容性和类型大小一致性。
 */
#include "Define.h"

/**
 * @brief 错误处理和异常管理
 *
 * 提供错误报告、断言、异常处理等功能。
 * 包括错误级别定义和错误处理回调机制。
 */
#include "Errors.h"

/**
 * @brief Git 版本信息
 *
 * 包含从 Git 仓库提取的版本信息，如提交哈希、分支名称等。
 * 用于服务器版本追踪和日志记录。
 */
#include "GitRevision.h"

/**
 * @brief 日志系统核心
 *
 * 提供日志记录的核心功能，包括日志级别、日志输出、日志文件管理等。
 * 是 TrinityCore 日志系统的主要接口。
 */
#include "Log.h"

/**
 * @brief 日志消息结构
 *
 * 定义日志消息的数据结构，包含时间戳、级别、内容等字段。
 * 用于日志系统的内部消息传递和处理。
 */
#include "LogMessage.h"

/**
 * @brief 地图树结构
 *
 * 定义地图数据的空间树结构，用于地图管理和导航。
 * 支持地图块的快速查找和层次化管理。
 */
#include "MapTree.h"

/**
 * @brief 模型实例
 *
 * 定义游戏世界中的模型实例，包括位置、旋转、缩放等属性。
 * 用于虚拟地图系统中的模型管理。
 */
#include "ModelInstance.h"

/**
 * @brief 通用工具函数
 *
 * 提供字符串处理、随机数生成、时间转换等通用工具函数。
 * 包含大量辅助函数，简化常见编程任务。
 */
#include "Util.h"

/**
 * @brief 虚拟地图定义
 *
 * 定义虚拟地图系统的基本数据结构、常量和类型。
 * VMap 用于服务器端的碰撞检测和视线检测。
 */
#include "VMapDefinitions.h"

/**
 * @brief 世界模型
 *
 * 定义游戏世界模型的结构和加载逻辑。
 * 用于处理地图中的静态模型数据。
 */
#include "WorldModel.h"

/* ==================== G3D 3D数学库 ==================== */

/**
 * @brief G3D 射线类
 *
 * 提供 3D 射线的数学表示和操作，用于视线检测、射线投射等。
 * G3D 是一个高性能的 3D 数学库。
 */
#include <G3D/Ray.h>

/**
 * @brief G3D 三维向量类
 *
 * 提供 3D 向量的数学运算，包括点积、叉积、归一化等。
 * 广泛用于游戏中的位置、方向、速度等向量计算。
 */
#include <G3D/Vector3.h>

/* ==================== C++ 标准库 ==================== */

/**
 * @brief 算法库
 *
 * 提供排序、搜索、变换等通用算法。
 * 包括 std::sort、std::find、std::transform 等常用算法。
 */
#include <algorithm>

/**
 * @brief C 字符串处理
 *
 * 提供 C 风格字符串操作函数，如 memcpy、strcpy、strcmp 等。
 * 用于低级别的内存和字符串操作。
 */
#include <cstring>

/**
 * @brief 智能指针库
 *
 * 提供智能指针类型，如 std::unique_ptr、std::shared_ptr、std::weak_ptr。
 * 用于自动内存管理和资源生命周期控制。
 */
#include <memory>

/**
 * @brief 互斥量库
 *
 * 提供线程同步原语，包括 std::mutex、std::lock_guard、std::unique_lock。
 * 用于多线程环境下的数据保护。
 */
#include <mutex>

/**
 * @brief 集合容器
 *
 * 提供有序集合容器 std::set 和 std::multiset。
 * 用于存储唯一元素并进行快速查找和排序。
 */
#include <set>

/**
 * @brief 字符串流库
 *
 * 提供字符串流操作，包括 std::stringstream、std::istringstream、std::ostringstream。
 * 用于字符串与其他类型之间的转换和格式化。
 */
#include <sstream>

/**
 * @brief 字符串库
 *
 * 提供 std::string 和 std::wstring 类型及其操作函数。
 * 是 C++ 中文本处理的核心组件。
 */
#include <string>

/**
 * @brief 无序映射容器
 *
 * 提供哈希映射容器 std::unordered_map 和 std::unordered_multimap。
 * 用于键值对的快速查找，平均时间复杂度 O(1)。
 */
#include <unordered_map>

/**
 * @brief 动态数组容器
 *
 * 提供动态数组容器 std::vector。
 * 是最常用的序列容器，支持随机访问和动态大小调整。
 */
#include <vector>

/** @} */ // end of CommonPCH group
