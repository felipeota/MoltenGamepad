
#include "parser.h"
#include "event_translators/event_change.h"
#include "event_translators/translators.h"
#include <stdlib.h>
/*Want a modified INI syntax.
 * Three main line formats.
 *
 * [header]
 *
 * this = that
 *
 * do this
 *
 * the square brackets or equals signs uniquely identify the first two formats.
 *
 * The third format is arbitrary shell-style
 *   command arg1 arg2 ...
 *
 * The extra complication is in letting the right-hand side of
 *    this = that
 * grow complicated and nested, like redirect(mouse_slot, btn2btn(BTN_1))
 */

void print_tokens(std::vector<token>& tokens) {
  for (auto it = tokens.begin(); it != tokens.end(); it++) {
    std::cout << (*it).type << (*it).value << " ";
  }
  std::cout << std::endl;
}

bool isIdent(char c) {
  return isalnum(c) || c == '_' || c == '-' || c == '+' || c == '?' ;
}

std::vector<token> tokenize(std::string line) {
  std::string temp;
  std::vector<token> tokens;
  bool quotemode = false;
  bool escaped = false;
  for (auto it = line.begin(); it != line.end(); it++) {
    char c = *it;

    if (quotemode) {
      if (escaped && (c == '\"' || c == '\\')) {
        temp.push_back(c);
        escaped = false;
        continue;
      }
      if (c == '\\' && !escaped) {
        escaped = true;
        continue;
      }
      if (c == '\"' && !escaped) {
        quotemode = false;
        tokens.push_back({TK_IDENT, std::string(temp)});
        temp.clear();
        continue;
      }
      temp.push_back(c);
      continue;
    }

    if (temp.empty() && c == '\"') {
      quotemode = true;
      continue;
    }


    if (!isIdent(c) && !temp.empty()) {
      tokens.push_back({TK_IDENT, std::string(temp)});
      temp.clear();
    }

    if (c == '#') { //Comment. Stop reading this line.
      break;
    }
    if (c == '\n') {
      tokens.push_back({TK_ENDL, "\\n"});
    }
    if (c == '.') {
      tokens.push_back({TK_DOT, "."});
    }
    if (c == '=') {
      tokens.push_back({TK_EQUAL, "="});
    }
    if (c == '[') {
      tokens.push_back({TK_HEADER_OPEN, "["});
    }
    if (c == ']') {
      tokens.push_back({TK_HEADER_CLOSE, "]"});
    }
    if (c == '(') {
      tokens.push_back({TK_LPAREN, "("});
    }
    if (c == ')') {
      tokens.push_back({TK_RPAREN, ")"});
    }
    if (c == ',') {
      tokens.push_back({TK_COMMA, ","});
    }
    if (c == ':') {
      tokens.push_back({TK_COLON, ":"});
    }
    if (c == '/') {
      tokens.push_back({TK_SLASH, "/"});
    }

    if (isIdent(c)) {
      temp.push_back(c);
    }
  }

  if (!temp.empty()) {
    tokens.push_back({TK_IDENT, std::string(temp)});
    temp.clear();
  }
  tokens.push_back({TK_ENDL, "\\n"});


  return tokens;
}


bool find_token_type(enum tokentype type, std::vector<token>& tokens) {
  for (auto it = tokens.begin(); it != tokens.end(); it++) {
    if ((*it).type == type) return true;
    if ((*it).type == TK_ENDL) return false;
  }
  return false;

}

void do_header_line(std::vector<token>& line, std::string& header) {
  if (line.empty()) return;
  if (line.at(0).type != TK_HEADER_OPEN) return;
  std::string newheader;
  for (auto it = ++line.begin(); it != line.end(); it++) {

    if ((*it).type == TK_HEADER_OPEN) return; //abort.

    if ((*it).type == TK_HEADER_CLOSE) {
      if (newheader.empty()) return;
      header = newheader;
      return;
    }
    if (!newheader.empty()) newheader.push_back(' ');
    newheader += (*it).value;

  }
}

