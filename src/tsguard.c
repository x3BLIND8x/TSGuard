#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
#define strtok_r strtok_s
#else
#include <pthread.h>
#include <stdatomic.h>
#include <strings.h>
#endif

#include "teamspeak/public_definitions.h"
#include "teamspeak/public_rare_definitions.h"
#include "ts3_functions.h"

#if defined(_WIN32)
#define TSG_EXPORT __declspec(dllexport)
#else
#define TSG_EXPORT __attribute__((visibility("default")))
#endif

#define TSG_PLUGIN_API_VERSION 26
#ifndef TSG_VERSION
#define TSG_VERSION "dev"
#endif
#define TSG_MAX_SERVERS 32
#define TSG_MSG_MAX 1400
#define TSG_COMMAND_MAX 512
#define TSG_CONNECT_MAX 512
#define TSG_NICK_MAX 128
#define TSG_GUARD_WINDOW_MS 5000ULL
#define TSG_GUARD_MAX_ACTIONS 3
#define TSG_RECONNECT_GUARD_WINDOW_MS 30000ULL
#define TSG_RECONNECT_GUARD_MAX_ACTIONS 3
#define TSG_ECHO_LOOP_WINDOW_MS 5000ULL
#define TSG_AUTO_AWAY_SECONDS 300ULL
#define TSG_WORKER_SLEEP_MS 250ULL

#define TSG_ON  "[color=#00d26a][b]ON[/b][/color]"
#define TSG_OFF "[color=#ff5c5c][b]OFF[/b][/color]"

enum {
    TSG_MENU_FOLLOW_CLIENT = 1,

    TSG_MENU_TOGGLE_ANTIMOVE = 10,
    TSG_MENU_TOGGLE_SERVER_KICK = 11,
    TSG_MENU_TOGGLE_TEMP_BAN = 12,
    TSG_MENU_TOGGLE_ECHO_BACK = 13,
    TSG_MENU_TOGGLE_AUTO_AWAY = 14,
    TSG_MENU_TOGGLE_GROUP_GUARD = 15,
    TSG_MENU_RETURN_LAST = 16,

    TSG_MENU_STATUS_HEADER = 100,
    TSG_MENU_STATUS_ANTIMOVE = 101,
    TSG_MENU_STATUS_SERVER_KICK = 102,
    TSG_MENU_STATUS_TEMP_BAN = 103,
    TSG_MENU_STATUS_ECHO = 104,
    TSG_MENU_STATUS_AUTO_AWAY = 105,
    TSG_MENU_STATUS_GROUP_GUARD = 106,
};

enum {
    TSG_RECONNECT_NONE = 0,
    TSG_RECONNECT_SERVER_KICK = 1,
    TSG_RECONNECT_TEMP_BAN = 2,
};

typedef struct {
    int used;
    uint64 schid;

    int anti_move;
    int anti_server_kick;
    int anti_temp_ban;
    int echo_back;
    int auto_away;
    int anti_group_removal;
    int logging;

    anyID follow_client;
    anyID self_id;
    uint64 self_dbid;
    uint64 last_channel;

    uint64 default_channel_group;
    uint64 tracked_channel_group;
    uint64 tracked_channel_for_group;

    unsigned int suppress_self_move_events;
    uint64_t guard_window_start_ms;
    unsigned int guard_actions;
    uint64_t group_guard_window_start_ms;
    unsigned int group_guard_actions;
    uint64_t reconnect_guard_window_start_ms;
    unsigned int reconnect_guard_actions;

    anyID last_text_echo_peer;
    uint64_t last_text_echo_hash;
    uint64_t last_text_echo_ms;
    anyID last_poke_echo_peer;
    uint64_t last_poke_echo_hash;
    uint64_t last_poke_echo_ms;

    int auto_away_owned;

    char host[TSG_CONNECT_MAX];
    unsigned short port;
    char server_password[TSG_CONNECT_MAX];
    char nickname[TSG_NICK_MAX];
    char channel_path[TSG_CONNECT_MAX];
    char channel_password[TSG_CONNECT_MAX];

    int reconnect_pending;
    int reconnect_kind;
    uint64_t reconnect_at_ms;
    uint64 reconnect_ban_seconds;
} TSGServerState;

typedef struct {
    int valid;
    uint64 old_schid;
    int kind;
    char address[TSG_CONNECT_MAX + 32];
    char server_password[TSG_CONNECT_MAX];
    char nickname[TSG_NICK_MAX];
    char channel_path[TSG_CONNECT_MAX];
    char channel_password[TSG_CONNECT_MAX];
} TSGReconnectTask;

static struct TS3Functions ts3Functions;
static TSGServerState states[TSG_MAX_SERVERS];
static char* pluginID = NULL;
#ifdef _WIN32
static HANDLE worker_thread = NULL;
static volatile LONG worker_stop = 0;
static SRWLOCK state_mutex = SRWLOCK_INIT;
#else
static pthread_t worker_thread;
static atomic_int worker_stop;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
static int worker_started = 0;

static void state_lock(void)
{
#ifdef _WIN32
    AcquireSRWLockExclusive(&state_mutex);
#else
    pthread_mutex_lock(&state_mutex);
#endif
}

static void state_unlock(void)
{
#ifdef _WIN32
    ReleaseSRWLockExclusive(&state_mutex);
#else
    pthread_mutex_unlock(&state_mutex);
#endif
}

static void set_worker_stop(int value)
{
#ifdef _WIN32
    InterlockedExchange(&worker_stop, value ? 1L : 0L);
#else
    atomic_store(&worker_stop, value ? 1 : 0);
#endif
}

static int is_worker_stopped(void)
{
#ifdef _WIN32
    return InterlockedCompareExchange(&worker_stop, 0L, 0L) != 0;
#else
    return atomic_load(&worker_stop) != 0;
#endif
}

static uint64_t now_ms(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return (uint64_t)time(NULL) * 1000ULL;
    }
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

static void sleep_ms(uint64_t ms)
{
#ifdef _WIN32
    Sleep((DWORD)(ms > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : ms));
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000ULL);
    ts.tv_nsec = (long)((ms % 1000ULL) * 1000000ULL);
    nanosleep(&ts, NULL);
#endif
}

static uint64_t hash_text(const char* s)
{
    uint64_t h = 1469598103934665603ULL;
    const unsigned char* p = (const unsigned char*)(s ? s : "");
    while (*p) {
        h ^= (uint64_t)*p++;
        h *= 1099511628211ULL;
    }
    return h;
}

static TSGServerState* state_for(uint64 schid)
{
    size_t i;
    TSGServerState* found = NULL;
    TSGServerState* free_slot = NULL;

    state_lock();
    for (i = 0; i < TSG_MAX_SERVERS; ++i) {
        if (states[i].used && states[i].schid == schid) {
            found = &states[i];
            break;
        }
        if (!states[i].used && !free_slot) {
            free_slot = &states[i];
        }
    }

    if (!found && free_slot) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->used = 1;
        free_slot->schid = schid;
        free_slot->anti_move = 1;
        free_slot->anti_server_kick = 0;
        free_slot->anti_temp_ban = 0;
        free_slot->echo_back = 0;
        free_slot->auto_away = 0;
        free_slot->anti_group_removal = 0;
        free_slot->logging = 1;
        found = free_slot;
    }
    state_unlock();
    return found;
}

