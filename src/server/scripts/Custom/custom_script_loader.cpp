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
 * @file custom_script_loader.cpp
 * @brief 自定义脚本加载器模块
 *
 * 本文件提供了一个用于加载自定义脚本的框架。
 * 开发者可以在此文件中添加自定义脚本的加载函数声明和调用。
 *
 * 这个目录（Custom）用于存放服务器特定的自定义脚本，
 * 这些脚本不属于官方TrinityCore项目，而是服务器管理员自行开发的特色功能。
 *
 * 调用时机：
 * - 在服务器启动时，由脚本系统统一调用各个加载器函数
 *
 * 使用方法：
 * 1. 在Custom目录下创建自定义脚本文件
 * 2. 在文件中实现脚本注册函数（如：AddSC_my_custom_script）
 * 3. 在此文件顶部声明该函数
 * 4. 在AddCustomScripts函数中调用该函数
 */

// 在此处声明自定义脚本的加载函数
// 示例：void AddSC_my_custom_script();

/**
 * @brief 注册所有自定义脚本
 *
 * 此函数负责加载和注册所有自定义脚本模块。
 * 当前为空实现，开发者可以根据需要添加自定义脚本的注册代码。
 *
 * 调用时机：
 * - 服务器启动过程中的脚本初始化阶段
 *
 * 使用示例：
 * void AddCustomScripts()
 * {
 *     AddSC_my_custom_script();      // 注册自定义脚本1
 *     AddSC_my_custom_script2();     // 注册自定义脚本2
 * }
 *
 * 性能注意事项：
 * - 仅在启动时调用一次，无性能影响
 */
void AddCustomScripts()
{
    // 在此处添加自定义脚本的注册函数调用
    // 示例：AddSC_my_custom_script();
}
