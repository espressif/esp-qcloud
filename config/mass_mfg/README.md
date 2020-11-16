# 量产说明

`腾讯物联网开发平台 (IoT Explorer) ` 在使用密钥认证的情况下，需要在每个设备上烧写与设备对应的 `产品 ID (PRODUCT_ID)` 、 `设备名称 (DEVICE_NAME)`、 `设备密钥 (DEVICE_SECRET)`。

为了简化流程，我们使用 IDF 的 NVS 分区功能，将 `PRODUCT_ID`、 `DEVICE_NAME`、`DEVICE_SECRET` 通过 NVS 分区生成工具或量产工具生成对应的 `NVS分区bin文件`，该分区中利用 NVS 结构保存了 `PRODUCT_ID` 、`DEVICE_NAME` 、`DEVICE_SECRET` 的键值对。生成的 `NVS分区bin文件` 可以通过 `esptool` 或其他烧写工具直接烧录到 NVS 分区对应的起始扇区，`partitions` 分区表中指明了分区的起始地址。软件可通过 NVS 相关接口读取 `PRODUCT_ID` 、`DEVICE_NAME` 、`DEVICE_SECRET` 信息。

请参照 [partitions_2MB.csv](../partition_table/partitions_2MB.csv) 和 [partitions_4MB.csv](../partition_table/partitions_4MB.csv) 中 `fctry` 的起始地址进行烧录，也可根据实际项目对 `partitions` 进行调整，但一定要保证 `partitions` 中 `fctry` 的实际地址与烧录地址保持吻合。

关于 NVS 分区生成工具、量产工具，请参考：
- [NVS](https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/storage/nvs_flash.html)
- [NVS 分区生成工具](https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/storage/nvs_partition_gen.html)
- [量产工具](https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/storage/mass_mfg.html)

## 单一 bin 生成

在调试过程中，建议使用该方式。

1. `mass_mfg` 目录中有一组参考配置：`single_mfg_config.csv` ，请拷贝成你的配置文件，如 `my_single_mfg_config.csv`。

    ```shell
    cp single_mfg_config.csv my_single_mfg_config.csv
    ```

2. 使用你的 `PRODUCT_ID`、 `DEVICE_NAME`、 `DEVICE_SECRET` 对 `my_single_mfg_config.csv` 进行修改：

    ```shell
    key,type,encoding,value
    qcloud-key,namespace,,
    product_id,data,string,PRODUCT_ID
    device_name,data,string,DEVICE_NAME
    device_secret,data,string,DEVICE_SECRET
    ```

3. 修改完成后，使用如下命令生成对应的 `NVS分区bin文件`：

    ```shell
    $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate my_single_mfg_config.csv my_single_mfg.bin 0x4000
    ```

4. 使用 `esptool` 工具将生成的 `NVS分区bin文件` 烧入对应的 `sector`。
    `esp32`、`esp32-s2` 使用如下命令：

    ```shell
    $IDF_PATH/components/esptool_py/esptool/esptool.py write_flash 0x15000 my_single_mfg.bin
    ```

## 批量 bin 生成

量产时采用单 bin 生成工具会极大影响生产效率，因此采用 IDF 中的量产工具，该量产工具也是基于 NVS 分区生成工具的扩充。
`multipule_mfg_config.csv` 为参数区配置文件，该文件已完成对 `IoT Explorer` 的配置，你无需配置其他信息。

1. 将 `multipule_mfg_values.csv` 复制为 `my_multipule_mfg_values.csv` 

    ```shell
    cp multipule_mfg_values.csv my_multipule_mfg_values.csv
    ```

2. 对 `my_multipule_mfg_values.csv` 文件进行修改，填写希望用于量产的 `PRODUCT_ID_x`、`DEVICE_NAME_x`、 `DEVICE_SECRET_x` 信息。 

    每一行代表了一组设备信息，第一列为 `id` 信息，不会生成到对应的 NVS 分区中，仅用作标号。

    ```csv
    id,product_id,device_name,device_secret
    1,PRODUCT_ID_1,DEVICE_NAME_1,DEVICE_SECRET_1
    2,PRODUCT_ID_2,DEVICE_NAME_2,DEVICE_SECRET_2
    3,PRODUCT_ID_3,DEVICE_NAME_3,DEVICE_SECRET_3
    ```

> 注：tools 目录下的工具可快速完成 `my_multipule_mfg_values.csv` 文件的生成，建议使用该方式。

3. 批量生成 `NVS分区bin文件`。

    ```shell
    $IDF_PATH/tools/mass_mfg/mfg_gen.py generate multipule_mfg_config.csv my_multipule_mfg_values.csv qcloud 0x4000
    ```

    其中 `qcloud` 为生成的批量 bin 的前缀名称。执行完成后，会在当前目录下生成 `bin` 目录，里面保存了所有可用于量产的 NVS 分区 `bin`。
