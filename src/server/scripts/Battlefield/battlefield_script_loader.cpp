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
 * @file battlefield_script_loader.cpp
 * @brief 战场脚本加载器模块
 *
 * 本文件负责加载所有战场相关的脚本模块。
 * 在TrinityCore架构中，每个脚本目录都有对应的加载器函数，
 * 用于统一管理和注册该目录下的所有脚本。
 *
 * 调用时机：
 * - 在服务器启动时，由脚本系统统一调用各个加载器函数
 * - 确保所有战场脚本被正确注册到脚本管理器中
 */

/// 前向声明：冬拥湖战场脚本注册函数
void AddSC_BF_wintergrasp();

/**
 * @brief 注册所有战场脚本
 *
 * 此函数负责加载和注册所有战场相关的脚本模块。
 * 当前仅包含冬拥湖战场，后续可扩展其他战场脚本。
 *
 * 调用时机：
 * - 服务器启动过程中的脚本初始化阶段
 *
 * 性能注意事项：
 * - 仅在启动时调用一次，无性能影响
 */
void AddBattlefieldScripts()
{
    // 注册冬拥湖战场脚本
    AddSC_BF_wintergrasp();
}
