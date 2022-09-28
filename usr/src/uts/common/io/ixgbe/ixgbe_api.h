/*
 * CDDL HEADER START
 *
 * Copyright(c) 2007-2009 Intel Corporation. All rights reserved.
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at:
 *      http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When using or redistributing this file, you may do so under the
 * License only. No other modification of this header is permitted.
 *
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/* IntelVersion: 1.77 scm_100309_002210 */

#ifndef _IXGBE_API_H
#define	_IXGBE_API_H

#include "ixgbe_type.h"

s32 ixgbe_init_shared_code(struct ixgbe_hw *hw);

s32 ixgbe_set_mac_type(struct ixgbe_hw *hw);
s32 ixgbe_init_hw(struct ixgbe_hw *hw);
s32 ixgbe_reset_hw(struct ixgbe_hw *hw);
s32 ixgbe_start_hw(struct ixgbe_hw *hw);
s32 ixgbe_clear_hw_cntrs(struct ixgbe_hw *hw);
enum ixgbe_media_type ixgbe_get_media_type(struct ixgbe_hw *hw);
s32 ixgbe_get_mac_addr(struct ixgbe_hw *hw, u8 *mac_addr);
s32 ixgbe_get_bus_info(struct ixgbe_hw *hw);
u32 ixgbe_get_num_of_tx_queues(struct ixgbe_hw *hw);
u32 ixgbe_get_num_of_rx_queues(struct ixgbe_hw *hw);
s32 ixgbe_stop_adapter(struct ixgbe_hw *hw);
s32 ixgbe_read_pba_num(struct ixgbe_hw *hw, u32 *pba_num);

s32 ixgbe_identify_phy(struct ixgbe_hw *hw);
s32 ixgbe_reset_phy(struct ixgbe_hw *hw);
s32 ixgbe_read_phy_reg(struct ixgbe_hw *hw, u32 reg_addr, u32 device_type,
    u16 *phy_data);
s32 ixgbe_write_phy_reg(struct ixgbe_hw *hw, u32 reg_addr, u32 device_type,
    u16 phy_data);

s32 ixgbe_setup_phy_link(struct ixgbe_hw *hw);
s32 ixgbe_check_phy_link(struct ixgbe_hw *hw,
    ixgbe_link_speed *speed, bool *link_up);
s32 ixgbe_setup_phy_link_speed(struct ixgbe_hw *hw, ixgbe_link_speed speed,
    bool autoneg, bool autoneg_wait_to_complete);
s32 ixgbe_setup_link(struct ixgbe_hw *hw, ixgbe_link_speed speed,
    bool autoneg, bool autoneg_wait_to_complete);
s32 ixgbe_check_link(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
    bool *link_up, bool link_up_wait_to_complete);
s32 ixgbe_get_link_capabilities(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
    bool *autoneg);
