# EnvRedis

![envredis](https://user-images.githubusercontent.com/6597086/97159849-6c190d80-17b6-11eb-9831-70eebb649322.png)

Wrap syscall for get/set/clean and e.t.c environment and map values to/from Redis


# Requirements

- libhiredis (tested on 0.13)
- C11

# Environment variable

* **ENVREDIS_IP** - ip of redis server. Default: `127.0.0.1`
* **ENVREDIS_PORT** - port of redis server. Default: `6379`
* **ENVREDIS_PREFIX** - prefix for each keys. Default: nothing
* **ENVREDIS_PANIC** - fail if redis operations failed. Disabled by default

# Sample usage

## Local

    export LD_PRELOAD=/path/to/libenvredis.so

    python -c 'import os; os.environ["MY_SAMPLE_VAR"]="111"'
    python -c 'import os; print os.environ["MY_SAMPLE_VAR"]'

## Remote

    export LD_PRELOAD=/path/to/libenvredis.so
    export ENVREDIS_IP='xx.yy.zz.cc'

    python -c 'import os; os.environ["MY_SAMPLE_VAR"]="111"'
    python -c 'import os; print os.environ["MY_SAMPLE_VAR"]'
