#include "uinput.h"
#include "eventlists/eventlist.h"
#include "string.h"
#include "output_slot.h"
#include <sys/epoll.h>


const char* try_to_find_uinput() {
  static const char* paths[] = {
    "/dev/uinput",
    "/dev/input/uinput",
    "/dev/misc/uinput"
  };
  const int num_paths = 3;
  int i;

  for (i = 0; i < num_paths; i++) {
    if (access(paths[i], F_OK) == 0) {
      return paths[i];
    }
  }
  return nullptr;
}

#define UI_GET_SYSNAME(len)     _IOC(_IOC_READ, UINPUT_IOCTL_BASE, 44, len)

std::string uinput_devnode(int fd) {
  char buffer[128];
  memset(buffer,0,sizeof(buffer));
  ioctl(fd, UI_GET_SYSNAME(127), &buffer);
  buffer[127] = '\0';
  if (buffer[0] == '\0')
    return "";
  return std::string("/sys/devices/virtual/input/") + std::string(buffer);
}

void uinput::uinput_destroy(int fd) {
  std::lock_guard<std::mutex> guard(lock);
  int ret = ioctl(fd, UI_DEV_DESTROY);
  close(fd);
  ff_slots.erase(fd);
  if (ret < 0)
    perror("ui_destroy");
}

uinput::uinput() {
  filename = try_to_find_uinput();
  if (filename == nullptr) throw - 1;

  epfd = -1;
  ff_thread = nullptr;

}

uinput::~uinput() {
  if (epfd >= 0)
    close(epfd);
  if (ff_thread) {
    ff_thread->detach();
  }
}

int uinput::make_gamepad(const uinput_ids& ids, bool dpad_as_hat, bool analog_triggers, bool rumble) {
  static int abs[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY};
  static int key[] = { BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_SELECT, BTN_MODE, BTN_START, BTN_TL, BTN_TR, BTN_THUMBL, BTN_THUMBR, -1};
  struct uinput_user_dev uidev;
  int fd;
  int i;
  int mode = O_WRONLY;
  if (rumble)
    mode = O_RDWR;
  fd = open(filename, mode | O_NONBLOCK);
  if (fd < 0) {
    perror("open uinput");
    return -1;
  }
  memset(&uidev, 0, sizeof(uidev));
  snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, ids.device_string.c_str());
  uidev.id.bustype = BUS_USB;
  uidev.id.vendor = ids.vendor_id;
  uidev.id.product = ids.product_id;
  uidev.id.version = ids.version_id;

  ioctl(fd, UI_SET_EVBIT, EV_ABS);
  for (i = 0; i < 4; i++) {
    ioctl(fd, UI_SET_ABSBIT, abs[i]);
    uidev.absmin[abs[i]] = -32768;
    uidev.absmax[abs[i]] = 32767;
    uidev.absflat[abs[i]] = 1024;
  }

  if (analog_triggers) {
    ioctl(fd, UI_SET_ABSBIT, ABS_Z);
    uidev.absmin[ABS_Z] = 0;
    uidev.absmax[ABS_Z] = 255;
    uidev.absflat[ABS_Z] = 0;
    ioctl(fd, UI_SET_ABSBIT, ABS_RZ);
    uidev.absmin[ABS_RZ] = 0;
    uidev.absmax[ABS_RZ] = 255;
    uidev.absflat[ABS_RZ] = 0;
  }

  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  for (i = 0; key[i] >= 0; i++) {
    ioctl(fd, UI_SET_KEYBIT, key[i]);
  }

  if (dpad_as_hat) {
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0X);
    uidev.absmin[ABS_HAT0X] = -1;
    uidev.absmax[ABS_HAT0X] = 1;
    uidev.absflat[ABS_HAT0X] = 0;
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0Y);
    uidev.absmin[ABS_HAT0Y] = -1;
    uidev.absmax[ABS_HAT0Y] = 1;
    uidev.absflat[ABS_HAT0Y] = 0;
  } else {
    ioctl(fd, UI_SET_KEYBIT, BTN_DPAD_UP);
    ioctl(fd, UI_SET_KEYBIT, BTN_DPAD_DOWN);
    ioctl(fd, UI_SET_KEYBIT, BTN_DPAD_LEFT);
    ioctl(fd, UI_SET_KEYBIT, BTN_DPAD_RIGHT);
  }
  if (!analog_triggers) {
    ioctl(fd, UI_SET_KEYBIT, BTN_TL2);
    ioctl(fd, UI_SET_KEYBIT, BTN_TR2);
  }

  if (rumble) {
    ioctl(fd, UI_SET_EVBIT, EV_FF);
    ioctl(fd, UI_SET_FFBIT, FF_RUMBLE);
    uidev.ff_effects_max = 1;
  }
  ioctl(fd, UI_SET_PHYS, "moltengamepad");


  write(fd, &uidev, sizeof(uidev));
  if (ioctl(fd, UI_DEV_CREATE) < 0)
    perror("uinput device creation");

  lock.lock();
  virtual_nodes.push_back(uinput_devnode(fd));
  lock.unlock();

  return fd;
}


