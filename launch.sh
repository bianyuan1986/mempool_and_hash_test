#!/bin/bash

sudo ./build/test_mempool -l 4 -- 1000000
sudo ./build/test_mempool --proc-type secondary -l 5,6,7,8
