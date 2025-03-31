#include "..\include\global.h"
// #include<sys/mman.h>
#include<sys/stat.h>
#include "Database.h"
using namespace db;
// �ṹ�嶨��PIN

class FileArchManage {
public:
    string filename = "D:\\contest\\fpga\\code\\FPGA_pack_LUTFF\\edacomoetition-Version1.0\\Arch";
    bool readFileLib() {
        string lib_filename = filename + "\\fpga.lib";
        ifstream fs(lib_filename);
        if (!fs.is_open()) {
            cerr<<"Read lib file: "<<lib_filename<<" fail"<<endl;
            return false;
        }
        std::string line;
        while (std::getline(fs, line)) {
            if (line.empty()) {
                continue;
            }
            if (line.find("CELL") == 0) {
                Master *master = NULL;
                // Cell currentCell;
                std::istringstream iss(line);
                std::string netPrefix;
                std::string mastername;
                iss >> netPrefix >> mastername;
                master = database.getMaster(Master::NameString2Enum(mastername)); //�������ֲ���ȡ
                if(master == NULL){
                    Master newmaster(Master::NameString2Enum(mastername));
                    master = database.addMaster(newmaster);                    
                    for (int i = 0; i < database.resources.size(); i++) {
                        if (master->name == Master::PLB) {
                            // std::cout << master->name << std::endl;
                            // Resource *resource = database.getResource(Resource::PLB);
                            // if (resource == NULL) {
                            Resource newresource(Resource::PLB);
                            Resource *resource = database.addResource(newresource);
                            // }
                            // cout << Resource::NameEnum2String(newresource.name) << endl;
                            bool isadd = master->addResource(resource);
                            if (isadd == true) break;
                            // cout << master->resource->name << endl;
                        } else {
                            bool isadd = master->addResource(database.resources[i]);
                            if (isadd == true) break;
                        }
                    }
                } else return false;
                while (std::getline(fs, line) && line.find("END_CELL") == std::string::npos) {
                    if (line.empty()) {
                        continue; // ��������
                    }
                    PinType pintype;
                    std::istringstream pinStream(line);
                    std::string pinPrefix;
                    std::string pinname;
                    std::string type;
                    std::string arr;
                    pinStream >> pinPrefix >> pinname >> type;
                    pintype.name = pinname;
                    pintype.type = type;
                    if (pinStream >> arr) {
                        pintype.arr = arr;
                    } else {
                        pintype.arr = "";
                    }
                    if (master != NULL) {
                        master->addPin(pintype);
                    }
                }
            }
        }
        fs.close();
        std::cout << "reading .lib successfully!" << std::endl;
        return true;
    }
    bool readFileScl() { 
        string scl_filename = filename + "\\fpga.scl";
        std::ifstream fs(scl_filename);
        if (!fs.is_open()) {
            cerr<<"Read scl file: "<<scl_filename<<" fail"<<endl;
            return false;
        }
        std::string line;
        while (std::getline(fs, line)) {
            if (line.empty()) {
                continue; // ��������
            }
            if (line.find("SITE ") != std::string::npos) {
                // std::cout << line << std::endl;
                std::istringstream iss(line);
                std::string sitePrefix,sitefix2;
                std::string sitename;
                iss >> sitePrefix >> sitefix2 >> sitename;
                // std::cout << sitename << std::endl;
                SiteType *sitetype = database.getSiteType(SiteType::NameString2Enum(sitename));
                if(sitetype == NULL){
                    // if (sitename == "IO") {
                    //     SiteType newsitetype1(SiteType::NameString2Enum(sitename+"A"));
                    //     newsitetype1.weight = 1;
                    //     newsitetype1.height = 1;
                    //     SiteType newsitetype2(SiteType::NameString2Enum(sitename+"B"));
                    //     newsitetype2.weight = 1;
                    //     newsitetype2.height = 1;
                    //     sitetype = database.addSiteType(newsitetype1);
                    //     sitetype = database.addSiteType(newsitetype2);
                    // } else {
                        // cout << sitename << endl;
                    SiteType newsitetype(SiteType::NameString2Enum(sitename));
                    sitetype = database.addSiteType(newsitetype);
                    // }
                    
                }else{
                    return false;
                }
                while (std::getline(fs, line) && line.find("END_SITE") == std::string::npos) {
                    if (line.empty()) {
                        continue; // ��������
                    }
                    std::string prefix;
                    int value;
                    if (line.find("WIDTH") != std::string::npos) {
                        std::istringstream iss(line);
                        iss >> prefix >> value;
                        sitetype->weight = value;
                    } else if (line.find("HEIGHT") != std::string::npos) {
                        std::istringstream iss(line);
                        iss >> prefix >> value;
                        sitetype->height = value;
                    } else if (line.find("CAPACITY") != std::string::npos 
                                || line.find("SETTING LOGIC") != std::string::npos) {
                        continue;
                    } else {
                        std::istringstream iss(line);
                        std::string lineprefix,logicname;
                        int value;
                        iss >> lineprefix >> logicname >> value;
                        // cout << logicname << ',' << value << endl ;
                        Resource *resource = database.getResource(Resource::NameString2Enum(logicname));
                        // Master *master = database.getMaster(Master::NameString2Enum(logicname));
                        if(resource == NULL){ 
                            Resource newresource(Resource::NameString2Enum(logicname));
                            resource = database.addResource(newresource);
                            // cout << Resource::NameEnum2String(resource->name) << endl;
                            // Master newmaster(Master::NameString2Enum(logicname));
                            // master = database.addMaster(newmaster);
                        }
                        sitetype->addResource(resource,value);
                        // sitetype->addMaster(master, value);
                        // sitetype.capacity[logicname] = value;   
                    }
                }
                // sites.push_back(sitetype);
            } else if (line.find("SITEMAP") != std::string::npos) {
                std::istringstream iss(line);
                std::string prefix;
                int nx,ny;
                iss >> prefix >> nx >> ny;
                database.setSiteMap(nx, ny);
                while (std::getline(fs, line) && line.find("END_SITEMAP") == std::string::npos) {
                    std::istringstream iss(line);
                    std::string pos;
                    std::string logicname;
                    iss >> pos >> logicname;
                    int ypos = pos.find("Y");
                    int x = std::stoi(pos.substr(1,ypos-1));
                    int y = std::stoi(pos.substr(ypos+1));
                    // std::cout << x << ',' << y ;
                    SiteType *sitetype = database.getSiteType(SiteType::NameString2Enum(logicname));
                    // cout << sitetype->NameEnum2String(sitetype->name) << endl;
                    if(sitetype == NULL){
                        return false;
                    }else{
                        Site * site = database.addSite(x, y, sitetype);
                        site->pack = new Pack(site->type,site);
                    }
                }
            }
        }
        fs.close();
        std::cout << "reading .scl successfully!" << std::endl;
        return true;
    }
    bool readFileClk() {
        string clk_filename = filename + "\\fpga.clk";
        std::ifstream fs(clk_filename);
        if (!fs.is_open()) {
            cerr<<"Read net file: "<<clk_filename<<" fail"<<endl;
            return false;
        }
        std::string line;
        while (std::getline(fs, line)) {
            if (line.empty()) {
                continue; // ��������
            }
            if (line.find("CLOCKREGIONS") != std::string::npos) {
                std::istringstream iss(line);
                std::string prefix;
                iss >> prefix >> database.crmap_nx >> database.crmap_ny;
                database.clkrgns.assign(database.crmap_nx, vector<ClkRgn*>(database.crmap_ny, NULL));
                database.hfcols.assign(database.sitemap_nx,vector<HfCol*>(2*database.crmap_ny,NULL));
                for (int y = 0; y < database.crmap_ny; y++) {
                    for (int x = 0; x < database.crmap_nx; x++) {
                        std::getline(fs, line);
                        std::istringstream iss(line);
                        std::string clockname;
                        int lx,hx,ly,hy;
                        iss >> clockname >> lx >> hx >> ly >> hy;
                        database.clkrgns[x][y] = new ClkRgn(clockname,lx,ly,hx,hy,x,y);
                    }
                }
            }
            else if (line.find("END_CLOCKREGIONS") != std::string::npos) {
                break;
            }
        }
        fs.close();
        std::cout << "reading .clk successfully!" << std::endl;
        return true;
    }
};