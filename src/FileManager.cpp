#include "..\include\global.h"
// #include<sys/mman.h>
#include<sys/stat.h>
#include <iostream>
#include <sstream>
class FileManager {
// 结构体定义PIN
struct Pin {
    std::string name;
    std::string type;
    std::string attribute;
};

// 结构体定义CELL
struct Cell {
    std::string name;
    std::vector<Pin> pins;
};
struct Site
{
    std::string name;
    int width;
    int height;
    map<string,int> capacity;
    /* data */
};
struct Position {
    Site site;
    int posx;
    int posy;
    std::string name;
};
struct ClockRegion {
    std::string name;
    int x1, x2, y1, y2;
    int width, height;
};
public:
    // 解析文件并提取信息
    string filename = "../Arch";
    bool readFileLib() {
        string lib_filename = filename + "/fpga.lib";
        std::vector<Cell> cells;
        int fd = open(lib_filename.c_str(), O_RDONLY);
        if (fd < 0) {
            cout << "OPEN FILE ERROR!" << endl;
            return false;
        }
        struct stat fileStat;
        if (fstat(fd, &fileStat) == -1) {
            cout << "CAN'T GET SIZE OF THE FILE" << endl;
            close(fd);
            return false;
        }
        int flieLength = fileStat.st_size;
        char *buffer = (char *) mmap(NULL, fileLength, PROT_READ, MAP_SHARED, fd, 0);
        close(fd);
        if (buffer == MAP_FAILED) {
            cout << "CAN'T MAP FILE TO THE MEMORY" << strerror(errno) << endl;
            return false;
        }
        size_t pos = 0;
        bool insideCell = false;
        
        Cell currentCell;
        std::string content(buffer, flieLength);
        while (pos < content.size()) {
            size_t endLine = content.find('\n', pos); //找到换行符的位置
            std::string line = content.substr(pos, endLine - pos); //将一行拼接成字符串
            pos = (endLine == std::string::npos) ? content.size() : endLine + 1; //移动位置，如果后面没有换行符了说明处理完毕了，则设置到达结尾，否则指向下一行
            if (line.find("CELL") == 0) { //找到CELL
                if (insideCell) { //避免识别到结尾的END_CELL
                    // 将解析好的CELL添加到列表
                    cells.push_back(currentCell);
                }
                // 开始一个新CELL
                currentCell = Cell(); // 初始化当前单元
                currentCell.name = line.substr(5); // CELL名从第6个字符开始
                insideCell = true;
            } else if (line.find("PIN") != line.npos) {
                Pin pin;
                size_t firstSpace = line.find(' ', 6);
                size_t secondSpace = line.find(' ', firstSpace + 1);
                pin.name = line.substr(6, firstSpace - 6);
                pin.type = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);

                // 检查是否有属性
                if (secondSpace != std::string::npos) {
                    pin.attribute = line.substr(secondSpace + 1);
                } else {
                    pin.attribute = "None";
                }
                currentCell.pins.push_back(pin);
            } else if (line.find("END_CELL") == 0) {
                if (insideCell) {
                    cells.push_back(currentCell);
                    insideCell = false;
                }
            }
        }