//Some lazy macros to build some function pointers with associated name strings.
//TRANSGEN just associates an event_translator with its name.
//RENAME_TRANSGEN instead supplies a different name for the parser's sake.

#define TRANSGEN(X) trans_gens[#X] = trans_generator(X::fields,[] (std::vector<MGField>& fields) { return new X(fields);});
#define RENAME_TRANSGEN(name,X) trans_gens[#name] = trans_generator(X::fields,[] (std::vector<MGField>& fields) { return new X(fields);});

//Need a static location for this array
MGType mouse_fields[] = {MG_TRANS, MG_NULL};

std::map<std::string,trans_generator> MGparser::trans_gens;
moltengamepad* MGparser::mg;

void MGparser::load_translators(moltengamepad* mg) {
  MGparser::mg = mg;
  TRANSGEN(btn2btn);
  TRANSGEN(btn2axis);
  TRANSGEN(axis2axis);
  TRANSGEN(axis2btns);
  TRANSGEN(btn2rel);
  TRANSGEN(axis2rel);
  RENAME_TRANSGEN(redirect,redirect_trans);
  //RENAME_TRANSGEN(key,keyboard_redirect);
  RENAME_TRANSGEN(multi,multitrans);
  //add a quick mouse redirect
  trans_gens["mouse"] = trans_generator( mouse_fields, [mg] (std::vector<MGField>& fields) {
    //Need to tack on a field with the keyboard slot
    MGField keyboard_slot;
    keyboard_slot.type = MG_SLOT;
    keyboard_slot.slot = mg->slots->keyboard;
    fields.push_back(keyboard_slot);
    return new redirect_trans(fields);
  });
  //key is just a synonym to the above. It redirects events to the keyboard slot.
  trans_gens["key"] = trans_gens["mouse"];
}

MGparser::MGparser(moltengamepad* mg) : out("parse") {
  out.add_listener(1);
}

void MGparser::do_assignment(std::string header, std::string field, std::vector<token> rhs) {
  enum entry_type left_type = NO_ENTRY;
  
  auto prof = mg->find_profile(header);
  if (!prof) {
    out.take_message("could not locate profile " + header);
    return;
  }
  
  left_type = prof->get_entry_type(field);

  if (left_type == DEV_KEY || left_type == DEV_AXIS) {
    auto it = rhs.begin();
    event_translator* trans = parse_trans(left_type, rhs, it);

    if (!trans) {
      out.take_message("Parsing the right-hand side failed.");
      return; //abort
    }

    if (trans)  {
      prof->set_mapping(field, trans->clone(), left_type, false);
      std::stringstream ss;
      MGTransDef def;
      trans->fill_def(def);
      print_def(left_type, def, ss);
      delete trans;
      out.take_message("setting " + header + "." + field + " = " + ss.str());
      return;
    }
  }

  if (rhs.empty()) return;

  if (!field.empty() && field.front() == '?' && rhs.size() > 0) {
    field.erase(field.begin());
    int ret = prof->set_option(field, rhs.front().value);
    if (ret)
      out.take_message(field + " is not a registered option");
    return;
  }

  out.take_message("assignment failed for field " + field);
}



void MGparser::do_adv_assignment(std::string header, std::vector<std::string>& fields, std::vector<token> rhs) {
  if (rhs.empty()) return;
  auto prof = mg->find_profile(header);
  if (!prof) {
    out.take_message("could not locate profile " + header);
    return;
  }

  if (rhs.front().value == "nothing") {
    prof->set_advanced(fields, nullptr);
    return;
  }
  advanced_event_translator* trans = parse_adv_trans(fields, rhs);
  if (!trans) {
    out.take_message("could not parse right hand side");
  }
  if (trans) {
    prof->set_advanced(fields, trans->clone());
    std::stringstream ss;
    MGTransDef def;
    trans->fill_def(def);
    print_def(DEV_KEY, def, ss);
    out.take_message("setting advanced translator to " + ss.str());
  }

  if (trans) delete trans;


  if (rhs.empty()) return;

}

