#include "my_shell.h"

static void zombie_handler(int) {
    int wstat;
    while (true) {
        pid_t pid = wait3 (&wstat, WNOHANG, NULL );
        if (pid == 0 || pid == -1){
            break;
        }
    }
}

void my_shell::restore_redirection(const Redirection& redir) {
    if (redir.stdout_redirected) {
        dup2(redir.stdout_backup, STDOUT_FILENO);
        close(redir.stdout_backup);
    }
    if (redir.stderr_redirected) {
        dup2(redir.stderr_backup, STDERR_FILENO);
        close(redir.stderr_backup);
    }
}

Redirection my_shell::handle_redirection(std::string& line) {
    Redirection redir;
    std::string tmp_line = line;
    size_t pos;
    if ((pos = line.find(">")) != std::string::npos) {
        std::string file = line.substr(pos + 1);
        tmp_line = line.substr(0, pos);
        file.erase(0, file.find_first_not_of(" \t"));
        file = file.substr(0, file.find_first_of(" \t"));
        int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("Failed to open file for stdout redirection");
            return redir;
        }
        if (line.find("2>&1") != std::string::npos) {
            redir.stderr_backup = dup(STDERR_FILENO);
            dup2(fd, STDERR_FILENO);
            redir.stderr_redirected = true;
        }
        redir.stdout_backup = dup(STDOUT_FILENO);
        dup2(fd, STDOUT_FILENO);
        redir.stdout_redirected = true;
        close(fd);
        redirecting = true;
    }
    if ((pos = line.find("2>")) != std::string::npos && line.find("2>&1", pos) == std::string::npos) {
        restore_redirection(redir);
        std::string file = line.substr(pos + 2);
        tmp_line = line.substr(0, pos);
        file.erase(0, file.find_first_not_of(" \t")); 
        file = file.substr(0, file.find_first_of(" \t"));
        int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("Failed to open file for stderr redirection");
            return redir;
        }
        redir.stderr_backup = dup(STDERR_FILENO);
        dup2(fd, STDERR_FILENO);
        redir.stderr_redirected = true;
        close(fd);
        redirecting = true;

    }
    if ((pos = line.find("&>")) != std::string::npos) {
        restore_redirection(redir);
        std::string file = line.substr(pos + 2);
        tmp_line = line.substr(0, pos);
        file.erase(0, file.find_first_not_of(" \t"));
        file = file.substr(0, file.find_first_of(" \t"));
        int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("Failed to open file for combined redirection");
            return redir;
        }
        redir.stdout_backup = dup(STDOUT_FILENO);
        redir.stderr_backup = dup(STDERR_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        redir.stdout_redirected = true;
        redir.stderr_redirected = true;
        close(fd);
        redirecting = true;

    }
    line = tmp_line;
    return redir;
}



my_shell::my_shell(int argc, char** argv)
{
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.length() > 2 && arg.substr(arg.length() - 2) == "sh") {
                run_script(arg);
            }
        }
        exit(0);
    }
    auto mpwd_f = [&](const std::vector<std::string>& args, const Redirection& redir) {
        mpwd(args, redir);
    };
    internal_cmds_m["mpwd"] = mpwd_f;

    auto mcd_f = [&](const std::vector<std::string>& args, const Redirection& redir) {
        mcd(args, redir);
    };
    internal_cmds_m["mcd"] = mcd_f;

    auto merrno_f = [&](const std::vector<std::string>& args, const Redirection& redir) {
        merrno(args, redir);
    };
    internal_cmds_m["merrno"] = merrno_f;

    auto mexit_f = [&](const std::vector<std::string>& args, const Redirection& redir) {
        mexit(args, redir);
    };
    internal_cmds_m["mexit"] = mexit_f;

    auto mecho_f = [&](const std::vector<std::string>& args, const Redirection& redir) {
        mecho(args, redir);
    };
    internal_cmds_m["mecho"] = mecho_f;

    auto point_f = [&](const std::vector<std::string>& args, const Redirection& redir) {
        point(args, redir);
    };
    internal_cmds_m["."] = point_f;

    auto mexport_f = [&](const std::vector<std::string>& args, const Redirection& redir) {
        mexport(args, redir);
    };
    internal_cmds_m["mexport"] = mexport_f;

    std::string old_path = std::getenv("PATH");
    std::string cwd = std::filesystem::canonical("/proc/self/exe").parent_path().string();
    setenv("PATH", (cwd + ":" + old_path).c_str(), 1);
    if (signal(SIGCHLD, zombie_handler) == SIG_ERR) {
        perror("signal setup failed");
        exit(EXIT_FAILURE);
    }
}


