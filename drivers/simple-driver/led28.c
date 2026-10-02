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


struct class *hello_kernel_class;
struct device *test_device;
DEFINE_MUTEX(test_lock);

typedef enum{
    LEd_OFF=0,
    LED_ON=1
} LED_STATUS;

LED_STATUS led_status=LEd_OFF;
ssize_t (test_show)(struct device *dev, struct device_attribute *attr,char *buf)
{
    mutex_lock(&test_lock);
    sprintf(buf,"LED status is : %s\n",(led_status==LED_ON)?"ON":"OFF");
    printk("[DRIVER_Iris]: done read sys/class/helllo-kernel/test\n");
    mutex_unlock(&test_lock);
    return strlen(buf);
}

ssize_t (test_store)(struct device *dev, struct device_attribute *attr,const char *buf, size_t count)
{
    mutex_lock(&test_lock);
    printk("[DRIVER_Iris]: done write [%s] to sys/class/helllo-kernel/test\n",buf);
    if (strstr(buf,"on"))
    {
        /* code */
        led_status=LED_ON;
    }
    else if (strstr(buf,"off"))
    {
        /* code */
        led_status=LEd_OFF;
    };
 
    mutex_unlock(&test_lock);
    return count;
    
}

DEVICE_ATTR_RW(test);
#define HELLO_LED_DTB_PATH "/hello_led"
struct gpio_desc *g;
int __init hello_init(void){
    int rets=gpio_request(GPIO_NUM,"mygpio");
    gpio_direction_output(GPIO_NUM,0);
    gpio_set_value_cansleep(GPIO_NUM,0);
    if (rets){
        printk("[DRIVER_Iris]: request gpio28 failed[error code: %d]\n", rets  );
        return -1;
    }
    struct device_node *np = of_find_node_by_path(HELLO_LED_DTB_PATH);
    if (!np) {
        printk("[Driver_Iris]: Dont found %s \n",(HELLO_LED_DTB_PATH));
        return -1;
    }
    g = gpiod_get_from_of_node(np, "gpios",0,GPIOD_OUT_LOW,NULL);
    of_node_put(np);
    if (IS_ERR(g)){
        printk("[Driver_Iris]: gpiod_get_from_of_node failed: %ld\n", PTR_ERR(g));
        return -1;
    }
    gpiod_set_value_cansleep(g,1);
    printk("[Driver_Iris]: LOAD OK");
    hello_kernel_class=class_create(THIS_MODULE,"hello-kernel");
    if (IS_ERR(hello_kernel_class)){
        printk("[Driver_Iris]: vreate hello_kernel_class failed \n");
        return -1;
    }

    int rets =  gpio_request(28,"mygpio");
    gpio_derection_output(28,0);

    if (rets){
        printk("[DRIVER_Iris]: request gpio28 failed[error code: %d]\n", rets  );
        return -1;
    }


    printk("[DRIVER_Iris]: Load\n");
    hello_kernel_class=class_create(THIS_MODULE,"hello-kernel");
    if (IS_ERR(hello_kernel_class)){
        printk("[DRIVER_Iris]: create driver-kernel failed \n");
        return -1;
    };

    test_device= device_create(hello_kernel_class,NULL,MKDEV(0,0),NULL,"device0");
    if (IS_ERR(test_device)){
        printk("DRIVER_Iris]: create device failed \n");
        class_destroy(hello_kernel_class);
        return -1;
    };

     
    int ret = device_create_file(test_device, &dev_attr_test);
    if (ret){
        printk("DRIVER_Iris]: create test file failed\n");
        device_destroy(hello_kernel_class,MKDEV(0,0));
        class_destroy(hello_kernel_class);

    };
    return 0;
}


void __exit hello_exit(void){

    printk("[DRIVER_Iris]: Exit\n");
    gpio_free(GPIO_NUM); 
    device_remove_file(test_device, &dev_attr_test);
    device_destroy(hello_kernel_class,MKDEV(0,0));
    class_destroy(hello_kernel_class);

}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IRIS");
MODULE_VERSION("1.0");

module_init(hello_init);
module_exit(hello_exit);