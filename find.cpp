#include <iostream>
#include <vector>
#include <unistd.h>
#include <sstream>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <sys/wait.h>
#include <dirent.h>
#include <queue>

const std::string MANUAL = "Welcome to find util created by SP4RK!\n"
        "usage: find initialPath [-inum num | -name name | -size [-=+]size | -nlinks num | -exec path";
enum sizeRequestType {
    NONE, EQUAL, LESS, GREATER
};

long long parseNumber(std::string& number) {
    try {
        return std::stoll(number);
    } catch (std::invalid_argument& ia) {
        std::cerr << "Invalid argument passed as a number: " << ia.what() << std::endl;
        exit(EXIT_FAILURE);
    }
}

struct RequestInfo {
    std::string name = "";
    nlink_t nLinks = 0;
    ino_t iNodes = 0;
    off_t size = 0;
    bool nameFilter = false, nLinksFilter = false, iNodesFilter = false, sizeFilter = false, ifExecNeeded = false;
    std::string execPath;
    sizeRequestType sizeType = NONE;

    void addFilter(std::string& filterName, std::string& filterValue) {
        if (filterName == "-inum") {
            iNodesFilter = true;
            iNodes = static_cast<ino_t>(parseNumber(filterValue));
        } else if (filterName == "-name") {
            nameFilter = true;
            name = filterValue;
        } else if (filterName == "-size") {
            sizeFilter = true;
            char sizeT = filterValue[0];
            std::string sizeN = filterValue.substr(1);
            size = static_cast<off_t>(parseNumber(sizeN));
            switch (filterValue[0]) {
                case '+':
                    sizeType = GREATER;
                    break;
                case '-':
                    sizeType = LESS;
                    break;
                case '=':
                    sizeType = EQUAL;
                    break;
                default:
                    //Shouldn't happen
                    break;
            }
        } else if (filterName == "-nlinks") {
            nLinksFilter = true;
            nLinks = static_cast<nlink_t>(parseNumber(filterValue));
        } else if (filterName == "-exec") {
            ifExecNeeded = true;
            execPath = filterValue;
        }
    }
};

struct File {
    std::string name;
    std::string fullPath;
    nlink_t nLinks;
    ino_t iNodes;
    off_t size;

    File(const struct stat& fileStat, std::string& fileName, std::string& path) {
        nLinks = fileStat.st_nlink;
        iNodes = fileStat.st_ino;
        size = fileStat.st_size;
        name = fileName;
        fullPath = path;
    }

    bool ifMatchFilter(const RequestInfo& filter) {
        if (filter.iNodesFilter && filter.iNodes != iNodes) {
            return false;
        }
        if (filter.nLinksFilter && filter.nLinks != nLinks) {
            return false;
        }
        if (filter.nameFilter && filter.name != name) {
            return false;
        }
        if (filter.sizeFilter) {
            switch (filter.sizeType) {
                case EQUAL:
                    return filter.size == size;
                case LESS:
                    return size < filter.size;
                case GREATER:
                    return size > filter.size;
                default:
                    //How did we get here? O_o
                    break;
            }
        }
        return true;
    }
};

std::vector<char*> getConvertedArgs(std::vector<File>& args) {
    std::vector<char*> arguments;
    for (auto& arg: args) {
        arguments.push_back(&arg.fullPath[0]);
    }
    arguments.push_back(nullptr);
    return arguments;
}

void fileBfs(std::string& curPath, const RequestInfo& requestInfo, std::vector<File>& result) {
    DIR* curDir;
    std::queue<std::string> q;
    q.push(curPath);
    while (!q.empty()) {
        curPath = q.front();
        curDir = opendir(curPath.c_str());
        q.pop();
        if (curDir == nullptr) {
            continue;
        }
        while (dirent* fileInDir = readdir(curDir)) {
            std::string fileName = fileInDir->d_name;
            if (fileName == "." || fileName == "..") {
                continue;
            }
            struct stat fileInfo{};
            std::string pathOfFile(curPath);
            pathOfFile.append(pathOfFile.back() == '/' ? "" : "/").append(fileName);
            if (stat(pathOfFile.c_str(), &fileInfo) == -1) {
                std::cerr << "Can't access information of file at path: " << pathOfFile << " " << strerror(errno)
                          << std::endl;
                continue;
            }
            File file(fileInfo, fileName, pathOfFile);
            if (fileInDir->d_type == DT_DIR) {
                q.emplace(pathOfFile);
            } else if (file.ifMatchFilter(requestInfo)) {
                result.push_back(file);
            }
        }
	closedir(curDir);
    }
}

void execute(std::string& path, std::vector<File> arguments) {
    pid_t pid = fork();
    if (pid == -1) {
        std::cerr << "Couldn't fork current process: " << strerror(errno) << std::endl;
    } else if (pid == 0) {
        std::vector<char*> convertedArguments = getConvertedArgs(arguments);
        if (execve(path.c_str(), convertedArguments.data(), nullptr) == -1) {
            std::cerr << "Couldn't execute file: " << strerror(errno) << std::endl;
            exit(EXIT_FAILURE);
        }
    } else {
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            std::cerr << "Error while waiting for child return";
        } else {
            std::cout << "Program finished with code: " << WEXITSTATUS(status) << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc % 2 == 1) {
        std::cerr << "Wrong number of arguments, type 'help' to see usage" << std::endl;
        exit(EXIT_FAILURE);
    }
    std::vector<std::string> programArguments;
    for (int i = 1; i < argc; ++i) {
        programArguments.emplace_back(argv[i]);
    }
    if (programArguments[0] == "exit") {
        exit(EXIT_SUCCESS);
    } else if (programArguments[0] == "help") {
        std::cout << MANUAL << std::endl;
        exit(EXIT_SUCCESS);
    }
    std::string initialPath = programArguments[0];
    std::vector<File> resultFiles;
    RequestInfo requestInfo;
    for (int i = 1; i < programArguments.size(); i += 2) {
        requestInfo.addFilter(programArguments[i], programArguments[i + 1]);
    }
    fileBfs(initialPath, requestInfo, resultFiles);
    if (requestInfo.ifExecNeeded) {
        execute(requestInfo.execPath, resultFiles);
    } else {
        std::cout << resultFiles.size() << " files found for your query:" << std::endl;
        for (const File& f : resultFiles) {
            std::cout << f.fullPath << std::endl;
        }
    }
}