static TSGServerState* find_state_unlocked(uint64 schid)
{
    size_t i;
    for (i = 0; i < TSG_MAX_SERVERS; ++i) {
        if (states[i].used && states[i].schid == schid) {
            return &states[i];
        }
    }
    return NULL;
}

static void tsg_log(uint64 schid, enum LogLevel level, const char* fmt, ...)
{
    char body[TSG_MSG_MAX];
    char msg[TSG_MSG_MAX + 32];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    snprintf(msg, sizeof(msg), "[TSGuard] %s", body);
    if (ts3Functions.logMessage) {
        ts3Functions.logMessage(msg, level, "TSGuard", schid);
    }
}

static void tsg_print(uint64 schid, const char* fmt, ...)
{
    char body[TSG_MSG_MAX];
    char msg[TSG_MSG_MAX + 64];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    snprintf(msg, sizeof(msg), "[color=#ffffff][b][TSGuard][/b][/color] %s", body);
    if (ts3Functions.printMessage) {
        ts3Functions.printMessage(schid, msg, PLUGIN_MESSAGE_TARGET_SERVER);
    } else if (ts3Functions.printMessageToCurrentTab) {
        ts3Functions.printMessageToCurrentTab(msg);
    }
}

static int get_self_id(uint64 schid, anyID* out)
{
    TSGServerState* st = state_for(schid);
    anyID id = 0;

    if (!out) {
        return 0;
    }
    if (ts3Functions.getClientID && ts3Functions.getClientID(schid, &id) == 0) {
        *out = id;
        if (st) {
            st->self_id = id;
        }
        return 1;
    }
    if (st && st->self_id) {
        *out = st->self_id;
        return 1;
    }
    return 0;
}

static uint64 get_self_channel(uint64 schid)
{
    anyID me;
    uint64 channel = 0;
    if (!get_self_id(schid, &me) || !ts3Functions.getChannelOfClient) {
        return 0;
    }
    if (ts3Functions.getChannelOfClient(schid, me, &channel) != 0) {
        return 0;
    }
    return channel;
}

static uint64 get_self_dbid(uint64 schid)
{
    TSGServerState* st = state_for(schid);
    anyID me;
    uint64 dbid = 0;

    if (!st || !get_self_id(schid, &me)) {
        return 0;
    }
    if (ts3Functions.getClientVariableAsUInt64 &&
        ts3Functions.getClientVariableAsUInt64(schid, me, CLIENT_DATABASE_ID, &dbid) == 0 && dbid) {
        st->self_dbid = dbid;
        return dbid;
    }
    return st->self_dbid;
}

static int guard_allow_window(uint64_t* window_start, unsigned int* actions,
                              uint64_t window_ms, unsigned int max_actions)
{
    uint64_t now = now_ms();
    if (!*window_start || now - *window_start > window_ms) {
        *window_start = now;
        *actions = 0;
    }
    if (*actions >= max_actions) {
        return 0;
    }
    (*actions)++;
    return 1;
}

static int move_guard_allow(TSGServerState* st)
{
    if (!st) return 0;
    return guard_allow_window(&st->guard_window_start_ms, &st->guard_actions,
                              TSG_GUARD_WINDOW_MS, TSG_GUARD_MAX_ACTIONS);
}

static int group_guard_allow(TSGServerState* st)
{
    if (!st) return 0;
    return guard_allow_window(&st->group_guard_window_start_ms, &st->group_guard_actions,
                              TSG_GUARD_WINDOW_MS, TSG_GUARD_MAX_ACTIONS);
}

static int reconnect_guard_allow(TSGServerState* st)
{
    if (!st) return 0;
    return guard_allow_window(&st->reconnect_guard_window_start_ms, &st->reconnect_guard_actions,
                              TSG_RECONNECT_GUARD_WINDOW_MS, TSG_RECONNECT_GUARD_MAX_ACTIONS);
}

static int move_self(uint64 schid, uint64 channel, const char* password, const char* why)
{
    anyID me;
    TSGServerState* st = state_for(schid);
    unsigned int err;

    if (!st || !channel || !get_self_id(schid, &me) || !ts3Functions.requestClientMove) {
        return 0;
    }

    if (!move_guard_allow(st)) {
        tsg_print(schid, "%s stopped: safety limiter hit (%u actions / %llu ms).", why,
                 TSG_GUARD_MAX_ACTIONS, (unsigned long long)TSG_GUARD_WINDOW_MS);
        return 0;
    }

    st->suppress_self_move_events++;
    err = ts3Functions.requestClientMove(schid, me, channel, password ? password : "", NULL);
    if (err != 0) {
        if (st->suppress_self_move_events > 0) {
            st->suppress_self_move_events--;
        }
        tsg_log(schid, LogLevel_WARNING, "%s request failed immediately (error %u)", why, err);
        tsg_print(schid, "%s request failed (error %u).", why, err);
        return 0;
    }

    tsg_log(schid, LogLevel_INFO, "%s -> channel %llu", why, (unsigned long long)channel);
    return 1;
}

static void follow_move(uint64 schid, anyID clientID, uint64 newChannelID)
{
    TSGServerState* st = state_for(schid);
    uint64 my_channel;

    if (!st || !st->follow_client || clientID != st->follow_client || !newChannelID) {
        return;
    }

    my_channel = get_self_channel(schid);
    if (my_channel == newChannelID) {
        return;
    }

    move_self(schid, newChannelID, "", "AutoFollow");
}

static int parse_toggle(const char* value, int current, int* ok)
{
    *ok = 1;
    if (!value || !*value || !strcasecmp(value, "toggle")) {
        return !current;
    }
    if (!strcasecmp(value, "on") || !strcasecmp(value, "1") || !strcasecmp(value, "true")) {
        return 1;
    }
    if (!strcasecmp(value, "off") || !strcasecmp(value, "0") || !strcasecmp(value, "false")) {
        return 0;
    }
    *ok = 0;
    return current;
}

static const char* onoff(int value)
{
    return value ? TSG_ON : TSG_OFF;
}

static void print_status(uint64 schid)
{
    TSGServerState* st = state_for(schid);
    if (!st) {
        tsg_print(schid, "No free state slot for this server tab.");
        return;
    }

    tsg_print(schid,
             "[color=#00d26a][b]STATUS[/b]  AntiMove/AntiKick=%s | ServerKick=%s | TempBan=%s | EchoBack=%s | AutoAway=%s | GroupGuard=%s | Follow=%u[/color]",
             onoff(st->anti_move), onoff(st->anti_server_kick), onoff(st->anti_temp_ban),
             onoff(st->echo_back), onoff(st->auto_away), onoff(st->anti_group_removal),
             (unsigned int)st->follow_client);
}

static void print_help(uint64 schid)
{
    tsg_print(schid,
             "Commands: /tsg status | /tsg antimove [on|off|toggle] | /tsg serverkick [...] | /tsg tempban [...] | "
             "/tsg echo [...] | /tsg autoaway [...] | /tsg groups [...] | /tsg logging [...] | /tsg back [channelPassword] | "
             "/tsg follow <clientID|off> | /tsg help");
}

