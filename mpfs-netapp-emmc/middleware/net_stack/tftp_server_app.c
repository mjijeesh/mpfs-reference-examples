#include <string.h>
#include "lwip/apps/tftp_server.h"
#include "tftp_server_app.h"
#include "mmc_storage.h"

/**
 * @brief Struct to hold state for a single active TFTP session.
 */
typedef struct {
    uint8_t  is_write;   /* 1 if writing (PUT), 0 if reading (GET) */
    uint32_t file_size;  /* Total size of the file (for GET) */
    uint32_t offset;     /* Current byte offset in the transfer */
} tftp_mmc_handle_t;

/* Bare-metal limitation: only supports 1 concurrent TFTP connection */
static tftp_mmc_handle_t g_tftp_handle;

/**
 * @brief Callback fired by lwIP when a WRQ (Write) or RRQ (Read) packet arrives.
 * 
 * @param fname The filename requested by the TFTP client.
 * @param mode The TFTP transfer mode (e.g., "octet").
 * @param write 1 if the client wants to write (PUT), 0 if they want to read (GET).
 * 
 * @return void* A handle to the session context, or NULL to reject the request.
 */
static void *tftp_mmc_open(const char *fname, const char *mode, u8_t write)
{
    (void)mode;
    memset(&g_tftp_handle, 0, sizeof(g_tftp_handle));
    g_tftp_handle.is_write = write;

    /* Delegate to the mmc_storage layer based on read/write intent */
    if (write != 0U) {
        return mmc_file_open_write(fname) ? &g_tftp_handle : NULL;
    } else {
        return mmc_file_open_read(fname, &g_tftp_handle.file_size) ? &g_tftp_handle : NULL;
    }
}

/**
 * @brief Callback fired by lwIP when a TFTP session ends (success or failure).
 * 
 * @param handle Pointer to the session context (g_tftp_handle).
 */
static void tftp_mmc_close(void *handle)
{
    tftp_mmc_handle_t *h = (tftp_mmc_handle_t *)handle;
    /* If this was a write session, finalize the file metadata to flash */
    if ((h != NULL) && (h->is_write != 0U)) {
        mmc_file_close_write();
    }
}

/**
 * @brief Callback fired by lwIP to fetch file data to send to the client (GET).
 * 
 * @param handle Pointer to the session context.
 * @param buf Buffer allocated by lwIP to hold the outbound chunk.
 * @param bytes Number of bytes lwIP wants to send in this packet.
 * 
 * @return int Number of bytes actually read from flash and placed in 'buf', or -1 on error.
 */
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

/**
 * @brief Callback fired by lwIP when new file data arrives from the client (PUT).
 * 
 * @param handle Pointer to the session context.
 * @param p Pointer to the start of lwIP's pbuf chain containing the payload.
 * 
 * @return int 0 on success, or -1 on error (which aborts the TFTP connection).
 */
static int tftp_mmc_write(void *handle, struct pbuf *p)
{
    tftp_mmc_handle_t *h = (tftp_mmc_handle_t *)handle;
    if ((h == NULL) || (p == NULL)) {
        return -1;
    }

    /* Iterate through the linked pbuf chain and write each segment to flash */
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        if (mmc_file_write_chunk((const uint8_t *)q->payload, q->len) < 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief lwIP TFTP context mapping our specific MMC callbacks to the server engine.
 */
static const struct tftp_context g_tftp_ctx = {
    .open  = tftp_mmc_open,
    .close = tftp_mmc_close,
    .read  = tftp_mmc_read,
    .write = tftp_mmc_write,
};

/**
 * @brief Initializes the lwIP TFTP daemon.
 */
void tftp_server_app_init(void)
{
    (void)tftp_init_server(&g_tftp_ctx);
}

/**
 * @brief Exposes the underlying MMC metadata diagnostics.
 */
void tftp_server_print_info(void)
{
    mmc_storage_print_info();
}