void MGparser::do_assignment_line(std::vector<token>& line, std::string& header) {
  std::string effective_header = "";
  std::string effective_field;
  std::string chord1 = "";
  std::string chord2 = "";
  std::vector<std::string> multifield;
  std::vector<token> leftside;
  std::vector<token> rightside;
  std::string* field = &effective_header;
  bool seen_dot = 0;
  bool seen_field = 0;

  auto it = line.begin();

  /* Only valid left hand sides:
   *
   * HEADER DOT FIELD
   * FIELD
   * DOT FIELD
   *
   * FIELD may actually be a tuple, that is valid as well.
   *
   * We start by reading an IDENT into effective_header.
   * If we see a dot, we switch to trying to read
   *   an IDENT into effective_field.
   * Afterwards, if there was no dot, then
   * our header was implicit, and we need to move
   * the read value into effective_field.
   */
  for (; it != line.end() && (*it).type != TK_EQUAL; it++) {

    if ((*it).type == TK_DOT) {
      if (seen_dot) return;
      seen_dot = true;
      field = &effective_field;
      seen_field = false;

    } else if ((*it).type == TK_IDENT) {
      if (seen_field) return;
      *field = (*it).value;
      seen_field = true;

    } else if ((*it).type == TK_LPAREN) {
      do {
        it++;
        if (it == line.end()) return;
        multifield.push_back(it->value);
        it++;
      } while (it != line.end() && it->type == TK_COMMA);

      if (it == line.end() || it->type != TK_RPAREN) return;
      it++;
      if (it == line.end() || it->type != TK_EQUAL) return;
      break;


    } else {
      return; //abort.
    }

  }

  if (!seen_dot) {
    effective_field = effective_header;
    effective_header = header;
  }

  if (effective_header.empty()) effective_header = "moltengamepad";

  if (it == line.end()) return; //Shouldn't happen.

  it++; //Skip past the "="

  for (; it != line.end() && (*it).type != TK_ENDL; it++) {
    rightside.push_back(*it);
  }

  if (multifield.size() > 0) {
    do_adv_assignment(effective_header, multifield, rightside);
    return;
  }

  if (effective_field.empty() || rightside.empty()) return;


  do_assignment(effective_header, effective_field, rightside);



}

void MGparser::parse_line(std::vector<token>& line, std::string& header) {

  if (find_token_type(TK_HEADER_OPEN, line)) {
    do_header_line(line, header);
    out.take_message("header is " + header);
    return;
  }

  if (find_token_type(TK_EQUAL, line) && line[0].value != "set") {
    do_assignment_line(line, header);
  } else {
    do_command(mg, line);
  }

}

void MGparser::exec_line(std::vector<token>& line, std::string& header) {
  parse_line(line, header);
}


event_translator* MGparser::parse_trans(enum entry_type intype, std::vector<token>& tokens, std::vector<token>::iterator& it) {
  //Note: this function is assumed to be called at the top of parsing a translator,
  //not as some recursive substep. If we wish to avoid the quirks, use parse_complex_trans instead.
  event_translator* trans = parse_trans_toplevel_quirks(intype, tokens, it);
  if (trans) return trans;
  trans = parse_trans_strict(intype, tokens, it);

  return trans;
}

