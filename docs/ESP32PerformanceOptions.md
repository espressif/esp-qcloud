# ESP32内存优化

### 相关信息
- 硬件环境： `ESP32-S2-WROOM`
- 硬件环境： `ESP32-WROOM-32`
- ESP-Qcloud commit： `f3f534b476432ce27e619956b3439bfb24447967`
- ESP-IDF release/v4.2 commit：`511965b269452e247a610f44c209020a7d1f05c2`

### 性能参数


- 根据 `WiFi 性能与内存参数配置手册` 的叙述，提供三组参数供用户选择。 

    - ESP32

        |  档位     | 高性能  | 默认  | 省内存 |
        |  ----     | ----   | ----  | ----   |
        | ESP32_WIFI_STATIC_RX_BUFFER_NUM  | 6 | 6 | 4 |
        | ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM  | 32 | 20 | 8 |
        | ESP32_WIFI_TX_BUFFER_TYPE  | 1 | 1 | 1 |
        | ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM  | 32 | 20 | 8 |
        | ESP32_WIFI_RX_BA_WIN  | 6 | 6 | disable |
        | LWIP_UDP_RECVMBOX_SIZ  | 32 | 20 | 8 |
        | LWIP_TCPIP_RECVMBOX_SIZE  | 32 | 20 | 8 |
        | LWIP_TCP_RECVMBOX_SIZE  | 32 | 20 | 8 |
        | LWIP_TCP_SND_BUF_DEFAULT  | 42K | 24K | 8K |
        | LWIP_TCP_WND_DEFAULT  | 42K | 24K | 8K |

    - ESP32S2
    
        |  档位     | 高性能  | 默认  | 省内存 | 
        |  ----     | ----   | ----  | ----   |
        | ESP32_WIFI_STATIC_RX_BUFFER_NUM  | 6 | 4 | 3 |
        | ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM  | 18 | 12 | 6 | 
        | ESP32_WIFI_TX_BUFFER_TYPE  | 1 | 1 | 1 |
        | ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM  | 18 | 12 | 6 |
        | ESP32_WIFI_RX_BA_WIN  | 9 | 6 | 3 |
        | LWIP_UDP_RECVMBOX_SIZ  | 18 | 12 | 6 |
        | LWIP_TCPIP_RECVMBOX_SIZE  | 18 | 12 | 6 |
        | LWIP_TCP_RECVMBOX_SIZE  |  18  | 12 | 6 |
        | LWIP_TCP_SND_BUF_DEFAULT  | 18K | 12K | 6K |
        | LWIP_TCP_WND_DEFAULT  | 18K | 12K | 6K |

 ### 性能对比

- ESP32

    - 可用内存对比

        |  档位  | 高性能  | 默认  | 省内存 | 
        |  ----     | ----   | ----  | ----   | 
        | System Initialised  | 279 | 279 | 279 |  
        | QCloud Initialised  | 224 | 225 | 227 |  
        | http OTA  | 177/161 | 193/178 | 212/205 | 
        | https OTA | 145/124 | 161/148 | 190/184 | 

    - OTA 速度对比

        |  档位  | 高性能  | 默认  | 省内存 |
        |  ----  | ----   | ----  | ----   |
        |  OTA   |  2.5s    |   4.2s  |   15s   | 

- ESP32-S2

    - 可用内存对比

        |  档位  | 高性能  | 默认  | 省内存 |
        |  ----     | ----   | ----  | ----   |
        | System Initialised | 156 | 156 | 156 |
        | QCloud Initialised | 101 | 104 | 106 |
        | http OTA           | 74/62| 89/75 | 92/84 |
        | https OTA          | 52/47 | 65/48 | 71/61 |

    - OTA 速度对比

        |  档位  | 高性能  | 默认  | 省内存 | 
        |  ----  | ----   | ----  | ----   | 
        |  OTA   | 4s | 7s   | 28s  | 
 
