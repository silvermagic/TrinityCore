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
 * @file ScriptMgr.h
 * @brief 脚本管理器核心头文件
 *
 * 本模块是TrinityCore脚本系统的核心，负责：
 * - 定义各种脚本类型的基类（SpellScriptLoader, CreatureScript, PlayerScript等）
 * - 提供ScriptMgr单例类管理所有脚本的注册、加载和执行
 * - 提供宏简化脚本的注册过程
 *
 * 脚本系统架构：
 * - ScriptObject: 所有脚本对象的基类
 * - 各种专用脚本类: 继承自ScriptObject，提供特定功能的钩子函数
 * - ScriptMgr: 管理器，负责脚本的注册、查询和调用
 * - ScriptRegistry: 模板类，存储和管理特定类型的脚本
 *
 * 使用方式：
 * 1. 创建自定义脚本类，继承相应的脚本基类
 * 2. 在构造函数中注册脚本名称
 * 3. 重写需要的虚拟函数实现自定义逻辑
 * 4. 使用宏（如RegisterCreatureAI）在模块初始化时注册脚本
 */

#ifndef SC_SCRIPTMGR_H
#define SC_SCRIPTMGR_H

#include "Common.h"
#include "ObjectGuid.h"
#include "Tuples.h"
#include "Types.h"
#include <memory>
#include <vector>

// ==================== 前向声明 ====================
// 游戏核心类
class AccountMgr;
class AuctionHouseObject;
class Aura;
class AuraScript;
class Battlefield;
class Battleground;
class BattlegroundMap;
class Channel;
class Creature;
class CreatureAI;
class DynamicObject;
class GameObject;
class GameObjectAI;
class Guild;
class GridMap;
class Group;
class InstanceMap;
class InstanceScript;
class Item;
class Map;
class ModuleReference;
class OutdoorPvP;
class Player;
class Quest;
class ScriptMgr;
class Spell;
class SpellInfo;
class SpellScript;
class SpellCastTargets;
class Transport;
class Unit;
class Vehicle;
class Weather;
class WorldPacket;
class WorldSocket;
class WorldObject;
class WorldSession;

// 数据结构
struct AchievementCriteriaData;
struct AreaTriggerEntry;
struct AuctionEntry;
struct ConditionSourceInfo;
struct Condition;
struct CreatureTemplate;
struct CreatureData;
struct ItemTemplate;
struct MapEntry;
struct Position;

namespace Trinity::ChatCommands { struct ChatCommandBuilder; }

// 枚举类型
enum BattlegroundTypeId : uint32;
enum ContentLevels : uint8;
enum Difficulty : uint8;
enum DuelCompleteType : uint8;
enum Emote : uint32;
enum QuestStatus : uint8;
enum RemoveMethod : uint8;
enum ShutdownExitCode : uint32;
enum ShutdownMask : uint32;
enum SpellEffIndex : uint8;
enum WeatherState : uint32;
enum XPColorChar : uint8;

/** @brief 最大可视范围（网格大小） */
#define VISIBLE_RANGE       166.0f

/**
 * @brief 待添加的脚本类型类
 *
 * 以下脚本类型计划在未来版本中添加：
 * - MailScript: 邮件相关脚本
 * - SessionScript: 会话相关脚本
 * - CollisionScript: 碰撞检测脚本
 * - ArenaTeamScript: 竞技场队伍脚本
 */

/**
 * @brief 添加新脚本类型的标准流程
 *
 * 步骤说明：
 *
 * 1. 定义脚本类，继承自ScriptObject：
 *    class MyScriptType : public ScriptObject
 *    {
 *        uint32 _someId;  // 脚本特定数据
 *
 *        private:
 *            void RegisterSelf();  // 注册自身
 *
 *        protected:
 *            // 构造函数，调用基类构造函数并注册脚本
 *            MyScriptType(char const* name, uint32 someId)
 *                : ScriptObject(name), _someId(someId)
 *            {
 *                ScriptRegistry<MyScriptType>::AddScript(this);
 *            }
 *
 *        public:
 *            // 非必须重写的虚函数：提供空实现
 *            virtual void OnSomeEvent(uint32 someArg1, std::string& someArg2) { }
 *
 *            // 纯虚函数：必须由派生类实现
 *            virtual void OnAnotherEvent(uint32 someArg) = 0;
 *    }
 *
 * 2. 在ScriptMgr.cpp底部添加ScriptRegistry特化：
 *    template class ScriptRegistry<MyScriptType>;
 *
 * 3. 在ScriptMgr::~ScriptMgr析构函数中添加清理代码：
 *    SCR_CLEAR(MyScriptType);
 *
 * 4. 在ScriptMgr类中添加触发事件的函数（ScriptMgr.h）：
 *    void OnSomeEvent(uint32 someArg1, std::string& someArg2);
 *    void OnAnotherEvent(uint32 someArg);
 *
 * 5. 在ScriptMgr.cpp中实现这些函数：
 *    void ScriptMgr::OnSomeEvent(uint32 someArg1, std::string& someArg2)
 *    {
 *        FOREACH_SCRIPT(MyScriptType)->OnSomeEvent(someArg1, someArg2);
 *    }
 *
 * 6. 在核心代码中调用这些函数触发事件
 */

/**
 * @class ScriptObject
 * @brief 所有脚本对象的基类
 *
 * 这是TrinityCore脚本系统的基础类，所有其他脚本类型都继承自此类。
 * 提供脚本的基本功能：名称管理和脚本注册。
 *
 * 职责：
 * - 存储脚本名称（用于识别和调试）
 * - 提供统一的脚本对象接口
 * - 自动管理脚本生命周期
 *
 * 注意：
 * - 此类不能直接实例化，只能通过派生类使用
 * - 脚本名称在构造时设置，之后不可更改
 */
class TC_GAME_API ScriptObject
{
    friend class ScriptMgr;  // 允许ScriptMgr访问私有成员

    public:
        /**
         * @brief 获取脚本名称
         * @return 脚本名称的常量引用
         *
         * 脚本名称用于识别脚本，通常与数据库中的ScriptName字段对应
         */
        std::string const& GetName() const;

    protected:
        /**
         * @brief 构造函数（protected，只能由派生类调用）
         * @param name 脚本名称
         *
         * 初始化脚本对象并设置脚本名称
         */
        ScriptObject(char const* name);

        /**
         * @brief 虚析构函数
         *
         * 确保派生类对象能够正确销毁
         */
        virtual ~ScriptObject();

    private:
        /** @brief 脚本名称（不可变） */
        std::string const _name;
};

/**
 * @class SpellScriptLoader
 * @brief 法术脚本加载器基类
 *
 * 用于创建和管理法术（Spell）和光环（Aura）的脚本。
 * 继承自ScriptObject，提供创建SpellScript和AuraScript实例的接口。
 *
 * 职责：
 * - 为特定法术创建SpellScript实例（处理法术施放事件）
 * - 为特定法术创建AuraScript实例（处理光环效果事件）
 *
 * 使用方式：
 * 1. 创建派生类，重写GetSpellScript()或GetAuraScript()
 * 2. 在函数中创建并返回新的脚本实例
 * 3. 使用RegisterSpellScript宏注册脚本
 *
 * 注意：
 * - 此类需要在数据库中关联法术ID和脚本名称
 * - 一个SpellScriptLoader可以为同一个法术提供多种脚本类型
 */
class TC_GAME_API SpellScriptLoader : public ScriptObject
{
    protected:
        /**
         * @brief 构造函数（protected）
         * @param name 脚本名称，通常与数据库中的ScriptName对应
         */
        explicit SpellScriptLoader(char const* name);

    public:
        /**
         * @brief 创建SpellScript实例
         * @return 新创建的SpellScript指针，如果不支持则返回nullptr
         *
         * 当法术施放时，ScriptMgr会调用此函数创建脚本实例。
         * 派生类应重写此函数以提供自定义的SpellScript实现。
         *
         * @note 返回的指针由核心管理，无需手动释放
         */
        virtual SpellScript* GetSpellScript() const;

        /**
         * @brief 创建AuraScript实例
         * @return 新创建的AuraScript指针，如果不支持则返回nullptr
         *
         * 当光环效果应用时，ScriptMgr会调用此函数创建脚本实例。
         * 派生类应重写此函数以提供自定义的AuraScript实现。
         *
         * @note 返回的指针由核心管理，无需手动释放
         */
        virtual AuraScript* GetAuraScript() const;
};

/**
 * @class ServerScript
 * @brief 服务器脚本基类
 *
 * 提供服务器级别的网络事件钩子，用于监控和拦截网络通信。
 * 适用于需要处理原始网络数据包的场景，如反作弊、日志记录、协议分析等。
 *
 * 调用时机：网络事件发生时（连接、断开、收发包等）
 * 性能注意事项：
 * - 钩子在I/O线程中执行，不要执行耗时操作
 * - 数据包钩子会被频繁调用，需要优化性能
 */
class TC_GAME_API ServerScript : public ScriptObject
{
    protected:
        explicit ServerScript(char const* name);

    public:
        /**
         * @brief 网络I/O启动时调用
         *
         * 当WorldTcpSessionMgr启动时触发，表示服务器开始监听网络连接
         */
        virtual void OnNetworkStart();

        /**
         * @brief 网络I/O停止时调用
         *
         * 当服务器关闭网络服务时触发
         */
        virtual void OnNetworkStop();

        /**
         * @brief 新连接建立时调用
         * @param socket 新建立的socket连接
         *
         * 当客户端建立连接时触发。
         * @warning 不要存储socket对象，它可能在任何时候失效
         */
        virtual void OnSocketOpen(std::shared_ptr<WorldSocket> socket);

        /**
         * @brief 连接关闭时调用
         * @param socket 关闭的socket连接
         *
         * 当客户端断开连接时触发。
         * @warning 不要存储socket对象，连接已断开
         */
        virtual void OnSocketClose(std::shared_ptr<WorldSocket> socket);

