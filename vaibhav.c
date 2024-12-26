/*
 * To implement a dynamic circular queue in linux char device
 * which takes data from IOCTL calls.
 *
 * IOCTL Calls to implement:
 *
 * SET_SIZE_OF_QUEUE: which takes an integer argument and creates queue according to given size
 * PUSH_DATA: passing a structure which contains data and it's length, and push the data of given length
 * POP_DATA: passing a structure same as above and just pass the length, while popping data in the structure can be random.
 */

// lets start with including various header files

#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/fs.h>

// define module name
#define MODULE_NAME 	"vaibhav"

// define IOCTL operations
#define SET_SIZE_OF_QUEUE _IOW('a', 'a', int * )
#define PUSH_DATA _IOW('a', 'b', struct data * )
#define POP_DATA _IOR('a', 'c', struct data * )

// lets add module authors, description and lisence
MODULE_DESCRIPTION("A simple char device taking ioctl commands set_size, push and pop.\n");
MODULE_AUTHOR("vaibhav");
MODULE_LICENSE("GPL");

struct vaibhav_device_data {
	struct cdev cdev;
	dev_t devt;
};

struct data {
	int length;
	char *data;
};

struct vaibhav_device_data device_data;
static struct class *vaibhav_class;
static struct device *vaibhav_device;

// queue
static int queue_size = 0;
static char *queue = NULL;
static int head = 0, tail = 0;
static int count = 0;
static wait_queue_head_t read_queue;
static struct mutex lock;

// let us create the ioctl function
static long vaibhav_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
	struct data user_data;
	int temp_length = 0;
	char *temp_buffer;
	int i;

	// lets create a mutex lock so only one program can access device at a time
	mutex_lock(&lock);

	switch(cmd) {
		case SET_SIZE_OF_QUEUE:
			// receive the integer object
			if (copy_from_user(&temp_length, (int *) arg, sizeof(int))) {
				//  if copy fails, unlock mutex and exit
				mutex_unlock(&lock);

				return -EFAULT;
			}
			// check if temp_length is invalid
			if (temp_length <= 0) {
				mutex_unlock(&lock);
				return -EINVAL;
			}
			// check if queue is already present/alloted some address
			if (queue) {
				kfree(queue);
				queue = NULL;
			}
			queue_size = temp_length;
			queue = kmalloc(queue_size * sizeof(char), GFP_KERNEL);
			// if not alloted 
			if (!queue) {
				mutex_unlock(&lock);
				return -ENOMEM;
			}

			// set head, tail and count
			head = 0;
			tail = 0;
			count = 0;
			break;

		case PUSH_DATA:
			// receive struct data from user space
			if (copy_from_user(&user_data, (struct data *) arg, sizeof(struct data))) {
				mutex_unlock(&lock);
				return -EFAULT;
			}

			// check if queue is alloted space 
			if (!queue) {
				mutex_unlock(&lock);
				return -ENOMEM;
			}

			// check if queue has enough space
			// this also checks for invalid user_data.lenght values
			if (user_data.length > queue_size - count) {
				mutex_unlock(&lock);
				return -ENOMEM;
			}

			// allocate space to temp_buffer and copy data from
			// temp_buffer to queue
			temp_buffer = kmalloc(user_data.length, GFP_KERNEL);
			if (!temp_buffer) {
				mutex_unlock(&lock);
				return -ENOMEM;
			}

			// I was not copying data from user at all
			if (copy_from_user(temp_buffer, user_data.data, user_data.length)) {
				kfree(temp_buffer);
				mutex_unlock(&lock);
				return -EFAULT;
			}
			
			// copy data from temp_buffer to queue
			for (i=0; i< user_data.length; i++) {
				queue[tail] = temp_buffer[i];
				tail = (tail + 1) % queue_size;
				count++;
			}

			printk(KERN_INFO "temp_buffer: %s.\n", temp_buffer);
			printk(KERN_INFO "head: %d, tail: %d, count: %d\n", head, tail, count);


			// release resources
			kfree(temp_buffer);
			wake_up_interruptible(&read_queue);
			break;

		case POP_DATA:
			if (copy_from_user(&user_data, (struct data *) arg, sizeof(struct data))) {
				mutex_unlock(&lock);
				return -EFAULT;
			}

			// wait till there is enough space in queue if not enough data to pop
			mutex_unlock(&lock);
			if (wait_event_interruptible(read_queue, count >= user_data.length)) {
				return -EFAULT;
			}
			mutex_lock(&lock);

			// allcate space to temp_buffer and pop data to it.
			temp_buffer = kmalloc(user_data.length, GFP_KERNEL);
			if (!temp_buffer) {
				mutex_unlock(&lock);
				return -ENOMEM;
			}

			// pop data to temp_buffer
			for (i=0; i< user_data.length; i++) {
				temp_buffer[i] = queue[head];
				head = (head + 1) % queue_size;
				count--;
			}
			if (copy_to_user(user_data.data, temp_buffer, user_data.length)) {
				// unallocate resources
				kfree(temp_buffer);

				mutex_unlock(&lock);
				return -EFAULT;
			}

			printk(KERN_INFO "temp_buffer: %s.\n", temp_buffer);
			printk(KERN_INFO "head: %d, tail: %d, count: %d\n", head, tail, count);

			kfree(temp_buffer);
			break;
			
		default:
			mutex_unlock(&lock);
			return -EINVAL;
	}

	mutex_unlock(&lock);
	return 0;
}

