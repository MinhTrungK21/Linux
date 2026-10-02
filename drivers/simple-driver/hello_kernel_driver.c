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
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#define GPIO_NUM 28
#define HELLO_MAGIC 'H'
#define IOCTL_LED_SET _IOW(HELLO_MAGIC,1,int)
#define IOCTL_LED_GET _IOR(HELLO_MAGIC,2,int)
struct gpio_desc *g;
struct class *hello_kernel_class;
struct device *test_device;
struct device* hello_device;
DEFINE_MUTEX(test_lock);
struct class* hello_class;


typedef enum{
    LED_OFF,
    LED_ON
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

ssize_t (test_store)(struct device *dev, struct device_attribute *attr,const char *buf, size_t count)
{
    mutex_lock(&test_lock);
    printk("[DRIVER_Iris]: done write [%s] to sys/class/hello-kernel/device0/test\n",buf);
    if (strstr(buf,"on"))
    {
        /* code */
        led_status=LED_ON;
    }
    else if (strstr(buf,"off"))
    {
        /* code */
        led_status=LED_OFF;
    };
    mutex_unlock(&test_lock);
    return count;

}

//static DEVICE_ATTR_RW(test);
#define HELLO_LED_DTB_PATH "/hello_led"
//static struct gpio_desc *g;

dev_t cdev;
struct cdev dev;
ssize_t hello_read(struct file *fp, char __user *buf, size_t size, loff_t *offset){
    printk("[HELLO_KERNEL_DRIVER]hello_read called\n");
    return 0;
}
ssize_t hello_write(struct file *fp, const char __user *buf, size_t size, loff_t *offset){
    printk("[HELLO_KERNEL_DRIVER]hello_write called\n");
    return size;
}
long (hello_ioctl)(struct file *fp, unsigned int cmd, unsigned long data){
    printk("[HELLO_KERNEL_DRIVER]hello_ioctl called cmd %d data 0x%.8lx\n",cmd, data);
    int val;
    switch (cmd)
    {
case IOCTL_LED_SET:
    if (*(int *)data == 0) {
        gpiod_set_value_cansleep(g, 0); // tắt LED
        led_status = LED_OFF;
    } else {
        gpiod_set_value_cansleep(g, 1); // bật LED
        led_status = LED_ON;
    }
    printk("[HELLO_KERNEL_DRIVER]LED set to %s\n", (led_status==LED_OFF)?"OFF":"ON");
    break;

    case IOCTL_LED_GET:
        val = gpiod_get_value_cansleep(g);
        copy_to_user((int __user *)data, &val, sizeof(val));
        printk("[HELLO_KERNEL_DRIVER]LED GET is %d\n", val);
        // printk("[HELLO_KERNEL_DRIVER]LED GET is %s\n",(led_status==LED_OFF)?"OFF":"ON");
        // char* temp = data;// data is pointer from user space of led status
        // *temp = led_status;// write led status to user space is located at data
        // copy_to_user((char __user *)data, &led_status, sizeof(led_status));
        break;
    }
    return 0;
}
const struct file_operations hello_fops={
    .owner=THIS_MODULE,
    .read= hello_read,
    .write= hello_write,
    .unlocked_ioctl= hello_ioctl,
};

//int hello_driver_init(struct platform_device *pdev)

int __init hello_init(void){
    struct device_node *np = of_find_node_by_path(HELLO_LED_DTB_PATH);
    if (!np) {
        printk("[Driver_Iris]: Dont found %s \n",(HELLO_LED_DTB_PATH));
        return -1;
    }
    g = gpiod_get_from_of_node(np, "gpios",0,GPIOD_OUT_LOW,NULL);
    of_node_put(np);
    if (IS_ERR(g)){
        printk("[Driver_Iris]: gpiod_get_from_of_node failed: %ld\n", PTR_ERR(g));
        return PTR_ERR(g);
    }
    gpiod_set_value_cansleep(g,1);
    printk("[Driver_Iris]: LOAD Module OK");
    printk("[HELLO_KERNEL_DRIVER]Loading...\n");
    int ret = alloc_chrdev_region(&cdev,0,1,"hello-driver");
    if (ret<0){
        printk("[HELLO_KERNEL_DRIVER]alloc_chrdev_region failed\n");
        return -1;

    }
    cdev_init(&dev,&hello_fops);
    ret = cdev_add(&dev,cdev,1);
    if (ret<0){
        printk("[HELLO_KERNEL_DRIVER]cdev_add failed\n");
        //unregister_chrdev_region(cdev,1);
        return -1;
    }
    printk("[HELLO_KERNEL_DRIVER]LOAD Module OK\n");
    hello_class = class_create(THIS_MODULE,"hello_class");
    hello_device = device_create(hello_class,NULL,cdev,NULL,"hello_device"); 
    return 0;
}


//int hello_driver_exit(struct platform_device *pdev)

void __exit hello_exit(void){
    printk("[DRIVER_Iris]: Exit\n");

    // gpiod_set_value_cansleep(g,0);
    // gpiod_put(g);
    // device_remove_file(test_device, &dev_attr_test);
    // device_destroy(hello_kernel_class,MKDEV(0,0));
    // class_destroy(hello_kernel_class);
    // return 0;
    device_destroy(hello_class,cdev);
    class_destroy(hello_class);
    unregister_chrdev_region(cdev,1);

    gpiod_set_value_cansleep(g, 0);
    gpiod_put(g);
    printk("[HELLO_KERNEL_DRIVER]Unload Module OK\n");
};
// struct of_device_id hello_of_driver[]={
//     {.compatible="hello-led"},
//     {},
// }
// static struct platform_driver hello_platform_driver={
//     .driver={
//         .name="hello-driver-v2",
//         .of_match_table=hello_of_driver,
//         .owner=THIS_MODULE,
//     },
//     .probe=hello_driver_init,
//     .remove=hello_driver_exit,
// };
module_init(hello_init);
module_exit(hello_exit);
//module_platform_driver(hello_platform_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("IRIS");
MODULE_VERSION("1.0");

