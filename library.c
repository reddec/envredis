#define _GNU_SOURCE

#include <stdio.h>
#include <hiredis/hiredis.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#define wrap(name) __##name = dlsym(RTLD_NEXT, ""#name"")
#define real(name) __##name

enum envredis_death_code {
    death = 33
};

// local utils

void final_check_reply(redisReply *reply);

void set_redis_val(const char *name, redisReply *reply);

const char *prefix();

static redisContext *__redis = NULL;

// original functions

static int (*__clearenv)();

static int (*__unsetenv)(const char *name);

static int (*__setenv)(const char *envname, const char *envval, int overwrite)= NULL;

static int (*__putenv)(char *string);

static char *(*__getenv)(const char *name);

static char *(*__secure_getenv)(const char *name);

// wrappers

int unsetenv(const char *name) {
    if (__redis != NULL) {
        redisReply *reply;
        reply = redisCommand(__redis, "DEL %s%s", prefix(), name);
        final_check_reply(reply);
    }
    return real(unsetenv)(name);
}

int clearenv() {
    if (__redis != NULL) {
        redisReply *reply = redisCommand(__redis, "KEYS %s*", prefix());
        if (reply != NULL && reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = 0; i < reply->elements; ++i) {
                redisReply *item = reply->element[i];
                unsetenv(item->str); // call wrap
            }
        }
        final_check_reply(reply);
    }
    return real(clearenv)();
}

char *getenv(const char *name) {
    if (__redis != NULL) {
        redisReply *reply;
        reply = redisCommand(__redis, "GET %s%s", prefix(), name);
        set_redis_val(name, reply);
        final_check_reply(reply);
    }
    return real(getenv)(name);
}

int setenv(const char *envname, const char *envval, int overwrite) {
    if (__redis != NULL) {
        redisReply *reply;
        if (overwrite) {
            reply = redisCommand(__redis, "SET %s%s %s", prefix(), envname, envval);
        } else {
            reply = redisCommand(__redis, "SET %s%s %s NX", prefix(), envname, envval);
        }
        final_check_reply(reply);
    }
    return real(setenv)(envname, envval, overwrite);
}

int putenv(char *string) {
    char *pos = strchr(string, '=');
    char *val = "";
    if (pos != NULL) {
        *pos = '\0';
        val = pos + 1;
    }
    int res = setenv(string, val, 1);
    if (pos != NULL) {
        *pos = '=';
    }
    return res;
}

// utils

void fill_env() {
    if (__redis == NULL) {
        return;
    }
    redisReply *reply = redisCommand(__redis, "KEYS %s*", prefix());

    if (reply != NULL && reply->type == REDIS_REPLY_ARRAY) {
        size_t offset = strlen(prefix());
        for (size_t i = 0; i < reply->elements; ++i) {
            redisReply *item = reply->element[i];
            getenv((item->str) + offset); // call wrap
        }
    }
    final_check_reply(reply);
}

void set_redis_val(const char *name, redisReply *reply) {
    if (reply != NULL) {
        switch (reply->type) {
            case REDIS_REPLY_STRING: {
                real(setenv)(name, reply->str, 1);
                break;
            }
            case REDIS_REPLY_INTEGER: {
                char tmp[32];
                sprintf(tmp, "%lli", reply->integer);
                real(setenv)(name, tmp, 1);
                break;
            }
            case REDIS_REPLY_NIL: {
                real(unsetenv)(name);
                break;
            }
            default: {
                fprintf(stderr, "reids: unsupported type: %i for key %s%s\n", reply->type, prefix(), name);
                break;
            }
        }
    }
}

void final_check_reply(redisReply *reply) {
    int err = 0;
    if (reply == NULL) {
        fprintf(stderr, "REDIS: failed get reply\n");
        err = 1;
    } else if (reply->type == REDIS_REPLY_ERROR) {
        fprintf(stderr, "REDIS: failed execute command: %s\n", reply->str);
        err = 1;
    }
    freeReplyObject(reply);
    if (err && real(getenv)("ENVREDIS_PANIC") != NULL) {
        exit(death);
    }
}

const char *prefix() {
    const char *v = real(getenv)("ENVREDIS_PREFIX");
    return v == NULL ? "" : v;
}

int __attribute__((constructor)) __init__(void) {
    wrap(clearenv);
    wrap(unsetenv);
    wrap(setenv);
    wrap(putenv);
    wrap(getenv);
    wrap(secure_getenv);

    const char *ip = real(getenv)("ENVREDIS_IP");
    const char *s_port = real(getenv)("ENVREDIS_PORT");
    int port = 6379;

    if (ip == NULL) {
        ip = "127.0.0.1";
    }

    if (s_port != NULL) {
        port = atoi(s_port);
    }

    __redis = redisConnect(ip, port);
    if (__redis == NULL) {
        return death;
    }
    if (__redis->err) {
        printf("redis: %s\n", __redis->errstr);
        redisFree(__redis);
        __redis = NULL;
        return death;
    }
    fill_env();
    return 0;
}

int __attribute__((destructor)) __destroy__(void) {
    if (__redis != NULL) {
        redisFree(__redis);
        __redis = NULL;
    }
    return 0;
}