static void set_status_item(int id, int value)
{
    if (!pluginID || !ts3Functions.setPluginMenuEnabled) {
        return;
    }
    /* API 26 cannot hide or rename menu items at runtime.
     * One row per feature is therefore used: bright/enabled = ON, grey/disabled = OFF. */
    ts3Functions.setPluginMenuEnabled(pluginID, id, value ? 1 : 0);
}

static void refresh_menu_status(uint64 schid)
{
    TSGServerState* st = state_for(schid);
    if (!st || !pluginID || !ts3Functions.setPluginMenuEnabled) {
        return;
    }

    ts3Functions.setPluginMenuEnabled(pluginID, TSG_MENU_STATUS_HEADER, 0);
    set_status_item(TSG_MENU_STATUS_ANTIMOVE, st->anti_move);
    set_status_item(TSG_MENU_STATUS_SERVER_KICK, st->anti_server_kick);
    set_status_item(TSG_MENU_STATUS_TEMP_BAN, st->anti_temp_ban);
    set_status_item(TSG_MENU_STATUS_ECHO, st->echo_back);
    set_status_item(TSG_MENU_STATUS_AUTO_AWAY, st->auto_away);
    set_status_item(TSG_MENU_STATUS_GROUP_GUARD, st->anti_group_removal);
}

static void capture_group_state(uint64 schid, TSGServerState* st)
{
    anyID me;
    uint64 value = 0;
    uint64 channel = 0;

    if (!st || !get_self_id(schid, &me)) {
        return;
    }

    if (ts3Functions.getClientVariableAsUInt64 &&
        ts3Functions.getClientVariableAsUInt64(schid, me, CLIENT_DATABASE_ID, &value) == 0) {
        st->self_dbid = value;
    }

    value = 0;
    if (ts3Functions.getClientVariableAsUInt64 &&
        ts3Functions.getClientVariableAsUInt64(schid, me, CLIENT_CHANNEL_GROUP_ID, &value) == 0) {
        channel = get_self_channel(schid);
        if (channel) {
            st->tracked_channel_group = value;
            st->tracked_channel_for_group = channel;
        }
    }

    value = 0;
    if (ts3Functions.getServerVariableAsUInt64 &&
        ts3Functions.getServerVariableAsUInt64(schid, VIRTUALSERVER_DEFAULT_CHANNEL_GROUP, &value) == 0) {
        st->default_channel_group = value;
    }
}

static void capture_connection_snapshot(uint64 schid, TSGServerState* st)
{
    char* nick = NULL;
    char host[TSG_CONNECT_MAX] = {0};
    char server_pw[TSG_CONNECT_MAX] = {0};
    unsigned short port = 0;
    uint64 channel;

    if (!st) {
        return;
    }

    if (ts3Functions.getServerConnectInfo &&
        ts3Functions.getServerConnectInfo(schid, host, &port, server_pw, sizeof(host)) == 0) {
        snprintf(st->host, sizeof(st->host), "%s", host);
        snprintf(st->server_password, sizeof(st->server_password), "%s", server_pw);
        st->port = port;
    }

    if (ts3Functions.getClientSelfVariableAsString &&
        ts3Functions.getClientSelfVariableAsString(schid, CLIENT_NICKNAME, &nick) == 0 && nick) {
        snprintf(st->nickname, sizeof(st->nickname), "%s", nick);
        if (ts3Functions.freeMemory) {
            ts3Functions.freeMemory(nick);
        }
    }

    channel = get_self_channel(schid);
    if (channel && ts3Functions.getChannelConnectInfo) {
        char path[TSG_CONNECT_MAX] = {0};
        char channel_pw[TSG_CONNECT_MAX] = {0};
        if (ts3Functions.getChannelConnectInfo(schid, channel, path, channel_pw, sizeof(path)) == 0) {
            snprintf(st->channel_path, sizeof(st->channel_path), "%s", path);
            snprintf(st->channel_password, sizeof(st->channel_password), "%s", channel_pw);
        }
    }

    capture_group_state(schid, st);
}

static void make_server_address(const TSGServerState* st, char* out, size_t out_sz)
{
    if (!st || !out || out_sz == 0) {
        return;
    }
    out[0] = '\0';
    if (!st->host[0]) {
        return;
    }

    if (strchr(st->host, ':') && st->host[0] != '[') {
        snprintf(out, out_sz, "[%s]:%u", st->host, (unsigned int)st->port);
    } else if (st->port) {
        snprintf(out, out_sz, "%s:%u", st->host, (unsigned int)st->port);
    } else {
        snprintf(out, out_sz, "%s", st->host);
    }
}

static void schedule_reconnect(uint64 schid, TSGServerState* st, int kind, uint64 delay_seconds, uint64 ban_seconds)
{
    if (!st) {
        return;
    }

    capture_connection_snapshot(schid, st);
    if (!st->host[0]) {
        tsg_log(schid, LogLevel_WARNING, "Reconnect not scheduled: server connection info unavailable.");
        return;
    }

    if (!reconnect_guard_allow(st)) {
        tsg_print(schid, "Auto reconnect stopped: safety limiter hit (%u reconnects / %llu sec).",
                 TSG_RECONNECT_GUARD_MAX_ACTIONS,
                 (unsigned long long)(TSG_RECONNECT_GUARD_WINDOW_MS / 1000ULL));
        return;
    }

    state_lock();
    st->reconnect_pending = 1;
    st->reconnect_kind = kind;
    st->reconnect_at_ms = now_ms() + delay_seconds * 1000ULL;
    st->reconnect_ban_seconds = ban_seconds;
    state_unlock();

    if (kind == TSG_RECONNECT_TEMP_BAN) {
        tsg_log(schid, LogLevel_INFO, "Temporary ban: reconnect scheduled in %llu sec.",
               (unsigned long long)delay_seconds);
    } else {
        tsg_log(schid, LogLevel_INFO, "Server kick: reconnect scheduled in %llu sec.",
               (unsigned long long)delay_seconds);
    }
}

static void inherit_state(uint64 old_schid, uint64 new_schid)
{
    TSGServerState* oldst;
    TSGServerState* newst = state_for(new_schid);
    if (!newst) {
        return;
    }

    state_lock();
    oldst = find_state_unlocked(old_schid);
    if (oldst && oldst != newst) {
        newst->anti_move = oldst->anti_move;
        newst->anti_server_kick = oldst->anti_server_kick;
        newst->anti_temp_ban = oldst->anti_temp_ban;
        newst->echo_back = oldst->echo_back;
        newst->auto_away = oldst->auto_away;
        newst->anti_group_removal = oldst->anti_group_removal;
        newst->logging = oldst->logging;
        newst->last_channel = oldst->last_channel;
        snprintf(newst->host, sizeof(newst->host), "%s", oldst->host);
        newst->port = oldst->port;
        snprintf(newst->server_password, sizeof(newst->server_password), "%s", oldst->server_password);
        snprintf(newst->nickname, sizeof(newst->nickname), "%s", oldst->nickname);
        snprintf(newst->channel_path, sizeof(newst->channel_path), "%s", oldst->channel_path);
        snprintf(newst->channel_password, sizeof(newst->channel_password), "%s", oldst->channel_password);
    }
    state_unlock();
}