std::vector<std::string> my_shell::convert_to_str_vec(const std::vector<char*>& char_vect) {
    std::vector<std::string> res;
    for (const char* c : char_vect) {
        if (c) {
            res.emplace_back(c);
        }
    }
    return res;
}

bool my_shell::parse_args(const std::vector<std::string>& args, 
                          const po::options_description& desc,
                          po::variables_map& vm) 
{
    try {
        po::parsed_options parsed = po::command_line_parser(args).options(desc).run();
        po::store(parsed, vm);
        po::notify(vm);
    } catch (const po::unknown_option& e) {
        std::cerr << "Error: Unknown option '" << e.get_option_name() << "'" << std::endl;
        last_status = ERROR::UknownOption;
        return false;  
    }

    if (vm.count("help")) {
        if (args.size() != 2) {
            std::cerr << "Error: Too many arguments" << std::endl;
            last_status = ERROR::TooManyArgs;
        } else {
            show_help(desc);
            last_status = 0;
        }
        return false; 
    }

    return true; 
}

void my_shell::show_help(const po::options_description& desc) {
    std::cout << desc << std::endl;
}

void my_shell::mpwd(const std::vector<std::string>& args, const Redirection& redir) {
    int stdout_fd = redir.stdout_fd;
    int stderr_fd = redir.stderr_fd;
    po::variables_map vm;
    po::options_description mpwd_desc("mpwd options");
    mpwd_desc.add_options()
        ("help,h", "Print the current working directory");

    if (!parse_args(args, mpwd_desc, vm)) return;
    if (is_background && stdout_fd==1 && stderr_fd==2 && !redirecting) {
        is_background = false;
        redirecting = false;
        return;
    }

    if (args.size() == 1) {
        const size_t size = 1024; 
        char buffer[size];        

        if (getcwd(buffer, size) != NULL) {
            dprintf(stdout_fd, "%s\n", buffer);
            last_status = 0;
        } else {
            dprintf(stderr_fd, "Error: Failed to get current directory\n");
            last_status = ERROR::Other;
        }
    } else {
        dprintf(stderr_fd, "Error: Too many arguments\n");
        last_status = ERROR::TooManyArgs;
    }
}


void my_shell::mcd(const std::vector<std::string>& args, const Redirection& redir) {
    int stdout_fd = redir.stdout_fd;
    int stderr_fd = redir.stderr_fd;
    po::variables_map vm;
    po::options_description mcd_desc("mcd options");
    mcd_desc.add_options()
        ("help,h", "Change the current directory");
    
    if (!parse_args(args, mcd_desc, vm)) return;

    
    if (args.size() == 2) {
        if (chdir(args[1].data()) == 0) {
            last_status = 0;
        } else {
            if (is_background && stdout_fd==1 && stderr_fd==2 && !redirecting) {
                is_background = false;
                redirecting = false;
        
                return;
            }
            dprintf(stderr_fd, "Error: Cannot cd to %s\n", args[1].c_str());
            last_status = ERROR::Other;
        }
    } else {
        if (is_background && stdout_fd==1 && stderr_fd==2 && !redirecting) {
            is_background = false;
            redirecting = false;
            return;
        }
        dprintf(stderr_fd, "Error: Too many arguments");
        last_status = ERROR::TooManyArgs;
    }
}

