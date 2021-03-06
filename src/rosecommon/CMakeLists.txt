link_directories(${EXTERNAL_BINARY_DIR})

set(PROJECT "osIROSE-common-rosecommon")
project(${PROJECT})

set( SOURCES_HEADER
  include/crosecrypt.h
  include/crosepacket.h
  include/epackettype.h
  include/packet_dispatcher.h
  include/isccommon.h
  include/crosewriter.h
  include/dataconsts.h
  include/enums.h
  include/iserialize.h
  include/packetfactory.h
  include/types.h
)

SET(SOURCES_BASE
  src/crosecrypt.cpp
  src/isccommon.cpp
  src/packetfactory.cpp
  src/types.cpp
)

set(IDL_TYPE_FILES
  packets/equipped_item.xml
  packets/equipped_position.xml
  packets/item.xml
)

set(IDL_FILES
  packets/cli_accept_req.xml
  packets/cli_alive.xml
  packets/cli_allied_chat.xml
  packets/cli_allied_shout_chat.xml
  packets/cli_appraisal_req.xml
  packets/cli_attack.xml
  packets/cli_bank_list_req.xml
  packets/cli_bank_move_item.xml
  packets/cli_bank_move_money.xml
  packets/cli_change_char_req.xml
  packets/cli_change_map_req.xml
  packets/cli_channel_list_req.xml
  packets/cli_char_list_req.xml
  packets/cli_chatroom_message.xml
  packets/cli_chatroom.xml
  packets/cli_clan_chat.xml
  packets/cli_clan_command.xml
  packets/cli_clan_icon_req.xml
  packets/cli_clan_icon_set.xml
  packets/cli_clan_icon_timestamp.xml
  packets/cli_craft_enhance_req.xml
  packets/cli_craft_req.xml
  packets/cli_craft_status.xml
  packets/cli_create_char_req.xml
  packets/cli_damage.xml
  packets/cli_delete_char_req.xml
  packets/cli_drop_item.xml
  packets/cli_equip_item_ride.xml
  packets/cli_equip_item.xml
  packets/cli_equip_projectile.xml
  packets/cli_hotbar_set_icon_req.xml
  packets/cli_hp_req.xml
  packets/cli_join_server_req.xml
  packets/cli_login_req.xml
  packets/cli_logout_req.xml
  packets/cli_memo.xml
  packets/cli_messenger_chat.xml
  packets/cli_messenger.xml
  packets/cli_mouse_cmd.xml
  packets/cli_normal_chat.xml
  packets/cli_party_chat.xml
  packets/cli_party_reply.xml
  packets/cli_party_req.xml
  packets/cli_party_rule.xml
  packets/cli_pickup_item_req.xml
  packets/cli_quest_data_req.xml
  packets/cli_repair_npc.xml
  packets/cli_repair_use_item.xml
  packets/cli_revive_req.xml
  packets/cli_revive_set_pos.xml
  packets/cli_ride_request.xml
  packets/cli_stat_add_req.xml
  packets/cli_screen_shot_time_req.xml
  packets/cli_select_char_req.xml
  packets/cli_set_animation.xml
  packets/cli_set_position.xml
  packets/cli_set_server_var_req.xml
  packets/cli_set_weight_req.xml
  packets/cli_shop_close.xml
  packets/cli_shop_list_req.xml
  packets/cli_shop_open.xml
  packets/cli_shout_chat.xml
  packets/cli_skill_cast_position.xml
  packets/cli_skill_cast_self.xml
  packets/cli_skill_cast_target.xml
  packets/cli_skill_level_req.xml
  packets/cli_srv_select_req.xml
  packets/cli_stop_moving.xml
  packets/cli_stop.xml
  packets/cli_store_trade_req.xml
  packets/cli_teleport_req.xml
  packets/cli_toggle_move.xml
  packets/cli_trade_item.xml
  packets/cli_trade.xml
  packets/cli_use_item.xml
  packets/cli_whisper_chat.xml
  packets/isc_client_status.xml
  packets/isc_alive.xml
  packets/isc_server_register.xml
  packets/isc_transfer.xml
  packets/isc_transfer_char.xml
  packets/isc_shutdown.xml
  packets/srv_accept_reply.xml
  packets/srv_allied_chat.xml
  packets/srv_allied_shout_chat.xml
  packets/srv_announce_chat.xml
  packets/srv_appraisal_reply.xml
  packets/srv_attack.xml
  packets/srv_bank_list_reply.xml
  packets/srv_bank_move_item.xml
  packets/srv_bank_move_money.xml
  packets/srv_billing_message.xml
  packets/srv_chan_char_reply.xml
  packets/srv_change_map_reply.xml
  packets/srv_change_skin.xml
  packets/srv_channel_list_reply.xml
  packets/srv_chatroom_message.xml
  packets/srv_chatroom.xml
  packets/srv_char_list_reply.xml
  packets/srv_char_state_change.xml
  packets/srv_clan_chat.xml
  packets/srv_clan_command.xml
  packets/srv_clan_icon_reply.xml
  packets/srv_clan_icon_timestamp.xml
  packets/srv_clear_status.xml
  packets/srv_craft_enhance_reply.xml
  packets/srv_craft_reply.xml
  packets/srv_craft_status.xml
  packets/srv_create_char_reply.xml
  packets/srv_damage.xml
  packets/srv_delete_char_reply.xml
  packets/srv_drop_item.xml
  packets/srv_equip_item_ride.xml
  packets/srv_equip_item.xml
  packets/srv_equip_projectile.xml
  packets/srv_event_add.xml
  packets/srv_event_status.xml
  packets/srv_fairy.xml
  packets/srv_global_flags.xml
  packets/srv_global_vars.xml
  packets/srv_gm_command_code.xml
  packets/srv_hp_reply.xml
  packets/srv_inventory_data.xml
  packets/srv_join_server_reply.xml
  packets/srv_levelup.xml
  packets/srv_login_reply.xml
  packets/srv_logout_reply.xml
  packets/srv_memo.xml
  packets/srv_messenger.xml
  packets/srv_mob_char.xml
  packets/srv_mouse_cmd.xml
  packets/srv_move.xml
  packets/srv_normal_chat.xml
  packets/srv_npc_char.xml
  packets/srv_npc_event.xml
  packets/srv_npc_show.xml
  packets/srv_party_chat.xml
  packets/srv_party_item.xml
  packets/srv_party_levelexp.xml
  packets/srv_party_member_update.xml
  packets/srv_party_member.xml
  packets/srv_party_reply.xml
  packets/srv_party_req.xml
  packets/srv_party_rule.xml
  packets/srv_pickup_item_reply.xml
  packets/srv_player_char.xml
  packets/srv_quest_data_reply.xml
  packets/srv_quest_data.xml
  packets/srv_quest_reward_add_value.xml
  packets/srv_quest_reward_set_value.xml
  packets/srv_remove_object.xml
  packets/srv_revive_reply.xml
  packets/srv_ride_state_change.xml
  packets/srv_ride_request.xml
  packets/srv_screen_shot_time_reply.xml
  packets/srv_select_char_reply.xml
  packets/srv_server_data.xml
  packets/srv_set_animation.xml
  packets/srv_set_exp.xml
  packets/srv_set_hp_and_mp.xml
  packets/srv_set_item_life.xml
  packets/srv_set_item.xml
  packets/srv_set_money_and_item.xml
  packets/srv_set_money.xml
  packets/srv_set_position.xml
  packets/srv_set_server_var_reply.xml
  packets/srv_set_weight.xml
  packets/srv_shop_close.xml
  packets/srv_shop_list_reply.xml
  packets/srv_shop_open.xml
  packets/srv_shout_chat.xml
  packets/srv_skill_cancel.xml
  packets/srv_skill_cast_self.xml
  packets/srv_skill_cast_target.xml
  packets/srv_skill_damage.xml
  packets/srv_skill_effect.xml
  packets/srv_skill_finish.xml
  packets/srv_skill_learn.xml
  packets/srv_skill_level_reply.xml
  packets/srv_skill_start.xml
  packets/srv_speed_changed.xml
  packets/srv_srv_select_reply.xml
  packets/srv_stat_add_reply.xml
  packets/srv_stop_moving.xml
  packets/srv_stop.xml
  packets/srv_store_trade_reply.xml
  packets/srv_switch_server.xml
  packets/srv_teleport_reply.xml
  packets/srv_toggle_move.xml
  packets/srv_trade_item.xml
  packets/srv_trade.xml
  packets/srv_update_name.xml
  packets/srv_update_npc.xml
  packets/srv_use_item.xml
  packets/srv_whisper_chat.xml
)