static int should_suppress_echo(TSGServerState* st, anyID peer, uint64_t hash, int is_poke)
{
    uint64_t now = now_ms();
    if (!st) return 1;

    if (is_poke) {
        if (st->last_poke_echo_peer == peer && st->last_poke_echo_hash == hash &&
            now - st->last_poke_echo_ms <= TSG_ECHO_LOOP_WINDOW_MS) {
            return 1;
        }
    } else {
        if (st->last_text_echo_peer == peer && st->last_text_echo_hash == hash &&
            now - st->last_text_echo_ms <= TSG_ECHO_LOOP_WINDOW_MS) {
            return 1;
        }
    }
    return 0;
}

static void remember_echo(TSGServerState* st, anyID peer, uint64_t hash, int is_poke)
{
    uint64_t now = now_ms();
    if (!st) return;
    if (is_poke) {
        st->last_poke_echo_peer = peer;
        st->last_poke_echo_hash = hash;
        st->last_poke_echo_ms = now;
    } else {
        st->last_text_echo_peer = peer;
        st->last_text_echo_hash = hash;
        st->last_text_echo_ms = now;
    }
}

static void set_auto_away_state(uint64 schid, int away)
{
    if (!ts3Functions.setClientSelfVariableAsInt || !ts3Functions.flushClientSelfUpdates) {
        return;
    }

    ts3Functions.setClientSelfVariableAsInt(schid, CLIENT_AWAY, away ? AWAY_ZZZ : AWAY_NONE);
    if (ts3Functions.setClientSelfVariableAsString) {
        ts3Functions.setClientSelfVariableAsString(schid, CLIENT_AWAY_MESSAGE,
                                                    away ? "Auto Away - inactive for 5 minutes" : "");
    }
    ts3Functions.flushClientSelfUpdates(schid, NULL);
}

static void worker_auto_away(uint64 schid)
{
    TSGServerState* st;
    int enabled;
    int owned;
    anyID me;
    uint64 idle = 0;
    int current_away = AWAY_NONE;
    int conn = STATUS_DISCONNECTED;

    state_lock();
    st = find_state_unlocked(schid);
    if (!st) {
        state_unlock();
        return;
    }
    enabled = st->auto_away;
    owned = st->auto_away_owned;
    state_unlock();

    if (!enabled) {
        if (owned) {
            set_auto_away_state(schid, 0);
            state_lock();
            st = find_state_unlocked(schid);
            if (st) st->auto_away_owned = 0;
            state_unlock();
        }
        return;
    }

    if (!ts3Functions.getConnectionStatus || ts3Functions.getConnectionStatus(schid, &conn) != 0 ||
        conn != STATUS_CONNECTION_ESTABLISHED) {
        return;
    }
    if (!get_self_id(schid, &me) || !ts3Functions.getClientVariableAsUInt64 ||
        ts3Functions.getClientVariableAsUInt64(schid, me, CLIENT_IDLE_TIME, &idle) != 0) {
        return;
    }
    if (ts3Functions.getClientSelfVariableAsInt) {
        ts3Functions.getClientSelfVariableAsInt(schid, CLIENT_AWAY, &current_away);
    }

    if (idle >= TSG_AUTO_AWAY_SECONDS && current_away == AWAY_NONE) {
        set_auto_away_state(schid, 1);
        state_lock();
        st = find_state_unlocked(schid);
        if (st) st->auto_away_owned = 1;
        state_unlock();
    } else if (idle < TSG_AUTO_AWAY_SECONDS && owned && current_away != AWAY_NONE) {
        set_auto_away_state(schid, 0);
        state_lock();
        st = find_state_unlocked(schid);
        if (st) st->auto_away_owned = 0;
        state_unlock();
    } else if (owned && current_away == AWAY_NONE) {
        state_lock();
        st = find_state_unlocked(schid);
        if (st) st->auto_away_owned = 0;
        state_unlock();
    }
}

static TSGReconnectTask pop_reconnect_task(uint64_t now)
{
    TSGReconnectTask task;
    size_t i;
    memset(&task, 0, sizeof(task));

    state_lock();
    for (i = 0; i < TSG_MAX_SERVERS; ++i) {
        TSGServerState* st = &states[i];
        if (!st->used || !st->reconnect_pending || now < st->reconnect_at_ms) {
            continue;
        }

        st->reconnect_pending = 0;
        task.valid = 1;
        task.old_schid = st->schid;
        task.kind = st->reconnect_kind;
        make_server_address(st, task.address, sizeof(task.address));
        snprintf(task.server_password, sizeof(task.server_password), "%s", st->server_password);
        snprintf(task.nickname, sizeof(task.nickname), "%s", st->nickname);
        snprintf(task.channel_path, sizeof(task.channel_path), "%s", st->channel_path);
        snprintf(task.channel_password, sizeof(task.channel_password), "%s", st->channel_password);
        break;
    }
    state_unlock();
    return task;
}

static void run_reconnect_task(const TSGReconnectTask* task)
{
    uint64 new_schid = 0;
    unsigned int err;

    if (!task || !task->valid || !task->address[0] || !ts3Functions.guiConnect) {
        return;
    }

    err = ts3Functions.guiConnect(
        PLUGIN_CONNECT_TAB_NEW_IF_CURRENT_CONNECTED,
        "TSGuard reconnect",
        task->address,
        task->server_password,
        task->nickname,
        task->channel_path,
        task->channel_password,
        "", "", "", "", "", "", "",
        &new_schid);

    if (err != 0) {
        tsg_log(task->old_schid, LogLevel_WARNING, "Auto reconnect failed (error %u).", err);
        if (ts3Functions.printMessageToCurrentTab) {
            ts3Functions.printMessageToCurrentTab("[color=#ffffff][b][TSGuard][/b][/color] Auto reconnect failed. Use your normal bookmark/connect action if this server uses a non-default identity/profile.");
        }
        return;
    }

    if (new_schid) {
        inherit_state(task->old_schid, new_schid);
    }
    tsg_log(new_schid ? new_schid : task->old_schid, LogLevel_INFO,
           "Auto reconnect requested after %s.",
           task->kind == TSG_RECONNECT_TEMP_BAN ? "temporary ban expiry" : "server kick");
}