        /**
         * @brief 发送数据包时调用
         * @param session 客户端会话（可能为null）
         * @param packet 发送的数据包（可修改）
         *
         * 当服务器向客户端发送数据包时触发。
         * packet是数据包的副本，可以安全地读取和修改。
         */
        virtual void OnPacketSend(WorldSession* session, WorldPacket& packet);

        /**
         * @brief 接收数据包时调用
         * @param session 客户端会话（可能为null，如认证包）
         * @param packet 接收的数据包（可修改）
         *
         * 当收到客户端发来的有效数据包时触发。
         * packet是数据包的副本，可以安全地读取和修改。
         * @note 需要检查session指针是否为null
         */
        virtual void OnPacketReceive(WorldSession* session, WorldPacket& packet);
};

/**
 * @class WorldScript
 * @brief 世界脚本基类
 *
 * 提供世界级别的全局事件钩子，用于处理服务器生命周期、配置更新等。
 * 适用于需要监控服务器状态变化的场景。
 *
 * 调用时机：服务器启动、关闭、配置更新等全局事件
 */
class TC_GAME_API WorldScript : public ScriptObject
{
    protected:
        explicit WorldScript(char const* name);

    public:
        /**
         * @brief 世界开放状态改变时调用
         * @param open true表示世界开放，false表示世界关闭
         *
         * 当服务器开放或关闭玩家登录时触发
         */
        virtual void OnOpenStateChange(bool open);

        /**
         * @brief 世界配置加载后调用
         * @param reload true表示重新加载，false表示首次加载
         *
         * 当worldserver.conf配置文件加载完成时触发
         */
        virtual void OnConfigLoad(bool reload);

        /**
         * @brief 每日消息（MOTD）改变前调用
         * @param newMotd 新的MOTD内容（可修改）
         *
         * 当服务器每日消息即将更新时触发，可以修改消息内容
         */
        virtual void OnMotdChange(std::string& newMotd);

        /**
         * @brief 服务器关闭启动时调用
         * @param code 退出代码
         * @param mask 关闭掩码（如重启、关机等）
         *
         * 当服务器开始关闭流程时触发
         */
        virtual void OnShutdownInitiate(ShutdownExitCode code, ShutdownMask mask);

        /**
         * @brief 服务器关闭取消时调用
         *
         * 当正在进行的关闭流程被取消时触发
         */
        virtual void OnShutdownCancel();

        /**
         * @brief 世界更新时调用
         * @param diff 距离上次更新的时间（毫秒）
         *
         * 每个世界tick都会调用，频率很高。
         * @warning 不要在此函数中执行耗时操作，会影响服务器性能
         */
        virtual void OnUpdate(uint32 diff);

        /**
         * @brief 服务器启动时调用
         *
         * 当服务器完成初始化并开始运行时触发
         */
        virtual void OnStartup();

        /**
         * @brief 服务器关闭时调用
         *
         * 当服务器即将停止运行时触发
         */
        virtual void OnShutdown();
};

/**
 * @class FormulaScript
 * @brief 公式计算脚本基类
 *
 * 提供游戏公式计算的钩子，用于自定义经验、荣誉、颜色等级等计算。
 * 适用于需要修改游戏平衡性公式的场景。
 *
 * 调用时机：各种游戏公式计算完成后
 * 修改方式：通过引用参数修改计算结果
 */
class TC_GAME_API FormulaScript : public ScriptObject
{
    protected:
        explicit FormulaScript(char const* name);

    public:
        /**
         * @brief 荣誉计算后调用
         * @param honor 荣誉值（可修改）
         * @param level 玩家等级
         * @param multiplier 荣誉倍率
         */
        virtual void OnHonorCalculation(float& honor, uint8 level, float multiplier);

        /**
         * @brief 灰色等级计算后调用
         * @param grayLevel 灰色等级（可修改）
         * @param playerLevel 玩家等级
         *
         * 灰色等级用于判断怪物是否给予经验值
         */
        virtual void OnGrayLevelCalculation(uint8& grayLevel, uint8 playerLevel);

        /**
         * @brief 经验颜色代码计算后调用
         * @param color 颜色代码（可修改）
         * @param playerLevel 玩家等级
         * @param mobLevel 怪物等级
         *
         * 颜色代码用于显示怪物等级相对于玩家的颜色（红色、黄色、绿色等）
         */
        virtual void OnColorCodeCalculation(XPColorChar& color, uint8 playerLevel, uint8 mobLevel);

        /**
         * @brief 零差异计算后调用
         * @param diff 差异值（可修改）
         * @param playerLevel 玩家等级
         */
        virtual void OnZeroDifferenceCalculation(uint8& diff, uint8 playerLevel);

        /**
         * @brief 基础经验获取计算后调用
         * @param gain 经验值（可修改）
         * @param playerLevel 玩家等级
         * @param mobLevel 怪物等级
         * @param content 内容等级
         */
        virtual void OnBaseGainCalculation(uint32& gain, uint8 playerLevel, uint8 mobLevel, ContentLevels content);

        /**
         * @brief 经验获取计算后调用
         * @param gain 经验值（可修改）
         * @param player 获得经验的玩家
         * @param unit 击杀的单位
         */
        virtual void OnGainCalculation(uint32& gain, Player* player, Unit* unit);

        /**
         * @brief 组队经验倍率计算后调用
         * @param rate 倍率（可修改）
         * @param count 队伍人数
         * @param isRaid 是否为团队副本
         */
        virtual void OnGroupRateCalculation(float& rate, uint32 count, bool isRaid);
};

/**
 * @class MapScript
 * @brief 地图脚本模板基类
 *
 * 提供地图级别的事件钩子，用于监控和管理地图生命周期。
 * 这是一个模板类，可以用于不同类型的地图（普通地图、副本、战场）。
 *
 * 模板参数TMap可以是：
 * - Map: 普通世界地图
 * - InstanceMap: 副本地图
 * - BattlegroundMap: 战场地图
 *
 * 调用时机：地图创建、销毁、玩家进出、网格加载卸载等
 */
template<class TMap>
class TC_GAME_API MapScript
{
        /** @brief 关联的地图条目信息 */
        MapEntry const* _mapEntry;

    protected:
        /**
         * @brief 构造函数（protected）
         * @param mapEntry 地图条目信息，包含地图ID等基础数据
         */
        explicit MapScript(MapEntry const* mapEntry);

    public:
        /**
         * @brief 获取地图条目信息
         * @return 地图条目指针，可能为null
         */
        MapEntry const* GetEntry() const;

        /**
         * @brief 地图创建时调用
         * @param map 新创建的地图对象
         *
         * 当新的地图实例被创建时触发
         */
        virtual void OnCreate(TMap* map);

        /**
         * @brief 地图销毁前调用
         * @param map 即将销毁的地图对象
         *
         * 当地图实例即将被销毁时触发
         */
        virtual void OnDestroy(TMap* map);

        /**
         * @brief 网格地图加载时调用
         * @param map 地图对象
         * @param gmap 网格地图对象
         * @param gx 网格X坐标
         * @param gy 网格Y坐标
         *
         * 当地图的某个网格被加载到内存时触发
         */
        virtual void OnLoadGridMap(TMap* /*map*/, GridMap* /*gmap*/, uint32 /*gx*/, uint32 /*gy*/) { }

        /**
         * @brief 网格地图卸载时调用
         * @param map 地图对象
         * @param gmap 网格地图对象
         * @param gx 网格X坐标
         * @param gy 网格Y坐标
         *
         * 当地图的某个网格从内存中卸载时触发
         */
        virtual void OnUnloadGridMap(TMap* /*map*/, GridMap* /*gmap*/, uint32 /*gx*/, uint32 /*gy*/)  { }

        /**
         * @brief 玩家进入地图时调用
         * @param map 地图对象
         * @param player 进入的玩家
         */
        virtual void OnPlayerEnter(TMap* map, Player* player);

        /**
         * @brief 玩家离开地图时调用
         * @param map 地图对象
         * @param player 离开的玩家
         */
        virtual void OnPlayerLeave(TMap* map, Player* player);

        /**
         * @brief 地图更新时调用
         * @param map 地图对象
         * @param diff 距离上次更新的时间（毫秒）
         *
         * 每个地图tick都会调用
         * @warning 频率很高，不要执行耗时操作
         */
        virtual void OnUpdate(TMap* map, uint32 diff);
};

/**
 * @class WorldMapScript
 * @brief 世界地图脚本类
 *
 * 用于普通世界地图（非副本、非战场）的脚本。
 * 继承自ScriptObject和MapScript<Map>。
 *
 * 使用场景：
 * - 监控玩家在世界地图中的活动
 * - 自定义世界地图行为
 * - 区域性事件处理
 */
class TC_GAME_API WorldMapScript : public ScriptObject, public MapScript<Map>
{
    protected:
        /**
         * @brief 构造函数
         * @param name 脚本名称
         * @param mapId 地图ID
         */
        explicit WorldMapScript(char const* name, uint32 mapId);
};

/**
 * @class InstanceMapScript
 * @brief 副本地图脚本类
 *
 * 用于副本（Instance）地图的脚本。
 * 继承自ScriptObject和MapScript<InstanceMap>。
 *
 * 职责：
 * - 管理副本实例数据
 * - 处理副本进度和Boss击杀
 * - 控制副本内的事件
 *
 * 使用场景：
 * - 自定义副本逻辑
 * - Boss战斗脚本
 * - 副本成就和进度跟踪
 */
class TC_GAME_API InstanceMapScript : public ScriptObject, public MapScript<InstanceMap>
{
    protected:
        /**
         * @brief 构造函数
         * @param name 脚本名称
         * @param mapId 副本地图ID
         */
        explicit InstanceMapScript(char const* name, uint32 mapId);

