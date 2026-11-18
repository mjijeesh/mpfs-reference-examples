/* src/middleware/net_stack/web_server.c */
#include <stdio.h>
#include "board_config.h"
#include "web_server.h"
#include "lwip/apps/httpd.h"

/**
 * @brief CGI Handler for the /ctrl.cgi endpoint.
 *
 * This function is triggered by the lwIP HTTP server whenever a client makes
 * an HTTP GET request to `/ctrl.cgi` (e.g., submitting an HTML form or
 * clicking a link like `http://<board-ip>/ctrl.cgi?param1=val1&param2=val2`).
 *
 * It parses the URL query parameters and logs each key-value pair directly
 * to the board's physical UART console.
 *
 * @param iIndex     The index of the CGI handler in the g_cgis array (unused here).
 * @param iNumParams The total number of query parameters extracted from the URL.
 * @param pcParam    Array of strings containing the parameter names (keys).
 * @param pcValue    Array of strings containing the corresponding parameter values.
 * 
 * @return const char* The URI of the HTML page the web server should send back 
 *                     to the client as a response (in this case, redirecting 
 *                     them back to the main "/index.html" page).
 */
static const char *cgi_ctrl_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
    (void)iIndex; /* Suppress unused parameter warning */
    
    /* Iterate through every parameter provided in the HTTP request */
    for (int i = 0; i < iNumParams; i++) {
        char buf[96];
        /* Format the key-value pair for logging */
        snprintf(buf, sizeof(buf), "[HTTP-CGI] %s = %s\r\n", pcParam[i], pcValue[i]);
        /* Transmit the log string out over the application UART (MMUART1) */
        MSS_UART_polled_tx_string(APP_UART, (const uint8_t *)buf);
    }
    
    /* Redirect the client's browser back to the root index page */
    return "/index.html";
}

/**
 * @brief Global CGI routing table for the HTTP server.
 *
 * This array maps incoming URI paths (e.g., "/ctrl.cgi") to their specific 
 * C function handlers. When lwIP parses an HTTP request matching a path in 
 * this table, it automatically calls the associated handler.
 */
static const tCGI g_cgis[] = {
    { "/ctrl.cgi", cgi_ctrl_handler }
};

/**
 * @brief Initializes the embedded web server and registers CGI routes.
 *
 * This function must be called exactly once during the system boot sequence,
 * immediately after initializing the core lwIP network stack and Ethernet ports.
 * 
 * It starts the lwIP HTTP daemon (listening on TCP port 80 by default) and
 * passes our custom routing table (`g_cgis`) so the server knows how to 
 * handle interactive web requests.
 */
void web_server_init(void)
{
    /* Start the core lwIP HTTP daemon (TCP/80) */
    httpd_init();
    
    /* Register our custom CGI handler array. The '1' specifies the array size. */
    http_set_cgi_handlers(g_cgis, 1);
}
