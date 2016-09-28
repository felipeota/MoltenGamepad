#include "network_device.h"

manager_methods network_device_manager::methods;

network_device_manager::network_device_manager() {

}

int network_device_manager::start() {
  //need to do a thread for hot plugs, since these don't go through udev.
  keep_scanning = true;

  nd_context_thread = new std::thread([&] () {
    gamepad = new network_gamepad();
    methods.add_device(ref, nddev, gamepad);
  });
  return 0;
}



void network_device_manager::init_profile() {
  //Init some event translators

  const event_decl* ev = &nd_events[0];
  for (int i = 0; ev->name && *ev->name; ev = &nd_events[++i]) {
    methods.register_event(ref, *ev);
  }

  auto set_alias = [&] (const char* external, const char* internal) {
    methods.register_alias(ref, external, internal);
  };

  //Init some aliases to act like a standardized game pad
  set_alias("primary","a");
  set_alias("secondary","b");
  set_alias("third","x");
  set_alias("fourth","y");
  set_alias("leftright","left_pad_x");
  set_alias("updown","left_pad_y");
  set_alias("start","forward");
  set_alias("select","back");
  set_alias("thumbl","stick_click");
  set_alias("thumbr","right_pad_click");
  set_alias("left_x","stick_x");
  set_alias("left_y","stick_y");
  set_alias("right_x","right_pad_x");
  set_alias("right_y","right_pad_y");

};