    public:
        /**
         * @brief 获取副本脚本实例
         * @param map 副本地图对象
         * @return InstanceScript指针，用于管理副本数据
         *
         * 当副本创建时会调用此函数获取副本脚本实例。
         * 返回的InstanceScript将管理副本的进度、Boss状态等数据。
         */
        virtual InstanceScript* GetInstanceScript(InstanceMap* map) const;
};

/**
 * @class BattlegroundMapScript
 * @brief 战场地图脚本类
 *
 * 用于战场（Battleground）地图的脚本。
 * 继承自ScriptObject和MapScript<BattlegroundMap>。
 *
 * 使用场景：
 * - 自定义战场规则
 * - 战场事件处理
 * - 战场得分和胜负判定
 */
class TC_GAME_API BattlegroundMapScript : public ScriptObject, public MapScript<BattlegroundMap>
{
    protected:
        explicit BattlegroundMapScript(char const* name, uint32 mapId);
};

/**
 * @class ItemScript
 * @brief 物品脚本基类
 *
 * 提供物品相关的事件钩子，用于自定义物品行为。
 *
 * 使用场景：
 * - 特殊物品的使用效果
 * - 物品触发任务
 * - 物品消耗和过期处理
 * - 物品战斗法术触发
 */
class TC_GAME_API ItemScript : public ScriptObject
{
    protected:
        explicit ItemScript(char const* name);

    public:
        /**
         * @brief 玩家从物品接受任务时调用
         * @param player 接受任务的玩家
         * @param item 物品对象
         * @param quest 任务对象
         * @return true继续处理，false阻止接受
         */
        virtual bool OnQuestAccept(Player* player, Item* item, Quest const* quest);

        /**
         * @brief 玩家使用物品时调用
         * @param player 使用物品的玩家
         * @param item 物品对象
         * @param targets 施法目标
         * @return true继续处理，false阻止使用
         */
        virtual bool OnUse(Player* player, Item* item, SpellCastTargets const& targets);

        /**
         * @brief 物品过期时调用
         * @param player 物品所有者
         * @param proto 物品模板
         * @return true继续销毁，false阻止销毁
         */
        virtual bool OnExpire(Player* player, ItemTemplate const* proto);

        /**
         * @brief 物品被销毁时调用
         * @param player 物品所有者
         * @param item 物品对象
         * @return true继续处理，false阻止销毁
         */
        virtual bool OnRemove(Player* player, Item* item);

        /**
         * @brief 施放物品战斗法术前调用
         * @param player 施法者
         * @param victim 目标
         * @param spellInfo 法术信息
         * @param item 物品对象
         * @return true允许施放，false阻止施放
         *
         * 当物品模板有几率触发战斗法术时，在施放前调用。
         * 可用于阻止施放或记录日志。
         */
        virtual bool OnCastItemCombatSpell(Player* player, Unit* victim, SpellInfo const* spellInfo, Item* item);
};

/**
 * @class UnitScript
 * @brief 单位脚本基类
 *
 * 提供单位（Unit）相关的事件钩子，包括伤害和治疗的修改。
 * 单位是玩家和生物的基类。
 *
 * 使用场景：
 * - 修改伤害输出
 * - 修改治疗效果
 * - 自定义伤害吸收或减免
 * - 记录伤害日志
 *
 * 调用时机：伤害或治疗事件发生时
 * 性能注意事项：这些钩子会被频繁调用，需要优化性能
 */
class TC_GAME_API UnitScript : public ScriptObject
{
    protected:
        explicit UnitScript(char const* name);

    public:
        /**
         * @brief 单位进行治疗时调用
         * @param healer 治疗者
         * @param receiver 接受治疗者
         * @param gain 治疗量（可修改）
         */
        virtual void OnHeal(Unit* healer, Unit* reciever, uint32& gain);

        /**
         * @brief 单位造成伤害时调用
         * @param attacker 攻击者
         * @param victim 受害者
         * @param damage 伤害量（可修改）
         */
        virtual void OnDamage(Unit* attacker, Unit* victim, uint32& damage);

        /**
         * @brief 周期性伤害光环跳动时调用
         * @param target 目标
         * @param attacker 攻击者
         * @param damage 伤害量（可修改）
         *
         * 当DoT（持续伤害）法术造成周期性伤害时触发
         */
        virtual void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage);

        /**
         * @brief 近战伤害时调用
         * @param target 目标
         * @param attacker 攻击者
         * @param damage 伤害量（可修改）
         */
        virtual void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage);

        /**
         * @brief 法术伤害时调用
         * @param target 目标
         * @param attacker 攻击者
         * @param damage 伤害量（可修改）
         */
        virtual void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage);
};

/**
 * @class CreatureScript
 * @brief 生物脚本基类
 *
 * 用于为生物（Creature）创建AI对象。
 * 这是最常用的脚本类型之一，用于实现自定义的生物行为。
 *
 * 使用场景：
 * - 自定义生物AI
 * - Boss战斗逻辑
 * - NPC交互行为
 * - 生物巡逻和战斗策略
 *
 * 使用方式：
 * 1. 创建继承自CreatureScript的类
 * 2. 重写GetAI函数，返回自定义的CreatureAI实例
 * 3. 使用RegisterCreatureAI宏注册脚本
 */
class TC_GAME_API CreatureScript : public ScriptObject
{
    protected:
        explicit CreatureScript(char const* name);

    public:
        /**
         * @brief 获取生物的AI对象
         * @param creature 需要AI的生物对象
         * @return 新创建的CreatureAI指针
         *
         * 当生物需要一个AI对象时会调用此函数。
         * 必须由派生类实现，返回自定义的AI实例。
         *
         * @note 纯虚函数，必须重写
         * @note 返回的指针由核心管理，无需手动释放
         */
        virtual CreatureAI* GetAI(Creature* creature) const = 0;
};

/**
 * @class GameObjectScript
 * @brief 游戏对象脚本基类
 *
 * 用于为游戏对象（GameObject）创建AI对象。
 * 适用于需要自定义行为的游戏对象，如门、宝箱、机关等。
 *
 * 使用场景：
 * - 自定义游戏对象AI
 * - 交互式对象（宝箱、门等）
 * - 触发器和机关
 */
class TC_GAME_API GameObjectScript : public ScriptObject
{
    protected:
        explicit GameObjectScript(char const* name);

    public:
        /**
         * @brief 获取游戏对象的AI对象
         * @param go 需要AI的游戏对象
         * @return 新创建的GameObjectAI指针
         *
         * 当游戏对象需要一个AI对象时会调用此函数。
         * 必须由派生类实现。
         *
         * @note 纯虚函数，必须重写
         */
        virtual GameObjectAI* GetAI(GameObject* go) const = 0;
};

/**
 * @class AreaTriggerScript
 * @brief 区域触发器脚本基类
 *
 * 提供区域触发器（AreaTrigger）的事件钩子。
 * 区域触发器是地图上定义的隐形区域，玩家进入时会触发事件。
 *
 * 使用场景：
 * - 触发过场动画
 * - 传送玩家
 * - 触发脚本事件
 * - 区域性任务目标
 */
class TC_GAME_API AreaTriggerScript : public ScriptObject
{
    protected:
        explicit AreaTriggerScript(char const* name);

    public:
        /**
         * @brief 玩家激活区域触发器时调用
         * @param player 触发区域的玩家
         * @param trigger 区域触发器定义
         * @return true表示成功处理，false表示不处理
         */
        virtual bool OnTrigger(Player* player, AreaTriggerEntry const* trigger);
};

/**
 * @class OnlyOnceAreaTriggerScript
 * @brief 一次性区域触发器脚本类
 *
 * 继承自AreaTriggerScript，提供只触发一次的区域触发器功能。
 * 玩家每次进入只会触发一次，适合副本中的剧情触发等场景。
 */
class TC_GAME_API OnlyOnceAreaTriggerScript : public AreaTriggerScript
{
    using AreaTriggerScript::AreaTriggerScript;

    public:
        /**
         * @brief 触发事件（final，不可重写）
         * @param player 触发区域的玩家
         * @param trigger 区域触发器定义
         * @return 处理结果
         *
         * 内部会检查是否已触发，如果是则不再调用TryHandleOnce
         */
        bool OnTrigger(Player* /*player*/, AreaTriggerEntry const* /*trigger*/) final override;

    protected:
        /**
         * @brief 尝试一次性处理触发事件
         * @param player 触发区域的玩家
         * @param trigger 区域触发器定义
         * @return true表示成功处理，false表示应下次再尝试
         *
         * 由派生类实现具体的触发逻辑
         */
        virtual bool TryHandleOnce(Player* player, AreaTriggerEntry const* trigger) = 0;

        /**
         * @brief 重置副本中的触发器状态
         * @param instance 副本脚本
         * @param triggerId 触发器ID
         */
        void ResetAreaTriggerDone(InstanceScript* instance, uint32 triggerId);

        /**
         * @brief 重置玩家的触发器状态
         * @param player 玩家对象
         * @param trigger 触发器定义
         */
        void ResetAreaTriggerDone(Player const* player, AreaTriggerEntry const* trigger);
};

/**
 * @class BattlefieldScript
 * @brief 战场脚本基类
 *
 * 用于创建和管理战场（Battlefield）实例。
 * 战场是大型世界PvP区域，如冬拥湖。
 *
 * 使用场景：
 * - 自定义战场规则
 * - 战场资源争夺
 * - 战场事件管理
 */
class TC_GAME_API BattlefieldScript : public ScriptObject
{
    protected:
        explicit BattlefieldScript(char const* name);

    public:
        /**
         * @brief 获取战场对象
         * @return 战场实例指针
         */
        virtual Battlefield* GetBattlefield() const = 0;
};

/**
 * @class BattlegroundScript
 * @brief 战场脚本基类（竞技场和 battleground）
 *
 * 用于创建战场（Battleground）和竞技场实例。
 *
 * 使用场景：
 * - 自定义战场类型
 * - 竞技场规则
 * - 战场胜负判定
 */
