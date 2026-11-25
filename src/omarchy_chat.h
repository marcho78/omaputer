#ifndef OMARCHY_CHAT_H
#define OMARCHY_CHAT_H

#if defined(OMARCHY_GROK)

#include <esp_http_server.h>

// The web portal's chat with Grok. The browser sends its request for xAI's API to the
// Cardputer, which adds the xAI key saved in Settings > Grok and passes it on, so the key
// never reaches the browser. xAI's answer comes back as it arrives: its status, its type and
// its body, the reply streaming in while Grok writes it.
//
// Sends req's body, if any, to https://api.x.ai<path> and answers req with what comes back.
// For POST /chat (path /v1/responses) and GET /chatmodels (/v1/models). The caller checks
// the login first. The web server answers nothing else while a reply streams.
esp_err_t omarchyChatRelay(httpd_req_t *req, httpd_method_t method, const char *path);

#endif
#endif
