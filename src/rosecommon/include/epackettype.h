// Copyright 2016 Chirstopher Torres (Raven), L3nn0x
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http ://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
 * epackettype.h
 *
 *  Created on: Nov 10, 2015
 *      Author: ctorres
 */

#ifndef EPACKETTYPE_H_
#define EPACKETTYPE_H_

#include <string>
#include <stdint.h>

namespace RoseCommon {

#ifdef _WIN32
  #ifndef __MINGW32__
    #define PACK(...) __pragma(pack(push, 1)) __VA_ARGS__ __pragma(pack(pop))
  #else
    #define PACK(...) __VA_ARGS__ __attribute__((__packed__))
  #endif
#else
  #define PACK(...) __VA_ARGS__ __attribute__((__packed__))
#endif

#ifndef MAX_PACKET_SIZE
#define MAX_PACKET_SIZE 0xFFF
#endif

// CS = Client -> server
// SC = server -> server
// SS = server -> server
// LC = Login -> server
// CC = Char -> Client
// WC = World -> client
enum class ePacketType : uint16_t {
  ISCSTART = 0x300,
  ISC_ALIVE,
  ISC_SERVER_REGISTER,
  ISC_TRANSFER, // transfer to specific map(s)
  ISC_TRANSFER_CHAR, // transfer to specific character(s) (map(s) is/are determined automatically for you)
  ISC_CLIENT_STATUS, // maps send this packet to the char server every time the status of a client changes
  ISC_SHUTDOWN,
  ISCEND,

  // CLIENT PACKETS START HERE!!!
  PAKSTART = 0x500,
  PAKCS_ALIVE = PAKSTART,
  PAKSS_ERROR,
  PAKSS_ANNOUNCE_TEXT,
  PAKSW_ANNOUNCE_CHAT,
  PAKCS_ACCEPT_REQ,
  PAKCS_CHANNEL_LIST_REQ,
  PAKLC_CHANNEL_LIST_REPLY,

  PAKCS_LOGOUT_REQ,
  PAKWC_LOGOUT_REPLY,
  PAKCS_LOGIN_REQ,
  PAKCS_LOGIN_TOKEN_REQ,
  PAKLC_LOGIN_REPLY,
  PAKGC_LOGIN_REPLY,
  PAKCS_SRV_SELECT_REQ,
  PAKLC_SRV_SELECT_REPLY,

  PAKCS_JOIN_SERVER_REQ,
  PAKCS_JOIN_SERVER_TOKEN_REQ,
  PAKSC_JOIN_SERVER_REPLY,
  PAKWC_GM_COMMAND,

  PAKWC_GLOBAL_VARS,
  PAKWC_GLOBAL_FLAGS,

  PAKCC_SWITCH_SERVER,
  PAKCS_CHAR_LIST_REQ,
  PAKCC_CHAR_LIST_REPLY,
  PAKCS_CREATE_CHAR_REQ,
  PAKCC_CREATE_CHAR_REPLY,
  PAKCS_DELETE_CHAR_REQ,
  PAKCC_DELETE_CHAR_REPLY,
  PAKCS_SELECT_CHAR_REQ,
  PAKWC_SELECT_CHAR_REPLY,

  PAKWC_INVENTORY_DATA,
  PAKWC_SET_MONEY_AND_ITEM,
  PAKWC_SET_ITEM,
  PAKWC_SERVER_DATA,

  PAKWC_QUEST_DATA,
  PAKCS_CHANGE_CHAR_REQ,
  PAKCC_CHAN_CHAR_REPLY,
  PAKWC_SET_MONEY,
  PAKWC_QUEST_REWARD_MONEY,
  PAKWC_QUEST_REWARD_ITEM,
  PAKWC_QUEST_REWARD_ADD_VALUE,
  PAKWC_QUEST_REWARD_SET_VALUE,
  PAKCS_CANCEL_LOGOUT,
  PAKWC_QUEST_UPDATE,
  PAKWC_WISH_LIST,

  PAKCS_QUEST_DATA_REQ,
  PAKWC_QUEST_DATA_REPLY,
  PAKWC_NPC_EVENT,

  PAKWC_GM_COMMAND_CODE, // This is for updating client side varibles
  PAKCS_CHANGE_MAP_REQ,
  PAKWC_CHANGE_MAP_REPLY,
  PAKWC_INIT_DATA,
  PAKCS_REVIVE_REQ,
  PAKWC_REVIVE_REPLY,
  PAKCS_REVIVE_SET_POS,
  PAKCS_SET_SERVER_VAR_REQ,
  PAKWC_SET_SERVER_VAR_REPLY,

