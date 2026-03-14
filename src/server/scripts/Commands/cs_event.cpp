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
 * @file cs_event.cpp
 * @brief 游戏事件管理命令模块
 *
 * 本模块实现了GM命令系统中用于管理游戏事件的命令。
 * 游戏事件是指在特定时间段内激活的游戏内容，如节日活动、世界事件等。
 * GM可以通过这些命令查看、启动和停止游戏事件。
 *
 * 主要功能:
 * - 显示当前激活的游戏事件列表
 * - 查看指定事件的详细信息（开始时间、结束时间、持续时间等）
 * - 手动启动游戏事件
 * - 手动停止游戏事件
 *
 * 使用场景:
 * - 测试节日活动的效果
 * - 手动控制世界事件的激活状态
 * - 查看事件的调度信息
 * - 排查事件相关的问题
 */

/* ScriptData
Name: event_commandscript
%Complete: 100
Comment: All event related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "Chat.h"
#include "GameEventMgr.h"
#include "GameTime.h"
#include "Language.h"
#include "Player.h"
#include "RBAC.h"

using namespace Trinity::ChatCommands;

/**
 * @class event_commandscript
 * @brief 游戏事件命令脚本类
 *
 * 继承自CommandScript基类，提供游戏事件管理功能的GM命令实现。
 * 该类负责注册和处理所有与游戏事件相关的命令，包括查看、启动和停止事件。
 */
