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
 * @file MailHandler.cpp
 * @brief 游戏内邮件系统网络消息处理器
 *
 * 本模块负责处理玩家与邮件系统交互的所有网络消息，包括：
 * - 发送邮件（包含物品附件和金币）
 * - 接收和阅读邮件
 * - 取出邮件附件和金币
 * - 删除邮件和退回邮件
 * - 查询邮件列表和下次邮件到达时间
 *
 * 邮件系统特性：
 * - 支持金币和物品附件
 * - 支持货到付款（COD）功能
 * - 邮件投递有延迟（跨账户发送物品时）
 * - 支持邮件模板（系统邮件）
 * - 战队绑定物品可在同账户角色间邮寄
 *
 * 安全措施：
 * - 发送邮件需要等级限制
 * - 收件人邮箱有容量限制（最多100封）
 * - 跨阵营邮寄需要特殊权限
 * - 防止负数金币和COD金额
 *
 * @note 邮件系统与物品系统紧密关联，附件物品需要特殊处理
 */

#include "WorldSession.h"
#include "AccountMgr.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Item.h"
#include "Language.h"
#include "Log.h"
#include "Mail.h"
#include "MailPackets.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "World.h"
#include "WorldPacket.h"

/**
 * @brief 检查玩家是否可以打开邮箱
 *
 * 验证玩家是否有权限访问指定的邮箱。支持三种邮箱类型：
 * 玩家自己（需要GM权限）、游戏对象邮箱、NPC邮箱。
 *
 * @param guid 邮箱对象的 GUID
 * @return true 如果可以打开邮箱，false 否则
 *
 * 检查类型：
 * - 玩家自己的 GUID：需要 GM 邮箱权限（作弊检测）
 * - 游戏对象：必须是邮箱类型且可交互
 * - NPC：必须有邮箱 NPC 标志且可交互
 *
 * 安全措施：
 * - 记录尝试通过作弊打开邮箱的玩家
 * - 验证游戏对象和 NPC 是否可交互
 *
 * @see Player::GetGameObjectIfCanInteractWith()
 * @see Player::GetNPCIfCanInteractWith()
 */
bool WorldSession::CanOpenMailBox(ObjectGuid guid)
{
    if (guid == _player->GetGUID())
    {
        if (!HasPermission(rbac::RBAC_PERM_COMMAND_MAILBOX))
        {
            TC_LOG_WARN("cheat", "{} attempted to open mailbox by using a cheat.", _player->GetName());
            return false;
        }
    }
    else if (guid.IsGameObject())
    {
        if (!_player->GetGameObjectIfCanInteractWith(guid, GAMEOBJECT_TYPE_MAILBOX))
            return false;
    }
    else if (guid.IsAnyTypeCreature())
    {
        if (!_player->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_MAILBOX))
            return false;
    }
    else
        return false;

    return true;
}

/**
 * @brief 处理发送邮件的请求
 *
 * 当玩家发送邮件时，系统会验证发送条件、处理物品附件和金币，
 * 然后将邮件保存到数据库并发送给收件人。
 *
 * @param sendMail 发送邮件的数据包，包含收件人、主题、正文、附件、金币和COD金额
 *
 * 调用时机：
 * - 玩家在邮箱界面点击发送按钮时
 * - 客户端发送 CMSG_SEND_MAIL 消息
 *
 * 处理流程：
 * 1. 验证邮箱访问权限
 * 2. 验证发件人等级要求
 * 3. 查找收件人角色信息
 * 4. 验证金币和COD金额是否合法（非负数）
 * 5. 验证收件人邮箱容量限制
 * 6. 验证阵营限制（跨阵营邮寄需要权限）
 * 7. 验证所有附件物品是否可交易
 * 8. 扣除邮费和发送的金币
 * 9. 将物品从发件人背包移至邮件
 * 10. 保存邮件到数据库
 * 11. 如果收件人在线，通知其收到新邮件
 *
 * 邮件费用：
 * - 无附件邮件：30铜
 * - 有附件邮件：30铜 × 附件数量
 *
 * 物品附件限制：
 * - 物品必须可交易
 * - 不能发送非空背包
 * - 战队绑定物品只能寄给同账户角色
 * - 不能发送消耗品和有持续时间的物品
 * - 包装物品不能使用COD
 *
 * 投递延迟：
 * - 同账户内邮寄：即时投递
 * - 跨账户邮寄物品：1小时投递延迟
 *
 * 性能注意事项：
 * - 使用异步数据库查询获取离线收件人信息
 * - 物品转移使用事务保证数据一致性
 *
 * @see MailDraft::SendMailTo()
 * @see Player::CanStoreNewItem()
 */