  PAKCS_CHAR_INFO_REQ,
  PAKWC_CHAR_INFO_REPLY,
  PAKCS_SET_WEIGHT_REQ,
  PAKWC_SET_WEIGHT,

  PAKWC_SET_POSITION,
  PAKCS_STOP_MOVING, // Client stopped moving because it can't anymore
  PAKWC_STOP_MOVING, // Handled the same as PAKWC_STOP in the client

  PAKWC_UPDATE_NPC,
  PAKCS_SUMMON_CMD,
  PAKWC_SUMMON_CMD,

  PAKCS_SET_ANIMATION,
  PACWC_SET_ANIMATION,
  PAKCS_TOGGLE_MOVE,
  PAKWC_TOGGLE_MOVE,
  PAKCS_NORMAL_CHAT,
  PAKWC_NORMAL_CHAT,
  PAKCS_WHISPER_CHAT,
  PAKWC_WHISPER_CHAT,
  PAKCS_SHOUT_CHAT,
  PAKWC_SHOUT_CHAT,
  PAKCS_PARTY_CHAT,
  PAKWC_PARTY_CHAT,
  PAKCS_CLAN_CHAT,
  PAKWC_CLAN_CHAT,
  PAKCS_ALLIED_CHAT,
  PAKWC_ALLIED_CHAT,
  PAKCS_ALLIED_SHOUT_CHAT,
  PAKWC_ALLIED_SHOUT_CHAT,

  PAKWC_EVENT_STATUS,
  PAKWC_NPC_CHAR,
  PAKWC_MOB_CHAR,
  PAKWC_PLAYER_CHAR,
  PAKWC_REMOVE_OBJECT,
  PAKCS_SET_POSITION,
  PAKCS_STOP, // client wants to stop
  PAKWC_STOP, // object stops at position x
  PAKWC_MOVE, // mouse cmd with move mode in it??
  
  PAKCS_ATTACK,
  PAKWC_ATTACK,
  
  PAKCS_DAMAGE,
  PAKWC_DAMAGE,

  PAKCS_MOUSE_CMD, // client wants to move or click on an object
  PAKWC_MOUSE_CMD, // answer from the server
  
  PAKWC_SET_EXP,
  PAKWC_LEVELUP,
  
  PAKCS_HP_REQ,
  PAKWC_HP_REPLY,
  
  PAKWC_SET_HP_AND_MP,
  
  PAKCS_STORE_TRADE_REQ,
  PAKWC_STORE_TRADE_REPLY,

  PAKCS_USE_ITEM,
  PAKWC_USE_ITEM,
  
  PAKCS_DROP_ITEM,

  PAKCS_EQUIP_ITEM,
  PAKWC_EQUIP_ITEM,
  
  PAKWC_DROP_ITEM,
  
  PAKCS_PICKUP_ITEM_REQ, //FIELD_ITEM_REQ in client
  PAKWC_PICKUP_ITEM_REPLY, // FIELD_ITEM_REPLY in client
  
  PAKCS_TELEPORT_REQ,
  PAKWC_TELEPORT_REPLY,
  
  PAKCS_STAT_ADD_REQ,
  PAKWC_STAT_ADD_REPLY,

  PAKCS_HOTBAR_SET_ICON_REQ,
  PAKWC_HOTBAR_SET_ICON_REPLY,
  
  PAKCS_EQUIP_PROJECTILE,
  PAKWC_EQUIP_PROJECTILE,

  PAKWC_CHANGE_SKIN,

  PAKCS_BANK_LIST_REQ,
  PAKWC_BANK_LIST_REPLY,
  PAKCS_BANK_MOVE_ITEM,
  PAKWC_BANK_MOVE_ITEM,
  
  PAKCS_CRAFT_REQ,
  PAKWC_CRAFT_REPLY,

  PAKWC_SKILL_LEARN,
  PAKCS_SKILL_LEVEL_REQ,
  PAKWC_SKILL_LEVEL_REPLY,
  PAKCS_SKILL_CAST_SELF,
  PAKWC_SKILL_CAST_SELF,
  PAKCS_SKILL_CAST_TARGET,
  PAKWC_SKILL_CAST_TARGET,
  PAKCS_SKILL_CAST_POSITION,
  PAKWC_SKILL_CAST_POSITION,
  PAKWC_SKILL_EFFECT,
  PAKWC_SKILL_DAMAGE,

  PAKWC_CLEAR_STATUS,
  PAKWC_SPEED_CHANGED,

  PAKWC_SKILL_FINISH,

