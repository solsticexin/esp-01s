// 原理说明：该实现文件给出 Wi-Fi 等静态配置字符串，避免将敏感信息散落在业务代码中。
#include "device_config.h"

namespace device_config {

// Update these credentials to match the Wi-Fi network you want the device to join.
const char WIFI_SSID[] = "Redmi Note 13 Pro";
const char WIFI_PASSWORD[] = "11111111";

}  // namespace device_config
