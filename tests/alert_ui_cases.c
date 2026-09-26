#include "alert_ui.h"
#include "alert_protocol.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int alert_ui_test(void)
{
    AlertUI u = {0};
    CHECK(!AlertUI_Update(&u, 0, 1, 1, 0));
    CHECK(!AlertUI_Update(&u, 5000, 1, 1, 0)); /* Held at startup is ignored. */
    AlertUI_Update(&u, 5010, 0, 1, 0);
    CHECK(!AlertUI_Update(&u, 5020, 1, 1, 0));
    CHECK(!AlertUI_Update(&u, 8000, 1, 1, 0));
    CHECK(AlertUI_Update(&u, 8020, 1, 1, 0));
    CHECK(!AlertUI_Update(&u, 8020, 1, 0, 1));
    CHECK(AlertUI_BuzzerOn(&u, 8020));
    CHECK(!AlertUI_BuzzerOn(&u, 8170));
    CHECK(AlertUI_BuzzerOn(&u, 8270));
    CHECK(!AlertUI_BuzzerOn(&u, 8470));
    CHECK(AlertUI_BuzzerOn(&u, 23470)); /* Third pulse after escalation. */
    AlertUI_Update(&u, 24000, 1, 0, 0);
    CHECK(AlertUI_BuzzerOn(&u, 24050));
    CHECK(!AlertUI_BuzzerOn(&u, 24100));
    CHECK(!AlertUI_Update(&u, 30000, 1, 1, 0)); /* ACK hold cannot retrigger. */
    u = (AlertUI){0};
    AlertUI_Update(&u, UINT32_MAX-100, 0, 0, 1);
    CHECK(AlertUI_BuzzerOn(&u, UINT32_MAX-50));
    CHECK(!AlertUI_BuzzerOn(&u, 50));
    CHECK(!AlertProtocol_IsAck("HTTP/1.1 200 OK\r\n\r\nACK boot-1", "ACK boot-1\n"));
    CHECK(AlertProtocol_IsAck("HTTP/1.1 200 OK\r\n\r\nACK boot-1\n", "ACK boot-1\n"));
    CHECK(!AlertProtocol_IsAck("HTTP/1.1 401 Unauthorized\r\n\r\nACK boot-1\n", "ACK boot-1\n"));
    CHECK(!AlertProtocol_IsAck("HTTP/1.1 200 OK\r\n\r\nACK boot-10\n", "ACK boot-1\n"));
    return 0;
}
