print("we are in sos config lua")

--  cpus
sos_config:setCpus(4)

--  vgic
--  gicd_paddr
--  gicr_paddr
--[[
void setVgicPaddr(size_t gicd, size_t gicr)
--]]
sos_config:setVgicPaddr(0x8000000, 0x80a0000)

-- vgic percpu_interrupt
sos_config:setVgicPercpuIrq(15)

-- vgic interrupts
local interrupts = {
    "36-69",
    "71-149",
    "152-177",
    "179",
    "181-190",
    "201-220",
    "225-244",
    "246-251",
    "253",
    "255",
    "257-379",
    "381-512"
}
for _, spec in ipairs(interrupts) do
    for irq in spec:gmatch("%d+") do
        local start_irq = tonumber(irq)
        local hyphen_index = spec:find("-", nil, true)

        if hyphen_index then
            local end_irq = tonumber(spec:sub(hyphen_index + 1))
            for i = start_irq, end_irq do
                sos_config:setVgicIrq(i)
            end
            break
        else
            sos_config:setVgicIrq(start_irq)
        end
    end
end

-- ipc-mbox
sos_config:setIpcMbox(150, 151)

-- tipc-vq-notifier-irq
sos_config:setTipcVqNotifierIrq(245)

-- vhm-irq
sos_config:setVhmIrq(254)

-- guest-reserved-memory
sos_config:setGuestReservedMemory(256)

-- vsock-irqs
VsockIrqs = {380, 382}
for i = 1, #VsockIrqs do
    sos_config:setVsockIrqs(VsockIrqs[i])
end

-- vtee-notifier-irq
sos_config:setVteeNotifierIrq(513)

sos_config:setVmem("IO", 0x00000000, 0x00000000, 0x40000000, 1, 0)

print("sos config ending... lua")

