## 概述
此程序为hypervisor激活测试程序。该程序运行成功后会激活hypervisor
因其包含敏感秘钥，所以不能公开，仅能用于内部相关人员开发测试使用

## 获取代码：
通过get_code.sh脚本获取代码（代码受到权限管控，部分人员拥有权限）

## 目录结构：
googletest是依赖谷歌测试程序库
hyp-license_test是测试程序

## 编译：
执行目录下build.sh脚本

## 运行：
编译成功会在hyp-license_test/hyp-license_test/out文件夹中生成可执行程序hyp-license_test
将该程序push到设备中
例如QEMU下scp -o StrictHostKeyChecking=no ./hyp-license_test root@192.168.3.53:/data/hyp-license_test
在QEMU的data目录运行程序./hyp-license_test
