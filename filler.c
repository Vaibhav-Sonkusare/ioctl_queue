#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>

#define DRIVER_NAME "/dev/vaibhav"
#define PUSH_DATA _IOW('a', 'b', struct data * )

struct data {
    int length;
    char * data;
};

int main(void) {
    int fd = open(DRIVER_NAME, O_RDWR);
    struct data * d = malloc(sizeof(struct data));
    d->length = 3;
    d->data = malloc(4);
    memcpy(d->data, "xyz", 3);
	d->data[3] = '\0';
	printf("data: %s\n", d->data);
    int ret = ioctl(fd, PUSH_DATA, d);
    close(fd);
    free(d->data);
    free(d);
    return ret;
}