class TC_GAME_API BattlegroundScript : public ScriptObject
{
    protected:
        explicit BattlegroundScript(char const* name);

    public:
        /**
         * @brief 获取战场对象
         * @return 战场实例指针
         *
         * 为特定战场类型ID创建战场实例
         */
        virtual Battleground* GetBattleground() const = 0;
};

/**
 * @class OutdoorPvPScript
 * @brief 世界PvP脚本基类
 *
 * 用于创建世界PvP（OutdoorPvP）实例。
 * 世界PvP是开放世界中的PvP活动，如塔楼争夺。
 *
 * 使用场景：
 * - 世界PvP区域控制
 * - 动态PvP事件
 */
class TC_GAME_API OutdoorPvPScript : public ScriptObject
{
    protected:
        explicit OutdoorPvPScript(char const* name);

    public:
        /**
         * @brief 获取世界PvP对象
         * @return 世界PvP实例指针
         */
        virtual OutdoorPvP* GetOutdoorPvP() const = 0;
};

/**
 * @class CommandScript
 * @brief 命令脚本基类
 *
 * 用于注册自定义GM命令和控制台命令。
 *
 * 使用场景：
 * - 添加自定义GM命令
 * - 服务器管理命令
 * - 调试工具命令
 */
class TC_GAME_API CommandScript : public ScriptObject
{
    protected:
        explicit CommandScript(char const* name);

    public:
        /**
         * @brief 获取命令表
         * @return 命令构建器数组
         *
         * 返回此脚本提供的所有命令定义。
         * ChatHandler会使用这些命令处理GM命令输入。
         */
        virtual std::vector<Trinity::ChatCommands::ChatCommandBuilder> GetCommands() const = 0;
};

/**
 * @class WeatherScript
 * @brief 天气脚本基类
 *
 * 提供天气变化相关的事件钩子。
 * 用于自定义天气行为和天气变化时的逻辑。
 *
 * 使用场景：
 * - 天气特效
 * - 天气相关事件
 * - 季节性内容
 */
class TC_GAME_API WeatherScript : public ScriptObject
{
    protected:
        explicit WeatherScript(char const* name);

    public:
        /**
         * @brief 天气变化时调用
         * @param weather 天气对象
         * @param state 天气状态（晴天、下雨、下雪等）
         * @param grade 天气强度（0-1）
         */
        virtual void OnChange(Weather* weather, WeatherState state, float grade);

        /**
         * @brief 天气更新时调用
         * @param weather 天气对象
         * @param diff 距离上次更新的时间（毫秒）
         */
        virtual void OnUpdate(Weather* weather, uint32 diff);
};

/**
 * @class AuctionHouseScript
 * @brief 拍卖行脚本基类
 *
 * 提供拍卖行相关的事件钩子。
 * 用于监控和管理拍卖行活动。
 *
 * 使用场景：
 * - 记录拍卖日志
 * - 限制拍卖行为
 * - 经济监控
 */
class TC_GAME_API AuctionHouseScript : public ScriptObject
{
    protected:
        explicit AuctionHouseScript(char const* name);

    public:
        /**
         * @brief 拍卖添加时调用
         * @param ah 拍卖行对象
         * @param entry 拍卖条目
         */
        virtual void OnAuctionAdd(AuctionHouseObject* ah, AuctionEntry* entry);

        /**
         * @brief 拍卖移除时调用
         * @param ah 拍卖行对象
         * @param entry 拍卖条目
         */
        virtual void OnAuctionRemove(AuctionHouseObject* ah, AuctionEntry* entry);

        /**
         * @brief 拍卖成功完成时调用
         * @param ah 拍卖行对象
         * @param entry 拍卖条目
         */
        virtual void OnAuctionSuccessful(AuctionHouseObject* ah, AuctionEntry* entry);

        /**
         * @brief 拍卖过期时调用
         * @param ah 拍卖行对象
         * @param entry 拍卖条目
         */
        virtual void OnAuctionExpire(AuctionHouseObject* ah, AuctionEntry* entry);
};

/**
 * @class ConditionScript
 * @brief 条件脚本基类
 *
 * 提供条件检查的钩子，用于自定义条件判断逻辑。
 * 条件系统用于控制各种游戏功能的前置条件。
 *
 * 使用场景：
 * - 自定义条件类型
 * - 复杂条件判断
 * - 条件日志记录
 */
class TC_GAME_API ConditionScript : public ScriptObject
{
    protected:
        explicit ConditionScript(char const* name);

    public:
        /**
         * @brief 条件检查时调用
         * @param condition 条件定义
         * @param sourceInfo 条件源信息
         * @return true表示条件满足，false表示不满足
         */
        virtual bool OnConditionCheck(Condition const* condition, ConditionSourceInfo& sourceInfo);
};

/**
 * @class VehicleScript
 * @brief 载具脚本基类
 *
 * 提供载具（Vehicle）相关的事件钩子。
 * 载具是可以骑乘和操作的物体，如车辆、飞行坐骑等。
 *
 * 使用场景：
 * - 载具安装和卸载
 * - 乘客管理
 * - 载具配件安装
 */
class TC_GAME_API VehicleScript : public ScriptObject
{
    protected:
        explicit VehicleScript(char const* name);

    public:
        /**
         * @brief 载具安装后调用
         * @param veh 载具对象
         */
        virtual void OnInstall(Vehicle* veh);

        /**
         * @brief 载具卸载后调用
         * @param veh 载具对象
         */
        virtual void OnUninstall(Vehicle* veh);

        /**
         * @brief 载具重置时调用
         * @param veh 载具对象
         */
        virtual void OnReset(Vehicle* veh);

        /**
         * @brief 安装载具配件后调用
         * @param veh 载具对象
         * @param accessory 配件生物
         */
        virtual void OnInstallAccessory(Vehicle* veh, Creature* accessory);

        /**
         * @brief 添加乘客后调用
         * @param veh 载具对象
         * @param passenger 乘客单位
         * @param seatId 座位ID
         */
        virtual void OnAddPassenger(Vehicle* veh, Unit* passenger, int8 seatId);

        /**
         * @brief 移除乘客后调用
         * @param veh 载具对象
         * @param passenger 乘客单位
         */
        virtual void OnRemovePassenger(Vehicle* veh, Unit* passenger);
};

/**
 * @class DynamicObjectScript
 * @brief 动态对象脚本基类
 *
 * 提供动态对象（DynamicObject）的更新钩子。
 * 动态对象是法术创建的临时对象，如AOE效果区域。
 *
 * 使用场景：
 * - 自定义动态对象行为
 * - 监控动态对象生命周期
 */
class TC_GAME_API DynamicObjectScript : public ScriptObject
{
    protected:
        explicit DynamicObjectScript(char const* name);

    public:
        /**
         * @brief 动态对象更新时调用
         * @param obj 动态对象
         * @param diff 距离上次更新的时间（毫秒）
         */
        virtual void OnUpdate(DynamicObject* obj, uint32 diff);
};

/**
 * @class TransportScript
 * @brief 交通运输工具脚本基类
 *
 * 提供交通运输工具（Transport）相关的事件钩子。
 * 交通工具包括船只、飞艇、电梯等可移动的游戏对象。
 *
 * 使用场景：
 * - 监控乘客上下车
 * - 交通工具移动事件
 * - 自定义交通工具行为
 */
class TC_GAME_API TransportScript : public ScriptObject
{
    protected:
        explicit TransportScript(char const* name);

    public:
        /**
         * @brief 玩家登上交通工具时调用
         * @param transport 交通工具对象
         * @param player 上车的玩家
         */
        virtual void OnAddPassenger(Transport* transport, Player* player);

        /**
         * @brief 生物登上交通工具时调用
         * @param transport 交通工具对象
         * @param creature 上车的生物
         */
        virtual void OnAddCreaturePassenger(Transport* transport, Creature* creature);

        /**
         * @brief 玩家离开交通工具时调用
         * @param transport 交通工具对象
         * @param player 下车的玩家
         */
        virtual void OnRemovePassenger(Transport* transport, Player* player);

        /**
         * @brief 交通工具移动时调用
         * @param transport 交通工具对象
         * @param waypointId 路点ID
         * @param mapId 地图ID
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         */
        virtual void OnRelocate(Transport* transport, uint32 waypointId, uint32 mapId, float x, float y, float z);

        /**
         * @brief 交通工具更新时调用
         * @param transport 交通工具对象
         * @param diff 距离上次更新的时间（毫秒）
         */
        virtual void OnUpdate(Transport* transport, uint32 diff);
};

/**
 * @class AchievementCriteriaScript
 * @brief 成就条件脚本基类
 *
 * 提供成就条件检查的钩子。
 * 用于实现自定义的成就条件类型。
 *
 * 使用场景：
 * - 自定义成就类型
 * - 特殊成就条件判断
 */
class TC_GAME_API AchievementCriteriaScript : public ScriptObject
{
    protected:
        explicit AchievementCriteriaScript(char const* name);

    public:
        /**
         * @brief 检查成就条件时调用
         * @param source 源玩家
         * @param target 目标单位（可能为null）
         * @return true表示条件满足，false表示不满足
         *
         * @note 纯虚函数，必须由派生类实现
         */
        virtual bool OnCheck(Player* source, Unit* target) = 0;
};

/**
 * @class PlayerScript
 * @brief 玩家脚本基类
 *
 * 提供玩家相关的大量事件钩子，是功能最丰富的脚本类型之一。
 * 几乎涵盖了玩家所有重要的生命周期事件和行为。
 *
 * 使用场景：
 * - 玩家行为监控
 * - 自定义玩家事件
 * - PvP/PvE事件处理
 * - 玩家数据统计
 * - 聊天消息处理
 * - 任务和副本事件
 *
 * 注意：
 * - 很多钩子会频繁调用，需要注意性能
 * - 部分钩子可以修改参数影响结果
 */
