/* src/middleware/net_stack/tftp_server_app.c */
#include <stdio.h>
#include <string.h>
#include "board_config.h"
#include "tftp_server_app.h"
#include "lwip/apps/tftp_server.h"

/* 192 KiB RAM-disk (leaves >250 KiB headroom inside 512 KiB L2-LIM) */
//#define TFTP_RAMDISK_BYTES  (192U * 1024U)


#if defined(MEM_TARGET_LIM)
#define TFTP_RAMDISK_BYTES  (16U * 1024U)       /* 16 KiB in L2-LIM SRAM */
#else
#define TFTP_RAMDISK_BYTES  (1024U * 1024U)     /* 1 MiB in DDR */
#endif



static uint8_t s_ramdisk[TFTP_RAMDISK_BYTES] __attribute__((aligned(64)));
static size_t  s_stored_len = 0U;
static char    s_stored_name[64] = "(none)";

typedef struct {
    size_t  offset;
    uint8_t is_write;
} tftp_ctx_state_t;

static tftp_ctx_state_t s_state;

void tftp_server_print_info(void)
{
    char msg[160];
    snprintf(msg, sizeof(msg),
             "\r\n[TFTP] Buffer Addr : 0x%010lx (Cap: %u KiB)\r\n"
             "[TFTP] Stored File : %s (%lu bytes)\r\n",
             (unsigned long)(uintptr_t)s_ramdisk,
             (unsigned int)(TFTP_RAMDISK_BYTES / 1024U),
             s_stored_name, (unsigned long)s_stored_len);
    MSS_UART_polled_tx_string(APP_UART, (const uint8_t *)msg);
}

static void *tftp_open(const char *fname, const char *mode, u8_t write)
{
    (void)mode;
    char msg[128];
    s_state.offset = 0U;
    s_state.is_write = write;

    if (write) {
        s_stored_len = 0U;
        snprintf(s_stored_name, sizeof(s_stored_name), "%s", fname);
        snprintf(msg, sizeof(msg), "[TFTP] Receiving '%s'...\r\n", fname);
    } else {
        snprintf(msg, sizeof(msg), "[TFTP] Sending '%s' (%lu bytes)...\r\n",
                 s_stored_name, (unsigned long)s_stored_len);
    }
    MSS_UART_polled_tx_string(APP_UART, (const uint8_t *)msg);
    return &s_state;
}

static void tftp_close(void *handle)
{
    tftp_ctx_state_t *st = (tftp_ctx_state_t *)handle;
    if (st != NULL && st->is_write) {
        tftp_server_print_info();
    }
}

static int tftp_read(void *handle, void *buf, int bytes)
{
    tftp_ctx_state_t *st = (tftp_ctx_state_t *)handle;
    if (st->offset >= s_stored_len) {
        return 0;
    }
    size_t rem = s_stored_len - st->offset;
    size_t n = ((size_t)bytes < rem) ? (size_t)bytes : rem;
    memcpy(buf, &s_ramdisk[st->offset], n);
    st->offset += n;
    return (int)n;
}

static int tftp_write(void *handle, struct pbuf *p)
{
    tftp_ctx_state_t *st = (tftp_ctx_state_t *)handle;
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        if ((st->offset + q->len) > TFTP_RAMDISK_BYTES) {
            MSS_UART_polled_tx_string(APP_UART, (const uint8_t *)"[TFTP] ERROR: Buffer full!\r\n");
            return -1;
        }
        memcpy(&s_ramdisk[st->offset], q->payload, q->len);
        st->offset += q->len;
    }
    s_stored_len = st->offset;
    return 0;
}

static const struct tftp_context g_tftp_ops = {
    .open  = tftp_open,
    .close = tftp_close,
    .read  = tftp_read,
    .write = tftp_write
};

void tftp_server_app_init(void)
{
    tftp_init_server(&g_tftp_ops);
}