s32 ixgbe_led_on(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_led_off(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_blink_led_start(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_blink_led_stop(struct ixgbe_hw *hw, u32 index);

s32 ixgbe_init_eeprom_params(struct ixgbe_hw *hw);
s32 ixgbe_write_eeprom(struct ixgbe_hw *hw, u16 offset, u16 data);
s32 ixgbe_read_eeprom(struct ixgbe_hw *hw, u16 offset, u16 *data);
s32 ixgbe_validate_eeprom_checksum(struct ixgbe_hw *hw, u16 *checksum_val);
s32 ixgbe_update_eeprom_checksum(struct ixgbe_hw *hw);

s32 ixgbe_insert_mac_addr(struct ixgbe_hw *hw, u8 *addr, u32 vmdq);
s32 ixgbe_set_rar(struct ixgbe_hw *hw, u32 index, u8 *addr, u32 vmdq,
    u32 enable_addr);
s32 ixgbe_clear_rar(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_set_vmdq(struct ixgbe_hw *hw, u32 rar, u32 vmdq);
s32 ixgbe_clear_vmdq(struct ixgbe_hw *hw, u32 rar, u32 vmdq);
s32 ixgbe_init_rx_addrs(struct ixgbe_hw *hw);
u32 ixgbe_get_num_rx_addrs(struct ixgbe_hw *hw);
s32 ixgbe_update_uc_addr_list(struct ixgbe_hw *hw, u8 *addr_list,
    u32 addr_count, ixgbe_mc_addr_itr func);
s32 ixgbe_update_mc_addr_list(struct ixgbe_hw *hw, u8 *mc_addr_list,
    u32 mc_addr_count, ixgbe_mc_addr_itr func);
void ixgbe_add_uc_addr(struct ixgbe_hw *hw, u8 *addr_list, u32 vmdq);
s32 ixgbe_enable_mc(struct ixgbe_hw *hw);
s32 ixgbe_disable_mc(struct ixgbe_hw *hw);
s32 ixgbe_clear_vfta(struct ixgbe_hw *hw);
s32 ixgbe_set_vfta(struct ixgbe_hw *hw, u32 vlan, u32 vind, bool vlan_on);

s32 ixgbe_fc_enable(struct ixgbe_hw *hw, s32 packetbuf_num);

void ixgbe_set_mta(struct ixgbe_hw *hw, u8 *mc_addr);
s32 ixgbe_get_phy_firmware_version(struct ixgbe_hw *hw,
    u16 *firmware_version);
s32 ixgbe_read_analog_reg8(struct ixgbe_hw *hw, u32 reg, u8 *val);
s32 ixgbe_write_analog_reg8(struct ixgbe_hw *hw, u32 reg, u8 val);
s32 ixgbe_init_uta_tables(struct ixgbe_hw *hw);
s32 ixgbe_read_i2c_eeprom(struct ixgbe_hw *hw, u8 byte_offset, u8 *eeprom_data);
u32 ixgbe_get_supported_physical_layer(struct ixgbe_hw *hw);
s32 ixgbe_enable_rx_dma(struct ixgbe_hw *hw, u32 regval);
s32 ixgbe_reinit_fdir_tables_82599(struct ixgbe_hw *hw);
s32 ixgbe_init_fdir_signature_82599(struct ixgbe_hw *hw, u32 pballoc);
s32 ixgbe_init_fdir_perfect_82599(struct ixgbe_hw *hw, u32 pballoc);
s32 ixgbe_fdir_add_signature_filter_82599(struct ixgbe_hw *hw,
    struct ixgbe_atr_input *input, u8 queue);
s32 ixgbe_fdir_add_perfect_filter_82599(struct ixgbe_hw *hw,
    struct ixgbe_atr_input *input, u16 soft_id, u8 queue);
u16 ixgbe_atr_compute_hash_82599(struct ixgbe_atr_input *input, u32 key);
s32 ixgbe_atr_set_vlan_id_82599(struct ixgbe_atr_input *input, u16 vlan_id);
s32 ixgbe_atr_set_src_ipv4_82599(struct ixgbe_atr_input *input, u32 src_addr);
s32 ixgbe_atr_set_dst_ipv4_82599(struct ixgbe_atr_input *input, u32 dst_addr);
s32 ixgbe_atr_set_src_ipv6_82599(struct ixgbe_atr_input *input, u32 src_addr_1,
    u32 src_addr_2, u32 src_addr_3, u32 src_addr_4);
s32 ixgbe_atr_set_dst_ipv6_82599(struct ixgbe_atr_input *input, u32 dst_addr_1,
    u32 dst_addr_2, u32 dst_addr_3, u32 dst_addr_4);
s32 ixgbe_atr_set_src_port_82599(struct ixgbe_atr_input *input, u16 src_port);
s32 ixgbe_atr_set_dst_port_82599(struct ixgbe_atr_input *input, u16 dst_port);
s32 ixgbe_atr_set_flex_byte_82599(struct ixgbe_atr_input *input, u16 flex_byte);
s32 ixgbe_atr_set_vm_pool_82599(struct ixgbe_atr_input *input, u8 vm_pool);
s32 ixgbe_atr_set_l4type_82599(struct ixgbe_atr_input *input, u8 l4type);
s32 ixgbe_atr_get_vlan_id_82599(struct ixgbe_atr_input *input, u16 *vlan_id);
s32 ixgbe_atr_get_src_ipv4_82599(struct ixgbe_atr_input *input, u32 *src_addr);
s32 ixgbe_atr_get_dst_ipv4_82599(struct ixgbe_atr_input *input, u32 *dst_addr);
s32 ixgbe_atr_get_src_ipv6_82599(struct ixgbe_atr_input *input, u32 *src_addr_1,
    u32 *src_addr_2, u32 *src_addr_3, u32 *src_addr_4);
s32 ixgbe_atr_get_dst_ipv6_82599(struct ixgbe_atr_input *input, u32 *dst_addr_1,
    u32 *dst_addr_2, u32 *dst_addr_3, u32 *dst_addr_4);
s32 ixgbe_atr_get_src_port_82599(struct ixgbe_atr_input *input, u16 *src_port);
s32 ixgbe_atr_get_dst_port_82599(struct ixgbe_atr_input *input, u16 *dst_port);
s32 ixgbe_atr_get_flex_byte_82599(struct ixgbe_atr_input *input,
    u16 *flex_byte);
s32 ixgbe_atr_get_vm_pool_82599(struct ixgbe_atr_input *input, u8 *vm_pool);
s32 ixgbe_atr_get_l4type_82599(struct ixgbe_atr_input *input, u8 *l4type);
s32 ixgbe_read_i2c_byte(struct ixgbe_hw *hw, u8 byte_offset, u8 dev_addr,
    u8 *data);
s32 ixgbe_write_i2c_byte(struct ixgbe_hw *hw, u8 byte_offset, u8 dev_addr,
    u8 data);
s32 ixgbe_write_i2c_eeprom(struct ixgbe_hw *hw, u8 byte_offset, u8 eeprom_data);
s32 ixgbe_get_san_mac_addr(struct ixgbe_hw *hw, u8 *san_mac_addr);
s32 ixgbe_set_san_mac_addr(struct ixgbe_hw *hw, u8 *san_mac_addr);
s32 ixgbe_get_device_caps(struct ixgbe_hw *hw, u16 *device_caps);
s32 ixgbe_acquire_swfw_semaphore(struct ixgbe_hw *hw, u16 mask);
void ixgbe_release_swfw_semaphore(struct ixgbe_hw *hw, u16 mask);
s32 ixgbe_get_wwn_prefix(struct ixgbe_hw *hw, u16 *wwnn_prefix,
    u16 *wwpn_prefix);

#endif /* _IXGBE_API_H */