static const struct file_operations vaibhav_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = vaibhav_ioctl
};

// lets start with init function

static int vaibhav_init(void) {
	// to store return values of certain commands
	int ret;

	// start with allocating chrdev region and adding it
	// this function will atomatically allot us a MAJOR
	ret = alloc_chrdev_region(&device_data.devt, 0, 1, MODULE_NAME);
	// handle errors in above
	if (ret < 0) {
		printk(KERN_ALERT "Error: unable to allocate MAJOR.\n");
		return ret;
	}
	// initilizing cdev
	cdev_init(&device_data.cdev, &vaibhav_fops);

	// adding cdev
	ret = cdev_add(&device_data.cdev, device_data.devt, 1);
	if (ret < 0) {
		printk(KERN_ALERT "Error: unable to add device.\n");

		// unallocate resources
		unregister_chrdev_region(device_data.devt, 1);

		return ret;
	}

	// now let us create a class
	vaibhav_class = class_create(THIS_MODULE, MODULE_NAME);
	if (IS_ERR(vaibhav_class)) {
		// we are usign IS_ERR to handle errors because our vaibhav_class is a pointer
		// and for pointer type returns it returns it such that we can detect errors using 
		// IS_ERR()

		printk(KERN_ALERT "Error: unable to create class.\n");

		// unallocate any resources 
		cdev_del(&device_data.cdev);
		unregister_chrdev_region(device_data.devt, 1);
		
		return PTR_ERR(vaibhav_class);
	}

	// let's create a device
	vaibhav_device = device_create(vaibhav_class, NULL, device_data.devt, NULL, MODULE_NAME);
	if (IS_ERR(vaibhav_device)) {
		// unallocate any resources
		class_destroy(vaibhav_class);
		cdev_del(&device_data.cdev);
		unregister_chrdev_region(device_data.devt, 1);

		return PTR_ERR(vaibhav_device);
	}

	// initilize the mutex lock
	mutex_init(&lock);

	// initilize wait queue
	init_waitqueue_head(&read_queue);

	printk(KERN_INFO "Successfully initilized vaibhav module.\n");
	return 0;
}

static void vaibhav_exit(void) {
	// unallocate all resources in reverse order of assignment
	
	if (queue) {
		kfree(queue);
		queue = NULL;
	}
	
	device_destroy(vaibhav_class, device_data.devt);
	class_destroy(vaibhav_class);
	cdev_del(&device_data.cdev);
	unregister_chrdev_region(device_data.devt, 1);

	printk(KERN_INFO "Successfully exited vaibhav moduel.\n");
}

module_init(vaibhav_init);
module_exit(vaibhav_exit);