  PAKCS_APPRAISAL_REQ,
  PAKWC_APPRAISAL_REPLY,

  PAKWC_SKILL_START,

  PAKCS_CRAFT_ENHANCE_REQ,
  PAKWC_CRAFT_ENHANCE_REPLY,

  PAKWC_SKILL_CANCEL,

  PAKCS_WISHLIST_ADD,

  PAKCS_TRADE,
  PAKWC_TRADE,
  PAKCS_TRADE_ITEM,
  PAKWC_TRADE_ITEM,

  PAKCS_SHOP_OPEN,
  PAKWC_SHOP_OPEN,
  PAKCS_SHOP_CLOSE,
  PAKWC_SHOP_CLOSE,
  PAKCS_SHOP_LIST_REQ,
  PAKWC_SHOP_LIST_REPLY,
  PAKCS_SHOP_BUY_REQ,
  PAKCS_SHOP_SELL_REQ,
  PAKCS_SHOP_BUYSELL_REPLY,

  PAKCS_EQUIP_ITEM_RIDE,
  PAKWC_EQUIP_ITEM_RIDE,
  PAKCS_REPAIR_USE_ITEM,
  PAKCS_REPAIR_NPC,
  PAKWC_SET_ITEM_LIFE,

  PAKCS_PARTY_REQ,
  PAKWC_PARTY_REQ,
  PAKCS_PARTY_REPLY,
  PAKWC_PARTY_REPLY,
  PAKWC_PARTY_MEMBER,
  PAKWC_PARTY_ITEM,
  PAKWC_PARTY_LEVELEXP,
  PAKWC_PARTY_MEMBER_UPDATE,

  PAKWC_EVENT_ADD,

  PAKCS_PARTY_RULE,
  PAKWC_PARTY_RULE,

  PAKCS_CRAFT_STATUS,
  PAKWC_CRAFT_STATUS,

  PAKCS_BANK_MOVE_MONEY,
  PAKWC_BANK_MOVE_MONEY,

  PAKWC_NPC_SHOW,
  PAKWC_FAIRY,

  PAKCS_RIDE_REQUEST,
  PAKWC_RIDE_REQUEST,

  PAKWC_BILLING_MESSAGE,
  PAKWC_BILLING_MESSAGE_EXT,

  PAKCS_CLAN_COMMAND,
  PAKCC_CLAN_COMMAND,

  PAKCS_MESSENGER,
  PAKCC_MESSENGER,
  PAKCS_MESSENGER_CHAT,
  PAKCC_MESSENGER_CHAT,
  PAKCS_CHATROOM,
  PAKCC_CHATROOM,
  PAKCS_CHATROOM_MESSAGE,
  PAKCC_CHATROOM_MESSAGE,
  PAKCS_MEMO,
  PAKCC_MEMO,

  PAKCS_CLAN_ICON_SET,
  PAKCS_CLAN_ICON_REQ,
  PAKCC_CLAN_ICON_REPLY,
  PAKCS_CLAN_ICON_TIMESTAMP,
  PAKCC_CLAN_ICON_TIMESTAMP,

  PAKWC_RIDE_STATE_CHANGE,
  PAWKC_CHAR_STATE_CHANGE,

  PAKCS_SCREEN_SHOT_TIME_REQ,
  PAKSC_SCREEN_SHOT_TIME_REPLY,
  PAKWC_UPDATE_NAME,

  PAKSS_ACCEPT_REPLY,
  EPACKETMAX,

  STRESS = 0x6F6D
};

inline bool operator!(const ePacketType& rhs) {
  return static_cast<int16_t>(rhs) == 0;
}
inline bool operator!=(const uint32_t& lhs, const ePacketType& rhs) {
  return (lhs != static_cast<uint32_t>(rhs));
}

template <typename E>
constexpr auto to_underlying(E e) noexcept -> std::underlying_type_t<E> {
  return static_cast<typename std::underlying_type_t<E>>(e);
}

static_assert(to_underlying(ePacketType::EPACKETMAX) < 0x7ff, "Our max packet ID will interfere with the encryption!");

struct EPacketTypeHash {
   template <typename T>
     std::size_t operator()(T t) const noexcept {
     return to_underlying(t);
   }
};


//TODO: put it in its correct place
struct tChannelInfo {
  uint16_t ChannelID;
  uint16_t Port;
  uint32_t MinRight;
  std::string channelName;
  std::string IPAddress;

  tChannelInfo()
      : ChannelID(0), Port(0), MinRight(0), channelName(""), IPAddress("") {}
};

}

#endif /* EPACKETTYPE_H_ */