class event_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化事件命令脚本，设置脚本名称为"event_commandscript"
     */
    event_commandscript() : CommandScript("event_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回命令表的常量引用
     *
     * 注册所有游戏事件相关的命令，包括：
     * - event activelist：显示当前激活的事件列表
     * - event start：启动指定事件
     * - event stop：停止指定事件
     * - event info：显示事件详细信息
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable eventCommandTable =
        {
            { "activelist", HandleEventActiveListCommand, rbac::RBAC_PERM_COMMAND_EVENT_ACTIVELIST, Console::Yes },
            { "start",      HandleEventStartCommand,      rbac::RBAC_PERM_COMMAND_EVENT_START,      Console::Yes },
            { "stop",       HandleEventStopCommand,       rbac::RBAC_PERM_COMMAND_EVENT_STOP,       Console::Yes },
            { "info",       HandleEventInfoCommand,       rbac::RBAC_PERM_COMMAND_EVENT_INFO,       Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "event", eventCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 处理显示激活事件列表命令
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @return 总是返回true
     *
     * 列出当前服务器中所有正在激活的游戏事件。
     * 包括事件的ID和描述信息。
     * 如果没有激活的事件，会显示相应的提示信息。
     *
     * 调用时机：当GM执行 .event activelist 命令时
     */
    static bool HandleEventActiveListCommand(ChatHandler* handler)
    {
        uint32 counter = 0;

        // 获取所有游戏事件数据和当前激活的事件列表
        GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();
        GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr->GetActiveEventList();

        char const* active = handler->GetTrinityString(LANG_ACTIVE);

        // 遍历所有激活的事件
        for (uint16 eventId : activeEvents)
        {
            GameEventData const& eventData = events[eventId];

            // 根据是否在游戏中显示不同格式的消息
            if (handler->GetSession())
                handler->PSendSysMessage(LANG_EVENT_ENTRY_LIST_CHAT, eventId, eventId, eventData.description.c_str(), active);
            else
                handler->PSendSysMessage(LANG_EVENT_ENTRY_LIST_CONSOLE, eventId, eventData.description.c_str(), active);

            ++counter;
        }

        // 如果没有激活的事件，显示提示信息
        if (counter == 0)
            handler->SendSysMessage(LANG_NOEVENTFOUND);
        handler->SetSentErrorMessage(true);

        return true;
    }

    /**
     * @brief 处理显示事件详细信息命令
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @param eventId 事件ID，支持超链接或数值形式
     * @return 成功返回true，失败返回false
     *
     * 显示指定游戏事件的详细信息，包括：
     * - 事件描述
     * - 激活状态
     * - 开始时间和结束时间
     * - 发生周期和持续时间
     * - 下次检查时间
     *
     * 调用时机：当GM执行 .event info 命令时
     */
    static bool HandleEventInfoCommand(ChatHandler* handler, Variant<Hyperlink<gameevent>, uint16> eventId)
    {
        GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();

        // 验证事件ID是否有效
        if (*eventId >= events.size())
        {
            handler->SendSysMessage(LANG_EVENT_NOT_EXIST);
            handler->SetSentErrorMessage(true);
            return false;
        }

        GameEventData const& eventData = events[*eventId];
        if (!eventData.isValid())
        {
            handler->SendSysMessage(LANG_EVENT_NOT_EXIST);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查事件是否当前激活
        GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr->GetActiveEventList();
        bool active = activeEvents.find(eventId) != activeEvents.end();
        char const* activeStr = active ? handler->GetTrinityString(LANG_ACTIVE) : "";

        // 格式化时间信息
        std::string startTimeStr = TimeToTimestampStr(eventData.start);
        std::string endTimeStr = TimeToTimestampStr(eventData.end);

        // 计算下次检查时间
        uint32 delay = sGameEventMgr->NextCheck(eventId);
        time_t nextTime = GameTime::GetGameTime() + delay;
        std::string nextStr = nextTime >= eventData.start && nextTime < eventData.end ? TimeToTimestampStr(GameTime::GetGameTime() + delay) : "-";

        // 格式化发生周期和持续时间
        std::string occurenceStr = secsToTimeString(eventData.occurence * MINUTE);
        std::string lengthStr = secsToTimeString(eventData.length * MINUTE);

        // 发送事件详细信息给用户
        handler->PSendSysMessage(LANG_EVENT_INFO, eventId, eventData.description.c_str(), activeStr,
            startTimeStr.c_str(), endTimeStr.c_str(), occurenceStr.c_str(), lengthStr.c_str(),
            nextStr.c_str());
        return true;
    }

    /**
     * @brief 处理启动游戏事件命令
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @param eventId 事件ID，支持超链接或数值形式
     * @return 成功返回true，失败返回false
     *
     * 手动启动指定的游戏事件。如果事件已经在激活状态，则不会重复启动。
     * 强制启动的事件不会遵循原有的时间表，需要手动停止。
     *
     * 调用时机：当GM执行 .event start 命令时
     */
    static bool HandleEventStartCommand(ChatHandler* handler, Variant<Hyperlink<gameevent>, uint16> eventId)
    {
        GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();

        // 验证事件ID是否有效
        if (*eventId < 1 || *eventId >= events.size())
        {
            handler->SendSysMessage(LANG_EVENT_NOT_EXIST);
            handler->SetSentErrorMessage(true);
            return false;
        }

        GameEventData const& eventData = events[*eventId];
        if (!eventData.isValid())
        {
            handler->SendSysMessage(LANG_EVENT_NOT_EXIST);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查事件是否已经激活
        GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr->GetActiveEventList();
        if (activeEvents.find(eventId) != activeEvents.end())
        {
            // 事件已经激活，提示用户
            handler->PSendSysMessage(LANG_EVENT_ALREADY_ACTIVE, eventId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 启动事件，参数true表示强制启动（不遵循时间表）
        sGameEventMgr->StartEvent(eventId, true);
        return true;
    }

    /**
     * @brief 处理停止游戏事件命令
     * @param handler 聊天处理器，用于发送消息和获取会话信息
     * @param eventId 事件ID，支持超链接或数值形式
     * @return 成功返回true，失败返回false
     *
     * 手动停止指定的游戏事件。只有当前激活的事件才能被停止。
     * 停止后的事件将按照原有时间表自动激活（如果有配置）。
     *
     * 调用时机：当GM执行 .event stop 命令时
     */
    static bool HandleEventStopCommand(ChatHandler* handler, Variant<Hyperlink<gameevent>, uint16> eventId)
    {
        GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();

        // 验证事件ID是否有效
        if (*eventId < 1 || *eventId >= events.size())
        {
            handler->SendSysMessage(LANG_EVENT_NOT_EXIST);
            handler->SetSentErrorMessage(true);
            return false;
        }

        GameEventData const& eventData = events[*eventId];
        if (!eventData.isValid())
        {
            handler->SendSysMessage(LANG_EVENT_NOT_EXIST);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 检查事件是否当前激活
        GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr->GetActiveEventList();

        if (activeEvents.find(eventId) == activeEvents.end())
        {
            // 事件未激活，提示用户
            handler->PSendSysMessage(LANG_EVENT_NOT_ACTIVE, eventId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 停止事件，参数true表示强制停止
        sGameEventMgr->StopEvent(eventId, true);
        return true;
    }
};

/**
 * @brief 注册游戏事件命令脚本
 *
 * 此函数用于将游戏事件命令脚本注册到服务器中，
 * 在服务器启动时被调用以初始化命令系统
 */
void AddSC_event_commandscript()
{
    new event_commandscript();
}