void WorldSession::HandleSendMail(WorldPackets::Mail::SendMail& sendMail)
{
    if (!CanOpenMailBox(sendMail.Info.Mailbox))
        return;

    if (sendMail.Info.Target.empty())
        return;

    Player* player = _player;

    if (_player->GetLevel() < sWorld->getIntConfig(CONFIG_MAIL_LEVEL_REQ))
    {
        SendNotification(GetTrinityString(LANG_MAIL_SENDER_REQ), sWorld->getIntConfig(CONFIG_MAIL_LEVEL_REQ));
        return;
    }

    ObjectGuid receiverGuid;
    if (normalizePlayerName(sendMail.Info.Target))
        receiverGuid = sCharacterCache->GetCharacterGuidByName(sendMail.Info.Target);

    if (!receiverGuid)
    {
        TC_LOG_INFO("network", "Player {} is sending mail to {} (GUID: non-existing!) with subject {} "
            "and body {} includes {} items, {} copper and {} COD copper with StationeryID = {}, PackageID = {}",
            GetPlayerInfo(), sendMail.Info.Target, sendMail.Info.Subject, sendMail.Info.Body,
            sendMail.Info.Attachments.size(), sendMail.Info.SendMoney, sendMail.Info.Cod, sendMail.Info.StationeryID, sendMail.Info.PackageID);
        player->SendMailResult(0, MAIL_SEND, MAIL_ERR_RECIPIENT_NOT_FOUND);
        return;
    }

    if (sendMail.Info.SendMoney < 0)
    {
        GetPlayer()->SendMailResult(0, MAIL_SEND, MAIL_ERR_INTERNAL_ERROR);
        TC_LOG_WARN("cheat", "Player {} attempted to send mail to {} ({}) with negative money value (SendMoney: {})",
            GetPlayerInfo(), sendMail.Info.Target, receiverGuid.ToString(), sendMail.Info.SendMoney);
        return;
    }

    if (sendMail.Info.Cod < 0)
    {
        GetPlayer()->SendMailResult(0, MAIL_SEND, MAIL_ERR_INTERNAL_ERROR);
        TC_LOG_WARN("cheat", "Player {} attempted to send mail to {} ({}) with negative COD value (Cod: {})",
            GetPlayerInfo(), sendMail.Info.Target, receiverGuid.ToString(), sendMail.Info.Cod);
        return;
    }

    TC_LOG_INFO("network", "Player {} is sending mail to {} ({}) with subject {} and body {} "
        "including {} items, {} copper and {} COD copper with StationeryID = {}, PackageID = {}",
        GetPlayerInfo(), sendMail.Info.Target, receiverGuid.ToString(), sendMail.Info.Subject,
        sendMail.Info.Body, sendMail.Info.Attachments.size(), sendMail.Info.SendMoney, sendMail.Info.Cod, sendMail.Info.StationeryID, sendMail.Info.PackageID);

    if (player->GetGUID() == receiverGuid)
    {
        player->SendMailResult(0, MAIL_SEND, MAIL_ERR_CANNOT_SEND_TO_SELF);
        return;
    }

    int32 cost = !sendMail.Info.Attachments.empty() ? 30 * sendMail.Info.Attachments.size() : 30;  // price hardcoded in client

    int32 reqmoney = cost + sendMail.Info.SendMoney;

    // Check for overflow
    if (reqmoney < sendMail.Info.SendMoney)
    {
        player->SendMailResult(0, MAIL_SEND, MAIL_ERR_NOT_ENOUGH_MONEY);
        return;
    }

    auto mailCountCheckContinuation = [this, player = _player, receiverGuid, mailInfo = std::move(sendMail.Info), reqmoney, cost](uint32 receiverTeam, uint64 mailsCount, uint8 receiverLevel, uint32 receiverAccountId) mutable
    {
        if (_player != player)
            return;

        if (!player->HasEnoughMoney(reqmoney) && !player->IsGameMaster())
        {
            player->SendMailResult(0, MAIL_SEND, MAIL_ERR_NOT_ENOUGH_MONEY);
            return;
        }

        // do not allow to have more than 100 mails in mailbox.. mails count is in opcode uint8!!! - so max can be 255..
        if (mailsCount > 100)
        {
            player->SendMailResult(0, MAIL_SEND, MAIL_ERR_RECIPIENT_CAP_REACHED);
            return;
        }

        // test the receiver's Faction... or all items are account bound
        bool accountBound = !mailInfo.Attachments.empty();
        for (auto const& att : mailInfo.Attachments)
        {
            if (Item* item = player->GetItemByGuid(att.ItemGUID))
            {
                ItemTemplate const* itemProto = item->GetTemplate();
                if (!itemProto || !itemProto->HasFlag(ITEM_FLAG_IS_BOUND_TO_ACCOUNT))
                {
                    accountBound = false;
                    break;
                }
            }
        }

        if (!accountBound && player->GetTeam() != receiverTeam && !HasPermission(rbac::RBAC_PERM_TWO_SIDE_INTERACTION_MAIL))
        {
            player->SendMailResult(0, MAIL_SEND, MAIL_ERR_NOT_YOUR_TEAM);
            return;
        }

        if (receiverLevel < sWorld->getIntConfig(CONFIG_MAIL_LEVEL_REQ))
        {
            SendNotification(GetTrinityString(LANG_MAIL_RECEIVER_REQ), sWorld->getIntConfig(CONFIG_MAIL_LEVEL_REQ));
            return;
        }

        std::vector<Item*> items;

        for (auto const& att : mailInfo.Attachments)
        {
            if (att.ItemGUID.IsEmpty())
            {
                player->SendMailResult(0, MAIL_SEND, MAIL_ERR_MAIL_ATTACHMENT_INVALID);
                return;
            }

            Item* item = player->GetItemByGuid(att.ItemGUID);

            // prevent sending bag with items (cheat: can be placed in bag after adding equipped empty bag to mail)
            if (!item)
            {
                player->SendMailResult(0, MAIL_SEND, MAIL_ERR_MAIL_ATTACHMENT_INVALID);
                return;
            }

            // handle empty bag before CanBeTraded, since that func already has that check
            if (item->IsNotEmptyBag())
            {
                player->SendMailResult(0, MAIL_SEND, MAIL_ERR_EQUIP_ERROR, EQUIP_ERR_CAN_ONLY_DO_WITH_EMPTY_BAGS);
                return;
            }

            if (!item->CanBeTraded(true))
            {
                player->SendMailResult(0, MAIL_SEND, MAIL_ERR_EQUIP_ERROR, EQUIP_ERR_MAIL_BOUND_ITEM);
                return;
            }

            if (item->IsBoundAccountWide() && item->IsSoulBound() && GetAccountId() != receiverAccountId)
            {
                player->SendMailResult(0, MAIL_SEND, MAIL_ERR_EQUIP_ERROR, EQUIP_ERR_ARTEFACTS_ONLY_FOR_OWN_CHARACTERS);
                return;
            }

            if (item->GetTemplate()->HasFlag(ITEM_FLAG_CONJURED) || item->GetUInt32Value(ITEM_FIELD_DURATION))
            {
                player->SendMailResult(0, MAIL_SEND, MAIL_ERR_EQUIP_ERROR, EQUIP_ERR_MAIL_BOUND_ITEM);
                return;
            }

            if (mailInfo.Cod && item->IsWrapped())
            {
                player->SendMailResult(0, MAIL_SEND, MAIL_ERR_CANT_SEND_WRAPPED_COD);
                return;
            }

            items.push_back(item);
        }

        player->SendMailResult(0, MAIL_SEND, MAIL_OK);

        player->ModifyMoney(-int32(reqmoney));
        player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_GOLD_SPENT_FOR_MAIL, cost);

        bool needItemDelay = false;

        MailDraft draft(mailInfo.Subject, mailInfo.Body);

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        if (!mailInfo.Attachments.empty() || mailInfo.SendMoney > 0)
        {
            bool log = HasPermission(rbac::RBAC_PERM_LOG_GM_TRADE);
            if (!mailInfo.Attachments.empty())
            {
                for (Item* item : items)
                {
                    if (log)
                    {
                        sLog->OutCommand(GetAccountId(), "GM {} (GUID: {}) (Account: {}) mail item: {} (Entry: {} Count: {}) "
                            "to: {} ({}) (Account: {})", GetPlayerName(), GetGUIDLow(), GetAccountId(),
                            item->GetTemplate()->Name1, item->GetEntry(), item->GetCount(),
                            mailInfo.Target, receiverGuid.ToString(), receiverAccountId);
                    }

                    item->SetNotRefundable(GetPlayer()); // makes the item no longer refundable
                    player->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);

                    item->DeleteFromInventoryDB(trans);     // deletes item from character's inventory
                    item->SetOwnerGUID(receiverGuid);
                    item->SetState(ITEM_CHANGED);
                    item->SaveToDB(trans);                  // recursive and not have transaction guard into self, item not in inventory and can be save standalone

                    draft.AddItem(item);
                }

                // if item send to character at another account, then apply item delivery delay
                needItemDelay = GetAccountId() != receiverAccountId;
            }

            if (log && mailInfo.SendMoney > 0)
            {
                sLog->OutCommand(GetAccountId(), "GM {} (GUID: {}) (Account: {}) mail money: {} to: {} ({}) (Account: {})",
                    GetPlayerName(), GetGUIDLow(), GetAccountId(), mailInfo.SendMoney, mailInfo.Target, receiverGuid.ToString(), receiverAccountId);
            }
        }

        // If theres is an item, there is a one hour delivery delay if sent to another account's character.
        uint32 deliver_delay = needItemDelay ? sWorld->getIntConfig(CONFIG_MAIL_DELIVERY_DELAY) : 0;

        // don't ask for COD if there are no items
        if (mailInfo.Attachments.empty())
            mailInfo.Cod = 0;

        // will delete item or place to receiver mail list
        draft
            .AddMoney(mailInfo.SendMoney)
            .AddCOD(mailInfo.Cod)
            .SendMailTo(trans, MailReceiver(ObjectAccessor::FindConnectedPlayer(receiverGuid), receiverGuid.GetCounter()), MailSender(player), mailInfo.Body.empty() ? MAIL_CHECK_MASK_COPIED : MAIL_CHECK_MASK_HAS_BODY, deliver_delay);

        player->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);
    };

    if (Player* receiver = ObjectAccessor::FindConnectedPlayer(receiverGuid))
    {
        mailCountCheckContinuation(receiver->GetTeam(), receiver->GetMailSize(), receiver->GetLevel(), receiver->GetSession()->GetAccountId());
    }
    else
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_MAIL_COUNT);
        stmt->setUInt32(0, receiverGuid.GetCounter());

        GetQueryProcessor().AddCallback(CharacterDatabase.AsyncQuery(stmt)
            .WithPreparedCallback([continuation = std::move(mailCountCheckContinuation), receiverGuid](PreparedQueryResult result) mutable
        {
            if (CharacterCacheEntry const* characterInfo = sCharacterCache->GetCharacterCacheByGuid(receiverGuid))
                continuation(Player::TeamForRace(characterInfo->Race), result ? (*result)[0].GetUInt64() : UI64LIT(0), characterInfo->Level, characterInfo->AccountId);
        }));
    }
}