event_translator* MGparser::parse_trans_toplevel_quirks(enum entry_type intype, std::vector<token>& tokens, std::vector<token>::iterator& it) {
  //For backwards compatibility/ease of use, there is some automagic hard to fit in elsewhere.
  //Namely, the automagic "btn,btn" parsing of axis2btns. Without a leading parenthesis,
  //this structure is hard to detect as an expression.
  //Allowing a top level expression to include a comma would make the parse descent ambiguous.
  //So instead, it ends up here.

  if (it != tokens.begin()) return nullptr; //Not the toplevel? Abort?

  //This is very heavy on formatting, a button, a comma, a button.
  if (intype == DEV_AXIS && tokens.size() == 3 && tokens[1].type == TK_COMMA) {
    complex_expr* expr = new complex_expr;
    expr->ident = "";
    expr->params.push_back(new complex_expr);
    expr->params.push_back(new complex_expr);
    expr->params[0]->ident = tokens[0].value;
    expr->params[1]->ident = tokens[2].value;
    event_translator* trans = parse_trans_expr(DEV_AXIS, expr);
    free_complex_expr(expr);
    return trans;
  }
  return nullptr;
}


event_translator* MGparser::parse_trans_strict(enum entry_type intype, std::vector<token>& tokens, std::vector<token>::iterator& it) {
  auto localit = it;
  complex_expr* expr = read_expr(tokens, localit);
  event_translator* trans =  parse_trans_expr(intype, expr);
  free_complex_expr(expr);
  it = localit;
  return trans;

}



void release_def(MGTransDef& def) {
  for (auto entry : def.fields) {
    if (entry.type == MG_TRANS || entry.type == MG_KEY_TRANS || entry.type == MG_AXIS_TRANS || entry.type == MG_REL_TRANS)
      delete entry.trans;
    if (entry.type == MG_ADVANCED_TRANS)
      delete entry.adv_trans;
    if (entry.type == MG_STRING)
      free((char*)entry.string);
  }
}


event_translator* MGparser::parse_trans_expr(enum entry_type intype, complex_expr* expr) {
  if (!expr) return nullptr;

  event_translator* trans = parse_special_trans(intype, expr);
  if (trans) return trans;

  const MGType(*fields) = nullptr;

  auto generator = trans_gens.find(expr->ident);
  if (generator == trans_gens.end())
    return nullptr;

  fields = generator->second.fields;

  MGTransDef def;
  def.identifier = expr->ident;

  for (int i = 0; (fields)[i] != MG_NULL; i++) {
    def.fields.push_back({(fields)[i], 0});
  }

  if (!parse_def(intype, def, expr)) return nullptr;


  //still need to build it!
  try {
    trans = generator->second.generate(def.fields);
  } catch (...) {
    trans = nullptr;
  }
  release_def(def);
  return trans;
}


int read_ev_code(std::string& code, out_type type) {
  int i;
  try {
    i = atoi(code.c_str());
    return i;
  } catch (...) {
    if (type == OUT_NONE) return 0;
    event_info info = lookup_event(code.c_str());
    if (info.type == type) return info.value;
  }
  return -1;
}

//Handles those automagic simple cases.
#define SPEC_REL_BTN 3
#define SPEC_REL_AXIS 10
event_translator* MGparser::parse_special_trans(enum entry_type intype, complex_expr* expr) {
  if (!expr) return nullptr;

  if (expr->ident == "nothing") return new event_translator();

  //Key to a key.
  if (intype == DEV_KEY && expr->params.size() == 0) {
    int out_button = read_ev_code(expr->ident, OUT_KEY);
    if (out_button >= 0) return new btn2btn(out_button);
  }

  //Axis or key to an axis or rel. (Detect +/- directions)
  if ((intype == DEV_AXIS || intype == DEV_KEY) && expr->params.size() == 0) {
    std::string outevent = expr->ident;
    int direction = 1;
    if (outevent.size() > 0) {
      if (outevent[0] == '+') {
        outevent.erase(outevent.begin());
      }
      if (outevent[0] == '-') {
        outevent.erase(outevent.begin());
        direction = -1;
      }
      //Check for it being an axis
      int out_axis = read_ev_code(outevent, OUT_ABS);
      if (out_axis >= 0 && intype == DEV_AXIS) return new axis2axis(out_axis, direction);
      if (out_axis >= 0 && intype == DEV_KEY)  return new btn2axis(out_axis, direction);

      //Check for it being a rel
      int out_rel = read_ev_code(outevent, OUT_REL);
      if (out_rel >= 0 && intype == DEV_AXIS) return new axis2rel(out_rel, SPEC_REL_AXIS*direction);
      if (out_rel >= 0 && intype == DEV_KEY)  return new btn2rel(out_rel, SPEC_REL_BTN*direction);
    }
  }


  //Axis to buttons.
  if ((intype == DEV_AXIS) && expr->ident.empty() && expr->params.size() == 2) {
    int neg_btn = read_ev_code(expr->params[0]->ident, OUT_KEY);
    int pos_btn = read_ev_code(expr->params[1]->ident, OUT_KEY);
    if (neg_btn >= 0 && pos_btn >= 0) return new axis2btns(neg_btn, pos_btn);
  }

  return nullptr;
}


