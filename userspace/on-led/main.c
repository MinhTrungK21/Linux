#include <stdio.h>
#include <unistd.h>
void main() {
        printf("START \n");
        FILE* fp = fopen("/sys/class/leds/beaglebone:green:usr2/brightness", "w");
        if (fp==0) {
                printf("ngu \n");
                return;

        };
        printf("OK \n");
        fputs("1", fp);
        fclose(fp);
	sleep(3);

}
