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

#ifndef TRINITY_GAMEOBJECTAIFACTORY_H
#define TRINITY_GAMEOBJECTAIFACTORY_H

#include "ObjectRegistry.h"
#include "SelectableAI.h"

// 前向声明
class GameObject;
class GameObjectAI;

/**
 * @file GameObjectAIFactory.h
 * @brief 游戏对象AI工厂类定义
 *
 * 本文件定义了游戏对象AI的工厂模板类，用于创建和管理游戏对象的AI实例。
 * 工厂模式允许在运行时动态创建不同类型的游戏对象AI，而无需在编译时确定具体类型。
 *
 * 主要功能：
 * - 提供游戏对象AI实例的创建机制
 * - 支持AI适用性检查（Permit机制）
 * - 通过注册表系统管理所有可用的AI工厂
 *
 * 使用示例：
 * @code
 * // 注册一个新的游戏对象AI
 * new GameObjectAIFactory<MyGameObjectAI>("MyGameObjectAI");
 *
 * // 通过注册表创建AI实例
 * GameObjectAI* ai = sGameObjectAIRegistry->GetAIItem("MyGameObjectAI")->Create(go);
 * @endcode
 *
 * @see GameObject
 * @see GameObjectAI
 * @see SelectableAI
 */

/**
 * @brief 游戏对象AI工厂模板类
 *
 * 该模板类继承自 SelectableAI，提供了创建特定类型游戏对象AI的能力。
 * 每个具体的游戏对象AI类型都应该有一个对应的工厂实例在注册表中注册。
 *
 * 模板参数：
 * - REAL_GO_AI: 实际的游戏对象AI类型，必须继承自GameObjectAI
 * - is_db_allowed: 是否允许从数据库配置中选择此AI，默认为true
 *   当设置为false时，该AI只能通过核心代码硬编码使用，不能通过数据库脚本配置
 *
 * 设计模式：
 * 使用工厂方法模式（Factory Method Pattern），将对象的创建延迟到子类。
 * 结合注册表模式（Registry Pattern），实现AI类型的动态注册和查找。
 *
 * @tparam REAL_GO_AI 实际的游戏对象AI类型
 * @tparam is_db_allowed 是否允许数据库配置使用此AI
 */
template <class REAL_GO_AI, bool is_db_allowed = true>
struct GameObjectAIFactory : public SelectableAI<GameObject, GameObjectAI, is_db_allowed>
{
    /**
     * @brief 构造函数
     *
     * 创建一个游戏对象AI工厂实例，并将其注册到全局AI注册表中。
     * 工厂创建后会自动注册，因此只需使用 new 创建即可完成注册。
     *
     * @param name AI工厂的名称，用于在注册表中标识此AI类型
     *             名称应该具有描述性，便于调试和日志记录
     *
     * 使用示例：
     * @code
     * // 在脚本加载时注册AI
     * new GameObjectAIFactory<MyChestAI>("MyChestAI");
     * @endcode
     */
    GameObjectAIFactory(std::string const& name) : SelectableAI<GameObject, GameObjectAI, is_db_allowed>(name) { }

    /**
     * @brief 创建AI实例
     *
     * 工厂方法：创建并返回一个新的游戏对象AI实例。
     * 该方法是虚函数，由基类定义接口，在此提供具体实现。
     *
     * 实现细节：
     * - 使用 new 操作符创建 REAL_GO_AI 类型的新实例
     * - 将游戏对象指针传递给AI构造函数
     * - 调用者负责管理返回指针的生命周期
     *
     * @param go 需要创建AI的游戏对象指针
     * @return GameObjectAI* 新创建的AI实例指针
     *
     * @note 调用者负责删除返回的AI实例
     * @note 必须确保 go 参数不为 nullptr
     */
    GameObjectAI* Create(GameObject* go) const override
    {
        return new REAL_GO_AI(go);
    }

    /**
     * @brief 检查AI对游戏对象的适用性
     *
     * 评估此AI类型是否适合控制指定的游戏对象。
     * 返回一个优先级分数，分数越高表示越适合。
     * 注册表会根据这个分数选择最合适的AI类型。
     *
     * 典型的返回值：
     * - 正数：表示适用，数值越大优先级越高
     * - 0：表示中性，可以使用但不是最佳选择
     * - 负数：表示不适用，不应该使用此AI
     *
     * PERMIT 常量说明：
     * - PERMIT_BASE_NORMAL (-1)：基础正常值，默认不适用
     * - PERMIT_BASE_IDLE (-2)：仅用于空闲状态
     * - PERMIT_BASE_REACTIVE (-3)：反应式AI
     * - PERMIT_BASE_PROACTIVE (-4)：主动式AI
     * - PERMIT_BASE_NO (-100)：完全不适用
     *
     * @param go 要检查的游戏对象（const指针，只读访问）
     * @return int32 适用性分数，数值越大优先级越高
     *
     * @note 该方法通过调用 REAL_GO_AI 类的静态 Permissible 方法实现
     * @note 每个AI类都应该实现自己的 Permissible 静态方法
     */
    int32 Permit(GameObject const* go) const override
    {
        return REAL_GO_AI::Permissible(go);
    }
};

/**
 * @brief 游戏对象AI注册表类型定义
 *
 * 定义了游戏对象AI工厂的全局注册表类型。
 * 注册表是一个单例容器，存储所有已注册的AI工厂，并支持：
 * - 按名称查找AI工厂
 * - 根据适用性选择最佳AI
 * - 遍历所有已注册的AI
 *
 * 注册表使用模板继承实现，基础类型为 SelectableAI<GameObject, GameObjectAI>::FactoryHolderRegistry
 */
typedef SelectableAI<GameObject, GameObjectAI>::FactoryHolderRegistry GameObjectAIRegistry;

/**
 * @brief 游戏对象AI注册表单例访问宏
 *
 * 通过此宏可以方便地访问全局游戏对象AI注册表实例。
 * 注册表在首次访问时自动创建，采用懒加载模式。
 *
 * 使用示例：
 * @code
 * // 查找特定名称的AI
 * auto factory = sGameObjectAIRegistry->GetAIItem("MyChestAI");
 *
 * // 获取最适合的AI
 * auto bestAI = sGameObjectAIRegistry->GetAI(go);
 * @endcode
 *
 * @return GameObjectAIRegistry* 返回全局注册表实例的指针
 */
#define sGameObjectAIRegistry GameObjectAIRegistry::instance()

#endif