bool MGparser::parse_def(enum entry_type intype, MGTransDef& def, complex_expr* expr) {
  if (!expr) return false;
  if (def.identifier != expr->ident) return false;
  int fieldsfound = expr->params.size();
  int j = 0;
  for (int i = 0; i < def.fields.size(); i++) {
    MGType type = def.fields[i].type;
    if (type == MG_KEYBOARD_SLOT) {
      def.fields[i].slot = mg->slots->keyboard;
      continue;
    };
    if (j >= fieldsfound) return false;

    if (type == MG_TRANS) def.fields[i].trans = parse_trans_expr(intype, expr->params[j]);
    if (type == MG_KEY_TRANS) def.fields[i].trans = parse_trans_expr(DEV_KEY, expr->params[j]);
    if (type == MG_REL_TRANS) def.fields[i].trans = parse_trans_expr(DEV_REL, expr->params[j]);
    if (type == MG_AXIS_TRANS) def.fields[i].trans = parse_trans_expr(DEV_AXIS, expr->params[j]);
    if (type == MG_KEY) def.fields[i].key = read_ev_code(expr->params[j]->ident, OUT_KEY);
    if (type == MG_REL) def.fields[i].rel = read_ev_code(expr->params[j]->ident, OUT_REL);
    if (type == MG_AXIS) def.fields[i].axis = read_ev_code(expr->params[j]->ident, OUT_ABS);
    if (type == MG_INT) def.fields[i].integer = read_ev_code(expr->params[j]->ident, OUT_NONE);
    if (type == MG_SLOT) {
      output_slot* slot = mg->slots->find_slot(expr->params[j]->ident);
      if (!slot) return false;
      def.fields[i].slot = slot;
    }
    if (type == MG_BOOL) {
      def.fields[i].integer = 0;
      read_bool(expr->params[j]->ident, [&def, i] (bool val) {
        def.fields[i].integer = val ? 1 : 0;
      });
    }
    if (type == MG_STRING) {
      size_t size = expr->params[j]->ident.size();
      char* copy = (char*) calloc(size+1,sizeof(char));
      strncpy(copy,expr->params[j]->ident.c_str(), size);
      copy[size] = '\0';
      def.fields[i].string = copy;
    }
    //TODO: float
    j++;
  }

  return true;
}

