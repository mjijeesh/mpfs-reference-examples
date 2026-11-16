/* src/middleware/net_stack/web_server.c */
#include <stdio.h>
#include "board_config.h"
#include "web_server.h"
#include "lwip/apps/httpd.h"

static const char *cgi_ctrl_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    (void)iIndex;
    for (int i = 0; i < iNumParams; i++) {
        char buf[96];
        snprintf(buf, sizeof(buf), "[HTTP-CGI] %s = %s\r\n", pcParam[i], pcValue[i]);
        MSS_UART_polled_tx_string(APP_UART, (const uint8_t *)buf);
    }
    return "/index.html";
}

static const tCGI g_cgis[] = {
    { "/ctrl.cgi", cgi_ctrl_handler }
};

void web_server_init(void)
{
    httpd_init();
    http_set_cgi_handlers(g_cgis, 1);
}