#ifdef _WIN32
static DWORD WINAPI worker_main(LPVOID unused)
#else
static void* worker_main(void* unused)
#endif
{
    (void)unused;
    while (!is_worker_stopped()) {
        uint64 schids[TSG_MAX_SERVERS];
        size_t count = 0;
        size_t i;
        TSGReconnectTask task;

        state_lock();
        for (i = 0; i < TSG_MAX_SERVERS; ++i) {
            if (states[i].used && states[i].schid) {
                schids[count++] = states[i].schid;
            }
        }
        state_unlock();

        for (i = 0; i < count; ++i) {
            worker_auto_away(schids[i]);
        }

        task = pop_reconnect_task(now_ms());
        if (task.valid) {
            run_reconnect_task(&task);
        }

        sleep_ms(TSG_WORKER_SLEEP_MS);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static int start_worker_thread(void)
{
#ifdef _WIN32
    worker_thread = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);
    return worker_thread != NULL;
#else
    return pthread_create(&worker_thread, NULL, worker_main, NULL) == 0;
#endif
}

static void join_worker_thread(void)
{
#ifdef _WIN32
    if (worker_thread) {
        WaitForSingleObject(worker_thread, INFINITE);
        CloseHandle(worker_thread);
        worker_thread = NULL;
    }
#else
    pthread_join(worker_thread, NULL);
#endif
}

static struct PluginHotkey* make_hotkey(const char* keyword, const char* description)
{
    struct PluginHotkey* hk = (struct PluginHotkey*)calloc(1, sizeof(*hk));
    if (!hk) return NULL;
    snprintf(hk->keyword, PLUGIN_HOTKEY_BUFSZ, "%s", keyword);
    snprintf(hk->description, PLUGIN_HOTKEY_BUFSZ, "%s", description);
    return hk;
}

static struct PluginMenuItem* make_menu(enum PluginMenuType type, int id, const char* text)
{
    struct PluginMenuItem* item = (struct PluginMenuItem*)calloc(1, sizeof(*item));
    if (!item) return NULL;
    item->type = type;
    item->id = id;
    snprintf(item->text, PLUGIN_MENU_BUFSZ, "%s", text);
    item->icon[0] = '\0';
    return item;
}

TSG_EXPORT const char* ts3plugin_name(void) { return "TSGuard"; }
TSG_EXPORT const char* ts3plugin_version(void) { return TSG_VERSION; }
TSG_EXPORT int ts3plugin_apiVersion(void) { return TSG_PLUGIN_API_VERSION; }
TSG_EXPORT const char* ts3plugin_author(void) { return "Blond"; }
TSG_EXPORT const char* ts3plugin_description(void)
{
    return "TeamSpeak 3 QoL guard for Windows and Linux: AntiMove/AntiKick, optional reconnect-after-kick/expired-temp-ban, echo-back, Auto Away, group-removal restore, AutoFollow and logging.";
}

TSG_EXPORT void ts3plugin_setFunctionPointers(const struct TS3Functions funcs)
{
    ts3Functions = funcs;
}

TSG_EXPORT int ts3plugin_init(void)
{
    memset(states, 0, sizeof(states));
    set_worker_stop(0);
    if (start_worker_thread()) {
        worker_started = 1;
    } else {
        worker_started = 0;
        tsg_log(0, LogLevel_WARNING, "Worker thread failed to start; Auto Away and delayed reconnect will be unavailable.");
    }
    tsg_log(0, LogLevel_INFO, "Loaded v%s (Plugin API %d)", TSG_VERSION, TSG_PLUGIN_API_VERSION);
    return 0;
}

TSG_EXPORT void ts3plugin_shutdown(void)
{
    set_worker_stop(1);
    if (worker_started) {
        join_worker_thread();
        worker_started = 0;
    }
    free(pluginID);
    pluginID = NULL;
    memset(states, 0, sizeof(states));
}

TSG_EXPORT int ts3plugin_requestAutoload(void) { return 0; }
TSG_EXPORT int ts3plugin_offersConfigure(void) { return PLUGIN_OFFERS_NO_CONFIGURE; }

TSG_EXPORT void ts3plugin_registerPluginID(const char* id)
{
    size_t n;
    free(pluginID);
    pluginID = NULL;
    if (!id) return;
    n = strlen(id) + 1;
    pluginID = (char*)malloc(n);
    if (pluginID) memcpy(pluginID, id, n);
}

TSG_EXPORT const char* ts3plugin_commandKeyword(void) { return "tsg"; }

TSG_EXPORT void ts3plugin_freeMemory(void* data)
{
    free(data);
}

TSG_EXPORT void ts3plugin_initHotkeys(struct PluginHotkey*** hotkeys)
{
    if (!hotkeys) return;
    *hotkeys = (struct PluginHotkey**)calloc(9, sizeof(struct PluginHotkey*));
    if (!*hotkeys) return;

    (*hotkeys)[0] = make_hotkey("tsg_toggle_antimove", "TSGuard: Toggle AntiMove / AntiKick");
    (*hotkeys)[1] = make_hotkey("tsg_toggle_serverkick", "TSGuard: Toggle Anti Server Kick");
    (*hotkeys)[2] = make_hotkey("tsg_toggle_tempban", "TSGuard: Toggle Anti Server Temporary Ban");
    (*hotkeys)[3] = make_hotkey("tsg_toggle_echo", "TSGuard: Toggle Poke/Message Back");
    (*hotkeys)[4] = make_hotkey("tsg_toggle_autoaway", "TSGuard: Toggle Auto Away");
    (*hotkeys)[5] = make_hotkey("tsg_toggle_groups", "TSGuard: Toggle Anti Group Removal");
    (*hotkeys)[6] = make_hotkey("tsg_return_last", "TSGuard: Return to last channel");
    (*hotkeys)[7] = make_hotkey("tsg_stop_follow", "TSGuard: Stop AutoFollow");
    (*hotkeys)[8] = NULL;
}

TSG_EXPORT void ts3plugin_initMenus(struct PluginMenuItem*** menuItems, char** menuIcon)
{
    size_t n = 0;
    if (menuIcon) *menuIcon = NULL;
    if (!menuItems) return;

    *menuItems = (struct PluginMenuItem**)calloc(16, sizeof(struct PluginMenuItem*));
    if (!*menuItems) return;

    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_CLIENT, TSG_MENU_FOLLOW_CLIENT, "Follow/unfollow client");

    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_TOGGLE_ANTIMOVE, "Toggle AntiMove / AntiKick");
    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_TOGGLE_SERVER_KICK, "Toggle Anti Server Kick");
    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_TOGGLE_TEMP_BAN, "Toggle Anti Server Temporary Ban");
    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_TOGGLE_ECHO_BACK, "Toggle Poke/Message Back");
    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_TOGGLE_AUTO_AWAY, "Toggle Auto Away (5 min)");
    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_TOGGLE_GROUP_GUARD, "Toggle Anti Group Removal");
    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_RETURN_LAST, "Return to last channel");

    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_STATUS_HEADER, "--- Current status ---");
    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_STATUS_ANTIMOVE, "● AntiMove / AntiKick");
    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_STATUS_SERVER_KICK, "● Anti Server Kick");
    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_STATUS_TEMP_BAN, "● Anti Server Temporary Ban");
    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_STATUS_ECHO, "● Poke/Message Back");
    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_STATUS_AUTO_AWAY, "● Auto Away");
    (*menuItems)[n++] = make_menu(PLUGIN_MENU_TYPE_GLOBAL, TSG_MENU_STATUS_GROUP_GUARD, "● Anti Group Removal");
    (*menuItems)[n] = NULL;

    if (ts3Functions.getCurrentServerConnectionHandlerID) {
        refresh_menu_status(ts3Functions.getCurrentServerConnectionHandlerID());
    }
}