/**
 * @brief 处理将邮件标记为已读的请求
 *
 * 当玩家打开一封邮件时，客户端发送此消息将该邮件标记为已读状态。
 * 这会影响未读邮件计数和邮件列表的显示。
 *
 * @param markAsRead 标记已读的数据包，包含邮箱GUID和邮件ID
 *
 * 调用时机：
 * - 玩家打开邮件阅读时
 * - 客户端发送 CMSG_MAIL_MARK_AS_READ 消息
 *
 * 处理流程：
 * 1. 验证邮箱访问权限
 * 2. 获取邮件对象
 * 3. 更新邮件的已读标志
 * 4. 减少未读邮件计数
 * 5. 标记邮件状态为已修改
 *
 * @see Player::GetMail()
 * @see MAIL_CHECK_MASK_READ
 */
//called when mail is read
void WorldSession::HandleMailMarkAsRead(WorldPackets::Mail::MailMarkAsRead& markAsRead)
{
    if (!CanOpenMailBox(markAsRead.Mailbox))
        return;

    Player* player = _player;
    Mail* m = player->GetMail(markAsRead.MailID);
    if (m && m->state != MAIL_STATE_DELETED)
    {
        if (player->unReadMails)
            --player->unReadMails;
        m->checked = m->checked | MAIL_CHECK_MASK_READ;
        player->m_mailsUpdated = true;
        m->state = MAIL_STATE_CHANGED;
    }
}