int uinput::make_keyboard(const uinput_ids& ids) {
  struct uinput_user_dev uidev;
  int fd;
  int i;


  fd = open(filename, O_WRONLY | O_NONBLOCK);
  if (fd < 0) {
    perror("\nopen uinput");
    return -1;
  }
  memset(&uidev, 0, sizeof(uidev));
  snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, ids.device_string.c_str());
  uidev.id.bustype = BUS_USB;
  uidev.id.vendor = ids.vendor_id;
  uidev.id.product = ids.product_id;
  uidev.id.version = ids.version_id;




  /*Just set all possible keys up to BTN_TASK
   * This should cover all reasonable keyboard and mouse keys.*/
  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  for (i = KEY_ESC; i <= BTN_TASK; i++) {
    ioctl(fd, UI_SET_KEYBIT, i);
  }
  for (i = KEY_OK; i < KEY_BRIGHTNESS_MAX; i++) {
    ioctl(fd, UI_SET_KEYBIT, i);
  }

  //Set BTN_TOUCH so joydev thinks this a touchscreen.
  ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);

  /*Set basic mouse events*/
  static int abs[] = { ABS_X, ABS_Y};

  ioctl(fd, UI_SET_EVBIT, EV_ABS);
  for (i = 0; i < 2; i++) {
    ioctl(fd, UI_SET_ABSBIT, abs[i]);
    uidev.absmin[abs[i]] = -32768;
    uidev.absmax[abs[i]] = 32767;
  }

  ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
  ioctl(fd, UI_SET_PHYS, "moltengamepad");

  write(fd, &uidev, sizeof(uidev));
  if (ioctl(fd, UI_DEV_CREATE) < 0)
    perror("uinput device creation");

  lock.lock();
  virtual_nodes.push_back(uinput_devnode(fd));
  lock.unlock();

  return fd;
}

int uinput::make_mouse(const uinput_ids& ids) {
  struct uinput_user_dev uidev;
  int fd;
  int i;


  fd = open(filename, O_WRONLY | O_NONBLOCK);
  if (fd < 0) {
    perror("\nopen uinput");
    return -1;
  }
  memset(&uidev, 0, sizeof(uidev));
  snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, ids.device_string.c_str());
  uidev.id.bustype = BUS_USB;
  uidev.id.vendor = ids.vendor_id;
  uidev.id.product = ids.product_id;
  uidev.id.version = ids.version_id;




  //Set Mouse buttons
  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  for (i = BTN_MOUSE; i <= BTN_MIDDLE; i++) {
    ioctl(fd, UI_SET_KEYBIT, i);
  }

  /*Set basic mouse events*/
  static int rel[] = { REL_X, REL_Y};

  ioctl(fd, UI_SET_EVBIT, EV_REL);
  for (i = 0; i < 2; i++) {
    ioctl(fd, UI_SET_RELBIT, rel[i]);
  }

  ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
  ioctl(fd, UI_SET_PHYS, "moltengamepad");

  write(fd, &uidev, sizeof(uidev));
  if (ioctl(fd, UI_DEV_CREATE) < 0)
    perror("uinput device creation");

  lock.lock();
  virtual_nodes.push_back(uinput_devnode(fd));
  lock.unlock();

  return fd;
}

