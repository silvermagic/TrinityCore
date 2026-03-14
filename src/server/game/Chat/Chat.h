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
 * @file Chat.h
 * @brief 聊天系统核心头文件
 *
 * 本文件定义了聊天系统的核心类,包括:
 * - ChatHandler: 聊天命令处理器基类,负责命令解析和消息发送
 * - CliHandler: 控制台命令处理器,用于服务器控制台
 * - AddonChannelCommandHandler: 插件频道命令处理器
 *
 * 这些类提供了游戏内外聊天命令的统一处理框架,支持:
 * - 命令解析和执行
 * - 权限验证
 * - 消息格式化和发送
 * - 多种输出目标(玩家、控制台、插件频道)
 */

#ifndef TRINITYCORE_CHAT_H
#define TRINITYCORE_CHAT_H

#include "Common.h"
#include "ChatCommand.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include <fmt/printf.h>
#include <string>
#include <vector>

class ChatHandler;
class Creature;
class GameObject;
class Group;
class Player;
class Unit;
class WorldSession;
class WorldObject;
class WorldPacket;

struct GameTele;

/**
 * @class ChatHandler
 * @brief 聊天命令处理器基类
 *
 * 提供聊天命令的处理框架,包括:
 * - 命令解析和执行
 * - 消息发送(系统消息、全局消息等)
 * - 权限验证
 * - 目标选择
 * - 链接解析
 *
 * 可用于游戏内聊天命令(.command格式)和控制台命令
 */
class TC_GAME_API ChatHandler
{
    public:
        /**
         * @brief 判断是否为控制台会话
         * @return true 如果是控制台,false 如果是游戏内会话
         */
        bool IsConsole() const { return (m_session == nullptr); }

        /**
         * @brief 获取世界会话对象
         * @return WorldSession* 世界会话指针
         */
        WorldSession* GetSession() { return m_session; }
        WorldSession const* GetSession() const { return m_session; }

        /**
         * @brief 获取当前玩家对象
         * @return Player* 玩家指针,如果是控制台则返回nullptr
         */
        Player* GetPlayer() const;

        /**
         * @brief 构造函数
         * @param session 世界会话指针(游戏内)或nullptr(控制台)
         */
        explicit ChatHandler(WorldSession* session) : m_session(session), sentErrorMessage(false) { }
        virtual ~ChatHandler() { }

        /**
         * @brief 构建聊天数据包(使用GUID)
         *
         * 构建聊天消息数据包,返回接收者GUID的位置以便后续替换
         *
         * @param data 输出的数据包
         * @param chatType 聊天类型
         * @param language 语言
         * @param senderGUID 发送者GUID
         * @param receiverGUID 接收者GUID
         * @param message 消息内容
         * @param chatTag 聊天标签
         * @param senderName 发送者名称
         * @param receiverName 接收者名称
         * @param achievementId 成就ID
         * @param gmMessage 是否为GM消息
         * @param channelName 频道名称
         * @return size_t 接收者GUID在数据包中的位置
         */
        static size_t BuildChatPacket(WorldPacket& data, ChatMsg chatType, Language language, ObjectGuid senderGUID, ObjectGuid receiverGUID, std::string_view message, uint8 chatTag,
                                    std::string const& senderName = "", std::string const& receiverName = "",
                                    uint32 achievementId = 0, bool gmMessage = false, std::string const& channelName = "");

        /**
         * @brief 构建聊天数据包(使用世界对象)
         *
         * 构建聊天消息数据包,自动从世界对象提取信息
         *
         * @param data 输出的数据包
         * @param chatType 聊天类型
         * @param language 语言
         * @param sender 发送者对象
         * @param receiver 接收者对象
         * @param message 消息内容
         * @param achievementId 成就ID
         * @param channelName 频道名称
         * @param locale 语言区域设置
         * @return size_t 接收者GUID在数据包中的位置
         */
        static size_t BuildChatPacket(WorldPacket& data, ChatMsg chatType, Language language, WorldObject const* sender, WorldObject const* receiver, std::string_view message, uint32 achievementId = 0, std::string const& channelName = "", LocaleConstant locale = DEFAULT_LOCALE);

        /**
         * @brief 从消息中提取一行
         * @param pos 消息位置指针
         * @return char* 一行消息的起始位置
         */
        static char* LineFromMessage(char*& pos) { char* start = strtok(pos, "\n"); pos = nullptr; return start; }

        // 聊天/控制台不同实现的函数