void my_shell::merrno(const std::vector<std::string> &args, const Redirection& redir)
{
    int stdout_fd = redir.stdout_fd;
    int stderr_fd = redir.stderr_fd;
    po::variables_map vm;
    po::options_description merrno_desc("merrno options");
    merrno_desc.add_options()
        ("help,h", "Print the error code of the last command");

    if (!parse_args(args, merrno_desc, vm)) return;
    if (is_background && stdout_fd==1 && stderr_fd==2 && !redirecting) {
        is_background = false;
        redirecting = false;
        return;
    }
    
    if (args.size() == 1) {
        dprintf(stdout_fd, "%d", last_status);
        last_status = 0;
    } else {
        dprintf(stderr_fd, "Error: Too many arguments");
        last_status = ERROR::TooManyArgs;
    }
}


void my_shell::mexit(const std::vector<std::string> &args, const Redirection& redir)
{
    int stdout_fd = redir.stdout_fd;
    int stderr_fd = redir.stderr_fd;
    po::variables_map vm;
    po::options_description mexit_desc("mexit options");
    int exit_status = 0;
    mexit_desc.add_options()
        ("help,h", "Exit the shell")
        ("status,s", po::value<int>(&exit_status)->default_value(0), "Exit status");

    po::parsed_options parsed = po::command_line_parser(args).options(mexit_desc).run();
    po::store(parsed, vm);
    po::notify(vm);

    if (vm.count("help")) {
        show_help(mexit_desc);
    } else {
        exit(exit_status);
    }
}

void my_shell::mecho(const std::vector<std::string> &args, const Redirection& redir)
{
    int stdout_fd = redir.stdout_fd;
    int stderr_fd = redir.stderr_fd;
    po::variables_map vm;
    po::options_description mecho_desc("mecho options");
    mecho_desc.add_options()
        ("help,h", "Ouput arguments to the standard output");

    if (!parse_args(args, mecho_desc, vm)) return;
    if (is_background && stdout_fd==1 && stderr_fd==2 && !redirecting) {
        is_background = false;
        redirecting = false;
        return;
    }
    for (const auto& arg : args) {
        if (arg == "mecho" || arg == "-h" || arg == "--help") continue;
        dprintf(stdout_fd, "%s ", arg.c_str());
    }
    dprintf(stdout_fd, "\n");
    last_status = 0;
}


void my_shell::point(const std::vector<std::string>& args, const Redirection& redir) {
    int stdout_fd = redir.stdout_fd;
    int stderr_fd = redir.stderr_fd;
    po::variables_map vm;
    po::options_description point_desc("point options");
    point_desc.add_options()
        ("help,h", "run_external commands from a file in the current shell");

    if (!parse_args(args, point_desc, vm)) return;
    if (is_background && stdout_fd==1 && stderr_fd==2 && !redirecting) {
        is_background = false;
        redirecting = false;
        return;
    }

    if (args.size() == 1) {
        dprintf(stderr_fd, "Error: Too few arguments");
        last_status = ERROR::WrongArgCount;
        return;
    }

    for (auto& filename : args) {
        if (filename == "point" || filename == "-h" || filename == "--help") continue;
        run_script(filename);
    } 
}

