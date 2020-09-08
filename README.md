#  esp-qcloud 

## 介绍

esp-qcloud 面向使用乐鑫芯处接入腾讯物联网平台的终端设备开发者。为了最大限度的发挥芯片的性能，本项目直接基于使用 [腾讯云物联网开发平台通信协议]() 进行开发的，而非将 [腾讯云物联 IoT C-SDK](https://github.com/tencentyun/qcloud-iot-sdk-embedded-c) 移植到乐鑫平台。 

本项目当前只适用于 ESP32/ESP32-S2 开发板


## 功能列表
- [ ] [配网]()
    - [x] softap
    - [x] provisioning softap
    - [x] airkiss
    - [x] esp-touch v1
    - [ ] esp-touch v2
    - [ ] provisioning ble

- [ ] [认证绑定](https://cloud.tencent.com/document/product/634/35272)
    - [x] 密钥认证
    - [ ] 证书认证
    - [ ] 动态注册认证

- [ ] 控制
    - [x] 设备状态上报与状态设置
    - [ ] 事件
    - [ ] 网关设备

- [ ] 升级
    - [x] OTA
    - [ ] 断点续传

- [ ] 调试
    - [x] 日志上报服务器
    - [x] 日志存储到 flash
    - [x] 日志串口调试

- [ ] 生产
    - [x] 设备配置信息写入
    - [ ] 设备配置信息生成
    - [ ] 加密


## 编译和运行

### 安装 ESP32 SDK
ESP32/ESP32-S2 SDK 安装方法参见： [ESP-IDF 编程指南](http://esp-idf.readthedocs.io/zh_CN/latest/)

> 注： 本项目基于 [esp-idf v4.2](https://github.com/espressif/esp-idf/tree/release/v4.2) 进行开发的， 通过 `cmake` 进行编译，即使用 `idf.py、 进行构建项目

### 单独构建本项目

1. 下载
```shall
git clone https://github.com/espressif/esp-qcloud.git
```

2. [创建产品](https://cloud.tencent.com/document/product/1081/41155)
登录 [物联网开发平台控制台](https://console.cloud.tencent.com/iotexplorer),创建测试设备，认证方式：选择“密钥认证”
 
3. 修改配置
- 进入 `examples/led_light` 目录
- 打开 `menuconfig` 进行配置, 配置路径：`(Top) -> Example Configuration`
    ```
    idf.py menuconfig
    ```

4. 编译
```shell
idf.py monitor erase_flash  flash
```

## 本项目作为库子模块
建议将此项目作为 `git` 子模块,你无需在此项目中复制或编辑任何文件，可以更方便的维护和更新本项目

```shell
mkdir -p components
git submodule add https://github.com/espressif/esp-qcloud.git components/qcloud
```