        /**
         * @brief 获取Trinity字符串
         * @param entry 字符串条目ID
         * @return char const* 本地化字符串
         */
        virtual char const* GetTrinityString(uint32 entry) const;

        /**
         * @brief 发送系统消息
         *
         * 向当前会话发送系统消息
         *
         * @param str 消息内容
         * @param escapeCharacters 是否转义特殊字符(|)
         */
        virtual void SendSysMessage(std::string_view str, bool escapeCharacters = false);

        /**
         * @brief 发送系统消息(通过条目ID)
         * @param entry 字符串条目ID
         */
        void SendSysMessage(uint32 entry);

        /**
         * @brief 格式化发送系统消息(C风格)
         *
         * @tparam Args 参数类型
         * @param fmt 格式化字符串
         * @param args 格式化参数
         */
        template<typename... Args>
        void PSendSysMessage(const char* fmt, Args&&... args)
        {
            SendSysMessage(fmt::sprintf(fmt, std::forward<Args>(args)...));
        }

        /**
         * @brief 格式化发送系统消息(通过条目ID)
         *
         * @tparam Args 参数类型
         * @param entry 字符串条目ID
         * @param args 格式化参数
         */
        template<typename... Args>
        void PSendSysMessage(uint32 entry, Args&&... args)
        {
            SendSysMessage(PGetParseString(entry, std::forward<Args>(args)...));
        }

        /**
         * @brief 获取格式化后的字符串
         *
         * @tparam Args 参数类型
         * @param entry 字符串条目ID
         * @param args 格式化参数
         * @return std::string 格式化后的字符串
         */
        template<typename... Args>
        std::string PGetParseString(uint32 entry, Args&&... args) const
        {
            return fmt::sprintf(GetTrinityString(entry), std::forward<Args>(args)...);
        }

        /**
         * @brief 解析命令(内部实现)
         * @param text 命令文本
         * @return true 如果命令被处理
         */
        bool _ParseCommands(std::string_view text);

        /**
         * @brief 解析命令
         *
         * 检查命令格式并调用内部解析函数
         *
         * @param text 命令文本
         * @return true 如果命令被处理
         */
        virtual bool ParseCommands(std::string_view text);

        /**
         * @brief 发送全局系统消息
         * @param str 消息内容
         */
        void SendGlobalSysMessage(const char *str);

        // 聊天/控制台不同实现的函数

        /**
         * @brief 检查输出是否可读
         * @return true 如果是人类可读格式
         */
        virtual bool IsHumanReadable() const { return true; }

        /**
         * @brief 检查是否具有权限
         * @param permission 权限ID
         * @return true 如果具有权限
         */
        virtual bool HasPermission(uint32 permission) const;

        /**
         * @brief 获取名称链接
         * @return std::string 格式化的名称链接
         */
        virtual std::string GetNameLink() const;

        /**
         * @brief 是否需要向目标报告
         * @param chr 目标玩家
         * @return true 如果需要报告
         */
        virtual bool needReportToTarget(Player* chr) const;

        /**
         * @brief 获取会话DBC语言设置
         * @return LocaleConstant 语言区域常量
         */
        virtual LocaleConstant GetSessionDbcLocale() const;

        /**
         * @brief 获取会话数据库语言索引
         * @return int 语言索引
         */
        virtual int GetSessionDbLocaleIndex() const;

        /**
         * @brief 检查目标权限是否低于当前用户
         *
         * @param target 目标玩家
         * @param guid 目标GUID
         * @param strong 是否严格检查(不允许同级)
         * @return true 如果目标权限更低
         */
        bool HasLowerSecurity(Player* target, ObjectGuid guid, bool strong = false);

        /**
         * @brief 检查账号权限是否低于当前用户
         *
         * @param target 目标会话
         * @param account 目标账号ID
         * @param strong 是否严格检查
         * @return true 如果目标权限更低
         */
        bool HasLowerSecurityAccount(WorldSession* target, uint32 account, bool strong = false);

        /**
         * @brief 发送全局GM系统消息
         * @param str 消息内容
         */
        void SendGlobalGMSysMessage(const char *str);

        /**
         * @brief 获取选中的玩家
         * @return Player* 选中的玩家或自己
         */
        Player* getSelectedPlayer();

        /**
         * @brief 获取选中的生物
         * @return Creature* 选中的生物
         */
        Creature* getSelectedCreature();

        /**
         * @brief 获取选中的单位
         * @return Unit* 选中的单位或自己
         */
        Unit* getSelectedUnit();