void my_shell::mexport(const std::vector<std::string>& args, const Redirection& redir) {
    int stdout_fd = redir.stdout_fd;
    int stderr_fd = redir.stderr_fd;
    std::string arg = args[0].substr(8);
    po::variables_map vm;
    po::options_description mexport_desc("mexport options");
    mexport_desc.add_options()
        ("help,h", "Export environment variables");

    if (!parse_args(args, mexport_desc, vm)) return;
    if (arg.empty()) {
        dprintf(stderr_fd, "Error: Too few arguments");
        last_status = ERROR::WrongArgCount;
        return;
    }
    size_t pos = arg.find('=');
    if (pos == std::string::npos) {
        dprintf(stderr_fd, "Error: Invalid argument");
        last_status = ERROR::Other;
        return;
    }
    std::string key = arg.substr(0, pos);
    std::string value = arg.substr(pos + 1);
    auto cmd_start = value.find("$(");
    auto cmd_end = value.find(")", cmd_start);
    if (cmd_start != std::string::npos && cmd_end != std::string::npos && cmd_start < cmd_end) {
        std::string cmd = value.substr(cmd_start + 2, cmd_end - cmd_start - 2);

        int pipefd[2];
        if (pipe(pipefd) == -1) {
            dprintf(stderr_fd, "mexport: pipe fail\n");
            last_status = ERROR::Other;
            return;
        }

        pid_t pid = fork();
        if (pid == -1) {
            dprintf(stderr_fd, "mexport: fork fail\n");
            last_status = ERROR::Other;
            return;
        } else if (pid == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            Redirection redir;
            execute(cmd, redir);
            exit(0);
        } else {
            close(pipefd[1]);
            char buffer[128];
            std::string result;
            ssize_t count;
            while ((count = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
                result.append(buffer, count);
            }
            close(pipefd[0]);
            waitpid(pid, nullptr, 0);
            for (size_t i = result.size()-1; i > 0; --i) {
                if (result[i] == '\n') result.erase(i);
                else break;
            }
            value.replace(cmd_start, cmd_end - cmd_start+1, result);
        }
    }
    setenv(key.c_str(), value.c_str(), 1);
    last_status = 0;
}


std::string my_shell::read_line() {
    char* line = readline((std::filesystem::current_path().string() + " $ ").c_str());
    if (line == nullptr) {
        return "";
    }
    
    std::string result = line;
    
    if (!result.empty()) {
        add_history(line);
    }
    
    free(line); 
    return result;
}

std::pair<std::vector<char *>, std::string> my_shell::split_line(const std::string& line) {
    std::vector<char *> args;
    
    auto line_stripped = line.substr(0, line.find('#'));
    if (line_stripped.empty()) return std::make_pair(args, "");
    std::istringstream iss(line_stripped);
    std::string token;
    std::vector<std::string> tokens;

    

    while (iss >> token) {
        if (token[0]=='$'){
            const char* env_var = std::getenv(token.substr(1).c_str());
            if (env_var) token = env_var;            
        }
        tokens.push_back(token);
    }

    std::string input_file;
    auto it = std::find(tokens.begin(), tokens.end(), "<");
    if (it != tokens.end() && std::next(it) != tokens.end()) {
        input_file = *(std::next(it));
        tokens.erase(it, std::next(it, 2));
    }
    for (auto& tok : tokens) {
        if (tok.find('*') != std::string::npos || tok.find('?') != std::string::npos || tok.find('[') != std::string::npos) {
            glob_t glob_result;
            glob(tok.c_str(), GLOB_TILDE, nullptr, &glob_result);

            if (glob_result.gl_pathc > 0) {
                for (size_t i = 0; i < glob_result.gl_pathc; ++i) {
                    args.push_back(strdup(glob_result.gl_pathv[i]));
                }
            } else {
                args.push_back(strdup(tok.c_str()));
            }
            globfree(&glob_result);
        } else {
            args.push_back(strdup(tok.c_str()));
        }
    }
    args.push_back(nullptr);
    if (!input_file.empty()) {
        return std::make_pair(args, input_file);
    }

    return std::make_pair(args, "");
}

std::vector<std::string> my_shell::split_pipe(const std::string &line)
{
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string token;

    while (std::getline(ss, token, '|')) {
        tokens.push_back(token); 
    }

    return tokens;
}

void my_shell::run_script(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filename << std::endl;
        last_status = ERROR::FileNotFound;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        Redirection redir = handle_redirection(line);
        auto [args, input_file] = split_line(line);
        if (args.empty()) continue;
        if (internal_cmds_m.find(args[0]) != internal_cmds_m.end()) {
            auto f = internal_cmds_m[args[0]];
            auto str_vec = convert_to_str_vec(args);
            run_internal(f, str_vec, redir);
        } else {
            run_external(args, input_file);
        }
        restore_redirection(redir);
    }
    file.close();
}



