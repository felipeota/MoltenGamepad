#include "network_device.h"
#include <iostream>


device_plugin nddev;





int nd_plugin_init(plugin_api api) {  
  //set static vars
  network_device_manager::methods = api.manager;
  network_gamepad::methods = api.device;
  network_device_manager* manager = new network_device_manager();

  //set manager call backs
  manager_plugin ndman;
  ndman.name = "network device";
  ndman.subscribe_to_gamepad_profile = true;
  ndman.init = [] (void* plug_data, device_manager* ref) -> int {
    return ((network_device_manager*)plug_data)->init(ref);
  };
  ndman.destroy = [] (void* data) -> int {
    delete (network_device_manager*) data;
    return 0;
  };
  ndman.start = [] (void* data) { 
    return ((network_device_manager*)data)->start();
  };
  ndman.process_manager_option = nullptr;
  ndman.process_udev_event = [] (void* data, struct udev* udev, struct udev_device* dev) {
    return -1; //This driver doesn't use udev events!
  };

  //set device call backs
  nddev.name_stem = "nd";
  nddev.uniq = "";
  nddev.phys = "";
  nddev.init = [] (void* data, input_source* ref) -> int {
    network_gamepad* sc = ((network_gamepad*)data);
    sc->ref = ref;
    network_gamepad::methods.watch_file(ref, sc->statepipe[0], sc->statepipe);
    return 0;
  };
  nddev.destroy = [] (void* data) -> int {
    delete (network_gamepad*) data;
    return 0;
  };
  nddev.get_description = [] (const void* data) {
    return "Network Controller";
  };
  nddev.get_type = [] (const void* data) {
    return "gamepad";
  };
  nddev.process_event = [] (void* data, void* tag) -> int {
    ((network_gamepad*)data)->process(tag);
    return 0;
  };
  nddev.process_option = [] (void* data, const char* opname, MGField opvalue) {
    return ((network_gamepad*)data)->process_option(opname, opvalue);
  };
  nddev.upload_ff = nullptr;
  nddev.erase_ff = nullptr;
  nddev.play_ff = nullptr;

  api.mg.add_manager(ndman,  manager);
  std::cout << "initialized dirver" << std::endl;
  return 0;
}


int nd_loaded = register_plugin(&nd_plugin_init);