TSG_EXPORT int ts3plugin_processCommand(uint64 schid, const char* command)
{
    char buf[TSG_COMMAND_MAX];
    char* save = NULL;
    char* cmd;
    char* arg;
    TSGServerState* st = state_for(schid);
    int ok;

    if (!st) return 0;

    snprintf(buf, sizeof(buf), "%s", command ? command : "");
    cmd = strtok_r(buf, " \t", &save);
    arg = strtok_r(NULL, " \t", &save);

    if (!cmd || !strcasecmp(cmd, "status")) {
        print_status(schid);
        return 0;
    }
    if (!strcasecmp(cmd, "help")) {
        print_help(schid);
        return 0;
    }
    if (!strcasecmp(cmd, "antimove") || !strcasecmp(cmd, "antikick")) {
        st->anti_move = parse_toggle(arg, st->anti_move, &ok);
        if (!ok) tsg_print(schid, "Usage: /tsg antimove [on|off|toggle]");
        else tsg_print(schid, "AntiMove / AntiKick %s.", onoff(st->anti_move));
        refresh_menu_status(schid);
        return 0;
    }
    if (!strcasecmp(cmd, "serverkick")) {
        st->anti_server_kick = parse_toggle(arg, st->anti_server_kick, &ok);
        if (!ok) tsg_print(schid, "Usage: /tsg serverkick [on|off|toggle]");
        else {
            tsg_print(schid, "Anti Server Kick %s.", onoff(st->anti_server_kick));
            if (st->anti_server_kick) capture_connection_snapshot(schid, st);
        }
        refresh_menu_status(schid);
        return 0;
    }
    if (!strcasecmp(cmd, "tempban")) {
        st->anti_temp_ban = parse_toggle(arg, st->anti_temp_ban, &ok);
        if (!ok) tsg_print(schid, "Usage: /tsg tempban [on|off|toggle]");
        else {
            tsg_print(schid, "Anti Server Temporary Ban %s.", onoff(st->anti_temp_ban));
            if (st->anti_temp_ban) capture_connection_snapshot(schid, st);
        }
        refresh_menu_status(schid);
        return 0;
    }
    if (!strcasecmp(cmd, "echo")) {
        st->echo_back = parse_toggle(arg, st->echo_back, &ok);
        if (!ok) tsg_print(schid, "Usage: /tsg echo [on|off|toggle]");
        else tsg_print(schid, "Poke/Message Back %s.", onoff(st->echo_back));
        refresh_menu_status(schid);
        return 0;
    }
    if (!strcasecmp(cmd, "autoaway")) {
        state_lock();
        st->auto_away = parse_toggle(arg, st->auto_away, &ok);
        state_unlock();
        if (!ok) tsg_print(schid, "Usage: /tsg autoaway [on|off|toggle]");
        else tsg_print(schid, "Auto Away (5 min) %s.", onoff(st->auto_away));
        refresh_menu_status(schid);
        return 0;
    }
    if (!strcasecmp(cmd, "groups") || !strcasecmp(cmd, "groupguard")) {
        st->anti_group_removal = parse_toggle(arg, st->anti_group_removal, &ok);
        if (!ok) tsg_print(schid, "Usage: /tsg groups [on|off|toggle]");
        else tsg_print(schid, "Anti Server/Channel Group Removal %s.", onoff(st->anti_group_removal));
        capture_group_state(schid, st);
        refresh_menu_status(schid);
        return 0;
    }
    if (!strcasecmp(cmd, "logging")) {
        st->logging = parse_toggle(arg, st->logging, &ok);
        if (!ok) tsg_print(schid, "Usage: /tsg logging [on|off|toggle]");
        else tsg_print(schid, "Event logging %s.", onoff(st->logging));
        return 0;
    }
    if (!strcasecmp(cmd, "back")) {
        if (!st->last_channel) tsg_print(schid, "No previous channel recorded yet.");
        else move_self(schid, st->last_channel, arg ? arg : "", "ReturnLast");
        return 0;
    }
    if (!strcasecmp(cmd, "follow")) {
        if (!arg) {
            tsg_print(schid, "Usage: /tsg follow <clientID|off>");
            return 0;
        }
        if (!strcasecmp(arg, "off") || !strcmp(arg, "0")) {
            st->follow_client = 0;
            tsg_print(schid, "AutoFollow %s.", TSG_OFF);
            return 0;
        }
        {
            unsigned long id = strtoul(arg, NULL, 10);
            uint64 channel = 0;
            if (id == 0 || id > 65535UL) {
                tsg_print(schid, "Invalid client ID: %s", arg);
                return 0;
            }
            st->follow_client = (anyID)id;
            if (ts3Functions.getChannelOfClient &&
                ts3Functions.getChannelOfClient(schid, st->follow_client, &channel) == 0 && channel) {
                tsg_print(schid, "AutoFollow client %u %s.", (unsigned int)st->follow_client, TSG_ON);
                follow_move(schid, st->follow_client, channel);
            } else {
                tsg_print(schid, "Following client %u; channel isn't resolved yet.", (unsigned int)st->follow_client);
            }
        }
        return 0;
    }

    print_help(schid);
    return 0;
}

TSG_EXPORT void ts3plugin_onHotkeyEvent(const char* keyword)
{
    uint64 schid = ts3Functions.getCurrentServerConnectionHandlerID ? ts3Functions.getCurrentServerConnectionHandlerID() : 0;
    TSGServerState* st = state_for(schid);
    if (!st || !keyword) return;

    if (!strcmp(keyword, "tsg_toggle_antimove")) {
        st->anti_move = !st->anti_move;
        tsg_print(schid, "AntiMove / AntiKick %s.", onoff(st->anti_move));
    } else if (!strcmp(keyword, "tsg_toggle_serverkick")) {
        st->anti_server_kick = !st->anti_server_kick;
        if (st->anti_server_kick) capture_connection_snapshot(schid, st);
        tsg_print(schid, "Anti Server Kick %s.", onoff(st->anti_server_kick));
    } else if (!strcmp(keyword, "tsg_toggle_tempban")) {
        st->anti_temp_ban = !st->anti_temp_ban;
        if (st->anti_temp_ban) capture_connection_snapshot(schid, st);
        tsg_print(schid, "Anti Server Temporary Ban %s.", onoff(st->anti_temp_ban));
    } else if (!strcmp(keyword, "tsg_toggle_echo")) {
        st->echo_back = !st->echo_back;
        tsg_print(schid, "Poke/Message Back %s.", onoff(st->echo_back));
    } else if (!strcmp(keyword, "tsg_toggle_autoaway")) {
        state_lock();
        st->auto_away = !st->auto_away;
        state_unlock();
        tsg_print(schid, "Auto Away (5 min) %s.", onoff(st->auto_away));
    } else if (!strcmp(keyword, "tsg_toggle_groups")) {
        st->anti_group_removal = !st->anti_group_removal;
        capture_group_state(schid, st);
        tsg_print(schid, "Anti Server/Channel Group Removal %s.", onoff(st->anti_group_removal));
    } else if (!strcmp(keyword, "tsg_return_last")) {
        if (st->last_channel) move_self(schid, st->last_channel, "", "ReturnLast");
        else tsg_print(schid, "No previous channel recorded yet.");
    } else if (!strcmp(keyword, "tsg_stop_follow")) {
        st->follow_client = 0;
        tsg_print(schid, "AutoFollow %s.", TSG_OFF);
    }
    refresh_menu_status(schid);
}