        /**
         * @brief 获取选中的对象
         * @return WorldObject* 选中的对象
         */
        WorldObject* getSelectedObject();

        /**
         * @brief 获取选中的玩家或自己
         *
         * 如果选中了玩家则返回选中的玩家,否则返回自己
         *
         * @return Player* 选中的玩家或自己
         */
        Player* getSelectedPlayerOrSelf();

        /**
         * @brief 从链接中提取键值
         *
         * 解析超链接格式: |color|linkType:key|h[name]|h|r
         *
         * @param text 输入文本
         * @param linkType 链接类型
         * @param something1 额外提取的参数
         * @return char* 提取的键值
         */
        char* extractKeyFromLink(char* text, char const* linkType, char** something1 = nullptr);

        /**
         * @brief 从链接中提取键值(多类型支持)
         *
         * @param text 输入文本
         * @param linkTypes 链接类型数组
         * @param found_idx 找到的类型索引
         * @param something1 额外提取的参数
         * @return char* 提取的键值
         */
        char* extractKeyFromLink(char* text, char const* const* linkTypes, int* found_idx, char** something1 = nullptr);

        /**
         * @brief 提取引号参数
         * @param args 参数字符串
         * @return char* 提取的引号内容
         */
        char* extractQuotedArg(char* args);

        /**
         * @brief 从链接提取低位GUID
         *
         * @param text 输入文本
         * @param guidHigh 输出GUID高位类型
         * @return ObjectGuid::LowType 提取的低位GUID
         */
        ObjectGuid::LowType extractLowGuidFromLink(char* text, HighGuid& guidHigh);

        /**
         * @brief 根据名称获取玩家、队伍和GUID
         *
         * @param cname 玩家名称
         * @param player 输出玩家指针
         * @param group 输出队伍指针
         * @param guid 输出GUID
         * @param offline 是否包含离线玩家
         * @return true 如果成功
         */
        bool GetPlayerGroupAndGUIDByName(char const* cname, Player*& player, Group*& group, ObjectGuid& guid, bool offline = false);

        /**
         * @brief 从链接提取玩家名称
         * @param text 输入文本
         * @return std::string 玩家名称
         */
        std::string extractPlayerNameFromLink(char* text);

        /**
         * @brief 提取玩家目标
         *
         * 通过参数(名称/链接)或游戏内选择获取玩家
         *
         * @param args 参数字符串
         * @param player 输出玩家指针
         * @param player_guid 输出玩家GUID
         * @param player_name 输出玩家名称
         * @return true 如果成功
         */
        bool extractPlayerTarget(char* args, Player** player, ObjectGuid* player_guid = nullptr, std::string* player_name = nullptr);

        /**
         * @brief 生成玩家链接
         * @param name 玩家名称
         * @return std::string 格式化的玩家链接
         */
        std::string playerLink(std::string const& name) const { return m_session ? "|cffffffff|Hplayer:"+name+"|h["+name+"]|h|r" : name; }

        /**
         * @brief 获取玩家的名称链接
         * @param chr 玩家对象
         * @return std::string 格式化的名称链接
         */
        std::string GetNameLink(Player* chr) const;

        /**
         * @brief 获取附近的游戏对象
         * @return GameObject* 最近的GameObject
         */
        GameObject* GetNearbyGameObject();

        /**
         * @brief 通过数据库GUID从玩家地图获取游戏对象
         * @param lowguid 数据库GUID
         * @return GameObject* 游戏对象指针
         */
        GameObject* GetObjectFromPlayerMapByDbGuid(ObjectGuid::LowType lowguid);

        /**
         * @brief 通过数据库GUID从玩家地图获取生物
         * @param lowguid 数据库GUID
         * @return Creature* 生物指针
         */
        Creature* GetCreatureFromPlayerMapByDbGuid(ObjectGuid::LowType lowguid);

        /**
         * @brief 检查是否已发送错误消息
         * @return true 如果已发送错误消息
         */
        bool HasSentErrorMessage() const { return sentErrorMessage; }

        /**
         * @brief 设置错误消息标志
         * @param val 标志值
         */
        void SetSentErrorMessage(bool val){ sentErrorMessage = val; }

    protected:
        /**
         * @brief 保护构造函数(用于CLI子类)
         */
        explicit ChatHandler() : m_session(nullptr), sentErrorMessage(false) { }

    private:
        WorldSession* m_session;    ///< 世界会话指针(游戏内非空,控制台为nullptr)

