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
    CHECK(AlertUI_BuzzerHz(&u, 8020));
    CHECK(!AlertUI_BuzzerHz(&u, 8170));
    CHECK(AlertUI_BuzzerHz(&u, 8270));
    CHECK(!AlertUI_BuzzerHz(&u, 8470));
    CHECK(AlertUI_BuzzerHz(&u, 23470)); /* Third pulse after escalation. */
    AlertUI_Update(&u, 24000, 1, 0, 0);
    CHECK(AlertUI_BuzzerHz(&u, 24050));
    CHECK(!AlertUI_BuzzerHz(&u, 24100));
    CHECK(!AlertUI_Update(&u, 30000, 1, 1, 0)); /* ACK hold cannot retrigger. */
    u = (AlertUI){0};
    AlertUI_Update(&u, UINT32_MAX-100, 0, 0, 1);
    CHECK(AlertUI_BuzzerHz(&u, UINT32_MAX-50));
    CHECK(!AlertUI_BuzzerHz(&u, 50));
    CHECK(!AlertProtocol_IsAck("HTTP/1.1 200 OK\r\n\r\nACK boot-1", "ACK boot-1\n"));
    CHECK(AlertProtocol_IsAck("HTTP/1.1 200 OK\r\n\r\nACK boot-1\n", "ACK boot-1\n"));
    CHECK(!AlertProtocol_IsAck("HTTP/1.1 401 Unauthorized\r\n\r\nACK boot-1\n", "ACK boot-1\n"));
    CHECK(!AlertProtocol_IsAck("HTTP/1.1 200 OK\r\n\r\nACK boot-10\n", "ACK boot-1\n"));
    u = (AlertUI){0};
    AlertUI_Update(&u, 0, 0, 1, 0);
    AlertUI_Update(&u, 100, 1, 1, 0);
    AlertUI_Update(&u, 180, 0, 1, 0);
    CHECK(!u.tuning);
    AlertUI_Update(&u, 300, 1, 1, 0);
    AlertUI_Update(&u, 380, 0, 1, 0);
    CHECK(u.tuning && AlertUI_BuzzerHz(&u, 400));
    CHECK(!AlertUI_BuzzerHz(&u, 500));
    CHECK(AlertUI_BuzzerHz(&u, 800));
    AlertUI_Update(&u, 1200, 0, 1, 0);
    CHECK(!u.tuning && !AlertUI_BuzzerHz(&u, 1200));
    AlertUI_Update(&u, 1300, 0, 0, 1);
    u.sos_pattern = 1;
    CHECK(AlertUI_BuzzerHz(&u, 1300));
    CHECK(!AlertUI_BuzzerHz(&u, 1450));
    CHECK(AlertUI_BuzzerHz(&u, 2300)); /* First dash lasts 300 ms. */
    CHECK(!AlertUI_BuzzerHz(&u, 2450));
    CHECK(AlertUI_BuzzerHz(&u, 16750)); /* Escalation overrides Morse. */
    CHECK(AlertUI_BuzzerHz(&u, 1300) == 2093U);
    CHECK(AlertUI_BuzzerHz(&u, 2300) == 2093U);
    CHECK(AlertUI_BuzzerHz(&u, 16300) == 1568U);
    CHECK(AlertUI_BuzzerHz(&u, 16500) == 2093U);
    CHECK(AlertUI_BuzzerHz(&u, 16700) == 2637U);
    u = (AlertUI){.tuning=1, .tune_since=100};
    CHECK(AlertUI_BuzzerHz(&u, 100) == 1047U);
    CHECK(AlertUI_BuzzerHz(&u, 180) == 0U);
    CHECK(AlertUI_BuzzerHz(&u, 260) == 1319U);
    CHECK(AlertUI_BuzzerHz(&u, 420) == 1568U);
    CHECK(AlertUI_BuzzerHz(&u, 820) == 2093U);
    CHECK(AlertUI_BuzzerHz(&u, 900) == 0U);
    AlertUI_Update(&u, 500, 0, 0, 1); /* Alarm preempts a melody. */
    CHECK(!u.tuning && AlertUI_BuzzerHz(&u, 500) == 1568U);
    AlertUI_Update(&u, 1000, 0, 0, 0);
    CHECK(AlertUI_BuzzerHz(&u, 1000) == 2093U);
    CHECK(AlertUI_BuzzerHz(&u, 1100) == 0U);
    CHECK(AlertUI_BuzzerHz(&u, 1120) == 1568U);
    CHECK(AlertUI_BuzzerHz(&u, 1240) == 1047U);
    CHECK(AlertUI_BuzzerHz(&u, 1340) == 0U);
    u = (AlertUI){.tuning=1, .tune_since=UINT32_MAX-99U};
    CHECK(AlertUI_BuzzerHz(&u, 60) == 1319U); /* Notes survive tick wrap. */
    return 0;
}
