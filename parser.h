#ifndef SEND_PARSER_H
#define SEND_PARSER_H
#include <string>
#include <map>
#include <vector>

typedef struct {
  char ch;
  bool flag;
  bool is_number; // if flag then this is a number
  bool present; // if this is present in the parsed argument
  std::string string;
  std::string description;
  float number;
} named_arg_t;

typedef struct {
    std::string name; // shortname
    std::string description;
    std::string string;
    bool optional;
} pos_arg_t;

class parser {
private:
    std::map<char,named_arg_t> named_args;
    std::vector<pos_arg_t> pos_args;
    int next_position = 0;
    int required_args = 0;
    bool end_of_required = false;
    int parse_arg(const std::string &s);
public:
    parser();
    void add_arg(char ch, std::string description, bool flag = true, bool is_number = false);
    void add_arg_pos(std::string name, std::string description, bool optional = false);
    int parse_args(int argc, char* argv[]);
    bool is_present(char ch);
    int get_string(char ch, std::string &s);
    int get_number(char ch, float &n);
    std::string help(const std::string& prog_name);

    int get_pos(int i, std::string &s);

    int get_int(char ch, int &n);
};


#endif //SEND_PARSER_H