        // 通用全局标志
        bool sentErrorMessage;      ///< 是否已发送错误消息
};

/**
 * @class CliHandler
 * @brief 控制台命令处理器
 *
 * 继承自ChatHandler,专门用于处理服务器控制台命令。
 * 与游戏内命令处理器的区别:
 * - 没有会话对象
 * - 拥有所有权限
 * - 输出到控制台而非游戏内聊天频道
 */
class TC_GAME_API CliHandler : public ChatHandler
{
    public:
        /**
         * @brief 打印函数类型定义
         */
        using Print = void(void*, std::string_view);

        /**
         * @brief 构造函数
         * @param callbackArg 回调参数
         * @param zprint 打印函数指针
         */
        explicit CliHandler(void* callbackArg, Print* zprint) : m_callbackArg(callbackArg), m_print(zprint) { }

        // 重写基类函数

        /**
         * @brief 获取Trinity字符串(使用默认语言)
         * @param entry 字符串条目ID
         * @return char const* 本地化字符串
         */
        char const* GetTrinityString(uint32 entry) const override;

        /**
         * @brief 控制台拥有所有权限
         * @param permission 权限ID
         * @return true 始终返回true
         */
        bool HasPermission(uint32 /*permission*/) const override { return true; }

        /**
         * @brief 发送系统消息到控制台
         * @param str 消息内容
         * @param escapeCharacters 是否转义字符(未使用)
         */
        void SendSysMessage(std::string_view, bool escapeCharacters) override;

        /**
         * @brief 解析控制台命令
         *
         * 控制台命令可以带或不带.或!前缀
         *
         * @param str 命令字符串
         * @return true 如果命令被处理
         */
        bool ParseCommands(std::string_view str) override;

        /**
         * @brief 获取控制台名称链接
         * @return std::string "Console"
         */
        std::string GetNameLink() const override;

        /**
         * @brief 控制台总是需要向目标报告
         * @param chr 目标玩家
         * @return true 始终返回true
         */
        bool needReportToTarget(Player* chr) const override;

        /**
         * @brief 获取默认DBC语言
         * @return LocaleConstant 默认语言
         */
        LocaleConstant GetSessionDbcLocale() const override;

        /**
         * @brief 获取默认数据库语言索引
         * @return int 语言索引
         */
        int GetSessionDbLocaleIndex() const override;

    private:
        void* m_callbackArg;    ///< 回调参数
        Print* m_print;         ///< 打印函数指针
};

/**
 * @class AddonChannelCommandHandler
 * @brief 插件频道命令处理器
 *
 * 继承自ChatHandler,用于处理通过插件频道发送的命令。
 * 支持远程命令执行和响应。
 *
 * 通信协议格式:
 * - Ping: TrinityCore\tp<echo>
 * - Human-readable: TrinityCore\th<echo>\0<command>
 * - Machine: TrinityCore\ti<echo>\0<command>
 *
 * 响应格式:
 * - Ack: TrinityCore\ta<echo>
 * - OK: TrinityCore\to<echo>
 * - Failed: TrinityCore\tf<echo>
 * - Message: TrinityCore\tm<echo><message>
 */
class TC_GAME_API AddonChannelCommandHandler : public ChatHandler
{
    public:
        using ChatHandler::ChatHandler;

        /**
         * @brief 解析插件频道命令
         * @param str 命令字符串
         * @return true 如果命令被处理
         */
        bool ParseCommands(std::string_view str) override;

        /**
         * @brief 发送系统消息
         * @param str 消息内容
         * @param escapeCharacters 是否转义字符
         */
        void SendSysMessage(std::string_view, bool escapeCharacters) override;
        using ChatHandler::SendSysMessage;

        /**
         * @brief 检查是否为人类可读格式
         * @return true 如果是人类可读格式
         */
        bool IsHumanReadable() const override { return humanReadable; }

    private:
        /**
         * @brief 发送消息
         * @param msg 消息内容
         */
        void Send(std::string const& msg);

        /**
         * @brief 发送确认消息(Ack)
         */
        void SendAck();

        /**
         * @brief 发送成功消息(OK)
         */
        void SendOK();

        /**
         * @brief 发送失败消息(Failed)
         */
        void SendFailed();

        char const* echo = nullptr;     ///< 回显标识符
        bool hadAck = false;            ///< 是否已发送确认
        bool humanReadable = false;     ///< 是否为人类可读格式
};

#endif
