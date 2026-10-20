// SPDX-License-Identifier: BSD-3-Clause

// Copyright 2019 GoldenRiver Technology Co., Ltd.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/device/grt-gpio.h>
#include <lib/zx/vmo.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <threads.h>

#define INPUT_PIN (147)
static volatile int g_running = 1;
int fd = -1;
static uint32_t pin = INPUT_PIN;

void *gpio_poll_thread(void *arg) {
    uint32_t level;

    while (g_running) {
        ioctl_grt_get_gpio_level(fd, &pin, &level);
        printf("[Thread] GPIO Level: %u\n", level);
        sleep(1);
    }

    return NULL;
}

int main(int argc, char **argv) {
    pthread_t thread;
    uint32_t enable_irq = 1;

    if ((fd = open(GRT_GPIO_CONTROL_DEVICE, O_RDWR)) < 0) {
        printf("gpio: failed to open dev %s: %d\n", GRT_GPIO_CONTROL_DEVICE, fd);
        return -1;
    }

    ioctl_grt_gpio_enable_irq(fd, &enable_irq);

    if (pthread_create(&thread, NULL, gpio_poll_thread, NULL) != 0) {
        perror("Failed to create thread");
        close(fd);
        return -1;
    }

    sleep(100);

    g_running = 0;
    pthread_join(thread, NULL);

    if (fd >= 0) {
        close(fd);
        fd = -1;
    }

    return 0;
}
