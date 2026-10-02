#include <stdio.h>
#include <linux/gpio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../../ti-sdk-kernel/linux-devkit/sysroots/armv7at2hf-neon-oe-linux-gnueabi/usr/include/linux/gpio.h"

#define dev_name "/dev/hello_device"

int main()
{
    printf("Start: CUSTOM DRIVER\n");
    int gpio_fp=open("/dev/hello_device", O_RDWR);
    int res=0;
    if (gpio_fp<0)
    {
        printf("Open GPIO_chip0 failse(error %d)\n",dev_name,gpio_fp);
        return -1;
    };
    struct gpio_v2_line_request gpio_line_req = {
        .offsets={28},
        .consumer="LED_DEMO",
        .config=
        {
            .flags=GPIO_V2_LINE_FLAG_OUTPUT,
        },
    .num_lines=1
    };   
    res = ioctl(gpio_fp,1,1);
    if(res<0){
        printf("IOCTL_GPIO_V2_GET_LINE_IOCTL failed: error %d\n",res);
        goto X;
        return -1;
    }
    struct gpio_v2_line_values line_val = 
    {
        .bits=1,
        .mask=1
    };
    
    res=ioctl(gpio_line_req.fd,GPIO_V2_LINE_SET_VALUES_IOCTL,&line_val);
    if(res<0){
        printf("GPIO_V2_LINE_SET_VALUES_IOCTL failed\n");
        goto Y;
        return -1;
    }
    sleep(5);
Y:  close (gpio_line_req.fd);
X:  close (gpio_fp);

    return res;

}