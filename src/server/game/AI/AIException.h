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

#ifndef TRINITY_AIEXCEPTION_H
#define TRINITY_AIEXCEPTION_H

#include "Define.h"
#include <exception>
#include <string>

/**
 * @file AIException.h
 * @brief AI 系统异常定义文件
 *
 * 本文件定义了 AI 系统中使用的异常类。当 AI 系统遇到无效或错误的 AI 配置时，
 * 会抛出这些异常以便上层代码能够正确处理错误情况。
 *
 * @see InvalidAIException
 */

/**
 * @class InvalidAIException
 * @brief 无效 AI 异常类
 *
 * 当尝试创建或使用无效的 AI 对象时抛出此异常。例如：
 * - 尝试创建不存在的 AI 类型
 * - AI 脚本注册失败
 * - AI 初始化参数无效
 *
 * 此异常继承自 std::exception，符合标准 C++ 异常规范，
 * 可以被标准的 try-catch 块捕获和处理。
 *
 * @note 此异常类使用 TC_GAME_API 宏导出，可在动态链接库边界传递
 *
 * @example 使用示例：
 * @code
 * try
 * {
 *     CreatureAI* ai = CreateAI(invalidAIName, creature);
 * }
 * catch (InvalidAIException const& e)
 * {
 *     LOG_ERROR("ai", "创建 AI 失败: {}", e.what());
 * }
 * @endcode
 */
class TC_GAME_API InvalidAIException : public std::exception
{
public:
    /**
     * @brief 构造函数
     *
     * 创建一个包含错误消息的 InvalidAIException 实例
     *
     * @param msg 错误消息字符串指针，描述异常的具体原因。
     *            消息内容会被复制到内部存储，因此传入的字符串
     *            可以是临时字符串。消息应使用英文以保持一致性。
     *
     * @note 消息字符串应该清晰描述错误原因，便于调试和日志记录
     *
     * @example 示例：
     * @code
     * throw InvalidAIException("AI type 'CustomAI' not found in registry");
     * @endcode
     */
    InvalidAIException(char const* msg) : msg_(msg) {}

    /**
     * @brief 获取异常消息
     *
     * 重写 std::exception::what() 方法，返回异常的详细描述消息
     *
     * @return 返回指向以 null 结尾的字符数组的指针，包含异常消息
     *         返回的指针在异常对象生命周期内有效
     *
     * @note 此方法标记为 noexcept，保证不会抛出异常，符合 std::exception 规范
     *       返回的字符串是内部存储的副本，调用者无需释放内存
     */
    char const* what() const noexcept override { return msg_.c_str(); }

private:
    /**
     * @brief 异常消息存储
     *
     * 存储异常的详细描述消息。使用 std::string 管理内存，
     * 确保消息在异常对象生命周期内有效，无需调用者手动管理内存。
     *
     * 成员变量声明为 const，表示消息一旦在构造时设置后就不可更改，
     * 符合异常对象的不可变语义。
     */
    std::string const msg_;
};

#endif
