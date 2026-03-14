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
 * @file VMapAssembler.cpp
 * @brief VMap组装器主程序入口
 *
 * 本模块是TrinityCore服务器地图处理工具链的重要组成部分，负责将
 * vmap4_extractor提取的原始建筑模型数据组装成可被游戏服务器使用的
 * 虚拟地图(VMap)格式。
 *
 * 主要职责:
 * 1. 接收命令行参数，指定输入输出目录
 * 2. 调用TileAssembler完成模型文件的格式转换和组装
 * 3. 将分散的建筑模型文件合并为按地图组织的vmap文件
 *
 * VMap在服务器中的作用:
 * - 提供视线检测(Line of Sight)计算
 * - 支持高度查询和碰撞检测
 * - 用于生物寻路和战斗逻辑
 *
 * 调用流程:
 * vmap4_extractor -> VMapAssembler -> vmaps/ (服务器加载)
 */

#include <string>
#include <iostream>

#include "TileAssembler.h"
#include "Banner.h"
#include "Locales.h"
#include "Util.h"

/**
 * @brief VMap组装器主函数
 *
 * 程序入口点，负责解析命令行参数并启动地图组装流程。
 *
 * @param argc 参数个数
 * @param argv 参数数组
 *             argv[0]: 程序名称
 *             argv[1]: 可选，原始数据目录（默认为"Buildings"）
 *             argv[2]: 可选，输出目录（默认为"vmaps"）
 * @return 成功返回0，失败返回1
 *
 * 使用示例:
 *   VMapAssembler                      # 使用默认目录
 *   VMapAssembler ./extracted ./vmaps  # 指定自定义目录
 *
 * 性能注意事项:
 * - 组装过程需要读取大量小文件，建议在SSD上执行
 * - 内存占用取决于处理的地图数量和复杂度
 */
int main(int argc, char* argv[])
{
    // 验证操作系统版本兼容性
    // 确保程序在支持的系统版本上运行
    Trinity::VerifyOsVersion();

    // 初始化本地化支持
    // 设置正确的字符编码和区域设置
    Trinity::Locale::Init();

    // 显示程序启动横幅
    // 包含TrinityCore版权信息和工具名称
    Trinity::Banner::Show("VMAP assembler", [](char const* text) { std::cout << text << std::endl; }, nullptr);

    // 设置默认输入输出目录
    // src: 存放vmap4_extractor提取的原始建筑模型文件
    // dest: 存放组装完成的vmap文件，供服务器使用
    std::string src = "Buildings";
    std::string dest = "vmaps";

    // 解析命令行参数
    // 允许用户自定义输入输出目录
    if (argc > 3)
    {
        // 参数过多时显示用法说明
        std::cout << "usage: " << argv[0] << " <raw data dir> <vmap dest dir>" << std::endl;
        return 1;
    }
    else
    {
        // 设置自定义输入目录
        if (argc > 1)
            src = argv[1];
        // 设置自定义输出目录
        if (argc > 2)
            dest = argv[2];
    }

    // 输出当前使用的目录配置
    std::cout << "using " << src << " as source directory and writing output to " << dest << std::endl;

    // 创建TileAssembler实例
    // TileAssembler负责实际的模型组装工作
    // 它会遍历输入目录，读取模型文件，并进行格式转换
    VMAP::TileAssembler* ta = new VMAP::TileAssembler(src, dest);

    // 执行世界地图转换
    // convertWorld2()会处理所有找到的地图数据
    // 包括模型文件的读取、转换、合并和写入
    if (!ta->convertWorld2())
    {
        // 转换失败，输出错误信息
        std::cout << "exit with errors" << std::endl;
        delete ta;
        return 1;
    }

    // 清理资源并输出成功信息
    delete ta;
    std::cout << "Ok, all done" << std::endl;
    return 0;
}