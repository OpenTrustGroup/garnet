print("we are in product lua")

--  vmem
--product_config:setVmem("device0", 0x00000000, 0x00000000, 0x40000000, 1, 0)

--  ProductSpec
product_config:setProductSpec("product1", "sos1.lua")
product_config:setProductSpec("product2", "sos2.lua")
product_config:setProductSpec("product3", "sos3.lua")

print("product config ending... lua")