TSG_EXPORT void ts3plugin_onMenuItemEvent(uint64 schid, enum PluginMenuType type, int menuItemID, uint64 selectedItemID)
{
    TSGServerState* st = state_for(schid);
    if (!st) return;

    if (type == PLUGIN_MENU_TYPE_CLIENT && menuItemID == TSG_MENU_FOLLOW_CLIENT) {
        anyID target = (anyID)selectedItemID;
        if (st->follow_client == target) {
            st->follow_client = 0;
            tsg_print(schid, "AutoFollow %s for client %u.", TSG_OFF, (unsigned int)target);
        } else {
            uint64 channel = 0;
            st->follow_client = target;
            tsg_print(schid, "AutoFollow client %u %s.", (unsigned int)target, TSG_ON);
            if (ts3Functions.getChannelOfClient && ts3Functions.getChannelOfClient(schid, target, &channel) == 0) {
                follow_move(schid, target, channel);
            }
        }
        return;
    }

    if (type != PLUGIN_MENU_TYPE_GLOBAL) return;

    switch (menuItemID) {
        case TSG_MENU_TOGGLE_ANTIMOVE:
            st->anti_move = !st->anti_move;
            tsg_print(schid, "AntiMove / AntiKick %s.", onoff(st->anti_move));
            break;
        case TSG_MENU_TOGGLE_SERVER_KICK:
            st->anti_server_kick = !st->anti_server_kick;
            if (st->anti_server_kick) capture_connection_snapshot(schid, st);
            tsg_print(schid, "Anti Server Kick %s.", onoff(st->anti_server_kick));
            break;
        case TSG_MENU_TOGGLE_TEMP_BAN:
            st->anti_temp_ban = !st->anti_temp_ban;
            if (st->anti_temp_ban) capture_connection_snapshot(schid, st);
            tsg_print(schid, "Anti Server Temporary Ban %s.", onoff(st->anti_temp_ban));
            break;
        case TSG_MENU_TOGGLE_ECHO_BACK:
            st->echo_back = !st->echo_back;
            tsg_print(schid, "Poke/Message Back %s.", onoff(st->echo_back));
            break;
        case TSG_MENU_TOGGLE_AUTO_AWAY:
            state_lock();
            st->auto_away = !st->auto_away;
            state_unlock();
            tsg_print(schid, "Auto Away (5 min) %s.", onoff(st->auto_away));
            break;
        case TSG_MENU_TOGGLE_GROUP_GUARD:
            st->anti_group_removal = !st->anti_group_removal;
            capture_group_state(schid, st);
            tsg_print(schid, "Anti Server/Channel Group Removal %s.", onoff(st->anti_group_removal));
            break;
        case TSG_MENU_RETURN_LAST:
            if (st->last_channel) move_self(schid, st->last_channel, "", "ReturnLast");
            else tsg_print(schid, "No previous channel recorded yet.");
            break;
        default:
            /* Status rows are informational. Bright/enabled = ON; grey/disabled = OFF. */
            break;
    }
    refresh_menu_status(schid);
}

TSG_EXPORT void ts3plugin_currentServerConnectionChanged(uint64 schid)
{
    refresh_menu_status(schid);
}

TSG_EXPORT void ts3plugin_onConnectStatusChangeEvent(uint64 schid, int newStatus, unsigned int errorNumber)
{
    TSGServerState* st = state_for(schid);
    (void)errorNumber;
    if (!st) return;

    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        state_lock();
        st->reconnect_pending = 0;
        state_unlock();
        capture_connection_snapshot(schid, st);
        refresh_menu_status(schid);
    }
}

TSG_EXPORT void ts3plugin_onClientMoveEvent(uint64 schid, anyID clientID, uint64 oldChannelID, uint64 newChannelID,
                                           int visibility, const char* moveMessage)
{
    anyID me;
    TSGServerState* st = state_for(schid);
    (void)visibility;
    (void)moveMessage;
    if (!st) return;

    follow_move(schid, clientID, newChannelID);

    if (get_self_id(schid, &me) && clientID == me) {
        if (st->suppress_self_move_events > 0) {
            st->suppress_self_move_events--;
        } else if (oldChannelID && newChannelID && oldChannelID != newChannelID) {
            st->last_channel = oldChannelID;
        }
        capture_connection_snapshot(schid, st);
    }
}

TSG_EXPORT void ts3plugin_onClientMoveMovedEvent(uint64 schid, anyID clientID, uint64 oldChannelID, uint64 newChannelID,
                                                int visibility, anyID moverID, const char* moverName,
                                                const char* moverUniqueIdentifier, const char* moveMessage)
{
    anyID me;
    TSGServerState* st = state_for(schid);
    (void)visibility;
    (void)moverUniqueIdentifier;
    if (!st) return;

    follow_move(schid, clientID, newChannelID);
    if (!get_self_id(schid, &me) || clientID != me) return;

    st->last_channel = oldChannelID;
    if (st->logging) {
        tsg_print(schid, "Moved by %s (%u): %llu -> %llu%s%s",
                 moverName ? moverName : "unknown", (unsigned int)moverID,
                 (unsigned long long)oldChannelID, (unsigned long long)newChannelID,
                 (moveMessage && *moveMessage) ? " | " : "",
                 (moveMessage && *moveMessage) ? moveMessage : "");
    }

    if (st->anti_move && oldChannelID) {
        if (!move_self(schid, oldChannelID, "", "AntiMove")) {
            tsg_print(schid, "AntiMove couldn't return you. Channel may require a password/permission, or the limiter fired.");
        }
    }
}

TSG_EXPORT void ts3plugin_onClientKickFromChannelEvent(uint64 schid, anyID clientID, uint64 oldChannelID, uint64 newChannelID,
                                                       int visibility, anyID kickerID, const char* kickerName,
                                                       const char* kickerUniqueIdentifier, const char* kickMessage)
{
    anyID me;
    TSGServerState* st = state_for(schid);
    (void)visibility;
    (void)kickerUniqueIdentifier;
    if (!st) return;

    follow_move(schid, clientID, newChannelID);
    if (!get_self_id(schid, &me) || clientID != me) return;

    st->last_channel = oldChannelID;
    if (st->logging) {
        tsg_print(schid, "Channel-kicked by %s (%u)%s%s",
                 kickerName ? kickerName : "unknown", (unsigned int)kickerID,
                 (kickMessage && *kickMessage) ? ": " : "",
                 (kickMessage && *kickMessage) ? kickMessage : "");
    }

    if (st->anti_move && oldChannelID) {
        if (!move_self(schid, oldChannelID, "", "AntiKick")) {
            tsg_print(schid, "AntiKick couldn't return you. Try /tsg back <password> for password-protected channels.");
        }
    }
}

