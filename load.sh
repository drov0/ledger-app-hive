#!/bin/bash

python -m ledgerblue.loadApp --appFlags 0x240 --path "48'/13'" --curve secp256k1 --tlv --targetId 0x31100003 --delete --fileName bin/app.hex --appName Hive --appVersion 1.3.0 --dataSize `cat debug/app.map |grep _nvram_data_size | tr -s ' ' | cut -f2 -d' '` --icon 0100000000ffffff003ff367e667e6c3ccc3cc81998199003f003f81998199c3ccc3cc67e667e63ff3