/**
 * @brief 处理删除邮件的请求
 *
 * 当玩家删除邮件时，系统将邮件标记为删除状态。
 * COD邮件不能被删除，必须先取出附件。
 *
 * @param mailDelete 删除邮件的数据包，包含邮箱GUID和邮件ID
 *
 * 调用时机：
 * - 玩家在邮箱界面点击删除按钮时
 * - 客户端发送 CMSG_MAIL_DELETE 消息
 *
 * 处理流程：
 * 1. 验证邮箱访问权限
 * 2. 获取邮件对象
 * 3. 检查是否为COD邮件（COD邮件不能删除）
 * 4. 将邮件状态标记为已删除
 * 5. 发送删除结果给客户端
 *
 * 安全措施：
 * - COD邮件不能删除，防止绕过付款
 * - 即使邮件不存在也返回成功，防止信息泄露
 *
 * @note 邮件删除后会从数据库中移除，包含的物品也会被删除
 *
 * @see Player::GetMail()
 */
//called when client deletes mail
void WorldSession::HandleMailDelete(WorldPackets::Mail::MailDelete& mailDelete)
{
    if (!CanOpenMailBox(mailDelete.Mailbox))
        return;

    Mail* m = _player->GetMail(mailDelete.MailID);
    Player* player = _player;
    player->m_mailsUpdated = true;
    if (m)
    {
        // delete shouldn't show up for COD mails
        if (m->COD)
        {
            player->SendMailResult(mailDelete.MailID, MAIL_DELETED, MAIL_ERR_INTERNAL_ERROR);
            return;
        }

        m->state = MAIL_STATE_DELETED;
    }
    player->SendMailResult(mailDelete.MailID, MAIL_DELETED, MAIL_OK);
}