void my_shell::run_external(std::vector<char*>& args, const std::string& input_file) {
    pid_t pid, wpid;
    int status;
    pid = fork();
    if (pid == 0) {
        if (!input_file.empty()) {
            int input_fd = open(input_file.c_str(), O_RDONLY);
            if (input_fd == -1) {
                perror("Cannot open file for input");
                exit(1);
            }
            if (dup2(input_fd, 0) == -1) {
                perror("Couldn't change input direction");
                exit(1);
            }
            close(input_fd);
        }


        if (is_background && !redirecting) {
            close(0);
            close(1);
            close(2);
        }
        redirecting = false;

        if (execvpe(args[0], args.data(), environ) == -1) {
            perror("execv failed");
            exit(0);
        }
    } else if (pid > 0) {
            if (!is_background) {
            waitpid(pid, &status, 0);
            if ( WIFEXITED(status) )
                last_status = WEXITSTATUS(status);        
            }
    } else {
        perror("fork failed");
    }
}

void my_shell::run_internal(std::function<void(const std::vector<std::string>&, const Redirection&)>& f,
                            const std::vector<std::string>& args, const Redirection& redir) {
    f(args, redir);
}

void my_shell::pipe_execute(std::vector<std::string>& pipe_line, Redirection& redir) {
    size_t i;
    int status;
    int fd[2];

    int stdout_d = dup(1);
    int stdin_d = dup(0);


    for( i=0; i < pipe_line.size()-1; ++i)
    {
        auto cmd = pipe_line[i];
        auto [args, input_file] = split_line(cmd);
        pipe(fd);
        if (!fork()) {
            dup2(fd[1], 1);
            close(fd[0]);
            if (internal_cmds_m.find(args[0]) != internal_cmds_m.end() && i == 0) {
                auto f = internal_cmds_m[args[0]];
                auto str_vec = convert_to_str_vec(args);
                Redirection redir_internal;
                redir_internal.stdout_redirected = true;
                redir_internal.stdout_fd = fd[1];
                run_internal(f, str_vec, redir_internal);
                exit(0);
            } else{
                execlp(args[0], args[0], NULL);
                perror("exec");
                abort();
            }
        }
        dup2(fd[0], 0);
        close(fd[1]);
        waitpid(-1, &status, 0);
    }

    if (!fork()) {
        auto cmd = pipe_line[pipe_line.size()-1];
        auto [args, input_file] = split_line(cmd);
        if (redir.stdout_redirected) {
            dup2(redir.stdout_fd, 1);
        }
        execlp(args[0], args[0], NULL);
        perror("exec");
    } else {
        wait(nullptr);
    }
    dup2(stdout_d, 1);
    dup2(stdin_d, 0);
    restore_redirection(redir);

}

void my_shell::execute(std::string& line, Redirection& redir) {
    auto [args, input_file] = split_line(line);
    if (args.empty()) return;
    std::string arg = args[0];
    if (internal_cmds_m.find(arg) != internal_cmds_m.end()) {
        auto f = internal_cmds_m[arg];
        if (arg=="mexport") {
            mexport({line}, redir);
        } else {
            auto str_vec = convert_to_str_vec(args);
            run_internal(f, str_vec, redir);
        }
    } else {
        run_external(args, input_file);
    }
    restore_redirection(redir);
}


void my_shell::run() {
    read_history("history.txt");
    while (true) {
        auto line = read_line();
        if (line.empty()) continue;

        auto pipe_line = split_pipe(line);
        auto [last_args, input_file] = split_line(pipe_line[pipe_line.size()-1]);

        if (strcmp(last_args[last_args.size()-2], "&") == 0) {
            is_background = true;
            last_args.pop_back();
            last_args[last_args.size()-1] = nullptr;
        } else is_background = false;
        
        Redirection redir = handle_redirection(line);

        if ( pipe_line.size() > 1 ) pipe_execute(pipe_line, redir);
        else execute(line, redir);

        redirecting = false;
    }
    write_history("history.txt");
}
