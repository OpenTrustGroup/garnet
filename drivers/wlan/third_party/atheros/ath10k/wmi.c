/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

// The way how macros in wmi.h and wmi-tlv.h are working (read carefully how MSG() is defined before
// ath10k_msg_type and how ATH10K_MSG_TYPE_WMI is *referred* in wmi.h) is that the .c code must
// include the msg_buf.h. Then the msg_buf.h will include wmi.h and wmi-tlv.h to expand the macros
// properly. TODO(NET-1237): Cleanup ath10k msg_buf
#include "msg_buf.h"

#include <ddk/driver.h>

#include "core.h"
#include "debug.h"
#include "hif.h"
#include "htc.h"
#include "hw.h"
#include "ieee80211.h"
#include "mac.h"
#include "p2p.h"
#include "testmode.h"
#include "wmi-ops.h"

#define ATH10K_WMI_BARRIER_ECHO_ID 0xBA991E9
#define ATH10K_WMI_BARRIER_TIMEOUT (ZX_SEC(3))

/* MAIN WMI cmd track */
static struct wmi_cmd_map wmi_cmd_map = {
    .init_cmdid = WMI_INIT_CMDID,
    .start_scan_cmdid = WMI_START_SCAN_CMDID,
    .stop_scan_cmdid = WMI_STOP_SCAN_CMDID,
    .scan_chan_list_cmdid = WMI_SCAN_CHAN_LIST_CMDID,
    .scan_sch_prio_tbl_cmdid = WMI_SCAN_SCH_PRIO_TBL_CMDID,
    .pdev_set_regdomain_cmdid = WMI_PDEV_SET_REGDOMAIN_CMDID,
    .pdev_set_channel_cmdid = WMI_PDEV_SET_CHANNEL_CMDID,
    .pdev_set_param_cmdid = WMI_PDEV_SET_PARAM_CMDID,
    .pdev_pktlog_enable_cmdid = WMI_PDEV_PKTLOG_ENABLE_CMDID,
    .pdev_pktlog_disable_cmdid = WMI_PDEV_PKTLOG_DISABLE_CMDID,
    .pdev_set_wmm_params_cmdid = WMI_PDEV_SET_WMM_PARAMS_CMDID,
    .pdev_set_ht_cap_ie_cmdid = WMI_PDEV_SET_HT_CAP_IE_CMDID,
    .pdev_set_vht_cap_ie_cmdid = WMI_PDEV_SET_VHT_CAP_IE_CMDID,
    .pdev_set_dscp_tid_map_cmdid = WMI_PDEV_SET_DSCP_TID_MAP_CMDID,
    .pdev_set_quiet_mode_cmdid = WMI_PDEV_SET_QUIET_MODE_CMDID,
    .pdev_green_ap_ps_enable_cmdid = WMI_PDEV_GREEN_AP_PS_ENABLE_CMDID,
    .pdev_get_tpc_config_cmdid = WMI_PDEV_GET_TPC_CONFIG_CMDID,
    .pdev_set_base_macaddr_cmdid = WMI_PDEV_SET_BASE_MACADDR_CMDID,
    .vdev_create_cmdid = WMI_VDEV_CREATE_CMDID,
    .vdev_delete_cmdid = WMI_VDEV_DELETE_CMDID,
    .vdev_start_request_cmdid = WMI_VDEV_START_REQUEST_CMDID,
    .vdev_restart_request_cmdid = WMI_VDEV_RESTART_REQUEST_CMDID,
    .vdev_up_cmdid = WMI_VDEV_UP_CMDID,
    .vdev_stop_cmdid = WMI_VDEV_STOP_CMDID,
    .vdev_down_cmdid = WMI_VDEV_DOWN_CMDID,
    .vdev_set_param_cmdid = WMI_VDEV_SET_PARAM_CMDID,
    .vdev_install_key_cmdid = WMI_VDEV_INSTALL_KEY_CMDID,
    .peer_create_cmdid = WMI_PEER_CREATE_CMDID,
    .peer_delete_cmdid = WMI_PEER_DELETE_CMDID,
    .peer_flush_tids_cmdid = WMI_PEER_FLUSH_TIDS_CMDID,
    .peer_set_param_cmdid = WMI_PEER_SET_PARAM_CMDID,
    .peer_assoc_cmdid = WMI_PEER_ASSOC_CMDID,
    .peer_add_wds_entry_cmdid = WMI_PEER_ADD_WDS_ENTRY_CMDID,
    .peer_remove_wds_entry_cmdid = WMI_PEER_REMOVE_WDS_ENTRY_CMDID,
    .peer_mcast_group_cmdid = WMI_PEER_MCAST_GROUP_CMDID,
    .bcn_tx_cmdid = WMI_BCN_TX_CMDID,
    .pdev_send_bcn_cmdid = WMI_PDEV_SEND_BCN_CMDID,
    .bcn_tmpl_cmdid = WMI_BCN_TMPL_CMDID,
    .bcn_filter_rx_cmdid = WMI_BCN_FILTER_RX_CMDID,
    .prb_req_filter_rx_cmdid = WMI_PRB_REQ_FILTER_RX_CMDID,
    .mgmt_tx_cmdid = WMI_MGMT_TX_CMDID,
    .prb_tmpl_cmdid = WMI_PRB_TMPL_CMDID,
    .addba_clear_resp_cmdid = WMI_ADDBA_CLEAR_RESP_CMDID,
    .addba_send_cmdid = WMI_ADDBA_SEND_CMDID,
    .addba_status_cmdid = WMI_ADDBA_STATUS_CMDID,
    .delba_send_cmdid = WMI_DELBA_SEND_CMDID,
    .addba_set_resp_cmdid = WMI_ADDBA_SET_RESP_CMDID,
    .send_singleamsdu_cmdid = WMI_SEND_SINGLEAMSDU_CMDID,
    .sta_powersave_mode_cmdid = WMI_STA_POWERSAVE_MODE_CMDID,
    .sta_powersave_param_cmdid = WMI_STA_POWERSAVE_PARAM_CMDID,
    .sta_mimo_ps_mode_cmdid = WMI_STA_MIMO_PS_MODE_CMDID,
    .pdev_dfs_enable_cmdid = WMI_PDEV_DFS_ENABLE_CMDID,
    .pdev_dfs_disable_cmdid = WMI_PDEV_DFS_DISABLE_CMDID,
    .roam_scan_mode = WMI_ROAM_SCAN_MODE,
    .roam_scan_rssi_threshold = WMI_ROAM_SCAN_RSSI_THRESHOLD,
    .roam_scan_period = WMI_ROAM_SCAN_PERIOD,
    .roam_scan_rssi_change_threshold = WMI_ROAM_SCAN_RSSI_CHANGE_THRESHOLD,
    .roam_ap_profile = WMI_ROAM_AP_PROFILE,
    .ofl_scan_add_ap_profile = WMI_ROAM_AP_PROFILE,
    .ofl_scan_remove_ap_profile = WMI_OFL_SCAN_REMOVE_AP_PROFILE,
    .ofl_scan_period = WMI_OFL_SCAN_PERIOD,
    .p2p_dev_set_device_info = WMI_P2P_DEV_SET_DEVICE_INFO,
    .p2p_dev_set_discoverability = WMI_P2P_DEV_SET_DISCOVERABILITY,
    .p2p_go_set_beacon_ie = WMI_P2P_GO_SET_BEACON_IE,
    .p2p_go_set_probe_resp_ie = WMI_P2P_GO_SET_PROBE_RESP_IE,
    .p2p_set_vendor_ie_data_cmdid = WMI_P2P_SET_VENDOR_IE_DATA_CMDID,
    .ap_ps_peer_param_cmdid = WMI_AP_PS_PEER_PARAM_CMDID,
    .ap_ps_peer_uapsd_coex_cmdid = WMI_AP_PS_PEER_UAPSD_COEX_CMDID,
    .peer_rate_retry_sched_cmdid = WMI_PEER_RATE_RETRY_SCHED_CMDID,
    .wlan_profile_trigger_cmdid = WMI_WLAN_PROFILE_TRIGGER_CMDID,
    .wlan_profile_set_hist_intvl_cmdid = WMI_WLAN_PROFILE_SET_HIST_INTVL_CMDID,
    .wlan_profile_get_profile_data_cmdid = WMI_WLAN_PROFILE_GET_PROFILE_DATA_CMDID,
    .wlan_profile_enable_profile_id_cmdid = WMI_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID,
    .wlan_profile_list_profile_id_cmdid = WMI_WLAN_PROFILE_LIST_PROFILE_ID_CMDID,
    .pdev_suspend_cmdid = WMI_PDEV_SUSPEND_CMDID,
    .pdev_resume_cmdid = WMI_PDEV_RESUME_CMDID,
    .add_bcn_filter_cmdid = WMI_ADD_BCN_FILTER_CMDID,
    .rmv_bcn_filter_cmdid = WMI_RMV_BCN_FILTER_CMDID,
    .wow_add_wake_pattern_cmdid = WMI_WOW_ADD_WAKE_PATTERN_CMDID,
    .wow_del_wake_pattern_cmdid = WMI_WOW_DEL_WAKE_PATTERN_CMDID,
    .wow_enable_disable_wake_event_cmdid = WMI_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID,
    .wow_enable_cmdid = WMI_WOW_ENABLE_CMDID,
    .wow_hostwakeup_from_sleep_cmdid = WMI_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID,
    .rtt_measreq_cmdid = WMI_RTT_MEASREQ_CMDID,
    .rtt_tsf_cmdid = WMI_RTT_TSF_CMDID,
    .vdev_spectral_scan_configure_cmdid = WMI_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID,
    .vdev_spectral_scan_enable_cmdid = WMI_VDEV_SPECTRAL_SCAN_ENABLE_CMDID,
    .request_stats_cmdid = WMI_REQUEST_STATS_CMDID,
    .set_arp_ns_offload_cmdid = WMI_SET_ARP_NS_OFFLOAD_CMDID,
    .network_list_offload_config_cmdid = WMI_NETWORK_LIST_OFFLOAD_CONFIG_CMDID,
    .gtk_offload_cmdid = WMI_GTK_OFFLOAD_CMDID,
    .csa_offload_enable_cmdid = WMI_CSA_OFFLOAD_ENABLE_CMDID,
    .csa_offload_chanswitch_cmdid = WMI_CSA_OFFLOAD_CHANSWITCH_CMDID,
    .chatter_set_mode_cmdid = WMI_CHATTER_SET_MODE_CMDID,
    .peer_tid_addba_cmdid = WMI_PEER_TID_ADDBA_CMDID,
    .peer_tid_delba_cmdid = WMI_PEER_TID_DELBA_CMDID,
    .sta_dtim_ps_method_cmdid = WMI_STA_DTIM_PS_METHOD_CMDID,
    .sta_uapsd_auto_trig_cmdid = WMI_STA_UAPSD_AUTO_TRIG_CMDID,
    .sta_keepalive_cmd = WMI_STA_KEEPALIVE_CMD,
    .echo_cmdid = WMI_ECHO_CMDID,
    .pdev_utf_cmdid = WMI_PDEV_UTF_CMDID,
    .dbglog_cfg_cmdid = WMI_DBGLOG_CFG_CMDID,
    .pdev_qvit_cmdid = WMI_PDEV_QVIT_CMDID,
    .pdev_ftm_intg_cmdid = WMI_PDEV_FTM_INTG_CMDID,
    .vdev_set_keepalive_cmdid = WMI_VDEV_SET_KEEPALIVE_CMDID,
    .vdev_get_keepalive_cmdid = WMI_VDEV_GET_KEEPALIVE_CMDID,
    .force_fw_hang_cmdid = WMI_FORCE_FW_HANG_CMDID,
    .gpio_config_cmdid = WMI_GPIO_CONFIG_CMDID,
    .gpio_output_cmdid = WMI_GPIO_OUTPUT_CMDID,
    .pdev_get_temperature_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_enable_adaptive_cca_cmdid = WMI_CMD_UNSUPPORTED,
    .scan_update_request_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_standby_response_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_resume_response_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_add_peer_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_evict_peer_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_restore_peer_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_print_all_peers_info_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_update_wds_entry_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_add_proxy_sta_entry_cmdid = WMI_CMD_UNSUPPORTED,
    .rtt_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
    .oem_req_cmdid = WMI_CMD_UNSUPPORTED,
    .nan_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_ratemask_cmdid = WMI_CMD_UNSUPPORTED,
    .qboost_cfg_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_smart_ant_enable_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_smart_ant_set_rx_antenna_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_smart_ant_set_tx_antenna_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_smart_ant_set_train_info_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_smart_ant_set_node_config_ops_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_antenna_switch_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_ctl_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_mimogain_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_ratepwr_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_ratepwr_chainmsk_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_fips_cmdid = WMI_CMD_UNSUPPORTED,
    .tt_set_conf_cmdid = WMI_CMD_UNSUPPORTED,
    .fwtest_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_atf_request_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_atf_request_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_ani_cck_config_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_ani_ofdm_config_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_reserve_ast_entry_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_nfcal_power_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_tpc_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_ast_info_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_set_dscp_tid_map_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_info_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_get_info_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_filter_neighbor_rx_packets_cmdid = WMI_CMD_UNSUPPORTED,
    .mu_cal_start_cmdid = WMI_CMD_UNSUPPORTED,
    .set_cca_params_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_bss_chan_info_request_cmdid = WMI_CMD_UNSUPPORTED,
};

/* 10.X WMI cmd track */
static struct wmi_cmd_map wmi_10x_cmd_map = {
    .init_cmdid = WMI_10X_INIT_CMDID,
    .start_scan_cmdid = WMI_10X_START_SCAN_CMDID,
    .stop_scan_cmdid = WMI_10X_STOP_SCAN_CMDID,
    .scan_chan_list_cmdid = WMI_10X_SCAN_CHAN_LIST_CMDID,
    .scan_sch_prio_tbl_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_regdomain_cmdid = WMI_10X_PDEV_SET_REGDOMAIN_CMDID,
    .pdev_set_channel_cmdid = WMI_10X_PDEV_SET_CHANNEL_CMDID,
    .pdev_set_param_cmdid = WMI_10X_PDEV_SET_PARAM_CMDID,
    .pdev_pktlog_enable_cmdid = WMI_10X_PDEV_PKTLOG_ENABLE_CMDID,
    .pdev_pktlog_disable_cmdid = WMI_10X_PDEV_PKTLOG_DISABLE_CMDID,
    .pdev_set_wmm_params_cmdid = WMI_10X_PDEV_SET_WMM_PARAMS_CMDID,
    .pdev_set_ht_cap_ie_cmdid = WMI_10X_PDEV_SET_HT_CAP_IE_CMDID,
    .pdev_set_vht_cap_ie_cmdid = WMI_10X_PDEV_SET_VHT_CAP_IE_CMDID,
    .pdev_set_dscp_tid_map_cmdid = WMI_10X_PDEV_SET_DSCP_TID_MAP_CMDID,
    .pdev_set_quiet_mode_cmdid = WMI_10X_PDEV_SET_QUIET_MODE_CMDID,
    .pdev_green_ap_ps_enable_cmdid = WMI_10X_PDEV_GREEN_AP_PS_ENABLE_CMDID,
    .pdev_get_tpc_config_cmdid = WMI_10X_PDEV_GET_TPC_CONFIG_CMDID,
    .pdev_set_base_macaddr_cmdid = WMI_10X_PDEV_SET_BASE_MACADDR_CMDID,
    .vdev_create_cmdid = WMI_10X_VDEV_CREATE_CMDID,
    .vdev_delete_cmdid = WMI_10X_VDEV_DELETE_CMDID,
    .vdev_start_request_cmdid = WMI_10X_VDEV_START_REQUEST_CMDID,
    .vdev_restart_request_cmdid = WMI_10X_VDEV_RESTART_REQUEST_CMDID,
    .vdev_up_cmdid = WMI_10X_VDEV_UP_CMDID,
    .vdev_stop_cmdid = WMI_10X_VDEV_STOP_CMDID,
    .vdev_down_cmdid = WMI_10X_VDEV_DOWN_CMDID,
    .vdev_set_param_cmdid = WMI_10X_VDEV_SET_PARAM_CMDID,
    .vdev_install_key_cmdid = WMI_10X_VDEV_INSTALL_KEY_CMDID,
    .peer_create_cmdid = WMI_10X_PEER_CREATE_CMDID,
    .peer_delete_cmdid = WMI_10X_PEER_DELETE_CMDID,
    .peer_flush_tids_cmdid = WMI_10X_PEER_FLUSH_TIDS_CMDID,
    .peer_set_param_cmdid = WMI_10X_PEER_SET_PARAM_CMDID,
    .peer_assoc_cmdid = WMI_10X_PEER_ASSOC_CMDID,
    .peer_add_wds_entry_cmdid = WMI_10X_PEER_ADD_WDS_ENTRY_CMDID,
    .peer_remove_wds_entry_cmdid = WMI_10X_PEER_REMOVE_WDS_ENTRY_CMDID,
    .peer_mcast_group_cmdid = WMI_10X_PEER_MCAST_GROUP_CMDID,
    .bcn_tx_cmdid = WMI_10X_BCN_TX_CMDID,
    .pdev_send_bcn_cmdid = WMI_10X_PDEV_SEND_BCN_CMDID,
    .bcn_tmpl_cmdid = WMI_CMD_UNSUPPORTED,
    .bcn_filter_rx_cmdid = WMI_10X_BCN_FILTER_RX_CMDID,
    .prb_req_filter_rx_cmdid = WMI_10X_PRB_REQ_FILTER_RX_CMDID,
    .mgmt_tx_cmdid = WMI_10X_MGMT_TX_CMDID,
    .prb_tmpl_cmdid = WMI_CMD_UNSUPPORTED,
    .addba_clear_resp_cmdid = WMI_10X_ADDBA_CLEAR_RESP_CMDID,
    .addba_send_cmdid = WMI_10X_ADDBA_SEND_CMDID,
    .addba_status_cmdid = WMI_10X_ADDBA_STATUS_CMDID,
    .delba_send_cmdid = WMI_10X_DELBA_SEND_CMDID,
    .addba_set_resp_cmdid = WMI_10X_ADDBA_SET_RESP_CMDID,
    .send_singleamsdu_cmdid = WMI_10X_SEND_SINGLEAMSDU_CMDID,
    .sta_powersave_mode_cmdid = WMI_10X_STA_POWERSAVE_MODE_CMDID,
    .sta_powersave_param_cmdid = WMI_10X_STA_POWERSAVE_PARAM_CMDID,
    .sta_mimo_ps_mode_cmdid = WMI_10X_STA_MIMO_PS_MODE_CMDID,
    .pdev_dfs_enable_cmdid = WMI_10X_PDEV_DFS_ENABLE_CMDID,
    .pdev_dfs_disable_cmdid = WMI_10X_PDEV_DFS_DISABLE_CMDID,
    .roam_scan_mode = WMI_10X_ROAM_SCAN_MODE,
    .roam_scan_rssi_threshold = WMI_10X_ROAM_SCAN_RSSI_THRESHOLD,
    .roam_scan_period = WMI_10X_ROAM_SCAN_PERIOD,
    .roam_scan_rssi_change_threshold = WMI_10X_ROAM_SCAN_RSSI_CHANGE_THRESHOLD,
    .roam_ap_profile = WMI_10X_ROAM_AP_PROFILE,
    .ofl_scan_add_ap_profile = WMI_10X_OFL_SCAN_ADD_AP_PROFILE,
    .ofl_scan_remove_ap_profile = WMI_10X_OFL_SCAN_REMOVE_AP_PROFILE,
    .ofl_scan_period = WMI_10X_OFL_SCAN_PERIOD,
    .p2p_dev_set_device_info = WMI_10X_P2P_DEV_SET_DEVICE_INFO,
    .p2p_dev_set_discoverability = WMI_10X_P2P_DEV_SET_DISCOVERABILITY,
    .p2p_go_set_beacon_ie = WMI_10X_P2P_GO_SET_BEACON_IE,
    .p2p_go_set_probe_resp_ie = WMI_10X_P2P_GO_SET_PROBE_RESP_IE,
    .p2p_set_vendor_ie_data_cmdid = WMI_CMD_UNSUPPORTED,
    .ap_ps_peer_param_cmdid = WMI_10X_AP_PS_PEER_PARAM_CMDID,
    .ap_ps_peer_uapsd_coex_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_rate_retry_sched_cmdid = WMI_10X_PEER_RATE_RETRY_SCHED_CMDID,
    .wlan_profile_trigger_cmdid = WMI_10X_WLAN_PROFILE_TRIGGER_CMDID,
    .wlan_profile_set_hist_intvl_cmdid = WMI_10X_WLAN_PROFILE_SET_HIST_INTVL_CMDID,
    .wlan_profile_get_profile_data_cmdid = WMI_10X_WLAN_PROFILE_GET_PROFILE_DATA_CMDID,
    .wlan_profile_enable_profile_id_cmdid = WMI_10X_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID,
    .wlan_profile_list_profile_id_cmdid = WMI_10X_WLAN_PROFILE_LIST_PROFILE_ID_CMDID,
    .pdev_suspend_cmdid = WMI_10X_PDEV_SUSPEND_CMDID,
    .pdev_resume_cmdid = WMI_10X_PDEV_RESUME_CMDID,
    .add_bcn_filter_cmdid = WMI_10X_ADD_BCN_FILTER_CMDID,
    .rmv_bcn_filter_cmdid = WMI_10X_RMV_BCN_FILTER_CMDID,
    .wow_add_wake_pattern_cmdid = WMI_10X_WOW_ADD_WAKE_PATTERN_CMDID,
    .wow_del_wake_pattern_cmdid = WMI_10X_WOW_DEL_WAKE_PATTERN_CMDID,
    .wow_enable_disable_wake_event_cmdid = WMI_10X_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID,
    .wow_enable_cmdid = WMI_10X_WOW_ENABLE_CMDID,
    .wow_hostwakeup_from_sleep_cmdid = WMI_10X_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID,
    .rtt_measreq_cmdid = WMI_10X_RTT_MEASREQ_CMDID,
    .rtt_tsf_cmdid = WMI_10X_RTT_TSF_CMDID,
    .vdev_spectral_scan_configure_cmdid = WMI_10X_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID,
    .vdev_spectral_scan_enable_cmdid = WMI_10X_VDEV_SPECTRAL_SCAN_ENABLE_CMDID,
    .request_stats_cmdid = WMI_10X_REQUEST_STATS_CMDID,
    .set_arp_ns_offload_cmdid = WMI_CMD_UNSUPPORTED,
    .network_list_offload_config_cmdid = WMI_CMD_UNSUPPORTED,
    .gtk_offload_cmdid = WMI_CMD_UNSUPPORTED,
    .csa_offload_enable_cmdid = WMI_CMD_UNSUPPORTED,
    .csa_offload_chanswitch_cmdid = WMI_CMD_UNSUPPORTED,
    .chatter_set_mode_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_tid_addba_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_tid_delba_cmdid = WMI_CMD_UNSUPPORTED,
    .sta_dtim_ps_method_cmdid = WMI_CMD_UNSUPPORTED,
    .sta_uapsd_auto_trig_cmdid = WMI_CMD_UNSUPPORTED,
    .sta_keepalive_cmd = WMI_CMD_UNSUPPORTED,
    .echo_cmdid = WMI_10X_ECHO_CMDID,
    .pdev_utf_cmdid = WMI_10X_PDEV_UTF_CMDID,
    .dbglog_cfg_cmdid = WMI_10X_DBGLOG_CFG_CMDID,
    .pdev_qvit_cmdid = WMI_10X_PDEV_QVIT_CMDID,
    .pdev_ftm_intg_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_set_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_get_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
    .force_fw_hang_cmdid = WMI_CMD_UNSUPPORTED,
    .gpio_config_cmdid = WMI_10X_GPIO_CONFIG_CMDID,
    .gpio_output_cmdid = WMI_10X_GPIO_OUTPUT_CMDID,
    .pdev_get_temperature_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_enable_adaptive_cca_cmdid = WMI_CMD_UNSUPPORTED,
    .scan_update_request_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_standby_response_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_resume_response_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_add_peer_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_evict_peer_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_restore_peer_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_print_all_peers_info_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_update_wds_entry_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_add_proxy_sta_entry_cmdid = WMI_CMD_UNSUPPORTED,
    .rtt_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
    .oem_req_cmdid = WMI_CMD_UNSUPPORTED,
    .nan_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_ratemask_cmdid = WMI_CMD_UNSUPPORTED,
    .qboost_cfg_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_smart_ant_enable_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_smart_ant_set_rx_antenna_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_smart_ant_set_tx_antenna_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_smart_ant_set_train_info_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_smart_ant_set_node_config_ops_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_antenna_switch_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_ctl_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_mimogain_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_ratepwr_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_ratepwr_chainmsk_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_fips_cmdid = WMI_CMD_UNSUPPORTED,
    .tt_set_conf_cmdid = WMI_CMD_UNSUPPORTED,
    .fwtest_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_atf_request_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_atf_request_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_ani_cck_config_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_ani_ofdm_config_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_reserve_ast_entry_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_nfcal_power_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_tpc_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_ast_info_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_set_dscp_tid_map_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_info_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_get_info_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_filter_neighbor_rx_packets_cmdid = WMI_CMD_UNSUPPORTED,
    .mu_cal_start_cmdid = WMI_CMD_UNSUPPORTED,
    .set_cca_params_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_bss_chan_info_request_cmdid = WMI_CMD_UNSUPPORTED,
};

/* 10.2.4 WMI cmd track */
static struct wmi_cmd_map wmi_10_2_4_cmd_map = {
    .init_cmdid = WMI_10_2_INIT_CMDID,
    .start_scan_cmdid = WMI_10_2_START_SCAN_CMDID,
    .stop_scan_cmdid = WMI_10_2_STOP_SCAN_CMDID,
    .scan_chan_list_cmdid = WMI_10_2_SCAN_CHAN_LIST_CMDID,
    .scan_sch_prio_tbl_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_regdomain_cmdid = WMI_10_2_PDEV_SET_REGDOMAIN_CMDID,
    .pdev_set_channel_cmdid = WMI_10_2_PDEV_SET_CHANNEL_CMDID,
    .pdev_set_param_cmdid = WMI_10_2_PDEV_SET_PARAM_CMDID,
    .pdev_pktlog_enable_cmdid = WMI_10_2_PDEV_PKTLOG_ENABLE_CMDID,
    .pdev_pktlog_disable_cmdid = WMI_10_2_PDEV_PKTLOG_DISABLE_CMDID,
    .pdev_set_wmm_params_cmdid = WMI_10_2_PDEV_SET_WMM_PARAMS_CMDID,
    .pdev_set_ht_cap_ie_cmdid = WMI_10_2_PDEV_SET_HT_CAP_IE_CMDID,
    .pdev_set_vht_cap_ie_cmdid = WMI_10_2_PDEV_SET_VHT_CAP_IE_CMDID,
    .pdev_set_quiet_mode_cmdid = WMI_10_2_PDEV_SET_QUIET_MODE_CMDID,
    .pdev_green_ap_ps_enable_cmdid = WMI_10_2_PDEV_GREEN_AP_PS_ENABLE_CMDID,
    .pdev_get_tpc_config_cmdid = WMI_10_2_PDEV_GET_TPC_CONFIG_CMDID,
    .pdev_set_base_macaddr_cmdid = WMI_10_2_PDEV_SET_BASE_MACADDR_CMDID,
    .vdev_create_cmdid = WMI_10_2_VDEV_CREATE_CMDID,
    .vdev_delete_cmdid = WMI_10_2_VDEV_DELETE_CMDID,
    .vdev_start_request_cmdid = WMI_10_2_VDEV_START_REQUEST_CMDID,
    .vdev_restart_request_cmdid = WMI_10_2_VDEV_RESTART_REQUEST_CMDID,
    .vdev_up_cmdid = WMI_10_2_VDEV_UP_CMDID,
    .vdev_stop_cmdid = WMI_10_2_VDEV_STOP_CMDID,
    .vdev_down_cmdid = WMI_10_2_VDEV_DOWN_CMDID,
    .vdev_set_param_cmdid = WMI_10_2_VDEV_SET_PARAM_CMDID,
    .vdev_install_key_cmdid = WMI_10_2_VDEV_INSTALL_KEY_CMDID,
    .peer_create_cmdid = WMI_10_2_PEER_CREATE_CMDID,
    .peer_delete_cmdid = WMI_10_2_PEER_DELETE_CMDID,
    .peer_flush_tids_cmdid = WMI_10_2_PEER_FLUSH_TIDS_CMDID,
    .peer_set_param_cmdid = WMI_10_2_PEER_SET_PARAM_CMDID,
    .peer_assoc_cmdid = WMI_10_2_PEER_ASSOC_CMDID,
    .peer_add_wds_entry_cmdid = WMI_10_2_PEER_ADD_WDS_ENTRY_CMDID,
    .peer_remove_wds_entry_cmdid = WMI_10_2_PEER_REMOVE_WDS_ENTRY_CMDID,
    .peer_mcast_group_cmdid = WMI_10_2_PEER_MCAST_GROUP_CMDID,
    .bcn_tx_cmdid = WMI_10_2_BCN_TX_CMDID,
    .pdev_send_bcn_cmdid = WMI_10_2_PDEV_SEND_BCN_CMDID,
    .bcn_tmpl_cmdid = WMI_CMD_UNSUPPORTED,
    .bcn_filter_rx_cmdid = WMI_10_2_BCN_FILTER_RX_CMDID,
    .prb_req_filter_rx_cmdid = WMI_10_2_PRB_REQ_FILTER_RX_CMDID,
    .mgmt_tx_cmdid = WMI_10_2_MGMT_TX_CMDID,
    .prb_tmpl_cmdid = WMI_CMD_UNSUPPORTED,
    .addba_clear_resp_cmdid = WMI_10_2_ADDBA_CLEAR_RESP_CMDID,
    .addba_send_cmdid = WMI_10_2_ADDBA_SEND_CMDID,
    .addba_status_cmdid = WMI_10_2_ADDBA_STATUS_CMDID,
    .delba_send_cmdid = WMI_10_2_DELBA_SEND_CMDID,
    .addba_set_resp_cmdid = WMI_10_2_ADDBA_SET_RESP_CMDID,
    .send_singleamsdu_cmdid = WMI_10_2_SEND_SINGLEAMSDU_CMDID,
    .sta_powersave_mode_cmdid = WMI_10_2_STA_POWERSAVE_MODE_CMDID,
    .sta_powersave_param_cmdid = WMI_10_2_STA_POWERSAVE_PARAM_CMDID,
    .sta_mimo_ps_mode_cmdid = WMI_10_2_STA_MIMO_PS_MODE_CMDID,
    .pdev_dfs_enable_cmdid = WMI_10_2_PDEV_DFS_ENABLE_CMDID,
    .pdev_dfs_disable_cmdid = WMI_10_2_PDEV_DFS_DISABLE_CMDID,
    .roam_scan_mode = WMI_10_2_ROAM_SCAN_MODE,
    .roam_scan_rssi_threshold = WMI_10_2_ROAM_SCAN_RSSI_THRESHOLD,
    .roam_scan_period = WMI_10_2_ROAM_SCAN_PERIOD,
    .roam_scan_rssi_change_threshold = WMI_10_2_ROAM_SCAN_RSSI_CHANGE_THRESHOLD,
    .roam_ap_profile = WMI_10_2_ROAM_AP_PROFILE,
    .ofl_scan_add_ap_profile = WMI_10_2_OFL_SCAN_ADD_AP_PROFILE,
    .ofl_scan_remove_ap_profile = WMI_10_2_OFL_SCAN_REMOVE_AP_PROFILE,
    .ofl_scan_period = WMI_10_2_OFL_SCAN_PERIOD,
    .p2p_dev_set_device_info = WMI_10_2_P2P_DEV_SET_DEVICE_INFO,
    .p2p_dev_set_discoverability = WMI_10_2_P2P_DEV_SET_DISCOVERABILITY,
    .p2p_go_set_beacon_ie = WMI_10_2_P2P_GO_SET_BEACON_IE,
    .p2p_go_set_probe_resp_ie = WMI_10_2_P2P_GO_SET_PROBE_RESP_IE,
    .p2p_set_vendor_ie_data_cmdid = WMI_CMD_UNSUPPORTED,
    .ap_ps_peer_param_cmdid = WMI_10_2_AP_PS_PEER_PARAM_CMDID,
    .ap_ps_peer_uapsd_coex_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_rate_retry_sched_cmdid = WMI_10_2_PEER_RATE_RETRY_SCHED_CMDID,
    .wlan_profile_trigger_cmdid = WMI_10_2_WLAN_PROFILE_TRIGGER_CMDID,
    .wlan_profile_set_hist_intvl_cmdid = WMI_10_2_WLAN_PROFILE_SET_HIST_INTVL_CMDID,
    .wlan_profile_get_profile_data_cmdid = WMI_10_2_WLAN_PROFILE_GET_PROFILE_DATA_CMDID,
    .wlan_profile_enable_profile_id_cmdid = WMI_10_2_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID,
    .wlan_profile_list_profile_id_cmdid = WMI_10_2_WLAN_PROFILE_LIST_PROFILE_ID_CMDID,
    .pdev_suspend_cmdid = WMI_10_2_PDEV_SUSPEND_CMDID,
    .pdev_resume_cmdid = WMI_10_2_PDEV_RESUME_CMDID,
    .add_bcn_filter_cmdid = WMI_10_2_ADD_BCN_FILTER_CMDID,
    .rmv_bcn_filter_cmdid = WMI_10_2_RMV_BCN_FILTER_CMDID,
    .wow_add_wake_pattern_cmdid = WMI_10_2_WOW_ADD_WAKE_PATTERN_CMDID,
    .wow_del_wake_pattern_cmdid = WMI_10_2_WOW_DEL_WAKE_PATTERN_CMDID,
    .wow_enable_disable_wake_event_cmdid = WMI_10_2_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID,
    .wow_enable_cmdid = WMI_10_2_WOW_ENABLE_CMDID,
    .wow_hostwakeup_from_sleep_cmdid = WMI_10_2_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID,
    .rtt_measreq_cmdid = WMI_10_2_RTT_MEASREQ_CMDID,
    .rtt_tsf_cmdid = WMI_10_2_RTT_TSF_CMDID,
    .vdev_spectral_scan_configure_cmdid = WMI_10_2_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID,
    .vdev_spectral_scan_enable_cmdid = WMI_10_2_VDEV_SPECTRAL_SCAN_ENABLE_CMDID,
    .request_stats_cmdid = WMI_10_2_REQUEST_STATS_CMDID,
    .set_arp_ns_offload_cmdid = WMI_CMD_UNSUPPORTED,
    .network_list_offload_config_cmdid = WMI_CMD_UNSUPPORTED,
    .gtk_offload_cmdid = WMI_CMD_UNSUPPORTED,
    .csa_offload_enable_cmdid = WMI_CMD_UNSUPPORTED,
    .csa_offload_chanswitch_cmdid = WMI_CMD_UNSUPPORTED,
    .chatter_set_mode_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_tid_addba_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_tid_delba_cmdid = WMI_CMD_UNSUPPORTED,
    .sta_dtim_ps_method_cmdid = WMI_CMD_UNSUPPORTED,
    .sta_uapsd_auto_trig_cmdid = WMI_CMD_UNSUPPORTED,
    .sta_keepalive_cmd = WMI_CMD_UNSUPPORTED,
    .echo_cmdid = WMI_10_2_ECHO_CMDID,
    .pdev_utf_cmdid = WMI_10_2_PDEV_UTF_CMDID,
    .dbglog_cfg_cmdid = WMI_10_2_DBGLOG_CFG_CMDID,
    .pdev_qvit_cmdid = WMI_10_2_PDEV_QVIT_CMDID,
    .pdev_ftm_intg_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_set_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_get_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
    .force_fw_hang_cmdid = WMI_CMD_UNSUPPORTED,
    .gpio_config_cmdid = WMI_10_2_GPIO_CONFIG_CMDID,
    .gpio_output_cmdid = WMI_10_2_GPIO_OUTPUT_CMDID,
    .pdev_get_temperature_cmdid = WMI_10_2_PDEV_GET_TEMPERATURE_CMDID,
    .pdev_enable_adaptive_cca_cmdid = WMI_10_2_SET_CCA_PARAMS,
    .scan_update_request_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_standby_response_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_resume_response_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_add_peer_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_evict_peer_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_restore_peer_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_print_all_peers_info_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_update_wds_entry_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_add_proxy_sta_entry_cmdid = WMI_CMD_UNSUPPORTED,
    .rtt_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
    .oem_req_cmdid = WMI_CMD_UNSUPPORTED,
    .nan_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_ratemask_cmdid = WMI_CMD_UNSUPPORTED,
    .qboost_cfg_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_smart_ant_enable_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_smart_ant_set_rx_antenna_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_smart_ant_set_tx_antenna_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_smart_ant_set_train_info_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_smart_ant_set_node_config_ops_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_antenna_switch_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_ctl_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_mimogain_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_ratepwr_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_ratepwr_chainmsk_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_fips_cmdid = WMI_CMD_UNSUPPORTED,
    .tt_set_conf_cmdid = WMI_CMD_UNSUPPORTED,
    .fwtest_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_atf_request_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_atf_request_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_ani_cck_config_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_ani_ofdm_config_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_reserve_ast_entry_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_nfcal_power_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_tpc_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_ast_info_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_set_dscp_tid_map_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_info_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_get_info_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_filter_neighbor_rx_packets_cmdid = WMI_CMD_UNSUPPORTED,
    .mu_cal_start_cmdid = WMI_CMD_UNSUPPORTED,
    .set_cca_params_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_bss_chan_info_request_cmdid = WMI_10_2_PDEV_BSS_CHAN_INFO_REQUEST_CMDID,
};

/* 10.4 WMI cmd track */
static struct wmi_cmd_map wmi_10_4_cmd_map = {
    .init_cmdid = WMI_10_4_INIT_CMDID,
    .start_scan_cmdid = WMI_10_4_START_SCAN_CMDID,
    .stop_scan_cmdid = WMI_10_4_STOP_SCAN_CMDID,
    .scan_chan_list_cmdid = WMI_10_4_SCAN_CHAN_LIST_CMDID,
    .scan_sch_prio_tbl_cmdid = WMI_10_4_SCAN_SCH_PRIO_TBL_CMDID,
    .pdev_set_regdomain_cmdid = WMI_10_4_PDEV_SET_REGDOMAIN_CMDID,
    .pdev_set_channel_cmdid = WMI_10_4_PDEV_SET_CHANNEL_CMDID,
    .pdev_set_param_cmdid = WMI_10_4_PDEV_SET_PARAM_CMDID,
    .pdev_pktlog_enable_cmdid = WMI_10_4_PDEV_PKTLOG_ENABLE_CMDID,
    .pdev_pktlog_disable_cmdid = WMI_10_4_PDEV_PKTLOG_DISABLE_CMDID,
    .pdev_set_wmm_params_cmdid = WMI_10_4_PDEV_SET_WMM_PARAMS_CMDID,
    .pdev_set_ht_cap_ie_cmdid = WMI_10_4_PDEV_SET_HT_CAP_IE_CMDID,
    .pdev_set_vht_cap_ie_cmdid = WMI_10_4_PDEV_SET_VHT_CAP_IE_CMDID,
    .pdev_set_dscp_tid_map_cmdid = WMI_10_4_PDEV_SET_DSCP_TID_MAP_CMDID,
    .pdev_set_quiet_mode_cmdid = WMI_10_4_PDEV_SET_QUIET_MODE_CMDID,
    .pdev_green_ap_ps_enable_cmdid = WMI_10_4_PDEV_GREEN_AP_PS_ENABLE_CMDID,
    .pdev_get_tpc_config_cmdid = WMI_10_4_PDEV_GET_TPC_CONFIG_CMDID,
    .pdev_set_base_macaddr_cmdid = WMI_10_4_PDEV_SET_BASE_MACADDR_CMDID,
    .vdev_create_cmdid = WMI_10_4_VDEV_CREATE_CMDID,
    .vdev_delete_cmdid = WMI_10_4_VDEV_DELETE_CMDID,
    .vdev_start_request_cmdid = WMI_10_4_VDEV_START_REQUEST_CMDID,
    .vdev_restart_request_cmdid = WMI_10_4_VDEV_RESTART_REQUEST_CMDID,
    .vdev_up_cmdid = WMI_10_4_VDEV_UP_CMDID,
    .vdev_stop_cmdid = WMI_10_4_VDEV_STOP_CMDID,
    .vdev_down_cmdid = WMI_10_4_VDEV_DOWN_CMDID,
    .vdev_set_param_cmdid = WMI_10_4_VDEV_SET_PARAM_CMDID,
    .vdev_install_key_cmdid = WMI_10_4_VDEV_INSTALL_KEY_CMDID,
    .peer_create_cmdid = WMI_10_4_PEER_CREATE_CMDID,
    .peer_delete_cmdid = WMI_10_4_PEER_DELETE_CMDID,
    .peer_flush_tids_cmdid = WMI_10_4_PEER_FLUSH_TIDS_CMDID,
    .peer_set_param_cmdid = WMI_10_4_PEER_SET_PARAM_CMDID,
    .peer_assoc_cmdid = WMI_10_4_PEER_ASSOC_CMDID,
    .peer_add_wds_entry_cmdid = WMI_10_4_PEER_ADD_WDS_ENTRY_CMDID,
    .peer_remove_wds_entry_cmdid = WMI_10_4_PEER_REMOVE_WDS_ENTRY_CMDID,
    .peer_mcast_group_cmdid = WMI_10_4_PEER_MCAST_GROUP_CMDID,
    .bcn_tx_cmdid = WMI_10_4_BCN_TX_CMDID,
    .pdev_send_bcn_cmdid = WMI_10_4_PDEV_SEND_BCN_CMDID,
    .bcn_tmpl_cmdid = WMI_10_4_BCN_PRB_TMPL_CMDID,
    .bcn_filter_rx_cmdid = WMI_10_4_BCN_FILTER_RX_CMDID,
    .prb_req_filter_rx_cmdid = WMI_10_4_PRB_REQ_FILTER_RX_CMDID,
    .mgmt_tx_cmdid = WMI_10_4_MGMT_TX_CMDID,
    .prb_tmpl_cmdid = WMI_10_4_PRB_TMPL_CMDID,
    .addba_clear_resp_cmdid = WMI_10_4_ADDBA_CLEAR_RESP_CMDID,
    .addba_send_cmdid = WMI_10_4_ADDBA_SEND_CMDID,
    .addba_status_cmdid = WMI_10_4_ADDBA_STATUS_CMDID,
    .delba_send_cmdid = WMI_10_4_DELBA_SEND_CMDID,
    .addba_set_resp_cmdid = WMI_10_4_ADDBA_SET_RESP_CMDID,
    .send_singleamsdu_cmdid = WMI_10_4_SEND_SINGLEAMSDU_CMDID,
    .sta_powersave_mode_cmdid = WMI_10_4_STA_POWERSAVE_MODE_CMDID,
    .sta_powersave_param_cmdid = WMI_10_4_STA_POWERSAVE_PARAM_CMDID,
    .sta_mimo_ps_mode_cmdid = WMI_10_4_STA_MIMO_PS_MODE_CMDID,
    .pdev_dfs_enable_cmdid = WMI_10_4_PDEV_DFS_ENABLE_CMDID,
    .pdev_dfs_disable_cmdid = WMI_10_4_PDEV_DFS_DISABLE_CMDID,
    .roam_scan_mode = WMI_10_4_ROAM_SCAN_MODE,
    .roam_scan_rssi_threshold = WMI_10_4_ROAM_SCAN_RSSI_THRESHOLD,
    .roam_scan_period = WMI_10_4_ROAM_SCAN_PERIOD,
    .roam_scan_rssi_change_threshold = WMI_10_4_ROAM_SCAN_RSSI_CHANGE_THRESHOLD,
    .roam_ap_profile = WMI_10_4_ROAM_AP_PROFILE,
    .ofl_scan_add_ap_profile = WMI_10_4_OFL_SCAN_ADD_AP_PROFILE,
    .ofl_scan_remove_ap_profile = WMI_10_4_OFL_SCAN_REMOVE_AP_PROFILE,
    .ofl_scan_period = WMI_10_4_OFL_SCAN_PERIOD,
    .p2p_dev_set_device_info = WMI_10_4_P2P_DEV_SET_DEVICE_INFO,
    .p2p_dev_set_discoverability = WMI_10_4_P2P_DEV_SET_DISCOVERABILITY,
    .p2p_go_set_beacon_ie = WMI_10_4_P2P_GO_SET_BEACON_IE,
    .p2p_go_set_probe_resp_ie = WMI_10_4_P2P_GO_SET_PROBE_RESP_IE,
    .p2p_set_vendor_ie_data_cmdid = WMI_10_4_P2P_SET_VENDOR_IE_DATA_CMDID,
    .ap_ps_peer_param_cmdid = WMI_10_4_AP_PS_PEER_PARAM_CMDID,
    .ap_ps_peer_uapsd_coex_cmdid = WMI_10_4_AP_PS_PEER_UAPSD_COEX_CMDID,
    .peer_rate_retry_sched_cmdid = WMI_10_4_PEER_RATE_RETRY_SCHED_CMDID,
    .wlan_profile_trigger_cmdid = WMI_10_4_WLAN_PROFILE_TRIGGER_CMDID,
    .wlan_profile_set_hist_intvl_cmdid = WMI_10_4_WLAN_PROFILE_SET_HIST_INTVL_CMDID,
    .wlan_profile_get_profile_data_cmdid = WMI_10_4_WLAN_PROFILE_GET_PROFILE_DATA_CMDID,
    .wlan_profile_enable_profile_id_cmdid = WMI_10_4_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID,
    .wlan_profile_list_profile_id_cmdid = WMI_10_4_WLAN_PROFILE_LIST_PROFILE_ID_CMDID,
    .pdev_suspend_cmdid = WMI_10_4_PDEV_SUSPEND_CMDID,
    .pdev_resume_cmdid = WMI_10_4_PDEV_RESUME_CMDID,
    .add_bcn_filter_cmdid = WMI_10_4_ADD_BCN_FILTER_CMDID,
    .rmv_bcn_filter_cmdid = WMI_10_4_RMV_BCN_FILTER_CMDID,
    .wow_add_wake_pattern_cmdid = WMI_10_4_WOW_ADD_WAKE_PATTERN_CMDID,
    .wow_del_wake_pattern_cmdid = WMI_10_4_WOW_DEL_WAKE_PATTERN_CMDID,
    .wow_enable_disable_wake_event_cmdid = WMI_10_4_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID,
    .wow_enable_cmdid = WMI_10_4_WOW_ENABLE_CMDID,
    .wow_hostwakeup_from_sleep_cmdid = WMI_10_4_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID,
    .rtt_measreq_cmdid = WMI_10_4_RTT_MEASREQ_CMDID,
    .rtt_tsf_cmdid = WMI_10_4_RTT_TSF_CMDID,
    .vdev_spectral_scan_configure_cmdid = WMI_10_4_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID,
    .vdev_spectral_scan_enable_cmdid = WMI_10_4_VDEV_SPECTRAL_SCAN_ENABLE_CMDID,
    .request_stats_cmdid = WMI_10_4_REQUEST_STATS_CMDID,
    .set_arp_ns_offload_cmdid = WMI_CMD_UNSUPPORTED,
    .network_list_offload_config_cmdid = WMI_CMD_UNSUPPORTED,
    .gtk_offload_cmdid = WMI_10_4_GTK_OFFLOAD_CMDID,
    .csa_offload_enable_cmdid = WMI_10_4_CSA_OFFLOAD_ENABLE_CMDID,
    .csa_offload_chanswitch_cmdid = WMI_10_4_CSA_OFFLOAD_CHANSWITCH_CMDID,
    .chatter_set_mode_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_tid_addba_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_tid_delba_cmdid = WMI_CMD_UNSUPPORTED,
    .sta_dtim_ps_method_cmdid = WMI_CMD_UNSUPPORTED,
    .sta_uapsd_auto_trig_cmdid = WMI_CMD_UNSUPPORTED,
    .sta_keepalive_cmd = WMI_CMD_UNSUPPORTED,
    .echo_cmdid = WMI_10_4_ECHO_CMDID,
    .pdev_utf_cmdid = WMI_10_4_PDEV_UTF_CMDID,
    .dbglog_cfg_cmdid = WMI_10_4_DBGLOG_CFG_CMDID,
    .pdev_qvit_cmdid = WMI_10_4_PDEV_QVIT_CMDID,
    .pdev_ftm_intg_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_set_keepalive_cmdid = WMI_10_4_VDEV_SET_KEEPALIVE_CMDID,
    .vdev_get_keepalive_cmdid = WMI_10_4_VDEV_GET_KEEPALIVE_CMDID,
    .force_fw_hang_cmdid = WMI_10_4_FORCE_FW_HANG_CMDID,
    .gpio_config_cmdid = WMI_10_4_GPIO_CONFIG_CMDID,
    .gpio_output_cmdid = WMI_10_4_GPIO_OUTPUT_CMDID,
    .pdev_get_temperature_cmdid = WMI_10_4_PDEV_GET_TEMPERATURE_CMDID,
    .vdev_set_wmm_params_cmdid = WMI_CMD_UNSUPPORTED,
    .tdls_set_state_cmdid = WMI_CMD_UNSUPPORTED,
    .tdls_peer_update_cmdid = WMI_CMD_UNSUPPORTED,
    .adaptive_qcs_cmdid = WMI_CMD_UNSUPPORTED,
    .scan_update_request_cmdid = WMI_10_4_SCAN_UPDATE_REQUEST_CMDID,
    .vdev_standby_response_cmdid = WMI_10_4_VDEV_STANDBY_RESPONSE_CMDID,
    .vdev_resume_response_cmdid = WMI_10_4_VDEV_RESUME_RESPONSE_CMDID,
    .wlan_peer_caching_add_peer_cmdid = WMI_10_4_WLAN_PEER_CACHING_ADD_PEER_CMDID,
    .wlan_peer_caching_evict_peer_cmdid = WMI_10_4_WLAN_PEER_CACHING_EVICT_PEER_CMDID,
    .wlan_peer_caching_restore_peer_cmdid = WMI_10_4_WLAN_PEER_CACHING_RESTORE_PEER_CMDID,
    .wlan_peer_caching_print_all_peers_info_cmdid =
        WMI_10_4_WLAN_PEER_CACHING_PRINT_ALL_PEERS_INFO_CMDID,
    .peer_update_wds_entry_cmdid = WMI_10_4_PEER_UPDATE_WDS_ENTRY_CMDID,
    .peer_add_proxy_sta_entry_cmdid = WMI_10_4_PEER_ADD_PROXY_STA_ENTRY_CMDID,
    .rtt_keepalive_cmdid = WMI_10_4_RTT_KEEPALIVE_CMDID,
    .oem_req_cmdid = WMI_10_4_OEM_REQ_CMDID,
    .nan_cmdid = WMI_10_4_NAN_CMDID,
    .vdev_ratemask_cmdid = WMI_10_4_VDEV_RATEMASK_CMDID,
    .qboost_cfg_cmdid = WMI_10_4_QBOOST_CFG_CMDID,
    .pdev_smart_ant_enable_cmdid = WMI_10_4_PDEV_SMART_ANT_ENABLE_CMDID,
    .pdev_smart_ant_set_rx_antenna_cmdid = WMI_10_4_PDEV_SMART_ANT_SET_RX_ANTENNA_CMDID,
    .peer_smart_ant_set_tx_antenna_cmdid = WMI_10_4_PEER_SMART_ANT_SET_TX_ANTENNA_CMDID,
    .peer_smart_ant_set_train_info_cmdid = WMI_10_4_PEER_SMART_ANT_SET_TRAIN_INFO_CMDID,
    .peer_smart_ant_set_node_config_ops_cmdid = WMI_10_4_PEER_SMART_ANT_SET_NODE_CONFIG_OPS_CMDID,
    .pdev_set_antenna_switch_table_cmdid = WMI_10_4_PDEV_SET_ANTENNA_SWITCH_TABLE_CMDID,
    .pdev_set_ctl_table_cmdid = WMI_10_4_PDEV_SET_CTL_TABLE_CMDID,
    .pdev_set_mimogain_table_cmdid = WMI_10_4_PDEV_SET_MIMOGAIN_TABLE_CMDID,
    .pdev_ratepwr_table_cmdid = WMI_10_4_PDEV_RATEPWR_TABLE_CMDID,
    .pdev_ratepwr_chainmsk_table_cmdid = WMI_10_4_PDEV_RATEPWR_CHAINMSK_TABLE_CMDID,
    .pdev_fips_cmdid = WMI_10_4_PDEV_FIPS_CMDID,
    .tt_set_conf_cmdid = WMI_10_4_TT_SET_CONF_CMDID,
    .fwtest_cmdid = WMI_10_4_FWTEST_CMDID,
    .vdev_atf_request_cmdid = WMI_10_4_VDEV_ATF_REQUEST_CMDID,
    .peer_atf_request_cmdid = WMI_10_4_PEER_ATF_REQUEST_CMDID,
    .pdev_get_ani_cck_config_cmdid = WMI_10_4_PDEV_GET_ANI_CCK_CONFIG_CMDID,
    .pdev_get_ani_ofdm_config_cmdid = WMI_10_4_PDEV_GET_ANI_OFDM_CONFIG_CMDID,
    .pdev_reserve_ast_entry_cmdid = WMI_10_4_PDEV_RESERVE_AST_ENTRY_CMDID,
    .pdev_get_nfcal_power_cmdid = WMI_10_4_PDEV_GET_NFCAL_POWER_CMDID,
    .pdev_get_tpc_cmdid = WMI_10_4_PDEV_GET_TPC_CMDID,
    .pdev_get_ast_info_cmdid = WMI_10_4_PDEV_GET_AST_INFO_CMDID,
    .vdev_set_dscp_tid_map_cmdid = WMI_10_4_VDEV_SET_DSCP_TID_MAP_CMDID,
    .pdev_get_info_cmdid = WMI_10_4_PDEV_GET_INFO_CMDID,
    .vdev_get_info_cmdid = WMI_10_4_VDEV_GET_INFO_CMDID,
    .vdev_filter_neighbor_rx_packets_cmdid = WMI_10_4_VDEV_FILTER_NEIGHBOR_RX_PACKETS_CMDID,
    .mu_cal_start_cmdid = WMI_10_4_MU_CAL_START_CMDID,
    .set_cca_params_cmdid = WMI_10_4_SET_CCA_PARAMS_CMDID,
    .pdev_bss_chan_info_request_cmdid = WMI_10_4_PDEV_BSS_CHAN_INFO_REQUEST_CMDID,
    .ext_resource_cfg_cmdid = WMI_10_4_EXT_RESOURCE_CFG_CMDID,
};

/* MAIN WMI VDEV param map */
static struct wmi_vdev_param_map wmi_vdev_param_map = {
    .rts_threshold = WMI_VDEV_PARAM_RTS_THRESHOLD,
    .fragmentation_threshold = WMI_VDEV_PARAM_FRAGMENTATION_THRESHOLD,
    .beacon_interval = WMI_VDEV_PARAM_BEACON_INTERVAL,
    .listen_interval = WMI_VDEV_PARAM_LISTEN_INTERVAL,
    .multicast_rate = WMI_VDEV_PARAM_MULTICAST_RATE,
    .mgmt_tx_rate = WMI_VDEV_PARAM_MGMT_TX_RATE,
    .slot_time = WMI_VDEV_PARAM_SLOT_TIME,
    .preamble = WMI_VDEV_PARAM_PREAMBLE,
    .swba_time = WMI_VDEV_PARAM_SWBA_TIME,
    .wmi_vdev_stats_update_period = WMI_VDEV_STATS_UPDATE_PERIOD,
    .wmi_vdev_pwrsave_ageout_time = WMI_VDEV_PWRSAVE_AGEOUT_TIME,
    .wmi_vdev_host_swba_interval = WMI_VDEV_HOST_SWBA_INTERVAL,
    .dtim_period = WMI_VDEV_PARAM_DTIM_PERIOD,
    .wmi_vdev_oc_scheduler_air_time_limit = WMI_VDEV_OC_SCHEDULER_AIR_TIME_LIMIT,
    .wds = WMI_VDEV_PARAM_WDS,
    .atim_window = WMI_VDEV_PARAM_ATIM_WINDOW,
    .bmiss_count_max = WMI_VDEV_PARAM_BMISS_COUNT_MAX,
    .bmiss_first_bcnt = WMI_VDEV_PARAM_BMISS_FIRST_BCNT,
    .bmiss_final_bcnt = WMI_VDEV_PARAM_BMISS_FINAL_BCNT,
    .feature_wmm = WMI_VDEV_PARAM_FEATURE_WMM,
    .chwidth = WMI_VDEV_PARAM_CHWIDTH,
    .chextoffset = WMI_VDEV_PARAM_CHEXTOFFSET,
    .disable_htprotection = WMI_VDEV_PARAM_DISABLE_HTPROTECTION,
    .sta_quickkickout = WMI_VDEV_PARAM_STA_QUICKKICKOUT,
    .mgmt_rate = WMI_VDEV_PARAM_MGMT_RATE,
    .protection_mode = WMI_VDEV_PARAM_PROTECTION_MODE,
    .fixed_rate = WMI_VDEV_PARAM_FIXED_RATE,
    .sgi = WMI_VDEV_PARAM_SGI,
    .ldpc = WMI_VDEV_PARAM_LDPC,
    .tx_stbc = WMI_VDEV_PARAM_TX_STBC,
    .rx_stbc = WMI_VDEV_PARAM_RX_STBC,
    .intra_bss_fwd = WMI_VDEV_PARAM_INTRA_BSS_FWD,
    .def_keyid = WMI_VDEV_PARAM_DEF_KEYID,
    .nss = WMI_VDEV_PARAM_NSS,
    .bcast_data_rate = WMI_VDEV_PARAM_BCAST_DATA_RATE,
    .mcast_data_rate = WMI_VDEV_PARAM_MCAST_DATA_RATE,
    .mcast_indicate = WMI_VDEV_PARAM_MCAST_INDICATE,
    .dhcp_indicate = WMI_VDEV_PARAM_DHCP_INDICATE,
    .unknown_dest_indicate = WMI_VDEV_PARAM_UNKNOWN_DEST_INDICATE,
    .ap_keepalive_min_idle_inactive_time_secs =
        WMI_VDEV_PARAM_AP_KEEPALIVE_MIN_IDLE_INACTIVE_TIME_SECS,
    .ap_keepalive_max_idle_inactive_time_secs =
        WMI_VDEV_PARAM_AP_KEEPALIVE_MAX_IDLE_INACTIVE_TIME_SECS,
    .ap_keepalive_max_unresponsive_time_secs =
        WMI_VDEV_PARAM_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS,
    .ap_enable_nawds = WMI_VDEV_PARAM_AP_ENABLE_NAWDS,
    .mcast2ucast_set = WMI_VDEV_PARAM_UNSUPPORTED,
    .enable_rtscts = WMI_VDEV_PARAM_ENABLE_RTSCTS,
    .txbf = WMI_VDEV_PARAM_TXBF,
    .packet_powersave = WMI_VDEV_PARAM_PACKET_POWERSAVE,
    .drop_unencry = WMI_VDEV_PARAM_DROP_UNENCRY,
    .tx_encap_type = WMI_VDEV_PARAM_TX_ENCAP_TYPE,
    .ap_detect_out_of_sync_sleeping_sta_time_secs = WMI_VDEV_PARAM_UNSUPPORTED,
    .rc_num_retries = WMI_VDEV_PARAM_UNSUPPORTED,
    .cabq_maxdur = WMI_VDEV_PARAM_UNSUPPORTED,
    .mfptest_set = WMI_VDEV_PARAM_UNSUPPORTED,
    .rts_fixed_rate = WMI_VDEV_PARAM_UNSUPPORTED,
    .vht_sgimask = WMI_VDEV_PARAM_UNSUPPORTED,
    .vht80_ratemask = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_adjust_enable = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_tgt_bmiss_num = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_bmiss_sample_cycle = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_slop_step = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_init_slop = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_adjust_pause = WMI_VDEV_PARAM_UNSUPPORTED,
    .proxy_sta = WMI_VDEV_PARAM_UNSUPPORTED,
    .meru_vc = WMI_VDEV_PARAM_UNSUPPORTED,
    .rx_decap_type = WMI_VDEV_PARAM_UNSUPPORTED,
    .bw_nss_ratemask = WMI_VDEV_PARAM_UNSUPPORTED,
};

/* 10.X WMI VDEV param map */
static struct wmi_vdev_param_map wmi_10x_vdev_param_map = {
    .rts_threshold = WMI_10X_VDEV_PARAM_RTS_THRESHOLD,
    .fragmentation_threshold = WMI_10X_VDEV_PARAM_FRAGMENTATION_THRESHOLD,
    .beacon_interval = WMI_10X_VDEV_PARAM_BEACON_INTERVAL,
    .listen_interval = WMI_10X_VDEV_PARAM_LISTEN_INTERVAL,
    .multicast_rate = WMI_10X_VDEV_PARAM_MULTICAST_RATE,
    .mgmt_tx_rate = WMI_10X_VDEV_PARAM_MGMT_TX_RATE,
    .slot_time = WMI_10X_VDEV_PARAM_SLOT_TIME,
    .preamble = WMI_10X_VDEV_PARAM_PREAMBLE,
    .swba_time = WMI_10X_VDEV_PARAM_SWBA_TIME,
    .wmi_vdev_stats_update_period = WMI_10X_VDEV_STATS_UPDATE_PERIOD,
    .wmi_vdev_pwrsave_ageout_time = WMI_10X_VDEV_PWRSAVE_AGEOUT_TIME,
    .wmi_vdev_host_swba_interval = WMI_10X_VDEV_HOST_SWBA_INTERVAL,
    .dtim_period = WMI_10X_VDEV_PARAM_DTIM_PERIOD,
    .wmi_vdev_oc_scheduler_air_time_limit = WMI_10X_VDEV_OC_SCHEDULER_AIR_TIME_LIMIT,
    .wds = WMI_10X_VDEV_PARAM_WDS,
    .atim_window = WMI_10X_VDEV_PARAM_ATIM_WINDOW,
    .bmiss_count_max = WMI_10X_VDEV_PARAM_BMISS_COUNT_MAX,
    .bmiss_first_bcnt = WMI_VDEV_PARAM_UNSUPPORTED,
    .bmiss_final_bcnt = WMI_VDEV_PARAM_UNSUPPORTED,
    .feature_wmm = WMI_10X_VDEV_PARAM_FEATURE_WMM,
    .chwidth = WMI_10X_VDEV_PARAM_CHWIDTH,
    .chextoffset = WMI_10X_VDEV_PARAM_CHEXTOFFSET,
    .disable_htprotection = WMI_10X_VDEV_PARAM_DISABLE_HTPROTECTION,
    .sta_quickkickout = WMI_10X_VDEV_PARAM_STA_QUICKKICKOUT,
    .mgmt_rate = WMI_10X_VDEV_PARAM_MGMT_RATE,
    .protection_mode = WMI_10X_VDEV_PARAM_PROTECTION_MODE,
    .fixed_rate = WMI_10X_VDEV_PARAM_FIXED_RATE,
    .sgi = WMI_10X_VDEV_PARAM_SGI,
    .ldpc = WMI_10X_VDEV_PARAM_LDPC,
    .tx_stbc = WMI_10X_VDEV_PARAM_TX_STBC,
    .rx_stbc = WMI_10X_VDEV_PARAM_RX_STBC,
    .intra_bss_fwd = WMI_10X_VDEV_PARAM_INTRA_BSS_FWD,
    .def_keyid = WMI_10X_VDEV_PARAM_DEF_KEYID,
    .nss = WMI_10X_VDEV_PARAM_NSS,
    .bcast_data_rate = WMI_10X_VDEV_PARAM_BCAST_DATA_RATE,
    .mcast_data_rate = WMI_10X_VDEV_PARAM_MCAST_DATA_RATE,
    .mcast_indicate = WMI_10X_VDEV_PARAM_MCAST_INDICATE,
    .dhcp_indicate = WMI_10X_VDEV_PARAM_DHCP_INDICATE,
    .unknown_dest_indicate = WMI_10X_VDEV_PARAM_UNKNOWN_DEST_INDICATE,
    .ap_keepalive_min_idle_inactive_time_secs =
        WMI_10X_VDEV_PARAM_AP_KEEPALIVE_MIN_IDLE_INACTIVE_TIME_SECS,
    .ap_keepalive_max_idle_inactive_time_secs =
        WMI_10X_VDEV_PARAM_AP_KEEPALIVE_MAX_IDLE_INACTIVE_TIME_SECS,
    .ap_keepalive_max_unresponsive_time_secs =
        WMI_10X_VDEV_PARAM_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS,
    .ap_enable_nawds = WMI_10X_VDEV_PARAM_AP_ENABLE_NAWDS,
    .mcast2ucast_set = WMI_10X_VDEV_PARAM_MCAST2UCAST_SET,
    .enable_rtscts = WMI_10X_VDEV_PARAM_ENABLE_RTSCTS,
    .txbf = WMI_VDEV_PARAM_UNSUPPORTED,
    .packet_powersave = WMI_VDEV_PARAM_UNSUPPORTED,
    .drop_unencry = WMI_VDEV_PARAM_UNSUPPORTED,
    .tx_encap_type = WMI_VDEV_PARAM_UNSUPPORTED,
    .ap_detect_out_of_sync_sleeping_sta_time_secs =
        WMI_10X_VDEV_PARAM_AP_DETECT_OUT_OF_SYNC_SLEEPING_STA_TIME_SECS,
    .rc_num_retries = WMI_VDEV_PARAM_UNSUPPORTED,
    .cabq_maxdur = WMI_VDEV_PARAM_UNSUPPORTED,
    .mfptest_set = WMI_VDEV_PARAM_UNSUPPORTED,
    .rts_fixed_rate = WMI_VDEV_PARAM_UNSUPPORTED,
    .vht_sgimask = WMI_VDEV_PARAM_UNSUPPORTED,
    .vht80_ratemask = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_adjust_enable = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_tgt_bmiss_num = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_bmiss_sample_cycle = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_slop_step = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_init_slop = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_adjust_pause = WMI_VDEV_PARAM_UNSUPPORTED,
    .proxy_sta = WMI_VDEV_PARAM_UNSUPPORTED,
    .meru_vc = WMI_VDEV_PARAM_UNSUPPORTED,
    .rx_decap_type = WMI_VDEV_PARAM_UNSUPPORTED,
    .bw_nss_ratemask = WMI_VDEV_PARAM_UNSUPPORTED,
};

static struct wmi_vdev_param_map wmi_10_2_4_vdev_param_map = {
    .rts_threshold = WMI_10X_VDEV_PARAM_RTS_THRESHOLD,
    .fragmentation_threshold = WMI_10X_VDEV_PARAM_FRAGMENTATION_THRESHOLD,
    .beacon_interval = WMI_10X_VDEV_PARAM_BEACON_INTERVAL,
    .listen_interval = WMI_10X_VDEV_PARAM_LISTEN_INTERVAL,
    .multicast_rate = WMI_10X_VDEV_PARAM_MULTICAST_RATE,
    .mgmt_tx_rate = WMI_10X_VDEV_PARAM_MGMT_TX_RATE,
    .slot_time = WMI_10X_VDEV_PARAM_SLOT_TIME,
    .preamble = WMI_10X_VDEV_PARAM_PREAMBLE,
    .swba_time = WMI_10X_VDEV_PARAM_SWBA_TIME,
    .wmi_vdev_stats_update_period = WMI_10X_VDEV_STATS_UPDATE_PERIOD,
    .wmi_vdev_pwrsave_ageout_time = WMI_10X_VDEV_PWRSAVE_AGEOUT_TIME,
    .wmi_vdev_host_swba_interval = WMI_10X_VDEV_HOST_SWBA_INTERVAL,
    .dtim_period = WMI_10X_VDEV_PARAM_DTIM_PERIOD,
    .wmi_vdev_oc_scheduler_air_time_limit = WMI_10X_VDEV_OC_SCHEDULER_AIR_TIME_LIMIT,
    .wds = WMI_10X_VDEV_PARAM_WDS,
    .atim_window = WMI_10X_VDEV_PARAM_ATIM_WINDOW,
    .bmiss_count_max = WMI_10X_VDEV_PARAM_BMISS_COUNT_MAX,
    .bmiss_first_bcnt = WMI_VDEV_PARAM_UNSUPPORTED,
    .bmiss_final_bcnt = WMI_VDEV_PARAM_UNSUPPORTED,
    .feature_wmm = WMI_10X_VDEV_PARAM_FEATURE_WMM,
    .chwidth = WMI_10X_VDEV_PARAM_CHWIDTH,
    .chextoffset = WMI_10X_VDEV_PARAM_CHEXTOFFSET,
    .disable_htprotection = WMI_10X_VDEV_PARAM_DISABLE_HTPROTECTION,
    .sta_quickkickout = WMI_10X_VDEV_PARAM_STA_QUICKKICKOUT,
    .mgmt_rate = WMI_10X_VDEV_PARAM_MGMT_RATE,
    .protection_mode = WMI_10X_VDEV_PARAM_PROTECTION_MODE,
    .fixed_rate = WMI_10X_VDEV_PARAM_FIXED_RATE,
    .sgi = WMI_10X_VDEV_PARAM_SGI,
    .ldpc = WMI_10X_VDEV_PARAM_LDPC,
    .tx_stbc = WMI_10X_VDEV_PARAM_TX_STBC,
    .rx_stbc = WMI_10X_VDEV_PARAM_RX_STBC,
    .intra_bss_fwd = WMI_10X_VDEV_PARAM_INTRA_BSS_FWD,
    .def_keyid = WMI_10X_VDEV_PARAM_DEF_KEYID,
    .nss = WMI_10X_VDEV_PARAM_NSS,
    .bcast_data_rate = WMI_10X_VDEV_PARAM_BCAST_DATA_RATE,
    .mcast_data_rate = WMI_10X_VDEV_PARAM_MCAST_DATA_RATE,
    .mcast_indicate = WMI_10X_VDEV_PARAM_MCAST_INDICATE,
    .dhcp_indicate = WMI_10X_VDEV_PARAM_DHCP_INDICATE,
    .unknown_dest_indicate = WMI_10X_VDEV_PARAM_UNKNOWN_DEST_INDICATE,
    .ap_keepalive_min_idle_inactive_time_secs =
        WMI_10X_VDEV_PARAM_AP_KEEPALIVE_MIN_IDLE_INACTIVE_TIME_SECS,
    .ap_keepalive_max_idle_inactive_time_secs =
        WMI_10X_VDEV_PARAM_AP_KEEPALIVE_MAX_IDLE_INACTIVE_TIME_SECS,
    .ap_keepalive_max_unresponsive_time_secs =
        WMI_10X_VDEV_PARAM_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS,
    .ap_enable_nawds = WMI_10X_VDEV_PARAM_AP_ENABLE_NAWDS,
    .mcast2ucast_set = WMI_10X_VDEV_PARAM_MCAST2UCAST_SET,
    .enable_rtscts = WMI_10X_VDEV_PARAM_ENABLE_RTSCTS,
    .txbf = WMI_VDEV_PARAM_UNSUPPORTED,
    .packet_powersave = WMI_VDEV_PARAM_UNSUPPORTED,
    .drop_unencry = WMI_VDEV_PARAM_UNSUPPORTED,
    .tx_encap_type = WMI_VDEV_PARAM_UNSUPPORTED,
    .ap_detect_out_of_sync_sleeping_sta_time_secs =
        WMI_10X_VDEV_PARAM_AP_DETECT_OUT_OF_SYNC_SLEEPING_STA_TIME_SECS,
    .rc_num_retries = WMI_VDEV_PARAM_UNSUPPORTED,
    .cabq_maxdur = WMI_VDEV_PARAM_UNSUPPORTED,
    .mfptest_set = WMI_VDEV_PARAM_UNSUPPORTED,
    .rts_fixed_rate = WMI_VDEV_PARAM_UNSUPPORTED,
    .vht_sgimask = WMI_VDEV_PARAM_UNSUPPORTED,
    .vht80_ratemask = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_adjust_enable = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_tgt_bmiss_num = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_bmiss_sample_cycle = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_slop_step = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_init_slop = WMI_VDEV_PARAM_UNSUPPORTED,
    .early_rx_adjust_pause = WMI_VDEV_PARAM_UNSUPPORTED,
    .proxy_sta = WMI_VDEV_PARAM_UNSUPPORTED,
    .meru_vc = WMI_VDEV_PARAM_UNSUPPORTED,
    .rx_decap_type = WMI_VDEV_PARAM_UNSUPPORTED,
    .bw_nss_ratemask = WMI_VDEV_PARAM_UNSUPPORTED,
};

static struct wmi_vdev_param_map wmi_10_4_vdev_param_map = {
    .rts_threshold = WMI_10_4_VDEV_PARAM_RTS_THRESHOLD,
    .fragmentation_threshold = WMI_10_4_VDEV_PARAM_FRAGMENTATION_THRESHOLD,
    .beacon_interval = WMI_10_4_VDEV_PARAM_BEACON_INTERVAL,
    .listen_interval = WMI_10_4_VDEV_PARAM_LISTEN_INTERVAL,
    .multicast_rate = WMI_10_4_VDEV_PARAM_MULTICAST_RATE,
    .mgmt_tx_rate = WMI_10_4_VDEV_PARAM_MGMT_TX_RATE,
    .slot_time = WMI_10_4_VDEV_PARAM_SLOT_TIME,
    .preamble = WMI_10_4_VDEV_PARAM_PREAMBLE,
    .swba_time = WMI_10_4_VDEV_PARAM_SWBA_TIME,
    .wmi_vdev_stats_update_period = WMI_10_4_VDEV_STATS_UPDATE_PERIOD,
    .wmi_vdev_pwrsave_ageout_time = WMI_10_4_VDEV_PWRSAVE_AGEOUT_TIME,
    .wmi_vdev_host_swba_interval = WMI_10_4_VDEV_HOST_SWBA_INTERVAL,
    .dtim_period = WMI_10_4_VDEV_PARAM_DTIM_PERIOD,
    .wmi_vdev_oc_scheduler_air_time_limit = WMI_10_4_VDEV_OC_SCHEDULER_AIR_TIME_LIMIT,
    .wds = WMI_10_4_VDEV_PARAM_WDS,
    .atim_window = WMI_10_4_VDEV_PARAM_ATIM_WINDOW,
    .bmiss_count_max = WMI_10_4_VDEV_PARAM_BMISS_COUNT_MAX,
    .bmiss_first_bcnt = WMI_10_4_VDEV_PARAM_BMISS_FIRST_BCNT,
    .bmiss_final_bcnt = WMI_10_4_VDEV_PARAM_BMISS_FINAL_BCNT,
    .feature_wmm = WMI_10_4_VDEV_PARAM_FEATURE_WMM,
    .chwidth = WMI_10_4_VDEV_PARAM_CHWIDTH,
    .chextoffset = WMI_10_4_VDEV_PARAM_CHEXTOFFSET,
    .disable_htprotection = WMI_10_4_VDEV_PARAM_DISABLE_HTPROTECTION,
    .sta_quickkickout = WMI_10_4_VDEV_PARAM_STA_QUICKKICKOUT,
    .mgmt_rate = WMI_10_4_VDEV_PARAM_MGMT_RATE,
    .protection_mode = WMI_10_4_VDEV_PARAM_PROTECTION_MODE,
    .fixed_rate = WMI_10_4_VDEV_PARAM_FIXED_RATE,
    .sgi = WMI_10_4_VDEV_PARAM_SGI,
    .ldpc = WMI_10_4_VDEV_PARAM_LDPC,
    .tx_stbc = WMI_10_4_VDEV_PARAM_TX_STBC,
    .rx_stbc = WMI_10_4_VDEV_PARAM_RX_STBC,
    .intra_bss_fwd = WMI_10_4_VDEV_PARAM_INTRA_BSS_FWD,
    .def_keyid = WMI_10_4_VDEV_PARAM_DEF_KEYID,
    .nss = WMI_10_4_VDEV_PARAM_NSS,
    .bcast_data_rate = WMI_10_4_VDEV_PARAM_BCAST_DATA_RATE,
    .mcast_data_rate = WMI_10_4_VDEV_PARAM_MCAST_DATA_RATE,
    .mcast_indicate = WMI_10_4_VDEV_PARAM_MCAST_INDICATE,
    .dhcp_indicate = WMI_10_4_VDEV_PARAM_DHCP_INDICATE,
    .unknown_dest_indicate = WMI_10_4_VDEV_PARAM_UNKNOWN_DEST_INDICATE,
    .ap_keepalive_min_idle_inactive_time_secs =
        WMI_10_4_VDEV_PARAM_AP_KEEPALIVE_MIN_IDLE_INACTIVE_TIME_SECS,
    .ap_keepalive_max_idle_inactive_time_secs =
        WMI_10_4_VDEV_PARAM_AP_KEEPALIVE_MAX_IDLE_INACTIVE_TIME_SECS,
    .ap_keepalive_max_unresponsive_time_secs =
        WMI_10_4_VDEV_PARAM_AP_KEEPALIVE_MAX_UNRESPONSIVE_TIME_SECS,
    .ap_enable_nawds = WMI_10_4_VDEV_PARAM_AP_ENABLE_NAWDS,
    .mcast2ucast_set = WMI_10_4_VDEV_PARAM_MCAST2UCAST_SET,
    .enable_rtscts = WMI_10_4_VDEV_PARAM_ENABLE_RTSCTS,
    .txbf = WMI_10_4_VDEV_PARAM_TXBF,
    .packet_powersave = WMI_10_4_VDEV_PARAM_PACKET_POWERSAVE,
    .drop_unencry = WMI_10_4_VDEV_PARAM_DROP_UNENCRY,
    .tx_encap_type = WMI_10_4_VDEV_PARAM_TX_ENCAP_TYPE,
    .ap_detect_out_of_sync_sleeping_sta_time_secs =
        WMI_10_4_VDEV_PARAM_AP_DETECT_OUT_OF_SYNC_SLEEPING_STA_TIME_SECS,
    .rc_num_retries = WMI_10_4_VDEV_PARAM_RC_NUM_RETRIES,
    .cabq_maxdur = WMI_10_4_VDEV_PARAM_CABQ_MAXDUR,
    .mfptest_set = WMI_10_4_VDEV_PARAM_MFPTEST_SET,
    .rts_fixed_rate = WMI_10_4_VDEV_PARAM_RTS_FIXED_RATE,
    .vht_sgimask = WMI_10_4_VDEV_PARAM_VHT_SGIMASK,
    .vht80_ratemask = WMI_10_4_VDEV_PARAM_VHT80_RATEMASK,
    .early_rx_adjust_enable = WMI_10_4_VDEV_PARAM_EARLY_RX_ADJUST_ENABLE,
    .early_rx_tgt_bmiss_num = WMI_10_4_VDEV_PARAM_EARLY_RX_TGT_BMISS_NUM,
    .early_rx_bmiss_sample_cycle = WMI_10_4_VDEV_PARAM_EARLY_RX_BMISS_SAMPLE_CYCLE,
    .early_rx_slop_step = WMI_10_4_VDEV_PARAM_EARLY_RX_SLOP_STEP,
    .early_rx_init_slop = WMI_10_4_VDEV_PARAM_EARLY_RX_INIT_SLOP,
    .early_rx_adjust_pause = WMI_10_4_VDEV_PARAM_EARLY_RX_ADJUST_PAUSE,
    .proxy_sta = WMI_10_4_VDEV_PARAM_PROXY_STA,
    .meru_vc = WMI_10_4_VDEV_PARAM_MERU_VC,
    .rx_decap_type = WMI_10_4_VDEV_PARAM_RX_DECAP_TYPE,
    .bw_nss_ratemask = WMI_10_4_VDEV_PARAM_BW_NSS_RATEMASK,
    .inc_tsf = WMI_10_4_VDEV_PARAM_TSF_INCREMENT,
    .dec_tsf = WMI_10_4_VDEV_PARAM_TSF_DECREMENT,
};

static struct wmi_pdev_param_map wmi_pdev_param_map = {
    .tx_chain_mask = WMI_PDEV_PARAM_TX_CHAIN_MASK,
    .rx_chain_mask = WMI_PDEV_PARAM_RX_CHAIN_MASK,
    .txpower_limit2g = WMI_PDEV_PARAM_TXPOWER_LIMIT2G,
    .txpower_limit5g = WMI_PDEV_PARAM_TXPOWER_LIMIT5G,
    .txpower_scale = WMI_PDEV_PARAM_TXPOWER_SCALE,
    .beacon_gen_mode = WMI_PDEV_PARAM_BEACON_GEN_MODE,
    .beacon_tx_mode = WMI_PDEV_PARAM_BEACON_TX_MODE,
    .resmgr_offchan_mode = WMI_PDEV_PARAM_RESMGR_OFFCHAN_MODE,
    .protection_mode = WMI_PDEV_PARAM_PROTECTION_MODE,
    .dynamic_bw = WMI_PDEV_PARAM_DYNAMIC_BW,
    .non_agg_sw_retry_th = WMI_PDEV_PARAM_NON_AGG_SW_RETRY_TH,
    .agg_sw_retry_th = WMI_PDEV_PARAM_AGG_SW_RETRY_TH,
    .sta_kickout_th = WMI_PDEV_PARAM_STA_KICKOUT_TH,
    .ac_aggrsize_scaling = WMI_PDEV_PARAM_AC_AGGRSIZE_SCALING,
    .ltr_enable = WMI_PDEV_PARAM_LTR_ENABLE,
    .ltr_ac_latency_be = WMI_PDEV_PARAM_LTR_AC_LATENCY_BE,
    .ltr_ac_latency_bk = WMI_PDEV_PARAM_LTR_AC_LATENCY_BK,
    .ltr_ac_latency_vi = WMI_PDEV_PARAM_LTR_AC_LATENCY_VI,
    .ltr_ac_latency_vo = WMI_PDEV_PARAM_LTR_AC_LATENCY_VO,
    .ltr_ac_latency_timeout = WMI_PDEV_PARAM_LTR_AC_LATENCY_TIMEOUT,
    .ltr_sleep_override = WMI_PDEV_PARAM_LTR_SLEEP_OVERRIDE,
    .ltr_rx_override = WMI_PDEV_PARAM_LTR_RX_OVERRIDE,
    .ltr_tx_activity_timeout = WMI_PDEV_PARAM_LTR_TX_ACTIVITY_TIMEOUT,
    .l1ss_enable = WMI_PDEV_PARAM_L1SS_ENABLE,
    .dsleep_enable = WMI_PDEV_PARAM_DSLEEP_ENABLE,
    .pcielp_txbuf_flush = WMI_PDEV_PARAM_PCIELP_TXBUF_FLUSH,
    .pcielp_txbuf_watermark = WMI_PDEV_PARAM_PCIELP_TXBUF_TMO_EN,
    .pcielp_txbuf_tmo_en = WMI_PDEV_PARAM_PCIELP_TXBUF_TMO_EN,
    .pcielp_txbuf_tmo_value = WMI_PDEV_PARAM_PCIELP_TXBUF_TMO_VALUE,
    .pdev_stats_update_period = WMI_PDEV_PARAM_PDEV_STATS_UPDATE_PERIOD,
    .vdev_stats_update_period = WMI_PDEV_PARAM_VDEV_STATS_UPDATE_PERIOD,
    .peer_stats_update_period = WMI_PDEV_PARAM_PEER_STATS_UPDATE_PERIOD,
    .bcnflt_stats_update_period = WMI_PDEV_PARAM_BCNFLT_STATS_UPDATE_PERIOD,
    .pmf_qos = WMI_PDEV_PARAM_PMF_QOS,
    .arp_ac_override = WMI_PDEV_PARAM_ARP_AC_OVERRIDE,
    .dcs = WMI_PDEV_PARAM_DCS,
    .ani_enable = WMI_PDEV_PARAM_ANI_ENABLE,
    .ani_poll_period = WMI_PDEV_PARAM_ANI_POLL_PERIOD,
    .ani_listen_period = WMI_PDEV_PARAM_ANI_LISTEN_PERIOD,
    .ani_ofdm_level = WMI_PDEV_PARAM_ANI_OFDM_LEVEL,
    .ani_cck_level = WMI_PDEV_PARAM_ANI_CCK_LEVEL,
    .dyntxchain = WMI_PDEV_PARAM_DYNTXCHAIN,
    .proxy_sta = WMI_PDEV_PARAM_PROXY_STA,
    .idle_ps_config = WMI_PDEV_PARAM_IDLE_PS_CONFIG,
    .power_gating_sleep = WMI_PDEV_PARAM_POWER_GATING_SLEEP,
    .fast_channel_reset = WMI_PDEV_PARAM_UNSUPPORTED,
    .burst_dur = WMI_PDEV_PARAM_UNSUPPORTED,
    .burst_enable = WMI_PDEV_PARAM_UNSUPPORTED,
    .cal_period = WMI_PDEV_PARAM_UNSUPPORTED,
    .aggr_burst = WMI_PDEV_PARAM_UNSUPPORTED,
    .rx_decap_mode = WMI_PDEV_PARAM_UNSUPPORTED,
    .smart_antenna_default_antenna = WMI_PDEV_PARAM_UNSUPPORTED,
    .igmpmld_override = WMI_PDEV_PARAM_UNSUPPORTED,
    .igmpmld_tid = WMI_PDEV_PARAM_UNSUPPORTED,
    .antenna_gain = WMI_PDEV_PARAM_UNSUPPORTED,
    .rx_filter = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_mcast_to_ucast_tid = WMI_PDEV_PARAM_UNSUPPORTED,
    .proxy_sta_mode = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_mcast2ucast_mode = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_mcast2ucast_buffer = WMI_PDEV_PARAM_UNSUPPORTED,
    .remove_mcast2ucast_buffer = WMI_PDEV_PARAM_UNSUPPORTED,
    .peer_sta_ps_statechg_enable = WMI_PDEV_PARAM_UNSUPPORTED,
    .igmpmld_ac_override = WMI_PDEV_PARAM_UNSUPPORTED,
    .block_interbss = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_disable_reset_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_msdu_ttl_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_ppdu_duration_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .txbf_sound_period_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_promisc_mode_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_burst_mode_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .en_stats = WMI_PDEV_PARAM_UNSUPPORTED,
    .mu_group_policy = WMI_PDEV_PARAM_UNSUPPORTED,
    .noise_detection = WMI_PDEV_PARAM_UNSUPPORTED,
    .noise_threshold = WMI_PDEV_PARAM_UNSUPPORTED,
    .dpd_enable = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_mcast_bcast_echo = WMI_PDEV_PARAM_UNSUPPORTED,
    .atf_strict_sch = WMI_PDEV_PARAM_UNSUPPORTED,
    .atf_sched_duration = WMI_PDEV_PARAM_UNSUPPORTED,
    .ant_plzn = WMI_PDEV_PARAM_UNSUPPORTED,
    .mgmt_retry_limit = WMI_PDEV_PARAM_UNSUPPORTED,
    .sensitivity_level = WMI_PDEV_PARAM_UNSUPPORTED,
    .signed_txpower_2g = WMI_PDEV_PARAM_UNSUPPORTED,
    .signed_txpower_5g = WMI_PDEV_PARAM_UNSUPPORTED,
    .enable_per_tid_amsdu = WMI_PDEV_PARAM_UNSUPPORTED,
    .enable_per_tid_ampdu = WMI_PDEV_PARAM_UNSUPPORTED,
    .cca_threshold = WMI_PDEV_PARAM_UNSUPPORTED,
    .rts_fixed_rate = WMI_PDEV_PARAM_UNSUPPORTED,
    .pdev_reset = WMI_PDEV_PARAM_UNSUPPORTED,
    .wapi_mbssid_offset = WMI_PDEV_PARAM_UNSUPPORTED,
    .arp_srcaddr = WMI_PDEV_PARAM_UNSUPPORTED,
    .arp_dstaddr = WMI_PDEV_PARAM_UNSUPPORTED,
    .enable_btcoex = WMI_PDEV_PARAM_UNSUPPORTED,
};

static struct wmi_pdev_param_map wmi_10x_pdev_param_map = {
    .tx_chain_mask = WMI_10X_PDEV_PARAM_TX_CHAIN_MASK,
    .rx_chain_mask = WMI_10X_PDEV_PARAM_RX_CHAIN_MASK,
    .txpower_limit2g = WMI_10X_PDEV_PARAM_TXPOWER_LIMIT2G,
    .txpower_limit5g = WMI_10X_PDEV_PARAM_TXPOWER_LIMIT5G,
    .txpower_scale = WMI_10X_PDEV_PARAM_TXPOWER_SCALE,
    .beacon_gen_mode = WMI_10X_PDEV_PARAM_BEACON_GEN_MODE,
    .beacon_tx_mode = WMI_10X_PDEV_PARAM_BEACON_TX_MODE,
    .resmgr_offchan_mode = WMI_10X_PDEV_PARAM_RESMGR_OFFCHAN_MODE,
    .protection_mode = WMI_10X_PDEV_PARAM_PROTECTION_MODE,
    .dynamic_bw = WMI_10X_PDEV_PARAM_DYNAMIC_BW,
    .non_agg_sw_retry_th = WMI_10X_PDEV_PARAM_NON_AGG_SW_RETRY_TH,
    .agg_sw_retry_th = WMI_10X_PDEV_PARAM_AGG_SW_RETRY_TH,
    .sta_kickout_th = WMI_10X_PDEV_PARAM_STA_KICKOUT_TH,
    .ac_aggrsize_scaling = WMI_10X_PDEV_PARAM_AC_AGGRSIZE_SCALING,
    .ltr_enable = WMI_10X_PDEV_PARAM_LTR_ENABLE,
    .ltr_ac_latency_be = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_BE,
    .ltr_ac_latency_bk = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_BK,
    .ltr_ac_latency_vi = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_VI,
    .ltr_ac_latency_vo = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_VO,
    .ltr_ac_latency_timeout = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_TIMEOUT,
    .ltr_sleep_override = WMI_10X_PDEV_PARAM_LTR_SLEEP_OVERRIDE,
    .ltr_rx_override = WMI_10X_PDEV_PARAM_LTR_RX_OVERRIDE,
    .ltr_tx_activity_timeout = WMI_10X_PDEV_PARAM_LTR_TX_ACTIVITY_TIMEOUT,
    .l1ss_enable = WMI_10X_PDEV_PARAM_L1SS_ENABLE,
    .dsleep_enable = WMI_10X_PDEV_PARAM_DSLEEP_ENABLE,
    .pcielp_txbuf_flush = WMI_PDEV_PARAM_UNSUPPORTED,
    .pcielp_txbuf_watermark = WMI_PDEV_PARAM_UNSUPPORTED,
    .pcielp_txbuf_tmo_en = WMI_PDEV_PARAM_UNSUPPORTED,
    .pcielp_txbuf_tmo_value = WMI_PDEV_PARAM_UNSUPPORTED,
    .pdev_stats_update_period = WMI_10X_PDEV_PARAM_PDEV_STATS_UPDATE_PERIOD,
    .vdev_stats_update_period = WMI_10X_PDEV_PARAM_VDEV_STATS_UPDATE_PERIOD,
    .peer_stats_update_period = WMI_10X_PDEV_PARAM_PEER_STATS_UPDATE_PERIOD,
    .bcnflt_stats_update_period = WMI_10X_PDEV_PARAM_BCNFLT_STATS_UPDATE_PERIOD,
    .pmf_qos = WMI_10X_PDEV_PARAM_PMF_QOS,
    .arp_ac_override = WMI_10X_PDEV_PARAM_ARPDHCP_AC_OVERRIDE,
    .dcs = WMI_10X_PDEV_PARAM_DCS,
    .ani_enable = WMI_10X_PDEV_PARAM_ANI_ENABLE,
    .ani_poll_period = WMI_10X_PDEV_PARAM_ANI_POLL_PERIOD,
    .ani_listen_period = WMI_10X_PDEV_PARAM_ANI_LISTEN_PERIOD,
    .ani_ofdm_level = WMI_10X_PDEV_PARAM_ANI_OFDM_LEVEL,
    .ani_cck_level = WMI_10X_PDEV_PARAM_ANI_CCK_LEVEL,
    .dyntxchain = WMI_10X_PDEV_PARAM_DYNTXCHAIN,
    .proxy_sta = WMI_PDEV_PARAM_UNSUPPORTED,
    .idle_ps_config = WMI_PDEV_PARAM_UNSUPPORTED,
    .power_gating_sleep = WMI_PDEV_PARAM_UNSUPPORTED,
    .fast_channel_reset = WMI_10X_PDEV_PARAM_FAST_CHANNEL_RESET,
    .burst_dur = WMI_10X_PDEV_PARAM_BURST_DUR,
    .burst_enable = WMI_10X_PDEV_PARAM_BURST_ENABLE,
    .cal_period = WMI_10X_PDEV_PARAM_CAL_PERIOD,
    .aggr_burst = WMI_PDEV_PARAM_UNSUPPORTED,
    .rx_decap_mode = WMI_PDEV_PARAM_UNSUPPORTED,
    .smart_antenna_default_antenna = WMI_PDEV_PARAM_UNSUPPORTED,
    .igmpmld_override = WMI_PDEV_PARAM_UNSUPPORTED,
    .igmpmld_tid = WMI_PDEV_PARAM_UNSUPPORTED,
    .antenna_gain = WMI_PDEV_PARAM_UNSUPPORTED,
    .rx_filter = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_mcast_to_ucast_tid = WMI_PDEV_PARAM_UNSUPPORTED,
    .proxy_sta_mode = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_mcast2ucast_mode = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_mcast2ucast_buffer = WMI_PDEV_PARAM_UNSUPPORTED,
    .remove_mcast2ucast_buffer = WMI_PDEV_PARAM_UNSUPPORTED,
    .peer_sta_ps_statechg_enable = WMI_PDEV_PARAM_UNSUPPORTED,
    .igmpmld_ac_override = WMI_PDEV_PARAM_UNSUPPORTED,
    .block_interbss = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_disable_reset_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_msdu_ttl_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_ppdu_duration_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .txbf_sound_period_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_promisc_mode_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_burst_mode_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .en_stats = WMI_PDEV_PARAM_UNSUPPORTED,
    .mu_group_policy = WMI_PDEV_PARAM_UNSUPPORTED,
    .noise_detection = WMI_PDEV_PARAM_UNSUPPORTED,
    .noise_threshold = WMI_PDEV_PARAM_UNSUPPORTED,
    .dpd_enable = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_mcast_bcast_echo = WMI_PDEV_PARAM_UNSUPPORTED,
    .atf_strict_sch = WMI_PDEV_PARAM_UNSUPPORTED,
    .atf_sched_duration = WMI_PDEV_PARAM_UNSUPPORTED,
    .ant_plzn = WMI_PDEV_PARAM_UNSUPPORTED,
    .mgmt_retry_limit = WMI_PDEV_PARAM_UNSUPPORTED,
    .sensitivity_level = WMI_PDEV_PARAM_UNSUPPORTED,
    .signed_txpower_2g = WMI_PDEV_PARAM_UNSUPPORTED,
    .signed_txpower_5g = WMI_PDEV_PARAM_UNSUPPORTED,
    .enable_per_tid_amsdu = WMI_PDEV_PARAM_UNSUPPORTED,
    .enable_per_tid_ampdu = WMI_PDEV_PARAM_UNSUPPORTED,
    .cca_threshold = WMI_PDEV_PARAM_UNSUPPORTED,
    .rts_fixed_rate = WMI_PDEV_PARAM_UNSUPPORTED,
    .pdev_reset = WMI_PDEV_PARAM_UNSUPPORTED,
    .wapi_mbssid_offset = WMI_PDEV_PARAM_UNSUPPORTED,
    .arp_srcaddr = WMI_PDEV_PARAM_UNSUPPORTED,
    .arp_dstaddr = WMI_PDEV_PARAM_UNSUPPORTED,
    .enable_btcoex = WMI_PDEV_PARAM_UNSUPPORTED,
};

static struct wmi_pdev_param_map wmi_10_2_4_pdev_param_map = {
    .tx_chain_mask = WMI_10X_PDEV_PARAM_TX_CHAIN_MASK,
    .rx_chain_mask = WMI_10X_PDEV_PARAM_RX_CHAIN_MASK,
    .txpower_limit2g = WMI_10X_PDEV_PARAM_TXPOWER_LIMIT2G,
    .txpower_limit5g = WMI_10X_PDEV_PARAM_TXPOWER_LIMIT5G,
    .txpower_scale = WMI_10X_PDEV_PARAM_TXPOWER_SCALE,
    .beacon_gen_mode = WMI_10X_PDEV_PARAM_BEACON_GEN_MODE,
    .beacon_tx_mode = WMI_10X_PDEV_PARAM_BEACON_TX_MODE,
    .resmgr_offchan_mode = WMI_10X_PDEV_PARAM_RESMGR_OFFCHAN_MODE,
    .protection_mode = WMI_10X_PDEV_PARAM_PROTECTION_MODE,
    .dynamic_bw = WMI_10X_PDEV_PARAM_DYNAMIC_BW,
    .non_agg_sw_retry_th = WMI_10X_PDEV_PARAM_NON_AGG_SW_RETRY_TH,
    .agg_sw_retry_th = WMI_10X_PDEV_PARAM_AGG_SW_RETRY_TH,
    .sta_kickout_th = WMI_10X_PDEV_PARAM_STA_KICKOUT_TH,
    .ac_aggrsize_scaling = WMI_10X_PDEV_PARAM_AC_AGGRSIZE_SCALING,
    .ltr_enable = WMI_10X_PDEV_PARAM_LTR_ENABLE,
    .ltr_ac_latency_be = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_BE,
    .ltr_ac_latency_bk = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_BK,
    .ltr_ac_latency_vi = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_VI,
    .ltr_ac_latency_vo = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_VO,
    .ltr_ac_latency_timeout = WMI_10X_PDEV_PARAM_LTR_AC_LATENCY_TIMEOUT,
    .ltr_sleep_override = WMI_10X_PDEV_PARAM_LTR_SLEEP_OVERRIDE,
    .ltr_rx_override = WMI_10X_PDEV_PARAM_LTR_RX_OVERRIDE,
    .ltr_tx_activity_timeout = WMI_10X_PDEV_PARAM_LTR_TX_ACTIVITY_TIMEOUT,
    .l1ss_enable = WMI_10X_PDEV_PARAM_L1SS_ENABLE,
    .dsleep_enable = WMI_10X_PDEV_PARAM_DSLEEP_ENABLE,
    .pcielp_txbuf_flush = WMI_PDEV_PARAM_UNSUPPORTED,
    .pcielp_txbuf_watermark = WMI_PDEV_PARAM_UNSUPPORTED,
    .pcielp_txbuf_tmo_en = WMI_PDEV_PARAM_UNSUPPORTED,
    .pcielp_txbuf_tmo_value = WMI_PDEV_PARAM_UNSUPPORTED,
    .pdev_stats_update_period = WMI_10X_PDEV_PARAM_PDEV_STATS_UPDATE_PERIOD,
    .vdev_stats_update_period = WMI_10X_PDEV_PARAM_VDEV_STATS_UPDATE_PERIOD,
    .peer_stats_update_period = WMI_10X_PDEV_PARAM_PEER_STATS_UPDATE_PERIOD,
    .bcnflt_stats_update_period = WMI_10X_PDEV_PARAM_BCNFLT_STATS_UPDATE_PERIOD,
    .pmf_qos = WMI_10X_PDEV_PARAM_PMF_QOS,
    .arp_ac_override = WMI_10X_PDEV_PARAM_ARPDHCP_AC_OVERRIDE,
    .dcs = WMI_10X_PDEV_PARAM_DCS,
    .ani_enable = WMI_10X_PDEV_PARAM_ANI_ENABLE,
    .ani_poll_period = WMI_10X_PDEV_PARAM_ANI_POLL_PERIOD,
    .ani_listen_period = WMI_10X_PDEV_PARAM_ANI_LISTEN_PERIOD,
    .ani_ofdm_level = WMI_10X_PDEV_PARAM_ANI_OFDM_LEVEL,
    .ani_cck_level = WMI_10X_PDEV_PARAM_ANI_CCK_LEVEL,
    .dyntxchain = WMI_10X_PDEV_PARAM_DYNTXCHAIN,
    .proxy_sta = WMI_PDEV_PARAM_UNSUPPORTED,
    .idle_ps_config = WMI_PDEV_PARAM_UNSUPPORTED,
    .power_gating_sleep = WMI_PDEV_PARAM_UNSUPPORTED,
    .fast_channel_reset = WMI_10X_PDEV_PARAM_FAST_CHANNEL_RESET,
    .burst_dur = WMI_10X_PDEV_PARAM_BURST_DUR,
    .burst_enable = WMI_10X_PDEV_PARAM_BURST_ENABLE,
    .cal_period = WMI_10X_PDEV_PARAM_CAL_PERIOD,
    .aggr_burst = WMI_PDEV_PARAM_UNSUPPORTED,
    .rx_decap_mode = WMI_PDEV_PARAM_UNSUPPORTED,
    .smart_antenna_default_antenna = WMI_PDEV_PARAM_UNSUPPORTED,
    .igmpmld_override = WMI_PDEV_PARAM_UNSUPPORTED,
    .igmpmld_tid = WMI_PDEV_PARAM_UNSUPPORTED,
    .antenna_gain = WMI_PDEV_PARAM_UNSUPPORTED,
    .rx_filter = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_mcast_to_ucast_tid = WMI_PDEV_PARAM_UNSUPPORTED,
    .proxy_sta_mode = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_mcast2ucast_mode = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_mcast2ucast_buffer = WMI_PDEV_PARAM_UNSUPPORTED,
    .remove_mcast2ucast_buffer = WMI_PDEV_PARAM_UNSUPPORTED,
    .peer_sta_ps_statechg_enable = WMI_PDEV_PARAM_UNSUPPORTED,
    .igmpmld_ac_override = WMI_PDEV_PARAM_UNSUPPORTED,
    .block_interbss = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_disable_reset_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_msdu_ttl_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_ppdu_duration_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .txbf_sound_period_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_promisc_mode_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_burst_mode_cmdid = WMI_PDEV_PARAM_UNSUPPORTED,
    .en_stats = WMI_PDEV_PARAM_UNSUPPORTED,
    .mu_group_policy = WMI_PDEV_PARAM_UNSUPPORTED,
    .noise_detection = WMI_PDEV_PARAM_UNSUPPORTED,
    .noise_threshold = WMI_PDEV_PARAM_UNSUPPORTED,
    .dpd_enable = WMI_PDEV_PARAM_UNSUPPORTED,
    .set_mcast_bcast_echo = WMI_PDEV_PARAM_UNSUPPORTED,
    .atf_strict_sch = WMI_PDEV_PARAM_UNSUPPORTED,
    .atf_sched_duration = WMI_PDEV_PARAM_UNSUPPORTED,
    .ant_plzn = WMI_PDEV_PARAM_UNSUPPORTED,
    .mgmt_retry_limit = WMI_PDEV_PARAM_UNSUPPORTED,
    .sensitivity_level = WMI_PDEV_PARAM_UNSUPPORTED,
    .signed_txpower_2g = WMI_PDEV_PARAM_UNSUPPORTED,
    .signed_txpower_5g = WMI_PDEV_PARAM_UNSUPPORTED,
    .enable_per_tid_amsdu = WMI_PDEV_PARAM_UNSUPPORTED,
    .enable_per_tid_ampdu = WMI_PDEV_PARAM_UNSUPPORTED,
    .cca_threshold = WMI_PDEV_PARAM_UNSUPPORTED,
    .rts_fixed_rate = WMI_PDEV_PARAM_UNSUPPORTED,
    .pdev_reset = WMI_PDEV_PARAM_UNSUPPORTED,
    .wapi_mbssid_offset = WMI_PDEV_PARAM_UNSUPPORTED,
    .arp_srcaddr = WMI_PDEV_PARAM_UNSUPPORTED,
    .arp_dstaddr = WMI_PDEV_PARAM_UNSUPPORTED,
    .enable_btcoex = WMI_PDEV_PARAM_UNSUPPORTED,
};

/* firmware 10.2 specific mappings */
static struct wmi_cmd_map wmi_10_2_cmd_map = {
    .init_cmdid = WMI_10_2_INIT_CMDID,
    .start_scan_cmdid = WMI_10_2_START_SCAN_CMDID,
    .stop_scan_cmdid = WMI_10_2_STOP_SCAN_CMDID,
    .scan_chan_list_cmdid = WMI_10_2_SCAN_CHAN_LIST_CMDID,
    .scan_sch_prio_tbl_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_regdomain_cmdid = WMI_10_2_PDEV_SET_REGDOMAIN_CMDID,
    .pdev_set_channel_cmdid = WMI_10_2_PDEV_SET_CHANNEL_CMDID,
    .pdev_set_param_cmdid = WMI_10_2_PDEV_SET_PARAM_CMDID,
    .pdev_pktlog_enable_cmdid = WMI_10_2_PDEV_PKTLOG_ENABLE_CMDID,
    .pdev_pktlog_disable_cmdid = WMI_10_2_PDEV_PKTLOG_DISABLE_CMDID,
    .pdev_set_wmm_params_cmdid = WMI_10_2_PDEV_SET_WMM_PARAMS_CMDID,
    .pdev_set_ht_cap_ie_cmdid = WMI_10_2_PDEV_SET_HT_CAP_IE_CMDID,
    .pdev_set_vht_cap_ie_cmdid = WMI_10_2_PDEV_SET_VHT_CAP_IE_CMDID,
    .pdev_set_quiet_mode_cmdid = WMI_10_2_PDEV_SET_QUIET_MODE_CMDID,
    .pdev_green_ap_ps_enable_cmdid = WMI_10_2_PDEV_GREEN_AP_PS_ENABLE_CMDID,
    .pdev_get_tpc_config_cmdid = WMI_10_2_PDEV_GET_TPC_CONFIG_CMDID,
    .pdev_set_base_macaddr_cmdid = WMI_10_2_PDEV_SET_BASE_MACADDR_CMDID,
    .vdev_create_cmdid = WMI_10_2_VDEV_CREATE_CMDID,
    .vdev_delete_cmdid = WMI_10_2_VDEV_DELETE_CMDID,
    .vdev_start_request_cmdid = WMI_10_2_VDEV_START_REQUEST_CMDID,
    .vdev_restart_request_cmdid = WMI_10_2_VDEV_RESTART_REQUEST_CMDID,
    .vdev_up_cmdid = WMI_10_2_VDEV_UP_CMDID,
    .vdev_stop_cmdid = WMI_10_2_VDEV_STOP_CMDID,
    .vdev_down_cmdid = WMI_10_2_VDEV_DOWN_CMDID,
    .vdev_set_param_cmdid = WMI_10_2_VDEV_SET_PARAM_CMDID,
    .vdev_install_key_cmdid = WMI_10_2_VDEV_INSTALL_KEY_CMDID,
    .peer_create_cmdid = WMI_10_2_PEER_CREATE_CMDID,
    .peer_delete_cmdid = WMI_10_2_PEER_DELETE_CMDID,
    .peer_flush_tids_cmdid = WMI_10_2_PEER_FLUSH_TIDS_CMDID,
    .peer_set_param_cmdid = WMI_10_2_PEER_SET_PARAM_CMDID,
    .peer_assoc_cmdid = WMI_10_2_PEER_ASSOC_CMDID,
    .peer_add_wds_entry_cmdid = WMI_10_2_PEER_ADD_WDS_ENTRY_CMDID,
    .peer_remove_wds_entry_cmdid = WMI_10_2_PEER_REMOVE_WDS_ENTRY_CMDID,
    .peer_mcast_group_cmdid = WMI_10_2_PEER_MCAST_GROUP_CMDID,
    .bcn_tx_cmdid = WMI_10_2_BCN_TX_CMDID,
    .pdev_send_bcn_cmdid = WMI_10_2_PDEV_SEND_BCN_CMDID,
    .bcn_tmpl_cmdid = WMI_CMD_UNSUPPORTED,
    .bcn_filter_rx_cmdid = WMI_10_2_BCN_FILTER_RX_CMDID,
    .prb_req_filter_rx_cmdid = WMI_10_2_PRB_REQ_FILTER_RX_CMDID,
    .mgmt_tx_cmdid = WMI_10_2_MGMT_TX_CMDID,
    .prb_tmpl_cmdid = WMI_CMD_UNSUPPORTED,
    .addba_clear_resp_cmdid = WMI_10_2_ADDBA_CLEAR_RESP_CMDID,
    .addba_send_cmdid = WMI_10_2_ADDBA_SEND_CMDID,
    .addba_status_cmdid = WMI_10_2_ADDBA_STATUS_CMDID,
    .delba_send_cmdid = WMI_10_2_DELBA_SEND_CMDID,
    .addba_set_resp_cmdid = WMI_10_2_ADDBA_SET_RESP_CMDID,
    .send_singleamsdu_cmdid = WMI_10_2_SEND_SINGLEAMSDU_CMDID,
    .sta_powersave_mode_cmdid = WMI_10_2_STA_POWERSAVE_MODE_CMDID,
    .sta_powersave_param_cmdid = WMI_10_2_STA_POWERSAVE_PARAM_CMDID,
    .sta_mimo_ps_mode_cmdid = WMI_10_2_STA_MIMO_PS_MODE_CMDID,
    .pdev_dfs_enable_cmdid = WMI_10_2_PDEV_DFS_ENABLE_CMDID,
    .pdev_dfs_disable_cmdid = WMI_10_2_PDEV_DFS_DISABLE_CMDID,
    .roam_scan_mode = WMI_10_2_ROAM_SCAN_MODE,
    .roam_scan_rssi_threshold = WMI_10_2_ROAM_SCAN_RSSI_THRESHOLD,
    .roam_scan_period = WMI_10_2_ROAM_SCAN_PERIOD,
    .roam_scan_rssi_change_threshold = WMI_10_2_ROAM_SCAN_RSSI_CHANGE_THRESHOLD,
    .roam_ap_profile = WMI_10_2_ROAM_AP_PROFILE,
    .ofl_scan_add_ap_profile = WMI_10_2_OFL_SCAN_ADD_AP_PROFILE,
    .ofl_scan_remove_ap_profile = WMI_10_2_OFL_SCAN_REMOVE_AP_PROFILE,
    .ofl_scan_period = WMI_10_2_OFL_SCAN_PERIOD,
    .p2p_dev_set_device_info = WMI_10_2_P2P_DEV_SET_DEVICE_INFO,
    .p2p_dev_set_discoverability = WMI_10_2_P2P_DEV_SET_DISCOVERABILITY,
    .p2p_go_set_beacon_ie = WMI_10_2_P2P_GO_SET_BEACON_IE,
    .p2p_go_set_probe_resp_ie = WMI_10_2_P2P_GO_SET_PROBE_RESP_IE,
    .p2p_set_vendor_ie_data_cmdid = WMI_CMD_UNSUPPORTED,
    .ap_ps_peer_param_cmdid = WMI_10_2_AP_PS_PEER_PARAM_CMDID,
    .ap_ps_peer_uapsd_coex_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_rate_retry_sched_cmdid = WMI_10_2_PEER_RATE_RETRY_SCHED_CMDID,
    .wlan_profile_trigger_cmdid = WMI_10_2_WLAN_PROFILE_TRIGGER_CMDID,
    .wlan_profile_set_hist_intvl_cmdid = WMI_10_2_WLAN_PROFILE_SET_HIST_INTVL_CMDID,
    .wlan_profile_get_profile_data_cmdid = WMI_10_2_WLAN_PROFILE_GET_PROFILE_DATA_CMDID,
    .wlan_profile_enable_profile_id_cmdid = WMI_10_2_WLAN_PROFILE_ENABLE_PROFILE_ID_CMDID,
    .wlan_profile_list_profile_id_cmdid = WMI_10_2_WLAN_PROFILE_LIST_PROFILE_ID_CMDID,
    .pdev_suspend_cmdid = WMI_10_2_PDEV_SUSPEND_CMDID,
    .pdev_resume_cmdid = WMI_10_2_PDEV_RESUME_CMDID,
    .add_bcn_filter_cmdid = WMI_10_2_ADD_BCN_FILTER_CMDID,
    .rmv_bcn_filter_cmdid = WMI_10_2_RMV_BCN_FILTER_CMDID,
    .wow_add_wake_pattern_cmdid = WMI_10_2_WOW_ADD_WAKE_PATTERN_CMDID,
    .wow_del_wake_pattern_cmdid = WMI_10_2_WOW_DEL_WAKE_PATTERN_CMDID,
    .wow_enable_disable_wake_event_cmdid = WMI_10_2_WOW_ENABLE_DISABLE_WAKE_EVENT_CMDID,
    .wow_enable_cmdid = WMI_10_2_WOW_ENABLE_CMDID,
    .wow_hostwakeup_from_sleep_cmdid = WMI_10_2_WOW_HOSTWAKEUP_FROM_SLEEP_CMDID,
    .rtt_measreq_cmdid = WMI_10_2_RTT_MEASREQ_CMDID,
    .rtt_tsf_cmdid = WMI_10_2_RTT_TSF_CMDID,
    .vdev_spectral_scan_configure_cmdid = WMI_10_2_VDEV_SPECTRAL_SCAN_CONFIGURE_CMDID,
    .vdev_spectral_scan_enable_cmdid = WMI_10_2_VDEV_SPECTRAL_SCAN_ENABLE_CMDID,
    .request_stats_cmdid = WMI_10_2_REQUEST_STATS_CMDID,
    .set_arp_ns_offload_cmdid = WMI_CMD_UNSUPPORTED,
    .network_list_offload_config_cmdid = WMI_CMD_UNSUPPORTED,
    .gtk_offload_cmdid = WMI_CMD_UNSUPPORTED,
    .csa_offload_enable_cmdid = WMI_CMD_UNSUPPORTED,
    .csa_offload_chanswitch_cmdid = WMI_CMD_UNSUPPORTED,
    .chatter_set_mode_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_tid_addba_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_tid_delba_cmdid = WMI_CMD_UNSUPPORTED,
    .sta_dtim_ps_method_cmdid = WMI_CMD_UNSUPPORTED,
    .sta_uapsd_auto_trig_cmdid = WMI_CMD_UNSUPPORTED,
    .sta_keepalive_cmd = WMI_CMD_UNSUPPORTED,
    .echo_cmdid = WMI_10_2_ECHO_CMDID,
    .pdev_utf_cmdid = WMI_10_2_PDEV_UTF_CMDID,
    .dbglog_cfg_cmdid = WMI_10_2_DBGLOG_CFG_CMDID,
    .pdev_qvit_cmdid = WMI_10_2_PDEV_QVIT_CMDID,
    .pdev_ftm_intg_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_set_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_get_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
    .force_fw_hang_cmdid = WMI_CMD_UNSUPPORTED,
    .gpio_config_cmdid = WMI_10_2_GPIO_CONFIG_CMDID,
    .gpio_output_cmdid = WMI_10_2_GPIO_OUTPUT_CMDID,
    .pdev_get_temperature_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_enable_adaptive_cca_cmdid = WMI_CMD_UNSUPPORTED,
    .scan_update_request_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_standby_response_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_resume_response_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_add_peer_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_evict_peer_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_restore_peer_cmdid = WMI_CMD_UNSUPPORTED,
    .wlan_peer_caching_print_all_peers_info_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_update_wds_entry_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_add_proxy_sta_entry_cmdid = WMI_CMD_UNSUPPORTED,
    .rtt_keepalive_cmdid = WMI_CMD_UNSUPPORTED,
    .oem_req_cmdid = WMI_CMD_UNSUPPORTED,
    .nan_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_ratemask_cmdid = WMI_CMD_UNSUPPORTED,
    .qboost_cfg_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_smart_ant_enable_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_smart_ant_set_rx_antenna_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_smart_ant_set_tx_antenna_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_smart_ant_set_train_info_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_smart_ant_set_node_config_ops_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_antenna_switch_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_ctl_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_set_mimogain_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_ratepwr_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_ratepwr_chainmsk_table_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_fips_cmdid = WMI_CMD_UNSUPPORTED,
    .tt_set_conf_cmdid = WMI_CMD_UNSUPPORTED,
    .fwtest_cmdid = WMI_CMD_UNSUPPORTED,
    .vdev_atf_request_cmdid = WMI_CMD_UNSUPPORTED,
    .peer_atf_request_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_ani_cck_config_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_get_ani_ofdm_config_cmdid = WMI_CMD_UNSUPPORTED,
    .pdev_reserve_ast_entry_cmdid = WMI_CMD_UNSUPPORTED,
};

static struct wmi_pdev_param_map wmi_10_4_pdev_param_map = {
    .tx_chain_mask = WMI_10_4_PDEV_PARAM_TX_CHAIN_MASK,
    .rx_chain_mask = WMI_10_4_PDEV_PARAM_RX_CHAIN_MASK,
    .txpower_limit2g = WMI_10_4_PDEV_PARAM_TXPOWER_LIMIT2G,
    .txpower_limit5g = WMI_10_4_PDEV_PARAM_TXPOWER_LIMIT5G,
    .txpower_scale = WMI_10_4_PDEV_PARAM_TXPOWER_SCALE,
    .beacon_gen_mode = WMI_10_4_PDEV_PARAM_BEACON_GEN_MODE,
    .beacon_tx_mode = WMI_10_4_PDEV_PARAM_BEACON_TX_MODE,
    .resmgr_offchan_mode = WMI_10_4_PDEV_PARAM_RESMGR_OFFCHAN_MODE,
    .protection_mode = WMI_10_4_PDEV_PARAM_PROTECTION_MODE,
    .dynamic_bw = WMI_10_4_PDEV_PARAM_DYNAMIC_BW,
    .non_agg_sw_retry_th = WMI_10_4_PDEV_PARAM_NON_AGG_SW_RETRY_TH,
    .agg_sw_retry_th = WMI_10_4_PDEV_PARAM_AGG_SW_RETRY_TH,
    .sta_kickout_th = WMI_10_4_PDEV_PARAM_STA_KICKOUT_TH,
    .ac_aggrsize_scaling = WMI_10_4_PDEV_PARAM_AC_AGGRSIZE_SCALING,
    .ltr_enable = WMI_10_4_PDEV_PARAM_LTR_ENABLE,
    .ltr_ac_latency_be = WMI_10_4_PDEV_PARAM_LTR_AC_LATENCY_BE,
    .ltr_ac_latency_bk = WMI_10_4_PDEV_PARAM_LTR_AC_LATENCY_BK,
    .ltr_ac_latency_vi = WMI_10_4_PDEV_PARAM_LTR_AC_LATENCY_VI,
    .ltr_ac_latency_vo = WMI_10_4_PDEV_PARAM_LTR_AC_LATENCY_VO,
    .ltr_ac_latency_timeout = WMI_10_4_PDEV_PARAM_LTR_AC_LATENCY_TIMEOUT,
    .ltr_sleep_override = WMI_10_4_PDEV_PARAM_LTR_SLEEP_OVERRIDE,
    .ltr_rx_override = WMI_10_4_PDEV_PARAM_LTR_RX_OVERRIDE,
    .ltr_tx_activity_timeout = WMI_10_4_PDEV_PARAM_LTR_TX_ACTIVITY_TIMEOUT,
    .l1ss_enable = WMI_10_4_PDEV_PARAM_L1SS_ENABLE,
    .dsleep_enable = WMI_10_4_PDEV_PARAM_DSLEEP_ENABLE,
    .pcielp_txbuf_flush = WMI_10_4_PDEV_PARAM_PCIELP_TXBUF_FLUSH,
    .pcielp_txbuf_watermark = WMI_10_4_PDEV_PARAM_PCIELP_TXBUF_WATERMARK,
    .pcielp_txbuf_tmo_en = WMI_10_4_PDEV_PARAM_PCIELP_TXBUF_TMO_EN,
    .pcielp_txbuf_tmo_value = WMI_10_4_PDEV_PARAM_PCIELP_TXBUF_TMO_VALUE,
    .pdev_stats_update_period = WMI_10_4_PDEV_PARAM_PDEV_STATS_UPDATE_PERIOD,
    .vdev_stats_update_period = WMI_10_4_PDEV_PARAM_VDEV_STATS_UPDATE_PERIOD,
    .peer_stats_update_period = WMI_10_4_PDEV_PARAM_PEER_STATS_UPDATE_PERIOD,
    .bcnflt_stats_update_period = WMI_10_4_PDEV_PARAM_BCNFLT_STATS_UPDATE_PERIOD,
    .pmf_qos = WMI_10_4_PDEV_PARAM_PMF_QOS,
    .arp_ac_override = WMI_10_4_PDEV_PARAM_ARP_AC_OVERRIDE,
    .dcs = WMI_10_4_PDEV_PARAM_DCS,
    .ani_enable = WMI_10_4_PDEV_PARAM_ANI_ENABLE,
    .ani_poll_period = WMI_10_4_PDEV_PARAM_ANI_POLL_PERIOD,
    .ani_listen_period = WMI_10_4_PDEV_PARAM_ANI_LISTEN_PERIOD,
    .ani_ofdm_level = WMI_10_4_PDEV_PARAM_ANI_OFDM_LEVEL,
    .ani_cck_level = WMI_10_4_PDEV_PARAM_ANI_CCK_LEVEL,
    .dyntxchain = WMI_10_4_PDEV_PARAM_DYNTXCHAIN,
    .proxy_sta = WMI_10_4_PDEV_PARAM_PROXY_STA,
    .idle_ps_config = WMI_10_4_PDEV_PARAM_IDLE_PS_CONFIG,
    .power_gating_sleep = WMI_10_4_PDEV_PARAM_POWER_GATING_SLEEP,
    .fast_channel_reset = WMI_10_4_PDEV_PARAM_FAST_CHANNEL_RESET,
    .burst_dur = WMI_10_4_PDEV_PARAM_BURST_DUR,
    .burst_enable = WMI_10_4_PDEV_PARAM_BURST_ENABLE,
    .cal_period = WMI_10_4_PDEV_PARAM_CAL_PERIOD,
    .aggr_burst = WMI_10_4_PDEV_PARAM_AGGR_BURST,
    .rx_decap_mode = WMI_10_4_PDEV_PARAM_RX_DECAP_MODE,
    .smart_antenna_default_antenna = WMI_10_4_PDEV_PARAM_SMART_ANTENNA_DEFAULT_ANTENNA,
    .igmpmld_override = WMI_10_4_PDEV_PARAM_IGMPMLD_OVERRIDE,
    .igmpmld_tid = WMI_10_4_PDEV_PARAM_IGMPMLD_TID,
    .antenna_gain = WMI_10_4_PDEV_PARAM_ANTENNA_GAIN,
    .rx_filter = WMI_10_4_PDEV_PARAM_RX_FILTER,
    .set_mcast_to_ucast_tid = WMI_10_4_PDEV_SET_MCAST_TO_UCAST_TID,
    .proxy_sta_mode = WMI_10_4_PDEV_PARAM_PROXY_STA_MODE,
    .set_mcast2ucast_mode = WMI_10_4_PDEV_PARAM_SET_MCAST2UCAST_MODE,
    .set_mcast2ucast_buffer = WMI_10_4_PDEV_PARAM_SET_MCAST2UCAST_BUFFER,
    .remove_mcast2ucast_buffer = WMI_10_4_PDEV_PARAM_REMOVE_MCAST2UCAST_BUFFER,
    .peer_sta_ps_statechg_enable = WMI_10_4_PDEV_PEER_STA_PS_STATECHG_ENABLE,
    .igmpmld_ac_override = WMI_10_4_PDEV_PARAM_IGMPMLD_AC_OVERRIDE,
    .block_interbss = WMI_10_4_PDEV_PARAM_BLOCK_INTERBSS,
    .set_disable_reset_cmdid = WMI_10_4_PDEV_PARAM_SET_DISABLE_RESET_CMDID,
    .set_msdu_ttl_cmdid = WMI_10_4_PDEV_PARAM_SET_MSDU_TTL_CMDID,
    .set_ppdu_duration_cmdid = WMI_10_4_PDEV_PARAM_SET_PPDU_DURATION_CMDID,
    .txbf_sound_period_cmdid = WMI_10_4_PDEV_PARAM_TXBF_SOUND_PERIOD_CMDID,
    .set_promisc_mode_cmdid = WMI_10_4_PDEV_PARAM_SET_PROMISC_MODE_CMDID,
    .set_burst_mode_cmdid = WMI_10_4_PDEV_PARAM_SET_BURST_MODE_CMDID,
    .en_stats = WMI_10_4_PDEV_PARAM_EN_STATS,
    .mu_group_policy = WMI_10_4_PDEV_PARAM_MU_GROUP_POLICY,
    .noise_detection = WMI_10_4_PDEV_PARAM_NOISE_DETECTION,
    .noise_threshold = WMI_10_4_PDEV_PARAM_NOISE_THRESHOLD,
    .dpd_enable = WMI_10_4_PDEV_PARAM_DPD_ENABLE,
    .set_mcast_bcast_echo = WMI_10_4_PDEV_PARAM_SET_MCAST_BCAST_ECHO,
    .atf_strict_sch = WMI_10_4_PDEV_PARAM_ATF_STRICT_SCH,
    .atf_sched_duration = WMI_10_4_PDEV_PARAM_ATF_SCHED_DURATION,
    .ant_plzn = WMI_10_4_PDEV_PARAM_ANT_PLZN,
    .mgmt_retry_limit = WMI_10_4_PDEV_PARAM_MGMT_RETRY_LIMIT,
    .sensitivity_level = WMI_10_4_PDEV_PARAM_SENSITIVITY_LEVEL,
    .signed_txpower_2g = WMI_10_4_PDEV_PARAM_SIGNED_TXPOWER_2G,
    .signed_txpower_5g = WMI_10_4_PDEV_PARAM_SIGNED_TXPOWER_5G,
    .enable_per_tid_amsdu = WMI_10_4_PDEV_PARAM_ENABLE_PER_TID_AMSDU,
    .enable_per_tid_ampdu = WMI_10_4_PDEV_PARAM_ENABLE_PER_TID_AMPDU,
    .cca_threshold = WMI_10_4_PDEV_PARAM_CCA_THRESHOLD,
    .rts_fixed_rate = WMI_10_4_PDEV_PARAM_RTS_FIXED_RATE,
    .pdev_reset = WMI_10_4_PDEV_PARAM_PDEV_RESET,
    .wapi_mbssid_offset = WMI_10_4_PDEV_PARAM_WAPI_MBSSID_OFFSET,
    .arp_srcaddr = WMI_10_4_PDEV_PARAM_ARP_SRCADDR,
    .arp_dstaddr = WMI_10_4_PDEV_PARAM_ARP_DSTADDR,
    .enable_btcoex = WMI_10_4_PDEV_PARAM_ENABLE_BTCOEX,
};

static const struct wmi_peer_flags_map wmi_peer_flags_map = {
    .auth = WMI_PEER_AUTH,
    .qos = WMI_PEER_QOS,
    .need_ptk_4_way = WMI_PEER_NEED_PTK_4_WAY,
    .need_gtk_2_way = WMI_PEER_NEED_GTK_2_WAY,
    .apsd = WMI_PEER_APSD,
    .ht = WMI_PEER_HT,
    .bw40 = WMI_PEER_40MHZ,
    .stbc = WMI_PEER_STBC,
    .ldbc = WMI_PEER_LDPC,
    .dyn_mimops = WMI_PEER_DYN_MIMOPS,
    .static_mimops = WMI_PEER_STATIC_MIMOPS,
    .spatial_mux = WMI_PEER_SPATIAL_MUX,
    .vht = WMI_PEER_VHT,
    .bw80 = WMI_PEER_80MHZ,
    .vht_2g = WMI_PEER_VHT_2G,
    .pmf = WMI_PEER_PMF,
    .bw160 = WMI_PEER_160MHZ,
};

static const struct wmi_peer_flags_map wmi_10x_peer_flags_map = {
    .auth = WMI_10X_PEER_AUTH,
    .qos = WMI_10X_PEER_QOS,
    .need_ptk_4_way = WMI_10X_PEER_NEED_PTK_4_WAY,
    .need_gtk_2_way = WMI_10X_PEER_NEED_GTK_2_WAY,
    .apsd = WMI_10X_PEER_APSD,
    .ht = WMI_10X_PEER_HT,
    .bw40 = WMI_10X_PEER_40MHZ,
    .stbc = WMI_10X_PEER_STBC,
    .ldbc = WMI_10X_PEER_LDPC,
    .dyn_mimops = WMI_10X_PEER_DYN_MIMOPS,
    .static_mimops = WMI_10X_PEER_STATIC_MIMOPS,
    .spatial_mux = WMI_10X_PEER_SPATIAL_MUX,
    .vht = WMI_10X_PEER_VHT,
    .bw80 = WMI_10X_PEER_80MHZ,
    .bw160 = WMI_10X_PEER_160MHZ,
};

static const struct wmi_peer_flags_map wmi_10_2_peer_flags_map = {
    .auth = WMI_10_2_PEER_AUTH,
    .qos = WMI_10_2_PEER_QOS,
    .need_ptk_4_way = WMI_10_2_PEER_NEED_PTK_4_WAY,
    .need_gtk_2_way = WMI_10_2_PEER_NEED_GTK_2_WAY,
    .apsd = WMI_10_2_PEER_APSD,
    .ht = WMI_10_2_PEER_HT,
    .bw40 = WMI_10_2_PEER_40MHZ,
    .stbc = WMI_10_2_PEER_STBC,
    .ldbc = WMI_10_2_PEER_LDPC,
    .dyn_mimops = WMI_10_2_PEER_DYN_MIMOPS,
    .static_mimops = WMI_10_2_PEER_STATIC_MIMOPS,
    .spatial_mux = WMI_10_2_PEER_SPATIAL_MUX,
    .vht = WMI_10_2_PEER_VHT,
    .bw80 = WMI_10_2_PEER_80MHZ,
    .vht_2g = WMI_10_2_PEER_VHT_2G,
    .pmf = WMI_10_2_PEER_PMF,
    .bw160 = WMI_10_2_PEER_160MHZ,
};

void ath10k_wmi_put_wmi_channel(struct wmi_channel* ch, const struct wmi_channel_arg* arg) {
    uint32_t flags = 0;

    memset(ch, 0, sizeof(*ch));

    if (arg->passive) { flags |= WMI_CHAN_FLAG_PASSIVE; }
    if (arg->allow_ibss) { flags |= WMI_CHAN_FLAG_ADHOC_ALLOWED; }
    if (arg->allow_ht) { flags |= WMI_CHAN_FLAG_ALLOW_HT; }
    if (arg->allow_vht) { flags |= WMI_CHAN_FLAG_ALLOW_VHT; }
    if (arg->ht40plus) { flags |= WMI_CHAN_FLAG_HT40_PLUS; }
    if (arg->chan_radar) { flags |= WMI_CHAN_FLAG_DFS; }

    ch->mhz = arg->freq;
    ch->band_center_freq1 = arg->band_center_freq1;
    if (arg->mode == MODE_11AC_VHT80_80) {
        ch->band_center_freq2 = arg->band_center_freq2;
    } else {
        ch->band_center_freq2 = 0;
    }
    ch->min_power = arg->min_power;
    ch->max_power = arg->max_power;
    ch->reg_power = arg->max_reg_power;
    ch->antenna_max = arg->max_antenna_gain;
    ch->max_tx_power = arg->max_power;

    /* mode & flags share storage */
    ch->mode = arg->mode;
    ch->flags |= flags;
}

zx_status_t ath10k_wmi_wait_for_service_ready(struct ath10k* ar) {
    if (sync_completion_wait(&ar->wmi.service_ready, WMI_SERVICE_READY_TIMEOUT) ==
        ZX_ERR_TIMED_OUT) {
        return ZX_ERR_TIMED_OUT;
    }
    return ZX_OK;
}

zx_status_t ath10k_wmi_wait_for_unified_ready(struct ath10k* ar) {
    if (sync_completion_wait(&ar->wmi.unified_ready, WMI_UNIFIED_READY_TIMEOUT) ==
        ZX_ERR_TIMED_OUT) {
        return ZX_ERR_TIMED_OUT;
    }
    return ZX_OK;
}

static void ath10k_wmi_htc_tx_complete(struct ath10k* ar, struct ath10k_msg_buf* buf) {
    ath10k_msg_buf_free(buf);
}

zx_status_t ath10k_wmi_cmd_send_nowait(struct ath10k* ar, struct ath10k_msg_buf* buf,
                                       uint32_t cmd_id) {
    struct wmi_cmd_hdr* cmd_hdr;
    uint32_t cmd = 0;

    cmd |= SM(cmd_id, WMI_CMD_HDR_CMD_ID);

    cmd_hdr = ath10k_msg_buf_get_header(buf, ATH10K_MSG_TYPE_WMI);
    cmd_hdr->cmd_id = cmd;

    return ath10k_htc_send(&ar->htc, ar->wmi.eid, buf);
}

#if 0   // NEEDS PORTING
static void ath10k_wmi_tx_beacon_nowait(struct ath10k_vif* arvif) {
    struct ath10k* ar = arvif->ar;
    struct ath10k_skb_cb* cb;
    struct sk_buff* bcn;
    bool dtim_zero;
    bool deliver_cab;
    int ret;

    mtx_lock(&ar->data_lock);

    bcn = arvif->beacon;

    if (!bcn) {
        goto unlock;
    }

    cb = ATH10K_SKB_CB(bcn);

    switch (arvif->beacon_state) {
    case ATH10K_BEACON_SENDING:
    case ATH10K_BEACON_SENT:
        break;
    case ATH10K_BEACON_SCHEDULED:
        arvif->beacon_state = ATH10K_BEACON_SENDING;
        mtx_unlock(&ar->data_lock);

        dtim_zero = !!(cb->flags & ATH10K_SKB_F_DTIM_ZERO);
        deliver_cab = !!(cb->flags & ATH10K_SKB_F_DELIVER_CAB);
        ret = ath10k_wmi_beacon_send_ref_nowait(arvif->ar,
                                                arvif->vdev_id,
                                                bcn->data, bcn->len,
                                                cb->paddr,
                                                dtim_zero,
                                                deliver_cab);

        mtx_lock(&ar->data_lock);

        if (ret == 0) {
            arvif->beacon_state = ATH10K_BEACON_SENT;
        } else {
            arvif->beacon_state = ATH10K_BEACON_SCHEDULED;
        }
    }

unlock:
    mtx_unlock(&ar->data_lock);
}

static void ath10k_wmi_tx_beacons_iter(void* data, uint8_t* mac,
                                       struct ieee80211_vif* vif) {
    struct ath10k_vif* arvif = (void*)vif->drv_priv;

    ath10k_wmi_tx_beacon_nowait(arvif);
}
#endif  // NEEDS PORTING

static void ath10k_wmi_tx_beacons_nowait(struct ath10k* ar) {
#if 0   // NEEDS PORTING
    ieee80211_iterate_active_interfaces_atomic(ar->hw,
            IEEE80211_IFACE_ITER_NORMAL,
            ath10k_wmi_tx_beacons_iter,
            NULL);
#endif  // NEEDS PORTING
}

static void ath10k_wmi_op_ep_tx_credits(struct ath10k* ar) {
    /* try to send pending beacons first. they take priority */
    ath10k_wmi_tx_beacons_nowait(ar);

    zx_status_t status = zx_object_signal(ar->wmi.tx_credits_event, 0, WMI_TX_CREDITS_AVAILABLE);
    if (status != ZX_OK) {
        ZX_PANIC("unable to signal availability of tx credits: %s\n", zx_status_get_string(status));
    }
}

zx_status_t ath10k_wmi_cmd_send(struct ath10k* ar, struct ath10k_msg_buf* buf, uint32_t cmd_id) {
    zx_status_t ret = ZX_ERR_NOT_SUPPORTED;

    if (cmd_id == WMI_CMD_UNSUPPORTED) {
        ath10k_warn("wmi command %d is not supported by firmware\n", cmd_id);
        return ret;
    }

    zx_time_t send_timeout = zx_deadline_after(ZX_SEC(3));

    do {
        /* try to send pending beacons first. they take priority */
        ath10k_wmi_tx_beacons_nowait(ar);

        // Clear our tx_credits_event signal
        ret = zx_object_signal(ar->wmi.tx_credits_event, WMI_TX_CREDITS_AVAILABLE, 0);
        if (ret != ZX_OK) {
            ZX_PANIC("failed to signal availability of tx credits: %s\n",
                     zx_status_get_string(ret));
        }

        ret = ath10k_wmi_cmd_send_nowait(ar, buf, cmd_id);

        if (ret == ZX_OK) { break; }

        if (BITARR_TEST(ar->dev_flags, ATH10K_FLAG_CRASH_FLUSH)) {
            ret = ZX_ERR_IO_NOT_PRESENT;
            break;
        }

        if (ret != ZX_ERR_SHOULD_WAIT) { break; }

        ret = zx_object_wait_one(ar->wmi.tx_credits_event, WMI_TX_CREDITS_AVAILABLE, send_timeout,
                                 NULL);
    } while (ret == ZX_OK);

    if (ret != ZX_OK) {
        ath10k_info("failed to send wmi command: %s\n", zx_status_get_string(ret));
        ath10k_msg_buf_free(buf);
    }

    return ret;
}

#if 0   // NEEDS PORTING
static struct sk_buff*
ath10k_wmi_op_gen_mgmt_tx(struct ath10k* ar, struct sk_buff* msdu) {
    struct ath10k_skb_cb* cb = ATH10K_SKB_CB(msdu);
    struct ath10k_vif* arvif;
    struct wmi_mgmt_tx_cmd* cmd;
    struct ieee80211_hdr* hdr;
    struct sk_buff* skb;
    int len;
    uint32_t vdev_id;
    uint32_t buf_len = msdu->len;
    uint16_t fc;

    hdr = (struct ieee80211_hdr*)msdu->data;
    fc = hdr->frame_control;

    if (cb->vif) {
        arvif = (void*)cb->vif->drv_priv;
        vdev_id = arvif->vdev_id;
    } else {
        vdev_id = 0;
    }

    if (COND_WARN_ONCE(!ieee80211_is_mgmt(hdr->frame_control))) {
        return ERR_PTR(-EINVAL);
    }

    len = sizeof(cmd->hdr) + msdu->len;

    if ((ieee80211_is_action(hdr->frame_control) ||
            ieee80211_is_deauth(hdr->frame_control) ||
            ieee80211_is_disassoc(hdr->frame_control)) &&
            ieee80211_has_protected(hdr->frame_control)) {
        len += IEEE80211_CCMP_MIC_LEN;
        buf_len += IEEE80211_CCMP_MIC_LEN;
    }

    len = ROUNDUP(len, 4);

    skb = ath10k_wmi_alloc_skb(ar, len);
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_mgmt_tx_cmd*)skb->data;

    cmd->hdr.vdev_id = vdev_id;
    cmd->hdr.tx_rate = 0;
    cmd->hdr.tx_power = 0;
    cmd->hdr.buf_len = buf_len;

    memcpy(cmd->hdr.peer_macaddr.addr, ieee80211_get_DA(hdr), ETH_ALEN);
    memcpy(cmd->buf, msdu->data, msdu->len);

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi mgmt tx skb %pK len %d ftype %02x stype %02x\n",
               msdu, skb->len, fc & IEEE80211_FCTL_FTYPE,
               fc & IEEE80211_FCTL_STYPE);
    trace_ath10k_tx_hdr(ar, skb->data, skb->len);
    trace_ath10k_tx_payload(ar, skb->data, skb->len);

    return skb;
}
#endif  // NEEDS PORTING

static void ath10k_wmi_event_scan_started(struct ath10k* ar) {
    ASSERT_MTX_HELD(&ar->data_lock);

    switch (ar->scan.state) {
    case ATH10K_SCAN_IDLE:
    case ATH10K_SCAN_RUNNING:
    case ATH10K_SCAN_ABORTING:
        ath10k_warn("received scan started event in an invalid scan state: %s (%d)\n",
                    ath10k_scan_state_str(ar->scan.state), ar->scan.state);
        break;
    case ATH10K_SCAN_STARTING:
        ar->scan.state = ATH10K_SCAN_RUNNING;

#if 0   // NEEDS PORTING
        if (ar->scan.is_roc) {
            ieee80211_ready_on_channel(ar->hw);
        }
#endif  // NEEDS PORTING

        sync_completion_signal(&ar->scan.started);
        break;
    }
}

static void ath10k_wmi_event_scan_start_failed(struct ath10k* ar) {
    ASSERT_MTX_HELD(&ar->data_lock);

    switch (ar->scan.state) {
    case ATH10K_SCAN_IDLE:
    case ATH10K_SCAN_RUNNING:
    case ATH10K_SCAN_ABORTING:
        ath10k_warn("received scan start failed event in an invalid scan state: %s (%d)\n",
                    ath10k_scan_state_str(ar->scan.state), ar->scan.state);
        break;
    case ATH10K_SCAN_STARTING:
        sync_completion_signal(&ar->scan.started);
        __ath10k_scan_finish(ar);
        break;
    }
}

static void ath10k_wmi_event_scan_completed(struct ath10k* ar) {
    ASSERT_MTX_HELD(&ar->data_lock);

    switch (ar->scan.state) {
    case ATH10K_SCAN_IDLE:
    case ATH10K_SCAN_STARTING:
        /* One suspected reason scan can be completed while starting is
         * if firmware fails to deliver all scan events to the host,
         * e.g. when transport pipe is full. This has been observed
         * with spectral scan phyerr events starving wmi transport
         * pipe. In such case the "scan completed" event should be (and
         * is) ignored by the host as it may be just firmware's scan
         * state machine recovering.
         */
        ath10k_warn("received scan completed event in an invalid scan state: %s (%d)\n",
                    ath10k_scan_state_str(ar->scan.state), ar->scan.state);
        break;
    case ATH10K_SCAN_RUNNING:
    case ATH10K_SCAN_ABORTING:
        __ath10k_scan_finish(ar);
        break;
    }
}

static void ath10k_wmi_event_scan_bss_chan(struct ath10k* ar) {
    ASSERT_MTX_HELD(&ar->data_lock);

    switch (ar->scan.state) {
    case ATH10K_SCAN_IDLE:
    case ATH10K_SCAN_STARTING:
        ath10k_warn("received scan bss chan event in an invalid scan state: %s (%d)\n",
                    ath10k_scan_state_str(ar->scan.state), ar->scan.state);
        break;
    case ATH10K_SCAN_RUNNING:
    case ATH10K_SCAN_ABORTING:
        break;
    }
}

static void ath10k_wmi_event_scan_foreign_chan(struct ath10k* ar, uint32_t freq) {
    ASSERT_MTX_HELD(&ar->data_lock);

    switch (ar->scan.state) {
    case ATH10K_SCAN_IDLE:
    case ATH10K_SCAN_STARTING:
        ath10k_warn("received scan foreign chan event in an invalid scan state: %s (%d)\n",
                    ath10k_scan_state_str(ar->scan.state), ar->scan.state);
        break;
    case ATH10K_SCAN_RUNNING:
    case ATH10K_SCAN_ABORTING:
        if (ar->scan.is_roc && ar->scan.roc_freq == (int)freq) {
            sync_completion_signal(&ar->scan.on_channel);
        }
        break;
    }
}

static const char* ath10k_wmi_event_scan_type_str(enum wmi_scan_event_type type,
                                                  enum wmi_scan_completion_reason reason) {
    switch (type) {
    case WMI_SCAN_EVENT_STARTED:
        return "started";
    case WMI_SCAN_EVENT_COMPLETED:
        switch (reason) {
        case WMI_SCAN_REASON_COMPLETED:
            return "completed";
        case WMI_SCAN_REASON_CANCELLED:
            return "completed [cancelled]";
        case WMI_SCAN_REASON_PREEMPTED:
            return "completed [preempted]";
        case WMI_SCAN_REASON_TIMEDOUT:
            return "completed [timedout]";
        case WMI_SCAN_REASON_INTERNAL_FAILURE:
            return "completed [internal err]";
        case WMI_SCAN_REASON_MAX:
            break;
        }
        return "completed [unknown]";
    case WMI_SCAN_EVENT_BSS_CHANNEL:
        return "bss channel";
    case WMI_SCAN_EVENT_FOREIGN_CHANNEL:
        return "foreign channel";
    case WMI_SCAN_EVENT_DEQUEUED:
        return "dequeued";
    case WMI_SCAN_EVENT_PREEMPTED:
        return "preempted";
    case WMI_SCAN_EVENT_START_FAILED:
        return "start failed";
    case WMI_SCAN_EVENT_RESTARTED:
        return "restarted";
    case WMI_SCAN_EVENT_FOREIGN_CHANNEL_EXIT:
        return "foreign channel exit";
    default:
        return "unknown";
    }
}

static zx_status_t ath10k_wmi_op_pull_scan_ev(struct ath10k* ar, struct ath10k_msg_buf* msg_buf,
                                              struct wmi_scan_ev_arg* arg) {
    struct wmi_scan_event* ev;

    if (ath10k_msg_buf_get_payload_len(msg_buf, ATH10K_MSG_TYPE_WMI) < sizeof(*ev)) {
        return ZX_ERR_INVALID_ARGS;
    }

    ev = ath10k_msg_buf_get_payload(msg_buf);

    arg->event_type = ev->event_type;
    arg->reason = ev->reason;
    arg->channel_freq = ev->channel_freq;
    arg->scan_req_id = ev->scan_req_id;
    arg->scan_id = ev->scan_id;
    arg->vdev_id = ev->vdev_id;

    return ZX_OK;
}

int ath10k_wmi_event_scan(struct ath10k* ar, struct ath10k_msg_buf* buf) {
    struct wmi_scan_ev_arg arg = {};
    enum wmi_scan_event_type event_type;
    enum wmi_scan_completion_reason reason;
    uint32_t freq;
    uint32_t req_id;
    uint32_t scan_id;
    uint32_t vdev_id;
    int ret;

    ret = ath10k_wmi_pull_scan(ar, buf, &arg);
    if (ret != ZX_OK) {
        ath10k_warn("failed to parse scan event: %s\n", zx_status_get_string(ret));
        return ret;
    }

    event_type = arg.event_type;
    reason = arg.reason;
    freq = arg.channel_freq;
    req_id = arg.scan_req_id;
    scan_id = arg.scan_id;
    vdev_id = arg.vdev_id;

    mtx_lock(&ar->data_lock);

    ath10k_dbg(
        ar, ATH10K_DBG_WMI,
        "scan event %s type %d reason %d freq %d req_id %d scan_id %d vdev_id %d state %s (%d)\n",
        ath10k_wmi_event_scan_type_str(event_type, reason), event_type, reason, freq, req_id,
        scan_id, vdev_id, ath10k_scan_state_str(ar->scan.state), ar->scan.state);

    switch (event_type) {
    case WMI_SCAN_EVENT_STARTED:
        ath10k_wmi_event_scan_started(ar);
        break;
    case WMI_SCAN_EVENT_COMPLETED:
        ath10k_wmi_event_scan_completed(ar);
        break;
    case WMI_SCAN_EVENT_BSS_CHANNEL:
        ath10k_wmi_event_scan_bss_chan(ar);
        break;
    case WMI_SCAN_EVENT_FOREIGN_CHANNEL:
        ath10k_wmi_event_scan_foreign_chan(ar, freq);
        break;
    case WMI_SCAN_EVENT_START_FAILED:
        ath10k_warn("received scan start failure event\n");
        ath10k_wmi_event_scan_start_failed(ar);
        break;
    case WMI_SCAN_EVENT_DEQUEUED:
    case WMI_SCAN_EVENT_PREEMPTED:
    case WMI_SCAN_EVENT_RESTARTED:
    case WMI_SCAN_EVENT_FOREIGN_CHANNEL_EXIT:
    default:
        break;
    }

    mtx_unlock(&ar->data_lock);
    return 0;
}

#if 0   // NEEDS PORTING
/* If keys are configured, HW decrypts all frames
 * with protected bit set. Mark such frames as decrypted.
 */
static void ath10k_wmi_handle_wep_reauth(struct ath10k* ar,
        struct sk_buff* skb,
        struct ieee80211_rx_status* status) {
    struct ieee80211_hdr* hdr = (struct ieee80211_hdr*)skb->data;
    unsigned int hdrlen;
    bool peer_key;
    uint8_t* addr, keyidx;

    if (!ieee80211_is_auth(hdr->frame_control) ||
            !ieee80211_has_protected(hdr->frame_control)) {
        return;
    }

    hdrlen = ieee80211_hdrlen(hdr->frame_control);
    if (skb->len < (hdrlen + IEEE80211_WEP_IV_LEN)) {
        return;
    }

    keyidx = skb->data[hdrlen + (IEEE80211_WEP_IV_LEN - 1)] >> WEP_KEYID_SHIFT;
    addr = ieee80211_get_SA(hdr);

    mtx_lock(&ar->data_lock);
    peer_key = ath10k_mac_is_peer_wep_key_set(ar, addr, keyidx);
    mtx_unlock(&ar->data_lock);

    if (peer_key) {
        ath10k_dbg(ar, ATH10K_DBG_MAC,
                   "mac wep key present for peer %pM\n", addr);
        status->flag |= RX_FLAG_DECRYPTED;
    }
}
#endif  // NEEDS PORTING

static zx_status_t ath10k_wmi_op_pull_mgmt_rx_ev(struct ath10k* ar, struct ath10k_msg_buf* msg_buf,
                                                 struct wmi_mgmt_rx_ev_arg* arg) {
    struct wmi_mgmt_rx_event_v1* ev_v1;
    struct wmi_mgmt_rx_event_v2* ev_v2;
    struct wmi_mgmt_rx_hdr_v1* ev_hdr;
    struct wmi_mgmt_rx_ext_info* ext_info;
    size_t ev_len;
    uint32_t msdu_len;
    uint32_t len;
    void* payload = ath10k_msg_buf_get_payload(msg_buf);

    if (BITARR_TEST(ar->running_fw->fw_file.fw_features, ATH10K_FW_FEATURE_EXT_WMI_MGMT_RX)) {
        ev_v2 = payload;
        ev_hdr = &ev_v2->hdr.v1;
        ev_len = sizeof(*ev_v2);
    } else {
        ev_v1 = payload;
        ev_hdr = &ev_v1->hdr;
        ev_len = sizeof(*ev_v1);
    }

    if (ath10k_msg_buf_get_payload_len(msg_buf, ATH10K_MSG_TYPE_WMI) < ev_len) {
        return ZX_ERR_INVALID_ARGS;
    }

    arg->channel = ev_hdr->channel;
    arg->buf_len = ev_hdr->buf_len;
    arg->status = ev_hdr->status;
    arg->snr = ev_hdr->snr;
    arg->phy_mode = ev_hdr->phy_mode;
    arg->rate = ev_hdr->rate;

    msg_buf->rx.frame_offset = ath10k_msg_buf_get_payload_offset(ATH10K_MSG_TYPE_WMI) + ev_len;

    msdu_len = arg->buf_len;
    if (msg_buf->used - (ath10k_msg_buf_get_payload_offset(ATH10K_MSG_TYPE_WMI) + ev_len) <
        msdu_len) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (arg->status & WMI_RX_STATUS_EXT_INFO) {
        len = ALIGN(arg->buf_len, 4);
        ext_info = (struct wmi_mgmt_rx_ext_info*)((uint8_t*)payload + len);
        memcpy(&arg->ext_info, ext_info, sizeof(struct wmi_mgmt_rx_ext_info));
    }

    return ZX_OK;
}

#if 0   // NEEDS PORTING
static int ath10k_wmi_10_4_op_pull_mgmt_rx_ev(struct ath10k* ar,
        struct sk_buff* skb,
        struct wmi_mgmt_rx_ev_arg* arg) {
    struct wmi_10_4_mgmt_rx_event* ev;
    struct wmi_10_4_mgmt_rx_hdr* ev_hdr;
    size_t pull_len;
    uint32_t msdu_len;
    struct wmi_mgmt_rx_ext_info* ext_info;
    uint32_t len;

    ev = (struct wmi_10_4_mgmt_rx_event*)skb->data;
    ev_hdr = &ev->hdr;
    pull_len = sizeof(*ev);

    if (skb->len < pull_len) {
        return -EPROTO;
    }

    skb_pull(skb, pull_len);
    arg->channel = ev_hdr->channel;
    arg->buf_len = ev_hdr->buf_len;
    arg->status = ev_hdr->status;
    arg->snr = ev_hdr->snr;
    arg->phy_mode = ev_hdr->phy_mode;
    arg->rate = ev_hdr->rate;

    msdu_len = arg->buf_len;
    if (skb->len < msdu_len) {
        return -EPROTO;
    }

    if (arg->status & WMI_RX_STATUS_EXT_INFO) {
        len = ALIGN(arg->buf_len, 4);
        ext_info = (struct wmi_mgmt_rx_ext_info*)(skb->data + len);
        memcpy(&arg->ext_info, ext_info,
               sizeof(struct wmi_mgmt_rx_ext_info));
    }

    /* Make sure bytes added for padding are removed. */
    skb_trim(skb, msdu_len);

    return 0;
}

static bool ath10k_wmi_rx_is_decrypted(struct ath10k* ar,
                                       struct ieee80211_hdr* hdr) {
    if (!ieee80211_has_protected(hdr->frame_control)) {
        return false;
    }

    /* FW delivers WEP Shared Auth frame with Protected Bit set and
     * encrypted payload. However in case of PMF it delivers decrypted
     * frames with Protected Bit set.
     */
    if (ieee80211_is_auth(hdr->frame_control)) {
        return false;
    }

    /* qca99x0 based FW delivers broadcast or multicast management frames
     * (ex: group privacy action frames in mesh) as encrypted payload.
     */
    if (is_multicast_ether_addr(ieee80211_get_DA(hdr)) &&
            ar->hw_params.sw_decrypt_mcast_mgmt) {
        return false;
    }

    return true;
}
#endif  // NEEDS PORTING

zx_status_t ath10k_wmi_event_mgmt_rx(struct ath10k* ar, struct ath10k_msg_buf* buf) {
    struct wmi_mgmt_rx_ev_arg arg = {};
    uint32_t rx_status;
    bool free_buf = true;

    zx_status_t ret = ath10k_wmi_pull_mgmt_rx(ar, buf, &arg);
    if (ret != ZX_OK) {
        ath10k_warn("failed to parse mgmt rx event: %s\n", zx_status_get_string(ret));
        goto maybe_free_buf;
    }

    wlan_rx_info_t rx_info = {};

    rx_info.valid_fields |= WLAN_RX_INFO_VALID_PHY;
    rx_info.phy = arg.phy_mode;

    rx_info.valid_fields |= WLAN_RX_INFO_VALID_DATA_RATE;
    rx_info.data_rate = arg.rate;

    rx_info.valid_fields |= WLAN_RX_INFO_VALID_SNR;
    rx_info.snr_dbh = arg.snr;

    rx_info.chan.primary = arg.channel;
    rx_info.chan.cbw = CBW20;
    rx_info.chan.secondary80 = 0;

    rx_status = arg.status;

    if ((BITARR_TEST(ar->dev_flags, ATH10K_CAC_RUNNING)) ||
        (rx_status &
         (WMI_RX_STATUS_ERR_DECRYPT | WMI_RX_STATUS_ERR_KEY_CACHE_MISS | WMI_RX_STATUS_ERR_CRC))) {
        goto maybe_free_buf;
    }

#if 0   // NEEDS PORTING
    if (rx_status & WMI_RX_STATUS_ERR_MIC) {
        status->flag |= RX_FLAG_MMIC_ERROR;
    }

    if (rx_status & WMI_RX_STATUS_EXT_INFO) {
        status->mactime =
            arg.ext_info.rx_mac_timestamp;
        status->flag |= RX_FLAG_MACTIME_END;
    }
    /* Hardware can Rx CCK rates on 5GHz. In that case phy_mode is set to
     * MODE_11B. This means phy_mode is not a reliable source for the band
     * of mgmt rx.
     */
    if (channel >= 1 && channel <= 14) {
        status->band = NL80211_BAND_2GHZ;
    } else if (channel >= 36 && channel <= 169) {
        status->band = NL80211_BAND_5GHZ;
    } else {
        /* Shouldn't happen unless list of advertised channels to
         * mac80211 has been changed.
         */
        WARN_ONCE();
        dev_kfree_skb(skb);
        return 0;
    }

    if (phy_mode == MODE_11B && status->band == NL80211_BAND_5GHZ) {
        ath10k_dbg(ar, ATH10K_DBG_MGMT, "wmi mgmt rx 11b (CCK) on 5GHz\n");
    }

    sband = &ar->mac.sbands[status->band];

    status->freq = ieee80211_channel_to_frequency(channel, status->band);
    status->signal = snr + ATH10K_DEFAULT_NOISE_FLOOR;
    status->rate_idx = ath10k_mac_bitrate_to_idx(sband, rate / 100);
#endif  // NEEDS PORTING

    struct ieee80211_frame_header* hdr = ath10k_msg_buf_get_payload(buf) + buf->rx.frame_offset;
    uint16_t fc = hdr->frame_ctrl;

#if 0   // NEEDS PORTING
    /* Firmware is guaranteed to report all essential management frames via
     * WMI while it can deliver some extra via HTT. Since there can be
     * duplicates split the reporting wrt monitor/sniffing.
     */
    status->flag |= RX_FLAG_SKIP_MONITOR;

    ath10k_wmi_handle_wep_reauth(ar, skb, status);

    if (ath10k_wmi_rx_is_decrypted(ar, hdr)) {
        status->flag |= RX_FLAG_DECRYPTED;

        if (!ieee80211_is_action(hdr->frame_control) &&
                !ieee80211_is_deauth(hdr->frame_control) &&
                !ieee80211_is_disassoc(hdr->frame_control)) {
            status->flag |= RX_FLAG_IV_STRIPPED |
                            RX_FLAG_MMIC_STRIPPED;
            hdr->frame_control = fc & ~IEEE80211_FCTL_PROTECTED;
        }
    }

    if (ieee80211_is_beacon(hdr->frame_control)) {
        ath10k_mac_handle_beacon(ar, skb);
    }
#endif  // NEEDS PORTING

    ath10k_dbg(ar, ATH10K_DBG_MGMT, "event mgmt rx buf %pK len %d ftype %02x stype %02x\n", buf,
               buf->used, fc & IEEE80211_FRAME_TYPE_MASK, fc & IEEE80211_FRAME_SUBTYPE_MASK);

    void* frame_data = ath10k_msg_buf_get_payload(buf) + buf->rx.frame_offset;

    // There's no wlan event for assocation, so we have to look for the association
    // response ourselves and send the associate command to the firmware.
    // TODO: NET-821
    if ((ieee80211_get_frame_type(hdr) == IEEE80211_FRAME_TYPE_MGMT) &&
        ieee80211_get_frame_subtype(hdr) == IEEE80211_FRAME_SUBTYPE_ASSOC_RESP) {
        mtx_lock(&ar->assoc_lock);
        // assoc_frame is reset to NULL by ath10k_mac_bss_assoc when it has finished
        // processing an association response.
        if (ar->assoc_frame == NULL) {
            ar->assoc_frame = buf;
            // This path is critical. If we don't tell the firmware to associate before
            // the first key packet is received, it will gladly deauthenticate us.
            sync_completion_signal(&ar->assoc_complete);

            // mac_bss_assoc owns the buffer now
            free_buf = false;
        } else {
            // In general the lock should prevent us from getting here, but if by some
            // chance we get back-to-back packets and no progress has been made on the
            // assoc thread, drop this one.
            mtx_unlock(&ar->assoc_lock);
            goto maybe_free_buf;
        }
        mtx_unlock(&ar->assoc_lock);
    }

    ar->wlanmac.ifc->recv(ar->wlanmac.cookie, 0, frame_data, arg.buf_len, &rx_info);

maybe_free_buf:
    if (free_buf) { ath10k_msg_buf_free(buf); }
    return ret;
}

#if 0   // NEEDS PORTING
static int freq_to_idx(struct ath10k* ar, int freq) {
    struct ieee80211_supported_band* sband;
    int band, ch, idx = 0;

    for (band = NL80211_BAND_2GHZ; band < NUM_NL80211_BANDS; band++) {
        sband = ar->hw->wiphy->bands[band];
        if (!sband) {
            continue;
        }

        for (ch = 0; ch < sband->n_channels; ch++, idx++)
            if (sband->channels[ch].center_freq == freq) {
                goto exit;
            }
    }

exit:
    return idx;
}

static int ath10k_wmi_op_pull_ch_info_ev(struct ath10k* ar, struct sk_buff* skb,
        struct wmi_ch_info_ev_arg* arg) {
    struct wmi_chan_info_event* ev = (void*)skb->data;

    if (skb->len < sizeof(*ev)) {
        return -EPROTO;
    }

    skb_pull(skb, sizeof(*ev));
    arg->err_code = ev->err_code;
    arg->freq = ev->freq;
    arg->cmd_flags = ev->cmd_flags;
    arg->noise_floor = ev->noise_floor;
    arg->rx_clear_count = ev->rx_clear_count;
    arg->cycle_count = ev->cycle_count;

    return 0;
}

static int ath10k_wmi_10_4_op_pull_ch_info_ev(struct ath10k* ar,
        struct sk_buff* skb,
        struct wmi_ch_info_ev_arg* arg) {
    struct wmi_10_4_chan_info_event* ev = (void*)skb->data;

    if (skb->len < sizeof(*ev)) {
        return -EPROTO;
    }

    skb_pull(skb, sizeof(*ev));
    arg->err_code = ev->err_code;
    arg->freq = ev->freq;
    arg->cmd_flags = ev->cmd_flags;
    arg->noise_floor = ev->noise_floor;
    arg->rx_clear_count = ev->rx_clear_count;
    arg->cycle_count = ev->cycle_count;
    arg->chan_tx_pwr_range = ev->chan_tx_pwr_range;
    arg->chan_tx_pwr_tp = ev->chan_tx_pwr_tp;
    arg->rx_frame_count = ev->rx_frame_count;

    return 0;
}
#endif  // NEEDS PORTING

void ath10k_wmi_event_chan_info(struct ath10k* ar, struct ath10k_msg_buf* buf) {
#if 0   // NEEDS PORTING
    struct wmi_ch_info_ev_arg arg = {};
    struct survey_info* survey;
    uint32_t err_code, freq, cmd_flags, noise_floor, rx_clear_count, cycle_count;
    int idx, ret;

    ret = ath10k_wmi_pull_ch_info(ar, skb, &arg);
    if (ret) {
        ath10k_warn("failed to parse chan info event: %d\n", ret);
        return;
    }

    err_code = arg.err_code;
    freq = arg.freq;
    cmd_flags = arg.cmd_flags;
    noise_floor = arg.noise_floor;
    rx_clear_count = arg.rx_clear_count;
    cycle_count = arg.cycle_count;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "chan info err_code %d freq %d cmd_flags %d noise_floor %d rx_clear_count %d cycle_count %d\n",
               err_code, freq, cmd_flags, noise_floor, rx_clear_count,
               cycle_count);

    mtx_lock(&ar->data_lock);

    switch (ar->scan.state) {
    case ATH10K_SCAN_IDLE:
    case ATH10K_SCAN_STARTING:
        ath10k_warn("received chan info event without a scan request, ignoring\n");
        goto exit;
    case ATH10K_SCAN_RUNNING:
    case ATH10K_SCAN_ABORTING:
        break;
    }

    idx = freq_to_idx(ar, freq);
    if (idx >= countof(ar->survey)) {
        ath10k_warn("chan info: invalid frequency %d (idx %d out of bounds)\n",
                    freq, idx);
        goto exit;
    }

    if (cmd_flags & WMI_CHAN_INFO_FLAG_COMPLETE) {
        if (ar->ch_info_can_report_survey) {
            survey = &ar->survey[idx];
            survey->noise = noise_floor;
            survey->filled = SURVEY_INFO_NOISE_DBM;

            ath10k_hw_fill_survey_time(ar,
                                       survey,
                                       cycle_count,
                                       rx_clear_count,
                                       ar->survey_last_cycle_count,
                                       ar->survey_last_rx_clear_count);
        }

        ar->ch_info_can_report_survey = false;
    } else {
        ar->ch_info_can_report_survey = true;
    }

    if (!(cmd_flags & WMI_CHAN_INFO_FLAG_PRE_COMPLETE)) {
        ar->survey_last_rx_clear_count = rx_clear_count;
        ar->survey_last_cycle_count = cycle_count;
    }

exit:
    mtx_unlock(&ar->data_lock);
#endif  // NEEDS PORTING
}

void ath10k_wmi_event_echo(struct ath10k* ar, struct ath10k_msg_buf* msg_buf) {
    struct wmi_echo_ev_arg arg = {};
    int ret;

    ret = ath10k_wmi_pull_echo_ev(ar, msg_buf, &arg);
    if (ret) {
        ath10k_warn("failed to parse echo: %d\n", ret);
        return;
    }

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi event echo value 0x%08x\n", arg.value);

    if (arg.value == ATH10K_WMI_BARRIER_ECHO_ID) { sync_completion_signal(&ar->wmi.barrier); }
}

#if 0   // NEEDS PORTING
int ath10k_wmi_event_debug_mesg(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi event debug mesg len %d\n",
               skb->len);

    trace_ath10k_wmi_dbglog(ar, skb->data, skb->len);

    return 0;
}

void ath10k_wmi_pull_pdev_stats_base(const struct wmi_pdev_stats_base* src,
                                     struct ath10k_fw_stats_pdev* dst) {
    dst->ch_noise_floor = src->chan_nf;
    dst->tx_frame_count = src->tx_frame_count;
    dst->rx_frame_count = src->rx_frame_count;
    dst->rx_clear_count = src->rx_clear_count;
    dst->cycle_count = src->cycle_count;
    dst->phy_err_count = src->phy_err_count;
    dst->chan_tx_power = src->chan_tx_pwr;
}

void ath10k_wmi_pull_pdev_stats_tx(const struct wmi_pdev_stats_tx* src,
                                   struct ath10k_fw_stats_pdev* dst) {
    dst->comp_queued = src->comp_queued;
    dst->comp_delivered = src->comp_delivered;
    dst->msdu_enqued = src->msdu_enqued;
    dst->mpdu_enqued = src->mpdu_enqued;
    dst->wmm_drop = src->wmm_drop;
    dst->local_enqued = src->local_enqued;
    dst->local_freed = src->local_freed;
    dst->hw_queued = src->hw_queued;
    dst->hw_reaped = src->hw_reaped;
    dst->underrun = src->underrun;
    dst->tx_abort = src->tx_abort;
    dst->mpdus_requed = src->mpdus_requed;
    dst->tx_ko = src->tx_ko;
    dst->data_rc = src->data_rc;
    dst->self_triggers = src->self_triggers;
    dst->sw_retry_failure = src->sw_retry_failure;
    dst->illgl_rate_phy_err = src->illgl_rate_phy_err;
    dst->pdev_cont_xretry = src->pdev_cont_xretry;
    dst->pdev_tx_timeout = src->pdev_tx_timeout;
    dst->pdev_resets = src->pdev_resets;
    dst->phy_underrun = src->phy_underrun;
    dst->txop_ovf = src->txop_ovf;
}

static void
ath10k_wmi_10_4_pull_pdev_stats_tx(const struct wmi_10_4_pdev_stats_tx* src,
                                   struct ath10k_fw_stats_pdev* dst) {
    dst->comp_queued = src->comp_queued;
    dst->comp_delivered = src->comp_delivered;
    dst->msdu_enqued = src->msdu_enqued;
    dst->mpdu_enqued = src->mpdu_enqued;
    dst->wmm_drop = src->wmm_drop;
    dst->local_enqued = src->local_enqued;
    dst->local_freed = src->local_freed;
    dst->hw_queued = src->hw_queued;
    dst->hw_reaped = src->hw_reaped;
    dst->underrun = src->underrun;
    dst->tx_abort = src->tx_abort;
    dst->mpdus_requed = src->mpdus_requed;
    dst->tx_ko = src->tx_ko;
    dst->data_rc = src->data_rc;
    dst->self_triggers = src->self_triggers;
    dst->sw_retry_failure = src->sw_retry_failure;
    dst->illgl_rate_phy_err = src->illgl_rate_phy_err;
    dst->pdev_cont_xretry = src->pdev_cont_xretry;
    dst->pdev_tx_timeout = src->pdev_tx_timeout;
    dst->pdev_resets = src->pdev_resets;
    dst->phy_underrun = src->phy_underrun;
    dst->txop_ovf = src->txop_ovf;
    dst->hw_paused = src->hw_paused;
    dst->seq_posted = src->seq_posted;
    dst->seq_failed_queueing =
        src->seq_failed_queueing;
    dst->seq_completed = src->seq_completed;
    dst->seq_restarted = src->seq_restarted;
    dst->mu_seq_posted = src->mu_seq_posted;
    dst->mpdus_sw_flush = src->mpdus_sw_flush;
    dst->mpdus_hw_filter = src->mpdus_hw_filter;
    dst->mpdus_truncated = src->mpdus_truncated;
    dst->mpdus_ack_failed = src->mpdus_ack_failed;
    dst->mpdus_hw_filter = src->mpdus_hw_filter;
    dst->mpdus_expired = src->mpdus_expired;
}

void ath10k_wmi_pull_pdev_stats_rx(const struct wmi_pdev_stats_rx* src,
                                   struct ath10k_fw_stats_pdev* dst) {
    dst->mid_ppdu_route_change = src->mid_ppdu_route_change;
    dst->status_rcvd = src->status_rcvd;
    dst->r0_frags = src->r0_frags;
    dst->r1_frags = src->r1_frags;
    dst->r2_frags = src->r2_frags;
    dst->r3_frags = src->r3_frags;
    dst->htt_msdus = src->htt_msdus;
    dst->htt_mpdus = src->htt_mpdus;
    dst->loc_msdus = src->loc_msdus;
    dst->loc_mpdus = src->loc_mpdus;
    dst->oversize_amsdu = src->oversize_amsdu;
    dst->phy_errs = src->phy_errs;
    dst->phy_err_drop = src->phy_err_drop;
    dst->mpdu_errs = src->mpdu_errs;
}

void ath10k_wmi_pull_pdev_stats_extra(const struct wmi_pdev_stats_extra* src,
                                      struct ath10k_fw_stats_pdev* dst) {
    dst->ack_rx_bad = src->ack_rx_bad;
    dst->rts_bad = src->rts_bad;
    dst->rts_good = src->rts_good;
    dst->fcs_bad = src->fcs_bad;
    dst->no_beacons = src->no_beacons;
    dst->mib_int_count = src->mib_int_count;
}

void ath10k_wmi_pull_peer_stats(const struct wmi_peer_stats* src,
                                struct ath10k_fw_stats_peer* dst) {
    memcpy(dst->peer_macaddr, src->peer_macaddr.addr, ETH_ALEN);
    dst->peer_rssi = src->peer_rssi;
    dst->peer_tx_rate = src->peer_tx_rate;
}

static void
ath10k_wmi_10_4_pull_peer_stats(const struct wmi_10_4_peer_stats* src,
                                struct ath10k_fw_stats_peer* dst) {
    memcpy(dst->peer_macaddr, src->peer_macaddr.addr, ETH_ALEN);
    dst->peer_rssi = src->peer_rssi;
    dst->peer_tx_rate = src->peer_tx_rate;
    dst->peer_rx_rate = src->peer_rx_rate;
}

static int ath10k_wmi_main_op_pull_fw_stats(struct ath10k* ar,
        struct sk_buff* skb,
        struct ath10k_fw_stats* stats) {
    const struct wmi_stats_event* ev = (void*)skb->data;
    uint32_t num_pdev_stats, num_vdev_stats, num_peer_stats;
    int i;

    if (!skb_pull(skb, sizeof(*ev))) {
        return -EPROTO;
    }

    num_pdev_stats = ev->num_pdev_stats;
    num_vdev_stats = ev->num_vdev_stats;
    num_peer_stats = ev->num_peer_stats;

    for (i = 0; i < num_pdev_stats; i++) {
        const struct wmi_pdev_stats* src;
        struct ath10k_fw_stats_pdev* dst;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
        if (!dst) {
            continue;
        }

        ath10k_wmi_pull_pdev_stats_base(&src->base, dst);
        ath10k_wmi_pull_pdev_stats_tx(&src->tx, dst);
        ath10k_wmi_pull_pdev_stats_rx(&src->rx, dst);

        list_add_tail(&dst->list, &stats->pdevs);
    }

    /* fw doesn't implement vdev stats */

    for (i = 0; i < num_peer_stats; i++) {
        const struct wmi_peer_stats* src;
        struct ath10k_fw_stats_peer* dst;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
        if (!dst) {
            continue;
        }

        ath10k_wmi_pull_peer_stats(src, dst);
        list_add_tail(&dst->list, &stats->peers);
    }

    return 0;
}

static int ath10k_wmi_10x_op_pull_fw_stats(struct ath10k* ar,
        struct sk_buff* skb,
        struct ath10k_fw_stats* stats) {
    const struct wmi_stats_event* ev = (void*)skb->data;
    uint32_t num_pdev_stats, num_vdev_stats, num_peer_stats;
    int i;

    if (!skb_pull(skb, sizeof(*ev))) {
        return -EPROTO;
    }

    num_pdev_stats = ev->num_pdev_stats;
    num_vdev_stats = ev->num_vdev_stats;
    num_peer_stats = ev->num_peer_stats;

    for (i = 0; i < num_pdev_stats; i++) {
        const struct wmi_10x_pdev_stats* src;
        struct ath10k_fw_stats_pdev* dst;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
        if (!dst) {
            continue;
        }

        ath10k_wmi_pull_pdev_stats_base(&src->base, dst);
        ath10k_wmi_pull_pdev_stats_tx(&src->tx, dst);
        ath10k_wmi_pull_pdev_stats_rx(&src->rx, dst);
        ath10k_wmi_pull_pdev_stats_extra(&src->extra, dst);

        list_add_tail(&dst->list, &stats->pdevs);
    }

    /* fw doesn't implement vdev stats */

    for (i = 0; i < num_peer_stats; i++) {
        const struct wmi_10x_peer_stats* src;
        struct ath10k_fw_stats_peer* dst;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
        if (!dst) {
            continue;
        }

        ath10k_wmi_pull_peer_stats(&src->old, dst);

        dst->peer_rx_rate = src->peer_rx_rate;

        list_add_tail(&dst->list, &stats->peers);
    }

    return 0;
}

static int ath10k_wmi_10_2_op_pull_fw_stats(struct ath10k* ar,
        struct sk_buff* skb,
        struct ath10k_fw_stats* stats) {
    const struct wmi_10_2_stats_event* ev = (void*)skb->data;
    uint32_t num_pdev_stats;
    uint32_t num_pdev_ext_stats;
    uint32_t num_vdev_stats;
    uint32_t num_peer_stats;
    int i;

    if (!skb_pull(skb, sizeof(*ev))) {
        return -EPROTO;
    }

    num_pdev_stats = ev->num_pdev_stats;
    num_pdev_ext_stats = ev->num_pdev_ext_stats;
    num_vdev_stats = ev->num_vdev_stats;
    num_peer_stats = ev->num_peer_stats;

    for (i = 0; i < num_pdev_stats; i++) {
        const struct wmi_10_2_pdev_stats* src;
        struct ath10k_fw_stats_pdev* dst;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
        if (!dst) {
            continue;
        }

        ath10k_wmi_pull_pdev_stats_base(&src->base, dst);
        ath10k_wmi_pull_pdev_stats_tx(&src->tx, dst);
        ath10k_wmi_pull_pdev_stats_rx(&src->rx, dst);
        ath10k_wmi_pull_pdev_stats_extra(&src->extra, dst);
        /* FIXME: expose 10.2 specific values */

        list_add_tail(&dst->list, &stats->pdevs);
    }

    for (i = 0; i < num_pdev_ext_stats; i++) {
        const struct wmi_10_2_pdev_ext_stats* src;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        /* FIXME: expose values to userspace
         *
         * Note: Even though this loop seems to do nothing it is
         * required to parse following sub-structures properly.
         */
    }

    /* fw doesn't implement vdev stats */

    for (i = 0; i < num_peer_stats; i++) {
        const struct wmi_10_2_peer_stats* src;
        struct ath10k_fw_stats_peer* dst;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
        if (!dst) {
            continue;
        }

        ath10k_wmi_pull_peer_stats(&src->old, dst);

        dst->peer_rx_rate = src->peer_rx_rate;
        /* FIXME: expose 10.2 specific values */

        list_add_tail(&dst->list, &stats->peers);
    }

    return 0;
}

static int ath10k_wmi_10_2_4_op_pull_fw_stats(struct ath10k* ar,
        struct sk_buff* skb,
        struct ath10k_fw_stats* stats) {
    const struct wmi_10_2_stats_event* ev = (void*)skb->data;
    uint32_t num_pdev_stats;
    uint32_t num_pdev_ext_stats;
    uint32_t num_vdev_stats;
    uint32_t num_peer_stats;
    int i;

    if (!skb_pull(skb, sizeof(*ev))) {
        return -EPROTO;
    }

    num_pdev_stats = ev->num_pdev_stats;
    num_pdev_ext_stats = ev->num_pdev_ext_stats;
    num_vdev_stats = ev->num_vdev_stats;
    num_peer_stats = ev->num_peer_stats;

    for (i = 0; i < num_pdev_stats; i++) {
        const struct wmi_10_2_pdev_stats* src;
        struct ath10k_fw_stats_pdev* dst;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
        if (!dst) {
            continue;
        }

        ath10k_wmi_pull_pdev_stats_base(&src->base, dst);
        ath10k_wmi_pull_pdev_stats_tx(&src->tx, dst);
        ath10k_wmi_pull_pdev_stats_rx(&src->rx, dst);
        ath10k_wmi_pull_pdev_stats_extra(&src->extra, dst);
        /* FIXME: expose 10.2 specific values */

        list_add_tail(&dst->list, &stats->pdevs);
    }

    for (i = 0; i < num_pdev_ext_stats; i++) {
        const struct wmi_10_2_pdev_ext_stats* src;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        /* FIXME: expose values to userspace
         *
         * Note: Even though this loop seems to do nothing it is
         * required to parse following sub-structures properly.
         */
    }

    /* fw doesn't implement vdev stats */

    for (i = 0; i < num_peer_stats; i++) {
        const struct wmi_10_2_4_ext_peer_stats* src;
        struct ath10k_fw_stats_peer* dst;
        int stats_len;

        if (BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_PEER_STATS)) {
            stats_len = sizeof(struct wmi_10_2_4_ext_peer_stats);
        } else {
            stats_len = sizeof(struct wmi_10_2_4_peer_stats);
        }

        src = (void*)skb->data;
        if (!skb_pull(skb, stats_len)) {
            return -EPROTO;
        }

        dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
        if (!dst) {
            continue;
        }

        ath10k_wmi_pull_peer_stats(&src->common.old, dst);

        dst->peer_rx_rate = src->common.peer_rx_rate;

        if (ath10k_peer_stats_enabled(ar)) {
            dst->rx_duration = src->rx_duration;
        }
        /* FIXME: expose 10.2 specific values */

        list_add_tail(&dst->list, &stats->peers);
    }

    return 0;
}

static int ath10k_wmi_10_4_op_pull_fw_stats(struct ath10k* ar,
        struct sk_buff* skb,
        struct ath10k_fw_stats* stats) {
    const struct wmi_10_2_stats_event* ev = (void*)skb->data;
    uint32_t num_pdev_stats;
    uint32_t num_pdev_ext_stats;
    uint32_t num_vdev_stats;
    uint32_t num_peer_stats;
    uint32_t num_bcnflt_stats;
    uint32_t stats_id;
    int i;

    if (!skb_pull(skb, sizeof(*ev))) {
        return -EPROTO;
    }

    num_pdev_stats = ev->num_pdev_stats;
    num_pdev_ext_stats = ev->num_pdev_ext_stats;
    num_vdev_stats = ev->num_vdev_stats;
    num_peer_stats = ev->num_peer_stats;
    num_bcnflt_stats = ev->num_bcnflt_stats;
    stats_id = ev->stats_id;

    for (i = 0; i < num_pdev_stats; i++) {
        const struct wmi_10_4_pdev_stats* src;
        struct ath10k_fw_stats_pdev* dst;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
        if (!dst) {
            continue;
        }

        ath10k_wmi_pull_pdev_stats_base(&src->base, dst);
        ath10k_wmi_10_4_pull_pdev_stats_tx(&src->tx, dst);
        ath10k_wmi_pull_pdev_stats_rx(&src->rx, dst);
        dst->rx_ovfl_errs = src->rx_ovfl_errs;
        ath10k_wmi_pull_pdev_stats_extra(&src->extra, dst);

        list_add_tail(&dst->list, &stats->pdevs);
    }

    for (i = 0; i < num_pdev_ext_stats; i++) {
        const struct wmi_10_2_pdev_ext_stats* src;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        /* FIXME: expose values to userspace
         *
         * Note: Even though this loop seems to do nothing it is
         * required to parse following sub-structures properly.
         */
    }

    /* fw doesn't implement vdev stats */

    for (i = 0; i < num_peer_stats; i++) {
        const struct wmi_10_4_peer_stats* src;
        struct ath10k_fw_stats_peer* dst;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
        if (!dst) {
            continue;
        }

        ath10k_wmi_10_4_pull_peer_stats(src, dst);
        list_add_tail(&dst->list, &stats->peers);
    }

    for (i = 0; i < num_bcnflt_stats; i++) {
        const struct wmi_10_4_bss_bcn_filter_stats* src;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        /* FIXME: expose values to userspace
         *
         * Note: Even though this loop seems to do nothing it is
         * required to parse following sub-structures properly.
         */
    }

    if ((stats_id & WMI_10_4_STAT_PEER_EXTD) == 0) {
        return 0;
    }

    stats->extended = true;

    for (i = 0; i < num_peer_stats; i++) {
        const struct wmi_10_4_peer_extd_stats* src;
        struct ath10k_fw_extd_stats_peer* dst;

        src = (void*)skb->data;
        if (!skb_pull(skb, sizeof(*src))) {
            return -EPROTO;
        }

        dst = kzalloc(sizeof(*dst), GFP_ATOMIC);
        if (!dst) {
            continue;
        }

        memcpy(dst->peer_macaddr, src->peer_macaddr.addr, ETH_ALEN);
        dst->rx_duration = src->rx_duration;
        list_add_tail(&dst->list, &stats->peers_extd);
    }

    return 0;
}

void ath10k_wmi_event_update_stats(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_UPDATE_STATS_EVENTID\n");
    ath10k_debug_fw_stats_process(ar, skb);
}
#endif  // NEEDS PORTING

static zx_status_t ath10k_wmi_op_pull_vdev_start_ev(struct ath10k* ar,
                                                    struct ath10k_msg_buf* msg_buf,
                                                    struct wmi_vdev_start_ev_arg* arg) {
    struct wmi_vdev_start_response_event* ev;

    if (ath10k_msg_buf_get_payload_len(msg_buf, ATH10K_MSG_TYPE_WMI) < sizeof(*ev)) {
        return ZX_ERR_INVALID_ARGS;
    }

    ev = ath10k_msg_buf_get_payload(msg_buf);

    arg->vdev_id = ev->vdev_id;
    arg->req_id = ev->req_id;
    arg->resp_type = ev->resp_type;
    arg->status = ev->status;

    return ZX_OK;
}

void ath10k_wmi_event_vdev_start_resp(struct ath10k* ar, struct ath10k_msg_buf* buf) {
    struct wmi_vdev_start_ev_arg arg = {};
    zx_status_t ret;

    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_VDEV_START_RESP_EVENTID\n");

    ret = ath10k_wmi_pull_vdev_start(ar, buf, &arg);
    if (ret != ZX_OK) {
        ath10k_warn("failed to parse vdev start event: %d\n", ret);
        return;
    }

    if (COND_WARN(arg.status)) { return; }

    sync_completion_signal(&ar->vdev_setup_done);
}

void ath10k_wmi_event_vdev_stopped(struct ath10k* ar, struct ath10k_msg_buf* buf) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_VDEV_STOPPED_EVENTID\n");
    sync_completion_signal(&ar->vdev_setup_done);
}

#if 0   // NEEDS PORTING
static int
ath10k_wmi_op_pull_peer_kick_ev(struct ath10k* ar, struct sk_buff* skb,
                                struct wmi_peer_kick_ev_arg* arg) {
    struct wmi_peer_sta_kickout_event* ev = (void*)skb->data;

    if (skb->len < sizeof(*ev)) {
        return -EPROTO;
    }

    skb_pull(skb, sizeof(*ev));
    arg->mac_addr = ev->peer_macaddr.addr;

    return 0;
}

void ath10k_wmi_event_peer_sta_kickout(struct ath10k* ar, struct sk_buff* skb) {
    struct wmi_peer_kick_ev_arg arg = {};
    struct ieee80211_sta* sta;
    int ret;

    ret = ath10k_wmi_pull_peer_kick(ar, skb, &arg);
    if (ret) {
        ath10k_warn("failed to parse peer kickout event: %d\n",
                    ret);
        return;
    }

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi event peer sta kickout %pM\n",
               arg.mac_addr);

    rcu_read_lock();

    sta = ieee80211_find_sta_by_ifaddr(ar->hw, arg.mac_addr, NULL);
    if (!sta) {
        ath10k_warn("Spurious quick kickout for STA %pM\n",
                    arg.mac_addr);
        goto exit;
    }

    ieee80211_report_low_ack(sta, 10);

exit:
    rcu_read_unlock();
}

/*
 * FIXME
 *
 * We don't report to mac80211 sleep state of connected
 * stations. Due to this mac80211 can't fill in TIM IE
 * correctly.
 *
 * I know of no way of getting nullfunc frames that contain
 * sleep transition from connected stations - these do not
 * seem to be sent from the target to the host. There also
 * doesn't seem to be a dedicated event for that. So the
 * only way left to do this would be to read tim_bitmap
 * during SWBA.
 *
 * We could probably try using tim_bitmap from SWBA to tell
 * mac80211 which stations are asleep and which are not. The
 * problem here is calling mac80211 functions so many times
 * could take too long and make us miss the time to submit
 * the beacon to the target.
 *
 * So as a workaround we try to extend the TIM IE if there
 * is unicast buffered for stations with aid > 7 and fill it
 * in ourselves.
 */
static void ath10k_wmi_update_tim(struct ath10k* ar,
                                  struct ath10k_vif* arvif,
                                  struct sk_buff* bcn,
                                  const struct wmi_tim_info_arg* tim_info) {
    struct ieee80211_hdr* hdr = (struct ieee80211_hdr*)bcn->data;
    struct ieee80211_tim_ie* tim;
    uint8_t* ies, *ie;
    uint8_t ie_len, pvm_len;
    uint32_t t;
    uint32_t v, tim_len;

    /* When FW reports 0 in tim_len, ensure atleast first byte
     * in tim_bitmap is considered for pvm calculation.
     */
    tim_len = tim_info->tim_len ? tim_info->tim_len : 1;

    /* if next SWBA has no tim_changed the tim_bitmap is garbage.
     * we must copy the bitmap upon change and reuse it later
     */
    if (tim_info->tim_changed) {
        int i;

        if (sizeof(arvif->u.ap.tim_bitmap) < tim_len) {
            ath10k_warn("SWBA TIM field is too big (%u), truncated it to %zu",
                        tim_len, sizeof(arvif->u.ap.tim_bitmap));
            tim_len = sizeof(arvif->u.ap.tim_bitmap);
        }

        for (i = 0; i < tim_len; i++) {
            t = tim_info->tim_bitmap[i / 4];
            v = t;
            arvif->u.ap.tim_bitmap[i] = (v >> ((i % 4) * 8)) & 0xFF;
        }

        /* FW reports either length 0 or length based on max supported
         * station. so we calculate this on our own
         */
        arvif->u.ap.tim_len = 0;
        for (i = 0; i < tim_len; i++)
            if (arvif->u.ap.tim_bitmap[i]) {
                arvif->u.ap.tim_len = i;
            }

        arvif->u.ap.tim_len++;
    }

    ies = bcn->data;
    ies += ieee80211_hdrlen(hdr->frame_control);
    ies += 12; /* fixed parameters */

    ie = (uint8_t*)cfg80211_find_ie(WLAN_EID_TIM, ies,
                               (uint8_t*)skb_tail_pointer(bcn) - ies);
    if (!ie) {
        if (arvif->vdev_type != WMI_VDEV_TYPE_IBSS) {
            ath10k_warn("no tim ie found;\n");
        }
        return;
    }

    tim = (void*)ie + 2;
    ie_len = ie[1];
    pvm_len = ie_len - 3; /* exclude dtim count, dtim period, bmap ctl */

    if (pvm_len < arvif->u.ap.tim_len) {
        int expand_size = tim_len - pvm_len;
        int move_size = skb_tail_pointer(bcn) - (ie + 2 + ie_len);
        void* next_ie = ie + 2 + ie_len;

        if (skb_put(bcn, expand_size)) {
            memmove(next_ie + expand_size, next_ie, move_size);

            ie[1] += expand_size;
            ie_len += expand_size;
            pvm_len += expand_size;
        } else {
            ath10k_warn("tim expansion failed\n");
        }
    }

    if (pvm_len > tim_len) {
        ath10k_warn("tim pvm length is too great (%d)\n", pvm_len);
        return;
    }

    tim->bitmap_ctrl = !!tim_info->tim_mcast;
    memcpy(tim->virtual_map, arvif->u.ap.tim_bitmap, pvm_len);

    if (tim->dtim_count == 0) {
        ATH10K_SKB_CB(bcn)->flags |= ATH10K_SKB_F_DTIM_ZERO;

        if (tim_info->tim_mcast == 1) {
            ATH10K_SKB_CB(bcn)->flags |= ATH10K_SKB_F_DELIVER_CAB;
        }
    }

    ath10k_dbg(ar, ATH10K_DBG_MGMT, "dtim %d/%d mcast %d pvmlen %d\n",
               tim->dtim_count, tim->dtim_period,
               tim->bitmap_ctrl, pvm_len);
}

static void ath10k_wmi_update_noa(struct ath10k* ar, struct ath10k_vif* arvif,
                                  struct sk_buff* bcn,
                                  const struct wmi_p2p_noa_info* noa) {
    if (!arvif->vif->p2p) {
        return;
    }

    ath10k_dbg(ar, ATH10K_DBG_MGMT, "noa changed: %d\n", noa->changed);

    if (noa->changed & WMI_P2P_NOA_CHANGED_BIT) {
        ath10k_p2p_noa_update(arvif, noa);
    }

    if (arvif->u.ap.noa_data)
        if (!pskb_expand_head(bcn, 0, arvif->u.ap.noa_len, GFP_ATOMIC))
            skb_put_data(bcn, arvif->u.ap.noa_data,
                         arvif->u.ap.noa_len);
}

static int ath10k_wmi_op_pull_swba_ev(struct ath10k* ar, struct sk_buff* skb,
                                      struct wmi_swba_ev_arg* arg) {
    struct wmi_host_swba_event* ev = (void*)skb->data;
    uint32_t map;
    size_t i;

    if (skb->len < sizeof(*ev)) {
        return -EPROTO;
    }

    skb_pull(skb, sizeof(*ev));
    arg->vdev_map = ev->vdev_map;

    for (i = 0, map = ev->vdev_map; map; map >>= 1) {
        if (!(map & 0x1)) {
            continue;
        }

        /* If this happens there were some changes in firmware and
         * ath10k should update the max size of tim_info array.
         */
        if (COND_WARN_ONCE(i == countof(arg->tim_info))) {
            break;
        }

        if (ev->bcn_info[i].tim_info.tim_len >
                sizeof(ev->bcn_info[i].tim_info.tim_bitmap)) {
            ath10k_warn("refusing to parse invalid swba structure\n");
            return -EPROTO;
        }

        arg->tim_info[i].tim_len = ev->bcn_info[i].tim_info.tim_len;
        arg->tim_info[i].tim_mcast = ev->bcn_info[i].tim_info.tim_mcast;
        arg->tim_info[i].tim_bitmap =
            ev->bcn_info[i].tim_info.tim_bitmap;
        arg->tim_info[i].tim_changed =
            ev->bcn_info[i].tim_info.tim_changed;
        arg->tim_info[i].tim_num_ps_pending =
            ev->bcn_info[i].tim_info.tim_num_ps_pending;

        arg->noa_info[i] = &ev->bcn_info[i].p2p_noa_info;
        i++;
    }

    return 0;
}

static int ath10k_wmi_10_2_4_op_pull_swba_ev(struct ath10k* ar,
        struct sk_buff* skb,
        struct wmi_swba_ev_arg* arg) {
    struct wmi_10_2_4_host_swba_event* ev = (void*)skb->data;
    uint32_t map;
    size_t i;

    if (skb->len < sizeof(*ev)) {
        return -EPROTO;
    }

    skb_pull(skb, sizeof(*ev));
    arg->vdev_map = ev->vdev_map;

    for (i = 0, map = ev->vdev_map; map; map >>= 1) {
        if (!(map & 0x1)) {
            continue;
        }

        /* If this happens there were some changes in firmware and
         * ath10k should update the max size of tim_info array.
         */
        if (COND_WARN_ONCE(i == countof(arg->tim_info))) {
            break;
        }

        if (ev->bcn_info[i].tim_info.tim_len >
                sizeof(ev->bcn_info[i].tim_info.tim_bitmap)) {
            ath10k_warn("refusing to parse invalid swba structure\n");
            return -EPROTO;
        }

        arg->tim_info[i].tim_len = ev->bcn_info[i].tim_info.tim_len;
        arg->tim_info[i].tim_mcast = ev->bcn_info[i].tim_info.tim_mcast;
        arg->tim_info[i].tim_bitmap =
            ev->bcn_info[i].tim_info.tim_bitmap;
        arg->tim_info[i].tim_changed =
            ev->bcn_info[i].tim_info.tim_changed;
        arg->tim_info[i].tim_num_ps_pending =
            ev->bcn_info[i].tim_info.tim_num_ps_pending;
        i++;
    }

    return 0;
}

static int ath10k_wmi_10_4_op_pull_swba_ev(struct ath10k* ar,
        struct sk_buff* skb,
        struct wmi_swba_ev_arg* arg) {
    struct wmi_10_4_host_swba_event* ev = (void*)skb->data;
    uint32_t map, tim_len;
    size_t i;

    if (skb->len < sizeof(*ev)) {
        return -EPROTO;
    }

    skb_pull(skb, sizeof(*ev));
    arg->vdev_map = ev->vdev_map;

    for (i = 0, map = ev->vdev_map; map; map >>= 1) {
        if (!(map & 0x1)) {
            continue;
        }

        /* If this happens there were some changes in firmware and
         * ath10k should update the max size of tim_info array.
         */
        if (COND_WARN_ONCE(i == countof(arg->tim_info))) {
            break;
        }

        if (ev->bcn_info[i].tim_info.tim_len >
                sizeof(ev->bcn_info[i].tim_info.tim_bitmap)) {
            ath10k_warn("refusing to parse invalid swba structure\n");
            return -EPROTO;
        }

        tim_len = ev->bcn_info[i].tim_info.tim_len;
        if (tim_len) {
            /* Exclude 4 byte guard length */
            tim_len -= 4;
            arg->tim_info[i].tim_len = tim_len;
        } else {
            arg->tim_info[i].tim_len = 0;
        }

        arg->tim_info[i].tim_mcast = ev->bcn_info[i].tim_info.tim_mcast;
        arg->tim_info[i].tim_bitmap =
            ev->bcn_info[i].tim_info.tim_bitmap;
        arg->tim_info[i].tim_changed =
            ev->bcn_info[i].tim_info.tim_changed;
        arg->tim_info[i].tim_num_ps_pending =
            ev->bcn_info[i].tim_info.tim_num_ps_pending;

        /* 10.4 firmware doesn't have p2p support. notice of absence
         * info can be ignored for now.
         */

        i++;
    }

    return 0;
}

static enum wmi_txbf_conf ath10k_wmi_10_4_txbf_conf_scheme(struct ath10k* ar) {
    return WMI_TXBF_CONF_BEFORE_ASSOC;
}

void ath10k_wmi_event_host_swba(struct ath10k* ar, struct sk_buff* skb) {
    struct wmi_swba_ev_arg arg = {};
    uint32_t map;
    int i = -1;
    const struct wmi_tim_info_arg* tim_info;
    const struct wmi_p2p_noa_info* noa_info;
    struct ath10k_vif* arvif;
    struct sk_buff* bcn;
    dma_addr_t paddr;
    int ret, vdev_id = 0;

    ret = ath10k_wmi_pull_swba(ar, skb, &arg);
    if (ret) {
        ath10k_warn("failed to parse swba event: %d\n", ret);
        return;
    }

    map = arg.vdev_map;

    ath10k_dbg(ar, ATH10K_DBG_MGMT, "mgmt swba vdev_map 0x%x\n",
               map);

    for (; map; map >>= 1, vdev_id++) {
        if (!(map & 0x1)) {
            continue;
        }

        i++;

        if (i >= WMI_MAX_AP_VDEV) {
            ath10k_warn("swba has corrupted vdev map\n");
            break;
        }

        tim_info = &arg.tim_info[i];
        noa_info = arg.noa_info[i];

        ath10k_dbg(ar, ATH10K_DBG_MGMT,
                   "mgmt event bcn_info %d tim_len %d mcast %d changed %d num_ps_pending %d bitmap 0x%08x%08x%08x%08x\n",
                   i,
                   tim_info->tim_len,
                   tim_info->tim_mcast,
                   tim_info->tim_changed,
                   tim_info->tim_num_ps_pending,
                   tim_info->tim_bitmap[3],
                   tim_info->tim_bitmap[2],
                   tim_info->tim_bitmap[1],
                   tim_info->tim_bitmap[0]);

        /* TODO: Only first 4 word from tim_bitmap is dumped.
         * Extend debug code to dump full tim_bitmap.
         */

        arvif = ath10k_get_arvif(ar, vdev_id);
        if (arvif == NULL) {
            ath10k_warn("no vif for vdev_id %d found\n",
                        vdev_id);
            continue;
        }

        /* mac80211 would have already asked us to stop beaconing and
         * bring the vdev down, so continue in that case
         */
        if (!arvif->is_up) {
            continue;
        }

        /* There are no completions for beacons so wait for next SWBA
         * before telling mac80211 to decrement CSA counter
         *
         * Once CSA counter is completed stop sending beacons until
         * actual channel switch is done
         */
        if (arvif->vif->csa_active &&
                ieee80211_csa_is_complete(arvif->vif)) {
            ieee80211_csa_finish(arvif->vif);
            continue;
        }

        bcn = ieee80211_beacon_get(ar->hw, arvif->vif);
        if (!bcn) {
            ath10k_warn("could not get mac80211 beacon\n");
            continue;
        }

        ath10k_tx_h_seq_no(arvif->vif, bcn);
        ath10k_wmi_update_tim(ar, arvif, bcn, tim_info);
        ath10k_wmi_update_noa(ar, arvif, bcn, noa_info);

        mtx_lock(&ar->data_lock);

        if (arvif->beacon) {
            switch (arvif->beacon_state) {
            case ATH10K_BEACON_SENT:
                break;
            case ATH10K_BEACON_SCHEDULED:
                ath10k_warn("SWBA overrun on vdev %d, skipped old beacon\n",
                            arvif->vdev_id);
                break;
            case ATH10K_BEACON_SENDING:
                ath10k_warn("SWBA overrun on vdev %d, skipped new beacon\n",
                            arvif->vdev_id);
                dev_kfree_skb(bcn);
                goto skip;
            }

            ath10k_mac_vif_beacon_free(arvif);
        }

        if (!arvif->beacon_buf) {
            paddr = dma_map_single(arvif->ar->dev, bcn->data,
                                   bcn->len, DMA_TO_DEVICE);
            ret = dma_mapping_error(arvif->ar->dev, paddr);
            if (ret) {
                ath10k_warn("failed to map beacon: %d\n",
                            ret);
                dev_kfree_skb_any(bcn);
                goto skip;
            }

            ATH10K_SKB_CB(bcn)->paddr = paddr;
        } else {
            if (bcn->len > IEEE80211_MAX_FRAME_LEN) {
                ath10k_warn("trimming beacon %d -> %d bytes!\n",
                            bcn->len, IEEE80211_MAX_FRAME_LEN);
                skb_trim(bcn, IEEE80211_MAX_FRAME_LEN);
            }
            memcpy(arvif->beacon_buf, bcn->data, bcn->len);
            ATH10K_SKB_CB(bcn)->paddr = arvif->beacon_paddr;
        }

        arvif->beacon = bcn;
        arvif->beacon_state = ATH10K_BEACON_SCHEDULED;

        trace_ath10k_tx_hdr(ar, bcn->data, bcn->len);
        trace_ath10k_tx_payload(ar, bcn->data, bcn->len);

skip:
        mtx_unlock(&ar->data_lock);
    }

    ath10k_wmi_tx_beacons_nowait(ar);
}

void ath10k_wmi_event_tbttoffset_update(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_TBTTOFFSET_UPDATE_EVENTID\n");
}

static void ath10k_dfs_radar_report(struct ath10k* ar,
                                    struct wmi_phyerr_ev_arg* phyerr,
                                    const struct phyerr_radar_report* rr,
                                    uint64_t tsf) {
    uint32_t reg0, reg1, tsf32l;
    struct ieee80211_channel* ch;
    struct pulse_event pe;
    uint64_t tsf64;
    uint8_t rssi, width;

    reg0 = rr->reg0;
    reg1 = rr->reg1;

    ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
               "wmi phyerr radar report chirp %d max_width %d agc_total_gain %d pulse_delta_diff %d\n",
               MS(reg0, RADAR_REPORT_REG0_PULSE_IS_CHIRP),
               MS(reg0, RADAR_REPORT_REG0_PULSE_IS_MAX_WIDTH),
               MS(reg0, RADAR_REPORT_REG0_AGC_TOTAL_GAIN),
               MS(reg0, RADAR_REPORT_REG0_PULSE_DELTA_DIFF));
    ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
               "wmi phyerr radar report pulse_delta_pean %d pulse_sidx %d fft_valid %d agc_mb_gain %d subchan_mask %d\n",
               MS(reg0, RADAR_REPORT_REG0_PULSE_DELTA_PEAK),
               MS(reg0, RADAR_REPORT_REG0_PULSE_SIDX),
               MS(reg1, RADAR_REPORT_REG1_PULSE_SRCH_FFT_VALID),
               MS(reg1, RADAR_REPORT_REG1_PULSE_AGC_MB_GAIN),
               MS(reg1, RADAR_REPORT_REG1_PULSE_SUBCHAN_MASK));
    ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
               "wmi phyerr radar report pulse_tsf_offset 0x%X pulse_dur: %d\n",
               MS(reg1, RADAR_REPORT_REG1_PULSE_TSF_OFFSET),
               MS(reg1, RADAR_REPORT_REG1_PULSE_DUR));

    if (!ar->dfs_detector) {
        return;
    }

    mtx_lock(&ar->data_lock);
    ch = ar->rx_channel;

    /* fetch target operating channel during channel change */
    if (!ch) {
        ch = ar->tgt_oper_chan;
    }

    mtx_unlock(&ar->data_lock);

    if (!ch) {
        ath10k_warn("failed to derive channel for radar pulse, treating as radar\n");
        goto radar_detected;
    }

    /* report event to DFS pattern detector */
    tsf32l = phyerr->tsf_timestamp;
    tsf64 = tsf & (~0xFFFFFFFFULL);
    tsf64 |= tsf32l;

    width = MS(reg1, RADAR_REPORT_REG1_PULSE_DUR);
    rssi = phyerr->rssi_combined;

    /* hardware store this as 8 bit signed value,
     * set to zero if negative number
     */
    if (rssi & 0x80) {
        rssi = 0;
    }

    pe.ts = tsf64;
    pe.freq = ch->center_freq;
    pe.width = width;
    pe.rssi = rssi;
    pe.chirp = (MS(reg0, RADAR_REPORT_REG0_PULSE_IS_CHIRP) != 0);
    ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
               "dfs add pulse freq: %d, width: %d, rssi %d, tsf: %llX\n",
               pe.freq, pe.width, pe.rssi, pe.ts);

    ATH10K_DFS_STAT_INC(ar, pulses_detected);

    if (!ar->dfs_detector->add_pulse(ar->dfs_detector, &pe)) {
        ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
                   "dfs no pulse pattern detected, yet\n");
        return;
    }

radar_detected:
    ath10k_dbg(ar, ATH10K_DBG_REGULATORY, "dfs radar detected\n");
    ATH10K_DFS_STAT_INC(ar, radar_detected);

    /* Control radar events reporting in debugfs file
     * dfs_block_radar_events
     */
    if (ar->dfs_block_radar_events) {
        ath10k_trace("DFS Radar detected, but ignored as requested\n");
        return;
    }

    ieee80211_radar_detected(ar->hw);
}

static int ath10k_dfs_fft_report(struct ath10k* ar,
                                 struct wmi_phyerr_ev_arg* phyerr,
                                 const struct phyerr_fft_report* fftr,
                                 uint64_t tsf) {
    uint32_t reg0, reg1;
    uint8_t rssi, peak_mag;

    reg0 = fftr->reg0;
    reg1 = fftr->reg1;
    rssi = phyerr->rssi_combined;

    ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
               "wmi phyerr fft report total_gain_db %d base_pwr_db %d fft_chn_idx %d peak_sidx %d\n",
               MS(reg0, SEARCH_FFT_REPORT_REG0_TOTAL_GAIN_DB),
               MS(reg0, SEARCH_FFT_REPORT_REG0_BASE_PWR_DB),
               MS(reg0, SEARCH_FFT_REPORT_REG0_FFT_CHN_IDX),
               MS(reg0, SEARCH_FFT_REPORT_REG0_PEAK_SIDX));
    ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
               "wmi phyerr fft report rel_pwr_db %d avgpwr_db %d peak_mag %d num_store_bin %d\n",
               MS(reg1, SEARCH_FFT_REPORT_REG1_RELPWR_DB),
               MS(reg1, SEARCH_FFT_REPORT_REG1_AVGPWR_DB),
               MS(reg1, SEARCH_FFT_REPORT_REG1_PEAK_MAG),
               MS(reg1, SEARCH_FFT_REPORT_REG1_NUM_STR_BINS_IB));

    peak_mag = MS(reg1, SEARCH_FFT_REPORT_REG1_PEAK_MAG);

    /* false event detection */
    if (rssi == DFS_RSSI_POSSIBLY_FALSE &&
            peak_mag < 2 * DFS_PEAK_MAG_THOLD_POSSIBLY_FALSE) {
        ath10k_dbg(ar, ATH10K_DBG_REGULATORY, "dfs false pulse detected\n");
        ATH10K_DFS_STAT_INC(ar, pulses_discarded);
        return -EINVAL;
    }

    return 0;
}

void ath10k_wmi_event_dfs(struct ath10k* ar,
                          struct wmi_phyerr_ev_arg* phyerr,
                          uint64_t tsf) {
    int buf_len, tlv_len, res, i = 0;
    const struct phyerr_tlv* tlv;
    const struct phyerr_radar_report* rr;
    const struct phyerr_fft_report* fftr;
    const uint8_t* tlv_buf;

    buf_len = phyerr->buf_len;
    ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
               "wmi event dfs err_code %d rssi %d tsfl 0x%X tsf64 0x%llX len %d\n",
               phyerr->phy_err_code, phyerr->rssi_combined,
               phyerr->tsf_timestamp, tsf, buf_len);

    /* Skip event if DFS disabled */
    if (!IS_ENABLED(CONFIG_ATH10K_DFS_CERTIFIED)) {
        return;
    }

    ATH10K_DFS_STAT_INC(ar, pulses_total);

    while (i < buf_len) {
        if (i + sizeof(*tlv) > buf_len) {
            ath10k_warn("too short buf for tlv header (%d)\n",
                        i);
            return;
        }

        tlv = (struct phyerr_tlv*)&phyerr->buf[i];
        tlv_len = tlv->len;
        tlv_buf = &phyerr->buf[i + sizeof(*tlv)];
        ath10k_dbg(ar, ATH10K_DBG_REGULATORY,
                   "wmi event dfs tlv_len %d tlv_tag 0x%02X tlv_sig 0x%02X\n",
                   tlv_len, tlv->tag, tlv->sig);

        switch (tlv->tag) {
        case PHYERR_TLV_TAG_RADAR_PULSE_SUMMARY:
            if (i + sizeof(*tlv) + sizeof(*rr) > buf_len) {
                ath10k_warn("too short radar pulse summary (%d)\n",
                            i);
                return;
            }

            rr = (struct phyerr_radar_report*)tlv_buf;
            ath10k_dfs_radar_report(ar, phyerr, rr, tsf);
            break;
        case PHYERR_TLV_TAG_SEARCH_FFT_REPORT:
            if (i + sizeof(*tlv) + sizeof(*fftr) > buf_len) {
                ath10k_warn("too short fft report (%d)\n",
                            i);
                return;
            }

            fftr = (struct phyerr_fft_report*)tlv_buf;
            res = ath10k_dfs_fft_report(ar, phyerr, fftr, tsf);
            if (res) {
                return;
            }
            break;
        }

        i += sizeof(*tlv) + tlv_len;
    }
}

void ath10k_wmi_event_spectral_scan(struct ath10k* ar,
                                    struct wmi_phyerr_ev_arg* phyerr,
                                    uint64_t tsf) {
    int buf_len, tlv_len, res, i = 0;
    struct phyerr_tlv* tlv;
    const void* tlv_buf;
    const struct phyerr_fft_report* fftr;
    size_t fftr_len;

    buf_len = phyerr->buf_len;

    while (i < buf_len) {
        if (i + sizeof(*tlv) > buf_len) {
            ath10k_warn("failed to parse phyerr tlv header at byte %d\n",
                        i);
            return;
        }

        tlv = (struct phyerr_tlv*)&phyerr->buf[i];
        tlv_len = tlv->len;
        tlv_buf = &phyerr->buf[i + sizeof(*tlv)];

        if (i + sizeof(*tlv) + tlv_len > buf_len) {
            ath10k_warn("failed to parse phyerr tlv payload at byte %d\n",
                        i);
            return;
        }

        switch (tlv->tag) {
        case PHYERR_TLV_TAG_SEARCH_FFT_REPORT:
            if (sizeof(*fftr) > tlv_len) {
                ath10k_warn("failed to parse fft report at byte %d\n",
                            i);
                return;
            }

            fftr_len = tlv_len - sizeof(*fftr);
            fftr = tlv_buf;
            res = ath10k_spectral_process_fft(ar, phyerr,
                                              fftr, fftr_len,
                                              tsf);
            if (res < 0) {
                ath10k_dbg(ar, ATH10K_DBG_WMI, "failed to process fft report: %d\n",
                           res);
                return;
            }
            break;
        }

        i += sizeof(*tlv) + tlv_len;
    }
}

static int ath10k_wmi_op_pull_phyerr_ev_hdr(struct ath10k* ar,
        struct sk_buff* skb,
        struct wmi_phyerr_hdr_arg* arg) {
    struct wmi_phyerr_event* ev = (void*)skb->data;

    if (skb->len < sizeof(*ev)) {
        return -EPROTO;
    }

    arg->num_phyerrs = ev->num_phyerrs;
    arg->tsf_l32 = ev->tsf_l32;
    arg->tsf_u32 = ev->tsf_u32;
    arg->buf_len = skb->len - sizeof(*ev);
    arg->phyerrs = ev->phyerrs;

    return 0;
}

static int ath10k_wmi_10_4_op_pull_phyerr_ev_hdr(struct ath10k* ar,
        struct sk_buff* skb,
        struct wmi_phyerr_hdr_arg* arg) {
    struct wmi_10_4_phyerr_event* ev = (void*)skb->data;

    if (skb->len < sizeof(*ev)) {
        return -EPROTO;
    }

    /* 10.4 firmware always reports only one phyerr */
    arg->num_phyerrs = 1;

    arg->tsf_l32 = ev->tsf_l32;
    arg->tsf_u32 = ev->tsf_u32;
    arg->buf_len = skb->len;
    arg->phyerrs = skb->data;

    return 0;
}

int ath10k_wmi_op_pull_phyerr_ev(struct ath10k* ar,
                                 const void* phyerr_buf,
                                 int left_len,
                                 struct wmi_phyerr_ev_arg* arg) {
    const struct wmi_phyerr* phyerr = phyerr_buf;
    int i;

    if (left_len < sizeof(*phyerr)) {
        ath10k_warn("wrong phyerr event head len %d (need: >=%zd)\n",
                    left_len, sizeof(*phyerr));
        return -EINVAL;
    }

    arg->tsf_timestamp = phyerr->tsf_timestamp;
    arg->freq1 = phyerr->freq1;
    arg->freq2 = phyerr->freq2;
    arg->rssi_combined = phyerr->rssi_combined;
    arg->chan_width_mhz = phyerr->chan_width_mhz;
    arg->buf_len = phyerr->buf_len;
    arg->buf = phyerr->buf;
    arg->hdr_len = sizeof(*phyerr);

    for (i = 0; i < 4; i++) {
        arg->nf_chains[i] = phyerr->nf_chains[i];
    }

    switch (phyerr->phy_err_code) {
    case PHY_ERROR_GEN_SPECTRAL_SCAN:
        arg->phy_err_code = PHY_ERROR_SPECTRAL_SCAN;
        break;
    case PHY_ERROR_GEN_FALSE_RADAR_EXT:
        arg->phy_err_code = PHY_ERROR_FALSE_RADAR_EXT;
        break;
    case PHY_ERROR_GEN_RADAR:
        arg->phy_err_code = PHY_ERROR_RADAR;
        break;
    default:
        arg->phy_err_code = PHY_ERROR_UNKNOWN;
        break;
    }

    return 0;
}

static int ath10k_wmi_10_4_op_pull_phyerr_ev(struct ath10k* ar,
        const void* phyerr_buf,
        int left_len,
        struct wmi_phyerr_ev_arg* arg) {
    const struct wmi_10_4_phyerr_event* phyerr = phyerr_buf;
    uint32_t phy_err_mask;
    int i;

    if (left_len < sizeof(*phyerr)) {
        ath10k_warn("wrong phyerr event head len %d (need: >=%zd)\n",
                    left_len, sizeof(*phyerr));
        return -EINVAL;
    }

    arg->tsf_timestamp = phyerr->tsf_timestamp;
    arg->freq1 = phyerr->freq1;
    arg->freq2 = phyerr->freq2;
    arg->rssi_combined = phyerr->rssi_combined;
    arg->chan_width_mhz = phyerr->chan_width_mhz;
    arg->buf_len = phyerr->buf_len;
    arg->buf = phyerr->buf;
    arg->hdr_len = sizeof(*phyerr);

    for (i = 0; i < 4; i++) {
        arg->nf_chains[i] = phyerr->nf_chains[i];
    }

    phy_err_mask = phyerr->phy_err_mask[0];

    if (phy_err_mask & PHY_ERROR_10_4_SPECTRAL_SCAN_MASK) {
        arg->phy_err_code = PHY_ERROR_SPECTRAL_SCAN;
    } else if (phy_err_mask & PHY_ERROR_10_4_RADAR_MASK) {
        arg->phy_err_code = PHY_ERROR_RADAR;
    } else {
        arg->phy_err_code = PHY_ERROR_UNKNOWN;
    }

    return 0;
}

void ath10k_wmi_event_phyerr(struct ath10k* ar, struct sk_buff* skb) {
    struct wmi_phyerr_hdr_arg hdr_arg = {};
    struct wmi_phyerr_ev_arg phyerr_arg = {};
    const void* phyerr;
    uint32_t count, i, buf_len, phy_err_code;
    uint64_t tsf;
    int left_len, ret;

    ATH10K_DFS_STAT_INC(ar, phy_errors);

    ret = ath10k_wmi_pull_phyerr_hdr(ar, skb, &hdr_arg);
    if (ret) {
        ath10k_warn("failed to parse phyerr event hdr: %d\n", ret);
        return;
    }

    /* Check number of included events */
    count = hdr_arg.num_phyerrs;

    left_len = hdr_arg.buf_len;

    tsf = hdr_arg.tsf_u32;
    tsf <<= 32;
    tsf |= hdr_arg.tsf_l32;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi event phyerr count %d tsf64 0x%llX\n",
               count, tsf);

    phyerr = hdr_arg.phyerrs;
    for (i = 0; i < count; i++) {
        ret = ath10k_wmi_pull_phyerr(ar, phyerr, left_len, &phyerr_arg);
        if (ret) {
            ath10k_warn("failed to parse phyerr event (%d)\n",
                        i);
            return;
        }

        left_len -= phyerr_arg.hdr_len;
        buf_len = phyerr_arg.buf_len;
        phy_err_code = phyerr_arg.phy_err_code;

        if (left_len < buf_len) {
            ath10k_warn("single event (%d) wrong buf len\n", i);
            return;
        }

        left_len -= buf_len;

        switch (phy_err_code) {
        case PHY_ERROR_RADAR:
            ath10k_wmi_event_dfs(ar, &phyerr_arg, tsf);
            break;
        case PHY_ERROR_SPECTRAL_SCAN:
            ath10k_wmi_event_spectral_scan(ar, &phyerr_arg, tsf);
            break;
        case PHY_ERROR_FALSE_RADAR_EXT:
            ath10k_wmi_event_dfs(ar, &phyerr_arg, tsf);
            ath10k_wmi_event_spectral_scan(ar, &phyerr_arg, tsf);
            break;
        default:
            break;
        }

        phyerr = phyerr + phyerr_arg.hdr_len + buf_len;
    }
}

void ath10k_wmi_event_roam(struct ath10k* ar, struct sk_buff* skb) {
    struct wmi_roam_ev_arg arg = {};
    int ret;
    uint32_t vdev_id;
    uint32_t reason;
    int32_t rssi;

    ret = ath10k_wmi_pull_roam_ev(ar, skb, &arg);
    if (ret) {
        ath10k_warn("failed to parse roam event: %d\n", ret);
        return;
    }

    vdev_id = arg.vdev_id;
    reason = arg.reason;
    rssi = arg.rssi;
    rssi += WMI_SPECTRAL_NOISE_FLOOR_REF_DEFAULT;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi roam event vdev %u reason 0x%08x rssi %d\n",
               vdev_id, reason, rssi);

    if (reason >= WMI_ROAM_REASON_MAX)
        ath10k_warn("ignoring unknown roam event reason %d on vdev %i\n",
                    reason, vdev_id);

    switch (reason) {
    case WMI_ROAM_REASON_BEACON_MISS:
        ath10k_mac_handle_beacon_miss(ar, vdev_id);
        break;
    case WMI_ROAM_REASON_BETTER_AP:
    case WMI_ROAM_REASON_LOW_RSSI:
    case WMI_ROAM_REASON_SUITABLE_AP_FOUND:
    case WMI_ROAM_REASON_HO_FAILED:
        ath10k_warn("ignoring not implemented roam event reason %d on vdev %i\n",
                    reason, vdev_id);
        break;
    }
}

void ath10k_wmi_event_profile_match(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_PROFILE_MATCH\n");
}

void ath10k_wmi_event_debug_print(struct ath10k* ar, struct sk_buff* skb) {
    char buf[101], c;
    int i;

    for (i = 0; i < sizeof(buf) - 1; i++) {
        if (i >= skb->len) {
            break;
        }

        c = skb->data[i];

        if (c == '\0') {
            break;
        }

        if (isascii(c) && isprint(c)) {
            buf[i] = c;
        } else {
            buf[i] = '.';
        }
    }

    if (i == sizeof(buf) - 1) {
        ath10k_warn("wmi debug print truncated: %d\n", skb->len);
    }

    /* for some reason the debug prints end with \n, remove that */
    if (skb->data[i - 1] == '\n') {
        i--;
    }

    /* the last byte is always reserved for the null character */
    buf[i] = '\0';

    ath10k_dbg(ar, ATH10K_DBG_WMI_PRINT, "wmi print '%s'\n", buf);
}

void ath10k_wmi_event_pdev_qvit(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_PDEV_QVIT_EVENTID\n");
}

void ath10k_wmi_event_wlan_profile_data(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_WLAN_PROFILE_DATA_EVENTID\n");
}

void ath10k_wmi_event_rtt_measurement_report(struct ath10k* ar,
        struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_RTT_MEASUREMENT_REPORT_EVENTID\n");
}

void ath10k_wmi_event_tsf_measurement_report(struct ath10k* ar,
        struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_TSF_MEASUREMENT_REPORT_EVENTID\n");
}

void ath10k_wmi_event_rtt_error_report(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_RTT_ERROR_REPORT_EVENTID\n");
}

void ath10k_wmi_event_wow_wakeup_host(struct ath10k* ar, struct sk_buff* skb) {
    struct wmi_wow_ev_arg ev = {};
    int ret;

    sync_completion_signal(&ar->wow.wakeup_completed);

    ret = ath10k_wmi_pull_wow_event(ar, skb, &ev);
    if (ret) {
        ath10k_warn("failed to parse wow wakeup event: %d\n", ret);
        return;
    }

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wow wakeup host reason %s\n",
               wow_reason(ev.wake_reason));
}

void ath10k_wmi_event_dcs_interference(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_DCS_INTERFERENCE_EVENTID\n");
}

static uint8_t ath10k_tpc_config_get_rate(struct ath10k* ar,
                                     struct wmi_pdev_tpc_config_event* ev,
                                     uint32_t rate_idx, uint32_t num_chains,
                                     uint32_t rate_code, uint8_t type) {
    uint8_t tpc, num_streams, preamble, ch, stm_idx;

    num_streams = ATH10K_HW_NSS(rate_code);
    preamble = ATH10K_HW_PREAMBLE(rate_code);
    ch = num_chains - 1;

    tpc = MIN_T(uint8_t, ev->rates_array[rate_idx], ev->max_reg_allow_pow[ch]);

    if (ev->num_tx_chain <= 1) {
        goto out;
    }

    if (preamble == WMI_RATE_PREAMBLE_CCK) {
        goto out;
    }

    stm_idx = num_streams - 1;
    if (num_chains <= num_streams) {
        goto out;
    }

    switch (type) {
    case WMI_TPC_TABLE_TYPE_STBC:
        tpc = MIN_T(uint8_t, tpc,
                    ev->max_reg_allow_pow_agstbc[ch - 1][stm_idx]);
        break;
    case WMI_TPC_TABLE_TYPE_TXBF:
        tpc = MIN_T(uint8_t, tpc,
                    ev->max_reg_allow_pow_agtxbf[ch - 1][stm_idx]);
        break;
    case WMI_TPC_TABLE_TYPE_CDD:
        tpc = MIN_T(uint8_t, tpc,
                    ev->max_reg_allow_pow_agcdd[ch - 1][stm_idx]);
        break;
    default:
        ath10k_warn("unknown wmi tpc table type: %d\n", type);
        tpc = 0;
        break;
    }

out:
    return tpc;
}

static void ath10k_tpc_config_disp_tables(struct ath10k* ar,
        struct wmi_pdev_tpc_config_event* ev,
        struct ath10k_tpc_stats* tpc_stats,
        uint8_t* rate_code, uint16_t* pream_table, uint8_t type) {
    uint32_t i, j, pream_idx, flags;
    uint8_t tpc[WMI_TPC_TX_N_CHAIN];
    char tpc_value[WMI_TPC_TX_N_CHAIN * WMI_TPC_BUF_SIZE];
    char buff[WMI_TPC_BUF_SIZE];

    flags = ev->flags;

    switch (type) {
    case WMI_TPC_TABLE_TYPE_CDD:
        if (!(flags & WMI_TPC_CONFIG_EVENT_FLAG_TABLE_CDD)) {
            ath10k_dbg(ar, ATH10K_DBG_WMI, "CDD not supported\n");
            tpc_stats->flag[type] = ATH10K_TPC_TABLE_TYPE_FLAG;
            return;
        }
        break;
    case WMI_TPC_TABLE_TYPE_STBC:
        if (!(flags & WMI_TPC_CONFIG_EVENT_FLAG_TABLE_STBC)) {
            ath10k_dbg(ar, ATH10K_DBG_WMI, "STBC not supported\n");
            tpc_stats->flag[type] = ATH10K_TPC_TABLE_TYPE_FLAG;
            return;
        }
        break;
    case WMI_TPC_TABLE_TYPE_TXBF:
        if (!(flags & WMI_TPC_CONFIG_EVENT_FLAG_TABLE_TXBF)) {
            ath10k_dbg(ar, ATH10K_DBG_WMI, "TXBF not supported\n");
            tpc_stats->flag[type] = ATH10K_TPC_TABLE_TYPE_FLAG;
            return;
        }
        break;
    default:
        ath10k_dbg(ar, ATH10K_DBG_WMI,
                   "invalid table type in wmi tpc event: %d\n", type);
        return;
    }

    pream_idx = 0;
    for (i = 0; i < ev->rate_max; i++) {
        memset(tpc_value, 0, sizeof(tpc_value));
        memset(buff, 0, sizeof(buff));
        if (i == pream_table[pream_idx]) {
            pream_idx++;
        }

        for (j = 0; j < WMI_TPC_TX_N_CHAIN; j++) {
            if (j >= ev->num_tx_chain) {
                break;
            }

            tpc[j] = ath10k_tpc_config_get_rate(ar, ev, i, j + 1,
                                                rate_code[i],
                                                type);
            snprintf(buff, sizeof(buff), "%8d ", tpc[j]);
            strncat(tpc_value, buff, strlen(buff));
        }
        tpc_stats->tpc_table[type].pream_idx[i] = pream_idx;
        tpc_stats->tpc_table[type].rate_code[i] = rate_code[i];
        memcpy(tpc_stats->tpc_table[type].tpc_value[i],
               tpc_value, sizeof(tpc_value));
    }
}

void ath10k_wmi_event_pdev_tpc_config(struct ath10k* ar, struct sk_buff* skb) {
    uint32_t i, j, pream_idx, num_tx_chain;
    uint8_t rate_code[WMI_TPC_RATE_MAX], rate_idx;
    uint16_t pream_table[WMI_TPC_PREAM_TABLE_MAX];
    struct wmi_pdev_tpc_config_event* ev;
    struct ath10k_tpc_stats* tpc_stats;

    ev = (struct wmi_pdev_tpc_config_event*)skb->data;

    tpc_stats = kzalloc(sizeof(*tpc_stats), GFP_ATOMIC);
    if (!tpc_stats) {
        return;
    }

    /* Create the rate code table based on the chains supported */
    rate_idx = 0;
    pream_idx = 0;

    /* Fill CCK rate code */
    for (i = 0; i < 4; i++) {
        rate_code[rate_idx] =
            ATH10K_HW_RATECODE(i, 0, WMI_RATE_PREAMBLE_CCK);
        rate_idx++;
    }
    pream_table[pream_idx] = rate_idx;
    pream_idx++;

    /* Fill OFDM rate code */
    for (i = 0; i < 8; i++) {
        rate_code[rate_idx] =
            ATH10K_HW_RATECODE(i, 0, WMI_RATE_PREAMBLE_OFDM);
        rate_idx++;
    }
    pream_table[pream_idx] = rate_idx;
    pream_idx++;

    num_tx_chain = ev->num_tx_chain;

    /* Fill HT20 rate code */
    for (i = 0; i < num_tx_chain; i++) {
        for (j = 0; j < 8; j++) {
            rate_code[rate_idx] =
                ATH10K_HW_RATECODE(j, i, WMI_RATE_PREAMBLE_HT);
            rate_idx++;
        }
    }
    pream_table[pream_idx] = rate_idx;
    pream_idx++;

    /* Fill HT40 rate code */
    for (i = 0; i < num_tx_chain; i++) {
        for (j = 0; j < 8; j++) {
            rate_code[rate_idx] =
                ATH10K_HW_RATECODE(j, i, WMI_RATE_PREAMBLE_HT);
            rate_idx++;
        }
    }
    pream_table[pream_idx] = rate_idx;
    pream_idx++;

    /* Fill VHT20 rate code */
    for (i = 0; i < ev->num_tx_chain; i++) {
        for (j = 0; j < 10; j++) {
            rate_code[rate_idx] =
                ATH10K_HW_RATECODE(j, i, WMI_RATE_PREAMBLE_VHT);
            rate_idx++;
        }
    }
    pream_table[pream_idx] = rate_idx;
    pream_idx++;

    /* Fill VHT40 rate code */
    for (i = 0; i < num_tx_chain; i++) {
        for (j = 0; j < 10; j++) {
            rate_code[rate_idx] =
                ATH10K_HW_RATECODE(j, i, WMI_RATE_PREAMBLE_VHT);
            rate_idx++;
        }
    }
    pream_table[pream_idx] = rate_idx;
    pream_idx++;

    /* Fill VHT80 rate code */
    for (i = 0; i < num_tx_chain; i++) {
        for (j = 0; j < 10; j++) {
            rate_code[rate_idx] =
                ATH10K_HW_RATECODE(j, i, WMI_RATE_PREAMBLE_VHT);
            rate_idx++;
        }
    }
    pream_table[pream_idx] = rate_idx;
    pream_idx++;

    rate_code[rate_idx++] =
        ATH10K_HW_RATECODE(0, 0, WMI_RATE_PREAMBLE_CCK);
    rate_code[rate_idx++] =
        ATH10K_HW_RATECODE(0, 0, WMI_RATE_PREAMBLE_OFDM);
    rate_code[rate_idx++] =
        ATH10K_HW_RATECODE(0, 0, WMI_RATE_PREAMBLE_CCK);
    rate_code[rate_idx++] =
        ATH10K_HW_RATECODE(0, 0, WMI_RATE_PREAMBLE_OFDM);
    rate_code[rate_idx++] =
        ATH10K_HW_RATECODE(0, 0, WMI_RATE_PREAMBLE_OFDM);

    pream_table[pream_idx] = ATH10K_TPC_PREAM_TABLE_END;

    tpc_stats->chan_freq = ev->chan_freq;
    tpc_stats->phy_mode = ev->phy_mode;
    tpc_stats->ctl = ev->ctl;
    tpc_stats->reg_domain = ev->reg_domain;
    tpc_stats->twice_antenna_gain = ev->twice_antenna_gain;
    tpc_stats->twice_antenna_reduction =
        ev->twice_antenna_reduction;
    tpc_stats->power_limit = ev->power_limit;
    tpc_stats->twice_max_rd_power = ev->twice_max_rd_power;
    tpc_stats->num_tx_chain = ev->num_tx_chain;
    tpc_stats->rate_max = ev->rate_max;

    ath10k_tpc_config_disp_tables(ar, ev, tpc_stats,
                                  rate_code, pream_table,
                                  WMI_TPC_TABLE_TYPE_CDD);
    ath10k_tpc_config_disp_tables(ar, ev,  tpc_stats,
                                  rate_code, pream_table,
                                  WMI_TPC_TABLE_TYPE_STBC);
    ath10k_tpc_config_disp_tables(ar, ev, tpc_stats,
                                  rate_code, pream_table,
                                  WMI_TPC_TABLE_TYPE_TXBF);

    ath10k_debug_tpc_stats_process(ar, tpc_stats);

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi event tpc config channel %d mode %d ctl %d regd %d gain %d %d limit %d max_power %d tx_chanins %d rates %d\n",
               ev->chan_freq,
               ev->phy_mode,
               ev->ctl,
               ev->reg_domain,
               ev->twice_antenna_gain,
               ev->twice_antenna_reduction,
               ev->power_limit,
               ev->twice_max_rd_power / 2,
               ev->num_tx_chain,
               ev->rate_max);
}

void ath10k_wmi_event_pdev_ftm_intg(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_PDEV_FTM_INTG_EVENTID\n");
}

void ath10k_wmi_event_gtk_offload_status(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_GTK_OFFLOAD_STATUS_EVENTID\n");
}

void ath10k_wmi_event_gtk_rekey_fail(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_GTK_REKEY_FAIL_EVENTID\n");
}

void ath10k_wmi_event_delba_complete(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_TX_DELBA_COMPLETE_EVENTID\n");
}

void ath10k_wmi_event_addba_complete(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_TX_ADDBA_COMPLETE_EVENTID\n");
}
#endif  // NEEDS PORTING

void ath10k_wmi_event_vdev_install_key_complete(struct ath10k* ar, struct ath10k_msg_buf* msg_buf) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_VDEV_INSTALL_KEY_COMPLETE_EVENTID\n");
}

#if 0   // NEEDS PORTING
void ath10k_wmi_event_inst_rssi_stats(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_INST_RSSI_STATS_EVENTID\n");
}

void ath10k_wmi_event_vdev_standby_req(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_VDEV_STANDBY_REQ_EVENTID\n");
}

void ath10k_wmi_event_vdev_resume_req(struct ath10k* ar, struct sk_buff* skb) {
    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI_VDEV_RESUME_REQ_EVENTID\n");
}
#endif  // NEEDS PORTING

static zx_status_t ath10k_wmi_alloc_host_mem(struct ath10k* ar, uint32_t req_id, uint32_t num_units,
                                             uint32_t unit_len) {
    uint32_t pool_size;
    zx_status_t ret;
    unsigned int idx = ar->wmi.num_mem_chunks;
    ZX_ASSERT(idx < countof(ar->wmi.mem_chunks));

    zx_handle_t bti_handle;
    ret = ath10k_hif_get_bti_handle(ar, &bti_handle);
    if (ret != ZX_OK) { return ret; }

    struct ath10k_mem_chunk* chunk = &ar->wmi.mem_chunks[idx];
    pool_size = num_units * ROUNDUP(unit_len, 4);
    ret = io_buffer_init(&chunk->handle, bti_handle, pool_size, IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (ret != ZX_OK) { return ret; }

    chunk->paddr = io_buffer_phys(&chunk->handle);
    if (chunk->paddr + pool_size > 0x100000000ULL) {
        ath10k_err("io buffer allocated with address above 32b range (see ZX-1073)\n");
        io_buffer_release(&chunk->handle);
        return ZX_ERR_NO_MEMORY;
    }
    chunk->vaddr = io_buffer_virt(&chunk->handle);
    chunk->len = pool_size;
    chunk->req_id = req_id;

    memset(chunk->vaddr, 0, pool_size);

    ar->wmi.num_mem_chunks++;

    return ZX_OK;
}

static bool ath10k_wmi_is_host_mem_allocated(struct ath10k* ar,
                                             const struct wlan_host_mem_req** mem_reqs,
                                             uint32_t num_mem_reqs) {
    uint32_t req_id, num_units, unit_size, num_unit_info;
    uint32_t pool_size;
    unsigned int i, j;
    bool found;

    if (ar->wmi.num_mem_chunks != num_mem_reqs) { return false; }

    for (i = 0; i < num_mem_reqs; ++i) {
        req_id = mem_reqs[i]->req_id;
        num_units = mem_reqs[i]->num_units;
        unit_size = mem_reqs[i]->unit_size;
        num_unit_info = mem_reqs[i]->num_unit_info;

        if (num_unit_info & NUM_UNITS_IS_NUM_ACTIVE_PEERS) {
            if (ar->num_active_peers) {
                num_units = ar->num_active_peers + 1;
            } else {
                num_units = ar->max_num_peers + 1;
            }
        } else if (num_unit_info & NUM_UNITS_IS_NUM_PEERS) {
            num_units = ar->max_num_peers + 1;
        } else if (num_unit_info & NUM_UNITS_IS_NUM_VDEVS) {
            num_units = ar->max_num_vdevs + 1;
        }

        found = false;
        for (j = 0; j < ar->wmi.num_mem_chunks; j++) {
            if (ar->wmi.mem_chunks[j].req_id == req_id) {
                pool_size = num_units * ROUNDUP(unit_size, 4);
                if (ar->wmi.mem_chunks[j].len == pool_size) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) { return false; }
    }

    return true;
}

static zx_status_t ath10k_wmi_main_op_pull_svc_rdy_ev(struct ath10k* ar,
                                                      struct ath10k_msg_buf* msg_buf,
                                                      struct wmi_svc_rdy_ev_arg* arg) {
    struct wmi_service_ready_event* ev;
    size_t i, n;
    size_t buffer_left = ath10k_msg_buf_get_payload_len(msg_buf, ATH10K_MSG_TYPE_WMI);

    if (buffer_left < sizeof(*ev)) { return ZX_ERR_INVALID_ARGS; }

    ev = ath10k_msg_buf_get_payload(msg_buf);
    arg->min_tx_power = ev->hw_min_tx_power;
    arg->max_tx_power = ev->hw_max_tx_power;
    arg->ht_cap = ev->ht_cap_info;
    arg->vht_cap = ev->vht_cap_info;
    arg->sw_ver0 = ev->sw_version;
    arg->sw_ver1 = ev->sw_version_1;
    arg->phy_capab = ev->phy_capability;
    arg->num_rf_chains = ev->num_rf_chains;
    arg->eeprom_rd = ev->hal_reg_capabilities.eeprom_rd;
    arg->low_5ghz_chan = ev->hal_reg_capabilities.low_5ghz_chan;
    arg->high_5ghz_chan = ev->hal_reg_capabilities.high_5ghz_chan;
    arg->num_mem_reqs = ev->num_mem_reqs;
    arg->service_map = ev->wmi_service_bitmap;
    arg->service_map_len = sizeof(ev->wmi_service_bitmap);

    n = MIN_T(size_t, arg->num_mem_reqs, countof(arg->mem_reqs));
    for (i = 0; i < n; i++) {
        arg->mem_reqs[i] = &ev->mem_reqs[i];
    }

    buffer_left -= sizeof(*ev);
    if (buffer_left < arg->num_mem_reqs * sizeof(arg->mem_reqs[0])) { return ZX_ERR_INVALID_ARGS; }

    return ZX_OK;
}

static zx_status_t ath10k_wmi_10x_op_pull_svc_rdy_ev(struct ath10k* ar,
                                                     struct ath10k_msg_buf* msg_buf,
                                                     struct wmi_svc_rdy_ev_arg* arg) {
    struct wmi_10x_service_ready_event* ev;
    int i, n;
    size_t buffer_left = ath10k_msg_buf_get_payload_len(msg_buf, ATH10K_MSG_TYPE_WMI);

    if (buffer_left < sizeof(*ev)) { return ZX_ERR_INVALID_ARGS; }

    ev = ath10k_msg_buf_get_payload(msg_buf);
    arg->min_tx_power = ev->hw_min_tx_power;
    arg->max_tx_power = ev->hw_max_tx_power;
    arg->ht_cap = ev->ht_cap_info;
    arg->vht_cap = ev->vht_cap_info;
    arg->sw_ver0 = ev->sw_version;
    arg->phy_capab = ev->phy_capability;
    arg->num_rf_chains = ev->num_rf_chains;
    arg->eeprom_rd = ev->hal_reg_capabilities.eeprom_rd;
    arg->low_5ghz_chan = ev->hal_reg_capabilities.low_5ghz_chan;
    arg->high_5ghz_chan = ev->hal_reg_capabilities.high_5ghz_chan;
    arg->num_mem_reqs = ev->num_mem_reqs;
    arg->service_map = ev->wmi_service_bitmap;
    arg->service_map_len = sizeof(ev->wmi_service_bitmap);

    n = MIN_T(size_t, arg->num_mem_reqs, countof(arg->mem_reqs));
    for (i = 0; i < n; i++) {
        arg->mem_reqs[i] = &ev->mem_reqs[i];
    }

    buffer_left -= sizeof(*ev);
    if (buffer_left < arg->num_mem_reqs * sizeof(arg->mem_reqs[0])) { return ZX_ERR_INVALID_ARGS; }

    return ZX_OK;
}

static int ath10k_wmi_event_service_ready_work(void* thrd_init_param) {
    struct ath10k* ar = thrd_init_param;
    struct ath10k_msg_buf* buf = ar->svc_rdy_buf;
    struct wmi_svc_rdy_ev_arg arg = {};
    uint32_t num_units, req_id, unit_size, num_mem_reqs, num_unit_info, i;
    int ret;
    bool allocated;

    if (!buf) {
        ath10k_warn("invalid service ready event msg buf\n");
        return -1;
    }

    struct ath10k_htc_hdr* htc_hdr = ath10k_msg_buf_get_header(buf, ATH10K_MSG_TYPE_HTC);
    buf->used = sizeof(struct ath10k_htc_hdr) + htc_hdr->len;
    ZX_DEBUG_ASSERT(buf->used <= buf->capacity);

    ret = ath10k_wmi_pull_svc_rdy(ar, buf, &arg);
    if (ret != ZX_OK) {
        ath10k_warn("failed to parse service ready: %s\n", zx_status_get_string(ret));
        return -1;
    }

    memset(&ar->wmi.svc_map, 0, sizeof(ar->wmi.svc_map));
    ath10k_wmi_map_svc(ar, arg.service_map, ar->wmi.svc_map, arg.service_map_len);

    ar->hw_min_tx_power = arg.min_tx_power;
    ar->hw_max_tx_power = arg.max_tx_power;
    ar->ht_cap_info = arg.ht_cap;
    ar->vht_cap_info = arg.vht_cap;
    ar->fw_version_major = (arg.sw_ver0 & 0xff000000) >> 24;
    ar->fw_version_minor = (arg.sw_ver0 & 0x00ffffff);
    ar->fw_version_release = (arg.sw_ver1 & 0xffff0000) >> 16;
    ar->fw_version_build = (arg.sw_ver1 & 0x0000ffff);
    ar->phy_capability = arg.phy_capab;
    ar->num_rf_chains = arg.num_rf_chains;
    ar->hw_eeprom_rd = arg.eeprom_rd;
    ar->low_5ghz_chan = arg.low_5ghz_chan;
    ar->high_5ghz_chan = arg.high_5ghz_chan;

    ath10k_dbg_dump(ar, ATH10K_DBG_WMI, NULL, "wmi svc: ", arg.service_map, arg.service_map_len);

    if (ar->num_rf_chains > ar->max_spatial_stream) {
        ath10k_warn(
            "hardware advertises support for more spatial streams than it should (%d > %d)\n",
            ar->num_rf_chains, ar->max_spatial_stream);
        ar->num_rf_chains = ar->max_spatial_stream;
    }

    if (!ar->cfg_tx_chainmask) {
        ar->cfg_tx_chainmask = (1 << ar->num_rf_chains) - 1;
        ar->cfg_rx_chainmask = (1 << ar->num_rf_chains) - 1;
    }

    num_mem_reqs = arg.num_mem_reqs;
    if (num_mem_reqs > WMI_MAX_MEM_REQS) {
        ath10k_warn("requested memory chunks number (%d) exceeds the limit\n", num_mem_reqs);
        return -1;
    }

    if (BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_PEER_CACHING)) {
        if (BITARR_TEST(ar->running_fw->fw_file.fw_features, ATH10K_FW_FEATURE_PEER_FLOW_CONTROL)) {
            ar->num_active_peers = TARGET_10_4_QCACHE_ACTIVE_PEERS_PFC + ar->max_num_vdevs;
        } else {
            ar->num_active_peers = TARGET_10_4_QCACHE_ACTIVE_PEERS + ar->max_num_vdevs;
        }

        ar->max_num_peers = TARGET_10_4_NUM_QCACHE_PEERS_MAX + ar->max_num_vdevs;
        ar->num_tids = ar->num_active_peers * 2;
        ar->max_num_stations = TARGET_10_4_NUM_QCACHE_PEERS_MAX;
    }

    /* TODO: Adjust max peer count for cases like WMI_SERVICE_RATECTRL_CACHE
     * and WMI_SERVICE_IRAM_TIDS, etc.
     */

    allocated = ath10k_wmi_is_host_mem_allocated(ar, arg.mem_reqs, num_mem_reqs);
    if (allocated) { goto skip_mem_alloc; }

    /* Either this event is received during boot time or there is a change
     * in memory requirement from firmware when compared to last request.
     * Free any old memory and do a fresh allocation based on the current
     * memory requirement.
     */
    ath10k_wmi_free_host_mem(ar);

    for (i = 0; i < num_mem_reqs; ++i) {
        req_id = arg.mem_reqs[i]->req_id;
        num_units = arg.mem_reqs[i]->num_units;
        unit_size = arg.mem_reqs[i]->unit_size;
        num_unit_info = arg.mem_reqs[i]->num_unit_info;

        if (num_unit_info & NUM_UNITS_IS_NUM_ACTIVE_PEERS) {
            if (ar->num_active_peers) {
                num_units = ar->num_active_peers + 1;
            } else {
                num_units = ar->max_num_peers + 1;
            }
        } else if (num_unit_info & NUM_UNITS_IS_NUM_PEERS) {
            /* number of units to allocate is number of
             * peers, 1 extra for self peer on target
             * this needs to be tied, host and target
             * can get out of sync
             */
            num_units = ar->max_num_peers + 1;
        } else if (num_unit_info & NUM_UNITS_IS_NUM_VDEVS) {
            num_units = ar->max_num_vdevs + 1;
        }

        ath10k_dbg(ar, ATH10K_DBG_WMI,
                   "wmi mem_req_id %d num_units %d num_unit_info %d unit size %d actual units %d\n",
                   req_id, arg.mem_reqs[i]->num_units, num_unit_info, unit_size, num_units);

        ret = ath10k_wmi_alloc_host_mem(ar, req_id, num_units, unit_size);
        if (ret != ZX_OK) { return -1; }
    }

skip_mem_alloc:
    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi event service ready min_tx_power 0x%08x max_tx_power 0x%08x ht_cap 0x%08x "
               "vht_cap 0x%08x sw_ver0 0x%08x sw_ver1 0x%08x fw_build 0x%08x phy_capab 0x%08x "
               "num_rf_chains 0x%08x eeprom_rd 0x%08x num_mem_reqs 0x%08x\n",
               arg.min_tx_power, arg.max_tx_power, arg.ht_cap, arg.vht_cap, arg.sw_ver0,
               arg.sw_ver1, arg.fw_build, arg.phy_capab, arg.num_rf_chains, arg.eeprom_rd,
               arg.num_mem_reqs);

    ath10k_msg_buf_free(buf);
    ar->svc_rdy_buf = NULL;
    sync_completion_signal(&ar->wmi.service_ready);
    return 0;
}

void ath10k_wmi_event_service_ready(struct ath10k* ar, struct ath10k_msg_buf* buf) {
    thrd_t wmi_rdy_thrd;
    ar->svc_rdy_buf = buf;
    // TODO: Optimize thread handling (NET-708)
    thrd_create_with_name(&wmi_rdy_thrd, ath10k_wmi_event_service_ready_work, ar,
                          "ath10k-wmi-ready");
    thrd_detach(wmi_rdy_thrd);
}

static zx_status_t ath10k_wmi_op_pull_rdy_ev(struct ath10k* ar, struct ath10k_msg_buf* msg_buf,
                                             struct wmi_rdy_ev_arg* arg) {
    struct wmi_ready_event* ev = ath10k_msg_buf_get_payload(msg_buf);
    size_t msg_len = ath10k_msg_buf_get_payload_len(msg_buf, ATH10K_MSG_TYPE_WMI);

    if (msg_len < sizeof(*ev)) { return ZX_ERR_INVALID_ARGS; }

    arg->sw_version = ev->sw_version;
    arg->abi_version = ev->abi_version;
    arg->status = ev->status;
    arg->mac_addr = ev->mac_addr.addr;

    return ZX_OK;
}

static zx_status_t ath10k_wmi_op_pull_roam_ev(struct ath10k* ar, struct ath10k_msg_buf* msg_buf,
                                              struct wmi_roam_ev_arg* arg) {
    struct wmi_roam_ev* ev;

    if (ath10k_msg_buf_get_payload_len(msg_buf, ATH10K_MSG_TYPE_WMI) < sizeof(*ev)) {
        return ZX_ERR_INVALID_ARGS;
    }

    ev = ath10k_msg_buf_get_payload(msg_buf);
    arg->vdev_id = ev->vdev_id;
    arg->reason = ev->reason;

    return ZX_OK;
}

static zx_status_t ath10k_wmi_op_pull_echo_ev(struct ath10k* ar, struct ath10k_msg_buf* msg_buf,
                                              struct wmi_echo_ev_arg* arg) {
    struct wmi_echo_event* ev;

    if (ath10k_msg_buf_get_payload_len(msg_buf, ATH10K_MSG_TYPE_WMI) < sizeof(*ev)) {
        return ZX_ERR_INVALID_ARGS;
    }

    ev = ath10k_msg_buf_get_payload(msg_buf);
    arg->value = ev->value;

    return ZX_OK;
}

zx_status_t ath10k_wmi_event_ready(struct ath10k* ar, struct ath10k_msg_buf* msg_buf) {
    struct wmi_rdy_ev_arg arg = {};
    zx_status_t ret;

    ret = ath10k_wmi_pull_rdy(ar, msg_buf, &arg);
    if (ret != ZX_OK) {
        ath10k_warn("failed to parse ready event: %s\n", zx_status_get_string(ret));
        return ret;
    }

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi event ready sw_version %u abi_version %u mac_addr %pM status %d\n",
               arg.sw_version, arg.abi_version, arg.mac_addr, arg.status);

    memcpy(ar->mac_addr, arg.mac_addr, ETH_ALEN);
    sync_completion_signal(&ar->wmi.unified_ready);
    return 0;
}

#if 0   // NEEDS PORTING
static int ath10k_wmi_event_temperature(struct ath10k* ar, struct sk_buff* skb) {
    const struct wmi_pdev_temperature_event* ev;

    ev = (struct wmi_pdev_temperature_event*)skb->data;
    if (COND_WARN(skb->len < sizeof(*ev))) {
        return -EPROTO;
    }

    ath10k_thermal_event_temperature(ar, ev->temperature);
    return 0;
}

static int ath10k_wmi_event_pdev_bss_chan_info(struct ath10k* ar,
        struct sk_buff* skb) {
    struct wmi_pdev_bss_chan_info_event* ev;
    struct survey_info* survey;
    uint64_t busy, total, tx, rx, rx_bss;
    uint32_t freq, noise_floor;
    uint32_t cc_freq_hz = ar->hw_params.channel_counters_freq_hz;
    int idx;

    ev = (struct wmi_pdev_bss_chan_info_event*)skb->data;
    if (COND_WARN(skb->len < sizeof(*ev))) {
        return -EPROTO;
    }

    freq        = ev->freq;
    noise_floor = ev->noise_floor;
    busy        = ev->cycle_busy;
    total       = ev->cycle_total;
    tx          = ev->cycle_tx;
    rx          = ev->cycle_rx;
    rx_bss      = ev->cycle_rx_bss;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi event pdev bss chan info:\n freq: %d noise: %d cycle: busy %llu total %llu tx %llu rx %llu rx_bss %llu\n",
               freq, noise_floor, busy, total, tx, rx, rx_bss);

    mtx_lock(&ar->data_lock);
    idx = freq_to_idx(ar, freq);
    if (idx >= countof(ar->survey)) {
        ath10k_warn("bss chan info: invalid frequency %d (idx %d out of bounds)\n",
                    freq, idx);
        goto exit;
    }

    survey = &ar->survey[idx];

    survey->noise     = noise_floor;
    survey->time      = div_u64(total, cc_freq_hz);
    survey->time_busy = div_u64(busy, cc_freq_hz);
    survey->time_rx   = div_u64(rx_bss, cc_freq_hz);
    survey->time_tx   = div_u64(tx, cc_freq_hz);
    survey->filled   |= (SURVEY_INFO_NOISE_DBM |
                         SURVEY_INFO_TIME |
                         SURVEY_INFO_TIME_BUSY |
                         SURVEY_INFO_TIME_RX |
                         SURVEY_INFO_TIME_TX);
exit:
    mtx_unlock(&ar->data_lock);
    sync_completion_signal(&ar->bss_survey_done);
    return 0;
}
#endif  // NEEDS PORTING

static inline void ath10k_wmi_queue_set_coverage_class_work(struct ath10k* ar) {
    if (ar->hw_params.hw_ops->set_coverage_class) {
#if 0  // NEEDS PORTING
        mtx_lock(&ar->data_lock);

        /* This call only ensures that the modified coverage class
         * persists in case the firmware sets the registers back to
         * their default value. So calling it is only necessary if the
         * coverage class has a non-zero value.
         */
        if (ar->fw_coverage.coverage_class) {
            queue_work(ar->workqueue, &ar->set_coverage_class_work);
        }

        mtx_unlock(&ar->data_lock);
#endif
    }
}

static void ath10k_wmi_op_rx(struct ath10k* ar, struct ath10k_msg_buf* buf) {
    struct wmi_cmd_hdr* cmd_hdr;
    enum wmi_event_id id;

    cmd_hdr = ath10k_msg_buf_get_header(buf, ATH10K_MSG_TYPE_WMI);
    id = MS(cmd_hdr->cmd_id, WMI_CMD_HDR_CMD_ID);

    if (ath10k_msg_buf_get_payload_offset(ATH10K_MSG_TYPE_WMI) > buf->used) { goto out; }

    buf->type = ATH10K_MSG_TYPE_WMI;

    switch (id) {
#if 0   // NEEDS PORTING
    case WMI_MGMT_RX_EVENTID:
        ath10k_wmi_event_mgmt_rx(ar, buf);
        /* mgmt_rx() owns the skb now! */
        return;
    case WMI_SCAN_EVENTID:
        ath10k_wmi_event_scan(ar, buf);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_CHAN_INFO_EVENTID:
        ath10k_wmi_event_chan_info(ar, buf);
        break;
    case WMI_ECHO_EVENTID:
        ath10k_wmi_event_echo(ar, buf);
        break;
    case WMI_DEBUG_MESG_EVENTID:
        ath10k_wmi_event_debug_mesg(ar, buf);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_UPDATE_STATS_EVENTID:
        ath10k_wmi_event_update_stats(ar, buf);
        break;
    case WMI_VDEV_START_RESP_EVENTID:
        ath10k_wmi_event_vdev_start_resp(ar, buf);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_VDEV_STOPPED_EVENTID:
        ath10k_wmi_event_vdev_stopped(ar, buf);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_PEER_STA_KICKOUT_EVENTID:
        ath10k_wmi_event_peer_sta_kickout(ar, buf);
        break;
    case WMI_HOST_SWBA_EVENTID:
        ath10k_wmi_event_host_swba(ar, buf);
        break;
    case WMI_TBTTOFFSET_UPDATE_EVENTID:
        ath10k_wmi_event_tbttoffset_update(ar, buf);
        break;
    case WMI_PHYERR_EVENTID:
        ath10k_wmi_event_phyerr(ar, buf);
        break;
    case WMI_ROAM_EVENTID:
        ath10k_wmi_event_roam(ar, buf);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_PROFILE_MATCH:
        ath10k_wmi_event_profile_match(ar, buf);
        break;
    case WMI_DEBUG_PRINT_EVENTID:
        ath10k_wmi_event_debug_print(ar, buf);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_PDEV_QVIT_EVENTID:
        ath10k_wmi_event_pdev_qvit(ar, buf);
        break;
    case WMI_WLAN_PROFILE_DATA_EVENTID:
        ath10k_wmi_event_wlan_profile_data(ar, buf);
        break;
    case WMI_RTT_MEASUREMENT_REPORT_EVENTID:
        ath10k_wmi_event_rtt_measurement_report(ar, buf);
        break;
    case WMI_TSF_MEASUREMENT_REPORT_EVENTID:
        ath10k_wmi_event_tsf_measurement_report(ar, buf);
        break;
    case WMI_RTT_ERROR_REPORT_EVENTID:
        ath10k_wmi_event_rtt_error_report(ar, buf);
        break;
    case WMI_WOW_WAKEUP_HOST_EVENTID:
        ath10k_wmi_event_wow_wakeup_host(ar, buf);
        break;
    case WMI_DCS_INTERFERENCE_EVENTID:
        ath10k_wmi_event_dcs_interference(ar, buf);
        break;
    case WMI_PDEV_TPC_CONFIG_EVENTID:
        ath10k_wmi_event_pdev_tpc_config(ar, buf);
        break;
    case WMI_PDEV_FTM_INTG_EVENTID:
        ath10k_wmi_event_pdev_ftm_intg(ar, buf);
        break;
    case WMI_GTK_OFFLOAD_STATUS_EVENTID:
        ath10k_wmi_event_gtk_offload_status(ar, buf);
        break;
    case WMI_GTK_REKEY_FAIL_EVENTID:
        ath10k_wmi_event_gtk_rekey_fail(ar, buf);
        break;
    case WMI_TX_DELBA_COMPLETE_EVENTID:
        ath10k_wmi_event_delba_complete(ar, buf);
        break;
    case WMI_TX_ADDBA_COMPLETE_EVENTID:
        ath10k_wmi_event_addba_complete(ar, buf);
        break;
    case WMI_VDEV_INSTALL_KEY_COMPLETE_EVENTID:
        ath10k_wmi_event_vdev_install_key_complete(ar, buf);
        break;
#endif  // NEEDS PORTING
    case WMI_SERVICE_READY_EVENTID:
        ath10k_wmi_event_service_ready(ar, buf);
        return;
#if 0   // NEEDS PORTING
    case WMI_READY_EVENTID:
        ath10k_wmi_event_ready(ar, buf);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
#endif  // NEEDS PORTING
    default:
        ath10k_warn("Unknown eventid: %d\n", id);
        break;
    }

out:
    ath10k_msg_buf_free(buf);
}

#if 0   // NEEDS PORTING
static void ath10k_wmi_10_1_op_rx(struct ath10k* ar, struct sk_buff* skb) {
    struct wmi_cmd_hdr* cmd_hdr;
    enum wmi_10x_event_id id;
    bool consumed;

    cmd_hdr = (struct wmi_cmd_hdr*)skb->data;
    id = MS(cmd_hdr->cmd_id, WMI_CMD_HDR_CMD_ID);

    if (skb_pull(skb, sizeof(struct wmi_cmd_hdr)) == NULL) {
        goto out;
    }

    trace_ath10k_wmi_event(ar, id, skb->data, skb->len);

    consumed = ath10k_tm_event_wmi(ar, id, skb);

    /* Ready event must be handled normally also in UTF mode so that we
     * know the UTF firmware has booted, others we are just bypass WMI
     * events to testmode.
     */
    if (consumed && id != WMI_10X_READY_EVENTID) {
        ath10k_dbg(ar, ATH10K_DBG_WMI,
                   "wmi testmode consumed 0x%x\n", id);
        goto out;
    }

    switch (id) {
    case WMI_10X_MGMT_RX_EVENTID:
        ath10k_wmi_event_mgmt_rx(ar, skb);
        /* mgmt_rx() owns the skb now! */
        return;
    case WMI_10X_SCAN_EVENTID:
        ath10k_wmi_event_scan(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10X_CHAN_INFO_EVENTID:
        ath10k_wmi_event_chan_info(ar, skb);
        break;
    case WMI_10X_ECHO_EVENTID:
        ath10k_wmi_event_echo(ar, skb);
        break;
    case WMI_10X_DEBUG_MESG_EVENTID:
        ath10k_wmi_event_debug_mesg(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10X_UPDATE_STATS_EVENTID:
        ath10k_wmi_event_update_stats(ar, skb);
        break;
    case WMI_10X_VDEV_START_RESP_EVENTID:
        ath10k_wmi_event_vdev_start_resp(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10X_VDEV_STOPPED_EVENTID:
        ath10k_wmi_event_vdev_stopped(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10X_PEER_STA_KICKOUT_EVENTID:
        ath10k_wmi_event_peer_sta_kickout(ar, skb);
        break;
    case WMI_10X_HOST_SWBA_EVENTID:
        ath10k_wmi_event_host_swba(ar, skb);
        break;
    case WMI_10X_TBTTOFFSET_UPDATE_EVENTID:
        ath10k_wmi_event_tbttoffset_update(ar, skb);
        break;
    case WMI_10X_PHYERR_EVENTID:
        ath10k_wmi_event_phyerr(ar, skb);
        break;
    case WMI_10X_ROAM_EVENTID:
        ath10k_wmi_event_roam(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10X_PROFILE_MATCH:
        ath10k_wmi_event_profile_match(ar, skb);
        break;
    case WMI_10X_DEBUG_PRINT_EVENTID:
        ath10k_wmi_event_debug_print(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10X_PDEV_QVIT_EVENTID:
        ath10k_wmi_event_pdev_qvit(ar, skb);
        break;
    case WMI_10X_WLAN_PROFILE_DATA_EVENTID:
        ath10k_wmi_event_wlan_profile_data(ar, skb);
        break;
    case WMI_10X_RTT_MEASUREMENT_REPORT_EVENTID:
        ath10k_wmi_event_rtt_measurement_report(ar, skb);
        break;
    case WMI_10X_TSF_MEASUREMENT_REPORT_EVENTID:
        ath10k_wmi_event_tsf_measurement_report(ar, skb);
        break;
    case WMI_10X_RTT_ERROR_REPORT_EVENTID:
        ath10k_wmi_event_rtt_error_report(ar, skb);
        break;
    case WMI_10X_WOW_WAKEUP_HOST_EVENTID:
        ath10k_wmi_event_wow_wakeup_host(ar, skb);
        break;
    case WMI_10X_DCS_INTERFERENCE_EVENTID:
        ath10k_wmi_event_dcs_interference(ar, skb);
        break;
    case WMI_10X_PDEV_TPC_CONFIG_EVENTID:
        ath10k_wmi_event_pdev_tpc_config(ar, skb);
        break;
    case WMI_10X_INST_RSSI_STATS_EVENTID:
        ath10k_wmi_event_inst_rssi_stats(ar, skb);
        break;
    case WMI_10X_VDEV_STANDBY_REQ_EVENTID:
        ath10k_wmi_event_vdev_standby_req(ar, skb);
        break;
    case WMI_10X_VDEV_RESUME_REQ_EVENTID:
        ath10k_wmi_event_vdev_resume_req(ar, skb);
        break;
    case WMI_10X_SERVICE_READY_EVENTID:
        ath10k_wmi_event_service_ready(ar, skb);
        return;
    case WMI_10X_READY_EVENTID:
        ath10k_wmi_event_ready(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10X_PDEV_UTF_EVENTID:
        /* ignore utf events */
        break;
    default:
        ath10k_warn("Unknown eventid: %d\n", id);
        break;
    }

out:
    dev_kfree_skb(skb);
}
#endif  // NEEDS PORTING

static void ath10k_wmi_10_2_op_rx(struct ath10k* ar, struct ath10k_msg_buf* msg_buf) {
    struct wmi_cmd_hdr* cmd_hdr;
    enum wmi_10_2_event_id id;
    bool consumed;

    if (ath10k_msg_buf_get_payload_offset(ATH10K_MSG_TYPE_WMI) > msg_buf->used) { goto out; }

    msg_buf->type = ATH10K_MSG_TYPE_WMI;
    cmd_hdr = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI);
    id = MS(cmd_hdr->cmd_id, WMI_CMD_HDR_CMD_ID);

    consumed = ath10k_tm_event_wmi(ar, id, msg_buf);

    /* Ready event must be handled normally also in UTF mode so that we
     * know the UTF firmware has booted, others we are just bypass WMI
     * events to testmode.
     */
    if (consumed && id != WMI_10_2_READY_EVENTID) {
        ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi testmode consumed 0x%x\n", id);
        goto out;
    }

    switch (id) {
    case WMI_10_2_MGMT_RX_EVENTID:
        ath10k_wmi_event_mgmt_rx(ar, msg_buf);
        /* mgmt_rx() owns the skb now! */
        return;
    case WMI_10_2_SCAN_EVENTID:
        ath10k_wmi_event_scan(ar, msg_buf);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_2_CHAN_INFO_EVENTID:
        ath10k_wmi_event_chan_info(ar, msg_buf);
        break;
    case WMI_10_2_ECHO_EVENTID:
        ath10k_wmi_event_echo(ar, msg_buf);
        break;
    case WMI_10_2_DEBUG_MESG_EVENTID:
        ath10k_err("WMI_10_2_DEBUG_MESG_EVENTID unimplemented\n");
        // ath10k_wmi_event_debug_mesg(ar, skb);
        // ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_2_UPDATE_STATS_EVENTID:
        ath10k_err("WMI_10_2_UPDATE_STATS_EVENTID unimplemented\n");
        // ath10k_wmi_event_update_stats(ar, skb);
        break;
    case WMI_10_2_VDEV_START_RESP_EVENTID:
        ath10k_wmi_event_vdev_start_resp(ar, msg_buf);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_2_VDEV_STOPPED_EVENTID:
        ath10k_err("WMI_10_2_VDEV_STOPPED_EVENTID unimplemented\n");
        ath10k_wmi_event_vdev_stopped(ar, msg_buf);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_2_PEER_STA_KICKOUT_EVENTID:
        ath10k_err("WMI_10_2_PEER_STA_KICKOUT_EVENTID unimplemented\n");
        // ath10k_wmi_event_peer_sta_kickout(ar, skb);
        break;
    case WMI_10_2_HOST_SWBA_EVENTID:
        ath10k_err("WMI_10_2_HOST_SWBA_EVENTID unimplemented\n");
        // ath10k_wmi_event_host_swba(ar, skb);
        break;
    case WMI_10_2_TBTTOFFSET_UPDATE_EVENTID:
        ath10k_err("WMI_10_2_TBTTOFFSET_UPDATE_EVENTID unimplemented\n");
        // ath10k_wmi_event_tbttoffset_update(ar, skb);
        break;
    case WMI_10_2_PHYERR_EVENTID:
        ath10k_err("WMI_10_2_PHYERR_EVENTID unimplemented\n");
        // ath10k_wmi_event_phyerr(ar, skb);
        break;
    case WMI_10_2_ROAM_EVENTID:
        ath10k_err("WMI_10_2_ROAM_EVENTID unimplemented\n");
        // ath10k_wmi_event_roam(ar, skb);
        // ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_2_PROFILE_MATCH:
        ath10k_err("WMI_10_2_PROFILE_MATCH unimplemented\n");
        // ath10k_wmi_event_profile_match(ar, skb);
        break;
    case WMI_10_2_DEBUG_PRINT_EVENTID:
        ath10k_err("WMI_10_2_DEBUG_PRINT_EVENTID unimplemented\n");
        // ath10k_wmi_event_debug_print(ar, skb);
        // ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_2_PDEV_QVIT_EVENTID:
        ath10k_err("WMI_10_2_PDEV_QVIT_EVENTID unimplemented\n");
        // ath10k_wmi_event_pdev_qvit(ar, skb);
        break;
    case WMI_10_2_WLAN_PROFILE_DATA_EVENTID:
        ath10k_err("WMI_10_2_WLAN_PROFILE_DATA_EVENTID unimplemented\n");
        // ath10k_wmi_event_wlan_profile_data(ar, skb);
        break;
    case WMI_10_2_RTT_MEASUREMENT_REPORT_EVENTID:
        ath10k_err("WMI_10_2_RTT_MEASUREMENT_REPORT_EVENTID unimplemented\n");
        // ath10k_wmi_event_rtt_measurement_report(ar, skb);
        break;
    case WMI_10_2_TSF_MEASUREMENT_REPORT_EVENTID:
        ath10k_err("WMI_10_2_TSF_MEASUREMENT_REPORT_EVENTID unimplemented\n");
        // ath10k_wmi_event_tsf_measurement_report(ar, skb);
        break;
    case WMI_10_2_RTT_ERROR_REPORT_EVENTID:
        ath10k_err("WMI_10_2_RTT_ERROR_REPORT_EVENTID unimplemented\n");
        // ath10k_wmi_event_rtt_error_report(ar, skb);
        break;
    case WMI_10_2_WOW_WAKEUP_HOST_EVENTID:
        ath10k_err("WMI_10_2_WOW_WAKEUP_HOST_EVENTID unimplemented\n");
        // ath10k_wmi_event_wow_wakeup_host(ar, skb);
        break;
    case WMI_10_2_DCS_INTERFERENCE_EVENTID:
        ath10k_err("WMI_10_2_DCS_INTERFERENCE_EVENTID unimplemented\n");
        // ath10k_wmi_event_dcs_interference(ar, skb);
        break;
    case WMI_10_2_PDEV_TPC_CONFIG_EVENTID:
        ath10k_err("WMI_10_2_PDEV_TPC_CONFIG_EVENTID unimplemented\n");
        // ath10k_wmi_event_pdev_tpc_config(ar, skb);
        break;
    case WMI_10_2_INST_RSSI_STATS_EVENTID:
        ath10k_err("WMI_10_2_INST_RSSI_STATS_EVENTID unimplemented\n");
        // ath10k_wmi_event_inst_rssi_stats(ar, skb);
        break;
    case WMI_10_2_VDEV_STANDBY_REQ_EVENTID:
        ath10k_err("WMI_10_2_VDEV_STANDBY_REQ_EVENTID unimplemented\n");
        // ath10k_wmi_event_vdev_standby_req(ar, skb);
        // ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_2_VDEV_RESUME_REQ_EVENTID:
        ath10k_err("WMI_10_2_VDEV_RESUME_REQ_EVENTID unimplemented\n");
        // ath10k_wmi_event_vdev_resume_req(ar, skb);
        // ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_2_SERVICE_READY_EVENTID:
        ath10k_wmi_event_service_ready(ar, msg_buf);
        return;
    case WMI_10_2_READY_EVENTID:
        ath10k_wmi_event_ready(ar, msg_buf);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_2_PDEV_TEMPERATURE_EVENTID:
        ath10k_err("WMI_10_2_PDEV_TEMPERATURE_EVENTID unimplemented\n");
        // ath10k_wmi_event_temperature(ar, skb);
        break;
    case WMI_10_2_PDEV_BSS_CHAN_INFO_EVENTID:
        ath10k_err("WMI_10_2_PDEV_BSS_CHAN_INFO_EVENTID unimplemented\n");
        // ath10k_wmi_event_pdev_bss_chan_info(ar, skb);
        break;
    case WMI_10_2_RTT_KEEPALIVE_EVENTID:
    case WMI_10_2_GPIO_INPUT_EVENTID:
    case WMI_10_2_PEER_RATECODE_LIST_EVENTID:
    case WMI_10_2_GENERIC_BUFFER_EVENTID:
    case WMI_10_2_MCAST_BUF_RELEASE_EVENTID:
    case WMI_10_2_MCAST_LIST_AGEOUT_EVENTID:
    case WMI_10_2_WDS_PEER_EVENTID:
        ath10k_dbg(ar, ATH10K_DBG_WMI, "received event id %d not implemented\n", id);
        break;
    default:
        ath10k_warn("Unknown eventid: %d\n", id);
        break;
    }

out:
    ath10k_msg_buf_free(msg_buf);
}

#if 0   // NEEDS PORTING
static void ath10k_wmi_10_4_op_rx(struct ath10k* ar, struct sk_buff* skb) {
    struct wmi_cmd_hdr* cmd_hdr;
    enum wmi_10_4_event_id id;
    bool consumed;

    cmd_hdr = (struct wmi_cmd_hdr*)skb->data;
    id = MS(cmd_hdr->cmd_id, WMI_CMD_HDR_CMD_ID);

    if (!skb_pull(skb, sizeof(struct wmi_cmd_hdr))) {
        goto out;
    }

    trace_ath10k_wmi_event(ar, id, skb->data, skb->len);

    consumed = ath10k_tm_event_wmi(ar, id, skb);

    /* Ready event must be handled normally also in UTF mode so that we
     * know the UTF firmware has booted, others we are just bypass WMI
     * events to testmode.
     */
    if (consumed && id != WMI_10_4_READY_EVENTID) {
        ath10k_dbg(ar, ATH10K_DBG_WMI,
                   "wmi testmode consumed 0x%x\n", id);
        goto out;
    }

    switch (id) {
    case WMI_10_4_MGMT_RX_EVENTID:
        ath10k_wmi_event_mgmt_rx(ar, skb);
        /* mgmt_rx() owns the skb now! */
        return;
    case WMI_10_4_ECHO_EVENTID:
        ath10k_wmi_event_echo(ar, skb);
        break;
    case WMI_10_4_DEBUG_MESG_EVENTID:
        ath10k_wmi_event_debug_mesg(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_4_SERVICE_READY_EVENTID:
        ath10k_wmi_event_service_ready(ar, skb);
        return;
    case WMI_10_4_SCAN_EVENTID:
        ath10k_wmi_event_scan(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_4_CHAN_INFO_EVENTID:
        ath10k_wmi_event_chan_info(ar, skb);
        break;
    case WMI_10_4_PHYERR_EVENTID:
        ath10k_wmi_event_phyerr(ar, skb);
        break;
    case WMI_10_4_READY_EVENTID:
        ath10k_wmi_event_ready(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_4_PEER_STA_KICKOUT_EVENTID:
        ath10k_wmi_event_peer_sta_kickout(ar, skb);
        break;
    case WMI_10_4_ROAM_EVENTID:
        ath10k_wmi_event_roam(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_4_HOST_SWBA_EVENTID:
        ath10k_wmi_event_host_swba(ar, skb);
        break;
    case WMI_10_4_TBTTOFFSET_UPDATE_EVENTID:
        ath10k_wmi_event_tbttoffset_update(ar, skb);
        break;
    case WMI_10_4_DEBUG_PRINT_EVENTID:
        ath10k_wmi_event_debug_print(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_4_VDEV_START_RESP_EVENTID:
        ath10k_wmi_event_vdev_start_resp(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_4_VDEV_STOPPED_EVENTID:
        ath10k_wmi_event_vdev_stopped(ar, skb);
        ath10k_wmi_queue_set_coverage_class_work(ar);
        break;
    case WMI_10_4_WOW_WAKEUP_HOST_EVENTID:
    case WMI_10_4_PEER_RATECODE_LIST_EVENTID:
    case WMI_10_4_WDS_PEER_EVENTID:
        ath10k_dbg(ar, ATH10K_DBG_WMI,
                   "received event id %d not implemented\n", id);
        break;
    case WMI_10_4_UPDATE_STATS_EVENTID:
        ath10k_wmi_event_update_stats(ar, skb);
        break;
    case WMI_10_4_PDEV_TEMPERATURE_EVENTID:
        ath10k_wmi_event_temperature(ar, skb);
        break;
    case WMI_10_4_PDEV_BSS_CHAN_INFO_EVENTID:
        ath10k_wmi_event_pdev_bss_chan_info(ar, skb);
        break;
    case WMI_10_4_PDEV_TPC_CONFIG_EVENTID:
        ath10k_wmi_event_pdev_tpc_config(ar, skb);
        break;
    default:
        ath10k_warn("Unknown eventid: %d\n", id);
        break;
    }

out:
    dev_kfree_skb(skb);
}
#endif  // NEEDS PORTING

static void ath10k_wmi_process_rx(struct ath10k* ar, struct ath10k_msg_buf* buf) {
    zx_status_t ret;

    ret = ath10k_wmi_rx(ar, buf);
    if (ret != ZX_OK) { ath10k_warn("failed to process wmi rx: %d\n", ret); }
}

zx_status_t ath10k_wmi_connect(struct ath10k* ar) {
    zx_status_t status;
    struct ath10k_htc_svc_conn_req conn_req;
    struct ath10k_htc_svc_conn_resp conn_resp;

    memset(&conn_req, 0, sizeof(conn_req));
    memset(&conn_resp, 0, sizeof(conn_resp));

    /* these fields are the same for all service endpoints */
    conn_req.ep_ops.ep_tx_complete = ath10k_wmi_htc_tx_complete;
    conn_req.ep_ops.ep_rx_complete = ath10k_wmi_process_rx;
    conn_req.ep_ops.ep_tx_credits = ath10k_wmi_op_ep_tx_credits;

    /* connect to control service */
    conn_req.service_id = ATH10K_HTC_SVC_ID_WMI_CONTROL;

    status = ath10k_htc_connect_service(&ar->htc, &conn_req, &conn_resp);
    if (status) {
        ath10k_warn("failed to connect to WMI CONTROL service status: %d\n", status);
        return status;
    }

    ar->wmi.eid = conn_resp.eid;
    return ZX_OK;
}

static zx_status_t ath10k_wmi_op_gen_pdev_set_rd(struct ath10k* ar,
                                                 struct ath10k_msg_buf** msg_buf_ptr, uint16_t rd,
                                                 uint16_t rd2g, uint16_t rd5g, uint16_t ctl2g,
                                                 uint16_t ctl5g, enum wmi_dfs_region dfs_reg) {
    struct wmi_pdev_set_regdomain_cmd* cmd;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI_PDEV_SET_RD, 0);
    if (status != ZX_OK) { return status; }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI_PDEV_SET_RD);
    cmd->reg_domain = rd;
    cmd->reg_domain_2G = rd2g;
    cmd->reg_domain_5G = rd5g;
    cmd->conformance_test_limit_2G = ctl2g;
    cmd->conformance_test_limit_5G = ctl5g;

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi pdev regdomain rd %x rd2g %x rd5g %x ctl2g %x ctl5g %x\n",
               rd, rd2g, rd5g, ctl2g, ctl5g);
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

#if 0   // NEEDS PORTING
static struct sk_buff*
ath10k_wmi_10x_op_gen_pdev_set_rd(struct ath10k* ar, uint16_t rd, uint16_t rd2g, uint16_t rd5g,
                                  uint16_t ctl2g, uint16_t ctl5g,
                                  enum wmi_dfs_region dfs_reg) {
    struct wmi_pdev_set_regdomain_cmd_10x* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_pdev_set_regdomain_cmd_10x*)skb->data;
    cmd->reg_domain = rd;
    cmd->reg_domain_2G = rd2g;
    cmd->reg_domain_5G = rd5g;
    cmd->conformance_test_limit_2G = ctl2g;
    cmd->conformance_test_limit_5G = ctl5g;
    cmd->dfs_domain = dfs_reg;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi pdev regdomain rd %x rd2g %x rd5g %x ctl2g %x ctl5g %x dfs_region %x\n",
               rd, rd2g, rd5g, ctl2g, ctl5g, dfs_reg);
    return skb;
}
#endif  // NEEDS PORTING

static zx_status_t ath10k_wmi_op_gen_pdev_suspend(struct ath10k* ar,
                                                  struct ath10k_msg_buf** msg_buf_ptr,
                                                  uint32_t suspend_opt) {
    struct wmi_pdev_suspend_cmd* cmd;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI_PDEV_SUSPEND, 0);
    if (status != ZX_OK) { return status; }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI_PDEV_SUSPEND);
    cmd->suspend_opt = suspend_opt;

    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

static zx_status_t ath10k_wmi_op_gen_pdev_resume(struct ath10k* ar,
                                                 struct ath10k_msg_buf** msg_buf_ptr) {
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI, 0);

    if (status != ZX_OK) { return status; }
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

static zx_status_t ath10k_wmi_op_gen_pdev_set_param(struct ath10k* ar,
                                                    struct ath10k_msg_buf** msg_buf_ptr,
                                                    uint32_t id, uint32_t value) {
    struct wmi_pdev_set_param_cmd* cmd;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;

    if (id == WMI_PDEV_PARAM_UNSUPPORTED) {
        ath10k_warn("pdev param %d not supported by firmware\n", id);
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI_PDEV_SET_PARAM, 0);
    if (status != ZX_OK) { return status; }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI_PDEV_SET_PARAM);
    cmd->param_id = id;
    cmd->param_value = value;

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi pdev set param %d value %d\n", id, value);
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

void ath10k_wmi_put_host_mem_chunks(struct ath10k* ar, struct wmi_host_mem_chunks* chunks) {
    struct host_memory_chunk* chunk;
    unsigned int i;

    chunks->count = ar->wmi.num_mem_chunks;

    for (i = 0; i < ar->wmi.num_mem_chunks; i++) {
        chunk = &chunks->items[i];
        chunk->ptr = ar->wmi.mem_chunks[i].paddr;
        chunk->size = ar->wmi.mem_chunks[i].len;
        chunk->req_id = ar->wmi.mem_chunks[i].req_id;

        ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi chunk %d len %d requested, addr 0x%llx\n", i,
                   ar->wmi.mem_chunks[i].len, (unsigned long long)ar->wmi.mem_chunks[i].paddr);
    }
}

#if 0   // NEEDS PORTING
static struct sk_buff* ath10k_wmi_op_gen_init(struct ath10k* ar) {
    struct wmi_init_cmd* cmd;
    struct sk_buff* buf;
    struct wmi_resource_config config = {};
    uint32_t len, val;

    config.num_vdevs = TARGET_NUM_VDEVS;
    config.num_peers = TARGET_NUM_PEERS;
    config.num_offload_peers = TARGET_NUM_OFFLOAD_PEERS;

    config.num_offload_reorder_bufs =
        TARGET_NUM_OFFLOAD_REORDER_BUFS;

    config.num_peer_keys = TARGET_NUM_PEER_KEYS;
    config.num_tids = TARGET_NUM_TIDS;
    config.ast_skid_limit = TARGET_AST_SKID_LIMIT;
    config.tx_chain_mask = TARGET_TX_CHAIN_MASK;
    config.rx_chain_mask = TARGET_RX_CHAIN_MASK;
    config.rx_timeout_pri_vo = TARGET_RX_TIMEOUT_LO_PRI;
    config.rx_timeout_pri_vi = TARGET_RX_TIMEOUT_LO_PRI;
    config.rx_timeout_pri_be = TARGET_RX_TIMEOUT_LO_PRI;
    config.rx_timeout_pri_bk = TARGET_RX_TIMEOUT_HI_PRI;
    config.rx_decap_mode = ar->wmi.rx_decap_mode;
    config.scan_max_pending_reqs =
        TARGET_SCAN_MAX_PENDING_REQS;

    config.bmiss_offload_max_vdev =
        TARGET_BMISS_OFFLOAD_MAX_VDEV;

    config.roam_offload_max_vdev =
        TARGET_ROAM_OFFLOAD_MAX_VDEV;

    config.roam_offload_max_ap_profiles =
        TARGET_ROAM_OFFLOAD_MAX_AP_PROFILES;

    config.num_mcast_groups = TARGET_NUM_MCAST_GROUPS;
    config.num_mcast_table_elems =
        TARGET_NUM_MCAST_TABLE_ELEMS;

    config.mcast2ucast_mode = TARGET_MCAST2UCAST_MODE;
    config.tx_dbg_log_size = TARGET_TX_DBG_LOG_SIZE;
    config.num_wds_entries = TARGET_NUM_WDS_ENTRIES;
    config.dma_burst_size = TARGET_DMA_BURST_SIZE;
    config.mac_aggr_delim = TARGET_MAC_AGGR_DELIM;

    val = TARGET_RX_SKIP_DEFRAG_TIMEOUT_DUP_DETECTION_CHECK;
    config.rx_skip_defrag_timeout_dup_detection_check = val;

    config.vow_config = TARGET_VOW_CONFIG;

    config.gtk_offload_max_vdev =
        TARGET_GTK_OFFLOAD_MAX_VDEV;

    config.num_msdu_desc = TARGET_NUM_MSDU_DESC;
    config.max_frag_entries = TARGET_MAX_FRAG_ENTRIES;

    len = sizeof(*cmd) +
          (sizeof(struct host_memory_chunk) * ar->wmi.num_mem_chunks);

    buf = ath10k_wmi_alloc_skb(ar, len);
    if (!buf) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_init_cmd*)buf->data;

    memcpy(&cmd->resource_config, &config, sizeof(config));
    ath10k_wmi_put_host_mem_chunks(ar, &cmd->mem_chunks);

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi init\n");
    return buf;
}

static struct sk_buff* ath10k_wmi_10_1_op_gen_init(struct ath10k* ar) {
    struct wmi_init_cmd_10x* cmd;
    struct sk_buff* buf;
    struct wmi_resource_config_10x config = {};
    uint32_t len, val;

    config.num_vdevs = TARGET_10X_NUM_VDEVS;
    config.num_peers = TARGET_10X_NUM_PEERS;
    config.num_peer_keys = TARGET_10X_NUM_PEER_KEYS;
    config.num_tids = TARGET_10X_NUM_TIDS;
    config.ast_skid_limit = TARGET_10X_AST_SKID_LIMIT;
    config.tx_chain_mask = TARGET_10X_TX_CHAIN_MASK;
    config.rx_chain_mask = TARGET_10X_RX_CHAIN_MASK;
    config.rx_timeout_pri_vo = TARGET_10X_RX_TIMEOUT_LO_PRI;
    config.rx_timeout_pri_vi = TARGET_10X_RX_TIMEOUT_LO_PRI;
    config.rx_timeout_pri_be = TARGET_10X_RX_TIMEOUT_LO_PRI;
    config.rx_timeout_pri_bk = TARGET_10X_RX_TIMEOUT_HI_PRI;
    config.rx_decap_mode = ar->wmi.rx_decap_mode;
    config.scan_max_pending_reqs =
        TARGET_10X_SCAN_MAX_PENDING_REQS;

    config.bmiss_offload_max_vdev =
        TARGET_10X_BMISS_OFFLOAD_MAX_VDEV;

    config.roam_offload_max_vdev =
        TARGET_10X_ROAM_OFFLOAD_MAX_VDEV;

    config.roam_offload_max_ap_profiles =
        TARGET_10X_ROAM_OFFLOAD_MAX_AP_PROFILES;

    config.num_mcast_groups = TARGET_10X_NUM_MCAST_GROUPS;
    config.num_mcast_table_elems =
        TARGET_10X_NUM_MCAST_TABLE_ELEMS;

    config.mcast2ucast_mode = TARGET_10X_MCAST2UCAST_MODE;
    config.tx_dbg_log_size = TARGET_10X_TX_DBG_LOG_SIZE;
    config.num_wds_entries = TARGET_10X_NUM_WDS_ENTRIES;
    config.dma_burst_size = TARGET_10X_DMA_BURST_SIZE;
    config.mac_aggr_delim = TARGET_10X_MAC_AGGR_DELIM;

    val = TARGET_10X_RX_SKIP_DEFRAG_TIMEOUT_DUP_DETECTION_CHECK;
    config.rx_skip_defrag_timeout_dup_detection_check = val;

    config.vow_config = TARGET_10X_VOW_CONFIG;

    config.num_msdu_desc = TARGET_10X_NUM_MSDU_DESC;
    config.max_frag_entries = TARGET_10X_MAX_FRAG_ENTRIES;

    len = sizeof(*cmd) +
          (sizeof(struct host_memory_chunk) * ar->wmi.num_mem_chunks);

    buf = ath10k_wmi_alloc_skb(ar, len);
    if (!buf) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_init_cmd_10x*)buf->data;

    memcpy(&cmd->resource_config, &config, sizeof(config));
    ath10k_wmi_put_host_mem_chunks(ar, &cmd->mem_chunks);

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi init 10x\n");
    return buf;
}
#endif  // NEEDS PORTING

static zx_status_t ath10k_wmi_10_2_op_gen_init(struct ath10k* ar,
                                               struct ath10k_msg_buf** msg_buf_ptr) {
    struct wmi_init_cmd_10_2* cmd;
    struct wmi_resource_config_10x config = {};
    uint32_t extra_len, val, features;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;

    config.num_vdevs = TARGET_10X_NUM_VDEVS;
    config.num_peer_keys = TARGET_10X_NUM_PEER_KEYS;

    if (ath10k_peer_stats_enabled(ar)) {
        config.num_peers = TARGET_10X_TX_STATS_NUM_PEERS;
        config.num_tids = TARGET_10X_TX_STATS_NUM_TIDS;
    } else {
        config.num_peers = TARGET_10X_NUM_PEERS;
        config.num_tids = TARGET_10X_NUM_TIDS;
    }

    config.ast_skid_limit = TARGET_10X_AST_SKID_LIMIT;
    config.tx_chain_mask = TARGET_10X_TX_CHAIN_MASK;
    config.rx_chain_mask = TARGET_10X_RX_CHAIN_MASK;
    config.rx_timeout_pri_vo = TARGET_10X_RX_TIMEOUT_LO_PRI;
    config.rx_timeout_pri_vi = TARGET_10X_RX_TIMEOUT_LO_PRI;
    config.rx_timeout_pri_be = TARGET_10X_RX_TIMEOUT_LO_PRI;
    config.rx_timeout_pri_bk = TARGET_10X_RX_TIMEOUT_HI_PRI;
    config.rx_decap_mode = ar->wmi.rx_decap_mode;

    config.scan_max_pending_reqs = TARGET_10X_SCAN_MAX_PENDING_REQS;

    config.bmiss_offload_max_vdev = TARGET_10X_BMISS_OFFLOAD_MAX_VDEV;

    config.roam_offload_max_vdev = TARGET_10X_ROAM_OFFLOAD_MAX_VDEV;

    config.roam_offload_max_ap_profiles = TARGET_10X_ROAM_OFFLOAD_MAX_AP_PROFILES;

    config.num_mcast_groups = TARGET_10X_NUM_MCAST_GROUPS;
    config.num_mcast_table_elems = TARGET_10X_NUM_MCAST_TABLE_ELEMS;

    config.mcast2ucast_mode = TARGET_10X_MCAST2UCAST_MODE;
    config.tx_dbg_log_size = TARGET_10X_TX_DBG_LOG_SIZE;
    config.num_wds_entries = TARGET_10X_NUM_WDS_ENTRIES;
    config.dma_burst_size = TARGET_10_2_DMA_BURST_SIZE;
    config.mac_aggr_delim = TARGET_10X_MAC_AGGR_DELIM;

    val = TARGET_10X_RX_SKIP_DEFRAG_TIMEOUT_DUP_DETECTION_CHECK;
    config.rx_skip_defrag_timeout_dup_detection_check = val;

    config.vow_config = TARGET_10X_VOW_CONFIG;

    config.num_msdu_desc = TARGET_10X_NUM_MSDU_DESC;
    config.max_frag_entries = TARGET_10X_MAX_FRAG_ENTRIES;

    extra_len = sizeof(struct host_memory_chunk) * ar->wmi.num_mem_chunks;

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI_INIT_CMD_10_2, extra_len);
    if (status != ZX_OK) { return status; }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI_INIT_CMD_10_2);

    features = WMI_10_2_RX_BATCH_MODE;

    if (BITARR_TEST(ar->dev_flags, ATH10K_FLAG_BTCOEX) &&
        BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_COEX_GPIO)) {
        features |= WMI_10_2_COEX_GPIO;
    }

    if (ath10k_peer_stats_enabled(ar)) { features |= WMI_10_2_PEER_STATS; }

    if (BITARR_TEST(ar->wmi.svc_map, WMI_SERVICE_BSS_CHANNEL_INFO_64)) {
        features |= WMI_10_2_BSS_CHAN_INFO;
    }

    cmd->resource_config.feature_mask = features;

    memcpy(&cmd->resource_config.common, &config, sizeof(config));
    ath10k_wmi_put_host_mem_chunks(ar, &cmd->mem_chunks);

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi init 10.2\n");
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

#if 0   // NEEDS PORTING
static struct sk_buff* ath10k_wmi_10_4_op_gen_init(struct ath10k* ar) {
    struct wmi_init_cmd_10_4* cmd;
    struct sk_buff* buf;
    struct wmi_resource_config_10_4 config = {};
    uint32_t len;

    config.num_vdevs = ar->max_num_vdevs;
    config.num_peers = ar->max_num_peers;
    config.num_active_peers = ar->num_active_peers;
    config.num_tids = ar->num_tids;

    config.num_offload_peers = TARGET_10_4_NUM_OFFLOAD_PEERS;
    config.num_offload_reorder_buffs =
        TARGET_10_4_NUM_OFFLOAD_REORDER_BUFFS;
    config.num_peer_keys  = TARGET_10_4_NUM_PEER_KEYS;
    config.ast_skid_limit = TARGET_10_4_AST_SKID_LIMIT;
    config.tx_chain_mask  = ar->hw_params.tx_chain_mask;
    config.rx_chain_mask  = ar->hw_params.rx_chain_mask;

    config.rx_timeout_pri[0] = TARGET_10_4_RX_TIMEOUT_LO_PRI;
    config.rx_timeout_pri[1] = TARGET_10_4_RX_TIMEOUT_LO_PRI;
    config.rx_timeout_pri[2] = TARGET_10_4_RX_TIMEOUT_LO_PRI;
    config.rx_timeout_pri[3] = TARGET_10_4_RX_TIMEOUT_HI_PRI;

    config.rx_decap_mode        = ar->wmi.rx_decap_mode;
    config.scan_max_pending_req = TARGET_10_4_SCAN_MAX_REQS;
    config.bmiss_offload_max_vdev =
        TARGET_10_4_BMISS_OFFLOAD_MAX_VDEV;
    config.roam_offload_max_vdev  =
        TARGET_10_4_ROAM_OFFLOAD_MAX_VDEV;
    config.roam_offload_max_ap_profiles =
        TARGET_10_4_ROAM_OFFLOAD_MAX_PROFILES;
    config.num_mcast_groups = TARGET_10_4_NUM_MCAST_GROUPS;
    config.num_mcast_table_elems =
        TARGET_10_4_NUM_MCAST_TABLE_ELEMS;

    config.mcast2ucast_mode = TARGET_10_4_MCAST2UCAST_MODE;
    config.tx_dbg_log_size  = TARGET_10_4_TX_DBG_LOG_SIZE;
    config.num_wds_entries  = TARGET_10_4_NUM_WDS_ENTRIES;
    config.dma_burst_size   = TARGET_10_4_DMA_BURST_SIZE;
    config.mac_aggr_delim   = TARGET_10_4_MAC_AGGR_DELIM;

    config.rx_skip_defrag_timeout_dup_detection_check =
        TARGET_10_4_RX_SKIP_DEFRAG_TIMEOUT_DUP_DETECTION_CHECK;

    config.vow_config = TARGET_10_4_VOW_CONFIG;
    config.gtk_offload_max_vdev =
        TARGET_10_4_GTK_OFFLOAD_MAX_VDEV;
    config.num_msdu_desc = ar->htt.max_num_pending_tx;
    config.max_frag_entries = TARGET_10_4_11AC_TX_MAX_FRAGS;
    config.max_peer_ext_stats =
        TARGET_10_4_MAX_PEER_EXT_STATS;
    config.smart_ant_cap = TARGET_10_4_SMART_ANT_CAP;

    config.bk_minfree = TARGET_10_4_BK_MIN_FREE;
    config.be_minfree = TARGET_10_4_BE_MIN_FREE;
    config.vi_minfree = TARGET_10_4_VI_MIN_FREE;
    config.vo_minfree = TARGET_10_4_VO_MIN_FREE;

    config.rx_batchmode = TARGET_10_4_RX_BATCH_MODE;
    config.tt_support =
        TARGET_10_4_THERMAL_THROTTLING_CONFIG;
    config.atf_config = TARGET_10_4_ATF_CONFIG;
    config.iphdr_pad_config = TARGET_10_4_IPHDR_PAD_CONFIG;
    config.qwrap_config = TARGET_10_4_QWRAP_CONFIG;

    len = sizeof(*cmd) +
          (sizeof(struct host_memory_chunk) * ar->wmi.num_mem_chunks);

    buf = ath10k_wmi_alloc_skb(ar, len);
    if (!buf) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_init_cmd_10_4*)buf->data;
    memcpy(&cmd->resource_config, &config, sizeof(config));
    ath10k_wmi_put_host_mem_chunks(ar, &cmd->mem_chunks);

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi init 10.4\n");
    return buf;
}
#endif  // NEEDS PORTING

zx_status_t ath10k_wmi_start_scan_verify(const struct wmi_start_scan_arg* arg) {
    if (arg->ie_len > WLAN_SCAN_PARAMS_MAX_IE_LEN) { return ZX_ERR_INVALID_ARGS; }
    if (arg->n_channels > countof(arg->channels)) { return ZX_ERR_INVALID_ARGS; }
    if (arg->n_ssids > WLAN_SCAN_PARAMS_MAX_SSID) { return ZX_ERR_INVALID_ARGS; }
    if (arg->n_bssids > WLAN_SCAN_PARAMS_MAX_BSSID) { return ZX_ERR_INVALID_ARGS; }

    return ZX_OK;
}

#if 0   // NEEDS PORTING
static size_t
ath10k_wmi_start_scan_tlvs_len(const struct wmi_start_scan_arg* arg) {
    int len = 0;

    if (arg->ie_len) {
        len += sizeof(struct wmi_ie_data);
        len += ROUNDUP(arg->ie_len, 4);
    }

    if (arg->n_channels) {
        len += sizeof(struct wmi_chan_list);
        len += sizeof(uint32_t) * arg->n_channels;
    }

    if (arg->n_ssids) {
        len += sizeof(struct wmi_ssid_list);
        len += sizeof(struct wmi_ssid) * arg->n_ssids;
    }

    if (arg->n_bssids) {
        len += sizeof(struct wmi_bssid_list);
        len += sizeof(struct wmi_mac_addr) * arg->n_bssids;
    }

    return len;
}
#endif  // NEEDS PORTING

void ath10k_wmi_put_start_scan_common(struct wmi_start_scan_common* cmn,
                                      const struct wmi_start_scan_arg* arg) {
    uint32_t scan_id;
    uint32_t scan_req_id;

    scan_id = WMI_HOST_SCAN_REQ_ID_PREFIX;
    scan_id |= arg->scan_id;

    scan_req_id = WMI_HOST_SCAN_REQUESTOR_ID_PREFIX;
    scan_req_id |= arg->scan_req_id;

    cmn->scan_id = scan_id;
    cmn->scan_req_id = scan_req_id;
    cmn->vdev_id = arg->vdev_id;
    cmn->scan_priority = arg->scan_priority;
    cmn->notify_scan_events = arg->notify_scan_events;
    cmn->dwell_time_active = arg->dwell_time_active;
    cmn->dwell_time_passive = arg->dwell_time_passive;
    cmn->min_rest_time = arg->min_rest_time;
    cmn->max_rest_time = arg->max_rest_time;
    cmn->repeat_probe_time = arg->repeat_probe_time;
    cmn->probe_spacing_time = arg->probe_spacing_time;
    cmn->idle_time = arg->idle_time;
    cmn->max_scan_time = arg->max_scan_time;
    cmn->probe_delay = arg->probe_delay;
    cmn->scan_ctrl_flags = arg->scan_ctrl_flags;
}

#if 0   // NEEDS PORTING
static void
ath10k_wmi_put_start_scan_tlvs(struct wmi_start_scan_tlvs* tlvs,
                               const struct wmi_start_scan_arg* arg) {
    struct wmi_ie_data* ie;
    struct wmi_chan_list* channels;
    struct wmi_ssid_list* ssids;
    struct wmi_bssid_list* bssids;
    void* ptr = tlvs->tlvs;
    int i;

    if (arg->n_channels) {
        channels = ptr;
        channels->tag = WMI_CHAN_LIST_TAG;
        channels->num_chan = arg->n_channels;

        for (i = 0; i < arg->n_channels; i++)
            channels->channel_list[i].freq =
                arg->channels[i];

        ptr += sizeof(*channels);
        ptr += sizeof(uint32_t) * arg->n_channels;
    }

    if (arg->n_ssids) {
        ssids = ptr;
        ssids->tag = WMI_SSID_LIST_TAG;
        ssids->num_ssids = arg->n_ssids;

        for (i = 0; i < arg->n_ssids; i++) {
            ssids->ssids[i].ssid_len =
                arg->ssids[i].len;
            memcpy(&ssids->ssids[i].ssid,
                   arg->ssids[i].ssid,
                   arg->ssids[i].len);
        }

        ptr += sizeof(*ssids);
        ptr += sizeof(struct wmi_ssid) * arg->n_ssids;
    }

    if (arg->n_bssids) {
        bssids = ptr;
        bssids->tag = WMI_BSSID_LIST_TAG;
        bssids->num_bssid = arg->n_bssids;

        for (i = 0; i < arg->n_bssids; i++)
            memcpy(bssids->bssid_list[i].addr, arg->bssids[i].bssid, ETH_ALEN);

        ptr += sizeof(*bssids);
        ptr += sizeof(struct wmi_mac_addr) * arg->n_bssids;
    }

    if (arg->ie_len) {
        ie = ptr;
        ie->tag = WMI_IE_TAG;
        ie->ie_len = arg->ie_len;
        memcpy(ie->ie_data, arg->ie, arg->ie_len);

        ptr += sizeof(*ie);
        ptr += ROUNDUP(arg->ie_len, 4);
    }
}

static struct sk_buff*
ath10k_wmi_op_gen_start_scan(struct ath10k* ar,
                             const struct wmi_start_scan_arg* arg) {
    struct wmi_start_scan_cmd* cmd;
    struct sk_buff* skb;
    size_t len;
    int ret;

    ret = ath10k_wmi_start_scan_verify(arg);
    if (ret) {
        return ERR_PTR(ret);
    }

    len = sizeof(*cmd) + ath10k_wmi_start_scan_tlvs_len(arg);
    skb = ath10k_wmi_alloc_skb(ar, len);
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_start_scan_cmd*)skb->data;

    ath10k_wmi_put_start_scan_common(&cmd->common, arg);
    ath10k_wmi_put_start_scan_tlvs(&cmd->tlvs, arg);

    cmd->burst_duration_ms = 0;

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi start scan\n");
    return skb;
}

static struct sk_buff*
ath10k_wmi_10x_op_gen_start_scan(struct ath10k* ar,
                                 const struct wmi_start_scan_arg* arg) {
    struct wmi_10x_start_scan_cmd* cmd;
    struct sk_buff* skb;
    size_t len;
    int ret;

    ret = ath10k_wmi_start_scan_verify(arg);
    if (ret) {
        return ERR_PTR(ret);
    }

    len = sizeof(*cmd) + ath10k_wmi_start_scan_tlvs_len(arg);
    skb = ath10k_wmi_alloc_skb(ar, len);
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_10x_start_scan_cmd*)skb->data;

    ath10k_wmi_put_start_scan_common(&cmd->common, arg);
    ath10k_wmi_put_start_scan_tlvs(&cmd->tlvs, arg);

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi 10x start scan\n");
    return skb;
}
#endif  // NEEDS PORTING

void ath10k_wmi_start_scan_init(struct wmi_start_scan_arg* arg) {
    /* setup commonly used values */
    arg->scan_req_id = 1;
    arg->scan_priority = WMI_SCAN_PRIORITY_LOW;
    arg->dwell_time_active = 50;
    arg->dwell_time_passive = 150;
    arg->min_rest_time = 50;
    arg->max_rest_time = 500;
    arg->repeat_probe_time = 0;
    arg->probe_spacing_time = 0;
    arg->idle_time = 0;
    arg->max_scan_time = 20000;
    arg->probe_delay = 5;
    arg->notify_scan_events = WMI_SCAN_EVENT_STARTED | WMI_SCAN_EVENT_COMPLETED;
    arg->scan_ctrl_flags = 0;
    arg->n_bssids = 1;

    static uint8_t wildcard_bssid[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    arg->bssids[0].bssid = wildcard_bssid;
}

#if 0   // NEEDS PORTING
static struct sk_buff*
ath10k_wmi_op_gen_stop_scan(struct ath10k* ar,
                            const struct wmi_stop_scan_arg* arg) {
    struct wmi_stop_scan_cmd* cmd;
    struct sk_buff* skb;
    uint32_t scan_id;
    uint32_t req_id;

    if (arg->req_id > 0xFFF) {
        return ERR_PTR(-EINVAL);
    }
    if (arg->req_type == WMI_SCAN_STOP_ONE && arg->u.scan_id > 0xFFF) {
        return ERR_PTR(-EINVAL);
    }

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    scan_id = arg->u.scan_id;
    scan_id |= WMI_HOST_SCAN_REQ_ID_PREFIX;

    req_id = arg->req_id;
    req_id |= WMI_HOST_SCAN_REQUESTOR_ID_PREFIX;

    cmd = (struct wmi_stop_scan_cmd*)skb->data;
    cmd->req_type    = arg->req_type;
    cmd->vdev_id     = arg->u.vdev_id;
    cmd->scan_id     = scan_id;
    cmd->scan_req_id = req_id;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi stop scan reqid %d req_type %d vdev/scan_id %d\n",
               arg->req_id, arg->req_type, arg->u.scan_id);
    return skb;
}
#endif  // NEEDS PORTING

static zx_status_t ath10k_wmi_op_gen_vdev_create(struct ath10k* ar,
                                                 struct ath10k_msg_buf** msg_buf_ptr,
                                                 uint32_t vdev_id, enum wmi_vdev_type type,
                                                 enum wmi_vdev_subtype subtype,
                                                 const uint8_t macaddr[ETH_ALEN]) {
    struct wmi_vdev_create_cmd* cmd;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_CREATE, 0);
    if (status != ZX_OK) { return status; }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_CREATE);
    cmd->vdev_id = vdev_id;
    cmd->vdev_type = type;
    cmd->vdev_subtype = subtype;
    memcpy(cmd->vdev_macaddr.addr, macaddr, ETH_ALEN);

    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI vdev create: id %d type %d subtype %d macaddr %pM\n",
               vdev_id, type, subtype, macaddr);
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

static zx_status_t ath10k_wmi_op_gen_vdev_delete(struct ath10k* ar,
                                                 struct ath10k_msg_buf** msg_buf_ptr,
                                                 uint32_t vdev_id) {
    struct wmi_vdev_delete_cmd* cmd;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_DELETE, 0);
    if (status != ZX_OK) { return status; }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_DELETE);
    cmd->vdev_id = vdev_id;

    ath10k_dbg(ar, ATH10K_DBG_WMI, "WMI vdev delete id %d\n", vdev_id);
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

static zx_status_t ath10k_wmi_op_gen_vdev_start(struct ath10k* ar,
                                                struct ath10k_msg_buf** msg_buf_ptr,
                                                const struct wmi_vdev_start_request_arg* arg,
                                                bool restart) {
    struct wmi_vdev_start_request_cmd* cmd;
    struct ath10k_msg_buf* msg_buf;
    const char* cmdname;
    uint32_t flags = 0;
    zx_status_t status;

    if (COND_WARN(arg->hidden_ssid && !arg->ssid)) { return ZX_ERR_INVALID_ARGS; }
    if (COND_WARN(arg->ssid_len > sizeof(cmd->ssid.ssid))) { return ZX_ERR_INVALID_ARGS; }

    if (restart) {
        cmdname = "restart";
    } else {
        cmdname = "start";
    }

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_START, 0);
    if (status != ZX_OK) { return status; }

    if (arg->hidden_ssid) { flags |= WMI_VDEV_START_HIDDEN_SSID; }
    if (arg->pmf_enabled) { flags |= WMI_VDEV_START_PMF_ENABLED; }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_START);
    cmd->vdev_id = arg->vdev_id;
    cmd->disable_hw_ack = arg->disable_hw_ack;
    cmd->beacon_interval = arg->bcn_intval;
    cmd->dtim_period = arg->dtim_period;
    cmd->flags = flags;
    cmd->bcn_tx_rate = arg->bcn_tx_rate;
    cmd->bcn_tx_power = arg->bcn_tx_power;

    if (arg->ssid) {
        cmd->ssid.ssid_len = arg->ssid_len;
        memcpy(cmd->ssid.ssid, arg->ssid, arg->ssid_len);
    }

    ath10k_wmi_put_wmi_channel(&cmd->chan, &arg->channel);

    ath10k_dbg(
        ar, ATH10K_DBG_WMI,
        "wmi vdev %s id 0x%x flags: 0x%0X, freq %d, mode %d, ch_flags: 0x%0X, max_power: %d\n",
        cmdname, arg->vdev_id, flags, arg->channel.freq, arg->channel.mode, cmd->chan.flags,
        arg->channel.max_power);
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

static zx_status_t ath10k_wmi_op_gen_vdev_stop(struct ath10k* ar,
                                               struct ath10k_msg_buf** msg_buf_ptr,
                                               uint32_t vdev_id) {
    struct wmi_vdev_stop_cmd* cmd;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_STOP, 0);
    if (status != ZX_OK) { return status; }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_STOP);
    cmd->vdev_id = vdev_id;

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi vdev stop id 0x%x\n", vdev_id);
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

static zx_status_t ath10k_wmi_op_gen_vdev_up(struct ath10k* ar, struct ath10k_msg_buf** msg_buf_ptr,
                                             uint32_t vdev_id, uint32_t aid, const uint8_t* bssid) {
    struct wmi_vdev_up_cmd* cmd;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_UP, 0);
    if (status != ZX_OK) { return status; }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_UP);
    cmd->vdev_id = vdev_id;
    cmd->vdev_assoc_id = aid;
    memcpy(cmd->vdev_bssid.addr, bssid, ETH_ALEN);

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi mgmt vdev up id 0x%x assoc id %d bssid %pM\n", vdev_id, aid,
               bssid);
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

static zx_status_t ath10k_wmi_op_gen_vdev_down(struct ath10k* ar,
                                               struct ath10k_msg_buf** msg_buf_ptr,
                                               uint32_t vdev_id) {
    struct wmi_vdev_down_cmd* cmd;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_DOWN, 0);
    if (status != ZX_OK) { return status; }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_DOWN);
    cmd->vdev_id = vdev_id;

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi mgmt vdev down id 0x%x\n", vdev_id);
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

static zx_status_t ath10k_wmi_op_gen_vdev_set_param(struct ath10k* ar,
                                                    struct ath10k_msg_buf** msg_buf_ptr,
                                                    uint32_t vdev_id, uint32_t param_id,
                                                    uint32_t param_value) {
    struct wmi_vdev_set_param_cmd* cmd;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;

    if (param_id == WMI_VDEV_PARAM_UNSUPPORTED) {
        ath10k_dbg(ar, ATH10K_DBG_WMI, "vdev param %d not supported by firmware\n", param_id);
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_SET_PARAM, 0);
    if (status != ZX_OK) { return status; }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_SET_PARAM);
    cmd->vdev_id = vdev_id;
    cmd->param_id = param_id;
    cmd->param_value = param_value;

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi vdev id 0x%x set param %d value %d\n", vdev_id, param_id,
               param_value);
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

static zx_status_t ath10k_wmi_op_gen_vdev_install_key(struct ath10k* ar,
                                                      struct ath10k_msg_buf** msg_buf_ptr,
                                                      const struct wmi_vdev_install_key_arg* arg) {
    struct wmi_vdev_install_key_cmd* cmd;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;

    if (arg->key_cipher == WMI_CIPHER_NONE && arg->key_data != NULL) { return ZX_ERR_INVALID_ARGS; }
    if (arg->key_cipher != WMI_CIPHER_NONE && arg->key_data == NULL) { return ZX_ERR_INVALID_ARGS; }

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_INSTALL_KEY, arg->key_len);
    if (status != ZX_OK) { return status; }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI_VDEV_INSTALL_KEY);
    cmd->vdev_id = arg->vdev_id;
    cmd->key_idx = arg->key_idx;
    cmd->key_flags = arg->key_flags;
    cmd->key_cipher = arg->key_cipher;
    cmd->key_len = arg->key_len;
    cmd->key_txmic_len = arg->key_txmic_len;
    cmd->key_rxmic_len = arg->key_rxmic_len;

    if (arg->macaddr) { memcpy(cmd->peer_macaddr.addr, arg->macaddr, ETH_ALEN); }
    if (arg->key_data) { memcpy(cmd->key_data, arg->key_data, arg->key_len); }

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi vdev install key idx %d cipher %d len %d\n", arg->key_idx,
               arg->key_cipher, arg->key_len);
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

#if 0   // NEEDS PORTING
static struct sk_buff*
ath10k_wmi_op_gen_vdev_spectral_conf(struct ath10k* ar,
                                     const struct wmi_vdev_spectral_conf_arg* arg) {
    struct wmi_vdev_spectral_conf_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_vdev_spectral_conf_cmd*)skb->data;
    cmd->vdev_id = arg->vdev_id;
    cmd->scan_count = arg->scan_count;
    cmd->scan_period = arg->scan_period;
    cmd->scan_priority = arg->scan_priority;
    cmd->scan_fft_size = arg->scan_fft_size;
    cmd->scan_gc_ena = arg->scan_gc_ena;
    cmd->scan_restart_ena = arg->scan_restart_ena;
    cmd->scan_noise_floor_ref = arg->scan_noise_floor_ref;
    cmd->scan_init_delay = arg->scan_init_delay;
    cmd->scan_nb_tone_thr = arg->scan_nb_tone_thr;
    cmd->scan_str_bin_thr = arg->scan_str_bin_thr;
    cmd->scan_wb_rpt_mode = arg->scan_wb_rpt_mode;
    cmd->scan_rssi_rpt_mode = arg->scan_rssi_rpt_mode;
    cmd->scan_rssi_thr = arg->scan_rssi_thr;
    cmd->scan_pwr_format = arg->scan_pwr_format;
    cmd->scan_rpt_mode = arg->scan_rpt_mode;
    cmd->scan_bin_scale = arg->scan_bin_scale;
    cmd->scan_dbm_adj = arg->scan_dbm_adj;
    cmd->scan_chn_mask = arg->scan_chn_mask;

    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_vdev_spectral_enable(struct ath10k* ar, uint32_t vdev_id,
                                       uint32_t trigger, uint32_t enable) {
    struct wmi_vdev_spectral_enable_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_vdev_spectral_enable_cmd*)skb->data;
    cmd->vdev_id = vdev_id;
    cmd->trigger_cmd = trigger;
    cmd->enable_cmd = enable;

    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_peer_create(struct ath10k* ar, uint32_t vdev_id,
                              const uint8_t peer_addr[ETH_ALEN],
                              enum wmi_peer_type peer_type) {
    struct wmi_peer_create_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_peer_create_cmd*)skb->data;
    cmd->vdev_id = vdev_id;
    memcpy(cmd->peer_macaddr.addr, peer_addr, ETH_ALEN);

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi peer create vdev_id %d peer_addr %pM\n",
               vdev_id, peer_addr);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_peer_delete(struct ath10k* ar, uint32_t vdev_id,
                              const uint8_t peer_addr[ETH_ALEN]) {
    struct wmi_peer_delete_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_peer_delete_cmd*)skb->data;
    cmd->vdev_id = vdev_id;
    memcpy(cmd->peer_macaddr.addr, peer_addr, ETH_ALEN);

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi peer delete vdev_id %d peer_addr %pM\n",
               vdev_id, peer_addr);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_peer_flush(struct ath10k* ar, uint32_t vdev_id,
                             const uint8_t peer_addr[ETH_ALEN], uint32_t tid_bitmap) {
    struct wmi_peer_flush_tids_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_peer_flush_tids_cmd*)skb->data;
    cmd->vdev_id         = vdev_id;
    cmd->peer_tid_bitmap = tid_bitmap;
    memcpy(cmd->peer_macaddr.addr, peer_addr, ETH_ALEN);

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi peer flush vdev_id %d peer_addr %pM tids %08x\n",
               vdev_id, peer_addr, tid_bitmap);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_peer_set_param(struct ath10k* ar, uint32_t vdev_id,
                                 const uint8_t* peer_addr,
                                 enum wmi_peer_param param_id,
                                 uint32_t param_value) {
    struct wmi_peer_set_param_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_peer_set_param_cmd*)skb->data;
    cmd->vdev_id     = vdev_id;
    cmd->param_id    = param_id;
    cmd->param_value = param_value;
    memcpy(cmd->peer_macaddr.addr, peer_addr, ETH_ALEN);

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi vdev %d peer 0x%pM set param %d value %d\n",
               vdev_id, peer_addr, param_id, param_value);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_set_psmode(struct ath10k* ar, uint32_t vdev_id,
                             enum wmi_sta_ps_mode psmode) {
    struct wmi_sta_powersave_mode_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_sta_powersave_mode_cmd*)skb->data;
    cmd->vdev_id     = vdev_id;
    cmd->sta_ps_mode = psmode;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi set powersave id 0x%x mode %d\n",
               vdev_id, psmode);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_set_sta_ps(struct ath10k* ar, uint32_t vdev_id,
                             enum wmi_sta_powersave_param param_id,
                             uint32_t value) {
    struct wmi_sta_powersave_param_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_sta_powersave_param_cmd*)skb->data;
    cmd->vdev_id     = vdev_id;
    cmd->param_id    = param_id;
    cmd->param_value = value;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi sta ps param vdev_id 0x%x param %d value %d\n",
               vdev_id, param_id, value);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_set_ap_ps(struct ath10k* ar, uint32_t vdev_id, const uint8_t* mac,
                            enum wmi_ap_ps_peer_param param_id, uint32_t value) {
    struct wmi_ap_ps_peer_cmd* cmd;
    struct sk_buff* skb;

    if (!mac) {
        return ERR_PTR(-EINVAL);
    }

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_ap_ps_peer_cmd*)skb->data;
    cmd->vdev_id = vdev_id;
    cmd->param_id = param_id;
    cmd->param_value = value;
    memcpy(cmd->peer_macaddr.addr, mac, ETH_ALEN);

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi ap ps param vdev_id 0x%X param %d value %d mac_addr %pM\n",
               vdev_id, param_id, value, mac);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_scan_chan_list(struct ath10k* ar,
                                 const struct wmi_scan_chan_list_arg* arg) {
    struct wmi_scan_chan_list_cmd* cmd;
    struct sk_buff* skb;
    struct wmi_channel_arg* ch;
    struct wmi_channel* ci;
    int len;
    int i;

    len = sizeof(*cmd) + arg->n_channels * sizeof(struct wmi_channel);

    skb = ath10k_wmi_alloc_skb(ar, len);
    if (!skb) {
        return ERR_PTR(-EINVAL);
    }

    cmd = (struct wmi_scan_chan_list_cmd*)skb->data;
    cmd->num_scan_chans = arg->n_channels;

    for (i = 0; i < arg->n_channels; i++) {
        ch = &arg->channels[i];
        ci = &cmd->chan_info[i];

        ath10k_wmi_put_wmi_channel(ci, ch);
    }

    return skb;
}

static void
ath10k_wmi_peer_assoc_fill(struct ath10k* ar, void* buf,
                           const struct wmi_peer_assoc_complete_arg* arg) {
    struct wmi_common_peer_assoc_complete_cmd* cmd = buf;

    cmd->vdev_id            = arg->vdev_id;
    cmd->peer_new_assoc     = arg->peer_reassoc ? 0 : 1;
    cmd->peer_associd       = arg->peer_aid;
    cmd->peer_flags         = arg->peer_flags;
    cmd->peer_caps          = arg->peer_caps;
    cmd->peer_listen_intval = arg->peer_listen_intval;
    cmd->peer_ht_caps       = arg->peer_ht_caps;
    cmd->peer_max_mpdu      = arg->peer_max_mpdu;
    cmd->peer_mpdu_density  = arg->peer_mpdu_density;
    cmd->peer_rate_caps     = arg->peer_rate_caps;
    cmd->peer_nss           = arg->peer_num_spatial_streams;
    cmd->peer_vht_caps      = arg->peer_vht_caps;
    cmd->peer_phymode       = arg->peer_phymode;

    memcpy(cmd->peer_macaddr.addr, arg->addr, ETH_ALEN);

    cmd->peer_legacy_rates.num_rates =
        arg->peer_legacy_rates.num_rates;
    memcpy(cmd->peer_legacy_rates.rates, arg->peer_legacy_rates.rates,
           arg->peer_legacy_rates.num_rates);

    cmd->peer_ht_rates.num_rates =
        arg->peer_ht_rates.num_rates;
    memcpy(cmd->peer_ht_rates.rates, arg->peer_ht_rates.rates,
           arg->peer_ht_rates.num_rates);

    cmd->peer_vht_rates.rx_max_rate =
        arg->peer_vht_rates.rx_max_rate;
    cmd->peer_vht_rates.rx_mcs_set =
        arg->peer_vht_rates.rx_mcs_set;
    cmd->peer_vht_rates.tx_max_rate =
        arg->peer_vht_rates.tx_max_rate;
    cmd->peer_vht_rates.tx_mcs_set =
        arg->peer_vht_rates.tx_mcs_set;
}

static void
ath10k_wmi_peer_assoc_fill_main(struct ath10k* ar, void* buf,
                                const struct wmi_peer_assoc_complete_arg* arg) {
    struct wmi_main_peer_assoc_complete_cmd* cmd = buf;

    ath10k_wmi_peer_assoc_fill(ar, buf, arg);
    memset(cmd->peer_ht_info, 0, sizeof(cmd->peer_ht_info));
}

static void
ath10k_wmi_peer_assoc_fill_10_1(struct ath10k* ar, void* buf,
                                const struct wmi_peer_assoc_complete_arg* arg) {
    ath10k_wmi_peer_assoc_fill(ar, buf, arg);
}

static void
ath10k_wmi_peer_assoc_fill_10_2(struct ath10k* ar, void* buf,
                                const struct wmi_peer_assoc_complete_arg* arg) {
    struct wmi_10_2_peer_assoc_complete_cmd* cmd = buf;
    int max_mcs, max_nss;
    uint32_t info0;

    /* TODO: Is using max values okay with firmware? */
    max_mcs = 0xf;
    max_nss = 0xf;

    info0 = SM(max_mcs, WMI_PEER_ASSOC_INFO0_MAX_MCS_IDX) |
            SM(max_nss, WMI_PEER_ASSOC_INFO0_MAX_NSS);

    ath10k_wmi_peer_assoc_fill(ar, buf, arg);
    cmd->info0 = info0;
}

static void
ath10k_wmi_peer_assoc_fill_10_4(struct ath10k* ar, void* buf,
                                const struct wmi_peer_assoc_complete_arg* arg) {
    struct wmi_10_4_peer_assoc_complete_cmd* cmd = buf;

    ath10k_wmi_peer_assoc_fill_10_2(ar, buf, arg);
    if (arg->peer_bw_rxnss_override)
        cmd->peer_bw_rxnss_override =
            (arg->peer_bw_rxnss_override - 1 |
                          BIT(PEER_BW_RXNSS_OVERRIDE_OFFSET));
    else {
        cmd->peer_bw_rxnss_override = 0;
    }
}

static int
ath10k_wmi_peer_assoc_check_arg(const struct wmi_peer_assoc_complete_arg* arg) {
    if (arg->peer_mpdu_density > 16) {
        return -EINVAL;
    }
    if (arg->peer_legacy_rates.num_rates > MAX_SUPPORTED_RATES) {
        return -EINVAL;
    }
    if (arg->peer_ht_rates.num_rates > MAX_SUPPORTED_RATES) {
        return -EINVAL;
    }

    return 0;
}

static struct sk_buff*
ath10k_wmi_op_gen_peer_assoc(struct ath10k* ar,
                             const struct wmi_peer_assoc_complete_arg* arg) {
    size_t len = sizeof(struct wmi_main_peer_assoc_complete_cmd);
    struct sk_buff* skb;
    int ret;

    ret = ath10k_wmi_peer_assoc_check_arg(arg);
    if (ret) {
        return ERR_PTR(ret);
    }

    skb = ath10k_wmi_alloc_skb(ar, len);
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    ath10k_wmi_peer_assoc_fill_main(ar, skb->data, arg);

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi peer assoc vdev %d addr %pM (%s)\n",
               arg->vdev_id, arg->addr,
               arg->peer_reassoc ? "reassociate" : "new");
    return skb;
}

static struct sk_buff*
ath10k_wmi_10_1_op_gen_peer_assoc(struct ath10k* ar,
                                  const struct wmi_peer_assoc_complete_arg* arg) {
    size_t len = sizeof(struct wmi_10_1_peer_assoc_complete_cmd);
    struct sk_buff* skb;
    int ret;

    ret = ath10k_wmi_peer_assoc_check_arg(arg);
    if (ret) {
        return ERR_PTR(ret);
    }

    skb = ath10k_wmi_alloc_skb(ar, len);
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    ath10k_wmi_peer_assoc_fill_10_1(ar, skb->data, arg);

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi peer assoc vdev %d addr %pM (%s)\n",
               arg->vdev_id, arg->addr,
               arg->peer_reassoc ? "reassociate" : "new");
    return skb;
}

static struct sk_buff*
ath10k_wmi_10_2_op_gen_peer_assoc(struct ath10k* ar,
                                  const struct wmi_peer_assoc_complete_arg* arg) {
    size_t len = sizeof(struct wmi_10_2_peer_assoc_complete_cmd);
    struct sk_buff* skb;
    int ret;

    ret = ath10k_wmi_peer_assoc_check_arg(arg);
    if (ret) {
        return ERR_PTR(ret);
    }

    skb = ath10k_wmi_alloc_skb(ar, len);
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    ath10k_wmi_peer_assoc_fill_10_2(ar, skb->data, arg);

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi peer assoc vdev %d addr %pM (%s)\n",
               arg->vdev_id, arg->addr,
               arg->peer_reassoc ? "reassociate" : "new");
    return skb;
}

static struct sk_buff*
ath10k_wmi_10_4_op_gen_peer_assoc(struct ath10k* ar,
                                  const struct wmi_peer_assoc_complete_arg* arg) {
    size_t len = sizeof(struct wmi_10_4_peer_assoc_complete_cmd);
    struct sk_buff* skb;
    int ret;

    ret = ath10k_wmi_peer_assoc_check_arg(arg);
    if (ret) {
        return ERR_PTR(ret);
    }

    skb = ath10k_wmi_alloc_skb(ar, len);
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    ath10k_wmi_peer_assoc_fill_10_4(ar, skb->data, arg);

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi peer assoc vdev %d addr %pM (%s)\n",
               arg->vdev_id, arg->addr,
               arg->peer_reassoc ? "reassociate" : "new");
    return skb;
}

static struct sk_buff*
ath10k_wmi_10_2_op_gen_pdev_get_temperature(struct ath10k* ar) {
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, 0);
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi pdev get temperature\n");
    return skb;
}

static struct sk_buff*
ath10k_wmi_10_2_op_gen_pdev_bss_chan_info(struct ath10k* ar,
        enum wmi_bss_survey_req_type type) {
    struct wmi_pdev_chan_info_req_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_pdev_chan_info_req_cmd*)skb->data;
    cmd->type = type;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi pdev bss info request type %d\n", type);

    return skb;
}

/* This function assumes the beacon is already DMA mapped */
static struct sk_buff*
ath10k_wmi_op_gen_beacon_dma(struct ath10k* ar, uint32_t vdev_id, const void* bcn,
                             size_t bcn_len, uint32_t bcn_paddr, bool dtim_zero,
                             bool deliver_cab) {
    struct wmi_bcn_tx_ref_cmd* cmd;
    struct sk_buff* skb;
    struct ieee80211_hdr* hdr;
    uint16_t fc;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    hdr = (struct ieee80211_hdr*)bcn;
    fc = hdr->frame_control;

    cmd = (struct wmi_bcn_tx_ref_cmd*)skb->data;
    cmd->vdev_id = vdev_id;
    cmd->data_len = bcn_len;
    cmd->data_ptr = bcn_paddr;
    cmd->msdu_id = 0;
    cmd->frame_control = fc;
    cmd->flags = 0;
    cmd->antenna_mask = WMI_BCN_TX_REF_DEF_ANTENNA;

    if (dtim_zero) {
        cmd->flags |= WMI_BCN_TX_REF_FLAG_DTIM_ZERO;
    }

    if (deliver_cab) {
        cmd->flags |= WMI_BCN_TX_REF_FLAG_DELIVER_CAB;
    }

    return skb;
}
#endif  // NEEDS PORTING

void ath10k_wmi_set_wmm_param(struct wmi_wmm_params* params, const struct wmi_wmm_params_arg* arg) {
    params->cwmin = arg->cwmin;
    params->cwmax = arg->cwmax;
    params->aifs = arg->aifs;
    params->txop = arg->txop;
    params->acm = arg->acm;
    params->no_ack = arg->no_ack;
}

#if 0   // NEEDS PORTING
static struct sk_buff*
ath10k_wmi_op_gen_pdev_set_wmm(struct ath10k* ar,
                               const struct wmi_wmm_params_all_arg* arg) {
    struct wmi_pdev_set_wmm_params* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_pdev_set_wmm_params*)skb->data;
    ath10k_wmi_set_wmm_param(&cmd->ac_be, &arg->ac_be);
    ath10k_wmi_set_wmm_param(&cmd->ac_bk, &arg->ac_bk);
    ath10k_wmi_set_wmm_param(&cmd->ac_vi, &arg->ac_vi);
    ath10k_wmi_set_wmm_param(&cmd->ac_vo, &arg->ac_vo);

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi pdev set wmm params\n");
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_request_stats(struct ath10k* ar, uint32_t stats_mask) {
    struct wmi_request_stats_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_request_stats_cmd*)skb->data;
    cmd->stats_id = stats_mask;

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi request stats 0x%08x\n",
               stats_mask);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_force_fw_hang(struct ath10k* ar,
                                enum wmi_force_fw_hang_type type, uint32_t delay_ms) {
    struct wmi_force_fw_hang_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_force_fw_hang_cmd*)skb->data;
    cmd->type = type;
    cmd->delay_ms = delay_ms;

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi force fw hang %d delay %d\n",
               type, delay_ms);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_dbglog_cfg(struct ath10k* ar, uint64_t module_enable,
                             uint32_t log_level) {
    struct wmi_dbglog_cfg_cmd* cmd;
    struct sk_buff* skb;
    uint32_t cfg;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_dbglog_cfg_cmd*)skb->data;

    if (module_enable) {
        cfg = SM(log_level,
                 ATH10K_DBGLOG_CFG_LOG_LVL);
    } else {
        /* set back defaults, all modules with WARN level */
        cfg = SM(ATH10K_DBGLOG_LEVEL_WARN,
                 ATH10K_DBGLOG_CFG_LOG_LVL);
        module_enable = ~0;
    }

    cmd->module_enable = module_enable;
    cmd->module_valid = ~0;
    cmd->config_enable = cfg;
    cmd->config_valid = ATH10K_DBGLOG_CFG_LOG_LVL_MASK;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi dbglog cfg modules %08x %08x config %08x %08x\n",
               cmd->module_enable,
               cmd->module_valid,
               cmd->config_enable,
               cmd->config_valid);
    return skb;
}

static struct sk_buff*
ath10k_wmi_10_4_op_gen_dbglog_cfg(struct ath10k* ar, uint64_t module_enable,
                                  uint32_t log_level) {
    struct wmi_10_4_dbglog_cfg_cmd* cmd;
    struct sk_buff* skb;
    uint32_t cfg;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_10_4_dbglog_cfg_cmd*)skb->data;

    if (module_enable) {
        cfg = SM(log_level,
                 ATH10K_DBGLOG_CFG_LOG_LVL);
    } else {
        /* set back defaults, all modules with WARN level */
        cfg = SM(ATH10K_DBGLOG_LEVEL_WARN,
                 ATH10K_DBGLOG_CFG_LOG_LVL);
        module_enable = ~0;
    }

    cmd->module_enable = module_enable;
    cmd->module_valid = ~0;
    cmd->config_enable = cfg;
    cmd->config_valid = ATH10K_DBGLOG_CFG_LOG_LVL_MASK;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi dbglog cfg modules 0x%016llx 0x%016llx config %08x %08x\n",
               cmd->module_enable,
               cmd->module_valid,
               cmd->config_enable,
               cmd->config_valid);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_pktlog_enable(struct ath10k* ar, uint32_t ev_bitmap) {
    struct wmi_pdev_pktlog_enable_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    ev_bitmap &= ATH10K_PKTLOG_ANY;

    cmd = (struct wmi_pdev_pktlog_enable_cmd*)skb->data;
    cmd->ev_bitmap = ev_bitmap;

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi enable pktlog filter 0x%08x\n",
               ev_bitmap);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_pktlog_disable(struct ath10k* ar) {
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, 0);
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi disable pktlog\n");
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_pdev_set_quiet_mode(struct ath10k* ar, uint32_t period,
                                      uint32_t duration, uint32_t next_offset,
                                      uint32_t enabled) {
    struct wmi_pdev_set_quiet_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_pdev_set_quiet_cmd*)skb->data;
    cmd->period = period;
    cmd->duration = duration;
    cmd->next_start = next_offset;
    cmd->enabled = enabled;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi quiet param: period %u duration %u enabled %d\n",
               period, duration, enabled);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_addba_clear_resp(struct ath10k* ar, uint32_t vdev_id,
                                   const uint8_t* mac) {
    struct wmi_addba_clear_resp_cmd* cmd;
    struct sk_buff* skb;

    if (!mac) {
        return ERR_PTR(-EINVAL);
    }

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_addba_clear_resp_cmd*)skb->data;
    cmd->vdev_id = vdev_id;
    memcpy(cmd->peer_macaddr.addr, mac, ETH_ALEN);

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi addba clear resp vdev_id 0x%X mac_addr %pM\n",
               vdev_id, mac);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_addba_send(struct ath10k* ar, uint32_t vdev_id, const uint8_t* mac,
                             uint32_t tid, uint32_t buf_size) {
    struct wmi_addba_send_cmd* cmd;
    struct sk_buff* skb;

    if (!mac) {
        return ERR_PTR(-EINVAL);
    }

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_addba_send_cmd*)skb->data;
    cmd->vdev_id = vdev_id;
    memcpy(cmd->peer_macaddr.addr, mac, ETH_ALEN);
    cmd->tid = tid;
    cmd->buffersize = buf_size;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi addba send vdev_id 0x%X mac_addr %pM tid %u bufsize %u\n",
               vdev_id, mac, tid, buf_size);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_addba_set_resp(struct ath10k* ar, uint32_t vdev_id, const uint8_t* mac,
                                 uint32_t tid, uint32_t status) {
    struct wmi_addba_setresponse_cmd* cmd;
    struct sk_buff* skb;

    if (!mac) {
        return ERR_PTR(-EINVAL);
    }

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_addba_setresponse_cmd*)skb->data;
    cmd->vdev_id = vdev_id;
    memcpy(cmd->peer_macaddr.addr, mac, ETH_ALEN);
    cmd->tid = tid;
    cmd->statuscode = status;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi addba set resp vdev_id 0x%X mac_addr %pM tid %u status %u\n",
               vdev_id, mac, tid, status);
    return skb;
}

static struct sk_buff*
ath10k_wmi_op_gen_delba_send(struct ath10k* ar, uint32_t vdev_id, const uint8_t* mac,
                             uint32_t tid, uint32_t initiator, uint32_t reason) {
    struct wmi_delba_send_cmd* cmd;
    struct sk_buff* skb;

    if (!mac) {
        return ERR_PTR(-EINVAL);
    }

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_delba_send_cmd*)skb->data;
    cmd->vdev_id = vdev_id;
    memcpy(cmd->peer_macaddr.addr, mac, ETH_ALEN);
    cmd->tid = tid;
    cmd->initiator = initiator;
    cmd->reasoncode = reason;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi delba send vdev_id 0x%X mac_addr %pM tid %u initiator %u reason %u\n",
               vdev_id, mac, tid, initiator, reason);
    return skb;
}

static struct sk_buff*
ath10k_wmi_10_2_4_op_gen_pdev_get_tpc_config(struct ath10k* ar, uint32_t param) {
    struct wmi_pdev_get_tpc_config_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_pdev_get_tpc_config_cmd*)skb->data;
    cmd->param = param;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi pdev get tcp config param:%d\n", param);
    return skb;
}

size_t ath10k_wmi_fw_stats_num_peers(struct list_head* head) {
    struct ath10k_fw_stats_peer* i;
    size_t num = 0;

    list_for_each_entry(i, head, list)
    ++num;

    return num;
}

size_t ath10k_wmi_fw_stats_num_vdevs(struct list_head* head) {
    struct ath10k_fw_stats_vdev* i;
    size_t num = 0;

    list_for_each_entry(i, head, list)
    ++num;

    return num;
}

static void
ath10k_wmi_fw_pdev_base_stats_fill(const struct ath10k_fw_stats_pdev* pdev,
                                   char* buf, uint32_t* length) {
    uint32_t len = *length;
    uint32_t buf_len = ATH10K_FW_STATS_BUF_SIZE;

    len += SNPRINTF_USED(buf + len, buf_len - len, "\n");
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s\n",
                         "ath10k PDEV stats");
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s\n\n",
                         "=================");

    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Channel noise floor", pdev->ch_noise_floor);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10u\n",
                         "Channel TX power", pdev->chan_tx_power);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10u\n",
                         "TX frame count", pdev->tx_frame_count);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10u\n",
                         "RX frame count", pdev->rx_frame_count);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10u\n",
                         "RX clear count", pdev->rx_clear_count);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10u\n",
                         "Cycle count", pdev->cycle_count);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10u\n",
                         "PHY error count", pdev->phy_err_count);

    *length = len;
}

static void
ath10k_wmi_fw_pdev_extra_stats_fill(const struct ath10k_fw_stats_pdev* pdev,
                                    char* buf, uint32_t* length) {
    uint32_t len = *length;
    uint32_t buf_len = ATH10K_FW_STATS_BUF_SIZE;

    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10u\n",
                         "RTS bad count", pdev->rts_bad);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10u\n",
                         "RTS good count", pdev->rts_good);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10u\n",
                         "FCS bad count", pdev->fcs_bad);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10u\n",
                         "No beacon count", pdev->no_beacons);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10u\n",
                         "MIB int count", pdev->mib_int_count);

    len += SNPRINTF_USED(buf + len, buf_len - len, "\n");
    *length = len;
}

static void
ath10k_wmi_fw_pdev_tx_stats_fill(const struct ath10k_fw_stats_pdev* pdev,
                                 char* buf, uint32_t* length) {
    uint32_t len = *length;
    uint32_t buf_len = ATH10K_FW_STATS_BUF_SIZE;

    len += SNPRINTF_USED(buf + len, buf_len - len, "\n%30s\n",
                         "ath10k PDEV TX stats");
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s\n\n",
                         "=================");

    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "HTT cookies queued", pdev->comp_queued);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "HTT cookies disp.", pdev->comp_delivered);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MSDU queued", pdev->msdu_enqued);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MPDU queued", pdev->mpdu_enqued);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MSDUs dropped", pdev->wmm_drop);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Local enqued", pdev->local_enqued);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Local freed", pdev->local_freed);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "HW queued", pdev->hw_queued);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "PPDUs reaped", pdev->hw_reaped);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Num underruns", pdev->underrun);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "PPDUs cleaned", pdev->tx_abort);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MPDUs requed", pdev->mpdus_requed);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Excessive retries", pdev->tx_ko);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "HW rate", pdev->data_rc);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Sched self tiggers", pdev->self_triggers);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Dropped due to SW retries",
                         pdev->sw_retry_failure);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Illegal rate phy errors",
                         pdev->illgl_rate_phy_err);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Pdev continuous xretry", pdev->pdev_cont_xretry);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "TX timeout", pdev->pdev_tx_timeout);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "PDEV resets", pdev->pdev_resets);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "PHY underrun", pdev->phy_underrun);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MPDU is more than txop limit", pdev->txop_ovf);
    *length = len;
}

static void
ath10k_wmi_fw_pdev_rx_stats_fill(const struct ath10k_fw_stats_pdev* pdev,
                                 char* buf, uint32_t* length) {
    uint32_t len = *length;
    uint32_t buf_len = ATH10K_FW_STATS_BUF_SIZE;

    len += SNPRINTF_USED(buf + len, buf_len - len, "\n%30s\n",
                         "ath10k PDEV RX stats");
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s\n\n",
                         "=================");

    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Mid PPDU route change",
                         pdev->mid_ppdu_route_change);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Tot. number of statuses", pdev->status_rcvd);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Extra frags on rings 0", pdev->r0_frags);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Extra frags on rings 1", pdev->r1_frags);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Extra frags on rings 2", pdev->r2_frags);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Extra frags on rings 3", pdev->r3_frags);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MSDUs delivered to HTT", pdev->htt_msdus);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MPDUs delivered to HTT", pdev->htt_mpdus);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MSDUs delivered to stack", pdev->loc_msdus);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MPDUs delivered to stack", pdev->loc_mpdus);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Oversized AMSUs", pdev->oversize_amsdu);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "PHY errors", pdev->phy_errs);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "PHY errors drops", pdev->phy_err_drop);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MPDU errors (FCS, MIC, ENC)", pdev->mpdu_errs);
    *length = len;
}

static void
ath10k_wmi_fw_vdev_stats_fill(const struct ath10k_fw_stats_vdev* vdev,
                              char* buf, uint32_t* length) {
    uint32_t len = *length;
    uint32_t buf_len = ATH10K_FW_STATS_BUF_SIZE;
    int i;

    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %u\n",
                         "vdev id", vdev->vdev_id);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %u\n",
                         "beacon snr", vdev->beacon_snr);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %u\n",
                         "data snr", vdev->data_snr);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %u\n",
                         "num rx frames", vdev->num_rx_frames);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %u\n",
                         "num rts fail", vdev->num_rts_fail);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %u\n",
                         "num rts success", vdev->num_rts_success);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %u\n",
                         "num rx err", vdev->num_rx_err);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %u\n",
                         "num rx discard", vdev->num_rx_discard);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %u\n",
                         "num tx not acked", vdev->num_tx_not_acked);

    for (i = 0 ; i < countof(vdev->num_tx_frames); i++)
        len += SNPRINTF_USED(buf + len, buf_len - len,
                             "%25s [%02d] %u\n",
                             "num tx frames", i,
                             vdev->num_tx_frames[i]);

    for (i = 0 ; i < countof(vdev->num_tx_frames_retries); i++)
        len += SNPRINTF_USED(buf + len, buf_len - len,
                             "%25s [%02d] %u\n",
                             "num tx frames retries", i,
                             vdev->num_tx_frames_retries[i]);

    for (i = 0 ; i < countof(vdev->num_tx_frames_failures); i++)
        len += SNPRINTF_USED(buf + len, buf_len - len,
                             "%25s [%02d] %u\n",
                             "num tx frames failures", i,
                             vdev->num_tx_frames_failures[i]);

    for (i = 0 ; i < countof(vdev->tx_rate_history); i++)
        len += SNPRINTF_USED(buf + len, buf_len - len,
                             "%25s [%02d] 0x%08x\n",
                             "tx rate history", i,
                             vdev->tx_rate_history[i]);

    for (i = 0 ; i < countof(vdev->beacon_rssi_history); i++)
        len += SNPRINTF_USED(buf + len, buf_len - len,
                             "%25s [%02d] %u\n",
                             "beacon rssi history", i,
                             vdev->beacon_rssi_history[i]);

    len += SNPRINTF_USED(buf + len, buf_len - len, "\n");
    *length = len;
}

static void
ath10k_wmi_fw_peer_stats_fill(const struct ath10k_fw_stats_peer* peer,
                              char* buf, uint32_t* length) {
    uint32_t len = *length;
    uint32_t buf_len = ATH10K_FW_STATS_BUF_SIZE;

    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %pM\n",
                         "Peer MAC address", peer->peer_macaddr);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %u\n",
                         "Peer RSSI", peer->peer_rssi);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %u\n",
                         "Peer TX rate", peer->peer_tx_rate);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %u\n",
                         "Peer RX rate", peer->peer_rx_rate);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %u\n",
                         "Peer RX duration", peer->rx_duration);

    len += SNPRINTF_USED(buf + len, buf_len - len, "\n");
    *length = len;
}

void ath10k_wmi_main_op_fw_stats_fill(struct ath10k* ar,
                                      struct ath10k_fw_stats* fw_stats,
                                      char* buf) {
    uint32_t len = 0;
    uint32_t buf_len = ATH10K_FW_STATS_BUF_SIZE;
    const struct ath10k_fw_stats_pdev* pdev;
    const struct ath10k_fw_stats_vdev* vdev;
    const struct ath10k_fw_stats_peer* peer;
    size_t num_peers;
    size_t num_vdevs;

    mtx_lock(&ar->data_lock);

    pdev = list_first_entry_or_null(&fw_stats->pdevs,
                                    struct ath10k_fw_stats_pdev, list);
    if (!pdev) {
        ath10k_warn("failed to get pdev stats\n");
        goto unlock;
    }

    num_peers = ath10k_wmi_fw_stats_num_peers(&fw_stats->peers);
    num_vdevs = ath10k_wmi_fw_stats_num_vdevs(&fw_stats->vdevs);

    ath10k_wmi_fw_pdev_base_stats_fill(pdev, buf, &len);
    ath10k_wmi_fw_pdev_tx_stats_fill(pdev, buf, &len);
    ath10k_wmi_fw_pdev_rx_stats_fill(pdev, buf, &len);

    len += SNPRINTF_USED(buf + len, buf_len - len, "\n");
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s (%zu)\n",
                         "ath10k VDEV stats", num_vdevs);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s\n\n",
                         "=================");

    list_for_each_entry(vdev, &fw_stats->vdevs, list) {
        ath10k_wmi_fw_vdev_stats_fill(vdev, buf, &len);
    }

    len += SNPRINTF_USED(buf + len, buf_len - len, "\n");
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s (%zu)\n",
                         "ath10k PEER stats", num_peers);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s\n\n",
                         "=================");

    list_for_each_entry(peer, &fw_stats->peers, list) {
        ath10k_wmi_fw_peer_stats_fill(peer, buf, &len);
    }

unlock:
    mtx_unlock(&ar->data_lock);

    if (len >= buf_len) {
        buf[len - 1] = 0;
    } else {
        buf[len] = 0;
    }
}

void ath10k_wmi_10x_op_fw_stats_fill(struct ath10k* ar,
                                     struct ath10k_fw_stats* fw_stats,
                                     char* buf) {
    unsigned int len = 0;
    unsigned int buf_len = ATH10K_FW_STATS_BUF_SIZE;
    const struct ath10k_fw_stats_pdev* pdev;
    const struct ath10k_fw_stats_vdev* vdev;
    const struct ath10k_fw_stats_peer* peer;
    size_t num_peers;
    size_t num_vdevs;

    mtx_lock(&ar->data_lock);

    pdev = list_first_entry_or_null(&fw_stats->pdevs,
                                    struct ath10k_fw_stats_pdev, list);
    if (!pdev) {
        ath10k_warn("failed to get pdev stats\n");
        goto unlock;
    }

    num_peers = ath10k_wmi_fw_stats_num_peers(&fw_stats->peers);
    num_vdevs = ath10k_wmi_fw_stats_num_vdevs(&fw_stats->vdevs);

    ath10k_wmi_fw_pdev_base_stats_fill(pdev, buf, &len);
    ath10k_wmi_fw_pdev_extra_stats_fill(pdev, buf, &len);
    ath10k_wmi_fw_pdev_tx_stats_fill(pdev, buf, &len);
    ath10k_wmi_fw_pdev_rx_stats_fill(pdev, buf, &len);

    len += SNPRINTF_USED(buf + len, buf_len - len, "\n");
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s (%zu)\n",
                         "ath10k VDEV stats", num_vdevs);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s\n\n",
                         "=================");

    list_for_each_entry(vdev, &fw_stats->vdevs, list) {
        ath10k_wmi_fw_vdev_stats_fill(vdev, buf, &len);
    }

    len += SNPRINTF_USED(buf + len, buf_len - len, "\n");
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s (%zu)\n",
                         "ath10k PEER stats", num_peers);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s\n\n",
                         "=================");

    list_for_each_entry(peer, &fw_stats->peers, list) {
        ath10k_wmi_fw_peer_stats_fill(peer, buf, &len);
    }

unlock:
    mtx_unlock(&ar->data_lock);

    if (len >= buf_len) {
        buf[len - 1] = 0;
    } else {
        buf[len] = 0;
    }
}

static struct sk_buff*
ath10k_wmi_op_gen_pdev_enable_adaptive_cca(struct ath10k* ar, uint8_t enable,
        uint32_t detect_level, uint32_t detect_margin) {
    struct wmi_pdev_set_adaptive_cca_params* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_pdev_set_adaptive_cca_params*)skb->data;
    cmd->enable = enable;
    cmd->cca_detect_level = detect_level;
    cmd->cca_detect_margin = detect_margin;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi pdev set adaptive cca params enable:%d detection level:%d detection margin:%d\n",
               enable, detect_level, detect_margin);
    return skb;
}

void ath10k_wmi_10_4_op_fw_stats_fill(struct ath10k* ar,
                                      struct ath10k_fw_stats* fw_stats,
                                      char* buf) {
    uint32_t len = 0;
    uint32_t buf_len = ATH10K_FW_STATS_BUF_SIZE;
    const struct ath10k_fw_stats_pdev* pdev;
    const struct ath10k_fw_stats_vdev* vdev;
    const struct ath10k_fw_stats_peer* peer;
    size_t num_peers;
    size_t num_vdevs;

    mtx_lock(&ar->data_lock);

    pdev = list_first_entry_or_null(&fw_stats->pdevs,
                                    struct ath10k_fw_stats_pdev, list);
    if (!pdev) {
        ath10k_warn("failed to get pdev stats\n");
        goto unlock;
    }

    num_peers = ath10k_wmi_fw_stats_num_peers(&fw_stats->peers);
    num_vdevs = ath10k_wmi_fw_stats_num_vdevs(&fw_stats->vdevs);

    ath10k_wmi_fw_pdev_base_stats_fill(pdev, buf, &len);
    ath10k_wmi_fw_pdev_extra_stats_fill(pdev, buf, &len);
    ath10k_wmi_fw_pdev_tx_stats_fill(pdev, buf, &len);

    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "HW paused", pdev->hw_paused);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Seqs posted", pdev->seq_posted);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Seqs failed queueing", pdev->seq_failed_queueing);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Seqs completed", pdev->seq_completed);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Seqs restarted", pdev->seq_restarted);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MU Seqs posted", pdev->mu_seq_posted);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MPDUs SW flushed", pdev->mpdus_sw_flush);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MPDUs HW filtered", pdev->mpdus_hw_filter);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MPDUs truncated", pdev->mpdus_truncated);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MPDUs receive no ACK", pdev->mpdus_ack_failed);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "MPDUs expired", pdev->mpdus_expired);

    ath10k_wmi_fw_pdev_rx_stats_fill(pdev, buf, &len);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s %10d\n",
                         "Num Rx Overflow errors", pdev->rx_ovfl_errs);

    len += SNPRINTF_USED(buf + len, buf_len - len, "\n");
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s (%zu)\n",
                         "ath10k VDEV stats", num_vdevs);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s\n\n",
                         "=================");

    list_for_each_entry(vdev, &fw_stats->vdevs, list) {
        ath10k_wmi_fw_vdev_stats_fill(vdev, buf, &len);
    }

    len += SNPRINTF_USED(buf + len, buf_len - len, "\n");
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s (%zu)\n",
                         "ath10k PEER stats", num_peers);
    len += SNPRINTF_USED(buf + len, buf_len - len, "%30s\n\n",
                         "=================");

    list_for_each_entry(peer, &fw_stats->peers, list) {
        ath10k_wmi_fw_peer_stats_fill(peer, buf, &len);
    }

unlock:
    mtx_unlock(&ar->data_lock);

    if (len >= buf_len) {
        buf[len - 1] = 0;
    } else {
        buf[len] = 0;
    }
}
#endif  // NEEDS PORTING

int ath10k_wmi_op_get_vdev_subtype(struct ath10k* ar, enum wmi_vdev_subtype subtype) {
    switch (subtype) {
    case WMI_VDEV_SUBTYPE_NONE:
        return WMI_VDEV_SUBTYPE_LEGACY_NONE;
    case WMI_VDEV_SUBTYPE_P2P_DEVICE:
        return WMI_VDEV_SUBTYPE_LEGACY_P2P_DEV;
    case WMI_VDEV_SUBTYPE_P2P_CLIENT:
        return WMI_VDEV_SUBTYPE_LEGACY_P2P_CLI;
    case WMI_VDEV_SUBTYPE_P2P_GO:
        return WMI_VDEV_SUBTYPE_LEGACY_P2P_GO;
    case WMI_VDEV_SUBTYPE_PROXY_STA:
        return WMI_VDEV_SUBTYPE_LEGACY_PROXY_STA;
    case WMI_VDEV_SUBTYPE_MESH_11S:
    case WMI_VDEV_SUBTYPE_MESH_NON_11S:
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_ERR_NOT_SUPPORTED;
}

#if 0   // NEEDS PORTING
static int ath10k_wmi_10_2_4_op_get_vdev_subtype(struct ath10k* ar,
        enum wmi_vdev_subtype subtype) {
    switch (subtype) {
    case WMI_VDEV_SUBTYPE_NONE:
        return WMI_VDEV_SUBTYPE_10_2_4_NONE;
    case WMI_VDEV_SUBTYPE_P2P_DEVICE:
        return WMI_VDEV_SUBTYPE_10_2_4_P2P_DEV;
    case WMI_VDEV_SUBTYPE_P2P_CLIENT:
        return WMI_VDEV_SUBTYPE_10_2_4_P2P_CLI;
    case WMI_VDEV_SUBTYPE_P2P_GO:
        return WMI_VDEV_SUBTYPE_10_2_4_P2P_GO;
    case WMI_VDEV_SUBTYPE_PROXY_STA:
        return WMI_VDEV_SUBTYPE_10_2_4_PROXY_STA;
    case WMI_VDEV_SUBTYPE_MESH_11S:
        return WMI_VDEV_SUBTYPE_10_2_4_MESH_11S;
    case WMI_VDEV_SUBTYPE_MESH_NON_11S:
        return -ENOTSUPP;
    }
    return -ENOTSUPP;
}

static int ath10k_wmi_10_4_op_get_vdev_subtype(struct ath10k* ar,
        enum wmi_vdev_subtype subtype) {
    switch (subtype) {
    case WMI_VDEV_SUBTYPE_NONE:
        return WMI_VDEV_SUBTYPE_10_4_NONE;
    case WMI_VDEV_SUBTYPE_P2P_DEVICE:
        return WMI_VDEV_SUBTYPE_10_4_P2P_DEV;
    case WMI_VDEV_SUBTYPE_P2P_CLIENT:
        return WMI_VDEV_SUBTYPE_10_4_P2P_CLI;
    case WMI_VDEV_SUBTYPE_P2P_GO:
        return WMI_VDEV_SUBTYPE_10_4_P2P_GO;
    case WMI_VDEV_SUBTYPE_PROXY_STA:
        return WMI_VDEV_SUBTYPE_10_4_PROXY_STA;
    case WMI_VDEV_SUBTYPE_MESH_11S:
        return WMI_VDEV_SUBTYPE_10_4_MESH_11S;
    case WMI_VDEV_SUBTYPE_MESH_NON_11S:
        return WMI_VDEV_SUBTYPE_10_4_MESH_NON_11S;
    }
    return -ENOTSUPP;
}

static struct sk_buff*
ath10k_wmi_10_4_ext_resource_config(struct ath10k* ar,
                                    enum wmi_host_platform_type type,
                                    uint32_t fw_feature_bitmap) {
    struct wmi_ext_resource_config_10_4_cmd* cmd;
    struct sk_buff* skb;

    skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd));
    if (!skb) {
        return ERR_PTR(-ENOMEM);
    }

    cmd = (struct wmi_ext_resource_config_10_4_cmd*)skb->data;
    cmd->host_platform_config = type;
    cmd->fw_feature_bitmap = fw_feature_bitmap;

    ath10k_dbg(ar, ATH10K_DBG_WMI,
               "wmi ext resource config host type %d firmware feature bitmap %08x\n",
               type, fw_feature_bitmap);
    return skb;
}
#endif  // NEEDS PORTING

static zx_status_t ath10k_wmi_op_gen_echo(struct ath10k* ar, struct ath10k_msg_buf** msg_buf_ptr,
                                          uint32_t value) {
    struct wmi_echo_cmd* cmd;
    struct ath10k_msg_buf* msg_buf;
    zx_status_t status;

    status = ath10k_msg_buf_alloc(ar, &msg_buf, ATH10K_MSG_TYPE_WMI_ECHO_CMD, 0);
    if (status != ZX_OK) { return status; }

    cmd = ath10k_msg_buf_get_header(msg_buf, ATH10K_MSG_TYPE_WMI_ECHO_CMD);
    cmd->value = value;

    ath10k_dbg(ar, ATH10K_DBG_WMI, "wmi echo value 0x%08x\n", value);
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

zx_status_t ath10k_wmi_barrier(struct ath10k* ar) {
    zx_status_t ret;

    mtx_lock(&ar->data_lock);
    sync_completion_reset(&ar->wmi.barrier);
    mtx_unlock(&ar->data_lock);

    ret = ath10k_wmi_echo(ar, ATH10K_WMI_BARRIER_ECHO_ID);
    if (ret != ZX_OK) {
        ath10k_warn("failed to submit wmi echo: %d\n", ret);
        return ret;
    }

    if (sync_completion_wait(&ar->wmi.barrier, ATH10K_WMI_BARRIER_TIMEOUT) == ZX_ERR_TIMED_OUT) {
        return ZX_ERR_TIMED_OUT;
    }

    return ZX_OK;
}

static const struct wmi_ops wmi_ops = {
    .rx = ath10k_wmi_op_rx,
    .map_svc = wmi_main_svc_map,

    .pull_scan = ath10k_wmi_op_pull_scan_ev,
    .pull_mgmt_rx = ath10k_wmi_op_pull_mgmt_rx_ev,
#if 0   // NEEDS PORTING
    .pull_ch_info = ath10k_wmi_op_pull_ch_info_ev,
#endif  // NEEDS PORTING
    .pull_vdev_start = ath10k_wmi_op_pull_vdev_start_ev,
#if 0   // NEEDS PORTING
    .pull_peer_kick = ath10k_wmi_op_pull_peer_kick_ev,
    .pull_swba = ath10k_wmi_op_pull_swba_ev,
    .pull_phyerr_hdr = ath10k_wmi_op_pull_phyerr_ev_hdr,
    .pull_phyerr = ath10k_wmi_op_pull_phyerr_ev,
#endif  // NEEDS PORTING
    .pull_svc_rdy = ath10k_wmi_main_op_pull_svc_rdy_ev,
    .pull_rdy = ath10k_wmi_op_pull_rdy_ev,
#if 0   // NEEDS PORTING
    .pull_fw_stats = ath10k_wmi_main_op_pull_fw_stats,
#endif  // NEEDS PORTING
    .pull_roam_ev = ath10k_wmi_op_pull_roam_ev,
    .pull_echo_ev = ath10k_wmi_op_pull_echo_ev,

    .gen_pdev_suspend = ath10k_wmi_op_gen_pdev_suspend,
    .gen_pdev_resume = ath10k_wmi_op_gen_pdev_resume,
    .gen_pdev_set_rd = ath10k_wmi_op_gen_pdev_set_rd,
    .gen_pdev_set_param = ath10k_wmi_op_gen_pdev_set_param,
#if 0   // NEEDS PORTING
    .gen_init = ath10k_wmi_op_gen_init,
    .gen_start_scan = ath10k_wmi_op_gen_start_scan,
    .gen_stop_scan = ath10k_wmi_op_gen_stop_scan,
#endif  // NEEDS PORTING
    .gen_vdev_create = ath10k_wmi_op_gen_vdev_create,
    .gen_vdev_delete = ath10k_wmi_op_gen_vdev_delete,
    .gen_vdev_start = ath10k_wmi_op_gen_vdev_start,
    .gen_vdev_stop = ath10k_wmi_op_gen_vdev_stop,
    .gen_vdev_up = ath10k_wmi_op_gen_vdev_up,
    .gen_vdev_down = ath10k_wmi_op_gen_vdev_down,
    .gen_vdev_set_param = ath10k_wmi_op_gen_vdev_set_param,
    .gen_vdev_install_key = ath10k_wmi_op_gen_vdev_install_key,
#if 0   // NEEDS PORTING
    .gen_vdev_spectral_conf = ath10k_wmi_op_gen_vdev_spectral_conf,
    .gen_vdev_spectral_enable = ath10k_wmi_op_gen_vdev_spectral_enable,
    /* .gen_vdev_wmm_conf not implemented */
    .gen_peer_create = ath10k_wmi_op_gen_peer_create,
    .gen_peer_delete = ath10k_wmi_op_gen_peer_delete,
    .gen_peer_flush = ath10k_wmi_op_gen_peer_flush,
    .gen_peer_set_param = ath10k_wmi_op_gen_peer_set_param,
    .gen_peer_assoc = ath10k_wmi_op_gen_peer_assoc,
    .gen_set_psmode = ath10k_wmi_op_gen_set_psmode,
    .gen_set_sta_ps = ath10k_wmi_op_gen_set_sta_ps,
    .gen_set_ap_ps = ath10k_wmi_op_gen_set_ap_ps,
    .gen_scan_chan_list = ath10k_wmi_op_gen_scan_chan_list,
    .gen_beacon_dma = ath10k_wmi_op_gen_beacon_dma,
    .gen_pdev_set_wmm = ath10k_wmi_op_gen_pdev_set_wmm,
    .gen_request_stats = ath10k_wmi_op_gen_request_stats,
    .gen_force_fw_hang = ath10k_wmi_op_gen_force_fw_hang,
    .gen_mgmt_tx = ath10k_wmi_op_gen_mgmt_tx,
    .gen_dbglog_cfg = ath10k_wmi_op_gen_dbglog_cfg,
    .gen_pktlog_enable = ath10k_wmi_op_gen_pktlog_enable,
    .gen_pktlog_disable = ath10k_wmi_op_gen_pktlog_disable,
    .gen_pdev_set_quiet_mode = ath10k_wmi_op_gen_pdev_set_quiet_mode,
    /* .gen_pdev_get_temperature not implemented */
    .gen_addba_clear_resp = ath10k_wmi_op_gen_addba_clear_resp,
    .gen_addba_send = ath10k_wmi_op_gen_addba_send,
    .gen_addba_set_resp = ath10k_wmi_op_gen_addba_set_resp,
    .gen_delba_send = ath10k_wmi_op_gen_delba_send,
    .fw_stats_fill = ath10k_wmi_main_op_fw_stats_fill,
    .get_vdev_subtype = ath10k_wmi_op_get_vdev_subtype,
#endif  // NEEDS PORTING
    .gen_echo = ath10k_wmi_op_gen_echo,
    /* .gen_bcn_tmpl not implemented */
    /* .gen_prb_tmpl not implemented */
    /* .gen_p2p_go_bcn_ie not implemented */
    /* .gen_adaptive_qcs not implemented */
    /* .gen_pdev_enable_adaptive_cca not implemented */
};

static const struct wmi_ops wmi_10_1_ops = {
#if 0   // NEEDS PORTING
    .rx = ath10k_wmi_10_1_op_rx,
#endif  // NEEDS PORTING
    .map_svc = wmi_10x_svc_map,
    .pull_svc_rdy = ath10k_wmi_10x_op_pull_svc_rdy_ev,
#if 0   // NEEDS PORTING
    .pull_fw_stats = ath10k_wmi_10x_op_pull_fw_stats,
    .gen_init = ath10k_wmi_10_1_op_gen_init,
    .gen_pdev_set_rd = ath10k_wmi_10x_op_gen_pdev_set_rd,
    .gen_start_scan = ath10k_wmi_10x_op_gen_start_scan,
    .gen_peer_assoc = ath10k_wmi_10_1_op_gen_peer_assoc,
#endif  // NEEDS PORTING
    /* .gen_pdev_get_temperature not implemented */

    /* shared with main branch */
    .pull_scan = ath10k_wmi_op_pull_scan_ev,
    .pull_mgmt_rx = ath10k_wmi_op_pull_mgmt_rx_ev,
#if 0   // NEEDS PORTING
    .pull_ch_info = ath10k_wmi_op_pull_ch_info_ev,
#endif  // NEEDS PORTING
    .pull_vdev_start = ath10k_wmi_op_pull_vdev_start_ev,
#if 0   // NEEDS PORTING
    .pull_peer_kick = ath10k_wmi_op_pull_peer_kick_ev,
    .pull_swba = ath10k_wmi_op_pull_swba_ev,
    .pull_phyerr_hdr = ath10k_wmi_op_pull_phyerr_ev_hdr,
    .pull_phyerr = ath10k_wmi_op_pull_phyerr_ev,
#endif  // NEEDS PORTING
    .pull_rdy = ath10k_wmi_op_pull_rdy_ev,
    .pull_roam_ev = ath10k_wmi_op_pull_roam_ev,
    .pull_echo_ev = ath10k_wmi_op_pull_echo_ev,

    .gen_pdev_suspend = ath10k_wmi_op_gen_pdev_suspend,
    .gen_pdev_resume = ath10k_wmi_op_gen_pdev_resume,
    .gen_pdev_set_param = ath10k_wmi_op_gen_pdev_set_param,
#if 0   // NEEDS PORTING
    .gen_stop_scan = ath10k_wmi_op_gen_stop_scan,
#endif  // NEEDS PORTING
    .gen_vdev_create = ath10k_wmi_op_gen_vdev_create,
    .gen_vdev_delete = ath10k_wmi_op_gen_vdev_delete,
    .gen_vdev_start = ath10k_wmi_op_gen_vdev_start,
    .gen_vdev_stop = ath10k_wmi_op_gen_vdev_stop,
    .gen_vdev_up = ath10k_wmi_op_gen_vdev_up,
    .gen_vdev_down = ath10k_wmi_op_gen_vdev_down,
    .gen_vdev_set_param = ath10k_wmi_op_gen_vdev_set_param,
    .gen_vdev_install_key = ath10k_wmi_op_gen_vdev_install_key,
#if 0   // NEEDS PORTING
    .gen_vdev_spectral_conf = ath10k_wmi_op_gen_vdev_spectral_conf,
    .gen_vdev_spectral_enable = ath10k_wmi_op_gen_vdev_spectral_enable,
    /* .gen_vdev_wmm_conf not implemented */
    .gen_peer_create = ath10k_wmi_op_gen_peer_create,
    .gen_peer_delete = ath10k_wmi_op_gen_peer_delete,
    .gen_peer_flush = ath10k_wmi_op_gen_peer_flush,
    .gen_peer_set_param = ath10k_wmi_op_gen_peer_set_param,
    .gen_set_psmode = ath10k_wmi_op_gen_set_psmode,
    .gen_set_sta_ps = ath10k_wmi_op_gen_set_sta_ps,
    .gen_set_ap_ps = ath10k_wmi_op_gen_set_ap_ps,
    .gen_scan_chan_list = ath10k_wmi_op_gen_scan_chan_list,
    .gen_beacon_dma = ath10k_wmi_op_gen_beacon_dma,
    .gen_pdev_set_wmm = ath10k_wmi_op_gen_pdev_set_wmm,
    .gen_request_stats = ath10k_wmi_op_gen_request_stats,
    .gen_force_fw_hang = ath10k_wmi_op_gen_force_fw_hang,
    .gen_mgmt_tx = ath10k_wmi_op_gen_mgmt_tx,
    .gen_dbglog_cfg = ath10k_wmi_op_gen_dbglog_cfg,
    .gen_pktlog_enable = ath10k_wmi_op_gen_pktlog_enable,
    .gen_pktlog_disable = ath10k_wmi_op_gen_pktlog_disable,
    .gen_pdev_set_quiet_mode = ath10k_wmi_op_gen_pdev_set_quiet_mode,
    .gen_addba_clear_resp = ath10k_wmi_op_gen_addba_clear_resp,
    .gen_addba_send = ath10k_wmi_op_gen_addba_send,
    .gen_addba_set_resp = ath10k_wmi_op_gen_addba_set_resp,
    .gen_delba_send = ath10k_wmi_op_gen_delba_send,
    .fw_stats_fill = ath10k_wmi_10x_op_fw_stats_fill,
    .get_vdev_subtype = ath10k_wmi_op_get_vdev_subtype,
#endif  // NEEDS PORTING
    .gen_echo = ath10k_wmi_op_gen_echo,
    /* .gen_bcn_tmpl not implemented */
    /* .gen_prb_tmpl not implemented */
    /* .gen_p2p_go_bcn_ie not implemented */
    /* .gen_adaptive_qcs not implemented */
    /* .gen_pdev_enable_adaptive_cca not implemented */
};

static const struct wmi_ops wmi_10_2_ops = {
#if 0   // NEEDS PORTING
    .rx = ath10k_wmi_10_2_op_rx,
    .pull_fw_stats = ath10k_wmi_10_2_op_pull_fw_stats,
    .gen_init = ath10k_wmi_10_2_op_gen_init,
    .gen_peer_assoc = ath10k_wmi_10_2_op_gen_peer_assoc,
#endif  // NEEDS PORTING
    /* .gen_pdev_get_temperature not implemented */

    /* shared with 10.1 */
    .map_svc = wmi_10x_svc_map,
    .pull_svc_rdy = ath10k_wmi_10x_op_pull_svc_rdy_ev,
#if 0   // NEEDS PORTING
    .gen_pdev_set_rd = ath10k_wmi_10x_op_gen_pdev_set_rd,
    .gen_start_scan = ath10k_wmi_10x_op_gen_start_scan,
#endif  // NEEDS PORTING
    .gen_echo = ath10k_wmi_op_gen_echo,

    .pull_scan = ath10k_wmi_op_pull_scan_ev,
    .pull_mgmt_rx = ath10k_wmi_op_pull_mgmt_rx_ev,
#if 0   // NEEDS PORTING
    .pull_ch_info = ath10k_wmi_op_pull_ch_info_ev,
#endif  // NEEDS PORTING
    .pull_vdev_start = ath10k_wmi_op_pull_vdev_start_ev,
#if 0   // NEEDS PORTING
    .pull_peer_kick = ath10k_wmi_op_pull_peer_kick_ev,
    .pull_swba = ath10k_wmi_op_pull_swba_ev,
    .pull_phyerr_hdr = ath10k_wmi_op_pull_phyerr_ev_hdr,
    .pull_phyerr = ath10k_wmi_op_pull_phyerr_ev,
#endif  // NEEDS PORTING
    .pull_rdy = ath10k_wmi_op_pull_rdy_ev,
    .pull_roam_ev = ath10k_wmi_op_pull_roam_ev,
    .pull_echo_ev = ath10k_wmi_op_pull_echo_ev,

    .gen_pdev_suspend = ath10k_wmi_op_gen_pdev_suspend,
    .gen_pdev_resume = ath10k_wmi_op_gen_pdev_resume,
    .gen_pdev_set_param = ath10k_wmi_op_gen_pdev_set_param,
#if 0   // NEEDS PORTING
    .gen_stop_scan = ath10k_wmi_op_gen_stop_scan,
#endif  // NEEDS PORTING
    .gen_vdev_create = ath10k_wmi_op_gen_vdev_create,
    .gen_vdev_delete = ath10k_wmi_op_gen_vdev_delete,
    .gen_vdev_start = ath10k_wmi_op_gen_vdev_start,
    .gen_vdev_stop = ath10k_wmi_op_gen_vdev_stop,
    .gen_vdev_up = ath10k_wmi_op_gen_vdev_up,
    .gen_vdev_down = ath10k_wmi_op_gen_vdev_down,
    .gen_vdev_set_param = ath10k_wmi_op_gen_vdev_set_param,
    .gen_vdev_install_key = ath10k_wmi_op_gen_vdev_install_key,
#if 0   // NEEDS PORTING
    .gen_vdev_spectral_conf = ath10k_wmi_op_gen_vdev_spectral_conf,
    .gen_vdev_spectral_enable = ath10k_wmi_op_gen_vdev_spectral_enable,
    /* .gen_vdev_wmm_conf not implemented */
    .gen_peer_create = ath10k_wmi_op_gen_peer_create,
    .gen_peer_delete = ath10k_wmi_op_gen_peer_delete,
    .gen_peer_flush = ath10k_wmi_op_gen_peer_flush,
    .gen_peer_set_param = ath10k_wmi_op_gen_peer_set_param,
    .gen_set_psmode = ath10k_wmi_op_gen_set_psmode,
    .gen_set_sta_ps = ath10k_wmi_op_gen_set_sta_ps,
    .gen_set_ap_ps = ath10k_wmi_op_gen_set_ap_ps,
    .gen_scan_chan_list = ath10k_wmi_op_gen_scan_chan_list,
    .gen_beacon_dma = ath10k_wmi_op_gen_beacon_dma,
    .gen_pdev_set_wmm = ath10k_wmi_op_gen_pdev_set_wmm,
    .gen_request_stats = ath10k_wmi_op_gen_request_stats,
    .gen_force_fw_hang = ath10k_wmi_op_gen_force_fw_hang,
    .gen_mgmt_tx = ath10k_wmi_op_gen_mgmt_tx,
    .gen_dbglog_cfg = ath10k_wmi_op_gen_dbglog_cfg,
    .gen_pktlog_enable = ath10k_wmi_op_gen_pktlog_enable,
    .gen_pktlog_disable = ath10k_wmi_op_gen_pktlog_disable,
    .gen_pdev_set_quiet_mode = ath10k_wmi_op_gen_pdev_set_quiet_mode,
    .gen_addba_clear_resp = ath10k_wmi_op_gen_addba_clear_resp,
    .gen_addba_send = ath10k_wmi_op_gen_addba_send,
    .gen_addba_set_resp = ath10k_wmi_op_gen_addba_set_resp,
    .gen_delba_send = ath10k_wmi_op_gen_delba_send,
    .fw_stats_fill = ath10k_wmi_10x_op_fw_stats_fill,
    .get_vdev_subtype = ath10k_wmi_op_get_vdev_subtype,
#endif  // NEEDS PORTING
    /* .gen_pdev_enable_adaptive_cca not implemented */
};

static const struct wmi_ops wmi_10_2_4_ops = {
    .rx = ath10k_wmi_10_2_op_rx,
#if 0   // NEEDS PORTING
    .pull_fw_stats = ath10k_wmi_10_2_4_op_pull_fw_stats,
#endif  // NEEDS PORTING
    .gen_init = ath10k_wmi_10_2_op_gen_init,
#if 0   // NEEDS PORTING
    .gen_peer_assoc = ath10k_wmi_10_2_op_gen_peer_assoc,
    .gen_pdev_get_temperature = ath10k_wmi_10_2_op_gen_pdev_get_temperature,
    .gen_pdev_bss_chan_info_req = ath10k_wmi_10_2_op_gen_pdev_bss_chan_info,
#endif  // NEEDS PORTING

    /* shared with 10.1 */
    .map_svc = wmi_10x_svc_map,
    .pull_svc_rdy = ath10k_wmi_10x_op_pull_svc_rdy_ev,
#if 0   // NEEDS PORTING
    .gen_pdev_set_rd = ath10k_wmi_10x_op_gen_pdev_set_rd,
    .gen_start_scan = ath10k_wmi_10x_op_gen_start_scan,
#endif  // NEEDS PORTING
    .gen_echo = ath10k_wmi_op_gen_echo,

    .pull_scan = ath10k_wmi_op_pull_scan_ev,
    .pull_mgmt_rx = ath10k_wmi_op_pull_mgmt_rx_ev,
#if 0   // NEEDS PORTING
    .pull_ch_info = ath10k_wmi_op_pull_ch_info_ev,
#endif  // NEEDS PORTING
    .pull_vdev_start = ath10k_wmi_op_pull_vdev_start_ev,
#if 0   // NEEDS PORTING
    .pull_peer_kick = ath10k_wmi_op_pull_peer_kick_ev,
    .pull_swba = ath10k_wmi_10_2_4_op_pull_swba_ev,
    .pull_phyerr_hdr = ath10k_wmi_op_pull_phyerr_ev_hdr,
    .pull_phyerr = ath10k_wmi_op_pull_phyerr_ev,
#endif  // NEEDS PORTING
    .pull_rdy = ath10k_wmi_op_pull_rdy_ev,
    .pull_roam_ev = ath10k_wmi_op_pull_roam_ev,
    .pull_echo_ev = ath10k_wmi_op_pull_echo_ev,

    .gen_pdev_suspend = ath10k_wmi_op_gen_pdev_suspend,
    .gen_pdev_resume = ath10k_wmi_op_gen_pdev_resume,
    .gen_pdev_set_param = ath10k_wmi_op_gen_pdev_set_param,
#if 0   // NEEDS PORTING
    .gen_stop_scan = ath10k_wmi_op_gen_stop_scan,
#endif  // NEEDS PORTING
    .gen_vdev_create = ath10k_wmi_op_gen_vdev_create,
    .gen_vdev_delete = ath10k_wmi_op_gen_vdev_delete,
    .gen_vdev_start = ath10k_wmi_op_gen_vdev_start,
    .gen_vdev_stop = ath10k_wmi_op_gen_vdev_stop,
    .gen_vdev_up = ath10k_wmi_op_gen_vdev_up,
    .gen_vdev_down = ath10k_wmi_op_gen_vdev_down,
    .gen_vdev_set_param = ath10k_wmi_op_gen_vdev_set_param,
    .gen_vdev_install_key = ath10k_wmi_op_gen_vdev_install_key,
#if 0   // NEEDS PORTING
    .gen_vdev_spectral_conf = ath10k_wmi_op_gen_vdev_spectral_conf,
    .gen_vdev_spectral_enable = ath10k_wmi_op_gen_vdev_spectral_enable,
    .gen_peer_create = ath10k_wmi_op_gen_peer_create,
    .gen_peer_delete = ath10k_wmi_op_gen_peer_delete,
    .gen_peer_flush = ath10k_wmi_op_gen_peer_flush,
    .gen_peer_set_param = ath10k_wmi_op_gen_peer_set_param,
    .gen_set_psmode = ath10k_wmi_op_gen_set_psmode,
    .gen_set_sta_ps = ath10k_wmi_op_gen_set_sta_ps,
    .gen_set_ap_ps = ath10k_wmi_op_gen_set_ap_ps,
    .gen_scan_chan_list = ath10k_wmi_op_gen_scan_chan_list,
    .gen_beacon_dma = ath10k_wmi_op_gen_beacon_dma,
    .gen_pdev_set_wmm = ath10k_wmi_op_gen_pdev_set_wmm,
    .gen_request_stats = ath10k_wmi_op_gen_request_stats,
    .gen_force_fw_hang = ath10k_wmi_op_gen_force_fw_hang,
    .gen_mgmt_tx = ath10k_wmi_op_gen_mgmt_tx,
    .gen_dbglog_cfg = ath10k_wmi_op_gen_dbglog_cfg,
    .gen_pktlog_enable = ath10k_wmi_op_gen_pktlog_enable,
    .gen_pktlog_disable = ath10k_wmi_op_gen_pktlog_disable,
    .gen_pdev_set_quiet_mode = ath10k_wmi_op_gen_pdev_set_quiet_mode,
    .gen_addba_clear_resp = ath10k_wmi_op_gen_addba_clear_resp,
    .gen_addba_send = ath10k_wmi_op_gen_addba_send,
    .gen_addba_set_resp = ath10k_wmi_op_gen_addba_set_resp,
    .gen_delba_send = ath10k_wmi_op_gen_delba_send,
    .gen_pdev_get_tpc_config = ath10k_wmi_10_2_4_op_gen_pdev_get_tpc_config,
    .fw_stats_fill = ath10k_wmi_10x_op_fw_stats_fill,
    .gen_pdev_enable_adaptive_cca = ath10k_wmi_op_gen_pdev_enable_adaptive_cca,
    .get_vdev_subtype = ath10k_wmi_10_2_4_op_get_vdev_subtype,
#endif  // NEEDS PORTING
    /* .gen_bcn_tmpl not implemented */
    /* .gen_prb_tmpl not implemented */
    /* .gen_p2p_go_bcn_ie not implemented */
    /* .gen_adaptive_qcs not implemented */
};

static const struct wmi_ops wmi_10_4_ops = {
#if 0   // NEEDS PORTING
    .rx = ath10k_wmi_10_4_op_rx,
    .map_svc = wmi_10_4_svc_map,

    .pull_fw_stats = ath10k_wmi_10_4_op_pull_fw_stats,
#endif  // NEEDS PORTING
    .pull_scan = ath10k_wmi_op_pull_scan_ev,
#if 0   // NEEDS PORTING
    .pull_mgmt_rx = ath10k_wmi_10_4_op_pull_mgmt_rx_ev,
    .pull_ch_info = ath10k_wmi_10_4_op_pull_ch_info_ev,
#endif  // NEEDS PORTING
    .pull_vdev_start = ath10k_wmi_op_pull_vdev_start_ev,
#if 0   // NEEDS PORTING
    .pull_peer_kick = ath10k_wmi_op_pull_peer_kick_ev,
    .pull_swba = ath10k_wmi_10_4_op_pull_swba_ev,
    .pull_phyerr_hdr = ath10k_wmi_10_4_op_pull_phyerr_ev_hdr,
    .pull_phyerr = ath10k_wmi_10_4_op_pull_phyerr_ev,
#endif  // NEEDS PORTING
    .pull_svc_rdy = ath10k_wmi_main_op_pull_svc_rdy_ev,
    .pull_rdy = ath10k_wmi_op_pull_rdy_ev,
    .pull_roam_ev = ath10k_wmi_op_pull_roam_ev,
#if 0   // NEEDS PORTING
    .get_txbf_conf_scheme = ath10k_wmi_10_4_txbf_conf_scheme,
#endif  // NEEDS PORTING

    .gen_pdev_suspend = ath10k_wmi_op_gen_pdev_suspend,
    .gen_pdev_resume = ath10k_wmi_op_gen_pdev_resume,
#if 0   // NEEDS PORTING
    .gen_pdev_set_rd = ath10k_wmi_10x_op_gen_pdev_set_rd,
#endif  // NEEDS PORTING
    .gen_pdev_set_param = ath10k_wmi_op_gen_pdev_set_param,
#if 0   // NEEDS PORTING
    .gen_init = ath10k_wmi_10_4_op_gen_init,
    .gen_start_scan = ath10k_wmi_op_gen_start_scan,
    .gen_stop_scan = ath10k_wmi_op_gen_stop_scan,
#endif  // NEEDS PORTING
    .gen_vdev_create = ath10k_wmi_op_gen_vdev_create,
    .gen_vdev_delete = ath10k_wmi_op_gen_vdev_delete,
    .gen_vdev_start = ath10k_wmi_op_gen_vdev_start,
    .gen_vdev_stop = ath10k_wmi_op_gen_vdev_stop,
    .gen_vdev_up = ath10k_wmi_op_gen_vdev_up,
    .gen_vdev_down = ath10k_wmi_op_gen_vdev_down,
    .gen_vdev_set_param = ath10k_wmi_op_gen_vdev_set_param,
    .gen_vdev_install_key = ath10k_wmi_op_gen_vdev_install_key,
#if 0   // NEEDS PORTING
    .gen_vdev_spectral_conf = ath10k_wmi_op_gen_vdev_spectral_conf,
    .gen_vdev_spectral_enable = ath10k_wmi_op_gen_vdev_spectral_enable,
    .gen_peer_create = ath10k_wmi_op_gen_peer_create,
    .gen_peer_delete = ath10k_wmi_op_gen_peer_delete,
    .gen_peer_flush = ath10k_wmi_op_gen_peer_flush,
    .gen_peer_set_param = ath10k_wmi_op_gen_peer_set_param,
    .gen_peer_assoc = ath10k_wmi_10_4_op_gen_peer_assoc,
    .gen_set_psmode = ath10k_wmi_op_gen_set_psmode,
    .gen_set_sta_ps = ath10k_wmi_op_gen_set_sta_ps,
    .gen_set_ap_ps = ath10k_wmi_op_gen_set_ap_ps,
    .gen_scan_chan_list = ath10k_wmi_op_gen_scan_chan_list,
    .gen_beacon_dma = ath10k_wmi_op_gen_beacon_dma,
    .gen_pdev_set_wmm = ath10k_wmi_op_gen_pdev_set_wmm,
    .gen_force_fw_hang = ath10k_wmi_op_gen_force_fw_hang,
    .gen_mgmt_tx = ath10k_wmi_op_gen_mgmt_tx,
    .gen_dbglog_cfg = ath10k_wmi_10_4_op_gen_dbglog_cfg,
    .gen_pktlog_enable = ath10k_wmi_op_gen_pktlog_enable,
    .gen_pktlog_disable = ath10k_wmi_op_gen_pktlog_disable,
    .gen_pdev_set_quiet_mode = ath10k_wmi_op_gen_pdev_set_quiet_mode,
    .gen_addba_clear_resp = ath10k_wmi_op_gen_addba_clear_resp,
    .gen_addba_send = ath10k_wmi_op_gen_addba_send,
    .gen_addba_set_resp = ath10k_wmi_op_gen_addba_set_resp,
    .gen_delba_send = ath10k_wmi_op_gen_delba_send,
    .fw_stats_fill = ath10k_wmi_10_4_op_fw_stats_fill,
    .ext_resource_config = ath10k_wmi_10_4_ext_resource_config,
#endif  // NEEDS PORTING

    /* shared with 10.2 */
    .pull_echo_ev = ath10k_wmi_op_pull_echo_ev,
#if 0   // NEEDS PORTING
    .gen_request_stats = ath10k_wmi_op_gen_request_stats,
    .gen_pdev_get_temperature = ath10k_wmi_10_2_op_gen_pdev_get_temperature,
    .get_vdev_subtype = ath10k_wmi_10_4_op_get_vdev_subtype,
    .gen_pdev_bss_chan_info_req = ath10k_wmi_10_2_op_gen_pdev_bss_chan_info,
#endif  // NEEDS PORTING
    .gen_echo = ath10k_wmi_op_gen_echo,
#if 0   // NEEDS PORTING
    .gen_pdev_get_tpc_config = ath10k_wmi_10_2_4_op_gen_pdev_get_tpc_config,
#endif  // NEEDS PORTING
};

zx_status_t ath10k_wmi_attach(struct ath10k* ar) {
    switch (ar->running_fw->fw_file.wmi_op_version) {
    case ATH10K_FW_WMI_OP_VERSION_10_4:
        ath10k_err("WMI v10.4 not supported\n");
        ar->wmi.ops = &wmi_10_4_ops;
        ar->wmi.cmd = &wmi_10_4_cmd_map;
        ar->wmi.vdev_param = &wmi_10_4_vdev_param_map;
        ar->wmi.pdev_param = &wmi_10_4_pdev_param_map;
        ar->wmi.peer_flags = &wmi_10_2_peer_flags_map;
        break;
    case ATH10K_FW_WMI_OP_VERSION_10_2_4:
        ath10k_err("WMI v10.2.4 not supported\n");
        ar->wmi.cmd = &wmi_10_2_4_cmd_map;
        ar->wmi.ops = &wmi_10_2_4_ops;
        ar->wmi.vdev_param = &wmi_10_2_4_vdev_param_map;
        ar->wmi.pdev_param = &wmi_10_2_4_pdev_param_map;
        ar->wmi.peer_flags = &wmi_10_2_peer_flags_map;
        break;
    case ATH10K_FW_WMI_OP_VERSION_10_2:
        ath10k_err("WMI v10.2 not supported\n");
        ar->wmi.cmd = &wmi_10_2_cmd_map;
        ar->wmi.ops = &wmi_10_2_ops;
        ar->wmi.vdev_param = &wmi_10x_vdev_param_map;
        ar->wmi.pdev_param = &wmi_10x_pdev_param_map;
        ar->wmi.peer_flags = &wmi_10_2_peer_flags_map;
        break;
    case ATH10K_FW_WMI_OP_VERSION_10_1:
        ath10k_err("WMI v10.1 not supported\n");
        ar->wmi.cmd = &wmi_10x_cmd_map;
        ar->wmi.ops = &wmi_10_1_ops;
        ar->wmi.vdev_param = &wmi_10x_vdev_param_map;
        ar->wmi.pdev_param = &wmi_10x_pdev_param_map;
        ar->wmi.peer_flags = &wmi_10x_peer_flags_map;
        break;
    case ATH10K_FW_WMI_OP_VERSION_MAIN:
        ath10k_err("WMI main version not supported\n");
        ar->wmi.cmd = &wmi_cmd_map;
        ar->wmi.ops = &wmi_ops;
        ar->wmi.vdev_param = &wmi_vdev_param_map;
        ar->wmi.pdev_param = &wmi_pdev_param_map;
        ar->wmi.peer_flags = &wmi_peer_flags_map;
        break;
    case ATH10K_FW_WMI_OP_VERSION_TLV:
        ath10k_wmi_tlv_attach(ar);
        break;
    case ATH10K_FW_WMI_OP_VERSION_UNSET:
    case ATH10K_FW_WMI_OP_VERSION_MAX:
        ath10k_err("unsupported WMI op version: %d\n", ar->running_fw->fw_file.wmi_op_version);
        return ZX_ERR_NOT_SUPPORTED;
    }

    ar->wmi.service_ready = SYNC_COMPLETION_INIT;
    ar->wmi.unified_ready = SYNC_COMPLETION_INIT;
    ar->wmi.barrier = SYNC_COMPLETION_INIT;

    return ZX_OK;
}

void ath10k_wmi_free_host_mem(struct ath10k* ar) {
    unsigned int i;

    /* free the host memory chunks requested by firmware */
    for (i = 0; i < ar->wmi.num_mem_chunks; i++) {
        io_buffer_release(&ar->wmi.mem_chunks[i].handle);
    }

    ar->wmi.num_mem_chunks = 0;
}

void ath10k_wmi_detach(struct ath10k* ar) {
#if 0   // NEEDS PORTING
    cancel_work_sync(&ar->svc_rdy_work);

    if (ar->svc_rdy_skb) {
        dev_kfree_skb(ar->svc_rdy_skb);
    }
#endif  // NEEDS PORTING
}
