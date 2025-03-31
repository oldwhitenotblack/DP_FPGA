#include "..\include\global.h"
// #include<sys/mman.h>
#include<sys/stat.h>
#include "Database.h"
using namespace db;

class FileNetManage {
public:
    string filename = "D:\\contest\\fpga\\code\\FPGA_pack_LUTFF\\edacomoetition-Version1.0\\Benchmark\\public_release";
    bool readFileNodes(std::string benchmark) {
        string nodes_filename = filename + "\\case_"+benchmark+".nodes";
        ifstream fs(nodes_filename);
        if (!fs.is_open()) {
            cerr<<"Read net file: "<<nodes_filename<<" fail"<<endl;
            return false;
        }
        std::string line;
        bool parsingInstances = false;
        while (std::getline(fs, line)) {
            if (line.find('#') == 0) {
                // �����պ�ע��
                continue;
            }
            // if (line.find("List of Models in This Case") != std::string::npos) {
            //     continue; // ����������
            // }
            if (line.empty() && !parsingInstances) {
                parsingInstances = true;
                continue;
            }
            if (parsingInstances) {
                std::istringstream iss(line);
                std::string position;
                std::string mastername;
                std::string instancename;
                std::string attr;

                // ����λ����Ϣ����Ԫ���͡���Ԫ���ƺ�����
                if (iss >> position >> mastername >> instancename) {
                    // �������ԣ�����еĻ���
                    if (iss >> attr) {
                        attr = "FIXED";
                    } else {
                        attr = "";
                    }
                    int xpos = position.find("X");
                    int ypos = position.find("Y");
                    int zpos = position.find("Z");
                    int x =  std::stoi(position.substr(1,ypos-xpos));
                    int y = std::stoi(position.substr(ypos+1,zpos-ypos));
                    int z = std::stoi(position.substr(zpos+1));
                    Instance *instance = database.getInstance(instancename);
                    if(instance != NULL){
                        // ˵�����instance�Ѿ�����ȡ���ˣ��Ǿͳ�������
                        return false;
                    }  else{
                        Master *master = database.getMaster(Master::NameString2Enum(mastername));
                        if(master == NULL){
                            return false;
                        } else{
                            // std::cout << instancename << ',' << mastername<< std::endl;
                            Instance newinstance(instancename, master);
                            instance = database.addInstance(newinstance);
                        }
                    }
                    if (attr != "") {
                        instance->fixed = true;
                    }
                    instance->x = x;
                    instance->y = y;
                    instance->z = z;
                    Site * site = database.getSite(x,y);
                    // cout << x << ',' << y << ',' << SiteType::NameEnum2String(site->type->name) << endl;
                    if (instance->master->name == Master::IPPIN && site->type->name != SiteType::IPPIN) {
                        // cout << x << ',' << y << instance->name << endl;
                        site = site->formuti;
                    }
                    // site->pack = new Pack(site->type,site);
                    instance->pack = site->pack;
                    // site->pack->site = site;
                    // �¼ӵģ���Ϊ������dp������ֻ��Ҫ��ȡ��ǰ�����siteȻ����뵽pack��
                    site->pack->addInstance(instance);
                    // std::cout << site->x << ',' << site->y << std::endl;
                }
            }
        }
        fs.close();
        std::cout << "reading .nodes successfully!" << std::endl;
        return true;
    }
    bool readFileTiming(std::string benchmark) {
        string timing_filename = filename + "\\case_"+benchmark+".timing";
        std::ifstream fs(timing_filename);
        if (!fs.is_open()) {
            cerr<<"Read net file: "<<timing_filename<<" fail"<<endl;
            return false;
        }
        std::string line;
        while (std::getline(fs, line)) {
            if (line.empty()) {
                continue; // ��������
            }
            std::istringstream iss(line);
            std::string instName, pinName;
            iss >> instName >> pinName;
            Instance *instance = database.getInstance(instName);
            PinType * pintype = instance->getPin(pinName)->type;
            pintype->isTiming = true;
            Pin * pin = instance->getPin(pinName);
            pin->isTiming = true;
        }
        fs.close();
        std::cout << "reading .timing successfully!" << std::endl;
        return true;
    }
    bool readFileNets(std::string benchmark) {
        if (readFileTiming(benchmark)==false) {
            return false;
        }
        string nets_filename = filename + "\\case_"+benchmark+".nets";
        // vector<InstanceInfo> instances;
        ifstream fs(nets_filename);
        if (!fs.is_open()) {
            cerr<<"Read net file: "<<nets_filename<<" fail"<<endl;
            return false;
        }
        std::string line;
        Net *net = NULL;
        while (std::getline(fs, line)) {
            // ����ע���кͿ���
            if (line.empty()) {
                continue;
            }
            // ���net�Ŀ�ʼ
            if (line.find("net") == 0) {
                std::istringstream iss(line);
                std::string netPrefix;
                std::string netname;
                std::string arr = "";
                int totalpins;
                iss >> netPrefix >> netname >> totalpins;
                net = database.getNet(netname);
                if(net != NULL){
                    return false;
                }else{
                    Net newnet(netname);
                    net = database.addNet(newnet);
                }
                // ����Ƿ�����������
                if (iss >> arr) {
                    // ���Կ��������ֻ����ַ����������߼����Ը�����Ҫ����
                    net->isClk = true;
                } else {
                    arr = ""; // ����������
                }

                // ��ȡpin��Ϣ
                while (std::getline(fs, line) && line.find("endnet") == std::string::npos) {
                    if (line.empty()) {
                        continue; // ��������
                    }
                    std::string insname;
                    std::string pinname;
                    std::istringstream iss(line);
                    iss >> insname >> pinname;
                    Instance *instance = database.getInstance(insname);
                    Pin *pin = NULL;
                    if(instance != NULL){
                        pin = instance->getPin(pinname);
                        // std::cout << pinname << ',' << pinname << std::endl;
                    }else{
                        return false;
                    }
                    if(pin == NULL){
                        return false;
                    }else{
                        net->addPin(pin);
                    }
                }
            }
        }
        fs.close();
        std::cout << "reading .nets successfully!" << std::endl;
        return true;
    }

};