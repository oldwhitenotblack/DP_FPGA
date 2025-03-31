#include <iostream>
#include "include/global.h"
#include "include/Test.h"
#include "src/FileArchManage.cpp"
#include "src/FileNetManage.cpp"
#include "src/Database.h"
#include "src/pack/Packble.h"
#include "src/output.h"
using namespace db;
FileArchManage filearc;
FileNetManage filenet;
std::string benchmark = "1";
int main() {
    // int sum = add(2,4);
    // std::cout << sum << std::endl;
    if (filearc.readFileScl() &&
        filearc.readFileLib() &&
        filearc.readFileClk()
            ) {
        std::cout << "reading Arch files successful!" << std::endl;
    } else {
        std::cout << "read Arch file error" << std::endl;
        exit(-1);
    }
    if (filenet.readFileNodes(string("2")) &&
        filenet.readFileTiming(string("2")) &&
        filenet.readFileNets(string("2"))) {
        std::cout << "reading Benchmark" + benchmark + " files successful!" << std::endl;
    } else {
        std::cout << "read Benchmark file error" << std::endl;
        exit(-1);
    }
    database.setup();
    /* dff的pin检查： */
    // cout << "clkPinIdx：" << database.clkPinIdx << ',' 
    //     << "srPinIdx:" << database.srPinIdx << ',' 
    //     << "cePinIdx：" << database.cePinIdx << ','
    //     << "ffIPinIdx：" << database.ffIPinIdx << endl;
    /* 全局sites获取和pack绑定检查:*/
    // std::cout << database.sitemap_nx << std::endl;
    // for (int x = 0; x < database.sitemap_nx; x++) {
    //     for (int y = 0; y < database.sitemap_ny; y++) {
    //         Site *site = database.sites[x][y];
    //         if (site==NULL) continue;
    //         if (site->type->name == SiteType::PLB) {
    //             cout << "X" << site->x << "Y" << site->y << ":" << SiteType::NameEnum2String(site->type->name) << endl;
    //             cout << site->pack->instances.size() << endl;
    //             exit(-1);
    //         }
    //     }
    // }
    /* 映射关系检查： */
    // for (int i = 0; i < database.masters.size(); i++) {
    //     cout << Master::NameEnum2String(database.masters[i]->name) << ":";
    //     cout << Resource::NameEnum2String(database.masters[i]->resource->name) << endl;
    // }
    /* 全局instance和site绑定检查: */
    // cout << database.instances.size() << endl;
    // for (int i = 0; i < database.instances.size(); i++) {
    //     std::cout << database.instances[i]->pack->site->x 
    //         << ',' << database.instances[i]->pack->site->y << ":" << SiteType::NameEnum2String(database.instances[i]->pack->type->name)  << endl;
    // }
    /* site中pack资源数量的检查*/
    // Site *site = database.getSite(92,131);
    // for (int i = 0; i < site->pack->instances.size(); i++) {
    //     if (site->pack->instances[i] != NULL) {
    //         cout << site->pack->instances[i]->name << "的z是" 
    //             << site->pack->instances[i]->z << ",在site为" 
    //             << 92 << ',' << 131 << "坐标pack中的位置为" << i 
    //             << ',' << site->pack->instances[i]->packz << endl;
    //     }
    // }
    vector<Group> groups(database.instances.size());
    for (unsigned int i = 0; i < groups.size(); i++) {
        groups[i].instances.push_back(database.instances[i]);
        groups[i].id = i;
        groups[i].x = database.instances[i]->x;
        groups[i].y = database.instances[i]->y;
        groups[i].z = database.instances[i]->z;
    }

//    packble(groups);
    database.recInstConn();
    database.dplace();
    output("case_1.nodes.out");

    cout<<"test for datastructure"<<endl;
}