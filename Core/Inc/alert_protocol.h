#ifndef ALERT_PROTOCOL_H
#define ALERT_PROTOCOL_H
#include <string.h>
/* Called after each TCP fragment is appended and NUL-terminated. Only a full
 * matching application ACK counts as delivery, never a successful socket send.
 */
static int AlertProtocol_IsAck(const char *response, const char *expected)
{
    const char *body = strstr(response, "\r\n\r\n");
    return body && (!strncmp(response, "HTTP/1.0 200 ", 13) ||
                    !strncmp(response, "HTTP/1.1 200 ", 13)) &&
           !strcmp(body + 4, expected);
}
#endif
