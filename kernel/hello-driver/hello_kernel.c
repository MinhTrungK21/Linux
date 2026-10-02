#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/platform_device.h>


#define GPIO_NUM 28

struct class *hello_kernel_class;
struct device *test_device;
DEFINE_MUTEX(test_lock);

typedef enum{
    LED_OFF=0,
    LED_ON=1
} LED_STATUS;

LED_STATUS led_status=LED_OFF;
ssize_t (test_show)(struct device *dev, struct device_attribute *attr,char *buf)
{
    mutex_lock(&test_lock);
    sprintf(buf,"LED status is : %s\n",(led_status==LED_ON)?"ON":"OFF");
    printk("[DRIVER_Iris]: done read sys/class/hello-kernel/test\n");
    mutex_unlock(&test_lock);
    return strlen(buf);
}
static struct gpio_desc *g;
ssize_t (test_store)(struct device *dev, struct device_attribute *attr,const char *buf, size_t count)
{
    mutex_lock(&test_lock);
    printk("[DRIVER_Iris]: done write [%s] to sys/class/hello-kernel/device0/test\n",buf);
    if (strstr(buf,"on"))
    {
        /* code */
        led_status=LED_ON;
        gpiod_set_value_cansleep(g, 1);
    }
    else if (strstr(buf,"off"))
    {
        /* code */
        led_status=LED_OFF;
        gpiod_set_value_cansleep(g, 0);
    };
 
    mutex_unlock(&test_lock);
    return count;

}
static DEVICE_ATTR_RW(test);
#define HELLO_LED_DTB_PATH "/hello_led"




int hello_driver_init(struct platform_device *pdev){
        struct device_node *np = of_find_node_by_path(HELLO_LED_DTB_PATH);
    if (!np) {
        printk("[Driver_Iris]: Dont found %s \n",(HELLO_LED_DTB_PATH));
        return -1;
    }
    g = gpiod_get_from_of_node(np, "gpios",0,GPIOD_OUT_LOW,"hello_led");
    of_node_put(np);
    if (IS_ERR(g)){
        printk("[Driver_Iris]: gpiod_get_from_of_node failed: %ld\n", PTR_ERR(g));
        return PTR_ERR(g);
    }
    gpiod_set_value_cansleep(g,0);
    printk("[Driver_Iris]: LOAD OK");
    hello_kernel_class=class_create(THIS_MODULE,"hello-kernel");
    if (IS_ERR(hello_kernel_class)){
        printk("[Driver_Iris]: create hello_kernel_class failed \n");
        return -1;
    }


    test_device= device_create(hello_kernel_class,NULL,MKDEV(0,0),NULL,"device0");
    if (IS_ERR(test_device)){
        printk("DRIVER_Iris]: create device failed \n");
        class_destroy(hello_kernel_class);
        return -1;
    }

     
    int ret = device_create_file(test_device, &dev_attr_test);
    if (ret){
        printk("DRIVER_Iris]: create test file failed\n");
        device_destroy(hello_kernel_class,MKDEV(0,0));
        class_destroy(hello_kernel_class);

    }
    return 0;
}
int hello_driver_exit(struct platform_device *pdev){
    printk("[DRIVER_Iris]: Exit\n");
    device_remove_file(test_device, &dev_attr_test);
    device_destroy(hello_kernel_class,MKDEV(0,0));
    class_destroy(hello_kernel_class);
    return 0;
}
struct of_device_id hello_of_driver[]={
    {.compatible="hello-led"},
    {},
};
static struct platform_driver hello_platform_driver={
    .driver={
        .name="hello-driver",
        .of_match_table=hello_of_driver,
        .owner=THIS_MODULE,
    },
    .probe=hello_driver_init,
    .remove=hello_driver_exit,
};
// module_init(hello_init);
// module_exit(hello_exit);
module_platform_driver(hello_platform_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("IRIS");
MODULE_VERSION("1.0");

