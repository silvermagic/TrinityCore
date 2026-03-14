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
 * @file cs_send.cpp
 * @brief 发送命令脚本模块
 *
 * 本文件实现了所有与发送邮件和消息相关的GM命令，包括：
 * - 发送邮件
 * - 发送物品（通过邮件附件）
 * - 发送金币
 * - 发送即时消息
 *
 * 这些命令主要用于游戏管理、测试和玩家奖励发放。
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Language.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

#if TRINITY_COMPILER == TRINITY_COMPILER_GNU
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

using namespace Trinity::ChatCommands;

/**
 * @class send_commandscript
 * @brief 发送命令脚本类
 *
 * 实现所有与邮件和消息发送相关的GM命令。
 * 该类继承自CommandScript，提供命令注册和处理接口。
 */
class send_commandscript : public CommandScript
{
public:
    /**
     * @brief 构造函数
     * 初始化命令脚本，设置脚本名称为"send_commandscript"
     */
    send_commandscript() : CommandScript("send_commandscript") { }

    /**
     * @brief 获取命令表
     * @return 返回发送命令的命令表结构
     *
     * 注册以下命令层次结构：
     * - .send
     *   - .items   - 发送物品邮件
     *   - .mail    - 发送普通邮件
     *   - .message - 发送即时消息
     *   - .money   - 发送金币邮件
     */
    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable sendCommandTable =
        {
            { "items",   HandleSendItemsCommand,   rbac::RBAC_PERM_COMMAND_SEND_ITEMS,   Console::Yes },
            { "mail",    HandleSendMailCommand,    rbac::RBAC_PERM_COMMAND_SEND_MAIL,    Console::Yes },
            { "message", HandleSendMessageCommand, rbac::RBAC_PERM_COMMAND_SEND_MESSAGE, Console::Yes },
            { "money",   HandleSendMoneyCommand,   rbac::RBAC_PERM_COMMAND_SEND_MONEY,   Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "send", sendCommandTable },
        };
        return commandTable;
    }

    /**
     * @brief 处理发送邮件命令
     * @param handler 聊天命令处理器
     * @param args 命令参数
     * @return 命令执行成功返回true
     *
     * 命令格式: .send mail #playername "subject" "text"
     *
     * 功能：向指定玩家发送一封纯文本邮件。
     * - 邮件主题和正文必须用引号包围
     * - 邮件使用GM信纸样式（MAIL_STATIONERY_GM）
     * - 发件人：在线GM显示玩家GUID，控制台显示0
     *
     * @note 收件人可以是离线玩家，邮件会保存在数据库中
     */
    static bool HandleSendMailCommand(ChatHandler* handler, char const* args)
    {
        // 解析命令参数：玩家名 "主题" "正文"
        Player* target;
        ObjectGuid targetGuid;
        std::string targetName;
        // 提取目标玩家（支持在线和离线玩家）
        if (!handler->extractPlayerTarget((char*)args, &target, &targetGuid, &targetName))
            return false;

        // 提取邮件主题（必须用引号包围）
        char* tail1 = strtok(nullptr, "");
        if (!tail1)
            return false;

        char const* msgSubject = handler->extractQuotedArg(tail1);
        if (!msgSubject)
            return false;

        // 提取邮件正文（必须用引号包围）
        char* tail2 = strtok(nullptr, "");
        if (!tail2)
            return false;

        char const* msgText = handler->extractQuotedArg(tail2);
        if (!msgText)
            return false;

        // 保存主题和正文内容
        std::string subject = msgSubject;
        std::string text    = msgText;

        // 创建发件人信息：控制台发件时使用0，在线GM发件时使用玩家GUID
        MailSender sender(MAIL_NORMAL, handler->GetSession() ? handler->GetSession()->GetPlayer()->GetGUID().GetCounter() : 0, MAIL_STATIONERY_GM);

        // 创建数据库事务并发送邮件
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        MailDraft(subject, text)
            .SendMailTo(trans, MailReceiver(target, targetGuid.GetCounter()), sender);

        CharacterDatabase.CommitTransaction(trans);

        // 发送成功消息
        std::string nameLink = handler->playerLink(targetName);
        handler->PSendSysMessage(LANG_MAIL_SENT, nameLink.c_str());
        return true;
    }

    /**
     * @brief 处理发送物品命令
     * @param handler 聊天命令处理器
     * @param args 命令参数
     * @return 命令执行成功返回true
     *
     * 命令格式: .send items #playername "subject" "text" itemid1[:count1] itemid2[:count2] ...
     *
     * 功能：向指定玩家发送一封包含物品的邮件。
     * - 支持多个物品，用空格分隔
     * - 物品格式：itemid[:count]，count默认为1
     * - 物品会根据堆叠上限自动拆分为多个物品槽
     * - 最多支持12个物品槽（MAX_MAIL_ITEMS）
     *
     * @note 物品会先保存到数据库再发送，防止邮件加载时丢失
     * @note 如果物品ID无效或数量超出限制，命令将失败
     */
    static bool HandleSendItemsCommand(ChatHandler* handler, char const* args)
    {
        // 解析命令参数：玩家名 "主题" "正文" 物品列表
        Player* receiver;
        ObjectGuid receiverGuid;
        std::string receiverName;
        // 提取目标玩家
        if (!handler->extractPlayerTarget((char*)args, &receiver, &receiverGuid, &receiverName))
            return false;

        // 提取邮件主题
        char* tail1 = strtok(nullptr, "");
        if (!tail1)
            return false;

        char const* msgSubject = handler->extractQuotedArg(tail1);
        if (!msgSubject)
            return false;

        // 提取邮件正文
        char* tail2 = strtok(nullptr, "");
        if (!tail2)
            return false;

        char const* msgText = handler->extractQuotedArg(tail2);
        if (!msgText)
            return false;

        std::string subject = msgSubject;
        std::string text    = msgText;

        // 定义物品对类型（物品ID, 数量）
        typedef std::pair<uint32, uint32> ItemPair;
        typedef std::list< ItemPair > ItemPairs;
        ItemPairs items;

        // 解析物品列表
        char* tail = strtok(nullptr, "");

        // 遍历解析每个物品参数
        while (char* itemStr = strtok(tail, " "))
        {
            tail = strtok(nullptr, "");

            // 解析物品ID和数量（格式：itemid:count）
            char const* itemIdStr = strtok(itemStr, ":");
            char const* itemCountStr = strtok(nullptr, " ");

            Optional<uint32> itemId = Trinity::StringTo<uint32>(itemIdStr);
            if (!itemId)
                return false;

            // 验证物品模板是否存在
            ItemTemplate const* item_proto = sObjectMgr->GetItemTemplate(*itemId);
            if (!item_proto)
            {
                handler->PSendSysMessage(LANG_COMMAND_ITEMIDINVALID, *itemId);
                handler->SetSentErrorMessage(true);
                return false;
            }

            // 解析物品数量，默认为1
            uint32 itemCount = itemCountStr ? atoi(itemCountStr) : 1;
            // 验证物品数量是否合法
            if (itemCount < 1 || (item_proto->MaxCount > 0 && itemCount > uint32(item_proto->MaxCount)))
            {
                handler->PSendSysMessage(LANG_COMMAND_INVALID_ITEM_COUNT, itemCount, *itemId);
                handler->SetSentErrorMessage(true);
                return false;
            }

            // 根据物品堆叠上限拆分物品
            while (itemCount > item_proto->GetMaxStackSize())
            {
                items.push_back(ItemPair(*itemId, item_proto->GetMaxStackSize()));
                itemCount -= item_proto->GetMaxStackSize();
            }

            // 添加剩余物品
            items.push_back(ItemPair(*itemId, itemCount));

            // 检查是否超过邮件物品槽上限
            if (items.size() > MAX_MAIL_ITEMS)
            {
                handler->PSendSysMessage(LANG_COMMAND_MAIL_ITEMS_LIMIT, MAX_MAIL_ITEMS);
                handler->SetSentErrorMessage(true);
                return false;
            }
        }

        // 创建发件人信息
        MailSender sender(MAIL_NORMAL, handler->GetSession() ? handler->GetSession()->GetPlayer()->GetGUID().GetCounter() : 0, MAIL_STATIONERY_GM);

        // 创建邮件草稿
        MailDraft draft(subject, text);

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        // 创建并保存所有物品
        for (ItemPairs::const_iterator itr = items.begin(); itr != items.end(); ++itr)
        {
            // 创建物品实例
            if (Item* item = Item::CreateItem(itr->first, itr->second, handler->GetSession() ? handler->GetSession()->GetPlayer() : 0))
            {
                // 保存到数据库，防止下次邮件加载时丢失
                item->SaveToDB(trans);
                draft.AddItem(item);
            }
        }

        // 发送邮件
        draft.SendMailTo(trans, MailReceiver(receiver, receiverGuid.GetCounter()), sender);
        CharacterDatabase.CommitTransaction(trans);

        std::string nameLink = handler->playerLink(receiverName);
        handler->PSendSysMessage(LANG_MAIL_SENT, nameLink.c_str());
        return true;
    }

    /**
     * @brief 处理发送金币命令
     * @param handler 聊天命令处理器
     * @param receiver 收件人标识
     * @param subject 邮件主题
     * @param text 邮件正文
     * @param money 金币数量（铜币单位）
     * @return 命令执行成功返回true
     *
     * 命令格式: .send money #playername "subject" "text" #money
     *
     * 功能：向指定玩家发送一封包含金币的邮件。
     * - 金币数量以铜币为单位
     * - 邮件使用GM信纸样式
     *
     * @note 收件人可以是离线玩家
     */
    static bool HandleSendMoneyCommand(ChatHandler* handler, PlayerIdentifier const& receiver, QuotedString const& subject, QuotedString const& text, uint32 money)
    {
        // 创建发件人信息
        MailSender sender(MAIL_NORMAL, handler->GetSession() ? handler->GetSession()->GetPlayer()->GetGUID().GetCounter() : 0, MAIL_STATIONERY_GM);

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        // 创建包含金币的邮件草稿并发送
        MailDraft(subject, text)
            .AddMoney(money)
            .SendMailTo(trans, MailReceiver(receiver.GetConnectedPlayer(), receiver.GetGUID().GetCounter()), sender);

        CharacterDatabase.CommitTransaction(trans);

        std::string nameLink = handler->playerLink(receiver.GetName());
        handler->PSendSysMessage(LANG_MAIL_SENT, nameLink.c_str());
        return true;
    }

    /**
     * @brief 处理发送即时消息命令
     * @param handler 聊天命令处理器
     * @param args 命令参数
     * @return 命令执行成功返回true
     *
     * 命令格式: .send message #playername #message
     *
     * 功能：向指定在线玩家发送一条即时消息。
     * - 使用AreaTriggerMessage方式发送，消息会立即显示在玩家屏幕上
     * - 消息前会带有红色"[Message from administrator]:"前缀
     * - 只能发送给在线玩家
     *
     * @note 此消息不会保存到聊天记录，是临时性的即时通讯
     * @note 如果玩家正在登出，命令将失败
     */
    static bool HandleSendMessageCommand(ChatHandler* handler, char const* args)
    {
        // 查找目标玩家
        Player* player;
        if (!handler->extractPlayerTarget((char*)args, &player))
            return false;

        // 提取消息内容
        char* msgStr = strtok(nullptr, "");
        if (!msgStr)
            return false;

        // 检查玩家是否正在登出
        if (player->GetSession()->isLogingOut())
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        // 发送即时消息（使用SendAreaTriggerMessage实现最快送达）
        player->GetSession()->SendAreaTriggerMessage("%s", msgStr);
        // 发送管理员标识前缀
        player->GetSession()->SendAreaTriggerMessage("|cffff0000[Message from administrator]:|r");

        // 发送确认消息给发送者
        std::string nameLink = handler->GetNameLink(player);
        handler->PSendSysMessage(LANG_SENDMESSAGE, nameLink.c_str(), msgStr);

        return true;
    }
};

/**
 * @brief 注册发送命令脚本
 *
 * 此函数在脚本系统初始化时被调用，
 * 创建send_commandscript实例并注册到命令脚本管理器中。
 */
void AddSC_send_commandscript()
{
    new send_commandscript();
}
