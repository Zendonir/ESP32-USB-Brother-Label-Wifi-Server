#include "raw_tcp_server.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "print_queue.h"
#include "sdkconfig.h"

static const char *TAG = "raw9100";

#define NVS_KEY_ALLOW "raw_allow"

/* Allowed client network (network byte order); mask 0 = everyone */
static uint32_t s_allow_net;
static uint32_t s_allow_mask;
static uint32_t s_connections;

static bool parse_cidr(const char *s, uint32_t *net, uint32_t *mask)
{
    if (s[0] == '\0' || strcmp(s, "any") == 0) {
        *net = *mask = 0;
        return true;
    }
    char ip[20];
    strlcpy(ip, s, sizeof(ip));
    int bits = 32;
    char *slash = strchr(ip, '/');
    if (slash) {
        *slash = '\0';
        char *end;
        long b = strtol(slash + 1, &end, 10);
        if (*end || b < 0 || b > 32) {
            return false;
        }
        bits = (int)b;
    }
    struct in_addr a;
    if (inet_aton(ip, &a) == 0) {
        return false;
    }
    uint32_t m = bits == 0 ? 0 : htonl(0xffffffffu << (32 - bits));
    *net = a.s_addr & m;
    *mask = m;
    return true;
}

esp_err_t raw_tcp_server_set_allowed(const char *cidr)
{
    uint32_t net, mask;
    if (!parse_cidr(cidr, &net, &mask)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = cfg_set_str(CFG_NS_SYSTEM, NVS_KEY_ALLOW, mask ? cidr : "any");
    if (err == ESP_OK) {
        s_allow_net = net;
        s_allow_mask = mask;
        ESP_LOGI(TAG, "allowed clients: %s", mask ? cidr : "any");
    }
    return err;
}

void raw_tcp_server_get_allowed(char *buf, size_t size)
{
    if (cfg_get_str(CFG_NS_SYSTEM, NVS_KEY_ALLOW, buf, size) != ESP_OK || !buf[0]) {
        strlcpy(buf, "any", size);
    }
}

static void handle_client(int sock, const char *client)
{
    /* Windows' Standard TCP/IP port (RAW) sends the job and then closes the connection:
     * the end of a job is the end of the connection, never a packet boundary. */
    struct timeval tv = { .tv_sec = CONFIG_RAW_TCP_IDLE_TIMEOUT_S, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    static uint8_t buf[4096];
    print_spool_t sp = { 0 };
    const char *fail = NULL;
    for (;;) {
        int r = recv(sock, buf, sizeof(buf), 0);
        if (r == 0) {
            break; /* orderly close: job complete */
        }
        if (r < 0) {
            fail = (errno == EAGAIN || errno == EWOULDBLOCK) ? "no data received for too long (idle timeout)"
                                                             : "connection error before end of job";
            break;
        }
        esp_err_t err = print_spool_append(&sp, buf, (size_t)r);
        if (err == ESP_ERR_INVALID_SIZE) {
            fail = "job larger than the configured maximum";
            break;
        }
        if (err != ESP_OK) {
            fail = "out of memory while receiving";
            break;
        }
    }
    if (fail) {
        size_t n = sp.len;
        print_spool_free(&sp);
        print_queue_record_incomplete(JOB_SRC_RAW9100, client, n, fail);
        return;
    }
    if (sp.len == 0) {
        ESP_LOGI(TAG, "%s connected without data", client);
        print_spool_free(&sp);
        return;
    }
    uint32_t id = 0;
    if (print_queue_submit(&sp, JOB_SRC_RAW9100, client, &id) != ESP_OK) {
        print_queue_record_incomplete(JOB_SRC_RAW9100, client, 0, "print queue full - job rejected");
    }
}

static void server_task(void *arg)
{
    for (;;) {
        int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (ls < 0) {
            ESP_LOGE(TAG, "socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        int one = 1;
        setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(CONFIG_RAW_TCP_PORT),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };
        if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(ls, 4) != 0) {
            ESP_LOGE(TAG, "bind/listen on port %d failed: errno %d", CONFIG_RAW_TCP_PORT, errno);
            close(ls);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        ESP_LOGI(TAG, "RAW print server listening on TCP port %d", CONFIG_RAW_TCP_PORT);
        for (;;) {
            struct sockaddr_in peer;
            socklen_t plen = sizeof(peer);
            int cs = accept(ls, (struct sockaddr *)&peer, &plen);
            if (cs < 0) {
                ESP_LOGE(TAG, "accept: errno %d", errno);
                break;
            }
            char client[16];
            inet_ntoa_r(peer.sin_addr, client, sizeof(client));
            s_connections++;
            if (s_allow_mask && (peer.sin_addr.s_addr & s_allow_mask) != s_allow_net) {
                ESP_LOGW(TAG, "connection from %s rejected (not in allowed network)", client);
                close(cs);
                continue;
            }
            ESP_LOGI(TAG, "connection from %s", client);
            /* One connection at a time; others wait in the listen backlog. Receiving
             * only spools - printing happens in the queue worker - so this is quick. */
            handle_client(cs, client);
            shutdown(cs, SHUT_RDWR);
            close(cs);
        }
        close(ls);
    }
}

esp_err_t raw_tcp_server_start(void)
{
    char cidr[24];
    raw_tcp_server_get_allowed(cidr, sizeof(cidr));
    if (!parse_cidr(cidr, &s_allow_net, &s_allow_mask)) {
        s_allow_net = s_allow_mask = 0;
    }
    if (s_allow_mask) {
        ESP_LOGI(TAG, "allowed clients: %s", cidr);
    }
    return xTaskCreate(server_task, "raw9100", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

uint32_t raw_tcp_server_connections(void)
{
    return s_connections;
}
