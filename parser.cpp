#include "parser.h"

#include <utility>
#include <iostream>

void parser::add_arg(char ch, std::string description, bool flag, bool is_number) {
  named_arg_t arg{};
  arg.ch = ch;
  arg.flag = flag;
  arg.is_number = is_number;
  arg.description = std::move(description);
  named_args[ch] = arg;
}

int parser::parse_arg(const std::string &s) {
  if (s[0] != '-') {
    if (pos_args.size() <= next_position) return -1;
    pos_args[next_position++].string = s;
    return 0;
  }
  if (s.length() < 2) return -1;
  if (named_args.count(s[1]) == 0) return -1;
  named_arg_t *arg = &named_args[s[1]];
  arg->present = true;
  if (arg->flag) return 0;
  if (s.length() < 4) return -1;
  if (s[2] != '=') return -1;
  std::string sub = s.substr(3);
  arg->string = sub;
  if (arg->is_number) {
    arg->number = std::stof(sub);
  }
  return 0;
}

// returns -1 on failure; -2 to indicate exit
int parser::parse_args(int argc, char *argv[]) {
  next_position = 0;
  bool failed = false;
  for (int i = 1; i < argc; i++) {
    std::string s = argv[i];
    if (parse_arg(s) < 0) {
      failed = true;
      break;
    }
  }
  if (named_args['h'].present || failed || next_position < required_args) {
    std::cout << help(argv[0]);
    return -1;
  }
  return 0;
}

bool parser::is_present(char ch) {
  return named_args[ch].present;
}

int parser::get_string(char ch, std::string &s) {
  if (named_args.count(ch) == 0) return -1;
  if (!named_args[ch].present) return -1;
  s = named_args[ch].string;
  return 0;
}

int parser::get_number(char ch, float &n) {
  if (named_args.count(ch) == 0) return -1;
  if (!named_args[ch].present) return -1;
  n = named_args[ch].number;
  return 0;
}

int parser::get_int(char ch, int &n) {
  if (named_args.count(ch) == 0) return -1;
  if (!named_args[ch].present) return -1;
  n = (int) named_args[ch].number;
  return 0;
}

std::string parser::help(const std::string& prog_name) {
  std::string str;
  str += "Usage:\n" + prog_name + " ";
  std::string arg_details;
  for (const pos_arg_t& arg : pos_args) {
    if (arg.optional) {
      arg_details += "[";
      str += "[";
    }
    str += arg.name;
    arg_details += arg.name;
    if (arg.optional) {
      arg_details += "]";
      str += "] ";
    } else {
      str += " ";
    }
    arg_details += ": " + arg.description + "\n";
  }
  for (std::pair<char,named_arg_t> arg : named_args) {
    arg_details += "[-";
    arg_details.append(1, arg.first);
    str += "[-";
    str.append(1, arg.first);
    if (!arg.second.flag) {
      str += "=";
      arg_details += "=";
      str += (arg.second.is_number) ? "#?" : "s?";
      arg_details += (arg.second.is_number) ? "#?" : "s?";
    }
    str += "] ";
    arg_details += "] " + arg.second.description + "\n";
  }
  return str + "\n" + arg_details;
}

parser::parser() {
  add_arg('h', "Show this help");
}

int parser::get_pos(int i, std::string &s) {
  if (pos_args.size() <= i) return -1;
  if (next_position <= i) return -1;
  s = pos_args[i].string;
  return 0;
}

void parser::add_arg_pos(std::string name, std::string description, bool optional) {
  pos_arg_t arg{};
  arg.description = std::move(description);
  arg.name = std::move(name);
  arg.optional = optional;
  if (arg.optional) {
    end_of_required = true;
  } else if (!end_of_required) {
    required_args++;
  }
  pos_args.push_back(arg);
}

