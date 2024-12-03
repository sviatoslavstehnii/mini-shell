#include <stdio.h>
#include <string>
#include <vector>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <bits/stdc++.h>
#include <boost/program_options.hpp>
#include <filesystem>
#include <stdlib.h>
#include <sys/stat.h>
#include <glob.h>
#include <dirent.h>
#include <readline/readline.h>
#include <readline/history.h>

namespace po = boost::program_options;

enum ERROR {
    UknownOption=1,
    FileNotFound=2,
    TooManyArgs=3,
    WrongArgCount=4,
    Other=5
};

struct Redirection {
    int stdout_fd = STDOUT_FILENO;
    int stderr_fd = STDERR_FILENO;
    bool stdout_redirected = false;
    bool stderr_redirected = false;
    int stdout_backup = -1;
    int stderr_backup = -1;
};

class my_shell {
private:
    std::unordered_map<std::string, std::function<void(const std::vector<std::string>&, const Redirection&)>> internal_cmds_m;
    int last_status = 0;
    bool is_background = false;
    bool redirecting = false;
public:
    my_shell(int argc=1, char** argv=nullptr);
    ~my_shell() = default;
    void run();

private:
    std::string read_line();
    std::pair<std::vector<char *>, std::string> split_line(const std::string& line);
    std::vector<std::string> split_pipe(const std::string& line);
    Redirection handle_redirection(std::string& line);
    void restore_redirection(const Redirection& redir);

    void execute(std::string& line, Redirection& redir);
    void pipe_execute(std::vector<std::string>& pipe_line, Redirection& redir);

    void show_help(const po::options_description& desc);
    bool parse_args(const std::vector<std::string>& args, 
                          const po::options_description& desc, po::variables_map& vm);
 
    void run_external(std::vector<char*>& args, const std::string& input_file = "");
    void run_internal(std::function<void(const std::vector<std::string>&, const Redirection&)>& f, const std::vector<std::string>& args, const Redirection& redir);
    void run_script(const std::string& filename);

    void mpwd(const std::vector<std::string>& args, const Redirection& redir);
    void mcd(const std::vector<std::string>& args, const Redirection& redir);
    void merrno(const std::vector<std::string>& args, const Redirection& redir);
    void mexit(const std::vector<std::string>& args, const Redirection& redir);
    void mecho(const std::vector<std::string>& args, const Redirection& redir);
    void point(const std::vector<std::string>& args, const Redirection& redir);
    void mexport(const std::vector<std::string>& args, const Redirection& redir);

    std::vector<std::string> convert_to_str_vec(const std::vector<char*>& char_vect);
};