class TC_GAME_API PlayerScript : public ScriptObject
{
    protected:
        explicit PlayerScript(char const* name);

    public:
        // ==================== PvP事件 ====================

        /**
         * @brief 玩家击杀另一玩家时调用
         * @param killer 击杀者
         * @param killed 被击杀者
         */
        virtual void OnPVPKill(Player* killer, Player* killed);

        /**
         * @brief 玩家击杀生物时调用
         * @param killer 击杀者
         * @param killed 被击杀的生物
         */
        virtual void OnCreatureKill(Player* killer, Creature* killed);

        /**
         * @brief 玩家被生物击杀时调用
         * @param killer 击杀玩家的生物
         * @param killed 被击杀的玩家
         */
        virtual void OnPlayerKilledByCreature(Creature* killer, Player* killed);

        // ==================== 玩家属性变化 ====================

        /**
         * @brief 玩家等级变化后调用
         * @param player 玩家对象
         * @param oldLevel 旧等级
         *
         * 在等级已经应用后触发
         */
        virtual void OnLevelChanged(Player* player, uint8 oldLevel);

        /**
         * @brief 自由天赋点变化前调用
         * @param player 玩家对象
         * @param points 新的天赋点数
         *
         * 在天赋点变化即将应用前触发
         */
        virtual void OnFreeTalentPointsChanged(Player* player, uint32 points);

        /**
         * @brief 天赋重置前调用
         * @param player 玩家对象
         * @param involuntarily true表示非自愿重置（如技能调整）
         */
        virtual void OnTalentsReset(Player* player, bool involuntarily);

        /**
         * @brief 金钱变化前调用
         * @param player 玩家对象
         * @param amount 变化量（可修改，正数增加负数减少）
         */
        virtual void OnMoneyChanged(Player* player, int32& amount);

        /**
         * @brief 金钱达到上限时调用
         * @param player 玩家对象
         * @param amount 尝试添加的金钱数量
         */
        virtual void OnMoneyLimit(Player* player, int32 amount);

        /**
         * @brief 玩家获得经验前调用
         * @param player 玩家对象
         * @param amount 经验值（可修改）
         * @param victim 击杀的单位（可能为null）
         */
        virtual void OnGiveXP(Player* player, uint32& amount, Unit* victim);

        /**
         * @brief 声望变化前调用
         * @param player 玩家对象
         * @param factionId 声望阵营ID
         * @param standing 声望值（可修改）
         * @param incremental true表示增量变化，false表示绝对值设置
         */
        virtual void OnReputationChange(Player* player, uint32 factionId, int32& standing, bool incremental);

        // ==================== 决斗事件 ====================

        /**
         * @brief 决斗请求时调用
         * @param target 被挑战者
         * @param challenger 挑战者
         */
        virtual void OnDuelRequest(Player* target, Player* challenger);

        /**
         * @brief 决斗开始时调用
         * @param player1 玩家1
         * @param player2 玩家2
         *
         * 在3秒倒计时后决斗真正开始时触发
         */
        virtual void OnDuelStart(Player* player1, Player* player2);

        /**
         * @brief 决斗结束时调用
         * @param winner 胜者
         * @param loser 败者
         * @param type 决斗完成类型（胜利、逃跑等）
         */
        virtual void OnDuelEnd(Player* winner, Player* loser, DuelCompleteType type);

        // ==================== 聊天事件 ====================

        /**
         * @brief 玩家发送聊天消息时调用
         * @param player 玩家对象
         * @param type 消息类型
         * @param lang 语言类型
         * @param msg 消息内容（可修改）
         */
        virtual void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg);

        /** @brief 私聊消息 */
        virtual void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver);

        /** @brief 队伍消息 */
        virtual void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Group* group);

        /** @brief 公会消息 */
        virtual void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Guild* guild);

        /** @brief 频道消息 */
        virtual void OnChat(Player* player, uint32 type, uint32 lang, std::string& msg, Channel* channel);

        // ==================== 表情事件 ====================

        /**
         * @brief 玩家使用表情时调用
         * @param player 玩家对象
         * @param emote 表情类型
         */
        virtual void OnEmote(Player* player, Emote emote);

        /**
         * @brief 玩家使用文本表情时调用
         * @param player 玩家对象
         * @param textEmote 文本表情ID
         * @param emoteNum 表情编号
         * @param guid 目标GUID
         */
        virtual void OnTextEmote(Player* player, uint32 textEmote, uint32 emoteNum, ObjectGuid guid);

        // ==================== 法术事件 ====================

        /**
         * @brief 玩家施放法术时调用
         * @param player 玩家对象
         * @param spell 法术对象
         * @param skipCheck 是否跳过检查
         */
        virtual void OnSpellCast(Player* player, Spell* spell, bool skipCheck);

        // ==================== 登录登出事件 ====================

        /**
         * @brief 玩家登录时调用
         * @param player 玩家对象
         * @param firstLogin true表示首次登录（新角色）
         */
        virtual void OnLogin(Player* player, bool firstLogin);

        /**
         * @brief 玩家登出时调用
         * @param player 玩家对象
         */
        virtual void OnLogout(Player* player);

        /**
         * @brief 玩家创建时调用
         * @param player 新创建的玩家对象
         */
        virtual void OnCreate(Player* player);

        /**
         * @brief 玩家删除时调用
         * @param guid 玩家GUID
         * @param accountId 账号ID
         */
        virtual void OnDelete(ObjectGuid guid, uint32 accountId);

        /**
         * @brief 玩家删除失败时调用
         * @param guid 玩家GUID
         * @param accountId 账号ID
         */
        virtual void OnFailedDelete(ObjectGuid guid, uint32 accountId);

        /**
         * @brief 玩家保存前调用
         * @param player 玩家对象
         */
        virtual void OnSave(Player* player);

        // ==================== 地图和副本事件 ====================

        /**
         * @brief 玩家绑定到副本时调用
         * @param player 玩家对象
         * @param difficulty 难度
         * @param mapId 地图ID
         * @param permanent 是否永久绑定
         * @param extendState 延展状态
         */
        virtual void OnBindToInstance(Player* player, Difficulty difficulty, uint32 mapId, bool permanent, uint8 extendState);

        /**
         * @brief 玩家切换区域时调用
         * @param player 玩家对象
         * @param newZone 新区域ID
         * @param newArea 新子区域ID
         */
        virtual void OnUpdateZone(Player* player, uint32 newZone, uint32 newArea);

        /**
         * @brief 玩家切换地图后调用
         * @param player 玩家对象
         */
        virtual void OnMapChanged(Player* player);

        // ==================== 任务事件 ====================

        /**
         * @brief 玩家任务目标进度更新时调用
         * @param player 玩家对象
         * @param quest 任务对象
         * @param objectiveIndex 目标索引
         * @param progress 进度值
         */
        virtual void OnQuestObjectiveProgress(Player* /*player*/, Quest const* /*quest*/, uint32 /*objectiveIndex*/, uint16 /*progress*/) { }

        /**
         * @brief 玩家任务状态改变后调用
         * @param player 玩家对象
         * @param questId 任务ID
         */
        virtual void OnQuestStatusChange(Player* player, uint32 questId);

        // ==================== 其他事件 ====================

        /**
         * @brief 玩家死亡释放灵魂时调用
         * @param player 玩家对象
         */
        virtual void OnPlayerRepop(Player* player);

        /**
         * @brief 玩家观看电影完成时调用
         * @param player 玩家对象
         * @param movieId 电影ID
         */
        virtual void OnMovieComplete(Player* player, uint32 movieId);

};

/**
 * @class AccountScript
 * @brief 账号脚本基类
 *
 * 提供账号级别的事件钩子，用于监控账号活动。
 * 适用于账号管理、安全审计等场景。
 *
 * 使用场景：
 * - 登录日志记录
 * - 异常登录检测
 * - 账号信息变更追踪
 */
class TC_GAME_API AccountScript : public ScriptObject
{
    protected:
        explicit AccountScript(char const* name);

    public:
        /**
         * @brief 账号成功登录时调用
         * @param accountId 账号ID
         */
        virtual void OnAccountLogin(uint32 accountId);

        /**
         * @brief 账号登录失败时调用
         * @param accountId 账号ID
         */
        virtual void OnFailedAccountLogin(uint32 accountId);

        /**
         * @brief 账号邮箱成功修改时调用
         * @param accountId 账号ID
         */
        virtual void OnEmailChange(uint32 accountId);

        /**
         * @brief 账号邮箱修改失败时调用
         * @param accountId 账号ID
         */
        virtual void OnFailedEmailChange(uint32 accountId);

        /**
         * @brief 账号密码成功修改时调用
         * @param accountId 账号ID
         */
        virtual void OnPasswordChange(uint32 accountId);

        /**
         * @brief 账号密码修改失败时调用
         * @param accountId 账号ID
         */
        virtual void OnFailedPasswordChange(uint32 accountId);
};

/**
 * @class GuildScript
 * @brief 公会脚本基类
 *
 * 提供公会相关的事件钩子。
 * 用于监控和管理公会活动。
 *
 * 使用场景：
 * - 公会事件日志
 * - 公会银行监控
 * - 公会管理功能扩展
 */
class TC_GAME_API GuildScript : public ScriptObject
{
    protected:
        explicit GuildScript(char const* name);

    public:
        /**
         * @brief 成员加入公会时调用
         * @param guild 公会对象
         * @param player 加入的玩家
         * @param plRank 玩家等级（可修改）
         */
        virtual void OnAddMember(Guild* guild, Player* player, uint8& plRank);

        /**
         * @brief 成员离开公会时调用
         * @param guild 公会对象
         * @param player 离开的玩家
         * @param isDisbanding 是否正在解散公会
         * @param isKicked 是否被踢出
         */
        virtual void OnRemoveMember(Guild* guild, Player* player, bool isDisbanding, bool isKicked);

