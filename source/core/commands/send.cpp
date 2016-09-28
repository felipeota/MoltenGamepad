#include "../moltengamepad.h"
#include <iostream>
#include <fstream>
#include <string>
#include <cctype>
#include "../parser.h"
#include <linux/uinput.h>
#include <stdlib.h>
#include "../eventlists/eventlist.h"


#define MOVE_USAGE "USAGE:\n\tsend <event> <value> to <slot>\n\t\"all\" can be used to refer to all devices\n\t"
int do_send(moltengamepad* mg, std::vector<token>& command) {
  if (command.size() < 4) {
    std::cout << "size is less than 4" << MOVE_USAGE << std::endl;
    return -1;
  }
  if (command.at(3).value != "to") {
    std::cout << "to not found" << MOVE_USAGE << std::endl;
    return -1;
  }
  std::string event = command.at(1).value;
  std::string value = command.at(2).value;
  std::string slotname = command.at(4).value;
  int ievent = get_key_id(event.c_str());
  int ival = atoi(value.c_str());
  output_slot* slot = mg->slots->find_slot(slotname);
  if (!slot && slotname != "nothing") {
    std::cout << "slot " << slotname << " not found.\n" << MOVE_USAGE << std::endl;
    return -1;
  }
  struct input_event i_event;
  i_event.type = EV_KEY;
  i_event.code = ievent;
  i_event.value = ival;
  std::cout << "sending event to " << slotname << std::endl;
  slot->take_event(i_event);
  i_event.type = EV_SYN;
  i_event.code = 0;
  i_event.value = 1;
  slot->take_event(i_event);
  return 0;
}
