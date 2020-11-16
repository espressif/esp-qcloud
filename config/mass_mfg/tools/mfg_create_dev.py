# Copyright 2020 Espressif Systems (Shanghai) PTE LTD
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import json
import argparse
from tencentcloud.common import credential
from tencentcloud.common.profile.client_profile import ClientProfile
from tencentcloud.common.profile.http_profile import HttpProfile
from tencentcloud.common.exception.tencent_cloud_sdk_exception import TencentCloudSDKException
from tencentcloud.iotexplorer.v20190423 import iotexplorer_client, models

def main():
    parser = argparse.ArgumentParser(description='argparse')
    parser.add_argument('--SecretId', '-id', required =True, type=str, dest='SecretId', help="Tencent Cloud ID used to call the API interface")
    parser.add_argument('--SecretKey', '-key', required =True, type=str, dest='SecretKey', help="Tencent Cloud key used to call the API interface")
    parser.add_argument('--ProductId', '-pid', required =True, type=str, dest='ProductId', help="Product ID of the device to be created")
    parser.add_argument('--ProductNum', '-num', required =True, type=int, dest='ProductNum', help="The number of devices to be created")
    args = parser.parse_args()

    try:
        cred = credential.Credential(args.SecretId, args.SecretKey) 
        httpProfile = HttpProfile()
        httpProfile.endpoint = "iotexplorer.tencentcloudapi.com"

        clientProfile = ClientProfile()
        clientProfile.httpProfile = httpProfile
        client = iotexplorer_client.IotexplorerClient(cred, "ap-shanghai", clientProfile) 
        i = 1
        while i < args.ProductNum + 1:
            req = models.CreateDeviceRequest()
            params = {
                "ProductId":args.ProductId,
                "DeviceName":"test_"+ str(i)
            }
            req.from_json_string(json.dumps(params))
            i += 1
            resp = client.CreateDevice(req) 
            print(resp.to_json_string())

    except TencentCloudSDKException as err: 
        print(err) 

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        quit()
