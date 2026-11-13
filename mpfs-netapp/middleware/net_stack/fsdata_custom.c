/* src/middleware/net_stack/fsdata_custom.c (Included by lwIP fs.c — do not compile separately) */
#include "lwip/apps/fs.h"
#include "lwip/def.h"

static const char g_index_html[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Connection: close\r\n\r\n"
    "<!DOCTYPE html><html><head><title>PolarFire SoC U54 Server</title>"
    "<style>body{font-family:sans-serif;margin:2rem;background:#0f172a;color:#e2e8f0;}"
    ".box{background:#1e293b;padding:1.5rem;border-radius:8px;max-width:560px;}"
    "a{color:#38bdf8;}</style></head><body><div class='box'>"
    "<h2>PolarFire&reg; SoC Bare-Metal Node</h2>"
    "<p>Booted via Hart Software Services (HSS).</p>"
    "<ul><li>HTTP Server: <b>Running (Port 80)</b></li>"
    "<li>TFTP Server: <b>Running (UDP Port 69)</b></li></ul>"
    "<p><a href='/ctrl.cgi?cmd=led_toggle'>Send CGI Command to UART</a></p>"
    "</div></body></html>";

const struct fsdata_file file_index_html[] = {{
    NULL,
    (const unsigned char *)"/index.html",
    (const unsigned char *)g_index_html,
    sizeof(g_index_html) - 1U,
    FS_FILE_FLAGS_HEADER_INCLUDED | FS_FILE_FLAGS_HEADER_PERSISTENT
}};

#define FS_ROOT file_index_html
#define FS_NUMFILES 1