TSG_EXPORT void ts3plugin_onClientKickFromServerEvent(uint64 schid, anyID clientID, uint64 oldChannelID, uint64 newChannelID,
                                                      int visibility, anyID kickerID, const char* kickerName,
                                                      const char* kickerUniqueIdentifier, const char* kickMessage)
{
    anyID me;
    TSGServerState* st = state_for(schid);
    (void)newChannelID;
    (void)visibility;
    (void)kickerUniqueIdentifier;
    if (!st || !get_self_id(schid, &me) || clientID != me) return;

    if (oldChannelID) st->last_channel = oldChannelID;
    st->follow_client = 0;
    capture_connection_snapshot(schid, st);

    if (st->logging) {
        tsg_log(schid, LogLevel_INFO, "Server-kicked by %s (%u): %s",
               kickerName ? kickerName : "unknown", (unsigned int)kickerID,
               (kickMessage && *kickMessage) ? kickMessage : "(no reason)");
    }

    if (st->anti_server_kick) {
        schedule_reconnect(schid, st, TSG_RECONNECT_SERVER_KICK, 1, 0);
    }
}

TSG_EXPORT void ts3plugin_onClientBanFromServerEvent(uint64 schid, anyID clientID, uint64 oldChannelID, uint64 newChannelID,
                                                     int visibility, anyID kickerID, const char* kickerName,
                                                     const char* kickerUniqueIdentifier, uint64 banTime,
                                                     const char* kickMessage)
{
    anyID me;
    TSGServerState* st = state_for(schid);
    (void)newChannelID;
    (void)visibility;
    (void)kickerUniqueIdentifier;
    if (!st || !get_self_id(schid, &me) || clientID != me) return;

    if (oldChannelID) st->last_channel = oldChannelID;
    st->follow_client = 0;
    capture_connection_snapshot(schid, st);

    if (st->logging) {
        tsg_log(schid, LogLevel_INFO, "Server-ban by %s (%u), duration=%llu sec: %s",
               kickerName ? kickerName : "unknown", (unsigned int)kickerID,
               (unsigned long long)banTime,
               (kickMessage && *kickMessage) ? kickMessage : "(no reason)");
    }

    if (st->anti_temp_ban) {
        if (banTime > 0) {
            schedule_reconnect(schid, st, TSG_RECONNECT_TEMP_BAN, banTime + 1ULL, banTime);
        } else {
            tsg_log(schid, LogLevel_INFO, "Permanent/indefinite ban detected; no reconnect scheduled.");
        }
    }
}

TSG_EXPORT int ts3plugin_onTextMessageEvent(uint64 schid, anyID targetMode, anyID toID, anyID fromID,
                                           const char* fromName, const char* fromUniqueIdentifier,
                                           const char* message, int ffIgnored)
{
    TSGServerState* st = state_for(schid);
    anyID me;
    uint64_t h;
    (void)toID;
    (void)fromName;
    (void)fromUniqueIdentifier;

    if (!st || !st->echo_back || ffIgnored || targetMode != TextMessageTarget_CLIENT ||
        !message || !get_self_id(schid, &me) || fromID == me || !ts3Functions.requestSendPrivateTextMsg) {
        return 0;
    }

    h = hash_text(message);
    if (should_suppress_echo(st, fromID, h, 0)) {
        return 0;
    }

    if (ts3Functions.requestSendPrivateTextMsg(schid, message, fromID, NULL) == 0) {
        remember_echo(st, fromID, h, 0);
    }
    return 0;
}

TSG_EXPORT int ts3plugin_onClientPokeEvent(uint64 schid, anyID fromClientID, const char* pokerName,
                                          const char* pokerUniqueIdentity, const char* message, int ffIgnored)
{
    TSGServerState* st = state_for(schid);
    anyID me;
    uint64_t h;
    (void)pokerName;
    (void)pokerUniqueIdentity;

    if (!st || !st->echo_back || ffIgnored || !message || !get_self_id(schid, &me) ||
        fromClientID == me || !ts3Functions.requestClientPoke) {
        return 0;
    }

    h = hash_text(message);
    if (should_suppress_echo(st, fromClientID, h, 1)) {
        return 0;
    }

    if (ts3Functions.requestClientPoke(schid, fromClientID, message, NULL) == 0) {
        remember_echo(st, fromClientID, h, 1);
    }
    return 0;
}

TSG_EXPORT void ts3plugin_onServerGroupClientDeletedEvent(uint64 schid, anyID clientID, const char* clientName,
                                                          const char* clientUniqueIdentity, uint64 serverGroupID,
                                                          anyID invokerClientID, const char* invokerName,
                                                          const char* invokerUniqueIdentity)
{
    TSGServerState* st = state_for(schid);
    anyID me;
    uint64 dbid;
    unsigned int err;
    (void)clientName;
    (void)clientUniqueIdentity;
    (void)invokerUniqueIdentity;

    if (!st || !st->anti_group_removal || !get_self_id(schid, &me) || clientID != me || invokerClientID == me) {
        return;
    }
    if (!group_guard_allow(st)) {
        tsg_print(schid, "Group restore stopped: safety limiter hit.");
        return;
    }

    dbid = get_self_dbid(schid);
    if (!dbid || !ts3Functions.requestServerGroupAddClient) {
        return;
    }

    err = ts3Functions.requestServerGroupAddClient(schid, serverGroupID, dbid, NULL);
    if (err == 0) {
        tsg_print(schid, "Server group %llu removed by %s; restore requested.",
                 (unsigned long long)serverGroupID, invokerName ? invokerName : "unknown");
    } else {
        tsg_print(schid, "Server group restore failed immediately (error %u).", err);
    }
}

TSG_EXPORT void ts3plugin_onClientChannelGroupChangedEvent(uint64 schid, uint64 channelGroupID, uint64 channelID,
                                                           anyID clientID, anyID invokerClientID, const char* invokerName,
                                                           const char* invokerUniqueIdentity)
{
    TSGServerState* st = state_for(schid);
    anyID me;
    uint64 previous;
    uint64 dbid;
    uint64 group_ids[1];
    uint64 channel_ids[1];
    uint64 dbids[1];
    unsigned int err;
    (void)invokerUniqueIdentity;

    if (!st || !get_self_id(schid, &me) || clientID != me) {
        return;
    }

    if (invokerClientID == me || !st->anti_group_removal) {
        st->tracked_channel_group = channelGroupID;
        st->tracked_channel_for_group = channelID;
        return;
    }

    previous = (st->tracked_channel_for_group == channelID) ? st->tracked_channel_group : 0;
    if (!st->default_channel_group) {
        capture_group_state(schid, st);
    }

    if (!previous || previous == channelGroupID ||
        (channelGroupID != 0 && channelGroupID != st->default_channel_group)) {
        st->tracked_channel_group = channelGroupID;
        st->tracked_channel_for_group = channelID;
        return;
    }

    if (!group_guard_allow(st)) {
        tsg_print(schid, "Channel-group restore stopped: safety limiter hit.");
        return;
    }

    dbid = get_self_dbid(schid);
    if (!dbid || !ts3Functions.requestSetClientChannelGroup) {
        return;
    }

    group_ids[0] = previous;
    channel_ids[0] = channelID;
    dbids[0] = dbid;
    err = ts3Functions.requestSetClientChannelGroup(schid, group_ids, channel_ids, dbids, 1, NULL);
    if (err == 0) {
        tsg_print(schid, "Channel group %llu removed by %s; restore requested.",
                 (unsigned long long)previous, invokerName ? invokerName : "unknown");
    } else {
        tsg_print(schid, "Channel-group restore failed immediately (error %u).", err);
        st->tracked_channel_group = channelGroupID;
        st->tracked_channel_for_group = channelID;
    }
}