/**
 * @brief 处理退回邮件的请求
 *
 * 当玩家将邮件退回给发件人时，系统会创建一封新邮件发回给原始发件人，
 * 包含原始邮件的所有附件和金币，并删除原始邮件。
 *
 * @param returnToSender 退回邮件的数据包，包含邮箱GUID和邮件ID
 *
 * 调用时机：
 * - 玩家在邮箱界面点击退回按钮时
 * - 客户端发送 CMSG_MAIL_RETURN_TO_SENDER 消息
 *
 * 处理流程：
 * 1. 验证邮箱访问权限
 * 2. 验证邮件存在且未删除
 * 3. 验证邮件已投递（投递时间已过）
 * 4. 从数据库删除原始邮件及其物品记录
 * 5. 从玩家邮件列表移除邮件
 * 6. 如果发件人存在：
 *    - 创建新邮件草稿
 *    - 将附件物品添加到新邮件
 *    - 发送退回邮件给发件人
 * 7. 如果发件人不存在，物品被删除
 * 8. 释放邮件对象内存
 *
 * 退回规则：
 * - 只有普通邮件可以退回
 * - 退回邮件保留原始主题和正文
 * - 退回邮件保留所有附件和金币
 * - 邮件模板邮件可以退回
 *
 * @note 如果发件人角色已删除，退回的物品将永久丢失
 *
 * @see MailDraft::SendReturnToSender()
 */
void WorldSession::HandleMailReturnToSender(WorldPackets::Mail::MailReturnToSender& returnToSender)
{
    if (!CanOpenMailBox(returnToSender.Mailbox))
        return;

    Player* player = _player;
    Mail* m = player->GetMail(returnToSender.MailID);
    if (!m || m->state == MAIL_STATE_DELETED || m->deliver_time > GameTime::GetGameTime())
    {
        player->SendMailResult(returnToSender.MailID, MAIL_RETURNED_TO_SENDER, MAIL_ERR_INTERNAL_ERROR);
        return;
    }
    //we can return mail now
    //so firstly delete the old one
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_MAIL_BY_ID);
    stmt->setUInt32(0, returnToSender.MailID);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_MAIL_ITEM_BY_ID);
    stmt->setUInt32(0, returnToSender.MailID);
    trans->Append(stmt);

    player->RemoveMail(returnToSender.MailID);

    // only return mail if the player exists (and delete if not existing)
    if (m->messageType == MAIL_NORMAL && m->sender)
    {
        MailDraft draft(m->subject, m->body);
        if (m->mailTemplateId)
            draft = MailDraft(m->mailTemplateId, false);     // items already included

        if (m->HasItems())
        {
            for (MailItemInfoVec::iterator itr2 = m->items.begin(); itr2 != m->items.end(); ++itr2)
            {
                Item* item = player->GetMItem(itr2->item_guid);
                if (item)
                    draft.AddItem(item);
                else
                {
                    //WTF?
                }

                player->RemoveMItem(itr2->item_guid);
            }
        }
        draft.AddMoney(m->money).SendReturnToSender(GetAccountId(), m->receiver, m->sender, trans);
    }

    CharacterDatabase.CommitTransaction(trans);

    delete m;                                               //we can deallocate old mail
    player->SendMailResult(returnToSender.MailID, MAIL_RETURNED_TO_SENDER, MAIL_OK);
}

/**
 * @brief 处理取出邮件附件物品的请求
 *
 * 当玩家点击邮件中的附件物品时，系统将物品转移到玩家背包，
 * 并处理货到付款（COD）逻辑。
 *
 * @param takeItem 取出物品的数据包，包含邮箱GUID、邮件ID和附件ID
 *
 * 调用时机：
 * - 玩家在邮件界面点击附件物品图标时
 * - 客户端发送 CMSG_MAIL_TAKE_ITEM 消息
 *
 * 处理流程：
 * 1. 验证邮箱访问权限
 * 2. 获取邮件对象并验证有效性
 * 3. 验证邮件包含指定的附件物品
 * 4. 如果是COD邮件，验证玩家有足够的金币
 * 5. 检查玩家背包是否有空间
 * 6. 如果是COD邮件：
 *    - 从玩家背包扣除COD金额
 *    - 将COD金额邮寄给发件人
 *    - 记录GM交易日志（如有权限）
 * 7. 从邮件中移除附件物品
 * 8. 将物品添加到玩家背包
 * 9. 更新邮件状态并保存
 *
 * COD处理：
 * - COD金额在取出物品时扣除
 * - COD金额会邮寄给原始发件人
 * - 取出物品后COD标记清零
 *
 * 安全措施：
 * - 验证附件确实在邮件中，防止作弊取出COD物品
 * - 验证玩家有足够金币支付COD
 * - 物品转移使用数据库事务
 *
 * @see Player::MoveItemToInventory()
 * @see MailDraft::SendMailTo()
 */
