#include <MiniFB.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    printf("This is a test\n");
    struct mfb_timer *timer = mfb_timer_create();
    mfb_timer_reset(timer);
    sleep(2);
    printf("time: %f\n", mfb_timer_delta(timer));

    return 0;
}