        /**
         * @brief 公会MOTD改变时调用
         * @param guild 公会对象
         * @param newMotd 新的每日消息
         */
        virtual void OnMOTDChanged(Guild* guild, std::string const& newMotd);

        /**
         * @brief 公会信息改变时调用
         * @param guild 公会对象
         * @param newInfo 新的公会信息
         */
        virtual void OnInfoChanged(Guild* guild, std::string const& newInfo);

        /**
         * @brief 公会创建时调用
         * @param guild 公会对象
         * @param leader 公会会长
         * @param name 公会名称
         */
        virtual void OnCreate(Guild* guild, Player* leader, std::string const& name);

        /**
         * @brief 公会解散时调用
         * @param guild 公会对象
         */
        virtual void OnDisband(Guild* guild);

        /**
         * @brief 成员从公会银行取钱时调用
         * @param guild 公会对象
         * @param player 取钱的玩家
         * @param amount 取款金额（可修改）
         * @param isRepair 是否用于修理
         */
        virtual void OnMemberWitdrawMoney(Guild* guild, Player* player, uint32& amount, bool isRepair);

        /**
         * @brief 成员向公会银行存钱时调用
         * @param guild 公会对象
         * @param player 存钱的玩家
         * @param amount 存款金额（可修改）
         */
        virtual void OnMemberDepositMoney(Guild* guild, Player* player, uint32& amount);

        /**
         * @brief 成员在公会银行移动物品时调用
         * @param guild 公会对象
         * @param player 玩家对象
         * @param pItem 物品对象
         * @param isSrcBank 源是否为银行
         * @param srcContainer 源容器
         * @param srcSlotId 源槽位ID
         * @param isDestBank 目标是否为银行
         * @param destContainer 目标容器
         * @param destSlotId 目标槽位ID
         */
        virtual void OnItemMove(Guild* guild, Player* player, Item* pItem, bool isSrcBank, uint8 srcContainer, uint8 srcSlotId,
            bool isDestBank, uint8 destContainer, uint8 destSlotId);

        /**
         * @brief 公会事件发生时调用
         * @param guild 公会对象
         * @param eventType 事件类型
         * @param playerGuid1 玩家GUID 1
         * @param playerGuid2 玩家GUID 2
         * @param newRank 新等级
         */
        virtual void OnEvent(Guild* guild, uint8 eventType, ObjectGuid::LowType playerGuid1, ObjectGuid::LowType playerGuid2, uint8 newRank);

        /**
         * @brief 公会银行事件发生时调用
         * @param guild 公会对象
         * @param eventType 事件类型
         * @param tabId 标签页ID
         * @param playerGuid 玩家GUID
         * @param itemOrMoney 物品或金钱
         * @param itemStackCount 物品堆叠数
         * @param destTabId 目标标签页ID
         */
        virtual void OnBankEvent(Guild* guild, uint8 eventType, uint8 tabId, ObjectGuid::LowType playerGuid, uint32 itemOrMoney, uint16 itemStackCount, uint8 destTabId);
};

/**
 * @class GroupScript
 * @brief 队伍脚本基类
 *
 * 提供队伍（小队和团队）相关的事件钩子。
 * 用于监控队伍活动和管理。
 *
 * 使用场景：
 * - 队伍事件日志
 * - 自动队伍管理
 * - 队伍匹配系统扩展
 */
class TC_GAME_API GroupScript : public ScriptObject
{
    protected:
        explicit GroupScript(char const* name);

    public:
        /**
         * @brief 成员加入队伍时调用
         * @param group 队伍对象
         * @param guid 成员GUID
         */
        virtual void OnAddMember(Group* group, ObjectGuid guid);

        /**
         * @brief 成员被邀请加入队伍时调用
         * @param group 队伍对象
         * @param guid 被邀请者GUID
         */
        virtual void OnInviteMember(Group* group, ObjectGuid guid);

        /**
         * @brief 成员离开队伍时调用
         * @param group 队伍对象
         * @param guid 成员GUID
         * @param method 离开方式
         * @param kicker 踢人者GUID
         * @param reason 离开原因
         */
        virtual void OnRemoveMember(Group* group, ObjectGuid guid, RemoveMethod method, ObjectGuid kicker, char const* reason);

        /**
         * @brief 队伍队长变更时调用
         * @param group 队伍对象
         * @param newLeaderGuid 新队长GUID
         * @param oldLeaderGuid 旧队长GUID
         */
        virtual void OnChangeLeader(Group* group, ObjectGuid newLeaderGuid, ObjectGuid oldLeaderGuid);

        /**
         * @brief 队伍解散时调用
         * @param group 队伍对象
         */
        virtual void OnDisband(Group* group);
};

/**
 * @class ScriptMgr
 * @brief 脚本管理器（单例模式）
 *
 * ScriptMgr是TrinityCore脚本系统的核心管理器，负责：
 * - 脚本的注册、加载和管理
 * - 脚本上下文的管理（支持动态脚本库）
 * - 提供脚本调用的接口函数
 * - 脚本生命周期的管理
 *
 * 架构说明：
 * - 采用单例模式，全局唯一实例
 * - 使用ScriptRegistry模板类管理各类型脚本
 * - 支持脚本上下文，实现动态脚本库的加载和卸载
 *
 * 使用方式：
 * - 通过sScriptMgr宏访问单例
 * - 在服务器启动时调用Initialize()初始化
 * - 通过各种OnXXX函数触发脚本事件
 *
 * 性能注意事项：
 * - 大部分函数会遍历所有注册的脚本，避免在热路径中频繁调用
 * - 脚本数量过多会影响调用性能
 */
class TC_GAME_API ScriptMgr
{
    friend class ScriptObject;  // 允许ScriptObject访问私有成员

    private:
        /** @brief 私有构造函数（单例模式） */
        ScriptMgr();
        /** @brief 虚析构函数 */
        virtual ~ScriptMgr();

        /**
         * @brief 填充法术摘要信息
         *
         * 初始化法术相关的摘要数据，用于快速查询
         */
        void FillSpellSummary();

        /**
         * @brief 从数据库加载脚本数据
         *
         * 加载与脚本关联的数据库数据（如法术脚本关联）
         */
        void LoadDatabase();

        /**
         * @brief 增加脚本计数
         */
        void IncreaseScriptCount() { ++_scriptCount; }

        /**
         * @brief 减少脚本计数
         */
        void DecreaseScriptCount() { --_scriptCount; }

    public: /* ==================== 初始化 ==================== */

        /**
         * @brief 获取单例实例
         * @return ScriptMgr单例指针
         */
        static ScriptMgr* instance();

        /**
         * @brief 初始化脚本系统
         *
         * 在服务器启动时调用，完成脚本系统的初始化工作。
         * 包括加载数据库数据、填充摘要信息等。
         */
        void Initialize();

        /**
         * @brief 获取已注册的脚本数量
         * @return 脚本总数
         */
        uint32 GetScriptCount() const { return _scriptCount; }

        /** @brief 脚本加载器回调函数类型 */
        typedef void(*ScriptLoaderCallbackType)();

        /**
         * @brief 设置脚本加载器回调函数
         * @param script_loader_callback 回调函数指针
         *
         * 用于解决game模块和scripts模块之间的循环依赖问题。
         * 脚本加载器在脚本库中定义，由ScriptMgr在需要时调用。
         */
        void SetScriptLoader(ScriptLoaderCallbackType script_loader_callback)
        {
            _script_loader_callback = script_loader_callback;
        }

    public: /* ==================== 脚本上下文管理 ==================== */

        /**
         * @brief 设置当前脚本上下文
         * @param context 上下文名称
         *
         * 设置当前脚本上下文，允许ScriptMgr在该上下文中接受新脚本。
         * 需要随后调用SwapScriptContext()来加载新脚本。
         *
         * 脚本上下文用于支持动态脚本库（shared library）的加载和卸载，
         * 每个动态库可以有自己的上下文，实现脚本的模块化管理。
         */
        void SetScriptContext(std::string const& context);

        /**
         * @brief 获取当前脚本上下文
         * @return 当前上下文名称
         */
        std::string const& GetCurrentScriptContext() const { return _currentContext; }

        /**
         * @brief 释放指定上下文的所有脚本
         * @param context 要释放的上下文名称
         *
         * 立即释放与给定上下文关联的所有脚本。
         * 需要随后调用SwapScriptContext()完成卸载流程。
         */
        void ReleaseScriptContext(std::string const& context);

        /**
         * @brief 执行上下文更改
         * @param initialize 是否为初始化模式
         *
         * 执行所有由SetScriptContext和ReleaseScriptContext引入的更改。
         * 可以组合多个Set和Release调用以提高性能（批量处理）。
         */
        void SwapScriptContext(bool initialize = false);

        /**
         * @brief 获取静态上下文的名称
         * @return 静态上下文名称
         *
         * 静态上下文是worldserver提供的默认上下文，包含编译时链接的脚本
         */
        static std::string const& GetNameOfStaticContext();

        /**
         * @brief 获取脚本名称对应的模块引用
         * @param scriptname 脚本名称
         * @return 模块引用的shared_ptr
         *
         * 获取包含指定脚本的模块的强引用，防止包含该脚本的共享库被卸载。
         * 当所有引用都被释放后，共享库会被延迟卸载。
         */
        std::shared_ptr<ModuleReference> AcquireModuleReferenceOfScriptName(
            std::string const& scriptname) const;

    public: /* ==================== 卸载 ==================== */

        /**
         * @brief 卸载所有脚本
         *
         * 卸载并清理所有已注册的脚本，在服务器关闭时调用
         */
        void Unload();

    public: /* ==================== SpellScriptLoader 接口 ==================== */