set(args
  TARGET RosePackets
  IDLFILES ${IDL_FILES}
  IDLROOT ${CMAKE_CURRENT_SOURCE_DIR}/packets
  SRC_OUTPATH ${CMAKE_GEN_SRC_DIR}/packets
  HDR_OUTPATH ${CMAKE_GEN_SRC_DIR}/packets
)

generate_packets(PACKET_SOURCES PACKET_HEADERS ${args})

add_library(${PROJECT} STATIC ${SOURCES_BASE} ${PACKET_SOURCES} ${PACKET_HEADERS} ${SOURCES_HEADER})
target_include_directories(${PROJECT}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_GEN_SRC_DIR}/packets>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if (NOT MSVC)
  target_compile_options(${PROJECT} PRIVATE -Werror)
endif()

add_dependencies(${PROJECT} osIROSE-common-core)
add_dependencies(${PROJECT} RosePackets_generated)
target_link_libraries(${PROJECT}
  PUBLIC
  osIROSE-common-core
)

list(TRANSFORM SOURCES_HEADER PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

set(HEADERS "${SOURCES_HEADER};${PACKET_HEADERS}")

set_target_properties(${PROJECT} PROPERTIES
    PUBLIC_HEADER "${HEADERS}")

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${SOURCES_BASE})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX "Header Files" FILES ${SOURCES_HEADER})
source_group(TREE ${CMAKE_GEN_SRC_DIR}/packets PREFIX "Packet Source Files" FILES ${PACKET_SOURCES})
source_group(TREE ${CMAKE_GEN_SRC_DIR}/packets PREFIX "Packet Header Files" FILES ${PACKET_HEADERS})

SET_PROPERTY(TARGET ${PROJECT}                PROPERTY FOLDER "Common")