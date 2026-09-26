#include "motion_capture.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int capture_test(void)
{
    MotionSample sample = {0}, rows[CAPTURE_CHUNK];
    CaptureInfo info;
    uint32_t count;
    for (uint32_t i=0; i<300; ++i) {
        sample.time_ms=i*20;
        sample.ar[0]=(int32_t)i;
        MotionCapture_Add(&sample, i==100 ? "manual" : 0, i==250 ? "no_rotation" : 0);
    }
    CHECK(capture_completed==1);
    CHECK(MotionCapture_Read(0,&info,rows,&count));
    CHECK(info.count==300 && info.pre_count==100 && count==3);
    CHECK(rows[0].time_ms==0 && rows[2].ar[0]==2);
    CHECK(MotionCapture_Read(99,&info,rows,&count));
    CHECK(rows[2].time_ms==5980 && info.outcome[0]=='n');
    CHECK(!MotionCapture_Read(100,&info,rows,&count));
    for(uint32_t i=300;i<500;++i) {
        sample.time_ms=i*20;
        MotionCapture_Add(&sample,i==300 ? "manual":0,0);
    }
    CHECK(capture_completed==2);
    MotionCapture_Add(&sample,"manual",0);
    CHECK(capture_dropped==1);
    MotionCapture_Release(1);
    CHECK(capture_uploaded==1);
    CHECK(MotionCapture_Read(0,&info,rows,&count) && info.id==2);
    CHECK(rows[0].time_ms==4000);
    MotionCapture_Release(2);
    CHECK(!MotionCapture_Read(0,&info,rows,&count));
    return 0;
}