        /**
         * @brief 创建法术脚本实例
         * @param spellId 法术ID
         * @param scriptVector 脚本实例向量（输出）
         * @param invoker 触发者（法术对象）
         *
         * 为指定法术创建所有关联的SpellScript实例
         */
        void CreateSpellScripts(uint32 spellId, std::vector<SpellScript*>& scriptVector, Spell* invoker) const;

        /**
         * @brief 创建光环脚本实例
         * @param spellId 法术ID
         * @param scriptVector 脚本实例向量（输出）
         * @param invoker 触发者（光环对象）
         *
         * 为指定光环创建所有关联的AuraScript实例
         */
        void CreateAuraScripts(uint32 spellId, std::vector<AuraScript*>& scriptVector, Aura* invoker) const;

        /**
         * @brief 获取法术脚本加载器
         * @param scriptId 脚本ID
         * @return SpellScriptLoader指针
         */
        SpellScriptLoader* GetSpellScriptLoader(uint32 scriptId);

    public: /* ==================== ServerScript 接口 ==================== */

        void OnNetworkStart();
        void OnNetworkStop();
        void OnSocketOpen(std::shared_ptr<WorldSocket> socket);
        void OnSocketClose(std::shared_ptr<WorldSocket> socket);
        void OnPacketReceive(WorldSession* session, WorldPacket const& packet);
        void OnPacketSend(WorldSession* session, WorldPacket const& packet);

    public: /* ==================== WorldScript 接口 ==================== */

        void OnOpenStateChange(bool open);
        void OnConfigLoad(bool reload);
        void OnMotdChange(std::string& newMotd);
        void OnShutdownInitiate(ShutdownExitCode code, ShutdownMask mask);
        void OnShutdownCancel();
        void OnWorldUpdate(uint32 diff);
        void OnStartup();
        void OnShutdown();

    public: /* ==================== FormulaScript 接口 ==================== */

        void OnHonorCalculation(float& honor, uint8 level, float multiplier);
        void OnGrayLevelCalculation(uint8& grayLevel, uint8 playerLevel);
        void OnColorCodeCalculation(XPColorChar& color, uint8 playerLevel, uint8 mobLevel);
        void OnZeroDifferenceCalculation(uint8& diff, uint8 playerLevel);
        void OnBaseGainCalculation(uint32& gain, uint8 playerLevel, uint8 mobLevel, ContentLevels content);
        void OnGainCalculation(uint32& gain, Player* player, Unit* unit);
        void OnGroupRateCalculation(float& rate, uint32 count, bool isRaid);

    public: /* ==================== MapScript 接口 ==================== */

        void OnCreateMap(Map* map);
        void OnDestroyMap(Map* map);
        void OnLoadGridMap(Map* map, GridMap* gmap, uint32 gx, uint32 gy);
        void OnUnloadGridMap(Map* map, GridMap* gmap, uint32 gx, uint32 gy);
        void OnPlayerEnterMap(Map* map, Player* player);
        void OnPlayerLeaveMap(Map* map, Player* player);
        void OnMapUpdate(Map* map, uint32 diff);

    public: /* ==================== InstanceMapScript 接口 ==================== */

        /**
         * @brief 创建副本数据实例
         * @param map 副本地图对象
         * @return InstanceScript指针，用于管理副本数据
         */
        InstanceScript* CreateInstanceData(InstanceMap* map);

    public: /* ==================== ItemScript 接口 ==================== */

        bool OnQuestAccept(Player* player, Item* item, Quest const* quest);
        bool OnItemUse(Player* player, Item* item, SpellCastTargets const& targets);
        bool OnItemExpire(Player* player, ItemTemplate const* proto);
        bool OnItemRemove(Player* player, Item* item);
        bool OnCastItemCombatSpell(Player* player, Unit* victim, SpellInfo const* spellInfo, Item* item);

    public: /* ==================== CreatureScript 接口 ==================== */

        /**
         * @brief 获取生物AI
         * @param creature 生物对象
         * @return CreatureAI指针
         *
         * 遍历所有CreatureScript，找到第一个能为此生物提供AI的脚本
         */
        CreatureAI* GetCreatureAI(Creature* creature);

    public: /* ==================== GameObjectScript 接口 ==================== */

        /**
         * @brief 获取游戏对象AI
         * @param go 游戏对象
         * @return GameObjectAI指针
         */
        GameObjectAI* GetGameObjectAI(GameObject* go);

    public: /* ==================== AreaTriggerScript 接口 ==================== */

        bool OnAreaTrigger(Player* player, AreaTriggerEntry const* trigger);

    public: /* ==================== BattlefieldScript 接口 ==================== */

        Battlefield* CreateBattlefield(uint32 scriptId);

    public: /* ==================== BattlegroundScript 接口 ==================== */

        Battleground* CreateBattleground(BattlegroundTypeId typeId);

    public: /* ==================== OutdoorPvPScript 接口 ==================== */

        OutdoorPvP* CreateOutdoorPvP(uint32 scriptId);

    public: /* ==================== CommandScript 接口 ==================== */

        std::vector<Trinity::ChatCommands::ChatCommandBuilder> GetChatCommands();

    public: /* ==================== WeatherScript 接口 ==================== */

        void OnWeatherChange(Weather* weather, WeatherState state, float grade);
        void OnWeatherUpdate(Weather* weather, uint32 diff);

    public: /* ==================== AuctionHouseScript 接口 ==================== */

        void OnAuctionAdd(AuctionHouseObject* ah, AuctionEntry* entry);
        void OnAuctionRemove(AuctionHouseObject* ah, AuctionEntry* entry);
        void OnAuctionSuccessful(AuctionHouseObject* ah, AuctionEntry* entry);
        void OnAuctionExpire(AuctionHouseObject* ah, AuctionEntry* entry);

    public: /* ==================== ConditionScript 接口 ==================== */

        bool OnConditionCheck(Condition const* condition, ConditionSourceInfo& sourceInfo);

    public: /* ==================== VehicleScript 接口 ==================== */

        void OnInstall(Vehicle* veh);
        void OnUninstall(Vehicle* veh);
        void OnReset(Vehicle* veh);
        void OnInstallAccessory(Vehicle* veh, Creature* accessory);
        void OnAddPassenger(Vehicle* veh, Unit* passenger, int8 seatId);
        void OnRemovePassenger(Vehicle* veh, Unit* passenger);

    public: /* ==================== DynamicObjectScript 接口 ==================== */

        void OnDynamicObjectUpdate(DynamicObject* dynobj, uint32 diff);

    public: /* ==================== TransportScript 接口 ==================== */

        void OnAddPassenger(Transport* transport, Player* player);
        void OnAddCreaturePassenger(Transport* transport, Creature* creature);
        void OnRemovePassenger(Transport* transport, Player* player);
        void OnTransportUpdate(Transport* transport, uint32 diff);
        void OnRelocate(Transport* transport, uint32 waypointId, uint32 mapId, float x, float y, float z);

    public: /* ==================== AchievementCriteriaScript 接口 ==================== */

        bool OnCriteriaCheck(uint32 scriptId, Player* source, Unit* target);

    public: /* ==================== PlayerScript 接口 ==================== */

        void OnPVPKill(Player* killer, Player* killed);
        void OnCreatureKill(Player* killer, Creature* killed);
        void OnPlayerKilledByCreature(Creature* killer, Player* killed);
        void OnPlayerLevelChanged(Player* player, uint8 oldLevel);
        void OnPlayerFreeTalentPointsChanged(Player* player, uint32 newPoints);
        void OnPlayerTalentsReset(Player* player, bool involuntarily);
        void OnPlayerMoneyChanged(Player* player, int32& amount);
        void OnPlayerMoneyLimit(Player* player, int32 amount);
        void OnGivePlayerXP(Player* player, uint32& amount, Unit* victim);
        void OnPlayerReputationChange(Player* player, uint32 factionID, int32& standing, bool incremental);
        void OnPlayerDuelRequest(Player* target, Player* challenger);
        void OnPlayerDuelStart(Player* player1, Player* player2);
        void OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type);
        void OnPlayerChat(Player* player, uint32 type, uint32 lang, std::string& msg);
        void OnPlayerChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver);
        void OnPlayerChat(Player* player, uint32 type, uint32 lang, std::string& msg, Group* group);
        void OnPlayerChat(Player* player, uint32 type, uint32 lang, std::string& msg, Guild* guild);
        void OnPlayerChat(Player* player, uint32 type, uint32 lang, std::string& msg, Channel* channel);
        void OnPlayerEmote(Player* player, Emote emote);
        void OnPlayerTextEmote(Player* player, uint32 textEmote, uint32 emoteNum, ObjectGuid guid);
        void OnPlayerSpellCast(Player* player, Spell* spell, bool skipCheck);
        void OnPlayerLogin(Player* player, bool firstLogin);
        void OnPlayerLogout(Player* player);
        void OnPlayerCreate(Player* player);
        void OnPlayerDelete(ObjectGuid guid, uint32 accountId);
        void OnPlayerFailedDelete(ObjectGuid guid, uint32 accountId);
        void OnPlayerSave(Player* player);
        void OnPlayerBindToInstance(Player* player, Difficulty difficulty, uint32 mapid, bool permanent, uint8 extendState);
        void OnPlayerUpdateZone(Player* player, uint32 newZone, uint32 newArea);
        void OnQuestObjectiveProgress(Player* player, Quest const* quest, uint32 objectiveIndex, uint16 progress);
        void OnQuestStatusChange(Player* player, uint32 questId);
        void OnMovieComplete(Player* player, uint32 movieId);
        void OnPlayerRepop(Player* player);

    public: /* ==================== AccountScript 接口 ==================== */

        void OnAccountLogin(uint32 accountId);
        void OnFailedAccountLogin(uint32 accountId);
        void OnEmailChange(uint32 accountId);
        void OnFailedEmailChange(uint32 accountId);
        void OnPasswordChange(uint32 accountId);
        void OnFailedPasswordChange(uint32 accountId);

    public: /* ==================== GuildScript 接口 ==================== */

        void OnGuildAddMember(Guild* guild, Player* player, uint8& plRank);
        void OnGuildRemoveMember(Guild* guild, Player* player, bool isDisbanding, bool isKicked);
        void OnGuildMOTDChanged(Guild* guild, const std::string& newMotd);
        void OnGuildInfoChanged(Guild* guild, const std::string& newInfo);
        void OnGuildCreate(Guild* guild, Player* leader, const std::string& name);
        void OnGuildDisband(Guild* guild);
        void OnGuildMemberWitdrawMoney(Guild* guild, Player* player, uint32 &amount, bool isRepair);
        void OnGuildMemberDepositMoney(Guild* guild, Player* player, uint32 &amount);
        void OnGuildItemMove(Guild* guild, Player* player, Item* pItem, bool isSrcBank, uint8 srcContainer, uint8 srcSlotId,
            bool isDestBank, uint8 destContainer, uint8 destSlotId);
        void OnGuildEvent(Guild* guild, uint8 eventType, ObjectGuid::LowType playerGuid1, ObjectGuid::LowType playerGuid2, uint8 newRank);
        void OnGuildBankEvent(Guild* guild, uint8 eventType, uint8 tabId, ObjectGuid::LowType playerGuid, uint32 itemOrMoney, uint16 itemStackCount, uint8 destTabId);

    public: /* ==================== GroupScript 接口 ==================== */

        void OnGroupAddMember(Group* group, ObjectGuid guid);
        void OnGroupInviteMember(Group* group, ObjectGuid guid);
        void OnGroupRemoveMember(Group* group, ObjectGuid guid, RemoveMethod method, ObjectGuid kicker, char const* reason);
        void OnGroupChangeLeader(Group* group, ObjectGuid newLeaderGuid, ObjectGuid oldLeaderGuid);
        void OnGroupDisband(Group* group);

    public: /* ==================== UnitScript 接口 ==================== */

        void OnHeal(Unit* healer, Unit* reciever, uint32& gain);
        void OnDamage(Unit* attacker, Unit* victim, uint32& damage);
        void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage);
        void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage);
        void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage);

    private:
        /** @brief 已注册的脚本总数 */
        uint32 _scriptCount;

        /** @brief 脚本加载器回调函数 */
        ScriptLoaderCallbackType _script_loader_callback;

        /** @brief 当前脚本上下文名称 */
        std::string _currentContext;
};