//called when player takes item attached in mail
void WorldSession::HandleMailTakeItem(WorldPackets::Mail::MailTakeItem& takeItem)
{
    if (!CanOpenMailBox(takeItem.Mailbox))
        return;

    Player* player = _player;

    Mail* m = player->GetMail(takeItem.MailID);
    if (!m || m->state == MAIL_STATE_DELETED || m->deliver_time > GameTime::GetGameTime())
    {
        player->SendMailResult(takeItem.MailID, MAIL_ITEM_TAKEN, MAIL_ERR_INTERNAL_ERROR);
        return;
    }

    // verify that the mail has the item to avoid cheaters taking COD items without paying
    if (std::find_if(m->items.begin(), m->items.end(), [attachId = uint32(takeItem.AttachID)](MailItemInfo info){ return info.item_guid == attachId; }) == m->items.end())
    {
        player->SendMailResult(takeItem.MailID, MAIL_ITEM_TAKEN, MAIL_ERR_INTERNAL_ERROR);
        return;
    }

    // prevent cheating with skip client money check
    if (!player->HasEnoughMoney(m->COD))
    {
        player->SendMailResult(takeItem.MailID, MAIL_ITEM_TAKEN, MAIL_ERR_NOT_ENOUGH_MONEY);
        return;
    }

    Item* it = player->GetMItem(takeItem.AttachID);

    ItemPosCountVec dest;
    uint8 msg = _player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, it, false);
    if (msg == EQUIP_ERR_OK)
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        m->RemoveItem(takeItem.AttachID);
        m->removedItems.push_back(takeItem.AttachID);

        if (m->COD > 0)                                     //if there is COD, take COD money from player and send them to sender by mail
        {
            ObjectGuid sender_guid(HighGuid::Player, m->sender);
            Player* receiver = ObjectAccessor::FindConnectedPlayer(sender_guid);

            uint32 sender_accId = 0;

            if (HasPermission(rbac::RBAC_PERM_LOG_GM_TRADE))
            {
                std::string sender_name;
                if (receiver)
                {
                    sender_accId = receiver->GetSession()->GetAccountId();
                    sender_name = receiver->GetName();
                }
                else
                {
                    // can be calculated early
                    sender_accId = sCharacterCache->GetCharacterAccountIdByGuid(sender_guid);

                    if (!sCharacterCache->GetCharacterNameByGuid(sender_guid, sender_name))
                        sender_name = sObjectMgr->GetTrinityStringForDBCLocale(LANG_UNKNOWN);
                }
                sLog->OutCommand(GetAccountId(), "GM {} (Account: {}) receiver mail item: {} (Entry: {} Count: {}) and send COD money: {} to player: {} (Account: {})",
                    GetPlayerName(), GetAccountId(), it->GetTemplate()->Name1, it->GetEntry(), it->GetCount(), m->COD, sender_name, sender_accId);
            }
            else if (!receiver)
                sender_accId = sCharacterCache->GetCharacterAccountIdByGuid(sender_guid);

            // check player existence
            if (receiver || sender_accId)
            {
                MailDraft(m->subject, "")
                    .AddMoney(m->COD)
                    .SendMailTo(trans, MailReceiver(receiver, m->sender), MailSender(MAIL_NORMAL, m->receiver), MAIL_CHECK_MASK_COD_PAYMENT);
            }

            player->ModifyMoney(-int32(m->COD));
        }
        m->COD = 0;
        m->state = MAIL_STATE_CHANGED;
        player->m_mailsUpdated = true;
        player->RemoveMItem(it->GetGUID().GetCounter());

        uint32 count = it->GetCount();                      // save counts before store and possible merge with deleting
        it->SetState(ITEM_UNCHANGED);                       // need to set this state, otherwise item cannot be removed later, if neccessary
        player->MoveItemToInventory(dest, it, true);

        player->SaveInventoryAndGoldToDB(trans);
        player->_SaveMail(trans);
        CharacterDatabase.CommitTransaction(trans);

        player->SendMailResult(takeItem.MailID, MAIL_ITEM_TAKEN, MAIL_OK, 0, takeItem.AttachID, count);
    }
    else
        player->SendMailResult(takeItem.MailID, MAIL_ITEM_TAKEN, MAIL_ERR_EQUIP_ERROR, msg);
}

