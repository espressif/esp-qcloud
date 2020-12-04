# 脚本使用说明

`tools` 目录下的脚本可帮你快速创建、获取设备信息。脚本依赖环境为 `python3` ，需搭建腾讯云 `Python SDK`，具体搭建请参考 [腾讯云开发者工具套件](https://cloud.tencent.com/document/sdk/Python)。更多 API 使用细节，请参考 [API文档](https://cloud.tencent.com/document/product/1081/37178)。

- `create_dev.py` 批量创建设备
- `get_dev_list.py` 批量获取设备三元组信息

## 批量创建设备

参考命令:
```python
python3 create_dev.py -id "xxxxx" -key "xxxxx" -pid "xxxxx" -num 10
```

该命令需要 4 个参数，缺一不可，用于在指定产品上创建设备。

- `id` 调用 API 接口使用的 ID
- `key` 调用 API 接口使用的密钥
- `pid` 所要创建设备的产品 ID
- `num` 所要创建设备的个数

> **id 与 key 等同于你的账号与密码，泄露可能造成您的云上资产损失**


## 批量查询设备

参考命令:
```python
python3 get_dev_list.py -id "xxxxx" -key "xxxxx" -pid "xxxxx" -limit 10 -offset 10
```

该命令需要 5 个参数，缺一不可，用于查询设备的三元组信息，并生成 csv 文件，生成的 csv 文件可用于批量生成 bin 文件。

- `id` 调用 API 接口使用的 ID
- `key` 调用 API 接口使用的密钥
- `pid` 所要创建设备的产品 ID
- `limit` 所要查询的个数
- `offset` 所要查询的页数的偏移量

> 设备列表 1 页有 10 条设备记录，limit 参数最大为 100。
