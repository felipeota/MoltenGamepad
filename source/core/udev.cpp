#include "udev.h"
#include <iostream>
#include <sys/epoll.h>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include "devices/device.h"
#include "uinput.h"

void udev_handler::pass_along_device(struct udev_device* new_dev) {
}



udev_handler::udev_handler() {
}

udev_handler::~udev_handler() {
}

void udev_handler::set_managers(std::vector<device_manager*>* managers) {
}

void udev_handler::set_uinput(const uinput* ui) {
  this->ui = ui;
}

int udev_handler::start_monitor() {

  return 0;
}

int udev_handler::enumerate() {
  return 0;
}


int udev_handler::read_monitor() {

  return 0;
}

int udev_handler::grab_permissions(udev_device* dev, bool grabbed) {
}