/**
 * @brief 处理取出邮件中金币的请求
 *
 * 当玩家点击邮件中的金币时，系统将金币添加到玩家背包，
 * 并清空邮件中的金币字段。
 *
 * @param takeMoney 取出金币的数据包，包含邮箱GUID和邮件ID
 *
 * 调用时机：
 * - 玩家在邮件界面点击金币图标时
 * - 客户端发送 CMSG_MAIL_TAKE_MONEY 消息
 *
 * 处理流程：
 * 1. 验证邮箱访问权限
 * 2. 获取邮件对象并验证有效性
 * 3. 验证邮件已投递（投递时间已过）
 * 4. 将金币添加到玩家背包（检查金币上限）
 * 5. 清空邮件的金币字段
 * 6. 更新邮件状态为已修改
 * 7. 保存金币和邮件到数据库
 *
 * 金币限制：
 * - 玩家金币有上限，超过上限时无法取出
 * - 金币取出后邮件仍保留，直到被删除
 *
 * 安全措施：
 * - 保存金币和邮件到数据库防止作弊
 * - 使用事务保证数据一致性
 *
 * @see Player::ModifyMoney()
 */
void WorldSession::HandleMailTakeMoney(WorldPackets::Mail::MailTakeMoney& takeMoney)
{
    if (!CanOpenMailBox(takeMoney.Mailbox))
        return;

    Player* player = _player;

    Mail* m = player->GetMail(takeMoney.MailID);
    if (!m || m->state == MAIL_STATE_DELETED || m->deliver_time > GameTime::GetGameTime())
    {
        player->SendMailResult(takeMoney.MailID, MAIL_MONEY_TAKEN, MAIL_ERR_INTERNAL_ERROR);
        return;
    }

    if (!player->ModifyMoney(m->money, false))
    {
        player->SendMailResult(takeMoney.MailID, MAIL_MONEY_TAKEN, MAIL_ERR_EQUIP_ERROR, EQUIP_ERR_TOO_MUCH_GOLD);
        return;
    }

    m->money = 0;
    m->state = MAIL_STATE_CHANGED;
    player->m_mailsUpdated = true;

    player->SendMailResult(takeMoney.MailID, MAIL_MONEY_TAKEN, MAIL_OK);

    // save money and mail to prevent cheating
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    player->SaveGoldToDB(trans);
    player->_SaveMail(trans);
    CharacterDatabase.CommitTransaction(trans);
}

/**
 * @brief 处理获取邮件列表的请求
 *
 * 当玩家打开邮箱时，客户端发送此消息请求获取该玩家的所有邮件列表。
 * 服务器返回所有有效邮件的详细信息。
 *
 * @param getList 获取邮件列表的数据包，包含邮箱GUID
 *
 * 调用时机：
 * - 玩家打开邮箱界面时
 * - 客户端发送 CMSG_GET_MAIL_LIST 消息
 *
 * 处理流程：
 * 1. 验证邮箱访问权限
 * 2. 遍历玩家的所有邮件
 * 3. 过滤掉已删除和未投递的邮件
 * 4. 构建邮件列表响应包
 * 5. 发送邮件列表给客户端
 * 6. 更新下次邮件投递时间和未读邮件计数
 *
 * 邮件过滤：
 * - 跳过已删除的邮件
 * - 跳过投递时间未到的邮件
 *
 * @see Player::GetMails()
 * @see Player::UpdateNextMailTimeAndUnreads()
 */
//called when player lists his received mails
void WorldSession::HandleGetMailList(WorldPackets::Mail::MailGetList& getList)
{
    if (!CanOpenMailBox(getList.Mailbox))
        return;

    Player* player = _player;

    WorldPackets::Mail::MailListResult response;
    time_t curTime = GameTime::GetGameTime();

    for (Mail* m : player->GetMails())
    {
        // skip deleted or not delivered (deliver delay not expired) mails
        if (m->state == MAIL_STATE_DELETED || curTime < m->deliver_time)
            continue;

        response.AddMail(m, _player);
    }

    SendPacket(response.Write());

    // recalculate m_nextMailDelivereTime and unReadMails
    _player->UpdateNextMailTimeAndUnreads();
}

/**
 * @brief 处理将邮件正文复制为物品的请求
 *
 * 当玩家点击"复制邮件内容"时，系统创建一个包含邮件正文的物品，
 * 允许玩家保存邮件内容到背包中。
 *
 * @param createTextItem 创建文本物品的数据包，包含邮箱GUID和邮件ID
 *
 * 调用时机：
 * - 玩家在邮件界面点击复制内容按钮时
 * - 客户端发送 CMSG_MAIL_CREATE_TEXT_ITEM 消息
 *
 * 处理流程：
 * 1. 验证邮箱访问权限
 * 2. 获取邮件对象并验证有效性
 * 3. 验证邮件有正文或邮件模板
 * 4. 验证邮件未被复制过
 * 5. 创建邮件正文物品（模板ID: MAIL_BODY_ITEM_TEMPLATE）
 * 6. 设置物品的文本内容
 * 7. 设置物品的创建者信息
 * 8. 将物品添加到玩家背包
 * 9. 标记邮件为已复制
 *
 * 物品属性：
 * - 使用固定的邮件正文物品模板
 * - 物品包含邮件的完整正文
 * - 物品标记为邮件文本类型
 *
 * 限制条件：
 * - 邮件必须有正文内容
 * - 每封邮件只能复制一次
 * - 玩家背包需要有空间
 *
 * @see Item::SetText()
 */
