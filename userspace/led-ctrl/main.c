#include <stdio.h>
#include <linux/gpio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../../ti-sdk-kernel/linux-devkit/sysroots/armv7at2hf-neon-oe-linux-gnueabi/usr/include/linux/gpio.h"
#define HELLO_MAGIC 'H'
#define IOCTL_LED_SET _IOW(HELLO_MAGIC,1,int)
#define IOCTL_LED_GET _IOR(HELLO_MAGIC,2,int)
#define dev_name "/dev/hello_device"

int main()
{
    printf("Start: CUSTOM DRIVER\n");
    int gpio_fp=open("/dev/hello_device", O_RDWR);
    int res=0;
    if (gpio_fp<0)
    {
        printf("Open %s GPIO_chip0 failse(error %d)\n",dev_name,gpio_fp);
        return -1;
    };
  
    int led_stt=0; //user space variable to get led status
    printf("Led_stt address: 0x%p\n",&led_stt);
    res = ioctl(gpio_fp,IOCTL_LED_GET,&led_stt);

    if(res<0){
        printf("IOCTL_GPIO_V2_GET_LINE_IOCTL failed: error %d\n",res);
        close (gpio_fp);
        return -1;
    }
    printf("Current LED status: %d (%s)\n", led_stt, (led_stt ? "ON" : "OFF"));
    led_stt=1;
    res = ioctl(gpio_fp,IOCTL_LED_SET,&led_stt);
    if(res<0){
        printf("SSET LED ON faild");
        return -1;
    }
    else {
        printf("SET LED ON success\n");
    }
    sleep(2);
    led_stt=0;
    res = ioctl(gpio_fp,IOCTL_LED_SET,&led_stt);
    if(res<0){
        printf("SET LED OFF faild");
        return -1;
    }
    else {
        printf("SET LED OFF success\n");
    }
    printf("FNISH: led_stt %d\n",led_stt);
    close(gpio_fp);
    return 0;
}