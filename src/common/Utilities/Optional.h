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
 * @file Optional.h
 * @brief 可选值类型别名定义模块
 *
 * 本模块定义了 Optional 类型别名，用于包装可能存在或不存在的值。
 * 这是 C++17 std::optional 的封装，提供统一的项目内类型名称。
 *
 * 主要特点:
 * - 基于 C++17 标准库的 std::optional 实现
 * - 提供统一的项目命名风格
 * - 支持所有 std::optional 的特性
 *
 * Optional 的优势:
 * - 比 std::pair<T, bool> 更语义化
 * - 比原始指针更安全，无需手动管理内存
 * - 明确表达"值可能不存在"的语义
 * - 支持 std::nullopt 表示空值
 *
 * 典型应用场景:
 * - 函数返回值可能失败时
 * - 配置项可能不存在时
 * - 查找操作未找到结果时
 * - 延迟初始化的场景
 *
 * @example
 * // 函数返回可能不存在的值
 * Optional<int> FindPlayerLevel(uint32 playerId) {
 *     Player* player = GetPlayer(playerId);
 *     if (player)
 *         return player->GetLevel();
 *     return std::nullopt;  // 或 {}
 * }
 *
 * // 使用示例
 * auto level = FindPlayerLevel(123);
 * if (level) {
 *     std::cout << "Level: " << *level << std::endl;
 * } else {
 *     std::cout << "Player not found" << std::endl;
 * }
 *
 * // 使用 value_or 提供默认值
 * int levelValue = level.value_or(0);
 */

#ifndef TrinityCore_Optional_h__
#define TrinityCore_Optional_h__

#include <optional>

/**
 * @brief 可选值类型别名
 *
 * 封装 std::optional，提供统一的项目命名风格。
 * Optional<T> 表示一个可能包含 T 类型值或为空的容器。
 *
 * @tparam T 包装的值类型
 *
 * 常用操作:
 * - has_value() 或 operator bool(): 检查是否有值
 * - operator*() 或 value(): 获取值（有值时）
 * - value_or(default): 获取值或默认值
 * - reset(): 清空值
 * - emplace(...): 原地构造值
 *
 * @性能说明:
 * - 存储开销：通常等于 sizeof(T) + sizeof(bool)
 * - 无动态内存分配（值内联存储）
 * - 访问值的开销与直接访问 T 相当
 *
 * @example
 * Optional<std::string> name = GetName();
 * if (name.has_value()) {
 *     std::cout << "Name: " << name.value() << std::endl;
 * }
 *
 * // 使用范围 for 遍历（C++20 起）
 * for (auto& n : name) {
 *     std::cout << n << std::endl;
 * }
 */
template <class T>
using Optional = std::optional<T>;

#endif // TrinityCore_Optional_h__