//used when player copies mail body to his inventory
void WorldSession::HandleMailCreateTextItem(WorldPackets::Mail::MailCreateTextItem& createTextItem)
{
    if (!CanOpenMailBox(createTextItem.Mailbox))
        return;

    Player* player = _player;

    Mail* m = player->GetMail(createTextItem.MailID);
    if (!m || (m->body.empty() && !m->mailTemplateId) || m->state == MAIL_STATE_DELETED || m->deliver_time > GameTime::GetGameTime() || (m->checked & MAIL_CHECK_MASK_COPIED))
    {
        player->SendMailResult(createTextItem.MailID, MAIL_MADE_PERMANENT, MAIL_ERR_INTERNAL_ERROR);
        return;
    }

    Item* bodyItem = new Item;                              // This is not bag and then can be used new Item.
    if (!bodyItem->Create(sObjectMgr->GetGenerator<HighGuid::Item>().Generate(), MAIL_BODY_ITEM_TEMPLATE, player))
    {
        delete bodyItem;
        return;
    }

    // in mail template case we need create new item text
    if (m->mailTemplateId)
    {
        MailTemplateEntry const* mailTemplateEntry = sMailTemplateStore.LookupEntry(m->mailTemplateId);
        ASSERT(mailTemplateEntry);
        bodyItem->SetText(mailTemplateEntry->Body[GetSessionDbcLocale()]);
    }
    else
        bodyItem->SetText(m->body);

    if (m->messageType == MAIL_NORMAL)
        bodyItem->SetGuidValue(ITEM_FIELD_CREATOR, ObjectGuid(HighGuid::Player, m->sender));

    bodyItem->SetFlag(ITEM_FIELD_FLAGS, ITEM_FLAG_MAIL_TEXT_MASK);

    TC_LOG_INFO("network", "HandleMailCreateTextItem mailid={}", createTextItem.MailID);

    ItemPosCountVec dest;
    uint8 msg = _player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, bodyItem, false);
    if (msg == EQUIP_ERR_OK)
    {
        m->checked = m->checked | MAIL_CHECK_MASK_COPIED;
        m->state = MAIL_STATE_CHANGED;
        player->m_mailsUpdated = true;

        player->StoreItem(dest, bodyItem, true);
        player->SendMailResult(createTextItem.MailID, MAIL_MADE_PERMANENT, MAIL_OK);
    }
    else
    {
        player->SendMailResult(createTextItem.MailID, MAIL_MADE_PERMANENT, MAIL_ERR_EQUIP_ERROR, msg);
        delete bodyItem;
    }
}

/**
 * @brief 处理查询下次邮件到达时间的请求
 *
 * 客户端定期查询下次邮件到达时间，用于显示邮件到达提示。
 * 服务器返回未读邮件的发送者信息（最多2个）。
 *
 * @param queryNextMailTime 查询邮件时间的数据包（空数据包）
 *
 * 调用时机：
 * - 客户端定期自动查询
 * - 玩家有未读邮件时
 * - 客户端发送 CMSG_QUERY_NEXT_MAIL_TIME 消息
 *
 * 处理流程：
 * 1. 检查玩家是否有未读邮件
 * 2. 如果没有未读邮件，返回-1天（表示无邮件）
 * 3. 如果有未读邮件：
 *    - 返回时间为0（表示立即）
 *    - 收集未读邮件的发送者信息
 *    - 每个发送者只显示一次
 *    - 最多返回2个发送者信息
 *
 * 响应内容：
 * - 下次邮件到达时间
 * - 未读邮件发送者列表（最多2个）
 *
 * @note 客户端会根据返回值显示"你有新邮件"的提示
 */
void WorldSession::HandleQueryNextMailTime(WorldPackets::Mail::MailQueryNextMailTime& /*queryNextMailTime*/)
{
    WorldPackets::Mail::MailQueryNextTimeResult result;

    if (_player->unReadMails > 0)
    {
        result.NextMailTime = 0.0f;

        time_t now = GameTime::GetGameTime();
        std::set<uint32> sentSenders;
        for (Mail* m : _player->GetMails())
        {
            // must be not checked yet
            if (m->checked & MAIL_CHECK_MASK_READ)
                continue;

            // and already delivered
            if (now < m->deliver_time)
                continue;

            // only send each mail sender once
            if (sentSenders.count(m->sender))
                continue;

            result.Next.emplace_back(m);

            sentSenders.insert(m->sender);

            // do not send more than 2 mails
            if (sentSenders.size() > 2)
                break;
        }
    }
    else
        result.NextMailTime = -DAY;

    SendPacket(result.Write());
}
