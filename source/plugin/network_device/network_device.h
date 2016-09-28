#pragma once
#include <thread>
#include <functional>
#include <unistd.h>
#include <linux/input.h>
#include <unordered_map>
#include <mutex>
#include "../plugin.h"

extern int nd_loaded;
extern device_plugin nddev;

enum nd_gamepad_keys{
  nd_a,
  nd_b,
  nd_x,
  nd_y,
  nd_dpad_right,
  nd_dpad_left,
  nd_dpad_up,
  nd_dpad_down,
  nd_lt,
  nd_rt,
  nd_lb,
  nd_rb,
  nd_ls,
  nd_rs,
  nd_lh,
  nd_rh,
  nd_start,
  nd_back,
  nd_menu,

  //TODO: gyroscopes

  size,
};


extern const event_decl nd_events[];

extern const option_decl nd_options[];

int lookup_steamcont_event(const char* evname);


class network_gamepad {
public:
  network_gamepad();
  ~network_gamepad();

  input_source* ref;
  friend int nd_plugin_init(plugin_api api);
  static device_methods methods;

protected:
  void process(void*);
  int process_option(const char* opname, const MGField value) { return -1; }; //TODO: options
private:
  int statepipe[2];
};

class network_device_manager {
public:

  void init_profile();

  int init(device_manager* ref) {
    this->ref = ref;
    init_profile();
  }

  int start();

  network_device_manager();

  ~network_device_manager() {
    keep_scanning = false;
    if (nd_context_thread) {
      nd_context_thread->join();
      delete nd_context_thread;
    }
  }
  network_gamepad *gamepad;
  friend int nd_plugin_init(plugin_api api);
  static manager_methods methods;
private:
  std::thread* nd_context_thread = nullptr;
  volatile bool keep_scanning;
  std::mutex devlistlock;
  device_manager* ref;
};