void MGparser::print_def(entry_type intype, MGTransDef& def, std::ostream& output) {
  //Check for the possibility of some automagic.
  if (print_special_def(intype, def, output)) return;

  output << def.identifier;
  if (def.fields.size() > 0) output << "(";
  bool needcomma = false;
  for (auto field : def.fields) {
    if (needcomma) output << ",";
    MGType type = field.type;
    if (type == MG_KEY) {
      const char* name = get_key_name(field.key);
      if (name) {
        output << name;
      } else {
        output << field.key;
      }
    }
    if (type == MG_AXIS) {
      const char* name = get_axis_name(field.axis);
      if (name) {
        output << name;
      } else {
        output << field.axis;
      }
    }
    if (type == MG_REL) {
      const char* name = get_rel_name(field.rel);
      if (name) {
        output << name;
      } else {
        output << field.rel;
      }
    }
    if (type == MG_INT) output << field.integer;
    if (type == MG_FLOAT) output << field.real;
    if (type == MG_STRING) output << "\""<<field.string<<"\"";
    if (type == MG_SLOT) output << field.slot->name;
    if (type == MG_BOOL) output << (field.integer ? "true":"false");
    if (type == MG_TRANS || type == MG_KEY_TRANS || type == MG_AXIS_TRANS || type == MG_REL_TRANS) {
      MGTransDef innerdef;
      entry_type context = intype;
      if (type == MG_KEY_TRANS) context = DEV_KEY;
      if (type == MG_AXIS_TRANS) context = DEV_AXIS;
      if (type == MG_REL_TRANS) context = DEV_REL;
      field.trans->fill_def(innerdef);
      print_def(context, innerdef, output);
    }
    needcomma = true;
  }
  if (def.fields.size() > 0) output << ")";
}

bool MGparser::print_special_def(entry_type intype, MGTransDef& def, std::ostream& output) {
  //Check if we are in a setting where some magic can be applied.
  //e.g. print "primary" instead of "btn2btn(primary)" where sensible.
  //This method is essentially a lot of code checking for special cases.

  //Check for btn2btn on a button
  if (intype == DEV_KEY) {
    if (def.identifier == "btn2btn" && def.fields.size() > 0 && def.fields[0].type == MG_KEY) {
      const char* name = get_key_name(def.fields[0].key);
      if (name) {
        output << name;
      } else {
        output << def.fields[0].key;
      }
      return true;
    }
  }

  //check for being simply mapped to an axis or rel
  if ((intype == DEV_KEY && (def.identifier == "btn2axis" || def.identifier == "btn2rel"))
    || (intype == DEV_AXIS && (def.identifier == "axis2axis" || def.identifier == "axis2rel"))) {
    if (def.fields.size() >= 2 &&  def.fields[1].type == MG_INT) {
      const char* name = nullptr;
      const char* prefix = "";
      //Axis: get axis name and check for default speeds of +/- one.
      if (def.fields[0].type == MG_AXIS) {
        name = get_axis_name(def.fields[0].axis);
        if (def.fields[1].integer == -1) {
          prefix = "-";
        }
        if (def.fields[1].integer == +1) {
          prefix = "+";
        }
      } else if (def.fields[0].type == MG_REL) {
        //Rel: default speeds depend on the intype as well!
        name = get_rel_name(def.fields[0].axis);
        int speed = def.fields[1].integer;
        if ((intype == DEV_KEY && speed == -SPEC_REL_BTN) || (intype == DEV_AXIS && speed == -SPEC_REL_AXIS)) {
          prefix = "-";
        }
        if ((intype == DEV_KEY && speed == SPEC_REL_BTN) || (intype == DEV_AXIS && speed == SPEC_REL_AXIS)) {
          prefix = "+";
        }
      }
      
      if (!prefix) return false;
      if (!name) return false;
      output << prefix << name;
      return true;
    }
  }
  //Check for simple mappings of an axis to two buttons
  if (intype == DEV_AXIS && def.identifier == "axis2btns" && def.fields.size() >= 2 && def.fields[0].type == MG_KEY && def.fields[1].type == MG_KEY) {
    const char* nameneg = get_key_name(def.fields[0].key);
    const char* namepos = get_key_name(def.fields[1].key);
    output << "(";
    if (nameneg) output << nameneg;
    if (!nameneg) output << def.fields[0].key;
    output << ",";
    if (namepos) output << namepos;
    if (!namepos) output << def.fields[1].key;
    output << ")";
    return true;
  }

  //Check for redirecting to the keyboard_slot
  if (def.identifier == "redirect" && def.fields.size() >= 2 && def.fields[1].type == MG_SLOT) {
    if (def.fields[1].slot && def.fields[1].slot->name == "keyboard") {
      MGTransDef innerdef;
      entry_type context = intype;
      def.fields[0].trans->fill_def(innerdef);
      //Quick heuristic: if it is a "2rel" translation, it is a mouse movement.
      if (innerdef.identifier == "btn2rel" || innerdef.identifier == "axis2rel") {
        output << "mouse(";
      } else {
        output << "key(";
      }
      print_def(context, innerdef, output);
      output << ")";
      return true;
    }
  }

  return false;
}



