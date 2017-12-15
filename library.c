#define _GNU_SOURCE // for RTLD_NEXT

#include <stdio.h>
#include <hiredis/hiredis.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#define wrap(name) __##name = dlsym(RTLD_NEXT, ""#name"") // bind original function to variable
#define real(name) __##name                               // call original function

// local
static redisContext *__redis = NULL; // holder for Redis connection

// Exit codes
static const int envredis_exit_failed_init = 33;    // exit code when Redis connection can't be initialized
static const int envredis_exit_failed_connect = 34; // exit code when Redis can't establish connect
static const int envredis_exit_panic = 35;          // exit code when fail on Redis command (if PANIC enabled)

// Buffers
static const size_t envredis_buf_itoa = 32;         // buffer size to keep chars during integer to symbols conversation

// Default values
static const char envredis_default_ip[] = "127.0.0.1"; // default Redis IP if not provided by environment
static const int envredis_default_port = 6379;         // default Redis port if not provided by environment

// Environment variables names
static const char envredis_env_ip[] = "ENVREDIS_IP";         // Redis IP   (see 'defaults')
static const char envredis_env_port[] = "ENVREDIS_PORT";     // Redis port (see 'defaults')
static const char envredis_env_prefix[] = "ENVREDIS_PREFIX"; // Prefix for each key. Prefix will be removed after retrieve
static const char envredis_env_panic[] = "ENVREDIS_PANIC";   // Finish application if redis command failed



// holders for original functions - see man

static int (*__clearenv)();

static int (*__unsetenv)(const char *name);

static int (*__setenv)(const char *envname, const char *envval, int overwrite)= NULL;

static int (*__putenv)(char *string);

static char *(*__getenv)(const char *name);

static char *(*__secure_getenv)(const char *name);

/**
 * Get current prefix (from env) or empty string
 * @return non-NULL prefix
 */
const char *prefix() {
    const char *v = real(getenv)(envredis_env_prefix);
    return v == NULL ? "" : v;
}

/**
 * Check reply and panic/free depending on result and PANIC environment var
 * @param reply NULL or result of redis command
 */
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
    if (err && real(getenv)(envredis_env_panic) != NULL) {
        exit(envredis_exit_panic);
    }
}

/**
 * Try set environment variable from redis reply (also tries convert to string)
 * @param name original environment variable name (without prefix)
 * @param reply NULL or result of redis command
 */
void set_redis_val(const char *name, redisReply *reply) {
    if (reply != NULL) {
        switch (reply->type) {
            case REDIS_REPLY_STRING: {
                real(setenv)(name, reply->str, 1);
                break;
            }
            case REDIS_REPLY_INTEGER: {
                char tmp[envredis_buf_itoa];
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

// wrappers - see man

/**
 * Invokes DEL operation in redis
 */
int unsetenv(const char *name) {
    if (__redis != NULL) {
        redisReply *reply;
        reply = redisCommand(__redis, "DEL %s%s", prefix(), name);
        final_check_reply(reply);
    }
    return real(unsetenv)(name);
}

/**
 * Gets all KEYS (for prefix) and invokes DEL for each item
 */
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

/**
 * Get's value from Redis by GET command and sets current env. Return value from original function
 */
char *getenv(const char *name) {
    if (__redis != NULL) {
        redisReply *reply;
        reply = redisCommand(__redis, "GET %s%s", prefix(), name);
        set_redis_val(name, reply);
        final_check_reply(reply);
    }
    return real(getenv)(name);
}

/**
 * Set's value to Redis by SET command. If overwrite is not allowed - used NX suffix
 */
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

/**
 * Same as setenv but with overwrite by default
 */
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

/**
 * Get all keys in prefix and fill environment
 */
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


/**
 * Bind original functions and initialize connection
 */
int __attribute__((constructor)) __init__(void) {
    wrap(clearenv);
    wrap(unsetenv);
    wrap(setenv);
    wrap(putenv);
    wrap(getenv);
    wrap(secure_getenv);

    const char *ip = real(getenv)(envredis_env_ip);
    const char *s_port = real(getenv)(envredis_env_port);
    int port = envredis_default_port;

    if (ip == NULL) {
        ip = envredis_default_ip;
    }

    if (s_port != NULL) {
        port = atoi(s_port);
    }

    __redis = redisConnect(ip, port);
    if (__redis == NULL) {
        return envredis_exit_failed_init;
    }
    if (__redis->err) {
        printf("redis: %s\n", __redis->errstr);
        redisFree(__redis);
        __redis = NULL;
        return envredis_exit_failed_connect;
    }
    fill_env();
    return 0;
}

/**
 * Close redis state (if initialized)
 */
int __attribute__((destructor)) __destroy__(void) {
    if (__redis != NULL) {
        redisFree(__redis);
        __redis = NULL;
    }
    return 0;
}