        // 如果最后还有一个未保存的CELL，保存它
        if (insideCell) {
            cells.push_back(currentCell);
        }
        return true;
    }
    bool readFileScl() {
        std::vector<Site> sites;
        string scl_filename = filename + "/fpga.scl";
        int fd = open(scl_filename.c_str(), O_RDONLY);
        if (fd < 0) {
            cout << "OPEN FILE ERROR!" << endl;
            return false;
        }
        struct stat fileStat;
        if (fstat(fd, &fileStat) == -1) {
            cout << "CAN'T GET SIZE OF THE FILE" << endl;
            close(fd);
            return false;
        }
        int flieLength = fileStat.st_size;
        char *buffer = (char *) mmap(NULL, fileLength, PROT_READ, MAP_SHARED, fd, 0);
        close(fd);
        if (buffer == MAP_FAILED) {
            cout << "CAN'T MAP FILE TO THE MEMORY" << strerror(errno) << endl;
            return false;
        }
        size_t pos = 0;
        bool insideSite = false;
        std::vector<Site> sites;
        Site currentSite;
        std::string content(buffer, flieLength);
        bool hasReachCapacity = false;
        int firstEndsize = content.find("SITEMAP");
        while (pos < firstEndsize) {
            size_t endLine = content.find('\n', pos); //找到换行符的位置
            std::string line = content.substr(pos, endLine - pos); //将一行拼接成字符串
            pos = endLine + 1; //移动位置，指向下一行,如果其超过firstEndsize，那么直接退出循环
            if (line.find("SITE") != line.npos) { //找到SITE
                if (insideSite) { //避免识别到结尾的END_CELL
                    // 将解析好的CELL添加到列表
                    sites.push_back(currentSite);
                }
                // 开始一个新CELL
                currentSite = Site(); // 初始化当前单元
                currentSite.name = line.substr(7); // Site名从第8个字符开始
                insideSite = true;
            } else if (line.find("WIDTH") != line.npos) {
                std::string name = "";
                int num = 0;
                size_t startpos = line.find("WIDTH");
                size_t firstSpace = line.find(' ', startpos);
                name = line.substr(startpos, firstSpace - startpos);
                num = std::stoi(line.substr(firstSpace + 1));
                currentSite.width = num;
                // currentSite.capacity.insert(std::make_pair(name,num));
            } else if (line.find("HEIGHT") != line.npos) {
                std::string name = "";
                int num = 0;
                size_t startpos = line.find("HEIGHT");
                size_t firstSpace = line.find(' ', startpos);
                name = line.substr(startpos, firstSpace - startpos);
                num = std::stoi(line.substr(firstSpace + 1));
                currentSite.width = num;
                // currentSite.capacity.insert(std::make_pair(name,num));
            } else if (line.find("CAPACITY") != line.npos||line.find("SETTING LOGIC") != line.npos) {
                hasReachCapacity = true;
                // currentSite.capacity.insert(std::make_pair(name,num));
            } else if (hasReachCapacity) {
                size_t startpos = line.find_first_not_of(" \t",2); //第一个不是空格的字符(从第二个字符开始找挑过#)
                std::string name = "";
                int num = 0;
                size_t firstSpace = line.find(' ', startpos);
                name = line.substr(startpos, firstSpace - startpos);
                num = std::stoi(line.substr(firstSpace + 1));
                currentSite.capacity.insert(std::make_pair(name,num));
            } else if (line.find("END_SITE") == 0) {
                if (insideSite) {
                    sites.push_back(currentSite);
                    insideSite = false;
                    hasReachCapacity = false;
                }
            }
        }
        // 如果最后还有一个未保存的CELL，保存它
        if (insideSite) {
            sites.push_back(currentSite);
        }
        pos = firstEndsize;
        vector<Position> logicpos;
        while (pos < content.size()) {
            size_t endLine = content.find('\n', pos); //找到换行符的位置
            std::string line = content.substr(pos, endLine - pos); //将一行拼接成字符串
            pos = (endLine == std::string::npos) ? content.size() : endLine + 1;
            if (line.find("SITEMAP") == 0) { //找到CELL
                size_t firstSpace = line.find(' ', 3);
                size_t secondSpace = line.find(' ', firstSpace + 1);
                int length = std::stoi(line.substr(firstSpace + 1, secondSpace - firstSpace - 1));
                int weight = std::stoi(line.substr(secondSpace + 1));
            } else if (line.find("X") != line.npos) {
                Position poslogic;
                size_t xsite = line.find("X")+1;
                size_t ysite = line.find("Y")+1;
                int x = std::stoi(line.substr(xsite,1));
                int y = std::stoi(line.substr(ysite,1));
                std::string name = line.substr(line.find_first_not_of(" \t",ysite+1));
                poslogic.posx = x;
                poslogic.posy = y;
                poslogic.name = name;
            }
        }
        return true;
    }
    bool readFileClk() {
        string clk_filename = filename + "/fpga.clk";
        vector<ClockRegion> clockRegions;
        int fd = open(clk_filename.c_str(), O_RDONLY);
        if (fd < 0) {
            cout << "OPEN FILE ERROR!" << endl;
            return false;
        }
        struct stat fileStat;
        if (fstat(fd, &fileStat) == -1) {
            cout << "CAN'T GET SIZE OF THE FILE" << endl;
            close(fd);
            return false;
        }
        int flieLength = fileStat.st_size;
        char *buffer = (char *) mmap(NULL, fileLength, PROT_READ, MAP_SHARED, fd, 0);
        close(fd);
        if (buffer == MAP_FAILED) {
            cout << "CAN'T MAP FILE TO THE MEMORY" << strerror(errno) << endl;
            return false;
        }
        size_t pos = 0;
        std::string content(buffer, flieLength);
        // 查找"CLOCKREGIONS"开头的行
        size_t clockRegionsPos = content.find("CLOCKREGIONS", pos);
        if (clockRegionsPos == std::string::npos) {
            return false;  // 如果没有找到CLOCKREGIONS, 返回空
        }
        pos = clockRegionsPos; // 将pos移动到"CLOCKREGIONS"处
        // 移动pos到下一行，跳过CLOCKREGIONS的行
        size_t endLine = content.find('\n', pos);
        pos = (endLine == std::string::npos) ? content.size() : endLine + 1;
        while (pos < content.size()) {
            endLine = content.find('\n', pos);
            std::string line = content.substr(pos, endLine - pos);
            pos = (endLine == std::string::npos) ? content.size() : endLine + 1;

            // 检查是否遇到"END_CLOCKREGIONS"，则结束解析
            if (line.find("END_CLOCKREGIONS") != std::string::npos) {
                break;
            }

            // 跳过空行或无效行
            if (line.empty()) {
                continue;
            }

            // 使用字符串流解析行内容
            std::istringstream iss(line);
            std::string regionName;
            int x1, x2, y1, y2;

            iss >> regionName >> x1 >> x2 >> y1 >> y2;

            // 计算宽度和高度
            int width = x2 - x1 + 1;
            int height = y2 - y1 + 1;

            // 存储解析后的时钟区域信息
            clockRegions.push_back({regionName, x1, x2, y1, y2, width, height});
        }
        return true;
    }
};