void free_complex_expr(complex_expr* expr) {
  if (!expr) return;
  for (auto it = expr->params.begin(); it != expr->params.end(); it++) {
    if ((*it)) free_complex_expr(*it);
  }
  delete expr;
}

void print_expr(complex_expr* expr, int depth) {
  for (int i = 0; i < depth; i++) std::cout << " ";
  if (!expr) {
    std::cout << "(null expr)" << std::endl;
    return;
  }

  std::cout << expr->ident << std::endl;
  for (auto it = expr->params.begin(); it != expr->params.end(); it++) {
    print_expr((*it), depth + 1);
  }

}

struct complex_expr* read_expr(std::vector<token>& tokens, std::vector<token>::iterator& it) {
  if (tokens.empty() || it == tokens.end()) return nullptr;

  bool abort = false;


  if ((*it).type == TK_IDENT || (*it).type == TK_LPAREN) {
    complex_expr* expr = new complex_expr;
    //If we have ident, read it in.
    //Otherwise, we have a paren, start reading children and leave the ident empty.
    if ((*it).type == TK_IDENT) {
      expr->ident = (*it).value;
      it++;
    }

    if (it == tokens.end()) return expr;

    if ((*it).type == TK_LPAREN) {
      it++;
      complex_expr* subexpr = read_expr(tokens, it);
      if (subexpr) expr->params.push_back(subexpr);
      while (it != tokens.end() && (*it).type == TK_COMMA) {
        it++;
        subexpr = read_expr(tokens, it);
        if (subexpr) expr->params.push_back(subexpr);
      }


      if (it != tokens.end() && (*it).type == TK_RPAREN) {
        it++;
        return expr;
      }
    } else {
      return expr;
    }

    free_complex_expr(expr); //failed to parse.
  }



  return nullptr;
}

advanced_event_translator* build_adv_from_def(const std::vector<std::string>& event_names, MGTransDef& def) {
  if (def.identifier == "simple") return new simple_chord(event_names, def.fields);
  if (def.identifier == "exclusive") return new exclusive_chord(event_names, def.fields);
  return nullptr;
}

advanced_event_translator* MGparser::parse_adv_trans(const std::vector<std::string>& event_names, std::vector<token>& rhs) {
  auto it = rhs.begin();
  event_translator* trans = parse_trans(DEV_KEY, rhs, it);
  if (trans) return new simple_chord(event_names, trans);

  it = rhs.begin();
  complex_expr* expr = read_expr(rhs, it);

  if (!expr) return nullptr;

  if (expr->params.empty()) return nullptr;

  advanced_event_translator* adv_trans = nullptr;

  const MGType(*fields) = nullptr;

  if (expr->ident == "simple") fields = simple_chord::fields;
  if (expr->ident == "exclusive") fields = exclusive_chord::fields;

  if (!fields) return nullptr;

  MGTransDef def;
  def.identifier = expr->ident;

  for (int i = 0; (fields)[i] != MG_NULL; i++) {
    def.fields.push_back({(fields)[i], 0});
  }

  if (!parse_def(DEV_KEY, def, expr)) return nullptr;


  //still need to build it!
  adv_trans = build_adv_from_def(event_names, def);
  release_def(def);

  free_complex_expr(expr);

  return adv_trans;
}