bool uinput::node_owned(const std::string& path) const {
  lock.lock();
  for (auto node : virtual_nodes) {
    //Check to see if node is a prefix of this path, if so the path belongs to us.
    auto comp = std::mismatch(node.begin(),node.end(),path.begin());
    if (comp.first == node.end()) {
      lock.unlock();
      return true;
    }
  }
  lock.unlock();
  return false;
}

int uinput::setup_epoll() {
  epfd = epoll_create(1);
  if (epfd < 0) perror("epoll create");
  return (epfd < 0);
}

int uinput::watch_for_ff(int fd, output_slot* slot) {
  std::lock_guard<std::mutex> guard(lock);
  if (epfd < 0)
    setup_epoll();
  if (fd <= 0) return -1;
  struct epoll_event event;
  memset(&event, 0, sizeof(event));

  event.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
  event.data.fd = fd;
  int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
  if (ret < 0) perror("epoll add");

  ff_slots[fd] = slot;

  return (ret < 0);
}

int uinput::start_ff_thread() {
  keep_looping = true;
  ff_thread = new std::thread(&uinput::ff_thread_loop, this);
  return 0;
}



void uinput::ff_thread_loop() {
  if (epfd < 0)
    return;
  struct epoll_event event;
  struct epoll_event events[1];
  memset(&event, 0, sizeof(event));

  while ((keep_looping)) {
    int n = epoll_wait(epfd, events, 1, 1000);
    if ((n < 0 && errno == EINTR) || n == 0) {
      continue;
    }
    if (n < 0 && errno != EINTR) {
      perror("epoll wait:");
      break;
    }
    int uinput_fd = events[0].data.fd;
    //Read the event.
    struct input_event ev;
    int ret;
    ret = read(uinput_fd, &ev, sizeof(ev));
    if (ret != sizeof(ev)) {
      if (ret < 0 && errno != EAGAIN && errno != EINTR)
        perror("read_ff");
      return;
    } else {
      std::lock_guard<std::mutex> guard(lock);
      auto listener = ff_slots.find(uinput_fd);
      output_slot* slot = nullptr;
      if (listener != ff_slots.end())
        slot = listener->second;

      if (ev.type == EV_UINPUT && ev.code == UI_FF_UPLOAD) {
        struct uinput_ff_upload effect;
        memset(&effect,0,sizeof(effect));
        effect.request_id = ev.value;

        ioctl(uinput_fd, UI_BEGIN_FF_UPLOAD, &effect);
        if (slot) {
          int id = slot->upload_ff(effect.effect);
          int retval = (id < 0); //fail if id < 0
          effect.effect.id = id;
        } else {
          //no slot? Just say this upload fails.
          effect.effect.id = -1;
          effect.retval = -1;
        }

        ioctl(uinput_fd, UI_END_FF_UPLOAD, &effect);
      }
      if (ev.type == EV_UINPUT && ev.code == UI_FF_ERASE) {
        struct uinput_ff_erase effect;
        memset(&effect,0,sizeof(effect));
        effect.request_id = ev.value;

        ioctl(uinput_fd, UI_BEGIN_FF_ERASE, &effect);
        if (slot) {
          slot->erase_ff(effect.effect_id);
        }
        ioctl(uinput_fd, UI_END_FF_ERASE, &effect);
      }
      if (ev.type == EV_FF) {
        if (slot) {
          slot->play_ff(ev.code, ev.value);
        }
      }
    }
  }
}



