#include <string.h>
#include "lwip/apps/tftp_server.h"
#include "tftp_server_app.h"
#include "mmc_storage.h"

typedef struct {
    uint8_t  is_write;
    uint32_t file_size;
    uint32_t offset;
} tftp_mmc_handle_t;

static tftp_mmc_handle_t g_tftp_handle;

static void *tftp_mmc_open(const char *fname, const char *mode, u8_t write)
{
    (void)mode;
    memset(&g_tftp_handle, 0, sizeof(g_tftp_handle));
    g_tftp_handle.is_write = write;

    if (write != 0U) {
        return mmc_file_open_write(fname) ? &g_tftp_handle : NULL;
    } else {
        return mmc_file_open_read(fname, &g_tftp_handle.file_size) ? &g_tftp_handle : NULL;
    }
}

static void tftp_mmc_close(void *handle)
{
    tftp_mmc_handle_t *h = (tftp_mmc_handle_t *)handle;
    if ((h != NULL) && (h->is_write != 0U)) {
        mmc_file_close_write();
    }
}

static int tftp_mmc_read(void *handle, void *buf, int bytes)
{
    tftp_mmc_handle_t *h = (tftp_mmc_handle_t *)handle;
    if ((h == NULL) || (bytes <= 0)) {
        return -1;
    }

    int n = mmc_file_read_chunk((uint8_t *)buf, (uint32_t)bytes, h->offset);
    if (n > 0) {
        h->offset += (uint32_t)n;
    }
    return n;
}

static int tftp_mmc_write(void *handle, struct pbuf *p)
{
    tftp_mmc_handle_t *h = (tftp_mmc_handle_t *)handle;
    if ((h == NULL) || (p == NULL)) {
        return -1;
    }

    for (struct pbuf *q = p; q != NULL; q = q->next) {
        if (mmc_file_write_chunk((const uint8_t *)q->payload, q->len) < 0) {
            return -1;
        }
    }
    return 0;
}

static const struct tftp_context g_tftp_ctx = {
    .open  = tftp_mmc_open,
    .close = tftp_mmc_close,
    .read  = tftp_mmc_read,
    .write = tftp_mmc_write,
};

void tftp_server_app_init(void)
{
    (void)tftp_init_server(&g_tftp_ctx);
}

void tftp_server_print_info(void)
{
    mmc_storage_print_info();
}