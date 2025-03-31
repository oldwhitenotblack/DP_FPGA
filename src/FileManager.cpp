#include "..\include\global.h"
// #include<sys/mman.h>
#include<sys/stat.h>
#include <iostream>
#include <sstream>
class FileManager {
// �ṹ�嶨��PIN
struct Pin {
    std::string name;
    std::string type;
    std::string attribute;
};

// �ṹ�嶨��CELL
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
    // �����ļ�����ȡ��Ϣ
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
            size_t endLine = content.find('\n', pos); //�ҵ����з���λ��
            std::string line = content.substr(pos, endLine - pos); //��һ��ƴ�ӳ��ַ���
            pos = (endLine == std::string::npos) ? content.size() : endLine + 1; //�ƶ�λ�ã��������û�л��з���˵����������ˣ������õ����β������ָ����һ��
            if (line.find("CELL") == 0) { //�ҵ�CELL
                if (insideCell) { //����ʶ�𵽽�β��END_CELL
                    // �������õ�CELL��ӵ��б�
                    cells.push_back(currentCell);
                }
                // ��ʼһ����CELL
                currentCell = Cell(); // ��ʼ����ǰ��Ԫ
                currentCell.name = line.substr(5); // CELL���ӵ�6���ַ���ʼ
                insideCell = true;
            } else if (line.find("PIN") != line.npos) {
                Pin pin;
                size_t firstSpace = line.find(' ', 6);
                size_t secondSpace = line.find(' ', firstSpace + 1);
                pin.name = line.substr(6, firstSpace - 6);
                pin.type = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);

                // ����Ƿ�������
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

        // ��������һ��δ�����CELL��������
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
            size_t endLine = content.find('\n', pos); //�ҵ����з���λ��
            std::string line = content.substr(pos, endLine - pos); //��һ��ƴ�ӳ��ַ���
            pos = endLine + 1; //�ƶ�λ�ã�ָ����һ��,����䳬��firstEndsize����ôֱ���˳�ѭ��
            if (line.find("SITE") != line.npos) { //�ҵ�SITE
                if (insideSite) { //����ʶ�𵽽�β��END_CELL
                    // �������õ�CELL��ӵ��б�
                    sites.push_back(currentSite);
                }
                // ��ʼһ����CELL
                currentSite = Site(); // ��ʼ����ǰ��Ԫ
                currentSite.name = line.substr(7); // Site���ӵ�8���ַ���ʼ
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
                size_t startpos = line.find_first_not_of(" \t",2); //��һ�����ǿո���ַ�(�ӵڶ����ַ���ʼ������#)
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
        // ��������һ��δ�����CELL��������
        if (insideSite) {
            sites.push_back(currentSite);
        }
        pos = firstEndsize;
        vector<Position> logicpos;
        while (pos < content.size()) {
            size_t endLine = content.find('\n', pos); //�ҵ����з���λ��
            std::string line = content.substr(pos, endLine - pos); //��һ��ƴ�ӳ��ַ���
            pos = (endLine == std::string::npos) ? content.size() : endLine + 1;
            if (line.find("SITEMAP") == 0) { //�ҵ�CELL
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
        // ����"CLOCKREGIONS"��ͷ����
        size_t clockRegionsPos = content.find("CLOCKREGIONS", pos);
        if (clockRegionsPos == std::string::npos) {
            return false;  // ���û���ҵ�CLOCKREGIONS, ���ؿ�
        }
        pos = clockRegionsPos; // ��pos�ƶ���"CLOCKREGIONS"��
        // �ƶ�pos����һ�У�����CLOCKREGIONS����
        size_t endLine = content.find('\n', pos);
        pos = (endLine == std::string::npos) ? content.size() : endLine + 1;
        while (pos < content.size()) {
            endLine = content.find('\n', pos);
            std::string line = content.substr(pos, endLine - pos);
            pos = (endLine == std::string::npos) ? content.size() : endLine + 1;

            // ����Ƿ�����"END_CLOCKREGIONS"�����������
            if (line.find("END_CLOCKREGIONS") != std::string::npos) {
                break;
            }

            // �������л���Ч��
            if (line.empty()) {
                continue;
            }

            // ʹ���ַ���������������
            std::istringstream iss(line);
            std::string regionName;
            int x1, x2, y1, y2;

            iss >> regionName >> x1 >> x2 >> y1 >> y2;

            // �����Ⱥ͸߶�
            int width = x2 - x1 + 1;
            int height = y2 - y1 + 1;

            // �洢�������ʱ��������Ϣ
            clockRegions.push_back({regionName, x1, x2, y1, y2, width, height});
        }
        return true;
    }
};