// ==================== 脚本类型特征（Type Traits） ====================

namespace Trinity::SpellScripts
{
    /** @brief 判断类型T是否为SpellScript的派生类 */
    template<typename T>
    using is_SpellScript = std::is_base_of<SpellScript, T>;

    /** @brief 判断类型T是否为AuraScript的派生类 */
    template<typename T>
    using is_AuraScript = std::is_base_of<AuraScript, T>;
}

/**
 * @class GenericSpellAndAuraScriptLoader
 * @brief 通用法术和光环脚本加载器模板类
 *
 * 这是一个可变参数模板类，用于简化法术脚本的注册。
 * 支持同时为同一个法术注册SpellScript和AuraScript。
 *
 * 模板参数：
 * - Ts...: 脚本类型列表，可以包含SpellScript、AuraScript和构造参数tuple
 *
 * 使用场景：
 * - 快速注册简单的法术脚本
 * - 需要传递构造参数的脚本
 */
template <typename... Ts>
class GenericSpellAndAuraScriptLoader : public SpellScriptLoader
{
    /** @brief 从Ts...中找到SpellScript类型 */
    using SpellScriptType = typename Trinity::find_type_if_t<Trinity::SpellScripts::is_SpellScript, Ts...>;
    /** @brief 从Ts...中找到AuraScript类型 */
    using AuraScriptType = typename Trinity::find_type_if_t<Trinity::SpellScripts::is_AuraScript, Ts...>;
    /** @brief 从Ts...中找到tuple参数类型 */
    using ArgsType = typename Trinity::find_type_if_t<Trinity::is_tuple, Ts...>;

public:
    /**
     * @brief 构造函数
     * @param name 脚本名称
     * @param args 构造参数（tuple）
     */
    GenericSpellAndAuraScriptLoader(char const* name, ArgsType&& args) : SpellScriptLoader(name), _args(std::move(args)) { }

private:
    /**
     * @brief 创建SpellScript实例
     * @return SpellScript指针，如果不支持则返回nullptr
     */
    SpellScript* GetSpellScript() const override
    {
        if constexpr (!std::is_same_v<SpellScriptType, Trinity::find_type_end>)
            return Trinity::new_from_tuple<SpellScriptType>(_args);
        else
            return nullptr;
    }

    /**
     * @brief 创建AuraScript实例
     * @return AuraScript指针，如果不支持则返回nullptr
     */
    AuraScript* GetAuraScript() const override
    {
        if constexpr (!std::is_same_v<AuraScriptType, Trinity::find_type_end>)
            return Trinity::new_from_tuple<AuraScriptType>(_args);
        else
            return nullptr;
    }

    /** @brief 构造参数tuple */
    ArgsType _args;
};

// ==================== 脚本注册宏 ====================

/**
 * @brief 注册带参数的法术脚本
 * @param spell_script 脚本类名
 * @param script_name 脚本名称
 * @param ... 构造参数
 *
 * 使用示例：
 * RegisterSpellScriptWithArgs(MySpellScript, "spell_my_spell", 100, 200);
 */
#define RegisterSpellScriptWithArgs(spell_script, script_name, ...) new GenericSpellAndAuraScriptLoader<spell_script, decltype(std::make_tuple(__VA_ARGS__))>(script_name, std::make_tuple(__VA_ARGS__))

/**
 * @brief 注册法术脚本（自动使用类名作为脚本名）
 * @param spell_script 脚本类名
 *
 * 使用示例：
 * RegisterSpellScript(MySpellScript);
 */
#define RegisterSpellScript(spell_script) RegisterSpellScriptWithArgs(spell_script, #spell_script)

/**
 * @brief 注册法术和光环脚本对（带参数）
 * @param script_1 SpellScript类型
 * @param script_2 AuraScript类型
 * @param script_name 脚本名称
 * @param ... 构造参数
 */
#define RegisterSpellAndAuraScriptPairWithArgs(script_1, script_2, script_name, ...) new GenericSpellAndAuraScriptLoader<script_1, script_2, decltype(std::make_tuple(__VA_ARGS__))>(script_name, std::make_tuple(__VA_ARGS__))

/**
 * @brief 注册法术和光环脚本对（自动使用第一个脚本类名）
 * @param script_1 SpellScript类型
 * @param script_2 AuraScript类型
 */
#define RegisterSpellAndAuraScriptPair(script_1, script_2) RegisterSpellAndAuraScriptPairWithArgs(script_1, script_2, #script_1)

// ==================== 生物AI脚本模板 ====================

/**
 * @class GenericCreatureScript
 * @brief 通用生物脚本模板类
 *
 * 用于快速注册生物AI脚本。
 *
 * 模板参数：
 * - AI: CreatureAI的派生类类型
 */
template <class AI>
class GenericCreatureScript : public CreatureScript
{
    public:
        GenericCreatureScript(char const* name) : CreatureScript(name) { }
        CreatureAI* GetAI(Creature* me) const override { return new AI(me); }
};

/**
 * @brief 注册生物AI脚本
 * @param ai_name AI类名
 *
 * 使用示例：
 * RegisterCreatureAI(MyBossAI);
 */
#define RegisterCreatureAI(ai_name) new GenericCreatureScript<ai_name>(#ai_name)

/**
 * @class FactoryCreatureScript
 * @brief 工厂模式的生物脚本模板类
 *
 * 用于使用工厂函数创建AI实例的场景。
 */
template <class AI, AI* (*AIFactory)(Creature*)>
class FactoryCreatureScript : public CreatureScript
{
    public:
        FactoryCreatureScript(char const* name) : CreatureScript(name) { }
        CreatureAI* GetAI(Creature* me) const override { return AIFactory(me); }
};

/**
 * @brief 使用工厂函数注册生物AI
 * @param ai_name AI类名
 * @param factory_fn 工厂函数名
 */
#define RegisterCreatureAIWithFactory(ai_name, factory_fn) new FactoryCreatureScript<ai_name, &factory_fn>(#ai_name)

// ==================== 游戏对象AI脚本模板 ====================

/**
 * @class GenericGameObjectScript
 * @brief 通用游戏对象脚本模板类
 *
 * 用于快速注册游戏对象AI脚本。
 */
template <class AI>
class GenericGameObjectScript : public GameObjectScript
{
    public:
        GenericGameObjectScript(char const* name) : GameObjectScript(name) { }
        GameObjectAI* GetAI(GameObject* go) const override { return new AI(go); }
};

/**
 * @brief 注册游戏对象AI脚本
 * @param ai_name AI类名
 */
#define RegisterGameObjectAI(ai_name) new GenericGameObjectScript<ai_name>(#ai_name)

/**
 * @class FactoryGameObjectScript
 * @brief 工厂模式的游戏对象脚本模板类
 */
template <class AI, AI* (*AIFactory)(GameObject*)>
class FactoryGameObjectScript : public GameObjectScript
{
    public:
        FactoryGameObjectScript(char const* name) : GameObjectScript(name) { }
        GameObjectAI* GetAI(GameObject* me) const override { return AIFactory(me); }
};

/**
 * @brief 使用工厂函数注册游戏对象AI
 * @param ai_name AI类名
 * @param factory_fn 工厂函数名
 */
#define RegisterGameObjectAIWithFactory(ai_name, factory_fn) new FactoryGameObjectScript<ai_name, &factory_fn>(#ai_name)

/** @brief ScriptMgr单例访问宏 */
#define sScriptMgr ScriptMgr::instance()

#endif
