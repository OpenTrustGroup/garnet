print("we are in tbox config lua")

--  cpus
sos_config:setCpus(4)

--  vgic
--  gicd_paddr
--  gicr_paddr
--[[
void setVgicPaddr(size_t gicd, size_t gicr)
--]]
sos_config:setVgicPaddr(0x8000000, 0x80a0000)

sos_config:setCmdline("earlycon=pl011,0xffff32000 console=hvc0 INITTAB=/etc/inittab.tbox")

--  vmem_auto // insert memory node & map PA to guest os
--[[
void setVmemAuto(std::string name, std::string node_name,
    uint64_t base_addr, uint64_t size,
    uint8_t policy, uint8_t is_phy_mem)
--]]
sos_config:setVmemAuto("vmlog_sink_tbox", "", 0x0, 0x00, 1, 0)

-- tipc-vq-notifier-irq
--sos_config:setTipcVqNotifierIrq(246)

--**************************************************************
-- usb interrupt
-- char *getSysEnv(const char *env, const char *default_value)
local USB_SWITCH_value = getSysEnv("USB_SWITCH", "T")

print("tbox config ending... lua")

